# 工作树与暂存核查（2026-10-14）

当前工作树 Inkeys-draw：fluent，HEAD c747068265a7fb64bea853676f45c1a04a6a51ce（perf(settings): preload and optimize home entrance animation）。之前 f77bd6f3 字标入场/DX11纹理修复及 a3b22667 主页品牌/反光已在历史中。fluent reflog 确认连续三次提交。

原 Inkeys 工作树：feature/mode，HEAD 2dd3b344。索引为空；未提交项为工具栏按钮/排列、IdtPlug-in、相关回归和工具栏 Trellis 资料。针对 Setting 目录、setting_home_warp_tests.cpp 及主页旧任务的 diff/status（包括未跟踪文件）为空，没有遗漏的本任务源码需复制。

共9个stash，逐个 git diff stash^1 stash 对相关目录核查；仅 stash@{0}（bugfix/vibration）包含旧Setting.cpp中的HapticsConsoleOutput选项和卡片高度调整，不属于fluent开屏，未应用。stash@{6}（feature/settings）只有PptCOM.dll；stash@{7}（feature/animation）是Bar.Main.cpp与颜色样式任务资料。全部stash保留；未pop/apply/delete，也未动原工作树。

当前初始唯一产品dirty为Inkeys/PptCOM.dll。SHA256 AB34C31F9510BE311439B798A35D6980D5DA609D55622C8DFAD339E97733C105，构建前备份在Inkeys/Cache/setting-entrance-cache-20261014/PptCOM.before.dll。构建后的生成物不替代这份用户既有修改。

git status普通模式遇HiMsg子模块git元数据权限错误，改用--ignore-submodules=all；读取原工作树需要runner沙箱只读提升。以上是环境访问限制，不是遗失文件的证据。