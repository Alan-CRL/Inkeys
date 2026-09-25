# PPT UI3、切页同步与主栏放映场景修复

## Goal and approval
修复 PPT 控件位置/保存、有效缩放、可信页同步、主栏场景、仅主栏退出确认和焦点。2026-09-25 用户已在独立后续消息明确 PLEASE IMPLEMENT THIS PLAN，批准完整方案及最小 PptCOM 补充，规划审阅门已通过。
基线 bugfix/pptui / 94e07b2599adab9de4286aa5fb7526e2c1c681f6，本地与远端 dev 相同；初始工作区干净，无本会话活动任务。十个其他任务不动。

## Requirements and acceptance
- R1：记忆开关不限制本场拖动。区分运行偏好、候选/成功提交、保存请求和磁盘成功值；版本化成对交接，旧快照/设置刷新/旧完成不能回写新位置。取消/丢捕获/窗口失败不永久锁定；临时 DPI/屏幕适配不保存。
- R2（用户补充）：拖动不写盘。开启和关闭记忆瞬间都保存当前位置；真正结束放映且记忆开启时保存；关闭后不更新保存位置直到重新开启，重置除外。每次进入读保存位置、缺失用默认。重置无论开关均更新运行/保存默认值。正常软件退出视为会话结束；白板覆盖、结束页、无效状态不算退出。
- R3：旧 Settings JSON 不得回写旧保存位置；真实文件成功才标记保存。失败保留旧文件/当前位置/同一重试快照，不采集关闭记忆后的临时位置。
- R4：布局、Scene、文字、命中、阴影、呈现使用同一有效缩放；覆盖乘积>4、屏幕适配<.5、资源上限、白板分页。保持左右两对联动。
- R5：不预测 Next 页码；保留文稿/binding/session/target/SlideID/index 保护；区分文档切换、成功呈现、页码提交、输入开放。安全合并未接受目标，不丢笔迹/保存事务。
- R6：切页收尾已接受持久笔迹到旧页，隔离仍按下 contact 至物理终态；停止跨页重连/惯性，取消瞬态 Laser；不无限等 Up。目标画布和可见页码提交后接受新输入。
- R7：Esc重入、文稿切换/同名、重排/重绑定不串页；保留正确的 parked/warm 旧 target 恢复后比较 SlideID 顺序，不将普通翻页当结构变化。
- R8：进入展开低栏=>居中低栏，主体下沿在放映屏幕底边上5 DIP；展开普通悬浮保持。收起普通保持位置/收起；收起低栏无跳变解除吸附。退出展开=>居中低栏/正常工作区高度；收起保持。场景幂等、白板优先，主栏与四控件互不排斥。
- R9（最新决定覆盖原请求）：仅主栏 EndShow 点击确认。PageControl结束图标/滚轮/长按/原生键盘/Office退出不新增确认，保留已有非点击行为。默认取消，关闭/失败不退出，不提前切模式。single-flight和会话校验防止旧确认退出新会话。
- R10：不重新启用 Draw2；保留非PPT快捷键、颜色编辑、控件系统repeat参数。有效进入/明确业务按钮后一次性交还当前PPT焦点；尊重设置/颜色/确认框，绘制/拖动/轮询/渲染不抢焦点。
- R11：可关闭分段计时，native观察不是COM事件时间；只报告真实基线/修改后数据与条件，未实测 Office/WPS/硬件标 NOT VERIFIED。

## Constraints and verification
用户批准最小托管补充：事件唤醒现有 descriptor owner、缓存会话状态和预期会话退出；保留旧 COM GUID/顺序/descriptor、页码读取、ROT、Application绑定和Office/WPS兼容。Draw3单Host/producer/独立设备不变。
保持编码/换行/minimal diff/关键中文注释。完整Solution Debug|ARM64，原生ARM64 MSBuild，规范化PATH，>=5分钟。允许headless/hidden-window/offscreen，不启动交互式GUI。不commit/push；journal/archive用--no-commit；未满足验收不提前归档。
验收使用生产交接测试、实际Scene/offscreen、Draw3 hidden、托管状态/释放测试、i18n check、diff check；人工矩阵保留为未验证。