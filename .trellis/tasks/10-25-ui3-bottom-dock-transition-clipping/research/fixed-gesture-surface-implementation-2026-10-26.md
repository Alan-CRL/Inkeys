# 固定手势表面实现与验收

本轮固定手势表面已实现，软件回归及相同 36 秒 V 字路线的人工验收通过。用户现授权收尾并提交本轮改动；任务保持 in_progress，不执行 finish/archive，不推送。验证结论仅适用于本报告记录的显示环境和路线。

## 实现

- Interaction 从 Down 成功位置及同一有效输入差计算连续 native translation，吸附/脱离不重设窗口原点基准。屏幕可见性限位与 mode 无关；透明 HWND 可越屏。
- render owner 在 Down 完整 GetDC/ULW/ReleaseDC/EndDraw 成功后冻结同一 session/display/zoom 段的 viewport、capacity origin/size 与 source。预留包含当前成功形状、预测退场内容、两种提示文案和 Back/光晕/AA、捕获带与实际弹簧范围；失败候选不累计扩张。
- 底端、居中远端继续追向稳定世界目标，按同帧 translation 换算 local，弹簧保存相对目标的误差。顶边仍只随有效输入移动，横向成功抓手与远端联合播种。
- Docked/Centered 不先用 SetWindowPos 搬旧位图；由 ULW 同帧提交位置和对应像素，普通过期输入也不能替换这张位图的 frame translation。纯 Floating 保留既有直接平移及屏障保护。
- Up/取消包含零 translation 的情况，先选择 Docked/Centered 规范 root 或 Floating 吸收位置，再从最后完整成功形状交接，首张恢复不积分；完整提交后退出冻结并沿既有 viewport 收拢。动画关闭则直接到目标。
- 未预见的包络溢出不在 held 扩窗；通过已有 pointer ordinal 精确取消该手势后重新归位，避免延迟原生取消消息误伤下一手势。bitmap/source 包含保护仍保留，未引入新的绘制后端、线程、配置或构建系统。

## 开发中捕获的缺陷

1. Windows `far` 宏与新增局部变量重名，首轮构建报 C2513；改用 `farError`，后续错误均为连锁，不改工具链。
2. 静止桌面 Down 没有外观变化时，旧 idle demand 会跳过准备提交。真实 Seek + RenderLoop 测试先红，生产 demand 边界加入尚未成功冻结的 Down 请求后转绿，保留严格的 Down 成功断言。
3. 白板再次 Down 的底边抓点在屏幕钳制段少移动约 1.12px；独立数值核对恰好是合法可见性 clamp。测试原期望使用 raw pointer，现按 Down 抓点/未形变半高/monitor 边界独立计算 effective pointer；未放宽容差。
4. 包络不足路径的普通 WM_CANCELMODE 会在送达时取得新 ticket；现改为 session 校验后的原有 pointer ordinal 请求，旧请求不覆盖较新取消。
5. 全量 headless 首跑默认 sandbox TEMP 的配置原子写失败。独立同进程 Win32 对照证实 Create/Write/Flush/Close 成功，MoveFileExW(REPLACE_EXISTING|WRITE_THROUGH) 返回 ERROR_ACCESS_DENIED；workspace 专用目录全成功。证据见 `TestResults/ui3-fixed-gesture-2026-10-26/atomic-probe/9cb7a050-c9ad-4487-9add-52037a5cb9d3/result.json`。通过同一 invocation 设置 TEMP/TMP 后复跑，不改配置写入源码。

## 软件验证

- 当前完整 `InkeysRepo.sln Debug|ARM64`：ARM64 原生 MSBuild，exit 0；保留既有第三方类型转换 warning。
- `--ui3-bottom-dock-transition-test`：exit 0，19 个组合、836 条记录、744 个成功 held 帧；Down 准备、两轴进入/退出、失败重试、再次 Down、Up 均有覆盖。同一固定段 source/size/target 恒定，未钳制段 native 位移符合独立输入差，仍有 partial 提交。
- 同一路径保留顶边和转换连续性断言；动画关闭允许直接恢复目标高度。不能把关闭动画时的瞬时高度恢复记作动画开启的连续性误差。
- `--ui3-workspace-centering-test`：exit 0，Desktop/PPT/Whiteboard、快速切换、左右展开、不同缩放、负原点及 Freeze 退场均通过。
- `--ui3-input-worker-test`：最终候选 exit 0，包含新增取消代次断言及原有 mailbox/continuation/捕获回归。
- `InkeysHeadlessTests --no-window`：workspace TEMP 复跑 exit 0，最终 `PASS animation correctness`；第一次默认 TEMP 失败报告单独保留，不计为通过。
- 二进制、日志、CSV、几何摘要及本地测试脚本均在 `TestResults/ui3-fixed-gesture-2026-10-26/`。

## 实机验收状态

用户准备好后已完成本次实机测试：PID11884，EXE SHA256 `372B2F328A605BCADCFBECADA74B213809E47BD21DF2A7C62E156212C3C9261B`。启动原生窗口 `[876,1676,2004,1874]`，DPI192，2880×1920屏幕，工作区 `[0,48,2880,1856]`；命中Logo位置已核对。

正式 V 路线 36001.1ms，12轮完整完成，7899次输入移动，17791个原生外框采样，无录屏/抓屏/Computer Use。正式held段17348个样本全部为1608×504，整个路线没有尺寸增长；真实HWND.bottom范围1890..2050，确实允许透明窗口越屏。普通连续移动最大单次记录22px，两轴一致，不再出现原有模式造成的80/40px阶跃；此几何结果本身不代替视觉判断。

用户明确反馈：**“未见位置跳变，顶边与果冻正常”**。这次候选在当前同屏同DPI的相同复现路线通过人工验收。完成后仅向自启动PID11884的 `Inkeys DrawpadWindow` 发送WM_CLOSE，进程正常退出码6，随后bar_count=0。当前按用户新授权提交本轮改动，任务保持进行中，不推送。

证据：`TestResults/ui3-fixed-gesture-2026-10-26/live-v-01.csv`、`live-v-01-summary.json`、`manifest.json`；不在产品热路径加入诊断。

当前同一原症状路线的软件合同与人工观察均通过；不据此认定Windows内部根因。不同显示器/DPI和自动隐藏任务栏等扩展场景未作实机泛化验收，性能仅确认固定表面/保留partial及无尺寸增长，不宣称已完成吞吐基准对比。

## 提交前收尾

复核最终全部改动：7个产品/测试文件SHA256均与已验证 manifest 一致，UTF-8无BOM及CRLF保持；构建和4个软件测试入口的exit记录均为0，用户36秒V人工结果已记录。最终检查仅补齐提交授权、历史阶段标记及任务继续状态，未改已验证代码，未重复启动GUI或测试。

工作提交包含本次修复、回归、V字重放工具、规范及3份相关研究记录；未识别的设置窗口任务目录不进入提交。完整测试日志与二进制保留在本地TestResults，不纳入Git。
