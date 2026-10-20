# 实施计划（2026-10-19 已批准）

## 规划
- [x] 创建planning任务，记录后续默认/配置/双样式/绘制范围决定。
- [x] 只读核对Hook热路径/生命周期、PPT HiMsg实际接入和旧四项行为。
- [x] 形成PRD、设计、研究、实现/检查context。
- [x] 用户于2026-10-19明确批准整份规划并要求开始实现。

## 获批后的执行顺序
1. 读取decisions→PRD→design→研究/context，批准后才task.py start。按Trellis实现/检查代理流程执行，不新建替代任务。
2. 新Config四字段、统一三态/dock纯策略、严格读写测试；先确定接口，避免各消费者重复解释，不访问旧bool。
3. Hook紧凑快照/预分配桥/必要原子鼠标键分支，同线程安装卸载及有界停止。先验证负code/MOVE/拥塞/consumer锁住/启停，旧延迟业务移到owner。
4. PPT统一expected identity准入、delta累计及过期清理，原业务节流/黑页保持；expected token一直进入IdtPlug-in最终request，旧事件不转到白板/新场次；EndShow同token只一项pending/in-flight；提交前失败链传、提交后可选wake失败不重复链传，嵌套交互有限batch drain。与本地入口不重复。
5. Bar owner接accepted drawing/erasing、栏外候选与100ms取消、主按钮受控fold。先验证会话/布局barrier再启用默认Drawing=1；保留原光照聚合activity。
6. 设置四行下拉和三语/缩放布局，FIFO冻结保存；正式i18n sync/check，不手改生成物，两种样式一致。
7. fresh全范围独立check，修确认问题；核对minimal diff/编码/BOM/CRLF及键盘hook、RTS、Raw/HiMsg/WM_TOUCH边界。
8. 用既有InkeysRepo.sln Debug|ARM64完整构建，vswhere定位当前VS ARM64原生MSBuild；同PowerShell先Remove-Item Env:PATH并设MSBUILDDISABLENODEREUSE=1，允许至少5分钟，不改工具链绕环境。
9. Headless --no-window、相关配置/状态/笔宽生产探针，记录实际ExitCode。性能压测仅生产快路径/有界桥，不启动系统Hook/GUI冒充headless。
10. 实际鼠标/触摸板、PPT/WPS、Win7/UIPI及系统光标手感由用户人工或另获GUI授权验收；更新spec，若要commit届时另取本任务授权，不沿用上次。

## 验证矩阵
- Hook：安装失败/重复Stop/StartStop竞态/consumer停顿/队列满/不稳路由/旧wake跨generation；MOVE链传，兼容缓存无map分配或业务锁。
- PPT：确认唯一累计owner及本地独立有界投递，不让native HWND与Hook两线程直接写累计或共用SPSC；本地保留inputLocked/targetVisible及Selection语义。正负/0/30+30+60/大delta/反转/切场清余量，Hook成功接纳仅一条逻辑命令，白板接管不调用白板，Unknown/隐藏/电脑操作模式/设置模态不接管；原末页黑页/PreviousNext outstanding保持；最终request不会重新抓取新token，黑页EndShow连续滚动队列有界且终态释放。
- Config：各字段0..2roundtrip，缺失、类型错误/超宽越界独立回默认，ReadMini/reset/copy/FIFO冻结、不上传；旧deploy所有bool组合都不影响新值；样式切换不改字段。
- AutoFold：双样式×四触发×三态×Floating/BottomDocked-Free/BottomDocked-Centered，Drawing含形状/激光，实际临时橡皮按Erasing，拒绝contact不触发；短笔已结束但有效起笔仍触发，旧场景起笔不得影响新场景；副面板独立。
- Drag：普通点击不触发，原阈值/真实脱离后触发，弹性不误判，fold不取消本手势/不漂移锚点，外部Cancel保留，Raw/native Up一次，触摸同样有效。
- Outside：Bar主栏/菜单/颜色/滑条/弹窗内不收，Selection/ClickThrough栏外才收；延迟期设置/工具/dock/新输入取消；提升事件不生成第二笔。
- UI：简中/繁中/英文、缩放/长文本四行布局，值不变不写盘，变更作用后续触发且不立即展开/收起。

## 执行边界
用户已批准源码实现与非交互验证。仍不启动GUI/Computer Use，不commit/push；保留既有输入来源及兼容性合同。

## 实施完成记录
- [x] 用户批准、task.py start，保留原任务。
- [x] Config/严格codec、四行下拉、三语正式生成与检查。
- [x] Hook生产桥、鼠标原子缓存、PPT最终token与本地独立通道、有界结束请求。
- [x] Bar owner有限消费、accepted-stroke通知、拖动内fold和栏外候选取消。
- [x] fresh独立全范围检查与实际编译接线修正复核。
- [x] 完整ARM64 Solution通过；--no-window、state-mode、pen-width均exit0。
- [x] spec、最小diff、原编码/BOM/CRLF与验证记录。
- [x] 用户确认改动生效，授权结束任务；代理未启动GUI，不扩大为Win7/全部设备已验收声明。
- [x] 用户授权本任务commit与归档；依次实现提交、归档提交、会话记录提交，不push。
