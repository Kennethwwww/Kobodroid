"""Keep cross-compiling ART's 32-bit data below 4 GiB on a translated host."""
import argparse
from pathlib import Path


HOST = "defined(__linux__) && defined(__x86_64__) && !defined(__ANDROID__)"
HELPER = "#if USE_ART_LOW_4G_ALLOCATOR\nstatic inline void* TryMemMapLow4GB"
BRANCH = """  actual = mmap(addr, length, prot, flags, fd, offset);
#endif
  return actual;
}"""
FALLBACK = f"""  actual = mmap(addr, length, prot, flags, fd, offset);
#if {HOST}
  // x86 translation can ignore MAP_32BIT. Retry with low, non-fixed hints.
  if (low_4gb && addr == nullptr && actual != MAP_FAILED &&
      reinterpret_cast<uintptr_t>(actual) + length >= 4 * GB) {{
    munmap(actual, length);
    actual = MAP_FAILED;
    for (uintptr_t hint = 64 * MB; hint < 4 * GB - length; hint += 16 * MB) {{
      actual = TryMemMapLow4GB(reinterpret_cast<void*>(hint), length, prot,
                               flags & ~MAP_32BIT, fd, offset);
      if (actual != MAP_FAILED) {{
        break;
      }}
    }}
    if (actual == MAP_FAILED) {{
      errno = ENOMEM;
    }}
  }}
#endif
#endif
  return actual;
}}"""


def patch(aosp):
    source = aosp / "art/runtime/mem_map.cc"
    text = source.read_text()
    if FALLBACK in text:
        return
    if text.count(HELPER) != 1 or text.count(BRANCH) != 1:
        raise ValueError("Unexpected ART low-memory allocator")
    text = text.replace(HELPER, f"#if USE_ART_LOW_4G_ALLOCATOR || ({HOST})\n"
                                "static inline void* TryMemMapLow4GB")
    source.write_text(text.replace(BRANCH, FALLBACK))


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("aosp", type=Path)
    patch(parser.parse_args().aosp)
