# 技术设计

协调三个子任务，统一验证、更新spec，不直接替代子任务实现。

复用既有Draw3 Host、RenderPipeline、BarSurfaceScene和Window Service，禁止第二套运行时或窗口。所有异步结果按身份与revision门控。UI所有权与业务callbacks分离。详细合同见prd.md。

集成顺序：storage发布纯值/Bridge接口→UI主题及菜单→scene联动→总体检查；接口明确后允许独立文件并行。主会话独占工程文件与spec更新。

## 截图反馈修订设计
分页标签、独立菜单与右侧主题矩形统一由共享 Scene 呈现和命中，布局包络随实际回弹值更新，透明间隙穿透。最小化状态由现有窗口服务发布，分页可见性与主栏贴底策略共同消费；放映存在时入口和业务同时拒绝最小化。主栏图标、方向与属性动画沿用已有规范。

## UI3 统一修订设计
以共享 BarSurfaceScene 承载 UI 拓扑，分页层只发布菜单开关、主题选择和锚点。面板与控件通过共享 UI 继承布局、动画与 envelope 使用同一几何，不能重复推进独立计时器。对齐共享 4×1 / 2×2 metrics、More divider、colorSelect SVG；纹理相位以内容中心建立对称格点。保持 HWND owner、业务请求和异步存储接口不变。

## 2026-10-15 UI3统一设计（最新，替代Scene拓扑方案）
1. 沿用BarButtonClass/BarButtonListClass，抽取实际Main/MainMore的列表装箱、视觉目标、每帧推进及公共命中/绘制；添加稳定列表快照。固定属性不改动态列表，逐按钮调用同一函数。PageIndicator/ThemeColor/ThemePattern仅为有限内容适配器。
2. 固定面板共享现有绘制/橡皮属性的目标、motion和inheritance函数，同帧统一dt；不保留popupClock或递归parentPanel图。分组opacity，复用renderer按钮clip/圆角资源，不维护白板预览自有roundedgeometry缓存。
3. Renderer不依赖完整BarUISetClass：显式Typography/format cache、倍率、lighting snapshot、设备generation。主栏光照计算一次，各target坐标转换，各自D2D context/bitmap/cache。Target事务统一dirty-before-clear、缩放补偿、精确遮罩生命周期、提交重试与成功发布。
4. 输入复用现有worker，HWND路由+已成功呈现的immutable hit snapshot。每个手势Down/Move/Up/Cancel仅走一步并返回，允许coroutine/continuation保存旧手势局部状态；不保留阻塞nested Wait。交互反馈由render消费，动作通过原服务队列派发，阻塞dialog改异步续接。保留触摸mouse兼容过滤、double-click、长按、拖动语义。
5. 分页移除BuildWidgets/独立目标推进/命中/渲染，替换为自身ButtonList及两个固定面板。检查Scene其他consumer，无使用方时删旧实现与工程项。PPT布局revision、成对drag/pagePresented、窗口所有权、存储不变。
6. 无窗口性能sink执行真实D2D/GetDC同步，仅不调用原生ULW；raw诊断覆盖同Scheduler批次，不能宣称DWM视觉帧率。主栏单独baseline与迁移后多target用同机同配置，旧异常日志不作改善百分比基线。

## 菜单层级与预览修订
调整固定面板的绘制顺序和命中优先级，使分页栏在更多菜单上层、更多在主题上层。色板调整静态映射；使用现有纹理函数的共享居中规则绘制全按钮预览，70 DIP预览沿用24 DIP居中间距得到九点/九交点，画布规则不变，不新增动画或缓存。
