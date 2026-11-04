"""Check saved AE8/AP8 inputs and a completed SpaceRad scan, without SpacePy."""
import argparse
import csv
import hashlib
import json
import math
from pathlib import Path
import subprocess


def read_json(path):
    return json.loads(path.read_bytes())


def rows(path):
    with path.open(encoding="utf-8", newline="") as f:
        return list(csv.DictReader(f))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for key in ("environment-dir", "scan-dir", "cli"):
        parser.add_argument(f"--{key}", type=Path, required=True)
    args = parser.parse_args()
    envdir, job = args.environment_dir.resolve(), args.scan_dir.resolve()
    manifest = read_json(envdir / "environment-manifest.json")
    checks = []

    def check(name, ok):
        if not ok:
            raise ValueError(name)
        checks.append(name)

    def artifact(name, identity):
        path = envdir / name
        data = path.read_bytes()
        check(f"{name}: byte identity", identity == {"file_name": name, "size_bytes": len(data),
              "sha256": hashlib.sha256(data).hexdigest()})
        return path

    def near(a, b):
        return math.isclose(a, b, rel_tol=1e-11)

    check("model manifest schema", manifest["schema"] == "spacerad.ae8ap8-example/1")
    project = read_json(artifact("project.json", manifest["project"]))
    settings = read_json(artifact("scan-settings.json", manifest["scan_settings"]))
    orbit = rows(artifact("orbit.csv", manifest["orbit"]))
    env, n, duration = project["environment"], manifest["samples"], manifest["duration_s"]
    check("full equal-time orbit", len(orbit) == n and duration == project["duration_s"] and
          all(near(float(row["offset_s"]), (i + 0.5) * duration / n) and
              near(math.hypot(*(float(row[f"{axis}_GEI_Re"]) for axis in "xyz")), manifest["orbit_radius_Re"])
              for i, row in enumerate(orbit)))
    check("model flux conventions", env["synthetic"] is False and env["quantity"] == "differential_flux"
          and env["unit"] == "cm-2 s-1 MeV-1" and env["angular_convention"] == "omnidirectional_scalar"
          and env["angular_distribution"] == "isotropic")
    band_flux = {}
    for label, particle in (("AE8MIN", "e-"), ("AP8MIN", "proton")):
        entry = manifest["models"][label]
        spec, = [s for s in env["spectra"] if s["particle"] == particle]
        raw = rows(artifact(f"{label.lower()}-integral-flux.csv", entry["source_file"]))
        grid = spec["energy_MeV"]
        width = [b - a for a, b in zip(grid, grid[1:])]
        check(f"{label}: source identity, binning and sample count",
              spec["provenance"]["source_file"] == entry["source_file"] and
              spec["interpolation"] == "bin_constant" and len(spec["values"]) == len(width) == entry["bins"] and
              all(w > 0 for w in width) and len(raw) == n * len(grid) == entry["valid_integral_values"])
        per_bin = [[] for _ in width]
        for i in range(n):
            block = raw[i * len(grid):(i + 1) * len(grid)]
            flux = [float(r["integral_omnidirectional_flux_cm-2_s-1"]) for r in block]
            if not (all(r["utc"] == orbit[i]["utc"] and float(r["threshold_MeV"]) == e for r, e in zip(block, grid))
                    and all(math.isfinite(f) and f >= 0 for f in flux)
                    and all(a >= b for a, b in zip(flux, flux[1:]))):
                raise ValueError(f"{label}: malformed integral flux block {i}")
            for values, a, b in zip(per_bin, flux, flux[1:]):
                values.append(a - b)
        check(f"{label}: all raw bins reconstruct project values",
              all(near(v, math.fsum(raw_bin) / n / w) for v, raw_bin, w in zip(spec["values"], per_bin, width)))
        total = math.fsum(math.fsum(b) / n for b in per_bin)
        check(f"{label}: manifest flux and fluence", total > 0 and near(total, entry["mean_band_flux_cm-2_s-1"])
              and near(total * duration, entry["band_fluence_cm-2"]))
        band_flux[particle] = total

    proc = subprocess.run([str(args.cli.resolve()), "inspect-scan", "--job", str(job)],
                          capture_output=True, text=True, encoding="utf-8", timeout=60)
    events = [json.loads(line) for line in proc.stdout.splitlines() if line.strip()]
    check("accepted offline scan archive", proc.returncode == 0 and events and events[-1]["type"] == "archived-scan"
          and events[-1]["exit_code"] == 0)
    archived_project = read_json(job / "project.json")
    archived_env = archived_project["environment"]
    # The canonical writer can omit default transforms for all-box geometry.
    def geometry(p):
        defaults = {"parent": "", "rotation_deg": [0, 0, 0], "shape": "box", "visible": True}
        return [defaults | v for v in p["volumes"]]

    check("scan used the generated geometry, environment and settings",
          archived_project["provenance"]["last_loaded_input"] == manifest["project"] and
          all(archived_env[k] == v for k, v in env.items() if k != "metadata") and
          all(archived_env["metadata"][k] == v for k, v in env["metadata"].items()) and
          geometry(archived_project) == geometry(project) and
          all(archived_project[k] == project[k] for k in ("duration_s", "source_radius_mm")) and
          read_json(job / "scan-settings.json") == settings)
    report = read_json(job / "scan-result.json")
    expected_cases = {(t, p) for t in settings["thicknesses_mm"] for p in band_flux}
    expected_targets = {v["id"] for v in project["volumes"] if v["sensitive"]}
    check("all thickness/species cases completed", report["status"] == "completed" and
          len(report["cases"]) == len(expected_cases) and
          {(c["thickness_mm"], c["particle"]) for c in report["cases"]} == expected_cases and
          all(c["status"] == "completed" for c in report["cases"]))
    for case in report["cases"]:
        result = read_json(job / case["directory"] / "result.json")
        expected = math.pi * (project["source_radius_mm"] / 10)**2 * band_flux[case["particle"]] * duration
        check(f"{case['directory']}: exposure applied once and both targets scored",
              near(result["expected_primaries"], expected) and
              {t["id"] for t in result["targets"]} == expected_targets)
    curves = {(r["thickness_mm"], r["target_id"]) for r in report["curves"]}
    check("all thickness/target dose rows", len(report["curves"]) == len(curves)
          and curves == {(t, target) for t in settings["thicknesses_mm"] for target in expected_targets})
    print(json.dumps({"passed": True, "checks": checks, "band_flux_cm-2_s-1": band_flux,
                      "curves": report["curves"]}, indent=2, allow_nan=False))


if __name__ == "__main__":
    main()
