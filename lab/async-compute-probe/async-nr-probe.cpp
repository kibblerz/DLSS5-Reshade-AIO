// Isolated asynchronous-compute probe for the private DLSS-NR feature.
//
// Reuse the deterministic contract laboratory in the same translation unit so
// this probe exercises exactly the same feature-18 parameters and validation.
// Only the queue used for resource upload, feature creation, evaluation and
// readback changes: DIRECT in nr-lab, COMPUTE here.

#define main nr_lab_embedded_main
#include "../nr-lab.cpp"
#undef main

static bool InstallComputeContext(
    ID3D12CommandQueue **direct_queue,
    ID3D12CommandAllocator **direct_allocator,
    ID3D12GraphicsCommandList **direct_list,
    ID3D12Fence **direct_fence,
    HANDLE *direct_fence_event,
    UINT64 *direct_fence_value)
{
    D3D12_COMMAND_QUEUE_DESC queue_desc = {};
    queue_desc.Type = D3D12_COMMAND_LIST_TYPE_COMPUTE;

    ID3D12CommandQueue *queue = nullptr;
    ID3D12CommandAllocator *allocator = nullptr;
    ID3D12GraphicsCommandList *list = nullptr;
    ID3D12Fence *fence = nullptr;
    HANDLE fence_event = nullptr;

    if (!Hr(g.device->CreateCommandQueue(&queue_desc, IID_PPV_ARGS(&queue)),
            "CreateCommandQueue(COMPUTE)") ||
        !Hr(g.device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_COMPUTE,
            IID_PPV_ARGS(&allocator)), "CreateCommandAllocator(COMPUTE)") ||
        !Hr(g.device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_COMPUTE,
            allocator, nullptr, IID_PPV_ARGS(&list)), "CreateCommandList(COMPUTE)") ||
        !Hr(list->Close(), "initial compute command list Close") ||
        !Hr(g.device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence)),
            "CreateFence(COMPUTE)")) {
        Release(fence);
        Release(list);
        Release(allocator);
        Release(queue);
        return false;
    }

    fence_event = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    if (fence_event == nullptr) {
        Log("FAIL CreateEvent(COMPUTE): %lu", GetLastError());
        Release(fence);
        Release(list);
        Release(allocator);
        Release(queue);
        return false;
    }

    // Transfer ownership of the original DIRECT submission objects out of the
    // shared context. The swapchain retains its own reference to the queue, but
    // keeping ours alive also makes teardown ordering explicit.
    *direct_queue = g.queue;
    *direct_allocator = g.allocator;
    *direct_list = g.list;
    *direct_fence = g.fence;
    *direct_fence_event = g.fence_event;
    *direct_fence_value = g.fence_value;

    g.queue = queue;
    g.allocator = allocator;
    g.list = list;
    g.fence = fence;
    g.fence_event = fence_event;
    g.fence_value = 0;
    Log("ASYNC PROBE: feature 18 will be created and evaluated on a COMPUTE queue");
    return true;
}

int main(int argc, char **argv)
{
    GetModuleFileNameA(nullptr, g_dir, MAX_PATH);
    if (char *slash = strrchr(g_dir, '\\')) *(slash + 1) = '\0';
    sprintf_s(g_log_path, "%sasync-nr-probe.log", g_dir);
    { FILE *file = nullptr; if (fopen_s(&file, g_log_path, "wb") == 0 && file != nullptr) std::fclose(file); }

    Options options;
    options.input_w = 960;
    options.input_h = 540;
    options.output_w = 960;
    options.output_h = 540;
    options.frames = 4;
    options.nr_only = true;

    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "--size") == 0 && i + 1 < argc) {
            if (!ParseSize(argv[++i], &options.input_w, &options.input_h)) {
                Log("invalid --size");
                return 64;
            }
            options.output_w = options.input_w;
            options.output_h = options.input_h;
        } else if (strcmp(argv[i], "--frames") == 0 && i + 1 < argc) {
            options.frames = std::max(1u, static_cast<UINT>(strtoul(argv[++i], nullptr, 10)));
        } else if (strcmp(argv[i], "--model") == 0 && i + 1 < argc) {
            options.model = atoi(argv[++i]);
        } else if (strcmp(argv[i], "--full") == 0) {
            options.nr_only = false;
            options.output_w = options.input_w * 2;
            options.output_h = options.input_h * 2;
        } else if (strcmp(argv[i], "--framegen") == 0) {
            options.nr_only = false;
            options.framegen = true;
            options.output_w = options.input_w * 2;
            options.output_h = options.input_h * 2;
        } else if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
            std::printf("async-nr-probe [--size 960x540] [--frames N] [--model 1|2|3] [--full] [--framegen]\n");
            return 0;
        } else {
            Log("unknown argument: %s", argv[i]);
            return 64;
        }
    }
    if (options.model < 1 || options.model > 3) {
        Log("invalid --model (use 1, 2, or 3)");
        return 64;
    }

    Log("DLSS-NR asynchronous-compute probe build %s %s", __DATE__, __TIME__);
    Log("probe dimensions=%ux%u -> %ux%u frames=%u model=%d stages=%s", options.input_w,
        options.input_h, options.output_w, options.output_h, options.frames, options.model,
        options.nr_only ? "NR" : (options.framegen ? "NR+SR+FG" : "NR+SR"));

    int code = 1;
    ID3D12CommandQueue *direct_queue = nullptr;
    ID3D12CommandAllocator *direct_allocator = nullptr;
    ID3D12GraphicsCommandList *direct_list = nullptr;
    ID3D12Fence *direct_fence = nullptr;
    HANDLE direct_fence_event = nullptr;
    UINT64 direct_fence_value = 0;

    if (InitNvApi() && InitD3D12() && InitPresentBoundary() && InitNgx() &&
        InstallComputeContext(&direct_queue, &direct_allocator, &direct_list,
            &direct_fence, &direct_fence_event, &direct_fence_value)) {
        code = Run(options);
        Log("ASYNC PROBE VERDICT: %s", code == 0
            ? "PASS - raw DLSS-NR accepts COMPUTE queue submission"
            : "FAIL - inspect the first failed compute operation above");
    } else {
        Log("ASYNC PROBE VERDICT: FAIL - environment or compute queue initialization failed");
    }

    Shutdown();
    if (direct_fence_event != nullptr) CloseHandle(direct_fence_event);
    Release(direct_fence);
    Release(direct_list);
    Release(direct_allocator);
    Release(direct_queue);
    (void)direct_fence_value;
    return code;
}
