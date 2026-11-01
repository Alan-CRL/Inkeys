# 手掌橡皮交叉审查

状态：全范围静态交叉审查完成；Controller 两项实际问题已局部修正，等待 root 的完整构建及动态验证。

## 已审范围

- PalmEraser 纯策略与共享 DisplayScale/单位解析。
- Other.Config schema/严格读盘、IdtState 偏好快照和保存调用、Host owner 应用及启动发布。
- Setting 草稿/事件、导航与独立 child ID、倍率重置、像素预览和七工具含 Brush。
- 三语言文案、真实 ImGui 测试、无窗口策略测试、CLI 分派及工程登记。

## 已发现问题

1. **编译阻塞，root 已修正并经静态复核**：`Inkeys/IdtMain.cpp:851` 使用 `Inkeys::Config copied(restored)`。Config 仅有显式复制赋值，`shared_mutex rwMutex` 使隐式复制构造被删除。已改为既有测试使用的默认构造加赋值，没有扩展 Config API。

## 静态结果

- 配置阈值/窗口先校验 JSON 原始整数类型和可表示宽度；bool codec 严格，新增项均 NoUpload。
- 页面一帧实际编辑只调用一次 SetPalmEraserPreference，不重复 QueueConfigWrite；保存复用现有捕获至入队锁。
- 现有 settingTab/subpage 双 PushID 为 Palm 提供独立稳定滚动身份。
- 预览读取已应用的绘图输出标尺，灰色矩形用原始像素绘制；文本/留白才经过 UI 倍率。
- `git diff --check --ignore-submodules=all` 通过。
- 记录的原文件全部保留 baseline BOM，零裸 LF。

## 验证边界

本审查代理初轮只读，后经root协调局部修复了下述Controller问题；始终未运行构建、测试二进制、GUI、隐藏 HWND 或 Computer Use，未commit/push。构建及动态结果由 root 完成后统一记录；上述静态结果不能代替运行通过。

## 用户补充参数复核

- 用户最新确认：物理可靠时仍 25cm²；DPI 回退改为 100×100 系统 DIP（10000DIP²），倍率均按面积。策略已分开物理和 DIP 分支，回退不伪造毫米密度，预览与识别共用阈值 helper。
- 已向 root 指出旧三语 ThresholdDescription 仍无条件称 100%=25cm²，应按新参数说明可信物理与 DPI 回退两个基准。

## Controller 集成审查与修复

1. **P1 已修**：关闭边擦边写合成收尾其他 Laser 时，原实现只调用 completeModelUp，遗漏 Laser 活动计数退休与 CPU 层冻结。现由正常物理终态和合成终态共用 FinalizeLaserRuntime，仅退休本层并合并旧/新脏区。生产 probe 验证另一 Laser 不变、completedPoints 不再借用被回收 Runtime、最后活动计数归零并可淡出。
2. **P2 已修**：Palm 为 Fixed，原长空档重锚仅适用 Touch SpeedEraser；默认360Hz/2000输出时约5.56秒停顿后 Move 会持续模型失败。现实际 Touch+palm.active 复用同一 UpdateRuntimeContactModel 有界重锚；记录最后成功建模时间，保留原 Speed 时钟和普通 Fixed 路径。生产 probe 覆盖四档 timing 的超上限 Move/Up、连续两次长空档、已接受点/锁定直径/原工具/QPC身份不变及普通 Fixed 负对照。
3. observer-only 检查覆盖初始化不开始普通模型、渲染/光标/Shape/共享层/CPU存储绕过、窗口结束或物理终态退休及路由终态隔离。新 Down 在真实续接尝试后才走严格单 Touch 与新 Palm 识别；成功续接先锁窗并继承旧实际工具、宽度和状态。
4. 转换以局部模型成功为提交点；Pen/HardPen/Highlighter/Shape/Laser 的自有旧 CPU 几何撤去，原 Eraser 的已接受点和半径保留；全局/批次取 selectedTool。活动层仅重建 L0/L1，不改文档、L2、历史或 UInk。
5. 短接触 Down 面积在不可变 Down 快照读取，palmInterruptQpc 保留本帧激活后收尾意图。无功能路径的 state 默认 locked，新增面积观察被旁路。Palm idle 不缺少原本的帧级 model 更新：既有帧级 stationary 推进仅用于 Pen/HardPen，Eraser 仍在真实 Move/Up 更新。

局部修复后 diff --check 与 Controller UTF-8 BOM/零裸 LF 静态核对通过；本代理没有运行编译或测试。真实 Run/GPU/硬件效果不能由 CPU helper probe 等同证明，需与 root 运行结果及未授权 GUI 边界一并说明。

## Root动态复核

完整ARM64 Debug、129项手掌策略、设置专项、全量Headless无窗口，以及手掌/配置/控制屏障/CPU退出快照/白板存储原生早期CLI均已exit0，详见[validation.md](validation.md)。没有用这些结果声称真实Run/GPU/硬件已验收。
