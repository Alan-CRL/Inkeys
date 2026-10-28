# Research: 剩余GetDC成本与API范围核验

- Query：Release仍有WARP刷新/GetDC耗时时，能否简单开启D2D多线程flag解决？
- Scope：mixed；当前源码与Microsoft官方文档，只读，无新性能实验。
- Date：2026-10-28

## Findings

`RenderPipeline.cpp:194` 的设备flags只有 `D3D11_CREATE_DEVICE_BGRA_SUPPORT`，没有主动禁用WARP内部线程。`Bar.Rendering.cpp:327,1739,2344` 的D2D context使用OPTIONS_NONE。

Microsoft[Direct2D性能说明](https://learn.microsoft.com/en-us/windows/win32/direct2d/improving-direct2d-performance#use-a-multithreaded-device-context)明确：D2D的多线程优化flag不影响WARP软件渲染，且该flag的几何适用范围有限。因此不应为本任务直接把OPTIONS_NONE改为ENABLE_MULTITHREADED_OPTIMIZATIONS并预期得到WARP收益；不能把factory multithreaded、D2D几何并行与WARP内部线程混为一谈。

同页说明D2D命令会批处理到EndDraw或flush等时刻。当前GetDC阶段包含此前绘制提交等待，不能仅按位图面积下降比例推断剩余都是读回拷贝。静态渲染缓存建议也不能自动授权本仓已禁止的最终动画帧缓存或额外CPU staging拷贝。

## Caveats / Not Found

当前最终Release热态24/54组预算通过；组GetDC P95中位11.299ms只是软件阶段归因，不是GPU duration，也不是每条绘制的独立耗时。尚需无副作用分段或真实函数探针确定更小候选，不能凭API命名盲试flag。新的研究见 `residual-getdc-cost.md`，本笔记不改变已验证修复或默认后端。
