#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>

namespace Inkeys::Business::PenWidth
{
    enum class Family { Brush, Highlighter, Laser };
    enum class Source { Preset, ExplicitDip };
    enum class Preset { Thin = 0, Medium = 1, Thick = 2 };
    struct Selection
    {
        Source source = Source::Preset;
        Preset preset = Preset::Medium;
        double explicitDip = 3.0;
    };
    struct Context
    {
        std::uint64_t revision = 0;
        int paintDevice = 1;
        double effectiveDpi = 96.0;
        int pixelWidth = 1920;
        int pixelHeight = 1080;
    };
    struct Metrics
    {
        double resolvedWidthDip = 3.0;
        double idealCanvasWidthPx = 3.0;
        double appliedCanvasWidthPx = 3.0;
    };
    inline constexpr double MaximumDip = 65536.0;
    inline constexpr double MaximumCanvasPx = 1048576.0;
    inline bool IsValidDip(double value) noexcept
    {
        return std::isfinite(value) && value > 0.0 && value <= MaximumDip
            && static_cast<float>(value) > 0.0f;
    }
    inline bool IsValidPreset(Preset preset) noexcept
    {
        return preset == Preset::Thin || preset == Preset::Medium || preset == Preset::Thick;
    }
    inline bool IsValidSelection(const Selection& value) noexcept
    {
        return value.source == Source::Preset ? IsValidPreset(value.preset)
            : value.source == Source::ExplicitDip && IsValidDip(value.explicitDip);
    }
    inline double SystemScale(const Context& context) noexcept
    {
        return std::isfinite(context.effectiveDpi) && context.effectiveDpi >= 1.0
            && context.effectiveDpi <= 1536.0 ? context.effectiveDpi / 96.0 : 1.0;
    }
    inline double PresetDip(Preset preset, Family family, const Context& context) noexcept
    {
        const int index = IsValidPreset(preset) ? static_cast<int>(preset) : 1;
        constexpr double brush[] = { 1.0, 3.0, 6.0 };
        constexpr double largeBrush[] = { 2.0, 4.0, 6.0 };
        constexpr double highlighter[] = { 16.0, 24.0, 36.0 };
        constexpr double laser[] = { 3.0, 5.0, 7.0 };
        if (family == Family::Laser) return laser[index];
        if (context.paintDevice != 0)
            return family == Family::Highlighter ? highlighter[index] : brush[index];
        double contentScale = 1.0;
        if (context.pixelWidth > 0 && context.pixelHeight > 0)
        {
            // 全幅输出像素与旋转对称参考，既不使用工作区也不依赖 EDID。
            contentScale = (std::min)((std::max)(context.pixelWidth, context.pixelHeight) / 1920.0,
                (std::min)(context.pixelWidth, context.pixelHeight) / 1080.0);
        }
        const double result = (family == Family::Highlighter ? highlighter[index] : largeBrush[index])
            * contentScale / SystemScale(context);
        return IsValidDip(result) ? result : (family == Family::Highlighter ? 24.0 : 4.0) / SystemScale(context);
    }
    inline double CanvasWidthPx(double widthDip, const Context& context) noexcept
    {
        // 浮点几何保留精度；只在最终输出边界保护乘法与缩窄范围。
        const double width = IsValidDip(widthDip) ? widthDip : 3.0;
        return (std::max)(static_cast<double>((std::numeric_limits<float>::denorm_min)()),
            (std::min)(MaximumCanvasPx, width * SystemScale(context)));
    }
    inline Metrics Resolve(Selection selection, Family family, const Context& context) noexcept
    {
        if (!IsValidSelection(selection)) selection = {};
        const double dip = selection.source == Source::Preset
            ? PresetDip(selection.preset, family, context) : selection.explicitDip;
        const double pixels = CanvasWidthPx(dip, context);
        return { dip, pixels, pixels };
    }
    inline int RoundPositiveCanvasPx(double pixels) noexcept
    {
        if (!std::isfinite(pixels) || pixels <= 0.0) return 1;
        pixels = (std::min)(pixels, MaximumCanvasPx);
        return (std::max)(1, static_cast<int>(std::floor(pixels + 0.5)));
    }
    inline bool EquivalentContext(const Context& left, const Context& right) noexcept
    {
        return left.paintDevice == right.paintDevice && SystemScale(left) == SystemScale(right)
            && left.pixelWidth == right.pixelWidth && left.pixelHeight == right.pixelHeight;
    }
}
