"""Keep the Android renderer buildable without the optional Widevine target."""
import sys
from pathlib import Path


source = Path(sys.argv[1]) / "components/cdm/renderer/key_system_support_update.cc"
text = source.read_text()
check = "  DCHECK_NE(key_system, kWidevineKeySystem);\n"
guarded = "#if BUILDFLAG(ENABLE_WIDEVINE)\n" + check + "#endif\n"
if guarded not in text:
    if text.count(check) != 1:
        raise ValueError("Unexpected Chromium renderer key-system check")
    source.write_text(text.replace(check, guarded))
