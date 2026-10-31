#define NOMINMAX
#include "../../../../Inkeys/Inkeys/UI/Bar/Bar.BottomDock.h"
#include "../../../../Inkeys/Inkeys/UI/Bar/Bar.WindowGeometry.h"
#include <cstdio>

// 仅调用现有生产几何函数验证边界，不创建 HWND，也不修改产品实现。
int main()
{
    using namespace Inkeys::UI::Bar;
    constexpr RECT layout{0, 0, 3840, 2160};
    constexpr POINT anchor{1920, 1080};
    constexpr RECT content{1880, 1040, 1960, 1120};
    auto normal = ResolveBarWindowCapacity({1600, 1200}, anchor, content, layout, 2);
    auto peak = ResolveBarWindowCapacity({4800, 3600}, anchor, content, layout, 2);
    auto after = ResolveBarWindowCapacity(peak.size, anchor, content, layout, 2);
    const bool retained = after.size.cx == 4800 && after.size.cy == 3600;
    std::printf("capacity current=%ldx%ld, after_peak=%ldx%ld, retained=%d\n",
        normal.size.cx, normal.size.cy, after.size.cx, after.size.cy, retained);

    // 合成集成边界：分配时只见当前内容，之后才加入更大的动画预留包络。
    // 输入用于证明 API 组合缺少包含保证，不声称此序列已在用户 GUI 中发生。
    BarWindowViewportController viewport;
    const auto reserved = viewport.Resolve(content, RECT{600, 500, 3240, 1660},
        layout, 2, false);
    const RECT capacityBounds{normal.origin.x, normal.origin.y,
        normal.origin.x + normal.size.cx, normal.origin.y + normal.size.cy};
    const POINT source{reserved.viewport.left - normal.origin.x,
        reserved.viewport.top - normal.origin.y};
    const bool contained = ContainsBarWindowRect(capacityBounds, reserved.viewport);
    std::printf("reserved source=(%ld,%ld), window=%ldx%ld, capacity_contains=%d\n",
        source.x, source.y, reserved.viewport.right - reserved.viewport.left,
        reserved.viewport.bottom - reserved.viewport.top, contained);

    // 光影需求使 settle=false 时，已提交的宽包络在几何恢复后仍可保留。
    viewport.Commit(reserved.viewport);
    const auto lightingActive = viewport.Resolve(content, {}, layout, 2, false);
    const auto idle = viewport.Resolve(content, {}, layout, 2, true);
    const bool untilIdle = SameBarWindowRect(lightingActive.viewport, reserved.viewport)
        && !SameBarWindowRect(idle.viewport, reserved.viewport);
    std::printf("viewport width: lighting_active=%ld, settled=%ld, retained_until_idle=%d\n",
        lightingActive.viewport.right - lightingActive.viewport.left,
        idle.viewport.right - idle.viewport.left, untilIdle);

    // 更接近生产的居中展开边界：W=600，x:0->350，w:80->600。
    // 复用居中 helper；范围采用端点，不加入 Back 过冲，容量仍额外预留 1.06。
    const auto rootRange = ResolveBarBottomDockCenteredRootRange(
        960, 82, 82, 0, 350, 82, 602);
    const auto placement = ResolveBarBottomDockCenteredRootPlacement(960, 82, 350, 602);
    const POINT dockAnchor{static_cast<LONG>(std::lround(placement.mainCenterDip)), 960};
    const LONG plannedHalfWidth = static_cast<LONG>(std::ceil(
        600 * 1.06 + 80 / 2.0 + 10 + 207.5 + 24));
    const auto dockCapacity = ResolveBarWindowCapacity({plannedHalfWidth * 2, 1000},
        dockAnchor, RECT{614, 910, 1306, 1010}, RECT{0, 0, 1920, 1080}, 2);
    const auto barEnvelope = ResolveBarWindowAnimatedRect(
        {rootRange.minimumDip, rootRange.maximumDip + 350}, {960, 960},
        {80, 600}, {80, 80}, 1, 11);
    BarWindowViewportController dockViewport;
    const auto dockCandidate = dockViewport.Resolve(RECT{614, 910, 1306, 1010},
        barEnvelope, RECT{0, 0, 1920, 1080}, 2, false);
    const RECT dockCapacityBounds{dockCapacity.origin.x, dockCapacity.origin.y,
        dockCapacity.origin.x + dockCapacity.size.cx,
        dockCapacity.origin.y + dockCapacity.size.cy};
    const bool dockContained = ContainsBarWindowRect(dockCapacityBounds, dockCandidate.viewport);
    std::printf("centered endpoint model: anchor_x=%ld, capacity_right=%ld, viewport_right=%ld, contains=%d\n",
        dockAnchor.x, dockCapacityBounds.right, dockCandidate.viewport.right, dockContained);
    return retained && !contained && source.x < 0 && untilIdle && !dockContained ? 0 : 1;
}
