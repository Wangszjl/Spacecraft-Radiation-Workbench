"""Plot audited task-spectrum estimates and conservative fine-partition hit support."""
import argparse
import json
from pathlib import Path


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--study-dir", type=Path, required=True)
    parser.add_argument("--output-dir", type=Path, required=True)
    args = parser.parse_args()
    import matplotlib
    matplotlib.use("Agg")
    import matplotlib.pyplot as plt
    from matplotlib.colors import LogNorm
    import numpy as np
    result = json.loads((args.study_dir/"summary.json").read_bytes())
    if result["schema"] != "spacerad.partition-study-result/1":
        raise ValueError("Unsupported study summary")
    thicknesses = sorted({r["thickness_mm"] for r in result["rows"]})
    if len(thicknesses) != 2:
        raise ValueError("Plot requires two thicknesses")
    fig, axes = plt.subplots(2, 2, figsize=(15, 9.5), gridspec_kw={"width_ratios": [1.05, 1]})
    colors = ["#315d9c", "#7799c8", "#b05b30", "#dfa37c"]
    for row_index, thickness in enumerate(thicknesses):
        rows = sorted((r for r in result["rows"] if r["thickness_mm"] == thickness), key=lambda r: r["target_id"])
        names = [r["target_id"].replace("/Cell_Si", "").replace("_Si", "") for r in rows]
        ax = axes[row_index, 0]
        for index in range(len(rows[0]["estimates"])):
            for target, row in enumerate(rows):
                e = row["estimates"][index]
                x = target+(index-1.5)*0.16
                supported = e["support_screen_passed"]
                ax.plot(x, e["tid_Gy"], marker="o", linestyle="none", color=colors[index],
                    markerfacecolor=colors[index] if supported else "white",
                    label=e["study"] if target == 0 else None)
                # A full error is unknown for zero-hit components. No whisker is drawn in that case.
                if e["relative_standard_error"] is not None:
                    ax.errorbar(x, e["tid_Gy"], yerr=e["observed_standard_error_Gy"], fmt="none",
                                ecolor=colors[index], capsize=2)
        ax.set_yscale("log")
        ax.set_xticks(range(len(names)), names)
        ax.set_ylabel("Ionizing dose (Gy)")
        ax.set_title(f"{thickness:g} mm bus wall: exact task-spectrum estimates")
        ax.grid(axis="y", alpha=0.2)
        if row_index == 0:
            ax.legend(ncol=2, fontsize=9)
        heat = axes[row_index, 1]
        # Minimum observed hit count over the independently seeded fine-partition replicas.
        counts = np.array([[min(e["components"][i]["nonzero_histories"]
                                 for e in r["estimates"] if e["partition"] == "fine")
                            for i in range(len(next(e for e in r["estimates"] if e["partition"] == "fine")["components"]))]
                           for r in rows])
        components = next(e for e in rows[0]["estimates"] if e["partition"] == "fine")["components"]
        labels = [f"{c['particle']}\n{c['range_MeV'][0]:g}-{c['range_MeV'][1]:g}" for c in components]
        cmap = plt.get_cmap("Blues").copy()
        cmap.set_bad("#efefef")
        heat.imshow(np.ma.masked_less(counts, 1), cmap=cmap, norm=LogNorm(1, max(2, int(counts.max()))), aspect="auto")
        for y in range(counts.shape[0]):
            for x in range(counts.shape[1]):
                heat.text(x, y, str(counts[y, x]), ha="center", va="center", fontsize=8,
                          color="white" if counts[y, x] > counts.max()*0.2 else "#233045")
        heat.set_xticks(range(len(labels)), labels, fontsize=8)
        heat.set_yticks(range(len(names)), names)
        heat.set_title(f"{thickness:g} mm: minimum nonzero histories in fine replicas")
    fig.suptitle(f"SpaceRad full-spectrum partition / seed study | {result['total_histories']:,} histories", fontsize=16)
    fig.text(0.02, 0.025, "Open markers: insufficient stratum support. Whiskers: observed 1-SE only when every stratum has hits.\n"
        "Zero-hit cells are unresolved contributions, not zero-dose bounds. No monoenergetic interpolation; no independent physical qualification.", fontsize=10)
    fig.tight_layout(rect=(0, 0.07, 1, 0.95))
    args.output_dir.mkdir(parents=True, exist_ok=False)
    for suffix in ("png", "pdf"):
        fig.savefig(args.output_dir/f"partition-study.{suffix}", dpi=180)
    plt.close(fig)
    print(args.output_dir)


if __name__ == "__main__":
    main()
