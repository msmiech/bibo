# BIBO (Bear-In Bear-Out): notes for coding agents

A small 3D game in **C++20** with OpenGL 4.3 (core profile): a bear cub runs, jumps and swims around a generated island
and does quests for the animals it meets. Built with CMake and Ninja; all libraries (GLFW, GLM, Assimp, FreeType, GLEW,
Jolt Physics, RmlUi, miniaudio, stb, Catch2) are downloaded by CMake with pinned versions and hashes.

`README.md` documents the game for players and developers (features, controls, building, testing, code style and how
everything is implemented). Read the part about what you change, and keep it up to date in the same change.

The commands for building, testing, formatting, linting and looking at what the game renders are in the skill
[`.agents/skills/bibo-dev-environment/SKILL.md`](.agents/skills/bibo-dev-environment/SKILL.md). Read it before the first
build of a session.

## Layout
| Path | Content |
| --- | --- |
| `BIBO/Main.cpp` | Window, game loop, game state (main menu, playing, paused), HUD text, F-keys, command line |
| `BIBO/Scene/` | The scene of a frame, quests (`Quest`, `QuestDirector`, `RaceQuest`, `SalmonQuest`, `StonesQuest`, `Dialog`), the bear (`BearCharacter`, `BearRig`), the animals, the camera, their renderers |
| `BIBO/World/` | The generated island: `WorldGenerator`, chunks and their meshes, streaming on worker threads (`WorldStreamer`, `WorkerPool`), vegetation, water, sky |
| `BIBO/Render/` | Frame renderer: shadow cascades, HDR, bloom, exposure, the uniforms of a frame |
| `BIBO/Physics/` | Wrapper around Jolt: bodies, ray and sphere casts, character controller, ragdoll |
| `BIBO/Resources/` | Models and meshes (Assimp), shaders, textures |
| `BIBO/Audio/`, `BIBO/UI/`, `BIBO/Util/` | Synthesized music and sounds; menus and HUD (RmlUi, documents in `UI/`); input, settings, display, demo scripts, frame recorder |
| `Shader/`, `Model/`, `Texture/`, `Font/`, `UI/` | Assets, loaded relative to the working directory (the build copies them next to the executable) |
| `tests/` | Catch2 tests: `unit` (logic only), `assets` (need the asset files), `gl` (need an OpenGL 4.3 context, skipped without one) |
| `tools/` | Smoke test, gameplay recording, and the scripts that generate the bear model (Blender) and the icon |
| `.github/workflows/` | CI (`build.yml`), release packages (`release.yml`), the GIF of the README (`readme-gif.yml`) |

Everything except `main()` is the library `bibo_core`, so the tests use exactly the code of the game. Source files are
listed explicitly in `CMakeLists.txt` (tests in `tests/CMakeLists.txt`): add new files there.

## Before a change is done
1. It builds without warnings: `cmake --build build --parallel`.
2. All tests pass: `ctest --test-dir build -j4 --output-on-failure`.
3. A fixed bug has a test that fails without the fix (revert the fix once and watch it fail); new game logic has tests.
   Logic that should be tested must not need a window: keep it in classes like `Dialog`, `RaceQuest` or `GameRules.hpp`.
4. Start up, shaders, assets, the game loop or the clean up changed: `tools/smoke_test.sh build` passes.
5. Formatted by the pre-commit hook; the names follow `.clang-tidy` (the skill has the commands).
6. `README.md` describes the new behavior (also the list of what the tests cover).

## Guardrails
What the repository enforces:

| Guardrail | Where | Catches |
| --- | --- | --- |
| Tests on every push, also with AddressSanitizer and UndefinedBehaviorSanitizer | `build.yml` | Wrong logic, memory errors, leaks, undefined behavior |
| Smoke test: the game plays `tools/smoke_demo.txt` and must end by itself with exit code 0 | `tools/smoke_test.sh`, `build.yml`, `release.yml` | Crashes, hangs, shaders that only compile on a lenient driver, missing assets, errors in the clean up |
| Warnings are errors (`-DBIBO_WARNINGS_AS_ERRORS=ON`) | `build.yml` (Ubuntu) | New compiler warnings in `BIBO/` and `tests/` |
| clang-tidy: names, and mistakes that compile | `.clang-tidy`, `build.yml` | Use after move, dangling handles, swapped arguments, unused results, needless copies |
| clang-format and file checks | `.pre-commit-config.yaml`, `build.yml` | Unformatted code, conflict markers, broken YAML, files over 2 MB, names that only differ in case |
| `BIBO.exe` depends on Windows DLLs only | `build.yml`, `release.yml` | A library that is not linked statically |
| Checklist of a pull request | `.github/pull_request_template.md` | A change without tests or documentation |

Rules for agents (and everyone else):

- **Never weaken a guardrail to make it pass.** No deleted or skipped tests, no `NOLINT`, `-Wno-...` or removed check to
  silence a finding, no `--no-verify`, no longer `SMOKE_TIMEOUT` to hide a hang. If a check is wrong, say so and change
  it on purpose, with the reason next to it.
- **A test that asserts a behavior is the specification.** Look for one before calling a behavior a bug (e.g. "Restarting
  keeps the completed quest" in `tests/gl/SceneTest.cpp`: the best times and the completed stones quest survive a
  restart on purpose). Change such a test only together with the decision to change the behavior.
- **Commit and push only when asked,** never to `main` and never with force. Work on a branch; `readme-gif.yml` commits
  the new GIF to the branch of a pull request, so pull before pushing to it again.
- **Dependencies are pinned** (URL and SHA-256 in `CMakeLists.txt`). Do not add or update a library, a GitHub Action or a
  hook version as a side effect of another task.
- **Assets are binary and large.** Do not add, regenerate or re-export files in `Model/`, `Texture/`, `Font/` or
  `Screenshots/` unless that is the task. The game only loads `Model/SnowTerrain/track1.dae` and `Model/BearCub/BearCub.glb`.
- **The words `Failed`, `Could not`, `ERROR`, `not complete` and `below 4.3` on the console mean an error:** the smoke
  test fails when the game prints them. Use `WARNING:` (and other words) for what the game can go on without.
- **Workflows:** do not change their triggers, `permissions` or the release steps unless that is the task.
- **Scratch files** (demo scripts, recordings, frames, logs, experiments) stay outside of the repository. `settings.cfg`
  and build folders are not committed.
- **Several checkouts at once:** worktrees of this repository are often in use side by side, each with a running game.
  Build and run only in the build folder of your own checkout, never stop games by their name (they are all `BIBO.exe`),
  and do not use a bare `git stash` (the stash is shared by all worktrees).

## Mistakes this code has made before
Found by a review of the whole code (November 2026). Each one is fixed and has a test; the pattern is what to look for
in new code.

- **The button that closes a menu also played the game.** Resuming with A, B or ENTER answered the conversation that was
  open behind the pause menu. Buttons that count once per press go through `Input::ReadPlayerInput`, and
  `Input::IgnoreMenuPress()` drops them for the frame a menu closes: a new "pressed in this frame" input belongs there.
- **A guard that was overwritten a few lines later.** `BearCharacter` remembered that the jump button was held while the
  bear ate, then assigned the same variable again from the input it had just cleared. When a function replaces its
  input (`input = BearInput()`), check every later use of both the original and the replacement.
- **A pointer to fewer elements than OpenGL reads.** `glClearBufferfv(GL_COLOR, ...)` always reads four floats.
  Check the element count of every `gl...v` call; neither the compiler nor the sanitizers see into the driver.
- **A list matched by its index from frame to frame.** `FoliageInteraction` follows the bodies by their position in the
  list, and the list left out hidden animals, so everyone behind them became someone else. Keep such lists stable
  (`Body::present`) or give the entries an identity.
- **Work done right away and then again by a job.** `WorldStreamer::EnsureCollision` generated columns on the main thread
  without marking them as requested. Code that does the work of a job itself must leave the same state as the job.
- **A loader that took either a node or its children.** `Model` skipped the meshes of a node that has children, in two
  copies of the same code. Shared steps (reading a material) go into one function.
- **A check that could not fail.** The smoke test stopped the game after 30 seconds and called that a success, so a hang
  passed and the clean up never ran. A test of the whole game must let it end by itself and look at how it ended.
- **A copied message.** F5 (mipmapping) announced itself as "Texture sampling". After copying a block, read its strings.
- **A review finding that was a decision.** The completed stones quest staying completed after a restart was reported
  as a bug; a test said otherwise (see the guardrails). Look at the tests first.

## Languages
The game, its tests and its tools are C++20, GLSL (`Shader/`), RML/RCSS (`UI/`), CMake and Bash. Python is not part of
the game: it runs the pre-commit hook and the script that renders the icon (`tools/build_icon.py`; the bear model is built by a
Blender script, `tools/build_bear_cub.py`), and it is handy for throw-away scripts (see the end of the skill). Do not add Python (or any other new language or tool) to
the build or to the tests.
