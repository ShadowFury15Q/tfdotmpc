#!/usr/bin/env python3
"""Private Xbox360 DOTM PowerPC source audit.
Usage: python scripts/audit_private_recomp.py <PRIVATE_PPC_DIRECTORY> [--out report.json]

Checks generated source completeness without building or publishing commercial game code.
Reports ABI names, entry function, mapping consistency, unresolved linkage classes.
"""
import argparse
import collections
import json
from pathlib import Path
import re
import sys

MAPPING = re.compile(r"\{\s*(0x[0-9A-Fa-f]+)\s*,\s*([A-Za-z_]\w*)\s*\}")
DECL = re.compile(r"PPC_EXTERN_FUNC\(([A-Za-z_]\w*)\);")
DEF = re.compile(r"PPC_FUNC_IMPL\(([A-Za-z_]\w*)\)")
HELPER = re.compile(r"__?(?:save|rest)(?:gprlr|fpr|vmx)_\d+")

def audit(directory: Path) -> dict:
    mapping_file = directory/"ppc_func_mapping.cpp"
    shared_file = directory/"ppc_recomp_shared.h"
    config_file = directory/"ppc_config.h"
    for path in (mapping_file,shared_file,config_file):
        if not path.is_file():
            raise RuntimeError(f"Required private generated file missing: {path.name}")
    mapping_text=mapping_file.read_text(encoding="utf-8")
    shared_text=shared_file.read_text(encoding="utf-8")
    mapping=[(int(a,16),name) for a,name in MAPPING.findall(mapping_text) if int(a,16)]
    all_names={name for _,name in mapping}
    addresses=[addr for addr,_ in mapping]
    entries=dict(mapping)
    decl=set(DECL.findall(shared_text))
    impl=set()
    parts=sorted(directory.glob("ppc_recomp.*.cpp"))
    for source in parts:
        impl.update(DEF.findall(source.read_text(encoding="utf-8")))
    entry=0x8277F670
    platform_imports=sorted(x for x in decl if x.startswith("__imp__") and
                            not x.startswith("__imp__sub_") and x!="__imp___xstart")
    helper_symbols=sorted(x for x in decl if HELPER.fullmatch(x))
    other_undefined=sorted(x for x in decl if x not in impl and
                           not x.startswith("__imp__") and x not in helper_symbols)
    mapped_undefined=sorted(x for x in all_names if
                            x not in impl and ("__imp__"+x) not in impl)
    unique_address_count=len(set(addresses))
    map_entry_name=entries.get(entry)
    result={
        "private_code_not_uploaded": True,
        "generated_cpp_files":len(parts),
        "mapping_entries":len(mapping),
        "mapping_unique_addresses":unique_address_count,
        "mapping_symbols":len(all_names),
        "entry_address":f"0x{entry:08X}",
        "entry_symbol":map_entry_name,
        "entry_resolved_to_implementation":bool(map_entry_name and (
           map_entry_name in impl or "__imp__"+map_entry_name in impl)),
        "platform_imports_declarations":len(platform_imports),
        "helper_declarations":len(helper_symbols),
        "unresolved_mapped_symbols":len(mapped_undefined),
        "unresolved_other_declarations":len(other_undefined),
        "unresolved_mapped_examples":mapped_undefined[:15],
        "unresolved_other_examples":other_undefined[:15],
        "missing_preconditions":[]
    }
    if len(parts)<600:result["missing_preconditions"].append("Incomplete generated C++ source set")
    if len(mapping)!=unique_address_count:
        result["missing_preconditions"].append("Duplicate guest function addresses")
    if map_entry_name!="_xstart" or not result["entry_resolved_to_implementation"]:
        result["missing_preconditions"].append("Missing or unimplemented DOTM entry _xstart")
    if mapped_undefined:
        result["missing_preconditions"].append("Mapped guest functions missing from generated C++")
    return result

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument("private_ppc_dir",type=Path)
    parser.add_argument("--out",type=Path)
    args=parser.parse_args()
    result=audit(args.private_ppc_dir)
    output=json.dumps(result,ensure_ascii=False,indent=2)
    if args.out:
        args.out.parent.mkdir(parents=True,exist_ok=True)
        args.out.write_text(output+"\n",encoding="utf-8")
    print(output)
    return 1 if result["missing_preconditions"] else 0

if __name__=="__main__":
    sys.exit(main())
