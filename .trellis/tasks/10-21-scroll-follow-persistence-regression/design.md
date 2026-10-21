# 滚动批注保存与完整回归设计

## 当前范围

依赖产品接入代码和顺序check。只补验证工具、必要最小bugfix与报告；沿UInk v10与原Clear/Exit保存，不另建保存链。用户批准全部后续实现，但本轮只允许无窗口或完全不可见HWND，禁止Computer Use、可见内容和前台。无commit/push授权；必要实屏验收时停止，父任务保留未完成。

实际研究：../10-21-scroll-follow-mag-capture/research/product-integration-current.md第8/9节。全隐藏基线：Inkeys.exe --draw3-host-metrics-smoke已exit0，含真实WARP/Host输入与present，所有自建窗visible=0。

## 独立专用早退入口

新增严格harness gated的follow隐藏专测，必须在配置、单实例和普通UI初始化前返回；坏argv返回2，不落普通启动。调用既有WindowService/真实Host/真实contact mailbox，全部HWND保持IsWindowVisible=false，不能调用Reconcile的实际show或SetDrawpadSurfaceVisibility显示分支。不运行名字含hidden却会ShowWindow的旧集成/eraser测试。

输出仅任务私有新建目录，逐组件拒reparse、不覆盖，autoSaveRoot显式私有。Stop guard在callback context/窗口前执行真实Host drain，再WindowService退出。正常/异常都自动关闭私有资源，不触碰用户配置或文档。

可注入生成帧/生产绝对目标验证owner、历史和文件，但不能把注入视为真实Mag截图/屏幕滚动验收。双窗只验证真实输出ready与纯决策/受控ACK，不能为了跑通而显示。

## A -> 跟随 -> B -> 编辑 -> 保存重载

1. 普通Desktop画笔，真实Down/Move/Up生成A；等待stored/recycle和canUndo，记录原始点/style、Workspace/Page GUID和viewport。
2. 生产Begin关闭准入并seal已接受contact；注入后续Down拒绝且真实Up回收。发重复/乱序/跳号、旧session、错scene/page/host、非finite、latest单槽压力，核对合法绝对目标只应用一次。
3. 非零base、水平/垂直/反向、clamp、模拟1/1.25/2DPI但同物理位移测试不重复缩放；若帧映射2则正确一次倍数。clamp明确报告，不能当完整跟踪成功。
4. End可靠携带最后可信量，等实际tile恢复/viewport完整present与特定session/sequence回执，再退出override和释放本会话gate；其它blocker不被清。跟随不增历史、不清已有Redo、不改A点/Page GUID。
5. 同client位置画B，Bstored=client+最终viewport，A点仍完全一致。Undo只去B，再去A；Redo顺序恢复。跟随前的Redo分支单独保留验证；正常新B应沿原语义取消Redo。
6. 在所有前序屏障之后真实StopProduct，沿PrepareExitAutoSave和worker drain保存；读私有索引/.uink，Read=Complete、Import=Success，比较v10、原页/工作区、最终viewport、A/B点/style/顺序。
7. 用真实codec重载到同功能Host，再启动跟随并验证基准是重载viewport。Clear保存另测，保留Clear/Exit触发语义。不得用人工另造三点快照代替A/B证明。

## 生命周期与系统边界

最新目标与End竞态、普通队列满仍可End、End后迟到frame、初始化失败回滚、重复Stop、新Host旧代、工作区/页/文档替换、display/DPI/resize/device retirement；shutdown最后目标先于最终保存，不能等关闭WindowService显示ACK。纯测试覆盖工具/shortcut/Freeze的先End后动作顺序，保留原租约/场景拒绝。实际系统输入路由和窗口视觉另列未测。

## 构建和现有检查

完整InkeysRepo.sln Debug ARM64，vswhere定位ARM64原生MSBuild，同PS规范Path/PATH并禁node reuse，>=5min；正常宿主避免已确认sandbox C1902，不改项目绕环境。ARM64通过再尝试完整x64，依赖/环境/源码错误分别记录，不自动升级vcpkg/toolchain。

运行新增定向和--no-window，以及真正不显示窗口的既有Draw3自检/形状/工具/定格协调/白板PPT合同测试。共享storage现有inkStrokeModelerTestTests的--uink-file-only、--uink-page-file-only、--uink-presentation-only、--desktop-autosave-only、--presentation-autosave-only可按现有测试工程构建/运行；不运行demo/benchmark可见入口。i18n sync由UI阶段执行，最后check；检查三工程XML、PE hard imports、git diff与实际文件BOM/EOL。

## 完成门槛

报告各阶段实际文件/行为、最终Tracker参数及严格每序列误差/耗时、捕获质量/暂停条件、viewport/GPU/历史/保存结果和console方法。软件允许检查完成不等于端到端通过。必要实屏剩余门槛包括正式Mag worker真实图案变化/排除/FPS/资源、实际穿透/辅助交接/墨迹稳定/浏览器WordPDF与主栏视觉。到此停止并给明确人工步骤；旧系统和缺硬件逐项未测，不伪记通过。


## Viewport-only和重载证据边界（实际源码研究补充）

见产品research/viewport-only-save-current.md。现有Desktop Exit不按dirty跳过，当前canvas.viewport三值每次进入新请求；不需要改UInk格式、dirty或请求去重。必须增加真实用例：A经Clear实际Committed后，确定Controller已经接收完成且释放CPU fallback，再Undo走worker磁盘读取安装A；此后不发送新Down/B，仅Follow并在最新目标尚未预等Applied时Stop，严格读最后Exit索引/.uink，单独比较A的style/点、workspace/PageGUID和最终viewport。完整SnapshotReceipt哈希还含fileGuid/viewport，不能用它断言几何不变。

当前Host没有任意外部Desktop快照冷载/替换API，只有现有Clear Undo匹配恢复路径；该路径有意保持当前视口。共享ReadUInkFile->ImportDraw3UInkDocument能验证文件viewport往返，不能写成跨进程Host冷载已测。步骤7应使用现有磁盘恢复后再次Follow验证真实Host，再分别验证codec的保存viewport，明确这两个层级；不为验收增建产品导入功能或绕过恢复资格。若无现成确定性恢复分支证据，可加严格hidden gated的有限receipt，不以sleep或worker提交计数冒充Controller已安装。

既有共享storage四组CLI已由主会话在原代码通过，详storage-baseline-current.md；PPT legacy fixture私有TEMP须<=40字符，否则其深层session路径会超旧路径预算。除有相关生产变更外不用重复基线。后续最终完整ARM64/x64与PE检查由主会话统筹，避免并发MSBuild。


## 普通笔与形状路径必须分别说明

SolidLine在Bridge中是形状工具，真实完成类型为Shape，导出通常只有两个端点；不能把这个用例称作普通笔Drawing/建模点路径通过。保存回归至少包括实际Pen或HardPen，完成Kind Drawing、导出Pen，A的完整点/width/style/GUID在Follow前后及Exit文件保持一致；Follow后普通笔B必须核对client Down与最终viewport对应的真实首点/锚点。普通笔建模受采样时间影响，不强迫A和B整条点数组是完全相同的平移；只分别断言A前后无改写、B锁存当前视口一次。保留Shape严格平移用例可单独证明形状路径。

现有Eraser持久化为蒙版stroke，不必删除A/B原始点。测试应证明真实mask覆盖到目标及Undo/Redo的可见历史语义，不能以工具发布或收到Eraser完成kind本身声称已擦中；最终文件对象数须按既有表示而不是假定删除笔划解释。
