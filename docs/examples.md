# Examples

The samples are off by default, because some of them carry dependencies nothing else here needs.

```bash
cmake -B build -DAZOTH_RHI_BUILD_EXAMPLES=ON
cmake --build build
```

A sample missing something it needs is skipped instead of failing the configure so a machine
without SDL3, GLFW or Slang still builds the rest.

The tree is split in two on purpose. examples/lib is the framework every sample links and it may
grow abstractions and take dependencies. examples/samples is what a reader came for: one directory
each, no sample depending on another. A sample that hides an RHI call behind a framework helper has
made itself worse at the one thing it is for so what lives in examples/lib is the part that is not
about the RHI, which is windowing, logging, assets and scene loading.

Most samples take a backend name as their first argument so the same binary can be pointed at
whichever backend the build contains.

## Headless

These run over the Null backend so they are worth running on a machine with no GPU at all. Each
carries a smoke test.

**device\_info** asks a device for every optional feature, preferred and not required, then prints
what it granted. A device grants nothing the caller did not ask for so a sample whose job is
reporting what a device can do has to ask for everything or it reports a row of no.

**adapter\_report** enumerates what the build can reach and reports each adapter it finds.

**buffer\_roundtrip** creates a buffer, writes to it, copies through a command list and reads the
bytes back, which is the smallest end to end proof that recording and submission work.

**offscreen\_clear** clears a render target that no window owns and copies the result back to the
host so the drawing half is exercised without a surface.

**frame\_pacing** drives several frames against the frame ring and its fences, which is where the
rules about how many frames may be recorded ahead of the GPU live.

**unique\_handles** shows rhi::Unique, the first of the two owning tiers. Unique owns lifetime and
nothing else. The flat handle API stays the whole API and what the owner removes is the Destroy
call, not the handle so a Unique still goes to every function that takes the plain handle.

**raii\_handles** shows rhi::raii, the second tier. The device vends owners and failure arrives as a
value so raii::Device::CreateBuffer hands back a Result of an owner. A setup that creates a dozen
things is a dozen lines that cannot get the test wrong by leaving one out.

**profiler\_sink** implements a sink that tallies instead of visualizing. It prints what a Tracy,
PIX or Optick integration would be drawing: which zones the library opened, how often and what its
counters were doing. A real sink forwards to a tool and the shape is the same either way.

**shader\_languages** runs four shading languages against one RHI, each on the kernel it is good at.
They do not reach the same answer. What they share is that a binding a shader declares lands where
the RHI's ABI says it does, whichever language declared it. Needs Slang.

**shader\_reflection** builds a pipeline layout out of the shader instead of beside it. Every other
sample writes its bindings twice, once in the shader and once in an array that has to agree with it
and nothing checks that they do. Needs Slang.

## Windowed

These open a window so none of them carries a smoke test.

**hello\_triangle** is the smallest thing that draws, on an SDL3 window. It also shows what each
backend wants its shaders in. Needs SDL3 and Slang.

**windowing\_sdl3** implements rhi::SurfaceSource against SDL3 itself instead of using the shared
window from examples/lib, because showing how that is done is what it is for. One method, one branch
per payload the window can answer and no graphics type in sight, except for creating the Vulkan
surface, which cannot be written without them so it lives in native/ where they are allowed. Needs
SDL3.

**windowing\_glfw** does the same against GLFW. One API is decided before anything else, because the
window has to be made for it and a Vulkan device is dispatched through the loader the window library
brought up. Needs GLFW.

**imgui\_overlay** draws Dear ImGui through the RHI. ImGui is split in two everywhere it is used and
this sample brings neither half itself: the input half is ImGui's own SDL3 backend and the drawing
half is azoth::rhi-imgui. Needs SDL3 and Dear ImGui.

**deccer\_cubes** loads a glTF scene and lights it with an environment map, which makes it the one
sample that looks like a renderer. It fetches the glTF at configure time. Needs SDL3, Slang and the
scene loading dependencies.
