# 滚动批注产品接入实施顺序

## 前置与本轮边界

依赖正式Capture和Tracker实现/check。用户批准全部后续代码与允许测试，不自动commit/push。本轮仅无窗口/完全不可见HWND，禁止Computer Use、可见内容、前台/置顶。正式实屏必要验收时停止，不标全功能通过。

## 顺序与所有权

- [x] validate/start本任务，读取prd/design/implement、Capture/Tracker最终接口/检查及product/end-before-action两份实际调查。
- [x] Core实现者统一拥有Business会话、Draw3 Begin/latest槽/可靠End/scene/owner present/gate、IdtState投影/guards、Bar交互和shortcut前门、IdtMain诊断/退出/严格harness入口，及所有工程/runner登记。开始先定稿public头和UI注册所需符号。
- [x] 可由独立UI资源实现者拥有Other.Config的仅新按钮补缺、Bar.Button.cppm的enum/数量、SVG/rc/三语源与i18n生成、必要config测试helper；不改Core拥有的Bar.Button.cpp/Interaction、工程或runner。明确共享符号并发消息给Core，源码完成前不并发build。
- [x] Core在Bar.Button.cpp完成注册白名单、explicitMore Setting前插入、最终Selected/Disable，业务toggle非阻塞。保留原组件投影和用户B/A顺序/Visible/未知项，仅补缺新官方项。
- [x] 生产会话F0/Tracker串行/shared context/短提交门，输入关闭seal，临时selection真实双窗ready+owner ACK；初始化失败回滚，运行期失效Paused，无自动恢复。图像回调不等UI/owner/PW，UI不join。
- [x] owner绝对base-total、不重复DPI；可信L2/热前像失效/按预算Tile恢复。无历史/点/PageGUID变化，输入gate聚合，可靠End自带final且等指定viewport full present，不把commandProcessed当完成。
- [x] 工具同工具early-return前门与原bool/面板/haptic，shortcut pending/expected/lease不变，普通命令可靠流前置End且跨帧阻止后继执行。单独projection代，不改真实modeRevision/shortcut scope；路由不能因follow关闭而泄漏按键到底层。
- [x] scene/device/display/文档退休和Shutdown单调升级，旧目标拒绝；Shutdown不等Window ACK，最后CPU viewport先于ExitAutoSave。保留同步PrintWindow不可取消边界，不强杀/提前释放。
- [x] 纯状态/协议/配置定向tests纳入--no-window；全不可见真实Host测试至少begin/absolute/seq/scene/latest/End多帧replay/输入seal/quarantine/失败rollback/钳制/Shutdown。测试注入不当作实屏Capture或系统穿透通过。禁止旧会ShowWindow的hidden/eraser入口。
- [x] 两实现者源码完成后单一完整InkeysRepo.sln Debug ARM64原生MSBuild，vswhere/同PS单一Path/禁node reuse/normalhost/至少5min，定向+全量允许测试、i18n/XML/PE/diff/BOM/EOL。
- [x] 顺序独立trellis-check覆盖整个Core+UI；机械局部修复与必要复测，跨模块设计问题先主会话确认；后更新spec/实际结果交接保存阶段。

## 验收记录

Core/UI已完成并冻结源码，实际接口与结果见research/core-implementation.md及ui-resources-implementation.md。最后完整ARM64 exit0/26.661s；协议、状态快捷键、配置、全隐藏真实Host和15坏argv均通过。最终End19帧/17恢复帧，随调度变化，不代表FPS。独立trellis-check进行中，full --no-window由check统一执行；真正Business初始化/暂停/回滚整链软件矩阵尚需补核。代码和隐藏owner通过不替代实际Mag/系统Window ACK/实屏门槛；保存另阶段。


## 独立check已完成（软件范围）

research/check-current.md为当前软件验收依据。修复Host所有者Channel/旧hold与SetOffSignal早期退出锁序，新增真实Tick/Frame/Tracker文件内harness矩阵。最终ARM64 exit0/15.332s，四定向全pass，完整--no-window exit0/157.972s，原8无GUI失败接管exit0/95.773s；15坏argv、46文本/4XML/diff/i18n426均过。没有留存本阶段已确认未修的源码错误，已交保存阶段。

本表勾选表示代码实施和允许的软件check完成，不放行本任务prd中的实际Mag/系统窗口ACK/可见Bar/硬件门槛；task保留in_progress及矩阵未测。最终x64/完整PE由保存阶段后主会话统筹。新的Host Hold必须显式expected Channel，不能在后续恢复无代归还。
