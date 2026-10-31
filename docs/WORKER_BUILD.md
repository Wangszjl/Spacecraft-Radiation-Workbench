# Headless Geant4 Worker build

The Worker performs transport in a separate process. It requires Qt Core and Geant4, but no VTK or application desktop widget. Building also needs a C/C++ compiler: some Geant4 packages discover C dependencies through their exported CMake configuration.

## Local build and checks

```powershell
cmake -S . -B build/worker -DCMAKE_PREFIX_PATH='<Qt>;<Geant4>' -DSPACERAD_BUILD_DESKTOP=OFF -DSPACERAD_BUILD_WORKER=ON
cmake --build build/worker --config Release
ctest --test-dir build/worker -C Release --output-on-failure
```

The normal tests cover model, runtime IO and task-service contracts; they need no physics data. Real transport requires the matching dataset manifest, `radiation-build.json`, libraries and data directories. Configure the existing `spacerad.runtime/1` JSON with those paths and run:

```powershell
python tools/check_worker_runtime.py --runtime runtime.local.json --service-test build/test_service_runtime.exe --output-dir "reports/worker/new run"
```

Use the actual executable location for your generator (e.g. `Release/` for Visual Studio). The runner accepts only a new output directory. Its four jobs use a 20 mm silicon sphere inside an 11 mm source sphere with simple electron/proton spectra: repeated seed, flux, equivalent fluence and doubled exposure. It verifies input/Worker/build/dataset identities, mass, raw moments, dose/error reconstruction, seeded equivalence, exposure scaling, source geometry statistics and negative cases. It then executes the existing real task-service test, including busy rejection, cancellation, changed input rejection and unchanged host environment. Logs and numerical results are retained in the output directory.

The sphere primary-entry expectation is `(10/11)^2`, from the projected area of concentric spheres under inward cosine sampling. Its bounded six-sigma check and the Lambert mean `2/3` are geometric/source checks. They do not validate a proton/electron dose model against experimental data. Seeded equality is required within the same runtime; no cross-platform bitwise dose equality is assumed.

## Linux CI dependencies and data

The [Worker transport workflow](https://github.com/Wangszjl/Spacecraft-Radiation-Workbench/actions/workflows/worker-ci.yml) provisions the specifications in `ci/worker-linux.yml` into its own micromamba environment. Geant4 11.4.3 and Qt 6.8.3 are selected, along with development packages required by Geant4's CMake exports. The job records the full resolved package list, builds headlessly, runs 35 CTests, prepares explicit data mappings and executes the real transport acceptance.

The successful run's complete package identities, versions, build strings, URLs and archive hashes are preserved in `ci/worker-linux-baseline.json`. This is a reference inventory, not an automatically applied lockfile: the YAML pins principal dependencies and the solver may resolve newer compatible transitive packages on later runs. Every run records its actual environment.

For the dependency packaging details, see the maintained [Geant4 feedstock](https://github.com/conda-forge/geant4-feedstock), [EMLOW data feedstock](https://github.com/conda-forge/geant4-data-emlow-feedstock) and [setup-micromamba action](https://github.com/mamba-org/setup-micromamba). The Geant4 upstream release is [11.4.3](https://geant4.web.cern.ch/download/11.4.3.html).

Conda data directory names may omit the `G4` prefix used by Geant4's generated manifest. `tools/prepare_worker_runtime.py` checks each activated source directory is inside the dependency prefix, nonempty and has exactly the expected versioned name (with only that documented prefix difference). It creates a new directory of symlinks under `build/worker-runtime/data`, writes runtime JSON and records every source/alias pair. Audit logic in the Worker remains enabled. This is a directory/version audit, not an independent checksum validation of every data file.

Dependency downloads remain inside the CI environment. The workflow stores logs, inputs/results, runtime mappings and package metadata as short-lived artifacts; it does not upload dependency binaries, datasets or a desktop installer. Changes to Worker/runtime inputs trigger it; documentation-only updates do not start a new transport run. Manual dispatch is also available.

Linux desktop rendering, Windows hosted Worker CI, clean-machine desktop installation, licensing/distribution checks and independent physical validation remain separate acceptance work.

Actual Linux/Windows headless results and their limits are recorded in [Worker CI acceptance](WORKER_CI_ACCEPTANCE.md).
