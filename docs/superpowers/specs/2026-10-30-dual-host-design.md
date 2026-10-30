# Spacecraft Radiation Workbench dual-host design

Approved architecture: user confirmed on 2026-10-30 to build a standalone desktop application that can also be embedded in TKMaster.

Deliverable for this batch: a separate local Git project containing a reusable Qt-free model, Qt Core asynchronous AnalysisService, Qt/VTK WorkbenchWidget, a standalone shell and an embedding example, plus an optional source-level TKMaster adapter. Both hosts use the same widget/service and an independently built Geant4 Worker. No global QApplication creation or environment mutation inside libraries. No Geant4 linkage in either GUI host. Private copied engine C++ namespace is spacerad::engine to avoid duplicate symbols when embedded beside the existing scdat::radiation module; file schemas remain scdat.radiation/* so no physics/file-format migration is introduced. Public API namespace is spacerad.

Supported first UI path: qualified analytic spacecraft geometry (boxes, box shells, cylinders, spheres), synthetic demo and external differential CSV spectra, component materials/sensitivity, history/exposure controls, per-target TID/MC errors, saved projects/results. Advanced mesh code is retained in the Worker but is not advertised as a full CAD UI in this batch. DDD/SEE remain explicitly unavailable.

Runtime injection: absolute Worker path, Geant4 dataset root and optional library directories. Configure only the child QProcess environment. Results must match launch input/Worker hash and completed history/target identities; a changed project invalidates UI acceptance. Existing output folders cannot be overwritten. Cancelled/failed jobs cannot produce an accepted result.

First platform Windows/MSVC/Qt6/VTK/Geant4 11.4.3. CMake uses find_package; no parent-project source or fixed SDK location is required. Local cache paths are gitignored. Include a release-readiness checklist: licensing/ownership and clean-machine redistribution remain gates for public release, not obstacles to this local development batch. No GitHub publication or current-project commits in this batch.
