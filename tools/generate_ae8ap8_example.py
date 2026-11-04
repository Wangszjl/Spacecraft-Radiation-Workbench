"""Generate a SpaceRad example from an actual AE8MIN/AP8MIN model run."""
import argparse
import csv
import ctypes
from datetime import datetime, timedelta, timezone
import hashlib
import json
import math
from pathlib import Path


SPACEPY_VERSION = "0.7.0"
EPOCH = datetime(2025, 1, 1, tzinfo=timezone.utc)
EARTH_RADIUS_KM = 6371.2  # IRBEM's Re
RADIUS_RE = 1.6
INCLINATION_DEG = 15.0
MU_KM3_S2 = 398600.4418
PERIOD_S = round(2 * math.pi * math.sqrt((RADIUS_RE * EARTH_RADIUS_KM)**3 / MU_KM3_S2))
SPECIES = (
    ("AE8MIN", 1, "e", "e-", 0.1, 4.0),
    ("AP8MIN", 3, "p", "proton", 1.0, 100.0),
)
OMNI_KEYS = "Kp Dst dens velo Pdyn ByIMF BzIMF G1 G2 G3 W1 W2 W3 W4 W5 W6".split()


def utc(t, seconds=False):
    return t.isoformat(timespec="seconds" if seconds else "microseconds").replace("+00:00", "Z")


def file_identity(path):
    data = path.read_bytes()
    return {"file_name": path.name, "size_bytes": len(data), "sha256": hashlib.sha256(data).hexdigest()}


def json_file(path, value):
    path.write_text(json.dumps(value, indent=2, allow_nan=False) + "\n", encoding="utf-8")


def integral_flux(np, ib, coords, grid, model):
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
            ib.int4(n), dc["sysaxes"], ib.int4(model), ib.int4(3), ib.int4(len(block)),
            energy.ctypes.data_as(ctypes.POINTER((ib.real8 * 2) * ne)),
            dc["iyearsat"], dc["idoysat"], dc["utsat"],
            dc["xin1"], dc["xin2"], dc["xin3"],
            flux.ctypes.data_as(ctypes.POINTER((ib.real8 * nt) * ne)),
        )
        result[:, start:start + len(block)] = flux[:len(block), :n].T
    return result


def average_binned_flux(np, flux, grid):
    invalid = ~np.isfinite(flux) | (flux < 0)
    if invalid.any():
        raise ValueError(f"{int(invalid.sum())} invalid model values; missing data cannot be interpreted as zero")
    differences = flux[:, :-1] - flux[:, 1:]
    if (differences < 0).any():
        raise ValueError("Integral flux must be nonincreasing at each orbit sample")
    bin_flux = differences.mean(axis=0)
    differential = bin_flux / np.diff(grid)
    total = float(np.sum(differential * np.diff(grid)))
    expected = float((flux[:, 0] - flux[:, -1]).mean())
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
    parser.add_argument("--samples", type=int, default=192, help="uniform time midpoint samples (16..4096)")
    args = parser.parse_args()
    if not 16 <= args.samples <= 4096:
        parser.error("--samples must be 16..4096")
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

    samples = args.samples
    fractions = (np.arange(samples) + 0.5) / samples
    angle = 2 * math.pi * fractions
    offsets = PERIOD_S * fractions
    times = [EPOCH + timedelta(seconds=float(offset)) for offset in offsets]
    inc = math.radians(INCLINATION_DEG)
    transverse = RADIUS_RE * np.sin(angle)
    xyz = np.column_stack((RADIUS_RE * np.cos(angle), transverse * math.cos(inc), transverse * math.sin(inc)))
    coords = Coords(xyz, "GEI", "car", use_irbem=True)
    coords.ticks = Ticktock([t.replace(tzinfo=None) for t in times], "UTC")
    runs = []
    for label, model, kind, particle, low, high in SPECIES:
        grid = np.geomspace(low, high, 33)
        flux = integral_flux(np, ib, coords, grid, model)
        try:
            differential, total = average_binned_flux(np, flux, grid)
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
        writer.writerow(["utc", "offset_s", "x_GEI_Re", "y_GEI_Re", "z_GEI_Re"])
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
                          "band_fluence_cm-2": total * PERIOD_S,
                          "valid_integral_values": int(flux.size), "scalar_layout_checks": 3, "source_file": raw_id}

    project_path = output / "project.json"
    json_file(project_path, project(spectra, samples))
    json_file(output / "scan-settings.json", {"schema": "spacerad.shielding-settings/1",
              "shell_id": "Shield_Al", "thicknesses_mm": [0.5, 2.0]})
    json_file(output / "environment-manifest.json", {
        "schema": "spacerad.ae8ap8-example/1", "spacepy": SPACEPY_VERSION, "numpy": np.__version__,
        "backend": backend, "models": details, "epoch_utc": utc(EPOCH, True),
        "interval_end_utc": utc(EPOCH + timedelta(seconds=PERIOD_S), True),
        "samples": samples, "duration_s": PERIOD_S, "earth_radius_km": EARTH_RADIUS_KM,
        "orbit_radius_Re": RADIUS_RE, "inclination_deg": INCLINATION_DEG, "mu_km3_s2": MU_KM3_S2,
        "orbit": file_identity(orbit), "project": file_identity(project_path),
        "scan_settings": file_identity(output / "scan-settings.json"),
        "method": "IRBEM FLY_IN_NASA_AEAP integral flux; difference adjacent thresholds, average equal time bins, divide by energy width",
        "scope": "Idealized orbit, trapped electron/proton bands only, isotropic transport; no flight or mission qualification",
    })
    print(f"Generated AE8MIN/AP8MIN spectra: {samples} orbit samples, {PERIOD_S} s; {project_path}")


if __name__ == "__main__":
    main()
