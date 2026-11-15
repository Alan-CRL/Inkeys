# 第七轮输入回归根因与修复

## 首次按下与四角
Bar.ShapeEditor 的 Advance 原来把 Frame.input 绑定整个浮条入场完成及 editOpacity 完全稳定，Hit 在检查圆点前就因此返回空。圆点已经可见时，首个按下会落到画布，后续抬起才进入稳定可编辑阶段。改为已呈现的编辑控件随 editOpacity 可见就接纳，浮条按钮继续保留原入场与行布局稳定门。透明命中覆盖与 Hit 共用该输入门，避免屏内圆点只有视觉没有命中。
核心与 Hit 中四角先于中心区域；本轮不改几何缩放合同。将全预设逐柄回归由旧 Pointer 改为实际 Contact 路径，检查四角没有被多指中心路径吞掉；另在入场第 4 帧做第一次触摸 Down/Move/Up，核 handle=1000、无 gestureTransform、真实候选有变化。

## 三维点拖动积压
SelectionOverlay 第六轮无条件 preserveMoveSamples=true，绕过 Bar.Interaction 的连续 Move 合并。每条旧坐标都在 worker 内进行真实/预测 EditSession 与 BuildEditHandles、BuildGeometry，再排到 Draw3，造成昂贵几何在输入线程积压。恢复按 Down 时锁存的策略：Angle/Rotate 与可加入多指的触摸中心保留轨迹，其余位置控件合并。
三维底面大小+Z旋转柄是 id1001 / Vertex；EditShape 以 before 和末点的逆投影 atan2 差求结果，不需中间轨迹。弧/扇形 102/103 的累计角保留原有逐点消费；多指中心保持有序输入和拓扑。
Bar.Interaction 合并条件补齐 contactSource/pen，不能把同 ID 的不同设备或笔源合并。实际生产邮箱回归压入 80 条位置 Move，只消费最后一条并保留唯一 Down/Up；另核来源变化的分界。现有跨 180 度弧端点真实邮箱测试保留；全预设 Contact 回归验证三维柄分类允许合并。

## 验证边界
源码静态检查完成；主代理统一执行 ARM64 Solution 构建、形状编辑器离屏和输入 worker 测试。未运行 GUI，没有提交。实际触控/笔延迟仍需用户设备验收。
