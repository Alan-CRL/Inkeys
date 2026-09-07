#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>

#include "../Inkeys/Inkeys/UI/Setting/Setting.Design.h"
#include "../Inkeys/Inkeys/UI/Setting/Setting.Typography.h"
#include "../Inkeys/Inkeys/UI/Setting/Setting.Controls.h"
#include "../Inkeys/additional/imfluent/imfluent.h"
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
		ImGui::End();
		ImGui::Render();
		CheckDrawData(checks);
	}
	return checks.failures;
}
