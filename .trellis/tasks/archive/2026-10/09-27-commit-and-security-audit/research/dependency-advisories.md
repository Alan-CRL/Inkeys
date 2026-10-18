# 锁定依赖公告核验（2026-09-27）

本轮只读核对 `vcpkg.json`、实际 ARM64 安装头与项目调用点，并查官方/维护者公告；不据版本旧直接判定可利用，不修改 Vcpkg 或随附第三方源码。

| 依赖 | 当前证据 | 公告与适用性 | 本轮结论 |
| --- | --- | --- | --- |
| OpenSSL | `vcpkg.json` override `3.0.8#2`，更新链的 cpp-httplib 使用 OpenSSL TLS 客户端 | OpenSSL [3.0 EOL 公告](https://openssl-library.org/post/2026-09-16-eol30/) 称 2026-09-07 后无公开安全修复；[3.0 release notes](https://mirror.openssl-library.org/news/openssl-3.0-notes/) 显示此后多次安全修补。[CVE-2024-6119](https://www.openssl-library.org/news/vulnerabilities-3.0/index.html) 影响 3.0.0–3.0.14 的 X.509 name check，TLS client 属潜在调用类型；但当前 cpp-httplib 在证书链错误时先返回，攻击者能否在本产品可信源链构造可触发证书未验证。 | **依赖风险需处理/真机核验**，尚无本产品可利用复现；不能因静态链接编译通过宣布安全。升级须独立核 Win7 SP1+仅 KB2670838、三架构、OpenSSL/CRT ABI、许可与证书链。 |
| cpp-httplib | `vcpkg.json` override `0.22.0`，当前更新链调用普通 `SSLClient::Get` + response/content callbacks，手动 HTTPS 重定向；不使用 Server 或 `httplib::stream::Get` | 维护者 [GHSA-39q5-hh6x-jpxx](https://github.com/yhirose/cpp-httplib/security/advisories/GHSA-39q5-hh6x-jpxx) 指向 `stream::Get` 的恶意 Content-Length 崩溃，普通 `Get` 明确不受该特定缺陷影响。[GHSA-c3h8-fqq4-xm4g](https://github.com/yhirose/cpp-httplib/security/advisories/GHSA-c3h8-fqq4-xm4g) 要求代理和库内 `set_follow_location(true)`；当前代码已禁库内自动跳转。多个 request smuggling/trailer 公告要求本进程作为 Server，本产品更新链无此入口。 | 已做**公告条件映射**，不能把“版本在受影响范围”直接说成当前更新调用可利用；仍缺全部客户端公告逐项确认、Win32/x64 和恶意 TLS/HTTP 本地夹具。维护者当前 README 说明 [32 位平台未做安全审查](https://github.com/yhirose/cpp-httplib)，不能凭 Win32 编译宣布安全。 |
| JsonCpp、lunasvg、msgpack、随附 ziputils | `vcpkg.json` 与主工程锁定；UInk codec 对文件、对象、递归、模型成本已有生产限制；ziputils 保留历史代码 | 本轮未完成逐版本官方 advisory/二进制来源全量核验；更新 ZIP 只提取指定 EXE 到固定路径，但旧解码器解析不可信 ZIP 的内存安全尚未做受限 fuzz。 | **未验证**，不是 PASS；重大依赖升级不在无 ABI/Win7 证据时静默进行。 |

结论限于已查官方公告与当前调用点。发布前需确认实际 Release 链接的版本、NOTICE/许可证、三架构与 Win7 TLS/证书结果，并决定发布签名/公钥与依赖修补策略；本报告不把自报 SHA-256 当来源认证。
