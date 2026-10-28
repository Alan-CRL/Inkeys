# Execution Plan

- [x] trellis-implement 读取全部context、PRD/design，直接实现；不递归dispatch。
- [x] 主产品StrokeGeometry用弧长长度约束替换时间比例形状，保留现有100%起点/预测保护窗口。
- [x] 增加最少临时状态保证停笔长度不反弹、Move/Reset恢复，完成态共用同一求解；插值仅修改派生几何。
- [x] 添加早退出 --draw3-pen-taper-test 生产probe，覆盖比例、弧线、稀疏/重复/相同时间戳、逐帧真实模型、L1连接/脏区、续画、多笔、Up/Stored和禁用taper。
- [x] 保存源码原编码/BOM/CRLF；git diff检查minimal diff，不改demo/第三方/构建配置。
- [x] vswhere定位ARM64原生MSBuild，执行完整 InkeysRepo.sln /p:Configuration=Debug /p:Platform=ARM64。每次MSBuild同一PowerShell invocation先Remove-Item Env:PATH -ErrorAction SilentlyContinue并设MSBUILDDISABLENODEREUSE=1，构建允许至少5分钟。
- [x] 运行 Build/ARM64/Debug/Inkeys.exe --draw3-pen-taper-test 与 Build/ARM64/Debug/InkeysHeadlessTests.exe --no-window；可补同域已有无窗口probe，检查退出码和首个有意义错误。不得启动可见GUI或现有会ShowWindow的hidden集成测试。
- [x] trellis-check全范围审查及必要自修，修改后只重跑受影响验证；把证据写research/。
- [x] 主会话trellis-update-spec记录长度/停笔/时间边界及CLI合同；复核diff/编码与验收证据，更新执行状态。
- [x] 不commit/push，不用会自动commit的archive/journal脚本；任务保持可审阅，交付自动验证结论与人工手感限制。

## Environment
Python命令使用 C:/Users/alan-/.cache/codex-runtimes/codex-primary-runtime/dependencies/python/python.exe；PATH中的WindowsApps/python.exe不可访问。现有git status需--ignore-submodules以避开HiMsg模块目录权限限制；不修改该子模块。

## Result
- 完整 solution Debug|ARM64 成功：exit 0，2:37.06；最终必要增量仍通过 solution：exit 0，7.85 秒。371 个完整构建告警及最终 7 个告警均未落在本任务修改行；最终 7 个来自固定模型库头文件。
- 生产笔锋测试 exit 0：72 组停笔模型、36 组真实续画，331063 条断言、0 失败；最长收敛 166.667ms，单次最多 19 点。无效附加参数 exit 2。
- InkeysHeadlessTests.exe --no-window exit 0：配置元数据 0 失败，PASS animation correctness。
- 首轮 sandbox 的编译 IPC/临时文件写入问题由已获准的沙箱外执行及命令级单节点/关闭共享编译解决；没有为环境问题修改项目配置。构建仍保留 pwsh/applocal 的本机环境提示。
- 独立 trellis-check 已完成，无遗留任务源码/设计问题。四份源码和两份规范的 BOM/CRLF 与原文件一致，git diff --check --ignore-submodules=all 通过，context manifests validate 通过。
- 详见 research/implementation.md、research/verification.md 和 research/check.md。自动验证范围为生产 CPU 几何、模型及共享时间/完成/Stored helpers；未运行 HWND/Host/RTS/controller owner loop、GPU 像素或实体设备手感验收。
- 任务保留待人工可见手感验收，不 commit/push、不自动归档。

## User Follow-up
用户随后明确授权提交本次本地 commit；保持任务 in_progress 等待人工测试，不结束、不归档、不 push。此前不提交的记录为实施阶段约束。
