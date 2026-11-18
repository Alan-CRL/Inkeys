module;

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>

#include <algorithm>
#include <cstdint>
#include <optional>

export module Inkeys.UI.Bar.PenPreviewMotion;

import Inkeys.UI.UI3.Animation;

using namespace Inkeys::UI::UI3;

using namespace std;

export
{
	// 粗细预览、快捷按钮与笔型扩展共用的纯状态计算，供渲染和 HeadlessTests 使用。
	enum class BarThicknessPreviewVisualKind : uint8_t
	{
		SoftPen,
		HardPen,
		Highlighter,
		Laser,
		Unsupported,
	};

	struct BarThicknessPreviewMorphSample
	{
		double curveProgress = 1.0;
		double highlighterProgress = 0.0;
	};

	inline BarThicknessPreviewMorphSample ResolveBarThicknessPreviewMorph(
		double highlighterMorph) noexcept
	{
		highlighterMorph = clamp(highlighterMorph, 0.0, 1.0);
		return {
			clamp(1.0 - highlighterMorph * 2.0, 0.0, 1.0),
			clamp((highlighterMorph - 0.5) * 2.0, 0.0, 1.0),
		};
	}

	enum class BarThicknessPresetVisualKind : uint8_t
	{
		Circle,
		Number,
	};

	inline BarThicknessPresetVisualKind ResolveBarThicknessPresetVisualKind(
		BarThicknessPreviewVisualKind penKind) noexcept
	{
		return penKind == BarThicknessPreviewVisualKind::Highlighter
			? BarThicknessPresetVisualKind::Number
			: BarThicknessPresetVisualKind::Circle;
	}

	struct BarThicknessPresetOpacitySample
	{
		double circleOpacity = 0.0;
		double numberOpacity = 0.0;
	};

	inline BarThicknessPresetOpacitySample ResolveBarThicknessPresetOpacity(
		double numberProgress) noexcept
	{
		numberProgress = clamp(numberProgress, 0.0, 1.0);
		return {
			clamp(1.0 - numberProgress * 2.0, 0.0, 1.0),
			clamp(numberProgress * 2.0 - 1.0, 0.0, 1.0),
		};
	}

	inline bool BarThicknessPresetRetargetsCircle(
		BarThicknessPresetVisualKind targetKind) noexcept
	{
		return targetKind == BarThicknessPresetVisualKind::Circle;
	}

	inline bool BarThicknessPresetRetargetsNumber(
		BarThicknessPresetVisualKind currentKind,
		BarThicknessPresetVisualKind targetKind) noexcept
	{
		return targetKind == BarThicknessPresetVisualKind::Number
			&& currentKind != targetKind;
	}

	inline bool BarPenTypeSupportsExtension(
		BarThicknessPreviewVisualKind kind) noexcept
	{
		return kind == BarThicknessPreviewVisualKind::SoftPen
			|| kind == BarThicknessPreviewVisualKind::HardPen
			|| kind == BarThicknessPreviewVisualKind::Highlighter;
	}

	enum class BarPenTypeExtensionSlot : uint8_t
	{
		SoftPen,
		HardPen,
		Highlighter,
		Count,
	};

	inline optional<BarPenTypeExtensionSlot> ResolveBarPenTypeExtensionSlot(
		BarThicknessPreviewVisualKind kind) noexcept
	{
		switch (kind)
		{
		case BarThicknessPreviewVisualKind::SoftPen:
			return BarPenTypeExtensionSlot::SoftPen;
		case BarThicknessPreviewVisualKind::HardPen:
			return BarPenTypeExtensionSlot::HardPen;
		case BarThicknessPreviewVisualKind::Highlighter:
			return BarPenTypeExtensionSlot::Highlighter;
		default:
			return nullopt;
		}
	}

	enum class BarAnnotationPopupTitleKind : uint8_t
	{
		Unavailable,
		FixedThicknessUnsupported,
	};

	inline BarAnnotationPopupTitleKind ResolveBarAnnotationPopupTitle(
		BarThicknessPreviewVisualKind anchorKind) noexcept
	{
		return anchorKind == BarThicknessPreviewVisualKind::SoftPen
			? BarAnnotationPopupTitleKind::FixedThicknessUnsupported
			: BarAnnotationPopupTitleKind::Unavailable;
	}

	// Laser 预览的阶段只由当前视觉端点决定，反向时不重置任何动画值。
	enum class BarLaserPreviewPhase : uint8_t
	{
		NonLaserStable,
		EnteringCore,
		EnteringShell,
		LaserStable,
		LeavingShell,
		LeavingCore,
	};

	inline BarLaserPreviewPhase ResolveBarLaserPreviewPhase(
		BarLaserPreviewPhase phase, bool targetLaser,
		bool coreAtLaserEndpoint, bool coreAtNonLaserEndpoint,
		bool shellHidden, bool shellExpanded) noexcept
	{
		switch (phase)
		{
		case BarLaserPreviewPhase::NonLaserStable:
			return targetLaser
				? BarLaserPreviewPhase::EnteringCore : phase;
		case BarLaserPreviewPhase::EnteringCore:
			if (!targetLaser) return BarLaserPreviewPhase::LeavingCore;
			return coreAtLaserEndpoint
				? BarLaserPreviewPhase::EnteringShell : phase;
		case BarLaserPreviewPhase::EnteringShell:
			if (!targetLaser) return BarLaserPreviewPhase::LeavingShell;
			return shellExpanded
				? BarLaserPreviewPhase::LaserStable : phase;
		case BarLaserPreviewPhase::LaserStable:
			return targetLaser
				? phase : BarLaserPreviewPhase::LeavingShell;
		case BarLaserPreviewPhase::LeavingShell:
			if (targetLaser) return BarLaserPreviewPhase::EnteringShell;
			return shellHidden
				? BarLaserPreviewPhase::LeavingCore : phase;
		case BarLaserPreviewPhase::LeavingCore:
			if (targetLaser) return BarLaserPreviewPhase::EnteringCore;
			return coreAtNonLaserEndpoint
				? BarLaserPreviewPhase::NonLaserStable : phase;
		default:
			return targetLaser ? BarLaserPreviewPhase::EnteringCore
				: BarLaserPreviewPhase::LeavingCore;
		}
	}

	enum class BarLaserPreviewSemanticTarget : uint8_t
	{
		Hold,
		Laser,
		NonLaser,
	};

	struct BarLaserPreviewTargetPolicy
	{
		BarLaserPreviewSemanticTarget core =
			BarLaserPreviewSemanticTarget::Hold;
		BarLaserPreviewSemanticTarget outer =
			BarLaserPreviewSemanticTarget::Hold;
		bool shellExpanded = false;
	};

	inline BarLaserPreviewTargetPolicy ResolveBarLaserPreviewTargetPolicy(
		BarLaserPreviewPhase phase) noexcept
	{
		switch (phase)
		{
		case BarLaserPreviewPhase::NonLaserStable:
			return { BarLaserPreviewSemanticTarget::NonLaser,
				BarLaserPreviewSemanticTarget::NonLaser, false };
		case BarLaserPreviewPhase::EnteringCore:
			return { BarLaserPreviewSemanticTarget::Laser,
				BarLaserPreviewSemanticTarget::Laser, false };
		case BarLaserPreviewPhase::EnteringShell:
			return { BarLaserPreviewSemanticTarget::Hold,
				BarLaserPreviewSemanticTarget::Hold, true };
		case BarLaserPreviewPhase::LaserStable:
			return { BarLaserPreviewSemanticTarget::Laser,
				BarLaserPreviewSemanticTarget::Laser, true };
		case BarLaserPreviewPhase::LeavingShell:
			return { BarLaserPreviewSemanticTarget::Hold,
				BarLaserPreviewSemanticTarget::Hold, false };
		case BarLaserPreviewPhase::LeavingCore:
			return { BarLaserPreviewSemanticTarget::NonLaser,
				BarLaserPreviewSemanticTarget::NonLaser, false };
		default:
			return {};
		}
	}

	inline double ResolveBarLaserPreviewEnvelopeThickness(
		double coreThickness, double outerThickness,
		double shellProgress) noexcept
	{
		coreThickness = (std::max)(0.0, coreThickness);
		outerThickness = (std::max)(0.0, outerThickness);
		shellProgress = clamp(shellProgress, 0.0, 1.0);
		const double currentShellThickness = coreThickness
			+ (outerThickness - coreThickness) * shellProgress;
		return (std::max)(coreThickness, currentShellThickness);
	}

	struct BarLaserPreviewLayerGeometry
	{
		double endpointDiameter = 0.0;
		double horizontalInset = 0.0;
	};

	inline BarLaserPreviewLayerGeometry ResolveBarLaserPreviewLayerGeometry(
		double layerThickness, double animatedOuterDiameter,
		double sliderProgress, double sliderTrackThickness) noexcept
	{
		layerThickness = (std::max)(0.0, layerThickness);
		animatedOuterDiameter = (std::max)(0.0, animatedOuterDiameter);
		sliderTrackThickness = (std::max)(0.0, sliderTrackThickness);
		sliderProgress = clamp(sliderProgress, 0.0, 1.0);
		// Slider 展开时端点圆心与芯宽同步收向轨道，避免两套几何在交接帧错位。
		const double endpointDiameter = animatedOuterDiameter
			+ (sliderTrackThickness - animatedOuterDiameter) * sliderProgress;
		return {
			endpointDiameter,
			(std::max)(0.0, (endpointDiameter - layerThickness) / 2.0),
		};
	}

	struct BarPenTypeExtensionAnchor
	{
		double x = 0.0;
		double y = 0.0;
	};

	inline BarPenTypeExtensionAnchor ResolveBarPenTypeExtensionAnchor(
		double selectedButtonCurrentX, double selectedButtonCurrentY,
		double dividerOffsetX) noexcept
	{
		return {
			selectedButtonCurrentX + dividerOffsetX,
			selectedButtonCurrentY,
		};
	}

	inline COLORREF ResolveBarPenTypeExtensionColor(
		COLORREF selectedButtonCurrentColor) noexcept
	{
		return selectedButtonCurrentColor;
	}

	struct BarPenTypeExtensionPresentation
	{
		bool interactive = false;
		double opacity = 0.0;
	};

	inline BarPenTypeExtensionPresentation ResolveBarPenTypeExtensionPresentation(
		bool targetInteractive, double visualProgress,
		double contentOpacity) noexcept
	{
		return {
			targetInteractive,
			clamp(visualProgress, 0.0, 1.0)
				* clamp(contentOpacity, 0.0, 1.0),
		};
	}

}
