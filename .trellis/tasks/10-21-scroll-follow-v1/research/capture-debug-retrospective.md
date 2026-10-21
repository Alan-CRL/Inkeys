# Bug Analysis: 全局 Mag lease 不保证独立 owner 捕获可用

## 1. Root Cause Category

- Category: E（隐含假设），同时涉及 B（运行库寿命与HWND owner的跨层合同）和D（fixture初始化上下文差异）。
- 已确认的实测症状：主线程先STA COM、再Display/首次DLL/Freeze lease后，bare-lease及预建真实隐藏first-owner/global-once对照的独立捕获owner均在MagSetWindowFilterList返回50。独立冷启动历史两轮可取得变化像素，最新check复跑55帧全NearBlack。具体SDK内部根因尚未定位。
- 同owner两个独立Mag控件的像素与交替更新正例通过，但私有内存增长尚未证明有界。不把SDK内部线程机制写成微软正式保证，不宣称ARM64全面不支持Magnification。

## 2. Why Fixes Failed / Corrections

1. 初始一秒监督混淆冷启动资源准备与采样结果。分开5秒初始化、1秒采样期限后，能测到真实API失败阶段。
2. WinSta0/Default、原生ARM64、WOW64=0、DWM启用、源图案painted及IME候选排除均已记录；仅给capture worker初始化COM不能消除error50。
3. 最终审查发现warm fixture的首次Freeze lease此前没有先初始化STA，原生helper也未严格要求初始化成功；这不等价于Window.cpp::RunGroup先COM再CreateGroup的产品前提。修正并记录HRESULT后重测，shared仍失败，cold再次成功，且helper同owner正例恢复。此前helper失败不作为共同owner不可行的证据。
4. 额外每owner初始化及仅最终反初始化实验未得到满足批准寿命合同的可靠像素/资源结果，不因BOOL成功推广到生产。
5. 共同owner像素成功只验证一个候选方向；GDI/USER/handles平稳而private bytes约311MB→376MB→440MB，不能直接宣称所有资源释放或长期运行有界，也不能仅凭3轮认定内存泄漏。

## 3. Prevention Mechanisms

| Priority | Mechanism | Specific Action | Status |
| --- | --- | --- | --- |
| P0 | 真实门槛 | cold与preheld-Freeze服务PoC分别从新进程运行，shared未过不接后续 | 已执行，门槛失败 |
| P0 | 生命周期 | 不提前Uninitialize，不强杀阻塞线程；owner保活直到系统调用退出 | check监督/Stop/lease保活通过；真实PrintWindow永久阻塞未测 |
| P1 | fixture一致 | 首次owner的COM/版本/DPI/消息泵顺序与产品对齐，检查HRESULT | 已修正并重测 |
| P1 | 分层观测 | 分别记录painted、API stage/error、质量、已知像素与资源 | 已加入诊断 |
| P1 | 规范 | 新增native-desktop/scroll-follow-capture.md并加入索引 | 已写入 |
| P1 | 正例预检 | 原生实验先复现单控件真实像素，再解释多控件差异 | 对齐后已通过 |

## 4. Systematic Expansion

- 共享引用计数只证明寿命保护，不证明新HWND的owner、API调用或像素更新兼容。
- 后续须先明确新的Mag owner与寿命方案，并验证长期资源及定格回归；此轮没有擅自迁移定格窗口线程。
- 构建、无窗口检查、冷捕获成功、shared失败、共同owner候选实验分别记账，不能合并为一个PASS。

## 5. Knowledge Capture

- 已更新 .trellis/spec/native-desktop/scroll-follow-capture.md 与 index.md。
- 完整日志保留 Build/ScrollFollow/，最终实测结论由 capture-poc.md 保存。
- 未创建远程issue、commit或push；用户明确禁止自动提交。此仓库没有Trellis CLI模板目录，不做无关模板同步。

## 检查阶段修正与结论

独立review补上实际产品创建即隐藏的first-owner Host/Child，严格保持global-once，不先Source/Filter/Show。追加真实服务仍130帧Filter50/0有效，元数据、Stop及最终lease清理通过。幸存原控件首次读回黑帧，但没有相同样式独立先验正对照，不作“捕获破坏Freeze”的因果判断。

最新cold为55帧NearBlack/0有效；lifecycle的监督、Stop、busy、运行库保活及清理通过，但最后有效像素再启失败，整体exit1。历史cold和lifecycle通过日志保留为历史，不能作为当前验收通过。新增fixture在cold路径没有额外DLL/窗口API或Mag eager硬导入；生产源码未因check改变。真实读回变动的具体原因未定，不通过放宽断言或无依据改源码消除失败。

check同时加强Source/WorkArea/generation/stride/span/QPC断言和阻塞回调managed lease断言。完整结果见子任务research/check.md，父任务阶段门槛保持失败。
