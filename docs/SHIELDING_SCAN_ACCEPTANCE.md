# 0.1.5 fixed-cavity shielding scan acceptance

Recorded 2026-11-01. The functional service/CLI source revision is `b468dfb9c1cd3e7a87079b48fc085662099efc19`. A later desktop-only change adds scrolling for small scan windows; the final native Windows checks include it.

## Delivered scope

Fixed-budget, sequential electron/proton thickness scans, using the imported engine's fixed-inner-cavity planner and the shared `AnalysisService` for each actual Geant4 run. New shared Qt Core API, desktop/embedded scan window, dose plot with per-point MC SE, CLI planning/execution/inspection, full child archives and offline curve/CSV reconstruction. See [the workflow and precise rules](SHIELDING_SCAN.md).

The original engine transport sources and legacy batch/scan runners are unchanged. This workflow uses one leaf Worker at a time, so cancelling or destroying its service does not leave a supervisor's grandchildren to manage. It has a fixed history budget per species/thickness; it does not expose the legacy runner's adaptive batch convergence.

## Windows checks

| Check | Actual result |
| --- | --- |
| Full desktop / headless CTests | 39/39 each |
| Planning/rejection contract assertions | 12 |
| Real CLI/scan/offline reconstruction acceptance | 94 |
| Asynchronous service checks within that acceptance | 19 |
| Final native scan UI with small-window scrolling | 19 |
| Bundled dependencies / Worker native scan UI | 19 |
| Existing manual environment UI regression | 31 |
| Existing component authoring UI regression | 27 |
| Actual TKMaster SDK integration and preserved scene | 7 |
| Packaged standalone real transport smoke | 9 |
| Installed headless SDK, external scan consumer, VTK discovery disabled | passed; `curves=3; state=ready` |

Service checks include busy rejection, original-project preservation, progress, exactly one successful completion, offline reconstruction, copied-archive corruption, cancellation in initial notification and during active transport, repeated cancellation, changed copied runtime assets preventing the next launch, and destruction from start/completion callbacks. Native checks include both exports, history without runtime configuration, host project/result/signal preservation, active-window closure and small-window access to exports.

The final native test uses 2,000 histories per species/thickness. The CLI integration test below uses 10,000 and repeats the whole scan to verify seeded numerical repeatability. These are different sampling budgets; screenshots and CLI tables are not asserted to have identical estimates.

## Actual synthetic scan

Geant4 11.4.3, FTFP_BERT_EMZ, 0.1 mm production cut. A 20 mm diameter Si sphere inside an Al box shell, fixed 29 mm inner cavity, source sphere radius 40 mm. Synthetic omnidirectional isotropic electron/proton spectra with 64 s exposure. Three thicknesses × two incident species × 10,000 histories = 60,000 histories per scan.

| Wall (mm) | Total TID (Gy) | Total MC SE (Gy) | RSE |
| --- | --- | --- | --- |
| 0 | 1.2503754842436343e-6 | 5.16529255803394e-8 | 0.041309931481569966 |
| 0.5 | 1.0368940462976992e-6 | 4.3957420642627336e-8 | 0.0423933581252398 |
| 2 | 3.09799388411358e-7 | 2.096606683509596e-8 | 0.06767626928706774 |

Integration checks reconstruct each species' source normalization, sum dose and variance, verify fixed cavity and zero-shell removal, repeat numeric curves, relocate the archive, reject CSV/child-project corruption, preserve existing output and original input files, and stop a timed-out 100-million-history scan without accepting it as completed. This is a synthetic software integration case; the displayed trend is not a general monotonicity guarantee or an independent physical benchmark.

## Hosted CI

The [actual Linux Worker run](https://github.com/Wangszjl/Spacecraft-Radiation-Workbench/actions/runs/41266563822), job `124730476795`, passed on `b468dfb9c1cd3e7a87079b48fc085662099efc19`: 39 CTests, 57 prior transport/service checks, 53 prior CLI checks, 38 prior saved-result checks and 99 new scan integration checks. The additional Linux scan checks exercise active SIGINT cancellation and absence of surviving Worker PIDs. The [Windows/Linux model and relocated-SDK run](https://github.com/Wangszjl/Spacecraft-Radiation-Workbench/actions/runs/41266563901) also passed.

The hosted job tests headless code, which is unchanged by the subsequent Windows-tested dialog scrolling adjustment; it does not build/render the Linux desktop. Decoded job logs and source/run metadata are retained in ignored `reports/scan/github-linux.log` and `github-verification.json`.

## Local delivery and evidence

Local package: `dist/SpaceRad-0.1.5-local/`, including scan project/settings and `SHIELDING_SCAN.md`. It uses the existing external Geant4 data installation. Native bundled tests remove developer library paths and use packaged Qt plugins and Worker. A separate installed headless package is consumed with VTK discovery disabled through `examples/scan-consumer`.

Evidence is under ignored `reports/scan/`: `windows-verified/`, `bundle-final/`, existing-workflow regressions, headless CTest logs, external-consumer logs and package smoke. These are workstation artifacts, not public binary downloads. No clean-machine install, binary release, project-license clearance, independent dose physics validation, orbit-model execution or calibrated DDD/SEE is claimed. Archive consistency does not authenticate its producer or checksum every installed Geant4 data/library file.

`reports/scan/acceptance.json` records 412 source/runtime/evidence file identities; an independent rehash found zero mismatches. The scope is software/transport integration and preserved inputs, not scientific certification.
