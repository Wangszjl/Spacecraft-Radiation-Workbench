"""Run and independently audit fixed-budget, distinct-seed device response studies."""
import argparse
from concurrent.futures import ThreadPoolExecutor
from itertools import combinations
import math
from pathlib import Path
from statistics import stdev

from run_energy_response import cli_environment, encode, invoke, need, read, sha


def plan_cases(plan):
    budgets, seeds = plan["histories_per_run"], plan["seeds"]
    need(plan["schema"] == "spacerad.history-response-study-plan/1" and
         len(budgets) == 2 and all(type(n) is int for n in budgets) and
         2 <= budgets[0] < budgets[1] <= 100_000_000, "Supply two increasing history budgets")
    need(4 <= len(seeds) <= 32 and len(seeds) % 2 == 0 and len(set(seeds)) == len(seeds) and
         all(type(seed) is int and 1 <= seed <= 2147483646 for seed in seeds),
         "Supply 2 to 16 distinct seeds per budget, without reuse between budgets")
    need(type(plan["min_positive"]) is int and 2 <= plan["min_positive"] <= budgets[0] and
         0 < plan["max_rse"] <= 1, "Invalid response support criteria")
    repeats = len(seeds)//2
    need(sum(budgets)*repeats <= 100_000_000, "Combined study budget exceeds 100 million")
    return [(f"case-{i+1:02}", budgets[i//repeats], seed) for i, seed in enumerate(seeds)]


def job_input(project, case):
    return encode(dict(project, events=case[1], seed=case[2]))


def run_case(cli, runtime, root, env, timeout, case):
    name, histories, seed = case
    folder = root/name
    event = invoke(cli, ["run", "--project", folder/"input.json", "--runtime", runtime,
                         "--fluence-settings", root/"fluence.json", "--history-response",
                         root/"response.csv", "--output", folder/"job", "--timeout-seconds", timeout],
                   folder, "run", env, timeout+30)
    need(event["type"] == "completed" and "history_response" in event["target_fluence"],
         f"{name}: no completed response")
    print(f"{name}: {histories:,} histories, seed {seed}", flush=True)



def uncovered_bins(field, table):
    edges = field["settings"]["energy_edges_MeV"]
    domains = {curve["particle"]: (curve["energy_MeV"][0], curve["energy_MeV"][-1])
               for curve in table["curves"]}
    missed = []

    def add(species, kind, low, high, row):
        n = row["moments"]["nonzero_histories"]
        if n:
            missed.append({"species": species, "kind": kind, "lower_MeV": low, "upper_MeV": high,
                           "fluence_cm-2": row["fluence_cm-2"], "nonzero_histories": n})

    for species, bins in sorted(field["species"].items()):
        add(species, "underflow", None, edges[0], bins[0])
        domain = domains.get(species)
        for i, (low, high) in enumerate(zip(edges, edges[1:]), 1):
            if domain is None or low < domain[0] or high > domain[1]:
                add(species, "energy_bin", low, high, bins[i])
        add(species, "overflow", edges[-1], None, bins[-1])
    add("other_species", "unclassified_species", None, None, field["other_species"])
    return missed


def audit_case(cli, root, env, project, settings, case):
    name, histories, seed = case
    folder = root/name
    job = folder/"job"
    need((folder/"input.json").read_bytes() == job_input(project, case),
         f"{name}: input differs from the fixed plan")
    event = invoke(cli, ["inspect-result", "--job", job], None, name, env, 45)
    need(event["type"] == "archived-result", f"{name}: archive not verified")
    summary = event["summary"]
    result = read(job/"result.json")
    p = read(job/"project.json")
    need(summary["result_sha256"] == sha(job/"result.json") and summary["project_sha256"] == sha(job/"project.json") and
         p["provenance"]["last_loaded_input"]["sha256"] == sha(folder/"input.json") and
         (p["events"], p["seed"]) == (histories, seed), f"{name}: archive differs from the planned input")
    field = summary["target_fluence"]
    saved = dict(field["settings"])
    need(saved.pop("schema") == "spacerad.target-fluence-settings/2", f"{name}: response table was not pinned")
    response_table = saved.pop("response_table")
    need(saved == {k: v for k, v in settings.items() if k != "schema"} and
         response_table["source_file"]["sha256"] == sha(root/"response.csv"),
         f"{name}: different fluence grid or response CSV")
    response = field["history_response"]
    raw = response["covered_moments"]
    value, error = response["observed_folded_value"], response["standard_error"]
    factor = summary["expected_primaries"]*100/field["volume_mm3"]
    mean, m2 = raw["mean_weighted_length_mm"], raw["m2_weighted_length_mm2"]
    raw_se = math.sqrt(m2/(histories*(histories-1)))
    need(raw["histories"] == histories and (value is None or math.isclose(value, factor*mean, rel_tol=1e-12))
         and (error is None or math.isclose(error, factor*raw_se, rel_tol=1e-12)),
         f"{name}: normalized response does not match history moments")
    missing = uncovered_bins(field, response_table)
    need(len(missing) == response["uncovered_observed_bins"] and
         math.isclose(math.fsum(b["fluence_cm-2"] for b in missing),
                      response["uncovered_observed_fluence_cm-2"], rel_tol=1e-12),
         f"{name}: uncovered bin details do not match the archive totals")
    covered_rse = raw_se/mean if mean > 0 else None
    fixed = dict(p)
    fixed.pop("seed")
    fixed.pop("events")
    fixed["provenance"] = dict(fixed["provenance"], last_loaded_input=None)
    identity = {
        "target_id": response["target_id"], "metric": response["metric"], "unit": response["result_unit"],
        "settings_sha256": field["settings_sha256"], "normalization_factor": factor,
        "engine_version": summary["engine_version"], "worker_sha256": result["worker"]["sha256"],
        "random_engine": result["random_engine"], "runtime_manifest_sha256": result["runtime_audit"]["manifest_sha256"],
    }
    row = {
        "case": name, "seed": seed, "histories": histories, "status": response["status"],
        "nonzero_response_histories": raw["nonzero_histories"],
        "uncovered_observed_bins": response["uncovered_observed_bins"],
        "uncovered_observed_fluence_cm-2": response["uncovered_observed_fluence_cm-2"],
        "uncovered_bins": missing,
        "response": value, "standard_error": error, "rse": error/value if error is not None and value else None,
        "covered_response_effective_histories": (histories/(1+(histories-1)*covered_rse**2)
                                                 if covered_rse is not None else None),
        "result_sha256": summary["result_sha256"],
    }
    return identity, fixed, row, (mean, m2)


def standardized_difference(a, b):
    combined = math.hypot(a["standard_error"], b["standard_error"])
    return abs(a["response"]-b["response"])/combined if combined > 0 else None


def summarize_group(rows, moments, plan, factor):
    n = sum(row["histories"] for row in rows)
    group = {"histories_per_run": rows[0]["histories"], "total_histories": n,
             "replicates": len(rows), "cases": [r["case"] for r in rows],
             "estimated_replicates": sum(r["status"] == "estimated" for r in rows),
             "minimum_nonzero_response_histories": min(r["nonzero_response_histories"] for r in rows),
             "response": None, "standard_error": None, "rse": None,
             "seed_sample_sd": None, "rms_within_seed_standard_error": None,
             "max_seed_standardized_difference": None, "passes_precision_screen": False}
    if any(row["status"] != "estimated" for row in rows):
        return group
    mean = math.fsum(row["histories"]*m[0] for row, m in zip(rows, moments))/n
    # Within-run variation plus between-run variation, including all zero histories.
    m2 = math.fsum(m[1]+row["histories"]*(m[0]-mean)**2 for row, m in zip(rows, moments))
    value, error = factor*mean, factor*math.sqrt(m2/(n*(n-1)))
    differences = [standardized_difference(a, b) for a, b in combinations(rows, 2)]
    max_difference = max(differences) if all(d is not None for d in differences) else None
    group.update(response=value, standard_error=error, rse=error/value,
                 seed_sample_sd=stdev(r["response"] for r in rows),
                 rms_within_seed_standard_error=math.hypot(*(r["standard_error"] for r in rows))/math.sqrt(len(rows)),
                 max_seed_standardized_difference=max_difference,
                 passes_precision_screen=(all(r["nonzero_response_histories"] >= plan["min_positive"] and
                                              r["rse"] <= plan["max_rse"] for r in rows)
                                          and max_difference is not None and max_difference <= 3))
    return group


def reconstruct(cli, root, env):
    plan = read(root/"plan.json")
    cases = plan_cases(plan)
    for name in ("parent.json", "fluence.json", "response.csv"):
        need(plan["inputs_sha256"][name] == sha(root/name), f"Study input hash mismatch: {name}")
    project, settings = read(root/"parent.json"), read(root/"fluence.json")
    need(settings["schema"] == "spacerad.target-fluence-settings/1", "Supply fluence settings without a pinned table")
    audited = [audit_case(cli, root, env, project, settings, case) for case in cases]
    identity, fixed = audited[0][:2]
    need(all(a[:2] == (identity, fixed) for a in audited),
         "Archived cases disagree on physical project, scoring or transport runtime identity")
    rows = [a[2] for a in audited]
    split = len(rows)//2
    moments = [a[3] for a in audited]
    groups = [summarize_group(rows[s], moments[s], plan, identity["normalization_factor"])
              for s in (slice(None, split), slice(split, None))]
    shift = standardized_difference(*groups) if all(g["standard_error"] is not None for g in groups) else None
    return {
        "schema": "spacerad.history-response-study/1", "identity": identity, "plan_sha256": sha(root/"plan.json"),
        "cases": rows, "budgets": groups, "between_budget_standardized_difference": shift,
        "passes_high_budget_screen": bool(groups[1]["passes_precision_screen"] and shift is not None and shift <= 3),
        "scope": "Descriptive Monte Carlo screen for this binned response only. Distinct seeds are treated as independent. "
                 "This study gives no bounds for unobserved contributions or for environment, transport or calibration systematics.",
    }


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    modes = parser.add_subparsers(dest="mode", required=True)
    for mode in ("run", "inspect"):
        cmd = modes.add_parser(mode)
        cmd.add_argument("--cli", type=Path, required=True)
        cmd.add_argument("--output-dir", type=Path, required=True)
        cmd.add_argument("--runtime", type=Path, required=(mode == "run"))
        if mode == "run":
            for field in ("project", "fluence-settings", "response"):
                cmd.add_argument(f"--{field}", type=Path, required=True)
            cmd.add_argument("--histories", type=int, nargs=2, required=True)
            cmd.add_argument("--seeds", type=int, nargs="+", required=True)
            cmd.add_argument("--min-positive", type=int, default=20)
            cmd.add_argument("--max-rse", type=float, default=0.10)
            cmd.add_argument("--parallel", type=int, choices=range(1, 9), default=1)
            cmd.add_argument("--timeout", type=int, default=900, metavar="SECONDS")
    args = parser.parse_args()
    cli, root = args.cli.resolve(), args.output_dir.resolve()
    runtime = args.runtime.resolve() if args.runtime else None
    env = cli_environment(cli, runtime)
    if args.mode == "run":
        need(30 <= args.timeout <= 86400, "Study timeout must be 30 to 86400 seconds per run")
        plan = {"schema": "spacerad.history-response-study-plan/1", "histories_per_run": args.histories,
                "seeds": args.seeds, "min_positive": args.min_positive, "max_rse": args.max_rse}
        cases = plan_cases(plan)
        inputs = {"parent.json": args.project.read_bytes(), "fluence.json": args.fluence_settings.read_bytes(),
                  "response.csv": args.response.read_bytes()}
        need(read(args.fluence_settings)["schema"] == "spacerad.target-fluence-settings/1",
             "Supply fluence settings without a pinned table")
        root.mkdir(parents=True)
        for name, data in inputs.items():
            (root/name).write_bytes(data)
        plan["inputs_sha256"] = {name: sha(root/name) for name in inputs}
        (root/"plan.json").write_bytes(encode(plan))
        project = read(root/"parent.json")
        for case in cases:
            folder = root/case[0]
            folder.mkdir()
            (folder/"input.json").write_bytes(job_input(project, case))
        with ThreadPoolExecutor(max_workers=args.parallel) as pool:
            futures = [pool.submit(run_case, cli, runtime, root, env, args.timeout, case) for case in cases]
            for future in futures:
                future.result()
    report = encode(reconstruct(cli, root, env))
    summary = root/"summary.json"
    if args.mode == "run":
        summary.write_bytes(report)
    else:
        need(summary.read_bytes() == report, "Study report differs from verified reconstruction")
    print(f"Verified response study: {summary}")


if __name__ == "__main__":
    try:
        main()
    except (ValueError, OSError, KeyError) as error:
        raise SystemExit(str(error)) from error
