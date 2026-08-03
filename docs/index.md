# Azoth RHI

A C++23 render hardware interface with Vulkan, Direct3D 12 and Metal behind one API.

One handle-based API covers every backend. Errors come back as result types and nothing throws
across the public API. You ask a device what it can do and no feature level stands in for the
answer. An operation a backend cannot perform reports eUnsupportedFeature, never a success that
quietly records nothing.

It comes out of the Azoth engine, which is closed source, but none of it depends on the engine so
there is no reason to keep it shut in there. It is written from scratch, not built over somebody
else's library.

## Documentation

- [Overview](overview.md), what the library is, the rules it holds to and how to build and link it.
- [Guides](guides.md), how to do the things a renderer needs: pick a backend, present to a window,
  install a profiler, own device memory, reach the native objects, add a backend of your own.
- [Options](options.md), every CMake option and every environment variable the tests read.
- [Examples](examples.md), what each of the fifteen samples shows.

The [CONTRIBUTING.md](../CONTRIBUTING.md) file covers what a change has to hold to.
