# 选择与穿透操作模式

## 目标与授权
用户已确认完整计划并明确要求实施。一个整体任务，最小 diff；不启动交互 GUI/Computer Use，不 commit/push。保留源文件编码/换行，在关键逻辑添加中文注释。

## 需求
- 默认合并式：选择统一支持空画布与保留墨迹操作电脑。
- 分离式：选择清空当前页后操作电脑；穿透作为独立顶层工具保留墨迹并操作下方窗口。重复点穿透无动作。
- 穿透与定格互斥；进入穿透取消定格；穿透时点定格，恢复记住的画笔/图形/橡皮并开启定格，墨迹不变。保存具体笔型和属性，不恢复旧定格。
- 设置切换即时生效、不丢墨迹：合并式有墨迹选择→分离式穿透，反向穿透→合并式选择；其余状态不变。
- 分离式选择态显示“选择”，其他工具态显示“选择(清空)”；繁体“選擇(清空)”，英文 Select / Select (Clear)，允许英文两行局部排版。
- 定格可用：合并式定格2×2；分离式穿透上2×1+定格下2×1。选择空页时隐藏穿透，定格2×2。
- 定格不可用（PPT等）：两模式均隐藏定格；分离式可用穿透2×2；选择空页两者隐藏。白板不新增穿透。
- 清空第一下只清空、保留工具，空画布再点进入选择；命令拒绝不假装成功。空画布已选择时清空使用统一Disable灰色且不响应，其他工具空页仍可点击进入选择。
- 常规页标题下、自启区前增加说明块与下拉框。说明块在上，样式复用PPT助手信息块，按三语言测量自动高度。
- main.json UI.Bar.SelectAndClickThroughMode：0合并式、1分离式，缺失/非法默认0，不上传，通过现有异步写盘。

## 三语言精确设置说明
zh-CN:
设置项：选择与穿透
合并式：通过“选择”统一操作电脑，涵盖无墨迹和保留墨迹两种状态。
分离式：“选择（清空）”清空墨迹后操作电脑；“穿透”保留墨迹并操作下方窗口。
穿透：保留屏幕上的墨迹，让点击和触摸穿过批注层，操作下方窗口。

zh-TW:
設定項：選擇與穿透
合併式：透過「選擇」統一操作電腦，涵蓋無墨跡與保留墨跡兩種狀態。
分離式：「選擇（清空）」清空墨跡後操作電腦；「穿透」保留墨跡並操作下方視窗。
穿透：保留螢幕上的墨跡，讓點擊與觸控穿過註記層，操作下方視窗。

en-US:
Setting: Select and Click-through
Combined: Use Select to operate your computer with no ink displayed or with ink kept visible.
Separate: Use Select (Clear Ink) to clear ink and operate your computer, or Click-through to interact with underlying windows while keeping ink visible.
Click-through: Keep ink visible while allowing clicks and touch input to pass through the annotation layer to the windows underneath.

## 验收
配置默认/非法/读写；三类工具进入穿透、重复点击、与定格互斥/恢复；双向设置映射；选择Clear拒绝/接受与历史；清空两击与灰色；桌面/PPT/空页布局矩阵；三语言不截断与隐藏无命中。
自动：i18n sync/check，早期无窗口状态测试，headless --no-window，完整Debug ARM64 solution。
人工：真实点击/触摸穿透、PPT页切换/退出、设置视觉，未获GUI授权不执行，明确记录待验。
