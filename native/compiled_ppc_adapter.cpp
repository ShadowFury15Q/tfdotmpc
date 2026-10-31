#include "recomp_bridge.hpp"
#include "ppc_context.h" // from the PRIVATE XenonRecomp output; CI uses a synthetic test ABI
#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <type_traits>
#include <vector>

// XenonRecomp normally emits PPCFuncMappings[] in ppc_func_mapping.cpp.
// Never publish the generated game code into the public GitHub repository.
static_assert(std::is_same_v<dotm::NativeFunction, PPCFunc*>,
              "The guest dispatcher ABI must EXACTLY match XenonRecomp");
static_assert(sizeof(PPCFuncMapping)==sizeof(dotm::RecompiledFunction),
              "XenonRecomp mapping and native bridge have different layouts");

namespace dotm {
DispatchSummary install_actual_xenon_table(GuestMemory& memory, NativeFunction unresolved) {
    constexpr std::size_t kMaxEntries=2000000;
    constexpr std::uint32_t kDotmEntry=0x8277F670U;
    std::vector<RecompiledFunction> entries;
    entries.reserve(200000);
    bool terminated=false;
    for(std::size_t n=0;n<kMaxEntries;++n){
        const auto& m=PPCFuncMappings[n];
        if(!m.guest&&!m.host){terminated=true;break;}
        if(!m.guest||!m.host)
            throw std::runtime_error("Corrupted XenonRecomp guest mapping sentinel");
        entries.push_back({m.guest,m.host});
    }
    if(!terminated)throw std::runtime_error("Unterminated XenonRecomp guest function table");
    return install_recompiled_dispatch(memory,entries,kDotmEntry,unresolved);
}
}
