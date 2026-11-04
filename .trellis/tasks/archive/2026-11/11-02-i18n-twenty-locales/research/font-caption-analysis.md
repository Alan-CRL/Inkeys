# Caption12 字形夹具分析

## 推荐结论

采用“保持原common glyph严格行框断言；新增特征字母单独coverage；真实译文在真实Caption控件测实际clip/相邻内容”的最小修正。不要调小字体、改行高、改全局基线或放宽原±0.5px断言；也不能仅以人工大写字母未使用为由删除断言后宣称真实文本安全。

## 证据

- 根会话完整headless日志：仅scale1、Caption12、四Harmony脸新行框断言失败；Regular总界[-3,13]、Bold[-4,13]，line16/padding2。其余字号/倍率没有此断言失败。
- 标准库读取实际sfnt cmap/loca/glyf：Harmony SC Regular/Bold em1000、hhea asc928/desc-285。人工样本Ắ/Ấ/Ế/Ố/Ớ/Ộ等yMax892；真实Ứ(U+1EE8)/Ở(U+1EDE)同为892，Ổ(U+1ED4)为890/889。实际大写与人工样本的轮廓上界同高，无法靠“删除未使用的高yMax字符”可靠排除问题。glyf bbox不包含LightHinting与26.6舍入，不能据此伪称已精确定位日志中的最顶字母。
- Ứ：`SettingsUI/PlugIn/SuperTop/Capability/SuperTopE1`、`SuperTopE2`用Toggle说明的Caption12；`SettingsUI/Component/SoftwareSection`为节标题。
- Ổ：`SettingsUI/Version/Update/ChannelTip`→Setting.cpp:3260 Notice说明→SettingRow Caption12。
- Ở：`SettingsUI/Regular/Appearance/TaskbarScaleBase/E`→ComboRow说明、`SettingsUI/PlugIn/DesktopDrawpadBlocker/EnableDescription`→Toggle说明、`SettingsUI/HotKey/E`→Notice说明，均Caption12。
- `Setting.Controls.cpp:201` TextAt按(lineHeight-fontSize)/2居中后逐行AddText，使用window/child现有clip，不为每个16DIP语义行单独压clip。SettingRow还有自己的padding和布局区域。因此相对语义行框顶部-1/-2px的字形overhang不等于像素实际被裁掉。是否裁切／重叠须由真实绘制位置与所属ClipRect判定。

## 最小验证落点

保留原`Agpqy（）【】中文设置繁體，。` common行框样本和原阈值；特征字母继续用FindGlyphNoFallback检查Regular/Bold可用。另在已有真实Caption说明夹具使用上述实际资源（至少SuperTopE1、ChannelTip、TaskbarScaleBase.E，覆盖Ứ/Ổ/Ở），按生产TextAt的实际位置观察draw-data顶点/字形墨迹与window或child clip，确认重音完整且不侵入前后内容。此后才判断是否需局部生产修正。

本代理只做源码/日志/sfnt读取，没有修改代码/译文、构建、运行产品测试或GUI。尝试Pillow的既有FreeType2.14.3默认hint模式只得近似值，不能重放生产LightHinting，因此未将其作为通过证据。精确字符Y0/Y1仍应由根会话现有字体夹具输出或真实Caption回归确认。

## 根会话批准后的落地

仅修改setting_design_tests.cpp：原common样本/严格行框阈值恢复，特征字母改为独立真实coverage；追加CheckVietnameseCaptionClipping，读取实际vi资源，走ToggleRow/Notice/SettingRow，观察目标Ứ/Ổ/Ở的完整UV四角、字图宽高、行区域和实际提交ImDrawCmd ClipRect。CPU裁字导致四角缺失/尺寸缩小，GPU裁字导致quad越过命令clip，均使断言失败。仅沿现有vi矩阵调用，不改变生产字体/字号/行框或标准，不新建测试入口。代码已交还根统一构建/运行，尚未在本代理执行测试。

## 后续实施

在根会话协调下已恢复原common样本严格行框断言，新增字母单独coverage，补CheckVietnameseCaptionClipping。该真实控件回归用完整UV四角/尺寸和IdxBuffer所引用ImDrawCmd的ClipRect检测CPU或GPU裁字，保留生产字号/行高/基线。独立审查记录见caption-fixture-review.md；运行结果由最终validation.md记录。
