# Model-only build and CI

The public model library can be built without Qt, VTK or Geant4. It includes project serialization, analytic geometry/mesh checks, immutable component editing, spectrum editing and normalization. It does not perform particle transport or create a desktop window.

## Build locally

CMake 3.22+, a C++17 compiler and Python 3 for the tests are sufficient. nlohmann JSON headers are already vendored. With Ninja available and the compiler environment initialized:

```powershell
cmake --preset model
cmake --build --preset model
ctest --preset model
```

On Windows, use a Visual Studio developer shell for Ninja/MSVC, or configure a Visual Studio generator directly:

```powershell
cmake -S . -B build/model-vs -G "Visual Studio 17 2022" -DSPACERAD_BUILD_SERVICES=OFF -DSPACERAD_BUILD_DESKTOP=OFF -DSPACERAD_BUILD_WORKER=OFF
cmake --build build/model-vs --config Release
ctest --test-dir build/model-vs -C Release --output-on-failure
```

`BUILD_TESTING=OFF` removes the Python/test requirement. Desktop and Worker modes require `SPACERAD_BUILD_SERVICES=ON`; the defaults still build the existing Qt/VTK desktop and separate Geant4 Worker.

## Install and consume

```powershell
cmake --install build/model --prefix "install/model sdk"
cmake -S examples/model-consumer -B build/model-consumer -DCMAKE_PREFIX_PATH="<absolute path>/install/model sdk"
cmake --build build/model-consumer --config Release
ctest --test-dir build/model-consumer -C Release --output-on-failure
```

Use a compiler/toolchain compatible with the installed library. A consumer needs only:

```cmake
find_package(SpaceRad CONFIG REQUIRED COMPONENTS Model)
target_link_libraries(my_program PRIVATE SpaceRad::Model)
```

The target propagates C++17 and its public include paths. `spacerad/Project.h`, `spacerad/Authoring.h` and `spacerad/Environment.h` are available. A model-only SDK does not install the Qt service/widget headers and rejects `COMPONENTS Services` or `Widgets`. A full SDK still discovers dependencies for the libraries it exports; use the model-only installation when the consumer must avoid Qt.

`tools/check_model_install.py --build build/model --work "reports/model-install/new run"` installs into a new path, moves that installation, configures an independent consumer against the relocated package with all three external packages disabled, executes it against both synthetic examples, and checks rejection of unavailable components/invalid build modes. It refuses an existing output directory. For multi-configuration builds pass `--config Release`.

## Continuous integration scope

[Model build and SDK](https://github.com/Wangszjl/Spacecraft-Radiation-Workbench/actions/workflows/model-ci.yml) runs on pushes to `main`/`codex/**`, pull requests and manual dispatch. Its matrix uses Windows/MSVC and Linux/GCC. Each job disables external-package discovery, builds the model, runs 13 CTests, then performs the relocated SDK acceptance. Test/install logs are retained as short-lived Actions artifacts; the workflow does not publish a desktop runtime, external SDK or dataset.

The action revisions are pinned. Workflow syntax and action usage follow the upstream [GitHub workflow reference](https://docs.github.com/en/actions/reference/workflows-and-actions/workflow-syntax), [checkout](https://github.com/actions/checkout) and [upload-artifact](https://github.com/actions/upload-artifact) documentation.

A model CI pass establishes the recorded build/test/install scope only. Linux desktop rendering, Geant4 transport, dataset provisioning, clean-machine installation, licensing and independent physical validation remain separate work.
