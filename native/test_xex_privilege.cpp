#include "guest_memory.hpp"
#include "ppc_recomp_shared.h"
#include "xex_optional_header.hpp"
#include "xex_privilege.hpp"
#include <cstdint>
#include <stdexcept>
namespace {
void check(bool ok) {
    if (!ok) throw std::runtime_error("Original Xbox XEX privilege PPC ABI failure");
}
void put_be32(dotm::GuestMemory& m, std::uint32_t a, std::uint32_t value) {
    auto* b = m.address(a);
    b[0] = std::uint8_t(value >> 24);
    b[1] = std::uint8_t(value >> 16);
    b[2] = std::uint8_t(value >> 8);
    b[3] = std::uint8_t(value);
}
}
int main() {
    dotm::GuestMemory memory;
    constexpr std::uint32_t header = 0x81000000U;
    memory.commit(header, 4096);
    put_be32(memory, header, 0x58455832U);
    put_be32(memory, header + 8, 0x200);
    put_be32(memory, header + 20, 1);
    put_be32(memory, header + 24, 0x00030000U);
    put_be32(memory, header + 28, 0x00000600U);
    dotm::bind_xex_optional_header(&memory);
    PPCContext c{};
    c.r3.u64 = 9;
    __imp__XexCheckExecutablePrivilege(c, memory.base());
    check(c.r3.u64 == 1);
    c.r3.u64 = 10;
    __imp__XexCheckExecutablePrivilege(c, memory.base());
    check(c.r3.u64 == 1);
    c.r3.u64 = 8;
    __imp__XexCheckExecutablePrivilege(c, memory.base());
    check(c.r3.u64 == 0);
    c.r3.u64 = 32;
    __imp__XexCheckExecutablePrivilege(c, memory.base());
    check(c.r3.u64 == 0);
    c.r3.u64 = 0xffffffffU;
    __imp__XexCheckExecutablePrivilege(c, memory.base());
    check(c.r3.u64 == 0);
    put_be32(memory, header + 28, 0x00000001U);
    check(dotm::bound_xex_check_executable_privilege(0) == 1);
    check(dotm::bound_xex_check_executable_privilege(9) == 0);
    put_be32(memory, header + 20, 0);
    check(dotm::bound_xex_check_executable_privilege(0) == 0);
    dotm::bind_xex_optional_header(nullptr);
    return 0;
}
