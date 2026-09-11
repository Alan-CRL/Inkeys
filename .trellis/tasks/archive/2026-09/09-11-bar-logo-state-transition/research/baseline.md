# 已确认代码证据

- 0606cbe0 的 RenderLoop 5399 附近无绘制态门控，三个 SetLogoVisual 使用 SetDirect，Dark 为 DisplayPenColor 的整笔填色；logo1 只有屏幕，Frame94 为四段实色笔，logo2 为完整浅色图。
- 4478887c 的 showLogoInk = IdtPen || IdtShape，opacity 通过 SetTar 过渡；默认 logo1 为白笔+22%白色屏幕，Frame94 包含旧笔/屏幕渐变、笔尖和高光条。新要求以该深色效果为基线。
- Bar.UI.cpp::CacheBitmap 先进行两个 RGB 槽替换，再 lunasvg::Document::loadFromData/renderToBitmap；槽忽略 alpha。Bar.Rendering.cpp::Svg 目前依据尺寸、content 和 cColor1/cColor2 判缓存失效。
- BarUiSVGClass::ResetCache 保留源 SVG，清理设备位图/缓存值；新增主按钮专属外观必须覆盖相同失效/恢复语义。
- 当前 native-desktop/bar-theme-material.md 中跨 Selection/Eraser 显示记忆色、Dark DisplayPenColor 与多 Logo 层条款已被用户本次要求覆盖，不能据此否决修正。
- 原 draw 差异已经另存 stash7872c2c0；当前 theme 工作树才是唯一编辑位置。上轮 commit 0606cbe0 已按明确请求提交，本次修正无新的 commit 授权。
