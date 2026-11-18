module;

#include <windows.h>
#include <d2d1_1.h>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <functional>
#include <vector>

export module Inkeys.UI.UI3:Lighting;

namespace Inkeys::UI::UI3
{
export inline constexpr double BorderCursorLightRadius = 240.0;
export inline constexpr double BorderLightIntensity = 1.0;

// Surface 只消费 Main Bar 已经计算完成的两路光源，不复制第三光源状态机。
export struct UiFrameLightingSnapshot
{
	D2D1_POINT_2F primaryLight = D2D1::Point2F();
	FLOAT primaryRadius = 0.0F;
	D2D1_POINT_2F cursorLight = D2D1::Point2F();
	D2D1_POINT_2F cursorScreenLight = D2D1::Point2F();
	FLOAT cursorRadius = 0.0F;
	FLOAT cursorIntensity = 0.0F;
	COLORREF drawingPenColor = RGB(0, 0, 0);
	double drawingPenColorBlend = 0.0;
	double drawingLightOpacity = 1.0;
	bool primaryLightVisible = false;
	bool cursorLightVisible = false;
	bool edgeLightingEnabled = false;
};

// 产品层解析后的每帧输入；锚点和颜色源 id 只用于识别切换。
export struct UiFrameLightingInput
{
	D2D1_POINT_2F primaryAnchor{};
	bool primaryAnchorAvailable = false;
	int primaryAnchorId = 0;
	D2D1_POINT_2F cursorScreenPoint{};
	unsigned long long cursorSerial = 0;
	bool cursorAvailable = false;
	COLORREF initialAccentColor = RGB(0, 0, 0);
	COLORREF accentColor = RGB(0, 0, 0);
	bool usesAccentColor = false;
	int modeId = -1;
	int colorSourceId = -1;
	bool colorSourceCarriesHistory = false;
	bool takeOverAccentColor = false;
	int historyResetSourceId = -1;
	bool edgeLightingEnabled = false;
	bool dynamicEdgeLightingEnabled = false;
};

// 主栏推进唯一状态机，其他表面只消费帧快照。
export class LightingStateMachine
{
public:
	bool PrepareFrameLighting(double animationDtSeconds, POINT layoutScreenOrigin,
		const UiFrameLightingInput& input);
	[[nodiscard]] RECT GetFramePrimaryLightDamageBounds() const noexcept;
	[[nodiscard]] RECT GetFrameCursorLightDamageBounds() const noexcept;
	[[nodiscard]] bool DidFramePrimaryLightChange() const noexcept
	{
		return framePrimaryLightChanged;
	}
	[[nodiscard]] bool DidFrameCursorLightChange() const noexcept
	{
		return frameCursorLightChanged;
	}
	[[nodiscard]] UiFrameLightingSnapshot
		SnapshotFrameLighting() const noexcept;
	void SetFrameZoom(double zoom) noexcept
	{ frameZoom = std::isfinite(zoom) && zoom > 0.0 ? zoom : 1.0; }
private:
	D2D1_POINT_2F framePrimaryLight = D2D1::Point2F();
	D2D1_POINT_2F framePrimaryLightStart = D2D1::Point2F();
	D2D1_POINT_2F framePrimaryLightTarget = D2D1::Point2F();
	D2D1_POINT_2F frameCursorLight = D2D1::Point2F();
	D2D1_POINT_2F frameCursorScreenLight = D2D1::Point2F();
	FLOAT frameCursorLightIntensity = 0.0F;
	FLOAT frameCursorLightIntensityStart = 0.0F;
	FLOAT frameCursorLightIntensityTarget = 0.0F;
	FLOAT frameLightRadius = 0.0F;
	FLOAT frameCursorLightRadius = 0.0F;
	int framePrimaryLightAnchor = 0;
	bool framePrimaryLightAnchorInitialized = false;
	bool framePrimaryLightAnimating = false;
	bool frameCursorLightVisible = false;
	bool frameCursorLightAnimating = false;
	bool framePrimaryLightChanged = false;
	bool frameCursorLightChanged = false;
	bool framePrimaryLightWasAnimating = false;
	bool frameCursorLightWasAnimating = false;
	bool frameAnimationStateInitialized = false;
	bool frameLastAnimationEnabled = false;
	bool frameCursorInputAvailable = false;
	bool frameLightingWasAnimating = false;
	bool frameEdgeLightingEnabled = false;
	double frameZoom = 1.0;
	double framePrimaryLightMoveElapsed = 0.0;
	double frameCursorLightFadeElapsed = 0.0;
	double frameDrawingPenColorElapsed = 0.0;
	double frameDrawingModeTransitionElapsed = 0.0;
	double frameDrawingPenColorBlend = 0.0;
	double frameDrawingPenColorBlendStart = 0.0;
	double frameDrawingPenColorBlendTarget = 0.0;
	double frameDrawingLightOpacity = 1.0;
	double frameDrawingLightOpacityStart = 1.0;
	unsigned long long handledBorderCursorLightSerial = 0;
	COLORREF frameDrawingPenColor = RGB(0, 0, 0);
	COLORREF frameDrawingPenColorStart = RGB(0, 0, 0);
	COLORREF frameDrawingPenColorTarget = RGB(0, 0, 0);
	bool frameDrawingUsesPenColor = false;
	bool frameDrawingPenColorInitialized = false;
	bool frameDrawingPenColorAnimating = false;
	bool frameDrawingModeInitialized = false;
	bool frameDrawingModeTransitionAnimating = false;
	bool frameDrawingPenColorSourceInitialized = false;
	bool frameDrawingPenColorCarriesHighlighterHistory = false;
	int frameDrawingMode = -1;
	int frameDrawingPenColorSource = -1;
};

export struct SurfaceSharedLighting
{
	double primaryScreenX = 0, primaryScreenY = 0, primaryRadiusPixels = 0;
	double cursorScreenX = 0, cursorScreenY = 0, cursorRadiusPixels = 0;
	double cursorIntensity = 0;
	COLORREF drawingPenColor = RGB(0, 0, 0);
	double drawingPenColorBlend = 0, drawingLightOpacity = 1;
	bool primaryVisible = false, cursorVisible = false, edgeLightingEnabled = false;
};

// 回调在锁内按值快照；锁外通知不再访问订阅者 owner。
export struct LightingBorderBounds
{
	RECT outer{}, inner{};
};
export struct LightingSubscriptionTarget
{
	std::vector<LightingBorderBounds> borders;
	double damageOutsetPixels = 0;
	bool enabled = false, allChanges = false;
	std::function<void()> notify;
};
export using LightingSubscriberToken = std::uint64_t;
export struct SharedLightingSnapshot
{
	SurfaceSharedLighting lighting;
	std::uint64_t revision = 1;
};
export LightingSubscriberToken AddSharedLightingSubscriber(
	std::function<LightingSubscriptionTarget()> queryTarget);
export void RemoveSharedLightingSubscriber(LightingSubscriberToken token) noexcept;
export void PublishSharedLighting(const SurfaceSharedLighting& value) noexcept;
export SharedLightingSnapshot ReadSharedLighting() noexcept;
export UiFrameLightingSnapshot ResolveSurfaceFrameLightingSnapshot(
	const SurfaceSharedLighting& lighting, const RECT& bounds, LONG outset) noexcept;

// 每条通知单独唤醒并按序号读取，宿主可保持它与自身输入的原队列顺序。
export enum class LightingPointerEventKind { Entered, Left, Moved };
export struct LightingPointerNotification
{
	LightingPointerEventKind kind = LightingPointerEventKind::Moved;
	std::uintptr_t source = 0;
	POINT screenPoint{};
	bool screenPointAvailable = false;
	std::uint64_t sourceSerial = 0, serial = 0;
};
export void SetLightingPublisherWake(std::function<void(std::uint64_t)> wake);
export void NotifyLightingPointer(LightingPointerNotification notification) noexcept;
export bool ReadLightingPointerNotification(std::uint64_t serial,
	LightingPointerNotification& notification) noexcept;
}
