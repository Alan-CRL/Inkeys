# Trellis 检查记录

## 已检查
- 已读取完整 native hook 文件及任务 check/PRD/design/implement 上下文，结合当前 diff 和实施研究检查真实生产入口。
- 配置两字段为原子 int、NoUpload，严格 JSON 整数类型/范围读取，缺失或非法回0/1；ReadAll/ReadMini、冻结副本和重启测试覆盖，未改变用户A1/A2序列。
- Settings 卡片位置正确，仅实际改变时写配置、UpdateRendering(false)及QueueConfigWrite；三语最长选项测宽，局部popup内边距8×4 DIP。生产FIFO复用而非新增链路。
- RenderLoop 完整目标宽度计算已前移到方向判定前；稳定网格/固定40 DIP主按钮半宽/10 DIP间隔/目标描边/同帧实际zoom与monitor origin决定包络。没有使用父栏动画宽度、pulse主按钮宽度或Back超调作为输入。
- 空间策略仅current不fit且opposite完全fit换边，两侧不fit保留；重复相同输入缓存结果。首次placement、PPT/白板交接及居中方向锁不被抢占，PositionUpdate空间模式不按中轴覆盖，primaryBar上下逻辑保持原合同。
- 四排序只映射X；生产正序列表、1×1/2×1/2×2/Divider各分支均通过同一X函数，Y不镜像；隐藏锚点复制已映射target，hit仍由现有显隐过滤。order-only变化纳入布局批次，width不变也从当前值重新目标。
- finite DTO字段/sizeof72不变；bit8..9/bit10生产者与frame覆盖一致，合法mask接受全部8组合，bit7未知仍拒绝；冻结输入比较新增配置位防旧结果套新布局。
- 基线动画批次/继承/damage/hit机制复用；排序发生在换边早期仍继承统一keyframe，晚期按既有CanJoin规则重新批次。

## Findings (fixed)
统一完整构建首次退出1，暴露新增fixture的6处C2248及新测试局部变量遮蔽warning；本轮已直接自修，未改产品接口。
- Bar.EraserAttribute.Test.cpp：移除对protected bottomDockMode/bottomDockRecoveryActive的直接读写，改用既有公开InitializeStartupPlacement准备浮动状态，并检查IsBottomDockLayoutLocked；测试末恢复placement/fold及原几何、方向。早期CLI无交互/渲染/恢复阶段，未扩大产品字段权限或新增接口。
- config_metadata_tests.cpp：只将新增循环局部value改为preferenceValue，消除C4456遮蔽，不处理其他既有warning。

## Findings (not fixed)
无已确认源码问题。真实GUI拖动、边缘换边与设置视觉不在授权自动验证范围内，保留人工验收，不由纯策略/离屏冒充完整RenderLoop或HWND证据。

## Verification
- Lint/static diff：git diff --check --ignore-submodules=all通过。
- TypeCheck/完整C++编译：由主会话统一执行Debug ARM64 solution，首次失败原因已记录并修复，等待主会话复建结果；不并行独立构建。
- Tests：静态审查新增配置、方向矩阵/重复帧/负坐标/缩放/精确像素边界与真实PositionUpdate/按钮位置动画回归；Headless和Bar离屏由主会话统一执行，报告时待结果。
- 设置回放及i18n：实施记录已有12组无HWND控件回放与357/357 sync/check通过；未重复回放。

离屏入口直接执行共享策略、生产PositionUpdate与真实BarUiValue动画/hidden anchor，并未执行整个私有RenderLoop。Presentation harness涉及HWND且受编译门禁，本轮未启用或运行。没有GUI、Computer Use、commit、push或任务结束操作。

## 第二次构建机械修复
完整solution第二次退出1，首个有意义错误为Setting.cpp新增块使用未声明裸config。已限定为Inkeys::config；原生产Setting当前无using namespace Inkeys，全块其余Widgets、barUISet、QueueConfigWrite、字体/尺度等均已有相邻代码真实scope。此前控件回放错误地定义了全局config，掩盖namespace接线问题；生成器fixture现在只在namespace Inkeys中声明配置，所有回放计数访问也使用限定名，并断言抽取的生产块采用限定名。主会话需重新生成/编译/执行12组回放及solution复建，初次回放不能作为此次限定名版本验证。

## 主会话最终统一验证
两轮编译反馈的fixture访问与设置限定名均修复后，第三次完整InkeysRepo.sln Debug ARM64退出0（0 errors）。修正命名范围的设置回放重新生成/编译/运行12组退出0。InkeysHeadlessTests.exe --no-window退出0，Config metadata failures=0；Inkeys.exe --bar-eraser-offscreen-test退出0，EraserVisual/PageControlScene failures=0。详细日志、既有pwsh.exe applocal环境提示及人工边界见../validation.md。任务保持进行中，未commit/push/finish/archive。
