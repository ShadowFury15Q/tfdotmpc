#include "ppc_recomp_shared.h"
#include "kernel_file_io.hpp"
#include "kernel_mutant.hpp"
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>

PPC_EXTERN_FUNC(__imp__NtCreateFile);
PPC_EXTERN_FUNC(__imp__NtReadFile);
PPC_EXTERN_FUNC(__imp__NtQueryInformationFile);
PPC_EXTERN_FUNC(__imp__NtClose);
namespace {
void check(bool ok, const char* message) {
    if (!ok) throw std::runtime_error(message);
}
void put32(dotm::GuestMemory& m, std::uint32_t a, std::uint32_t v) {
    auto* p = m.address(a);
    for (int i=0; i<4; ++i) p[i] = std::uint8_t(v >> (24 - 8*i));
}
std::uint32_t get32(dotm::GuestMemory& m, std::uint32_t a) {
    auto* p = m.address(a);
    return (std::uint32_t(p[0]) << 24) | (std::uint32_t(p[1]) << 16) |
        (std::uint32_t(p[2]) << 8) | p[3];
}
}
int main() {
    try {
        namespace fs = std::filesystem;
        const auto root = fs::temp_directory_path() /
            ("dotm_nt_file_test_" + std::to_string(
                std::chrono::steady_clock::now().time_since_epoch().count()));
        fs::create_directories(root / "TransGame");
        struct Cleanup {fs::path p; ~Cleanup() {std::error_code ec; fs::remove_all(p, ec);}} cleanup{root};
        {
            std::ofstream out(root / "TransGame" / "Xbox360TOC.txt", std::ios::binary);
            out << "Original fixture bytes";
        }
        dotm::GuestMemory m;
        constexpr std::uint32_t a = 0x40000000U;
        m.commit(a, 4096);
        const auto handle_out = a + 0x10;
        const auto iosb = a + 0x20;
        const auto attrs = a + 0x30;
        const auto str = a + 0x40;
        const auto name = a + 0x50;
        const auto stack = a + 0x200;
        const auto buffer = a + 0x300;
        const std::string guest = "D:\\transgame\\xbox360toc.TXT";
        put32(m, attrs, 0xFFFFFFFDU);
        put32(m, attrs + 4, str);
        put32(m, attrs + 8, 0x40U);
        m.address(str)[0] = 0; m.address(str)[1] = std::uint8_t(guest.size());
        m.address(str)[2] = 0; m.address(str)[3] = std::uint8_t(guest.size()+1);
        put32(m, str + 4, name);
        for (std::size_t i=0;i<guest.size();++i) m.address(name)[i] = guest[i];
        put32(m, stack + 84, 0x64U);
        dotm::bind_kernel_file_io(&m, root);
        PPCContext ctx{};
        ctx.r1.u64 = stack;
        ctx.r3.u64 = handle_out; ctx.r4.u64 = 0x80100080U;
        ctx.r5.u64 = attrs; ctx.r6.u64 = iosb; ctx.r7.u64 = 0;
        ctx.r8.u64 = 1; ctx.r9.u64 = 1; ctx.r10.u64 = 1;
        __imp__NtCreateFile(ctx, m.base());
        check(ctx.r3.u32 == 0 && get32(m, iosb) == 0 &&
              get32(m, iosb+4) == 1, "NtCreateFile opens real host-backed title file");
        const auto handle = get32(m, handle_out);
        check(handle >= 0xF9000000U && handle < 0xFA000000U, "File handle namespace");
        // Original title first requests FILE_NETWORK_OPEN_INFORMATION (34).
        ctx.r3.u64 = handle; ctx.r4.u64 = iosb; ctx.r5.u64 = buffer;
        ctx.r6.u64 = 56; ctx.r7.u64 = 34;
        __imp__NtQueryInformationFile(ctx, m.base());
        const auto length = (std::uint64_t(get32(m, buffer + 40)) << 32) |
            get32(m, buffer + 44);
        check(ctx.r3.u32 == 0 && length == 22 &&
              get32(m, buffer + 48) == 0x80 && get32(m, iosb+4) == 56,
              "NtQueryInformationFile returns real host file size and Xbox metadata");
        ctx.r3.u64 = handle; ctx.r4.u64 = iosb; ctx.r5.u64 = buffer;
        ctx.r6.u64 = 24; ctx.r7.u64 = 5;
        __imp__NtQueryInformationFile(ctx, m.base());
        check(ctx.r3.u32 == 0 && get32(m, buffer + 12) == 22 &&
              get32(m, iosb+4) == 24,
              "FileStandardInformation returns exact file length");
        put32(m, stack + 84, 0);
        ctx.r3.u64 = handle; ctx.r4.u64 = 0; ctx.r5.u64 = 0;
        ctx.r6.u64 = 0; ctx.r7.u64 = iosb; ctx.r8.u64 = buffer;
        ctx.r9.u64 = 8; ctx.r10.u64 = 0;
        __imp__NtReadFile(ctx, m.base());
        check(ctx.r3.u32 == 0 && get32(m, iosb+4) == 8 &&
              std::string(reinterpret_cast<char*>(m.address(buffer)), 8) == "Original",
              "NtReadFile transfers actual bytes and advances position");
        // The ninth NtReadFile argument is a PULONG Key. Original DOTM
        // supplies a non-null pointer for synchronous file reads.
        put32(m, stack + 84, a + 0x90);
        put32(m, a + 0x90, 0x83940000U);
        put32(m, a + 0xA0, 0);
        put32(m, a + 0xA4, 0);
        ctx.r3.u64 = handle; ctx.r9.u64 = 8; ctx.r10.u64 = a + 0xA0;
        __imp__NtReadFile(ctx, m.base());
        check(ctx.r3.u32 == 0 && get32(m, iosb + 4) == 8 &&
              get32(m, a + 0x90) == 0x83940000U &&
              std::string(reinterpret_cast<char*>(m.address(buffer)), 8) == "Original",
              "NtReadFile accepts a valid non-null PPC Key pointer");
        put32(m, stack + 84, 0x3000U);
        ctx.r3.u64 = handle;
        __imp__NtReadFile(ctx, m.base());
        check(ctx.r3.u32 == 0xC000000DU,
              "NtReadFile rejects an unmapped Key pointer");
        put32(m, stack + 84, 0);
        ctx.r10.u64 = 0;
        ctx.r3.u64 = handle; ctx.r9.u64 = 0;
        __imp__NtReadFile(ctx, m.base());
        check(ctx.r3.u32 == 0 && get32(m, iosb+4) == 0,
              "Zero-length read must not reuse previous gcount");
        ctx.r3.u64 = handle; ctx.r9.u64 = 30;
        __imp__NtReadFile(ctx, m.base());
        check(ctx.r3.u32 == 0 && get32(m, iosb+4) == 14,
              "Short EOF read returns actual remaining bytes");
        ctx.r3.u64 = handle; ctx.r9.u64 = 1;
        __imp__NtReadFile(ctx, m.base());
        check(ctx.r3.u32 == 0xC0000011U && get32(m, iosb+4) == 0,
              "Read past EOF returns STATUS_END_OF_FILE");
        ctx.r3.u64 = handle;
        __imp__NtClose(ctx, m.base());
        check(ctx.r3.u32 == 0, "NtClose releases host file handle");
        ctx.r3.u64 = handle; ctx.r9.u64 = 1;
        __imp__NtReadFile(ctx, m.base());
        check(ctx.r3.u32 == 0xC0000008U, "Closed handle rejected");
        // Traversal must not allow access outside the mounted title directory.
        const std::string escape = "D:\\..\\outside.txt";
        m.address(str)[1] = std::uint8_t(escape.size());
        m.address(str)[3] = std::uint8_t(escape.size()+1);
        for (std::size_t i=0;i<escape.size();++i) m.address(name)[i] = escape[i];
        put32(m, stack + 84, 0x64U);
        ctx.r3.u64 = handle_out; ctx.r4.u64 = 0x80100080U;
        ctx.r5.u64 = attrs; ctx.r6.u64 = iosb; ctx.r7.u64 = 0;
        ctx.r8.u64 = 1; ctx.r9.u64 = 1; ctx.r10.u64 = 1;
        __imp__NtCreateFile(ctx, m.base());
        check(ctx.r3.u32 == 0xC0000034U, "Guest traversal rejected");
        // An absent disc mount is a host setup error, not a successful
        // guest file lookup. Fail with the exact original path, not a SIGSEGV.
        dotm::bind_kernel_file_io(&m, "");
        m.address(str)[1] = std::uint8_t(guest.size());
        m.address(str)[3] = std::uint8_t(guest.size()+1);
        for (std::size_t i=0;i<guest.size();++i) m.address(name)[i] = guest[i];
        ctx.r3.u64 = handle_out;
        bool missing_mount_reported = false;
        try { __imp__NtCreateFile(ctx, m.base()); }
        catch (const std::runtime_error& e) {
            missing_mount_reported = std::string(e.what()).find("xbox360toc.TXT") != std::string::npos;
        }
        check(missing_mount_reported, "Unbound disc mount reports exact original title path");
        dotm::bind_kernel_file_io(nullptr, "");
        std::cout << "Xbox 360 real read-only title file I/O PPC ABI PASS\n";
        return 0;
    } catch (const std::exception& e) {
        dotm::bind_kernel_file_io(nullptr, "");
        std::cerr << "FAIL: " << e.what() << '\n';
        return 1;
    }
}
