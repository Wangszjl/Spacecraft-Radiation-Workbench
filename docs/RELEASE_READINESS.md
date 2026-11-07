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
- [x] Shared-service CLI and Qt Core runtime loading: project/runtime checks, NDJSON progress, accepted-result summaries, timeout/interruption and stable exits. Version 0.1.3: 37 CTests; 49 Windows / 53 Linux real CLI checks; desktop and actual host regressions passed. [Acceptance](CLI_ACCEPTANCE.md).
- [x] Saved-job review and dose comparison in desktop/embedded widget, Qt Core SDK and CLI. Version 0.1.4: 38 CTests, 38 real shielding/archive integration checks, 17 native viewer checks, portable archive loading and existing GUI/TKMaster regressions. [Behavior](RESULTS.md), [acceptance](RESULTS_ACCEPTANCE.md).
- [x] Automated fixed-cavity box-shell thickness scans, per-species real transport, cancellation, dose/error curves, exports and offline reconstruction. Version 0.1.5: 39 CTests, 94 Windows CLI/integration checks including 19 service checks, and 19 final native UI checks. [Behavior](SHIELDING_SCAN.md), [acceptance](SHIELDING_SCAN_ACCEPTANCE.md).
- [x] Spatial TID scoring for uniform sensitive boxes: project configuration, strict worker result validation, VTK overlay, slice view, and portable VTI/CSV export. Version 0.1.6 passed local and hosted Worker voxel validation. [Behavior](VOXEL_SCORING.md), [acceptance](VOXEL_ACCEPTANCE.md).
- [ ] Extended spacecraft model import/assembly workflows, placement tools and full CAD interaction.
- [x] Notional spacecraft TID benchmark: 16 material volumes, six sensitive parts, two bus-wall thicknesses and 35 audit checks across two sampling budgets. [Evidence and unresolved interior statistics](SPACECRAFT_TID_BENCHMARK.md).
- [x] Fixed-budget spectral stratification with exact exposure reconstruction and a real 8-million-history equal-budget comparison. [Scope and remaining zero-hit bands](STRATIFIED_TID.md).
- [x] Opt-in schema-v8 energy/species importance sampling, conditional moment reconstruction and real equal-budget spacecraft comparison; 43 local CTests and 7 real packaged checks. [Evidence and statistical limits](IMPORTANCE_SAMPLING.md).
- [x] Live and historical weighted-result statistics viewer, shared Qt-free review API, CLI summaries and precise exports; 44 local CTests and 25 native workflow checks. [Review behavior](IMPORTANCE_REVIEW.md).
- [x] Desktop importance proposal configuration with precision-preserving save/reopen and a public copy-based Model API; 46 CTests and 27 native configuration checks. [Editor](SOURCE_SAMPLING_EDITOR.md).
- [x] Narrow-band spacecraft energy-response diagnosis: 22 actual Worker jobs, 2.2 million histories, 132 target/energy rows, exact offline reconstruction and 7 real normalization checks. [Response study and limits](ENERGY_RESPONSE_STUDY.md).
- [x] Full-task-spectrum partition/seed study: 8 million histories, 48 actual tasks, archived JSON/CSV/HTML reconstruction, zero-event probability limits distinct from dose bounds; 47 CTests and 11 real acceptance checks. [Results and unresolved interior support](PARTITION_STUDY.md).
- [x] Native desktop planning, execution, cancellation and aggregate review for full-spectrum stratified studies; shared Qt Core service and relocated SDK consumer. [Workflow and acceptance](NATIVE_PARTITION_STUDY.md).
- [ ] Establish adequate statistics for the shielded interior and repeat with representative imported CAD geometry.
- [x] Run AE8MIN/AP8MIN on an idealized orbit, generate a traceable project and verify a two-target Geant4 shielding scan. [Reference workflow](AE8_AP8_EXAMPLE.md).
- [x] Import timestamped GEO/GEI ephemerides, reproduce a published SpacePy point and cross-check four RADBELT points. Transport a spectrum from a public Van Allen Probe A trajectory segment through demo scan and voxel projects. [External ephemeris evidence](AE8_AP8_EPHEMERIS.md).
- [x] Integrated external-ephemeris desktop AE8MIN/AP8MIN execution, C++ raw-spectrum reconstruction, explicit guarded application and offline model-job review. Model dependencies run in a separately configured process. [Workflow](ORBIT_DESKTOP.md).
- [ ] Qualified full-mission environments, broader model choices and independent measured-environment comparisons.
- [x] User-supplied device DDD/SEE response-table fold, provenance, no-extrapolation checks, result archive and SDK API. This is deterministic response screening, not calibrated Geant4 DDD/DPA or SEE event transport. [Scope](DEVICE_RESPONSE.md).
- [ ] Geant4 displacement-damage/DPA and SEE event-level processes, calibrated response data and independent physical validation.
- [x] First external-reference TID regression: thin silicon sphere, 10/20 MeV protons, analytic entry and fluence checks, 38 Windows checks. [PSTAR benchmark](PSTAR_BENCHMARK.md).
- [ ] Independent physical validation and geometry/dose sensitivity studies.

## Public-release gates — still open

- [ ] Code ownership/provenance review and selection of the project's license. No redistribution permission is asserted by this local extraction.
- [ ] Third-party license review, required notices/source offers, and data/example redistribution rights.
- [ ] Clean Windows machine installation/runtime acceptance, including dataset provisioning and required system runtimes.
- [x] Model-only Windows/MSVC and Linux/GCC CI: 13 CTests and 10 relocated-SDK checks on each platform. [Actual successful run](https://github.com/Wangszjl/Spacecraft-Radiation-Workbench/actions/runs/41189019059), [scope and evidence](MODEL_CI_ACCEPTANCE.md).
- [x] Linux headless Geant4 11.4.3 Worker CI: 35 CTests and 57 real transport/service checks; new Windows headless build also passes. [Successful run](https://github.com/Wangszjl/Spacecraft-Radiation-Workbench/actions/runs/41191922702), [evidence and scope](WORKER_CI_ACCEPTANCE.md).
- [ ] Full desktop CI and Windows hosted Worker CI, release packaging and installation documentation; Linux desktop rendering has not been accepted.
- [x] Public source repository created and source history pushed on 2026-10-31: [Wangszjl/Spacecraft-Radiation-Workbench](https://github.com/Wangszjl/Spacecraft-Radiation-Workbench), default branch `main`. Local SDK/data/build/report/runtime directories are excluded.
- [ ] Packaged binary release publication after the remaining applicable distribution and installation checks. Public source hosting does not establish license clearance or scientific validation.
