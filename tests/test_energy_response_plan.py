"""Energy-response inputs conserve unit fluence and fixed spacecraft geometry."""
from copy import deepcopy
import json
import math
from pathlib import Path
import sys
import unittest

ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/"tools"))
from run_energy_response import plan_cases,response_csv


class ResponsePlan(unittest.TestCase):
    def setUp(self):
        self.base=json.loads((ROOT/"examples/projects/spacecraft-tid-assembly.json").read_bytes())
        self.settings=json.loads((ROOT/"examples/scans/spacecraft-bus-thickness.json").read_bytes())

    def test_unit_fluence_and_case_identity(self):
        original=deepcopy(self.base)
        jobs=plan_cases(self.base,self.settings,100000)
        self.assertEqual(self.base,original)
        self.assertEqual(len(jobs),22)
        self.assertEqual(len({j["project"]["seed"] for j in jobs}),22)
        for job in jobs:
            p=job["project"]
            self.assertEqual(p["environment"]["quantity"],"differential_fluence")
            spectrum,=p["environment"]["spectra"]
            a,b=spectrum["energy_MeV"]
            self.assertTrue(math.isclose((b-a)*sum(spectrum["values"])/2,1.0))
            self.assertTrue(math.isclose((a+b)/2,job["central_energy_MeV"]))
            shell=next(v for v in p["volumes"] if v["id"]=="Bus_Al")
            self.assertTrue(all(math.isclose(d-2*shell["wall_mm"],inner)
                                for d,inner in zip(shell["size_mm"],[348.4,268.4,208.4])))
            self.assertEqual(p["instances"],self.base["instances"])
            self.assertEqual(p["templates"],self.base["templates"])
            self.assertEqual(p["events"],100000)

    def test_invalid_budgets_and_weighted_sources(self):
        for budget in (1,True,100000001):
            with self.subTest(budget=budget),self.assertRaises(ValueError):
                plan_cases(self.base,self.settings,budget)
        self.base["source_sampling"]={"schema":"spacerad.source-importance/1"}
        with self.assertRaises(ValueError):plan_cases(self.base,self.settings,100)

    def test_csv_escapes_target_formulas(self):
        result={"rows":[{"target_id":"=unsafe","nonzero_histories":0,"result_sha256":"ignored"}]}
        text=response_csv(result).decode()
        self.assertIn("'=unsafe",text)
        self.assertNotIn("result_sha256",text)


if __name__=="__main__":unittest.main()
