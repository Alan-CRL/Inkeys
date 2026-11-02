# Caption 裁切夹具只读审查

范围：`InkeysHeadlessTests/setting_design_tests.cpp` 的 `CheckGlyphs`（约784行）与 `CheckVietnameseCaptionClipping`（约971行），并反查生产 `Setting.Controls.cpp::TextAt/SettingRow/ToggleRow/Notice` 及固定 ImGui `imgui_draw.cpp::ImFont::RenderText`。按根会话要求未编辑代码、构建、运行测试或GUI。

## 结论

未发现当前这组样本的具体误通过或误失败问题。

- `CheckGlyphs` 恢复原共同字形行框样本，新增字母独立验证真实可见字形；不会再仅因越南语重音越过名义行框就推断生产控件裁切。
- 三个样本分别来自当前实际越南语 `SuperTopE1`、`ChannelTip`、`TaskbarScaleBase/E`，目标 `Ứ/Ổ/Ở` 均在对应说明中出现一次。它们在生产 SettingRow 的 Caption/`TextSecondary` 路径绘制，查验的角色与颜色相符，不会误用标题粗体的同字形充当通过证据。
- 当前 ImGui RenderText 的顶点顺序确为左上、右上、右下、左下，UV与目标字图四角相同。`glyphScale=GetFontSize()/baked->Size` 与绘制时字号投影一致；DPI没有再次乘到顶点或clip上。
- 软件精细裁切若改动任一UV或尺寸，该唯一目标字形将不能满足完整四角／尺寸条件；完全被剔除时 `emitted=false`。GPU裁剪矩形若切入字图，引用该quad的真实命令 `ClipRect` 比较会失败。索引解析正确包含 `VtxOffset + IdxBuffer`。
- 行边界来自真实控件占位，允许重音使用既有卡片内留白；没有将名义Caption行框误作GPU剪裁区。浅深色、宽窄与100/150/187.5%调用覆盖是明确的，page-only入口确实绕过此夹具，应由根会话重编后运行普通headless。

此结论仅说明CPU实际绘制几何与提交clip检查的有效性；不将它等同于GPU像素对比或实机窗口观感通过。
