// Replays the verified first original DOTM Xenos rectangle using translated
// ORIGINAL shaders. Never creates stand-in shaders, geometry, or game assets.
// Offline diagnostic only: this does not replace live PM4 execution.
#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#include <shellapi.h>
#include <d3d11.h>
#include <d3dcompiler.h>
#include <wrl/client.h>
#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <vector>

using Microsoft::WRL::ComPtr;
namespace {
void check(HRESULT value, const char* what) {
    if (FAILED(value)) throw std::runtime_error(what);
}
std::vector<std::uint8_t> read_binary(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) throw std::runtime_error("Missing private original draw or translated shader file");
    return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}
std::string read_text(const std::filesystem::path& path) {
    const auto bytes = read_binary(path);
    return {bytes.begin(), bytes.end()};
}
std::uint64_t fnv64(const std::vector<std::uint8_t>& bytes) {
    std::uint64_t hash = 14695981039346656037ULL;
    for (auto b : bytes) hash = (hash ^ b) * 1099511628211ULL;
    return hash;
}
void verify_original_draw(const std::filesystem::path& capture) {
    const auto meta = read_text(capture / "draw.json");
    for (const char* field : {
        "\"initiator\": 196744", "\"primitive_type\": 8",
        "\"source_select\": 2", "\"index_count\": 3",
        "\"vertex_shader_dwords\": 27", "\"pixel_shader_dwords\": 9",
        "\"slot\":0", "\"size_dwords\":21", "\"endian_mode\":2"}) {
        // Check exact expected original PM4 data before using the capture.
        if (meta.find(field) == std::string::npos)
            throw std::runtime_error(std::string("Original DOTM first-draw mismatch: ") + field);
    }
    const auto vs = read_binary(capture / "vertex_shader.be");
    const auto ps = read_binary(capture / "pixel_shader.be");
    if (vs.size() != 108 || fnv64(vs) != 0x539EA9E08AA83F0CULL ||
        ps.size() != 36 || fnv64(ps) != 0x438C2AF84C78A133ULL)
        throw std::runtime_error("Original Xenos microcode fingerprint mismatch");
    const auto vertices = read_binary(capture / "vertex_fetch_slot_0.decoded.bin");
    if (vertices.size() != 84)
        throw std::runtime_error("Original first rectangle must contain three 28-byte vertices");
    for (unsigned i = 0; i < 3; ++i) {
        float xy[2]{};
        std::memcpy(xy, vertices.data() + i * 28, sizeof(xy));
        if (!std::isfinite(xy[0]) || !std::isfinite(xy[1]))
            throw std::runtime_error("Invalid original rectangle coordinates");
    }
}
// XenosRecomp writes HLSL with DX12 register spaces and DXC's shader
// attributes. Strip only those *binding/entry annotations* for D3D11 FXC;
// never replace or edit original shader instructions/expressions.
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
// The geometry stage runs AFTER the original vertex shader. Xenos primitive
// 8 (rectangle list) extends three VS outputs into two rasterized triangles.
// The fourth corner is derived from the actual output of the guest VS, never
// from fabricated vertex data; every original interpolator is preserved.
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
// Read back actual GPU output for a reproducible first-original-draw image.
// This screenshot is NOT a full game frame. No CPU-side substitute is drawn.
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
LRESULT CALLBACK wndproc(HWND wnd, UINT msg, WPARAM wp, LPARAM lp) {
    if (msg == WM_CLOSE) { DestroyWindow(wnd); return 0; }
    if (msg == WM_DESTROY) { PostQuitMessage(0); return 0; }
    if (msg == WM_KEYDOWN && wp == VK_ESCAPE) { DestroyWindow(wnd); return 0; }
    return DefWindowProcW(wnd, msg, wp, lp);
}
void replay(const std::filesystem::path& capture,
            const std::filesystem::path& translated,
            const std::filesystem::path& screenshot) {
    verify_original_draw(capture);
    const auto vertices = read_binary(capture / "vertex_fetch_slot_0.decoded.bin");
    const auto vs_blob = compile_source(read_text(translated / "vertex.hlsl"), "vs_5_0", "original_vertex");
    const auto ps_blob = compile_source(read_text(translated / "pixel.hlsl"), "ps_5_0", "original_pixel");
    const auto gs_blob = compile_rectangle_geometry();
    HINSTANCE instance = GetModuleHandleW(nullptr);
    WNDCLASSW wc{};
    wc.hInstance = instance; wc.lpfnWndProc = wndproc;
    wc.lpszClassName = L"DOTM_ORIGINAL_FIRST_XENOS_DRAW";
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    if (!RegisterClassW(&wc)) throw std::runtime_error("RegisterClass failed");
    HWND wnd = CreateWindowW(wc.lpszClassName,
        L"DOTM: original first Xenos draw (private replay, not the game)",
        WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT,
        1280, 720, nullptr, nullptr, instance, nullptr);
    if (!wnd) throw std::runtime_error("CreateWindow failed");
    RECT client{};
    GetClientRect(wnd, &client);
    const UINT width = static_cast<UINT>(client.right - client.left);
    const UINT height = static_cast<UINT>(client.bottom - client.top);
    DXGI_SWAP_CHAIN_DESC desc{};
    desc.BufferDesc.Width = width; desc.BufferDesc.Height = height;
    desc.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    desc.SampleDesc.Count = 1; desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    desc.BufferCount = 1; desc.OutputWindow = wnd; desc.Windowed = TRUE;
    desc.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;
    ComPtr<ID3D11Device> device;
    ComPtr<ID3D11DeviceContext> context;
    ComPtr<IDXGISwapChain> swapchain;
    D3D_FEATURE_LEVEL feature{};
    check(D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0,
           nullptr, 0, D3D11_SDK_VERSION, &desc,
           &swapchain, &device, &feature, &context), "Create D3D11 device");
    if (feature < D3D_FEATURE_LEVEL_11_0)
        throw std::runtime_error("The original rectangle backend requires D3D11 FL11");
    ComPtr<ID3D11Texture2D> surface;
    check(swapchain->GetBuffer(0, IID_PPV_ARGS(&surface)), "Get backbuffer");
    ComPtr<ID3D11RenderTargetView> rtv;
    check(device->CreateRenderTargetView(surface.Get(), nullptr, &rtv), "Create RTV");
    ComPtr<ID3D11VertexShader> vs;
    ComPtr<ID3D11PixelShader> ps;
    ComPtr<ID3D11GeometryShader> gs;
    check(device->CreateVertexShader(vs_blob->GetBufferPointer(),
           vs_blob->GetBufferSize(), nullptr, &vs), "Create original VS");
    check(device->CreatePixelShader(ps_blob->GetBufferPointer(),
           ps_blob->GetBufferSize(), nullptr, &ps), "Create original PS");
    check(device->CreateGeometryShader(gs_blob->GetBufferPointer(),
           gs_blob->GetBufferSize(), nullptr, &gs), "Create rectangle GS");
    // The original verified 84-byte DMA has 3 * 28-byte vertices:
    // POSITION0 float4 at 0, COLOR0 float3 at 16 (not a dummy triangle).
    constexpr D3D11_INPUT_ELEMENT_DESC elements[] = {
        {"POSITION", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 0,
         D3D11_INPUT_PER_VERTEX_DATA, 0},
        {"COLOR", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 16,
         D3D11_INPUT_PER_VERTEX_DATA, 0},
    };
    ComPtr<ID3D11InputLayout> layout;
    check(device->CreateInputLayout(elements, 2, vs_blob->GetBufferPointer(),
           vs_blob->GetBufferSize(), &layout), "Create original VS input layout");
    D3D11_BUFFER_DESC vb_desc{};
    vb_desc.ByteWidth = static_cast<UINT>(vertices.size());
    vb_desc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
    vb_desc.Usage = D3D11_USAGE_IMMUTABLE;
    D3D11_SUBRESOURCE_DATA data{};
    data.pSysMem = vertices.data();
    ComPtr<ID3D11Buffer> vb;
    check(device->CreateBuffer(&vb_desc, &data, &vb), "Upload original vertex DMA");
    D3D11_RASTERIZER_DESC raster_desc{};
    raster_desc.FillMode = D3D11_FILL_SOLID;
    raster_desc.CullMode = D3D11_CULL_NONE;
    raster_desc.DepthClipEnable = TRUE;
    ComPtr<ID3D11RasterizerState> raster;
    check(device->CreateRasterizerState(&raster_desc, &raster), "Create rasterizer");
    D3D11_VIEWPORT viewport{};
    viewport.Width = static_cast<float>(width);
    viewport.Height = static_cast<float>(height);
    viewport.MinDepth = 0.0f; viewport.MaxDepth = 1.0f;
    UINT stride = 28, offset = 0;
    context->IASetInputLayout(layout.Get());
    context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    context->IASetVertexBuffers(0, 1, vb.GetAddressOf(), &stride, &offset);
    context->VSSetShader(vs.Get(), nullptr, 0);
    context->GSSetShader(gs.Get(), nullptr, 0);
    context->PSSetShader(ps.Get(), nullptr, 0);
    context->RSSetViewports(1, &viewport);
    context->RSSetState(raster.Get());
    context->OMSetRenderTargets(1, rtv.GetAddressOf(), nullptr);
    ShowWindow(wnd, SW_SHOW);
    // Execute the actual submitted VS -> rectangle GS -> PS immediately;
    // redraw after uncovering the window, never wait for arbitrary input.
    const auto draw_original = [&]() {
        const float black[] = {0, 0, 0, 1};
        context->ClearRenderTargetView(rtv.Get(), black);
        context->Draw(3, 0);
        check(swapchain->Present(1, 0), "Present first original Xenos draw");
    };
    draw_original();
    if (!screenshot.empty())
        save_gpu_draw_bmp(device.Get(), context.Get(), surface.Get(), screenshot);
    MSG msg{};
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
        if (msg.message == WM_PAINT) draw_original();
    }
}
} // namespace
int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int) {
    int argc = 0;
    wchar_t** argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (!argv) return 1;
    try {
        if (argc != 3 && argc != 4) throw std::runtime_error(
            "Usage: dotm_dx11_original_draw_replay.exe <private-capture/first-original-draw> <private-translated-output> [original-first-draw.bmp]\n"
            "The ORIGINAL first Xenos draw and XenosRecomp HLSL are required.");
        const std::filesystem::path capture(argv[1]), translated(argv[2]);
        const std::filesystem::path screenshot = argc == 4 ?
            std::filesystem::path(argv[3]) : std::filesystem::path();
        replay(capture, translated, screenshot);
        LocalFree(argv);
        return 0;
    } catch (const std::exception& e) {
        LocalFree(argv);
        MessageBoxA(nullptr, e.what(), "DOTM first ORIGINAL Xenos draw replay", MB_ICONERROR | MB_OK);
        return 1;
    }
}
#else
#error Original DX11 Xenos draw replay is supported on Windows only.
#endif
