Import("env")

import sys

if sys.platform == "win32":
    env.Append(LIBS=["ws2_32", "winhttp"])
    if env["PIOENV"] == "calendar-host":
        # Keep the preview portable without MinGW GCC/C++ runtime DLLs.
        # Windows system libraries remain dynamically linked.
        # -static also includes POSIX MinGW's winpthread runtime; ws2_32 and
        # winhttp use Windows import libraries and still resolve to system DLLs.
        env.Append(LINKFLAGS=["-static-libgcc", "-static-libstdc++", "-static"])
else:
    env.Append(LIBS=["curl"])
