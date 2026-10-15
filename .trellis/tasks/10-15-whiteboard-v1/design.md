# 技术设计

协调三个子任务，统一验证、更新spec，不直接替代子任务实现。

复用既有Draw3 Host、RenderPipeline、BarSurfaceScene和Window Service，禁止第二套运行时或窗口。所有异步结果按身份与revision门控。UI所有权与业务callbacks分离。详细合同见prd.md。

集成顺序：storage发布纯值/Bridge接口→UI主题及菜单→scene联动→总体检查；接口明确后允许独立文件并行。主会话独占工程文件与spec更新。

## 截图反馈修订设计
分页标签、独立菜单与右侧主题矩形统一由共享 Scene 呈现和命中，布局包络随实际回弹值更新，透明间隙穿透。最小化状态由现有窗口服务发布，分页可见性与主栏贴底策略共同消费；放映存在时入口和业务同时拒绝最小化。主栏图标、方向与属性动画沿用已有规范。

## UI3 统一修订设计
以共享 BarSurfaceScene 承载 UI 拓扑，分页层只发布菜单开关、主题选择和锚点。面板与控件通过共享 UI 继承布局、动画与 envelope 使用同一几何，不能重复推进独立计时器。对齐共享 4×1 / 2×2 metrics、More divider、colorSelect SVG；纹理相位以内容中心建立对称格点。保持 HWND owner、业务请求和异步存储接口不变。
