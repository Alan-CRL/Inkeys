# Recorder v2 frozen interface

Owner: recorder_v2. Header: `Draw3.Performance.h`. The product renderer and script owners consume this contract. CSV uses CRLF, plain UTF-8, exact integer QPC/identities; JSON uses existing JsonCpp export. Product exports only after producer stop/owner join.

## Native calls

- `PerformanceGraphicsOperation`: `Map=0, WriteCopy=1, WriteConstant=2, Unmap=3, Bind=4, Copy=5, Clear=6, Draw=7, Present=8, Other=9`.
- `PerformanceGraphicsRole`: `Unknown=0, InkData=1, GlobalConstants=2, HighlighterData=3, LaserData=4, CompositeVertices=5, LayerTexture=6, BackBuffer=7, StagingReadback=8, SnapshotTexture=9, Other=10`.
- `PerformanceGraphicsPass`: `Unknown=0, Initialization=1, Geometry=2, Snapshot=3, Composite=4, Present=5, Recovery=6`.
- `PerformanceGraphicsSite("static_ascii_call_site")` supplies a **consteval** FNV32 site ID, including Debug off paths; session exports `graphics_sites` id/name dictionary. Keep each site specific to the API and function, including bind/unbind distinction. Literal or static constexpr sites only; do not route a runtime const char* through this constructor.
- `PerformanceGraphicsArguments { ID3D11Resource* resource, relatedResource; uint64 bytes, offset; uint32 subresource,mapType,flags,argument0,argument1; PerformanceGraphicsPass pass; }`. Argument builder executes only when session is nonnull; bytes means requested Map bytes / actual CPU write bytes / estimated Copy bytes / zero for nonbyte APIs, as determined by operation. Draw counts use argument0/argument1.
- `PerformanceGraphicsCall(session,operation,role,site,[&]{ return arguments; },[&]{ return original_hresult_call; })` returns the original HRESULT once. `PerformanceGraphicsVoid(...)` preserves void APIs and exports hresult_available=false. Do not nest leaf wrappers; nested calls retain parent identity but invalidate accounting so they cannot double count.
- Existing `PerformanceMap` gains optional trailing `role,site,pass`, retains original Map arguments/result and existing inclusive stage accounting. Do not add another wrapper around PerformanceMap.
- `RegisterResource(resource,role,site,HRESULT=S_OK)` cold-registers a new generation and reads its descriptor once. Call for each actual resource creation/replacement, including null failed attempts. `ResourceId(resource)` only looks up identity, never GetDesc. `RetireResource(resource)` marks destroyed QPC. No pointer is exported. Registering the same pointer again assigns a new generation ID.
- `GpuBegin(loop_serial)` only polls and prepares loop context. `GpuWork(pass)` lazily starts selected work. Leaf BeginGraphicsCall calls GpuWork for actual D3D operations; CPU writes alone do not start queries. Leaf EndGraphicsCall for Draw calls GpuDraw once. With an existing Draw wrapper, do not additionally call GpuDraw. Unwrapped actual Draw needs GpuWork before the call and GpuDraw(pass,count) exactly once. Existing `Draw(count)` retains v1 frame counters only.
- Frame and prior candidate sequences are independent per device epoch: **frame first 8, then every 8; prior first 4, then every 16**. Prior samples have frame_serial=loop_serial=0 and never consume frame sequence priority. EndFrame resets the candidate context so Down work before the next Begin cannot attach to a sealed frame. Unknown/Initialization Map/Bind calls do not consume work candidates; actual Draw/Copy/Clear/Present can trigger even when the pass is unknown. There are 8 fixed shared slots; each poll handles one slot with DONOTFLUSH, any S_FALSE immediately returns. No wait/Flush. Geometry/Snapshot use marker pair 0/1, Composite 2/3, Present readback 4/5. A lazy begin records a clipped actual pass start; missing pairs remain missing.

## Raw schemas and files

`session.json=2, frames.csv=2, gpu.csv=2`; `initialization.json=1, input-events.csv=1`; new files all schema 1. Session `raw_schemas` keys are actual filenames (including extensions). launch/completion remain schema 1. All new files must exist even when empty.

`graphics-calls.csv` and `slow-graphics-calls.csv` share exact columns:

`call_serial,frame_serial,device_epoch,resource_id,related_resource_id,site_id,operation,resource_role,pass,start_qpc,end_qpc,valid_interval,hresult_available,hresult,subresource,map_type,map_flags,offset_bytes,size_bytes,argument0,argument1,parent_call_serial,attribution,accounting_valid,resource_available,depth`

operation/resource_role/pass are snake_case names corresponding to enum entries; site names live in session dictionary. hresult is blank for void APIs. `valid_interval` means positive start, monotonic endpoints and known clock frequency; zero duration is valid if measured. `accounting_valid` also requires nonnested, same device epoch, and assignable boundary. Attribution is `frame`, `prior`, `split_prior_frame`, `cross_frame`, or `unassigned`. Original endpoints are never clipped. `split_prior_frame` has the current frame ID and contributes separately before/after its start; completed prior calls have frame_serial=0 and their aggregate transfers to the next frame's prior columns, not frame work. `parent_call_serial` is zero for a leaf root. Resource ID zero is explicitly unavailable, not a fabricated instance.

Full prefix capacity=8192. Slow prefix capacity=4096 and threshold=1ms (QPC threshold rounded up); slow store includes qualifying rows even if they also appear in full prefix. Deduplicate by call_serial, never add these denominators. Both stop retaining at capacity; all-call aggregates continue.

`graphics-groups.csv`:

`group_id,site_id,operation,resource_role,pass,attribution,calls_seen,valid_calls,invalid_calls,failed_calls,hresult_calls,size_bytes,total_qpc,min_qpc,max_qpc,max_call_serial,max_frame_serial,arithmetic_overflow`

Fixed 2048 groups by site/operation/role/pass/attribution. Counts and extrema cover all observed calls, independent of detailed prefixes. `valid_calls` uses accounting validity. No percentile is claimed from aggregate-only rows. Group table exhaustion is separately counted, never silently merged into a known group. Prior and frame group denominators are separate.

`resources.csv`:

`resource_id,device_epoch,resource_kind,resource_role,site_id,descriptor_available,byte_width,width,height,format,usage,bind_flags,cpu_access_flags,misc_flags,structure_byte_stride,created_qpc,destroyed_qpc,create_hresult`

Fixed 512 creation attempts. Kinds are unknown/buffer/texture1d/texture2d/texture3d. Descriptor unavailable is explicit. Identity is a session creation ordinal and epoch; object addresses never leave the recorder. Unknown/unregistered resource calls remain valid time evidence but lack resource identity, tracked separately.

Frames retain all v1 columns and append two summaries with prefixes `graphics_` and `prior_graphics_`. Each has `leaf_qpc,call_count,invalid_count,observed_mask,invalid_mask`, followed by `<operation>_qpc` for all ten operations in enum order, then `<operation>_count` for all ten. Leaf qpc includes nonnested accounting-valid calls, split at true BeginFrame. Inclusive legacy stages must not be added. invalid_mask/invalid_count forbid claiming an exact residual. Summary continues after call detail saturation; per-frame summary is retained only while frame prefix is available.

GPU keeps v1 columns and appends:

`sample_id,loop_serial,candidate_index,sampling_reason,marker_observed_mask,draw_calls_in_sample,geometry_draw_calls,composite_draw_calls,readback_calls,work_mask,cpu_begin_qpc,cpu_end_qpc,frame_retained,marker_clipped_mask,sampling_domain,domain_candidate_index`

sampling_reason is `first_work` or `periodic_work`; sampling_domain is `frame` or `prior`. candidate_index is the global work ordinal and domain_candidate_index is the independent per-epoch selection ordinal. True draw counters come from actual Draw calls; readback_calls counts Copy work in Present pass. Row frame_serial is the actual current frame if open, otherwise zero; prior loop_serial is zero. Ready alone is insufficient for GPU draw evidence: require real draw count, matching epoch/frame, frequency and complete marker pair. Prior samples remain separate evidence and cannot enter frame GPU coverage. Raw status meanings remain v1. Clip mask identifies lazy pass starts and must be disclosed in timing interpretation.

## Session counters and accounting

Counters: `graphics_calls_seen/retained/dropped`, `slow_graphics_calls_seen/retained/dropped`, `graphics_groups_retained`, `graphics_group_calls_dropped`, `resources_seen/retained/dropped`, `graphics_invalid_intervals`, `graphics_nested_calls`, `graphics_unknown_resources`, `graphics_site_collisions`, `graphics_cross_frame_calls`, `graphics_group_overflows`, `graphics_arithmetic_overflows`; capacities named `graphics_call_capacity,slow_graphics_call_capacity,graphics_group_capacity,resource_capacity`.

Coverage: `first_retained_call_qpc,last_retained_call_qpc,graphics_detail_dropped_after_qpc`, `graphics_detail_policy=prefix_plus_independent_slow_prefix`, `graphics_slow_threshold_qpc`, `graphics_slow_threshold_ms=1`, `graphics_accounting=non_nested_leaf_intervals_split_at_frame_start`; enum dictionaries and graphics_sites. Retained+dropped=seen for both prefixes and resources; group retained is number of keys, group_calls_dropped counts calls without a group. Unknown resources are not missing calls.

GPU counters: `gpu_work_candidates,gpu_candidates_selected,gpu_samples_started,gpu_ready_draw_samples,gpu_slots_busy_skips,gpu_capacity_skips,gpu_query_failures,gpu_pending_at_stop,gpu_draw_frames_seen,gpu_prior_draw_segments,gpu_prior_work_candidates,gpu_frame_work_candidates,gpu_frame_candidates_selected,gpu_prior_candidates_selected,gpu_pending_at_device_replace`. Policy fields include `gpu_sampling_domain_policy`, `gpu_prior_sampling_interval=16`, `gpu_prior_first_work_samples_per_device_epoch=4`, with frame policy 8/8. GPU sample loss is separate from intentionally unselected candidates. Query issue/poll overhead and metadata/bookkeeping overhead are exported as diagnostic clocks/totals, not subtracted from original API durations. No per-fast-call GetThreadTimes or CPU clock; existing frame owner CPU remains the CPU attribution evidence.

`payload_components` supplies sizeof×capacity bytes for every array plus Impl, with actual AllocatedBytes <=32MiB. With legacy payload the total contract <=64MiB. Payload excludes export-only JSON/join/buffer allocations and D3D driver query allocation; this is not an RSS guarantee. Default off creates no session, metadata, clock, query, statistics, file, or thread.

## Implementation handoff / pending validation

Both source files were frozen for root build and independent review. No GUI or D3D device was run by this implementer. Source is UTF-8 BOM + CRLF; local `git diff --check` for the two files emitted no errors (they are pre-existing untracked v1 source files, so final root baseline-format comparison is still required). `RunPerformanceRecorderProbe` now includes deterministic quantitative fixtures for null lazy builders, exact HRESULT/void execution, prefix/slow/group/resource saturation, real Begin split, sealed-frame rejection, zero/backward/overlap/nested time, overflow, buffer identity recreation and descriptor read counts, fake query DONOTFLUSH/S_FALSE early return, separate frame/prior scheduler, marker and draw coverage, disjoint/failure/pool/row budget/stop/replacement, and actual default payload sizeof output. Root owns actual compile and CLI execution.

Review follow-ups reported to root at handoff: current `gpu_ready_draw_samples` summary includes prior ready draw rows (raw sampling_domain is correct); it should be split into frame and prior summary fields. `session.graphics_detail_complete` and completion's independently computed flag must use an explicitly consistent slow-prefix-drop policy. The independent reviewer should resolve these before final package verification.
