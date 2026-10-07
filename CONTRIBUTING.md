# Contributing

Issues and pull requests are welcome. Open an issue before large changes.

## Build and test

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build
ctest --test-dir build
```

Build and test both Debug and Release before opening a pull request.

Tests run against compiled backends and skip those unavailable on your machine.
Filter by label from the build directory:

```bash
ctest -L unit
ctest -L conformance
ctest -L rigorous
```

## Expectations

- Keep Vulkan, D3D12, and Metal includes in public headers under azoth/rhi/native/. Build and CTest gates check this.
- Return eUnsupportedFeature for unsupported operations.
- Keep DeviceCaps consistent with device behavior.
- Return errors through result types. Do not throw across the public API.

## Style

Follow .clang-format. Comment on non-obvious decisions and backend constraints.

- Give public operations and callbacks short Doxygen briefs.
- If a parameter needs explanation (units, limits, null handling, ownership, or lifetime)
- Any doxygen comment that has a parameter comment should describe every parameter, but Keep these non-confusing params terse.
- Use short, imperative commit subjects and keep changes focused.
