"""Ephemeris validation and nonuniform time weighting (stdlib only)."""
import math
import json
import tempfile
from pathlib import Path
import sys
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
from generate_ae8ap8_example import read_base_project
from ae8ap8_ephemeris import parse_ephemeris, trapezoid_weights


SAMPLE = b"""utc,x,y,z
2014-01-01T00:00:18.368Z,12742.4,0,0
2014-01-01T00:00:28.368Z,0,12742.4,0
2014-01-01T00:00:58.368Z,0,0,12742.4
"""


class Ephemeris(unittest.TestCase):
    def test_unequal_intervals_and_km_conversion(self):
        times, offsets, positions = parse_ephemeris(SAMPLE, "km", 30)
        self.assertEqual(times[0].microsecond, 368000)
        self.assertEqual(positions, [(2, 0, 0), (0, 2, 0), (0, 0, 2)])
        self.assertEqual(offsets, [0, 10, 40])
        weights = trapezoid_weights(offsets)
        self.assertEqual(weights, [5, 20, 15])
        self.assertEqual(math.fsum(a*b for a, b in zip(weights, [2, 3, 7])) / offsets[-1], 4.375)

    def test_v8_base_sampling_is_preserved_without_mutation(self):
        document = {"schema":"scdat.radiation/8", "name":"weighted fixture", "source_sampling":{
            "schema":"spacerad.source-importance/1", "physical_fraction":0.9, "tail_fraction":0.05}}
        with tempfile.TemporaryDirectory() as temp:
            path = Path(temp)/"base.json"
            raw = json.dumps(document).encode()
            path.write_bytes(raw)
            data, base = read_base_project(path)
            self.assertEqual(data, raw)
            self.assertEqual(base, document)
            self.assertEqual(path.read_bytes(), raw)
            path.write_text('{"schema":"scdat.radiation/8","name":"a","name":"b"}')
            with self.assertRaises(ValueError):read_base_project(path)

    def test_rejects_gaps_and_invalid_input(self):
        variants = [
            (SAMPLE, "km", 29.9),
            (SAMPLE, "m", 30),
            (SAMPLE, "km", math.nan),
            (SAMPLE.replace(b"28.368Z", b"18.368Z"), "km", 40),
            (SAMPLE.replace(b"28.368Z", b"08.368Z"), "km", 60),
            (SAMPLE.replace(b"18.368Z", b"18.368+00:00"), "km", 30),
            (SAMPLE.replace(b"12742.4", b"NaN"), "km", 30),
            (SAMPLE.replace(b"12742.4", b"6000"), "km", 30),
            (SAMPLE.replace(b"utc,x,y,z", b"utc,x,y,z,frame"), "km", 30),
            (b"\n".join(SAMPLE.splitlines()[:2]), "km", 30),
        ]
        for data, unit, gap in variants:
            with self.subTest(data=data, unit=unit, gap=gap), self.assertRaises(ValueError):
                parse_ephemeris(data, unit, gap)


if __name__ == "__main__":
    unittest.main()
