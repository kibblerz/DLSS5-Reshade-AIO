#pragma once

#include <cstddef>
#include <cstring>

// Authoritative, architecture-neutral definition of the user-facing AIO menu.
// The x64 add-on applies these settings directly. The x86 wrapper renders the
// same schema and writes the same keys into the carrier's ReShade.ini.
namespace dlss5_aio_menu
{
inline constexpr const char *kConfigSection = "Standalone.DLSSNR";

enum NRKind { NR_BOOL, NR_COMBO, NR_FLOAT };
enum class Group { General, Neural, Output, Compatibility };

struct NRSetting
{
    const char        *key;
    const char        *label;
    NRKind             kind;
    float              def, lo, hi;
    const char        *format;
    const char *const *items;
    int                item_count;
    const char        *tooltip;
    Group              group;
};

inline constexpr const char *kColorItems[] = {
    "Auto (swapchain + format)", "sRGB (nonlinear BT.709)", "Linear BT.709 / scRGB",
    "BT.2100 PQ / HDR10", "BT.2100 HLG" };
inline constexpr const char *kModelItems[] = { "Model 1", "Model 2", "Model 3" };
inline constexpr const char *kPresetItems[] = {
    "Default (NVIDIA)", "Preset J", "Preset K", "Preset L (Recommended default)", "Preset M" };
inline constexpr const char *kSourceItems[] = {
    "Disabled (use game backbuffer)", "16:9 - 960 x 540", "16:9 - 1280 x 720",
    "16:9 - 1600 x 900", "16:9 - 1920 x 1080", "16:9 - 2560 x 1440",
    "16:9 - 3200 x 1800", "16:10 - 1280 x 800", "16:10 - 1440 x 900",
    "16:10 - 1680 x 1050", "16:10 - 1920 x 1200", "16:10 - 2560 x 1600",
    "21:9 - 1280 x 540", "21:9 - 1720 x 720", "21:9 - 1920 x 800",
    "21:9 - 2560 x 1080", "21:9 - 3440 x 1440", "32:9 - 1920 x 540",
    "32:9 - 2560 x 720", "32:9 - 3840 x 1080", "32:9 - 5120 x 1440",
    "4:3 - 960 x 720", "4:3 - 1280 x 960", "4:3 - 1440 x 1080",
    "4:3 - 1600 x 1200", "5:4 - 1280 x 1024" };

inline constexpr NRSetting kSettings[] = {
    { "Enabled", "Enable addon", NR_BOOL, 1, 0, 1, nullptr, nullptr, 0,
      "Enables the complete standalone presentation pipeline.", Group::General },
    { "InputColorProfile", "Input color profile", NR_COMBO, 0, 0, 4, nullptr, kColorItems, 5,
      "Auto is recommended. Select a manual profile only when the game reports its output color space incorrectly.", Group::General },
    { "SourceResolutionOverride", "Pipeline source resolution override", NR_COMBO, 0, 0, 25, nullptr, kSourceItems, 26,
      "Use when source resolution is detected incorrectly, or to downsample before NR without lowering the game's own resolution.", Group::General },
    { "DlssRenderPreset", "DLSS render preset", NR_COMBO, 3, 0, 4, nullptr, kPresetItems, 5,
      "Preset L is recommended and showed the least smearing in testing.", Group::General },

    { "NeuralRendering", "Enable Neural Rendering", NR_BOOL, 1, 0, 1, nullptr, nullptr, 0,
      "Off skips NR while leaving DLSS/DLAA and optional Frame Generation available.", Group::Neural },
    { "Model", "DLSS-NR model", NR_COMBO, 0, 0, 2, nullptr, kModelItems, 3,
      "Selects one of the three available Neural Rendering models.", Group::Neural },
    { "Intensity", "NR intensity", NR_FLOAT, 1, 0, 2, "%.2f", nullptr, 0, nullptr, Group::Neural },
    { "LocalTone", "Local tone strength", NR_FLOAT, 1, 0, 2, "%.2f", nullptr, 0, nullptr, Group::Neural },
    { "LocalStructure", "Local structure strength", NR_FLOAT, 1, 0, 2, "%.2f", nullptr, 0, nullptr, Group::Neural },
    { "SkinStructure", "Skin / character structure", NR_FLOAT, -1, -1, 1, "%.2f", nullptr, 0, nullptr, Group::Neural },
    { "NvidiaOpticalFlowMotion", "NVIDIA Optical Flow motion (experimental)", NR_BOOL, 0, 0, 1, nullptr, nullptr, 0,
      "Off by default. Uses the NVIDIA driver's hardware Optical Flow engine to derive motion from captured source frames; no game profile or VORT shader is required.", Group::Neural },
    { "NvidiaOpticalFlowDepth", "Add ReShade depth geometry to Optical Flow (prototype)", NR_BOOL, 0, 0, 1, nullptr, nullptr, 0,
      "Opt-in D3D11 prototype. Rejects optical-flow history across object depth boundaries without requiring VORT. Restart after changing.", Group::Neural },
    { "NvidiaOpticalFlowMotionRepair", "Geometry/confidence motion repair", NR_FLOAT, 1, 0, 1, "%.2f", nullptr, 0,
      "With geometry enabled, attenuates unreliable motion before it reaches NR, DLSS, and Frame Generation. Zero preserves the original vectors.", Group::Neural },
    { "NvidiaOpticalFlowConsistency", "Optical Flow consistency tolerance", NR_FLOAT, 3, 0.5f, 12, "%.1f px", nullptr, 0,
      "Higher values trust more motion; lower values reject more inconsistent forward/backward flow from DLSS history.", Group::Neural },
    { "NvidiaOpticalFlowCost", "Optical Flow cost tolerance", NR_FLOAT, 0.35f, 0, 0.99f, "%.2f", nullptr, 0,
      "Higher values accept lower-confidence motion. This affects DLSS history rejection, not the NR control mask.", Group::Neural },
    { "VortGuides", "Enable VORT motion integration (experimental)", NR_BOOL, 0, 0, 1, nullptr, nullptr, 0,
      "Off by default. VORT motion/guide conversion may improve temporal quality but can have a large performance cost.", Group::Neural },
    { "NrRejectionMask", "VORT NR rejection mask (experimental)", NR_BOOL, 0, 0, 1, nullptr, nullptr, 0,
      "Only applies while VORT motion integration is enabled.", Group::Neural },
    { "NrRejectionStrength", "NR rejection strength", NR_FLOAT, 1, 0, 1, "%.2f", nullptr, 0,
      "Higher values reject more NR history near unreliable motion and depth edges.", Group::Neural },
    { "ResetEveryFrame", "Reset temporal history every frame", NR_BOOL, 0, 0, 1, nullptr, nullptr, 0,
      "Diagnostic option that disables persistent temporal history.", Group::Neural },
    { "StableSrHistory", "Stable DLSS SR (no persistent SR history)", NR_BOOL, 0, 0, 1, nullptr, nullptr, 0,
      "Off by default; enable only as a per-frame SR-history diagnostic.", Group::Neural },
    { "FrameGeneration", "Experimental DLSS Frame Generation (2x)", NR_BOOL, 1, 0, 1, nullptr, nullptr, 0,
      "Presents one generated frame followed by one reconstructed real frame.", Group::Neural },
    { "AsyncComputePipeline", "Asynchronous NGX compute (experimental)", NR_BOOL, 1, 0, 1, nullptr, nullptr, 0,
      "Runs NGX work on a compute queue so it can overlap game graphics. Restart after changing it.", Group::Neural },

    { "CompositeReshade", "Composite ReShade menu while open", NR_BOOL, 1, 0, 1, nullptr, nullptr, 0, nullptr, Group::Output },
    { "ShowProxyFps", "Show native output FPS counter", NR_BOOL, 1, 0, 1, nullptr, nullptr, 0, nullptr, Group::Output },
    { "AdaptivePressureGovernor", "Adaptive GPU pressure governor (prototype)", NR_BOOL, 1, 0, 1, nullptr, nullptr, 0,
      "Limits source Presents only after sustained pipeline starvation, then probes for recovered capacity.", Group::Output },
    { "SuppressQueuePressureWarning", "Hide queue-full performance warning", NR_BOOL, 0, 0, 1, nullptr, nullptr, 0,
      "Hides the recommendation to lower the game's frame cap or render resolution.", Group::Output },
    { "PerformanceTelemetry", "Collect performance telemetry", NR_BOOL, 1, 0, 1, nullptr, nullptr, 0,
      "Collects asynchronous GPU timestamps and pipeline pacing measurements.", Group::Output },

    { "AutoWindowedVirtualization", "Automatic non-invasive presentation", NR_BOOL, 1, 0, 1, nullptr, nullptr, 0,
      "Recommended. Leaves reduced game windows untouched and displays finished native output separately.", Group::Compatibility },
    { "WindowedVirtualization", "Force reduced-window virtualization", NR_BOOL, 0, 0, 1, nullptr, nullptr, 0,
      "Try when the game image occupies only part of the screen. This physically enlarges the game window.", Group::Compatibility },
    { "WindowedLogicalSizeMessages", "Virtualize logical client size and coordinates", NR_BOOL, 0, 0, 1, nullptr, nullptr, 0,
      "Use with forced window virtualization if a game becomes stretched or jumps back to native rendering.", Group::Compatibility },
    { "WindowedInputScaling", "Scale window input coordinates to render resolution", NR_BOOL, 0, 0, 1, nullptr, nullptr, 0,
      "Try when mouse clicks land in the wrong place. Requires and automatically enables forced window virtualization.", Group::Compatibility },
    { "DpiPhysicalOutputCorrection", "Correct DPI-virtualized native resolution", NR_BOOL, 0, 0, 1, nullptr, nullptr, 0,
      "Try when the detected native resolution is wrong because of Windows display scaling. Restart required.", Group::Compatibility },
    { "DetachedPresentation", "Detached native output (Vulkan compatibility)", NR_BOOL, 0, 0, 1, nullptr, nullptr, 0,
      "Try when processed output is black, missing, or trapped inside the original game window. Restart required.", Group::Compatibility },
    { "HideDetachedSystemCursor", "Hide detached Windows cursor", NR_BOOL, 0, 0, 1, nullptr, nullptr, 0,
      "Force-hides a duplicate system cursor when the game does not report cursor visibility correctly.", Group::Compatibility },
    { "OpaqueComposition", "Opaque attached composition", NR_BOOL, 0, 0, 1, nullptr, nullptr, 0,
      "Try when original and processed pictures appear layered together. Restart required.", Group::Compatibility },
    { "SynchronousProxyPresentation", "Serialized presentation (crash workaround)", NR_BOOL, 0, 0, 1, nullptr, nullptr, 0,
      "Try when processed output crashes, freezes, or remains black. It may reduce performance. Restart required.", Group::Compatibility },
    { "EarlyProxyInitialization", "Early proxy initialization (D3D11On12 compatibility)", NR_BOOL, 0, 0, 1, nullptr, nullptr, 0,
      "Last-resort option when the addon remains waiting for Present. It can reduce compatibility. Restart required.", Group::Compatibility },
};

inline constexpr std::size_t kSettingCount = sizeof(kSettings) / sizeof(kSettings[0]);

inline const NRSetting *Find(const char *key)
{
    for (const NRSetting &setting : kSettings)
        if (std::strcmp(setting.key, key) == 0)
            return &setting;
    return nullptr;
}

inline const char *Label(const char *key, const char *fallback)
{
    const NRSetting *setting = Find(key);
    return setting != nullptr ? setting->label : fallback;
}
}
