# Research: Win7 comprehensive candidate independent review

- Query: 新候选矩阵是否覆盖已确认的完整 PS Draw/readback 分界，采集和推断是否保留必要控制变量。
- Scope: internal；只读计划/规范/实现/验证资料，仅写本报告。
- Date: 2026-10-06
- Task: `.trellis/tasks/10-02-win7-main-canvas`
- Status: FINAL LIMITED PASS；限定冻结诊断实现、生成字节、本机构建/自检与采集 mock/ProbeOnly 范围。不是 Win7 视觉验收或生产修复 PASS。

## Findings

### 设计判断

已读 `comprehensive-plan-20261006.md`、workflow、`native-desktop/draw3-shaders.md`、`shaders/cpu-gpu-contracts.md` 与 `native-desktop/input-and-ink.md` 相关诊断合同。未读取任何 implement/check JSONL。

新方案直接控制上一轮尚未隔离的三个变量：实际整数路由、完整 PS 的额外绑定、完整程序功能群。保留旧 18 项以及独立生产四项退出判定，增加硬件 fixture，随后同 EXE 普通/full ULW 两轮。设计没有发现足以否定执行的关键遗漏；也没有把旧透明读回冒称为 ULW 或 Pointer API 唯一根因。

预期 69 项新增矩阵算术一致：4 个路由变体 × 2 绑定状态 = 8；四资源组 16 mask 各测试并恢复重测 = 32；有限 Laser CB 对照 = 1；六群 without/with 与 CursorFamilyOnly 共 13 × 2 = 26；末尾原 PS/Cursor-only = 2。与旧 18 合计 87；每个 driver 独立记录。

### 实现冻结后必须逐项核查

1. Compact integer 的真实 DXBC 含转换和原 4..6/type4 guards，且错误路径为可辨非透明颜色；成功标准不能只检查 alpha 非零。Float 对照须保留相同输入、输出和 blend。完整入口常色须在转换前且依赖运行时数据以保持其它声明/执行块；不能被 FXC 折叠为另一 mini shader。
2. 原 PS 资源矩阵须查询实际 PS b1..3/s0/8 个 2D/4 个 Array 槽，真实保存、AddRef 持有并恢复原状态。mask=0 对应原状态的诊断基线；各组测试结束立刻恢复并重测同一原 PS。不得用 ClearState 替代恢复，不得修改 VS b0/t0 或生产 PS 对象。Dummy 维度、初始化、CB 字节和无 RTV 别名须核；有限 CB 组以同资源仅替换数据与零 CB 形成有效对照。
3. 功能群变体不等同于运行到该群。必须保存实际源码区间、FXC profile/signature/resource/指令/hash，确认 Cursor4..6 始终保留、Laser 排除 Cursor、所有其他类型仍按原位置或透明移除。单群加入/删除可能同时改变编译布局、声明或跨群交互，因此只能报告关联，不能凭组名宣布某行指令为根因。
4. 硬件 CLI 仅两个参数精确识别、初始化前早退，创建永不显示的 HWND；不得软件 fallback。初始 D3D11 hardware 创建只有指定 unavailable HRESULT 返回 3，其它创建/后续失败返回失败。硬件 SKIP 不能伪造执行了 87 项；正常应用实际 adapter 单列，独立 hardware probe 不强制改变普通后续两轮 GPU。
5. collector 每 driver 600 秒属于有界采集上限而非硬实时总时长保证；driver/API 创建仍可能阻塞。两 probe 必须各自产生完整 raw/identity/status，超时精确终止子进程并显式标记部分输出。FAIL/UNAVAILABLE/启动错误后仍继续可执行的正常/full 两轮；null ExitCode、缺字段或旧 schema 不得计 PASS。最终 manifest 与当前预期 87/18/69 数匹配。
6. 一 CMD、唯一三阶段根路径、同 EXE SHA；精确 env opt-in、finally 恢复、无持久配置修改。普通/full 仍只控制 prcDirty；若源零，两轮不能证明显示恢复。用户需提交根目录及两次视觉观察，现场 PS2 与本机 WinPS mock 的验证范围须分开。
7. 保留前 6 个旧数组、8 正式 CSO/资源301–304、原现场资料和旧包；missing-before 已缺文件必须标记为缺失，不能计本轮保持 PASS。所有产品改动须限定于自检入口/临时诊断；不将 dummy 或切片 shader 带入生产渲染。

### 当前代码基线（冻结前仅用于理解，不作为最终代码验收）

- `Draw3.HiddenWindowTest.cpp:2142` 当前 WARP fixture 创建/退出逻辑。
- `Draw3.HiddenWindowTest.cpp:2280` 读回 EVENT、Flush、3 秒等待、DO_NOT_WAIT Map；不能把两次有界 query 推导为所有 API 的严格时限。
- `Draw3.HiddenWindowTest.cpp:2305` 全纹理 alpha/RGB/字段错误并集；新增路由颜色须增加合适的独立 expectation。
- `Draw3.HiddenWindowTest.cpp:2405` 正式 Cursor draw 与 query/readback、恢复 renderer 字段；矩阵须额外恢复实际 PS 资源槽，不能只恢复持有字段。
- `native-desktop/input-and-ink.md:483` 独立自检/源像素/光学等级与退出合同。
- `shaders/cpu-gpu-contracts.md` b1=112、b2/b3=48 字节，t10..13 Texture2DArray，输入/混合/寄存器契约。

### Files found

- `research/comprehensive-plan-20261006.md`：新实现所有权、69 项矩阵和单 CMD 验收合同。
- `research/check-capture-224620-20261006.md`：上一轮独立故障边界与根因限定。
- `win7-comprehensive-20261006/preservation-before.json`：主会话 24 项存在资料基线，最终须复核。
- `win7-comprehensive-20261006/missing-before.json`：本轮开始即缺失的旧资料记录。
- `win7-comprehensive-20261006/old-six-arrays.json`：旧六份诊断 DXBC identity 基线。

### 冻结前实现预审（不替代最终 hash/运行验证）

- 当前 `gpu-fxc/FullEarlyConstant.asm` 实际为 cb0 shape=4 与 alpha>0 条件后输出 .5 并 return，之后才进行 v4.z 的 add/ftoi；其余完整执行块/声明仍存在。Compact integer/float 真实 gate 与错误有色输出可由 Constant expectation 的白色中心/alpha 条件区分。
- 当前 generator 切割 main 锚定的顶层原块、保留 Cursor 和源顺序；Laser7 不依赖默认墨迹 d 变量，移除 DefaultInk 后保留 Laser7 不产生此类源码依赖缺口。真实编译后 retained resource 差异仍须按各变体的 asm/evidence 解释。
- 当前 `MatrixBindingSnapshot` 使用 PSGet 持有 b1..3、s0、t1..14 并原样恢复。matrix mask=0 使用保存态，四组 16 mask 每组紧跟原状态原 PS 重测，有限 Laser 数据仅替换 mask15 下 b1。所有 dummy 为 immutable 零初值，ArraySize=2 的 Array SRV 与 2D 独立创建；实际 PSGet 检查资源身份/维度/字节/目标别名。
- `matrixProbe` 新增显式关闭 drawProbe 的生产计数、独立像素与绑定结果。矩阵创建或绑定失败保留 ERROR 与单项失败，继续其它组；最终旧 production4 仍独立决定原 CLI 退出码，不把 matrix PASS/FAIL 改作生产结果。
- 当前初始 Hardware 创建仅 DXGI_ERROR_UNSUPPORTED/NOT_CURRENTLY_AVAILABLE 返回3，其他初始错误与后续失败返回1；两公开入口共享 fixture 并传不同 driver，无软件 fallback。argv 识别在产品配置/窗口初始化之前，以 count==2 门禁早退。
- 上述为作者编辑期间的只读观察，目前无强反证；collector 尚未冻结，旧脚本当前文本不作为新合同已实现的证据。

### 已发给主会话的冻结前采集合同问题

以下两项均已修正并以最终新 mock 验证关闭；保留此节作为发现与解决记录。

当前新版 Collect 末尾 probeFailure 只取 exit/status，读取 raw 仅把 schema/driver-line 附加到 identity，并用 WARP schema 决定是否发硬件 CLI。若 WARP header 正确、Hardware exit0 而缺失/错误其 schema 或 driver-line，直接 ProbeOnly 仍返回0。包装器会将该状态标为 incomplete，但规范要求直接 ProbeOnly 身份不符也返回1。已要求实现者将每个 driver 的当前 schema/实际 driver-line 验证结果纳入最终失败，保留真实 exit/status，并补 mock；最终冻结再复核是否修正。

包装器 manifest 当前已核 EXE SHA/schema/packageid，但尚未核其 87/69 期望数字。已提示主会话按最终 manifest 契约核对数字；该建议不能替代具体冻结代码和包证据。

### GPU 冻结与独立字节复核

已读 `comprehensive-gpu-implementation-20261006.md` 与 `gpu-fxc/static-verification.json`，独立重新计算五个源文件 SHA，全部匹配作者冻结（Hidden CPP `97B545...A1056`、H `730D18...EFE8`、header `A0473D...961A8`、generator `AFBC11...E9ACD`、HLSL `906B04...D6726`）。冻结版本补记 actual_adapter Description/VendorId/DeviceId，各驱动创建仍与普通应用后续选择分离。

独立直接从当前 header 解析 23 个 unsigned char 数组并算其 bytes/hash，23/23 等于对应 FXC CSO 和证据 SHA；旧六数组与上一轮独立 FXC CSO 6/6 逐字节身份相同。没有调用 generator、没有重写 header/CSO 或重复编译。

进一步独立解析 23 CSO 的 DXBC chunk table，23/23 OSGN payload SHA 等于正式 production PS；23 份实际 ASM 都为 ps_5_0。FullRecompiled ISGN/OSGN/SHEX/STAT 四块 SHA 各等于当前正式 production PS。该确认的是输出/执行字节合同，并不声称旧 Win7 runtime 运行正常。作者生成证据另记录 actual resource 声明/保留群；family 输出不等同实际进入 family。

已重读主会话更新的 input-and-ink 合同：87/69、两个 probe 加两普通应用、600 秒、schema/实际 driver/manifest 身份、生产退出与 matrix 摘要分离、Win7 PS2/光学实测边界均已明确。

### 真实本机验证材料与采集修正复核

主会话完整 solution ARM64 Debug build exit0/52.94 秒、x64 Release exit0/161.59 秒。已读两架构 tests/results.json，WARP/Hardware/headless 均 exit0；headless 分别142.324/15.724秒。独立从四份原始 stderr 取终态，不依赖进程 exit 推断矩阵：四次各87个唯一 case、FAIL/ERROR=0、binding FAIL=0，旧 production4/diagnostic7/sequence5、新matrix69各PASS，deviceRemoved0、visible0。实际 adapter 在 ARM64 WARP/Hardware 分别 Basic Render Driver/Adreno；Win11本机通过不代替现场Win7。

已读 actual-probe-result.json：最终x64 EXE在真实WinPS5.1的空格路径ProbeOnly双后端各87PASS，退出0，身份状态matched且完整双流；不启动普通GUI。首次与argv并行时被现有Inkeys进程拒绝的错误资料仍保留，后续顺序final复测才是上述PASS证据。argv-result.json六情景quotes、大小写/尾随空白、额外参数分别0/0/2均符合合同。

binary-and-preservation.json主会话取实际两EXE SHADER301–304并与正式CSO匹配，状态PASS。研究者进一步独立重新hash24项存在资料，24/24匹配本轮before（含正式源/8CSO/现场六文件/旧包），x64 EXE SHA确为`5D5254AAFE523640EC4F47E4AF132E25D43060B99FF426CD34666821672E672C`；旧224927六文件在开始就缺失，未计入保持通过。

上述两采集合同问题的当前源码修正已复读确认：Collect为各驱动算schema+真实driver-line的identity-status，将两identityValid纳入ProbeOnly失败，WARP身份不符禁止硬件未知CLI；wrapper manifest严格核当前package_id、EXE SHA/schema及87/69字段。尚等作者final-check-2新mock完成和文件冻结，不能用修正前results.json充当新增负例证明。

### 最终采集冻结与结论

已读作者最终 `comprehensive-collector-implementation-20261006.md`，独立重算当前四文件 SHA 均匹配：Collect `BA82C159D110C80E9891A8A73D1693B0799194F09D071E064FED21167681DB34`；wrapper `28D8A09091FECABDC277EF5FDB86378A9B9E108AD93E688E10C9BCCC125CC0A1`；CMD `F555222FCED3A2D8A9F18B3272FB5082694334739653BCBD9F6043EE5CE53C9D`；IdtMain `6DD8358EB8A13C8436028636BE459FE40EB06B9ED58CBD8CDD110CF404056052`。

`collector-tests/final-check-2/results.json` 的30项全部PASS，明确含hardware缺schema/错误driver、WARP缺driver的wrapper及直接ProbeOnly负例，87/69和包名错误manifest的拒绝。除此之外三阶段四调用、失败仍继续、hardware3与失败分离、空格路径、大双流、部分输出、缺失资料、缺EXE、env finally恢复、三个实际CMD0/1传播、直接旧选项和PS2子集静态检查均在最终集记录。

额外 `collector-tests/timeout-result.json`（不在final-check-2子目录）确认实际owned child超时注入：WaitForExit参数600000，立即模拟到期，真实精确子进程Kill/等待，后续Hardware继续，最终exit1而记录PASS。没有本机真实等待600秒；API/调度上界与现场PS2仍保留。

冻结代码直接使用Handle→WaitForExit→ExitCode，无Refresh；真实退出保持raw，不用身份状态覆盖退出；wrapper核identity及双流/三阶段是否齐，summary的captured/completed不是像素或视觉PASS。env严格设置/清除并finally恢复、不修改产品持久配置，正常两轮仍可选实际GPU。一CMD源保持，最终ZIP/manifest内容与hash由主会话独立包装验证，不在本研究已完成证据范围冒充已出包。

设计、GPU冻结字节和实际case、采集修正与负例已形成闭环，没有尚未解决的阻断发现。本研究最终结论为上述限定范围PASS；正式质量门禁可复用此报告，Win7两设备原始87矩阵、PS2实采与同候选普通/full光学观察仍必须等待用户回传，任务保持in_progress。

## Caveats / Not Found

- 顶部FINAL LIMITED PASS包含已冻结实现/生成字节/本机构建与mock；最终候选ZIP/manifest另由主会话验证，本报告不预先验收未创建的包。
- 旧故障边界在完整 PS Draw/readback、ULW 之前；新增矩阵不能预先保证一次得到唯一 opcode/JIT 根因或产品修复。
- 本轮不 GUI/build/spawn/Git，不改产品/规范/采集文件；仅本报告写入。外部技术事实沿用已核的上轮官方依据，本轮没有新外部推论。
