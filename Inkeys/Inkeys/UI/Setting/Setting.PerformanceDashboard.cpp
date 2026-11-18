#ifndef NOMINMAX
#define NOMINMAX
#endif

#include "Setting.PerformanceDashboard.h"
#include "../../../IdtI18n.h"
#include "../../../IdtI18nKeys.g.h"
#include "imgui/imgui_internal.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <ctime>
#include <format>
#include <string>
#include <vector>

namespace Inkeys::UI::Setting::Design
{
	namespace
	{
		using namespace Inkeys::Drawing::Draw3::DevicePerformance;

		constexpr float kPaddingDip = 20.0F;
		constexpr float kRadiusDip = 8.0F;
		constexpr float kHeaderHeightDip = 32.0F;
		constexpr float kRowHeightDip = 32.0F;
		constexpr float kValueTweenSeconds = 0.35F;
		constexpr float kChevronTweenSeconds = 0.18F;
		constexpr double kClearConfirmSeconds = 3.0;

		// 看板只有一个实例，状态只在本次进程的设置会话内保留（展开状态默认收起，不持久化）。
		struct DashboardMotion
		{
			int frame = -1;
			bool expanded = false;
			bool loadInitialized = false;
			NavigationTween load;
			std::array<NavigationTween, kDimensionCount> bars{};
			NavigationTween chevron;
			// 档位底色：固定起点色与目标色之间按 0→1 进度插值（不做逐帧指数逼近）。
			bool colorInitialized = false;
			NavigationTween colorProgress;
			ImU32 colorFrom = 0, colorTo = 0;
			bool clearArmed = false;
			double clearArmedAt = 0.0;
			float lastHeight = 0.0F;
		};

		DashboardMotion& Motion()
		{
			static DashboardMotion motion;
			return motion;
		}

		ImU32 LerpColor(ImU32 a, ImU32 b, float t)
		{
			t = std::clamp(t, 0.0F, 1.0F);
			ImU32 result = 0;
			for (const int shift : { IM_COL32_R_SHIFT, IM_COL32_G_SHIFT, IM_COL32_B_SHIFT, IM_COL32_A_SHIFT })
			{
				const float from = static_cast<float>((a >> shift) & 0xFFu);
				const float to = static_cast<float>((b >> shift) & 0xFFu);
				result |= static_cast<ImU32>(std::lround(from + (to - from) * t)) << shift;
			}
			return result;
		}

		ImU32 WithAlpha(ImU32 color, int alpha)
		{
			return (color & ~IM_COL32_A_MASK) | (static_cast<ImU32>(alpha) << IM_COL32_A_SHIFT);
		}

		// 译文模板出错时（占位符被破坏）回退为模板原文，不能让设置页因 format_error 崩溃。
		template<class... Args>
		std::string Format(const std::string& pattern, const Args&... args)
		{
			try { return std::vformat(pattern, std::make_format_args(args...)); }
			catch (const std::exception&) { return pattern; }
		}

		std::string DateText(std::int32_t date)
		{
			char text[16];
			std::snprintf(text, sizeof(text), "%02d-%02d", date / 100 % 100, date % 100);
			return text;
		}

		std::string DateTimeText(std::int64_t unixSeconds)
		{
			const std::time_t value = static_cast<std::time_t>(unixSeconds);
			std::tm local{};
			if (localtime_s(&local, &value) != 0) return {};
			char text[32];
			std::snprintf(text, sizeof(text), "%04d-%02d-%02d %02d:%02d:%02d", local.tm_year + 1900, local.tm_mon + 1,
				local.tm_mday, local.tm_hour, local.tm_min, local.tm_sec);
			return text;
		}

		std::string LevelName(Level level)
		{
			switch (level)
			{
			case Level::Ample: return IA(I18nKey.SettingsUI.Performance.Dashboard.Level.Ample);
			case Level::Moderate: return IA(I18nKey.SettingsUI.Performance.Dashboard.Level.Moderate);
			case Level::Tight: return IA(I18nKey.SettingsUI.Performance.Dashboard.Level.Tight);
			case Level::Overloaded: return IA(I18nKey.SettingsUI.Performance.Dashboard.Level.Overloaded);
			default: return {};
			}
		}

		std::string ConclusionText(Level level)
		{
			switch (level)
			{
			case Level::Ample: return IA(I18nKey.SettingsUI.Performance.Dashboard.Conclusion.Ample);
			case Level::Moderate: return IA(I18nKey.SettingsUI.Performance.Dashboard.Conclusion.Moderate);
			case Level::Tight: return IA(I18nKey.SettingsUI.Performance.Dashboard.Conclusion.Tight);
			case Level::Overloaded: return IA(I18nKey.SettingsUI.Performance.Dashboard.Conclusion.Overloaded);
			default: return {};
			}
		}

		std::string DimensionName(std::size_t index)
		{
			switch (static_cast<Dimension>(index))
			{
			case Dimension::Writing: return IA(I18nKey.SettingsUI.Performance.Dashboard.Dimension.Writing);
			case Dimension::Highlighter: return IA(I18nKey.SettingsUI.Performance.Dashboard.Dimension.Highlighter);
			case Dimension::Eraser: return IA(I18nKey.SettingsUI.Performance.Dashboard.Dimension.Eraser);
			case Dimension::Laser: return IA(I18nKey.SettingsUI.Performance.Dashboard.Dimension.Laser);
			case Dimension::Shape: return IA(I18nKey.SettingsUI.Performance.Dashboard.Dimension.Shape);
			default: return IA(I18nKey.SettingsUI.Performance.Dashboard.Dimension.Pan);
			}
		}

		// 帧率建议与附加说明，每项一段；数字模板用 std::format 占位符，顺序与 zh-CN 一致。
		std::vector<std::pair<std::string, bool>> AdviceParagraphs(const Advice& advice, const Report& report)
		{
			std::vector<std::pair<std::string, bool>> paragraphs; // second = 次要色
			const int suggested = advice.suggestedFps;
			const int predicted = static_cast<int>(std::lround(PredictLoadPercent(report.overall.avgWorkMs, suggested)));
			// 刷新率未知时界面按与之匹配的预设档说明（MatchFrameRate 已按 60 Hz 处理）。
			const int refresh = advice.refreshHz > 0 ? advice.refreshHz : advice.matchFps;
			switch (advice.suggestion)
			{
			case Suggestion::Lower:
				paragraphs.emplace_back(Format(IA(I18nKey.SettingsUI.Performance.Dashboard.Advice.Lower), suggested, predicted), false);
				break;
			case Suggestion::LowestHeavy:
				paragraphs.emplace_back(IA(I18nKey.SettingsUI.Performance.Dashboard.Advice.LowestHeavy), false);
				break;
			case Suggestion::LowerToMatch:
				paragraphs.emplace_back(Format(IA(I18nKey.SettingsUI.Performance.Dashboard.Advice.LowerToMatch), suggested), false);
				break;
			case Suggestion::Raise:
				paragraphs.emplace_back(Format(IA(I18nKey.SettingsUI.Performance.Dashboard.Advice.Raise), suggested, predicted, refresh), false);
				break;
			case Suggestion::MatchedCanRaise:
				paragraphs.emplace_back(Format(IA(I18nKey.SettingsUI.Performance.Dashboard.Advice.MatchedCanRaise), refresh, suggested), false);
				break;
			case Suggestion::AboveRefresh:
				paragraphs.emplace_back(Format(IA(I18nKey.SettingsUI.Performance.Dashboard.Advice.AboveRefresh), refresh), false);
				break;
			case Suggestion::Fine:
				paragraphs.emplace_back(IA(I18nKey.SettingsUI.Performance.Dashboard.Advice.Fine), false);
				break;
			default: break;
			}
			if (advice.stutterNote)
			{
				const int percent = static_cast<int>(std::lround(advice.overBudgetPercent));
				paragraphs.emplace_back(Format(IA(I18nKey.SettingsUI.Performance.Dashboard.Advice.Stutter), percent), false);
			}
			if (advice.restartPending)
				paragraphs.emplace_back(Format(IA(I18nKey.SettingsUI.Performance.Dashboard.Advice.RestartPending), advice.configuredFps), true);
			if (report.source == ReportSource::PreviousSignature)
				paragraphs.emplace_back(IA(I18nKey.SettingsUI.Performance.Dashboard.Advice.Previous), true);
			return paragraphs;
		}

		// 小号胶囊标签；返回宽度（像素）。
		float DrawChip(ImDrawList* draw, const std::string& text, ImVec2 position, float height, ImU32 color)
		{
			const float width = TextWidth(text.c_str(), ImFluentTextStyle_Caption) + Pixels(16.0F);
			draw->AddRectFilled(position, { position.x + width, position.y + height }, WithAlpha(color, 36), height * 0.5F);
			TextAt(text.c_str(), { position.x + Pixels(8.0F), position.y + (height - Pixels(16.0F)) * 0.5F },
				width, ImFluentTextStyle_Caption, color);
			return width;
		}

		std::string PercentText(double percent)
		{
			const long rounded = std::lround(percent);
			return rounded >= 1000 ? std::string("999+") : std::to_string((std::max)(0L, rounded));
		}
	}

	PerformanceDashboardEvents RenderPerformanceDashboard(const PerformanceDashboardInput& input)
	{
		PerformanceDashboardEvents events;
		events.recordingEnabled = input.recordingEnabled;
		DashboardMotion& motion = Motion();
		// 只有连续帧推进动画；离页再回来时直接从当前值继续，不补走离开期间的时间。
		const int frame = ImGui::GetFrameCount();
		const float delta = motion.frame == frame - 1 ? input.delta : 0.0F;
		motion.frame = frame;
		const float rate = std::clamp(input.animationRate, 0.1F, 5.0F);
		const bool animate = input.animationsEnabled;

		static const Report kEmptyReport{};
		const Report& report = input.report ? *input.report : kEmptyReport;
		const bool hasData = report.source != ReportSource::None && report.overall.available;
		const Advice advice = Advise(report.overall, report.signature.frameRate, input.configuredFps,
			static_cast<int>(report.displayRefreshHz), static_cast<int>(report.sessionFrameRate));
		const Level level = hasData ? advice.level : Level::None;
		const auto& palette = GetPalette();
		const int levelIndex = static_cast<int>(level);
		const ImU32 accent = level == Level::None ? palette.textSecondary : palette.performanceAccent[levelIndex];

		// 首项不是 SectionHeader：补页头与内容之间的标准间距；卡片之后的 SectionHeader 自带节间距。
		PageContentStart();
		PageBlockScope block("##perf-dashboard");
		ImGui::PushID("##perf-dashboard");
		ImDrawList* draw = ImGui::GetWindowDrawList();
		const ImVec2 origin = ImGui::GetCursorScreenPos();
		const float width = ImGui::GetContentRegionAvail().x;
		const float padding = Pixels(kPaddingDip);
		const float radius = Pixels(kRadiusDip);
		const float left = origin.x + padding;
		const float right = origin.x + width - padding;
		const float contentWidth = (std::max)(Pixels(120.0F), right - left);

		// ---- 底色：档位变化时从当前可见色过渡到新色 ----
		const ImU32 targetFill = level == Level::None ? palette.card : palette.performanceFill[levelIndex];
		if (!motion.colorInitialized)
		{
			motion.colorFrom = motion.colorTo = targetFill;
			motion.colorProgress.Reset(1.0F);
			motion.colorInitialized = true;
		}
		else if (targetFill != motion.colorTo)
		{
			motion.colorFrom = LerpColor(motion.colorFrom, motion.colorTo, motion.colorProgress.value);
			motion.colorTo = targetFill;
			motion.colorProgress.Reset(0.0F);
			motion.colorProgress.Target(1.0F);
		}
		motion.colorProgress.Advance(delta, kValueTweenSeconds / rate, animate);
		const ImU32 fill = LerpColor(motion.colorFrom, motion.colorTo, motion.colorProgress.value);

		// ---- 背景先画（位于内容下层），内容画完后按真实高度平移下半部顶点 ----
		// 不用 ChannelsSplit：嵌套的详情条件块在拆分状态下会退回逐顶点淡化（见 setting-motion.md）。
		const float predictedHeight = (std::max)(motion.lastHeight, radius * 2.0F + Pixels(2.0F));
		const int backgroundStart = draw->VtxBuffer.Size;
		draw->AddRectFilled(origin, { origin.x + width, origin.y + predictedHeight }, fill, radius);
		draw->AddRect(origin, { origin.x + width, origin.y + predictedHeight }, palette.stroke, radius);
		const int backgroundEnd = draw->VtxBuffer.Size;

		float y = origin.y + padding;
		const float headerHeight = Pixels(kHeaderHeightDip);

		// ---- 标题行：标题、档位/状态胶囊、展开按钮 ----
		const std::string title = IA(I18nKey.SettingsUI.Performance.Dashboard.Title);
		TextAt(title.c_str(), { left, y + (headerHeight - Pixels(28.0F)) * 0.5F }, contentWidth,
			ImFluentTextStyle_Subtitle, palette.textPrimary);
		float chipX = left + TextWidth(title.c_str(), ImFluentTextStyle_Subtitle) + Pixels(12.0F);
		const float chipHeight = Pixels(22.0F);
		const float chipY = y + (headerHeight - chipHeight) * 0.5F;
		// 窄窗口下放不下的标签不画，避免压到右侧“详情”按钮（按较长的按钮文案预留）。
		const float chipLimit = right - Pixels(NavigationGlyphSize + 24.0F) - Pixels(8.0F) - (std::max)(
			TextWidth(IA(I18nKey.SettingsUI.Performance.Dashboard.Expand).c_str(), ImFluentTextStyle_Body),
			TextWidth(IA(I18nKey.SettingsUI.Performance.Dashboard.Collapse).c_str(), ImFluentTextStyle_Body));
		const auto chip = [&](const std::string& text, ImU32 color)
		{
			const float width = TextWidth(text.c_str(), ImFluentTextStyle_Caption) + Pixels(16.0F);
			if (chipX + width > chipLimit) return;
			chipX += DrawChip(draw, text, { chipX, chipY }, chipHeight, color) + Pixels(6.0F);
		};
		// 顺序即优先级：窄窗口放不下时先舍弃靠后的标签，“记录已暂停”比“设置变更前”更需要看到。
		if (hasData) chip(LevelName(level), accent);
		if (!input.recordingEnabled)
			chip(IA(I18nKey.SettingsUI.Performance.Dashboard.PausedTag), palette.textSecondary);
		if (report.source == ReportSource::PreviousSignature)
			chip(IA(I18nKey.SettingsUI.Performance.Dashboard.PreviousTag), palette.textSecondary);

		// 展开按钮：透明按钮 + 文字 + 旋转箭头；无数据时也显示，便于打开记录开关。
		{
			const std::string label = motion.expanded ? IA(I18nKey.SettingsUI.Performance.Dashboard.Collapse)
				: IA(I18nKey.SettingsUI.Performance.Dashboard.Expand);
			const float glyph = Pixels(NavigationGlyphSize);
			const float buttonWidth = TextWidth(label.c_str(), ImFluentTextStyle_Body) + glyph + Pixels(24.0F);
			ImGui::SetCursorScreenPos({ right - buttonWidth + Pixels(8.0F), y });
			ImFluent::PushStyleColor(ImFluentCol_ControlFillDefault, IM_COL32(0, 0, 0, 0));
			ImFluent::PushStyleColor(ImFluentCol_ControlStrokeDefault, IM_COL32(0, 0, 0, 0));
			ImFluent::PushStyleColor(ImFluentCol_ControlStrokeSecondary, IM_COL32(0, 0, 0, 0));
			ImFluent::PushStyleColor(ImFluentCol_ElevationControlTop, IM_COL32(0, 0, 0, 0));
			ImFluent::PushStyleColor(ImFluentCol_ElevationControlBottom, IM_COL32(0, 0, 0, 0));
			const bool pressed = Button("##perf-details-toggle", { buttonWidth, headerHeight });
			ImFluent::PopStyleColor(5);
			const ImVec2 buttonMin = ImGui::GetItemRectMin();
			const ImVec2 buttonMax = ImGui::GetItemRectMax();
			if (pressed)
			{
				motion.expanded = !motion.expanded;
				motion.clearArmed = false;
			}
			TextAt(label.c_str(), { buttonMin.x + Pixels(8.0F), buttonMin.y + (headerHeight - Pixels(20.0F)) * 0.5F },
				buttonWidth, ImFluentTextStyle_Body, palette.textPrimary);
			motion.chevron.Target(motion.expanded ? 1.0F : 0.0F);
			motion.chevron.Advance(delta, kChevronTweenSeconds / rate, animate);
			const ImVec2 iconMin(buttonMax.x - glyph - Pixels(8.0F), buttonMin.y + (headerHeight - glyph) * 0.5F);
			const ImVec2 iconMax(iconMin.x + glyph, iconMin.y + glyph);
			const int firstVertex = draw->VtxBuffer.Size;
			ImFluent::DrawIcon("\xEE\x9C\x8D", iconMin, iconMax, NavigationGlyphSize, palette.textSecondary);
			const float angle = motion.chevron.value * 3.14159265F;
			const float cosine = std::cos(angle), sine = std::sin(angle);
			const ImVec2 center((iconMin.x + iconMax.x) * 0.5F, (iconMin.y + iconMax.y) * 0.5F);
			for (int vertex = firstVertex; vertex < draw->VtxBuffer.Size; ++vertex)
			{
				const ImVec2 relative(draw->VtxBuffer[vertex].pos.x - center.x, draw->VtxBuffer[vertex].pos.y - center.y);
				draw->VtxBuffer[vertex].pos = { center.x + relative.x * cosine - relative.y * sine,
					center.y + relative.x * sine + relative.y * cosine };
			}
		}
		y += headerHeight + Pixels(12.0F);

		// ---- 大号负载百分比 ----
		if (hasData)
		{
			if (!motion.loadInitialized)
			{
				motion.load.Reset(static_cast<float>(report.overall.loadPercent));
				motion.loadInitialized = true;
			}
			motion.load.Target(static_cast<float>(report.overall.loadPercent));
		}
		motion.load.Advance(delta, kValueTweenSeconds / rate, animate);
		const std::string number = hasData ? PercentText(motion.load.value) : std::string("--");
		ImFluent::PushFont(ImFluentTextStyle_Display);
		const float bigAscent = ImGui::GetFontBaked()->Ascent;
		const ImVec2 numberSize = ImGui::CalcTextSize(number.c_str());
		draw->AddText(ImGui::GetFont(), ImGui::GetFontSize(), { left, y }, hasData ? palette.textPrimary : palette.textSecondary,
			number.c_str());
		ImFluent::PopFont();
		float numberRight = left + numberSize.x;
		if (hasData)
		{
			// “%”用标题字号，按基线与大数字对齐。
			ImFluent::PushFont(ImFluentTextStyle_Title);
			const float smallAscent = ImGui::GetFontBaked()->Ascent;
			const ImVec2 percentSize = ImGui::CalcTextSize("%");
			draw->AddText(ImGui::GetFont(), ImGui::GetFontSize(), { numberRight + Pixels(4.0F), y + bigAscent - smallAscent },
				palette.textPrimary, "%");
			ImFluent::PopFont();
			numberRight += Pixels(4.0F) + percentSize.x;
		}
		{
			const float sideLeft = numberRight + Pixels(24.0F);
			const float sideWidth = (std::max)(Pixels(80.0F), right - sideLeft);
			if (hasData)
			{
				const std::string label = IA(I18nKey.SettingsUI.Performance.Dashboard.LoadLabel);
				const int unlimited = static_cast<int>(std::lround(report.overall.unlimitedFps));
				const int measured = static_cast<int>(report.signature.frameRate);
				const std::string detail = Format(IA(I18nKey.SettingsUI.Performance.Dashboard.UnlimitedFormat), unlimited, measured);
				const float labelHeight = TextHeight(label.c_str(), sideWidth, ImFluentTextStyle_BodyStrong);
				const float detailHeight = TextHeight(detail.c_str(), sideWidth, ImFluentTextStyle_Body);
				const float sideTop = y + (numberSize.y - labelHeight - detailHeight - Pixels(2.0F)) * 0.5F;
				TextAt(label.c_str(), { sideLeft, sideTop }, sideWidth, ImFluentTextStyle_BodyStrong, palette.textPrimary);
				TextAt(detail.c_str(), { sideLeft, sideTop + labelHeight + Pixels(2.0F) }, sideWidth, ImFluentTextStyle_Body,
					palette.textSecondary);
			}
			else
			{
				const std::string hint = IA(I18nKey.SettingsUI.Performance.Dashboard.EmptyHint);
				const float hintHeight = TextHeight(hint.c_str(), sideWidth, ImFluentTextStyle_Body);
				TextAt(hint.c_str(), { sideLeft, y + (numberSize.y - hintHeight) * 0.5F }, sideWidth, ImFluentTextStyle_Body,
					palette.textSecondary);
			}
		}
		y += numberSize.y + Pixels(8.0F);

		// ---- 结论、帧率建议与元信息 ----
		if (hasData)
		{
			const std::string conclusion = ConclusionText(level);
			TextAt(conclusion.c_str(), { left, y }, contentWidth, ImFluentTextStyle_BodyStrong, palette.textPrimary);
			y += TextHeight(conclusion.c_str(), contentWidth, ImFluentTextStyle_BodyStrong) + Pixels(4.0F);
			for (const auto& [text, secondary] : AdviceParagraphs(advice, report))
			{
				TextAt(text.c_str(), { left, y }, contentWidth, ImFluentTextStyle_Body,
					secondary ? palette.textSecondary : palette.textPrimary);
				y += TextHeight(text.c_str(), contentWidth, ImFluentTextStyle_Body) + Pixels(2.0F);
			}
			y += Pixels(10.0F);

			std::string range = report.usageDays <= 1
				? Format(IA(I18nKey.SettingsUI.Performance.Dashboard.RangeSingleFormat), DateText(report.lastDate))
				: Format(IA(I18nKey.SettingsUI.Performance.Dashboard.RangeFormat), static_cast<int>(report.usageDays),
					DateText(report.firstDate), DateText(report.lastDate));
			const std::string evaluated = Format(IA(I18nKey.SettingsUI.Performance.Dashboard.EvaluatedFormat),
				DateTimeText(report.evaluatedUnixSeconds));
			const std::string firstLine = evaluated + " \xC2\xB7 " + range;
			std::string coverage;
			for (std::size_t i = 0; i < kDimensionCount; ++i)
				if ((report.coverageMask & (1u << i)) != 0)
					coverage += (coverage.empty() ? "" : " \xC2\xB7 ") + DimensionName(i);
			std::string secondLine = Format(IA(I18nKey.SettingsUI.Performance.Dashboard.CoverageFormat), coverage);
			if (report.overall.lowSample)
				secondLine += "   " + IA(I18nKey.SettingsUI.Performance.Dashboard.LowSample);
			for (const std::string* line : std::array<const std::string*, 2>{ &firstLine, &secondLine })
			{
				TextAt(line->c_str(), { left, y }, contentWidth, ImFluentTextStyle_Caption, palette.textSecondary);
				y += TextHeight(line->c_str(), contentWidth, ImFluentTextStyle_Caption) + Pixels(2.0F);
			}
		}
		// 收起态内容统一用一个 Dummy 登记高度（其中的按钮已作为独立 item 提交）。
		ImGui::SetCursorScreenPos(origin);
		ImGui::Dummy({ width, y - origin.y });

		// ---- 展开区：各维度明细 + 记录开关 + 清除数据 ----
		if (!motion.expanded) motion.clearArmed = false;
		if (motion.clearArmed && ImGui::GetTime() - motion.clearArmedAt > kClearConfirmSeconds) motion.clearArmed = false;
		for (std::size_t i = 0; i < kDimensionCount; ++i)
		{
			const Figure& figure = report.dimensions[i];
			motion.bars[i].Target(figure.available ? static_cast<float>(figure.loadPercent) : 0.0F);
			motion.bars[i].Advance(delta, kValueTweenSeconds / rate, animate);
		}
		if (ConditionalBlock details{ "##perf-details", motion.expanded }; details)
		{
			const ImVec2 start = ImGui::GetCursorScreenPos();
			float row = start.y;
			draw->AddLine({ left, row }, { right, row }, palette.stroke, (std::max)(1.0F, Pixels(1.0F)));
			row += Pixels(8.0F);
			const float rowHeight = Pixels(kRowHeightDip);
			const float nameWidth = Pixels(96.0F);
			const float fpsWidth = Pixels(110.0F);
			const float percentWidth = Pixels(64.0F);
			const float barLeft = left + nameWidth + Pixels(8.0F);
			const float barRight = (std::max)(barLeft + Pixels(40.0F), right - fpsWidth - percentWidth - Pixels(16.0F));
			const float barHeight = Pixels(6.0F);
			for (std::size_t i = 0; i < kDimensionCount; ++i)
			{
				const Figure& figure = report.dimensions[i];
				const float textTop = row + (rowHeight - Pixels(20.0F)) * 0.5F;
				const std::string name = DimensionName(i);
				TextAt(name.c_str(), { left, textTop }, nameWidth, ImFluentTextStyle_Body, palette.textPrimary);
				if (!figure.available)
				{
					const std::string none = IA(I18nKey.SettingsUI.Performance.Dashboard.NoDimensionData);
					TextAt(none.c_str(), { barLeft, textTop }, right - barLeft, ImFluentTextStyle_Body, palette.textSecondary);
				}
				else
				{
					const Level dimensionLevel = ClassifyFigure(figure);
					const ImU32 barColor = palette.performanceAccent[static_cast<int>(dimensionLevel)];
					const float centerY = row + rowHeight * 0.5F;
					draw->AddRectFilled({ barLeft, centerY - barHeight * 0.5F }, { barRight, centerY + barHeight * 0.5F },
						WithAlpha(palette.textSecondary, 40), barHeight * 0.5F);
					// 超过 100% 时整条填满，颜色已是超载色。
					const float ratio = std::clamp(motion.bars[i].value / 100.0F, 0.0F, 1.0F);
					if (ratio > 0.0F)
						draw->AddRectFilled({ barLeft, centerY - barHeight * 0.5F },
							{ barLeft + (std::max)(barHeight, (barRight - barLeft) * ratio), centerY + barHeight * 0.5F },
							barColor, barHeight * 0.5F);
					const ImU32 valueColor = figure.lowSample ? palette.textSecondary : palette.textPrimary;
					const std::string percent = PercentText(figure.loadPercent) + "%";
					const float percentTextWidth = TextWidth(percent.c_str(), ImFluentTextStyle_Body);
					TextAt(percent.c_str(), { barRight + Pixels(8.0F) + percentWidth - percentTextWidth, textTop },
						percentTextWidth + Pixels(2.0F), ImFluentTextStyle_Body, valueColor);
					const int fps = static_cast<int>(std::lround(figure.unlimitedFps));
					std::string fpsText = Format(IA(I18nKey.SettingsUI.Performance.Dashboard.DimensionFpsFormat), fps);
					if (figure.lowSample) fpsText += " \xC2\xB7 " + IA(I18nKey.SettingsUI.Performance.Dashboard.LowSampleShort);
					const float fpsTextWidth = TextWidth(fpsText.c_str(), ImFluentTextStyle_Body);
					TextAt(fpsText.c_str(), { right - fpsTextWidth, textTop }, fpsTextWidth + Pixels(2.0F),
						ImFluentTextStyle_Body, palette.textSecondary);
				}
				row += rowHeight;
			}
			row += Pixels(6.0F);
			draw->AddLine({ left, row }, { right, row }, palette.stroke, (std::max)(1.0F, Pixels(1.0F)));
			row += Pixels(10.0F);

			// 记录开关：卡片内不用 ToggleRow（会画出卡中卡），手动布局 ToggleAction。
			{
				const std::string name = IA(I18nKey.SettingsUI.Performance.Dashboard.Recording.N);
				const std::string description = IA(I18nKey.SettingsUI.Performance.Dashboard.Recording.E);
				const float textWidth = contentWidth - Pixels(64.0F);
				const float nameHeight = TextHeight(name.c_str(), textWidth, ImFluentTextStyle_Body);
				const float descriptionHeight = TextHeight(description.c_str(), textWidth, ImFluentTextStyle_Caption);
				const float blockHeight = (std::max)(nameHeight + descriptionHeight + Pixels(2.0F), Pixels(40.0F));
				TextAt(name.c_str(), { left, row }, textWidth, ImFluentTextStyle_Body, palette.textPrimary);
				TextAt(description.c_str(), { left, row + nameHeight + Pixels(2.0F) }, textWidth, ImFluentTextStyle_Caption,
					palette.textSecondary);
				bool recording = input.recordingEnabled;
				ImGui::PushID("##perf-recording");
				if (ToggleAction(LayoutRect{ right - Pixels(48.0F), row, right, row + blockHeight }, recording))
				{
					events.recordingChanged = true;
					events.recordingEnabled = recording;
				}
				ImGui::PopID();
				row += blockHeight + Pixels(10.0F);
			}
			// 清除数据：两段式确认，首次点击变为“确认清除”，3 秒内再次点击生效。
			{
				const std::string label = motion.clearArmed ? IA(I18nKey.SettingsUI.Performance.Dashboard.ClearConfirm)
					: IA(I18nKey.SettingsUI.Performance.Dashboard.Clear);
				const float buttonWidth = Pixels(ButtonWidth(label.c_str()));
				ImGui::SetCursorScreenPos({ left, row });
				if (Button((label + "###perf-clear").c_str(), { buttonWidth, Pixels(32.0F) }))
				{
					if (motion.clearArmed)
					{
						events.clearRequested = true;
						motion.clearArmed = false;
					}
					else
					{
						motion.clearArmed = true;
						motion.clearArmedAt = ImGui::GetTime();
					}
				}
				row += Pixels(32.0F);
			}
			ImGui::SetCursorScreenPos({ origin.x, start.y });
			ImGui::Dummy({ width, row - start.y });
		}

		// ---- 底部内边距并按真实高度修正背景 ----
		ImGui::Dummy({ width, padding * 0.5F });
		const float bottom = ImGui::GetItemRectMax().y;
		const float realHeight = (std::max)(bottom - origin.y, radius * 2.0F + Pixels(2.0F));
		const float shift = realHeight - predictedHeight;
		if (shift != 0.0F)
			for (int vertex = backgroundStart; vertex < backgroundEnd; ++vertex)
				if (draw->VtxBuffer[vertex].pos.y > origin.y + predictedHeight * 0.5F)
					draw->VtxBuffer[vertex].pos.y += shift;
		motion.lastHeight = realHeight;
		ImGui::PopID();
		return events;
	}
}
