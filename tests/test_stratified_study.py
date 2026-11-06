"""Independent support, zero-event and exact partition stability contracts."""
from copy import deepcopy
import math
from pathlib import Path
import sys
import tempfile
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT/"tools"))
from stratified_study import (make_study_plan, reconstruct, review_component, study_csv,
                              study_html, summarize, zero_probability_upper)
from stratified_tid import encode, read, sha


class PartitionStudy(unittest.TestCase):
    def setUp(self):
        self.project = read(ROOT/"examples/projects/spacecraft-tid-assembly.json")
        for s in self.project["environment"]["spectra"]:
            s["energy_MeV"] = [0.1, 3] if s["particle"] == "e-" else [1, 100]
        self.scan = read(ROOT/"examples/scans/spacecraft-bus-thickness.json")
        self.config = read(ROOT/"examples/scans/spacecraft-partition-study.json")
        self.plan = make_study_plan(self.project, self.scan, self.config, sha(encode(self.project)))

    def reports(self):
        result = {}
        for study in self.plan["studies"]:
            components = []
            jobs = study["plan"]["jobs"]
            for job in jobs:
                dose = 1/len(jobs)
                components.append({"stratum": job["id"], "particle": job["particle"],
                    "range_MeV": job["range_MeV"], "mass_kg": 0.001,
                    "histories": job["histories_per_thickness"], "nonzero_histories": 10000,
                    "tid_Gy": dose, "observed_standard_error_Gy": dose*0.01})
            result[study["id"]] = {"total_histories": study["plan"]["total_histories"],
                "rows": [{"target_id": "Si", "thickness_mm": 0.8, "strata": components}]}
        return result

    def test_partition_conserves_budgets_and_physical_exposure_without_mutation(self):
        before = deepcopy(self.project)
        self.assertEqual(self.plan["total_histories"], 8000000)
        self.assertEqual(self.project, before)
        self.assertEqual(len({s["project"]["seed"] for s in self.plan["studies"]}), 4)
        exposures = []
        for study in self.plan["studies"]:
            self.assertEqual(study["project"]["environment"], self.project["environment"])
            exposures.append({p: math.fsum(j["expected_primaries"] for j in study["plan"]["jobs"] if j["particle"] == p)
                              for p in ("e-", "proton")})
        for e in exposures[1:]:
            for p in e:
                self.assertTrue(math.isclose(e[p], exposures[0][p], rel_tol=1e-14))

    def test_unmatched_budget_nonrefinement_and_unsupported_sources_rejected(self):
        config = deepcopy(self.config)
        config["partitions"]["fine"]["species"]["proton"]["histories"][0] += 1
        with self.assertRaisesRegex(ValueError, "budgets"):
            make_study_plan(self.project, self.scan, config, "sha")
        config = deepcopy(self.config)
        config["partitions"]["fine"]["species"]["proton"]["split_MeV"] = [8, 19, 50]
        with self.assertRaisesRegex(ValueError, "refine"):
            make_study_plan(self.project, self.scan, config, "sha")
        for feature in ("source_sampling", "voxel_scoring"):
            project = {**self.project, feature: {}}
            with self.assertRaises(ValueError):
                make_study_plan(project, self.scan, self.config, "sha")

    def test_zero_bound_solves_binomial_tail_and_is_not_a_dose_bound(self):
        upper = zero_probability_upper(100000, 576, 0.95)
        self.assertTrue(math.isclose(100000*math.log1p(-upper), math.log(0.05/576), rel_tol=1e-13))
        self.assertGreater(upper, zero_probability_upper(100000, 1, 0.95))
        self.assertLess(upper, zero_probability_upper(10000, 576, 0.95))
        zero = {"histories": 100000, "nonzero_histories": 0, "tid_Gy": 0, "observed_standard_error_Gy": 0}
        reviewed = review_component(zero, 1, 1, self.config["screen"], 576)
        self.assertEqual(reviewed["zero_event_probability_upper"], upper)
        self.assertIsNone(reviewed["dose_upper_bound_Gy"])
        self.assertFalse(reviewed["support_screen_passed"])

    def test_deposition_ess_uses_energy_moments_and_detects_heavy_tail(self):
        # N=1000: one hundred histories deposit 10 each, nine hundred deposit zero.
        part = {"histories": 1000, "nonzero_histories": 100, "tid_Gy": 1,
                "observed_standard_error_Gy": math.sqrt(9000/(1000*999))}
        reviewed = review_component(part, 1, part["observed_standard_error_Gy"]**2, self.config["screen"], 1)
        self.assertTrue(math.isclose(reviewed["effective_deposition_histories"], 100))
        self.assertTrue(reviewed["support_screen_passed"])
        part["observed_standard_error_Gy"] = 0.8
        reviewed = review_component(part, 1, 0.64, self.config["screen"], 1)
        self.assertLess(reviewed["effective_deposition_histories"], 2)
        self.assertFalse(reviewed["support_screen_passed"])

    def test_positive_replicates_reconstruct_and_pair_descriptively(self):
        result = summarize(self.plan, self.reports())
        row, = result["rows"]
        self.assertEqual(row["mean_estimate_Gy"], 1)
        self.assertTrue(row["support_screen_passed"])
        self.assertEqual(len(row["comparisons"]), 6)
        self.assertEqual(sum(p["kind"] == "seed_repeat" for p in row["comparisons"]), 2)
        self.assertTrue(all(p["difference_in_empirical_standard_errors"] == 0 for p in row["comparisons"]))
        errors = [e["observed_standard_error_Gy"] for e in row["estimates"]]
        self.assertTrue(math.isclose(row["mean_standard_error_Gy"], math.hypot(*errors)/4))
        for estimate in row["estimates"]:
            self.assertEqual(math.fsum(estimate["species_dose_Gy"].values()), estimate["tid_Gy"])

    def test_one_zero_region_keeps_full_uncertainty_unknown_despite_other_hits(self):
        reports = self.reports()
        component = reports["fine-2"]["rows"][0]["strata"][0]
        component.update(nonzero_histories=0, tid_Gy=0, observed_standard_error_Gy=0)
        row, = summarize(self.plan, reports)["rows"]
        self.assertGreater(row["mean_estimate_Gy"], 0)
        self.assertGreater(row["mean_observed_standard_error_Gy"], 0)
        self.assertIsNone(row["mean_standard_error_Gy"])
        self.assertIsNone(row["mean_relative_standard_error"])
        self.assertFalse(row["support_screen_passed"])
        self.assertEqual(sum(p["eligible_for_empirical_comparison"] for p in row["comparisons"]), 3)

    def test_target_mismatch_and_impossible_moments_rejected(self):
        reports = self.reports()
        reports["fine-2"]["rows"][0]["strata"][0]["mass_kg"] *= 2
        with self.assertRaisesRegex(ValueError, "mass"):
            summarize(self.plan, reports)
        reports = self.reports()
        reports["fine-2"]["rows"][0]["target_id"] = "different"
        with self.assertRaisesRegex(ValueError, "set differs"):
            summarize(self.plan, reports)
        part = {"histories": 1000, "nonzero_histories": 2, "tid_Gy": 1, "observed_standard_error_Gy": 0}
        with self.assertRaisesRegex(ValueError, "ESS"):
            review_component(part, 1, 0, self.config["screen"], 1)

    def test_exports_escape_untrusted_target_and_explain_unknown_limits(self):
        result = summarize(self.plan, self.reports())
        result["rows"][0]["target_id"] = "=HYPERLINK(<script>)"
        self.assertIn("'=HYPERLINK", study_csv(result).decode())
        page = study_html(result).decode()
        self.assertNotIn("<script>", page)
        self.assertIn("&lt;script&gt;", page)
        self.assertIn("不能换算为剂量上限", page)

    def test_stored_plan_tamper_rejected_before_cli(self):
        with tempfile.TemporaryDirectory() as temp:
            directory = Path(temp)
            for filename, data in (("parent-project.json", self.project), ("scan-settings.json", self.scan),
                                   ("study-settings.json", self.config), ("plan.json", self.plan)):
                (directory/filename).write_bytes(encode(data))
            bad = deepcopy(self.plan)
            bad["total_histories"] += 1
            (directory/"plan.json").write_bytes(encode(bad))
            with patch("stratified_study.inspect") as mock:
                with self.assertRaisesRegex(ValueError, "reconstruction"):
                    reconstruct(Path("unused"), directory, {})
                mock.assert_not_called()


if __name__ == "__main__":
    unittest.main()
