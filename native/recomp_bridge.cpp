#include "recomp_bridge.hpp"
#include <limits>
#include <stdexcept>
#include <unordered_set>
#include <vector>

namespace dotm {
DispatchSummary install_recompiled_dispatch(
    GuestMemory& memory,
    std::span<const RecompiledFunction> mappings,
    std::uint32_t entry_point,
    NativeFunction unresolved) {
    if (!unresolved) throw std::invalid_argument("Missing XenonRecomp indirect fallback");
    if (mappings.empty()) throw std::invalid_argument("Empty XenonRecomp host function mapping");
    if (mappings.size()>2000000) throw std::invalid_argument("Unreasonably large PPC mapping");
    std::unordered_set<std::uint32_t> unique_addresses;
    unique_addresses.reserve(mappings.size());
    std::vector<FunctionEntry> validated;
    validated.reserve(mappings.size());
    bool entry_found=false;
    for (const auto& item:mappings) {
        if (item.guest>std::numeric_limits<std::uint32_t>::max() || !item.host)
            throw std::invalid_argument("Invalid mapped PPC function");
        const auto address=std::uint32_t(item.guest);
        if (address<GuestMemory::kCodeBase ||
            std::uint64_t(address)-GuestMemory::kCodeBase>=GuestMemory::kCodeSize ||
            (address&3U)!=0)
            throw std::out_of_range("Recompiled function outside original DOTM executable code");
        if (!unique_addresses.insert(address).second)
            throw std::invalid_argument("Duplicate original DOTM function address");
        validated.push_back({address,item.host});
        if (address==entry_point) entry_found=true;
    }
    if(!entry_found)
        throw std::runtime_error("Original DOTM _xstart entry point is absent from recompiled mapping");
    memory.initialize_function_table(unresolved);
    memory.install_functions(validated);
    return {validated.size(),entry_point};
}
}
