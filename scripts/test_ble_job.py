"""Verify the actual BLE notification loop and queued-Wi-Fi completion rules."""
from pathlib import Path
import subprocess
root=Path(__file__).resolve().parents[1]
out=root/'.pio/host-tests';out.mkdir(parents=True,exist_ok=True)
source=(root/'src/ble_provisioning.cpp').read_text(encoding='utf-8')
notify=source[source.index('static const unsigned long CONNECT_TIMEOUT_MS'):source.index('class WifiCredentialsCallback')]
loop=source[source.index('void andon_ble_loop('):source.index('String andon_ble_status(')]
fixture=(root/'test/ble_job_test.cpp').read_text(encoding='utf-8')
cpp=out/'ble_job.cpp';exe=out/'ble_job.exe'
cpp.write_text(fixture.replace('// NOTIFY_SOURCE',notify).replace('// LOOP_SOURCE',loop),encoding='utf-8')
subprocess.run(['python','-m','ziglang','c++','-std=c++17','-O0','-I'+str(root/'test/mqtt_mocks'),'-I'+str(root/'test/host'),str(cpp),'-o',str(exe)],check=True)
subprocess.run([str(exe)],check=True)
