# UI3 边缘光影实验开关

## Goal

在“选项 → 实验选项 → Inkeys3”中提供边缘光影总开关和动态鼠标光开关，让用户可以按性能与视觉偏好关闭 UI3 的高成本光影路径，并持久保存到 Inkeys3 配置。

## Background

- UI3 点光边框当前包含基础灰边、第一主光、第三鼠标光以及 Gaussian 柔光。
- 第三鼠标光启用时会按既有 `Dormant / Inside / Grace` 状态机动态注册 Raw Input。
- `Inkeys::Config` 通过 `Other.Config.cppm` 的 schema 自动生成成员、JSON 读写与默认值；`Other.Config.cpp` 提供通用持久化实现。

## Requirements

- 在实验选项的 Inkeys3 区域新增“启用边缘光影”，仅在 UI3 已启用时显示。
- “启用边缘光影”关闭时，不绘制第一主光、第三鼠标光或 Gaussian 柔光，不执行仅服务于光影的持续动画计算；基础灰边保持不变。
- 在总开关开启时显示“动态边缘光影”；它只控制第三鼠标光和对应 Raw Input 跟踪，不影响第一主光与基础灰边。
- 关闭动态光影或总开关时立即请求第三光源进入既有休眠路径，注销 Raw Input，避免继续因鼠标移动唤醒渲染。
- 重新启用动态光影后保持 Dormant，必须由鼠标自然进入 UI3 可接收消息区域后重新激活。
- 配置保存到 `Experimental.Inkeys3.UI3.EdgeLighting.Enable` 与 `Experimental.Inkeys3.UI3.EdgeLighting.Dynamic`，默认值均为 `true`，保持升级前行为。
- 总开关关闭时仅隐藏动态开关 UI，不覆盖其配置值；重新开启后恢复之前选择。
- 设置变更即时应用并调用现有 `Inkeys::config.Write()` 持久化，不要求重启。
- 保持传统 `IdtFloating`、UI3 通用动画设置、240px 第三光源半径和五秒状态机语义不变。

## Acceptance Criteria

- [x] UI3 启用时显示“启用边缘光影”，开启后才显示“动态边缘光影”，容器高度随条目数量正确调整。
- [x] 总开关关闭后仅保留基础灰边，第一/第三光源和 Gaussian 柔光均不绘制，第三光源 Raw Input 被注销。
- [x] 动态开关关闭后第一主光仍显示，第三光源不再激活或因鼠标移动触发 UI3 渲染。
- [x] 两个开关打开后保持现有光影和鼠标休眠状态机行为。
- [x] 配置写入上述 JSON 路径并在下次启动读取；旧配置缺少字段时默认开启。
- [x] 完整 `InkeysRepo.sln` `Debug | ARM64` 构建、`git diff --check` 和目标文件编码/CRLF 检查通过。

## Out of Scope

- 修改光源半径、强度、颜色、Gaussian 参数或五秒休眠时间。
- 给传统 `IdtFloating` 增加同名设置。
- 迁移或重写 `Other.Config.cpp` 的通用 schema 持久化机制。
