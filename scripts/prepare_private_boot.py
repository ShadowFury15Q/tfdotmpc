#!/usr/bin/env python3
"""Prepare a legally owned DOTM XEX + 697 XenonRecomp files for a PRIVATE native boot.

Never downloads original game assets, edits default.xex, or copies assets
into the public repository. Use --boot to build and attempt the original _xstart.
"""
import argparse
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


def checked(command):
    print("Running:", command[0], flush=True)
    subprocess.run([str(c) for c in command], check=True)


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


def boot(repo, output, ppc, pe, xex, simde):
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
    checked([binary, "--run-entry", pe, "--xex-header", xex])


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", type=Path, required=True,
                        help="Original game folder on Q: containing default.xex and ppc.zip")
    parser.add_argument("--repo", type=Path, default=Path(__file__).resolve().parents[1])
    parser.add_argument("--output", type=Path)
    parser.add_argument("--extractor", help="Existing xex2ool or XexTool executable")
    parser.add_argument("--simde-include", type=Path)
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
            boot(repo, output, ppc, pe, xex, args.simde_include)
        else:
            print("Use --boot to compile all 697 private PPC units and attempt original _xstart.")
        return 0
    except (OSError, ValueError, subprocess.CalledProcessError, zipfile.BadZipFile) as exc:
        print("PRIVATE BOOT BLOCKED:", exc, file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
