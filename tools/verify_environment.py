"""Capture local 0.1.2 spectrum-authoring and dual-host acceptance evidence."""
import argparse
import hashlib
import json
from pathlib import Path


def sha(path):
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def read(path):
    return json.loads(path.read_text(encoding="utf-8-sig"))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--report", type=Path, default=Path("reports/environment"))
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[1]
    report = args.report.resolve()
    output = report / "acceptance.json"
    if output.exists():
        raise RuntimeError("Refusing to overwrite an existing acceptance capture")
    checks = []

    def check(name, ok):
        checks.append({"name": name, "passed": bool(ok)})
        if not ok:
            raise AssertionError(name)

    check("35 CTests passed", "100% tests passed, 0 tests failed out of 35" in
          (report / "ctest.log").read_text(errors="replace"))
    check("41 environment authoring checks passed", "41 environment authoring checks passed" in
          (report / "model-contract.log").read_text(errors="replace"))
    context = root / "build/radiation-build.json"
    manifest = root / "build/radiation-datasets.txt"
    worker = root / "build/scdat_radiation_worker.exe"
    worker_hash = sha(worker)
    check("Worker identifies version 0.1.2", read(context)["application_version"] == "0.1.2")
    for name, count in (("native-scroll", 31), ("native-bundle", 31), ("authoring-regression", 27),
                        ("standalone-release", 9), ("embedded-release", 11),
                        ("tkmaster-release", 8), ("bundle-release", 9)):
        summary = read(report / name / "summary.json")
        check(name + " native checks all passed", summary["passed"] and len(summary["checks"]) == count
              and all(c["passed"] for c in summary["checks"]))
    jobs = [(case + "/" + kind + "-run", 20000, 3)
            for case in ("native-scroll", "native-bundle") for kind in ("flux", "fluence")]
    jobs += [(case + "/run", 10000, 4)
             for case in ("standalone-release", "embedded-release", "bundle-release")]
    jobs += [("tkmaster-release/run", 20000, 3), ("authoring-regression/transport-run", 10000, 5),
             ("example-flux", 100000, 3), ("example-fluence", 100000, 3)]
    results = {}
    for name, histories, targets in jobs:
        job = report / name
        result = read(job / "result.json")
        check(name + " real input and Worker identities match", result["schema"] == "scdat.radiation-result/2"
              and result["status"] == "completed" and result["histories"] == histories
              and result["engine_version"] == "11.4.3" and len(result["targets"]) == targets
              and result["project_sha256"] == sha(job / "project.json")
              and result["worker"]["sha256"] == worker_hash)
        audit = result["runtime_audit"]
        check(name + " build and manifest identities match", audit["status"] == "ready"
              and audit["build_context_sha256"] == sha(context) and audit["manifest_sha256"] == sha(manifest))
        check(name + " score moments and finite nonnegative targets retained", all(
            result["score_moments"][t["id"]]["ionizing"]["count"] == histories
            and t["mass_kg"] > 0 and t["tid_Gy"] >= 0 and t["tid_standard_error_Gy"] >= 0
            for t in result["targets"]))
        log = (job / "worker.log").read_text(errors="replace")
        check(name + " no Worker geometry warnings", not any(w in log for w in
              ("G4Exception", "WARNING", "GeomNav1002", "Stuck Track")))
        results[name] = result
    comparisons = [("native-scroll/flux-run", "native-scroll/fluence-run"),
                   ("native-scroll/flux-run", "native-bundle/flux-run"),
                   ("native-scroll/flux-run", "native-bundle/fluence-run"),
                   ("native-scroll/flux-run", "tkmaster-release/run"),
                   ("example-flux", "example-fluence"),
                   ("standalone-release/run", "embedded-release/run"),
                   ("standalone-release/run", "bundle-release/run")]
    for first, second in comparisons:
        check(second + " identical seeded dose and normalization to " + first, all(
            results[first][key] == results[second][key]
            for key in ("targets", "score_moments", "expected_primaries")))
    for case in ("native-scroll", "native-bundle"):
        flux = read(report / case / "flux-run/project.json")
        fluence = read(report / case / "fluence-run/project.json")
        check(case + " conversion multiplies all values and retains manual origin", all(
            a["particle"] == b["particle"] and a["energy_MeV"] == b["energy_MeV"]
            and [v * 64 for v in a["values"]] == b["values"]
            and b["provenance"]["conversions"][-1]["duration_s"] == 64
            and a["provenance"]["manual_definition"]["schema"] == "spacerad.manual-spectrum/1"
            for a, b in zip(flux["environment"]["spectra"], fluence["environment"]["spectra"])))
    check("actual TKMaster scene unchanged byte for byte",
          (report / "tkmaster-release/scene-before.json").read_bytes() ==
          (report / "tkmaster-release/scene-after.json").read_bytes())
    check("installed SDK environment API executed", "manual-integral=4; service=ready" in
          (report / "consumer-run.log").read_text())
    bundle = root / "dist/SpaceRad-0.1.2-local"
    check("staged binaries and runtime identities match build", all(
        sha(bundle / name) == sha(root / "build" / name) for name in
        ("spacerad.exe", "spacerad_embedded_example.exe", "scdat_radiation_worker.exe",
         "radiation-build.json", "radiation-datasets.txt")))
    check("runtime relative Worker and Qt plugins present", read(bundle / "runtime.json")["worker"] ==
          "scdat_radiation_worker.exe" and (bundle / "platforms/qwindows.dll").is_file()
          and (bundle / "styles/qmodernwindowsstyle.dll").is_file())
    for quantity in ("flux", "fluence"):
        example = root / f"examples/projects/electron-proton-{quantity}.json"
        packaged = bundle / f"examples/projects/electron-proton-{quantity}.json"
        project = read(example)
        check(quantity + " portable example and tested bytes match", sha(example) == sha(packaged)
              == sha(report / ("example-" + quantity) / "project.json")
              and project["events"] == 100000 and project["environment"]["synthetic"]
              and project["environment"]["quantity"] == "differential_" + quantity)
    excluded = {"acceptance.json", "independent-rehash.json"}
    paths = {p for p in report.rglob("*") if p.is_file() and p.name not in excluded}
    for folder in ("src", "include", "engine", "app", "cmake", "tests", "tools", "integration",
                   "examples", "docs", "third_party", "install"):
        paths.update(p for p in (root / folder).rglob("*") if p.is_file() and "__pycache__" not in p.parts)
    paths.update(p for p in root.iterdir() if p.is_file() and
                 p.name not in ("runtime.local.json", "CMakeUserPresets.json", "topbar_debug.log"))
    paths.update(p for p in bundle.rglob("*") if p.is_file())
    paths.update(root / "build" / name for name in
                 ("scdat_radiation_worker.exe", "spacerad.exe", "spacerad_embedded_example.exe",
                  "test_widget_environment.exe", "test_environment_authoring.exe"))
    records = [{"path": p.relative_to(root).as_posix(), "size_bytes": p.stat().st_size,
                "sha256": sha(p)} for p in sorted(paths)]
    output.write_text(json.dumps({"schema": "spacerad.environment-acceptance/1", "passed": True,
        "checks": checks, "files": records,
        "scope": "Local Windows 0.1.2 manual differential spectra and normalization equivalence, "
                 "dual-host/UI checks and synthetic examples. External datasets remain configured "
                 "separately. No orbit-model execution, dose certification or public-release clearance."
    }, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    print(f"Environment acceptance: {len(checks)} checks, {len(records)} captured files")


if __name__ == "__main__":
    main()
