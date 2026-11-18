module;

#include <d2d1_1.h>
#include <d2d1_1helper.h>
#include <array>
#include <optional>
#include <string>
#include <utility>

export module Inkeys.UI.UI3:Panel;

export import Inkeys.UI.UI3.PanelStack;
import Inkeys.UI.UI3.Animation;
import :Primitive;
import :Renderer;
import :Lighting;

export namespace Inkeys::UI::UI3
{
struct PanelMotionSpec
{
	double openDuration = .4, closeDuration = .4;
	UiCurveEnum openGeometry = UiCurveEnum::EaseOutBack, closeGeometry = UiCurveEnum::EaseInBack;
	UiCurveEnum openOpacity = UiCurveEnum::EaseOutSine, closeOpacity = UiCurveEnum::EaseInSine;
	bool opacityFollowsGeometry = false;
};

class Panel : public PanelState
{
public:
	explicit Panel(std::string identifier) : identifier_(std::move(identifier)) {}
	void BindFrame(UiShapeClass& shape) noexcept { shape_ = &shape; }
	using PanelState::SetCandidate;
	void SetCandidate(RECT visual, PanelRegion interaction, bool visible) noexcept;
	RECT CandidateBounds() const noexcept;
	RECT PresentedInteractionBounds() const noexcept;
	bool Retarget(bool open, const PanelMotionSpec& spec = {});
	void Advance(const UiAnimationAdvanceContextClass& context);
	// 专用 motion 保留在拥有者；只同步结果，不再推进第二条动画时间线。
	void SetExternalState(bool open, double geometry, double opacity);
	double Geometry() const noexcept { return geometry_; }
	double Opacity() const noexcept { return opacity_; }
	bool DrawFrame(UIRendering&, ID2D1DeviceContext*, const UiInheritClass&,
		RECT* target = nullptr, bool clip = false, std::optional<D2D1_RECT_F> contentClip = std::nullopt) const;
private:
	bool SetTarget(bool open);
	std::string identifier_;
	UiShapeClass* shape_ = nullptr;
	bool requested_ = false;
	PanelMotionSpec spec_;
	UiValueClass geometryValue_{0};
	UiPctClass opacityValue_{0};
	double geometry_ = 0, opacity_ = 0;
};

struct PanelPose
{
	double zoom = 1, fontTargetZoom = 1, fontTargetScale = 1;
	double diffuseReferenceZoom = 0, diffuseGeometryScale = 1;
	D2D1_MATRIX_3X2_F transform = D2D1::Matrix3x2F::Identity();
	std::optional<UiFrameLightingSnapshot> lighting;
	bool edgeLighting = true;
};

class PanelPoseScope
{
public:
	PanelPoseScope(UIRendering&, ID2D1DeviceContext*, const PanelPose&);
	~PanelPoseScope();
	PanelPoseScope(const PanelPoseScope&) = delete;
	PanelPoseScope& operator=(const PanelPoseScope&) = delete;
private:
	UIRendering& renderer_;
	ID2D1DeviceContext* context_;
	D2D1_MATRIX_3X2_F transform_;
	double zoom_, fontZoom_, referenceZoom_, geometryScale_;
	std::array<double, 2> fontY_;
	bool fontReady_, edgeAllowed_, primaryInitialized_, cursorVisible_;
	UiFrameLightingSnapshot lighting_;
	D2D1_POINT_2F cursorLocal_;
	FLOAT cursorRadiusX_, cursorRadiusY_;
};
}
