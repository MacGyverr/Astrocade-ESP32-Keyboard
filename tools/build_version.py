"""Keep the ESP-IDF image version in sync with the firmware build flag."""
import json
import re

Import("env")

defines = env.ParseFlags(env.get("BUILD_FLAGS", []))["CPPDEFINES"]
values = [d[1] for d in defines if isinstance(d, (list, tuple)) and d[0] == "ASTROCADE_FIRMWARE_VERSION"]
if len(values) != 1:
    raise ValueError("Set ASTROCADE_FIRMWARE_VERSION exactly once in build_flags")
version = json.loads(values[0])
if not isinstance(version, str) or not re.fullmatch(r"\d+\.\d+\.\d+(?:[-+][A-Za-z0-9.-]+)?", version) or len(version) > 31:
    raise ValueError("Firmware version must be a quoted version such as 1.0.0 (at most 31 characters)")
board = env.BoardConfig()
board.update("build.cmake_extra_args", board.get("build.cmake_extra_args", "") + " -DPROJECT_VER=" + version)
