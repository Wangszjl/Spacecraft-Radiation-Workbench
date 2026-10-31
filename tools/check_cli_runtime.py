"""Exercise the installed/public CLI protocol with a real configured Worker."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import signal
import subprocess
import time


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
    parser.add_argument("--cli", type=Path, required=True)
    parser.add_argument("--runtime", type=Path, required=True)
    parser.add_argument("--output-dir", type=Path, required=True)
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[1]
    cli, runtime_file, output = args.cli.resolve(), args.runtime.resolve(), args.output_dir.resolve()
    if output.exists():
        raise RuntimeError("Refusing an existing acceptance directory")
    output.mkdir(parents=True)
    original_environment = dict(os.environ)
    checks, commands = [], []

    def check(name, ok):
        checks.append({"name": name, "passed": bool(ok)})
        if not ok:
            raise AssertionError(name)

    def protocol(text, expected):
        events = [json.loads(line) for line in text.splitlines()]
        check("ordered CLI NDJSON events", bool(events) and all(
            e["schema"] == "spacerad.cli-event/1" and e["sequence"] == i for i, e in enumerate(events)))
        terminal = [e for e in events if e["type"] in ("completed", "cancelled", "error", "validated", "runtime")]
        check("exactly one final terminal event", len(terminal) == 1 and terminal[0] == events[-1] and terminal[0]["type"] == expected)
        return events

    def invoke(name, arguments, code, expected):
        process = subprocess.run([str(cli), *map(str, arguments)], capture_output=True, encoding="utf-8",
                                 errors="replace", timeout=120)
        (output / (name + ".stdout.log")).write_text(process.stdout, encoding="utf-8")
        (output / (name + ".stderr.log")).write_text(process.stderr, encoding="utf-8")
        commands.append({"name": name, "arguments": list(map(str, arguments)), "exit_code": process.returncode})
        check(name + " process exit", process.returncode == code)
        events = protocol(process.stdout, expected)
        check(name + " protocol exit agrees", events[-1]["exit_code"] == code)
        return events

    try:
        check("CLI executable exists", cli.is_file())
        runtime = read(runtime_file)
        worker = (runtime_file.parent / runtime["worker"]).resolve()
        event = invoke("inspect", ["inspect-runtime", "--runtime", runtime_file], 0, "runtime")[-1]
        check("actual runtime inspected and fingerprinted", event["worker_sha256"] == sha(worker) and
              event["audit"]["status"] == "ready" and event["audit"]["engine_version"] == "11.4.3")
        results = {}
        for name, quantity in (("flux", "flux"), ("repeat", "flux"), ("fluence", "fluence")):
            project = root / f"examples/projects/electron-proton-{quantity}.json"
            original_hash = sha(project)
            job = output / (name + " job \u5de5\u7a0b")
            arguments = ["run", "--runtime", runtime_file, "--project", project, "--output", job, "--histories", "10000"]
            if name == "flux":
                arguments += ["--verbose"]
            events = invoke(name, arguments, 0, "completed")
            result = read(job / "result.json")
            check(name + " terminal summary matches accepted result bytes", events[-1]["result_sha256"] == sha(job / "result.json")
                  and Path(events[-1]["result_file"]) == job / "result.json" and events[-1]["targets"] == result["targets"]
                  and result["project_sha256"] == sha(job / "project.json") and result["worker"]["sha256"] == sha(worker))
            check(name + " history override and source preservation", result["histories"] == 10000
                  and read(job / "project.json")["events"] == 10000 and sha(project) == original_hash
                  and events[0]["source_sha256"] == original_hash)
            progress = [e for e in events if e["type"] == "progress"]
            check(name + " real progress is streamed", any(0 < e["completed"] < 10000 for e in progress)
                  and all(e["total"] == 10000 for e in progress))
            results[name] = result
        check("verbose Worker text stays on stderr and in the job log", "SCDAT_PROGRESS" in
              (output / "flux.stderr.log").read_text() and "SCDAT_COMPLETED" in
              (output / "flux job \u5de5\u7a0b/worker.log").read_text())
        check("CLI repeat and quantity conversion preserve seeded transport", all(
            results["flux"][key] == results[name][key] for name in ("repeat", "fluence")
            for key in ("targets", "score_moments", "expected_primaries")))
        job = output / "flux job \u5de5\u7a0b"
        before = {p.name: sha(p) for p in job.iterdir() if p.is_file()}
        project = root / "examples/projects/electron-proton-flux.json"
        invoke("existing-output", ["run", "--runtime", runtime_file, "--project", project, "--output", job], 2, "error")
        check("existing job is byte-for-byte protected", before == {p.name: sha(p) for p in job.iterdir() if p.is_file()})
        timed = output / "timeout job"
        event = invoke("timeout", ["run", "--runtime", runtime_file, "--project", project, "--output", timed,
                                   "--histories", "100000000", "--timeout-seconds", "1"], 124, "cancelled")[-1]
        check("timeout cancels without accepting a result", event["reason"] == "timeout" and not (timed / "result.json").exists())
        broken = output / "broken worker"
        broken.mkdir()
        copied = broken / worker.name
        shutil.copy2(worker, copied)
        shutil.copy2(worker.parent / "radiation-datasets.txt", broken / "radiation-datasets.txt")
        (broken / "radiation-build.json").write_bytes((worker.parent / "radiation-build.json").read_bytes() + b"\n")
        changed = dict(runtime)
        changed["worker"] = str(copied)
        changed["data_root"] = str((runtime_file.parent / runtime["data_root"]).resolve())
        changed["library_directories"] = [str(worker.parent)] + [str((runtime_file.parent / p).resolve()) for p in runtime["library_directories"]]
        changed_file = output / "broken-runtime.json"
        changed_file.write_text(json.dumps(changed, indent=2) + "\n", encoding="utf-8")
        invoke("broken-inspect", ["inspect-runtime", "--runtime", changed_file], 3, "error")
        failed = output / "failed job"
        invoke("broken-run", ["run", "--runtime", changed_file, "--project", project, "--output", failed], 3, "error")
        check("Worker runtime failure produces no accepted result", not (failed / "result.json").exists()
              and "snapshot hash mismatch" in (failed / "worker.log").read_text(errors="replace"))
        if os.name == "posix":
            interrupted_job = output / "interrupt job"
            command = [str(cli), "run", "--runtime", str(runtime_file), "--project", str(project),
                       "--output", str(interrupted_job), "--histories", "100000000"]
            with (output / "interrupt.stdout.log").open("w", encoding="utf-8") as out, \
                    (output / "interrupt.stderr.log").open("w", encoding="utf-8") as err:
                process = subprocess.Popen(command, stdout=out, stderr=err)
                try:
                    deadline = time.monotonic() + 20
                    while time.monotonic() < deadline and process.poll() is None:
                        log = interrupted_job / "worker.log"
                        if log.exists() and log.stat().st_size > 0:
                            break
                        time.sleep(.02)
                    else:
                        raise RuntimeError("Worker did not start for interrupt check")
                    children = Path(f"/proc/{process.pid}/task/{process.pid}/children")
                    child_pids = children.read_text().split() if children.exists() else []
                    process.send_signal(signal.SIGINT)
                    process.wait(timeout=15)
                finally:
                    if process.poll() is None:
                        process.kill(); process.wait(timeout=5)
            check("SIGINT exits with cancellation status", process.returncode == 130)
            events = protocol((output / "interrupt.stdout.log").read_text(), "cancelled")
            check("SIGINT leaves no accepted result or running Worker", events[-1]["exit_code"] == 130
                  and not (interrupted_job / "result.json").exists() and bool(child_pids)
                  and all(not Path("/proc" + "/" + pid).exists() for pid in child_pids))
        check("acceptance runner keeps its host environment unchanged", dict(os.environ) == original_environment)
        passed = True
    except Exception as error:
        if not checks or checks[-1]["passed"]:
            checks.append({"name": str(error), "passed": False})
        print(str(error))
        passed = False
    (output / "summary.json").write_text(json.dumps({"schema": "spacerad.cli-runtime-check/1",
        "passed": passed, "checks": checks, "commands": commands,
        "scope": "CLI protocol/shared-service integration with synthetic spectra; not independent dose validation"}, indent=2) + "\n", encoding="utf-8")
    print(f"CLI runtime acceptance: {'passed' if passed else 'failed'}; {len(checks)} checks")
    if not passed:
        raise SystemExit(1)


if __name__ == "__main__":
    main()
