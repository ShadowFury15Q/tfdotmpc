#include "ppc_nonvolatile_fpr.hpp"
#include <array>
#include <cstdint>
#include <stdexcept>

#define CHECK(test) do { if (!(test)) throw std::runtime_error(#test); } while (false)

struct Register {
    union { std::uint64_t u64; std::uint32_t u32; };
    Register() : u64(0) {}
};
struct Context {
    Register r12;
    Register f14,f15,f16,f17,f18,f19,f20,f21,f22,f23,f24,f25,f26,f27,f28,f29,f30,f31;
    std::uint64_t lr = 0;
};
int main() {
    std::array<std::uint8_t, 2048> memory{};
    Context ctx{};
    ctx.r12.u32 = 0x400;
    ctx.lr = 0xAABBCCDDEEFF1133ULL;
    const auto regs = std::array{&ctx.f14,&ctx.f15,&ctx.f16,&ctx.f17,
                                 &ctx.f18,&ctx.f19,&ctx.f20,&ctx.f21,
                                 &ctx.f22,&ctx.f23,&ctx.f24,&ctx.f25,
                                 &ctx.f26,&ctx.f27,&ctx.f28,&ctx.f29,
                                 &ctx.f30,&ctx.f31};
    for (unsigned i=0;i<regs.size();++i)
        regs[i]->u64=0x7FF8000000000010ULL+i;
    dotm::ppc::save_fpr(ctx,memory.data(),28);
    CHECK(dotm::ppc::load_be64(memory.data(),0x400-0x90+14*8)==
          0x7FF800000000001EULL);
    CHECK(memory[0x400-0x90]==0);
    for (auto* r:regs) r->u64=0;
    dotm::ppc::restore_fpr(ctx,memory.data(),28);
    for (unsigned i=0;i<regs.size();++i)
        CHECK(regs[i]->u64==(i<14?0ULL:0x7FF8000000000010ULL+i));
    CHECK(ctx.lr==0xAABBCCDDEEFF1133ULL);
    for (unsigned i=0;i<regs.size();++i)
        regs[i]->u64=0x3FF0000000000000ULL+i;
    dotm::ppc::save_fpr(ctx,memory.data(),14);
    CHECK(memory[0x400-0x90]==0x3F);
    for (auto* r:regs) r->u64=0;
    dotm::ppc::restore_fpr(ctx,memory.data(),14);
    for (unsigned i=0;i<regs.size();++i)
        CHECK(regs[i]->u64==0x3FF0000000000000ULL+i);
    bool rejected=false;
    try { dotm::ppc::save_fpr(ctx,memory.data(),13); }
    catch (const std::invalid_argument&) {rejected=true;}
    CHECK(rejected);
    rejected=false;
    try { dotm::ppc::restore_fpr(ctx,memory.data(),32); }
    catch (const std::invalid_argument&) {rejected=true;}
    CHECK(rejected);
}
