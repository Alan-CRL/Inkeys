module;

#include "../../../IdtMain.h"

#include <d2d1_1.h>
#include <dwrite_1.h>
#include <wrl/client.h>
#include <array>
#include <cstdint>
#include "Bar.BottomDock.h"

export module Inkeys.UI.Bar:Main;

import :UI;
import :State;
import :Button;
import :Format;
export import :Rendering;
import :RenderingAttribute;

import Inkeys.UI.Bar.Animation;
export import Inkeys.UI.Bar.ToggleClickCoalescer;

import Inkeys.Conv.Color;
import Inkeys.Helper.Thread;
import Inkeys.Message;
import Inkeys.Display;

// ====================
// 动画

IdtAtomic<bool> BarUiEdgeLightingEnabled = true;
IdtAtomic<bool> BarUiDynamicEdgeLightingEnabled = true;
IdtAtomic<bool> BarUiDebugModeEnabled = false;
IdtAtomic<bool> BarUiDebugFrameRateEnabled = true;

// ====================
// 窗口

LRESULT CALLBACK barWindowMsgCallback(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

// 窗口模态信息
class BarWindowPosClass
{
public:
	IdtAtomic<int> x = 0, y = 0;
	IdtAtomic<unsigned int> w = 0, h = 0;
	IdtAtomic<unsigned int> pct = 255; // 透明度
};

// ====================
// 媒体

// 媒体操控类
class BarMediaClass
{
public:
	void LoadFormat();

public:
	unique_ptr<BarFormatCache> formatCache;

protected:
};

// ====================
// 界面

// 前向声明
class BarUISetClass;
class BarRenderLoopCoordinator;
// 控件枚举
enum class BarUISetShapeEnum : int
{
	MainBar,
	MorePanel,
	MorePanelDivider,
	MorePanelCloseHit,

	DrawAttributeBar,
	DrawAttributeBar_ColorSelect1,
	DrawAttributeBar_ColorSelect2,
	DrawAttributeBar_ColorSelect3,
	DrawAttributeBar_ColorSelect4,
	DrawAttributeBar_ColorSelect5,
	DrawAttributeBar_ColorSelect6,
	DrawAttributeBar_ColorSelect7,
	DrawAttributeBar_ColorSelect8,
	DrawAttributeBar_ColorSelect9,
	DrawAttributeBar_ColorSelect10,
	DrawAttributeBar_ColorSelect11,
	DrawAttributeBar_ColorSelect12,
	DrawAttributeBar_Brush1,
	DrawAttributeBar_Highlight1,
	DrawAttributeBar_Laser,
	DrawAttributeBar_Brush2,
	DrawAttributeBar_SoftPen,
	DrawAttributeBar_PenTypeExtensionHit,
	DrawAttributeBar_PenTypeExtensionDivider,
	DrawAttributeBar_ThicknessSelect,
	DrawAttributeBar_ThicknessDivider,
	DrawAttributeBar_ThicknessFine,
	DrawAttributeBar_ThicknessMedium,
	DrawAttributeBar_ThicknessCoarse,
	DrawAttributeBar_ThicknessAdjust,
	DrawAttributeBar_ThicknessSliderHit,
	DrawAttributeBar_ThicknessSliderThumb,
	DrawAttributeBar_ThicknessPreviewPopupSurface,
	DrawAttributeBar_ThicknessPreviewPopupCircle,
	DrawAttributeBar_ThicknessAnnotationInfoHit,
	DrawAttributeBar_ThicknessOverflowBadge,
	DrawAttributeBar_ThicknessOverflowInfoHit,
	DrawAttributeBar_ThicknessAnnotationPopup,
	DrawAttributeBar_ThicknessAnnotationPopupCloseHit,
	DrawAttributeBar_ThicknessOverflowPopup,
	DrawAttributeBar_ThicknessOverflowPopupCloseHit,
	DrawAttributeBar_PenTypeMenu,
	DrawAttributeBar_PenTypeMenuFreeLine,
	DrawAttributeBar_PenTypeMenuAnnotationLine,
	DrawAttributeBar_ColorPickerPanel,
	DrawAttributeBar_ColorPickerPalette,
	DrawAttributeBar_ColorPickerToneToggle,
	DrawAttributeBar_ColorPickerCloseHit,
	DrawAttributeBar_ColorPickerPreviewBubble,
	DrawAttributeBar_ColorPickerHoldHint,
	DrawAttributeBar_ColorSelect12Inner,

	GeometryAttributeBar,
	GeometryAttributeBar_Divider,
	GeometryAttributeBar_StraightLine,
	GeometryAttributeBar_Rectangle,
	GeometryAttributeBar_ThicknessFine,
	GeometryAttributeBar_ThicknessMedium,
	GeometryAttributeBar_ThicknessCoarse,
	GeometryAttributeBar_Close,
};
enum class BarUISetSuperellipseEnum : int
{
	MainButton,
};
enum class BarUISetSvgEnum : int
{
	logo1,
	logoInk,
	MorePanelClose,

	DrawAttributeBar_ColorSelect1,
	DrawAttributeBar_ColorSelect2,
	DrawAttributeBar_ColorSelect3,
	DrawAttributeBar_ColorSelect4,
	DrawAttributeBar_ColorSelect5,
	DrawAttributeBar_ColorSelect6,
	DrawAttributeBar_ColorSelect7,
	DrawAttributeBar_ColorSelect8,
	DrawAttributeBar_ColorSelect9,
	DrawAttributeBar_ColorSelect10,
	DrawAttributeBar_ColorSelect11,

	DrawAttributeBar_Brush1,
	DrawAttributeBar_Highlight1,
	DrawAttributeBar_Laser,
	DrawAttributeBar_Brush2,
	DrawAttributeBar_SoftPen,
	DrawAttributeBar_PenTypeExtensionArrow,
	DrawAttributeBar_ThicknessAdjust,
	DrawAttributeBar_PenTypeMenuCheck,
	DrawAttributeBar_ThicknessAnnotationInfo,
	DrawAttributeBar_ThicknessOverflowInfo,
	DrawAttributeBar_ThicknessAnnotationPopupClose,
	DrawAttributeBar_ThicknessOverflowPopupClose,
	DrawAttributeBar_ColorSelect12Check,
	DrawAttributeBar_ColorPickerToneSun,
	DrawAttributeBar_ColorPickerToneMoon,
	GeometryAttributeBar_StraightLine,
	GeometryAttributeBar_Rectangle,
	GeometryAttributeBar_Close,
};
enum class BarUISetPngEnum : int
{
	DrawAttributeBar_ColorSelect12Wheel,
};
enum class BarUISetWordEnum : int
{
	BackgroundWarning,

	MainButton,

	DrawAttributeBar_Brush1,
	DrawAttributeBar_Highlight1,
	DrawAttributeBar_Laser,
	DrawAttributeBar_Brush2,
	DrawAttributeBar_SoftPen,
	DrawAttributeBar_ThicknessDisplay,
	DrawAttributeBar_ThicknessFineNumber,
	DrawAttributeBar_ThicknessMediumNumber,
	DrawAttributeBar_ThicknessCoarseNumber,
	DrawAttributeBar_PenTypeMenuFreeLine,
	DrawAttributeBar_ThicknessAnnotationLabel,
		DrawAttributeBar_ThicknessHoldLockLabel,
		DrawAttributeBar_ThicknessAnnotationPopupText,
		DrawAttributeBar_ThicknessAnnotationPopupBody,
		DrawAttributeBar_ThicknessOverflowPopupText,
		DrawAttributeBar_ThicknessOverflowPopupBody,
		DrawAttributeBar_ColorPickerRgb,
		DrawAttributeBar_ColorPickerG,
		DrawAttributeBar_ColorPickerB,
		DrawAttributeBar_ColorPickerOpacity,
		DrawAttributeBar_ColorPickerRgbValue,
		DrawAttributeBar_ColorPickerGValue,
		DrawAttributeBar_ColorPickerBValue,
		DrawAttributeBar_ColorPickerOpacityValue,
		DrawAttributeBar_ColorPickerHoldLabel,
		DrawAttributeBar_ThicknessPreviewPopupNumber,

		GeometryAttributeBar_StraightLine,
		GeometryAttributeBar_Rectangle,
		GeometryAttributeBar_ThicknessFineNumber,
		GeometryAttributeBar_ThicknessMediumNumber,
		GeometryAttributeBar_ThicknessCoarseNumber,
	};

enum class BarBorderCursorTrackingStateEnum : int
{
	Dormant,
	Inside,
	Grace,
};

enum class BarDirectWindowDragPhase : std::uint8_t
{
	Idle,
	Dragging,
	Absorbing,
};

struct BarSeekResult
{
	double rawPathLength = 0.0;
	bool moved = false;
	bool captured = false;
	bool detached = false;
	bool modeChanged = false;
	bool allowClick = true;
};

struct BarPendingDisplaySnapshot
{
	RECT bounds{ 0, 0, 1, 1 };
	RECT workArea{ 0, 0, 1, 1 };
	UINT dpi = USER_DEFAULT_SCREEN_DPI;
	unsigned long long serial = 0;
};

struct BarBottomDockPresentedSnapshot
{
	Inkeys::UI::Bar::BarBottomDockMode mode =
		Inkeys::UI::Bar::BarBottomDockMode::BottomDocked;
	Inkeys::UI::Bar::BarBottomDockPhase phase =
		Inkeys::UI::Bar::BarBottomDockPhase::Stable;
	bool recoveryActive = false;
	Inkeys::UI::Bar::BarBottomDockVerticalMapping mapping{};
	Inkeys::UI::Bar::BarBottomDockCenterMode centerMode =
		Inkeys::UI::Bar::BarBottomDockCenterMode::Centered;
	Inkeys::UI::Bar::BarBottomDockPhase centerPhase =
		Inkeys::UI::Bar::BarBottomDockPhase::Stable;
	Inkeys::UI::Bar::BarBottomDockHorizontalMapping horizontalMapping{};
	double centerElasticOffsetDip = 0.0;
	double elasticOffsetDip = 0.0;
	double rigidTranslationDip = 0.0;
	double zoom = 1.0;
		POINT monitorOrigin{};
		RECT monitorBounds{ 0, 0, 1, 1 };
		RECT workArea{ 0, 0, 1, 1 };
		UINT dpi = USER_DEFAULT_SCREEN_DPI;
		unsigned long long displaySerial = 0;
	POINT directTranslation{};
	double mainCenterScreenX = 0.0;
	double mainCenterScreenY = 0.0;
	double rawMainCenterScreenX = 0.0;
	double bodyCenterScreenX = 0.0;
	double rawBodyCenterScreenX = 0.0;
	bool indicatorVisible = false;
	bool indicatorOccluding = false;
	RECT indicatorBounds{};
	unsigned long long transitionSerial = 0;
	unsigned long long serial = 0;
};

// UI 总集
export class BarUISetClass
{
public:
	BarUISetClass() : spec(this) {};

	// 渲染
	void Rendering();
	void StopRendering();
	// 鼠标交互
	void Interact();
	// 仅合并已经判定为展开/收起的动作，不拦截状态切换和原始输入。
	bool TryBeginToggle(Inkeys::UI::Bar::BarToggleChannel channel)
	{
		return toggleClickCoalescer.TryBegin(channel);
	}
	void RequestBottomDockCenterAfterExpand() noexcept
	{
		bottomDockCenterAutoCaptureRequested.store(true, memory_order_release);
	}
	void ClearBottomDockCenterForFold() noexcept
	{
		bottomDockTransitionSerial.fetch_add(1, memory_order_acq_rel);
		bottomDockCenterMode.store(
			Inkeys::UI::Bar::BarBottomDockCenterMode::Free,
			memory_order_relaxed);
		bottomDockCenterPhase.store(
			Inkeys::UI::Bar::BarBottomDockPhase::Stable,
			memory_order_relaxed);
		bottomDockCenterElasticOffsetDip.store(0.0, memory_order_relaxed);
		bottomDockDeferredTransitionSerial.store(
			bottomDockTransitionSerial.fetch_add(1, memory_order_acq_rel) + 1,
			memory_order_release);
	}

public:
	BarWindowPosClass barWindow;
	BarMediaClass barMedia;
	BarButtonSetClass barButtonSet;
	BarUIRendering spec;

	BarStateClass barState;
	BarStyleClass barStyle;

	ankerl::unordered_dense::map<BarUISetShapeEnum, shared_ptr<BarUiShapeClass>> shapeMap;
	ankerl::unordered_dense::map<BarUISetSuperellipseEnum, shared_ptr<BarUiSuperellipseClass>> superellipseMap;
	ankerl::unordered_dense::map<BarUISetSvgEnum, shared_ptr<BarUiSVGClass>> svgMap;
	ankerl::unordered_dense::map<BarUISetPngEnum, shared_ptr<BarUiPNGClass>> pngMap;
	ankerl::unordered_dense::map<BarUISetWordEnum, shared_ptr<BarUiWordClass>> wordMap;

	// 绘制属性按钮同样复用自身背景层，仅单独记录悬停动画阶段。
	IdtAtomic<BarButtonHoverStageEnum> drawAttributeBrushHoverStage = BarButtonHoverStageEnum::None;
	IdtAtomic<BarButtonHoverStageEnum> drawAttributeSoftPenHoverStage = BarButtonHoverStageEnum::None;
	IdtAtomic<BarButtonHoverStageEnum> drawAttributeLaserHoverStage = BarButtonHoverStageEnum::None;
	IdtAtomic<BarButtonHoverStageEnum> drawAttributeHighlightHoverStage = BarButtonHoverStageEnum::None;
	IdtAtomic<BarButtonHoverStageEnum> drawAttributePenTypeExtensionHoverStage = BarButtonHoverStageEnum::None;
	IdtAtomic<BarButtonHoverStageEnum> drawAttributePenTypeFreeLineHoverStage = BarButtonHoverStageEnum::None;
	IdtAtomic<BarButtonHoverStageEnum> drawAttributeThicknessFineHoverStage = BarButtonHoverStageEnum::None;
	IdtAtomic<BarButtonHoverStageEnum> drawAttributeThicknessMediumHoverStage = BarButtonHoverStageEnum::None;
	IdtAtomic<BarButtonHoverStageEnum> drawAttributeThicknessCoarseHoverStage = BarButtonHoverStageEnum::None;
	IdtAtomic<BarButtonHoverStageEnum> drawAttributeThicknessAdjustHoverStage = BarButtonHoverStageEnum::None;
	IdtAtomic<BarButtonHoverStageEnum> drawAttributeAnnotationCloseHoverStage = BarButtonHoverStageEnum::None;
	IdtAtomic<BarButtonHoverStageEnum> drawAttributeOverflowCloseHoverStage = BarButtonHoverStageEnum::None;
	IdtAtomic<BarButtonHoverStageEnum> drawAttributeColorPickerToneHoverStage = BarButtonHoverStageEnum::None;
	IdtAtomic<BarButtonHoverStageEnum> drawAttributeColorPickerCloseHoverStage = BarButtonHoverStageEnum::None;
	IdtAtomic<BarButtonHoverStageEnum> moreCloseHoverStage = BarButtonHoverStageEnum::None;
	IdtAtomic<BarButtonHoverStageEnum> geometryStraightLineHoverStage = BarButtonHoverStageEnum::None;
	IdtAtomic<BarButtonHoverStageEnum> geometryRectangleHoverStage = BarButtonHoverStageEnum::None;
	IdtAtomic<BarButtonHoverStageEnum> geometryThicknessFineHoverStage = BarButtonHoverStageEnum::None;
	IdtAtomic<BarButtonHoverStageEnum> geometryThicknessMediumHoverStage = BarButtonHoverStageEnum::None;
	IdtAtomic<BarButtonHoverStageEnum> geometryThicknessCoarseHoverStage = BarButtonHoverStageEnum::None;
	IdtAtomic<BarButtonHoverStageEnum> geometryCloseHoverStage = BarButtonHoverStageEnum::None;

public:
	// 渲染更新：状态更新 + 通知计算并渲染
	void UpdateRendering(bool updateState = true);
	// 工具或白板工作区切换时收起所有属性浮层，主栏本体保持不变。
	void CollapseAuxiliaryPanels(bool cancelCapture = true);
	void StartDisplayTracking();
	void StopDisplayTracking() noexcept;
	void PublishDisplaySnapshot(Inkeys::Display::SnapshotPtr snapshot) noexcept;
	void PublishWindowDpi(UINT dpi) noexcept;
	BarPendingDisplaySnapshot PendingDisplaySnapshot() const noexcept
	{
		for (;;)
		{
			const auto serialBefore = pendingDisplaySerial.load(
				memory_order_acquire);
			if ((serialBefore & 1ULL) != 0) continue;
			BarPendingDisplaySnapshot snapshot{
				RECT{
					pendingDisplayLeft.load(memory_order_relaxed),
					pendingDisplayTop.load(memory_order_relaxed),
					pendingDisplayRight.load(memory_order_relaxed),
					pendingDisplayBottom.load(memory_order_relaxed) },
				RECT{
					pendingWorkAreaLeft.load(memory_order_relaxed),
					pendingWorkAreaTop.load(memory_order_relaxed),
					pendingWorkAreaRight.load(memory_order_relaxed),
					pendingWorkAreaBottom.load(memory_order_relaxed) },
				pendingDisplayDpi.load(memory_order_relaxed),
				serialBefore };
			if (pendingDisplaySerial.load(memory_order_acquire) == serialBefore)
				return snapshot;
		}
	}
	POINT PresentedMonitorOrigin() const noexcept
	{
		return BottomDockPresentedSnapshot().monitorOrigin;
	}
	POINT DirectWindowPresentedTranslation(bool ignoreWhileDragging = false) const noexcept
	{
		if (ignoreWhileDragging
			&& directWindowDragPhase.load(memory_order_acquire)
				== BarDirectWindowDragPhase::Dragging)
			return {};
		return BottomDockPresentedSnapshot().directTranslation;
	}
	bool IsBottomDockLayoutLocked() const noexcept
	{
		return bottomDockMode.load(memory_order_acquire)
			== Inkeys::UI::Bar::BarBottomDockMode::BottomDocked
			|| bottomDockRecoveryActive.load(memory_order_acquire);
	}
	BarBottomDockPresentedSnapshot BottomDockPresentedSnapshot() const noexcept
	{
		for (;;)
		{
			const auto serialBefore = bottomDockPresentedMappingSerial.load(
				memory_order_acquire);
			if ((serialBefore & 1ULL) != 0) continue;
			BarBottomDockPresentedSnapshot snapshot{};
			snapshot.mode = bottomDockPresentedMode.load(memory_order_relaxed);
			snapshot.phase = bottomDockPresentedPhase.load(memory_order_relaxed);
			snapshot.recoveryActive =
				bottomDockPresentedRecoveryActive.load(memory_order_relaxed);
			snapshot.mapping.baseTopDip = bottomDockPresentedBaseTopDip.load(
				memory_order_relaxed);
			snapshot.mapping.baseBottomDip =
				bottomDockPresentedBaseBottomDip.load(memory_order_relaxed);
			snapshot.mapping.visualTopDip = bottomDockPresentedVisualTopDip.load(
				memory_order_relaxed);
			snapshot.mapping.visualBottomDip =
				bottomDockPresentedVisualBottomDip.load(memory_order_relaxed);
			snapshot.mapping.scaleY = max(0.000001,
				bottomDockPresentedScaleY.load(memory_order_relaxed));
			snapshot.mapping.rigidGripYDip =
				bottomDockPresentedRigidGripYDip.load(memory_order_relaxed);
			snapshot.elasticOffsetDip = bottomDockPresentedElasticOffsetDip.load(
				memory_order_relaxed);
			snapshot.rigidTranslationDip =
				bottomDockPresentedRigidTranslationDip.load(memory_order_relaxed);
			snapshot.mapping.rigidOverlayTranslationYDip =
				snapshot.rigidTranslationDip;
			snapshot.centerMode = bottomDockPresentedCenterMode.load(
				memory_order_relaxed);
			snapshot.centerPhase = bottomDockPresentedCenterPhase.load(
				memory_order_relaxed);
			snapshot.horizontalMapping.baseLeftDip =
				bottomDockPresentedBaseLeftDip.load(memory_order_relaxed);
			snapshot.horizontalMapping.baseRightDip =
				bottomDockPresentedBaseRightDip.load(memory_order_relaxed);
			snapshot.horizontalMapping.visualLeftDip =
				bottomDockPresentedVisualLeftDip.load(memory_order_relaxed);
			snapshot.horizontalMapping.visualRightDip =
				bottomDockPresentedVisualRightDip.load(memory_order_relaxed);
			snapshot.horizontalMapping.scaleX = max(0.000001,
				bottomDockPresentedScaleX.load(memory_order_relaxed));
			snapshot.horizontalMapping.rigidGripTranslationXDip =
				bottomDockPresentedRigidGripTranslationXDip.load(
					memory_order_relaxed);
			snapshot.horizontalMapping.rigidOverlayTranslationXDip =
				bottomDockPresentedRigidTranslationXDip.load(
					memory_order_relaxed);
			snapshot.centerElasticOffsetDip =
				bottomDockPresentedCenterElasticOffsetDip.load(memory_order_relaxed);
			snapshot.zoom = Inkeys::UI::Bar::NormalizeBarBottomDockZoom(
				bottomDockPresentedZoom.load(memory_order_relaxed));
			snapshot.monitorOrigin = POINT{
				presentedMonitorOriginX.load(memory_order_relaxed),
				presentedMonitorOriginY.load(memory_order_relaxed) };
			snapshot.monitorBounds = RECT{
				bottomDockPresentedDisplayLeft.load(memory_order_relaxed),
				bottomDockPresentedDisplayTop.load(memory_order_relaxed),
				bottomDockPresentedDisplayRight.load(memory_order_relaxed),
				bottomDockPresentedDisplayBottom.load(memory_order_relaxed) };
				snapshot.workArea = RECT{
					bottomDockPresentedWorkAreaLeft.load(memory_order_relaxed),
					bottomDockPresentedWorkAreaTop.load(memory_order_relaxed),
					bottomDockPresentedWorkAreaRight.load(memory_order_relaxed),
					bottomDockPresentedWorkAreaBottom.load(memory_order_relaxed) };
				snapshot.dpi = bottomDockPresentedDisplayDpi.load(memory_order_relaxed);
				snapshot.displaySerial =
				bottomDockPresentedDisplaySerial.load(memory_order_relaxed);
			snapshot.directTranslation = POINT{
				bottomDockPresentedDirectTranslationX.load(memory_order_relaxed),
				bottomDockPresentedDirectTranslationY.load(memory_order_relaxed) };
			snapshot.mainCenterScreenX =
				bottomDockPresentedMainCenterScreenX.load(memory_order_relaxed);
			snapshot.mainCenterScreenY =
				bottomDockPresentedMainCenterScreenY.load(memory_order_relaxed);
			snapshot.rawMainCenterScreenX =
				bottomDockPresentedRawMainCenterScreenX.load(memory_order_relaxed);
			snapshot.bodyCenterScreenX =
				bottomDockPresentedBodyCenterScreenX.load(memory_order_relaxed);
			snapshot.rawBodyCenterScreenX =
				bottomDockPresentedRawBodyCenterScreenX.load(memory_order_relaxed);
			snapshot.indicatorVisible =
				bottomDockIndicatorPresentedVisible.load(memory_order_relaxed);
			snapshot.indicatorOccluding =
				bottomDockIndicatorPresentedOccluding.load(memory_order_relaxed);
			snapshot.indicatorBounds = RECT{
				bottomDockIndicatorPresentedLeft.load(memory_order_relaxed),
				bottomDockIndicatorPresentedTop.load(memory_order_relaxed),
				bottomDockIndicatorPresentedRight.load(memory_order_relaxed),
				bottomDockIndicatorPresentedBottom.load(memory_order_relaxed) };
			snapshot.transitionSerial =
				bottomDockPresentedTransitionSerial.load(memory_order_relaxed);
			snapshot.serial = serialBefore;
			if (bottomDockPresentedMappingSerial.load(memory_order_acquire)
				== serialBefore) return snapshot;
		}
	}
	int BottomDockRigidHitTestY(int visualY) const noexcept
	{
		const auto snapshot = BottomDockPresentedSnapshot();
		return static_cast<int>(lround(static_cast<double>(visualY)
			- snapshot.rigidTranslationDip * snapshot.zoom));
	}
	int BottomDockRigidVisualY(int rigidY) const noexcept
	{
		const auto snapshot = BottomDockPresentedSnapshot();
		return static_cast<int>(lround(static_cast<double>(rigidY)
			+ snapshot.rigidTranslationDip * snapshot.zoom));
	}
	int BottomDockBodyHitTestY(int visualY) const noexcept
	{
		const auto snapshot = BottomDockPresentedSnapshot();
		return static_cast<int>(lround(
			Inkeys::UI::Bar::UnmapBarBottomDockBodyPixelY(
				static_cast<double>(visualY), snapshot.mapping, snapshot.zoom)));
	}
	int BottomDockRigidHitTestX(int visualX) const noexcept
	{
		const auto snapshot = BottomDockPresentedSnapshot();
		return static_cast<int>(lround(static_cast<double>(visualX)
			- snapshot.horizontalMapping.rigidOverlayTranslationXDip
				* snapshot.zoom));
	}
	int BottomDockRigidVisualX(int rigidX) const noexcept
	{
		const auto snapshot = BottomDockPresentedSnapshot();
		return static_cast<int>(lround(static_cast<double>(rigidX)
			+ snapshot.horizontalMapping.rigidOverlayTranslationXDip
				* snapshot.zoom));
	}
	int BottomDockBodyHitTestX(int visualX) const noexcept
	{
		const auto snapshot = BottomDockPresentedSnapshot();
		return static_cast<int>(lround(
			Inkeys::UI::Bar::UnmapBarBottomDockBodyPixelX(
				static_cast<double>(visualX), snapshot.horizontalMapping,
				snapshot.zoom)));
	}
	int BottomDockGripHitTestX(int visualX) const noexcept
	{
		const auto snapshot = BottomDockPresentedSnapshot();
		return static_cast<int>(lround(
			Inkeys::UI::Bar::UnmapBarBottomDockGripPixelX(
				static_cast<double>(visualX), snapshot.horizontalMapping,
				snapshot.zoom)));
	}
	int BottomDockGripHitTestXFromRigid(int rigidX,
		int* visualX = nullptr) const noexcept
	{
		const auto snapshot = BottomDockPresentedSnapshot();
		const int resolvedVisualX = static_cast<int>(lround(
			static_cast<double>(rigidX)
				+ snapshot.horizontalMapping.rigidOverlayTranslationXDip
					* snapshot.zoom));
		if (visualX) *visualX = resolvedVisualX;
		return static_cast<int>(lround(
			Inkeys::UI::Bar::UnmapBarBottomDockGripPixelX(
				static_cast<double>(resolvedVisualX), snapshot.horizontalMapping,
				snapshot.zoom)));
	}
	int BottomDockBodyHitTestXFromRigid(int rigidX,
		int* visualX = nullptr) const noexcept
	{
		const auto snapshot = BottomDockPresentedSnapshot();
		const int resolvedVisualX = static_cast<int>(lround(
			static_cast<double>(rigidX)
				+ snapshot.horizontalMapping.rigidOverlayTranslationXDip
					* snapshot.zoom));
		if (visualX) *visualX = resolvedVisualX;
		return static_cast<int>(lround(
			Inkeys::UI::Bar::UnmapBarBottomDockBodyPixelX(
				static_cast<double>(resolvedVisualX), snapshot.horizontalMapping,
				snapshot.zoom)));
	}
	int BottomDockBodyHitTestYFromRigid(int rigidY,
		int* visualY = nullptr) const noexcept
	{
		const auto snapshot = BottomDockPresentedSnapshot();
		const int resolvedVisualY = static_cast<int>(lround(
			static_cast<double>(rigidY)
				+ snapshot.rigidTranslationDip * snapshot.zoom));
		if (visualY) *visualY = resolvedVisualY;
		return static_cast<int>(lround(
			Inkeys::UI::Bar::UnmapBarBottomDockBodyPixelY(
				static_cast<double>(resolvedVisualY), snapshot.mapping,
				snapshot.zoom)));
	}
	RECT BottomDockBodyVisualBounds(RECT bounds) const noexcept
	{
		const auto snapshot = BottomDockPresentedSnapshot();
		return Inkeys::UI::Bar::TransformBarBottomDockBodyRect(
			bounds, snapshot.horizontalMapping, snapshot.mapping, snapshot.zoom);
	}
	RECT BottomDockGripVisualBounds(RECT bounds) const noexcept
	{
		const auto snapshot = BottomDockPresentedSnapshot();
		return Inkeys::UI::Bar::TransformBarBottomDockGripRect(
			bounds, snapshot.horizontalMapping, snapshot.mapping, snapshot.zoom);
	}
	RECT BottomDockRigidVisualBounds(RECT bounds) const noexcept
	{
		const auto snapshot = BottomDockPresentedSnapshot();
		return Inkeys::UI::Bar::TranslateBarBottomDockRigidRect(bounds,
			snapshot.horizontalMapping.rigidOverlayTranslationXDip,
			snapshot.rigidTranslationDip, snapshot.zoom);
	}
	bool IsBottomDockIndicatorPresentedAt(LONG x, LONG y) const noexcept
	{
		const auto snapshot = BottomDockPresentedSnapshot();
		return Inkeys::UI::Bar::IsBarBottomDockIndicatorHit(
			snapshot.indicatorVisible && snapshot.indicatorOccluding,
			snapshot.indicatorBounds, x, y);
	}
protected:
	// 调用方持有 directWindowDragMutex；直移只重基准屏幕位置，不改变已呈现位图。
	void RebaseBottomDockPresentedWindow(POINT directTranslation,
		POINT screenDelta = {}) noexcept
	{
		directWindowPresentedTranslationX.store(
			directTranslation.x, memory_order_relaxed);
		directWindowPresentedTranslationY.store(
			directTranslation.y, memory_order_relaxed);
		bottomDockPresentedMappingSerial.fetch_add(1, memory_order_acq_rel);
		bottomDockPresentedDirectTranslationX.store(
			directTranslation.x, memory_order_relaxed);
		bottomDockPresentedDirectTranslationY.store(
			directTranslation.y, memory_order_relaxed);
		bottomDockPresentedMainCenterScreenX.store(
			bottomDockPresentedMainCenterScreenX.load(memory_order_relaxed)
				+ screenDelta.x,
			memory_order_relaxed);
		bottomDockPresentedMainCenterScreenY.store(
			bottomDockPresentedMainCenterScreenY.load(memory_order_relaxed)
				+ screenDelta.y,
			memory_order_relaxed);
		bottomDockPresentedRawMainCenterScreenX.store(
			bottomDockPresentedRawMainCenterScreenX.load(memory_order_relaxed)
				+ screenDelta.x,
			memory_order_relaxed);
		bottomDockPresentedBodyCenterScreenX.store(
			bottomDockPresentedBodyCenterScreenX.load(memory_order_relaxed)
				+ screenDelta.x,
			memory_order_relaxed);
		bottomDockPresentedRawBodyCenterScreenX.store(
			bottomDockPresentedRawBodyCenterScreenX.load(memory_order_relaxed)
				+ screenDelta.x,
			memory_order_relaxed);
		bottomDockPresentedMappingSerial.fetch_add(1, memory_order_release);
	}
	// 拖动交互
	BarSeekResult Seek(const ExMessage& msg);
	bool SetBorderCursorRawInputEnabled(HWND hWnd, bool enabled);
	void ActivateBorderCursorTracking(HWND hWnd);
	void RegisterBorderCursorLight(HWND hWnd);
	void HandleBorderCursorGraceTimeout(HWND hWnd);
	void SuspendBorderCursorTracking(HWND hWnd, bool waitForMouseLeave = false);
	bool ScheduleBorderCursorGraceTimer(HWND hWnd, UINT delayMs);
	void HandleCanvasDrawingActivity(HWND hWnd, bool started);
	void CloseAnnotationTooltip();
	void CloseThicknessOverflowTooltip();
	void CloseDrawAttributeTooltips();
	void ClosePenTypeMenu();
	void CloseThicknessSlider(bool cancelCapture);
	void CloseColorPicker(bool cancelCapture);
	void ShutdownWindowInput(HWND hWnd);
	void RefreshBorderCursorVisibleRegions();
	bool IsBorderCursorLightNearVisibleRegion(POINT screenPoint);
	std::atomic<unsigned long long> mainButtonClickPulseSerial = 0;
	Inkeys::UI::Bar::BarToggleClickCoalescer toggleClickCoalescer;

	mutex borderCursorLightMutex;
	D2D1_POINT_2F borderCursorLightPoint = D2D1::Point2F();
	unsigned long long borderCursorLightSerial = 0;
	bool borderCursorInputAvailable = false;
	bool borderCursorLightReady = false;
	bool borderCursorLightNearVisibleRegion = false;
	bool borderCursorRawInputRegistered = false;
	bool borderCursorRegistrationFailureLogged = false;
	bool borderCursorRemovalFailureLogged = false;
	bool borderCursorTimerFailureLogged = false;
	bool borderCursorActivationBlockedUntilLeave = false;
	BarBorderCursorTrackingStateEnum borderCursorTrackingState =
		BarBorderCursorTrackingStateEnum::Dormant;
	ULONGLONG borderCursorGraceDeadlineTick = 0;
	array<RECT, 7> borderCursorVisibleRegions{};
	size_t borderCursorVisibleRegionCount = 0;

	// ULW 与主按钮直移共用几何锁；位移使用原子目标，交互线程不等待慢提交。
	mutex directWindowDragMutex;
	atomic<BarDirectWindowDragPhase> directWindowDragPhase =
		BarDirectWindowDragPhase::Idle;
	atomic<LONG> directWindowDragTranslationX = 0;
	atomic<LONG> directWindowDragTranslationY = 0;
	atomic<LONG> directWindowPresentedTranslationX = 0;
	atomic<LONG> directWindowPresentedTranslationY = 0;
	RECT committedWindowScreenBounds{};
	bool committedWindowScreenBoundsReady = false;
	Inkeys::Display::Subscription displaySubscription;
	atomic<unsigned long long> pendingDisplaySerial = 0;
	mutex pendingDisplayPublishMutex;
	atomic<LONG> pendingDisplayLeft = 0;
	atomic<LONG> pendingDisplayTop = 0;
	atomic<LONG> pendingDisplayRight = 1;
	atomic<LONG> pendingDisplayBottom = 1;
	atomic<LONG> pendingWorkAreaLeft = 0;
	atomic<LONG> pendingWorkAreaTop = 0;
	atomic<LONG> pendingWorkAreaRight = 1;
	atomic<LONG> pendingWorkAreaBottom = 1;
	atomic<UINT> pendingDisplayDpi = USER_DEFAULT_SCREEN_DPI;
	atomic<LONG> presentedMonitorOriginX = 0;
	atomic<LONG> presentedMonitorOriginY = 0;
	// 交互线程发布目标，渲染线程只回写当前视觉偏移与恢复完成状态。
	atomic<Inkeys::UI::Bar::BarBottomDockMode> bottomDockMode =
		Inkeys::UI::Bar::BarBottomDockMode::BottomDocked;
	atomic<Inkeys::UI::Bar::BarBottomDockPhase> bottomDockPhase =
		Inkeys::UI::Bar::BarBottomDockPhase::Stable;
	atomic<double> bottomDockElasticOffsetDip = 0.0;
	atomic<Inkeys::UI::Bar::BarBottomDockCenterMode> bottomDockCenterMode =
		Inkeys::UI::Bar::BarBottomDockCenterMode::Centered;
	atomic<Inkeys::UI::Bar::BarBottomDockPhase> bottomDockCenterPhase =
		Inkeys::UI::Bar::BarBottomDockPhase::Stable;
	atomic<double> bottomDockCenterElasticOffsetDip = 0.0;
	atomic<bool> bottomDockIndicatorGestureEligible = false;
	atomic<bool> bottomDockCenterAutoCaptureRequested = false;
	atomic<unsigned long long> bottomDockPresentedMappingSerial = 0;
	atomic<Inkeys::UI::Bar::BarBottomDockMode> bottomDockPresentedMode =
		Inkeys::UI::Bar::BarBottomDockMode::BottomDocked;
	atomic<Inkeys::UI::Bar::BarBottomDockPhase> bottomDockPresentedPhase =
		Inkeys::UI::Bar::BarBottomDockPhase::Stable;
	atomic<bool> bottomDockPresentedRecoveryActive = false;
	atomic<double> bottomDockPresentedElasticOffsetDip = 0.0;
	atomic<double> bottomDockPresentedBaseTopDip = 0.0;
	atomic<double> bottomDockPresentedBaseBottomDip = 0.0;
	atomic<double> bottomDockPresentedVisualTopDip = 0.0;
	atomic<double> bottomDockPresentedVisualBottomDip = 0.0;
	atomic<double> bottomDockPresentedScaleY = 1.0;
	atomic<double> bottomDockPresentedRigidGripYDip = 0.0;
	atomic<double> bottomDockPresentedRigidTranslationDip = 0.0;
	atomic<Inkeys::UI::Bar::BarBottomDockCenterMode>
		bottomDockPresentedCenterMode =
			Inkeys::UI::Bar::BarBottomDockCenterMode::Centered;
	atomic<Inkeys::UI::Bar::BarBottomDockPhase> bottomDockPresentedCenterPhase =
		Inkeys::UI::Bar::BarBottomDockPhase::Stable;
	atomic<double> bottomDockPresentedCenterElasticOffsetDip = 0.0;
	atomic<double> bottomDockPresentedBaseLeftDip = 0.0;
	atomic<double> bottomDockPresentedBaseRightDip = 0.0;
	atomic<double> bottomDockPresentedVisualLeftDip = 0.0;
	atomic<double> bottomDockPresentedVisualRightDip = 0.0;
	atomic<double> bottomDockPresentedScaleX = 1.0;
	atomic<double> bottomDockPresentedRigidGripTranslationXDip = 0.0;
	atomic<double> bottomDockPresentedRigidTranslationXDip = 0.0;
	atomic<double> bottomDockPresentedZoom = 1.0;
	atomic<LONG> bottomDockPresentedDisplayLeft = 0;
	atomic<LONG> bottomDockPresentedDisplayTop = 0;
	atomic<LONG> bottomDockPresentedDisplayRight = 1;
	atomic<LONG> bottomDockPresentedDisplayBottom = 1;
	atomic<LONG> bottomDockPresentedWorkAreaLeft = 0;
	atomic<LONG> bottomDockPresentedWorkAreaTop = 0;
	atomic<LONG> bottomDockPresentedWorkAreaRight = 1;
		atomic<LONG> bottomDockPresentedWorkAreaBottom = 1;
		atomic<UINT> bottomDockPresentedDisplayDpi = USER_DEFAULT_SCREEN_DPI;
		atomic<unsigned long long> bottomDockPresentedDisplaySerial = 0;
	atomic<LONG> bottomDockPresentedDirectTranslationX = 0;
	atomic<LONG> bottomDockPresentedDirectTranslationY = 0;
	atomic<double> bottomDockPresentedMainCenterScreenX = 0.0;
	atomic<double> bottomDockPresentedMainCenterScreenY = 0.0;
	atomic<double> bottomDockPresentedRawMainCenterScreenX = 0.0;
	atomic<double> bottomDockPresentedBodyCenterScreenX = 0.0;
	atomic<double> bottomDockPresentedRawBodyCenterScreenX = 0.0;
	atomic<bool> bottomDockIndicatorPresentedVisible = false;
	atomic<bool> bottomDockIndicatorPresentedOccluding = false;
	atomic<LONG> bottomDockIndicatorPresentedLeft = 0;
	atomic<LONG> bottomDockIndicatorPresentedTop = 0;
	atomic<LONG> bottomDockIndicatorPresentedRight = 0;
	atomic<LONG> bottomDockIndicatorPresentedBottom = 0;
	atomic<bool> bottomDockDragActive = false;
	atomic<double> bottomDockDragRigidGripScreenY = 0.0;
	atomic<bool> bottomDockRecoveryActive = false;
	atomic<unsigned long long> bottomDockTransitionSerial = 0;
	atomic<unsigned long long> bottomDockDeferredTransitionSerial = 0;
	atomic<unsigned long long> bottomDockPresentedTransitionSerial = 0;

	friend class BarUIRendering;
	friend class BarRenderLoopCoordinator;
	friend LRESULT CALLBACK barWindowMsgCallback(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);
};
// 全局 Bar UI 集合
export extern BarUISetClass barUISet;

// 初始化

namespace Inkeys::UI::Bar
{
	export WNDPROC WindowProc() noexcept;
	export Inkeys::Message::Reply QueueWindowMessageInLayoutSpace(
		HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam);
	export void Initialization();
	export void SetAnimationOptions(bool enable, double speedRate);
	export void SetEdgeLightingOptions(bool enable, bool dynamic);
	export void SetDebugOptions(bool enable, bool showFrameRate);
	export bool DebugModeEnabled() noexcept;
	export void SetCurrentPageHasContent(bool hasContent) noexcept;
	export bool CurrentPageHasContent() noexcept;
	export void SetWhiteboardActive(bool active) noexcept;
	export void CollapseAuxiliaryPanels(bool cancelCapture = true) noexcept;
	export bool WhiteboardActive() noexcept;
	export void RequestWhiteboardBottomDock() noexcept;
	export bool ConsumeWhiteboardBottomDockRequest() noexcept;
	export bool WhiteboardDockLockActive() noexcept;
	export void ClearWhiteboardDockLock() noexcept;
	export bool HideWhiteboardSnapIndicator() noexcept;
	export void NotifyCanvasDrawingStarted();
	export void NotifyCanvasDrawingEnded();
	export bool TryQueueColorPickerKeyboardInput(BYTE vkCode, bool keyDown);

	bool InitializeWindow(BarUISetClass& barUISet);
	void InitializeUI(BarUISetClass& barUISet);
	void SetContentStateUpdatesReady(bool ready) noexcept;
};
