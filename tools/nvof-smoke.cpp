#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <d3d12.h>
#include <dxgi1_6.h>
#include <wrl/client.h>
#include <cstdio>

#include "../addon/include/nvof-motion-provider.hpp"

using Microsoft::WRL::ComPtr;

static void PrintStatus(const char *message)
{
    std::printf("NVOF: %s\n", message ? message : "unknown");
}

static bool MakeSource(ID3D12Device *device, ComPtr<ID3D12Resource> &source)
{
    D3D12_HEAP_PROPERTIES heap = {};
    heap.Type = D3D12_HEAP_TYPE_DEFAULT;
    D3D12_RESOURCE_DESC texture = {};
    texture.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    texture.Width = 1280;
    texture.Height = 720;
    texture.DepthOrArraySize = 1;
    texture.MipLevels = 1;
    texture.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    texture.SampleDesc.Count = 1;
    return SUCCEEDED(device->CreateCommittedResource(&heap,
        D3D12_HEAP_FLAG_NONE, &texture, D3D12_RESOURCE_STATE_COMMON,
        nullptr, IID_PPV_ARGS(&source)));
}

static bool MakeOutput(ID3D12Device *device, ComPtr<ID3D12Resource> &output)
{
    D3D12_HEAP_PROPERTIES heap = {};
    heap.Type = D3D12_HEAP_TYPE_DEFAULT;
    D3D12_RESOURCE_DESC texture = {};
    texture.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    texture.Width = 1920;
    texture.Height = 1080;
    texture.DepthOrArraySize = 1;
    texture.MipLevels = 1;
    texture.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    texture.SampleDesc.Count = 1;
    texture.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
    return SUCCEEDED(device->CreateCommittedResource(&heap,
        D3D12_HEAP_FLAG_NONE, &texture,
        D3D12_RESOURCE_STATE_UNORDERED_ACCESS, nullptr,
        IID_PPV_ARGS(&output)));
}

int main()
{
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    ComPtr<IDXGIFactory6> factory;
    ComPtr<IDXGIAdapter1> adapter;
    ComPtr<ID3D12Device> device;
    if (FAILED(CreateDXGIFactory1(IID_PPV_ARGS(&factory)))) return 10;
    for (UINT index = 0; factory->EnumAdapters1(index, &adapter) != DXGI_ERROR_NOT_FOUND; ++index)
    {
        DXGI_ADAPTER_DESC1 description = {};
        adapter->GetDesc1(&description);
        if ((description.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) == 0 &&
            SUCCEEDED(D3D12CreateDevice(adapter.Get(), D3D_FEATURE_LEVEL_11_0,
                IID_PPV_ARGS(&device))))
            break;
        adapter.Reset();
    }
    if (!device)
    {
        std::puts("No hardware D3D12 adapter found.");
        return 11;
    }

    D3D12_COMMAND_QUEUE_DESC queue_description = {};
    queue_description.Type = D3D12_COMMAND_LIST_TYPE_COMPUTE;
    ComPtr<ID3D12CommandQueue> queue;
    ComPtr<ID3D12Fence> neural_fence;
    if (FAILED(device->CreateCommandQueue(&queue_description, IID_PPV_ARGS(&queue))) ||
        FAILED(device->CreateFence(0, D3D12_FENCE_FLAG_NONE,
            IID_PPV_ARGS(&neural_fence))))
        return 12;

    NvofMotionProvider provider;
    const bool initialized = provider.Initialize(device.Get(), queue.Get(),
        neural_fence.Get(), 1280, 720, DXGI_FORMAT_R8G8B8A8_UNORM,
        PrintStatus);
    bool full_path = initialized;
    ComPtr<ID3D12Resource> source_a, source_b, visualization;
    NvofMotionProvider::Submission first, second;
    if (full_path)
        full_path = MakeSource(device.Get(), source_a) &&
            MakeSource(device.Get(), source_b) &&
            MakeOutput(device.Get(), visualization);
    if (full_path)
    {
        std::puts("Submitting history frame...");
        full_path = provider.Submit(source_a.Get(), D3D12_RESOURCE_STATE_COMMON,
            1, true, first) && !first.valid;
    }
    if (full_path)
    {
        std::puts("Submitting flow frame...");
        full_path = provider.Submit(source_b.Get(), D3D12_RESOURCE_STATE_COMMON,
            2, false, second) && second.valid;
    }

    ComPtr<ID3D12CommandAllocator> allocator;
    ComPtr<ID3D12GraphicsCommandList> list;
    ComPtr<ID3D12Fence> done;
    HANDLE event = nullptr;
    if (full_path)
    {
        std::puts("Recording vector conversion...");
        full_path = SUCCEEDED(queue->Wait(provider.CompletionFence(),
            second.completion_value)) &&
            SUCCEEDED(device->CreateCommandAllocator(
                D3D12_COMMAND_LIST_TYPE_COMPUTE, IID_PPV_ARGS(&allocator))) &&
            SUCCEEDED(device->CreateCommandList(0,
                D3D12_COMMAND_LIST_TYPE_COMPUTE, allocator.Get(), nullptr,
                IID_PPV_ARGS(&list))) &&
            provider.RecordConversion(list.Get(), second, 3.0f, 0.35f) &&
            provider.RecordVisualization(list.Get(), second,
                visualization.Get(), 1, 16.0f) &&
            SUCCEEDED(list->Close()) &&
            SUCCEEDED(device->CreateFence(0, D3D12_FENCE_FLAG_NONE,
                IID_PPV_ARGS(&done)));
    }
    if (full_path)
    {
        ID3D12CommandList *lists[] = {list.Get()};
        queue->ExecuteCommandLists(1, lists);
        full_path = SUCCEEDED(queue->Signal(done.Get(), 1));
    }
    if (full_path)
    {
        event = CreateEventW(nullptr, FALSE, FALSE, nullptr);
        full_path = event && SUCCEEDED(done->SetEventOnCompletion(1, event)) &&
            WaitForSingleObject(event, 10000) == WAIT_OBJECT_0;
    }
    if (event) CloseHandle(event);
    std::printf("Result: %s (%s)\n", full_path ? "PASS" : "FAIL",
        provider.Status());
    provider.Shutdown();
    return full_path ? 0 : 20;
}
