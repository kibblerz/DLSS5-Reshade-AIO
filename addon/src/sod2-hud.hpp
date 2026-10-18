#pragma once
// State of Decay 2 HUD preservation experiment.
// Captures the scene at the first known UI draw; stores a UI difference mask
// in the immutable input frame alpha so the asynchronous presenter always
// receives the mask belonging to its own frame. No cross-frame UI comparison.
// Shader CRCs: https://anticowl.github.io/files/ShaderToggler.ini
#include <unordered_set>
#include <unordered_map>

static bool g_sod_hud = false;

static bool SodIsGameExecutable(const wchar_t *path)
{
    const wchar_t *base = wcsrchr(path, L'\\');
    base = base ? base + 1 : path;
    return _wcsicmp(base, L"StateOfDecay2-Win64-Shipping.exe") == 0;
}

static bool SodSupportsFormat(DXGI_FORMAT format)
{
    switch (TypedInputFormat(format)) {
    case DXGI_FORMAT_R8G8B8A8_UNORM:
    case DXGI_FORMAT_B8G8R8A8_UNORM:
    case DXGI_FORMAT_R10G10B10A2_UNORM:
        return true;
    default: return false;
    }
}
// Use a Windows lock: the game ships an older MSVC runtime whose std::mutex
// layout is incompatible with the current compiler's constexpr constructor.
static SRWLOCK g_sod_mutex = SRWLOCK_INIT;
struct SodLock {
    SodLock() { AcquireSRWLockExclusive(&g_sod_mutex); }
    ~SodLock() { ReleaseSRWLockExclusive(&g_sod_mutex); }
};
static std::unordered_set<uint64_t> g_sod_ui_shaders;
struct SodBound { bool vs = false; bool ps = false; };
static std::unordered_map<uint64_t, SodBound> g_sod_bound;
static Microsoft::WRL::ComPtr<ID3D11Texture2D> g_sod_clean, g_sod_final;
// Non-owning identity, cleared at Present. A reference to the backbuffer here
// would prevent DXGI ResizeBuffers from releasing the old buffers.
static ID3D11Resource *g_sod_source = nullptr;
static Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> g_sod_clean_srv, g_sod_final_srv;
static Microsoft::WRL::ComPtr<ID3D11VertexShader> g_sod_vs;
static Microsoft::WRL::ComPtr<ID3D11PixelShader> g_sod_ps, g_sod_empty_ps;
static Microsoft::WRL::ComPtr<ID3DDeviceContextState> g_sod_state;
static bool g_sod_captured = false;
static unsigned long long g_sod_capture_count = 0, g_sod_pack_count = 0;

static uint32_t SodCrc(const void *code, size_t size)
{
    static constexpr auto table = [] {
        std::array<uint32_t, 256> values = {};
        for (uint32_t i = 0; i < values.size(); ++i) {
            uint32_t value = i;
            for (int b = 0; b < 8; ++b)
                value = (value >> 1) ^ (0xEDB88320u & (0u - (value & 1u)));
            values[i] = value;
        }
        return values;
    }();
    uint32_t crc = 0xFFFFFFFFu;
    const auto *bytes = static_cast<const unsigned char *>(code);
    for (size_t i = 0; i < size; ++i)
        crc = (crc >> 8) ^ table[(crc ^ bytes[i]) & 0xffu];
    return ~crc;
}

static void SodInitPipeline(reshade::api::device *device, reshade::api::pipeline_layout,
    uint32_t count, const reshade::api::pipeline_subobject *objects, reshade::api::pipeline pipeline)
{
    if (!g_sod_hud || device->get_api() != reshade::api::device_api::d3d11) return;
    for (uint32_t i = 0; i < count; ++i) {
        if (objects[i].type != reshade::api::pipeline_subobject_type::pixel_shader &&
            objects[i].type != reshade::api::pipeline_subobject_type::vertex_shader) continue;
        const auto *shader = static_cast<const reshade::api::shader_desc *>(objects[i].data);
        if (!shader || !shader->code || !shader->code_size) continue;
        const uint32_t crc = SodCrc(shader->code, shader->code_size);
        const bool vertex = objects[i].type == reshade::api::pipeline_subobject_type::vertex_shader;
        const bool match = vertex ? (crc == 1243086938u || crc == 4087502031u) :
            (crc == 984419267u || crc == 3110570065u || crc == 1367753804u ||
             crc == 1833449711u || crc == 83750393u || crc == 2992494283u);
        if (match) {
            SodLock lock;
            g_sod_ui_shaders.insert(pipeline.handle);
            Log("SOD2 HUD: matched %s shader CRC=%u", vertex ? "vertex" : "pixel", crc);
        }
    }
}
static void SodDestroyPipeline(reshade::api::device *, reshade::api::pipeline pipeline)
{
    if (!g_sod_hud) return;
    SodLock lock;
    g_sod_ui_shaders.erase(pipeline.handle);
}

static void SodResetCommandList(reshade::api::command_list *cmd)
{
    if (!g_sod_hud) return;
    SodLock lock;
    g_sod_bound.erase(cmd->get_native());
}

static void SodDestroyDevice(reshade::api::device *device)
{
    if (!g_sod_hud || device->get_api() != reshade::api::device_api::d3d11) return;
    if (g_sod_clean) {
        Microsoft::WRL::ComPtr<ID3D11Device> owner;
        g_sod_clean->GetDevice(&owner);
        if (reinterpret_cast<uint64_t>(owner.Get()) != device->get_native()) return;
    }
    g_sod_source = nullptr; g_sod_captured = false;
    g_sod_clean.Reset(); g_sod_final.Reset(); g_sod_clean_srv.Reset(); g_sod_final_srv.Reset();
    g_sod_vs.Reset(); g_sod_ps.Reset(); g_sod_empty_ps.Reset(); g_sod_state.Reset();
    SodLock lock;
    g_sod_bound.clear(); g_sod_ui_shaders.clear();
}
static void SodBindPipeline(reshade::api::command_list *cmd, reshade::api::pipeline_stage stages,
    reshade::api::pipeline pipeline)
{
    if (!g_sod_hud || cmd->get_device()->get_api() != reshade::api::device_api::d3d11) return;
    SodLock lock;
    auto &bound = g_sod_bound[cmd->get_native()];
    const bool ui = g_sod_ui_shaders.contains(pipeline.handle);
    if ((stages & reshade::api::pipeline_stage::vertex_shader) != static_cast<reshade::api::pipeline_stage>(0)) bound.vs = ui;
    if ((stages & reshade::api::pipeline_stage::pixel_shader) != static_cast<reshade::api::pipeline_stage>(0)) bound.ps = ui;
}

static bool SodEnsureTextures(ID3D11Device *device, const D3D11_TEXTURE2D_DESC &desc)
{
    if (!SodSupportsFormat(desc.Format) || desc.SampleDesc.Count != 1 ||
        desc.MipLevels != 1 || desc.ArraySize != 1) return false;
    if (g_sod_clean) {
        D3D11_TEXTURE2D_DESC previous; g_sod_clean->GetDesc(&previous);
        Microsoft::WRL::ComPtr<ID3D11Device> owner; g_sod_clean->GetDevice(&owner);
        if (owner.Get() == device && previous.Width == desc.Width && previous.Height == desc.Height && previous.Format == desc.Format && g_sod_ps && g_sod_empty_ps && g_sod_state) return true;
    }
    g_sod_clean.Reset(); g_sod_final.Reset(); g_sod_clean_srv.Reset(); g_sod_final_srv.Reset();
    g_sod_vs.Reset(); g_sod_ps.Reset(); g_sod_empty_ps.Reset(); g_sod_state.Reset();
    D3D11_TEXTURE2D_DESC copy = desc;
    copy.Usage = D3D11_USAGE_DEFAULT; copy.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    copy.CPUAccessFlags = 0; copy.MiscFlags = 0;
    HRESULT hr = device->CreateTexture2D(&copy, nullptr, &g_sod_clean);
    if (SUCCEEDED(hr)) hr = device->CreateTexture2D(&copy, nullptr, &g_sod_final);
    D3D11_SHADER_RESOURCE_VIEW_DESC srv = {};
    srv.Format = TypedInputFormat(desc.Format); srv.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
    srv.Texture2D.MipLevels = 1;
    if (SUCCEEDED(hr)) hr = device->CreateShaderResourceView(g_sod_clean.Get(), &srv, &g_sod_clean_srv);
    if (SUCCEEDED(hr)) hr = device->CreateShaderResourceView(g_sod_final.Get(), &srv, &g_sod_final_srv);
    const char *hlsl =
        "Texture2D<float4> Final:register(t0);Texture2D<float4> Clean:register(t1);"
        "struct O{float4 p:SV_Position;};O VS(uint id:SV_VertexID){O o;float2 p=float2((id<<1)&2,id&2);o.p=float4(p*float2(2,-2)+float2(-1,1),0,1);return o;}"
        "float4 PS(O i):SV_Target{int2 p=int2(i.p.xy);uint w,h;Final.GetDimensions(w,h);float mask=0;"
        "for(int y=-1;y<=1;y++)for(int x=-1;x<=1;x++){int2 q=clamp(p+int2(x,y),int2(0,0),int2(w-1,h-1));"
        "float3 d=abs(Final.Load(int3(q,0)).rgb-Clean.Load(int3(q,0)).rgb);mask=max(mask,step(0.0015,max(d.r,max(d.g,d.b))));}"
        // Alpha is frame-local metadata: 1/3 = world, 2/3 = UI. An unmodified
        // opaque input has alpha 1 and must not be mistaken for a full UI mask.
        "return float4(Final.Load(int3(p,0)).rgb,(1+mask)/3);}"
        "float4 PSNoMask(O i):SV_Target{return float4(Final.Load(int3(int2(i.p.xy),0)).rgb,1.0/3.0);}";
    Microsoft::WRL::ComPtr<ID3DBlob> vs, ps, empty_ps, errors;
    if (SUCCEEDED(hr)) hr = D3DCompile(hlsl, strlen(hlsl), nullptr, nullptr, nullptr, "VS", "vs_5_0", 0, 0, &vs, &errors);
    if (SUCCEEDED(hr)) hr = D3DCompile(hlsl, strlen(hlsl), nullptr, nullptr, nullptr, "PS", "ps_5_0", 0, 0, &ps, &errors);
    if (SUCCEEDED(hr)) hr = device->CreateVertexShader(vs->GetBufferPointer(), vs->GetBufferSize(), nullptr, &g_sod_vs);
    if (SUCCEEDED(hr)) hr = device->CreatePixelShader(ps->GetBufferPointer(), ps->GetBufferSize(), nullptr, &g_sod_ps);
    if (SUCCEEDED(hr)) hr = D3DCompile(hlsl, strlen(hlsl), nullptr, nullptr, nullptr, "PSNoMask", "ps_5_0", 0, 0, &empty_ps, &errors);
    if (SUCCEEDED(hr)) hr = device->CreatePixelShader(empty_ps->GetBufferPointer(), empty_ps->GetBufferSize(), nullptr, &g_sod_empty_ps);
    Microsoft::WRL::ComPtr<ID3D11Device1> device1;
    if (SUCCEEDED(hr)) hr = device->QueryInterface(IID_PPV_ARGS(&device1));
    const D3D_FEATURE_LEVEL requested = device->GetFeatureLevel(); D3D_FEATURE_LEVEL selected;
    if (SUCCEEDED(hr)) hr = device1->CreateDeviceContextState(0, &requested, 1, D3D11_SDK_VERSION,
        __uuidof(ID3D11Device), &selected, &g_sod_state);
    if (FAILED(hr)) { Log("SOD2 HUD: mask resource creation failed hr=%08X", static_cast<unsigned int>(hr)); return false; }
    return true;
}

static void SodBeforeDraw(reshade::api::command_list *cmd)
{
    if (!g_sod_hud || cmd->get_device()->get_api() != reshade::api::device_api::d3d11) return;
    auto *context = reinterpret_cast<ID3D11DeviceContext *>(cmd->get_native());
    if (context->GetType() != D3D11_DEVICE_CONTEXT_IMMEDIATE) return;
    // Capture state belongs only to the game's immediate-context thread.
    if (g_sod_captured || !g_runtime || SourceResolutionOverrideActive()) return;
    {
        SodLock lock;
        const auto it = g_sod_bound.find(cmd->get_native());
        if (it == g_sod_bound.end() || (!it->second.vs && !it->second.ps)) return;
    }
    Microsoft::WRL::ComPtr<ID3D11RenderTargetView> target; context->OMGetRenderTargets(1, &target, nullptr);
    if (!target) return;
    Microsoft::WRL::ComPtr<ID3D11Resource> resource; target->GetResource(&resource);
    if (reinterpret_cast<uint64_t>(resource.Get()) != g_runtime->get_current_back_buffer().handle) return;
    Microsoft::WRL::ComPtr<ID3D11Texture2D> texture; if (FAILED(resource.As(&texture))) return;
    D3D11_TEXTURE2D_DESC desc; texture->GetDesc(&desc);
    Microsoft::WRL::ComPtr<ID3D11Device> device; context->GetDevice(&device);
    if (!SodEnsureTextures(device.Get(), desc)) return;
    context->CopyResource(g_sod_clean.Get(), resource.Get());
    g_sod_source = resource.Get(); g_sod_captured = true;
    if (++g_sod_capture_count <= 3 || g_sod_capture_count % 600 == 0)
        Log("SOD2 HUD: pre-UI scene captured count=%llu %ux%u", g_sod_capture_count, desc.Width, desc.Height);
}
static bool SodDraw(reshade::api::command_list *cmd, uint32_t, uint32_t, uint32_t, uint32_t)
{ SodBeforeDraw(cmd); return false; }
static bool SodDrawIndexed(reshade::api::command_list *cmd, uint32_t, uint32_t, uint32_t, int32_t, uint32_t)
{ SodBeforeDraw(cmd); return false; }

static bool SodPackHud(ID3D11DeviceContext *context, ID3D11Texture2D *destination, ID3D11Texture2D *source)
{
    if (!g_sod_hud) return false;
    D3D11_TEXTURE2D_DESC desc, dst; source->GetDesc(&desc); destination->GetDesc(&dst);
    if (desc.Width != dst.Width || desc.Height != dst.Height || desc.Format != dst.Format) return false;
    Microsoft::WRL::ComPtr<ID3D11Device> device; context->GetDevice(&device);
    if (!SodEnsureTextures(device.Get(), desc)) return false;
    const bool has_ui = g_sod_captured && g_sod_source == source;
    Microsoft::WRL::ComPtr<ID3D11RenderTargetView> target;
    D3D11_RENDER_TARGET_VIEW_DESC rtv = {}; rtv.Format = TypedInputFormat(dst.Format); rtv.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2D;
    if (FAILED(device->CreateRenderTargetView(destination, &rtv, &target))) return false;
    Microsoft::WRL::ComPtr<ID3D11DeviceContext1> context1;
    if (FAILED(context->QueryInterface(IID_PPV_ARGS(&context1)))) return false;
    Microsoft::WRL::ComPtr<ID3DDeviceContextState> previous;
    context1->SwapDeviceContextState(g_sod_state.Get(), &previous);
    context->CopyResource(g_sod_final.Get(), source);
    ID3D11RenderTargetView *rtvs[] = {target.Get()}; context->OMSetRenderTargets(1, rtvs, nullptr);
    D3D11_VIEWPORT viewport = {0, 0, static_cast<float>(desc.Width), static_cast<float>(desc.Height), 0, 1};
    context->RSSetViewports(1, &viewport); context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    context->VSSetShader(g_sod_vs.Get(), nullptr, 0);
    context->PSSetShader(has_ui ? g_sod_ps.Get() : g_sod_empty_ps.Get(), nullptr, 0);
    ID3D11ShaderResourceView *views[] = {g_sod_final_srv.Get(), g_sod_clean_srv.Get()};
    context->PSSetShaderResources(0, 2, views); context->Draw(3, 0);
    ID3D11ShaderResourceView *empty[] = {nullptr, nullptr}; context->PSSetShaderResources(0, 2, empty);
    context->OMSetRenderTargets(0, nullptr, nullptr);
    context1->SwapDeviceContextState(previous.Get(), nullptr);
    if (++g_sod_pack_count <= 3 || g_sod_pack_count % 600 == 0)
        Log("SOD2 HUD: frame-local UI mask packed count=%llu", g_sod_pack_count);
    return true;
}

static void SodFrameEnd(reshade::api::command_queue *queue, reshade::api::swapchain *swapchain,
    const reshade::api::rect *, const reshade::api::rect *, uint32_t, const reshade::api::rect *)
{
    if (!g_sod_hud || queue->get_device()->get_api() != reshade::api::device_api::d3d11) return;
    // Match the window, not the current buffer index, which can rotate on a
    // flip-model swapchain. Proxy presentation uses a separate D3D12 device.
    if (g_runtime && swapchain->get_hwnd() == g_runtime->get_hwnd()) {
        g_sod_captured = false;
        g_sod_source = nullptr;
    }
}
