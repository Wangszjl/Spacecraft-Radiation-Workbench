"""Independently check a binned response archive using exact rational arithmetic."""
import argparse
import csv
from fractions import Fraction
import json
import math
from pathlib import Path


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("archive", type=Path)
    root = parser.parse_args().archive
    project = json.loads((root / "project.json").read_bytes(), parse_float=Fraction)
    report = json.loads((root / "study-result.json").read_bytes())
    assert report["integration_method"] == "piecewise_product_exact_v2"
    env = project["environment"]
    spectra = {s["particle"]: s for s in env["spectra"]}
    assert all(s["interpolation"] == "bin_constant" for s in spectra.values())
    exposure = project["duration_s"] if env["quantity"] == "differential_flux" else 1
    curves = {}
    with (root / "response.csv").open(encoding="utf-8-sig", newline="") as file:
        for r in csv.DictReader(file):
            curves.setdefault((r["device_id"], r["particle"]), []).append(
                (Fraction(r["energy_MeV"]), Fraction(r["response"])))
    checks = []
    for row in report["rows"]:
        total = Fraction(0)
        for component in row["components"]:
            particle = component["particle"]
            spectrum, curve = spectra[particle], curves[row["device_id"], particle]
            result = Fraction(0)
            for low, high, density in zip(spectrum["energy_MeV"], spectrum["energy_MeV"][1:], spectrum["values"]):
                for (start, left), (end, right) in zip(curve, curve[1:]):
                    a, b = max(low, start), min(high, end)
                    if a < b:
                        # Integrate the response line in the original bin.
                        slope = (right - left) / (end - start)
                        result += density * ((b-a)*left + slope*((b-start)**2-(a-start)**2)/2)
            result *= exposure
            assert math.isclose(float(result), component["folded_value"], rel_tol=1e-12)
            total += result
            checks.append({"device": row["device_id"], "particle": particle,
                           "expected": float(result), "actual": component["folded_value"]})
        assert math.isclose(float(total), row["folded_value"], rel_tol=1e-12)
    assert len(checks) == len(curves), "Response components omitted"
    print(json.dumps({"metric": report["metric"], "method": "independent rational bin overlaps",
                      "passed": True, "components": checks}, indent=2))


if __name__ == "__main__":
    main()
