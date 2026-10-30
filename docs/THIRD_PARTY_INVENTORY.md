# Third-party inventory — review pending

This records technical dependencies, not a license compatibility conclusion.

| Dependency | Use | Current source/distribution treatment |
|---|---|---|
| Geant4 | Separate transport Worker; local verification used 11.4.3 | Located through CMake; source/SDK/data are not vendored into this repository. Local preview stages required runtime DLLs; public redistribution review remains open. |
| Qt6 | Core services and desktop widgets | Located through CMake; local runtime staging includes required DLLs and plugins. Public distribution obligations remain to be checked for the actual modules/build. |
| VTK | Independent 3D viewport | Located through CMake; local runtime DLL staging only. Preserve applicable notices when releasing. |
| nlohmann JSON | Project/result serialization | Vendored headers and the upstream license file are retained in `third_party/nlohmann_json`. |
| Existing radiation module | Reused model, geometry, transport and postprocessing | Original/derived file identities and namespace-only transformation are recorded in `source-import.json`. Ownership and permission to publish the extracted code are not asserted here. |

The project license has not been selected. Geant4 physics data, external spectra and CAD/example models require their own provenance and redistribution review. Do not interpret a working local runtime bundle as public-release clearance.
