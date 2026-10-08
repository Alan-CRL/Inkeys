# 执行记录（第一批已完成）

## 批准门

- [x] 建立任务，读取当前源码、相关 spec 与历史性能审计记录。
- [x] 将用户后续“时快时慢、关闭动态光影改善”补充纳入归因。
- [x] 完成真实动画 module、生产几何/呈现 helper、原样绘制函数计数和无 HWND D2D API 对照。
- [x] 写入 findings.md、prd.md、design.md 和本计划。
- [x] 用户明确批准最新第一批 S1–S4 范围（2026-09-22：批准并开始实现）。
- [x] 已执行 `python .trellis/scripts/task.py start .trellis/tasks/09-22-ui3-animation-stutter-investigation`，进入实施。

用户已批准第一批实施；仍不启动产品、设置、浏览器或 Computer Use，不 commit/push。新建 research 程序是调查探针，未替换任何产品实现。

## 第一批步骤

1. 重新检查 Git 状态与 HEAD；记录产品文件 BOM/编码/CRLF，读取 trellis-before-dev 及对应 native-desktop 规范。
2. S1：移除 `Bar.RenderLoop.cpp:3126-3421` 内 22 个颜色块中间描边目标，保留统一 `4196` 与批次同步。用真实 Animation module 验证关闭/展开/反向及隐藏终态，断言几何比例而非只数 SetTar 次数。
3. S2：按 `research/pagecontrol-fix-design-review.md` 修改 `Bar.Scene.cpp` 私有 damage 返回契约及 hooks 选择；不得通过 pending 矩形前后相等来判断。验证无相交零唤醒、旧光擦除、同区域强度/颜色改变、首次订阅、主光变化、其他原有请求。
4. S3：审查 Bar/PageControl 获取 DC 到释放期间无 GDI 绘制，使用空 RECT 释放。保留 COPY 和失败路径事务。
5. S4：增加集中、限频的阶段/状态聚合；日志中分开回调次数、动画推进次数、尝试次数、成功呈现次数。记录分片调用与新建遮罩，不能把 cache hit 直接当作无绘制成本。
6. 更新受影响的窄 spec 合同（仅获批实施之后），补充“同帧最终目标唯一”和“光照贡献决定唤醒”的具体回归边界。
7. 完成针对性无 GUI 测试和主 Solution 构建，审查差异、日志负担、错误处理以及剩余风险。

## 验证方式

本轮已执行内容与证据边界见 [research/validation.md](research/validation.md)。现有本机 headless 可执行文件已运行通过，但本轮未重新构建该完整可执行文件，不能用它宣称当前主程序完整编译通过。

批准后主程序构建：从 vswhere 定位 Visual Studio 安装，再定位 `MSBuild/Current/Bin/arm64/MSBuild.exe`，不要把调查时读取到的版本号写进项目配置。

```powershell
# 在同一次 PowerShell invocation 中执行；保留构建日志和退出码。
Remove-Item Env:PATH -ErrorAction SilentlyContinue
$env:MSBUILDDISABLENODEREUSE = '1'
& $ui3NativeMsbuild 'InkeysRepo.sln' /p:Configuration=Debug /p:Platform=ARM64 /m:1 /nr:false
```

允许至少 5 分钟，不以工具 10 秒返回 session ID 当超时；等待期间持续读取输出并报告真实阶段。失败先定位第一个有意义的错误，区分代码与环境，不升级依赖或关闭签名绕过。

headless 使用项目已有 `InkeysHeadlessTests` 构建目标/依赖，在当前原生配置下构建需要的测试，再运行：

```powershell
& '.\Build\ARM64\Debug\InkeysHeadlessTests.exe' --no-window
```

新集成测试若需 Window/D2D 能力，先检查现有测试的 no-window 边界；不为测试启动隐形产品窗口。可创建无 HWND 的 D2D bitmap/context 做像素对照，不能以其冒充 ULW/视觉验收。

## 后续按证据路由

- 绘制分片或 mask create 占主导 → 提出 D2 的像素等价验证及窄平移支持；复核不同 zoom 的正常量化成本。
- 失败/长退避占主导 → 按 D1 成组修正时钟和恢复，不单改通知或 clamp。
- source/viewport 超容量 → D3 优先建立最终包含保证；保留全脏/成功提交边界。
- 峰值容量与长帧相关 → 同尺寸重建/缩容重建分开评估，预热后比较，不直接归因缩容。
- Bar 各阶段正常、回调间隔异常 → 查看其他客户端耗时与 Scheduler，不先修改光影质量。

以上后续 D1–D3 不在本次第一批实施范围；按新增证据完善可审阅方案后再与用户确认范围。

## 完成时需明确报告

- 实际修复的文件与行为、测试/编译退出码、未执行的 GUI 项目及原因。
- 能证明的源级缺陷已修复，与尚未实机确认的低概率故障归因分开。
- 不自动创建 commit 或 push，不把任务标为用户原始故障已完全解决，直到相应验收证据成立。

## 2026-09-23 执行结果

S1-S4、对应窄规范更新及全范围独立审查已完成；两次完整Solution构建均退出0，最终headless --no-window退出0。新增C4127已修复，构建附带的跟踪PptCOM.dll已还原。无GUI、无Git提交。正式结果与残余现场验收项见 [verification/first-batch-result.md](verification/first-batch-result.md)。
