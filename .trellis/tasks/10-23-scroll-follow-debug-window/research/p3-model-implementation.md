# P3 数据模型、业务与捕获观测实施

已完成源码，尚未构建/运行：按 main 协调等待所有 writer 释放后统一验证。本阶段不执行 GUI，不绕过现有安全门，不 commit/push。

## 文件与接口

- 新增 Business/ScrollFollowDebug.h/.cpp：标准库纯数据公共头，三槽 latest-only Channel/WriteLease，30 条数值摘要，状态与图像分离；Activate/Enabled/Current/Deactivate、ConsumeLatest/Displayed/CloneDisplayed；完整实际 totalTrackerMs 与 copyMs。
- 修改 Business/ScrollFollow.cpp：只在真实 Initialize/Process 分支获取 Channel/拷贝实际 WorkArea 输入，原 Tracker 可选指针原位写 snapshot，原 Target/WakeHost/result store 顺序保留。AwaitTracking 的原默认 result store 保留，但不发布图像/Tracker 摘要。Context 仅新增首因 atomic，不永久持 Channel。
- 修改 Graphics/FreezeCapture.h/.cpp：Snapshot 追加 Read 起止/有效采样/间隔/故障阶段与时间/原生错误有效性；Frame 尾加 sampleInterval；BackendResult 尾加 nativeError/valid。数据源均在原返回与判定位置，不修改捕获调度、质量分类或撤销决策。
- 新增 InkeysHeadlessTests/scroll_follow_debug_tests.cpp 并接入 animation_tests.cpp 严格短入口（P1 CV + P3 model + UI 实际导出 writer test）。登记主程序/测试项目与主 filters；现有 Surface.cpp/jsoncpp 依赖已经在 headless 工程内，无额外框架或依赖。

## 线程与失败边界

Channel 的 front/back 仅各自 owner 访问；packed atomic middle 使用槽号/dirty/closed，acquire-release CAS 发布/消费/关门。UI Freeze 固定 front，Mag 只交换另外两槽；大图像/71KB 级特征只在 Activate 的 heap storage 存在。后台摘要 try_lock，争用丢摘要且加计数，不等待 UI。ReadLive 短锁只复制固定数值；图像/GDI/导出不在锁内。

关闭撤销全局 current 并置旧代 closed，在途回调仅通过短 ChannelPtr/lease 保活；paused Context 无大缓冲永久引用。旧代关闭/发布不能污染新代；导出 clone 独立，点击之后不持 Channel。BeginFrame 检查 BGRA 布局/overflow/64M pixels 上限，逐行复制实际 input.image，容量跨帧复用；失败只丢诊断并保留生产 CV 调用。

总 Tracker 时间仅已取得 Channel 时读取两个 steady_clock，起点在像素复制后、真实 CV 调用前，终点在 CV 返回后、Target/Wake/result store 前。关闭路径只有轻量判断、空 lease/optional，不构造大型快照，不拷图、不加计时/任务。原分段耗时仍按 Tracker 原字段，totalTrackerMs 包含启用的诊断记录开销。

首因 atomic 在原 ROI/Tracker/质量/API/停止/clamp/超时/控制异常路径 CAS(None,cause)，后继 Paused Tick 的 !publishing 或 Stop(Requested) 不覆盖。质量计数也可能由 Read API 失败增加，结合只读 failureStage/API delta 区分实际来源。Capture Stop(Requested) 不更新 failureStage；后继真实 End 失败可以更新“最近捕获故障”，业务 pauseCause 仍保持首因。

Read 完成时间在原 `completed=now_()` 位置产生，随后才 Analyze；Frame 注释据此改为“后端 Read 返回时刻（之后才做 Analyze）”。lastReadDuration 是整个 Backend::Read，不能宣称隔离的 PrintWindow 耗时。Probe worker 独占 IdtMagnification.cpp，负责真实失败 API 立即获取错误并沿新增尾字段传递；这里不读取 Windows GetLastError。

## 已写短验证

1. Headless model：奇数宽高、非零 ROI offset、padded stride、借用源复用、深拷贝导出、frame/summary 相同 copy/total 时间、布局错误只丢诊断。
2. Freeze + 快生产：front 固定而后两槽持续交换、只取 latest、固定最近30数值摘要、最终拒绝图像不被状态覆盖；状态 hasMotion=false、不增加 Tracker reject。
3. 真实两个线程 400 张 tiny frame 压力：消费者每次核对全部 BGRA、元数据 sequence、Tracker 数值及点 identity，最终到400；争用摘要只断言安全/有界，不要求非确定性精确丢数。
4. 在途 lease 关闭/新代重建：旧关闭不能撤销新窗口，旧 lease 发布失败，weak_ptr 证明最后在途引用释放后回收，原 Follow 身份可继续发布。
5. 两线程竞争 pause latch；实际 FreezeCapture::Session + fake backend 检查 Begin/Update/Read API、Quality、Callback、End、Timeout 的观测、原 retry/counters/acceptance 决策及实际采样间隔。

另在既有产品 `RunScrollFollowControlTests` 中增强真实业务链路（不创建 HWND，Capture/Host/ACK 外部边界仍是既有 seam）：开启 model 后 F0/实际 ROI；AwaitTracking 保留原 store 但不增加 CV/frame；第三拒绝终止帧和 Tracker 首因在 Requested 停止后保持；质量/API/clamp/无效 ROI 通过实际 Tick/Frame 断言原因。main 应在完整构建后运行 `Inkeys.exe --scroll-follow-protocol-test`，这与 standalone latch 测试分开，是实际 Business 控制链路的无窗口证据，不是真实 Mag/GUI 证据。

UI worker 提供的 RunExportTests 被严格 `--scroll-follow-debug-only` 调用；其完成情况和导出覆盖范围由 UI report 说明。

## 静态检查与待验证

Python XML parse 对主/测试项目及 filters 通过，无重复 ClCompile/ClInclude 项。对所有本次已有文件以 `git show HEAD:path` 比较并保持原 BOM；ScrollFollow.cpp 无 BOM，Capture.h/.cpp、animation_tests.cpp、主/测试工程及 filters 保持 BOM。全部相关文件保持纯 CRLF，新 model/测试为 UTF-8 无 BOM + CRLF。

`git diff --ignore-submodules=all --check` 通过。当前唯一 EOL 提示来自 root 正在同步的 spec/index.md，不属于本 agent 源码编辑。未运行任何构建/测试，以上用例均是已编写待 main 统一执行。OOM 未强制注入；异常丢帧路径由局部 noexcept/catch 审查，不能写成已实测。

真实可见排除、完整图例/交互/GUI 导出与约15–20s开/关性能仍受既有安全门及后续环境限制，不能用这些离线测试宣称通过。文件现已释放给 main/reviewer。
