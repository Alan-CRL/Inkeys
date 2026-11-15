# Phase0→1 与通用 shader 验证（2026-10-17）

## 结果
本轮已实施并验收：共享参数化核心、旧四形兼容、生产文档/历史/UInk/白板冷历史接线、独立图形shader及内部测试。未新增UI按钮、新图形用户创建入口、SelectionOverlay或交互编辑；Phase2–7未实施。

| 验证 | 命令/入口 | 结果 |
| --- | --- | --- |
| 主完整solution | ARM64原生MSBuild InkeysRepo.sln /p:Configuration=Debug /p:Platform=ARM64 | 通过，exit0；最终链接恢复见下 |
| 资源链 | 两份shape VS/PS及原ink VS/PS、Laser两个CS | 六份成功生成；RC嵌入及产品加载成功 |
| 共享solution | inkStrokeModelerTest.sln Debug ARM64 | 通过，exit0 |
| 核心/生产文档 | Inkeys.exe --draw3-shape-core-test | 核心0 failure；生产文档0 failure；exit0 |
| 真实离屏GPU | Inkeys.exe --draw3-shape-renderer-test | 72 cases，0 failures，exit0；无HWND |
| 白板存储 | Inkeys.exe --draw3-whiteboard-storage-test | 100页、冷卸载、CPU/GPU释放、redo、主题、过期结果及故障恢复通过；exit0 |
| Headless | InkeysHeadlessTests.exe --no-window | 正常本机权限通过，exit0 |
| UInk/分页文件 | inkStrokeModelerTestTests.exe --uink-file-only | 新旧用例全部通过；page file 0 failures；exit0 |
| PPT持久化 | inkStrokeModelerTestTests.exe --uink-presentation-only | exit0 |
| 桌面自动保存 | inkStrokeModelerTestTests.exe --desktop-autosave-only | 全部通过，exit0 |
| 静态与格式 | git diff --check；26个既有文件BOM/CRLF逐文件比较 | 无格式错误，无BOM/换行改变 |
| Trellis | task.py validate Phase1 | 通过；最终上下文已改为较小的针对性spec |

## GPU证据范围
生产renderer、实际产品嵌入shader与纹理读回；36种基础几何/样式/线型组合、凹填充/半透明接缝、有序复合颜色、连续虚线相位、全画布与tile一致、50001 primitive批次边界与DISCARD回退、资源丢失/真实Map失败原子性、临时绑定清理、100:1细长椭圆描边。
在首次通用渲染前采集旧四形/普通笔/荧光笔/Laser七份非空像素基线，通用渲染及失败测试后逐字节相等。这证明新旧路径交替不污染旧工具状态；不是修改前独立发行包的视觉金样对比。原ink*.hlsl和Win7GuardedShader.h未改。

## 模型与持久化证据
独立Kind/CreationMode/Style、受约束圆变换、两向凹多边形/自交拒绝、细分误差/控制点/线型命中/修订缓存/事务逆操作。生产测试通过AppendShape创建命令追加复合逻辑项，并实际写出/解析JSON字节验证白板隐藏redo、稳定ID与精确参数。
UInk回归包括七类图形、标准几何降级、弧度旋转、未知版本诊断、损坏元数据、重复身份、线性复合引用篡改/缺件、Clear wire/逻辑编号。桌面/PPT/白板三条生产捕获复制同一Shape值，冷恢复重建CPU几何及现有history/tile缓存。

## 构建环境与修复过程
- WindowsApps python别名不能执行，改用已安装的workspace Python运行Trellis脚本，不修改系统配置。
- 初次FXC报point保留字冲突，局部改名samplePosition后六shader全部编译成功。
- 初始沙箱构建在module scan阶段未完成；正常本机权限同一solution构建成功（validation-build-03.log，41秒）。沙箱Headless的配置/原子文件临时写入失败，正常权限复测exit0，源码未为此更改。
- 最后增量链接出现MSVC LNK1000 Internal error during IncrBuildImage。未改源代码/工程/工具链，单次命令行/p:LinkIncremental=false全量链接恢复成功，exit0（validation-build-link-recovery.log）。相关LNK4075来自该次禁用增量；不是源代码错误。
- 保留现有第三方转换/编码及旧测试nodiscard警告，没有为0 warning修改无关代码。早期规范化PATH的构建还曾提示可选pwsh脚本查找失败，不能据该行否定MSBuild的真实exit0；最终产品和共享测试产物已实际运行。
- GUI子系统测试通过Start-Process -WindowStyle Hidden -Wait -PassThru显式等待并捕获日志，不以PowerShell直接启动后的空LASTEXITCODE当成功证据。所有内部入口均在正常窗口初始化前返回。

## 审查与边界
Trellis独立审查已修复：细长椭圆描边距离误差、旧矩形4DIP查询、旧表示/精确参数一致性、UUID失败、外部矩形圆角保真及动态快照内存计数；后续补充真实旧工具前后像素基线。
未测试Win7/Win10真机、x64/Win32构建、真实触控笔手感及可见GUI。未来交互编辑命令未接入运行期Undo；当前仅创建事务及纯命令基础。孔洞/自交填充、函数求解与永久几何约束留后续。
普通笔高频渲染不调用Shape求解；UI3按钮/布局及原shader不变。无commit、push或自动归档。

## 主要日志
validation-build-03.log；validation-build-link-recovery.log；validation-shared-build.log；validation-shape-core-final.*.log；validation-shape-renderer-final.*.log；validation-whiteboard-storage-final.*.log；validation-headless.*.log；validation-uink.*.log；validation-uink-presentation-only.*.log；validation-desktop-autosave-only.*.log。
