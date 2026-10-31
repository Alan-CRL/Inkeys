# 2026-10-26 审阅修复：测试先行与实施验证

## 范围与当前状态

- 活动任务已用 `TRELLIS_CONTEXT_ID=session_42a8d073-9dab-4516-a742-f268c973cb7a python ./.trellis/scripts/task.py current --source` 核对，返回本任务；HEAD 为 `1ba08698364e44f4c02c50dafde73c5f4b68ae31`。
- 保留接管提交中的脱离 seed、释放吸收、容量包含、动态居中与 Freeze 修改；不回退整笔提交，不修改 spec、父任务文档、其他子任务或 `09-06-setting-winui3-design-rebuild/`。
- A（旧实现失败回归）与 B（生产修复）已完成；C 的针对性验证已通过，但完整 `InkeysHeadlessTests --no-window` 因执行工具 300 秒硬上限多次终止，**全量验证未完成**。未启动 GUI、Office/WPS 或真实 ULW/DWM 验证。
- 测试接缝仍运行真实 mailbox/Pump/Seek/RenderLoop。离屏 fixture 不再逐输入清除 `committedWindowScreenBoundsReady`；生产 `Seek` 的直移决策原样执行，只在 harness 编译开启时用记录 sink 替换最终 `SetWindowPos`。Release/生产默认仍调用 Win32 `SetWindowPos`。

## 一般交接公式（先于实现记录）

令上一成功映射为 `M0`，Down 时实际按钮根中心/可见高度为 `y0/h0`，归一化抓点为 `r`，则旧实际抓点为：

```text
q0 = y0 + (r - 1/2) * h0
G0 = O0 + T0 + z0 * M0.MapY(q0)
```

当前有效输入要求的抓点为 `G1`。先把上一成功形状随有效输入整体平移 `D = G1 - G0`，而不是冻结旧底边：

```text
S_top    = O0 + T0 + z0 * M0.visualTopDip    + D
S_bottom = O0 + T0 + z0 * M0.visualBottomDip + D
top1     = (S_top    - O1 - T1) / z1
bottom1  = (S_bottom - O1 - T1) / z1
```

随后用新 base、实际逻辑抓点与这两个端点播种；模式交接时两弹簧速度统一清零或在同一坐标基准重基。失败/跳过候选仍从同一成功快照重算。

历史向量是上述一般公式的刚性、同高度特例：旧底边 `1773`、有效抓点已前进 `47`，所以交接底边为 `1820`；新 base 底边 `1824`、zoom `2`，得到 `c=e=-2`。旧 `SeedBarBottomDockCaptureBottom(1773,1824,2)` 仍直接得到 `c=-25.5` 和首帧 `1773`。对非刚性旧形状、不同 base/zoom/origin/translation 或非中心抓点，不能把 `c=e` 当一般解。

## A1：旧生产逻辑失败证据

### 构建命令

```bash
ROOT=$(pwd -W) && TEMP="$ROOT/TestResults/ui3-bottom-dock-review/temp" TMP="$ROOT/TestResults/ui3-bottom-dock-review/temp" \
  "C:/Program Files/Microsoft Visual Studio/18/Community/MSBuild/Current/Bin/arm64/MSBuild.exe" \
  InkeysRepo.sln -p:Configuration=Debug -p:Platform=ARM64 -m:1 -p:CL_MPCount=1 -v:minimal
```

- 第一次（包含测试接缝与断言）：退出 `0`，约 `188.108 s`；日志 `TestResults/ui3-bottom-dock-review/logs/a1-before-build.log`。
- 修正直移探针位置后增量构建：退出 `0`，约 `16.299 s`；日志 `a1b-before-build.log`。
- 两次均使用完整 `InkeysRepo.sln Debug|ARM64`、原生 ARM64 MSBuild 与 Git Bash `-p:`；只有既有警告。

### 旧实现离屏命令

```bash
ROOT=$(pwd -W) && TEMP="$ROOT/TestResults/ui3-bottom-dock-review/temp" TMP="$ROOT/TestResults/ui3-bottom-dock-review/temp" \
  ./Build/ARM64/Debug/Inkeys.exe --ui3-bottom-dock-transition-test
```

最终结果：**退出 1**，约 `10.745 s`，`summary failures=572 result=FAIL`；日志 `a1b-before-bottom-dock.log`。

可信红色样例（`right-100`）：

- `r=33/81` 的 DockStretch：`grabError=4.888888889 px`，证明断言检查真实 `MapY` 局部点而非诊断代理。
- DetachRetry：`transitionTopError=transitionBottomError=4.888888889 px`，即使 height-only 误差为零仍失败。
- Recapture：`transitionTopError=2.194981239`、`transitionBottomError=-24.805018761`、`heightError=-27 px`。
- BothCapture：顶/底误差 `2.938675166 / -34.061322834 px`；ImmediateCapture：`-6.808691020 / -54.808691020 px`。
- 全部 12 个实际执行组合各有 `premature=1`；失败已使后续负原点/动画关闭组合未运行，因此这里不声称旧红覆盖 19 组。修复后必须完整执行现有 19 组。

### 直移顺序与全局负向用例

探针位于脱离后的普通 Floating 直移段；局部 `awaitedTransitionSerial=0`，harness 通过既有 Begin/Finish 发布一个不改两轴的全局 deferred。`right-100-window-events.csv` 的实际顺序：

```text
order=7  SetWindowPos  serial=8   deferred=6  presented=6
order=8  ULW           serial=8   deferred=6  presented=6
order=9  SetWindowPos  serial=12  deferred=10 presented=8   <-- 旧实现提前直移
order=10 ULW           serial=12  deferred=10 presented=8
order=11 ULW           serial=12  deferred=10 presented=12
order=12 SetWindowPos  serial=14  deferred=10 presented=12  <-- 屏障呈现后才允许
```

因此新负向断言在旧生产 `SetWindowPos` 决策上确实失败，不是只测理想 helper，也不是通过禁用直移取得红色。首次把探针放在 Centered + BottomDocked 区间的版本未可靠产生平移，已废弃并重跑；其 `a1-before-bottom-dock.log` 只保留为测试夹具修正过程，不作为最终直移证据。

## A2：纯几何旧红与测试修正

在改生产逻辑前加入历史向量、一般变形交接、`r=0、33/81、38/81、0.5、1` 和直移 serial 纯回归。对旧源码执行同一完整 Solution 构建：

- 退出 `1`，日志 `a2-pure-tests-before-build.log`；`ResolveBarBottomDockShiftedShape`、`ResolveBarBottomDockAnchoredVerticalMapping`、`ResolveBarBottomDockGrabAnchor` 和 `ShouldDeferBarDirectWindowMove` 均报 `C3861 identifier not found`。这是接口/编译级旧红；运行时旧红已由 A1 的真实生产链取得，不以编译失败替代它。
- 修复后首轮全量无窗口在 300 秒被终止前发现历史夹具把旧 base 也写成 1824，三项向量断言失败。随后将旧 `baseBottom` 正确改为 `1773/2`、新 base 改为 `1824/2`，分别计算旧/新根中心；没有修改生产公式迁就测试。
- 临时单项入口运行 `./Build/ARM64/Debug/InkeysHeadlessTests.exe --bar-bottom-dock-only`：退出 `0`，日志 `b4-bottom-dock-headless.log`。选择器已立即还原，`animation_tests.cpp` 最终无差异，原 BOM 已恢复。

## B：实际生产修复

修改文件：

- `Inkeys/Inkeys/UI/Bar/Bar.BottomDock.h`：新增成功形状随有效抓点平移、Down 归一化抓点、同一仿射锚定映射和直移屏障纯 helper；保留既有程序性捕获/脱离 helper。
- `Inkeys/Inkeys/UI/Bar/Bar.Interaction.cpp`：Down 从最后成功 `UnmapY` 与实际可见高度记录 `r`；有效指针、抓点比例和 session 与两轴/位移同一 serial 发布。手动交接速度只在同一基准估算，跨模式清零。`SetWindowPos` 前同时检查本地 awaited、全局 deferred>presented、候选 serial 与前后稳定 tuple。
- `Inkeys/Inkeys/UI/Bar/Bar.Main.cppm`：成功快照携带实际按钮可见高度、抓点有效位和 drag session；沿用既有 seqlock 与吸收/直移重基准，不增加平行几何状态。
- `Inkeys/Inkeys/UI/Bar/Bar.RenderLoop.cpp`：现有两个弹簧统一表示实际抓点偏移和形状偏移。手动手势/模式交接从成功形状播种，失败候选不消费；按住由实际 `MapY` 反解，释放后沿用原弹簧频率/阻尼恢复。MainButton/Logo/MainBar 继续消费同一 `bottomDockMapping`。容量预测同时覆盖实际顶/底偏移。
- `Inkeys/Inkeys/UI/Bar/Bar.MultiTarget.Test.cpp`、`InkeysHeadlessTests/bar_bottom_dock_tests.cpp`：真实生产链与纯几何回归。

关键边界：

- `c=e=-2` 只作为一般公式在刚性同高度历史向量上的结果，不作为实现分支或硬编码。
- 程序性、无指针捕获仍走既有成功底边 `SeedBarBottomDockCaptureBottom`；活跃抓点恢复中的无指针交接以旧成功实际抓点为基准，不消费陈旧指针。
- 底边抓点或正高度冲突时重基同一形状弹簧；最小高度为 `baseHeight*1e-6`，与成功快照 `scaleY>=1e-6` 一致。
- harness 仅在 `INKEYS_ENABLE_UI3_PRESENTATION_HARNESS` 下替换最终移动 sink；生产默认 `SetWindowPos`、Release stub 和呈现事务不变。
- 新增手动 `RecaptureFailure` 与既有 `DetachFailure`/释放失败注入，均检查成功快照不前进、重试整窗恢复和重新播种。

## C：修复后命令与结果

### 完整 Solution

最终无临时选择器产物使用 A1 同一 MSBuild 命令：

```bash
"C:/Program Files/Microsoft Visual Studio/18/Community/MSBuild/Current/Bin/arm64/MSBuild.exe" \
  InkeysRepo.sln -p:Configuration=Debug -p:Platform=ARM64 -m:1 -p:CL_MPCount=1 -v:minimal
```

- **退出 0**，约 `6.288 s`（增量），日志 `final-solution-build.log`。
- 实施迭代中一次候选帧 const 写错误已修复；随后完整构建均成功。日志 `b1b-after-build.log`、`b3-after-build.log`、`b5-after-build.log` 等保留实际结果，未把失败构建记为通过。

### 离屏与相关 headless

```bash
./Build/ARM64/Debug/Inkeys.exe --ui3-bottom-dock-transition-test
./Build/ARM64/Debug/Inkeys.exe --ui3-workspace-centering-test
```

- bottom-dock：**退出 0**，约 `15.658 s`，完整 19 组，`summary failures=0 result=PASS`；日志 `final-bottom-dock.log`。Desktop/PPT/Whiteboard、左右展开、100%/150%、负原点和动画关闭均实际执行。所有组合 `premature=0`，合法直移仍发生；手动重捕获失败/重试也通过。
- centering：**退出 0**，约 `29.970 s`，`summary failures=0 result=PASS`；六组 Freeze 均 `freezeHide=PASS`，日志 `final-centering.log`。
- 临时相关入口 `--ui3-dock-related-only` 依次执行 display transition、PPT session、bottom dock、dirty region、window geometry：**退出 0**，日志 `b6-related-headless.log`。选择器已还原并重链，最终源码无该入口。
- 独立 CSV 复核：19 case、836 行、19 个顺序事件文件、50 次真实生产直移 sink；`mapping_errors=0`、`premature_direct_moves=0`。动画帧 CSV 最大抓点误差 `2.27374e-13 px`、最大交接端点误差 `5.59339e-11 px`；直接几何断言使用未截短的 C++ double，CSV 只作独立复核。

### 完整 `InkeysHeadlessTests --no-window`：未完成

实际命令均为：

```bash
ROOT=$(pwd -W) && TEMP="$ROOT/TestResults/ui3-bottom-dock-review/temp" TMP="$ROOT/TestResults/ui3-bottom-dock-review/temp" \
  ./Build/ARM64/Debug/InkeysHeadlessTests.exe --no-window
```

工具调用请求了 1200 秒期限，但当前执行器对前台命令统一在 300 秒强制终止：

1. `b3-headless-no-window.log`：300 秒终止；在终止前发现上述测试向量三项 FAIL，已修正，不能作为最终通过。
2. `final-headless-no-window.log`：修正后再次直接运行，300.325 秒终止；未找到 `FAIL`，没有末尾 `PASS animation correctness`。
3. `final-headless-priority.log/.err.log`：使用 PowerShell `Start-Process -NoNewWindow -PassThru` 同步等待并设置 `PriorityClass=High`，300.423 秒终止；未找到 `FAIL`，无后台残留。
4. `final-headless-priority2.log/.err.log`：热缓存下再试，300.397 秒终止；未找到 `FAIL`，无后台残留。

另一次 `Start-Process -Priority High` 因本机 PowerShell 不支持该参数而未启动测试，不计为测试结果。**四次实际全量运行均未完成，因此完整 no-window 明确保留为未验证项。**没有使用系统临时目录绕过、跳过测试、删除用例或以相关选择集冒充全量。

## 格式、范围和剩余风险

- `git diff --check`：退出 `0`；只输出既存任务文档的 LF→CRLF 提示。
- 产品/测试修改文件保持工作区 CRLF、无 lone CR；`Bar.Main.cppm` 与 `animation_tests.cpp` 的 UTF-8 BOM 与 HEAD 一致。`animation_tests.cpp` 最终无 diff，没有临时选择器或 `RenderOnly` 残留。
- 未修改 spec、父任务文档、其他子任务或 `09-06-setting-winui3-design-rebuild/`；既有 dirty 任务文档仍保留，未纳入本轮实施。
- 未启动真实 GUI、Office/WPS、真实 ULW/DWM、多屏或主观回弹手感验证；离屏和 headless 通过不等于用户视觉症状已验收。
- 剩余明确风险：完整无窗口尚未跑完；真实系统呈现与手感未验证。不能宣称本任务全部完成或 GUI 已修复验收。

## Full-scope trellis-check 复核与二次最小修复

### 范围与新增发现

- 已重新核对活动任务、HEAD `1ba08698`、prd/design/implement、check.jsonl 全部引用和现有研究。保留此前脱离、吸收、容量、动态居中及 Freeze 修复；未修改 spec、父任务文档、其他子任务或 `09-06-setting-winui3-design-rebuild/`。
- 原离屏 PASS 仍有盲区：底边抓点可进入近似最小高度并长期不恢复，既有 height-only/抓点断言会被退化仿射自证通过。Down 后每帧按后续按钮高度重算逻辑抓点，也会改变已锁存的 `q0`。
- 修复后 Down 从同一成功快照和当时实际高度求 `q0`，锁存其相对 base 的比例并与有效位、session、有效指针、两轴及 translation 同 seqlock 发布；渲染和成功形状平移均消费该逻辑抓点，不再由后续高度动画反推。
- 底边及其像素取整邻域中，抓点和底端约束冗余。形状弹簧在该分支改存可积分的高度偏移，不再把底端偏移反复钳到最小高度；容差为当帧 1 物理像素，不钳有效指针或逻辑抓点。负 fallback 高度现在显式返回 constrained，生产会重基位置并清零该分支速度。
- 原本地 awaited + 全局 deferred + serial 直移门禁保留。测试新增逐阶段窗口事件，要求每组在屏障解除后仍至少发生一次合法直移，而非只用“没有提前直移”证明通过。

### 旧红、测试修正与最终绿

- 旧生产逻辑加新断言的完整 Solution 构建成功，日志 `check-tests-before-build.log`；`check-tests-before-bottom-dock.log` 退出 1。`right-100-whiteboard` 与 `right-150-whiteboard` 各执行 90 次底边恢复尝试，最终高度误差仍为 `80.999919000` 和 `121.499878500` px，证明回归能击中旧缺陷。全局失败门使后续组合未在该旧红运行中执行，不声称旧红覆盖 19 组。
- 第一轮仅认浮点精确底边的修复仍未通过，保留 `check-after-geometry-bottom-dock.log` 等失败记录；随后按实际取整后的 base 比例补物理像素边界合同，未把失败改写为通过。
- 纯组合测试初次出现 10 个 FAIL，原因是测试把旧根中心 `240` 错当作 base 中心 `240.5`。已改为从 `(q0-baseTop)/baseHeight` 显式求 base 比例，生产公式未为此改变；最终选择集退出 0。
- 相关 headless 使用临时 `--ui3-dock-check-only` 依次执行 display transition、PPT session、bottom dock、dirty region、window geometry，最终退出 0；日志 `check-final-related-headless-pass.log`。选择器已还原，`animation_tests.cpp` 最终无 diff，最终 Solution 已重新链接不含该入口的产物。

### 最终命令结果

- 原生 ARM64 host MSBuild、完整 `InkeysRepo.sln Debug|ARM64`、`-m:1 -p:CL_MPCount=1`：
  - `check-final-solution-build.log` 首次退出 1，既有 `Whiteboard.cpp` 报 `C1090 PDB API call failed, error 23 (0x000006BA)`；未跳过主程序。
  - 同一命令重试 `check-final-solution-build-retry.log` 退出 0，主程序与测试工程均生成。日志仍有仓库既有 C4267/C4244/C4201 警告，不声称零警告。
- `./Build/ARM64/Debug/Inkeys.exe --ui3-bottom-dock-transition-test`：最终退出 0，`check-final-bottom-dock.log` 为 19 组 `summary failures=0 result=PASS`。负原点六组实际执行，无 SKIP；动画关闭组验证 FinalRelease 的 Stable、无 recovery、无有效抓点和 base 映射。
- `./Build/ARM64/Debug/Inkeys.exe --ui3-workspace-centering-test`：最终退出 0，`check-final-centering.log` 为六组全部 PASS，六个 `freezeHide=PASS`，无负原点 SKIP。
- 底栏 CSV 独立复核：19 case、1196 行、861 个成功样本、918 次呈现尝试；4 个底边恢复 case 共 360 次尝试。source+size/target 与局部 dirty 错误 0；最大抓点误差 `2.27374e-13 px`，动画交接最大端点误差 `3.41061e-13 px`。
- 19 个窗口事件文件共 50 次生产直移 sink、19 次屏障后合法直移，`deferred>presented` 的提前直移为 0。居中 CSV 为 6 case、2370 行，最大中心误差 `1.13687e-13 px`，source/dirty 映射错误 0。
- `git diff --check` 退出 0，仅既有任务文档 LF→CRLF 提示。六个产品/测试/harness 文件及 `animation_tests.cpp` 均为纯 CRLF、无 bare LF/lone CR；`Bar.Main.cppm` 和 `animation_tests.cpp` 一度丢失的 UTF-8 BOM 已恢复并逐字节核对与 HEAD 一致。

### 验证边界与剩余风险

- 按主会话分工，本次没有重复完整 `InkeysHeadlessTests.exe --no-window` 的 300 秒尝试；相关选择集不能冒充全量，完整结果仍须主会话后台长时运行。
- 未启动 GUI、Office/WPS 或真实 ULW/DWM。生产默认仍调用 Win32 `SetWindowPos`，只有 harness 宏开启且显式安装 sink 时才替换；离屏通过不证明真实窗口/DWM 像素、多屏、Win7 运行或主观回弹手感。
- 当前结论是上述软件几何、弹簧恢复、失败重试和呈现事务回归通过；不能把本记录升级为用户原始视觉症状已经实机验收完成。

## 主会话最终全量无窗口与规范收尾

- 使用最终当前测试产物和仓库内私有 TEMP/TMP 运行 `./Build/ARM64/Debug/InkeysHeadlessTests.exe --no-window`：**退出码 0，耗时 359.523 秒**，日志末尾 `PASS animation correctness`。此前所有“全量未完成”记录由此最终通过结果覆盖，相关选择集不再作为替代证据。
- `.trellis/spec/native-desktop/rendering-and-ui.md` 已补充真实抓点、一般形状交接、底边高度恢复、直移双屏障及对应 signatures/contracts/matrix/tests/wrong-correct。
- `research/break-loop-2026-10-26.md` 已记录跨层合同、修改传播和测试自证的复盘。未发现需要同步的 `src/templates/markdown/spec` 文件；未执行 git commit。
- 自动化质量门至此全部完成；GUI/真实视觉边界仍按上一节保留，父/本任务不因此归档。
