#include "Setting.Pages.h"
#include "Setting.HomeBrand.h"
#include "imfluent/imfluent_internal.h"
#include <cstring>
#include "../../../IdtI18n.h"
#include "../../../IdtI18nKeys.g.h"
#include <array>
#include "imgui/imgui_internal.h"

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

		void Link(const char* /*id*/, const char* label, HomeAction kind, HomeAction& action)
		{
			ImGui::PushID(static_cast<int>(kind));
			const std::string stableLabel = std::string(label) + "###link";
			if (HyperlinkButton(stableLabel.c_str())) action = kind;
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

	void ConfigureHomeEntryViewport(HomeMotionState& state, ImVec4 clientContent, ImVec4 contentClip)
	{
		state.clientContent=clientContent; state.contentClip=contentClip;
		state.viewportReady=clientContent.z>clientContent.x && clientContent.w>clientContent.y
			&& contentClip.z>contentClip.x && contentClip.w>contentClip.y;
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
		std::uint64_t fontSerial, int language, ImFont* regular, ImFont* strong, ImFont* icons)
	{
		const auto& io=ImGui::GetIO(); const auto* atlas=io.Fonts;
		HomeEntryFrameKey key;
		key.geometry={io.DisplaySize.x,io.DisplaySize.y,io.DisplayFramebufferScale.x,io.DisplayFramebufferScale.y,
			contentBounds.x,contentBounds.y,contentBounds.z,contentBounds.w,gutter,Pixels(1)};
		key.fonts={reinterpret_cast<std::uintptr_t>(atlas),reinterpret_cast<std::uintptr_t>(regular),
			reinterpret_cast<std::uintptr_t>(strong),reinterpret_cast<std::uintptr_t>(icons)};
		key.epoch=epoch; key.fontSerial=fontSerial; key.language=language; key.dark=IsDarkMode();
		if (atlas->TexData)
		{
			key.texture=atlas->TexData->UniqueID; key.atlasWidth=atlas->TexData->Width; key.atlasHeight=atlas->TexData->Height;
		}
		return key;
	}

	bool BeginHomeEntryFrame(HomeMotionState& state, const HomeEntryFrameKey& key, bool allowFreeze)
	{
		auto& prep=state.preparation; prep.managed=true; ++prep.frame;
		prep.skipBackground=false; prep.fullFrame=true; prep.previousElapsed=state.entryElapsed;
		if (!state.EntryActive() && state.entry!=HomeMotionState::Entry::Pending) return false;
		if (!prep.keyValid || prep.key!=key)
		{
			// DPI/布局/字形图集/资源 epoch 变化时先重走真实页；保留已消费 Show 与动画进度。
			prep.key=key; prep.keyValid=true; prep.ready=false; prep.warmFrames=0;
			prep.revealFrames=0; prep.revealReady=false; state.entryLayoutValidated=false;
		}
		if (!prep.ready || !state.EntryActive()) return false;
		if (!allowFreeze) { FinishHomeEntry(state); return false; }
		const float next=(std::min)(HomeEntryTiming::Total,state.entryElapsed+state.delta);
		if (!prep.revealReady && next>=HomeEntryTiming::DockStart)
		{
			// child 数帧未出现会触发 AutoResize 重新测量；在不透明边界完成两次成功暖帧再淡出。
			prep.revealPending=true; state.entryElapsed=HomeEntryTiming::DockStart;
			return false;
		}
		state.entryElapsed=next;
		if (next>=HomeEntryTiming::Total) { FinishHomeEntry(state); return false; }
		prep.skipBackground=next<HomeEntryTiming::DockStart;
		prep.fullFrame=!prep.skipBackground;
		if (prep.skipBackground) ++prep.skippedFrames;
		return prep.skipBackground;
	}

	bool HomeEntryTexturesReady(const ImDrawData* data)
	{
		if (!data || !data->Textures) return false;
		for (const auto* texture : *data->Textures)
			if (texture->Status==ImTextureStatus_WantCreate || texture->Status==ImTextureStatus_WantUpdates) return false;
		const auto* texture=ImGui::GetIO().Fonts->TexData;
		return texture && texture->Status==ImTextureStatus_OK && texture->GetTexID()!=ImTextureID_Invalid;
	}

	void CompleteHomeEntryFrame(HomeMotionState& state, bool submitted, bool texturesReady)
	{
		auto& prep=state.preparation;
		if (!prep.managed || prep.submitted==prep.frame || !state.EntryActive()) return;
		prep.submitted=prep.frame;
		if (!submitted || !texturesReady)
		{
			// 失败/遮挡帧不消费动画时间；下一次可呈现帧重新确认字体和资源事务。
			state.entryElapsed=prep.previousElapsed; prep.ready=false; prep.warmFrames=0;
			prep.revealFrames=0; prep.revealReady=false;
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
			if (submitted && texturesReady) { if (++prep.revealFrames>=2) prep.revealReady=true; }
			else prep.revealFrames=0;
		}
	}

	void FilterHomeEntryDrawData(const HomeMotionState& state, ImDrawData* data,
		ImDrawList* root, ImDrawList* titleBar, ImDrawList* foreground)
	{
		if (!data || !state.preparation.managed || !state.preparation.ready || !state.EntryActive()
			|| ResolveHomeEntryPose(state).opacity!=1) return;
		// 覆盖 alpha 精确为 1 时底层 GPU 提交没有可见贡献。保留 caption 与单份前景，纹理队列不变。
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

	void RenderHomeEntryOverlay(const HomeMotionState& state)
	{
		if (!state.EntryActive()) return;
		const auto pose=ResolveHomeEntryPose(state); auto* draw=ImGui::GetForegroundDrawList();
		const auto bounds=state.clientContent;
		draw->PushClipRect({bounds.x,bounds.y},{bounds.z,bounds.w},false);
		if (pose.opacity>0)
		{
			const ImU32 solid=AppBase;
			draw->AddRectFilled({bounds.x,bounds.y},{bounds.z,bounds.w},
				(solid & ~IM_COL32_A_MASK)|(static_cast<ImU32>(std::round(pose.opacity*255))<<IM_COL32_A_SHIFT));
		}
		HomeBrand::DrawWriting(draw,pose.origin,pose.width,IsDarkMode(),pose.writing);
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
				if (!state.preparation.managed) state.entryElapsed=(std::min)(HomeEntryTiming::Total,state.entryElapsed+state.delta);
				if (state.entryElapsed>=HomeEntryTiming::Total) FinishHomeEntry(state);
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
			bool fits=contains(state.contentClip,state.heroOrigin,state.heroSize,0);
			// 在同一真实布局上验证完整曲线包络；首帧 AutoResize clip 未测量也不影响前景覆盖。
			HomeMotionState probe; probe.entry=HomeMotionState::Entry::Playing;
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
			else if (!state.preparation.managed) state.entryElapsed=(std::min)(HomeEntryTiming::Total,state.entryElapsed+state.delta);
			if (state.entryElapsed>=HomeEntryTiming::Total) FinishHomeEntry(state);
		}
	}

	void ApplyContentAlpha(ImGuiWindow* window, float alpha)
	{
		// 内容自绘文字/图标也直接写入顶点颜色；统一处理整棵子窗口，避免只淡入库控件。
		if (!window || alpha >= 1.0F) return;
		alpha = std::clamp(alpha, 0.0F, 1.0F);
		for (auto& vertex : window->DrawList->VtxBuffer)
		{
			const ImU32 originalAlpha = (vertex.col >> IM_COL32_A_SHIFT) & 0xff;
			const ImU32 fadedAlpha = static_cast<ImU32>(std::round(originalAlpha * alpha));
			vertex.col = (vertex.col & ~IM_COL32_A_MASK) | (fadedAlpha << IM_COL32_A_SHIFT);
		}
		for (ImGuiWindow* child : window->DC.ChildWindows)
			ApplyContentAlpha(child, alpha);
	}

	GeneralEvents RenderGeneral(GeneralDraft& draft, GeneralSection section)
	{
		GeneralEvents events;
		// 存储页由原配置保存业务分支承载，避免在此重复提交标题或控件。
		if (section == GeneralSection::Storage) return events;
		const char* title = section == GeneralSection::Appearance ? I18nKey.SettingsUI.Design.GeneralAppearance
			: section == GeneralSection::Interaction ? I18nKey.SettingsUI.Design.GeneralInteraction
			: I18nKey.SettingsUI.Design.GeneralBasic;
		PageHeader(IA(title).c_str());
		// 仅渲染当前子页；未显示字段继续使用同一草稿，不触发写回或重置。
		if (section == GeneralSection::Basic)
		{
			SectionHeader(IA(I18nKey.SettingsUI.Regular.StartUp.N).c_str());
			events.startupChanged = ToggleRow("startup", IA(I18nKey.SettingsUI.Regular.StartUp.AutoStart).c_str(),
				IA(I18nKey.SettingsUI.Regular.StartUp.AutoStartE).c_str(), "\ue7e8", draft.startup);
			const std::string more = IA(I18nKey.SettingsUI.Regular.StartUp.Link.More);
			const std::string create = IA(I18nKey.Operate.Create);
			const int shortcutAction = ButtonGroupRow("shortcut", IA(I18nKey.SettingsUI.Regular.StartUp.Link.N).c_str(),
				IA(I18nKey.SettingsUI.Regular.StartUp.Link.E).c_str(), "\ue71b",
				{ { "create", create }, { "more", more } });
			events.createShortcut = shortcutAction == 0;
			events.shortcutOptions = shortcutAction == 1;
			events.startupPreviewChanged = ToggleRow("startup-preview",
				IA(I18nKey.SettingsUI.Regular.StartUp.LoadingAnimation).c_str(),
				nullptr, "\ue768", draft.startupPreview);
		}
		if (section == GeneralSection::Appearance)
		{
			SectionHeader(IA(I18nKey.SettingsUI.Regular.Appearance.N).c_str());
			const float sliderWidth = ImGui::GetContentRegionAvail().x >= Pixels(740.0F) ? 240.0F : 196.0F;
			events.animationChanged = ToggleRow("animation", IA(I18nKey.SettingsUI.Regular.Appearance.AnimationEnable).c_str(),
				IA(I18nKey.SettingsUI.Regular.Appearance.AnimationEnableE).c_str(), "\ue768", draft.animation);
			if (events.animationChanged) ConfigureRowAnimations(0.0F, draft.animation, draft.animationRate);
			// 总开关只控制可见性，隐藏子项时保留它们的保存值。
			if (draft.animation)
			{
				SliderRow("animation-rate", IA(I18nKey.SettingsUI.Regular.Appearance.AnimationRate).c_str(),
					IA(I18nKey.SettingsUI.Regular.Appearance.AnimationRateE).c_str(), "\ue9d9", draft.animationRate,
					0.1F, 5.0F, events.animationRateActive, "%.1fx");
				draft.animationRate = round(draft.animationRate * 10.0F) / 10.0F;
			}
			events.edgeLightingChanged = ToggleRow("edge-lighting", IA(I18nKey.SettingsUI.Regular.Appearance.EdgeLighting).c_str(),
				IA(I18nKey.SettingsUI.Regular.Appearance.EdgeLightingE).c_str(), "\ue706", draft.edgeLighting);
			if (draft.edgeLighting)
				events.dynamicEdgeLightingChanged = ToggleRow("dynamic-edge-lighting",
					IA(I18nKey.SettingsUI.Regular.Appearance.DynamicEdgeLighting).c_str(),
					IA(I18nKey.SettingsUI.Regular.Appearance.DynamicEdgeLightingE).c_str(), "\ue706", draft.dynamicEdgeLighting);
			SettingRow("bar-zoom", IA(I18nKey.SettingsUI.Regular.Appearance.BarZoom.N).c_str(),
				IA(I18nKey.SettingsUI.Regular.Appearance.BarZoom.E).c_str(), "\ue9a6", sliderWidth, 32.0F,
				[&](const LayoutRect& bounds) { events.barZoomChanged = SliderAction(bounds, draft.barZoom, 0.50F, 2.00F, events.barZoomActive); });
			SettingRow("setting-zoom", IA(I18nKey.SettingsUI.Regular.Appearance.SettingUIScale.N).c_str(),
				IA(I18nKey.SettingsUI.Regular.Appearance.SettingUIScale.E).c_str(), "\ue8a9", sliderWidth, 32.0F,
				[&](const LayoutRect& bounds) { SliderAction(bounds, draft.settingZoom, 1.00F, 2.00F, events.settingZoomActive); });
		}
		if (section == GeneralSection::Interaction)
		{
			SectionHeader(IA(I18nKey.SettingsUI.Regular.Behavior.N).c_str());
			events.topIntervalChanged = ComboRow("top-interval", IA(I18nKey.SettingsUI.Regular.Behavior.TopWindow.N).c_str(),
				IA(I18nKey.SettingsUI.Regular.Behavior.TopWindow.E).c_str(), "\ue922", draft.topInterval,
				{ IA(I18nKey.SettingsUI.Regular.Behavior.TopWindow.K_100ms), IA(I18nKey.SettingsUI.Regular.Behavior.TopWindow.K_500ms),
				IA(I18nKey.SettingsUI.Regular.Behavior.TopWindow.K_1s), IA(I18nKey.SettingsUI.Regular.Behavior.TopWindow.K_3s),
				IA(I18nKey.SettingsUI.Regular.Behavior.TopWindow.K_5s), IA(I18nKey.SettingsUI.Regular.Behavior.TopWindow.K_10s),
				IA(I18nKey.SettingsUI.Regular.Behavior.TopWindow.K_30s) });
			events.rightClickCloseChanged = ToggleRow("right-click-close", IA(I18nKey.SettingsUI.Regular.Behavior.RightClickClose).c_str(),
				IA(I18nKey.SettingsUI.Regular.Behavior.RightClickCloseE).c_str(), "\ue8b2", draft.rightClickClose);

			SectionHeader(IA(I18nKey.SettingsUI.Regular.Tentative.N).c_str());
			events.safetyModeChanged = ComboRow("teaching-safety", IA(I18nKey.SettingsUI.Regular.Tentative.SafeMode.N).c_str(),
				IA(I18nKey.SettingsUI.Regular.Tentative.SafeMode.E).c_str(), "\ue72e", draft.safetyMode,
				{ IA(I18nKey.SettingsUI.Regular.Tentative.SafeMode.Mode1), IA(I18nKey.SettingsUI.Regular.Tentative.SafeMode.Mode2),
				IA(I18nKey.SettingsUI.Regular.Tentative.SafeMode.Mode3), IA(I18nKey.SettingsUI.Regular.Tentative.SafeMode.Mode4) });

			SectionHeader(IA(I18nKey.SettingsUI.HotKey.N).c_str());
			Notice("##hotkey-notice", IA(I18nKey.SettingsUI.HotKey.N).c_str(),
				IA(I18nKey.SettingsUI.HotKey.E).c_str(), ImFluentInfoSeverity_Informational);
		}
		return events;
	}

	HomeAction RenderHome(const HomeContent& content, HomeMotionState* motion)
	{
		HomeAction action = HomeAction::None;
		const auto& palette = GetPalette();
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

		const ImVec2 copyrightOrigin(heroOrigin.x,heroOrigin.y+heroHeight+Pixels(16));
		const std::string copyrightText = IA(I18nKey.SettingsUI.Design.HomeCopyright);
		const std::string redistribution = IA(I18nKey.SettingsUI.Design.HomeRedistribution);
		const float copyrightHeight = TextHeight(copyrightText.c_str(),copyWidth);
		const auto license = MeasureInline({{IA(I18nKey.SettingsUI.Design.HomeLicensePrefix)},
			{IA(I18nKey.SettingsUI.Design.HomeLicenseName),HomeAction::License},
			{IA(I18nKey.SettingsUI.Design.HomeLicenseSuffix)}},copyWidth,ImFluentTextStyle_Caption,true);
		const float redistributionHeight = TextHeight(redistribution.c_str(),copyWidth,ImFluentTextStyle_Caption);
		const float copyrightBlockHeight = copyrightHeight+Pixels(4)+license.height+Pixels(2)+redistributionHeight;
		ImGui::SetCursorScreenPos(copyrightOrigin); ImGui::Dummy({width,copyrightBlockHeight});
		const ImVec2 copyrightAfter = ImGui::GetCursorScreenPos();
		y=copyrightOrigin.y;
		CenteredCopy(copyrightText.c_str(),{copyrightOrigin.x+inset,y},copyWidth,ImFluentTextStyle_Body,TextPrimary,action);
		y+=copyrightHeight+Pixels(4);
		DrawInline("##home-license",license,{copyrightOrigin.x+inset,y},ImFluentTextStyle_Caption,TextSecondary,action);
		y+=license.height+Pixels(2);
		CenteredCopy(redistribution.c_str(),{copyrightOrigin.x+inset,y},copyWidth,ImFluentTextStyle_Caption,TextSecondary,action);
		ImGui::SetCursorScreenPos({copyrightAfter.x,copyrightAfter.y+Pixels(24)});

		SectionHeader(IA(I18nKey.SettingsUI.Design.Explore).c_str());
		struct Feature { const char* title; const char* description; const char* icon; HomeAction action; ImU32 tint; ImU32 ink; };
		const std::array features{
			Feature{ I18nKey.SettingsUI.Draw.N, I18nKey.SettingsUI.Design.DrawDescription, "\uedc6", HomeAction::Draw, palette.featureTint[0], palette.featureInk[0] },
			Feature{ I18nKey.SettingsUI.PlugIn.N, I18nKey.SettingsUI.Design.PluginDescription, "\uea86", HomeAction::Plugins, palette.featureTint[2], palette.featureInk[2] }
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
			if (Button("##entry", { entryWidth, height })) action = target;
			AllowContentDrag();
			ImFluent::DrawIcon(icon, { origin.x + Pixels(12.0F), origin.y }, { origin.x + Pixels(40.0F), origin.y + height }, 20.0F, TextPrimary);
			const float textY = origin.y + (height - titleHeight - descriptionHeight) * 0.5F;
			TextAt(title, { origin.x + Pixels(52.0F), textY }, copyWidth);
			TextAt(description, { origin.x + Pixels(52.0F), textY + titleHeight }, copyWidth, ImFluentTextStyle_Caption, TextSecondary);
			ImFluent::DrawIcon("\ue76c", { origin.x + entryWidth - Pixels(36.0F), origin.y }, { origin.x + entryWidth - Pixels(12.0F), origin.y + height }, 12.0F, TextSecondary);
			ImGui::PopID();
		};
		entry("home-general", IA(I18nKey.SettingsUI.Regular.N).c_str(), IA(I18nKey.SettingsUI.Design.GeneralDescription).c_str(), "\ue713", HomeAction::General);
		const char* languageKey = I18n::isIdentifying(L"zh-CN") ? I18nKey.SettingsUI.Language.UI.Language.zh_CN
			: I18n::isIdentifying(L"zh-TW") ? I18nKey.SettingsUI.Language.UI.Language.zh_TW : I18nKey.SettingsUI.Language.UI.Language.en_US;
		entry("home-language", IA(I18nKey.SettingsUI.Design.DisplayLanguage).c_str(), IA(languageKey).c_str(), "\ue774", HomeAction::Language);
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
			Text("QQ: 2685549821", ImFluentTextStyle_Caption, TextSecondary);
			if (HyperlinkButton("alan-crl@foxmail.com###author-email")) action = HomeAction::Contact;
			Text(IA(I18nKey.SettingsUI.Home.Group).c_str(), ImFluentTextStyle_Caption, TextSecondary);
			Link("home-group", IA(I18nKey.SettingsUI.Home.GroupE).c_str(), HomeAction::Community, action);
			ImGui::SetCursorPosY(ImGui::GetCursorPosY() + Pixels(10.0F));
			ImFluent::BeginWrapPanel(Pixels(16.0F), Pixels(8.0F));
			const std::array links{
				std::pair{ I18nKey.SettingsUI.Design.Contact, HomeAction::Contact },
				std::pair{ I18nKey.SettingsUI.Home.FeedBack, HomeAction::Feedback },
				std::pair{ I18nKey.SettingsUI.Sponsor.N, HomeAction::Support }
			};
			for (const auto& link : links)
			{
				const std::string label = IA(link.first);
				ImFluent::WrapPanelNextItem(ControlTextWidth(label.c_str()) + Pixels(16.0F));
				Link(link.first, label.c_str(), link.second, action);
				if (link.second == HomeAction::Feedback && ImGui::IsItemHovered() && content.feedbackTexture)
				{
					ImGui::BeginTooltip();
					ImGui::Image(content.feedbackTexture, { Pixels(140.0F), Pixels(140.0F) * content.feedbackAspect });
					ImGui::EndTooltip();
				}
			}
			ImFluent::EndWrapPanel();
		}
		EndCard();
		ImGui::EndChild();
		const float authorBottom = ImGui::GetItemRectMax().y;
		ImGui::SetCursorScreenPos({ bottomOrigin.x, (std::max)(commonBottom, authorBottom) + Pixels(20.0F) });

		ImFluent::BeginWrapPanel(Pixels(16.0F), Pixels(8.0F));
		const std::array footer{
			std::pair{ IA(I18nKey.SettingsUI.Design.Website), HomeAction::Website },
			std::pair{ std::string("GitHub"), HomeAction::GitHub },
			std::pair{ IA(I18nKey.SettingsUI.Home.BilibiliChannel), HomeAction::Bilibili },
			std::pair{ std::string(content.version), HomeAction::Version }
		};
		for (const auto& link : footer)
		{
			ImFluent::WrapPanelNextItem(ControlTextWidth(link.first.c_str()) + Pixels(16.0F));
			Link(link.first.c_str(), link.first.c_str(), link.second, action);
		}
		ImFluent::EndWrapPanel();
		Text(IA(I18nKey.SettingsUI.Home.Prompt).c_str(), ImFluentTextStyle_Caption, TextSecondary);
		return action;
	}
}
