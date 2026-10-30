# Spacecraft Radiation Workbench

Spacecraft Radiation Workbench is an independent C++/Qt desktop application and reusable analysis UI for spacecraft radiation studies. Its standalone shell and an embedding Qt host use the same `spacerad::WorkbenchWidget` and `spacerad::AnalysisService`. Geant4 runs in a separate `scdat_radiation_worker` process; the GUI host does not link Geant4.

The first local engineering milestone is verified on Windows: 33/33 CTests, 16 real-Worker service checks, standalone and embedded shells, actual TKMaster integration, and a runtime bundle launched with developer DLL paths removed. Each of the four host/runtime cases completed 10,000 histories over four sensitive regions with identical seeded target doses and raw moments. See [acceptance](docs/BOOTSTRAP_ACCEPTANCE.md). This is a local research application; public-release and independent physical validation are not complete.

For this workstation, launch `dist/SpaceRad-0.1.0-local/spacerad.exe`. Its `runtime.json` already points at the locally available matching Geant4 data. The data files are external to the bundle; moving to a different machine requires configuring or installing them.

## First workflow scope

The first workflow targets analytic spacecraft geometry (boxes, box shells, cylinders, and spheres), synthetic demo or user-supplied differential CSV spectra, component materials and sensitivity, history/exposure controls, per-target TID and Monte Carlo errors, and saved projects/results. The model and worker retain mesh-related code, but this batch does not promise a full CAD UI. DDD and SEE are unavailable. Independent physical validation has not been completed.

## Build

The first target platform is Windows/MSVC with C++17, CMake 3.22 or newer, Qt 6 (Core, Widgets, OpenGLWidgets), VTK, and Geant4. Configure CMake with local dependency prefixes:

```powershell
cmake -S . -B build -DCMAKE_PREFIX_PATH='<Qt>;<VTK>;<Geant4>'
cmake --build build --config Release
```

Replace the placeholders with the package prefixes on your machine. This is the intended local build command; CI and clean-machine installation/runtime acceptance are still pending. Linux has not been accepted.

For the standalone shell, place a valid `runtime.json` beside the application or pass `--runtime <path>`; the shell also accepts `--project <path>`. See [Embedding](docs/EMBEDDING.md) for the schema and host integration example.

## CMake targets

The libraries are `spacerad_model`, `spacerad_services`, and `spacerad_widgets`, exported as `SpaceRad::Model`, `SpaceRad::Services`, and `SpaceRad::Widgets`. The executables are `spacerad`, `spacerad_embedded_example`, and `spacerad_worker`; the worker executable filename is `scdat_radiation_worker` (with the platform executable suffix). The public headers are `spacerad/AnalysisService.h` and `spacerad/WorkbenchWidget.h`; the model's public project alias is `spacerad::Project`. Internal engine C++ types are isolated under `spacerad::engine`; the compatible project/result JSON schema retains the `scdat.radiation/*` identifiers.

An installed CMake package exports `SpaceRad::Widgets`, `SpaceRad::Services`, and `SpaceRad::Model`. Source builds or static libraries built with the same toolchain are the supported integration paths for now. Qt 6 and VTK ABI/toolchain versions must match the host. The export/install flow has not yet been accepted on a clean machine.

## Release status

Code ownership and third-party license review, data redistribution rights, clean-machine install/runtime acceptance, CI, Linux support, independent scientific verification, and public GitHub release remain open. No license or redistribution permission is asserted. The local engineering evidence establishes the recorded scope only; it does not establish scientific or clean-machine acceptance. See [the readiness checklist](docs/RELEASE_READINESS.md).
