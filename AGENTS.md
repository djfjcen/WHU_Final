# Repository Instructions

## Testing Policy

- Do not use CTest in this repository.
- Do not add `enable_testing()`, `add_test()`, `include(GoogleTest)`, or `gtest_discover_tests()` to CMake.
- All tests must be GoogleTest binaries run directly.
- The canonical frontend test command is `build/toyc-frontend-tests`.
- Common build, run, test, and coverage workflows are recorded in the root `Justfile`.

