# 全界面语言热切换实现交接

2026-11-02。实现者 live_locale；与dynamic_fonts/设置生命周期实现者共享工作区。没有启动GUI、创建commit或push。

## 已实现

- IdtI18n.h/.cpp：PreparedLanguage + prepare/commit，准备期间不发布，commit检查baseRevision后swap并递增revision；原load复用该链。capture(keys,TextSnapshot&,nonblocking)一次锁读取成组文案和LANGID，失败不改output。crash用单次try_lock。
- Bar.Button.cpp/.cppm：RefreshLocalizedText原位更新固定标签、More/Redo及14个内置组件显示元数据；清除Draw/Eraser/Geometry的styleKey，后续既有状态投影重算动态标签，保留对象/顺序/工具状态。
- Bar.Rendering.cpp/.cppm：RefreshLocalizedTypography以语言revision更新fontFamily/textLocale和既有测量缓存。先读取revision再读取locale，避免并发发布将旧字体标记为新版本。
- Bar.RenderLoop.cpp：真实RenderFrame入口消费revision，刷新20个属性/提示文字及其宽高缓存，标记真实视觉损伤。没有重建主栏状态。
- Bar.Scene.cpp：每次分页内容配置前刷新该scene独立renderer的本地化字族。
- PageControl.cpp：ConfigureSurface判定加入语言revision（一次捕获，成功配置后记账），即使业务revision不变也重建文字/测量；隐藏后再显示同样消费新版本。
- IdtPlug-in.cpp / Bar.Interaction.cpp / Window.Legacy.cpp / Helper.CrashHandler.cpp：改用成组capture生成一致的消息框语言、正文和按钮。内部MessageBox本来持有request.language和文本，本轮无需重构它。PPT读取失败不发混合语言请求；Close/Window/Crash保留完整英文fallback；Crash保持非阻塞。
- 11个JSONC的SettingsUI.Language.UI.Warn/SelectNote更新为即时生效，无重启。zh-CN snapshot对应两值同步，未改schema或索引。

## 接口与协调

设置生命周期agent须在字体候选准备就绪后commit并持久化，旧语言/字体失败保持由设置侧负责。commit仅发布map/revision，不在核心层耦合UI唤醒。设置成功后应RenderPipeline::Request相关clients，及必要PageControl::NotifyLayoutChanged；Bar/PageControl将在真实帧消费revision。

设置worker的Information/ConfirmRestart消息框也有enqueue正文/dequeue标题LANGID混用风险，已告知dynamic_fonts使用capture固定请求。本文件不拥有Setting.cpp。

## 回归覆盖

- --i18n-locales-test：prepare不发布、过期候选拒绝、成功commit、旧TextSnapshot在提交后保持、失败capture保留输出；并发publisher循环11语言3轮，reader验证同一snapshot的根Language与LANGID从未混合。
- --bar-eraser-offscreen-test：11语言下工具不变、分离模式的动态选择标签更新，按钮identity/顺序/半高网格不变；生产RefreshLocalizedText和RefreshLocalizedTypography验证，复用既有真实字体布局检查。
- --ui3-multitarget-benchmark：首个性能负载采样之前，经实际CreateBarOffscreenRenderCallback/RenderFrame消费11次语言变化，断言fontFamily、More对象/文案、颜色保持提示已更新。使用原有无HWND WARP及替代最终ULW sink；IdtMain.cpp入口明确为offscreen。测试字体collection扩展到既有8字体，未修改产品字体来源。

## 已执行验证与当前限制

- Scripts/i18n.ps1 -Command check：PASS，10个非基准语言均679/679。
- git diff --check --ignore-submodules：PASS。
- 本人文件编码/BOM和换行逐一按git cat-file --filters HEAD:path核对/恢复，多个C++ module原来没有BOM，未统一加BOM。
- 主构建由dynamic_fonts独占协调。首轮编译曾报告新MultiTarget测试未include生成key头；已补IdtI18nKeys.g.h，等待后续构建。不能据此宣称运行回归通过。
- 还需运行上述3条CLI。完整实际设置切换/图集失败/设备恢复和字体内存由设置/字体agent联合验证。
- PageControl的语言条件已加在真实ConfigureSurface，但现有独立offscreen adapter按业务变化配置，并未增加专用语言测试；最终review宜补覆盖实际ConfigureSurface，避免只测试独立adapter替身。
- 自有Window动态错误文案、Bar所有初始化IW词条、PageControl白板/PPT菜单已审计；IdtSysNotifications和Whiteboard/Ppt生产模块没有直接IA/IW静态文本。系统已托管的历史通知和外部内容不改写。

## 首轮运行诊断与修正

由构建owner执行：--i18n-locales-test通过；--bar-eraser-offscreen-test报告10个active separate-mode label失败。定位为实际热切换缺口：TransitionToString将新目标保存在transitionContent，旧content一直保留到淡出中点，而字体已经切换。RefreshLocalizedText现在复用当前状态投影的immediateLabel路径，动态标签也用SetStringImmediate撤销旧中点替换；正常工具动作仍默认原动画。回归保留即时检查并扩展GetVal/GetTar、随后推进动画不恢复旧语言。等待重编译/复跑，尚未宣称修正已验证。

--ui3-multitarget-benchmark首轮日志进入600帧采样，说明新增语言断言并未早退。现有culled=0门槛与部分p95>16.67绝对门槛产生1–2失败；是否因新probe污染夹具尚未实证。增加仅测试入口读取的INKEYS_UI3_SKIP_LOCALE_PROBE=1开关，供同一二进制启用/跳过新probe对照。默认继续执行probe，未放宽已有性能门槛，也未修改生产渲染来掩盖失败。

设置字体生命周期CLI最初与另一agent重复登记，已撤回本人的早期重复项，保留IdtMain.cpp中唯一--setting-font-lifecycle-test既有harness分支。
