#include "guest_memory.hpp"
#include "ppc_recomp_shared.h"
#include "xex_optional_header.hpp"
#include <cstdint>
#include <stdexcept>
namespace {
void check(bool test) {
    if (!test) throw std::runtime_error("XEX2 optional field PPC ABI regression");
}
void write_be32(dotm::GuestMemory& memory, std::uint32_t addr, std::uint32_t value) {
    auto* p = memory.address(addr);
    p[0] = std::uint8_t(value >> 24);
    p[1] = std::uint8_t(value >> 16);
    p[2] = std::uint8_t(value >> 8);
    p[3] = std::uint8_t(value);
}
}
int main() {
    dotm::GuestMemory memory;
    constexpr std::uint32_t base = 0x81020000;
    constexpr std::uint32_t entry_key = 0x00010100; // immediate entry point
    constexpr std::uint32_t image_key = 0x00010201; // pointer to inline value
    constexpr std::uint32_t execution_key = 0x00040006; // offset
    memory.commit(base, 4096);
    write_be32(memory, base, 0x58455832U);
    write_be32(memory, base + 8, 0x200);
    write_be32(memory, base + 20, 3);
    write_be32(memory, base + 24, entry_key);
    write_be32(memory, base + 28, 0x8277F670);
    write_be32(memory, base + 32, image_key);
    write_be32(memory, base + 36, 0x82000000);
    write_be32(memory, base + 40, execution_key);
    write_be32(memory, base + 44, 0x100);
    write_be32(memory, base + 0x100 + 12, 0x41560890); // sample title id
    dotm::bind_xex_optional_header(&memory);
    PPCContext c{};
    c.r3.u32 = base; c.r4.u32 = entry_key;
    __imp__RtlImageXexHeaderField(c, memory.base());
    check(c.r3.u64 == 0x8277F670);
    c.r3.u32 = base; c.r4.u32 = image_key;
    __imp__RtlImageXexHeaderField(c, memory.base());
    check(c.r3.u64 == base + 36);
    c.r3.u32 = base; c.r4.u32 = execution_key;
    __imp__RtlImageXexHeaderField(c, memory.base());
    check(c.r3.u64 == base + 0x100);
    c.r3.u32 = base; c.r4.u32 = 0xDEADBEEF;
    __imp__RtlImageXexHeaderField(c, memory.base());
    check(c.r3.u64 == 0);
    write_be32(memory, base + 44, 0x1000);
    bool threw = false;
    try { dotm::xex_optional_header_field(memory, base, execution_key); }
    catch (const std::runtime_error&) { threw = true; }
    check(threw);
    write_be32(memory, base + 44, 0x100);
    write_be32(memory, base + 20, 5000);
    threw = false;
    try { dotm::xex_optional_header_field(memory, base, entry_key); }
    catch (const std::runtime_error&) { threw = true; }
    check(threw);
    write_be32(memory, base + 20, 3);
    check(dotm::xex_optional_header_field(memory, 0, entry_key) == 0);
    threw = false;
    try { dotm::xex_optional_header_field(memory, base + 4096, entry_key); }
    catch (const std::runtime_error&) { threw = true; }
    check(threw);
    dotm::bind_xex_optional_header(nullptr);
    return 0;
}
