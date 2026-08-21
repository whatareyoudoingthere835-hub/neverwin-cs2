#!/usr/bin/env python3
# Выключает конфликтующую с mingw-заголовками декларацию phnt (#if 0).
# Использование: patch_phnt_conflict.py <header> <имя функции>
import re, sys

header, name = sys.argv[1], sys.argv[2]
lines = open(header, encoding="utf-8", errors="replace").read().split("\n")

idx = None
for i, ln in enumerate(lines):
    if ln.strip().startswith(name + "("):
        idx = i
        break
if idx is None:
    sys.exit(f"not found: {name}")

# начало декларации: ближайший NTSYSAPI/NTKERNELAPI выше (не дальше 6 строк)
start = idx
for j in range(idx, max(idx - 6, -1), -1):
    if re.match(r"^(NTSYSAPI|NTKERNELAPI)\s*$", lines[j].strip()):
        start = j
        break
# конец: первая строка с ');' начиная с idx
end = idx
for j in range(idx, min(idx + 20, len(lines))):
    if ");" in lines[j]:
        end = j
        break

lines.insert(end + 1, "#endif // конфликт с mingw")
lines.insert(start, "#if 0 // конфликт с mingw: в winnt.h/других уже объявлено иначе")
open(header, "w", encoding="utf-8").write("\n".join(lines))
print(f"выключено: {name} в {header} (строки {start}-{end})")
