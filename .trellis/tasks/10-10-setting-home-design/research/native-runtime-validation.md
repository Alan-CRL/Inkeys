# 第七轮主会话验收：原生设置窗口运行测试

2026-10-11，Windows 11 ARM64，继续 fluent 工作区与 10-10-setting-home-design 子任务。用户本轮明确允许启动窗口测试，并进一步要求不使用 Computer Use；后续原生测试通过命令行及已有 User32/GDI 测试脚本完成。本轮没有发现需要追加产品源码修复的新缺陷。任务保持 in_progress，不新增 commit/push，不归档。

## 测试副本与业务入口

- 使用 Build/SettingHomeRuntimeReview20261011/app 隔离副本，二进制来自上一轮已完成完整 ARM64 构建和无窗口回归的 Build/ARM64/Debug/Inkeys.exe。两者 SHA256 均为 30E494E218A54FEDBE14E496DE01A6897A43764BA22CC78653AAACBFD9DD80A8。
- 仅测试副本关闭自动更新及改变语言/主题。原 Build/ARM64/Debug/opt/deploy.json 的 SHA256 保持 13810DFE66395A9E1F83C73DDB8145164AD72B0E7C5FDEB4AB588593DF0DCD4E，测试前后相同。
- 顺序启动英文 PID42384、简体 PID48648、繁体 PID14956，路径/启动时间记录于 session-en/cn/tw.json。每次输入先验证进程可执行路径、HWND owner、桌面解锁、命中位置及遮挡；只操作本轮副本。
- 原生打开采用正常 Bar More→Setting tile→Setting::Toggle→Show，不用裸 ShowWindow(HWND) 代替业务 Show。关闭使用 WM_CLOSE 经现有 Hide 路径；最终通过设置导航底部“关闭软件”正常退出。三个实例均有正常关闭日志、窗口消失与进程已退出的核查。
- 复用父任务 research/window-review.py 的原生方法，编排脚本及逐步截图保存在 ignored Build/SettingHomeRuntimeReview20261011/native-review.py。没有新增产品调试入口、构建配置或输入机制。

## 原生测试结果

| 范围 | 实际验证 | 结果 |
|---|---|---|
| 冷启动、字体、主题 | en-US、zh-CN、zh-TW 三次独立冷启动，各切换浅/深主题，共六种组合；检查欢迎语、字标、标语、仓库说明、按钮及版权呈现 | PASS；中文无缺字，英文双署名可见，没有观察到横向溢出或重叠 |
| 正常显示与关闭重开 | 总计观察14次正常业务显示；native-actions.jsonl 记录13次 open，首次英文打开在引入编排脚本前独立捕获。包含额外连续6次关闭→正常打开 | PASS；未崩溃，关闭后再次呈现正常 |
| 页面及滚动复位 | 切到语言页后关闭重开；主页滚动到探索工具以下后关闭重开 | PASS；均回主页顶部 |
| 入场与尺寸门禁 | 正常尺寸捕获实际书写/归位中间状态及最终主页；720×520 DIP 小窗重开立即显示静态主页 | PASS；小窗没有强行播放全窗口入场 |
| 窗口生命周期 | 缩小/放大、紧凑导航及其覆盖菜单、最小化/恢复、最大化/还原 | PASS；正常呈现与原布局恢复，未出现白屏/闪退 |
| 资源及日志 | 三份运行日志严格UTF-8解码；无 [error]、deviceLost非零或Setting失败/恢复诊断；各份日志观测epoch集合均为{1} | PASS；未复现设备失效，不能据此宣称真实设备移除后的重建已验收 |
| 测试清理 | 三个测试副本正常退出，原运行配置校验值不变 | PASS |

原生测试统一DPI192：实际client 1440×1040、1920×1400，对应720×520、960×700 DIP；最大化client为2882×1778，对应1441×889 DIP，不将它记为精确1440×900。上一轮无窗口420布局/七种倍率覆盖仍是独立证据，本轮未声称原生重新跑完该矩阵。

正常尺寸截图可看到完整渐变横幅、连贯的k末端、字标下伸笔画、统一标语及开源区域；较小窗口纵向内容按原滚动布局延伸，不强求全部内容同时可见。文件 home-small-nav-collapsed.png 实际为点击汉堡后的紧凑导航覆盖菜单，名称不表示菜单已收起。

## 日志与证据边界

- 英文 idt1791718348069.log：19:32:28.076起至19:44:41.887正常退出，289966字节。
- 简体 idt1791719107389.log：19:45:07.396起至19:45:58.483正常退出，32093字节。
- 繁体 idt1791719159523.log：19:45:59.530起至19:50:34.142正常退出，36534字节。
- 三份日志仍分别存在94/5/7条UI3Diag warning，内容为longBatches/longPeriods等调度耗时诊断；未出现failure/deviceLost/recovery。未将其掩盖为零warning，也未凭这次含截图采集的运行认定稳定帧率或诊断成新的产品缺陷，性能结论仍需同机基线对照。
- 每份末行均记录“已结束智绘教所有线程并关闭程序”；native-results.json保存匹配规则汇总、实际尺寸、校验值、计数和限定结论。
- 没有主动触发真实TDR/DeviceRemoved，也没有修改显卡/系统配置。此次成功的正常打开和生命周期测试补足上一轮 native HWND/coroutine 的正常路径证据，不等于所有设备失效恢复通过。截图中最初DeviceRemoved触发源仍未知；上一轮真实WARP E_INVALIDARG与removed/reset HRESULT注入的边界见 texture-crash-validation.md。
- 原生采集GDI截图有耗时，帧文件名记录实际采集时刻，不用其推断60FPS、精确530/300ms时序或CPU/GPU性能。入场精确时钟及WARP像素/性能证据仍引用上一轮离屏测试。
- 早期 PrintWindow 对DX11得到空白，以及 PostMessage client lParam没有触发Bar，均是测试方法限制：前者不作为产品白屏证据，后者因GetMessagePos不等价真实鼠标输入。裸ShowWindow导致空HWND不作为正常Show失败；均已改用真正业务入口及无遮挡可见窗口截取。
- settings-first.png、settings-visible.png、home-light-en.png等早期被其他窗口遮挡的截图及PrintWindow白图不计入有效验收。采用后续明确owned窗口、遮挡检查和稳定捕获的证据。
- 初次Computer Use启动请求被工具拒绝，未启动应用；用户随后明确禁止Computer Use，之后没有再调用该工具。所有实际启动、输入、采集与退出由命令行原生测试完成。
- Win7 SP1+KB2670838实机未运行；兼容性仍仅为静态FL11.0合同与Windows11 WARP测试证据。

## 可复查记录

- Build/SettingHomeRuntimeReview20261011/native-results.json、native-actions.jsonl、baseline.json、session-en/cn/tw.json、app/log/*.log。
- 真实浅深主题：home-light-en-stable.png、home-dark-en.png、home-light-cn-cold.png、home-dark-cn.png、home-light-tw-cold.png、home-dark-tw.png。
- 真实生命周期：language-page.png、reopen-from-language-*.png、reopen-from-language.png、home-scrolled.png、reopen-scrolled-top.png、reopen-small-*.png、repeat-open-1..6.png、home-tw-maximized.png、home-tw-restored.png。
- research/native-window-open-notes.md：正常入口与原生输入边界。research/native-runtime-check-review.md：本轮独立核查。
- 本轮同步验收记录、任务元数据，以及rendering-and-ui.md中的最新窗口授权和正常业务Show测试边界。trellis-update-spec技能仍未安装，沿用workflow Phase3.3的手工规范同步；没有冒称调用缺失技能。未再改产品源码，故没有重复构建同一二进制。上一轮最终完整solution构建Exit0、完整 --no-window Exit0、i18n384/384检查和真实/注入WARP回归仍有效。最终执行编码/CRLF、HEAD/index及diff审计。
