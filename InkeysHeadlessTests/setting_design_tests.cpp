#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>

#include "../Inkeys/Inkeys/UI/Setting/Setting.Design.h"
#include "../Inkeys/Inkeys/UI/Setting/Setting.Typography.h"
#include "../Inkeys/Inkeys/UI/Setting/Setting.Controls.h"
#include "../Inkeys/Inkeys/UI/Setting/Setting.Pages.h"
#include "../Inkeys/Inkeys/UI/Setting/Setting.FrameSubmission.h"
#include "../Inkeys/Inkeys/UI/Setting/Setting.HomeBrand.h"
#include "../Inkeys/IdtI18n.h"
#include "setting_home_warp_tests.h"
#include "../Inkeys/additional/imfluent/imfluent.h"
#include "../Inkeys/additional/imfluent/imfluent_internal.h"
#include "../Inkeys/additional/imfluent/imfluent_icons.h"
#include "../Inkeys/additional/imgui/imgui_internal.h"

#include <array>
#include <cfloat>
#include <chrono>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
#include <vector>
#include <unordered_set>

// 导航测试只读取语言源的标签，不初始化产品配置、资源或异步服务。
std::string I18n::getA(std::string key)
{
	const auto found = i18n.find(key);
	return found == i18n.end() ? key : found->second;
}

bool I18n::isIdentifying(const wchar_t* language)
{
	return identifying == language;
}

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
						shell.pane.Width(), shell.pane.Height());
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
					checks.Expect(Near(regions.footer.bottom - regions.footerItems[3].bottom, 8)
						&& Near(regions.footer.Height(), 184),
						"footer ends after close action with only its original bottom inset and no debug reservation");
					checks.Expect(Near(regions.middleContentHeight, 236)
						&& Near(regions.top.Height() + regions.middle.Height() + regions.footer.Height(), pane.Height())
						&& (size.y != 520 || regions.middleContentHeight > regions.middle.Height()),
						"remaining height alone budgets the six-root middle with overflow at minimum size");
				}
		const auto shortPane = Design::ResolveNavigationPaneGeometry(248, 240);
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

	bool LoadNavigationLabels(const std::filesystem::path& fontDirectory, const char* language = "zh-CN")
	{
		std::ifstream stream(fontDirectory.parent_path() / "i18n" / (std::string(language) + ".jsonc"), std::ios::binary);
		Json::Value source;
		Json::CharReaderBuilder reader;
		std::string errors;
		if (!Json::parseFromStream(reader, stream, &source, &errors)) return false;
		I18n::i18n.clear();
		auto flatten = [&](auto&& self, const Json::Value& node, const std::string& path) -> void {
			if (node.isString()) I18n::i18n[path] = node.asString();
			else if (node.isObject())
				for (const auto& key : node.getMemberNames())
					self(self, node[key], path.empty() ? key : path + "/" + key);
		};
		flatten(flatten, source, "");
		I18n::identifying = std::wstring(language, language + std::strlen(language));
		return !I18n::i18n.empty();
	}

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

	void CheckMultilineRowAnimations(Checks& checks, const std::array<ImFont*, 5>& fonts,
		const std::filesystem::path& fontDirectory, float scale)
	{
		const std::array<const char*, 3> languages{ "zh-CN", "zh-TW", "en-US" };
		for (size_t language = 0; language < languages.size(); ++language)
		{
			std::ifstream stream(fontDirectory.parent_path() / "i18n"
				/ (std::string(languages[language]) + ".jsonc"), std::ios::binary);
			Json::Value source;
			Json::CharReaderBuilder reader;
			std::string errors;
			if (!Json::parseFromStream(reader, stream, &source, &errors))
			{
				checks.Expect(false, "responsive rows load the actual SC/TC/English localization fixtures");
				continue;
			}
			const auto& setting = source["SettingsUI"]["Regular"]["Behavior"]["TopWindow"];
			const std::string title = setting["N"].asString(), description = setting["E"].asString();
			checks.Expect(!title.empty() && !description.empty(), "top interval retains its complete localized explanation");
			Design::BindTextFonts(fonts[language == 1 ? 2 : 0], fonts[language == 1 ? 3 : 1]);
			ImFluent::SetIconFont(fonts.back());
			for (bool dark : { false, true })
			for (int kind = 0; kind < 3; ++kind)
			{
				struct Sample { float reveal = 0, target = 0, height = 0; Design::RowMeasure measure; LayoutRect description; };
				int calls = 0;
				float value = 1.25F;
				int selected = 0;
				bool diagnosed = false;
				auto draw = [&](float width, float requestedAction, bool enabled, float rate,
					bool visible = true, float delta = 1.0F / 120.0F,
					const char* windowId = "##multiline-transition") {
					Design::ApplyPalette(dark);
					Design::ConfigureRowAnimations(delta, enabled, rate);
					auto& io = ImGui::GetIO();
					io.DeltaTime = (std::max)(delta, 0.001F);
					io.DisplaySize = { 1440 * scale, 1600 * scale };
					io.AddMousePosEvent(-1000, -1000); io.AddMouseButtonEvent(0, false);
					ImGui::NewFrame();
					ImGui::SetNextWindowPos({ 0, 0 });
					ImGui::SetNextWindowSize({ width * scale, 1500 * scale });
					ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, { 0, 0 });
					ImGui::Begin(windowId, nullptr, ImGuiWindowFlags_NoDecoration
						| ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoScrollbar);
					ImGui::PopStyleVar();
					// 相同窗口中的语言/主题/操作分别使用稳定行 ID；另一个窗口验证状态隔离。
					ImGui::PushID(static_cast<int>(language) * 6 + static_cast<int>(dark) * 3 + kind);
					Sample sample;
					if (visible)
					{
						const ImVec2 origin = ImGui::GetCursorScreenPos();
						const float actualWidth = ImGui::GetContentRegionAvail().x / scale;
						const auto group = Design::ResolveButtonGroup({ Design::ButtonWidth("Restore defaults"),
							Design::ButtonWidth("Export configuration"), Design::ButtonWidth("More options") },
							(std::min)(requestedAction, Design::MeasureRow(actualWidth, FLT_MAX, 32).actionWidth));
						const float actionWidth = kind == 2 ? group.width : kind == 0
							? (std::min)(240.0F, requestedAction) : requestedAction;
						const float actionHeight = kind == 2 ? group.height : kind == 1 ? 64.0F : 32.0F;
						const int firstVertex = ImGui::GetWindowDrawList()->VtxBuffer.Size;
						int captionEnd = firstVertex;
						LayoutRect action, clip;
						std::vector<LayoutRect> controls;
						bool changed = false;
						const int previousCalls = calls;
						Design::SettingRow("responsive", title.c_str(), description.c_str(), "\ue922", actionWidth, actionHeight,
							[&](const LayoutRect& bounds) {
								++calls; action = bounds;
								// Combo 的灰色箭头不是说明字形；在唯一 action 绘制前截取文本顶点。
								captionEnd = ImGui::GetWindowDrawList()->VtxBuffer.Size;
								const auto currentClip = ImGui::GetWindowDrawList()->_ClipRectStack.back();
								clip = { currentClip.x, currentClip.y, currentClip.z, currentClip.w };
								auto record = [&] {
									const auto a = ImGui::GetItemRectMin(), b = ImGui::GetItemRectMax();
									controls.push_back({ a.x, a.y, b.x, b.y });
								};
								if (kind == 0)
								{
									bool active = false;
									changed = Design::SliderAction(bounds, value, 0.5F, 2, active); record();
									checks.Expect(!active, "responsive idle slider has no active edit");
								}
								else if (kind == 1)
								{
									const float top = bounds.top + (bounds.Height() - 32 * scale) * 0.5F;
									ImGui::SetCursorScreenPos({ bounds.left, top });
									changed |= Design::Button("Reset", { 72 * scale, 32 * scale }); record();
									ImGui::SetCursorScreenPos({ bounds.left + 80 * scale, top });
									ImGui::SetNextItemWidth(bounds.Width() - 80 * scale);
									const char* options[]{ "Medium (3s)", "Long (10s)" };
									changed |= Design::ComboBox("##choice", &selected, options, 2); record();
								}
								else
									for (size_t index = 0; index < group.items.size(); ++index)
									{
										const auto& item = group.items[index];
										ImGui::SetCursorScreenPos({ bounds.left + item.left * scale, bounds.top + item.top * scale });
										ImGui::PushID(static_cast<int>(index));
										changed |= Design::Button("Action", { item.Width() * scale, item.Height() * scale }); record();
										ImGui::PopID();
									}
								});
						checks.Expect(calls == previousCalls + 1 && !changed && Near(value, 1.25F) && selected == 0,
							"each animated frame submits exactly one idle action without changing its values");
						ImGui::PushID("responsive");
						const auto* storage = ImGui::GetStateStorage();
						sample.reveal = storage->GetFloat(ImGui::GetID("##description-value"));
						sample.target = storage->GetFloat(ImGui::GetID("##description-target"));
						ImGui::PopID();
						sample.measure = Design::MeasureRow(actualWidth, actionWidth, actionHeight);
						const auto& measure = sample.measure;
						const auto geometry = Design::ResolveMultilineRowLayout(measure,
							Design::TextHeight(title.c_str(), measure.textWidth * scale) / scale,
							Design::TextHeight(description.c_str(), Design::DescriptionWidth(measure, sample.reveal) * scale,
								ImFluentTextStyle_Caption) / scale,
							Design::TextHeight(description.c_str(), measure.textWidth * scale, ImFluentTextStyle_Caption) / scale,
							sample.reveal);
						sample.description = geometry.description; sample.height = geometry.height;
						const LayoutRect card{ 0, 0, actualWidth, geometry.height };
						checks.Expect(Contains(card, geometry.title) && Contains(card, geometry.description)
							&& Contains(card, geometry.action) && Contains(card, geometry.icon)
							&& Disjoint(geometry.title, geometry.action) && Disjoint(geometry.description, geometry.action)
							&& Disjoint(geometry.title, geometry.description) && Disjoint(geometry.icon, geometry.title)
							&& Disjoint(geometry.icon, geometry.description) && Disjoint(geometry.icon, geometry.action),
							"every live row frame contains disjoint title/description/icon/actions");
						auto physical = [&](const LayoutRect& rect) -> LayoutRect {
							return { origin.x + rect.left * scale, origin.y + rect.top * scale,
								origin.x + rect.right * scale, origin.y + rect.bottom * scale };
						};
						const auto descriptionBounds = physical(geometry.description);
						for (const auto& control : controls)
							checks.Expect(control.left >= action.left - 1 && control.top >= action.top - 1
								&& control.right <= action.right + 1 && control.bottom <= action.bottom + 1
								&& Disjoint(control, descriptionBounds) && Disjoint(control, physical(geometry.title))
								&& Disjoint(control, physical(geometry.icon)),
								"actual slider/button/combo/group rectangles fit their right-aligned action allocation");
						checks.Expect(Near(action.right, origin.x + (actualWidth - measure.padding) * scale, 1),
							"every natural action budget remains right aligned during reflow");
						const float followingTop = ImGui::GetCursorScreenPos().y;
						checks.Expect(Near(followingTop, origin.y + (geometry.height + Design::RowGap) * scale, 1),
							"downstream cursor follows the current complete wrapped-text height");
						LayoutRect ink{ FLT_MAX, FLT_MAX, -FLT_MAX, -FLT_MAX };
						const auto* list = ImGui::GetWindowDrawList();
						int captionVertices = 0;
						for (int vertex = firstVertex; vertex < captionEnd; ++vertex)
							if (list->VtxBuffer[vertex].col == Design::TextSecondary)
							{
								++captionVertices;
								const auto point = list->VtxBuffer[vertex].pos;
								ink.left = (std::min)(ink.left, point.x); ink.right = (std::max)(ink.right, point.x);
								ink.top = (std::min)(ink.top, point.y); ink.bottom = (std::max)(ink.bottom, point.y);
							}
						int visibleGlyphs = 0;
						ImFluent::PushFont(ImFluentTextStyle_Caption);
						for (const char* cursor = description.c_str(); *cursor;)
						{
							unsigned int codepoint;
							cursor += ImTextCharFromUtf8(&codepoint, cursor, nullptr);
							const auto* glyph = ImGui::GetFontBaked()->FindGlyph(static_cast<ImWchar>(codepoint));
							if (codepoint >= 32 && glyph && glyph->Visible) ++visibleGlyphs;
						}
						ImFluent::PopFont();
						const bool complete = captionVertices == visibleGlyphs * 4;
						const bool contained = ink.Width() > 0 && Contains(clip, ink)
							&& ink.left >= descriptionBounds.left - scale && ink.right <= descriptionBounds.right + scale
							&& ink.top >= descriptionBounds.top - scale && ink.bottom <= descriptionBounds.bottom + scale
							&& Disjoint(ink, action);
						checks.Expect(complete, "every non-space localized description glyph is emitted exactly once");
						checks.Expect(contained, "actual description vertices remain unclipped inside the live text envelope and avoid controls");
						if ((!complete || !contained) && !diagnosed)
						{
							diagnosed = true;
							std::cerr << "[MultilineInk] language=" << languages[language] << " scale=" << scale
								<< " dark=" << dark << " kind=" << kind << " width=" << actualWidth << " reveal=" << sample.reveal
								<< " vertices=" << captionVertices << "/" << visibleGlyphs * 4
								<< " ink=" << ink.left << ',' << ink.top << ',' << ink.right << ',' << ink.bottom << '\n';
						}
						// 下一张真实公共行也提交 item，覆盖连续卡片及末项 End 的 ImGui 占位合同。
						Design::SettingRow("following", "Following setting", nullptr, nullptr, 80, 32,
							[&](const LayoutRect& bounds) {
								checks.Expect(bounds.top >= followingTop
									&& followingTop >= origin.y + geometry.height * scale,
									"the next real card and its control start below every visible description glyph");
								checks.Expect(!Design::Button("Next", { bounds.Width(), bounds.Height() }), "following card has no idle action");
							});
						if (kind == 2)
							checks.Expect(Design::ButtonGroupRow("public-group", "Actions", nullptr, "\ue922",
								{ { "restore", "Restore defaults" }, { "export", "Export configuration" },
								{ "more", "More options" } }) == -1, "the actual public wrapping-group row creates no idle event");
					}
					ImGui::PopID(); ImGui::End(); ImGui::Render();
					return sample;
				};
				const auto baseline = draw(900, 320, false, 1);
				const float narrowWidth = baseline.measure.actionWidth + 88 + 220;
				const float wideWidth = baseline.measure.actionWidth + 88 + 340;
				const auto first = draw(narrowWidth, 320, true, 1, true, 1.0F / 120, "##multiline-first");
				checks.Expect(Near(first.reveal, 1), "first appearance resolves directly to the current responsive mode");
				draw(wideWidth, 320, false, 1);
				bool intermediate = false;
				for (int step = 1; step <= 10; ++step)
				{
					const auto sample = draw(wideWidth - 12 * step, 320, true, 1);
					intermediate |= sample.reveal > 0 && sample.reveal < 1;
				}
				checks.Expect(intermediate, "progressive window widths produce visible intermediate layouts");
				draw(wideWidth, 320, false, 1);
				Sample expanding;
				for (int frame = 0; frame < 20; ++frame)
				{
					expanding = draw(narrowWidth, 320, true, 1);
					if (expanding.reveal >= 0.65F) break;
				}
				checks.Expect(expanding.reveal >= 0.65F && expanding.reveal < 1
					&& expanding.description.Width() > expanding.measure.textWidth,
					"reversal is exercised after full-width expansion has actually begun");
				const auto reverse = draw(wideWidth, 320, true, 1);
				checks.Expect(reverse.reveal < expanding.reveal && reverse.reveal > 0 && Near(reverse.target, 0),
					"late reversal shrinks from current visible progress without resetting");
				for (int frame = 0; frame < 24; ++frame) draw(narrowWidth, 320, true, 1);
				const auto below = draw(narrowWidth, 320, true, 1);
				checks.Expect(Near(below.reveal, 1) && Near(below.description.right, narrowWidth - 16, 1)
					&& below.description.top >= 16 + below.measure.actionHeight,
					"settled fallback places the complete description below header/action across the full inner width");
				for (float budget : { 282.0F, 294.0F, 280.0F, 288.0F })
					checks.Expect(Near(draw(baseline.measure.actionWidth + 88 + budget, 320, true, 1).target, 1),
						"small width jitter inside the return margin never redirects the layout");
				const auto immediate = draw(wideWidth, 320, false, 1);
				checks.Expect(Near(immediate.reveal, 0), "animation disabled settles the real layout immediately");
				const auto beforeHide = draw(narrowWidth, 320, true, 0.1F);
				draw(narrowWidth, 320, true, 0.1F, false);
				const auto resumed = draw(narrowWidth, 320, true, 0.1F, true, 0.25F);
				checks.Expect(Near(beforeHide.reveal, resumed.reveal), "absent-page clock is excluded on the first visible frame");
				const auto zeroDelta = draw(narrowWidth, 320, true, 0.1F, true, 0);
				checks.Expect(Near(resumed.reveal, zeroDelta.reveal), "session resume delta zero excludes hidden wall-clock time");
				const auto faster = draw(narrowWidth, 320, true, 5);
				checks.Expect(faster.reveal > zeroDelta.reveal, "changing the existing speed advances from the current rendered progress");
				const float fixedWidth = baseline.measure.actionWidth + 88 + 240;
				const auto smallAction = draw(fixedWidth, 160, false, 1);
				const auto largerAction = draw(fixedWidth, 320, true, 1);
				checks.Expect(Near(smallAction.reveal, 0) && largerAction.reveal > 0 && Near(largerAction.target, 1),
					"changing only the natural action budget redirects the complete description");
				const auto stacked = draw(300, 320, true, 1);
				checks.Expect(stacked.measure.stacked && Near(stacked.reveal, 0),
					"extreme narrowing safely stacks the actual controls below the full description in the current frame");
				checks.Expect(calls > 50 && Near(value, 1.25F) && selected == 0,
					"real continuous slider/combo/tall wrapping-group frames preserve business values");
			}
		}
		Design::ApplyPalette(false);
		Design::ConfigureRowAnimations(0, false, 1);
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
				const auto containerBase = ImGui::GetStyle(); const int containerVars = GImGui->StyleVarStack.Size, containerColors = GImGui->ColorStack.Size;
				auto checkContainer = [&](bool card) {
					const auto& fluent = ImFluent::GetStyle(); const auto& current = ImGui::GetStyle();
					if (verify) checks.Expect(Near(current.WindowPadding.x, ImFluent::FluentDpx(card ? fluent.CardPadding : fluent.SpacingXLarge))
						&& Near(current.WindowPadding.y, ImFluent::FluentDpx(card ? fluent.CardPadding : fluent.SpacingLarge))
						&& ImGui::GetColorU32(ImGuiCol_ChildBg) == ImFluent::GetColorU32(ImFluentCol_CardBgDefault)
						&& GImGui->StyleVarStack.Size == containerVars + (card ? 3 : 2) && GImGui->ColorStack.Size == containerColors + (card ? 2 : 1)
						&& current.ScrollbarSize == containerBase.ScrollbarSize && current.ScrollbarPadding == containerBase.ScrollbarPadding
						&& std::memcmp(&current.Colors[ImGuiCol_ScrollbarGrab], &containerBase.Colors[ImGuiCol_ScrollbarGrab], sizeof(ImVec4) * 3) == 0,
						"inside real Card/Details content the vendor padding/background stacks and independent scrollbar fields remain intact");
				};
				auto nestedContainer = [&](bool card) {
					checkContainer(card);
					Design::BeginScrollChild("##container-scroll", { 0, 60 * scale });
					ImGui::Dummy({ 0, 120 * scale }); ImGui::EndChild();
					checkContainer(card);
				};
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
							nestedContainer(false);
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
						nestedContainer(true);
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

	void CheckGeneralSections(Checks& checks, ImFont* regular, ImFont* strong, ImFont* icons, float scale)
	{
		Design::BindTextFonts(regular, strong); ImFluent::SetIconFont(icons); Design::ApplyPalette(false);
		Design::GeneralDraft draft{ true, 1.37F, 1.25F, false, 5, true, 2, false, false, 4.7F, true };
		auto& io = ImGui::GetIO(); io.DisplaySize = ImVec2(1100 * scale, 1100 * scale);
		struct Frame { Design::GeneralEvents events; LayoutRect lastItem; float height = 0; int vertices = 0; };
		auto draw = [&](Design::GeneralSection section, ImVec2 mouse, bool down)
		{
			io.AddMousePosEvent(mouse.x, mouse.y); io.AddMouseButtonEvent(0, down);
			ImGui::NewFrame(); ImGui::SetNextWindowPos(ImVec2(0, 0)); ImGui::SetNextWindowSize(io.DisplaySize);
			ImGui::Begin("##general-section-cpu", nullptr, ImGuiWindowFlags_NoDecoration
				| ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoMove);
			ImFluent::PushFont(ImFluentTextStyle_Body); ImGui::PushID(static_cast<int>(section));
			Frame frame; frame.events = Design::RenderGeneral(draft, section);
			frame.lastItem = { ImGui::GetItemRectMin().x, ImGui::GetItemRectMin().y,
				ImGui::GetItemRectMax().x, ImGui::GetItemRectMax().y };
			frame.height = ImGui::GetCursorPosY();
			ImGui::PopID(); ImFluent::PopFont(); ImGui::End(); ImGui::Render();
			frame.vertices = ImGui::GetDrawData()->TotalVtxCount;
			return frame;
		};
		auto noEdits = [](const Design::GeneralEvents& events) {
			return !events.startupChanged && !events.createShortcut && !events.shortcutOptions
				&& !events.barZoomChanged && !events.barZoomActive && !events.settingZoomActive
				&& !events.edgeLightingChanged && !events.topIntervalChanged && !events.rightClickCloseChanged
				&& !events.safetyModeChanged && !events.startupPreviewChanged && !events.animationChanged
				&& !events.animationRateActive && !events.dynamicEdgeLightingChanged;
		};
		const ImVec2 outside(-100, -100);
		for (auto section : { Design::GeneralSection::Basic, Design::GeneralSection::Appearance, Design::GeneralSection::Interaction })
		{
			draw(section, outside, false); const auto frame = draw(section, outside, false);
			checks.Expect(frame.vertices > 0 && noEdits(frame.events), "actual general subpage renders without creating idle edits");
			checks.Expect(draft.startup && Near(draft.barZoom, 1.37F) && Near(draft.settingZoom, 1.25F)
				&& !draft.edgeLighting && draft.topInterval == 5 && draft.rightClickClose && draft.safetyMode == 2
				&& !draft.startupPreview && !draft.animation && Near(draft.animationRate, 4.7F) && draft.dynamicEdgeLighting,
				"page filtering and conditional hidden controls preserve every resident draft value");
		}
		const auto basic = draw(Design::GeneralSection::Basic, outside, false);
		const ImVec2 toggle((basic.lastItem.left + basic.lastItem.right) / 2, (basic.lastItem.top + basic.lastItem.bottom) / 2);
		draw(Design::GeneralSection::Basic, toggle, false); draw(Design::GeneralSection::Basic, toggle, true);
		const auto released = draw(Design::GeneralSection::Basic, toggle, false);
		checks.Expect(draft.startupPreview && released.events.startupPreviewChanged && !released.events.animationChanged
			&& !draft.animation && Near(draft.animationRate, 4.7F),
			"real basic-page startup-preview toggle changes only its existing startup field");
		auto hidden = draw(Design::GeneralSection::Appearance, outside, false);
		draft.animation = true; draft.edgeLighting = true;
		draw(Design::GeneralSection::Appearance, outside, false);
		const auto shown = draw(Design::GeneralSection::Appearance, outside, false);
		checks.Expect(shown.height > hidden.height + 100 * scale && draft.dynamicEdgeLighting && Near(draft.animationRate, 4.7F),
			"actual appearance page reveals rate and dynamic-light controls without resetting their stored values");
		draft.animation = false; draft.edgeLighting = false;
		const auto appearance = draw(Design::GeneralSection::Appearance, outside, false);
		const ImVec2 slider(appearance.lastItem.left + appearance.lastItem.Width() * 0.75F,
			(appearance.lastItem.top + appearance.lastItem.bottom) / 2);
		draw(Design::GeneralSection::Appearance, slider, false);
		const auto dragging = draw(Design::GeneralSection::Appearance, slider, true);
		const auto committed = draw(Design::GeneralSection::Appearance, slider, false);
		checks.Expect(dragging.events.settingZoomActive && !committed.events.settingZoomActive
			&& draft.settingZoom >= 1 && draft.settingZoom <= 2 && !Near(draft.settingZoom, 1.25F)
			&& Near(draft.barZoom, 1.37F),
			"actual appearance slider exposes drag/release state and edits only its existing zoom draft");
		checks.Expect(Near(draft.animationRate, 4.7F) && draft.dynamicEdgeLighting && draft.startupPreview,
			"conditional children remain intact after subpage controls are edited");
		io.DisplaySize = ImVec2(1440 * scale, 1000 * scale);
	}

	void CheckHierarchicalNavigation(Checks& checks, ImFont* regular, ImFont* strong, ImFont* icons, float scale)
	{
		using Design::GeneralSection;
		using Design::NavigationActionKind;
		using Design::PageId;
		struct Frame
		{
			Design::NavigationAction action;
			Design::ShellGeometry shell;
			ImGuiWindow* list = nullptr;
			ImGuiWindow* parentWindow = nullptr;
			ImGuiWindow* group = nullptr;
			ImGuiWindow* footer = nullptr;
			LayoutRect indicator{ FLT_MAX, FLT_MAX, -FLT_MAX, -FLT_MAX };
			bool indicatorDrawn = false;
			std::vector<ImVec2> indicatorPoints;
			int sponsorBlueText = 0, sponsorOtherIcon = 0, sponsorBlueIcon = 0;
			FlatSample sponsorFill, menuFill;
			LayoutRect menu;
			ImGuiWindow* top = nullptr; unsigned int menuAlpha = 0;
			std::array<ImVec2, 2> fixedOrigins{};
			float paneWidth = 0, scroll = 0, maximumScroll = 0, contentAlpha = 1;
			unsigned int glyphAlpha = 0, flatAlpha = 0;
			LayoutRect body, debug;
			int vertices = 0;
		};
		auto& io = ImGui::GetIO();
		const auto previousFlags = io.ConfigFlags;
		io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
		io.ConfigInputTrickleEventQueue = false;
		Design::BindTextFonts(regular, strong);
		ImFluent::SetIconFont(icons);
		Design::NavigationState state;
		Design::NavigationDestination selected;
		ImVec2 size(960, 700);
		bool animated = false, dark = false;
		float rate = 1;
		float frameDelta = 1.0F / 60.0F;
		float constrainedFlyoutBottom = 0;
		int frames = 0, navigations = 0, businessActions = 0, bodyActions = 0, bodyDraws = 0;
		float requestedScroll = -1;
		const ImVec2 outside(-100, -100);
		auto center = [](const LayoutRect& bounds) { return ImVec2((bounds.left + bounds.right) / 2,
			(bounds.top + bounds.bottom) / 2); };
		auto draw = [&](ImVec2 mouse, bool down)
		{
			Design::ApplyPalette(dark);
			io.DisplaySize = ImVec2(size.x * scale, size.y * scale);
			io.DeltaTime = frameDelta;
			io.AddMousePosEvent(mouse.x, mouse.y); io.AddMouseButtonEvent(0, down);
			state.Resize(size.x);
			Design::AdvanceNavigationAnimations(state, io.DeltaTime, animated, rate);
			Frame frame; frame.shell = Design::ResolveShellGeometry(size.x, size.y, state);
			if (constrainedFlyoutBottom > 0) frame.shell.content.bottom = constrainedFlyoutBottom;
			frame.contentAlpha = Design::ContentAlpha(state);
			ImGui::NewFrame(); Design::BeginDragScrolling(state);
			Design::ConfigureRowAnimations(io.DeltaTime, animated, rate);
			ImGui::SetNextWindowPos(ImVec2(0, 0)); ImGui::SetNextWindowSize(io.DisplaySize);
			ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
			ImGui::Begin("##hierarchical-navigation-cpu", nullptr, ImGuiWindowFlags_NoDecoration
				| ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar
				| ImGuiWindowFlags_NoScrollWithMouse);
			ImFluent::PushFont(ImFluentTextStyle_Body);
			const auto title = ResolveTitleBarGeometry(size.x * scale, scale, 46 * scale, 100 * scale, 80 * scale);
			frame.debug = title.debug;
			if (Design::RenderDebugButton(title.debug, selected.page == PageId::Debug, state))
			{
				if (selected.page != PageId::Debug) ++navigations;
				selected = { PageId::Debug }; Design::NotifyNavigationDestination(state, selected);
			}
			// 同产品一样先提交一份正文；菜单关闭的鼠标释放由导航屏障阻止穿透。
			ImGui::SetCursorScreenPos(ImVec2((frame.shell.content.left + 64) * scale, 180 * scale));
			ImGui::BeginChild("##hierarchy-body", ImVec2(320 * scale, 100 * scale), ImGuiChildFlags_None,
				ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
			auto* bodyWindow = ImGui::GetCurrentWindow();
			Design::TextAt("正文 Agpqy", ImGui::GetCursorScreenPos(), 240 * scale, ImFluentTextStyle_Body);
			ImGui::SetCursorPos(ImVec2(8 * scale, 40 * scale));
			ImGui::BeginDisabled(state.BlockContentInput());
			bodyActions += Design::Button("Body action", ImVec2(160 * scale, 32 * scale)) ? 1 : 0;
			frame.body = { ImGui::GetItemRectMin().x, ImGui::GetItemRectMin().y,
				ImGui::GetItemRectMax().x, ImGui::GetItemRectMax().y };
			++bodyDraws;
			ImGui::EndDisabled(); ImGui::EndChild();
			Design::ApplyContentAlpha(bodyWindow, frame.contentAlpha);
			const auto white = io.Fonts->TexUvWhitePixel;
			for (const auto& vertex : bodyWindow->DrawList->VtxBuffer)
			{
				const unsigned int alpha = (vertex.col >> IM_COL32_A_SHIFT) & 0xff;
				if (vertex.uv.x == white.x && vertex.uv.y == white.y) frame.flatAlpha = (std::max)(frame.flatAlpha, alpha);
				else frame.glyphAlpha = (std::max)(frame.glyphAlpha, alpha);
			}
			frame.action = Design::RenderNavigationPane(frame.shell, state, selected);
			if (frame.action.kind == NavigationActionKind::Navigate)
			{
				++navigations; selected = Design::NormalizeDestination(frame.action.destination);
				Design::NotifyNavigationDestination(state, selected);
			}
			else if (frame.action.kind != NavigationActionKind::None) ++businessActions;
			for (auto* window : ImGui::GetCurrentContext()->Windows)
			{
				if (!window->Active || window->RootWindow != ImGui::GetCurrentWindow()) continue;
				const std::string name(window->Name);
				// ImGui child 名含父路径，必须先识别更深的子列表，不能误把它当中段滚动窗。
				if (name.find("##settings-general-children") != std::string::npos) frame.group = window;
				else if (name.find("##settings-nav-list") != std::string::npos)
				{
					frame.list = window; frame.scroll = window->Scroll.y; frame.maximumScroll = window->ScrollMax.y;
					if (requestedScroll >= 0) ImGui::SetScrollY(window, requestedScroll);
				}
				else if (name.find("##settings-nav-top") != std::string::npos) { frame.fixedOrigins[0] = window->Pos; frame.top = window; }
				else if (name.find("##settings-nav-footer") != std::string::npos)
				{ frame.fixedOrigins[1] = window->Pos; frame.footer = window; }
				else if (name.find("##settings-pane") != std::string::npos) frame.paneWidth = window->Size.x;
			}
			frame.parentWindow = frame.list;
			ImFluent::PopFont(); ImGui::End(); ImGui::PopStyleVar();
			Design::ApplyDragScrolling(state); ImGui::Render();
			++frames; frame.vertices = ImGui::GetDrawData()->TotalVtxCount;
			checks.Expect(ImGui::GetDrawData()->Valid && frame.vertices > 0,
				"production hierarchy renders real controls every CPU frame");
			bool finite = true;
			for (const auto* list : ImGui::GetDrawData()->CmdLists)
			{
				for (const auto& vertex : list->VtxBuffer)
					finite &= std::isfinite(vertex.pos.x) && std::isfinite(vertex.pos.y);
				for (const auto& command : list->CmdBuffer)
					finite &= std::isfinite(command.ClipRect.x) && std::isfinite(command.ClipRect.y)
						&& command.ClipRect.z >= command.ClipRect.x && command.ClipRect.w >= command.ClipRect.y;
			}
			checks.Expect(finite, "continuous hierarchy DrawData retains finite bounded geometry");
			const ImU32 blue = ImFluent::GetColorU32(ImFluentCol_AccentFillDefault);
			for (auto* window : ImGui::GetCurrentContext()->Windows)
			{
				if (!window->Active || std::string(window->Name).find("##settings-nav-") == std::string::npos) continue;
				for (const auto& vertex : window->DrawList->VtxBuffer)
				{
					if (vertex.col != blue || vertex.uv.x != white.x || vertex.uv.y != white.y) continue;
					frame.indicatorDrawn = true;
					frame.indicatorPoints.push_back(vertex.pos);
					frame.indicator.left = (std::min)(frame.indicator.left, vertex.pos.x);
					frame.indicator.top = (std::min)(frame.indicator.top, vertex.pos.y);
					frame.indicator.right = (std::max)(frame.indicator.right, vertex.pos.x);
					frame.indicator.bottom = (std::max)(frame.indicator.bottom, vertex.pos.y);
				}
			}
			if (frame.list)
			{
				const auto track = ImGui::GetWindowScrollbarRect(frame.list, ImGuiAxis_Y);
				const float right = frame.list->ScrollbarY ? track.Min.x - Design::RowGap * scale : frame.list->ContentRegionRect.Max.x;
				bool separated = !frame.list->ScrollbarX;
				for (const auto& bounds : state.middleBounds) separated &= bounds.right <= right + 1;
				if (state.chevronBounds.Width() > 0) separated &= state.chevronBounds.right <= right + 1;
				if (frame.group) for (const auto& bounds : state.childBounds) separated &= bounds.right <= right + 1;
				checks.Expect(separated, "all production middle roots, arrows and inline children leave the actual scrollbar hit track and gap clear");
				checks.Expect(Near(state.middleBounds.back().right, frame.list->ContentRegionRect.Max.x, 1),
					"navigation hit rectangles use the same current-frame available width with or without the scrollbar");
				if (frame.list->ScrollbarY && track.GetWidth() > 0.0F)
				{
					unsigned alpha = 0;
					for (auto* list : ImGui::GetDrawData()->CmdLists) for (const auto& vertex : list->VtxBuffer)
						if (vertex.pos.x >= track.Min.x && vertex.pos.x <= track.Max.x && vertex.pos.y >= track.Min.y && vertex.pos.y <= track.Max.y)
							for (auto color : { ImGuiCol_ScrollbarGrab, ImGuiCol_ScrollbarGrabHovered, ImGuiCol_ScrollbarGrabActive })
								if ((vertex.col & ~IM_COL32_A_MASK) == (ImGui::GetColorU32(color) & ~IM_COL32_A_MASK)) alpha = (std::max)(alpha, (vertex.col >> IM_COL32_A_SHIFT) & 255);
					checks.Expect(alpha > 0 && track.GetWidth() <= Design::ScrollbarTrackSize * scale + 1,
						"actual production navigation draws the themed thumb at its current animated track width and alpha");
				}
			}
			if (frame.top)
			{
				const auto regions = Design::ResolveNavigationPaneGeometry(frame.shell.pane.Width(), frame.shell.pane.Height(), 36);
				const auto origin = frame.top->Pos;
				frame.menu = { origin.x + regions.menu.left * scale, origin.y + regions.menu.top * scale,
					origin.x + regions.menu.right * scale, origin.y + regions.menu.bottom * scale };
				frame.menuFill = SampleFlatDrawData({ frame.menu.left + 29.37F * scale, frame.menu.top + 4.61F * scale },
					frame.top->DrawList, 0, frame.top->DrawList->VtxBuffer.Size, Design::AppBase);
				for (const auto& vertex : frame.top->DrawList->VtxBuffer)
					if (vertex.uv.x == white.x && vertex.uv.y == white.y && frame.menu.Contains(vertex.pos.x, vertex.pos.y))
						frame.menuAlpha = (std::max)(frame.menuAlpha, (vertex.col >> IM_COL32_A_SHIFT) & 0xff);
			}
			if (frame.footer)
			{
				const auto& sponsor = state.footerBounds[1];
				const ImU32 textBlue = ImFluent::GetColorU32(ImFluentCol_AccentFillDefault);
				for (const auto& vertex : frame.footer->DrawList->VtxBuffer)
				{
					if (!sponsor.Contains(vertex.pos.x, vertex.pos.y)
						|| (vertex.uv.x == white.x && vertex.uv.y == white.y)) continue;
					if (vertex.pos.x >= sponsor.left + 44 * scale && vertex.col == textBlue) ++frame.sponsorBlueText;
					if (vertex.pos.x < sponsor.left + 40 * scale)
					{
						frame.sponsorOtherIcon += vertex.col != textBlue ? 1 : 0;
						frame.sponsorBlueIcon += vertex.col == textBlue ? 1 : 0;
					}
				}
				frame.sponsorFill = SampleFlatDrawData({ sponsor.right - 10 * scale, sponsor.top + 21 * scale },
					frame.footer->DrawList, 0, frame.footer->DrawList->VtxBuffer.Size, Design::AppBase);
			}
			return frame;
		};
		auto idle = [&](int count = 3) { Frame frame; for (int i = 0; i < count; ++i) frame = draw(outside, false); return frame; };
		auto click = [&](const LayoutRect& bounds) {
			const auto mouse = center(bounds); draw(mouse, false); draw(mouse, true); return draw(mouse, false);
		};
		auto key = [&](ImGuiKey pressed) {
			io.AddKeyEvent(pressed, true); draw(outside, false);
			io.AddKeyEvent(pressed, false); return draw(outside, false);
		};
		auto focusParent = [&](const Frame& frame) {
			if (!frame.parentWindow) return;
			ImGui::FocusWindow(frame.parentWindow);
			ImGui::SetNavID(state.parentItemId, ImGuiNavLayer_Main, frame.parentWindow->NavRootFocusScopeId,
				ImRect(ImVec2(state.parentBounds.left - frame.parentWindow->Pos.x, state.parentBounds.top - frame.parentWindow->Pos.y),
					ImVec2(state.parentBounds.right - frame.parentWindow->Pos.x, state.parentBounds.bottom - frame.parentWindow->Pos.y)));
			ImGui::GetCurrentContext()->NavCursorVisible = true;
		};
		checks.Expect(!state.generalExpanded && Near(state.groupReveal.value, 0),
			"a new resident navigation state starts with its child group folded");
		Frame frame = draw(outside, false);
		checks.Expect(!state.generalExpanded && Near(state.groupReveal.value, 0)
			&& !frame.group && state.childBounds[0].Height() == 0,
			"the first real draw has no expanded child window or one-frame reveal flash");
		frame = idle(2);
		if (state.parentBounds.Width() <= 0 || state.chevronBounds.Width() <= 0
			|| !Disjoint(state.parentBounds, state.chevronBounds))
			std::cerr << "[HierarchyRect] scale=" << scale << " parent=" << state.parentBounds.left << ','
				<< state.parentBounds.top << ',' << state.parentBounds.right << ',' << state.parentBounds.bottom
				<< " chevron=" << state.chevronBounds.left << ',' << state.chevronBounds.top << ','
				<< state.chevronBounds.right << ',' << state.chevronBounds.bottom << '\n';
		checks.Expect(state.parentBounds.Width() > 0 && state.chevronBounds.Width() > 0
			&& Disjoint(state.parentBounds, state.chevronBounds),
			"actual parent label and chevron have separate hit rectangles");
		const int beforeArrow = navigations;
		click(state.chevronBounds); frame = idle();
		checks.Expect(state.generalExpanded && navigations == beforeArrow && selected.page == PageId::Home,
			"real chevron expands the default folded group without routing the current page");
		click(state.chevronBounds); frame = idle();
		checks.Expect(!state.generalExpanded && navigations == beforeArrow && selected.page == PageId::Home,
			"real chevron folds the group without routing the current page");
		click(state.parentBounds); frame = idle();
		checks.Expect(state.generalExpanded && selected.page == PageId::General && selected.section == GeneralSection::Basic,
			"real parent label enters default basic and expands the group");
		click(state.childBounds[1]); frame = idle();
		checks.Expect(selected.section == GeneralSection::Appearance && state.lastGeneralSection == GeneralSection::Appearance,
			"real inline child commits its composite destination and remembered child");
		const int beforeAppearanceFold = navigations;
		click(state.parentBounds); frame = idle();
		checks.Expect(!state.generalExpanded && selected.section == GeneralSection::Appearance
			&& state.lastGeneralSection == GeneralSection::Appearance && navigations == beforeAppearanceFold,
			"repeated parent click folds appearance without routing or changing the remembered child");
		click(state.parentBounds); frame = idle();
		checks.Expect(state.generalExpanded && selected.section == GeneralSection::Appearance
			&& navigations == beforeAppearanceFold,
			"parent label reopens its remembered appearance page without redundant navigation");
		click(state.childBounds[3]); frame = idle();
		focusParent(frame); key(ImGuiKey_DownArrow); frame = idle();
		checks.Expect(state.childFocusId != 0 && ImGui::GetCurrentContext()->NavId == state.childFocusId,
			"a real child owns keyboard focus before its parent is clicked to fold");
		const int beforeStorageFold = navigations;
		click(state.parentBounds); frame = idle();
		checks.Expect(!state.generalExpanded && selected.section == GeneralSection::Storage
			&& state.lastGeneralSection == GeneralSection::Storage && navigations == beforeStorageFold
			&& ImGui::GetCurrentContext()->NavId == state.parentItemId,
			"repeated parent click also preserves the storage page rather than falling back to basic");
		click(state.parentBounds); frame = idle();
		checks.Expect(state.generalExpanded && selected.section == GeneralSection::Storage
			&& navigations == beforeStorageFold,
			"reopening the parent restores storage without changing its body destination");
		click(state.childBounds[1]); frame = idle();
		focusParent(frame); key(ImGuiKey_LeftArrow); frame = idle();
		checks.Expect(!state.generalExpanded && selected.section == GeneralSection::Appearance,
			"keyboard left folds without changing the remembered page");
		focusParent(frame); key(ImGuiKey_RightArrow); frame = idle();
		checks.Expect(state.generalExpanded, "keyboard right opens the focused parent group");
		const int beforeKeyboardFold = navigations;
		focusParent(frame); key(ImGuiKey_Enter); frame = idle();
		checks.Expect(selected.page == PageId::General && !state.generalExpanded
			&& selected.section == GeneralSection::Appearance && navigations == beforeKeyboardFold
			&& ImGui::GetCurrentContext()->NavId == state.parentItemId,
			"keyboard enter on an expanded parent folds it without navigating its body");
		focusParent(frame); key(ImGuiKey_Space); frame = idle();
		checks.Expect(state.generalExpanded && selected.section == GeneralSection::Appearance
			&& navigations == beforeKeyboardFold && ImGui::GetCurrentContext()->NavId == state.parentItemId,
			"keyboard space reopens the parent with the same remembered child");
		// 点击动画中完全被裁掉的子项区域，不能激活该子项；同一区域的可见根项允许正常响应。
		state.generalExpanded = false; frame = idle();
		animated = true; rate = 0.1F; state.generalExpanded = true; frame = draw(outside, false);
		checks.Expect(frame.group && state.childBounds[3].top >= frame.group->InnerClipRect.Max.y,
			"real opening child-window clip excludes the storage item at the beginning of the animation");
		click(state.childBounds[3]); frame = idle();
		checks.Expect(selected != Design::NavigationDestination{ PageId::General, GeneralSection::Storage },
			"clicking the fully clipped storage item cannot activate an invisible general child");
		if (frame.group)
		{
			LayoutRect visibleBasic = state.childBounds[0];
			visibleBasic.top = (std::max)(visibleBasic.top, frame.group->InnerClipRect.Min.y);
			visibleBasic.bottom = (std::min)(visibleBasic.bottom, frame.group->InnerClipRect.Max.y);
			checks.Expect(visibleBasic.Height() > 0, "the real clip retains a visible portion of the basic item");
			click(visibleBasic); frame = idle();
			checks.Expect(selected == Design::NavigationDestination{ PageId::General, GeneralSection::Basic },
				"the visible clipped portion of a real child still accepts mouse activation");
		}
		rate = 5; frame = idle(5); animated = false; frame = idle();
		click(state.childBounds[1]); frame = idle();
		state.Toggle(); selected = { PageId::Home, GeneralSection::Basic }; Design::NotifyNavigationDestination(state, selected);
		frame = idle();
		click(state.parentBounds); frame = idle();
		checks.Expect(selected.section == GeneralSection::Appearance && !state.flyoutOpen,
			"compact first click navigates to remembered child without opening a menu");
		const int beforeMenu = navigations;
		click(state.parentBounds); frame = idle();
		checks.Expect(state.flyoutOpen && navigations == beforeMenu && state.flyoutBounds.Width() > 0,
			"compact second click opens actual popup without redundant navigation");
		const LayoutRect client{ 0, 0, size.x * scale, size.y * scale };
		checks.Expect(Contains(client, state.flyoutBounds) && Near(state.flyoutBounds.Width(), 248 * scale, 1)
			&& state.flyoutBounds.top >= state.parentBounds.bottom,
			"actual child menu is 248 DIP and opens below its anchor within the client");
		click(state.parentBounds); frame = idle();
		checks.Expect(!state.flyoutOpen && navigations == beforeMenu,
			"compact third click closes the popup without navigating");
		click(state.parentBounds); frame = idle();
		click(state.flyoutChildBounds[3]); frame = idle();
		checks.Expect(!state.flyoutOpen && selected.page == PageId::General && selected.section == GeneralSection::Storage,
			"real popup child selects storage and closes the popup");
		click(state.parentBounds); frame = idle();
		const auto storageFocus = ImGui::GetCurrentContext()->NavId;
		key(ImGuiKey_UpArrow); frame = idle();
		const auto interactionFocus = ImGui::GetCurrentContext()->NavId;
		checks.Expect(interactionFocus != 0 && interactionFocus != storageFocus
			&& selected.section == GeneralSection::Storage,
			"keyboard up moves real flyout focus without navigating its body");
		key(ImGuiKey_UpArrow); frame = idle();
		checks.Expect(ImGui::GetCurrentContext()->NavId != interactionFocus,
			"a second up moves focus to the next visible flyout item");
		key(ImGuiKey_DownArrow); frame = idle();
		checks.Expect(ImGui::GetCurrentContext()->NavId == interactionFocus
			&& selected.section == GeneralSection::Storage,
			"keyboard down returns to the observed flyout item without activating it");
		key(ImGuiKey_Space); frame = idle();
		checks.Expect(!state.flyoutOpen && selected == Design::NavigationDestination{ PageId::General, GeneralSection::Interaction },
			"keyboard space activates the focused real flyout item and closes the menu");
		click(state.parentBounds); frame = idle();
		const int beforeBusiness = businessActions;
		click(state.footerBounds[3]); frame = idle();
		checks.Expect(!state.flyoutOpen && businessActions == beforeBusiness,
			"outside click dismisses real popup without activating the exit footer below it");
		click(state.parentBounds); frame = idle();
		const int beforeBody = bodyActions, beforeBodyNavigation = navigations;
		LayoutRect outsideBody = frame.body;
		// 正文按钮中心可能正好落入基本子项，选择按钮真实位于浮层之外的可见部分。
		outsideBody.left = (std::max)(outsideBody.left, state.flyoutBounds.right + 1.0F);
		const auto outsideBodyPoint = center(outsideBody);
		const bool outsideBodyReachable = outsideBody.Width() > 0 && outsideBody.Height() > 0
			&& frame.body.Contains(outsideBodyPoint.x, outsideBodyPoint.y)
			&& !state.flyoutBounds.Contains(outsideBodyPoint.x, outsideBodyPoint.y);
		checks.Expect(outsideBodyReachable, "the real body button retains a mouse target outside the popup");
		checks.Expect(selected == Design::NavigationDestination{ PageId::General, GeneralSection::Interaction }
			&& state.lastGeneralSection == GeneralSection::Interaction,
			"the outside-body case begins with the actual previously selected interaction page");
		if (outsideBodyReachable) click(outsideBody);
		frame = idle();
		checks.Expect(!state.flyoutOpen && bodyActions == beforeBody && navigations == beforeBodyNavigation
			&& selected == Design::NavigationDestination{ PageId::General, GeneralSection::Interaction }
			&& state.lastGeneralSection == GeneralSection::Interaction,
			"popup outside press and release cannot penetrate the actual body button or change the current page");
		click(state.parentBounds); frame = idle(); key(ImGuiKey_Escape); frame = idle();
		checks.Expect(!state.flyoutOpen && ImGui::GetCurrentContext()->NavId == state.parentItemId,
			"escape closes the actual popup and returns focus to its compact parent");
		size = ImVec2(720, 520); frame = idle(); state.Toggle(); frame = idle();
		checks.Expect(state.overlayOpen && Near(frame.shell.content.left, 48), "narrow expansion retains compact body reservation");
		state.generalExpanded = false; selected = { PageId::Home, GeneralSection::Basic };
		Design::NotifyNavigationDestination(state, selected); frame = idle();
		const int beforeOverlayParent = navigations;
		LayoutRect overlayParentHit;
		auto clickVisibleOverlayParent = [&]() {
			LayoutRect visible = state.parentBounds;
			if (frame.list)
			{
				// 窄窗中段可能为最近子项滚动，仅点击父行实际仍可见的裁剪交集。
				visible.left = (std::max)(visible.left, frame.list->InnerClipRect.Min.x);
				visible.top = (std::max)(visible.top, frame.list->InnerClipRect.Min.y);
				visible.right = (std::min)(visible.right, frame.list->InnerClipRect.Max.x);
				visible.bottom = (std::min)(visible.bottom, frame.list->InnerClipRect.Max.y);
			}
			const bool reachable = frame.list && visible.Width() > 0 && visible.Height() > 0;
			overlayParentHit = visible;
			checks.Expect(reachable, "the narrow parent retains a visible clipped mouse target");
			if (reachable) click(visible);
			frame = idle();
		};
		auto checkOverlayParent = [&](bool passed, const char* message) {
			if (!passed)
				std::cerr << "[HierarchyOverlayParent] " << message << " scale=" << scale
					<< " overlay=" << state.overlayOpen << " expanded=" << state.generalExpanded
					<< " selected=" << static_cast<int>(selected.page) << ',' << static_cast<int>(selected.section)
					<< " remembered=" << static_cast<int>(state.lastGeneralSection)
					<< " routes=" << navigations << " before=" << beforeOverlayParent
					<< " parent=" << state.parentBounds.left << ',' << state.parentBounds.top << ','
					<< state.parentBounds.right << ',' << state.parentBounds.bottom
					<< " clip=" << (frame.list ? frame.list->InnerClipRect.Min.x : 0) << ','
					<< (frame.list ? frame.list->InnerClipRect.Min.y : 0) << ','
					<< (frame.list ? frame.list->InnerClipRect.Max.x : 0) << ','
					<< (frame.list ? frame.list->InnerClipRect.Max.y : 0)
					<< " clicked=" << overlayParentHit.left << ',' << overlayParentHit.top << ','
					<< overlayParentHit.right << ',' << overlayParentHit.bottom
					<< " nav=" << ImGui::GetCurrentContext()->NavId << " parentId=" << state.parentItemId
					<< " hovered=" << ImGui::GetCurrentContext()->HoveredId
					<< " active=" << ImGui::GetCurrentContext()->ActiveId << '\n';
			checks.Expect(passed, message);
		};
		clickVisibleOverlayParent();
		checkOverlayParent(state.overlayOpen && state.generalExpanded && selected.page == PageId::General
			&& selected.section == GeneralSection::Interaction && navigations == beforeOverlayParent + 1,
			"narrow parent expansion enters its remembered child while keeping the group visible");
		clickVisibleOverlayParent();
		checkOverlayParent(state.overlayOpen && !state.generalExpanded && selected.section == GeneralSection::Interaction
			&& state.lastGeneralSection == GeneralSection::Interaction && navigations == beforeOverlayParent + 1,
			"narrow repeated parent click folds only its children and preserves its body");
		clickVisibleOverlayParent();
		checkOverlayParent(state.overlayOpen && state.generalExpanded && selected.section == GeneralSection::Interaction,
			"narrow parent can reopen the preserved child inside the same overlay");
		click(state.childBounds[0]); frame = idle();
		checks.Expect(!state.overlayOpen && selected.section == GeneralSection::Basic,
			"an actual narrow leaf selection still closes the overlay after parent expansion");
		state.Toggle(); frame = idle();
		focusParent(frame); key(ImGuiKey_DownArrow); frame = idle();
		checks.Expect(state.childFocusId != 0 && ImGui::GetCurrentContext()->NavId == state.childFocusId
			&& state.childFocusId != state.parentItemId,
			"keyboard down focuses a real inline child inside the narrow overlay");
		const auto focusBeforeOverlayEscape = ImGui::GetCurrentContext()->NavId;
		key(ImGuiKey_Escape); frame = idle();
		if (state.overlayOpen || ImGui::GetCurrentContext()->NavId != state.parentItemId)
			std::cerr << "[HierarchyEscape] scale=" << scale << " before=" << focusBeforeOverlayEscape
				<< " after=" << ImGui::GetCurrentContext()->NavId << " child=" << state.childFocusId
				<< " parent=" << state.parentItemId << " restore=" << state.restoreParentFocus
				<< " reveal=" << state.groupReveal.value << " overlay=" << state.overlayOpen
				<< " expanded=" << state.Expanded() << " parentY=" << state.parentBounds.top << ','
				<< state.parentBounds.bottom << " listClip=" << (frame.list ? frame.list->InnerClipRect.Min.y : 0)
				<< ',' << (frame.list ? frame.list->InnerClipRect.Max.y : 0) << '\n';
		checks.Expect(!state.overlayOpen && ImGui::GetCurrentContext()->NavId == state.parentItemId,
			"escape closes overlay and restores the parent when its inline child held keyboard focus");
		state.Toggle(); frame = idle(); click(state.childBounds[0]); frame = idle();
		checks.Expect(!state.overlayOpen && selected.section == GeneralSection::Basic,
			"actual narrow child selection closes only the overlay and keeps its destination");
		size = ImVec2(960, 700); state.desktopCompact = false; frame = idle();
		focusParent(frame); key(ImGuiKey_DownArrow); frame = idle();
		checks.Expect(state.childFocusId != 0 && ImGui::GetCurrentContext()->NavId == state.childFocusId,
			"a real inline child can hold focus before a responsive mode change");
		size = ImVec2(720, 520); frame = idle();
		checks.Expect(!state.Expanded() && ImGui::GetCurrentContext()->NavId == state.parentItemId,
			"resizing a focused inline child into compact navigation restores visible parent focus");

		// 固定 child 的真实位置、滚动范围与末项命中来自生产提交，避免复制菜单实现。
		for (bool nextDark : { false, true })
		for (const ImVec2 nextSize : { ImVec2(720, 520), ImVec2(960, 700), ImVec2(1440, 900) })
		for (bool expanded : { false, true })
		{
			dark = nextDark; size = nextSize; state.Resize(size.x);
			if (state.Expanded() != expanded) state.Toggle();
			state.generalExpanded = true; selected = { PageId::Home, GeneralSection::Basic }; requestedScroll = 0;
			frame = idle(); const auto origins = frame.fixedOrigins;
			requestedScroll = frame.maximumScroll; frame = idle();
			checks.Expect(frame.list && Near(frame.scroll, frame.maximumScroll, 1)
				&& (size.y != 520 || frame.maximumScroll > 0),
				"actual hierarchical middle scroll reaches its final root at minimum/default/wide sizes");
			checks.Expect(Near(frame.fixedOrigins[0].y, origins[0].y) && Near(frame.fixedOrigins[1].y, origins[1].y),
				"actual fixed top/footer origins survive middle scrolling across sizes and themes");
			if (frame.list)
			{
				const LayoutRect visible{ frame.list->InnerClipRect.Min.x, frame.list->InnerClipRect.Min.y,
					frame.list->InnerClipRect.Max.x, frame.list->InnerClipRect.Max.y };
				if (!Contains(visible, state.middleBounds[5]))
					std::cerr << "[HierarchyScroll] scale=" << scale << " size=" << size.x << 'x' << size.y
						<< " expanded=" << expanded << " narrow=" << state.narrow << " overlay=" << state.overlayOpen
						<< " scroll=" << frame.scroll << '/' << frame.maximumScroll
						<< " content=" << frame.list->ContentSize.y << " inner=" << frame.list->InnerRect.GetHeight()
						<< " cursorMax=" << frame.list->DC.CursorMaxPos.y << " pos=" << frame.list->Pos.y
						<< " clip=" << visible.left << ',' << visible.top << ',' << visible.right << ',' << visible.bottom
						<< " last=" << state.middleBounds[5].left << ',' << state.middleBounds[5].top << ','
						<< state.middleBounds[5].right << ',' << state.middleBounds[5].bottom << '\n';
				checks.Expect(Contains(visible, state.middleBounds[5]), "last real software-version root is fully visible at the scroll end");
			}
			const int before = navigations; click(state.middleBounds[5]); frame = idle();
			checks.Expect(selected.page == PageId::Version && navigations == before + 1,
				"software version remains reachable through the final root at every size/mode/theme");
			requestedScroll = -1;
		}

		// 动画参数推动真实 pane/DrawData，同时检验反向、速率及关闭，不只测补间公式。
		size = ImVec2(960, 700); state.Resize(size.x); state.desktopCompact = false;
		requestedScroll = 0; dark = false; animated = false; frame = idle(); requestedScroll = -1;
		selected = { PageId::General, GeneralSection::Appearance };
		Design::NotifyNavigationDestination(state, selected); state.generalExpanded = true; frame = idle();
		animated = true; rate = 1;
		const int beforeRapidParent = navigations;
		click(state.parentBounds); frame = draw(outside, false);
		const float foldingReveal = state.groupReveal.value;
		checks.Expect(!state.generalExpanded && foldingReveal > 0 && foldingReveal < 1 && frame.group
			&& Near(frame.group->Size.y, (36 * 4 + 4 * 3) * scale * foldingReveal, 1)
			&& selected.section == GeneralSection::Appearance && navigations == beforeRapidParent
			&& Near(frame.contentAlpha, 1),
			"real parent folding animates the clipped group geometry without restarting the body fade");
		click(state.parentBounds); const float reversingReveal = state.groupReveal.value;
		frame = draw(outside, false);
		checks.Expect(state.generalExpanded && state.groupReveal.value > reversingReveal && state.groupReveal.value < 1
			&& frame.group && Near(frame.group->Size.y, (36 * 4 + 4 * 3) * scale * state.groupReveal.value, 1)
			&& selected.section == GeneralSection::Appearance && navigations == beforeRapidParent
			&& Near(frame.contentAlpha, 1),
			"rapid parent reversal continues from the current real child-window height without re-navigating");
		frame = idle(12); click(state.parentBounds); animated = false; frame = draw(outside, false);
		checks.Expect(!state.generalExpanded && Near(state.groupReveal.value, 0) && !frame.group
			&& selected.section == GeneralSection::Appearance && navigations == beforeRapidParent
			&& Near(frame.contentAlpha, 1),
			"disabling animations settles a parent fold immediately without altering its body");
		click(state.parentBounds); frame = idle();
		checks.Expect(state.generalExpanded && Near(state.groupReveal.value, 1)
			&& selected.section == GeneralSection::Appearance && navigations == beforeRapidParent,
			"parent expansion with animations disabled restores its remembered page and full child height");
		// 正文自身尚在淡入时收起父行，也应继续同一份正文过渡，而非重新开始。
		animated = true; click(state.childBounds[3]); frame = draw(outside, false);
		const float fadingBody = frame.contentAlpha;
		const float fadingProgress = state.contentReveal.progress, fadingTarget = state.contentReveal.target;
		const int beforeFadingFold = navigations;
		click(state.parentBounds); frame = draw(outside, false);
		checks.Expect(fadingBody > 0 && fadingBody < 1 && !state.generalExpanded
			&& state.contentReveal.value >= fadingBody && state.contentReveal.progress >= fadingProgress
			&& state.contentReveal.target == fadingTarget && selected.section == GeneralSection::Storage
			&& state.lastGeneralSection == GeneralSection::Storage && navigations == beforeFadingFold,
			"folding while storage is fading preserves its active transition and remembered destination");
		animated = false; frame = idle(); click(state.parentBounds); frame = idle();
		animated = true; rate = 1; state.Toggle();
		const float startingWidth = frame.paneWidth;
		frame = draw(outside, false); const float closingWidth = frame.paneWidth;
		checks.Expect(closingWidth < startingWidth && closingWidth > 48 * scale,
			"first animated collapse frame draws an intermediate physical pane width");
		state.Toggle(); frame = draw(outside, false);
		checks.Expect(frame.paneWidth > closingWidth && frame.paneWidth < startingWidth,
			"rapid reversal continues from the current rendered width without jumping");
		animated = false; frame = draw(outside, false);
		checks.Expect(Near(frame.paneWidth, 248 * scale, 1) && Near(frame.contentAlpha, 1),
			"disabling animation snaps actual pane geometry and content alpha in the same frame");
		for (float speed : { 0.1F, 5.0F })
		{
			rate = speed; animated = true; state.Toggle(); float previousWidth = frame.paneWidth;
			const int expectedFrames = static_cast<int>(std::ceil(0.2F / speed * 60)) + 2;
			for (int i = 0; i < expectedFrames; ++i)
			{
				if (i == 1) dark = !dark;
				frame = draw(outside, false);
				checks.Expect(frame.paneWidth <= previousWidth + 1 && frame.paneWidth >= 48 * scale - 1,
					"actual width motion remains monotonic and bounded during theme and speed changes");
				checks.Expect(Near(frame.paneWidth / scale, frame.shell.pane.Width(), 1),
					"drawn pane width and content layout use the same animation geometry");
				previousWidth = frame.paneWidth;
			}
			checks.Expect(Near(frame.paneWidth, 48 * scale, 1), "actual pane reaches compact target at the configured rate");
			animated = false; state.Toggle(); frame = idle();
		}
		// 中途变速仍从当前进度推进，直接检查真实 pane 绘制宽度而非另写补间公式。
		animated = true; rate = 1; state.Toggle(); frame = idle(2);
		float previousWidth = frame.paneWidth;
		rate = 0.1F; frame = draw(outside, false);
		checks.Expect(frame.paneWidth < previousWidth && frame.paneWidth > 48 * scale,
			"slowing a live pane tween continues toward its target without rewinding the rendered width");
		previousWidth = frame.paneWidth;
		rate = 5.0F; frame = draw(outside, false);
		checks.Expect(frame.paneWidth < previousWidth && frame.paneWidth >= 48 * scale - 1,
			"speeding a live pane tween preserves progress and remains bounded in actual DrawData");
		frame = idle(5);
		checks.Expect(Near(frame.paneWidth, 48 * scale, 1),
			"a pane tween changed to the fastest rate still reaches its final geometry");
		animated = false; state.Toggle(); frame = idle();

		// 可见慢帧不能被当成隐藏恢复而丢弃，否则低帧率下导航永远停在起点。
		animated = true; rate = 0.1F; frameDelta = 0.2F; state.Toggle();
		previousWidth = frame.paneWidth; frame = draw(outside, false);
		checks.Expect(frame.paneWidth < previousWidth && frame.paneWidth > 48 * scale,
			"a visible 200 ms frame advances real pane motion rather than freezing it");
		for (int index = 0; index < 45; ++index)
		{
			previousWidth = frame.paneWidth; frame = draw(outside, false);
			checks.Expect(frame.paneWidth <= previousWidth + 1 && frame.paneWidth >= 48 * scale - 1,
				"slow visible frames keep the production navigation geometry monotonic and bounded");
		}
		checks.Expect(Near(frame.paneWidth, 48 * scale, 1),
			"slow visible frames converge on the compact navigation target");
		frameDelta = 1.0F / 60.0F;
		animated = false; state.Toggle(); frame = idle();

		// 先经过真实一帧更新策略，再按产品顺序通知新目的地；上一帧关闭动画时通知应直接显示。
		animated = true; rate = 1; frame = draw(outside, false);
		selected = { PageId::General, GeneralSection::Appearance };
		Design::NotifyNavigationDestination(state, selected); frame = draw(outside, false);
		checks.Expect(frame.contentAlpha > 0 && frame.contentAlpha < 1,
			"new body controls render once with a partial fade after a composite destination changes");
		checks.Expect(frame.glyphAlpha > 0 && frame.flatAlpha > 0
			&& frame.glyphAlpha <= static_cast<unsigned int>(std::ceil(255 * frame.contentAlpha))
			&& frame.flatAlpha <= static_cast<unsigned int>(std::ceil(255 * frame.contentAlpha)),
			"production content fade reaches both direct text glyphs and real control fill vertices");
		selected.section = GeneralSection::Interaction; Design::NotifyNavigationDestination(state, selected); frame = idle(10);
		checks.Expect(Near(frame.contentAlpha, 1) && bodyDraws == frames,
			"rapid page changes converge on one body render per frame without duplicate controls");
		checks.Expect(Design::DestinationForPage(static_cast<int>(PageId::Configuration))
			== Design::NavigationDestination{ PageId::General, GeneralSection::Storage }
			&& Design::DestinationForPage(static_cast<int>(PageId::HotKeys))
			== Design::NavigationDestination{ PageId::General, GeneralSection::Interaction },
			"production legacy-route normalization preserves configuration and hotkey entry destinations");
		std::cout << "[HierarchyDrawData] scale=" << scale << " frames=" << frames << " navigations=" << navigations
			<< " businessActions=" << businessActions << " bodyActions=" << bodyActions << '\n';
		const int originalFrames = frames;
		auto indicatorCenter = [](const Frame& actual) { return (actual.indicator.top + actual.indicator.bottom) * 0.5F; };
		auto rowCenter = [](const LayoutRect& row) { return (row.top + row.bottom) * 0.5F; };
		auto expectAttached = [&](const Frame& actual, const LayoutRect& row) {
			checks.Expect(actual.indicatorDrawn && Near(indicatorCenter(actual), rowCenter(row), 0.75F)
				&& actual.indicator.Width() <= 3 * scale + 1 && actual.indicator.Height() <= 16 * scale + 1,
				"actual DrawData contains one blue bar attached to its selected navigation row");
			float topLeft = FLT_MAX, topRight = -FLT_MAX, bottomLeft = FLT_MAX, bottomRight = -FLT_MAX;
			for (const auto& point : actual.indicatorPoints)
			{
				if (Near(point.y, actual.indicator.top, 0.01F * scale))
				{ topLeft = (std::min)(topLeft, point.x); topRight = (std::max)(topRight, point.x); }
				if (Near(point.y, actual.indicator.bottom, 0.01F * scale))
				{ bottomLeft = (std::min)(bottomLeft, point.x); bottomRight = (std::max)(bottomRight, point.x); }
			}
			checks.Expect(actual.indicatorDrawn && topLeft <= topRight && bottomLeft <= bottomRight
				&& topRight - topLeft < actual.indicator.Width() * 0.25F
				&& bottomRight - bottomLeft < actual.indicator.Width() * 0.25F,
				"actual blue-bar geometry narrows at both ends into full capsule caps rather than flat edges");
		};
		// 原684帧合同保持，追加真实滚轮与蓝条顶点回归；不另写一套导航或动画公式。
		size = ImVec2(960, 520); state.Resize(size.x); state.desktopCompact = false;
		state.generalExpanded = false; animated = false; dark = false; rate = 1; requestedScroll = 0;
		frame = idle(); requestedScroll = -1; click(state.middleBounds[2]); frame = idle();
		animated = true; frame = idle(12); expectAttached(frame, state.middleBounds[2]);
		const ImVec2 wheelPoint(frame.list->InnerClipRect.Max.x - 2, frame.list->InnerClipRect.Min.y + 20 * scale);
		frame = draw(wheelPoint, false);
		for (float wheel : { -0.2F, -0.2F, 0.2F, 0.2F })
		{
			const float previousRow = rowCenter(state.middleBounds[2]), previousBar = indicatorCenter(frame), previousScroll = frame.scroll;
			io.AddMouseWheelEvent(0, wheel); frame = draw(wheelPoint, false);
			checks.Expect(std::abs(frame.scroll - previousScroll) > 0.1F,
				"real mouse wheel moves the selected middle row in both scrolling directions");
			checks.Expect(Near(indicatorCenter(frame) - previousBar, rowCenter(state.middleBounds[2]) - previousRow, 0.75F),
				"selected blue bar and its real row move by the same scroll delta in the same frame");
			expectAttached(frame, state.middleBounds[2]);
		}
		click(state.middleBounds[1]); frame = draw(outside, false);
		checks.Expect(selected.page == PageId::Draw && state.indicatorY.Active() && state.indicatorY.progress == 0,
			"a real tab change still begins a distinct 160 ms selection transition");
		for (float wheel : { -0.1F, 0.1F })
		{
			const float previousStart = state.indicatorY.start, previousProgress = state.indicatorY.progress;
			const float previousRow = rowCenter(state.middleBounds[1]);
			io.AddMouseWheelEvent(0, wheel); frame = draw(wheelPoint, false);
			checks.Expect(state.indicatorY.progress > previousProgress
				&& Near((state.indicatorY.start - previousStart) * scale, rowCenter(state.middleBounds[1]) - previousRow, 0.75F)
				&& frame.indicatorDrawn && Near(indicatorCenter(frame), state.indicatorY.value * scale, 0.75F),
				"scrolling during a tab transition translates the actual blue bar without restarting its progress");
		}
		click(state.middleBounds[2]); frame = draw(outside, false);
		checks.Expect(selected.page == PageId::Plugins && state.indicatorY.Active() && state.indicatorY.progress == 0
			&& Near(state.indicatorY.start * scale, indicatorCenter(frame), 0.75F),
			"reversing the tab selection starts from the current rendered blue-bar position");
		frame = idle(12); expectAttached(frame, state.middleBounds[2]);
		// click返回帧仍画旧锚点，首个新目的地帧同时滚动，验证跨锚点的起点补偿。
		frame = click(state.middleBounds[1]);
		const float oldAnchorRow = rowCenter(state.middleBounds[2]), oldAnchorBar = indicatorCenter(frame), oldAnchorScroll = frame.scroll;
		io.AddMouseWheelEvent(0, -0.1F); frame = draw(wheelPoint, false);
		checks.Expect(selected.page == PageId::Draw && std::abs(frame.scroll - oldAnchorScroll) > 0.1F
			&& state.indicatorY.progress == 0 && state.indicatorY.Active() && frame.indicatorDrawn
			&& Near(state.indicatorY.start * scale, oldAnchorBar + rowCenter(state.middleBounds[2]) - oldAnchorRow, 0.75F)
			&& Near(indicatorCenter(frame), state.indicatorY.start * scale, 0.75F)
			&& Near(state.indicatorY.target * scale, rowCenter(state.middleBounds[1]), 0.75F),
			"a real tab change and wheel in the same frame compensate the old anchor scroll before starting the new transition");
		frame = idle(12); expectAttached(frame, state.middleBounds[1]);
		selected = { PageId::General, GeneralSection::Basic }; Design::NotifyNavigationDestination(state, selected);
		requestedScroll = frame.maximumScroll; frame = idle(12);
		const auto clippedAnchor = state.indicatorItemId;
		checks.Expect(!frame.indicatorDrawn && state.indicatorInitialized,
			"a selected row outside the real clip retains its indicator anchor without drawing a stray bar");
		requestedScroll = 0; frame = idle(3); requestedScroll = -1;
		checks.Expect(state.indicatorItemId == clippedAnchor && state.indicatorY.progress >= 0.99F,
			"bringing the same clipped anchor back into view does not restart the selection transition");
		expectAttached(frame, state.parentBounds);
		requestedScroll = frame.maximumScroll; frame = idle(3);
		selected = { PageId::Draw, GeneralSection::Basic }; Design::NotifyNavigationDestination(state, selected);
		frame = idle(12); const auto changedClippedAnchor = state.indicatorItemId;
		checks.Expect(!frame.indicatorDrawn && changedClippedAnchor != clippedAnchor && state.indicatorY.progress >= 0.99F,
			"a destination changed while both real rows are clipped still advances its distinct selection anchor");
		requestedScroll = 0; frame = idle(3); requestedScroll = -1;
		checks.Expect(state.indicatorItemId == changedClippedAnchor && state.indicatorY.progress >= 0.99F,
			"revealing a destination changed outside the clip retains its completed transition");
		expectAttached(frame, state.middleBounds[1]);
		size = ImVec2(960, 700); frame = idle(); click(state.middleBounds[2]); frame = idle(12);
		click(state.chevronBounds); frame = draw(outside, false); expectAttached(frame, state.middleBounds[2]);
		checks.Expect(state.groupReveal.Active() && !state.indicatorY.Active(),
			"expanding another group moves the same selected row and blue bar together without an extra lag");
		click(state.chevronBounds); frame = draw(outside, false); expectAttached(frame, state.middleBounds[2]);
		frame = idle(12); animated = false;
		auto sameVisibleColor = [](ImVec4 a, ImVec4 b) { return Near(a.x, b.x, 0.02F)
			&& Near(a.y, b.y, 0.02F) && Near(a.z, b.z, 0.02F); };
		for (bool sponsorDark : { false, true })
		for (bool sponsorCompact : { false, true })
		{
			dark = sponsorDark; state.desktopCompact = sponsorCompact;
			selected = { PageId::Home, GeneralSection::Basic }; Design::NotifyNavigationDestination(state, selected);
			frame = idle(20);
			checks.Expect(sameVisibleColor(frame.sponsorFill.visible, ImGui::ColorConvertU32ToFloat4(Design::AppBase))
				&& frame.sponsorBlueIcon > 0 && frame.sponsorOtherIcon == 0
				&& (sponsorCompact ? frame.sponsorBlueText == 0 : frame.sponsorBlueText > 0),
				"sponsor uses an ordinary unfilled row with text and icon matching the current blue-bar theme token");
			const auto sponsorMouse = center(state.footerBounds[1]);
			for (int i = 0; i < 20; ++i) frame = draw(sponsorMouse, false);
			const auto subtle = Composite(ImFluent::GetColorU32(ImFluentCol_SubtleFillSecondary), ImGui::ColorConvertU32ToFloat4(Design::AppBase));
			checks.Expect(sameVisibleColor(frame.sponsorFill.visible, subtle), "sponsor hover uses the ordinary subtle navigation background");
			const int beforeSponsor = navigations;
			click(state.footerBounds[1]); frame = idle(20);
			checks.Expect(selected.page == PageId::Support && navigations == beforeSponsor + 1
				&& sameVisibleColor(frame.sponsorFill.visible, subtle),
				"the actual sponsor row navigates to its existing page and keeps the normal selected background");
			expectAttached(frame, state.footerBounds[1]);
		}
		const int debugStart = frames;
		for (bool debugDark : { false, true })
		for (int mode : { 0, 1, 2 })
		for (bool debugAnimated : { false, true })
		{
			dark = debugDark; animated = debugAnimated; requestedScroll = -1;
			size = ImVec2(mode == 2 ? 720.0F : 960.0F, 700.0F); state.Resize(size.x);
			state.desktopCompact = mode == 1; state.overlayOpen = mode == 2;
			selected = { PageId::Home }; Design::NotifyNavigationDestination(state, selected); frame = idle(20);
			selected = { PageId::Draw }; Design::NotifyNavigationDestination(state, selected); frame = idle(1);
			// 真正按下标题栏入口，包含普通选中条尚在移动时的进入。
			frame = click(frame.debug);
			checks.Expect(selected.page == PageId::Debug && !frame.indicatorDrawn && !state.indicatorInitialized
				&& state.indicatorItemId == 0 && state.indicatorWindowId == 0
				&& !state.indicatorX.Active() && !state.indicatorY.Active() && !state.overlayOpen,
				"actual titlebar debug click hides the blue bar and drops its anchor and motion in the activation frame");
			const int beforeRepeat = navigations; frame = click(frame.debug);
			checks.Expect(navigations == beforeRepeat && !frame.indicatorDrawn && selected.page == PageId::Debug,
				"repeating the actual debug button keeps the page without a stray selection bar");
			if (mode == 2)
			{
				state.Toggle(); frame = idle(20);
				checks.Expect(state.overlayOpen && state.Expanded() && !frame.indicatorDrawn,
					"debug page can reopen the narrow overlay without destination synchronization closing it");
			}
			frame = click(state.middleBounds[2]);
			checks.Expect(selected.page == PageId::Plugins && !state.indicatorY.Active(),
				"return from debug binds the selected row directly instead of sliding from an old blue-bar anchor");
			frame = draw(outside, false); expectAttached(frame, state.middleBounds[2]);
			selected = { PageId::Draw }; Design::NotifyNavigationDestination(state, selected); frame = draw(outside, false);
			checks.Expect(state.indicatorY.Active() == animated,
				"ordinary destinations resume their configured blue-bar transition after leaving debug");
			selected = { PageId::Debug }; Design::NotifyNavigationDestination(state, selected); frame = draw(outside, false);
			checks.Expect(!frame.indicatorDrawn && !state.indicatorY.Active(), "rapid reversal to debug suppresses blue-bar motion immediately");
			selected = { PageId::Plugins }; Design::NotifyNavigationDestination(state, selected); frame = draw(outside, false);
			expectAttached(frame, state.middleBounds[2]);
		}
		// 紧凑轨道的真实模态子菜单不能吞掉标题栏 Debug；它只继续隔离正文。
		animated = true; size = ImVec2(960, 700); state.desktopCompact = true; state.overlayOpen = false;
		selected = { PageId::Home }; Design::NotifyNavigationDestination(state, selected); frame = idle(20);
		click(state.parentBounds); frame = idle(12);
		click(state.parentBounds); frame = idle(12);
		checks.Expect(state.flyoutOpen && state.IsFlyoutOpen(), "real compact parent opens the navigation modal before debug activation");
		frame = click(frame.debug);
		checks.Expect(selected.page == PageId::Debug && !state.IsFlyoutOpen() && !frame.indicatorDrawn,
			"titlebar debug remains actionable over the real navigation modal and closes it on activation");
		std::cout << "[DebugNavigationDrawData] scale=" << scale << " frames=" << frames - debugStart << '\n';
		std::cout << "[IndicatorSponsorDrawData] scale=" << scale << " frames=" << frames - originalFrames << '\n';
		const int menuStart = frames;
		for (bool menuDark : { false, true })
		for (int mode : { 0, 1, 2 })
		{
			Design::NavigationState menuState; state = menuState; dark = menuDark; animated = false;
			size = ImVec2(mode == 2 ? 720.0F : 960.0F, 700.0F); state.Resize(size.x);
			state.desktopCompact = mode == 1; state.overlayOpen = mode == 2;
			selected = { PageId::Home }; Design::NotifyNavigationDestination(state, selected);
			frame = idle(20);
			auto sameMenuColor = [&](ImU32 target) {
				const auto expected = ImGui::ColorConvertU32ToFloat4(target);
				// Fluent渐近混色按8位量化，稳定端点允许3LSB误差，三种反馈仍明确分开。
				const bool matches = Near(frame.menuFill.visible.x, expected.x, 3.01F / 255)
					&& Near(frame.menuFill.visible.y, expected.y, 3.01F / 255)
					&& Near(frame.menuFill.visible.z, expected.z, 3.01F / 255);
				if (!matches) std::cerr << "[HamburgerColor] scale=" << scale << " mode=" << mode
					<< " dark=" << dark << " actual=" << frame.menuFill.visible.x << ','
					<< frame.menuFill.visible.y << ',' << frame.menuFill.visible.z << " expected="
					<< expected.x << ',' << expected.y << ',' << expected.z << '\n';
				return matches;
			};
			checks.Expect(frame.top && Near(frame.menu.Width(), 40 * scale)
				&& Near(frame.menu.Height(), 36 * scale) && sameMenuColor(Design::AppBase) && frame.menuAlpha <= 3,
				"actual hamburger starts transparent with the same hit size in expanded, compact and overlay panes");
			const auto mouse = center(frame.menu);
			for (int index = 0; index < 20; ++index) frame = draw(mouse, false);
			checks.Expect(frame.menuFill.triangles > 0 && sameMenuColor(Design::GetPalette().subtleHover),
				"actual hamburger DrawData reaches the sidebar hover fill in both themes");
			const ImVec4 hoveredMenuColor = frame.menuFill.visible;
			const bool wasCompact = state.desktopCompact, wasOverlay = state.overlayOpen;
			const int beforeMenuNavigation = navigations, beforeMenuBusiness = businessActions;
			for (int index = 0; index < 20; ++index)
			{
				frame = draw(mouse, true);
				checks.Expect(state.desktopCompact == wasCompact && state.overlayOpen == wasOverlay,
					"holding the actual hamburger preserves the pane target until release");
			}
			const auto pressedTarget = ImGui::ColorConvertU32ToFloat4(Design::GetPalette().subtlePressed);
			auto pressedDistance = [&](ImVec4 value) { return std::abs(value.x - pressedTarget.x)
				+ std::abs(value.y - pressedTarget.y) + std::abs(value.z - pressedTarget.z); };
			checks.Expect(sameMenuColor(Design::GetPalette().subtlePressed)
				&& pressedDistance(frame.menuFill.visible) < pressedDistance(hoveredMenuColor),
				"actual held hamburger approaches the pressed fill and is distinct from its preceding hover fill");
			std::cout << "[HamburgerColors] scale=" << scale << " mode=" << mode << " dark=" << dark
				<< " hover=" << hoveredMenuColor.x * 255 << " held=" << frame.menuFill.visible.x * 255;
			// 用真实按下/释放确认只切换一次，随后移出应恢复透明，不改变页面或业务动作。
			frame = draw(mouse, false); frame = idle(30);
			checks.Expect(state.desktopCompact == (mode == 2 ? wasCompact : !wasCompact)
				&& state.overlayOpen == (mode == 2 ? !wasOverlay : wasOverlay)
				&& navigations == beforeMenuNavigation && businessActions == beforeMenuBusiness
				&& sameMenuColor(Design::AppBase) && frame.menuAlpha <= 3,
				"actual hamburger release toggles the pane once and mouse leave restores its transparent fill");
			std::cout << " leave=" << frame.menuFill.visible.x * 255 << " alpha=" << frame.menuAlpha << '\n';
		}
		std::cout << "[HamburgerDrawData] scale=" << scale << " frames=" << frames - menuStart << '\n';
		const int scrollbarStart = frames;
		for (bool theme : { false, true }) for (int mode : { 0, 1, 2 })
		{
			Design::NavigationState next; state = next; dark = theme; animated = false;
			size = ImVec2(mode == 2 ? 720.0F : 960.0F, 600); state.Resize(size.x);
			state.desktopCompact = mode == 1; state.overlayOpen = mode == 2;
			selected = { PageId::Home }; state.generalExpanded = false; frame = idle(4);
			checks.Expect(frame.list && !frame.list->ScrollbarY && Near(frame.maximumScroll, 0), "collapsed navigation hides the vertical bar when its real middle fits");
			state.generalExpanded = true; frame = idle(4);
			checks.Expect(frame.list->ScrollbarY == state.Expanded(), "expanding the real inline group shows the bar only in expanded and overlay modes");
			size.y = 520; frame = idle(4);
			checks.Expect(frame.list->ScrollbarY && frame.maximumScroll > 0, "minimum-height navigation overflows in every pane mode");
			const auto gapTrack = ImGui::GetWindowScrollbarRect(frame.list, ImGuiAxis_Y);
			checks.Expect(Near(gapTrack.Min.x - state.middleBounds.back().right, 4 * scale, 1), "actual navigation leaves exactly its original padding gap instead of double reservation");
			if (mode == 1)
			{
				focusParent(frame); frame = idle(2); const auto track = ImGui::GetWindowScrollbarRect(frame.list, ImGuiAxis_Y);
				const auto white = io.Fonts->TexUvWhitePixel; bool glyphsClear = true; int glyphs = 0;
				for (const auto& vertex : frame.list->DrawList->VtxBuffer)
					if ((vertex.uv.x != white.x || vertex.uv.y != white.y) && vertex.col == Design::TextPrimary
						&& vertex.pos.y >= frame.list->InnerClipRect.Min.y && vertex.pos.y <= frame.list->InnerClipRect.Max.y)
					{ ++glyphs; glyphsClear &= vertex.pos.x < track.Min.x; }
				checks.Expect(glyphs > 0 && glyphsClear && GImGui->NavId == state.parentItemId,
					"compact real focused icon glyphs stay to the left of the visible scrollbar track");
			}
			const int before = navigations, beforeTrackBusiness = businessActions; const auto destination = selected;
			const auto origins = frame.fixedOrigins; const bool groupOpen = state.generalExpanded;
			const auto track = ImGui::GetWindowScrollbarRect(frame.list, ImGuiAxis_Y);
			const ImVec2 trackPoint(track.GetCenter().x, track.Max.y - 12 * scale);
			const float beforeTrackScroll = frame.scroll;
			draw(trackPoint, false); frame = draw(trackPoint, true);
			const float seekScroll = frame.scroll;
			checks.Expect(GImGui->ActiveId == ImGui::GetWindowScrollbarID(frame.list, ImGuiAxis_Y), "pressing the real navigation track keeps scrollbar ownership");
			checks.Expect(seekScroll > beforeTrackScroll, "pressing the real navigation track changes Scroll.y with the default page seek policy");
			LayoutRect thumb{ FLT_MAX, FLT_MAX, -FLT_MAX, -FLT_MAX };
			for (auto* list : ImGui::GetDrawData()->CmdLists) for (const auto& vertex : list->VtxBuffer)
				if (vertex.col == ImGui::GetColorU32(ImGuiCol_ScrollbarGrabActive) && track.Contains(vertex.pos))
				{ thumb.left = (std::min)(thumb.left, vertex.pos.x); thumb.right = (std::max)(thumb.right, vertex.pos.x);
					thumb.top = (std::min)(thumb.top, vertex.pos.y); thumb.bottom = (std::max)(thumb.bottom, vertex.pos.y); }
			checks.Expect(thumb.Width() > 0 && thumb.Height() > 0, "actual active thumb DrawData supplies its drag hit point");
			draw(trackPoint, false); const auto grab = center(thumb); draw(grab, false); frame = draw(grab, true);
			const float beforeThumbDrag = frame.scroll; frame = draw({ grab.x, grab.y - 25 * scale }, true);
			checks.Expect(frame.scroll < beforeThumbDrag && GImGui->ActiveId == ImGui::GetWindowScrollbarID(frame.list, ImGuiAxis_Y),
				"holding the actual rendered thumb reverses Scroll.y while retaining scrollbar ownership");
			draw({ grab.x, grab.y - 25 * scale }, false);
			checks.Expect(navigations == before && businessActions == beforeTrackBusiness && selected == destination && state.generalExpanded == groupOpen,
				"scrollbar press and drag cannot activate a navigation root, chevron or footer action");
			// 实际行右端到轨道之间的空白可以沿用旧 void 拖动，但不会点击导航。
			requestedScroll = 0; frame = idle(3); requestedScroll = -1;
			const ImVec2 gap(track.Min.x - 2 * scale, frame.list->InnerClipRect.GetCenter().y);
			for (auto source : { ImGuiMouseSource_Mouse, ImGuiMouseSource_TouchScreen })
			{
				io.AddMouseSourceEvent(source); draw(gap, false); draw(gap, true);
				const float initial = frame.list->Scroll.y;
				frame = draw({ gap.x, gap.y - 20 * scale }, true); frame = draw({ gap.x, gap.y - 20 * scale }, true);
				checks.Expect(frame.scroll > initial && navigations == before && businessActions == beforeTrackBusiness && selected == destination,
					"mouse and touch-source holding navigation gap scrolls the real list without route or business actions");
				frame = draw({ gap.x, gap.y - 5 * scale }, true); frame = draw({ gap.x, gap.y - 5 * scale }, false);
				checks.Expect(Near(frame.fixedOrigins[0].y, origins[0].y) && Near(frame.fixedOrigins[1].y, origins[1].y), "dragging middle content keeps top and footer fixed");
			}
			io.AddMouseSourceEvent(ImGuiMouseSource_Mouse); state.generalExpanded = false; size.y = 900; frame = idle(4);
			checks.Expect(!frame.list->ScrollbarY && Near(frame.scroll, 0), "fold and resize remove the actual bar and clamp the old scroll position");
		}
		// 子列表固定 child 的 padding 仍由 nav-list 消费，选中蓝条跟随下一 Begin 的真实位置。
		Design::NavigationState inlineState; state = inlineState; size = ImVec2(960, 520); animated = false; dark = false;
		state.generalExpanded = true; selected = { PageId::General, GeneralSection::Appearance }; frame = idle(4);
		requestedScroll = 0; frame = idle(3); requestedScroll = -1;
		if (frame.group)
		{
			const ImVec2 padding(frame.group->InnerClipRect.Min.x + 2 * scale, center(state.childBounds[1]).y);
			const float beforeInlineScroll = frame.scroll; const int beforeInlineNavigation = navigations;
			draw(padding, false); draw(padding, true); draw({ padding.x, padding.y - 15 * scale }, true); frame = draw({ padding.x, padding.y - 15 * scale }, true);
			checks.Expect(Near(frame.scroll - beforeInlineScroll, 15 * scale, 1) && Near(frame.group->Scroll.y, 0) && navigations == beforeInlineNavigation,
				"actual inline child padding drag scrolls navigation once while its fixed clip child stays at zero");
			expectAttached(frame, state.childBounds[1]); draw(padding, false); frame = idle();
		}
		// 正常最小窗的四项浮层可完整容纳；再给真实 renderer 更短客户裁剪，覆盖其溢出分支。
		Design::NavigationState flyoutState; state = flyoutState; size = ImVec2(960, 520); dark = false; animated = true;
		state.desktopCompact = true; selected = { PageId::General }; frame = idle(20);
		state.flyoutOpen = true; frame = idle(20);
		auto* flyout = ImGui::FindWindowByID(state.flyoutWindowId);
		checks.Expect(flyout && Near(flyout->ScrollMax.y, 0), "real navigation flyout fits its four entries at the normal minimum client size");
		constrainedFlyoutBottom = 180; frame = idle(4); flyout = ImGui::FindWindowByID(state.flyoutWindowId);
		checks.Expect(flyout && flyout->ScrollbarY && flyout->ScrollMax.y > 0, "actual production flyout clamp creates a scroll owner for constrained client bounds");
		if (flyout)
		{
			const ImVec2 padding(flyout->InnerClipRect.Min.x + 1, flyout->InnerClipRect.GetCenter().y);
			const int beforeFlyoutNavigation = navigations, beforeFlyoutBusiness = businessActions;
			const float middleBefore = frame.scroll, initial = flyout->Scroll.y;
			draw(padding, false); draw(padding, true); draw({ padding.x, padding.y - 15 * scale }, true); frame = draw({ padding.x, padding.y - 15 * scale }, true);
			checks.Expect(flyout->Scroll.y > initial && Near(frame.scroll, middleBefore) && navigations == beforeFlyoutNavigation && businessActions == beforeFlyoutBusiness,
				"actual navigation popup padding drag consumes one delta without scrolling middle navigation or choosing a child");
			state.CloseFlyout(); frame = draw({ padding.x, padding.y - 30 * scale }, true);
			checks.Expect(state.IsFlyoutOpen() && !state.flyoutOpen && flyout->ScrollTarget.y == FLT_MAX,
				"already ended disabled navigation flyout does not consume drag during its close animation");
			draw(outside, false); frame = idle(20);
			checks.Expect(!state.IsFlyoutOpen() && GImGui->OpenPopupStack.empty(), "navigation fade completes popup closure and its drag ownership together");
		}
		constrainedFlyoutBottom = 0;
		for (bool theme : { false, true }) for (int mode : { 0, 1, 2 })
		{
			Design::NavigationState fresh; state = fresh; dark = theme; animated = true; rate = 1;
			size = { mode == 2 ? 720.0F : 960.0F, 900 }; state.Resize(size.x); state.desktopCompact = mode == 1; state.overlayOpen = mode == 2;
			selected = { PageId::Home }; frame = idle(24); const float fullWidth = state.middleBounds.back().Width();
			size.y = 520; bool partial = false;
			for (int index = 0; index < 18; ++index)
			{
				frame = draw(outside, false); const float width = frame.list->ScrollbarSizes.x;
				if (frame.list->ScrollbarY && width > 0 && width < 7.99F * scale)
				{ partial = true; checks.Expect(state.middleBounds.back().Width() < fullWidth && state.middleBounds.back().Width() > fullWidth - 8 * scale - 1,
					"actual navigation row background and hit rectangle continuously shrink through scrollbar appearance"); }
			}
			checks.Expect(partial && Near(frame.list->ScrollbarSizes.x, 8 * scale, 1), "every pane mode has a partial native scrollbar transition followed by its full 8 DIP endpoint");
			const int routeBefore = navigations; const float initial = frame.scroll;
			const auto point = center(state.middleBounds[1]); draw(point, false); draw(point, true);
			frame = draw({ point.x, point.y - 15 * scale }, true);
			checks.Expect(frame.scroll > initial && navigations == routeBefore, "dragging actual navigation row content crosses native threshold and scrolls in that input frame");
			draw({ point.x, point.y - 15 * scale }, false); checks.Expect(navigations == routeBefore, "navigation content drag cancels route release while ordinary taps retain routing");
			size.y = 900; bool fading = false;
			for (int index = 0; index < 18; ++index)
			{
				frame = draw(outside, false); const float width = frame.list->ScrollbarSizes.x;
				if (!frame.list->ScrollbarY && width > 0 && width < 7.99F * scale)
				{ fading = true; checks.Expect(state.middleBounds.back().Width() > fullWidth - 8 * scale - 1 && state.middleBounds.back().Width() < fullWidth,
					"actual navigation hit width expands while its rail exits without a full-width input overlap"); }
			}
			checks.Expect(fading && frame.list->ScrollbarSizes.x == 0 && Near(state.middleBounds.back().Width(), fullWidth, 1), "navigation exit reaches zero rail reservation and restores its normal gap and width");
		}
		std::cout << "[NavigationScrollbarDragDrawData] scale=" << scale << " frames=" << frames - scrollbarStart << '\n';
		io.ConfigFlags = previousFlags;
		io.DisplaySize = ImVec2(1440 * scale, 1000 * scale);
		Design::ApplyPalette(false);
	}

	void CheckDragScrolling(Checks& checks, ImFont* regular, ImFont* strong, float scale)
	{
		auto& io = ImGui::GetIO(); io.ConfigInputTrickleEventQueue = false;
		Design::BindTextFonts(regular, strong); Design::ApplyPalette(false);
		Design::NavigationState state;
		struct Frame { ImGuiWindow* page = nullptr; ImGuiWindow* inner = nullptr; ImGuiWindow* popup = nullptr;
			ImGuiID active = 0; ImRect button, slider, text, combo; float pageScroll = 0, innerScroll = 0; };
		int mode = 0, frames = 0, buttons = 0, selected = 0, movingFrames = 0; bool closePopup = false, omitPopup = false;
		float reset = -1, sliderValue = 0.5F; bool comboAnimated = false, nativeCombo = false; int comboCount = 40; char text[64] = "Original text";
		std::vector<std::string> options; for (int index = 0; index < 40; ++index) options.push_back("Option " + std::to_string(index));
		auto draw = [&](ImVec2 mouse, bool down) {
			io.DisplaySize = { 800 * scale, 560 * scale }; io.DeltaTime = 1.0F / 60;
			io.AddMousePosEvent(mouse.x, mouse.y); io.AddMouseButtonEvent(0, down); ImGui::NewFrame(); Design::BeginDragScrolling(state);
			Design::ConfigureRowAnimations(io.DeltaTime, comboAnimated, 1);
			ImGui::SetNextWindowPos({ 0, 0 }); ImGui::SetNextWindowSize(io.DisplaySize);
			// 与产品相同的固定 root flags，不能用测试独有 NoMove 隐藏空白 child 的移动风险。
			ImGui::Begin("##drag-scroll-production-root", nullptr, ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize
				| ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoTitleBar);
			ImGui::SetCursorPos({ 40 * scale, 50 * scale });
			ImGui::SetNextWindowContentSize({ 0, 1100 * scale });
			Design::BeginScrollChild("##drag-scroll-page", { 430 * scale, 240 * scale });
			Frame frame; frame.page = ImGui::GetCurrentWindow(); if (reset >= 0) ImGui::SetScrollY(reset);
			if (mode == 1)
				Design::BeginScrollChild("##drag-scroll-auto-body", { 0, 0 }, ImGuiChildFlags_AutoResizeY);
			if (mode == 2 || mode == 3)
			{
				ImGui::SetNextWindowContentSize({ 0, 600 * scale });
				Design::BeginScrollChild("##drag-scroll-nested", { 340 * scale, 140 * scale }, ImGuiChildFlags_None,
					mode == 3 ? ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse : ImGuiWindowFlags_None);
				frame.inner = ImGui::GetCurrentWindow(); if (reset >= 0) ImGui::SetScrollY(reset);
				ImGui::Dummy({ 0, 600 * scale }); ImGui::EndChild();
			}
			else
			{
				buttons += Design::Button("Drag test button", { 170 * scale, 32 * scale }) ? 1 : 0;
				frame.button = { ImGui::GetItemRectMin(), ImGui::GetItemRectMax() };
				ImGui::SetNextItemWidth(170 * scale); ImGui::SliderFloat("##drag-test-slider", &sliderValue, 0, 1);
				frame.slider = { ImGui::GetItemRectMin(), ImGui::GetItemRectMax() };
				ImGui::SetNextItemWidth(170 * scale); Design::TextBox("##drag-test-text", text, sizeof(text));
				frame.text = { ImGui::GetItemRectMin(), ImGui::GetItemRectMax() };
				if (!omitPopup) { ImGui::SetNextItemWidth(170 * scale);
					if (nativeCombo)
					{
						ImFluent::PushFont(ImFluentTextStyle_Body, Design::ControlText.size);
						std::vector<const char*> items; for (int index = 0; index < comboCount; ++index) items.push_back(options[index].c_str());
						ImFluent::ComboBox("##drag-test-combo", &selected, items.data(), comboCount);
						ImFluent::PopFont();
					}
					else Design::ComboBox("##drag-test-combo", &selected, std::vector<std::string>(options.begin(), options.begin() + comboCount));
					frame.combo = { ImGui::GetItemRectMin(), ImGui::GetItemRectMax() }; }
				ImGui::Dummy({ 0, 900 * scale });
			}
			if (mode == 1) ImGui::EndChild();
			ImGui::EndChild(); ImGui::End();
			for (const auto& popup : GImGui->OpenPopupStack) if (popup.Window && popup.Window->Active) frame.popup = popup.Window;
			if (closePopup && frame.popup) ImGui::ClosePopupToLevel(0, false);
			auto& g = *GImGui; auto* previous = g.CurrentWindow; auto* stack = g.StackSizesInBeginForCurrentWindow;
			auto* table = g.CurrentTable; auto* font = g.Font; const float fontSize = g.FontSize;
			const float dpi = g.CurrentDpiScale, density = g.FontRasterizerDensity;
			const auto last = g.LastItemData; const int depth = g.CurrentWindowStack.Size;
			const auto pageClip = frame.page->ClipRect; const auto innerClip = frame.inner ? frame.inner->ClipRect : ImRect();
			const int vertices = frame.page->DrawList->VtxBuffer.Size, commands = frame.page->DrawList->CmdBuffer.Size;
			Design::ApplyDragScrolling(state);
			checks.Expect(g.CurrentWindow == previous && g.StackSizesInBeginForCurrentWindow == stack && g.CurrentTable == table
				&& g.Font == font && g.FontSize == fontSize && g.CurrentDpiScale == dpi && g.FontRasterizerDensity == density
				&& g.CurrentWindowStack.Size == depth && std::memcmp(&last, &g.LastItemData, sizeof(last)) == 0,
				"frame-end drag deployment restores current window, font, table, DPI, stack and last actual item");
			checks.Expect(frame.page->ClipRect.Min.x == pageClip.Min.x && frame.page->ClipRect.Min.y == pageClip.Min.y && frame.page->ClipRect.Max.x == pageClip.Max.x && frame.page->ClipRect.Max.y == pageClip.Max.y
				&& (!frame.inner || (frame.inner->ClipRect.Min.x == innerClip.Min.x && frame.inner->ClipRect.Min.y == innerClip.Min.y && frame.inner->ClipRect.Max.x == innerClip.Max.x && frame.inner->ClipRect.Max.y == innerClip.Max.y))
				&& frame.page->DrawList->VtxBuffer.Size == vertices && frame.page->DrawList->CmdBuffer.Size == commands,
				"drag deployment restores End-popped clips and adds no drawing or clip-stack commands");
			frame.active = g.ActiveId; frame.pageScroll = frame.page->Scroll.y; frame.innerScroll = frame.inner ? frame.inner->Scroll.y : 0;
			ImGui::Render(); ++frames; movingFrames += g.MovingWindow ? 1 : 0;
			checks.Expect(Near(frame.page->RootWindow->Pos.x, 0) && Near(frame.page->RootWindow->Pos.y, 0), "actual fixed product root stays positioned during child void fallback");
			checks.Expect(ImGui::GetDrawData()->Valid && ImGui::GetDrawData()->TotalVtxCount > 0, "drag regression renders real production controls and DrawData");
			return frame;
		};
		const ImVec2 outside(-100, -100);
		auto idle = [&](int count = 3) { Frame frame; for (int index = 0; index < count; ++index) frame = draw(outside, false); return frame; };
		auto center = [](const ImRect& rect) { return rect.GetCenter(); };
		Frame frame;
		for (auto source : { ImGuiMouseSource_Mouse, ImGuiMouseSource_TouchScreen }) for (mode = 0; mode < 4; ++mode)
		{
			io.AddMouseSourceEvent(source); reset = 100 * scale; frame = idle(4); reset = -1;
			const ImVec2 point = mode >= 2 ? ImVec2(frame.inner->InnerClipRect.Max.x - 14 * scale, frame.inner->InnerClipRect.GetCenter().y)
				: ImVec2(frame.page->InnerClipRect.Max.x - 30 * scale, frame.page->InnerClipRect.GetCenter().y);
			draw(point, false); frame = draw(point, true);
			const float originalPage = frame.pageScroll, originalInner = frame.innerScroll;
			frame = draw({ point.x, point.y - 30 * scale }, true);
			// 已抓取位移在当前 Begin 生效；零位移续帧不能给父子再次应用。
			frame = draw({ point.x, point.y - 30 * scale }, true);
			checks.Expect(mode == 2 ? Near(frame.pageScroll, originalPage, 1) && Near(frame.innerScroll - originalInner, 30 * scale, 1)
				: Near(frame.pageScroll - originalPage, 30 * scale, 1) && (!frame.inner || Near(frame.innerScroll, originalInner, 1)),
				"mouse and touch source drag once at the nearest real scroll owner, including auto-height and fixed children");
			frame = draw({ point.x, point.y - 10 * scale }, true); frame = draw({ point.x, point.y - 10 * scale }, true);
			checks.Expect(mode == 2 ? Near(frame.innerScroll - originalInner, 10 * scale, 1) : Near(frame.pageScroll - originalPage, 10 * scale, 1),
				"reversing a held drag uses physical MouseDelta once without multiplying DPI again");
			frame = draw({ point.x, point.y - 10 * scale }, false); const float released = mode == 2 ? frame.innerScroll : frame.pageScroll;
			frame = draw({ point.x, point.y - 40 * scale }, false); frame = draw(outside, false);
			checks.Expect(Near(mode == 2 ? frame.innerScroll : frame.pageScroll, released, 1), "release stops content scrolling immediately");
			if (mode == 0)
			{
				reset = 100 * scale; frame = idle(); reset = -1; draw(point, false); frame = draw(point, true);
				const auto id = frame.page->GetID("##scrolldraggingoverlay");
				checks.Expect(frame.active == id, "void dragging retains the exact historical overlay identity");
				frame = draw({ 780 * scale, point.y - 40 * scale }, true); frame = draw({ 780 * scale, point.y - 40 * scale }, true);
				checks.Expect(frame.active == id && frame.pageScroll > 100 * scale, "captured historical overlay stays alive and scrolls after the pointer leaves its window");
				draw(outside, false); frame = idle();
			}
		}
		io.AddMouseSourceEvent(ImGuiMouseSource_Mouse); mode = 0; reset = 0; frame = idle(); reset = -1;
		const ImVec2 edgePoint(frame.page->InnerClipRect.Max.x - 30 * scale, frame.page->InnerClipRect.GetCenter().y);
		draw(edgePoint, false); draw(edgePoint, true); draw({ edgePoint.x, edgePoint.y + 100 * scale }, true);
		frame = draw({ edgePoint.x, edgePoint.y + 100 * scale }, true);
		checks.Expect(Near(frame.pageScroll, 0), "historical drag target clamps at the top boundary on next Begin");
		draw(outside, false); reset = 100 * scale; frame = idle(); reset = -1;
		draw(edgePoint, false); draw(edgePoint, true); draw({ edgePoint.x, edgePoint.y - 2000 * scale }, true);
		frame = draw({ edgePoint.x, edgePoint.y - 2000 * scale }, true);
		checks.Expect(Near(frame.pageScroll, frame.page->ScrollMax.y, 1), "captured historical drag clamps at bottom without an internal alternate gesture");
		draw({ edgePoint.x, edgePoint.y + 1000 * scale }, true); frame = draw({ edgePoint.x, edgePoint.y + 1000 * scale }, true);
		checks.Expect(Near(frame.pageScroll, 0), "rapid captured reversal from bottom clamps back to top without accumulated delta");
		draw(outside, false); reset = 100 * scale; frame = idle(); reset = -1;
		const float beforeExternalDown = frame.pageScroll;
		draw({ edgePoint.x, edgePoint.y + 20 * scale }, false); draw(outside, true);
		frame = draw(edgePoint, true);
		std::cout << "[HeldEnterDrag] scale=" << scale << " delta=" << io.MouseDelta.y << " target=" << frame.page->ScrollTarget.y
			<< " owned=" << io.MouseDownOwned[0] << " active=" << GImGui->ActiveId << " hovered=" << (GImGui->HoveredWindow ? GImGui->HoveredWindow->Name : "none") << '\n';
		frame = draw({ edgePoint.x, edgePoint.y - 20 * scale }, true); frame = draw({ edgePoint.x, edgePoint.y - 20 * scale }, true);
		// 库拒绝整个 viewport 外开始的未归属按下，不能把原 child fallback 扩大成外部手势。
		checks.Expect(!io.MouseDownOwned[0] && !GImGui->HoveredWindow && Near(frame.pageScroll, beforeExternalDown, 1),
			"unowned mouse-down outside the viewport stays excluded after entering content, preserving the original ImGui input ownership");
		draw(outside, false); reset = 0; frame = idle(); reset = -1;
		const int before = buttons; auto button = center(frame.button); draw(button, false); frame = draw(button, true);
		const auto buttonId = frame.active; const float scroll = frame.pageScroll;
		frame = draw({ button.x, button.y + 8 * scale }, true); frame = draw({ button.x, button.y + 8 * scale }, true);
		checks.Expect(frame.active == buttonId && Near(frame.pageScroll, scroll), "holding a real button prevents the old void overlay from stealing its ActiveId");
		draw(button, false); frame = idle(); checks.Expect(buttons == before + 1, "ordinary real button release still clicks exactly once");
		auto slider = center(frame.slider); draw(slider, false); frame = draw(slider, true); const auto sliderId = frame.active;
		frame = draw({ slider.x + 20 * scale, slider.y + 4 * scale }, true); frame = draw({ slider.x + 20 * scale, slider.y + 4 * scale }, true);
		checks.Expect(frame.active == sliderId && Near(frame.pageScroll, 0) && sliderValue != 0.5F, "real slider retains dragging and does not scroll its page");
		draw(slider, false); frame = idle(); auto textbox = center(frame.text); draw(textbox, false); frame = draw(textbox, true); const auto textId = frame.active;
		frame = draw({ textbox.x, textbox.y + 5 * scale }, true); frame = draw({ textbox.x, textbox.y + 5 * scale }, true);
		checks.Expect(frame.active == textId && Near(frame.pageScroll, 0) && std::string(text) == "Original text", "real text input selection retains ownership and content");
		draw(textbox, false); frame = idle(); ImGui::ClearActiveID();
		// 长 Combo 使用真实生产控件的内部 Popup；从非条目 padding 起拖保留旧 OnVoid 规则。
		auto combo = center(frame.combo); draw(combo, false); draw(combo, true); frame = draw(combo, false); frame = idle(3);
		checks.Expect(frame.popup && frame.popup->ScrollbarY && frame.popup->ScrollMax.y > 0, "long real Combo creates an actually scrollable internal popup");
		if (frame.popup)
		{
			const ImVec2 padding(frame.popup->InnerClipRect.Min.x + 1, frame.popup->InnerClipRect.GetCenter().y);
			const float pageBefore = frame.pageScroll, popupBefore = frame.popup->Scroll.y;
			draw(padding, false); draw(padding, true); frame = draw({ padding.x, padding.y - 35 * scale }, true); frame = draw({ padding.x, padding.y - 35 * scale }, true);
			checks.Expect(frame.popup && frame.popup->Scroll.y > popupBefore && Near(frame.pageScroll, pageBefore) && selected == 0,
				"dragging actual Combo popup padding scrolls only its popup without selecting a row");
			auto* submittedPopup = frame.popup; const float hiddenTarget = submittedPopup->ScrollTarget.y;
			omitPopup = true; frame = draw({ padding.x, padding.y - 45 * scale }, true); omitPopup = false;
			checks.Expect(!submittedPopup->Active && submittedPopup->ScrollTarget.y == hiddenTarget,
				"a popup omitted from the current frame cannot reuse its previous Active state or consume captured dragging");
			frame = draw({ padding.x, padding.y - 45 * scale }, true);
			closePopup = true; const float closedTarget = submittedPopup->ScrollTarget.y; frame = draw({ padding.x, padding.y - 60 * scale }, true); closePopup = false;
			checks.Expect(GImGui->OpenPopupStack.empty() && (!frame.popup || frame.popup->ScrollTarget.y == closedTarget), "already submitted closed popup cannot consume another drag delta");
			draw(outside, false); omitPopup = true; frame = idle(); omitPopup = false;
		}
		// 真实长下拉列表从条目本身起拖，随后缩短内容验证退出宽度在提交条目前完成。
		comboAnimated = true; frame = idle(12); auto opening = center(frame.combo);
		draw(opening, false); draw(opening, true); frame = draw(opening, false); frame = idle(24);
		checks.Expect(frame.popup && Near(frame.popup->ScrollbarSizes.x, 8 * scale, 1), "real long Combo reaches its full animated native scrollbar endpoint");
		if (frame.popup)
		{
			const ImVec2 row(frame.popup->WorkRect.Min.x + 40 * scale, frame.popup->WorkRect.Min.y + 16 * scale);
			draw(row, false); frame = draw(row, true); const auto rowId = frame.active; const float popupBefore = frame.popup->Scroll.y;
			frame = draw({ row.x, row.y - 20 * scale }, true);
			checks.Expect(frame.popup && rowId && frame.active != rowId && frame.popup->Scroll.y > popupBefore && selected == 0,
				"drag from a real Combo Selectable transfers its owned press at the native threshold and scrolls without selecting it");
			frame = draw({ row.x, row.y - 20 * scale }, false);
			checks.Expect(frame.popup && selected == 0, "releasing a Combo content drag keeps its popup and value");
			comboCount = 2; bool fade = false;
			for (int index = 0; index < 24; ++index)
			{
				frame = draw(outside, false);
				if (frame.popup && !frame.popup->ScrollbarY && frame.popup->ScrollbarSizes.x > 0 && frame.popup->ScrollbarSizes.x < 7.99F * scale)
				{
					fade = true;
					checks.Expect(Near(frame.popup->DC.CursorMaxPos.x, frame.popup->ContentRegionRect.Max.x, 1), "actual shortened Combo row draw/hit width is submitted after the shared fading reservation");
				}
			}
			if (frame.popup) std::cout << "[ComboShortEndpoint] scale=" << scale << " fade=" << fade << " bar=" << frame.popup->ScrollbarY
				<< " reserve=" << frame.popup->ScrollbarSizes.x << " max=" << frame.popup->ScrollMax.y << " content=" << frame.popup->ContentSize.y
				<< " inner=" << frame.popup->InnerRect.GetHeight() << " size=" << frame.popup->Size.y << " padding=" << frame.popup->WindowPadding.y << '\n';
			const bool shortBar = frame.popup && frame.popup->ScrollbarY;
			const float shortReserve = frame.popup ? frame.popup->ScrollbarSizes.x : -1, shortMax = frame.popup ? frame.popup->ScrollMax.y : -1;
			if (!shortBar) checks.Expect(fade && shortReserve == 0 && shortMax == 0,
				"a fitting real two-row Combo releases its full native rail after its exit transition");
			// 1.875 倍的原生 auto-size 截断会留下 0.5px 真溢出，先与未修改 vendor 严格对照。
			nativeCombo = true; frame = idle(24); nativeCombo = false;
			checks.Expect(frame.popup && frame.popup->ScrollbarY == shortBar && frame.popup->ScrollbarSizes.x == shortReserve && frame.popup->ScrollMax.y == shortMax,
				"short real Combo scrollbar visibility and range exactly match the same-version native vendor at fractional DPI");
			if (frame.popup) std::cout << "[ComboNativeShortEndpoint] scale=" << scale << " bar=" << frame.popup->ScrollbarY
				<< " reserve=" << frame.popup->ScrollbarSizes.x << " max=" << frame.popup->ScrollMax.y << " content=" << frame.popup->ContentSize.y
				<< " inner=" << frame.popup->InnerRect.GetHeight() << " size=" << frame.popup->Size.y << " padding=" << frame.popup->WindowPadding.y << '\n';
			comboCount = 40; frame = idle(24); comboCount = 0; bool emptyFade = false;
			for (int index = 0; index < 24; ++index)
			{
				frame = draw(outside, false);
				emptyFade |= frame.popup && !frame.popup->ScrollbarY && frame.popup->ScrollbarSizes.x > 0 && frame.popup->ScrollbarSizes.x < 7.99F * scale;
			}
			// 空列表的原生圆角最小高度足以容纳 padding，是所有倍率都真实零溢出的 fixture。
			checks.Expect(emptyFade && frame.popup && !frame.popup->ScrollbarY && frame.popup->ScrollbarSizes.x == 0 && frame.popup->ScrollMax.y == 0,
				"a genuinely fitting empty real Combo releases its native rail and has zero scrolling range at every scale");
			if (frame.popup) std::cout << "[ComboEmptyEndpoint] scale=" << scale << " bar=" << frame.popup->ScrollbarY
				<< " reserve=" << frame.popup->ScrollbarSizes.x << " max=" << frame.popup->ScrollMax.y << '\n';
			if (frame.popup)
			{
				const float pageBefore = frame.pageScroll; const ImVec2 pad(frame.popup->InnerRect.Min.x + 1, frame.popup->InnerRect.GetCenter().y);
				draw(pad, false); draw(pad, true); frame = draw({ pad.x, pad.y - 12 * scale }, true); draw(outside, false);
				checks.Expect(Near(frame.pageScroll, pageBefore), "a no-range Combo cannot propagate its content drag to the background page");
			}
			closePopup = true; frame = idle(1); closePopup = false; comboCount = 40; frame = idle(4);
			opening = center(frame.combo); draw(opening, false); draw(opening, true); frame = draw(opening, false);
			checks.Expect(frame.popup && frame.popup->ScrollbarSizes.x < 7.99F * scale, "closed Combo reopens with a new visibility transition rather than stale full-width decoration");
			frame = idle(24); closePopup = true; frame = idle(1); closePopup = false;
		}
		comboAnimated = false; frame = idle();
		state.mouseReleaseBarrier = true; reset = 100 * scale; frame = idle(); reset = -1;
		const ImVec2 blank(frame.page->InnerClipRect.Max.x - 25 * scale, frame.page->InnerClipRect.GetCenter().y);
		draw(blank, false); draw(blank, true); draw({ blank.x, blank.y - 30 * scale }, true); frame = draw({ blank.x, blank.y - 30 * scale }, true);
		checks.Expect(Near(frame.pageScroll, 100 * scale, 1), "ended disabled scopes cannot bypass the navigation release input barrier at frame end");
		draw(outside, false); state.mouseReleaseBarrier = false; idle();
		std::cout << "[DragScrollDrawData] scale=" << scale << " frames=" << frames << " rootMovingFrames=" << movingFrames << '\n';
	}

	// b8beea0b 的实际旧部署，仅用于同一真实主页输入/CPU对照，不作为新算法测试替身。
	void LegacyScrollWhenDraggingOnVoid(const ImVec2& delta, ImGuiMouseButton mouse_button)
	{
		ImGuiContext& g = *ImGui::GetCurrentContext();
		ImGuiWindow* window = g.CurrentWindow;
		bool hovered = false;
		bool held = false;
		ImGuiID id = window->GetID("##scrolldraggingoverlay");
		ImGui::KeepAliveID(id);

		ImGui::ButtonBehavior(window->Rect(), id, &hovered, &held, mouse_button);
		if (!held && ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows) && ImGui::IsMouseDown(mouse_button))
			held = true;

		if (held && delta.x != 0.0f) ImGui::SetScrollX(window, window->Scroll.x + delta.x);
		if (held && delta.y != 0.0f) ImGui::SetScrollY(window, window->Scroll.y + delta.y);
	}

	void ApplyLegacyDragScrolling(const Design::NavigationState& state)
	{
		ImGuiContext& g = *ImGui::GetCurrentContext();
		auto eligible = [&](ImGuiWindow* window) {
			if (!window || !window->Active || window->Hidden || window->SkipItems || window->ScrollMax.y <= 0.0F
				|| (window->Flags & (ImGuiWindowFlags_NoMouseInputs | ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_Tooltip))) return false;
			// 已绘制却已关闭的 Popup、淡出导航及释放屏障都不能继续消费正文输入。
			for (auto* ancestor = window; ancestor; ancestor = ancestor->ParentWindow)
				if ((ancestor->Flags & ImGuiWindowFlags_Popup)
					&& !ImGui::IsPopupOpen(ancestor->PopupId, ImGuiPopupFlags_AnyPopupLevel)) return false;
			if (window->ID == state.flyoutWindowId)
				return state.flyoutOpen && !state.mouseReleaseBarrier;
			if (state.BlockContentInput()) return window->ID == state.navigationScrollWindowId && !state.mouseReleaseBarrier;
			return true;
		};
		ImGuiWindow* owner = nullptr;
		// overlay 抓取后即使移出也继续 KeepAlive；先选唯一指针，避免 FocusWindow 重排窗口列表。
		if (eligible(g.ActiveIdWindow) && g.ActiveId == g.ActiveIdWindow->GetID("##scrolldraggingoverlay"))
			owner = g.ActiveIdWindow;
		if (!owner)
			for (auto* window = g.HoveredWindow; window; window = window->ParentWindow)
				if (eligible(window)) { owner = window; break; }
		if (!owner) return;
		// End 已弹出内裁剪；局部恢复原调用环境，helper 不绘制，不改变字体/表格或 Begin 栈。
		ImGuiWindow* previous = g.CurrentWindow;
		const ImRect previousClip = owner->ClipRect;
		g.CurrentWindow = owner;
		owner->ClipRect = owner->InnerClipRect;
		ImVec2 mouse_delta = ImGui::GetIO().MouseDelta;
		LegacyScrollWhenDraggingOnVoid(ImVec2(0.0f, -mouse_delta.y), ImGuiMouseButton_Left);
		owner->ClipRect = previousClip;
		g.CurrentWindow = previous;
	}


	void CheckHomeScrollFollowup(Checks& checks, ImFont* regular, ImFont* strong, float scale)
	{
		Design::BindTextFonts(regular, strong); Design::ApplyPalette(false);
		auto& io = ImGui::GetIO(); io.ConfigInputTrickleEventQueue = false;
		Design::NavigationState state; bool animated = false, dark = false;
		float viewport = 380, rate = 1, requested = -1, lateTarget = -1; ImGuiWindow* lateOwner = nullptr;
		int frames = 0, actions = 0; bool legacyMode = false, benchmarking = false;
		std::array<std::array<std::vector<double>, 2>, 2> samples;
		struct Frame { ImGuiWindow* outer = nullptr; ImGuiWindow* body = nullptr; ImGuiWindow* author = nullptr;
			float scroll = 0, width = 0, reserve = 0, glyphY = FLT_MAX, authorGlyphY = FLT_MAX;
			ImGuiWindow* common = nullptr; ImRect feature{ { FLT_MAX, FLT_MAX }, { -FLT_MAX, -FLT_MAX } };
			ImVec2 text; ImGuiID active = 0; bool bar = false; unsigned alpha = 0; };
		const ImVec2 outside(-100, -100);
		auto draw = [&](ImVec2 mouse, bool down) {
			Design::ApplyPalette(dark); io.DisplaySize = { 1000 * scale, 1000 * scale }; io.DeltaTime = 1.0F / 60;
			io.AddMousePosEvent(mouse.x, mouse.y); io.AddMouseButtonEvent(0, down); ImGui::NewFrame();
			const auto begin = std::chrono::steady_clock::now(); if (!legacyMode) Design::BeginDragScrolling(state);
			Design::ConfigureRowAnimations(io.DeltaTime, animated, rate);
			ImGui::SetNextWindowPos({ 0, 0 }); ImGui::SetNextWindowSize(io.DisplaySize);
			ImGui::Begin("##home-scroll-production-root", nullptr, ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize
				| ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoTitleBar);
			ImGui::SetCursorPos({ 40 * scale, 50 * scale });
			ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, { 28 * scale, 28 * scale });
			if (lateTarget >= 0 && lateOwner) ImGui::SetScrollY(lateOwner, lateTarget);
			Design::BeginScrollChild("##setting-content", { 850 * scale, viewport * scale }, ImGuiChildFlags_AlwaysUseWindowPadding);
			ImGui::PopStyleVar(); Frame frame; frame.outer = ImGui::GetCurrentWindow();
			if (requested >= 0) ImGui::SetScrollY(requested);
			const float available = ImGui::GetContentRegionAvail().x;
			const float pageWidth = (std::min)(available, Design::Pixels(Design::PageMaximumWidth));
			ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (available - pageWidth) * 0.5F);
			Design::BeginScrollChild("##setting-page-content", { pageWidth, 0 }, ImGuiChildFlags_AutoResizeY);
			frame.body = ImGui::GetCurrentWindow(); frame.text = ImGui::GetCursorScreenPos();
			const auto styleBefore = ImGui::GetStyle(); const int colorsBefore = GImGui->ColorStack.Size, varsBefore = GImGui->StyleVarStack.Size;
			actions += Design::RenderHome({ 0, 1.0F, "3.0.0-dev.3981", 0, 0, 1.0F }) != Design::HomeAction::None;
			checks.Expect(ImGui::GetStyle().ScrollbarSize == styleBefore.ScrollbarSize && ImGui::GetStyle().ScrollbarPadding == styleBefore.ScrollbarPadding
				&& ImGui::GetStyle().WindowPadding.x == styleBefore.WindowPadding.x && ImGui::GetStyle().WindowPadding.y == styleBefore.WindowPadding.y
				&& std::memcmp(ImGui::GetStyle().Colors, styleBefore.Colors, sizeof(styleBefore.Colors)) == 0
				&& GImGui->ColorStack.Size == colorsBefore && GImGui->StyleVarStack.Size == varsBefore,
				"actual Home Card/child scopes restore scrollbar fields, padding, all colors and vendor style stack depths exactly");
			ImGui::EndChild(); ImGui::EndChild(); ImGui::End();
			for (auto* window : GImGui->Windows) if (window->Active)
			{
				const std::string name(window->Name);
				if (name.find("##author_") != std::string::npos) frame.author = window;
				if (name.find("##home-common_") != std::string::npos) frame.common = window;
			}
			if (legacyMode) ApplyLegacyDragScrolling(state); else Design::ApplyDragScrolling(state); frame.active = GImGui->ActiveId;
			frame.scroll = frame.outer->Scroll.y + frame.body->Scroll.y;
			frame.width = frame.outer->ContentRegionRect.GetWidth(); frame.reserve = frame.outer->ScrollbarSizes.x; frame.bar = frame.outer->ScrollbarY;
			ImGui::Render(); ++frames;
			if (benchmarking && (!down || !io.MouseClicked[0]))
				samples[legacyMode ? 1 : 0][down ? 1 : 0].push_back(std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - begin).count());
			const auto white = io.Fonts->TexUvWhitePixel;
			for (const auto& vertex : frame.body->DrawList->VtxBuffer)
			{
				if (frame.glyphY == FLT_MAX && vertex.col == Design::TextPrimary && (vertex.uv.x != white.x || vertex.uv.y != white.y)) frame.glyphY = vertex.pos.y;
				if (vertex.col == Design::GetPalette().featureTint[0]) frame.feature.Add(vertex.pos);
			}
			if (frame.author) for (const auto& vertex : frame.author->DrawList->VtxBuffer)
				if (frame.authorGlyphY == FLT_MAX && vertex.col == Design::TextPrimary && (vertex.uv.x != white.x || vertex.uv.y != white.y)) frame.authorGlyphY = vertex.pos.y;
			checks.Expect(ImGui::GetDrawData()->Valid && ImGui::GetDrawData()->TotalVtxCount > 0, "actual production Home draws cards and controls every input frame");
			const ImRect rail = ImGui::GetWindowScrollbarRect(frame.outer, ImGuiAxis_Y);
			for (const auto* list : ImGui::GetDrawData()->CmdLists) for (const auto& vertex : list->VtxBuffer)
				if (vertex.pos.x >= rail.Min.x && vertex.pos.x <= rail.Max.x && vertex.pos.y >= rail.Min.y && vertex.pos.y <= rail.Max.y
					&& (vertex.col & ~IM_COL32_A_MASK) == (Design::GetPalette().scrollbar & ~IM_COL32_A_MASK))
					frame.alpha = (std::max)(frame.alpha, (vertex.col >> IM_COL32_A_SHIFT) & 255);
			return frame;
		};
		auto idle = [&](int count = 4) { Frame frame; for (int index = 0; index < count; ++index) frame = draw(outside, false); return frame; };
		Frame frame = idle(8);
		checks.Expect(frame.outer->ScrollbarY && frame.outer->ScrollMax.y > 0, "actual Home content exceeds its real production viewport");
		for (auto source : { ImGuiMouseSource_Mouse, ImGuiMouseSource_TouchScreen })
		{
			io.AddMouseSourceEvent(source); requested = 0; frame = idle(); requested = -1;
			const ImVec2 hero(frame.text.x + 400 * scale, frame.text.y + 140 * scale);
			draw(hero, false); frame = draw(hero, true); const float before = frame.scroll, textBefore = frame.text.y, glyphBefore = frame.glyphY;
			frame = draw({ hero.x, hero.y - 20 * scale }, true);
			checks.Expect(Near(frame.scroll - before, 20 * scale, 1) && Near(textBefore - frame.text.y, 20 * scale, 1) && Near(glyphBefore - frame.glyphY, 20 * scale, 1),
				"Home hero drag updates actual Scroll and production content position in the movement frame without an extra frame");
			const auto capture = frame.active;
			frame = draw({ hero.x - 300 * scale, hero.y - 45 * scale }, true);
			checks.Expect(capture && frame.active == capture && Near(frame.scroll - before, 45 * scale, 1), "Home captured drag crosses noninteractive content and sibling regions without losing ownership");
			draw(outside, false); requested = frame.scroll + frame.feature.Min.y - frame.outer->InnerRect.Min.y - 40 * scale; frame = idle(); requested = -1;
			// 真实 feature card 位于 hero 后；用其绘制文本/图标区域起拖，而非空窗口。
			checks.Expect(frame.feature.GetWidth() > 0 && frame.feature.GetHeight() > 0, "actual Home feature tint geometry is present in production DrawData");
			const ImVec2 feature = frame.feature.GetCenter();
			draw(feature, false); frame = draw(feature, true); const float cardBefore = frame.scroll; const auto cardId = frame.active;
			const int actionBefore = actions;
			frame = draw({ feature.x, feature.y - 25 * scale }, true);
			checks.Expect(cardId && frame.active != cardId && Near(frame.scroll - cardBefore, 25 * scale, 1), "actual Home feature content card crosses the native drag threshold and transfers its stable ActiveId to scrolling");
			draw({ feature.x, feature.y - 25 * scale }, false); frame = idle();
			checks.Expect(actions == actionBefore, "dragging Home content card cancels its navigation click exactly once");
			requested = frame.outer->ScrollMax.y; frame = idle(); requested = -1;
			checks.Expect(frame.common && frame.author && frame.authorGlyphY != FLT_MAX, "real Home author/card and common siblings are visible at the bottom");
			if (frame.common && frame.author)
			{
				const ImVec2 author(frame.author->WorkRect.Min.x + scale, frame.author->WorkRect.Min.y + 10 * scale);
				draw(author, false); frame = draw(author, true); const float previous = frame.scroll, authorGlyph = frame.authorGlyphY;
				frame = draw({ author.x, author.y + 20 * scale }, true); const auto ownerId = frame.active;
				checks.Expect(Near(previous - frame.scroll, 20 * scale, 1) && Near(frame.authorGlyphY - authorGlyph, 20 * scale, 1),
					"drag from actual nested Home author card updates ancestor Scroll and actual author glyph vertices in the same movement frame");
				frame = draw({ frame.common->WorkRect.Min.x + 20 * scale, author.y + 30 * scale }, true);
				checks.Expect(ownerId && frame.active == ownerId && Near(previous - frame.scroll, 30 * scale, 1), "captured author drag crosses to the actual common sibling without losing or doubling its owner");
				draw(outside, false); frame = idle(); requested = frame.outer->ScrollMax.y; frame = idle(); requested = -1;
				const ImVec2 common(frame.common->WorkRect.Min.x + 100 * scale, frame.common->WorkRect.Min.y + 50 * scale);
				draw(common, false); frame = draw(common, true); const float commonBefore = frame.scroll; const auto commonId = frame.active;
				frame = draw({ common.x, common.y + 20 * scale }, true);
				checks.Expect(commonId && frame.active != commonId && Near(commonBefore - frame.scroll, 20 * scale, 1), "actual common settings entry transfers its real ActiveId to its nearest scrolling ancestor after the native threshold");
				draw(outside, false); checks.Expect(actions == actionBefore, "nested common/author dragging does not invoke Home actions");
			}

		}
		io.AddMouseSourceEvent(ImGuiMouseSource_Mouse); requested = 0; frame = idle(); requested = -1;
		legacyMode = true; requested = 0; frame = idle(); requested = -1;
		const ImVec2 baselinePoint(frame.outer->InnerRect.Min.x + 2 * scale, frame.outer->InnerRect.GetCenter().y);
		draw(baselinePoint, false); frame = draw(baselinePoint, true); const float legacyBefore = frame.scroll, legacyGlyph = frame.glyphY;
		frame = draw({ baselinePoint.x, baselinePoint.y - 20 * scale }, true);
		checks.Expect(Near(frame.scroll, legacyBefore) && Near(frame.glyphY, legacyGlyph), "exact accepted legacy helper leaves actual Home Scroll and glyphs one frame behind the movement input");
		frame = draw({ baselinePoint.x, baselinePoint.y - 20 * scale }, true);
		checks.Expect(Near(frame.scroll - legacyBefore, 20 * scale, 1) && Near(legacyGlyph - frame.glyphY, 20 * scale, 1), "exact legacy helper updates real Home only at the next native Begin");
		draw(outside, false); legacyMode = false; requested = 0; frame = idle(); requested = -1;
		const ImVec2 precise(frame.outer->InnerRect.Min.x + 2 * scale, frame.outer->InnerRect.GetCenter().y);
		draw(precise, false); frame = draw(precise, true); const float preciseBefore = frame.scroll;
		float actualDelta = 0;
		for (int index = 1; index <= 20; ++index)
		{ frame = draw({ precise.x, precise.y - index * 0.25F }, true); actualDelta -= io.MouseDelta.y; }
		checks.Expect(Near(frame.scroll - preciseBefore, actualDelta, 1), "high-frequency quarter-pixel events use the real ImGui-quantized MouseDelta without lost or repeated motion");
		ImGui::SetScrollY(frame.outer, frame.outer->Scroll.y + 13 * scale); frame = draw({ precise.x, precise.y - 8 }, true);
		checks.Expect(Near(frame.outer->Scroll.y - preciseBefore - actualDelta, 13 * scale, 1), "an existing native external scroll target retains priority over captured content dragging");
		const float lateBefore = frame.scroll; lateOwner = frame.outer; lateTarget = frame.outer->Scroll.y + 9 * scale;
		frame = draw({ precise.x, precise.y - 11 }, true); lateTarget = -1;
		checks.Expect(Near(frame.scroll - lateBefore, 9 * scale, 1), "an external target posted after drag sampling but before the actual Begin is not overwritten by pending content motion");
		const float resumed = frame.scroll; frame = draw({ precise.x, precise.y - 14 }, true);
		checks.Expect(Near(frame.scroll - resumed, 3, 1), "captured motion rebases from the actual consumed external target on its next frame");
		draw(outside, false); requested = frame.scroll + frame.feature.Min.y - frame.outer->InnerRect.Min.y - 40 * scale; frame = idle(); requested = -1;
		io.ConfigInputTrickleEventQueue = true; const auto tap = frame.feature.GetCenter(); const int tapsBefore = actions;
		draw(tap, false); io.AddMouseButtonEvent(0, true); io.AddMouseButtonEvent(0, false);
		draw(tap, false); frame = draw(tap, false); frame = idle(2);
		checks.Expect(actions == tapsBefore + 1, "a quick real Home card tap with default input trickling invokes exactly one original navigation action");
		io.ConfigInputTrickleEventQueue = false; requested = 0; frame = idle(); requested = -1;
		// 原生末帧目标的对照：真实 SetScrollY 不改变已提交内容，新部署在下一帧 Begin 前消费。
		const float oldScroll = frame.outer->Scroll.y; ImGui::SetScrollY(frame.outer, oldScroll + 18 * scale);
		checks.Expect(Near(frame.outer->Scroll.y, oldScroll), "native frame-end SetScrollY baseline leaves the already-rendered frame unchanged");
		frame = idle(1); checks.Expect(Near(frame.outer->Scroll.y - oldScroll, 18 * scale, 1), "native baseline applies only at the following Begin, unlike the held production drag movement frame");
		requested = 0; frame = idle(); requested = -1;
		for (bool theme : { false, true })
		{
			dark = theme; animated = true; viewport = 2400; frame = idle(24);
			checks.Expect(!frame.bar && frame.reserve == 0, "Home overflow removal completes with no permanent scrollbar reservation");
			const float fullWidth = frame.width; viewport = 380;
			for (int index = 0; index < 12; ++index) { frame = draw(outside, false);
				std::cout << "[HomeScrollbarEnter] scale=" << scale << " step=" << index << " bar=" << frame.bar << " reserve=" << frame.reserve << " width=" << frame.width << " alpha=" << frame.alpha << '\n';
				if (frame.bar && frame.reserve > 0 && frame.reserve < 7.99F * scale) break; }
			checks.Expect(frame.bar && frame.reserve > 0 && frame.reserve < Design::Pixels(Design::ScrollbarTrackSize), "actual Home native scrollbar appears with a partial animated width");
			checks.Expect(frame.width < fullWidth && frame.width > fullWidth - Design::Pixels(Design::ScrollbarTrackSize), "actual Home content width continuously shrinks with its native rail");
			checks.Expect(frame.alpha > 0 && frame.alpha < ((Design::GetPalette().scrollbar >> IM_COL32_A_SHIFT) & 255), "actual Home thumb DrawData fades in together with its width");
			frame = idle(24); const float settled = frame.reserve;
			viewport = 2400;
			for (int index = 0; index < 12; ++index) { frame = draw(outside, false);
				std::cout << "[HomeScrollbarExit] scale=" << scale << " step=" << index << " bar=" << frame.bar << " reserve=" << frame.reserve << " width=" << frame.width << " alpha=" << frame.alpha << '\n';
				if (!frame.bar && frame.reserve > 0 && frame.reserve < settled - 0.01F) break; }
			checks.Expect(!frame.bar && frame.reserve > 0 && frame.reserve < settled, "native hidden Home rail retains only fading decoration and smoothly releases its content width");
			checks.Expect(frame.alpha > 0, "hidden Home rail has actual fading thumb vertices");
			const float reversing = frame.reserve; viewport = 380; frame = draw(outside, false);
			checks.Expect(frame.reserve <= reversing + 0.1F * scale, "quick overflow reversal continues from the currently visible scrollbar width");
			rate = 2.5F; frame = idle(16); viewport = 2400; animated = false; frame = idle(2);
			checks.Expect(!frame.bar && frame.reserve == 0 && Near(frame.width, fullWidth, 1), "animation disable restores full native content width with zero hidden input reservation");
		}
		animated = false; dark = false; viewport = 380; rate = 1;
		for (bool baseline : { true, false })
		{
			legacyMode = baseline; requested = 100 * scale; frame = idle(12); requested = -1;
			benchmarking = true; frame = idle(100); benchmarking = false;
			const ImVec2 point(frame.outer->InnerRect.Min.x + 2 * scale, frame.outer->InnerRect.GetCenter().y);
			draw(point, false); frame = draw(point, true); const float before = frame.scroll;
			benchmarking = true;
			for (int index = 1; index <= 100; ++index) frame = draw({ point.x, point.y - index }, true);
			benchmarking = false; frame = draw({ point.x, point.y - 100 }, true);
			checks.Expect(Near(frame.scroll - before, 100, 1), "both warmed legacy and new CPU samples actually scroll the same production Home by the same total input");
			draw(outside, false); frame = idle();
		}
		for (int baseline = 0; baseline < 2; ++baseline) for (int held = 0; held < 2; ++held)
		{
			auto values = samples[baseline][held]; std::sort(values.begin(), values.end());
			checks.Expect(values.size() == 100, "Home CPU timing separates exactly 100 warmed idle or captured frames per actual implementation");
			if (!values.empty()) std::cout << "[HomeCpuSubmission] scale=" << scale << " legacy=" << baseline << " held=" << held << " samples=" << values.size()
				<< " medianUs=" << values[values.size() / 2] << " p95Us=" << values[values.size() * 95 / 100] << '\n';
		}
		std::cout << "[HomeScrollFollowup] scale=" << scale << " frames=" << frames << '\n';
		Design::ConfigureRowAnimations(0, false, 1); Design::ApplyPalette(false);
	}

	void CheckHomeBrandDesign(Checks& checks, const std::array<ImFont*, 5>& fonts,
		const std::filesystem::path& fontDirectory, float scale)
	{
		using Design::HomeAction;
		const auto& mesh = Design::HomeBrand::Geometry();
		checks.Expect(&mesh == &Design::HomeBrand::Geometry() && mesh.size.y > mesh.size.x*.38F
			&& mesh.vertices.size() < 65536, "complete cached website glyph includes the y descender and fits the native index budget");
		auto& io = ImGui::GetIO(); io.ConfigInputTrickleEventQueue = false;
		const auto previousFlags = io.ConfigFlags;
		io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
		const ImVec2 outside(-100,-100);
		int frames = 0, actionCount = 0, wrappedLicenseRows = 0, wrappedLicenseCases = 0, entryPlays=0, entrySkips=0;
		for (const char* language : {"zh-CN","zh-TW","en-US"})
		{
			checks.Expect(LoadNavigationLabels(fontDirectory,language), "home fixtures load each actual localization source");
			const bool traditional = std::strcmp(language,"zh-TW")==0;
			Design::BindTextFonts(fonts[traditional ? 2 : 0],fonts[traditional ? 3 : 1]); ImFluent::SetIconFont(fonts.back());
			const auto label = [&](const char* key) { return I18n::getA(std::string("SettingsUI/Design/")+key); };
			const std::string welcome = label("HomeWelcome"), signature=label("HomeCopyright");
			const float fullLicenseWidth = Design::TextWidth(label("HomeLicenseName").c_str(),ImFluentTextStyle_Caption);
			checks.Expect(welcome == (traditional ? "歡迎使用 Inkeys3" : std::strcmp(language,"en-US")==0
				? "Welcome to Inkeys3" : "欢迎使用 智绘教Inkeys 3"), "home uses the approved localized welcome");
			checks.Expect(signature.find("2023-2026")!=std::string::npos && (std::strcmp(language,"en-US")!=0
				|| (signature.find("Chen Runlin")!=std::string::npos && signature.find("Chen Yunlam")!=std::string::npos)),
				"copyright retains the full year range and both English names");
			const std::string slogan=label("HeroTitle")+label("HeroJoin")+label("HeroDescription");
			checks.Expect(slogan == (traditional ? "讓螢幕上的書寫行雲流水，讓每一次操作都賞心悅目。" : std::strcmp(language,"en-US")==0
				? "Fluid ink. Beautiful motion. Screen annotation that feels as good as it looks."
				: "让屏幕上的书写行云流水，让每一次操作都赏心悦目。"), "two measured slogan paragraphs retain the exact approved full wording");
			for (bool dark : {false,true}) for (const ImVec2 size : {ImVec2(320,520),ImVec2(480,520),ImVec2(720,520),ImVec2(960,700),ImVec2(1440,900)})
			for (bool expanded : {false,true})
			{
				Design::NavigationState state; state.Resize(size.x); if (state.Expanded()!=expanded) state.Toggle();
				const auto shell=Design::ResolveShellGeometry(size.x,size.y,state);
				float viewport=shell.content.Height(), requested=-1;
				bool probeEntry=false; Design::HomeMotionState gateMotion;
				std::unordered_set<ImU32> inkColors; for (const auto& v : mesh.vertices) inkColors.insert(dark ? v.dark : v.light);
				struct GlyphGroup { ImRect bounds; ImU32 color; };
				struct Frame { ImGuiWindow* outer; ImGuiWindow* page; HomeAction action; ImGuiID hovered, active;
					ImRect brand{{FLT_MAX,FLT_MAX},{-FLT_MAX,-FLT_MAX}}, nav;
					float featureY=FLT_MAX, pageTop, width; std::vector<GlyphGroup> groups; std::vector<std::pair<ImGuiID,HomeAction>> ids; };
				auto draw = [&](ImVec2 mouse, bool down) {
					Design::ApplyPalette(dark); Design::ConfigureRowAnimations(0,false,1);
					io.DisplaySize={size.x*scale,2600*scale}; io.AddMousePosEvent(mouse.x,mouse.y); io.AddMouseButtonEvent(0,down);
					ImGui::NewFrame(); Design::BeginDragScrolling(state);
					ImGui::SetNextWindowPos({0,0}); ImGui::SetNextWindowSize(io.DisplaySize);
					ImGui::Begin("##home-brand-root",nullptr,ImGuiWindowFlags_NoTitleBar|ImGuiWindowFlags_NoResize|ImGuiWindowFlags_NoCollapse
						|ImGuiWindowFlags_NoSavedSettings|ImGuiWindowFlags_NoScrollbar|ImGuiWindowFlags_NoScrollWithMouse);
					ImGui::SetCursorPos({shell.content.left*scale,shell.content.top*scale});
					Design::BeginScrollChild("##setting-content",{shell.content.Width()*scale,viewport*scale},ImGuiChildFlags_AlwaysUseWindowPadding);
					Frame f; f.outer=ImGui::GetCurrentWindow(); if (requested>=0) ImGui::SetScrollY(requested);
					const float available=ImGui::GetContentRegionAvail().x, width=(std::min)(available,Design::Pixels(Design::PageMaximumWidth));
					ImGui::SetCursorPosX(ImGui::GetCursorPosX()+(available-width)*.5F);
					Design::BeginScrollChild("##setting-page-content",{width,0},ImGuiChildFlags_AutoResizeY);
					f.page=ImGui::GetCurrentWindow(); f.pageTop=ImGui::GetCursorScreenPos().y; f.width=ImGui::GetContentRegionAvail().x;
					const int fontStack=GImGui->FontStack.Size, colorStack=GImGui->ColorStack.Size, varStack=GImGui->StyleVarStack.Size;
					if (probeEntry)
					{
						Design::ConfigureHomeMotion(gateMotion,true,1,0,true,1);
						const auto clip=f.outer->ClipRect;
						Design::ConfigureHomeEntryViewport(gateMotion,{0,32*scale,size.x*scale,size.y*scale},
							{clip.Min.x,clip.Min.y,clip.Max.x,clip.Max.y});
					}
					f.action=Design::RenderHome({0,1,"3.0.0-dev.3981",0,0,1},probeEntry ? &gateMotion : nullptr); actionCount += f.action!=HomeAction::None;
					checks.Expect(fontStack==GImGui->FontStack.Size && colorStack==GImGui->ColorStack.Size && varStack==GImGui->StyleVarStack.Size,
						"real home brand/copyright/card restores font, color and style scopes");
					f.ids={{ImGui::GetID("##home-star"),HomeAction::GitHub},{ImGui::GetID("##home-sponsor"),HomeAction::Support}};
					for (const auto section : {std::pair{"##home-license",1},std::pair{"##home-repositories",1},std::pair{"##home-repositories",3}})
					{
						ImGui::PushID(section.first); ImGui::PushID(section.second);
						for (int line=0;line<10;++line) { ImGui::PushID(line); f.ids.emplace_back(ImGui::GetID("##inline-link"),
							section.second==3 ? HomeAction::GitCode : std::strcmp(section.first,"##home-license")==0 ? HomeAction::License : HomeAction::GitHub); ImGui::PopID(); }
						ImGui::PopID(); ImGui::PopID();
					}
					ImGui::EndChild(); ImGui::EndChild();
					if (probeEntry) Design::RenderHomeEntryOverlay(gateMotion);
					ImGui::End(); Design::ApplyDragScrolling(state);
					f.hovered=GImGui->HoveredId; f.active=GImGui->ActiveId;
					f.nav=f.page->NavRectRel[ImGuiNavLayer_Main]; f.nav.Translate(f.page->Pos);
					ImGui::Render(); ++frames;
					const auto white=io.Fonts->TexUvWhitePixel;
					for (const auto& vertex : f.page->DrawList->VtxBuffer)
					{
						if (vertex.uv.x==white.x && vertex.uv.y==white.y && inkColors.contains(vertex.col)) f.brand.Add(vertex.pos);
						if (vertex.col==Design::GetPalette().featureTint[0]) f.featureY=(std::min)(f.featureY,vertex.pos.y);
					}
					// 从真实字形 quad 聚合文字块，点击定位不复制产品的横幅/版权/按钮高度公式。
					const auto& list=*f.page->DrawList;
					for (const auto& cmd : list.CmdBuffer) for (unsigned i=0;i+5<cmd.ElemCount;++i)
					{
						const auto* index=list.IdxBuffer.Data+cmd.IdxOffset+i;
						if (index[0]!=index[3] || index[2]!=index[4]) continue;
						const auto& a=list.VtxBuffer[cmd.VtxOffset+index[0]]; const auto& c=list.VtxBuffer[cmd.VtxOffset+index[2]];
						if (a.col!=c.col || (a.uv.x==white.x && a.uv.y==white.y) || a.uv.x==c.uv.x || a.uv.y==c.uv.y) continue;
						if (a.pos.y<=f.brand.Max.y || a.pos.y>=f.featureY) continue;
						ImRect bounds(a.pos,c.pos); bool merged=false;
						for (auto& group : f.groups) if (group.color==a.col && group.bounds.Min.y<bounds.Max.y && bounds.Min.y<group.bounds.Max.y
							&& bounds.Min.x>=group.bounds.Max.x-scale*2 && bounds.Min.x<=group.bounds.Max.x+scale*8)
						{ group.bounds.Add(bounds); merged=true; break; }
						if (!merged) f.groups.push_back({bounds,a.col}); i+=5;
					}
					CheckDrawData(checks); return f;
				};
				auto idle = [&](int count=4) { Frame f; for (int i=0;i<count;++i) f=draw(outside,false); return f; };
				Frame frame=idle();
				probeEntry=true; const auto gateFrame=draw(outside,false); probeEntry=false;
				const ImRect hero(gateMotion.heroOrigin,{gateMotion.heroOrigin.x+gateMotion.heroSize.x,gateMotion.heroOrigin.y+gateMotion.heroSize.y});
				if (gateMotion.EntryActive())
				{
					++entryPlays;
					checks.Expect(gateFrame.outer->ClipRect.Contains(hero) && gateMotion.entryElapsed==0,
						"actual language/theme/navigation/scale layout plays only with the whole hero in its viewport");
				}
				else
				{
					++entrySkips;
					checks.Expect(gateMotion.entry==Design::HomeMotionState::Entry::Skipped,
						"insufficient actual matrix layout consumes Show and renders the normal static page");
				}
				frame=idle(1);
				checks.Expect(frame.outer->ScrollMax.y>0 && frame.page->ScrollMax.y==0 && frame.outer->ScrollMax.x==0,
					"real window-sized home scrolls vertically at its ancestor without nested or horizontal overflow");
				viewport=2400; frame=idle();
				const float inset=Design::Pixels(frame.width>=Design::Pixels(700) ? 28.0F : 20.0F);
				checks.Expect(Near(frame.brand.GetWidth(),(std::min)((frame.width-inset*2)*.8F,Design::Pixels(520)),1)
					&& Near(frame.brand.GetHeight()/frame.brand.GetWidth(),mesh.size.y/mesh.size.x,.002F)
					&& frame.brand.Min.y>=frame.pageTop && frame.brand.Max.y<frame.featureY,
					"actual cached rainbow DrawData scales within the hero and retains complete aspect/descender in every theme/width/scale");
				std::vector<float> sloganRows;
				for (const auto& group : frame.groups) if (group.color==Design::GetPalette().heroDescription)
				{
					if (sloganRows.empty() || !Near(sloganRows.back(),group.bounds.Min.y,scale*2)) sloganRows.push_back(group.bounds.Min.y);
					checks.Expect(group.bounds.Min.y>frame.brand.Max.y+scale*20,"real slogan has readable separation below the complete descender");
				}
				const float inner=frame.width-inset*2;
				checks.Expect(Design::TextWidth(slogan.c_str())<=inner ? sloganRows.size()==1 : sloganRows.size()>=2,
					"actual localized slogan uses one regular-weight line when it fits and multiple lines when narrow");
				if (Design::TextWidth(slogan.c_str())>inner && Design::TextWidth(label("HeroTitle").c_str())<=inner
					&& Design::TextWidth(label("HeroDescription").c_str())<=inner)
					checks.Expect(sloganRows.size()==2,"both semantic slogan phrases occupy exactly two equal-style lines when they fit individually");
				for (const auto& group : frame.groups)
					checks.Expect(group.bounds.Min.x>=frame.page->WorkRect.Min.x-scale*2 && group.bounds.Max.x<=frame.page->WorkRect.Max.x+scale*2,
						"actual slogan/copyright/repository/button glyphs stay inside the page budget");
				if (scale==1 && !dark && !expanded && size.x<=720)
				{
					const auto candidates=frame.groups; std::vector<std::pair<ImGuiID,ImVec2>> links;
					for (const auto& group : candidates)
					{
						const ImVec2 point=group.bounds.GetCenter(); frame=draw(point,false);
						for (const auto [id,kind] : frame.ids) if (frame.hovered==id) { links.emplace_back(id,point); break; }
					}
					std::unordered_set<ImGuiID> checked;
					ImRect starBounds,sponsorBounds; int licenseTaps=0; bool licenseFound=false,githubFound=false,gitcodeFound=false,supportFound=false;
					for (const auto [id,point] : links)
					{
						if (!checked.insert(id).second) continue;
						frame=draw(point,false); HomeAction expected=HomeAction::None;
						for (const auto [item,kind] : frame.ids) if (item==id) expected=kind;
						const int before=actionCount; frame=draw(point,true); const ImRect hitBounds=frame.nav;
						checks.Expect(frame.active==id,"new links/buttons receive their real active item on down");
						frame=draw(point,false); checks.Expect(frame.action==expected && actionCount==before+1,"each actual wrapped link/button tap returns exactly its original action once");
						frame=idle(1); checks.Expect(actionCount==before+1,"idle rendering produces no extra home business action");
						// 使用真实轻点留下的焦点和键盘输入，不直接赋值 NavActivateId 伪造激活。
						checks.Expect(GImGui->NavId==id,"each new home link/button retains its actual keyboard focus after a tap");
						// 鼠标激活按原生规则隐藏焦点框；先用 Tab 进入键盘导航，再测试 Enter/Space。
						const int beforeTab=actionCount;
						io.AddKeyEvent(ImGuiKey_Tab,true); draw(outside,false);
						io.AddKeyEvent(ImGuiKey_Tab,false); draw(outside,false); frame=idle(1);
						checks.Expect(GImGui->NavId==id && GImGui->NavCursorVisible && actionCount==beforeTab,
							"real Tab enters keyboard navigation on each focused home link/button without activating it");
						for (const ImGuiKey key : {ImGuiKey_Enter,ImGuiKey_Space})
						{
							const int beforeKey=actionCount;
							io.AddKeyEvent(key,true); const auto keyDown=draw(outside,false);
							io.AddKeyEvent(key,false); const auto keyUp=draw(outside,false);
							checks.Expect((keyDown.action==expected || keyUp.action==expected) && actionCount==beforeKey+1,
								"real Enter/Space activates every wrapped home link and open-source button exactly once");
							frame=idle(1); checks.Expect(actionCount==beforeKey+1 && GImGui->NavId==id && GImGui->NavCursorVisible,
								"keyboard activation retains visible item focus and produces no action on the next idle frame");
						}
						if (id==frame.ids[0].first) starBounds=hitBounds;
						if (id==frame.ids[1].first) sponsorBounds=hitBounds;
						licenseFound |= expected==HomeAction::License; githubFound |= expected==HomeAction::GitHub;
						gitcodeFound |= expected==HomeAction::GitCode; supportFound |= expected==HomeAction::Support;
						wrappedLicenseRows += expected==HomeAction::License; licenseTaps += expected==HomeAction::License;
						viewport=shell.content.Height(); requested=(std::max)(0.0F,point.y-frame.outer->InnerRect.Min.y-viewport*.5F); frame=idle(); requested=-1;
						const ImVec2 visible(point.x,point.y-frame.outer->Scroll.y); frame=draw(visible,false); frame=draw(visible,true);
						const float scrollBefore=frame.outer->Scroll.y; const int beforeDrag=actionCount;
						frame=draw({visible.x,visible.y-25*scale},true);
						checks.Expect(frame.active!=id && Near(frame.outer->Scroll.y-scrollBefore,25*scale,1),"new text links and open-source buttons transfer capture to real scrolling after the native threshold");
						frame=draw({visible.x,visible.y-25*scale},false); checks.Expect(actionCount==beforeDrag,"dragging a new home link/button cancels the click action");
						viewport=2400; requested=0; frame=idle(); requested=-1; frame=idle();
					}
					checks.Expect(licenseFound && githubFound && gitcodeFound && supportFound,"actual drawn copyright/repository/star/sponsor controls expose all four business actions");
					wrappedLicenseCases += licenseTaps>=2;
					checks.Expect(fullLicenseWidth<=frame.width-inset*2 || licenseTaps>=2,"every license name fragment remains clickable when the real constrained content forces multiple lines");
					checks.Expect(starBounds.GetWidth()>0 && Near(starBounds.GetWidth(),sponsorBounds.GetWidth(),1)
						&& !starBounds.Overlaps(sponsorBounds),"real ImFluent star and heart button hit bounds are equal width and disjoint");
					checks.Expect(size.x!=320 || (Near(starBounds.Min.x,sponsorBounds.Min.x,1) && sponsorBounds.Min.y>=starBounds.Max.y),
						"constrained card content moves full width star/sponsor controls below the copy and stacks them without overlap");
				}
			}
		}
		checks.Expect(scale!=1 || wrappedLicenseRows>=3,"all real localized license controls were tapped without opening external applications");
		checks.Expect(scale!=1 || wrappedLicenseCases>0,"actual localized license link fragments include at least one genuinely wrapped case");
		checks.Expect(entryPlays>0 && entrySkips>0,"real three-language/theme/navigation size matrix includes both fitting and skipped entries");
		checks.Expect(LoadNavigationLabels(fontDirectory),"home tests restore the default Chinese fixtures for existing regressions");
		Design::BindTextFonts(fonts[0],fonts[1]); Design::ApplyPalette(false); Design::ConfigureRowAnimations(0,false,1);
		io.ConfigFlags = previousFlags;
		std::cout << "[HomeBrandDesign] scale=" << scale << " frames=" << frames << " linkTaps=" << actionCount
			<< " entry-plays="<<entryPlays<<" entry-skips="<<entrySkips<<'\n';
	}

	void CheckFrameSubmission(Checks& checks)
	{
		ImDrawList list(nullptr);
		list.VtxBuffer.resize(3); list.IdxBuffer.resize(3); list.CmdBuffer.resize(1);
		for (int i=0;i<3;++i)
		{
			list.VtxBuffer[i]={{static_cast<float>(i),static_cast<float>(i+1)},{.25F,.5F},IM_COL32(50,100,150,255)};
			list.IdxBuffer[i]=static_cast<ImDrawIdx>(i);
		}
		// ImVector::resize 不运行元素构造；显式初始化 callback/offset，避免把未定义字节当真实渲染命令。
		auto& cmd=list.CmdBuffer[0]; cmd=ImDrawCmd{};
		cmd.ClipRect={0,0,100,100}; cmd.TexRef=ImTextureRef(ImTextureID(31)); cmd.ElemCount=3;
		ImDrawData data; data.Valid=true; data.DisplaySize={100,100}; data.FramebufferScale={1,1};
		data.CmdLists.push_back(&list); data.CmdListsCount=1; data.TotalVtxCount=data.TotalIdxCount=3;
		Design::FrameSubmissionKey key; key.output.epoch=1; key.target=7; key.clear={.1F,.2F,.3F,1};
		Design::FrameSubmissionCache snapshot;
		checks.Expect(!snapshot.CanSkip(&data,key),"first output must actually submit before it can be deduplicated");
		snapshot.ConfirmPresented(&data,key);
		const auto stored=snapshot.StoredBytes(); const auto capacity=snapshot.Capacity();
		checks.Expect(stored>0 && stored<Design::FrameSubmissionCache::BudgetBytes && snapshot.CanSkip(&data,key),
			"successful output owns a bounded exact value snapshot");
		for (int i=0;i<100;++i) checks.Expect(snapshot.CanSkip(&data,key),"unchanged frames reuse the same successful output");
		checks.Expect(snapshot.StoredBytes()==stored && snapshot.Capacity()==capacity,"duplicate comparison does not grow or overwrite snapshot storage");
		auto reject=[&](auto mutate,auto restore) {
			mutate(); checks.Expect(!snapshot.CanSkip(&data,key),"each actual renderer field mutation forces submission");
			restore(); checks.Expect(snapshot.CanSkip(&data,key),"restoring exact output matches the owned successful snapshot");
		};
		auto scalar=[&](auto& field) { const auto old=field; reject([&]{field+=1;},[&]{field=old;}); };
		scalar(list.VtxBuffer[0].pos.x); scalar(list.VtxBuffer[0].pos.y);
		scalar(list.VtxBuffer[0].uv.x); scalar(list.VtxBuffer[0].uv.y); scalar(list.VtxBuffer[0].col);
		scalar(list.IdxBuffer[0]); scalar(cmd.ClipRect.x); scalar(cmd.ClipRect.y); scalar(cmd.ClipRect.z); scalar(cmd.ClipRect.w);
		scalar(cmd.TexRef._TexID); scalar(cmd.VtxOffset); scalar(cmd.IdxOffset); scalar(cmd.ElemCount);
		scalar(data.DisplayPos.x); scalar(data.DisplaySize.x); scalar(data.FramebufferScale.x);
		checks.Expect(!snapshot.CanSkip(&data,key,true),"explicit resource or layout transaction never skips submission");
		reject([&]{cmd.UserCallback=ImDrawCallback_ResetRenderState;},[&]{cmd.UserCallback=nullptr;});
		ImTextureData texture; texture.SetTexID(31); texture.SetStatus(ImTextureStatus_OK);
		ImVector<ImTextureData*> textures; textures.push_back(&texture); data.Textures=&textures;
		for (const auto status : {ImTextureStatus_WantCreate,ImTextureStatus_WantUpdates,ImTextureStatus_WantDestroy,ImTextureStatus_Destroyed})
		{
			texture.SetStatus(status); checks.Expect(!snapshot.CanSkip(&data,key),"every pending texture lifecycle action executes the real backend");
		}
		texture.SetStatus(ImTextureStatus_OK); cmd.TexRef=texture.GetTexRef();
		checks.Expect(snapshot.CanSkip(&data,key),"texture indirection compares the actual GetTexID rather than pointer storage");
		texture.SetTexID(ImTextureID_Invalid); checks.Expect(!snapshot.CanSkip(&data,key),"not-yet-uploaded indirect texture is rejected before GetTexID assertions");
		texture.SetTexID(31); cmd.TexRef=ImTextureRef(ImTextureID(31)); data.Textures=nullptr;
		for (int i=0;i<12;++i)
		{
			auto changed=key;
			switch(i)
			{
			case 0: ++changed.output.epoch; break; case 1: ++changed.output.fontSerial; break;
			case 2: ++changed.output.language; break; case 3: ++changed.output.backdrop; break;
			case 4: changed.clear[0]+=.1F; break; case 5: ++changed.cacheRevision; break;
			case 6: ++changed.target; break; case 7: ++changed.showSerial; break; case 8: ++changed.hideSerial; break;
			case 9: ++changed.themeSerial; break; case 10: changed.output.geometry[0]+=1; break; case 11: changed.output.material[0]=.5F; break;
			}
			checks.Expect(!snapshot.CanSkip(&data,changed),"target/session/material/font/cache identities invalidate a duplicate");
		}
		list.VtxBuffer[0].col=IM_COL32(1,2,3,255);
		checks.Expect(!snapshot.CanSkip(&data,key),"unsuccessful attempted output cannot replace last successful presentation");
		snapshot.Reset(); checks.Expect(!snapshot.CanSkip(&data,key),"failed or occluded submission requires the next identical frame to retry");
		snapshot.ConfirmPresented(&data,key); checks.Expect(snapshot.CanSkip(&data,key),"real successful retry publishes its own snapshot");
		list.VtxBuffer.resize(static_cast<int>(Design::FrameSubmissionCache::BudgetBytes/sizeof(ImDrawVert)+1));
		data.TotalVtxCount=list.VtxBuffer.Size; snapshot.ConfirmPresented(&data,key);
		checks.Expect(snapshot.StoredBytes()==0 && !snapshot.CanSkip(&data,key) && snapshot.Capacity()==capacity,
			"over-budget output submits normally without growing snapshot memory");
		Design::HomeMotionState motion; motion.entry=Design::HomeMotionState::Entry::Playing;
		checks.Expect(Design::RequiresHomeEntrySubmission(motion),"unprepared entry must count only actual submissions");
		motion.preparation.managed=motion.preparation.ready=motion.preparation.keyValid=motion.preparation.revealReady=true;
		motion.preparation.fullFrame=false; motion.preparation.key=key.output;
		motion.entryElapsed=Design::HomeEntryTiming::WritingEnd+.01F; motion.delta=.05F;
		const auto submitted=motion.preparation.submitted;
		for (int i=0;i<14;++i)
		{
			Design::BeginHomeEntryFrame(motion,key.output);
			checks.Expect(!Design::RequiresHomeEntrySubmission(motion),"ordinary cached hold and dock may reuse exact output");
		}
		checks.Expect(motion.entryElapsed>Design::HomeEntryTiming::DockStart && motion.preparation.submitted==submitted,
			"skipped hold frames advance the 600ms clock without inventing backend warm successes");
		motion.preparation.fullFrame=true; checks.Expect(Design::RequiresHomeEntrySubmission(motion),"capture and restore full layout frames always submit");
		std::cout<<"[FrameSubmission] snapshot-budget="<<Design::FrameSubmissionCache::BudgetBytes<<" exact-fields=17 invalidation-keys=12\n";
	}

	void CheckHomeEntryPreparation(Checks& checks)
	{
		using Timing=Design::HomeEntryTiming;
		checks.Expect(Near(Timing::Total,2.66F) && Near(Timing::Writing,1.06F) && Near(Timing::Hold,.6F),
			"one shared timing definition doubles complete entrance, writing and opaque hold");
		Design::HomeMotionState state; Design::HomeEntryFrameKey key;
		key.geometry={1000,1100,1,1,200,32,1000,1100,20,1}; key.epoch=1; key.fontSerial=1;
		state.home=true; state.showSerial=1; state.entry=Design::HomeMotionState::Entry::Playing;
		state.entryElapsed=.4F; state.entryLayoutValidated=true;
		const ImVec4 solidColor(.4F,.5F,.6F,1);
		checks.Expect(Design::CanOmitHomeEntryRootBackground(state,true,1,solidColor),
			"active home entry with opaque style and Solid clear permits only root background alpha override");
		checks.Expect(!Design::CanOmitHomeEntryRootBackground(state,false,1,solidColor)
			&& !Design::CanOmitHomeEntryRootBackground(state,true,.8F,solidColor)
			&& !Design::CanOmitHomeEntryRootBackground(state,true,1,{.4F,.5F,.6F,.8F})
			&& !Design::CanOmitHomeEntryRootBackground(state,true,1,{1.1F,.5F,.6F,1}),
			"transparent backdrop/style/background and non-normalized RGB cannot use redundant root suppression");
		auto inactive=state; Design::FinishHomeEntry(inactive);
		checks.Expect(!Design::CanOmitHomeEntryRootBackground(inactive,true,1,solidColor),"static or finished setting root retains legacy background");
		Design::ConfigureHomeMotion(state,true,1,.05F,true,5);
		checks.Expect(!Design::BeginHomeEntryFrame(state,key) && state.entryElapsed==.4F,
			"fastest animation rate cannot advance unprepared first frame");
		Design::CompleteHomeEntryFrame(state,true,true); Design::CompleteHomeEntryFrame(state,true,true);
		checks.Expect(state.preparation.warmFrames==1 && !state.preparation.ready,"duplicate completion cannot count as second successful warm frame");
		Design::BeginHomeEntryFrame(state,key); Design::CompleteHomeEntryFrame(state,true,true);
		checks.Expect(state.preparation.ready && state.entryElapsed==.4F,"exactly two distinct full submitted frames prepare entry");
		checks.Expect(Design::BeginHomeEntryFrame(state,key) && Near(state.entryElapsed,.65F),
			"ready opaque frame skips invisible background and advances clamped rate-scaled delta");
		Design::CompleteHomeEntryFrame(state,true,true);
		const auto ready=state;
		for (int field=0;field<27;++field)
		{
			state=ready; state.entryLayoutValidated=true; auto changed=key;
			if (field<10) changed.geometry[field]+=1;
			else if (field<14) changed.fonts[field-10]+=1;
			else switch (field)
			{
			case 14: ++changed.epoch; break; case 15: ++changed.fontSerial; break; case 16: ++changed.language; break;
			case 17: ++changed.texture; break; case 18: ++changed.atlasWidth; break; case 19: ++changed.atlasHeight; break;
			case 20: changed.dark=!changed.dark; break;
			case 26: ++changed.backdrop; break;
			default: changed.material[field-21]+=.1F; break;
			}
			checks.Expect(!Design::BeginHomeEntryFrame(state,changed) && !state.preparation.ready && !state.entryLayoutValidated
				&& state.entryElapsed==ready.entryElapsed,"every actual layout/font/atlas/epoch/theme/language key invalidates fast frame without replay");
		}
		state=ready; Design::BeginHomeEntryFrame(state,key); Design::CompleteHomeEntryFrame(state,false,false);
		checks.Expect(!state.preparation.ready && state.entryElapsed==ready.entryElapsed,
			"failed submission rolls back consumed delta and forces real resource preparation");
		for (int failure=0;failure<8 && state.EntryActive();++failure)
		{ Design::BeginHomeEntryFrame(state,key); Design::CompleteHomeEntryFrame(state,false,false); }
		checks.Expect(!state.EntryActive() && state.preparation.attempts<=6,"failed preparation is bounded and cannot permanently trap loading overlay");
		state=ready; state.entryElapsed=Timing::WritingEnd-.01F;
		for (int frame=0;frame<2;++frame)
		{
			checks.Expect(!Design::BeginHomeEntryFrame(state,key) && state.entryElapsed==Timing::WritingEnd,
				"rate 5 captures only after writing completes while the cover remains opaque");
			state.preparation.layoutReady=frame==1;
			Design::CompleteHomeEntryFrame(state,true,true,frame==1);
		}
		checks.Expect(state.preparation.revealReady && Design::BeginHomeEntryFrame(state,key),
			"successful no-logo capture freezes background throughout hold");
		state.entryElapsed=Timing::DockStart;
		checks.Expect(Design::BeginHomeEntryFrame(state,key) && state.entryElapsed>Timing::DockStart,
			"translucent docking freezes page and navigation CPU generation");
		state.entryElapsed=Timing::Total-.005F;
		for (int frame=0;frame<2;++frame)
		{
			checks.Expect(!Design::BeginHomeEntryFrame(state,key) && state.EntryActive(),
				"terminal child warm frames stay hidden behind cached page");
			state.preparation.layoutReady=true;
			Design::CompleteHomeEntryFrame(state,true,true);
		}
		checks.Expect(state.preparation.restoreReady && !Design::BeginHomeEntryFrame(state,key) && !state.EntryActive(),
			"after two full terminal submissions normal static frame resumes");
		// 即使字体/提交都成功，旧 native ContentSize 仍不能作为截图或恢复完成的依据。
		state=ready; state.entryElapsed=Timing::WritingEnd-.01F;
		for (int frame=0;frame<2;++frame)
		{ Design::BeginHomeEntryFrame(state,key); Design::CompleteHomeEntryFrame(state,true,true,true); }
		checks.Expect(!state.preparation.revealReady && !state.preparation.layoutReady && state.entryElapsed==Timing::WritingEnd,
			"two successful frames with unconsumed native sizes cannot publish a page snapshot");
		Design::BeginHomeEntryFrame(state,key); state.preparation.layoutReady=true; Design::CompleteHomeEntryFrame(state,true,true,true);
		checks.Expect(state.preparation.revealReady && state.preparation.revealFrames==3,
			"capture readiness follows the actual layout measurement instead of a larger fixed warm-frame constant");
		state.entryElapsed=Timing::Total-.005F;
		for (int frame=0;frame<2;++frame)
		{ Design::BeginHomeEntryFrame(state,key); Design::CompleteHomeEntryFrame(state,true,true); }
		checks.Expect(!state.preparation.restoreReady && state.EntryActive(),"unconsumed native restore sizes stay behind the cache after two successful submissions");
		Design::BeginHomeEntryFrame(state,key); state.preparation.layoutReady=true; Design::CompleteHomeEntryFrame(state,true,true);
		checks.Expect(state.preparation.restoreReady && state.preparation.restoreFrames==3,"restoration also waits for current native layout readiness");
		state.preparation.restoreReady=false; state.preparation.restoreFrames=0;
		for (int frame=0;frame<6 && state.EntryActive();++frame)
		{ Design::BeginHomeEntryFrame(state,key); Design::CompleteHomeEntryFrame(state,true,true); }
		checks.Expect(!state.EntryActive() && state.preparation.restoreFrames==6,"nonconverging native layout restoration terminates within its existing six-submission budget");
		state=ready; state.entryElapsed=Timing::WritingEnd-.01F;
		for (int frame=0;frame<6 && state.EntryActive();++frame)
		{ Design::BeginHomeEntryFrame(state,key); Design::CompleteHomeEntryFrame(state,true,true,true); }
		checks.Expect(!state.EntryActive() && state.preparation.revealFrames==6,"nonconverging native capture layout also terminates within the six-submission budget");
		state=ready; Design::ConfigureHomeMotion(state,true,2,.05F,true,1);
		checks.Expect(state.entry==Design::HomeMotionState::Entry::Pending && !state.preparation.ready && !state.preparation.managed,
			"new actual Show discards prior preparation without retaining GPU or ImGui resource pointers");
		state=ready; Design::ConfigureHomeMotion(state,false,1,.05F,true,1);
		checks.Expect(!state.EntryActive(),"navigation away terminates prepared overlay without late replay");
		std::cout<<"[HomeEntryPreparation] total="<<Timing::Total<<" ready-frames=2 invalidation-keys=27 bounded-state-bytes="
			<<sizeof(state.preparation)<<'\n';
	}

	void CheckHomeMotion(Checks& checks, ImFont* regular, ImFont* strong, ImFont* icons, float scale)
	{
		CheckHomeEntryPreparation(checks);
		Design::BindTextFonts(regular,strong); ImFluent::SetIconFont(icons); Design::ApplyPalette(false);
		auto& io=ImGui::GetIO(); io.ConfigInputTrickleEventQueue=false;
		Design::HomeMotionState motion;
		std::uint64_t serial=1; bool enabled=true, allowed=true, dark=false, popup=false;
		float delta=1.0F/60, rate=1, scroll=0, viewport=800;
		bool resetScroll=false;
		std::uint64_t hideSerial=0,consumedHideSerial=0;
		Design::NavigationState resetNavigation; Design::NavigationDestination resetDestination;
		const ImVec2 outside(-100,-100);
		struct Frame { std::vector<ImVec2> text; int vertices=0, indices=0; float scroll=0, maximum=0; ImRect clip; ImGuiWindow* window=nullptr; };
		auto frame=[&](ImVec2 mouse, ImGuiMouseSource source=ImGuiMouseSource_Mouse, bool down=false) {
			io.DisplaySize={1000*scale,1000*scale}; io.DeltaTime=1.0F/60;
			io.AddMouseSourceEvent(source); io.AddMousePosEvent(mouse.x,mouse.y); io.AddMouseButtonEvent(0,down);
			ImGui::NewFrame(); Design::ApplyPalette(dark); Design::ConfigureRowAnimations(0,false,1);
			ImGui::SetNextWindowPos({0,0}); ImGui::SetNextWindowSize(io.DisplaySize);
			ImGui::Begin("##home-motion-root",nullptr,ImGuiWindowFlags_NoTitleBar|ImGuiWindowFlags_NoResize
				|ImGuiWindowFlags_NoCollapse|ImGuiWindowFlags_NoSavedSettings|ImGuiWindowFlags_NoScrollbar|ImGuiWindowFlags_NoScrollWithMouse);
			const bool reset=Design::ConsumeSettingHomeReset(hideSerial,consumedHideSerial,resetDestination,resetNavigation,motion);
			resetScroll|=reset;
			Design::BeginScrollChild("##motion-scroll",{900*scale,viewport*scale},0,0,resetScroll);
			if (!resetScroll) ImGui::SetScrollY(scroll);
			resetScroll=false;
			Design::ConfigureHomeMotion(motion,true,serial,delta,enabled,rate,allowed);
			const bool entryBlocked=Design::CancelHomeEntryOnInput(motion);
			const auto actualClip=ImGui::GetCurrentWindow()->ClipRect;
			Design::ConfigureHomeEntryViewport(motion,{0,32*scale,io.DisplaySize.x,io.DisplaySize.y},
				{actualClip.Min.x,actualClip.Min.y,actualClip.Max.x,actualClip.Max.y});
			ImGui::PushStyleVar(ImGuiStyleVar_DisabledAlpha,1); ImGui::BeginDisabled(entryBlocked);
			checks.Expect(Design::RenderHome({0,1,"test",0,0,1},&motion)==Design::HomeAction::None,
				"home animation never dispatches a business action");
			ImGui::EndDisabled(); ImGui::PopStyleVar();
			Frame result; const auto* window=ImGui::GetCurrentWindow(); result.window=ImGui::GetCurrentWindow();
			result.scroll=window->Scroll.y; result.maximum=window->ScrollMax.y; result.clip=window->ClipRect;
			const auto& draw=*ImGui::GetWindowDrawList();
			result.vertices=draw.VtxBuffer.Size; result.indices=draw.IdxBuffer.Size;
			const auto white=io.Fonts->TexUvWhitePixel;
			for (const auto& v : draw.VtxBuffer) if (v.uv.x!=white.x || v.uv.y!=white.y) result.text.push_back(v.pos);
			ImGui::EndChild();
			if (popup) ImGui::OpenPopup("##motion-gate");
			if (ImGui::BeginPopup("##motion-gate")) { ImGui::TextUnformatted("gate"); if (!popup) ImGui::CloseCurrentPopup(); ImGui::EndPopup(); }
			Design::RenderHomeEntryOverlay(motion);
			ImGui::End(); ImGui::Render(); CheckDrawData(checks); return result;
		};
		auto idle=[&](int count=180) { Frame result; for (int i=0;i<count;++i) result=frame(outside); return result; };
		idle();
		checks.Expect(motion.entries==1 && !motion.EntryActive() && motion.x==0 && motion.y==0,
			"first home entry plays once then reaches a stable neutral pose");
		const auto allocations=motion.cache.allocations, rebuilt=motion.cache.rebuilds;
		const Frame resting=idle(10);
		checks.Expect(motion.cache.rebuilds==rebuilt && motion.cache.allocations==allocations,
			"settled real homepage reuses transformed geometry with no hot-path cache allocation");
		const ImVec2 center(motion.brandOrigin.x+motion.brandWidth*.5F,motion.brandOrigin.y+Design::HomeBrand::Height(motion.brandWidth)*.5F);
		for (const ImVec2 corner : {ImVec2(-1,-1),ImVec2(1,-1),ImVec2(-1,1),ImVec2(1,1)})
		{
			const ImVec2 point(center.x+corner.x*motion.brandWidth*.49F,center.y+corner.y*Design::HomeBrand::Height(motion.brandWidth)*.49F);
			Frame moving; for (int i=0;i<100;++i) moving=frame(point);
			checks.Expect(motion.x*corner.x>.9F && motion.y*corner.y>.9F && std::abs(motion.x)<=1 && std::abs(motion.y)<=1,
				"real mouse corners produce bounded perspective targets without feedback from transformed bounds");
			checks.Expect(moving.vertices==resting.vertices && moving.indices==resting.indices && moving.text.size()==resting.text.size(),
				"perspective keeps real page tessellation and text submission counts unchanged");
			bool same=moving.text.size()==resting.text.size();
			for (size_t i=0;same && i<moving.text.size();++i) same=Near(moving.text[i].x,resting.text[i].x,.1F)&&Near(moving.text[i].y,resting.text[i].y,.1F);
			checks.Expect(same,"actual slogan, copyright, button labels and hit-layout remain still while only the transparent brand moves");
			const auto& mesh=Design::HomeBrand::Geometry(); bool bounds=true,alpha=true;
			for (size_t i=0;i<mesh.vertices.size();++i)
			{
				const auto& v=motion.cache.vertices[i];
				bounds &= v.pos.x>=-Design::Pixels(14) && v.pos.x<=motion.brandWidth+Design::Pixels(14)
					&& v.pos.y>=-Design::Pixels(14) && v.pos.y<=Design::HomeBrand::Height(motion.brandWidth)+Design::Pixels(14);
				alpha &= (v.color&IM_COL32_A_MASK)==(mesh.vertices[i].light&IM_COL32_A_MASK);
			}
			checks.Expect(bounds && alpha,"all transformed descender and antialiasing vertices fit reserved envelope and retain transparent alpha");
		}
		const ImVec2 edge(center.x+motion.brandWidth*.45F,center.y);
		for (const auto source : {ImGuiMouseSource_TouchScreen,ImGuiMouseSource_Pen})
		{
			// 每个来源先建立真实偏转；来源附着在位置事件上，需跨像素移动以避免 ImGui 去重。
			frame({edge.x-Design::Pixels(2),edge.y});
			for (int i=0;i<100;++i) frame(edge);
			checks.Expect(io.MouseSource==ImGuiMouseSource_Mouse && motion.x>.85F,
				"each non-mouse gate starts from an actual mouse-source event and mouse-driven tilt");
			const ImVec2 nonMouseEdge(edge.x+Design::Pixels(2),edge.y);
			for (int i=0;i<100;++i) frame(nonMouseEdge,source);
			checks.Expect(io.MouseSource==source,"actual displaced input event switches to the tested pen or touch source");
			checks.Expect(motion.x==0 && motion.y==0,"touch and pen input return the wordmark to rest despite a nonzero pointer target");
		}
		for (int i=0;i<60;++i) frame(edge);
		allowed=false; idle(); checks.Expect(motion.x==0 && motion.y==0,"navigation overlay and inactive HWND gate return motion to rest"); allowed=true;
		for (int i=0;i<60;++i) frame(edge);
		for (int i=0;i<100;++i) frame(edge,ImGuiMouseSource_Mouse,true);
		frame(outside); checks.Expect(motion.x==0 && motion.y==0,"held input cancels pointer motion without moving page controls");
		for (int i=0;i<60;++i) frame(edge);
		popup=true; for (int i=0;i<100;++i) frame(edge);
		checks.Expect(motion.x==0 && motion.y==0,"real popup blocks perspective even with mouse in brand bounds"); popup=false; idle(3);
		idle();
		for (size_t button=0;button<2;++button)
		{
			const auto bounds=motion.buttonBounds[button];
			const ImVec2 point((bounds.x+bounds.z)*.5F,(bounds.y+bounds.w)*.5F);
			frame(point); frame(point);
			checks.Expect(motion.buttons[button].hovered && motion.buttons[button].elapsed>0 && motion.buttons[button].elapsed<.32F,
				"real open-source button hover starts a short icon feedback once");
			for (int i=0;i<60;++i) frame(point);
			const auto stableBounds=motion.buttonBounds[button];
			checks.Expect(motion.buttons[button].elapsed==.32F && bounds.x==stableBounds.x && bounds.y==stableBounds.y
				&& bounds.z==stableBounds.z && bounds.w==stableBounds.w,"held hover stops its icon pulse without moving the real Fluent hit rectangle");
			idle(3);
		}
		const auto entries=motion.entries;
		dark=true; idle(3); dark=false; idle(3);
		checks.Expect(motion.entries==entries && !motion.EntryActive(),"theme changes and repeated current-page frames do not replay entry");
		Design::ConfigureHomeMotion(motion,false,serial,delta,true,rate);
		checks.Expect(!motion.home && !motion.EntryActive(),"leaving home cancels entrance immediately");
		delta=0; frame(outside);
		checks.Expect(motion.entries==entries && !motion.EntryActive(),"internal navigation back to home does not replay");
		++serial; frame(outside);
		checks.Expect(motion.entries==entries+1 && motion.entryElapsed==0 && motion.EntryActive(),"hidden-to-visible serial starts a fresh entry with rebased time");
		const auto start=Design::ResolveHomeEntryPose(motion);
		for (float time : {1.30F,1.60F,1.898F})
		{
			motion.entryElapsed=time; frame(outside); const auto pose=Design::ResolveHomeEntryPose(motion);
			checks.Expect(pose.writing==1 && pose.opacity==1 && pose.origin.x==start.origin.x && pose.origin.y==start.origin.y
				&& pose.width==start.width,"all six strokes complete before exact 600ms opaque centre hold");
		}
		motion.entryElapsed=2.16F; frame(outside); const auto curve=Design::ResolveHomeEntryPose(motion);
		const ImVec2 a(start.origin.x+start.width*.5F,start.origin.y+Design::HomeBrand::Height(start.width)*.5F);
		const ImVec2 b(motion.brandOrigin.x+motion.brandWidth*.5F,motion.brandOrigin.y+Design::HomeBrand::Height(motion.brandWidth)*.5F);
		const ImVec2 c(curve.origin.x+curve.width*.5F,curve.origin.y+Design::HomeBrand::Height(curve.width)*.5F);
		const float deviation=std::abs((b.x-a.x)*(c.y-a.y)-(b.y-a.y)*(c.x-a.x))/std::hypot(b.x-a.x,b.y-a.y);
		checks.Expect(curve.opacity>0 && curve.opacity<1 && deviation>scale,"dock fades and follows a real spatial curve away from straight interpolation");
		motion.entryElapsed=.50F;
		delta=.02F; rate=1; frame(outside);
		const auto openingRebuilds=motion.cache.rebuilds; const auto openingBounds=motion.buttonBounds;
		for (int i=0;i<8;++i) frame(outside);
		checks.Expect(motion.cache.rebuilds==openingRebuilds,"writing and overlay do not recompute the original logo mesh");
		for (size_t i=0;i<openingBounds.size();++i)
			checks.Expect(std::memcmp(&openingBounds[i],&motion.buttonBounds[i],sizeof(ImVec4))==0,"entrance preserves actual home button hit regions");
		const float beforeSpeedChange=motion.entryElapsed;
		rate=2; frame(outside); checks.Expect(Near(motion.entryElapsed-beforeSpeedChange,.04F),"speed changes advance from current entry progress");
		delta=0; frame(outside); const float paused=motion.entryElapsed; frame(outside);
		checks.Expect(motion.entryElapsed==paused,"rebased resume frames do not accumulate hidden or skipped wall time");
		enabled=false; frame(outside); enabled=true; frame(outside);
		checks.Expect(!motion.EntryActive(),"disable finishes current show permanently, re-enable does not replay");
		delta=.02F; scroll=1200; frame(outside); frame(outside);
		const auto originalViewport=frame(outside);
		std::cout<<"[HomeMotionCull] scale="<<scale<<" viewport="<<viewport*scale
			<<" scroll="<<originalViewport.scroll<<" max="<<originalViewport.maximum
			<<" brandBottom="<<motion.brandOrigin.y+Design::HomeBrand::Height(motion.brandWidth)+Design::Pixels(14)
			<<" clipTop="<<originalViewport.clip.Min.y<<'\n';
		// 横幅重排后旧 800 DIP 视口可能无法把字标完全滚走；收窄视口建立真实离屏前置条件。
		viewport=300; frame(outside); frame(outside); const auto clipped=frame(outside);
		checks.Expect(clipped.scroll>0 && clipped.scroll<=clipped.maximum
			&& motion.brandOrigin.y+Design::HomeBrand::Height(motion.brandWidth)+Design::Pixels(14)<clipped.clip.Min.y,
			"ancestor scroll actually moves the full brand envelope above the smaller viewport");
		std::cout<<"[HomeMotionCull] scale="<<scale<<" viewport="<<viewport*scale
			<<" scroll="<<clipped.scroll<<" max="<<clipped.maximum
			<<" brandBottom="<<motion.brandOrigin.y+Design::HomeBrand::Height(motion.brandWidth)+Design::Pixels(14)
			<<" clipTop="<<clipped.clip.Min.y<<'\n';
		const auto offscreenRebuilds=motion.cache.rebuilds;
		const auto hidden=idle(5);
		checks.Expect(!motion.brandVisible && motion.cache.rebuilds==offscreenRebuilds,
			"real ancestor scrolling culls wordmark submission and pauses logo geometry updates");
		// 滚动只改变可见性，不产生新的显示事件。
		scroll=clipped.scroll+motion.heroOrigin.y+motion.heroSize.y-clipped.clip.Min.y-100*scale;
		frame(outside); frame(outside); const auto logoClippedBuilds=motion.cache.rebuilds;
		const auto footerOnly=frame(outside);
		checks.Expect(!motion.brandVisible && motion.heroVisible && !motion.EntryActive()
			&& motion.cache.rebuilds==logoClippedBuilds,"visible hero footer leaves completed entrance and culled ink stable");
		checks.Expect(footerOnly.text.size()>0,"partially visible hero retains real foreground submission");
		scroll=10000; frame(outside); frame(outside);
		checks.Expect(!motion.heroVisible && !motion.brandVisible && !motion.EntryActive(),
			"fully offscreen hero never pretends to enter the page again");
		viewport=800; scroll=0; frame(outside); frame(outside);
		checks.Expect(motion.brandVisible && motion.entries==entries+1 && hidden.vertices<resting.vertices,
			"scrolling back restores cached brand without a new home entry");
		enabled=false; frame(edge);
		checks.Expect(motion.x==0 && motion.y==0 && !motion.EntryActive(),"animation off snaps pose and entrance in the same production frame");
		enabled=true; idle();
		checks.Expect(motion.cache.allocations==allocations,"all warmed parallax, reflection and culling paths reuse allocated vertex storage");
		viewport=220; ++serial; delta=0; frame(outside);
		checks.Expect(motion.entry==Design::HomeMotionState::Entry::Skipped,"true Show with entire measured hero outside viewport skips all entry");
		viewport=800; frame(outside);
		checks.Expect(!motion.EntryActive(),"enlarging a skipped Show never starts a late entrance");
		++serial; frame(outside); checks.Expect(motion.EntryActive(),"next true Show may play when the measured hero fits");
		viewport=220; frame(outside); viewport=800; frame(outside);
		checks.Expect(!motion.EntryActive(),"shrinking active entry cancels permanently for the current Show");
		scroll=400; frame(outside); frame(outside);
		resetDestination={Design::PageId::General,Design::GeneralSection::Interaction};
		resetNavigation.overlayOpen=resetNavigation.flyoutOpen=true; resetNavigation.contentReveal.Reset(.2F);
		resetNavigation.childFocusId=123; resetNavigation.childWindowItemId=456; resetNavigation.inlineChildFocused=true;
		++hideSerial; scroll=0; ++serial;
		const auto resetFrame=frame(outside);
		checks.Expect(resetDestination.page==Design::PageId::Home && !resetNavigation.overlayOpen && !resetNavigation.flyoutOpen
			&& resetNavigation.contentReveal.value==1 && !resetNavigation.childFocusId && !resetNavigation.childWindowItemId
			&& !resetNavigation.inlineChildFocused && resetFrame.scroll==0 && motion.EntryActive(),
			"production Hide reset and pre-Begin scroll helper return Home to top on first reopened layout");
		const auto resetEntries=motion.entries; frame(outside);
		checks.Expect(motion.entries==resetEntries && consumedHideSerial==hideSerial,"consumed Hide reset and repeated Show do not restart entry");
		const auto star=motion.buttonBounds[0]; const ImVec2 button((star.x+star.z)*.5F,(star.y+star.w)*.5F);
		frame(button,ImGuiMouseSource_Mouse,true); frame(button,ImGuiMouseSource_Mouse,false);
		checks.Expect(!motion.EntryActive(),"actual mouse press cancels entrance and its release cannot activate the newly revealed Star");
		++serial; frame(outside); io.AddKeyEvent(ImGuiKey_Space,true); frame(outside);
		io.AddKeyEvent(ImGuiKey_Space,false); frame(outside);
		checks.Expect(!motion.EntryActive(),"actual key event cancels entrance without a synthetic navigation/activation");
		for (ImGuiKey key : {ImGuiKey_Tab,ImGuiKey_DownArrow,ImGuiKey_Space})
		{
			io.AddKeyEvent(key,true); frame(outside); // 隐藏前已按住，下一 Show 帧没有新的 pressed 边沿。
			checks.Expect(ImGui::IsKeyDown(key) && ImGui::IsKeyPressed(key,false),"held-key fixture begins with a real key edge");
			++serial; frame(outside);
			checks.Expect(ImGui::IsKeyDown(key) && !ImGui::IsKeyPressed(key,false) && !motion.EntryActive(),
				"key already held before Show cancels entrance before repeated navigation can pass through");
			io.AddKeyEvent(key,false); frame(outside);
			checks.Expect(!motion.EntryActive(),"releasing a held cancellation key cannot replay or activate revealed controls");
		}
		const auto previousFlags=io.ConfigFlags; io.ConfigFlags|=ImGuiConfigFlags_NavEnableKeyboard;
		for (bool withPopup : {false,true})
		{
			enabled=false; const auto before=frame(outside);
			const ImVec2 blank(before.clip.Min.x+1,before.clip.Min.y+1);
			frame(blank,ImGuiMouseSource_Mouse,true); frame(blank,ImGuiMouseSource_Mouse,false);
			popup=withPopup; frame(outside); frame(outside);
			if (withPopup) checks.Expect(!GImGui->OpenPopupStack.empty() && GImGui->NavWindow
				&& (GImGui->NavWindow->Flags & ImGuiWindowFlags_Popup),"keyboard reopen fixture starts with a real focused popup");
			++hideSerial; ++serial; popup=false;
			const auto reopened=frame(outside); const auto* root=reopened.window->RootWindow;
			checks.Expect(!root->Appearing && GImGui->NavWindow && GImGui->NavWindow->RootWindow==root
				&& !(GImGui->NavWindow->Flags & ImGuiWindowFlags_Popup) && GImGui->OpenPopupStack.empty(),
				"fast Hide/Show rebinds the existing setting root rather than losing keyboard focus or keeping a closed popup");
			io.AddKeyEvent(ImGuiKey_Tab,true); frame(outside); io.AddKeyEvent(ImGuiKey_Tab,false); frame(outside);
			checks.Expect(GImGui->NavWindow==root && GImGui->NavId==reopened.window->ChildId,
				"first Tab after fast reopen focuses the actual Home content host without a mouse or Ctrl+Tab");
			// 沿用原生非扁平 child 的 Enter 进入规则，再以真实 Tab 到达页面控件。
			io.AddKeyEvent(ImGuiKey_Enter,true); frame(outside); io.AddKeyEvent(ImGuiKey_Enter,false); frame(outside);
			const auto starId=reopened.window->GetID("##home-star"); bool reached=GImGui->NavId==starId && GImGui->NavWindow==reopened.window;
			for (int step=0;step<16 && !reached;++step)
			{
				io.AddKeyEvent(ImGuiKey_Tab,true); frame(outside);
				io.AddKeyEvent(ImGuiKey_Tab,false); frame(outside);
				reached=GImGui->NavId==starId && GImGui->NavWindow==reopened.window;
			}
			checks.Expect(reached,"real Tab after stable fast reopen reaches the actual Home Star control without mouse or Ctrl+Tab recovery");
		}
		io.ConfigFlags=previousFlags;
		Design::ApplyPalette(false); io.AddMouseSourceEvent(ImGuiMouseSource_Mouse);
		std::cout<<"[HomeMotion] scale="<<scale<<" entries="<<motion.entries<<" cacheBuilds="<<motion.cache.rebuilds<<" allocations="<<motion.cache.allocations<<'\n';
	}

	void CheckEdidCard(Checks& checks, ImFont* regular, ImFont* strong, float scale)
	{
		Design::BindTextFonts(regular, strong);
		auto& io = ImGui::GetIO();
		const std::string single = "Topology: Single | Monitor count: 1\n\nMonitor 1 (Primary): Screen | Windows DISPLAY1 | 2880x1920 px | DPI 144x144 | Physical 28.000000x18.000000 cm | EDID Valid, raw 28x18 cm";
		std::string multiple = "Topology: Extended | Monitor count: 8";
		for (int index = 1; index <= 8; ++index)
			multiple += "\n\nMonitor " + std::to_string(index) + ": Extremely long monitor friendly name with repeated model identifiers ABCDEFGHIJKLMNOPQRSTUVWXYZ | Windows DISPLAY" + std::to_string(index)
				+ " | 3840x2160 px | DPI 144x144 | Physical unavailable (EDID Read failed) | EDID Read failed, raw 0x0 cm";
		int frames = 0, outlineBoundarySamples = 0;
		for (bool dark : { false, true })
		{
			Design::ApplyPalette(dark);
			std::array<float, 5> heights{};
			for (int sample = 0; sample < 5; ++sample)
			{
				const char* text = sample == 0 ? "显示快照不可用" : sample == 1 || sample == 4 ? single.c_str() : multiple.c_str();
				const float width = (sample == 3 ? 230.0F : 620.0F) * scale;
				float pageScroll = 0;
				for (int pass = 0; pass < 4; ++pass)
				{
					io.DisplaySize = ImVec2(1440 * scale, 1000 * scale);
					io.AddMousePosEvent(-100, -100); io.AddMouseButtonEvent(0, false);
					ImGui::NewFrame();
					ImGui::SetNextWindowPos({ 0, 0 }); ImGui::SetNextWindowSize({ width + 80 * scale, 400 * scale });
					ImGui::Begin("##edid-card-cpu", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoSavedSettings);
					ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, { 0, 0 });
					ImGui::SetNextWindowScroll({ 0, pass == 3 ? pageScroll : 0 });
					ImGui::BeginChild("##edid-page", { width, 300 * scale });
					auto* page = ImGui::GetCurrentWindow();
					ImGui::BeginChild("##edid-page-content", { ImGui::GetContentRegionAvail().x, 0 }, ImGuiChildFlags_AutoResizeY);
					auto* body = ImGui::GetCurrentWindow();
					Design::RenderEdidCard(text);
					ImGuiWindow* card = body->DC.ChildWindows.empty() ? nullptr : body->DC.ChildWindows.back();
					checks.Expect(card && card->Size.y > 0 && Near(card->Scroll.y, 0) && Near(card->ScrollMax.y, 0)
						&& !card->ScrollbarY && !card->ScrollbarX,
						"actual EDID card has complete height with no internal scrollbar on its first frame and every width/text change");
					if (card)
					{
						heights[sample] = card->Size.y;
						checks.Expect(card->DC.CursorMaxPos.y <= card->Pos.y + card->Size.y - card->WindowPadding.y + 1,
							"real EDID text layout including the final line stays inside the full measured card");
						const auto white = io.Fonts->TexUvWhitePixel;
						int glyphs = 0; float glyphBottom = -FLT_MAX;
						for (const auto& vertex : card->DrawList->VtxBuffer)
						{
							// 卡片边框也会使用 atlas 中的线纹理，只取实际正文颜色的字形。
							if (vertex.col != Design::TextPrimary || (vertex.uv.x == white.x && vertex.uv.y == white.y))
							{
								if (vertex.pos.y < card->Pos.y || vertex.pos.y > card->Pos.y + card->Size.y + 1) ++outlineBoundarySamples;
								continue;
							}
							++glyphs; glyphBottom = (std::max)(glyphBottom, vertex.pos.y);
							checks.Expect(vertex.pos.x >= card->Pos.x && vertex.pos.x <= card->Pos.x + card->Size.x + 1
								&& vertex.pos.y >= card->Pos.y && vertex.pos.y <= card->Pos.y + card->Size.y + 1,
								"actual EDID glyph vertices remain bounded by their themed card");
						}
						if (pass == 3) checks.Expect(glyphs > 0 && glyphBottom > card->Pos.y + card->Size.y - card->WindowPadding.y - 24 * scale,
							"scrolling the outer page exposes the final actual EDID glyphs without scrolling the card");
					}
					ImGui::EndChild();
					if (pass == 2) pageScroll = page->ScrollMax.y;
					if (pass >= 2) checks.Expect(Near(body->ScrollMax.y, 0) && Near(body->Scroll.y, 0)
						&& ((sample == 2 || sample == 3) ? page->ScrollMax.y > 0 : Near(page->ScrollMax.y, 0)),
						"long EDID content scrolls at the outer page while the auto-height page body remains fully expanded");
					ImGui::EndChild(); ImGui::PopStyleVar(); ImGui::End(); ImGui::Render(); ++frames;
					checks.Expect(ImGui::GetDrawData()->Valid && ImGui::GetDrawData()->TotalVtxCount > 0, "EDID regression submits real DrawData");
				}
			}
			checks.Expect(heights[1] > heights[0] && heights[2] > heights[1] && heights[3] > heights[2]
				&& Near(heights[4], heights[1], 1), "actual card grows for multiple monitors and narrow wrapping and shrinks immediately with shorter text");
		}
		std::cout << "[EdidCardDrawData] scale=" << scale << " frames=" << frames << " outlineBoundarySamples=" << outlineBoundarySamples << '\n';
		Design::ApplyPalette(false);
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
				const auto regions = Design::ResolveNavigationPaneGeometry(shell.pane.Width(), shell.pane.Height(), 36);
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
					for (int i = 0; i < 6; ++i) {
						ImGui::PushID(i); const int first = ImGui::GetWindowDrawList()->VtxBuffer.Size;
						ImFluent::NavItemEx("item", "Middle page", i == 5, nullptr, !expanded);
						if (i == 5) lastVertices = ImGui::GetWindowDrawList()->VtxBuffer.Size - first;
						ImGui::PopID();
					}
					if (pass == 1) maximumScroll = ImGui::GetScrollMaxY();
					if (pass == 2) checks.Expect(lastVertices > 0 && Near(ImGui::GetScrollY(), maximumScroll, 1.0F)
						&& (size.y != 520 || maximumScroll > 0), "actual middle child scroll reaches the sixth root entry at minimum size"); end();
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
	CheckFrameSubmission(checks);
	const auto fontDirectory = FindFontDirectory();
	if (fontDirectory.empty())
	{
		checks.Expect(false, "cannot locate Inkeys/src/ttf from test source or working directory");
		return checks.failures;
	}
	checks.Expect(LoadNavigationLabels(fontDirectory), "actual navigation loads bundled Chinese label fixtures");
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
	for (const float scale : { 1.0F, 1.25F, 1.5F, 2.0F, 1.875F, 3.0F, 4.0F })
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
		// 真正 DX11 backend 必须在 CPU 测试写入模拟 TexID 之前创建和销毁纹理。
		if (scale==1.0F) checks.failures+=RunSettingHomeWarpProbe(fonts[0],fonts[1],fonts.back());
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
		CheckMultilineRowAnimations(checks, fonts, fontDirectory, scale);
		CheckNavigationScrolling(checks, fonts[0], fonts.back(), scale);
		CheckEdidCard(checks, fonts[0], fonts[1], scale);
		CheckHomeBrandDesign(checks, fonts, fontDirectory, scale);
		if (scale==1.0F || scale==1.875F) CheckHomeMotion(checks,fonts[0],fonts[1],fonts.back(),scale);
		if (scale == 1.0F || scale == 1.5F || scale == 1.875F)
			CheckGeneralSections(checks, fonts[0], fonts[1], fonts.back(), scale);
		if (scale == 1.0F || scale == 1.5F || scale == 1.875F)
		{
			CheckHierarchicalNavigation(checks, fonts[0], fonts[1], fonts.back(), scale);
			CheckDragScrolling(checks, fonts[0], fonts[1], scale);
			CheckHomeScrollFollowup(checks, fonts[0], fonts[1], scale);
		}
		if (scale == 1.0F || scale == 1.875F) CheckAnimatedColors(checks, fonts[0], scale);
	}
	return checks.failures;
}
