# Bear-In Bear-Out
[![Build](https://github.com/msmiech/bibo/actions/workflows/build.yml/badge.svg)](https://github.com/msmiech/bibo/actions/workflows/build.yml)

Bear-In Bear-Out (BIBO) is a simple 3D game in which the objective is to complete a lap as fast as possible and avoiding obstacles

![alt text](Screenshots/bibo_screenshot.PNG)

## Features
* Freely movable camera
  * Locked mode = following the player's character (default)
  * Unlocked mode = 3d flying camera (hold left mouse button)
* Movable objects with physics (based on Jolt Physics, originally Nvidia PhysX)
* Texture Mapping (including normal maps)
* Lighting and Materials
* A lap around the track: the finish line only counts after all checkpoints along the track were passed in order
* Keyboard controls
  * Arrow keys for navigation
  * Space bar for jumping
  * R to restart
  * ESC for the pause menu
  * F-Keys for settings and debug features (F1 for help)
* Main menu, pause menu, game over menu (restart the run or back to the main menu) and settings menu (based on RmlUi, usable with mouse or keyboard)
  * Graphics settings (VSync, HDR, bloom, shadows, normal mapping, transparency, texture filtering, mipmapping, wireframe)
  * Resolution and window mode (windowed, fullscreen or borderless)
* Supports complex models (like the bear, see copyright in files)
* Multiple light sources
* HDR
* Bloom
* Animations
* Transparency
* Shadow Maps
* Animated textures (e.g. water)
* View frustum culling: objects outside of the camera's view are not drawn (F8 to switch it off, the HUD shows how many objects are drawn)

## Building
The game is built with [CMake](https://cmake.org) 3.25 or newer, the [Ninja](https://ninja-build.org) build tool and any C++17 compiler (Clang, GCC, MinGW-w64 or MSVC).
All libraries are downloaded and built automatically the first time the project is configured (about 180 MB, internet access required),
so apart from the tools only the OpenGL and windowing headers of the operating system are needed.
Running the game requires a graphics card with OpenGL 4.3 support.

### Windows
1. **Install the tools (once).** The recommended compiler is [LLVM-MinGW](https://github.com/mstorsjo/llvm-mingw) (Clang/LLVM with the Universal C Runtime of Windows).
   It does not need Visual Studio or the Windows SDK, and the game it builds runs without any extra DLLs.
   In PowerShell:

   ```powershell
   winget install --id Kitware.CMake -e
   winget install --id MartinStorsjo.LLVM-MinGW.UCRT -e
   winget install --id Ninja-build.Ninja -e
   ```

   Without winget, download the `llvm-mingw-<version>-ucrt-x86_64.zip` from the [LLVM-MinGW releases](https://github.com/mstorsjo/llvm-mingw/releases)
   and `ninja-win.zip` from the [Ninja releases](https://github.com/ninja-build/ninja/releases), unpack them and add their folders (for LLVM-MinGW the `bin` folder) to the `Path` environment variable.

2. **Open a new terminal** (changes to `Path` only apply to newly started terminals) and check that all tools are found:

   ```powershell
   cmake --version
   ninja --version
   clang --version
   ```

3. **Configure and build** in the folder of this repository:

   ```powershell
   cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
   cmake --build build --parallel
   ```

   `-G Ninja` is required on Windows: without it CMake looks for Visual Studio or Microsoft's NMake.

4. **Run the game:**

   ```powershell
   cd build
   .\BIBO.exe 1280 720 0
   ```

Alternatively, [MSYS2](https://www.msys2.org) provides GCC: install the packages `mingw-w64-ucrt-x86_64-toolchain mingw-w64-ucrt-x86_64-cmake mingw-w64-ucrt-x86_64-ninja` with `pacman`
and run steps 3 and 4 in the *MSYS2 UCRT64* shell (`./BIBO.exe 1280 720 0`).
Visual Studio 2022 or newer can also open the folder directly as a CMake project.

### Linux
1. **Install the tools and headers (once).** On Debian/Ubuntu:

   ```bash
   sudo apt install build-essential cmake ninja-build git pkg-config libgl-dev libx11-dev libxrandr-dev libxinerama-dev libxcursor-dev libxi-dev libwayland-dev libxkbcommon-dev wayland-protocols
   ```

2. **Configure and build** in the folder of this repository:

   ```bash
   cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
   cmake --build build --parallel
   ```

3. **Run the game:**

   ```bash
   cd build
   ./BIBO 1280 720 0
   ```

To use the libraries of the distribution instead of downloading them, also install `libglfw3-dev libglm-dev libassimp-dev libfreetype-dev libglew-dev` and add `-DBIBO_USE_SYSTEM_LIBS=ON` to the configure command.

### macOS
Not supported: the game requires OpenGL 4.3, macOS only provides OpenGL 4.1.

### Running and distributing
The build copies the game assets (`Font`, `Shader`, `Texture`, `UI` and the scene `Model/SnowTerrain/track1.dae`) next to the executable, and the game loads them from the current folder, so always start it from the build folder.
The game starts in the main menu, ESC opens the pause menu during the game.
The settings menu saves the settings to `settings.cfg` in the current folder (a text file, delete it to reset the settings).
The command line arguments `BIBO [width height [fullscreen]]` override the saved resolution and window mode: width and height of the window in pixels and `1` for fullscreen (default: `800 600 0`).

To create a folder with the executable and all assets that can be copied to another computer:

```bash
cmake --install build --prefix BIBO-dist
```

Ready-to-run packages for Windows and Linux are attached to the [releases](https://github.com/msmiech/bibo/releases).
They are built by the [release workflow](.github/workflows/release.yml) when a version tag is pushed, e.g. for version 2.0:

```bash
git tag 2.0
git push origin 2.0
```

Tags with a suffix such as `2.1-rc1` are published as pre-releases.

For a debug build (OpenGL debug output on the console) configure a separate folder with `-DCMAKE_BUILD_TYPE=Debug`, e.g. `cmake -S . -B build-debug -G Ninja -DCMAKE_BUILD_TYPE=Debug`.

### Troubleshooting
* `Running 'nmake' '-?' failed` or `CMAKE_CXX_COMPILER not set`: CMake did not find Ninja or a compiler.
  Check step 2, then configure again with `-G Ninja` and `--fresh` (a failed configuration is remembered in the `build` folder):
  `cmake --fresh -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release`
* To use a specific compiler, add e.g. `-DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++` to the configure command (together with `--fresh`).
* `Could not create a window with an OpenGL 4.3 core context`: update the graphics driver; OpenGL 4.3 is required.

## Testing
The automated tests use [Catch2](https://github.com/catchorg/Catch2) and are built together with the game (configure with `-DBIBO_BUILD_TESTS=OFF` to skip them).
Run them in the build folder with CTest:

```bash
ctest --test-dir build --output-on-failure
```

The tests are in the `tests` folder:
* `unit`: game rules (falling off the track, checkpoints in their order, the finish line), physics (gravity, ground, walls, the bear walking, climbing, sliding and jumping, rock collisions), the view frustum (compared with the camera matrices for random cameras), math helpers and the generated textures
* `assets`: the content of the asset files, e.g. every node the game loads exists in the track, the bear starts on the terrain and on the finish line, going backwards from the start does not count, the checkpoints are apart and the collision mesh of the terrain matches the rendered terrain
* `gl`: everything that needs an OpenGL 4.3 context: shaders, textures, models and their bounding spheres, the camera and the whole scene (e.g. culling never removes a visible pixel). These tests are reported as *skipped* if no context can be created (no display, no OpenGL 4.3).
  On Linux servers without a graphics card they run on a virtual X server with Mesa's software renderer: `xvfb-run -a ctest --test-dir build --output-on-failure`

Every test case is a separate CTest test and its Catch2 tags become CTest labels, e.g. only the physics tests: `ctest --test-dir build -L physics`.
The test executable can also be run directly, e.g. `build/tests/bibo_tests "[rules]"` (see `--help` for the options of Catch2).

The CI also runs the tests with AddressSanitizer and UndefinedBehaviorSanitizer (memory errors, leaks, undefined behavior). On Linux:

```bash
CFLAGS="-fsanitize=address,undefined" CXXFLAGS="-fsanitize=address,undefined" LDFLAGS="-fsanitize=address,undefined" cmake -S . -B build-asan -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build-asan --parallel
UBSAN_OPTIONS=halt_on_error=1 xvfb-run -a ctest --test-dir build-asan --output-on-failure
```

## Implementation Details
### Language and Compiler
This project was originally written in C++11 using Microsoft Visual Studio 2013.
It now uses C++17 and CMake, and builds with GCC, Clang, MinGW-w64 and MSVC on Windows and Linux.
For hardware graphics (modern) OpenGL 4.3 (core profile) is used.
It was developed for Windows 8.1 and Windows 10 and tested using Nvidia GeForce graphics hardware
### Libraries and Dependencies
Downloaded automatically by CMake (see `CMakeLists.txt` for the exact versions and checksums):
* [Assimp](https://github.com/assimp/assimp) 6.0 (model loading)
* [FreeType](https://freetype.org) 2.14 (text rendering)
* [GLEW](https://github.com/nigels-com/glew) 2.3 (OpenGL function loading)
* [GLFW](https://www.glfw.org) 3.4 (window and input)
* [GLM](https://github.com/g-truc/glm) 1.0 (math)
* [Jolt Physics](https://github.com/jrouwe/JoltPhysics) 5.6 (physics, replaces Nvidia PhysX 3.3.4)
* [RmlUi](https://github.com/mikke89/RmlUi) 6.3 (menus, written in RML and RCSS similar to HTML and CSS, see the `UI` folder)
* [stb_image](https://github.com/nothings/stb) (image loading, replaces DevIL)
* [Catch2](https://github.com/catchorg/Catch2) 3.16 (tests only)

## Sources
### Literature
* "OpenGL SuperBible"
* learnopengl.com
* opengl-tutorial.org
* ogldev.atspace.co.uk
* open.gl
* lighthouse3d.com/tutorials
* learncpp.com
### Assets
* BearBrown - http://tf3dm.com/3d-model/bear-98965.html
* SnowTerrain - http://tf3dm.com/3d-model/snowy-terrain-46334.html
* Rock - http://tf3dm.com/3d-model/rock-86533.html
* Salmon - http://tf3dm.com/3d-model/salmon-44387.html
* Font: [DejaVu Sans](https://dejavu-fonts.github.io) (see `Font/DejaVuSans-LICENSE.txt`)
* The chequered flag of the finish line and the TV static of the monitor are generated by the game
