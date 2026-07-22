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

## Risk and Rollback

- 渐变创建或绘制失败时立即回退原纯色边框，避免控件边界消失。
- 若 Raw Input 注册失败或窗口外移动无效，仅禁用鼠标光并检查注册/客户区转换，不接入传统 `IdtFloating` 钩子。
- 若出现柔光残影，仅调整 PointLight 脏区外延，不扩大整个分层窗口的更新范围。
