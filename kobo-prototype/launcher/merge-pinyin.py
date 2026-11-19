"""Merge weighted Rime pinyin-simp phrases into the existing Kobo table."""
from collections import defaultdict
from pathlib import Path
import re
import sys

output, source = map(Path, sys.argv[1:])
table = {}
for line in output.read_text().splitlines():
    if line and not line.startswith("#"):
        code, *words = line.split()
        table[code] = words

phrases = defaultdict(list)
for line in source.read_text().splitlines():
    fields = line.split("\t")
    if len(fields) != 3:
        continue
    word, spelling, weight = fields
    code = spelling.replace(" ", "")
    if len(word) > 1 and re.fullmatch("[a-z]+", code) and weight.isdecimal():
        phrases[code].append((int(weight), word))
for code, entries in phrases.items():
    words = table.setdefault(code, [])
    for _, word in sorted(entries, reverse=True):
        if word not in words:
            words.append(word)
output.write_text("# Rime pinyin-simp, Apache-2.0. See pinyin-NOTICE.txt.\n"
                  + "".join(f"{code} {' '.join(words)}\n" for code, words in sorted(table.items())))
print(f"{len(table)} codes, {sum(len(words) for words in table.values())} candidates")
