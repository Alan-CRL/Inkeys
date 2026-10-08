#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>

#include "../Inkeys/Inkeys/UI/Setting/Setting.Design.h"
#include "../Inkeys/Inkeys/UI/Setting/Setting.Typography.h"
#include "../Inkeys/Inkeys/UI/Setting/Setting.Controls.h"
#include "../Inkeys/additional/imfluent/imfluent.h"
#include "../Inkeys/additional/imfluent/imfluent_internal.h"
#include "../Inkeys/additional/imfluent/imfluent_icons.h"
#include "../Inkeys/additional/imgui/imgui_internal.h"

#include <array>
#include <cfloat>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
#include <vector>

namespace
{
	using namespace Inkeys::UI::Setting;
	namespace Design = Inkeys::UI::Setting::Design;

	struct Checks
	{
		int failures = 0;
		void Expect(bool condition, const char* name)
		{
			if (condition) return;
			++failures;
			std::cerr << "[SettingDesign] failed: " << name << '\n';
		}
	};

	bool Near(float left, float right, float tolerance = 0.001F)
	{
		return std::abs(left - right) <= tolerance;
	}

	bool SameRect(const LayoutRect& left, const LayoutRect& right)
	{
		return Near(left.left, right.left) && Near(left.top, right.top)
			&& Near(left.right, right.right) && Near(left.bottom, right.bottom);
	}

	bool Disjoint(const LayoutRect& left, const LayoutRect& right)
	{
		return left.right <= right.left || right.right <= left.left
			|| left.bottom <= right.top || right.bottom <= left.top;
	}

	bool Contains(const LayoutRect& outer, const LayoutRect& inner)
	{
		return inner.left >= outer.left && inner.top >= outer.top
			&& inner.right <= outer.right && inner.bottom <= outer.bottom
			&& inner.Width() >= 0.0F && inner.Height() >= 0.0F;
	}

	void CheckNavigation(Checks& checks)
	{
		Design::NavigationState state;
		state.Resize(960.0F);
		const auto open = Design::ResolveShellGeometry(960.0F, 700.0F, state);
		checks.Expect(state.Expanded() && !state.narrow,
			"default desktop navigation opens");
		state.Toggle();
		const auto compact = Design::ResolveShellGeometry(960.0F, 700.0F, state);
		checks.Expect(!state.Expanded() && state.desktopCompact
			&& compact.content.left < open.content.left
			&& Near(compact.pane.left, open.pane.left),
			"desktop collapse releases content width without moving the leading axis");

		state.Resize(720.0F);
		const auto narrow = Design::ResolveShellGeometry(720.0F, 520.0F, state);
		state.Toggle();
		const auto overlay = Design::ResolveShellGeometry(720.0F, 520.0F, state);
		checks.Expect(state.overlayOpen && state.Expanded()
			&& overlay.pane.Width() > narrow.pane.Width()
			&& SameRect(overlay.content, narrow.content)
			&& Near(overlay.pageWidth, narrow.pageWidth),
			"opening narrow overlay preserves content position and width");
		state.Resize(800.0F);
		checks.Expect(state.overlayOpen && state.desktopCompact,
			"resize within narrow mode preserves overlay and desktop preference");
		state.DismissOverlay();
		checks.Expect(!state.overlayOpen && state.desktopCompact,
			"dismiss closes only overlay");
		state.Toggle();
		state.Resize(960.0F);
		checks.Expect(!state.overlayOpen && !state.Expanded() && state.desktopCompact,
			"returning to desktop clears overlay and restores manual collapse");
		state.Toggle();
		state.Resize(720.0F);
		state.Resize(960.0F);
		checks.Expect(state.Expanded() && !state.desktopCompact,
			"desktop open preference also survives the narrow round trip");

		for (const float width : { 720.0F, 800.0F, 960.0F, 1200.0F, 1440.0F })
		{
			state.Resize(width);
			const auto shell = Design::ResolveShellGeometry(width, 700.0F, state);
			const LayoutRect window{ 0.0F, 0.0F, width, 700.0F };
			const LayoutRect page{ shell.content.left + shell.pageOffset,
				shell.content.top, shell.content.left + shell.pageOffset + shell.pageWidth,
				shell.content.bottom };
			checks.Expect(Contains(window, shell.pane) && Contains(window, shell.content)
				&& Contains(shell.content, page)
				&& Near(page.left - shell.content.left, shell.content.right - page.right),
				"responsive shell retains bounded panes and symmetric page gutters");
		}
	}

	void CheckNavigationPaneGeometry(Checks& checks)
	{
		for (float scale : { 1.0F, 1.25F, 1.5F, 1.875F, 2.0F, 4.0F })
			for (const ImVec2 size : { ImVec2(720, 520), ImVec2(960, 700), ImVec2(1440, 900) })
				for (bool expanded : { false, true })
				{
					Design::NavigationState state;
					state.Resize(size.x * scale / scale);
					if (state.Expanded() != expanded) state.Toggle();
					const auto shell = Design::ResolveShellGeometry(size.x, size.y, state);
					const auto regions = Design::ResolveNavigationPaneGeometry(
						shell.pane.Width(), shell.pane.Height(), state.Expanded());
					const LayoutRect pane{ 0, 0, shell.pane.Width(), shell.pane.Height() };
					checks.Expect(regions.fits && Contains(pane, regions.top) && Contains(pane, regions.middle)
						&& Contains(pane, regions.footer) && Disjoint(regions.top, regions.middle)
						&& Disjoint(regions.middle, regions.footer) && Contains(regions.top, regions.menu),
						"production pane geometry fits disjoint fixed and scrolling regions at all sizes/DPI/modes");
					for (const auto& row : regions.topItems)
						checks.Expect(Contains(regions.top, row) && Near(row.Height(), 36),
							"two fixed top navigation rows retain 36 DIP height");
					for (const auto& row : regions.footerItems)
						checks.Expect(Contains(regions.footer, row) && Near(row.Height(), 36),
							"community/sponsor/restart/exit fit the measured fixed footer");
					checks.Expect(regions.menu.bottom <= regions.topItems[0].top
						&& regions.topItems[1].bottom < regions.separators[0].top
						&& regions.separators[1].bottom < regions.footerItems[0].top
						&& regions.footerItems[1].bottom < regions.separators[2].top
						&& regions.separators[2].bottom < regions.footerItems[2].top,
						"menu, all three thin separators and fixed rows keep their requested order");
					for (const auto& divider : regions.separators)
						checks.Expect(Contains(pane, divider) && Near(divider.Height(), 1),
							"separators stay within the pane and one DIP thick");
					checks.Expect(regions.debugVisible == expanded && (expanded
						? Contains(regions.footer, regions.debug) && Near(regions.debug.Height(), 28)
							&& Near(regions.debug.Width(), 72) && Near(regions.debug.left + regions.debug.Width() / 2, pane.Width() / 2)
							&& regions.debug.top > regions.footerItems[3].bottom
						: Near(regions.debug.Width(), 0) && Near(regions.debug.Height(), 0)),
						"debug is centered natural-width/28 DIP only in desktop or overlay expansion");
					checks.Expect(Near(regions.middleContentHeight, 316)
						&& Near(regions.top.Height() + regions.middle.Height() + regions.footer.Height(), pane.Height())
						&& (size.y != 520 || regions.middleContentHeight > regions.middle.Height()),
						"remaining height alone budgets the eight-item middle with overflow at minimum size");
				}
		const auto shortPane = Design::ResolveNavigationPaneGeometry(248, 240, true);
		checks.Expect(!shortPane.fits && Near(shortPane.middle.Height(), 0)
			&& Disjoint(shortPane.top, shortPane.footer),
			"abnormally short existing client explicitly reports insufficient height without fixed-region overlap");
	}

	std::filesystem::path FindFontDirectory()
	{
		std::array<wchar_t, 32768> executable{};
		const DWORD length = GetModuleFileNameW(nullptr, executable.data(),
			static_cast<DWORD>(executable.size()));
		const auto executableDirectory = length > 0 && length < executable.size()
			? std::filesystem::path(executable.data()).parent_path() : std::filesystem::path{};
		// 工作目录、编译来源和 exe 位置都能定位字库；找不到真实字库必须失败。
		for (auto root : { std::filesystem::current_path(),
			std::filesystem::absolute(std::filesystem::path(__FILE__)).parent_path(),
			executableDirectory })
		{
			for (int depth = 0; depth < 7 && !root.empty(); ++depth)
			{
				const auto fonts = root / "Inkeys" / "src" / "ttf";
				if (std::filesystem::is_regular_file(fonts / "HarmonyOS_Sans_SC_Regular.ttf"))
					return fonts;
				const auto parent = root.parent_path();
				if (parent == root) break;
				root = parent;
			}
		}
		return {};
	}

	struct FontData
	{
		const char* name;
		std::vector<char> bytes;
	};

	struct ImGuiSession
	{
		ImGuiContext* previous = ImGui::GetCurrentContext();
		ImGuiContext* context = ImGui::CreateContext();

		ImGuiSession()
		{
			ImGui::SetCurrentContext(context);
			ImFluent::ResetContext();
			auto& io = ImGui::GetIO();
			io.IniFilename = nullptr;
			io.LogFilename = nullptr;
			io.BackendFlags |= ImGuiBackendFlags_RendererHasTextures;
			io.DisplaySize = ImVec2(1440.0F, 1000.0F);
			io.DeltaTime = 1.0F / 60.0F;
			// 只启用 CPU atlas 的动态字号路径，不注册任何平台或渲染后端。
			auto& platform = ImGui::GetPlatformIO();
			platform.Renderer_TextureMaxWidth = 16384;
			platform.Renderer_TextureMaxHeight = 16384;
		}

		~ImGuiSession()
		{
			ImFluent::ResetContext();
			ImGui::DestroyContext(context);
			ImGui::SetCurrentContext(previous);
		}
	};

	float MeasureHeight(ImFont* font, const char* text,
		Design::TypographyMetrics metrics, float widthDip, float scale)
	{
		if (!text || !*text) return 0.0F;
		const auto style = metrics.size == Design::CaptionText.size
			? ImFluentTextStyle_Caption : ImFluentTextStyle_Body;
		ImFluent::SetFluentTextStyleFont(style, font, metrics.size);
		return Design::TextHeight(text, widthDip * scale, style) / scale;
	}

	void CheckMeasuredRows(Checks& checks, ImFont* font, float scale)
	{
		constexpr const char* title = "保持设置窗口与书写工具在最前端";
		constexpr const char* description =
			"较短的置顶间隔会增加资源消耗。When a presentation changes focus, "
			"keep the writing tools available without covering the setting description.";
		constexpr const char* longAction = "Create a desktop shortcut for all users";
		const float actionWidth = font->CalcTextSizeA(
			Design::BodyText.size * scale, FLT_MAX, 0.0F, longAction).x / scale + 32.0F;
		for (const float windowWidth : { 720.0F, 960.0F, 1440.0F })
		{
			Design::NavigationState navigation;
			// 布局 API 只接收 DIP；物理窗口尺寸在边界除以有效倍率一次。
			const float widthDip = windowWidth * scale / scale;
			navigation.Resize(widthDip);
			const auto shell = Design::ResolveShellGeometry(widthDip, 700.0F, navigation);
			for (const float requestedAction : { 76.0F, actionWidth, actionWidth * 2.0F + 8.0F })
			{
				const auto measure = Design::MeasureRow(shell.pageWidth, requestedAction, 32.0F);
				const float textHeight = MeasureHeight(font, title, Design::BodyText,
					measure.textWidth, scale)
					+ MeasureHeight(font, description, Design::CaptionText, measure.textWidth, scale);
				const auto row = Design::ResolveRowLayout(measure, textHeight);
				const LayoutRect bounds{ 0.0F, 0.0F, shell.pageWidth, row.height };
				checks.Expect(Contains(bounds, row.text) && Contains(bounds, row.icon)
					&& Contains(bounds, row.action) && Disjoint(row.text, row.action)
					&& Disjoint(row.text, row.icon) && Disjoint(row.icon, row.action),
					"measured Chinese and English rows contain disjoint text, icon and action");
				checks.Expect(Near(row.action.right, shell.pageWidth - measure.padding)
					&& Near(row.action.Width(), measure.actionWidth),
					"short and long actions anchor their actual right edge to the same inset");
				checks.Expect(row.stacked
					? row.action.top >= row.text.bottom + 12.0F
					: Near(row.text.top + row.text.Height() * 0.5F,
						row.action.top + row.action.Height() * 0.5F),
					"rows center inline controls or put constrained actions below text");
			}
		}

		const auto narrowMeasure = Design::MeasureRow(320.0F, actionWidth, 64.0F);
		const auto large = Design::MeasureRow(1040.0F, actionWidth, 64.0F);
		const auto smallRow = Design::ResolveRowLayout(narrowMeasure,
			MeasureHeight(font, description, Design::BodyText, narrowMeasure.textWidth, scale));
		const auto largeRow = Design::ResolveRowLayout(large,
			MeasureHeight(font, description, Design::BodyText, large.textWidth, scale));
		checks.Expect(smallRow.stacked && !largeRow.stacked && smallRow.height > largeRow.height,
			"long measured content reflows and grows at constrained content width");
		const auto textOnly = Design::MeasureRow(320.0F, 0.0F, 0.0F, false);
		const auto textOnlyRow = Design::ResolveRowLayout(textOnly,
			MeasureHeight(font, description, Design::BodyText, textOnly.textWidth, scale));
		checks.Expect(!textOnlyRow.stacked && textOnlyRow.text.left < smallRow.text.left
			&& textOnlyRow.text.Width() > smallRow.text.Width(),
			"text-only rows reclaim icon and action space");
	}

	void CheckGlyphs(Checks& checks, ImFont* font, const char* face,
		Design::TypographyMetrics metrics, float scale)
	{
		ImGui::PushFont(font, metrics.size);
		const float size = ImGui::GetFontSize();
		auto* baked = ImGui::GetFontBaked();
		float top = FLT_MAX;
		float bottom = -FLT_MAX;
		bool available = true;
		for (const auto point : U"Agpqy（）【】中文设置繁體，。")
		{
			if (point == U'\0') continue;
			const auto* glyph = baked->FindGlyphNoFallback(static_cast<ImWchar>(point));
			available &= glyph && glyph->Visible && glyph->X1 > glyph->X0 && glyph->Y1 > glyph->Y0;
			if (!glyph) continue;
			top = (std::min)(top, glyph->Y0);
			bottom = (std::max)(bottom, glyph->Y1);
		}
		checks.Expect(available, "bundled SC/TC regular and bold provide real Chinese and descender glyphs");
		checks.Expect(Near(size, metrics.size * scale, 0.501F),
			"ImGui semantic size scales once with at most half-pixel native quantization");
		const float linePadding = (metrics.lineHeight * scale - size) * 0.5F;
		const bool fits = top + linePadding >= -0.5F
			&& bottom + linePadding <= metrics.lineHeight * scale + 0.5F;
		checks.Expect(fits, "calibrated visible glyphs fit caption, body and title line boxes");
		if (!available || !fits)
			std::cerr << "  face=" << face << " size=" << metrics.size
				<< " scale=" << scale << " bounds=[" << top << ',' << bottom
				<< "] padding=" << linePadding << " line=" << metrics.lineHeight * scale << '\n';
		const auto* fullWidth = baked->FindGlyphNoFallback(L'中');
		// 全角 advance 原为 1 em；小幅光学校准只作用于字面，行框仍保持语义字号。
		checks.Expect(fullWidth && Near(fullWidth->AdvanceX,
			size * Design::TextOpticalScale, size * 0.025F),
			"full-width Chinese advance follows the optical em without double scaling");
		const auto* capital = baked->FindGlyphNoFallback(L'A');
		const auto* descender = baked->FindGlyphNoFallback(L'g');
		checks.Expect(capital && descender && descender->Y1 > capital->Y1,
			"Latin descenders survive the shared baseline correction");
		ImGui::PopFont();
	}

	void CheckTextRendering(Checks& checks, ImFont* font, float scale)
	{
		constexpr const char* text =
			"Agpqy（）【】 中文设置繁體\n"
			"较短的置顶间隔会增加资源消耗。Long English descriptions wrap "
			"without shrinking the font or cutting descenders.";
		for (const auto style : { ImFluentTextStyle_Caption, ImFluentTextStyle_Body,
			ImFluentTextStyle_Title })
		{
			const float size = style == ImFluentTextStyle_Caption ? Design::CaptionText.size
				: style == ImFluentTextStyle_Title ? Design::TitleText.size : Design::BodyText.size;
			ImFluent::SetFluentTextStyleFont(style, font, size);
			const ImVec2 origin(400.0F * scale, 400.0F * scale);
			const float width = 260.0F * scale;
			const float height = Design::TextHeight(text, width, style);
			auto* list = ImGui::GetWindowDrawList();
			const int first = list->VtxBuffer.Size;
			Design::TextAt(text, origin, width, style);
			LayoutRect ink{ FLT_MAX, FLT_MAX, -FLT_MAX, -FLT_MAX };
			for (int index = first; index < list->VtxBuffer.Size; ++index)
			{
				const auto& position = list->VtxBuffer[index].pos;
				ink.left = (std::min)(ink.left, position.x);
				ink.top = (std::min)(ink.top, position.y);
				ink.right = (std::max)(ink.right, position.x);
				ink.bottom = (std::max)(ink.bottom, position.y);
			}
			checks.Expect(ink.Width() > 0.0F && ink.top >= origin.y - 0.5F
				&& ink.bottom <= origin.y + height + 0.5F,
				"production TextHeight contains the actual multi-line TextAt vertices");
			checks.Expect(ink.left >= origin.x - scale && ink.right <= origin.x + width + scale,
				"production wrapping preserves the measured horizontal text budget");
		}
	}

	void CheckSettingRows(Checks& checks, ImFont* regular, ImFont* icons, float scale)
	{
		Design::ApplyPalette();
		ImFluent::SetFluentTextStyleFont(ImFluentTextStyle_Body, regular, Design::BodyText.size);
		ImFluent::SetFluentTextStyleFont(ImFluentTextStyle_Caption, regular, Design::CaptionText.size);
		ImFluent::SetIconFont(icons);
		for (const float widthDip : { 252.0F, 320.0F, 624.0F })
		{
			for (int kind = 0; kind < 4; ++kind)
			{
				ImGui::PushID(static_cast<int>(widthDip) * 10 + kind);
				ImGui::SetCursorScreenPos(ImVec2(760.0F * scale, 16.0F * scale));
				ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0F, 0.0F));
				ImGui::BeginChild("##measured-row", ImVec2(widthDip * scale, 400.0F * scale),
					ImGuiChildFlags_None, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
				ImFluent::PushFont(ImFluentTextStyle_Body);
				const auto origin = ImGui::GetCursorScreenPos();
				const float actualWidth = ImGui::GetContentRegionAvail().x;
				LayoutRect allocation;
				LayoutRect control;
				int calls = 0;
				bool changed = false;
				bool enabled = true;
				bool active = false;
				float zoom = 1.25F;
				int selected = 0;
				const char* options[]{ "默认设置 Agpqy", "Traditional Chinese option" };
				Design::SettingRow("general.sample", "保持工具在最前端 Agpqy",
					"较短的置顶间隔会增加资源消耗。Long English settings descriptions remain readable.",
					"\xEE\xA0\x8F", kind == 0 ? 72.0F : kind == 1 ? 76.0F : 240.0F,
					32.0F, [&](const LayoutRect& bounds)
					{
						++calls;
						allocation = bounds;
						switch (kind)
						{
						case 0:
							changed = Design::Button("重置", ImVec2(bounds.Width(), bounds.Height()));
							break;
						case 1: changed = Design::ToggleAction(bounds, enabled); break;
						case 2: changed = Design::SliderAction(bounds, zoom, 1.0F, 2.0F, active); break;
						case 3: changed = Design::ComboBox("##choice", &selected, options, 2); break;
						}
						const auto minimum = ImGui::GetItemRectMin();
						const auto maximum = ImGui::GetItemRectMax();
						control = { minimum.x, minimum.y, maximum.x, maximum.y };
					});
				const float rowBottom = ImGui::GetCursorScreenPos().y - Design::RowGap * scale;
				const LayoutRect row{ origin.x, origin.y, origin.x + actualWidth, rowBottom };
				checks.Expect(calls == 1 && !changed && enabled && !active
					&& Near(zoom, 1.25F) && selected == 0,
					"production SettingRow measures once and invokes one idle action");
				const bool actionFits = Contains(row, allocation) && Contains(allocation, control)
					// ImGui 对宽度取整；只容许一个物理像素内的量化，不容许空列导致的左偏。
					&& Near(control.right, allocation.right, 1.0F);
				checks.Expect(actionFits,
					"real button, toggle, slider and combo stay within their right-anchored allocation");
				if (!actionFits)
					std::cerr << "  scale=" << scale << " width=" << widthDip << " kind=" << kind
						<< " allocation=[" << allocation.left << ',' << allocation.top << ','
						<< allocation.right << ',' << allocation.bottom << "] control=["
						<< control.left << ',' << control.top << ',' << control.right << ','
						<< control.bottom << "] row-bottom=" << row.bottom << '\n';
				ImFluent::PopFont();
				const auto* window = ImGui::GetCurrentWindow();
				if (window->DC.CursorPos.x > window->DC.CursorMaxPos.x
					|| window->DC.CursorPos.y > window->DC.CursorMaxPos.y)
					std::cerr << "[SettingRow scope] scale=" << scale << " width=" << widthDip
						<< " kind=" << kind << " cursor=" << window->DC.CursorPos.x << ','
						<< window->DC.CursorPos.y << " max=" << window->DC.CursorMaxPos.x
						<< ',' << window->DC.CursorMaxPos.y << '\n';
				ImGui::EndChild();
				ImGui::PopStyleVar();
				ImGui::PopID();
			}
		}
	}

	LayoutRect GlyphDrawBounds(ImDrawList* list, int first,
		const ImFontGlyph& glyph)
	{
		LayoutRect bounds{ FLT_MAX, FLT_MAX, -FLT_MAX, -FLT_MAX };
		for (int index = first; index < list->VtxBuffer.Size; ++index)
		{
			const auto& vertex = list->VtxBuffer[index];
			if (vertex.uv.x < glyph.U0 || vertex.uv.x > glyph.U1
				|| vertex.uv.y < glyph.V0 || vertex.uv.y > glyph.V1) continue;
			bounds.left = (std::min)(bounds.left, vertex.pos.x);
			bounds.top = (std::min)(bounds.top, vertex.pos.y);
			bounds.right = (std::max)(bounds.right, vertex.pos.x);
			bounds.bottom = (std::max)(bounds.bottom, vertex.pos.y);
		}
		return bounds;
	}

	LayoutRect VertexBounds(ImDrawList* list, int first, ImU32 color = 0, bool filterColor = false)
	{
		LayoutRect bounds{ FLT_MAX, FLT_MAX, -FLT_MAX, -FLT_MAX };
		for (int index = first; index < list->VtxBuffer.Size; ++index)
		{
			const auto& vertex = list->VtxBuffer[index];
			if (filterColor && vertex.col != color) continue;
			bounds.left = (std::min)(bounds.left, vertex.pos.x);
			bounds.top = (std::min)(bounds.top, vertex.pos.y);
			bounds.right = (std::max)(bounds.right, vertex.pos.x);
			bounds.bottom = (std::max)(bounds.bottom, vertex.pos.y);
		}
		return bounds;
	}

	void CheckControlTypography(Checks& checks, ImFont* regular, float scale)
	{
		Design::ApplyPalette();
		ImFluent::SetFluentTextStyleFont(ImFluentTextStyle_Body, regular, Design::BodyText.size);
		ImFluent::PushFont(ImFluentTextStyle_Body);
		const auto* surroundingFont = ImGui::GetFont();
		const float surroundingSize = ImGui::GetFontSize();
		constexpr const char* label = "Agpqy 创建桌面快捷方式";
		const float bodyWidth = Design::TextWidth(label);
		const float controlWidth = Design::ControlTextWidth(label);
		const float naturalWidth = Design::ButtonWidth(label) * scale;
		ImGui::PushFont(regular, Design::ControlText.size);
		const auto* glyph = ImGui::GetFontBaked()->FindGlyphNoFallback(L'A');
		const ImFontGlyph shape = glyph ? *glyph : ImFontGlyph{};
		ImGui::PopFont();
		for (int variant = 0; variant < 3; ++variant)
		{
			ImGui::PushID(variant);
			ImGui::SetCursorScreenPos(ImVec2(32.0F * scale, (350.0F + variant * 48.0F) * scale));
			const ImVec2 requested = variant == 2 ? ImVec2(240.0F * scale, 40.0F * scale) : ImVec2(0, 0);
			auto* list = ImGui::GetWindowDrawList();
			const int first = list->VtxBuffer.Size;
			const bool clicked = variant == 1
				? Design::AccentButton(label, requested) : Design::Button(label, requested);
			const auto size = ImGui::GetItemRectSize();
			const auto ink = GlyphDrawBounds(list, first, shape);
			checks.Expect(!clicked && glyph && ink.Width() > 0.0F
				&& controlWidth < bodyWidth && size.x >= controlWidth,
				"production buttons render the independent control face below body text size");
			const float expectedWidth = variant == 2 ? requested.x : naturalWidth;
			const float expectedHeight = variant == 2 ? requested.y : 32.0F * scale;
			checks.Expect(Near(size.x, expectedWidth, 1.0F) && Near(size.y, expectedHeight, 1.0F),
				"button natural measurement matches drawing while explicit size remains intact");
			if (!Near(size.x, expectedWidth, 1.0F))
				std::cerr << "  scale=" << scale << " variant=" << variant << " expected=" << expectedWidth
					<< " actual=" << size.x << " text=" << controlWidth << '\n';
			checks.Expect(ImGui::GetFont() == surroundingFont
				&& Near(ImGui::GetFontSize(), surroundingSize),
				"control typography restores the surrounding semantic font");
			ImGui::PopID();
		}
		ImFluent::PopFont();
	}

	void CheckButtonGroupGeometry(Checks& checks)
	{
		const std::vector<float> widths{
			Design::ButtonWidth("Reset"), Design::ButtonWidth("取消"), Design::ButtonWidth("Options")
		};
		const auto wide = Design::ResolveButtonGroup(widths, 320.0F);
		const auto narrow = Design::ResolveButtonGroup(widths, 180.0F);
		checks.Expect(narrow.height > wide.height && narrow.items.size() == widths.size(),
			"natural button widths wrap into additional rows when the budget narrows");
		for (const auto* geometry : { &wide, &narrow })
		{
			const LayoutRect bounds{ 0.0F, 0.0F, geometry->width, geometry->height };
			bool contained = true;
			bool separate = true;
			bool aligned = true;
			for (std::size_t index = 0; index < geometry->items.size(); ++index)
			{
				const auto& item = geometry->items[index];
				contained &= Contains(bounds, item);
				for (std::size_t next = index + 1; next < geometry->items.size(); ++next)
					separate &= Disjoint(item, geometry->items[next]);
				if (index + 1 == geometry->items.size()
					|| !Near(item.top, geometry->items[index + 1].top))
					aligned &= Near(item.right, geometry->width);
			}
			checks.Expect(contained && separate && aligned,
				"wrapped button geometry stays disjoint and aligns each line by its last button");
		}
		const auto longButton = Design::ResolveButtonGroup(
			{ Design::ButtonWidth("Restore every user preference to the original configuration") }, 180.0F);
		checks.Expect(longButton.items.size() == 1 && longButton.width <= 180.0F
			&& longButton.items.front().Width() > 0.0F,
			"a single overlong localized button remains inside the available action width");
	}

	void CheckCompositeContainers(Checks& checks, float scale, bool verify)
	{
		constexpr const char* description =
			"Agpqy（）【】 详细说明保留完整内容。\n"
			"Long descriptions and warnings remain readable when the window is narrow. "
			"Changing the interval can increase resource consumption; review this explanation before changing it.";
		std::array<float, 2> groupHeights{};
		ImFluent::PushFont(ImFluentTextStyle_Body);
		for (int widthIndex = 0; widthIndex < 2; ++widthIndex)
		{
			const float widthDip = widthIndex == 0 ? 252.0F : 624.0F;
			for (int kind = 0; kind < 8; ++kind)
			{
				ImGui::PushID(10000 + widthIndex * 100 + kind);
				ImGui::SetCursorScreenPos(ImVec2(32.0F * scale, 20.0F * scale));
				ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0F, 0.0F));
				ImGui::BeginChild("##composite", ImVec2(widthDip * scale, 850.0F * scale),
					ImGuiChildFlags_None, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
				auto* host = ImGui::GetCurrentWindow();
				const auto origin = ImGui::GetCursorScreenPos();
				const float available = ImGui::GetContentRegionAvail().x;
				const float fontSize = ImGui::GetFontSize();
				int contentCalls = 0;
				bool activated = false;
				if (kind == 0)
				{
					activated = Design::ButtonGroupRow("actions", "Actions", nullptr, "\xee\xa0\x8f",
						{ { "reset", "Reset" }, { "cancel", "取消" }, { "options", "Options" } }) != -1;
					groupHeights[widthIndex] = (ImGui::GetCursorScreenPos().y - origin.y) / scale;
					if (verify)
						checks.Expect(Near(ImGui::GetItemRectMax().x,
							origin.x + available - Design::MeasureRow(available / scale, 0.0F, 0.0F).padding * scale, 1.0F),
							"production ButtonGroupRow anchors its final drawn button to the row inset");
				}
				else if (kind == 1 || kind == 2)
				{
					ImGui::PushID("details");
					ImGui::GetStateStorage()->SetBool(ImGui::GetID("##open"), kind == 2);
					ImGui::PopID();
					Design::Details("details", "Detailed explanation with a deliberately long localized heading",
						[&]
						{
							++contentCalls;
							const auto textOrigin = ImGui::GetCursorScreenPos();
							const float textWidth = ImGui::GetContentRegionAvail().x;
							const float measured = Design::TextHeight(description, textWidth, ImFluentTextStyle_Caption);
							auto* draw = ImGui::GetWindowDrawList();
							const int first = draw->VtxBuffer.Size;
							Design::Text(description, ImFluentTextStyle_Caption, Design::TextSecondary);
							const auto ink = VertexBounds(draw, first);
							if (verify)
								checks.Expect(ink.Width() > 0.0F
									&& ink.top >= textOrigin.y - 1.0F
									&& ink.bottom <= textOrigin.y + measured + 1.0F
									&& ink.right <= textOrigin.x + textWidth + 1.0F,
									"expanded Details contains the production multi-line description vertices");
						});
				}
				else if (kind == 3)
				{
					activated = Design::Notice("warning", "请阅读完整说明 Agpqy", description,
						ImFluentInfoSeverity_Warning, "Learn more");
					if (verify)
						checks.Expect(ImGui::GetCursorScreenPos().y - origin.y > 72.0F * scale
							&& ImGui::GetItemRectMax().x <= origin.x + available,
							"long Notice grows around its warning and right-side action");
				}
				else if (kind == 4) Design::PageContentStart();
				else if (kind == 5) Design::SectionHeader("A section can be the final item");
				else if (kind == 6)
					activated = Design::NavigationRow("navigation", "Navigate to details", description, "\xee\xa0\x8f");
				else
				{
					if (Design::BeginCard("card"))
					{
						++contentCalls;
						Design::Text(description);
					}
					Design::EndCard();
				}
				if (verify)
				{
					checks.Expect(ImGui::GetCurrentWindow() == host && !activated
						&& Near(ImGui::GetFontSize(), fontSize),
						"typed containers restore parent and font scopes without idle actions");
					if (kind == 1 || kind == 2 || kind == 7)
						checks.Expect(contentCalls == (kind == 1 ? 0 : 1),
							"closed Details skips content and open containers render it once");
				}
				// 直接结束容器，保留空分组、展开详情和末行的真实光标合同。
				ImGui::EndChild();
				ImGui::PopStyleVar();
				ImGui::PopID();
			}
		}
		if (verify)
			checks.Expect(groupHeights[0] > groupHeights[1],
				"drawn ButtonGroupRow grows after wrapping at narrow content width");
		ImFluent::PopFont();
	}

	void CheckScrollbars(Checks& checks, float scale)
	{
		Design::ApplyPalette();
		const float firstSize = ImGui::GetStyle().ScrollbarSize;
		const float firstPadding = ImGui::GetStyle().ScrollbarPadding;
		Design::ApplyScrollbars();
		Design::ApplyScrollbars();
		const auto& style = ImGui::GetStyle();
		checks.Expect(Near(firstSize, style.ScrollbarSize) && Near(firstPadding, style.ScrollbarPadding)
			&& style.Colors[ImGuiCol_ScrollbarBg].w == 0.0F,
			"repeated scrollbar setup preserves scale and keeps the track transparent");
		const auto rest = style.Colors[ImGuiCol_ScrollbarGrab];
		const auto hover = style.Colors[ImGuiCol_ScrollbarGrabHovered];
		const auto active = style.Colors[ImGuiCol_ScrollbarGrabActive];
		checks.Expect(Near(rest.x, rest.y) && Near(rest.y, rest.z)
			&& hover.w > rest.w && active.w > hover.w,
			"neutral scrollbar thumb has distinguishable hover and drag states");
		ImGui::SetCursorScreenPos(ImVec2(1000.0F * scale, 20.0F * scale));
		auto* parentDraw = ImGui::GetWindowDrawList();
		const int parentFirst = parentDraw->VtxBuffer.Size;
		ImGui::SetNextWindowContentSize(ImVec2(0.0F, 2000.0F * scale));
		ImGui::BeginChild("##scrollbar", ImVec2(200.0F * scale, 240.0F * scale),
			ImGuiChildFlags_None, ImGuiWindowFlags_AlwaysVerticalScrollbar);
		auto* window = ImGui::GetCurrentWindow();
		const auto track = ImGui::GetWindowScrollbarRect(window, ImGuiAxis_Y);
		auto thumb = VertexBounds(window->DrawList, 0,
			ImGui::GetColorU32(ImGuiCol_ScrollbarGrab), true);
		// ImGui 可把子窗口装饰合并到父 draw list，仍读取本次 BeginChild 的真实顶点。
		if (thumb.Width() <= 0.0F)
			thumb = VertexBounds(parentDraw, parentFirst, ImGui::GetColorU32(ImGuiCol_ScrollbarGrab), true);
		const bool rendered = window->ScrollbarY && Near(track.GetWidth(), 8.0F * scale, 1.0F)
			&& thumb.Width() > 0.0F && thumb.Width() < track.GetWidth()
			&& thumb.left >= track.Min.x && thumb.right <= track.Max.x;
		checks.Expect(rendered,
			"actual scrollbar uses one scaled hit track with a narrower visible thumb");
		if (!rendered)
			std::cerr << "  scale=" << scale << " scrollbar=" << window->ScrollbarY
				<< " track=[" << track.Min.x << ',' << track.Max.x << "] thumb=["
				<< thumb.left << ',' << thumb.right << "] vertices=" << window->DrawList->VtxBuffer.Size << '\n';
		ImGui::EndChild();
	}

	float Brightness(ImU32 color)
	{
		const auto rgb = ImGui::ColorConvertU32ToFloat4(color);
		return rgb.x + rgb.y + rgb.z;
	}

	void CheckThemeSwitching(Checks& checks, ImFont* regular, float scale)
	{
		const auto& light = Design::PaletteForTheme(false);
		const auto& dark = Design::PaletteForTheme(true);
		checks.Expect(Brightness(dark.appBase) < Brightness(light.appBase)
			&& Brightness(dark.card) < Brightness(light.card)
			&& Brightness(dark.textPrimary) > Brightness(dark.card)
			&& Brightness(light.textPrimary) < Brightness(light.card),
			"light and dark semantic surfaces retain opposite readable text contrast");
		const auto* activePalette = &Design::GetPalette();
		const auto* textAlias = &Design::TextPrimary;
		for (int transition = 0; transition < 3; ++transition)
		{
			const bool darkMode = transition == 1;
			const float bodyWidth = Design::TextWidth("Agpqy 中文");
			const float titleWidth = Design::TextWidth("Agpqy 中文", ImFluentTextStyle_Title);
			const float controlWidth = Design::ControlTextWidth("Agpqy 中文");
			ImGui::PushFont(regular, Design::BodyText.size);
			auto* baked = ImGui::GetFontBaked();
			baked->FindGlyphNoFallback(L'A');
			ImGui::PopFont();
			auto& io = ImGui::GetIO();
			auto* atlas = io.Fonts;
			auto* texture = atlas->TexData;
			const auto* pixels = texture->Pixels;
			const auto* defaultFont = io.FontDefault;
			const int fontCount = atlas->Fonts.Size;
			const int textureCount = atlas->TexList.Size;
			const int glyphCount = baked->Glyphs.Size;
			const float size = ImGui::GetFontSize();
			const float mainScale = ImGui::GetStyle().FontScaleMain;
			Design::ApplyPalette(darkMode);
			Design::ApplyPalette(darkMode);
			const auto& palette = Design::GetPalette();
			checks.Expect(Design::IsDarkMode() == darkMode && &palette == activePalette
				&& &Design::TextPrimary == textAlias
				&& Design::TextPrimary == palette.textPrimary && Design::Card == palette.card,
				"theme switches update the stable tokens used by default control arguments");
			checks.Expect(io.Fonts == atlas && io.FontDefault == defaultFont
				&& atlas->Fonts.Size == fontCount && atlas->TexData == texture
				&& texture->Pixels == pixels && atlas->TexList.Size == textureCount
				&& baked->Glyphs.Size == glyphCount && Near(ImGui::GetFontSize(), size)
				&& Near(ImGui::GetStyle().FontScaleDpi, scale)
				&& Near(ImGui::GetStyle().FontScaleMain, mainScale),
				"repeated theme changes keep font size, atlas pixels and existing glyph resources");
			checks.Expect(Near(Design::TextWidth("Agpqy 中文"), bodyWidth)
				&& Near(Design::TextWidth("Agpqy 中文", ImFluentTextStyle_Title), titleWidth)
				&& Near(Design::ControlTextWidth("Agpqy 中文"), controlWidth),
				"theme changes preserve the actual body, title and control font bindings");
			checks.Expect(ImFluent::GetThemePreset() ==
				(darkMode ? ImFluentThemePreset_Dark : ImFluentThemePreset_Light)
				&& ImGui::GetColorU32(ImGuiCol_TitleBgActive) == palette.appBase
				&& ImGui::GetColorU32(ImGuiCol_PopupBg) == palette.cardSecondary
				&& ImGui::GetStyle().Colors[ImGuiCol_ScrollbarBg].w == 0.0F
				&& ImGui::GetColorU32(ImGuiCol_ScrollbarGrab) == palette.scrollbar
				&& Near(ImGui::GetStyle().ScrollbarSize, 8.0F * scale),
				"caption, popup and scrollbar styles switch together without repeated scaling");

			ImGui::PushID(20000 + transition);
			ImGui::SetCursorScreenPos(ImVec2(760.0F * scale, 20.0F * scale));
			ImGui::BeginChild("##theme-controls", ImVec2(600.0F * scale, 900.0F * scale),
				ImGuiChildFlags_None, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
			auto* draw = ImGui::GetWindowDrawList();
			int first = draw->VtxBuffer.Size;
			Design::PageHeader("Appearance", "Agpqy 设置主题");
			checks.Expect(VertexBounds(draw, first, palette.textPrimary, true).Width() > 0.0F
				&& VertexBounds(draw, first, palette.textSecondary, true).Width() > 0.0F,
				"actual page heading and description use the current theme colors");
			first = draw->VtxBuffer.Size;
			const bool button = Design::Button("Theme action");
			checks.Expect(!button && VertexBounds(draw, first, palette.textPrimary, true).Width() > 0.0F
				&& VertexBounds(draw, first,
					ImGui::ColorConvertFloat4ToU32(ImFluent::GetStyle().Colors[ImFluentCol_ControlFillDefault]),
					true).Width() > 0.0F,
				"actual button fill and text follow the selected ImFluent theme");
			char text[]{ "Agpqy 中文" };
			ImGui::SetNextItemWidth(240.0F * scale);
			first = draw->VtxBuffer.Size;
			const bool textChanged = Design::TextBox("##theme-input", text, sizeof(text));
			checks.Expect(!textChanged
				&& VertexBounds(draw, first, palette.textPrimary, true).Width() > 0.0F,
				"input text remains visible with the theme's foreground");
			const char* items[]{ "Agpqy 中文", "Second option" };
			int selected = 0;
			ImGui::SetNextItemWidth(240.0F * scale);
			first = draw->VtxBuffer.Size;
			const bool choiceChanged = Design::ComboBox("##theme-choice", &selected, items, 2);
			checks.Expect(!choiceChanged && selected == 0
				&& VertexBounds(draw, first, palette.textPrimary, true).Width() > 0.0F,
				"actual combo preview changes color without changing its value");
			for (int severity = ImFluentInfoSeverity_Informational; severity <= ImFluentInfoSeverity_Critical; ++severity)
			{
				ImGui::PushID(severity);
				first = draw->VtxBuffer.Size;
				const bool notice = Design::Notice("notice", "Agpqy 状态提示",
					"Descriptions remain readable in both themes.", static_cast<ImFluentInfoSeverity>(severity));
				const auto fill = palette.notice[severity];
				checks.Expect(!notice && VertexBounds(draw, first, fill, true).Width() > 0.0F
					&& VertexBounds(draw, first, palette.textPrimary, true).Width() > 0.0F
					&& (darkMode ? Brightness(fill) < Brightness(palette.textPrimary)
						: Brightness(fill) > Brightness(palette.textPrimary)),
					"all Notice severities draw the current themed surface and readable text");
				ImGui::PopID();
			}
			// 浅/深/浅都在同一 atlas 会话中提交，末项仍直接结束 child。
			ImGui::EndChild();
			ImGui::PopID();
		}
		Design::ApplyPalette(false);
	}

	struct FlatSample
	{
		ImVec4 visible{};
		ImU32 fill = 0;
		int triangles = 0;
	};

	float Cross(ImVec2 a, ImVec2 b, ImVec2 p)
	{
		return (b.x - a.x) * (p.y - a.y) - (b.y - a.y) * (p.x - a.x);
	}

	ImVec4 Composite(ImU32 foreground, ImVec4 background)
	{
		const auto color = ImGui::ColorConvertU32ToFloat4(foreground);
		return { color.x * color.w + background.x * (1.0F - color.w),
			color.y * color.w + background.y * (1.0F - color.w),
			color.z * color.w + background.z * (1.0F - color.w), 1.0F };
	}

	// 只栅格化白像素平面三角形；样点避开文字、图标、圆角和描边。
	FlatSample SampleFlatDrawData(ImVec2 point, const ImDrawList* controlList,
		int firstVertex, int lastVertex, ImU32 background)
	{
		FlatSample result{ ImGui::ColorConvertU32ToFloat4(background) };
		const auto white = ImGui::GetIO().Fonts->TexUvWhitePixel;
		for (const auto* list : ImGui::GetDrawData()->CmdLists)
		{
			for (const auto& command : list->CmdBuffer)
			{
				if (command.UserCallback || point.x < command.ClipRect.x || point.x >= command.ClipRect.z
					|| point.y < command.ClipRect.y || point.y >= command.ClipRect.w) continue;
				for (unsigned int index = command.IdxOffset;
					index + 2 < command.IdxOffset + command.ElemCount; index += 3)
				{
					const unsigned int ia = command.VtxOffset + list->IdxBuffer[index];
					const unsigned int ib = command.VtxOffset + list->IdxBuffer[index + 1];
					const unsigned int ic = command.VtxOffset + list->IdxBuffer[index + 2];
					const auto& a = list->VtxBuffer[ia];
					const auto& b = list->VtxBuffer[ib];
					const auto& c = list->VtxBuffer[ic];
					if (a.col != b.col || b.col != c.col
						|| a.uv.x != white.x || a.uv.y != white.y
						|| b.uv.x != white.x || b.uv.y != white.y
						|| c.uv.x != white.x || c.uv.y != white.y) continue;
					const float ab = Cross(a.pos, b.pos, point);
					const float bc = Cross(b.pos, c.pos, point);
					const float ca = Cross(c.pos, a.pos, point);
					if (!((ab > 0 && bc > 0 && ca > 0) || (ab < 0 && bc < 0 && ca < 0))) continue;
					result.visible = Composite(a.col, result.visible);
					++result.triangles;
					if (list == controlList && ia >= static_cast<unsigned int>(firstVertex)
						&& ib >= static_cast<unsigned int>(firstVertex) && ic >= static_cast<unsigned int>(firstVertex)
						&& ia < static_cast<unsigned int>(lastVertex) && ib < static_cast<unsigned int>(lastVertex)
						&& ic < static_cast<unsigned int>(lastVertex)) result.fill = a.col;
				}
			}
		}
		return result;
	}

	void CheckAnimatedColors(Checks& checks, ImFont* regular, float scale)
	{
		struct Capture { ImDrawList* list = nullptr; int first = 0, last = 0; ImRect rect; };
		struct Frame { std::array<FlatSample, 5> sample{}; std::array<ImRect, 5> rect{};
			bool popup = false; ImGuiID popupId = 0; ImGuiWindow* popupWindow = nullptr; };
		std::array<int, 2> selected{ 0, 0 };
		std::array<int, 5> activations{};
		const char* options[]{ "First", "Second", "Third" };
		int frames = 0, samples = 0, violations = 0;
		std::array<int, 5> kindSamples{}, kindViolations{};
		std::array<float, 5> kindWorst{};
		float worst = 0.0F;
		auto& io = ImGui::GetIO();
		io.ConfigInputTrickleEventQueue = false;
		Design::ApplyPalette(false);
		ImFluent::SetFluentTextStyleFont(ImFluentTextStyle_Body, regular, Design::BodyText.size);
		auto draw = [&](ImVec2 mouse, bool down, bool dark)
		{
			Design::ApplyPalette(dark);
			io.AddMousePosEvent(mouse.x, mouse.y);
			io.AddMouseButtonEvent(0, down);
			ImGui::NewFrame();
			ImGui::SetNextWindowPos(ImVec2(0, 0));
			ImGui::SetNextWindowSize(ImVec2(600 * scale, 440 * scale));
			ImGui::Begin("##hover-draw-data", nullptr, ImGuiWindowFlags_NoDecoration
				| ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoMove);
			std::array<Capture, 5> capture{};
			for (int kind = 0; kind < 4; ++kind)
			{
				ImGui::SetCursorScreenPos(ImVec2(20 * scale, (20 + 64 * kind) * scale));
				auto& item = capture[kind];
				item.list = ImGui::GetWindowDrawList(); item.first = item.list->VtxBuffer.Size;
				bool activated = false;
				if (kind == 0) activated = ImFluent::NavItemEx("hover-nav", "Navigation", false, nullptr, false);
				else if (kind == 1) activated = Design::Button("Hover button", ImVec2(240 * scale, 32 * scale));
				else
				{
					ImGui::SetNextItemWidth(240 * scale);
					activated = Design::ComboBox(kind == 2 ? "##hover-combo-a" : "##hover-combo-b",
						&selected[kind - 2], options, 3);
				}
				activations[kind] += activated ? 1 : 0;
				// Popup结束会恢复combo的LastItemData，主体范围仍由实际item提供。
				item.rect = ImRect(ImGui::GetItemRectMin(), ImGui::GetItemRectMax());
				item.last = item.list->VtxBuffer.Size;
			}
			Frame frame;
			if (!ImGui::GetCurrentContext()->OpenPopupStack.empty())
			{
				const auto& popup = ImGui::GetCurrentContext()->OpenPopupStack.back();
				frame.popupId = popup.PopupId; frame.popupWindow = popup.Window;
				if (popup.Window && popup.Window->Active && !popup.Window->Hidden)
				{
					frame.popup = true;
					auto& item = capture[4];
					item.list = popup.Window->DrawList;
					item.last = item.list->VtxBuffer.Size;
					const auto start = popup.Window->DC.CursorStartPos;
					const float row = 32 * scale + ImGui::GetStyle().ItemSpacing.y;
					item.rect = ImRect(ImVec2(start.x, start.y + row),
						ImVec2(popup.Window->WorkRect.Max.x, start.y + row + 32 * scale));
				}
			}
			ImGui::End();
			ImGui::Render();
			++frames;
			for (int kind = 0; kind < 5; ++kind)
			{
				const auto& item = capture[kind]; frame.rect[kind] = item.rect;
				if (!item.list) continue;
				const ImU32 background = kind == 4
					? ImGui::GetColorU32(ImGuiCol_PopupBg) : Design::AppBase;
				const auto point = ImVec2(item.rect.Min.x + item.rect.GetWidth() * 0.62F + 0.37F,
					item.rect.Min.y + 4.61F * scale);
				frame.sample[kind] = SampleFlatDrawData(point, item.list, item.first, item.last, background);
				if (kind == 4 && frame.sample[kind].fill == background) frame.sample[kind].fill = 0;
			}
			return frame;
		};
		auto target = [](int kind, int state)
		{
			return ImFluent::GetColorU32(kind == 0 || kind == 4
				? (state == 0 ? ImFluentCol_SubtleFillTransparent
					: state == 1 ? ImFluentCol_SubtleFillSecondary : ImFluentCol_SubtleFillTertiary)
				: (state == 0 ? ImFluentCol_ControlFillDefault
					: state == 1 ? ImFluentCol_ControlFillSecondary : ImFluentCol_ControlFillTertiary));
		};
		auto verify = [&](const Frame& previous, const Frame& current, int kind, int state)
		{
			if (kind == 4 && (!previous.popup || !current.popup)) return;
			const auto background = ImGui::ColorConvertU32ToFloat4(kind == 4
				? ImGui::GetColorU32(ImGuiCol_PopupBg) : Design::AppBase);
			const auto start = Composite(previous.sample[kind].fill, background);
			const auto end = Composite(target(kind, state), background);
			const auto visible = current.sample[kind].visible;
			checks.Expect(current.sample[kind].triangles > 0, "hover samples actual clipped DrawData flat triangles");
			auto component = [](ImVec4 color, int channel) { return channel == 0 ? color.x : channel == 1 ? color.y : color.z; };
			for (int channel = 0; channel < 3; ++channel)
			{
				const float lower = (std::min)(component(start, channel), component(end, channel));
				const float upper = (std::max)(component(start, channel), component(end, channel));
				const float excess = (std::max)(lower - component(visible, channel), component(visible, channel) - upper);
				worst = (std::max)(worst, excess);
				kindWorst[kind] = (std::max)(kindWorst[kind], excess);
				if (excess > 1.01F / 255.0F)
				{
					++kindViolations[kind];
					if (violations++ == 0) std::cerr << "[HoverDrawData] first failure kind=" << kind
						<< " frame=" << frames << " scale=" << scale << " visible=" << component(visible, channel)
						<< " endpoints=" << component(start, channel) << ',' << component(end, channel) << '\n';
				}
			}
			++samples; ++kindSamples[kind];
		};
		const ImVec2 outside(-100, -100);
		auto frame = draw(outside, false, false);
		frame = draw(outside, false, false);
		for (const bool dark : { false, true, false })
		{
			for (int kind = 0; kind < 3; ++kind)
			{
				const auto mouse = frame.rect[kind].GetCenter();
				// 首次进入、快速进出、按下/释放及同一session浅深切换。
				for (int step = 0; step < 28; ++step)
				{
					const bool hover = step < 8 || (step >= 10 && step < 24);
					const bool down = kind < 2 && step >= 18 && step < 21;
					const auto next = draw(hover ? mouse : outside, down, dark);
					verify(frame, next, kind, down ? 2 : hover ? 1 : 0);
					frame = next;
				}
			}
		}
		checks.Expect(activations[0] == 3 && activations[1] == 3 && activations[2] == 0,
			"animated navigation/button release once without editing an idle combo");
		ImGuiID firstPopupId = 0;
		ImGuiWindow* firstPopupWindow = nullptr;
		for (int owner : { 2, 2, 3 })
		{
			const auto pressedBody = draw(frame.rect[owner].GetCenter(), true, false);
			verify(frame, pressedBody, owner, 2); frame = pressedBody;
			const auto openBody = draw(outside, false, false);
			verify(frame, openBody, owner, 2); frame = openBody;
			frame = draw(outside, false, false);
			checks.Expect(frame.popup && selected[owner - 2] == 0, "actual combo popup opens without changing selection");
			if (!frame.popup) continue;
			if (!firstPopupId) { firstPopupId = frame.popupId; firstPopupWindow = frame.popupWindow; }
			if (owner == 3) checks.Expect(frame.popupId != firstPopupId && frame.popupWindow != firstPopupWindow,
				"combos with identical option labels retain distinct popup storage owners");
			const auto mouse = frame.rect[4].GetCenter();
			for (int step = 0; step < 18; ++step)
			{
				const bool hover = step < 8 || step >= 10;
				const auto next = draw(hover ? mouse : outside, false, step >= 12);
				verify(frame, next, 4, hover ? 1 : 0); frame = next;
			}
			// 点击外部走真实popup关闭路径，下一轮重开仍从透明端点启动。
			frame = draw(ImVec2(520 * scale, 400 * scale), true, false);
			frame = draw(outside, false, false);
			checks.Expect(!frame.popup && selected[owner - 2] == 0, "popup close/reopen preserves combo value");
		}
		const auto pressedBody = draw(frame.rect[3].GetCenter(), true, false);
		verify(frame, pressedBody, 3, 2); frame = pressedBody;
		const auto openBody = draw(outside, false, false);
		verify(frame, openBody, 3, 2); frame = openBody;
		frame = draw(outside, false, false);
		checks.Expect(frame.popup, "actual combo must open for the final selectable press/release case");
		if (frame.popup)
		{
			const auto mouse = frame.rect[4].GetCenter();
			const auto pressed = draw(mouse, true, false);
			verify(frame, pressed, 4, 2);
			frame = draw(mouse, false, false);
			checks.Expect(selected[1] == 1 && !frame.popup && activations[3] == 1,
				"popup selectable commits one value on release and closes its owning combo");
		}
		std::cout << "[HoverDrawData] scale=" << scale << " frames=" << frames << " samples=" << samples
			<< " violations=" << violations << " worst=" << worst << '\n';
		for (int kind : { 0, 1, 2, 3, 4 })
			std::cout << "[HoverDrawData] kind=" << kind << " samples=" << kindSamples[kind]
				<< " violations=" << kindViolations[kind] << " worst=" << kindWorst[kind] << '\n';
		checks.Expect(violations == 0, "animated visible fills stay within previous and target endpoints (1 LSB)");
		Design::ApplyPalette(false);
	}

	void CheckDrawData(Checks& checks)
	{
		const auto* data = ImGui::GetDrawData();
		checks.Expect(data && data->Valid && data->TotalVtxCount > 0 && data->TotalIdxCount > 0,
			"native controls produce non-empty DrawData without a window");
		if (!data) return;
		bool valid = true;
		for (const auto* list : data->CmdLists)
		{
			for (const auto& vertex : list->VtxBuffer)
				valid &= std::isfinite(vertex.pos.x) && std::isfinite(vertex.pos.y)
					&& std::isfinite(vertex.uv.x) && std::isfinite(vertex.uv.y);
			for (const auto& command : list->CmdBuffer)
			{
				valid &= std::isfinite(command.ClipRect.x) && std::isfinite(command.ClipRect.y)
					&& std::isfinite(command.ClipRect.z) && std::isfinite(command.ClipRect.w)
					&& command.ClipRect.z >= command.ClipRect.x
					&& command.ClipRect.w >= command.ClipRect.y
					&& command.IdxOffset + command.ElemCount <= static_cast<unsigned int>(list->IdxBuffer.Size);
				for (unsigned int index = command.IdxOffset;
					index < command.IdxOffset + command.ElemCount; ++index)
					valid &= command.VtxOffset + list->IdxBuffer[static_cast<int>(index)]
						< static_cast<unsigned int>(list->VtxBuffer.Size);
			}
		}
		checks.Expect(valid, "DrawData retains finite clip rectangles and valid indexed geometry");
		bool pixels = false;
		if (data->Textures)
			for (const auto* texture : *data->Textures)
				pixels |= texture->Pixels && texture->Width > 0 && texture->Height > 0;
		checks.Expect(pixels, "bundled fonts generate CPU atlas pixels");
	}

	void CheckNavigationScrolling(Checks& checks, ImFont* regular, ImFont* icons, float scale)
	{
		Design::ApplyPalette(false);
		ImFluent::SetFluentTextStyleFont(ImFluentTextStyle_Body, regular, Design::BodyText.size);
		ImFluent::SetFluentTextStyleFont(ImFluentTextStyle_Caption, regular, Design::CaptionText.size);
		ImFluent::SetIconFont(icons);
		for (bool dark : { false, true })
		for (const ImVec2 size : { ImVec2(720, 520), ImVec2(960, 700), ImVec2(1440, 900) })
			for (bool expanded : { false, true })
			{
				Design::ApplyPalette(dark);
				Design::NavigationState state; state.Resize(size.x);
				if (state.Expanded() != expanded) state.Toggle();
				const auto shell = Design::ResolveShellGeometry(size.x, size.y, state);
				const float debugWidth = Design::TextWidth("调试软件", ImFluentTextStyle_Caption) / scale + 24;
				const auto regions = Design::ResolveNavigationPaneGeometry(shell.pane.Width(), shell.pane.Height(), expanded, 36, debugWidth);
				std::array<ImVec2, 2> fixedOrigins{};
				float maximumScroll = 0;
				for (int pass = 0; pass < 3; ++pass)
				{
					ImGui::GetIO().AddMousePosEvent(-100, -100);
					ImGui::GetIO().AddMouseButtonEvent(0, false);
					ImGui::NewFrame();
					ImGui::SetNextWindowPos(ImVec2(0, 0));
					ImGui::SetNextWindowSize(ImVec2(size.x * scale, size.y * scale));
					ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
					ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0, Design::RowGap * scale));
					ImGui::Begin("##navigation-region-tests", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoSavedSettings);
					ImGui::PushID(static_cast<int>(size.x) + (expanded ? 1 : 0));
					ImFluent::PushFont(ImFluentTextStyle_Body);
					auto begin = [&](const char* id, const LayoutRect& region, bool fixed) {
						ImGui::SetCursorPos(ImVec2((shell.pane.left + region.left) * scale, (shell.pane.top + region.top) * scale));
						ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(4 * scale, 0));
						ImGui::BeginChild(id, ImVec2(region.Width() * scale, region.Height() * scale), ImGuiChildFlags_AlwaysUseWindowPadding,
							ImGuiWindowFlags_NoScrollbar | (fixed ? ImGuiWindowFlags_NoScrollWithMouse : 0));
					};
					auto position = [&](const LayoutRect& row, float offset) {
						ImGui::SetCursorPos(ImVec2(row.left * scale, (row.top - offset) * scale));
					};
					auto end = [] { ImGui::EndChild(); ImGui::PopStyleVar(); };
					begin("top", regions.top, true);
					const auto topOrigin = ImGui::GetWindowPos();
					for (int i = 0; i < 2; ++i) {
						position(regions.topItems[i], 0); ImGui::PushID(i);
						ImFluent::NavItemEx("item", i == 0 ? "Home" : "Language", i == 0, nullptr, !expanded); ImGui::PopID();
					}
					checks.Expect(Near(ImGui::GetScrollY(), 0) && (pass == 0 || Near(ImGui::GetScrollMaxY(), 0)), "actual fixed top child has no scroll range"); end();
					ImGui::SetNextWindowScroll(ImVec2(0, pass == 2 ? maximumScroll : 0));
					begin("middle", regions.middle, false);
					int lastVertices = 0;
					for (int i = 0; i < 8; ++i) {
						ImGui::PushID(i); const int first = ImGui::GetWindowDrawList()->VtxBuffer.Size;
						ImFluent::NavItemEx("item", "Middle page", i == 7, nullptr, !expanded);
						if (i == 7) lastVertices = ImGui::GetWindowDrawList()->VtxBuffer.Size - first;
						ImGui::PopID();
					}
					if (pass == 1) maximumScroll = ImGui::GetScrollMaxY();
					if (pass == 2) checks.Expect(lastVertices > 0 && Near(ImGui::GetScrollY(), maximumScroll, 1.0F)
						&& (size.y != 520 || maximumScroll > 0), "actual middle child scroll reaches the eighth entry at minimum size"); end();
					begin("footer", regions.footer, true);
					const auto footerOrigin = ImGui::GetWindowPos();
					for (int i = 0; i < 4; ++i) {
						position(regions.footerItems[i], regions.footer.top); ImGui::PushID(i);
						if (i == 1) {
							auto* list = ImGui::GetWindowDrawList(); const int first = list->VtxBuffer.Size;
							checks.Expect(!Design::AccentNavigationItem("##sponsor", "Sponsor", false, "\ue789", !expanded), "idle sponsor has no action");
							checks.Expect(VertexBounds(list, first, ImFluent::GetColorU32(ImFluentCol_AccentFillDefault), true).Width() > 0,
								"real sponsor wrapper keeps accent fill in compact and expanded modes");
						} else if (i == 3) {
							auto* list = ImGui::GetWindowDrawList(); const int first = list->VtxBuffer.Size;
							const auto red = Design::GetPalette().criticalText;
							ImFluent::PushStyleColor(ImFluentCol_TextPrimary, red);
							ImFluent::NavItemEx("item", "Close", false, "\ue711", !expanded); ImFluent::PopStyleColor();
							checks.Expect(VertexBounds(list, first, red, true).Width() > 0,
								"real close text/icon uses the light/dark critical foreground without changing its background");
						} else ImFluent::NavItemEx("item", "Fixed action", false, nullptr, !expanded);
						ImGui::PopID();
					}
					if (regions.debugVisible) {
						position(regions.debug, regions.footer.top); auto* list = ImGui::GetWindowDrawList(); const int first = list->VtxBuffer.Size;
						checks.Expect(!Design::CaptionNavigationButton("debug", "调试软件", false,
							ImVec2(regions.debug.Width() * scale, regions.debug.Height() * scale)), "idle debug has no action");
						const auto actual = ImGui::GetItemRectSize(); int glyphVertices = 0;
						const auto white = ImGui::GetIO().Fonts->TexUvWhitePixel;
						for (int v = first; v < list->VtxBuffer.Size; ++v) if (list->VtxBuffer[v].uv.x != white.x || list->VtxBuffer[v].uv.y != white.y) ++glyphVertices;
						checks.Expect(Near(actual.y, 28 * scale, 1.0F) && Near(actual.x, debugWidth * scale, 1.0F) && glyphVertices == 16,
							"actual debug wrapper draws only four caption glyphs with natural width and 28 DIP height");
					}
					checks.Expect(Near(ImGui::GetScrollY(), 0) && (pass == 0 || Near(ImGui::GetScrollMaxY(), 0)), "actual fixed footer child has no scroll range"); end();
					if (pass == 0) fixedOrigins = { topOrigin, footerOrigin };
					else checks.Expect(Near(topOrigin.x, fixedOrigins[0].x) && Near(topOrigin.y, fixedOrigins[0].y)
						&& Near(footerOrigin.x, fixedOrigins[1].x) && Near(footerOrigin.y, fixedOrigins[1].y),
						"real fixed child origins do not move when middle content scrolls");
					checks.Expect(Near(ImGui::GetScrollY(), 0) && (pass == 0 || Near(ImGui::GetScrollMaxY(), 0)), "actual region host does not create an outer scroll range");
					ImFluent::PopFont(); ImGui::PopID(); ImGui::End(); ImGui::PopStyleVar(2); ImGui::Render();
				}
			}
	}

	void CheckSidebarIconSet(Checks& checks, ImFont* icons, float scale)
	{
		constexpr std::array<ImWchar, 14> codes{ 0xE80F, 0xF2B7, 0xE7B8, 0xEE56,
			0xE74C, 0xE70B, 0xE765, 0xEC4A, 0xE81E, 0xE946, 0xE716, 0xE789, 0xE72C, 0xE711 };
		ImFluent::SetIconFont(icons);
		for (std::size_t index = 0; index < codes.size(); ++index)
		{
			ImGui::PushFont(icons, Design::NavigationGlyphSize);
			const auto* found = ImGui::GetFontBaked()->FindGlyphNoFallback(codes[index]);
			const auto glyph = found ? *found : ImFontGlyph{};
			ImGui::PopFont();
			checks.Expect(found && glyph.Visible, "all sidebar semantic glyphs exist in the bundled icon font");
			if (!found) continue;
			char text[5]{}; ImTextCharToUtf8(text, codes[index]);
			auto* draw = ImGui::GetWindowDrawList();
			const int first = draw->VtxBuffer.Size;
			const ImVec2 min(420 * scale, (20 + 36 * static_cast<float>(index)) * scale);
			const ImVec2 max(min.x + 24 * scale, min.y + 36 * scale);
			ImFluent::DrawIcon(text, min, max, Design::NavigationGlyphSize, Design::TextPrimary);
			const auto bounds = GlyphDrawBounds(draw, first, glyph);
			checks.Expect(bounds.Width() > 0 && bounds.Height() > 0
				&& Near(bounds.left + bounds.Width() * 0.5F, (min.x + max.x) * 0.5F, 1.0F)
				&& Near(bounds.top + bounds.Height() * 0.5F, (min.y + max.y) * 0.5F, 1.0F),
				"actual semantic sidebar icons draw centered inside the unchanged glyph slot");
		}
	}

	void CheckNativeControls(Checks& checks, ImFont* regular, ImFont* icons, float scale)
	{
		Design::ApplyPalette();
		ImFluent::SetFluentTextStyleFont(ImFluentTextStyle_Body, regular, Design::BodyText.size);
		ImFluent::SetIconFont(icons);
		ImFluent::PushFont(ImFluentTextStyle_Body);
		ImGui::SetCursorScreenPos(ImVec2(32.0F * scale, 32.0F * scale));
		const bool button = Design::Button("创建快捷方式 Agpqy", ImVec2(240.0F * scale, 32.0F * scale));
		const auto buttonSize = ImGui::GetItemRectSize();
		bool enabled = true;
		ImGui::SetCursorScreenPos(ImVec2(32.0F * scale, 80.0F * scale));
		const bool toggle = Design::ToggleSwitch("##state", &enabled, "开启", "关闭");
		const auto toggleSize = ImGui::GetItemRectSize();
		float zoom = 1.25F;
		ImGui::SetCursorScreenPos(ImVec2(32.0F * scale, 120.0F * scale));
		ImGui::SetNextItemWidth(240.0F * scale);
		const bool slider = Design::Slider("##zoom", &zoom, 1.0F, 2.0F, "%.2f");
		int selected = 0;
		const char* items[]{ "默认设置 Agpqy", "繁體中文選項" };
		ImGui::SetCursorScreenPos(ImVec2(32.0F * scale, 168.0F * scale));
		ImGui::SetNextItemWidth(240.0F * scale);
		const bool combo = Design::ComboBox("##choice", &selected, items, 2);
		checks.Expect(!button && !toggle && !slider && !combo && enabled
			&& Near(zoom, 1.25F) && selected == 0,
			"measuring and drawing idle controls causes no edits");
		checks.Expect(Near(buttonSize.y, 32.0F * scale) && toggleSize.y <= buttonSize.y
			&& toggleSize.x > 40.0F * scale,
			"native button and toggle use scaled control geometry");

		ImGui::CalcTextSize("主页");
		ImGui::PushFont(icons, Design::NavigationGlyphSize);
		const auto* glyph = ImGui::GetFontBaked()->FindGlyphNoFallback(0xE80F);
		const ImFontGlyph iconShape = glyph ? *glyph : ImFontGlyph{};
		ImGui::PopFont();
		checks.Expect(glyph && glyph->Visible, "navigation uses the bundled Fluent icon face");
		if (glyph)
		{
			// 截取真实图标顶点比较展开/收起中心，不用布局常量相等冒充绘制验证。
			auto* list = ImGui::GetWindowDrawList();
			ImGui::SetCursorScreenPos(ImVec2(4.0F * scale, 230.0F * scale));
			const int openFirst = list->VtxBuffer.Size;
			ImFluent::NavItemEx("nav.open", "主页", true, "\xEE\xA0\x8F", false);
			const auto open = GlyphDrawBounds(list, openFirst, iconShape);
			ImGui::SetCursorScreenPos(ImVec2(4.0F * scale, 280.0F * scale));
			const int compactFirst = list->VtxBuffer.Size;
			ImFluent::NavItemEx("nav.compact", "主页", true, "\xEE\xA0\x8F", true);
			const auto compact = GlyphDrawBounds(list, compactFirst, iconShape);
			checks.Expect(open.Width() > 0.0F && compact.Width() > 0.0F
				&& Near(open.left + open.Width() * 0.5F,
					compact.left + compact.Width() * 0.5F, 0.1F)
				&& Near(compact.top - open.top, 50.0F * scale, 1.0F),
				"drawn navigation glyph keeps its axis when labels collapse");
		}
		ImFluent::PopFont();
	}
}

int RunSettingDesignTests()
{
	Checks checks;
	CheckNavigation(checks);
	CheckNavigationPaneGeometry(checks);
	const auto fontDirectory = FindFontDirectory();
	if (fontDirectory.empty())
	{
		checks.Expect(false, "cannot locate Inkeys/src/ttf from test source or working directory");
		return checks.failures;
	}
	std::array<FontData, 5> sources{ {
		{ "HarmonyOS_Sans_SC_Regular.ttf", {} },
		{ "HarmonyOS_SansSC_Bold.ttf", {} },
		{ "HarmonyOS_Sans_TC_Regular.ttf", {} },
		{ "HarmonyOS_SansTC_Bold.ttf", {} },
		{ "Segoe Fluent Icons.ttf", {} },
	} };
	for (auto& source : sources)
	{
		std::ifstream stream(fontDirectory / source.name, std::ios::binary);
		source.bytes.assign(std::istreambuf_iterator<char>(stream), {});
		if (source.bytes.empty())
		{
			checks.Expect(false, source.name);
			return checks.failures;
		}
	}

	// 系统 DPI 100/125/150/200%，另覆盖系统 DPI × 用户倍率的组合。
	for (const float scale : { 1.0F, 1.25F, 1.5F, 2.0F, 1.875F, 4.0F })
	{
		ImGuiSession session;
		ImGui::GetStyle().FontScaleDpi = scale;
		ImGui::GetIO().DisplaySize = ImVec2(1440.0F * scale, 1000.0F * scale);
		std::array<ImFont*, 5> fonts{};
		bool loaded = true;
		for (std::size_t index = 0; index < sources.size(); ++index)
		{
			auto config = index == sources.size() - 1
				? Design::IconFontConfig(scale) : Design::HarmonyFontConfig(scale);
			fonts[index] = ImGui::GetIO().Fonts->AddFontFromMemoryTTF(
				sources[index].bytes.data(), static_cast<int>(sources[index].bytes.size()),
				Design::FontReferenceSize, &config);
			loaded &= fonts[index] != nullptr;
			if (index == 2 || index == 3)
			{
				// 产品的繁体主字库后合并简体回退；验证相同组合，不能误把合法回退当缺字。
				config.MergeMode = true;
				auto& fallback = sources[index - 2];
				loaded &= ImGui::GetIO().Fonts->AddFontFromMemoryTTF(
					fallback.bytes.data(), static_cast<int>(fallback.bytes.size()),
					Design::FontReferenceSize, &config) != nullptr;
			}
		}
		checks.Expect(loaded, "actual bundled font sources load into ImGui");
		if (!loaded) continue;
		ImGui::GetIO().FontDefault = fonts[0];
		ImFluent::SetFluentTextStyleFont(ImFluentTextStyle_BodyStrong, fonts[1], Design::BodyText.size);
		ImGui::NewFrame();
		ImGui::SetNextWindowPos(ImVec2(0.0F, 0.0F));
		ImGui::SetNextWindowSize(ImGui::GetIO().DisplaySize);
		ImGui::Begin("##setting-cpu-tests", nullptr,
			ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoSavedSettings
			| ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollWithMouse);
		for (std::size_t index = 0; index < fonts.size() - 1; ++index)
		{
			for (const auto metrics : { Design::CaptionText, Design::BodyText,
				Design::TitleText, Design::ControlText })
				CheckGlyphs(checks, fonts[index], sources[index].name, metrics, scale);
			CheckTextRendering(checks, fonts[index], scale);
		}
		CheckMeasuredRows(checks, fonts[0], scale);
		CheckNativeControls(checks, fonts[0], fonts.back(), scale);
		CheckSidebarIconSet(checks, fonts.back(), scale);
		CheckSettingRows(checks, fonts[0], fonts.back(), scale);
		CheckControlTypography(checks, fonts[0], scale);
		CheckButtonGroupGeometry(checks);
		CheckCompositeContainers(checks, scale, false);
		ImGui::End();
		ImGui::Render();
		CheckDrawData(checks);

		// AutoResizeY 的详情和卡片先完成一帧测量，再检查真实可见内容。
		ImGui::NewFrame();
		ImGui::SetNextWindowPos(ImVec2(0.0F, 0.0F));
		ImGui::SetNextWindowSize(ImGui::GetIO().DisplaySize);
		ImGui::Begin("##setting-cpu-tests", nullptr,
			ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoSavedSettings
			| ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollWithMouse);
		CheckCompositeContainers(checks, scale, true);
		CheckScrollbars(checks, scale);
		CheckThemeSwitching(checks, fonts[0], scale);
		ImGui::End();
		ImGui::Render();
		CheckDrawData(checks);
		CheckNavigationScrolling(checks, fonts[0], fonts.back(), scale);
		if (scale == 1.0F || scale == 1.875F) CheckAnimatedColors(checks, fonts[0], scale);
	}
	return checks.failures;
}
