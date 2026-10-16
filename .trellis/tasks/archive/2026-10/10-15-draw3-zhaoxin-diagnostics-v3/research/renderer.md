# Renderer implementation

## Boundary and actual changes

- Ownership remains `Draw3.Renderer.cppm/.cpp`, `Draw3.RendererPrimitives.cpp`, `Draw3.RendererLaser.cpp`. No shader/layout/geometry/presentation changes, no GUI test execution, no build or git commit by this worker.
- `Init(..., D3D_DRIVER_TYPE driver, bool enableZhaoxinCompatibility = true)` queries the actual device adapter. DEFAULT is selected only for enabled compatibility + HARDWARE + available identity + vendor `0x1D17`. WARP and other GPUs retain dynamic resources. `structured_upload_policy` details are requested/effective/DEFAULT/identity_available.
- Ink remains 3,200,000 B at 16 B stride; highlighter remains 4,800,000 B at 24 B stride. DEFAULT sets CPUAccessFlags=0. Every DEFAULT batch uses byte box `[0, actualBytes)` and shader offset0. Dynamic NO_OVERWRITE capability and wrap logic remain.
- `compositeVertexBuffer`/SRV are 256 B, stride16, DYNAMIC WRITE. ApplyOperatorLayers writes exactly32 B with DISCARD, binds its own t0, records its own resource identity and never mutates ink head. Resource registration, view mapping, retirement and release cover the new resource.
- `UploadStructuredBuffer` is the sole upload implementation for ordinary ink, analytic shapes, cursor, laser coverage, laser dots and highlighter. Dynamic leaves retain existing site IDs; DEFAULT has distinct `*_update_*` Copy leaves and real UploadCopy inclusive stage. UpdateSubresource remains void in graphics records. Device loss is checked separately using GetDeviceRemovedReason; no synthetic API HRESULT.
- On successful dynamic ink upload the helper owns head advancement to end of written range, including Laser/Cursor DISCARD. Stroke/shape capture starting offset before upload and no longer double-advance after Draw. This is necessary because independent composite no longer happens to reset shared ink state. Parent explicitly approved this same-resource lifecycle fix.
- Existing fixed-resource creation statements are extracted into `CreateFixedResources` so a real production renderer can be validated with offscreen textures, without calling Init/swapchain/Host.

## No-window correctness entry

Exported `RunRendererUploadValidation() noexcept` is invoked by the v3 worker's exact `--draw3-resource-probe-upload-test` early dispatcher. It creates only WARP FL11.0 and offscreen textures, loads the production embedded shaders and fixed resources. It never invokes HiddenWindowTest or creates any HWND/swapchain.

It checks:

- Compatibility enabled/disabled, actual driver hardware/WARP, vendor match/mismatch and unknown identity truth table.
- Actual resource Usage, ByteWidth, CPU access and dedicated composite size.
- Dynamic discard fallback, capability-gated dynamic append, and forced DEFAULT on WARP for correctness only.
- Actual ink/highlighter/cursor/laser upload byte readback, CB offset readback, head preservation and unchanged ink bytes across composite.
- Real failed composite Map rejects submission and preserves ink head.
- DEFAULT partial update leaves bytes outside active range unchanged.
- Cross-tool Cursor→stroke, Laser coverage→shape, Laser dot→stroke offset interleaving.
- Production tool/composite pixel equality across three upload modes and nonempty output.
- Two successive Update/Draw submissions with no intervening readback/wait retain red first and green second strokes, checked by exact interior pixel channels.
- Capacity+2 ordinary ink (shared final endpoint), capacity/2+1 shapes and capacity+1 highlighters final batch bytes/offset/head.

Staging readback copies complete buffers so constant-buffer copy restrictions are respected. External parent execution should be bounded (suggest120s) because tests deliberately exercise production200k capacities on WARP. These correctness results cannot prove Zhaoxin performance or full product/RTS flow.

## Checks and integration follow-up

- Worker `git diff --check` clean.
- All4 original renderer files retain UTF-8 BOM and CRLF (bareLF=0, bareCR=0).
- Inspected existing failure injection sections in DrawingController and HiddenWindowTest after parent authorized narrow extension: all existing immutable ink replacements intentionally target geometry uploads, so no retargeting or edits were needed.
- Main owns full Debug ARM64/Release builds, actual CLI execution, diagnostics packages and final verification. No compiler/runtime success is claimed in this note until main executes those steps.
- Field verification remains necessary for all Zhaoxin models other than directly measured ZX C-1190 and for continuous4K workload performance.
