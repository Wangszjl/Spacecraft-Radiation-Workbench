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
find_package(VTK REQUIRED COMPONENTS GUISupportQt)
target_link_libraries(my_host PRIVATE SpaceRad::Widgets VTK::GUISupportQt)
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

From 0.1.3, `loadRuntimeFile` is declared in `AnalysisService.h` and implemented by `SpaceRad::Services`, so a Qt Core host can read configuration without linking the widget library. Existing `WorkbenchWidget.h` consumers retain source access through its service include. Configuration loading also rejects duplicate JSON fields, excessive nesting and oversized files.

Hosts written in other languages can use `spacerad_cli` as a child process: it exposes project validation, actual runtime inspection, NDJSON progress and accepted-result events through the same task service. See [CLI integration](CLI.md) for command/exit contracts and a Python example.

## Minimal Qt host example

This example uses the built-in demo project to show the integration path. A real host can instead pass its own `spacerad::Project` via `setProject`.

```cpp
#include <QApplication>
#include <QCoreApplication>
#include <QMainWindow>
#include <QSurfaceFormat>
#include <QVTKOpenGLNativeWidget.h>
#include <spacerad/Project.h>
#include <spacerad/WorkbenchWidget.h>

int main(int argc, char* argv[])
{
    // The host establishes its OpenGL format before QApplication/VTK widgets.
    QSurfaceFormat::setDefaultFormat(QVTKOpenGLNativeWidget::defaultFormat());
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

The shared UI supports new basic projects and add/edit/remove dialogs for analytic boxes, box shells, cylinders and spheres, including parent assembly, local position/rotation, material identifier and sensitivity. Edits are validated before replacement. Implicit template descendants remain selectable but these dialogs only modify authored volumes. Synthetic demo or user-provided differential CSV spectra, history/exposure controls and per-target TID and Monte Carlo errors remain supported. DDD and SEE are unavailable, and a complete CAD authoring UI is not promised by this batch.

## Authoring and component navigation (0.1.1)

The Qt-free `spacerad/Authoring.h` API exposes `suggestPart`, `addPart`, `replacePart` and `removePart`. Each edit returns a validated copy and leaves its input unchanged. The suggestions are bounded placement attempts for a small analytic component; they do not resize the source or certify a material name. Link `SpaceRad::Model` to use these operations without the widget.

```cpp
#include <spacerad/Authoring.h>

auto input = workbench->project();
auto part = spacerad::suggestPart(input, "box", true);
part.id = "Detector_Si";
auto edited = spacerad::addPart(input, part); // throws on invalid geometry
workbench->setProject(edited);               // invalidates the previous result
workbench->selectComponent(QStringLiteral("Detector_Si"));
```

`selectComponent(stableId)`, `selectedComponent()` and `componentSelected(QString)` let the host synchronize its own component list. An empty ID clears the selection; an unknown ID throws before changing it. Selection synchronizes the model tree, authored-volume table and result table, highlights the actor and focuses the camera. A hidden component is revealed temporarily, while its stored visibility, physics input and accepted result remain unchanged. Selecting another component or clearing selection restores the previous display settings. Selection does not constitute a new analysis or device qualification.

The host may replace the project while an authoring dialog is open. A revision check prevents the old dialog from overwriting that newer host project. During transport, authoring and project replacement are blocked.

## Actual TKMaster integration

The optional adapter lives in `integration/tkmaster`. It accepts a serialized compatible radiation project, creates a dock containing the same `WorkbenchWidget`, and returns accepted bytes to the host's `spaceRadResultJson` property. It does not infer material/solid geometry from arbitrary viewport actors.

The existing TKMaster CMake project has an opt-in `SPACERAD_SOURCE_DIR` setting. Configure it together with `SpaceRad_DIR` pointing to this project's installed `lib/cmake/SpaceRad`. The simulation menu gains **SpaceRad 辐射分析组件...**. The adapter reads `SPACERAD_RUNTIME_FILE` when set, otherwise `spacerad-runtime.json` beside the host executable. Runtime paths can also be supplied in the page. Both old and new radiation modules can coexist; their C++ types are isolated and the bridge uses JSON.

## Editable environments (0.1.2)

`spacerad/Environment.h` provides Qt-free `setManualSpectrum(base, index, particle, energy, values, interpolation)` and `removeSpectrum(base, index)`. An index equal to `base.spectra.size()` appends; an existing index replaces. Duplicate particle declarations in other rows and removal of the final spectrum are rejected. The input remains unchanged and every returned project is validated. The shared widget exposes the same operations in its Environment tab.

```cpp
#include <spacerad/Environment.h>

auto input = workbench->project();
auto edited = spacerad::setManualSpectrum(input, input.spectra.size(),
    "e-", {1, 2, 3}, {4, 2, 0}); // append if e- is not already present
workbench->setProject(edited);
auto fluence = edited.convertedQuantity(spacerad::engine::SpectrumQuantity::Fluence);
```

Manual values use the project's current quantity and existing isotropic omnidirectional-scalar convention. Linear interpolation uses equal energy/value counts; `bin_constant` requires one more energy edge than value. The editor supports up to 2048 energy rows, preserves 17-digit numbers, and requires the last value cell to be empty for bin edges. It never sorts points automatically. Manually changing/removing a spectrum marks the environment synthetic and clears global orbit-model metadata. Unedited spectra retain their source provenance; the edited spectrum receives `spacerad.manual-spectrum/1` provenance instead of claiming an original CSV file still defines it.

`Project::convertedQuantity` converts every spectrum using the current duration and records each conversion. Flux normalization is `pi * R_cm^2 * integral * duration`; fluence normalization is `pi * R_cm^2 * integral`, with duration retained as context. A host should use this operation rather than relabeling units. The normalization label is refreshed on valid parameter changes and reports when the parameters are invalid. Manual dialogs have revision guards against stale host updates; environment controls are disabled during transport. The widget's scroll area supports smaller embedding windows.

## Validation status

`WorkbenchWidget::showOrbitEnvironment()` opens the shared external-ephemeris model workflow. `SpaceRad::Services` exports `OrbitEnvironmentService` and offline raw-spectrum reconstruction in `spacerad/OrbitEnvironment.h`; model generation uses a separately configured SpacePy process, while saved-job review requires neither Python nor Worker. See [orbit integration](ORBIT_DESKTOP.md). Apply operations are guarded against host edits and active transport.

The shared widget also exposes `showPartitionStudy(archiveDirectory={})` for an independent native full-spectrum study window. `SpaceRad::Services` exports `spacerad/PartitionStudy.h` for planning, asynchronous execution/cancellation and offline aggregation without Widgets or a Python runtime. See [native study integration and relocated consumer](NATIVE_PARTITION_STUDY.md). The standalone and embedding-example shells are revalidated for this addition; the actual TKMaster application has not been rebuilt in this batch. Recompile hosts against the updated SDK to obtain the new widget/API.

Version 0.1.4 also exposes `spacerad/Results.h` through `SpaceRad::Services`: load a completed saved job with `loadArchivedRun`, inspect its summary, compare two runs with `compareRuns`, and generate CSV with `comparisonCsv`. These functions require no Worker or Geant4 data. GUI hosts can call `WorkbenchWidget::showResults(A, B)` to open a separate nonmodal viewer without replacing the live project or emitting `resultReady`. See [saved-result semantics](RESULTS.md).

Local Windows 0.1.2 acceptance passed: 35/35 CTests; 41 environment checks; 31 environment UI checks repeated with the packaged Worker; 27 authoring regression checks; standalone, embedding example and actual TKMaster runs; installed SDK consumption with the old same-named header directory placed first; runtime relocation without developer library paths. TKMaster additionally accepted the two-particle manual environment without changing its points/provenance or the host scene. See [environment acceptance](ENVIRONMENT_ACCEPTANCE.md), [authoring acceptance](AUTHORING_ACCEPTANCE.md) and [foundation acceptance](BOOTSTRAP_ACCEPTANCE.md). Windows/Linux model CI and Linux headless transport are recorded in [model CI acceptance](MODEL_CI_ACCEPTANCE.md) and [Worker CI acceptance](WORKER_CI_ACCEPTANCE.md). Clean-machine desktop deployment, Linux desktop support, ownership/license review, data redistribution and independent physical validation remain open.
