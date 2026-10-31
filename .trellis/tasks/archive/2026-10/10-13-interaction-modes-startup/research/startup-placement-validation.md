# 启动位置预设：验证记录

2026-10-13，当前任务仍 in_progress；本批未 commit、push 或启动GUI。

## 结果
- 完整 InkeysRepo.sln Debug|ARM64：ARM64原生MSBuild，经vswhere发现；同一PowerShell调用规范化PATH、禁用node reuse。build-startup-presets-verified.log，无error，进程exit0；Inkeys.exe与InkeysHeadlessTests.exe均更新。
- InkeysHeadlessTests.exe --no-window：headless-startup-presets.log，exit0，PASS animation correctness。测试入口实际调用RunStartupPreviewStateTests，包含真实配置、几何、D2D/WARP像素及原有进度/交接用例。
- i18n.ps1 check：en-US、zh-TW各340/340，PASS；zh-CN为基准，生成头与snapshot由sync同步。
- 生产赋值提取回放：原生ARM64 cl编译与initial-state.exe exit0。覆盖0/1/2及非法值、Floating/Free/fold、主栏及所有子控件折叠透明度、Win32宏边界。
- 新预设及完整侧栏实际ImGui块回放：原生ARM64编译，presets.exe exit0；三语言×0.6/1/1.5/2缩放共12例，选择1→2→0写盘计数各3、重复选择不写，裁切/控件重叠/侧栏重叠均0。原36 DIP导航行无需更改。
- git diff --check通过；既有27个修改文件的UTF-8/BOM及CRLF属性均保持，没有换行/编码整文件差异。

## 配置、几何与缓存
- main.json真实读写覆盖字段缺失、0/1/2、-1/3和字符串非法值；ReadMini/ReadAll归一化一致，本次冻结选择不随ReadAll更改。
- 冻结写盘payload采用生产同样的默认构造再复制赋值；修改活动配置后落盘仍保存冻结值，体现下次启动生效。
- 4档DPI×4档zoom×左右角共32例，负坐标显示器与任务栏工作区；侧/底等24 DIP。主体80 DIP、描边包络81 DIP，窗口舍入独立于主体大小。
- 预览与正式UI使用同一主显示器有效DPI策略和纯几何resolver。缓存只保存展开名义目标宽度，角落Diagnostics宽度另为80 DIP。

## 离屏像素
- 实际D2D/WARP使用生产共享绘制原语、A8 opacity mask与CPU readback，不创建HWND。
- 中心/角落外形×1/1.5625/2/2.25缩放，验证透明角、填充透明度、n=3轮廓、移动反光两端离屏/中途可见/不越mask。
- 环形0/50/100%真实进度、顶部顺时针、圆头、32 DIP外径与3 DIP描边、正常蓝与fatal红（包括0%仅红轨道，不制造假进度）。
- 40份BMP位于忽略目录Build/StartupPreviewVisualQA。主会话与独立reviewer已查看实际中心50%、角落50%、角落fatal和shimmer，符合预期；BMP查看器黑底为查看呈现，透明角以像素alpha断言验证。

## 边界与环境
- no-window模式明确跳过真实窗口测试；初始化回放的控件属性/按钮列表为替身，页面回放使用默认字体与写盘计数桩。不能据此声明真实HWND/ULW committed首帧、嵌入字体GUI、真实混合DPI显示器或Win7现场验收。
- 首轮及后续源码编译错误均已定位修正；未修改项目配置、SDK、工具链或依赖版本。
- 沿用已知沙箱PDB/临时文件访问限制，在自动审批通过后于沙箱外执行既有构建与Headless。构建保留已有C4244/C4267等warning；postbuild有pwsh.exe PATH提示，solution仍exit0，i18n已独立执行验证。
- Trellis独立审查覆盖源码/规格及配置→Mini→Preview/Bar→首帧→缓存→FIFO数据链；最终结论另由check代理提供。
