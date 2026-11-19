"""Skip unavailable LMKD only when the session declares its host memory guard."""
import sys
from pathlib import Path

path = Path(sys.argv[1])
text = path.read_text()
method = ".method private static writeLmkd(Ljava/nio/ByteBuffer;)V"
start = text.index(method)
end = text.index(".end method", start)
original = text[start:end]
if "const-wide/16 v4, 0x3e8" not in original or "->openLmkdSocket()Z" not in original:
    raise SystemExit("Unexpected ProcessList implementation")
anchor = "    .prologue\n"
if original.count(anchor) != 1:
    raise SystemExit("Unexpected writeLmkd prologue")
guard = """    .prologue
    const-string v0, "ro.kindleroid.host_memory_guard"
    const/4 v1, 0x0
    invoke-static {v0, v1}, Landroid/os/SystemProperties;->getBoolean(Ljava/lang/String;Z)Z
    move-result v0
    if-eqz v0, :kindleroid_use_lmkd
    return-void
    :kindleroid_use_lmkd
"""
path.write_text(text[:start] + original.replace(anchor, guard) + text[end:])
