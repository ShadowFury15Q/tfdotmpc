#!/usr/bin/env python3
"""Run upstream XenosRecomp's native shader-container scanner on DOTM assets.

No game files are copied to the public repository. The upstream scanner
recognizes containers; guessing signatures from raw microcode is unsafe.
"""
import argparse
import json
import pathlib
import subprocess
import sys


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("input", type=pathlib.Path, help="Extracted original DOTM asset directory")
    parser.add_argument("output", type=pathlib.Path, help="Private output directory")
    parser.add_argument("--xenosrecomp", required=True, type=pathlib.Path,
                        help="Built hedge-dev/XenosRecomp executable")
    parser.add_argument("--header", required=True, type=pathlib.Path,
                        help="XenosRecomp/shader_common.h")
    args = parser.parse_args()
    source = args.input.resolve()
    output = args.output.resolve()
    executable = args.xenosrecomp.resolve()
    header = args.header.resolve()
    if not source.is_dir():
        parser.error("Input must be an existing extracted-game directory")
    if not executable.is_file() or not header.is_file():
        parser.error("Built XenosRecomp and shader_common.h must exist")
    if output == source or source in output.parents:
        parser.error("Output must be outside the input tree to avoid recursive scans")
    output.mkdir(parents=True, exist_ok=True)
    # Upstream's directory mode recognizes real shader containers, compiles
    # shader caches and emits a C++ source file. It is not a raw DWORD dump.
    target = output / "dotm_xenos_shader_cache.cpp"
    command = [str(executable), str(source), str(target), str(header)]
    result = subprocess.run(command, capture_output=True, text=True, check=False)
    manifest = {"input": str(source), "output": str(target),
                "exit_code": result.returncode, "stdout": result.stdout[-16000:],
                "stderr": result.stderr[-16000:],
                "output_exists": target.is_file()}
    (output / "xenosrecomp_result.json").write_text(
        json.dumps(manifest, indent=2, ensure_ascii=False) + "\n",
        encoding="utf-8")
    if result.stdout:
        print(result.stdout)
    if result.stderr:
        print(result.stderr, file=sys.stderr)
    if result.returncode or not target.is_file():
        print("XenosRecomp did not produce a shader cache", file=sys.stderr)
        return result.returncode or 1
    print(f"Original shader cache: {target}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
