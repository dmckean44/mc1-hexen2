"""Lists the functions with the most cycles per byte in an mr32sim perf log, up to a code size budget.

usage: hotlist.py PERF_LOG SYMBOL_SIZES BUDGET_BYTES > hot_functions.txt

SYMBOL_SIZES is the output of `nm -S` for the same program. The linker places these functions
first, in this order, so the code that runs every frame shares the instruction cache well.
"""
import re
import sys

perf_log, sizes_file, budget = sys.argv[1], sys.argv[2], int(sys.argv[3])

sizes = {}
for line in open(sizes_file):
    fields = line.split()
    if len(fields) == 4 and fields[2] in 'Tt':
        sizes[fields[3]] = int(fields[1], 16)

cycles = []
in_table = False
for line in open(perf_log):
    if line.startswith('Address (hex)'):
        in_table = True
        continue
    m = re.match(r'0x[0-9a-f]+\s+(\d+)\s+(\S+)', line) if in_table else None
    if m:
        cycles.append((int(m.group(1)), m.group(2)))

executed = [(count, name) for count, name in cycles if count > 0 and name in sizes]
executed.sort(key=lambda row: -row[0] / sizes[row[1]])

used = 0
for count, name in executed:
    if used + sizes[name] > budget:
        continue
    used += sizes[name]
    print(name)
