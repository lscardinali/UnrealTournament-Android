#!/usr/bin/env python3
"""Hook utandroid/SDL_utandroid.c into an SDL 3.4.x source tree. Idempotent.

    python -I apply_utandroid.py <sdl-src-dir>
"""
import os
import shutil
import sys

HERE = os.path.dirname(os.path.abspath(__file__))


def edit(path, old, new, marker):
    with open(path, encoding="utf-8", newline="") as f:
        s = f.read()
    if marker in s:
        return
    if "\r\n" in s:  # a checkout with core.autocrlf=true
        old, new = old.replace("\n", "\r\n"), new.replace("\n", "\r\n")
    if old not in s:
        raise SystemExit(f"{path}: anchor not found: {old!r}")
    with open(path, "w", encoding="utf-8", newline="") as f:
        f.write(s.replace(old, new, 1))


def main():
    src = sys.argv[1]
    dst = os.path.join(src, "src", "utandroid")
    os.makedirs(dst, exist_ok=True)
    for fn in ("SDL_utandroid.c", "SDL_utandroid.h"):
        shutil.copyfile(os.path.join(HERE, "utandroid", fn), os.path.join(dst, fn))

    edit(os.path.join(src, "CMakeLists.txt"),
         'dep_option(SDL_DUMMYAUDIO ',
         'set_option(SDL_UTANDROID             "UTAndroid sound and gamepad" OFF)\ndep_option(SDL_DUMMYAUDIO ',
         "SDL_UTANDROID")
    edit(os.path.join(src, "CMakeLists.txt"),
         'if(SDL_CAMERA)',
         'if(SDL_UTANDROID)\n  set(HAVE_UTANDROID TRUE)\n'
         '  sdl_glob_sources("${SDL3_SOURCE_DIR}/src/utandroid/*.c")\nendif()\n\nif(SDL_CAMERA)',
         "HAVE_UTANDROID")
    edit(os.path.join(src, "include", "build_config", "SDL_build_config.h.cmake"),
         '#cmakedefine SDL_AUDIO_DRIVER_DISK 1',
         '#cmakedefine SDL_AUDIO_DRIVER_DISK 1\n#cmakedefine SDL_UTANDROID 1',
         "SDL_UTANDROID")
    edit(os.path.join(src, "src", "audio", "SDL_audio.c"),
         '#ifdef SDL_AUDIO_DRIVER_DUMMY\n    &DUMMYAUDIO_bootstrap,',
         '#ifdef SDL_UTANDROID\n    &UTANDROIDAUDIO_bootstrap,\n#endif\n'
         '#ifdef SDL_AUDIO_DRIVER_DUMMY\n    &DUMMYAUDIO_bootstrap,',
         "UTANDROIDAUDIO_bootstrap")
    edit(os.path.join(src, "src", "audio", "SDL_sysaudio.h"),
         'extern AudioBootStrap DUMMYAUDIO_bootstrap;',
         'extern AudioBootStrap UTANDROIDAUDIO_bootstrap;\nextern AudioBootStrap DUMMYAUDIO_bootstrap;',
         "UTANDROIDAUDIO_bootstrap")
    joy = os.path.join(src, "src", "joystick", "SDL_joystick.c")
    edit(joy,
         '#include "SDL_sysjoystick.h"\n',
         '#include "SDL_sysjoystick.h"\n#ifdef SDL_UTANDROID\n#include "../utandroid/SDL_utandroid.h"\n#endif\n',
         "SDL_utandroid.h")
    edit(joy,
         '    if (!result) {\n        SDL_QuitJoysticks();\n    }\n',
         '    if (!result) {\n        SDL_QuitJoysticks();\n    }\n#ifdef SDL_UTANDROID\n'
         '    else {\n        SDL_UTANDROID_StartInput();\n    }\n#endif\n',
         "SDL_UTANDROID_StartInput")
    edit(joy,
         'void SDL_QuitJoysticks(void)\n{\n    int i;\n    SDL_JoystickID *joysticks;\n\n    SDL_LockJoysticks();',
         'void SDL_QuitJoysticks(void)\n{\n    int i;\n    SDL_JoystickID *joysticks;\n\n#ifdef SDL_UTANDROID\n'
         '    SDL_UTANDROID_StopInput(); /* before the lock: its thread takes the lock on its way out */\n#endif\n\n'
         '    SDL_LockJoysticks();',
         "SDL_UTANDROID_StopInput")
    print("utandroid hooks applied")


if __name__ == "__main__":
    main()
