# 方案确认与规范/代码证据

## 最新用户指令（优先级最高）

2026-10-08 用户先要求规划并暂停实现，规划完成后明确“批准开始实现”，见 authorized-implementation.md。此前暂停和撤回是历史记录，当前已获实施授权。

## 已确认需求

默认标题＋多行说明卡片；右侧控件占用过宽时采用标题/操作在上、说明下方通栏；窗口宽度改变时动态切换，并有切换动画。控件数量仅是例子，关键是说明可读预算。

## 官方依据

- 微软应用设置设计指南：SettingsCard 用于单个设置，标题/可选说明/可选图标/操作组成卡片；SettingsExpander 主要收纳子设置。
  https://learn.microsoft.com/en-us/windows/apps/design/app-settings/guidelines-for-app-settings
- Toolkit SettingsCard 默认模板：PART_HeaderPresenter 和 PART_DescriptionPresenter 均使用 TextWrapping=Wrap；Description 的 TextBlock 风格也允许换行。没有单行强制要求。
  https://github.com/CommunityToolkit/Windows/blob/main/components/SettingsControls/src/SettingsCard/SettingsCard.xaml
- 普通 Expander：空间有限时按需显示相关次要内容，不要求每个多行说明都折叠。
  https://learn.microsoft.com/en-us/windows/apps/develop/ui/controls/expander

## 当前代码基线（已撤回试做）

- Setting.Pages.cpp 的 top-interval 使用短摘要，另调用 top-interval-details 显示完整 TopWindow.E；可改为同一卡片说明，不改七个选项或事件。
- Setting.Controls.cpp 的 SettingRow 已测真实标题和说明高度，以一次 action 回调绘制操作。
- Setting.Design.h 的 MeasureRow 按当前 action 宽度计算预算，不足约160 DIP时采用所有文案在上/操作在下的 stacked；目前尚无独立标题/操作上行与通栏说明模式。
- 当前 NavigationTween 已提供从当前值反向续接和现有速率语义；设置可见呈现成功后返回 Continue，隐藏/恢复已有 idle 时钟重基准。
- RunSettingDesignTests 已有实际字体、Controls/DrawData、连续导航帧和倍率覆盖，应扩展已有入口，不建立第二套测试框架。

动态通栏断点/时长是项目设计取舍，不能声称为微软固定规范。研究发生在当前规划指令之前；当前按最新授权推进实现。
