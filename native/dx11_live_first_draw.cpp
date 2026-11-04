// Live original PPC/PM4 renderer: multiple shader pairs and draw calls are
// dispatched through a GPU pipeline cache. Unsupported guest state fails closed.
// DX11 is a limited host backend; full Xenos EDRAM/texture parity is not claimed.
#ifdef _WIN32
#define NOMINMAX
#include "dx11_live_first_draw.hpp"
#include "live_xenos_first_draw.hpp"
#include "xenos_draw_pipeline.hpp"
#include <windows.h>
#include <d3d11.h>
#include <d3dcompiler.h>
#include <d3d11shader.h>
#include <wrl/client.h>
#include <algorithm>
#include <atomic>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

using Microsoft::WRL::ComPtr;
namespace dotm {
namespace {
void check(HRESULT hr, const char* operation) {
    if (FAILED(hr)) throw std::runtime_error(operation);
}
std::string private_hlsl(const std::filesystem::path& path) {
    std::ifstream stream(path, std::ios::binary);
    if (!stream) throw std::runtime_error(
        "Missing original privately translated Xenos shader: " + path.string());
    return {std::istreambuf_iterator<char>(stream),
            std::istreambuf_iterator<char>()};
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


D3D11_PRIMITIVE_TOPOLOGY map_topology(XenosHostTopology topology) {
    switch (topology) {
    case XenosHostTopology::point_list: return D3D11_PRIMITIVE_TOPOLOGY_POINTLIST;
    case XenosHostTopology::line_list: return D3D11_PRIMITIVE_TOPOLOGY_LINELIST;
    case XenosHostTopology::line_strip: return D3D11_PRIMITIVE_TOPOLOGY_LINESTRIP;
    case XenosHostTopology::triangle_list: return D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
    case XenosHostTopology::triangle_strip: return D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP;
    case XenosHostTopology::rectangle_list: return D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
    }
    throw std::runtime_error("Unhandled original Xenos GPU topology");
}
DXGI_FORMAT map_format(XenosInputFormat fmt) {
    switch (fmt) {
    case XenosInputFormat::float1: return DXGI_FORMAT_R32_FLOAT;
    case XenosInputFormat::float2: return DXGI_FORMAT_R32G32_FLOAT;
    case XenosInputFormat::float3: return DXGI_FORMAT_R32G32B32_FLOAT;
    case XenosInputFormat::float4: return DXGI_FORMAT_R32G32B32A32_FLOAT;
    case XenosInputFormat::unorm8x4: return DXGI_FORMAT_R8G8B8A8_UNORM;
    case XenosInputFormat::uint8x4: return DXGI_FORMAT_R8G8B8A8_UINT;
    case XenosInputFormat::sint16x2: return DXGI_FORMAT_R16G16_SINT;
    case XenosInputFormat::unorm16x2: return DXGI_FORMAT_R16G16_UNORM;
    case XenosInputFormat::float16x2: return DXGI_FORMAT_R16G16_FLOAT;
    case XenosInputFormat::sint16x4: return DXGI_FORMAT_R16G16B16A16_SINT;
    case XenosInputFormat::unorm16x4: return DXGI_FORMAT_R16G16B16A16_UNORM;
    case XenosInputFormat::float16x4: return DXGI_FORMAT_R16G16B16A16_FLOAT;
    }
    throw std::runtime_error("Unsupported original Xenos vertex format");
}
// Vertex/pixel shaders referencing unbound textures, constant buffers or
// samplers must never be submitted as if they were valid original game output.
void reject_missing_shader_resources(ID3DBlob* blob) {
    ComPtr<ID3D11ShaderReflection> reflection;
    check(D3DReflect(blob->GetBufferPointer(), blob->GetBufferSize(),
                     IID_ID3D11ShaderReflection,
                     reinterpret_cast<void**>(reflection.GetAddressOf())),
          "Reflect original translated shader resources");
    D3D11_SHADER_DESC desc{};
    check(reflection->GetDesc(&desc), "Describe original translated shader");
    if (desc.BoundResources)
        throw std::runtime_error(
            "Original shader uses Xenos resources not yet translated to DX11");
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
    struct CachedPipeline {
        XenosShaderPair key{};
        XenosVertexLayout layout;
        ComPtr<ID3D11VertexShader> vs;
        ComPtr<ID3D11PixelShader> ps;
        ComPtr<ID3D11InputLayout> input;
    };
    explicit Impl(std::filesystem::path root, std::filesystem::path output)
        : shader_root(std::move(root)), bmp(std::move(output)) {}
    std::filesystem::path shader_root, bmp;
    bool stopped = false, wrote_bmp = false;
    std::uint64_t pending_draw_count = 0;
    std::uint64_t last_presented_request = 0;
    std::atomic<std::uint64_t> requested_swaps{0};
    HWND window = nullptr;
    UINT width = 0, height = 0;
    ComPtr<ID3D11Device> device;
    ComPtr<ID3D11DeviceContext> context;
    ComPtr<IDXGISwapChain> swap;
    ComPtr<ID3D11Texture2D> surface;
    ComPtr<ID3D11RenderTargetView> target;
    ComPtr<ID3D11GeometryShader> rectangle_gs;
    ComPtr<ID3D11RasterizerState> raster;
    std::map<XenosShaderPair, CachedPipeline> pipelines;

    void initialize() {
        if (device) return;
        const HINSTANCE instance = GetModuleHandleW(nullptr);
        WNDCLASSW cls{};
        cls.hInstance = instance;
        cls.lpfnWndProc = &live_wndproc;
        cls.lpszClassName = L"DOTM_LIVE_ORIGINAL_XENOS_MULTI_DRAW";
        cls.hCursor = LoadCursor(nullptr, IDC_ARROW);
        if (!RegisterClassW(&cls) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS)
            throw std::runtime_error("Register original Xenos multi-draw window");
        window = CreateWindowW(cls.lpszClassName,
            L"DOTM native original PM4 draw stream (incomplete GPU emulation)",
            WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT,
            1280, 720, nullptr, nullptr, instance, nullptr);
        if (!window) throw std::runtime_error("Create original Xenos live output window");
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
              nullptr, 0, nullptr, 0, D3D11_SDK_VERSION,
              &desc, &swap, &device, &level, &context),
              "Create original Xenos streaming DX11 GPU device");
        if (level < D3D_FEATURE_LEVEL_11_0)
            throw std::runtime_error("Original Xenos streaming path needs D3D11 FL11");
        check(swap->GetBuffer(0, IID_PPV_ARGS(&surface)), "Get original render surface");
        check(device->CreateRenderTargetView(surface.Get(), nullptr, &target),
              "Create original Xenos RTV");
        D3D11_RASTERIZER_DESC rs{};
        rs.FillMode = D3D11_FILL_SOLID;
        rs.CullMode = D3D11_CULL_NONE;
        rs.DepthClipEnable = TRUE;
        check(device->CreateRasterizerState(&rs, &raster),
              "Create original Xenos rasterizer");
        ShowWindow(window, SW_SHOW);
    }
    CachedPipeline& pipeline_for(const GpuDrawCommand& draw) {
        const auto key = xenos_shader_pair(draw);
        if (auto found = pipelines.find(key); found != pipelines.end())
            return found->second;
        if (pipelines.size() >= 512)
            throw std::runtime_error("Original Xenos DX11 shader cache limit reached");
        // The verified original shader pair may be reused by many later PM4
        // draws with different index counts and physical vertex allocations.
        // Never tie pipeline selection to a single initiator/first frame.
        const bool known_rectangle_pair =
            key.vertex == 0x539EA9E08AA83F0CULL &&
            key.pixel == 0x438C2AF84C78A133ULL;
        const auto vs_name = xenos_shader_hex(key.vertex);
        const auto ps_name = xenos_shader_hex(key.pixel);
        const auto layout_file = shader_root / "layouts" / (vs_name + ".layout");
        CachedPipeline p;
        p.key = key;
        p.layout = std::filesystem::exists(layout_file)
            ? XenosVertexLayout::load(layout_file)
            : known_rectangle_pair
                ? XenosVertexLayout::original_first_rectangle()
                : throw std::runtime_error(
                    "Missing original Xenos vertex shader reflection/layout: " +
                    layout_file.string());
        auto vs_path = xenos_shader_source(shader_root, "vertex", key.vertex);
        auto ps_path = xenos_shader_source(shader_root, "pixel", key.pixel);
        // Preserve the existing private first-draw XenosRecomp output format
        // only for the verified original first draw's exact shader hashes.
        if (known_rectangle_pair && !std::filesystem::exists(vs_path))
            vs_path = shader_root / "vertex.hlsl";
        if (known_rectangle_pair && !std::filesystem::exists(ps_path))
            ps_path = shader_root / "pixel.hlsl";
        const auto vs_blob = compile_source(private_hlsl(vs_path), "vs_5_0",
                                            "original-xenos-vertex");
        const auto ps_blob = compile_source(private_hlsl(ps_path), "ps_5_0",
                                            "original-xenos-pixel");
        reject_missing_shader_resources(vs_blob.Get());
        reject_missing_shader_resources(ps_blob.Get());
        check(device->CreateVertexShader(vs_blob->GetBufferPointer(),
            vs_blob->GetBufferSize(), nullptr, &p.vs), "Create original Xenos VS");
        check(device->CreatePixelShader(ps_blob->GetBufferPointer(),
            ps_blob->GetBufferSize(), nullptr, &p.ps), "Create original Xenos PS");
        std::vector<D3D11_INPUT_ELEMENT_DESC> elements;
        std::map<std::uint32_t, UINT> slots;
        for (const auto& attribute : p.layout.attributes) {
            if (!slots.count(attribute.fetch_slot))
                slots.emplace(attribute.fetch_slot, static_cast<UINT>(slots.size()));
            elements.push_back({attribute.semantic.c_str(),
                                attribute.semantic_index, map_format(attribute.format),
                                slots.at(attribute.fetch_slot), attribute.byte_offset,
                                D3D11_INPUT_PER_VERTEX_DATA, 0U});
        }
        check(device->CreateInputLayout(elements.data(),
            static_cast<UINT>(elements.size()), vs_blob->GetBufferPointer(),
            vs_blob->GetBufferSize(), &p.input),
            "Create original shader-reflected multi-stream input layout");
        auto [inserted, ok] = pipelines.emplace(key, std::move(p));
        return inserted->second;
    }
    bool submit(const GpuDrawCommand& draw) {
        if (stopped) return false;
        // Never acknowledge unknown original conditions or geometry. Resource
        // captures happen in the *live* GPU callback under its read window.
        initialize();
        auto& pipeline = pipeline_for(draw);
        const auto native = prepare_original_xenos_draw(draw, pipeline.layout);
        if (native.topology == XenosHostTopology::rectangle_list &&
            (native.shaders.vertex != 0x539EA9E08AA83F0CULL ||
             native.shaders.pixel != 0x438C2AF84C78A133ULL))
            throw std::runtime_error(
                "Generic Xenos rectangle shader output reflection not implemented");
        if (native.topology == XenosHostTopology::rectangle_list && !rectangle_gs) {
            const auto shader = compile_rectangle_geometry();
            check(device->CreateGeometryShader(shader->GetBufferPointer(),
                shader->GetBufferSize(), nullptr, &rectangle_gs),
                "Create native Xenos rectangle geometry shader");
        }
        if (pending_draw_count == 0) {
            const float black[] = {0, 0, 0, 1};
            context->ClearRenderTargetView(target.Get(), black);
        }
        D3D11_VIEWPORT vp{};
        vp.Width = static_cast<float>(width);
        vp.Height = static_cast<float>(height);
        vp.MinDepth = 0.f;
        vp.MaxDepth = 1.f;
        context->RSSetViewports(1, &vp);
        context->RSSetState(raster.Get());
        context->OMSetRenderTargets(1, target.GetAddressOf(), nullptr);
        context->IASetInputLayout(pipeline.input.Get());
        context->IASetPrimitiveTopology(map_topology(native.topology));
        context->VSSetShader(pipeline.vs.Get(), nullptr, 0);
        context->GSSetShader(
            native.topology == XenosHostTopology::rectangle_list ?
                rectangle_gs.Get() : nullptr, nullptr, 0);
        context->PSSetShader(pipeline.ps.Get(), nullptr, 0);
        std::vector<ComPtr<ID3D11Buffer>> host_vbs;
        host_vbs.reserve(native.streams.size());
        for (std::size_t i = 0; i < native.streams.size(); ++i) {
            const auto& stream = native.streams[i];
            D3D11_BUFFER_DESC desc{};
            desc.ByteWidth = static_cast<UINT>(stream.bytes.size());
            desc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
            desc.Usage = D3D11_USAGE_IMMUTABLE;
            D3D11_SUBRESOURCE_DATA bytes{};
            bytes.pSysMem = stream.bytes.data();
            ComPtr<ID3D11Buffer> gpu_buffer;
            check(device->CreateBuffer(&desc, &bytes, &gpu_buffer),
                  "Upload original live Xenos vertex allocation");
            const UINT stride = stream.stride, offset = 0;
            context->IASetVertexBuffers(static_cast<UINT>(i), 1,
                gpu_buffer.GetAddressOf(), &stride, &offset);
            host_vbs.emplace_back(std::move(gpu_buffer));
        }
        if (native.indices.empty()) {
            context->IASetIndexBuffer(nullptr, DXGI_FORMAT_UNKNOWN, 0);
            context->Draw(native.count, 0);
        } else {
            D3D11_BUFFER_DESC desc{};
            desc.ByteWidth = static_cast<UINT>(native.indices.size() * sizeof(std::uint32_t));
            desc.BindFlags = D3D11_BIND_INDEX_BUFFER;
            desc.Usage = D3D11_USAGE_IMMUTABLE;
            D3D11_SUBRESOURCE_DATA data{};
            data.pSysMem = native.indices.data();
            ComPtr<ID3D11Buffer> ib;
            check(device->CreateBuffer(&desc, &data, &ib),
                  "Upload original indexed PM4 draw");
            context->IASetIndexBuffer(ib.Get(), DXGI_FORMAT_R32_UINT, 0);
            context->DrawIndexed(native.count, 0, 0);
        }
        // Do not stall the CPU/GPU for each primitive. D3D11 preserves draw
        // ordering and Present flushes accumulated work at the frame boundary.
        // A validated original guest VdSwap request triggers host composition.
        ++pending_draw_count;
        return true;
    }
    void notify_swap(const GpuSwapRequest& request) {
        if (!request.width || !request.height || !request.physical_address)
            return;
        requested_swaps.fetch_add(1, std::memory_order_release);
    }
    void pump() {
        if (stopped) return;
        MSG message{};
        while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
            if (message.message == WM_QUIT) { stopped = true; break; }
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }
        const auto target_swap = requested_swaps.load(std::memory_order_acquire);
        if (!device || !pending_draw_count ||
            target_swap <= last_presented_request)
            return;
        // This is a preview of accumulated native draws at the VdSwap
        // request boundary. The real Xenos EDRAM resolve/frontbuffer
        // ownership must still be implemented to claim a full game frame.
        if (!bmp.empty() && !wrote_bmp) {
            save_gpu_draw_bmp(device.Get(), context.Get(), surface.Get(), bmp);
            wrote_bmp = true;
        }
        check(swap->Present(1U, 0U),
              "Present accumulated original Xenos PM4 draw batch");
        pending_draw_count = 0;
        last_presented_request = target_swap;
    }
};
Dx11LiveFirstDraw::Dx11LiveFirstDraw(std::filesystem::path root,
                                     std::filesystem::path gpu_bmp)
    : impl_(std::make_unique<Impl>(std::move(root), std::move(gpu_bmp))) {}
Dx11LiveFirstDraw::~Dx11LiveFirstDraw() = default;
bool Dx11LiveFirstDraw::submit(const GpuDrawCommand& draw) {
    return impl_->submit(draw);
}
void Dx11LiveFirstDraw::pump_messages() { impl_->pump(); }
void Dx11LiveFirstDraw::request_swap(const GpuSwapRequest& request) {
    impl_->notify_swap(request);
}
void Dx11LiveFirstDraw::swap_callback(const GpuSwapRequest& request,
                                     void* context) {
    if (context) static_cast<Dx11LiveFirstDraw*>(context)->request_swap(request);
}
bool Dx11LiveFirstDraw::callback(const GpuDrawCommand& draw, void* context) {
    return context && static_cast<Dx11LiveFirstDraw*>(context)->submit(draw);
}
} // namespace dotm
#else
#error Native original Xenos D3D11 GPU pipeline is available only on Windows.
#endif
