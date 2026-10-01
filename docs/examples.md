# Examples

Install the [build requirements](overview.md#building), then run from the repository root:

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DAZOTH_RHI_BUILD_EXAMPLES=ON
cmake --build build --config Release --target rhi_device_info
./build/examples/rhi_device_info
```

Visual Studio and Xcode builds put executables under build/examples/Release/. On Windows, add .exe.
To select a backend, pass its name as the first argument:

```bash
./build/examples/rhi_device_info vulkan
```

CMake fetches dependencies by default. Check configure output if a sample is skipped.
Use the rhi\_examples build target to build all available samples. Sources are in [examples/samples](../examples/samples).
