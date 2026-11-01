"""Real fixed-cavity shielding scans, offline reconstruction and cancellation."""
import argparse
import copy
import csv
import hashlib
import json
import math
import os
from pathlib import Path
import shutil
import signal
import subprocess
import time


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cli", type=Path, required=True)
    parser.add_argument("--runtime", type=Path, required=True)
    parser.add_argument("--service-test", type=Path)
    parser.add_argument("--output-dir", type=Path, required=True)
    args = parser.parse_args()
    cli, runtime, output = args.cli.resolve(), args.runtime.resolve(), args.output_dir.resolve()
    if output.exists():
        raise RuntimeError("Choose a new acceptance directory")
    output.mkdir(parents=True)
    checks, commands = [], []
    environment = dict(os.environ)
    root = Path(__file__).resolve().parents[1]
    project = root / "examples/projects/shielding-scan.json"
    settings = root / "examples/scans/three-thicknesses.json"

    def read(path):
        return json.loads(path.read_text(encoding="utf-8-sig"))

    def sha(path):
        return hashlib.sha256(path.read_bytes()).hexdigest()

    def check(name, ok):
        checks.append({"name": name, "passed": bool(ok)})
        if not ok:
            raise AssertionError(name)

    def protocol(text, kind, code):
        events = [json.loads(line) for line in text.splitlines()]
        check("ordered scan NDJSON", bool(events) and all(e["schema"] == "spacerad.cli-event/1" and e["sequence"] == i for i, e in enumerate(events)))
        terminals = [e for e in events if e["type"] in ("scan-planned", "scan-completed", "archived-scan", "error", "cancelled")]
        check("single terminal scan event", len(terminals) == 1 and terminals[0] == events[-1] and events[-1]["type"] == kind and events[-1]["exit_code"] == code)
        return events

    def invoke(name, arguments, kind, code=0):
        process = subprocess.run([str(cli), *map(str, arguments)], capture_output=True, encoding="utf-8", errors="replace", timeout=180)
        (output / (name + ".stdout.log")).write_text(process.stdout, encoding="utf-8")
        (output / (name + ".stderr.log")).write_text(process.stderr, encoding="utf-8")
        commands.append({"name": name, "arguments": list(map(str, arguments)), "exit_code": process.returncode})
        check(name + " exit", process.returncode == code)
        return protocol(process.stdout, kind, code)

    passed = False
    try:
        before = sha(project), sha(settings)
        plan = invoke("plan", ["plan-scan", "--project", project, "--scan-settings", settings], "scan-planned")[-1]["plan"]
        check("three thicknesses and two species each receive full histories", len(plan["cases"]) == 6 and plan["histories_per_case"] == 10000 and plan["total_planned_histories"] == 60000)
        scans = []
        for name in ("scan", "repeat"):
            folder = output / name
            events = invoke(name, ["scan", "--project", project, "--scan-settings", settings, "--runtime", runtime,
                                   "--output", folder, "--timeout-seconds", 120], "scan-completed")
            report = events[-1]["report"]
            check(name + " complete saved scan", report["status"] == "completed" and len(report["cases"]) == 6 and len(report["curves"]) == 3 and
                  events[-1]["report_sha256"] == sha(folder / "scan-result.json"))
            check(name + " exposes intermediate case progress", any(e["type"] == "progress" and 0 < e["histories"] < e["total_histories"] for e in events))
            all_seeds = set()
            for case in report["cases"]:
                child_dir = folder / case["directory"]
                saved, result = read(child_dir / "project.json"), read(child_dir / "result.json")
                all_seeds.add(saved["seed"])
                check(name + " child identity and single incident species", result["status"] == "completed" and result["histories"] == 10000 and
                      len(saved["environment"]["spectra"]) == 1 and saved["environment"]["spectra"][0]["particle"] == case["particle"] and sha(child_dir / "result.json") == case["result_sha256"])
                shell = next((v for v in saved["volumes"] if v["id"] == "Shield_Al"), None)
                t = case["thickness_mm"]
                check(name + " fixed cavity and zero-shell removal", shell is None if t == 0 else shell["size_mm"] == [29 + 2 * t] * 3 and shell["wall_mm"] == t)
                spectrum = saved["environment"]["spectra"][0]
                integral = sum((b-a)*(v+w)/2 for a,b,v,w in zip(spectrum["energy_MeV"], spectrum["energy_MeV"][1:], spectrum["values"], spectrum["values"][1:]))
                exposure = math.pi*(saved["source_radius_mm"]/10)**2*integral*saved["duration_s"]
                check(name + " exposure applied separately once per species", math.isclose(result["expected_primaries"], exposure, rel_tol=1e-12))
            check(name + " distinct deterministic case seeds", len(all_seeds) == 6)
            for row in report["curves"]:
                components = row["components"]
                check(name + " sum of species dose and variances", len(components) == 2 and math.isclose(row["tid_Gy"], sum(c["tid_Gy"] for c in components), rel_tol=1e-14) and
                      math.isclose(row["tid_standard_error_Gy"]**2, sum(c["tid_standard_error_Gy"]**2 for c in components), rel_tol=1e-14))
            with (folder / "scan.csv").open(newline="") as stream:
                check(name + " CSV contains totals and contributions", len(list(csv.DictReader(stream))) == 9)
            scans.append(report)
        check("seeded repeats preserve numeric curves", scans[0]["curves"] == scans[1]["curves"])
        check("source project and settings unchanged", before == (sha(project), sha(settings)))
        inspected = invoke("inspect", ["inspect-scan", "--job", output / "scan"], "archived-scan")[-1]["report"]
        check("offline verification rebuilds same report", inspected == scans[0])
        relocated = output / "relocated \u626b\u63cf"
        shutil.copytree(output / "scan", relocated)
        invoke("relocated", ["inspect-scan", "--job", relocated], "archived-scan")
        for name, relative in (("changed-csv", "scan.csv"), ("changed-case", "cases/case-000000/project.json")):
            broken = output / name
            shutil.copytree(output / "scan", broken)
            with (broken / relative).open("ab") as stream:
                stream.write(b"\n ")
            invoke(name, ["inspect-scan", "--job", broken], "error", 2)
        digest = sha(output / "scan/scan-manifest.json")
        invoke("old-output", ["scan", "--project", project, "--scan-settings", settings, "--runtime", runtime, "--output", output / "scan"], "error", 2)
        check("existing scan directory preserved", sha(output / "scan/scan-manifest.json") == digest)
        for name, arguments in (("missing-settings", ["plan-scan", "--project", project]),
                                ("offline-rejects-runtime", ["inspect-scan", "--job", output / "scan", "--runtime", runtime])):
            invoke(name, arguments, "error", 2)
        timed = output / "timed"
        invoke("timeout", ["scan", "--project", project, "--histories", 100000000, "--scan-settings", settings,
                           "--runtime", runtime, "--output", timed, "--timeout-seconds", 1], "cancelled", 124)
        partial = invoke("inspect-cancelled", ["inspect-scan", "--job", timed], "archived-scan")[-1]["report"]
        check("cancelled scan is terminal and not completed", partial["status"] == "cancelled" and len(partial["curves"]) == 0)
        if os.name != "nt":
            interrupted = output / "interrupted"
            stdout, stderr = output / "interrupt.stdout.log", output / "interrupt.stderr.log"
            with stdout.open("w") as out, stderr.open("w") as err:
                process = subprocess.Popen([str(cli), "scan", "--project", str(project), "--histories", "100000000", "--scan-settings", str(settings), "--runtime", str(runtime), "--output", str(interrupted)], stdout=out, stderr=err)
                deadline = time.monotonic() + 20
                child_pids = []
                while time.monotonic() < deadline and process.poll() is None:
                    children = Path(f"/proc/{process.pid}/task/{process.pid}/children")
                    if children.exists():
                        child_pids = children.read_text().split()
                    if child_pids:
                        break
                    time.sleep(.03)
                check("SIGINT targets active scan with Worker", bool(child_pids))
                process.send_signal(signal.SIGINT)
                code = process.wait(timeout=15)
            check("scan SIGINT exit", code == 130)
            protocol(stdout.read_text(), "cancelled", 130)
            check("scan leaves no surviving Worker", all(not Path(f"/proc/{pid}").exists() for pid in child_pids))
        if args.service_test:
            process = subprocess.run([str(args.service_test.resolve()), str(runtime), str(output / "service")], capture_output=True, text=True, timeout=120)
            (output / "service.stdout.log").write_text(process.stdout)
            (output / "service.stderr.log").write_text(process.stderr)
            check("real asynchronous scan service acceptance", process.returncode == 0 and read(output / "service/summary.json")["passed"])
        check("parent environment unchanged", dict(os.environ) == environment)
        passed = True
    finally:
        (output / "summary.json").write_text(json.dumps({"passed": passed, "checks": checks, "commands": commands,
            "scope": "synthetic software and transport integration; no independent dose physics validation"}, indent=2), encoding="utf-8")
    print(f"Scan runtime acceptance: passed; {len(checks)} checks")


if __name__ == "__main__":
    main()
