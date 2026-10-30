# Release readiness

## Local foundation milestone — completed 2026-10-30

- [x] Independent Windows/MSVC build; no TKMaster/PIC linkage in the standalone product; 33/33 CTests.
- [x] Real-Worker service checks (16): seeded repeatability, progress, output protection, busy rejection, cancellation, direct-signal cancellation, snapshot-tamper rejection and unchanged host environment.
- [x] Same widget in standalone and embedded-example shells; save/reopen, exact parameter round-trip, stale-result clearing, cancellation and destruction while running.
- [x] Installed CMake SDK consumed with a deliberately competing legacy header search directory; public types remain distinct.
- [x] Actual TKMaster opt-in dock: JSON input/result bridge and host scene/old radiation project preservation.
- [x] Local runtime bundle with 90 resolved dependency DLLs and Qt plugins, executed with developer library paths removed. Geant4 data remain external and explicitly configured.
- [x] Read-only code review findings resolved; source import identities and namespace-only transformation recorded.

Evidence and exact limits: [bootstrap acceptance](BOOTSTRAP_ACCEPTANCE.md). These check marks cover this foundation milestone only.

## Subsequent product work

- [x] Basic project creation and analytic component add/edit/remove dialogs; model/tree/result selection and 3D focus. Version 0.1.1: 34/34 CTests, 27 native workflow checks and real TKMaster revalidation. See [authoring acceptance](AUTHORING_ACCEPTANCE.md).
- [x] Editable manual differential spectra, linear/bin-constant previews, normalization/sampling shares, explicit flux/fluence conversion and portable synthetic two-particle examples. Version 0.1.2: 35/35 CTests, 31 native workflow checks and actual TKMaster environment verification. See [environment acceptance](ENVIRONMENT_ACCEPTANCE.md).
- [ ] Extended spacecraft model import/assembly workflows, placement tools and full CAD interaction.
- [ ] Reference environment/model execution and qualified environment examples; current first UI uses synthetic or externally supplied spectra.
- [ ] Calibrated device DDD/SEE workflows and response data.
- [ ] Independent physical validation and geometry/dose sensitivity studies.

## Public-release gates — still open

- [ ] Code ownership/provenance review and selection of the project's license. No redistribution permission is asserted by this local extraction.
- [ ] Third-party license review, required notices/source offers, and data/example redistribution rights.
- [ ] Clean Windows machine installation/runtime acceptance, including dataset provisioning and required system runtimes.
- [ ] CI on supported toolchains, release packaging and installation documentation; Linux has not been built or accepted.
- [ ] Public GitHub repository/release publication. This batch creates a local repository only.
