module;
#include <algorithm>
#include <cmath>
#include <optional>

export module Inkeys.UI.Bar.EraserAttributeMotion;
export import Inkeys.UI.Bar.EraserAttributeLayout;
import Inkeys.UI.Bar.Animation;

export namespace Inkeys::UI::Bar
{
	struct EraserSurfacePose
	{
		double scale=1,x=0,y=0;
		EraserAttributeRect Apply(EraserAttributeRect r) const noexcept
		{ return {r.left*scale+x,r.top*scale+y,r.right*scale+x,r.bottom*scale+y}; }
		friend bool operator==(const EraserSurfacePose&,const EraserSurfacePose&)=default;
	};
	inline EraserSurfacePose ComposeEraserPose(EraserSurfacePose parent,EraserSurfacePose child) noexcept
	{ return {parent.scale*child.scale,parent.x+child.x*parent.scale,parent.y+child.y*parent.scale}; }
	inline EraserSurfacePose ResolveEraserSurfacePose(EraserAttributeRect panel,EraserAttributeRect anchor,
		double geometry,double compactWidth) noexcept
	{
		const double compact=(std::clamp)(compactWidth/(std::max)(1.0,panel.Width()),0.01,1.0);
		// Back几何不裁到0..1；只防非正缩放，alpha在独立时间线上有界。
		const double scale=(std::max)(0.001,compact+(1-compact)*geometry);
		const double cx=EraserRectCenterX(anchor)+(EraserRectCenterX(panel)-EraserRectCenterX(anchor))*geometry;
		const double cy=EraserRectCenterY(anchor)+(EraserRectCenterY(panel)-EraserRectCenterY(anchor))*geometry;
		return {scale,cx-EraserRectCenterX(panel)*scale,cy-EraserRectCenterY(panel)*scale};
	}
	inline EraserSurfacePose FitEraserSurfacePose(EraserSurfacePose pose,EraserAttributeRect shell,EraserAttributeRect work) noexcept
	{
		const auto actual=pose.Apply(shell);
		if(actual.Width()<=work.Width())pose.x+=(std::clamp)(actual.left,work.left,work.right-actual.Width())-actual.left;
		if(actual.Height()<=work.Height())pose.y+=(std::clamp)(actual.top,work.top,work.bottom-actual.Height())-actual.top;
		return pose;
	}
	// 分页、属性面板可共用的保守回弹包络；父/子矩形使用同一曲线极值与姿态组合。
	inline EraserAttributeRect ResolveEraserPopupEnvelope(EraserAttributeRect panel,EraserAttributeRect anchor,
		double compactWidth, std::optional<EraserAttributeRect> parentPanel=std::nullopt,
		std::optional<EraserAttributeRect> parentAnchor=std::nullopt) noexcept
	{
		const auto range=BarUiGetCurveExtrema(BarUiCurveEnum::EaseOutBack);
		const double phases[]={0.0,1.0,range.maximum,1.0-range.maximum};
		EraserAttributeRect envelope=panel;
		for(const double child:phases) for(const double parent:phases) {
			auto pose=ResolveEraserSurfacePose(panel,anchor,child,compactWidth);
			if(parentPanel && parentAnchor) pose=ComposeEraserPose(
				ResolveEraserSurfacePose(*parentPanel,*parentAnchor,parent,compactWidth),pose);
			const auto r=pose.Apply(panel);
			envelope.left=(std::min)(envelope.left,r.left); envelope.top=(std::min)(envelope.top,r.top);
			envelope.right=(std::max)(envelope.right,r.right); envelope.bottom=(std::max)(envelope.bottom,r.bottom);
		}
		return envelope;
	}
	class BarAttributePanelMotion
	{
	public:
		bool Retarget(bool open,double duration,const BarUiTimelineClass* parent=nullptr)
		{
			if(open==requested_)return false;
			requested_=open;double phase=0;
			if(parent && parent->CanJoin()){duration=parent->GetRemainingDuration();phase=parent->GetProgress();}
			const auto geometry=open?BarUiCurveEnum::EaseOutBack:BarUiCurveEnum::EaseInBack;
			const auto opacity=open?BarUiCurveEnum::EaseOutSine:BarUiCurveEnum::EaseInSine;
			geometry_.SetTar(open?1:0,duration,std::nullopt,false,{geometry,geometry,phase,phase>0});
			opacity_.SetTar(open?1:0,duration,std::nullopt,false,{opacity,opacity,phase,phase>0});
			timeline_.Restart(duration);return true;
		}
		bool Advance(const BarUiAnimationAdvanceContextClass& context)
		{
			// 同一帧内反向只改目标；底层把dt<=0当作立即完成，此处保留当前姿态。
			if(context.animationEnabled && !context.forceReplace && timeline_.IsActive() && (!std::isfinite(context.dtSeconds) || context.dtSeconds<=0))return true;
			const auto g=BarUiAdvanceAnimation(geometry_,context),a=BarUiAdvanceAnimation(opacity_,context);
			if(!context.animationEnabled)timeline_.Restart(0);else timeline_.Advance(context.dtSeconds,context.speedRate);
			// 默认时长恰好落在整帧边界时消除浮点尾差，确保换边中点和绘制属性落在同一帧。
			if(timeline_.IsActive() && timeline_.GetRemainingDuration()<=0.000000001)
			{
				timeline_.Restart(0);geometry_.SetDirect(requested_?1:0);opacity_.SetDirect(requested_?1:0);
				return true;
			}
			return g.changed || g.active || a.changed || a.active || timeline_.IsActive();
		}
		bool Active() const noexcept { return timeline_.IsActive(); }
		bool Visible() const noexcept { return requested_ || timeline_.IsActive() || static_cast<double>(opacity_.val)>0; }
		double Geometry() const noexcept { return geometry_.val; }
		double TargetGeometry() const noexcept { return geometry_.tar; }
		double Opacity() const noexcept { return (std::clamp)(static_cast<double>(opacity_.val),0.0,1.0); }
		const BarUiTimelineClass& Timeline() const noexcept { return timeline_; }
	private:
		bool requested_=false;
		BarUiValueClass geometry_{0.0};
		BarUiPctClass opacity_{0.0};
		BarUiTimelineClass timeline_;
	};
	using EraserSurfaceMotion = BarAttributePanelMotion;
	struct BarAttributePanelFrame
	{
		EraserSurfacePose pose, targetPose;
		double opacity = 0.0;
		bool visible = false;
	};
	// 固定属性与白板面板统一从触发按钮解析几何、父级姿态及透明度，不各自编排递归图。
	inline BarAttributePanelFrame ResolveBarAttributePanelFrame(
		const BarAttributePanelMotion& motion, EraserAttributeRect panel,
		EraserAttributeRect anchor, double compactWidth,
		const BarAttributePanelFrame* parent = nullptr) noexcept
	{
		BarAttributePanelFrame frame;
		frame.pose = ResolveEraserSurfacePose(panel, anchor, motion.Geometry(), compactWidth);
		frame.targetPose = ResolveEraserSurfacePose(panel, anchor, motion.TargetGeometry(), compactWidth);
		frame.opacity = motion.Opacity();
		frame.visible = motion.Visible() && frame.opacity > 0.000001;
		if (parent)
		{
			frame.pose = ComposeEraserPose(parent->pose, frame.pose);
			frame.targetPose = ComposeEraserPose(parent->targetPose, frame.targetPose);
			frame.opacity *= parent->opacity;
			frame.visible = frame.visible && parent->visible && frame.opacity > 0.000001;
		}
		return frame;
	}
	struct EraserAttributePresentation
	{
		EraserAttributeLayout geometry;
		EraserSurfacePose panelPose,menuPose;
		double zoom=1,panelOpacity=0,menuOpacity=0;
		bool panelVisible=false,menuVisible=false;
	};
	inline EraserAttributeLayout TransformEraserAttributeLayout(EraserAttributeLayout layout,EraserSurfacePose panel,EraserSurfacePose menu) noexcept
	{
		layout.panel=panel.Apply(layout.panel);layout.menu=menu.Apply(layout.menu);layout.automatic=panel.Apply(layout.automatic);
		layout.automaticDivider=panel.Apply(layout.automaticDivider);
		layout.previewRegion=panel.Apply(layout.previewRegion);layout.menuTitle=menu.Apply(layout.menuTitle);
		for(size_t i=0;i<layout.items.size();++i)layout.items[i]=(i<6?panel:menu).Apply(layout.items[i]);
		for(auto& r:layout.dividers)r=panel.Apply(r);
		for(size_t i=0;i<3;++i){layout.previews[i]=panel.Apply(layout.previews[i]);layout.previewClips[i]=panel.Apply(layout.previewClips[i]);layout.previewDiameters[i]*=panel.scale;}
		return layout;
	}
}
