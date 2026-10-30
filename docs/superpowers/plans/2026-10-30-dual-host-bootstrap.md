# Dual-host desktop foundation implementation plan

> **For agentic workers:** Use superpowers:executing-plans for the coupled runtime/UI path; simple independent checks may be delegated to Luna. Track each task below.

**Goal:** Build and exercise a standalone and embeddable radiation workbench from an independent source tree.
**Architecture:** Reuse qualified radiation model/Worker sources, expose AnalysisService and WorkbenchWidget, and host the same widget in independent and embedded shells. Geant4 remains in the Worker process.
**Tech Stack:** C++17, CMake 3.22+, Qt6 Core/Widgets/OpenGLWidgets, VTK, Geant4, vendored nlohmann JSON.
**Spec:** ../specs/2026-10-30-dual-host-design.md

## Global constraints
- Separate sibling repository; never copy unrelated PIC/charging sources or modify old physics.
- Host supplies runtime; no library-created QApplication, event loop or global PATH/dataset mutation.
- New job directory only; completed results are bound to input and executable identities.
- This batch is local; no GitHub push or invented redistribution/license claims.

## Tasks
- [x] 1. Independent source and build: run `python tests/test_project_layout.py` first and observe missing CMake/API contracts; copy existing model/runtime/transport with SHA manifest; add find_package CMake and build Worker plus original portable tests. Verify no parent paths in tracked source/configuration.
- [x] 2. Runtime service: add include/spacerad/AnalysisService.h and src/AnalysisService.cpp. Public API: RuntimeOptions{worker,dataRoot,libraryDirectories}; configure(RuntimeOptions), start(Project,QString), cancel(), running(), result(); signals stateChanged(QString), progress(int,int), completed(QByteArray), failed(QString). Add Qt event-loop tests for missing runtime, existing output, real progress/completion/hash validation, busy rejection, cancellation, and unchanged host environment; observe failure before implementation.
- [x] 3. Dual-host UI: add include/spacerad/WorkbenchWidget.h, src/WorkbenchWidget.cpp and src/GeometryView.cpp; export setProject/project, load/save project, setRuntime, runTo/cancel and resultReady. app/main.cpp and examples/embedded/main.cpp own QApplication and contain the same widget. Add native smoke with two hosts, snapshots, actual identical seeded doses, stale-result invalidation and cancel; verify visible screenshot.
- [x] 4. Integration and delivery: add optional host adapter/CMake package, install/export targets and portable runtime staging. Exercise a relocated bundle with runtime configuration, and the actual TKMaster entry if compatible. Run all tests, code review, record limitations/ownership/third-party inventory and update this checklist. Commit only this new local repository after checks; no remote publication.

## Execution rulings
- User's approved independent-project boundary replaces a worktree of the unrelated parent project.
- The tightly coupled new API/UI path is executed inline; Luna assists isolated tests/docs/review. No further permission question is necessary for the approved implementation.

- Ruling: isolate the imported C++ engine namespace as spacerad::engine. Linking two static copies of scdat::radiation into TKMaster would violate the host boundary; schemas and algorithms remain unchanged.

Completed locally 2026-10-30. Final evidence is documented in ../../BOOTSTRAP_ACCEPTANCE.md; public-release gates remain open.
