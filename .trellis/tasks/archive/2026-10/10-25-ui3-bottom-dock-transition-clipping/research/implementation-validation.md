# 吸附实施与验证

## 变更边界

用户已授权继续所有已规划子任务。先复用已有离屏初始化、真实 mailbox/Pump/Seek 与 RenderLoop，新增内部 Debug 入口，不新建框架或 HWND。三个产品修复限定 Bar.BottomDock.h 的成功脱离 seed、Bar.Main.cppm 的完整吸收重基准，以及 Bar.RenderLoop.cpp 的同帧调用/容量顺序/最终包含验证；既有普通捕获、水平远端 seed、serial/barrier、动画参数不改。测试接缝在 Bar.Interaction.cpp 与已有 Bar.MultiTarget.Test.cpp，早退入口在 IdtMain.cpp。

居中/定格前置结果与 DPI 审计见 ../10-25-ui3-centered-bar-dynamic-centering/research/resume-audit.md；完整 no-window 已在原始产物通过 329.844s。GUI/Office/WPS/真实 ULW/DWM/手感仍未授权和未验证。

## 已取得的红/绿证据（2026-10-26）

- 旧生产实现 + 真实 Seek/Pump/RenderLoop：`dock-absorb-before-fix.log` 退出 1。四组（左右展开、100%/150%）均有脱离高度额外残差，最大 40.848717281 px；释放吸收后的屏幕端点误差 65/98 px。初版测试释放时 translation=0，未覆盖吸收；已改为非零两轴位移，并在吸收失败后、完整重试前立即再次按下，才取得以上可信反例。
- 三个窄修复的首轮完整 Debug|ARM64 Solution 构建退出 0；`dock-after-fix.log` 退出 0。6 组含合成负原点，每组 35 个成功样本、4 次成功脱离、1 次吸收、30 个提示可见帧；所有脱离形状和吸收屏幕端点误差均在 1e-6 px 内。完整 source/target 和提示命中矩形在窗口内。
- 容量纯回归显式构造 predicted viewport 超出原容量：旧五参数调用重现越界；新调用把完整 viewport 作为 requiredViewport，且验证重复帧不继续扩容、idle 只收紧 HWND、不缩 target，并验证 source 的负值/空尺寸/越界/加法溢出。该反例不是当前 GUI 提示裁剪的唯一运行根因证据。
- 释放快照坐标复核：indicatorBounds 是布局像素，按钮 hit 是绝对 DIP；真实 screenDelta 只移动屏幕中心和 translation，布局吸收同步移动两轴绝对映射端点及这两类命中域。新增正负两轴往返及真实屏幕直移中心检查。
- 正在追加父任务组合矩阵：PPT/Whiteboard 宽度/位置动画未结束即进入真实 Seek 吸附序列，并追加动画关闭组；同时收紧失败候选不推进成功形状、重试整窗替换与失败注入前 source 验证。最终当前产物的完整 no-window 和两个入口仍待最后一轮验证。
- 构建环境：沙箱内 C1902；沙箱外可以构建。一次完整重编译 C1060 内存不足，命令增加 `-m:1 -p:CL_MPCount=1` 后成功。未改工程、依赖或工具链；Path/PATH 在同一 invocation 规范化后保留单一 Path。

## 自动化接缝边界

输入由现有 mailbox、PumpSharedInteraction 和真实 Seek 处理，测试不创建 HWND；仅把普通无 HWND 直移交回完整渲染，最终 ULW 用记录 sink。保留真实 D2D/GetDC/ReleaseDC/EndDraw、候选 serial、成功快照和失败恢复。CSV 只将已呈现 elastic/端点标作成功状态，不声称记录未暴露的内部候选速度。

GUI、Office/WPS、真实 ULW/DWM、Whiteboard 最小化/恢复、真实多屏和回弹主观手感仍未验证，不能因离屏通过宣称用户所有真实视觉症状已验收。

## 最终组合验证

2026-10-26：19 组组合入口退出 0；722 候选记录、674 成功帧、72 次动画脱离、19 次吸收、38 次失败注入。动画脱离最大已记录高度残差 1.13687e-13 px，712 次实际 source/dirty 检查错误 0。完整 no-window 退出 0（337.813s）；既有 eraser 离屏退出 0（27.5s）。

质量审查修正两类过严/无效测试前提：映射稳定的整窗 client dirty 也属于完整恢复；PPT/Whiteboard 捕获输入须相对场景 dock 线，不能沿用桌面 Y。动画关闭按立即落定验收，不要求保留按住时果冻形状。最后补齐 source 验证先于失败注入、成功 sink 不得替代 EndDraw 成功，以及明确 negative-origin SKIP 边界。

最终报告：`../../10-25-ui3-bar-centering-dock-geometry/research/final-integration-validation.md`。三个产品窄修复和组合自动化完成；真实视觉/系统呈现和手感待 GUI 授权，保持 in_progress，不归档。
