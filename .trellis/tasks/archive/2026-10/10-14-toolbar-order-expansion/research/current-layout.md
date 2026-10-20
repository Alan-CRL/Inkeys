# 已完成只读研究

基线16d6551f，feature/mode，工作区初始干净。旧任务已archive，无活动任务。
- Bar.State.cpp::PositionUpdate浮动/非居中dock按中轴x<=w/2判左右；primaryBar按y<=h*2/5判上下。居中dock/白板专用锁不能覆盖。
- Bar.RenderLoop.cpp::SubmitTargetsAndLayout先读widgetPosition判mainBarSideSwitch（~1540），宽度计算晚于该步（~2380），需先目标宽度后新策略。
- CalculateButtonLayoutWidth以列表正序算列宽；SetButtonPositionTar mirrorX按左展镜像到layoutTotalWidth-target；viewVariant始终baseRange，不需逆序。
- mainBarLayoutWidth参与批次、换边关键帧与Back/Ease动画；只改排序width不变时需独立变化判定。
- 主栏相对主按钮间隔10 DIP，按钮网格gap5/one32.5/two70；主栏描边也需包络。只判目标完整宽，勿用pulse实际mainButton.W/AnimationAbsMaximum。
- 初始化startup placement和initialZoomFit、display monitorOrigin、BottomDock center/whiteboard placement需要保持旧合同。拖动只钳制主按钮本身，用户选择两侧不足不平移/缩放。
- 现有隐藏AnchorHiddenBarButton涵盖Pierce等，固定列表不可抽离。已有Recall灰色/3秒门禁、PPT/Whiteboard不要回归。
- Settings选择与穿透块结束后自启区前放新卡（现有60DIP card+5DIP gap）；combo宽按最长文字测。
- main.json schema在Other.Config.cppm；ReadAll/ReadMini都沿共同codec，新增严格整型读取依旧SelectAndClickThroughMode分支模式。
- 验证复用config_metadata_tests、bar_bottom_dock_tests、Bar.EraserAttribute.Test/Bar.Presentation.Test及归档任务settings replay，不新造构建系统。
- 前任务成功MSBuild参数：/p:VcpkgManifestInstall=false /p:CL_MPCount=1 /p:MultiProcMaxCount=1 /p:LinkIncremental=false /m:1 /nr:false。已装ARM64依赖核对过，非源错误的vcpkg/并行PDB/增量链接故障用命令范围参数处理，源/SDK/工程不改。

## 验证入口补充
- IdtMain在配置/单实例/产品窗口之前对--bar-eraser-offscreen-test直接返回，允许无窗口真实Bar回归。
- Bar.Presentation.Test.cpp受INKEYS_ENABLE_UI3_PRESENTATION_HARNESS门禁，默认未装fixture且涉及HWND。本轮不启用，兼容静态修改不计作动态验收；finite策略和布局用Headless/Bar离屏覆盖。
