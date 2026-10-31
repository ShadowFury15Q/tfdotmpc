#!/usr/bin/env python3
"""Prepare a legally owned DOTM XEX + 697 XenonRecomp files for a PRIVATE native boot.

Never downloads original game assets, edits default.xex, or copies assets
into the public repository. Use --boot to build and attempt the original _xstart.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import zipfile
import zlib
from private_ppc_preflight import (
    EXPECTED_ENTRY, EXPECTED_IMAGE_BASE, EXPECTED_UNITS, EXPECTED_XEX_SHA256,
    inspect_archive, inspect_units, pe_entry, sha256,
)

REQUIRED = ("ppc_recomp_shared.h", "ppc_context.h", "ppc_config.h", "ppc_func_mapping.cpp")


def checked(command, env=None):
    print("Running:", command[0], flush=True)
    subprocess.run([str(c) for c in command], check=True, env=env)


def verify_pe(path):
    if not path.is_file() or pe_entry(path) != (EXPECTED_IMAGE_BASE, EXPECTED_ENTRY):
        raise ValueError("Missing or incorrect decrypted Xbox PPC PE: " + str(path))
    return path


def verified_ppc(root, output):
    original = root / "ppc"
    if original.is_dir():
        report = inspect_units(original)
        if report and report[0] == EXPECTED_UNITS and not any(report[1:]) and all(
            (original / f).is_file() for f in REQUIRED
        ):
            print("Using existing original PPC source directory:", original)
            return original
    archive = root / "ppc.zip"
    report = inspect_archive(archive) if archive.is_file() else None
    if not report or report[0] != EXPECTED_UNITS or any(report[1:]):
        raise ValueError("Original 697 PPC translation units missing or incomplete in ppc.zip")
    target = output / "ppc"
    target.mkdir(parents=True, exist_ok=True)
    with zipfile.ZipFile(archive) as z:
        files = {n for n in z.namelist() if n.endswith("ppc_recomp.0.cpp")}
        if len(files) != 1:
            raise ValueError("Ambiguous private PPC root in ZIP")
        prefix = next(iter(files))[:-len("ppc_recomp.0.cpp")]
        names = [prefix + f"ppc_recomp.{i}.cpp" for i in range(EXPECTED_UNITS)]
        names += [prefix + f for f in REQUIRED]
        for name in names:
            info = z.getinfo(name)
            destination = target / name[len(prefix):]
            if destination.exists():
                crc = 0
                with destination.open("rb") as current:
                    for block in iter(lambda: current.read(1024 * 1024), b""):
                        crc = zlib.crc32(block, crc)
                if destination.stat().st_size != info.file_size or crc != info.CRC:
                    raise ValueError("Different private PPC file already exists; refusing overwrite: " + str(destination))
            else:
                with z.open(info) as src, destination.open("xb") as dst:
                    shutil.copyfileobj(src, dst)
    print("Prepared original PPC units:", EXPECTED_UNITS)
    return target


def find_extractor(explicit, root):
    candidates = [explicit] if explicit else [
        str(root / "tools" / "xex2ool.exe"),
        str(root / "tools" / "XexTool.exe"),
        "xex2ool", "xex2ool.exe", "XexTool", "XexTool.exe",
    ]
    for candidate in candidates:
        if not candidate:
            continue
        path = candidate if Path(candidate).is_file() else shutil.which(candidate)
        if path:
            return str(path), "xextool" if "xextool" in Path(path).name.lower() else "xex2ool"
    raise ValueError("No XEX extractor found. Install xex2ool or specify --extractor.")


def verified_or_extracted_pe(root, output, extractor):
    for path in (
        root / "private" / "original_title.pe",
        root / "private" / "default.pe",
        root / "private" / "default.exe",
        root / "private" / "default.bin",
        output / "original_title.pe",
    ):
        if path.is_file() and pe_entry(path) == (EXPECTED_IMAGE_BASE, EXPECTED_ENTRY):
            print("Using existing decrypted original PPC PE:", path)
            return path
    if extractor is None:
        # The uploaded original XEX is retail AES + Basic compression.
        # It can be decoded losslessly without a separate executable.
        from extract_dotm_xex_basic import extract as extract_original_xex
        return verify_pe(extract_original_xex(
            root / "default.xex", output / "original_title.pe"))
    executable, kind = find_extractor(extractor, root)
    path = output / "original_title.pe"
    if path.exists():
        raise ValueError("A different PE already exists at " + str(path) + "; refusing overwrite")
    command = (
        [executable, "basefile", str(root / "default.xex"), "-o", str(path)]
        if kind == "xex2ool" else
        [executable, "-b", str(path), str(root / "default.xex")]
    )
    checked(command)
    print("Original PE extraction completed; validating image and entrypoint.")
    return verify_pe(path)


def staged_boot_resources(root, output):
    """Verify a private boot-resource ZIP and stage files without overwriting originals."""
    archive = next((p for p in (
        root / "DOTM_BOOT_RESOURCES.zip",
        root / "private" / "DOTM_BOOT_RESOURCES.zip",
        output.parent / "DOTM_BOOT_RESOURCES.zip",
    ) if p.is_file()), None)
    if archive is None:
        return None
    target = output / "game"
    with zipfile.ZipFile(archive) as z:
        manifest = json.loads(z.read("DOTM_BOOT_RESOURCES_HASHES.json"))
        entries = manifest.get("files", [])
        if not entries or len(entries) != len({entry["path"] for entry in entries}):
            raise ValueError("Invalid boot resource hash manifest")
        for entry in entries:
            name = entry["path"]
            parts = Path(name).parts
            if (not name.startswith("TransGame/") or
                    any(part in ("", ".", "..") for part in parts) or
                    "\\\\" in name or ":" in name):
                raise ValueError("Unsafe original title resource path: " + name)
            info = z.getinfo(name)
            if (info.external_attr >> 16) & 0o170000 == 0o120000:
                raise ValueError("Refusing symbolic link in boot resources: " + name)
            if info.file_size != entry["bytes"]:
                raise ValueError("Original boot resource size mismatch: " + name)
            digest = hashlib.sha256()
            with z.open(info) as source:
                for block in iter(lambda: source.read(1024 * 1024), b""):
                    digest.update(block)
            if digest.hexdigest() != entry["sha256"]:
                raise ValueError("Original boot resource SHA256 mismatch: " + name)
        for entry in entries:
            destination = target.joinpath(*Path(entry["path"]).parts)
            if destination.exists():
                if not destination.is_file() or sha256(destination) != entry["sha256"]:
                    raise ValueError("Refusing to overwrite different original resource: " + str(destination))
                continue
            destination.parent.mkdir(parents=True, exist_ok=True)
            with z.open(entry["path"]) as source, destination.open("xb") as dest:
                shutil.copyfileobj(source, dest)
    print("Verified and staged private original boot resources:", len(entries), target)
    return target


def boot(repo, root, output, ppc, pe, xex, simde, game_root):
    cmake, ninja = shutil.which("cmake"), shutil.which("ninja")
    clang = shutil.which("clang++") or shutil.which("clang-cl")
    if not (cmake and ninja and clang):
        raise ValueError("Native toolchain missing: require CMake, Ninja, Clang")
    if not (repo / "native" / "CMakeLists.txt").is_file():
        raise ValueError("GitHub tfdotmpc checkout missing native/CMakeLists.txt: " + str(repo))
    build_path = output / "native-build"
    args = [cmake, "-S", str(repo / "native"), "-B", str(build_path),
            "-G", "Ninja", "-DCMAKE_BUILD_TYPE=Release",
            "-DCMAKE_CXX_COMPILER=" + clang, "-DDOTM_PRIVATE_PPC_DIR=" + str(ppc),
            # The bctr jump-table repair requires original decrypted PPC bytes.
            # Without this, the private link can succeed but indirect branches
            # inside original functions still use incomplete generated targets.
            "-DDOTM_PRIVATE_PE_FILE=" + str(pe)]
    if simde:
        args.append("-DDOTM_SIMDE_INCLUDE_DIR=" + str(simde))
    checked(args)
    checked([cmake, "--build", str(build_path), "--target",
             "dotm_private_link_probe", "--parallel", "2"])
    binary = build_path / ("dotm_private_link_probe.exe" if sys.platform == "win32"
                           else "dotm_private_link_probe")
    print("Attempting the ORIGINAL PPC _xstart, not the synthetic test.")
    env = os.environ.copy()
    if game_root is None:
        for candidate in (root, output / "game", output.parent / "game"):
            if (candidate / "TransGame" / "Xbox360TOC.txt").is_file():
                game_root = candidate
                break
        if game_root is None:
            game_root = staged_boot_resources(root, output)
    if game_root is not None:
        game_root = game_root.resolve()
        toc = game_root / "TransGame" / "Xbox360TOC.txt"
        if not toc.is_file():
            raise ValueError("Original title TOC missing from --game-root: " + str(toc))
        env["DOTM_GAME_ROOT"] = str(game_root)
        print("Mounted private original title resources:", game_root, flush=True)
    checked([binary, "--run-entry", pe, "--xex-header", xex], env=env)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", type=Path, required=True,
                        help="Original game folder on Q: containing default.xex and ppc.zip")
    parser.add_argument("--repo", type=Path, default=Path(__file__).resolve().parents[1])
    parser.add_argument("--output", type=Path)
    parser.add_argument("--extractor", help="Existing xex2ool or XexTool executable")
    parser.add_argument("--simde-include", type=Path)
    parser.add_argument("--game-root", type=Path,
                        help="Private extracted title filesystem containing TransGame/Xbox360TOC.txt")
    parser.add_argument("--boot", action="store_true",
                        help="Compile original PPC code and attempt original game entry")
    args = parser.parse_args()
    root = args.root.resolve()
    repo = args.repo.resolve()
    output = (args.output or root / "private" / "native_boot").resolve()
    try:
        if not root.is_dir():
            raise ValueError("Original game directory not accessible: " + str(root))
        if output.is_relative_to(repo) and not output.is_relative_to(repo / "private"):
            raise ValueError("Private game output cannot go into public repository paths")
        xex = root / "default.xex"
        if not xex.is_file() or sha256(xex) != EXPECTED_XEX_SHA256:
            raise ValueError("Expected original DOTM default.xex with verified SHA256, not another build")
        print("Verified original DOTM XEX SHA256")
        ppc = verified_ppc(root, output)
        output.mkdir(parents=True, exist_ok=True)
        pe = verified_or_extracted_pe(root, output, args.extractor)
        print("PRIVATE ORIGINAL INPUTS READY:", ppc, pe, xex)
        if args.boot:
            boot(repo, root, output, ppc, pe, xex, args.simde_include, args.game_root)
        else:
            print("Use --boot to compile all 697 private PPC units and attempt original _xstart.")
        return 0
    except (OSError, ValueError, subprocess.CalledProcessError, zipfile.BadZipFile) as exc:
        print("PRIVATE BOOT BLOCKED:", exc, file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
