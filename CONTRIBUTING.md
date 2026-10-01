# Contributing

Issues and pull requests are welcome. For anything large, open an issue first.

## Building

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build
ctest --test-dir build
```

Build and test both Debug and Release before opening a pull request.

## What the tests expect

The suite tests the compiled backends and skips those unavailable on your machine.
From the build directory, filter tests by label:

```bash
ctest -L unit        # the per-module suites
ctest -L conformance # the cross-backend contracts, including gate_api_boundary
ctest -L rigorous    # the slower cross-backend campaigns
```

AZOTH\_RHI\_TEST\_BACKENDS=vulkan,null restricts a run to named backends.
AZOTH\_RHI\_TEST\_REQUIRE\_BACKENDS=metal fails the test if Metal cannot create a device, except when it reports
no compatible adapter. That case still skips. CI sets this variable for each backend.

## What a change has to hold to

- In public headers, keep Vulkan, D3D12, and Metal includes inside azoth/rhi/native/. A build gate and a CTest case check this.
- Return eUnsupportedFeature for operations a backend cannot perform.
- Keep DeviceCaps consistent with device behavior.
- Return errors through result types. Do not throw across the public API.

## Style

Follow .clang-format, which CI checks. Comments should explain non-obvious decisions and backend constraints.

## Commits

Use short imperative subject lines and keep each change focused.
