"""Real unit-fluence response scaling check, including fluence duration semantics."""
import argparse
from copy import deepcopy
import json
import math
from pathlib import Path
from run_energy_response import cli_environment, encode, invoke, read, reconstruct, sha


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    for option in ("study-dir","cli","runtime","output-dir"):
        parser.add_argument(f"--{option}",required=True,type=Path)
    args=parser.parse_args()
    root,cli,runtime=(x.resolve() for x in (args.study_dir,args.cli,args.runtime))
    output=args.output_dir.resolve()
    if output.exists():parser.error("Choose a new output directory")
    env=cli_environment(cli,runtime)
    original_summary=sha(root/"response.json")
    reconstructed=reconstruct(cli,root,env)
    if (root/"response.json").read_bytes()!=encode(reconstructed):
        raise ValueError("Response study cannot be reconstructed exactly")
    plan=read(root/"plan.json")
    source=next(c for c in plan["cases"] if c["particle"]=="proton" and c["central_energy_MeV"]==50)
    before=read(root/source["directory"]/"job"/"result.json")
    scaled=deepcopy(source["project"])
    scaled["duration_s"]=999  # fluence context; must not enter exposure again
    for spectrum in scaled["environment"]["spectra"]:
        spectrum["values"]=[v*8 for v in spectrum["values"]]
    output.mkdir(parents=True)
    (output/"input.json").write_bytes(encode(scaled))
    terminal=invoke(cli,["run","--project",output/"input.json","--runtime",runtime,
                        "--output",output/"job","--timeout-seconds","180"],output,"run",env,240)
    after=read(output/"job"/"result.json")
    checks={
        "same_worker_and_runtime":before["worker"]["sha256"]==after["worker"]["sha256"] and
            before["runtime_audit"]==after["runtime_audit"],
        "same_seeded_event_moments":before["score_moments"]==after["score_moments"],
        "same_primary_sampling_diagnostics":before["source_diagnostics"]==after["source_diagnostics"],
        "fluence_scales_once_despite_context_duration":math.isclose(after["expected_primaries"],8*before["expected_primaries"],rel_tol=1e-12),
        "same_actual_geometry":before["geometry_audit"]==after["geometry_audit"],
        "study_is_unchanged":sha(root/"response.json")==original_summary,
    }
    before_targets={t["id"]:t for t in before["targets"]}
    checks["dose_and_error_scale_by_eight"]=all(
        target["mass_kg"]==before_targets[target["id"]]["mass_kg"] and
        all(math.isclose(target[key],8*before_targets[target["id"]][key],rel_tol=1e-10,abs_tol=1e-280)
            for key in ("tid_Gy","tid_standard_error_Gy","total_absorbed_Gy","nonionizing_Gy"))
        for target in after["targets"])
    result={"passed":all(checks.values()),"checks":checks,"result_sha256":sha(output/"job"/"result.json"),
            "scope":"Seeded software normalization check at 50 MeV; not an independent transport physics validation"}
    (output/"summary.json").write_bytes(encode(result))
    if not result["passed"]:raise ValueError(checks)
    print(f"Real energy-response normalization checks passed: {len(checks)}")


if __name__=="__main__":main()
