# 技术设计

## 选路

IdtMain 在创建窗口链前确定启动偏好，通过 Host 传到 TransparentPresentationController。将产品“首选”与隐藏窗口测试的严格 requiredMode 分开。初始化和恢复复用同一规则。

| 请求 | 允许的主画布路径 |
| --- | --- |
| 自动 / DComp | DComp 可用则尝试，否则或明确失败后转 ULW。自动在 Win7 直接到 ULW。 |
| Win7Dwm | 仅目标 Win7；DwmIsCompositionEnabled 成功且开启，DwmGetColorizationColor 成功且未报告 opaqueBlend 时尝试，其余情况到 ULW。 |
| ULW | 使用阶段 B 的独立纹理与现有 presenter。 |

按仓库已有平台判断及接口能力分流，避免通过安装更新清单推断。硬件与 WARP 仍遵循现有策略，记录实际设备。Win8+ 保持现有自动路线。

## 窗口、链与恢复

- 产品仅开放第一版 DwmBlurBehind 小区域方式，普通可重定向主 HWND 清除 WS_EX_LAYERED 和 WS_EX_NOREDIRECTIONBITMAP。保持预乘 BGRA、全透明清屏、禁止 GDI 擦背景。第二版整窗玻璃保留禁用。
- FLIP_SEQUENTIAL 用 2 个缓冲，SEQUENTIAL 用 1 个。Win7 跳过 waitable flag，保留 STRETCH、BGRA8、单采样并记录实际 alpha mode 与 GetDesc1。两种链均在绘制末尾提交，检查设备级 SetMaximumFrameLatency(1) 的 HRESULT。常规帧不加 DwmFlush、查询自旋或同步读回。
- 首帧、恢复与 Resize 后完整重绘，再按脏矩形提交；覆盖移动光标的新旧位置。合成和着色状态变化时重新判定能力，API 明确失败时记录阶段和 HRESULT，同一次恢复避免重复尝试失败配置。
- 创建期链失败时释放旧尝试再试兼容链。已经成功 Present 的 FLIP HWND 改用其他窗口合同前，须考虑重定向与 GDI 的生命周期限制。需要新 HWND 时由主线程协调 Window Service 和 Host 的交接，保留文档、输入与辅助窗，恢复后先提交完整帧。绘制线程不得自行 Stop 后 join 自身。启动恢复与运行期恢复分别验证。
- 两个 GPU 后端遵守桌面画布交换链偏好。Win7Dwm 创建 FLIP 失败时尝试 SEQUENTIAL；DirectComposition 创建 SEQUENTIAL 失败时保留后端并尝试 FLIP。设备丢失直接进入设备恢复。仅实际使用的交换链类型改变时发布该项警告。

## 配置与看板

沿用 Other.Config 的 Draw3 配置归属、NoUpload 和重启生效。新键缺失才读旧布尔，保留显式关闭 DComp 用户的行为。Setting.cpp 使用全称显示选项。软件调试页在画布初始化信息标题与当前状态之间分别显示后端和交换链的黄色回退提示；后端自动选择时省略后端提示。启动重建 HWND 时携带首次失败步骤和可用的 HRESULT。阶段 C 看板按实际 GPU 后端和交换链区分数据，旧 DComp/ULW 桶仍可读取，WARP 单列。ULW 无交换链，辅助窗呈现不改变主后端签名。

## 依据

Inkeys 早期提交 2e2a2586 使用单缓冲 SEQUENTIAL，a97607b6 后统一 FLIP。mpv 的 D3D11 手册对背景透明建议关闭 FLIP；GLFW 的 Win7 路径检查不透明着色以避免与前一帧叠加。两者为选路依据，目标设备的视觉正确性需实机确认。[mpv](https://mpv.io/manual/master/#options-d3d11-flip) · [GLFW](https://github.com/glfw/glfw/blob/master/src/win32_window.c) · [DWM 对未模糊区域 alpha 的说明](https://learn.microsoft.com/en-us/windows/win32/dwm/blur-ovw)
