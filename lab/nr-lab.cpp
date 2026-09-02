// Standalone DLSS Neural Rendering contract laboratory.
//
// This program deliberately does not load ReShade, ShortFuse, or the old feeder.
// It creates raw NGX feature 18, evaluates a low-resolution deterministic frame
// into a larger UAV, reads the result back, and proves whether the entire native
// output was written.  The same parameter contract is intended to be shared by
// the eventual ReShade addon.

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <d3d12.h>
#include <dxgi1_6.h>
#include <nvsdk_ngx.h>
#include <nvsdk_ngx_params.h>
#include <nvsdk_ngx_defs_dlssd.h>
#include <nvsdk_ngx_defs_dlssg.h>

#include <algorithm>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

static constexpr NVSDK_NGX_Feature kFeatureDlssNr = static_cast<NVSDK_NGX_Feature>(0x12);
// Exported by this nvngx_dlssnr.dll's NVSDK_NGX_GetApplicationId routine.
// Using an unrelated sample ID makes the driver core reject this snippet family.
[[maybe_unused]] static constexpr unsigned long long kDlssNrApplicationId = 0x0E658703ULL;
// ShortFuse initializes the generic custom driver core with this CMS ID, then
// attaches the NR snippet (whose own exported application ID is E658703).
static constexpr unsigned long long kGenericCustomCoreId = 0x0876232CULL;

enum class ColorProfile { Srgb, ScRgb, Hdr10 };

struct Options {
    UINT input_w = 960;
    UINT input_h = 540;
    UINT output_w = 1920;
    UINT output_h = 1080;
    UINT frames = 8;
    int model = 1;
    ColorProfile profile = ColorProfile::Srgb;
    float intensity = 1.0f;
    float local_tone = 1.0f;
    float local_structure = 1.0f;
    float skin_structure = -1.0f;
    bool nr_only = false;
    bool compact_nr = false;
    bool framegen = false;
};

static UINT NrWidth(const Options &o) { return o.compact_nr ? o.input_w : o.output_w; }
static UINT NrHeight(const Options &o) { return o.compact_nr ? o.input_h : o.output_h; }

struct Context {
    IDXGIFactory6 *factory = nullptr;
    IDXGIAdapter1 *adapter = nullptr;
    IDXGISwapChain1 *swapchain = nullptr;
    HWND window = nullptr;
    ID3D12Device *device = nullptr;
    ID3D12CommandQueue *queue = nullptr;
    ID3D12CommandAllocator *allocator = nullptr;
    ID3D12GraphicsCommandList *list = nullptr;
    ID3D12Fence *fence = nullptr;
    HANDLE fence_event = nullptr;
    UINT64 fence_value = 0;
    NVSDK_NGX_Parameter *params = nullptr;
    NVSDK_NGX_Handle *feature = nullptr;
    NVSDK_NGX_Handle *sr_feature = nullptr;
    NVSDK_NGX_Handle *fg_feature = nullptr;
    bool ngx_initialized = false;
};

static Context g;
static char g_dir[MAX_PATH] = {};
static char g_log_path[MAX_PATH] = {};

using NgxCoreInitD3D12 = NVSDK_NGX_Result (NVSDK_CONV *)(unsigned long long, const wchar_t *,
    ID3D12Device *, NVSDK_NGX_Version);
using NgxSnippetInitD3D12Ext = NVSDK_NGX_Result (NVSDK_CONV *)(unsigned long long, const wchar_t *,
    ID3D12Device *, NVSDK_NGX_Version, const NVSDK_NGX_Parameter *);
using NgxBridgeInitD3D12Ext = NVSDK_NGX_Result (NVSDK_CONV *)(NgxSnippetInitD3D12Ext,
    unsigned long long, const wchar_t *, ID3D12Device *, NVSDK_NGX_Version,
    const NVSDK_NGX_Parameter *);
using NgxAllocateParameters = NVSDK_NGX_Result (NVSDK_CONV *)(NVSDK_NGX_Parameter **);
using NgxGetCapabilityParameters = NVSDK_NGX_Result (NVSDK_CONV *)(NVSDK_NGX_Parameter **);
using NgxPopulateParameters = NVSDK_NGX_Result (NVSDK_CONV *)(NVSDK_NGX_Parameter *);
using NgxBridgePopulateParameters = NVSDK_NGX_Result (NVSDK_CONV *)(NgxPopulateParameters, NVSDK_NGX_Parameter *);
using NgxDestroyParameters = NVSDK_NGX_Result (NVSDK_CONV *)(NVSDK_NGX_Parameter *);
using NgxCreateFeature = NVSDK_NGX_Result (NVSDK_CONV *)(ID3D12GraphicsCommandList *,
    NVSDK_NGX_Feature, NVSDK_NGX_Parameter *, NVSDK_NGX_Handle **);
using NgxEvaluateFeature = NVSDK_NGX_Result (NVSDK_CONV *)(ID3D12GraphicsCommandList *,
    const NVSDK_NGX_Handle *, const NVSDK_NGX_Parameter *, PFN_NVSDK_NGX_ProgressCallback_C);
using NgxReleaseFeature = NVSDK_NGX_Result (NVSDK_CONV *)(NVSDK_NGX_Handle *);
using NgxShutdownD3D12 = NVSDK_NGX_Result (NVSDK_CONV *)(ID3D12Device *);
using NgxBridgeCreateFeature = NVSDK_NGX_Result (NVSDK_CONV *)(NgxCreateFeature,
    ID3D12GraphicsCommandList *, NVSDK_NGX_Feature, NVSDK_NGX_Parameter *, NVSDK_NGX_Handle **);
using NgxBridgeEvaluateFeature = NVSDK_NGX_Result (NVSDK_CONV *)(NgxEvaluateFeature,
    ID3D12GraphicsCommandList *, const NVSDK_NGX_Handle *, const NVSDK_NGX_Parameter *,
    PFN_NVSDK_NGX_ProgressCallback_C);
using NgxBridgeReleaseFeature = NVSDK_NGX_Result (NVSDK_CONV *)(NgxReleaseFeature, NVSDK_NGX_Handle *);
using NgxBridgeShutdownD3D12 = NVSDK_NGX_Result (NVSDK_CONV *)(NgxShutdownD3D12, ID3D12Device *);

static HMODULE g_core_module = nullptr;
static HMODULE g_nr_module = nullptr;
static HMODULE g_dlss_module = nullptr;
static HMODULE g_dlssg_module = nullptr;
static HMODULE g_bridge_module = nullptr;
static HMODULE g_nvapi_module = nullptr;
static LUID g_nvapi_luid = {};
static bool g_has_nvapi_luid = false;
static NgxDestroyParameters g_core_destroy_parameters = nullptr;
static NgxShutdownD3D12 g_core_shutdown = nullptr;
static NgxCreateFeature g_nr_create_feature = nullptr;
static NgxEvaluateFeature g_nr_evaluate_feature = nullptr;
static NgxReleaseFeature g_nr_release_feature = nullptr;
static NgxShutdownD3D12 g_nr_shutdown = nullptr;
static NgxCreateFeature g_dlss_create_feature = nullptr;
static NgxEvaluateFeature g_dlss_evaluate_feature = nullptr;
static NgxCreateFeature g_dlssg_create_feature = nullptr;
static NgxEvaluateFeature g_dlssg_evaluate_feature = nullptr;
static NgxReleaseFeature g_dlssg_release_feature = nullptr;
static NgxPopulateParameters g_dlssg_populate_parameters = nullptr;
static NgxBridgeCreateFeature g_bridge_create_feature = nullptr;
static NgxBridgeEvaluateFeature g_bridge_evaluate_feature = nullptr;
static NgxBridgeReleaseFeature g_bridge_release_feature = nullptr;
static NgxBridgeShutdownD3D12 g_bridge_shutdown = nullptr;
static NgxBridgePopulateParameters g_bridge_populate_parameters = nullptr;
static NgxPopulateParameters g_nr_populate_parameters = nullptr;
static NgxPopulateParameters g_nr_compute_scaling_ratio = nullptr;
static float g_nr_resolved_scaling_ratio = 1.0f;
static NVSDK_NGX_Parameter *g_traced_params = nullptr;

using NvApiQueryInterface = void *(__cdecl *)(unsigned int);
using NvApiInitialize = int (__cdecl *)();
using NvApiEnumPhysicalGpus = int (__cdecl *)(void **, int *);
using NvApiGpuGetArchInfo = int (__cdecl *)(void *, void *);
using NvApiGetLogicalGpuFromPhysicalGpu = int (__cdecl *)(void *, void **);
using NvApiGpuGetLogicalGpuInfo = int (__cdecl *)(void *, void *);
static void Log(const char *format, ...);

static bool InitNvApi()
{
    g_nvapi_module = LoadLibraryW(L"nvapi64.dll");
    if (g_nvapi_module == nullptr) {
        Log("FAIL LoadLibrary(nvapi64.dll): %lu", GetLastError());
        return false;
    }
    auto query = reinterpret_cast<NvApiQueryInterface>(GetProcAddress(g_nvapi_module, "nvapi_QueryInterface"));
    if (query == nullptr) {
        Log("FAIL nvapi_QueryInterface export is missing");
        return false;
    }
    // Public NVAPI interface ID for NvAPI_Initialize. A game has normally made
    // this call before its first render device; the standalone harness must do
    // that host-side prerequisite itself.
    auto initialize = reinterpret_cast<NvApiInitialize>(query(0x0150E828U));
    if (initialize == nullptr) {
        Log("FAIL nvapi_QueryInterface(NvAPI_Initialize) returned null");
        return false;
    }
    const int status = initialize();
    Log("NvAPI_Initialize = %d", status);
    return status == 0;
}

static void ProbeNvApiHardware(const LUID &d3d_luid)
{
    auto query = reinterpret_cast<NvApiQueryInterface>(GetProcAddress(g_nvapi_module, "nvapi_QueryInterface"));
    auto enumerate = reinterpret_cast<NvApiEnumPhysicalGpus>(query(0xE5AC921FU));
    auto get_arch = reinterpret_cast<NvApiGpuGetArchInfo>(query(0xD8265D24U));
    auto get_logical = reinterpret_cast<NvApiGetLogicalGpuFromPhysicalGpu>(query(0xADD604D1U));
    auto get_logical_info = reinterpret_cast<NvApiGpuGetLogicalGpuInfo>(query(0x842B066EU));
    if (enumerate == nullptr || get_arch == nullptr || get_logical == nullptr || get_logical_info == nullptr) {
        Log("NVAPI hardware probe: one or more interfaces are unavailable");
        return;
    }
    void *physical[64] = {};
    int count = 0;
    const int enumerate_status = enumerate(physical, &count);
    Log("NVAPI hardware probe: EnumPhysicalGPUs=%d count=%d D3D LUID=%08X:%08X", enumerate_status,
        count, static_cast<unsigned>(d3d_luid.HighPart), d3d_luid.LowPart);
    for (int i = 0; enumerate_status == 0 && i < count; ++i) {
        struct ArchInfo { unsigned version, architecture, implementation, revision; } arch = { 0x00020010U };
        void *logical = nullptr;
        LUID nv_luid = {};
        struct LogicalInfo {
            unsigned version;
            unsigned reserved;
            LUID *os_adapter_id;
            unsigned char remainder[0x238 - 16];
        } logical_info = {};
        logical_info.version = 0x00010238U;
        logical_info.os_adapter_id = &nv_luid;
        const int arch_status = get_arch(physical[i], &arch);
        const int logical_status = get_logical(physical[i], &logical);
        const int info_status = logical_status == 0 ? get_logical_info(logical, &logical_info) : logical_status;
        if (info_status == 0 && !g_has_nvapi_luid) {
            g_nvapi_luid = nv_luid;
            g_has_nvapi_luid = true;
        }
        Log("NVAPI GPU %d: arch_status=%d arch=0x%X impl=0x%X rev=0x%X logical_status=%d "
            "info_status=%d LUID=%08X:%08X match=%s", i, arch_status, arch.architecture,
            arch.implementation, arch.revision, logical_status, info_status,
            static_cast<unsigned>(nv_luid.HighPart), nv_luid.LowPart,
            (nv_luid.HighPart == d3d_luid.HighPart && nv_luid.LowPart == d3d_luid.LowPart) ? "yes" : "no");
    }
}

static void Log(const char *format, ...)
{
    char text[4096];
    va_list args;
    va_start(args, format);
    _vsnprintf_s(text, sizeof(text), _TRUNCATE, format, args);
    va_end(args);
    SYSTEMTIME now;
    GetLocalTime(&now);
    std::printf("%02u:%02u:%02u.%03u  %s\n", now.wHour, now.wMinute, now.wSecond, now.wMilliseconds, text);
    FILE *file = nullptr;
    if (fopen_s(&file, g_log_path, "a") == 0 && file != nullptr) {
        std::fprintf(file, "%02u:%02u:%02u.%03u  %s\n", now.wHour, now.wMinute, now.wSecond, now.wMilliseconds, text);
        std::fclose(file);
    }
}

static void LogNgxDebugText(const char *text)
{
    if (text == nullptr || *text == '\0') return;
    std::string clean(text);
    while (!clean.empty() && (clean.back() == '\r' || clean.back() == '\n')) clean.pop_back();
    if (!clean.empty()) Log("NGXDBG %s", clean.c_str());
}

static void WINAPI CaptureOutputDebugStringA(LPCSTR text)
{
    LogNgxDebugText(text);
}

static void WINAPI CaptureOutputDebugStringW(LPCWSTR text)
{
    if (text == nullptr || *text == L'\0') return;
    const int bytes = WideCharToMultiByte(CP_UTF8, 0, text, -1, nullptr, 0, nullptr, nullptr);
    if (bytes <= 1) return;
    std::vector<char> utf8(static_cast<size_t>(bytes));
    WideCharToMultiByte(CP_UTF8, 0, text, -1, utf8.data(), bytes, nullptr, nullptr);
    LogNgxDebugText(utf8.data());
}

static bool HookImportedFunction(HMODULE module, const char *import_module, const char *function_name,
    void *replacement)
{
    if (module == nullptr) return false;
    auto *base = reinterpret_cast<unsigned char *>(module);
    auto *dos = reinterpret_cast<IMAGE_DOS_HEADER *>(base);
    if (dos->e_magic != IMAGE_DOS_SIGNATURE) return false;
    auto *nt = reinterpret_cast<IMAGE_NT_HEADERS64 *>(base + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE) return false;
    const auto &directory = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
    if (directory.VirtualAddress == 0) return false;
    auto *descriptor = reinterpret_cast<IMAGE_IMPORT_DESCRIPTOR *>(base + directory.VirtualAddress);
    for (; descriptor->Name != 0; ++descriptor) {
        const char *dll_name = reinterpret_cast<const char *>(base + descriptor->Name);
        if (_stricmp(dll_name, import_module) != 0) continue;
        auto *names = reinterpret_cast<IMAGE_THUNK_DATA64 *>(base +
            (descriptor->OriginalFirstThunk != 0 ? descriptor->OriginalFirstThunk : descriptor->FirstThunk));
        auto *iat = reinterpret_cast<IMAGE_THUNK_DATA64 *>(base + descriptor->FirstThunk);
        for (; names->u1.AddressOfData != 0; ++names, ++iat) {
            if (IMAGE_SNAP_BY_ORDINAL64(names->u1.Ordinal)) continue;
            auto *import = reinterpret_cast<IMAGE_IMPORT_BY_NAME *>(base + names->u1.AddressOfData);
            if (strcmp(reinterpret_cast<const char *>(import->Name), function_name) != 0) continue;
            DWORD old_protect = 0;
            if (!VirtualProtect(&iat->u1.Function, sizeof(iat->u1.Function), PAGE_READWRITE, &old_protect)) {
                return false;
            }
            iat->u1.Function = reinterpret_cast<ULONGLONG>(replacement);
            DWORD ignored = 0;
            VirtualProtect(&iat->u1.Function, sizeof(iat->u1.Function), old_protect, &ignored);
            FlushInstructionCache(GetCurrentProcess(), &iat->u1.Function, sizeof(iat->u1.Function));
            return true;
        }
    }
    return false;
}

static void InstallNgxDebugCapture(HMODULE module)
{
    const bool ansi = HookImportedFunction(module, "KERNEL32.dll", "OutputDebugStringA",
        reinterpret_cast<void *>(&CaptureOutputDebugStringA));
    const bool wide = HookImportedFunction(module, "KERNEL32.dll", "OutputDebugStringW",
        reinterpret_cast<void *>(&CaptureOutputDebugStringW));
    Log("NGX debug capture: OutputDebugStringA=%s OutputDebugStringW=%s",
        ansi ? "hooked" : "not imported", wide ? "hooked" : "not imported");
}

static const char *ProfileName(ColorProfile profile)
{
    switch (profile) {
    case ColorProfile::Srgb: return "sRGB";
    case ColorProfile::ScRgb: return "scRGB";
    case ColorProfile::Hdr10: return "HDR10/PQ";
    default: return "unknown";
    }
}

static const char *ResultName(NVSDK_NGX_Result result)
{
    switch (static_cast<unsigned>(result)) {
    case 0x00000001: return "Success";
    case 0xBAD00001: return "FeatureNotSupported";
    case 0xBAD00002: return "PlatformError";
    case 0xBAD00003: return "FeatureAlreadyExists";
    case 0xBAD00004: return "FeatureNotFound";
    case 0xBAD00005: return "InvalidParameter";
    case 0xBAD00007: return "NotInitialized";
    case 0xBAD00008: return "UnsupportedInputFormat";
    case 0xBAD00009: return "RWFlagMissing";
    case 0xBAD0000A: return "MissingInput";
    case 0xBAD0000B: return "UnableToInitializeFeature";
    case 0xBAD0000D: return "OutOfGPUMemory";
    case 0xBAD00010: return "UnsupportedParameter";
    default: return "Unknown";
    }
}

static bool Hr(HRESULT result, const char *what)
{
    if (SUCCEEDED(result)) return true;
    Log("FAIL %s: HRESULT=0x%08X", what, static_cast<unsigned>(result));
    return false;
}

template <typename T> static void Release(T *&value)
{
    if (value != nullptr) { value->Release(); value = nullptr; }
}

static bool BeginCommands()
{
    if (!Hr(g.allocator->Reset(), "command allocator Reset")) return false;
    return Hr(g.list->Reset(g.allocator, nullptr), "command list Reset");
}

static bool SubmitAndWait(DWORD timeout_ms = 30000)
{
    if (!Hr(g.list->Close(), "command list Close")) return false;
    ID3D12CommandList *lists[] = { g.list };
    g.queue->ExecuteCommandLists(1, lists);
    const UINT64 value = ++g.fence_value;
    if (!Hr(g.queue->Signal(g.fence, value), "queue Signal")) return false;
    if (g.fence->GetCompletedValue() < value) {
        if (!Hr(g.fence->SetEventOnCompletion(value, g.fence_event), "fence SetEventOnCompletion")) return false;
        if (WaitForSingleObject(g.fence_event, timeout_ms) != WAIT_OBJECT_0) {
            Log("FAIL GPU timeout waiting for fence %llu", static_cast<unsigned long long>(value));
            return false;
        }
    }
    return true;
}

static void Barrier(ID3D12Resource *resource, D3D12_RESOURCE_STATES before, D3D12_RESOURCE_STATES after)
{
    D3D12_RESOURCE_BARRIER barrier = {};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition.pResource = resource;
    barrier.Transition.StateBefore = before;
    barrier.Transition.StateAfter = after;
    barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    g.list->ResourceBarrier(1, &barrier);
}

static bool InitD3D12()
{
    if (!Hr(CreateDXGIFactory2(0, IID_PPV_ARGS(&g.factory)), "CreateDXGIFactory2")) return false;

    // Obtain the LUID NGX itself will use before choosing a DXGI adapter. On
    // systems exposing multiple DXGI identities for one physical NVIDIA GPU,
    // choosing by VRAM alone can select an identity the private NGX gate rejects.
    ProbeNvApiHardware({});

    SIZE_T best_memory = 0;
    for (UINT i = 0;; ++i) {
        IDXGIAdapter1 *candidate = nullptr;
        if (g.factory->EnumAdapterByGpuPreference(i, DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE,
                IID_PPV_ARGS(&candidate)) == DXGI_ERROR_NOT_FOUND) break;
        DXGI_ADAPTER_DESC1 desc = {};
        candidate->GetDesc1(&desc);
        char candidate_name[256] = {};
        WideCharToMultiByte(CP_UTF8, 0, desc.Description, -1, candidate_name, sizeof(candidate_name), nullptr, nullptr);
        const bool luid_match = g_has_nvapi_luid && desc.AdapterLuid.HighPart == g_nvapi_luid.HighPart &&
            desc.AdapterLuid.LowPart == g_nvapi_luid.LowPart;
        Log("DXGI candidate %u: %s vendor=%04X VRAM=%llu MiB LUID=%08X:%08X nvapi_match=%s", i,
            candidate_name, desc.VendorId,
            static_cast<unsigned long long>(desc.DedicatedVideoMemory / (1024 * 1024)),
            static_cast<unsigned>(desc.AdapterLuid.HighPart), desc.AdapterLuid.LowPart, luid_match ? "yes" : "no");
        if ((desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) == 0 && desc.VendorId == 0x10DE &&
            SUCCEEDED(D3D12CreateDevice(candidate, D3D_FEATURE_LEVEL_11_0, _uuidof(ID3D12Device), nullptr)) &&
            (luid_match || (!g_has_nvapi_luid && desc.DedicatedVideoMemory >= best_memory))) {
            Release(g.adapter);
            g.adapter = candidate;
            best_memory = desc.DedicatedVideoMemory;
            if (luid_match) break;
        } else {
            candidate->Release();
        }
    }
    if (g.adapter == nullptr) { Log("FAIL no compatible NVIDIA D3D12 adapter found"); return false; }

    DXGI_ADAPTER_DESC1 desc = {};
    g.adapter->GetDesc1(&desc);
    char gpu[256] = {};
    WideCharToMultiByte(CP_UTF8, 0, desc.Description, -1, gpu, sizeof(gpu), nullptr, nullptr);
    Log("GPU: %s (vendor=%04X device=%04X VRAM=%llu MiB)", gpu, desc.VendorId, desc.DeviceId,
        static_cast<unsigned long long>(desc.DedicatedVideoMemory / (1024 * 1024)));
    ProbeNvApiHardware(desc.AdapterLuid);

    // Conan's working ShortFuse path requests FL 11.0. The resulting device can
    // still expose newer D3D12 interfaces; matching the creation contract avoids
    // making NGX's private hardware gate distinguish the laboratory from the game.
    if (!Hr(D3D12CreateDevice(g.adapter, D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&g.device)), "D3D12CreateDevice")) return false;
    D3D12_COMMAND_QUEUE_DESC queue_desc = {};
    queue_desc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
    if (!Hr(g.device->CreateCommandQueue(&queue_desc, IID_PPV_ARGS(&g.queue)), "CreateCommandQueue")) return false;
    if (!Hr(g.device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&g.allocator)), "CreateCommandAllocator")) return false;
    if (!Hr(g.device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, g.allocator, nullptr,
            IID_PPV_ARGS(&g.list)), "CreateCommandList")) return false;
    if (!Hr(g.list->Close(), "initial command list Close")) return false;
    if (!Hr(g.device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&g.fence)), "CreateFence")) return false;
    g.fence_event = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    if (g.fence_event == nullptr) { Log("FAIL CreateEvent: %lu", GetLastError()); return false; }
    return true;
}

static LRESULT CALLBACK LabWindowProc(HWND window, UINT message, WPARAM wparam, LPARAM lparam)
{
    return DefWindowProcW(window, message, wparam, lparam);
}

static bool InitPresentBoundary()
{
    WNDCLASSEXW cls = {};
    cls.cbSize = sizeof(cls);
    cls.lpfnWndProc = LabWindowProc;
    cls.hInstance = GetModuleHandleW(nullptr);
    cls.lpszClassName = L"DlssNrStandaloneLabWindow";
    RegisterClassExW(&cls);
    g.window = CreateWindowExW(0, cls.lpszClassName, L"DLSS-NR standalone laboratory",
        WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT, 320, 180,
        nullptr, nullptr, cls.hInstance, nullptr);
    if (g.window == nullptr) { Log("FAIL CreateWindowExW: %lu", GetLastError()); return false; }

    DXGI_SWAP_CHAIN_DESC1 desc = {};
    desc.Width = 320;
    desc.Height = 180;
    desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    desc.SampleDesc.Count = 1;
    desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    desc.BufferCount = 2;
    desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    if (!Hr(g.factory->CreateSwapChainForHwnd(g.queue, g.window, &desc, nullptr, nullptr,
            &g.swapchain), "CreateSwapChainForHwnd")) return false;
    g.factory->MakeWindowAssociation(g.window, DXGI_MWA_NO_ALT_ENTER | DXGI_MWA_NO_WINDOW_CHANGES);

    for (int i = 0; i < 8; ++i) {
        const HRESULT result = g.swapchain->Present(0, 0);
        if (FAILED(result)) { Log("FAIL warmup Present %d: 0x%08X", i, static_cast<unsigned>(result)); return false; }
        MSG message;
        while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }
    }
    Log("presentation boundary: D3D12 flip swapchain created and presented 8 frames");
    return true;
}

static bool FileExists(const char *name)
{
    char path[MAX_PATH];
    sprintf_s(path, "%s%s", g_dir, name);
    WIN32_FILE_ATTRIBUTE_DATA data = {};
    if (!GetFileAttributesExA(path, GetFileExInfoStandard, &data)) {
        Log("runtime file: MISSING %s", path);
        return false;
    }
    const unsigned long long bytes = (static_cast<unsigned long long>(data.nFileSizeHigh) << 32) | data.nFileSizeLow;
    Log("runtime file: %s (%llu bytes)", path, bytes);
    return true;
}

static HMODULE LoadInstalledNgxCore()
{
    if (HMODULE loaded = GetModuleHandleW(L"_nvngx.dll")) {
        Log("NGX core: reusing already loaded _nvngx.dll at %p", loaded);
        return loaded;
    }

    wchar_t system[MAX_PATH] = {};
    if (GetSystemDirectoryW(system, MAX_PATH) == 0) return nullptr;
    wchar_t pattern[MAX_PATH] = {};
    swprintf_s(pattern, L"%s\\DriverStore\\FileRepository\\nv*.inf_amd64_*", system);
    WIN32_FIND_DATAW found = {};
    HANDLE search = FindFirstFileW(pattern, &found);
    if (search == INVALID_HANDLE_VALUE) return nullptr;

    std::wstring best;
    FILETIME best_time = {};
    do {
        if ((found.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) == 0) continue;
        wchar_t candidate[MAX_PATH] = {};
        swprintf_s(candidate, L"%s\\DriverStore\\FileRepository\\%s\\_nvngx.dll", system, found.cFileName);
        WIN32_FILE_ATTRIBUTE_DATA attributes = {};
        if (!GetFileAttributesExW(candidate, GetFileExInfoStandard, &attributes)) continue;
        if (best.empty() || CompareFileTime(&attributes.ftLastWriteTime, &best_time) > 0) {
            best = candidate;
            best_time = attributes.ftLastWriteTime;
        }
    } while (FindNextFileW(search, &found));
    FindClose(search);
    if (best.empty()) return nullptr;

    HMODULE module = LoadLibraryExW(best.c_str(), nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
    char utf8[MAX_PATH] = {};
    WideCharToMultiByte(CP_UTF8, 0, best.c_str(), -1, utf8, sizeof(utf8), nullptr, nullptr);
    Log("NGX core: LoadLibraryEx(%s) -> %p (error=%lu)", utf8, module, module ? 0 : GetLastError());
    return module;
}

static void NVSDK_CONV NgxLogCallback(const char *message, NVSDK_NGX_Logging_Level level,
    NVSDK_NGX_Feature source)
{
    if (message == nullptr) return;
    char clean[3072];
    size_t n = 0;
    while (message[n] != '\0' && n + 1 < sizeof(clean)) {
        clean[n] = (message[n] == '\r' || message[n] == '\n') ? ' ' : message[n];
        ++n;
    }
    while (n > 0 && clean[n - 1] == ' ') --n;
    clean[n] = '\0';
    Log("NGX[%d feature=%u] %s", static_cast<int>(level), static_cast<unsigned>(source), clean);
}

// Parameter providers are an undocumented part of the feature-18 contract. A
// transparent provider lets the lab report exactly which name/type each runtime
// asks for and whether the underlying NVIDIA provider contained it.
class TracingParameters final : public NVSDK_NGX_Parameter {
public:
    explicit TracingParameters(NVSDK_NGX_Parameter *inner) : inner_(inner) {}

    void Set(const char *name, unsigned long long value) override { inner_->Set(name, value); Log("PARAM Set ULL %s=%llu", name, value); }
    void Set(const char *name, float value) override { inner_->Set(name, value); Log("PARAM Set F32 %s=%.9g", name, value); }
    void Set(const char *name, double value) override { inner_->Set(name, value); Log("PARAM Set F64 %s=%.17g", name, value); }
    void Set(const char *name, unsigned int value) override { inner_->Set(name, value); Log("PARAM Set U32 %s=%u", name, value); }
    void Set(const char *name, int value) override { inner_->Set(name, value); Log("PARAM Set I32 %s=%d", name, value); }
    void Set(const char *name, ID3D11Resource *value) override { inner_->Set(name, value); Log("PARAM Set D3D11 %s=%p", name, value); }
    void Set(const char *name, ID3D12Resource *value) override { inner_->Set(name, value); Log("PARAM Set D3D12 %s=%p", name, value); }
    void Set(const char *name, void *value) override { inner_->Set(name, value); Log("PARAM Set PTR %s=%p", name, value); }

    NVSDK_NGX_Result Get(const char *name, unsigned long long *value) const override {
        const auto r = inner_->Get(name, value); LogGet("ULL", name, r, NVSDK_NGX_SUCCEED(r) ? *value : 0); return r;
    }
    NVSDK_NGX_Result Get(const char *name, float *value) const override {
        const auto r = inner_->Get(name, value); Log("PARAM Get F32 %s -> 0x%08X value=%.9g", name, static_cast<unsigned>(r), NVSDK_NGX_SUCCEED(r) ? *value : 0.0f); return r;
    }
    NVSDK_NGX_Result Get(const char *name, double *value) const override {
        const auto r = inner_->Get(name, value); Log("PARAM Get F64 %s -> 0x%08X value=%.17g", name, static_cast<unsigned>(r), NVSDK_NGX_SUCCEED(r) ? *value : 0.0); return r;
    }
    NVSDK_NGX_Result Get(const char *name, unsigned int *value) const override {
        const auto r = inner_->Get(name, value); LogGet("U32", name, r, NVSDK_NGX_SUCCEED(r) ? *value : 0); return r;
    }
    NVSDK_NGX_Result Get(const char *name, int *value) const override {
        const auto r = inner_->Get(name, value); Log("PARAM Get I32 %s -> 0x%08X value=%d", name, static_cast<unsigned>(r), NVSDK_NGX_SUCCEED(r) ? *value : 0); return r;
    }
    NVSDK_NGX_Result Get(const char *name, ID3D11Resource **value) const override {
        const auto r = inner_->Get(name, value); Log("PARAM Get D3D11 %s -> 0x%08X value=%p", name, static_cast<unsigned>(r), NVSDK_NGX_SUCCEED(r) ? *value : nullptr); return r;
    }
    NVSDK_NGX_Result Get(const char *name, ID3D12Resource **value) const override {
        const auto r = inner_->Get(name, value); Log("PARAM Get D3D12 %s -> 0x%08X value=%p", name, static_cast<unsigned>(r), NVSDK_NGX_SUCCEED(r) ? *value : nullptr); return r;
    }
    NVSDK_NGX_Result Get(const char *name, void **value) const override {
        const auto r = inner_->Get(name, value); Log("PARAM Get PTR %s -> 0x%08X value=%p", name, static_cast<unsigned>(r), NVSDK_NGX_SUCCEED(r) ? *value : nullptr); return r;
    }
    void Reset() override { Log("PARAM Reset requested by runtime"); inner_->Reset(); }

private:
    static void LogGet(const char *type, const char *name, NVSDK_NGX_Result result, unsigned long long value) {
        Log("PARAM Get %s %s -> 0x%08X value=%llu", type, name, static_cast<unsigned>(result), value);
    }
    NVSDK_NGX_Parameter *inner_;
};

static bool InitNgx()
{
    FileExists("nvngx_dlss.dll");
    if (!FileExists("nvngx_dlssnr.dll")) return false;
    wchar_t runtime_path[MAX_PATH] = {};
    MultiByteToWideChar(CP_UTF8, 0, g_dir, -1, runtime_path, MAX_PATH);

    // Exact path construction recovered from the ShortFuse backend: when no
    // explicit override is configured it obtains GetTempPath2W/GetTempPathW and
    // appends the wide path components "dlssnr" and "RenoDX". This path is only
    // NGX application data; the snippet itself remains beside this executable.
    wchar_t temp_path[MAX_PATH] = {};
    const DWORD temp_length = GetTempPathW(MAX_PATH, temp_path);
    if (temp_length == 0 || temp_length >= MAX_PATH) {
        Log("FAIL GetTempPathW: %lu", GetLastError());
        return false;
    }
    wchar_t data_parent[MAX_PATH] = {};
    wchar_t data_path[MAX_PATH] = {};
    swprintf_s(data_parent, L"%sdlssnr", temp_path);
    swprintf_s(data_path, L"%s\\RenoDX", data_parent);
    if (!CreateDirectoryW(data_parent, nullptr) && GetLastError() != ERROR_ALREADY_EXISTS) {
        Log("FAIL CreateDirectoryW(dlssnr): %lu", GetLastError());
        return false;
    }
    if (!CreateDirectoryW(data_path, nullptr) && GetLastError() != ERROR_ALREADY_EXISTS) {
        Log("FAIL CreateDirectoryW(RenoDX): %lu", GetLastError());
        return false;
    }
    char data_path_utf8[MAX_PATH] = {};
    WideCharToMultiByte(CP_UTF8, 0, data_path, -1, data_path_utf8, sizeof(data_path_utf8), nullptr, nullptr);
    Log("NGX application-data path: %s", data_path_utf8);
    // Feature 18 is a snippet-family feature. The public NGX facade rejects the
    // current 616.xx driver as "unsupported hw", while ShortFuse's accepted path
    // initializes the driver core export directly and then attaches the NR snippet.
    // Reproduce that ownership boundary here rather than relying on the public
    // facade to discover an unpublished feature ID.
    // Match the observed working order: ShortFuse attaches the snippet at the
    // device boundary, then initializes the backend core on first present.
    wchar_t snippet_path[MAX_PATH] = {};
    swprintf_s(snippet_path, L"%snvngx_dlssnr.dll", runtime_path);
    g_nr_module = LoadLibraryExW(snippet_path, nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
    Log("NR snippet LoadLibraryEx = %p (error=%lu)", g_nr_module, g_nr_module ? 0 : GetLastError());
    if (g_nr_module == nullptr) return false;
    InstallNgxDebugCapture(g_nr_module);
    auto snippet_init = reinterpret_cast<NgxSnippetInitD3D12Ext>(GetProcAddress(g_nr_module, "NVSDK_NGX_D3D12_Init_Ext"));
    g_nr_create_feature = reinterpret_cast<NgxCreateFeature>(GetProcAddress(g_nr_module, "NVSDK_NGX_D3D12_CreateFeature"));
    g_nr_evaluate_feature = reinterpret_cast<NgxEvaluateFeature>(GetProcAddress(g_nr_module, "NVSDK_NGX_D3D12_EvaluateFeature"));
    g_nr_release_feature = reinterpret_cast<NgxReleaseFeature>(GetProcAddress(g_nr_module, "NVSDK_NGX_D3D12_ReleaseFeature"));
    g_nr_shutdown = reinterpret_cast<NgxShutdownD3D12>(GetProcAddress(g_nr_module, "NVSDK_NGX_D3D12_Shutdown1"));
    if (snippet_init == nullptr || g_nr_create_feature == nullptr || g_nr_evaluate_feature == nullptr ||
        g_nr_release_feature == nullptr || g_nr_shutdown == nullptr) {
        Log("FAIL NR snippet is missing required D3D12 lifecycle exports");
        return false;
    }
    wchar_t bridge_path[MAX_PATH] = {};
    swprintf_s(bridge_path, L"%snvngx.dll", runtime_path);
    g_bridge_module = LoadLibraryExW(bridge_path, nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
    auto bridge_init = g_bridge_module == nullptr ? nullptr : reinterpret_cast<NgxBridgeInitD3D12Ext>(
        GetProcAddress(g_bridge_module, "NVNGXBridge_D3D12_InitExt"));
    if (g_bridge_module != nullptr) {
        g_bridge_create_feature = reinterpret_cast<NgxBridgeCreateFeature>(GetProcAddress(
            g_bridge_module, "NVNGXBridge_D3D12_CreateFeature"));
        g_bridge_evaluate_feature = reinterpret_cast<NgxBridgeEvaluateFeature>(GetProcAddress(
            g_bridge_module, "NVNGXBridge_D3D12_EvaluateFeature"));
        g_bridge_release_feature = reinterpret_cast<NgxBridgeReleaseFeature>(GetProcAddress(
            g_bridge_module, "NVNGXBridge_D3D12_ReleaseFeature"));
        g_bridge_shutdown = reinterpret_cast<NgxBridgeShutdownD3D12>(GetProcAddress(
            g_bridge_module, "NVNGXBridge_D3D12_Shutdown1"));
        g_bridge_populate_parameters = reinterpret_cast<NgxBridgePopulateParameters>(GetProcAddress(
            g_bridge_module, "NVNGXBridge_D3D12_PopulateParameters"));
    }
    Log("caller-identity bridge module=%p init=%p create=%p evaluate=%p release=%p shutdown=%p",
        g_bridge_module, bridge_init, g_bridge_create_feature, g_bridge_evaluate_feature,
        g_bridge_release_feature, g_bridge_shutdown);
    if (bridge_init == nullptr || g_bridge_create_feature == nullptr ||
        g_bridge_evaluate_feature == nullptr || g_bridge_release_feature == nullptr ||
        g_bridge_shutdown == nullptr || g_bridge_populate_parameters == nullptr) return false;

    g_core_module = LoadInstalledNgxCore();
    if (g_core_module == nullptr) { Log("FAIL could not locate the driver _nvngx.dll"); return false; }
    auto core_init = reinterpret_cast<NgxCoreInitD3D12>(GetProcAddress(g_core_module, "NVSDK_NGX_D3D12_Init"));
    auto core_get_capabilities = reinterpret_cast<NgxGetCapabilityParameters>(GetProcAddress(
        g_core_module, "NVSDK_NGX_D3D12_GetCapabilityParameters"));
    g_core_destroy_parameters = reinterpret_cast<NgxDestroyParameters>(GetProcAddress(g_core_module, "NVSDK_NGX_D3D12_DestroyParameters"));
    g_core_shutdown = reinterpret_cast<NgxShutdownD3D12>(GetProcAddress(g_core_module, "NVSDK_NGX_D3D12_Shutdown1"));
    if (core_init == nullptr || core_get_capabilities == nullptr || g_core_destroy_parameters == nullptr || g_core_shutdown == nullptr) {
        Log("FAIL driver core is missing required D3D12 parameter/lifecycle exports");
        return false;
    }
    NVSDK_NGX_Result result = core_init(kGenericCustomCoreId, data_path, g.device, NVSDK_NGX_Version_API);
    Log("driver-core Init(app=0x%llX) = 0x%08X (%s)", kGenericCustomCoreId,
        static_cast<unsigned>(result), ResultName(result));
    if (NVSDK_NGX_FAILED(result)) return false;

    // Exact ShortFuse ABI recovered at its snippet call site:
    //   (0x876232C, full snippet DLL path, device, 0x15, nullptr)
    // The snippet's own exported application ID (0xE658703) is an identity, not
    // the application ID used to attach this private feature family to the core.
    // Its second argument is also deliberately the full loaded DLL path rather
    // than the core's writable application-data directory.
    result = bridge_init(snippet_init, kGenericCustomCoreId, snippet_path, g.device,
        NVSDK_NGX_Version_API, nullptr);
    char snippet_path_utf8[MAX_PATH] = {};
    WideCharToMultiByte(CP_UTF8, 0, snippet_path, -1, snippet_path_utf8, sizeof(snippet_path_utf8), nullptr, nullptr);
    Log("NR snippet Init_Ext(app=0x%llX, path=%s) = 0x%08X (%s)", kGenericCustomCoreId,
        snippet_path_utf8, static_cast<unsigned>(result), ResultName(result));
    if (NVSDK_NGX_FAILED(result)) return false;

    // DLSS-NR evaluates only its active/network rectangle. A second, standard
    // DLSS Super Resolution feature performs the temporal reconstruction into
    // the native target. Load it through the same nvngx.dll caller-identity
    // bridge so the laboratory proves the complete standalone chain.
    wchar_t dlss_path[MAX_PATH] = {};
    swprintf_s(dlss_path, L"%snvngx_dlss.dll", runtime_path);
    g_dlss_module = LoadLibraryExW(dlss_path, nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
    Log("DLSS SR snippet LoadLibraryEx = %p (error=%lu)", g_dlss_module,
        g_dlss_module ? 0 : GetLastError());
    if (g_dlss_module == nullptr) return false;
    InstallNgxDebugCapture(g_dlss_module);
    auto dlss_init = reinterpret_cast<NgxSnippetInitD3D12Ext>(GetProcAddress(
        g_dlss_module, "NVSDK_NGX_D3D12_Init_Ext"));
    g_dlss_create_feature = reinterpret_cast<NgxCreateFeature>(GetProcAddress(
        g_dlss_module, "NVSDK_NGX_D3D12_CreateFeature"));
    g_dlss_evaluate_feature = reinterpret_cast<NgxEvaluateFeature>(GetProcAddress(
        g_dlss_module, "NVSDK_NGX_D3D12_EvaluateFeature"));
    if (dlss_init == nullptr || g_dlss_create_feature == nullptr || g_dlss_evaluate_feature == nullptr) {
        Log("FAIL DLSS SR snippet is missing required D3D12 exports");
        return false;
    }
    result = bridge_init(dlss_init, kGenericCustomCoreId, dlss_path, g.device,
        NVSDK_NGX_Version_API, nullptr);
    char dlss_path_utf8[MAX_PATH] = {};
    WideCharToMultiByte(CP_UTF8, 0, dlss_path, -1, dlss_path_utf8, sizeof(dlss_path_utf8), nullptr, nullptr);
    Log("DLSS SR snippet Init_Ext(app=0x%llX, path=%s) = 0x%08X (%s)", kGenericCustomCoreId,
        dlss_path_utf8, static_cast<unsigned>(result), ResultName(result));
    if (NVSDK_NGX_FAILED(result)) return false;

    wchar_t dlssg_path[MAX_PATH] = {};
    swprintf_s(dlssg_path, L"%snvngx_dlssg.dll", runtime_path);
    g_dlssg_module = LoadLibraryExW(dlssg_path, nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
    Log("DLSS-G snippet LoadLibraryEx = %p (error=%lu)", g_dlssg_module,
        g_dlssg_module ? 0 : GetLastError());
    if (g_dlssg_module == nullptr) return false;
    InstallNgxDebugCapture(g_dlssg_module);
    auto dlssg_init = reinterpret_cast<NgxSnippetInitD3D12Ext>(GetProcAddress(
        g_dlssg_module, "NVSDK_NGX_D3D12_Init_Ext"));
    g_dlssg_create_feature = reinterpret_cast<NgxCreateFeature>(GetProcAddress(
        g_dlssg_module, "NVSDK_NGX_D3D12_CreateFeature"));
    g_dlssg_evaluate_feature = reinterpret_cast<NgxEvaluateFeature>(GetProcAddress(
        g_dlssg_module, "NVSDK_NGX_D3D12_EvaluateFeature"));
    g_dlssg_release_feature = reinterpret_cast<NgxReleaseFeature>(GetProcAddress(
        g_dlssg_module, "NVSDK_NGX_D3D12_ReleaseFeature"));
    g_dlssg_populate_parameters = reinterpret_cast<NgxPopulateParameters>(GetProcAddress(
        g_dlssg_module, "NVSDK_NGX_D3D12_PopulateParameters_Impl"));
    if (dlssg_init == nullptr || g_dlssg_create_feature == nullptr ||
        g_dlssg_evaluate_feature == nullptr || g_dlssg_release_feature == nullptr ||
        g_dlssg_populate_parameters == nullptr) {
        Log("FAIL DLSS-G snippet is missing required D3D12 exports");
        return false;
    }
    result = bridge_init(dlssg_init, kGenericCustomCoreId, dlssg_path, g.device,
        NVSDK_NGX_Version_API, nullptr);
    Log("DLSS-G snippet Init_Ext = 0x%08X (%s)", static_cast<unsigned>(result), ResultName(result));
    if (NVSDK_NGX_FAILED(result)) return false;

    result = core_get_capabilities(&g.params);
    Log("driver-core GetCapabilityParameters = 0x%08X (%s), ptr=%p", static_cast<unsigned>(result), ResultName(result), g.params);
    if (NVSDK_NGX_FAILED(result) || g.params == nullptr) return false;
    g_nr_populate_parameters = reinterpret_cast<NgxPopulateParameters>(GetProcAddress(
        g_nr_module, "NVSDK_NGX_D3D12_PopulateParameters_Impl"));
    result = g_bridge_populate_parameters(g_nr_populate_parameters, g.params);
    Log("NR PopulateParameters_Impl = 0x%08X (%s)", static_cast<unsigned>(result), ResultName(result));
    for (const char *name : {"DLSSNRComputeScalingRatioCallback", "DLSSNRGetStatsCallback",
        "ResourceAllocCallback", "ResourceReleaseCallback"})
    {
        void *callback = nullptr;
        const NVSDK_NGX_Result get_result = g.params->Get(name, &callback);
        Log("populated callback %-36s get=0x%08X ptr=%p", name,
            static_cast<unsigned>(get_result), callback);
        if (strcmp(name, "DLSSNRComputeScalingRatioCallback") == 0 && NVSDK_NGX_SUCCEED(get_result))
            g_nr_compute_scaling_ratio = reinterpret_cast<NgxPopulateParameters>(callback);
    }
    if (NVSDK_NGX_FAILED(result)) return false;
    g_traced_params = new TracingParameters(g.params);
    g.ngx_initialized = true;
    return true;
}

static ID3D12Resource *MakeTexture(UINT width, UINT height, DXGI_FORMAT format, bool uav)
{
    D3D12_HEAP_PROPERTIES heap = {};
    heap.Type = D3D12_HEAP_TYPE_DEFAULT;
    D3D12_RESOURCE_DESC desc = {};
    desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    desc.Width = width;
    desc.Height = height;
    desc.DepthOrArraySize = 1;
    desc.MipLevels = 1;
    desc.Format = format;
    desc.SampleDesc.Count = 1;
    desc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
    desc.Flags = uav ? D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS : D3D12_RESOURCE_FLAG_NONE;
    ID3D12Resource *resource = nullptr;
    if (!Hr(g.device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &desc,
            D3D12_RESOURCE_STATE_COMMON, nullptr, IID_PPV_ARGS(&resource)), "CreateCommittedResource(texture)")) return nullptr;
    return resource;
}

static ID3D12Resource *MakeBuffer(UINT64 bytes, D3D12_HEAP_TYPE heap_type, D3D12_RESOURCE_STATES initial_state)
{
    D3D12_HEAP_PROPERTIES heap = {};
    heap.Type = heap_type;
    D3D12_RESOURCE_DESC desc = {};
    desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    desc.Width = bytes;
    desc.Height = 1;
    desc.DepthOrArraySize = 1;
    desc.MipLevels = 1;
    desc.Format = DXGI_FORMAT_UNKNOWN;
    desc.SampleDesc.Count = 1;
    desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    ID3D12Resource *resource = nullptr;
    if (!Hr(g.device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &desc,
            initial_state, nullptr, IID_PPV_ARGS(&resource)), "CreateCommittedResource(buffer)")) return nullptr;
    return resource;
}

static uint16_t FloatToHalf(float value)
{
    uint32_t bits;
    std::memcpy(&bits, &value, sizeof(bits));
    const uint32_t sign = (bits >> 16) & 0x8000;
    int exponent = static_cast<int>((bits >> 23) & 0xff) - 127 + 15;
    uint32_t mantissa = bits & 0x7fffff;
    if (exponent <= 0) return static_cast<uint16_t>(sign);
    if (exponent >= 31) return static_cast<uint16_t>(sign | 0x7c00);
    return static_cast<uint16_t>(sign | (static_cast<uint32_t>(exponent) << 10) | (mantissa >> 13));
}

static float HalfToFloat(uint16_t half)
{
    const uint32_t sign = (static_cast<uint32_t>(half & 0x8000)) << 16;
    int exponent = (half >> 10) & 0x1f;
    uint32_t mantissa = half & 0x3ff;
    uint32_t bits;
    if (exponent == 0) bits = sign;
    else if (exponent == 31) bits = sign | 0x7f800000 | (mantissa << 13);
    else bits = sign | (static_cast<uint32_t>(exponent - 15 + 127) << 23) | (mantissa << 13);
    float value;
    std::memcpy(&value, &bits, sizeof(value));
    return value;
}

static UINT BytesPerPixel(DXGI_FORMAT format)
{
    switch (format) {
    case DXGI_FORMAT_R8G8B8A8_UNORM: return 4;
    case DXGI_FORMAT_R10G10B10A2_UNORM: return 4;
    case DXGI_FORMAT_R16G16_FLOAT: return 4;
    case DXGI_FORMAT_R16G16B16A16_FLOAT: return 8;
    case DXGI_FORMAT_R32_FLOAT: return 4;
    default: return 0;
    }
}

static bool UploadTexture(ID3D12Resource *texture, const std::vector<uint8_t> &source,
    D3D12_RESOURCE_STATES final_state)
{
    const D3D12_RESOURCE_DESC desc = texture->GetDesc();
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint = {};
    UINT rows = 0;
    UINT64 row_bytes = 0, total = 0;
    g.device->GetCopyableFootprints(&desc, 0, 1, 0, &footprint, &rows, &row_bytes, &total);
    const UINT bpp = BytesPerPixel(desc.Format);
    if (bpp == 0 || source.size() != static_cast<size_t>(desc.Width) * desc.Height * bpp) {
        Log("FAIL bad upload source: fmt=%u expected=%llu got=%llu", desc.Format,
            static_cast<unsigned long long>(desc.Width * desc.Height * bpp),
            static_cast<unsigned long long>(source.size()));
        return false;
    }
    ID3D12Resource *upload = MakeBuffer(total, D3D12_HEAP_TYPE_UPLOAD, D3D12_RESOURCE_STATE_GENERIC_READ);
    if (upload == nullptr) return false;
    uint8_t *mapped = nullptr;
    D3D12_RANGE empty = { 0, 0 };
    if (!Hr(upload->Map(0, &empty, reinterpret_cast<void **>(&mapped)), "upload Map")) { upload->Release(); return false; }
    const size_t source_pitch = static_cast<size_t>(desc.Width) * bpp;
    for (UINT y = 0; y < desc.Height; ++y)
        std::memcpy(mapped + footprint.Offset + static_cast<size_t>(y) * footprint.Footprint.RowPitch,
            source.data() + static_cast<size_t>(y) * source_pitch, source_pitch);
    upload->Unmap(0, nullptr);

    if (!BeginCommands()) { upload->Release(); return false; }
    Barrier(texture, D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_COPY_DEST);
    D3D12_TEXTURE_COPY_LOCATION dst = {};
    dst.pResource = texture;
    dst.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    dst.SubresourceIndex = 0;
    D3D12_TEXTURE_COPY_LOCATION src = {};
    src.pResource = upload;
    src.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
    src.PlacedFootprint = footprint;
    g.list->CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr);
    Barrier(texture, D3D12_RESOURCE_STATE_COPY_DEST, final_state);
    const bool ok = SubmitAndWait();
    upload->Release();
    return ok;
}

static std::vector<uint8_t> MakeColorPattern(UINT width, UINT height, DXGI_FORMAT format, ColorProfile profile)
{
    const UINT bpp = BytesPerPixel(format);
    std::vector<uint8_t> data(static_cast<size_t>(width) * height * bpp);
    for (UINT y = 0; y < height; ++y) {
        for (UINT x = 0; x < width; ++x) {
            const bool checker = ((x / 12) ^ (y / 12)) & 1;
            const bool line = (x % 61 < 2) || (y % 47 < 2) || ((x + y) % 79 < 2);
            float r = line ? 1.0f : (checker ? 0.82f : 0.06f);
            float gg = line ? 0.18f : (checker ? 0.11f : 0.68f);
            float b = line ? 0.04f : (checker ? 0.55f : 0.09f);
            if (profile != ColorProfile::Srgb) { r *= 2.0f; gg *= 2.0f; b *= 2.0f; }
            uint8_t *pixel = data.data() + (static_cast<size_t>(y) * width + x) * bpp;
            if (format == DXGI_FORMAT_R8G8B8A8_UNORM) {
                pixel[0] = static_cast<uint8_t>(std::min(r, 1.0f) * 255.0f + 0.5f);
                pixel[1] = static_cast<uint8_t>(std::min(gg, 1.0f) * 255.0f + 0.5f);
                pixel[2] = static_cast<uint8_t>(std::min(b, 1.0f) * 255.0f + 0.5f);
                pixel[3] = 255;
            } else if (format == DXGI_FORMAT_R10G10B10A2_UNORM) {
                const uint32_t pr = static_cast<uint32_t>(std::min(r, 1.0f) * 1023.0f + 0.5f);
                const uint32_t pg = static_cast<uint32_t>(std::min(gg, 1.0f) * 1023.0f + 0.5f);
                const uint32_t pb = static_cast<uint32_t>(std::min(b, 1.0f) * 1023.0f + 0.5f);
                *reinterpret_cast<uint32_t *>(pixel) = pr | (pg << 10) | (pb << 20) | (3u << 30);
            } else {
                uint16_t *half = reinterpret_cast<uint16_t *>(pixel);
                half[0] = FloatToHalf(r); half[1] = FloatToHalf(gg); half[2] = FloatToHalf(b); half[3] = FloatToHalf(1.0f);
            }
        }
    }
    return data;
}

static std::vector<uint8_t> EmbedTopLeft(const std::vector<uint8_t> &source, UINT source_width,
    UINT source_height, UINT target_width, UINT target_height, UINT bytes_per_pixel)
{
    std::vector<uint8_t> target(static_cast<size_t>(target_width) * target_height * bytes_per_pixel, 0);
    const size_t source_pitch = static_cast<size_t>(source_width) * bytes_per_pixel;
    const size_t target_pitch = static_cast<size_t>(target_width) * bytes_per_pixel;
    for (UINT y = 0; y < source_height; ++y) {
        std::memcpy(target.data() + static_cast<size_t>(y) * target_pitch,
            source.data() + static_cast<size_t>(y) * source_pitch, source_pitch);
    }
    return target;
}

static std::vector<uint8_t> MakeDepth(UINT width, UINT height)
{
    std::vector<uint8_t> data(static_cast<size_t>(width) * height * sizeof(float));
    float *pixels = reinterpret_cast<float *>(data.data());
    for (UINT y = 0; y < height; ++y)
        for (UINT x = 0; x < width; ++x)
            pixels[static_cast<size_t>(y) * width + x] = 1.0f - (0.15f + 0.8f * static_cast<float>(x + y) / static_cast<float>(width + height));
    return data;
}

static std::vector<uint8_t> MakeMotionVectors(UINT width, UINT height)
{
    std::vector<uint8_t> data(static_cast<size_t>(width) * height * 4, 0);
    return data;
}

static std::vector<uint8_t> MakeSentinel(UINT width, UINT height, DXGI_FORMAT format)
{
    const UINT bpp = BytesPerPixel(format);
    std::vector<uint8_t> data(static_cast<size_t>(width) * height * bpp);
    for (size_t i = 0; i < static_cast<size_t>(width) * height; ++i) {
        uint8_t *pixel = data.data() + i * bpp;
        if (format == DXGI_FORMAT_R8G8B8A8_UNORM) {
            pixel[0] = 13; pixel[1] = 37; pixel[2] = 73; pixel[3] = 251;
        } else {
            uint16_t *half = reinterpret_cast<uint16_t *>(pixel);
            half[0] = FloatToHalf(-2.0f); half[1] = FloatToHalf(-3.0f); half[2] = FloatToHalf(-4.0f); half[3] = FloatToHalf(-5.0f);
        }
    }
    return data;
}

static NVSDK_NGX_Result SafeCreateFeature(DWORD *seh_code)
{
    *seh_code = 0;
    __try {
        return g_bridge_create_feature(g_nr_create_feature, g.list, kFeatureDlssNr,
            g_traced_params != nullptr ? g_traced_params : g.params, &g.feature);
    }
    __except (EXCEPTION_EXECUTE_HANDLER) { *seh_code = GetExceptionCode(); return static_cast<NVSDK_NGX_Result>(0x7fffffff); }
}

static NVSDK_NGX_Result SafeEvaluateFeature(DWORD *seh_code)
{
    *seh_code = 0;
    __try {
        return g_bridge_evaluate_feature(g_nr_evaluate_feature, g.list, g.feature,
            g_traced_params != nullptr ? g_traced_params : g.params, nullptr);
    }
    __except (EXCEPTION_EXECUTE_HANDLER) { *seh_code = GetExceptionCode(); return static_cast<NVSDK_NGX_Result>(0x7fffffff); }
}

static NVSDK_NGX_Result SafeCreateSrFeature(DWORD *seh_code)
{
    *seh_code = 0;
    __try {
        return g_bridge_create_feature(g_dlss_create_feature, g.list,
            NVSDK_NGX_Feature_SuperSampling, g_traced_params != nullptr ? g_traced_params : g.params,
            &g.sr_feature);
    }
    __except (EXCEPTION_EXECUTE_HANDLER) { *seh_code = GetExceptionCode(); return static_cast<NVSDK_NGX_Result>(0x7fffffff); }
}

static NVSDK_NGX_Result SafeEvaluateSrFeature(DWORD *seh_code)
{
    *seh_code = 0;
    __try {
        return g_bridge_evaluate_feature(g_dlss_evaluate_feature, g.list, g.sr_feature,
            g_traced_params != nullptr ? g_traced_params : g.params, nullptr);
    }
    __except (EXCEPTION_EXECUTE_HANDLER) { *seh_code = GetExceptionCode(); return static_cast<NVSDK_NGX_Result>(0x7fffffff); }
}

static NVSDK_NGX_Result SafeCreateFgFeature(DWORD *seh_code)
{
    *seh_code = 0;
    __try {
        return g_bridge_create_feature(g_dlssg_create_feature, g.list,
            NVSDK_NGX_Feature_FrameGeneration,
            g_traced_params != nullptr ? g_traced_params : g.params, &g.fg_feature);
    }
    __except (EXCEPTION_EXECUTE_HANDLER) { *seh_code = GetExceptionCode(); return static_cast<NVSDK_NGX_Result>(0x7fffffff); }
}

static NVSDK_NGX_Result SafeEvaluateFgFeature(DWORD *seh_code)
{
    *seh_code = 0;
    __try {
        return g_bridge_evaluate_feature(g_dlssg_evaluate_feature, g.list, g.fg_feature,
            g_traced_params != nullptr ? g_traced_params : g.params, nullptr);
    }
    __except (EXCEPTION_EXECUTE_HANDLER) { *seh_code = GetExceptionCode(); return static_cast<NVSDK_NGX_Result>(0x7fffffff); }
}

static void SetCreationContract(const Options &o, int flags)
{
    const UINT nr_width = NrWidth(o), nr_height = NrHeight(o);
    g.params->Reset();
    // PopulateParameters installs feature-18's provider callbacks into the
    // parameter object. Reset removes them, so restore them for every create
    // contract exactly as an NGX host integration is expected to do.
    if (g_nr_populate_parameters != nullptr) {
        const NVSDK_NGX_Result populate_result =
            g_bridge_populate_parameters(g_nr_populate_parameters, g.params);
        Log("creation PopulateParameters_Impl = 0x%08X (%s)",
            static_cast<unsigned>(populate_result), ResultName(populate_result));
    }
    g.params->Set("CreationNodeMask", 1u);
    g.params->Set("VisibilityNodeMask", 1u);
    g.params->Set("Width", o.input_w);
    g.params->Set("Height", o.input_h);
    g.params->Set("OutWidth", nr_width);
    g.params->Set("OutHeight", nr_height);
    g.params->Set("ResourceWidth", o.input_w);
    g.params->Set("ResourceHeight", o.input_h);
    g.params->Set("ResourceOutWidth", nr_width);
    g.params->Set("ResourceOutHeight", nr_height);
    const float requested_ratio = static_cast<float>(o.input_w) / static_cast<float>(nr_width);
    const int quality = requested_ratio >= 0.72f ? NVSDK_NGX_PerfQuality_Value_UltraQuality
        : requested_ratio >= 0.62f ? NVSDK_NGX_PerfQuality_Value_MaxQuality
        : requested_ratio >= 0.54f ? NVSDK_NGX_PerfQuality_Value_Balanced
        : requested_ratio >= 0.42f ? NVSDK_NGX_PerfQuality_Value_MaxPerf
        : NVSDK_NGX_PerfQuality_Value_UltraPerformance;
    g.params->Set("PerfQualityValue", quality);
    g.params->Set("DLSS.Feature.Create.Flags", flags);
    g.params->Set("DLSS.Enable.Output.Subrects", 0);
    g.params->Set("DLSS.Denoise.Mode", 1);
    g.params->Set("DLSS.Roughness.Mode", 0u);
    g.params->Set("DLSS.Use.HW.Depth", 1u);
    g.params->Set("DLSSNR.Enabled", 1u);
    g.params->Set("DLSSNR.InputWidth", o.input_w);
    g.params->Set("DLSSNR.InputHeight", o.input_h);
    g.params->Set("DLSSNR.Width", nr_width);
    g.params->Set("DLSSNR.Height", nr_height);
    g.params->Set("DLSSNR.OutputWidth", nr_width);
    g.params->Set("DLSSNR.OutputHeight", nr_height);
    g.params->Set("Output.Width", nr_width);
    g.params->Set("Output.Height", nr_height);
    g.params->Set("DLSSNR.Upscaling", 1u);
    g.params->Set("DLSSNR.ScalingRatio", requested_ratio);
    g.params->Set("DLSSNR.Scale", requested_ratio);
    g.params->Set("DLSSNR.Hint.Render.Preset", o.model);
    g.params->Set("DLSSNR.Intensity", o.intensity);
    g.params->Set("DLSSNR.LocalToneStrength", o.local_tone);
    g.params->Set("DLSSNR.LocalStructureStrength", o.local_structure);
    g.params->Set("DLSSNR.SkinStructureStrength", o.skin_structure);
    g.params->Set("DLSSNR.UseAutoMask", 1u);
    g.params->Set("DLSSNR.UICorrection", 0u);
    if (g_nr_compute_scaling_ratio != nullptr) {
        const NVSDK_NGX_Result ratio_result = g_nr_compute_scaling_ratio(g.params);
        float resolved_ratio = requested_ratio;
        const NVSDK_NGX_Result get_result = g.params->Get("DLSSNR.ScalingRatio", &resolved_ratio);
        g.params->Set("DLSSNR.Scale", resolved_ratio);
        g_nr_resolved_scaling_ratio = resolved_ratio;
        Log("DLSSNRComputeScalingRatioCallback quality=%d result=0x%08X (%s), get=0x%08X requested=%.6f resolved=%.6f",
            quality, static_cast<unsigned>(ratio_result), ResultName(ratio_result),
            static_cast<unsigned>(get_result), requested_ratio, resolved_ratio);
    } else {
        Log("WARN DLSSNRComputeScalingRatioCallback is unavailable; using requested ratio %.6f",
            requested_ratio);
    }
}

static bool CreateNrFeature(const Options &o, int flags)
{
    SetCreationContract(o, flags);
    if (!BeginCommands()) return false;
    DWORD exception_code = 0;
    const NVSDK_NGX_Result result = SafeCreateFeature(&exception_code);
    if (exception_code != 0) {
        g.list->Close();
        Log("FAIL feature 18 creation raised SEH 0x%08X; command list was not submitted", exception_code);
        return false;
    }
    if (!SubmitAndWait()) return false;
    Log("CreateFeature(feature=18) = 0x%08X (%s), handle=%p", static_cast<unsigned>(result), ResultName(result), g.feature);
    if (NVSDK_NGX_FAILED(result) || g.feature == nullptr) return false;
    Log("PASS raw DLSS-NR feature 18 created with %ux%u -> %ux%u contract, model=%d profile=%s",
        o.input_w, o.input_h, o.output_w, o.output_h, o.model, ProfileName(o.profile));
    return true;
}

static bool CreateSrFeature(const Options &o, int flags)
{
    g.params->Reset();
    g.params->Set("CreationNodeMask", 1u);
    g.params->Set("VisibilityNodeMask", 1u);
    g.params->Set("Width", o.input_w);
    g.params->Set("Height", o.input_h);
    g.params->Set("OutWidth", o.output_w);
    g.params->Set("OutHeight", o.output_h);
    const float ratio = static_cast<float>(o.input_w) / static_cast<float>(o.output_w);
    const bool dlaa = o.input_w == o.output_w && o.input_h == o.output_h;
    const int quality = dlaa ? NVSDK_NGX_PerfQuality_Value_DLAA
        : ratio >= 0.62f ? NVSDK_NGX_PerfQuality_Value_MaxQuality
        : ratio >= 0.54f ? NVSDK_NGX_PerfQuality_Value_Balanced
        : ratio >= 0.42f ? NVSDK_NGX_PerfQuality_Value_MaxPerf
        : NVSDK_NGX_PerfQuality_Value_UltraPerformance;
    g.params->Set("PerfQualityValue", quality);
    g.params->Set("DLSS.Feature.Create.Flags", flags);
    g.params->Set("DLSS.Enable.Output.Subrects", 0);

    if (!BeginCommands()) return false;
    DWORD exception_code = 0;
    const NVSDK_NGX_Result result = SafeCreateSrFeature(&exception_code);
    if (exception_code != 0) {
        g.list->Close();
        Log("FAIL DLSS SR creation raised SEH 0x%08X; command list was not submitted", exception_code);
        return false;
    }
    if (!SubmitAndWait()) return false;
    Log("CreateFeature(feature=SuperSampling) = 0x%08X (%s), handle=%p mode=%s quality=%d",
        static_cast<unsigned>(result), ResultName(result), g.sr_feature,
        dlaa ? "DLAA" : "DLSS SR", quality);
    return NVSDK_NGX_SUCCEED(result) && g.sr_feature != nullptr;
}

static bool CreateFgFeature(const Options &o, DXGI_FORMAT format)
{
    g.params->Reset();
    const NVSDK_NGX_Result populate_result =
        g_bridge_populate_parameters(g_dlssg_populate_parameters, g.params);
    Log("DLSS-G PopulateParameters_Impl = 0x%08X (%s)",
        static_cast<unsigned>(populate_result), ResultName(populate_result));
    g.params->Set("CreationNodeMask", 1u);
    g.params->Set("VisibilityNodeMask", 1u);
    g.params->Set("Width", o.output_w);
    g.params->Set("Height", o.output_h);
    g.params->Set("DLSSG.BackbufferFormat", static_cast<unsigned>(format));
    g.params->Set("DLSSG.InternalWidth", o.input_w);
    g.params->Set("DLSSG.InternalHeight", o.input_h);
    g.params->Set("DLSSG.DynamicResolution", 0u);

    if (!BeginCommands()) return false;
    DWORD exception_code = 0;
    const NVSDK_NGX_Result result = SafeCreateFgFeature(&exception_code);
    if (exception_code != 0) {
        g.list->Close();
        Log("FAIL DLSS-G creation raised SEH 0x%08X", exception_code);
        return false;
    }
    if (!SubmitAndWait()) return false;
    Log("CreateFeature(feature=FrameGeneration) = 0x%08X (%s), handle=%p",
        static_cast<unsigned>(result), ResultName(result), g.fg_feature);
    return NVSDK_NGX_SUCCEED(result) && g.fg_feature != nullptr;
}

static void SetEvaluationContract(const Options &o, ID3D12Resource *color, ID3D12Resource *output,
    ID3D12Resource *depth, ID3D12Resource *motion, bool reset)
{
    // Keep both the public DLSS/RR names and the feature-18 names. ShortFuse's
    // working evaluator does the same; this lets the lab identify which family
    // a future runtime version consumes without changing resource ownership.
    g.params->Set("Color", color);
    g.params->Set("Output", output);
    g.params->Set("Depth", depth);
    g.params->Set("MotionVectors", motion);
    g.params->Set("DLSSNR.Color", color);
    g.params->Set("DLSSNR.Output", output);
    g.params->Set("DLSSNR.Depth", depth);
    g.params->Set("DLSSNR.MVec", motion);
    g.params->Set("Reset", reset ? 1 : 0);
    g.params->Set("DLSSNR.Reset", reset ? 1 : 0);
    g.params->Set("Jitter.Offset.X", 0.0f);
    g.params->Set("Jitter.Offset.Y", 0.0f);
    g.params->Set("MV.Scale.X", 1.0f);
    g.params->Set("MV.Scale.Y", 1.0f);
    g.params->Set("DLSSNR.JitterOffsetX", 0.0f);
    g.params->Set("DLSSNR.JitterOffsetY", 0.0f);
    g.params->Set("DLSSNR.MVecScaleX", 1.0f);
    g.params->Set("DLSSNR.MVecScaleY", 1.0f);
    g.params->Set("DLSS.Pre.Exposure", 1.0f);
    g.params->Set("DLSS.Exposure.Scale", 1.0f);
    g.params->Set("DLSS.Render.Subrect.Dimensions.Width", o.input_w);
    g.params->Set("DLSS.Render.Subrect.Dimensions.Height", o.input_h);
    g.params->Set("DLSS.Input.Color.Subrect.Base.X", 0u);
    g.params->Set("DLSS.Input.Color.Subrect.Base.Y", 0u);
    g.params->Set("DLSS.Input.Depth.Subrect.Base.X", 0u);
    g.params->Set("DLSS.Input.Depth.Subrect.Base.Y", 0u);
    g.params->Set("DLSS.Input.MV.Subrect.Base.X", 0u);
    g.params->Set("DLSS.Input.MV.Subrect.Base.Y", 0u);
    g.params->Set("DLSS.Output.Subrect.Base.X", 0u);
    g.params->Set("DLSS.Output.Subrect.Base.Y", 0u);
    // Feature 18 does not consume the public DLSS subrect names above. Its
    // parameter-provider trace shows these private I32 keys are used to build
    // and validate the D3D12 resource descriptors passed to the backend.
    g.params->Set("DLSSNR.ColorSubrectBaseX", 0);
    g.params->Set("DLSSNR.ColorSubrectBaseY", 0);
    g.params->Set("DLSSNR.ColorSubrectWidth", static_cast<int>(o.input_w));
    g.params->Set("DLSSNR.ColorSubrectHeight", static_cast<int>(o.input_h));
    g.params->Set("DLSSNR.MVecSubrectBaseX", 0);
    g.params->Set("DLSSNR.MVecSubrectBaseY", 0);
    g.params->Set("DLSSNR.MVecSubrectWidth", static_cast<int>(o.input_w));
    g.params->Set("DLSSNR.MVecSubrectHeight", static_cast<int>(o.input_h));
    g.params->Set("DLSSNR.DepthSubrectBaseX", 0);
    g.params->Set("DLSSNR.DepthSubrectBaseY", 0);
    g.params->Set("DLSSNR.DepthSubrectWidth", static_cast<int>(o.input_w));
    g.params->Set("DLSSNR.DepthSubrectHeight", static_cast<int>(o.input_h));
    g.params->Set("DLSSNR.OutputSubrectBaseX", 0);
    g.params->Set("DLSSNR.OutputSubrectBaseY", 0);
    g.params->Set("DLSSNR.OutputSubrectWidth", static_cast<int>(NrWidth(o)));
    g.params->Set("DLSSNR.OutputSubrectHeight", static_cast<int>(NrHeight(o)));
    g.params->Set("DLSSNR.DepthInverted", 1u);
    g.params->Set("DLSSNR.InputWidth", o.input_w);
    g.params->Set("DLSSNR.InputHeight", o.input_h);
    g.params->Set("DLSSNR.Width", NrWidth(o));
    g.params->Set("DLSSNR.Height", NrHeight(o));
    g.params->Set("DLSSNR.OutputWidth", NrWidth(o));
    g.params->Set("DLSSNR.OutputHeight", NrHeight(o));
    g.params->Set("DLSSNR.Upscaling", 1u);
    // Keep the provider-resolved value during evaluation. Overwriting it with
    // the requested geometric ratio would defeat the callback we are testing.
    g.params->Set("DLSSNR.ScalingRatio", g_nr_resolved_scaling_ratio);
    g.params->Set("DLSSNR.Scale", g_nr_resolved_scaling_ratio);
    g.params->Set("DLSSNR.Hint.Render.Preset", o.model);
    g.params->Set("DLSSNR.Intensity", o.intensity);
    g.params->Set("DLSSNR.LocalToneStrength", o.local_tone);
    g.params->Set("DLSSNR.LocalStructureStrength", o.local_structure);
    g.params->Set("DLSSNR.SkinStructureStrength", o.skin_structure);
    // The shared provider is reset while creating the downstream SR feature,
    // so evaluation must republish every NR runtime switch rather than relying
    // on values left behind by feature creation.
    g.params->Set("DLSSNR.Enabled", 1u);
    g.params->Set("DLSSNR.UseAutoMask", 1u);
    g.params->Set("DLSSNR.UICorrection", 0u);
}

static bool EvaluateFrame(const Options &o, ID3D12Resource *color, ID3D12Resource *output,
    ID3D12Resource *depth, ID3D12Resource *motion, bool reset, UINT frame)
{
    SetEvaluationContract(o, color, output, depth, motion, reset);
    if (!BeginCommands()) return false;
    DWORD exception_code = 0;
    const NVSDK_NGX_Result result = SafeEvaluateFeature(&exception_code);
    if (exception_code != 0) {
        g.list->Close();
        Log("FAIL frame %u EvaluateFeature raised SEH 0x%08X; command list was not submitted", frame, exception_code);
        return false;
    }
    if (!SubmitAndWait()) return false;
    Log("frame %u EvaluateFeature = 0x%08X (%s)", frame, static_cast<unsigned>(result), ResultName(result));
    return NVSDK_NGX_SUCCEED(result);
}

static bool EvaluateSrFrame(const Options &o, ID3D12Resource *color, ID3D12Resource *output,
    ID3D12Resource *depth, ID3D12Resource *motion, bool reset, UINT frame)
{
    // Public DLSS SR consumes the low-resolution NR result from the active
    // top-left rectangle and performs the actual temporal reconstruction.
    g.params->Set("Color", color);
    g.params->Set("Output", output);
    g.params->Set("Depth", depth);
    g.params->Set("MotionVectors", motion);
    g.params->Set("Reset", reset ? 1 : 0);
    g.params->Set("Jitter.Offset.X", 0.0f);
    g.params->Set("Jitter.Offset.Y", 0.0f);
    g.params->Set("Sharpness", 0.0f);
    g.params->Set("MV.Scale.X", 1.0f);
    g.params->Set("MV.Scale.Y", 1.0f);
    g.params->Set("DLSS.Render.Subrect.Dimensions.Width", o.input_w);
    g.params->Set("DLSS.Render.Subrect.Dimensions.Height", o.input_h);
    g.params->Set("DLSS.Input.Color.Subrect.Base.X", 0u);
    g.params->Set("DLSS.Input.Color.Subrect.Base.Y", 0u);
    g.params->Set("DLSS.Input.Depth.Subrect.Base.X", 0u);
    g.params->Set("DLSS.Input.Depth.Subrect.Base.Y", 0u);
    g.params->Set("DLSS.Input.MV.Subrect.Base.X", 0u);
    g.params->Set("DLSS.Input.MV.Subrect.Base.Y", 0u);
    g.params->Set("DLSS.Output.Subrect.Base.X", 0u);
    g.params->Set("DLSS.Output.Subrect.Base.Y", 0u);
    g.params->Set("DLSS.Pre.Exposure", 1.0f);
    g.params->Set("DLSS.Exposure.Scale", 1.0f);
    g.params->Set("DLSS.Indicator.Invert.X.Axis", 0);
    g.params->Set("DLSS.Indicator.Invert.Y.Axis", 0);

    if (!BeginCommands()) return false;
    Barrier(color, D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
        D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    DWORD exception_code = 0;
    const NVSDK_NGX_Result result = SafeEvaluateSrFeature(&exception_code);
    if (exception_code != 0) {
        g.list->Close();
        Log("FAIL frame %u DLSS SR EvaluateFeature raised SEH 0x%08X; command list was not submitted",
            frame, exception_code);
        return false;
    }
    Barrier(color, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
        D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    if (!SubmitAndWait()) return false;
    Log("frame %u DLSS SR EvaluateFeature = 0x%08X (%s)", frame,
        static_cast<unsigned>(result), ResultName(result));
    return NVSDK_NGX_SUCCEED(result);
}

static bool EvaluateFgFrame(const Options &o, ID3D12Resource *real_frame,
    ID3D12Resource *interpolated_frame, ID3D12Resource *depth,
    ID3D12Resource *motion, bool reset, UINT frame)
{
    static float identity[4][4] = {
        {1, 0, 0, 0}, {0, 1, 0, 0}, {0, 0, 1, 0}, {0, 0, 0, 1}
    };
    g.params->Set("DLSSG.Backbuffer", real_frame);
    g.params->Set("DLSSG.MVecs", motion);
    g.params->Set("DLSSG.Depth", depth);
    g.params->Set("DLSSG.HUDLess", real_frame);
    g.params->Set("DLSSG.OutputInterpolated", interpolated_frame);
    g.params->Set("DLSSG.MultiFrameCount", 1u);
    g.params->Set("DLSSG.MultiFrameIndex", 1u);
    g.params->Set("DLSSG.CameraViewToClip", reinterpret_cast<void *>(identity));
    g.params->Set("DLSSG.ClipToCameraView", reinterpret_cast<void *>(identity));
    g.params->Set("DLSSG.ClipToLensClip", reinterpret_cast<void *>(identity));
    g.params->Set("DLSSG.ClipToPrevClip", reinterpret_cast<void *>(identity));
    g.params->Set("DLSSG.PrevClipToClip", reinterpret_cast<void *>(identity));
    g.params->Set("DLSSG.JitterOffsetX", 0.0f);
    g.params->Set("DLSSG.JitterOffsetY", 0.0f);
    g.params->Set("DLSSG.MvecScaleX", 1.0f / static_cast<float>(o.input_w));
    g.params->Set("DLSSG.MvecScaleY", 1.0f / static_cast<float>(o.input_h));
    g.params->Set("DLSSG.CameraPinholeOffsetX", 0.0f);
    g.params->Set("DLSSG.CameraPinholeOffsetY", 0.0f);
    g.params->Set("DLSSG.CameraPosX", 0.0f); g.params->Set("DLSSG.CameraPosY", 0.0f); g.params->Set("DLSSG.CameraPosZ", 0.0f);
    g.params->Set("DLSSG.CameraUpX", 0.0f); g.params->Set("DLSSG.CameraUpY", 1.0f); g.params->Set("DLSSG.CameraUpZ", 0.0f);
    g.params->Set("DLSSG.CameraRightX", 1.0f); g.params->Set("DLSSG.CameraRightY", 0.0f); g.params->Set("DLSSG.CameraRightZ", 0.0f);
    g.params->Set("DLSSG.CameraFwdX", 0.0f); g.params->Set("DLSSG.CameraFwdY", 0.0f); g.params->Set("DLSSG.CameraFwdZ", 1.0f);
    g.params->Set("DLSSG.CameraNear", 0.1f);
    g.params->Set("DLSSG.CameraFar", 1000.0f);
    g.params->Set("DLSSG.CameraFOV", 1.04719755f);
    g.params->Set("DLSSG.CameraAspectRatio", static_cast<float>(o.output_w) / o.output_h);
    g.params->Set("DLSSG.ColorBuffersHDR", o.profile == ColorProfile::Srgb ? 0u : 1u);
    g.params->Set("DLSSG.DepthInverted", 1u);
    g.params->Set("DLSSG.CameraMotionIncluded", 1u);
    g.params->Set("DLSSG.Reset", reset ? 1u : 0u);
    g.params->Set("DLSSG.AutomodeOverrideReset", 0u);
    g.params->Set("DLSSG.NotRenderingGameFrames", 0u);
    g.params->Set("DLSSG.OrthoProjection", 0u);
    g.params->Set("DLSSG.MvecInvalidValue", -99999.0f);
    g.params->Set("DLSSG.MvecDilated", 0u);
    g.params->Set("DLSSG.MenuDetectionEnabled", 0u);
    g.params->Set("DLSSG.BackbufferFrameID", frame);
    for (const char *prefix : {"DLSSG.MVecsSubrect", "DLSSG.DepthSubrect"}) {
        std::string key = std::string(prefix) + "BaseX"; g.params->Set(key.c_str(), 0u);
        key = std::string(prefix) + "BaseY"; g.params->Set(key.c_str(), 0u);
        key = std::string(prefix) + "Width"; g.params->Set(key.c_str(), o.input_w);
        key = std::string(prefix) + "Height"; g.params->Set(key.c_str(), o.input_h);
    }
    for (const char *prefix : {"DLSSG.InputBackbufferSubrect", "DLSSG.HUDLessSubrect",
        "DLSSG.OutputInterpolatedSubrect"}) {
        std::string key = std::string(prefix) + "BaseX"; g.params->Set(key.c_str(), 0u);
        key = std::string(prefix) + "BaseY"; g.params->Set(key.c_str(), 0u);
        key = std::string(prefix) + "Width"; g.params->Set(key.c_str(), o.output_w);
        key = std::string(prefix) + "Height"; g.params->Set(key.c_str(), o.output_h);
    }

    if (!BeginCommands()) return false;
    Barrier(real_frame, D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
        D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    DWORD exception_code = 0;
    const NVSDK_NGX_Result result = SafeEvaluateFgFeature(&exception_code);
    if (exception_code != 0) {
        g.list->Close();
        Log("FAIL frame %u DLSS-G EvaluateFeature raised SEH 0x%08X", frame, exception_code);
        return false;
    }
    Barrier(real_frame, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
        D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    if (!SubmitAndWait()) return false;
    Log("frame %u DLSS-G EvaluateFeature = 0x%08X (%s)", frame,
        static_cast<unsigned>(result), ResultName(result));
    return NVSDK_NGX_SUCCEED(result);
}

static bool ReadbackTexture(ID3D12Resource *texture, D3D12_RESOURCE_STATES before,
    std::vector<uint8_t> *tight, UINT *out_row_pitch)
{
    const D3D12_RESOURCE_DESC desc = texture->GetDesc();
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint = {};
    UINT rows = 0;
    UINT64 row_bytes = 0, total = 0;
    g.device->GetCopyableFootprints(&desc, 0, 1, 0, &footprint, &rows, &row_bytes, &total);
    ID3D12Resource *readback = MakeBuffer(total, D3D12_HEAP_TYPE_READBACK, D3D12_RESOURCE_STATE_COPY_DEST);
    if (readback == nullptr) return false;
    if (!BeginCommands()) { readback->Release(); return false; }
    Barrier(texture, before, D3D12_RESOURCE_STATE_COPY_SOURCE);
    D3D12_TEXTURE_COPY_LOCATION src = {};
    src.pResource = texture;
    src.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    D3D12_TEXTURE_COPY_LOCATION dst = {};
    dst.pResource = readback;
    dst.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
    dst.PlacedFootprint = footprint;
    g.list->CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr);
    if (!SubmitAndWait()) { readback->Release(); return false; }
    const UINT bpp = BytesPerPixel(desc.Format);
    const size_t pitch = static_cast<size_t>(desc.Width) * bpp;
    tight->resize(pitch * desc.Height);
    uint8_t *mapped = nullptr;
    D3D12_RANGE range = { 0, static_cast<SIZE_T>(total) };
    if (!Hr(readback->Map(0, &range, reinterpret_cast<void **>(&mapped)), "readback Map")) { readback->Release(); return false; }
    for (UINT y = 0; y < desc.Height; ++y)
        std::memcpy(tight->data() + static_cast<size_t>(y) * pitch,
            mapped + footprint.Offset + static_cast<size_t>(y) * footprint.Footprint.RowPitch, pitch);
    D3D12_RANGE written = { 0, 0 };
    readback->Unmap(0, &written);
    readback->Release();
    if (out_row_pitch != nullptr) *out_row_pitch = static_cast<UINT>(pitch);
    return true;
}

static bool SavePpm(const Options &o, DXGI_FORMAT format, const std::vector<uint8_t> &pixels)
{
    char path[MAX_PATH];
    sprintf_s(path, "%snr-lab-output-model%d-%s.ppm", g_dir, o.model,
        o.profile == ColorProfile::Srgb ? "srgb" : o.profile == ColorProfile::ScRgb ? "scrgb" : "hdr10");
    FILE *file = nullptr;
    if (fopen_s(&file, path, "wb") != 0 || file == nullptr) return false;
    std::fprintf(file, "P6\n%u %u\n255\n", o.output_w, o.output_h);
    for (size_t i = 0; i < static_cast<size_t>(o.output_w) * o.output_h; ++i) {
        uint8_t rgb[3];
        if (format == DXGI_FORMAT_R8G8B8A8_UNORM) {
            rgb[0] = pixels[i * 4 + 0]; rgb[1] = pixels[i * 4 + 1]; rgb[2] = pixels[i * 4 + 2];
        } else {
            const uint16_t *half = reinterpret_cast<const uint16_t *>(pixels.data() + i * 8);
            for (int c = 0; c < 3; ++c) {
                const float value = std::max(0.0f, std::min(HalfToFloat(half[c]) / 2.0f, 1.0f));
                rgb[c] = static_cast<uint8_t>(value * 255.0f + 0.5f);
            }
        }
        std::fwrite(rgb, 1, 3, file);
    }
    std::fclose(file);
    Log("wrote visual output: %s", path);
    return true;
}

struct Coverage {
    double total = 0.0;
    double quadrant[4] = {};
    uint64_t checksum = 1469598103934665603ULL;
};

static Coverage AnalyzeCoverage(const Options &o, DXGI_FORMAT format,
    const std::vector<uint8_t> &pixels, const std::vector<uint8_t> &sentinel)
{
    Coverage result;
    const UINT bpp = BytesPerPixel(format);
    uint64_t changed = 0, quadrant_changed[4] = {}, quadrant_total[4] = {};
    const uint64_t total = static_cast<uint64_t>(o.output_w) * o.output_h;
    for (UINT y = 0; y < o.output_h; ++y) {
        for (UINT x = 0; x < o.output_w; ++x) {
            const size_t offset = (static_cast<size_t>(y) * o.output_w + x) * bpp;
            const int quadrant = (x >= o.output_w / 2 ? 1 : 0) + (y >= o.output_h / 2 ? 2 : 0);
            ++quadrant_total[quadrant];
            bool different = false;
            for (UINT c = 0; c < bpp; ++c) {
                const uint8_t value = pixels[offset + c];
                result.checksum = (result.checksum ^ value) * 1099511628211ULL;
                if (value != sentinel[offset + c]) different = true;
            }
            if (different) { ++changed; ++quadrant_changed[quadrant]; }
        }
    }
    result.total = 100.0 * static_cast<double>(changed) / static_cast<double>(total);
    for (int i = 0; i < 4; ++i)
        result.quadrant[i] = 100.0 * static_cast<double>(quadrant_changed[i]) / static_cast<double>(quadrant_total[i]);
    return result;
}

static void WriteResultJson(const Options &o, bool created, UINT evaluations, const Coverage &coverage, bool pass)
{
    char path[MAX_PATH];
    sprintf_s(path, "%snr-lab-result.json", g_dir);
    FILE *file = nullptr;
    if (fopen_s(&file, path, "wb") != 0 || file == nullptr) return;
    std::fprintf(file,
        "{\n"
        "  \"featureId\": 18,\n"
        "  \"created\": %s,\n"
        "  \"evaluationsSucceeded\": %u,\n"
        "  \"input\": { \"width\": %u, \"height\": %u },\n"
        "  \"output\": { \"width\": %u, \"height\": %u },\n"
        "  \"model\": %d,\n"
        "  \"profile\": \"%s\",\n"
        "  \"pipeline\": \"%s\",\n"
        "  \"compactNrResources\": %s,\n"
        "  \"nrResolvedScalingRatio\": %.6f,\n"
        "  \"changedPercent\": %.5f,\n"
        "  \"quadrantChangedPercent\": [%.5f, %.5f, %.5f, %.5f],\n"
        "  \"checksumFnv1a64\": \"%016llX\",\n"
        "  \"upscalingValidated\": %s\n"
        "}\n",
        created ? "true" : "false", evaluations, o.input_w, o.input_h, o.output_w, o.output_h,
        o.model, ProfileName(o.profile), o.nr_only ? "nr-only" :
            (o.framegen ? "nr-plus-dlss-sr-plus-framegen" : "nr-plus-dlss-sr"),
        o.compact_nr ? "true" : "false", g_nr_resolved_scaling_ratio, coverage.total,
        coverage.quadrant[0], coverage.quadrant[1], coverage.quadrant[2], coverage.quadrant[3],
        static_cast<unsigned long long>(coverage.checksum), pass ? "true" : "false");
    std::fclose(file);
    Log("wrote machine-readable result: %s", path);
}

static int Run(const Options &o)
{
    const UINT nr_width = NrWidth(o), nr_height = NrHeight(o);
    const DXGI_FORMAT input_format = o.profile == ColorProfile::Hdr10
        ? DXGI_FORMAT_R10G10B10A2_UNORM
        : o.profile == ColorProfile::Srgb ? DXGI_FORMAT_R8G8B8A8_UNORM
        : DXGI_FORMAT_R16G16B16A16_FLOAT;
    const DXGI_FORMAT color_format = o.profile == ColorProfile::Srgb
        ? DXGI_FORMAT_R8G8B8A8_UNORM : DXGI_FORMAT_R16G16B16A16_FLOAT;
    const int flags = NVSDK_NGX_DLSS_Feature_Flags_MVLowRes |
        NVSDK_NGX_DLSS_Feature_Flags_AutoExposure |
        NVSDK_NGX_DLSS_Feature_Flags_DepthInverted |
        (o.profile == ColorProfile::Srgb ? 0 : NVSDK_NGX_DLSS_Feature_Flags_IsHDR);

    Log("contract: feature=18 input=%ux%u output=%ux%u scale=%.6f model=%d profile=%s flags=0x%X",
        o.input_w, o.input_h, o.output_w, o.output_h,
        static_cast<double>(o.input_w) / o.output_w, o.model, ProfileName(o.profile), flags);
    Log("tuning: intensity=%.3f localTone=%.3f localStructure=%.3f skinStructure=%.3f",
        o.intensity, o.local_tone, o.local_structure, o.skin_structure);

    // Private feature 18 requires Color and Output to have matching physical
    // allocation dimensions. The lower render resolution is expressed only by
    // DLSSNR.ColorSubrect*, with the low-resolution pixels packed at (0, 0).
    // A physically low-resolution Color resource is rejected as an invalid
    // Color/Output rect configuration before the neural network is dispatched.
    ID3D12Resource *color = MakeTexture(nr_width, nr_height, input_format, false);
    ID3D12Resource *depth = MakeTexture(o.input_w, o.input_h, DXGI_FORMAT_R32_FLOAT, false);
    ID3D12Resource *motion = MakeTexture(o.input_w, o.input_h, DXGI_FORMAT_R16G16_FLOAT, false);
    ID3D12Resource *nr_output = MakeTexture(nr_width, nr_height, color_format, true);
    ID3D12Resource *output = MakeTexture(o.output_w, o.output_h, color_format, true);
    ID3D12Resource *fg_output = MakeTexture(o.output_w, o.output_h, color_format, true);
    if (color == nullptr || depth == nullptr || motion == nullptr || nr_output == nullptr ||
        output == nullptr || fg_output == nullptr) {
        Log("FAIL resource allocation");
        return 2;
    }

    const std::vector<uint8_t> color_pattern = MakeColorPattern(o.input_w, o.input_h, input_format, o.profile);
    const std::vector<uint8_t> color_data = o.compact_nr ? color_pattern :
        EmbedTopLeft(color_pattern, o.input_w, o.input_h,
            o.output_w, o.output_h, BytesPerPixel(input_format));
    const std::vector<uint8_t> depth_data = MakeDepth(o.input_w, o.input_h);
    const std::vector<uint8_t> motion_data = MakeMotionVectors(o.input_w, o.input_h);
    const std::vector<uint8_t> sentinel = MakeSentinel(o.output_w, o.output_h, color_format);
    const std::vector<uint8_t> nr_sentinel = MakeSentinel(nr_width, nr_height, color_format);
    if (!UploadTexture(color, color_data, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE) ||
        !UploadTexture(depth, depth_data, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE) ||
        !UploadTexture(motion, motion_data, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE) ||
        !UploadTexture(nr_output, nr_sentinel, D3D12_RESOURCE_STATE_UNORDERED_ACCESS) ||
        !UploadTexture(output, sentinel, D3D12_RESOURCE_STATE_UNORDERED_ACCESS) ||
        !UploadTexture(fg_output, sentinel, D3D12_RESOURCE_STATE_UNORDERED_ACCESS)) {
        Log("FAIL deterministic texture upload");
        return 2;
    }

    const bool created = CreateNrFeature(o, flags);
    if (!created) {
        Coverage empty;
        WriteResultJson(o, false, 0, empty, false);
        Log("VERDICT FAIL: the runtime did not create raw feature 18; inspect this log and NVIDIA NGX logs");
        return 3;
    }
    if (!o.nr_only && !CreateSrFeature(o, flags)) {
        Coverage empty;
        WriteResultJson(o, true, 0, empty, false);
        Log("VERDICT FAIL: DLSS-NR created, but the DLSS Super Resolution stage did not");
        return 3;
    }
    if (o.framegen && !CreateFgFeature(o, color_format)) {
        Coverage empty;
        WriteResultJson(o, true, 0, empty, false);
        Log("VERDICT FAIL: NR and SR created, but DLSS Frame Generation did not");
        return 3;
    }

    UINT good = 0;
    for (UINT frame = 0; frame < o.frames; ++frame) {
        if (!EvaluateFrame(o, color, nr_output, depth, motion, frame == 0, frame)) break;
        if (!o.nr_only && !EvaluateSrFrame(o, nr_output, output, depth, motion, frame == 0, frame)) break;
        if (o.framegen && !EvaluateFgFrame(o, output, fg_output, depth, motion,
            frame == 0, frame)) break;
        ++good;
    }

    std::vector<uint8_t> result;
    ID3D12Resource *measured_output = o.nr_only ? nr_output : (o.framegen ? fg_output : output);
    if (good == 0 || !ReadbackTexture(measured_output, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, &result, nullptr)) {
        Coverage empty;
        WriteResultJson(o, true, good, empty, false);
        Log("VERDICT FAIL: feature 18 was created but no readable evaluation completed");
        return 4;
    }
    const Coverage coverage = AnalyzeCoverage(o, color_format, result, sentinel);
    SavePpm(o, color_format, result);
    const bool full_coverage = coverage.total > 95.0 &&
        coverage.quadrant[0] > 90.0 && coverage.quadrant[1] > 90.0 &&
        coverage.quadrant[2] > 90.0 && coverage.quadrant[3] > 90.0;
    const bool pass = good == o.frames && full_coverage;
    Log("coverage: total=%.3f%% TL=%.3f%% TR=%.3f%% BL=%.3f%% BR=%.3f%% checksum=%016llX",
        coverage.total, coverage.quadrant[0], coverage.quadrant[1], coverage.quadrant[2], coverage.quadrant[3],
        static_cast<unsigned long long>(coverage.checksum));
    WriteResultJson(o, true, good, coverage, pass);
    if (pass) {
        const bool dlaa = !o.nr_only && o.input_w == o.output_w && o.input_h == o.output_h;
        Log("VERDICT PASS: %s wrote the complete native-resolution target%s",
            o.nr_only ? "DLSS-NR alone" : (dlaa ? "DLSS-NR plus DLAA" : "DLSS-NR plus DLSS SR"),
            dlaa ? " at a 1:1 render scale" : " from lower-resolution inputs");
        return 0;
    }
    Log("VERDICT FAIL: creation/evaluation ran, but native-target coverage was not proven (this catches upper-left-only output)");
    return 5;
}

static void Shutdown()
{
    // This is a short-lived process, and several private NR builds can block
    // indefinitely in ReleaseFeature/Shutdown1 after a rejected evaluation.
    // Let process teardown reclaim the private feature/provider/modules so every
    // failed probe deterministically returns its logs to the matrix runner.
    g.feature = nullptr;
    g.params = nullptr;
    g_traced_params = nullptr;
    g.ngx_initialized = false;
    // Keep both private modules loaded until process teardown. Unloading a
    // partially initialized snippet from DllMain is not a useful lab signal and
    // can obscure the actual initialization result with a teardown hang.
    g_nr_module = nullptr;
    g_bridge_module = nullptr;
    // Do not FreeLibrary the core if another loader already owned its reference;
    // process teardown reclaims it and this avoids an ownership ambiguity.
    if (g.fence_event != nullptr) CloseHandle(g.fence_event);
    Release(g.swapchain);
    if (g.window != nullptr) { DestroyWindow(g.window); g.window = nullptr; }
    Release(g.fence); Release(g.list); Release(g.allocator); Release(g.queue);
    Release(g.device); Release(g.adapter); Release(g.factory);
}

static bool ParseSize(const char *text, UINT *width, UINT *height)
{
    unsigned w = 0, h = 0;
    if (sscanf_s(text, "%ux%u", &w, &h) != 2 || w == 0 || h == 0) return false;
    *width = w; *height = h;
    return true;
}

static bool ParseFloat(const char *text, float *value)
{
    char *end = nullptr;
    const float parsed = strtof(text, &end);
    if (end == text || *end != '\0') return false;
    *value = parsed;
    return true;
}

int main(int argc, char **argv)
{
    GetModuleFileNameA(nullptr, g_dir, MAX_PATH);
    if (char *slash = strrchr(g_dir, '\\')) *(slash + 1) = '\0';
    sprintf_s(g_log_path, "%snr-lab.log", g_dir);
    { FILE *file = nullptr; if (fopen_s(&file, g_log_path, "wb") == 0 && file != nullptr) std::fclose(file); }

    Options options;
    for (int i = 1; i < argc; ++i) {
        const char *arg = argv[i];
        if (strcmp(arg, "--input") == 0 && i + 1 < argc) {
            if (!ParseSize(argv[++i], &options.input_w, &options.input_h)) { Log("invalid --input"); return 64; }
        } else if (strcmp(arg, "--output") == 0 && i + 1 < argc) {
            if (!ParseSize(argv[++i], &options.output_w, &options.output_h)) { Log("invalid --output"); return 64; }
        } else if (strcmp(arg, "--frames") == 0 && i + 1 < argc) {
            options.frames = std::max(1u, static_cast<UINT>(strtoul(argv[++i], nullptr, 10)));
        } else if (strcmp(arg, "--model") == 0 && i + 1 < argc) {
            options.model = atoi(argv[++i]);
        } else if (strcmp(arg, "--profile") == 0 && i + 1 < argc) {
            const char *profile = argv[++i];
            if (_stricmp(profile, "srgb") == 0) options.profile = ColorProfile::Srgb;
            else if (_stricmp(profile, "scrgb") == 0) options.profile = ColorProfile::ScRgb;
            else if (_stricmp(profile, "hdr10") == 0 || _stricmp(profile, "pq") == 0) options.profile = ColorProfile::Hdr10;
            else { Log("invalid --profile (use srgb, scrgb, or hdr10)"); return 64; }
        } else if (strcmp(arg, "--intensity") == 0 && i + 1 < argc) {
            if (!ParseFloat(argv[++i], &options.intensity)) return 64;
        } else if (strcmp(arg, "--local-tone") == 0 && i + 1 < argc) {
            if (!ParseFloat(argv[++i], &options.local_tone)) return 64;
        } else if (strcmp(arg, "--local-structure") == 0 && i + 1 < argc) {
            if (!ParseFloat(argv[++i], &options.local_structure)) return 64;
        } else if (strcmp(arg, "--skin-structure") == 0 && i + 1 < argc) {
            if (!ParseFloat(argv[++i], &options.skin_structure)) return 64;
        } else if (strcmp(arg, "--nr-only") == 0) {
            options.nr_only = true;
        } else if (strcmp(arg, "--compact-nr") == 0) {
            options.compact_nr = true;
        } else if (strcmp(arg, "--framegen") == 0) {
            options.framegen = true;
        } else if (strcmp(arg, "--help") == 0 || strcmp(arg, "-h") == 0) {
            std::printf("nr-lab [--input 960x540] [--output 1920x1080] [--frames N] [--model 1|2|3]\n"
                        "       [--profile srgb|scrgb|hdr10] [--intensity F] [--local-tone F]\n"
                        "       [--local-structure F] [--skin-structure F] [--nr-only] [--compact-nr] [--framegen]\n");
            return 0;
        } else {
            Log("unknown argument: %s", arg);
            return 64;
        }
    }
    if (options.model < 1 || options.model > 3 || options.output_w < options.input_w || options.output_h < options.input_h) {
        Log("invalid contract: model must be 1..3 and output must be at least input size");
        return 64;
    }
    if (options.nr_only && options.compact_nr &&
        (options.output_w != options.input_w || options.output_h != options.input_h)) {
        Log("invalid contract: --compact-nr with an enlarged --output requires the downstream DLSS SR stage");
        return 64;
    }
    if (options.nr_only && options.framegen) {
        Log("invalid contract: --framegen requires the DLSS SR stage");
        return 64;
    }
    if (options.framegen) options.frames = std::max(options.frames, 2u);

    Log("DLSS-NR standalone laboratory build %s %s", __DATE__, __TIME__);
    int code = 1;
    if (InitNvApi() && InitD3D12() && InitPresentBoundary() && InitNgx()) code = Run(options);
    else Log("VERDICT FAIL: environment initialization failed");
    Shutdown();
    return code;
}
