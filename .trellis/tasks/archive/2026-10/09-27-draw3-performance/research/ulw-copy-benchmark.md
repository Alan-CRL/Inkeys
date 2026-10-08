# ULW CPU dirty copy 基准记录

## 门禁 1：生产 helper 与无窗口入口

- 原路径：`UlwDirtyRectPresenter::Present` 对 staging 的 dirty 行逐行复制到 top-down DIB，`Unmap` 后再扫描 DIB dirty 像素，产生 `updatedRegionAllZeroAlpha`、`premultipliedAlphaValid`、`fullFrameAllZeroAlpha`。
- 当前抽取：`CopyAndInspectUlwDirtyRows` 由生产 Present 和 `RunUlwDirtyCopyBenchmark()` 共用；生产回调仍在复制后、DIB 检查前执行 `Unmap`。Presenter 的 dirty 裁剪、DIB 字节、三项 flag、`UpdateLayeredWindowIndirect` 调用位置和 FLIP swap chain 均未主动改变。
- 入口签名：`int Inkeys::Drawing::Draw3::RunUlwDirtyCopyBenchmark() noexcept`，声明于 `Draw3.TransparentPresentation.cppm`。入口本身只分配固定 CPU 缓冲，调用生产 helper，不创建 HWND、不调用 GPU `Map`/`Unmap`、不提交 ULW。
- 固定场景：1920×1080 全幅混合预乘色、256×256 局部、8×1080 窄长 dirty、全幅透明、局部透明、局部半透明、局部单像素无效预乘色。源 `RowPitch = DIB stride + 64`；脏区外 DIB 预置非零 RGB/零 alpha，用完整 DIB 对照检查外部像素未改。
- 每场景 16 个预热块与 128 个测量块；每块只调用一次 helper，DIB 重置在计时前，完整 DIB 比较与三项 flag 的 FNV-1a hash 在计时后。逐块输出 QPC ticks、`iterations=1`、可得时的 thread cycles 与 hash。若完整 DIB 或 flag 不匹配，入口返回非零。

## 方法学修正与原始证据

- 主任务接入 `--draw3-ulw-copy-benchmark`。首版每块重复相同调用，存在编译器合并/测量失真风险；`TestResults/release-hardening/draw3-ulw-copy-baseline-release-arm64-{1,2,3}.stdout.log` 的**耗时作废**，不参与前后比较。该轮仅说明七场景 hash/flag 可稳定重现。
- 修正后每个计时块只有一次生产 helper 调用，16 个预热块、128 个测量块/场景/轮。主任务串行完成 Release|ARM64 基线和候选构建、各三轮 CLI，退出码均为 0；原始样本分别位于忽略目录 `TestResults/release-hardening/draw3-ulw-copy-rebaseline-release-arm64-{1,2,3}.stdout.log` 与 `draw3-ulw-copy-singlecall-candidate-release-arm64-{1,2,3}.stdout.log`。每轮每场景均有 128 条原始 QPC/线程周期记录。
- 表中为每轮单次调用耗时中位数，单位 ms；P99 未报告。候选只把检查移到逐行 `memcpy` 后仍有效的 mapped 源行，DIB 写入、dirty 范围和三项 flag 公式不变。

| 场景 | 独立 DIB 扫描基线（三轮） | 融合源行扫描候选（三轮） |
| --- | --- | --- |
| 全幅混合预乘色 | 2.9001 / 2.8918 / 2.9517 | 2.8697 / 2.9223 / 2.8578 |
| 256×256 局部 | 0.1647 / 0.1610 / 0.1555 | 0.1793 / 0.1805 / 0.1836 |
| 8×1080 窄长 dirty | 0.0368 / 0.0364 / 0.0348 | 0.0504 / 0.0515 / 0.0628 |
| 全幅透明 | 4.3535 / 4.1864 / 4.2859 | 4.2704 / 4.2672 / 4.2755 |
| 局部透明 | 0.1626 / 0.1612 / 0.1599 | 0.1820 / 0.1800 / 0.1812 |
| 局部半透明 | 0.1692 / 0.1639 / 0.1636 | 0.1817 / 0.1812 / 0.1800 |
| 局部无效预乘色 | 0.1659 / 0.1633 / 0.1598 | 0.1702 / 0.1710 / 0.1697 |

## 正确性与取舍

- 每次调用后先以完整 DIB 对照检查 dirty 内外全部字节，再核对三项 flag，最后在计时区外对完整 DIB+flag 计算 hash。基线和候选共六轮的七场景 hash/flag 均逐块一致：`full_mixed=ee91ae952ee74786 (0/1/0)`、`partial_256=e8b42779253ce538 (0/1/0)`、`narrow_long=c9b7c904ae711e67 (0/1/0)`、`full_transparent=dc74737d38bdd282 (1/1/1)`、`partial_transparent=d026720829703c35 (1/1/0)`、`partial_half=67c4b45ac01fff62 (0/1/0)`、`partial_invalid_alpha=9296b37ff5d81d30 (0/0/0)`；括号顺序为 allZero/premultipliedValid/fullFrameAllZero。
- 全幅混合色三轮中位数的中位数仅约下降 1.05%，384 个原始块合并 P95 从 3.9912 升至 4.1347 ms；全幅透明约下降 0.36%。256×256 局部约变慢 12%，局部透明约变慢 12%，窄长 dirty 约变慢 42%。收益未超过噪声且局部场景明确退化，**候选已撤回**。最终 helper 恢复独立 DIB 扫描、复制后 `Unmap` 再检查；保留无窗口基准和七场景正确性回归。最终 `.cpp` SHA256 `BAB0B3DC5A29D51548739C700FD234B589452B08F399DCF0328E94776EF32E99`，与修正基线完全相同；最终 Debug/Release 复建由主任务另行收口。
- 该入口只量 staging→DIB 的 CPU 拷贝与 alpha 检查，不覆盖 GPU `Map` 等待、真实 ULW 提交、成功 Present、Win7 设备、像素/光学延迟。Windows 文档对 `prcDirty` 的更新范围表述存在差异，此处不推断系统脏区提交节省，也不宣称 Draw3 用户体验性能提升。
