"""Compare the SpacePy manual example and optional independent RADBELT points."""
import argparse
import csv
import json
import math
from pathlib import Path

from generate_ae8ap8_example import SPACEPY_VERSION, model_flux


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--radbelt-points", type=Path, help="CSV from tools/reference/ae8ap8_radbelt_points.f90")
    args = parser.parse_args()
    import numpy as np
    import spacepy
    from spacepy import irbempy as ib
    from spacepy.coordinates import Coords
    from spacepy.time import Ticktock

    if spacepy.__version__ != SPACEPY_VERSION:
        parser.error(f"Expected SpacePy {SPACEPY_VERSION}")
    coords = Coords([[3.0, 0.0, 0.0]], "GEO", "car", use_irbem=True)
    coords.ticks = Ticktock(["2017-02-02T12:00:00"], "ISO")
    expected = 1932209.4427359989  # SpacePy get_AEP8 manual example, AE8MAX at 1 MeV.
    points = {}
    for frame, p in (("GEO", coords), ("GEI", coords.convert("GEI", "car"))):
        value = float(model_flux(np, ib, p, [1.0], 2, flux_kind=1)[0, 0])
        if not math.isclose(expected, value, rel_tol=1e-6):
            raise ValueError(f"{frame}: result differs from the documented SpacePy point")
        points[frame] = value
    radbelt = []
    if args.radbelt_points:
        with args.radbelt_points.open(encoding="utf-8-sig", newline="") as f:
            raw = {(int(m), float(e)): float(value) for m, e, value in csv.reader(f)}
        if raw.keys() != {(1, 0.5), (1, 1.0), (3, 10.0), (3, 30.0)}:
            raise ValueError("Expected four RADBELT AE8MIN/AP8MIN reference points")
        for (model, energy), reference in sorted(raw.items()):
            value = float(ib.get_AEP8(energy, [1.2, 2.0], model="min", fluxtype="int",
                                      particles="e" if model == 1 else "p"))
            if reference <= 0 or not math.isclose(value, reference, rel_tol=1e-6):
                raise ValueError(f"RADBELT comparison differs at model {model}, {energy} MeV")
            radbelt.append({"model": model, "threshold_MeV": energy,
                            "reference_integral_flux_cm-2_s-1": reference, "irbem": value})
    print(json.dumps({"passed": True, "manual_expected_differential_flux": expected,
                      "manual_frame_results": points, "radbelt_integral_comparison": radbelt},
                     indent=2, allow_nan=False))


if __name__ == "__main__":
    main()
