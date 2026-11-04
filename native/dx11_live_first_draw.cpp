#ifdef _WIN32
#define NOMINMAX
#include "dx11_live_first_draw.hpp"
#include "live_xenos_first_draw.hpp"
#include <windows.h>
#include <d3d11.h>
#include <d3dcompiler.h>
#include <wrl/client.h>
#include <array>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <vector>

using Microsoft::WRL::ComPtr;
namespace dotm {
namespace {
void check(HRESULT hr, const char* operation) {
    if (FAILED(hr)) throw std::runtime_error(operation);
}
std::string private_hlsl(const std::filesystem::path& path) {
    std::ifstream stream(path, std::ios::binary);
    if (!stream) throw std::runtime_error("Missing privately translated ORIGINAL HLSL shader");
    return {std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>()};
}
void replace_all(std::string& text, const std::string& before,
                 const std::string& after) {
    std::size_t pos = 0;
    while ((pos = text.find(before, pos)) != std::string::npos) {
        text.replace(pos, before.size(), after);
        pos += after.size();
    }
}
ComPtr<ID3DBlob> compile_source(std::string src, const char* target,
                               const char* label) {
    replace_all(src, ", space4", "");
    // XenosRecomp emits Vulkan location decorations for the same semantic
    // inputs that the native D3D11 input layout binds by HLSL semantic.
    // Remove metadata only: the original translated math remains untouched.
    std::size_t decoration = 0;
    while ((decoration = src.find("[[vk::", decoration)) != std::string::npos) {
        const auto end = src.find("]]", decoration);
        if (end == std::string::npos)
            throw std::runtime_error("Malformed XenosRecomp Vulkan semantic decoration");
        src.erase(decoration, end + 2 - decoration);
    }
    replace_all(src, "[shader(\"pixel\")]", "");
    replace_all(src, "[shader(\"vertex\")]", "");
    ComPtr<ID3DBlob> compiled, diagnostics;
    const HRESULT hr = D3DCompile(src.data(), src.size(), label, nullptr,
                                  nullptr, "main", target,
                                  D3DCOMPILE_ENABLE_STRICTNESS, 0,
                                  &compiled, &diagnostics);
    if (FAILED(hr)) {
        const std::string msg = diagnostics ?
            std::string(static_cast<const char*>(diagnostics->GetBufferPointer()),
                        diagnostics->GetBufferSize()) :
            std::string("unknown FXC error");
        throw std::runtime_error(std::string("Original XenosRecomp shader compilation failed: ") + msg);
    }
    return compiled;
}
constexpr const char* kRectangleGs = R"HLSL(
struct V {
    float4 pos : SV_Position;
    float4 t0 : TEXCOORD0; float4 t1 : TEXCOORD1;
    float4 t2 : TEXCOORD2; float4 t3 : TEXCOORD3;
    float4 t4 : TEXCOORD4; float4 t5 : TEXCOORD5;
    float4 t6 : TEXCOORD6; float4 t7 : TEXCOORD7;
    float4 t8 : TEXCOORD8; float4 t9 : TEXCOORD9;
    float4 t10 : TEXCOORD10; float4 t11 : TEXCOORD11;
    float4 t12 : TEXCOORD12; float4 t13 : TEXCOORD13;
    float4 t14 : TEXCOORD14; float4 t15 : TEXCOORD15;
    float4 c0 : COLOR0; float4 c1 : COLOR1;
};
float dist2(V a, V b) {
    float2 d = a.pos.xy / a.pos.w - b.pos.xy / b.pos.w;
    return dot(d, d);
}
V corner(V a, V b, V c) {
    V v;
    v.pos = b.pos + c.pos - a.pos;
    v.t0=b.t0+c.t0-a.t0; v.t1=b.t1+c.t1-a.t1;
    v.t2=b.t2+c.t2-a.t2; v.t3=b.t3+c.t3-a.t3;
    v.t4=b.t4+c.t4-a.t4; v.t5=b.t5+c.t5-a.t5;
    v.t6=b.t6+c.t6-a.t6; v.t7=b.t7+c.t7-a.t7;
    v.t8=b.t8+c.t8-a.t8; v.t9=b.t9+c.t9-a.t9;
    v.t10=b.t10+c.t10-a.t10; v.t11=b.t11+c.t11-a.t11;
    v.t12=b.t12+c.t12-a.t12; v.t13=b.t13+c.t13-a.t13;
    v.t14=b.t14+c.t14-a.t14; v.t15=b.t15+c.t15-a.t15;
    v.c0=b.c0+c.c0-a.c0; v.c1=b.c1+c.c1-a.c1;
    return v;
}
[maxvertexcount(4)]
void main(triangle V v[3], inout TriangleStream<V> stream) {
    float d12=dist2(v[1],v[2]);
    float d20=dist2(v[2],v[0]);
    float d01=dist2(v[0],v[1]);
    uint a=0, b=1, c=2;
    if (d12>d20 && d12>d01) { a=0; b=1; c=2; }
    else if (d20>d01) { a=1; b=2; c=0; }
    else { a=2; b=0; c=1; }
    stream.Append(v[a]); stream.Append(v[b]); stream.Append(v[c]);
    stream.Append(corner(v[a],v[b],v[c]));
    stream.RestartStrip();
}
)HLSL";
ComPtr<ID3DBlob> compile_rectangle_geometry() {
    ComPtr<ID3DBlob> compiled, errors;
    check(D3DCompile(kRectangleGs, std::strlen(kRectangleGs), "XenosRectangle8",
                     nullptr, nullptr, "main", "gs_5_0",
                     D3DCOMPILE_ENABLE_STRICTNESS, 0, &compiled, &errors),
          "Compiling original Xenos rectangle-list geometry expansion");
    return compiled;
}
void save_gpu_draw_bmp(ID3D11Device* device, ID3D11DeviceContext* context,
                       ID3D11Texture2D* surface, const std::filesystem::path& output) {
    D3D11_TEXTURE2D_DESC desc{};
    surface->GetDesc(&desc);
    if (desc.Format != DXGI_FORMAT_R8G8B8A8_UNORM || !desc.Width || !desc.Height ||
        desc.Width > 8192 || desc.Height > 8192)
        throw std::runtime_error("Unsupported original draw screenshot format/size");
    D3D11_TEXTURE2D_DESC staging_desc = desc;
    staging_desc.Usage = D3D11_USAGE_STAGING;
    staging_desc.BindFlags = 0;
    staging_desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    staging_desc.MiscFlags = 0;
    ComPtr<ID3D11Texture2D> staging;
    check(device->CreateTexture2D(&staging_desc, nullptr, &staging),
          "Create original draw GPU readback");
    context->CopyResource(staging.Get(), surface);
    D3D11_MAPPED_SUBRESOURCE mapped{};
    check(context->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &mapped),
          "Map actual original draw GPU readback");
    struct Unmap { ID3D11DeviceContext* c; ID3D11Texture2D* t;
        ~Unmap() { c->Unmap(t, 0); } } unmap{context, staging.Get()};
    if (std::filesystem::exists(output))
        throw std::runtime_error("Refusing to overwrite existing original draw screenshot");
    std::ofstream out(output, std::ios::binary);
    if (!out) throw std::runtime_error("Cannot create original GPU screenshot");
    const auto put16 = [&](std::uint16_t v) {
        const char b[]{char(v), char(v >> 8)};
        out.write(b, 2);
    };
    const auto put32 = [&](std::uint32_t v) {
        const char b[]{char(v), char(v >> 8), char(v >> 16), char(v >> 24)};
        out.write(b, 4);
    };
    const auto payload = std::uint64_t(desc.Width) * desc.Height * 4;
    if (payload > 0xFFFFFFFFU - 54U)
        throw std::runtime_error("First original draw screenshot exceeds BMP bounds");
    out.put('B'); out.put('M'); put32(static_cast<std::uint32_t>(payload + 54));
    put32(0); put32(54); put32(40); put32(desc.Width); put32(desc.Height);
    put16(1); put16(32); put32(0); put32(static_cast<std::uint32_t>(payload));
    put32(0); put32(0); put32(0); put32(0);
    for (std::uint32_t y = desc.Height; y > 0; --y) {
        const auto* row = static_cast<const std::uint8_t*>(mapped.pData) +
                          std::size_t(y - 1) * mapped.RowPitch;
        for (std::uint32_t x = 0; x < desc.Width; ++x) {
            const char bgra[]{char(row[x * 4 + 2]), char(row[x * 4 + 1]),
                              char(row[x * 4]), char(row[x * 4 + 3])};
            out.write(bgra, 4);
        }
    }
    out.close();
    if (!out) throw std::runtime_error("Cannot finish original GPU screenshot");
}

LRESULT CALLBACK live_wndproc(HWND window, UINT message, WPARAM wp, LPARAM lp) {
    if (message == WM_CLOSE) { DestroyWindow(window); return 0; }
    if (message == WM_DESTROY) { PostQuitMessage(0); return 0; }
    if (message == WM_KEYDOWN && wp == VK_ESCAPE) {
        DestroyWindow(window); return 0;
    }
    return DefWindowProcW(window, message, wp, lp);
}
}  // namespace

struct Dx11LiveFirstDraw::Impl {
    explicit Impl(std::filesystem::path hlsl, std::filesystem::path screenshot)
        : hlsl_dir(std::move(hlsl)), bmp(std::move(screenshot)) {}
    std::filesystem::path hlsl_dir, bmp;
    bool stopped = false;
    bool submitted = false;
    HWND window = nullptr;
    UINT width = 0, height = 0;
    ComPtr<ID3D11Device> device;
    ComPtr<ID3D11DeviceContext> context;
    ComPtr<IDXGISwapChain> swap;
    ComPtr<ID3D11Texture2D> surface;
    ComPtr<ID3D11RenderTargetView> target;
    ComPtr<ID3D11VertexShader> vs;
    ComPtr<ID3D11PixelShader> ps;
    ComPtr<ID3D11GeometryShader> gs;
    ComPtr<ID3D11InputLayout> layout;
    ComPtr<ID3D11RasterizerState> raster;
    void initialize() {
        if (device) return;
        const auto vertex = compile_source(
            private_hlsl(hlsl_dir / "vertex.hlsl"), "vs_5_0", "original-live-vertex");
        const auto pixel = compile_source(
            private_hlsl(hlsl_dir / "pixel.hlsl"), "ps_5_0", "original-live-pixel");
        const auto rectangle = compile_rectangle_geometry();
        const auto instance = GetModuleHandleW(nullptr);
        WNDCLASSW cls{};
        cls.hInstance = instance;
        cls.lpfnWndProc = &live_wndproc;
        cls.lpszClassName = L"DOTM_LIVE_ORIGINAL_XENOS_FIRST_DRAW";
        cls.hCursor = LoadCursor(nullptr, IDC_ARROW);
        if (!RegisterClassW(&cls) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS)
            throw std::runtime_error("Register live original Xenos window");
        window = CreateWindowW(cls.lpszClassName,
            L"DOTM original live PM4 GPU draw (NOT full frame)",
            WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT, 1280, 720,
            nullptr, nullptr, instance, nullptr);
        if (!window) throw std::runtime_error("Create live Xenos output window");
        RECT rect{};
        GetClientRect(window, &rect);
        width = static_cast<UINT>(rect.right - rect.left);
        height = static_cast<UINT>(rect.bottom - rect.top);
        DXGI_SWAP_CHAIN_DESC desc{};
        desc.BufferDesc.Width = width;
        desc.BufferDesc.Height = height;
        desc.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        desc.SampleDesc.Count = 1;
        desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        desc.BufferCount = 1;
        desc.OutputWindow = window;
        desc.Windowed = TRUE;
        desc.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;
        D3D_FEATURE_LEVEL level{};
        check(D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_HARDWARE,
              nullptr, 0, nullptr, 0, D3D11_SDK_VERSION, &desc,
              &swap, &device, &level, &context),
              "Create native D3D11 live Xenos device and swapchain");
        if (level < D3D_FEATURE_LEVEL_11_0)
            throw std::runtime_error("Live original Xenos rectangle needs D3D11 FL11");
        check(swap->GetBuffer(0, IID_PPV_ARGS(&surface)), "Get live Xenos surface");
        check(device->CreateRenderTargetView(surface.Get(), nullptr, &target),
              "Create live Xenos render target");
        check(device->CreateVertexShader(vertex->GetBufferPointer(),
              vertex->GetBufferSize(), nullptr, &vs), "Create original live VS");
        check(device->CreatePixelShader(pixel->GetBufferPointer(),
              pixel->GetBufferSize(), nullptr, &ps), "Create original live PS");
        check(device->CreateGeometryShader(rectangle->GetBufferPointer(),
              rectangle->GetBufferSize(), nullptr, &gs), "Create Xenos rectangle GS");
        // The exact, verified first retail draw fetches POSITION0 and COLOR0
        // from 3 live 28-byte Xenos vertices, decoded by its fetch endian mode.
        constexpr D3D11_INPUT_ELEMENT_DESC input[] = {
            {"POSITION", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 0,
             D3D11_INPUT_PER_VERTEX_DATA, 0},
            {"COLOR", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 16,
             D3D11_INPUT_PER_VERTEX_DATA, 0},
        };
        check(device->CreateInputLayout(input, 2, vertex->GetBufferPointer(),
              vertex->GetBufferSize(), &layout), "Create original live VS input layout");
        D3D11_RASTERIZER_DESC rs{};
        rs.FillMode = D3D11_FILL_SOLID;
        rs.CullMode = D3D11_CULL_NONE;
        rs.DepthClipEnable = TRUE;
        check(device->CreateRasterizerState(&rs, &raster),
              "Create live Xenos rasterizer");
        ShowWindow(window, SW_SHOW);
    }
    bool submit(const GpuDrawCommand& draw) {
        if (stopped || !is_original_first_xenos_draw(draw)) return false;
        // The caller holds the GPU register lock and grants a read window only
        // for this callback. Copy guest physical vertex data synchronously.
        const auto vertices = read_original_first_xenos_vertices(draw);
        initialize();
        D3D11_BUFFER_DESC vertex_desc{};
        vertex_desc.ByteWidth = static_cast<UINT>(vertices.size());
        vertex_desc.Usage = D3D11_USAGE_IMMUTABLE;
        vertex_desc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
        D3D11_SUBRESOURCE_DATA init{};
        init.pSysMem = vertices.data();
        ComPtr<ID3D11Buffer> vertex_buffer;
        check(device->CreateBuffer(&vertex_desc, &init, &vertex_buffer),
              "Upload LIVE original Xenos vertex DMA");
        const UINT stride = 28U, offset = 0U;
        D3D11_VIEWPORT viewport{};
        viewport.Width = static_cast<float>(width);
        viewport.Height = static_cast<float>(height);
        viewport.MinDepth = 0.f; viewport.MaxDepth = 1.f;
        context->IASetInputLayout(layout.Get());
        context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        context->IASetVertexBuffers(0, 1, vertex_buffer.GetAddressOf(), &stride, &offset);
        context->VSSetShader(vs.Get(), nullptr, 0);
        context->GSSetShader(gs.Get(), nullptr, 0);
        context->PSSetShader(ps.Get(), nullptr, 0);
        context->RSSetViewports(1, &viewport);
        context->RSSetState(raster.Get());
        context->OMSetRenderTargets(1, target.GetAddressOf(), nullptr);
        if (!submitted) {
            const float clear[] = {0, 0, 0, 1};
            context->ClearRenderTargetView(target.Get(), clear);
        }
        // This Draw(3) consists exclusively of ORIGINAL live PM4 vertices and
        // shaders, expanded to the correct Xenos rectangle by the GS.
        context->Draw(3U, 0U);
        context->Flush();
        if (!bmp.empty() && !submitted)
            save_gpu_draw_bmp(device.Get(), context.Get(), surface.Get(), bmp);
        check(swap->Present(1U, 0U), "Present live original PM4 draw");
        submitted = true;
        return true; // Acknowledge PM4 only after real native GPU submission.
    }
    void pump() {
        if (!window || stopped) return;
        MSG msg{};
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
            if (msg.message == WM_QUIT) { stopped = true; break; }
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }
};
Dx11LiveFirstDraw::Dx11LiveFirstDraw(std::filesystem::path hlsl,
                                     std::filesystem::path gpu_bmp)
    : impl_(std::make_unique<Impl>(std::move(hlsl), std::move(gpu_bmp))) {}
Dx11LiveFirstDraw::~Dx11LiveFirstDraw() = default;
bool Dx11LiveFirstDraw::submit(const GpuDrawCommand& draw) {
    return impl_->submit(draw);
}
void Dx11LiveFirstDraw::pump_messages() { impl_->pump(); }
bool Dx11LiveFirstDraw::callback(const GpuDrawCommand& draw, void* context) {
    return context && static_cast<Dx11LiveFirstDraw*>(context)->submit(draw);
}
} // namespace dotm
#else
#error Live Xenos native DX11 pipeline is Windows-only.
#endif
