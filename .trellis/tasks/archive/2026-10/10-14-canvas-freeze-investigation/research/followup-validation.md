# 后续修复验证（2026-10-17）

## 授权和边界

修复 PPT 翻页/同页暂停恢复、原窗口断言失败，并审计输入准入。本轮禁止桌面控制、应用主窗口及任何测试 HWND；未运行默认窗口套件或 Office。原 F1–F4 已提交 68cd297da63642d58fd755da20ec4c8bac42bd1a；本轮不 commit/push。

## 实现和独立审查

- PPT raw/descriptor：生产共享 PptTargetCache 保留未应用状态，raw 追上后不依赖新 revision；UI 使用已接受 target 页码。
- 同页暂停：Bridge 按当前完整身份标记刷新，恢复有效发布获得新 revision，重新经过文档呈现与 UI 回执；没有强制开门。
- Setting owner：根刷新后在 Setting owner 线程明确执行带内排序，保留实际最终核验/失败回滚。八项相关断言待窗口运行；三项根排序断言未定位，不宣称 11 项全部修好。
- PPT worker：命名索引锁每次最多等 1000ms，失败沿既有 IoError completion，不操作未取得锁的数据；不改变失败预算和安全保护。
- 独立 Trellis 检查未发现产品源码阻塞项，补足 Bridge 无效发布/Clear/Reset/换会话回归；存储测试空容器防护已补。详见 check-window-storage-followup.md、ppt-turn-fix.md。

## 红测及环境区分

`followup-storage-red.log`：受限构建首先在 ink_prediction.cppm 报 C1902 PDB manager mismatch。MSBuild 实际选择既有 v143 14.44.35207，未改工程/SDK/依赖。相同命令正常宿主构建 `followup-storage-red-host.log` exit0，表明受限构建环境影响；保持原调试信息及独立 PDB endpoint。

正常宿主运行旧无限锁实现＋新增真实worker测试：`Build/CanvasFreezeInvestigation/followup-20261017/storage-red-host.json`，exit1，约8.547秒，SHA256 6b7e176d842fd0a47a0dd137ae4f50ff178568e7525a988b817ced7e3c784485。仅新增用例的 `completions == 2` 和释放后旧内容保留断言失败；持锁5秒不返回，释放后原等待请求继续，证明该触发存在。测试先释放锁再drain，未强杀。

受限环境运行同一EXE的 storage-red 日志出现大量既有正常保存失败；独立保留为环境限制，不作为新缺陷证据。所有运行使用 CREATE_NO_WINDOW，存储套件子命令亦使用 CREATE_NO_WINDOW，不创建 HWND。

## 最终构建与绿测

- 完整 `InkeysRepo.sln Debug|ARM64`：exit0，日志 `Build/CanvasFreezeInvestigation/fixes/followup-solution.log`，ARM64 原生 MSBuild，约21分6秒；保留既有同版本完整依赖 provider，没有更改工程配置或 SDK。
- 存储目标 `inkStrokeModelerTest.sln /t:inkStrokeModelerTestTests Debug|ARM64`：exit0，日志 `followup-storage-green.log`。post-build 出现既有 `pwsh.exe not recognized` 文本，MSBuild 仍exit0且产物/随后测试成功，未改源码绕过。
- `InkeysHeadlessTests --no-window`：exit0，93.313秒，末尾 PASS animation correctness，stderr为空。覆盖本次生产 cache/resolver/Bridge/回执谓词，包括真实Bridge Clear/Reset/新会话及无效发布保留刷新；未运行任何窗口断言。
- 新主程序三个显式无窗口 CLI 均exit0：parked-ppt-retained、pending-topology-load、ppt-current-load-retry；stderr分别有 PASS，最后包含真实 worker sharing failure/retry/strict install 与 bounded same-target Current recovery 两条PASS。
- 最终存储 `--presentation-autosave-only`：exit0，3.859秒，stderr为空。新增持锁 Save→Load 均返回IoError，释放锁后同一个运行中的worker读回revision1原非空墨迹；没有Stop/Start恢复捷径。红测的两条失败均变绿，既有存储用例通过。
- git diff --check通过；源文件原UTF-8 BOM/CRLF保持。新生成的4个未跟踪shader输出已移入Build证据目录，未混入源码变更。

最终二进制SHA256：

| 产物 | SHA256 |
| --- | --- |
| Inkeys.exe | 033f683e4e57ea7375c3f932e04a3a970306f550f99f815d8ab2fdf0dfe6dd53 |
| InkeysHeadlessTests.exe | 65cd6789c86d1b0e3644c36ed8ea6765ac21cadd96907a5f286f6b822a4e39f4 |
| inkStrokeModelerTestTests.exe | 20a7207f4d46a855662c69953f908b6b18aa539dabf2ff05f73ce305a2f5c734 |

命令、退出码、耗时、hash保存在 `Build/CanvasFreezeInvestigation/followup-20261017/*results.json`、`headless.json`、`storage-green.json`；源码快照见 `source-manifest.json`。产品GUI、默认HWND、真实Office没有运行。

## 剩余边界

- 原三项根窗口连续性/样式断言缺少故障快照；只能保留新增采样，禁止猜测性改代码或放宽断言。
- 所有窗口相关行为、完整 Office/WPS、真实笔/触摸与GPU提交未在本轮运行。
- 准入审计覆盖13类门禁。OOM completion分配/传递失败、绘制线程异常退出有明确源码路径但未完成故障注入；底层文件I/O及独立desktop锁等待也未被本次PPT锁预算覆盖。
- 未读槽/身份冲突/损坏文件/有限重试耗尽仍拒绝输入，这是防止丢失墨迹的安全状态，不应强制变为空白可写。用户现场具体根因仍需同场景证据，不能声称所有报告均已解决。
