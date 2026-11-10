<!-- What changes for the player or the developer, and why. -->

## Checklist
- [ ] Built and tested: `cmake --build build --parallel` and `ctest --test-dir build -j4 --output-on-failure`
- [ ] A fixed bug has a test that fails without the fix; new game logic has tests (`tests/`, listed in `tests/CMakeLists.txt`)
- [ ] Start up, shaders, assets or clean up changed: `tools/smoke_test.sh build` passes
- [ ] Formatted with the pre-commit hook (`pre-commit run --all-files`), no new compiler warnings
- [ ] `README.md` is up to date (features, controls, build, tests, implementation details)
