# 实施设计

## 差距与归属

当前 RenderLoop 的 SetLogoVisual 对 logo1/logoInk/logoLight 分别直接设置透明度/笔色，三层交叉淡化，且所有模式均着色。修正位于主按钮专属外观状态、SVG 数据和必要的 SVG 缓存接入；保持通用颜色替换与非主按钮 SVG 不变。

## 单一 SVG 与外观状态

使用一个稳定主图标实例加载统一 SVG，包含原中性笔/屏幕、4478887c 原渐变指示路径及浅色整体轮廓。复用原路径，允许内部 group/defs/属性分层。通过真实 current 材质权重、绘制态指示透明度和当前动画笔色解析各部分 fill/stroke/stop-color/opacity，不在 draw 阶段以布尔值瞬切。

优先用当前 lunasvg 的文档元素属性 API 对已加载文档做主按钮专属加工，或同等有明确作用域的模板处理；禁止新建通用 SVG 主题引擎。默认路径要始终是可解析的 SVG。主图标外观参数进入缓存失效比较、成功缓存快照和 dirty 观察；失败不发布成功缓存，设备重建保持外观输入。

非绘制时指示权重收至零，笔身/屏幕回到对应中性色；Dark 绘制端点与原 logo1+Frame94 的组合一致，Light 绘制端点真实整笔色与轮廓。画笔色使用既有 Color 动画，几何从荧光等切换时不得遗留错误来源。收展浅深权重继续来自现有 mainButtonLightMaterial。

## 文件边界

核心代理负责 Bar.RenderLoop.cpp、Initialization.cpp、Main.cppm、UI.cpp/.cppm、Rendering.cpp 的必要 SVG 缓存检查、统一主 Logo 资源及必要小型纯外观 helper。只删除多余主图标层的引用/生命周期代码，不重构外围逻辑。测试代理负责原生外观测试、当前测试入口和新任务下 SVG/离屏回归脚本。主会话负责工程登记、规范、构建与收尾。

## 兼容与证据

普通 SVG 保持原 color1/color2 路径；新增专属外观参数默认关闭。底部刚性变换、点击脉冲、继承/dirty、失效重建保留单实例原逻辑。对照 4478887c 的 Dark 资源/行为与 0606cbe0 的 Light 图形；不回退其他主题材质。
