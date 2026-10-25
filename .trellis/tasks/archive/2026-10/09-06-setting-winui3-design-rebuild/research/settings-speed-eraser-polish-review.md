# 笔速橡皮与主页联系方式精修检查（2026-10-25）

本轮仅修改 Setting.Pages.cpp、Setting.cpp 和 setting_design_tests.cpp；旧演示动画、资源和i18n工作区内容保持原样。完整需求见 settings-speed-eraser-polish.md，本记录覆盖旧文档中的“面积辅助始终可见/无标题”要求。

- 精细控制使用本帧总开关与触摸/笔尖/笔尾选择。总开关关闭或无可显示项时整个分类不占位；触摸面积辅助只跟随触摸项。隐藏值不被改写，不产生额外配置事件；灵敏度和设备选择仍可预设。
- 返回箭头后复用既有 PageHeader 与本地化笔速橡皮标题，无副标题或版本。导航/滚动路径未改。
- 绘制分类为绘图设备、橡皮擦、光标、触摸、笔触觉。静态逐段比较确认所有分类内容/回调与移动前一致。
- 作者卡 QQ 标签去掉冒号，邮箱增加 Email。两行使用 Body 与8 DIP行内间距，复制ID/准确值、QQ群和地区Discussions行为保留。

独立只读审查无待修正问题。现有真实控件测试扩展到八种相关设备组合及两种总开关状态，验证同帧显隐、整个分类不占位、取消/恢复触摸和总开关时保留偏好。复用原Win7枚举/返回/滚动与主页剪贴板mock及布局回归，不为静态文字和顺序新增镜像测试。

原生ARM64完整 InkeysRepo.sln Debug|ARM64 构建退出0，0错误/46条既有警告，耗时61.85秒。InkeysHeadlessTests.exe --no-window --setting-home-only 退出0；现有设置/主页多语言、DPI、浅深、控件和离屏渲染回归通过。i18n检查英文/繁中均518/518；无新翻译键。

BOM/CRLF及git diff --check通过；17项预存改动/资源保护哈希与本轮开始一致，未改真实笔速算法。完整日志与源码/产物哈希见 settings-speed-eraser-polish-verification.json。未启动GUI或进行实机触控/Win7验收，未commit/push，父任务保持in_progress。
