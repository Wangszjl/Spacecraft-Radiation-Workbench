"""Generate a SpaceRad project from AE8MIN/AP8MIN on an example or external orbit."""
import argparse
import csv
import ctypes
from datetime import datetime, timedelta, timezone
import hashlib
import json
import math
from pathlib import Path

from ae8ap8_ephemeris import EARTH_RADIUS_KM, MAX_SAMPLES, read_ephemeris, trapezoid_weights

SPACEPY_VERSION = "0.7.0"
EPOCH = datetime(2025, 1, 1, tzinfo=timezone.utc)
RADIUS_RE = 1.6
INCLINATION_DEG = 15.0
MU_KM3_S2 = 398600.4418
PERIOD_S = round(2 * math.pi * math.sqrt((RADIUS_RE * EARTH_RADIUS_KM)**3 / MU_KM3_S2))
SPECIES = (
    ("AE8MIN", 1, "e", "e-", 0.1, 4.0),
    ("AP8MIN", 3, "p", "proton", 1.0, 100.0),
)
OMNI_KEYS = "Kp Dst dens velo Pdyn ByIMF BzIMF G1 G2 G3 W1 W2 W3 W4 W5 W6".split()
MAX_PROJECT_BYTES = 8 * 1024 * 1024


def utc(t, seconds=False):
    return t.isoformat(timespec="seconds" if seconds else "microseconds").replace("+00:00", "Z")


def project_time(t):
    # The current project metadata schema accepts whole UTC seconds only.
    return None if t.microsecond else utc(t, True)


def file_identity(path):
    data = path.read_bytes()
    return {"file_name": path.name, "size_bytes": len(data), "sha256": hashlib.sha256(data).hexdigest()}


def json_file(path, value):
    path.write_text(json.dumps(value, indent=2, allow_nan=False) + "\n", encoding="utf-8")


def read_base_project(path):
    def unique(pairs):
        fields = {}
        for key, value in pairs:
            if key in fields:
                raise ValueError(f"Duplicate key in base project: {key}")
            fields[key] = value
        return fields

    def reject_constant(name):
        raise ValueError(f"Invalid number in base project: {name}")

    with path.open("rb") as f:
        data = f.read(MAX_PROJECT_BYTES + 1)
    if not data or len(data) > MAX_PROJECT_BYTES:
        raise ValueError("Base project must be nonempty and at most 8 MiB")
    document = json.loads(data, object_pairs_hook=unique, parse_constant=reject_constant)
    if not isinstance(document, dict) or document.get("schema") not in [f"scdat.radiation/{v}" for v in range(2, 8)]:
        raise ValueError("Base project must be SpaceRad schema v2..v7")
    if not isinstance(document.get("name"), str) or not document["name"]:
        raise ValueError("Base project must have a nonempty name")
    return data, document


def model_flux(np, ib, coords, grid, model, flux_kind=3):
    """Use the IRBEM batch layout through the pinned SpacePy ctypes binding."""
    n = len(coords)
    d = ib.prep_irbem(coords.ticks, coords, omnivals={key: [0.0] * n for key in OMNI_KEYS})
    dc = ib.prep_ctypes(d)
    nt, ne = d["ntime_max"], d["nalp_max"]
    if n > nt:
        raise ValueError("IRBEM time array limit exceeded")
    energy = np.zeros((ne, 2), dtype=np.float64)
    flux = np.empty((ne, nt), dtype=np.float64)
    result = np.empty((n, len(grid)), dtype=np.float64)
    for start in range(0, len(grid), ne):
        block = grid[start:start + ne]
        energy[:len(block), 0] = block
        flux.fill(np.nan)
        ib.irbemlib.fly_in_nasa_aeap1(
            ib.int4(n), dc["sysaxes"], ib.int4(model), ib.int4(flux_kind), ib.int4(len(block)),
            energy.ctypes.data_as(ctypes.POINTER((ib.real8 * 2) * ne)),
            dc["iyearsat"], dc["idoysat"], dc["utsat"],
            dc["xin1"], dc["xin2"], dc["xin3"],
            flux.ctypes.data_as(ctypes.POINTER((ib.real8 * nt) * ne)),
        )
        result[:, start:start + len(block)] = flux[:len(block), :n].T
    return result


def average_binned_flux(np, flux, grid, weights=None):
    invalid = ~np.isfinite(flux) | (flux < 0)
    if invalid.any():
        raise ValueError(f"{int(invalid.sum())} invalid model values; missing data cannot be interpreted as zero")
    differences = flux[:, :-1] - flux[:, 1:]
    if (differences < 0).any():
        raise ValueError("Integral flux must be nonincreasing at each orbit sample")
    bin_flux = np.average(differences, axis=0, weights=weights)
    differential = bin_flux / np.diff(grid)
    total = float(np.sum(differential * np.diff(grid)))
    expected = float(np.average(flux[:, 0] - flux[:, -1], weights=weights))
    if total <= 0 or not math.isclose(total, expected, rel_tol=1e-12):
        raise ValueError("Selected energy band has zero flux or failed conservation")
    return differential, total


def project(spectra, samples):
    def box(name, material, center, size, sensitive=False, wall=0):
        return {"id": name, "material": material, "shape": "box", "center_mm": center,
                "size_mm": size, "wall_mm": wall, "sensitive": sensitive,
                "parent": "", "rotation_deg": [0, 0, 0], "visible": True}

    return {
        "schema": "scdat.radiation/6", "name": "AE8MIN/AP8MIN idealized MEO avionics example",
        "duration_s": PERIOD_S, "source_radius_mm": 75, "events": 50000, "seed": 19283741,
        "production_cut_mm": 0.1, "physics_list": "FTFP_BERT_EMZ",
        "units": {"length": "mm", "energy": "MeV_per_particle", "time": "s"},
        "provenance": {"origin_schema": "scdat.radiation/6", "last_loaded_input": None, "migration_origin": None},
        "geometry_contract": "scdat.analytic-assembly/1", "instance_contract": "scdat.component-instances/1",
        "assemblies": [], "templates": [], "instances": [],
        "volumes": [
            box("Shield_Al", "G4_Al", [0, 0, 0], [100, 80, 60], wall=0.5),
            box("OBC_Si", "G4_Si", [-23, 0, 0], [30, 22, 8], True),
            box("Payload_Si", "G4_Si", [22, 4, 6], [24, 26, 8], True),
            box("Payload_Local_Al", "G4_Al", [22, 4, 6], [29, 31, 13], wall=1),
        ],
        "environment": {
            "source": f"IRBEM AE8MIN/AP8MIN on idealized MEO, {samples} midpoint samples; not flight data",
            "synthetic": False, "quantity": "differential_flux", "unit": "cm-2 s-1 MeV-1",
            "angular_distribution": "isotropic", "angular_convention": "omnidirectional_scalar",
            "metadata": {"model": "AE8MIN/AP8MIN (IRBEM)", "model_version": f"SpacePy {SPACEPY_VERSION} bundled IRBEM",
                         "orbit_source": "Analytic circular GEI example; radius 1.6 Re, inclination 15 deg; no flight ephemeris",
                         "epoch_utc": utc(EPOCH, True), "interval_start_utc": utc(EPOCH, True),
                         "interval_end_utc": utc(EPOCH + timedelta(seconds=PERIOD_S), True),
                         "statistical_mode": "Solar minimum maps; one-orbit midpoint time average"},
            "spectra": spectra,
        },
    }


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output-dir", type=Path, required=True, help="a new directory for project and model evidence")
    parser.add_argument("--samples", type=int, help="built-in orbit midpoint samples (default 192)")
    parser.add_argument("--ephemeris", type=Path, help="external Cartesian UTC CSV, header utc,x,y,z")
    parser.add_argument("--frame", choices=("GEO", "GEI"))
    parser.add_argument("--position-unit", choices=("km", "Re"))
    parser.add_argument("--max-gap-seconds", type=float)
    parser.add_argument("--base-project", type=Path, help="preserve an existing project geometry and run settings")
    parser.add_argument("--electron-band-mev", type=float, nargs=2, metavar=("LOW", "HIGH"))
    parser.add_argument("--proton-band-mev", type=float, nargs=2, metavar=("LOW", "HIGH"))
    args = parser.parse_args()
    external = args.ephemeris is not None
    input_options = (args.frame, args.position_unit, args.max_gap_seconds)
    if external:
        if any(v is None for v in input_options) or args.samples is not None:
            parser.error("--ephemeris requires --frame, --position-unit and --max-gap-seconds; omit --samples")
    elif any(v is not None for v in (*input_options, args.base_project, args.electron_band_mev, args.proton_band_mev)):
        parser.error("External frame, units, gap, project and band options require --ephemeris")
    samples = 192 if args.samples is None else args.samples
    if not 16 <= samples <= MAX_SAMPLES:
        parser.error("--samples must be 16..4096")
    bands = (args.electron_band_mev, args.proton_band_mev)
    for (label, _, _, _, low, high), band in zip(SPECIES, bands):
        if band and not low <= band[0] < band[1] <= high:
            parser.error(f"{label}: requested band must lie within {low:g}..{high:g} MeV")
    output = args.output_dir.resolve()
    if output.exists():
        parser.error("--output-dir already exists")
    try:
        import numpy as np
        import spacepy
        from spacepy import irbempy as ib
        from spacepy.coordinates import Coords
        from spacepy.time import Ticktock
    except ImportError as exc:
        parser.error(f"Install SpacePy {SPACEPY_VERSION} in a separate Python environment: {exc}")
    if spacepy.__version__ != SPACEPY_VERSION:
        parser.error(f"The batch binding requires SpacePy {SPACEPY_VERSION}")

    base = base_id = None
    if external:
        try:
            ephemeris_bytes, ephemeris_id, (times, offsets, xyz) = read_ephemeris(
                args.ephemeris, args.position_unit, args.max_gap_seconds)
            if args.base_project:
                base_bytes, base = read_base_project(args.base_project)
                base_id = {"file_name": args.base_project.name, "size_bytes": len(base_bytes),
                           "sha256": hashlib.sha256(base_bytes).hexdigest()}
        except (OSError, ValueError, csv.Error) as exc:
            parser.error(str(exc))
        start, end, duration = times[0], times[-1], offsets[-1]
        samples, frame = len(times), args.frame
        weights = trapezoid_weights(offsets)
    else:
        start, end, duration = EPOCH, EPOCH + timedelta(seconds=PERIOD_S), PERIOD_S
        frame, weights = "GEI", None
        fractions = (np.arange(samples) + 0.5) / samples
        angle = 2 * math.pi * fractions
        offsets = duration * fractions
        times = [EPOCH + timedelta(seconds=float(offset)) for offset in offsets]
        inc = math.radians(INCLINATION_DEG)
        transverse = RADIUS_RE * np.sin(angle)
        xyz = np.column_stack((RADIUS_RE * np.cos(angle), transverse * math.cos(inc), transverse * math.sin(inc)))
    coords = Coords(xyz, frame, "car", use_irbem=True)
    coords.ticks = Ticktock([t.replace(tzinfo=None) for t in times], "UTC")
    if external and not np.allclose(coords.ticks.TAI - coords.ticks.TAI[0], offsets, atol=1e-6, rtol=0):
        parser.error("An external interval crossing a UTC leap second is not supported")
    runs = []
    for (label, model, kind, particle, low, high), band in zip(SPECIES, bands):
        if band:
            low, high = band
        grid = np.geomspace(low, high, 33)
        flux = model_flux(np, ib, coords, grid, model)
        try:
            differential, total = average_binned_flux(np, flux, grid, weights)
        except ValueError as exc:
            parser.error(f"{label}: {exc}")
        # The scalar public API gives an independent check on the ctypes array layout.
        for i, j in ((0, 0), (samples // 2, 16), (samples - 1, 32)):
            sample = coords[i]
            scalar = ib.get_AEP8(float(grid[j]), sample, model="min", fluxtype="int", particles=kind)
            if not math.isclose(flux[i, j], scalar, rel_tol=1e-12):
                raise RuntimeError(f"{label}: batch layout does not match the scalar API at ({i}, {j})")
        runs.append((label, particle, grid, flux, differential, total))

    backend = file_identity(Path(ib.irbemlib._name))
    output.mkdir(parents=True)
    orbit = output / "orbit.csv"
    with orbit.open("w", encoding="utf-8", newline="") as stream:
        writer = csv.writer(stream)
        writer.writerow(["utc", "offset_s", *(f"{axis}_{frame}_Re" for axis in "xyz")])
        writer.writerows((utc(t), offset, *point) for t, offset, point in zip(times, offsets, xyz))

    spectra, details = [], {}
    for label, particle, grid, flux, differential, total in runs:
        raw = output / f"{label.lower()}-integral-flux.csv"
        with raw.open("w", encoding="utf-8", newline="") as stream:
            writer = csv.writer(stream)
            writer.writerow(["utc", "threshold_MeV", "integral_omnidirectional_flux_cm-2_s-1"])
            writer.writerows((utc(t), energy, f) for t, row in zip(times, flux) for energy, f in zip(grid, row))
        raw_id = file_identity(raw)
        spectra.append({"particle": particle, "energy_MeV": grid.tolist(), "values": differential.tolist(),
                        "interpolation": "bin_constant", "provenance": {"conversions": [], "source_file": raw_id}})
        details[label] = {"particle": particle, "energy_range_MeV": [float(grid[0]), float(grid[-1])],
                          "bins": len(differential), "mean_band_flux_cm-2_s-1": total,
                          "band_fluence_cm-2": total * duration,
                          "valid_integral_values": int(flux.size), "scalar_layout_checks": 3, "source_file": raw_id}

    project_path = output / "project.json"
    document = project(spectra, samples)
    manifest = {
        "schema": "spacerad.ae8ap8-example/1", "spacepy": SPACEPY_VERSION, "numpy": np.__version__,
        "backend": backend, "models": details, "epoch_utc": utc(start, not external),
        "interval_end_utc": utc(end, not external),
        "samples": samples, "duration_s": duration, "earth_radius_km": EARTH_RADIUS_KM,
        "orbit": file_identity(orbit),
        "method": "IRBEM FLY_IN_NASA_AEAP integral flux; difference adjacent thresholds, average equal time bins, divide by energy width",
        "scope": "Idealized orbit, trapped electron/proton bands only, isotropic transport; no flight or mission qualification",
    }
    if external:
        env = document["environment"]
        env["source"] = f"IRBEM AE8MIN/AP8MIN evaluated on external {frame} ephemeris; modeled, not measured flux"
        env["metadata"].update(orbit_source=f"{ephemeris_id['file_name']} ({frame}, {args.position_unit})",
                               epoch_utc=project_time(start), interval_start_utc=project_time(start),
                               interval_end_utc=project_time(end),
                               statistical_mode="Solar minimum maps; ephemeris endpoint trapezoidal time average")
        name = base.get("name", "External orbit") if base else "External orbit avionics example"
        if base:
            document = {**base, "environment": env}
            (output / "input-project.json").write_bytes(base_bytes)
        document.update(name=f"{name} (AE8MIN/AP8MIN)", duration_s=duration)
        (output / "input-ephemeris.csv").write_bytes(ephemeris_bytes)
        manifest.update(schema="spacerad.ae8ap8-ephemeris/1", input_ephemeris=ephemeris_id,
                        frame=frame, input_position_unit=args.position_unit, max_gap_s=args.max_gap_seconds,
                        base_project=base_id,
                        method="IRBEM integral flux; difference energy thresholds, integrate by trapezoid over UTC endpoints, divide by exposure and bin width",
                        scope="External orbit samples, selected trapped electron/proton bands only, isotropic transport; no mission qualification")
    else:
        manifest.update(orbit_radius_Re=RADIUS_RE, inclination_deg=INCLINATION_DEG, mu_km3_s2=MU_KM3_S2)
    if base is None:
        json_file(output / "scan-settings.json", {"schema": "spacerad.shielding-settings/1",
                  "shell_id": "Shield_Al", "thicknesses_mm": [0.5, 2.0]})
    json_file(project_path, document)
    manifest["project"] = file_identity(project_path)
    manifest["scan_settings"] = file_identity(output / "scan-settings.json") if base is None else None
    json_file(output / "environment-manifest.json", manifest)
    print(f"Generated AE8MIN/AP8MIN spectra: {samples} orbit samples, {duration:g} s; {project_path}")


if __name__ == "__main__":
    main()
