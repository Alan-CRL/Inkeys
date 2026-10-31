# 技术方案（2026-10-19 已批准）

## 缩放合同
保留真实 DPI scale D = effectiveDpi/96；新增独立的界面基准 B：开关关闭时 B=D，开启且有有效任务栏厚度 T 时 B=T/BarMainBarHeightDip（当前 80 DIP）。全局 UI 用户倍率 U=Zoom2，主栏最终 scale=B×U；PPT 控件最终 scale=B×U×各自倍率，再由既有空间 fit 约束。

不要把 dpiZoom 的语义直接换为 B：现有画笔 DIP/物理尺寸消费者需要 D。实现时在 BarStyle/帧快照中明确区分真实 DPI 与 UI 基准，避免为了少改行数污染画笔宽度。PPT 布局中的屏幕 DIP 阈值仍使用 D，控件尺寸和命中使用 B×U。不得把全局倍率写回各自倍率或位置配置。

## 显示数据与任务栏采样
- 在现有 Inkeys.Display 中集中采样逐屏任务栏信息并发布不可变 snapshot；不在每帧 UI 渲染或鼠标 Move 中调用 Shell 查询。
- 优先系统任务栏矩形；逐屏兼容枚举仅接受可映射 monitor 的合法 Shell 任务栏窗口。顶部/底部取完整高，左右取完整宽，校验正值及厚度与屏幕短边的合理关系（自动隐藏允许完整矩形移出屏幕）。方向判定不得将竖任务栏长度误当厚度。
- 当前屏没有任务栏：采用主屏 T×D目标/D主屏。查询暂时失败：优先复用同屏同 DPI/拓扑下最近有效值；没有可信缓存时退回 D。自动隐藏保留完整矩形厚度，不能从工作区缺口推断厚度，也不能将任务栏露出/隐藏动画中间值作为新基准。
- 接入已有 WM_SETTINGCHANGE / WM_DISPLAYCHANGE / WM_DEVICECHANGE 刷新路径，并按需处理 TaskbarCreated 的 Explorer 重建。厚度变化必须进入 snapshot 差异比较和 serial，才能唤醒订阅者；避免同值通知重启动画。没有通知保证的 Shell 变化可低频限流复查，禁止逐帧轮询。
- Display 只提供显示数据/纯解析，开关与倍率归 UI/config 消费者；启动预览尚未初始化 Display 时复用同一个轻量采样/计算逻辑，之后随正式 snapshot 收敛。

## 各消费者
1. Bar 初始化和 ApplyDisplayTransition：真实 D 与 B 独立更新；最终 zoom 统一 B×U，沿现有位置保持、显示过渡、finite 调度和命中/damage 路径。不能只改 Initialize 后让 RenderLoop 覆盖回来。
2. 底栏/交互几何：以正式 UI scale 计算可见尺寸与吸附位置；屏幕 DIP 阈值保留 D。检查 Bar.BottomDock.h、Bar.Interaction.cpp 以及 Presentation.Source/Probe 的 DPI×configZoom 推导，防止实际绘制和稳定几何证明不一致。
3. StartupPreview：ReadMini 改为 Zoom2 和新开关，预览尺寸用同一 B×U。缓存宽度保持逻辑 DIP。保留原启动空间不足的 fit 策略，fit 后显示真实倍率，不能对 T/80 再施加 0.5..2 的用户倍率 clamp。
4. PPT/Ppt + PageControl：共享基准计算；四分页补入 U；结束放映按钮已位于 Bar，继续继承 Bar 缩放，不恢复旧退出窗口或兼容 exitScale。PageControl 的 LayoutMonitor 当前主屏布局保持；独立传递 D 与 UI scale，覆盖 render、drag、collision、fit、frame 比较及配置变化唤醒，保留 pair version/实际提交边界。共享 PageControl 的白板展开控件保留既有独立布局/倍率，避免因改共享 helper 无意改变白板行为。
5. Setting：在缩放区域新增开关、三语标题与说明。变更开关后更新 Bar/PPT 渲染并 QueueConfigWrite，SettingUI 自身只读原 settingGlobalScale。

## 配置与兼容
main.json：UI.Bar.Zoom2，IdtAtomic<double>，1.0，NoUpload；UI.Bar.UseTaskbarHeightAsScaleBase，IdtAtomic<bool>，true，NoUpload。键名作为本次批准方案的一部分。

旧 Zoom 不做任何导入；已有有效 Zoom2 优先。旧 Zoom 作为未知键按原 AutoClean 策略保留即可，保证全部活动消费者（含 ReadMini、自动 fit、测试）仅使用 Zoom2。保留默认/复制/严格 bool/数值归一化及写盘原子性。不触碰 deploy.json 的 SettingUI 或 PPT 各自倍率。

## 风险和取舍
- Explorer 自动隐藏、第三方任务栏和副屏窗口类不是完整公开合同：应隔离采样与纯计算，保留稳定回退及真实机验证说明。
- 80 DIP 主体高度不包含窗口透明包络/阴影；缩放误差按最终像素舍入验收。
- 保留当前启动 fit 和 PPT 空间 fit；它们可能使实际倍率低于用户目标，测试区分有足够空间的等高合同与受限布局保护。
- 不扩大平台支持承诺，不改 SDK/工程/依赖，不调整 PPT 布局屏幕策略，不重新设计画笔物理标尺。
