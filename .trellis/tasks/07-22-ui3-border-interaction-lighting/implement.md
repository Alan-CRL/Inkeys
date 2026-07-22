# UI3 边框点光追光实施清单

## Implementation

- [x] 将光源缓存缩减为 Primary、Cursor，删除交互残光和边框主光脉冲的全部状态、常量与注册入口。
- [x] 为圆角矩形和超椭圆增加默认 `Frame` 的内部光色策略，并仅对主按钮、主栏和绘制属性栏启用 `PenWhenDrawing`。
- [x] PointLight 成功路径先绘制完整原灰边，再绘制两束同色光的柔光层和清晰层。
- [x] 将主光和鼠标光末端均改为 0%，保留 480px 半径、100%/60% 强度和鼠标启用淡入。
- [x] 保留 Raw Input、普通按钮反馈、主按钮尺寸/图标动画、Solid 默认路径和纯色失败回退。

## Validation

- [x] 搜索确认 Interaction 和边框主光脉冲状态及全部注册调用均已删除。
- [x] 检查三类可染色外框与 11 个颜色块符合最终颜色矩阵，两束光在同一控件上始终同色。
- [x] 检查完整基础灰边、两束光 0% 末端、动画关闭和 Raw Input 失败路径。
- [x] 检查 CRLF/编码、`git diff --check` 和改动范围。
- [x] 运行 Trellis 质量检查和 ARM64 Host 完整 `Debug | ARM64` 构建。
- [x] 不启动程序，不提交、不归档，任务保持 `in_progress` 等待人工验收。

## Validation Result

- 2026-07-22：ARM64 Host MSBuild 完整构建 `InkeysRepo.sln` 的 `Debug | ARM64` 成功，退出码为 0；仅有项目既有编译警告。
- 双光源上限、颜色策略范围、完整基础灰边、0% 渐变末端、Raw Input、动画关闭及纯色回退的静态检查通过。
- 本次 6 个改动文件均为 UTF-8 无 BOM、CRLF，`git diff --check` 通过。
- 未启动生成程序、未执行窗口或屏幕操作；任务保持 `in_progress`，等待人工视觉与静止 CPU 验收。

## Current Revision

- [x] 将 PointLight 扩散层调整为约 7px 总宽，并由绘制与脏区共享同一个 6px 额外宽度常量。
- [x] 默认边框光使用 30% 扩散强度，`PenWhenDrawing` 生效后的画笔光使用 20%，两束光始终复用同一最终 RGB。
- [x] 为 11 个颜色块初始化独立 `framePct`，收起设为 0%，展开设为 18%，并接入既有批次和换边关键帧。
- [x] 保留填充、布局、命中、1px 选中描边、SVG 勾、Raw Input、480px 衰减及 Solid 回退。

## Current Revision Validation

- [x] 静态确认颜色解析只有单一结果，色块始终使用 `SwatchFrame`，可染色三外框才可切换画笔光。
- [x] 检查 11 个颜色块的 0%/18% `framePct` 目标和动画同步路径。
- [x] 检查 7px 柔光、30%/20% 强度、共享脏区常量和渐变失败回退。
- [x] 执行 UTF-8/CRLF、`git diff --check`、Trellis 质量检查和 ARM64 Host 完整构建。
- [x] 不启动程序，不提交、不归档，任务保持 `in_progress` 等待人工验收。

## Current Revision Validation Result

- 2026-07-22：ARM64 Host MSBuild 完整构建 `InkeysRepo.sln` 的 `Debug | ARM64` 成功，退出码为 0；仅有项目既有编译警告。
- 单一最终光色、两束光同 RGB、三类可染色外框以及颜色块固定 `SwatchFrame` 的静态检查通过。
- 11 个颜色块均具有独立 `framePct`，收起目标为 0%，展开目标为 18%，并复用既有显示、隐藏和上下换边动画路径。
- 柔光总宽约 7px，默认灰光/画笔光扩散强度分别为 30%/20%；绘制和脏区共用 6px 额外宽度常量。
- 本轮 5 个改动文件均保持 UTF-8 无 BOM、CRLF，`git diff --check` 通过。
- 未启动生成程序、未执行窗口或屏幕操作；未提交、未归档，任务保持 `in_progress` 等待人工验收。

## Gaussian Revision

- [x] 将基础边框透明度与点光显现度拆分，颜色块使用 `framePct` 的 0%/18% 基础灰边和 `pct` 的 0%/100% 点光显现度。
- [x] 用每控件 CommandList 记录两束同色、带 480px 空间衰减的 1px 光边，并用内置 Gaussian Blur 生成约 3px 连续柔光。
- [x] 复用 Gaussian Effect 和共享扩散宽度常量，保留主光 100%、鼠标光 60%、默认灰光 30% 与画笔光 20%。
- [x] Effect 路径失败时保留基础边和清晰追光、只跳过柔光并只记录一次，不改变 Solid 与渐变失败回退。

## Gaussian Revision Validation

- [x] 静态确认色块光色仍为 `SwatchFrame`，点光显现度不再被 18% 基础边框重复衰减。
- [x] 静态确认 Gaussian 输入仍包含两束 480px 径向衰减，且不同控件不会在同一模糊输入中异色混合。
- [x] 检查 CommandList target 恢复、Close、Effect 复用、COM 生命周期和失败日志。
- [x] 执行 UTF-8/CRLF、`git diff --check`、Trellis 质量检查和 ARM64 Host 完整构建。
- [x] 不启动程序，不提交、不归档，任务保持 `in_progress` 等待人工验收。

## Gaussian Revision Validation Result

- 2026-07-22：ARM64 Host MSBuild 完整构建 `InkeysRepo.sln` 的 `Debug | ARM64` 成功，退出码为 0；仅有项目既有编译警告。
- 色块基础灰边使用 `framePct` 的 0%/18%，点光显现度使用对象 `pct` 的 0%/100%；其余 PointLight 控件默认保持原透明度来源。
- 每个控件的 CommandList 只包含同一最终 RGB 的 Primary/Cursor 两束 1px 径向光，Gaussian Blur 使用 Soft Border 和 `1px × zoom` 标准差，清晰光随后单独绘制。
- CommandList 创建、target 恢复、Close、Effect 复用和一次性失败禁用路径的静态检查通过；旧的单次宽描边柔光已无引用。
- 本轮 7 个改动文件均保持 UTF-8 无 BOM、CRLF，`git diff --check` 通过。
- 未启动生成程序、未执行窗口或屏幕操作；未提交、未归档，任务保持 `in_progress` 等待人工验收。

## Nonlinear Diffuse Revision

- [x] 将同一 Gaussian 柔光输出用 `SOURCE_OVER` 合成两次，使近端有效透明度由线性 `a` 调整为 `1-(1-a)^2`。
- [x] 保持现有 Gaussian 标准差、480px 径向渐变、约 3px 外扩和 6px 脏区外延不变，确保远端透明度仍连续归零。
- [x] 静态确认双次合成复用同一 CommandList、Effect 和最终 RGB，清晰 1px 光边仍只绘制一次。
- [x] 执行 UTF-8/CRLF、`git diff --check`、Trellis 质量检查和 ARM64 Host 完整构建。
- [x] 不启动程序，不提交、不归档，任务保持 `in_progress` 等待人工验收。

## Nonlinear Diffuse Revision Validation Result

- 2026-07-22：ARM64 Host MSBuild 完整构建 `InkeysRepo.sln` 的 `Debug | ARM64` 成功，退出码为 0；仅有项目既有编译警告。
- 同一个 Gaussian Effect 输出复用两次 `DrawImage`，有效中心强度由单次 30%/20% 提升为约 51%/36%；CommandList、最终 RGB 和清晰边绘制次数均未增加。
- 480px 径向渐变、`1px × zoom` Gaussian 标准差、约 3px 外扩与 6px 脏区外延保持不变，因此远端沿原连续高斯尾部归零且没有新增透明度边界。
- 本轮 7 个改动文件均保持 UTF-8 无 BOM、CRLF，`git diff --check` 通过；任务专属视觉参数不构成新的项目通用规范，无需同步 `.trellis/spec/`。
- 未启动生成程序、未执行窗口或屏幕操作；未提交、未归档，任务保持 `in_progress` 等待人工验收。

## Risk and Rollback

- 渐变创建或绘制失败时立即回退原纯色边框，避免控件边界消失。
- 若 Raw Input 注册失败或窗口外移动无效，仅禁用鼠标光并检查注册/客户区转换，不接入传统 `IdtFloating` 钩子。
- 若出现柔光残影，仅调整 PointLight 脏区外延，不扩大整个分层窗口的更新范围。

## Human Acceptance

- 2026-07-22：维护者确认视觉验收完成，授权提交当前改动并结束、归档本任务。
