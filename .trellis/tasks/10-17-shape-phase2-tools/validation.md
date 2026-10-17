# P2 验证与交付记录

日期：2026-10-17。分支：feature/shape。基线：已提交 P1 e59703ec。本轮未创建commit或push。

## 最终结果

P2批准范围已完成：11类别单笔构造、圆两模式、3–12边正多边形、UI3面板与SVG/轮播、独立样式/线型、预测预览、单对象历史与构造参数保存恢复。P2及五个子任务完成；总任务继续进行，P3–7和P3后的逐点工具仍为planning。

## 构建证据

- `InkeysRepo.sln Debug|ARM64`：最终 build04 实际退出码0，0 errors，耗时00:02:37.47。使用vswhere定位的Visual Studio ARM64原生MSBuild；同一次PowerShell invocation执行 `Remove-Item Env:PATH` 与 `MSBUILDDISABLENODEREUSE=1`。日志：`validation-build-04.log`。
- `inkStrokeModelerTest.sln Debug|ARM64`：实际退出码0，0 errors，耗时00:00:25.72。日志：`validation-shared-build.log`。
- 全部构建允许至少5分钟；没有超时重启。每次仅一个完整构建。
- 单次命令使用 `/p:LinkIncremental=false`，沿用P1已定位的本机增量链接器规避，不修改工程/工具链配置。最终native有170 warnings（第三方头、既有转换/metrics以及新增测试整数坐标转float等）；共享构建18 warnings（代码页/转换及LNK4075）。未为0 warning清理无关代码。
- 首轮FXC生成现有四份shader与Shape VS/PS，见`validation-build-01.log`；修复后build04再次成功编译shapePixelShader并完成ResourceCompile/Link。GPU用生产renderer加载正式嵌入资源通过，UI用正式嵌入SVG通过。
- post-link输出过`pwsh.exe`不可用信息，但构建链完成且实际退出码0；未修改仓库来绕过环境问题。

## 命令行与离屏测试

GUI子系统测试用 `Start-Process -WindowStyle Hidden -Wait -PassThru` 取得真实退出码；入口均在产品窗口启动前退出，不创建HWND。

| 测试入口 | 最终结果 | 主要覆盖 |
| --- | --- | --- |
| Inkeys `--draw3-shape-core-test` | exit0；core/Create/Document均0失败 | 全预设、四方向、两种圆、3–12边、非法/退化、Down快照、真实modeler预测、raw Up、取消、并发shape工具、新分支、Undo/Redo/Clear、桌面/PPT、冷页隐藏redo与Clear continuation |
| Inkeys `--shape-tool-state-test` | exit0；0失败 | 11类别投影、独立样式/线型、开放/闭合槽、DIP、圆与边数、revision、旧setter与兼容工具 |
| Inkeys `--draw3-shape-renderer-test` | exit0；306 cases / 0 failures | 全部预设/样式/线型、活动RenderData与最终对象/viewport replay、全画布/tile逐字节一致、箭头实线头与半透明并集、批次/Map失败、原四图形/普通笔/荧光笔/Laser像素基准 |
| Inkeys `--ui3-geometry-attribute-test` | exit0；6238 checks / 0 failures；1439次实际主栏呈现提交 | 真实BarButton动画/hover/按压/拖出/失效、参数动画、圆图标、轮播与暂停停止、3–12边、三语/DPI/上下展开、精调与调色子树的像素/命中、旧透明控件排除、记忆HL/Laser、工具/场景切换、三语全部主栏长标签逐帧适配 |
| Inkeys `--bar-eraser-offscreen-test` | exit0；0失败 | 共用UI3动画、SVG、脏区裁剪、主题/字体、现有属性面板与真实图形面板slot回归 |
| Inkeys `--state-mode-production-test` | exit0 | 跨入口/修订/槽隔离/Clear及FineDial过期物理、惯性、取消、失效上下文不提交 |
| Inkeys `--pen-width-production-test` | exit0 | 偏好/业务宽度策略及FineDial生产路径 |
| Inkeys `--draw3-whiteboard-storage-test` | exit0 | 100页冷热卸载、CPU/GPU引用释放、undo/redo、主题、过期eviction与故障保持warm页 |
| Shared `--shape-core-only` | exit0；0失败 | 共享构造/几何/约束/命中/控制点/样式 |
| Shared `--uink-file-only` | exit0；0失败 | 236构造对象 / 240标准部件，v1/v2、标准降级、损坏/未知元数据、文件与分页 |
| Shared `--uink-presentation-only` | exit0 | PPT页文件与恢复 |
| Shared `--desktop-autosave-only` | exit0 | 桌面自动保存、快照与恢复 |
| InkeysHeadlessTests `--no-window` | exit0；PASS animation correctness | 既有共享动画与面板布局，Eraser216布局/0失败 |
| `Scripts/i18n.ps1 check` | exit0 | en-US、zh-TW均393/393（100%） |

native最终stdout/stderr保存在本目录`validation-<入口>-final.*.log`，共享/Headless用对应入口日志。UI详细报告和PNG为`Build/shape-p2-ui/results.log`、`panel-light.png`、`panel-dark.png`、`scene-fine.png`、`scene-color.png`、`scene-pen-restored.png`及两个英文长标签PNG。Eraser详细日志为`Build/eraser-b/offscreen-results.log`。已用本地图片查看器检查生成像素，未启动交互式GUI。

## 实际发现与修复

1. 首轮/第二轮编译发现GDI+ FillMode同名、宏/类型、formatter声明及回调owner限定问题，均按实际源码修复。build03首次完整通过；最终build04包含后续测试发现的修复。
2. 产品`PublishProductState`的属性合并白名单漏传新shapeTool/shapeStyle，实际状态投影测试发现并补齐，不是仅修改测试。
3. 通用PS使用插值TEXCOORD造成非轴对齐几何tile舍入；改为SV_Position像素中心。最终306例严格逐字节通过，未放宽tile容差。
4. Clear continuation测试误把整页恢复redo深度当作1；按生产语义修正，继续要求根隐藏redo为1，并实际逐笔重做比较全部构造参数与稳定ID。
5. 借用子树需显式禁用隐藏控件（透明度0仍可能命中）；旧geometry控件也统一排除。切回Pen必须在清借用标志前关闭借用drawAttribute。
6. 长英文主栏名称在原70 DIP槽中被裁切；仅Geometry按当前/目标文字统一测量字号，透明中点前收小；旧Eraser测试同步读取新面板真实slot，不沿用被替换面板的固定50 DIP假设。
7. Shape借用宽度按Brush量程/语义，异步保存，逐次重验workspace/page/generation/PPT/tool上下文；公共光照取同帧Shape颜色。原笔型记忆不改写。

## 格式、审查与交付边界

- 父任务及五个子任务context manifest验证全部exit0；超大长期spec的自动注入会截断，本轮按相关章节手动读取，未扩大注入配置。会话107用`add_session.py --no-commit`记录，当前实施任务已清除。
- Trellis implement/check覆盖core/state/Draw和UI，已修复本轮发现的明确问题；无待修复代码阻断。
- 代码定版及任务、日志收尾后的全部已跟踪改动文本通过UTF8/BOM/换行与HEAD逐一比较；新CPP保持同层BOM/CRLF；27份新增SVG XML有效；`git diff --ignore-submodules=all --check`通过，任务文档收尾后亦通过；暂存区为空。
- `.vcxproj/.filters`只登记两个新增UI源文件，沿用现有solution、FXC、资源嵌入链与依赖，不新增构建系统。
- 工作区既有P1验证日志保留。本轮构建生成的四个共享demo未跟踪cso已按确切路径清理；正式资源产物位于既有忽略目录。
- 未运行Computer Use，未启动产品/设置/浏览器/调试窗口。未执行commit/push。
- Win7/Win10真机、其他架构真实设备、实体笔/触屏手感与显示器组合待人工验收；当前Windows11 ARM64离屏结果不替代上述验收。
- SelectionOverlay、浮动编辑栏及多阶段提示窗口属于P3；逐点折线/任意多边形在P3之后独立实施，当前未添加入口。
