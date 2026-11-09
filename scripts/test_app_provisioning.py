"""Run production APP discovery, BLE credentials and registration with host IO mocks."""
from pathlib import Path
import re
import subprocess

root = Path(__file__).resolve().parents[1]
out = root / '.pio/host-tests'
out.mkdir(parents=True, exist_ok=True)

def source(name):
    return re.sub(r'^#include[^\n]*\n', '', (root / 'src' / name).read_text(encoding='utf-8'), flags=re.M)

portal = source('web_portal.cpp')
route = portal[portal.index('    server->on("/api/device-info"'):portal.index('    server->on("/api/config"')]
fixture = (root / 'test/app_provisioning_test.cpp').read_text(encoding='utf-8')
for marker, code in (
    ('// DEVICE_INFO_SOURCE', source('device_info.cpp')),
    ('// BLE_SOURCE', source('ble_provisioning.cpp')),
    ('// REPORT_SOURCE', source('device_access_reporter.cpp')),
    ('// HTTP_ROUTE_SOURCE', route),
):
    fixture = fixture.replace(marker, code)
cpp = out / 'app_provisioning.cpp'
exe = out / 'app_provisioning.exe'
cpp.write_text(fixture, encoding='utf-8')
subprocess.run([
    'python', '-m', 'ziglang', 'c++', '-std=c++17', '-O0',
    '-DARDUINOJSON_ENABLE_ARDUINO_STRING=1', '-DARDUINOJSON_ENABLE_ARDUINO_STREAM=0',
    '-DARDUINOJSON_ENABLE_ARDUINO_PRINT=0', '-DARDUINOJSON_ENABLE_PROGMEM=0',
    '-I' + str(root / 'test/host'), '-I' + str(root / 'include'),
    '-I' + str(root / '.pio/libdeps/esp32-s3-devkitc-1/ArduinoJson/src'),
    str(cpp), '-o', str(exe),
], check=True)
subprocess.run([str(exe)], check=True)
