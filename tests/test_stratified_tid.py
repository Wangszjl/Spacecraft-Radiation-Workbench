"""Conservation across original knots and off-grid energy cuts."""
import math
from pathlib import Path
import sys
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
from stratified_tid import integral, make_plan, split_spectrum


class StratifiedSource(unittest.TestCase):
    def test_linear_at_off_grid_cut_and_knot(self):
        spectrum = {"energy_MeV": [1, 3, 4], "values": [2, 6, 8]}
        a, b, c = split_spectrum(spectrum, [2, 3])
        self.assertEqual([p["values"] for p in (a, b, c)], [[2, 4], [4, 6], [6, 8]])
        self.assertEqual([integral(p) for p in (a, b, c)], [3, 5, 7])

    def test_bin_constant_uses_right_value_at_knot(self):
        spectrum = {"energy_MeV": [1, 3, 5], "values": [4, 2], "interpolation": "bin_constant"}
        a, b, c = split_spectrum(spectrum, [2.5, 3])
        self.assertEqual([p["values"] for p in (a, b, c)], [[4], [4], [2]])
        self.assertEqual([integral(p) for p in (a, b, c)], [6, 2, 4])

    def test_rejects_unusable_partitions(self):
        spectrum = {"energy_MeV": [1, 2, 3], "values": [0, 4], "interpolation": "bin_constant"}
        for cuts in ([0.9], [2], [2.5, 2.5], [2.6, 2.5], [math.nan]):
            with self.subTest(cuts=cuts), self.assertRaises(ValueError):
                split_spectrum(spectrum, cuts)

    def test_exposure_is_independent_of_history_allocation(self):
        spectrum = {"particle": "proton", "energy_MeV": [1, 2], "values": [4, 4], "provenance": {}}
        project = {"source_radius_mm": 10, "duration_s": 7, "seed": 1,
                   "environment": {"spectra": [spectrum], "quantity": "differential_flux"}}
        settings = {"schema": "spacerad.shielding-settings/1", "thicknesses_mm": [1, 2]}
        config = {"schema": "spacerad.energy-strata/1", "species": {"proton": {"split_MeV": [1.5], "histories": [10, 90]}}}
        flux = make_plan(project, settings, config, "test")
        self.assertEqual(flux["total_histories"], 200)
        self.assertTrue(all(math.isclose(j["expected_primaries"], 14*math.pi) for j in flux["jobs"]))
        project["environment"]["quantity"] = "differential_fluence"
        fluence = make_plan(project, settings, config, "test")
        self.assertTrue(all(math.isclose(j["expected_primaries"], 2*math.pi) for j in fluence["jobs"]))
        self.assertEqual(project["environment"]["spectra"], [spectrum])


if __name__ == "__main__":
    unittest.main()
