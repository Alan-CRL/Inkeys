# 动态内容及动画单元盘点

基线 feature/i18n / dbabd500；不把用户操作后执行业务的if误当成动态可见性。作用域只包裹真实UI，退出阶段禁交互，业务保存仍由原事件入口单次触发。

## Pages.cpp

- PalmEraser：physicalSizeWarning 提示；正常预览/设备卡使用顶级Card；parallelWriting仅改变常驻说明，属于文本布局而非新增整条。
- SpeedEraser：physicalSizeWarning；automaticEnabled && (Touch/PenTip/PenTail) 的FineControl标题和高级项组；其中PenTip/PenTail响应各自显隐，TouchArea开关单独显隐。外组与内部条件需要共同裁剪/反向，不能重复预留高度。
- General/Appearance：动画速率依赖animation；关闭全局动画即时归位。DynamicEdgeLighting依赖edgeLighting。
- General/Shortcuts：真实hook/global状态提示在两种状态文本均为空时退出，需要保留上一份展示文字。
- Home：自绘hero、版权、feature卡片、常用设置行、作者卡、页脚。使用显式PageBlockScope补充公共PageHeader/SettingRow/Card等入口，防止只给标准设置行加动效。

## Setting.cpp 实际产品switch

- Version：四种update action布局互斥但总有一个，不应当作整页重播；new版本额外manual-update按钮动态出现/退出；architecture warning为提示组。状态文本持续改变不触发整页入场。
- Draw overview：workspace recovery、canvas recovery提示根据运行快照动态显示，退出action禁用，保留正确提示文字且不能在失效revision上提交恢复。
- PPT plugin：COM版本错误、admin警告随状态变化显隐；常驻setup提示不误当动态条目。sourceUrl属于具体子页面结构，不是用户开关触发内容。
- Debug：ShowFrameRate依赖DebugMode，退出期间只绘制禁用UI，配置同步只在真实ToggleRow事件时执行。
- 固定row/button点击后的if、控件内部选项if、插件路由switch、平台/编译条件不额外增加动态块。

## 基线验证

现有二进制 `InkeysHeadlessTests.exe --no-window --setting-general-only` 通过，exit0，墙钟14.938s。stdout/stderr已保存至忽略目录 `Build/settingui-style-motion/general-before.log`。这个时间包含既有字体校准，不等同于resize成本；只作为整体回归基线，不宣传性能收益。
