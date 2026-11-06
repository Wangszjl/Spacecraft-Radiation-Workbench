"""Run or reconstruct an energy-stratified SpaceRad shielding scan."""
import argparse
from bisect import bisect_right
from concurrent.futures import ThreadPoolExecutor
from copy import deepcopy
import csv
import hashlib
import io
import json
import math
from pathlib import Path
import subprocess
import sys
from run_energy_response import cli_environment


MEV_J = 1.602176634e-13


def require(condition, reason):
    if not condition:
        raise ValueError(reason)


def encode(value):
    return (json.dumps(value, indent=2, sort_keys=True, allow_nan=False) + "\n").encode()


def read(path):
    return json.loads(path.read_bytes())


def sha(data):
    return hashlib.sha256(data).hexdigest()


def sha_file(path):
    return sha(path.read_bytes())


def near(a, b):
    return math.isclose(a, b, rel_tol=1e-10)


def number(x):
    return type(x) in (float, int) and math.isfinite(x)


def integral(s):
    edges, values = s["energy_MeV"], s["values"]
    heights = values if s.get("interpolation") == "bin_constant" else [(a+b)/2 for a, b in zip(values, values[1:])]
    return math.fsum((b-a)*h for a, b, h in zip(edges, edges[1:], heights))


def split_spectrum(s, cuts):
    grid, values = s["energy_MeV"], s["values"]
    mode = s.get("interpolation", "linear")
    require(mode in ("linear", "bin_constant") and len(grid) >= 2 and
            len(grid) == len(values) + (mode == "bin_constant") and
            all(number(e) and e > 0 for e in grid) and all(a < b for a,b in zip(grid,grid[1:])) and
            all(number(v) and v >= 0 for v in values), "Invalid source spectrum")
    require(type(cuts) is list and all(number(x) and grid[0] < x < grid[-1] for x in cuts) and
            all(a < b for a,b in zip(cuts,cuts[1:])), "Cuts must be strictly increasing inside the spectrum")

    def value(e):
        i = bisect_right(grid, e) - 1
        if mode == "bin_constant" or grid[i] == e:
            return values[i]
        f = (e - grid[i]) / (grid[i+1] - grid[i])
        return (1-f)*values[i] + f*values[i+1]

    bounds = [grid[0], *cuts, grid[-1]]
    parts = []
    for low, high in zip(bounds, bounds[1:]):
        edges = [low, *(e for e in grid if low < e < high), high]
        part = deepcopy(s)
        part.update(energy_MeV=edges, values=[value(e) for e in (edges[:-1] if mode == "bin_constant" else edges)])
        area = integral(part)
        require(number(area) and area > 0, "A selected stratum has zero or invalid integral")
        parts.append(part)
    require(near(math.fsum(integral(p) for p in parts), integral(s)), "Energy partition does not conserve source integral")
    return parts


def make_plan(project, settings, config, source_sha):
    require(config.get("schema") == "spacerad.energy-strata/1" and set(config) == {"schema", "species"}, "Unsupported strata settings")
    spectra = project["environment"]["spectra"]
    by_particle = {s["particle"]: s for s in spectra}
    require(0 < len(by_particle) == len(spectra) and by_particle.keys() <= {"e-", "proton"} and
            by_particle.keys() == config["species"].keys(), "Provide one stratum specification per unique incident species")
    require(project.get("voxel_scoring") is None, "Stratified scan does not combine voxel fields")
    require(project.get("source_sampling") is None, "Use the physical source for fixed stratification")
    thicknesses = settings["thicknesses_mm"]
    require(settings["schema"] == "spacerad.shielding-settings/1" and 2 <= len(thicknesses) <= 64, "Invalid scan settings")
    quantity = project["environment"]["quantity"]
    require(quantity in ("differential_flux", "differential_fluence"), "Unsupported exposure quantity")
    factor = math.pi * (project["source_radius_mm"] / 10)**2
    if quantity == "differential_flux":
        factor *= project["duration_s"]
    jobs = []
    for species, original in sorted(by_particle.items()):
        spec = config["species"][species]
        require(set(spec) == {"split_MeV", "histories"} and type(spec["histories"]) is list and
                1 <= len(spec["histories"]) <= 16 and all(type(n) is int and 2 <= n <= 100000000 for n in spec["histories"]),
                "Invalid stratum history allocation")
        parts = split_spectrum(original, spec["split_MeV"])
        require(len(parts) == len(spec["histories"]), "Supply one history budget per stratum")
        for spectrum, histories in zip(parts, spec["histories"]):
            idx = len(jobs)
            low, high = spectrum["energy_MeV"][0], spectrum["energy_MeV"][-1]
            area = integral(spectrum)
            spectrum["provenance"]["energy_stratum"] = {"parent_project_sha256": source_sha, "range_MeV": [low, high]}
            child = deepcopy(project)
            child["environment"]["spectra"] = [spectrum]
            child["events"] = histories
            # Reserve a separate seed block for each scan's thickness children.
            child["seed"] = 1 + (project["seed"] - 1 + 4096 + idx * 128) % 2147483646
            jobs.append({"id": f"stratum-{idx:03}", "particle": species, "range_MeV": [low, high],
                         "integral": area, "fraction_of_species": area / integral(original),
                         "expected_primaries": area * factor, "histories_per_thickness": histories,
                         "input_sha256": sha(encode(child)), "project": child})
    return {"schema": "spacerad.stratified-tid-plan/1", "parent_project_sha256": source_sha,
            "scan_settings": settings, "strata_settings": config,
            "total_histories": sum(j["histories_per_thickness"] for j in jobs)*len(thicknesses), "jobs": jobs}


def cli_scan(cli, root, job, runtime, timeout, env=None):
    name = job["id"]
    path = root / f"{name}.json"
    path.write_bytes(encode(job["project"]))
    command = [str(cli), "scan", "--project", str(path), "--scan-settings", str(root / "scan-settings.json"),
               "--output", str(root / name), "--timeout-seconds", str(timeout)]
    if runtime:
        command += ["--runtime", str(runtime)]
    with (root / f"{name}.stdout.log").open("wb") as stdout, (root / f"{name}.stderr.log").open("wb") as stderr:
        proc = subprocess.run(command, stdout=stdout, stderr=stderr, timeout=timeout+60, env=env)
    require(proc.returncode == 0, f"{name}: scan failed; inspect logs")
    print(f"Completed {name}", flush=True)


def inspect_scan(cli, directory, env=None):
    proc = subprocess.run([str(cli), "inspect-scan", "--job", str(directory)],
                          capture_output=True, text=True, encoding="utf-8", timeout=90, env=env)
    events = [json.loads(line) for line in proc.stdout.splitlines() if line.strip()]
    require(proc.returncode == 0 and events and events[-1]["type"] == "archived-scan" and
            events[-1]["report"]["status"] == "completed", f"Invalid or incomplete scan: {directory}")
    return events[-1]["report"]


def summary_csv(rows):
    fields = ("thickness_mm", "target_id", "tid_Gy", "observed_standard_error_Gy",
              "tid_standard_error_Gy", "relative_standard_error", "zero_hit_strata")
    f = io.StringIO(newline="")
    writer = csv.DictWriter(f, fields, extrasaction="ignore")
    writer.writeheader()
    for row in rows:
        target = row["target_id"]
        if target and (target[0].isspace() or target[0] in "=+-@"):
            target = "'" + target
        writer.writerow({**row, "target_id": target, "zero_hit_strata": ";".join(row["zero_hit_strata"])})
    return f.getvalue().encode("utf-8")


def write_csv(path, rows):
    path.write_bytes(summary_csv(rows))


def inspect(cli, root, baseline=None, env=None):
    parent_path = root / "parent-project.json"
    plan = make_plan(read(parent_path), read(root / "scan-settings.json"), read(root / "strata-settings.json"), sha_file(parent_path))
    require(encode(plan) == (root / "plan.json").read_bytes(), "Stored plan differs from source reconstruction")
    settings = plan["scan_settings"]
    runtime = None
    expected_geometry = {}
    combined = {}
    seeds = set()
    for job in plan["jobs"]:
        name = job["id"]
        require(sha_file(root / f"{name}.json") == job["input_sha256"], f"{name}: altered stratum input")
        directory = root / name
        report = inspect_scan(cli, directory, env)
        archive = read(directory / "project.json")
        require(archive["provenance"]["last_loaded_input"]["sha256"] == job["input_sha256"] and
                report["settings"] == settings and report["histories_per_case"] == job["histories_per_thickness"],
                f"{name}: archive differs from planned input, settings or budget")
        runtime = report["runtime_identity"] if runtime is None else runtime
        require(runtime == report["runtime_identity"], "Worker identity changed between strata")
        keys = set()
        for case in report["cases"]:
            result = read(directory / case["directory"] / "result.json")
            t = case["thickness_mm"]
            require(case["particle"] == job["particle"] and near(result["expected_primaries"], job["expected_primaries"]) and
                    result["histories"] == job["histories_per_thickness"], f"{name}: unexpected source normalization or histories")
            seed = result["project"]["seed"]
            require(seed not in seeds, "Repeated child seed")
            seeds.add(seed)
            geom = result["geometry_audit"]
            require(expected_geometry.setdefault(t, geom) == geom, f"{name}: physical geometry changed at thickness {t}")
            for target in result["targets"]:
                key = t, target["id"]
                require(key not in keys, f"{name}: duplicate target row")
                keys.add(key)
                raw = result["score_moments"][target["id"]]["ionizing"]
                factor = MEV_J * job["expected_primaries"] / target["mass_kg"]
                dose = raw["mean_MeV"] * factor
                n = result["histories"]
                error = math.sqrt(raw["m2_MeV2"] / (n*(n-1))) * factor
                require(raw["count"] == n and raw["nonzero_histories"] == target["nonzero_histories"] and
                        near(dose, target["tid_Gy"]) and near(error, target["tid_standard_error_Gy"]),
                        f"{name}: dose or error does not match raw moments and stratum exposure")
                combined.setdefault(key, []).append({"stratum": name, "particle": job["particle"],
                    "range_MeV": job["range_MeV"], "mass_kg": target["mass_kg"], "histories": n,
                    "nonzero_histories": target["nonzero_histories"], "tid_Gy": dose, "observed_standard_error_Gy": error})
        require(keys == combined.keys(), f"{name}: target set mismatch")

    comparison = {}
    if baseline:
        report = inspect_scan(cli, baseline, env)
        parent = read(baseline / "project.json")
        counts = {p: sum(j["histories_per_thickness"] for j in plan["jobs"] if j["particle"] == p)
                  for p in plan["strata_settings"]["species"]}
        require(parent["provenance"]["last_loaded_input"]["sha256"] == plan["parent_project_sha256"] and
                report["runtime_identity"] == runtime and report["settings"] == settings and
                all(n == report["histories_per_case"] for n in counts.values()), "Baseline does not match source, runtime, shielding or per-species budget")
        for case in report["cases"]:
            r = read(baseline / case["directory"] / "result.json")
            require(r["project"]["seed"] not in seeds and expected_geometry[case["thickness_mm"]] == r["geometry_audit"],
                    "Baseline seed or geometry differs from comparison contract")
        comparison = {(r["thickness_mm"], r["target_id"]): r for r in report["curves"]}
        require(comparison.keys() == combined.keys(), "Baseline target set mismatch")

    rows = []
    for (t, name), components in sorted(combined.items()):
        require(len(components) == len(plan["jobs"]) and len({c["mass_kg"] for c in components}) == 1, "Incomplete strata or inconsistent target mass")
        total = math.fsum(c["tid_Gy"] for c in components)
        se = math.hypot(*(c["observed_standard_error_Gy"] for c in components))
        zero = [c["stratum"] for c in components if c["nonzero_histories"] == 0]
        hits = {p: sum(c["nonzero_histories"] for c in components if c["particle"] == p)
                for p in plan["strata_settings"]["species"]}
        row = {"thickness_mm": t, "target_id": name, "tid_Gy": total, "observed_standard_error_Gy": se,
               "tid_standard_error_Gy": se if not zero else None, "relative_standard_error": se/total if total and not zero else None,
               "zero_hit_strata": zero, "nonzero_histories_by_species": hits, "strata": components}
        if baseline:
            b = comparison[t, name]
            row["baseline"] = {"tid_Gy": b["tid_Gy"], "relative_standard_error": b["relative_standard_error"],
                               "nonzero_histories_by_species": {c["particle"]: c["nonzero_histories"] for c in b["components"]},
                               "observed_standard_error_Gy": b["tid_standard_error_Gy"]}
            if not zero and b["relative_standard_error"] is not None:
                row["difference_standard_errors"] = abs(total-b["tid_Gy"]) / math.hypot(se, b["tid_standard_error_Gy"]) if se or b["tid_standard_error_Gy"] else None
        rows.append(row)
    return {"schema": "spacerad.stratified-tid-result/1", "total_histories": plan["total_histories"],
            "runtime_identity": runtime, "rows": rows,
            "uncertainty": "Quadrature of independently seeded strata; any zero-hit stratum leaves full uncertainty unknown."}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    sub = parser.add_subparsers(dest="mode", required=True)
    run = sub.add_parser("run")
    offline = sub.add_parser("inspect")
    for command in (run, offline):
        command.add_argument("--cli", required=True, type=Path)
        command.add_argument("--output-dir", required=True, type=Path)
        command.add_argument("--baseline", type=Path)
        command.add_argument("--runtime", type=Path)
    for option in ("project", "scan-settings", "strata-settings"):
        run.add_argument(f"--{option}", required=True, type=Path)
    run.add_argument("--timeout-seconds", type=int, default=840)
    run.add_argument("--parallel", type=int, default=1)
    args = parser.parse_args()
    root, cli = args.output_dir.resolve(), args.cli.resolve()
    baseline = args.baseline.resolve() if args.baseline else None
    runtime = args.runtime.resolve() if args.runtime else None
    env = cli_environment(cli, runtime)
    if args.mode == "run":
        require(1 <= args.parallel <= 4 and 1 <= args.timeout_seconds <= 86400, "Invalid parallel or timeout limit")
        project_bytes = args.project.read_bytes()
        settings, config = read(args.scan_settings), read(args.strata_settings)
        plan = make_plan(json.loads(project_bytes), settings, config, sha(project_bytes))
        root.mkdir(parents=True)
        (root / "parent-project.json").write_bytes(project_bytes)
        (root / "scan-settings.json").write_bytes(encode(settings))
        (root / "strata-settings.json").write_bytes(encode(config))
        (root / "plan.json").write_bytes(encode(plan))
        with ThreadPoolExecutor(max_workers=args.parallel) as pool:
            list(pool.map(lambda job: cli_scan(cli, root, job, runtime, args.timeout_seconds, env), plan["jobs"]))
        result = inspect(cli, root, baseline, env)
        (root / "summary.json").write_bytes(encode(result))
        write_csv(root / "summary.csv", result["rows"])
        print(f"Accepted {len(result['rows'])} target/thickness rows; {root / 'summary.json'}")
    else:
        sys.stdout.buffer.write(encode(inspect(cli, root, baseline, env)))


if __name__ == "__main__":
    main()
