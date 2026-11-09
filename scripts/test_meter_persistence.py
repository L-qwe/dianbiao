"""Verify production counter persistence against recording/failing NVS doubles."""
from pathlib import Path
import subprocess
root=Path(__file__).resolve().parents[1]
out=root/'.pio/host-tests';out.mkdir(parents=True,exist_ok=True)
config=(root/'src/app_config.cpp').read_text(encoding='utf-8')
globals=config[config.index('const char* FIRMWARE_VERSION'):config.index('// ── NVS 命名空间')]
source=(root/'src/meter_pulse.cpp').read_text(encoding='utf-8')
fixture=(root/'test/meter_persistence_test.cpp').read_text(encoding='utf-8')
cpp=out/'meter_persistence.cpp';exe=out/'meter_persistence.exe'
cpp.write_text(fixture.replace('// CONFIG_GLOBALS',globals).replace('// METER_SOURCE',source),encoding='utf-8')
subprocess.run(['python','-m','ziglang','c++','-std=c++17','-O0','-I'+str(root/'test/meter_mocks'),'-I'+str(root/'test/mqtt_mocks'),'-I'+str(root/'test/host'),'-I'+str(root/'include'),str(cpp),'-o',str(exe)],check=True)
subprocess.run([str(exe)],check=True)
