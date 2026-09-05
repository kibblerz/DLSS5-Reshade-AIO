#pragma once

#include <windows.h>
#include <cstdint>

// Read-only public contract consumed by tools/DLSS5.PerformanceAnalyzer.
// Writers use Sequence as a seqlock: odd while updating, even when stable.
// Add fields only at the end and increment Version when compatibility breaks.
namespace dlss5_aio_telemetry
{
constexpr std::uint32_t Magic = 0x4F494144; // "DAIO" in little endian
constexpr std::uint32_t Version = 1;
constexpr wchar_t MappingPrefix[] = L"Local\\DLSS5_AIO_Telemetry_";

enum Flags : std::uint32_t
{
    AddonEnabled = 1u << 0,
    NeuralRenderingEnabled = 1u << 1,
    FrameGenerationEnabled = 1u << 2,
    NeuralPipelineReady = 1u << 3,
    FrameGenerationFailed = 1u << 4,
    ProcessedOutputVisible = 1u << 5,
    VortGuidesEnabled = 1u << 6,
    DlaaMode = 1u << 7,
    ProxyHidden = 1u << 8,
    ProxyFailed = 1u << 9,
    SynchronousPresenter = 1u << 10,
    ReshadeOverlayOpen = 1u << 11,
    GpuTelemetryAvailable = 1u << 12,
    ProxyGpuTelemetryAvailable = 1u << 13,
    GuideGpuTelemetryAvailable = 1u << 14,
};

enum class BenchmarkMode : std::uint32_t
{
    UserSettings = 0,
    AddonDisabled = 1,
    DlssOnly = 2,
    NrDlss = 3,
    DlssFrameGeneration = 4,
    NrDlssFrameGeneration = 5,
};

#pragma pack(push, 8)
struct SnapshotV1
{
    std::uint32_t MagicValue;
    std::uint32_t VersionValue;
    std::uint32_t StructSize;
    std::uint32_t ProcessId;
    volatile LONG64 Sequence;
    std::int64_t QpcTimestamp;
    std::int64_t QpcFrequency;
    std::uint32_t BenchmarkEpoch;
    std::uint32_t BenchmarkModeValue;
    std::uint32_t FlagsValue;
    std::uint32_t GraphicsApi;
    std::uint32_t InputWidth;
    std::uint32_t InputHeight;
    std::uint32_t OutputWidth;
    std::uint32_t OutputHeight;
    std::uint32_t SourceFps;
    std::uint32_t ProxyFps;
    std::uint32_t ActiveNrModel;
    std::uint32_t DlssPreset;
    std::uint32_t PipelineSlotStates;
    std::uint32_t Reserved0;
    std::uint32_t GpuPrepUs;
    std::uint32_t GpuNrUs;
    std::uint32_t GpuSrUs;
    std::uint32_t GpuFgUs;
    std::uint32_t GpuCleanupUs;
    std::uint32_t GpuTotalUs;
    std::uint32_t GpuVortUs;
    std::uint32_t GpuFeedUs;
    std::uint32_t GpuGuidesTotalUs;
    std::uint32_t GpuProxyGeneratedUs;
    std::uint32_t GpuProxyRealUs;
    std::uint32_t GpuProxyTotalUs;
    std::uint32_t AddonCpuCurrentUs;
    std::uint32_t AddonCpuAverageUs;
    std::uint32_t SourceFrameAverageUs;
    std::uint32_t SourceFrameP99Us;
    std::uint32_t CpuProxyMailboxUs;
    std::uint32_t CpuProxyFenceWaitUs;
    std::uint32_t CpuProxySwapWaitUs;
    std::uint32_t CpuProxyPresentUs;
    std::uint32_t CpuProxyWorkerUs;
    std::uint32_t CpuSharedTelemetryUs;
    std::uint64_t SourceFrameSequence;
    std::uint64_t LastNeuralSourceSequence;
    std::uint64_t NrFrames;
    std::uint64_t SrFrames;
    std::uint64_t FgFrames;
    std::uint64_t FramesPresented;
    std::uint64_t NeuralSkips;
    std::uint64_t ProxySkips;
    std::uint64_t ProxyCoalesced;
    std::uint64_t ProxyTimeouts;
    std::uint64_t DisplayBackpressureDrops;
    std::uint64_t TemporalDiscontinuities;
    std::uint64_t ProxyRequests;
    std::uint64_t ProxyCompleted;
    std::uint64_t TelemetrySamples;
    std::uint64_t PrimarySwapchainAddress;
    std::uint64_t ProxySwapchainAddress;
};
#pragma pack(pop)

static_assert(sizeof(SnapshotV1) == 320, "Telemetry ABI changed unexpectedly");
}
