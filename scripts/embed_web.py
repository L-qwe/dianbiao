from pathlib import Path
Import("env")
root = Path(env.subst("$PROJECT_DIR"))
page = (root / "web/index.html").read_text(encoding="utf-8")
assert ')DBWEB"' not in page
header = '#pragma once\n#include <Arduino.h>\nstatic const char WEB_PAGE[] PROGMEM = R"DBWEB(' + page + ')DBWEB";\n'
target = root / "include/web_assets.h"
if not target.exists() or target.read_text(encoding="utf-8") != header:
    target.write_text(header, encoding="utf-8")
