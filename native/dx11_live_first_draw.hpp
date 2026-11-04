#pragma once
#ifdef _WIN32
#include "gpu_engines.hpp"
#include <filesystem>
#include <memory>

namespace dotm {
// Hook into LIVE original PPC/PM4 draw submission, not saved draw replay.
// The first original shader pair must already have been translated privately.
class Dx11LiveFirstDraw {
public:
    Dx11LiveFirstDraw(std::filesystem::path hlsl_dir,
                      std::filesystem::path gpu_bmp = {});
    ~Dx11LiveFirstDraw();
    Dx11LiveFirstDraw(const Dx11LiveFirstDraw&) = delete;
    Dx11LiveFirstDraw& operator=(const Dx11LiveFirstDraw&) = delete;
    bool submit(const GpuDrawCommand& draw);
    void pump_messages();
    void request_swap(const GpuSwapRequest& request);
    static void swap_callback(const GpuSwapRequest& request, void* context);
    static bool callback(const GpuDrawCommand& draw, void* context);
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}
#endif
