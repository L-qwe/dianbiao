"""Compile the production plain-HTTP pre-parser against recording sockets."""
from pathlib import Path
import subprocess
root=Path(__file__).resolve().parents[1]
out=root/'.pio/host-tests';out.mkdir(parents=True,exist_ok=True)
source=(root/'src/web_portal.cpp').read_text(encoding='utf-8')
transport=source[source.index('class PlainHttpWebServer:'):source.index('namespace {')]
fixture=(root/'test/http_transport_test.cpp').read_text(encoding='utf-8')
cpp=out/'http_transport.cpp';exe=out/'http_transport.exe'
cpp.write_text(fixture.replace('// PRODUCTION_HTTP_TRANSPORT',transport),encoding='utf-8')
subprocess.run(['python','-m','ziglang','c++','-std=c++17','-O0','-I'+str(root/'test/host'),str(cpp),'-o',str(exe)],check=True)
subprocess.run([str(exe)],check=True)
