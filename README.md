# Azoth RHI

A C++23 render hardware interface.

One handle-based API covers every backend. Errors come back as result types and nothing throws
across the public API. You ask a device what it can do and no feature level stands in for the
answer. An operation a backend cannot perform reports eUnsupportedFeature, never a success that
quietly records nothing.

The Azoth RHI comes out of the Azoth engine, which is closed source game engine the RHI was developed for.
The RHI has been written from the ground up for speed and performance. I've also decided to make it open source
to help out others I know in the graphics space.

| Backend     | Platforms                        |
|-------------|----------------------------------|
| Vulkan      | Windows, Linux, macOS (MoltenVK) |
| Direct3D 12 | Windows                          |
| Metal       | macOS, iOS                       |
| Null        | everywhere                       |

Null implements the full interface without touching a driver so the tests can run on a machine with no GPU also.

To learn more about Azoth RHI, how to build it and what it can do, read [the docs](docs/index.md).

## Examples and real-world usage

The [examples/](examples) directory has multiple samples, from a headless adapter report to a glTF
scene with image based lighting. See [the examples doc](docs/examples.md) for what each one shows.

## License

Azoth RHI is licensed under Apache 2.0, which is permissive so it can go into anything including
commercial and closed source work. See [LICENSE](LICENSE) for the terms and [NOTICE](NOTICE) for
attribution. The Azoth engine itself is closed source and is not covered by this license.

Libraries embedded in Azoth RHI:

- [vk-dynamic](https://github.com/Rinzii/vk-dynamic), Vulkan-Hpp plus dispatcher storage. Present
  when the Vulkan backend is on. Licensed under Apache-2.0 OR MIT.
- [metal-cpp](https://github.com/apple/metal-cpp). Present when the Metal backend is on. Licensed
  under Apache-2.0.
- [VulkanMemoryAllocator](https://github.com/GPUOpen-LibrariesAndSDKs/VulkanMemoryAllocator) and
  [D3D12MemoryAllocator](https://github.com/GPUOpen-LibrariesAndSDKs/D3D12MemoryAllocator), each
  present with its backend. Licensed under MIT.

Libraries you supply, linked only when you ask for them:

- [Tracy](https://github.com/wolfpld/tracy), through AZOTH\_RHI\_TRACY\_TARGET. Licensed under
  BSD-3-Clause.
- WinPixEventRuntime, through AZOTH\_RHI\_PIX\_TARGET. Microsoft proprietary.
