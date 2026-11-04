# 续作调研与边界（2026-11-04）

- 原父任务 `11-03-draw-cursor-per-device-pen-cursor` 与识别子任务均已完成，保持归档；只恢复按设备专页子任务。
- `trellis mem search 11-03-per-device-cursor-page` 命中历史会话 `4ac1e967-9ce6-4783-a031-5e1af506f601`。检索“光标”的原始对话确认用户已选择五种统一样式、自绘箭头、默认 ScreenPen=Dot/Tablet=HighlightDot，蓝色提示条经实机验收后归档。
- Draw3.PenCursor.cpp:353 附近在设备样式应用前按 `drawingCursorDuringContactEnabled` 统一隐藏 Pen/注入 Tablet 接触光标，生产默认 false；应由产品的已解析设备/样式/新模式取代，nullptr 独立接口保留。
- Draw3.DrawingController.cpp:383 的 ProductTargetScale 是 PenWidth::SystemScale；13500 附近传递给 resolver。ApplyCursorStyle 已对 Arrow 和 PenIcon、描边乘 scale。不能仅凭用户所见偏小就断言当前无缩放，也不能无证据重复相乘。
- Draw3.Host.cpp:1660 附近从 Display 同一快照建立 penContext，并通过 runtimeCallbacks.penWidthContextChanged 发布；需要贯通 IdtState 到当前 ProductVisualStyle 的测试。
- Setting.Pages.cpp:670 的 RenderCursorPage 当前为四设备行、笔设备区、识别开关、蓝色 Notice。新精细控制放 Notice 后即可满足目标顺序，不必搬改 Notice 拆行逻辑。
- 配置现有 Drawing.Cursor 归属 main.json，NoUpload；新增字段不进入旧 deploy.json。Setting.cpp:3800 附近负责草稿和 deferred SyncDraw3State/QueueConfigWrite。
- 初始 git status --ignore-submodules=all 干净。普通 git status 因 HiMsg submodule Git 元数据读取权限失败；此为环境读取限制，不需要修改产品或 Git 配置。

本轮仅恢复/编辑规划文档，尚无产品代码变更。
