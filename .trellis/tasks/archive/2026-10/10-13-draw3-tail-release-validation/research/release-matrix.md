# 阶段四发布配置验证矩阵

2026-10-13。本代理仅负责忽略的 Build/modeler-baseline 构建/检查工具、证据、中间缓存和本报告；没有修改产品、模块测试、依赖、SDK、工具集或算法，没有运行 GUI、提交或推送。主会话负责最终 Debug/Headless、现场日志解释、人工状态、任务收尾和 commit。

## 构建与执行

每项使用动态定位的原生 ARM64 MSBuild、已有 solution、/m:1、/nr:false、/t:Build、LinkIncremental=false 和既有输出隔离 props。主 solution 为 InkeysRepo.sln；demo/test 为 inkStrokeModelerTest.sln，Win32 模块按原映射使用 Platform=x86。每次在同一 PowerShell invocation 规范化 PATH，单次构建超时3600秒；沿用 v143 14.44.35207 / SDK 10.0.26100.0、固定 vcpkg Abseil 20250512.1#0 和既有 ABI。

| 配置/目标 | 实际构建退出码 | 秒 | 无窗口结果 | EXE SHA256 |
| --- | --- | ---: | --- | --- |
| main-ARM64-Release | 0 | 220.094 | 334115项/0失败；正常颜色；无新TEMP尾日志 | `fa8e7aeb99338d3cac8334cc150855bcd35dbb41d9bbbe9b4e683078e3dcc492` |
| tests-ARM64-Release | 0 | 133.360 | 9245+128412项/0失败；完成态persistence/local integration 0失败 | `3841bf7bfb0c72d64c7a2cf75fe334ffee927d26b2b1b9046e1c0285ac898821` |
| main-x64-Release | 0 | 321.328 | 334115项/0失败；正常颜色；无新TEMP尾日志 | `d4de7578d819f508ea4851f3c7389d5e36f04ceeb794a59d5001c51e2602c4cc` |
| tests-x64-Release | 0 | 121.297 | 9245+128412项/0失败；完成态persistence/local integration 0失败 | `72ed020f44e6bf4fa7c7a4103b1aa2a8ed07ce176c2d493e0f380c27965fe905` |
| main-Win32-Release | 0 | 419.078 | 334115项/0失败；正常颜色；无新TEMP尾日志 | `48f41022de1d0f07fcc7cab6f3eec648517ebb47c0d19827c2519e608af3ac7c` |
| tests-Win32-Release | 0 | 70.094 | 9245+128412项/0失败；完成态persistence/local integration 0失败 | `bc9c6dd4df79c5f124a52649a3f812822bb17492a312e6e5dbe2490d8a46e528` |

三个Release配置的原模型报告SHA256均为 `0119fd2a0edfdefa055ac3b5e1fde5aeadb4c693bd67f7157b55c09dae02b646`，和阶段一/三基线一致。独立solution同时成功输出demo和test程序；实际运行的是测试程序的提前返回CLI，不运行demo窗口。完成态集成包含修正后的变宽/taper笔划、历史Undo/Redo、composition真值，以及UInk capture/export/file/read/import精确往返和deferred接续/Reset。

## 完整完成态成本

每个生产probe在构建完成后单独运行，预热后30轮；实际几何相对基线仍有修正，计时区外核对每轮位置/半径/时间/样式；diagnostics_io=off。这是当前完整Finalize成本，不能用同步诊断计时或反色marker证明Apply。

| 平台 | 稳定前缀点数 | 中位数 ms | P95 ms |
| --- | ---: | ---: | ---: |
| ARM64 | 0 | 0.047100 | 0.049300 |
| ARM64 | 1000 | 0.146400 | 0.149800 |
| x64 | 0 | 0.080800 | 0.087100 |
| x64 | 1000 | 0.236400 | 0.278400 |
| Win32 | 0 | 0.392200 | 0.958200 |
| Win32 | 1000 | 0.560750 | 0.696600 |

ARM64为原生执行；x64/Win32为本机Windows 11 ARM64仿真执行，特别是Win32分位数存在较大调度/仿真波动。该表不是原生x86/x64机器结论，也不是与历史程序同时成对测量的性能回归证明。阶段四没有改变求解算法；具体结果可供后续同环境复测。

## PE、shader资源及兼容边界

只读PE与资源，不执行入口：四个当前产品（ARM64 Debug/Release、x64 Release、Win32 Release）SHA256均匹配成功构建JSON；machine分别AA64/8664/14c。资源301–304均非空且以DXBC开头。

x64/Win32产品与保存的源码迁移基线相比，全部静态导入 added={}、removed={}；demo和模块test同样没有导入差异。没有新增最低平台之外的静态系统API。该检查不代表真实Windows7 SP1+KB2670838运行或D3D Debug Layer验证。

构建日志确认没有旧merge.lib链接。Abseil链接沿用各平台VcpkgInstalled/Release库；无第三方源码或ABI配置diff。原有Modeler/第三方数值转换等warning保留，未为归零warning修改无关代码。

## 中间缓存与证据

只在Build/modeler-baseline下验证绝对路径和obj目录后，清理已验收的9个Release项目obj目录，共 `6979631931` 字节（`6.500` GiB）。各项目CL/link/FXC/RC command tlog先归档到phase4-evidence/release-compile-commands；bin、EXE/PDB、旧基线、输入/输出报告、依赖和当前ARM64 Debug缓存保留。主会话此前的旧阶段缓存清理是独立记录，不能和本次重复计数。

检查工具显式删除子环境中的INKEYS_DRAW3_TAIL_TRACE。默认无日志证据比较执行前后的TEMP文件集合，仅匹配本次PID的主文件或时间戳后缀，避免历史PID日志误报和遗漏后缀。

机器证据位于忽略的Build/modeler-baseline/phase4-evidence：*-build.json/log、每配置results.json/production.log或adaptive/baseline/integration报告、compatibility/products.json及auxiliary-imports.json、release-cache-cleanup.json和release-matrix.json。完整报告和程序hash保留，生成文件不进入commit。

## 验收范围

本次完成所有剩余Release构建和可执行无窗口矩阵。Debug完整solution、Headless和诊断on/off最终结果由主会话关联；本代理没有冒称重复运行它们。用户本轮明确报告鼠标/触控板及实体笔/触摸均没有异常，之前偶发长斜线按用户要求暂不继续修复；根因未确认，不称已经修复该异常。

真实Windows7、D3D Debug Layer/GPU像素自动验证和Release设备GUI本代理未执行。正常颜色实体体验以用户实际反馈为证据；任务完成及归档由主会话根据最终范围和人工结果处理。
