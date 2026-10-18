#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <d3d11_4.h>
#include <d3dcompiler.h>
#include <wrl/client.h>
#include <atomic>
#include <array>
#include <cstdio>
#include <cstdarg>
#include <cstring>
#include <vector>
#define ImTextureID ImU64
#include <imgui.h>
#include <reshade.hpp>
static reshade::api::effect_runtime *g_runtime = nullptr;
static DXGI_FORMAT TypedInputFormat(DXGI_FORMAT f) { return f; }
static bool SourceResolutionOverrideActive() { return false; }
static void Log(const char *format, ...) { va_list a; va_start(a,format); vprintf(format,a); va_end(a); puts(""); }
#include "sod2-hud.hpp"

int main(int argc, char **argv)
{
    if (!SodIsGameExecutable(L"C:\\Games\\StateOfDecay2-Win64-Shipping.exe") ||
        !SodIsGameExecutable(L"stateofdecay2-win64-shipping.EXE") ||
        SodIsGameExecutable(L"C:\\Games\\AnotherGame.exe") ||
        SodIsGameExecutable(L"StateOfDecay2-Win64-Shipping.exe.bak") ||
        SodCrc("123456789", 9) != 0xcbf43926u ||
        SodSupportsFormat(DXGI_FORMAT_R11G11B10_FLOAT)) return 10;
    using Microsoft::WRL::ComPtr;
    ComPtr<ID3D11Device> device; ComPtr<ID3D11DeviceContext> context;
    D3D_FEATURE_LEVEL level;
    HRESULT hr = D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_WARP,nullptr,0,nullptr,0,
        D3D11_SDK_VERSION,&device,&level,&context);
    if (FAILED(hr)) return 1;
    D3D11_TEXTURE2D_DESC desc = {};
    desc.Width=64;desc.Height=64;desc.MipLevels=1;desc.ArraySize=1;
    const bool tenbit=argc>1 && strcmp(argv[1],"10bit")==0;
    const bool bgra=argc>1 && strcmp(argv[1],"bgra")==0;
    desc.Format=tenbit ? DXGI_FORMAT_R10G10B10A2_UNORM :
        (bgra ? DXGI_FORMAT_B8G8R8A8_UNORM : DXGI_FORMAT_R8G8B8A8_UNORM);
    desc.SampleDesc.Count=1;
    const uint32_t rgbMask=tenbit ? 0x3fffffffu : 0x00ffffffu;
    const int alphaShift=tenbit ? 30 : 24;
    const uint32_t uiAlpha=tenbit ? 2u : 170u;
    desc.BindFlags=D3D11_BIND_RENDER_TARGET|D3D11_BIND_SHADER_RESOURCE;
    ComPtr<ID3D11Texture2D> source,destination,staging;
    if (FAILED(device->CreateTexture2D(&desc,nullptr,&source)) ||
        FAILED(device->CreateTexture2D(&desc,nullptr,&destination)) ||
        !SodEnsureTextures(device.Get(),desc)) return 2;
    auto readDesc=desc;readDesc.BindFlags=0;readDesc.Usage=D3D11_USAGE_STAGING;readDesc.CPUAccessFlags=D3D11_CPU_ACCESS_READ;
    if(FAILED(device->CreateTexture2D(&readDesc,nullptr,&staging)))return 3;
    D3D11_VIEWPORT originalViewport={2,3,48,49,0,1};
    context->RSSetViewports(1,&originalViewport);
    std::vector<uint32_t> clean(4096,0xff604020u),final=clean;
    // The inactive profile must leave this path untouched for other games.
    if (SodPackHud(context.Get(),destination.Get(),source.Get())) return 11;
    g_sod_hud=true;
    for (int frame=0;frame<5;++frame) {
        final=clean;
        const int left=10+frame*9;
        for(int y=20;frame<3&&y<25;++y)for(int x=left;x<left+12;++x)
            final[y*64+x]=0xffe8f0ffu;
        context->UpdateSubresource(g_sod_clean.Get(),0,nullptr,clean.data(),256,0);
        context->UpdateSubresource(source.Get(),0,nullptr,final.data(),256,0);
        // Last case deliberately keeps stale UI in Clean while no UI boundary
        // was observed in the new frame. It must produce an empty mask.
        if(frame==4)context->UpdateSubresource(g_sod_clean.Get(),0,nullptr,
            std::vector<uint32_t>(4096,0xffffffffu).data(),256,0);
        g_sod_captured=frame!=4;g_sod_source=frame!=4 ? source.Get() : nullptr;
        if(!SodPackHud(context.Get(),destination.Get(),source.Get()))return 4;
        context->CopyResource(staging.Get(),destination.Get());
        D3D11_MAPPED_SUBRESOURCE mapped;
        if(FAILED(context->Map(staging.Get(),0,D3D11_MAP_READ,0,&mapped)))return 5;
        int failures=0,maskPixels=0;
        for(int y=0;y<64;++y)for(int x=0;x<64;++x) {
            uint32_t pixel=reinterpret_cast<uint32_t *>(static_cast<unsigned char *>(mapped.pData)+y*mapped.RowPitch)[x];
            bool expected=frame<3&&x>=left-1&&x<=left+12&&y>=19&&y<=25;
            bool mask=(pixel>>alphaShift)==uiAlpha;
            if(mask)++maskPixels;
            if(mask!=expected || (pixel&rgbMask)!=(final[y*64+x]&rgbMask))++failures;
            // Presenter contract: protected pixels restore the exact game UI,
            // while unprotected world pixels retain the neural result.
            uint32_t neural=0x00887766u;
            uint32_t composed=mask?(pixel&rgbMask):neural;
            if(expected&&composed!=(final[y*64+x]&rgbMask))++failures;
            if(!expected&&composed!=neural)++failures;
        }
        context->Unmap(staging.Get(),0);
        UINT count=1;D3D11_VIEWPORT restored={};context->RSGetViewports(&count,&restored);
        if(memcmp(&originalViewport,&restored,sizeof(restored)))++failures;
        printf("frame=%d moving-UI-mask=%d failures=%d state-restored=%s\n",frame,maskPixels,failures,
            memcmp(&originalViewport,&restored,sizeof(restored))?"NO":"yes");
        if(failures)return 6;
    }
    puts("PASS: GPU mask, original RGB, moving UI, world preservation, D3D11 state restoration.");
    g_sod_source=nullptr;g_sod_clean.Reset();g_sod_final.Reset();g_sod_clean_srv.Reset();g_sod_final_srv.Reset();
    g_sod_state.Reset();g_sod_vs.Reset();g_sod_ps.Reset();g_sod_empty_ps.Reset();
    return 0;
}
