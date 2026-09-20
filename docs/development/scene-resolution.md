# Compact scene resolution

The existing `renderAtUpscaleRes` setting selects compact scene targets on flat
and VR. Quality presets determine their dimensions; VR's explicit render-scale
setting can override the preset. Output dimensions come from the engine's native
`kMAIN` allocation. No OpenVR size, pose, or compositor submission hook is used.

The implementation retains the existing `PerfMode` class and upscaler routing.
Allocation is transactional: a failed allocation or resolve-shader creation
leaves the native targets selected and uses standard upscaling.

## Frame order

1. Select compact scene color, normals, motion, masks, and scene depth targets
   before jitter configuration. Use their full extents with identity dynamic
   resolution ratios.
2. Render the scene normally. Deferred G-buffers and scene feature resources,
   including SSGI, inherit the compact dimensions during setup.
3. Seed a native color output with a spatial fallback, then reconstruct it with
   DLSS/FSR and apply sharpening. The existing foveated route remains available.
4. Restore native engine targets. Copy reconstructed color into native `kMAIN`
   and resolve the attributes consumed by post-processing. Depth is de-jittered
   with point sampling; the main stencil is transferred in the same draw when
   shader stencil export is supported, with a bitwise fallback otherwise. Refresh the
   native depth copy and VR underwater mask.
5. Run post-processing and UI against native targets. HDR, Post Processing, and
   Effects11 initialize their resources with native bindings.

For menu backgrounds that skip the normal post-processing call, the first
image-space copy/tonemap restores native resources before drawing. The
interface-start path provides the same fallback. These paths use spatial
reconstruction.

## Ownership and cost

Engine-owned native targets remain alive. Compact target textures and all their
views are owned by the scene-resolution manager. Target switches exchange whole
render-target/depth-stencil records; viewport correction follows the active records.
Resolution settings stay latched until target recreation/restart.

Shared scene/post targets require both allocations. The six deferred G-buffers
and scene feature working textures use compact allocations. Total VRAM reduction
is not guaranteed. Spatial fallback, attribute resolves, and exact stencil
transfer add GPU work; a net speedup requires measurement.

The combined depth/stencil path checks
[`PSSpecifiedStencilRefSupported`](https://learn.microsoft.com/en-us/windows/win32/direct3d11/shader-specified-stencil-reference-value)
before using shader stencil export.

## Runtime verification

Compilation does not establish image correctness or RenderDoc compatibility.
Compare enabled/disabled runs at fixed scene, camera, output resolution, and
upscaler settings on flat and VR. Check both eyes, water/refraction, vanilla
SSAO, motion blur, foveation, menus/loading transitions, and flat HDR/frame
generation. Verify compact scene and native post target extents in a capture,
including the depth attachments. Check target recreation and fallback behavior.

Use GPU pass timings for `SceneResolution::SpatialFallback` and
`SceneResolution::Resolve` together with the scene/deferred/SSGI savings.
Report net change as a percentage of the target frame budget.
