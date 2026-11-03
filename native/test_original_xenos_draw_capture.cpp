#include "original_xenos_draw_capture.hpp"
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <stdexcept>
#include <string>

int main() {
    namespace fs = std::filesystem;
    const auto temp = fs::temp_directory_path() /
        ("dotm_original_draw_capture_" + std::to_string(
            std::chrono::steady_clock::now().time_since_epoch().count()));
    struct Cleanup {
        fs::path path;
        ~Cleanup() { std::error_code e; fs::remove_all(path, e); }
    } cleanup{temp};
    // An existing capture directory must not permanently consume a capture.
    const auto retry_root = temp / "retry";
    fs::create_directories(retry_root / "first-original-draw");
    dotm::OriginalXenosDrawCapture retry_capture(retry_root);
    dotm::OriginalXenosDrawCapture capture(temp);
    dotm::GpuDrawCommand draw;
    draw.initiator = 0x00030088U;
    draw.primitive_type = 8U;
    draw.source_select = 2U;
    draw.index_count = 3U;
    draw.vertex_shader_words = {0x12345678U, 0xAABBCCDDU};
    draw.pixel_shader_words = {0x01020304U};
    draw.register_words = {0x89ABCDEFU};
    draw.vertex_fetch_constants.push_back({1U, 0x200000U, 4U, 2U});
    bool first_attempt_failed = false;
    try { retry_capture.submit(draw); }
    catch (const std::runtime_error&) { first_attempt_failed = true; }
    if (!first_attempt_failed)
        throw std::runtime_error("Existing snapshot must be refused");
    fs::remove(retry_root / "first-original-draw");
    if (retry_capture.submit(draw))
        throw std::runtime_error("Capture retry must not acknowledge rendering");
    if (!fs::exists(retry_root / "first-original-draw" / "draw.json"))
        throw std::runtime_error("Failed capture incorrectly consumed retry");
    if (dotm::OriginalXenosDrawCapture::callback(draw, &capture))
        throw std::runtime_error("Capture must never acknowledge a draw");
    const auto source = temp / "first-original-draw";
    auto bytes = [&](const fs::path& path) {
        std::ifstream in(path, std::ios::binary);
        if (!in) throw std::runtime_error("Original draw snapshot missing");
        return std::string(std::istreambuf_iterator<char>(in), {});
    };
    const auto vs = bytes(source / "vertex_shader.be");
    if (vs.size() != 8U || static_cast<std::uint8_t>(vs[0]) != 0x12U ||
        static_cast<std::uint8_t>(vs[3]) != 0x78U ||
        static_cast<std::uint8_t>(vs[4]) != 0xAAU)
        throw std::runtime_error("Original shader big-endian bytes changed");
    const auto metadata = bytes(source / "draw.json");
    if (metadata.find("\"primitive_type\": 8") == std::string::npos ||
        metadata.find("\"slot\":1") == std::string::npos)
        throw std::runtime_error("Original draw metadata was lost");
    if (dotm::OriginalXenosDrawCapture::callback(draw, &capture))
        throw std::runtime_error("Duplicate capture must not acknowledge a draw");
    dotm::OriginalXenosDrawCapture other(temp);
    bool refused = false;
    try { other.submit(draw); }
    catch (const std::runtime_error&) { refused = true; }
    if (!refused)
        throw std::runtime_error("Snapshot must never be overwritten");
    return 0;
}
