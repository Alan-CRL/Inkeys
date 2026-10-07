# 用户动作到可观察结果回归表

当前为规划映射；具体符号和状态 owner 由生产调用链调查补齐。自动化状态均为未验证；需要 GUI/真实笔输入的结果是需要人工，不以 no-window 测试代替。

| ID | 用户动作/场景 | 相关模块候选 | 关键状态变化 | 可观察结果与门禁 |
| --- | --- | --- | --- | --- |
| U01 | 主栏展开/收起、重复点击、快速反向 | UI3 Bar、FramePacing、RenderLoop | target、animation、committed frame | 动画真实完成时长、成功帧间隔、无跳变/残影 |
| U02 | 主栏拖动、吸附、跨屏 | Bar Interaction/Layout、Display | capture、位置、DPI、dock target | 轨迹平顺、最终位置/命中一致 |
| U03 | 属性栏开关、颜色、粗细、Fine Dial | Bar 属性/交互、IdtState | pen/color target 与 applied | UI 高亮和真实笔宽/颜色一致 |
| U04 | 设置开关、主题、光影 | Setting、RenderPipeline、Bar Lighting | 配置、资源 epoch、dirty | 效果/主题一致，不阻塞其他动画 |
| U05 | 选择、软/硬笔、橡皮、激光、开放形状切换 | Bar/快捷键、IdtState、Draw3 bridge | mode、tool、cursor、window hit-test | UI/光标/穿透/真实输入一致，迟到结果不覆盖新选择 |
| U06 | 连续书写、慢速、快速折返、停住再动 | RTS、ContactInput、Modeler、Host/Renderer | sample/queue、L0/L1/L2 | 压感/转角连续，Down 到首成功 Present 延迟 |
| U07 | 抬笔、取消、多接触 | RTS、ContactInput、History | Up/Cancel、contact generation、提交 | 无残影、尾端稳定、无跨 contact 污染 |
| U08 | 固定/速度橡皮、激光 | Draw3 tool、renderer、history | tool 参数、dirty、commit | 轨迹/擦除范围/历史正确 |
| U09 | 撤销、重做、清屏 | Draw3 document/history、UInk | revision、history、persistence | 屏幕与重启后数据一致，Clear 可回撤 |
| U10 | PPT 进入/翻页/退出 | PptCOM、IdtPlug-in、Draw3、PageControl | session/slide identity、input gate | 当前页可写且不串页，控件确认后输入开放 |
| U11 | PPT 最后一页/结束放映页 | PptCOM、Draw3 storage、PageControl | slide slot/end-page identity | 两者持久化身份分离，退出后仍正确 |
| U12 | resize、多屏 DPI、刷新率变化 | WindowService、Display、UI3/Draw3 presenter | scale、surface/device epoch | 绘制/命中/dirty 与像素对齐，无黑屏或错误 fallback |
| U13 | 睡眠恢复、device-lost、呈现失败 | RenderPipeline、Draw3 presenter | retry、epoch、committed frame | 合法退避后恢复，不提交未成功呈现帧 |
| U14 | 正常退出、保存、磁盘满/无权限 | IdtMain、Draw3 UInk/index | accepted save、durable commit | 上次有效状态不被半写覆盖，错误可见 |
| U15 | 用户主动重启 | IdtMain、CrashHandler/Helper | offSignal、旧/新实例 | 一次重启、参数/单实例正确，不误走崩溃链 |
| U16 | 受支持异常自动重启、启动即崩溃 | CrashHandler、IdtMain | CrashTry、loop guard | 有限重试、无多实例、正常退出不拉起 |
| U17 | 崩溃后已提交数据恢复 | Draw3 UInk/index、startup | workspace/document/page identity | 最后有效恢复点可读；未持久化数据不承诺零丢失 |
| U18 | 定格、放大镜、设置 owner/排除列表 | Whiteboard/Setting/WindowService | owner、visibility、capture | 当前功能 gate 和窗口层级正确 |
| U19 | 长时间 idle 与首次唤醒 | UI3 scheduler、FramePacing、Draw3 host | wake、clock、dirty | idle 不忙等，首帧无额外停顿/跳变 |
| U20 | 白板等关闭入口 | feature gate、共享内部逻辑 | Unsupported/NotReady | 发布入口仍关闭，不因测试打开 |

构建和 headless 可验证的子合同由各子任务在验证记录中对应 ID；真 GUI/Office/笔输入路径需明确设备、版本、配置与人工步骤。
