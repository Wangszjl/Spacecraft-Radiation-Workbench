# 0.1.4 saved-result review and comparison acceptance

Recorded 2026-11-01. The functional source revision is `56465ba36b2aa97e6c1e213d7f656a02199dec9b`. A subsequent UI adjustment keeps comparison warnings visible after exports; the final Windows native tests cover that adjustment.

## Delivered

- Shared desktop/embedded-widget history browser, independent A/B result viewer, CSV/JSON exports.
- Offline Qt Core `ArchivedRun`, `loadArchivedRun`, `compareRuns`, `comparisonCsv` API, installed through `SpaceRad::Services`.
- CLI `inspect-result` and `compare`, with bounded archive checks and NDJSON output.
- ID union, changed-target/source/runtime flags, undefined zero-baseline ratio and explicit unknown covariance handling. See [behavior and limitations](RESULTS.md).

## Local Windows evidence

| Check | Recorded result |
| --- | --- |
| Full desktop CTests | 38/38 |
| Headless Qt Core + Geant4 CTests, VTK discovery disabled | 38/38 |
| Offline API synthetic-fixture assertions | 29 |
| Real shielding transport + archive/CLI integration | 38 |
| Native result-viewer workflow, including exports retaining warnings | 17 |
| Viewer with bundled DLLs/plugins, developer paths removed, invalid G4LEDATA | 17 |
| Existing environment editor regression | 31 |
| Existing component authoring regression | 27 |
| Actual TKMaster SDK integration / unchanged scene | 7 |
| Packaged standalone real-Worker smoke, developer paths removed | 9 |
| External installed headless SDK consumer, legacy headers first, VTK disabled | passed; `offline-comparison=1` |

The 29 offline API checks use deliberately generated consistent files, not physics results. They cover relocation, settings/target/capability differences, malformed archives and numerical/CSV boundaries. The separate integration checker performs actual transport.

Windows actual transport used Geant4 11.4.3, FTFP_BERT_EMZ, 0.1 mm production cut, synthetic isotropic electron/proton spectra, a 20 mm diameter Si sphere, 30 mm Al box shell and 30 mm source radius; 50,000 histories for each case. The shell wall is the changed parameter:

| Al wall | Si TID (Gy) | MC SE (Gy) | Nonzero histories |
| --- | --- | --- | --- |
| 0.5 mm | 1.025748568736884e-6 | 1.5879385362356258e-8 | 5467 |
| 2.0 mm | 2.80515506476216e-7 | 7.222987260747531e-9 | 2062 |

The tool reports B/A = 0.27347394383561785 and B−A = −7.45233062260668e-7 Gy. This is a synthetic software integration case, not a qualified mission prediction or independent physical benchmark. It does not calculate a standard error or statistical significance for the difference.

Local evidence is under `reports/results/`; the local package is `dist/SpaceRad-0.1.4-local/`. Both are excluded from Git. The package uses the existing external data installation. It is not a GitHub binary release or clean-machine installation acceptance. Screenshots include `reports/results/bundle-viewer-final/comparison.png`.

## Hosted CI

The [Linux headless Worker run](https://github.com/Wangszjl/Spacecraft-Radiation-Workbench/actions/runs/41263920356), job `124722625725`, passed on source revision `56465ba36b2aa97e6c1e213d7f656a02199dec9b`: 38 CTests, existing real transport/service and CLI acceptance, and 38 new real shielding/archive comparison checks. The [Windows/Linux model and relocated SDK run](https://github.com/Wangszjl/Spacecraft-Radiation-Workbench/actions/runs/41263920441) also passed. This CI does not build or render the desktop. The later change preserving UI warnings after export affects the Windows-tested dialog only; the hosted headless implementation is unchanged.

Local `reports/results/acceptance.json` records 204 source/runtime/evidence file identities, independently rehashed without mismatches. The hosted job log and job/source metadata are retained locally as `github-linux.log` and `github-verification.json`. These ignored files are local evidence, not repository downloads.

## Scope

Saved-file consistency is not producer authentication. No dataset content digests, independent dose validation, orbit-model execution, calibrated DDD/SEE, Linux desktop rendering or project-license clearance are claimed. Historical results are read into an independent memory snapshot and do not become the host's current accepted run. The tests confirm project/result/scene preservation within their stated workflows.
