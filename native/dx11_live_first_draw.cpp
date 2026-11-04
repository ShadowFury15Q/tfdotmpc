// Live original PPC/PM4 renderer: multiple shader pairs and draw calls are
// dispatched through a GPU pipeline cache. Unsupported guest state fails closed.
// DX11 is a limited host backend; full Xenos EDRAM/texture parity is not claimed.
#ifdef _WIN32
#define NOMINMAX
#include "dx11_live_first_draw.hpp"
#include "live_xenos_first_draw.hpp"
#include "xenos_draw_pipeline.hpp"
#include "xenos_shader_constants.hpp"
#include "xenos_sampler_state.hpp"
#include "xenos_depth_state.hpp"
#include "xenos_blend_state.hpp"
#include "xenos_edram_resolve.hpp"
#include "xenos_edram_color_lifetime.hpp"
#include "xenos_texture_upload.hpp"
#include "xenos_gpu_texture_cache.hpp"
#include "xenos_gpu_buffer_cache.hpp"
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
#include <mutex>
#include <optional>
#include <stdexcept>
#include <string>
#include <tuple>
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
    case XenosHostTopology::triangle_fan:
    case XenosHostTopology::line_loop:
        throw std::runtime_error("Original primitive must be converted before D3D11 submission");
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
struct XenosShaderResources {
    std::vector<UINT> textures;
    std::vector<UINT> samplers;
    struct FloatBuffer { UINT slot, bytes; };
    std::vector<FloatBuffer> float_buffers;
};
XenosShaderResources reflect_shader_resources(ID3DBlob* blob,
                                              XenosShaderStage stage) {
    ComPtr<ID3D11ShaderReflection> reflection;
    check(D3DReflect(blob->GetBufferPointer(), blob->GetBufferSize(),
                     IID_ID3D11ShaderReflection,
                     reinterpret_cast<void**>(reflection.GetAddressOf())),
          "Reflect original translated shader resources");
    D3D11_SHADER_DESC desc{};
    check(reflection->GetDesc(&desc), "Describe original translated shader");
    XenosShaderResources resources;
    for (UINT i = 0; i < desc.BoundResources; ++i) {
        D3D11_SHADER_INPUT_BIND_DESC binding{};
        check(reflection->GetResourceBindingDesc(i, &binding),
              "Reflect original Xenos shader binding");
        if (binding.BindCount != 1)
            throw std::runtime_error("Unbounded Xenos descriptor array requires DX12 backend");
        if (binding.Type == D3D_SIT_CBUFFER) {
            // The original shader's float4 constants live in the PM4
            // register shadow, with a programmable per-stage SQ base.
            const std::string name = binding.Name ? binding.Name : "";
            if ((stage == XenosShaderStage::vertex &&
                 name != "VertexShaderConstants") ||
                (stage == XenosShaderStage::pixel &&
                 name != "PixelShaderConstants"))
                throw std::runtime_error(
                    "Original shared/unknown Xenos constant buffer is not yet mapped");
            auto* cb = reflection->GetConstantBufferByName(binding.Name);
            D3D11_SHADER_BUFFER_DESC buffer{};
            check(cb->GetDesc(&buffer),
                  "Reflect actual Xenos float4 shader constant buffer");
            if (!buffer.Size || buffer.Size % 16U ||
                buffer.Size > 256U * 16U ||
                binding.BindPoint >= D3D11_COMMONSHADER_CONSTANT_BUFFER_API_SLOT_COUNT)
                throw std::runtime_error(
                    "Invalid original Xenos stage constant buffer range");
            resources.float_buffers.push_back({binding.BindPoint, buffer.Size});
        } else if (binding.Type == D3D_SIT_TEXTURE && binding.Dimension == D3D_SRV_DIMENSION_TEXTURE2D)
            resources.textures.push_back(binding.BindPoint);
        else if (binding.Type == D3D_SIT_SAMPLER)
            resources.samplers.push_back(binding.BindPoint);
        else
            throw std::runtime_error("Unsupported original Xenos shader resource binding");
    }
    return resources;
}
DXGI_FORMAT xenos_texture_format(std::uint32_t format) {
    switch (format) {
        case 6: return DXGI_FORMAT_R8G8B8A8_UNORM;
        case 18: return DXGI_FORMAT_BC1_UNORM;
        case 19: return DXGI_FORMAT_BC2_UNORM;
        case 20: return DXGI_FORMAT_BC3_UNORM;
        case 49: return DXGI_FORMAT_BC5_UNORM;
        default: throw std::runtime_error("Unsupported original Xenos texture format");
    }
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
        XenosShaderResources vs_resources, ps_resources;
    };
    explicit Impl(std::filesystem::path root, std::filesystem::path output)
        : shader_root(std::move(root)), bmp(std::move(output)) {}
    std::filesystem::path shader_root, bmp;
    std::atomic<bool> stopped{false};
    bool wrote_bmp = false;
    std::uint64_t pending_draw_count = 0;
    std::uint64_t last_presented_request = 0;
    std::atomic<std::uint64_t> requested_swaps{0};
    std::mutex swap_request_mutex, dx11_context_mutex;
    std::optional<GpuSwapRequest> latest_swap_request;
    HWND window = nullptr;
    UINT width = 0, height = 0;
    ComPtr<ID3D11Device> device;
    ComPtr<ID3D11DeviceContext> context;
    ComPtr<IDXGISwapChain> swap;
    ComPtr<ID3D11Texture2D> surface;
    ComPtr<ID3D11RenderTargetView> target;
    // Persistent EDRAM color is independent of the discardable DXGI scanout.
    ComPtr<ID3D11Texture2D> edram_color;
    ComPtr<ID3D11RenderTargetView> edram_target;
    ComPtr<ID3D11Texture2D> edram_readback;
    XenosEdramColorLifetime edram_lifetime;
    ComPtr<ID3D11GeometryShader> rectangle_gs;
    ComPtr<ID3D11RasterizerState> raster;
    std::map<XenosShaderPair, CachedPipeline> pipelines;
    XenosGpuTextureCache<ComPtr<ID3D11ShaderResourceView>> texture_cache;
    XenosGpuBufferCache<ComPtr<ID3D11Buffer>> buffer_cache;
    using ConstantKey = std::tuple<XenosShaderStage, UINT, UINT>;
    std::map<ConstantKey, ComPtr<ID3D11Buffer>> reusable_constant_buffers;
    struct DepthAttachment {
        ComPtr<ID3D11Texture2D> texture;
        ComPtr<ID3D11DepthStencilView> view;
    };
    // Separate original EDRAM depth bases retain their own depth surfaces.
    // Full guest EDRAM aliasing, resolves and clears require further support.
    std::map<std::uint32_t, DepthAttachment> depth_attachments;
    using DepthStateKey = std::tuple<std::uint32_t, std::uint32_t, std::uint32_t>;
    std::map<DepthStateKey, ComPtr<ID3D11DepthStencilState>> depth_states;
    using BlendKey = std::pair<std::uint32_t, std::uint8_t>;
    std::map<BlendKey, ComPtr<ID3D11BlendState>> blend_states;

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
        create_edram_color();
        D3D11_RASTERIZER_DESC rs{};
        rs.FillMode = D3D11_FILL_SOLID;
        rs.CullMode = D3D11_CULL_NONE;
        rs.DepthClipEnable = TRUE;
        check(device->CreateRasterizerState(&rs, &raster),
              "Create original Xenos rasterizer");
        ShowWindow(window, SW_SHOW);
    }
    void create_edram_color() {
        D3D11_TEXTURE2D_DESC desc{};
        desc.Width = width;
        desc.Height = height;
        desc.MipLevels = 1;
        desc.ArraySize = 1;
        desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        desc.SampleDesc.Count = 1;
        desc.Usage = D3D11_USAGE_DEFAULT;
        desc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
        check(device->CreateTexture2D(&desc, nullptr, &edram_color),
              "Create persistent original EDRAM color attachment");
        check(device->CreateRenderTargetView(edram_color.Get(), nullptr, &edram_target),
              "Create persistent original EDRAM RTV");
        // The host readback survives multiple guest RB_COPY commands.
        D3D11_TEXTURE2D_DESC readback_desc = desc;
        readback_desc.BindFlags = 0U;
        readback_desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
        readback_desc.Usage = D3D11_USAGE_STAGING;
        check(device->CreateTexture2D(&readback_desc, nullptr, &edram_readback),
              "Create reusable original EDRAM resolve readback");
        edram_lifetime.on_render_target_created();
        // Deterministic initial host storage; later clears must be driven by
        // guest RB commands, never by the host's Present/frame counter.
        const float initial_color[] = {0.f, 0.f, 0.f, 1.f};
        context->ClearRenderTargetView(edram_target.Get(), initial_color);
    }
    ID3D11DepthStencilView* bind_original_depth_state(
            const GpuDrawCommand& draw) {
        const auto state = decode_xenos_depth_state(draw.register_words);
        if (!state.depth_enable && !state.stencil_enable) {
            context->OMSetDepthStencilState(nullptr, 0U);
            return nullptr;
        }
        const DepthStateKey key{
            draw.register_words[0x2200U],
            draw.register_words[0x210DU],
            draw.register_words[0x210CU]};
        auto depth_state = depth_states.find(key);
        if (depth_state == depth_states.end()) {
            D3D11_DEPTH_STENCIL_DESC desc{};
            desc.DepthEnable = state.depth_enable ? TRUE : FALSE;
            desc.DepthWriteMask = state.depth_write
                ? D3D11_DEPTH_WRITE_MASK_ALL : D3D11_DEPTH_WRITE_MASK_ZERO;
            // R400 comparison and stencil op encodings are zero-based;
            // D3D11's corresponding enumerators start at one.
            desc.DepthFunc = static_cast<D3D11_COMPARISON_FUNC>(
                state.depth_compare + 1U);
            desc.StencilEnable = state.stencil_enable ? TRUE : FALSE;
            desc.StencilReadMask = state.stencil_read_mask;
            desc.StencilWriteMask = state.stencil_write_mask;
            const auto fill_face = [](D3D11_DEPTH_STENCILOP_DESC& output,
                                      const XenosStencilFace& original) {
                output.StencilFunc = static_cast<D3D11_COMPARISON_FUNC>(
                    original.compare + 1U);
                output.StencilFailOp = static_cast<D3D11_STENCIL_OP>(
                    original.fail + 1U);
                output.StencilDepthFailOp = static_cast<D3D11_STENCIL_OP>(
                    original.depth_fail + 1U);
                output.StencilPassOp = static_cast<D3D11_STENCIL_OP>(
                    original.pass + 1U);
            };
            fill_face(desc.FrontFace, state.front);
            fill_face(desc.BackFace, state.back);
            ComPtr<ID3D11DepthStencilState> created;
            check(device->CreateDepthStencilState(&desc, &created),
                  "Create actual original PM4 depth/stencil state");
            depth_state = depth_states.emplace(key, std::move(created)).first;
        }
        context->OMSetDepthStencilState(depth_state->second.Get(),
                                        state.stencil_ref);
        auto depth = depth_attachments.find(state.edram_base_tiles);
        if (depth == depth_attachments.end()) {
            D3D11_TEXTURE2D_DESC tex{};
            tex.Width = width;
            tex.Height = height;
            tex.MipLevels = 1;
            tex.ArraySize = 1;
            tex.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
            tex.SampleDesc.Count = 1;
            tex.Usage = D3D11_USAGE_DEFAULT;
            tex.BindFlags = D3D11_BIND_DEPTH_STENCIL;
            DepthAttachment attachment;
            check(device->CreateTexture2D(&tex, nullptr, &attachment.texture),
                  "Allocate original Xenos depth EDRAM host attachment");
            check(device->CreateDepthStencilView(attachment.texture.Get(),
                  nullptr, &attachment.view),
                  "Create original Xenos depth EDRAM view");
            // Host depth is initialized on first use while genuine guest
            // depth clears / EDRAM tile resolves are not implemented yet.
            context->ClearDepthStencilView(attachment.view.Get(),
                  D3D11_CLEAR_DEPTH | D3D11_CLEAR_STENCIL, 1.0f, 0);
            depth = depth_attachments.emplace(state.edram_base_tiles,
                                               std::move(attachment)).first;
        }
        return depth->second.view.Get();
    }
    static D3D11_BLEND host_blend_factor(std::uint32_t value, bool alpha) {
        switch (value) {
            case 0U: return D3D11_BLEND_ZERO;
            case 1U: return D3D11_BLEND_ONE;
            case 4U: return alpha ? D3D11_BLEND_SRC_ALPHA : D3D11_BLEND_SRC_COLOR;
            case 5U: return alpha ? D3D11_BLEND_INV_SRC_ALPHA : D3D11_BLEND_INV_SRC_COLOR;
            case 6U: return D3D11_BLEND_SRC_ALPHA;
            case 7U: return D3D11_BLEND_INV_SRC_ALPHA;
            case 8U: return alpha ? D3D11_BLEND_DEST_ALPHA : D3D11_BLEND_DEST_COLOR;
            case 9U: return alpha ? D3D11_BLEND_INV_DEST_ALPHA : D3D11_BLEND_INV_DEST_COLOR;
            case 10U: return D3D11_BLEND_DEST_ALPHA;
            case 11U: return D3D11_BLEND_INV_DEST_ALPHA;
            case 12U: case 14U: return D3D11_BLEND_BLEND_FACTOR;
            case 13U: case 15U: return D3D11_BLEND_INV_BLEND_FACTOR;
            case 16U: if (!alpha) return D3D11_BLEND_SRC_ALPHA_SAT;
        }
        throw std::runtime_error("Original Xenos blend factor unsupported by DX11");
    }
    static D3D11_BLEND_OP host_blend_op(std::uint32_t value) {
        switch (value) {
            case 0U: return D3D11_BLEND_OP_ADD;
            case 1U: return D3D11_BLEND_OP_SUBTRACT;
            case 2U: return D3D11_BLEND_OP_MIN;
            case 3U: return D3D11_BLEND_OP_MAX;
            case 4U: return D3D11_BLEND_OP_REV_SUBTRACT;
        }
        throw std::runtime_error("Original Xenos blend operation unsupported by DX11");
    }
    void bind_original_blend_state(const GpuDrawCommand& draw) {
        const auto state = decode_xenos_blend_state(draw.register_words);
        const BlendKey key{state.raw_control, state.write_mask};
        auto it = blend_states.find(key);
        if (it == blend_states.end()) {
            D3D11_BLEND_DESC desc{};
            auto& rt = desc.RenderTarget[0];
            rt.BlendEnable = state.enabled ? TRUE : FALSE;
            rt.RenderTargetWriteMask = state.write_mask;
            rt.SrcBlend = host_blend_factor(state.rgb_source, false);
            rt.DestBlend = host_blend_factor(state.rgb_destination, false);
            rt.BlendOp = host_blend_op(state.rgb_operation);
            rt.SrcBlendAlpha = host_blend_factor(state.alpha_source, true);
            rt.DestBlendAlpha = host_blend_factor(state.alpha_destination, true);
            rt.BlendOpAlpha = host_blend_op(state.alpha_operation);
            ComPtr<ID3D11BlendState> created;
            check(device->CreateBlendState(&desc, &created),
                  "Create exact original Xenos RT0 RGB/alpha blend and color mask");
            it = blend_states.emplace(key, std::move(created)).first;
        }
        context->OMSetBlendState(it->second.Get(), state.constant_factor.data(),
                                 0xFFFFFFFFU);
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
        p.vs_resources = reflect_shader_resources(
            vs_blob.Get(), XenosShaderStage::vertex);
        p.ps_resources = reflect_shader_resources(
            ps_blob.Get(), XenosShaderStage::pixel);
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
    bool resolve_original_edram_copy(const GpuDrawCommand& draw) {
        const auto operation = prepare_xenos_full_surface_resolve(
            draw, width, height);
        if (!operation)
            throw std::runtime_error("Native EDRAM resolve called without RB copy mode");
        if (!edram_lifetime.can_resolve() || !draw.copy_physical_memory ||
            !draw.write_physical_memory)
            throw std::runtime_error(
                "Original EDRAM resolve requires live GPU color and physical allocation");
        // Use real previously submitted shader output as the EDRAM color
        // source. Never generate a fullscreen test texture or stand-in VS.
        D3D11_TEXTURE2D_DESC desc{};
        edram_color->GetDesc(&desc);
        if (desc.Width != width || desc.Height != height ||
            desc.Format != DXGI_FORMAT_R8G8B8A8_UNORM ||
            desc.SampleDesc.Count != 1U)
            throw std::runtime_error("Unsupported actual EDRAM color attachment format");
        context->OMSetRenderTargets(0, nullptr, nullptr);
        context->CopyResource(edram_readback.Get(), edram_color.Get());
        // Preserve padding and the other tiles in the already allocated
        // guest destination; copying only the genuine resolved color texels.
        auto physical = draw.copy_physical_memory(
            operation->physical_address,
            static_cast<std::uint32_t>(operation->required_physical_bytes));
        D3D11_MAPPED_SUBRESOURCE source{};
        check(context->Map(edram_readback.Get(), 0U, D3D11_MAP_READ, 0U, &source),
              "Read actual Xenos shader output for native EDRAM resolve");
        try {
            for (std::uint32_t y = 0U; y < height; ++y) {
                for (std::uint32_t x = 0U; x < width; ++x) {
                    const auto destination = xenos_tiled_2d_offset(
                        x, y, operation->tiled_layout.pitch_elements, 4U);
                    const auto* rgba =
                        static_cast<const std::uint8_t*>(source.pData) +
                        std::size_t(y) * source.RowPitch + std::size_t(x) * 4U;
                    std::memcpy(physical.data() + destination, rgba, 4U);
                }
            }
        } catch (...) {
            context->Unmap(edram_readback.Get(), 0U);
            throw;
        }
        context->Unmap(edram_readback.Get(), 0U);
        // The guest's original physically allocated tiled texture becomes
        // the authoritative frontbuffer later read by VdSwap scanout.
        // The writer rejects expired/unallocated memory before any mutation.
        draw.write_physical_memory(operation->physical_address, physical);
        return true;
    }
    bool submit(const GpuDrawCommand& draw) {
        std::lock_guard lock(dx11_context_mutex);
        if (stopped.load(std::memory_order_acquire)) return false;
        // Never acknowledge unknown original conditions or geometry. Resource
        // captures happen in the *live* GPU callback under its read window.
        initialize();
        if (draw.register_words.size() > 0x2208U &&
            (draw.register_words[0x2208U] & 7U) == 6U)
            return resolve_original_edram_copy(draw);
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
        // VdSwap is not a guest EDRAM clear. Keep previous render-target
        // contents available for later PM4 passes and subsequent frames.
        D3D11_VIEWPORT vp{};
        vp.Width = static_cast<float>(width);
        vp.Height = static_cast<float>(height);
        vp.MinDepth = 0.f;
        vp.MaxDepth = 1.f;
        context->RSSetViewports(1, &vp);
        context->RSSetState(raster.Get());
        auto* depth_view = bind_original_depth_state(draw);
        bind_original_blend_state(draw);
        context->OMSetRenderTargets(1, edram_target.GetAddressOf(), depth_view);
        context->IASetInputLayout(pipeline.input.Get());
        context->IASetPrimitiveTopology(map_topology(native.topology));
        context->VSSetShader(pipeline.vs.Get(), nullptr, 0);
        context->GSSetShader(
            native.topology == XenosHostTopology::rectangle_list ?
                rectangle_gs.Get() : nullptr, nullptr, 0);
        context->PSSetShader(pipeline.ps.Get(), nullptr, 0);
        // PM4 can modify VS and PS constant windows between DRAWs using the
        // SAME shader pair. Re-upload the original float bits each time.
        // Reuse constant-buffer allocations, but write actual PM4 float
        // registers on every draw. DISCARD gives earlier queued GPU draws
        // independent backing storage while eliminating per-draw CreateBuffer.
        const auto upload_float_buffers = [&](const XenosShaderResources& resources,
                                             XenosShaderStage stage) {
            for (const auto& resource : resources.float_buffers) {
                const auto original = xenos_draw_float_constants(
                    draw, stage, resource.bytes);
                auto& buffer = reusable_constant_buffers[
                    std::make_tuple(stage, resource.slot, resource.bytes)];
                if (!buffer) {
                    D3D11_BUFFER_DESC desc{};
                    desc.ByteWidth = resource.bytes;
                    desc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
                    desc.Usage = D3D11_USAGE_DYNAMIC;
                    desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
                    check(device->CreateBuffer(&desc, nullptr, &buffer),
                          "Allocate reusable original Xenos shader constant buffer");
                }
                D3D11_MAPPED_SUBRESOURCE mapped{};
                check(context->Map(buffer.Get(), 0U, D3D11_MAP_WRITE_DISCARD,
                                   0U, &mapped),
                      "Map original PM4 shader constants with GPU DISCARD");
                std::memcpy(mapped.pData, original.data(), resource.bytes);
                context->Unmap(buffer.Get(), 0U);
                if (stage == XenosShaderStage::vertex)
                    context->VSSetConstantBuffers(resource.slot, 1,
                                                  buffer.GetAddressOf());
                else
                    context->PSSetConstantBuffers(resource.slot, 1,
                                                  buffer.GetAddressOf());
            }
        };
        // Do not inherit stale shader registers from a previous pipeline.
        ID3D11Buffer* null_constant_buffers[3] = {};
        context->VSSetConstantBuffers(0, 3, null_constant_buffers);
        context->PSSetConstantBuffers(0, 3, null_constant_buffers);
        upload_float_buffers(pipeline.vs_resources, XenosShaderStage::vertex);
        upload_float_buffers(pipeline.ps_resources, XenosShaderStage::pixel);
        // Bind original live texture DMA, never placeholder resources. A shader
        // requiring an unsupported descriptor layout fails before Draw().
        std::map<UINT, ComPtr<ID3D11ShaderResourceView>> views;
        std::map<UINT, ComPtr<ID3D11SamplerState>> samplers;
        const auto bind_stage = [&](const XenosShaderResources& resources, bool vertex) {
            for (const UINT slot : resources.textures) {
                if (slot >= D3D11_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT)
                    throw std::runtime_error("Xenos texture register exceeds D3D11 limit");
                if (!views.count(slot)) {
                    const auto image = prepare_xenos_texture_2d(draw, slot);
                    // Guest memory is read and detiled each time; a DMA write
                    // with the same fetch address cannot return a stale SRV.
                    auto view = texture_cache.get_or_upload(image,
                        [&](const XenosTextureUpload& original) {
                            D3D11_TEXTURE2D_DESC texture_desc{};
                            texture_desc.Width = original.width;
                            texture_desc.Height = original.height;
                            texture_desc.MipLevels = 1;
                            texture_desc.ArraySize = 1;
                            texture_desc.Format = xenos_texture_format(original.format);
                            texture_desc.SampleDesc.Count = 1;
                            texture_desc.Usage = D3D11_USAGE_IMMUTABLE;
                            texture_desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
                            D3D11_SUBRESOURCE_DATA source{};
                            source.pSysMem = original.bytes.data();
                            source.SysMemPitch = original.row_pitch;
                            ComPtr<ID3D11Texture2D> texture;
                            check(device->CreateTexture2D(&texture_desc, &source, &texture),
                                  "Upload actual original PM4 texture to DX11");
                            ComPtr<ID3D11ShaderResourceView> created;
                            check(device->CreateShaderResourceView(
                                      texture.Get(), nullptr, &created),
                                  "Create original PM4 shader texture view");
                            return created;
                        });
                    views.emplace(slot, std::move(view));
                }
                auto* srv = views.at(slot).Get();
                if (vertex) context->VSSetShaderResources(slot, 1, &srv);
                else context->PSSetShaderResources(slot, 1, &srv);
            }
            for (const UINT slot : resources.samplers) {
                if (slot >= D3D11_COMMONSHADER_SAMPLER_SLOT_COUNT)
                    throw std::runtime_error("Xenos sampler register exceeds D3D11 limit");
                if (!samplers.count(slot)) {
                    const auto found = std::find_if(
                        draw.texture_fetch_constants.begin(),
                        draw.texture_fetch_constants.end(),
                        [slot](const XenosTextureFetchConstant& t) {
                            return t.slot == slot;
                        });
                    if (found == draw.texture_fetch_constants.end())
                        throw std::runtime_error("Original Xenos sampler fetch slot unbound");
                    const auto host = decode_xenos_host_sampler(*found);
                    const auto address = [](XenosHostAddress mode) {
                        switch (mode) {
                        case XenosHostAddress::wrap:
                            return D3D11_TEXTURE_ADDRESS_WRAP;
                        case XenosHostAddress::mirror:
                            return D3D11_TEXTURE_ADDRESS_MIRROR;
                        case XenosHostAddress::clamp:
                            return D3D11_TEXTURE_ADDRESS_CLAMP;
                        }
                        throw std::runtime_error("Unknown Xenos sampler address mode");
                    };
                    D3D11_SAMPLER_DESC desc{};
                    desc.AddressU = address(host.u);
                    desc.AddressV = address(host.v);
                    desc.AddressW = address(host.w);
                    desc.Filter =
                        host.min_linear && host.mag_linear
                            ? D3D11_FILTER_MIN_MAG_LINEAR_MIP_POINT
                        : host.min_linear
                            ? D3D11_FILTER_MIN_LINEAR_MAG_MIP_POINT
                        : host.mag_linear
                            ? D3D11_FILTER_MIN_POINT_MAG_LINEAR_MIP_POINT
                            : D3D11_FILTER_MIN_MAG_MIP_POINT;
                    desc.MinLOD = 0.0f;
                    desc.MaxLOD = 0.0f;
                    desc.MaxAnisotropy = 1U;
                    ComPtr<ID3D11SamplerState> state;
                    check(device->CreateSamplerState(&desc, &state),
                          "Create original Xenos sampler from PM4 fetch constants");
                    samplers.emplace(slot, std::move(state));
                }
                auto* state = samplers.at(slot).Get();
                if (vertex) context->VSSetSamplers(slot, 1, &state);
                else context->PSSetSamplers(slot, 1, &state);
            }
        };
        bind_stage(pipeline.vs_resources, true);
        bind_stage(pipeline.ps_resources, false);
        std::vector<ComPtr<ID3D11Buffer>> host_vbs;
        host_vbs.reserve(native.streams.size());
        for (std::size_t i = 0; i < native.streams.size(); ++i) {
            const auto& stream = native.streams[i];
            // Original PM4 vertex DMA is re-read on every draw. Reuse only
            // byte-identical immutable GPU allocations, never guest addresses.
            auto gpu_buffer = buffer_cache.get_or_upload(
                XenosGpuBufferRole::vertex, stream.bytes,
                [&](const std::vector<std::uint8_t>& original) {
                    D3D11_BUFFER_DESC desc{};
                    desc.ByteWidth = static_cast<UINT>(original.size());
                    desc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
                    desc.Usage = D3D11_USAGE_IMMUTABLE;
                    D3D11_SUBRESOURCE_DATA bytes{};
                    bytes.pSysMem = original.data();
                    ComPtr<ID3D11Buffer> created;
                    check(device->CreateBuffer(&desc, &bytes, &created),
                          "Upload original live Xenos vertex allocation");
                    return created;
                });
            const UINT stride = stream.stride, offset = 0;
            context->IASetVertexBuffers(static_cast<UINT>(i), 1,
                gpu_buffer.GetAddressOf(), &stride, &offset);
            host_vbs.emplace_back(std::move(gpu_buffer));
        }
        if (native.indices.empty()) {
            context->IASetIndexBuffer(nullptr, DXGI_FORMAT_UNKNOWN, 0);
            context->Draw(native.count, 0);
        } else {
            // The decoded original index order is cached independently of
            // vertex buffers. A changed index sequence forces a new upload.
            const auto* index_bytes = reinterpret_cast<const std::uint8_t*>(
                native.indices.data());
            std::vector<std::uint8_t> original_indices(
                index_bytes, index_bytes + native.indices.size() * sizeof(std::uint32_t));
            auto ib = buffer_cache.get_or_upload(
                XenosGpuBufferRole::index, original_indices,
                [&](const std::vector<std::uint8_t>& original) {
                    D3D11_BUFFER_DESC desc{};
                    desc.ByteWidth = static_cast<UINT>(original.size());
                    desc.BindFlags = D3D11_BIND_INDEX_BUFFER;
                    desc.Usage = D3D11_USAGE_IMMUTABLE;
                    D3D11_SUBRESOURCE_DATA data{};
                    data.pSysMem = original.data();
                    ComPtr<ID3D11Buffer> created;
                    check(device->CreateBuffer(&desc, &data, &created),
                          "Upload original indexed PM4 draw");
                    return created;
                });
            context->IASetIndexBuffer(ib.Get(), DXGI_FORMAT_R32_UINT, 0);
            context->DrawIndexed(native.count, 0, 0);
        }
        // Do not stall the CPU/GPU for each primitive. D3D11 preserves draw
        // ordering and Present flushes accumulated work at the frame boundary.
        // A validated original guest VdSwap request triggers host composition.
        ++pending_draw_count;
        edram_lifetime.on_original_draw_submitted();
        return true;
    }
    void notify_swap(const GpuSwapRequest& request) {
        if (!request.width || !request.height || !request.physical_address)
            return;
        // Observers run under the guest GPU lock, potentially on the PPC
        // thread. Queue by value; never enter D3D11 or reenter GPU here.
        {
            std::lock_guard lock(swap_request_mutex);
            latest_swap_request = request;
        }
        requested_swaps.fetch_add(1, std::memory_order_release);
    }
    void pump() {
        if (stopped.load(std::memory_order_acquire)) return;
        MSG message{};
        while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
            if (message.message == WM_QUIT) {
                stopped.store(true, std::memory_order_release);
                break;
            }
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }
        const auto target_swap = requested_swaps.load(std::memory_order_acquire);
        if (target_swap <= last_presented_request ||
            stopped.load(std::memory_order_acquire))
            return;
        GpuSwapRequest request;
        {
            std::lock_guard lock(swap_request_mutex);
            if (!latest_swap_request) return;
            request = *latest_swap_request;
        }
        // Acquire the ACTUAL guest frontbuffer before the DX11 context
        // mutex. PM4 submit owns the GPU lock first, so reversing this
        // order would deadlock the PPC renderer on concurrent VdSwap.
        // If the original game has not resolved EDRAM into this allocation,
        // those unresolved guest pixels (not our preview DRAWs) are shown.
        const auto scanout = vd_bound_copy_frontbuffer_scanout(request);
        std::lock_guard lock(dx11_context_mutex);
        if (stopped.load(std::memory_order_acquire)) return;
        initialize();
        context->OMSetRenderTargets(0, nullptr, nullptr);
        if (width != scanout.width || height != scanout.height) {
            // The original scanout width/height, not a hard-coded host
            // window dimension, owns the scanout buffer dimensions.
            context->ClearState();
            target.Reset();
            surface.Reset();
            edram_target.Reset();
            edram_color.Reset();
            edram_readback.Reset();
            depth_attachments.clear();
            check(swap->ResizeBuffers(1, scanout.width, scanout.height,
                    DXGI_FORMAT_R8G8B8A8_UNORM, 0),
                  "Resize DXGI buffer to original Xbox frontbuffer");
            width = scanout.width;
            height = scanout.height;
            check(swap->GetBuffer(0, IID_PPV_ARGS(&surface)),
                  "Get original Xbox scanout DXGI surface");
            check(device->CreateRenderTargetView(surface.Get(), nullptr, &target),
                  "Create original scanout RTV");
            create_edram_color();
            pending_draw_count = 0;
        }
        // No fake clear, debug geometry or host preview framebuffer:
        // upload the independently validated guest scanout bytes verbatim.
        context->UpdateSubresource(surface.Get(), 0, nullptr,
                                   scanout.pixels.data(),
                                   scanout.row_pitch, 0);
        if (!bmp.empty() && !wrote_bmp) {
            save_gpu_draw_bmp(device.Get(), context.Get(), surface.Get(), bmp);
            wrote_bmp = true;
        }
        check(swap->Present(1U, 0U),
              "Present original Xbox VdSwap physical frontbuffer");
        pending_draw_count = 0;
        edram_lifetime.on_vd_swap();
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
