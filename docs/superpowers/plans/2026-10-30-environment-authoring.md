# Editable differential environments

Goal: let standalone and embedding users define simple spectra, inspect their sampling convention and normalization, and run a reproducible two-particle example without external mission files.

- [x] 1. Immutable Qt-free spectrum editing/removal with prepared distributions, input validation, explicit synthetic provenance and preservation of unedited spectra.
- [x] 2. Shared add/edit/remove spectrum dialog, linear/bin-constant preview, normalization summary and duration-aware flux/fluence conversion. Reject stale modal edits and block changes during transport.
- [x] 3. Model tests and native UI checks, including roundtrip, invalid/cancel/stale edits, normalization equivalence and two real-Worker runs.
- [x] 4. Rebuild installed SDK, standalone/embedded/TKMaster hosts and a new 0.1.2 local package; capture acceptance and update product checklist.

This milestone executes no AP8/AE8 or orbit model and makes no new physical-validation claim. Manual inputs retain the existing isotropic omnidirectional-scalar convention. Linear interpolation and bin-constant values retain their different meanings; changing quantity requires numeric conversion, not relabeling. Imported spectrum provenance is replaced only for the edited spectrum, while stale global orbit-model metadata is cleared on manual edits.

Completed 2026-10-30. Evidence: [environment acceptance](../../ENVIRONMENT_ACCEPTANCE.md) and `reports/environment/`. Version 0.1.2: 35/35 CTests, 41 environment checks, 31 UI checks with each of two Worker locations, 27 authoring regressions, actual TKMaster acceptance and two packaged 100,000-history examples.
