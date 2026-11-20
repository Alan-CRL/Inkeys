#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "Setting.Controls.h"
#include "../../../IdtI18n.h"
#include "../../../IdtI18nKeys.g.h"
#include "imgui/imgui_internal.h"
#include "imgui/imgui_impl_dx11.h"
#include <d2d1_1helper.h>
#include <d3d11.h>
#include <dwrite_1.h>
#include <wrl/client.h>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <memory>
#include <optional>
#include <string_view>
#include <variant>
#include <vector>

import Inkeys.Other.Config;
import Inkeys.UI.Bar;
import Inkeys.UI.Bar.Metrics;
import Inkeys.UI.RenderPipeline;
import Inkeys.UI.UI3;
#include "Setting.ComponentEditor.h"

namespace Inkeys::UI::Setting::ComponentEditor
{
	using namespace Bar;
	using namespace UI3;
	using Microsoft::WRL::ComPtr;
	namespace Pipeline = Inkeys::UI::RenderPipeline;
	namespace
	{
		struct Texture
		{
			ComPtr<ID2D1Bitmap1> bitmap;
			ComPtr<ID3D11ShaderResourceView> view;
			UINT width = 0, height = 0;
			bool Ensure(const Pipeline::DeviceEpoch &epoch, ID2D1DeviceContext *dc, UINT w, UINT h)
			{
				if (view && width == w && height == h) return true;
				D3D11_TEXTURE2D_DESC desc{};
				desc.Width = w;
				desc.Height = h;
				desc.MipLevels = 1;
				desc.ArraySize = 1;
				desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
				desc.SampleDesc.Count = 1;
				desc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
				ComPtr<ID3D11Texture2D> texture;
				ComPtr<IDXGISurface> surface;
				Texture next;
				auto properties = D2D1::BitmapProperties1(
					D2D1_BITMAP_OPTIONS_TARGET, D2D1::PixelFormat(desc.Format, D2D1_ALPHA_MODE_PREMULTIPLIED));
				if (FAILED(epoch.d3dDevice->CreateTexture2D(&desc, nullptr, &texture)) ||
					FAILED(texture.As(&surface)) ||
					FAILED(epoch.d3dDevice->CreateShaderResourceView(texture.Get(), nullptr, &next.view)) ||
					FAILED(dc->CreateBitmapFromDxgiSurface(surface.Get(), &properties, &next.bitmap)))
					return false;
				next.width = w;
				next.height = h;
				*this = std::move(next);
				return true;
			}
			void Image(ImDrawList *list, ImVec2 offset, D2D1_RECT_F clip) const
			{
				list->AddCallback(ImGui_ImplDX11_PremultipliedBegin, nullptr);
				list->AddImage((ImTextureID)(uintptr_t)view.Get(), {offset.x + clip.left, offset.y + clip.top},
							   {offset.x + clip.right, offset.y + clip.bottom}, {clip.left / width, clip.top / height},
							   {clip.right / width, clip.bottom / height});
				list->AddCallback(ImGui_ImplDX11_PremultipliedEnd, nullptr);
			}
		};

		struct Editor
		{
			BarContentInstance instance;
			FrameTarget target{instance.Renderer(), FrameTargetKind::ExternalContext};
			Texture preview, overlay;
			ComPtr<ID2D1DeviceContext> dc;
			uint64_t generation = 0, revision = 0, pointerSerial = 0;
			std::optional<BarLayoutModel> model;
			PreviewFrame visible;
			std::optional<D2D1_POINT_2F> drawnPointer;
			DragSession drag;
			std::variant<Position, std::string> source;
			PreviewHit pressed;
			int lastFrame = -1;
			ImGuiID capture = 0;
			bool expanded = false, dirty = true;
			double moreSince = -1, fixedUntil = 0;
			ImVec2 lastMouse{};
			std::optional<ImVec2> cursorStart, cursorEnd;
			float trashAmount = 0, trashHover = 0;
			bool discardPending = false;
			float scrollPosition = -1;
			int releasedFrames = 0;
			struct Style
			{
				double width, height, targetHeight;
				BarSizeMode sizeMode;
				int order;
				bool edgeLight, dynamicLight, fractionalFont;
				uint64_t language;
				double theme, contentOpacity, backgroundOpacity;
				bool animation;
				double rate;
				bool operator==(const Style &) const = default;
			};
			std::optional<Style> style;
			~Editor()
			{
				target.BindRenderer(nullptr);
				target.Discard();
			}
			void Cancel()
			{
				drag.Cancel();
				instance.CancelLift();
				if (capture && ImGui::GetActiveID() == capture) ImGui::ClearActiveID();
				cursorStart.reset();
				cursorEnd.reset();
				moreSince = -1;
				trashAmount = trashHover = 0;
				releasedFrames = 0;
				scrollPosition = -1;
				dirty = true;
			}
			bool Device(const Pipeline::DeviceEpoch &epoch)
			{
				if (discardPending)
				{
					discardPending = false;
					dc.Reset();
				}
				if (dc && generation == epoch.generation) return true;
				if (dc)
				{
					Cancel();
					instance.Suspend();
				}
				target.Discard();
				dc.Reset();
				preview = {};
				overlay = {};
				visible = {};
				dirty = true;
				generation = epoch.generation;
				return epoch.d2dDevice && epoch.d3dDevice &&
					   SUCCEEDED(epoch.d2dDevice->CreateDeviceContext(D2D1_DEVICE_CONTEXT_OPTIONS_NONE, &dc));
			}
			template <class Paint>
			HRESULT Render(Texture &texture, const Pipeline::DeviceEpoch &epoch, UINT w, UINT h, Paint paint)
			{
				if (!texture.Ensure(epoch, dc.Get(), w, h)) return E_FAIL;
				if (const HRESULT attached = target.AttachExternalContext(dc.Get(), generation, w, h); FAILED(attached))
					return attached;
				dc->SetTarget(texture.bitmap.Get());
				dc->SetDpi(96, 96);
				dc->BeginDraw();
				dc->Clear(D2D1::ColorF(0, 0.f));
				target.BeginFrame(dc.Get(), D2D1::RectF(0, 0, float(w), float(h)));
				const HRESULT commands = paint();
				target.FinishDrawCommands(dc.Get());
				target.EndFrame(dc.Get());
				const HRESULT hr = dc->EndDraw();
				target.NotifyExternalEndDraw(hr);
				dc->SetTarget(nullptr);
				// D2D 要求目标失效后丢弃本地资源；但当帧 ImGui 绘制列表可能已引用预览 SRV，
				// 只做标记，由下一帧开头（尚未提交图像时）统一丢弃，Device() 随后按同一设备代次重建。
				if (hr == D2DERR_RECREATE_TARGET) discardPending = true;
				return FAILED(commands) ? commands : hr;
			}
		};
		std::unique_ptr<Editor> editor;
		Editor &Session()
		{
			if (editor) return *editor;
			editor = std::make_unique<Editor>();
			// 页外无 Draw 调用，通过 ImGui 帧末和会话销毁钩子释放捕获与设备资源。
			ImGuiContextHook hook;
			hook.Type = ImGuiContextHookType_EndFramePost;
			hook.Callback = [](ImGuiContext *ctx, ImGuiContextHook *) {
				if (editor && editor->lastFrame == ctx->FrameCount - 1)
				{
					editor->Cancel();
					editor->instance.Suspend();
					editor->fixedUntil = 0;
				}
			};
			ImGui::AddContextHook(ImGui::GetCurrentContext(), &hook);
			hook.Type = ImGuiContextHookType_Shutdown;
			hook.Callback = [](ImGuiContext *, ImGuiContextHook *) { editor.reset(); };
			ImGui::AddContextHook(ImGui::GetCurrentContext(), &hook);
			return *editor;
		}
		void Wake()
		{
			Pipeline::Request(Pipeline::Client::Settings);
		}
		D2D1_POINT_2F Pt(ImVec2 p)
		{
			return {p.x, p.y};
		}
		ImVec2 Offset(ImVec2 origin, const PreviewFrame &f)
		{
			// 实例本体在整张纹理中居中；浮窗在一侧伸出时保持本体与指针映射稳定。
			return {origin.x, origin.y - f.bounds.top};
		}
	}
	bool DragActive()
	{
		return editor && editor->drag.Phase() == DragPhase::Dragging;
	}
	void BeginLibraryDrag(const char *id, ImVec2 point, bool touch)
	{
		auto &e = Session();
		if (!id || !e.model || e.drag.Phase() != DragPhase::Idle) return;
		e.source = std::string(id);
		const auto kind = ImGui::GetIO().MouseSource == ImGuiMouseSource_Pen ? DragPointerKind::Pen
						  : touch											 ? DragPointerKind::Touch
																			 : DragPointerKind::Mouse;
		e.drag.BeginDragging(kind == DragPointerKind::Mouse ? 0 : 1, kind, Pt(point), {e.revision});
		ImGui::SetActiveID(e.capture, ImGui::GetCurrentWindow());
		e.dirty = true;
		Wake();
	}
	void Draw(ImGuiWindow* scroll, void (*queueWrite)())
	{
		auto &e = Session();
		auto &io = ImGui::GetIO();
		auto *window = ImGui::GetCurrentWindow();
		e.capture = window->GetID("##component-preview");
		e.lastFrame = ImGui::GetFrameCount();
		auto &buttons = barUISet.barButtonSet;
		auto published = buttons.ReadBarLayout();
		if (!published)
		{
			e.Cancel();
			e.model.reset();
			return;
		}
		if (!e.model || !(*e.model == *published))
		{
			e.Cancel();
			e.model = published;
			e.instance.SetLayout(*published, ++e.revision);
		}
		auto epoch = Pipeline::GetDeviceEpoch();
		if (!e.Device(epoch))
		{
			Wake();
			return;
		}
		const ImVec2 origin = ImGui::GetCursorScreenPos();
		ImVec2 mouse = io.MousePos;
		const float width = std::floor(ImGui::GetContentRegionAvail().x);
		if (width < 1)
		{
			e.Cancel();
			return;
		}
		const auto catalog = buttons.GetLayoutCatalog();
		const auto &cfg = config.Experimental.Inkeys3.UI3;
		PreviewInputs in;
		in.time = std::chrono::steady_clock::now();
		in.width = width;
		in.height = (std::max)(Design::Pixels(80), scroll->InnerClipRect.GetHeight());
		in.targetHeight = Design::Pixels(64);
		in.reserveHintSpace = true;
		in.sizeMode = NormalizeBarSizeMode(cfg.SizeMode.load());
		in.iconOrder = config.UI.Bar.IconOrder.load();
		in.edgeLighting = cfg.EdgeLighting.Enable.load();
		in.dynamicLighting = cfg.EdgeLighting.Dynamic.load();
		in.fractionalFont = cfg.AllowFractionalFontSize.load();
		in.typography = BarContentInstance::CurrentTypography();
		const auto assets = Pipeline::GetSharedAssets();
		in.writeFactory = assets.dwriteFactory.Get();
		in.fonts = assets.fontCollection.Get();
		const auto kind = io.MouseSource == ImGuiMouseSource_TouchScreen ? DragPointerKind::Touch
						  : io.MouseSource == ImGuiMouseSource_Pen		 ? DragPointerKind::Pen
																		 : DragPointerKind::Mouse;
		const uint32_t pointer = kind == DragPointerKind::Mouse ? 0 : 1;
		// 拖动中输入来源变化（鼠标与触摸混用）时，ImGui 合流后的坐标与按键已不属于同一接触，
		// 继续会把放下落到错误位置甚至误删，因此取消。逐条检查本帧实际消费的指针事件，
		// 而不只看帧末来源：同一帧先触摸移动、后鼠标抬起时帧末来源是鼠标，坐标却来自触摸。
		const auto toKind = [](ImGuiMouseSource source)
		{
			return source == ImGuiMouseSource_TouchScreen ? DragPointerKind::Touch
				: source == ImGuiMouseSource_Pen ? DragPointerKind::Pen : DragPointerKind::Mouse;
		};
		bool sourceChanged = e.drag.Phase() != DragPhase::Idle && kind != e.drag.Kind();
		if (e.drag.Phase() != DragPhase::Idle)
			for (const ImGuiInputEvent &event : ImGui::GetCurrentContext()->InputEventsTrail)
				if ((event.Type == ImGuiInputEventType_MousePos && toKind(event.MousePos.MouseSource) != e.drag.Kind()) ||
					(event.Type == ImGuiInputEventType_MouseButton && toKind(event.MouseButton.MouseSource) != e.drag.Kind()))
					sourceChanged = true;
		// 原生捕获被夺走后收不到抬起，ImGui 会一直认为按住，下一次点击才补发抬起（可能落到别处）。
		// 设置页渲染线程无法查询窗口线程的捕获，鼠标拖动时改用与线程无关的物理主按钮状态：
		// 系统已松开而 ImGui 仍认为按住，连续 3 帧即取消。触摸没有可靠的等价状态，不做此判定。
		if (e.drag.Phase() != DragPhase::Idle && e.drag.Kind() == DragPointerKind::Mouse && io.MouseDown[0])
		{
			const int primary = ::GetSystemMetrics(SM_SWAPBUTTON) ? VK_RBUTTON : VK_LBUTTON;
			e.releasedFrames = (::GetAsyncKeyState(primary) & 0x8000) ? 0 : e.releasedFrames + 1;
		}
		else
			e.releasedFrames = 0;
		const bool captureLost = e.releasedFrames >= 3;
		in.mouse = kind == DragPointerKind::Mouse;
		ImVec2 offset = Offset(origin, e.visible);
		D2D1_POINT_2F local{mouse.x - offset.x, mouse.y - offset.y};
		const bool over = ImGui::IsWindowHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem) &&
						  window->ClipRect.Contains(mouse) && mouse.y >= origin.y &&
						  mouse.y < origin.y + e.visible.extent.height && mouse.x >= origin.x &&
						  mouse.x < origin.x + width;
		const bool free = ImGui::GetActiveID() == 0 || ImGui::GetActiveID() == e.capture;
		if (over && free) in.pointer = local;
		if (e.lastMouse.x != mouse.x || e.lastMouse.y != mouse.y) ++e.pointerSerial;
		in.pointerSerial = e.pointerSerial;
		e.lastMouse = mouse;
		const bool cancelled = (ImGui::GetCurrentContext()->CurrentItemFlags & ImGuiItemFlags_Disabled) || io.AppFocusLost || ImGui::IsKeyPressed(ImGuiKey_Escape) ||
							   (e.drag.Phase() != DragPhase::Idle && ImGui::GetActiveID() != e.capture) || sourceChanged || captureLost;
		if (cancelled)
		{
			e.Cancel();
			e.fixedUntil = 0;
		}
		auto hit = e.instance.HitTest(local);
		const double now = ImGui::GetTime();
		if (e.drag.Phase() == DragPhase::Idle && !cancelled && over && free && !ImGui::IsAnyItemHovered() &&
			ImGui::IsMouseClicked(0))
		{
			if (hit.kind == PreviewHitKind::Boundary)
				e.fixedUntil = now + 0.8;
			else if (hit.position || hit.kind == PreviewHitKind::More || hit.kind == PreviewHitKind::Close)
			{
				e.pressed = hit;
				if (hit.position) e.source = *hit.position;
				e.drag.Press(pointer, kind, Pt(mouse), {e.revision}, Design::Pixels(1));
				ImGui::SetActiveID(e.capture, window);
			}
		}
		if (e.drag.Phase() != DragPhase::Idle)
		{
			ImGui::KeepAliveID(e.capture);
			if (e.drag.Move(pointer, Pt(mouse)) &&
				(!e.pressed.position || !e.instance.Lift(*e.pressed.position, e.drag.Payload().token)))
				e.Cancel();
		}
		in.editing = DragActive();
		Editor::Style style{in.width,
							in.height,
							in.targetHeight,
							in.sizeMode,
							in.iconOrder,
							in.edgeLighting,
							in.dynamicLighting,
							in.fractionalFont,
							in.typography.revision,
							GetThemeLightWeight(),
							GetThemeResidentContentOpacity(),
							GetThemeResidentBackgroundOpacityOverride(),
							UiAnimationEnabled,
							UiAnimationSpeedRate};
		const bool due = e.visible.hintDeadline && in.time >= *e.visible.hintDeadline;
		const bool pointerChanged =
			in.pointer.has_value() != e.drawnPointer.has_value() ||
			(in.pointer && (in.pointer->x != e.drawnPointer->x || in.pointer->y != e.drawnPointer->y));
		auto frame = e.instance.Advance(in);
		// 静止时复用已确认的像素；动画结束的最后一帧也必须提交。
		if (e.dirty || !e.style || *e.style != style || frame.active || e.visible.active || due || pointerChanged ||
			!e.instance.Geometry().frameId)
		{
			const bool ok = SUCCEEDED(e.Render(e.preview, epoch, UINT(in.width), UINT(std::ceil(in.height)),
									 [&] { return e.instance.Draw(e.target, frame); }));
			e.instance.ConfirmFrame(frame.frameId, ok);
			e.dirty = !ok;
			if (ok)
			{
				e.visible = frame;
				e.style = style;
				e.drawnPointer = in.pointer;
			}
			else
			{
				if (e.drag.Phase() != DragPhase::Idle) e.Cancel();
				Wake();
				return;
			}
		}
		offset = Offset(origin, e.visible);
		local = {mouse.x - offset.x, mouse.y - offset.y};
		hit = e.instance.HitTest(local);
		e.preview.Image(window->DrawList, offset, e.visible.bounds);
		ImGui::Dummy({width, e.visible.extent.height + Design::Pixels(Design::RowGap)});
		const bool dragging = DragActive();
		const bool release = pointer == e.drag.PointerId() && ImGui::IsMouseReleased(0);
		const auto &palette = Design::GetPalette();
		const auto &keys = I18nKey.SettingsUI.Component;
		if (dragging)
		{
			// 正文内层 child 自动增高，滚动与垃圾桶均以外层可视区域为准。
			const ImRect viewport = scroll->InnerClipRect;
			const float edge = Design::Pixels(48);
			const float speed = std::clamp((mouse.y - viewport.Max.y + edge) / edge, 0.f, 1.f) -
								std::clamp((viewport.Min.y + edge - mouse.y) / edge, 0.f, 1.f);
			// 浮点累计目标位置：ImGui 每帧把 Scroll 取整，直接在取整值上积分会吞掉亚像素增量；
			// 滚轮等外部滚动使实际位置偏离超过 1 像素时，以实际位置重新基准。
			if (e.scrollPosition < 0 || std::abs(e.scrollPosition - scroll->Scroll.y) >= 1.f) e.scrollPosition = scroll->Scroll.y;
			if (speed != 0 && mouse.x >= viewport.Min.x && mouse.x <= viewport.Max.x)
			{
				e.scrollPosition = std::clamp(e.scrollPosition + speed * Design::Pixels(1200) * io.DeltaTime, 0.f, scroll->ScrollMax.y);
				ImGui::SetScrollY(scroll, e.scrollPosition);
			}
			const bool clipped = window->ClipRect.Contains(mouse);
			const bool more = clipped && hit.kind == PreviewHitKind::More;
			if (more && e.moreSince < 0) e.moreSince = now;
			if (!more) e.moreSince = -1;
			if (more && !e.expanded && now - e.moreSince >= 0.5)
			{
				e.expanded = true;
				e.instance.SetMoreExpanded(true);
				e.dirty = true;
			}
			const auto *from = std::get_if<Position>(&e.source);
			const ItemSource source =
				from ? ItemSource(*from) : ItemSource(std::string_view(std::get<std::string>(e.source)));
			const bool removable = !from || Remove(*published, *from, catalog).has_value();
			const float step =
				UiAnimationEnabled ? 1.f - std::exp(-io.DeltaTime * 16.f * float(UiAnimationSpeedRate)) : 1.f;
			e.trashAmount += (1 - e.trashAmount) * step;
			ImVec2 center{viewport.Max.x - Design::Pixels(42),
						  viewport.Max.y - Design::Pixels(42) + Design::Pixels(12) * (1 - e.trashAmount)};
			const bool trash = std::hypot(mouse.x - center.x, mouse.y - center.y) <= Design::Pixels(36);
			e.trashHover += ((trash && removable ? 1.f : 0.f) - e.trashHover) * step;
			auto *front = ImGui::GetForegroundDrawList();
			const auto color = trash && removable ? palette.card
							   : removable		  ? palette.accent
												  : palette.textDisabled;
			auto fade = [&](ImU32 c) {
				return (c & ~IM_COL32_A_MASK) | (ImU32((c >> IM_COL32_A_SHIFT) * e.trashAmount) << IM_COL32_A_SHIFT);
			};
			front->AddCircleFilled(center, Design::Pixels(24 + 4 * e.trashHover), fade(trash && removable ? palette.criticalText : palette.card));
			// 与设置页其他删除入口一致，使用 Fluent 删除字形；靠近时随热区一起放大。
			const float glyph = Design::Pixels(10.0F + 2.0F * e.trashHover);
			ImFluent::DrawIcon(front, "\ue74d", {center.x - glyph, center.y - glyph}, {center.x + glyph, center.y + glyph},
							   18.0F + 3.0F * e.trashHover, fade(color));
			auto gaps = clipped ? e.instance.InsertionGaps(local) : std::vector<PreviewGap>{};
			std::vector<Position> positions;
			for (const auto &gap : gaps)
				positions.push_back(gap.position);
			const auto drop = ResolveDrop(*published, source, catalog, trash, more, positions);
			if (drop.action == DropAction::Insert)
			{
				auto gap = std::find_if(gaps.begin(), gaps.end(), [&](const auto &g) {
					return g.position.index == drop.gap->index && g.position.segment == drop.gap->segment;
				});
				auto follow = [&](std::optional<ImVec2> &old, D2D1_POINT_2F dest) {
					if (!old) old = ImVec2(dest.x, dest.y);
					old->x += (dest.x - old->x) * step;
					old->y += (dest.y - old->y) * step;
					return ImVec2(offset.x + old->x, offset.y + old->y);
				};
				const auto a = follow(e.cursorStart, gap->start), b = follow(e.cursorEnd, gap->end);
				const float thickness = Design::Pixels(3);
				window->DrawList->AddLine(a, b, palette.accent, thickness);
				for (auto point : {a, b})
					window->DrawList->AddCircleFilled(point, thickness / 2, palette.accent);
			}
			else
			{
				e.cursorStart.reset();
				e.cursorEnd.reset();
			}
			// 不可放置时区分原因：默认 A1/A2 按钮只能区内调整；其他来源统一为“不能放在这里”；
			// 指针既不在预览上也不在垃圾桶上时不显示提示。
			const bool overPreview = clipped && mouse.x >= origin.x && mouse.x < origin.x + width &&
									 mouse.y >= origin.y && mouse.y < origin.y + e.visible.extent.height;
			const bool fixedSource = from && catalog.contains((*published)[from->segment][from->index].Id) &&
									 catalog.at((*published)[from->segment][from->index].Id).zone != Segment::B1 &&
									 (*published)[from->segment][from->index].Id != BarButtonId::UserDivider;
			const char *hint = drop.action == DropAction::Insert	? keys.DragInsert
							   : drop.action == DropAction::Store		? keys.DragStore
							   : drop.action == DropAction::Delete		? keys.DragDelete
							   : drop.action == DropAction::CancelAdd ? keys.DragCancelAdd
							   : !overPreview && !trash				? nullptr
							   : fixedSource							? keys.DragNotAllowed
																		: keys.DragNotHere;
			const UINT w = UINT(std::ceil(Design::Pixels(512))), h = UINT(std::ceil(Design::Pixels(200)));
			const ImVec2 ghostOrigin{std::clamp(mouse.x - w / 2, 0.f, (std::max)(0.f, io.DisplaySize.x - w)),
									 std::clamp(mouse.y - h / 2, 0.f, (std::max)(0.f, io.DisplaySize.y - h))};
			if (SUCCEEDED(e.Render(e.overlay, epoch, w, h, [&] {
					return e.instance.DrawGhost(e.target, e.source, {mouse.x - ghostOrigin.x, mouse.y - ghostOrigin.y},
												hint ? IW(hint) : std::wstring{}, trash && removable);
				})))
				e.overlay.Image(front, ghostOrigin, {0, 0, float(w), float(h)});
			else
			{
				// 浮层绘制失败（含目标失效）：本帧不提交放下，安全取消。
				e.Cancel();
				Wake();
				return;
			}
			if (release)
			{
				const auto sourceRevision = e.drag.Payload().token;
				e.drag.Release(pointer, Pt(mouse));
				std::optional<BarLayoutModel> next;
				if (drop.gap)
					next = from ? Move(*published, *from, *drop.gap, catalog)
								: Insert(*published, std::get<std::string>(e.source), *drop.gap, catalog);
				else if (drop.action == DropAction::Delete)
					next = Remove(*published, *from, catalog);
				if (next)
				{
					const auto result = buttons.ApplyBarLayout(*published, *next);
					if (result == decltype(result)::Applied)
					{
						queueWrite();
						if (drop.gap) e.instance.Place(*next, *drop.gap, sourceRevision);
					}
				}
				e.Cancel();
				e.model = buttons.ReadBarLayout();
				if (e.model) e.instance.SetLayout(*e.model, ++e.revision);
			}
		}
		else if (release && e.drag.Release(pointer, Pt(mouse)) == DragSession::ReleaseResult::Click)
		{
			if (over && hit.kind == e.pressed.kind &&
				(hit.kind == PreviewHitKind::More || hit.kind == PreviewHitKind::Close))
			{
				e.expanded = hit.kind == PreviewHitKind::More && !e.expanded;
				e.instance.SetMoreExpanded(e.expanded);
			}
			e.Cancel();
		}
		if (e.fixedUntil > now)
		{
			if (!dragging)
			{
				const UINT w = UINT(std::ceil(Design::Pixels(320))), h = UINT(std::ceil(Design::Pixels(100)));
				const ImVec2 topLeft{std::clamp(mouse.x - w / 2, 0.f, (std::max)(0.f, io.DisplaySize.x - w)),
									 std::clamp(mouse.y - h / 2, 0.f, (std::max)(0.f, io.DisplaySize.y - h))};
				if (SUCCEEDED(e.Render(e.overlay, epoch, w, h, [&] {
						return e.instance.DrawFollowingHint(e.target, {mouse.x - topLeft.x, mouse.y - topLeft.y},
															IW(keys.DragFixed), 12);
					})))
					e.overlay.Image(ImGui::GetForegroundDrawList(), topLeft, {0, 0, float(w), float(h)});
			}
			Wake();
		}
		if (e.dirty || dragging || frame.active || frame.hintDeadline) Wake();
	}
}
