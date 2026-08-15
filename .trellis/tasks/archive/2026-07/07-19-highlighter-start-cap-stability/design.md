# 荧光笔固定竖直矩形画刷设计

## 1. 根因

旧实现把每个 body 的 50px 截面旋转到相邻点切线方向，并在方向变化时生成 sector/circle join。低速建模产生的极短线段只要有轻微方向噪声，就会把整个 50px 截面大幅旋转；首段 12px 锁定只能阻止 Up 重建，无法消除实时切线抖动和起笔附近的 join 形变。

## 2. 几何模型

`HighlighterPrimitive` 只保存两个中心点和固定 half size：

```text
p1: float2
p2: float2
halfSize: float2 = (1.25, 25)
```

单点令 `p1 == p2`，得到轴对齐矩形。线段 sweep 是固定矩形与中心线段的 Minkowski sum；像素着色器以 X/Y 轴向边界和线段法线边界三个方向的半平面交集求覆盖，零长度时使用标准矩形 SDF。相邻段都包含共享端点的完整矩形，因此 coverage union 会自然补齐连接，不需要额外 join primitive。

## 3. 分层数据流

- `BuildHighlighterGeometry` 先按 0.25px 去重，再为单点或每对相邻点生成 primitive 和一致的 CPU bounds。
- Down 输入点可直接生成 L0 点击矩形；真实点和 prediction 使用同一入口。
- `CommitStablePrefixToL1` 从 `committedIndex` 开始提交并同步追加 CPU 缓存，不保留 round join 或 12px 上下文。
- 完成态只合并 `committedHighlighterGeometry + l0HighlighterGeometry`。从未生成 live 几何的同步点击是唯一允许首次生成最终几何的例外。

## 4. CPU/GPU 契约

结构化缓冲 stride 从 48 bytes 改为 24 bytes，C++ `static_assert`、HLSL struct、vertex shader AABB、pixel shader coverage 和 CPU dirty bounds 同步更新。shape type、SRV 槽、Add/Retain 和 coverage union 语义不变。

## 5. 风险与验证

主要风险是 CPU bounds 小于 shader sweep，以及 L1/L0 切片丢失共享端点。测试必须覆盖单点尺寸、三方向线段、锐角/回折、0.25px 去重、切片连接、缓存合并和 `realPoints` 改写后的完成态稳定性；完整解决方案构建必须证明两个 shader 重新编译成功。
