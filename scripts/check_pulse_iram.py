"""Check the built ESP32-S3 ISR call chain and state are in internal memory."""
from pathlib import Path
import os
import re
import subprocess

root = Path(__file__).resolve().parents[1]
pio = Path(os.environ.get('PLATFORMIO_CORE_DIR', Path.home() / '.platformio'))
objdump = pio / 'packages/toolchain-xtensa-esp32s3/bin/xtensa-esp32s3-elf-objdump.exe'
elf = root / '.pio/build/esp32-s3-devkitc-1/firmware.elf'
table = subprocess.check_output([str(objdump), '-t', str(elf)], text=True)
symbols = {}
for line in table.splitlines():
    fields = line.split()
    if len(fields) >= 6:
        symbols[fields[-1]] = fields

code = ('_Z9meter_isrPv', '__onPinInterrupt', 'micros',
        'esp_timer_get_time', 'gpio_intr_service', 'gpio_isr_loop')
data = ('_ZL7s_delta', '_ZL12s_edge_state', '_ZL13s_isr_context', '__pinInterruptHandlers')
for name in code + data:
    if name not in symbols:
        raise SystemExit(f'FAIL missing ISR symbol: {name}')
    fields = symbols[name]
    prefix = '.iram' if name in code else '.dram'
    if not any(field.startswith(prefix) for field in fields):
        raise SystemExit(f'FAIL {name} is outside {prefix}: {fields}')
    print(f'PASS {name}: {fields[0]} in {next(f for f in fields if f.startswith(prefix))}')
# Xtensa objdump --disassemble=<symbol> may select the literal pool instead of
# the actual function. Bound the disassembly by its ELF address and size.
isr = symbols['_Z9meter_isrPv']
start = int(isr[0], 16)
size = int(isr[-2], 16)
body = subprocess.check_output([str(objdump), '-d', f'--start-address={start}',
                                f'--stop-address={start + size}', str(elf)], text=True)
if '<_Z9meter_isrPv>:' not in body or not re.search(r'\bentry\b', body):
    raise SystemExit('FAIL actual pulse ISR instructions were not disassembled')
if re.search(r'\bcallx\d*\b', body):
    raise SystemExit('FAIL pulse ISR contains an unverified indirect call')
calls = re.findall(r'\bcall(?:0|4|8|12)\b[^\n]*<([^>]+)>', body)
for target in calls:
    if target not in code:
        raise SystemExit(f'FAIL pulse ISR calls an unverified function: {target}')
print('PASS pulse ISR direct calls:', ', '.join(calls) if calls else 'none')
print('PASS built pulse ISR call chain is in IRAM and state is in internal DRAM')
