# 实施计划

## 1. 实现步骤

1. 在 `Bar.Main.cppm` 增加粗细分割线、笔型扩展入口、菜单行/对钩所需的 Shape/SVG/Word 枚举；在 `Bar.State.cppm` 增加最小瞬态菜单状态，并在 `BarUISetClass` 增加对应 hover stage。
2. 在 `Bar.Main.cpp` 提取几何/绘制属性分割线共用的 `1 DIP`、`0.5 DIP` 和第三光强度常量；初始化新粗细分割线时完整复用几何选择的 `PointLight` 配置，关闭旧粗细外框的可见 fill/frame/light。
3. 重排粗细控制行和分割线目标几何：从面板尺寸、笔类型列和 `BarDrawAttributeGap` 推导横向端点，从颜色边界推导纵向两个 `5 DIP` 间隙；把新对象纳入展开、收起和上下换边动画批次。
4. 改写粗细预览几何计算，删除标注线徽标保护深度和预览中心偏移；让预览、滑轨及 hold-lock 提示使用完整可用区与稳定内容内边距。
5. 将溢出提示接入 Preview 专属动画门控：进入 Slider 时先禁用命中并关闭提示，再等待视觉退场后拉直轨道；退出时在轨道恢复后再允许提示淡入。清理快速反向时的 hover、pin、close press、timer 和零透明度命中。
6. 调整全部笔类型的图标/文字内部布局并使用 leading 对齐；添加单个动态扩展入口，使其只跟随当前选中且通过 `PenModeSupportsAnnotationLine` 的按钮，资格失效时立即清理交互状态。
7. 初始化并布局自由线/标注线菜单；复用 `barThicknessAdjust` 小三角、`colorSelect` 对钩以及原 annotation 问号/popup。实现自由线关闭菜单、标注线禁用消费、问号帮助和外部点击关闭后继续处理原消息。
8. 复用颜色浮层的绝对坐标及曲线实现向外展开；打开时锁存 `primaryBar` 方向，换边/笔型变化/面板关闭时统一退场，动画归零后才接受新方向。
9. 更新渲染顺序、独立 hover 映射、dirty rect、设备资源清理与线程退出清理，审计所有依赖枚举连续范围的循环，确保新增对象不会漏动画或误参与交互。

## 2. 实现中检查点

- 完成步骤 3 后静态核对：分割线外缘到颜色/控制行均为 `5 DIP`，横向为面板左间距到笔型列左间距，几何选择分割线视觉未改变。
- 完成步骤 5 后逐帧核对四个阶段：Overflow -> fade out -> track morph -> thumb；thumb -> track restore -> Overflow fade in。
- 完成步骤 7 后确认没有任何代码从菜单写入 annotation 模式，且自由线不引入第二份业务状态。
- 完成步骤 8 后核对 `primaryBar=true/false` 的目标位置和初始位移符号一致，换边退场沿锁存方向播放。

## 3. 验证

1. 运行 `git diff --check`，检查空白、冲突标记和意外文件改动。
2. 搜索并审计新增枚举、旧 `ThicknessAnnotationBadge` 布局引用、`thicknessPreviewOverflow`、Slider 动画门控和所有 tooltip availability/click 路径。
3. 使用 ARM64 host `MSBuild.exe` 构建完整 Solution：

~~~text
MSBuild.exe InkeysRepo.sln /m /p:Configuration=Debug /p:Platform=ARM64
~~~

构建超时至少 5 分钟；不单独构建 `Inkeys.vcxproj`。

4. 若发现现有相关测试 target 则运行；当前仓库静态扫描未发现 UI3 Bar 自动化测试项目。
5. 手工验证矩阵：
   - 面板在主栏上/下方的分割线位置、5 DIP 间距和第三鼠标光；
   - 硬笔与荧光笔互切、支持/不支持能力变化、选中/未选中入口显隐；
   - 菜单向上/向下展开、自由线对钩、标注线禁用、问号悬停/固定/关闭；
   - Preview/Slider 双向切换、有/无 overflow、快速 hover/固定/拖动/反向；
   - 面板展开/收起/换边过程中无旧 hover、按压、命中、timer 或分层窗口残影；
   - 颜色选择器、粗细预设、拖动提交与 hold-lock 无回归。

## 4. 风险与回滚点

- 枚举范围循环是最高编译/运行风险；每新增一组对象后立即审计同步、ChangeValue、dirty、render 和 discard 路径。
- Preview/Slider 门控若顺序错误会造成延迟或闪烁；以“命中立即关、视觉先退、Slider 后进”为不可交换顺序。
- 菜单方向如果读取实时 `primaryBar` 会在换边时跳跃；退场必须使用打开时锁存值。
- 若任一阶段产生回归，先回滚对应独立块（分割线、Preview 门控、笔型菜单），不改动绘制引擎或既有颜色选择器来绕过。
