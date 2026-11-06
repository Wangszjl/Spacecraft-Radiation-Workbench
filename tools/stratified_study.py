"""Run and audit full-spectrum partition/seed stability without interpolating responses."""
import argparse
from copy import deepcopy
import csv
from html import escape
import io
from itertools import combinations
import math
from pathlib import Path
import subprocess
import sys

from run_energy_response import cli_environment
from stratified_tid import encode, inspect, make_plan, read, require, sha, sha_file, summary_csv


def make_study_plan(project, scan, config, parent_sha):
    require(set(config) == {"schema", "replicas", "partitions", "screen"}
            and config["schema"] == "spacerad.partition-study-settings/1", "Invalid study settings")
    require(type(config["replicas"]) is int and 2 <= config["replicas"] <= 4,
            "Use two to four independently seeded replicas")
    partitions = config["partitions"]
    require(set(partitions) == {"coarse", "fine"}, "Provide coarse and fine exact partitions")
    screen = config["screen"]
    require(set(screen) == {"minimum_nonzero_histories", "maximum_rse", "family_confidence"}
            and type(screen["minimum_nonzero_histories"]) is int and screen["minimum_nonzero_histories"] >= 2
            and type(screen["maximum_rse"]) in (int, float) and 0 < screen["maximum_rse"] < 1
            and type(screen["family_confidence"]) in (int, float) and 0 < screen["family_confidence"] < 1,
            "Invalid descriptive support screen")
    children = []
    budgets = None
    for partition in ("coarse", "fine"):
        for replica in range(config["replicas"]):
            index = len(children)
            child = deepcopy(project)
            child["seed"] = 1 + (project["seed"]-1 + 200000 + index*100000) % 2147483646
            source = encode(child)
            plan = make_plan(child, scan, partitions[partition], sha(source))
            allocation = {p: sum(j["histories_per_thickness"] for j in plan["jobs"] if j["particle"] == p)
                          for p in partitions[partition]["species"]}
            budgets = allocation if budgets is None else budgets
            require(allocation == budgets, "Match per-species history budgets across both partitions")
            children.append({"id": f"{partition}-{replica+1}", "partition": partition,
                             "replica": replica+1, "project": child,
                             "input_sha256": sha(source), "plan": plan})
    for p, coarse in partitions["coarse"]["species"].items():
        fine = partitions["fine"]["species"][p]
        require(set(coarse["split_MeV"]) < set(fine["split_MeV"]),
                "Fine partition must strictly refine the coarse cuts for each species")
    total = sum(c["plan"]["total_histories"] for c in children)
    require(total <= 100000000, "Total study budget exceeds 100 million histories")
    return {"schema": "spacerad.partition-study-plan/1", "parent_project_sha256": parent_sha,
            "scan_settings": scan, "study_settings": config, "total_histories": total, "studies": children}


def zero_probability_upper(histories, cells, confidence):
    """One-sided zero-success binomial limit with a Bonferroni family allocation."""
    require(type(histories) is int and histories >= 2 and type(cells) is int and cells >= 1
            and 0 < confidence < 1, "Invalid zero-event probability bound")
    return -math.expm1((math.log1p(-confidence)-math.log(cells))/histories)


def review_component(component, total_dose, variance, screen, cells):
    n, hits = component["histories"], component["nonzero_histories"]
    dose, se = component["tid_Gy"], component["observed_standard_error_Gy"]
    require(type(n) is int and n >= 2 and type(hits) is int and 0 <= hits <= n
            and math.isfinite(dose) and dose >= 0 and math.isfinite(se) and se >= 0
            and ((hits == 0 and dose == 0 and se == 0) or (hits > 0 and dose > 0)),
            "Inconsistent stratum moments or deposition support")
    rse = se/dose if dose else None
    ess = n / (1+(n-1)*(se/dose)**2) if dose else 0.0
    require(ess <= hits*(1+1e-10), "Deposition moment ESS exceeds nonzero history count")
    return {**component, "relative_standard_error": rse,
            "effective_deposition_histories": ess,
            "observed_dose_fraction": dose/total_dose if total_dose else None,
            "observed_variance_fraction": se*se/variance if variance else None,
            "support_screen_passed": hits >= screen["minimum_nonzero_histories"]
                and rse is not None and rse <= screen["maximum_rse"],
            "zero_event_probability_upper": zero_probability_upper(n, cells, screen["family_confidence"])
                if hits == 0 else None,
            "dose_upper_bound_Gy": None}


def summarize(plan, reports):
    studies = plan["studies"]
    require(set(reports) == {s["id"] for s in studies}, "Missing or duplicate study reports")
    mappings = {name: {(r["thickness_mm"], r["target_id"]): r for r in report["rows"]}
                for name, report in reports.items()}
    keys = set(next(iter(mappings.values())))
    require(keys and all(set(rows) == keys and len(rows) == len(reports[name]["rows"])
                         for name, rows in mappings.items()), "Study target/thickness set differs")
    require(all(reports[s["id"]]["total_histories"] == s["plan"]["total_histories"] for s in studies),
            "Study report budget differs")
    cells = sum(len(row["strata"]) for report in reports.values() for row in report["rows"])
    screen = plan["study_settings"]["screen"]
    rows = []
    for key in sorted(keys):
        estimates = []
        for study in studies:
            row = mappings[study["id"]][key]
            components = row["strata"]
            require(len(components) == len(study["plan"]["jobs"]), "Incomplete energy partition")
            total = math.fsum(c["tid_Gy"] for c in components)
            variance = math.fsum(c["observed_standard_error_Gy"]**2 for c in components)
            parts = [review_component(c, total, variance, screen, cells) for c in components]
            rse = math.sqrt(variance)/total if total and all(c["nonzero_histories"] for c in parts) else None
            estimates.append({"study": study["id"], "partition": study["partition"],
                "replica": study["replica"], "tid_Gy": total,
                "observed_standard_error_Gy": math.sqrt(variance), "relative_standard_error": rse,
                "support_screen_passed": all(c["support_screen_passed"] for c in parts)
                    and rse is not None and rse <= screen["maximum_rse"],
                "species_dose_Gy": {p: math.fsum(c["tid_Gy"] for c in parts if c["particle"] == p)
                                    for p in study["plan"]["strata_settings"]["species"]},
                "components": parts})
        masses = {c["mass_kg"] for e in estimates for c in e["components"]}
        require(len(masses) == 1, "Target mass differs across partitions or replicas")
        pairs = []
        for a, b in combinations(estimates, 2):
            eligible = a["support_screen_passed"] and b["support_screen_passed"]
            error = math.hypot(a["observed_standard_error_Gy"], b["observed_standard_error_Gy"])
            pairs.append({"a": a["study"], "b": b["study"],
                "kind": "seed_repeat" if a["partition"] == b["partition"] else "partition_and_seed",
                "difference_Gy": b["tid_Gy"]-a["tid_Gy"],
                "eligible_for_empirical_comparison": eligible,
                "difference_in_empirical_standard_errors": abs(b["tid_Gy"]-a["tid_Gy"])/error
                    if eligible and error else None})
        all_positive = all(c["nonzero_histories"] for e in estimates for c in e["components"])
        mean_dose = math.fsum(e["tid_Gy"] for e in estimates)/len(estimates)
        mean_se = math.hypot(*(e["observed_standard_error_Gy"] for e in estimates))/len(estimates)
        rows.append({"thickness_mm": key[0], "target_id": key[1], "mass_kg": next(iter(masses)),
            "mean_estimate_Gy": mean_dose, "mean_observed_standard_error_Gy": mean_se,
            "mean_standard_error_Gy": mean_se if all_positive else None,
            "mean_relative_standard_error": mean_se/mean_dose if all_positive and mean_dose else None,
            "support_screen_passed": all(e["support_screen_passed"] for e in estimates),
            "estimates": estimates, "comparisons": pairs})
    return {"schema": "spacerad.partition-study-result/1", "total_histories": plan["total_histories"],
        "screen": screen, "probability_bound_family_cells": cells, "rows": rows,
        "uncertainty": "Empirical variances assume independent histories and independently seeded runs. Distinct seeds do not prove stream disjointness. Zero-event bounds cover event probability, never deposited-energy severity or dose. Support screens and pairwise shifts are descriptive, not physical qualification or a formal convergence test.",
        "folding": "Each stratum retains the original spectrum shape and its exact exposure. No monoenergetic response interpolation or energy-grid approximation is used. Partition changes test sampling stability, not deterministic discretization error."}


def reconstruct(cli, root, env):
    source = root/"parent-project.json"
    plan = make_study_plan(read(source), read(root/"scan-settings.json"), read(root/"study-settings.json"), sha_file(source))
    require(encode(plan) == (root/"plan.json").read_bytes(), "Stored study plan differs from input reconstruction")
    reports, identity, geometry, seeds, archive_hashes = {}, None, {}, set(), []
    for study in plan["studies"]:
        name, directory = study["id"], root/study["id"]
        require((root/f"{name}.json").read_bytes() == encode(study["project"])
                and (directory/"parent-project.json").read_bytes() == encode(study["project"]),
                f"{name}: altered parent source")
        report = inspect(cli, directory, env=env)
        require(encode(report) == (directory/"summary.json").read_bytes()
                and summary_csv(report["rows"]) == (directory/"summary.csv").read_bytes(),
                f"{name}: saved summary JSON or CSV differs from archive reconstruction")
        require((directory/"plan.json").read_bytes() == encode(study["plan"]), f"{name}: unexpected partition plan")
        identity = report["runtime_identity"] if identity is None else identity
        require(identity == report["runtime_identity"], "Worker/runtime changed across studies")
        for job in study["plan"]["jobs"]:
            scan_root = directory/job["id"]
            scan = read(scan_root/"scan-result.json")
            for case in scan["cases"]:
                result_path = scan_root/case["directory"]/"result.json"
                result = read(result_path)
                seed, thickness = result["project"]["seed"], case["thickness_mm"]
                require(seed not in seeds, "Repeated child seed across partitions or replicas")
                seeds.add(seed)
                require(geometry.setdefault(thickness, result["geometry_audit"]) == result["geometry_audit"],
                        "Physical geometry differs across studies")
                archive_hashes.append({"path": result_path.relative_to(root).as_posix(), "sha256": sha_file(result_path)})
        reports[name] = report
    result = summarize(plan, reports)
    result.update(runtime_identity=identity, parent_project_sha256=plan["parent_project_sha256"],
                  unique_child_seeds=len(seeds), archive_results=archive_hashes)
    return result


def study_csv(result):
    f = io.StringIO(newline="")
    fields = ["thickness_mm", "target_id", "study", "tid_Gy", "observed_standard_error_Gy",
              "relative_standard_error", "support_screen_passed", "zero_hit_strata"]
    writer = csv.DictWriter(f, fields)
    writer.writeheader()
    for row in result["rows"]:
        name = row["target_id"]
        if name and (name[0].isspace() or name[0] in "=+-@"):
            name = "'"+name
        for estimate in row["estimates"]:
            writer.writerow({k: {**row, **estimate, "target_id": name,
                "zero_hit_strata": ";".join(c["stratum"] for c in estimate["components"] if c["nonzero_histories"] == 0)}[k]
                for k in fields})
    return f.getvalue().encode("utf-8")


def study_html(result):
    def fmt(x):
        return "未知" if x is None else f"{x:.5g}"
    rows, details = [], []
    for row in result["rows"]:
        for estimate in row["estimates"]:
            zero = sum(c["nonzero_histories"] == 0 for c in estimate["components"])
            values = [row["target_id"], fmt(row["thickness_mm"]), estimate["study"], fmt(estimate["tid_Gy"]),
                fmt(estimate["relative_standard_error"]), str(zero), "通过经验筛查" if estimate["support_screen_passed"] else "统计支持不足"]
            rows.append("<tr>"+"".join(f"<td>{escape(v)}</td>" for v in values)+"</tr>")
            part_rows = []
            for c in estimate["components"]:
                values = [c["particle"], "–".join(fmt(v) for v in c["range_MeV"]), str(c["histories"]),
                    str(c["nonzero_histories"]), fmt(c["tid_Gy"]), fmt(c["relative_standard_error"]),
                    fmt(c["effective_deposition_histories"]), fmt(c["zero_event_probability_upper"])]
                part_rows.append("<tr>"+"".join(f"<td>{escape(v)}</td>" for v in values)+"</tr>")
            details.append(f"<details><summary>{escape(row['target_id'])} / {row['thickness_mm']:g} mm / {escape(estimate['study'])}</summary><table><tr><th>粒子</th><th>MeV</th><th>历史</th><th>非零沉积</th><th>Gy</th><th>经验 RSE</th><th>沉积 ESS</th><th>零事件概率上限</th></tr>{''.join(part_rows)}</table></details>")
    return ("<!doctype html><html lang='zh-CN'><meta charset='utf-8'><title>SpaceRad 全任务谱统计复核</title>"
        "<style>body{font:15px system-ui;margin:32px;color:#233045}table{border-collapse:collapse;width:100%;margin:14px 0}th,td{padding:8px;border-bottom:1px solid #ddd;text-align:left}th{background:#eef3f7}summary{cursor:pointer;padding:10px}p{max-width:1100px;line-height:1.6}</style>"
        f"<h1>全任务谱分层与独立种子复核</h1><p>总历史数：{result['total_histories']:,}。各能段保留原谱形与物理暴露；未使用窄能点插值。改变分层检验抽样稳定性，不是确定性离散误差收敛。</p>"
        "<p>RSE 为经验相对标准误。任一能段零沉积时，全谱 RSE 保留未知。零事件概率上限只约束该段每次历史出现非零沉积的概率，无法约束单次沉积严重程度，因此不能换算为剂量上限。经验筛查不等于独立物理验证或工程验收。</p>"
        f"<p>同时概率覆盖率：{result['screen']['family_confidence']:.1%}，Bonferroni 分配到预先固定的 {result['probability_bound_family_cells']} 个靶区／能段单元。独立历史与独立运行是统计假设，种子不同不是随机数流不相交的证明。RSE 显示为比例。</p>"
        "<table><tr><th>靶区</th><th>壁厚 mm</th><th>分层／重复</th><th>总剂量 Gy</th><th>全谱 RSE</th><th>零命中能段</th><th>统计支持</th></tr>"
        +"".join(rows)+"</table><h2>逐能段原始支持</h2>"+"".join(details)+"</html>\n").encode("utf-8")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("mode", choices=("run", "inspect"))
    for name in ("cli", "output-dir"):
        parser.add_argument(f"--{name}", type=Path, required=True)
    for name in ("project", "scan-settings", "study-settings", "runtime"):
        parser.add_argument(f"--{name}", type=Path)
    parser.add_argument("--parallel", type=int, default=3)
    parser.add_argument("--timeout-seconds", type=int, default=840)
    args = parser.parse_args()
    cli, root = args.cli.resolve(), args.output_dir.resolve()
    runtime = args.runtime.resolve() if args.runtime else None
    env = cli_environment(cli, runtime)
    if args.mode == "run":
        require(args.project and args.scan_settings and args.study_settings, "Supply project, scan and study settings")
        require(1 <= args.parallel <= 4 and 1 <= args.timeout_seconds <= 86400, "Invalid execution limits")
        source = args.project.read_bytes()
        scan, config = read(args.scan_settings), read(args.study_settings)
        plan = make_study_plan(read(args.project), scan, config, sha(source))
        root.mkdir(parents=True)
        (root/"parent-project.json").write_bytes(source)
        (root/"scan-settings.json").write_bytes(encode(scan))
        (root/"study-settings.json").write_bytes(encode(config))
        (root/"plan.json").write_bytes(encode(plan))
        for study in plan["studies"]:
            name = study["id"]
            (root/f"{name}.json").write_bytes(encode(study["project"]))
            (root/f"{name}-strata.json").write_bytes(encode(study["plan"]["strata_settings"]))
            command = [sys.executable, str(Path(__file__).with_name("stratified_tid.py")), "run",
                "--cli", str(cli), "--project", str(root/f"{name}.json"), "--scan-settings", str(root/"scan-settings.json"),
                "--strata-settings", str(root/f"{name}-strata.json"), "--output-dir", str(root/name),
                "--parallel", str(args.parallel), "--timeout-seconds", str(args.timeout_seconds)]
            if runtime:
                command += ["--runtime", str(runtime)]
            with (root/f"{name}.stdout.log").open("wb") as out, (root/f"{name}.stderr.log").open("wb") as err:
                proc = subprocess.run(command, env=env, stdout=out, stderr=err,
                                      timeout=(args.timeout_seconds+90)*len(study["plan"]["jobs"])+120)
            require(proc.returncode == 0, f"{name}: failed; inspect logs")
            print(f"Accepted {name}", flush=True)
        result = reconstruct(cli, root, env)
        (root/"summary.json").write_bytes(encode(result))
        (root/"summary.csv").write_bytes(study_csv(result))
        (root/"review.html").write_bytes(study_html(result))
    else:
        result = reconstruct(cli, root, env)
        require((root/"summary.json").read_bytes() == encode(result)
                and (root/"summary.csv").read_bytes() == study_csv(result)
                and (root/"review.html").read_bytes() == study_html(result),
                "Saved study JSON, CSV or HTML differs from archive reconstruction")
    print(f"Accepted {len(result['rows'])} target/thickness rows, {result['unique_child_seeds']} real jobs; {root}")


if __name__ == "__main__":
    main()
