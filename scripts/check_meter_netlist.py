"""Trace the supplied TEL netlist from each meter terminal to firmware GPIO."""
from pathlib import Path
import re

root = Path(__file__).resolve().parents[1]
source = (root / '电表采集PCB.tel').read_text(encoding='utf-8-sig')
block = source.split('$NETS', 1)[1].split('$SCHEDULE', 1)[0]
nets = {}
name = None
for line in block.splitlines():
    if ';' in line:
        name, line = line.split(';', 1)
        name = name.strip().strip("'")
        nets[name] = set()
    if name is not None:
        nets[name].update(re.findall(r'\b[A-Za-z][A-Za-z0-9]*\.\d+\b', line))
pin_net = {}
for net, pins in nets.items():
    for pin in pins:
        if pin in pin_net:
            raise SystemExit(f'Duplicate pin: {pin}')
        pin_net[pin] = net


def require(condition, message):
    if not condition:
        raise SystemExit('FAIL ' + message)


def one(items, message):
    require(len(items) == 1, f'{message}: {items}')
    return items[0]


firmware = (root / 'src/meter_pulse.cpp').read_text(encoding='utf-8')
mapping = re.search(r'METER_GPIO\[METER_CHANNELS\]\s*=\s*\{([^}]+)\}', firmware)
require(mapping is not None, 'firmware GPIO table missing')
gpios = [int(pin.strip()) for pin in mapping.group(1).split(',')]
require(len(gpios) == 12 and len(set(gpios)) == 12, 'expected 12 unique firmware GPIOs')
gates = {1: 2, 3: 4, 5: 6, 9: 8, 11: 10, 13: 12}
# ESP32-S3-DevKitC-1 J1, numbered from the 3V3 end.
header = {4: 4, 5: 5, 6: 6, 7: 7, 15: 8, 8: 12,
          9: 15, 10: 16, 11: 17, 12: 18, 13: 19, 14: 20}
require(pin_net['H1.1'] == 'VCC', 'H1.1 must supply the 3.3V VCC rail')
require(pin_net['H1.21'] == '5V_MAIN', 'H1.21 must connect to 5V_MAIN')
require(pin_net['H1.22'] == 'GND', 'H1.22 must connect to GND')
for ch, gpio in enumerate(gpios, 1):
    terminal = f'CN{ch}'
    require(pin_net[f'{terminal}.2'] == 'GND', f'{terminal}.2 ground')
    opto_pin = one([p for p in nets[pin_net[f'{terminal}.1']]
                    if re.fullmatch(r'U\d+\.2', p)], f'{terminal} optocoupler cathode')
    opto = opto_pin.split('.')[0]
    require(pin_net[f'{opto}.6'] == '5V_MAIN', f'{opto} detector supply')
    require(pin_net[f'{opto}.5'] == 'GND', f'{opto} detector ground')
    output_net = pin_net[f'{opto}.4']
    # Find the series resistor whose other end reaches an RC net.
    series = []
    pullups = []
    for pin in nets[output_net]:
        if not re.fullmatch(r'R\d+\.[12]', pin):
            continue
        ref, number = pin.split('.')
        other = pin_net.get(f'{ref}.{3-int(number)}', '')
        if re.fullmatch(r'RC\d+', other):
            series.append((ref, other))
        if other == 'VCC':
            pullups.append(ref)
    resistor, rc = one(series, f'{terminal} RC series resistor')
    one(pullups, f'{terminal} 3.3V pull-up')
    gate = one([p for p in nets[rc] if re.fullmatch(r'U1[45]\.\d+', p)], f'{terminal} inverter input')
    chip, number = gate.split('.')
    require(int(number) in gates, f'{gate} is not a 74LVC14 input')
    require(pin_net[f'{chip}.14'] == 'VCC' and pin_net[f'{chip}.7'] == 'GND', f'{chip} supply')
    actual_net = pin_net[f'{chip}.{gates[int(number)]}']
    require(actual_net == f'GPIO{gpio}', f'{terminal} net {actual_net}, firmware GPIO{gpio}')
    require(pin_net[f'H1.{header[gpio]}'] == actual_net, f'{terminal} header pin mismatch')
    print(f'PASS {terminal} -> {opto} -> {resistor}/{rc} -> {chip} -> GPIO{gpio} -> H1.{header[gpio]}')
print('PASS all 12 channels match firmware and standard DevKitC-1 J1 numbering')
print('INFO VCC is connected to H1.1 (3V3)')
h2 = sorted(pin for pin in pin_net if pin.startswith('H2.'))
print('INFO H2 connected pins:', ', '.join(h2) if h2 else 'none')
print('INFO all CNx.2 pins share ESP32 GND: this carrier does not provide galvanic isolation')
