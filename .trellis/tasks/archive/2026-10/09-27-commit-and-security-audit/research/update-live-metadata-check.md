# 更新源只读形状核验（2026-09-27）

- 在隔离命令行中只读请求代码既有主 CDN 的 HTTPS `version.json`，返回 HTTP 200、UTF-8 正文 8,934 字节；不下载或执行安装包。备源 HTTPS 请求在本机 .NET TLS 层失败，未判定服务器或产品 OpenSSL 行为。
- 主源 JSON 有 LTS、Insider、Canary 三通道；均有 `representation=Inkeys.exe`、`size.file/file64/fileArm64` 和相应 `path/path64/pathArm64`。这九个包 URL 均是 `https://get.smart-teach.cn:443` 形式，符合当前词法验证。ARM64 的 LTS/Insider 声明大小各 18,756,986 字节，Canary 为 27,927,955 字节；三通道 ARM64 MD5/SHA-256 长度分别为 32/64，均低于本次 512MiB 上限。
- 对 Canary ARM64 包地址只发 HEAD（无包体）：首跳 HTTP 302 到另一 HTTPS 域名 `cn-nb1.rains3.com`，含临时签名查询参数。当前手动 redirect parser 接受 HTTPS 跨主机及 query，跨主机时已删除 Referer。尝试 HEAD 跟随最终得到 403；签名下载地址可能仅授权 GET，本轮**未取得 GET 下载成功或 TLS/ZIP 运行证据**。原始签名 URL 不保存到仓库文档，避免泄露短时凭据。
- 上述由 PowerShell/.NET 执行，只证明当天主源 JSON 和 HEAD 形状；不是产品内 cpp-httplib 路径、Win7 根证书/TLS、真实包字节及更新可用性的 PASS。用户安装的 HC 二进制与该 URL 是否同一包亦未确认。
