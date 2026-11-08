"""Analytic response folds and archive migration through the public CLI."""
import argparse
import hashlib
import json
import math
from pathlib import Path
import subprocess
import tempfile


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cli", type=Path, required=True)
    parser.add_argument("--project", type=Path, required=True)
    args = parser.parse_args()
    cli = args.cli.resolve()
    project = json.loads(args.project.read_bytes())
    with tempfile.TemporaryDirectory() as tmp:
        root = Path(tmp)

        def invoke(command, *options, code=0, event=None):
            run = subprocess.run([str(cli), command, *map(str, options)], cwd=root,
                                 capture_output=True, encoding="utf-8", timeout=30)
            rows = [json.loads(line) for line in run.stdout.splitlines()]
            assert run.returncode == code and len(rows) == 1, (run.returncode, rows, run.stderr)
            assert rows[0]["type"] == (event if code == 0 else "error"), rows
            return rows[0]

        def write(path, data):
            path.write_text(json.dumps(data, indent=2) + "\n", encoding="utf-8")

        source = root / "project.json"
        response = root / "response.csv"
        archive = root / "new" / "binned"
        project["duration_s"] = 10
        spectrum = project["environment"]["spectra"][0]
        spectrum.update(particle="proton", energy_MeV=[1, 2, 4], values=[1, 10],
                        interpolation="bin_constant")
        project["environment"]["spectra"] = [spectrum]
        write(source, project)
        header = "device_id,metric,particle,energy_MeV,response,response_unit,angular_basis,reference\n"

        def curve(points, metric, unit):
            response.write_text(header + "".join(
                f"OBC_Si,{metric},proton,{e},{r},{unit},isotropic_omnidirectional,Analytic test\n"
                for e, r in points), encoding="utf-8")

        curve([(1, 2), (1.5, 4), (4, 4)], "ddd", "MeV_cm2_per_g")
        raw = source.read_bytes()
        folded = invoke("fold-response", "--project", source, "--response", response,
                        "--output", archive, event="response-folded")["report"]
        assert folded["schema"] == "spacerad.device-response-study/2"
        assert folded["integration_method"] == "piecewise_product_exact_v2"
        # The independent area is 0.5*3 + 0.5*4 + 2*10*4 = 83.5.
        assert math.isclose(folded["rows"][0]["folded_value"], 835, rel_tol=1e-14)
        assert math.isclose(folded["rows"][0]["niel_dose_Gy"], 835 * 1.602176634e-10)
        assert source.read_bytes() == raw
        assert invoke("inspect-response", "--job", archive, event="archived-response")["report"] == folded
        invoke("fold-response", "--project", source, "--response", response, "--output", archive, code=2)
        invoke("inspect-response", "--job", archive, "--runtime", "unused", code=2)

        result_file, manifest_file = archive / "study-result.json", archive / "study-manifest.json"
        manifest = json.loads(manifest_file.read_bytes())
        folded["rows"][0]["folded_value"] = 865.0  # Previous right-edge leak adds 30.
        write(result_file, folded)
        manifest["report_sha256"] = hashlib.sha256(result_file.read_bytes()).hexdigest()
        write(manifest_file, manifest)
        invoke("inspect-response", "--job", archive, code=2)
        folded["schema"] = "spacerad.device-response-study/1"
        del folded["integration_method"]
        manifest["schema"] = "spacerad.device-response-study-archive/1"
        write(result_file, folded)
        manifest["report_sha256"] = hashlib.sha256(result_file.read_bytes()).hexdigest()
        write(manifest_file, manifest)
        old_bytes = result_file.read_bytes()
        review = invoke("inspect-response", "--job", archive, event="archived-response")["report"]
        assert review["archive_review"] == "legacy_inputs_recomputed_v2"
        assert math.isclose(review["rows"][0]["folded_value"], 835, rel_tol=1e-14)
        assert result_file.read_bytes() == old_bytes

        # Linear spectrum E times linear response 0.001E: integral from 1 to 4 is 0.021.
        spectrum.update(interpolation="linear", values=[1, 2, 4])
        env = project["environment"]
        env.update(quantity="differential_fluence", unit="cm-2 MeV-1")
        write(source, project)
        curve([(1, 0.001), (4, 0.004)], "see", "cm2_per_device")
        result = invoke("fold-response", "--project", source, "--response", response,
                        "--output", root / "linear", event="response-folded")["report"]["rows"][0]
        assert math.isclose(result["folded_value"], 0.021, rel_tol=1e-14)
        assert math.isclose(result["poisson_probability_at_least_one"], -math.expm1(-0.021))

        curve([(2, 1), (4, 1)], "see", "cm2_per_device")
        invalid = root / "no-output"
        invoke("fold-response", "--project", source, "--response", response, "--output", invalid, code=2)
        assert not invalid.exists()
    print("Device response CLI: analytic linear/bin folds, flux/fluence, rehashed tamper, legacy review and output guards passed")


if __name__ == "__main__":
    main()
