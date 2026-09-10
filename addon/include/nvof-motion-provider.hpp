#pragma once

#include <array>
#include <cstdint>
#include <string>

#include <d3d12.h>
#include <dxgi1_6.h>
#include <wrl/client.h>

#include "nvOpticalFlowD3D12.h"

class NvofMotionProvider
{
public:
    // Capacity for the previous reference, staged flow, neural consumption,
    // and split-FG consumption without transiently losing the motion provider.
    static constexpr unsigned int kSlotCount = 5;

    struct Submission
    {
        bool valid = false;
        unsigned int slot = ~0u;
        std::uint64_t completion_value = 0;
        ID3D12Resource *motion = nullptr;
        ID3D12Resource *history_mask = nullptr;
        ID3D12Resource *depth = nullptr;
    };

    using LogCallback = void (*)(const char *message);

    NvofMotionProvider() = default;
    ~NvofMotionProvider();
    NvofMotionProvider(const NvofMotionProvider &) = delete;
    NvofMotionProvider &operator=(const NvofMotionProvider &) = delete;

    bool Initialize(ID3D12Device *device, ID3D12CommandQueue *compute_queue,
        ID3D12Fence *neural_fence, unsigned int width, unsigned int height,
        unsigned int flow_width, unsigned int flow_height,
        DXGI_FORMAT source_format, LogCallback log);
    void Shutdown();
    void ResetHistory();

    // The caller must have queued any dependency which makes 'source' readable
    // on compute_queue before calling this method. It records a compact RGBA8
    // copy, submits NVOFA asynchronously, and queues no CPU wait.
    bool Submit(ID3D12Resource *source, D3D12_RESOURCE_STATES source_state,
        std::uint64_t source_sequence, bool reset, Submission &submission);

    // Queue this fence wait before submitting the command list that contains
    // RecordConversion. NVOFA signals it after both flow directions are ready.
    ID3D12Fence *CompletionFence() const { return completion_fence_.Get(); }
    bool IsComplete(const Submission &submission) const;
    bool LastSubmitWasBackpressured() const { return last_submit_backpressured_; }

    // Converts S10.5 forward/backward flow into full-resolution pixel motion
    // and produces a current-frame-bias mask from flow consistency and cost.
    bool RecordConversion(ID3D12GraphicsCommandList *commands,
        const Submission &submission, float consistency_threshold_pixels,
        float cost_threshold, ID3D12Resource *geometry_depth = nullptr,
        D3D12_RESOURCE_STATES geometry_depth_state = D3D12_RESOURCE_STATE_COMMON,
        bool depth_reversed = true, float motion_repair_strength = 0.0f);
    bool RecordVisualization(ID3D12GraphicsCommandList *commands,
        const Submission &submission, ID3D12Resource *output,
        unsigned int mode, float magnitude_scale);
    void MarkNeuralUse(const Submission &submission, std::uint64_t neural_fence_value);
    void MarkConsumerUse(const Submission &submission, ID3D12Fence *fence,
        std::uint64_t fence_value);

    bool IsReady() const { return ready_; }
    bool IsIdle() const;
    unsigned int GridSize() const { return grid_size_; }
    const char *Status() const { return status_.c_str(); }

private:
    struct Slot
    {
        Microsoft::WRL::ComPtr<ID3D12CommandAllocator> prep_allocator;
        Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList> prep_list;
        Microsoft::WRL::ComPtr<ID3D12Resource> input;
        Microsoft::WRL::ComPtr<ID3D12Resource> forward;
        Microsoft::WRL::ComPtr<ID3D12Resource> backward;
        Microsoft::WRL::ComPtr<ID3D12Resource> forward_cost;
        Microsoft::WRL::ComPtr<ID3D12Resource> backward_cost;
        Microsoft::WRL::ComPtr<ID3D12Resource> motion;
        Microsoft::WRL::ComPtr<ID3D12Resource> history_mask;
        Microsoft::WRL::ComPtr<ID3D12Resource> geometry_depth;
        NvOFGPUBufferHandle input_handle = nullptr;
        NvOFGPUBufferHandle forward_handle = nullptr;
        NvOFGPUBufferHandle backward_handle = nullptr;
        NvOFGPUBufferHandle forward_cost_handle = nullptr;
        NvOFGPUBufferHandle backward_cost_handle = nullptr;
        std::uint64_t prep_fence_value = 0;
        std::uint64_t flow_fence_value = 0;
        std::uint64_t neural_fence_value = 0;
        Microsoft::WRL::ComPtr<ID3D12Fence> consumer_fence;
        std::uint64_t consumer_fence_value = 0;
        bool converted_once = false;
    };

    bool LoadApi();
    bool CreatePipelineState();
    bool CreateSessionResources();
    bool RegisterResource(ID3D12Resource *resource, NvOFGPUBufferHandle &handle);
    void UnregisterResource(NvOFGPUBufferHandle &handle);
    int AcquireSlot() const;
    void SetStatus(const char *format, ...);
    void Log(const char *format, ...) const;

    HMODULE module_ = nullptr;
    NV_OF_D3D12_API_FUNCTION_LIST api_ = {};
    NvOFHandle session_ = nullptr;
    Microsoft::WRL::ComPtr<ID3D12Device> device_;
    Microsoft::WRL::ComPtr<ID3D12CommandQueue> compute_queue_;
    Microsoft::WRL::ComPtr<ID3D12Fence> neural_fence_;
    Microsoft::WRL::ComPtr<ID3D12Fence> prep_fence_;
    Microsoft::WRL::ComPtr<ID3D12Fence> completion_fence_;
    Microsoft::WRL::ComPtr<ID3D12Fence> registration_fence_;
    Microsoft::WRL::ComPtr<ID3D12RootSignature> prep_root_;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> prep_pipeline_;
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> prep_descriptors_;
    Microsoft::WRL::ComPtr<ID3D12RootSignature> conversion_root_;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> conversion_pipeline_;
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> conversion_descriptors_;
    Microsoft::WRL::ComPtr<ID3D12RootSignature> visualization_root_;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> visualization_pipeline_;
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> visualization_descriptors_;
    std::array<Slot, kSlotCount> slots_ = {};
    LogCallback log_ = nullptr;
    std::string status_ = "disabled";
    unsigned int width_ = 0;
    unsigned int height_ = 0;
    unsigned int flow_width_ = 0;
    unsigned int flow_height_ = 0;
    unsigned int grid_size_ = 1;
    unsigned int prep_descriptor_stride_ = 0;
    unsigned int conversion_descriptor_stride_ = 0;
    unsigned int visualization_descriptor_stride_ = 0;
    DXGI_FORMAT source_format_ = DXGI_FORMAT_UNKNOWN;
    DXGI_FORMAT input_format_ = DXGI_FORMAT_UNKNOWN;
    DXGI_FORMAT flow_format_ = DXGI_FORMAT_UNKNOWN;
    DXGI_FORMAT cost_format_ = DXGI_FORMAT_UNKNOWN;
    std::uint64_t prep_fence_value_ = 0;
    std::uint64_t completion_fence_value_ = 0;
    std::uint64_t registration_fence_value_ = 0;
    std::uint64_t previous_sequence_ = 0;
    int previous_slot_ = -1;
    bool last_submit_backpressured_ = false;
    bool ready_ = false;
};
