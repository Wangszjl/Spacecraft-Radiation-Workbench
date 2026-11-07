"""Cross-check native C++ partition statistics against the independent Python fold."""
import argparse
import math
from pathlib import Path

from stratified_tid import read, encode, integral
from stratified_study import summarize


def verify(root):
    plan, result = read(root/"study-plan.json"), read(root/"study-result.json")
    if plan["schema"] != "spacerad.native-partition-plan/1" or result["status"] != "completed":
        raise ValueError("Choose a complete native partition archive")
    studies, reports, seeds, geometry, exposures = [], {}, set(), {}, {}
    total_expected = None
    for name in plan["studies"]:
        jobs = [j for j in plan["jobs"] if j["study"] == name]
        studies.append({"id":name, "partition":jobs[0]["partition"], "replica":jobs[0]["replica"],
            "plan":{"jobs":jobs, "total_histories":sum(j["total_histories"] for j in jobs),
                    "strata_settings":plan["study_settings"]["partitions"][jobs[0]["partition"]]}})
        grouped = {}
        for job in jobs:
            directory = root/job["directory"]
            scan = read(directory/"scan-result.json")
            child_source = job["project"]
            spectrum, = child_source["environment"]["spectra"]
            exposure = math.pi*(child_source["source_radius_mm"]/10)**2*integral(spectrum)
            if child_source["environment"]["quantity"] == "differential_flux":
                exposure *= child_source["duration_s"]
            if not math.isclose(exposure,job["expected_primaries"],rel_tol=1e-12):
                raise ValueError("Native band exposure differs from independent spectrum integral")
            exposures[name] = exposures.get(name,0)+exposure
            for case in scan["cases"]:
                raw = read(directory/case["directory"]/"result.json")
                if raw["project"]["seed"] in seeds:
                    raise ValueError("Repeated real child seed")
                seeds.add(raw["project"]["seed"])
                thickness = case["thickness_mm"]
                if geometry.setdefault(thickness,raw["geometry_audit"]) != raw["geometry_audit"]:
                    raise ValueError("Native physical geometry changed across bands")
                for target in raw["targets"]:
                    moments = raw["score_moments"][target["id"]]["ionizing"]
                    n = raw["histories"]
                    factor = 1.602176634e-13*exposure/target["mass_kg"]
                    part = {"stratum":job["stratum"], "particle":job["particle"], "range_MeV":job["range_MeV"],
                            "mass_kg":target["mass_kg"], "histories":n, "nonzero_histories":moments["nonzero_histories"],
                            "tid_Gy":moments["mean_MeV"]*factor,
                            "observed_standard_error_Gy":math.sqrt(moments["m2_MeV2"]/(n*(n-1)))*factor}
                    grouped.setdefault((thickness,target["id"]),[]).append(part)
        total_expected = exposures[name] if total_expected is None else total_expected
        if not math.isclose(exposures[name],total_expected,rel_tol=1e-12):
            raise ValueError("Native partitions do not preserve total physical exposure")
        reports[name] = {"total_histories":sum(j["total_histories"] for j in jobs),
                        "rows":[{"thickness_mm":k[0],"target_id":k[1],"strata":v} for k,v in grouped.items()]}
    expected = summarize({"studies":studies,"study_settings":plan["study_settings"],"total_histories":plan["total_histories"]},reports)

    def compare(a,b,path="rows"):
        if isinstance(a,bool) or a is None or isinstance(a,str):
            if a != b:raise ValueError(f"Mismatch at {path}")
        elif isinstance(a,(int,float)):
            if not isinstance(b,(int,float)) or not math.isclose(a,b,rel_tol=1e-10,abs_tol=1e-280):
                raise ValueError(f"Numeric mismatch at {path}: {a} / {b}")
        elif isinstance(a,list):
            if len(a) != len(b):raise ValueError(f"Length mismatch at {path}")
            for i,(x,y) in enumerate(zip(a,b)):compare(x,y,f"{path}/{i}")
        else:
            for key,value in a.items():
                if key == "probability_bound_family_cells":
                    if value != expected[key]:raise ValueError("Probability family count differs")
                else:compare(value,b[key],f"{path}/{key}")
    compare(result["rows"],expected["rows"])
    return {"passed":True,"real_jobs":len(seeds),"target_thickness_rows":len(result["rows"]),
            "checks":["raw-moment dose/error folding","full exposure conservation","unique actual seeds",
                      "physical geometry stability","native/Python aggregate statistics and support parity"],
            "scope":"Independent software calculation of the same accepted raw moments, not independent transport physics"}


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--study-dir",type=Path,required=True)
    parser.add_argument("--output",type=Path,required=True)
    args=parser.parse_args()
    if args.output.exists():parser.error("Choose a new parity evidence file")
    result=verify(args.study_dir.resolve())
    args.output.write_bytes(encode(result))
    print(f"Native/Python parity passed: {result['real_jobs']} real jobs, {result['target_thickness_rows']} rows")


if __name__ == "__main__":main()
