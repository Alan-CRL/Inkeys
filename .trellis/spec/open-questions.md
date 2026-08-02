# Bootstrap Decisions and Remaining Verification

本文件记录 Bootstrap 调研后已经确认的项目规则，以及仍不能仅靠当前源码证明的验证项。

状态含义：

- **已确认规则**：后续任务必须遵守，除非由新的明确架构决定取代。
- **待验证**：目标或设计已经明确，但当前测试程序、测试环境或运行证据不足，不能写成已保证能力。
- **后续任务**：本次不选择实现方案，也不修改源码。

## Windows Compatibility

**已确认规则**：Windows 7 SP1 + KB2670838 是 Inkeys 的正式项目级兼容目标。

当前测试程序保留了多条兼容路径：

- feature level 11_1 返回 `E_INVALIDARG` 时以 11_0 列表重试；
- DirectComposition API 运行时探测；
- DWM HWND swapchain 的 alpha mode 重试；
- DirectComposition、DWM extended frame、UpdateLayeredWindow 的呈现回退链；
- hardware D3D11 失败后回退 WARP。

**待验证**：这些代码路径尚不能证明当前测试程序在 Windows 7 SP1 + KB2670838 上的具体 DWM、透明 alpha、dirty rect、resize 和输入行为。Spec 只能写“兼容目标”和“当前存在的候选路径”，不能写成已经验证或保证的 presenter 能力。

后续兼容性验证至少应记录操作系统补丁状态、显卡/驱动、D3D feature level、实际选中的 presenter、fallback 日志和人工场景结果。

## Third-Party And External Source Policy

**已确认规则**：`inkStrokeModelerTest/additional/`、`inkStrokeModelerTest/lib/` 以及同类目录默认视为第三方或外部来源代码。

- 原则上不直接修改。
- 确需修改时，使用独立补丁或独立变更批次。
- 补丁必须记录修改原因、上游项目/版本/来源、与上游差异及必要验证。
- 不把第三方代码风格自动推广为 `draw3` 自研代码规范。

**待验证**：当前快照对应的精确上游版本和既有本地补丁历史，仓库内尚无完整清单。

## Current Source Versus Phase Documents

**已确认规则**：

- 当前编入工程的源码和配置表示“现有行为”。
- 阶段说明表示“历史设计、阶段计划或当时决策”。
- 两者不一致时必须显式记录差异，不得自动选择任一方作为最终规范。
- 是否更新实现、恢复历史设计或修正文档，必须由当前任务的明确需求或专门架构决定确认。

具体差异必须以当前任务中的源码锚点记录；已经删除的历史入口不得继续作为现行行为示例。

## Experimental Parameters

**已确认规则**：预测时长、目标帧率、工具笔宽、live-tip 时长、荧光笔 12px 阈值、去重/转角/平滑阈值等目前默认视为实验参数。

只有满足以下任一条件时，参数才升级为兼容契约：

- 已出现在公开接口或外部可见配置中；
- 已写入持久化格式或影响旧数据解释；
- 已有明确的跨版本、跨平台或视觉兼容要求依赖该值。

修改实验参数仍需搜索所有镜像和消费者、记录前后值并执行对应人工场景，但不应把当前数值描述为永久产品标准。

**待验证**：未来正式配置入口、参数版本化和持久化迁移策略尚未设计。

## Minimum Quality Gate

**已确认规则**：涉及业务源码、HLSL 或工程配置的变更，当前最低质量门槛是：

1. Visual Studio 主工程/解决方案成功编译。
2. vertex shader 与 pixel shader 成功编译并完成资源嵌入。
3. 启动后没有明显 D3D Debug Layer error。
4. 完成基础绘制、prediction、抬笔烘干和窗口 resize 的人工验证。

纯文档变更可以不执行构建和运行验证，但交付必须明确说明未执行。

**待验证**：当前 `InitializeGraphicsDevice` 没有显式请求 `D3D11_CREATE_DEVICE_DEBUG`；如何在本项目中稳定启用并收集 Debug Layer 输出，需要后续任务确认。未实际启用 Debug Layer 时，不能把普通控制台无报错等同于该门槛已通过。

**后续任务**：自动化测试框架暂不指定；测试工程位置、框架和覆盖范围另行设计。

## Historical And Reference Assets

**已确认规则**：历史入口、旧 renderer 和 `ResTest/` 暂不删除；在专门清理任务前保持引用关系，不把它们作为当前实现依据。

当前调研结果：

| Asset | Current relationship | Status |
|---|---|---|
| `main2.cpp`, `main3.cpp` | `.vcxproj` 中保留 `None` 引用，不参与编译；当前工作树中不存在对应文件 | 候选弃用；具体历史用途待验证 |
| `renderer2.h`, `shader.hlsl` | `.vcxproj` 中保留 `None` 引用，不参与编译；当前工作树中不存在对应文件 | 候选弃用；具体历史用途待验证 |
| `ResTest/DirectInkPresenter/` | 受 Git 跟踪的独立解决方案；README 标记为 DWM 透明背景批注参考；不属于主解决方案 | 参考保留；候选弃用状态待专门清理任务评估 |

不得在普通功能任务中顺手删除上述 `.vcxproj` 引用或 `ResTest/` 内容。

## InkRenderer Resource Exposure

**已确认规则**：`InkRenderer` 当前公开的 device、context、纹理、RTV、SRV 和操作层属于实现暴露，不是稳定公共 API。

- 现有调用者可以维持当前直接访问，避免无关重构。
- 新代码不得在没有专门架构任务的情况下扩大直接资源依赖。
- 需要新增跨模块资源访问时，先判断是否应增加窄接口或 renderer 方法，并在架构任务中评估迁移范围。
- 不能因为成员是 `public` 就假设其生命周期、绑定状态或布局具备兼容承诺。

**待验证**：未来是否收敛为私有资源、访问器或 command-style API，尚未决定。

## Prediction And Persistence

**已确认规则**：prediction 是瞬时视觉结果，不是默认可信的永久笔迹输入。

- 正式持久化内容原则上由已确认输入生成。
- 最终 prediction 必须被后续真实采样替换，或通过明确定义的提交动作转为已确认数据。
- 未确认的预测点不得无条件写入永久笔迹或回放记录。
- `retainPredictionOnUp` 开启时把最后可见 L0 合入 L2，是测试程序的可选视觉画布行为；L2 不是正式 `InkStrokeRecord`，不能据此推导持久化契约。

**待验证**：正式 `InkStrokeRecord`、预测来源标记、显式提交条件、替换规则及回放兼容尚未设计，应作为独立持久化任务处理。

## Remaining Follow-Up

- 在 Windows 7 SP1 + KB2670838 环境验证 presenter、DWM 和 resize 行为。
- 确认并记录第三方快照的上游版本及既有补丁。
- 建立可重复的 D3D Debug Layer 启用和日志检查方式。
- 为历史 `.vcxproj` 残留引用建立专门清理/弃用任务。
- 设计自动化测试工程。
- 设计正式笔迹持久化与 prediction 提交协议。
