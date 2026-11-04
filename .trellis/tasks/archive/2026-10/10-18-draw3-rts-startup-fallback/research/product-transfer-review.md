# Draw3 产品修复迁移独立审查

日期：2026-10-18。审查对象：`codex/draw3-compatibility-fixes` 相对 `origin/dev`（5d319bbf）的完整工作区变更，以及新增的两个生产头、两个 Headless 测试。已按 trellis-check 读取任务 PRD/design/implement、七条 check.jsonl 上下文和 native-desktop/shaders 索引。根代理负责构建与执行测试，本检查代理不重复编译，不运行 GUI，不提交或 push。

## 结论

静态审查未发现阻塞性产品缺陷或诊断体系迁入。当前生产迁移范围与用户要求一致；本结论须结合本分支实际构建/测试结果使用，不把原诊断分支的通过记录当成本分支验证。

## 来源范围核查

- 42745199 的 ContactInput、DrawingController、InkHistoryGpu、LaserParticleSystem/LaserParticles、RendererSnapshot 变更逐项核对后均为 Performance 会话接线、资源登记、计时包裹、输入 proof 追踪或其测试。未发现需独立移入的产品修复；保持这些 dev 文件原样可避免把诊断控制流带入产品。
- 22f7f3c9 的硬件偏好、真实 Hardware/VendorId=0x1D17 兼容门禁、DEFAULT 上传、小合成资源及配置/UI/三语部分被提取；Performance/ResourceProbe/包脚本和历史任务未提取。
- e8dd090c 的 RTS 基础 X/Y 与鼠标回退、callback 关闭/排空、owner 队列停止屏障、启动/退出接线及回归测试被提取；不依赖新的 Performance API。
- 搜索生产源码/工程/测试/脚本未发现新 Draw3.Performance、PerformanceSession、PerformanceGraphics、ResourceProbe、IdtPerformanceLaunch、performanceOutputDirectory 或 ConsoleOutput.Performance 残留。dev 原有 Draw3.PerformanceProbe 保留。

## 端到端检查

1. 配置以 main.json 的严格 bool/NoUpload 字段声明，默认均 true。设置只保存下次启动偏好；三语 key、生成头、基准快照相符。Main 从 configOnce 复制进 HostStartOptions，Host 同时传给初始 Graphics 和 TransparentPresentationOptions；恢复路径从 Presenter 的同代 options 重新创建设备和 Renderer，不读取运行中实时配置。
2. DEFAULT 兼容只允许实际 Hardware、可取得 adapter 身份、VendorId=0x1D17 且开关开启；WARP、其他 GPU、身份未知和开关关闭保留动态路径。硬件加速关闭直接 WARP，开启保留 Hardware 失败后 WARP；原 Win7 11_0 E_INVALIDARG 重试保留。
3. ink/highlighter DEFAULT 资源 CPUAccess=0，按有效字节和 offset0 UpdateSubresource；调用 void API 后独立查询 device-removed 状态。动态路径仍按真实 NO_OVERWRITE 能力选择追加/DISCARD；统一上传后维护 head，DrawStroke/Shape 使用上传前保存的 offset。Laser/Cursor 的 DISCARD 更新 head，避免下一次追加覆盖本代已用槽。
4. ApplyOperatorLayers 使用独立 256B DYNAMIC structured buffer、stride16、t0、两个 InkPoint，不写大 ink buffer、不改变 head。资源创建/释放匹配，HLSL、常量布局、混合与资源嵌入链未改。
5. RTS 扩展配置/Enable 的可恢复失败先清理并 Complete/join 独立监督，再以新实例尝试 X/Y；公共基础设施失败不重复相同配置。候选 Down 在 callback 入口捕获权限，不能因稍后 ready 把旧 Down 发布到 consumer。业务回调和诊断借用均纳入 lease，Shutdown 先关门、无内部锁 Disable/Remove、等待 event 排空，再终止 contact 和清借用。Disable/Remove 不能证明停产时仍致命收口，不带着旧 producer 启用鼠标。
6. Host 只有 RTS 安全 false 后才请求 mouse owner，窗口 start 确认后按 InputReady 放行；RTS 模式仍无鼠标 Contact producer。鼠标使用有符号坐标、真实 QPC、MouseLeft/MouseRight、未知能力，并只接现有 Coordinator/绘制工具链。
7. 鼠标单活动流、左右键接管、重复/迟到 Up、选择/页边界隔离及失捕获重同步已逐项与测试核对。跨线程启停使用 PostMessage、独立事件及稳定对象身份；关闭 startAllowed 防止迟到 start。停止先关 contact 准入并预约最终保存，再由 owner 取消/释放捕获和确认、RTS Shutdown、发布保存唤醒、排空保存 worker、join 绘制线程、Detach。窄内部 stop 转发可通过 productStopping；等待不持 productCallMutex。
8. 两个新测试在标准 --no-window 套件登记，使用生产共享状态/编排与真实 Coordinator。两个新产品 CLI 在任何 HWND 初始化前严格验参后返回；设备策略测真实生产 helper，上传测试使用真实 WARP 离屏资源/生产 Draw、读回字节与像素、交错与跨容量批次，未借用隐藏 HWND 测试。

## 本代理执行的检查

- `git diff --check --ignore-submodules=all origin/dev`：通过。
- 34 个已跟踪变更文件及 4 个新增生产/测试文件的 UTF-8、纯 CRLF；已有文件相对 dev 的 BOM：38 文件检查通过。
- Inkeys.vcxproj、filters、InkeysHeadlessTests.vcxproj XML：解析通过；头与测试登记一致。
- 没有修改产品源码或测试，也没有提交/推送。构建、i18n check 和命令行测试退出码由根代理记录到本任务验证材料。

## 集成编译发现与修正

根代理首次完整构建发现 IdtMain 新增 CLI 所调用的导出函数缺少直接 module import，出现 C2039/C3861。根已最小加入 graphics_initialization 与 renderer 两条 import；旧生产测试函数的头声明不能代替新增 module 导出可见性。此处已静态核对调用/导出/import，最终以根的 build02 完整退出码为准。

## 验证界限

静态和无窗口验证不能证明真实故障机接受 X/Y、真实 COM provider 的停产行为、USER32 捕获/重入与系统 Pen/Touch 的鼠标提升、DComp/ULW Present 或首帧。实际兆芯 GPU 性能和原故障设备启动/绘制仍为现场待验；没有扩大 Windows 支持声明，没有加入通用进程重启。
