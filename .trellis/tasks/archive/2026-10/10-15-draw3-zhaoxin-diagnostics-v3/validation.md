# v3 验证记录

## 授权与工作区
- 用户已审阅计划并明确授权任务/实现，追加默认开启兆芯兼容选项和v3双架构诊断包；不重复请求许可。
- 任务in_progress，分支diagnostic/performance，起点42745199；开始时git status --ignore-submodules=all干净。普通status的HiMsg子模块元数据权限问题不改子模块处理。
- 不commit/push、不启动真实/隐藏HWND/GUI/Computer Use。原兆芯v2目录只读，重分析只用本工作区副本。
- 本机Windows ARM64；VS由vswhere定位，原生MSBuild记录于Build/Draw3V3Validation/environment.json。所有build同invocation规范化PATH、关闭node reuse，使用现有solution和固定依赖，超时2700秒。

## 构建环境与实际结果
- 首次完整Debug|ARM64编译完成，链接LNK2001/LNK2019/LNK1120：工作区Arm64依赖缺少Abseil头文件和全部88个absl库；非本次源码编译错误。原checkout已有20250512.1、正确ARM64静态triplet；两侧vcpkg.json和triplet SHA256一致。复用这些本地依赖到工作区，未改依赖版本、工程或toolchain。
- Win64依赖也复用相同manifest/triplet的原checkout预编译目录；所有复制目标均在本工作区VcpkgInstalled。
- Debug|ARM64最终完整solution exit0；日志/结果Build/Draw3V3Validation/debug-arm64-final.*。vcpkg applocal首选pwsh.exe不可用后按既有targets回退系统PowerShell，MSBuild exit0；无需改工程。
- Release|ARM64完整solution exit0、1317.20秒；7早期CLI全exit0。正式ARM64 ZIP已生成，PE/capability/manifest/5脚本/ZIP7entry hashes全部通过，记录Build/Draw3V3Validation/packages.json。Release|x64完整solution exit0、1033.39秒，7早期CLI全exit0。ARM64/x64正式ZIP均已生成，所有PE/capability/manifest/5脚本与ZIP7entry hashes通过，最终packages.json含双架构及ZIP SHA256。

## 已通过检查
- i18n sync/check：en-US/zh-TW各366/366；原始文件UTF-8 BOM状态/CRLF及git diff --check通过。
- Windows PowerShell5.1和pwsh完整离线suite各267项通过；drain专项各5项，最终pwsh drain+标题专项7项通过。结果位于Build/Draw3PerformanceV3Tests，日志Build/Draw3-v3-*-test.log。
- 真实兆芯v2数据复制到Build/Draw3V3Validation/real-v2-zhaoxin，manifest SHA256与原始一致；当前v3分析器重新分析exit0，72有效循环/34主绘制帧/37成功呈现、0完整性问题，capture_schema2保留。未改用户原始目录。
- Debug ARM64早期CLI：--draw3-performance-test、--draw3-performance-launch-test、--config-boundary-test、--draw3-device-policy-test、--draw3-resource-probe-test、--draw3-resource-probe-list、--draw3-resource-probe-upload-test全部exit0、无超时。
- 上传测试使用真实生产资源/上传/绘制方法和无HWND WARP：dynamic DISCARD、NO_OVERWRITE追加及DEFAULT三模式，独立合成/失败不改head，读回字节/offset、工具交织/回卷/200k拆批、多模式图像一致与非空，连续红绿两笔无中间等待的像素断言。
- 新CLI门禁：普通/带引号/大写返回0，额外参数返回2，均在GUI初始化前退出；结果argv-gate.json。
- check代理静态审查无剩余确认代码缺陷；新增CLI argv、GPU drain完整性、v3版本文案三项问题已修。

## 矩阵复测与现场限制
- Headless --no-window已exit0（116.34秒，PASS animation correctness）。首次56项矩阵在编译负载下47 complete/1 partial/8 timeout，48身份验证；新增16paired用例全部complete/identity。超时/partial原样排除，不补比值。后续Release ARM64无编译负载矩阵56/56 complete且identity verified，29278ms；真实Analyze-ResourceMatrix函数分析status complete、0issues、8/8 comparable，未伪造产品launch/Host文件。记录offscreen-release-arm64-quiet-final/{validation-summary,matrix-analysis}.json。x64 build/7项CLI/package全部通过；最终双架构文件见Build/Draw3PerformancePackages/v3-20261015。
- 本机矩阵仅验证协议和负载身份；本机并非兆芯，开发期负载下的数字不能作为性能结论。
- 真实RTS/Present/4K流畅度及兆芯优化普通/兼容优劣由用户按README两次真实书写A/B验收，高通补充v2/v3。

## 最终交付
- ARM64: Build/Draw3PerformancePackages/v3-20261015/Draw3Performance-v3-arm64-20261015-141304-7726cf26b49b4081b23dd858224aae39.zip，29312971字节，SHA256 38a351012f8358260e5a0990db0fefb477287d1176538f7e89eec9b8d7c93490。
- x64: Build/Draw3PerformancePackages/v3-20261015/Draw3Performance-v3-x64-20261015-143055-45963d123e554a10bef51fe3a85374fa.zip，29813217字节，SHA256 be154eb705ced92f75bef6bf28c5a4b094328ae891840f230fd9338218b48cd8。
- 包内各7文件：EXE、manifest、5个用户文件；manifest包/probe revision3、capture schema2、case_count56。
- 本轮交付验收A1..A5已满足；目标兆芯/高通真实产品RTS/Present/4K测试待用户回传，不能从开发机无窗口结果承诺流畅度。
- 无commit/push、无GUI/HWND；所有原始失败及环境处理保留在Build验证目录。Trellis归档/日志将显式--no-commit。
- Trellis上下文清单校验通过；3个既有spec超过32KiB自动注入上限，相关完整章节已显式读取/审查，不改注入配置或扩大重构。
