# Quality and Validation

## Scope Discipline

- 只修改任务要求的最小范围。
- 不顺手格式化、重命名、封装公开 renderer 字段或清理历史文件。
- `additional/`、`HiEasyX/`、`lib/` 默认视为第三方/外部来源，原则上不直接修改；必要修改必须形成独立补丁并记录原因、上游来源和验证。
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

任何未执行或因环境不足无法执行的项目都必须明确标记“未验证”，不能用静态阅读或普通控制台无报错替代。

纯文档变更可以不执行构建、Shader 编译或运行验证，但交付说明必须明确原因。

## Visual Studio And Shader Build

使用 ARM64 版 MSBuild 构建完整解决方案，超时至少 5 分钟：

```powershell
MSBuild.exe .\inkStrokeModelerTest.sln /m /p:Configuration=Debug /p:Platform=ARM64
```

不要单独编译项目文件或模块源文件。工程需要 MSBuild 解析 C++ module dependencies、生成 FXC 临时 HLSL、编译 shader 并将 `.cso` 嵌入资源。

构建日志需要同时证明 C++ 编译和两个 Shader 编译成功；只看到最终 EXE 存在，不能替代对 Shader 构建/资源链的检查。

## D3D Debug Layer

最低门槛要求启动后没有明显 D3D Debug Layer error。

> **待验证**：当前 `InitializeGraphicsDevice` 只设置 `D3D11_CREATE_DEVICE_BGRA_SUPPORT`，没有显式请求 `D3D11_CREATE_DEVICE_DEBUG`。在后续任务明确 Debug Layer 的启用方式和日志采集方式前，不能声称该项已通过。

普通控制台没有 HRESULT 输出、程序未崩溃或画面看似正常，都不能替代 Debug Layer 检查。

## Automated Tests

当前主解决方案没有自动化测试工程，也没有发现项目自有的单元/集成测试文件。不要声称“测试通过”而只执行了构建。

自动化测试框架暂不指定，测试工程位置、框架和覆盖范围保留为后续任务。新增可测试的纯几何或状态逻辑时，不在普通功能任务中顺手引入测试依赖。

## Manual Validation Matrix

最低人工场景：

- 基础绘制：按下、移动和抬起后笔迹可见。
- prediction：移动中可见预测更新，旧预测不会残留。
- 抬笔烘干：最终可见结果合入稳定画布，不出现明显回缩或跳变。
- 窗口 resize：历史内容按当前规则保留，当前笔和后续呈现可继续工作。

按影响范围追加：

- 普通笔：点击、慢速、快速、停住后继续、抬笔无回缩。
- 荧光笔：纯点击、短于/等于/长于 12px、直线、锐角、近 180° 折返、自交。
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
- 从历史 `main2/main3` 或参考项目直接复制旧语义。
