/* libkdashmem: memfd-backed replacement for the libcutils ashmem API on kernels without /dev/ashmem
 * (Kindle 5.15 and the kindlelike VM have CONFIG_ASHMEM unset but memfd). Loaded with LD_PRELOAD so it
 * interposes libcutils' five exported ashmem_* functions. Semantics follow Android 10's memfd mode:
 * read-only protection is a F_SEAL_FUTURE_WRITE seal, size comes from fstat, pinning is a no-op.
 * Code that drives the device itself (Chromium/WebView's own ashmem: open("/dev/ashmem") + ASHMEM_* ioctls, so
 * nothing in libcutils to interpose) is covered too: opening exactly /dev/ashmem returns a sealable memfd, and the
 * ASHMEM_* ioctls are emulated on memfds only. Every other open/ioctl goes straight to the kernel, as bionic does. */
#include <errno.h>
#include <stdarg.h>
#include <linux/types.h>
#include <linux/ashmem.h>
#include <sys/ioctl.h>
#include <fcntl.h>
#include <stddef.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <unistd.h>

#define EXPORT __attribute__((visibility("default")))
#ifndef __NR_memfd_create
#define __NR_memfd_create 385 /* ARM EABI */
#endif
#define KD_MFD_ALLOW_SEALING 0x0002U
#define KD_F_ADD_SEALS 1033
#define KD_F_GET_SEALS 1034
#define KD_F_SEAL_SEAL 0x0001
#define KD_F_SEAL_FUTURE_WRITE 0x0010
#define ASHMEM_NOT_PURGED 0

EXPORT int ashmem_create_region(const char* name, size_t size) {
  char label[64] = "ashmem";
  if (name && name[0]) {
    strncpy(label, name, sizeof label - 1);
    label[sizeof label - 1] = 0;
  }
  /* /dev/ashmem was opened without O_CLOEXEC; keep that inheritance behaviour. */
  int fd = syscall(__NR_memfd_create, label, KD_MFD_ALLOW_SEALING);
  if (fd < 0) return -1;
  if (ftruncate(fd, (off_t)size) < 0) {
    int e = errno;
    close(fd);
    errno = e;
    return -1;
  }
  return fd;
}

EXPORT int ashmem_set_prot_region(int fd, int prot) {
  int seals = fcntl(fd, KD_F_GET_SEALS);
  if (seals < 0) return -1;
  if (prot & PROT_WRITE) {
    /* Like ashmem, protection can only be narrowed. */
    if (seals & KD_F_SEAL_FUTURE_WRITE) {
      errno = EINVAL;
      return -1;
    }
    return 0;
  }
  if (seals & KD_F_SEAL_FUTURE_WRITE) return 0;
  return fcntl(fd, KD_F_ADD_SEALS, KD_F_SEAL_FUTURE_WRITE | KD_F_SEAL_SEAL) < 0 ? -1 : 0;
}

EXPORT int ashmem_pin_region(int fd, size_t offset, size_t len) {
  (void)fd; (void)offset; (void)len;
  return ASHMEM_NOT_PURGED;
}

EXPORT int ashmem_unpin_region(int fd, size_t offset, size_t len) {
  (void)fd; (void)offset; (void)len;
  return 0;
}

EXPORT int ashmem_get_size_region(int fd) {
  struct stat st;
  if (fstat(fd, &st) < 0) return -1;
  if (!S_ISREG(st.st_mode)) {
    errno = ENOTTY;
    return -1;
  }
  return (int)st.st_size;
}

/* --- /dev/ashmem opened directly (Chromium) ------------------------------------------------------------------ */
#define KD_MFD_CLOEXEC 0x0001U
static int kd_ashmem_device(const char* path) { return path && !strcmp(path, "/dev/ashmem"); }
static int kd_ashmem_open(int flags) {
  return syscall(__NR_memfd_create, "ashmem", KD_MFD_ALLOW_SEALING | ((flags & O_CLOEXEC) ? KD_MFD_CLOEXEC : 0));
}
/* Same as bionic: 32-bit opens always get O_LARGEFILE. */
static int kd_openat(int dirfd, const char* path, int flags, int mode) {
  return syscall(__NR_openat, dirfd, path, flags | O_LARGEFILE, mode);
}
EXPORT int open(const char* path, int flags, ...) {
  int mode = 0;
  if (flags & O_CREAT) { va_list ap; va_start(ap, flags); mode = va_arg(ap, int); va_end(ap); }
  return kd_ashmem_device(path) ? kd_ashmem_open(flags) : kd_openat(AT_FDCWD, path, flags, mode);
}
EXPORT int __open_2(const char* path, int flags) {
  return kd_ashmem_device(path) ? kd_ashmem_open(flags) : kd_openat(AT_FDCWD, path, flags, 0);
}
EXPORT int openat(int dirfd, const char* path, int flags, ...) {
  int mode = 0;
  if (flags & O_CREAT) { va_list ap; va_start(ap, flags); mode = va_arg(ap, int); va_end(ap); }
  return kd_ashmem_device(path) ? kd_ashmem_open(flags) : kd_openat(dirfd, path, flags, mode);
}
EXPORT int __openat_2(int dirfd, const char* path, int flags) {
  return kd_ashmem_device(path) ? kd_ashmem_open(flags) : kd_openat(dirfd, path, flags, 0);
}

static int kd_is_memfd(int fd) {
  char link[32] = "/proc/self/fd/", digits[12], target[16];
  if (fd < 0) return 0;
  int n = 0;
  do { digits[n++] = (char)('0' + fd % 10); fd /= 10; } while (fd);
  size_t at = strlen(link);
  while (n) link[at++] = digits[--n];
  link[at] = 0;
  ssize_t got = readlink(link, target, sizeof target);
  return got >= 7 && !memcmp(target, "/memfd:", 7);
}
static int kd_ashmem_ioctl(int fd, int request, void* arg) {
  struct stat st;
  int seals;
  switch (request) {
    case ASHMEM_SET_NAME: return 0;  /* the memfd name is fixed at creation */
    case ASHMEM_GET_NAME: strcpy((char*)arg, "ashmem"); return 0;
    case ASHMEM_SET_SIZE: return ftruncate(fd, (off_t)(size_t)arg);
    case ASHMEM_GET_SIZE: return fstat(fd, &st) < 0 ? -1 : (int)st.st_size;
    case ASHMEM_SET_PROT_MASK: return ashmem_set_prot_region(fd, (int)(unsigned long)arg);
    case ASHMEM_GET_PROT_MASK:
      seals = fcntl(fd, KD_F_GET_SEALS);
      if (seals < 0) return -1;
      return (seals & KD_F_SEAL_FUTURE_WRITE) ? PROT_READ | PROT_EXEC : PROT_READ | PROT_WRITE | PROT_EXEC;
    case ASHMEM_PIN: return ASHMEM_NOT_PURGED;
    case ASHMEM_UNPIN: return 0;
    case ASHMEM_GET_PIN_STATUS: return ASHMEM_IS_PINNED;
    case ASHMEM_PURGE_ALL_CACHES: return 0;
  }
  errno = ENOTTY;
  return -1;
}
EXPORT int ioctl(int fd, int request, ...) {
  va_list ap;
  va_start(ap, request);
  void* arg = va_arg(ap, void*);
  va_end(ap);
  if (_IOC_TYPE((unsigned)request) == __ASHMEMIOC && kd_is_memfd(fd)) return kd_ashmem_ioctl(fd, request, arg);
  return syscall(__NR_ioctl, fd, request, arg);
}
