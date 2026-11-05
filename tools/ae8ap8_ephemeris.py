"""Strict Cartesian UTC ephemeris input for the AE8/AP8 workflow."""
import csv
from datetime import datetime
import hashlib
import io
import math
import re


EARTH_RADIUS_KM = 6371.2  # IRBEM's Re
HEADER = ["utc", "x", "y", "z"]
MAX_BYTES = 2 * 1024 * 1024
MAX_SAMPLES = 4096


def parse_ephemeris(data, unit, max_gap):
    if unit not in ("km", "Re") or not math.isfinite(max_gap) or max_gap <= 0:
        raise ValueError("An explicit position unit and positive finite maximum gap are required")
    if not data or len(data) > MAX_BYTES:
        raise ValueError("Ephemeris must be nonempty and at most 2 MiB")
    reader = csv.reader(io.StringIO(data.decode("utf-8-sig"), newline=""), strict=True)
    if next(reader, None) != HEADER:
        raise ValueError("Ephemeris header must be utc,x,y,z")
    times, positions = [], []
    divisor = EARTH_RADIUS_KM if unit == "km" else 1.0
    for line, row in enumerate(reader, 2):
        if len(times) >= MAX_SAMPLES:
            raise ValueError("Ephemeris exceeds 4096 samples")
        if len(row) != 4 or not re.fullmatch(r"[0-9]{4}-[0-9]{2}-[0-9]{2}T[0-9]{2}:[0-9]{2}:[0-9]{2}(\.[0-9]{1,6})?Z", row[0]):
            raise ValueError(f"Row {line}: expected ISO UTC timestamp and three coordinates")
        try:
            t = datetime.fromisoformat(row[0][:-1] + "+00:00")
            position = tuple(float(v) / divisor for v in row[1:])
        except ValueError as exc:
            raise ValueError(f"Row {line}: invalid UTC timestamp or position") from exc
        radius = math.hypot(*position)
        if t.year < 1972 or not math.isfinite(radius) or radius <= 1:
            raise ValueError(f"Row {line}: UTC year must be >=1972 and finite position outside 1 Re")
        if times:
            gap = (t - times[-1]).total_seconds()
            if not 0 < gap <= max_gap:
                raise ValueError(f"Row {line}: timestamps must strictly increase within maximum gap {max_gap:g} s")
        times.append(t)
        positions.append(position)
    if len(times) < 2:
        raise ValueError("At least two ephemeris samples are required")
    offsets = [(t - times[0]).total_seconds() for t in times]
    return times, offsets, positions


def read_ephemeris(path, unit, max_gap):
    with path.open("rb") as f:
        data = f.read(MAX_BYTES + 1)
    samples = parse_ephemeris(data, unit, max_gap)
    return data, {"file_name": path.name, "size_bytes": len(data),
                  "sha256": hashlib.sha256(data).hexdigest()}, samples


def trapezoid_weights(offsets):
    """Sample weights in seconds; their sum is the covered exposure."""
    gaps = [b - a for a, b in zip(offsets, offsets[1:])]
    return [gaps[0] / 2, *((a + b) / 2 for a, b in zip(gaps, gaps[1:])), gaps[-1] / 2]
