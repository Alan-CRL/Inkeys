# Setting字体生命周期实现边界

## 修改
- Setting.cpp：显式FreeType atlas；正文使用生产FreeTypeTextFontConfig，图标保留原stb源配置。
- 分离BindSettingFonts以便失败恢复；ReplaceSettingFonts在非帧期间建立并注册有OwnerContext的候选atlas，构建默认字体并上传初始图集，然后可选提交PreparedLanguage。
- 失败恢复IO、5个字体角色、ImFluent/default/current font与DPI；成功清除旧图集backend纹理、PlatformIO引用、注册和元数据。保持同一ImGui context和窗口状态。
- 同字族语言直接发布、复用atlas；跨字族更换。选择项先排队，下一RenderSettingFrame处理，成功才写配置并推进font serial。
- 队列Information/ConfirmRestart/WorkspaceTransitionFailureNotice在入队时冻结公共文案+LANGID；worker使用owned快照。正文由当前渲染帧生成，同线程帧边界commit不会插入其中。
- Setting.cppm新增harness导出RunFontLifecycleProductionTest。

## 真实生产路径测试
测试创建WARP device/render target，无HWND；实际调用RebuildSettingFonts、ReplaceSettingFonts和DX11 backend，循环11语言3轮、绘制动态字形、验证context/window StateStorage保留与单atlas注册。构造stale revision使已经完成初始upload的候选拒绝发布，核对旧图集/语言保持并能继续render。

目前源码已完成，待编译和运行；不宣称通过。主agent协调CLI注册，dynamic_fonts独占构建/测试。

## 明确限制
Build/初始纹理上传不表示未来每个字形已烘焙。首次新正文frame继续走标准dynamic glyph requests；若后续upload失败，沿既有backend HRESULT与Retry/DeviceLost，不Present部分帧，但不回滚已发布locale。该边界尚需review确认与对应故障注入验证，不能宣称全页面字形失败都有旧语言回滚。本轮未预热所有679key和所有角色。

没有GUI/commit/push。文件BOM与换行已按git cat-file --filters HEAD还原（两个模块文件无BOM、CRLF）。
