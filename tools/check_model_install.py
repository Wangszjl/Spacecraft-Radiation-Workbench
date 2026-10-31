"""Install, relocate and consume the Qt-free model SDK in a new directory."""
import argparse
import json
from pathlib import Path
import subprocess


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build", type=Path, required=True)
    parser.add_argument("--work", type=Path, required=True)
    parser.add_argument("--config", default="Release")
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[1]
    build, work = args.build.resolve(), args.work.resolve()
    if work.exists():
        raise RuntimeError("Refusing an existing acceptance directory")
    work.mkdir(parents=True)
    checks = []
    commands = []

    def check(name, ok):
        checks.append({"name": name, "passed": bool(ok)})
        if not ok:
            raise AssertionError(name)

    def run(arguments, name, expect_success=True):
        result = subprocess.run([str(x) for x in arguments], stdout=subprocess.PIPE,
                                stderr=subprocess.STDOUT, text=True, errors="replace")
        (work / (name + ".log")).write_text(result.stdout, encoding="utf-8")
        commands.append({"name": name, "arguments": [str(x) for x in arguments],
                         "exit_code": result.returncode})
        if (result.returncode == 0) != expect_success:
            print(result.stdout)
            raise RuntimeError(name + " returned unexpected exit code")
        return result

    try:
        cache = {}
        for line in (build / "CMakeCache.txt").read_text(encoding="utf-8").splitlines():
            if "=" in line and ":" in line.split("=", 1)[0]:
                key, value = line.split("=", 1)
                cache[key.split(":", 1)[0]] = value
        check("source SDK is a model-only build", all(cache.get(key) == "OFF" for key in
              ("SPACERAD_BUILD_SERVICES", "SPACERAD_BUILD_DESKTOP", "SPACERAD_BUILD_WORKER")))
        prefix, relocated = work / "stage install", work / "relocated sdk"
        run(["cmake", "--install", build, "--prefix", prefix, "--config", args.config], "install")
        # Both resolved paths are confined to the newly-created acceptance directory.
        prefix.resolve().relative_to(work)
        relocated.resolve().relative_to(work)
        prefix.rename(relocated)
        check("original install location removed by relocation", not prefix.exists() and relocated.is_dir())
        configs = list(relocated.rglob("SpaceRadConfig.cmake"))
        check("exactly one installed package config", len(configs) == 1)
        check("model-only headers exclude Qt API declarations", not any(
            (relocated / "include/spacerad" / name).exists()
            for name in ("AnalysisService.h", "WorkbenchWidget.h")))
        check("no Qt or Geant4 binaries staged", not any(
            p.suffix.lower() in (".dll", ".exe", ".so", ".dylib")
            for p in relocated.rglob("*") if p.is_file()))
        consumer = work / "consumer build"
        configure = ["cmake", "-S", root / "examples/model-consumer", "-B", consumer,
                     "-G", cache["CMAKE_GENERATOR"], "-DCMAKE_BUILD_TYPE=" + args.config,
                     "-DSpaceRad_DIR=" + str(configs[0].parent),
                     "-DCMAKE_DISABLE_FIND_PACKAGE_Qt6=ON", "-DCMAKE_DISABLE_FIND_PACKAGE_VTK=ON",
                     "-DCMAKE_DISABLE_FIND_PACKAGE_Geant4=ON"]
        for key, flag in (("CMAKE_GENERATOR_PLATFORM", "-A"), ("CMAKE_GENERATOR_TOOLSET", "-T")):
            if cache.get(key):
                configure += [flag, cache[key]]
        if "Ninja" in cache["CMAKE_GENERATOR"] and cache.get("CMAKE_CXX_COMPILER"):
            configure.append("-DCMAKE_CXX_COMPILER=" + cache["CMAKE_CXX_COMPILER"])
        run(configure, "consumer-configure")
        run(["cmake", "--build", consumer, "--config", args.config, "--parallel", "2"], "consumer-build")
        run(["ctest", "--test-dir", consumer, "-C", args.config, "--output-on-failure"], "consumer-test")
        check("relocated SDK compiles and executes independent consumer", True)
        executable = consumer / "spacerad_model_consumer"
        if not executable.exists():
            executable = consumer / "spacerad_model_consumer.exe"
        if not executable.exists():
            executable = consumer / args.config / "spacerad_model_consumer.exe"
        for quantity in ("flux", "fluence"):
            run([executable, root / f"examples/projects/electron-proton-{quantity}.json"], "example-" + quantity)
            check(quantity + " example loads through installed public API", True)
        unavailable = work / "unavailable component"
        unavailable.mkdir()
        (unavailable / "CMakeLists.txt").write_text(
            "cmake_minimum_required(VERSION 3.22)\nproject(Unavailable LANGUAGES CXX)\n"
            "find_package(SpaceRad CONFIG REQUIRED COMPONENTS Services)\n", encoding="utf-8")
        bad_configure = configure.copy()
        bad_configure[bad_configure.index("-S") + 1] = unavailable
        bad_configure[bad_configure.index("-B") + 1] = work / "unavailable build"
        result = run(bad_configure, "unavailable-component", expect_success=False)
        check("unavailable Services component is rejected by package config",
              "SpaceRad_FOUND to FALSE" in result.stdout)
        invalid = ["cmake", "-S", root, "-B", work / "invalid desktop build",
                   "-G", cache["CMAKE_GENERATOR"], "-DSPACERAD_BUILD_SERVICES=OFF",
                   "-DSPACERAD_BUILD_DESKTOP=ON", "-DSPACERAD_BUILD_WORKER=OFF"]
        for key, flag in (("CMAKE_GENERATOR_PLATFORM", "-A"), ("CMAKE_GENERATOR_TOOLSET", "-T")):
            if cache.get(key):
                invalid += [flag, cache[key]]
        result = run(invalid, "invalid-build-mode", expect_success=False)
        check("desktop without services fails with an explicit dependency diagnostic",
              "require SPACERAD_BUILD_SERVICES=ON" in result.stdout)
        passed = True
    except Exception as error:
        if not checks or checks[-1]["passed"]:
            checks.append({"name": str(error), "passed": False})
        passed = False
    (work / "summary.json").write_text(json.dumps({"schema": "spacerad.model-install-check/1",
        "passed": passed, "checks": checks, "commands": commands}, indent=2) + "\n", encoding="utf-8")
    print(f"Model install acceptance: {'passed' if passed else 'failed'}; {len(checks)} checks")
    if not passed:
        raise SystemExit(1)


if __name__ == "__main__":
    main()
