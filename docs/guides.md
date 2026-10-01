# Guides

- [Picking a backend](#picking-a-backend)
- [Configuring a backend](#configuring-a-backend)
- [Presenting to a window](#presenting-to-a-window)
- [Installing a profiler](#installing-a-profiler)
- [Owning device memory](#owning-device-memory)
- [Owning the library's CPU allocations](#owning-the-librarys-cpu-allocations)
- [Reaching the native objects](#reaching-the-native-objects)
- [Adding a backend of your own](#adding-a-backend-of-your-own)

## Picking a backend

BackendSelection owns the registry and backend order. By default it registers the compiled backends and tries the
preferred one first. Device creation falls back to the next backend if that fails.

The first nonempty preference wins:

- BackendPreference::requested.
- AZOTH\_RHI\_BACKEND, when consultEnvironment is true.
- AZOTH\_RHI\_DEFAULT\_BACKEND, set at configure time.

The build default is Metal 4 on Apple when built, Metal 3 on Apple otherwise, Direct3D 12 on Windows, and Vulkan elsewhere.
Both short and canonical names work, such as vulkan and azoth.rhi.vulkan.

```cpp
rhi::BackendPreference preference{};
preference.requested = "vulkan";  // wins over the environment and the build default
preference.includeNull = false;   // a program that must draw should not fall through to Null

rhi::BackendSelection backends(preference);
```

Set includeNull to false when the program needs to render. Otherwise it can fall back to a Null device that reports
success without drawing.

Set includeAvailable to false to register custom backends first, then call AddAvailable for the compiled backends.
AvailableBackends lists only compiled backends. Disabled backends retain their API tags and typed entry points for
custom implementations.

## Configuring a backend

Include the backend's native configuration header and set its fields through DeviceBuilder. Configure sets device
options. ConfigureInstance sets options for the instance created by Build.

```cpp
#include <azoth/rhi/builders/device_builder.hpp>
#include <azoth/rhi/native/vulkan_config.hpp>

rhi::DeviceBuilder builder;
builder.ConfigureInstance<rhi::VulkanApi>([](rhi::native::VulkanInstanceConfig & config) {
    config.minimumInstanceVersion = { .major = 1, .minor = 3 };
});
builder.Configure<rhi::VulkanApi>([](rhi::native::VulkanDeviceConfig & config) {
    config.deviceVersion = { .major = 1, .minor = 3 };
});
auto device = builder.Build<rhi::VulkanApi>();
```

Each backend reads only its own block, so a builder can carry several APIs' configurations while trying a preferred
order. Repeated configuration preserves fields from the previous call. Copies of a builder can be configured
independently. Extension name spans remain borrowed and must stay alive through Build.

For direct creation, put device blocks in DeviceDesc::backendConfigs and instance blocks in DeviceDesc::instanceConfigs.
A standalone InstanceDesc takes instance blocks in backendConfigs. An explicit Vulkan device version cannot exceed
the instance's version. With no device version set, the backend limits its default to the instance's target.

## Presenting to a window

Implement rhi::SurfaceSource to supply the Vulkan loader entry point and surface, a CAMetalLayer for Metal, or an HWND
for Direct3D 12. The library has no windowing dependency.

Requests carry an interface ID and payload size. Fill the payloads you support. SurfacePayloadOf accepts a larger
payload with the same ID, allowing compatible fields to be appended.

The windowing\_sdl3 and windowing\_glfw samples implement SurfaceSource directly. Other windowed samples use the shared
SDL3 window in examples/lib.

## Installing a profiler

Implement rhi::Profiler and install it with SetProfiler to receive CPU zones, queue and pool counters, device memory
events, and GPU zones. Override the callbacks you need. BroadcastProfiler forwards events to multiple sinks, including
the bundled Tracy sink.

```cpp
class MySink final : public rhi::Profiler
{
    // override only what you want
};

MySink sink;
rhi::SetProfiler(&sink);
```

AZOTH\_RHI\_ENABLE\_PROFILING=OFF removes the instrumentation calls.

Debug labels work independently of profiling through VK\_EXT\_debug\_utils, PIX events, and Metal debug groups.
Direct3D 12 labels require AZOTH\_RHI\_PIX. Device settings also control whether labels are emitted.

The profiler\_sink sample prints event counts.

## Owning device memory

Implement rhi::DeviceMemoryAllocator to control buffer and texture placement. It supplies spans as HeapHandle values and
byte offsets, keeping budgeting, defragmentation, and residency policy independent of native API types.

## Owning the library's CPU allocations

Implement rhi::HostAllocator and install it with SetHostAllocator to handle the library's CPU heap allocations.
A build gate checks for allocations that bypass it.

With AZOTH\_RHI\_NO\_EXCEPTIONS, HostAllocatorAdapter aborts on allocation failure. Operations that explicitly check
allocation failure can still return eOutOfHostMemory.

## Reaching the native objects

Link a native target to access objects such as VkDevice through azoth/rhi/native/:

```cmake
target_link_libraries(my_tooling PRIVATE azoth::rhi-native-vulkan)
```

The azoth::rhi public headers exclude Vulkan, D3D12, and Metal headers. Build and CTest checks enforce this boundary.

## Adding a backend of your own

Link azoth::rhi-backend-sdk for the public headers, slot maps, host allocation helpers, format and subresource
arithmetic, and validation registry.

Loadable backends link azoth::rhi-module-sdk and define their entry point with AZO\_RHI\_DEFINE\_MODULE. The module's ABI
stamp must match the host before it is called. The stamp includes AZOTH\_RHI\_NO\_EXCEPTIONS because it changes allocation
failure behavior.

Register a backend with BackendSelection::Add or discover it with AddCatalog. A self-registered backend replaces a
bundled backend with the same GraphicsApiId.
