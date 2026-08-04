# Guides

- [Picking a backend](#picking-a-backend)
- [Presenting to a window](#presenting-to-a-window)
- [Installing a profiler](#installing-a-profiler)
- [Owning device memory](#owning-device-memory)
- [Owning the library's CPU allocations](#owning-the-librarys-cpu-allocations)
- [Reaching the native objects](#reaching-the-native-objects)
- [Adding a backend of your own](#adding-a-backend-of-your-own)

## Picking a backend

A BackendSelection owns the registry and the order. Default construction registers every backend the build has and moves
the one you asked for to the front. Creation then takes the first backend that comes up.

The one you asked for is resolved in order. A name in BackendPreference::requested wins. When that is null and
consultEnvironment is left on, the AZOTH\_RHI\_BACKEND environment variable is read. When neither names anything, the
configure-time AZOTH\_RHI\_DEFAULT\_BACKEND is used: Metal 4 on Apple where that backend is built, Metal 3 on Apple
otherwise, Direct3D 12 on Windows and Vulkan everywhere else.

A name is accepted in either its short form or its canonical one so a command line can say vulkan and a configuration
file can say azoth.rhi.vulkan.

```cpp
rhi::BackendPreference preference{};
preference.requested = "vulkan";  // wins over the environment and the build default
preference.includeNull = false;   // a program that must draw should not fall through to Null

rhi::BackendSelection backends(preference);
```

includeNull is worth setting false in anything that has to put pixels on a screen. Left on, a host with no working
driver quietly gets a device that records nothing and reports success. That is
exactly the failure the rest of the library is built to avoid.

Set includeAvailable to false to register your own backends first and call AddAvailable later for the compiled-in
remainder. AvailableBackends reports what the build actually contains and not what it declares. A backend compiled out
is absent from that list. Its API tag and its typed entry point are still declared so a backend of your own can fill
them.

## Presenting to a window

The library owns no window and has no windowing dependency. You implement rhi::SurfaceSource and
each backend asks it for what that backend needs: the loader entry point and a surface for Vulkan,
the CAMetalLayer for Metal, the HWND for Direct3D 12.

A request arrives identified by an interface id and a payload size. You fill the one you support. A request from newer
headers than you were built against is read as a prefix so a source written today keeps working against a later library.

SDL3 and GLFW implementations live in the examples. The windowing\_sdl3 and windowing\_glfw samples implement rhi::
SurfaceSource themselves because showing how that is done is what they are for. Every other windowed sample links the
shared SDL3 window from examples/lib and that is the part that is not about the RHI.

## Installing a profiler

Implement rhi::Profiler and install it with SetProfiler. You get CPU zones for the library's own
work, queue and pool counters, device memory events and GPU zones recorded into a command list.

Every method has a do-nothing default so an implementation only overrides what it cares about. BroadcastProfiler
forwards to several sinks at once so the bundled Tracy sink stays alongside your own.

```cpp
class MySink final : public rhi::Profiler
{
    // override only what you want
};

MySink sink;
rhi::SetProfiler(&sink);
```

Building with AZOTH\_RHI\_ENABLE\_PROFILING off expands every instrumentation point to nothing so
an installed sink is never consulted and the calls are not there to consult it.

Debug labels are separate and always on. They go out through VK\_EXT\_debug\_utils, PIX events and
Metal debug groups whether or not a sink is installed. A capture in RenderDoc or PIX is readable
without turning profiling on.

The profiler\_sink sample implements a sink that tallies instead of visualizing. It prints what a
Tracy, PIX or Optick integration would be drawing.

## Owning device memory

Implement rhi::DeviceMemoryAllocator. Every buffer and texture then goes into a span you granted so
the backend makes no allocation of its own. The interface deals in HeapHandle and byte offsets and
it never names VkDeviceMemory or ID3D12Heap so one implementation serves every backend.

This is how a host with its own budgeting, defragmentation or residency policy keeps that policy in
one place instead of splitting it across the RHI and everything above it.

## Owning the library's CPU allocations

Implement rhi::HostAllocator and install it with SetHostAllocator. The library's own heap allocations then route through
it. A build gate checks that nothing in the library allocates around that seam so the hook covers the whole library
instead of the parts somebody remembered to route.

Under AZOTH\_RHI\_NO\_EXCEPTIONS a host allocation that fails aborts instead of reporting. The standard library already
does the same under -fno-exceptions. eOutOfHostMemory survives only where
refusal is explicit and already checked.

## Reaching the native objects

Sometimes you need the VkDevice. Everything in azoth/rhi/native/ is quarantined behind a separate
target so reaching for it is a deliberate act in your build file and not an accident in a header.

```cmake
target_link_libraries(my_tooling PRIVATE azoth::rhi-native-vulkan)
```

No header you get from linking azoth::rhi includes a Vulkan, D3D12 or Metal header. A build gate and a CTest case both
check that.

## Adding a backend of your own

A backend outside this repository links azoth::rhi-backend-sdk. That is the public headers plus the
pieces a backend needs and would otherwise reimplement: slot maps, the host allocation seam, format and subresource
arithmetic plus the validation registry.

A backend that ships as a loadable module links azoth::rhi-module-sdk instead and compiles its entry
point through AZO\_RHI\_DEFINE\_MODULE. The module carries an ABI stamp that has to agree with the host before anything
in it is called. AZOTH\_RHI\_NO\_EXCEPTIONS is part of that stamp because a module and a host that disagree on it
disagree about what an allocation failure does.

Register what you built with BackendSelection::Add or let AddCatalog find it. A self-registered backend with the same
GraphicsApiId as a bundled one replaces it. A host substitutes an implementation that way without patching the library.
