# 第三次实机反馈与无 Inkeys 对照

## 日志确认

输入：8cf35729-b59e-4b59-935e-8ec43fdf296d/已粘贴的文本.txt，模块 230cb63b-1639-4077-bf63-257114a882e5。

- 2490–2495 行：2031 到达后及时进入 service finally/FullCleanup，最终关闭逻辑确实生效。
- 2496–2497 行：Unadvise 成功，真实耗时 550908/10000000 = 0.0550908 秒。
- 2498 行：用户给定 qpc=99486560809，开始释放 CP RCW 43。
- 2499 行：qpc=99821147088，CP ReleaseComObject 返回 0，真实耗时 334575715/10000000 = 33.4575715 秒。
- 之后 presentation 与 application 的释放仅微秒级，GC 约毫秒级，原 binding 的汇总 528/528，outstanding=0。末尾空轮询累计 558/558。

这是原释放调用内部等待，不是没有进入清理、Unadvise 失败、GC 延迟或可由额外 Release 修正的计数遗漏。日志不能进一步区分底层代理/CLR context 切换/Office 端析构等待，不能据此直接修改释放顺序或增加后台释放线程。

## 用户补充对照（关键）

1. 同机 Inkeys2 20261019a 也出现延迟。
2. 从打开到关闭完全不启动任何 Inkeys，也出现延迟退出。
3. Inkeys 始终运行时，即使 PowerPoint 旧后台进程还在，也可以打开新的 PPT 文件。
4. 后续用户已确认：Inkeys保持运行时联动要等旧进程退出；重启Inkeys则旧进程仍在也可立即联动。第四份日志直接捕获旧Unadvise阻塞55.58秒，详见 fourth-user-log-and-rebinding-block.md。

因此必须撤回“该退出延迟已确定是 Inkeys3/0eb1c7c5 特有回归”的因果归属。该提交删除过 GC/final 策略仍是历史事实，但不是当前现象的充分解释。

同时不能把后台驻留等同于 Office 卡死或 COM 泄漏：新文稿可打开。也不能从一个机器的结果断言仅此一台有问题；相同 Office/系统/扩展组合可能共同触发。

## 只读环境记录

- Windows 26H2，build 26300.9974，OS ARM64。
- Office 安装记录：O365ProPlusRetail（另有 OneNoteFreeRetail），16.0.20527.20150；POWERPNT.EXE 文件版本一致。
- Click-to-Run Platform=x64，EXE PE machine=0x8664。该值不能单独排除混合 ARM64EC 模块，未据此声称整套 Office 都是纯 x64 仿真。
- .NET Framework 注册值 Version=4.8.10008，Release=534296。
- 没有修改系统/Office 配置；常见 Addins 注册表位置枚举遇到读取限制，未将其误记为“无加载项”。

## 后续判别，不再盲改主程序

新文稿联动被阻塞现已确认，主要修复目标应是隔离旧绑定的同步COM清理与新绑定发现。底层等待栈仍未知，但不需要先将Office后台驻留解释成故障，才能确认当前Inkeys串行路径的进度问题。

若仍需判断 Office 退出驻留来源，可在不运行 Inkeys 的条件下对照普通模式和 Office 安全模式（powerpnt /safe），一次只改此因素。安全模式消失则再排查加载项/定制；同样存在时再看 Office版本、文件/配置或系统链路。未经这个对照，不升级/重装、不改注册表，不把背景机制强行命名为 Startup Boost（官方当前说明只足以证明 Word 支持，不支持据此推定本机 PowerPoint）。

参考：[Microsoft Office 安全模式](https://support.microsoft.com/en-us/office/lifecycle/open-office-apps-in-safe-mode-on-a-windows-pc)。

## 代码状态

本轮冻结正在测试的代码/EXE，仅分析日志和记录环境。GC 恢复、明确 RPC 断连处理与诊断可独立审查；2031 任意文稿关闭后全绑定重启仍属候选策略，尚未解决底层等待且有多文稿重连副作用，不应把它当作已验证的根因修复直接定稿。
