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

Azoth RHI is a C++23 render hardware interface with a shared handle-based API for Vulkan, Direct3D 12, and Metal.

## Backends

| Backend     | Platforms                        |
|-------------|----------------------------------|
| Vulkan      | Windows, Linux, macOS (MoltenVK) |
| Direct3D 12 | Windows                          |
| Metal       | macOS, iOS (Metal 3 and Metal 4) |

Metal 3 and Metal 4 are separate backends. The Apple default prefers Metal 4 when built. If device creation fails,
selection tries the remaining backends. See [the backend options](options.md#backends).

Choose which backends to compile at configure time and [which to use](guides.md#picking-a-backend) at runtime.

## What the API holds to

- Calls that can fail return rhi::Result with a value or an Error. Exceptions must not cross the public API.
- Unsupported operations return eUnsupportedFeature.
- Query DeviceCaps for supported features. Reported capabilities must match device behavior.
- Public headers include Vulkan, D3D12, and Metal headers only through azoth/rhi/native/ and its separate targets.
  A build gate and a CTest case check this boundary. See [reaching the native objects](guides.md#reaching-the-native-objects).

AZOTH\_RHI\_NO\_EXCEPTIONS supports hosts compiled with -fno-exceptions.

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

By default, BackendSelection tries the requested backend first, then falls back until one creates a device.

## Building

Use CMake 3.24 or newer and a compiler with C++23 library support. The [CI matrix](../.github/workflows/ci.yml)
uses GCC 14, Clang 18, AppleClang, and MSVC.

The build fetches Vulkan and Metal headers for enabled backends. Direct3D 12 uses the Windows SDK.
Hardware backends also need a runtime driver.

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build
```

See [options](options.md) for build configuration.

## Linking it from your own build

```cmake
find_package(AzothRHI CONFIG REQUIRED)
target_link_libraries(my_renderer PRIVATE azoth::rhi)
```

You can also include the library through FetchContent. Exported targets are:

- azoth::rhi: the library.
- azoth::rhi-backend-sdk and azoth::rhi-module-sdk: custom backends.
- azoth::rhi-native-vulkan and azoth::rhi-native-metal: native access when the corresponding backend is enabled.

## Running the tests

The suite tests the compiled backends and skips those unavailable on your machine.
From the build directory, filter tests by label:

```bash
ctest -L unit        # the per-module suites
ctest -L conformance # the cross-backend contracts, including gate_api_boundary
ctest -L rigorous    # the slower cross-backend campaigns
```

AZOTH\_RHI\_TEST\_BACKENDS selects backends. With AZOTH\_RHI\_TEST\_REQUIRE\_BACKENDS, device creation errors fail
the test except for eNoCompatibleAdapter, which still skips. See [test options](options.md#test-environment-variables).

## Extras

Use add\_subdirectory or FetchContent for these targets. They are not installed.

- azoth::rhi-utils provides scaled blits and mip generation using hardware blits or compute. It requires slangc
  and an enabled hardware backend to compile its shaders.
- azoth::rhi-imgui renders Dear ImGui draw lists. Enable AZOTH\_RHI\_BUILD\_IMGUI and provide an ImGui target,
  or enable AZOTH\_RHI\_FETCH\_IMGUI to fetch one.
