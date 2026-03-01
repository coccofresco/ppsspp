# Project State

## Project Reference

See: .planning/PROJECT.md (updated 2026-02-17)

**Core value:** PSP games playable in immersive VR on PC with the same quality and feature set as the existing Meta Quest version
**Current focus:** Phase 3.1 IN PROGRESS - Display Modes & Projection Correction — display surface selection, geometry-based cylinder rendering (q3vr approach), projection layer submission.

## Current Position

Phase: 3.1 of 4 (Display Modes & Projection Correction)
Plan: 3 of 3 in current phase (COMPLETE)
Status: **Phase 3.1 COMPLETE.** Perspective-corrected cylinder UVs (inverse cylindrical projection), shared VRDisplaySurface enum, self-documenting code. Ready for hardware testing.
Last activity: 2026-03-01 — Perspective-corrected UVs and shared enum constants

Progress: [██████████] 100%

## Performance Metrics

**Velocity:**
- Total plans completed: 10 (01-01, 01-02, 01-03, 02-01, 02-02, 03-01, 03-02, 03.1-01, 03.1-02, 03.1-03)
- Average duration: ~9min
- Total execution time: ~1.4 hours

**By Phase:**

| Phase | Plans | Total | Avg/Plan |
|-------|-------|-------|----------|
| 01 | 3 | ~64min | ~21min |
| 02 | 2 | ~6min | ~3min |
| 03 | 2 | ~12min | ~6min |
| 03.1 | 3 | ~15min | ~5min |

*Updated after each plan completion*

## Accumulated Context

### Decisions

Decisions are logged in PROJECT.md Key Decisions table.
Recent decisions affecting current work:

- [Roadmap]: OpenGL first (not Vulkan) -- matches Quest implementation, lower risk, every Windows OpenXR runtime supports it
- [01-02]: XrCompositionLayerQuad used on Win32 instead of CylinderKHR -- SteamVR does not support cylinder composition layers
- [02-01]: Anti-flickering enabled unconditionally on Windows cinema mode (no user toggle yet)
- [03-01]: Per-eye FOV from projections[eye].fov replaces averaged FOV in projection layers — critical for correct stereo
- [03-01]: Stereo intensity default 70% (moderate depth per user decision)
- [03-01]: Head tracking default off, sensitivity default 1.0 (1:1 natural per user decision)
- [03-fix]: Depth FBO attachment from 03-01 broke packed GL_DEPTH24_STENCIL8 renderbuffer — reverted
- [03-fix]: Stereo mode sets VR_CONFIG_REPROJECTION=1 (projection layers)
- [03-hwtest]: Immersive quad aspect must use FBO dimensions (like Quest cylinder), not PSP 480:272
- [03-hwtest]: bForceVR auto-set to true on successful Windows VR init (enables VR settings tab)
- [03.1-01]: Display surface default 0 (Flat/Quad) -- safe default preserving existing behavior
- [03.1-01]: Equirect2 preferred over v1 when both available (simpler angular parameters)
- [03.1-01]: ovrMaxLayerCount increased from 3 to 4 (passthrough + per-eye layers + spare)
- [03.1-01]: Display surface independent of stereo toggle -- orthogonal controls
- [03.1-02]: Old cylinder correction shader (pincushion pre-warp + composition layer) REPLACED by geometry-based cylinder mesh (q3vr approach)
- [03.1-02]: Custom glDraw* works on swapchain FBOs — previous "swapchain blocks draws" was dirty GL state, not a limitation
- [03.1-02]: Raw sRGB passthrough (GL_FRAMEBUFFER_SRGB disabled) eliminates posterization — zero conversions in the pipeline
- [03.1-02]: GL state save/restore (q3vr pattern) required for drawing on swapchain FBO after PPSSPP's GLQueueRunner
- [03.1-03]: Perspective-corrected UVs use tan(theta)/tan(halfArc) — inverse cylindrical projection makes straight lines appear straight on curved surface
- [03.1-03]: VRDisplaySurface enum shared via VRRenderer.h, not duplicated locally

### Roadmap Evolution

- Phase 3.1 inserted after Phase 3: Display Modes & Projection Correction (INSERTED) — decouple stereo toggle from display surface selection, geometry-based cylinder rendering (replaced old correction shader + composition layer approach). Phase 4 postponed.

### Pending Todos

- (RESOLVED) **FIX: Horizontal stretch in anti-flickering quad layer** — Fixed: quad aspect now uses FBO dimensions (matching Quest cylinder_layer.aspectRatio approach) instead of PSP 480:272.

### Blockers/Concerns

- (RESOLVED) **Horizontal stretch in immersive mode**
- (RESOLVED) **VR settings tab not visible**
- (RESOLVED) **Stereo divergence** — Root cause: rendering used averaged FOV, submission declared per-eye FOV. Fix: per-eye projection matrices via UNIFORMSTEREOMATRIX + UpdateVRProjectionStereo. Confirmed working on Quest Link.

## Session Continuity

Last session: 2026-03-01
Stopped at: Completed 03.1-03-PLAN.md — Phase 3.1 complete. Perspective-corrected cylinder UVs, shared VRDisplaySurface enum.
Resume file: N/A — no active debug.

### Stereo Divergence — RESOLVED (commit 031fdaf088)

**Root cause**: rendering used averaged FOV, submission declared per-eye asymmetric FOV. PC runtimes warp to compensate → divergence.
**Fix**: per-eye projection matrices (`VR_PROJECTION_MATRIX_LEFT/RIGHT`) from `vrView[eye].fov`, uploaded via `UNIFORMSTEREOMATRIX`. `UpdateVRProjectionStereo()` preserves M[8]/M[9] asymmetry unconditionally.
**Confirmed**: divergence eliminated on Quest Link USB.

### Confirmed Facts (from debug sessions)

- FBO 0 → left eye, FBO 1 → right eye (watermark test ✓)
- UNIFORMSTEREOMATRIX: FBO 0 uses mData[0..15] (LEFT), FBO 1 uses mData[16..31] (RIGHT) ✓
- Both `u_view` AND `u_proj_lens` now dispatched per-eye via UNIFORMSTEREOMATRIX
- GPU_USE_SINGLE_PASS_STEREO is NOT set for OpenGL backend (only Vulkan)
- GPU_USE_VIRTUAL_REALITY IS set when VR enabled

### Hardware Test Results (Partial)

| Test | Result |
|------|--------|
| Stretch in default 3D gameplay | FIXED (FBO aspect) |
| VR settings tab visible | FIXED (bForceVR auto-set) |
| Stereo checkbox + controls visible | FIXED |
| FBO→eye mapping | CONFIRMED CORRECT (watermark) |
| View matrix stereo offset | NO VISIBLE EFFECT (dominated by FOV mismatch) |
| Coordinate-space fix (R*p) | NO VISIBLE EFFECT (same reason) |
| Submission pose scaling | NO VISIBLE EFFECT (same reason) |
| FOV/Projection mismatch | FIXED (per-eye projection) |
| Stereo 3D depth perception | FIXED — no divergence |
| Intensity slider | Not yet tested |
| Head tracking | Not yet tested |
| Quick toggle | Not yet tested |
| Auto-switch | Not yet tested |
| Per-game persistence | Not yet tested |

### Phase 3 Verification Fix History

1. **Stereo reprojection config** (FIXED, committed 65ff93e3ae): Stereo mode was using reprojection=0 (quad layers). Fixed to set reprojection=1 for stereo.
2. **Depth FBO attachment** (REVERTED, committed 65ff93e3ae): Depth swapchain texture broke packed depth-stencil renderbuffer.
3. **Depth info chaining** (DISABLED, committed 65ff93e3ae): Depth swapchain not populated during rendering.
4. **Immersive quad aspect stretch** (FIXED, committed d7662ff0ea): Used FBO dimensions instead of PSP 480:272, matching Quest's cylinder layer approach.
5. **VR settings tab missing** (FIXED, committed d7662ff0ea): Auto-set bForceVR=true after successful VR init on Windows.
6. **Stereo depth divergence** (FIXED): Root cause confirmed — rendering used averaged FOV, submission declared per-eye asymmetric FOV. PC runtimes warp to compensate → divergence. Fix: per-eye projection matrices (VR_PROJECTION_MATRIX_LEFT/RIGHT) built from each eye's XrFovf, uploaded via UNIFORMSTEREOMATRIX. UpdateVRProjectionStereo preserves M[8]/M[9] asymmetry unconditionally.

### Phase 2 Bug Fix History

1. **Inverted eyes + too close** (FIXED): Projection layer cinema path filled entire FOV and applied per-eye IPD offsets to mono content. Fix: removed projection layer path, always use quad layer for cinema.
2. **Horizontal stretch** (FIXED): Quad aspect used PSP 480:272 instead of FBO dimensions. Fixed to match Quest approach.
