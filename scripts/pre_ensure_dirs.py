# Pre-build hook: ensure build subdirectories exist before SCons starts compiling.
# Fixes "opening dependency file .pio/build/esp32dev/src/*.d: No such file or directory"
# seen on some Windows machines (Defender/OneDrive IO contention or SCons dir cache).
import os

from SCons.Script import DefaultEnvironment

env = DefaultEnvironment()
build_dir = env.subst("$BUILD_DIR")  # e.g. .pio/build/esp32dev

for sub in ("src",):
    p = os.path.join(build_dir, sub)
    try:
        os.makedirs(p, exist_ok=True)
        print("[pre-ensure] dir ok: %s" % p)
    except OSError as e:
        print("[pre-ensure] mkdir fail %s: %s" % (p, e))