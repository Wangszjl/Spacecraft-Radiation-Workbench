"""Small real-Worker partition study and archived-output tamper acceptance."""
import argparse
from pathlib import Path
import subprocess
import sys

from run_energy_response import cli_environment
from stratified_tid import encode, read, require


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ("cli", "runtime", "output-dir"):
        parser.add_argument(f"--{name}", required=True, type=Path)
    args = parser.parse_args()
    cli, runtime, root = (p.resolve() for p in (args.cli, args.runtime, args.output_dir))
    env = cli_environment(cli, runtime)
    source_root = Path(__file__).resolve().parents[1]
    project = read(source_root/"examples/projects/spacecraft-tid-assembly.json")
    for spectrum in project["environment"]["spectra"]:
        spectrum["energy_MeV"] = [0.1, 3] if spectrum["particle"] == "e-" else [1, 100]
    config = read(source_root/"examples/scans/spacecraft-partition-study.json")
    for name, partition in config["partitions"].items():
        for spec in partition["species"].values():
            spec["histories"] = [2000 if name == "coarse" else 1000]*len(spec["histories"])
    root.mkdir(parents=True)
    (root/"project.json").write_bytes(encode(project))
    (root/"settings.json").write_bytes(encode(config))
    script = Path(__file__).with_name("stratified_study.py")
    common = ["--cli", str(cli), "--runtime", str(runtime), "--output-dir", str(root/"study")]
    checks = {}

    def invoke(label, arguments, success=True):
        proc = subprocess.run([sys.executable, str(script), *arguments], env=env, capture_output=True,
                              encoding="utf-8", timeout=600)
        (root/f"{label}.stdout.log").write_text(proc.stdout, encoding="utf-8")
        (root/f"{label}.stderr.log").write_text(proc.stderr, encoding="utf-8")
        require((proc.returncode == 0) == success, f"Unexpected exit for {label}: {proc.returncode}; see logs")
        checks[label] = True

    invoke("real_study", ["run", *common, "--project", str(root/"project.json"),
        "--study-settings", str(root/"settings.json"), "--scan-settings",
        str(source_root/"examples/scans/spacecraft-bus-thickness.json"), "--parallel", "2"])
    invoke("offline_reconstruction", ["inspect", *common])
    result = read(root/"study/summary.json")
    require(result["total_histories"] == 64000 and result["unique_child_seeds"] == 48
            and len(result["archive_results"]) == 48 and len(result["rows"]) == 12,
            "Unexpected actual study budget or archive coverage")
    checks["actual_budget_and_unique_seed_coverage"] = True
    zeros = [c for row in result["rows"] for estimate in row["estimates"] for c in estimate["components"]
             if c["nonzero_histories"] == 0]
    require(zeros and all(c["zero_event_probability_upper"] > 0 and c["dose_upper_bound_Gy"] is None
                         and not c["support_screen_passed"] for c in zeros), "Zero-hit regions were not guarded")
    require(all(row["mean_standard_error_Gy"] is None and row["mean_relative_standard_error"] is None
                and not row["support_screen_passed"] for row in result["rows"]
                if any(c["nonzero_histories"] == 0 for e in row["estimates"] for c in e["components"])),
            "Unknown component uncertainty was replaced with a full-spectrum error")
    checks["zero_support_never_becomes_dose_bound_or_qualified_error"] = True

    def tamper(label, relative, transform):
        path = root/"study"/relative
        saved = path.read_bytes()
        try:
            path.write_bytes(transform(saved))
            invoke(label, ["inspect", *common], success=False)
        finally:
            path.write_bytes(saved)

    def change_json(data, transform):
        import json
        obj = json.loads(data)
        transform(obj)
        return encode(obj)

    tamper("reject_changed_plan", "plan.json", lambda b: change_json(b, lambda o: o.update(total_histories=o["total_histories"]+1)))
    tamper("reject_changed_summary_dose", "summary.json", lambda b: change_json(b,
        lambda o: o["rows"][0].update(mean_estimate_Gy=o["rows"][0]["mean_estimate_Gy"]+1)))
    tamper("reject_changed_csv", "summary.csv", lambda b: b+b"altered\n")
    tamper("reject_changed_review", "review.html", lambda b: b+b"<p>altered</p>\n")
    tamper("reject_changed_child_csv", "coarse-1/summary.csv", lambda b: b+b"altered\n")
    relative = result["archive_results"][0]["path"]
    tamper("reject_changed_raw_result", relative, lambda b: b+b" ")
    invoke("restored_archive_reconstruction", ["inspect", *common])
    summary = {"passed": all(checks.values()), "checks": checks, "total_histories": result["total_histories"],
               "real_jobs": result["unique_child_seeds"], "scope": "Small synthetic source; software/archival acceptance, not mission-dose precision validation"}
    (root/"summary.json").write_bytes(encode(summary))
    print(f"Real partition-study acceptance passed: {len(checks)} checks, 48 jobs, 64000 histories")


if __name__ == "__main__":
    main()
