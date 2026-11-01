"""Run two synthetic shielding cases, then inspect/compare saved jobs offline."""
import argparse
import copy
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cli", type=Path, required=True)
    parser.add_argument("--runtime", type=Path, required=True)
    parser.add_argument("--output-dir", type=Path, required=True)
    args = parser.parse_args()
    cli, runtime, output = args.cli.resolve(), args.runtime.resolve(), args.output_dir.resolve()
    if output.exists():
        raise RuntimeError("Choose a new acceptance directory")
    output.mkdir(parents=True)
    checks = []
    original_environment = dict(os.environ)
    passed = False

    def check(name, ok):
        checks.append({"name": name, "passed": bool(ok)})
        if not ok:
            raise AssertionError(name)

    def invoke(name, arguments, kind, code=0):
        process = subprocess.run([str(cli), *map(str, arguments)], capture_output=True,
                                 encoding="utf-8", errors="replace", timeout=180)
        (output / (name + ".stdout.log")).write_text(process.stdout, encoding="utf-8")
        (output / (name + ".stderr.log")).write_text(process.stderr, encoding="utf-8")
        events = [json.loads(line) for line in process.stdout.splitlines()]
        check(name + " process exit", process.returncode == code)
        check(name + " ordered protocol", bool(events) and all(e["schema"] == "spacerad.cli-event/1" and e["sequence"] == i for i, e in enumerate(events)))
        check(name + " final event", events[-1]["type"] == kind and events[-1]["exit_code"] == code)
        return events[-1]

    def sha(path):
        return hashlib.sha256(path.read_bytes()).hexdigest()

    try:
        root = Path(__file__).resolve().parents[1]
        project = json.loads((root / "examples/projects/electron-proton-flux.json").read_text())
        project["name"] = "SYNTHETIC shielding comparison; no orbit model or independent physics validation"
        project["events"] = 50000
        project["source_radius_mm"] = 30.0
        project.update({"schema": "scdat.radiation/6", "geometry_contract": "scdat.analytic-assembly/1",
                        "instance_contract": "scdat.component-instances/1", "assemblies": [], "templates": [], "instances": []})
        project["volumes"] = [
            {"id": "Shield_Al", "material": "G4_Al", "center_mm": [0, 0, 0], "size_mm": [30, 30, 30], "wall_mm": 0.5, "sensitive": False},
            {"id": "Detector_Si", "material": "G4_Si", "center_mm": [0, 0, 0], "size_mm": [20, 20, 20], "wall_mm": 0, "shape": "sphere", "sensitive": True},
        ]
        for volume in project["volumes"]:
            volume.update({"parent": "", "rotation_deg": [0, 0, 0], "visible": True})
            volume.setdefault("shape", "box")
        # The analytic sphere remains the same; only the Al box-shell wall changes.
        jobs, results = {}, {}
        for name, wall in (("baseline", 0.5), ("candidate", 2.0)):
            case = copy.deepcopy(project)
            case["volumes"][0]["wall_mm"] = wall
            source = output / (name + "-input.json")
            source.write_text(json.dumps(case, indent=2), encoding="utf-8")
            source_sha = sha(source)
            jobs[name] = output / name
            event = invoke(name + "-run", ["run", "--runtime", runtime, "--project", source, "--output", jobs[name], "--timeout-seconds", 120], "completed")
            results[name] = json.loads((jobs[name] / "result.json").read_text())
            check(name + " real completed transport and nonzero detector histories", event["histories"] == 50000 and
                  results[name]["targets"][0]["nonzero_histories"] > 1000 and results[name]["engine_version"] == "11.4.3")
            check(name + " source file preserved", sha(source) == source_sha)
        snapshot_hashes = {str(path): sha(path) for job in jobs.values() for path in (job / "project.json", job / "result.json")}
        summary = invoke("inspect", ["inspect-result", "--job", jobs["baseline"]], "archived-result")["summary"]
        check("offline result fingerprint and explicit verification scope", summary["result_sha256"] == sha(jobs["baseline"] / "result.json") and
              summary["verification"] == "offline_pair_consistency_only_not_producer_authentication")
        report = invoke("compare", ["compare", "--baseline", jobs["baseline"], "--candidate", jobs["candidate"]], "comparison")["comparison"]
        row = report["rows"][0]
        av, bv = results["baseline"]["targets"][0]["tid_Gy"], results["candidate"]["targets"][0]["tid_Gy"]
        check("unchanged target and source with altered shielding", len(report["rows"]) == 1 and row["status"] == "matched" and row["controlled_comparison"] and report["same_source_settings"])
        check("actual arithmetic uses archived doses", row["delta_Gy"] == bv - av and row["ratio_B_over_A"] == bv / av and av > 0 and bv > 0 and av != bv)
        check("difference significance is not invented", report["difference_uncertainty"] == "not_estimated_covariance_unknown" and "delta_standard_error_Gy" not in row)
        relocated = output / "relocated \u5386\u53f2 job"
        shutil.copytree(jobs["baseline"], relocated)
        moved = invoke("relocated-inspect", ["inspect-result", "--job", relocated], "archived-result")["summary"]
        check("relocation retains saved byte identities", moved["result_sha256"] == summary["result_sha256"] and moved["project_sha256"] == summary["project_sha256"])
        # No runtime option is permitted on an offline command.
        invoke("reject-runtime-option", ["inspect-result", "--job", relocated, "--runtime", output / "missing-runtime.json"], "error", 2)
        invoke("missing-baseline", ["compare", "--candidate", relocated], "error", 2)
        invoke("reject-run-archive-option", ["run", "--baseline", relocated], "error", 2)
        damaged = output / "damaged"
        shutil.copytree(jobs["baseline"], damaged)
        saved = json.loads((damaged / "project.json").read_text())
        saved["seed"] += 1
        (damaged / "project.json").write_text(json.dumps(saved), encoding="utf-8")
        invoke("damaged-input", ["inspect-result", "--job", damaged], "error", 2)
        check("review preserves both saved jobs", all(sha(Path(path)) == digest for path, digest in snapshot_hashes.items()))
        check("parent process environment preserved", dict(os.environ) == original_environment)
        (output / "comparison.json").write_text(json.dumps(report, indent=2), encoding="utf-8")
        passed = True
    finally:
        (output / "summary.json").write_text(json.dumps({"passed": passed, "checks": checks,
            "scope": "synthetic real transport and offline consistency; no independent dose physics validation"}, indent=2), encoding="utf-8")
    print(f"{len(checks)} results integration checks passed")


if __name__ == "__main__":
    main()
