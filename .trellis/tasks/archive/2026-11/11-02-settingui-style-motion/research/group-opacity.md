# 动画块整体透明度调查

用户确认：先按正常图层合成块，再整体调整透明度。当前TransformMotionDraw逐顶点乘alpha，DX11使用SRC_ALPHA/INV_SRC_ALPHA，轨道和toggle黑色thumb分别混合，会使轨道透过thumb，与真正的组透明度不等价。

正确合成：先绘入透明离屏目标取得预乘RGBA，再使用ONE/INV_SRC_ALPHA将RGB和alpha同时乘组opacity后合成到原目标。不能用普通SRC_ALPHA再次乘离屏alpha，否则出现暗边；也不能只针对toggle调色。难点是一个块可跨父/子DrawList，不能仅在父列表插入一对回调，否则child绘制发生在组结束之后；须确保层序、剪裁、popup、设备epoch、回退和无窗口真实GPU验证。

整体时间已约加快10%：1×下旧页125ms，最后块位置418ms、透明度558ms，后续滚动条110ms；完整Debug|ARM64构建及三倍率定向回归通过。此证据不代表组透明度已实现。

## 当前状态

本轮真实WARP原型对opacity 0/.2/.5/1验证了组内合成后的预乘混合公式（中心像素误差≤2），但旧算法的实际GPU对照未执行。原型尚不能正确覆盖跨父/child DrawList、嵌套条件块和旧页整体淡出，未接入生产；未接入的临时头与测试已移除，避免留下无效产品代码。

清理后重新完整Debug|ARM64 Solution构建exit0；不含原型的最终二进制在三倍率定向MotionOnly回归exit0，stderr为空。当前仍是逐顶点alpha，用户指出的叠色问题尚未修复；任务保持in_progress，不commit、不归档。

## 2026-11-03 后续

已在 backend 内接入生产整块合成并完成真实 WARP 像素回归，见 [group-opacity-implementation.md](group-opacity-implementation.md)；上文“未接入”状态已过时。
