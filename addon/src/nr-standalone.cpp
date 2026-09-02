#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <d3d9.h>
#include <d3d11_4.h>
#include <d3d12.h>
#include <dxgi1_6.h>
#include <d3dcompiler.h>
#include <wrl/client.h>

#include <algorithm>
#include <atomic>
#include <cstdarg>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#define ImTextureID ImU64
#include <imgui.h>
#include <reshade.hpp>
#include <nvsdk_ngx.h>
#include <nvsdk_ngx_params.h>
#include <nvsdk_ngx_defs_dlssd.h>
#include <nvsdk_ngx_defs_dlssg.h>

#include "../../external/DLSS5-Feeder/src/feed_vk.h"
#include "../../external/DLSS5-Feeder/src/feed_vk_hook.h"

#define ADDON_VERSION "1.7.8-ngx-core-discovery"

extern "C" __declspec(dllexport) const char *NAME = "Standalone DLSS-NR + SR " ADDON_VERSION;
extern "C" __declspec(dllexport) const char *DESCRIPTION =
    "Standalone D3D9/D3D11/D3D12/Vulkan DLSS Neural Rendering, Super Resolution, and Frame Generation.";

static HMODULE g_self;
static wchar_t g_addon_directory[MAX_PATH];
static char g_log_path[MAX_PATH];
static CRITICAL_SECTION g_log_lock;
static std::atomic<unsigned int> g_output_width{0};
static std::atomic<unsigned int> g_output_height{0};
static std::atomic<unsigned int> g_input_width{0};
static std::atomic<unsigned int> g_input_height{0};
static bool g_enabled = true;
static std::atomic<unsigned long long> g_frames_presented{0};
static ID3D12Resource *g_nr_output;
static ID3D12CommandQueue *g_command_queue;
static reshade::api::command_queue *g_rs_queue;
static HWND g_game_window;
static HWND g_proxy_window;
static HANDLE g_proxy_window_thread;
static HANDLE g_proxy_window_ready;
static HHOOK g_proxy_mouse_hook;
static Microsoft::WRL::ComPtr<IDXGISwapChain3> g_proxy_swapchain;
static Microsoft::WRL::ComPtr<ID3D12CommandAllocator> g_proxy_allocators[2];
static Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList> g_proxy_lists[2];
static Microsoft::WRL::ComPtr<ID3D12Fence> g_proxy_fence;
static Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> g_proxy_rtv_heap;
static Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> g_proxy_srv_heap;
static Microsoft::WRL::ComPtr<ID3D12RootSignature> g_proxy_root_signature;
static Microsoft::WRL::ComPtr<ID3D12PipelineState> g_proxy_pipeline;
static UINT g_proxy_rtv_stride;
static UINT g_proxy_srv_stride;
static DXGI_FORMAT g_proxy_present_format;
static HANDLE g_proxy_fence_event;
static UINT64 g_proxy_fence_value;
static bool g_proxy_allow_tearing;
static bool g_proxy_failed;
static std::atomic<bool> g_proxy_hidden{false};
static bool g_show_neural_output = true;
static bool g_pending_proxy_frame;
static bool g_composite_reshade_output = true;
static std::atomic<unsigned long long> g_post_reshade_frames{0};
static std::atomic<bool> g_reshade_overlay_open{false};
static std::atomic<reshade::api::effect_runtime *> g_proxy_runtime{nullptr};
static std::atomic<bool> g_proxy_overlay_open{false};
static bool g_proxy_overlay_syncing;
static std::atomic<bool> g_proxy_cursor_clip_active{false};
static std::atomic<unsigned long long> g_overlay_mouse_events{0};
static unsigned long long g_last_logged_mouse_event;
static bool g_show_proxy_fps = true;
static std::atomic<unsigned int> g_proxy_fps{0};
static std::atomic<unsigned int> g_source_fps{0};
static ULONGLONG g_source_fps_sample_start;
static unsigned int g_source_fps_sample_frames;
static ULONGLONG g_output_fps_sample_start;
static unsigned int g_output_fps_sample_frames;
static bool g_f10_down;
static bool g_home_down;
static bool g_alt_x_down;
static std::atomic<unsigned long long> g_last_primary_present_tick{0};
static std::atomic<bool> g_proxy_watchdog_hidden{false};
static std::atomic<unsigned long long> g_ignored_secondary_surfaces{0};

enum class ColorProfile : int
{
    Auto = 0,
    Srgb = 1,
    ScRgb = 2,
    Hdr10Pq = 3,
    Hdr10Hlg = 4
};
// Keep the user's requested convention separate from the convention actually
// fed to NGX and presented by the proxy. Auto is resolved from the primary
// game swapchain, with a format fallback for older ReShade/DXGI paths.
static ColorProfile g_color_profile = ColorProfile::Auto;
static ColorProfile g_active_color_profile = ColorProfile::Srgb;
static reshade::api::color_space g_detected_color_space = reshade::api::color_space::unknown;
static DXGI_FORMAT g_detected_swapchain_format = DXGI_FORMAT_UNKNOWN;
static int g_nr_model = 1;
static int g_active_nr_model;
static float g_nr_intensity = 1.0f;
static float g_nr_local_tone = 1.0f;
static float g_nr_local_structure = 1.0f;
static float g_nr_skin_structure = -1.0f;
static bool g_reset_every_frame = false;
static bool g_stable_sr_history = false;
static bool g_bypass_nr_for_ab = false;
static bool g_framegen_enabled = true;
static bool g_framegen_failed = false;
static std::atomic<bool> g_feature_recreate_requested{false};
static bool g_need_history_reset = true;
static bool g_depth_reversed = true;
static bool g_neural_failed = false;
static bool g_neural_ready = false;
static bool g_mask_available = false;
static bool g_using_external_guides = false;
static char g_neural_status[256] = "waiting for first game present";
static std::atomic<unsigned long long> g_nr_frames{0};
static std::atomic<unsigned long long> g_sr_frames{0};
static std::atomic<unsigned long long> g_fg_frames{0};

static reshade::api::effect_runtime *g_runtime;
static reshade::api::effect_technique g_feed_technique;
static reshade::api::effect_technique g_motion_technique;
static reshade::api::effect_texture_variable g_mv_variable;
static reshade::api::effect_texture_variable g_depth_variable;
static reshade::api::effect_texture_variable g_mask_variable;
struct BackbufferView
{
    ID3D12Resource *resource;
    reshade::api::resource_view rtv;
};
static std::vector<BackbufferView> g_backbuffer_views;
static std::atomic<unsigned long long> g_current_guide_frames{0};

static Microsoft::WRL::ComPtr<ID3D12Device> g_neural_device;
static Microsoft::WRL::ComPtr<ID3D12CommandAllocator> g_neural_allocator;
static Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList> g_neural_list;
static Microsoft::WRL::ComPtr<ID3D12Fence> g_neural_fence;
static HANDLE g_neural_fence_event;
static UINT64 g_neural_fence_value;
static Microsoft::WRL::ComPtr<ID3D12Resource> g_packed_color;
static Microsoft::WRL::ComPtr<ID3D12Resource> g_nr_stage;
static Microsoft::WRL::ComPtr<ID3D12Resource> g_sr_stage;
static Microsoft::WRL::ComPtr<ID3D12Resource> g_fg_stage;
static Microsoft::WRL::ComPtr<ID3D12Resource> g_fallback_motion;
static Microsoft::WRL::ComPtr<ID3D12Resource> g_fallback_depth;
static Microsoft::WRL::ComPtr<ID3D12Resource> g_captured_motion;
static Microsoft::WRL::ComPtr<ID3D12Resource> g_captured_depth;
static Microsoft::WRL::ComPtr<ID3D12Resource> g_captured_mask;
static reshade::api::device_api g_present_api = static_cast<reshade::api::device_api>(0);
static Microsoft::WRL::ComPtr<ID3D11Device> g_legacy_device11;
static Microsoft::WRL::ComPtr<ID3D11DeviceContext> g_legacy_context11;
static Microsoft::WRL::ComPtr<ID3D11DeviceContext4> g_legacy_context4;
static Microsoft::WRL::ComPtr<ID3D11Fence> g_legacy_fence11;
static Microsoft::WRL::ComPtr<ID3D12Fence> g_legacy_fence12;
static Microsoft::WRL::ComPtr<ID3D11Texture2D> g_legacy_input11;
static Microsoft::WRL::ComPtr<ID3D11Texture2D> g_legacy_post11;
static Microsoft::WRL::ComPtr<ID3D12Resource> g_legacy_post12;
static Microsoft::WRL::ComPtr<IDirect3DDevice9> g_legacy_device9;
static Microsoft::WRL::ComPtr<IDirect3DTexture9> g_legacy_input9;
static Microsoft::WRL::ComPtr<IDirect3DTexture9> g_legacy_post9;
static Microsoft::WRL::ComPtr<ID3D11Texture2D> g_legacy_input9_11;
static Microsoft::WRL::ComPtr<ID3D11Texture2D> g_legacy_post9_11;
static Microsoft::WRL::ComPtr<IDirect3DQuery9> g_legacy_query9;
static UINT64 g_legacy_fence_value;
static UINT64 g_legacy_d3d12_done_value;
static UINT g_legacy_width;
static UINT g_legacy_height;
static DXGI_FORMAT g_legacy_format = DXGI_FORMAT_UNKNOWN;
static FeedVk g_vulkan;
static reshade::api::device *g_vulkan_reshade_device;
static reshade::api::fence g_vulkan_fence;
static VkSemaphore g_vulkan_semaphore = VK_NULL_HANDLE;
static VkImage g_vulkan_input = VK_NULL_HANDLE;
static VkImage g_vulkan_post = VK_NULL_HANDLE;
static VkDeviceMemory g_vulkan_input_memory = VK_NULL_HANDLE;
static VkDeviceMemory g_vulkan_post_memory = VK_NULL_HANDLE;
static bool g_vulkan_input_layout_initialized;
static bool g_vulkan_post_layout_initialized;
static bool g_vulkan_release_wait_queued;
static bool g_vulkan_input_copy_recorded;
static bool g_vulkan_post_copy_recorded;
static bool g_vulkan_waiting_for_effects;
static Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> g_guide_rtv_heap;
static UINT g_guide_rtv_stride;
static UINT g_resource_input_width;
static UINT g_resource_input_height;
static UINT g_resource_output_width;
static UINT g_resource_output_height;
static DXGI_FORMAT g_resource_input_format = DXGI_FORMAT_UNKNOWN;

static constexpr NVSDK_NGX_Feature kFeatureDlssNr = static_cast<NVSDK_NGX_Feature>(0x12);
static constexpr unsigned long long kGenericCustomCoreId = 0x0876232CULL;
using NgxCoreInitD3D12 = NVSDK_NGX_Result (NVSDK_CONV *)(unsigned long long, const wchar_t *, ID3D12Device *, NVSDK_NGX_Version);
using NgxSnippetInitD3D12Ext = NVSDK_NGX_Result (NVSDK_CONV *)(unsigned long long, const wchar_t *, ID3D12Device *, NVSDK_NGX_Version, const NVSDK_NGX_Parameter *);
using NgxAllocateParameters = NVSDK_NGX_Result (NVSDK_CONV *)(NVSDK_NGX_Parameter **);
using NgxGetCapabilityParameters = NVSDK_NGX_Result (NVSDK_CONV *)(NVSDK_NGX_Parameter **);
using NgxPopulateParameters = NVSDK_NGX_Result (NVSDK_CONV *)(NVSDK_NGX_Parameter *);
using NgxCreateFeature = NVSDK_NGX_Result (NVSDK_CONV *)(ID3D12GraphicsCommandList *, NVSDK_NGX_Feature, NVSDK_NGX_Parameter *, NVSDK_NGX_Handle **);
using NgxEvaluateFeature = NVSDK_NGX_Result (NVSDK_CONV *)(ID3D12GraphicsCommandList *, const NVSDK_NGX_Handle *, const NVSDK_NGX_Parameter *, PFN_NVSDK_NGX_ProgressCallback_C);
using NgxReleaseFeature = NVSDK_NGX_Result (NVSDK_CONV *)(NVSDK_NGX_Handle *);
using NgxBridgeInitD3D12Ext = NVSDK_NGX_Result (NVSDK_CONV *)(NgxSnippetInitD3D12Ext, unsigned long long, const wchar_t *, ID3D12Device *, NVSDK_NGX_Version, const NVSDK_NGX_Parameter *);
using NgxBridgeCreateFeature = NVSDK_NGX_Result (NVSDK_CONV *)(NgxCreateFeature, ID3D12GraphicsCommandList *, NVSDK_NGX_Feature, NVSDK_NGX_Parameter *, NVSDK_NGX_Handle **);
using NgxBridgeEvaluateFeature = NVSDK_NGX_Result (NVSDK_CONV *)(NgxEvaluateFeature, ID3D12GraphicsCommandList *, const NVSDK_NGX_Handle *, const NVSDK_NGX_Parameter *, PFN_NVSDK_NGX_ProgressCallback_C);
using NgxBridgeReleaseFeature = NVSDK_NGX_Result (NVSDK_CONV *)(NgxReleaseFeature, NVSDK_NGX_Handle *);
using NgxBridgePopulateParameters = NVSDK_NGX_Result (NVSDK_CONV *)(NgxPopulateParameters, NVSDK_NGX_Parameter *);

static HMODULE g_core_module;
static HMODULE g_nr_module;
static HMODULE g_dlss_module;
static HMODULE g_dlssg_module;
static HMODULE g_bridge_module;
static NVSDK_NGX_Parameter *g_ngx_params;
static NVSDK_NGX_Handle *g_nr_feature;
static NVSDK_NGX_Handle *g_sr_feature;
static NVSDK_NGX_Handle *g_fg_feature;
static NgxCreateFeature g_nr_create;
static NgxEvaluateFeature g_nr_evaluate;
static NgxReleaseFeature g_nr_release;
static NgxCreateFeature g_sr_create;
static NgxEvaluateFeature g_sr_evaluate;
static NgxReleaseFeature g_sr_release;
static NgxCreateFeature g_fg_create;
static NgxEvaluateFeature g_fg_evaluate;
static NgxReleaseFeature g_fg_release;
static NgxPopulateParameters g_fg_populate;
static NgxBridgeCreateFeature g_bridge_create;
static NgxBridgeEvaluateFeature g_bridge_evaluate;
static NgxBridgeReleaseFeature g_bridge_release;
static NgxBridgePopulateParameters g_bridge_populate;

static void ReleaseLegacyFrameResources();
static bool EnsureStandaloneResources(ID3D12Resource *backbuffer);
static NgxPopulateParameters g_nr_populate;
static NgxPopulateParameters g_nr_compute_scaling_ratio;
static float g_nr_scaling_ratio = 1.0f;

static void Log(const char *format, ...)
{
    char message[3072];
    va_list args;
    va_start(args, format);
    vsnprintf_s(message, sizeof(message), _TRUNCATE, format, args);
    va_end(args);

    EnterCriticalSection(&g_log_lock);
    FILE *file = nullptr;
    if (fopen_s(&file, g_log_path, "a") == 0 && file != nullptr)
    {
        SYSTEMTIME now;
        GetLocalTime(&now);
        fprintf(file, "%02u:%02u:%02u.%03u %s\n", now.wHour, now.wMinute, now.wSecond, now.wMilliseconds, message);
        fclose(file);
    }
    LeaveCriticalSection(&g_log_lock);
    reshade::log::message(reshade::log::level::info, message);
}

static void SetStatus(const char *format, ...)
{
    va_list args;
    va_start(args, format);
    vsnprintf_s(g_neural_status, sizeof(g_neural_status), _TRUNCATE, format, args);
    va_end(args);
}

static const char *ProfileName(ColorProfile profile)
{
    switch (profile)
    {
    case ColorProfile::Auto: return "Auto";
    case ColorProfile::Srgb: return "sRGB";
    case ColorProfile::ScRgb: return "Linear BT.709 / scRGB";
    case ColorProfile::Hdr10Pq: return "BT.2100 PQ / HDR10";
    case ColorProfile::Hdr10Hlg: return "BT.2100 HLG";
    default: return "unknown";
    }
}

static const char *ColorSpaceName(reshade::api::color_space color_space)
{
    switch (color_space)
    {
    case reshade::api::color_space::srgb: return "sRGB nonlinear / BT.709";
    case reshade::api::color_space::scrgb: return "extended sRGB linear / scRGB";
    case reshade::api::color_space::hdr10_pq: return "HDR10 ST.2084 / BT.2020";
    case reshade::api::color_space::hdr10_hlg: return "HDR10 HLG / BT.2020";
    default: return "unknown";
    }
}

static ColorProfile DetectColorProfile(reshade::api::color_space color_space, DXGI_FORMAT format)
{
    switch (color_space)
    {
    case reshade::api::color_space::srgb: return ColorProfile::Srgb;
    case reshade::api::color_space::scrgb: return ColorProfile::ScRgb;
    case reshade::api::color_space::hdr10_pq: return ColorProfile::Hdr10Pq;
    case reshade::api::color_space::hdr10_hlg: return ColorProfile::Hdr10Hlg;
    default: break;
    }

    // D3D9 and some D3D11 wrappers cannot report a DXGI color space. Float
    // swapchains are conventionally scRGB; RGB10A2 is conventionally HDR10.
    // All ordinary 8-bit surfaces remain SDR instead of inheriting a global
    // HDR setting, which was the source of the green/red casts.
    switch (format)
    {
    case DXGI_FORMAT_R16G16B16A16_FLOAT:
    case DXGI_FORMAT_R16G16B16A16_TYPELESS:
        return ColorProfile::ScRgb;
    case DXGI_FORMAT_R10G10B10A2_UNORM:
    case DXGI_FORMAT_R10G10B10A2_TYPELESS:
        return ColorProfile::Hdr10Pq;
    default:
        return ColorProfile::Srgb;
    }
}

static void RefreshColorProfile(reshade::api::swapchain *swapchain, DXGI_FORMAT format, const char *boundary)
{
    if (swapchain == nullptr) return;
    const reshade::api::color_space color_space = swapchain->get_color_space();
    const ColorProfile detected = DetectColorProfile(color_space, format);
    const ColorProfile resolved = g_color_profile == ColorProfile::Auto ? detected : g_color_profile;
    const bool observation_changed = color_space != g_detected_color_space || format != g_detected_swapchain_format;
    g_detected_color_space = color_space;
    g_detected_swapchain_format = format;

    if (g_neural_ready && resolved != g_active_color_profile)
    {
        if (observation_changed)
            Log("color auto-detect changed after NGX creation at %s: swapchain=%s fmt=%u resolved=%s; restart required",
                boundary, ColorSpaceName(color_space), static_cast<unsigned int>(format), ProfileName(resolved));
        return;
    }

    if (resolved != g_active_color_profile || observation_changed)
    {
        g_active_color_profile = resolved;
        Log("color profile resolved at %s: requested=%s swapchain=%s fmt=%u active=%s",
            boundary, ProfileName(g_color_profile), ColorSpaceName(color_space),
            static_cast<unsigned int>(format), ProfileName(g_active_color_profile));
    }
}

static const char *ResultName(NVSDK_NGX_Result result)
{
    switch (static_cast<unsigned int>(result))
    {
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
    default: return "Unknown";
    }
}

static void Fail(const char *stage, unsigned int code = 0)
{
    g_neural_failed = true;
    SetStatus("failed at %s (0x%08X); see log", stage, code);
    Log("standalone pipeline FAILED at %s: 0x%08X", stage, code);
    // A failed neural frame must never strand the user behind an empty native
    // proxy. Leave the game's real surface visible so logs/menu remain usable.
    if (g_proxy_window)
    {
        g_proxy_hidden = true;
        ShowWindow(g_proxy_window, SW_HIDE);
    }
}

static bool WaitForNeuralGpu(DWORD timeout = 30000)
{
    if (!g_neural_fence || g_neural_fence_value == 0 || g_neural_fence->GetCompletedValue() >= g_neural_fence_value)
        return true;
    if (FAILED(g_neural_fence->SetEventOnCompletion(g_neural_fence_value, g_neural_fence_event)) ||
        WaitForSingleObject(g_neural_fence_event, timeout) != WAIT_OBJECT_0)
    {
        Fail("neural GPU fence timeout", static_cast<unsigned int>(GetLastError()));
        return false;
    }
    return true;
}

static bool BeginNeuralCommands()
{
    if (!WaitForNeuralGpu()) return false;
    HRESULT hr = g_neural_allocator->Reset();
    if (SUCCEEDED(hr)) hr = g_neural_list->Reset(g_neural_allocator.Get(), nullptr);
    if (FAILED(hr)) { Fail("neural command-list reset", static_cast<unsigned int>(hr)); return false; }
    return true;
}

static bool SubmitNeuralCommands(bool wait)
{
    HRESULT hr = g_neural_list->Close();
    if (FAILED(hr)) { Fail("neural command-list close", static_cast<unsigned int>(hr)); return false; }
    ID3D12CommandList *lists[] = {g_neural_list.Get()};
    g_command_queue->ExecuteCommandLists(1, lists);
    const UINT64 value = ++g_neural_fence_value;
    hr = g_command_queue->Signal(g_neural_fence.Get(), value);
    if (FAILED(hr)) { Fail("neural queue signal", static_cast<unsigned int>(hr)); return false; }
    return !wait || WaitForNeuralGpu();
}

static HMODULE LoadInstalledNgxCore()
{
    if (HMODULE loaded = GetModuleHandleW(L"_nvngx.dll"))
    {
        Log("NGX core: reusing loaded _nvngx.dll at %p", loaded);
        return loaded;
    }
    wchar_t system[MAX_PATH] = {}, pattern[MAX_PATH] = {};
    if (GetSystemDirectoryW(system, MAX_PATH) == 0) return nullptr;
    swprintf_s(pattern, L"%s\\DriverStore\\FileRepository\\nv*.inf_amd64_*", system);
    WIN32_FIND_DATAW found = {};
    HANDLE search = FindFirstFileW(pattern, &found);
    if (search == INVALID_HANDLE_VALUE)
    {
        Log("NGX core: no NVIDIA DriverStore packages matched %ls error=%lu", pattern, GetLastError());
        return nullptr;
    }
    std::wstring best;
    FILETIME best_time = {};
    do
    {
        if ((found.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) == 0) continue;
        wchar_t candidate[MAX_PATH] = {};
        swprintf_s(candidate, L"%s\\DriverStore\\FileRepository\\%s\\_nvngx.dll", system, found.cFileName);
        WIN32_FILE_ATTRIBUTE_DATA attributes = {};
        if (!GetFileAttributesExW(candidate, GetFileExInfoStandard, &attributes)) continue;
        if (best.empty() || CompareFileTime(&attributes.ftLastWriteTime, &best_time) > 0)
        {
            best = candidate;
            best_time = attributes.ftLastWriteTime;
        }
    } while (FindNextFileW(search, &found));
    FindClose(search);
    if (best.empty())
    {
        Log("NGX core: no _nvngx.dll found in NVIDIA DriverStore packages matching %ls", pattern);
        return nullptr;
    }
    HMODULE module = LoadLibraryExW(best.c_str(), nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
    Log("NGX core: LoadLibraryExW(%ls) -> %p error=%lu", best.c_str(), module, module ? 0 : GetLastError());
    return module;
}

static void BuildRuntimePath(wchar_t (&path)[MAX_PATH], const wchar_t *directory, const wchar_t *name)
{
    const size_t length = wcslen(directory);
    const bool has_separator = length != 0 && (directory[length - 1] == L'\\' || directory[length - 1] == L'/');
    swprintf_s(path, L"%ls%ls%ls", directory, has_separator ? L"" : L"\\", name);
}

static std::vector<std::wstring> RuntimeSearchDirectories()
{
    std::vector<std::wstring> candidates;
    auto add_candidate = [&candidates](const wchar_t *candidate)
    {
        if (candidate == nullptr || candidate[0] == L'\0') return;
        for (const std::wstring &existing : candidates)
            if (_wcsicmp(existing.c_str(), candidate) == 0) return;
        candidates.emplace_back(candidate);
    };

    // ReShade may load an addon from a dedicated addon search directory, while
    // users conventionally put NGX runtime DLLs beside the game executable.
    // Probe both locations explicitly instead of assuming they are identical.
    add_candidate(g_addon_directory);

    wchar_t game_directory[MAX_PATH] = {};
    if (GetModuleFileNameW(nullptr, game_directory, MAX_PATH) != 0)
    {
        wchar_t *slash = wcsrchr(game_directory, L'\\');
        if (slash != nullptr)
        {
            slash[1] = L'\0';
            add_candidate(game_directory);
        }
    }

    wchar_t working_directory[MAX_PATH] = {};
    const DWORD working_length = GetCurrentDirectoryW(MAX_PATH, working_directory);
    if (working_length != 0 && working_length < MAX_PATH)
        add_candidate(working_directory);

    wchar_t local_app_data[MAX_PATH] = {};
    if (GetEnvironmentVariableW(L"LOCALAPPDATA", local_app_data, MAX_PATH) != 0)
    {
        wchar_t custom_addons[MAX_PATH] = {};
        swprintf_s(custom_addons, L"%ls\\RHI\\Custom\\Addons", local_app_data);
        add_candidate(custom_addons);
    }

    return candidates;
}

static bool FindStandaloneRuntimeFile(const wchar_t *name, wchar_t (&path)[MAX_PATH], bool required)
{
    for (const std::wstring &directory : RuntimeSearchDirectories())
    {
        wchar_t candidate[MAX_PATH] = {};
        BuildRuntimePath(candidate, directory.c_str(), name);
        Log("runtime search candidate: %ls", candidate);
        const DWORD attributes = GetFileAttributesW(candidate);
        if (attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_DIRECTORY) == 0)
        {
            wcscpy_s(path, candidate);
            return true;
        }
    }
    Log("%s runtime not found: %ls", required ? "required" : "optional", name);
    return false;
}

static bool InitializeNgx()
{
    if (g_ngx_params != nullptr) return true;
    wchar_t nr_path[MAX_PATH] = {}, dlss_path[MAX_PATH] = {}, dlssg_path[MAX_PATH] = {}, bridge_path[MAX_PATH] = {};
    const bool have_nr = FindStandaloneRuntimeFile(L"nvngx_dlssnr.dll", nr_path, true);
    const bool have_dlss = FindStandaloneRuntimeFile(L"nvngx_dlss.dll", dlss_path, true);
    const bool have_dlssg = FindStandaloneRuntimeFile(L"nvngx_dlssg.dll", dlssg_path, false);
    const bool have_bridge = FindStandaloneRuntimeFile(L"nvngx.dll", bridge_path, true);
    if (!have_nr || !have_dlss || !have_bridge)
    {
        Fail("required private runtime dependency missing", ERROR_FILE_NOT_FOUND);
        return false;
    }
    for (const wchar_t *runtime_path : {nr_path, dlss_path, bridge_path})
    {
        WIN32_FILE_ATTRIBUTE_DATA data = {};
        if (!GetFileAttributesExW(runtime_path, GetFileExInfoStandard, &data)) return false;
        const unsigned long long size = (static_cast<unsigned long long>(data.nFileSizeHigh) << 32) | data.nFileSizeLow;
        Log("runtime dependency: %ls (%llu bytes)", runtime_path, size);
    }
    if (have_dlssg)
    {
        WIN32_FILE_ATTRIBUTE_DATA data = {};
        if (GetFileAttributesExW(dlssg_path, GetFileExInfoStandard, &data))
        {
            const unsigned long long size = (static_cast<unsigned long long>(data.nFileSizeHigh) << 32) | data.nFileSizeLow;
            Log("runtime dependency: %ls (%llu bytes)", dlssg_path, size);
        }
    }
    else
    {
        Log("DLSS-G runtime unavailable; NR and DLSS SR will continue with frame generation disabled");
        g_framegen_failed = true;
    }

    g_nr_module = LoadLibraryExW(nr_path, nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
    auto nr_init = g_nr_module ? reinterpret_cast<NgxSnippetInitD3D12Ext>(GetProcAddress(g_nr_module, "NVSDK_NGX_D3D12_Init_Ext")) : nullptr;
    g_nr_create = g_nr_module ? reinterpret_cast<NgxCreateFeature>(GetProcAddress(g_nr_module, "NVSDK_NGX_D3D12_CreateFeature")) : nullptr;
    g_nr_evaluate = g_nr_module ? reinterpret_cast<NgxEvaluateFeature>(GetProcAddress(g_nr_module, "NVSDK_NGX_D3D12_EvaluateFeature")) : nullptr;
    g_nr_release = g_nr_module ? reinterpret_cast<NgxReleaseFeature>(GetProcAddress(g_nr_module, "NVSDK_NGX_D3D12_ReleaseFeature")) : nullptr;
    g_nr_populate = g_nr_module ? reinterpret_cast<NgxPopulateParameters>(GetProcAddress(g_nr_module, "NVSDK_NGX_D3D12_PopulateParameters_Impl")) : nullptr;
    g_bridge_module = LoadLibraryExW(bridge_path, nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
    auto bridge_init = g_bridge_module ? reinterpret_cast<NgxBridgeInitD3D12Ext>(GetProcAddress(g_bridge_module, "NVNGXBridge_D3D12_InitExt")) : nullptr;
    g_bridge_create = g_bridge_module ? reinterpret_cast<NgxBridgeCreateFeature>(GetProcAddress(g_bridge_module, "NVNGXBridge_D3D12_CreateFeature")) : nullptr;
    g_bridge_evaluate = g_bridge_module ? reinterpret_cast<NgxBridgeEvaluateFeature>(GetProcAddress(g_bridge_module, "NVNGXBridge_D3D12_EvaluateFeature")) : nullptr;
    g_bridge_release = g_bridge_module ? reinterpret_cast<NgxBridgeReleaseFeature>(GetProcAddress(g_bridge_module, "NVNGXBridge_D3D12_ReleaseFeature")) : nullptr;
    g_bridge_populate = g_bridge_module ? reinterpret_cast<NgxBridgePopulateParameters>(GetProcAddress(g_bridge_module, "NVNGXBridge_D3D12_PopulateParameters")) : nullptr;
    if (!nr_init || !g_nr_create || !g_nr_evaluate || !g_nr_release || !bridge_init ||
        !g_nr_populate || !g_bridge_create || !g_bridge_evaluate || !g_bridge_release || !g_bridge_populate)
    {
        Fail("NR snippet/caller bridge exports", static_cast<unsigned int>(GetLastError()));
        return false;
    }

    g_core_module = LoadInstalledNgxCore();
    auto core_init = g_core_module ? reinterpret_cast<NgxCoreInitD3D12>(GetProcAddress(g_core_module, "NVSDK_NGX_D3D12_Init")) : nullptr;
    auto get_capabilities = g_core_module ? reinterpret_cast<NgxGetCapabilityParameters>(GetProcAddress(g_core_module, "NVSDK_NGX_D3D12_GetCapabilityParameters")) : nullptr;
    if (!core_init || !get_capabilities) { Fail("driver NGX core exports", static_cast<unsigned int>(GetLastError())); return false; }

    wchar_t temp[MAX_PATH] = {}, parent[MAX_PATH] = {}, data_path[MAX_PATH] = {};
    if (GetTempPathW(MAX_PATH, temp) == 0) { Fail("NGX data path", GetLastError()); return false; }
    swprintf_s(parent, L"%sdlssnr", temp);
    swprintf_s(data_path, L"%s\\RenoDX", parent);
    CreateDirectoryW(parent, nullptr);
    CreateDirectoryW(data_path, nullptr);
    NVSDK_NGX_Result result = core_init(kGenericCustomCoreId, data_path, g_neural_device.Get(), NVSDK_NGX_Version_API);
    Log("driver-core Init(app=0x%llX) = 0x%08X (%s)", kGenericCustomCoreId, static_cast<unsigned int>(result), ResultName(result));
    if (NVSDK_NGX_FAILED(result)) { Fail("driver-core Init", static_cast<unsigned int>(result)); return false; }
    result = bridge_init(nr_init, kGenericCustomCoreId, nr_path, g_neural_device.Get(), NVSDK_NGX_Version_API, nullptr);
    Log("NR snippet Init_Ext = 0x%08X (%s)", static_cast<unsigned int>(result), ResultName(result));
    if (NVSDK_NGX_FAILED(result)) { Fail("NR snippet Init_Ext", static_cast<unsigned int>(result)); return false; }

    g_dlss_module = LoadLibraryExW(dlss_path, nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
    auto dlss_init = g_dlss_module ? reinterpret_cast<NgxSnippetInitD3D12Ext>(GetProcAddress(g_dlss_module, "NVSDK_NGX_D3D12_Init_Ext")) : nullptr;
    g_sr_create = g_dlss_module ? reinterpret_cast<NgxCreateFeature>(GetProcAddress(g_dlss_module, "NVSDK_NGX_D3D12_CreateFeature")) : nullptr;
    g_sr_evaluate = g_dlss_module ? reinterpret_cast<NgxEvaluateFeature>(GetProcAddress(g_dlss_module, "NVSDK_NGX_D3D12_EvaluateFeature")) : nullptr;
    g_sr_release = g_dlss_module ? reinterpret_cast<NgxReleaseFeature>(GetProcAddress(g_dlss_module, "NVSDK_NGX_D3D12_ReleaseFeature")) : nullptr;
    if (!dlss_init || !g_sr_create || !g_sr_evaluate || !g_sr_release) { Fail("DLSS SR snippet exports", static_cast<unsigned int>(GetLastError())); return false; }
    result = bridge_init(dlss_init, kGenericCustomCoreId, dlss_path, g_neural_device.Get(), NVSDK_NGX_Version_API, nullptr);
    Log("DLSS SR snippet Init_Ext = 0x%08X (%s)", static_cast<unsigned int>(result), ResultName(result));
    if (NVSDK_NGX_FAILED(result)) { Fail("DLSS SR snippet Init_Ext", static_cast<unsigned int>(result)); return false; }

    g_dlssg_module = have_dlssg ? LoadLibraryExW(dlssg_path, nullptr, LOAD_WITH_ALTERED_SEARCH_PATH) : nullptr;
    auto dlssg_init = g_dlssg_module ? reinterpret_cast<NgxSnippetInitD3D12Ext>(GetProcAddress(g_dlssg_module, "NVSDK_NGX_D3D12_Init_Ext")) : nullptr;
    g_fg_create = g_dlssg_module ? reinterpret_cast<NgxCreateFeature>(GetProcAddress(g_dlssg_module, "NVSDK_NGX_D3D12_CreateFeature")) : nullptr;
    g_fg_evaluate = g_dlssg_module ? reinterpret_cast<NgxEvaluateFeature>(GetProcAddress(g_dlssg_module, "NVSDK_NGX_D3D12_EvaluateFeature")) : nullptr;
    g_fg_release = g_dlssg_module ? reinterpret_cast<NgxReleaseFeature>(GetProcAddress(g_dlssg_module, "NVSDK_NGX_D3D12_ReleaseFeature")) : nullptr;
    g_fg_populate = g_dlssg_module ? reinterpret_cast<NgxPopulateParameters>(GetProcAddress(g_dlssg_module, "NVSDK_NGX_D3D12_PopulateParameters_Impl")) : nullptr;
    if (!dlssg_init || !g_fg_create || !g_fg_evaluate || !g_fg_release || !g_fg_populate)
    {
        Log("DLSS-G runtime unavailable; frame generation will remain disabled");
        g_framegen_failed = true;
    }
    else
    {
        result = bridge_init(dlssg_init, kGenericCustomCoreId, dlssg_path, g_neural_device.Get(), NVSDK_NGX_Version_API, nullptr);
        Log("DLSS-G snippet Init_Ext = 0x%08X (%s)", static_cast<unsigned int>(result), ResultName(result));
        if (NVSDK_NGX_FAILED(result)) g_framegen_failed = true;
    }
    result = get_capabilities(&g_ngx_params);
    Log("driver-core GetCapabilityParameters = 0x%08X (%s), ptr=%p", static_cast<unsigned int>(result), ResultName(result), g_ngx_params);
    if (NVSDK_NGX_FAILED(result) || !g_ngx_params) { Fail("GetCapabilityParameters", static_cast<unsigned int>(result)); return false; }
    result = g_bridge_populate(g_nr_populate, g_ngx_params);
    Log("NR PopulateParameters_Impl = 0x%08X (%s)", static_cast<unsigned int>(result), ResultName(result));
    if (NVSDK_NGX_FAILED(result)) { Fail("NR PopulateParameters_Impl", static_cast<unsigned int>(result)); return false; }
    void *scaling_callback = nullptr;
    result = g_ngx_params->Get("DLSSNRComputeScalingRatioCallback", &scaling_callback);
    g_nr_compute_scaling_ratio = reinterpret_cast<NgxPopulateParameters>(scaling_callback);
    Log("NR scaling callback get=0x%08X ptr=%p", static_cast<unsigned int>(result), scaling_callback);
    return true;
}

static int FeatureFlags()
{
    return NVSDK_NGX_DLSS_Feature_Flags_MVLowRes |
        NVSDK_NGX_DLSS_Feature_Flags_AutoExposure |
        (g_depth_reversed ? NVSDK_NGX_DLSS_Feature_Flags_DepthInverted : 0) |
        (g_active_color_profile == ColorProfile::Srgb ? 0 : NVSDK_NGX_DLSS_Feature_Flags_IsHDR);
}

static void SetNrCreationContract()
{
    const UINT iw = g_resource_input_width, ih = g_resource_input_height;
    g_ngx_params->Reset();
    const NVSDK_NGX_Result populate_result = g_bridge_populate(g_nr_populate, g_ngx_params);
    Log("NR creation PopulateParameters_Impl = 0x%08X (%s)",
        static_cast<unsigned int>(populate_result), ResultName(populate_result));
    g_ngx_params->Set("CreationNodeMask", 1u);
    g_ngx_params->Set("VisibilityNodeMask", 1u);
    g_ngx_params->Set("Width", iw); g_ngx_params->Set("Height", ih);
    g_ngx_params->Set("OutWidth", iw); g_ngx_params->Set("OutHeight", ih);
    g_ngx_params->Set("ResourceWidth", iw); g_ngx_params->Set("ResourceHeight", ih);
    g_ngx_params->Set("ResourceOutWidth", iw); g_ngx_params->Set("ResourceOutHeight", ih);
    g_ngx_params->Set("PerfQualityValue", static_cast<int>(NVSDK_NGX_PerfQuality_Value_UltraQuality));
    g_ngx_params->Set("DLSS.Feature.Create.Flags", FeatureFlags());
    g_ngx_params->Set("DLSS.Enable.Output.Subrects", 0);
    g_ngx_params->Set("DLSS.Denoise.Mode", 1);
    g_ngx_params->Set("DLSS.Roughness.Mode", 0u);
    g_ngx_params->Set("DLSS.Use.HW.Depth", 1u);
    g_ngx_params->Set("DLSSNR.Enabled", 1u);
    g_ngx_params->Set("DLSSNR.InputWidth", iw); g_ngx_params->Set("DLSSNR.InputHeight", ih);
    g_ngx_params->Set("DLSSNR.Width", iw); g_ngx_params->Set("DLSSNR.Height", ih);
    g_ngx_params->Set("DLSSNR.OutputWidth", iw); g_ngx_params->Set("DLSSNR.OutputHeight", ih);
    g_ngx_params->Set("Output.Width", iw); g_ngx_params->Set("Output.Height", ih);
    g_ngx_params->Set("DLSSNR.Upscaling", 1u);
    g_ngx_params->Set("DLSSNR.ScalingRatio", 1.0f); g_ngx_params->Set("DLSSNR.Scale", 1.0f);
    g_ngx_params->Set("DLSSNR.Hint.Render.Preset", g_nr_model);
    g_ngx_params->Set("DLSSNR.Intensity", g_nr_intensity);
    g_ngx_params->Set("DLSSNR.LocalToneStrength", g_nr_local_tone);
    g_ngx_params->Set("DLSSNR.LocalStructureStrength", g_nr_local_structure);
    g_ngx_params->Set("DLSSNR.SkinStructureStrength", g_nr_skin_structure);
    g_ngx_params->Set("DLSSNR.UseAutoMask", 1u);
    g_ngx_params->Set("DLSSNR.UICorrection", 0u);
    if (g_nr_compute_scaling_ratio)
    {
        const NVSDK_NGX_Result ratio_result = g_nr_compute_scaling_ratio(g_ngx_params);
        float resolved = 1.0f;
        g_ngx_params->Get("DLSSNR.ScalingRatio", &resolved);
        g_nr_scaling_ratio = resolved;
        g_ngx_params->Set("DLSSNR.Scale", resolved);
        Log("NR provider scaling ratio result=0x%08X (%s), resolved=%.6f",
            static_cast<unsigned int>(ratio_result), ResultName(ratio_result), resolved);
    }
}

static NVSDK_NGX_Result SafeCreate(bool nr, DWORD *exception)
{
    *exception = 0;
    __try
    {
        return g_bridge_create(nr ? g_nr_create : g_sr_create, g_neural_list.Get(),
            nr ? kFeatureDlssNr : NVSDK_NGX_Feature_SuperSampling, g_ngx_params,
            nr ? &g_nr_feature : &g_sr_feature);
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        *exception = GetExceptionCode();
        return static_cast<NVSDK_NGX_Result>(0x7fffffff);
    }
}

static NVSDK_NGX_Result SafeEvaluate(bool nr, DWORD *exception)
{
    *exception = 0;
    __try
    {
        return g_bridge_evaluate(nr ? g_nr_evaluate : g_sr_evaluate, g_neural_list.Get(),
            nr ? g_nr_feature : g_sr_feature, g_ngx_params, nullptr);
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        *exception = GetExceptionCode();
        return static_cast<NVSDK_NGX_Result>(0x7fffffff);
    }
}

static NVSDK_NGX_Result SafeRelease(NgxReleaseFeature release, NVSDK_NGX_Handle *handle, DWORD *exception)
{
    *exception = 0;
    __try
    {
        return g_bridge_release(release, handle);
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        *exception = GetExceptionCode();
        return static_cast<NVSDK_NGX_Result>(0x7fffffff);
    }
}

static NVSDK_NGX_Result SafeCreateFg(DWORD *exception);
static NVSDK_NGX_Result SafeEvaluateFg(DWORD *exception);

static bool CreateFeatures()
{
    SetNrCreationContract();
    if (!BeginNeuralCommands()) return false;
    DWORD exception = 0;
    NVSDK_NGX_Result result = SafeCreate(true, &exception);
    if (exception) { g_neural_list->Close(); Fail("NR feature creation exception", exception); return false; }
    if (!SubmitNeuralCommands(true)) return false;
    Log("CreateFeature(feature=18) = 0x%08X (%s), handle=%p", static_cast<unsigned int>(result), ResultName(result), g_nr_feature);
    if (NVSDK_NGX_FAILED(result) || !g_nr_feature) { Fail("NR feature creation", static_cast<unsigned int>(result)); return false; }

    const float ratio = static_cast<float>(g_resource_input_width) / static_cast<float>(g_resource_output_width);
    const int quality = ratio >= 0.72f ? NVSDK_NGX_PerfQuality_Value_UltraQuality :
        ratio >= 0.62f ? NVSDK_NGX_PerfQuality_Value_MaxQuality :
        ratio >= 0.54f ? NVSDK_NGX_PerfQuality_Value_Balanced :
        ratio >= 0.42f ? NVSDK_NGX_PerfQuality_Value_MaxPerf : NVSDK_NGX_PerfQuality_Value_UltraPerformance;
    g_ngx_params->Reset();
    g_ngx_params->Set("CreationNodeMask", 1u); g_ngx_params->Set("VisibilityNodeMask", 1u);
    g_ngx_params->Set("Width", g_resource_input_width); g_ngx_params->Set("Height", g_resource_input_height);
    g_ngx_params->Set("OutWidth", g_resource_output_width); g_ngx_params->Set("OutHeight", g_resource_output_height);
    g_ngx_params->Set("PerfQualityValue", quality);
    g_ngx_params->Set("DLSS.Feature.Create.Flags", FeatureFlags());
    g_ngx_params->Set("DLSS.Enable.Output.Subrects", 0);
    if (!BeginNeuralCommands()) return false;
    exception = 0;
    result = SafeCreate(false, &exception);
    if (exception) { g_neural_list->Close(); Fail("DLSS SR feature creation exception", exception); return false; }
    if (!SubmitNeuralCommands(true)) return false;
    Log("CreateFeature(feature=SuperSampling) = 0x%08X (%s), handle=%p quality=%d", static_cast<unsigned int>(result), ResultName(result), g_sr_feature, quality);
    if (NVSDK_NGX_FAILED(result) || !g_sr_feature) { Fail("DLSS SR feature creation", static_cast<unsigned int>(result)); return false; }

    if (!g_framegen_failed)
    {
        g_ngx_params->Reset();
        const NVSDK_NGX_Result populate_result = g_bridge_populate(g_fg_populate, g_ngx_params);
        Log("DLSS-G PopulateParameters_Impl = 0x%08X (%s)",
            static_cast<unsigned int>(populate_result), ResultName(populate_result));
        g_ngx_params->Set("CreationNodeMask", 1u); g_ngx_params->Set("VisibilityNodeMask", 1u);
        g_ngx_params->Set("Width", g_resource_output_width); g_ngx_params->Set("Height", g_resource_output_height);
        const DXGI_FORMAT fg_format = g_active_color_profile == ColorProfile::Srgb ?
            DXGI_FORMAT_R8G8B8A8_UNORM : DXGI_FORMAT_R16G16B16A16_FLOAT;
        g_ngx_params->Set("DLSSG.BackbufferFormat", static_cast<unsigned int>(fg_format));
        g_ngx_params->Set("DLSSG.InternalWidth", g_resource_input_width);
        g_ngx_params->Set("DLSSG.InternalHeight", g_resource_input_height);
        g_ngx_params->Set("DLSSG.DynamicResolution", 0u);
        if (!BeginNeuralCommands()) return false;
        exception = 0;
        result = SafeCreateFg(&exception);
        if (exception)
        {
            g_neural_list->Close();
            Log("DLSS-G creation exception 0x%08X; continuing without frame generation", exception);
            g_framegen_failed = true;
        }
        else if (!SubmitNeuralCommands(true)) return false;
        if (!exception)
        {
            Log("CreateFeature(feature=FrameGeneration) = 0x%08X (%s), handle=%p",
                static_cast<unsigned int>(result), ResultName(result), g_fg_feature);
            if (NVSDK_NGX_FAILED(result) || !g_fg_feature) g_framegen_failed = true;
        }
    }
    Log("standalone contract ready: NR feature 18 %ux%u active rect, DLSS SR -> %ux%u, DLSS-G=%s, model=%d, profile=%s",
        g_resource_input_width, g_resource_input_height, g_resource_output_width, g_resource_output_height,
        (!g_framegen_failed && g_fg_feature) ? "ready" : "fallback-off",
        g_nr_model, ProfileName(g_active_color_profile));
    g_active_nr_model = g_nr_model;
    return true;
}

static bool RecreateFeatures()
{
    if (!g_neural_ready || !g_nr_feature || !g_sr_feature) return false;
    if (!WaitForNeuralGpu()) return false;
    DWORD exception = 0;
    if (g_fg_feature)
    {
        const NVSDK_NGX_Result fg_result = SafeRelease(g_fg_release, g_fg_feature, &exception);
        if (exception || NVSDK_NGX_FAILED(fg_result))
        {
            Log("DLSS-G release failed during model change; disabling frame generation");
            g_framegen_failed = true;
        }
        g_fg_feature = nullptr;
    }
    NVSDK_NGX_Result sr_result = SafeRelease(g_sr_release, g_sr_feature, &exception);
    if (exception || NVSDK_NGX_FAILED(sr_result))
    {
        Fail(exception ? "DLSS SR release exception" : "DLSS SR release",
            exception ? exception : static_cast<unsigned int>(sr_result));
        return false;
    }
    g_sr_feature = nullptr;
    NVSDK_NGX_Result nr_result = SafeRelease(g_nr_release, g_nr_feature, &exception);
    if (exception || NVSDK_NGX_FAILED(nr_result))
    {
        Fail(exception ? "NR release exception" : "NR release",
            exception ? exception : static_cast<unsigned int>(nr_result));
        return false;
    }
    g_nr_feature = nullptr;
    Log("released live features for model change: old=%d requested=%d", g_active_nr_model, g_nr_model);
    g_fg_frames = 0;
    g_need_history_reset = true;
    if (!CreateFeatures()) return false;
    Log("live model switch complete: active model=%d", g_active_nr_model);
    return true;
}

static DXGI_FORMAT TypedInputFormat(DXGI_FORMAT format)
{
    switch (format)
    {
    case DXGI_FORMAT_R8G8B8A8_TYPELESS:
    case DXGI_FORMAT_R8G8B8A8_UNORM_SRGB: return DXGI_FORMAT_R8G8B8A8_UNORM;
    case DXGI_FORMAT_R10G10B10A2_TYPELESS: return DXGI_FORMAT_R10G10B10A2_UNORM;
    case DXGI_FORMAT_R16G16B16A16_TYPELESS: return DXGI_FORMAT_R16G16B16A16_FLOAT;
    case DXGI_FORMAT_B8G8R8A8_TYPELESS:
    case DXGI_FORMAT_B8G8R8A8_UNORM_SRGB: return DXGI_FORMAT_B8G8R8A8_UNORM;
    case DXGI_FORMAT_B8G8R8X8_TYPELESS:
    case DXGI_FORMAT_B8G8R8X8_UNORM_SRGB: return DXGI_FORMAT_B8G8R8X8_UNORM;
    default: return format;
    }
}

static bool CreateTexture(UINT width, UINT height, DXGI_FORMAT format, bool uav,
    D3D12_RESOURCE_STATES initial_state, Microsoft::WRL::ComPtr<ID3D12Resource> &resource)
{
    D3D12_HEAP_PROPERTIES heap = {};
    heap.Type = D3D12_HEAP_TYPE_DEFAULT;
    D3D12_RESOURCE_DESC desc = {};
    desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    desc.Width = width; desc.Height = height; desc.DepthOrArraySize = 1; desc.MipLevels = 1;
    desc.Format = format; desc.SampleDesc.Count = 1; desc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
    desc.Flags = uav ? D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS : D3D12_RESOURCE_FLAG_NONE;
    const HRESULT hr = g_neural_device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &desc,
        initial_state, nullptr, IID_PPV_ARGS(&resource));
    if (FAILED(hr)) { Fail("neural texture allocation", static_cast<unsigned int>(hr)); return false; }
    return true;
}

static bool CreateGuideTexture(UINT width, UINT height, DXGI_FORMAT format, const float clear_color[4],
    UINT descriptor_index, Microsoft::WRL::ComPtr<ID3D12Resource> &resource)
{
    D3D12_HEAP_PROPERTIES heap = {};
    heap.Type = D3D12_HEAP_TYPE_DEFAULT;
    D3D12_RESOURCE_DESC desc = {};
    desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    desc.Width = width; desc.Height = height; desc.DepthOrArraySize = 1; desc.MipLevels = 1;
    desc.Format = format; desc.SampleDesc.Count = 1; desc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
    desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
    D3D12_CLEAR_VALUE clear = {};
    clear.Format = format;
    memcpy(clear.Color, clear_color, sizeof(clear.Color));
    const HRESULT hr = g_neural_device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &desc,
        D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, &clear, IID_PPV_ARGS(&resource));
    if (FAILED(hr)) { Fail("fallback guide allocation", static_cast<unsigned int>(hr)); return false; }
    D3D12_CPU_DESCRIPTOR_HANDLE rtv = g_guide_rtv_heap->GetCPUDescriptorHandleForHeapStart();
    rtv.ptr += static_cast<SIZE_T>(descriptor_index) * g_guide_rtv_stride;
    g_neural_device->CreateRenderTargetView(resource.Get(), nullptr, rtv);
    return true;
}

static void PublishOutput(ID3D12Resource *resource)
{
    resource->AddRef();
    ID3D12Resource *old = static_cast<ID3D12Resource *>(
        InterlockedExchangePointer(reinterpret_cast<void *volatile *>(&g_nr_output), resource));
    if (old) old->Release();
}

static NVSDK_NGX_Result SafeCreateFg(DWORD *exception)
{
    *exception = 0;
    __try
    {
        return g_bridge_create(g_fg_create, g_neural_list.Get(),
            NVSDK_NGX_Feature_FrameGeneration, g_ngx_params, &g_fg_feature);
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        *exception = GetExceptionCode();
        return static_cast<NVSDK_NGX_Result>(0x7fffffff);
    }
}

static NVSDK_NGX_Result SafeEvaluateFg(DWORD *exception)
{
    *exception = 0;
    __try
    {
        return g_bridge_evaluate(g_fg_evaluate, g_neural_list.Get(),
            g_fg_feature, g_ngx_params, nullptr);
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        *exception = GetExceptionCode();
        return static_cast<NVSDK_NGX_Result>(0x7fffffff);
    }
}

static void ClearPublishedOutput()
{
    ID3D12Resource *old = static_cast<ID3D12Resource *>(
        InterlockedExchangePointer(reinterpret_cast<void *volatile *>(&g_nr_output), nullptr));
    if (old) old->Release();
}

static bool RetireResolutionDependentResources(UINT next_width, UINT next_height, DXGI_FORMAT next_format)
{
    Log("resolution reconfiguration begin: %ux%u fmt=%u -> %ux%u fmt=%u",
        g_resource_input_width, g_resource_input_height, static_cast<unsigned int>(g_resource_input_format),
        next_width, next_height, static_cast<unsigned int>(next_format));

    // The proxy submits after the neural fence, on the same queue, but owns a
    // later fence value. Do not withdraw its SRV resource until that sampling
    // work has completed.
    if (g_proxy_fence && g_proxy_fence_value != 0 && g_proxy_fence->GetCompletedValue() < g_proxy_fence_value)
    {
        if (FAILED(g_proxy_fence->SetEventOnCompletion(g_proxy_fence_value, g_proxy_fence_event)) ||
            WaitForSingleObject(g_proxy_fence_event, 30000) != WAIT_OBJECT_0)
        {
            Fail("proxy GPU fence during resolution change", static_cast<unsigned int>(GetLastError()));
            return false;
        }
    }
    if (!WaitForNeuralGpu()) return false;

    DWORD exception = 0;
    if (g_fg_feature)
    {
        const NVSDK_NGX_Result result = SafeRelease(g_fg_release, g_fg_feature, &exception);
        if (exception || NVSDK_NGX_FAILED(result))
        {
            Log("DLSS-G release failed during resolution change; continuing with FG disabled");
            g_framegen_failed = true;
        }
        g_fg_feature = nullptr;
    }
    if (g_sr_feature)
    {
        const NVSDK_NGX_Result result = SafeRelease(g_sr_release, g_sr_feature, &exception);
        if (exception || NVSDK_NGX_FAILED(result))
        {
            Fail(exception ? "DLSS SR release during resolution change" : "DLSS SR release during resolution change",
                exception ? exception : static_cast<unsigned int>(result));
            return false;
        }
        g_sr_feature = nullptr;
    }
    if (g_nr_feature)
    {
        const NVSDK_NGX_Result result = SafeRelease(g_nr_release, g_nr_feature, &exception);
        if (exception || NVSDK_NGX_FAILED(result))
        {
            Fail(exception ? "NR release during resolution change" : "NR release during resolution change",
                exception ? exception : static_cast<unsigned int>(result));
            return false;
        }
        g_nr_feature = nullptr;
    }

    ClearPublishedOutput();
    g_neural_ready = false;
    g_fg_frames = 0;
    g_need_history_reset = true;
    g_using_external_guides = false;
    g_mask_available = false;
    g_pending_proxy_frame = false;
    g_captured_motion.Reset(); g_captured_depth.Reset(); g_captured_mask.Reset();
    g_fallback_motion.Reset(); g_fallback_depth.Reset();
    ReleaseLegacyFrameResources();
    g_fg_stage.Reset(); g_sr_stage.Reset(); g_nr_stage.Reset(); g_packed_color.Reset();

    if (g_runtime)
    {
        auto *device = g_runtime->get_device();
        for (const BackbufferView &entry : g_backbuffer_views)
            if (entry.rtv.handle) device->destroy_resource_view(entry.rtv);
        g_backbuffer_views.clear();
    }
    Log("resolution reconfiguration retired old NGX handles and size-dependent resources");
    return true;
}

static void ReleaseLegacyFrameResources()
{
    if (g_vulkan.ok)
    {
        if (g_vulkan_input != VK_NULL_HANDLE) g_vulkan.DestroyImage(g_vulkan.dev, g_vulkan_input, nullptr);
        if (g_vulkan_post != VK_NULL_HANDLE) g_vulkan.DestroyImage(g_vulkan.dev, g_vulkan_post, nullptr);
        if (g_vulkan_input_memory != VK_NULL_HANDLE) g_vulkan.FreeMemory(g_vulkan.dev, g_vulkan_input_memory, nullptr);
        if (g_vulkan_post_memory != VK_NULL_HANDLE) g_vulkan.FreeMemory(g_vulkan.dev, g_vulkan_post_memory, nullptr);
    }
    g_vulkan_input = g_vulkan_post = VK_NULL_HANDLE;
    g_vulkan_input_memory = g_vulkan_post_memory = VK_NULL_HANDLE;
    g_vulkan_input_layout_initialized = g_vulkan_post_layout_initialized = false;
    g_legacy_input9.Reset(); g_legacy_post9.Reset();
    g_legacy_input9_11.Reset(); g_legacy_post9_11.Reset();
    g_legacy_input11.Reset(); g_legacy_post11.Reset(); g_legacy_post12.Reset();
    g_legacy_width = g_legacy_height = 0;
    g_legacy_format = DXGI_FORMAT_UNKNOWN;
}

static Microsoft::WRL::ComPtr<IDXGIAdapter1> FindAdapterByLuid(reshade::api::device *device)
{
    Microsoft::WRL::ComPtr<IDXGIAdapter1> adapter;
    if (!device) return adapter;
    LUID luid = {};
    if (!device->get_property(reshade::api::device_properties::adapter_luid, &luid))
    {
        Log("Vulkan interop could not query the game adapter LUID; using the default adapter");
        return adapter;
    }
    Microsoft::WRL::ComPtr<IDXGIFactory4> factory;
    if (FAILED(CreateDXGIFactory1(IID_PPV_ARGS(&factory))) ||
        FAILED(factory->EnumAdapterByLuid(luid, IID_PPV_ARGS(&adapter))))
    {
        Log("Vulkan interop could not resolve DXGI adapter LUID %08X:%08X; using the default adapter",
            static_cast<unsigned int>(luid.HighPart), luid.LowPart);
        adapter.Reset();
    }
    return adapter;
}

static bool InitializeVulkanTransport(reshade::api::command_queue *queue)
{
    if (!queue || queue->get_device()->get_api() != reshade::api::device_api::vulkan) return false;
    reshade::api::device *device = queue->get_device();
    if (g_command_queue && g_present_api == reshade::api::device_api::vulkan &&
        g_vulkan.ok && g_vulkan_reshade_device == device && g_vulkan_semaphore != VK_NULL_HANDLE)
    {
        g_rs_queue = queue;
        return true;
    }
    if (g_command_queue || g_vulkan_reshade_device)
    {
        Log("Vulkan device changed after transport initialization; refusing to reuse stale external objects");
        return false;
    }

    if (!FeedVkLoad(&g_vulkan, FeedVkDispatch<VkDevice>(device->get_native())))
    {
        Log("Vulkan transport missing external-memory/semaphore entry points; vkCreateDevice hook calls=%d",
            g_vk_hook_devices);
        Log("Vulkan fallback: use the bundled VK_LAYER_feed_vk if the in-process device hook did not add the required extensions");
        return false;
    }

    const auto adapter = FindAdapterByLuid(device);
    Microsoft::WRL::ComPtr<ID3D12Device> device12;
    HRESULT hr = D3D12CreateDevice(adapter.Get(), D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&device12));
    D3D12_COMMAND_QUEUE_DESC queue_desc = {};
    queue_desc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
    if (SUCCEEDED(hr)) hr = device12->CreateCommandQueue(&queue_desc, IID_PPV_ARGS(&g_command_queue));
    HANDLE shared_fence = nullptr;
    if (SUCCEEDED(hr)) hr = device12->CreateFence(0, D3D12_FENCE_FLAG_SHARED, IID_PPV_ARGS(&g_legacy_fence12));
    if (SUCCEEDED(hr)) hr = device12->CreateSharedHandle(g_legacy_fence12.Get(), nullptr, GENERIC_ALL, nullptr, &shared_fence);
    if (FAILED(hr))
    {
        if (shared_fence) CloseHandle(shared_fence);
        Log("Vulkan private D3D12 session failed: 0x%08X", static_cast<unsigned int>(hr));
        return false;
    }

    g_vulkan_semaphore = FeedVkImportFence(&g_vulkan, shared_fence);
    CloseHandle(shared_fence);
    if (g_vulkan_semaphore == VK_NULL_HANDLE)
    {
        Log("Vulkan failed to import the shared D3D12 fence as a timeline semaphore");
        return false;
    }
    g_vulkan_fence = {FeedVkValue(g_vulkan_semaphore)};
    g_vulkan_reshade_device = device;
    g_rs_queue = queue;
    g_present_api = reshade::api::device_api::vulkan;
    Log("Vulkan interop session ready: VkDevice=%p D3D12 queue=%p shared timeline=%p",
        reinterpret_cast<void *>(device->get_native()), g_command_queue,
        reinterpret_cast<void *>(FeedVkValue(g_vulkan_semaphore)));
    return true;
}

static DXGI_FORMAT VulkanSharedFormat(DXGI_FORMAT format)
{
    format = TypedInputFormat(format);
    // Vulkan commonly exposes BGRX swapchains as the BGRA compatibility class.
    // Use an explicit alpha-bearing D3D12 resource so the external format has a
    // direct VkFormat mapping while preserving the same 32-bit texel layout.
    if (format == DXGI_FORMAT_B8G8R8X8_UNORM) return DXGI_FORMAT_B8G8R8A8_UNORM;
    return format;
}

static bool CreateSharedPairVk(UINT width, UINT height, DXGI_FORMAT format,
    Microsoft::WRL::ComPtr<ID3D12Resource> &resource12, VkImage &image, VkDeviceMemory &memory)
{
    if (!g_neural_device || !g_vulkan.ok) return false;
    format = VulkanSharedFormat(format);
    const VkFormat vk_format = FeedVkFormat(format);
    if (vk_format == VK_FORMAT_UNDEFINED)
    {
        Log("Vulkan interop has no shared format mapping for %u", static_cast<unsigned int>(format));
        return false;
    }
    D3D12_HEAP_PROPERTIES heap = {};
    heap.Type = D3D12_HEAP_TYPE_DEFAULT;
    D3D12_RESOURCE_DESC desc = {};
    desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    desc.Width = width; desc.Height = height; desc.DepthOrArraySize = 1; desc.MipLevels = 1;
    desc.Format = format; desc.SampleDesc.Count = 1; desc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
    desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_SIMULTANEOUS_ACCESS;
    HRESULT hr = g_neural_device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_SHARED, &desc,
        D3D12_RESOURCE_STATE_COMMON, nullptr, IID_PPV_ARGS(&resource12));
    HANDLE shared = nullptr;
    if (SUCCEEDED(hr)) hr = g_neural_device->CreateSharedHandle(resource12.Get(), nullptr, GENERIC_ALL, nullptr, &shared);
    const bool imported = SUCCEEDED(hr) && FeedVkImportImage(&g_vulkan, shared, width, height,
        vk_format, false, &image, &memory);
    if (shared) CloseHandle(shared);
    if (!imported)
    {
        Log("Vulkan shared image import failed: %ux%u dxgi=%u vk=%u hr=0x%08X",
            width, height, static_cast<unsigned int>(format), static_cast<unsigned int>(vk_format),
            static_cast<unsigned int>(hr));
        resource12.Reset();
        return false;
    }
    return true;
}

static bool BuildVulkanFrameResources(UINT width, UINT height, DXGI_FORMAT format)
{
    ReleaseLegacyFrameResources();
    g_packed_color.Reset();
    g_legacy_post12.Reset();
    const DXGI_FORMAT shared_format = VulkanSharedFormat(format);
    if (!CreateSharedPairVk(width, height, shared_format, g_packed_color,
            g_vulkan_input, g_vulkan_input_memory) ||
        !CreateSharedPairVk(width, height, shared_format, g_legacy_post12,
            g_vulkan_post, g_vulkan_post_memory))
    {
        ReleaseLegacyFrameResources();
        g_packed_color.Reset(); g_legacy_post12.Reset();
        return false;
    }
    g_legacy_width = width; g_legacy_height = height; g_legacy_format = shared_format;
    Log("Vulkan shared frame resources ready: %ux%u game fmt=%u shared fmt=%u",
        width, height, static_cast<unsigned int>(format), static_cast<unsigned int>(shared_format));
    return true;
}

static bool RecordVulkanFrameCopy(reshade::api::command_queue *queue,
    reshade::api::resource source, reshade::api::resource_usage source_usage, bool post_effects,
    reshade::api::command_list *provided_list = nullptr)
{
    if (!queue || !g_vulkan.ok || source.handle == 0 || !g_command_queue || !g_legacy_fence12)
        return false;
    VkImage destination = post_effects ? g_vulkan_post : g_vulkan_input;
    bool &layout_initialized = post_effects ? g_vulkan_post_layout_initialized : g_vulkan_input_layout_initialized;
    if (destination == VK_NULL_HANDLE) return false;

    if (!g_vulkan_release_wait_queued && g_legacy_d3d12_done_value != 0 &&
        !queue->wait(g_vulkan_fence, g_legacy_d3d12_done_value))
    {
        Log("Vulkan queue failed to wait for D3D12 shared image release value %llu", g_legacy_d3d12_done_value);
        return false;
    }
    g_vulkan_release_wait_queued = true;
    reshade::api::command_list *list = provided_list ? provided_list : queue->get_immediate_command_list();
    if (!list) return false;
    VkCommandBuffer command_buffer = FeedVkDispatch<VkCommandBuffer>(list->get_native());
    VkImage source_image = FeedVkHandle<VkImage>(source.handle);
    if (!layout_initialized)
    {
        FeedVkBarrier(&g_vulkan, command_buffer, destination,
            VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL);
        layout_initialized = true;
    }
    const reshade::api::resource resources[1] = {source};
    const reshade::api::resource_usage copy_source[1] = {reshade::api::resource_usage::copy_source};
    list->barrier(1, resources, &source_usage, copy_source);
    FeedVkCopyImage(&g_vulkan, command_buffer, source_image,
        VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, destination, VK_IMAGE_LAYOUT_GENERAL,
        g_legacy_width, g_legacy_height);
    list->barrier(1, resources, copy_source, &source_usage);

    if (post_effects) g_vulkan_post_copy_recorded = true;
    else g_vulkan_input_copy_recorded = true;
    return true;
}

static bool CompleteVulkanFrameCopies(reshade::api::command_queue *queue)
{
    if (!queue || !g_vulkan_input_copy_recorded || !g_command_queue || !g_legacy_fence12)
        return false;
    const UINT64 value = ++g_legacy_fence_value;
    if (!queue->signal(g_vulkan_fence, value) || FAILED(g_command_queue->Wait(g_legacy_fence12.Get(), value)))
    {
        Log("Vulkan -> D3D12 deferred present handoff failed at value %llu", value);
        return false;
    }
    return true;
}

static bool CreateSharedPair11(UINT width, UINT height, DXGI_FORMAT format,
    Microsoft::WRL::ComPtr<ID3D12Resource> &resource12,
    Microsoft::WRL::ComPtr<ID3D11Texture2D> &resource11)
{
    if (!g_neural_device || !g_legacy_device11) return false;
    D3D12_HEAP_PROPERTIES heap = {};
    heap.Type = D3D12_HEAP_TYPE_DEFAULT;
    D3D12_RESOURCE_DESC desc = {};
    desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    desc.Width = width; desc.Height = height; desc.DepthOrArraySize = 1; desc.MipLevels = 1;
    desc.Format = format; desc.SampleDesc.Count = 1; desc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
    desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_SIMULTANEOUS_ACCESS;
    HRESULT hr = g_neural_device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_SHARED, &desc,
        D3D12_RESOURCE_STATE_COMMON, nullptr, IID_PPV_ARGS(&resource12));
    HANDLE shared = nullptr;
    if (SUCCEEDED(hr)) hr = g_neural_device->CreateSharedHandle(resource12.Get(), nullptr, GENERIC_ALL, nullptr, &shared);
    Microsoft::WRL::ComPtr<ID3D11Device1> device1;
    if (SUCCEEDED(hr)) hr = g_legacy_device11.As(&device1);
    if (SUCCEEDED(hr)) hr = device1->OpenSharedResource1(shared, IID_PPV_ARGS(&resource11));
    if (shared) CloseHandle(shared);
    if (SUCCEEDED(hr)) return true;

    Log("legacy D3D12->D3D11 texture failed: 0x%08X; trying D3D11->D3D12",
        static_cast<unsigned int>(hr));
    resource11.Reset(); resource12.Reset();
    D3D11_TEXTURE2D_DESC desc11 = {};
    desc11.Width = width; desc11.Height = height; desc11.MipLevels = 1; desc11.ArraySize = 1;
    desc11.Format = format; desc11.SampleDesc.Count = 1; desc11.Usage = D3D11_USAGE_DEFAULT;
    desc11.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET;
    desc11.MiscFlags = D3D11_RESOURCE_MISC_SHARED_NTHANDLE | D3D11_RESOURCE_MISC_SHARED;
    hr = g_legacy_device11->CreateTexture2D(&desc11, nullptr, &resource11);
    shared = nullptr;
    Microsoft::WRL::ComPtr<IDXGIResource1> dxgi_resource;
    if (SUCCEEDED(hr)) hr = resource11.As(&dxgi_resource);
    if (SUCCEEDED(hr)) hr = dxgi_resource->CreateSharedHandle(nullptr,
        DXGI_SHARED_RESOURCE_READ | DXGI_SHARED_RESOURCE_WRITE, nullptr, &shared);
    if (SUCCEEDED(hr)) hr = g_neural_device->OpenSharedHandle(shared, IID_PPV_ARGS(&resource12));
    if (shared) CloseHandle(shared);
    if (FAILED(hr))
    {
        Log("legacy shared texture failed in both directions: %ux%u fmt=%u hr=0x%08X",
            width, height, static_cast<unsigned int>(format), static_cast<unsigned int>(hr));
        resource11.Reset(); resource12.Reset();
        return false;
    }
    Log("legacy shared texture using D3D11->D3D12 fallback: %ux%u fmt=%u",
        width, height, static_cast<unsigned int>(format));
    return true;
}

static Microsoft::WRL::ComPtr<IDXGIAdapter1> FindD3D9Adapter(IDirect3DDevice9 *device9)
{
    Microsoft::WRL::ComPtr<IDXGIAdapter1> match;
    if (!device9) return match;
    D3DDEVICE_CREATION_PARAMETERS creation = {};
    if (FAILED(device9->GetCreationParameters(&creation))) return match;
    Microsoft::WRL::ComPtr<IDirect3D9> d3d9;
    if (FAILED(device9->GetDirect3D(&d3d9))) return match;
    LUID wanted = {};
    bool have_luid = false;
    Microsoft::WRL::ComPtr<IDirect3D9Ex> d3d9ex;
    if (SUCCEEDED(d3d9.As(&d3d9ex)) && SUCCEEDED(d3d9ex->GetAdapterLUID(creation.AdapterOrdinal, &wanted)))
        have_luid = true;
    const HMONITOR wanted_monitor = d3d9->GetAdapterMonitor(creation.AdapterOrdinal);
    Microsoft::WRL::ComPtr<IDXGIFactory1> factory;
    if (FAILED(CreateDXGIFactory1(IID_PPV_ARGS(&factory)))) return match;
    for (UINT index = 0; ; ++index)
    {
        Microsoft::WRL::ComPtr<IDXGIAdapter1> adapter;
        if (factory->EnumAdapters1(index, &adapter) == DXGI_ERROR_NOT_FOUND) break;
        DXGI_ADAPTER_DESC1 adapter_desc = {};
        adapter->GetDesc1(&adapter_desc);
        if (have_luid && adapter_desc.AdapterLuid.HighPart == wanted.HighPart &&
            adapter_desc.AdapterLuid.LowPart == wanted.LowPart) return adapter;
        for (UINT output_index = 0; wanted_monitor != nullptr; ++output_index)
        {
            Microsoft::WRL::ComPtr<IDXGIOutput> output;
            if (adapter->EnumOutputs(output_index, &output) == DXGI_ERROR_NOT_FOUND) break;
            DXGI_OUTPUT_DESC output_desc = {};
            if (SUCCEEDED(output->GetDesc(&output_desc)) && output_desc.Monitor == wanted_monitor)
                return adapter;
        }
    }
    return match;
}

static bool InitializeLegacyTransport(reshade::api::device *reshade_device)
{
    if (!reshade_device) return false;
    const reshade::api::device_api api = reshade_device->get_api();
    if (api != reshade::api::device_api::d3d11 && api != reshade::api::device_api::d3d9) return false;
    if (g_command_queue && g_present_api == api && g_legacy_context4 && g_legacy_fence11 && g_legacy_fence12)
        return true;

    Microsoft::WRL::ComPtr<IDXGIAdapter1> adapter;
    if (api == reshade::api::device_api::d3d11)
    {
        auto *native = reinterpret_cast<ID3D11Device *>(reshade_device->get_native());
        if (!native) return false;
        g_legacy_device11 = native;
        native->GetImmediateContext(&g_legacy_context11);
        Microsoft::WRL::ComPtr<IDXGIDevice> dxgi_device;
        Microsoft::WRL::ComPtr<IDXGIAdapter> adapter0;
        if (FAILED(native->QueryInterface(IID_PPV_ARGS(&dxgi_device))) ||
            FAILED(dxgi_device->GetAdapter(&adapter0)) || FAILED(adapter0.As(&adapter))) return false;
    }
    else
    {
        auto *native9 = reinterpret_cast<IDirect3DDevice9 *>(reshade_device->get_native());
        if (!native9) return false;
        g_legacy_device9 = native9;
        adapter = FindD3D9Adapter(native9);
        if (!adapter) { Log("D3D9 interop could not match the game adapter to DXGI"); return false; }
        D3D_FEATURE_LEVEL feature_level = D3D_FEATURE_LEVEL_11_0;
        const UINT flags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;
        HRESULT hr = D3D11CreateDevice(adapter.Get(), D3D_DRIVER_TYPE_UNKNOWN, nullptr, flags,
            &feature_level, 1, D3D11_SDK_VERSION, &g_legacy_device11, nullptr, &g_legacy_context11);
        if (FAILED(hr)) { Log("D3D9 interop D3D11CreateDevice failed: 0x%08X", static_cast<unsigned int>(hr)); return false; }
        native9->CreateQuery(D3DQUERYTYPE_EVENT, &g_legacy_query9);
    }

    Microsoft::WRL::ComPtr<ID3D12Device> device12;
    HRESULT hr = D3D12CreateDevice(adapter.Get(), D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&device12));
    D3D12_COMMAND_QUEUE_DESC queue_desc = {};
    queue_desc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
    if (SUCCEEDED(hr)) hr = device12->CreateCommandQueue(&queue_desc, IID_PPV_ARGS(&g_command_queue));
    if (SUCCEEDED(hr)) hr = g_legacy_context11.As(&g_legacy_context4);
    Microsoft::WRL::ComPtr<ID3D11Device5> device5;
    if (SUCCEEDED(hr)) hr = g_legacy_device11.As(&device5);
    HANDLE shared_fence = nullptr;
    if (SUCCEEDED(hr)) hr = device12->CreateFence(0, D3D12_FENCE_FLAG_SHARED, IID_PPV_ARGS(&g_legacy_fence12));
    if (SUCCEEDED(hr)) hr = device12->CreateSharedHandle(g_legacy_fence12.Get(), nullptr, GENERIC_ALL, nullptr, &shared_fence);
    if (SUCCEEDED(hr)) hr = device5->OpenSharedFence(shared_fence, IID_PPV_ARGS(&g_legacy_fence11));
    if (shared_fence) CloseHandle(shared_fence);
    if (FAILED(hr))
    {
        Log("legacy D3D11/D3D12 session initialization failed: 0x%08X", static_cast<unsigned int>(hr));
        return false;
    }
    g_present_api = api;
    Log("legacy interop session ready: api=%s D3D11=%p D3D12 queue=%p shared fence=%p",
        api == reshade::api::device_api::d3d11 ? "D3D11" : "D3D9",
        g_legacy_device11.Get(), g_command_queue, g_legacy_fence12.Get());
    return true;
}

static D3DFORMAT ToD3D9Format(DXGI_FORMAT format)
{
    switch (format)
    {
    case DXGI_FORMAT_B8G8R8A8_UNORM: return D3DFMT_A8R8G8B8;
    case DXGI_FORMAT_B8G8R8X8_UNORM: return D3DFMT_X8R8G8B8;
    case DXGI_FORMAT_R8G8B8A8_UNORM: return D3DFMT_A8B8G8R8;
    case DXGI_FORMAT_R10G10B10A2_UNORM: return D3DFMT_A2B10G10R10;
    case DXGI_FORMAT_R16G16B16A16_FLOAT: return D3DFMT_A16B16G16R16F;
    default: return D3DFMT_UNKNOWN;
    }
}

static bool CreateD3D9SharedStage(UINT width, UINT height, DXGI_FORMAT format,
    Microsoft::WRL::ComPtr<IDirect3DTexture9> &texture9,
    Microsoft::WRL::ComPtr<ID3D11Texture2D> &texture11)
{
    if (!g_legacy_device9 || !g_legacy_device11) return false;
    const D3DFORMAT format9 = ToD3D9Format(format);
    if (format9 == D3DFMT_UNKNOWN)
    {
        Log("D3D9 interop does not support backbuffer format %u", static_cast<unsigned int>(format));
        return false;
    }
    D3D11_TEXTURE2D_DESC desc = {};
    desc.Width = width; desc.Height = height; desc.MipLevels = 1; desc.ArraySize = 1;
    desc.Format = format; desc.SampleDesc.Count = 1; desc.Usage = D3D11_USAGE_DEFAULT;
    desc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET;
    desc.MiscFlags = D3D11_RESOURCE_MISC_SHARED;
    HRESULT hr = g_legacy_device11->CreateTexture2D(&desc, nullptr, &texture11);
    HANDLE shared = nullptr;
    Microsoft::WRL::ComPtr<IDXGIResource> dxgi_resource;
    if (SUCCEEDED(hr)) hr = texture11.As(&dxgi_resource);
    if (SUCCEEDED(hr)) hr = dxgi_resource->GetSharedHandle(&shared);
    if (SUCCEEDED(hr))
        hr = g_legacy_device9->CreateTexture(width, height, 1, D3DUSAGE_RENDERTARGET,
            format9, D3DPOOL_DEFAULT, &texture9, &shared);
    if (FAILED(hr))
    {
        texture9.Reset(); texture11.Reset();
        shared = nullptr;
        hr = g_legacy_device9->CreateTexture(width, height, 1, D3DUSAGE_RENDERTARGET,
            format9, D3DPOOL_DEFAULT, &texture9, &shared);
        if (SUCCEEDED(hr)) hr = g_legacy_device11->OpenSharedResource(shared, IID_PPV_ARGS(&texture11));
    }
    if (FAILED(hr))
    {
        Log("D3D9/D3D11 shared stage creation failed: %ux%u fmt9=%u dxgi=%u hr=0x%08X",
            width, height, static_cast<unsigned int>(format9), static_cast<unsigned int>(format),
            static_cast<unsigned int>(hr));
        texture9.Reset(); texture11.Reset();
        return false;
    }
    return true;
}

static bool BuildLegacyFrameResources(UINT width, UINT height, DXGI_FORMAT format)
{
    ReleaseLegacyFrameResources();
    g_packed_color.Reset();
    if (!CreateSharedPair11(width, height, format, g_packed_color, g_legacy_input11) ||
        !CreateSharedPair11(width, height, format, g_legacy_post12, g_legacy_post11))
        return false;
    if (g_present_api == reshade::api::device_api::d3d9)
    {
        if (!CreateD3D9SharedStage(width, height, format, g_legacy_input9, g_legacy_input9_11) ||
            !CreateD3D9SharedStage(width, height, format, g_legacy_post9, g_legacy_post9_11))
            return false;
    }
    g_legacy_width = width; g_legacy_height = height; g_legacy_format = format;
    Log("legacy shared frame resources ready: api=%s %ux%u fmt=%u",
        g_present_api == reshade::api::device_api::d3d11 ? "D3D11" : "D3D9",
        width, height, static_cast<unsigned int>(format));
    return true;
}

static bool WaitForD3D9Copy()
{
    if (!g_legacy_query9 && g_legacy_device9)
        g_legacy_device9->CreateQuery(D3DQUERYTYPE_EVENT, &g_legacy_query9);
    if (!g_legacy_query9 || FAILED(g_legacy_query9->Issue(D3DISSUE_END))) return false;
    const ULONGLONG deadline = GetTickCount64() + 3000;
    HRESULT hr = S_FALSE;
    while ((hr = g_legacy_query9->GetData(nullptr, 0, D3DGETDATA_FLUSH)) == S_FALSE && GetTickCount64() < deadline)
        SwitchToThread();
    return hr == S_OK;
}

static bool CopyLegacyFrameToD3D12(void *native_resource, bool post_effects)
{
    if (!native_resource || !g_legacy_context11 || !g_legacy_context4 || !g_legacy_fence11 ||
        !g_legacy_fence12 || !g_command_queue) return false;
    ID3D11Texture2D *destination11 = post_effects ? g_legacy_post11.Get() : g_legacy_input11.Get();
    if (!destination11) return false;
    if (g_legacy_d3d12_done_value != 0 &&
        FAILED(g_legacy_context4->Wait(g_legacy_fence11.Get(), g_legacy_d3d12_done_value)))
        return false;
    if (g_present_api == reshade::api::device_api::d3d11)
    {
        Microsoft::WRL::ComPtr<ID3D11Texture2D> source11;
        if (FAILED(reinterpret_cast<IUnknown *>(native_resource)->QueryInterface(IID_PPV_ARGS(&source11)))) return false;
        g_legacy_context11->CopyResource(destination11, source11.Get());
    }
    else if (g_present_api == reshade::api::device_api::d3d9)
    {
        auto *source9 = reinterpret_cast<IDirect3DSurface9 *>(native_resource);
        IDirect3DTexture9 *stage9 = post_effects ? g_legacy_post9.Get() : g_legacy_input9.Get();
        ID3D11Texture2D *stage11 = post_effects ? g_legacy_post9_11.Get() : g_legacy_input9_11.Get();
        if (!source9 || !stage9 || !stage11) return false;
        Microsoft::WRL::ComPtr<IDirect3DSurface9> destination9;
        if (FAILED(stage9->GetSurfaceLevel(0, &destination9)) ||
            FAILED(g_legacy_device9->StretchRect(source9, nullptr, destination9.Get(), nullptr, D3DTEXF_NONE)) ||
            !WaitForD3D9Copy()) return false;
        g_legacy_context11->CopyResource(destination11, stage11);
    }
    else return false;

    const UINT64 value = ++g_legacy_fence_value;
    if (FAILED(g_legacy_context4->Signal(g_legacy_fence11.Get(), value))) return false;
    g_legacy_context11->Flush();
    if (FAILED(g_command_queue->Wait(g_legacy_fence12.Get(), value))) return false;
    return true;
}

static bool EnsureStandaloneResources(UINT iw, UINT ih, DXGI_FORMAT input_format)
{
    if (g_neural_failed || !g_command_queue) return false;
    const UINT ow = g_output_width.load(), oh = g_output_height.load();
    input_format = TypedInputFormat(input_format);
    if (iw == 0 || ih == 0 || ow == 0 || oh == 0) return false;
    if (g_neural_ready && (iw != g_resource_input_width || ih != g_resource_input_height ||
        ow != g_resource_output_width || oh != g_resource_output_height || input_format != g_resource_input_format))
    {
        if (!RetireResolutionDependentResources(iw, ih, input_format)) return false;
    }
    if (iw > ow || ih > oh)
    {
        if (g_proxy_window) { g_proxy_hidden = true; ShowWindow(g_proxy_window, SW_HIDE); }
        SetStatus("render resolution exceeds native output");
        return false;
    }
    if (iw == ow && ih == oh)
    {
        if (g_proxy_window) { g_proxy_hidden = true; ShowWindow(g_proxy_window, SW_HIDE); }
        SetStatus("waiting for a reduced fullscreen render resolution");
        return false;
    }
    if (g_neural_ready) return true;

    if (!g_neural_device)
    {
        HRESULT hr = g_command_queue->GetDevice(IID_PPV_ARGS(&g_neural_device));
        if (SUCCEEDED(hr)) hr = g_neural_device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&g_neural_allocator));
        if (SUCCEEDED(hr)) hr = g_neural_device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT,
            g_neural_allocator.Get(), nullptr, IID_PPV_ARGS(&g_neural_list));
        if (SUCCEEDED(hr)) hr = g_neural_list->Close();
        if (SUCCEEDED(hr)) hr = g_neural_device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&g_neural_fence));
        D3D12_DESCRIPTOR_HEAP_DESC guide_heap_desc = {};
        guide_heap_desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
        guide_heap_desc.NumDescriptors = 2;
        if (SUCCEEDED(hr)) hr = g_neural_device->CreateDescriptorHeap(&guide_heap_desc, IID_PPV_ARGS(&g_guide_rtv_heap));
        if (FAILED(hr)) { Fail("D3D12 neural command infrastructure", static_cast<unsigned int>(hr)); return false; }
        g_guide_rtv_stride = g_neural_device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
        g_neural_fence_event = CreateEventW(nullptr, FALSE, FALSE, nullptr);
        if (!g_neural_fence_event) { Fail("neural fence event", GetLastError()); return false; }
    }

    g_resource_input_width = iw; g_resource_input_height = ih;
    g_resource_output_width = ow; g_resource_output_height = oh;
    g_resource_input_format = input_format;
    const DXGI_FORMAT result_format = g_active_color_profile == ColorProfile::Srgb ?
        DXGI_FORMAT_R8G8B8A8_UNORM : DXGI_FORMAT_R16G16B16A16_FLOAT;
    const bool legacy = g_present_api == reshade::api::device_api::d3d11 ||
        g_present_api == reshade::api::device_api::d3d9 ||
        g_present_api == reshade::api::device_api::vulkan;
    const bool input_ready = g_present_api == reshade::api::device_api::vulkan ?
        BuildVulkanFrameResources(iw, ih, input_format) :
        (legacy ? BuildLegacyFrameResources(iw, ih, input_format) :
            CreateTexture(iw, ih, input_format, false,
                D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, g_packed_color));
    if (!input_ready ||
        !CreateTexture(iw, ih, result_format, true, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, g_nr_stage) ||
        !CreateTexture(ow, oh, result_format, true, D3D12_RESOURCE_STATE_COMMON, g_sr_stage) ||
        !CreateTexture(ow, oh, result_format, true, D3D12_RESOURCE_STATE_COMMON, g_fg_stage)) return false;
    const float motion_clear[4] = {0.0f, 0.0f, 0.0f, 0.0f};
    const float depth_clear[4] = {g_depth_reversed ? 0.0f : 1.0f, 0.0f, 0.0f, 0.0f};
    if (!CreateGuideTexture(iw, ih, DXGI_FORMAT_R16G16_FLOAT, motion_clear, 0, g_fallback_motion) ||
        !CreateGuideTexture(iw, ih, DXGI_FORMAT_R32_FLOAT, depth_clear, 1, g_fallback_depth)) return false;
    if (!InitializeNgx() || !CreateFeatures()) return false;
    PublishOutput(g_sr_stage.Get());
    g_neural_ready = true;
    if (g_proxy_window)
    {
        g_proxy_hidden = false;
        ShowWindow(g_proxy_window, SW_SHOWNOACTIVATE);
    }
    SetStatus("active on present: NR + DLSS SR (fallback guides)");
    Log("resources ready on present: compact packed/NR=%ux%u, SR=%ux%u, input fmt=%u result fmt=%u; fallback guides=%ux%u",
        iw, ih, ow, oh, static_cast<unsigned int>(input_format), static_cast<unsigned int>(result_format), iw, ih);
    Log("resolution configuration active without restart: input=%ux%u output=%ux%u", iw, ih, ow, oh);
    return true;
}

static void SetNrEvaluationContract(ID3D12Resource *depth, ID3D12Resource *motion, bool reset)
{
    const UINT iw = g_resource_input_width, ih = g_resource_input_height;
    for (const char *name : {"Color", "DLSSNR.Color"}) g_ngx_params->Set(name, g_packed_color.Get());
    for (const char *name : {"Output", "DLSSNR.Output"}) g_ngx_params->Set(name, g_nr_stage.Get());
    for (const char *name : {"Depth", "DLSSNR.Depth"}) g_ngx_params->Set(name, depth);
    g_ngx_params->Set("MotionVectors", motion); g_ngx_params->Set("DLSSNR.MVec", motion);
    g_ngx_params->Set("Reset", reset ? 1 : 0); g_ngx_params->Set("DLSSNR.Reset", reset ? 1 : 0);
    g_ngx_params->Set("Jitter.Offset.X", 0.0f); g_ngx_params->Set("Jitter.Offset.Y", 0.0f);
    g_ngx_params->Set("MV.Scale.X", 1.0f); g_ngx_params->Set("MV.Scale.Y", 1.0f);
    g_ngx_params->Set("DLSSNR.JitterOffsetX", 0.0f); g_ngx_params->Set("DLSSNR.JitterOffsetY", 0.0f);
    g_ngx_params->Set("DLSSNR.MVecScaleX", 1.0f); g_ngx_params->Set("DLSSNR.MVecScaleY", 1.0f);
    g_ngx_params->Set("DLSS.Pre.Exposure", 1.0f); g_ngx_params->Set("DLSS.Exposure.Scale", 1.0f);
    g_ngx_params->Set("DLSS.Render.Subrect.Dimensions.Width", iw); g_ngx_params->Set("DLSS.Render.Subrect.Dimensions.Height", ih);
    g_ngx_params->Set("DLSS.Input.Color.Subrect.Base.X", 0u); g_ngx_params->Set("DLSS.Input.Color.Subrect.Base.Y", 0u);
    g_ngx_params->Set("DLSS.Input.Depth.Subrect.Base.X", 0u); g_ngx_params->Set("DLSS.Input.Depth.Subrect.Base.Y", 0u);
    g_ngx_params->Set("DLSS.Input.MV.Subrect.Base.X", 0u); g_ngx_params->Set("DLSS.Input.MV.Subrect.Base.Y", 0u);
    g_ngx_params->Set("DLSS.Output.Subrect.Base.X", 0u); g_ngx_params->Set("DLSS.Output.Subrect.Base.Y", 0u);
    g_ngx_params->Set("DLSSNR.ColorSubrectBaseX", 0); g_ngx_params->Set("DLSSNR.ColorSubrectBaseY", 0);
    g_ngx_params->Set("DLSSNR.ColorSubrectWidth", static_cast<int>(iw)); g_ngx_params->Set("DLSSNR.ColorSubrectHeight", static_cast<int>(ih));
    g_ngx_params->Set("DLSSNR.MVecSubrectBaseX", 0); g_ngx_params->Set("DLSSNR.MVecSubrectBaseY", 0);
    g_ngx_params->Set("DLSSNR.MVecSubrectWidth", static_cast<int>(iw)); g_ngx_params->Set("DLSSNR.MVecSubrectHeight", static_cast<int>(ih));
    g_ngx_params->Set("DLSSNR.DepthSubrectBaseX", 0); g_ngx_params->Set("DLSSNR.DepthSubrectBaseY", 0);
    g_ngx_params->Set("DLSSNR.DepthSubrectWidth", static_cast<int>(iw)); g_ngx_params->Set("DLSSNR.DepthSubrectHeight", static_cast<int>(ih));
    g_ngx_params->Set("DLSSNR.OutputSubrectBaseX", 0); g_ngx_params->Set("DLSSNR.OutputSubrectBaseY", 0);
    g_ngx_params->Set("DLSSNR.OutputSubrectWidth", static_cast<int>(iw)); g_ngx_params->Set("DLSSNR.OutputSubrectHeight", static_cast<int>(ih));
    g_ngx_params->Set("DLSSNR.DepthInverted", g_depth_reversed ? 1u : 0u);
    g_ngx_params->Set("DLSSNR.InputWidth", iw); g_ngx_params->Set("DLSSNR.InputHeight", ih);
    g_ngx_params->Set("DLSSNR.Width", iw); g_ngx_params->Set("DLSSNR.Height", ih);
    g_ngx_params->Set("DLSSNR.OutputWidth", iw); g_ngx_params->Set("DLSSNR.OutputHeight", ih);
    g_ngx_params->Set("DLSSNR.Upscaling", 1u);
    g_ngx_params->Set("DLSSNR.ScalingRatio", g_nr_scaling_ratio); g_ngx_params->Set("DLSSNR.Scale", g_nr_scaling_ratio);
    g_ngx_params->Set("DLSSNR.Hint.Render.Preset", g_nr_model);
    g_ngx_params->Set("DLSSNR.Intensity", g_nr_intensity);
    g_ngx_params->Set("DLSSNR.LocalToneStrength", g_nr_local_tone);
    g_ngx_params->Set("DLSSNR.LocalStructureStrength", g_nr_local_structure);
    g_ngx_params->Set("DLSSNR.SkinStructureStrength", g_nr_skin_structure);
    g_ngx_params->Set("DLSSNR.Enabled", 1u);
    g_ngx_params->Set("DLSSNR.UseAutoMask", 1u);
    g_ngx_params->Set("DLSSNR.UICorrection", 0u);
}

static void SetSrEvaluationContract(ID3D12Resource *color, ID3D12Resource *depth, ID3D12Resource *motion,
    ID3D12Resource *history_mask, bool reset)
{
    g_ngx_params->Set("Color", color); g_ngx_params->Set("Output", g_sr_stage.Get());
    g_ngx_params->Set("Depth", depth); g_ngx_params->Set("MotionVectors", motion);
    g_ngx_params->Set("Reset", reset ? 1 : 0);
    g_ngx_params->Set("Jitter.Offset.X", 0.0f); g_ngx_params->Set("Jitter.Offset.Y", 0.0f);
    g_ngx_params->Set("Sharpness", 0.0f); g_ngx_params->Set("MV.Scale.X", 1.0f); g_ngx_params->Set("MV.Scale.Y", 1.0f);
    g_ngx_params->Set("DLSS.Render.Subrect.Dimensions.Width", g_resource_input_width);
    g_ngx_params->Set("DLSS.Render.Subrect.Dimensions.Height", g_resource_input_height);
    g_ngx_params->Set("DLSS.Input.Color.Subrect.Base.X", 0u); g_ngx_params->Set("DLSS.Input.Color.Subrect.Base.Y", 0u);
    g_ngx_params->Set("DLSS.Input.Depth.Subrect.Base.X", 0u); g_ngx_params->Set("DLSS.Input.Depth.Subrect.Base.Y", 0u);
    g_ngx_params->Set("DLSS.Input.MV.Subrect.Base.X", 0u); g_ngx_params->Set("DLSS.Input.MV.Subrect.Base.Y", 0u);
    g_ngx_params->Set("DLSS.Input.Bias.Current.Color.Mask", history_mask);
    g_ngx_params->Set("DLSS.Input.Reduce.Ghost.Mask", history_mask);
    g_ngx_params->Set("DLSS.DisocclusionMask", history_mask);
    g_ngx_params->Set("DLSS.Input.Bias.Current.Color.Subrect.Base.X", 0u);
    g_ngx_params->Set("DLSS.Input.Bias.Current.Color.Subrect.Base.Y", 0u);
    g_ngx_params->Set("DLSS.Input.Reduce.Ghost.Subrect.Base.X", 0u);
    g_ngx_params->Set("DLSS.Input.Reduce.Ghost.Subrect.Base.Y", 0u);
    g_ngx_params->Set("DLSS.DisocclusionMask.Subrect.Base.X", 0u);
    g_ngx_params->Set("DLSS.DisocclusionMask.Subrect.Base.Y", 0u);
    g_ngx_params->Set("DLSS.Output.Subrect.Base.X", 0u); g_ngx_params->Set("DLSS.Output.Subrect.Base.Y", 0u);
    g_ngx_params->Set("DLSS.Pre.Exposure", 1.0f); g_ngx_params->Set("DLSS.Exposure.Scale", 1.0f);
    g_ngx_params->Set("DLSS.Indicator.Invert.X.Axis", 0); g_ngx_params->Set("DLSS.Indicator.Invert.Y.Axis", 0);
}

static void SetFgEvaluationContract(ID3D12Resource *depth, ID3D12Resource *motion, bool reset)
{
    static float identity[4][4] = {
        {1, 0, 0, 0}, {0, 1, 0, 0}, {0, 0, 1, 0}, {0, 0, 0, 1}
    };
    const UINT iw = g_resource_input_width, ih = g_resource_input_height;
    const UINT ow = g_resource_output_width, oh = g_resource_output_height;
    g_ngx_params->Set("DLSSG.Backbuffer", g_sr_stage.Get());
    g_ngx_params->Set("DLSSG.MVecs", motion);
    g_ngx_params->Set("DLSSG.Depth", depth);
    g_ngx_params->Set("DLSSG.HUDLess", g_sr_stage.Get());
    g_ngx_params->Set("DLSSG.OutputInterpolated", g_fg_stage.Get());
    g_ngx_params->Set("DLSSG.MultiFrameCount", 1u); g_ngx_params->Set("DLSSG.MultiFrameIndex", 1u);
    for (const char *name : {"DLSSG.CameraViewToClip", "DLSSG.ClipToCameraView",
        "DLSSG.ClipToLensClip", "DLSSG.ClipToPrevClip", "DLSSG.PrevClipToClip"})
        g_ngx_params->Set(name, reinterpret_cast<void *>(identity));
    g_ngx_params->Set("DLSSG.JitterOffsetX", 0.0f); g_ngx_params->Set("DLSSG.JitterOffsetY", 0.0f);
    g_ngx_params->Set("DLSSG.MvecScaleX", 1.0f / iw); g_ngx_params->Set("DLSSG.MvecScaleY", 1.0f / ih);
    g_ngx_params->Set("DLSSG.CameraPinholeOffsetX", 0.0f); g_ngx_params->Set("DLSSG.CameraPinholeOffsetY", 0.0f);
    g_ngx_params->Set("DLSSG.CameraPosX", 0.0f); g_ngx_params->Set("DLSSG.CameraPosY", 0.0f); g_ngx_params->Set("DLSSG.CameraPosZ", 0.0f);
    g_ngx_params->Set("DLSSG.CameraUpX", 0.0f); g_ngx_params->Set("DLSSG.CameraUpY", 1.0f); g_ngx_params->Set("DLSSG.CameraUpZ", 0.0f);
    g_ngx_params->Set("DLSSG.CameraRightX", 1.0f); g_ngx_params->Set("DLSSG.CameraRightY", 0.0f); g_ngx_params->Set("DLSSG.CameraRightZ", 0.0f);
    g_ngx_params->Set("DLSSG.CameraFwdX", 0.0f); g_ngx_params->Set("DLSSG.CameraFwdY", 0.0f); g_ngx_params->Set("DLSSG.CameraFwdZ", 1.0f);
    g_ngx_params->Set("DLSSG.CameraNear", 0.1f); g_ngx_params->Set("DLSSG.CameraFar", 1000.0f);
    g_ngx_params->Set("DLSSG.CameraFOV", 1.04719755f); g_ngx_params->Set("DLSSG.CameraAspectRatio", static_cast<float>(ow) / oh);
    g_ngx_params->Set("DLSSG.ColorBuffersHDR", g_active_color_profile == ColorProfile::Srgb ? 0u : 1u);
    g_ngx_params->Set("DLSSG.DepthInverted", g_depth_reversed ? 1u : 0u);
    g_ngx_params->Set("DLSSG.CameraMotionIncluded", 1u);
    g_ngx_params->Set("DLSSG.Reset", reset ? 1u : 0u);
    g_ngx_params->Set("DLSSG.AutomodeOverrideReset", 0u); g_ngx_params->Set("DLSSG.NotRenderingGameFrames", 0u);
    g_ngx_params->Set("DLSSG.OrthoProjection", 0u); g_ngx_params->Set("DLSSG.MvecInvalidValue", -99999.0f);
    g_ngx_params->Set("DLSSG.MvecDilated", 0u); g_ngx_params->Set("DLSSG.MenuDetectionEnabled", 0u);
    g_ngx_params->Set("DLSSG.BackbufferFrameID", static_cast<unsigned long long>(g_sr_frames.load() + 1));
    for (const char *prefix : {"DLSSG.MVecsSubrect", "DLSSG.DepthSubrect"})
    {
        std::string key = std::string(prefix) + "BaseX"; g_ngx_params->Set(key.c_str(), 0u);
        key = std::string(prefix) + "BaseY"; g_ngx_params->Set(key.c_str(), 0u);
        key = std::string(prefix) + "Width"; g_ngx_params->Set(key.c_str(), iw);
        key = std::string(prefix) + "Height"; g_ngx_params->Set(key.c_str(), ih);
    }
    for (const char *prefix : {"DLSSG.InputBackbufferSubrect", "DLSSG.HUDLessSubrect", "DLSSG.OutputInterpolatedSubrect"})
    {
        std::string key = std::string(prefix) + "BaseX"; g_ngx_params->Set(key.c_str(), 0u);
        key = std::string(prefix) + "BaseY"; g_ngx_params->Set(key.c_str(), 0u);
        key = std::string(prefix) + "Width"; g_ngx_params->Set(key.c_str(), ow);
        key = std::string(prefix) + "Height"; g_ngx_params->Set(key.c_str(), oh);
    }
}

static void ResolveHandles(reshade::api::effect_runtime *runtime)
{
    g_runtime = runtime;
    g_feed_technique = runtime->find_technique("DLSS5_Feed.fx", "DLSS5_Feed");
    g_motion_technique = runtime->find_technique("vort_Motion.fx", "vort_MotionEffects");
    g_mv_variable = runtime->find_texture_variable("DLSS5_Feed.fx", "DLSS5_MV");
    g_depth_variable = runtime->find_texture_variable("DLSS5_Feed.fx", "DLSS5_Depth");
    g_mask_variable = runtime->find_texture_variable("DLSS5_Feed.fx", "DLSS5_Mask");
    char reversed[16] = {};
    g_depth_reversed = !runtime->get_preprocessor_definition("RESHADE_DEPTH_INPUT_IS_REVERSED", reversed) || atoi(reversed) != 0;
    // These are rendered explicitly from OnPresent, in this order, so their
    // current-frame resources exist before NGX evaluation. Leaving either in
    // ReShade's ordinary effect list would render it a second time afterwards.
    if (g_motion_technique.handle && runtime->get_technique_state(g_motion_technique))
        runtime->set_technique_state(g_motion_technique, false);
    if (g_feed_technique.handle && runtime->get_technique_state(g_feed_technique))
        runtime->set_technique_state(g_feed_technique, false);
    Log("current-frame guide handles: VORT=%s feed=%s mv=%s depth=%s mask=%s depth_reversed=%d",
        g_motion_technique.handle ? "found" : "MISSING",
        g_feed_technique.handle ? "found" : "MISSING", g_mv_variable.handle ? "found" : "MISSING",
        g_depth_variable.handle ? "found" : "MISSING", g_mask_variable.handle ? "found" : "optional/missing",
        g_depth_reversed ? 1 : 0);
    if (!g_motion_technique.handle || !g_feed_technique.handle || !g_mv_variable.handle || !g_depth_variable.handle)
        Log("same-frame optical-flow path unavailable; internal zero-motion fallback will be used");
}

static void OnInitEffectRuntime(reshade::api::effect_runtime *runtime)
{
    if (runtime == nullptr) return;
    const HWND runtime_window = static_cast<HWND>(runtime->get_hwnd());
    const auto runtime_backbuffer = runtime->get_current_back_buffer();
    const auto runtime_desc = runtime->get_device()->get_resource_desc(runtime_backbuffer);
    if (g_proxy_window != nullptr && runtime_window == g_proxy_window)
    {
        g_proxy_runtime = runtime;
        g_proxy_overlay_open = false;
        Log("adopted native proxy ReShade runtime for overlay mirroring: runtime=%p hwnd=%p surface=%llux%u",
            runtime, runtime_window, runtime_desc.texture.width, runtime_desc.texture.height);
        return;
    }
    if (runtime_window == nullptr || runtime_desc.texture.width < 640 || runtime_desc.texture.height < 360)
    {
        Log("ignored helper ReShade runtime: hwnd=%p surface=%llux%u", runtime_window,
            runtime_desc.texture.width, runtime_desc.texture.height);
        return;
    }
    if (g_game_window != nullptr && runtime_window != nullptr && runtime_window != g_game_window)
    {
        Log("ignored secondary ReShade runtime: hwnd=%p primary=%p", runtime_window, g_game_window);
        return;
    }
    if (g_runtime != nullptr && runtime != g_runtime)
    {
        Log("ignored additional ReShade runtime on primary window: runtime=%p primary=%p", runtime, g_runtime);
        return;
    }
    if (g_game_window == nullptr && runtime_window != nullptr)
        g_game_window = runtime_window;
    ResolveHandles(runtime);
}
static void OnReloadedEffects(reshade::api::effect_runtime *runtime) { if (!g_runtime || runtime == g_runtime) ResolveHandles(runtime); }
static void OnDestroyEffectRuntime(reshade::api::effect_runtime *runtime)
{
    if (runtime == g_proxy_runtime.load())
    {
        g_proxy_runtime = nullptr;
        g_proxy_overlay_open = false;
        return;
    }
    if (runtime != g_runtime) return;
    auto *device = runtime->get_device();
    for (const BackbufferView &entry : g_backbuffer_views)
        if (entry.rtv.handle) device->destroy_resource_view(entry.rtv);
    g_backbuffer_views.clear();
    g_runtime = nullptr; g_feed_technique = {}; g_motion_technique = {};
    g_mv_variable = {}; g_depth_variable = {}; g_mask_variable = {};
    g_captured_motion.Reset(); g_captured_depth.Reset(); g_captured_mask.Reset();
    g_using_external_guides = false;
    g_reshade_overlay_open = false;
}

static void OnRenderTechnique(reshade::api::effect_runtime *runtime, reshade::api::effect_technique technique,
    reshade::api::command_list *, reshade::api::resource_view rtv, reshade::api::resource_view)
{
    using namespace reshade::api;
    if (!g_enabled || g_neural_failed || runtime != g_runtime || !g_feed_technique.handle ||
        technique.handle != g_feed_technique.handle ||
        runtime->get_device()->get_api() != device_api::d3d12) return;
    resource_view mv_srv = {}, mv_srgb = {}, depth_srv = {}, depth_srgb = {}, mask_srv = {}, mask_srgb = {};
    runtime->get_texture_binding(g_mv_variable, &mv_srv, &mv_srgb);
    runtime->get_texture_binding(g_depth_variable, &depth_srv, &depth_srgb);
    if (g_mask_variable.handle) runtime->get_texture_binding(g_mask_variable, &mask_srv, &mask_srgb);
    auto *device = runtime->get_device();
    const resource backbuffer_resource = device->get_resource_from_view(rtv);
    auto *backbuffer = reinterpret_cast<ID3D12Resource *>(backbuffer_resource.handle);
    auto *motion = reinterpret_cast<ID3D12Resource *>(device->get_resource_from_view(mv_srv).handle);
    auto *depth = reinterpret_cast<ID3D12Resource *>(device->get_resource_from_view(depth_srv).handle);
    auto *mask = mask_srv.handle ? reinterpret_cast<ID3D12Resource *>(device->get_resource_from_view(mask_srv).handle) : nullptr;
    if (!backbuffer || !motion || !depth) return;
    const D3D12_RESOURCE_DESC color_desc = backbuffer->GetDesc();
    const D3D12_RESOURCE_DESC mv_desc = motion->GetDesc();
    const D3D12_RESOURCE_DESC depth_desc = depth->GetDesc();
    if (color_desc.SampleDesc.Count != 1 || mv_desc.Width != color_desc.Width || mv_desc.Height != color_desc.Height ||
        depth_desc.Width != color_desc.Width || depth_desc.Height != color_desc.Height ||
        mv_desc.Format != DXGI_FORMAT_R16G16_FLOAT || depth_desc.Format != DXGI_FORMAT_R32_FLOAT)
    {
        static bool logged = false;
        if (!logged)
        {
            logged = true;
            Log("guide mismatch: color=%llux%u fmt=%u samples=%u mv=%llux%u fmt=%u depth=%llux%u fmt=%u",
                color_desc.Width, color_desc.Height, static_cast<unsigned int>(color_desc.Format), color_desc.SampleDesc.Count,
                mv_desc.Width, mv_desc.Height, static_cast<unsigned int>(mv_desc.Format),
                depth_desc.Width, depth_desc.Height, static_cast<unsigned int>(depth_desc.Format));
        }
        return;
    }
    g_mask_available = mask && mask->GetDesc().Width == color_desc.Width && mask->GetDesc().Height == color_desc.Height &&
        mask->GetDesc().Format == DXGI_FORMAT_R8_UNORM;
    const bool first_capture = !g_captured_motion || !g_captured_depth;
    g_captured_motion = motion;
    g_captured_depth = depth;
    g_captured_mask = g_mask_available ? mask : nullptr;
    if (first_capture)
        Log("captured DLSS5_Feed resources for same-frame on-present evaluation: %ux%u; DLSS history rejection mask=%s",
            static_cast<unsigned int>(color_desc.Width), color_desc.Height, g_mask_available ? "active" : "missing");
}

static reshade::api::resource_view GetBackbufferRtv(ID3D12Resource *backbuffer)
{
    for (const BackbufferView &entry : g_backbuffer_views)
        if (entry.resource == backbuffer) return entry.rtv;
    if (!g_runtime) return {};
    reshade::api::resource_view rtv = {};
    const reshade::api::resource resource = {reinterpret_cast<uint64_t>(backbuffer)};
    if (!g_runtime->get_device()->create_resource_view(resource, reshade::api::resource_usage::render_target,
            reshade::api::resource_view_desc {}, &rtv))
    {
        static bool logged = false;
        if (!logged) { logged = true; Log("failed to create ReShade RTV for manual same-frame guides"); }
        return {};
    }
    g_backbuffer_views.push_back({backbuffer, rtv});
    return rtv;
}

static bool CapturedGuidesMatchInput();

static bool RenderCurrentFrameGuides(ID3D12Resource *backbuffer)
{
    if (!g_runtime || !g_motion_technique.handle || !g_feed_technique.handle ||
        !g_mv_variable.handle || !g_depth_variable.handle) return false;
    reshade::api::command_queue *queue = g_runtime->get_command_queue();
    if (!queue) return false;
    reshade::api::command_list *commands = queue->get_immediate_command_list();
    const reshade::api::resource_view rtv = GetBackbufferRtv(backbuffer);
    if (!commands || !rtv.handle) return false;

    // Present callbacks happen before ReShade's normal effect pass. Render the
    // optical-flow provider and packer now, then submit them before NGX work.
    const reshade::api::resource resource = {reinterpret_cast<uint64_t>(backbuffer)};
    commands->barrier(resource, reshade::api::resource_usage::present, reshade::api::resource_usage::render_target);
    g_runtime->render_technique(g_motion_technique, commands, rtv);
    g_runtime->render_technique(g_feed_technique, commands, rtv);
    commands->barrier(resource, reshade::api::resource_usage::render_target, reshade::api::resource_usage::present);
    queue->flush_immediate_command_list();

    // The runtime normally owns the same native queue as the Present callback.
    // Preserve ordering explicitly if an unusual game exposes a second queue.
    auto *native_queue = reinterpret_cast<ID3D12CommandQueue *>(queue->get_native());
    if (native_queue && native_queue != g_command_queue)
    {
        const UINT64 value = ++g_neural_fence_value;
        if (FAILED(native_queue->Signal(g_neural_fence.Get(), value)) ||
            FAILED(g_command_queue->Wait(g_neural_fence.Get(), value)))
        {
            Log("failed to synchronize current-frame guide queue with Present queue");
            return false;
        }
    }

    const unsigned long long frame = ++g_current_guide_frames;
    if (frame <= 4 || frame % 1800 == 0)
        Log("same-frame VORT optical flow + DLSS5_Feed submitted before NGX: frame=%llu", frame);
    return CapturedGuidesMatchInput();
}

static bool CapturedGuidesMatchInput()
{
    if (!g_captured_motion || !g_captured_depth) return false;
    const D3D12_RESOURCE_DESC motion = g_captured_motion->GetDesc();
    const D3D12_RESOURCE_DESC depth = g_captured_depth->GetDesc();
    return motion.Width == g_resource_input_width && motion.Height == g_resource_input_height &&
        depth.Width == g_resource_input_width && depth.Height == g_resource_input_height &&
        motion.Format == DXGI_FORMAT_R16G16_FLOAT && depth.Format == DXGI_FORMAT_R32_FLOAT;
}

static D3D12_RESOURCE_BARRIER Transition(ID3D12Resource *resource,
    D3D12_RESOURCE_STATES before, D3D12_RESOURCE_STATES after)
{
    D3D12_RESOURCE_BARRIER barrier = {};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition.pResource = resource;
    barrier.Transition.StateBefore = before;
    barrier.Transition.StateAfter = after;
    barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    return barrier;
}

static bool ExecuteOnPresentPipeline(ID3D12Resource *backbuffer)
{
    const bool legacy_input = backbuffer == nullptr;
    if ((!legacy_input && !EnsureStandaloneResources(backbuffer)) || (legacy_input && !g_neural_ready)) return false;
    if (g_feature_recreate_requested.exchange(false) && !RecreateFeatures()) return false;

    const bool use_external_guides = !legacy_input && RenderCurrentFrameGuides(backbuffer);
    if (use_external_guides != g_using_external_guides)
    {
        g_using_external_guides = use_external_guides;
        g_need_history_reset = true;
        Log("on-present guide source changed to %s", use_external_guides ? "same-frame VORT optical flow" : "internal zero-motion fallback");
    }
    ID3D12Resource *motion = use_external_guides ? g_captured_motion.Get() : g_fallback_motion.Get();
    ID3D12Resource *depth = use_external_guides ? g_captured_depth.Get() : g_fallback_depth.Get();

    if (!BeginNeuralCommands()) return false;
    if (!legacy_input)
    {
        D3D12_RESOURCE_BARRIER copy_begin[2] = {
            Transition(backbuffer, D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_COPY_SOURCE),
            Transition(g_packed_color.Get(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_COPY_DEST)
        };
        g_neural_list->ResourceBarrier(2, copy_begin);
        D3D12_TEXTURE_COPY_LOCATION source = {};
        source.pResource = backbuffer;
        source.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        source.SubresourceIndex = 0;
        D3D12_TEXTURE_COPY_LOCATION destination = {};
        destination.pResource = g_packed_color.Get();
        destination.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        destination.SubresourceIndex = 0;
        const D3D12_BOX source_box = {0, 0, 0, g_resource_input_width, g_resource_input_height, 1};
        g_neural_list->CopyTextureRegion(&destination, 0, 0, 0, &source, &source_box);
        D3D12_RESOURCE_BARRIER copy_end[2] = {
            Transition(backbuffer, D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_PRESENT),
            Transition(g_packed_color.Get(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE)
        };
        g_neural_list->ResourceBarrier(2, copy_end);
    }
    else
    {
        D3D12_RESOURCE_BARRIER input_to_srv = Transition(g_packed_color.Get(),
            D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        g_neural_list->ResourceBarrier(1, &input_to_srv);
    }

    if (!use_external_guides)
    {
        D3D12_RESOURCE_BARRIER guides_to_rtv[2] = {
            Transition(motion, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_RENDER_TARGET),
            Transition(depth, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_RENDER_TARGET)
        };
        g_neural_list->ResourceBarrier(2, guides_to_rtv);
        D3D12_CPU_DESCRIPTOR_HANDLE motion_rtv = g_guide_rtv_heap->GetCPUDescriptorHandleForHeapStart();
        D3D12_CPU_DESCRIPTOR_HANDLE depth_rtv = motion_rtv;
        depth_rtv.ptr += g_guide_rtv_stride;
        const float motion_clear[4] = {0.0f, 0.0f, 0.0f, 0.0f};
        const float depth_clear[4] = {g_depth_reversed ? 0.0f : 1.0f, 0.0f, 0.0f, 0.0f};
        g_neural_list->ClearRenderTargetView(motion_rtv, motion_clear, 0, nullptr);
        g_neural_list->ClearRenderTargetView(depth_rtv, depth_clear, 0, nullptr);
        D3D12_RESOURCE_BARRIER guides_to_srv[2] = {
            Transition(motion, D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE),
            Transition(depth, D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE)
        };
        g_neural_list->ResourceBarrier(2, guides_to_srv);
    }
    else if (g_stable_sr_history)
    {
        // The stable SR path deliberately does not consume VORT motion. Keep
        // its dedicated motion texture deterministically zero after every
        // resolution recreation rather than relying on allocation contents.
        D3D12_RESOURCE_BARRIER to_rtv = Transition(g_fallback_motion.Get(),
            D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_RENDER_TARGET);
        g_neural_list->ResourceBarrier(1, &to_rtv);
        const float zero_motion[4] = {0.0f, 0.0f, 0.0f, 0.0f};
        g_neural_list->ClearRenderTargetView(g_guide_rtv_heap->GetCPUDescriptorHandleForHeapStart(),
            zero_motion, 0, nullptr);
        D3D12_RESOURCE_BARRIER to_srv = Transition(g_fallback_motion.Get(),
            D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        g_neural_list->ResourceBarrier(1, &to_srv);
    }

    const bool reset = g_need_history_reset || g_reset_every_frame;
    g_need_history_reset = false;
    D3D12_RESOURCE_BARRIER sr_to_uav = Transition(
        g_sr_stage.Get(), D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    g_neural_list->ResourceBarrier(1, &sr_to_uav);
    DWORD exception = 0;
    NVSDK_NGX_Result nr_result = NVSDK_NGX_Result_Success;
    if (!g_bypass_nr_for_ab)
    {
        SetNrEvaluationContract(depth, motion, reset);
        nr_result = SafeEvaluate(true, &exception);
        if (exception)
        {
            g_neural_list->Close();
            Fail("on-present NR evaluation exception", exception);
            return false;
        }
    }
    D3D12_RESOURCE_BARRIER nr_to_srv = {};
    ID3D12Resource *sr_color = g_packed_color.Get();
    if (!g_bypass_nr_for_ab)
    {
        nr_to_srv = Transition(g_nr_stage.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        g_neural_list->ResourceBarrier(1, &nr_to_srv);
        sr_color = g_nr_stage.Get();
    }
    // Keep motion-guided NR, but do not let generic optical-flow errors persist
    // through DLSS SR's temporal accumulator in the stable mode.
    ID3D12Resource *sr_motion = g_stable_sr_history ? g_fallback_motion.Get() : motion;
    ID3D12Resource *history_mask = !g_stable_sr_history && use_external_guides && g_mask_available ? g_captured_mask.Get() : nullptr;
    const bool sr_reset = g_stable_sr_history || reset;
    SetSrEvaluationContract(sr_color, depth, sr_motion, history_mask, sr_reset);
    NVSDK_NGX_Result sr_result = static_cast<NVSDK_NGX_Result>(0xBAD00004);
    if (NVSDK_NGX_SUCCEED(nr_result)) sr_result = SafeEvaluate(false, &exception);
    if (exception)
    {
        g_neural_list->Close();
        Fail("on-present DLSS SR evaluation exception", exception);
        return false;
    }
    NVSDK_NGX_Result fg_result = static_cast<NVSDK_NGX_Result>(0xBAD00004);
    const bool evaluate_fg = g_framegen_enabled && !g_framegen_failed && g_fg_feature &&
        NVSDK_NGX_SUCCEED(nr_result) && NVSDK_NGX_SUCCEED(sr_result);
    if (evaluate_fg)
    {
        D3D12_RESOURCE_BARRIER fg_begin[2] = {
            Transition(g_sr_stage.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
                D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE),
            Transition(g_fg_stage.Get(), D3D12_RESOURCE_STATE_COMMON,
                D3D12_RESOURCE_STATE_UNORDERED_ACCESS)
        };
        g_neural_list->ResourceBarrier(2, fg_begin);
        SetFgEvaluationContract(depth, motion, reset || g_fg_frames.load() == 0);
        exception = 0;
        fg_result = SafeEvaluateFg(&exception);
        if (exception)
        {
            g_neural_list->Close();
            Log("DLSS-G evaluation exception 0x%08X; frame generation disabled", exception);
            g_framegen_failed = true;
            return false;
        }
    }

    D3D12_RESOURCE_BARRIER restore[4] = {};
    UINT restore_count = 0;
    if (!g_bypass_nr_for_ab)
        restore[restore_count++] = Transition(g_nr_stage.Get(),
            D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    if (evaluate_fg)
    {
        restore[restore_count++] = Transition(g_sr_stage.Get(),
            D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_COMMON);
        restore[restore_count++] = Transition(g_fg_stage.Get(),
            D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COMMON);
    }
    else
        restore[restore_count++] = Transition(g_sr_stage.Get(),
            D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COMMON);
    if (legacy_input)
        restore[restore_count++] = Transition(g_packed_color.Get(),
            D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_COMMON);
    g_neural_list->ResourceBarrier(restore_count, restore);
    if (!SubmitNeuralCommands(false)) return false;
    if (legacy_input)
    {
        const UINT64 done = ++g_legacy_fence_value;
        if (FAILED(g_command_queue->Signal(g_legacy_fence12.Get(), done))) return false;
        g_legacy_d3d12_done_value = done;
    }
    if (NVSDK_NGX_FAILED(nr_result) || NVSDK_NGX_FAILED(sr_result))
    {
        Log("on-present evaluation failure: NR=0x%08X (%s), SR=0x%08X (%s)",
            static_cast<unsigned int>(nr_result), ResultName(nr_result),
            static_cast<unsigned int>(sr_result), ResultName(sr_result));
        Fail(NVSDK_NGX_FAILED(nr_result) ? "on-present NR evaluation" : "on-present DLSS SR evaluation",
            static_cast<unsigned int>(NVSDK_NGX_FAILED(nr_result) ? nr_result : sr_result));
        return false;
    }

    if (evaluate_fg)
    {
        if (NVSDK_NGX_SUCCEED(fg_result)) ++g_fg_frames;
        else
        {
            Log("DLSS-G evaluation failed: 0x%08X (%s); falling back to real frames",
                static_cast<unsigned int>(fg_result), ResultName(fg_result));
            g_framegen_failed = true;
        }
    }

    const unsigned long long frame = g_bypass_nr_for_ab ? g_sr_frames.load() + 1 : ++g_nr_frames;
    ++g_sr_frames;
    SetStatus(g_bypass_nr_for_ab ? "diagnostic: NR BYPASSED; DLSS SR only" :
        "active on present: NR model %d + DLSS SR + FG %s (%s guides)",
        g_active_nr_model, (!g_framegen_failed && g_fg_frames.load() > 1) ? "2x" : "warming/fallback",
        use_external_guides ? "same-frame motion" : "fallback");
    if (frame <= 8 || frame % 1800 == 0)
        Log("on-present frame %llu: NR=%s, SR=Success, model=%d, NR-reset=%d, NR-guides=%s, SR-history=%s, DLSS history mask=%s, input=%ux%u, output=%ux%u",
            frame, g_bypass_nr_for_ab ? "BYPASSED" : "Success", g_active_nr_model,
            reset ? 1 : 0, use_external_guides ? "same-frame-motion" : "fallback",
            g_stable_sr_history ? "per-frame-reset/zero-motion" : "temporal/VORT",
            history_mask ? "BOUND" : "none",
            g_resource_input_width, g_resource_input_height, g_resource_output_width, g_resource_output_height);
    return true;
}

static WPARAM MouseMessageKeyState(UINT message, DWORD mouse_data)
{
    UINT keys = 0;
    if ((GetAsyncKeyState(VK_SHIFT) & 0x8000) != 0) keys |= MK_SHIFT;
    if ((GetAsyncKeyState(VK_CONTROL) & 0x8000) != 0) keys |= MK_CONTROL;
    if ((GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0) keys |= MK_LBUTTON;
    if ((GetAsyncKeyState(VK_RBUTTON) & 0x8000) != 0) keys |= MK_RBUTTON;
    if ((GetAsyncKeyState(VK_MBUTTON) & 0x8000) != 0) keys |= MK_MBUTTON;
    if ((GetAsyncKeyState(VK_XBUTTON1) & 0x8000) != 0) keys |= MK_XBUTTON1;
    if ((GetAsyncKeyState(VK_XBUTTON2) & 0x8000) != 0) keys |= MK_XBUTTON2;
    if (message == WM_LBUTTONDOWN) keys |= MK_LBUTTON;
    if (message == WM_LBUTTONUP) keys &= ~MK_LBUTTON;
    if (message == WM_RBUTTONDOWN) keys |= MK_RBUTTON;
    if (message == WM_RBUTTONUP) keys &= ~MK_RBUTTON;
    if (message == WM_MBUTTONDOWN) keys |= MK_MBUTTON;
    if (message == WM_MBUTTONUP) keys &= ~MK_MBUTTON;
    if (message == WM_XBUTTONDOWN)
        keys |= HIWORD(mouse_data) == XBUTTON1 ? MK_XBUTTON1 : MK_XBUTTON2;
    if (message == WM_XBUTTONUP)
        keys &= ~(HIWORD(mouse_data) == XBUTTON1 ? MK_XBUTTON1 : MK_XBUTTON2);

    if (message == WM_MOUSEWHEEL || message == WM_MOUSEHWHEEL ||
        message == WM_XBUTTONDOWN || message == WM_XBUTTONUP)
        return MAKEWPARAM(keys, HIWORD(mouse_data));
    return keys;
}

static bool MapProxyScreenPointToGame(POINT screen_point, POINT &game_client, POINT &game_screen)
{
    if (!g_proxy_window || !g_game_window) return false;
    RECT proxy_client = {}, target_client = {};
    if (!GetClientRect(g_proxy_window, &proxy_client) || !GetClientRect(g_game_window, &target_client))
        return false;
    const LONG proxy_width = proxy_client.right - proxy_client.left;
    const LONG proxy_height = proxy_client.bottom - proxy_client.top;
    const LONG target_width = target_client.right - target_client.left;
    const LONG target_height = target_client.bottom - target_client.top;
    if (proxy_width <= 0 || proxy_height <= 0 || target_width <= 0 || target_height <= 0)
        return false;

    POINT proxy_point = screen_point;
    if (!ScreenToClient(g_proxy_window, &proxy_point)) return false;
    game_client.x = std::clamp<LONG>(MulDiv(proxy_point.x, target_width, proxy_width), 0, target_width - 1);
    game_client.y = std::clamp<LONG>(MulDiv(proxy_point.y, target_height, proxy_height), 0, target_height - 1);
    game_screen = game_client;
    return ClientToScreen(g_game_window, &game_screen) != FALSE;
}

static void UpdateProxyCursorClip(bool active)
{
    if (active && g_proxy_window != nullptr && IsWindow(g_proxy_window))
    {
        RECT client = {};
        POINT top_left = {}, bottom_right = {};
        if (GetClientRect(g_proxy_window, &client))
        {
            top_left = {client.left, client.top};
            bottom_right = {client.right, client.bottom};
            if (ClientToScreen(g_proxy_window, &top_left) && ClientToScreen(g_proxy_window, &bottom_right))
            {
                const RECT clip = {top_left.x, top_left.y, bottom_right.x, bottom_right.y};
                ClipCursor(&clip);
                g_proxy_cursor_clip_active = true;
                return;
            }
        }
    }
    if (g_proxy_cursor_clip_active.exchange(false))
        ClipCursor(nullptr);
}

static LRESULT CALLBACK ProxyLowLevelMouseProc(int code, WPARAM wparam, LPARAM lparam)
{
    // A mirrored overlay on the native proxy consumes its own input. Keep the
    // old button bridge only as a fallback if that secondary runtime is absent.
    if (code < 0 || !g_reshade_overlay_open.load() || g_game_window == nullptr ||
        g_proxy_runtime.load() != nullptr)
        return CallNextHookEx(g_proxy_mouse_hook, code, wparam, lparam);

    const UINT message = static_cast<UINT>(wparam);
    const bool routed_message = message == WM_LBUTTONDOWN || message == WM_LBUTTONUP ||
        message == WM_RBUTTONDOWN || message == WM_RBUTTONUP ||
        message == WM_MBUTTONDOWN || message == WM_MBUTTONUP ||
        message == WM_XBUTTONDOWN || message == WM_XBUTTONUP ||
        message == WM_MOUSEWHEEL || message == WM_MOUSEHWHEEL;
    if (!routed_message)
        return CallNextHookEx(g_proxy_mouse_hook, code, wparam, lparam);

    const HWND foreground = GetForegroundWindow();
    if (foreground != g_game_window && GetAncestor(foreground, GA_ROOT) != g_game_window)
        return CallNextHookEx(g_proxy_mouse_hook, code, wparam, lparam);

    const auto *mouse = reinterpret_cast<const MSLLHOOKSTRUCT *>(lparam);
    POINT client_point = {}, screen_point = {};
    const bool screen_coordinates = message == WM_MOUSEWHEEL || message == WM_MOUSEHWHEEL;
    if (!MapProxyScreenPointToGame(mouse->pt, client_point, screen_point))
    {
        client_point = mouse->pt;
        ScreenToClient(g_game_window, &client_point);
        screen_point = mouse->pt;
    }
    const POINT point = screen_coordinates ? screen_point : client_point;
    const LPARAM position = MAKELPARAM(static_cast<short>(point.x), static_cast<short>(point.y));
    if (PostMessageW(g_game_window, message, MouseMessageKeyState(message, mouse->mouseData), position))
    {
        ++g_overlay_mouse_events;
        return 1; // Suppress the original proxy-targeted event; ReShade receives exactly one routed event.
    }
    return CallNextHookEx(g_proxy_mouse_hook, code, wparam, lparam);
}

static LRESULT CALLBACK ProxyWindowProc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam)
{
    if (message == WM_NCHITTEST)
        return g_reshade_overlay_open.load() && g_proxy_runtime.load() == nullptr ? HTTRANSPARENT : HTCLIENT;
    if (message == WM_ERASEBKGND) return 1;
    if (message == WM_CLOSE) { DestroyWindow(hwnd); return 0; }
    if (message == WM_DESTROY) { KillTimer(hwnd, 1); PostQuitMessage(0); return 0; }
    if (message == WM_TIMER && wparam == 1)
    {
        const ULONGLONG now = GetTickCount64();
        const ULONGLONG last_present = g_last_primary_present_tick.load();
        const HWND foreground = GetForegroundWindow();
        const bool game_foreground = g_game_window != nullptr &&
            (foreground == g_game_window || GetAncestor(foreground, GA_ROOT) == g_game_window);
        const bool primary_alive = g_game_window != nullptr && IsWindow(g_game_window) &&
            last_present != 0 && now - last_present < 5000;
        if ((!primary_alive || !game_foreground) && !g_proxy_hidden && IsWindowVisible(hwnd))
        {
            UpdateProxyCursorClip(false);
            ShowWindow(hwnd, SW_HIDE);
            g_proxy_watchdog_hidden = true;
        }
        return 0;
    }
    if (message == WM_SETCURSOR)
    {
        SetCursor(LoadCursorW(nullptr, MAKEINTRESOURCEW(32512)));
        return TRUE;
    }
    // The foreground game continues receiving its registered raw/relative mouse
    // input even though this no-activate proxy covers its reduced window. Posting
    // an additional, scaled absolute WM_MOUSEMOVE makes games which recenter the
    // cursor feed that position back through the proxy repeatedly (visible as
    // cursor "gravity"). Leave movement to the game's native raw-input path and
    // bridge only events whose hit target is otherwise consumed by the proxy.
    if (message == WM_MOUSEMOVE && g_game_window != nullptr)
        return g_reshade_overlay_open.load() && g_proxy_runtime.load() != nullptr ?
            DefWindowProcW(hwnd, message, wparam, lparam) : 0;

    const bool routed_mouse_message = message == WM_LBUTTONDOWN || message == WM_LBUTTONUP ||
        message == WM_RBUTTONDOWN || message == WM_RBUTTONUP ||
        message == WM_MBUTTONDOWN || message == WM_MBUTTONUP ||
        message == WM_XBUTTONDOWN || message == WM_XBUTTONUP ||
        message == WM_MOUSEWHEEL || message == WM_MOUSEHWHEEL;
    if (routed_mouse_message && g_game_window != nullptr)
    {
        if (g_reshade_overlay_open.load() && g_proxy_runtime.load() != nullptr)
            return DefWindowProcW(hwnd, message, wparam, lparam);
        if (message == WM_LBUTTONDOWN || message == WM_RBUTTONDOWN ||
            message == WM_MBUTTONDOWN || message == WM_XBUTTONDOWN)
            SetForegroundWindow(g_game_window);
        POINT screen_point = {static_cast<short>(LOWORD(lparam)), static_cast<short>(HIWORD(lparam))};
        ClientToScreen(hwnd, &screen_point);
        POINT client_point = {}, mapped_screen = {};
        if (!MapProxyScreenPointToGame(screen_point, client_point, mapped_screen))
        {
            ScreenToClient(g_game_window, &screen_point);
            client_point = screen_point;
        }
        // Wheel messages use screen coordinates; button messages use client
        // coordinates. Mixing these conventions can misroute menu interactions.
        const bool screen_coordinates = message == WM_MOUSEWHEEL || message == WM_MOUSEHWHEEL;
        const POINT mapped_point = screen_coordinates ? mapped_screen : client_point;
        const LPARAM mapped = MAKELPARAM(static_cast<short>(mapped_point.x), static_cast<short>(mapped_point.y));
        PostMessageW(g_game_window, message, wparam, mapped);
        return 0;
    }
    return DefWindowProcW(hwnd, message, wparam, lparam);
}

static void UpdateSourceFps()
{
    const ULONGLONG now = GetTickCount64();
    if (g_source_fps_sample_start == 0) g_source_fps_sample_start = now;
    ++g_source_fps_sample_frames;
    const ULONGLONG elapsed = now - g_source_fps_sample_start;
    if (elapsed < 500) return;
    g_source_fps = static_cast<unsigned int>((static_cast<ULONGLONG>(g_source_fps_sample_frames) * 1000 + elapsed / 2) / elapsed);
    g_source_fps_sample_frames = 0;
    g_source_fps_sample_start = now;
}

static void UpdateOutputFps()
{
    const ULONGLONG now = GetTickCount64();
    if (g_output_fps_sample_start == 0) g_output_fps_sample_start = now;
    ++g_output_fps_sample_frames;
    const ULONGLONG elapsed = now - g_output_fps_sample_start;
    if (elapsed < 500) return;
    g_proxy_fps = static_cast<unsigned int>((static_cast<ULONGLONG>(g_output_fps_sample_frames) * 1000 + elapsed / 2) / elapsed);
    g_output_fps_sample_frames = 0;
    g_output_fps_sample_start = now;
}

static DWORD WINAPI ProxyWindowThread(void *)
{
    WNDCLASSEXW wc = {sizeof(wc)};
    wc.lpfnWndProc = ProxyWindowProc;
    wc.hInstance = g_self;
    wc.hCursor = LoadCursorW(nullptr, MAKEINTRESOURCEW(32512));
    wc.lpszClassName = L"StandaloneDLSSNRNativeOutput";
    RegisterClassExW(&wc);
    HMONITOR monitor = MonitorFromWindow(g_game_window, MONITOR_DEFAULTTONEAREST);
    MONITORINFO info = {sizeof(info)};
    if (GetMonitorInfoW(monitor, &info))
    {
        g_proxy_window = CreateWindowExW(WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE,
            wc.lpszClassName, L"Standalone DLSS-NR Native Output", WS_POPUP | WS_VISIBLE,
            info.rcMonitor.left, info.rcMonitor.top, g_output_width.load(), g_output_height.load(),
            nullptr, nullptr, g_self, nullptr);
    }
    if (g_proxy_window) SetTimer(g_proxy_window, 1, 500, nullptr);
    g_proxy_mouse_hook = SetWindowsHookExW(WH_MOUSE_LL, ProxyLowLevelMouseProc, g_self, 0);
    Log("proxy overlay click bridge: %s (hook=%p error=%lu)",
        g_proxy_mouse_hook ? "ready" : "FAILED", g_proxy_mouse_hook, g_proxy_mouse_hook ? 0 : GetLastError());
    SetEvent(g_proxy_window_ready);
    if (g_proxy_window == nullptr) return 1;
    MSG message;
    while (GetMessageW(&message, nullptr, 0, 0) > 0)
    {
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
    if (g_proxy_mouse_hook) { UnhookWindowsHookEx(g_proxy_mouse_hook); g_proxy_mouse_hook = nullptr; }
    g_proxy_window = nullptr;
    return 0;
}

static bool EnsureProxy(ID3D12Resource *source)
{
    if (g_proxy_swapchain || g_proxy_failed || source == nullptr || g_command_queue == nullptr || g_game_window == nullptr)
        return g_proxy_swapchain != nullptr;

    const D3D12_RESOURCE_DESC source_desc = source->GetDesc();
    const UINT width = g_output_width.load(), height = g_output_height.load();
    if (source_desc.Width != width || source_desc.Height != height)
    {
        static bool logged = false;
        if (!logged) { Log("native presentation waiting: captured NR resource is %llux%u, expected %ux%u",
            source_desc.Width, source_desc.Height, width, height); logged = true; }
        return false;
    }

    HMONITOR monitor = MonitorFromWindow(g_game_window, MONITOR_DEFAULTTONEAREST);
    MONITORINFO monitor_info = {sizeof(monitor_info)};
    if (!GetMonitorInfoW(monitor, &monitor_info)) { g_proxy_failed = true; return false; }
    g_proxy_window_ready = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    g_proxy_window_thread = CreateThread(nullptr, 0, ProxyWindowThread, nullptr, 0, nullptr);
    if (g_proxy_window_thread == nullptr || WaitForSingleObject(g_proxy_window_ready, 3000) != WAIT_OBJECT_0 || g_proxy_window == nullptr)
    {
        Log("native presentation failed: proxy UI thread/window error=%lu", GetLastError());
        g_proxy_failed = true; return false;
    }
    SetForegroundWindow(g_game_window);

    DXGI_FORMAT present_format = source_desc.Format;
    switch (present_format)
    {
    case DXGI_FORMAT_R8G8B8A8_TYPELESS: present_format = DXGI_FORMAT_R8G8B8A8_UNORM; break;
    case DXGI_FORMAT_R10G10B10A2_TYPELESS: present_format = DXGI_FORMAT_R10G10B10A2_UNORM; break;
    case DXGI_FORMAT_R16G16B16A16_TYPELESS: present_format = DXGI_FORMAT_R16G16B16A16_FLOAT; break;
    case DXGI_FORMAT_B8G8R8A8_TYPELESS: present_format = DXGI_FORMAT_B8G8R8A8_UNORM; break;
    case DXGI_FORMAT_B8G8R8X8_TYPELESS: present_format = DXGI_FORMAT_B8G8R8X8_UNORM; break;
    case DXGI_FORMAT_R11G11B10_FLOAT: present_format = DXGI_FORMAT_R10G10B10A2_UNORM; break;
    default: break;
    }
    // Match the proxy swapchain to the resolved game convention. The shader
    // intentionally remains a pass-through: both F10 and neural output retain
    // the game's signal values while DXGI applies the correct presentation
    // metadata instead of unconditionally interpreting them as PQ/BT.2020.
    if (g_active_color_profile == ColorProfile::Srgb)
        present_format = DXGI_FORMAT_R8G8B8A8_UNORM;
    else if (g_active_color_profile == ColorProfile::ScRgb)
        present_format = DXGI_FORMAT_R16G16B16A16_FLOAT;
    else
        present_format = DXGI_FORMAT_R10G10B10A2_UNORM;
    Log("native proxy initialization: source=%llux%u source_format=%u present_format=%u queue=%p window=%p",
        source_desc.Width, source_desc.Height, static_cast<unsigned int>(source_desc.Format),
        static_cast<unsigned int>(present_format), g_command_queue, g_proxy_window);

    Microsoft::WRL::ComPtr<IDXGIFactory4> factory;
    HRESULT hr = CreateDXGIFactory1(IID_PPV_ARGS(&factory));
    const char *failed_stage = "CreateDXGIFactory1";
    g_proxy_allow_tearing = false;
    Microsoft::WRL::ComPtr<IDXGIFactory5> factory5;
    BOOL allow_tearing = FALSE;
    if (SUCCEEDED(hr) && SUCCEEDED(factory.As(&factory5)) &&
        SUCCEEDED(factory5->CheckFeatureSupport(DXGI_FEATURE_PRESENT_ALLOW_TEARING,
            &allow_tearing, sizeof(allow_tearing))))
        g_proxy_allow_tearing = allow_tearing == TRUE;
    DXGI_SWAP_CHAIN_DESC1 desc = {};
    desc.Width = width; desc.Height = height; desc.Format = present_format;
    desc.SampleDesc.Count = 1; desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    desc.BufferCount = 2; desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    desc.Scaling = DXGI_SCALING_STRETCH; desc.AlphaMode = DXGI_ALPHA_MODE_IGNORE;
    if (g_proxy_allow_tearing) desc.Flags |= DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING;
    Microsoft::WRL::ComPtr<IDXGISwapChain1> swapchain1;
    if (SUCCEEDED(hr)) { failed_stage = "CreateSwapChainForHwnd"; hr = factory->CreateSwapChainForHwnd(g_command_queue, g_proxy_window, &desc, nullptr, nullptr, &swapchain1); }
    if (SUCCEEDED(hr)) { failed_stage = "IDXGISwapChain3"; hr = swapchain1.As(&g_proxy_swapchain); }
    if (SUCCEEDED(hr) && g_active_color_profile == ColorProfile::Srgb)
    {
        const HRESULT color_hr = g_proxy_swapchain->SetColorSpace1(DXGI_COLOR_SPACE_RGB_FULL_G22_NONE_P709);
        Log("native proxy sRGB/BT.709 color space: hr=0x%08X", static_cast<unsigned int>(color_hr));
    }
    if (SUCCEEDED(hr) && g_active_color_profile == ColorProfile::ScRgb)
    {
        const HRESULT color_hr = g_proxy_swapchain->SetColorSpace1(DXGI_COLOR_SPACE_RGB_FULL_G10_NONE_P709);
        Log("native proxy scRGB color space: hr=0x%08X", static_cast<unsigned int>(color_hr));
    }
    if (SUCCEEDED(hr) && g_active_color_profile == ColorProfile::Hdr10Pq)
    {
        const HRESULT color_hr = g_proxy_swapchain->SetColorSpace1(DXGI_COLOR_SPACE_RGB_FULL_G2084_NONE_P2020);
        Log("native proxy HDR10 PQ/Rec.2020 color space: hr=0x%08X", static_cast<unsigned int>(color_hr));
    }
    if (SUCCEEDED(hr) && g_active_color_profile == ColorProfile::Hdr10Hlg)
    {
        // Match ReShade's DXGI compatibility mapping for hdr10_hlg on SDKs
        // that predate DXGI_COLOR_SPACE_RGB_FULL_GHLG_NONE_P2020.
        const HRESULT color_hr = g_proxy_swapchain->SetColorSpace1(DXGI_COLOR_SPACE_RGB_FULL_G22_NONE_P2020);
        Log("native proxy HLG/Rec.2020 compatibility color space: hr=0x%08X", static_cast<unsigned int>(color_hr));
    }
    Microsoft::WRL::ComPtr<ID3D12Device> device;
    if (SUCCEEDED(hr)) { failed_stage = "ID3D12CommandQueue::GetDevice"; hr = g_command_queue->GetDevice(IID_PPV_ARGS(&device)); }
    for (UINT index = 0; index < 2 && SUCCEEDED(hr); ++index)
    {
        failed_stage = "CreateCommandAllocator";
        hr = device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&g_proxy_allocators[index]));
        if (SUCCEEDED(hr))
        {
            failed_stage = "CreateCommandList";
            hr = device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT,
                g_proxy_allocators[index].Get(), nullptr, IID_PPV_ARGS(&g_proxy_lists[index]));
        }
        if (SUCCEEDED(hr)) { failed_stage = "CloseCommandList"; hr = g_proxy_lists[index]->Close(); }
    }
    if (SUCCEEDED(hr)) { failed_stage = "CreateFence"; hr = device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&g_proxy_fence)); }
    D3D12_DESCRIPTOR_HEAP_DESC heap_desc = {};
    heap_desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV; heap_desc.NumDescriptors = 2;
    if (SUCCEEDED(hr)) { failed_stage = "Create RTV heap"; hr = device->CreateDescriptorHeap(&heap_desc, IID_PPV_ARGS(&g_proxy_rtv_heap)); }
    heap_desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV; heap_desc.NumDescriptors = 6; heap_desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    if (SUCCEEDED(hr)) { failed_stage = "Create SRV heap"; hr = device->CreateDescriptorHeap(&heap_desc, IID_PPV_ARGS(&g_proxy_srv_heap)); }

    D3D12_DESCRIPTOR_RANGE range = {};
    range.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV; range.NumDescriptors = 3; range.BaseShaderRegister = 0;
    D3D12_ROOT_PARAMETER root_parameters[2] = {};
    root_parameters[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    root_parameters[0].DescriptorTable.NumDescriptorRanges = 1; root_parameters[0].DescriptorTable.pDescriptorRanges = &range;
    root_parameters[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    root_parameters[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
    root_parameters[1].Constants.ShaderRegister = 0;
    root_parameters[1].Constants.Num32BitValues = 8;
    root_parameters[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    D3D12_STATIC_SAMPLER_DESC sampler = {};
    // Native neural output remains a 1:1 sample. The same sampler also gives
    // the F10 diagnostic path a conventional linear stretch from render size
    // to monitor size instead of leaving the game surface in the upper-left.
    sampler.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR; sampler.AddressU = sampler.AddressV = sampler.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    sampler.ShaderRegister = 0; sampler.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL; sampler.MaxLOD = D3D12_FLOAT32_MAX;
    D3D12_ROOT_SIGNATURE_DESC root_desc = {};
    root_desc.NumParameters = 2; root_desc.pParameters = root_parameters; root_desc.NumStaticSamplers = 1; root_desc.pStaticSamplers = &sampler;
    root_desc.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;
    Microsoft::WRL::ComPtr<ID3DBlob> signature, error_blob, vertex_shader, pixel_shader;
    if (SUCCEEDED(hr)) { failed_stage = "SerializeRootSignature"; hr = D3D12SerializeRootSignature(&root_desc, D3D_ROOT_SIGNATURE_VERSION_1, &signature, &error_blob); }
    if (SUCCEEDED(hr)) { failed_stage = "CreateRootSignature"; hr = device->CreateRootSignature(0, signature->GetBufferPointer(), signature->GetBufferSize(), IID_PPV_ARGS(&g_proxy_root_signature)); }
    static const char *shader_source =
        "Texture2D<float3> Neural:register(t0); Texture2D<float3> Original:register(t1); Texture2D<float3> Post:register(t2);"
        "SamplerState Samp:register(s0); cbuffer C:register(b0){uint Mode;uint Fps;uint ShowFps;float Threshold;float2 CursorInput;float2 PreviousCursorInput;}"
        "struct O{float4 p:SV_Position;float2 uv:TEXCOORD0;};"
        "O VS(uint id:SV_VertexID){O o; float2 p=float2((id<<1)&2,id&2); o.uv=p; o.p=float4(p*float2(2,-2)+float2(-1,1),0,1); return o;}"
        "uint GlyphRow(uint c,uint y){static const uint r[91]={"
        "14,17,17,17,17,17,14,4,12,4,4,4,4,14,14,17,1,2,4,8,31,30,1,1,14,1,1,30,"
        "2,6,10,18,31,2,2,31,16,16,30,1,1,30,14,16,16,30,17,17,14,31,1,2,4,8,8,8,"
        "14,17,17,14,17,17,14,14,17,17,15,1,1,14,31,16,16,30,16,16,16,30,17,17,30,16,16,16,"
        "15,16,16,14,1,1,30};return(c<13&&y<7)?r[c*7+y]:0;}"
        "float3 AddFps(float3 color,float2 pos){if(ShowFps==0)return color;const uint scale=4;"
        "int2 q=int2(pos)-int2(16,16);if(q.x< -8||q.y< -6||q.x>=176||q.y>=36)return color;"
        "color*=0.25;if(q.x<0||q.y<0)return color;uint ch=(uint)q.x/(6*scale);uint x=((uint)q.x%(6*scale))/scale;uint y=(uint)q.y/scale;"
        "uint code=99;if(ch==0)code=10;else if(ch==1)code=11;else if(ch==2)code=12;else if(ch==4)code=min(Fps,999)/100;"
        "else if(ch==5)code=(min(Fps,999)/10)%10;else if(ch==6)code=min(Fps,999)%10;"
        "if(x<5&&y<7&&((GlyphRow(code,y)>>(4-x))&1)!=0)return float3(0.25,0.95,0.35);return color;}"
        "float4 PS(O i):SV_Target{float3 post=Post.SampleLevel(Samp,i.uv,0);float3 neural=Neural.SampleLevel(Samp,i.uv,0);float3 result;"
        "uint w,h; Post.GetDimensions(w,h); uint2 p=min(uint2(i.uv*float2(w,h)),uint2(w-1,h-1));"
        "float3 postPoint=Post.Load(int3(p,0)); float3 original=Original.Load(int3(p,0)); float3 d=abs(postPoint-original);"
        "if(Mode==0)result=post;else if(Mode==1)result=neural;else result=max(d.r,max(d.g,d.b))>Threshold?post:neural;"
        "bool cursorNow=CursorInput.x>=0&&all(float2(p)>=CursorInput-float2(6,6))&&all(float2(p)<=CursorInput+float2(36,44));"
        "bool cursorPrevious=PreviousCursorInput.x>=0&&all(float2(p)>=PreviousCursorInput-float2(6,6))&&all(float2(p)<=PreviousCursorInput+float2(36,44));"
        "if((cursorNow||cursorPrevious)&&Mode!=1)result=Mode==0?original:neural;"
        "return float4(AddFps(result,i.p.xy),1);}";
    if (SUCCEEDED(hr)) { failed_stage = "Compile vertex shader"; hr = D3DCompile(shader_source, strlen(shader_source), nullptr, nullptr, nullptr, "VS", "vs_5_0", 0, 0, &vertex_shader, &error_blob); }
    if (SUCCEEDED(hr)) { failed_stage = "Compile pixel shader"; hr = D3DCompile(shader_source, strlen(shader_source), nullptr, nullptr, nullptr, "PS", "ps_5_0", 0, 0, &pixel_shader, &error_blob); }
    D3D12_GRAPHICS_PIPELINE_STATE_DESC pipeline_desc = {};
    pipeline_desc.pRootSignature = g_proxy_root_signature.Get();
    if (vertex_shader) pipeline_desc.VS = {vertex_shader->GetBufferPointer(), vertex_shader->GetBufferSize()};
    if (pixel_shader) pipeline_desc.PS = {pixel_shader->GetBufferPointer(), pixel_shader->GetBufferSize()};
    pipeline_desc.BlendState.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
    pipeline_desc.SampleMask = UINT_MAX; pipeline_desc.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
    pipeline_desc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE; pipeline_desc.DepthStencilState.DepthEnable = FALSE;
    pipeline_desc.DepthStencilState.StencilEnable = FALSE; pipeline_desc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    pipeline_desc.NumRenderTargets = 1; pipeline_desc.RTVFormats[0] = present_format; pipeline_desc.SampleDesc.Count = 1;
    if (SUCCEEDED(hr)) { failed_stage = "CreateGraphicsPipelineState"; hr = device->CreateGraphicsPipelineState(&pipeline_desc, IID_PPV_ARGS(&g_proxy_pipeline)); }
    if (SUCCEEDED(hr))
    {
        g_proxy_rtv_stride = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
        g_proxy_srv_stride = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
        auto rtv = g_proxy_rtv_heap->GetCPUDescriptorHandleForHeapStart();
        for (UINT index = 0; index < 2; ++index)
        {
            Microsoft::WRL::ComPtr<ID3D12Resource> buffer;
            if (FAILED(g_proxy_swapchain->GetBuffer(index, IID_PPV_ARGS(&buffer)))) { hr = E_FAIL; failed_stage = "Get proxy buffer"; break; }
            device->CreateRenderTargetView(buffer.Get(), nullptr, rtv); rtv.ptr += g_proxy_rtv_stride;
        }
        D3D12_SHADER_RESOURCE_VIEW_DESC srv = {};
        srv.Format = TypedInputFormat(source_desc.Format); srv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
        srv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING; srv.Texture2D.MipLevels = 1;
        device->CreateShaderResourceView(source, &srv, g_proxy_srv_heap->GetCPUDescriptorHandleForHeapStart());
    }
    if (SUCCEEDED(hr)) g_proxy_fence_event = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    if (FAILED(hr) || g_proxy_fence_event == nullptr)
    {
        Log("native presentation initialization failed at %s: hr=0x%08X win32=%lu", failed_stage, static_cast<unsigned int>(hr), GetLastError());
        g_proxy_failed = true; if (g_proxy_window) { DestroyWindow(g_proxy_window); g_proxy_window = nullptr; }
        g_proxy_swapchain.Reset(); return false;
    }
    g_proxy_present_format = present_format;
    Log("native proxy ready: %ux%u source_format=%u present_format=%u hwnd=%p tearing=%s", width, height,
        static_cast<unsigned int>(source_desc.Format), static_cast<unsigned int>(present_format), g_proxy_window,
        g_proxy_allow_tearing ? "supported" : "unavailable");
    return true;
}

static void OnInitCommandQueue(reshade::api::command_queue *queue)
{
    if (queue == nullptr || queue->get_device()->get_api() != reshade::api::device_api::d3d12 ||
        (queue->get_type() & reshade::api::command_queue_type::graphics) != reshade::api::command_queue_type::graphics) return;
    Log("observed D3D12 graphics queue: reshade=%p native=%p", queue, queue->get_native());
}

static bool AdoptPresentQueue(reshade::api::command_queue *queue)
{
    if (!queue || queue->get_device()->get_api() != reshade::api::device_api::d3d12) return false;
    auto *native = reinterpret_cast<ID3D12CommandQueue *>(queue->get_native());
    if (!native) return false;
    if (native == g_command_queue)
    {
        g_rs_queue = queue;
        return true;
    }
    if (g_neural_ready || g_proxy_swapchain)
    {
        Fail("game OnPresent queue changed after initialization");
        return false;
    }
    if (g_command_queue) g_command_queue->Release();
    g_rs_queue = queue;
    g_command_queue = native;
    g_command_queue->AddRef();
    Log("adopted authoritative game OnPresent queue: reshade=%p native=%p", queue, g_command_queue);
    return true;
}

static bool PresentProxyAfterReshade(ID3D12Resource *backbuffer)
{
    ID3D12Resource *real_source = g_nr_output;
    ID3D12Resource *original_source = g_packed_color.Get();
    if (g_proxy_hidden || g_sr_frames.load() == 0 || real_source == nullptr ||
        original_source == nullptr || backbuffer == nullptr || !EnsureProxy(real_source)) return false;

    const bool use_framegen = g_framegen_enabled && !g_framegen_failed &&
        g_show_neural_output && g_fg_stage && g_fg_frames.load() >= 2;
    const bool legacy = g_present_api == reshade::api::device_api::d3d11 ||
        g_present_api == reshade::api::device_api::d3d9 ||
        g_present_api == reshade::api::device_api::vulkan;
    const D3D12_RESOURCE_STATES original_base_state = legacy ?
        D3D12_RESOURCE_STATE_COMMON : D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
    const D3D12_RESOURCE_STATES post_base_state = legacy ?
        D3D12_RESOURCE_STATE_COMMON : D3D12_RESOURCE_STATE_PRESENT;
    ID3D12Resource *present_sources[2] = {
        use_framegen ? g_fg_stage.Get() : real_source,
        real_source
    };
    const UINT present_count = use_framegen ? 2u : 1u;

    if (g_proxy_fence_value != 0 && g_proxy_fence->GetCompletedValue() < g_proxy_fence_value)
    {
        g_proxy_fence->SetEventOnCompletion(g_proxy_fence_value, g_proxy_fence_event);
        if (WaitForSingleObject(g_proxy_fence_event, 1000) != WAIT_OBJECT_0) return false;
    }
    Microsoft::WRL::ComPtr<ID3D12Device> device;
    if (FAILED(g_command_queue->GetDevice(IID_PPV_ARGS(&device)))) return false;
    float cursor_x = -1000.0f, cursor_y = -1000.0f;
    static float previous_cursor_x = -1000.0f, previous_cursor_y = -1000.0f;
    if (g_reshade_overlay_open.load())
    {
        POINT cursor = {};
        RECT client = {};
        if (GetCursorPos(&cursor) && ScreenToClient(g_game_window, &cursor) && GetClientRect(g_game_window, &client) &&
            client.right > client.left && client.bottom > client.top)
        {
            cursor_x = static_cast<float>(cursor.x) * g_resource_input_width / (client.right - client.left);
            cursor_y = static_cast<float>(cursor.y) * g_resource_input_height / (client.bottom - client.top);
        }
    }
    else
    {
        previous_cursor_x = previous_cursor_y = -1000.0f;
    }
    struct ProxyConstants
    {
        UINT mode; UINT fps; UINT show_fps; float threshold;
        float cursor_x; float cursor_y; float previous_cursor_x; float previous_cursor_y;
    } constants = {
        g_show_neural_output ? (g_composite_reshade_output && g_reshade_overlay_open.load() ? 2u : 1u) : 0u,
        g_proxy_fps.load(),
        g_show_proxy_fps ? 1u : 0u,
        0.0f,
        cursor_x, cursor_y, previous_cursor_x, previous_cursor_y
    };
    previous_cursor_x = cursor_x; previous_cursor_y = cursor_y;
    D3D12_VIEWPORT viewport = {0, 0, static_cast<float>(g_output_width.load()), static_cast<float>(g_output_height.load()), 0, 1};
    D3D12_RECT scissor = {0, 0, static_cast<LONG>(g_output_width.load()), static_cast<LONG>(g_output_height.load())};

    for (UINT present_index = 0; present_index < present_count; ++present_index)
    {
        ID3D12CommandAllocator *allocator = g_proxy_allocators[present_index].Get();
        ID3D12GraphicsCommandList *list = g_proxy_lists[present_index].Get();
        if (allocator == nullptr || list == nullptr || FAILED(allocator->Reset()) ||
            FAILED(list->Reset(allocator, nullptr))) return false;
        Microsoft::WRL::ComPtr<ID3D12Resource> destination;
        const UINT buffer_index = g_proxy_swapchain->GetCurrentBackBufferIndex();
        if (FAILED(g_proxy_swapchain->GetBuffer(buffer_index, IID_PPV_ARGS(&destination)))) return false;
        ID3D12Resource *neural_source = present_sources[present_index];
        ID3D12Resource *sources[3] = {neural_source, original_source, backbuffer};
        auto srv_handle = g_proxy_srv_heap->GetCPUDescriptorHandleForHeapStart();
        srv_handle.ptr += static_cast<SIZE_T>(present_index) * 3 * g_proxy_srv_stride;
        for (ID3D12Resource *source : sources)
        {
            D3D12_SHADER_RESOURCE_VIEW_DESC srv = {};
            srv.Format = TypedInputFormat(source->GetDesc().Format); srv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
            srv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING; srv.Texture2D.MipLevels = 1;
            device->CreateShaderResourceView(source, &srv, srv_handle);
            srv_handle.ptr += g_proxy_srv_stride;
        }
        D3D12_RESOURCE_BARRIER barriers[8] = {
            Transition(destination.Get(), D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_RENDER_TARGET),
            Transition(neural_source, D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE),
            Transition(original_source, original_base_state, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE),
            Transition(backbuffer, post_base_state, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE),
            Transition(destination.Get(), D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PRESENT),
            Transition(neural_source, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_COMMON),
            Transition(original_source, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, original_base_state),
            Transition(backbuffer, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, post_base_state)
        };
        list->ResourceBarrier(4, barriers);
        ID3D12DescriptorHeap *heaps[] = {g_proxy_srv_heap.Get()}; list->SetDescriptorHeaps(1, heaps);
        list->SetGraphicsRootSignature(g_proxy_root_signature.Get()); list->SetPipelineState(g_proxy_pipeline.Get());
        auto gpu_srv = g_proxy_srv_heap->GetGPUDescriptorHandleForHeapStart();
        gpu_srv.ptr += static_cast<UINT64>(present_index) * 3 * g_proxy_srv_stride;
        list->SetGraphicsRootDescriptorTable(0, gpu_srv);
        list->SetGraphicsRoot32BitConstants(1, 8, &constants, 0);
        list->RSSetViewports(1, &viewport); list->RSSetScissorRects(1, &scissor);
        auto rtv = g_proxy_rtv_heap->GetCPUDescriptorHandleForHeapStart(); rtv.ptr += buffer_index * g_proxy_rtv_stride;
        list->OMSetRenderTargets(1, &rtv, FALSE, nullptr); list->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        list->DrawInstanced(3, 1, 0, 0);
        list->ResourceBarrier(4, barriers + 4);
        if (FAILED(list->Close())) return false;
        ID3D12CommandList *lists[] = {list};
        g_command_queue->ExecuteCommandLists(1, lists);
        ++g_proxy_fence_value; g_command_queue->Signal(g_proxy_fence.Get(), g_proxy_fence_value);
        const UINT present_flags = g_proxy_allow_tearing ? DXGI_PRESENT_ALLOW_TEARING : 0u;
        if (FAILED(g_proxy_swapchain->Present(0, present_flags))) return false;
        ++g_frames_presented;
        UpdateOutputFps();
    }
    if (legacy)
    {
        const UINT64 done = ++g_legacy_fence_value;
        if (FAILED(g_command_queue->Signal(g_legacy_fence12.Get(), done))) return false;
        g_legacy_d3d12_done_value = done;
    }
    const unsigned long long post_frame = ++g_post_reshade_frames;
    if (post_frame == 1)
        Log("post-ReShade native presentation active; effects and overlay are available to the proxy compositor");
    if (use_framegen && post_frame == 2)
        Log("experimental DLSS-G presentation active: generated frame then real frame, uncapped flip cadence, tearing=%s",
            g_proxy_allow_tearing ? "enabled" : "unavailable");
    return true;
}

static bool EnsureStandaloneResources(ID3D12Resource *backbuffer)
{
    if (!backbuffer) return false;
    const D3D12_RESOURCE_DESC desc = backbuffer->GetDesc();
    return EnsureStandaloneResources(static_cast<UINT>(desc.Width), desc.Height, desc.Format);
}

static void OnReshadePresent(reshade::api::effect_runtime *runtime)
{
    if (runtime == nullptr) return;
    if (runtime == g_proxy_runtime.load())
    {
        const bool desired = g_reshade_overlay_open.load();
        if (g_proxy_overlay_open.load() != desired && !g_proxy_overlay_syncing)
        {
            g_proxy_overlay_syncing = true;
            runtime->open_overlay(desired, reshade::api::input_source::none);
            g_proxy_overlay_syncing = false;
        }
        return;
    }
    if (runtime != g_runtime || !g_pending_proxy_frame) return;
    g_pending_proxy_frame = false;
    if (!g_enabled || g_neural_failed || g_proxy_hidden) return;
    const reshade::api::resource resource = runtime->get_current_back_buffer();
    if (!resource.handle) return;

    reshade::api::command_queue *queue = runtime->get_command_queue();
    const reshade::api::device_api api = runtime->get_device()->get_api();
    if (api != reshade::api::device_api::vulkan && queue)
        queue->flush_immediate_command_list();
    if (api == reshade::api::device_api::d3d12)
        PresentProxyAfterReshade(reinterpret_cast<ID3D12Resource *>(resource.handle));
    else if ((api == reshade::api::device_api::d3d11 || api == reshade::api::device_api::d3d9) &&
        CopyLegacyFrameToD3D12(reinterpret_cast<void *>(resource.handle), true))
        PresentProxyAfterReshade(g_legacy_post12.Get());
    else if (api == reshade::api::device_api::vulkan)
        PresentProxyAfterReshade(g_packed_color.Get());
}

static void OnReshadeFinishEffects(reshade::api::effect_runtime *runtime,
    reshade::api::command_list *commands, reshade::api::resource_view rtv,
    reshade::api::resource_view)
{
    if (!runtime || runtime != g_runtime || !commands || !g_vulkan_waiting_for_effects ||
        runtime->get_device()->get_api() != reshade::api::device_api::vulkan)
        return;
    reshade::api::command_queue *queue = runtime->get_command_queue();
    const reshade::api::resource source = runtime->get_device()->get_resource_from_view(rtv);
    if (!queue || !source.handle) return;

    // Match the proven Vulkan feeder boundary exactly: ReShade has already
    // transitioned the swapchain image from present to render_target before
    // invoking reshade_finish_effects. Copy from that known state, restore it,
    // and flush the same command list before handing the shared image to D3D12.
    if (!RecordVulkanFrameCopy(queue, source, reshade::api::resource_usage::render_target,
        false, commands))
        return;
    queue->flush_immediate_command_list();
    const bool completed = CompleteVulkanFrameCopies(queue);
    g_vulkan_waiting_for_effects = false;
    g_vulkan_release_wait_queued = false;
    g_vulkan_input_copy_recorded = false;
    g_vulkan_post_copy_recorded = false;
    g_pending_proxy_frame = false;
    if (!completed)
    {
        if (g_proxy_window && !g_proxy_hidden)
        {
            g_proxy_hidden = true;
            ShowWindow(g_proxy_window, SW_HIDE);
        }
        SetStatus("Vulkan effects-boundary handoff failed; see log");
        return;
    }
    if (!ExecuteOnPresentPipeline(nullptr)) return;
    if (!g_enabled || g_neural_failed || !g_nr_output || !EnsureProxy(g_nr_output)) return;
    g_pending_proxy_frame = true;
}

static bool OnReshadeOpenOverlay(reshade::api::effect_runtime *runtime, bool open, reshade::api::input_source)
{
    if (runtime == g_proxy_runtime.load())
    {
        // The proxy overlay follows the primary runtime. Cancel an independent
        // proxy hotkey toggle so the two runtimes cannot become inverted.
        if (!g_proxy_overlay_syncing && open != g_reshade_overlay_open.load())
            return true;
        g_proxy_overlay_open = open;
        Log("native proxy ReShade overlay mirror %s", open ? "opened" : "closed");
        return false;
    }
    if (runtime != g_runtime) return false;
    g_reshade_overlay_open = open;
    Log("primary ReShade overlay %s; native proxy mirror requested",
        open ? "opened" : "closed");
    return false;
}

static void OnPresent(reshade::api::command_queue *queue, reshade::api::swapchain *swapchain,
    const reshade::api::rect *, const reshade::api::rect *, uint32_t, const reshade::api::rect *)
{
    if (!queue || !swapchain) return;
    const HWND present_window = static_cast<HWND>(swapchain->get_hwnd());
    if (g_game_window && present_window && present_window != g_game_window) return;
    if (!g_game_window && present_window) g_game_window = present_window;
    g_last_primary_present_tick = GetTickCount64();
    const HWND foreground = GetForegroundWindow();
    const bool primary_foreground = foreground == g_game_window || GetAncestor(foreground, GA_ROOT) == g_game_window;
    UpdateProxyCursorClip(primary_foreground && g_proxy_window != nullptr &&
        !g_proxy_hidden && IsWindowVisible(g_proxy_window));
    if (g_proxy_watchdog_hidden.load() && primary_foreground && g_proxy_window != nullptr && !g_proxy_hidden)
    {
        g_proxy_watchdog_hidden = false;
        ShowWindow(g_proxy_window, SW_SHOWNOACTIVATE);
        Log("native proxy restored after primary borderless presentation resumed");
    }
    const reshade::api::device_api api = queue->get_device()->get_api();
    if (!g_neural_ready)
    {
        const auto color_resource = swapchain->get_current_back_buffer();
        if (color_resource.handle)
        {
            const auto color_desc = swapchain->get_device()->get_resource_desc(color_resource);
            RefreshColorProfile(swapchain, static_cast<DXGI_FORMAT>(color_desc.texture.format), "first present");
        }
    }
    if (api == reshade::api::device_api::d3d12)
    {
        g_present_api = api;
        if (!AdoptPresentQueue(queue)) return;
    }
    else if (api == reshade::api::device_api::d3d11 || api == reshade::api::device_api::d3d9)
    {
        g_rs_queue = queue;
        if (!InitializeLegacyTransport(queue->get_device()))
        {
            SetStatus("failed to initialize %s -> D3D12 interop; see log",
                api == reshade::api::device_api::d3d11 ? "D3D11" : "D3D9");
            return;
        }
    }
    else if (api == reshade::api::device_api::vulkan)
    {
        g_rs_queue = queue;
        if (!InitializeVulkanTransport(queue))
        {
            SetStatus("failed to initialize Vulkan -> D3D12 interop; see log");
            return;
        }
    }
    else
    {
        SetStatus("unsupported graphics API (D3D9/D3D11/D3D12/Vulkan required)");
        return;
    }
    UpdateSourceFps();
    const unsigned long long routed_mouse_events = g_overlay_mouse_events.load();
    if (routed_mouse_events != g_last_logged_mouse_event && routed_mouse_events <= 8)
    {
        g_last_logged_mouse_event = routed_mouse_events;
        Log("overlay mouse bridge routed event=%llu", routed_mouse_events);
    }
    g_pending_proxy_frame = false;
    g_vulkan_release_wait_queued = false;
    g_vulkan_input_copy_recorded = false;
    g_vulkan_post_copy_recorded = false;
    g_vulkan_waiting_for_effects = false;
    if (!g_enabled || g_neural_failed)
    {
        if (g_proxy_window && !g_proxy_hidden)
        {
            g_proxy_hidden = true;
            UpdateProxyCursorClip(false);
            ShowWindow(g_proxy_window, SW_HIDE);
        }
        return;
    }
    const bool f10 = (GetAsyncKeyState(VK_F10) & 0x8000) != 0;
    if (f10 && !g_f10_down && g_proxy_window != nullptr)
    {
        if (g_proxy_hidden)
        {
            // Home/Alt+X deliberately hide the topmost presentation window so
            // external overlays can receive input. F10 first restores it.
            g_proxy_hidden = false;
            ShowWindow(g_proxy_window, SW_SHOWNOACTIVATE);
            Log("native presentation restored after overlay access; output=%s",
                g_show_neural_output ? "neural native" : "stretched original");
        }
        else
        {
            g_show_neural_output = !g_show_neural_output;
            g_need_history_reset = true;
            Log("F10 presentation A/B changed to %s",
                g_show_neural_output ? "neural native output" : "linearly stretched original backbuffer");
        }
    }
    g_f10_down = f10;
    const bool home = (GetAsyncKeyState(VK_HOME) & 0x8000) != 0;
    const bool alt_x = (GetAsyncKeyState(VK_MENU) & 0x8000) != 0 && (GetAsyncKeyState('X') & 0x8000) != 0;
    if (home && !g_home_down && g_proxy_window != nullptr && !g_proxy_hidden)
        Log("ReShade overlay requested; keeping native proxy visible for post-overlay composition");
    if (alt_x && !g_alt_x_down && g_proxy_window != nullptr && !g_proxy_hidden)
    {
        g_proxy_hidden = true;
        UpdateProxyCursorClip(false);
        ShowWindow(g_proxy_window, SW_HIDE);
        Log("native proxy hidden automatically for NVIDIA overlay");
    }
    g_home_down = home;
    g_alt_x_down = alt_x;
    const reshade::api::resource backbuffer_resource = swapchain->get_current_back_buffer();
    if (!backbuffer_resource.handle) return;
    if (api == reshade::api::device_api::d3d12)
    {
        auto *backbuffer = reinterpret_cast<ID3D12Resource *>(backbuffer_resource.handle);
        const D3D12_RESOURCE_DESC backbuffer_desc = backbuffer->GetDesc();
        g_input_width = static_cast<UINT>(backbuffer_desc.Width);
        g_input_height = backbuffer_desc.Height;
        if (!ExecuteOnPresentPipeline(backbuffer)) return;
    }
    else if (api == reshade::api::device_api::d3d11 || api == reshade::api::device_api::d3d9)
    {
        const auto desc = swapchain->get_device()->get_resource_desc(backbuffer_resource);
        const UINT width = static_cast<UINT>(desc.texture.width);
        const UINT height = desc.texture.height;
        const DXGI_FORMAT format = static_cast<DXGI_FORMAT>(desc.texture.format);
        g_input_width = width; g_input_height = height;
        if (!EnsureStandaloneResources(width, height, format)) return;
        if (!CopyLegacyFrameToD3D12(reinterpret_cast<void *>(backbuffer_resource.handle), false) ||
            !ExecuteOnPresentPipeline(nullptr))
        {
            if (!g_neural_failed) SetStatus("waiting for a valid %s shared frame",
                api == reshade::api::device_api::d3d11 ? "D3D11" : "D3D9");
            return;
        }
    }
    else
    {
        const auto desc = swapchain->get_device()->get_resource_desc(backbuffer_resource);
        const UINT width = static_cast<UINT>(desc.texture.width);
        const UINT height = desc.texture.height;
        const DXGI_FORMAT format = VulkanSharedFormat(static_cast<DXGI_FORMAT>(desc.texture.format));
        g_input_width = width; g_input_height = height;
        if (!EnsureStandaloneResources(width, height, format)) return;
        g_vulkan_waiting_for_effects = true;
        SetStatus("Vulkan resources ready; waiting for ReShade effects boundary");
        return;
    }
    if (g_nr_output != nullptr && EnsureProxy(g_nr_output)) g_pending_proxy_frame = true;
}

static void OnInitSwapchain(reshade::api::swapchain *swapchain, bool)
{
    if (swapchain == nullptr) return;
    const auto resource = swapchain->get_current_back_buffer();
    const auto desc = swapchain->get_device()->get_resource_desc(resource);
    HWND hwnd = static_cast<HWND>(swapchain->get_hwnd());
    const UINT width = static_cast<unsigned int>(desc.texture.width);
    const UINT height = desc.texture.height;
    if (hwnd == nullptr || width < 640 || height < 360)
    {
        const unsigned long long ignored = ++g_ignored_secondary_surfaces;
        if (ignored <= 8)
            Log("ignored helper swapchain: hwnd=%p surface=%ux%u", hwnd, width, height);
        return;
    }
    if (g_game_window != nullptr && hwnd != g_game_window && IsWindow(g_game_window))
    {
        const unsigned long long ignored = ++g_ignored_secondary_surfaces;
        if (ignored <= 8)
            Log("ignored secondary swapchain: hwnd=%p surface=%ux%u primary=%p", hwnd, width, height, g_game_window);
        return;
    }
    RefreshColorProfile(swapchain, static_cast<DXGI_FORMAT>(desc.texture.format), "init_swapchain");
    g_game_window = hwnd;
    g_input_width = width;
    g_input_height = height;
    MONITORINFO info = {sizeof(info)};
    if (hwnd != nullptr && GetMonitorInfoW(MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST), &info))
    {
        g_output_width = static_cast<unsigned int>(info.rcMonitor.right - info.rcMonitor.left);
        g_output_height = static_cast<unsigned int>(info.rcMonitor.bottom - info.rcMonitor.top);
        if (g_proxy_window != nullptr)
            SetWindowPos(g_proxy_window, HWND_TOPMOST, info.rcMonitor.left, info.rcMonitor.top,
                static_cast<int>(g_output_width.load()), static_cast<int>(g_output_height.load()),
                SWP_NOACTIVATE | (g_proxy_hidden ? 0 : SWP_SHOWWINDOW));
    }
    RECT client = {};
    GetClientRect(hwnd, &client);
    Log("adopted primary swapchain: render=%ux%u client=%ldx%ld monitor=%ux%u hwnd=%p mode=%s",
        g_input_width.load(), g_input_height.load(), client.right - client.left, client.bottom - client.top,
        g_output_width.load(), g_output_height.load(), hwnd,
        (client.right - client.left == static_cast<LONG>(g_output_width.load()) &&
         client.bottom - client.top == static_cast<LONG>(g_output_height.load())) ? "borderless/native-client" : "reduced borderless/windowed");
}

static bool OnSetFullscreenState(reshade::api::swapchain *swapchain, bool fullscreen, void *)
{
    if (!g_enabled || !fullscreen || swapchain == nullptr) return false;
    HWND hwnd = static_cast<HWND>(swapchain->get_hwnd());
    if (hwnd == nullptr) return false;
    HMONITOR monitor = MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST);
    MONITORINFO info = {sizeof(info)};
    if (!GetMonitorInfoW(monitor, &info)) return false;

    LONG_PTR style = GetWindowLongPtrW(hwnd, GWL_STYLE);
    style &= ~(WS_CAPTION | WS_THICKFRAME | WS_MINIMIZEBOX | WS_MAXIMIZEBOX | WS_SYSMENU);
    style |= WS_POPUP | WS_VISIBLE;
    SetWindowLongPtrW(hwnd, GWL_STYLE, style);
    SetWindowPos(hwnd, HWND_TOP,
        info.rcMonitor.left, info.rcMonitor.top,
        info.rcMonitor.right - info.rcMonitor.left,
        info.rcMonitor.bottom - info.rcMonitor.top,
        SWP_FRAMECHANGED | SWP_NOACTIVATE | SWP_SHOWWINDOW);
    Log("exclusive fullscreen request virtualized with window-only handling: monitor=%ldx%ld",
        info.rcMonitor.right - info.rcMonitor.left, info.rcMonitor.bottom - info.rcMonitor.top);
    return true;
}

static void DrawOverlay(reshade::api::effect_runtime *)
{
    constexpr const char *section = "Standalone.DLSSNR";
    ImGui::TextUnformatted("Standalone DLSS-NR + Super Resolution");
    const char *api_name = g_present_api == reshade::api::device_api::d3d9 ? "D3D9 -> D3D11 -> D3D12" :
        g_present_api == reshade::api::device_api::d3d11 ? "D3D11 -> D3D12" :
        g_present_api == reshade::api::device_api::vulkan ? "Vulkan -> D3D12 shared timeline" :
        g_present_api == reshade::api::device_api::d3d12 ? "D3D12 native" : "waiting";
    ImGui::Text("Graphics transport: %s", api_name);
    if (ImGui::Checkbox("Enable addon", &g_enabled))
    {
        reshade::set_config_value(nullptr, section, "Enabled", g_enabled ? "1" : "0");
        g_need_history_reset = true;
        if (g_runtime && g_motion_technique.handle)
            g_runtime->set_technique_state(g_motion_technique, false);
        if (g_runtime && g_feed_technique.handle)
            g_runtime->set_technique_state(g_feed_technique, false);
        if (g_proxy_window)
        {
            g_proxy_hidden = !g_enabled;
            ShowWindow(g_proxy_window, g_enabled ? SW_SHOWNOACTIVATE : SW_HIDE);
        }
    }
    ImGui::SameLine();
    ImGui::TextDisabled("supports reduced-resolution fullscreen and borderless swapchains");

    ImGui::TextDisabled("Vulkan: select a real reduced windowed resolution in-game; the native proxy supplies borderless output.");

    int profile = static_cast<int>(g_color_profile);
    if (ImGui::Combo("Input color profile", &profile,
        "Auto (swapchain + format)\0sRGB (nonlinear BT.709)\0Linear BT.709 / scRGB\0BT.2100 PQ / HDR10\0BT.2100 HLG\0"))
    {
        g_color_profile = static_cast<ColorProfile>(profile);
        char value[16]; sprintf_s(value, "%d", profile);
        reshade::set_config_value(nullptr, section, "InputColorProfile", static_cast<const char *>(value));
        SetStatus("color profile changed; restart required");
    }
    ImGui::TextDisabled("Active: %s | detected: %s (format %u)", ProfileName(g_active_color_profile),
        ColorSpaceName(g_detected_color_space), static_cast<unsigned int>(g_detected_swapchain_format));
    ImGui::TextDisabled("Auto follows the primary swapchain; use a manual profile only when a game reports it incorrectly.");
    ImGui::TextUnformatted("DLSS-NR model:");
    bool model_changed = false;
    if (ImGui::RadioButton("Model 1", g_nr_model == 1)) { g_nr_model = 1; model_changed = true; }
    ImGui::SameLine();
    if (ImGui::RadioButton("Model 2", g_nr_model == 2)) { g_nr_model = 2; model_changed = true; }
    ImGui::SameLine();
    if (ImGui::RadioButton("Model 3", g_nr_model == 3)) { g_nr_model = 3; model_changed = true; }
    if (model_changed)
    {
        char value[16]; sprintf_s(value, "%d", g_nr_model);
        reshade::set_config_value(nullptr, section, "Model", static_cast<const char *>(value));
        if (g_neural_ready)
        {
            g_feature_recreate_requested = true;
            SetStatus("switching live feature from model %d to model %d", g_active_nr_model, g_nr_model);
        }
    }
    ImGui::TextDisabled("Active feature model: %d%s", g_active_nr_model,
        g_feature_recreate_requested.load() ? " (switch queued for next Present)" : "");

    auto save_float = [section](const char *key, float value)
    {
        char text[32]; sprintf_s(text, "%.4f", value);
        reshade::set_config_value(nullptr, section, key, static_cast<const char *>(text));
    };
    if (ImGui::SliderFloat("NR intensity", &g_nr_intensity, 0.0f, 2.0f, "%.2f")) save_float("Intensity", g_nr_intensity);
    if (ImGui::SliderFloat("Local tone strength", &g_nr_local_tone, 0.0f, 2.0f, "%.2f")) save_float("LocalTone", g_nr_local_tone);
    if (ImGui::SliderFloat("Local structure strength", &g_nr_local_structure, 0.0f, 2.0f, "%.2f")) save_float("LocalStructure", g_nr_local_structure);
    if (ImGui::SliderFloat("Skin / character structure", &g_nr_skin_structure, -1.0f, 1.0f, "%.2f")) save_float("SkinStructure", g_nr_skin_structure);
    if (ImGui::Checkbox("Reset temporal history every frame", &g_reset_every_frame))
        reshade::set_config_value(nullptr, section, "ResetEveryFrame", g_reset_every_frame ? "1" : "0");
    ImGui::SameLine();
    if (ImGui::Button("Reset history now")) g_need_history_reset = true;
    if (ImGui::Checkbox("Stable DLSS SR (no persistent SR history)", &g_stable_sr_history))
    {
        reshade::set_config_value(nullptr, section, "StableSrHistory", g_stable_sr_history ? "1" : "0");
        g_need_history_reset = true;
        Log("DLSS SR history mode changed to %s",
            g_stable_sr_history ? "per-frame reset with zero motion" : "experimental temporal VORT motion");
    }
    ImGui::TextDisabled("Off by default; enable only as a per-frame SR-history diagnostic.");
    if (ImGui::Checkbox("Experimental DLSS Frame Generation (2x)", &g_framegen_enabled))
    {
        reshade::set_config_value(nullptr, section, "FrameGeneration", g_framegen_enabled ? "1" : "0");
        g_fg_frames = 0;
        g_need_history_reset = true;
        Log("experimental DLSS-G changed to %s", g_framegen_enabled ? "enabled" : "disabled");
    }
    ImGui::TextDisabled(g_framegen_failed ? "DLSS-G unavailable/failed; real-frame fallback is active." :
        "Presents one generated frame followed by one real frame; F10 original bypasses FG.");
    if (ImGui::Checkbox("Diagnostic A/B: bypass feature 18 (DLSS SR only)", &g_bypass_nr_for_ab))
    {
        g_need_history_reset = true;
        Log("user A/B diagnostic changed: NR feature 18 is now %s", g_bypass_nr_for_ab ? "BYPASSED" : "ENABLED");
    }
    ImGui::TextDisabled("Toggle this while viewing a detailed static scene to isolate NR's contribution from upscaling.");
    if (ImGui::Checkbox("Present neural output (F10)", &g_show_neural_output))
    {
        g_need_history_reset = true;
        Log("overlay presentation A/B changed to %s",
            g_show_neural_output ? "neural native output" : "linearly stretched original backbuffer");
    }
    if (ImGui::Checkbox("Composite ReShade menu while open", &g_composite_reshade_output))
        reshade::set_config_value(nullptr, section, "CompositeReshade", g_composite_reshade_output ? "1" : "0");
    if (ImGui::Checkbox("Show native proxy FPS counter", &g_show_proxy_fps))
        reshade::set_config_value(nullptr, section, "ShowProxyFps", g_show_proxy_fps ? "1" : "0");
    ImGui::TextDisabled("Off keeps the native-size proxy but displays a simple linear stretch of the game's original frame.");

    ImGui::Separator();
    ImGui::Text("Status: %s", g_neural_status);
    ImGui::TextUnformatted("Activation boundary: game OnPresent (standalone private NGX runtime)");
    ImGui::Text("Pipeline: NR=%llu; SR=%llu; generated=%llu; FG=%s; active model=%d%s",
        g_nr_frames.load(), g_sr_frames.load(), g_fg_frames.load(),
        g_framegen_failed ? "failed/off" : (g_framegen_enabled ? "2x" : "off"),
        g_active_nr_model, g_bypass_nr_for_ab ? " (NR BYPASSED)" : "");
    ImGui::Text("Contract: %ux%u -> %ux%u", g_input_width.load(), g_input_height.load(), g_output_width.load(), g_output_height.load());
    ImGui::Text("NR guides: %s; DLSS SR history: %s; validation mask=%s",
        g_using_external_guides ? "same-frame VORT optical flow" : "internal zero-motion fallback",
        g_stable_sr_history ? "per-frame reset / zero motion" : "experimental temporal VORT",
        g_mask_available ? "valid" : "automatic mask");
    ImGui::Text("Current-frame guide submissions: %llu", g_current_guide_frames.load());
    ImGui::Text("Native proxy: %s; frames=%llu; post-ReShade=%llu", g_proxy_swapchain ? "presenting" : (g_proxy_failed ? "failed" : "waiting"),
        g_frames_presented.load(), g_post_reshade_frames.load());
    ImGui::Text("Source FPS: %u; proxy presents/sec: %u; ReShade menu input: %s",
        g_source_fps.load(), g_proxy_fps.load(),
        g_reshade_overlay_open.load() ? (g_proxy_runtime.load() ? "native proxy overlay" : "fallback bridge") : "game forwarding");
    ImGui::Text("Routed ReShade mouse events: %llu", g_overlay_mouse_events.load());
    ImGui::Text("Presented image: %s", g_show_neural_output ? "neural native output" : "stretched original backbuffer");
    ImGui::TextWrapped("Set a reduced game resolution in fullscreen or borderless mode. Vulkan games must create a genuinely reduced windowed swapchain; the addon then supplies native-size borderless presentation. It performs NR followed by DLSS Super Resolution.");
    ImGui::TextUnformatted("Press F10 for neural/stretched-original A/B. Home is composited into the proxy; Alt+X hides it for NVIDIA.");
    ImGui::Text("Log: %s", g_log_path);
}

static void OnDestroyDevice(reshade::api::device *device)
{
    for (const BackbufferView &entry : g_backbuffer_views)
        if (entry.rtv.handle) device->destroy_resource_view(entry.rtv);
    g_backbuffer_views.clear();
    if (g_neural_device && reinterpret_cast<ID3D12Device *>(device->get_native()) == g_neural_device.Get())
    {
        Log("game D3D12 device is being destroyed; standalone session ending");
        g_neural_ready = false;
        g_neural_failed = true;
        SetStatus("D3D12 device destroyed");
    }
    else if ((device->get_api() == reshade::api::device_api::d3d11 ||
              device->get_api() == reshade::api::device_api::d3d9) && g_present_api == device->get_api())
    {
        Log("game legacy graphics device is being destroyed; standalone session ending");
        g_neural_ready = false;
        g_neural_failed = true;
        SetStatus("legacy graphics device destroyed");
    }
    else if (device->get_api() == reshade::api::device_api::vulkan && device == g_vulkan_reshade_device)
    {
        Log("game Vulkan device is being destroyed; releasing imported shared objects");
        if (g_rs_queue) g_rs_queue->wait_idle();
        ReleaseLegacyFrameResources();
        if (g_vulkan.ok && g_vulkan_semaphore != VK_NULL_HANDLE)
            g_vulkan.DestroySemaphore(g_vulkan.dev, g_vulkan_semaphore, nullptr);
        g_vulkan_semaphore = VK_NULL_HANDLE;
        g_vulkan_fence = {};
        if (g_vulkan.lib) FreeLibrary(g_vulkan.lib);
        g_vulkan = {};
        g_vulkan_reshade_device = nullptr;
        g_neural_ready = false;
        g_neural_failed = true;
        SetStatus("Vulkan device destroyed");
    }
}

static bool OnCreateDevice(reshade::api::device_api api, uint32_t &)
{
    if (api == reshade::api::device_api::vulkan)
        FeedVkHookInstall();
    return false;
}

BOOL APIENTRY DllMain(HMODULE module, DWORD reason, LPVOID)
{
    if (reason == DLL_PROCESS_ATTACH)
    {
        g_self = module;
        DisableThreadLibraryCalls(module);
        InitializeCriticalSection(&g_log_lock);
        GetModuleFileNameW(module, g_addon_directory, MAX_PATH);
        wchar_t *slash = wcsrchr(g_addon_directory, L'\\');
        if (slash) slash[1] = L'\0';
        char local[MAX_PATH] = {};
        if (GetEnvironmentVariableA("LOCALAPPDATA", local, MAX_PATH) != 0)
        {
            sprintf_s(g_log_path, "%s\\RHI\\Logs", local);
            CreateDirectoryA(g_log_path, nullptr);
            strcat_s(g_log_path, "\\standalone-dlssnr.log");
        }
        else strcpy_s(g_log_path, "standalone-dlssnr.log");
        { FILE *file = nullptr; if (fopen_s(&file, g_log_path, "w") == 0 && file) fclose(file); }

        if (!reshade::register_addon(module)) return FALSE;
        constexpr const char *section = "Standalone.DLSSNR";
        char enabled[8] = "1"; size_t enabled_size = sizeof(enabled);
        reshade::get_config_value(nullptr, section, "Enabled", enabled, &enabled_size);
        g_enabled = strcmp(enabled, "0") != 0;
        auto read_setting = [section](const char *key, const char *fallback, char *buffer, size_t capacity)
        {
            strcpy_s(buffer, capacity, fallback);
            size_t size = capacity;
            reshade::get_config_value(nullptr, section, key, buffer, &size);
        };
        char value[32];
        // InputColorProfile is intentionally a new key. Older builds defaulted
        // ColorProfile to HDR10 globally, so importing that value would retain
        // the cross-game color bug instead of migrating installations to Auto.
        read_setting("InputColorProfile", "0", value, sizeof(value)); g_color_profile = static_cast<ColorProfile>(std::clamp(atoi(value), 0, 4));
        read_setting("Model", "1", value, sizeof(value)); g_nr_model = std::clamp(atoi(value), 1, 3);
        read_setting("Intensity", "1.0", value, sizeof(value)); g_nr_intensity = std::clamp(static_cast<float>(atof(value)), 0.0f, 2.0f);
        read_setting("LocalTone", "1.0", value, sizeof(value)); g_nr_local_tone = std::clamp(static_cast<float>(atof(value)), 0.0f, 2.0f);
        read_setting("LocalStructure", "1.0", value, sizeof(value)); g_nr_local_structure = std::clamp(static_cast<float>(atof(value)), 0.0f, 2.0f);
        read_setting("SkinStructure", "-1.0", value, sizeof(value)); g_nr_skin_structure = std::clamp(static_cast<float>(atof(value)), -1.0f, 1.0f);
        read_setting("ResetEveryFrame", "0", value, sizeof(value)); g_reset_every_frame = strcmp(value, "0") != 0;
        read_setting("StableSrHistory", "0", value, sizeof(value)); g_stable_sr_history = strcmp(value, "0") != 0;
        read_setting("FrameGeneration", "1", value, sizeof(value)); g_framegen_enabled = strcmp(value, "0") != 0;
        read_setting("CompositeReshade", "1", value, sizeof(value)); g_composite_reshade_output = strcmp(value, "0") != 0;
        read_setting("ShowProxyFps", "1", value, sizeof(value)); g_show_proxy_fps = strcmp(value, "0") != 0;
        Log("Standalone DLSS-NR + SR %s attached; requested profile=%s model=%d", ADDON_VERSION, ProfileName(g_color_profile), g_nr_model);
        reshade::register_event<reshade::addon_event::create_device>(OnCreateDevice);
        reshade::register_event<reshade::addon_event::set_fullscreen_state>(OnSetFullscreenState);
        reshade::register_event<reshade::addon_event::init_effect_runtime>(OnInitEffectRuntime);
        reshade::register_event<reshade::addon_event::destroy_effect_runtime>(OnDestroyEffectRuntime);
        reshade::register_event<reshade::addon_event::reshade_reloaded_effects>(OnReloadedEffects);
        reshade::register_event<reshade::addon_event::reshade_render_technique>(OnRenderTechnique);
        reshade::register_event<reshade::addon_event::init_swapchain>(OnInitSwapchain);
        reshade::register_event<reshade::addon_event::init_command_queue>(OnInitCommandQueue);
        reshade::register_event<reshade::addon_event::destroy_device>(OnDestroyDevice);
        reshade::register_event<reshade::addon_event::present>(OnPresent);
        reshade::register_event<reshade::addon_event::reshade_finish_effects>(OnReshadeFinishEffects);
        reshade::register_event<reshade::addon_event::reshade_present>(OnReshadePresent);
        reshade::register_event<reshade::addon_event::reshade_open_overlay>(OnReshadeOpenOverlay);
        reshade::register_overlay(nullptr, DrawOverlay);
    }
    else if (reason == DLL_PROCESS_DETACH)
    {
        reshade::unregister_event<reshade::addon_event::create_device>(OnCreateDevice);
        reshade::unregister_event<reshade::addon_event::set_fullscreen_state>(OnSetFullscreenState);
        reshade::unregister_overlay(nullptr, DrawOverlay);
        reshade::unregister_event<reshade::addon_event::reshade_open_overlay>(OnReshadeOpenOverlay);
        reshade::unregister_event<reshade::addon_event::reshade_present>(OnReshadePresent);
        reshade::unregister_event<reshade::addon_event::reshade_finish_effects>(OnReshadeFinishEffects);
        reshade::unregister_event<reshade::addon_event::present>(OnPresent);
        reshade::unregister_event<reshade::addon_event::destroy_device>(OnDestroyDevice);
        reshade::unregister_event<reshade::addon_event::reshade_render_technique>(OnRenderTechnique);
        reshade::unregister_event<reshade::addon_event::reshade_reloaded_effects>(OnReloadedEffects);
        reshade::unregister_event<reshade::addon_event::destroy_effect_runtime>(OnDestroyEffectRuntime);
        reshade::unregister_event<reshade::addon_event::init_effect_runtime>(OnInitEffectRuntime);
        reshade::unregister_event<reshade::addon_event::init_swapchain>(OnInitSwapchain);
        reshade::unregister_event<reshade::addon_event::init_command_queue>(OnInitCommandQueue);
        if (g_proxy_fence_event) CloseHandle(g_proxy_fence_event);
        g_proxy_pipeline.Reset(); g_proxy_root_signature.Reset(); g_proxy_srv_heap.Reset(); g_proxy_rtv_heap.Reset();
        for (UINT index = 0; index < 2; ++index)
        {
            g_proxy_lists[index].Reset();
            g_proxy_allocators[index].Reset();
        }
        g_proxy_fence.Reset(); g_proxy_swapchain.Reset();
        UpdateProxyCursorClip(false);
        if (g_proxy_window) PostMessageW(g_proxy_window, WM_CLOSE, 0, 0);
        if (g_proxy_window_thread) CloseHandle(g_proxy_window_thread);
        if (g_proxy_window_ready) CloseHandle(g_proxy_window_ready);
        if (g_nr_output) g_nr_output->Release();
        g_captured_depth.Reset(); g_captured_motion.Reset(); g_captured_mask.Reset();
        g_fallback_depth.Reset(); g_fallback_motion.Reset(); g_guide_rtv_heap.Reset();
        ReleaseLegacyFrameResources();
        if (g_vulkan.ok && g_vulkan_semaphore != VK_NULL_HANDLE)
            g_vulkan.DestroySemaphore(g_vulkan.dev, g_vulkan_semaphore, nullptr);
        g_vulkan_semaphore = VK_NULL_HANDLE;
        if (g_vulkan.lib) FreeLibrary(g_vulkan.lib);
        g_vulkan = {};
        FeedVkHookRemove();
        g_legacy_query9.Reset(); g_legacy_device9.Reset();
        g_legacy_fence11.Reset(); g_legacy_fence12.Reset(); g_legacy_context4.Reset();
        g_legacy_context11.Reset(); g_legacy_device11.Reset();
        g_fg_stage.Reset(); g_sr_stage.Reset(); g_nr_stage.Reset(); g_packed_color.Reset();
        g_neural_list.Reset(); g_neural_allocator.Reset(); g_neural_fence.Reset(); g_neural_device.Reset();
        if (g_neural_fence_event) CloseHandle(g_neural_fence_event);
        if (g_command_queue) g_command_queue->Release();
        reshade::unregister_addon(module);
        DeleteCriticalSection(&g_log_lock);
    }
    return TRUE;
}
