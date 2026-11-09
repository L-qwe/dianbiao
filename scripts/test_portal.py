"""Compile production JSON validators and retry timing on the host (no device IO)."""
from pathlib import Path
import subprocess
root=Path(__file__).resolve().parents[1]
out=root/'.pio/host-tests'
out.mkdir(parents=True,exist_ok=True)
config=(root/'src/app_config.cpp').read_text(encoding='utf-8')
globals=config[config.index('const char* FIRMWARE_VERSION'):config.index('// ── NVS 命名空间')]
fixture=(root/'test/portal_settings_test.cpp').read_text(encoding='utf-8')
cpp=out/'portal.cpp';exe=out/'portal.exe'
cpp.write_text(fixture.replace('// CONFIG_GLOBALS',globals),encoding='utf-8')
subprocess.run(['python','-m','ziglang','c++','-std=c++17','-O0','-DARDUINOJSON_ENABLE_ARDUINO_STRING=1','-DARDUINOJSON_ENABLE_ARDUINO_STREAM=0','-DARDUINOJSON_ENABLE_ARDUINO_PRINT=0','-DARDUINOJSON_ENABLE_PROGMEM=0','-I'+str(root/'test/host'),'-I'+str(root/'include'),'-I'+str(root/'.pio/libdeps/esp32-s3-devkitc-1/ArduinoJson/src'),str(cpp),'-o',str(exe)],check=True)
subprocess.run([str(exe)],check=True)
