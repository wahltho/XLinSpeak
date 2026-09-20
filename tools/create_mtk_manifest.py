#!/usr/bin/env python3
"""Create an MTK manifest for an existing XLinSpeak ZIP without changing it."""
import argparse
import hashlib
import json
from pathlib import Path, PurePosixPath
import re
import stat
import zipfile


def create_manifest(archive: Path, version: str) -> dict:
    if not re.fullmatch(r"[0-9]+\.[0-9]+\.[0-9]+", version):
        raise ValueError("Expected a stable three-part version, e.g. 1.3.1")
    if archive.name != f"XLinSpeak-linux.{version}.zip":
        raise ValueError("Archive filename must match the declared version")
    files = []
    seen = set()
    with zipfile.ZipFile(archive) as package:
        for entry in package.infolist():
            name = entry.filename
            parts = PurePosixPath(name).parts
            if ("\\" in name or ":" in name or name.startswith("/") or ".." in parts
                    or not parts or parts[0] != "XLinSpeak" or stat.S_ISLNK(entry.external_attr >> 16)):
                raise ValueError(f"Unsafe package entry: {name}")
            if entry.is_dir():
                continue
            relative = "/".join(parts[1:])
            if not relative or relative in seen:
                raise ValueError(f"Duplicate or invalid package file: {name}")
            seen.add(relative)
            data = package.read(entry)
            if relative == "lin_x64/XLinSpeak.xpl":
                if len(data) < 20 or data[:6] != b"\x7fELF\x02\x01" or int.from_bytes(data[18:20], "little") != 62:
                    raise ValueError("Plugin must be a Linux x86-64 ELF binary")
            files.append({"path": relative, "size": len(data), "sha256": hashlib.sha256(data).hexdigest()})
    if not {"lin_x64/XLinSpeak.xpl", "README.md"}.issubset(seen):
        raise ValueError("Archive is missing the plugin or README")
    return {
        "schemaVersion": 1,
        "packageId": "wahltho.xlinspeak",
        "packageVersion": version,
        "releaseTag": f"r{version}",
        "channel": "stable",
        "repository": "https://github.com/wahltho/XLinSpeak",
        "installScope": "xPlaneInstallation",
        "layout": "directory",
        "targetPath": "Resources/plugins/XLinSpeak",
        "supportedProducts": ["zibo-737ng", "levelup-737ng"],
        "supportedPlatforms": ["linux-x64"],
        "restartRequired": True,
        "archive": {"fileName": archive.name, "rootPath": "XLinSpeak", "size": archive.stat().st_size,
                    "sha256": hashlib.sha256(archive.read_bytes()).hexdigest()},
        "protectedPaths": [],
        "files": sorted(files, key=lambda item: item["path"]),
    }


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("archive", type=Path)
    parser.add_argument("--version", required=True)
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()
    manifest = create_manifest(args.archive, args.version)
    output = args.output or args.archive.with_name(f"XLinSpeak-{args.version}-manifest.json")
    if output.resolve() == args.archive.resolve():
        raise ValueError("Manifest output must not overwrite the archive")
    output.write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")
    print(output)


if __name__ == "__main__":
    main()
