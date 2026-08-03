# Options

- [Backends](#backends)
- [Build contents](#build-contents)
- [Profiling](#profiling)
- [Dear ImGui](#dear-imgui)
- [Build behaviour](#build-behaviour)
- [Dependency versions](#dependency-versions)
- [Test environment variables](#test-environment-variables)

Every option below is a CMake cache variable. The default is given after the name.

## Backends

### AZOTH_RHI_BACKEND_VULKAN, AZOTH_RHI_BACKEND_D3D12, AZOTH_RHI_BACKEND_METAL, AZOTH_RHI_BACKEND_METAL4

ON where the platform can build them.

Whether that backend is compiled into the library. A backend that is OFF is absent from what
AvailableBackends reports, though its API tag and its typed entry point are declared either way so a
backend of your own can fill them. Asking for a backend the host cannot build is a configure error
instead of a silent OFF.

```bash
cmake -B build -DAZOTH_RHI_BACKEND_VULKAN=OFF
```

Metal is two backends and not one. Metal 4 replaced submission, recording and binding outright, and its
command objects share no base with the Metal 3 ones, so the two are separate APIs that happen to agree
about what a texture is. METAL is `azoth.rhi.metal`, the generation every Apple machine this runs on can
take. METAL4 is `azoth.rhi.metal4`, preferred where the adapter reports the family and refused with a
reason where it does not.

Turning one off drops its translation units and nothing else: neither names anything from the other, and
each passes the suite on its own.

METAL4 needs a metal-cpp release carrying the MTL4 headers. Enabling it against one that does not is a
configure error naming the pinned tag, rather than a quiet downgrade that would leave AvailableBackends
missing an entry the configuration asked for.

### AZOTH_RHI_DEFAULT_BACKEND

metal4 on Apple where that backend is built, metal on Apple otherwise, d3d12 on Windows, vulkan
everywhere else.

The backend chosen when nothing overrides it at runtime. The platform's own API is the default
because Apple ships Metal and the Vulkan beside it is MoltenVK, which is Vulkan translated onto
Metal so defaulting there would charge every caller for a translation the platform does not need.
The AZOTH\_RHI\_BACKEND environment variable still wins at runtime.

Naming metal4 on a machine that cannot run it costs one refused device creation and then Metal 3, since
selection walks the preferred order and a backend that cannot create falls through to the next.

### AZOTH_RHI_DETECT_PLATFORM_APIS

ON.

Check at configure time that each enabled backend's SDK is installed so a missing SDK is a
configure error and not a wall of compile errors.

## Build contents

### AZOTH_RHI_BUILD_TESTS

ON when Azoth RHI is the top-level project, otherwise OFF.

Build the test suite.

### AZOTH_RHI_BUILD_UNIT_TESTS

ON.

Build the per-module unit suites, which carry the ctest label unit.

### AZOTH_RHI_BUILD_RIGOROUS_TESTS

ON.

Build the conformance and cross-backend suites, which carry the ctest labels conformance and
rigorous.

### AZOTH_RHI_BUILD_STRESS_TESTS

OFF.

Build the long-running stress suites, which carry the ctest label stress.

### AZOTH_RHI_BUILD_EXAMPLES

OFF.

Build the examples. Off by default because some of them carry dependencies nothing else here needs
and a sample whose dependency is missing is skipped instead of failing the configure.

### AZOTH_RHI_BUILD_BENCHMARKS

ON when Azoth RHI is the top-level project, otherwise OFF.

Build the benchmarks the non-functional budgets are measured with.

### AZOTH_RHI_INSTALL

ON when Azoth RHI is the top-level project, otherwise OFF.

Generate the install and export rules.

## Profiling

### AZOTH_RHI_ENABLE_PROFILING

ON.

Compile in the profiler instrumentation points. With it off every instrumentation point expands to
nothing so an installed sink is never consulted and the calls are not there to consult it. Debug
labels are separate and stay on either way.

### AZOTH_RHI_TRACY_TARGET

Tracy::TracyClient.

The target providing the Tracy client. Tracy takes no on or off option: the sink compiles when this
target exists and was built with TRACY\_ENABLE, which Tracy puts on that target itself.

### AZOTH_RHI_PIX

OFF.

Compile the PIX event sink. Nothing here fetches WinPixEventRuntime so an ON build links the target
named by AZOTH\_RHI\_PIX\_TARGET. PIX events come from the Direct3D 12 backend so asking for them
without it is a configure error.

### AZOTH_RHI_PIX_TARGET

winpix.

The target providing WinPixEventRuntime and pix3.h when PIX is on. It has to put pix3.h on the
include search list and link the runtime.

### AZOTH_RHI_TESTS_FETCH_TRACY, AZOTH_RHI_TESTS_FETCH_PIX

Both OFF.

Fetch a Tracy client or WinPixEventRuntime for this build to link, standing in for one a host
supplies. These exist so CI can compile the two sinks that are otherwise only reachable when a host
brings its own. They are not meant for a consuming build.

## Dear ImGui

### AZOTH_RHI_BUILD_IMGUI

OFF.

Build azoth::rhi-imgui, the Dear ImGui renderer. Off by default because ImGui is a dependency the
library does not otherwise have and a consumer that wants no interface should not be made to carry
one.

### AZOTH_RHI_IMGUI_TARGET

imgui::imgui.

The Dear ImGui target azoth::rhi-imgui links. Point it at yours if it is called something else. The
ImGui is yours on purpose, because a second copy in the same process is a second context and a
second font atlas.

### AZOTH_RHI_FETCH_IMGUI

OFF.

Fetch Dear ImGui when the host provides none. Off so a consumer never ends up with two.

## Build behaviour

### AZOTH_RHI_NO_EXCEPTIONS

OFF.

Build a library that neither throws nor requires exceptions, for hosts compiled with
-fno-exceptions. It is PUBLIC and joins the module ABI stamp, because it changes HostAllocatorAdapter
in a header a consumer includes and it is the switch a loadable backend has to agree on before
anything in it is called. What it costs is stated here: a host allocation that fails aborts instead
of reporting, which is what the standard library already does under -fno-exceptions.

### AZOTH_RHI_SANITIZER

OFF.

Sanitizer to build with. Takes OFF, THREAD, ADDRESS or UNDEFINED.

### AZOTH_RHI_COMPILER_CACHE

ON.

Route compiles through ccache or sccache when one is installed.

## Dependency versions

Each of these pins a fetched dependency. They exist so a build can be reproduced or moved forward
deliberately and the defaults are what CI builds against.

| Option                      | Pins                                                       |
|-----------------------------|------------------------------------------------------------|
| AZOTH_RHI_VK_DYNAMIC_TAG    | vk-dynamic, which is Vulkan-Hpp plus dispatcher storage    |
| AZOTH_RHI_VMA_TAG           | VulkanMemoryAllocator                                      |
| AZOTH_RHI_METAL_CPP_TAG     | metal-cpp, tagged by the SDK it targets                    |
| AZOTH_RHI_TRACY_TAG         | the Tracy AZOTH_RHI_TESTS_FETCH_TRACY brings in            |
| AZOTH_RHI_WINPIX_VERSION    | the WinPixEventRuntime AZOTH_RHI_TESTS_FETCH_PIX brings in |
| AZOTH_RHI_IMGUI_TAG         | the Dear ImGui AZOTH_RHI_FETCH_IMGUI brings in             |
| AZOTH_RHI_SLANG_TAG         | Slang, for the samples that compile shaders               |
| AZOTH_RHI_GLM_TAG           | glm, for the samples that need matrices                   |
| AZOTH_RHI_FASTGLTF_TAG      | fastgltf, for the scene loader in deccer_cubes            |
| AZOTH_RHI_STB_TAG           | stb, for image decoding in the samples                    |
| AZOTH_RHI_QUILL_TAG         | Quill, for sample logging                                 |

## Test environment variables

These are read at runtime by the test suite, not at configure time.

### AZOTH_RHI_TEST_BACKENDS

Restricts a run to the named backends.

```bash
AZOTH_RHI_TEST_BACKENDS=vulkan,null ctest --test-dir build
```

### AZOTH_RHI_TEST_REQUIRE_BACKENDS

Turns a skipped backend into a failure. CI sets it so a driver that does not come up fails the job
instead of passing with everything skipped.

```bash
AZOTH_RHI_TEST_REQUIRE_BACKENDS=metal ctest --test-dir build
```

### AZOTH_RHI_BACKEND

Names the backend a program prefers at runtime, ahead of the configure-time
AZOTH\_RHI\_DEFAULT\_BACKEND. An empty value counts as nothing set. See [picking a
backend](guides.md#picking-a-backend).
