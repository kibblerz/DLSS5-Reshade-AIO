#include <windows.h>
#include <d3d12.h>

using NgxSnippetInitD3D12Ext = unsigned long (__cdecl *)(unsigned long long,
    const wchar_t *, ID3D12Device *, unsigned long, const void *);
using NgxCreateFeature = unsigned long (__cdecl *)(ID3D12GraphicsCommandList *,
    int, void *, void **);
using NgxEvaluateFeature = unsigned long (__cdecl *)(ID3D12GraphicsCommandList *,
    const void *, const void *, void *);
using NgxReleaseFeature = unsigned long (__cdecl *)(void *);
using NgxShutdownD3D12 = unsigned long (__cdecl *)(ID3D12Device *);
using NgxPopulateParameters = unsigned long (__cdecl *)(void *);

// NVIDIA's DLSS-NR snippet verifies that Init_Ext was called from a module whose
// filename contains "nvngx.dll". Keep this as a real, non-tail-called boundary:
// the return address observed by the snippet must remain inside this module.
#pragma optimize("", off)
extern "C" __declspec(dllexport) unsigned long __cdecl NVNGXBridge_D3D12_InitExt(
    NgxSnippetInitD3D12Ext init,
    unsigned long long application_id,
    const wchar_t *application_data_path,
    ID3D12Device *device,
    unsigned long api_version,
    const void *feature_common_info)
{
    if (init == nullptr) return 0xBAD00005UL;
    volatile unsigned long result = init(application_id, application_data_path,
        device, api_version, feature_common_info);
    MemoryBarrier();
    return result;
}

extern "C" __declspec(dllexport) unsigned long __cdecl NVNGXBridge_D3D12_PopulateParameters(
    NgxPopulateParameters populate, void *parameters)
{
    if (populate == nullptr || parameters == nullptr) return 0xBAD00005UL;
    volatile unsigned long result = populate(parameters);
    MemoryBarrier();
    return result;
}

extern "C" __declspec(dllexport) unsigned long __cdecl NVNGXBridge_D3D12_CreateFeature(
    NgxCreateFeature create, ID3D12GraphicsCommandList *command_list, int feature,
    void *parameters, void **handle)
{
    if (create == nullptr) return 0xBAD00005UL;
    volatile unsigned long result = create(command_list, feature, parameters, handle);
    MemoryBarrier();
    return result;
}

extern "C" __declspec(dllexport) unsigned long __cdecl NVNGXBridge_D3D12_EvaluateFeature(
    NgxEvaluateFeature evaluate, ID3D12GraphicsCommandList *command_list,
    const void *handle, const void *parameters, void *progress_callback)
{
    if (evaluate == nullptr) return 0xBAD00005UL;
    volatile unsigned long result = evaluate(command_list, handle, parameters, progress_callback);
    MemoryBarrier();
    return result;
}

extern "C" __declspec(dllexport) unsigned long __cdecl NVNGXBridge_D3D12_ReleaseFeature(
    NgxReleaseFeature release, void *handle)
{
    if (release == nullptr) return 0xBAD00005UL;
    volatile unsigned long result = release(handle);
    MemoryBarrier();
    return result;
}

extern "C" __declspec(dllexport) unsigned long __cdecl NVNGXBridge_D3D12_Shutdown1(
    NgxShutdownD3D12 shutdown, ID3D12Device *device)
{
    if (shutdown == nullptr) return 0xBAD00005UL;
    volatile unsigned long result = shutdown(device);
    MemoryBarrier();
    return result;
}
#pragma optimize("", on)

BOOL WINAPI DllMain(HINSTANCE, DWORD, LPVOID)
{
    return TRUE;
}
