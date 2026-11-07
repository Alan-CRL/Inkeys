#include "Setting.Pages.h"
#include "Setting.HomeBrand.h"
#include "imfluent/imfluent_internal.h"
#include <cstring>
#include <cfloat>
#include <format>
#include "../../../IdtI18n.h"
#include "../../../IdtI18nKeys.g.h"
#include <array>
#include "imgui/imgui_internal.h"
#include "../../Drawing/Draw3/Draw3.CursorSettings.h"
#include "../StartupPlacement.h"

import Inkeys.Conv.Text;

namespace Inkeys::UI::Setting::Design
{
	namespace
	{
		struct InlineSpan { std::string text; HomeAction action = HomeAction::None; };
		struct InlineFragment { std::string text; float x, y, width; size_t span, line; HomeAction action; };
		struct InlineLayout { std::vector<InlineFragment> fragments; float height = 0; };

		InlineLayout MeasureInline(std::initializer_list<InlineSpan> spans, float width,
			ImFluentTextStyle role, bool centered, float fontSize = 0, float lineDip = 0)
		{
			InlineLayout result;
			std::string joined; for (const auto& span : spans) joined += span.text;
			ImFluent::PushFont(role, fontSize);
			const float lineHeight = Pixels(lineDip ? lineDip : role == ImFluentTextStyle_Caption ? CaptionText.lineHeight : BodyText.lineHeight);
			const char* cursor = joined.c_str(); const char* end = cursor + joined.size(); size_t line = 0;
			while (cursor < end)
			{
				const char* stop = ImGui::GetFont()->CalcWordWrapPosition(ImGui::GetFontSize(), cursor, end, width);
				if (stop == cursor) { unsigned codepoint; stop += (std::max)(1, ImTextCharFromUtf8(&codepoint, cursor, end)); }
				const float lineWidth = ImGui::CalcTextSize(cursor, stop).x;
				float x = centered ? (width - lineWidth) * .5F : 0;
				size_t offset = 0, spanIndex = 0;
				for (const auto& span : spans)
				{
					const size_t from = (std::max)(offset, static_cast<size_t>(cursor - joined.c_str()));
					const size_t to = (std::min)(offset + span.text.size(), static_cast<size_t>(stop - joined.c_str()));
					if (to > from)
					{
						std::string text = joined.substr(from, to - from);
						const float fragmentWidth = ImGui::CalcTextSize(text.c_str()).x;
						result.fragments.push_back({text, x, line * lineHeight, fragmentWidth, spanIndex, line, span.action});
						x += fragmentWidth;
					}
					offset += span.text.size(); ++spanIndex;
				}
				cursor = stop; while (cursor < end && (*cursor == ' ' || *cursor == '\t')) ++cursor;
				++line;
			}
			result.height = line * lineHeight;
			ImFluent::PopFont();
			return result;
		}

		void DrawInline(const char* id, const InlineLayout& layout, ImVec2 origin,
			ImFluentTextStyle role, ImU32 color, HomeAction& action, float fontSize = 0, float lineDip = 0,
			const HomeMotionState* /*motion*/ = nullptr)
		{
			// 先测完整句子的换行；链接只登记各行中的真实片段，不把前后说明变成链接。
			ImGui::PushID(id);
			const float lineHeight = Pixels(lineDip ? lineDip : role == ImFluentTextStyle_Caption ? CaptionText.lineHeight : BodyText.lineHeight);
			ImFluent::PushFont(role, fontSize);
			for (const auto& fragment : layout.fragments)
			{
				const ImVec2 pos(origin.x + fragment.x, origin.y + fragment.y);
				ImU32 ink = color;
				if (fragment.action != HomeAction::None)
				{
					ImGui::PushID(static_cast<int>(fragment.span)); ImGui::PushID(static_cast<int>(fragment.line));
					ImGui::SetCursorScreenPos(pos);
					if (ImGui::InvisibleButton("##inline-link", {fragment.width, lineHeight}, ImGuiButtonFlags_EnableNav)) action = fragment.action;
					AllowContentDrag();
					ink = ImFluent::GetColorU32(ImGui::IsItemActive() ? ImFluentCol_AccentTextTertiary
						: ImGui::IsItemHovered() ? ImFluentCol_AccentTextSecondary : ImFluentCol_AccentTextPrimary);
					if (ImGui::IsItemHovered())
					{
						ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
						ImGui::GetWindowDrawList()->AddLine({pos.x,pos.y+lineHeight-1}, {pos.x+fragment.width,pos.y+lineHeight-1}, ink);
					}
					ImGui::PopID(); ImGui::PopID();
				}
				ImGui::GetWindowDrawList()->AddText(ImGui::GetFont(), ImGui::GetFontSize(),
					{std::round(pos.x),std::round(pos.y+(lineHeight-ImGui::GetFontSize())*.5F)},ink,fragment.text.c_str());
			}
			ImFluent::PopFont();
			ImGui::PopID();
		}

		void CenteredCopy(const char* text, ImVec2 origin, float width, ImFluentTextStyle role,
			ImU32 color, HomeAction& action)
		{
			DrawInline("##home-copy", MeasureInline({{text}}, width, role, true), origin, role, color, action);
		}

		void HeroBackground(ImDrawList* draw, ImVec2 origin, float width, float height)
		{
			const auto& palette = GetPalette();
			const int start = draw->VtxBuffer.Size;
			draw->AddRectFilled(origin, {origin.x+width,origin.y+height}, palette.heroFill, Pixels(8));
			// 仅着色本横幅的圆角顶点，保留抗锯齿 alpha；不会溢出到页面背景。
			for (int i = start; i < draw->VtxBuffer.Size; ++i)
			{
				auto& v = draw->VtxBuffer[i];
				const float x = std::clamp((v.pos.x-origin.x)/width, 0.0F, 1.0F);
				const float y = std::clamp((v.pos.y-origin.y)/height, 0.0F, 1.0F);
				ImU32 color = v.col & IM_COL32_A_MASK;
				for (int shift : {IM_COL32_R_SHIFT,IM_COL32_G_SHIFT,IM_COL32_B_SHIFT})
				{
					const float a = static_cast<float>((palette.heroMint >> shift) & 255), b = static_cast<float>((palette.heroLavender >> shift) & 255);
					const float base = static_cast<float>((palette.heroFill >> shift) & 255);
					color |= static_cast<ImU32>(std::round((a+(b-a)*x)*(1-y*.72F)+base*y*.72F)) << shift;
				}
				v.col = color;
			}
			draw->AddRect(origin, {origin.x+width,origin.y+height}, palette.heroStroke, Pixels(8));
		}

		bool HomePointerAllowed(const HomeMotionState& motion)
		{
			const auto& io=ImGui::GetIO();
			return !motion.EntryActive() && motion.entry!=HomeMotionState::Entry::Pending
				&& motion.pointerAllowed && !io.AppFocusLost && io.MouseSource==ImGuiMouseSource_Mouse
				&& !ImGui::IsAnyMouseDown() && !ImGui::IsAnyItemActive()
				&& !ImGui::IsPopupOpen(nullptr,ImGuiPopupFlags_AnyPopupId|ImGuiPopupFlags_AnyPopupLevel)
				&& ImGui::IsWindowHovered(ImGuiHoveredFlags_RootAndChildWindows);
		}

		void DrawHomeBrand(ImDrawList* draw, ImVec2 origin, float width, HomeMotionState* motion)
		{
			const float height=HomeBrand::Height(width);
			const ImRect bounds(origin,{origin.x+width,origin.y+height});
			ImRect envelope=bounds; envelope.Expand(Pixels(14));
			const bool visible=envelope.Overlaps(ImGui::GetCurrentWindow()->ClipRect);
			if (motion) { motion->brandOrigin=origin; motion->brandWidth=width; motion->brandVisible=visible; }
			if (motion && motion->EntryActive()) { motion->x=motion->y=0; return; }
			if (!visible) return;
			if (!motion) { HomeBrand::Draw(draw,origin,width,IsDarkMode()); return; }
			float targetX=0, targetY=0;
			if (motion->enabled && HomePointerAllowed(*motion))
			{
				const auto mouse=ImGui::GetIO().MousePos, center=bounds.GetCenter();
				const float dx=(std::max)(0.0F,std::abs(mouse.x-center.x)-width*.5F);
				const float dy=(std::max)(0.0F,std::abs(mouse.y-center.y)-height*.5F);
				const float fade=1-HomeBrand::Smooth(std::hypot(dx,dy)/Pixels(120));
				targetX=std::clamp((mouse.x-center.x)/(width*.5F),-1.0F,1.0F)*fade;
				targetY=std::clamp((mouse.y-center.y)/(height*.5F),-1.0F,1.0F)*fade;
			}
			const float follow=1-std::exp(-motion->delta/.16F);
			auto advance=[&](float value,float target) {
				value+=(target-value)*follow;
				return !motion->enabled || std::abs(value-target)<.0001F ? target : value;
			};
			motion->x=advance(motion->x,targetX); motion->y=advance(motion->y,targetY);
			HomeBrand::Prepare(motion->cache,width,Pixels(1),IsDarkMode(),motion->x,motion->y);
			HomeBrand::Draw(draw,origin,width,IsDarkMode(),&motion->cache);
		}

		bool HomeIconButton(const char* id, const char* label, const char* icon, ImVec2 pos,
			float width, bool accent, HomeMotionState* motion)
		{
			ImGui::SetCursorScreenPos(pos);
			if (!accent)
			{
				const bool dark=IsDarkMode();
				ImFluent::PushStyleColor(ImFluentCol_ControlFillDefault,dark ? IM_COL32(255,255,255,14) : IM_COL32(255,255,255,155));
				ImFluent::PushStyleColor(ImFluentCol_ControlFillSecondary,dark ? IM_COL32(255,255,255,24) : IM_COL32(255,255,255,205));
				ImFluent::PushStyleColor(ImFluentCol_ControlFillTertiary,dark ? IM_COL32(255,255,255,10) : IM_COL32(255,255,255,115));
			}
			const bool pressed = accent ? AccentButton(id, {width,Pixels(36)}) : Button(id, {width,Pixels(36)});
			if (!accent) ImFluent::PopStyleColor(3);
			AllowContentDrag();
			float pulse=0;
			if (motion)
			{
				auto& state=motion->buttons[accent ? 1 : 0];
				const auto minimum=ImGui::GetItemRectMin(), maximum=ImGui::GetItemRectMax();
				motion->buttonBounds[accent ? 1 : 0]={minimum.x,minimum.y,maximum.x,maximum.y};
				const bool hovered=ImGui::IsItemHovered() && HomePointerAllowed(*motion);
				if (hovered && !state.hovered && motion->enabled) state.elapsed=0;
				state.hovered=hovered;
				state.elapsed=motion->enabled && motion->pointerAllowed ? (std::min)(.32F,state.elapsed+motion->delta) : .32F;
				pulse=state.elapsed<.32F ? std::sin(state.elapsed/.32F*3.141592654F) : 0;
			}
			ImDrawList* draw=ImGui::GetWindowDrawList();
			if (accent) draw->AddLine({pos.x+Pixels(4),pos.y+Pixels(1)},
				{pos.x+width-Pixels(4),pos.y+Pixels(1)},IM_COL32(255,255,255,32),Pixels(1));
			const ImU32 ink = ImFluent::GetColorU32(accent ? ImFluentCol_TextOnAccentPrimary : ImFluentCol_TextPrimary);
			const float textWidth = ControlTextWidth(label), groupWidth = Pixels(24) + textWidth;
			const float left = pos.x + (width-groupWidth)*.5F;
			const int start=draw->VtxBuffer.Size;
			ImFluent::DrawIcon(icon, {left,pos.y}, {left+Pixels(16),pos.y+Pixels(36)},16,ink);
			// 仅图标顶点动一下，控件命中框、标签和键盘焦点保持原位。
			const ImVec2 center(left+Pixels(8),pos.y+Pixels(18));
			const float angle=accent ? 0 : pulse*.14F, c=std::cos(angle), sn=std::sin(angle), growth=1+pulse*.12F;
			for (int i=start;i<draw->VtxBuffer.Size;++i)
			{
				auto& v=draw->VtxBuffer[i]; const float x=(v.pos.x-center.x)*growth,y=(v.pos.y-center.y)*growth;
				v.pos={center.x+x*c-y*sn,center.y+x*sn+y*c-(accent ? Pixels(1.5F)*pulse : 0)};
			}
			if (!accent && pulse>.001F)
			{
				const ImU32 glint=(Accent & ~IM_COL32_A_MASK)|(static_cast<ImU32>(pulse*120)<<IM_COL32_A_SHIFT);
				const ImVec2 point(left+Pixels(18),pos.y+Pixels(9));
				draw->AddLine({point.x-Pixels(2),point.y},{point.x+Pixels(2),point.y},glint);
				draw->AddLine({point.x,point.y-Pixels(2)},{point.x,point.y+Pixels(2)},glint);
			}
			ImFluent::PushFont(ImFluentTextStyle_Body, ControlText.size);
			draw->AddText(ImGui::GetFont(),ImGui::GetFontSize(),
				{std::round(left+Pixels(24)),std::round(pos.y+(Pixels(36)-ImGui::GetFontSize())*.5F)},ink,label);
			ImFluent::PopFont();
			return pressed;
		}

		void Link(const char* /*id*/, const char* label, HomeAction kind, HomeAction& action, bool bodyFont = false)
		{
			ImGui::PushID(static_cast<int>(kind));
			const std::string stableLabel = std::string(label) + "###link";
			// 作者联系方式统一正文大小，其他页面链接保留原控件字号与稳定 ID。
			if (bodyFont) ImFluent::PushFont(ImFluentTextStyle_Body);
			const bool pressed = bodyFont ? ImFluent::HyperlinkButton(stableLabel.c_str()) : HyperlinkButton(stableLabel.c_str());
			if (bodyFont) ImFluent::PopFont();
			if (pressed) action = kind;
			ImGui::PopID();
		}
	}

	HomeEntryPose ResolveHomeEntryPose(const HomeMotionState& state)
	{
		HomeEntryPose pose{state.brandOrigin,state.brandWidth,1,0};
		if (!state.EntryActive()) return pose;
		const float time=state.entryElapsed;
		pose.origin=state.entryStart; pose.width=state.entryStartWidth; pose.opacity=1;
		pose.writing=time>=HomeEntryTiming::WritingEnd ? 1.0F
			: std::clamp((time-HomeEntryTiming::Wait)/HomeEntryTiming::Writing,0.0F,1.0F);
		// 详情入口写完即原地渐隐；主页仍保留停留与归位。
		if (state.fadeOnly)
		{
			pose.opacity=1-HomeBrand::Smooth(std::clamp((time-HomeEntryTiming::WritingEnd)/HomeEntryTiming::Dock,0.0F,1.0F));
			return pose;
		}
		// 六笔完全写好后保持 600ms；1.90s 前不透出界面、不移动字标。
		if (time<=HomeEntryTiming::DockStart) return pose;
		const float phase=std::clamp((time-HomeEntryTiming::DockStart)/HomeEntryTiming::Dock,0.0F,1.0F), u=HomeBrand::Smooth(phase), v=1-u;
		const float dpi=Pixels(1), endWidth=state.brandWidth*1.004F;
		const ImVec2 start(state.entryStart.x+state.entryStartWidth*.5F,state.entryStart.y+HomeBrand::Height(state.entryStartWidth)*.5F);
		const ImVec2 target(state.brandOrigin.x+state.brandWidth*.5F,state.brandOrigin.y+HomeBrand::Height(state.brandWidth)*.5F+1.5F*dpi);
		const ImVec2 p1(start.x+36*dpi,start.y-42*dpi), p2(target.x-24*dpi,target.y-38*dpi);
		const ImVec2 center(v*v*v*start.x+3*v*v*u*p1.x+3*v*u*u*p2.x+u*u*u*target.x,
			v*v*v*start.y+3*v*v*u*p1.y+3*v*u*u*p2.y+u*u*u*target.y);
		pose.width=state.entryStartWidth+(endWidth-state.entryStartWidth)*u;
		pose.origin={center.x-pose.width*.5F,center.y-HomeBrand::Height(pose.width)*.5F};
		pose.opacity=1-HomeBrand::Smooth(phase);
		if (time>=HomeEntryTiming::SettleStart)
		{
			const float settle=1-HomeBrand::Smooth((time-HomeEntryTiming::SettleStart)/HomeEntryTiming::Settle);
			pose.width=state.brandWidth*(1+.004F*settle);
			pose.origin={state.brandOrigin.x+(state.brandWidth-pose.width)*.5F,
				state.brandOrigin.y+1.5F*dpi*settle+(HomeBrand::Height(state.brandWidth)-HomeBrand::Height(pose.width))*.5F};
		}
		return pose;
	}

	namespace { void ResolveHomeEntryLayout(HomeMotionState& state); }

	void ConfigureHomeEntryViewport(HomeMotionState& state, ImVec4 clientContent, ImVec4 contentClip)
	{
		state.clientContent=clientContent; state.contentClip=contentClip;
		state.viewportReady=clientContent.z>clientContent.x && clientContent.w>clientContent.y
			&& contentClip.z>contentClip.x && contentClip.w>contentClip.y;
		if (state.fadeOnly) ResolveHomeEntryLayout(state);
	}

	bool CanOmitHomeEntryRootBackground(const HomeMotionState& state, bool solidBackdrop,
		float styleAlpha, ImVec4 windowBackground) noexcept
	{
		// 同色不透明 clear 已建立底色；只省根填充，保留 Begin 原 flags 的边框与输入合同。
		return state.home && state.EntryActive() && solidBackdrop && styleAlpha==1 && windowBackground.w==1
			&& std::isfinite(windowBackground.x) && windowBackground.x>=0 && windowBackground.x<=1
			&& std::isfinite(windowBackground.y) && windowBackground.y>=0 && windowBackground.y<=1
			&& std::isfinite(windowBackground.z) && windowBackground.z>=0 && windowBackground.z<=1;
	}

	HomeEntryFrameKey MakeHomeEntryFrameKey(ImVec4 contentBounds, float gutter, std::uint64_t epoch,
		std::uint64_t fontSerial, int language, ImFont* regular, ImFont* strong, ImFont* icons, int backdrop)
	{
		const auto& io=ImGui::GetIO(); const auto* atlas=io.Fonts;
		HomeEntryFrameKey key;
		key.geometry={io.DisplaySize.x,io.DisplaySize.y,io.DisplayFramebufferScale.x,io.DisplayFramebufferScale.y,
			contentBounds.x,contentBounds.y,contentBounds.z,contentBounds.w,gutter,Pixels(1)};
		key.fonts={reinterpret_cast<std::uintptr_t>(atlas),reinterpret_cast<std::uintptr_t>(regular),
			reinterpret_cast<std::uintptr_t>(strong),reinterpret_cast<std::uintptr_t>(icons)};
		key.epoch=epoch; key.fontSerial=fontSerial; key.language=language; key.dark=IsDarkMode(); key.backdrop=backdrop;
		const auto& style=ImGui::GetStyle(); const auto color=style.Colors[ImGuiCol_WindowBg];
		key.material={style.Alpha,color.x,color.y,color.z,color.w};
		if (atlas->TexData)
		{
			key.texture=atlas->TexData->UniqueID; key.atlasWidth=atlas->TexData->Width; key.atlasHeight=atlas->TexData->Height;
		}
		return key;
	}

	bool BeginHomeEntryFrame(HomeMotionState& state, const HomeEntryFrameKey& key, bool allowFreeze)
	{
		auto& prep=state.preparation; prep.managed=true; ++prep.frame;
		prep.skipBackground=false; prep.fullFrame=true; prep.layoutReady=false; prep.previousElapsed=state.entryElapsed;
		if (!state.EntryActive() && state.entry!=HomeMotionState::Entry::Pending) return false;
		if (!prep.keyValid || prep.key!=key)
		{
			// DPI/布局/字形图集/资源 epoch 变化时先重走真实页；保留已消费 Show 与动画进度。
			prep.key=key; prep.keyValid=true; prep.ready=false; prep.warmFrames=0;
			prep.revealFrames=0; prep.revealReady=false; prep.revealPending=false;
			prep.restoreFrames=0; prep.restoreReady=prep.restorePending=false; state.entryLayoutValidated=false;
		}
		if (!allowFreeze) { FinishHomeEntry(state); return false; }
		if (!prep.ready || !state.EntryActive()) return false;
		const float next=(std::min)(state.EntryTotal(),state.entryElapsed+state.delta);
		if (!prep.revealReady && next>=HomeEntryTiming::WritingEnd)
		{
			// 书写完成后仍保持不透明：至少两次完整排布且 native 已消费当前测量后，再截无字标页。
			prep.revealPending=true;
			state.entryElapsed=(std::max)(state.entryElapsed,HomeEntryTiming::WritingEnd);
			return false;
		}
		if (next>=state.EntryTotal() && !prep.restoreReady)
		{
			// 终态前在缓存遮挡下恢复真实 child，至少两帧且尺寸就绪才解除遮挡。
			prep.restorePending=true;
			return false;
		}
		state.entryElapsed=next;
		if (next>=state.EntryTotal()) { FinishHomeEntry(state); return false; }
		// style alpha 非 1 时蒙版未真正不透明；截图就绪前仍需绘制实际底层。
		if (!prep.revealReady && key.material[0]!=1) return false;
		prep.skipBackground=true; prep.fullFrame=false; ++prep.skippedFrames;
		return true;
	}

	bool HomeEntryTexturesReady(const ImDrawData* data)
	{
		if (!data || !data->Textures) return false;
		for (const auto* texture : *data->Textures)
			if (texture->Status==ImTextureStatus_WantCreate || texture->Status==ImTextureStatus_WantUpdates) return false;
		const auto* texture=ImGui::GetIO().Fonts->TexData;
		return texture && texture->Status==ImTextureStatus_OK && texture->GetTexID()!=ImTextureID_Invalid;
	}

	void CompleteHomeEntryLayout(HomeMotionState& state)
	{
		auto& prep=state.preparation;
		prep.layoutReady=false;
		if (!prep.managed || !prep.fullFrame || !state.EntryActive()) return;
		// ImGui Begin 使用上帧测量：多层 AutoResize/换行的当前高度必须先传播到 native ContentSize。
		// 仅检查本帧真实提交的正文子树；按 CalcWindowContentSizes 的 explicit/保留/取整规则比较。
		auto ready=[&](auto&& self, ImGuiWindow* window) -> bool {
			const bool preserve=(window->Collapsed && window->AutoFitFramesX<=0 && window->AutoFitFramesY<=0)
				|| (window->Hidden && window->HiddenFramesCannotSkipItems==0 && window->HiddenFramesCanSkipItems>0);
			if (!preserve) for (int axis=0;axis<2;++axis)
			{
				const float explicitSize=window->ContentSizeExplicit[axis];
				const float measured=explicitSize!=0 ? explicitSize
					: ImTrunc64(window->DC.CursorMaxPos[axis]-window->DC.CursorStartPos[axis]);
				const float ideal=explicitSize!=0 ? explicitSize
					: ImTrunc64((std::max)(window->DC.CursorMaxPos[axis],window->DC.IdealMaxPos[axis])-window->DC.CursorStartPos[axis]);
				if (window->ContentSize[axis]!=measured || window->ContentSizeIdeal[axis]!=ideal) return false;
			}
			for (auto* child : window->DC.ChildWindows)
				if (child->Active && child->LastFrameActive==GImGui->FrameCount && !self(self,child)) return false;
			return true;
		};
		auto* content=ImGui::GetCurrentWindow();
		prep.layoutReady=!content->SkipItems && ready(ready,content);
	}

	void CompleteHomeEntryFrame(HomeMotionState& state, bool submitted, bool texturesReady, bool captured)
	{
		auto& prep=state.preparation;
		if (!prep.managed || prep.submitted==prep.frame || !state.EntryActive()) return;
		prep.submitted=prep.frame;
		if (!submitted || !texturesReady)
		{
			// 失败/遮挡帧不消费动画时间；下一次可呈现帧重新确认字体和资源事务。
			state.entryElapsed=prep.previousElapsed; prep.ready=false; prep.layoutReady=false; prep.warmFrames=0;
			prep.revealFrames=0; prep.revealReady=false; prep.restoreFrames=0; prep.restoreReady=prep.restorePending=false;
			if (++prep.attempts>=6) FinishHomeEntry(state);
			return;
		}
		if (!prep.fullFrame) return;
		if (!prep.ready)
		{
			// 最多六次准备；故障继续走原 Retry/DeviceLost，不能在加载页永久等待。
			if (++prep.attempts>=6) { FinishHomeEntry(state); return; }
			if (submitted && texturesReady) { if (++prep.warmFrames>=2) prep.ready=true; }
			else prep.warmFrames=0;
		}
		if (prep.revealPending && !prep.revealReady)
		{
			++prep.revealFrames;
			if (captured && prep.layoutReady && prep.revealFrames>=2) prep.revealReady=true;
			else if (prep.revealFrames>=6) FinishHomeEntry(state);
		}
		if (prep.restorePending && !prep.restoreReady)
		{
			++prep.restoreFrames;
			if (prep.layoutReady && prep.restoreFrames>=2) prep.restoreReady=true;
			else if (prep.restoreFrames>=6) FinishHomeEntry(state);
		}
	}

	void FilterHomeEntryDrawData(const HomeMotionState& state, ImDrawData* data,
		ImDrawList* root, ImDrawList* titleBar, ImDrawList* foreground)
	{
		if (!data || !state.preparation.managed || !state.preparation.ready || !state.EntryActive()
			|| (!state.preparation.skipBackground && !state.preparation.revealReady)) return;
		// 捕获帧保留完整背景；复用/终态暖帧仅提交标题栏与单份前景，根填充不可覆盖缓存像素。
		int count=0; data->TotalVtxCount=data->TotalIdxCount=0;
		for (auto* list : data->CmdLists)
		{
			if (list!=root && list!=titleBar && list!=foreground) continue;
			if (list==root)
				for (auto& command : list->CmdBuffer) command.ClipRect.w=(std::min)(command.ClipRect.w,state.clientContent.y);
			data->CmdLists[count++]=list;
			data->TotalVtxCount+=list->VtxBuffer.Size; data->TotalIdxCount+=list->IdxBuffer.Size;
		}
		data->CmdLists.resize(count); data->CmdListsCount=count;
	}

	bool ConsumeSettingHomeReset(std::uint64_t resetSerial, std::uint64_t& consumed,
		NavigationDestination& destination, NavigationState& navigation, HomeMotionState& motion)
	{
		if (resetSerial==consumed) return false;
		consumed=resetSerial; destination={}; FinishHomeEntry(motion);
		navigation.overlayOpen=navigation.flyoutOpen=navigation.restoreParentFocus=false;
		navigation.mouseReleaseBarrier=false; navigation.generalExpanded=false;
		navigation.childFocusId=navigation.childWindowItemId=0; navigation.inlineChildFocused=false;
		navigation.scrollSelectedGeneral=false;
		navigation.contentPage=0; navigation.contentSection=GeneralSection::Basic;
		navigation.contentReveal.Reset(1); navigation.flyoutReveal.Reset(0); navigation.groupReveal.Reset(0);
		navigation.ClearIndicator(); motion.buttons={};
		ImGui::ClearActiveID(); GImGui->MovingWindow=nullptr;
		// ClosePopupToLevel 要求栈非空；从首层弹窗保存的导航窗回溯，不能重新聚焦已关闭的弹窗。
		auto* focus=GImGui->NavWindow;
		if (!GImGui->OpenPopupStack.empty() && GImGui->OpenPopupStack[0].RestoreNavWindow)
			focus=GImGui->OpenPopupStack[0].RestoreNavWindow;
		while (focus && (focus->RootWindow->Flags & ImGuiWindowFlags_Popup)) focus=focus->RootWindow->ParentWindow;
		auto* root=focus ? focus->RootWindow : nullptr;
		if (!GImGui->OpenPopupStack.empty()) ImGui::ClosePopupToLevel(0,false);
		ImGui::NavMoveRequestCancel();
		// 无缺帧快速重开时根窗不 Appearing；清旧 ID 后保留正常 Tab 导航入口。
		if (root) { root->NavLastIds[0]=root->NavLastIds[1]=0; root->NavLastChildNavWindow=nullptr; }
		ImGui::FocusWindow(root);
		if (root) ImGui::NavInitWindow(root,true);
		return true;
	}

	ImU32 HomeEntryMaskColor(const HomeMotionState& state)
	{
		const float opacity=state.EntryActive() ? ResolveHomeEntryPose(state).opacity : 0.0F;
		return (AppBase & ~IM_COL32_A_MASK)|(static_cast<ImU32>(std::round(opacity*255))<<IM_COL32_A_SHIFT);
	}

	void RenderHomeEntryOverlay(const HomeMotionState& state)
	{
		if (!state.EntryActive()) return;
		const auto pose=ResolveHomeEntryPose(state); auto* draw=ImGui::GetForegroundDrawList();
		const auto bounds=state.clientContent;
		draw->PushClipRect({bounds.x,bounds.y},{bounds.z,bounds.w},false);
		if (pose.opacity>0)
			draw->AddRectFilled({bounds.x,bounds.y},{bounds.z,bounds.w},HomeEntryMaskColor(state));
		const int firstVertex=draw->VtxBuffer.Size;
		HomeBrand::DrawWriting(draw,pose.origin,pose.width,IsDarkMode(),pose.writing);
		if (state.fadeOnly)
			for (int i=firstVertex;i<draw->VtxBuffer.Size;++i)
			{
				auto& color=draw->VtxBuffer[i].col;
				const auto alpha=static_cast<ImU32>(std::round(((color>>IM_COL32_A_SHIFT)&0xff)*pose.opacity));
				color=(color & ~IM_COL32_A_MASK)|(alpha<<IM_COL32_A_SHIFT);
			}
		draw->PopClipRect();
	}

	bool CancelHomeEntryOnInput(HomeMotionState& state)
	{
		if (!state.EntryActive() && state.entry!=HomeMotionState::Entry::Pending) return false;
		const auto& io=ImGui::GetIO();
		bool cancel=ImGui::IsAnyMouseDown() || io.MouseWheel!=0 || io.MouseWheelH!=0 || io.InputQueueCharacters.Size>0;
		// Show 前已按住的导航键也要收束，不能等重复事件在覆盖层下改变焦点。
		for (int key=ImGuiKey_NamedKey_BEGIN;key<ImGuiKey_NamedKey_END;++key)
			cancel|=ImGui::IsKeyDown(static_cast<ImGuiKey>(key));
		if (cancel) FinishHomeEntry(state);
		// 返回本帧屏障：即使取消也不能把同一个按下穿透为链接/导航点击。
		return cancel;
	}

	namespace
	{
		void ResolveHomeEntryLayout(HomeMotionState& state)
		{
			if (!state.viewportReady || (!state.EntryActive() && state.entry!=HomeMotionState::Entry::Pending)) return;
			const float dpi=Pixels(1);
			const std::array<float,17> key{state.clientContent.x,state.clientContent.y,state.clientContent.z,state.clientContent.w,
				state.contentClip.x,state.contentClip.y,state.contentClip.z,state.contentClip.w,
				state.heroOrigin.x,state.heroOrigin.y,state.heroSize.x,state.heroSize.y,
				state.brandOrigin.x,state.brandOrigin.y,state.brandWidth,HomeBrand::Height(state.brandWidth),dpi};
			if (state.entryLayoutValidated && state.entryLayoutKey==key)
			{
				if (!state.preparation.managed) state.entryElapsed=(std::min)(state.EntryTotal(),state.entryElapsed+state.delta);
				if (state.entryElapsed>=state.EntryTotal()) FinishHomeEntry(state);
				return;
			}
			state.entryLayoutKey=key; state.entryLayoutValidated=true;
			state.entryStartWidth=(std::min)(580*dpi,(state.clientContent.z-state.clientContent.x)*.7F);
			state.entryStart={(state.clientContent.x+state.clientContent.z-state.entryStartWidth)*.5F,
				(state.clientContent.y+state.clientContent.w-HomeBrand::Height(state.entryStartWidth))*.5F};
			auto contains=[](ImVec4 rect,ImVec2 origin,ImVec2 size,float margin) {
				return origin.x-margin>=rect.x && origin.y-margin>=rect.y
					&& origin.x+size.x+margin<=rect.z && origin.y+size.y+margin<=rect.w;
			};
			bool fits=state.fadeOnly || contains(state.contentClip,state.heroOrigin,state.heroSize,0);
			// 在同一真实布局上验证完整曲线包络；首帧 AutoResize clip 未测量也不影响前景覆盖。
			HomeMotionState probe; probe.entry=HomeMotionState::Entry::Playing;
			probe.fadeOnly=state.fadeOnly;
			probe.brandOrigin=state.brandOrigin; probe.brandWidth=state.brandWidth;
			probe.entryStart=state.entryStart; probe.entryStartWidth=state.entryStartWidth;
			for (int i=0;fits && i<=64;++i)
			{
				probe.entryElapsed=HomeEntryTiming::DockStart+(HomeEntryTiming::Dock+HomeEntryTiming::Settle)*i/64;
				const auto pose=ResolveHomeEntryPose(probe);
				fits=contains(state.clientContent,pose.origin,{pose.width,HomeBrand::Height(pose.width)},14*dpi);
			}
			if (!fits) { state.entry=HomeMotionState::Entry::Skipped; state.x=state.y=0; return; }
			if (state.entry==HomeMotionState::Entry::Pending)
			{
				state.entry=HomeMotionState::Entry::Playing; state.entryElapsed=0; ++state.entries;
			}
			else if (!state.preparation.managed) state.entryElapsed=(std::min)(state.EntryTotal(),state.entryElapsed+state.delta);
			if (state.entryElapsed>=state.EntryTotal()) FinishHomeEntry(state);
		}
	}

	void ApplyContentAlpha(ImGuiWindow* window, float alpha)
	{
		// 旧页整棵内容（含自绘文字/图标和子卡片）作为一块淡出；块内重叠控件不各自透出下层。
		ApplyGroupOpacity(window, alpha);
	}

	CicdAction RenderCicdDetails(const CicdContent& content)
	{
		CicdAction action = CicdAction::None;
		if (NavigationRow("##cicd-run-link", "CI/CD", content.url.c_str(), "\ue71b"))
			action = CicdAction::Run;
		if (NavigationRow("##cicd-repository-link", IA(I18nKey.SettingsUI.CICD.Repository).c_str(),
			content.repoUrl.c_str(), "\ue8a5"))
			action = CicdAction::Repository;
		PageContentStart();
		std::string details;
		details += IA(I18nKey.SettingsUI.CICD.Branch) + ": " + content.branch + "\n";
		details += IA(I18nKey.SettingsUI.CICD.Submitter) + ": " + content.submitter + "\n";
		details += IA(I18nKey.SettingsUI.CICD.BuildTime) + ": " + content.buildTime + "\n\n";
		details += IA(I18nKey.SettingsUI.CICD.BuildSystem) + ": " + content.buildOS + "\n";
		details += IA(I18nKey.SettingsUI.CICD.BuildSystemVersion) + ": " + content.buildOSVersion + "\n";
		details += IA(I18nKey.SettingsUI.CICD.RunnerImageSystem) + ": " + content.buildRunnerImageOS + "\n";
		details += IA(I18nKey.SettingsUI.CICD.RunnerImageVersion) + ": " + content.buildRunnerImageVersion + "\n\n";
		details += IA(I18nKey.SettingsUI.CICD.MSBuildVersion) + "\n" + content.msBuildVersion;
		const float width = ImGui::GetContentRegionAvail().x;
		const float padding = Pixels(ImFluent::GetStyle().CardPadding);
		const float textHeight = TextHeight(details.c_str(), (std::max)(1.0F, width - padding * 2.0F));
		const float height = std::ceil(textHeight + padding * 2.0F);
		// 当前帧按真实完整元数据定高，长工具链输出交给外层页面滚动，不截成内层小视口。
		ImGui::SetNextWindowSizeConstraints({ 0.0F, height }, { FLT_MAX, height });
		ImGui::SetNextWindowContentSize({ 0.0F, textHeight });
		if (BeginCard("##cicd-details", { width, height })) Text(details.c_str());
		EndCard();
		return action;
	}

	DrawingSubpage RenderDrawingEraserEntries(DrawingSubpage current)
	{
		const auto& eraser = I18nKey.SettingsUI.Draw.Eraser;
		if (NavigationRow("##speed-eraser-entry", IA(eraser.Kind.Speed).c_str(),
			IA(eraser.SpeedEraser.EntryDescription).c_str(), "\ued62"))
			current = DrawingSubpage::SpeedEraser;
		if (NavigationRow("##palm-eraser-entry", IA(eraser.PalmEraser.N).c_str(),
			IA(eraser.PalmEraser.EntryDescription).c_str(), "\ue7c9"))
			current = DrawingSubpage::PalmEraser;
		return current;
	}

	namespace
	{
		// 与 Draw3 实际光标共用尺寸/描边常量；预览是近似，不替代真实绘制验收。
		std::array<ImVec2, 5> InsetCursorPreviewPolygon(const ImVec2 points[5], float distance)
		{
			const auto cross = [](ImVec2 left, ImVec2 right) { return left.x * right.y - left.y * right.x; };
			float area = 0.0F;
			for (int index = 0; index < 5; ++index) area += cross(points[index], points[(index + 1) % 5]);
			const float direction = area >= 0.0F ? 1.0F : -1.0F;
			std::array<ImVec2, 5> result{};
			for (int index = 0; index < 5; ++index)
			{
				const ImVec2 previous = points[(index + 4) % 5], current = points[index], next = points[(index + 1) % 5];
				const ImVec2 edges[2]{ { current.x - previous.x, current.y - previous.y },
					{ next.x - current.x, next.y - current.y } };
				ImVec2 normals[2]{};
				for (int edge = 0; edge < 2; ++edge)
				{
					const float length = (std::max)(0.0001F, std::sqrt(edges[edge].x * edges[edge].x + edges[edge].y * edges[edge].y));
					normals[edge] = { -edges[edge].y / length * direction, edges[edge].x / length * direction };
				}
				const ImVec2 origin{ previous.x + normals[0].x * distance, previous.y + normals[0].y * distance };
				const ImVec2 other{ current.x + normals[1].x * distance, current.y + normals[1].y * distance };
				const float denominator = cross(edges[0], edges[1]);
				if (std::abs(denominator) < 0.0001F)
					result[index] = { current.x + (normals[0].x + normals[1].x) * 0.5F * distance,
						current.y + (normals[0].y + normals[1].y) * 0.5F * distance };
				else
				{
					const float t = cross({ other.x - origin.x, other.y - origin.y }, edges[1]) / denominator;
					result[index] = { origin.x + edges[0].x * t, origin.y + edges[0].y * t };
				}
			}
			return result;
		}

		void DrawCursorPreview(const LayoutRect& rect, Inkeys::Drawing::Draw3::CursorStyle style, float opacity)
		{
			namespace Cursor = Inkeys::Drawing::Draw3;
			ImDrawList* draw = ImGui::GetWindowDrawList();
			const ImVec2 min(rect.left, rect.top), max(rect.right, rect.bottom);
			const float corner = Pixels(4.0F);
			const bool dark = IsDarkMode();
			draw->AddRectFilled(min, max, dark ? IM_COL32(52, 54, 60, 255) : Card, corner);
			draw->AddRect(min, max, dark ? IM_COL32(255, 255, 255, 36) : Stroke, corner);
			const ImVec2 center((min.x + max.x) * 0.5F, (min.y + max.y) * 0.5F);
			const int opacityByte = static_cast<int>(std::round(std::clamp(opacity, 0.25F, 1.0F) * 255.0F));
			const ImU32 ink = IM_COL32(255, 15, 0, opacityByte);
			const bool highlight = Cursor::IsHighlightedCursorStyle(style);
			const float unit = Pixels(1.0F);
			const float edgeWidth = style == Cursor::CursorStyle::HighlightDot
				? Cursor::CursorHighlightDotEdgeWidthPx(10.0F * unit, unit)
				: (std::max)(1.0F, (highlight ? Cursor::kCursorHighlightEdgeDip : Cursor::kCursorPlainEdgeDip) * unit);
			const ImU32 edge = highlight ? IM_COL32(245, 245, 245, 255) : IM_COL32(184, 184, 184, opacityByte);
			switch (style)
			{
			case Cursor::CursorStyle::Arrow:
			{
				// 与 GPU 共用 12x19 归一化参考轮廓，按校准后的 DIP 尺寸显示。
				const ImVec2 origin(center.x - Cursor::kCursorArrowWidthDip * 0.5F * unit,
					center.y - Cursor::kCursorArrowHeightDip * 0.5F * unit);
				constexpr float arrow[7][2]{ {0.0F, 0.0F}, {0.0F, 16.5F}, {3.8F, 12.8F}, {6.6F, 19.0F},
					{9.1F, 17.9F}, {6.4F, 11.9F}, {11.4F, 11.9F} };
				ImVec2 points[7];
				for (int index = 0; index < 7; ++index)
					points[index] = { origin.x + arrow[index][0] / 12.0F * Cursor::kCursorArrowWidthDip * unit,
						origin.y + arrow[index][1] / 19.0F * Cursor::kCursorArrowHeightDip * unit };
				const ImU32 white = IM_COL32(255, 255, 255, 255);
				const ImVec2 head1[3]{ points[0], points[1], points[2] };
				const ImVec2 head2[3]{ points[0], points[2], points[5] };
				const ImVec2 head3[3]{ points[0], points[5], points[6] };
				const ImVec2 tail[4]{ points[2], points[3], points[4], points[5] };
				draw->AddConvexPolyFilled(head1, 3, white);
				draw->AddConvexPolyFilled(head2, 3, white);
				draw->AddConvexPolyFilled(head3, 3, white);
				draw->AddConvexPolyFilled(tail, 4, white);
				draw->AddPolyline(points, 7, IM_COL32(0, 0, 0, 255), ImDrawFlags_Closed, Cursor::CursorArrowOutlineWidthPx(unit));
				break;
			}
			case Cursor::CursorStyle::None:
			{
				const float radius = 7.0F * unit;
				const ImU32 dim = IM_COL32(200, 200, 200, 120);
				draw->AddCircle(center, radius, dim, 0, (std::max)(1.0F, unit));
				draw->AddLine({ center.x - radius * 0.7F, center.y + radius * 0.7F },
					{ center.x + radius * 0.7F, center.y - radius * 0.7F }, dim, (std::max)(1.0F, unit));
				break;
			}
			case Cursor::CursorStyle::Dot:
			case Cursor::CursorStyle::HighlightDot:
			{
				// 小圆点沿用实际光标的圆形预览（随笔宽，这里取常见笔宽）。
				const float radius = 5.0F * unit;
				if (highlight)
				{
					draw->AddCircleFilled(center, radius, IM_COL32(0, 0, 0, 255));
					draw->AddCircleFilled(center, radius - Cursor::kCursorHighlightOuterEdgePx, edge);
					draw->AddCircleFilled(center, radius - Cursor::kCursorHighlightOuterEdgePx - edgeWidth, ink);
				}
				else
				{
					draw->AddCircleFilled(center, radius, ink);
					draw->AddCircle(center, radius, edge, 0, edgeWidth);
				}
				break;
			}
			case Cursor::CursorStyle::Pen:
			case Cursor::CursorStyle::HighlightPen:
			{
				// 左上尖端为热点，笔身向右下；热点放在预览块左上区域以完整容纳 24 DIP 笔形。
				const ImVec2 tip(center.x - 12.0F * unit, center.y - 12.0F * unit);
				constexpr float axis = 0.70710678F;
				const auto at = [&](float along, float across)
				{
					return ImVec2(tip.x + (along * axis + across * axis) * unit, tip.y + (along * axis - across * axis) * unit);
				};
				const ImVec2 points[5]{ at(0, 0), at(6, 3.4F), at(30, 3.4F), at(30, -3.4F), at(6, -3.4F) };
				if (highlight)
				{
					const auto light = InsetCursorPreviewPolygon(points, Cursor::kCursorHighlightOuterEdgePx);
					const auto fill = InsetCursorPreviewPolygon(points, Cursor::kCursorHighlightOuterEdgePx + edgeWidth);
					draw->AddConvexPolyFilled(points, 5, IM_COL32(0, 0, 0, 255));
					draw->AddConvexPolyFilled(light.data(), 5, edge);
					draw->AddConvexPolyFilled(fill.data(), 5, ink);
				}
				else
				{
					draw->AddConvexPolyFilled(points, 5, ink);
					draw->AddPolyline(points, 5, edge, ImDrawFlags_Closed, edgeWidth);
				}
				break;
			}
			}
		}
	}

	CursorPageEvents RenderCursorPage(CursorPageDraft& draft, const EraserCursorPreviewPresenter& preview)
	{
		namespace Cursor = Inkeys::Drawing::Draw3;
		using Cursor::CursorDevice;
		using Cursor::CursorStyle;
		CursorPageEvents events;
		const auto& cursor = I18nKey.SettingsUI.Draw.Cursor;
		events.back = IconButton("##cursor-back", "\ue72b");
		PageHeader(IA(cursor.N).c_str());
		PageContentStart();

		const std::string styleNames[6]{ IA(cursor.Style.None), IA(cursor.Style.Arrow), IA(cursor.Style.Pen),
			IA(cursor.Style.HighlightPen), IA(cursor.Style.Dot), IA(cursor.Style.HighlightDot) };
		// 下拉项顺序即枚举顺序；触摸从“无光标”开始，其余设备从“鼠标”开始。
		const auto itemsFor = [&](CursorDevice device)
		{
			std::vector<std::string> items;
			for (int value = device == CursorDevice::Touch ? 0 : 1; value <= 5; ++value) items.push_back(styleNames[value]);
			return items;
		};
		const auto opacityIndex = [](CursorStyle style)
		{
			return style == CursorStyle::HighlightPen ? 1 : style == CursorStyle::Dot ? 2
				: style == CursorStyle::HighlightDot ? 3 : 0;
		};
		const auto row = [&](const char* id, const char* title, const char* description, const char* glyph,
			CursorDevice device, int& index)
		{
			index = std::clamp(index, 0, Cursor::CursorStyleItemCount(device) - 1);
			return ComboPreviewRow(id, title, description, glyph, index, itemsFor(device),
				[&](const LayoutRect& area) {
					const auto style = Cursor::CursorStyleFromItemIndex(device, index);
					DrawCursorPreview(area, style, draft.opacity[opacityIndex(style)]);
				});
		};

		SectionHeader(IA(cursor.Devices).c_str());
		events.mouse = row("##cursor-mouse", IA(cursor.Mouse.N).c_str(), IA(cursor.Mouse.E).c_str(), "\ue962",
			CursorDevice::Mouse, draft.mouse);
		events.touch = row("##cursor-touch", IA(cursor.Touch.N).c_str(), IA(cursor.Touch.E).c_str(), "\ue7c9",
			CursorDevice::Touch, draft.touch);
		events.screenPen = row("##cursor-screen-pen", IA(cursor.ScreenPen.N).c_str(), IA(cursor.ScreenPen.E).c_str(), "\uedc6",
			CursorDevice::ScreenPen, draft.screenPen);
		events.tablet = row("##cursor-tablet", IA(cursor.Tablet.N).c_str(), IA(cursor.Tablet.E).c_str(), "\ue70f",
			CursorDevice::Tablet, draft.tablet);

		// 橡皮光标为独立分类，位于“按设备设置”与“数位板识别”之间。
		SectionHeader(IA(cursor.Eraser.N).c_str());
		{
			const auto& eraser = cursor.Eraser;
			draft.eraserShape = draft.eraserShape >= 0 && draft.eraserShape <= 2 ? draft.eraserShape : 0;
			draft.eraserPattern = draft.eraserPattern >= 0 && draft.eraserPattern <= 2 ? draft.eraserPattern : 0;
			draft.eraserTheme = draft.eraserTheme == 1 ? 1 : 0;
			std::vector<PreviewSelectorGroup> groups(2);
			groups[0].selectedIndex = &draft.eraserShape; groups[0].changed = &events.eraserShape;
			groups[1].selectedIndex = &draft.eraserPattern; groups[1].changed = &events.eraserPattern;
			groups[1].separatorBefore = true;
			const char* shapeLabels[]{ eraser.Circle, eraser.Rectangle, eraser.Square };
			const char* patternLabels[]{ eraser.Grips, eraser.Inkeys, eraser.Sparkle };
			for (int index = 0; index < 3; ++index)
			{
				groups[0].items.push_back({ IA(shapeLabels[index]), [&, index](const LayoutRect& area) {
					if (preview) preview(area, { static_cast<Cursor::EraserShape>(index),
						static_cast<Cursor::EraserCursorPattern>(draft.eraserPattern), static_cast<Cursor::EraserCursorTheme>(draft.eraserTheme) }); } });
				groups[1].items.push_back({ IA(patternLabels[index]), [&, index](const LayoutRect& area) {
					if (preview) preview(area, { static_cast<Cursor::EraserShape>(draft.eraserShape),
						static_cast<Cursor::EraserCursorPattern>(index), static_cast<Cursor::EraserCursorTheme>(draft.eraserTheme) }); } });
			}
			const auto inlineContent = InlinePreviewSelector("##eraser-cursor-options", std::move(groups));
			const std::vector<std::string> themes{ IA(eraser.Light), IA(eraser.Dark) };
			const float comboWidth = (std::max)(160.0F, (std::max)(ControlTextWidth(themes[0].c_str()),
				ControlTextWidth(themes[1].c_str())) / Pixels(1) + 48.0F);
			SettingRow("##eraser-cursor", IA(eraser.Appearance).c_str(), IA(eraser.E).c_str(), nullptr,
				comboWidth, ControlHeight, [&](const LayoutRect&) {
					events.eraserTheme = ComboBox("##select", &draft.eraserTheme, themes);
				}, Card, Stroke, 0, IM_COL32_WHITE, false, nullptr, true, &inlineContent);
		}

		SectionHeader(IA(cursor.PenDevices).c_str());
		ImFluent::BeginDisabled(!draft.injectedAvailable);
		events.injectedMouse = ToggleRow("##cursor-injected-mouse", IA(cursor.InjectedMouse.N).c_str(),
			IA(cursor.InjectedMouse.E).c_str(), "\ue946", draft.injectedMouse,
			draft.injectedAvailable ? nullptr : IA(cursor.InjectedMouse.Unavailable).c_str());
		ImFluent::EndDisabled();
		{
			// 说明以分号分成两行：前半为标题行，后半为说明行；无分号时整句作标题。
			const std::string note = IA(cursor.Note);
			size_t split = std::string::npos, skip = 1;
			// 全角分号以 UTF-8 字节转义书写，不受源文件编码转换影响。
			for (const char* separator : { "\xEF\xBC\x9B", ";" })
			{
				const size_t found = note.find(separator);
				if (found != std::string::npos && (split == std::string::npos || found < split))
				{ split = found; skip = std::strlen(separator); }
			}
			const auto trim = [](std::string text)
			{
				const size_t first = text.find_first_not_of(" 	");
				const size_t last = text.find_last_not_of(" 	");
				return first == std::string::npos ? std::string() : text.substr(first, last - first + 1);
			};
			const std::string title = trim(split == std::string::npos ? note : note.substr(0, split));
			const std::string detail = split == std::string::npos ? std::string() : trim(note.substr(split + skip));
			Notice("##cursor-note", title.c_str(), detail.c_str(), ImFluentInfoSeverity_Informational);
		}

		SectionHeader(IA(cursor.OpacitySection).c_str());
		const float opacityActionWidth = (ImGui::GetContentRegionAvail().x >= Pixels(740.0F) ? 240.0F : 196.0F)
			+ SliderResetExtraWidthDip;
		const std::string resetLabel = IA(I18nKey.SettingsUI.Regular.Appearance.ResetDefault);
		const char* opacityIds[4]{ "##cursor-pen-opacity", "##cursor-highlight-pen-opacity",
			"##cursor-dot-opacity", "##cursor-highlight-dot-opacity" };
		const char* opacityTitles[4]{ cursor.PenOpacity, cursor.HighlightPenFillOpacity,
			cursor.DotOpacity, cursor.HighlightDotFillOpacity };
		constexpr CursorStyle opacityStyles[4]{ CursorStyle::Pen, CursorStyle::HighlightPen,
			CursorStyle::Dot, CursorStyle::HighlightDot };
		for (int index = 0; index < 4; ++index)
		{
			SettingRow(opacityIds[index], IA(opacityTitles[index]).c_str(), nullptr, index < 2 ? "\ue70f" : "\ue7c9",
				opacityActionWidth, ControlHeight, [&](const LayoutRect& bounds) {
					events.opacityChanged[index] = SliderResetAction(bounds, draft.opacity[index], 0.25F, 1.0F,
						events.opacityActive[index], events.opacityReset[index], resetLabel.c_str());
				});
			if (events.opacityChanged[index])
				draft.opacity[index] = std::round(draft.opacity[index] * 100.0F) / 100.0F;
			if (events.opacityReset[index])
			{
				const float value = Cursor::DefaultCursorOpacityPercent(opacityStyles[index]) / 100.0F;
				events.opacityChanged[index] |= draft.opacity[index] != value;
				draft.opacity[index] = value;
			}
		}
		SectionHeader(IA(cursor.FineControl).c_str());
		const auto& hide = cursor.DotHideOnContact;
		draft.dotHideOnContact = static_cast<int>(Cursor::RestoreDotCursorHideMode(draft.dotHideOnContact));
		events.dotHideOnContact = ComboRow("##cursor-dot-hide", IA(hide.N).c_str(), IA(hide.E).c_str(), "\ue7b3",
			draft.dotHideOnContact, { IA(hide.Off), IA(hide.ScreenPen), IA(hide.Tablet), IA(hide.Both) });
		return events;
	}

	PalmEraserEvents RenderPalmEraser(PalmEraserDraft& draft, const EraserCursorPreviewPresenter& preview)
	{
		PalmEraserEvents events;
		const auto& palm = I18nKey.SettingsUI.Draw.Eraser.PalmEraser;
		events.back = IconButton("##palm-eraser-back", "\ue72b");
		PageHeader(IA(palm.N).c_str());
		PageContentStart();
		const float width = ImGui::GetContentRegionAvail().x;
		const float padding = Pixels(ImFluent::GetStyle().CardPadding);
		const float innerWidth = (std::max)(1.0F, width - padding * 2.0F);
		if (ConditionalBlock block{"##palm-size-visibility", draft.physicalSizeWarning}; block)
		{
			Notice("##palm-eraser-size-warning", IA(palm.SizeWarningTitle).c_str(),
				IA(palm.SizeWarningDescription).c_str(), ImFluentInfoSeverity_Warning);
		}
		SectionHeader(IA(I18nKey.SettingsUI.Draw.Eraser.SpeedEraser.BasicSettings).c_str());
		events.enabledChanged = ToggleRow("##palm-eraser-enabled", IA(palm.Enabled).c_str(),
			nullptr, "\ue7c9", draft.enabled);
		constexpr std::array<int, 4> windows{ 100, 150, 250, 500 };
		const auto found = std::find(windows.begin(), windows.end(), draft.recognitionWindowMs);
		int windowIndex = found == windows.end() ? 1 : static_cast<int>(found - windows.begin());
		events.recognitionWindowChanged = ComboRow("##palm-eraser-window", IA(palm.RecognitionWindow).c_str(),
			IA(palm.RecognitionWindowDescription).c_str(), "\ue823", windowIndex,
			{ "100ms", "150ms" + IA(I18nKey.SettingsUI.Design.RecommendedSuffix), "250ms", "500ms" });
		if (events.recognitionWindowChanged) draft.recognitionWindowMs = windows[windowIndex];

		const std::array<const char*, 4> ratios{ "1:1", "4:3", "2:1", "1:2" };
		constexpr std::array<double, 4> aspects{ 1, 4.0 / 3.0, 2, .5 };
		// 比例只属于当前页面；切换比例不改变手掌阈值或触发配置保存。
		auto* pageStorage = ImGui::GetStateStorage();
		const auto ratioStateId = ImGui::GetID("##palm-eraser-preview-ratio-state");
		int ratioIndex = std::clamp(pageStorage->GetInt(ratioStateId, 0), 0, 3);
		const double noteArea = draft.referenceAreaPixels * draft.thresholdPercent / 100.0;
		std::string thresholdNote;
		if (std::isfinite(noteArea) && noteArea > 0)
		{
			const double widthPx = std::sqrt(noteArea * aspects[ratioIndex]), heightPx = std::sqrt(noteArea / aspects[ratioIndex]);
			const double areaCm2 = draft.referenceAreaCm2 * draft.thresholdPercent / 100.0;
			if (!draft.previewEstimated && std::isfinite(areaCm2) && areaCm2 > 0)
			{
				const double widthCm = std::sqrt(areaCm2 * aspects[ratioIndex]), heightCm = std::sqrt(areaCm2 / aspects[ratioIndex]);
				thresholdNote = std::vformat(IA(palm.ThresholdSizePhysical), std::make_format_args(widthCm, heightCm, widthPx, heightPx));
			}
			else thresholdNote = std::vformat(IA(palm.ThresholdSizePixels), std::make_format_args(widthPx, heightPx));
		}

		const auto reset = IA(I18nKey.Operate.Reset);
		const float resetWidth = ButtonWidth(reset.c_str()), numberWidth = 40.0F;
		const float actionWidth = numberWidth + 12.0F + 120.0F + ControlSpacing + resetWidth;
		const auto measure = MeasureRow(width / Pixels(1), actionWidth, ControlHeight);
		const bool resetBelow = measure.actionWidth < numberWidth + 12.0F + 24.0F + ControlSpacing + resetWidth;
		// 滑块和重置属于同一条目；极窄时只在条目内部换行，不增加独立重置卡片。
		SettingRow("##palm-eraser-threshold", IA(palm.Threshold).c_str(), IA(palm.ThresholdDescription).c_str(),
			"\ue7c9", actionWidth, resetBelow ? ControlHeight * 2 + ControlSpacing : ControlHeight,
			[&](const LayoutRect& bounds) {
				char percentage[24]; ImFormatString(percentage, IM_ARRAYSIZE(percentage), "%d%%", draft.thresholdPercent);
				TextAt(percentage, { bounds.left, bounds.top + Pixels(6) }, Pixels(numberWidth));
				const float sliderLeft = bounds.left + Pixels(numberWidth + 12);
				const float buttonLeft = bounds.right - Pixels(resetWidth);
				ImGui::SetCursorScreenPos({ sliderLeft, bounds.top });
				ImGui::SetNextItemWidth((std::max)(1.0F, (resetBelow ? bounds.right : buttonLeft - Pixels(ControlSpacing)) - sliderLeft));
				events.thresholdChanged = SliderInt("##slider", &draft.thresholdPercent, 1, 400, "%d%%", ImGuiSliderFlags_AlwaysClamp);
				ImGui::SetCursorScreenPos({ buttonLeft, bounds.top + (resetBelow ? Pixels(ControlHeight + ControlSpacing) : 0) });
				if (Button((reset + "###reset").c_str(), { Pixels(resetWidth), Pixels(ControlHeight) }))
				{
					events.thresholdReset = true;
					events.thresholdChanged |= draft.thresholdPercent != 100;
					draft.thresholdPercent = 100;
				}
			}, Card, Stroke, 0, IM_COL32_WHITE, false, thresholdNote.c_str());

		const double area = draft.referenceAreaPixels * draft.thresholdPercent / 100.0;
		const bool available = std::isfinite(area) && area > 0;
		const auto previewTitle = IA(palm.PreviewTitle);
		const auto previewDescription = IA(!available ? palm.PreviewUnavailable
			: draft.previewEstimated ? palm.PreviewEstimated : palm.PreviewDescription);
		const float titleHeight = TextHeight(previewTitle.c_str(), innerWidth, ImFluentTextStyle_BodyStrong);
		const float descriptionHeight = TextHeight(previewDescription.c_str(), innerWidth, ImFluentTextStyle_Caption);
		const float headingHeight = titleHeight + Pixels(8) + descriptionHeight;
		const ImVec2 size = available ? ImVec2(static_cast<float>(std::sqrt(area * aspects[ratioIndex])),
			static_cast<float>(std::sqrt(area / aspects[ratioIndex]))) : ImVec2();
		const float comboWidth = Pixels(112), inset = 2.0F;
		// 图形直接使用绘制输出像素；下拉悬浮于左上角，不挤占或换行图形。
		const float canvasWidth = (std::max)(size.x + inset * 2, comboWidth);
		const float canvasHeight = (std::max)(size.y + inset * 2, Pixels(ControlHeight));
		const float scrollReserve = canvasWidth > innerWidth ? Pixels(ScrollbarTrackSize + ScrollbarPadding) : 0;
		const float previewHeight = headingHeight + (available ? Pixels(16) + canvasHeight + scrollReserve : 0);
		const float previewCardHeight = std::ceil(previewHeight + padding * 2);
		ImGui::SetNextWindowSizeConstraints({ 0, previewCardHeight }, { FLT_MAX, previewCardHeight });
		ImGui::SetNextWindowContentSize({ 0, previewHeight });
		if (BeginCard("##palm-eraser-preview", { width, previewCardHeight }))
		{
			const auto origin = ImGui::GetCursorScreenPos();
			TextAt(previewTitle.c_str(), origin, innerWidth, ImFluentTextStyle_BodyStrong);
			TextAt(previewDescription.c_str(), { origin.x, origin.y + titleHeight + Pixels(8) }, innerWidth,
				ImFluentTextStyle_Caption, TextSecondary);
			if (available)
			{
				ImGui::SetCursorScreenPos({ origin.x, origin.y + headingHeight + Pixels(16) });
				ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, { 0, 0 });
				// 通用滚动补间只跟随纵向溢出；此处固定横轨，避免开启动画时轨道被压成零。
				ImGui::PushStyleVar(ImGuiStyleVar_ScrollbarSize, Pixels(ScrollbarTrackSize));
				ImGui::PushStyleVar(ImGuiStyleVar_ScrollbarPadding, Pixels(ScrollbarPadding));
				ImGui::SetNextWindowContentSize({ canvasWidth, canvasHeight });
				// 预览沿用外层卡片背景，避免子窗口再次叠加半透明底色。
				if (ImGui::BeginChild("##palm-eraser-preview-shapes", { innerWidth, canvasHeight + scrollReserve },
					ImGuiChildFlags_AlwaysUseWindowPadding, ImGuiWindowFlags_HorizontalScrollbar | ImGuiWindowFlags_NoBackground))
				{
					const auto top = ImGui::GetCursorScreenPos();
					const ImVec2 start(top.x + (std::max)(inset, (innerWidth - size.x) * .5F), top.y + inset);
					ImGui::GetWindowDrawList()->AddRect(start, { start.x + size.x, start.y + size.y },
						Accent, Pixels(8), 0, 2.0F);
					// 后绘制控件，使其可遮挡边框；恢复游标后仅按整个预览包络占位。
					ImGui::SetCursorScreenPos(top); ImGui::SetNextItemWidth(comboWidth);
					if (ComboBox("##palm-eraser-preview-ratio", &ratioIndex, ratios.data(), static_cast<int>(ratios.size())))
						pageStorage->SetInt(ratioStateId, ratioIndex);
					ImGui::SetCursorScreenPos(top);
					ImGui::Dummy({ canvasWidth, canvasHeight });
				}
				ImGui::EndChild(); ImGui::PopStyleVar(3);
			}
			// 完整高度始终进入外层页面滚动；超宽示例只在预览块横向查看。
			ImGui::SetCursorScreenPos(origin); ImGui::Dummy({ innerWidth, previewHeight });
		}
		EndCard();
		SectionHeader(IA(I18nKey.SettingsUI.Draw.Eraser.SpeedEraser.FineControl).c_str());
		// 适用工具归入精细控制，位于边擦边写上方。
		const auto& pen = I18nKey.UI.Bar.DrawAttributes;
		CheckboxGroup("##palm-eraser-tools", IA(palm.EnabledTools).c_str(), "\uedc6", {
			{ IA(pen.SoftPenLabel), &draft.tools[0], &events.toolsChanged[0] },
			{ IA(pen.HardPenLabel), &draft.tools[1], &events.toolsChanged[1] },
			{ IA(pen.HighlighterLabel), &draft.tools[2], &events.toolsChanged[2] },
			{ IA(pen.LaserLabel), &draft.tools[3], &events.toolsChanged[3] },
			{ IA(I18nKey.UI.Centre.Eraser), &draft.tools[4], &events.toolsChanged[4] },
			{ IA(I18nKey.UI.Bar.MainButtons.GeometryLabel), &draft.tools[5], &events.toolsChanged[5] },
			{ IA(pen.BrushLabel), &draft.tools[6], &events.toolsChanged[6] } });
		std::string parallelNote;
		// 全局多指关闭时优先说明；提示不依赖手掌总开关，也不替用户修改其他偏好。
		if (draft.parallelWriting)
		{
			if (!draft.multiTouchEnabled) parallelNote = IA(palm.ParallelWritingMultiTouchNote);
			else if (!draft.laserMultiTouchEnabled) parallelNote = IA(palm.ParallelWritingLaserMultiTouchNote);
		}
		events.parallelWritingChanged = ToggleRow("##palm-eraser-parallel", IA(palm.ParallelWriting).c_str(),
			IA(palm.ParallelWritingDescription).c_str(), "\ue7c9", draft.parallelWriting, parallelNote.c_str());
		SectionHeader(IA(palm.SizeSection).c_str());
		draft.sizeMode = draft.sizeMode >= 0 && draft.sizeMode <= 2 ? draft.sizeMode : 0;
		const char* sizeNotes[]{ palm.SizeModeDynamicNote, palm.SizeModeFollowDefaultNote, palm.SizeModeFixedNote };
		events.sizeModeChanged = ComboRow("##palm-eraser-size-mode", IA(palm.SizeMode).c_str(),
			IA(palm.SizeModeDescription).c_str(), "\ue7c9", draft.sizeMode,
			{ IA(palm.SizeModeDynamic) + IA(I18nKey.SettingsUI.Design.RecommendedSuffix),
				IA(palm.SizeModeFollowDefault), IA(palm.SizeModeFixed) }, IA(sizeNotes[draft.sizeMode]).c_str());
		if (ConditionalBlock block{ "##palm-eraser-dynamic-size", draft.sizeMode == 0 }; block)
		{
			const auto& eraser = I18nKey.SettingsUI.Draw.Cursor.Eraser;
			const int defaultShape = draft.eraserShape >= 0 && draft.eraserShape <= 2 ? draft.eraserShape : 0;
			const int shape = draft.shape >= 0 && draft.shape <= 2 ? draft.shape : defaultShape;
			const char* methodKeys[3][4]{
				{ palm.MethodEllipseArea, palm.MethodRectangleArea, palm.MethodInscribedCircle, palm.MethodCircumscribedCircle },
				{ palm.MethodRectangleArea, palm.MethodEllipseArea, palm.MethodInscribedRectangle, palm.MethodCircumscribedRectangle },
				{ palm.MethodEllipseArea, palm.MethodRectangleArea, palm.MethodInscribedSquare, palm.MethodCircumscribedSquare } };
			std::vector<std::string> methods;
			for (const char* key : methodKeys[shape]) methods.push_back(IA(key));
			methods[0] += IA(I18nKey.SettingsUI.Design.RecommendedSuffix);
			const char* shapeKeys[]{ eraser.Circle, eraser.Rectangle, eraser.Square };
			const auto shapeName = IA(shapeKeys[shape]);
			const auto shapeNote = std::vformat(IA(palm.SizeMethodShapeNote), std::make_format_args(shapeName));
			// 每种形状分别记住方法；跟随默认时只编辑当前生效形状的那一项。
			int& method = draft.sizeMethods[shape];
			method = method >= 0 && method <= 3 ? method : 0;
			events.sizeMethodsChanged[shape] = ComboRow("##palm-eraser-size-method", IA(palm.SizeMethod).c_str(),
				IA(palm.SizeMethodDescription).c_str(), "\ue7c9", method, methods, shapeNote.c_str());
			const float multiplierWidth = (ImGui::GetContentRegionAvail().x >= Pixels(740.0F) ? 240.0F : 196.0F)
				+ SliderResetExtraWidthDip;
			const auto resetLabel = IA(I18nKey.SettingsUI.Regular.Appearance.ResetDefault);
			float multiplier = static_cast<float>(draft.sizeMultiplierPercent);
			SettingRow("##palm-eraser-size-multiplier", IA(palm.SizeMultiplier).c_str(), IA(palm.SizeMultiplierDescription).c_str(),
				"\ue7c9", multiplierWidth, ControlHeight, [&](const LayoutRect& bounds) {
					events.sizeMultiplierChanged = SliderResetAction(bounds, multiplier, 25.0F, 300.0F,
						events.sizeMultiplierActive, events.sizeMultiplierReset, resetLabel.c_str(), "%.0f%%"); });
			if (events.sizeMultiplierChanged) draft.sizeMultiplierPercent = std::clamp(static_cast<int>(std::round(multiplier)), 25, 300);
			if (events.sizeMultiplierReset)
			{
				events.sizeMultiplierChanged |= draft.sizeMultiplierPercent != 100;
				draft.sizeMultiplierPercent = 100;
			}
		}
		{
			namespace Cursor = Inkeys::Drawing::Draw3;
			const auto& eraser = I18nKey.SettingsUI.Draw.Cursor.Eraser;
			// 明暗与纹样全局共用，这里只说明来源；手掌只能另选形状。
			SectionHeader(IA(eraser.N).c_str());
			Notice("##palm-eraser-cursor-note", IA(palm.CursorNoticeTitle).c_str(),
				IA(palm.CursorNoticeDescription).c_str(), ImFluentInfoSeverity_Informational);
			const int defaultShape = draft.eraserShape >= 0 && draft.eraserShape <= 2 ? draft.eraserShape : 0;
			const auto pattern = static_cast<Cursor::EraserCursorPattern>(
				draft.eraserPattern >= 0 && draft.eraserPattern <= 2 ? draft.eraserPattern : 0);
			const auto theme = draft.eraserTheme == 1 ? Cursor::EraserCursorTheme::Dark : Cursor::EraserCursorTheme::Light;
			// 选择器索引 0 为“跟随默认”（预览默认形状），其余依次为三种形状。
			int selected = draft.shape >= 0 && draft.shape <= 2 ? draft.shape + 1 : 0;
			std::vector<PreviewSelectorGroup> groups(1);
			groups[0].selectedIndex = &selected; groups[0].changed = &events.shapeChanged;
			const char* labels[]{ palm.FollowDefault, eraser.Circle, eraser.Rectangle, eraser.Square };
			for (int index = 0; index < 4; ++index)
				groups[0].items.push_back({ IA(labels[index]), [&, index](const LayoutRect& area) {
					if (preview) preview(area, { static_cast<Cursor::EraserShape>(index == 0 ? defaultShape : index - 1),
						pattern, theme }); } });
			const auto inlineContent = InlinePreviewSelector("##palm-eraser-shape-options", std::move(groups));
			SettingRow("##palm-eraser-shape", IA(palm.Shape).c_str(), IA(palm.ShapeDescription).c_str(), nullptr,
				0, 0, [](const LayoutRect&) {}, Card, Stroke, 0, IM_COL32_WHITE, false, nullptr, true, &inlineContent);
			if (events.shapeChanged) draft.shape = selected - 1;
		}
		return events;
	}

	SpeedEraserEvents RenderSpeedEraser(SpeedEraserDraft& draft, const EraserDemoPresenter& presenter)
	{
		SpeedEraserEvents events;
		const auto& eraser = I18nKey.SettingsUI.Draw.Eraser;
		events.back = IconButton("##speed-eraser-back", "\ue72b");
		PageHeader(IA(eraser.Kind.Speed).c_str());
		PageContentStart();
		const auto& speed = eraser.SpeedEraser;
		const std::string introduction = IA(speed.Introduction);
		const float width = ImGui::GetContentRegionAvail().x;
		const float padding = Pixels(ImFluent::GetStyle().CardPadding);
		const float innerWidth = (std::max)(1.0F, width - padding * 2.0F);
		const float textHeight = TextHeight(introduction.c_str(), innerWidth);
		float contentHeight = textHeight;
		const float gap = Pixels(16.0F), smallGap = Pixels(8.0F);
		const bool stacked = innerWidth < Pixels(600.0F);
		const float canvasWidth = stacked ? innerWidth : (innerWidth - gap) * 0.5F;
		const float canvasHeight = canvasWidth * EraserDemoCanvasHeight / EraserDemoCanvasWidth;
		const std::array<std::string, 2> titles{ IA(speed.DemoFineTitle), IA(speed.DemoSweepTitle) };
		const std::array<std::string, 2> descriptions{ IA(speed.DemoFineDescription), IA(speed.DemoSweepDescription) };
		const std::array<std::string, 2> labels{ IA(eraser.Kind.Fixed), IA(eraser.Kind.Speed) };
		const float titleHeight = (std::max)(TextHeight(titles[0].c_str(), innerWidth, ImFluentTextStyle_Subtitle),
			TextHeight(titles[1].c_str(), innerWidth, ImFluentTextStyle_Subtitle));
		const float descriptionHeight = (std::max)(TextHeight(descriptions[0].c_str(), innerWidth),
			TextHeight(descriptions[1].c_str(), innerWidth));
		const float labelHeight = (std::max)(TextHeight(labels[0].c_str(), canvasWidth, ImFluentTextStyle_BodyStrong),
			TextHeight(labels[1].c_str(), canvasWidth, ImFluentTextStyle_BodyStrong));
		const float headingHeight = titleHeight + smallGap + descriptionHeight + gap;
		const float boardHeight = labelHeight + smallGap + canvasHeight;
		const float boardsHeight = stacked ? boardHeight * 2.0F + gap : boardHeight;
		EraserDemoPresentation demo;
		bool deferredDemo = false;
		if (presenter)
		{
			// 先用真实字体和画布比例测量，再仅调用一次播放端；被外层滚动裁剪时不初始化/推进。
			const auto origin = ImGui::GetCursorScreenPos();
			const auto* window = ImGui::GetCurrentWindow();
			bool visible = false;
			for (int board = 0; board < 2; ++board)
			{
				const ImVec2 top(origin.x + padding + (stacked ? 0.0F : board * (canvasWidth + gap)),
					origin.y + padding + headingHeight + labelHeight + smallGap + (stacked ? board * (boardHeight + gap) : 0.0F));
				visible |= !window->SkipItems && window->ClipRect.Overlaps(ImRect(top, { top.x + canvasWidth, top.y + canvasHeight }));
			}
			demo = presenter(visible, canvasWidth);
			// 首次恢复到已滚动位置时暂未创建纹理，仍保留完整高度，避免压缩页面破坏滚动恢复。
			deferredDemo = !visible && !demo.texture;
			if (demo.texture || deferredDemo) contentHeight += headingHeight + boardsHeight + gap + Pixels(8.0F) + gap;
		}
		const float height = std::ceil(contentHeight + padding * 2.0F);
		ImGui::SetNextWindowSizeConstraints({ 0.0F, height }, { FLT_MAX, height });
		ImGui::SetNextWindowContentSize({ 0.0F, contentHeight });
		if (BeginCard("##speed-eraser-introduction", { width, height }))
		{
			const auto origin = ImGui::GetCursorScreenPos();
			auto* draw = ImGui::GetWindowDrawList();
			float y = origin.y;
			if (demo.texture || deferredDemo)
			{
				const int scene = std::clamp(demo.scene, 0, 1);
				TextAt(titles[scene].c_str(), { origin.x, y }, innerWidth, ImFluentTextStyle_Subtitle);
				y += titleHeight + smallGap;
				TextAt(descriptions[scene].c_str(), { origin.x, y }, innerWidth, ImFluentTextStyle_Body, TextSecondary);
				y = origin.y + headingHeight;
				const ImU32 tint = IM_COL32(255, 255, 255, static_cast<int>(std::clamp(demo.opacity, 0.0F, 1.0F) * 255));
				for (int board = 0; board < 2; ++board)
				{
					const ImVec2 label(origin.x + (stacked ? 0.0F : board * (canvasWidth + gap)),
						y + (stacked ? board * (boardHeight + gap) : 0.0F));
					TextAt(labels[board].c_str(), label, canvasWidth, ImFluentTextStyle_BodyStrong);
					const ImVec2 top(label.x, label.y + labelHeight + smallGap);
					const ImVec2 bottom(top.x + canvasWidth, top.y + canvasHeight);
					const float rounding = Pixels(4.0F);
					draw->AddRectFilled(top, bottom, IsDarkMode() ? IM_COL32(18, 59, 50, 255) : IM_COL32(247, 248, 250, 255), rounding);
					if (demo.texture) draw->AddImageRounded(demo.texture, top, bottom, { board * 0.5F, 0 }, { (board + 1) * 0.5F, 1 }, tint, rounding);
					draw->AddRect(top, bottom, Stroke, rounding, 0, 1.0F);
				}
				y += boardsHeight + gap;
				for (int dot = 0; dot < 2; ++dot)
					draw->AddCircleFilled({ origin.x + innerWidth * 0.5F + Pixels(dot == 0 ? -6.0F : 6.0F), y + Pixels(4.0F) },
						Pixels(3.0F), dot == scene ? Accent : Stroke);
				y += Pixels(8.0F) + gap;
			}
			TextAt(introduction.c_str(), { origin.x, y }, innerWidth);
			// 整卡一次登记完整高度，说明自然换行，内容交给页面滚动而不是卡内滚动。
			ImGui::Dummy({ innerWidth, contentHeight });
		}
		EndCard();
		if (ConditionalBlock block{"##speed-size-visibility", draft.physicalSizeWarning}; block)
		{
			PageContentStart();
			Notice("##speed-eraser-size-warning", IA(speed.SizeWarningTitle).c_str(),
				IA(speed.SizeWarningDescription).c_str(), ImFluentInfoSeverity_Warning);
		}

		SectionHeader(IA(speed.BasicSettings).c_str());
		events.automaticChanged = ToggleRow("##eraser-automatic", IA(eraser.Automatic).c_str(),
			nullptr, "\ued62", draft.automaticEnabled);
		const auto& sensitivity = I18nKey.UI.Bar.EraserAttributes;
		events.sensitivityChanged = ComboRow("##eraser-sensitivity", IA(sensitivity.SensitivityLabel).c_str(),
			nullptr, "\ue9e9", draft.sensitivity,
			{ IA(sensitivity.LowLabel), IA(sensitivity.MediumLabel), IA(sensitivity.HighLabel) });
		const std::array<std::string, 5> entries{
			IA(eraser.Input.MouseLeft), IA(eraser.Input.MouseRight), IA(eraser.Input.Touch),
			IA(eraser.Input.PenTip), IA(eraser.Input.PenTail) };
		CheckboxGroup("##eraser-devices", IA(speed.EnabledDevices).c_str(), "\ue7f8", {
			{ entries[0], &draft.enabled[0], &events.enabledChanged[0] },
			{ entries[1], &draft.enabled[1], &events.enabledChanged[1] },
			{ entries[2], &draft.enabled[2], &events.enabledChanged[2] },
			{ entries[3], &draft.enabled[3], &events.enabledChanged[3] },
			{ entries[4], &draft.enabled[4], &events.enabledChanged[4] } });

		// 显示条件只跟随本帧开关与设备选择，隐藏时保留原有精细偏好。
		if (ConditionalBlock block{"##eraser-fine-visibility",
			draft.automaticEnabled && (draft.enabled[2] || draft.enabled[3] || draft.enabled[4])}; block)
		{
			SectionHeader(IA(speed.FineControl).c_str());
			const std::vector<std::string> responses{ IA(eraser.Response.Auto),
				IA(eraser.Response.ScreenPen), IA(eraser.Response.Digitizer) };
			const int first = draft.automaticPenSupported ? 0 : 1;
			const std::vector<std::string> availableResponses(responses.begin() + first, responses.end());
			for (int pen = 0; pen < 2; ++pen)
			{
				const int entry = pen + 3;
				ImGui::PushID(entry);
				if (ConditionalBlock responseBlock{"##eraser-response-visibility", draft.enabled[entry]}; responseBlock)
				{
					int response = std::clamp(draft.penResponses[pen], first, 2) - first;
					events.penResponseChanged[pen] = ComboRow("##eraser-response", entries[entry].c_str(),
						IA(eraser.ResponseFor).c_str(), "\uedc6", response, availableResponses);
					// Win7 隐藏 Auto 项后仍返回稳定枚举，只在用户真正选择时更新草稿。
					if (events.penResponseChanged[pen]) draft.penResponses[pen] = response + first;
				}
				ImGui::PopID();
			}
			if (ConditionalBlock touchBlock{"##eraser-touch-visibility", draft.enabled[2]}; touchBlock)
				events.touchAreaChanged = ToggleRow("##eraser-touch-area", IA(eraser.TouchArea.N).c_str(),
					IA(eraser.TouchArea.E).c_str(), "\ue7c9", draft.touchArea, IA(eraser.TouchArea.Note).c_str());
		}
		return events;
	}

	void RenderSponsorThanks(const char* text, float widthPixels)
	{
		PageBlockScope motion("##sponsor-thanks-motion");
		const float width = (std::max)(1.0F, widthPixels);
		const auto origin = ImGui::GetCursorScreenPos();
		ImFluent::PushFont(ImFluentTextStyle_Body);
		const float size = ImGui::GetFontSize(), lineHeight = Pixels(BodyText.lineHeight);
		auto* baked = ImGui::GetFontBaked();
		const float glyphScale = size / baked->Size;
		// 换行按advance，但真实字图可越过advance；为两端墨迹和居中取整各留半物理像素。
		float leftOverhang = 0.0F, rightOverhang = 0.0F;
		const char* scan = text; const char* textEnd = text + std::strlen(text);
		while (scan < textEnd)
		{
			unsigned int point = 0; const int length = ImTextCharFromUtf8(&point, scan, textEnd);
			if (length <= 0) break;
			scan += length;
			const auto* glyph = baked->FindGlyph(static_cast<ImWchar>(point));
			if (!glyph->Visible) continue;
			leftOverhang = (std::max)(leftOverhang, -glyph->X0 * glyphScale);
			rightOverhang = (std::max)(rightOverhang, (glyph->X1 - glyph->AdvanceX) * glyphScale);
		}
		auto layout = MeasureInline({ { text } }, (std::max)(1.0F, width - leftOverhang - rightOverhang - 1.0F), ImFluentTextStyle_Body, true);
		float top = 0.0F, bottom = layout.height;
		for (auto& fragment : layout.fragments)
		{
			// 换行尾部空白和字形左右边距不属于可见文字，不能用 advance 宽度作视觉居中。
			while (!fragment.text.empty() && (fragment.text.back() == ' ' || fragment.text.back() == '\t')) fragment.text.pop_back();
			float advance = 0.0F, left = FLT_MAX, right = -FLT_MAX, inkTop = FLT_MAX, inkBottom = -FLT_MAX;
			const char* cursor = fragment.text.c_str(); const char* end = cursor + fragment.text.size();
			while (cursor < end)
			{
				unsigned int point = 0; const int length = ImTextCharFromUtf8(&point, cursor, end);
				if (length <= 0) break;
				cursor += length;
				const auto* glyph = baked->FindGlyph(static_cast<ImWchar>(point));
				if (glyph->Visible)
				{
					left = (std::min)(left, advance + glyph->X0 * glyphScale);
					right = (std::max)(right, advance + glyph->X1 * glyphScale);
					inkTop = (std::min)(inkTop, glyph->Y0 * glyphScale);
					inkBottom = (std::max)(inkBottom, glyph->Y1 * glyphScale);
				}
				advance += glyph->AdvanceX * glyphScale;
			}
			if (left == FLT_MAX) continue;
			fragment.x = (width - (right - left)) * 0.5F - left;
			fragment.width = advance;
			const float drawY = std::round(origin.y + fragment.y + (lineHeight - size) * 0.5F) - origin.y;
			top = (std::min)(top, drawY + inkTop);
			bottom = (std::max)(bottom, drawY + inkBottom);
		}
		// 与 DrawInline 的像素取整一致，完整登记变音符号和下伸部的真实高度。
		const float shift = std::ceil(-top);
		for (auto& fragment : layout.fragments) fragment.y += shift;
		layout.height = std::ceil(bottom + shift);
		ImFluent::PopFont();
		HomeAction ignored = HomeAction::None;
		DrawInline("##sponsor-thanks", layout, origin, ImFluentTextStyle_Body, TextPrimary, ignored);
		ImGui::Dummy({ width, layout.height });
	}

	FeedbackAction RenderFeedback(ImTextureID qrTexture, float qrAspect, ImTextureID githubTexture)
	{
		PageHeader(IA(I18nKey.SettingsUI.Feedback.N).c_str(), IA(I18nKey.SettingsUI.Feedback.Description).c_str());
		PageContentStart();
		FeedbackAction action = FeedbackAction::None;
		if (NavigationRow("##feedback-issues", IA(I18nKey.SettingsUI.Feedback.Issues.N).c_str(),
			IA(I18nKey.SettingsUI.Feedback.Issues.E).c_str(), "\ue943", githubTexture, TextPrimary))
			action = FeedbackAction::Issues;
		if (NavigationRow("##feedback-questionnaire", IA(I18nKey.SettingsUI.Feedback.Questionnaire.N).c_str(),
			IA(I18nKey.SettingsUI.Feedback.Questionnaire.E).c_str(), "\ue70f"))
			action = FeedbackAction::Questionnaire;
		const float qrHeight = 96.0F * (std::isfinite(qrAspect) && qrAspect > 0.0F ? qrAspect : 1.0F);
		SettingRow("##feedback-qr", IA(I18nKey.SettingsUI.Feedback.Qr.N).c_str(),
			IA(I18nKey.SettingsUI.Feedback.Qr.E).c_str(), nullptr, 96.0F, qrHeight,
			[&](const LayoutRect& bounds)
			{
				// 白色透明 mask 复用同一纹理，主题只改变 tint；三边间距由公共行 padding 保证。
				if (qrTexture) ImGui::GetWindowDrawList()->AddImage(qrTexture,
					{ bounds.left, bounds.top }, { bounds.right, bounds.bottom }, { 0, 0 }, { 1, 1 }, TextPrimary);
			}, Card, Stroke, 0, IM_COL32_WHITE, true);
		SectionHeader(IA(I18nKey.SettingsUI.Feedback.ContactSection).c_str());
		std::string community = IA(I18nKey.SettingsUI.Home.Group);
		if (I18n::isIdentifying(L"zh-CN")) community += " " + IA(I18nKey.SettingsUI.Home.GroupE);
		if (NavigationRow("##feedback-community", community.c_str(),
			IA(I18nKey.SettingsUI.Feedback.CommunityDescription).c_str(), "\ue716"))
			action = FeedbackAction::Community;
		const std::string emailDescription = "alan-crl@foxmail.com\n"
			+ IA(I18nKey.SettingsUI.Feedback.ContactDescription);
		if (NavigationRow("##feedback-contact", IA(I18nKey.SettingsUI.Design.Contact).c_str(),
			emailDescription.c_str(), "\ue715"))
			action = FeedbackAction::Contact;
		const std::string author = "AlanCRL · " + IA(I18nKey.SettingsUI.Feedback.AuthorQQ);
		SettingRow("##feedback-author-qq", author.c_str(), nullptr, "\ue77b", TextWidth("2685549821") / Pixels(1.0F) + 16.0F,
			BodyText.size + ImFluent::GetStyle().SpacingXSmall * 2.0F,
			[](const LayoutRect&) { CopyHyperlink("##feedback-author-qq-copy", "2685549821", "2685549821"); });
		return action;
	}

	PptIdleFadeEvents RenderPptIdleFadeRow(PptIdleFadeDraft& draft, const std::vector<std::string>& items)
	{
		PptIdleFadeEvents events;
		const auto& keys = I18nKey.SettingsUI.PlugIn.PPTHelper.WidgetDisplay;
		const std::array labels{ IA(keys.IdleFadeBottomPair), IA(keys.IdleFadeMiddlePair), IA(keys.IdleFadeMainBar) };
		const std::array values{ &draft.bottomPair, &draft.middlePair, &draft.mainBar };
		const std::array changed{ &events.bottomPair, &events.middlePair, &events.mainBar };
		float widest = 0, comboWidth = 160;
		for (const auto& label : labels) widest = (std::max)(widest, ControlTextWidth(label.c_str()) + Pixels(32));
		for (const auto& item : items) comboWidth = (std::max)(comboWidth, ControlTextWidth(item.c_str()) / Pixels(1) + 48);
		const auto columnsFor = [&](float width) { return width >= (std::max)(Pixels(440), widest * 2 + Pixels(16)) ? 2 : 1; };
		InlineRowContent scopes;
		scopes.visibilityId = "##ppt-idle-fade-options"; scopes.visible = draft.timeoutIndex != 0;
		scopes.height = [&](float width) {
			const int rows = (3 + columnsFor(width) - 1) / columnsFor(width);
			return rows * Pixels(ControlHeight) + (rows - 1) * Pixels(ControlSpacing);
		};
		scopes.render = [&](const LayoutRect& bounds) {
			// 三项只占父卡片的内联区，不再建立额外标题、图标或卡片。
			const int columns = columnsFor(bounds.Width());
			const float columnWidth = (bounds.Width() - Pixels(16) * (columns - 1)) / columns;
			ImGui::PushID("##ppt-idle-fade-scopes"); ImFluent::BeginDisabled(draft.timeoutIndex == 0);
			for (int index = 0; index < 3; ++index)
			{
				ImGui::SetCursorScreenPos({ bounds.left + (index % columns) * (columnWidth + Pixels(16)),
					bounds.top + (index / columns) * Pixels(ControlHeight + ControlSpacing) });
				ImGui::PushID(index); *changed[index] = Checkbox((labels[index] + "###scope").c_str(), values[index]); ImGui::PopID();
			}
			ImFluent::EndDisabled(); ImGui::PopID();
		};
		SettingRow("##ppt-idle-fade", IA(keys.IdleFade).c_str(), IA(keys.IdleFadeE).c_str(), "\ue7b3",
			(std::min)(320.0F, comboWidth), ControlHeight,
			[&](const LayoutRect&) { events.timeout = ComboBox("##select", &draft.timeoutIndex, items); },
			Card, Stroke, 0, IM_COL32_WHITE, false, nullptr, true, &scopes);
		return events;
	}

	std::wstring ResolveDesktopShortcutDraftName(const DesktopShortcutDraft& draft)
	{
		return draft.preset == Business::DesktopShortcut::CustomPreset(draft.names.kind)
			? utf8ToUtf16(draft.customName) : Business::DesktopShortcut::ResolveName(draft.preset, L"", draft.names);
	}

	bool DesktopShortcutDraftNamesCollide(const DesktopShortcutDraft& draft, const DesktopShortcutDraft& other)
	{
		const auto name = ResolveDesktopShortcutDraftName(draft);
		// 被拒绝的草稿不能替代最后有效名称；两份名称都受保护，避免交叉编辑抢占。
		return Business::DesktopShortcut::NamesCollide(name, ResolveDesktopShortcutDraftName(other))
			|| Business::DesktopShortcut::NamesCollide(name, other.lastValidName);
	}

	bool IsDesktopShortcutDraftValid(const DesktopShortcutDraft& draft, const DesktopShortcutDraft& other)
	{
		const auto name = ResolveDesktopShortcutDraftName(draft);
		return draft.desktopAvailable && Business::DesktopShortcut::IsValidName(name, draft.directoryLength)
			&& !DesktopShortcutDraftNamesCollide(draft, other);
	}

	GeneralEvents RenderGeneral(GeneralDraft& draft, GeneralSection section)
	{
		GeneralEvents events;
		// 存储页由原配置保存业务分支承载，避免在此重复提交标题或控件。
		if (section == GeneralSection::Storage) return events;
		const char* title = section == GeneralSection::Appearance ? I18nKey.SettingsUI.Design.GeneralAppearance
			: section == GeneralSection::Position ? I18nKey.SettingsUI.Position.N
			: section == GeneralSection::Interaction ? I18nKey.SettingsUI.Design.GeneralInteraction
			: section == GeneralSection::Shortcuts ? I18nKey.SettingsUI.HotKey.N
			: section == GeneralSection::DesktopShortcuts ? I18nKey.SettingsUI.DesktopShortcut.N
			: I18nKey.SettingsUI.Design.GeneralBasic;
		PageHeader(IA(title).c_str());
		// 仅渲染当前子页；未显示字段继续使用同一草稿，不触发写回或重置。
		if (section == GeneralSection::Basic)
		{
			SectionHeader(IA(I18nKey.SettingsUI.Design.OperationModeSection).c_str());
			// 操作模式说明独立成蓝色信息卡；选择行保留完整操作空间。
			const std::vector<std::string> modeNames{
				IA(I18nKey.SettingsUI.Regular.SelectAndClickThrough.Combined),
				IA(I18nKey.SettingsUI.Regular.SelectAndClickThrough.Separate) };
			const std::string modeDescription = modeNames[0] + ": "
				+ IA(I18nKey.SettingsUI.Regular.SelectAndClickThrough.CombinedDescription) + "\n"
				+ modeNames[1] + ": " + IA(I18nKey.SettingsUI.Regular.SelectAndClickThrough.SeparateDescription) + "\n"
				+ IA(I18nKey.SettingsUI.Regular.SelectAndClickThrough.ClickThrough) + ": "
				+ IA(I18nKey.SettingsUI.Regular.SelectAndClickThrough.ClickThroughDescription);
			Notice("##operation-mode-notice", IA(I18nKey.SettingsUI.Regular.SelectAndClickThrough.N).c_str(),
				modeDescription.c_str());
			events.selectModeChanged = ComboRow("##选择与穿透", IA(I18nKey.SettingsUI.Regular.SelectAndClickThrough.N).c_str(),
				nullptr, "\ue8b0", draft.selectMode, modeNames);

			SectionHeader(IA(I18nKey.SettingsUI.Design.IconArrangementSection).c_str());
			const std::vector<std::string> layoutTitles{
				IA(I18nKey.SettingsUI.Regular.ToolbarIconOrder.N),
				IA(I18nKey.SettingsUI.Regular.ToolbarExpansionDirection.N) };
			const std::vector<std::vector<std::string>> layoutOptions{
				{ IA(I18nKey.SettingsUI.Regular.ToolbarIconOrder.FollowMainButton),
				IA(I18nKey.SettingsUI.Regular.ToolbarIconOrder.FollowMainButtonReverse),
				IA(I18nKey.SettingsUI.Regular.ToolbarIconOrder.LeftToRight),
				IA(I18nKey.SettingsUI.Regular.ToolbarIconOrder.RightToLeft) },
				{ IA(I18nKey.SettingsUI.Regular.ToolbarExpansionDirection.Centerline),
				IA(I18nKey.SettingsUI.Regular.ToolbarExpansionDirection.WhenSpaceInsufficient) } };
			events.iconOrderChanged = ComboRow("主栏图标顺序", layoutTitles[0].c_str(), nullptr,
				"\ue8a4", draft.iconOrder, layoutOptions[0]);
			events.expansionDirectionChanged = ComboRow("主栏展开方向调整", layoutTitles[1].c_str(), nullptr,
				"\ue8a4", draft.expansionDirection, layoutOptions[1]);

			SectionHeader(IA(I18nKey.SettingsUI.Regular.StartUp.N).c_str());
			events.startupChanged = ToggleRow("startup", IA(I18nKey.SettingsUI.Regular.StartUp.AutoStart).c_str(),
				IA(I18nKey.SettingsUI.Regular.StartUp.AutoStartE).c_str(), "\ue7e8", draft.startup);
		}
		if (section == GeneralSection::Position)
		{
			const auto& position = I18nKey.SettingsUI.Position;
			SectionHeader(IA(position.DesktopSection).c_str());
			// 启动位置只保存下次启动的偏好，保持当前运行中的主栏位置；类型与位置是同一枚举的展示拆分。
			const auto placement = Inkeys::UI::NormalizeStartupPlacement(draft.startupPlacement);
			int placementType = Inkeys::UI::StartupPlacementType(placement);
			int placementPosition = Inkeys::UI::StartupPlacementPosition(placement);
			const std::vector<std::string> typeItems{ IA(position.TypeBottomBar), IA(position.TypeTopBar), IA(position.TypeFloating) };
			const std::vector<std::string> positionItems = placementType == 2
				? std::vector<std::string>{ IA(position.FloatBottomLeft), IA(position.FloatBottomRight), IA(position.FloatBottomCenter) }
				: std::vector<std::string>{ IA(position.ExpandRight), IA(position.ExpandLeft) };
			bool typeChanged = false, positionChanged = false;
			const std::string positionLabel = IA(position.PositionLabel);
			const std::string startupTitle = IA(I18nKey.SettingsUI.Preset.StartupPlacement.N);
			const std::string startupDescription = IA(position.StartupE);
			const std::string startupNote = IA(I18nKey.SettingsUI.Design.NextStartupRequired);
			float typeWidth = 160.0F;
			for (const auto& item : typeItems) typeWidth = (std::max)(typeWidth, ControlTextWidth(item.c_str()) / Pixels(1.0F) + 48.0F);
			const InlineRowContent positionContent = InlineCombo("startup-position", positionLabel.c_str(),
				placementPosition, positionItems, positionChanged);
			SettingRow("##启动后位置", startupTitle.c_str(), startupDescription.c_str(), "\ue707",
				(std::min)(320.0F, typeWidth), ControlHeight,
				[&](const LayoutRect&) { typeChanged = ComboBox("##select", &placementType, typeItems); },
				Card, Stroke, 0, IM_COL32_WHITE, false, startupNote.c_str(), true, &positionContent);
			if (typeChanged || positionChanged)
			{
				// 切换类型时位置回到该类型的首项，避免旧下标落到另一类选项上。
				if (typeChanged) placementPosition = 0;
				draft.startupPlacement = static_cast<int>(Inkeys::UI::StartupPlacementFromSelection(placementType, placementPosition));
				events.startupPlacementChanged = true;
			}

			// PPT / 白板场景：同一布局，复选框决定进入前的哪些主栏状态会自动变成居中底栏。
			const auto workspaceRow = [&](const char* id, const char* title, const char* description, const char* glyph,
				int& mask, int& direction, bool& maskChanged, bool& directionChanged)
			{
				const std::array<std::string, 4> labels{ IA(position.StateBottom), IA(position.StateBottomFolded),
					IA(position.StateFloating), IA(position.StateFloatingFolded) };
				const std::vector<std::string> directions{ IA(position.DirectionKeep), IA(position.ExpandRight), IA(position.ExpandLeft) };
				const std::string directionLabel = IA(position.MainButtonDirection);
				float widest = 0;
				for (const auto& label : labels) widest = (std::max)(widest, ControlTextWidth(label.c_str()) + Pixels(32));
				const auto columnsFor = [&](float width) { return width >= (std::max)(Pixels(440), widest * 2 + Pixels(16)) ? 2 : 1; };
				InlineRowContent content;
				content.height = [&](float width) {
					const int rows = (4 + columnsFor(width) - 1) / columnsFor(width);
					return (rows + 1) * Pixels(ControlHeight) + rows * Pixels(ControlSpacing);
				};
				content.render = [&](const LayoutRect& bounds) {
					const int columns = columnsFor(bounds.Width());
					const float columnWidth = (bounds.Width() - Pixels(16) * (columns - 1)) / columns;
					const int rows = (4 + columns - 1) / columns;
					ImGui::PushID(id);
					for (int index = 0; index < 4; ++index)
					{
						ImGui::SetCursorScreenPos({ bounds.left + (index % columns) * (columnWidth + Pixels(16)),
							bounds.top + (index / columns) * Pixels(ControlHeight + ControlSpacing) });
						bool checked = (mask >> index) & 1;
						ImGui::PushID(index);
						if (Checkbox((labels[index] + "###state").c_str(), &checked))
						{
							mask = checked ? (mask | (1 << index)) : (mask & ~(1 << index));
							maskChanged = true;
						}
						ImGui::PopID();
					}
					const float top = bounds.top + rows * Pixels(ControlHeight + ControlSpacing);
					const auto combo = InlineCombo("##direction", directionLabel.c_str(), direction, directions, directionChanged);
					combo.render({ bounds.left, top, bounds.right, top + Pixels(ControlHeight) });
					ImGui::PopID();
				};
				SettingRow(id, title, description, glyph, 0.0F, 0.0F, [](const LayoutRect&) {},
					Card, Stroke, 0, IM_COL32_WHITE, false, nullptr, true, &content);
			};
			SectionHeader(IA(position.PptSection).c_str());
			const std::string pptTitle = IA(position.PptAutoDock), pptDescription = IA(position.PptAutoDockE);
			workspaceRow("##ppt-auto-dock", pptTitle.c_str(), pptDescription.c_str(), "\ue7b3",
				draft.pptDockMask, draft.pptDockDirection, events.pptDockMaskChanged, events.pptDockDirectionChanged);
			SectionHeader(IA(position.WhiteboardSection).c_str());
			const std::string whiteboardTitle = IA(position.WhiteboardAutoDock), whiteboardDescription = IA(position.WhiteboardAutoDockE);
			workspaceRow("##whiteboard-auto-dock", whiteboardTitle.c_str(), whiteboardDescription.c_str(), "\ue70f",
				draft.whiteboardDockMask, draft.whiteboardDockDirection,
				events.whiteboardDockMaskChanged, events.whiteboardDockDirectionChanged);
		}
		if (section == GeneralSection::Appearance)
		{
			const float sliderWidth = ImGui::GetContentRegionAvail().x >= Pixels(740.0F) ? 240.0F : 196.0F;
			const float resetSliderWidth = sliderWidth + SliderResetExtraWidthDip;
			const std::string resetLabel = IA(I18nKey.SettingsUI.Regular.Appearance.ResetDefault);
			const std::vector<std::string> themeOptions{ IA(I18nKey.SettingsUI.Regular.Appearance.Theme.Dark),
				IA(I18nKey.SettingsUI.Regular.Appearance.Theme.Light) };

			SectionHeader(IA(I18nKey.SettingsUI.Design.ThemeSection).c_str());
			// 主栏主题只影响 UI3 Bar；设置窗口配色单独选择，与标题栏切换按钮共用同一偏好。
			events.themeModeChanged = ComboRow("##主栏主题", IA(I18nKey.SettingsUI.Regular.Appearance.Theme.N).c_str(),
				IA(I18nKey.SettingsUI.Regular.Appearance.Theme.E).c_str(), "\ue790", draft.themeMode, themeOptions);
			events.settingThemeModeChanged = ComboRow("##设置主题", IA(I18nKey.SettingsUI.Regular.Appearance.SettingTheme.N).c_str(),
				IA(I18nKey.SettingsUI.Regular.Appearance.SettingTheme.E).c_str(), "\ue771", draft.settingThemeMode, themeOptions);

			SectionHeader(IA(I18nKey.SettingsUI.Design.OpacitySection).c_str());
			// 只作用于常驻的主栏、主按钮与分页；弹出面板保持原透明度以保证可读。
			SettingRow("bar-background-opacity", IA(I18nKey.SettingsUI.Regular.Appearance.BackgroundOpacity.N).c_str(),
				IA(I18nKey.SettingsUI.Regular.Appearance.BackgroundOpacity.E).c_str(), "\uef1f", resetSliderWidth, 32.0F,
				[&](const LayoutRect& bounds) { events.backgroundOpacityChanged = SliderResetAction(bounds, draft.backgroundOpacity,
					0.30F, 1.00F, events.backgroundOpacityActive, events.backgroundOpacityReset, resetLabel.c_str()); });
			if (events.backgroundOpacityReset) draft.backgroundOpacity = draft.backgroundOpacityDefault;
			bool contentOpacityReset = false;
			SettingRow("bar-content-opacity", IA(I18nKey.SettingsUI.Regular.Appearance.ContentOpacity.N).c_str(),
				IA(I18nKey.SettingsUI.Regular.Appearance.ContentOpacity.E).c_str(), "\ue8a9", resetSliderWidth, 32.0F,
				[&](const LayoutRect& bounds) { events.contentOpacityChanged = SliderResetAction(bounds, draft.contentOpacity,
					0.30F, 1.00F, events.contentOpacityActive, contentOpacityReset, resetLabel.c_str()); });
			if (contentOpacityReset) { draft.contentOpacity = 1.0F; events.contentOpacityChanged = true; }

			SectionHeader(IA(I18nKey.SettingsUI.Design.AnimationSection).c_str());
			events.animationChanged = ToggleRow("animation", IA(I18nKey.SettingsUI.Regular.Appearance.AnimationEnable).c_str(),
				IA(I18nKey.SettingsUI.Regular.Appearance.AnimationEnableE).c_str(), "\ue768", draft.animation);
			if (events.animationChanged) ConfigureRowAnimations(0.0F, draft.animation, draft.animationRate);
			// 效果选择是加载动画卡片的内联下半区：关闭时随条件动画收起并保留偏好，选择仍由现有配置队列保存。
			const std::string startupEffectLabel = IA(I18nKey.SettingsUI.Regular.StartUp.StartupEffect.N);
			const std::vector<std::string> startupEffects{ IA(I18nKey.SettingsUI.Regular.StartUp.StartupEffect.Shimmer),
				IA(I18nKey.SettingsUI.Regular.StartUp.StartupEffect.Aurora),
				IA(I18nKey.SettingsUI.Regular.StartUp.StartupEffect.PixelAssembly) };
			const auto startupEffect = InlineCombo("startup-effect", startupEffectLabel.c_str(), draft.startupEffect,
				startupEffects, events.startupEffectChanged, "##startup-effect-visibility", draft.startupPreview);
			events.startupPreviewChanged = ToggleRow("startup-preview",
				IA(I18nKey.SettingsUI.Regular.StartUp.LoadingAnimation).c_str(),
				IA(I18nKey.SettingsUI.Regular.StartUp.LoadingAnimationE).c_str(), "\ue768", draft.startupPreview,
				IA(I18nKey.SettingsUI.Design.NextStartupRequired).c_str(), &startupEffect);
			events.settingEntryAnimationChanged = ComboRow("setting-entry-animation",
				IA(I18nKey.SettingsUI.Regular.StartUp.SettingEntryAnimation.N).c_str(),
				IA(I18nKey.SettingsUI.Regular.StartUp.SettingEntryAnimation.E).c_str(), "\ue768", draft.settingEntryAnimation,
				{ IA(I18nKey.SettingsUI.Regular.StartUp.SettingEntryAnimation.Off),
				IA(I18nKey.SettingsUI.Regular.StartUp.SettingEntryAnimation.First),
				IA(I18nKey.SettingsUI.Regular.StartUp.SettingEntryAnimation.Always) });

			SectionHeader(IA(I18nKey.SettingsUI.Design.LightingSection).c_str());
			events.edgeLightingChanged = ToggleRow("edge-lighting", IA(I18nKey.SettingsUI.Regular.Appearance.EdgeLighting).c_str(),
				IA(I18nKey.SettingsUI.Regular.Appearance.EdgeLightingE).c_str(), "\ue706", draft.edgeLighting);
			if (ConditionalBlock block{"##edge-lighting-visibility", draft.edgeLighting}; block)
				events.dynamicEdgeLightingChanged = ToggleRow("dynamic-edge-lighting",
					IA(I18nKey.SettingsUI.Regular.Appearance.DynamicEdgeLighting).c_str(),
					IA(I18nKey.SettingsUI.Regular.Appearance.DynamicEdgeLightingE).c_str(), "\ue706", draft.dynamicEdgeLighting);
			SectionHeader(IA(I18nKey.SettingsUI.Design.ScalingSection).c_str());
			events.taskbarScaleBaseChanged = ComboRow("##以任务栏高度作为缩放基础量",
				IA(I18nKey.SettingsUI.Regular.Appearance.TaskbarScaleBase.N).c_str(),
				IA(I18nKey.SettingsUI.Regular.Appearance.TaskbarScaleBase.E).c_str(), "\ue737", draft.taskbarScaleBase,
				{ IA(I18nKey.SettingsUI.Regular.Appearance.TaskbarScaleBase.Off),
				IA(I18nKey.SettingsUI.Regular.Appearance.TaskbarScaleBase.Base),
				IA(I18nKey.SettingsUI.Regular.Appearance.TaskbarScaleBase.Actual) });
			bool barZoomReset = false;
			SettingRow("bar-zoom", IA(I18nKey.SettingsUI.Regular.Appearance.BarZoom.N).c_str(),
				IA(I18nKey.SettingsUI.Regular.Appearance.BarZoom.E).c_str(), "\ue9a6", resetSliderWidth, 32.0F,
				[&](const LayoutRect& bounds) { events.barZoomChanged = SliderResetAction(bounds, draft.barZoom, 0.50F, 2.00F,
					events.barZoomActive, barZoomReset, resetLabel.c_str()); });
			if (barZoomReset) { draft.barZoom = 1.0F; events.barZoomChanged = true; }
		}
		if (section == GeneralSection::Interaction)
		{
			SectionHeader(IA(I18nKey.SettingsUI.Regular.AutoCollapse.N).c_str());
			const std::string collapseTitles[]{ IA(I18nKey.SettingsUI.Regular.AutoCollapse.Drawing),
				IA(I18nKey.SettingsUI.Regular.AutoCollapse.Erasing), IA(I18nKey.SettingsUI.Regular.AutoCollapse.Dragging),
				IA(I18nKey.SettingsUI.Regular.AutoCollapse.OutsideClick) };
			const std::vector<std::string> collapseOptions{ IA(I18nKey.SettingsUI.Regular.AutoCollapse.Off),
				IA(I18nKey.SettingsUI.Regular.AutoCollapse.NonBottom), IA(I18nKey.SettingsUI.Regular.AutoCollapse.Always) };
			// 公共规则在分区顶部说明，四个策略行只负责各自的选择。
			Notice("##auto-collapse-notice", IA(I18nKey.SettingsUI.Regular.AutoCollapse.N).c_str(),
				IA(I18nKey.SettingsUI.Regular.AutoCollapse.Description).c_str());
			const char* collapseIds[]{ "绘制自动收起", "擦除自动收起", "拖动自动收起", "栏外点击自动收起" };
			for (int item = 0; item < 4; ++item)
				events.autoCollapseChanged[item] = ComboRow(collapseIds[item], collapseTitles[item].c_str(),
					nullptr, "\ue70d", draft.autoCollapse[item], collapseOptions);

			SectionHeader(IA(I18nKey.SettingsUI.Design.WindowBehaviorSection).c_str());
			events.topIntervalChanged = ComboRow("top-interval", IA(I18nKey.SettingsUI.Regular.Behavior.TopWindow.N).c_str(),
				IA(I18nKey.SettingsUI.Regular.Behavior.TopWindow.E).c_str(), "\ue916", draft.topInterval,
				{ IA(I18nKey.SettingsUI.Regular.Behavior.TopWindow.K_100ms), IA(I18nKey.SettingsUI.Regular.Behavior.TopWindow.K_500ms),
				IA(I18nKey.SettingsUI.Regular.Behavior.TopWindow.K_1s), IA(I18nKey.SettingsUI.Regular.Behavior.TopWindow.K_3s),
				IA(I18nKey.SettingsUI.Regular.Behavior.TopWindow.K_5s), IA(I18nKey.SettingsUI.Regular.Behavior.TopWindow.K_10s),
				IA(I18nKey.SettingsUI.Regular.Behavior.TopWindow.K_30s) });
			events.rightClickCloseChanged = ToggleRow("right-click-close", IA(I18nKey.SettingsUI.Regular.Behavior.RightClickClose).c_str(),
				IA(I18nKey.SettingsUI.Regular.Behavior.RightClickCloseE).c_str(), "\ue8b2", draft.rightClickClose);

			SectionHeader(IA(I18nKey.SettingsUI.Design.SafetyRecoverySection).c_str());
			events.safetyModeChanged = ComboRow("teaching-safety", IA(I18nKey.SettingsUI.Regular.Tentative.SafeMode.N).c_str(),
				IA(I18nKey.SettingsUI.Regular.Tentative.SafeMode.E).c_str(), "\ue72e", draft.safetyMode,
				{ IA(I18nKey.SettingsUI.Regular.Tentative.SafeMode.Mode1), IA(I18nKey.SettingsUI.Regular.Tentative.SafeMode.Mode2),
				IA(I18nKey.SettingsUI.Regular.Tentative.SafeMode.Mode3), IA(I18nKey.SettingsUI.Regular.Tentative.SafeMode.Mode4) },
				IA(I18nKey.SettingsUI.Regular.Tentative.SafeMode.Note).c_str());
			events.allowMultipleInstancesChanged = ToggleRow("allow-multiple-instances",
				IA(I18nKey.SettingsUI.Regular.Tentative.AllowMultipleInstances.N).c_str(),
				IA(I18nKey.SettingsUI.Regular.Tentative.AllowMultipleInstances.E).c_str(), "\ue7f4", draft.allowMultipleInstances);
		}
		if (section == GeneralSection::Shortcuts)
		{
			Notice("##hotkey-notice", IA(I18nKey.SettingsUI.HotKey.N).c_str(), IA(I18nKey.SettingsUI.HotKey.E).c_str());
			if (ConditionalBlock block{"##hotkey-status-visibility",
				!draft.shortcutGlobalStatus.empty() || !draft.shortcutHookStatus.empty()}; block)
			{
				Notice("##hotkey-status", block.RetainText("title", draft.shortcutGlobalStatus.c_str()),
					block.RetainText("description", draft.shortcutHookStatus.c_str()),
					draft.shortcutStatusWarning ? ImFluentInfoSeverity_Warning : ImFluentInfoSeverity_Informational);
			}
			const char* combinations[]{ "Ctrl + Alt + X", "Ctrl + Z", "Ctrl + Y", "Ctrl + Shift + Z", "Ctrl + Delete", "Ctrl + Q" };
			const std::string names[]{ IA(I18nKey.SettingsUI.HotKey.ToggleAnnotationN), IA(I18nKey.SettingsUI.HotKey.UndoN),
				IA(I18nKey.SettingsUI.HotKey.RedoYN), IA(I18nKey.SettingsUI.HotKey.RedoShiftZN),
				IA(I18nKey.SettingsUI.HotKey.ClearN), IA(I18nKey.SettingsUI.HotKey.FreezeN) };
			const std::string descriptions[]{ IA(I18nKey.SettingsUI.HotKey.ToggleAnnotationE), IA(I18nKey.SettingsUI.HotKey.UndoE),
				IA(I18nKey.SettingsUI.HotKey.RedoYE), IA(I18nKey.SettingsUI.HotKey.RedoShiftZE),
				IA(I18nKey.SettingsUI.HotKey.ClearE), IA(I18nKey.SettingsUI.HotKey.FreezeE) };
			for (int item = 0; item < 6; ++item)
			{
				if (item == 0) SectionHeader(IA(I18nKey.SettingsUI.Design.ShortcutGlobalSection).c_str());
				else if (item == 1) SectionHeader(IA(I18nKey.SettingsUI.Design.ShortcutEditingSection).c_str());
				else if (item == 5) SectionHeader(IA(I18nKey.SettingsUI.Design.ShortcutAuxiliarySection).c_str());
				ImGui::PushID(item);
				const std::string shortcutTitle = names[item] + "  (" + combinations[item] + ")";
				events.shortcutsChanged[item] = ToggleRow("##启用固定快捷键", shortcutTitle.c_str(), descriptions[item].c_str(),
					"\ue765", draft.shortcuts[item], item == 5 ? IA(I18nKey.SettingsUI.HotKey.FreezeNote).c_str() : nullptr);
				ImGui::PopID();
			}
		}
		if (section == GeneralSection::DesktopShortcuts)
		{
			using namespace Business::DesktopShortcut;
			const auto& keys = I18nKey.SettingsUI.DesktopShortcut;
			// 两类使用相同控件/动画，只以稳定 ID 与独立草稿区分。
			auto renderCategory = [&](DesktopShortcutDraft& shortcut, const DesktopShortcutDraft& other,
				const char* title, const char* prefix, bool& nameCommitted, bool& presetChanged,
				bool& createRequested, bool& correctChanged, bool& createChanged)
			{
				const auto valid = [&] { return IsDesktopShortcutDraftValid(shortcut, other); };
				const auto error = [&] { return IA(DesktopShortcutDraftNamesCollide(shortcut, other) ? keys.NameConflict : keys.InvalidName); };
				const int custom = CustomPreset(shortcut.names.kind);
				SectionHeader(IA(title).c_str());
				const std::string create = IA(I18nKey.Operate.Create), invalidName = error();
				const std::string nameId = std::string("##") + prefix + "-name";
				InlineRowContent inlineName;
				inlineName.height = [&](float width) {
					return Pixels(ControlHeight) + (valid() ? 0.0F : Pixels(8) + (std::max)(Pixels(20),
						TextHeight(invalidName.c_str(), (std::max)(1.0F, width - Pixels(24)), ImFluentTextStyle_Caption)));
				};
				inlineName.render = [&](const LayoutRect& bounds) {
					ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, { 0, 0 });
					// 透明 child 只承载控件和输入；外框与分隔线由上方 SettingRow 统一绘制。
					if (ImGui::BeginChild(nameId.c_str(), { bounds.Width(), bounds.Height() },
						ImGuiChildFlags_NavFlattened, ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse))
					{
						const auto origin = ImGui::GetCursorScreenPos();
						const float innerWidth = ImGui::GetContentRegionAvail().x;
						const float gap = Pixels(ControlSpacing);
						const float comboWidth = (std::min)(Pixels(300), (std::max)(1.0F, (innerWidth - gap) * .55F));
						const float leftWidth = (std::max)(1.0F, innerWidth - comboWidth - gap);
						std::vector<std::string> names;
						for (int index = 0; index < PresetCount(shortcut.names.kind, shortcut.names.simplifiedChinese); ++index)
						{
							const int preset = PresetForIndex(index, shortcut.names.simplifiedChinese, shortcut.names.kind);
							names.push_back(preset == custom ? IA(keys.CustomName) : utf16ToUtf8(shortcut.names.presets[preset]));
						}
						if (shortcut.preset == custom)
						{
							ImGui::SetNextItemWidth(leftWidth);
							nameCommitted = TextBox("##desktop-shortcut-custom", shortcut.customName,
								IA(keys.CustomHint).c_str(), ImGuiInputTextFlags_EnterReturnsTrue);
							nameCommitted |= ImGui::IsItemDeactivatedAfterEdit();
						}
						else TextAt(IA(keys.NameLabel).c_str(), { origin.x, origin.y + Pixels(5) }, leftWidth);
						ImGui::SetCursorScreenPos({ origin.x + innerWidth - comboWidth, origin.y });
						ImGui::SetNextItemWidth(comboWidth);
						int index = IndexForPreset(shortcut.preset, shortcut.names.simplifiedChinese, shortcut.names.kind);
						const auto previous = ResolveName(shortcut.preset, utf8ToUtf16(shortcut.customName), shortcut.names);
						if (ComboBox("##desktop-shortcut-preset", &index, names, &shortcut.namePopup, I18n::revision()))
						{
							shortcut.preset = PresetForIndex(index, shortcut.names.simplifiedChinese, shortcut.names.kind);
							// 从预设进入自定义时使用当时的显示名；重开已保存的自定义不走此分支。
							if (shortcut.preset == custom) shortcut.customName = utf16ToUtf8(previous);
							presetChanged = true;
						}
						ImGui::SetCursorScreenPos(origin); ImGui::Dummy({ innerWidth, Pixels(ControlHeight) });
						if (!valid()) InfoText(error().c_str());
					}
					ImGui::EndChild(); ImGui::PopStyleVar();
				};
				SettingRow((std::string(prefix) + "-create").c_str(), IA(I18nKey.SettingsUI.Regular.StartUp.Link.N).c_str(),
					IA(I18nKey.SettingsUI.Regular.StartUp.Link.E).c_str(), "\ue71b", ButtonWidth(create.c_str()), ControlHeight,
					[&](const LayoutRect& bounds) {
						ImGui::PushID("create"); ImFluent::BeginDisabled(!valid());
						createRequested = Button((create + "###button").c_str(), { bounds.Width(), bounds.Height() });
						ImFluent::EndDisabled(); ImGui::PopID();
					}, Card, Stroke, 0, IM_COL32_WHITE, false, nullptr, true, &inlineName);
				correctChanged = ToggleRow((std::string(prefix) + "-correct").c_str(),
					IA(I18nKey.SettingsUI.PlugIn.LnkHelper.Capability.FixLnk).c_str(),
					IA(I18nKey.SettingsUI.PlugIn.LnkHelper.Capability.FixLnkE).c_str(), "\ue71b", shortcut.correct);
				createChanged = ToggleRow((std::string(prefix) + "-auto-create").c_str(),
					IA(I18nKey.SettingsUI.PlugIn.LnkHelper.Expansion.CreateLnk).c_str(),
					IA(I18nKey.SettingsUI.PlugIn.LnkHelper.Expansion.CreateLnkE).c_str(), "\ue71b", shortcut.create);
				if (!valid()) { createRequested = false; nameCommitted = false; }
			};
			renderCategory(draft.desktopShortcut, draft.whiteboardShortcut, keys.ApplicationSection, "desktop-shortcut",
				events.desktopNameCommitted, events.desktopNamePresetChanged, events.createShortcut, events.desktopCorrectChanged, events.desktopCreateChanged);
			renderCategory(draft.whiteboardShortcut, draft.desktopShortcut, keys.WhiteboardSection, "whiteboard-shortcut",
				events.whiteboardNameCommitted, events.whiteboardNamePresetChanged, events.createWhiteboardShortcut, events.whiteboardCorrectChanged, events.whiteboardCreateChanged);
		}
		return events;
	}

	HomeAction RenderHome(const HomeContent& content, HomeMotionState* motion)
	{
		HomeAction action = HomeAction::None;
		const auto& palette = GetPalette();
		PageBlockScope heroMotion("##home-hero-motion");
		PageHeader(IA(I18nKey.SettingsUI.Home.N).c_str(), IA(I18nKey.SettingsUI.Design.HomeWelcome).c_str());
		ImGui::SetCursorPosY(ImGui::GetCursorPosY() + Pixels(20.0F));
		const ImVec2 heroOrigin = ImGui::GetCursorScreenPos();
		const float width = ImGui::GetContentRegionAvail().x;
		const float inset = Pixels(width >= Pixels(700.0F) ? 28.0F : 20.0F);
		const float copyWidth = (std::max)(1.0F, width - inset*2);
		const float brandWidth = (std::min)(copyWidth*.8F, Pixels(520));
		const float brandHeight = HomeBrand::Height(brandWidth);
		const std::string heroTitle = IA(I18nKey.SettingsUI.Design.HeroTitle);
		const std::string heroDescription = IA(I18nKey.SettingsUI.Design.HeroDescription);
		const std::string slogan=heroTitle+IA(I18nKey.SettingsUI.Design.HeroJoin)+heroDescription;
		const bool oneLine=TextWidth(slogan.c_str())<=copyWidth;
		const auto first=MeasureInline({{oneLine ? slogan : heroTitle}},copyWidth,ImFluentTextStyle_Body,true);
		const auto second=MeasureInline({{oneLine ? "" : heroDescription}},copyWidth,ImFluentTextStyle_Body,true);
		const std::string openTitle=IA(I18nKey.SettingsUI.Design.HomeOpenSourceTitle);
		const auto openCopy=MeasureInline({{openTitle}},copyWidth,ImFluentTextStyle_BodyStrong,true,16,22);
		const auto repositories=MeasureInline({{IA(I18nKey.SettingsUI.Design.HomeRepositoryPrefix)},
			{IA(I18nKey.SettingsUI.Design.HomeGitHubName),HomeAction::GitHub},
			{IA(I18nKey.SettingsUI.Design.HomeRepositoryJoin)},
			{IA(I18nKey.SettingsUI.Design.HomeGitCodeName),HomeAction::GitCode},
			{IA(I18nKey.SettingsUI.Design.HomeRepositorySuffix)}},copyWidth,ImFluentTextStyle_Caption,true);
		const std::string star=IA(I18nKey.SettingsUI.Design.HomeStarAction), sponsor=IA(I18nKey.SettingsUI.Design.HomeSponsorAction);
		const float buttonWidth=(std::min)(copyWidth,(std::max)(Pixels(156),
			(std::max)(ControlTextWidth(star.c_str()),ControlTextWidth(sponsor.c_str()))+Pixels(56)));
		const bool buttonsInline=buttonWidth*2+Pixels(12)<=copyWidth;
		const float top=Pixels(width>=Pixels(700) ? 56.0F : 40.0F);
		const float heroHeight=top+brandHeight+Pixels(32)+first.height+second.height+Pixels(24+1+20)
			+openCopy.height+Pixels(8)+repositories.height+Pixels(16)+Pixels(buttonsInline ? 36.0F : 80.0F)+Pixels(28);
		ImGui::Dummy({width,heroHeight});
		ImDrawList* draw=ImGui::GetWindowDrawList();
		HeroBackground(draw,heroOrigin,width,heroHeight);
		if (motion)
		{
			motion->heroOrigin=heroOrigin; motion->heroSize={width,heroHeight};
			const auto clip=ImGui::GetCurrentWindow()->ClipRect;
			motion->heroVisible=clip.GetWidth()>0 && clip.GetHeight()>0
				&& ImRect(heroOrigin,{heroOrigin.x+width,heroOrigin.y+heroHeight}).Overlaps(clip);
			motion->brandOrigin={heroOrigin.x+(width-brandWidth)*.5F,heroOrigin.y+top}; motion->brandWidth=brandWidth;
			ResolveHomeEntryLayout(*motion);
			if (motion->entry == HomeMotionState::Entry::Skipped) ResumeSkippedPageOpening();
		}
		DrawHomeBrand(draw,{heroOrigin.x+(width-brandWidth)*.5F,heroOrigin.y+top},brandWidth,motion);
		float y=heroOrigin.y+top+brandHeight+Pixels(32);
		DrawInline("##home-slogan-first",first,{heroOrigin.x+inset,y},ImFluentTextStyle_Body,palette.heroDescription,action,0,0,motion);
		y+=first.height;
		DrawInline("##home-slogan-second",second,{heroOrigin.x+inset,y},ImFluentTextStyle_Body,palette.heroDescription,action,0,0,motion);
		y+=second.height+Pixels(24);
		const float lineWidth=(std::min)(copyWidth,Pixels(640));
		draw->AddLine({heroOrigin.x+(width-lineWidth)*.5F,y},{heroOrigin.x+(width+lineWidth)*.5F,y},palette.heroStroke,Pixels(1));
		y+=Pixels(21);
		DrawInline("##home-open-title",openCopy,{heroOrigin.x+inset,y},ImFluentTextStyle_BodyStrong,TextPrimary,action,16,22,motion);
		y+=openCopy.height+Pixels(8);
		DrawInline("##home-repositories",repositories,{heroOrigin.x+inset,y},ImFluentTextStyle_Caption,TextSecondary,action,0,0,motion);
		y+=repositories.height+Pixels(16);
		const float bx=heroOrigin.x+(width-buttonWidth-(buttonsInline ? buttonWidth+Pixels(12) : 0))*.5F;
		if (HomeIconButton("##home-star",star.c_str(),"\ue734",{bx,y},buttonWidth,false,motion)) action=HomeAction::GitHub;
		if (HomeIconButton("##home-sponsor",sponsor.c_str(),"\ueb51",
			{buttonsInline ? bx+buttonWidth+Pixels(12) : bx,y+Pixels(buttonsInline ? 0.0F : 44.0F)},buttonWidth,true,motion)) action=HomeAction::Support;

		heroMotion.Finish();
		const ImVec2 copyrightOrigin(heroOrigin.x,heroOrigin.y+heroHeight+Pixels(16));
		const std::string copyrightText = IA(I18nKey.SettingsUI.Design.HomeCopyright);
		const std::string redistribution = IA(I18nKey.SettingsUI.Design.HomeRedistribution);
		const float copyrightHeight = TextHeight(copyrightText.c_str(),copyWidth);
		const auto license = MeasureInline({{IA(I18nKey.SettingsUI.Design.HomeLicensePrefix)},
			{IA(I18nKey.SettingsUI.Design.HomeLicenseName),HomeAction::License},
			{IA(I18nKey.SettingsUI.Design.HomeLicenseSuffix)}},copyWidth,ImFluentTextStyle_Caption,true);
		const float redistributionHeight = TextHeight(redistribution.c_str(),copyWidth,ImFluentTextStyle_Caption);
		const float copyrightBlockHeight = copyrightHeight+Pixels(4)+license.height+Pixels(2)+redistributionHeight;
		ImGui::SetCursorScreenPos(copyrightOrigin);
		PageBlockScope copyrightMotion("##home-copyright-motion");
		ImGui::Dummy({width,copyrightBlockHeight});
		const ImVec2 copyrightAfter = ImGui::GetCursorScreenPos();
		y=copyrightOrigin.y;
		CenteredCopy(copyrightText.c_str(),{copyrightOrigin.x+inset,y},copyWidth,ImFluentTextStyle_Body,TextPrimary,action);
		y+=copyrightHeight+Pixels(4);
		DrawInline("##home-license",license,{copyrightOrigin.x+inset,y},ImFluentTextStyle_Caption,TextSecondary,action);
		y+=license.height+Pixels(2);
		CenteredCopy(redistribution.c_str(),{copyrightOrigin.x+inset,y},copyWidth,ImFluentTextStyle_Caption,TextSecondary,action);
		ImGui::SetCursorScreenPos({copyrightAfter.x,copyrightAfter.y+Pixels(24)});
		copyrightMotion.Finish();

		SectionHeader(IA(I18nKey.SettingsUI.Design.Explore).c_str());
		struct Feature { const char* title; const char* description; const char* icon; HomeAction action; ImU32 tint; ImU32 ink; };
		const std::array features{
			Feature{ I18nKey.SettingsUI.Design.GeneralAppearance, I18nKey.SettingsUI.Design.AppearanceDescription, "\ue790", HomeAction::Appearance, palette.featureTint[1], palette.featureInk[1] },
			Feature{ I18nKey.SettingsUI.Draw.N, I18nKey.SettingsUI.Design.DrawDescription, "\uedc6", HomeAction::Draw, palette.featureTint[0], palette.featureInk[0] }
		};
		const bool featureColumns = width >= Pixels(560.0F);
		const float featureWidth = featureColumns ? (width - Pixels(12.0F)) / 2.0F : width;
		float featureHeight = Pixels(164.0F);
		for (const auto& feature : features)
			featureHeight = (std::max)(featureHeight, Pixels(128.0F) + TextHeight(IA(feature.description).c_str(), featureWidth - Pixels(40.0F), ImFluentTextStyle_Caption));
		const ImVec2 featureOrigin = ImGui::GetCursorScreenPos();
		for (size_t index = 0; index < features.size(); ++index)
		{
			const auto& feature = features[index];
			const ImVec2 origin(featureOrigin.x + (featureColumns ? static_cast<float>(index) * (featureWidth + Pixels(12.0F)) : 0.0F),
				featureOrigin.y + (featureColumns ? 0.0F : static_cast<float>(index) * (featureHeight + Pixels(8.0F))));
			ImGui::PushID(static_cast<int>(index));
			ImGui::SetCursorScreenPos(origin);
			PageBlockScope featureMotion("##home-feature-motion");
			if (Button("##feature", { featureWidth, featureHeight })) action = feature.action;
			AllowContentDrag();
			draw->AddRectFilled({ origin.x + 1.0F, origin.y + 1.0F }, { origin.x + featureWidth - 1.0F, origin.y + Pixels(84.0F) }, feature.tint, Pixels(4.0F));
			ImFluent::DrawIcon(feature.icon, origin, { origin.x + featureWidth, origin.y + Pixels(84.0F) }, 36.0F, feature.ink);
			TextAt(IA(feature.title).c_str(), { origin.x + Pixels(16.0F), origin.y + Pixels(100.0F) }, featureWidth - Pixels(48.0F), ImFluentTextStyle_BodyStrong);
			TextAt(IA(feature.description).c_str(), { origin.x + Pixels(16.0F), origin.y + Pixels(124.0F) }, featureWidth - Pixels(40.0F), ImFluentTextStyle_Caption, TextSecondary);
			ImFluent::DrawIcon("\ue76c", { origin.x + featureWidth - Pixels(32.0F), origin.y + Pixels(100.0F) }, { origin.x + featureWidth - Pixels(12.0F), origin.y + Pixels(120.0F) }, 12.0F, TextSecondary);
			ImGui::PopID();
		}
		ImGui::SetCursorScreenPos({ featureOrigin.x, featureOrigin.y + (featureColumns ? featureHeight : featureHeight * 2.0F + Pixels(8.0F)) + Pixels(4.0F) });

		// 宽窗常用设置与作者卡并排；窄窗按阅读顺序纵向排列。
		const bool bottomColumns = width >= Pixels(740.0F);
		const ImVec2 bottomOrigin(featureOrigin.x, ImGui::GetCursorScreenPos().y + Pixels(24.0F));
		const float commonWidth = bottomColumns ? (width - Pixels(20.0F)) * (1.2F / 2.2F) : width;
		ImGui::SetCursorScreenPos(bottomOrigin);
		BeginScrollChild("##home-common", { commonWidth, 0.0F }, ImGuiChildFlags_AutoResizeY);
		Text(IA(I18nKey.SettingsUI.Design.CommonSettings).c_str(), ImFluentTextStyle_BodyStrong);
		ImGui::SetCursorPosY(ImGui::GetCursorPosY() + Pixels(4.0F));
		auto entry = [&](const char* id, const char* title, const char* description, const char* icon, HomeAction target)
		{
			const ImVec2 origin = ImGui::GetCursorScreenPos();
			const float entryWidth = ImGui::GetContentRegionAvail().x;
			const float copyWidth = (std::max)(1.0F, entryWidth - Pixels(92.0F));
			const float titleHeight = TextHeight(title, copyWidth);
			const float descriptionHeight = TextHeight(description, copyWidth, ImFluentTextStyle_Caption);
			const float height = (std::max)(Pixels(60.0F), titleHeight + descriptionHeight + Pixels(24.0F));
			ImGui::PushID(id);
			PageBlockScope entryMotion("##home-common-motion");
			if (Button("##entry", { entryWidth, height })) action = target;
			AllowContentDrag();
			ImFluent::DrawIcon(icon, { origin.x + Pixels(12.0F), origin.y }, { origin.x + Pixels(40.0F), origin.y + height }, 20.0F, TextPrimary);
			const float textY = origin.y + (height - titleHeight - descriptionHeight) * 0.5F;
			TextAt(title, { origin.x + Pixels(52.0F), textY }, copyWidth);
			TextAt(description, { origin.x + Pixels(52.0F), textY + titleHeight }, copyWidth, ImFluentTextStyle_Caption, TextSecondary);
			ImFluent::DrawIcon("\ue76c", { origin.x + entryWidth - Pixels(36.0F), origin.y }, { origin.x + entryWidth - Pixels(12.0F), origin.y + height }, 12.0F, TextSecondary);
			ImGui::PopID();
		};
		entry("home-general", IA(I18nKey.SettingsUI.Design.GeneralBasic).c_str(), IA(I18nKey.SettingsUI.Design.GeneralDescription).c_str(), "\ue713", HomeAction::General);
		entry("home-shortcuts", IA(I18nKey.SettingsUI.HotKey.N).c_str(), IA(I18nKey.SettingsUI.Design.ShortcutsDescription).c_str(), "\ue765", HomeAction::Shortcuts);
		entry("home-plugins", IA(I18nKey.SettingsUI.PlugIn.N).c_str(), IA(I18nKey.SettingsUI.Design.PluginDescription).c_str(), "\uea86", HomeAction::Plugins);
		entry("home-feedback", IA(I18nKey.SettingsUI.Feedback.N).c_str(), IA(I18nKey.SettingsUI.Feedback.Description).c_str(), "\ued15", HomeAction::Feedback);
		ImGui::EndChild();
		const float commonBottom = ImGui::GetItemRectMax().y;
		ImGui::SetCursorScreenPos({ bottomColumns ? bottomOrigin.x + commonWidth + Pixels(20.0F) : bottomOrigin.x,
			bottomColumns ? bottomOrigin.y : commonBottom + Pixels(24.0F) });
		const float authorWidth = bottomColumns ? width - commonWidth - Pixels(20.0F) : width;
		BeginScrollChild("##home-author-section", { authorWidth, 0.0F }, ImGuiChildFlags_AutoResizeY);
		Text(IA(I18nKey.SettingsUI.Design.Improve).c_str(), ImFluentTextStyle_BodyStrong);
		ImGui::SetCursorPosY(ImGui::GetCursorPosY() + Pixels(4.0F));
		const bool authorVisible = BeginCard("##author");
		if (authorVisible)
		{
			const ImVec2 authorOrigin = ImGui::GetCursorScreenPos();
			const float authorCopyWidth = (std::max)(1.0F, ImGui::GetContentRegionAvail().x - Pixels(48.0F));
			const std::string developer = IA(I18nKey.SettingsUI.Home.Developer);
			const float bioHeight = (std::max)(Pixels(36.0F), Pixels(20.0F) + TextHeight(developer.c_str(), authorCopyWidth, ImFluentTextStyle_Caption));
			ImGui::Dummy({ ImGui::GetContentRegionAvail().x, bioHeight });
			ImGui::GetWindowDrawList()->AddCircleFilled({ authorOrigin.x + Pixels(18.0F), authorOrigin.y + Pixels(18.0F) }, Pixels(18.0F), palette.avatarFill);
			if (content.avatarTexture)
				ImGui::GetWindowDrawList()->AddImage(content.avatarTexture, authorOrigin,
					{ authorOrigin.x + Pixels(36.0F), authorOrigin.y + Pixels(36.0F) });
			else
				ImFluent::DrawIcon("\ue77b", authorOrigin, { authorOrigin.x + Pixels(36.0F), authorOrigin.y + Pixels(36.0F) }, 20.0F, TextSecondary);
			TextAt("AlanCRL", { authorOrigin.x + Pixels(48.0F), authorOrigin.y }, authorCopyWidth);
			TextAt(developer.c_str(), { authorOrigin.x + Pixels(48.0F), authorOrigin.y + Pixels(20.0F) }, authorCopyWidth, ImFluentTextStyle_Caption, TextSecondary);
			ImGui::SetCursorPosY(ImGui::GetCursorPosY() + Pixels(12.0F));
			const float qqLabelWidth = TextWidth("QQ");
			TextAt("QQ", ImGui::GetCursorScreenPos(), qqLabelWidth, ImFluentTextStyle_Body, TextSecondary);
			ImGui::Dummy({ qqLabelWidth, Pixels(BodyText.lineHeight) });
			ImGui::SameLine(0.0F, Pixels(8.0F));
			CopyHyperlink("##home-author-qq-copy", "2685549821", "2685549821");
			const float emailLabelWidth = TextWidth("Email");
			TextAt("Email", ImGui::GetCursorScreenPos(), emailLabelWidth, ImFluentTextStyle_Body, TextSecondary);
			ImGui::Dummy({ emailLabelWidth, Pixels(BodyText.lineHeight) });
			ImGui::SameLine(0.0F, Pixels(8.0F));
			CopyHyperlink("##home-author-email-copy", "alan-crl@foxmail.com", "alan-crl@foxmail.com");
			const std::string groupTitle = IA(I18nKey.SettingsUI.Home.Group);
			if (I18n::isIdentifying(L"zh-CN"))
			{
				// 群标题只占自身宽度，让群号链接保持在同一行。
				const float titleWidth = TextWidth(groupTitle.c_str());
				TextAt(groupTitle.c_str(), ImGui::GetCursorScreenPos(), titleWidth, ImFluentTextStyle_Body, TextSecondary);
				ImGui::Dummy({ titleWidth, Pixels(BodyText.lineHeight) });
				ImGui::SameLine(0.0F, Pixels(8.0F));
				Link("home-group", IA(I18nKey.SettingsUI.Home.GroupE).c_str(), HomeAction::Community, action, true);
			}
			else
				Link("home-group", groupTitle.c_str(), HomeAction::Community, action, true);
			ImGui::SetCursorPosY(ImGui::GetCursorPosY() + Pixels(10.0F));
			ImFluent::BeginWrapPanel(Pixels(16.0F), Pixels(8.0F));
			const std::array links{
				std::pair{ I18nKey.SettingsUI.Sponsor.N, HomeAction::Support }
			};
			for (const auto& link : links)
			{
				const std::string label = IA(link.first);
				ImFluent::WrapPanelNextItem(ControlTextWidth(label.c_str()) + Pixels(16.0F));
				Link(link.first, label.c_str(), link.second, action);

			}
			ImFluent::EndWrapPanel();
		}
		EndCard();
		ImGui::EndChild();
		const float authorBottom = ImGui::GetItemRectMax().y;
		ImGui::SetCursorScreenPos({ bottomOrigin.x, (std::max)(commonBottom, authorBottom) + Pixels(20.0F) });

		PageBlockScope footerMotion("##home-footer-motion");
		ImFluent::BeginWrapPanel(Pixels(16.0F), Pixels(8.0F));
		const std::array footer{
			std::pair{ IA(I18nKey.SettingsUI.Design.TermsOfUse), HomeAction::TermsOfUse },
			std::pair{ IA(I18nKey.SettingsUI.Design.Website), HomeAction::Website },
			std::pair{ std::string("GitHub"), HomeAction::GitHub },
			std::pair{ IA(I18nKey.SettingsUI.Home.BilibiliChannel), HomeAction::Bilibili }
		};
		for (const auto& link : footer)
		{
			ImFluent::WrapPanelNextItem(ControlTextWidth(link.first.c_str()) + Pixels(16.0F));
			Link(link.first.c_str(), link.first.c_str(), link.second, action);
		}
		ImFluent::EndWrapPanel();
		footerMotion.Finish();
		Text(IA(I18nKey.SettingsUI.Home.Prompt).c_str(), ImFluentTextStyle_Caption, TextSecondary);
		return action;
	}
}
