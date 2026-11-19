/* SPDX-License-Identifier: MIT
 * Private gralloc/bridge ABI. Pixel bytes stay at offset 0 for old readers.
 * One metadata page follows the page-aligned pixels. Single writer (fb_post),
 * interprocess ARM barriers: odd = copying, even = committed. No shared locks.
 */
#ifndef KD_FRAME_PROTOCOL_H
#define KD_FRAME_PROTOCOL_H
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#define KD_FRAME_MAGIC UINT32_C(0x4b444631)
#define KD_FRAME_VERSION 1
#define KD_FRAME_PAGE 4096u
struct kd_frame_meta {
 uint32_t magic,version,header_bytes,width,height,stride,pixel_bytes,sequence;
};
static inline size_t kd_frame_offset(size_t pixels){return (pixels+KD_FRAME_PAGE-1)&~(size_t)(KD_FRAME_PAGE-1);}
static inline void kd_frame_init(struct kd_frame_meta*m,uint32_t w,uint32_t h,uint32_t stride){
 memset(m,0,sizeof(*m));m->version=KD_FRAME_VERSION;m->header_bytes=sizeof(*m);
 m->width=w;m->height=h;m->stride=stride;m->pixel_bytes=stride*h;
 __atomic_store_n(&m->magic,KD_FRAME_MAGIC,__ATOMIC_RELEASE);
}
static inline void kd_frame_begin(struct kd_frame_meta*m){
 uint32_t seq=__atomic_load_n(&m->sequence,__ATOMIC_RELAXED);
 __atomic_store_n(&m->sequence,seq+1,__ATOMIC_SEQ_CST);
 /* Prevent any pixel stores before the odd sequence is globally visible. */
 __atomic_thread_fence(__ATOMIC_SEQ_CST);
}
static inline void kd_frame_end(struct kd_frame_meta*m){
 uint32_t seq=__atomic_load_n(&m->sequence,__ATOMIC_RELAXED);
 __atomic_store_n(&m->sequence,seq+1,__ATOMIC_RELEASE);
}
#endif
