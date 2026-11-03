# WM_TOUCH缺失原因调查：另一块数位板

日期：2026-11-03。用户确认这份追加日志来自另一台数位板设备，提出Windows Ink可能关闭。当前没有现场测试环境，本轮只读调查应用源码、两份日志与微软/厂商官方资料，不改变输入行为、不构建、不启动GUI。

## 结论和置信边界

目前能确认的是两台设备在相同应用构建标识下暴露的消息路径不同，不能确认具体Windows Ink开关状态。未找到应用在接收前把所有Touch吞掉或漏给某个UI窗注册的确定代码路径；找到一个必须补齐的应用侧证据盲点：RegisterTouchWindow的BOOL结果完全忽略，也没有IsTouchWindow快照。

Windows Ink关闭属于合理待查项，但当前日志的GetPointerInfo/GetPointerCursorId成功且pointerType=PT_PEN，不支持直接把该设备定性为“只能输出普通鼠标”。同样，收到PT_PEN也不能反推出某厂商驱动面板的Ink复选框必定开启。

“数位板硬件 + Windows Ink开启”不是“每个笔接触必定向本应用发送WM_TOUCH”的已建立合同。两者应分别验证。上一份数位板确有TOUCHEVENTF_PEN Touch；不能反过来概括所有数位板，也不能将WM_TOUCH定义成绝不包含笔。

## 同标识包的日志对照

| 项目 | 前一块设备日志 | 本次另一块数位板日志 |
| --- | --- | --- |
| ignored归档 | tablet-ui-diagnostics-x64/other-tablet-capture-20261102.txt | touch-fix/additional-user-capture-20261103.txt |
| Draw3 build（两者L5） | Nov 2 2026T22:01:56，x64/msvc1944 | 相同 |
| ui_schema build（两者L41） | Nov 2 2026T22:02:09 | 相同 |
| RTS设置（L22-23） | tabletWindowFlags=0x1010219，MultiTouchEnabled=true | 相同 |
| SM_DIGITIZER（系统能力） | 0xc5 | 0xcd |
| 笔Pointer查询 | type=3，valid=1，cursor45 | type=3，valid=1，cursor10 |
| 实际Touch证据 | L114 raw WM_TOUCH，L115读取成功，L116/118 flags=0x5a（含0x40 PEN） | 全文件未观察到WM_TOUCH或解码记录 |
| 兼容Mouse签名 | 先前还出现72d/7ad变化 | 可见Pen Down/Up全部0xff51570a |

相同build字符串不等于已比对EXE hash；这里只排除“日志本身显示应用构建不同”的说法。系统digitizer能力是整机汇总，不能当作某个数位板的Windows Ink配置或来源归因。当前用户关于另一设备的确认已记录；不凭文件名M6猜品牌/型号。

## 应用侧路径调查

独立代码调查详见 [no-touch-application-path.md](no-touch-application-path.md)。

- 旧源码基线与当前实现都为Bar+四Page指定同一touchRegisterFuc，调用RegisterTouchWindow(hwnd,0)。Window Service在创建该HWND的owner线程执行created回调；未见某角色漏接或明显错误线程调用。
- IdtMain.cpp:3440忽略返回值，也不调用IsTouchWindow。源码有调用不等于现场成功，旧日志无法排除某环境注册失败/状态异常；不能把这一项写成已排除。
- Page四窗bindMessages=false，直达自己的WndProc；raw在Touch解码/过滤前输出。Bar在队列回调及WndProc都能记录WM_TOUCH，且旧设备日志实际证明能记录。未见HiMsg内部把所有Touch静默拦下的代码。
- 当前兼容Mouse过滤只针对Mouse消息，不会匹配WM_TOUCH本身，因此该过滤解释按钮业务断路，不能解释Touch原始消息消失。
- RTS绑定的是Drawpad HWND；TABLET相关property只在画布设置。未见全进程EnableMouseInPointer、RegisterPointerInputTarget、UnregisterTouchWindow或Ink/Touch全局关闭逻辑作用于五个UI窗。
- 边缘手势禁用不等于注销Touch；两份日志RTS/tablet flags相同，不能据此认定新设备因这些设置没有Touch。
- 日志累计8条contention掉样且Touch batch有Move限频，不能证明OS绝没发送；但大量完整笔边沿均无Touch也无法靠8条掉样充分解释为整个会话的所有Touch业务日志都偶然缺失。

## 官方资料核对

1. [Microsoft RegisterTouchWindow](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-registertouchwindow)：注册窗口为touch-capable，每个使用Touch的HWND都应注册；调用线程必须拥有窗口，失败返回0并用GetLastError查询。它没有给出“开启Ink会为每个Pen Pointer额外生成WM_TOUCH”的保证。
2. [Microsoft TOUCHINPUT](https://learn.microsoft.com/en-us/windows/win32/api/winuser/ns-winuser-touchinput)：TOUCHEVENTF_PEN=0x40，表示stylus触发。它与上一份flags0x5a相符，证明那份笔Touch真实存在；这只是可表达的输入类别，不是所有驱动都必须走该兼容通道的承诺。
3. [Microsoft pointer type](https://learn.microsoft.com/en-us/windows/win32/api/winuser/ne-winuser-tagpointer_input_type)与[WM_POINTERDOWN](https://learn.microsoft.com/en-us/windows/win32/inputmsg/wm-pointerdown)：PT_PEN=3；Pointer消息可表达笔接触，未处理时DefWindowProc可产生兼容Mouse。新日志的Pen Pointer与随后兼容Mouse符合这类消息链，不需要先假设Windows Ink关闭才能解释。
4. [HUION官方协议排查](https://support.huion.com/en/support/solutions/articles/44002618549-what-to-do-if-the-pen-pressure-function-is-not-available-on-the-application)：应用与驱动应选匹配的Windows Ink/WinTab协议，切换后重启驱动。这支持“开关会影响输入协议”这一可能性，不代表本次未知品牌数位板已经关闭Ink，也不保证打开后一定产生WM_TOUCH。
5. [Wacom官方按应用配置示例](https://support.wacom.com/hc/en-us/articles/1500006328562-A-virtual-keyboard-opens-every-time-I-use-my-pen-in-Windows-why)：Ink可按应用启用，All Other Applications与单独程序可以不同。因此将来核对时应看实际Inkeys程序的配置，不能只看一个全局复选框。本例不能被当成本次设备的具体驱动行为证明。

Wacom Native Touch概览有“WM_TOUCH不包含pen”的概括；微软TOUCHINPUT明确有PEN位，本项目旧日志也实际收到该位。这里以API定义和原始证据为准，不采用该概括来否认既有笔Touch，也不反向推导所有笔都应有Touch。

## 假设检查结果

| 假设 | 当前判断 |
| --- | --- |
| 五UI窗漏注册或固定错误线程注册 | 静态路径不支持；实际BOOL未记录，现场失败仍待排除 |
| HiMsg/业务filter先吞Touch，造成全无日志 | 未找到支持证据；原始记录在业务过滤前，Page还绕过HiMsg |
| RTS或手势配置全进程禁用五窗Touch | 未找到该作用范围；两份相关配置相同 |
| 此驱动/配置暴露原生Pen Pointer，但不走既有Touch兼容路径 | 与日志相容；具体系统/驱动内部选择机制尚不能定因 |
| Windows Ink关闭或应用配置覆盖 | 合理待查；现有日志不能读取复选框，PT_PEN证据不支持仅凭Touch缺失就认定关闭 |
| 只是普通Mouse/WinTab消息 | 不符合“只有普通Mouse”的描述：已成功查询到PT_PEN；具体WinTab并行状态未知 |

## 优先补证据的最小范围（本轮未实施）

在输入方案调整之前，先把同一输出中五个HWND的注册证据补齐：role/HWND、RegisterTouchWindow返回值、仅失败时GLE、调用线程/窗口owner线程、注册后的IsTouchWindow与flags；在首次Pen Pointer Down再次记录Touch注册快照，以区分创建成功后状态变化。Pointer来源保留GetPointerInfo成功/失败、PT_PEN/PT_TOUCH/设备句柄与源窗口；设备信息可帮助把具体数位板与整机能力分开。API诊断保持Win7可选接口边界、错误码和现有关闭时无昂贵查询规则。

| 现场证据组合 | 能推进的结论 |
| --- | --- |
| Register失败 / IsTouchWindow false | 先定位应用注册失败，不能先归驱动Ink开关 |
| 注册成功 + PT_PEN + 无Touch | 确认注册存在而该笔没有进入本应用Touch链；继续看该驱动应用配置/协议 |
| 注册成功 + 原始Touch有记录但无解码 | 定位句柄读取/输入处理，而非笼统驱动问题 |
| 同设备同包同配置，只切该程序Ink开关后Touch可重复出现/消失 | 才能将此设备的Touch缺失因果关联到该开关 |

用户目前无现场环境，不要求立刻测试；已有日志已经足以否定“所有数位板必有Touch”的假设，但不足以判定本次具体注册结果或驱动Ink状态。本轮仅记录调查，原产品代码/产物保持不变，Pointer业务接入建议暂不实施。
