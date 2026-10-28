# 技术设计

拥有IdtState/IdtMain、Bar.Main/Bar.Button/Bar.RenderLoop/Bar.BottomDock及相关场景测试、必要Window服务胶水；不得修改PageControl/Whiteboard/Draw3或其i18n文件。与UI代理协调返回放映文案，与storage协调Bridge/Host请求。

复用既有Draw3 Host、RenderPipeline、BarSurfaceScene和Window Service，禁止第二套运行时或窗口。所有异步结果按身份与revision门控。UI所有权与业务callbacks分离。详细合同见prd.md。

集成顺序：storage发布纯值/Bridge接口→UI主题及菜单→scene联动→总体检查；接口明确后允许独立文件并行。主会话独占工程文件与spec更新。

## 落地设计

BarWorkspaceSceneState 汇总 Desktop/Presentation/Whiteboard 的进入、互切和最终离开。渲染协调器仅首次进入捕获桌面位置、方向、折叠、dock/center状态及显示器；互切保留此基线，最终回桌面恢复并按显示器回退钳制可见位置。仅已有展开底栏适配目标屏幕并贴底5 DIP。

主栏标签依赖有效PPT会话，菜单关闭复用既有白板退出事务。PageControl继续拥有四个分页HWND，白板复用两个PPT底部窗口及按钮实例，其他场景层不抢先隐藏它们。最小化调用既有窗口组服务，保留工作区、页码与编辑状态。
