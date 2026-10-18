# State of Decay 2: experimental HUD preservation

The standalone renderer processes the completed backbuffer, including game UI.
In State of Decay 2 this can distort menu text, icons, and the HUD. This profile
restores the game's original pixels in regions identified as UI while retaining
the neural result elsewhere.

## Scope and configuration

The profile is enabled only when the host executable's basename is
`StateOfDecay2-Win64-Shipping.exe` (case insensitive). Other games do not register
the extra shader/draw callbacks. Set `StateOfDecay2HudProtection=0` in
`[Standalone.DLSSNR]` in `ReShade.ini` to disable it for this game.

Only native-size D3D11 capture with RGBA8, BGRA8, or RGB10A2 backbuffers is
supported. The presenter's restoration gate is disabled for other APIs, formats,
and the add-on's source-resolution override. This is not a general UI detector.

## How it works

The profile identifies known UI vertex/pixel shaders, checks that a UI draw
targets the current backbuffer on the immediate context, and copies the scene
immediately before the first such draw. At capture, a shader compares this clean
scene with the completed frame. Pixels changed by UI drawing receive a mask with
a one-pixel expansion to cover text edges.

The original RGB and the mask are packed together in the input texture. Alpha
`1/3` denotes scene pixels and `2/3` denotes protected UI. The existing immutable
input snapshots carry that metadata through the asynchronous pipeline, so the
presenter reads UI and its mask from the same source frame. Opaque, unpacked
input alpha (`1`) is not interpreted as a UI mask. No-boundary frames receive an
explicit empty mask instead of reusing the previous frame's capture.

At presentation, protected pixels use the original RGB; other pixels use the
neural output. This restores UI after neural rendering rather than supplying
NVIDIA's private UI-correction contract. Translucent UI regions also retain the
original background underneath those pixels; this is an intentional tradeoff.

The mask pass saves/restores the complete D3D11 context state. A Windows SRW lock
protects shader/context tracking without depending on the `std::mutex` ABI of
the game's bundled MSVC runtime. The pre-UI backbuffer identity is non-owning,
so retaining it does not prevent `ResizeBuffers`.

The shader CRC profile comes from Olivier Cesar's published State of Decay 2
[ShaderToggler configuration](https://anticowl.github.io/files/ShaderToggler.ini),
linked by the author's [game guide](https://anticowl.github.io/2025/01/16/SOD2/).
Only the numeric shader identifiers are used; no ShaderToggler implementation or
game shader bytecode is included. Future game builds may require new identifiers.

## Validation

Run `addon\test-hud.cmd` with the same header dependencies used by
`addon\build.bat`. It uses the Windows D3D11 WARP device and exercises the actual
mask shader on RGBA8, RGB10A2, and BGRA8 textures: moving UI, no UI, a missing
boundary with a stale previous capture, RGB preservation, and context-state
restoration. It also checks executable matching, an unsupported format, and the
disabled profile. Presenter selection is checked on CPU against the packed
metadata; this is not a full GPU presentation integration test.

A local v2.2.4-based prototype was tested in the Steam version of State of Decay 2
on an RTX 5090 Laptop GPU with driver 617.42. Logs showed shader matches, pre-UI
captures, mask packing, and successful NR/DLAA evaluation. The player confirmed
that the menu/HUD corruption was resolved. The driver was not changed.

The upstream preparation adds executable gating, a disable setting, encoded
alpha sentinels, and no-boundary/cleanup guards beyond that confirmed prototype.
These refinements are covered by the build and synthetic tests, but have not yet
been independently confirmed in gameplay. Prolonged sessions, resolution changes,
HDR, generated-frame UI cadence, and other hardware remain unverified. Keep this
profile experimental until broader testing is available.
