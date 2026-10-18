# 2026-10-18 最终收尾验收

## 结论与工作树

本轮窗口排序、PPT目标/同页恢复、持久化锁等待和PPT终态交接已完成实现、独立源码复核、最终完整构建及适用运行验收。原来默认窗口套件11项失败目前不再存在，新增可见Setting周期刷新回归也通过。没有宣称11项等于11个独立死锁，也没有宣称已确定所有用户现场的共同根因。

原F1–F4已提交于 `68cd297da63642d58fd755da20ec4c8bac42bd1a`。本轮后续修复保留工作区，不新增commit/push、不重置原暂存内容；Trellis工程验收完成，生命周期仍停留待本轮代码提交/归档。原始调查和失败产物保留，最终结果覆盖旧报告的“未运行GUI/根三项未解决”状态。

## 最终产物与构建

既有 `InkeysRepo.sln`、Debug|ARM64，动态定位Visual Studio ARM64原生MSBuild；同invocation规范化Path/PATH、关闭node reuse。使用现有同版本完整provider `D:/Project/Inkeys/Repo/Inkeys/VcpkgInstalled/Arm64`，未改solution、SDK、依赖版本或项目构建系统。最终构建日志 `Build/CanvasFreezeInvestigation/fixes/final-20261018-owned-setting.{json,log,exitcode}`，exit0。

| 产物 | SHA256 |
| --- | --- |
| 最终Inkeys.exe | `b49504f9995638f8c0dfae07acb7917db36f2f36bed1d401731b674822f14318` |
| 最终InkeysHeadlessTests.exe | `8f68e6e65ff1c8e6e46d07b61d8c51acb9a2b7ea029d192c657f1d99be4961ae` |
| 存储专用测试目标 | `8e15ad3e7142def4341266577de00addb26b6c3fe630c7e42509e84e2a28f33d` |

存储目标在窗口最终改动前已按既有inkStrokeModelerTest.sln的测试目标编译exit0，相关存储源码之后未变。一次增量链接器LNK1000属本地ILK损坏，隔离生成ILK后重建成功；没有以改产品源码/工具链绕过环境问题。最终solution退出码0，未把warning或末行输出代替退出码。

## 改动与证据

### 窗口

对实际Win32 owner/band状态取证：根SetWindowPos返回成功时实际band仍可能未改变；根修正后原生传播也可能漏掉七个基础节点。只用HWND_TOP不能把normal节点移到topmost。最终实现仅在实际失配时有限校正根、从同一有效根为遗漏基础成员继承band，并按owner顺序排序。根仍独占显式TOPMOST/NOTOPMOST；普通/白板沿用原生Bar owner联动；PPT相对顺序由固定四句柄EnumWindows快照保留，没有新增跨线程互等或任意系统辅助窗操作。

带探针的12轮中，3次自然失配均观察到七个基础节点遗漏，最终21/21节点恢复，证明修复分支实际执行。随后删除调查探针并重建。首轮最终GUI又抓到新排序将Drawpad推到visible Setting上方、导致内容点不到的回归；保留 `gui-clean-results-first-regression.json` / `clean-gui-failure-first-regression.png`。最终Most基础HWND_TOP保留原生owner联动，加入Show/Reshow后再次RootRefresh的完整十窗band/owner及两画布上方断言，重新构建及GUI通过。

原断言意图保留，修正两个经原生对照确认的测试假设：合法owner辅助窗可位于同组中，但同PID独立窗仍必须破坏连续性；visible SW_HIDE可以原生取消同输入队列其他窗capture，另用已隐藏重复Hide对照确认不会主动释放新他窗capture。不能把这些修正描述为删掉失败断言。

### PPT与画布准入

- raw/descriptor暂时不一致时保留待应用状态，可信raw追上后仍可收敛；页码UI使用已接受target的身份，避免永久等待不匹配UI回执。
- 同页暂停后要求下一次有效发布生成新targetRevision，等待真实Present及PageControl新回执；不强制ready、不放开未初始化墨迹保护。
- PPT root和Desktop root+date索引锁单次获取上限1000ms。未取得锁返回原错误，不读写索引、不返回伪NotFound；WAIT_ABANDONED仍是成功取得锁。释放后成功重试读取原非空墨迹。
- PPT Accepted之前预留work/completion节点及完整身份；worker处理异常仍保留同身份IoError，发布/共享取出不分配。Host保留待交付终态，WindowController预留单槽与普通命令保持FIFO并在消费后唤醒后继，覆盖重启清理，避免accepted loadPending永远等不到completion。
- 收尾中修正一个测试测时假设：生产QPC等待用15ms/粗GetTickCount64验收存在量化风险；只改harness为steady_clock/64ms、1ms下界容差，并保留generation和单次wait断言及失败实际值。未改生产idle协议。旧失败缺少实际值，不能倒推出它必然就是量化导致。

## 最终运行结果

证据目录：`Build/CanvasFreezeInvestigation/final-20261018`。窗口测试和产品GUI串行运行，使用唯一隔离产品实例。

| 验证 | 最终结果 |
| --- | --- |
| `--window-contract-only` | 同最终Headless SHA连续12/12轮exit0，含原11项意图、可见Setting周期刷新、全树band/owner、PPT相对顺序、白板、capture及原生对照 |
| 默认完整InkeysHeadlessTests（无参数） | exit0，82.547秒，实际包括窗口和全部默认组件 |
| 生产CLI completion/retry/topology/retained/control-fence/diagnostic-output | 6项均exit0，同最终产品SHA |
| completion CLI额外参数 | 按设计exit2，未启动GUI |
| `--presentation-autosave-only` | exit0，3.297秒；真实worker、operator new故障注入、持锁有限失败、释放后原非空墨迹恢复 |
| `--desktop-autosave-only` | exit0，1.969秒；真实worker持锁、索引和文件保护、释放后重试 |
| 最终产品GUI，全部ConsoleOutput=false | pass，PID41588，同最终产品SHA，具体如下 |

最终GUI证据 `gui-clean-results.json`：

1. Settings显示后再等待3.5秒周期刷新，实际Z序仍高于两画布，owner=Drawpad；RightDown内容区确实capture=自身。
2. 按住时Alt+F4隐藏，在物理Up前capture=0；后续新线1298红像素。真实橡皮移动、停住0.6秒、继续Move，ROI红像素1298→0。
3. 真实PowerPoint16自建三页文稿：鼠标仍按住时第1→2页，物理Up前已经匹配session/revision的新UI-ready；之后新Down产生1303新像素。回第1页匹配新revision并新增1305像素。
4. Inkeys打开真实Explorer，仅关闭本轮新增This PC窗口；选择→画笔后新增1304像素。重开设置首次点击切页、普通X正常隐藏。
5. 再次在设置内容区按住，点击真实“关闭软件”，exit0/1.688秒、forced=false、无该PID HWND。

同轮此前产品A35构建已跑真实PowerPoint1/2/3页恢复、快速1→3→1→2、结束页返回、放映退出重入及Inkeys分页按钮，逐步UI identity与新增像素通过；范围及SHA见 `gui-ppt-validation-20261018.md`。这些较广矩阵按自身产物记录；最终B495再次验证上述关键集成链，未把A35结果冒充B495的全部矩阵。Office黑屏当次没有触发Host suspend，因此只证明书写连续性，同页暂停新revision由生产共享组件证明。

## 审查、清理和限制

completion与Window已由独立检查复核，最终GUI回归修补再次只读复核未发现阻断问题或断言弱化。`git diff --check HEAD`通过；已比对Git checkout过滤后的BOM/CRLF（core.autocrlf=true，Git对象LF不能误判为工作区换行改变），源码格式保持，未改项目/依赖/用户配置。源码SHA/格式清单 `source-format-manifest.json`。

最终Inkeys PID41588正常退出，早前失败GUI PID41604也通过真实关闭正常exit0/1.547秒；自建PowerPoint收到stop后正常Close/Quit，fixture进程exit0。自建软件/PPT窗口无残留，用户原有Visual Studio和Inkeys Analytics Explorer保留。没有强杀最终验收实例、没有删除用户文稿或原始视频。

未运行物理触摸/触控笔、WPS、Windows7、多屏或故障用户原始设备；不能以鼠标通过替代物理设备验收。没有穷尽Office/GPU/RTS/OS内部等待；资源耗尽下CPU拓扑切换失败和Desktop completion全链异常不在本轮PPT Accepted保证内。持久化1000ms只约束单次mutex获取，不是整个文件I/O、队列drain或退出上限。持续I/O损坏/预算耗尽仍安全拒绝写入，不能为消除“不可画”而丢掉原墨迹保护。原始现场共同根因保持未确认。


## 提交及归档授权

用户在最终验收后明确要求“为改动提交commit，如果有相关任务则结束并提交”，并说明本线程即将合入dev。本轮据此提交修复、归档父任务及F1–F4四个已验收子任务，并提交会话记录；不执行push或dev合并。前文“未提交/待提交”保留为验收时状态，最终提交信息见归档task.json与本轮会话记录。
