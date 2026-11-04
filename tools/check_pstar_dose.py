"""Compare accepted Geant4 silicon-sphere TID with a NIST PSTAR thin-target estimate."""
import argparse
import hashlib
import json
import math
from pathlib import Path
import subprocess


PSTAR_URL = "https://physics.nist.gov/PhysRefData/Star/Text/PSTAR-t.html"
# Silicon (014), electronic column in MeV cm^2/g; NOT total stopping power.
STOPPING_POWER = {10: 34.58, 20: 20.19}
TARGET_RADIUS_MM = 0.025
SILICON_DENSITY_G_CM3 = 2.33
FLUENCE_CM2 = 1e7
DURATION_S = 50
HALF_WIDTH_MEV = 0.01
HISTORIES = 50_000
JOULES_PER_MEV = 1.602176634e-13
CASES = (
    ("10mev-fluence", 10, 0.055, False, 183491),
    ("10mev-wide-source", 10, 0.085, False, 327353),
    ("10mev-flux", 10, 0.055, True, 642133),
    ("20mev-fluence", 20, 0.055, False, 513223),
)


def make_project(name, energy, source_radius, flux, seed):
    quantity = "differential_flux" if flux else "differential_fluence"
    edges = [energy - HALF_WIDTH_MEV, energy + HALF_WIDTH_MEV]
    level = FLUENCE_CM2 / ((edges[1] - edges[0]) * (DURATION_S if flux else 1))
    return {
        "schema": "scdat.radiation/6",
        "name": f"Synthetic PSTAR benchmark: {name}",
        "duration_s": DURATION_S,
        "source_radius_mm": source_radius,
        "events": HISTORIES,
        "seed": seed,
        "production_cut_mm": 0.1,
        "physics_list": "FTFP_BERT_EMZ",
        "units": {"length": "mm", "energy": "MeV_per_particle", "time": "s"},
        "provenance": {"last_loaded_input": None, "migration_origin": None, "origin_schema": "scdat.radiation/6"},
        "geometry_contract": "scdat.analytic-assembly/1",
        "instance_contract": "scdat.component-instances/1",
        "assemblies": [],
        "templates": [],
        "instances": [],
        "volumes": [{
            "id": "Silicon", "material": "G4_Si", "shape": "sphere",
            "center_mm": [0, 0, 0], "size_mm": [2 * TARGET_RADIUS_MM] * 3,
            "wall_mm": 0, "sensitive": True, "parent": "", "rotation_deg": [0, 0, 0], "visible": True,
        }],
        "environment": {
            "source": "SYNTHETIC narrow-band proton reference; not an orbit model",
            "synthetic": True,
            "metadata": {},
            "quantity": quantity,
            "unit": "cm-2 s-1 MeV-1" if flux else "cm-2 MeV-1",
            "angular_convention": "omnidirectional_scalar",
            "angular_distribution": "isotropic",
            "spectra": [{
                "particle": "proton", "energy_MeV": edges, "values": [level, level],
                "provenance": {"conversions": [], "source_file": None},
            }],
        },
    }


def write_json(path, payload):
    path.write_text(json.dumps(payload, indent=2, allow_nan=False) + "\n", encoding="utf-8")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for option in ("cli", "runtime", "output-dir"):
        parser.add_argument(f"--{option}", type=Path, required=True)
    args = parser.parse_args()
    cli, runtime, output = args.cli.resolve(), args.runtime.resolve(), args.output_dir.resolve()
    output.mkdir(parents=True)
    checks, cases = {}, {}
    summary = {
        "schema": "spacerad.pstar-dose-benchmark/1", "passed": False,
        "reference": PSTAR_URL, "checks": checks, "cases": cases,
        "scope": "Thin-target proton TID reference comparison; no spacecraft mission or device qualification",
    }

    def check(name, condition):
        checks[name] = bool(condition)

    try:
        for name, energy, radius, flux, seed in CASES:
            project = output / f"{name}.json"
            job = output / name
            write_json(project, make_project(name, energy, radius, flux, seed))
            command = [str(cli), "run", "--runtime", str(runtime), "--project", str(project),
                       "--output", str(job), "--timeout-seconds", "120"]
            with (output / f"{name}.stdout.log").open("wb") as stdout, (output / f"{name}.stderr.log").open("wb") as stderr:
                proc = subprocess.run(command, stdout=stdout, stderr=stderr, timeout=150)
            check(f"{name}: CLI success", proc.returncode == 0)
            if proc.returncode != 0:
                continue
            events = [json.loads(line) for line in (output / f"{name}.stdout.log").read_text(encoding="utf-8").splitlines() if line.strip()]
            terminals = [e for e in events if e["type"] in ("completed", "error", "cancelled")]
            accepted = len(terminals) == 1 and terminals[0] == events[-1] and (
                terminals[0]["type"], terminals[0]["exit_code"]) == ("completed", 0)
            check(f"{name}: service accepted result", accepted)
            if not accepted:
                continue
            result_bytes = (job / "result.json").read_bytes()
            digest = hashlib.sha256(result_bytes).hexdigest()
            result = json.loads(result_bytes)
            check(f"{name}: accepted result hash", digest == terminals[0]["result_sha256"])
            target, = result["targets"]
            check(f"{name}: target and history identity", target["id"] == "Silicon" and result["histories"] == HISTORIES)
            reference = FLUENCE_CM2 * STOPPING_POWER[energy] * JOULES_PER_MEV * 1000
            mass = SILICON_DENSITY_G_CM3 * (4 * math.pi / 3) * (TARGET_RADIUS_MM / 10)**3 / 1000
            primaries = math.pi * (radius / 10)**2 * FLUENCE_CM2
            hit_probability = (TARGET_RADIUS_MM / radius)**2
            hit_fraction = target["primary_entry_histories"] / HISTORIES
            hit_se = math.sqrt(hit_probability * (1 - hit_probability) / HISTORIES)
            dose, se = target["tid_Gy"], target["tid_standard_error_Gy"]
            if not (math.isfinite(dose) and dose > 0 and math.isfinite(se) and se > 0):
                check(f"{name}: positive finite dose and uncertainty", False)
                continue
            envelope = (abs(dose - reference) + 3 * se) / reference
            cases[name] = {
                "energy_MeV": energy, "pstar_electronic_MeV_cm2_g": STOPPING_POWER[energy],
                "reference_tid_Gy": reference, "geant4_tid_Gy": dose, "standard_error_Gy": se,
                "relative_difference": (dose - reference) / reference, "relative_envelope": envelope,
                "expected_entry_fraction": hit_probability, "observed_entry_fraction": hit_fraction,
                "expected_mass_kg": mass, "worker_mass_kg": target["mass_kg"],
                "expected_primaries": primaries, "worker_expected_primaries": result["expected_primaries"],
                "geant4_version": result["engine_version"],
                "result_sha256": digest,
            }
            check(f"{name}: analytic mass", math.isclose(target["mass_kg"], mass, rel_tol=1e-4))
            check(f"{name}: analytic fluence normalization", math.isclose(result["expected_primaries"], primaries, rel_tol=1e-10))
            check(f"{name}: projected-area entry fraction", abs(hit_fraction - hit_probability) <= 5 * hit_se)
            check(f"{name}: dose precision", se / dose <= 0.02)
            check(f"{name}: PSTAR dose agreement", envelope <= 0.10)

        base = cases.get("10mev-fluence")
        for name in ("10mev-wide-source", "10mev-flux"):
            other = cases.get(name)
            if base and other:
                delta = abs(base["geant4_tid_Gy"] - other["geant4_tid_Gy"])
                combined_se = math.hypot(base["standard_error_Gy"], other["standard_error_Gy"])
                allowed = 0.01 * base["reference_tid_Gy"] + 4 * combined_se
                other["difference_from_baseline_Gy"] = delta
                other["allowed_difference_from_baseline_Gy"] = allowed
                check(f"{name}: TID consistent with baseline", delta <= allowed)
        summary["passed"] = len(cases) == len(CASES) and all(checks.values())
    finally:
        write_json(output / "summary.json", summary)
    failed = [name for name, passed in checks.items() if not passed]
    if not summary["passed"]:
        parser.exit(1, "PSTAR benchmark failed: " + ", ".join(failed) + "\n")
    print(f"PSTAR dose benchmark passed: {len(checks)} checks over {len(cases)} real transport cases")


if __name__ == "__main__":
    main()
