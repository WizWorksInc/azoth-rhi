# Overview

- [What it is](#what-it-is)
- [Backends](#backends)
- [What the API holds to](#what-the-api-holds-to)
- [Quick start](#quick-start)
- [Building](#building)
- [Linking it from your own build](#linking-it-from-your-own-build)
- [Running the tests](#running-the-tests)
- [Extras](#extras)

## What it is

Azoth RHI is a C++23 render hardware interface. You write against one handle-based API and the
library lowers it onto Vulkan, Direct3D 12 or Metal. It owns no window, no shader compiler and no
scene format. What you get is the device, the resources, the command recording and the presentation
seam.

It comes out of the Azoth engine, which is closed source, but none of it depends on the engine so
there is no reason to keep it shut in there. It is written from scratch, not built over somebody
else's library.

## Backends

| Backend     | Platforms                        |
|-------------|----------------------------------|
| Vulkan      | Windows, Linux, macOS (MoltenVK) |
| Direct3D 12 | Windows                          |
| Metal       | macOS, iOS                       |
| Null        | everywhere                       |

Null implements the full interface without touching a driver. It creates devices, records command
lists and hands back handles without ever reaching a GPU. That is what lets the suite run on a
machine with no GPU. It also keeps a sample useful when all it wants to show is API shape.

Which backend a build contains is a configure-time choice and which one it uses is a runtime one.
See [picking a backend](guides.md#picking-a-backend).

## What the API holds to

These are the rules the library is written to. Each one has a build gate or a test behind it.

**Errors are values.** Nothing throws across the public API. A call that can fail returns
rhi::Result, which is either the value or an Error carrying a code. There is a build for hosts
compiled with -fno-exceptions, where the library neither throws nor requires exceptions.

**No silent no-ops.** An operation a backend cannot perform reports eUnsupportedFeature. A call
that returns success while recording nothing is the worst failure mode here, because nothing
downstream can tell it happened.

**Capabilities match behaviour.** If DeviceCaps says a device can do something, it does. Support is
queried and no feature level stands in for the answer.

**The API names no graphics API.** No header you get from linking azoth::rhi includes a Vulkan,
D3D12 or Metal header. The one quarantined area is azoth/rhi/native/, which has a target of its own.
A build gate and a CTest case both check this. See [reaching the native
objects](guides.md#reaching-the-native-objects).

## Quick start

```cpp
#include <azoth/rhi/device/selection.hpp>
#include <azoth/rhi/rhi.hpp>

#include <print>

namespace rhi = azo::rhi;

int main()
{
    rhi::BackendSelection backends;

    rhi::DeviceDesc desc{};
    desc.requireSwapchain = false; // headless so no surface is needed

    const rhi::Result<rhi::UniqueDevice> device = backends.CreateDevice(desc);
    if (!device)
    {
        std::println("no device (error code {})", static_cast<unsigned>(device.GetError().code));
        return 1;
    }

    const rhi::Device handle = device.Value().Get();
    std::println("{}, {} graphics queues", handle.GetGraphicsApiName(), handle.GetCaps().graphicsQueueCount);
}
```

Constructing a BackendSelection registers every backend the build has and moves the one you asked
for to the front. Creating from it takes the first backend that comes up.

## Building

| Requirement | Minimum |
|-------------|---------|
| CMake       | 3.24    |
| GCC         | 14      |
| Clang       | 18      |
| MSVC        | 19.43   |

The build fetches the Vulkan and Metal headers at configure time. Direct3D 12 comes from the Windows
SDK. You still need a driver at runtime.

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build
```

Every knob the build takes is in [options](options.md).

## Linking it from your own build

```cmake
find_package(AzothRHI CONFIG REQUIRED)
target_link_libraries(my_renderer PRIVATE azoth::rhi)
```

FetchContent works as well. The exported targets are azoth::rhi for the library,
azoth::rhi-backend-sdk and azoth::rhi-module-sdk for writing a backend and
azoth::rhi-native-vulkan for the quarantined native access.

## Running the tests

The suite runs over every backend the build contains. A backend with no driver on your machine skips
without failing the run so a Metal-only laptop still gets a useful run.

```bash
ctest -L unit        # the per-module suites
ctest -L conformance # the cross-backend contracts, including gate_api_boundary
ctest -L rigorous    # the slower cross-backend campaigns
```

AZOTH\_RHI\_TEST\_BACKENDS and AZOTH\_RHI\_TEST\_REQUIRE\_BACKENDS narrow a run or turn a skipped
backend into a failure. Both are described in [options](options.md#test-environment-variables).

## Extras

Two optional targets sit beside the library. Neither has install rules so find\_package will not see
them. Add this repository as a subdirectory or through FetchContent instead.

**azoth::rhi-utils** does scaled blits and mip chain generation, by hardware blit where the device
has one and by compute where it does not. It builds when slangc is available to compile its shaders
ahead of time and it is skipped when there is none.

**azoth::rhi-imgui** is the drawing half of a Dear ImGui backend, the part that belongs to a
graphics API. Turn it on with AZOTH\_RHI\_BUILD\_IMGUI. It links the ImGui you already have and
never fetches its own, because a second copy in the same process is a second context and a second
font atlas.
