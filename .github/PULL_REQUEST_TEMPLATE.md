## Summary

<!-- What this changes and why. Link the issue if there is one. -->

## Backends touched

- [ ] Vulkan
- [ ] Direct3D 12
- [ ] Metal
- [ ] Null
- [ ] Not backend specific

## Checks

- [ ] ctest passes locally, including the api_boundary case
- [ ] Tested against a real driver, not only the null backend
- [ ] Public headers include no native graphics header outside azoth/rhi/native/
- [ ] Operations a backend cannot perform report eUnsupportedFeature and never a silent success

## Notes

<!-- Optional. What reviewers should look at first and what hardware this ran on. -->
