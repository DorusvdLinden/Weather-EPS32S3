"""PlatformIO pre-build hook (see platformio.ini `extra_scripts`). Regenerates
src/generated/icons_data.{h,cpp} from assets/icons/ so the baked icons never
drift from source PNGs. Safe to no-op (see convert_assets.py) when Pillow
isn't installed in whatever Python invoked `pio run` - the committed
generated files are then used unchanged.
"""
import os
import subprocess
import sys

Import("env")  # noqa: F821 - injected by PlatformIO

# SCons execs this file without setting __file__, so locate it via the
# project dir PlatformIO injects into `env` instead.
PROJECT_DIR = env.get("PROJECT_DIR")  # noqa: F821
SCRIPT_PATH = os.path.join(PROJECT_DIR, "tools", "convert_assets.py")
subprocess.run([sys.executable, SCRIPT_PATH], check=False)
