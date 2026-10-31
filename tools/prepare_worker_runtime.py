"""Map activated conda dataset locations to a newly-created Worker runtime directory."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--worker", type=Path, required=True)
    parser.add_argument("--prefix", type=Path, required=True)
    parser.add_argument("--output-dir", type=Path, required=True)
    args = parser.parse_args()
    worker, prefix, output = args.worker.resolve(), args.prefix.resolve(), args.output_dir.resolve()
    if output.exists():
        raise RuntimeError("Refusing an existing runtime directory")
    if not worker.is_file() or not prefix.is_dir() or not (prefix / "lib").is_dir():
        raise RuntimeError("Worker and dependency prefix must exist")
    manifest = worker.parent / "radiation-datasets.txt"
    entries = []
    seen = set()
    for line in manifest.read_text(encoding="utf-8").splitlines():
        if not line.strip():
            continue
        fields = line.strip().split("|")
        if len(fields) != 2:
            raise RuntimeError("Malformed dataset manifest")
        variable, name = fields
        if (not re.fullmatch(r"G4[A-Z0-9_]+DATA", variable) or variable in seen or
                Path(name).name != name or name in ("", ".", "..") or "\\" in name):
            raise RuntimeError("Invalid or duplicate dataset manifest entry")
        seen.add(variable)
        value = os.environ.get(variable)
        if not value:
            raise RuntimeError("Activate matching dependency environment first: " + variable)
        source = Path(value).resolve()
        source.relative_to(prefix)
        if not source.is_dir() or not any(source.iterdir()):
            raise RuntimeError("Missing or empty activated dataset: " + variable)
        # The environment pins dataset packages; never guess a different version by name.
        entries.append({"variable": variable, "manifest_directory": name, "source": str(source)})
    required = {"G4LEDATA", "G4PARTICLEXSDATA", "G4LEVELGAMMADATA", "G4ENSDFSTATEDATA", "G4RADIOACTIVEDATA"}
    if not required.issubset(seen):
        raise RuntimeError("Incomplete Worker dataset manifest")
    output.mkdir(parents=True)
    data = output / "data"
    data.mkdir()
    for item in entries:
        alias = data / item["manifest_directory"]
        if alias.exists():
            raise RuntimeError("Duplicate dataset directory name")
        alias.symlink_to(item["source"], target_is_directory=True)
        item["alias"] = str(alias)
    runtime = {"schema": "spacerad.runtime/1", "worker": str(worker),
               "data_root": str(data), "library_directories": [str(prefix / "lib")]}
    (output / "runtime.json").write_text(json.dumps(runtime, indent=2) + "\n", encoding="utf-8")
    mapping = {"schema": "spacerad.dataset-mapping/1", "prefix": str(prefix), "datasets": entries,
               "manifest_sha256": hashlib.sha256(manifest.read_bytes()).hexdigest(),
               "scope": "Explicit aliases of activated, pinned dataset packages; no content checksum certification"}
    (output / "dataset-mapping.json").write_text(json.dumps(mapping, indent=2) + "\n", encoding="utf-8")
    print(f"Prepared Worker runtime with {len(entries)} recorded dataset mappings")


if __name__ == "__main__":
    main()
