# 启动链只读调查（2026-11-01）

## 证据
- 本地远程跟踪引用 origin/inkeys2-final 的 智绘教/IdtMain.cpp:173–247：目录字符串替换/截断作为互斥名称；仅 IDT_RELEASE；Restart/WarnTry/CrashTry/SuperTopComplete 豁免。并非当前产品应直接照搬的完整协议。
- 当前 Inkeys/IdtMain.cpp:1721–1795：先解析启动参数和 helper，Release-only 创建 launchMutex；Restart/CrashTry 已走门，WarnTry 与 SuperTopComplete 仍绕过。当前目录替换/截断不是无碰撞规范化，长路径/大小写/别名需在实施设计中验证。
- 当前 IdtMain.cpp:1862–2090：installer/update.json 验证后替换安装根 EXE，ShellExecuteW 启动目标，返回成功即退出；回滚也启动旧目标。不能把这个返回值当作目标已取得实例资格。
- IdtMain.cpp:2092–2237：根目录发现 installer/update.json 后，先原子发布指令，再 ShellExecute installer EXE，成功即退出。安装根→installer→安装根属于同一逻辑交接链，不能只按每个中间 EXE 的父目录各算一份实例。
- IdtMain.cpp:2241–2394：SuperTop决定、尝试signal、调用LaunchSurperTop、失败继续原实例。
- Inkeys/SuperTop/IdtSuperTop.cpp:307–325、329–400：helper 创建带 SuperTopComplete 的最终进程；Ready/Accepted握手与精确父进程等待。成功父进程exit(0)，失败保留旧进程。
- Inkeys/Inkeys/Helper/ShutdownSupervisor.cpp:515–585：已有验真旧 HANDLE/父镜像的监督链；旧进程真正signaled后才StartSameExecutable一次，普通-Restart/恢复-CrashTry。不要退化为固定等待或参数豁免。
- IdtMain.cpp:2446：启动Mini读取发生在SuperTop之后；ReadAll在2897附近，晚于现有互斥门。新开关若只接ReadAll，早期门永远看不到真实配置。
- IdtMain.cpp:3875–3879：正常结尾CloseHandle launchMutex；早退/exit(0)同样必须由进程退出或局部RAII保证句柄收束，不依赖执行到主循环尾部。
- Setting.Pages.cpp:902–908：GeneralSection::Basic 内已有安全分类/教学安全Combo；GeneralDraft/GeneralEvents在Setting.Pages.h:238起，Setting.cpp负责保存。用现有ToggleRow与三语言生成链。

## 主要风险与设计约束
1. 只移门到SuperTop后：可以让最终实例持锁，但可能让普通重复启动提前执行更新和提权副作用。需要启动前置准入与合法交接的整体处理。
2. 保留最终进程参数豁免：最终进程无目录占位，后续普通启动仍可并行。必须接管保护而非一直绕过。
3. 新旧暂时重叠：存在对象不等于“其他独立实例”；只有验真的内部交接可以等待/转移占位。任意-Restart不构成授权。
4. 允许模式仍需登记活实例；否则开关关闭后启动的进程无法识别先前允许模式下的实例。
5. 不能将更新目标所在安装根与临时installer目录混为两个无关产品；必须保留一条逻辑目录身份，并复用已有路径/哈希校验。
6. 参数分流/隔离CLI应保持在配置/占位/GUI之前；测试不得写真实用户配置、触发UAC或打开主窗。

未修改产品代码，未运行构建/产品入口；以上为源码直接证据，不代表实机交接已通过。
