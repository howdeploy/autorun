#!/usr/bin/env python3
"""Prepare official x86 media components without replacing Autorun's system DLLs."""
import argparse
import hashlib
import io
import json
from pathlib import Path
import struct
import tarfile
from urllib.request import urlopen
import zipfile


ASSETS = (
    ("DSOAL_r697.zip", "https://github.com/kcat/dsoal/releases/download/archive/DSOAL_r697.zip",
     "1d70fc40efcb877bee65ce2a40ceb07fe35c16d51629bd7c4782caf37a4a864f"),
    ("LAVFilters-0.83-x86.zip", "https://github.com/Nevcairiel/LAVFilters/releases/download/0.83/LAVFilters-0.83-x86.zip",
     "8189505b593f25ff939f4a070135ec09f417d1d505012f79f43cbb247c7f21b2"),
    ("dxvk-sarek-1.14.1.tar.gz", "https://github.com/pythonlover02/dxvk-sarek/releases/download/v1.14.1/dxvk-sarek-1.14.1.tar.gz",
     "07a6d5d889e9810ce856511e853cd72d2f547ec12c0dc5d7049b32e4bfa05c25"),
)
GAME = "drive_c/Fallout New Vegas"
FILTERS = ("LAVSplitter.ax", "LAVAudio.ax", "LAVVideo.ax")


def sha(data):
    return hashlib.sha256(data).hexdigest()


def check_pe(data):
    if len(data) < 64 or data[:2] != b"MZ":
        raise ValueError("Not a PE file")
    offset = struct.unpack_from("<I", data, 0x3c)[0]
    if (offset > len(data) - 24 or data[offset:offset + 4] != b"PE\0\0"
            or struct.unpack_from("<H", data, offset + 4)[0] != 0x14c):
        raise ValueError("Expected an x86 PE file")


def prepare(bundle, runtime):
    downloads = bundle / "downloads"
    payload = bundle / "payload"
    downloads.mkdir(parents=True, exist_ok=True)
    if payload.exists():
        raise FileExistsError("Payload exists; use a new bundle directory or retain the existing manifest")
    archives = []
    for name, url, digest in ASSETS:
        path = downloads / name
        if not path.exists():
            with urlopen(url, timeout=60) as response:
                data = response.read(64 * 1024 * 1024 + 1)
            if len(data) > 64 * 1024 * 1024 or sha(data) != digest:
                raise ValueError("Download hash/size mismatch: " + name)
            path.write_bytes(data)
        data = path.read_bytes()
        if sha(data) != digest:
            raise ValueError("Archive changed: " + name)
        archives.append(data)

    files = []

    def put(relative, data):
        path = payload / relative
        if Path(relative).suffix.lower() in (".dll", ".exe", ".ax"):
            check_pe(data)
        path.parent.mkdir(parents=True, exist_ok=True)
        with path.open("xb") as output:
            output.write(data)
        files.append({"path": relative, "size": len(data), "sha256": sha(data)})

    with zipfile.ZipFile(io.BytesIO(archives[0])) as outer:
        with zipfile.ZipFile(io.BytesIO(outer.read("DSOAL_r697.zip"))) as archive:
            for name in ("dsound.dll", "dsoal-aldrv.dll"):
                put(f"{GAME}/{name}", archive.read("DSOAL/Win32/" + name))
            for name in archive.namelist():
                if name.startswith("DSOAL/Documentation/") and not name.endswith("/"):
                    put(f"{GAME}/media-licenses/dsoal/{Path(name).name}", archive.read(name))
    # WASAPI reaches Horizon's audio backend without falling back into DirectSound.
    put(f"{GAME}/alsoft.ini", b"[general]\ndrivers=wasapi\nfrequency=48000\nchannels=stereo\nstereo-encoding=basic\n")
    with zipfile.ZipFile(io.BytesIO(archives[1])) as archive:
        for name in archive.namelist():
            if "/" not in name and name.lower().endswith((".dll", ".ax", ".manifest")):
                put(f"{GAME}/{name}", archive.read(name))
        for name in ("COPYING", "README.md", "CHANGELOG.txt"):
            put(f"{GAME}/media-licenses/lav/{name}", archive.read(name))
    with tarfile.open(fileobj=io.BytesIO(archives[2]), mode="r:gz") as archive:
        prefix = "dxvk-sarek-1.14.1/build/x32/"
        for member in archive.getmembers():
            if member.isfile() and member.name.startswith(prefix) and member.name.endswith(".dll"):
                name = member.name[len(prefix):]
                if "/" in name:
                    raise ValueError("Unexpected Sarek member: " + member.name)
                put("drive_c/dxvk-sarek/versions/1.14.1/" + name, archive.extractfile(member).read())
        put(f"{GAME}/media-licenses/sarek/LICENSE", archive.extractfile("dxvk-sarek-1.14.1/LICENSE").read())
    put("drive_c/dxvk-sarek/versions/1.14.1/release.ini",
        f"version=1.14.1\nsource={ASSETS[2][1]}\nsha256={ASSETS[2][2]}\n".encode())

    # Reuse Wine's registrar. No new setup executable or subprocess support is needed.
    registrar = (runtime / "drive_c/windows/syswow64/regsvr32.exe").read_bytes()
    for action, switches in (("register", "/s"), ("unregister", "/u /s")):
        stem = f"FalloutNV-media-{action}"
        put(f"{GAME}/{stem}.exe", registrar)
        args = switches + " " + " ".join(f'"C:\\Fallout New Vegas\\{name}"' for name in FILTERS)
        put(f"{GAME}/{stem}.args.txt", (args + "\n").encode())
        put(f"{GAME}/{stem}.wine-nx.txt", b"verbose=0\nprofile=0\ncpu=fex\nd3d=wine\nlsfg=0\n")

    registry_tool = (runtime / "drive_c/windows/syswow64/reg.exe").read_bytes()
    override_key = r"HKCU\Software\Wine\AppDefaults\FalloutNV.exe\DllOverrides"
    for action, args in (("enable", f'add "{override_key}" /v dsound /t REG_SZ /d native /f'),
                         ("disable", f'delete "{override_key}" /v dsound /f')):
        stem = f"FalloutNV-media-{action}"
        put(f"{GAME}/{stem}.exe", registry_tool)
        put(f"{GAME}/{stem}.args.txt", (args + "\n").encode())
        put(f"{GAME}/{stem}.wine-nx.txt", b"verbose=0\nprofile=0\ncpu=fex\nd3d=wine\nlsfg=0\n")

    manifest = {"schema": 1, "sources": [{"archive": n, "url": u, "sha256": h} for n, u, h in ASSETS],
                "files": files, "registrar_sha256": sha(registrar), "registry_tool_sha256": sha(registry_tool),
                "game_settings": {"d3d": None, "d3d9": None, "cpu": "fex", "verbose": "0", "profile": "0",
                                  "dxvk-source": "sarek", "dxvk-version": "1.14.1", "frame-limit": "30",
                                  "vsync": "1", "lsfg": "0"},
                "registration": "sdmc:/switch/wine/drive_c/Fallout New Vegas/FalloutNV-media-register.exe",
                "enable": "sdmc:/switch/wine/drive_c/Fallout New Vegas/FalloutNV-media-enable.exe",
                "unregistration": "sdmc:/switch/wine/drive_c/Fallout New Vegas/FalloutNV-media-unregister.exe",
                "limitations": ["LAV registration affects the shared Wine registry.",
                                "Community DSOAL/LAV versions were not published; these versions need a console trial.",
                                "No music, localization, game INI, firmware or clock changes."]}
    (bundle / "manifest.json").write_text(json.dumps(manifest, indent=2) + "\n")
    print(f"Prepared {len(files)} files: {bundle / 'manifest.json'}")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("bundle", type=Path)
    parser.add_argument("--runtime", type=Path, required=True, help="Matching Autorun Windows DLL tree")
    options = parser.parse_args()
    prepare(options.bundle, options.runtime)
