# Options

- [Backends](#backends)
- [Build contents](#build-contents)
- [Profiling](#profiling)
- [Dear ImGui](#dear-imgui)
- [Build behavior](#build-behavior)
- [Dependency versions](#dependency-versions)
- [Test environment variables](#test-environment-variables)

Build options are CMake cache variables. Defaults follow each name. Test environment variables are listed separately.

## Backends

### AZOTH_RHI_BACKEND_VULKAN, AZOTH_RHI_BACKEND_D3D12, AZOTH_RHI_BACKEND_METAL, AZOTH_RHI_BACKEND_METAL4

ON where the platform can build them.

Compile the selected backend. Disabled backends are absent from AvailableBackends but retain their API tags and typed
entry points for custom implementations. Enabling a backend on an unsupported platform fails configuration.

```bash
cmake -B build -DAZOTH_RHI_BACKEND_VULKAN=OFF
```

METAL builds azoth.rhi.metal (Metal 3). METAL4 builds azoth.rhi.metal4 (Metal 4). They have separate command, submission,
and binding implementations and can be enabled independently. Metal 4 device creation requires a compatible adapter.

METAL4 also requires a metal-cpp release with MTL4 headers. Configuration fails and reports the pinned tag if they are
missing.

### AZOTH_RHI_DEFAULT_BACKEND

metal4 on Apple where that backend is built, metal on Apple otherwise, d3d12 on Windows, vulkan everywhere else.

Preferred backend unless overridden at runtime, including by AZOTH\_RHI\_BACKEND. Selection tries the remaining
backends in order if device creation fails. An unsupported Metal 4 device falls back to Metal 3 when it is built.

### AZOTH_RHI_DETECT_PLATFORM_APIS

ON.

Check enabled backends at configure time. Missing Direct3D 12 headers or Metal frameworks fail configuration.
A missing Vulkan loader produces a warning because the backend can build with fetched headers.

## Build contents

### AZOTH_RHI_BUILD_TESTS

ON when Azoth RHI is the top-level project and OFF otherwise.

Build the test suite.

### AZOTH_RHI_BUILD_UNIT_TESTS

ON.

Build the per-module unit suites. They carry the ctest label unit.

### AZOTH_RHI_BUILD_RIGOROUS_TESTS

ON.

Build the conformance and cross-backend suites. They carry the ctest labels conformance and rigorous.

### AZOTH_RHI_BUILD_STRESS_TESTS

OFF.

Build the long-running stress suites. They carry the ctest label stress.

### AZOTH_RHI_BUILD_EXAMPLES

OFF.

Build the examples and their dependencies. Samples with missing optional dependencies are skipped.

### AZOTH_RHI_BUILD_BENCHMARKS

ON when Azoth RHI is the top-level project and OFF otherwise.

Build the performance benchmarks.

### AZOTH_RHI_INSTALL

ON when Azoth RHI is the top-level project and OFF otherwise.

Generate the install and export rules.

## Profiling

### AZOTH_RHI_ENABLE_PROFILING

ON.

Compile profiler instrumentation. OFF removes the instrumentation calls. Debug labels are controlled separately.

### AZOTH_RHI_TRACY_TARGET

Tracy::TracyClient.

Target providing the Tracy client. The sink compiles when this target exposes TRACY\_ENABLE in its interface compile
definitions.

### AZOTH_RHI_PIX

OFF.

Compile PIX events for the Direct3D 12 backend. Requires that backend and the WinPixEventRuntime target named by
AZOTH\_RHI\_PIX\_TARGET. Only AZOTH\_RHI\_TESTS\_FETCH\_PIX fetches the runtime.

### AZOTH_RHI_PIX_TARGET

winpix.

Target that supplies the pix3.h include directory and links WinPixEventRuntime.

### AZOTH_RHI_TESTS_FETCH_TRACY, AZOTH_RHI_TESTS_FETCH_PIX

Both OFF.

Fetch Tracy or WinPixEventRuntime to test the sinks in CI. Consuming builds should supply their own targets.

## Dear ImGui

### AZOTH_RHI_BUILD_IMGUI

OFF.

Build azoth::rhi-imgui, the Dear ImGui renderer. Requires Dear ImGui.

### AZOTH_RHI_IMGUI_TARGET

imgui::imgui.

Dear ImGui target linked by azoth::rhi-imgui. Set this to your application's existing target to share the same copy.

### AZOTH_RHI_FETCH_IMGUI

OFF.

Fetch Dear ImGui when the host provides none.

### AZOTH_RHI_FETCH_SDL3

ON.

Fetch SDL3 for windowed samples when the host provides none.

### AZOTH_RHI_FETCH_SLANG

ON.

Fetch a prebuilt Slang when the host provides none. Supplies slangc for build-time shader compilation.

## Build behavior

### AZOTH_RHI_NO_EXCEPTIONS

OFF.

Build without exceptions for hosts compiled with -fno-exceptions. This setting propagates to consumers and is part of
the module ABI stamp because it changes HostAllocatorAdapter. Allocation failure in that adapter aborts. Operations
that explicitly check allocation failure can still return eOutOfHostMemory.

### AZOTH_RHI_SANITIZER

OFF.

Sanitizer to build with. Takes OFF, THREAD, ADDRESS or UNDEFINED.

### AZOTH_RHI_COMPILER_CACHE

ON.

Route compiles through ccache or sccache when one is installed.

## Dependency versions

These variables pin fetched dependencies.

| Option                      | Pins                                                       |
|-----------------------------|------------------------------------------------------------|
| AZOTH_RHI_VK_DYNAMIC_TAG    | vk-dynamic, which is Vulkan-Hpp plus dispatcher storage    |
| AZOTH_RHI_VMA_TAG           | VulkanMemoryAllocator                                      |
| AZOTH_RHI_METAL_CPP_TAG     | metal-cpp, tagged by the SDK it targets                    |
| AZOTH_RHI_TRACY_TAG         | the Tracy AZOTH_RHI_TESTS_FETCH_TRACY brings in            |
| AZOTH_RHI_WINPIX_VERSION    | the WinPixEventRuntime AZOTH_RHI_TESTS_FETCH_PIX brings in |
| AZOTH_RHI_IMGUI_TAG         | the Dear ImGui AZOTH_RHI_FETCH_IMGUI brings in             |
| AZOTH_RHI_SLANG_TAG         | the Slang AZOTH_RHI_FETCH_SLANG brings in                 |
| AZOTH_RHI_SDL3_TAG          | the SDL3 AZOTH_RHI_FETCH_SDL3 brings in                   |
| AZOTH_RHI_GLM_TAG           | glm, for the samples that need matrices                   |
| AZOTH_RHI_FASTGLTF_TAG      | fastgltf, for the scene loader in deccer_cubes            |
| AZOTH_RHI_STB_TAG           | stb, for image decoding in the samples                    |
| AZOTH_RHI_QUILL_TAG         | Quill, for sample logging                                 |

## Test environment variables

The test suite reads these variables at runtime. AZOTH\_RHI\_BACKEND also applies to applications.

### AZOTH_RHI_TEST_BACKENDS

Restricts a run to the named backends.

```bash
AZOTH_RHI_TEST_BACKENDS=vulkan,null ctest --test-dir build
```

### AZOTH_RHI_TEST_REQUIRE_BACKENDS

Fail if a named backend cannot create a device. The test still skips when the machine has no compatible adapter.

```bash
AZOTH_RHI_TEST_REQUIRE_BACKENDS=metal ctest --test-dir build
```

### AZOTH_RHI_BACKEND

Preferred runtime backend, overriding AZOTH\_RHI\_DEFAULT\_BACKEND. Empty values are ignored.
See [picking a backend](guides.md#picking-a-backend).
