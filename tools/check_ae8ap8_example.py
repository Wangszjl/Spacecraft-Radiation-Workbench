"""Check saved AE8/AP8 inputs and optionally a completed SpaceRad scan, without SpacePy."""
import argparse
import csv
import hashlib
import json
import math
from pathlib import Path
import subprocess

from ae8ap8_ephemeris import EARTH_RADIUS_KM, parse_ephemeris


def read_json(path):
    return json.loads(path.read_bytes())


def rows(path):
    with path.open(encoding="utf-8", newline="") as f:
        return list(csv.DictReader(f))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--environment-dir", type=Path, required=True)
    parser.add_argument("--scan-dir", type=Path)
    parser.add_argument("--cli", type=Path)
    args = parser.parse_args()
    if bool(args.scan_dir) != bool(args.cli):
        parser.error("--scan-dir and --cli must be used together")
    envdir = args.environment_dir.resolve()
    manifest = read_json(envdir / "environment-manifest.json")
    checks = []

    def check(name, ok):
        if not ok:
            raise ValueError(name)
        checks.append(name)

    def artifact(name, identity, source_name=None):
        path = envdir / name
        data = path.read_bytes()
        check(f"{name}: byte identity", identity == {"file_name": source_name or name, "size_bytes": len(data),
              "sha256": hashlib.sha256(data).hexdigest()})
        return path

    def near(a, b):
        return math.isclose(a, b, rel_tol=1e-11)

    external = manifest["schema"] == "spacerad.ae8ap8-ephemeris/1"
    check("model manifest schema", external or manifest["schema"] == "spacerad.ae8ap8-example/1")
    project = read_json(artifact("project.json", manifest["project"]))
    settings = read_json(artifact("scan-settings.json", manifest["scan_settings"])) if manifest["scan_settings"] else None
    orbit = rows(artifact("orbit.csv", manifest["orbit"]))
    env, n, duration = project["environment"], manifest["samples"], manifest["duration_s"]
    if external:
        source = manifest["input_ephemeris"]
        original = artifact("input-ephemeris.csv", source, source["file_name"])
        times, offsets, positions = parse_ephemeris(original.read_bytes(), manifest["input_position_unit"], manifest["max_gap_s"])
        frame = manifest["frame"]
        check("declared coordinate frame and Earth radius", frame in ("GEO", "GEI") and manifest["earth_radius_km"] == EARTH_RADIUS_KM)
        utc = [t.isoformat(timespec="microseconds").replace("+00:00", "Z") for t in times]
        check("normalized trajectory matches the input ephemeris",
              len(orbit) == len(times) == n and duration == offsets[-1] == project["duration_s"] and
              utc[0] == manifest["epoch_utc"] and utc[-1] == manifest["interval_end_utc"] and
              all(row["utc"] == stamp and float(row["offset_s"]) == offset and
                  tuple(float(row[f"{axis}_{frame}_Re"]) for axis in "xyz") == point
                  for row, stamp, offset, point in zip(orbit, utc, offsets, positions)))
        if manifest["base_project"]:
            identity = manifest["base_project"]
            base = read_json(artifact("input-project.json", identity, identity["file_name"]))
            excluded = {"name", "duration_s", "environment"}
            check("base geometry and run settings unchanged", {k: v for k, v in base.items() if k not in excluded} ==
                  {k: v for k, v in project.items() if k not in excluded})
    else:
        check("full equal-time orbit", len(orbit) == n and duration == project["duration_s"] and
              all(near(float(row["offset_s"]), (i + 0.5) * duration / n) and
                  near(math.hypot(*(float(row[f"{axis}_GEI_Re"]) for axis in "xyz")), manifest["orbit_radius_Re"])
                  for i, row in enumerate(orbit)))

    def average(values):
        if external:
            return math.fsum((offsets[i+1] - offsets[i]) * (a + b) / 2
                             for i, (a, b) in enumerate(zip(values, values[1:]))) / duration
        return math.fsum(values) / n
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
              all(near(v, average(raw_bin) / w) for v, raw_bin, w in zip(spec["values"], per_bin, width)))
        total = math.fsum(average(b) for b in per_bin)
        check(f"{label}: manifest flux and fluence", total > 0 and near(total, entry["mean_band_flux_cm-2_s-1"])
              and near(total * duration, entry["band_fluence_cm-2"]))
        band_flux[particle] = total

    report = {"passed": True, "checks": checks, "band_flux_cm-2_s-1": band_flux}
    if args.scan_dir:
        report["curves"] = verify_scan(args.cli.resolve(), args.scan_dir.resolve(), project, manifest, settings, band_flux, check, near)
    print(json.dumps(report, indent=2, allow_nan=False))


def verify_scan(cli, job, project, manifest, settings, band_flux, check, near):
    env, duration = project["environment"], manifest["duration_s"]
    proc = subprocess.run([str(cli), "inspect-scan", "--job", str(job)],
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

    archived_settings = read_json(job / "scan-settings.json")
    check("scan used the generated geometry and environment",
          archived_project["provenance"]["last_loaded_input"] == manifest["project"] and
          all(archived_env[k] == v for k, v in env.items() if k != "metadata") and
          all(archived_env["metadata"][k] == v for k, v in env["metadata"].items()) and
          geometry(archived_project) == geometry(project) and
          all(archived_project[k] == project[k] for k in ("duration_s", "source_radius_mm")))
    if settings is not None and settings != archived_settings:
        raise ValueError("Scan settings differ from the generated settings")
    settings = archived_settings
    report = read_json(job / "scan-result.json")
    expected_cases = {(t, p) for t in settings["thicknesses_mm"] for p in band_flux}
    expected_targets = {r["target_id"] for r in report["curves"]}
    if not manifest.get("base_project"):
        check("both demonstration targets scored", expected_targets == {"OBC_Si", "Payload_Si"})
    check("all thickness/species cases completed", report["status"] == "completed" and
          len(report["cases"]) == len(expected_cases) and
          {(c["thickness_mm"], c["particle"]) for c in report["cases"]} == expected_cases and
          all(c["status"] == "completed" for c in report["cases"]))
    for case in report["cases"]:
        result = read_json(job / case["directory"] / "result.json")
        expected = math.pi * (project["source_radius_mm"] / 10)**2 * band_flux[case["particle"]] * duration
        check(f"{case['directory']}: exposure applied once and all targets scored",
              near(result["expected_primaries"], expected) and
              {t["id"] for t in result["targets"]} == expected_targets)
    curves = {(r["thickness_mm"], r["target_id"]) for r in report["curves"]}
    check("all thickness/target dose rows", len(report["curves"]) == len(curves)
          and curves == {(t, target) for t in settings["thicknesses_mm"] for target in expected_targets})
    return report["curves"]


if __name__ == "__main__":
    main()
