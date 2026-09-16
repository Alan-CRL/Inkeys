# 橡皮属性栏与全局粗细控制

## 当前迭代：已确认方案B
本轮基线af22b4a6，继续feature/eraser。完整要求见[scheme-b-request.md](scheme-b-request.md)，本轮设计/执行见[scheme-b-design.md](scheme-b-design.md)与[scheme-b-progress.md](scheme-b-progress.md)。用户明确授权主Agent直接实现，禁止委派、提交、推送、归档。

## 产品需求
- 标准排列为24/32/40 DIP圆选项 | 清空 | 一体自动粗细按钮。清空始终在panel中轴，正常空间panel/清空/主栏擦除中心对齐；menu锚定整个自动按钮中心。
- 同主栏高度，左右等分配宽度、对称分割线，工作区边缘整体避让。倒转只交换完整侧组；文字、A、纹理不镜像或旋转。
- 删除方形底座和底部胶囊。稳定态真实DIP、不透明白色Contact圆、灰纹，选中以内侧Accent描边与光反馈表示；Hover/Pressed不改外直径。圆可见边缘等距，命中扩展有界且互不抢占。
- BaseSize=24/32/40，旧16→24、64→40、32保持，非法/缺失→32，迁移幂等。范围为12/24/120、16/32/160、20/40/200；Touch起点为各minimum，Fixed=B。
- 自动整体共享背景/外框/圆角/hover/pressed/selected/灯光，仅body和arrow动作不同。Down锁存动作区；Mixed/五入口事务规则保持。专用路径化A SVG，与通用橡皮资源分离。
- 菜单两行：左标题右禁用齿轮；下行低/中/高等宽。无旧footer或内部长分割线，设置提示暂未开放。
- 主/子面板复用绘制属性Back几何、独立Sine alpha与时间线。允许开合短暂等比缩放/回弹，稳定精确1:1。Bounds覆盖当前回弹及光影；子跟随父当前pose；反向连续，动画开关/速度生效，结束休眠。
- 开栏同次Up不能清空；新独立Down才可操作。原Clear、Undo/Redo、页面/底图、活动接触提交边界、全局类型/敏感度与Touch/非Touch模型保持。

## 验收
先记录红灯；重新运行完整InkeysRepo.sln Debug|ARM64、headless、offscreen及两类隐藏产品测试。216组及补充极窄/超高纯几何，生产组件深浅主题/96-144-192DPI/65-100-150%UI和SVG三态，多帧开关/反向/换边/父子关闭/禁用动画。只有实际执行可标通过，真人GUI及设备验收独立说明。

## 历史
[request.md](request.md)、[validation.md](validation.md)和[format-check.md](format-check.md)记录上一轮16/32/64方案及当时验证，本轮不改写。旧禁止开合缩放、左侧清空/矩形槽位/胶囊设计由当前方案B替代，已认可控制器和Clear业务合同不变。
