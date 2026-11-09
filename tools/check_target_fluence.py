"""Real Geant4 track-length normalization, statistics, and local response checks."""
import argparse
import json
import math
from pathlib import Path
import subprocess
import sys

from history_response_study import summarize_group, uncovered_bins

from run_energy_response import cli_environment


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ("cli", "runtime", "output-dir"):
        parser.add_argument(f"--{name}", type=Path, required=True)
    args = parser.parse_args()
    cli, runtime, root = (p.resolve() for p in (args.cli, args.runtime, args.output_dir))
    root.mkdir(parents=True)
    env = cli_environment(cli, runtime)
    checks = []

    def check(ok, message):
        if not ok:
            raise ValueError(message)
        checks.append(message)

    def invoke(label, *params, code=0, event=None):
        p = subprocess.run([str(cli), *map(str, params)], env=env, capture_output=True,
                           encoding="utf-8", timeout=150)
        (root / f"{label}.stdout.ndjson").write_text(p.stdout, encoding="utf-8")
        (root / f"{label}.stderr.log").write_text(p.stderr, encoding="utf-8")
        rows = [json.loads(line) for line in p.stdout.splitlines()]
        if p.returncode != code or not rows or rows[-1]["type"] != (event if code == 0 else "error"):
            raise ValueError((label, p.returncode, rows[-1:] or p.stderr))
        return rows[-1]

    def write(name, data):
        (root / name).write_text(json.dumps(data, indent=2) + "\n", encoding="utf-8")
        return root / name

    n, radius, target_radius = 30000, 30, 5
    fluence = 1e6
    volume = 4*math.pi*target_radius**3/3
    target = {"id":"Target", "material":"G4_Galactic", "shape":"sphere", "sensitive":True,
              "center_mm":[0,0,0], "size_mm":[10,10,10], "wall_mm":0, "parent":"",
              "rotation_deg":[0,0,0], "visible":True}
    project = {"schema":"scdat.radiation/6", "name":"Synthetic target fluence reference",
               "duration_s":777, "source_radius_mm":radius, "events":n, "seed":48151623,
               "physics_list":"FTFP_BERT_EMZ", "production_cut_mm":0.1,
               "units":{"energy":"MeV_per_particle","length":"mm","time":"s"},
               "provenance":{"origin_schema":"scdat.radiation/6","last_loaded_input":None,"migration_origin":None},
               "geometry_contract":"scdat.analytic-assembly/1","instance_contract":"scdat.component-instances/1",
               "assemblies":[],"templates":[],"instances":[],"volumes":[target],
               "environment":{"source":"Synthetic flat proton fluence for normalization","synthetic":True,
                              "quantity":"differential_fluence","unit":"cm-2 MeV-1",
                              "angular_distribution":"isotropic","angular_convention":"omnidirectional_scalar","metadata":{},
                              "spectra":[{"particle":"proton","energy_MeV":[10,11],"values":[fluence],
                                          "interpolation":"bin_constant","provenance":{"source_file":None,"conversions":[]}}]}}
    spec = {"schema":"spacerad.target-fluence-settings/1","target_id":"Target","energy_edges_MeV":[9,10.5,12]}
    settings = write("settings.json",spec)

    def run(label, p, score_settings=settings, history_response=None, identify=False):
        directory = root / label
        source = write(f"{label}-input.json",p)
        response_args = ["--history-response",history_response] if history_response else []
        if identify:
            response_args += ["--identify-other-species"]
        terminal = invoke(label,"run","--project",source,"--runtime",runtime,"--output",directory,
                          "--fluence-settings",score_settings,"--timeout-seconds","120",*response_args,event="completed")
        field = invoke(label+"-inspect","inspect-result","--job",directory,event="archived-result")["summary"]["target_fluence"]
        check(field == terminal["target_fluence"], label+": live and offline fluence match")
        return directory, field

    header = "device_id,metric,particle,energy_MeV,response,response_unit,angular_basis,reference\n"
    linear = root/"linear.csv"
    linear.write_text(header+"".join(
        f"Target,ddd,proton,{e},{e},MeV_cm2_per_g,isotropic_omnidirectional,Synthetic linear fixture\n"
        for e in (9,12)),encoding="utf-8")
    job, field = run("vacuum",project,history_response=linear)
    total = field["total"]
    normalization = math.pi*(radius/10)**2*fluence*100/volume
    # Impact parameter squared is uniform over the projected source disk.
    hit = (target_radius/radius)**2
    mean = hit*4*target_radius/3
    variance = hit*2*target_radius**2-mean**2
    analytic_se = math.sqrt(variance/n)*normalization
    check(abs(total["fluence_cm-2"]-fluence) <= 5*analytic_se,"Vacuum sphere recovers isotropic incident scalar fluence")
    check(abs(total["standard_error_cm-2"]/analytic_se-1)<0.12,"Event-level standard error matches analytic chord variance")
    check(total["moments"]["histories"]==n and 0<total["moments"]["nonzero_histories"]<n,
          "Total moments include zero-length histories")
    proton = field["species"]["proton"]
    check(all(abs(b["fluence_cm-2"]-fluence/2)<=5*b["standard_error_cm-2"] for b in proton[1:-1]),
          "Flat source divides evenly at the internal kinetic-energy edge")
    check(proton[0]["standard_error_cm-2"] is None and proton[-1]["standard_error_cm-2"] is None,
          "Unobserved bins retain unknown standard error")
    check(field["other_species"]["fluence_cm-2"]==0 and
          math.isclose(sum(b["fluence_cm-2"] for b in proton),total["fluence_cm-2"],rel_tol=1e-12),
          "Bins conserve all observed vacuum track length")

    for metric, unit, response in (("ddd","MeV_cm2_per_g",2),("see","cm2_per_device",1e-6)):
        table = root / f"{metric}.csv"
        table.write_text(header+"".join(f"Target,{metric},proton,{e},{response},{unit},isotropic_omnidirectional,Synthetic constant fixture\n" for e in (9,12)),encoding="utf-8")
        folded = invoke(metric,"fold-local-response","--job",job,"--response",table,event="local-response-folded")["report"]
        expected = total["fluence_cm-2"]*response
        check(math.isclose(folded["observed_folded_value"],expected,rel_tol=1e-12) and
              all(math.isclose(x,expected,rel_tol=1e-12) for x in folded["observed_energy_bin_response_range"]) and
              folded["standard_error"] is None, metric+": local constant response agrees with independent normalization")
    narrow = root/"narrow.csv"
    narrow.write_text((root/"ddd.csv").read_text().replace("proton,9,","proton,10,"),encoding="utf-8")
    invoke("missing-coverage","fold-local-response","--job",job,"--response",narrow,code=2)
    checks.append("Incomplete response bin coverage is rejected")

    folded = invoke("linear","fold-local-response","--job",job,"--response",linear,
                    event="local-response-folded")["report"]
    values = [b["fluence_cm-2"] for b in proton[1:-1]]
    bounds = spec["energy_edges_MeV"]
    expected = sum(v*(a+b)/2 for v,a,b in zip(values,bounds,bounds[1:]))
    expected_min = sum(v*a for v,a in zip(values,bounds))
    expected_max = sum(v*b for v,b in zip(values,bounds[1:]))
    check(math.isclose(folded["observed_folded_value"],expected,rel_tol=1e-12) and
          all(math.isclose(x,y,rel_tol=1e-12) for x,y in zip(
              folded["observed_energy_bin_response_range"],(expected_min,expected_max))),
          "Linear response recovers independently calculated observed bin bounds")
    history = field["history_response"]
    coeff = [(a+b)/2 for a,b in zip(bounds,bounds[1:])]
    first, second = [b["moments"] for b in proton[1:-1]]
    # In vacuum each proton stays in one energy bin for its entire history:
    # the empirical cross moment is zero, so the centered cross moment is -N*m1*m2.
    diagonal = sum(c*c*m["m2_weighted_length_mm2"] for c,m in zip(coeff,(first,second)))
    cross = -2*n*coeff[0]*coeff[1]*first["mean_weighted_length_mm"]*second["mean_weighted_length_mm"]
    expected_se = math.sqrt((diagonal+cross)/(n*(n-1)))*normalization
    check(cross<0 and history["status"]=="estimated" and
          math.isclose(history["observed_folded_value"],expected,rel_tol=1e-10) and
          math.isclose(history["standard_error"],expected_se,rel_tol=1e-10),
          "History response includes the independently reconstructed negative cross-bin covariance")

    raw = (job/"fluence-settings.json").read_bytes()
    try:
        (job/"fluence-settings.json").write_bytes(raw+b" ")
        invoke("changed-settings","inspect-result","--job",job,code=2)
        checks.append("Reformatted settings snapshot is rejected by hash")
    finally:
        (job/"fluence-settings.json").write_bytes(raw)
    result_path=job/"result.json"
    raw_result=result_path.read_bytes()
    try:
        altered=json.loads(raw_result)
        altered["target_fluence"]["total"]["standard_error_cm-2"]*=2
        result_path.write_text(json.dumps(altered),encoding="utf-8")
        invoke("changed-error","inspect-result","--job",job,code=2)
        checks.append("Changed normalized uncertainty is rejected")
    finally:
        result_path.write_bytes(raw_result)
    try:
        altered=json.loads(raw_result)
        altered["target_fluence"]["history_response"]["standard_error"]*=2
        result_path.write_text(json.dumps(altered),encoding="utf-8")
        invoke("changed-response-error","inspect-result","--job",job,code=2)
        checks.append("Archived history response uncertainty is reconstructed and verified")
    finally:
        result_path.write_bytes(raw_result)

    shield = {**target,"id":"Shell","material":"G4_Al","shape":"box","size_mm":[30,30,30],
              "wall_mm":8,"sensitive":False}
    _, attenuated = run("shielded",{**project,"name":"Synthetic aluminium shielding regression","volumes":[shield,target]})
    check(attenuated["total"]["fluence_cm-2"]<total["fluence_cm-2"]*0.2,
          "Shielded geometry reduces the observed target fluence in this fixture")

    weighted = {**project,"schema":"scdat.radiation/8","source_sampling":{
        "schema":"spacerad.source-importance/1","physical_fraction":0.5,"tail_fraction":0.2}}
    _, weighted_field = run("weighted-vacuum",weighted,history_response=root/"ddd.csv")
    weighted_total = weighted_field["total"]
    check(abs(weighted_total["fluence_cm-2"]-fluence)<=5*weighted_total["standard_error_cm-2"],
          "Importance weighting restores physical vacuum fluence")
    weighted_history=weighted_field["history_response"]
    check(math.isclose(weighted_history["observed_folded_value"],2*weighted_total["fluence_cm-2"],rel_tol=1e-10) and
          math.isclose(weighted_history["standard_error"],2*weighted_total["standard_error_cm-2"],rel_tol=1e-10),
          "Constant history response applies the importance weight once to both mean and uncertainty")

    wide = write("wide-settings.json",{**spec,"energy_edges_MeV":[1e-9,1,9,10.5,12,100]})
    table = root/"thin-shield-response.csv"
    table.write_text(header+"".join(
        f"Target,see,{species},{energy},2e-6,cm2_per_device,isotropic_omnidirectional,Synthetic constant fixture\n"
        for species in field["species"] for energy in (1e-9,100)),encoding="utf-8")
    thin_job, thin = run("thin-shield",{**project,"name":"Synthetic transmitted response regression",
        "volumes":[{**shield,"wall_mm":0.1},target]},wide,table)
    thin_total = thin["total"]
    check(thin_total["moments"]["nonzero_histories"]>50,"Thin shielding fixture yields observed transmission")
    folded = invoke("thin-shield-see","fold-local-response","--job",thin_job,"--response",table,
                    event="local-response-folded")["report"]
    check(math.isclose(folded["observed_folded_value"],2e-6*thin_total["fluence_cm-2"],rel_tol=1e-12),
          "Local response fold uses the actual post-shield target fluence")
    check(math.isclose(thin["history_response"]["standard_error"],2e-6*thin_total["standard_error_cm-2"],rel_tol=1e-10),
          "Shielded SEE response retains its full history-level variance")
    partial=root/"partial.csv"
    partial.write_text((root/"ddd.csv").read_text().replace("proton,12,","proton,10.5,"),encoding="utf-8")
    _, incomplete=run("partial-response",{**project,"events":5000},history_response=partial)
    report=incomplete["history_response"]
    check(report["status"]=="incomplete_observed_coverage" and report["uncovered_observed_fluence_cm-2"]>0 and
          report["observed_folded_value"] is None and report["standard_error"] is None,
          "Incomplete observed response coverage suppresses the total estimate and standard error")
    missing = uncovered_bins(incomplete, incomplete["settings"]["response_table"])
    check(len(missing)==1 and missing[0]["species"]=="proton" and missing[0]["lower_MeV"]==10.5 and
          missing[0]["upper_MeV"]==12 and missing[0]["nonzero_histories"]>0,
          "Coverage diagnosis identifies the observed particle and uncovered energy interval")

    secondary_project = {**project, "events":12000, "volumes":[{**target,"material":"G4_Si","size_mm":[40,40,40]}]}
    secondary_project["environment"] = {**project["environment"], "spectra":[
        {**project["environment"]["spectra"][0],"energy_MeV":[80,90]}]}
    secondary_grid = write("secondary-settings.json",{**spec,"energy_edges_MeV":[1e-9,0.01,0.1,1,10,100]})
    secondary_job, secondary = run("secondary-particles",secondary_project,secondary_grid,table,identify=True)
    detail = secondary["other_species_detail"]
    other = secondary["other_species"]
    check(secondary["schema"]=="spacerad.target-fluence/2" and len(detail)>1 and
          all(row["particle"] not in secondary["species"] and type(row["pdg_encoding"]) is int and
              row["score"]["moments"]["histories"]==12000 and row["score"]["moments"]["nonzero_histories"]>0
              for row in detail) and math.isclose(math.fsum(row["score"]["fluence_cm-2"] for row in detail),
                                                   other["fluence_cm-2"],rel_tol=1e-10),
          "Real silicon transport identifies other particles and conserves aggregated weighted fluence")
    missed = uncovered_bins(secondary, secondary["settings"]["response_table"])
    check(any(row.get("identified_bins") and len(row["identified_bins"])==len(detail) for row in missed) and
          secondary["history_response"]["status"]=="incomplete_observed_coverage" and
          secondary["history_response"]["standard_error"] is None,
          "Identification diagnoses missing response data without promoting an incomplete estimate")
    path = secondary_job/"result.json"
    raw = path.read_bytes()
    def reject_detail(label, modify):
        changed = json.loads(raw)
        modify(changed["target_fluence"]["other_species_detail"])
        try:
            path.write_text(json.dumps(changed),encoding="utf-8")
            invoke(label,"inspect-result","--job",secondary_job,code=2)
            checks.append(label)
        finally:
            path.write_bytes(raw)
    reject_detail("Archive rejects dropped secondary fluence",lambda rows: rows.pop())
    reject_detail("Archive rejects duplicate secondary energy bins",lambda rows: rows.append(rows[0]))
    reject_detail("Archive rejects changed secondary normalization",
                  lambda rows: rows[0]["score"].update({"fluence_cm-2":0}))
    reject_detail("Archive rejects out-of-range secondary identity",
                  lambda rows: rows[0].update({"pdg_encoding":2**64-1}))

    # Pooled moments must include between-seed variance and retain incomplete coverage.
    samples = ((0, 1, 3), (0, 2, 7))
    moments = [(sum(values)/len(values), sum((v-sum(values)/len(values))**2 for v in values)) for values in samples]
    rows = [{"histories":len(values), "case":str(i), "status":"estimated", "rse":0.2,
             "nonzero_response_histories":2, "response":m[0], "standard_error":math.sqrt(m[1]/6)}
            for i, (values, m) in enumerate(zip(samples, moments))]
    plan = {"min_positive":2, "max_rse":0.5}
    grouped = summarize_group(rows, moments, plan, 1)
    combined = samples[0]+samples[1]
    mean = sum(combined)/len(combined)
    error = math.sqrt(sum((v-mean)**2 for v in combined)/(len(combined)*(len(combined)-1)))
    check(math.isclose(grouped["response"],mean,rel_tol=1e-12) and
          math.isclose(grouped["standard_error"],error,rel_tol=1e-12),
          "Budget pooling reproduces direct statistics over concatenated histories")
    rows[1]["status"] = "incomplete_observed_coverage"
    grouped = summarize_group(rows, moments, plan, 1)
    check(grouped["response"] is None and grouped["standard_error"] is None and
          not grouped["passes_precision_screen"], "Budget pooling with incomplete coverage retains unknown totals")

    study = root/"seed-study"
    study_command = [sys.executable, str(Path(__file__).with_name("history_response_study.py"))]
    study_args = ["--cli",str(cli),"--runtime",str(runtime),"--output-dir",str(study)]
    def study_invoke(mode, extra=(), expected=0):
        p = subprocess.run(study_command+[mode,*study_args,*map(str,extra)],env=env,capture_output=True,
                           encoding="utf-8",timeout=120)
        if p.returncode != expected:
            raise ValueError((mode,p.returncode,p.stdout,p.stderr))
        return p.stderr
    detailed_settings = write("study-settings.json",{**spec,"schema":"spacerad.target-fluence-settings/3"})
    study_invoke("run",["--project",root/"vacuum-input.json","--fluence-settings",detailed_settings,
        "--response",linear,"--histories",8000,16000,"--seeds",71467,98689,154873,242897,"--parallel",2])
    summary_path = study/"summary.json"
    original = summary_path.read_bytes()
    result = json.loads(original)
    check([g["total_histories"] for g in result["budgets"]]==[16000,32000] and
          len({r["seed"] for r in result["cases"]})==4 and
          all(r["status"]=="estimated" for r in result["cases"]),
          "Four real Worker archives preserve both fixed budgets and distinct seeds")
    study_invoke("inspect")
    checks.append("Study verifies archived runs and reproduces its report")
    try:
        result["budgets"][0]["standard_error"] *= 2
        summary_path.write_text(json.dumps(result),encoding="utf-8")
        check("Study report differs" in study_invoke("inspect",expected=1),
              "Offline study rejects a changed pooled standard error")
    finally:
        summary_path.write_bytes(original)
    case_input = study/"case-01"/"input.json"
    original = case_input.read_bytes()
    try:
        case_input.write_bytes(original+b" ")
        check("input differs from the fixed plan" in study_invoke("inspect",expected=1),
              "Offline study rejects an altered case input")
    finally:
        case_input.write_bytes(original)
    write("summary.json",{"passed":True,"checks":checks,"histories_per_run":n,
                          "incident_cm-2":fluence,"analytic_se_cm-2":analytic_se,
                          "vacuum_cm-2":total["fluence_cm-2"],"vacuum_se_cm-2":total["standard_error_cm-2"],
                          "shielded_cm-2":attenuated["total"]["fluence_cm-2"],
                          "thin_shield_cm-2":thin_total["fluence_cm-2"],
                          "weighted_vacuum_cm-2":weighted_total["fluence_cm-2"],
                          "vacuum_response_se":history["standard_error"],
                          "independent_vacuum_response_se":expected_se,
                          "thin_shield_see_se":thin["history_response"]["standard_error"],
                          "scope":"Synthetic normalization and attenuation regression; not an independent shield-dose or device calibration benchmark"})
    print(f"Target fluence and history response acceptance: {len(checks)} checks, ten real Worker jobs")


if __name__ == "__main__":
    main()
