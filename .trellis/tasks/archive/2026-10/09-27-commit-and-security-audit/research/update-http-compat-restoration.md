# 更新链 HTTP 兼容恢复与旧 EXE 回退

日期：2026-09-28。用户最新明确决定：保留本任务 ZIP、路径、大小、写入、暂存事务修补，但恢复 HTTP/HTTPS 两种下载和 HTTPS 失败后的 HTTP 回退；本轮**不新增发布者验签/签名流程**。此授权取代 `update-threat-model.md`、`update-security-implementation.md` 中对“移除所有 HTTP 回退/必须新增签名”的实施建议；既有风险描述仍是威胁证据，不能写成风险已消失。旧程序名缺失时应优先选已存在且普通非 reparse 的 `智绘教.exe`，其次 `Inkeys.exe`；Main 接线由主 agent 独占。

## H0、先前补丁与本轮行为

- H0 `Net.Update.Download.cpp` 对固定主/备源逐个 HTTPS→HTTP，包下载也是 SSLClient 失败后 Client；`set_follow_location(true)` 无显式跳转上限，流写失败仍可继续、无大小上限。H0 `Net.Update.cpp::splitUrl` 捕获可选 scheme，却在调用下载时丢掉 prefix，下载层无论元数据 URL scheme 都先试 SSL，再试 HTTP。
- F-014 工作树先前收紧为 HTTPS-only、安全解析/最多三跳、证书/主机名校验、1 MiB 版本信息与 512 MiB 包上限、写盘完整性和只提取唯一指定 EXE；`GetEditionInfo`/`DownloadNewProgram` 会拒绝合法旧 HTTP 包 URL。它的 ZIP、固定 staging、原子 `update.json`、不递归删未知文件等修补本轮保留。
- 本轮 `UpdatePathSafety.h::ParseSafeUpdateUrl` 明确接收 HTTP/HTTPS 各自默认端口 80/443 和显式有效 1..65535 端口（例如旧 HTTP:8080、HTTPS:8443，HTTPS:80 仍按 HTTPS 处理），拒绝 0/溢出/非数字端口、userinfo、IP/异常主机、反斜杠、控制字符、无路径与超长 URL。`ResolveSafeUpdateRedirect` 在相对跳转保持原 scheme 与显式端口，允许 HTTP→HTTPS，拒绝单个 HTTPS 请求自动跟随 HTTPS→HTTP；该失败随后回到**原 host**的显式 HTTP 回退，而非把远端 `Location` 当可信下载目标。两种传输的 redirect 都保留最多 3 跳和跨 host 去 Referer。
- 固定 metadata 源顺序是主 HTTPS→同 host HTTP→备用 HTTPS→同 host HTTP；每次换传输清空旧部分 body。包 URL 无论 metadata 写 `http://` 或 `https://`，先以相同 host/path 请求 HTTPS，失败后才同 host HTTP。显式 URL 端口在两次传输和同源相对 redirect 中保持；无显式端口时 HTTPS443/HTTP80。下载保持一个本次 `CREATE_NEW` 私有文件句柄，HTTPS 部分字节在 HTTP 重试前 `SetFilePointerEx` 回卷、`SetEndOfFile` 截断并清零进度，绝不拼接两次响应；若失败源自本地 `WriteFile` 则不重试另一传输。两种客户端共用原响应长度/体积/非200正文/连接与读取超时限制；成功才 Flush/Close。`Net.Update.cpp` 对 metadata/direct call 的 URL 验证和 `splitUrl` 均接受明确两种 scheme，并把 prefix/显式端口保留给 `DownloadEdition`。
- 本轮没有改 ZIP 解析器、文件名/哈希校验、指定 EXE 提取、staging 或更新 JSON 发布逻辑；没有增加网络白名单或发布者签名。HTTP 字节在网络路径上可以被篡改，元数据内自报的 MD5/SHA-256 不能独立认证发布者，仍是用户明确接受的剩余安全风险；不能把编译/纯解析测试写成真实 CDN 或 Win7 下载安全已验。

## 可重复红测与验证状态

先只改生产 `UpdatePathSafety.h` 的真实调用方测试 `InkeysHeadlessTests/update_security_tests.cpp` 的 HTTP 期望，旧 HTTPS-only parser 对 HTTP 包、显式 `:80`、HTTP 相对跳转三项 FAIL。主 agent 串行完整 `InkeysRepo.sln Debug|ARM64` Build exit 0；`InkeysHeadlessTests --no-window` exit 1，stderr 有三条 `[UpdateSecurity]` HTTP FAIL（`TestResults/release-hardening/update-http-compat-red-build-debug-arm64.log`、`update-http-compat-red-headless.{stdout,stderr}.log`）。同次还出现三条旧 `PresentationDescriptor` ordinal-upgrade 断言 FAIL，来自并行 F-041 策略门，与网络变更无关；不能将整个 exit1 独归 HTTP。

第一版两 scheme 绿候选经完整 `InkeysRepo.sln Debug|ARM64` Build exit 0；Headless `--no-window` exit 1，但 `[UpdateSecurity]` 原三项 HTTP FAIL 全消失，新旧 EXE 纯候选测试无失败；仅三条并行 F-041 旧 ordinal 断言失败（`update-http-f041-lane-red-build-debug-arm64.log`、`update-http-compat-green-headless.*`）。随后自审发现 H0 真实 `httplib::Client(domain)` 支持 `host:8080`（本机 `httplib.h:10047-10072`），第一版 parser 只许显式默认端口会阻断合法旧 HTTP。新增四项端口红测：HTTP:8080、HTTPS:8443、HTTPS:80 scheme 语义与 HTTP:8080 相对 redirect；主 agent 为避 F-041 半成品争用仅构建 `InkeysHeadlessTests.vcxproj Debug|ARM64`，Build exit 0，测试 exit 1、四条新增 `[UpdateSecurity]` FAIL，F-041 旧断言已更新（`update-http-ports-red-headless-build-debug-arm64.log`、`update-http-ports-red-headless.*`）。该窄 Build **不算**主 Solution 验证。

端口修补后窄 Headless Build exit 0，四项新红转绿；有一项旧 `http://host:443` 应拒绝的断言与用户明确 scheme 合同冲突，首次测试 exit 1（`update-http-ports-green-headless.*`）。修正该旧期望并加失败候选不重试同名测试后，主 agent 串行完整 `InkeysRepo.sln Debug|ARM64` Build exit 0、`InkeysHeadlessTests --no-window` exit 0，stdout 末行 `PASS animation correctness`，stderr 空（`f041-controller-update-http-final-build-debug-arm64.log`、`f041-update-http-final-headless.*`）。这才是本轮最终自动绿证；真网络/Win7/三架构 Release 仍未执行。

`UpdatePathSafety.h` 与 `update_security_tests.cpp` 是本任务先前已登记的 untracked 文件，因此 `git diff --check` 只覆盖 tracked 片段，额外逐文件字节检查四者均 UTF-8 无 BOM、全 CRLF。

## 旧程序恢复候选纯合同

`UpdatePathSafety.h::SelectUpdateRollbackExecutableName(oldName, isRegularNonReparse)` 只返回安全 basename；先在 `IsSafeUpdateExecutableName` 后询问调用方 callback，安全且可用的 `old_name` 优先，然后固定 `智绘教.exe`、`Inkeys.exe`，全无返回空。恶意 `../`、绝对路径或设备名不会传给 callback。Main 在自己可信安装主目录拼完整路径，并以实际普通非 reparse 文件检查 callback；helper 不读 JSON 文件、不碰用户进程、不启动 EXE。Headless 以假 callback 检查有效旧名优先、缺失旧名时中文 legacy 优先、恶意名不查文件、reparse/不可用时 fallthrough、同名失败后排除与全无候选。主 agent 已独立把该 helper 接入 `IdtMain.cpp:1122-1146` 最多三轮启动回退；本 subagent 没修改 Main。Main 真实 ShellExecute/旧 Inkeys2 文件的隔离 no-GUI 与真用户环境仍未由本轮测试验证。

若被选的旧 EXE 经 `ShellExecuteW` **明确失败**，Main 可在最多三次循环里重用同一 helper：callback 排除本轮已尝试 basename，再核当前候选仍是安装主目录内普通非 reparse 文件；拿到候选即记入 attempted，`HINSTANCE` 数值 `>32` 表示 API 接受后立即停止、不再拉起别名，`<=32` 才试下一名。全失败不能返回“重启成功”；能辨认用户取消时应停止，不得自动尝试其他 EXE。旧名与固定候选重复也不能二次启动同一文件。此为建议给 Main owner 的调用合同，本 subagent 不改 `IdtMain.cpp`，也不以 ShellExecute 接受推断新进程真正稳定启动。

## 仍需的真实验收

没有连接真实主/备用 CDN、没有本地 HTTP/TLS 夹具验证连接失败、证书错误、301/302/307、Content-Length 截断与本地写失败后的不回退；也没有 Win7 SP1+仅 KB2670838、Win32/x64/ARM64 三架构实际运行证据。Win7 的 HTTPS 证书/TLS、HTTP 回退与重定向要分别记录，不能由 ARM64 Win11 编译推断。HTTP 回退是明确功能选择，安全报告仍须注明可被网络路径替换元数据与包的前提及同源哈希局限；不把它重新标成“已修复高危来源认证”。
