# v3 全范围质量审查

审查角色：trellis-check。按主会话分工直接审查和局部修复，未创建子代理，未启动 GUI/HWND、完整构建或产品可执行测试；后两类由主会话集中执行。

## 已修复发现

### P2：新增设备策略测试参数可能落入产品 GUI 启动
- 文件：`Inkeys/IdtMain.cpp`（新增 `--draw3-device-policy-test` 分派）。
- 证据：原新增分支只对完整 `lpCmdLine` 做比较；额外参数、引号或尾空格导致分支不匹配，而普通产品初始化仍继续。上方同类像素/生产测试已使用 argv 和 count 门禁。
- 修复：仅把新增入口移动到现有 argv 块，保留大小写不敏感；`count != 2` 立即返回 2。未更改既有 legacy 入口。
- 已复验：最终 Debug ARM64 产物的普通、带引号、大写形式均返回 0；新增入口加 `extra` 返回 2，均无超时。证据：Build/Draw3V3Validation/debug-arm64-final-cli.json、argv-gate.json；尾空格未单独执行。

### P2：GPU 完成 EVENT 不可用仍可能导出 complete
- 文件：`Inkeys/Inkeys/Drawing/Draw3/Draw3.ResourceProbe.cpp`、`Scripts/Draw3Performance/Performance.Common.ps1`。
- 证据：`CreateQueries()` 若 EVENT 创建失败，仅写 `gpuStatus=query_failed`；`Drain()` 原来跳过空 `completion_`，但没有设 partial。完成全部迭代后可导出 complete + drain_complete=false；脚本仅校验布尔类型，可能纳入 v3 成对比值。
- 修复：保留 CPU-only 无 GPU 工作的提前完成分支；其他分支缺 EVENT 时设置 partial 和 `gpu_completion_query_unavailable`。脚本拒绝 complete + drain_complete=false。
- 边界：时间戳 unavailable 但 EVENT 成功仍允许独立 CPU 调用比较，GPU 时间保持 null。此源代码缺陷与 fixture 在并发构建期间启动超过 5 秒的环境延迟是两个不同问题。
- 回归：diagnostics_v3 代理增加 `drain_incomplete` fixture；WinPS5.1/pwsh 各 5 项专项断言通过，最终 pwsh 连同标题断言共 7 项通过。

### P3：用户可见诊断版本提示仍为 v2
- 文件：`Scripts/Draw3Performance/Analyze-Draw3-Performance.ps1`、`Run-Draw3-Performance.ps1`、`Run-Draw3-Performance.cmd`。
- 证据：全范围检索发现分析标题、采集启动提示和 CMD 横幅仍残留包 2 字样，会令用户误认本轮产物。
- 修复：diagnostics_v3 代理在最终冻结前统一改为 3；分析标题注明兼容 v1/v2，并增加实际报告标题和采集器横幅断言。历史 v2 fake 协议标记保留。

## 已核对的生产合同
- 合成专用 256 B DYNAMIC structured buffer/SRV，仅写两个 InkPoint 共 32 B，t0/offset0，资源冷注册/view 查找/退休/释放齐全；不修改 ink 大资源或 m_bufferHead。
- 大资源策略使用实际设备 adapter；Hardware + vendor 0x1D17 + compatibility=true 才 DEFAULT。CPU 品牌、其他 GPU 和 WARP 不触发。
- ink/highlighter 保留 3.2/4.8 MB、结构布局和原拆批；DEFAULT 使用 `[0, actualBytes)` 字节 box、offset0，API 为 void Copy leaf，GetDeviceRemovedReason 作为独立状态检查。
- stroke/shape/cursor/laser coverage/laser dots/highlighter 全调用集中上传 helper。动态路径成功上传统一推进 head；stroke/shape 使用上传前保存的 batch offset，不重复推进；Laser/Cursor DISCARD 后续 NO_OVERWRITE 不覆盖刚用的范围。
- 产品上传验证不只做三模式相等：先要求组合像素非空；另清目标后同一层连续提交红色 y=12 与绿色 y=28 两批，中间无读回/等待，最终采样要求各自颜色通道 >240、其他通道 <8、alpha >240，确保旧 DEFAULT 数据没有被后一次更新覆盖。各工具另有真实 buffer/offset 读回，但不把组合图像一致性宣称为每个工具都有独立预期像素 oracle。
- 两个配置默认 true、严格 bool、NoUpload；设置只 QueueConfigWrite。configOnce -> HostStartOptions -> TransparentPresentationOptions 按值保存，DComp 替换及 device-lost 恢复复用启动快照。关闭硬件加速直接 WARP，开启保留失败回退。
- 三语标题/说明包含重启生效、兆芯 GPU 生效范围；生成键和快照配套。

## 已核对的 v3 诊断合同
- 保留 v2 原 40 case 的定义和工作量，新增 16 paired case，总计 56；paired ink/highlighter × 64/4K × dynamic/default × HW/WARP。
- 两侧相同主几何、shader、上传字节、目标尺寸、operator add copy 和 256 B 小合成；动态墨迹按能力环形追加，荧光笔逐批 DISCARD；DEFAULT 局部 offset0 更新。
- 分析先核实 session/case/PID/FILETIME/EXE/backend、实际 resource/原始 Draw 与上传量、adapter/shader/工作量；缺少一侧、partial 和不匹配不产生 winner 或零比值。
- GPU 4 轮 measure 和 CPU leaf、drain 分开；时间戳不可用为 null。报告明确不是完整 Host/RTS/Present A/B。
- README 要求兆芯开/关兼容各重启采集同动作，高通用 ARM64 普通动态路径；采集器不改用户设置，原始 v2 目录只读。

## 检查结果（最终）
- `git diff --check --ignore-submodules=all`：通过。
- `pwsh -NoProfile -ExecutionPolicy Bypass -File Scripts/i18n.ps1 check`：通过，en-US/zh-TW 各 366/366。
- 最终 31 个已跟踪修改文件：UTF-8 BOM/严格 UTF-8/CRLF/EOF 状态检查通过，沿用原文件是否带末尾换行的差异；证据为 Build/Draw3V3Validation/formats.json。
- Draw3Performance 全部 PS1 Parser：通过。
- TypeCheck/编译：Debug|ARM64 通过。主会话使用动态定位的 ARM64 原生 MSBuild，从完整 InkeysRepo.sln 入口构建，最终自然 exit0、timed_out=false、期限 2700 秒。证据为 Build/Draw3V3Validation/debug-arm64-final.json 及同名前缀 stdout/stderr。Release ARM64/x64 最终构建与交付门禁也已通过，见最终门禁小节。
- Scripts tests：WinPS5.1 与 pwsh 完整离线 suite 各 267 项 passed；新增 drain 专项各 5 项 passed，最终 pwsh drain+标题 7 项 passed。完整结果分别在 Build/Draw3PerformanceV3Tests/windows-final、pwsh-final、final-title-pwsh。主会话 native 检查的已获结果见下节。

## 已获得的原生验证证据

- 构建环境：首轮链接失败来自本工作区缺失 Abseil 头/静态库；主会话核对 manifest/triplet 相同后复用已有本地依赖，未改工程、工具链或依赖版本。该环境问题与本轮源码缺陷分别记录；最终 Debug solution exit0。
- 早期 CLI 七项全部 exit0、无超时：`--draw3-performance-test`、`--draw3-performance-launch-test`、`--config-boundary-test`、`--draw3-device-policy-test`、`--draw3-resource-probe-test`、`--draw3-resource-probe-list`、`--draw3-resource-probe-upload-test`。命令、时长及各自 stdout/stderr 见 Build/Draw3V3Validation/debug-arm64-final-cli.json。
- 真实 WARP 产品上传验证实际执行 dynamic_discard、dynamic_append、DEFAULT 三模式，stderr 明确 `PASS failures=0 no_hwnd=1 no_swapchain=1`。这验证前述资源/偏移/字节/工具交错/跨容量拆批及连续两批预期像素断言；不代替实际产品窗口呈现。
- Headless `--no-window` 自然 exit0，116.34 秒，无超时；末行 `PASS animation correctness`。结果同在 debug-arm64-final-cli.json，详细 stdout/stderr 使用 headless 文件名。
- 新 argv 门禁的带引号/大写/额外参数返回分别为 0/0/2；argv-gate.json 的 expected 与 exit_code 全部相符。
- v2 现场回归：主会话从原兆芯 v2 数据创建只读来源的工作区副本，源/副本 manifest SHA256 一致；当前 v3 分析器 exit0，72 个有效循环、34 个主绘制帧、37 次成功呈现、0 完整性问题，仍识别 capture_schema2。用户原始目录未修改。

## 首轮实际离屏矩阵与性能限制

- 主会话实际执行了 56 项矩阵：47 complete、1 partial、8 timeout；48 项身份验证成功。新增的 16 项 paired 全部 complete 且身份验证通过。
- 对本轮实际 CLI session/EXE 身份，直接调用真实 Analyze-ResourceMatrix 函数得到 8 组 comparable；没有伪造 launch/Host 采集记录，也没有据此声称获得产品 RTS/Present 测量。原始 partial/timeout 保留，未补零或补完整状态。
- 此轮与 Release 编译并行，结果只支持协议、身份及缺测排除验证；开发机并非兆芯，并行编译也影响调度与耗时。不能把本轮数值用于判断普通/兼容策略优劣。后续安静环境矩阵已全部通过，见最终门禁小节；本轮失败证据继续保留，没有被覆盖。
- 首轮矩阵记录时 Release ARM64 尚在构建；后续 Release ARM64/x64 与包校验已经完成，最终结果如下。
## 最终 native 与交付包门禁

本节只复核主会话已生成的 JSON 结果，没有重跑已通过测试或修改冻结产品代码。

- Release ARM64：完整 InkeysRepo.sln 构建自然 exit0，timed_out=false，1317.20 秒，2700 秒期限。证据：Build/Draw3V3Validation/release-arm64-final.json。
- Release x64：完整 InkeysRepo.sln 构建自然 exit0，timed_out=false，1033.39 秒，2700 秒期限。证据：Build/Draw3V3Validation/release-x64-final.json。两者均使用定位到的 ARM64 原生 MSBuild 和既有工程配置。
- Release 早期验证：release-arm64-final-cli.json 与 release-x64-final-cli.json 各 7 项，均 exit0、无超时；包含性能 recorder、launch 协议、配置边界、设备策略、resource test/list 和真实 WARP 产品上传/绘制读回。
- 安静环境实际矩阵：Build/Draw3V3Validation/offscreen-release-arm64-quiet-final/validation-summary.json 为 status=complete，56/56 complete、56/56 identity_verified，problems=[]，耗时 29278.0451 ms。先前编译负载下的 partial/timeout 原始结果继续保留。
- 真实矩阵分析：同目录 matrix-analysis.json 为 status=complete、issues=0；8 组 upload_pairs 全部 comparable。CPU、GPU 区间与 drain 的解释边界不变，不把 comparable 等同于目标产品流畅度通过。
- 包校验：Build/Draw3V3Validation/packages.json 包含 ARM64、x64 两项，均 package_revision=3、case_count=56、entry_count=7；每项 validation 均为 `all manifest,PE,capability,script and ZIP entry hashes passed`，并记录 EXE/ZIP SHA256。
- 已生成 ZIP 位于 Build/Draw3PerformancePackages/v3-20261015：`Draw3Performance-v3-arm64-20261015-141304-7726cf26b49b4081b23dd858224aae39.zip`、`Draw3Performance-v3-x64-20261015-143055-45963d123e554a10bef51fe3a85374fa.zip`。
- 最终结论：native 正确性、协议和交付包门禁通过；没有剩余确认代码缺陷。开发机为 Windows ARM64，Release x64 的进程验证也不能代替兆芯硬件测试。未执行目标机 GUI、RTS、真实 Present 或完整 4K 流畅度验收；兆芯普通/兼容优劣及高通改进仍由用户实机 A/B 数据决定。
- 交接：本次仅更新本审查记录，不创建 commit/push。向主会话回报后停止写入活动 task 路径，由主会话进行 --no-commit 归档和日志记录。

## 未修复 / 验证边界
- 暂无确认的剩余代码缺陷；Debug/Release 构建、native CLI、脚本、格式、规格同步及双架构交付包门禁均已通过。目标机完整产品性能仍保留现场验收边界。
- 目标兆芯与高通 GPU 的 4K 流畅度、真实 RTS/Present、驱动差异和设置视觉仍待用户实机验收；本机 WARP 离屏不代替现场。
- 主会话已同步 draw3-integration 的默认/例外设备策略，以及 diagnostics v3 的 56 case、EVENT 完成、启动元数据与两轮 A/B 合同；保留历史 v1/v2 原始读取兼容。已复核新增规格与代码一致。
- 普通 git status/diff 在 HiMsg 子模块 git 元数据处受沙箱权限限制；采用 `--ignore-submodules=all` 的只读检查，不修改子模块或 Git 配置。
