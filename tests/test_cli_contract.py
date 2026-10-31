"""CLI argument, validation and NDJSON checks without Geant4 or physics data."""
import argparse
import hashlib
import json
from pathlib import Path
import shutil
import subprocess
import tempfile


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cli", type=Path, required=True)
    parser.add_argument("--source-root", type=Path, required=True)
    parser.add_argument("--fake-worker", type=Path, required=True)
    args = parser.parse_args()
    cli, root = args.cli.resolve(), args.source_root.resolve()
    checks = 0

    def check(ok, name):
        nonlocal checks
        checks += 1
        if not ok:
            raise AssertionError(name)

    def invoke(arguments, expected, kind, cwd):
        result = subprocess.run([str(cli), *map(str, arguments)], cwd=cwd, capture_output=True,
                                encoding="utf-8", errors="replace", timeout=20)
        events = [json.loads(line) for line in result.stdout.splitlines()]
        check(result.returncode == expected and events and events[-1]["type"] == kind and
              events[-1]["exit_code"] == expected, "expected command exit and terminal event: " + str(arguments[:2]))
        check(all(e["schema"] == "spacerad.cli-event/1" and e["sequence"] == i for i, e in enumerate(events)),
              "ordered complete NDJSON protocol")
        return events[-1]

    with tempfile.TemporaryDirectory(prefix="SpaceRad CLI contract ") as temporary:
        work = Path(temporary)
        project = root / "examples/projects/electron-proton-flux.json"
        original = hashlib.sha256(project.read_bytes()).hexdigest()
        result = subprocess.run([str(cli), "--help"], capture_output=True, text=True, timeout=20)
        check(result.returncode == 0 and "inspect-runtime" in result.stdout, "CLI help describes commands")
        result = subprocess.run([str(cli), "--version"], capture_output=True, text=True, timeout=20)
        check(result.returncode == 0 and "spacerad_cli " in result.stdout, "CLI version is available")
        event = invoke(["validate", "--project", project], 0, "validated", work)
        check(event["histories"] == 100000 and len(event["sensitive_ids"]) == 3 and event["synthetic"] and
              event["source_sha256"] == original, "validation summarizes exact source and targets")
        event = invoke(["validate", "--project", project, "--histories", "2"], 0, "validated", work)
        check(event["histories"] == 2 and hashlib.sha256(project.read_bytes()).hexdigest() == original,
              "history override preserves source file")
        unicode_dir = work / "Unicode \u5de5\u7a0b space"
        unicode_dir.mkdir()
        unicode_file = unicode_dir / "\u7b80\u5355\u8c31.json"
        unicode_file.write_bytes(project.read_bytes())
        event = invoke(["validate", "--project", unicode_file.relative_to(work)], 0, "validated", work)
        check(Path(event["project"]) == unicode_file, "Unicode and relative project paths roundtrip")
        for arguments in (
            [], ["unknown"], ["validate", "extra"], ["validate", "--unknown"], ["validate"],
            ["validate", "--project"], ["validate", "--project", ""], ["validate", "--project", work],
            ["validate", "--project", project, "--project", project],
            ["validate", "--project", project, "--runtime", "ignored.json"],
            ["inspect-runtime", "--project", project],
            ["run", "--project", project],
        ):
            invoke(arguments, 2, "error", work)
        for value in ("0", "-1", "100000001", "2.5", "nan"):
            invoke(["validate", "--project", project, "--histories", value], 2, "error", work)
        malformed = work / "malformed.json"
        malformed.write_text("{", encoding="utf-8")
        invoke(["validate", "--project", malformed], 2, "error", work)
        oversized = work / "oversized.json"
        with oversized.open("wb") as stream:
            stream.truncate(8 * 1024 * 1024 + 1)
        invoke(["validate", "--project", oversized], 2, "error", work)
        invalid = json.loads(project.read_text())
        invalid["volumes"][0]["size_mm"][0] = -1
        invalid_file = work / "invalid-geometry.json"
        invalid_file.write_text(json.dumps(invalid), encoding="utf-8")
        invoke(["validate", "--project", invalid_file], 2, "error", work)
        output = work / "must not exist"
        invoke(["run", "--project", project, "--runtime", work / "missing.json", "--output", output], 2, "error", work)
        check(not output.exists(), "missing runtime does not create output")
        for value in ("-1", "86401", "1.5"):
            invoke(["run", "--project", project, "--runtime", work / "missing.json", "--output", output,
                    "--timeout-seconds", value], 2, "error", work)
        runtime = work / "runtime.json"
        runtime.write_text(json.dumps({"schema": "spacerad.runtime/1", "worker": "missing-worker",
                                      "data_root": "missing-data", "library_directories": []}), encoding="utf-8")
        invoke(["inspect-runtime", "--runtime", runtime], 2, "error", work)
        check(not output.exists(), "argument/configuration errors leave job directory absent")
        fake_bin = work / "fault worker"
        fake_bin.mkdir()
        fake = fake_bin / args.fake_worker.name
        shutil.copy2(args.fake_worker.resolve(), fake)
        (fake_bin / "radiation-build.json").write_text('{"schema":"test-double-only"}', encoding="utf-8")
        data = work / "fault data"
        manifest = []
        for i, variable in enumerate(("G4LEDATA", "G4PARTICLEXSDATA", "G4LEVELGAMMADATA", "G4ENSDFSTATEDATA", "G4RADIOACTIVEDATA")):
            name = "test-only-" + str(i)
            (data / name).mkdir(parents=True)
            (data / name / "marker").write_text("test double; not physics data", encoding="utf-8")
            manifest.append(variable + "|" + name)
        (fake_bin / "radiation-datasets.txt").write_text("\n".join(manifest) + "\n", encoding="utf-8")
        runtime.write_text(json.dumps({"schema": "spacerad.runtime/1", "worker": str(fake), "data_root": str(data),
                                      "library_directories": [str(args.fake_worker.resolve().parent)]}), encoding="utf-8")
        event = invoke(["run", "--project", project, "--runtime", runtime, "--output", output], 3, "error", work)
        check(event["message"].startswith("Unsupported completion payload:"),
              "malformed completion metadata becomes terminal error instead of escaping Qt callback")
    print(f"{checks} CLI contract checks passed")


if __name__ == "__main__":
    main()
