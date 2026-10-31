"""Exercise real Worker transport and its service against a configured runtime."""
import argparse
import copy
import hashlib
import json
import math
import os
from pathlib import Path
import shutil
import subprocess


def sha(path):
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1048576), b""):
            digest.update(block)
    return digest.hexdigest()


def read(path):
    return json.loads(path.read_text(encoding="utf-8-sig"))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--runtime", type=Path, required=True)
    parser.add_argument("--service-test", type=Path, required=True)
    parser.add_argument("--output-dir", type=Path, required=True)
    parser.add_argument("--histories", type=int, default=10000)
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[1]
    output = args.output_dir.resolve()
    if output.exists():
        raise RuntimeError("Refusing an existing acceptance directory")
    if args.histories < 2000:
        raise RuntimeError("At least 2000 histories required for bounded sampling checks")
    output.mkdir(parents=True)
    original_environment = dict(os.environ)
    checks, commands = [], []

    def check(name, ok):
        checks.append({"name": name, "passed": bool(ok)})
        if not ok:
            raise AssertionError(name)

    def run(command, directory, env, success=True):
        log = directory / "worker.log"
        with log.open("w", encoding="utf-8") as stream:
            result = subprocess.run([str(x) for x in command], cwd=directory, env=env,
                                    stdout=stream, stderr=subprocess.STDOUT, timeout=180)
        commands.append({"arguments": [str(x) for x in command], "exit_code": result.returncode,
                         "log": log.relative_to(output).as_posix()})
        check(directory.name + " expected process exit", (result.returncode == 0) == success)
        return log.read_text(encoding="utf-8", errors="replace")

    try:
        runtime_path = args.runtime.resolve()
        runtime = read(runtime_path)
        check("runtime uses exact supported schema", set(runtime) ==
              {"schema", "worker", "data_root", "library_directories"} and runtime["schema"] == "spacerad.runtime/1")
        def resolve(value):
            return (runtime_path.parent / value).resolve()
        worker, data = resolve(runtime["worker"]), resolve(runtime["data_root"])
        libraries = [resolve(x) for x in runtime["library_directories"]]
        service = args.service_test.resolve()
        check("Worker and service executable exist", worker.is_file() and service.is_file())
        check("runtime data and library directories exist", data.is_dir() and all(x.is_dir() for x in libraries))
        env = dict(os.environ)
        manifest = worker.parent / "radiation-datasets.txt"
        context = worker.parent / "radiation-build.json"
        names = set()
        for line in manifest.read_text(encoding="utf-8").splitlines():
            if not line.strip():
                continue
            variable, name = line.strip().split("|")
            if variable in names or Path(name).name != name or name in ("", ".", "..") or "\\" in name:
                raise RuntimeError("Malformed dataset manifest")
            names.add(variable)
            path = data / name
            if not path.is_dir() or not any(path.iterdir()):
                raise RuntimeError("Missing matching dataset: " + variable)
            # Keep alias directory names; resolving symlinks would defeat the audit.
            env[variable] = str(path)
        check("mandatory datasets declared", {"G4LEDATA", "G4PARTICLEXSDATA", "G4LEVELGAMMADATA",
              "G4ENSDFSTATEDATA", "G4RADIOACTIVEDATA"}.issubset(names))
        key = "PATH" if os.name == "nt" else "LD_LIBRARY_PATH"
        env[key] = os.pathsep.join([str(worker.parent)] + [str(p) for p in libraries] + [env.get(key, "")])
        worker_hash, context_hash, manifest_hash = sha(worker), sha(context), sha(manifest)
        check("Geant4 version is pinned", read(context)["compiled_geant4_version"] == "11.4.3")
        project = read(root / "examples/projects/electron-proton-flux.json")
        project.update({"schema": "scdat.radiation/6", "name": "Synthetic concentric-sphere Worker regression",
            "geometry_contract": "scdat.analytic-assembly/1", "instance_contract": "scdat.component-instances/1",
            "assemblies": [], "templates": [], "instances": [], "events": args.histories,
            "seed": 24681357, "source_radius_mm": 11, "production_cut_mm": .01,
            "provenance": {"origin_schema": "scdat.radiation/6", "last_loaded_input": None, "migration_origin": None},
            "volumes": [{"id": "Detector_Si", "material": "G4_Si", "shape": "sphere",
                "center_mm": [0, 0, 0], "size_mm": [20, 20, 20], "wall_mm": 0, "sensitive": True,
                "parent": "", "rotation_deg": [0, 0, 0], "visible": True}]})
        fluence, scaled = copy.deepcopy(project), copy.deepcopy(project)
        fluence["environment"]["quantity"] = "differential_fluence"
        fluence["environment"]["unit"] = "cm-2 MeV-1"
        for spectrum in fluence["environment"]["spectra"]:
            spectrum["values"] = [x * 64 for x in spectrum["values"]]
            spectrum["provenance"]["conversions"].append({"operation": "quantity_conversion",
                "from": "differential_flux", "to": "differential_fluence", "duration_s": 64, "factor": 64})
        scaled["duration_s"] = 128
        results = {}
        for name, input_project in (("flux", project), ("repeat", project), ("fluence", fluence), ("double-exposure", scaled)):
            directory = output / name
            directory.mkdir()
            input_file, result_file = directory / "project.json", directory / "result.json"
            input_file.write_text(json.dumps(input_project, indent=2) + "\n", encoding="utf-8")
            log = run([worker, input_file, result_file], directory, env)
            result = read(result_file)
            check(name + " completed actual requested transport", result["status"] == "completed" and
                  result["schema"] == "scdat.radiation-result/2" and result["histories"] == args.histories)
            check(name + " input and Worker identities match", result["project_sha256"] == sha(input_file) and
                  result["worker"]["sha256"] == worker_hash and result["engine_version"] == "11.4.3")
            audit = result["runtime_audit"]
            check(name + " runtime identities remain enforced", audit["status"] == "ready" and
                  audit["build_context_sha256"] == context_hash and audit["manifest_sha256"] == manifest_hash)
            check(name + " no Geant4 navigation warning", not any(x in log for x in
                  ("G4Exception", "WARNING", "GeomNav1002", "Stuck Track")))
            check(name + " all event moments retained", result["score_moments"]["Detector_Si"]["ionizing"]["count"] == args.histories)
            target = result["targets"][0]
            check(name + " analytic silicon sphere mass", len(result["targets"]) == 1 and target["id"] == "Detector_Si"
                  and math.isclose(target["mass_kg"], 2330 * 4 * math.pi / 3 * .01**3, rel_tol=1e-12))
            check(name + " nonzero statistically sampled target", target["nonzero_histories"] > args.histories / 2
                  and target["tid_Gy"] > 0 and 0 < target["relative_standard_error"] < .1)
            moments = result["score_moments"]["Detector_Si"]["ionizing"]
            factor = 1.602176634e-13 * result["expected_primaries"] / target["mass_kg"]
            se = math.sqrt(moments["m2_MeV2"] / (args.histories - 1) / args.histories)
            check(name + " dose and error agree with raw event moments", math.isclose(target["tid_Gy"], moments["mean_MeV"] * factor, rel_tol=1e-12)
                  and math.isclose(target["tid_standard_error_Gy"], se * factor, rel_tol=1e-12))
            results[name] = result
        first = results["flux"]
        for name in ("repeat", "fluence"):
            check(name + " seeded dose and raw moments agree", all(first[key] == results[name][key]
                  for key in ("targets", "score_moments", "expected_primaries", "source_diagnostics")))
        doubled = results["double-exposure"]
        check("exposure scales dose, preserves transport and RSE", doubled["score_moments"] == first["score_moments"]
              and doubled["expected_primaries"] == first["expected_primaries"] * 2
              and doubled["targets"][0]["tid_Gy"] == first["targets"][0]["tid_Gy"] * 2
              and doubled["targets"][0]["relative_standard_error"] == first["targets"][0]["relative_standard_error"])
        probability = (10 / 11)**2
        entries = first["targets"][0]["primary_entry_histories"]
        check("primary sphere entry agrees with analytic projected area within six sigma", abs(entries - args.histories * probability)
              <= 6 * math.sqrt(args.histories * probability * (1 - probability)))
        diagnostics = first["source_diagnostics"]
        check("Lambert cosine mean agrees with analytic sampling within six sigma", abs(diagnostics["mean_inward_cos_theta"] - 2/3)
              <= 6 * math.sqrt(1 / 18 / args.histories))
        check("two-particle mixing agrees with spectral integrals within six sigma", sum(diagnostics["species_histories"].values()) == args.histories
              and abs(diagnostics["species_histories"]["e-"] - args.histories / 6) <= 6 * math.sqrt(args.histories * 5/36))
        check("source direction remains inward and normalized", diagnostics["outward_directions"] == 0
              and diagnostics["maximum_direction_norm_error"] < 1e-12 and diagnostics["maximum_unit_position_error"] < 1e-12)
        existing = output / "flux/result.json"
        old_hash = sha(existing)
        directory = output / "overwrite-rejected"; directory.mkdir()
        log = run([worker, output / "flux/project.json", existing], directory, env, success=False)
        check("existing result bytes cannot be overwritten", sha(existing) == old_hash and "Refusing to overwrite" in log)
        directory = output / "missing-data"; directory.mkdir()
        missing_env = dict(env); missing_env.pop("G4LEDATA", None)
        log = run([worker, output / "flux/project.json", directory / "result.json"], directory, missing_env, success=False)
        check("missing required dataset fails closed", not (directory / "result.json").exists() and "Required dataset environment variable missing" in log)
        directory = output / "modified-manifest"; directory.mkdir()
        copied_worker = directory / worker.name
        shutil.copy2(worker, copied_worker)
        shutil.copy2(context, directory / context.name)
        (directory / manifest.name).write_bytes(manifest.read_bytes() + b"\n")
        log = run([copied_worker, output / "flux/project.json", directory / "result.json"], directory, env, success=False)
        check("modified dataset manifest identity is rejected", not (directory / "result.json").exists() and "manifest hash mismatch" in log)
        directory = output / "service"; directory.mkdir()
        service_libraries = libraries if libraries else [worker.parent]
        log = run([service, worker, data, *service_libraries], directory, env)
        check("real service failure paths and host environment checks passed", "Checks complete: 0 failure(s)" in log
              and log.count("PASS ") >= 16 and "FAIL " not in log)
        check("runner does not mutate process environment", dict(os.environ) == original_environment)
        passed = True
    except Exception as error:
        if not checks or checks[-1]["passed"]:
            checks.append({"name": str(error), "passed": False})
        print(str(error))
        passed = False
    (output / "summary.json").write_text(json.dumps({"schema": "spacerad.worker-runtime-check/1",
        "passed": passed, "checks": checks, "commands": commands,
        "scope": "Software transport, normalization and analytic source/geometry checks; not independent dose physics validation"}, indent=2) + "\n", encoding="utf-8")
    print(f"Worker runtime acceptance: {'passed' if passed else 'failed'}; {len(checks)} checks")
    if not passed:
        raise SystemExit(1)


if __name__ == "__main__":
    main()
