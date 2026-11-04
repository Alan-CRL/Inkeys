# 分段诊断版人工测试交接（2026-11-01）

## 开始

1. 正常退出所有正在运行的 Inkeys（包括其他工作区的旧版），无需关闭或修改原 PPT。
2. 双击 `D:\Project\Inkeys\Repo\Inkeys\Scripts\Start-PptNavigationDiagnostic.cmd`。入口固定启动本工作区 `Build/ARM64/Debug/Inkeys.exe`，校验诊断标记与内嵌 DLL；已有实例会提示，不会强制关闭。
3. 保留采集窗口。自行打开 `D:\Personal\Downloads\【10】羧酸 酯基.pptx` 并放映。

## 操作

- 先用控件在第3/4页来回5–10轮，重点观察第4→3页 Previous；每次等上一操作完成再点，不按住连发。
- 再用键盘同样来回5–10轮。第3页有两步点击触发的入场动画，页内动画与真正换页应分开观察。
- 若出现明显卡顿，记下是控件还是键盘、Next/Previous、约几时几分几秒；不必截屏或操控诊断工具。
- 完成后正常退出 Inkeys，入口会自动收集日志并显示目录。不要先关闭采集窗口；无需修改/保存PPT。
- 回复测试完成和卡顿操作/近似时间即可，AI读取 `Build/Validation/ppt-navigation-<本轮时间>-<唯一标识>/` 的日志分析。若应用持续阻塞无法正常退出，先告知当前操作与时间，不必强制结束Office。

## 分析目标与限制

分清 native成功入队后的驻留、入队前锁等待/其它开销、COM进出、managed精确邮箱驻留、owner执行、观察/ROT等维护重叠及focus。缺记录或歧义显示unknown；时长为代码诊断边界，不是屏幕光学翻页时间。本版只加诊断，翻页/超时/焦点/COM所有权/ready行为不变。

## 产物

- 新EXE内嵌PptCOM SHA256：52465C7A0779F6E8CBF4AB72E083ED69DB5F9382893C02021BBCAC663A921526。
- TLB仍为C652B5DD0603576033B2E0D15869BE103F23FFDCAA2A1984E53F3B6A14DBA606。
- 仅启动子进程时启用INKEYS_PPT_PERF/INKEYS_PPT_TIMING，不修改用户配置或永久环境。
- 一键入口使用现有执行策略允许的PowerShell宿主；没有改变/绕过系统策略。
