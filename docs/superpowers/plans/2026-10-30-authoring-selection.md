# Model authoring and result navigation

Goal: let both desktop and embedded users create/modify basic spacecraft components and locate computed targets without external JSON editing.

- [x] 1. Add Qt-free authoring operations with tests for valid box/cylinder/sphere suggestions, unique IDs, immutable input, overlap/source/last-target rejection.
- [x] 2. Add new-project and add/edit/remove component dialogs to the shared widget; preserve exact numeric values and existing geometry gates.
- [x] 3. Link model tree and result selection to 3D highlighting through public selectComponent/componentSelected; selection must never change physics or clear accepted results.
- [x] 4. Exercise real additional sensitive-region scoring, save/reopen, rejected edits, busy guards and current-host integration; rebuild/stage the updated SDK/runtime and record the validated scope.

Only existing analytic shapes/material identifiers are used. Arbitrary CAD, new radiation effects and new physical claims are out of scope. Component removal/editing applies to authored volumes, not implicit template descendants. Selection changes display only.

Completed 2026-10-30. Evidence: [authoring acceptance](../../AUTHORING_ACCEPTANCE.md), `reports/authoring/`. Version 0.1.1 has 34/34 CTests, 21 authoring checks, 27 UI checks in each of two real-Worker configurations, standalone/embedded/TKMaster and packaged runtime checks. The previous 0.1.0 evidence is a historical capture and is not overwritten.
