#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#include <shellapi.h>
#include <d3d11.h>
#include <d3dcompiler.h>
#include <DirectXMath.h>
#include <wrl/client.h>
#include "ue3_lod.hpp"
#include "ue3_package.hpp"
#include <algorithm>
#include <cstring>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <stdexcept>
#include <string>
#include <vector>

using Microsoft::WRL::ComPtr;
using namespace DirectX;

namespace {
struct Vertex {
    XMFLOAT3 pos;
    XMFLOAT2 uv;
};
struct Camera {
    XMFLOAT4X4 matrix;
};

constexpr const char* hlsl = R"HLSL(
cbuffer Camera : register(b0) { float4x4 MVP; };
struct Input { float3 position : POSITION; float2 uv : TEXCOORD0; };
struct Pixel { float4 position : SV_POSITION; float2 uv : TEXCOORD0; };
Pixel VSMain(Input input) {
    Pixel output;
    output.position=mul(float4(input.position, 1.0f), MVP);
    output.uv=input.uv;
    return output;
}
float4 PSMain(Pixel input) : SV_TARGET {
    return float4(0.25f + 0.70f * saturate(input.uv.x),
                  0.25f + 0.70f * saturate(input.uv.y), 0.80f, 1.0f);
}
)HLSL";

void check(HRESULT hr,const char* stage) {
    if(FAILED(hr))throw std::runtime_error(stage);
}
LRESULT CALLBACK wndproc(HWND wnd,UINT msg,WPARAM w,LPARAM l){
    if(msg==WM_DESTROY){PostQuitMessage(0);return 0;}
    if(msg==WM_KEYDOWN&&w==VK_ESCAPE){DestroyWindow(wnd);return 0;}
    return DefWindowProcW(wnd,msg,w,l);
}
void run(const std::filesystem::path&source,const std::string&filter) {
    const auto package=dotm::Ue3Package::load(source);
    std::size_t chosen=package.exports().size();
    for(std::size_t i=0;i<package.exports().size();++i){
        if(!package.exports()[i].serial_size||package.export_class_path(i)!="Engine.StaticMesh")continue;
        const auto name=package.object_reference_path(std::int32_t(i+1));
        if(!filter.empty()&&name.find(filter)==std::string::npos)continue;
        chosen=i;break;
    }
    if(chosen==package.exports().size())
        throw std::runtime_error("No matching StaticMesh objects in package");
    const auto lod=dotm::read_static_mesh_lod0(package,chosen);
    if(lod.vertices.empty()||lod.indices.empty())
        throw std::runtime_error("StaticMesh has no drawable LOD0 geometry");
    if(lod.vertices.size()>UINT_MAX||lod.indices.size()>UINT_MAX)
        throw std::runtime_error("Mesh exceeds supported DX11 buffer size");
    std::vector<Vertex> vertices;
    vertices.reserve(lod.vertices.size());
    XMFLOAT3 mn{1.0e30f,1.0e30f,1.0e30f},mx{-1.0e30f,-1.0e30f,-1.0e30f};
    for(const auto&src:lod.vertices){
        auto x=src.position[0],y=src.position[1],z=src.position[2];
        vertices.push_back({XMFLOAT3{x,y,z},XMFLOAT2{src.uv[0][0],src.uv[0][1]}});
        mn.x=std::min(mn.x,x);mn.y=std::min(mn.y,y);mn.z=std::min(mn.z,z);
        mx.x=std::max(mx.x,x);mx.y=std::max(mx.y,y);mx.z=std::max(mx.z,z);
    }
    const float cx=(mn.x+mx.x)*0.5f,cy=(mn.y+mx.y)*0.5f,cz=(mn.z+mx.z)*0.5f;
    const float radius=std::max({(mx.x-mn.x),(mx.y-mn.y),(mx.z-mn.z),1.0f});
    const auto hinst=GetModuleHandleW(nullptr);
    WNDCLASSEXW wc{};
    wc.cbSize=sizeof(wc);wc.hInstance=hinst;wc.lpfnWndProc=wndproc;
    wc.lpszClassName=L"DOTM_DX11_STATICMESH";
    wc.hCursor=LoadCursor(nullptr,IDC_ARROW);
    if(!RegisterClassExW(&wc))throw std::runtime_error("RegisterClassExW failed");
    HWND wnd=CreateWindowExW(0,wc.lpszClassName,
        L"Transformers DOTM - Native DX11 StaticMesh Preview (NOT THE GAME)",
        WS_OVERLAPPEDWINDOW,CW_USEDEFAULT,CW_USEDEFAULT,1100,780,
        nullptr,nullptr,hinst,nullptr);
    if(!wnd)throw std::runtime_error("CreateWindowExW failed");
    RECT rect{};GetClientRect(wnd,&rect);
    const UINT width=UINT(rect.right),height=UINT(rect.bottom);
    DXGI_SWAP_CHAIN_DESC desc{};
    desc.BufferDesc.Width=width;
    desc.BufferDesc.Height=height;
    desc.BufferDesc.Format=DXGI_FORMAT_R8G8B8A8_UNORM;
    desc.SampleDesc.Count=1;
    desc.BufferUsage=DXGI_USAGE_RENDER_TARGET_OUTPUT;
    desc.BufferCount=1;
    desc.OutputWindow=wnd;
    desc.Windowed=TRUE;
    desc.SwapEffect=DXGI_SWAP_EFFECT_DISCARD;
    ComPtr<IDXGISwapChain> swap;
    ComPtr<ID3D11Device> device;
    ComPtr<ID3D11DeviceContext> context;
    D3D_FEATURE_LEVEL level{};
    check(D3D11CreateDeviceAndSwapChain(nullptr,D3D_DRIVER_TYPE_HARDWARE,nullptr,0,
        nullptr,0,D3D11_SDK_VERSION,&desc,&swap,&device,&level,&context),
        "Cannot initialize Windows Direct3D 11 graphics");
    ComPtr<ID3D11Texture2D> backbuffer;
    check(swap->GetBuffer(0,IID_PPV_ARGS(&backbuffer)),"Get swapchain backbuffer");
    ComPtr<ID3D11RenderTargetView> rtv;
    check(device->CreateRenderTargetView(backbuffer.Get(),nullptr,&rtv),"Create render target");
    D3D11_TEXTURE2D_DESC depth_desc{};
    depth_desc.Width=width;depth_desc.Height=height;
    depth_desc.ArraySize=1;depth_desc.MipLevels=1;depth_desc.SampleDesc.Count=1;
    depth_desc.Format=DXGI_FORMAT_D24_UNORM_S8_UINT;
    depth_desc.BindFlags=D3D11_BIND_DEPTH_STENCIL;
    ComPtr<ID3D11Texture2D> depth;
    check(device->CreateTexture2D(&depth_desc,nullptr,&depth),"Create depth texture");
    ComPtr<ID3D11DepthStencilView> dsv;
    check(device->CreateDepthStencilView(depth.Get(),nullptr,&dsv),"Create depth view");
    ComPtr<ID3DBlob> vblob,pblob,error;
    auto hr=D3DCompile(hlsl,std::strlen(hlsl),nullptr,nullptr,nullptr,
                      "VSMain","vs_5_0",D3DCOMPILE_ENABLE_STRICTNESS,0,&vblob,&error);
    check(hr,"Compile DX11 HLSL vertex shader");
    error.Reset();
    hr=D3DCompile(hlsl,std::strlen(hlsl),nullptr,nullptr,nullptr,
                  "PSMain","ps_5_0",D3DCOMPILE_ENABLE_STRICTNESS,0,&pblob,&error);
    check(hr,"Compile DX11 HLSL pixel shader");
    ComPtr<ID3D11VertexShader> vs;
    ComPtr<ID3D11PixelShader> ps;
    check(device->CreateVertexShader(vblob->GetBufferPointer(),vblob->GetBufferSize(),
                                     nullptr,&vs),"Create vertex shader");
    check(device->CreatePixelShader(pblob->GetBufferPointer(),pblob->GetBufferSize(),
                                    nullptr,&ps),"Create pixel shader");
    const D3D11_INPUT_ELEMENT_DESC input[]={
        {"POSITION",0,DXGI_FORMAT_R32G32B32_FLOAT,0,0,D3D11_INPUT_PER_VERTEX_DATA,0},
        {"TEXCOORD",0,DXGI_FORMAT_R32G32_FLOAT,0,12,D3D11_INPUT_PER_VERTEX_DATA,0}
    };
    ComPtr<ID3D11InputLayout> layout;
    check(device->CreateInputLayout(input,2,vblob->GetBufferPointer(),
        vblob->GetBufferSize(),&layout),"Create input layout");
    auto buffer=[&](UINT bytes,UINT bind,const void*src,ComPtr<ID3D11Buffer>&out){
        D3D11_BUFFER_DESC d{};
        d.ByteWidth=bytes;d.BindFlags=bind;d.Usage=D3D11_USAGE_IMMUTABLE;
        D3D11_SUBRESOURCE_DATA init{};init.pSysMem=src;
        check(device->CreateBuffer(&d,&init,&out),"Create vertex/index GPU buffer");
    };
    ComPtr<ID3D11Buffer> vb,ib,cb;
    buffer(UINT(vertices.size()*sizeof(Vertex)),D3D11_BIND_VERTEX_BUFFER,vertices.data(),vb);
    buffer(UINT(lod.indices.size()*sizeof(std::uint32_t)),D3D11_BIND_INDEX_BUFFER,lod.indices.data(),ib);
    D3D11_BUFFER_DESC camera_desc{};
    camera_desc.ByteWidth=sizeof(Camera);
    camera_desc.Usage=D3D11_USAGE_DEFAULT;
    camera_desc.BindFlags=D3D11_BIND_CONSTANT_BUFFER;
    check(device->CreateBuffer(&camera_desc,nullptr,&cb),"Create DX11 camera constant buffer");
    D3D11_RASTERIZER_DESC rs{};
    rs.FillMode=D3D11_FILL_SOLID;
    rs.CullMode=D3D11_CULL_NONE;
    rs.DepthClipEnable=TRUE;
    ComPtr<ID3D11RasterizerState> raster;
    check(device->CreateRasterizerState(&rs,&raster),"Create rasterizer state");
    ShowWindow(wnd,SW_SHOW);
    UINT stride=sizeof(Vertex),offset=0;
    context->IASetInputLayout(layout.Get());
    context->IASetVertexBuffers(0,1,vb.GetAddressOf(),&stride,&offset);
    context->IASetIndexBuffer(ib.Get(),DXGI_FORMAT_R32_UINT,0);
    context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    context->RSSetState(raster.Get());
    D3D11_VIEWPORT viewport{};
    viewport.TopLeftX=0;viewport.TopLeftY=0;
    viewport.Width=float(width);viewport.Height=float(height);
    viewport.MinDepth=0;viewport.MaxDepth=1;
    context->RSSetViewports(1,&viewport);
    context->VSSetShader(vs.Get(),nullptr,0);
    context->VSSetConstantBuffers(0,1,cb.GetAddressOf());
    context->PSSetShader(ps.Get(),nullptr,0);
    context->OMSetRenderTargets(1,rtv.GetAddressOf(),dsv.Get());
    const float clear[4]={0.07f,0.10f,0.14f,1.0f};
    const auto start=GetTickCount64();
    MSG msg{};
    while(true){
        while(PeekMessageW(&msg,nullptr,0,0,PM_REMOVE)){
            if(msg.message==WM_QUIT)return;
            TranslateMessage(&msg);DispatchMessageW(&msg);
        }
        const float angle=float(GetTickCount64()-start)*0.0002f;
        const auto world=XMMatrixTranslation(-cx,-cy,-cz);
        const auto eye=XMVectorSet(std::sin(angle)*radius*2.4f,radius*0.7f,
                                   -std::cos(angle)*radius*2.4f,1.f);
        const auto view=XMMatrixLookAtLH(eye,XMVectorZero(),XMVectorSet(0,1,0,0));
        const auto proj=XMMatrixPerspectiveFovLH(XM_PIDIV4,float(width)/float(height),
                                               std::max(radius*0.0001f,0.01f),radius*12.f);
        Camera cam{};
        XMStoreFloat4x4(&cam.matrix,XMMatrixTranspose(world*view*proj));
        context->UpdateSubresource(cb.Get(),0,nullptr,&cam,0,0);
        context->ClearRenderTargetView(rtv.Get(),clear);
        context->ClearDepthStencilView(dsv.Get(),D3D11_CLEAR_DEPTH,1.f,0);
        context->DrawIndexed(UINT(lod.indices.size()),0,0);
        check(swap->Present(1,0),"DX11 swapchain present");
    }
}
}
int WINAPI wWinMain(HINSTANCE,HINSTANCE,PWSTR,int){
    int argc=0;
    wchar_t** argv=CommandLineToArgvW(GetCommandLineW(),&argc);
    if(!argv)return 1;
    try{
        if(argc<2){
            MessageBoxW(nullptr,L"Usage: dotm_dx11_mesh_viewer.exe <original-xxx-file> [mesh-name-filter]\n\nThis is a preview of original StaticMesh geometry, NOT a playable game.",
                        L"DOTM native DX11",MB_OK|MB_ICONINFORMATION);
            LocalFree(argv);return 2;
        }
        std::filesystem::path source(argv[1]);
        std::string filter;
        if(argc>=3){
            const auto n=WideCharToMultiByte(CP_UTF8,0,argv[2],-1,nullptr,0,nullptr,nullptr);
            if(n>0){
                std::string bytes(std::size_t(n),'\0');
                WideCharToMultiByte(CP_UTF8,0,argv[2],-1,bytes.data(),n,nullptr,nullptr);
                bytes.pop_back();filter=bytes;
            }
        }
        LocalFree(argv);
        run(source,filter);
        return 0;
    }catch(const std::exception&err){
        std::string message="Native DOTM DX11 preview failed: ";
        message+=err.what();
        MessageBoxA(nullptr,message.c_str(),"DOTM native DX11",MB_OK|MB_ICONERROR);
        return 1;
    }
}
#else
#error This DX11 proof-of-renderer is available only on native Windows.
#endif
