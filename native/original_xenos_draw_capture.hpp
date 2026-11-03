#pragma once
#include "gpu_engines.hpp"
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace dotm {
// Private capture of the *original* draw state. Never acknowledges rendering,
// fabricates a frame, or publishes copyrighted guest shaders into public CI.
class OriginalXenosDrawCapture {
public:
    explicit OriginalXenosDrawCapture(std::filesystem::path destination)
        : destination_(std::move(destination)) {}

    bool submit(const GpuDrawCommand& draw) {
        if (captured_) return false;
        captured_ = true;
        std::filesystem::create_directories(destination_);
        const auto snapshot = destination_ / "first-original-draw";
        if (!std::filesystem::create_directory(snapshot))
            throw std::runtime_error("Refusing to replace existing original Xenos draw capture");
        try {
            write_words(snapshot / "vertex_shader.be", draw.vertex_shader_words);
            write_words(snapshot / "pixel_shader.be", draw.pixel_shader_words);
            write_words(snapshot / "registers.be", draw.register_words);
            write_bytes(snapshot / "indices.raw", draw.dma_index_bytes);
            write_words(snapshot / "indices.decoded.be", draw.decoded_indices);
            std::ofstream meta(snapshot / "draw.json", std::ios::binary);
            if (!meta) throw std::runtime_error("Cannot create private Xenos draw manifest");
            meta << "{\n  \"initiator\": " << draw.initiator
                 << ",\n  \"primitive_type\": " << draw.primitive_type
                 << ",\n  \"source_select\": " << draw.source_select
                 << ",\n  \"major_mode\": " << draw.major_mode
                 << ",\n  \"index_count\": " << draw.index_count
                 << ",\n  \"index_size_bytes\": " << draw.index_size_bytes
                 << ",\n  \"dma_physical_address\": " << draw.dma_physical_address
                 << ",\n  \"dma_swap_mode\": " << draw.dma_swap_mode
                 << ",\n  \"has_visibility_query_token\": "
                 << (draw.has_visibility_query_token ? "true" : "false")
                 << ",\n  \"visibility_query_token\": " << draw.visibility_query_token
                 << ",\n  \"vertex_shader_dwords\": " << draw.vertex_shader_words.size()
                 << ",\n  \"pixel_shader_dwords\": " << draw.pixel_shader_words.size()
                 << ",\n  \"register_dwords\": " << draw.register_words.size()
                 << ",\n  \"vertex_fetch\": [";
            for (std::size_t i = 0; i < draw.vertex_fetch_constants.size(); ++i) {
                const auto& f = draw.vertex_fetch_constants[i];
                if (i) meta << ',';
                meta << "\n    {\"slot\":" << f.slot
                     << ",\"physical_address\":" << f.physical_address
                     << ",\"size_dwords\":" << f.size_dwords
                     << ",\"endian_mode\":" << f.endian_mode << '}';
            }
            meta << "\n  ],\n  \"texture_fetch\": [";
            for (std::size_t i = 0; i < draw.texture_fetch_constants.size(); ++i) {
                const auto& f = draw.texture_fetch_constants[i];
                if (i) meta << ',';
                meta << "\n    {\"slot\":" << f.slot
                     << ",\"base_physical_address\":" << f.base_physical_address
                     << ",\"mip_physical_address\":" << f.mip_physical_address
                     << ",\"format\":" << f.format
                     << ",\"width\":" << f.width
                     << ",\"height\":" << f.height
                     << ",\"pitch_pixels\":" << f.pitch_pixels
                     << ",\"tiled\":" << (f.tiled ? "true" : "false")
                     << ",\"endian_mode\":" << f.endian_mode << '}';
            }
            meta << "\n  ]\n}\n";
            meta.close();
            if (!meta) throw std::runtime_error("Cannot finalize private Xenos draw manifest");
        } catch (...) {
            std::error_code ignored;
            std::filesystem::remove_all(snapshot, ignored);
            throw;
        }
        return false;  // It is not a renderer; leave PM4 draw unacknowledged.
    }

    static bool callback(const GpuDrawCommand& draw, void* context) {
        return context && static_cast<OriginalXenosDrawCapture*>(context)->submit(draw);
    }
private:
    static void write_words(const std::filesystem::path& path,
                            const std::vector<std::uint32_t>& words) {
        std::ofstream out(path, std::ios::binary);
        if (!out) throw std::runtime_error("Cannot create private Xenos DWORD snapshot");
        for (const auto value : words) {
            const char bytes[4] = {static_cast<char>(value >> 24),
                                   static_cast<char>(value >> 16),
                                   static_cast<char>(value >> 8),
                                   static_cast<char>(value)};
            out.write(bytes, sizeof(bytes));
        }
        out.close();
        if (!out) throw std::runtime_error("Cannot finalize private Xenos DWORD snapshot");
    }
    static void write_bytes(const std::filesystem::path& path,
                            const std::vector<std::uint8_t>& bytes) {
        std::ofstream out(path, std::ios::binary);
        if (!out) throw std::runtime_error("Cannot create private Xenos byte snapshot");
        if (!bytes.empty())
            out.write(reinterpret_cast<const char*>(bytes.data()),
                      static_cast<std::streamsize>(bytes.size()));
        out.close();
        if (!out) throw std::runtime_error("Cannot finalize private Xenos byte snapshot");
    }
    std::filesystem::path destination_;
    bool captured_ = false;
};
}  // namespace dotm
