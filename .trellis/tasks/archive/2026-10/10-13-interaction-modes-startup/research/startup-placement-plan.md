# 规划调查与已批准合同
- 用户确认修改仅下次启动生效；corners侧/底同为24 DIP（现dock捕获20DIP）。
- Bar默认fold=false,BottomDocked+Centered；RenderLoop首布局无条件居中/force fold=false，必须分离initial placement。
- 主按钮80×80 n3折叠fill0.6；目前darkStyle默认true无生产主题切换，previewcenter复用dark0.8，corner用dark0.6，白frame0.18。
- StartupPreview已有ReadMini、3秒gate、真实ratio smooth、fatal直接红、300/1000ms顺序handoff、arbitrary shimmer opacity mask；不重建线程/设备。
- 首帧折叠NotifyBarFirstCommittedFrame目前发布80，会污染表示完整展开宽度的缓存；改为发布expanded envelope。
- Bar Superellipse/ring renderer为protected分区，Preview不能import fullBar（Bar已import Preview）；只提取无状态geometrybuilder共享。
- 新环外径32DIP/3DIP圆头/从12点顺时针，保留已有ratio/time/failure语义。D2D离屏验证像素，不能仅测试reducer。
- 当前预设页/nav不存在；新页只startup placement，旧记忆/宽度schema继续retired。

## 实施与审查确认
- StartupPlacement.h统一枚举、非法值回退、工作区/DPI/zoom/描边与舍入规则；早期Preview通过Display::QueryEffectiveDpi获得主显示器有效DPI，正式Bar采用相同查询策略。
- 缓存记录名义展开宽度，默认470 DIP；角落Diagnostics报告名义80 DIP。可见描边包络另加1 DIP，避免透明窗口padding混入24 DIP间距。
- Preview共享无状态超椭圆构造，生产与WARP像素测试共用StartupPreview.Drawing.h；失败0%使用红色轨道，不虚构进度。
- 审查曾误判侧栏导航重叠；实际ImGui回放测得原36 DIP导航行无重叠，已撤销32 DIP布局改动。保留原尺寸满足minimal diff。
- 首轮编译宏/ComPtr修正后，后续编译发现Initialization模块可见性、protected访问及测试误复制Config，按源码边界修正，不修改工具链或项目配置。
