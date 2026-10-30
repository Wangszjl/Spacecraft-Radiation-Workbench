# Embedding the workbench

The reusable UI is `spacerad::WorkbenchWidget`; the asynchronous process service is `spacerad::AnalysisService`. A Qt host owns the single `QApplication` and its event loop. The widget delegates transport to the separate `scdat_radiation_worker` process. Neither the host nor the GUI libraries link Geant4, and the libraries do not create a `QApplication` or mutate the host's process environment.

## Build and link

Build with compatible Qt 6, VTK, and Geant4 package prefixes:

```powershell
cmake -S . -B build -DCMAKE_PREFIX_PATH='<Qt>;<VTK>;<Geant4>'
cmake --build build --config Release
```

For the current integration stage, use the source targets or static libraries built with the same compiler/toolchain as the host. The library targets are `spacerad_model`, `spacerad_services`, and `spacerad_widgets`, with aliases `SpaceRad::Model`, `SpaceRad::Services`, and `SpaceRad::Widgets`. An installed CMake package exports these names; the host's Qt 6 and VTK ABI/toolchain must match the libraries. Clean-machine package installation and consumption are not yet accepted.

```cmake
find_package(SpaceRad CONFIG REQUIRED)
target_link_libraries(my_host PRIVATE SpaceRad::Widgets)
```

The widgets target brings in its service and model dependencies. The worker is deployed separately; do not link it or Geant4 into the Qt host.

## Runtime configuration

The runtime file is JSON with exactly these top-level properties:

```json
{
  "schema": "spacerad.runtime/1",
  "worker": "bin/scdat_radiation_worker.exe",
  "data_root": "C:/Geant4/data",
  "library_directories": ["bin"]
}
```

`worker` names the worker executable, `data_root` names the Geant4 dataset root, and `library_directories` is an array of directories containing libraries needed by the worker. Paths may be absolute or relative. Relative paths resolve from the directory containing `runtime.json`. Use the platform's actual worker filename and valid installed dataset path. The array can be empty when no additional library directories are needed.

`spacerad::loadRuntimeFile(path)` parses this schema and returns `RuntimeOptions`. The standalone shell reads `runtime.json` beside the executable by default and supports `--runtime <path>` to select another file. An embedding host can load the same file and call `setRuntime` on the widget.

## Minimal Qt host example

This example uses the built-in demo project to show the integration path. A real host can instead pass its own `spacerad::Project` via `setProject`.

```cpp
#include <QApplication>
#include <QCoreApplication>
#include <QMainWindow>
#include <spacerad/Project.h>
#include <spacerad/WorkbenchWidget.h>

int main(int argc, char* argv[])
{
    QApplication app(argc, argv); // The host creates QApplication once.

    QMainWindow window;
    auto* workbench = new spacerad::WorkbenchWidget(&window);
    const auto runtimeFile = QCoreApplication::applicationDirPath()
        + QStringLiteral("/runtime.json");
    workbench->setRuntime(spacerad::loadRuntimeFile(runtimeFile));
    workbench->setProject(spacerad::Project::assemblyDemo());
    window.setCentralWidget(workbench);
    window.resize(1200, 800);
    window.show();

    return app.exec();
}
```

The widget owns its service, and its `setRuntime` and `setProject` methods configure that shared UI/service path. Keep the runtime JSON, worker, datasets, and worker dependencies available at their configured locations. Runtime paths are injected into the child worker environment only; the host's global `PATH` and dataset environment remain under host control.

## Project and result lifecycle

Use `setProject` to supply a validated project, `loadProject`/`saveProject` to handle project files, `runTo(newDirectory)` to start an analysis in a new output directory, and `cancel()` to request cancellation. Read accepted output with `acceptedResult()` or observe `resultReady`. A changed project invalidates its previously accepted result. Existing output directories cannot be reused as a run destination.

The first UI path supports analytic boxes, box shells, cylinders, and spheres; synthetic demo or user-provided differential CSV spectra; component materials/sensitivity; history and exposure controls; and per-target TID and Monte Carlo errors. DDD and SEE are unavailable, and a complete CAD authoring UI is not promised by this batch.

## Actual TKMaster integration

The optional adapter lives in `integration/tkmaster`. It accepts a serialized compatible radiation project, creates a dock containing the same `WorkbenchWidget`, and returns accepted bytes to the host's `spaceRadResultJson` property. It does not infer material/solid geometry from arbitrary viewport actors.

The existing TKMaster CMake project has an opt-in `SPACERAD_SOURCE_DIR` setting. Configure it together with `SpaceRad_DIR` pointing to this project's installed `lib/cmake/SpaceRad`. The simulation menu gains **SpaceRad 辐射分析组件...**. The adapter reads `SPACERAD_RUNTIME_FILE` when set, otherwise `spacerad-runtime.json` beside the host executable. Runtime paths can also be supplied in the page. Both old and new radiation modules can coexist; their C++ types are isolated and the bridge uses JSON.

## Validation status

Local Windows acceptance passed: 33/33 CTests; 16 real-Worker service checks; standalone, embedding example and actual TKMaster runs; installed SDK consumption with the old same-named header directory placed first; runtime relocation without developer library paths. The actual TKMaster test verified result return and byte-identical host scene state after its initial delayed setup. See [acceptance](BOOTSTRAP_ACCEPTANCE.md). Clean-machine deployment, CI, Linux, ownership/license review, data redistribution and independent physical validation remain open.
