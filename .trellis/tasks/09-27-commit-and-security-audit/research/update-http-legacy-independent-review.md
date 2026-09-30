# 更新 HTTP/旧 EXE 兼容最终独立复审

日期：2026-09-28。角色：独立只读 reviewer，未参与更新代码实施；只写本报告，未构建、GUI、真实 CDN/ZIP/Win7 或对外服务测试。审查 `UpdatePathSafety.h`、`Net.Update.Download.cpp`、`Net.Update.cpp`、`IdtMain.cpp` 更新消费段、`update_security_tests.cpp`、H0 同段历史、Trellis 任务与当前日志。用户最新决定优先：保留 HTTP/HTTPS 与 HTTPS 失败的 HTTP 退路；不在本任务新增发布者签名；`old_name` 缺失优先旧 `智绘教.exe`，安全旧名存在时优先它。

复审结论：**当前代码按纯 helper/静态路径满足上述兼容顺序**，且本机 Debug 完整 Solution 与严格 Headless 已通过；真实 HTTP/TLS/ZIP/旧 EXE 启动/Win7 尚未运行。F-053 本地缓存 JSON reader 已有隔离红→绿，`IdtMain` 主启动的同类防护为静态修补，不能从 reader CLI 推断真实启动链 PASS。HTTP 元数据和包无独立发布者认证是用户已明确接受、仍需公开呈现的高影响风险。

## 对用户兼容要求的实际调用链

- URL helper `ParseSafeUpdateUrl` 现在只接 `http://`/`https://`、规范 host 与非零 `1..65535` 显式端口；默认分别 80/443，拒绝 userinfo、反斜杠、控制字符和 fragment。相对跳转保留原 scheme/显式端口；HTTP 可跳 HTTPS，单次 HTTPS 跳转不能悄悄降到 HTTP（`UpdatePathSafety.h:24-140`）。`FetchUpdate` 按当前 target.scheme 构造 `httplib::SSLClient` 或 `httplib::Client`，SSL 分支打开证书与主机名校验、禁库自动跳转，每次最多 3 次显式 redirect、200 正文边界受限，跨 host 删除 Referer（`Net.Update.Download.cpp:56-128`）。
- 元数据源顺序为固定主站 HTTPS→同 host HTTP，失败后固定备站 HTTPS→同 host HTTP（`:139-169`）。包 URL 先试 HTTPS 的原 host/path：若原 URL 带显式端口，HTTPS 保留它；否则用 443。网络/TLS/响应失败且**没有本地写失败**时，在同一个 CREATE_NEW stage 文件 HANDLE 上 `SetFilePointerEx(0)` + `SetEndOfFile`，清零 `downloadedSize`，再按原显式端口或默认 80 尝试 HTTP（`:170-227`）。WriteFile 短写/失败置 `localWriteFailed`，不会伪装成 TLS 失败触发 HTTP；截断/seek 失败不退路且删本次文件。成功必须 Fetch 200/非空/长度一致、FlushFileBuffers 和 CloseHandle 均成功。
- 远端 `representation` 仅作单文件 `.exe` basename，哈希语法/非零文件大小/URL 在创建 staging 前检查。ZIP 预扫描最多 128 entry、各项最多 512 MiB、累计声明展开最多 1 GiB，唯一精确匹配 EXE 才解到有上限的内存，落盘固定 `payload.exe`，不把 ZIP entry name 当路径；ZIP Open/Get/Unzip/Close、写入、MD5/SHA-256 失败只清本次已知 stage 路径（`Net.Update.cpp:251-498`）。新 EXE 先搬到应用生成的 finalName，`update.json` 暂存、flush 后最后原子发布；失败不会递归删未知 installer 目录。这里的哈希只证明与同一份元数据相等，**不认证发布者**。
- 启动读取旧 `update.json` 时，`old_name` 是可选字段；非空只接受 `IsSafeUpdateExecutableName` 的安全 basename，失败恢复 `SelectUpdateRollbackExecutableName` 实际在主安装目录用 `GetFileAttributesW` 排除目录/reparse，按安全 old_name→`智绘教.exe`→`Inkeys.exe` 取第一个存在者，再调用一次 `ShellExecuteW`（`IdtMain.cpp:925-1135`、`UpdatePathSafety.h:194-213`）。这保留早期 Inkeys2 缺字段的中文 EXE 优先路径；纯 helper 测试记录了探测顺序，但**没有执行真实 ShellExecute 或旧 Inkeys2 二进制**。

## 已执行证据与边界

HTTP helper 先有三项兼容红灯（HTTP 包 URL、显式 HTTP 默认端口、HTTP 相对跳转），`update-http-compat-red-headless.stderr.log`；恢复 HTTP 后这些项不再失败，但当轮整套 Headless 仍因 F-041 旧 ordinal 三断言 exit1，不得记作全套 PASS。自定义端口又先有四项红灯（HTTP:8080、HTTPS:8443、HTTPS:80 与 HTTP 相对跳转保端口），`update-http-ports-red-headless.stderr.log`；第一版绿测仍因旧的“HTTP:443 必拒”断言 exit1，该断言与新合法显式端口合同冲突。最后一次主 agent 报告完整 `InkeysRepo.sln Debug|ARM64` Build exit0，`InkeysHeadlessTests --no-window` exit0，日志 `f041-controller-update-http-final-build-debug-arm64.log`、`f041-update-http-final-headless.{stdout,stderr}.log`；当前 `update_security_tests.cpp:39-92` 同时覆盖默认/显式端口、HTTP/HTTPS/相对跳转/禁止单跳 HTTPS→HTTP、路径/哈希语法，`:97-125` 覆盖旧名候选次序。最终日志 stderr 空、stdout 有 `PASS animation correctness`；该套测试只是 helper/其它 headless 逻辑，不触发网络、ZIP、安装器 `ShellExecute` 或 Win7。

源文件的 `git diff --check` 当前 exit0；`Net.Update.Download.cpp`、`Net.Update.cpp` 原 UTF-8 无 BOM+CRLF，`IdtMain.cpp` UTF-8 BOM+CRLF；新头/测试为 UTF-8 无 BOM+CRLF。修改路径使用 `CreateFileW`、`SetFilePointerEx`、`SetEndOfFile`、`GetFileAttributesW`、`MoveFileExW`，微软官方最低客户端为 XP 或更早，**这些 API 本身**不要求 Win7 SP1 在 KB2670838 之外的补丁（[SetFilePointerEx](https://learn.microsoft.com/en-us/windows/win32/api/fileapi/nf-fileapi-setfilepointerex)、[MoveFileExW](https://learn.microsoft.com/en-us/windows/win32/api/winbase/nf-winbase-movefileexw)、[GetFileAttributesW](https://learn.microsoft.com/en-us/windows/win32/api/fileapi/nf-fileapi-getfileattributesw)）。这不验证当前 OpenSSL/cpp-httplib/CRT/ZIP 在 Win7 x86/x64 的真实加载、TLS 与文件语义；三架构 Release 也未由本次兼容补丁重新完整跑完。

## 发现与残余

### U-01 — P1，H0 既存的本地更新指令错误类型可使进程终止；已修补、验证边界分开

`IdtMain.cpp` 启动读取 `globalPath/update.json` 与 `installer/update.json` 虽已有 64 KiB/JsonCpp `stackLimit=32`，此前只在 `parseFromStream` 成功后直接 `isMember`，没有先核根为 object；隔离文件内容 `[]` 即可进入 JsonCpp `Value::find` 的 object 前置条件错误。字段类型错误如 `{"edition":{}}` 在另一路 `AutomaticUpdate` 的 `asString()` 也会抛 `Json::LogicError`（本机 JsonCpp 头 `JSON_USE_EXCEPTION=1`；[JsonCpp 官方源](https://github.com/open-source-parsers/jsoncpp/blob/master/src/lib_json/json_value.cpp)）。旧 `Net.Update.cpp:556-590` 还无大小/深度上限地 `Json::Reader::parse(ifstream)`，并在 `readjson.imbue(locale("zh_CN.UTF8"))` 处可能因目标机缺 locale 抛异常；同为无 catch，而执行它的 `AutomaticUpdate` 是 detached 线程（`Setting.cpp:350` 或带 `IDT_RELEASE` 的启动分支），异常可导致 `std::terminate`。**可达性分开看**：两处 `IdtMain` 文件一存在即在启动主线程解析；worker 的缓存复用须有非 mandatory 的更新轮次、较新版与 `enableAutoUpdate=true`，因为设置 Repair 首轮将 `mandatoryUpdate=true`，会跳过 `&& !mandatoryUpdate` 分支。当前 vcxproj 未显式定义 `IDT_RELEASE`，不能宣称默认自动线程始终运行；后续 Repair 线程轮次或其它发布宏组合需核。两条都是 H0 已存在的错误类型处理缺口，不能归咎于本次 HTTP 回退。

当前 `IdtMain.cpp:965-990,1191-1221` 已把根 `isObject()`、字段类型判断和 `std::exception` 捕获放在同一解析边界内；不再对 `[]` 调对象成员访问。`Net.Update.cpp::TryReadStagedUpdateMetadata` 直接生产 helper 现在以 `CreateFileW` 固定句柄读取最多 64 KiB，循环检查 ReadFile 字节数，JsonCpp `stackLimit=32/failIfExtra`，先核 object/hash/每个字符串字段，再在局部构造并发布；整个 helper `noexcept` 且将 JsonCpp/标准异常转 false，不再构造 `locale("zh_CN.UTF8")`（`:58-116`）。`AutomaticUpdate` 的旧复用分支已只调用该 helper，失败视 `fileDamage`，不再由错误类型越出 detached 线程。该代码变化没有放宽 URL、ZIP 或哈希来源边界。

直接生产 reader 无 GUI 子命令 `--staged-update-json-boundary-test` 在隔离测试目录逐个读取 good、`[]`、hash-array 和 edition-object。旧代码三份 malformed 分别为 exit2，stderr 两次 `requires objectValue or nullValue`、一次 `Type is not convertible to string`；新代码四份均 exit0，含 good 成功和三份明确无效。随后同一生产 reader 对带 BOM 的有效 218 B 文件 exit0，对 70,215 B 超限但语法有效文件及 80 层嵌套有效 JSON 均按预期拒绝、CLI exit0；这些输入不是只检查复制算法的测试替身。主 agent 用显式隔离进程句柄核 PID/退出码；原始日志 `TestResults/release-hardening/f053-json-9edc3dae794b4fceb5efff67b3e69323/*.red/green.*`。修补后完整 `InkeysRepo.sln Debug|ARM64` Build exit0、`InkeysHeadlessTests --no-window` exit0，日志 `f053-json-f041-lane-final-build-debug-arm64.log`、`f053-f041-final-headless.*`。这些绿灯**只证明生产 reader 在已列输入不逃异常/按限值拒绝**；未以真实启动 `globalPath/update.json`、`installer/update.json` 或有活跃 Office/Win7 的产品配置复验 `IdtMain` 两处分支，也未模拟磁盘在读取中途变化/全盘满。故状态为“已修复、reader 自动验证通过；启动/更新业务链待人工或进一步隔离验证”，不写整个更新恢复 PASS。

### U-02 — 用户接受的 HTTP 来源认证残余风险，非本轮回退实现错误

主动网络攻击者能阻断 TLS 使固定源退到 HTTP，随后替换 version JSON、下载 URL 与自报 MD5/SHA-256，并提供自洽 ZIP/EXE。HTTPS 跨主机跳转当前也只验证各跳证书/主机名，没有冻结发布 CDN allowlist；同源 hash 不等于发布者证明。设置“修复”使更新链可达。用户明确要求继续 HTTP fallback 且不加新签名，因此本轮不能标记来源认证已修复，也不能以“开源”证明安全；记录为接受的高影响供应链风险，最终发布结论须如实呈现攻击前提。

### U-03 — P2，H0 既存的旧 EXE 回退未核启动结果；当前已修复待真实运行验证

H0 fail 分支只对 `old_name` 或 `智绘教.exe` 调一次 `ShellExecuteW` 后无条件 `return 0`；候选存在但损坏、权限不足或启动被拒时会退出旧进程而无成功新实例。当前 `IdtMain.cpp:1147-1174` 已改为最多三次**不同**安全候选：`SelectUpdateRollbackExecutableName` 每次优先 oldName→`智绘教.exe`→`Inkeys.exe`，回调先排除已尝试名字，再用 `GetFileAttributesW` 排除不存在、目录和 reparse；`ShellExecuteW` 仅在 `reinterpret_cast<INT_PTR>(result)>32` 时结束，失败继续下一候选。紧随失败调用的 `GetLastError()==ERROR_CANCELLED` 会停止后续尝试，尊重用户取消；都失败则 `ShowStartupMessage` 明示需人工打开旧版本。此静态差异关闭了“只试一次、不核返回值”的已确认分支，保留旧 Inkeys2 缺 oldName 时中文 EXE 优先顺序。[微软 ShellExecuteW 文档](https://learn.microsoft.com/en-us/windows/win32/api/shellapi/nf-shellapi-shellexecutew)支持用 `>32` 判断 API 成功和失败后读取扩展错误。尚未对真实旧版 EXE、权限拒绝、UAC 取消和下一候选成功做 GUI/进程级验证；`ShellExecuteW>32` 也只表示 Shell API 接受请求，**不是新进程完成启动或 ready**。若安装目录能被同权限外部进程并发改动，属性检查到实际 ShellExecute 仍有条件性路径替换窗口，需按实际 ACL/权限前提单列，不从本机静态检查推断提权。

## 其它未验证项

低磁盘空间、拒绝写入、HTTP 中途断连、跨 host/端口 redirect、同 HANDLE 截断后回读、坏 CRC/截断/重名/异常 ZIP、文件系统重解析竞态、旧 Inkeys2 中文 EXE 真执行、真实 CDN/证书、Win7 SP1+仅 KB2670838 和三架构 Release 尚无本轮动态证据。ZIP 解析器的第三方内部内存安全不能由外层 entry/size 上限证明；存储目录若允许同权限进程并发替换，路径检查到实际操作仍有条件性 TOCTOU，应有隔离威胁模型和实际权限前提后再定级。自动更新入口关闭与设置修复入口可达要分开记录。
