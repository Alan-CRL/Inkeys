# 第七轮独立审查：原生设置窗口测试证据

2026-10-11，trellis-check 对 `.trellis/tasks/10-10-setting-home-design` 的本轮原生验收作只读核对。已读取保存的 hook 上下文、check.jsonl、PRD/design/implement、最新品牌入场/纹理修复合同及相关规范；以最新用户“可以启动窗口测试、不要 Computer Use”授权为准。本代理未启动 GUI、发送输入或调用 Computer Use；仅新增本审查文件，不改产品源码及他人的文件，不 commit/push，任务继续 in_progress。

## Findings (fixed)

未发现需要修复的本轮产品源码或验收记录问题，没有源码修改。主会话已准确注明紧凑菜单截图的命名与实际展开状态差异，并补充日志中的 UI3Diag warning，未将其误记为零警告。

## Findings (not fixed)

没有未修复的已确认缺陷。以下为本轮证据的明确范围，不是据此推断出的产品问题：

- 可独立复算的是 ledger 的13次正常业务 open；首次英文正常打开早于编排脚本，存在独立稳定截图及主会话观察记录，因此“共观察14次”须始终携带13次有ledger、1次独立观察的限定，不能表述为14次脚本自动断言。
- 三份日志只有 epoch=1，deviceLost和recovery计数为零。本轮没有复现设备移除，更没有对真正失效后的共享设备重建作现场验收；截图所示最初DeviceRemoved触发源仍未知。上一轮真实WARP参数失败和removed/reset HRESULT注入不能代替真实TDR/原生恢复或Win7实机。
- 实际native尺寸为DPI192下client 1440×1040、1920×1400、2882×1778，即720×520、960×700、1441×889 DIP；最大化不是精确1440×900。七倍率/420布局是既有无窗口测试证据，本轮没有原生全矩阵重复测试。
- GDI截图存在采集延迟，不能凭文件名证明530ms书写、300msHold、60FPS或性能指标；小窗308ms截图已经呈现静态主页是尺寸门禁的观感证据，与上一轮生产布局/时钟断言共同使用。
- 英文/简体/繁体日志分别有94/5/7条UI3Diag warning，均为已有调度耗时诊断；不能称日志无warning，也不能用包含截图的这次运行宣布稳定帧率达标或推断新的产品性能缺陷。

## Evidence review

- 正常业务入口与源码吻合：Bar.Button.cpp的Setting点击回调调用Setting::Toggle，More释放仲裁执行该回调；Setting.cpp的Show设置业务visible、入队ShowWindow并请求Settings渲染。SessionState::SetVisible(false→true)递增ShowSerial。native-review.py通过真实鼠标按下/释放选择More→Setting，而close通过WM_CLOSE→Hide；裸ShowWindow只用于最小化恢复，未冒充新的正常Show。
- 脚本在动作前验证记录中的可执行路径、进程存活、桌面解锁、HWND所属PID和输入位置遮挡。只临时提高测试HWND的Z序并在finally恢复；退出走设置底部正常业务，没有产品私有入口、后台线程、强杀或用户配置修改。
- native-actions.jsonl计数独立重算为click5/close11/open13/wheel1/resize4/minimize1/restore2/exit3/maximize1。repeat-close/open-1..6为连续六组；每次close记录visible=false、open记录visible=true及有效截图，三次exit窗口为null。session-en/cn/tw对应PID42384/48648/14956及同一隔离路径，三份日志末行均为“已结束智绘教所有线程并关闭程序”；核查时对应PID均不存在。
- 严格UTF-8读取三份完整日志：idt1791718348069.log为289966字节、idt1791719107389.log为32093字节、idt1791719159523.log为36534字节。均无[error]、设备移除/OOM/非零deviceLost或失败恢复诊断，观测epoch集合均为{1}；不是读取终端乱码推断日志损坏。
- 独立重算SHA256：Build/ARM64/Debug/Inkeys.exe与app/Inkeys.exe均为30E494E218A54FEDBE14E496DE01A6897A43764BA22CC78653AAACBFD9DD80A8；原Build/ARM64/Debug/opt/deploy.json为13810DFE66395A9E1F83C73DDB8145164AD72B0E7C5FDEB4AB588593DF0DCD4E，与baseline一致。所有ledger截图存在。
- 实际查看六种语言/主题稳定截图，欢迎语、彩虹字标及y下伸、统一标语、开源说明与按钮、GPL链接和双英文署名完整，未观察到横向溢出或文字重叠。另查真实书写中间帧、滚动后重开顶部、小窗静态页、紧凑导航覆盖菜单和最大化截图，均支持记录限定的结论。小窗下内容继续纵向滚动，不将窗口外内容称为裁切缺陷。
- 主会话research/native-runtime-validation.md、task.json及native-results.json与以上证据一致，任务仍in_progress且初始设备移除原因unknown、Win7未运行。rendering-and-ui.md“全页视觉验收授权边界”的小段更新准确区分最新授权、真实ShowSerial业务入口、GetMessagePos与PostMessage差异、PrintWindow空白及设备恢复证据等级；没有更改产品接口或平台承诺，无额外模板更新需求。

## Verification

- Lint/格式：本次git -c diff.ignoreSubmodules=all diff --check Exit0；普通命令在受限runner中读取未修改HiMsg子模块遭权限拒绝，忽略子模块仅用于核查当前主仓diff，没有修改Git配置或源码。本审查文件UTF-8无BOM、CRLF、正常末行。任务数据与ignored测试artifact的JSON/JSONL可解析，计数/校验值一致。
- TypeCheck/编译：本轮未修改源码，不重复构建同一二进制。复核上一轮最终完整InkeysRepo.sln Debug|ARM64结果Exit0、27.2498582秒，已使用动态定位ARM64原生MSBuild及同PowerShell invocation PATH workaround。
- Tests：本轮主会话原生正常路径smoke证据核查PASS，未复现崩溃；上一轮完整--no-window结果Exit0、172.6073485秒，i18n384/384检查保持。审查代理未冒称亲自重复GUI、故障注入或Win7测试。
