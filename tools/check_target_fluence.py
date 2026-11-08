"""Real Geant4 track-length normalization, statistics, and local response checks."""
import argparse
import json
import math
from pathlib import Path
import subprocess

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

    def run(label, p, score_settings=settings, history_response=None):
        directory = root / label
        source = write(f"{label}-input.json",p)
        response_args = ["--history-response",history_response] if history_response else []
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
    print(f"Target fluence and history response acceptance: {len(checks)} checks, five real Worker jobs")


if __name__ == "__main__":
    main()
