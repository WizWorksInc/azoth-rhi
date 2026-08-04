# Examples

The samples are off by default because some of them carry dependencies nothing else here needs.

```bash
cmake -B build -DAZOTH_RHI_BUILD_EXAMPLES=ON
cmake --build build
```

A sample missing something it needs is skipped instead of failing the configure so a machine
without SDL3, GLFW or Slang still builds the rest.

The tree is split in two on purpose. examples/lib is the framework every sample links and it may
grow abstractions and take dependencies. examples/samples is what a reader came for: one directory each, no sample
depending on another. A sample that hides an RHI call behind a framework helper is not showing you the RHI call. So what
lives in examples/lib is the part that is not about the RHI:
windowing, logging, assets and scene loading.

Most samples take a backend name as their first argument so the same binary can be pointed at
whichever backend the build contains.

## Headless

These run over the Null backend and open no window.

**device\_info** asks a device for every optional feature, preferred and not required. Then it prints what it granted. A
device grants nothing the caller did not ask for. A sample whose job is reporting what a device can do has to request
everything or it reports a row of no.

**adapter\_report** enumerates what the build can reach and reports each adapter it finds.

**buffer\_roundtrip** creates a buffer, writes to it, copies through a command list and reads the bytes back. Nothing
smaller proves that recording and submission work.

**offscreen\_clear** clears a render target that no window owns and copies the result back to the
host so the drawing half is exercised without a surface.

**frame\_pacing** drives several frames against the frame ring and its fences. The rules about how many frames may be
recorded ahead of the GPU live there.

**unique\_handles** shows rhi::Unique, the first of the two owning tiers. Unique owns lifetime and
nothing else. The flat handle API stays the whole API and what the owner removes is the Destroy call, not the handle. A
Unique still goes to every function that takes the plain handle.

**raii\_handles** shows rhi::raii, the second tier. The device vends owners and failure arrives as a
value so raii::Device::CreateBuffer hands back a Result of an owner. A setup that creates a dozen things is a dozen
lines and none of them can leak by forgetting a Destroy.

**profiler\_sink** implements a sink that tallies instead of visualizing. It prints what a Tracy,
PIX or Optick integration would be drawing: which zones the library opened, how often and what its
counters were doing. A real sink forwards to a tool and the shape is the same either way.

**tracy\_profiler** installs the bundled Tracy sink beside a second sink that only counts. What that shows is the
fan-out through BroadcastProfiler: installing Tracy's sink does not cost the application its own. It paces the frames it
records because a timeline where every frame landed in the same millisecond has nothing in it to read. Needs a Tracy
client. AZOTH\_RHI\_TESTS\_FETCH\_TRACY can fetch one.

**shader\_languages** runs several shading languages against one RHI, each on the kernel it is good at. They do not
reach the same answer. What they share is that a binding lands where the RHI's ABI says it does, whichever language
declared it. Needs Slang.

**shader\_reflection** builds a pipeline layout out of the shader instead of beside it. Every other sample writes its
bindings twice, once in the shader and once in an array that has to agree with it. Nothing checks that they do. Needs
Slang.

**bindless** fills a descriptor set against a layout that declares room for far more textures than it allocates. An
unbounded binding still names an upper bound and the allocation picks the real length below it so one layout built once
serves however many textures a scene turns out to have. Part of the set is written before the command list is recorded
and the rest after it is closed. That is what eUpdateAfterBind is for. Needs Slang.

**gpu\_timing** reads timestamp queries around a compute dispatch and a render pass. The slots cover submission, the
dispatch and the pass. The dispatch runs long enough to last milliseconds and not a handful of ticks. That is what keeps
the measurement above the cost of taking it. Needs Slang.

## Windowed

These open a window.

**hello\_triangle** is the smallest thing that draws on an SDL3 window. It also shows what each
backend wants its shaders in. Needs SDL3 and Slang.

**windowing\_sdl3** implements rhi::SurfaceSource against SDL3 itself instead of using the shared window from
examples/lib because showing how that is done is what it is for. One method, one branch per payload the window can
answer and no graphics types in sight. The exception is creating the Vulkan surface. That cannot be written without them
so it lives in native/ where they are allowed. Needs SDL3.

**windowing\_glfw** does the same against GLFW. The graphics API is decided before anything else because the window has
to be made for it and a Vulkan device is dispatched through the loader the window library brought up. Needs GLFW.

**imgui\_overlay** draws Dear ImGui through the RHI. ImGui is split in two everywhere it is used and
this sample brings neither half itself: the input half is ImGui's own SDL3 backend and the drawing
half is azoth::rhi-imgui. Needs SDL3 and Dear ImGui.

**deccer\_cubes** loads a glTF scene and lights it with an environment map. That makes it the one
sample that looks like a renderer. It fetches the glTF at configure time. Needs SDL3, Slang and the
scene loading dependencies.
