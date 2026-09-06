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
		// 此字库的中文全角 advance 为 1 em；验证校准落到真实字面而非只改字号常量。
		checks.Expect(fullWidth && Near(fullWidth->AdvanceX, size, size * 0.025F),
			"full-width Chinese advance matches the requested em without double scaling");
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
							changed = ImFluent::Button("重置", ImVec2(bounds.Width(), bounds.Height()));
							break;
						case 1: changed = Design::ToggleAction(bounds, enabled); break;
						case 2: changed = Design::SliderAction(bounds, zoom, 1.0F, 2.0F, active); break;
						case 3: changed = ImFluent::ComboBox("##choice", &selected, options, 2); break;
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
		ImFluent::SetFluentTextStyleFont(ImFluentTextStyle_Body, regular, Design::BodyText.size);
		ImFluent::SetIconFont(icons);
		ImFluent::PushFont(ImFluentTextStyle_Body);
		ImGui::SetCursorScreenPos(ImVec2(32.0F * scale, 32.0F * scale));
		const bool button = ImFluent::Button("创建快捷方式 Agpqy", ImVec2(240.0F * scale, 32.0F * scale));
		const auto buttonSize = ImGui::GetItemRectSize();
		bool enabled = true;
		ImGui::SetCursorScreenPos(ImVec2(32.0F * scale, 80.0F * scale));
		const bool toggle = ImFluent::ToggleSwitch("##state", &enabled, "开启", "关闭");
		const auto toggleSize = ImGui::GetItemRectSize();
		float zoom = 1.25F;
		ImGui::SetCursorScreenPos(ImVec2(32.0F * scale, 120.0F * scale));
		ImGui::SetNextItemWidth(240.0F * scale);
		const bool slider = ImFluent::Slider("##zoom", &zoom, 1.0F, 2.0F, "%.2f");
		int selected = 0;
		const char* items[]{ "默认设置 Agpqy", "繁體中文選項" };
		ImGui::SetCursorScreenPos(ImVec2(32.0F * scale, 168.0F * scale));
		ImGui::SetNextItemWidth(240.0F * scale);
		const bool combo = ImFluent::ComboBox("##choice", &selected, items, 2);
		checks.Expect(!button && !toggle && !slider && !combo && enabled
			&& Near(zoom, 1.25F) && selected == 0,
			"measuring and drawing idle controls causes no edits");
		checks.Expect(Near(buttonSize.y, 32.0F * scale) && toggleSize.y <= buttonSize.y
			&& toggleSize.x > 40.0F * scale,
			"native button and toggle use scaled control geometry");

		ImGui::CalcTextSize("主页");
		ImGui::PushFont(icons, 20.0F);
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
		ImGui::NewFrame();
		ImGui::SetNextWindowPos(ImVec2(0.0F, 0.0F));
		ImGui::SetNextWindowSize(ImGui::GetIO().DisplaySize);
		ImGui::Begin("##setting-cpu-tests", nullptr,
			ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoSavedSettings
			| ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollWithMouse);
		for (std::size_t index = 0; index < fonts.size() - 1; ++index)
		{
			for (const auto metrics : { Design::CaptionText, Design::BodyText, Design::TitleText })
				CheckGlyphs(checks, fonts[index], sources[index].name, metrics, scale);
			CheckTextRendering(checks, fonts[index], scale);
		}
		CheckMeasuredRows(checks, fonts[0], scale);
		CheckNativeControls(checks, fonts[0], fonts.back(), scale);
		CheckSettingRows(checks, fonts[0], fonts.back(), scale);
		ImGui::End();
		ImGui::Render();
		CheckDrawData(checks);
	}
	return checks.failures;
}
