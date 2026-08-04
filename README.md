# Azoth RHI

A C++23 render hardware interface.

The Azoth RHI comes out of the Azoth engine, my personal closed source game engine. Performance was the priority. I've
also decided to make it open source to help out others I know in the graphics space.

| Backend     | Platforms                        |
|-------------|----------------------------------|
| Vulkan      | Windows, Linux, macOS (MoltenVK) |
| Direct3D 12 | Windows                          |
| Metal       | macOS, iOS (Metal 3 and Metal 4) |
| Null        | everywhere                       |

To learn more about Azoth RHI, how to build it and what it can do, read [the docs](docs/index.md).

## Examples

The [examples/](examples) directory has samples from a headless adapter report to a glTF scene with image based
lighting. See [the examples doc](docs/examples.md) for what each one shows.

## License

Azoth RHI is licensed under Apache 2.0. See [LICENSE](LICENSE) for the terms and [NOTICE](NOTICE)
for attribution. The Azoth engine itself is closed source and is not covered by this license.

Libraries embedded in Azoth RHI:

- [vk-dynamic](https://github.com/Rinzii/vk-dynamic). Licensed under Apache-2.0 OR MIT.
- [metal-cpp](https://github.com/apple/metal-cpp). Present when the Metal backend is on. Licensed
  under Apache-2.0.
- [VulkanMemoryAllocator](https://github.com/GPUOpen-LibrariesAndSDKs/VulkanMemoryAllocator) and
  [D3D12MemoryAllocator](https://github.com/GPUOpen-LibrariesAndSDKs/D3D12MemoryAllocator), each
  present with its backend. Licensed under MIT.

Conditional licenses:

- [Tracy](https://github.com/wolfpld/tracy), provided through AZOTH\_RHI\_TRACY\_TARGET. Licensed under BSD-3-Clause.
- WinPixEventRuntime, provided through AZOTH\_RHI\_PIX\_TARGET. Microsoft proprietary.
