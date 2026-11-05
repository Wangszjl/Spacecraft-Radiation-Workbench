"""Create a schema-v8 copy with full-spectrum importance sampling."""
import argparse
from copy import deepcopy
import json
from pathlib import Path


def weighted_project(project, physical_fraction, tail_fraction, species_fractions=None):
    if project.get("schema") not in {f"scdat.radiation/{v}" for v in range(2, 7)}:
        raise ValueError("Choose an unweighted schema v2..v6 project without voxel scoring")
    if project.get("voxel_scoring") is not None or project.get("source_sampling") is not None:
        raise ValueError("Project already has voxel scoring or source sampling configured")
    if not 0.05 <= physical_fraction < 1 or not 1e-5 <= tail_fraction <= 0.5:
        raise ValueError("Physical fraction must be [0.05,1); tail fraction must be [1e-5,0.5]")
    p = deepcopy(project)
    sampling={"schema": "spacerad.source-importance/1",
              "physical_fraction": physical_fraction, "tail_fraction": tail_fraction}
    if species_fractions is not None:
        species={s["particle"] for s in project["environment"]["spectra"]}
        if set(species_fractions)!=species or any(not 0<x<=1 for x in species_fractions.values()) or abs(sum(species_fractions.values())-1)>1e-12:
            raise ValueError("Species proposal must give every source species a positive fraction summing to one")
        sampling["proposal_species_fractions"]=species_fractions
    p.update(schema="scdat.radiation/8", geometry_contract="scdat.analytic-assembly/1",
             instance_contract="scdat.component-instances/1",source_sampling=sampling)
    for key in ("assemblies", "templates", "instances"):
        p.setdefault(key, [])
    for volume in p["volumes"]:
        for key, value in (("shape", "box"), ("parent", ""), ("rotation_deg", [0, 0, 0]), ("visible", True)):
            volume.setdefault(key, value)
    return p


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--project", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--physical-fraction", type=float, required=True)
    parser.add_argument("--tail-fraction", type=float, required=True)
    parser.add_argument("--seed", type=int, help="optional independent comparison seed")
    parser.add_argument("--species-fraction",action="append",nargs=2,metavar=("PARTICLE","FRACTION"),
                        help="proposal fraction per incident species; repeat for all source species")
    args = parser.parse_args()
    project=json.loads(args.project.read_bytes())
    fractions={particle:float(value) for particle,value in args.species_fraction} if args.species_fraction else None
    p = weighted_project(project, args.physical_fraction, args.tail_fraction, fractions)
    if args.seed is not None:
        if not 1 <= args.seed <= 2147483646:
            parser.error("--seed must be 1..2147483646")
        p["seed"] = args.seed
    with args.output.open("x", encoding="utf-8", newline="\n") as stream:
        json.dump(p, stream, indent=2, allow_nan=False)
        stream.write("\n")


if __name__ == "__main__":
    main()
