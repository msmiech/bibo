---
name: bibo-dev-environment
description: How to build, test, smoke-test, format, lint and visually check BIBO (C++20, CMake, Ninja) on the developer machines (Windows with Git Bash/PowerShell, Linux CI). Use before configuring a fresh checkout or worktree, running the tests or the smoke test, checking the format and clang-tidy, looking at what the game renders, or writing a helper script.
---

# BIBO development environment

BIBO is a C++20 game built with CMake. `AGENTS.md` in the root of the repository has the layout, the rules and what a
finished change needs; this skill has the commands.

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

Useful options:
* `-DBIBO_WARNINGS_AS_ERRORS=ON`: warnings in `BIBO/` and `tests/` are errors, as in the CI on Ubuntu. Clang (the local
  compiler) and GCC (most CI jobs) warn about different things, e.g. only GCC reports `-Wsign-compare` for a `GLint`
  compared with `0xFFFFFFFF`: compare locations with `-1`.
* `-DCMAKE_EXPORT_COMPILE_COMMANDS=ON -DCMAKE_CXX_SCAN_FOR_MODULES=OFF`: the compile database for clang-tidy (below).
* `-DCMAKE_BUILD_TYPE=Debug` in a separate folder: OpenGL debug output on the console.

New source files must be listed in `CMakeLists.txt` (tests in `tests/CMakeLists.txt`). The build copies the assets next to
the executable: after changing a shader, a model, a texture or a menu document, build again before running the game.
The linker cannot overwrite a running `BIBO.exe`: close the game of this checkout first (only this one, see AGENTS.md).

## Tests
```bash
ctest --test-dir build -j4 --output-on-failure      # everything (~30 s with a GPU; [gl] tests are skipped without OpenGL 4.3)
ctest --test-dir build -L physics                   # the Catch2 tags are CTest labels
build/tests/bibo_tests "[rules]"                    # one Catch2 tag directly
build/tests/bibo_tests "The world is streamed with the level of detail of the distance"   # one test case by its name
```

* `ctest -R <text>` matches the names of test cases, not of their `SECTION`s: to run a section, run its test case.
* Where a test goes: `tests/unit` (logic only), `tests/assets` (reads the asset files), `tests/gl` (needs a window or an
  OpenGL context: `bibo::test::GlContext`, tagged `[gl]`).
* A test for a fixed bug must fail without the fix. Check it once: put the old code back (`git show HEAD:<file> > <file>`
  for files that still compile with the test, after copying the fixed file away), build, see the test fail, restore.
* For a quick throw-away experiment against the real assets (e.g. printing positions from the track or the world
  generator), a temporary Catch2 test with its own tag works well; remove it before committing.
* Keys for a test of the input: `bibo::input::SetScriptedKey(key, down)` presses them like the demo script does. They are
  shared by all tests, release them at the end.

## Smoke test
```bash
tools/smoke_test.sh build        # Linux server without a display: xvfb-run -a -s "-screen 0 1280x720x24" tools/smoke_test.sh build
```

The game plays `tools/smoke_demo.txt` (main menu, a few steps, the pause menu, quit) and must end by itself with exit code 0
and without the words `Failed`, `Could not`, `ERROR`, `not complete` or `below 4.3` in its output. Run it after changes to
the start up, the shaders, the assets, the game loop or the clean up. It opens the game window for a few seconds.
`SMOKE_TIMEOUT=<seconds>` (default 300) is how long the game may take before it counts as hanging.

## Format and lint
The C++ code is in the Google style (`.clang-format`): two spaces, 80 columns. Format before committing, with the pinned
clang-format of the pre-commit hook (a different clang-format version may format differently, and the CI checks it):

```bash
uvx --python 3.13 pre-commit run --all-files    # or only the changed files: --files <file>...
```

`--python 3.13`: PyYAML, which pre-commit needs, may have no prebuilt package for the newest Python yet.
pre-commit only checks the files that git knows: `git add` new files first, or a new file passes locally and fails in the CI.
The hook also rejects conflict markers, broken YAML, files over 2 MB and file names that only differ in their case.
Includes are sorted within their group; one that must stay first (`<Jolt/Jolt.h>`) is wrapped in `// clang-format off`/`on`.
CMake files, shell scripts and the demo scripts are indented with tabs.

Names in the Google style (`.clang-tidy`, checked by the CI): `CamelCase` types and functions, `snake_case` variables and
struct members, `snake_case_` private/protected class members, `kCamelCase` constants and enumerators. No `using namespace`:
a source is wrapped in the namespace of its header (`namespace bibo::scene {`) and adds using-declarations for other names,
headers qualify (`std::`, `glm::`). Headers have `#define` guards named after the path (`BIBO_SCENE_SCENE_HPP_`).
A function must not be named like a type (`FinishLine FinishLine()` does not compile in the class or namespace scope):
use a verb (`MakeFinishLine`, `FindNearest`).

clang-tidy also checks for mistakes that compile (use after move, dangling handles, swapped arguments, unused results,
needless copies; the list is in `.clang-tidy`). LLVM-MinGW ships `clang-tidy`; it needs the compile database of a build
folder (see Build):

```bash
clang-tidy -p build --quiet BIBO/Scene/Scene.cpp                                                # one source
git ls-files 'BIBO/*.cpp' 'tests/*.cpp' | xargs -P 8 -n 1 clang-tidy -p build --quiet           # all (a few minutes)
```

A new check goes into `.clang-tidy` only after the whole code passes it (try it with `--checks='-*,<check>'`).

## Looking at the game without a screen tool
The game can play a demo script and record raw frames (fixed time step, default settings, no audio device needed):

```bash
cd build
./BIBO --demo <script.txt> --record <frames.rgb> --fps 10 960 540 0
ffmpeg -loglevel error -f rawvideo -pixel_format rgb24 -video_size 960x540 -i <frames.rgb> -vf "vflip,select='not(mod(n\,10))'" -fps_mode passthrough frame_%02d.png
```

Record with `--fps 10` or more: the game advances at most 0.1 s per frame, so at fewer frames per second it runs in slow motion
compared with the times of the script (presses land later in the game than planned).
Script commands: `wait <s>`, `press <key>`, `hold <key> <s>`, `quit` (see `BIBO/Util/DemoScript.hpp`, examples `tools/readme_demo.txt` and `tools/smoke_demo.txt`).
The first focused main menu button can follow the mouse, so press `Up` before `Enter` to start the game.
Keep scratch scripts, recordings and frames outside of the repository.

## The CI
`gh run list --workflow build.yml` and `gh run view <run> --job <job> --log` show what the other compilers said, e.g.
`grep "warning:"` in the log of a GCC job before turning a warning into an error. Read every warning of a build, also the
ones reported in the headers of a library (`/usr/include/c++/...`): GCC reports where a warning happens, so one caused by
the code of the game or of a test (e.g. a `std::set` of vectors in a test, `-Wstringop-overread` in libstdc++ 13) shows
the path of the library, with the line of the game further up under `required from` / `inlined from`.
The CI pins its clang-tidy version (`build.yml`), the one of LLVM-MinGW may be older and report less: a clean local run
does not prove a clean CI run. The jobs and what each one guards are listed in AGENTS.md › Guardrails.

## Helper scripts (Python)
Python is not part of the game or of its build. It is installed (`python`, `python3`; with [uv](https://docs.astral.sh/uv/)
as the default Python) for the pre-commit hook, for `tools/build_icon.py` (`tools/build_bear_cub.py` runs in Blender; each says at its top how to
run it) and for throw-away scripts, e.g. an edit of many places or a look into the Collada file
of the track:

```bash
python script.py args
uv run --with numpy --with pillow python script.py     # with packages: no global installs, nothing added to the repository
```

Keep such scripts outside of the repository. Open the files with `encoding='utf-8'` and `newline=''`: a checkout on Windows
may have CRLF line endings, and a script that reads and writes text the default way changes every line ending of the file.
If a heredoc trips over the quoting of the shell, write the script to a file and run that.
