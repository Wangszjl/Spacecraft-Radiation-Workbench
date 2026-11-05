"""Run a real mixed-species, broad-spectrum Geant4 check of weighted vs direct TID."""
import argparse
from copy import deepcopy
import json
import math
from pathlib import Path
import subprocess

from check_pstar_dose import make_project
from enable_importance import weighted_project


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for key in ("cli", "runtime", "output-dir"):
        parser.add_argument(f"--{key}", type=Path, required=True)
    args = parser.parse_args()
    root = args.output_dir.resolve()
    root.mkdir(parents=True)
    cli = str(args.cli.resolve())
    def invoke(name, command):
        proc = subprocess.run([cli, *command], capture_output=True, encoding="utf-8", timeout=180)
        (root / f"{name}.stdout.log").write_text(proc.stdout, encoding="utf-8")
        (root / f"{name}.stderr.log").write_text(proc.stderr, encoding="utf-8")
        events = [json.loads(line) for line in proc.stdout.splitlines() if line.strip()]
        return proc.returncode, events[-1]

    p = make_project("importance check", 10, 4.5, True, 6273841)
    p["name"] = "Synthetic broad-spectrum electron/proton importance-sampling check"
    p["environment"]["source"] = "SYNTHETIC broad-spectrum electron/proton flux"
    p["events"] = 80000
    p["volumes"][0]["size_mm"] = [0.8, 0.8, 0.8]
    p["environment"]["spectra"][0].update(energy_MeV=[1, 20], values=[1000, 1000])
    electron=deepcopy(p["environment"]["spectra"][0])
    electron["particle"]="e-"
    p["environment"]["spectra"].append(electron)
    q = weighted_project(p, 0.35, 0.2, {"e-":.2,"proton":.8})
    q["seed"] = 8235243
    results = {}
    for name, project in (("direct", p), ("weighted", q)):
        src = root / f"{name}.json"
        src.write_text(json.dumps(project, allow_nan=False), encoding="utf-8")
        job = root / name
        code, event = invoke(name, ["run", "--project", str(src), "--runtime", str(args.runtime.resolve()),
                                    "--output", str(job), "--timeout-seconds", "120"])
        if code or event["type"] != "completed":
            raise RuntimeError(event)
        code, event = invoke(name + "-inspect", ["inspect-result", "--job", str(job)])
        if code or event["type"] != "archived-result":
            raise RuntimeError(event)
        results[name] = json.loads((job / "result.json").read_bytes())
    a, b = (results[k] for k in ("direct", "weighted"))
    n = b["histories"]
    da, db = a["targets"][0], b["targets"][0]
    audit = b["source_importance"]
    expected = 1 - 0.35*(1-0.2)
    se = math.sqrt(expected*(1-expected)/n)
    support = audit["target_support"]["Silicon"]
    checks = {
        "unchanged_physical_exposure": a["expected_primaries"] == b["expected_primaries"] and
            "source_importance" not in a and da["mass_kg"] == db["mass_kg"],
        "proposal_tail_frequency": abs(audit["tail_histories"]/n - expected) <= 5*se,
        "proposal_species_frequency": all(
            abs(b["source_diagnostics"]["species_histories"][particle]/n-fraction)<=
            5*math.sqrt(fraction*(1-fraction)/n)
            for particle,fraction in q["source_sampling"]["proposal_species_fractions"].items()),
        "both_source_regions_observed": all(
            min(support[particle]["body"]["nonzero_histories"],support[particle]["tail"]["nonzero_histories"])>=20
            for particle in support),
        "mean_likelihood_weight": audit["sum_weights"] > 0 and math.isclose(
            audit["sum_weights"],sum(
                audit["target_support"]["Silicon"][particle][region]["histories"] *
                (physical/proposal) *
                (1/.35 if region=="body" else .2/expected)
                for particle,physical,proposal in (("e-",.5,.2),("proton",.5,.8))
                for region in ("body","tail")),rel_tol=1e-12),
        "dose_agreement": abs(da["tid_Gy"]-db["tid_Gy"]) <=
            4*math.hypot(da["tid_standard_error_Gy"],db["tid_standard_error_Gy"]),
    }
    damaged = root / "tampered"
    damaged.mkdir()
    (damaged / "project.json").write_bytes((root / "weighted" / "project.json").read_bytes())
    altered = deepcopy(b)
    altered["source_importance"]["tail_histories"] += 1
    (damaged / "result.json").write_text(json.dumps(altered), encoding="utf-8")
    code, event = invoke("tampered-inspect", ["inspect-result", "--job", str(damaged)])
    checks["tampered_weight_audit_rejected"] = (code != 0 and event["type"] == "error" and
                                               "Weighted" in event.get("message", ""))
    summary = {"passed": all(checks.values()), "checks": checks, "direct": da, "weighted": db,
               "sampled_tail_fraction": audit["tail_histories"]/n, "expected_tail_fraction": expected}
    (root / "summary.json").write_text(json.dumps(summary, indent=2, allow_nan=False), encoding="utf-8")
    if not summary["passed"]:
        raise RuntimeError(checks)
    print(f"Importance runtime checks passed: {len(checks)}")


if __name__ == "__main__":
    main()
