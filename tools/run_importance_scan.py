"""Run auditable fixed-inner-cavity scans with a mixture of weighted species."""
import argparse
from copy import deepcopy
import hashlib
import json
import math
import os
from pathlib import Path
import subprocess
import sys


def raw_json(path):
    return json.loads(path.read_bytes())


def encoded(value):
    return (json.dumps(value, indent=2, allow_nan=False) + "\n").encode("utf-8")


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def spectrum_integral(s):
    e,v=s["energy_MeV"],s["values"]
    if s.get("interpolation")=="bin_constant":
        return math.fsum((b-a)*y for a,b,y in zip(e,e[1:],v))
    return math.fsum((b-a)*(x+y)/2 for a,b,x,y in zip(e,e[1:],v,v[1:]))


def require(condition, reason):
    if not condition:
        raise ValueError(reason)


def run(cli, arguments, stdout, stderr, timeout, environment):
    proc = subprocess.run([str(cli), *map(str, arguments)], capture_output=True,
                          text=True, encoding="utf-8", timeout=timeout, env=environment)
    stdout.write_text(proc.stdout, encoding="utf-8")
    stderr.write_text(proc.stderr, encoding="utf-8")
    events = [json.loads(line) for line in proc.stdout.splitlines() if line.strip()]
    require(bool(events), f"No CLI event from {arguments[0]}")
    return proc.returncode, events[-1]


def verify_scan(cli, root):
    """Revalidate original inputs, every CLI archive and the saved aggregation."""
    base_bytes=(root/"parent-project.json").read_bytes()
    base=json.loads(base_bytes)
    settings=raw_json(root/"scan-settings.json")
    plan=raw_json(root/"plan.json")
    summary=raw_json(root/"scan-result.json")
    require(plan["schema"]=="spacerad.importance-thickness-plan/1" and
            plan["parent_project_sha256"]==hashlib.sha256(base_bytes).hexdigest() and
            plan["scan_settings"]==settings and summary["scan_settings"]==settings and
            summary["status"]=="completed" and summary["plan_sha256"]==sha(root/"plan.json"),
            "Weighted scan plan or source snapshots differ")
    shell,=[v for v in base["volumes"] if v["id"]==settings["shell_id"]]
    cavity=[d-2*shell["wall_mm"] for d in shell["size_mm"]]
    histories=plan["histories_per_thickness"]
    thicknesses=settings["thicknesses_mm"]
    require(histories==summary["histories_per_thickness"] and
            plan["total_histories"]==summary["total_histories"]==histories*len(thicknesses) and
            len(plan["cases"])==len(summary["cases"])==len(thicknesses),
            "Weighted scan case coverage or budget differs")
    for i,(planned,accepted,t) in enumerate(zip(plan["cases"],summary["cases"],thicknesses)):
        expected=deepcopy(base)
        v=next(v for v in expected["volumes"] if v["id"]==settings["shell_id"])
        if t==0: expected["volumes"].remove(v)
        else:
            v["wall_mm"]=t
            v["size_mm"]=[d+2*t for d in cavity]
        expected["events"]=histories
        expected["seed"]=1+(base["seed"]-1+(i+1)*104729+7919)%2147483646
        expected["provenance"]["last_loaded_input"]=None
        expected["provenance"]["importance_thickness_scan"]={
            "index":i,"shell_id":settings["shell_id"],"thickness_mm":t,"fixed_inner_cavity_mm":cavity}
        name=f"case-{i:03}"
        digest=hashlib.sha256(encoded(expected)).hexdigest()
        require(planned=={"index":i,"thickness_mm":t,"directory":name,"project_sha256":digest,"project":expected}
                and accepted["index"]==i and accepted["thickness_mm"]==t and accepted["directory"]==name and
                accepted["project_sha256"]==digest and sha(root/name/"project.json")==digest,
                "Weighted thickness plan differs from fixed-cavity reconstruction")
        directory=root/name
        job=directory/"job"
        proc=subprocess.run([str(cli),"inspect-result","--job",str(job)],capture_output=True,
                            text=True,encoding="utf-8",timeout=90)
        events=[json.loads(line) for line in proc.stdout.splitlines() if line.strip()]
        require(proc.returncode==0 and events and events[-1]["type"]=="archived-result" and events[-1]["exit_code"]==0,
                f"Weighted child archive invalid: {name}")
        result=raw_json(job/"result.json")
        require(sha(job/"result.json")==accepted["result_sha256"] and
                raw_json(job/"project.json")["provenance"]["last_loaded_input"]["sha256"]==digest and
                result["project_sha256"]==sha(job/"project.json") and
                accepted["source_importance"]==result["source_importance"] and
                accepted["targets"]==result["targets"] and
                accepted["geometry_audit"]==result["geometry_audit"] and
                accepted["expected_primaries"]==result["expected_primaries"] and
                result["histories"]==histories and accepted==raw_json(directory/"accepted.json"),
                "Weighted child and saved aggregation differ")
        identity={"worker_sha256":result["worker"]["sha256"],
                  "manifest_sha256":result["runtime_audit"]["manifest_sha256"],
                  "build_context_sha256":result["runtime_audit"]["build_context_sha256"]}
        require(identity==accepted["runtime_identity"]==summary["runtime_identity"],
                "Weighted scan runtime identities differ")
    return summary


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for option in ("cli", "project", "scan-settings", "output-dir"):
        parser.add_argument(f"--{option}", required=True, type=Path)
    parser.add_argument("--runtime", type=Path)
    parser.add_argument("--histories-per-thickness", type=int, required=True)
    parser.add_argument("--timeout-seconds", type=int, default=840)
    args = parser.parse_args()
    cli, output = args.cli.resolve(), args.output_dir.resolve()
    require(not output.exists(), "Choose a new output directory")
    require(2 <= args.histories_per_thickness <= 100_000_000 and 1 <= args.timeout_seconds <= 86400,
            "Invalid fixed history budget or timeout")
    project_bytes, settings_bytes = args.project.read_bytes(), args.scan_settings.read_bytes()
    base, settings = json.loads(project_bytes), json.loads(settings_bytes)
    require(base.get("schema") == "scdat.radiation/8" and base.get("source_sampling") and
            base.get("voxel_scoring") is None, "Require a weighted schema-v8 whole-target project")
    spectra = {s["particle"]: s for s in base["environment"]["spectra"]}
    require(set(spectra) in ({"e-"}, {"proton"}, {"e-", "proton"}) and len(spectra)==len(base["environment"]["spectra"]),
            "Weighted spacecraft runs support one spectrum per electron/proton species")
    config = settings["thicknesses_mm"]
    require(settings.get("schema")=="spacerad.shielding-settings/1" and 2<=len(config)<=64 and
            all(math.isfinite(t) and t>=0 for t in config) and all(a<b for a,b in zip(config,config[1:])),
            "Invalid shielding scan settings")
    volumes = base["volumes"]
    shell, = [v for v in volumes if v["id"]==settings["shell_id"]]
    assembly_ids={a["id"] for a in base.get("assemblies",[])}
    require(shell["shape"]=="box" and shell["wall_mm"]>0 and not shell["sensitive"] and
            (not shell["parent"] or shell["parent"] in assembly_ids) and
            all(d>2*shell["wall_mm"] for d in shell["size_mm"]),
            "Importance thickness scan requires an explicitly declared hollow box volume")
    cavity=[d-2*shell["wall_mm"] for d in shell["size_mm"]]
    runtime=args.runtime.resolve() if args.runtime else None
    environment=dict(os.environ)
    if runtime:
        runtime_data=raw_json(runtime)
        directories=[str(cli.parent),str((runtime.parent/runtime_data["worker"]).resolve().parent)]
        directories.extend(str((runtime.parent/d).resolve()) for d in runtime_data.get("library_directories",[]))
        variable="PATH" if os.name=="nt" else "DYLD_LIBRARY_PATH" if sys.platform=="darwin" else "LD_LIBRARY_PATH"
        environment[variable]=os.pathsep.join(directories+([environment[variable]] if environment.get(variable) else []))
    output.mkdir(parents=True)
    (output/"parent-project.json").write_bytes(project_bytes)
    (output/"scan-settings.json").write_bytes(settings_bytes)
    plan={"schema":"spacerad.importance-thickness-plan/1","parent_project_sha256":hashlib.sha256(project_bytes).hexdigest(),
          "scan_settings":settings,"total_histories":args.histories_per_thickness*len(config),
          "histories_per_thickness":args.histories_per_thickness,"cases":[]}
    for i,thickness in enumerate(config):
        project=deepcopy(base)
        volume=next(v for v in project["volumes"] if v["id"]==settings["shell_id"])
        if thickness==0:
            project["volumes"].remove(volume)
        else:
            volume["wall_mm"]=thickness
            volume["size_mm"]=[inner+2*thickness for inner in cavity]
        project["events"]=args.histories_per_thickness
        project["seed"]=1+(base["seed"]-1+(i+1)*104729+7919) % 2147483646
        project["provenance"]["last_loaded_input"]=None
        project["provenance"]["importance_thickness_scan"]={"index":i,"shell_id":settings["shell_id"],
            "thickness_mm":thickness,"fixed_inner_cavity_mm":cavity}
        entry={"index":i,"thickness_mm":thickness,"directory":f"case-{i:03}",
               "project_sha256":hashlib.sha256(encoded(project)).hexdigest(),"project":project}
        case=output/entry["directory"]
        case.mkdir()
        (case/"project.json").write_bytes(encoded(project))
        (case/"plan.json").write_bytes(encoded(entry))
        plan["cases"].append(entry)
        print(f"Prepared thickness {thickness:g} mm", flush=True)
    (output/"plan.json").write_bytes(encoded(plan))
    reports=[]
    for case in plan["cases"]:
        directory=output/case["directory"]
        command=["run","--project",str(directory/"project.json"),"--output",str(directory/"job"),
                 "--timeout-seconds",str(args.timeout_seconds)]
        if runtime:
            command += ["--runtime",str(runtime)]
        code,event=run(cli,command, directory/"run.stdout.log",directory/"run.stderr.log",args.timeout_seconds+60,environment)
        require(code==0 and event["type"]=="completed" and event["exit_code"]==0,
                f"Thickness {case['thickness_mm']:g} mm transport not accepted")
        input_sha=sha(directory/"project.json")
        require(input_sha==case["project_sha256"],"Saved stratum input differs from fixed scan plan")
        code,archived=run(cli,["inspect-result","--job",str(directory/"job")],
                          directory/"inspect.stdout.log",directory/"inspect.stderr.log",90,environment)
        require(code==0 and archived["type"]=="archived-result" and archived["exit_code"]==0,
                "Weighted result failed offline archive acceptance")
        result=raw_json(directory/"job"/"result.json")
        job_project=raw_json(directory/"job"/"project.json")
        exposure=math.pi*(case["project"]["source_radius_mm"]/10)**2*sum(spectrum_integral(s) for s in case["project"]["environment"]["spectra"])
        if case["project"]["environment"]["quantity"]=="differential_flux":
            exposure*=case["project"]["duration_s"]
        require(result["histories"]==args.histories_per_thickness and
                math.isclose(result["expected_primaries"],exposure,rel_tol=1e-12) and
                result["source_importance"]["configuration"]==base["source_sampling"] and
                job_project["provenance"]["last_loaded_input"]["sha256"]==case["project_sha256"] and
                sha(directory/"job"/"project.json")==result["project_sha256"],
                "Weighted job exposure, histories, input identity or source proposal differ")
        target_ids={t["id"] for t in result["targets"]}
        require(target_ids=={v["id"] for v in result["geometry_audit"]["volumes"] if v["sensitive"]},
                "Target set differs from geometry audit")
        report={"index":case["index"],"thickness_mm":case["thickness_mm"],"directory":case["directory"],
                "result_sha256":sha(directory/"job"/"result.json"),"project_sha256":case["project_sha256"],
                "expected_primaries":result["expected_primaries"],"targets":result["targets"],
                "source_importance":result["source_importance"],
                "runtime_identity":{"worker_sha256":result["worker"]["sha256"],
                    "manifest_sha256":result["runtime_audit"]["manifest_sha256"],
                    "build_context_sha256":result["runtime_audit"]["build_context_sha256"]},
                "geometry_audit":result["geometry_audit"]}
        (directory/"accepted.json").write_bytes(encoded(report))
        reports.append(report)
        print(f"Accepted thickness {case['thickness_mm']:g} mm",flush=True)
    runtime_ids={json.dumps(r["runtime_identity"],sort_keys=True) for r in reports}
    require(len(runtime_ids)==1,"Worker/runtime identities changed during paired scan")
    summary={"schema":"spacerad.importance-thickness-result/1","status":"completed",
        "plan_sha256":sha(output/"plan.json"),"runtime_identity":reports[0]["runtime_identity"],
        "histories_per_thickness":args.histories_per_thickness,"total_histories":plan["total_histories"],
        "scan_settings":settings,"cases":reports}
    (output/"scan-result.json").write_bytes(encoded(summary))
    print(f"Importance scan completed: {plan['total_histories']} histories",flush=True)


if __name__=="__main__":
    main()
