#pragma once
#include "recomp_bridge.hpp"
namespace dotm {
// Optional integration: requires the PRIVATELY generated ppc_func_mapping.cpp
// (and all matching compiled PPC objects) to be linked into the executable.
// Resolves the real DOTM guest entry address 0x8277F670, symbol _xstart.
DispatchSummary install_actual_xenon_table(GuestMemory& memory, NativeFunction unresolved);
}
