# Engineering TODO

## Presentation modes

### Refresh-aware output

- Decouple game/NGX processing cadence from monitor presentation cadence.
- Copy completed results into fence-protected output-mailbox resources so display ownership never retains an NR, SR, or FG working slot.
- Timestamp and sequence real/generated outputs; present the newest temporally valid result on each refresh opportunity.
- Give real frames priority and use generated frames only to fill otherwise-unused display intervals.
- Drop stale generated frames first, then stale real frames, without blocking the producer or displaying a generated frame after its corresponding real frame.
- Replace the current `refresh / 2` hard real-frame ceiling with an efficiency target rather than a mandatory cap.
- Report game/source, NR-processed real, FG-produced, and display-accepted FPS separately.

### Uncapped tearing output

- Add an explicit opt-in mode for users who want presentation attempts above monitor refresh.
- Investigate a non-DirectComposition flip-discard output swapchain with tearing support and non-blocking `Present` calls.
- Preserve fence-protected resource ownership, chronological frame ordering, bounded queues, and the stale-frame policy from refresh-aware output.
- Keep the current compatible compositor as the automatic fallback when tearing or independent flip is unavailable.
- Clearly label the mode as potentially causing visible tearing and reduced compatibility, especially in windowed/composited environments.

These modes should share one producer/mailbox/scheduler implementation across D3D9, D3D11, D3D12, Vulkan, and the x86 carrier path. Neither task should reintroduce CPU fence waits or couple NGX admission to swapchain availability.
