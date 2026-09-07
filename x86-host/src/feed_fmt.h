// feed_fmt.h - the DXGI format decisions the transports share.
// Copied from DLSS5-Feeder, copyright (c) 2026 Jean-Laurent ROUZIES,
// under the MIT License in external/DLSS5-Feeder/LICENSE.
//
// Three files need the same answers and must not drift apart: the 64-bit add-on
// (which still carries its own copies, since its D3D11/D3D12 paths were written
// against them), the 32-bit add-on's Vulkan branch, and the host -- which, for a
// client whose API cannot export shared memory, CREATES the textures and so has to
// pick the output format itself.
//
// Everything here is a pure DXGI_FORMAT -> DXGI_FORMAT function. The one decision
// that needs a live device (does this GPU support a typed UAV store to BGRA8?) stays
// with the caller that has one; see the host's ResolveOutputFormatHost.
//
// Prefixed FeedFmt* so a file that already has its own TypedColorFormat -- the proven
// 32-bit D3D11 path does, and keeps it this release -- can include this header
// without a collision.

#pragma once
#include <dxgiformat.h>

static const char *FeedFmtName(DXGI_FORMAT f)
{
    switch (f)
    {
    case DXGI_FORMAT_R16G16B16A16_FLOAT:    return "R16G16B16A16_FLOAT";
    case DXGI_FORMAT_R16G16B16A16_TYPELESS: return "R16G16B16A16_TYPELESS";
    case DXGI_FORMAT_R11G11B10_FLOAT:       return "R11G11B10_FLOAT";
    case DXGI_FORMAT_R10G10B10A2_UNORM:     return "R10G10B10A2_UNORM";
    case DXGI_FORMAT_R10G10B10A2_TYPELESS:  return "R10G10B10A2_TYPELESS";
    case DXGI_FORMAT_R8G8B8A8_UNORM:        return "R8G8B8A8_UNORM";
    case DXGI_FORMAT_R8G8B8A8_UNORM_SRGB:   return "R8G8B8A8_UNORM_SRGB";
    case DXGI_FORMAT_R8G8B8A8_TYPELESS:     return "R8G8B8A8_TYPELESS";
    case DXGI_FORMAT_B8G8R8A8_UNORM:        return "B8G8R8A8_UNORM";
    case DXGI_FORMAT_B8G8R8A8_UNORM_SRGB:   return "B8G8R8A8_UNORM_SRGB";
    case DXGI_FORMAT_B8G8R8A8_TYPELESS:     return "B8G8R8A8_TYPELESS";
    case DXGI_FORMAT_B8G8R8X8_UNORM:        return "B8G8R8X8_UNORM";
    case DXGI_FORMAT_R16G16_FLOAT:          return "R16G16_FLOAT";
    case DXGI_FORMAT_R32_FLOAT:             return "R32_FLOAT";
    default:                                return "?";
    }
}

// The shared Color copy must be typed (the DLSS 5 add-on samples it) and in the same
// typeless family as the backbuffer, so the frame can be moved across by a raw copy.
static DXGI_FORMAT FeedFmtTypedColor(DXGI_FORMAT f)
{
    switch (f)
    {
    case DXGI_FORMAT_R8G8B8A8_TYPELESS: case DXGI_FORMAT_R8G8B8A8_UNORM: case DXGI_FORMAT_R8G8B8A8_UNORM_SRGB:
        return DXGI_FORMAT_R8G8B8A8_UNORM;
    case DXGI_FORMAT_B8G8R8A8_TYPELESS: case DXGI_FORMAT_B8G8R8A8_UNORM: case DXGI_FORMAT_B8G8R8A8_UNORM_SRGB:
        return DXGI_FORMAT_B8G8R8A8_UNORM;
    case DXGI_FORMAT_B8G8R8X8_TYPELESS: case DXGI_FORMAT_B8G8R8X8_UNORM: case DXGI_FORMAT_B8G8R8X8_UNORM_SRGB:
        return DXGI_FORMAT_B8G8R8X8_UNORM;
    case DXGI_FORMAT_R10G10B10A2_TYPELESS: case DXGI_FORMAT_R10G10B10A2_UNORM:
        return DXGI_FORMAT_R10G10B10A2_UNORM;
    case DXGI_FORMAT_R16G16B16A16_TYPELESS: case DXGI_FORMAT_R16G16B16A16_FLOAT:
        return DXGI_FORMAT_R16G16B16A16_FLOAT;
    case DXGI_FORMAT_R11G11B10_FLOAT:
        return DXGI_FORMAT_R11G11B10_FLOAT;
    default:
        return DXGI_FORMAT_UNKNOWN;
    }
}

// The output must keep the backbuffer's channel order. When it does not, the copy
// home has to convert, and on Vulkan that conversion is vkCmdBlitImage -- which is
// sRGB-aware, so writing our (linear-typed) output into a VK_FORMAT_*_SRGB swapchain
// applies a linear->sRGB encode and the whole image comes back washed out (issue #11).
// DXVK swapchains are almost always BGRA8, which is exactly why this mapping matters
// on the 32-bit Vulkan path.
static DXGI_FORMAT FeedFmtOutputFor(DXGI_FORMAT color_typed)
{
    switch (color_typed)
    {
    case DXGI_FORMAT_R16G16B16A16_FLOAT: return DXGI_FORMAT_R16G16B16A16_FLOAT;
    case DXGI_FORMAT_R11G11B10_FLOAT:    return DXGI_FORMAT_R16G16B16A16_FLOAT;
    case DXGI_FORMAT_R10G10B10A2_UNORM:  return DXGI_FORMAT_R10G10B10A2_UNORM;
    case DXGI_FORMAT_B8G8R8A8_UNORM:     return DXGI_FORMAT_B8G8R8A8_UNORM;
    case DXGI_FORMAT_B8G8R8X8_UNORM:     return DXGI_FORMAT_B8G8R8A8_UNORM;   // X8 has no alpha to preserve
    default:                             return DXGI_FORMAT_R8G8B8A8_UNORM;
    }
}

// Channel order and bit layout only, ignoring the transfer function: an _SRGB
// backbuffer and our UNORM output ARE interchangeable for a raw copy, and copying
// them raw is exactly the point -- the bytes must land unconverted.
static int FeedFmtTexelFamily(DXGI_FORMAT f)
{
    switch (f)
    {
    case DXGI_FORMAT_R8G8B8A8_TYPELESS: case DXGI_FORMAT_R8G8B8A8_UNORM: case DXGI_FORMAT_R8G8B8A8_UNORM_SRGB:
        return 1;
    case DXGI_FORMAT_B8G8R8A8_TYPELESS: case DXGI_FORMAT_B8G8R8A8_UNORM: case DXGI_FORMAT_B8G8R8A8_UNORM_SRGB:
    case DXGI_FORMAT_B8G8R8X8_TYPELESS: case DXGI_FORMAT_B8G8R8X8_UNORM: case DXGI_FORMAT_B8G8R8X8_UNORM_SRGB:
        return 2;
    case DXGI_FORMAT_R10G10B10A2_TYPELESS: case DXGI_FORMAT_R10G10B10A2_UNORM:
        return 3;
    case DXGI_FORMAT_R16G16B16A16_TYPELESS: case DXGI_FORMAT_R16G16B16A16_FLOAT:
        return 4;
    case DXGI_FORMAT_R11G11B10_FLOAT:
        return 5;
    default:
        return 0;   // unknown: never claim a raw copy is safe
    }
}

static bool FeedFmtSameTexelLayout(DXGI_FORMAT a, DXGI_FORMAT b)
{
    const int fa = FeedFmtTexelFamily(a);
    return fa != 0 && fa == FeedFmtTexelFamily(b);
}

static bool FeedFmtIsHdr(DXGI_FORMAT typed)
{
    return typed == DXGI_FORMAT_R16G16B16A16_FLOAT || typed == DXGI_FORMAT_R11G11B10_FLOAT;
}
