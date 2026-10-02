# Baseline and Test-Package Identity

Captured 2026-10-02 before product-code changes. Hashes are SHA-256.

## Repository State

- Branch: `chore/publish`
- HEAD: `f42083f4f0b5b969b25abc68672df0eeda070f75` (`fix: harden Inkeys3 persistence and rendering gates`)
- HEAD commit time: `2026-10-02 10:34:11 +0800`
- Pre-task working tree: no tracked diff; 5 untracked paths:
  - `.trellis/tasks/09-27-integration-and-release-check/research/draw3-remaining-minimal-delta.md`
  - `inkStrokeModelerTest/inkPixelShader.cso`
  - `inkStrokeModelerTest/inkVertexShader.cso`
  - `inkStrokeModelerTest/laserParticleEmitCS.cso`
  - `inkStrokeModelerTest/laserParticleUpdateCS.cso`
- Trellis task planning adds this task directory; it does not change product source.

## Win7 Test Package

| Artifact | Size | Modified | Architecture | SHA-256 |
| --- | ---: | --- | --- | --- |
| `G:\Inkeys\Inkeys.exe` | 43,768,832 | 2026-10-02 09:41:56 | x64 | `2BFB6A2B77DCBD33113D4380B6510D754B992ABB4CE0D33418C9ABFB8D2B0BEC` |
| `G:\Inkeys\PptCOM.dll` | 67,584 | 2026-10-02 09:46:52 | AnyCPU/.NET | `AB34C31F9510BE311439B798A35D6980D5DA609D55622C8DFAD339E97733C105` |

Both files are byte-identical to `Build/x64/Release/Inkeys.exe` and `Build/x64/Release/PptCOM.dll`. The test directory contains no PDB. The matching local EXE and PDB timestamps are 09:41:55, before the current HEAD commit time; this permits “same binary as local x64 Release” but does not prove which Git source revision produced it. The log version `3.0.0-20260811a` and file version `1.0.0.0` are not source revision identifiers.

Attachments read-only:

- `G:\Inkeys\idt1790915370099.log` — 31,390 bytes, modified 2026-10-02 12:31:52.
- `G:\Inkeys\console output.txt` — 1,470 bytes, modified 2026-10-02 12:32:02.

## Existing Build Artifacts to Preserve

| Artifact | Configuration | Size | Modified | SHA-256 |
| --- | --- | ---: | --- | --- |
| `Build/ARM64/Debug/Inkeys.exe` | ARM64 Debug | 54,049,792 | 2026-10-02 01:49:45 | `4E31A8C9BAF87E26C5645BC1F7911F07E6603385767D973E7D82B4655AF56DF0` |
| `Build/ARM64/Debug/Inkeys.pdb` | ARM64 Debug | 119,459,840 | 2026-10-02 01:49:45 | `E35FD25B50E4B1809CB2E5E1A4F8F6DDF8F94EC91B14A7AB6604024D8C054CE8` |
| `Build/ARM64/Debug/PptCOM.dll` | ARM64 Debug dependency | 67,584 | 2026-10-01 23:21:43 | `AB34C31F9510BE311439B798A35D6980D5DA609D55622C8DFAD339E97733C105` |
| `Build/ARM64/Release/Inkeys.exe` | ARM64 Release | 42,290,688 | 2026-09-30 11:21:55 | `6252228DBBA65E499A7AF06C731E8338FBAF9819072F8A0021C6C501AA864E55` |
| `Inkeys/PptCOM.dll` | tracked dependency copy | 67,584 | 2026-10-02 09:36:15 | `AB34C31F9510BE311439B798A35D6980D5DA609D55622C8DFAD339E97733C105` |
| `Inkeys/PptCOM.tlb` | tracked dependency copy | 6,640 | 2026-10-02 09:36:19 | `C652B5DD0603576033B2E0D15869BE103F23FFDCAA2A1984E53F3B6A14DBA606` |

The ARM64 Debug output is the configured build target and already exists. `PptCOM.csproj` copies DLL/TLB into the tracked `Inkeys/` directory after build; any future build must account for that side effect. Draw3 shader `FxCompile` outputs are configured under `Inkeys/Inkeys/Drawing/Draw3/Assets/`; the four user-listed untracked Demo `.cso` files are separate paths and must remain unchanged.

## User-Owned Demo Shader Checksums

| Artifact | Size | SHA-256 |
| --- | ---: | --- |
| `inkStrokeModelerTest/inkPixelShader.cso` | 28,748 | `3D4701811FF9A5F556C408FA4BC9885444697C793A8059EF63C9A92626E1CE85` |
| `inkStrokeModelerTest/inkVertexShader.cso` | 12,484 | `CA9A83348E43B12FF3AD059EC4F1DB47338720C77C0EF897002AEDEC63214B08` |
| `inkStrokeModelerTest/laserParticleEmitCS.cso` | 5,360 | `46DE79F93D6C181E941CCBC0B406D278AFE83F73839017F4A956DEB3DEC21523` |
| `inkStrokeModelerTest/laserParticleUpdateCS.cso` | 3,572 | `EF3F2373AF2275A4900594A865B4B93B7E1861AF16506D989DCCA374DD56107D` |

## Diagnostic Evidence Read

- Hardware D3D11 initialization failed; fallback selected WARP with feature level 11_0. Failure stage/HRESULT are absent.
- `UlwDirtyRect` waitable swapchain failed at `0x887a0001`, then ordinary swapchain succeeded. The active mode is explicitly `UlwDirtyRect`.
- UI3Diag samples label the successful ULW consumer as Bar; no Drawpad-specific present observation or HWND state transition appears.
- The IDT log reaches normal window/thread initialization and logs `TopWindow` waiting for overlay readiness. In current source, Draw3 first-frame readiness is checked earlier; `TopWindow` waits on Bar/PPT/Freeze readiness and is not direct proof of a Draw3 first-frame wait.
- No evidence here determines whether the main window was hidden by a valid selection state, failed a visibility transition, or accepted a presenter call yet remained visually absent.
