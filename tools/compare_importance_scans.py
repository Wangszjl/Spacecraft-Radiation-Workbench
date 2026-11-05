"""Compare matched direct-species scans to mixture importance-sampled runs."""
import argparse
import hashlib
import json
import math
from pathlib import Path
import subprocess
from run_importance_scan import verify_scan


MEV_J = 1.602176634e-13


def read(path):
    return json.loads(path.read_bytes())


def require(condition, message):
    if not condition:
        raise ValueError(message)


def flux_integral(spectrum):
    e, v = spectrum["energy_MeV"], spectrum["values"]
    if spectrum.get("interpolation") == "bin_constant":
        return math.fsum((b-a)*x for a,b,x in zip(e,e[1:],v))
    return math.fsum((b-a)*(x+y)/2 for a,b,x,y in zip(e,e[1:],v,v[1:]))


def physical_signature(project):
    p=json.loads(json.dumps(project))
    p.pop("schema",None)
    p.pop("provenance",None)
    p.pop("source_sampling",None)
    p.pop("seed",None)
    p.pop("geometry_contract",None)
    p.pop("instance_contract",None)
    metadata=p["environment"].setdefault("metadata",{})
    for key in ("model","model_version","orbit_source","epoch_utc","interval_start_utc",
                "interval_end_utc","statistical_mode","percentile","confidence_level"):
        metadata.setdefault(key,None)
    for volume in p["volumes"]:
        volume.setdefault("shape","box")
    return p


def inspect_scan(cli, directory):
    proc = subprocess.run([str(cli), "inspect-scan", "--job", str(directory)],
                          capture_output=True, text=True, encoding="utf-8", timeout=120)
    events = [json.loads(line) for line in proc.stdout.splitlines() if line.strip()]
    require(proc.returncode == 0 and events and events[-1]["type"] == "archived-scan"
            and events[-1]["exit_code"] == 0, f"Direct scan archive failed: {proc.stderr}")
    return events[-1]["report"]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cli", type=Path, required=True)
    parser.add_argument("--direct", type=Path, required=True)
    parser.add_argument("--weighted", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    cli, direct_root, weighted_root = (p.resolve() for p in (args.cli,args.direct,args.weighted))
    direct = inspect_scan(cli,direct_root)
    weighted = verify_scan(cli,weighted_root)
    direct_project = read(direct_root/"project.json")
    weighted_project = read(weighted_root/"parent-project.json")
    settings = direct["settings"]
    require(direct["status"] == "completed" and weighted["status"] == "completed"
            and direct["settings"] == weighted["scan_settings"]
            and direct["total_planned_histories"] == weighted["total_histories"]
            and direct["runtime_identity"] == weighted["runtime_identity"],
            "Paired total budgets, settings or Worker identity differ")
    require(physical_signature(direct_project) == physical_signature(weighted_project),
            "Physical source settings or spacecraft model differ")
    spectrum_integrals = {s["particle"]:flux_integral(s)
                          for s in direct_project["environment"]["spectra"]}
    source_factor = math.pi*(direct_project["source_radius_mm"]/10)**2
    if direct_project["environment"]["quantity"] == "differential_flux":
        source_factor *= direct_project["duration_s"]
    species_exposure = {p:i*source_factor for p,i in spectrum_integrals.items()}
    total_exposure = math.fsum(species_exposure.values())
    direct_cases = {(c["thickness_mm"],c["particle"]):c for c in direct["cases"]}
    thicknesses = list(settings["thicknesses_mm"])
    weighted_cases = {c["thickness_mm"]:c for c in weighted["cases"]}
    require(set(weighted_cases) == set(thicknesses)
            and len(direct_cases) == len(thicknesses)*len(species_exposure),
            "Missing thickness or species case")
    results = {}
    seeds = set()
    for (thickness,species),case in direct_cases.items():
        result = read(direct_root/case["directory"]/"result.json")
        require(result["histories"] == direct["histories_per_case"]
                and math.isclose(result["expected_primaries"],species_exposure[species],rel_tol=1e-12),
                "Direct species exposure or histories differ")
        seeds.add(result["project"]["seed"])
        results[thickness,species] = result

    rows = []
    for thickness in thicknesses:
        case = weighted_cases[thickness]
        job = weighted_root/case["directory"]/"job"
        result, project = read(job/"result.json"), read(job/"project.json")
        planned_path=weighted_root/case["directory"]/"project.json"
        planned_sha=hashlib.sha256(planned_path.read_bytes()).hexdigest()
        job_sha=hashlib.sha256((job/"project.json").read_bytes()).hexdigest()
        require(case["project_sha256"]==planned_sha and
                project["provenance"]["last_loaded_input"]["sha256"]==planned_sha and
                result["project_sha256"] == job_sha
                and result["histories"] == weighted["histories_per_thickness"]
                and math.isclose(result["expected_primaries"],total_exposure,rel_tol=1e-12)
                and project["source_sampling"] == weighted_project["source_sampling"],
                "Weighted job exposure, input identity, histories or proposal differ")
        weighted_seed=project["seed"]
        require(weighted_seed not in seeds,"Direct/weighted runs must have unique seeds")
        seeds.add(weighted_seed)
        audit=result["source_importance"]
        target_rows={t["id"]:t for t in result["targets"]}
        support=audit["target_support"]
        proposal_species=audit["proposal_species_fractions"]
        require(set(proposal_species)==set(species_exposure),"Proposal fractions omit an incident species")
        for species,proposal_fraction in proposal_species.items():
            species_histories=result["source_diagnostics"]["species_histories"][species]
            if species_histories:
                observed=species_histories/result["histories"]
                sigma=math.sqrt(proposal_fraction*(1-proposal_fraction)/result["histories"])
                require(abs(observed-proposal_fraction)<=6*sigma+1/result["histories"],
                        f"{species}: sampled species allocation differs from proposal")
        proposal=audit["proposal_tail_fraction"]
        expected_proposal=1-weighted_project["source_sampling"]["physical_fraction"]*(1-weighted_project["source_sampling"]["tail_fraction"])
        require(math.isclose(proposal,expected_proposal,rel_tol=1e-14),"Proposal tail fraction differs")
        sampled_species={}

        for species in species_exposure:
            species_histories=result["source_diagnostics"]["species_histories"][species]
            tail_histories=audit["tail_histories_by_species"][species]
            require(0<=tail_histories<=species_histories,"Invalid per-species proposal counts")
            if species_histories:
                tail_rate=tail_histories/species_histories
                tail_se=math.sqrt(proposal*(1-proposal)/species_histories)
                require(abs(tail_rate-proposal)<=6*tail_se+1/species_histories,
                        f"{species}: sampled tail allocation differs from proposal")
            else: tail_rate=None
            sampled_species[species]={"histories":species_histories,"tail_histories":tail_histories,
                                      "sampled_tail_fraction":tail_rate}
            for target,by_species in support.items():
                regions=by_species[species]
                require(set(regions)=={"body","tail"},f"{target}/{species}: missing source region")
                for region,entry in regions.items():
                    expected_count=tail_histories if region=="tail" else species_histories-tail_histories
                    require(entry["histories"]==expected_count and
                            entry["score_moments"][0]["count"]==expected_count,
                            f"{target}/{species}/{region}: history support differs")

        for target,weighted_target in target_rows.items():
            contributions={"body":0.0,"tail":0.0}
            hits={}
            for species in species_exposure:
                for region in ("body","tail"):
                    entry=support[target][species][region]
                    moment=entry["score_moments"][0]
                    require(moment["count"]==entry["histories"],"Conditional regional moment denominator differs")
                    hits[f"{species}/{region}"]=moment["nonzero_histories"]
                    contributions[region]+=moment["mean_MeV"]*entry["histories"]/result["histories"]*total_exposure*MEV_J/weighted_target["mass_kg"]
            need=math.fsum(contributions.values())
            require(math.isclose(need,weighted_target["tid_Gy"],rel_tol=1e-8,abs_tol=1e-280),
                    f"{target}: species/region physical doses do not reconstruct the full estimate")

            direct_species=[]
            direct_dose, direct_variance=0.0,0.0
            for species in species_exposure:
                dr=results[thickness,species]
                current_geometry=dr["geometry_audit"]
                require(current_geometry==result["geometry_audit"],"Direct and weighted geometry audits differ")
                t=next(v for v in dr["targets"] if v["id"]==target)
                require(t["mass_kg"]==weighted_target["mass_kg"],"Target mass differs")
                direct_dose+=t["tid_Gy"]
                direct_variance+=t["tid_standard_error_Gy"]**2
                direct_species.append({"particle":species,"tid_Gy":t["tid_Gy"],
                    "standard_error_Gy":t["tid_standard_error_Gy"],"nonzero_histories":t["nonzero_histories"]})
            direct_se=math.sqrt(direct_variance)
            low=[key for key,count in hits.items() if count<20]
            weighted_rse=weighted_target["relative_standard_error"]
            rows.append({"thickness_mm":thickness,"target_id":target,
                "direct_tid_Gy":direct_dose,"direct_standard_error_Gy":direct_se,
                "direct_species":direct_species,
                "weighted_tid_Gy":weighted_target["tid_Gy"],
                "weighted_empirical_standard_error_Gy":weighted_target["tid_standard_error_Gy"],
                "weighted_empirical_rse":weighted_rse,
                "difference_in_combined_observed_standard_errors":
                    abs(weighted_target["tid_Gy"]-direct_dose)/math.hypot(direct_se,weighted_target["tid_standard_error_Gy"]),
                "weighted_dose_Gy_by_source_region":contributions,
                "weighted_nonzero_histories_by_species_region":hits,
                "source_regions_below_20_hits":low,
                "statistics_screen_passed":not low and weighted_rse is not None and weighted_rse<=.2})

    summary={"schema":"spacerad.importance-spacecraft-comparison/1","passed_archive_audit":True,
        "total_histories_per_method":direct["total_planned_histories"],
        "proposal":weighted_project["source_sampling"],"sampled_species_support":sampled_species,
        "rows":rows,
        "scope":"Equal fixed histories. Low empirical error does not bound unsampled or rare energy-region contributions."}
    output=args.output.resolve()
    output.parent.mkdir(parents=True,exist_ok=True)
    output.write_text(json.dumps(summary,indent=2,allow_nan=False)+"\n",encoding="utf-8")
    print(f"Audited {len(rows)} target/thickness rows at matched total budget")


if __name__=="__main__":
    main()
