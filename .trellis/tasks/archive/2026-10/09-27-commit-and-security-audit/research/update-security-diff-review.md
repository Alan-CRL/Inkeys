# F-014 更新安全补丁独立 diff 复审

审查快照：2026-09-27 当前未提交的 `Net.Update.Download.cpp`、`Net.Update.cpp`、`UpdatePathSafety.h`、`IdtMain.cpp` 两段更新启动逻辑、`InkeysHeadlessTests/update_security_tests.cpp`、测试登记与现有日志。仅作只读静态复审；主 agent 在复审期间修补了 `IdtMain.cpp` 的第二阶段 JSON 发布，已复读该处新代码；新修补尚无本报告可引用的重新构建/故障注入证据。没有运行 GUI、网络夹具、ZIP 故障注入或并发构建。

## 已改善且有代码证据的边界

- `Net.Update.Download.cpp:29–109,111–180` 只构造 `httplib::SSLClient`，显式开启证书和主机名检查、关闭自动跟随；最多三跳，解析每跳 `Location`。`UpdatePathSafety.h:14–68,71–106` 拒绝明文 scheme、`@`、非 443 端口、反斜杠和控制字符；未发现本轮静态可构造的 HTTP 降级绕过。`FetchHttps` 对正文上限、重复/超限 Content-Length、写入失败与 flush/close 失败有 fail-closed 分支。未对真实证书链和 CDN 重定向运行验证。
- `Net.Update.cpp:109–178,251–266` 在使用远程字段前校验 EXE basename、哈希格式、非零且有界大小与 HTTPS URL；逐通道 fallback 前重置 `EditionInfoClass`，避免上一次坏通道的字段残留。`UpdatePathSafety.h:108–170` 的同一纯函数也在两个启动阶段被调用。
- `Net.Update.cpp:268–499` 不再递归清空 installer；每次建唯一 staging，ZIP 最多 128 项、单项上限 512 MiB、声明累计展开上限 1 GiB；只识别精确同名且非目录的目标 EXE，解压进有界内存，再写应用固定生成的 `payload.exe`。没有以 `ze.name` 落盘；ZIP API、磁盘写和 EXE hash 失败不发布新 `update.json`。先移动候选 EXE，再以 `MoveFileExW(...REPLACE_EXISTING|WRITE_THROUGH)` 发布完整暂存 JSON，失败删除本次候选 EXE。前述边界仍未经过恶意 ZIP 夹具验证，且在 ZIP **解析之前**尚无独立包签名/压缩包 digest。
- `IdtMain.cpp:503–565,713–771` 对两类 `update.json` 增加 64 KiB 大小、JSON 深度、hash/路径格式限制。第一阶段 `:606–670` 先复制并复核临时 EXE、备份现有 `Inkeys.exe`，再替换；`ShellExecuteW` 立即失败时尝试回滚，明显改善 H0 先删旧 EXE 再复制的失败路径。

## 可执行配置及测试证据边界

- 当前本地 `IdtMain.h:20` 是 `// #define IDT_RELEASE`，`Inkeys.vcxproj:171,252,328` 的 Release 预处理宏均不含 `IDT_RELEASE`；所以**当前直接构建的 Release** 不会编入 `IdtMain.cpp:1649–1650` 的自动更新线程。`.github/workflows/build-windows.yml:52–69` 对正式发布输入检查未注释的 `#define IDT_RELEASE`；该宏一旦启用，自动更新入口可达。当前配置仍有已开放的 Setting 修复按钮 `Setting.cpp:2495–2507`，当状态为 `UpdateNotStarted` 会经业务 worker `:333–335` 启动更新线程。不要把 `#ifdef` 的源码存在误报为当前 Release 自动运行，也不要把当前自动入口关闭等同于更新链不可达。
- `TestResults/release-hardening/work4-update-json-final-debug-arm64.log` 末尾 `Build succeeded`、0 error/6 warning；`work4-update-json-final-headless-debug-arm64.log` 末尾 `PASS animation correctness`，父任务 `validation.md:38` 记录构建退出码 0。`animation_tests.cpp:1594` 确认调用 `RunUpdateSecurityTests`，其源码仅直接 include **生产共用** `UpdatePathSafety.h`，覆盖少数 URL、重定向、EXE 名、hash 纯函数反例。测试不调用 `FetchHttps`、`GetEditionInfo`、`DownloadNewProgram`、ziputils、两个 `IdtMain` helper 阶段或实际进程替换；Debug 编译和无窗口 PASS 不能升级成端到端更新安全 PASS。Release、Win32/x64、Win7 SP1 仅 KB2670838 与真实 CDN 均未在本轮审查中验证。

## 剩余 findings（按本轮可证据支持的最高等级）

### P0：本轮未确认

当前静态 diff 未见仍可直接用明文网络响应触发任意写入/执行的 P0 反例。未跑敌意网络/ZIP 夹具，也没有签名链，不能因此宣称无高危风险。

### P1-1：更新来源认证仍未闭环（confirmed design gap）

`Net.Update.cpp:113–178` 接受 HTTPS 源自报的 URL 与 EXE MD5/SHA-256，`:417–446` 用同一组 hash 验候选并写指令；`IdtMain.cpp:788–812` 最后仍按可写 JSON 自报 hash 启动候选。`ResolveSafeHttpsUpdateRedirect` 允许跳到**任意持有有效 TLS 证书的主机**（`UpdatePathSafety.h:79–106`），下载元数据所指定的包 host 也没有发布方允许列表。网络路径攻击者无法再仅阻断 TLS 迫使 HTTP 降级；但更新源/CDN 被攻陷、可信源发出恶意跨 host 跳转、受信 CA 误签或本地指令被有权限进程篡改时，攻击者仍可提供自洽 hash 与可执行文件。没有内置发布公钥/签名 manifest 或客户端发布者身份校验。后果是在后续更新重启以应用令牌运行未认证 EXE；**没有**证据证明提权或对普通网络攻击者仍可直接利用。发布门禁需要签名来源合同及隔离端到端验证；TLS 和普通 checksum 不等价于来源认证。

### P1-2：自定义旧 EXE 名的更新迁移回归（confirmed counterexample）

`Net.Update.cpp:430,445` 把当前 `GetCurrentExeName()` 存为 `old_name`。辅助阶段 `IdtMain.cpp:588–604` 只用它等待旧进程退出，`:609–671` 始终写入/备份/启动 `main_path\Inkeys.exe`，成功后没有迁移或删除 `main_path\old_name`。若允许的 ASCII 自定义启动文件名是 `MyInkeys.exe`，旧文件及指向它的快捷方式仍保留，用户下次从该入口进入旧版并再次请求更新；若更新失败，`:690–699` 的回退只找 `Inkeys.exe`/`智绘教.exe`，也可能不重启原 `MyInkeys.exe`。H0 `IdtMain.cpp` 曾在复制新程序前删除 `main_path+old_name`；本轮为避免先删而改变了兼容行为。应在保留备份/回滚能力的同时决定原文件名的迁移或可靠入口更新，并用自定义 ASCII EXE 与中文旧版两种隔离用例验证。

### P1-3：阶段二 JSON 原位截断（复审初始确认；已修待验证）

复审快照 `IdtMain.cpp:792–812` 用 `OccupyFileForWrite`、`SetEndOfFile`、`WriteFile` 在已有 `installer\update.json` **原位置**写 `old_name`/MandatoryUpdate，再启动 staged EXE。崩溃、磁盘满或中途写失败可损坏此前原子发布的最后有效更新指令；与 `Net.Update.cpp:463–491` 的暂存 JSON 原子发布不一致。应同目录写临时文件、完整写入/flush/close、再 replace，任何失败保留旧 JSON；修补后需重新审最终 diff 和故障注入结果。

主 agent 随后改为当前 `IdtMain.cpp:799–835`：同目录 `GetTempFileNameW`、完整写入/flush/close，再 `MoveFileExW(...REPLACE_EXISTING|WRITE_THROUGH)`；失败删除临时文件，旧 JSON 保持不动。当前 `:838–848` 中 staged EXE 启动失败也保留旧有效指令并继续当前程序启动。静态上原位截断已消除；状态为**已修复待验证**，应重新构建、审最终 diff，并用磁盘满/rename 失败夹具验证，不能复用修补前 Debug/no-window PASS。

### P1-4：旧 EXE 备份并非自动恢复成功证明（confirmed limitation）

`IdtMain.cpp:657–670` 只把 `ShellExecuteW` 返回 `>32` 当作成功；Windows 接受启动请求后新进程初始化失败/立刻崩溃时，代码已退出，未确认新版本达到可用里程碑。备份留在 `IKB*.tmp` 供人工恢复，但没有自动回滚或有限重试。此路径不等于“更新成功/崩溃自动恢复成功”；首次发布前至少通过隔离故障注入验证旧版本仍可人工恢复，并在发布门禁中记录新进程握手缺口。备份文件长期不清理也需容量策略（P2），不能随手删除唯一有效回退点。

### P2-1：跨 host `Referer` 泄漏及镜像边界（confirmed）

`Net.Update.Download.cpp:115–119,147–155` 构造的同一 `headers` 被 `FetchHttps` 每一跳复用（`:44–106`），而 `ResolveSafeHttpsUpdateRedirect` 允许跨主机 HTTPS。`GetRefererInfo()` 是 `Inkeys::config.GetUploadInfo()`（`Net.Update.cpp:69–71`）；当前 schema 的 Upload 字段包含版本、架构、Windows 版本（`Other.Config.cppm:260–264`），**不含** `UserId`。受信源若重定向至第三方，可收到这些诊断字段。跨 host 时去掉 Referer，或在明确批准的镜像域名上才附带；不要报告为令牌泄漏。

### P2-2：真实元数据/镜像兼容未验证（未验证，非已确认缺陷）

`GetEditionInfo` 现在要求 `size.file*` 非零且不超过 512 MiB，并拒绝任何已选前十 URL 的无效项（`Net.Update.cpp:137–178`）；失败会回退 LTS，随后还可回退 JSON 的首个成员（`:180–204`），且 `AutomaticUpdate:541–545` 会把 fallback channel 持久化。若当前服务 JSON 缺 size、使用非标准但此前可工作的 URL/域名/端口，或 CDN 合法跳转至 IP/非 443，则更新会失败或静默切换通道。直接只读访问两个硬编码 HTTPS `version.json` 端点未取得内容，不能宣称已兼容或已退化；应保存真实签名/非签名历史 fixture、验证主备 metadata 和 CDN 跳转链，检查 fallback 前后设置语义。

### P2-3：本地文件系统竞态与内存上限（条件性风险）

`Net.Update.cpp:270–299,481–491` 先以路径检查 installer 及最终 JSON 属性，随后以同名路径创建/移动文件；启动阶段 `IdtMain.cpp:769–811` 仅验证 `installer\basename` 的**词法**路径，未证实 installer 目录 ACL 和重解析点不可被其他进程在检查与使用之间替换。若存在能写该目录的较低权限进程、而更新进程有更高权限，可能形成 TOCTOU/目录逃逸；当前没有 ACL/UIAccess 前提或 race 复现，不能标记为 confirmed 提权。另 `Net.Update.cpp:394–398` 在 Win32 构建中允许单个 512 MiB 解压内存，加上 ZIP/parser/程序其他内存可能逼近地址空间限制，需用实际 Win32 包与故障样本测量，不把一次 ARM64 Debug 构建当资源证明。

## 独立复验入口

1. P1-3 新修补须重新构建并审最终 diff；P1-2 仍待处理。按原本单文件 CRLF/编码检查无整文件格式变更。用同一目录隔离测试故障注入 `WriteFile`/flush/rename/copy/ShellExecute 失败，以及成功 ShellExecute 后立刻退出，分别断言 `update.json`、旧 EXE、备份和新进程状态。
2. 对**生产** `FetchHttps` 用本地 TLS/HTTP 夹具覆盖有效/过期/错误主机名证书、HTTPS→HTTP 302、跨 HTTPS host、3 跳边界、重复/截断 Content-Length、流写失败；核每跳 scheme、Host/SNI 和 Referer。当前纯 helper 测试无法替代。
3. 对**生产** `DownloadNewProgram` 在独立临时 installer 测 ZIP `../`/盘符/UNC/重复目标、坏 CRC/截断、空包、129 项、展开上限、有效 EXE，留预置外部哨兵；失败时无越界写、无新指令且旧有效 JSON/EXE 保留。避免对真实用户目录、Office 或外部服务注入。
4. Win7 SP1 仅 KB2670838 上运行完整 TLS/证书/更新失败回退，独立于 ARM64 构建；Win32/x64/ARM64 Release 包矩阵及正式 `IDT_RELEASE` 宏配置、签名/包来源需分别记录。现有 `MoveFileExW`、`CreateFileW`、`GetTempFileNameW` 是传统 Win32 API，静态审查未见本补丁新增的高版本专用 Win32 API；是否能在目标系统与真实 CRT/OpenSSL 产物上工作仍是未验证。
