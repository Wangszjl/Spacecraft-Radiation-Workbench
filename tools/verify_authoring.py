"""Capture read-only local 0.1.1 authoring and dual-host acceptance evidence."""
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
    parser.add_argument("--report", type=Path, default=Path("reports/authoring"))
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

    check("34 CTests passed", "100% tests passed, 0 tests failed out of 34" in
          (report / "ctest.log").read_text(errors="replace"))
    check("21 immutable authoring checks passed", "21 authoring checks passed" in
          (report / "model-contract.log").read_text(errors="replace"))
    worker = root / "build/scdat_radiation_worker.exe"
    worker_hash = sha(worker)
    context = root / "build/radiation-build.json"
    manifest = root / "build/radiation-datasets.txt"
    check("Worker identifies version 0.1.1", read(context)["application_version"] == "0.1.1")
    cases = (
        ("native-release", "transport-run", 27, 5),
        ("native-bundle", "transport-run", 27, 5),
        ("standalone-release", "run", 9, 4),
        ("embedded-release", "run", 11, 4),
        ("tkmaster-release", "run", 7, 4),
        ("bundle-release", "run", 9, 4),
    )
    results = {}
    for case, job_name, count, targets in cases:
        summary = read(report / case / "summary.json")
        check(case + " all native checks passed", summary["passed"] and
              len(summary["checks"]) == count and all(c["passed"] for c in summary["checks"]))
        job = report / case / job_name
        result = read(job / "result.json")
        check(case + " input and Worker identities match", result["schema"] ==
              "scdat.radiation-result/2" and result["status"] == "completed" and
              result["histories"] == 10000 and result["engine_version"] == "11.4.3" and
              result["project_sha256"] == sha(job / "project.json") and
              result["worker"]["sha256"] == worker_hash and len(result["targets"]) == targets)
        audit = result["runtime_audit"]
        check(case + " build and dataset manifest identities match", audit["status"] == "ready" and
              audit["build_context_sha256"] == sha(context) and
              audit["manifest_sha256"] == sha(manifest))
        check(case + " original score moments retained", all(
            result["score_moments"][t["id"]]["ionizing"]["count"] == 10000 and
            t["mass_kg"] > 0 and t["tid_Gy"] >= 0 and t["tid_standard_error_Gy"] >= 0
            for t in result["targets"]))
        log = (job / "worker.log").read_text(errors="replace")
        check(case + " no Worker geometry warnings", not any(word in log for word in
              ("G4Exception", "WARNING", "GeomNav1002", "Stuck Track")))
        if targets == 5:
            project = read(job / "project.json")
            target = next(t for t in result["targets"] if t["id"] == "New_Target")
            part = next(v for v in project["volumes"] if v["id"] == "New_Target")
            check(case + " hidden authored target is physically scored", part["visible"] is False and
                  part["sensitive"] is True and part["size_mm"] == [20, 20, 1] and
                  abs(target["mass_kg"] - 0.000932) < 1e-12 and
                  result["project"]["volumes"] == project["volumes"] and
                  read(report / case / "roundtrip.json")["volumes"] == project["volumes"])
        results[case] = result

    for case, reference in (("native-bundle", "native-release"),
                            ("embedded-release", "standalone-release"),
                            ("tkmaster-release", "standalone-release"),
                            ("bundle-release", "standalone-release")):
        check(case + " identical seeded targets and moments", all(
            results[case][key] == results[reference][key] for key in ("targets", "score_moments")))
    check("actual host scene preserved byte for byte",
          (report / "tkmaster-release/scene-before.json").read_bytes() ==
          (report / "tkmaster-release/scene-after.json").read_bytes())
    check("installed SDK authoring consumer executed", "service=ready" in
          (report / "consumer-run.log").read_text())
    bundle = root / "dist/SpaceRad-0.1.1-local"
    check("bundle and build Worker and shells match", all(
        sha(bundle / name) == sha(root / "build" / name) for name in
        ("spacerad.exe", "spacerad_embedded_example.exe", "scdat_radiation_worker.exe",
         "radiation-build.json", "radiation-datasets.txt")))
    check("relative runtime and Qt plugins staged", read(bundle / "runtime.json")["worker"] ==
          "scdat_radiation_worker.exe" and (bundle / "platforms/qwindows.dll").is_file() and
          (bundle / "styles/qmodernwindowsstyle.dll").is_file())

    excluded = {"acceptance.json", "independent-rehash.json"}
    paths = {p for p in report.rglob("*") if p.is_file() and p.name not in excluded}
    for folder in ("src", "include", "engine", "app", "cmake", "tests", "tools",
                   "integration", "examples", "docs", "third_party", "install"):
        paths.update(p for p in (root / folder).rglob("*") if p.is_file() and
                     "__pycache__" not in p.parts)
    paths.update(p for p in root.iterdir() if p.is_file() and
                 p.name not in ("runtime.local.json", "CMakeUserPresets.json", "topbar_debug.log"))
    paths.update(p for p in bundle.rglob("*") if p.is_file())
    paths.update(root / "build" / name for name in
                 ("scdat_radiation_worker.exe", "spacerad.exe", "spacerad_embedded_example.exe",
                  "test_widget_authoring.exe", "test_authoring.exe"))
    records = [{"path": p.relative_to(root).as_posix(), "size_bytes": p.stat().st_size,
                "sha256": sha(p)} for p in sorted(paths)]
    output.write_text(json.dumps({
        "schema": "spacerad.authoring-acceptance/1", "passed": True,
        "checks": checks, "files": records,
        "scope": "Local Windows 0.1.1 analytic authoring, real-Worker integration and dual-host "
                 "selection. External datasets remain configured separately. This capture does not "
                 "certify dose accuracy, environment models, clean-machine installation or public release."
    }, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    print(f"Authoring acceptance: {len(checks)} checks, {len(records)} captured files")


if __name__ == "__main__":
    main()
