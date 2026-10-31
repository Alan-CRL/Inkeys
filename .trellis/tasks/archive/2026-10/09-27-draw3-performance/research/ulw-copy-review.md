# F-005 ULW dirty copy 候选独立审查

日期：2026-09-28。范围：静态核对 `Draw3.TransparentPresentation.cpp/.cppm`、`IdtMain.cpp`、冻结方案及已有基线/候选原始日志；本人未构建、运行基准或打开 GUI。先前发现的 P1 夹具方法学问题已由单次调用版本修正，旧三轮重复调用样本作废。

## 结论

最终保留的生产代码已恢复原双遍顺序：staging 脏行逐行复制到 DIB，随后 `Unmap`，再扫描**DIB** 脏区得出三项 alpha 观察值，最后提交 ULW。候选扫描 staging 源行虽未显示 DIB/hash 语义错误，但新夹具三轮 Release|ARM64 数据未证明超过噪声的收益，局部场景明显退化，按冻结门槛撤销。保留的无窗入口只用于 CPU 子段诊断；本审查不把它记作产品性能提升或真机透明呈现验证。

## 发现

1. **P1 已修正：重复调用夹具的样本作废。** 旧实现每个计时块反复以同一源覆盖同一 DIB、每次覆盖 `result`，只验证最后一次，Release 优化器有权消去前面调用的扫描或重复拷贝；没有证据证明实际消除，但旧 `draw3-ulw-copy-baseline-release-arm64-{1,2,3}` 的耗时不可作为可比基线。新 `Draw3.TransparentPresentation.cpp:1092-1129` 每块在计时外重置 DIB、计时内只调用一次生产 helper、计时后消费三个 flag 并对**完整 DIB** 执行 `memcmp` 和 FNV hash；16 个 warmup、128 个 measured。两种变体用同一夹具重新串行采三轮。该改动消除了“计时循环重复调用被合并”这一特定风险；单次调用仍有 QPC 开销，因此绝对数值只用于受限 CPU 对照。
2. **候选否决有原始数据支持。** 新日志 `draw3-ulw-copy-rebaseline-release-arm64-{1,2,3}` 与 `draw3-ulw-copy-singlecall-candidate-release-arm64-{1,2,3}` 每轮都含七场景 × 128 测量块、stderr 为空、各场景 hash 与 flag 一致。按每轮中位数，全幅混合基线为 `2.9001/2.8918/2.9518 ms`，候选为 `2.8697/2.9223/2.8578 ms`，变化约 1% 且方向不稳定；`partial_256` 从 `0.1647/0.1610/0.1556` 变为 `0.1793/0.1805/0.1836 ms`，`narrow_long` 从 `0.0368/0.0364/0.0348` 变为 `0.0504/0.0516/0.0628 ms`。全幅混合 P95 在候选三轮中的后两轮变差。按方案 `ulw-dirty-copy-plan.md:13` 的收益门槛撤销融合扫描是合理的。
3. **无窗数据不模拟真实 Map 内存。** `Draw3.TransparentPresentation.cpp:1050-1055,1094-1110` 用普通 `std::vector` 作为源与 DIB，缓存行为未必等同 GPU staging 映射；没有计入 `Map` 等 GPU、`Unmap` 或 `UpdateLayeredWindowIndirect`。即使候选在 CPU 夹具中快，也不能推出真实 ULW 帧延迟下降。本次候选已经被否决，更不应写成性能提升。

## 逐项核对与未测边界

| 项目 | 代码证据与判断 |
| --- | --- |
| dirty/步长/像素 | `Draw3.TransparentPresentation.cpp:304-325` 的宽高最小值、全量覆盖、四边裁剪和空脏区早退仍是原逻辑；helper `:147-159` 以 `mapped.RowPitch` 和 `dibWidth*4` 分别走行，`memcpy` 仍只写脏区。32-bit top-down DIB 由 `:256-278` 建立并以零填充。 |
| alpha 与状态 | helper `:160-180` 在 `Unmap` 后读取 **DIB** 像素，使用旧版相同的布尔短路表达式和 full-frame 判定；`Present` `:329-331` 原样赋给观察值。staging Map 与 `CreateDIBSection` 是独立资源；最终代码不再依赖扫描源/DIB 等价假设。 |
| Unmap/失败 | `Map` 失败仍直接返回 false；成功后 helper 复制完立即调用 `Unmap` 回调 `:324-328`，然后扫描 DIB，最后查窗口位置、提交 ULW `:333-348`。未见新增漏 Unmap 的正常控制分支或改变失败返回。 |
| 选路/资源 | 自动模式仍为 DComp→ULW `:94-97`，强制/恢复共用 `TryInitialize` 的 DWM 禁用门 `:689-701`；swap chain 仍用 `FLIP_SEQUENTIAL` `:635-646`。同一 helper 由主 ULW 与 selection ULW 调用，窗口/设备资源所有权未改。 |
| 无窗入口 | `IdtMain.cpp:275-284` 在配置、互斥体、窗口初始化前精确匹配 CLI；`RunUlwDirtyCopyBenchmark` 仅分配 CPU 缓冲 `Draw3.TransparentPresentation.cpp:1007-1145`，不创建 HWND 或调用 GPU。七个固定场景覆盖全量/局部/窄长、透明/半透明/混合/单个无效预乘像素，完整 DIB `memcmp` 与三 flag 校验后输出 hash。夹具没有覆盖生产边界裁剪、resize 期间 staging/DIB 不同宽高、Map 失败和真实 ULW 成功提交；这些没有因候选引入新逻辑，但不能从 CLI PASS 推断其运行行为。 |

新样本中七个场景的跨变体 hash 均一致：`full_mixed=ee91ae952ee74786`、`partial_256=e8b42779253ce538`、`narrow_long=c9b7c904ae711e67`、`full_transparent=dc74737d38bdd282`、`partial_transparent=d026720829703c35`、`partial_half=67c4b45ac01fff62`、`partial_invalid_alpha=9296b37ff5d81d30`。hash 和无窗 `memcmp` 不能替代真实 D3D readback、`UpdateLayeredWindowIndirect` 成功、Win7 SP1+KB2670838 Hardware/WARP、resize/DPI、设备丢失或像素/光学延迟验证。用户已实测的 Win7 `FLIP_SEQUENTIAL` 约束仍被保留。
