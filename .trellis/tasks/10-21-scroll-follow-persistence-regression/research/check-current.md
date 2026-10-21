# ScrollFollow 保存与最终软件检查（2026-10-21）

本报告为本阶段顺序独立 trellis-check。已读取本任务 prd/design/implement、check 上下文、保存 implementation-current、产品 core/check、storage-baseline、viewport-only 研究与相关规范。`get_context.py --mode packages` 返回单仓库、没有配置 package；实际修改属于 native-desktop，另按 native Quality Check 与跨层指南检查 owner/CPU/GPU/文件边界。共享 native、Shader、PPT 的生产源码未因本阶段改变，不把独立 demo 或历史基线冒充产品验收。

**结论：本阶段发现的一个测试精度缺口已修复，完整 ARM64 Solution、四个真实文件场景及相关短回归通过，源码/build/test 已交还主会话冻结。可以进行串行 x64 与 PE 最终检查。当前软件证据仍不能放行真实 Mag、系统成对窗口 ACK、可见交互或硬件验收，不应把父任务标为完成。**

## Findings (fixed)

- File: `Inkeys/Inkeys/Drawing/Draw3/Draw3.HiddenWindowTest.cpp`，新保存专测的 `equalStroke`。
  - Issue: 同一比较器用于原 A/codec 往返和另画 Shape B 的平移；原实现对所有 x/y 均允许 0.001px 容差。即使实际运行没有发现漂移，这个断言仍不足以严格保证“原始坐标未改写”。这是测试精度缺口，不是已发现生产坐标漂移。
  - Fix: 零偏移比较要求 x/y 逐值精确相等，width 与 style 仍逐值比较；只有另画 Shape 的真实非零平移保留必要浮点容差。普通 Pen B 不要求整条建模数组与 A 相同，继续核实际 Down 锁存一次 viewport。
  - Verification: 最后完整 ARM64 重编后，四场景全部通过强化断言；另从新证据 JSON 独立比较四个原 A 的完整点/width/style，均精确相等。没有修改生产保存、追踪、笔锋/建模、擦除、历史或 UInk 格式。

## 核对的实际执行路径

- 新内部 flag 扫描整个 argv，在配置、单实例及普通 HWND 初始化前返回；关闭 harness 的路径也先返回 2。新函数主体及 receipt 类型、observer、存储、初始化、赋值、读取使用同一 harness 宏，只有 `enableHiddenTestContactInjection` 才绑定观察者。新 header 已登记主工程及 filters。
- `CommittedConsumed` 位于原 fileGuid 匹配的 Clear Committed 分支，确实在 `diskReady=true`、CPU `canvas.reset()` 后发送。`LoadRequested` 在原 worker 接纳请求并记录 history revision 后发送；`LoadedInstalled` 在原 CanInstall 资格检查、真实 Loaded 与 restore 成功后发送，内存恢复另计。receipt 只是有限事实值，不提供 completion 注入或任意 Load/Replace 能力。
- 真正严格 reader 持有非 reparse 路径 lease 和原日期命名 mutex，验证每日索引后读取其最后 committed 文件；`ReadCommittedDesktopPath` 调用真实 ReadUInkFile/Import，核身份。专测还直接对最终路径要求 Complete、Success、v10 和逐笔 codec 往返。因此 baseline/final 均来自实际索引与 UInk，不是内存拼造快照或只看 worker 成功计数。
- 每个场景先真实 mailbox 输入 A→Clear；等 owner 消费 Committed、释放 CPU fallback，再 Undo 走磁盘 Loaded 安装。file/workspace/page receipt 必须与真实 Clear 文件一致，loadRequested=loadedInstalled=1、memoryRestored=0，不能以固定 sleep 或 hasContent 替代。
- Shape 和普通 Pen 分开运行。普通 Pen 的完成类型为 Drawing，文件 kind 为 Pen，具有实际建模多点；Shape 的两端点不能替代这层证据。A 在 Follow/保存前后逐点不变，真实 B 的 Down world=(80,100)+(144,-64)=(224,36)。
- 编辑分支先生成可 Redo 的 C，再 Follow；End 后原 Redo 保留且 Undo/Redo 命令计数不变。恢复/撤回 C 后真实 B 清除其 Redo，随后 Undo B/A 到空、Redo A/B，再真实 FixedEraser 输入及 Undo/Redo。最终文件有序为 A、B、mask，并根据文件内真实几何验证 mask 覆盖 B；没有以工具发布或完成 kind 冒充擦中。
- viewport-only 在 A 磁盘安装后不再发送 Down/B，发布最后绝对目标后不预等 Applied，直接 Stop。原 Host Shutdown final→PrepareExitAutoSave→worker drain 后得到新 Exit 文件，sequence 增一、fileGuid 更新，A/world/page 身份保留且 viewport=(144,-64,1)。没有增加逐帧保存或新 dirty/去重策略。
- 全部 I/O 使用仓库下新建私有目录，逐组件拒 reparse，目录与 evidence 文件 create-new；测试不碰用户配置或文档。Cleanup 先真 StopProduct/drain，再停止 Window Service，最后销毁 callback context。自建 HWND 在创建、等待、操作、清理节点持续核当前 PID 与 visible=0，没有实际显示、激活、前台、置顶或 Computer Use。
- normal End 使用真实 owner/full Present/recovery 与原工具输出 ready，但 `channel->Handoff` 是明确 ACK seam。已与主会话更新后的 scroll-follow-product.md 有限 receipt/精确比较合同核对一致。上一阶段的 expected Channel、退出 Arm 顺序、单槽/End/动作合同在本阶段保持。

## 实测结果与证据

最后强化断言的真实目录为：

`Build/ScrollFollow/persistence-38284-144242000/`

四场景合计 8 个实际 `.uink`（各 Clear、Exit 两个）及最终 `evidence.json`。本次独立 CLI exit 0，4,194ms，stdout 33,205B、stderr 4,002B；stderr 包含正常 HWND/RTS/场景及最终 PASS，没有文件失败或可见窗口。时间是四个 Host 启停和文件验证总计，不是捕获/追踪 FPS。

| 场景 | 实际 A 点数 | 最终内容 | 原 A/codec 点与 style | 最终 viewport | disk install / memory restore |
| --- | ---: | --- | --- | --- | --- |
| shape-viewport-only | 2 | A | 逐值精确相等 | (144,-64,1) | 1 / 0 |
| shape-edit | 2 | A、B、mask | 逐值精确相等 | (144,-64,1) | 1 / 0 |
| pen-viewport-only | 23 | A | 逐值精确相等 | (144,-64,1) | 1 / 0 |
| pen-edit | 23 | A、B、mask | 逐值精确相等 | (144,-64,1) | 1 / 0 |

每场景均 accepted=committed=2、failed=pending=0；最后文件严格为 Exit、新 GUID、同 storage session、sequence+1，workspace/PageGUID 不变。两个编辑场景的 B 首锚点为 (224,36)，实际 eraser 正覆盖余量约 35.75px、36.04px。原 Pen 点数受真实采样时间影响，上一独立轮为 22/22；两轮均比较各自原 A 与最终 A，未强求不同运行的建模数组相同。

忽略目录中的 `check-persistence-evidence.py` 独立读取 JSON、每日 index、8 个真实文件，核对 A 精确相等、GUID/序号/触发、receipt、文件数/大小/SHA，以及重新计算 mask 覆盖。汇总 `persistence-check-evidence.json`。Python double 与 C++ 原 float 子表达式的覆盖计算差约 4.8e-7px，不影响大于 1px 的硬断言；没有把该离线脚本称为另一份 UInk decoder，真正 Complete/Import 是 C++ CLI 的实际结果。

## Verification

- **Lint: pass。** 无独立可复用 C++ lint 目标；最终 `git diff --check --ignore-submodules=all -- Inkeys InkeysHeadlessTests Scripts`、47 个源/工程/资源 strict UTF-8、原 BOM、物理 CRLF、原 EOF 风格通过。IdtMain 保持原无末尾换行。三份工程 XML 与 SVG 共四份 XML 通过，新增 header 登记齐全。i18n check exit 0，en-US/zh-TW 各 426/426。静态/源 hash 记录 `Build/ScrollFollow/product-check-static.json`。没有递归修改或宣称验证第三方子模块。
- **TypeCheck: pass。** 复用完整 `InkeysRepo.sln Debug|ARM64` wrapper；vswhere 定位当前 ARM64 原生 MSBuild，同一 PowerShell 规范单一 Path、禁 node reuse、normal host、`/m:1 /nr:false /p:BuildInParallel=false /p:PreferredToolArchitecture=arm64`，每进程期限 10 分钟。最终原配置完整重链 exit 0，MSBuild 7.89s/外部 8,143ms，0 warning/0 error。
- **本次首个错误及恢复：** 最初冻结源完整增量构建 exit 0/2,476ms。加强断言后新 TU 编译通过，但 link.exe 在 IncrBuildImage 出现既有 C0000005/LNK1000，exit 1/14,564ms；只核实并删除生成 `Build/ARM64/Debug/Inkeys.ilk` 后，原配置完整重链成功。没有改 SDK、依赖、PDB、链接参数或生产源码绕环境。日志为 `motion-tracker-build-persistence-check-final*`、`-exact*`、`-exact-relink*`。
- **Tests: pass。** 最终结果如下；日志均在 `Build/ScrollFollow/`。

| 实际检查 | Exit | 外部耗时 | 日志/汇总 |
| --- | ---: | ---: | --- |
| 最终 `--scroll-follow-persistence-test` | 0 | 4,194ms | `persistence-check-scroll-follow-persistence-test*`、`persistence-check-exact-final.json` |
| `--scroll-follow-protocol-test`，含真实 Tick/Frame/Tracker seam | 0 | 1,098ms | `product-check-persistence-exact-final-targets-scroll-follow-protocol-test*` |
| `--state-mode-production-test` | 0 | 128ms | 同前缀 `state-mode-production-test*` |
| `--bar-view-adjust-config-test` | 0 | 198ms | 同前缀 `bar-view-adjust-config-test*` |
| `--scroll-follow-hidden-test`，含跨 Host 凭据 | 0 | 1,814ms | 同前缀 `scroll-follow-hidden-test*` |
| 最终保存 flag 的 extra/misplaced/repeat/后缀/混合/三参七例 | 全为 2 | 每例 <10s 限制 | `persistence-check-final-argv.json`，stdout/stderr 全 0B |
| Headless `--scroll-follow-only` | 0 | 263ms | `persistence-check-scroll-follow-only*` |
| Headless `--freeze-capture-only` | 0 | 2,038ms | `persistence-check-freeze-capture-only*` |
| Headless `--shape-recognition-only` | 0 | 1,897ms | `persistence-check-shape-recognition-only*` |

Headless 短回归在强化 HiddenWindowTest 断言前执行，之后仅主程序已编译的 test TU 改动；Headless 最终 SHA 未变。没有无理由重跑原三分钟 `--no-window` 或 96 秒失败接管：上一产品独立 check 已分别 exit0/157,972ms、exit0/95,773ms。当前 Capture/Tracker、原算法/配置/codec、退出监督生产实现未变；新增 receipt 只在隐藏观察路径，Host.h 引起依赖重编已经由最终 Solution 和相关短回归验证。这里复用的是已记录的源/依赖结果，不宣称新 Headless 字节等同上一产物，或本阶段又跑过全量矩阵。

共享 UInk、PPT、Desktop/PPT AutoSave 四组基线与独立 storage solution 结果仍引用 storage-baseline-current.md；相关源码未改，没有将其算作本阶段新文件证据。真实四场景保存结果是本次另行执行的产品 Host 链。

最终产物 SHA-256（之后 x64/PE 由主会话串行执行）：

- `Build/ARM64/Debug/Inkeys.exe`：`28B8E8291E28F341656681956D5C6E2B74895086B73B05067C04E9FBD9A63488`。
- `Build/ARM64/Debug/InkeysHeadlessTests.exe`：`ED614767D28D07A27D60B0197F964097645B321A5229BD6502BA7E7E6727456A`。
- 本次唯一修改的源 `Draw3.HiddenWindowTest.cpp`：`97857ACD3A797C16C26F135ED5925A2197B6E18F43E1AD400DCAE152B751A1A7`。

## Findings (not fixed) / 必要验收边界

没有留存本阶段已确认但未处理的生产保存/编辑错误。以下不能由本检查放行：

1. 实际 formal Mag F0→Tracker→Business→系统窗口 ACK→底层应用滚动的整链仍未执行。正式线程变化像素、排除、持续帧率/资源与 Freeze 前后必须单独现场验收，旧 owner 读回证据不能扩大。
2. 当前真实文件链使用 fixture mailbox 和生产绝对目标，窗口 ACK 是 seam；没有屏幕像素擦除/可见墨迹稳定/鼠标滚轮路由或真实 Bar/快捷键手感证明。没有执行任何可见、前台、置顶、Computer Use 或普通 Inkeys 启动。
3. 实测的是本进程合法 Clear Undo 的磁盘恢复，然后该真实 Host 再 Follow，以及文件 codec 保留 viewport。Clear Undo 有意保留当前视角；不能宣称任意外部文件或跨进程 Host 冷载恢复已提供/验收，没有为测试新增此产品能力。
4. x64/PE 交主会话执行；本检查未另建 Release/harness-off 产物运行，宏保护仅静态核对。Win7/8.1/WOW64、实际笔/触控、DPI/显示/device 故障、浏览器/Word/PDF、长期性能及 D3D Debug Layer/现场视觉尚未测。ARM64 软件通过不扩大平台支持承诺。

本阶段仅作上述测试断言修复及报告，未派发子代理、commit、push 或修改依赖/第三方。已通知主会话源码/build/test 冻结空闲；本报告后不再启动构建/测试或改生产源码，主会话可继续最终跨架构检查，并在必要实屏门槛处停止。
