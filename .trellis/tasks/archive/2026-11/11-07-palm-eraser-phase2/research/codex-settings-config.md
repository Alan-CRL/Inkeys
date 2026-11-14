基线已核对：`bugfix/hand-erase@babdecf6`。全程只读，未构建、测试或启动 GUI。命令执行被环境拒绝，`get_context.py --mode packages` 未运行，改为读取规范目录及相关文档。

**① 代码事实**

以下设置文件均在 `Inkeys/Inkeys/UI/Setting/`。

- 页面采用 ImGui＋D3D11。绘制页内有 Overview／SpeedEraser／PalmEraser 子页，各自保留滚动；橡皮分组当前依次是笔速、手掌入口。全局样式应插在入口之前；手掌新增项位于详情渲染函数。[Setting.Pages.h:217](D:/Project/Inkeys/Repo/Inkeys/Inkeys/Inkeys/UI/Setting/Setting.Pages.h:217)、[Setting.cpp:3938](D:/Project/Inkeys/Repo/Inkeys/Inkeys/Inkeys/UI/Setting/Setting.cpp:3938)、[Setting.Pages.cpp:837](D:/Project/Inkeys/Repo/Inkeys/Inkeys/Inkeys/UI/Setting/Setting.Pages.cpp:837)。
- 现有手掌页含总开关、识别窗口、阈值滑块＋文字重置、实尺矩形预览、七工具和并行开关；尚无擦除样式、粗细模式或尺寸倍率。[Setting.Pages.cpp:852](D:/Project/Inkeys/Repo/Inkeys/Inkeys/Inkeys/UI/Setting/Setting.Pages.cpp:852)。
- 可复用 `BeginCard`、`InlineCombo`、`SliderResetAction`、`ConditionalBlock`。`ComboPreviewRow`仅提供下拉左侧单个44DIP预览，不是三图＋下方下拉。无普通说明时，`note` 会降为普通说明并取消 i；动态提示需显式布局。[Setting.Controls.h:63](D:/Project/Inkeys/Repo/Inkeys/Inkeys/Inkeys/UI/Setting/Setting.Controls.h:63)、[Setting.Controls.cpp:1156](D:/Project/Inkeys/Repo/Inkeys/Inkeys/Inkeys/UI/Setting/Setting.Controls.cpp:1156)、[Setting.Controls.cpp:2260](D:/Project/Inkeys/Repo/Inkeys/Inkeys/Inkeys/UI/Setting/Setting.Controls.cpp:2260)。
- 行按真实字体测高、DIP布局后经 `Pixels` 转换，使用主题 token；条件退出禁交互。现有阈值预览直接使用画布像素，不能乘设置缩放。[Setting.Controls.cpp:1165](D:/Project/Inkeys/Repo/Inkeys/Inkeys/Inkeys/UI/Setting/Setting.Controls.cpp:1165)、[Setting.Pages.cpp:916](D:/Project/Inkeys/Repo/Inkeys/Inkeys/Inkeys/UI/Setting/Setting.Pages.cpp:916)。
- 配置位于 `main.json`，橡皮/手掌字段均为 `IdtAtomic`、NoUpload；基础直径默认32DIP，自动粗细默认开。X宏贯通声明、遍历、默认和复制；All/Mini共用读取，Mini只恢复所选路径。通用浮点codec不校验有限性或范围；枚举需仿现有字段严验原始整数及宽度。改键不会自动迁移；普通橡皮另有旧固定偏好初始化兼容。[Other.Config.cppm:469](D:/Project/Inkeys/Repo/Inkeys/Inkeys/Inkeys/Other/Other.Config.cppm:469)、[Other.Config.cpp:209](D:/Project/Inkeys/Repo/Inkeys/Inkeys/Inkeys/Other/Other.Config.cpp:209)、[Other.Config.cpp:642](D:/Project/Inkeys/Repo/Inkeys/Inkeys/Inkeys/Other/Other.Config.cpp:642)、[IdtState.cpp:1223](D:/Project/Inkeys/Repo/Inkeys/Inkeys/IdtState.cpp:1223)。
- 普通橡皮：偏好mutex→`ProductState.eraserInputs`→Host→Window配置mutex→接触快照。手掌：偏好mutex→setter冻结保存并发布Host→Host mutex→绘制owner应用Controller；页面勿重复排队或跨线程改Controller。[IdtState.cpp:603](D:/Project/Inkeys/Repo/Inkeys/Inkeys/IdtState.cpp:603)、[IdtState.cpp:1305](D:/Project/Inkeys/Repo/Inkeys/Inkeys/IdtState.cpp:1305)、[Draw3.Host.cpp:1741](D:/Project/Inkeys/Repo/Inkeys/Inkeys/Inkeys/Drawing/Draw3/Draw3.Host.cpp:1741)、[Draw3.WindowControl.cpp:728](D:/Project/Inkeys/Repo/Inkeys/Inkeys/Inkeys/Drawing/Draw3/Draw3.WindowControl.cpp:728)。
- 主栏现为尺寸圆组｜清空｜自动粗细＋手掌；手掌按钮10仅翻转同一总开关，齿轮仍进笔速页。[Bar.EraserAttributeLayout.cppm:108](D:/Project/Inkeys/Repo/Inkeys/Inkeys/Inkeys/UI/Bar/Bar.EraserAttributeLayout.cppm:108)、[Bar.EraserAttribute.cpp:503](D:/Project/Inkeys/Repo/Inkeys/Inkeys/Inkeys/UI/Bar/Bar.EraserAttribute.cpp:503)。
- i18n以 `zh-CN.jsonc` 为基准，`Scripts/i18n.ps1 sync`生成其他语言结构、snapshot和`IdtI18nKeys.g.h`，补译后check；须覆盖目录全部20语，不能手改生成头。`--no-window --setting-eraser-only`已遍历20语、七种缩放、浅深和360/900宽，输入编辑集中于1及1.875倍；并非GUI/Win7证明。[i18n.ps1:13](D:/Project/Inkeys/Repo/Inkeys/Scripts/i18n.ps1:13)、[setting_design_tests.cpp:2832](D:/Project/Inkeys/Repo/Inkeys/InkeysHeadlessTests/setting_design_tests.cpp:2832)、[同文件:8010](D:/Project/Inkeys/Repo/Inkeys/InkeysHeadlessTests/setting_design_tests.cpp:8010)。

**② 推断与风险**

高置信度：最小改动需覆盖页面draft/events、Config校验、偏好快照及Draw3锁存结构；无需增加RTS同步回调工作或新系统API。

现有“倍率”是识别阈值：`W×H`与阈值比较；擦除直径另为`√(W×H)`。[Draw3.PalmEraser.cpp:116](D:/Project/Inkeys/Repo/Inkeys/Inkeys/Inkeys/Drawing/Draw3/Draw3.PalmEraser.cpp:116)。建议明确区分“识别面积阈值倍率”和“擦除尺寸倍率”，避免混用。

**③ 产品决定**

- 样式仅改光标，还是同步改变擦除几何？前者可能所见与擦除范围不符，后者扩大实现范围。
- 矩形长宽比、各形状推荐算法；切换形状时重置推荐值或记住各自选择，分别偏简洁或保留偏好。
- 尺寸倍率作用于长度还是面积、范围与重置值；非动态模式隐藏或禁用大小项，分别偏紧凑或易发现。
- 手掌笔速模式服从全局自动粗细开关，还是独立启用？决定关闭总开关后的实际行为。