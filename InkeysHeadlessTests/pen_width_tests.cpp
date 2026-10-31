#include "../Inkeys/Inkeys/Business/PenWidthPreferences.hpp"

#include <cmath>
#include <iostream>
#include <iomanip>
#include <limits>

int RunPenWidthTests()
{
    using namespace Inkeys::Business::PenWidth;
    int failures = 0;
    const auto check = [&](bool ok, const char* name)
    {
        if (!ok) { ++failures; std::cerr << "FAIL pen-width: " << name << '\n'; }
    };
    std::cout << "family,width,height,dpi,preset,resolvedDip,idealCanvasPx,appliedCanvasPx,integerAdapterPx\n";
    const auto record = [](Family family, Context context, int preset, const Metrics& metrics)
    {
        std::cout << (family == Family::Brush ? "Brush" : family == Family::Highlighter ? "Highlighter" : "Laser")
            << ',' << context.pixelWidth << ',' << context.pixelHeight << ',' << context.effectiveDpi << ',' << preset
            << ',' << std::setprecision(17) << metrics.resolvedWidthDip << ',' << metrics.idealCanvasWidthPx
            << ',' << metrics.appliedCanvasWidthPx << ',' << RoundPositiveCanvasPx(metrics.appliedCanvasWidthPx) << '\n';
    };
    const auto near = [](double a, double b) { return std::abs(a - b) < 1e-9; };
    const int ordinary[][3] = { {1,3,6}, {1,4,8}, {2,5,9}, {2,6,12}, {3,8,17} };
    const double dpi[] = { 96, 120, 144, 192, 264 };
    const double brush[] = { 1, 3, 6 };
    for (int scale = 0; scale < 5; ++scale)
    {
        Context context{ 1, 1, dpi[scale], 1920, 1080 };
        for (int preset = 0; preset < 3; ++preset)
        {
            const auto metrics = Resolve({ Source::Preset, static_cast<Preset>(preset), 0 }, Family::Brush, context);
            record(Family::Brush, context, preset, metrics);
            check(metrics.resolvedWidthDip == brush[preset], "ordinary DIP invariant");
            check(RoundPositiveCanvasPx(metrics.idealCanvasWidthPx) == ordinary[scale][preset], "ordinary integer matrix");
            check(metrics.appliedCanvasWidthPx == metrics.idealCanvasWidthPx, "continuous adapter retains ideal");
            const auto high = Resolve({ Source::Preset, static_cast<Preset>(preset), 0 }, Family::Highlighter, context);
            constexpr double ordinaryHighlighter[] = { 16, 24, 36 };
            constexpr int highExpected[][3] = { {16,24,36}, {20,30,45}, {24,36,54}, {32,48,72}, {44,66,99} };
            record(Family::Highlighter, context, preset, high);
            check(high.resolvedWidthDip == ordinaryHighlighter[preset]
                && RoundPositiveCanvasPx(high.appliedCanvasWidthPx) == highExpected[scale][preset], "ordinary highlighter matrix");
        }
    }
    const int widths[] = { 1920, 2560, 3840 }, heights[] = { 1080, 1440, 2160 };
    const int large[][3] = { {2,4,6}, {3,5,8}, {4,8,12} };
    const int highlighter[][3] = { {16,24,36}, {21,32,48}, {32,48,72} };
    for (int resolution = 0; resolution < 3; ++resolution)
        for (double effectiveDpi : dpi)
            for (int preset = 0; preset < 3; ++preset)
            {
                Context context{ 1, 0, effectiveDpi, widths[resolution], heights[resolution] };
                Selection choice{ Source::Preset, static_cast<Preset>(preset), 0 };
                const auto pen = Resolve(choice, Family::Brush, context);
                const auto high = Resolve(choice, Family::Highlighter, context);
                record(Family::Brush, context, preset, pen);
                record(Family::Highlighter, context, preset, high);
                check(RoundPositiveCanvasPx(pen.appliedCanvasWidthPx) == large[resolution][preset], "large pen matrix");
                check(RoundPositiveCanvasPx(high.appliedCanvasWidthPx) == highlighter[resolution][preset], "large highlighter matrix");
                const auto landscape = pen;
                std::swap(context.pixelWidth, context.pixelHeight);
                check(near(Resolve(choice, Family::Brush, context).idealCanvasWidthPx, landscape.idealCanvasWidthPx), "portrait symmetry");
            }
    Context fractional{ 1, 0, 264, 2560, 1440 };
    const auto middle = Resolve({}, Family::Brush, fractional);
    check(near(middle.resolvedWidthDip, 64.0 / 33.0) && near(middle.idealCanvasWidthPx, 16.0 / 3.0)
        && RoundPositiveCanvasPx(middle.idealCanvasWidthPx) == 5, "1440p 275 percent no early DIP round");
    check(RoundPositiveCanvasPx(4.5) == 5 && RoundPositiveCanvasPx(8.25) == 8
        && RoundPositiveCanvasPx(0.5) == 1, "positive half up and odd diameters");
    check(Resolve({}, Family::Laser, fractional).resolvedWidthDip == 5
        && Resolve({}, Family::Laser, fractional).idealCanvasWidthPx == 13.75, "laser one DPI multiplier");
    const auto nan = std::numeric_limits<double>::quiet_NaN();
    const auto inf = std::numeric_limits<double>::infinity();
    for (double invalid : { 0.0, -1.0, nan, inf, 1e300, 1e-300 })
    {
        check(!IsValidDip(invalid), "invalid DIP rejected");
        const auto result = Resolve({ Source::ExplicitDip, Preset::Thin, invalid }, Family::Brush, fractional);
        check(near(result.resolvedWidthDip, middle.resolvedWidthDip), "invalid selection middle fallback");
    }
    for (double invalidDpi : { 0.0, -96.0, nan, inf, 1e300, 1e-300 })
    {
        Context context = fractional; context.effectiveDpi = invalidDpi;
        check(SystemScale(context) == 1.0 && std::isfinite(Resolve({}, Family::Brush, context).resolvedWidthDip), "invalid DPI safe fallback");
    }
    check(RoundPositiveCanvasPx(nan) == 1 && RoundPositiveCanvasPx(inf) == 1
        && RoundPositiveCanvasPx(1e300) == static_cast<int>(MaximumCanvasPx), "integer conversion overflow protection");
    Context unusual = fractional; unusual.pixelWidth = 2560; unusual.pixelHeight = 1080;
    check(near(Resolve({}, Family::Brush, unusual).idealCanvasWidthPx, 4), "non 16 by 9 limiting content dimension");
    unusual.pixelWidth = 0;
    check(near(Resolve({}, Family::Brush, unusual).idealCanvasWidthPx, 4), "bad output safe 1080 reference");
    unusual.pixelWidth = (std::numeric_limits<int>::max)(); unusual.pixelHeight = unusual.pixelWidth;
    check(std::isfinite(Resolve({}, Family::Highlighter, unusual).appliedCanvasWidthPx), "huge dimensions finite bounded output");
    unusual = { 1, 0, 1536, 320, 180 };
    const Selection thin{ Source::Preset, Preset::Thin, 0 };
    check(Resolve(thin, Family::Brush, unusual).resolvedWidthDip < 1
        && RoundPositiveCanvasPx(Resolve(thin, Family::Brush, unusual).idealCanvasWidthPx)
        == RoundPositiveCanvasPx(Resolve({}, Family::Brush, unusual).idealCanvasWidthPx)
        && thin.preset != Selection{}.preset, "pixel collision does not merge identity");

    Json::Value legacy(Json::objectValue);
    legacy["Preset"]["MemoryWidth"] = false;
    legacy["Preset"]["AutoDefaultWidth"] = false;
    legacy["Preset"]["DefaultBrush1Width"] = 3.0;
    legacy["Preset"]["DefaultHighlighter1Width"] = 35.0;
    auto startup = DecodePreferences(legacy, false, fractional);
    check(startup.valid[0] && near(startup.selections[0].explicitDip, 12.0 / 11.0)
        && startup.migration["Estimated"].asBool(), "legacy explicit 3 not factory inferred");
    Json::Value memoryRoot(Json::objectValue);
    memoryRoot["Draw"]["Brush1"]["Width"] = 8.25;
    memoryRoot["Draw"]["Highlighter1"]["Width"] = 66.0;
    memoryRoot["Draw"]["Laser"]["Width"] = 7.0;
    auto memory = DecodePreferences(memoryRoot, true, fractional);
    check(memory.valid[0] && near(memory.selections[0].explicitDip, 3)
        && memory.selections[2].explicitDip == 7, "legacy fractional px and laser DIP migration");
    check(near(StartupSelection(startup, memory, 0).explicitDip, 12.0 / 11.0), "disabled memory uses startup");
    startup.memoryWidth = true;
    check(StartupSelection(startup, memory, 0).explicitDip == 3, "enabled memory has priority");
    startup.autoDefaultWidth = true; startup.memoryWidth = false;
    check(StartupSelection(startup, memory, 0).source == Source::Preset, "automatic middle beats explicit startup");
    memoryRoot["PenWidth"] = EncodePreferences(memory, true);
    fractional.effectiveDpi = 96;
    const auto reopened = DecodePreferences(memoryRoot, true, fractional);
    check(reopened.selections[0].explicitDip == 3 && !reopened.migrated, "versioned idempotent no second divide");
    check(EncodePreferences(reopened, true) == memoryRoot["PenWidth"], "roundtrip no drift and migration retained");
    memoryRoot["PenWidth"]["Selections"]["Brush1"]["Source"] = "Unknown";
    const auto partial = DecodePreferences(memoryRoot, true, fractional);
    check(!partial.valid[0] && partial.valid[1], "bad enum independent slot fallback");
    memoryRoot["PenWidth"]["Version"] = 99;
    const auto future = DecodePreferences(memoryRoot, true, fractional);
    check(future.unknownSchema && !future.valid[0] && EncodePreferences(future, true) == memoryRoot["PenWidth"], "future schema preserved no legacy migration");
    memoryRoot.removeMember("PenWidth"); memoryRoot["Draw"]["Brush1"] = "bad";
    memoryRoot["Draw"]["Highlighter1"]["Width"] = -2;
    const auto malformed = DecodePreferences(memoryRoot, true, fractional);
    check(!malformed.valid[0] && !malformed.valid[1] && malformed.valid[2], "malformed partial fields safe");
    check(DecodePreferences(Json::Value("invalid"), true, fractional).valid[0] == false, "malformed root safe");
    std::cout << "pen-width policy and codec: " << (failures ? "FAIL" : "PASS") << '\n';
    return failures;
}
