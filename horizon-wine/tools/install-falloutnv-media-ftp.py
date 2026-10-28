#!/usr/bin/env python3
"""Stage and verify the prepared media bundle through Autorun's FTP server."""
import argparse
from datetime import datetime
from ftplib import FTP, error_perm
from getpass import getpass
import hashlib
import io
import json
from pathlib import Path, PurePosixPath
import re


def digest(data):
    return hashlib.sha256(data).hexdigest()


def merge_settings(data, changes):
    lines = data.decode("utf-8").splitlines()
    lines = [line for line in lines if line.strip().split("=", 1)[0].strip().lower() not in changes]
    lines.extend(key + "=" + value for key, value in changes.items() if value is not None)
    return ("\n".join(lines) + "\n").encode()


def install(bundle, host, port, user):
    manifest = json.loads((bundle / "manifest.json").read_text())
    payload = []
    paths = set()
    for item in manifest["files"]:
        relative = PurePosixPath(item["path"])
        if relative.is_absolute() or ".." in relative.parts or "\n" in str(relative) or "\r" in str(relative):
            raise ValueError("Invalid target path")
        key = str(relative).casefold()
        if key in paths or not key.startswith(("drive_c/fallout new vegas/", "drive_c/dxvk-sarek/versions/1.14.1/")):
            raise ValueError("Duplicate or unexpected target: " + str(relative))
        paths.add(key)
        data = (bundle / "payload" / relative).read_bytes()
        if len(data) != item["size"] or digest(data) != item["sha256"]:
            raise ValueError("Payload changed: " + str(relative))
        payload.append(("/" + str(relative), data))
    password = getpass("FTP password: ")
    stamp = datetime.now().astimezone().strftime("%Y%m%d-%H%M%S")
    work = bundle / ("ftp-" + stamp)
    work.mkdir()
    report = {"started": datetime.now().astimezone().isoformat(), "status": "preflight",
              "registration": "not run; queued for the next Autorun start", "journal": [], "guards": {}}

    def save():
        (work / "install-receipt.json").write_text(json.dumps(report, indent=2) + "\n")

    def connect():
        connection = FTP()
        try:
            connection.connect(host, port, timeout=25)
            connection.login(user, password)
            return connection
        except BaseException:
            connection.close()
            raise

    def exists(path):
        try:
            names = ftp.nlst(str(PurePosixPath(path).parent))
        except error_perm as error:
            if str(error).startswith("550"):
                return False
            raise
        return any(PurePosixPath(n).name.casefold() == PurePosixPath(path).name.casefold() for n in names)

    def read(path):
        stream = io.BytesIO()
        ftp.retrbinary("RETR " + path, stream.write, blocksize=131072)
        return stream.getvalue()

    def remote_hash(path):
        result = hashlib.sha256()
        ftp.retrbinary("RETR " + path, result.update, blocksize=131072)
        return result.hexdigest()

    def directories(path):
        current = ""
        for part in PurePosixPath(path).parent.parts[1:]:
            current += "/" + part
            try:
                ftp.mkd(current)
            except error_perm:
                previous = ftp.pwd()
                ftp.cwd(current)
                ftp.cwd(previous)

    def backup(path, data):
        local = work / "backups" / path.lstrip("/")
        local.parent.mkdir(parents=True, exist_ok=True)
        local.write_bytes(data)

    save()
    ftp = None
    try:
        ftp = connect()
        settings = "/drive_c/Fallout New Vegas/FalloutNV.wine-nx.txt"
        if exists("/run-next.txt") and read("/run-next.txt") not in (
                (manifest["registration"] + "\n").encode(),
                (manifest["enable"] + "\n" + manifest["registration"] + "\n").encode()):
            raise RuntimeError("run-next.txt has another pending program; preserve it")
        if not exists("/drive_c/Fallout New Vegas/FalloutNV.exe"):
            raise RuntimeError("The expected game is missing")
        registrar = read("/drive_c/windows/syswow64/regsvr32.exe")
        if digest(registrar) != manifest["registrar_sha256"]:
            raise RuntimeError("The bundle's registrar does not match the installed Wine")
        if digest(read("/drive_c/windows/syswow64/reg.exe")) != manifest["registry_tool_sha256"]:
            raise RuntimeError("The bundle's registry tool does not match the installed Wine")
        old_settings = read(settings)
        backup(settings, old_settings)
        # Registry files are backed up before any registration is queued, never edited over FTP.
        for name in ("user.reg", "system.reg", "components-1.done"):
            path = "/registry/" + name
            data = read(path)
            backup(path, data)
            if name == "user.reg":
                section = r"Software\\Wine\\AppDefaults\\FalloutNV.exe\\DllOverrides"
                for match in re.finditer(r"(?ms)^\[([^\]\n]+)\][^\n]*\n(.*?)(?=^\[|\Z)", data.decode("utf-8")):
                    if match[1].casefold() == section.casefold() and re.search(r'(?mi)^"\*?dsound"\s*=', match[2]):
                        raise RuntimeError("An existing New Vegas dsound override needs preserving before install")
        for path in ("/drive_c/windows/syswow64/dsound.dll", "/drive_c/windows/syswow64/quartz.dll",
                     "/horizon-dlls/manifest.json"):
            report["guards"][path] = remote_hash(path)
        payload.append((settings, merge_settings(old_settings, manifest["game_settings"])))
        # Only register filters on restart. The game is deliberately not queued.
        payload.append(("/run-next.txt", (manifest["enable"] + "\n" + manifest["registration"] + "\n").encode()))
        report["status"] = "staging"
        save()
        for target, data in payload:
            directories(target)
            action = {"target": target, "staged": target + ".new-fnv-media-" + stamp,
                      "backup": target + ".before-fnv-media-" + stamp,
                      "sha256": digest(data), "old_sha256": None}
            if exists(action["staged"]) or exists(action["backup"]):
                raise RuntimeError("Staging path already exists: " + target)
            if exists(target):
                old = read(target)
                action["old_sha256"] = digest(old)
                if action["old_sha256"] == action["sha256"]:
                    action["unchanged"] = True
                else:
                    backup(target, old)
            report["journal"].append(action)
            save()
            if action.get("unchanged"):
                print("Already matches SHA-256:", target, flush=True)
                continue
            ftp.storbinary("STOR " + action["staged"], io.BytesIO(data), blocksize=131072)
            if remote_hash(action["staged"]) != action["sha256"]:
                raise RuntimeError("Staged readback mismatch: " + target)
            print("Staged and SHA-256 verified:", target, flush=True)
        report["status"] = "activating"
        save()
        for action in report["journal"]:
            if action.get("unchanged"):
                continue
            target = action["target"]
            action["activation_started"] = True
            save()
            if action["old_sha256"] is not None:
                if remote_hash(target) != action["old_sha256"]:
                    raise RuntimeError("Target changed during staging: " + target)
                ftp.rename(target, action["backup"])
            elif exists(target):
                raise RuntimeError("Target appeared during staging: " + target)
            ftp.rename(action["staged"], target)
            action["activated"] = True
            save()
            if remote_hash(target) != action["sha256"]:
                raise RuntimeError("Activated readback mismatch: " + target)
        for path, wanted in report["guards"].items():
            if remote_hash(path) != wanted:
                raise RuntimeError("System file changed: " + path)
        report["status"] = "installed_registration_pending"
        report["finished"] = datetime.now().astimezone().isoformat()
        save()
        print("Files installed; LAV registration is queued. Receipt:", work / "install-receipt.json", flush=True)
    except BaseException as error:
        report["status"] = "failed"
        report["error"] = str(error)
        report["rollback_errors"] = []
        if ftp:
            ftp.close()
        try:
            ftp = connect()
            for action in reversed(report["journal"]):
                target = action["target"]
                has_backup = exists(action["backup"])
                if not action.get("activation_started") and not has_backup:
                    continue
                if not has_backup and action["old_sha256"] is not None:
                    if exists(target) and remote_hash(target) == action["old_sha256"]:
                        continue
                    raise RuntimeError("Original file needs manual recovery: " + target)
                if exists(target):
                    if remote_hash(target) != action["sha256"] or exists(action["staged"]):
                        raise RuntimeError("Rollback refuses an unexpected file: " + target)
                    ftp.rename(target, action["staged"])
                if has_backup:
                    if remote_hash(action["backup"]) != action["old_sha256"]:
                        raise RuntimeError("Backup changed: " + target)
                    ftp.rename(action["backup"], target)
        except BaseException as rollback:
            report["rollback_errors"].append(str(rollback))
        save()
        raise
    finally:
        if ftp:
            ftp.close()


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("bundle", type=Path)
    parser.add_argument("--host", required=True)
    parser.add_argument("--port", type=int, default=5000)
    parser.add_argument("--user", required=True)
    options = parser.parse_args()
    install(options.bundle, options.host, options.port, options.user)
