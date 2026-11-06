"""Plot accepted energy-response observations and an external PSTAR range context."""
import argparse
import json
import math
from pathlib import Path


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--response",type=Path,required=True)
    parser.add_argument("--target",default="OBC_Si")
    parser.add_argument("--output-dir",type=Path,required=True)
    parser.add_argument("--al-wall-context-mm",type=float,nargs="+",
                        help="optional nominal Al wall sums, one per plotted thickness; not inferred for arbitrary CAD")
    args=parser.parse_args()
    import matplotlib
    matplotlib.use("Agg")
    import matplotlib.pyplot as plt
    from matplotlib import transforms
    data=json.loads(args.response.read_bytes())
    rows=[r for r in data["rows"] if r["target_id"]==args.target]
    if not rows:parser.error("Requested target is absent")
    root=Path(__file__).resolve().parents[1]
    ref=json.loads((root/"examples/reference/pstar-aluminum-range.json").read_bytes())
    args.output_dir.mkdir(parents=True)
    fig,axes=plt.subplots(2,2,figsize=(11,8),layout="constrained")
    colors={.8:"#1966ad",2.0:"#d36423"}
    thicknesses=sorted({r["thickness_mm"] for r in rows})
    if args.al_wall_context_mm and (len(args.al_wall_context_mm)!=len(thicknesses) or
                                   any(not math.isfinite(x) or x<=0 for x in args.al_wall_context_mm)):
        parser.error("--al-wall-context-mm requires one positive finite Al path per thickness")
    for t in thicknesses:
        color=colors.get(t)
        p=sorted((r for r in rows if r["thickness_mm"]==t and r["particle"]=="proton"),key=lambda r:r["central_energy_MeV"])
        axes[0,0].plot([r["central_energy_MeV"] for r in p],[r["primary_entry_fraction"]*100 for r in p],
                       "o",color=color,label=f"{t:g} mm bus wall")
        for species,axis in (("proton",axes[0,1]),("e-",axes[1,0])):
            selected=sorted((r for r in rows if r["thickness_mm"]==t and r["particle"]==species),key=lambda r:r["central_energy_MeV"])
            positive=[r for r in selected if r["nonzero_histories"]]
            axis.errorbar([r["central_energy_MeV"] for r in positive],
                          [r["ionizing_dose_Gy_per_omni_fluence_cm2"] for r in positive],
                          yerr=[r["observed_standard_error_Gy_per_omni_fluence_cm2"] for r in positive],
                          fmt="o",capsize=3,color=color,label=f"{t:g} mm bus wall")
            zero=[r for r in selected if not r["nonzero_histories"]]
            axis.plot([r["central_energy_MeV"] for r in zero],[.035 if t==thicknesses[0] else .085]*len(zero),
                      "x",color=color,transform=transforms.blended_transform_factory(axis.transData,axis.transAxes))
            axis.set_yscale("log")
    axes[0,0].set_title("Primary proton entry observations")
    axes[0,0].set_ylabel("Primary histories entering target (%)")
    axes[0,1].set_title("Proton ionizing-dose response")
    axes[1,0].set_title("Electron ionizing-dose response")
    for a in (axes[0,1],axes[1,0]):a.set_ylabel("Ionizing dose / fluence (Gy cm²)")
    density=2.699
    axes[1,1].plot([r["energy_MeV"] for r in ref["rows"]],
                   [r["csda_range_g_cm2"]/density*10 for r in ref["rows"]],"s",color="#253f62",label="NIST PSTAR Al CSDA")
    if args.al_wall_context_mm:
        for t,path in zip(thicknesses,args.al_wall_context_mm):
            axes[1,1].axhline(path,color=colors.get(t),linestyle="--",label=f"Nominal Al walls: {path:g} mm")
    axes[1,1].set_yscale("log")
    axes[1,1].set_ylabel("Al path-length context (mm)")
    axes[1,1].set_title("External stopping-range context")
    for i,a in enumerate(axes.flat):
        a.set_xscale("log")
        a.set_xlabel("Electron energy (MeV)" if i==2 else "Proton energy (MeV)")
        a.grid(True,which="both",alpha=.22)
        a.legend(fontsize=8,loc="best")
    fig.suptitle(f"{args.target}: narrow-band spacecraft response, {rows[0]['histories']:,} histories per point",fontsize=13)
    fig.text(.5,-.025,"Points are separate runs at ±0.1% energy; bars: observed 1-SE. Bottom crosses: zero-hit observations, not upper bounds.\nPSTAR CSDA range is a mean path-length approximation, not a transport cutoff. This is not a mission-dose curve.",
             ha="center",fontsize=9)
    fig.savefig(args.output_dir/"energy-response.png",dpi=180,bbox_inches="tight")
    fig.savefig(args.output_dir/"energy-response.pdf",bbox_inches="tight")
    print(args.output_dir/"energy-response.png")


if __name__=="__main__":main()
