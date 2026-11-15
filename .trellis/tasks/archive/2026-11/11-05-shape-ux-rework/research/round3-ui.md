# 第三轮 UI 实施记录

- 样式栏保留屏幕 AABB 与粘性换边，实时几何锚点直接跟随；离散停靠分支改变以 0.4 秒残差衰减过渡。下一帧继续推进残差，不重新启动跟随延迟。
- 平面 Rotate 原锚点由 UI 展开为水平并联 Rotate/Move 胶囊，Move id 0 复用整体移动；立体核心提供单 Move。真实柄与显示位置偏移分离。
- 辅助柄候选中心距约 24 DIP，新增局部 barShapeMove 圆润四向箭头，不改变其他界面的 barMove。图文采用 4 DIP 间隔与实测宽度。
- Circle 专用 AuxCircleCenterLabel，二十语言添加翻译并运行现有 sync/check，809/809 通过；椭圆仍使用中心。
- 等腰提示改为边中点双斜杠，仅编辑层绘制；等腰按钮右侧三点显示 constraintVertex，并沿用按钮点击循环动作。
- 主面板棱柱/棱锥 3–8 边；修正正形开关与边数起点重叠以及总宽度漏计开关。浮条 3D 边数弹层改三列两行。正多边形保留 3–14。
- IdtState 三种边数独立进程内状态，均默认 6；无配置写入。新增默认/上限/跨工具保留状态断言。
- 长方体/立方体步骤复用底面/高度本地化；Geometry 展开主动关闭 Recall 子面板。
- 构造期 Vertex 明确命中并发送 ConstructionBegin/Preview/Commit/CancelPointEdit，空白交还画布，不创建已完成对象的 localEdit。与 runtime 子任务协作新增同会话动作。

验证：git diff --check 通过；i18n sync/check 通过。补充离屏断言覆盖胶囊两半、3D仅移动、构造点拖动动作、实时跟随。ARM64 solution 与离屏实际运行由主代理串行集成执行，本文不预先声明其结果。未启动 GUI，未提交。

复核补充：首轮editor离屏544场景零失败；geometry仅一处旧断言未传preset，圆心文案使其失败，已按产品统一标签映射修正。新增主栏矩形像素/命中排除及只读drawingPreview首笔等腰标记，两项有离屏回归，等待最新集成重跑。
