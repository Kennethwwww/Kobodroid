"""Patch only the known AOSP7 init property's return-code decision, not Bionic."""
from pathlib import Path
import hashlib,json
base=Path(__file__).resolve().parents[1]
source=base.parent/'android7-prototype/rootfs/init'
b=source.read_bytes()
assert hashlib.sha256(b).hexdigest()=='8fe6b7cb2fba5b89adb7bbc0f2dd9562f6e038e90137b4efb8bce90f1d140e2c'
patch=(base/'build/property-init-compat.bin').read_bytes()
assert patch.hex()=='80b567f003f828b110f1020f02d00120f6f7f2fc80bd'
off=0x9748
assert b[off:off+32].hex()=='80b567f003f800b980bd04490320794405f0cef80120f6f7effc00bff0e00900'
new=b[:off]+patch+b'\x00\xbf'*((32-len(patch))//2)+b[off+32:]
assert len(new)==len(b)
assert all(off<=i<off+32 for i,(x,y) in enumerate(zip(b,new)) if x!=y)
out=base/'build/init-kobo-compat';out.write_bytes(new);out.chmod(0o750)
(base/'build/init-compat-manifest.json').write_text(json.dumps({'source_sha256':hashlib.sha256(b).hexdigest(),'patched_sha256':hashlib.sha256(new).hexdigest(),'function_va':'0x11748','patch_offset':off,'region_bytes':32,'source_contract':'__system_property_area_init returns 0, -1, or -2; only 0/-2 return; other values exit(1)','scope':'separate Kobo core-test init; no host or original Kindle binary changed'},indent=2)+'\n')
