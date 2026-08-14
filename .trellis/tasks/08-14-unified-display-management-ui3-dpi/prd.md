# 统一显示器管理与 UI3 动态显示适配

## Goal

建立统一、可订阅且按代发布的显示器信息模块，并让主显示器上的 UI3 PPT 控件和 Bar 在分辨率、方向或 DPI 改变后保持可见，以连续动画同步调整位置与尺寸。

## Requirements

- 将 `IdtDisplayManagement.h/.cpp` 的显示器枚举、EDID 和主屏信息迁入 `Inkeys.Display` C++20 module，删除旧全局显示器状态。
- 快照必须覆盖全部活动显示器、主屏、虚拟桌面、像素范围、工作区、方向、有效 DPI 和可判定有效性的 EDID 物理尺寸。
- 显示变化只发布完整不可变快照；枚举失败不能覆盖最后一个有效快照，首次失败必须提供显式 fallback。
- 现有 Draw2、冻结帧、设置页诊断等消费者继续获得等价主屏信息，且单次操作不得混用不同代数据。
- PPT 五个 UI3 窗口必须以当前主屏快照布局；自动越界/碰撞纠偏只存在于运行期，不回写 PPT 配置。
- Bar 必须在主屏尺寸改变后检测越界，并动画移动到最近的完全可见位置。
- PPT 与 Bar 的 DPI 变化同时动画调整位置和尺寸；过渡时长为 0.4 秒，动画中再次变化必须连续重定向。
- 显示变化发生在直接拖动期间时延后到拖动结束处理，避免窗口线程和渲染线程争用几何状态。
- 保持进程 DPI awareness、PPT COM ABI、现有 UI 显隐动画以及 Windows 7 动态 API 回退策略不变。
- 关键同步、fallback 和运行时纠偏步骤添加简短中文注释。

## Acceptance Criteria

- [ ] `Inkeys.Display` 能生成一致的全显示器快照，并在语义变化时递增 generation、通知订阅者。
- [ ] EDID 缺失或无效时保持 unknown；旋转显示器的物理宽高按当前方向提供，同时保留原始值。
- [ ] 编译路径不再依赖 `IdtDisplayManagement.h/.cpp`、`MainMonitor` 或 `DisplaysNumber`。
- [ ] PPT 在分辨率缩小、主屏原点改变、100% 与 150% DPI 往返时，五窗保持可见且尺寸/位置连续过渡。
- [ ] Bar 在屏内时保持主按钮局部中心，在越界时以动画回到最近的可见位置，并使用过渡中的缩放做命中和脏区计算。
- [ ] 自动纠偏不写入 `pptcom_configuration.json`，不改变 PPT COM 接口。
- [ ] 新增 headless 单元测试覆盖快照、EDID、PPT 重排和 Bar 显示过渡算法。
- [ ] `InkeysHeadlessTests.exe --no-window` 通过，`InkeysRepo.sln` 的 `Debug | ARM64` 完整构建通过，`git diff --check` 无问题。

## Notes

- Setting UI 不做动态 DPI；仅迁移它读取显示器信息的路径。
- 所有 UI 本任务仍固定在主显示器，并按完整显示器范围定位。
- 不实现 Draw3 多屏绘图、手掌橡皮阈值、像素/物理距离业务换算或直线拉直业务接口。
- 不覆盖现有 `Inkeys/PptCOM.dll` 修改，不提交 commit，不由代理启动可见窗口。
