# Quality and Validation

## Scope Discipline

- 只修改任务要求的最小范围。
- 不顺手格式化、重命名、封装公开 renderer 字段或清理历史文件。
- `additional/`、`lib/` 默认视为第三方/外部来源，原则上不直接修改；必要修改必须形成独立补丁并记录原因、上游来源和验证。
- 历史 `.vcxproj` 残留引用与 `ResTest/` 暂不删除；用途或弃用状态不清楚时标记“待验证”，留给专门清理任务。
- `InkRenderer` 公开 D3D 资源是实现暴露；没有专门架构任务时不新增直接依赖。
- `.cso`、`.aps`、中间 HLSL 副本和平台输出不手工编辑。
- 对关键步骤写简短中文注释，并保持原编码/换行。

## Required Static Review

- 搜索被修改的常量、枚举、字段和函数的全部引用。
- 对照 `.cppm` 检查导出 API、默认值和实现签名。
- 检查所有早退是否留下已绑定 SRV/RTV、部分资源或错误的提交游标。
- 检查 rect 的空值、裁剪、旧区清理和抗锯齿 padding。
- 检查 down/move/idle/up、单点、重复点、零长度和 resize 中途发生等边界。
- GPU 契约变更按 [CPU/GPU Contracts](../shaders/cpu-gpu-contracts.md) 检查。

## Minimum Quality Gate

涉及业务源码、HLSL 或 Visual Studio 工程配置的变更，交付前至少满足：

1. Visual Studio 主解决方案/工程成功编译。
2. vertex shader 与 pixel shader 成功编译，且 `.cso` 资源嵌入链完成。
3. 程序启动后没有明显 D3D Debug Layer error。
4. 人工验证基础绘制、prediction、抬笔烘干和窗口 resize。
5. `inkStrokeModelerTestTests` 的并发/生命周期/几何测试通过；涉及调度时再执行 Release 严格基准。

涉及窗口创建预设、presenter 初始化或 `.vcxproj` 优化元数据时，Debug 成功不能替代 Release 验证。至少需要构建 ARM64 Debug/Release，并多轮启动核对创建样式、active presenter 和错误日志。

任何未执行或因环境不足无法执行的项目都必须明确标记“未验证”，不能用静态阅读或普通控制台无报错替代。

纯文档变更可以不执行构建、Shader 编译或运行验证，但交付说明必须明确原因。

## Visual Studio And Shader Build

使用 ARM64 版 MSBuild 构建完整解决方案，超时至少 5 分钟：

```powershell
MSBuild.exe .\inkStrokeModelerTest.sln /m /p:Configuration=Debug /p:Platform=ARM64
```

不要单独编译项目文件或模块源文件。工程需要 MSBuild 解析 C++ module dependencies、生成 FXC 临时 HLSL、编译 shader 并将 `.cso` 嵌入资源。

构建日志需要同时证明 C++ 编译和两个 Shader 编译成功；只看到最终 EXE 存在，不能替代对 Shader 构建/资源链的检查。

仓库内 ink stroke modeler 预编译库使用 Release ABI。Debug 配置必须保留调试信息/非优化构建，但以 `/MT`、`NDEBUG` 链接；改回 `/MTd`、`_DEBUG` 会产生 `_ITERATOR_DEBUG_LEVEL` 和运行库不匹配。该约束适用于主工程和直接链接真实模块源码的测试工程。

原生窗口宿主不需要文件级 `WholeProgramOptimization` 例外。Release 必须继续使用项目级 `/GL/LTCG`，并通过启动测试确认 `WS_EX_NOREDIRECTIONBITMAP` 直接进入 `CreateWindowExW`。

## D3D Debug Layer

最低门槛要求启动后没有明显 D3D Debug Layer error。

> **待验证**：当前 `InitializeGraphicsDevice` 只设置 `D3D11_CREATE_DEVICE_BGRA_SUPPORT`，没有显式请求 `D3D11_CREATE_DEVICE_DEBUG`。在后续任务明确 Debug Layer 的启用方式和日志采集方式前，不能声称该项已通过。

普通控制台没有 HRESULT 输出、程序未崩溃或画面看似正常，都不能替代 Debug Layer 检查。

## Automated Tests

主解决方案包含无第三方测试框架的 `inkStrokeModelerTestTests` 控制台工程。它直接重新编译并链接真实 `draw3` 模块源码，不复制生产算法。

最低自动覆盖：

- contact pool：32 个并发生产者、32/64/多 block 边界、容量耗尽、释放再取得和无分配 Down。
- 生命周期：Move/Up 竞争、stale generation、重复回收、Cancelled/shutdown、ControlWake/Down/终态唤醒。
- 荧光笔：单点固定矩形、6.25×50px half size、0.25px 去抖、三方向 sweep、完成态/L1 切片、锐角和近 180° 回折。
- 架构：ARM64 Debug/Release、x64 Release、x86 Release 均构建并运行测试。

Release 运行指标用 `--metrics-output <json> --strict-metrics` 启用；关闭时不得分配指标会话或写文件。原始 JSON 放在忽略的 `TestResults/`，只提交环境、阈值和分位数摘要。

即时落笔硬门槛统一统计普通笔、荧光笔和橡皮的 Down→首次成功 Present；荧光笔不再有独立 VisibleEligibility 闸门。

## Manual Validation Matrix

最低人工场景：

- 基础绘制：按下、移动和抬起后笔迹可见。
- prediction：移动中可见预测更新，旧预测不会残留。
- 抬笔烘干：最终可见结果合入稳定画布，不出现明显回缩或跳变。
- 窗口 resize：历史内容按当前规则保留，当前笔和后续呈现可继续工作。

按影响范围追加：

- 普通笔：点击、慢速、快速、停住后继续、抬笔无回缩。
- 荧光笔：纯点击、极慢移动、快速移动、水平/竖直/斜线、锐角、近 180° 折返、自交和抬笔无跳变。
- 橡皮：点击、连续擦除、段重叠、L1/L0 交界抗锯齿。
- 窗口：绘制中 resize、缩小/放大、重新暴露、移动、清屏。
- 呈现：首选 GPU 路径；涉及兼容时覆盖 fallback 或 WARP。
- 诊断：无新增 HRESULT/Win32 错误，帧日志仍可读且不过度重复。

无法触发的 fallback 或硬件场景必须列为未验证，不用推测替代结果。

Windows 7 SP1 + KB2670838 是项目级目标；只有在记录了系统补丁、GPU/驱动、实际 presenter 和场景结果后，才可把某条兼容路径从“待验证”升级为实测能力。

## Review Red Flags

- 在 Win32 回调中直接调用 D3D。
- 只更新 C++ 或只更新 HLSL 的结构布局。
- 使用 straight alpha 写入当前 premultiplied 管线。
- L0 未先清为单位操作就重画。
- dirty rect 只包含新几何，不包含旧 L0 清除区。
- resize 成功一半就提交新逻辑尺寸。
- presenter fallback 沿用前一次失败的资源。
- 只验证 Debug 窗口预设，未执行 Release 启动对照。
- 在窗口创建后用 `SetWindowLongPtr` 补设 `WS_EX_NOREDIRECTIONBITMAP`。
- 从历史 `main2/main3` 或参考项目直接复制旧语义。
