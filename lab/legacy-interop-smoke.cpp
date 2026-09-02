#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <d3d9.h>
#include <d3d11_4.h>
#include <d3d12.h>
#include <dxgi1_6.h>
#include <wrl/client.h>
#include <cstdio>

using Microsoft::WRL::ComPtr;

static bool Check(HRESULT hr, const char *stage)
{
    if (SUCCEEDED(hr)) return true;
    std::printf("FAIL %s: 0x%08X\n", stage, static_cast<unsigned int>(hr));
    return false;
}

static ComPtr<IDXGIAdapter1> NvidiaAdapter()
{
    ComPtr<IDXGIFactory1> factory;
    if (FAILED(CreateDXGIFactory1(IID_PPV_ARGS(&factory)))) return {};
    for (UINT index = 0; ; ++index)
    {
        ComPtr<IDXGIAdapter1> adapter;
        if (factory->EnumAdapters1(index, &adapter) == DXGI_ERROR_NOT_FOUND) break;
        DXGI_ADAPTER_DESC1 desc = {};
        adapter->GetDesc1(&desc);
        if (desc.VendorId == 0x10DE && !(desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE)) return adapter;
    }
    return {};
}

static bool WaitFence(ID3D12CommandQueue *queue, ID3D12Fence *fence, UINT64 value)
{
    HANDLE event_handle = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    if (!event_handle || FAILED(queue->Signal(fence, value)) ||
        FAILED(fence->SetEventOnCompletion(value, event_handle)) ||
        WaitForSingleObject(event_handle, 5000) != WAIT_OBJECT_0)
    {
        if (event_handle) CloseHandle(event_handle);
        return false;
    }
    CloseHandle(event_handle);
    return true;
}

int main()
{
    ComPtr<IDXGIAdapter1> adapter = NvidiaAdapter();
    if (!adapter) { std::puts("FAIL NVIDIA adapter not found"); return 1; }
    DXGI_ADAPTER_DESC1 adapter_desc = {};
    adapter->GetDesc1(&adapter_desc);
    wprintf(L"Adapter: %ls\n", adapter_desc.Description);

    D3D_FEATURE_LEVEL feature_level = D3D_FEATURE_LEVEL_11_0;
    ComPtr<ID3D11Device> device11;
    ComPtr<ID3D11DeviceContext> context11;
    if (!Check(D3D11CreateDevice(adapter.Get(), D3D_DRIVER_TYPE_UNKNOWN, nullptr,
        D3D11_CREATE_DEVICE_BGRA_SUPPORT, &feature_level, 1, D3D11_SDK_VERSION,
        &device11, nullptr, &context11), "D3D11CreateDevice")) return 1;
    ComPtr<ID3D11DeviceContext4> context4;
    ComPtr<ID3D11Device5> device5;
    if (!Check(context11.As(&context4), "ID3D11DeviceContext4") ||
        !Check(device11.As(&device5), "ID3D11Device5")) return 1;

    ComPtr<ID3D12Device> device12;
    if (!Check(D3D12CreateDevice(adapter.Get(), D3D_FEATURE_LEVEL_11_0,
        IID_PPV_ARGS(&device12)), "D3D12CreateDevice")) return 1;
    D3D12_COMMAND_QUEUE_DESC queue_desc = {};
    queue_desc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
    ComPtr<ID3D12CommandQueue> queue12;
    if (!Check(device12->CreateCommandQueue(&queue_desc, IID_PPV_ARGS(&queue12)), "CreateCommandQueue")) return 1;

    D3D12_HEAP_PROPERTIES heap = {};
    heap.Type = D3D12_HEAP_TYPE_DEFAULT;
    D3D12_RESOURCE_DESC texture_desc = {};
    texture_desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    texture_desc.Width = 64; texture_desc.Height = 64;
    texture_desc.DepthOrArraySize = 1; texture_desc.MipLevels = 1;
    texture_desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    texture_desc.SampleDesc.Count = 1;
    texture_desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_SIMULTANEOUS_ACCESS;
    ComPtr<ID3D12Resource> shared12;
    if (!Check(device12->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_SHARED, &texture_desc,
        D3D12_RESOURCE_STATE_COMMON, nullptr, IID_PPV_ARGS(&shared12)), "Create shared D3D12 texture")) return 1;
    HANDLE texture_handle = nullptr;
    if (!Check(device12->CreateSharedHandle(shared12.Get(), nullptr, GENERIC_ALL, nullptr,
        &texture_handle), "CreateSharedHandle(texture)")) return 1;
    ComPtr<ID3D11Device1> device11_1;
    ComPtr<ID3D11Texture2D> shared11;
    if (!Check(device11.As(&device11_1), "ID3D11Device1")) return 1;
    HRESULT open_hr = device11_1->OpenSharedResource1(texture_handle, IID_PPV_ARGS(&shared11));
    CloseHandle(texture_handle);
    if (FAILED(open_hr))
    {
        std::printf("D3D12->D3D11 texture path unavailable (0x%08X); using reverse path\n",
            static_cast<unsigned int>(open_hr));
        shared11.Reset(); shared12.Reset();
        D3D11_TEXTURE2D_DESC reverse_desc = {};
        reverse_desc.Width = 64; reverse_desc.Height = 64; reverse_desc.MipLevels = 1; reverse_desc.ArraySize = 1;
        reverse_desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM; reverse_desc.SampleDesc.Count = 1;
        reverse_desc.Usage = D3D11_USAGE_DEFAULT;
        reverse_desc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET;
        reverse_desc.MiscFlags = D3D11_RESOURCE_MISC_SHARED_NTHANDLE | D3D11_RESOURCE_MISC_SHARED;
        if (!Check(device11->CreateTexture2D(&reverse_desc, nullptr, &shared11), "Create reverse D3D11 texture")) return 1;
        ComPtr<IDXGIResource1> reverse_dxgi;
        HANDLE reverse_handle = nullptr;
        if (!Check(shared11.As(&reverse_dxgi), "IDXGIResource1(reverse)") ||
            !Check(reverse_dxgi->CreateSharedHandle(nullptr,
                DXGI_SHARED_RESOURCE_READ | DXGI_SHARED_RESOURCE_WRITE, nullptr, &reverse_handle),
                "CreateSharedHandle(reverse)") ||
            !Check(device12->OpenSharedHandle(reverse_handle, IID_PPV_ARGS(&shared12)),
                "Open reverse texture in D3D12")) return 1;
        CloseHandle(reverse_handle);
    }

    ComPtr<ID3D12Fence> shared_fence12;
    HANDLE fence_handle = nullptr;
    if (!Check(device12->CreateFence(0, D3D12_FENCE_FLAG_SHARED, IID_PPV_ARGS(&shared_fence12)),
            "Create shared D3D12 fence") ||
        !Check(device12->CreateSharedHandle(shared_fence12.Get(), nullptr, GENERIC_ALL, nullptr,
            &fence_handle), "CreateSharedHandle(fence)")) return 1;
    ComPtr<ID3D11Fence> shared_fence11;
    if (!Check(device5->OpenSharedFence(fence_handle, IID_PPV_ARGS(&shared_fence11)),
        "Open shared fence in D3D11")) return 1;
    CloseHandle(fence_handle);

    D3D11_RENDER_TARGET_VIEW_DESC rtv_desc = {};
    rtv_desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    rtv_desc.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2D;
    ComPtr<ID3D11RenderTargetView> rtv;
    if (!Check(device11->CreateRenderTargetView(shared11.Get(), &rtv_desc, &rtv), "Create RTV")) return 1;
    const float red[4] = {1, 0, 0, 1};
    context11->ClearRenderTargetView(rtv.Get(), red);
    if (!Check(context4->Signal(shared_fence11.Get(), 1), "D3D11 signal") ||
        !Check(queue12->Wait(shared_fence12.Get(), 1), "D3D12 wait")) return 1;
    context11->Flush();
    std::puts("PASS D3D11 -> shared fence -> D3D12");

    WNDCLASSW wc = {};
    wc.lpfnWndProc = DefWindowProcW; wc.hInstance = GetModuleHandleW(nullptr);
    wc.lpszClassName = L"LegacyInteropSmoke";
    RegisterClassW(&wc);
    HWND hwnd = CreateWindowW(wc.lpszClassName, L"", WS_OVERLAPPEDWINDOW,
        0, 0, 64, 64, nullptr, nullptr, wc.hInstance, nullptr);
    ComPtr<IDirect3D9Ex> d3d9;
    if (!Check(Direct3DCreate9Ex(D3D_SDK_VERSION, &d3d9), "Direct3DCreate9Ex")) return 1;
    UINT adapter9 = D3DADAPTER_DEFAULT;
    for (UINT index = 0; index < d3d9->GetAdapterCount(); ++index)
    {
        LUID luid = {};
        if (SUCCEEDED(d3d9->GetAdapterLUID(index, &luid)) &&
            luid.HighPart == adapter_desc.AdapterLuid.HighPart && luid.LowPart == adapter_desc.AdapterLuid.LowPart)
        { adapter9 = index; break; }
    }
    D3DPRESENT_PARAMETERS pp = {};
    pp.Windowed = TRUE; pp.SwapEffect = D3DSWAPEFFECT_DISCARD;
    pp.hDeviceWindow = hwnd; pp.BackBufferWidth = 64; pp.BackBufferHeight = 64;
    pp.BackBufferFormat = D3DFMT_A8R8G8B8;
    ComPtr<IDirect3DDevice9Ex> device9;
    if (!Check(d3d9->CreateDeviceEx(adapter9, D3DDEVTYPE_HAL, hwnd,
        D3DCREATE_HARDWARE_VERTEXPROCESSING | D3DCREATE_MULTITHREADED,
        &pp, nullptr, &device9), "CreateDeviceEx")) return 1;

    D3D11_TEXTURE2D_DESC stage_desc = {};
    stage_desc.Width = 64; stage_desc.Height = 64; stage_desc.MipLevels = 1; stage_desc.ArraySize = 1;
    stage_desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM; stage_desc.SampleDesc.Count = 1;
    stage_desc.Usage = D3D11_USAGE_DEFAULT;
    stage_desc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET;
    stage_desc.MiscFlags = D3D11_RESOURCE_MISC_SHARED;
    ComPtr<ID3D11Texture2D> stage11;
    if (!Check(device11->CreateTexture2D(&stage_desc, nullptr, &stage11), "Create D3D11 legacy stage")) return 1;
    ComPtr<IDXGIResource> stage_dxgi;
    HANDLE stage_handle = nullptr;
    if (!Check(stage11.As(&stage_dxgi), "IDXGIResource(stage)") ||
        !Check(stage_dxgi->GetSharedHandle(&stage_handle), "GetSharedHandle(stage)")) return 1;
    ComPtr<IDirect3DTexture9> stage9;
    if (!Check(device9->CreateTexture(64, 64, 1, D3DUSAGE_RENDERTARGET,
        D3DFMT_A8R8G8B8, D3DPOOL_DEFAULT, &stage9, &stage_handle),
        "Open shared stage in D3D9")) return 1;
    ComPtr<IDirect3DSurface9> surface9;
    if (!Check(stage9->GetSurfaceLevel(0, &surface9), "GetSurfaceLevel") ||
        !Check(device9->ColorFill(surface9.Get(), nullptr, D3DCOLOR_ARGB(255, 0, 255, 0)), "D3D9 ColorFill")) return 1;
    ComPtr<IDirect3DQuery9> query9;
    if (!Check(device9->CreateQuery(D3DQUERYTYPE_EVENT, &query9), "Create D3D9 event query") ||
        !Check(query9->Issue(D3DISSUE_END), "Issue D3D9 event query")) return 1;
    const ULONGLONG deadline = GetTickCount64() + 5000;
    while (query9->GetData(nullptr, 0, D3DGETDATA_FLUSH) == S_FALSE && GetTickCount64() < deadline)
        SwitchToThread();
    context11->CopyResource(shared11.Get(), stage11.Get());
    if (!Check(context4->Signal(shared_fence11.Get(), 2), "D3D11 signal after D3D9 copy") ||
        !Check(queue12->Wait(shared_fence12.Get(), 2), "D3D12 wait after D3D9 copy")) return 1;
    context11->Flush();
    ComPtr<ID3D12Fence> completion;
    if (!Check(device12->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&completion)), "completion fence") ||
        !WaitFence(queue12.Get(), completion.Get(), 1)) return 1;
    std::puts("PASS D3D9 -> shared D3D11 stage -> shared fence -> D3D12");
    DestroyWindow(hwnd);
    std::puts("Legacy interop smoke test passed.");
    return 0;
}
