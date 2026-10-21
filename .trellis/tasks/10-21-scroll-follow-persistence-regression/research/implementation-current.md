# ScrollFollow 私有持久化与编辑回归实施（2026-10-21）

- 角色：本任务 trellis-implement；源码冻结于 23:13 +08:00，随后只写本报告。
- 分支/提交基线：`feature/move` / `3ace726b`；没有 commit/push，没有派发子代理。
- 本轮仅执行严格早退的无窗口或自建 HWND 全程不可见测试；没有 Computer Use、普通产品启动、可见内容、前台/置顶、旧 `--draw3-hidden-test` / `--draw3-eraser-hidden-test` / `--freeze-mag-printwindow-test`。
- **结论：四个真实 Host→原 Clear/Exit worker→实际 UInk 文件回读场景通过。普通 Pen 和 SolidLine 的 A 均在 Clear 真提交、CPU fallback 释放、真实 worker Loaded 安装后，改变 Viewport 并保存；A 的全部 world 点/宽度/style/工作区与页身份不变。新 B 使用新 Viewport，Follow 保留 Redo，原 Undo/Redo 与有序 eraser mask 正常进入最终文件。** 这不是实际 Mag/系统窗口穿透/可见墨迹或任意跨进程 Desktop 冷载的验收。

## 本阶段文件与最小边界

新增 `Inkeys/Inkeys/Drawing/Draw3/Draw3.DesktopPersistenceTest.h`：

- 严格 harness 宏内的有限 `HiddenDesktopRecoveryEvent/Receipt/Snapshot`，只含事件、file/workspace/page GUID、CPU fallback 是否释放以及四个累计事实计数。
- 事件为 `CommittedConsumed`、`LoadRequested`、`LoadedInstalled`、`MemoryRestored`；没有 completion 注入、通用 Load/Import/Replace 或生产保存策略。
- `RunScrollFollowPersistenceTest() noexcept` 声明；实现关闭 harness 时仅返回 2。

修改：

- `Draw3.DrawingController.cppm/.cpp`：仅在统一 harness 下增加可空 observer。在原 Clear Committed 的 file 匹配分支，确实设置 `diskReady` 且 `canvas.reset()` 之后发 `CommittedConsumed`；真实 `desktopLoadRequested` 接受并记下 history revision 后发 `LoadRequested`；原 `CanInstallDesktopClearRecovery` 校验通过、实际 `Loaded` 且 `restoreCurrentPageFromSnapshot` 成功后发 `LoadedInstalled`。内存恢复成功的分支单独记 `MemoryRestored`。保存请求、完成资格、history floor、恢复/保存语义不改。
- `Draw3.Host.h/.cpp`：harness 内短 mutex 保存上述值事实，只有 `enableHiddenTestContactInjection=true` 才绑定 observer。原 `HiddenPersistenceSnapshot()` 在原 worker 诊断之外复制该值；Start 在旧 owner 真 join 后清零该代事实。没有跨线程读 Controller/document/Canvas 字段。声明、初始化、赋值、读取和 Controller 发回分支均有相同宏保护。
- `Draw3.HiddenWindowTest.cpp`：同 TU 复用 `MakeHiddenSpec`、真实 Window Service、ULW Host、原 contact mailbox、生产 Follow Begin/latest/End、原保存与严格文件 reader；新增四场景专测。显式开启 128 容量真实 runtime metrics，交叉检查 contactSeen，普通产品默认不变。
- `IdtMain.cpp`：最前全 argv 扫描加入 `--scroll-follow-persistence-test`，完整前缀识别但只准精确单参；错位、额外、重复和后缀均在配置、单实例、普通 HWND 初始化前返回 2。harness-off 路由也先返回 2。
- `Inkeys.vcxproj/.filters`：仅登记新 header；新函数在已有测试 TU，不增加构建体系、依赖、SDK、库或新项目。

本阶段没有修改 UInk v10、Desktop/PPT 保存与 codec 实现、`Draw3.StrokeGeometry`、Shader、墨迹/预测参数、导航门禁、Capture 或 Tracker。新增代码的必要注释为中文。

## 可重复入口与隔离

```text
# cwd 必须是含 InkeysRepo.sln 的仓库；Build/ScrollFollow 已存在且非 reparse
Build/ARM64/Debug/Inkeys.exe --scroll-follow-persistence-test
```

入口逐组件拒绝 reparse，检查仓库标记后，用 `CreateDirectoryW` 新建 `Build/ScrollFollow/persistence-PID-tick`；四个 scenario 根也都是 create-new，原 `HostStartOptions.autoSaveRoot` 明确指向各自私有根，`ProductState.autoSaveEnabled=true`。证据 JSON 用 `CREATE_NEW` 并 flush，不覆盖未知文件，不触碰用户配置、文档或应用数据。

只创建私有 MagnifierHost/Freeze/DrawpadPresentation/Drawpad 四个普通 fixture HWND，初始及全部等待/关键节点检查 `IsWindowVisible=false` 和当前 PID；没有真实 Mag 初始化/屏幕图案，没有调用真实显示、Reconcile 显隐、前台或置顶。原 style callback 仅复用测试已有样式合同。实际图形日志为本机 `driver=Hardware`，不是 WARP 模拟，也未做独立 GPU 像素比对。异常/早退 RAII 先 `StopProduct()` 真 drain，再 `service.StopAndJoin()`，最后才销毁 style callback context。

正常 Follow End 等真实 owner final/full Present 与恢复收敛、恢复原输出 ready；**窗口交接 ACK 明确为 `channel->Handoff(session)` 的严格 fixture seam**，不会显示窗口。Viewport-only 场景 Publish 最后目标后不预等 Applied、不再发 Down/B，直接 Stop；真实 Shutdown final 在原退出捕获前应用 CPU Viewport。

## 四个真实文件场景与结果

四个场景各自从真实 Down/Move/Up 开始，先原 Clear：

1. 等原 Clear 处理及 Controller 的 `CommittedConsumed`，要求 CPU fallback 已释放；严格读取原每日 index 指向的实际 Clear UInk，取得 A 的完整值基准。
2. file/workspace/page GUID 与 owner receipt 对上后再 Undo。要求 `LoadRequested=1`、`LoadedInstalled=1`、`MemoryRestored=0`；因此 A 是现有 worker 从真实 committed 文件读回并安装，不能用 sleep、worker committed 计数或 `hasContent=true` 冒充磁盘安装。
3. 恢复后用当前 Channel 凭据 Begin，校验真实 workspace/page 身份；真实 owner 按 `base - total` 应用 `total=(-144,64)`，得到 `Viewport=(144,-64,1)`，没有第二次 DPI 或坐标换算。
4. 真 Stop→原 PrepareExitAutoSave→worker CloseAndDrain。每个场景恰 2 个 accepted/committed、0 failed、0 pending，分别为 Clear 基准和 Exit；严格 reader 核对 Exit trigger、新 file GUID、同 storage session 且 sequence 加 1。额外 `ReadUInkFile=Complete`、`ImportDraw3UInkDocument=Success`，严格 decoder 只接受 v10，完整导入再逐项检查真实点/style/顺序和 viewport。

最终实际运行 `persistence-pen-test*`：exit 0，外部 **5,237ms**，四个 Host 启停与全部文件检查合计时间，不能当作 Capture/Tracker FPS。stdout 33,204B，stderr 4,088B；包含正常 HWND/RTS/最终 PASS，以及有界诊断队列一次 `dropped=2 contention=2`，没有文件/索引失败。断言和持久文件是验真依据，没有靠完整日志条数判断 durable。

| 场景 | A 的真实点数 | Exit 内容 | A 的全部 `(x,y,width)` 与 style | 最终 Viewport | 真 disk install / memory restore |
| --- | ---: | --- | --- | --- | --- |
| shape-viewport-only | 2 | A | 全相等 | `(144,-64,1)` | 1 / 0 |
| shape-edit | 2 | A、B、Eraser mask | A 全相等；B 全点恰偏移一次 | `(144,-64,1)` | 1 / 0 |
| pen-viewport-only | 23 | A | 全相等 | `(144,-64,1)` | 1 / 0 |
| pen-edit | 22 | A、B、Eraser mask | A 全相等；B 真实 Down world=`(224,36)` | `(144,-64,1)` | 1 / 0 |

普通 Pen 用 `Bridge::Tool::Pen`、完成类型 `Drawing`、文件 kind `Pen`，不是 Shape 替身；23/22 是这次实际建模点数，不是固定测试输入数组。A 的每个宽度、位置、kind/opacity/fallbackRgb/texture 都与真实 Clear 基准精确一致；首个 world 锚点由实际 Down `(80,100)` 验证。B 的独立建模受实际时间影响，不强迫两笔全数组相同；只检查文件中真实 Pen 首锚点为 Down+新视口，即 `(224,36)`，style 一致且有多点。

编辑两个场景都另画 C 再 Undo，形成真实 Redo；Follow End 后仍可 Redo，Undo/Redo 命令计数不变，说明 Follow 不增加历史。Redo C→Undo C 后再画 B，B 按原语义取消 C 的 Redo。随后 Undo B→Undo A 到空，Redo A→Redo B；切原 FixedEraser 并真输入、Undo/Redo 后最终文件仍按 A/B/mask 顺序保存。工具交接靠真实 selection/output 代等待，不用固定 sleep。

Eraser 是原有序 **mask Stroke**，没有删除或改写 A/B 点。新增路径经过 B 中部，按文件中实际 mask 点/宽和 B 的 capsule 中心线/半径计算，两个编辑场景的正覆盖余量分别 **约 35.75px / 36.04px**（断言 >1px）。这证明实际保存的 mask 几何命中 B；没有把工具发布、history 内容 bool 或这些数值称为屏幕擦除像素验收。

最终证据目录：`Build/ScrollFollow/persistence-45392-143503171/`，共 **8 个真实 .uink**，每个 scenario 的 `desktop/2026-10-21/index.json` 指向对应最终 Exit。`evidence.json` 保存每个基准与 Exit 的 file/workspace/page GUID、完整真实点/style、Viewport、实际路径、receipt 和 mask 余量；日期读取同时考虑运行起始日与当前日，不用 mtime 猜最后文件。离线再次读取 JSON 验证四个 A 点数组及 style 全相等。

## 构建、参数与格式检查

复用 ignored `Build/ScrollFollow/build-motion-tracker.ps1`：vswhere 定位当前 ARM64 原生 MSBuild；同 PowerShell 规范单一 Path，`MSBUILDDISABLENODEREUSE=1`，完整 `InkeysRepo.sln Debug|ARM64 /m:1 /nr:false /p:BuildInParallel=false /p:PreferredToolArchitecture=arm64`；每次期限 10 分钟，normal host `require_escalated`，没有并发 MSBuild。

- 最终 `-Round persistence-pen-relink`：**exit 0，外部 7,926ms / MSBuild 7.68s，0 warning / 0 error**。此前改动 TU 已编译通过；同原配置完整 solution 成功重链。
- 本机已知 `link.exe IncrBuildImage C0000005/LNK1000` 在 first、metrics、observe、pen-compile 出现。每次保留日志，只在核实绝对路径属于工作区 `Build/ARM64/Debug/Inkeys.ilk` 后删除该生成文件，再原配置完整重链；没有删除 PDB、修改项目/SDK/依赖或关闭链接选项。
- 实施中 fixture 两次等待失败：SolidLine 的完成类型误写为 Drawing，现有准确类型为 Shape。另修正“同一内容 bool 下每笔必增 contentRevision”的错误测试假设，并显式小容量 metrics 检查 contactSeen。hidden 注入原来就启用 Coordinator 计数，早先推测计数关闭不是已确认根因，不作为生产缺陷。新增 helper 的一次 Windows `max` 宏冲突编译错误已用原括号习惯修正。这些都属于测试代码修正，未改生产保存/历史行为。
- 最终新 flag 的 extra、misplaced、repeat、`=bad`、无等号后缀、混合另一 test、三参数共 **7 项全部 exit 2，stdout/stderr=0B**，单项约 95–180ms；日志 `persistence-final-argv.json`。parser 在完整 argv 扫描，关闭 harness 时同样先拒绝；本阶段没有另建 Release/harness-off 运行产物。
- 9 个本阶段源/工程文本 strict UTF-8、原 BOM、物理 CRLF 通过；IdtMain 保持原无末尾换行，其它保持原末尾换行，新 header 无 BOM+CRLF。两份工程 XML 解析、owned 范围 `git diff --check --ignore-submodules=all` 通过。`persistence-final-static.json` 保存所有源/产物 hash。

最终产物 SHA-256：

- `Build/ARM64/Debug/Inkeys.exe`：`F2AFC228E853A48D6BCC143975F06D83FCA1290E4CBFAB8EA781F3B0FB494D4D`（83,044,864B）。
- `Build/ARM64/Debug/InkeysHeadlessTests.exe`：`ED614767D28D07A27D60B0197F964097645B321A5229BD6502BA7E7E6727456A`（24,245,248B）。Host.h 的 harness 布局依赖使 Headless 重编，本阶段没有无故重跑完整三分钟矩阵，独立 check 可按实际变化选择回归。

主要源码 SHA-256：

| 文件 | SHA-256 |
| --- | --- |
| Draw3.DesktopPersistenceTest.h | `E9C1C38C46B63F2A6844DDDF6E5E04587ABDD44F63CF7560D65F953553A4F195` |
| Draw3.HiddenWindowTest.cpp | `762A09A584348644BE0151DB335118D6C7CB6812A10DFA3F8F6A1BD7D71CA218` |
| Draw3.Host.cpp | `0235DBFC2C3EA84A5B0E9A86C5F6B6EC4E467CDBE08854C1668278A7A5F4DD3A` |
| Draw3.Host.h | `4127EAB3D76B1E8F369E195512B7806A25105D118C82DBB48B5E0F766C9AB33D` |
| Draw3.DrawingController.cpp | `83533418E5B1D7D6B1CF4B3A65E7EAA2141150782EE3DA7B334E74B29364F324` |
| Draw3.DrawingController.cppm | `718098E35A6F6B2240A722406E0C264F0F9768D3F6CC5E9864418F3CEF4E37E1` |
| IdtMain.cpp | `110F8EE51477C258BF501549C3C0034F7CF644DC8B7C21D1F7E0B055239BE367` |

## 交接与未放行的门槛

本阶段没有确认而未修的生产保存/编辑缺陷。独立 trellis-check 尚未完成，本报告不代替它；源码已冻结，无运行中的 build/test，交主会话顺序派发。主会话维护任务/spec/总矩阵及最后 x64、PE、i18n；本实现者不并发执行。

共享 storage 四组原 CLI、产品 protocol/control/State/Config/全隐藏 owner、一次完整 `--no-window` 与原失败接管的既有结果见前置任务报告，本阶段没有重复，也没有把它们冒充新保存证据。

当前 Host 没有任意外部 Desktop 文件的冷载/替换 API。这里实测的是本进程合法 Clear Undo 的真实磁盘恢复；该恢复有意保留当前 Viewport。本测试分别证明恢复后的 Host 可再次 Follow、以及文件 codec 保留保存后的 Viewport，**没有证明跨进程 Host 冷载已恢复文件的旧视口**，也没有为验收扩大产品导入功能。

四场景真实输入来自 fixture mailbox，运动来自生产绝对目标 Channel，窗口 ACK 是明确 seam。实际 `Mag F0→Tracker→Business→系统成对显隐→底层滚动`、正式 MagnifierThread 的连续变化像素/排除/帧率/资源、Freeze 前后、浏览器/Word/PDF、主栏视觉、笔/触控及旧 Windows/WOW64/显示设备事件仍须原授权之外的必要现场验收。软件检查通过不能放行这些项或把父任务标记完成。
