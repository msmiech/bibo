---
name: bibo-dev-environment
description: How to script, build, test and visually check BIBO on the developer machines (Windows with Git Bash/PowerShell, Linux CI). Use before running Python helpers, configuring a fresh checkout or worktree, running the tests, or looking at what the game renders.
---

# BIBO development environment

## Python
`python` (and `python3`) is on the PATH, installed with [uv](https://docs.astral.sh/uv/) as the default Python. Run it directly:

```bash
python -c "print('ok')"          # one-liner
python script.py args            # a script
python - <<'EOF'                  # inline script (handy for multi-line file edits)
p = 'BIBO/Scene/Scene.cpp'
s = open(p, encoding='utf-8').read()
s = s.replace('old', 'new', 1)
open(p, 'w', encoding='utf-8').write(s)
EOF
```

Packages for a one-off script: `uv run --with numpy --with pillow python script.py` (no global installs, nothing added to the repository).
If a heredoc trips over the quoting of the shell, write the script to a file outside of the repository and run that.
Read and write files with `encoding='utf-8'`; the sources are UTF-8 and use tabs for indentation.

## Build
Toolchain: CMake ≥ 3.25, Ninja, a C++20 compiler (LLVM-MinGW `c++`/`cc` on Windows). See README.md › Building.

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
```

A fresh worktree does not need to download the dependencies again: point FetchContent at the sources of a checkout that was
already configured (`<checkout>/build/_deps/<name>-src`) for each of ASSIMP, CATCH2, FREETYPE, GLEW, GLFW, GLM, JOLTPHYSICS,
MINIAUDIO, RMLUI and STB, e.g. `-DFETCHCONTENT_SOURCE_DIR_GLFW=<checkout>/build/_deps/glfw-src` (add `-DCMAKE_CXX_COMPILER=c++ -DCMAKE_C_COMPILER=cc` with LLVM-MinGW).
A dependency that is new in the branch is not there yet: leave its option out and let CMake download it.

## Tests
```bash
ctest --test-dir build -j4 --output-on-failure      # everything (~30 s with a GPU; [gl] tests are skipped without OpenGL 4.3)
build/tests/bibo_tests "[rules]"                    # one Catch2 tag directly
```
New test files must be listed in `tests/CMakeLists.txt`. For a quick throw-away experiment against the real assets (e.g. printing
positions from the track or the world generator), a temporary Catch2 test with its own tag works well; remove it before committing.

## Looking at the game without a screen tool
The game can play a demo script and record raw frames (fixed time step, default settings, no audio device needed):

```bash
cd build
./BIBO --demo <script.txt> --record <frames.rgb> --fps 10 960 540 0
ffmpeg -loglevel error -f rawvideo -pixel_format rgb24 -video_size 960x540 -i <frames.rgb> -vf "vflip,select='not(mod(n\,10))'" -fps_mode passthrough frame_%02d.png
```

Record with `--fps 10` or more: the game advances at most 0.1 s per frame, so at fewer frames per second it runs in slow motion
compared with the times of the script (presses land later in the game than planned).
Script commands: `wait <s>`, `press <key>`, `hold <key> <s>`, `quit` (see `BIBO/Util/DemoScript.hpp`, example `tools/readme_demo.txt`).
The first focused main menu button can follow the mouse, so press `Up` before `Enter` to start the game.
Keep scratch scripts, recordings and frames outside of the repository.
