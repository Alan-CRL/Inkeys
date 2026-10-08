# Research: 锁定依赖与随附二进制安全公告适用性

- Query: 截至 2026-09-29 核对 Inkeys3 实际安装的依赖/随附二进制、官方或原始安全公告、触发 API 和产品可达性；记录 Win7 SP1 + KB2670838 与三架构升级风险。
- Scope: mixed（本地源码/安装状态 + 上游公告）。本报告是针对当前产品调用面的适用性调查，不是完整依赖模糊测试或所有 CVE 的穷举证明。
- Date: 2026-09-29（Asia/Shanghai）。
- 执行边界: 只读；未运行构建、测试、GUI 或性能采样；未修改依赖、更新链和其它任务文件。用户已明确要求 HTTP/HTTPS 回退与旧 `智绘教.exe` 更新兼容保留，更新链来源认证改造不属本次修改范围。

## Findings

### 本地版本与调用面

`vcpkg.json` 固定 `builtin-baseline=99a97de2cb371449d4fb9dc970f2ac562d689ec2`，并 override OpenSSL 3.0.8#2、cpp-httplib 0.22.0、JsonCpp 1.9.6、lunasvg 3.5.0 等。实际 `VcpkgInstalled/{Arm64,Win32,Win64}/vcpkg/status` 均显示这四项 `install ok installed`；同时三架构均有传递依赖 PlutoVG 1.3.2 和直接依赖 msgpack 7.0.0。三处 triplet 分别是 `arm64-windows-static-v143`、`x86-windows-static-v143`、`x64-windows-static-v143`。安装状态不证明最终发布 EXE 已静态链接了同一份库，需以最终 HF 构建产物/链接映射另核。

| 文件 | 用途和关键调用 |
| --- | --- |
| `vcpkg.json`、`VcpkgInstalled/*/vcpkg/status` | 锁定和实际安装版本；PlutoVG 是 `Vcpkg/ports/lunasvg/vcpkg.json:8` 的传递依赖。 |
| `Inkeys/Inkeys/Net/Net.Update.Download.cpp:3,59-128,139-174,180-258` | 唯一发现的 `httplib` 第一方使用：普通 `Client::Get` / OpenSSL `SSLClient::Get`，`set_follow_location(false)`，手工最多三次重定向；HTTPS 证书及 hostname 校验启用；HTTP 回退由产品显式实施。未发现 `httplib::Server`、`httplib::stream`、代理或认证凭据配置。 |
| `Inkeys/Inkeys/UI/Bar/Bar.UI.cpp:213-274,333,400-469,528,547-604` | `lunasvg::Document::loadFromData`/`renderToBitmap` 解析 Bar SVG；`stbi_load_from_memory` 解 Bar PNG。PNG `InitializationFromMemory` 当前唯一生产调用来自 `InitializationFromResource`，`Bar.Button.cpp:715-716` 与 Bar 初始化只用内嵌图标资源。SVG 当前调用来自资源或第一方构造的字符串；未找到普通用户 SVG 文件直达 Bar 的调用。 |
| `Inkeys/Inkeys/Business/PptSession.cpp:38-57`、`Inkeys/Inkeys/Drawing/Draw3/Draw3.Presentation.cpp:203-220`、`Inkeys/Inkeys/Other/Other.Config.cpp:907-925`、`Inkeys/IdtMain.cpp:987-1002,1217-1232` | JsonCpp 读取 COM 描述、持久化/配置与本地更新说明；其中 PPT descriptor 1 MiB、`stackLimit=32`、严格 schema。不能以一个调用点的界限代替所有 JSON 路径的测试。 |
| `inkStrokeModelerTest/draw3/uink_codec.cpp:2399-2435,2465,2605`、`uink_model.cppm:424-432` | 产品集成的 UInk MessagePack 解析先做预扫描，再以 `msgpack::unpack_limit` 和 `msgpack::unpack` 解码；默认文件 128 MiB、单对象 32 MiB、深度 32 等上限。未发现 C API `msgpack_unpacker_reserve_buffer`。 |
| `PptCOM/PptCOM.csproj:45-55,64-66`、`Inkeys/Inkeys.rc:222`、`Inkeys/IdtMain.cpp:2121-2130` | 本地 PowerPoint/Office PIA 15.0.0.0 被设为 `EmbedInteropTypes=True` 的构建引用；首发 EXE 内嵌的是第一方 `PptCOM.dll`，启动时核其资源字节并加载。Office 本体仍取用户已安装的 COM 程序，PIA 版本不是 Office 运行补丁级别。 |

本地两份 PIA 的 `Get-AuthenticodeSignature` 结果均为 **Valid，Microsoft Corporation**；`AssemblyName` 均为版本 15.0.0.0、PublicKeyToken `71e9bce111e9429c`。指纹：`Microsoft.Office.Interop.PowerPoint.dll` SHA-256 `E252DB17CFD1BB58EEA20B5D8CA2B676236C8B015985B36398C569E5A67F0F0B`；`Office.dll` SHA-256 `DC1C9337435FA37201DBB8C012E0397E0A1BAE7273305CA397FEED566BA0F9E9`。本地 `.nupkg` 与 [NuGet PowerPoint PIA 15.0.4420.1018](https://www.nuget.org/packages/Microsoft.Office.Interop.PowerPoint/) 的版本对应，但尚未比对 NuGet 服务端包 hash/签名；DLL 的有效 Authenticode 也不代表本机 Office 安装已获安全补丁。

### 公告适用性判定

| ID / 依赖 | 判定 | 当前证据、触发前提与实际影响 |
| --- | --- | --- |
| DEP-01 OpenSSL 3.0.8#2 / CVE-2024-6119 | **confirmed: 锁定版本受影响；hypothesis: 可触发的产品 DoS** | [OpenSSL 原始公告](https://mirror.openssl-library.org/news/secadv/20240903.txt) 说明 3.0.0–3.0.14 在 X.509 `otherName` 与预期 DNS/IP 名称比较时可能读非法地址并异常退出，3.0.15 修复。产品 `SSLClient` 启用主机名检查，故对应 API 类别可达；攻击仍需把特制证书送入有效 TLS 验证/名称检查流程，当前固定更新源、系统信任链和网络位置下没有本轮复现证据。不要写成“已确认可远程利用”或“安全通过”。截至本日 [OpenSSL 3.0 已于 2026-09-07 EOL](https://openssl-library.org/post/2026-09-16-eol30/)，也不再有公开安全修复。用户排除更新链安全改动，故记为已知依赖暴露/待发布风险取舍；不据此擅自换库。 |
| DEP-02 OpenSSL 3.0.8#2 / 其它公告 | **not applicable to observed calls / unverified overall** | [OpenSSL 3.0 公告索引](https://mirror.openssl-library.org/news/vulnerabilities-3.0/) 和 [2026-08-25 原始公告](https://mirror.openssl-library.org/news/secadv/20260825.txt) 含 CMS、CMP、DTLS、PKCS7、QUIC 等问题。当前第一方 OpenSSL 入口是 HTTPS 更新客户端，未发现这些专用 API/服务器协议入口；例如 2026-08 的 CMS key unwrap 需 `CMS_decrypt()`，不适用于已观察的 TLS `SSLClient`。其它 TLS 客户端公告仍需逐项复核，不以“只用 HTTPS”概括 OpenSSL 3.0.8 全版本无风险。 |
| DEP-03 cpp-httplib 0.22.0 / GHSA-39q5-hh6x-jpxx | **not applicable to observed API** | [上游公告](https://github.com/yhirose/cpp-httplib/security/advisories/GHSA-39q5-hh6x-jpxx) 明确恶意 `Content-Length` 崩溃在 `httplib::stream::*` / `open_stream()`；普通 `Client::Get()` 走另一个先校验的解析路径。当前只见普通 `Get`；版本落在公告影响范围不足以宣布产品有该崩溃。 |
| DEP-04 cpp-httplib 0.22.0 / GHSA-6hrp-7fq9-3qv2、GHSA-c3h8-fqq4-xm4g | **not applicable to observed configuration** | [跨域重定向凭据公告](https://github.com/yhirose/cpp-httplib/security/advisories/GHSA-6hrp-7fq9-3qv2) 需要库自动跟随重定向并持有 Basic/Bearer/Digest 凭据；[代理重定向 TLS 公告](https://github.com/yhirose/cpp-httplib/security/advisories/GHSA-c3h8-fqq4-xm4g) 还要求代理。产品每个新 client 都 `set_follow_location(false)`，重定向在 `FetchUpdate` 手工解析；未发现 `set_proxy` 或认证 setter。保留 HTTP 回退意味着独立的来源认证风险，但不是这两个 GHSA 的触发链。 |
| DEP-05 cpp-httplib 0.22.0 / GHSA-j6p8-779x-p5pw 及 server 类 | **not applicable to observed role** | [0.22.0 精确命中的 trailer smuggling 公告](https://github.com/yhirose/cpp-httplib/security/advisories/GHSA-j6p8-779x-p5pw) 要求产品起 `httplib::Server` 并接收 chunked 请求；当前只用客户端。2026 的服务端 gzip、trusted-proxy、GET body 类同理。另 [Mbed TLS/wolfSSL IP literal 公告](https://github.com/yhirose/cpp-httplib/security/advisories/GHSA-8ffh-4p95-g3p2) 从 0.31 起，且原文明确 OpenSSL backend 不受影响；0.22/OpenSSL 不符合。没有宣称该依赖所有 21 项公告均逐项排除。 |
| DEP-06 msgpack 7.0.0 / CVE-2026-72854 | **not applicable to observed API** | [原始 GHSA/CVE 描述](https://github.com/advisories/GHSA-p5jw-mm77-f8jp) 指向 C API `msgpack_unpacker_reserve_buffer` 接受近 `SIZE_MAX` 的由集成方提供的 reservation size，库自己的 decode 并不会从不可信输入得出该尺寸。UInk 用 C++ `msgpack::unpack`、预扫描及 `unpack_limit`，未调用此 C reserve API。UInk 自身边界仍需现有故障注入测试，不能用此“不适用”覆盖其它解析风险。 |
| DEP-07 `stb_image.h` 2.28 / GHSL-2023-145..151 | **confirmed: 上游同版本有内存错误；not applicable to ordinary user-image path currently found** | [GitHub Security Lab 原始报告](https://securitylab.github.com/advisories/GHSL-2023-145_GHSL-2023-151_stb_image_h/) 明确测试 2.28，涉及 GIF、TGA、flip 等越界/双重释放。产品 Bar 的 `stbi_load_from_memory` 仅从 EXE 内嵌 PNG 图标资源入口调用；未发现用户文件/网络图片传入。若未来让用户图片走此函数，必须重新评估；当前不把上游同版本漏洞说成已证明产品远程利用。 |
| DEP-08 PlutoVG 1.3.2 / v1.3.3 修复项 | **hypothesis: 恶意 SVG/极端几何的资源或崩溃风险** | [上游 v1.3.3 release note](https://github.com/sammycage/plutovg/releases) 明确补了 surface allocation 整数溢出风险、raster pool 增长上限、`realloc` 失败。当前安装 1.3.2，且 `lunasvg::renderToBitmap` 经 PlutoVG；存在真实代码层关联，但未找到针对 1.3.2 的可用原始安全公告/产品输入复现。Bar 的 SVG 当前来自第一方资源/字符串。此项应保留有界复杂 SVG/分配失败复验；不因补丁说明就判可利用，也不自动升级整组绘图库。 |
| DEP-09 lunasvg 3.5.0 / JsonCpp 1.9.6 | **no release-matched confirmed advisory found; unverified under fuzz** | LunaSVG 3.5.0 是当前安装版本；[上游 #209](https://github.com/sammycage/lunasvg/issues/209) 的 ASan 病例针对 3.0.0，不能直接映射到 3.5.0。JsonCpp [上游 advisory 列表](https://github.com/open-source-parsers/jsoncpp/security/advisories) 当次检索显示无已发布 advisory；这只是公告检索结果，不能推断解析器或当前所有调用安全。严格输入大小/深度/类型检验和坏文件回归仍是产品合同。 |
| DEP-10 Microsoft Office PIA 15.0.0.0 / 第一方 PptCOM | **confirmed: 本地二进制签名与嵌入引用；Office 运行安全状态未验证** | 两份 PIA 当前签名有效且 `EmbedInteropTypes=True`；实际 PowerPoint/WPS COM server 来自设备安装，不能根据仓库 PIA 版本宣称 Office 已修复公告或 Win7 可运行。`PptCOM.dll` 是本项目编译/内嵌、启动前校验的第一方库；其代码审查与真实 Office 测试属单独门禁。 |

### 升级、Win7、三架构风险

- OpenSSL **3.0.8 → 3.0.22** 在官方 [patch 版 API/ABI 兼容政策](https://mirror.openssl-library.org/policies/general/versioning-policy/) 内，但 3.0 分支已 EOL；选 3.5 LTS 则属于 minor 迁移。任何升级仍需重新静态编译和链接 Win32/x64/ARM64，核 MSVC v143、CRT、证书 store 与 Win7 SP1 仅 KB2670838 的真实 TLS/导入表。API/ABI 政策不等于目标 OS 运行保证。用户目前不授权本次更新安全链调整；这里只给后续独立升级的检查合同。
- cpp-httplib 0.22 → 较新版本、lunasvg/PlutoVG 组合、msgpack C/C++ 版本均不应仅为清除“旧版本”标签直接升。需保留 OpenSSL 编译宏、Win7 Winsock/TLS 路径、C++20 module、静态 triplet 和 x86/x64/ARM64 ABI，分别跑 UInk/GUI/版本检查与实际输出。PlutoVG v1.3.3 的修复更适合独立相容性试验后决定。
- `NOTICE` 与 `ThirdpartyLicenses/` 涵盖依赖许可；当前报告未检查三个最终完整 ZIP 的实际随包内容。NuGet PIA 的签名不是许可证完整性证据。

### 对集成/发布门禁的明确建议

1. **需保留风险而非安全 PASS**：DEP-01 受影响版本和 EOL 是事实，产品可触发攻击尚未验证；更新链安全改动被用户排除。最终报告分别记录用户范围取舍与未证实利用，勿把“开源软件/HTTP 回退允许”推导为加密库无公告风险。
2. **适用的本任务验证**：用现有隔离坏配置/UInk/图标 headless 测试与 bounded parser 证据支撑 JsonCpp、MessagePack 和图像路径；对资源受控的 SVG/PNG 记清路径，真实用户 SVG/图片输入若被发现则重新打开 DEP-07/08。
3. **尚需实际系统验收**：Win7 SP1+KB2670838、Win32/x64 的 TLS 客户端和 DLL/EXE 导入表，以及三架构静态链接产物；当前本机安装状态不替代这些结果。PptCOM 还需目标 Office/WPS 与独立页恢复测试。

## External References

- [OpenSSL 2024-09-03 原始 CVE-2024-6119 公告](https://mirror.openssl-library.org/news/secadv/20240903.txt)、[3.0 漏洞索引](https://mirror.openssl-library.org/news/vulnerabilities-3.0/)、[3.0 EOL 声明](https://openssl-library.org/post/2026-09-16-eol30/)、[版本 API/ABI 政策](https://mirror.openssl-library.org/policies/general/versioning-policy/)。
- [cpp-httplib 上游 advisory 列表](https://github.com/yhirose/cpp-httplib/security/advisories) 与表中各原始 GHSA。
- [GitHub Security Lab 对 stb_image 2.28 的原始研究](https://securitylab.github.com/advisories/GHSL-2023-145_GHSL-2023-151_stb_image_h/)、[PlutoVG 官方发布说明](https://github.com/sammycage/plutovg/releases)、[LunaSVG 官方 issue #209](https://github.com/sammycage/lunasvg/issues/209)。
- [msgpack-c CVE-2026-72854 GHSA](https://github.com/advisories/GHSA-p5jw-mm77-f8jp)、[JsonCpp 上游 advisory 列表](https://github.com/open-source-parsers/jsoncpp/security/advisories)、[NuGet PowerPoint PIA 页面](https://www.nuget.org/packages/Microsoft.Office.Interop.PowerPoint/)。

## Related Specs

- `.trellis/spec/native-desktop/build-and-compatibility.md`：manifest、三架构、Office/TLB/许可产物合同。
- `.trellis/spec/native-desktop/errors-logging-and-resources.md`：资源、异常及退出边界。
- `.trellis/spec/native/quality-and-validation.md`：第三方源码和构建/测试最小验证门槛。
- `.trellis/spec/ppt-interop/com-contract.md`：PIA/COM ABI、生命周期及真实 Office 验证边界。

## Caveats / Not Found

- 没有针对本地 SSL 握手、PlutoVG 极端 SVG、`stb_image` 图标资源替换做动态注入；没有对 Vcpkg 全部传递包或 21 条 cpp-httplib 公告逐条形成完整证据矩阵。故本报告不是无漏洞证明。
- LunaSVG 3.5.0、PlutoVG 1.3.2 和 JsonCpp 1.9.6 未找到与当前版本/产品输入一致的已确认官方安全公告；上游缺少 advisory 不等于风险不存在。上游旧版本 bug 和“新版本修复”不能直接证明当前版本可利用。
- `Package/` NuGet 包本地来源链、锁定 `Vcpkg` 子模块的源码 hash、最终 EXE 与安装树的一致性、Win7 运行和实际 Office 版本均未在本研究中验证；需由最终产物和真机门禁补齐。
