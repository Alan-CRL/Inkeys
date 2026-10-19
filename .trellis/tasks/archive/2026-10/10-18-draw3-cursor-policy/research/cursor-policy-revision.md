# 2026-10-19 光标规则修订（当前权威需求）

用户在第一版实施后明确要求调整。本文替代首次“三场景可选高亮、鼠标默认新式”的规则；此前研究和 verification.md 只作历史证据。继续同一 Trellis 任务，实施和无窗口验证已授权；不运行 GUI/窗口 harness，不 commit/push。

## 确认后的产品行为

| 输入/工具 | 默认与选项 | 边缘 |
| --- | --- | --- |
| Mouse + 软笔/硬笔 | “默认光标”（系统，默认）或“画笔光标” | 画笔光标固定高亮 |
| Pen + 软笔/硬笔 | “默认光标”（原圆形，默认）或“画笔光标” | 已识别 ScreenPen 固定不高亮；Digitizer/Unknown/Conflict 固定高亮 |
| Mouse + Highlighter | 始终矩形粗细预览，不受 MouseStyle 或 PenStyle 控制 | 固定高亮，填充 alpha 仍 0.35 |
| Pen + Highlighter | 始终原矩形粗细预览，不受两类型选择控制 | 同固定设备规则 |
| Mouse + 直线/矩形等 Shape | 始终系统默认光标，不受 MouseStyle 影响 | 无新增主光标 |
| Pen + Shape | 保持既有圆形预览，不把 PenStyle 扩展到图形工具 | 同固定设备规则 |
| Eraser/Laser、Touch、选择/穿透 | 既有专用光标、生命周期、命中保护和穿透交接继续保留 | 两类型选择不改专用策略 |

设置块只保留上方“鼠标”类型选择、下方“笔”类型选择；两者都提供“默认光标”“画笔光标”。删除所有边缘高亮开关及配置。鼠标默认系统来自用户明确要求；笔默认圆形沿用原默认外观，是这次实现的默认选择。

## 最小实现边界

- `Drawing.Cursor.MouseStyle` 0=系统默认、1=笔形，默认0；新增 `PenStyle` 0=圆形、1=笔形，默认0。均为严格整数0/1、NoUpload；缺失/非法恢复各自0，保留合法已保存选择。
- 共享 `CursorSettings` 只携带 mouseStyle/penStyle。`MouseCursorStyle::System/Drawing` 保持现值；新增 `PenCursorStyle::Circle/PenIcon` 和对应恢复 helper。删除三份 HighlightEdge，不留影响固定规则的运行时字段。下一次保存仅清理本次废弃的三键，AutoClean=false 仍保留其它未知键。
- Root负责 Config/IdtState/Setting/i18n 和真实配置测试、文档；实施代理负责共享头、Bridge工具投影、Host、Controller、resolver及 Bridge/contact 测试。该分工无文件写入重叠。
- Host发布原始两类型设置；Controller按实际 EffectiveDrawingCursorTool 解析并同步同一策略给Window和resolver：Pen/HardPen才读取 MouseStyle，Highlighter固定Drawing，其它普通图形System。活动contact锁存旧工具时继续按该实际工具呈现，抬起后再按新选择投影，不能覆盖原始偏好或结束笔画。测试覆盖生产工具投影及连续切换，避免只测原始bool或复制映射。
- Resolver的笔形许可只针对软/硬画笔：Mouse读取MouseStyle、Pen读取PenStyle。圆形/矩形工具原大小保持；PenIcon复用24 DIP、左上尖端热点和既有shader type21。
- 边缘只按输入来源计算固定策略：Mouse高亮；IntegratedPen且非Conflict不高亮；其余笔高亮。Unknown/Conflict仅视觉回退，不改真实source。图形和专用工具范围不扩大。
- 不改 Win7 WindowControl、正式HLSL或guarded字节，不改墨迹/历史、输入过滤、全局系统设置、依赖或构建系统。既有 shader 已支持 PenIcon，除非实际发现数据合同缺口，否则不重新生成。
- 继续 deferred Sync→同一FIFO冻结写盘；设置仅两个选择控件，三语说明要讲清鼠标仅软硬笔、荧光笔独立及笔边缘固定规则。

## 验证

真实配置：两默认0、四种组合独立往返/ReadMini/冻结副本、严格非法类型、三退役键忽略并保存清理、未知键保留、不上传。
生产投影/resolver：MouseStyle两值×Pen/HardPen/Highlighter/全部Shape；PenStyle两值×已识别ScreenPen/Digitizer/Unknown/Conflict，Mouse/Pen互不影响；热点/DPI/alpha/Contact/Touch/Eraser/Laser与静止重绘请求。
i18n sync/check、独立 Trellis check、完整 InkeysRepo.sln Debug|ARM64（ARM64 native MSBuild、PATH规范化、>=600秒）、headless --no-window。PDB服务环境按上一轮已验证方式执行；不改项目配置绕过环境。
Win7实际可见行为仍未验收，不能沿用旧二进制或纯逻辑结果宣布通过。
