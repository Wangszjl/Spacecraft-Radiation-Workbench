"""Run/reconstruct narrow-band spacecraft energy responses with accepted Geant4 jobs."""
import argparse
from concurrent.futures import ThreadPoolExecutor
from copy import deepcopy
import csv
import hashlib
import io
import json
import math
import os
from pathlib import Path
import subprocess
import sys

ENERGIES = {"e-": [0.1, 0.5, 1, 2, 3], "proton": [1, 5, 10, 20, 50, 100]}
MEV_J = 1.602176634e-13


def read(path):
    return json.loads(path.read_bytes())


def encode(value):
    return (json.dumps(value, indent=2, sort_keys=True, allow_nan=False)+"\n").encode()


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def need(ok, message):
    if not ok:
        raise ValueError(message)


def plan_cases(base, settings, histories):
    need(base.get("source_sampling") is None and base.get("voxel_scoring") is None,
         "Choose an unweighted whole-target base project")
    need(type(histories) is int and 2 <= histories <= 100000000, "Invalid fixed history budget")
    need(settings["schema"] == "spacerad.shielding-settings/1", "Unsupported shielding settings")
    shell, = [v for v in base["volumes"] if v["id"] == settings["shell_id"]]
    need(shell.get("shape","box") == "box" and shell["wall_mm"] > 0 and not shell["sensitive"],
         "Select an explicit non-sensitive box shell")
    thicknesses = settings["thicknesses_mm"]
    need(2 <= len(thicknesses) <= 64 and all(math.isfinite(t) and t >= 0 for t in thicknesses)
         and all(a < b for a,b in zip(thicknesses,thicknesses[1:])), "Invalid thickness settings")
    cavity = [d-2*shell["wall_mm"] for d in shell["size_mm"]]
    jobs = []
    for thickness in thicknesses:
        for particle, energies in ENERGIES.items():
            for energy in energies:
                index = len(jobs)
                p = deepcopy(base)
                wall = next(v for v in p["volumes"] if v["id"] == settings["shell_id"])
                if thickness == 0:
                    p["volumes"].remove(wall)
                else:
                    wall["wall_mm"] = thickness
                    wall["size_mm"] = [d+2*thickness for d in cavity]
                low, high = energy*(1-0.001), energy*(1+0.001)
                p.update(name=f"Synthetic energy response: {particle} {energy:g} MeV, shell {thickness:g} mm",
                         events=histories, seed=1+(base["seed"]-1+40000+index*128)%2147483646, duration_s=1)
                p["environment"] = {
                    "source": "SYNTHETIC narrow energy band and unit omnidirectional fluence; response study only",
                    "synthetic": True, "quantity": "differential_fluence", "unit": "cm-2 MeV-1",
                    "angular_distribution": "isotropic", "angular_convention": "omnidirectional_scalar",
                    "metadata": {}, "spectra": [{"particle": particle, "energy_MeV": [low,high],
                        "values": [1/(high-low)]*2, "provenance": {"source_file": None,"conversions": []}}],
                }
                p["provenance"]["last_loaded_input"] = None
                p["provenance"]["energy_response"] = {"index": index, "central_energy_MeV": energy,
                    "relative_half_width": 0.001, "thickness_mm": thickness}
                jobs.append({"index": index,"directory": f"case-{index:03}","particle": particle,
                             "central_energy_MeV": energy,"thickness_mm": thickness,"project": p})
    need(len(jobs)*histories <= 100000000,"Total study history budget exceeds 100 million")
    return jobs


def cli_environment(cli, runtime):
    env = dict(os.environ)
    if runtime:
        r = read(runtime)
        dirs = [str(cli.parent),str((runtime.parent/r["worker"]).resolve().parent)]
        dirs += [str((runtime.parent/d).resolve()) for d in r.get("library_directories",[])]
        key = "PATH" if os.name == "nt" else "DYLD_LIBRARY_PATH" if sys.platform == "darwin" else "LD_LIBRARY_PATH"
        env[key] = os.pathsep.join(dirs + ([env[key]] if env.get(key) else []))
    return env


def invoke(cli, arguments, output, label, env, timeout):
    p = subprocess.run([str(cli),*map(str,arguments)],capture_output=True,encoding="utf-8",env=env,timeout=timeout)
    if output:
        (output/f"{label}.stdout.log").write_text(p.stdout,encoding="utf-8")
        (output/f"{label}.stderr.log").write_text(p.stderr,encoding="utf-8")
    events = [json.loads(line) for line in p.stdout.splitlines() if line.strip()]
    need(p.returncode == 0 and events and events[-1]["exit_code"] == 0,
         f"CLI {arguments[0]} failed ({p.returncode}): {events[-1] if events else p.stderr}")
    return events[-1]


def run_case(cli, root, job, runtime, environment):
    folder = root/job["directory"]
    arguments = ["run","--project",folder/"project.json","--output",folder/"job","--timeout-seconds","180"]
    if runtime:
        arguments += ["--runtime",runtime]
    event = invoke(cli,arguments,folder,"run",environment,240)
    need(event["type"] == "completed","Response transport did not complete")
    print(f"Completed {job['particle']} {job['central_energy_MeV']:g} MeV / {job['thickness_mm']:g} mm",flush=True)


def reconstruct(cli, root, environment):
    plan = read(root/"plan.json")
    parent, settings = read(root/"parent-project.json"), read(root/"settings.json")
    jobs = plan_cases(parent,settings,plan["histories_per_case"])
    need(plan["schema"] == "spacerad.energy-response-plan/1" and
         plan["parent_project_sha256"] == sha(root/"parent-project.json") and
         plan["settings_sha256"] == sha(root/"settings.json") and jobs == plan["cases"],
         "Energy response plan does not match physical inputs")
    rows, identity, geometry, targets = [], None, {}, None
    for case in jobs:
        folder = root/case["directory"]
        need((folder/"project.json").read_bytes() == encode(case["project"]),"Altered response input")
        event = invoke(cli,["inspect-result","--job",folder/"job"],None,"inspect",environment,90)
        need(event["type"] == "archived-result","Invalid response job archive")
        result, snap = read(folder/"job"/"result.json"), read(folder/"job"/"project.json")
        need(snap["provenance"]["last_loaded_input"]["sha256"] == sha(folder/"project.json") and
             result["project_sha256"] == sha(folder/"job"/"project.json") and
             result["histories"] == plan["histories_per_case"],"Response job snapshot or history budget differs")
        need(result["source_diagnostics"]["species_histories"] == {case["particle"]:plan["histories_per_case"]},
             "Response source species history count differs")
        source_energy=result["source_diagnostics"]["mean_primary_energy_MeV"]
        need(case["central_energy_MeV"]*(1-0.001)<=source_energy<=case["central_energy_MeV"]*(1+0.001),
             "Sampled mean source energy is outside the declared narrow band")
        expected_primaries = math.pi*(parent["source_radius_mm"]/10)**2
        need(math.isclose(result["expected_primaries"],expected_primaries,rel_tol=1e-10),
             "Unit fluence was not normalized exactly once")
        runtime = {"worker_sha256":result["worker"]["sha256"],
                   "manifest_sha256":result["runtime_audit"]["manifest_sha256"],
                   "build_context_sha256":result["runtime_audit"]["build_context_sha256"]}
        if identity is None:
            identity = runtime
        need(runtime == identity,"Worker identity changed between response cases")
        t = case["thickness_mm"]
        if t not in geometry:
            geometry[t] = result["geometry_audit"]
        need(result["geometry_audit"] == geometry[t],"Physical geometry changed within a thickness")
        ids = {r["id"] for r in result["targets"]}
        if targets is None:
            targets = ids
        need(ids == targets,"Response target set differs")
        for target in result["targets"]:
            m = result["score_moments"][target["id"]]["ionizing"]
            n = result["histories"]
            mass = target["mass_kg"]
            scale = expected_primaries*MEV_J/mass
            dose = m["mean_MeV"]*scale
            se = math.sqrt(m["m2_MeV2"]/(n*(n-1)))*scale
            need(math.isclose(dose,target["tid_Gy"],rel_tol=1e-10,abs_tol=1e-280) and
                 math.isclose(se,target["tid_standard_error_Gy"],rel_tol=1e-10,abs_tol=1e-280),
                 "Response dose or uncertainty differs from raw event moments")
            for channel,key in (("total","total_absorbed_Gy"),("nonionizing","nonionizing_Gy")):
                raw=result["score_moments"][target["id"]][channel]
                need(raw["count"]==n and math.isclose(raw["mean_MeV"]*scale,target[key],rel_tol=1e-10,abs_tol=1e-280),
                     "Response deposition channel differs from raw event moments")
            need(m["count"]==n and m["nonzero_histories"]==target["nonzero_histories"] and
                 0<=target["primary_entry_histories"]<=n,"Response target support differs from event counts")
            rows.append({"particle":case["particle"],"central_energy_MeV":case["central_energy_MeV"],
                "relative_half_width":0.001,"thickness_mm":t,"target_id":target["id"],
                "histories":n,"nonzero_histories":target["nonzero_histories"],
                "primary_entry_histories":target["primary_entry_histories"],
                "primary_entry_fraction":target["primary_entry_histories"]/n,
                "ionizing_dose_Gy_per_omni_fluence_cm2":dose,
                "total_dose_Gy_per_omni_fluence_cm2":target["total_absorbed_Gy"],
                "nonionizing_dose_Gy_per_omni_fluence_cm2":target["nonionizing_Gy"],
                "observed_standard_error_Gy_per_omni_fluence_cm2":se,
                "relative_standard_error":target["relative_standard_error"],
                "ionizing_response_Gy_per_source_primary":dose/expected_primaries,
                "status":"zero_hit" if target["nonzero_histories"]==0 else
                         "sufficient_observed" if target["nonzero_histories"]>=30 and target["relative_standard_error"]<=.2 else "insufficient",
                "result_sha256":sha(folder/"job"/"result.json")})
    return {"schema":"spacerad.energy-response-result/1","runtime_identity":identity,
            "total_histories":len(jobs)*plan["histories_per_case"],"case_count":len(jobs),"rows":rows,
            "scope":"Narrow-band whole-target response, not a task-spectrum dose; no zero-hit dose upper bound",
            "normalization":"Unit omnidirectional fluence 1 cm^-2; source expected primaries=pi*R_cm^2; zero histories included"}


def response_csv(result):
    fields=[k for k in result["rows"][0] if k!="result_sha256"]
    output=io.StringIO(newline="")
    writer=csv.DictWriter(output,fields,extrasaction="ignore")
    writer.writeheader()
    for row in result["rows"]:
        name=row["target_id"]
        if name and (name[0].isspace() or name[0] in "=+-@"):name="'"+name
        writer.writerow({**row,"target_id":name})
    return output.getvalue().encode("utf-8")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("mode",choices=("run","inspect"))
    parser.add_argument("--cli",type=Path,required=True)
    parser.add_argument("--output-dir",type=Path,required=True)
    parser.add_argument("--project",type=Path)
    parser.add_argument("--settings",type=Path)
    parser.add_argument("--runtime",type=Path)
    parser.add_argument("--histories",type=int,default=100000)
    parser.add_argument("--parallel",type=int,default=2)
    args = parser.parse_args()
    root, cli = args.output_dir.resolve(), args.cli.resolve()
    runtime = args.runtime.resolve() if args.runtime else None
    env = cli_environment(cli,runtime)
    if args.mode == "run":
        need(args.project and args.settings and not root.exists() and 1<=args.parallel<=4,
             "Provide input paths and a new output directory; parallel must be 1..4")
        parent_bytes, settings_bytes = args.project.read_bytes(), args.settings.read_bytes()
        jobs = plan_cases(json.loads(parent_bytes),json.loads(settings_bytes),args.histories)
        root.mkdir(parents=True)
        (root/"parent-project.json").write_bytes(parent_bytes)
        (root/"settings.json").write_bytes(settings_bytes)
        plan = {"schema":"spacerad.energy-response-plan/1","parent_project_sha256":sha(root/"parent-project.json"),
                "settings_sha256":sha(root/"settings.json"),"histories_per_case":args.histories,"cases":jobs}
        (root/"plan.json").write_bytes(encode(plan))
        for j in jobs:
            folder = root/j["directory"];folder.mkdir()
            (folder/"project.json").write_bytes(encode(j["project"]))
        with ThreadPoolExecutor(max_workers=args.parallel) as pool:
            list(pool.map(lambda j:run_case(cli,root,j,runtime,env),jobs))
    result = reconstruct(cli,root,env)
    if args.mode == "run":
        (root/"response.json").write_bytes(encode(result))
        (root/"response.csv").write_bytes(response_csv(result))
        print(f"Accepted {result['case_count']} response jobs and {len(result['rows'])} target/energy rows")
    else:
        need((root/"response.json").read_bytes()==encode(result),"Saved response summary differs from accepted jobs")
        need((root/"response.csv").read_bytes()==response_csv(result),"Saved response CSV differs from accepted jobs")
        print(f"Reconstructed {result['case_count']} response jobs exactly")


if __name__=="__main__":
    main()
