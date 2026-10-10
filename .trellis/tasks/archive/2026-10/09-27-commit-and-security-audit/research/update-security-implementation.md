# F-014 更新链最小安全设计与验收

## 设计边界（实施前）

- 修改生产下载与安装器消费的最小路径：`Net.Update.Download.cpp` 统一 HTTPS 请求，`Net.Update.cpp` 校验元数据并仅解压指定 EXE，`UpdatePathSafety.h` 提供产品启动与下载共用的路径纯函数。`IdtMain.cpp` 由主 agent 独占接入，不在本子任务修改。
- 元数据固定主/备源；每一跳显式构造 `httplib::SSLClient`，证书与主机名校验开启，最多三次 HTTPS 重定向。禁止自动跟随、HTTP 降级、userinfo、无效主机/端口和跨 scheme 跳转。下载包地址仍由现有元数据给出，限制为合法 HTTPS URL；尚无发布签名，不把其 SHA-256/MD5 当作来源认证。
- 元数据 JSON、压缩包流、ZIP entry 数与单项/累计展开量设固定上限。接收器遇到超限或 `ofstream::write` 失败立即停止，只有 HTTP 200、完整写入与非空响应才算下载成功。
- `representation` 必须是单个安全 `.exe` 文件名。先验证所有路径/哈希/大小再创建 staging；绝不使用远端文件名作落盘路径。只接受 ZIP 内唯一同名 EXE entry，预扫描全部 entry 的数量、目录与大小；应用创建固定 staging EXE 文件句柄后仅提取该 entry，核 ZIP API 返回码和 EXE 哈希，最后才发布 `update.json`。清理仅限本次创建的 staging 文件，不递归删除未知文件。
- Win7 SP1+仅 KB2670838 仍使用当前 OpenSSL/cpp-httplib 和 Win32 ZIP API，不引入更高系统 API。TLS 失败应保持旧版本可用并报告失败。

## 验收条件（实施前冻结）

1. 源码中无 `httplib::Client` 明文降级或 `set_follow_location(true)`；每跳均验证 HTTPS、host、证书和主机名，跳转数有界。
2. `GetEditionInfo` 在 success 前拒绝无效 URL、`representation`、哈希格式和越界大小；`DownloadNewProgram` 在任何 remove/rename 前复核，ZIP 仅指定 EXE 被提取到应用生成目标，不调用 `UnzipItem(..., ze.name)`。
3. ZIP Open/Get/Unzip/Close、下载写入和 JSON 写入失败都不发布新 `update.json`；已有 EXE/未知文件不被递归清理。
4. 静态 diff 与完整 Solution 编译由协调后的单一构建执行；离线夹具须覆盖 TLS/redirect、恶意 URL/文件名、ZIP 穿越/重名/超限/损坏/磁盘写失败及成功更新。未跑的 Win7、三架构与签名验收保留未验证或发布阻塞。

## 实施与静态自审

- UpdatePathSafety.h 是生产与 headless 测试共用的纯逻辑：安全 EXE basename、installer\ 固定相对路径、32/64 位十六进制哈希语法、HTTPS URL/redirect 解析。IdtMain.cpp 已由主 agent 独立接入路径/哈希函数；本子任务未改该文件。
- Net.Update.Download.cpp 使用 SSLClient 且显式开启证书/主机名验证，禁自动跳转与明文后备。最多三次 HTTPS 跳转；元数据 1 MiB、下载 512 MiB、非 200/跳转正文 16 KiB；Content-Length 重复、超限或实际长度不符失败。下载使用 CREATE_NEW、检查 WriteFile 完整写入和 flush，失败只删除本次创建的包文件。
- Net.Update.cpp 在元数据成功及每次下载前检查 URL、representation、MD5/SHA-256 语法和非零/有界大小。私有 installer 若为 reparse point 则拒绝；每次创建独立 staging 目录，不递归删除目录。ZIP 必须打开成功，最多 128 entry，单项不超过 512 MiB、总声明展开不超过 1 GiB；只允许唯一与 representation 精确匹配的 EXE，解压到有界内存再写应用生成的 payload.exe。Open/Get/Unzip/Close、文件写/flush/哈希任何失败均不发布新 update.json。最终 EXE 使用不覆盖目标的 MoveFileExW，暂存 JSON 写入/flush 后才使用 Win7 支持的 MoveFileExW 发布；其他取消路径只移除已知 update.json，不清理未知文件。
- 独立 diff review 后又将 JsonCpp 元数据递归深度设为 32，并把 Json::Exception 视作元数据损坏。当前安装的 JsonCpp writer.h:49-55 明确约定 StreamWriter::write 返回 0 为成功、非 0 为失败且还须检查输出流；本实现两者均检查。
- 独立复审 P2-1 后，FetchHttps 以首跳 host 为参照，每跳复制 headers；仍在原 host 的跳转保留 Referer，跨 host 的 HTTPS 请求移除 Referer，避免把 GetUploadInfo 中版本/架构/系统字段发给第三方。该字段不是令牌；本修补未改变 TLS、URL、ZIP 策略。
- git diff --check exit 0；两原文件仍为 UTF-8 无 BOM、CRLF。静态搜索无实际 httplib::Client、set_follow_location(true)、remove_all(installer) 或以 ZIP entry name 落盘。主 agent 报告在 JsonCpp 深度/异常补丁之后，InkeysRepo.sln Debug|ARM64 完整构建 exit 0、严格 --no-window 测试 exit 0，新增纯 helper 用例通过；之后的 P2-1 Referer 修补尚须重建。HTTP/ZIP 夹具与 Win7 运行未执行，因此完整更新链状态仍为“已修复待验证”。
- 当前 Release 自动线程入口按主 agent 复核处于关闭状态；Setting 修复按钮仍可到达这条更新链。风险不因默认入口关闭而不适用。

## 仍需发布决策与离线回归

- 元数据自报 SHA-256/MD5 不是签名，当前尚无内置发布公钥或发布者身份验证；服务端或可信 HTTPS 镜像被攻陷时仍可能提供自洽恶意 EXE。跨主机 HTTPS redirect 仅要求每跳有效证书/主机名与语法，批准 CDN host 列表尚未冻结。上述两项不能以本次 TLS/ZIP 修复宣称完整来源认证。
- ziputils 在 GetZipItem 内的历史解析器仍处理不可信 ZIP；本次内存/数量/大小限制不证明其无内存漏洞。应在隔离临时目录以坏 CRC、截断、超长/穿越/重名、异常大小 ZIP 做故障注入，另行评估受限 fuzz。
- 可执行回归：headless 直接 include UpdatePathSafety.h 跑 HTTP URL、userinfo、错误端口、HTTPS→HTTP、scheme-relative 和相对 HTTPS 跳转正反例；本地 TLS 夹具返回有效/过期/错误主机名证书、301/302/307、超限正文与截断响应；临时 installer 哨兵文件旁测试 ZIP 重名、../、目录、展开超限、写入失败与正确包，断言仅目标 EXE 和 update.json 在成功后出现，失败保留原文件与旧 JSON。真 Win7 SP1+仅 KB2670838、x86/x64/ARM64 与真实 CDN 跳转仍须独立验收。
