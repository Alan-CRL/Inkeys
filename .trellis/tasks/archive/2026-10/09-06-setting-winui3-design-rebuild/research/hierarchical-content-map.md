# 通用四子页内容与核心接线（2026-10-08）

当前依据：`approved-hierarchical-nav.md`。本记录只覆盖内容、内部目的地接入和国际化；侧栏、浮层、动画几何及完整构建由对应实现与主会话验证。不启动 GUI，不 commit/push。

## 逐项内容与行为映射

| 子页 | 原内容 | 字段或行为 | 保存及实时作用 |
| --- | --- | --- | --- |
| 基本 | 开机自动启动 | `StartUp` / `setlist.startUp` | 原启动项 FIFO 命令，旧配置保存 |
| 基本 | 创建桌面快捷方式 | 原 CreateShortcut 业务命令 | 原路径、本地化名称、FIFO 保持 |
| 基本 | 快捷方式更多选项 | Plugins / `tabPlug3` | 进入既有快捷方式详情，不被总览重置 |
| 基本 | 启动加载动画 | `StartupPreviewPreferenceState` / `UI3.StartupPreview.Enable` | 既有偏好与 pending 写入，不参与新增导航动画策略 |
| 外观与动画 | 启用动画 | `UI3.Animation.Enable` | 原 Bar 即时同步及新配置保存；新增导航动画同帧状态归位 |
| 外观与动画 | 动画速率 | `UI3.Animation.SpeedRate` | 0.1–5.0x、一位小数、即时同步、释放保存；关闭总开关时隐藏且不覆盖保存值 |
| 外观与动画 | 边缘光影 | `UI3.EdgeLighting.Enable` | 原 Bar 实时接口、新配置保存；携带现有 Dynamic 值 |
| 外观与动画 | 动态边缘光影 | `UI3.EdgeLighting.Dynamic` | 总开关打开才显示；原实时同步及保存 |
| 外观与动画 | 主栏缩放 | `UI.Bar.Zoom` | 0.50–2.00、两位小数、即时预览、释放保存 |
| 外观与动画 | 设置界面缩放 | `setlist.settingGlobalScale` | 1.00–2.00、原字体重建请求、HWND 最小尺寸消息及旧配置保存 |
| 交互与安全 | 置顶间隔及详细说明 | `TopSleepTime` / `setlist.topSleepTime` | 原 0..6 选项索引、保存与 `topWindowNow` |
| 交互与安全 | 右键退出 | `RightClickClose` / 同名旧字段 | 原 toggle 保存，原完整说明 |
| 交互与安全 | 教学安全 | `RegularSetting.TeachingSafetyMode` | 原 0..3 索引、保存及 CrashHandler 同步 |
| 交互与安全 | 原快捷键说明 | `SettingsUI.HotKey.N/E` | 原完整 Notice 文案；不增加编辑器或业务操作 |
| 存储与配置 | 自动清理配置 | `Config.AutoClean` | 原新配置写入与旧配置保存 |
| 存储与配置 | 画布自动保存 | `setlist.saveSetting.enable` | 原 Draw3 同步及旧配置保存 |

Storage 由原配置保存 switch 分支完整承载，只更换页头；`RenderGeneral(Storage)` 不重复提交控件或标题。General 的草稿仍从同一常驻状态构造，Basic/Appearance/Interaction 的副作用分别限定于当前子页，未显示子页不比较并写回旧缓存。所有异步业务继续使用既有 FIFO。

## 目的地、保存与显示

- 保留 PageId 数值及 6/7 编号空洞。旧 Configuration(3) 映射 General/Storage，旧 HotKeys(10) 映射 General/Interaction；General(4) 对应 Basic 默认入口。
- 主页面通用入口明确进入 Basic；侧栏父行使用 NavigationState 最近子页。插件详情及 Build/Version 内部路由沿用原业务分支。
- 通用四页以 GeneralSection 数值构建独立的内容子窗口 ID；插件继续以内部详情编号区分状态。翻译后的标题不参与 ID。
- 导航离页时提交尚未完成释放保存的主栏缩放/动画速率；设置缩放复用原提交动作。常规释放提交先完成时，离页提交不会再产生写入。
- 现有渲染线程每帧推进导航动画一次；未绘制、移动、隐藏、遮挡及失败路径在该线程设置私有 idle 标记，恢复绘制首帧只跳过一次 delta，连续可见低帧率仍推进动画。内容只绘制当前页面。共享 `ApplyContentAlpha` 对当前正文子窗口及其已提交子窗口的顶点 alpha 乘一次淡入系数，覆盖自绘文字、图标和 Fluent 控件，不修改 RGB、主题常量或控件值。
- 禁用动画时当帧状态归位，正文 alpha 为 1；导航在绘制前重新解析归位几何，因此同帧使用新宽度。正文已按此前几何完成一次布局，其宽度于下一帧使用归位几何，不二次执行设置控件。
- 正文及输入屏蔽跟随覆盖导航、浮层显示/关闭过渡和鼠标释放屏障；Escape 先交给浮层，再处理窄窗覆盖导航。

## 国际化与当前验证

- 新增四个 Design.General* 标签，原 Regular.N 改为通用；简中、繁中、英文均提供完整翻译。
- 更新启用动画与速率说明，明确 UI3 主栏及设置导航/正文过渡范围；保留其他原文字键。
- 使用原 i18n 脚本的 `Write-KeyHeader` 生成头文件；简中快照只同步同样的局部修改，不触发其他翻译改写。
- `Scripts/i18n.ps1 check`：en-US、zh-TW 均 370/370（100%），PASS。
- 内容/核心/三语/生成头/快照的 `git diff --check`：PASS。
- BOM、CRLF 与原文件保持；主会话已确认绘制、插件、组件、软件版本和实验选项 case 内容逐行与基线一致。
- 完整 Solution 编译及真实控件连续帧无窗口回归由主会话统一执行，结果须以其最终验证记录为准。GUI、实机鼠标观感和截图验收仍待另行明确授权。
