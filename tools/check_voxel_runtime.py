"""Run actual Geant4 voxel scoring and verify conserved cell doses plus VTI/CSV exports."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cli", type=Path, required=True)
    parser.add_argument("--runtime", type=Path, required=True)
    parser.add_argument("--output-dir", type=Path, required=True)
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[1]
    cli, runtime_path, output = args.cli.resolve(), args.runtime.resolve(), args.output_dir.resolve()
    if output.exists():
        raise RuntimeError("Choose a new voxel test directory")
    output.mkdir(parents=True)
    checks = []
    parent_environment = dict(os.environ)
    passed = False

    def check(name, ok):
        checks.append({"name": name, "passed": bool(ok)})
        if not ok:
            raise AssertionError(name)

    def sha(path):
        return hashlib.sha256(path.read_bytes()).hexdigest()

    try:
        example = root / "examples/projects/voxel-scoring.json"
        source_hash = sha(example)
        project = json.loads(example.read_text(encoding="utf-8"))
        check("example uses explicit v7 voxel schema and bounded grid", project["schema"] == "scdat.radiation/7" and
              project["voxel_scoring"]["counts"] == [2, 2, 2] and project["voxel_scoring"]["volume_id"] == "OBC_Si")
        run_directory = output / "voxel-job"
        proc = subprocess.run([str(cli), "run", "--project", str(example), "--runtime", str(runtime_path), "--output", str(run_directory),
                               "--histories", "4000", "--timeout-seconds", "120"], capture_output=True, text=True, encoding="utf-8", errors="replace", timeout=180)
        (output / "run.stdout.log").write_text(proc.stdout, encoding="utf-8")
        (output / "run.stderr.log").write_text(proc.stderr, encoding="utf-8")
        events = [json.loads(line) for line in proc.stdout.splitlines() if line.strip()]
        check("CLI transport exits successfully", proc.returncode == 0)
        check("CLI run emits one final completion event", bool(events) and events[-1]["type"] == "completed" and events[-1]["exit_code"] == 0)
        check("CLI run events retain sequence schema", all(event["schema"] == "spacerad.cli-event/1" and event["sequence"] == i for i, event in enumerate(events)))
        result_path = run_directory / "result.json"
        result = json.loads(result_path.read_text(encoding="utf-8"))
        field = result["voxel_field"]
        check("Worker records all eight grid cells", field["schema"] == "scdat.radiation-voxel-field/1" and field["counts"] == [2, 2, 2] and len(field["rows"]) == 8 and field["histories"] == 4000)
        target = next(row for row in result["targets"] if row["id"] == "OBC_Si")
        cells_dose = sum(row["tid_Gy"] for row in field["rows"])
        mass_weighted = sum(cell["tid_Gy"] * cell["mass_kg"] for cell in field["rows"]) / field["target_mass_kg"]
        tolerance = 1e-10 * max(1e-30, abs(target["tid_Gy"]))
        check("cell TID mass-weighted over full target matches whole-sensitive-target TID", abs(mass_weighted - target["tid_Gy"]) <= tolerance)
        channels = ("ionizing", "total", "nonionizing")
        cell_means = {channel: sum(cell["score_moments"][channel]["mean_MeV"] for cell in field["rows"]) for channel in channels}
        whole_moments = result["score_moments"]["OBC_Si"]
        check("per-event energy is conserved in every scoring channel", all(abs(cell_means[c] - whole_moments[c]["mean_MeV"]) <= 1e-10 * max(1e-30, abs(whole_moments[c]["mean_MeV"])) for c in channels))
        spectra = result["edep_spectra"]
        check("deposited-energy spectra account for zero and nonzero events", all(sum(histogram["counts"]) + histogram["overflow"] == 4000 and
              abs(sum(histogram["probabilities"]) + histogram["overflow_probability"] - 1.0) < 1e-12 for histogram in spectra.values()))
        check("checked-in synthetic project is unchanged", sha(example) == source_hash)

        runtime = json.loads(runtime_path.read_text(encoding="utf-8-sig"))
        worker = (runtime_path.parent / runtime["worker"]).resolve()
        manifest = worker.parent / "radiation-datasets.txt"
        environment = dict(os.environ)
        for line in manifest.read_text(encoding="utf-8").splitlines():
            if line.strip():
                variable, dataset = line.split("|", 1)
                environment[variable] = str(Path(runtime["data_root"]) / dataset)
        library_dirs = [str(worker.parent)] + [str((runtime_path.parent / directory).resolve()) for directory in runtime.get("library_directories", [])]
        library_variable = "PATH" if os.name == "nt" else "DYLD_LIBRARY_PATH" if sys.platform == "darwin" else "LD_LIBRARY_PATH"
        environment[library_variable] = os.pathsep.join(library_dirs + ([environment[library_variable]] if environment.get(library_variable) else []))
        export = output / "voxel-export"
        export_process = subprocess.run([str(worker), "--export-voxels", str(run_directory / "project.json"), str(result_path), str(export)],
                                        capture_output=True, text=True, encoding="utf-8", errors="replace", env=environment, timeout=60)
        (output / "export.stdout.log").write_text(export_process.stdout, encoding="utf-8")
        (output / "export.stderr.log").write_text(export_process.stderr, encoding="utf-8")
        check("Worker writes VTI and tabular spatial field", export_process.returncode == 0 and (export / "field.vti").is_file() and (export / "field.csv").is_file())
        exported_json = json.loads((export / "field.json").read_text(encoding="utf-8"))
        export_manifest = json.loads((export / "manifest.json").read_text(encoding="utf-8"))
        check("exported JSON is the accepted field", exported_json == field)
        check("all export file hashes and byte lengths match manifest", all(sha(export / entry["path"]) == entry["sha256"] and (export / entry["path"]).stat().st_size == entry["size_bytes"] for entry in export_manifest["files"]))
        check("voxel CSV has one row per grid cell", len((export / "field.csv").read_text(encoding="utf-8").splitlines()) == 9)
        passed = True
    finally:
        (output / "summary.json").write_text(json.dumps({"passed": passed, "checks": checks,
            "scope": "actual synthetic-spectrum Geant4 voxel scoring, exact geometry and output consistency; no independent physical validation"}, indent=2), encoding="utf-8")
        check_environment_unchanged = dict(os.environ) == parent_environment
        if not check_environment_unchanged:
            raise AssertionError("CLI/helper modified the test process environment")
    print(f"Voxel runtime acceptance passed: {len(checks)} checks")


if __name__ == "__main__":
    main()
