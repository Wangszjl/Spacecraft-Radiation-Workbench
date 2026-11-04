# Spacecraft Radiation Workbench

[![Model build and SDK](https://github.com/Wangszjl/Spacecraft-Radiation-Workbench/actions/workflows/model-ci.yml/badge.svg?branch=main)](https://github.com/Wangszjl/Spacecraft-Radiation-Workbench/actions/workflows/model-ci.yml)
[![Worker transport](https://github.com/Wangszjl/Spacecraft-Radiation-Workbench/actions/workflows/worker-ci.yml/badge.svg?branch=main)](https://github.com/Wangszjl/Spacecraft-Radiation-Workbench/actions/workflows/worker-ci.yml)

Spacecraft Radiation Workbench is an independent C++/Qt desktop application and reusable analysis UI for spacecraft radiation studies. Its standalone shell and an embedding Qt host use the same `spacerad::WorkbenchWidget` and `spacerad::AnalysisService`. Geant4 runs in a separate `scdat_radiation_worker` process; the GUI host does not link Geant4.

Source repository: [Wangszjl/Spacecraft-Radiation-Workbench](https://github.com/Wangszjl/Spacecraft-Radiation-Workbench), published on 2026-10-31.

```powershell
git clone https://github.com/Wangszjl/Spacecraft-Radiation-Workbench.git
cd Spacecraft-Radiation-Workbench
```

This repository contains source, tests, synthetic examples and documentation. Local runtime bundles, build/install directories, acceptance reports/screenshots, SDKs and Geant4 datasets are not uploaded. Links into `dist/` or `reports/` in the local acceptance documents refer to workstation artifacts; they are not GitHub release downloads. No binary release has been published.

Version 0.1.6 adds whole-target-bound, uniform-box spatial TID scoring with project-persisted grids, a VTK 3D overlay, axis slices and VTI/CSV exports. See [voxel scoring](docs/VOXEL_SCORING.md) and [acceptance](docs/VOXEL_ACCEPTANCE.md). Version 0.1.5 adds automated fixed-cavity thickness scans; see [scan workflow](docs/SHIELDING_SCAN.md). This is a research application; independent dose validation is not complete.

The [NIST PSTAR silicon benchmark](docs/PSTAR_BENCHMARK.md) compares accepted Geant4 proton TID with a thin-target estimate and checks source normalization. It covers a simple geometry and synthetic exposure.

For this workstation, launch `dist/SpaceRad-0.1.3-local/spacerad.exe`. Its `runtime.json` already points at the locally available matching Geant4 data. The data files are external to the bundle; moving to a different machine requires configuring or installing them. The same directory contains `spacerad_cli.exe` and a [CLI guide](docs/CLI.md).

## First workflow scope

The workflow supports new basic projects, add/edit/remove dialogs for boxes, box shells, cylinders and spheres, component materials/sensitivity and placement, synthetic demo or user-supplied differential CSV spectra, history/exposure controls, per-target TID and Monte Carlo errors, and saved projects/results. Select a model tree item or result row to highlight and focus the component. Edits pass the existing geometry/source checks; deleting the last sensitive region is rejected. Template-generated descendants are selectable but are not directly editable by these dialogs. The model and worker retain mesh-related code, but this batch does not promise a full CAD UI. DDD and SEE are unavailable. Independent physical validation has not been completed.

The Environment tab also supports manual energy/value tables, linear or bin-constant differential spectra, particle sampling shares and a linear-axis preview. Manual edits explicitly mark a synthetic environment and clear stale orbit-model metadata. Quantity conversion multiplies/divides values by the current exposure and records the operation. The bundled [electron/proton examples](examples/projects/README.md) can be opened directly; they do not execute an orbit model. The shared widget scrolls in small desktop/dock windows.

The CLI uses that same analysis service without a desktop window. It emits ordered `spacerad.cli-event/1` NDJSON events on stdout and supports bounded runtime, SIGINT cancellation and stable exit codes. For example, from the local bundle directory:

```powershell
.\spacerad_cli.exe run --project .\examples\projects\electron-proton-flux.json --output "$env:TEMP\SpaceRad-new-job" --histories 100000 --timeout-seconds 600
```

Choose a new output directory. See [CLI integration](docs/CLI.md) for offline validation, runtime inspection and Python usage.

## Build

The first target platform is Windows/MSVC with C++17, CMake 3.22 or newer, Qt 6 (Core, Widgets, OpenGLWidgets), VTK, and Geant4. Configure CMake with local dependency prefixes:

```powershell
cmake -S . -B build -DCMAKE_PREFIX_PATH='<Qt>;<VTK>;<Geant4>'
cmake --build build --config Release
```

Replace the placeholders with the package prefixes on your machine. Full desktop CI, Windows hosted Worker CI and clean-machine desktop installation are still pending. Linux desktop rendering has not been accepted.

The model library can also be built and installed without Qt, VTK or Geant4:

```powershell
cmake --preset model
cmake --build --preset model
ctest --preset model
```

This preset requires Ninja and an initialized compiler environment. The [model build guide](docs/MODEL_BUILD.md) covers Windows Visual Studio generators, model-only SDK consumption and the Windows/Linux CI workflow. Model-only CI checks 13 CTests plus installation, relocation and external-consumer acceptance; it does not certify the desktop or transport on Linux.

Both Windows and Linux model jobs passed in the [first actual CI run](https://github.com/Wangszjl/Spacecraft-Radiation-Workbench/actions/runs/41189019059). See [model CI acceptance](docs/MODEL_CI_ACCEPTANCE.md) for its source revision, evidence and the full Windows desktop regression.

The separate [headless Worker workflow](https://github.com/Wangszjl/Spacecraft-Radiation-Workbench/actions/runs/41191922702) also passed on Linux with Geant4 11.4.3: 35 CTests and 57 real transport/service checks. A new local Windows headless build passed the same checks. This covers actual synthetic electron/proton transport, repeated seeds, flux/fluence equivalence, exposure scaling and analytic sphere/source checks. See [Worker build instructions](docs/WORKER_BUILD.md) and [acceptance limits](docs/WORKER_CI_ACCEPTANCE.md); it is not independent dose physics validation.

For the standalone shell, place a valid `runtime.json` beside the application or pass `--runtime <path>`; the shell also accepts `--project <path>`. See [Embedding](docs/EMBEDDING.md) for the schema and host integration example.

## CMake targets

The libraries are `spacerad_model`, `spacerad_services`, and `spacerad_widgets`, exported as `SpaceRad::Model`, `SpaceRad::Services`, and `SpaceRad::Widgets`. The executables are `spacerad`, `spacerad_cli`, `spacerad_embedded_example`, and `spacerad_worker`; the worker executable filename is `scdat_radiation_worker` (with the platform executable suffix). The CLI is built whenever services are enabled, including headless/service-only builds. The public service header provides runtime-file loading; widget users retain access through their existing include. Internal engine C++ types are isolated under `spacerad::engine`; the compatible project/result JSON schema retains the `scdat.radiation/*` identifiers.

An installed full CMake package exports `SpaceRad::Widgets`, `SpaceRad::Services`, and `SpaceRad::Model`. A model-only installation exports `SpaceRad::Model` and rejects unavailable components. Source builds or static libraries built with the same toolchain are the supported integration paths for now. Qt 6 and VTK ABI/toolchain versions must match a GUI host. The full desktop SDK/runtime has not yet been accepted on a clean machine.

## Release status

The public source repository, Windows/Linux model CI and Linux headless Worker CI are available. Code ownership and third-party license review, data redistribution rights, clean-machine desktop runtime acceptance, full desktop CI, Windows hosted Worker CI, Linux desktop support, independent scientific verification, and packaged binary releases remain open. The project license has not been selected; no license or redistribution permission is asserted. Engineering evidence establishes its recorded scope only; CI does not establish desktop or scientific acceptance. See [the readiness checklist](docs/RELEASE_READINESS.md).
