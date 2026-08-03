# Contributing

Issues and pull requests are welcome. For anything large, open an issue first.

## Building

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build
ctest --test-dir build
```

Build Release too before opening a pull request. Some things only show up there and CI runs both.

## What the tests expect

The suite runs over every backend the build contains. A backend with no driver on your machine
skips without failing the run so a Metal-only laptop still gets a useful run.

```bash
ctest -L unit        # the per-module suites
ctest -L conformance # the cross-backend contracts, including gate_api_boundary
ctest -L rigorous    # the slower cross-backend campaigns
```

AZOTH\_RHI\_TEST\_BACKENDS=vulkan,null restricts a run to named backends.
AZOTH\_RHI\_TEST\_REQUIRE\_BACKENDS=metal turns a skipped backend into a failure. CI sets it so a
driver that does not come up fails the job instead of passing with everything skipped.

## What a change has to hold to

- **The API boundary.** No public header may include a Vulkan, D3D12 or Metal header outside
  azoth/rhi/native/. A build gate and a CTest case both check this.
- **No silent no-ops.** An operation a backend cannot perform reports eUnsupportedFeature. A call
  that returns success while recording nothing is the worst failure mode here because nothing
  downstream can tell.
- **Capabilities match behavior.** If DeviceCaps says a device can do something, it does.
- **Errors, not exceptions.** Nothing throws across the public API.

## Style

The .clang-format file is the authority and CI enforces it so run clang-format before pushing.

Comments carry the reasoning, not a restatement of the next line. Say why something is the way it
is, especially where a backend forced the shape.

## Commits

Short imperative subject lines. Keep unrelated cleanup out of a change that is meant to be reviewed
for something else.
