# 生产更新链威胁模型与最小防护序列

审查对象：当前未提交工作树中的 `Inkeys/Inkeys/Net/Net.Update.Download.cpp`、`Net.Update.cpp`、`Inkeys/IdtMain.cpp`、`Inkeys/additional/ziputils/unzip.cpp`，以及本机实际使用的 `VcpkgInstalled/Arm64/arm64-windows-static-v143/include/httplib.h`。只读静态审查；未连接更新服务器、未制作/解压恶意 ZIP、未运行构建或 GUI。目标系统仍为 Win7 SP1 仅 KB2670838；本报告不据源码宣称已在该系统运行通过。

## 生产可达性和信任边界

1. `IdtMain.cpp` 的启动时自动线程受 `#ifdef IDT_RELEASE` 保护；当前 `IdtMain.h:20` 注释了该宏，`Inkeys.vcxproj` 的 Release 预处理定义也未加入它，因此**当前可构建 Release 不在启动时自动进入更新线程**。不过已开放的设置页“修复”按钮 `Setting.cpp:2505` 置 `mandatoryUpdate=true`，经 `QueueAutomaticUpdate`/`SettingBusinessKind::StartAutomaticUpdate` 调用正式 `AutomaticUpdate` (`Setting.cpp:334`)；该高危链仍可由用户操作触发。若发布配置以后定义 `IDT_RELEASE`，启动时自动检查又会启用。配置默认 `enableAutoUpdate=true`（`IdtMain.cpp:1193–1199`），可被持久化设置覆盖；`Net.Update.cpp:326–371,442–479` 在条件满足时下载/解压。此处由主 agent 复核编译宏后纠正原只读报告的默认可达性表述，未改变其余源码证据。
2. 版本 JSON 来自固定主/备域名（`Net.Update.Download.cpp:27–80`）；它提供 `edition_date`、当前架构 URL 列表、`representation` 和 MD5/SHA-256（`Net.Update.cpp:94–149`）。下载 URL 用 `splitUrl` 提取 domain/path，`prefix` 被丢弃（`:198–236`）；域名没有产品侧允许列表。哈希和下载位置来自**同一份远程元数据**，哈希只能检查相对于这份元数据的字节一致性，不能独立证明来源。
3. 下载到 `globalPath\installer` 后，`DownloadNewProgram` 在校验 EXE 哈希之前遍历并解压**全部** ZIP entry（`:221–276`）。只有之后才写 `installer\update.json`（`:287–308`）。下一次启动，`IdtMain.cpp:616–717` 从该 JSON 读出路径/哈希，按 JSON 中哈希复查并 `ShellExecuteW` 启动候选 EXE。若更新器阶段已经接受被篡改的元数据，启动阶段复查同一份不可信哈希不能恢复来源真实性；实际替换由辅助更新启动流程 `IdtMain.cpp:489–614` 继续处理。

## 已确认的当前代码问题

| ID | 证据与触发前提 | 实际影响/严重性 |
| --- | --- | --- |
| U-1 明文降级 | 元数据主/备源在 TLS 失败或非 200 时直接 `httplib::Client` HTTP 重试（`Net.Update.Download.cpp:34–44,61–71`）；ZIP 同样重试 HTTP（`:109–133`）。两处 `SSLClient` 都启用 `set_follow_location(true)`；本机 `httplib.h:7950–8003` 对 `Location: http://...` 构造明文 `ClientImpl`。只要攻击者处于网络路径并能阻断 TLS 或注入被跟随的 HTTP 重定向，就能修改元数据/ZIP。**高危，Release 阻塞**；不需要控制真实 HTTPS 证书。 |
| U-2 校验前任意路径写入 | `Net.Update.cpp:247–255` 对每个 ZIP entry 直接 `UnzipItem(hz,i,ze.name)`，直到解压后才比较 EXE 哈希 `:263–276`。`unzip.cpp:3686–3699` 只剥离绝对路径和包含斜杠前缀的 `..` 片段；开头 `../file` / `..\file` 未匹配而保留。`:3866–3873` 把该相对目录与 `rootdir=installer\` 拼接并以 `CREATE_ALWAYS` 打开。恶意归档可在哈希失败前写入 installer 之外当前进程有权限的文件。**高危，Release 阻塞**；前提是攻击者控制下载 ZIP（U-1、更新服务/分发源被攻陷，或本机缓存被篡改）。不能静态断言已发生实际攻击或提权。 |
| U-3 元数据路径删除/搬移 | `representation` 直接来自 JSON（`Net.Update.cpp:147`）；`:242` 在解压前执行 `filesystem::remove(globalPath + "installer\\" + representation)`，`:260` 再用同一拼接路径 `rename`。没有 basename、规范化或目录边界检查。`../` 可让 `remove` 指向 installer 外的现存文件，之后还可能把该路径搬入 installer；无需恶意 ZIP entry。**高危数据损坏/越界操作，Release 阻塞**；前提是可控制版本元数据。 |
| U-4 更新包来源未独立认证 | 元数据中同时包含 URL 与两种 EXE 哈希（`Net.Update.cpp:99–149`），下载后仅比较这两个哈希（`:263–276`），`installer\update.json` 又复制同一组哈希（`:289–308`）。当前调用链无签名 manifest 或发布公钥验签，也没有调用代码签名验证。攻击者若能控制元数据及包，就可提供自洽哈希，候选 EXE 在后续更新重启时被执行（`IdtMain.cpp:690–715`）。**高危，Release 阻塞**；执行通常发生在后续更新/重启流程，未证明立即运行或权限提升。CI 有可跳过的 SignPath 步骤（`.github/workflows/build-windows.yml:399–460`），不能把“构建可能签名”当成客户端已验签。 |
| U-5 错误和资源边界缺失 | `Net.Update.cpp:247–257` 不检查 `OpenZip`、`SetUnzipBaseDir`、`GetZipItem`、`UnzipItem` 返回值；未限制 entry 数/解压总量或仅提取所需 EXE。`Download.cpp:96–100` 的流接收器在 `ofstream::write` 失败后仍返回 true，`:133` 只按 HTTP 200 报成功；元数据响应和 JSON 也无显式大小上限。恶意/异常服务可造成磁盘、内存、CPU 耗尽或错误状态被掩盖。**高可用性风险**；具体崩溃和内存破坏需受限夹具/故障注入，不能从缺少检查直接宣称已复现。 |
| U-6 替换失败可能丢失旧 EXE | `IdtMain.cpp:589–597` 先删除旧程序，再 `copy_file(... overwrite_existing,ec)` 和 `ShellExecuteW`，但未检查复制的 `ec` 或新进程创建结果就返回。磁盘满/权限变化等条件下可能留下不可启动的安装。**严重正确性风险**；触发概率与恢复手段未运行验证。 |

附：`httplib.h:1636–1637,9801–9853` 默认开启证书和主机名验证，`SSLClient` 最低 TLS 1.2（`:9589–9603`）。本报告**没有**认定正常 HTTPS 连接必然跳过证书验证；确认的问题是应用的 HTTP 后备和跨 scheme 自动重定向。OpenSSL 对 `SSL_VERIFY_NONE` 客户端行为及 `SSL_get_verify_result` 的说明见 [官方 SSL_CTX_set_verify 文档](https://docs.openssl.org/3.0/man3/SSL_CTX_set_verify/) 与 [SSL_get_verify_result 文档](https://docs.openssl.org/3.0/man3/SSL_get_verify_result/)。

## 尚未确认的风险和边界

- `unzip.cpp:3662–3699,3737–3765` 对 ZIP 文件名、extra 字段使用固定数组和历史解析逻辑；可见多个长度/返回值处理薄弱点，但本轮未构造归档或做内存检测，**不能**标记为已确认越界读写。仅把不可信 ZIP 在认证前送入该解析器视为已确认的攻击面。
- 本地 `installer\update.json` 的 `path` 可由本地文件内容直接决定（`IdtMain.cpp:654–715`），且该 JSON 的哈希不具备独立来源认证。是否构成跨权限提权取决于目录 ACL、UIAccess/提升状态和攻击者能否写入；尚未验证。`IdtMain.cpp:491–614` 辅助更新流程的 `representation`/`old_name` 路径和删除顺序也需独立审查，不能把 U-3 的网络前提自动套给此本地文件入口。
- Win7 SP1 仅 KB2670838 上的 TLS 1.2、根证书/链、OpenSSL 静态产物、真实 HTTPS CDN 和 ZIP/文件原子替换均未运行验证。不能为 Win7 TLS 失败保留 HTTP 回退；应明确更新失败并保持现有程序可用。

## 最小防护序列（保持 Win7 和现有更新语义）

### 发布前必须完成

1. **传输 fail closed**：删除两处 HTTP 自动回退；对元数据和 ZIP 的每一跳只允许 `https`，证书/主机名验证失败即报错。不要直接保留 `set_follow_location(true)`：禁用自动跳转后，用有界跳转解析器逐跳验证 `https`、合法 host/port、无 userinfo，并限制为批准的 CDN/镜像；未知 CDN 行为先通过当前真实地址的只读观察确认。严格校验元数据 URL（`splitUrl` 的 prefix、domain、path），拒绝 `http`、非 HTTPS scheme、空域名和未批准来源；下载仍可轮询多个可信 HTTPS 镜像。该做法复用当前 OpenSSL/cpp-httplib，不要求新 Windows API。
2. **先固定受信 manifest 合同**：元数据必须由内置发布公钥验签，签名绑定 channel、architecture、edition、ZIP URL、ZIP SHA-256、EXE SHA-256、`representation` 和大小上限；或采用经实测 Win7 兼容且明确固定发布者身份的等价代码签名策略。当前同源 MD5/SHA-256 不替代该步骤。已有 OpenSSL 依赖可用于离线签名验证，无需假定 Win7 上新的系统加密 API；发布端公钥/私钥管理和旧版 metadata 迁移须明确。未建立可信签名链时，不能把仅 HTTPS + 自报 hash 标作完整来源认证。
3. **先验参数再动文件**：`representation` 只能是合法的单个 EXE 文件名，无 `.`/`..` 组件、斜杠、反斜杠、冒号、绝对路径、设备路径或尾部空格/点；所有 URL 必须满足上述 HTTPS/域名约束；hash 格式与长度、版本、架构、正整数大小先检查。任何 `remove`/`rename` 前确认解析后的目标确在私有 installer staging 内，拒绝重解析点/非预期对象；不要以 `globalPath + 不可信字符串` 直接操作。避免 `remove_all(installer)` 清掉未知文件。
4. **隔离下载，校验 ZIP 后解压**：下载到唯一私有 staging 目录的临时文件，接收器在写失败或超过压缩包大小上限时返回 false；核对 HTTP 状态、实际字节数和签名 manifest 中的 ZIP SHA-256，失败仅清理本次创建的 staging。预扫描并限制 ZIP entry 数、单项及累计解压大小、路径长度；最小化为**只提取 manifest 指定的一个 EXE entry**，拒绝重复/异常 entry，并用应用生成的固定 staging 目标路径调用 `UnzipItem`，绝不传 `ze.name` 作为落盘路径。检查每个 ZIP API 结果和最终 EXE hash；ZIP 错误、空间不足和中断都不得生成 `update.json`。
5. **验证后发布与可回滚替换**：EXE hash/签名通过后，以同目录临时 JSON 写完、flush、再使用 Win7 已支持的文件替换/移动机制发布 `update.json`；启动阶段再次验证路径、签名/哈希。替换旧 EXE 前先准备新文件并检查复制/移动结果，保留最后有效 EXE 直到新文件就绪；检查 `ShellExecuteW` 返回值，失败恢复旧入口。不要通过复制未成功也返回 0 的路径丢失安装。

### 可在上述门禁完成后继续深化

- 对第三方 ziputils 做独立内存安全审查/受限 fuzz；评估替换为受维护 ZIP 库时必须单独核 ABI、许可证、Win32/x64/ARM64 和 Win7。
- 将当前安装目录 ACL、reparse point 与 UIAccess/提升进程组合纳入本地攻击前提测试；再决定是否需要更严格的目录创建和句柄级路径验证。
- 完善更新断点续传、用户反馈和诊断，但不能以放宽 TLS、跳过签名或预先删除旧程序换成功率。

## 回归验证建议（本轮未执行）

- 本地受控 HTTP/HTTPS 夹具分别返回 TLS 失败、伪证书、HTTPS→HTTP 302、未知 HTTPS host、可信 HTTPS 同域/批准镜像重定向；断言失败路径无下载包、无 `update.json`、无旧程序替换，成功路径保留镜像功能。不对外部服务发攻击流量。
- 使用独立临时目录的测试 ZIP：`../outside.txt`、`..\outside.txt`、绝对/盘符/UNC、重名/大小写别名、ADS、超长名称、目录、异常 entry 数、超大展开尺寸、坏 CRC/截断包；断言任何失败都不写 staging 外部且不覆盖预置哨兵文件。用 ZIP hash 不匹配夹具证明解析/解压前拒绝。
- 签名 manifest 正例及变更任一字段/哈希/URL/架构的反例；ZIP/EXE hash 正反例；文件写满/只读/rename 失败时原 EXE 保持可启动，`update.json` 不发布或保持最后有效版。仅测试本项目的隔离文件和进程。
- 完整 Solution 的 Debug/Release × Win32/x64/ARM64 构建和适用无窗口测试；真 Win7 SP1 仅 KB2670838 分别验证有效 TLS 1.2 证书、过期/无根证书失败、实际 CDN 重定向、更新成功与错误回退。构建成功不能升级为 Win7 运行 PASS。
