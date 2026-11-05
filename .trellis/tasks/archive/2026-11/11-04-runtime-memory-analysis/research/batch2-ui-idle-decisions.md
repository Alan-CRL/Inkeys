# 第5项实施边界与退役门

- Freeze：在原线程内增加像素资源owner；每次有自有像素退休时依赖当次真实透明提交，不缓存跨代透明bool。启动仍真实提交后上报FreezeFirstFrameCommitted；idle以同次active/Recall/PPT快照决定回收并continue，避免释放后又进入并发新信号分支。白板拥有共享HWND时不提交，只归还自有DIB/PptSign；重新接管的无提示Freeze入口先真实清洁。所有HDC/size从当前surface更新。使用同一生产owner的真实DIB/不可见layered HWND测试，验证失败保留、闲置归还、再入、白板接管不误提交与GDI对象回落，CLI为--freeze-resource-lifecycle-test。
- Whiteboard：inactive已经提交或真实Hide回执完成且pending已退休后，释放backgroundScene的device资源并清几何缓存；不清文档/主题/背景业务状态或共享epoch。Minimized以已发布owner快照确认隐藏后回收并失效prepared以便恢复完整重建。扩展现有RunHiddenBackgroundTests，包含等待Hide时仍有目标、Hide后0目标、再入真实像素及空闲不重建。
- PageControl：以真正隐藏和过渡结束为回收门，保留进行中交互与native提交证明；回收只影响该client scene/backing，下次资源代次变化沿原完整重绘/输入门。
- Bar：只在现有几何动画完成且当前所需backing显著小于历史容量时收缩；动画期间保持高水位。不清基础主栏、共享device、字体/业务输入状态，不引入逐帧重建。
- 所有源码保持BOM/CRLF；不触碰Renderer/Controller/Host/IdtState/Setting、不改1/6。主会话统一构建/运行，本代理只静态检查。

## 已实现与验证交接

- IdtFreezeFrame.cpp/.h：FreezeSurfaceResources用于真实生产ensure/退役；首次真实透明提交后归还，idle无像素不重复分配/提交，白板拥有时不提交。PptSign仅PPT提示用时加载并随闲置释放。测试RunFreezeSurfaceResourceLifecycleTest / --freeze-resource-lifecycle-test验证真实不可见ULW、GDI对象回落、再入、真实提交失败保留和白板接管不误清。
- Whiteboard.cpp：在实际inactive确认/已知隐藏最小化边界释放client目标并清配置缓存；扩展RunHiddenBackgroundTests对Hide等待期间保留、成功后目标0、idle不重建、重入真像素以及共享device不变的检查。
- PageControl.cpp：仅在已提交的真实隐藏、退场结束且无进行中pointer/drag/capture时退役scene图形资源，将backing重置最小、下一次强制完整替换；原input/page/native帧提交状态保留。若Hide确认时捕获尚在，后续已确认隐藏快路可补退休。已有RunHiddenWindowTests补隐藏目标归还与控制唤醒不重建。
- Bar.RenderLoop.cpp：复用reserveAnimationEnvelope稳定门，像素面积超过当前需求2倍且可省至少1MiB时收缩；保留小表面，沿原targetSizeChanged全脏替换流程。RunIdleBackingResourceLifecycleTest / --ui3-idle-capacity-test用真实WARP target由4MiB缩至49152B、读回远端红像素并验证重复idle目标复用及共享device不变。
- Bar.Main.cppm仅导出新测试声明；IdtMain只新增两条严格argv/harness早退入口，Setting/Laser/Aux三条已保留。
- 静态检查：7个源文件原BOM/UTF-8/工作区CRLF保留，git diff --check通过；未构建/运行。主会话统一编译和动态验证。
