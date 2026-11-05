"""Audit the notional spacecraft TID benchmark at two sampling budgets."""
import argparse
import json
import math
from pathlib import Path
import subprocess
import sys


TARGETS = ("OBC_Si", "Memory_Si", "Payload_Si", "Power_Si", "Wing_L/Cell_Si", "Wing_R/Cell_Si")
THICKNESSES = (0.8, 2.0)
PARTICLES = {"e-", "proton"}
MIN_NONZERO = 20
MAX_TOTAL_RSE = 0.20


def read_json(path):
    return json.loads(path.read_bytes())


def quality(row):
    low_hits = [c["particle"] for c in row["components"] if c["nonzero_histories"] < MIN_NONZERO]
    rse = row["relative_standard_error"]
    return {"passes": not low_hits and rse is not None and rse <= MAX_TOTAL_RSE,
            "species_below_minimum_hits": low_hits, "total_relative_standard_error": rse}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for option in ("environment-dir", "pilot", "production", "cli"):
        parser.add_argument(f"--{option}", type=Path, required=True)
    args = parser.parse_args()
    checks = []

    def check(name, ok):
        if not ok:
            raise ValueError(name)
        checks.append(name)

    def accept(label, directory):
        proc = subprocess.run(
            [sys.executable, str(Path(__file__).with_name("check_ae8ap8_example.py")),
             "--environment-dir", str(args.environment_dir.resolve()),
             "--scan-dir", str(directory.resolve()), "--cli", str(args.cli.resolve())],
            capture_output=True, encoding="utf-8", timeout=90,
        )
        if proc.returncode:
            raise ValueError(f"{label}: environment and archive audit failed: {proc.stderr.strip()}")
        check(f"{label}: reconstructed environment and accepted offline archive", json.loads(proc.stdout)["passed"])
        report = read_json(directory / "scan-result.json")
        check(f"{label}: two bus thicknesses and four complete species jobs",
              report["settings"]["shell_id"] == "Bus_Al" and
              report["settings"]["thicknesses_mm"] == list(THICKNESSES) and
              {(c["thickness_mm"], c["particle"]) for c in report["cases"]} ==
              {(t, p) for t in THICKNESSES for p in PARTICLES})
        curves = {(r["thickness_mm"], r["target_id"]): r for r in report["curves"]}
        check(f"{label}: six identified sensitive parts per thickness",
              set(curves) == {(t, name) for t in THICKNESSES for name in TARGETS})
        reference = None
        shell_mass = {}
        seeds = {}
        for case in report["cases"]:
            case_dir = directory / case["directory"]
            result = read_json(case_dir / "result.json")
            project = read_json(case_dir / "project.json")
            thickness = case["thickness_mm"]
            seeds[thickness, case["particle"]] = project["seed"]
            rows = {row["id"]: row for row in result["geometry_audit"]["volumes"]}
            shell = rows.pop("Bus_Al")
            cavity = [size - 2 * thickness for size in shell["size_mm"]]
            expected_cavity = (348.4, 268.4, 208.4)
            analytic_volume = math.prod(shell["size_mm"]) - math.prod(expected_cavity)
            check(f"{label}/{case['directory']}: fixed bus cavity and audited Geant4 wall mass",
                  all(math.isclose(a, b, abs_tol=1e-10) for a, b in zip(cavity, expected_cavity)) and
                  shell["wall_mm"] == thickness and shell["material"] == "G4_Al" and
                  math.isclose(shell["mass_kg"], analytic_volume * shell["density_kg_m3"] * 1e-9, rel_tol=1e-10))
            geometry = (rows, {k: shell[k] for k in ("world_center_mm", "world_rotation", "material")})
            if reference is None:
                reference = geometry
            check(f"{label}/{case['directory']}: all other physical placements unchanged",
                  geometry == reference and len(rows) == 15 and
                  {k for k, row in rows.items() if row["sensitive"]} == set(TARGETS))
            check(f"{label}/{case['directory']}: planned budget actually transported",
                  result["histories"] == report["histories_per_case"])
            shell_mass[thickness] = shell["mass_kg"]
        check(f"{label}: wall growth increases mass", shell_mass[2.0] > shell_mass[0.8])
        check(f"{label}: distinct seeds across configurations and species", len(set(seeds.values())) == 4)
        return report, curves, reference, shell_mass, seeds

    pilot, low, low_geometry, low_mass, low_seeds = accept("pilot", args.pilot)
    production, high, high_geometry, mass, high_seeds = accept("production", args.production)
    check("larger budget with same geometry, shell masses and audited runtime",
          production["histories_per_case"] > pilot["histories_per_case"] and
          low_geometry == high_geometry and low_mass == mass and
          pilot["runtime_identity"] == production["runtime_identity"])

    rows, comparisons = [], []
    for name in TARGETS:
        for t in THICKNESSES:
            p, h = low[t, name], high[t, name]
            rows.append({"target": name, "thickness_mm": t, "pilot": p, "production": h,
                         "pilot_screen": quality(p), "production_screen": quality(h)})
        a, b = (high[t, name] for t in THICKNESSES)
        change = b["tid_Gy"] - a["tid_Gy"]
        se = math.hypot(a["tid_standard_error_Gy"], b["tid_standard_error_Gy"])
        eligible = quality(a)["passes"] and quality(b)["passes"]
        comparisons.append({"target": name, "thick_minus_thin_Gy": change, "combined_standard_error_Gy": se,
                            "relative_change": change / a["tid_Gy"] if eligible else None,
                            "screen": ("unresolved" if abs(change) <= 2 * se else
                                       "decrease" if change < 0 else "increase") if eligible else "insufficient_statistics"})
    result = {"schema": "spacerad.spacecraft-tid-benchmark/1", "passed": True, "checks": checks,
              "shell_mass_kg": mass, "pilot_histories_per_case": pilot["histories_per_case"],
              "production_histories_per_case": production["histories_per_case"],
              "same_seeds_across_budgets": low_seeds == high_seeds, "rows": rows, "shielding_comparisons": comparisons,
              "statistics_policy": {"min_nonzero_per_species": MIN_NONZERO, "max_total_rse": MAX_TOTAL_RSE,
                                    "difference_screen": "Independent-case engineering screen at 2 combined standard errors; not a coverage guarantee"},
              "scope": "Notional analytic assembly and selected model bands on a short public trajectory segment"}
    print(json.dumps(result, indent=2, allow_nan=False))


if __name__ == "__main__":
    main()
