module;
#include "Assets/EraserGripVisual.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <cmath>
#include <deque>
#include <DirectXMath.h>
#include <iostream>
#include <ink_stroke_modeler/stroke_modeler.h>
#include <limits>
#include <memory>
#include <map>
#include <mutex>
#include <optional>
#include <span>
#include <utility>
#include <vector>
#include <dxgiformat.h>
#include <windows.h>
#include <mmsystem.h>

#include "Draw3.Bridge.h"
#include "Draw3.Presentation.h"
#include "Draw3.TimerPeriod.h"
#include "Draw3.SpeedEraser.h"

#pragma comment(lib, "winmm.lib")

module Inkeys.Drawing.Draw3.drawing_controller;

import Inkeys.Drawing.Draw3.diagnostics;
import Inkeys.Drawing.Draw3.canvas_navigation;
import Inkeys.Drawing.Draw3.haptic_feedback;
import Inkeys.Drawing.Draw3.pen_cursor;

namespace Inkeys::Drawing::Draw3
{
	namespace
	{
		bool BeginOneMillisecondTimerPeriod(void*, unsigned int period) noexcept
		{
			return timeBeginPeriod(period) == TIMERR_NOERROR;
		}

		void EndOneMillisecondTimerPeriod(void*, unsigned int period) noexcept
		{
			timeEndPeriod(period);
		}

		const DirectX::XMFLOAT4 kMultiContactInkColor(1.0f, 0.0f, 0.0f, 1.0f);
		const DirectX::XMFLOAT4 kReconnectManualTestColor(0.0f, 1.0f, 0.0f, 1.0f);
		constexpr float kPenDiameter = 5.0f;
		constexpr float kLaserDiameter = kLaserSolidDiameterAt96Dpi;
		constexpr size_t kLaserReservedContactCount = 32;
		constexpr float kMinimumPenCursorDiameterAt96Dpi = 5.0f;
		constexpr float kPenTipCursorFillAlpha = 0.25f;
		constexpr float kWideToolDiameter = 50.0f;
		// 荧光笔几何当前在 StrokeGeometry 中固定为 50px 长边，光标必须跟随实际笔迹。
		constexpr float kHighlighterStrokeDiameterPx = 50.0f;
		constexpr float kReconnectManualTestRadiusPx = 4.0f;
		constexpr float kRawMoveThresholdPx = 0.25f;
		constexpr float kStylusPressureEpsilon = 0.0001f;
		constexpr float kStylusAngleEpsilon = 0.0001f;
		constexpr float kPi = 3.14159265358979323846f;
		constexpr float kHalfPi = kPi * 0.5f;
		constexpr float kTwoPi = kPi * 2.0f;
		constexpr double kInputSpeedSmoothingSeconds = 0.060;
		constexpr double kSpeedEraserHoverHandbackWindowSeconds = 0.250;
		constexpr double kMaximumLaserHoldDurationSeconds = 24.0 * 60.0 * 60.0;
		constexpr size_t kPreheatedStrokeCount = 16;
		// 颜色/宽度由 WindowController 原子发布，绘制线程在帧边界采样。
		thread_local ProductVisualStyle currentProductVisualStyle{};

		const char* CanvasTouchDispositionName(CanvasTouchDisposition disposition) noexcept
		{
			switch (disposition)
			{
			case CanvasTouchDisposition::Draw: return "draw";
			case CanvasTouchDisposition::PanCandidate: return "pan-candidate";
			case CanvasTouchDisposition::Pan: return "pan";
			case CanvasTouchDisposition::Suppressed: return "suppressed";
			default: return "unknown";
			}
		}

		LONG SaturatingFloorToLong(double value) noexcept
		{
			value = std::floor(value);
			if (value <= static_cast<double>((std::numeric_limits<LONG>::min)()))
				return (std::numeric_limits<LONG>::min)();
			if (value >= static_cast<double>((std::numeric_limits<LONG>::max)()))
				return (std::numeric_limits<LONG>::max)();
			return static_cast<LONG>(value);
		}

		LONG SaturatingCeilToLong(double value) noexcept
		{
			value = std::ceil(value);
			if (value <= static_cast<double>((std::numeric_limits<LONG>::min)()))
				return (std::numeric_limits<LONG>::min)();
			if (value >= static_cast<double>((std::numeric_limits<LONG>::max)()))
				return (std::numeric_limits<LONG>::max)();
			return static_cast<LONG>(value);
		}

		bool TryAddQpcDuration(int64_t originQpc, int64_t qpcFrequency,
			double seconds, int64_t& deadlineQpc) noexcept
		{
			if (originQpc < 0 || qpcFrequency <= 0 ||
				!std::isfinite(seconds) || seconds < 0.0) return false;
			const double durationTicks = seconds * static_cast<double>(qpcFrequency);
			const int64_t maximumDelta = (std::numeric_limits<int64_t>::max)() - originQpc;
			if (!std::isfinite(durationTicks) || durationTicks < 0.0 ||
				durationTicks >= static_cast<double>(maximumDelta)) return false;
			deadlineQpc = originQpc + static_cast<int64_t>(durationTicks);
			return true;
		}

		double AbsoluteQpcSeconds(int64_t qpc, int64_t qpcFrequency) noexcept
		{
			if (qpc < 0 || qpcFrequency <= 0) return 0.0;
			return static_cast<double>(qpc) / static_cast<double>(qpcFrequency);
		}

		float DiameterForTool(DrawingTool tool,
			const ProductVisualStyle& visualStyle)
		{
			if (tool == DrawingTool::Pen || tool == DrawingTool::HardPen ||
				tool == DrawingTool::Highlighter ||
				IsShapeDrawingTool(tool))
				return (std::max)(0.1f, visualStyle.widthDip);
			if (tool == DrawingTool::Laser)
				return (std::max)(0.1f, visualStyle.widthDip);
			return kWideToolDiameter;
		}

		float DiameterForTool(DrawingTool tool)
		{
			return DiameterForTool(tool, currentProductVisualStyle);
		}

		float CanvasDiameterForTool(DrawingTool tool,
			const ProductVisualStyle& visualStyle, float dpiScale) noexcept
		{
			const float diameter = DiameterForTool(tool, visualStyle);
			return tool == DrawingTool::Laser ? diameter * dpiScale : diameter;
		}

		ShapePrimitiveKind ShapeKindForTool(DrawingTool tool) noexcept
		{
			switch (tool)
			{
			case DrawingTool::DashedLine: return ShapePrimitiveKind::DashedLine;
			case DrawingTool::OutlineRectangle: return ShapePrimitiveKind::OutlineRectangle;
			case DrawingTool::FilledRectangle: return ShapePrimitiveKind::FilledRectangle;
			default: return ShapePrimitiveKind::SolidLine;
			}
		}

		Bridge::CompletedStrokeKind CompletedStrokeKindForTool(
			DrawingTool tool) noexcept
		{
			if (tool == DrawingTool::Eraser)
				return Bridge::CompletedStrokeKind::Eraser;
			if (IsShapeDrawingTool(tool))
				return Bridge::CompletedStrokeKind::Shape;
			return Bridge::CompletedStrokeKind::Drawing;
		}

		bool HasLinearStylusChange(float current, float previous, float epsilon, float maximum) noexcept
		{
			if (!std::isfinite(current) || current < 0.0f || current > maximum) return false;
			return !std::isfinite(previous) || previous < 0.0f ||
				std::abs(current - previous) > epsilon;
		}

		bool HasOrientationChange(float current, float previous) noexcept
		{
			if (!std::isfinite(current) || current < 0.0f || current >= kTwoPi) return false;
			if (!std::isfinite(previous) || previous < 0.0f || previous >= kTwoPi) return true;
			const float difference = std::abs(current - previous);
			return std::min(difference, kTwoPi - difference) > kStylusAngleEpsilon;
		}

		bool HasStylusStateChange(const ContactSnapshot& current,
			const ContactSnapshot& previous) noexcept
		{
			return HasLinearStylusChange(current.pressure, previous.pressure,
				kStylusPressureEpsilon, 1.0f) ||
				HasLinearStylusChange(current.tilt, previous.tilt, kStylusAngleEpsilon, kHalfPi) ||
				HasOrientationChange(current.orientation, previous.orientation);
		}

		float KeepLastValidStylusValue(float value, float maximum, float& lastValue) noexcept
		{
			if (std::isfinite(value) && value >= 0.0f && value <= maximum)
				lastValue = value;
			return lastValue;
		}

		float KeepLastValidOrientation(float value, float& lastValue) noexcept
		{
			if (std::isfinite(value) && value >= 0.0f && value < kTwoPi)
				lastValue = value;
			return lastValue;
		}

		DirectX::XMFLOAT4 ColorForTool(DrawingTool tool,
			const ProductVisualStyle& visualStyle)
		{
			if (tool == DrawingTool::Eraser) return kTransparentLayerClearColor;
			const uint32_t rgba = visualStyle.colorRgba;
			DirectX::XMFLOAT4 productColor(
				static_cast<float>((rgba >> 24) & 0xffu) / 255.0f,
				static_cast<float>((rgba >> 16) & 0xffu) / 255.0f,
				static_cast<float>((rgba >> 8) & 0xffu) / 255.0f,
				static_cast<float>(rgba & 0xffu) / 255.0f);
			if (tool == DrawingTool::Highlighter)
			{
				productColor.w = (std::min)(productColor.w,
					Bridge::kHighlighterCompositeOpacity);
				return productColor;
			}
			return productColor;
		}

		DirectX::XMFLOAT4 ColorForTool(DrawingTool tool)
		{
			return ColorForTool(tool, currentProductVisualStyle);
		}

		void ConfigureLaserRendererStyle(InkRenderer& renderer,
			const ProductVisualStyle& visualStyle, float dpiScale) noexcept
		{
			renderer.ConfigureLaserStyle(
				dpiScale, ColorForTool(DrawingTool::Laser, visualStyle));
		}

		bool ProductVisualStyleEqual(const ProductVisualStyle& left,
			const ProductVisualStyle& right) noexcept
		{
			return left.colorRgba == right.colorRgba && left.widthDip == right.widthDip;
		}

		void ConfigureProductInkCursorAppearances(WindowController& window,
			const ProductVisualStyle& visualStyle, float dpiScale)
		{
			const DirectX::XMFLOAT4 penColor = ColorForTool(
				DrawingTool::Pen, visualStyle);
			const float penCursorDiameter = std::max(
				CanvasDiameterForTool(DrawingTool::Pen, visualStyle, dpiScale),
				kMinimumPenCursorDiameterAt96Dpi * dpiScale);
			DrawingCursorAppearance penAppearance = {
				DrawingCursorShape::Circle,
				penCursorDiameter,
				penCursorDiameter,
				penColor.x,
				penColor.y,
				penColor.z
			};
			penAppearance.fillAlpha = kPenTipCursorFillAlpha;
			window.ConfigureDrawingCursor(DrawingTool::Pen, penAppearance);

			const DirectX::XMFLOAT4 highlighterColor = ColorForTool(
				DrawingTool::Highlighter, visualStyle);
			// 荧光笔几何和光标共用产品宽度，保持预览、实际笔迹与光标一致。
			const float highlighterDiameter = std::max(1.0f, visualStyle.widthDip);
			DrawingCursorAppearance highlighterAppearance = {
				DrawingCursorShape::Rectangle,
				highlighterDiameter / kHighlighterNibAspectRatio,
				highlighterDiameter,
				highlighterColor.x,
				highlighterColor.y,
				highlighterColor.z
			};
			// Shader 的最终填充 alpha=opacity*fillAlpha，与荧光笔实际合成保持一致。
			highlighterAppearance.opacity = highlighterColor.w;
			highlighterAppearance.fillAlpha = 1.0f;
			window.ConfigureDrawingCursor(DrawingTool::Highlighter,
				highlighterAppearance);

			const float laserSolidDiameter = CanvasDiameterForTool(
				DrawingTool::Laser, visualStyle, dpiScale);
			DrawingCursorAppearance laserAppearance = {
				DrawingCursorShape::Circle,
				laserSolidDiameter,
				laserSolidDiameter,
				1.0f, 1.0f, 1.0f
			};
			laserAppearance.fillAlpha = 1.0f;
			laserAppearance.outlineWidth = 0.0f;
			// 与 Down 时 CanvasDiameterForTool 使用同一条 DIP -> canvas px 链路。
			window.ConfigureDrawingCursor(DrawingTool::Laser, laserAppearance);
		}

		bool TryCreateInkGuid(InkGuid& output) noexcept
		{
			GUID guid = {};
			const HRESULT result = CoCreateGuid(&guid);
			if (result != S_OK)
			{
				LogHResult("CoCreateGuid(ink document)", result);
				return false;
			}
			// Windows GUID 前三段是整数；显式转成 canonical UUID 字节序，避免依赖本机端序。
			const std::array<uint8_t, 16> bytes = {
				static_cast<uint8_t>(guid.Data1 >> 24),
				static_cast<uint8_t>(guid.Data1 >> 16),
				static_cast<uint8_t>(guid.Data1 >> 8),
				static_cast<uint8_t>(guid.Data1),
				static_cast<uint8_t>(guid.Data2 >> 8),
				static_cast<uint8_t>(guid.Data2),
				static_cast<uint8_t>(guid.Data3 >> 8),
				static_cast<uint8_t>(guid.Data3),
				guid.Data4[0], guid.Data4[1], guid.Data4[2], guid.Data4[3],
				guid.Data4[4], guid.Data4[5], guid.Data4[6], guid.Data4[7]
			};
			InkGuid created(bytes);
			if (created.IsZero())
			{
				std::cout << "CoCreateGuid returned a nil ink document GUID." << std::endl;
				return false;
			}
			output = created;
			return true;
		}

		std::optional<size_t> TryAppendBlankPage(InkCanvasCollection& document)
		{
			InkGuid pageGuid;
			if (!TryCreateInkGuid(pageGuid)) return std::nullopt;
			InkPage page(pageGuid);
			if (!page.GetOrCreateCanvas(kDefaultDeviceKey))
			{
				std::cout << "Failed to create the default ink canvas for a new page." << std::endl;
				return std::nullopt;
			}
			const std::optional<size_t> pageIndex = document.AppendPage(std::move(page));
			if (!pageIndex)
			{
				std::cout << "Failed to append a unique ink document page." << std::endl;
				return std::nullopt;
			}
			return pageIndex;
		}

		struct CanvasPageRuntimeState
		{
			CanvasRuntimeHistory history;
			InkRasterStateToken rasterState = 0;
			std::vector<InkRasterStateToken> beforeStates;
			std::vector<InkRasterStateToken> afterStates;
			// 普通导入内容可作为不可撤根；Clear 恢复则从零开始，允许逐笔撤空。
			std::size_t undoFloor = 0;
			std::uint32_t intervalOrdinal = 0;
			bool intervalLoadPending = false;
			bool previousClearUndoAvailable = true; // 跨 Clear 恢复成功后消费，阻止进入更早画布。
			bool clearRedoAvailable = false; // 仅Clear撤销恢复后有效，新笔迹提交会取消。
			std::optional<draw3::uink::Draw3UInkCanvasSnapshot> boundaryFallback;
		};

		struct DesktopClearRecovery
		{
			std::optional<draw3::uink::Draw3UInkCanvasSnapshot> canvas;
			std::optional<draw3::uink::UInkGuid> fileGuid;
			bool diskSaveRequested = false;
			bool diskReady = false;
			bool loadPending = false;
		};

		struct DrawingDocumentSlot
		{
			std::optional<InkCanvasCollection> document;
			std::vector<CanvasPageRuntimeState> pageRuntimeStates;
			// 稳定 SlideID 已从当前 PPT 消失时，保留其纯值快照，等待以后重新出现。
			std::map<std::int32_t, draw3::uink::Draw3UInkCanvasSnapshot> retainedSlides;
			std::size_t currentPageIndex = 0;
			std::optional<Bridge::PresentationTarget> presentationTarget;
			std::optional<draw3::uink::UInkGuid> fileGuid;
			std::uint64_t mutationRevision = 0;
			std::uint64_t queuedRevision = 0;
			std::uint64_t committedRevision = 0;
			bool persistenceInitialized = false;
			bool loadPending = false;
		};

		struct PendingWorkspaceReady
		{
			Bridge::Workspace workspace = Bridge::Workspace::Desktop;
			std::size_t currentPageIndex = 0;
			std::size_t pageCount = 0;
			std::optional<Bridge::PresentationReadyIdentity> presentationTarget;
		};

		struct CompositionMaintenanceItem
		{
			size_t pageIndex = 0;
			CompositionNodeId node = {};
			SignedTileCoordinate tile = {};
			uint64_t rasterGeneration = 0;
		};

		InkPixelBounds VisibleInkBounds(InkViewport viewport,
			int width, int height) noexcept
		{
			return { viewport.x, viewport.y,
				viewport.x + static_cast<float>(std::max(width, 0)),
				viewport.y + static_cast<float>(std::max(height, 0)) };
		}

		const char* CompositionRestorePathName(CompositionRestorePath path) noexcept
		{
			switch (path)
			{
			case CompositionRestorePath::CompositionCache: return "composition_cache";
			case CompositionRestorePath::CompositionRebuild: return "composition_rebuild";
			case CompositionRestorePath::OrderedTileReplay: return "ordered_tile_replay";
			case CompositionRestorePath::Empty: return "empty";
			default: return "failed";
			}
		}

		std::vector<SignedTileCoordinate> CollectVisibleCompositionTiles(
			const CanvasRuntimeHistory& history, InkViewport viewport,
			int width, int height)
		{
			return history.VisibleCompositionTiles({ viewport.x, viewport.y,
				viewport.x + static_cast<float>(std::max(width, 0)),
				viewport.y + static_cast<float>(std::max(height, 0)) });
		}

		std::vector<CanvasTileCoordinate> CollectCanvasContentTiles(
			const CanvasRuntimeHistory& history, CanvasRect coverage)
		{
			std::vector<CanvasTileCoordinate> tiles;
			for (SignedTileCoordinate tile : history.VisibleCompositionTiles({
				coverage.left, coverage.top, coverage.right, coverage.bottom }))
				tiles.push_back({ tile.x, tile.y });
			return tiles;
		}

		uint32_t PackStoredRgb(const DirectX::XMFLOAT4& color) noexcept
		{
			auto channel = [](float value) noexcept
			{
				return static_cast<uint32_t>(std::lround(
					std::clamp(value, 0.0f, 1.0f) * 255.0f));
			};
			return channel(color.x) << 16 | channel(color.y) << 8 | channel(color.z);
		}

		std::optional<StoredInkStyle> StoredStyleForTool(DrawingTool tool,
			const ProductVisualStyle& visualStyle) noexcept
		{
			StoredInkStyle style;
			const DirectX::XMFLOAT4 color = ColorForTool(tool, visualStyle);
			style.fallbackRgb = PackStoredRgb(color);
			style.texture = 0;
			switch (tool)
			{
			case DrawingTool::Pen:
			case DrawingTool::HardPen:
				style.inkType = StoredInkType::Pen;
				style.opacity = color.w;
				return style;
			case DrawingTool::Highlighter:
				style.inkType = StoredInkType::Highlighter;
				style.opacity = color.w;
				return style;
			case DrawingTool::Eraser:
				style.inkType = StoredInkType::Eraser;
				style.opacity = 1.0f;
				return style;
			case DrawingTool::SolidLine:
				style.inkType = StoredInkType::SolidLine;
				style.opacity = color.w;
				return style;
			case DrawingTool::DashedLine:
				style.inkType = StoredInkType::DashedLine;
				style.opacity = color.w;
				return style;
			case DrawingTool::OutlineRectangle:
				style.inkType = StoredInkType::OutlineRectangle;
				style.opacity = color.w;
				return style;
			case DrawingTool::FilledRectangle:
				style.inkType = StoredInkType::FilledRectangle;
				style.opacity = color.w;
				return style;
			default:
				return std::nullopt; // Laser 是瞬时视觉，不进入文档。
			}
		}

		std::optional<draw3::uink::Draw3UInkStrokeKind> UInkKindForStoredType(
			StoredInkType type) noexcept
		{
			using draw3::uink::Draw3UInkStrokeKind;
			switch (type)
			{
			case StoredInkType::Pen: return Draw3UInkStrokeKind::Pen;
			case StoredInkType::Highlighter: return Draw3UInkStrokeKind::Highlighter;
			case StoredInkType::Eraser: return Draw3UInkStrokeKind::Eraser;
			case StoredInkType::SolidLine: return Draw3UInkStrokeKind::SolidLine;
			case StoredInkType::DashedLine: return Draw3UInkStrokeKind::DashedLine;
			case StoredInkType::OutlineRectangle:
				return Draw3UInkStrokeKind::OutlineRectangle;
			case StoredInkType::FilledRectangle:
				return Draw3UInkStrokeKind::FilledRectangle;
			default: return std::nullopt;
			}
		}

		struct ReconnectManualTestRange
		{
			size_t firstPointIndex = 0;
			size_t lastPointIndex = 0;
		};

		struct RuntimeShapeState
		{
			ShapePrimitive primitive = {};
			ShapePrimitiveKind kind = ShapePrimitiveKind::SolidLine;
			DirectX::XMFLOAT2 rawEndpoint = {};
			DirectX::XMFLOAT2 modeledEndpoint = {};
			bool active = false;
			bool hasModeledEndpoint = false;
			bool rawFallbackRequired = false;
			bool visualChanged = false;

			void Reset() noexcept
			{
				*this = {};
			}
		};

		struct RuntimeStroke
		{
			explicit RuntimeStroke(float expectedSpeed)
				: stroke(kPenDiameter, expectedSpeed)
			{
				stroke.modeledResults.reserve(256);
				stroke.predictedResults.reserve(64);
				stroke.realPoints.reserve(256);
				stroke.predictedPoints.reserve(64);
				stroke.l0DrawPoints.reserve(128);
				stroke.previousL0DrawPoints.reserve(128);
				rebuildPoints.reserve(256);
				reconnectPredictedResults.reserve(64);
			}

			ActiveStroke stroke;
			RuntimeShapeState shape;
			InkViewport viewport = {};
			uint64_t touchGestureKey = 0;
			ContactHandle handle = {};
			DrawingTool selectedTool = DrawingTool::Pen;
			DrawingTool tool = DrawingTool::Pen;
			ProductVisualStyle visualStyle = {};
			EraserWidthMode eraserWidthMode = EraserWidthMode::Fixed;
			uint32_t eraserWidthModeRevision = 0;
			bool suppressPressure = false;
			ContactSnapshot lastSpeedSnapshot = {};
			ContactSnapshot lastInputSnapshot = {};
			ContactSnapshot lastModelSnapshot = {};
			uint64_t lastConsumedSequence = 0;
			int64_t qpcOrigin = 0;
			double lastModelInputTime = 0.0;
			uint64_t diagnosticStrokeId = 0;
			uint64_t diagnosticModelUpdates = 0;
			float filteredInputSpeed = 0.0f;
			float lastPressure = -1.0f;
			float lastTilt = -1.0f;
			float lastOrientation = -1.0f;
			// 每个 contact 保存批次快照，清屏和回收后不会遗留全局批次配置。
			SpeedEraser::DisplayScale speedEraserDisplayScale = {};
			SpeedEraser::DeviceMode speedEraserDeviceMode = SpeedEraser::DeviceMode::Laptop;
			SpeedEraserOcController speedEraserOc;
			SpeedEraser::ContactSizeState eraserSize;
			bool touchContactAreaAssistance = false;
			double eraserTimeOrigin = 0.0;
			SpeedEraser::Diagnostics eraserDiagnostics;
			bool mouseSpeedEraserFinished = false; // 非Touch统一退休标记，保留字段名以缩小改动。
			uint64_t eraserSessionToken = 0;
			SpeedEraser::InputSettings eraserInputs;
			SpeedEraser::EraserToolPolicy eraserPolicy = SpeedEraser::EraserToolPolicy::ByEntry;
			SpeedEraser::ResolvedInput resolvedEraser;
			bool firstEraserCursorFrame = false;
			double speedEraserModelTime = 0.0;
			float speedEraserModelDiameter = SpeedEraser::Config{}.minimumDiameterPx;
			RECT visibleDirty = {};
			std::vector<InkPoint> rebuildPoints;
			std::vector<ink::stroke_model::Result> reconnectPredictedResults;
			std::vector<ReconnectManualTestRange> reconnectManualTestRanges;
			InputDeviceType metricDeviceType = InputDeviceType::Touch;
			int64_t metricEligibleQpc = 0;
			bool inUse = false;
			bool ended = false;
			bool cancelled = false;
			bool metricVisible = false;
			bool movedThisFrame = false;
			bool modelInputThisFrame = false;
			bool stationaryModelAdvanceBlocked = false;
			bool laserParticleMovedThisFrame = false;
			bool hasFilteredInputSpeed = false;
			bool invertedCursor = false;
			bool hapticEligible = false;
			bool awaitingReconnect = false;
			bool reconnectVisualRefresh = false;
			ContactSnapshot deferredUpSnapshot = {};
			DirectX::XMFLOAT2 reconnectDirection = {};
			int64_t reconnectDeadlineQpc = 0;
			uint32_t laserParticleSeed = 0;
			uint64_t laserLayerId = 0;
			uint32_t laserParticleSeedCursor = 0;
			float laserParticleFractionalEmission = 0.0f;
			float laserParticleTangentX = 0.0f;
			float laserParticleTangentY = 0.0f;
			bool hasLaserParticleTangent = false;
		};


		void AppendRuntimeModeledPoints(RuntimeStroke& runtime,float inputSpeed,double currentInputTime)
		{
			if(runtime.stroke.widthMode!=StrokeWidthMode::SpeedEraser)
			{AppendNewModeledPoints(runtime.stroke,inputSpeed);return;}
			const auto& config=runtime.speedEraserOc.Configuration();
			const auto interval=runtime.eraserSize.MakeInterval(runtime.speedEraserModelTime,currentInputTime,
				runtime.speedEraserModelDiameter,runtime.speedEraserOc.Diameter(),
				runtime.eraserTimeOrigin+currentInputTime,config);
			const size_t previousCount=runtime.stroke.realPoints.size();
			AppendNewModeledPoints(runtime.stroke,inputSpeed,&interval);
			if(runtime.stroke.realPoints.size()>previousCount)
			{
				runtime.eraserSize.Accepted(interval);
				if(interval.reanchor && previousCount>0 && runtime.eraserDiagnostics.active)
				{
					// 测量真实新增移动段的足迹，排除旧同位大圆已经覆盖的历史。
					const auto& anchor=runtime.stroke.realPoints[previousCount];
					const auto& old=runtime.stroke.realPoints[previousCount-1];
					const bool inserted=anchor.x==old.x && anchor.y==old.y &&
						std::abs(anchor.r-interval.startDiameter*0.5f)<0.001f;
					const auto points=std::span<const InkPoint>(runtime.stroke.realPoints).subspan(
						inserted?previousCount:previousCount-1);
					const RECT bounds=RectFromStrokePoints(points,(std::numeric_limits<int>::max)(),(std::numeric_limits<int>::max)());
					auto& d=runtime.eraserDiagnostics;
					d.resumedWithAnchor=inserted;
					d.boundaryPoints={old.x,old.y,old.r*2,anchor.x,anchor.y,anchor.r*2,
						points.back().x,points.back().y,points.back().r*2};
					d.resumedLeft=static_cast<float>(bounds.left);d.resumedTop=static_cast<float>(bounds.top);
					d.resumedRight=static_cast<float>(bounds.right);d.resumedBottom=static_cast<float>(bounds.bottom);
					d.resumedMaxRadiusPx=0;
					for(const auto& point:points)d.resumedMaxRadiusPx=std::max(d.resumedMaxRadiusPx,point.r);
				}
			}
			runtime.speedEraserModelTime=currentInputTime;
			runtime.speedEraserModelDiameter=runtime.speedEraserOc.Diameter();
		}

		SpeedEraser::ContactAreaSample ContactAreaFromSnapshot(const ContactSnapshot& snapshot) noexcept
		{
			return {snapshot.rawContactSize.width,snapshot.rawContactSize.height,
				snapshot.contactSize.width,snapshot.contactSize.height,snapshot.contactAreaUnits};
		}

		float RuntimeSpeedEraserContactDiameter(const RuntimeStroke& runtime) noexcept
		{
			return runtime.eraserSize.effectiveDiameterPx;
		}

		bool IsMouseSpeedEraser(const RuntimeStroke& runtime) noexcept
		{
			return runtime.stroke.widthMode == StrokeWidthMode::SpeedEraser &&
				(runtime.metricDeviceType == InputDeviceType::MouseLeft ||
					runtime.metricDeviceType == InputDeviceType::MouseRight);
		}

		struct SpeedEraserHoverLane
		{
			SpeedEraser::MouseLifecycle lifecycle;
			uint64_t lastSampleSequence = 0;
			bool sampleVisible = false;
			void Invalidate() noexcept { lifecycle.CancelVisual();lastSampleSequence=0;sampleVisible=false; }
		};

		void ApplySpeedEraserCursorDiameter(
			DrawingCursorAppearance& appearance, float diameter) noexcept
		{
			if (!std::isfinite(diameter) || diameter <= 0.0f) return;
			appearance.width = diameter;
			appearance.height = diameter;
			appearance.outlineWidth = diameter * ERASER_GRIP_OUTLINE_RATIO;
		}

		struct CanvasGestureContactRuntime
		{
			ContactHandle handle = {};
			uint64_t key = 0;
			ContactSnapshot snapshot = {};
			PointF velocityPosition = {};
			uint64_t lastConsumedSequence = 0;
			CanvasTouchDisposition disposition = CanvasTouchDisposition::Suppressed;
			bool terminalPending = false;
		};

		uint64_t CanvasTouchKey(const ContactHandle& handle) noexcept
		{
			if (!handle.record) return 0;
			return static_cast<uint64_t>(handle.record->TabletContextId()) << 32 |
				handle.record->ContactId();
		}

		bool SetShapeVisualEndpoint(
			RuntimeShapeState& shape, DirectX::XMFLOAT2 endpoint) noexcept
		{
			if (!std::isfinite(endpoint.x) || !std::isfinite(endpoint.y)) return false;
			if (shape.primitive.end.x == endpoint.x &&
				shape.primitive.end.y == endpoint.y) return false;
			shape.primitive.end.x = endpoint.x;
			shape.primitive.end.y = endpoint.y;
			shape.visualChanged = true;
			return true;
		}

		void ExtractShapeModeledEndpoint(RuntimeStroke& runtime) noexcept
		{
			ActiveStroke& stroke = runtime.stroke;
			if (!stroke.modeledResults.empty())
			{
				const auto& position = stroke.modeledResults.back().position;
				if (std::isfinite(position.x) && std::isfinite(position.y))
				{
					runtime.shape.modeledEndpoint = { position.x, position.y };
					runtime.shape.hasModeledEndpoint = true;
					runtime.shape.rawFallbackRequired = false;
				}
			}
			stroke.modeledResults.clear(); // Shape 只复用末点 scratch，内存不随路径长度增长。
			stroke.convertedResultCount = 0;
		}

		struct LaserStrokeLayer
		{
			uint64_t id = 0;
			RuntimeStroke* runtime = nullptr;
			ProductVisualStyle visualStyle = {};
			std::vector<InkPoint> completedPoints;
			LaserIncrementalStrokeState incrementalState;
			RECT stableBounds = {};
			RECT liveBounds = {};
			RECT bounds = {};
			bool cancelled = false;
		};

		struct LaserTipVisual
		{
			LaserDot dot = {};
			ProductVisualStyle visualStyle = {};
		};

		LaserStrokeLayer* FindLaserStrokeLayer(
			std::vector<LaserStrokeLayer>& layers, uint64_t id) noexcept
		{
			const auto iterator = std::find_if(layers.begin(), layers.end(),
				[id](const LaserStrokeLayer& layer) { return layer.id == id; });
			return iterator == layers.end() ? nullptr : &*iterator;
		}

		const std::vector<InkPoint>& LaserStrokeLayerPoints(
			const LaserStrokeLayer& layer) noexcept
		{
			if (layer.runtime) return layer.runtime->stroke.l0DrawPoints;
			return layer.completedPoints;
		}

		bool UpdateLaserIncrementalCoverage(LaserStrokeLayer& layer,
			std::span<const InkPoint> realPoints,
			std::span<const InkPoint> visiblePoints,
			double protectedDurationSeconds, InkRenderer& renderer,
			float dpiScale, int width, int height,
			RECT& dirty)
		{
			ConfigureLaserRendererStyle(renderer, layer.visualStyle, dpiScale);
			dirty = layer.liveBounds;
			const LaserIncrementalRanges ranges = PlanLaserIncrementalRanges(
				realPoints, layer.incrementalState, protectedDurationSeconds);
			RECT nextStableBounds = layer.stableBounds;
			if (ranges.stablePointCount > 0)
			{
				if (ranges.stableFirstIndex >= visiblePoints.size() ||
					ranges.stablePointCount >
						visiblePoints.size() - ranges.stableFirstIndex)
					return false;
				const std::span<const InkPoint> stablePoints = visiblePoints.subspan(
					ranges.stableFirstIndex, ranges.stablePointCount);
				renderer.SetLaserCoverageTarget(renderer.laserStrokeCoverage);
				if (renderer.DrawLaserCoverage(stablePoints) != 0)
					return false;
				const RECT stableDirty = RectFromLaserPoints(
					stablePoints, dpiScale, width, height);
				UnionRectInPlace(nextStableBounds, stableDirty);
				UnionRectInPlace(dirty, stableDirty);
			}

			if (!renderer.ClearLaserLiveCoverageRect(layer.liveBounds))
				return false;
			if (ranges.liveFirstIndex > visiblePoints.size()) return false;
			const std::span<const InkPoint> livePoints =
				visiblePoints.subspan(ranges.liveFirstIndex);
			const RECT nextLiveBounds = RectFromLaserPoints(
				livePoints, dpiScale, width, height);
			if (!livePoints.empty())
			{
				renderer.SetLaserLiveCoverageTarget();
				if (renderer.DrawLaserCoverage(livePoints) != 0)
					return false;
			}
			UnionRectInPlace(dirty, nextLiveBounds);
			layer.stableBounds = nextStableBounds;
			layer.liveBounds = nextLiveBounds;
			layer.incrementalState.stableCommittedIndex =
				ranges.nextStableCommittedIndex;
			layer.incrementalState.rebuildRequired = false;
			layer.bounds = layer.stableBounds;
			UnionRectInPlace(layer.bounds, layer.liveBounds);
			return true;
		}

		void FinalizeLaserStrokeLayer(std::vector<LaserStrokeLayer>& layers,
			RuntimeStroke& runtime, bool cancelled, float dpiScale, int width, int height)
		{
			LaserStrokeLayer* layer = FindLaserStrokeLayer(layers, runtime.laserLayerId);
			if (!layer) return;
			layer->runtime = nullptr;
			layer->cancelled = cancelled;
			layer->completedPoints.clear();
			if (!cancelled)
			{
				if (!runtime.stroke.realPoints.empty())
					layer->completedPoints = runtime.stroke.realPoints;
				else if (runtime.stroke.hasInputStartPoint)
					layer->completedPoints.push_back(runtime.stroke.inputStartPoint);
			}
			layer->bounds = cancelled ? RECT{} : RectFromLaserPoints(
				layer->completedPoints, dpiScale, width, height);
			runtime.laserLayerId = 0;
		}

		void BakeLaserStrokeLayers(std::vector<LaserStrokeLayer>& layers,
			InkRenderer& renderer, float dpiScale, int width, int height,
			RECT& compositedBounds, RECT& bakeDirty, LaserCoverageMode& coverageMode)
		{
			if (coverageMode == LaserCoverageMode::Incremental && layers.size() == 1 &&
				renderer.LaserIncrementalCoverageAvailable())
			{
				LaserStrokeLayer& layer = layers.front();
				bool incrementalBakeSucceeded = true;
				if (!layer.cancelled)
				{
					const std::vector<InkPoint>& points = LaserStrokeLayerPoints(layer);
					RECT coverageDirty = {};
					const bool coverageUpdated = UpdateLaserIncrementalCoverage(
						layer, points, points, 0.0, renderer, dpiScale,
						width, height, coverageDirty);
					UnionRectInPlace(bakeDirty, coverageDirty);
					if (!coverageUpdated)
					{
						incrementalBakeSucceeded = false;
						coverageMode = LaserCoverageMode::FullRedraw;
					}
					else if (!IsEmptyRect(layer.bounds))
					{
						if (renderer.ResolveLaserIncrementalCoverage(
							renderer.laserCompositedColor.rtv.Get(), layer.bounds))
						{
							UnionRectInPlace(compositedBounds, layer.bounds);
						}
						else
						{
							incrementalBakeSucceeded = false;
							coverageMode = LaserCoverageMode::FullRedraw;
						}
					}
				}
				if (incrementalBakeSucceeded)
				{
					renderer.ClearLaserIncrementalCoverage();
					layers.clear();
					return;
				}
				renderer.ClearLaserIncrementalCoverage();
			}
			for (LaserStrokeLayer& layer : layers)
			{
				const std::vector<InkPoint>& points = LaserStrokeLayerPoints(layer);
				if (!ShouldCompositeLaserLayer(layer.cancelled, points.size())) continue;
				layer.bounds = RectFromLaserPoints(points, dpiScale, width, height);
				if (IsEmptyRect(layer.bounds)) continue;
				UnionRectInPlace(bakeDirty, layer.bounds);
				// 每支笔先独立生成 coverage，再按 Down 顺序烘入稳定预乘颜色。
				ConfigureLaserRendererStyle(renderer, layer.visualStyle, dpiScale);
				renderer.ClearLaserCoverageRect(layer.bounds);
				renderer.SetLaserCoverageTarget(renderer.laserStrokeCoverage);
				renderer.DrawLaserCoverage(points);
				renderer.ResolveLaserStrokeCoverage(
					renderer.laserCompositedColor.rtv.Get(), layer.bounds);
				UnionRectInPlace(compositedBounds, layer.bounds);
			}
			renderer.ClearLaserIncrementalCoverage();
			layers.clear();
		}

		void DrawLaserStrokeLayers(std::vector<LaserStrokeLayer>& layers,
			InkRenderer& renderer, ID3D11RenderTargetView* target,
			RECT clipBounds, float dpiScale,
			LaserCoverageMode& coverageMode)
		{
			if (coverageMode == LaserCoverageMode::Incremental && layers.size() == 1)
			{
				if (!renderer.LaserIncrementalCoverageAvailable())
				{
					coverageMode = LaserCoverageMode::FullRedraw;
					renderer.ClearLaserIncrementalCoverage();
				}
				else
				{
					LaserStrokeLayer& layer = layers.front();
					ConfigureLaserRendererStyle(renderer, layer.visualStyle, dpiScale);
					RECT resolveBounds = {};
					if (!IntersectRect(&resolveBounds, &layer.bounds, &clipBounds)) return;
					if (renderer.ResolveLaserIncrementalCoverage(target, resolveBounds))
						return;
					coverageMode = LaserCoverageMode::FullRedraw;
					renderer.ClearLaserIncrementalCoverage();
				}
			}
			else if (coverageMode == LaserCoverageMode::Incremental)
			{
				coverageMode = LaserCoverageMode::FullRedraw;
				renderer.ClearLaserIncrementalCoverage();
			}
			for (LaserStrokeLayer& layer : layers)
			{
				const std::vector<InkPoint>& points = LaserStrokeLayerPoints(layer);
				if (!ShouldCompositeLaserLayer(layer.cancelled, points.size())) continue;
				RECT resolveBounds = {};
				if (!IntersectRect(&resolveBounds, &layer.bounds, &clipBounds)) continue;
				// 仅处理最终 frame dirty 的交集；完整几何和 Down 顺序保持不变。
				ConfigureLaserRendererStyle(renderer, layer.visualStyle, dpiScale);
				renderer.ClearLaserCoverageRect(resolveBounds);
				renderer.SetLaserCoverageTarget(renderer.laserStrokeCoverage);
				renderer.DrawLaserCoverage(points, resolveBounds);
				renderer.ResolveLaserStrokeCoverage(target, resolveBounds);
			}
		}

		const char* InputDeviceTypeName(InputDeviceType deviceType) noexcept
		{
			switch (deviceType)
			{
			case InputDeviceType::Pen: return "Pen";
			case InputDeviceType::MouseLeft: return "MouseLeft";
			case InputDeviceType::MouseRight: return "MouseRight";
			default: return "Touch";
			}
		}

		const char* DrawingToolName(DrawingTool tool) noexcept
		{
			switch (tool)
			{
			case DrawingTool::Highlighter: return "Highlighter";
			case DrawingTool::HardPen: return "HardPen";
			case DrawingTool::Eraser: return "Eraser";
			case DrawingTool::Laser: return "Laser";
			case DrawingTool::SolidLine: return "SolidLine";
			case DrawingTool::DashedLine: return "DashedLine";
			case DrawingTool::OutlineRectangle: return "OutlineRectangle";
			case DrawingTool::FilledRectangle: return "FilledRectangle";
			default: return "Pen";
			}
		}

		const char* ReconnectMotionSourceName(
			InterruptedStrokeReconnectMotionSource source) noexcept
		{
			switch (source)
			{
			case InterruptedStrokeReconnectMotionSource::PredictionPosition:
				return "prediction_position";
			case InterruptedStrokeReconnectMotionSource::RealTail:
				return "real_tail";
			default:
				return "none";
			}
		}

		const char* ReconnectRejectReasonName(
			InterruptedStrokeReconnectRejectReason reason) noexcept
		{
			switch (reason)
			{
			case InterruptedStrokeReconnectRejectReason::IdentityMismatch:
				return "identity";
			case InterruptedStrokeReconnectRejectReason::InvalidTime:
				return "invalid_time";
			case InterruptedStrokeReconnectRejectReason::WindowExpired:
				return "window";
			case InterruptedStrokeReconnectRejectReason::InvalidSpeed:
				return "invalid_speed";
			case InterruptedStrokeReconnectRejectReason::InvalidDirection:
				return "invalid_direction";
			case InterruptedStrokeReconnectRejectReason::Distance:
				return "distance";
			case InterruptedStrokeReconnectRejectReason::ForecastError:
				return "forecast_error";
			case InterruptedStrokeReconnectRejectReason::Angle:
				return "angle";
			default:
				return "none";
			}
		}

		InterruptedStrokeReconnectIdentity ReconnectIdentity(const RuntimeStroke& runtime) noexcept
		{
			return {
				runtime.metricDeviceType,
				static_cast<uint32_t>(runtime.selectedTool),
				static_cast<uint32_t>(runtime.tool),
				runtime.stroke.widthMode,
				runtime.invertedCursor,
				runtime.suppressPressure
			};
		}

		bool HasPhysicalContact(const std::vector<RuntimeStroke*>& active) noexcept
		{
			return std::any_of(active.begin(), active.end(), [](const RuntimeStroke* runtime)
				{
					return runtime && !runtime->ended && !runtime->awaitingReconnect;
				});
		}

		HapticToolFeedback HapticToolForDrawingTool(DrawingTool tool) noexcept
		{
			switch (tool)
			{
			case DrawingTool::Highlighter:
				return HapticToolFeedback::Highlighter;
			case DrawingTool::Eraser:
				return HapticToolFeedback::Eraser;
			default:
				return HapticToolFeedback::Pen;
			}
		}

		HapticContinuousFeedback HapticFeedbackForRuntime(
			const RuntimeStroke& runtime) noexcept
		{
			// 部分笔固件虽枚举 0x1010，却不会驱动笔尾；倒转笔尾使用必需的 InkContinuous。
			if (runtime.invertedCursor)
				return HapticContinuousFeedback::InkContinuous;
			return ResolveContinuousHapticFeedback(
				HapticToolForDrawingTool(runtime.tool));
		}

		double QpcDeltaSeconds(int64_t newer, int64_t older, int64_t frequency)
		{
			if (frequency <= 0 || newer <= older) return 0.0;
			return static_cast<double>(newer - older) / static_cast<double>(frequency);
		}

		uint32_t MixLaserSeed(uint32_t value) noexcept
		{
			value ^= value >> 16;
			value *= 0x7FEB352Du;
			value ^= value >> 15;
			value *= 0x846CA68Bu;
			return value ^ (value >> 16);
		}

		uint32_t LaserSeedForHandle(const ContactHandle& handle) noexcept
		{
			if (!handle.record) return MixLaserSeed(static_cast<uint32_t>(handle.generation));
			return MixLaserSeed(handle.record->TabletContextId() * 0x9E3779B9u ^
				handle.record->ContactId() * 0x85EBCA6Bu ^
				static_cast<uint32_t>(handle.generation));
		}

		void ResetLaserParticleEmitterState(RuntimeStroke& runtime) noexcept
		{
			runtime.laserParticleSeedCursor = 0;
			runtime.laserParticleFractionalEmission = 0.0f;
			runtime.laserParticleTangentX = 0.0f;
			runtime.laserParticleTangentY = 0.0f;
			runtime.hasLaserParticleTangent = false;
		}

		struct LaserParticleEmissionSource
		{
			float positionX = 0.0f;
			float positionY = 0.0f;
			float tangentX = 0.0f;
			float tangentY = 0.0f;
			float entityRadius = 0.0f;
			bool valid = false;
		};

		LaserParticleEmissionSource ResolveLaserParticleEmissionSource(
			RuntimeStroke& runtime) noexcept
		{
			LaserParticleEmissionSource result;
			const std::vector<InkPoint>& points = runtime.stroke.l0DrawPoints;
			if (points.empty()) return result;

			const InkPoint& front = points.back();
			if (!std::isfinite(front.x) || !std::isfinite(front.y)) return result;
			if (!runtime.hasLaserParticleTangent &&
				!runtime.laserParticleMovedThisFrame)
				return result; // prediction 不能在真实首次移动前单独建立发射方向。
			for (size_t index = points.size(); index > 1; --index)
			{
				const InkPoint& current = points[index - 1];
				const InkPoint& previous = points[index - 2];
				const float deltaX = current.x - previous.x;
				const float deltaY = current.y - previous.y;
				const float length = std::hypot(deltaX, deltaY);
				if (!std::isfinite(length) || length <= 0.0001f) continue;
				runtime.laserParticleTangentX = deltaX / length;
				runtime.laserParticleTangentY = deltaY / length;
				runtime.hasLaserParticleTangent = true;
				break;
			}
			if (!runtime.hasLaserParticleTangent) return result;

			// 重复 L0 点沿用上一条有效切线；只有出生点会跟随本帧可见笔尖。
			result.positionX = front.x;
			result.positionY = front.y;
			result.tangentX = runtime.laserParticleTangentX;
			result.tangentY = runtime.laserParticleTangentY;
			result.entityRadius = std::isfinite(front.r)
				? std::max(front.r, 0.0f) : 0.0f;
			result.valid = true;
			return result;
		}

		RECT RectFromLaserDots(const std::vector<LaserTipVisual>& visuals,
			float dpiScale, int width, int height) noexcept
		{
			RECT bounds = {};
			for (const LaserTipVisual& visual : visuals)
			{
				const LaserDot& dot = visual.dot;
				if (!std::isfinite(dot.x) || !std::isfinite(dot.y)) continue;
				const double radius = static_cast<double>(LaserVisualRadius(dot.radius, dpiScale));
				if (!std::isfinite(radius)) continue;
				UnionRectInPlace(bounds, RECT{
					SaturatingFloorToLong(static_cast<double>(dot.x) - radius - 2.0),
					SaturatingFloorToLong(static_cast<double>(dot.y) - radius - 2.0),
					SaturatingCeilToLong(static_cast<double>(dot.x) + radius + 2.0),
					SaturatingCeilToLong(static_cast<double>(dot.y) + radius + 2.0) });
			}
			return ClampRectToCanvas(bounds, width, height);
		}

		RECT DrawReconnectManualTestRanges(RuntimeStroke& runtime, InkRenderer& renderer,
			int width, int height)
		{
			if constexpr (!kInterruptedStrokeReconnectManualTestModeEnabled) return {};
			if (runtime.reconnectManualTestRanges.empty() || runtime.stroke.realPoints.empty()) return {};

			renderer.SetOperatorTarget(renderer.layerL1);
			RECT dirty = {};
			for (const ReconnectManualTestRange& range : runtime.reconnectManualTestRanges)
			{
				const size_t firstIndex = std::min(range.firstPointIndex,
					runtime.stroke.realPoints.size());
				const size_t lastIndex = std::min(range.lastPointIndex,
					runtime.stroke.realPoints.size());
				if (lastIndex <= firstIndex) continue;
				runtime.rebuildPoints.assign(runtime.stroke.realPoints.begin() + firstIndex,
					runtime.stroke.realPoints.begin() + lastIndex);
				for (InkPoint& point : runtime.rebuildPoints)
					point.r = std::min(point.r, kReconnectManualTestRadiusPx);
				renderer.DrawStrokeOrDot(runtime.rebuildPoints, kReconnectManualTestColor);
				UnionRectInPlace(dirty,
					RectFromStrokePoints(runtime.rebuildPoints, width, height));
			}
			return ClampRectToCanvas(dirty, width, height);
		}

		RECT DrawStablePrefix(RuntimeStroke& runtime, InkRenderer& renderer, int width, int height)
		{
			ActiveStroke& stroke = runtime.stroke;
			if (!stroke.hasCommittedGeometry) return {};
			if (runtime.tool == DrawingTool::Highlighter)
			{
				if (stroke.committedHighlighterGeometry.primitives.empty()) return {};
				renderer.SetOperatorTarget(renderer.layerL1);
				renderer.DrawHighlighterPrimitives(stroke.committedHighlighterGeometry.primitives,
					ColorForTool(runtime.tool, runtime.visualStyle));
				return ClampRectToCanvas(stroke.committedHighlighterGeometry.bounds, width, height);
			}
			std::array<InkPoint, 1> fallbackPoint = {};
			std::span<const InkPoint> stablePoints;
			if (stroke.realPoints.empty())
			{
				if (runtime.tool != DrawingTool::Eraser || !stroke.hasInputStartPoint) return {};
				fallbackPoint[0] = stroke.inputStartPoint;
				stablePoints = fallbackPoint;
			}
			else
			{
				const size_t pointCount = std::min(stroke.committedIndex + 1, stroke.realPoints.size());
				if (pointCount == 0) return {};
				stablePoints = std::span<const InkPoint>(stroke.realPoints).first(pointCount);
			}
			renderer.SetOperatorTarget(renderer.layerL1);
			const InkOperatorKind operatorKind = runtime.tool == DrawingTool::Eraser
				? InkOperatorKind::Erase : InkOperatorKind::Draw;
			renderer.DrawStrokeOrDot(stablePoints,
				ColorForTool(runtime.tool, runtime.visualStyle),
				StrokeShape::RoundCapsule, operatorKind);
			return RectFromStrokePoints(stablePoints, width, height);
		}

		void DrawActiveShapePrimitives(const std::vector<RuntimeStroke*>& active,
			InkRenderer& renderer, std::vector<ShapePrimitive>& scratch)
		{
			constexpr std::array<ShapePrimitiveKind, 4> kKinds = {
				ShapePrimitiveKind::SolidLine,
				ShapePrimitiveKind::DashedLine,
				ShapePrimitiveKind::OutlineRectangle,
				ShapePrimitiveKind::FilledRectangle
			};
			for (ShapePrimitiveKind kind : kKinds)
			{
				const RuntimeStroke* batchStyle = nullptr;
				const auto flushBatch = [&]
				{
					if (!batchStyle || scratch.empty()) return;
					renderer.SetOperatorTarget(renderer.layerL0);
					renderer.DrawShapePrimitives(scratch, kind,
						ColorForTool(DrawingTool::Pen, batchStyle->visualStyle));
					scratch.clear();
				};
				scratch.clear();
				for (const RuntimeStroke* runtime : active)
				{
					if (!runtime || runtime->ended || !runtime->shape.active ||
						runtime->shape.kind != kind) continue;
					if (batchStyle && batchStyle->visualStyle.colorRgba !=
						runtime->visualStyle.colorRgba)
						flushBatch();
					// 只合并相邻同色 Shape，避免中途改色后重排笔划覆盖顺序。
					if (scratch.empty()) batchStyle = runtime;
					scratch.push_back(runtime->shape.primitive);
				}
				flushBatch();
			}
		}

		RECT RebuildActiveLayers(const std::vector<RuntimeStroke*>& active,
			InkRenderer& renderer, int width, int height,
			std::vector<ShapePrimitive>& shapeScratch)
		{
			renderer.ClearOperatorLayer(renderer.layerL1);
			renderer.ClearOperatorLayer(renderer.layerL0);
			RECT dirty = {};
			for (RuntimeStroke* runtime : active)
			{
				if (!runtime || runtime->ended) continue;
				if (runtime->tool == DrawingTool::Laser) continue;
				if (runtime->shape.active)
				{
					ActiveStroke& stroke = runtime->stroke;
					stroke.lastL0Rect = {};
					stroke.currentL0Rect = RectFromShapePrimitive(runtime->shape.primitive,
						runtime->shape.kind, width, height);
					UnionRectInPlace(dirty, stroke.currentL0Rect);
					continue;
				}
				UnionRectInPlace(dirty, DrawStablePrefix(*runtime, renderer, width, height));
				if constexpr (kInterruptedStrokeReconnectManualTestModeEnabled)
					UnionRectInPlace(dirty,
						DrawReconnectManualTestRanges(*runtime, renderer, width, height));
				ActiveStroke& stroke = runtime->stroke;
				stroke.lastL0Rect = {};
				stroke.currentL0Rect = runtime->tool == DrawingTool::Highlighter
					? ClampRectToCanvas(stroke.l0HighlighterGeometry.bounds, width, height)
					: RectFromStrokePoints(stroke.l0DrawPoints, width, height);
				if (runtime->tool != DrawingTool::Eraser && !stroke.l0DrawPoints.empty())
					DrawL0LiveComposite(stroke,
						ColorForTool(runtime->tool, runtime->visualStyle),
						StrokeShape::RoundCapsule, renderer, false);
				UnionRectInPlace(dirty, stroke.currentL0Rect);
			}
			DrawActiveShapePrimitives(active, renderer, shapeScratch);
			return ClampRectToCanvas(dirty, width, height);
		}
	}

	DrawingController::DrawingController(ContactInputCoordinator& input, WindowController& window, InkRenderer& renderer,
		TransparentPresentationController& presentation, StrokeModelConfiguration configuration,
		DrawingControllerRuntimeObserver observer,
		RuntimeMetricsSession* metrics, PenHapticFeedback* haptics)
		: input_(input), window_(window), renderer_(renderer),
		presentation_(presentation), configuration_(std::move(configuration)), observer_(observer),
		inputWidthModeSettings_(configuration_.inputWidthModes),
		invertedPenEraserEnabled_(configuration_.invertedPenEraserEnabled),
		interruptedStrokeReconnectEnabled_(configuration_.interruptedStrokeReconnectEnabled),
		drawingCursorDuringContactEnabled_(configuration_.drawingCursorDuringContactEnabled),
		translucentInkCursorEnabled_(configuration_.translucentInkCursorEnabled),
		laserParticlesEnabled_(configuration_.laserParticlesEnabled),
		laserMultiTouchDrawingEnabled_(configuration_.laserMultiTouchDrawingEnabled),
		laserHoldDurationSeconds_(std::isfinite(configuration_.laserHoldDurationSeconds) &&
			configuration_.laserHoldDurationSeconds >= 0.0
			? configuration_.laserHoldDurationSeconds : 1.0), metrics_(metrics),
		haptics_(haptics)
	{
		currentProductVisualStyle = window_.ProductVisualStyleSnapshot();
		window_.SetMouseUsesSystemCursor(configuration_.mouseUsesSystemCursor);
		ConfigureProductInkCursorAppearances(window_, currentProductVisualStyle,
			configuration_.dpiScale);
		// 橡皮实际模型使用画布像素宽度，光标不能再次乘 DPI。
		const float eraserCursorDiameter = SpeedEraser::FixedDiameterPx(
			SpeedEraser::ResolveSizes(window_.EraserInputsSnapshot().baseSize).fixedDiameterDip,
			window_.SpeedEraserDisplayScaleSnapshot());
		DrawingCursorAppearance eraserAppearance = {
			DrawingCursorShape::EraserGripCircle,
			eraserCursorDiameter,
			eraserCursorDiameter,
			1.0f, 1.0f, 1.0f
		};
		eraserAppearance.opacity = ERASER_GRIP_OPACITY;
		eraserAppearance.fillAlpha = 1.0f;
		eraserAppearance.outlineWidth = eraserCursorDiameter * ERASER_GRIP_OUTLINE_RATIO;
		eraserAppearance.outlineRed = ERASER_GRIP_OUTLINE_CHANNEL;
		eraserAppearance.outlineGreen = ERASER_GRIP_OUTLINE_CHANNEL;
		eraserAppearance.outlineBlue = ERASER_GRIP_OUTLINE_CHANNEL;
		window_.ConfigureDrawingCursor(DrawingTool::Eraser, eraserAppearance);
		ConfigureLaserRendererStyle(renderer_, currentProductVisualStyle,
			configuration_.dpiScale);
		renderer_.ConfigureShapePrimitives(configuration_.dpiScale);
		renderer_.ConfigureLaserParticles(
			configuration_.laserParticleConfig, configuration_.dpiScale);
		if (haptics_) haptics_->SetEnabled(configuration_.hapticFeedbackEnabled);
	}

	bool DrawingController::SetInputWidthModeSettings(InputWidthModeSettings settings) noexcept
	{
		return inputWidthModeSettings_.Set(settings);
	}

	InputWidthModeSettings DrawingController::GetInputWidthModeSettings() const noexcept
	{
		return inputWidthModeSettings_.Get();
	}

	void DrawingController::SetInvertedPenEraserEnabled(bool enabled) noexcept
	{
		invertedPenEraserEnabled_.store(enabled, std::memory_order_release);
	}

	bool DrawingController::GetInvertedPenEraserEnabled() const noexcept
	{
		return invertedPenEraserEnabled_.load(std::memory_order_acquire);
	}

	void DrawingController::SetInterruptedStrokeReconnectEnabled(bool enabled) noexcept
	{
		interruptedStrokeReconnectEnabled_.store(enabled, std::memory_order_release);
		input_.PublishControlWake(); // 关闭时唤醒绘制线程，立即提交仍在等待的 Touch Up。
	}

	bool DrawingController::GetInterruptedStrokeReconnectEnabled() const noexcept
	{
		return interruptedStrokeReconnectEnabled_.load(std::memory_order_acquire);
	}

	void DrawingController::SetDrawingCursorDuringContactEnabled(bool enabled) noexcept
	{
		if (drawingCursorDuringContactEnabled_.exchange(
			enabled, std::memory_order_acq_rel) == enabled) return;
		input_.PublishControlWake(); // 立即重建旧/新光标脏区，不等待下一次输入。
	}

	bool DrawingController::GetDrawingCursorDuringContactEnabled() const noexcept
	{
		return drawingCursorDuringContactEnabled_.load(std::memory_order_acquire);
	}

	void DrawingController::SetTranslucentInkCursorEnabled(bool enabled) noexcept
	{
		if (translucentInkCursorEnabled_.exchange(
			enabled, std::memory_order_acq_rel) == enabled) return;
		input_.PublishControlWake(); // 立即按新 Alpha 重建当前 Ink 光标。
	}

	bool DrawingController::GetTranslucentInkCursorEnabled() const noexcept
	{
		return translucentInkCursorEnabled_.load(std::memory_order_acquire);
	}

	void DrawingController::SetMouseUsesSystemCursor(bool enabled) noexcept
	{
		window_.SetMouseUsesSystemCursor(enabled);
	}

	bool DrawingController::GetMouseUsesSystemCursor() const noexcept
	{
		return window_.GetMouseUsesSystemCursor();
	}

	void DrawingController::SetLaserParticlesEnabled(bool enabled) noexcept
	{
		laserParticlesEnabled_.store(enabled, std::memory_order_release);
		input_.PublishControlWake();
	}

	bool DrawingController::GetLaserParticlesEnabled() const noexcept
	{
		return laserParticlesEnabled_.load(std::memory_order_acquire);
	}

	void DrawingController::SetLaserMultiTouchDrawingEnabled(bool enabled) noexcept
	{
		laserMultiTouchDrawingEnabled_.store(enabled, std::memory_order_release);
	}

	bool DrawingController::GetLaserMultiTouchDrawingEnabled() const noexcept
	{
		return laserMultiTouchDrawingEnabled_.load(std::memory_order_acquire);
	}

	bool DrawingController::SetLaserHoldDurationSeconds(double seconds) noexcept
	{
		if (!std::isfinite(seconds) || seconds < 0.0 ||
			seconds > kMaximumLaserHoldDurationSeconds) return false;
		laserHoldDurationSeconds_.store(seconds, std::memory_order_release);
		input_.PublishControlWake(); // 运行中的 Hold/Fade 需要立即按最后 Up 时刻重算。
		return true;
	}

	double DrawingController::GetLaserHoldDurationSeconds() const noexcept
	{
		return laserHoldDurationSeconds_.load(std::memory_order_acquire);
	}

	void DrawingController::SetUndoCachePolicy(UndoCachePolicy policy)
	{
		{
			const std::scoped_lock lock(historyCachePolicyMutex_);
			if (undoCachePolicy_.byteBudget == policy.byteBudget &&
				undoCachePolicy_.maxEntries == policy.maxEntries) return;
			undoCachePolicy_ = policy;
			historyCachePolicyGeneration_.fetch_add(1, std::memory_order_release);
		}
		input_.PublishControlWake();
	}

	UndoCachePolicy DrawingController::GetUndoCachePolicy() const
	{
		const std::scoped_lock lock(historyCachePolicyMutex_);
		return undoCachePolicy_;
	}

	void DrawingController::SetCompositionCachePolicy(CompositionCachePolicy policy)
	{
		{
			const std::scoped_lock lock(historyCachePolicyMutex_);
			if (compositionCachePolicy_.byteBudget == policy.byteBudget) return;
			compositionCachePolicy_ = policy;
			historyCachePolicyGeneration_.fetch_add(1, std::memory_order_release);
		}
		input_.PublishControlWake();
	}

	CompositionCachePolicy DrawingController::GetCompositionCachePolicy() const
	{
		const std::scoped_lock lock(historyCachePolicyMutex_);
		return compositionCachePolicy_;
	}

	void DrawingController::CompositeLayersToBackBuffer(RECT dirty, bool orderLiveOverStable)
	{
		const WindowSize size = window_.Size();
		dirty = ClampRectToCanvas(dirty, size.width, size.height); // 限制脏区，避免纹理复制越界。
		if (IsEmptyRect(dirty)) return;
		renderer_.CopyResource(renderer_.backBufferTexture.Get(), renderer_.layerL2Texture.Get(), dirty); // L2 是已经稳定的底层画布。
		const OperatorLayerMergeMode mergeMode = orderLiveOverStable
			? OperatorLayerMergeMode::Ordered : OperatorLayerMergeMode::CoverageUnion;
		renderer_.ApplyOperatorLayers(renderer_.backBufferRTV.Get(),
			renderer_.layerL1, renderer_.layerL0, dirty, mergeMode);
	}

	bool DrawingController::PresentFrame(RECT dirty, bool presentFull)
	{
		const double presentStartMs = GetQpcTimeMilliseconds();
		const bool succeeded = presentation_.Present(
			dirty, presentFull, currentContentRevision_);
		lastPresentDurationMs_ = GetQpcTimeMilliseconds() - presentStartMs;
		lastPresentSucceeded_ = succeeded;
		if (metrics_) metrics_->RecordPresent(lastPresentDurationMs_);
		if (observer_.presented)
			observer_.presented(observer_.context, succeeded, dirty, presentFull,
				presentation_.LastPresentObservation());
		if (!succeeded)
		{
			graphicsRecoveryPending_ = presentation_.RecoveryPending();
			window_.RequestFullPresent(); // 呈现失败后唤醒下一帧，在绘制线程完成设备恢复或后端降级。
		}
		return succeeded;
	}

	void DrawingController::ClearCanvas()
	{
		const WindowSize size = window_.Size();
		const RECT fullCanvas = GetFullCanvasRect(size.width, size.height);
		renderer_.ClearRTV(renderer_.layerL2RTV.Get(), kTransparentLayerClearColor); // 内部画布始终保持真透明背景。
		renderer_.ClearOperatorLayer(renderer_.layerL1); // 清掉当前笔画已确认层。
		renderer_.ClearOperatorLayer(renderer_.layerL0); // 清掉当前帧实时层。
		renderer_.ClearAllLaserCoverage(); // Laser 是独立瞬态层，清屏时必须同步清理。
		renderer_.ResetLaserParticles();
		renderer_.ClearRTV(renderer_.backBufferRTV.Get(), kTransparentLayerClearColor); // backbuffer 也不写入 ULW 的命中测试底层。
		CompositeLayersToBackBuffer(fullCanvas);
		PresentFrame(fullCanvas, true);
	}

	void DrawingController::PresentFullCanvas()
	{
		const WindowSize size = window_.Size();
		const RECT fullCanvas = GetFullCanvasRect(size.width, size.height);
		CompositeLayersToBackBuffer(fullCanvas);
		PresentFrame(fullCanvas, true);
	}

	bool DrawingController::ProcessPendingResize(bool presentAfterResize)
	{
		WindowSize requestedSize = {};
		if (!window_.ConsumeResizeRequest(requestedSize)) return false; // 只有窗口过程投递过尺寸变化才处理。
		const WindowSize oldSize = window_.Size();
		if (requestedSize.width <= 0 || requestedSize.height <= 0 ||
			(requestedSize.width == oldSize.width && requestedSize.height == oldSize.height)) return false;

		if (!renderer_.Resize(presentation_.SwapChain(), requestedSize.width, requestedSize.height)) // 先重建所有尺寸相关 D3D 资源。
		{
			std::cout << "Failed to resize D3D resources to " << requestedSize.width << "x" << requestedSize.height << std::endl;
			presentation_.MarkRuntimeFailure();
			graphicsRecoveryPending_ = true;
			return false;
		}
		if (!presentation_.Resize(requestedSize.width, requestedSize.height)) // 再通知当前透明呈现器更新外部资源。
		{
			std::cout << "Failed to resize transparent presenter to " << requestedSize.width << "x" << requestedSize.height << std::endl;
			graphicsRecoveryPending_ = presentation_.RecoveryPending();
			return false;
		}

		// 仅在两组资源都重建成功后提交逻辑尺寸。
		window_.CommitSize(requestedSize.width, requestedSize.height);
		if (observer_.resized)
			observer_.resized(observer_.context, requestedSize.width, requestedSize.height);
		if (presentAfterResize) PresentFullCanvas();
		return true;
	}

	void DrawingController::Run()
	{
		using namespace ink::stroke_model;
		Bridge::Workspace activeWorkspace = Bridge::Workspace::Desktop;
		DesktopAutoSavePolicy desktopAutoSavePolicy;
		InkGuid workspaceGuid;
		if (!TryCreateInkGuid(workspaceGuid)) return;
		document_.emplace(workspaceGuid);
		currentPageIndex_ = 0;
		const std::optional<size_t> firstPageIndex = TryAppendBlankPage(*document_);
		if (!firstPageIndex)
		{
			document_.reset();
			return;
		}
		currentPageIndex_ = *firstPageIndex;
		if (observer_.documentReady)
			observer_.documentReady(observer_.context, currentPageIndex_,
				document_->Pages().size());
		std::vector<CanvasPageRuntimeState> pageRuntimeStates;
		pageRuntimeStates.reserve(8);
		pageRuntimeStates.emplace_back();
		DrawingDocumentSlot desktopSlot;
		DrawingDocumentSlot whiteboardSlot;
		DrawingDocumentSlot isolatedPresentationSlot;
		std::optional<DesktopClearRecovery> desktopClearRecovery;
		std::map<Bridge::PresentationKey, DrawingDocumentSlot> presentationSlots;
		std::optional<Bridge::PresentationKey> activePresentationKey;
		std::optional<Bridge::PresentationTarget> activePresentationTarget;
		std::map<std::int32_t, draw3::uink::Draw3UInkCanvasSnapshot> activeRetainedSlides;
		std::optional<draw3::uink::UInkGuid> activePresentationFileGuid;
		std::uint64_t activePresentationMutationRevision = 0;
		std::uint64_t activePresentationQueuedRevision = 0;
		std::uint64_t activePresentationCommittedRevision = 0;
		bool activePresentationPersistenceInitialized = false;
		bool activePresentationLoadPending = false;
		std::optional<PendingWorkspaceReady> pendingWorkspaceReady;
		auto stageWorkspaceReady = [&](const Bridge::PresentationTarget* target = nullptr)
		{
			PendingWorkspaceReady ready;
			ready.workspace = activeWorkspace;
			ready.currentPageIndex = currentPageIndex_;
			ready.pageCount = document_ ? document_->Pages().size() : 0;
			if (target) ready.presentationTarget = Bridge::ReadyIdentityFor(*target);
			pendingWorkspaceReady = std::move(ready);
		};
		auto publishWorkspaceReadyAfterPresent = [&]()
		{
			if (!pendingWorkspaceReady || !observer_.workspaceChanged) return;
			const PendingWorkspaceReady ready = std::move(*pendingWorkspaceReady);
			pendingWorkspaceReady.reset();
			observer_.workspaceChanged(observer_.context, ready.workspace,
				ready.currentPageIndex, ready.pageCount,
				ready.presentationTarget ? &*ready.presentationTarget : nullptr);
		};
		auto markPresentationMutation = [&]() noexcept
		{
			if (activeWorkspace != Bridge::Workspace::Presentation ||
				!activePresentationKey || !activePresentationTarget) return;
			activePresentationMutationRevision =
				AdvancePresentationMutationRevision(activePresentationMutationRevision);
		};
		bool publishedCurrentPageHasContent = false;
		bool contentRevisionNeedsPresent = false;
		auto currentPageHasContent = [&]() noexcept
		{
			return currentPageIndex_ < pageRuntimeStates.size() &&
				pageRuntimeStates[currentPageIndex_].history.LastVisibleItem().has_value();
		};
		auto publishCurrentPageContent = [&]()
		{
			const bool hasContent = currentPageHasContent();
			if (hasContent == publishedCurrentPageHasContent) return;
			publishedCurrentPageHasContent = hasContent;
			if (++currentContentRevision_ == 0) ++currentContentRevision_;
			contentRevisionNeedsPresent = true;
			if (observer_.currentPageContentChanged)
				observer_.currentPageContentChanged(
					observer_.context, hasContent, currentContentRevision_);
		};
		auto captureCanvasTail = [&](const InkPage& page,
			const CanvasPageRuntimeState& runtime, std::uint32_t pageIndex,
			std::optional<std::int32_t> slideId = std::nullopt)
			-> std::optional<draw3::uink::Draw3UInkCanvasSnapshot>
		{
			const InkCanvas* canvas = page.FindCanvas(kDefaultDeviceKey);
			if (!canvas) return std::nullopt;
			draw3::uink::Draw3UInkCanvasSnapshot output;
			output.pageGuid = draw3::uink::UInkGuid(page.PageGuid().Bytes());
			output.pageIndex = pageIndex;
			output.pageNumber = pageIndex + 1;
			output.slideId = slideId;
			output.intervalOrdinal = runtime.intervalOrdinal;
			output.viewport = { canvas->Viewport().x, canvas->Viewport().y,
				canvas->Viewport().scale };
			const std::span<const InkStroke> strokes = canvas->Strokes();
			for (const RenderItemState& item : runtime.history.Items())
			{
				if (!item.visible) continue;
				if (item.strokeIndex >= strokes.size()) return std::nullopt;
				const InkStroke& stroke = strokes[item.strokeIndex];
				const auto kind = UInkKindForStoredType(stroke.Style().inkType);
				if (!kind) return std::nullopt;
				draw3::uink::Draw3UInkStrokeSnapshot outputStroke;
				outputStroke.style = { *kind, stroke.Style().opacity,
					stroke.Style().fallbackRgb, stroke.Style().texture };
				outputStroke.undoId = static_cast<std::uint32_t>(output.strokes.size());
				for (const StoredInkPoint& point : stroke.Points())
					outputStroke.points.push_back({ point.x, point.y, point.width });
				output.strokes.push_back(std::move(outputStroke));
			}
			return output;
		};
		auto captureDesktopAutoSave = [&](DesktopAutoSaveTrigger trigger,
			std::optional<draw3::uink::UInkGuid>* acceptedFileGuid = nullptr) -> bool
		{
			if (!observer_.desktopAutoSaveRequested ||
				!desktopAutoSavePolicy.ShouldCapture(activeWorkspace,
					window_.AutoSaveEnabled(), currentPageHasContent()) ||
				!document_ || currentPageIndex_ >= pageRuntimeStates.size()) return false;
			try
			{
				const InkPage* page = document_->PageAt(currentPageIndex_);
				const InkCanvas* canvas = page
					? page->FindCanvas(kDefaultDeviceKey) : nullptr;
				if (!page || !canvas) return false;
				const std::optional<draw3::uink::UInkGuid> fileGuid =
					draw3::uink::CreateUInkGuid();
				if (!fileGuid) return false;

				const double startedMilliseconds = GetQpcTimeMilliseconds();
				draw3::uink::Draw3UInkExportSnapshot snapshot;
				snapshot.fileGuid = *fileGuid;
				snapshot.workspaceGuid = draw3::uink::UInkGuid(
					document_->WorkspaceGuid().Bytes());
				snapshot.workspaceName = "Desktop";
				snapshot.dpiScale = configuration_.dpiScale;
				snapshot.assignedIndependentUndoGroups = true;
				draw3::uink::Draw3UInkCanvasSnapshot outputCanvas;
				outputCanvas.pageGuid = draw3::uink::UInkGuid(page->PageGuid().Bytes());
				// 每个自动保存文件只表示当前区间的一页，索引负责历史顺序。
				outputCanvas.pageIndex = 0;
				outputCanvas.pageNumber = 1;
				outputCanvas.viewport = {
					canvas->Viewport().x, canvas->Viewport().y, canvas->Viewport().scale };
				const std::span<const InkStroke> strokes = canvas->Strokes();
				const CanvasRuntimeHistory& history =
					pageRuntimeStates[currentPageIndex_].history;
				for (const RenderItemState& item : history.Items())
				{
					if (!item.visible) continue;
					if (item.strokeIndex >= strokes.size())
					{
						std::fputs("[Draw3.AutoSave] action=capture result=failed reason=history_mismatch\n",
							stderr);
						return false;
					}
					const InkStroke& stroke = strokes[item.strokeIndex];
					const std::optional<draw3::uink::Draw3UInkStrokeKind> kind =
						UInkKindForStoredType(stroke.Style().inkType);
					if (!kind) return false;
					draw3::uink::Draw3UInkStrokeSnapshot outputStroke;
					outputStroke.style = { *kind, stroke.Style().opacity,
						stroke.Style().fallbackRgb, stroke.Style().texture };
					outputStroke.undoId =
						static_cast<std::uint32_t>(outputCanvas.strokes.size());
					outputStroke.points.reserve(stroke.Points().size());
					for (const StoredInkPoint& point : stroke.Points())
						outputStroke.points.push_back({ point.x, point.y, point.width });
					outputCanvas.strokes.push_back(std::move(outputStroke));
				}
				if (outputCanvas.strokes.empty()) return false;
				const std::size_t strokeCount = outputCanvas.strokes.size();
				snapshot.canvases.push_back(std::move(outputCanvas));
				const std::uint64_t estimatedBytes =
					EstimateDesktopAutoSaveSnapshotBytes(snapshot);
				const bool accepted = observer_.desktopAutoSaveRequested(
					observer_.context, trigger, std::move(snapshot));
				if (accepted && acceptedFileGuid) *acceptedFileGuid = *fileGuid;
				std::fprintf(stdout,
					"[Draw3.AutoSave] action=capture trigger=%s strokes=%zu bytes=%llu elapsed_ms=%.3f\n",
					trigger == DesktopAutoSaveTrigger::Exit ? "exit" : "clear", strokeCount,
					static_cast<unsigned long long>(estimatedBytes),
					GetQpcTimeMilliseconds() - startedMilliseconds);
				return accepted;
			}
			catch (...)
			{
				std::fputs("[Draw3.AutoSave] action=capture result=failed reason=exception\n",
					stderr);
				return false;
			}
		};
		publishCurrentPageContent();
		uint64_t nextRasterStateToken = 1;
		auto allocateRasterStateToken = [&]()
		{
			if (nextRasterStateToken == 0) nextRasterStateToken = 1;
			return nextRasterStateToken++;
		};
		pageRuntimeStates.front().rasterState = allocateRasterStateToken();
		uint64_t rasterPipelineGeneration = 1;
		UndoCachePolicy appliedUndoPolicy;
		CompositionCachePolicy appliedCompositionPolicy;
		uint64_t appliedHistoryCachePolicyGeneration = 0;
		{
			const std::scoped_lock lock(historyCachePolicyMutex_);
			appliedUndoPolicy = undoCachePolicy_;
			appliedCompositionPolicy = compositionCachePolicy_;
			appliedHistoryCachePolicyGeneration =
				historyCachePolicyGeneration_.load(std::memory_order_acquire);
		}
		InkHistoryGpuCache historyGpuCache;
		if (!historyGpuCache.Initialize(
			renderer_, appliedUndoPolicy, appliedCompositionPolicy))
		{
			std::cout << "[InkHistory] GPU cache unavailable; history restore may fail."
				<< std::endl;
		}
		std::deque<CompositionMaintenanceItem> compositionMaintenance;
		constexpr size_t kMaximumCompositionMaintenanceItems = 4096;

		auto strokeModelParams = configuration_.modelParams;
		ApplyPredictionMode(strokeModelParams, configuration_.kalmanPredictorParams);
		auto eraserModelParams = configuration_.modelParams;
		eraserModelParams.prediction_params = DisabledPredictorParams{};
		LARGE_INTEGER qpcFrequencyValue = {};
		QueryPerformanceFrequency(&qpcFrequencyValue);
		const int64_t qpcFrequency = qpcFrequencyValue.QuadPart;
		const float reconnectDpiScale = configuration_.dpiScale;
		bool effectiveInvertedPenEraserEnabled = false;
		if constexpr (!kInterruptedStrokeReconnectManualTestModeEnabled)
			effectiveInvertedPenEraserEnabled =
				invertedPenEraserEnabled_.load(std::memory_order_acquire);
		std::cout << "[StrokeReconnect] enabled=" <<
			(GetInterruptedStrokeReconnectEnabled() ? "true" : "false") <<
			" window_ms=" << kInterruptedStrokeReconnectWindowSeconds * 1000.0 <<
			" prediction_endpoint_base_px=" <<
			kInterruptedStrokeReconnectDistanceSlackPx * reconnectDpiScale <<
			" prediction_relative=" << kInterruptedStrokeReconnectEndpointRelativeTolerance <<
			" beyond_horizon_ratio=" <<
			kInterruptedStrokeReconnectBeyondHorizonUncertaintyRatio <<
			" extrapolation_angle_deg=" <<
			kInterruptedStrokeReconnectExtrapolationMaximumAngleDegrees <<
			" terminal_direction_corridor_angle_deg=" <<
			kInterruptedStrokeReconnectExtrapolationMaximumAngleDegrees <<
			" in_horizon_terminal_gap_ms=" <<
			kInterruptedStrokeReconnectInHorizonTerminalMaximumGapSeconds * 1000.0 <<
			" in_horizon_terminal_angle_deg=" <<
			kInterruptedStrokeReconnectInHorizonTerminalMaximumAngleDegrees <<
			" in_horizon_terminal_speed_ratio=[" <<
			kInterruptedStrokeReconnectInHorizonTerminalMinimumSpeedRatio << "," <<
			kInterruptedStrokeReconnectInHorizonTerminalMaximumSpeedRatio << "]" <<
			" comparison_epsilon_px=" <<
			kInterruptedStrokeReconnectComparisonEpsilonPx * reconnectDpiScale <<
			" predicted_base_max_px=" <<
			kInterruptedStrokeReconnectPredictedMaximumDistancePx * reconnectDpiScale <<
			" predicted_adaptive_max_px=" <<
			kInterruptedStrokeReconnectAdaptiveMaximumDistancePx * reconnectDpiScale <<
			" adaptive_distance_speed_ratio=" <<
			kInterruptedStrokeReconnectAdaptiveDistanceSpeedRatio <<
			" adaptive_relative=" <<
			kInterruptedStrokeReconnectAdaptiveRelativeTolerance <<
			" fallback_angle_deg=" << kInterruptedStrokeReconnectMaximumAngleDegrees <<
			" fallback_max_distance_px=" <<
			kInterruptedStrokeReconnectFallbackMaximumDistancePx * reconnectDpiScale <<
			" fallback_speed_ratio=[" << kInterruptedStrokeReconnectMinimumSpeedRatio << "," <<
			kInterruptedStrokeReconnectMaximumSpeedRatio << "] candidates=" <<
			kMaximumInterruptedStrokeReconnectCandidates <<
			" manual_test=" << (kInterruptedStrokeReconnectManualTestModeEnabled ? "true" : "false") <<
			" simulation=" << (kInterruptedStrokeReconnectSimulationEnabled ? "true" : "false") <<
			" simulation_interval_ms=[" <<
			kInterruptedStrokeReconnectSimulationMinimumIntervalMs << "," <<
			kInterruptedStrokeReconnectSimulationMaximumIntervalMs << "]" <<
			" simulation_drop_ms=[" << kInterruptedStrokeReconnectSimulationMinimumDropMs << "," <<
			kInterruptedStrokeReconnectSimulationMaximumDropMs << "]" <<
			" inverted_pen_eraser=" << (effectiveInvertedPenEraserEnabled ? "true" : "false") <<
			std::endl;

		std::vector<std::unique_ptr<RuntimeStroke>> strokePool;
		strokePool.reserve(kPreheatedStrokeCount);
		for (size_t index = 0; index < kPreheatedStrokeCount; ++index)
		{
			auto runtime = std::make_unique<RuntimeStroke>(configuration_.expectedSpeed);
			if (absl::Status status = runtime->stroke.modeler.Reset(strokeModelParams); !status.ok())
			{
				std::cout << "Failed to preheat stroke model: " << status.message() << std::endl;
				return;
			}
			strokePool.push_back(std::move(runtime)); // 首批模型和 predictor 在接收 Down 前完成分配。
		}
		std::vector<RuntimeStroke*> active;
		uint64_t nextDiagnosticStrokeId = 0;
		auto publishPenDiagnostics = [&](const RuntimeStroke& runtime,
			std::span<const InkPoint> visible)
		{
			if (!observer_.penDiagnostics || !runtime.stroke.useDisplayTime) return;
			const auto& stroke = runtime.stroke;
			const InkPoint tip = visible.empty() ? stroke.inputStartPoint : visible.back();
			const InkPoint realTip = stroke.realPoints.empty() ? stroke.inputStartPoint : stroke.realPoints.back();
			PenRuntimeDiagnostics diagnostic;
			diagnostic.strokeId = runtime.diagnosticStrokeId;
			diagnostic.inputSequence = runtime.lastConsumedSequence;
			diagnostic.modelUpdateCount = runtime.diagnosticModelUpdates;
			diagnostic.realPointCount = stroke.realPoints.size();
			diagnostic.l0PointCount = visible.size();
			diagnostic.committedRealIndex = stroke.committedIndex;
			diagnostic.endpointError = std::hypot(realTip.x - runtime.lastModelSnapshot.position.x,
				realTip.y - runtime.lastModelSnapshot.position.y);
			diagnostic.tipRadius = tip.r;
			diagnostic.baseRadius = realTip.r;
			diagnostic.displayTime = ResolvePenDisplayTime(stroke);
			diagnostic.physicalUpTime = stroke.terminalDisplayTime.value_or(0.0);
			diagnostic.deviceType = static_cast<uint32_t>(runtime.metricDeviceType);
			diagnostic.terminalLocked = stroke.terminalDisplayTime.has_value();
			diagnostic.awaitingReconnect = runtime.awaitingReconnect;
			diagnostic.rawMove = { stroke.terminalRawMove.x, stroke.terminalRawMove.y };
			diagnostic.rawUp = { stroke.terminalRawUp.x, stroke.terminalRawUp.y };
			diagnostic.modelTailCount = stroke.terminalModelCount;
			diagnostic.acceptedTailCount = stroke.terminalAcceptedCount;
			diagnostic.tailTraceTruncated = stroke.terminalModelCount > 8 || stroke.terminalAcceptedCount > 8;
			for (size_t i = 0; i < 8; ++i)
			{
				diagnostic.modelTail[i] = { stroke.terminalModelTrace[i].x, stroke.terminalModelTrace[i].y };
				diagnostic.acceptedTail[i] = { stroke.terminalAcceptedTrace[i].x, stroke.terminalAcceptedTrace[i].y };
			}
			diagnostic.active = !runtime.ended;
			diagnostic.recovering = stroke.endpointAdmission.recovering;
			diagnostic.frozen = stroke.idleFrozen;
			observer_.penDiagnostics(observer_.context, diagnostic);
		};
		active.reserve(kPreheatedStrokeCount);
		bool publishedDrawingActivity = false;
		auto reconcileDrawingActivity = [&]() noexcept
		{
			const bool activeNow = HasPhysicalContact(active);
			if (activeNow == publishedDrawingActivity) return;
			publishedDrawingActivity = activeNow;
			if (observer_.drawingActivityChanged)
				observer_.drawingActivityChanged(observer_.context, activeNow);
		};
		SpeedEraser::MouseLifecycle leftMouseSpeedEraser,rightMouseSpeedEraser;
		SpeedEraser::InputEntry mouseEraserEntry=SpeedEraser::InputEntry::MouseLeft;
		auto mouseSpeedEraser=[&]() -> SpeedEraser::MouseLifecycle&
		{return mouseEraserEntry==SpeedEraser::InputEntry::MouseRight?rightMouseSpeedEraser:leftMouseSpeedEraser;};
		double mouseVisualSeconds = 0.0;
		SpeedEraserHoverLane penEraserHoverLane;
		SpeedEraserHoverLane invertedPenEraserHoverLane;
		uint32_t observedEraserWidthModeRevision =
			window_.ActiveEraserWidthModeRevision();
		SpeedEraser::DisplayScale observedSpeedEraserDisplayScale =
			window_.SpeedEraserDisplayScaleSnapshot();
		SpeedEraser::DeviceMode observedSpeedEraserDeviceMode =
			window_.SpeedEraserDeviceModeSnapshot();
		CanvasTouchGestureState touchGesture;
		CanvasPanMotionState panMotion;
		std::vector<CanvasGestureContactRuntime> gestureContacts;
		gestureContacts.reserve(kPreheatedStrokeCount);
		bool suppressPenUntilRelease = false;
		CanvasVector previousPanCentroid = {};
		bool panCentroidValid = false;
		size_t previousPanContactCount = 0;
		int64_t lastPanInputQpc = 0;
		int64_t lastTouchPanEndQpc = 0;
		int64_t suppressedPenTerminalQpc = 0;
		int64_t lastNavigationQpc = 0;
		int64_t lastPanReleaseQpc = 0;
		int64_t nextPanMoveDiagnosticQpc = 0;
		int64_t nextPanAnomalyDiagnosticQpc = 0;
		bool inertiaFirstStepDiagnosticPending = false;
		bool inertiaBrakeStateValid = false;
		bool previousInertiaBrake = false;
		LaserTrailLifecycle laserLifecycle;
		ProductVisualStyle laserTrailVisualStyle = currentProductVisualStyle;
		float laserOpacity = 0.0f;
		RECT laserStableBounds = {};
		RECT laserLiveBounds = {};
		RECT pendingLaserBakeDirty = {};
		std::vector<LaserStrokeLayer> laserStrokeLayers;
		LaserCoverageMode laserCoverageMode = LaserCoverageMode::Inactive;
		bool laserIncrementalEnsureAttempted = false;
		uint64_t nextLaserLayerId = 1;
		std::vector<LaserTipVisual> laserTipVisuals;
		std::vector<ShapePrimitive> shapePrimitiveScratch;
		laserStrokeLayers.reserve(kLaserReservedContactCount);
		laserTipVisuals.reserve(kPreheatedStrokeCount + 1);
		shapePrimitiveScratch.reserve(kLaserReservedContactCount * 2);
		std::vector<LaserParticleEmissionRequest> laserParticleEmissionRequests;
		laserParticleEmissionRequests.reserve(kLaserReservedContactCount);
		HighlighterGeometry completedHighlighterScratch;
		std::vector<InkPoint> redoRebuildPoints;
		HighlighterGeometry redoHighlighterScratch;
		LaserParticleDirtyTracker laserParticleDirtyTracker;
		const LaserParticleConfig laserParticleConfiguration =
			IsValidLaserParticleConfig(configuration_.laserParticleConfig)
			? configuration_.laserParticleConfig : LaserParticleConfig{};
		RECT previousLaserParticleBounds = {};
		RECT previousLaserTipBounds = {};
		int64_t lastLaserParticleSimulationQpc = 0;
		bool particlesWereEnabled =
			laserParticlesEnabled_.load(std::memory_order_acquire) &&
			renderer_.LaserParticlesAvailable();
		bool viewportRefreshPending = false;
		bool viewportRefreshClearsTransient = false;
		CanvasRenderTilePlan viewportTilePlan;
		size_t viewportTilePlanIndex = 0;
		bool viewportRecoveryPending = false;
		bool viewportVisibleClear = true;
		double viewportTileEwmaMilliseconds = 0.5;
		double previousCanvasFrameWorkMilliseconds = 0.0;
		double previousCanvasPresentMilliseconds = 0.0;
		bool trustedSnapshotSignatureValid = false;
		size_t trustedSnapshotPageIndex = 0;
		uint64_t trustedSnapshotRevision = 0;
		InkViewport trustedSnapshotViewport = {};
		// 有效状态：只在激光生命周期为 Inactive 时才同步开关，
		// 避免开关在绘制中/Hold/Fade 期间立即生效打断当前笔画或粒子动画。
		bool particlesEnabledEffective = particlesWereEnabled;
		const int originalThreadPriority = GetThreadPriority(GetCurrentThread());
		const bool drawingPriorityRaised = originalThreadPriority != THREAD_PRIORITY_ERROR_RETURN &&
			SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_ABOVE_NORMAL) != FALSE;
		// 绘制线程只在活动期占用 CPU；提高一级优先级降低 120 FPS deadline 被后台窗口抢占的概率。

		auto addGestureContact = [&](ContactHandle handle,
			CanvasTouchDisposition disposition, CanvasPanContactAnchorMode anchorMode)
		{
			if (!handle.record) return;
			const uint64_t key = CanvasTouchKey(handle);
			if (std::any_of(gestureContacts.begin(), gestureContacts.end(),
				[key](const CanvasGestureContactRuntime& contact)
				{ return contact.key == key; })) return;
			const ContactSnapshot downSnapshot = handle.record->DownSnapshot();
			ContactSnapshot currentSnapshot = downSnapshot;
			input_.TryReadSnapshot(handle, currentSnapshot);
			const bool currentTerminal = currentSnapshot.phase == ContactPhase::Up ||
				currentSnapshot.phase == ContactPhase::Cancelled;
			const CanvasPanContactAnchor anchor = ResolveCanvasPanContactAnchor(
				{ { downSnapshot.position.x, downSnapshot.position.y },
					downSnapshot.sequence, false },
				{ { currentSnapshot.position.x, currentSnapshot.position.y },
					currentSnapshot.sequence, false },
				anchorMode, currentTerminal);
			ContactSnapshot snapshot = anchorMode == CanvasPanContactAnchorMode::Down
				? downSnapshot : currentSnapshot;
			gestureContacts.push_back({
				handle, key, snapshot, snapshot.position,
				anchor.sequence, disposition, anchor.terminalPending });
			LogCanvasPan("pan-contact-acquire key=%llu anchor=%s down-sequence=%llu current-sequence=%llu selected-sequence=%llu phase=%u terminal-pending=%u",
				static_cast<unsigned long long>(key),
				anchorMode == CanvasPanContactAnchorMode::Down ? "down" : "current",
				static_cast<unsigned long long>(downSnapshot.sequence),
				static_cast<unsigned long long>(currentSnapshot.sequence),
				static_cast<unsigned long long>(anchor.sequence),
				static_cast<unsigned>(snapshot.phase), anchor.terminalPending ? 1u : 0u);
		};

		auto hasLiveSuppressedPenContact = [&]() noexcept
		{
			return std::any_of(gestureContacts.begin(), gestureContacts.end(),
				[&](const CanvasGestureContactRuntime& contact)
				{
					if (!contact.handle.record ||
						contact.handle.record->DeviceType() != InputDeviceType::Pen)
						return false;
					ContactSnapshot latest = contact.snapshot;
					input_.TryReadSnapshot(contact.handle, latest);
					return latest.phase != ContactPhase::Up &&
						latest.phase != ContactPhase::Cancelled;
				});
		};

		auto penDownBelongsToTouchPan = [&](int64_t penDownQpc) noexcept
		{
			bool touchPanContactLive = false;
			int64_t observedTouchPanEndQpc = lastTouchPanEndQpc;
			for (const CanvasGestureContactRuntime& contact : gestureContacts)
			{
				if (contact.handle.record == nullptr ||
					contact.handle.record->DeviceType() != InputDeviceType::Touch ||
					touchGesture.Disposition(contact.key) != CanvasTouchDisposition::Pan)
					continue;
				ContactSnapshot latest = contact.snapshot;
				input_.TryReadSnapshot(contact.handle, latest);
				if (latest.phase != ContactPhase::Up &&
					latest.phase != ContactPhase::Cancelled)
					touchPanContactLive = true;
				else
					observedTouchPanEndQpc = (std::max)(
						observedTouchPanEndQpc, latest.qpc);
			}
			return ShouldSuppressPenContactForTouchPan(
				touchPanContactLive, penDownQpc, observedTouchPanEndQpc,
				hasLiveSuppressedPenContact());
		};

		auto runtimeContactPhysicallyLive = [&](const RuntimeStroke& runtime) noexcept
		{
			ContactSnapshot latest = runtime.lastInputSnapshot;
			input_.TryReadSnapshot(runtime.handle, latest);
			return latest.phase != ContactPhase::Up &&
				latest.phase != ContactPhase::Cancelled;
		};

		auto speedEraserHoverLaneFor = [&](InputDeviceType deviceType,bool inverted) -> SpeedEraserHoverLane*
		{
			return deviceType==InputDeviceType::Pen?(inverted?&invertedPenEraserHoverLane:&penEraserHoverLane):nullptr;
		};
		auto sessionForEntry=[&](SpeedEraser::InputEntry entry) -> SpeedEraser::MouseLifecycle*
		{
			switch(entry)
			{
			case SpeedEraser::InputEntry::MouseLeft:return &leftMouseSpeedEraser;
			case SpeedEraser::InputEntry::MouseRight:return &rightMouseSpeedEraser;
			case SpeedEraser::InputEntry::PenTip:return &penEraserHoverLane.lifecycle;
			case SpeedEraser::InputEntry::PenTail:return &invertedPenEraserHoverLane.lifecycle;
			default:return nullptr;
			}
		};
		auto observedEraserInputs=window_.EraserInputsSnapshot();
		auto observedMouseExit=window_.MouseCanvasExitRevision();
		auto observedEraserPolicy=window_.EraserToolPolicySnapshot();
		auto eraserSource=[](InputDeviceType type,const SpeedEraser::InputSource& source)
		{
			return type==InputDeviceType::MouseLeft || type==InputDeviceType::MouseRight?
				SpeedEraser::InputSource{SpeedEraser::SourceKind::Mouse}:source;
		};
		auto synchronizeSpeedEraserHoverMode = [&]() noexcept
		{
			// 只更新低频快照；每个入口在解析后按有效配置决定是否重建，不全局Reset。
			observedEraserWidthModeRevision=window_.ActiveEraserWidthModeRevision();
			observedSpeedEraserDisplayScale=window_.SpeedEraserDisplayScaleSnapshot();
			observedSpeedEraserDeviceMode=window_.SpeedEraserDeviceModeSnapshot();
			observedEraserInputs=window_.EraserInputsSnapshot();
			observedEraserPolicy=window_.EraserToolPolicySnapshot();
			return observedEraserWidthModeRevision;
		};
		auto entryCanErase=[&](SpeedEraser::InputEntry entry,DrawingTool selected)
		{
			if(selected==DrawingTool::Eraser)return true;
			const bool overrideTool=selected==DrawingTool::Pen || selected==DrawingTool::HardPen ||
				selected==DrawingTool::Highlighter || IsShapeDrawingTool(selected);
			return overrideTool && (entry==SpeedEraser::InputEntry::MouseRight ||
				(entry==SpeedEraser::InputEntry::PenTail && invertedPenEraserEnabled_.load(std::memory_order_acquire)));
		};
		auto initializeSpeedEraserController = [&](RuntimeStroke& runtime,const ContactSnapshot& down)
		{
			const double seconds=AbsoluteQpcSeconds(down.qpc,qpcFrequency);
			runtime.eraserTimeOrigin=seconds;runtime.lastInputSnapshot=down;
			const auto& config=runtime.resolvedEraser.config;
			if(runtime.metricDeviceType==InputDeviceType::Touch)
			{
				const auto area=ContactAreaFromSnapshot(down);
				runtime.speedEraserOc.Reset(down.position.x,down.position.y,seconds,SpeedEraserStartKind::Touch,config,0,&area);
				return;
			}
			auto* session=sessionForEntry(runtime.resolvedEraser.entry);
			// Down队列可能先于旧Up的绘制消费；只以已到达的真实终态构造尺寸交还，不造几何。
			for(auto* old:active)
			{
				if(!old || old==&runtime || old->mouseSpeedEraserFinished ||
					old->stroke.widthMode!=StrokeWidthMode::SpeedEraser ||
					old->resolvedEraser.entry!=runtime.resolvedEraser.entry)continue;
				ContactSnapshot latest=old->lastInputSnapshot;input_.TryReadSnapshot(old->handle,latest);
				if((latest.phase==ContactPhase::Up || latest.phase==ContactPhase::Cancelled) && latest.qpc<=down.qpc)
				{
					auto ended=old->speedEraserOc;
					ended.UpdatePosition(latest.position.x,latest.position.y,
						AbsoluteQpcSeconds(latest.qpc,qpcFrequency),nullptr,true);
					session->EndContact(ended,ended.Diameter(),latest.position.x,latest.position.y,
						AbsoluteQpcSeconds(latest.qpc,qpcFrequency),false,latest.phase==ContactPhase::Cancelled,old->eraserSessionToken);
					old->mouseSpeedEraserFinished=true;
				}
			}
			runtime.mouseSpeedEraserFinished=false;
			runtime.eraserSessionToken=session->BeginContact(runtime.speedEraserOc,down.position.x,down.position.y,seconds,config);
			if(runtime.metricDeviceType==InputDeviceType::MouseLeft || runtime.metricDeviceType==InputDeviceType::MouseRight)
				mouseEraserEntry=runtime.resolvedEraser.entry;
		};
		auto finishMouseSpeedEraser = [&](RuntimeStroke& runtime)
		{
			if(runtime.stroke.widthMode!=StrokeWidthMode::SpeedEraser ||
				runtime.metricDeviceType==InputDeviceType::Touch || runtime.mouseSpeedEraserFinished)return;
			runtime.mouseSpeedEraserFinished=true;
			auto* session=sessionForEntry(runtime.resolvedEraser.entry);
			const bool anotherOwner=std::any_of(active.begin(),active.end(),[&](const RuntimeStroke* other)
			{
				return other && other!=&runtime && !other->ended && !other->mouseSpeedEraserFinished &&
					other->stroke.widthMode==StrokeWidthMode::SpeedEraser && other->resolvedEraser.entry==runtime.resolvedEraser.entry;
			});
			const auto current=SpeedEraser::ResolveInput(window_.SpeedEraserDisplayScaleSnapshot(),
				window_.SpeedEraserDeviceModeSnapshot(),runtime.resolvedEraser.config.inputSource,runtime.resolvedEraser.entry,
				window_.EraserInputsSnapshot(),window_.ActiveTool()==DrawingTool::Eraser?
					window_.EraserToolPolicySnapshot():SpeedEraser::EraserToolPolicy::ByEntry);
			const bool cancelled=runtime.cancelled || runtime.lastInputSnapshot.phase==ContactPhase::Cancelled ||
				window_.SelectionMode() || !entryCanErase(runtime.resolvedEraser.entry,window_.ActiveTool()) ||
				current.kind==SpeedEraser::EraserKind::Fixed || !SpeedEraser::SessionConfigCompatible(runtime.resolvedEraser.config,current.config);
			const auto& last=runtime.lastInputSnapshot;
			session->EndContact(runtime.speedEraserOc,RuntimeSpeedEraserContactDiameter(runtime),
				last.position.x,last.position.y,AbsoluteQpcSeconds(last.qpc,qpcFrequency),anotherOwner,cancelled,runtime.eraserSessionToken);
		};
		auto handBackSpeedEraserController = [&](RuntimeStroke& runtime)
		{
			finishMouseSpeedEraser(runtime); // 延后烘干只允许退休自己的票据，不能覆盖新段。
		};
		auto cancelTouchDrawingForPan = [&]()
		{
			for (RuntimeStroke* runtime : active)
			{
				if (!runtime || runtime->ended ||
					runtime->metricDeviceType != InputDeviceType::Touch ||
					!touchGesture.HasContact(runtime->touchGestureKey) ||
					touchGesture.Disposition(runtime->touchGestureKey) !=
						CanvasTouchDisposition::Pan) continue;
				ContactSnapshot latest = runtime->lastInputSnapshot;
				input_.TryReadSnapshot(runtime->handle, latest);
				const bool terminal = latest.phase == ContactPhase::Up ||
					latest.phase == ContactPhase::Cancelled;
				if (terminal)
				{
					// 已进 mailbox 的终态在 handoff 点同步退休，不能留下无 runtime 的 FSM contact。
					const CanvasTouchDisposition retired = touchGesture.OnTouchUp(
						runtime->touchGestureKey);
					input_.Recycle(runtime->handle);
					LogCanvasPan("touch-pan-handoff-retire key=%llu qpc=%lld phase=%u disposition=%s",
						static_cast<unsigned long long>(runtime->touchGestureKey),
						static_cast<long long>(latest.qpc),
						static_cast<unsigned>(latest.phase),
						CanvasTouchDispositionName(retired));
				}
				else
					addGestureContact(runtime->handle, CanvasTouchDisposition::Pan,
						CanvasPanContactAnchorMode::Current);
				runtime->handle = {}; // gesture runtime 接管或同步退休后，drawing 不再拥有 handle。
				runtime->ended = true;
				runtime->cancelled = true;
				runtime->awaitingReconnect = false;
				runtime->visibleDirty = GetFullCanvasRect(
					window_.Size().width, window_.Size().height);
			}
			for (CanvasGestureContactRuntime& contact : gestureContacts)
				contact.disposition = touchGesture.Disposition(contact.key);
			// Touch 的任意工具临时层都丢弃，下一帧只从剩余 Pen/Mouse runtime 重建。
			renderer_.ClearOperatorLayer(renderer_.layerL1);
			renderer_.ClearOperatorLayer(renderer_.layerL0);
			renderer_.ClearAllLaserCoverage();
			renderer_.ResetLaserParticles();
			laserLifecycle = {};
			laserOpacity = 0.0f;
			laserStableBounds = {};
			laserLiveBounds = {};
			laserStrokeLayers.clear();
			laserCoverageMode = LaserCoverageMode::Inactive;
			laserParticleDirtyTracker.Clear();
			viewportRefreshPending = true;
			viewportRefreshClearsTransient = true;
		};

		auto rebuildPanContactBaseline = [&](int64_t inputQpc,
			bool resetVelocitySamples)
		{
			const size_t oldContactCount = previousPanContactCount;
			const int64_t oldLastUpdateQpc = panMotion.lastUpdateQpc;
			previousPanCentroid = {};
			previousPanContactCount = 0;
			for (CanvasGestureContactRuntime& contact : gestureContacts)
			{
				if (touchGesture.Disposition(contact.key) !=
					CanvasTouchDisposition::Pan) continue;
				// 触点拓扑变化时，位置中心和估速中心必须从同一快照重新起步。
				contact.velocityPosition = contact.snapshot.position;
				previousPanCentroid.x += contact.snapshot.position.x;
				previousPanCentroid.y += contact.snapshot.position.y;
				++previousPanContactCount;
			}
			panCentroidValid = previousPanContactCount > 0;
			if (panCentroidValid)
			{
				previousPanCentroid.x /= static_cast<float>(previousPanContactCount);
				previousPanCentroid.y /= static_cast<float>(previousPanContactCount);
				if (resetVelocitySamples)
					ResetCanvasPanVelocitySamples(panMotion,
						(std::max)(inputQpc, panMotion.lastUpdateQpc));
			}
			LogCanvasPan("pan-baseline reset-velocity=%u input-qpc=%lld last-update-before=%lld last-update-after=%lld contacts-before=%zu contacts-after=%zu centroid=(%.2f,%.2f)",
				resetVelocitySamples ? 1u : 0u, static_cast<long long>(inputQpc),
				static_cast<long long>(oldLastUpdateQpc),
				static_cast<long long>(panMotion.lastUpdateQpc), oldContactCount,
				previousPanContactCount, previousPanCentroid.x, previousPanCentroid.y);
			for (const CanvasGestureContactRuntime& contact : gestureContacts)
			{
				if (touchGesture.Disposition(contact.key) !=
					CanvasTouchDisposition::Pan) continue;
				LogCanvasPan("pan-baseline-contact key=%llu snapshot=(%.2f,%.2f) velocity-position=(%.2f,%.2f) qpc=%lld phase=%u sequence=%llu",
					static_cast<unsigned long long>(contact.key),
					contact.snapshot.position.x, contact.snapshot.position.y,
					contact.velocityPosition.x, contact.velocityPosition.y,
					static_cast<long long>(contact.snapshot.qpc),
					static_cast<unsigned>(contact.snapshot.phase),
					static_cast<unsigned long long>(contact.snapshot.sequence));
			}
			const size_t terminalPending = static_cast<size_t>(std::count_if(
				gestureContacts.begin(), gestureContacts.end(),
				[&](const CanvasGestureContactRuntime& contact)
				{
					return contact.terminalPending && touchGesture.Disposition(contact.key) ==
						CanvasTouchDisposition::Pan;
				}));
			const size_t fsmPanContacts = touchGesture.PanContactCount();
			const bool lifecycleConsistent = IsCanvasPanLifecycleOwnershipConsistent(
				touchGesture.PanActive(), fsmPanContacts,
				previousPanContactCount, terminalPending);
			LogCanvasPan("pan-lifecycle panActive=%u fsmPanContacts=%zu gestureRuntimePanContacts=%zu terminalPendingPanContacts=%zu consistent=%u",
				touchGesture.PanActive() ? 1u : 0u, fsmPanContacts,
				previousPanContactCount, terminalPending,
				lifecycleConsistent ? 1u : 0u);
		};

		auto retireEndedTouchBeforeDown = [&](int64_t downQpc)
		{
			if (touchGesture.PanActive() || touchGesture.ContactCount() != 1) return;
			auto retire = [&](uint64_t key, ContactHandle handle,
				ContactSnapshot fallback) -> bool
				{
				if (key == 0 || !touchGesture.HasContact(key)) return false;
				ContactSnapshot latest = fallback;
				input_.TryReadSnapshot(handle, latest);
				if ((latest.phase != ContactPhase::Up &&
					latest.phase != ContactPhase::Cancelled) ||
					latest.qpc > downQpc) return false;
				touchGesture.OnTouchUp(key);
				LogCanvasPan("touch-retire-before-down key=%llu terminal-qpc=%lld down-qpc=%lld",
					static_cast<unsigned long long>(key),
					static_cast<long long>(latest.qpc),
					static_cast<long long>(downQpc));
				return true;
			};
			for (const CanvasGestureContactRuntime& contact : gestureContacts)
			{
				if (!contact.handle.record || contact.handle.record->DeviceType() !=
					InputDeviceType::Touch) continue;
				if (retire(contact.key, contact.handle, contact.snapshot)) return;
			}
			for (const RuntimeStroke* runtime : active)
			{
				if (!runtime || runtime->metricDeviceType != InputDeviceType::Touch) continue;
				if (retire(runtime->touchGestureKey, runtime->handle,
					runtime->lastInputSnapshot)) return;
			}
		};

		auto interruptNavigationForPenOrMouse = [&](const char* reason)
			{
				const float speedBefore = CanvasPanSpeed(panMotion);
				if (touchGesture.PanActive() || panMotion.inertiaActive || speedBefore > 0.0f)
					LogCanvasPan("navigation-interrupt reason=%s pan=%u inertia=%u velocity=(%.1f,%.1f) speed=%.1f contacts=%zu",
						reason ? reason : "unknown", touchGesture.PanActive() ? 1u : 0u,
						panMotion.inertiaActive ? 1u : 0u, panMotion.velocity.x,
						panMotion.velocity.y, speedBefore, touchGesture.ContactCount());
				InterruptCanvasPanForDrawing(panMotion, touchGesture);
				window_.SetTouchPanActive(false);
				for (CanvasGestureContactRuntime& contact : gestureContacts)
					contact.disposition = CanvasTouchDisposition::Suppressed;
				panCentroidValid = false;
				previousPanContactCount = 0;
				inertiaFirstStepDiagnosticPending = false;
				inertiaBrakeStateValid = false;
			};

		auto acquireStroke = [&]() -> RuntimeStroke*
			{
				for (const auto& candidate : strokePool)
				{
					if (!candidate->inUse)
					{
						candidate->inUse = true;
						return candidate.get();
					}
				}
				auto runtime = std::make_unique<RuntimeStroke>(configuration_.expectedSpeed);
				if (absl::Status status = runtime->stroke.modeler.Reset(strokeModelParams); !status.ok())
				{
					std::cout << "Failed to initialize expanded stroke model: " << status.message() << std::endl;
					return nullptr;
				}
				runtime->inUse = true;
				strokePool.push_back(std::move(runtime));
				return strokePool.back().get();
			};

		auto updateContactModel = [&](RuntimeStroke& runtime, const Input& next,
			double inputTime, std::vector<ink::stroke_model::Result>& modelOutput)
		{
			const auto& sampling=eraserModelParams.sampling_params;
			const bool touchSpeed=runtime.metricDeviceType==InputDeviceType::Touch &&
				runtime.stroke.widthMode==StrokeWidthMode::SpeedEraser;
			const double gap=inputTime-runtime.speedEraserModelTime;
			if(touchSpeed && !runtime.stroke.realPoints.empty() && sampling.min_output_rate>0 &&
				sampling.max_outputs_per_call>2 &&
				gap*sampling.min_output_rate>sampling.max_outputs_per_call-2)
			{
				// 长时间没有建模输入时，只恢复位置模型；不重新开始接触、不重置尺寸或改写旧结果。
				const auto& last=runtime.stroke.realPoints.back();
				Input anchor=next;anchor.event_type=Input::EventType::kDown;
				anchor.position=Vec2(last.x,last.y);
				anchor.time=Time(inputTime-1.0/sampling.min_output_rate);
				if(auto status=runtime.stroke.modeler.Reset(eraserModelParams);!status.ok())return status;
				if(auto status=runtime.stroke.modeler.Update(anchor,runtime.stroke.modeledResults);!status.ok())return status;
				runtime.stroke.predictedResults.clear();
				runtime.stroke.predictedPoints.clear();
				++runtime.eraserDiagnostics.idleModelReanchors;
				// anchor 仅供既有模型续接，绝不交给速度/面积控制器当成真实运动。
			}
			if (observer_.penDiagnostics && runtime.stroke.useDisplayTime)
				++runtime.diagnosticModelUpdates;
			return runtime.stroke.modeler.Update(next, modelOutput);
		};

		auto initializeStroke = [&](ContactHandle handle) -> bool
			{
				if (!handle.record || handle.record->Generation() != handle.generation) return false;
				const ContactSnapshot down = handle.record->DownSnapshot();
				const InputDeviceType deviceType = handle.record->DeviceType();
				const bool penBeganDuringTouchPan = deviceType == InputDeviceType::Pen &&
					penDownBelongsToTouchPan(down.qpc);
				if (deviceType == InputDeviceType::Pen &&
					(penBeganDuringTouchPan || hasLiveSuppressedPenContact()))
				{
					// Touch 跟手批次中的 Pen 只跟踪到抬笔，不能在惯性阶段补画。
					suppressPenUntilRelease = true;
					addGestureContact(handle, CanvasTouchDisposition::Suppressed,
						CanvasPanContactAnchorMode::Current);
					return true;
				}
				if (deviceType == InputDeviceType::Pen)
				{
					suppressPenUntilRelease = false;
					suppressedPenTerminalQpc = 0;
				}
				if (deviceType == InputDeviceType::Pen ||
					deviceType == InputDeviceType::MouseLeft ||
					deviceType == InputDeviceType::MouseRight)
				{
					interruptNavigationForPenOrMouse("drawing-contact");
				}
				else if (deviceType == InputDeviceType::Touch)
				{
					// 新 Down 可能先于旧 Up 的导航消费到达；先退休已物理结束的旧批次。
					retireEndedTouchBeforeDown(down.qpc);
					const bool blockingContactActive = std::any_of(
						active.begin(), active.end(), [](const RuntimeStroke* runtime)
						{
							return runtime && !runtime->ended && !runtime->awaitingReconnect &&
								runtime->metricDeviceType != InputDeviceType::Touch;
						});
					const bool canvasNavigationBlocked = blockingContactActive ||
						!kCanvasNavigationProductIntegrationEnabled;
					const uint64_t key = CanvasTouchKey(handle);
					const bool inheritedInertia = panMotion.inertiaActive;
					const size_t contactCountBefore = touchGesture.ContactCount();
					const int64_t firstDownQpc = touchGesture.FirstDownQpc();
					const bool batchAllowedBefore = touchGesture.BatchAllowsPan();
					const double gapMilliseconds = contactCountBefore == 1
						? QpcDeltaSeconds(down.qpc, firstDownQpc, qpcFrequency) * 1000.0
						: -1.0;
					const CanvasTouchDecision touchDecision = touchGesture.OnTouchDown(
						key, down.qpc, qpcFrequency, inheritedInertia,
						canvasNavigationBlocked);
					LogCanvasPan("touch-down key=%llu order=%zu qpc=%lld first-qpc=%lld gap-ms=%.3f within-180=%u blocking=%u inherited-inertia=%u batch-before=%u decision=%s begin=%u joined=%u cancel-draw=%u",
						static_cast<unsigned long long>(key), contactCountBefore + 1,
						static_cast<long long>(down.qpc), static_cast<long long>(
							contactCountBefore == 0 ? down.qpc : firstDownQpc), gapMilliseconds,
						contactCountBefore == 1 && gapMilliseconds >= 0.0 &&
							gapMilliseconds <= kCanvasPanGestureWindowSeconds * 1000.0 ? 1u : 0u,
						canvasNavigationBlocked ? 1u : 0u, inheritedInertia ? 1u : 0u,
						batchAllowedBefore ? 1u : 0u,
						CanvasTouchDispositionName(touchDecision.disposition),
						touchDecision.beginPan ? 1u : 0u,
						touchDecision.joinedExistingPan ? 1u : 0u,
						touchDecision.cancelExistingTouchDrawing ? 1u : 0u);
					if (touchDecision.disposition != CanvasTouchDisposition::Draw)
					{
						addGestureContact(handle, touchDecision.disposition,
							CanvasPanContactAnchorMode::Down);
						if (touchDecision.beginPan)
						{
							const CanvasVector residualVelocity = panMotion.velocity;
							const int64_t residualSourceQpc = panMotion.lastUpdateQpc;
							const int64_t velocitySourceQpc = panMotion.lastVelocitySampleQpc;
							BeginCanvasPan(panMotion, inheritedInertia, down.qpc);
							const double inertiaAgeMilliseconds = inheritedInertia &&
								lastPanReleaseQpc > 0
								? QpcDeltaSeconds(down.qpc, lastPanReleaseQpc,
									qpcFrequency) * 1000.0 : -1.0;
							LogCanvasPan("pan-begin inherited=%u residual=(%.1f,%.1f) speed=%.1f contacts=%zu down-qpc=%lld release-qpc=%lld inertia-age-ms=%.3f residual-source-qpc=%lld velocity-source-qpc=%lld",
								inheritedInertia ? 1u : 0u, residualVelocity.x,
								residualVelocity.y, std::hypot(residualVelocity.x,
									residualVelocity.y), touchGesture.GestureContactCount(),
								static_cast<long long>(down.qpc),
								static_cast<long long>(lastPanReleaseQpc),
								inertiaAgeMilliseconds,
								static_cast<long long>(residualSourceQpc),
								static_cast<long long>(velocitySourceQpc));
							window_.SetTouchPanActive(true);
							lastPanInputQpc = down.qpc;
							if (touchDecision.cancelExistingTouchDrawing)
								cancelTouchDrawingForPan();
							rebuildPanContactBaseline(down.qpc, false);
							nextPanMoveDiagnosticQpc = down.qpc;
							inertiaFirstStepDiagnosticPending = false;
							inertiaBrakeStateValid = false;
						}
						else if (touchDecision.joinedExistingPan)
						{
							rebuildPanContactBaseline(down.qpc, true);
						}
						return true;
					}
				}
				DrawingTool batchTool = window_.ActiveTool();
				uint32_t batchEraserWidthModeRevision =
					synchronizeSpeedEraserHoverMode();
				EraserWidthMode batchEraserWidthMode = EraserWidthModeForRevision(
					batchEraserWidthModeRevision);
				auto batchSpeedEraserDisplayScale = observedSpeedEraserDisplayScale;
				auto batchSpeedEraserDeviceMode = observedSpeedEraserDeviceMode;
				bool batchTouchArea=window_.TouchContactAreaAssistance();
				auto batchEraserInputs=observedEraserInputs;
				auto batchEraserPolicy=observedEraserPolicy;
				bool hasSpeedEraserBatchContact = false;
				bool hasActiveBatchContact = false;
				bool hasActiveLaserTouchContact = false;
				for (RuntimeStroke* activeRuntime : active)
				{
					if (activeRuntime && !activeRuntime->cancelled && !hasSpeedEraserBatchContact)
					{
						ContactSnapshot batchLatest = activeRuntime->lastInputSnapshot;
						input_.TryReadSnapshot(activeRuntime->handle, batchLatest);
						const bool terminal = batchLatest.phase == ContactPhase::Up ||
							batchLatest.phase == ContactPhase::Cancelled;
						if ((!activeRuntime->ended || terminal) && SpeedEraser::ContactBatchContains(
							activeRuntime->qpcOrigin, terminal ? batchLatest.qpc : 0,
							activeRuntime->awaitingReconnect ? activeRuntime->reconnectDeadlineQpc : 0,
							down.qpc))
						{
							// Up 先被消费不代表 B.Down 时已经结束；用真实 QPC 保留重叠批次。
							batchSpeedEraserDisplayScale = activeRuntime->speedEraserDisplayScale;
							batchSpeedEraserDeviceMode = activeRuntime->speedEraserDeviceMode;
							batchTouchArea=activeRuntime->touchContactAreaAssistance;
							batchEraserInputs=activeRuntime->eraserInputs;
							batchEraserPolicy=activeRuntime->eraserPolicy;
							hasSpeedEraserBatchContact = true;
						}
					}
					if (activeRuntime && !activeRuntime->ended && !activeRuntime->awaitingReconnect &&
						runtimeContactPhysicallyLive(*activeRuntime))
					{
						if (!hasActiveBatchContact)
						{
							batchTool = activeRuntime->selectedTool; // 后加入 contact 沿用首个物理批次状态。
							// 不复制第一个contact已解析出的模式；后续入口仍各自解析。
							batchEraserInputs=activeRuntime->eraserInputs;
							batchEraserPolicy=activeRuntime->eraserPolicy;
							batchEraserWidthModeRevision =
								activeRuntime->eraserWidthModeRevision;
						}
						hasActiveBatchContact = true;
						hasActiveLaserTouchContact = hasActiveLaserTouchContact ||
							(activeRuntime->selectedTool == DrawingTool::Laser &&
								activeRuntime->metricDeviceType == InputDeviceType::Touch);
					}
				}
				if (batchTool == DrawingTool::Laser && deviceType == InputDeviceType::Touch &&
					hasActiveLaserTouchContact &&
					!laserMultiTouchDrawingEnabled_.load(std::memory_order_acquire))
				{
					input_.Recycle(handle); // 关闭多指时忽略后续 Touch，保留第一根手指的完整生命周期。
					return false;
				}
				const bool selectedToolSupportsOverride =
					batchTool == DrawingTool::Pen || batchTool == DrawingTool::HardPen ||
					batchTool == DrawingTool::Highlighter ||
					IsShapeDrawingTool(batchTool);
				bool effectiveInvertedPenEraserEnabled = false;
				if constexpr (!kInterruptedStrokeReconnectManualTestModeEnabled)
					effectiveInvertedPenEraserEnabled =
						invertedPenEraserEnabled_.load(std::memory_order_acquire);
				const bool invertedEraser = ShouldUseInvertedPenEraser(deviceType,
					down.isInvertedCursor, effectiveInvertedPenEraserEnabled,
					selectedToolSupportsOverride);
				const bool rightEraser=deviceType==InputDeviceType::MouseRight && selectedToolSupportsOverride && !window_.SelectionMode();
				const DrawingTool tool = (invertedEraser || rightEraser) ? DrawingTool::Eraser : batchTool;
				const auto entry=SpeedEraser::EntryForInput(static_cast<uint32_t>(deviceType),down.isInvertedCursor);
				auto eraserDisplay=batchSpeedEraserDisplayScale;
				if(deviceType==InputDeviceType::Touch)eraserDisplay.development.touchContactAreaAssistance=batchTouchArea;
				const auto resolvedEraser=SpeedEraser::ResolveInput(eraserDisplay,batchSpeedEraserDeviceMode,
					eraserSource(deviceType,down.source),entry,batchEraserInputs,
					batchTool==DrawingTool::Eraser?batchEraserPolicy:SpeedEraser::EraserToolPolicy::ByEntry);
				batchEraserWidthMode=resolvedEraser.kind==SpeedEraser::EraserKind::Speed?EraserWidthMode::Speed:EraserWidthMode::Fixed;
				const bool suppressPressure = deviceType == InputDeviceType::Pen && down.isInvertedCursor;
				const float downPressure = ResolveStylusPressureForModel(
					deviceType, down.isInvertedCursor, down.pressure);
				const StrokeWidthMode widthMode = tool == DrawingTool::Pen
					? ResolveStrokeWidthMode(deviceType,
						inputWidthModeSettings_.Get(), downPressure)
					: tool == DrawingTool::HardPen
						? StrokeWidthMode::Fixed
					: tool == DrawingTool::Laser
						? (deviceType == InputDeviceType::Pen
							? StrokeWidthMode::LaserPressure
							: StrokeWidthMode::SimulatedPressure)
						: tool == DrawingTool::Eraser &&
							batchEraserWidthMode == EraserWidthMode::Speed
							? StrokeWidthMode::SpeedEraser : StrokeWidthMode::Fixed;
				const InterruptedStrokeReconnectIdentity downIdentity{
					deviceType,
					static_cast<uint32_t>(batchTool),
					static_cast<uint32_t>(tool),
					widthMode,
					down.isInvertedCursor,
					suppressPressure
				};

				RuntimeStroke* reconnectRuntime = nullptr;
				InterruptedStrokeReconnectResult reconnectResult;
				RuntimeStroke* diagnosticRuntime = nullptr;
				InterruptedStrokeReconnectResult diagnosticResult;
				if (GetInterruptedStrokeReconnectEnabled() &&
					IsInterruptedStrokeReconnectIdentitySupported(downIdentity) &&
					tool != DrawingTool::Laser && !IsShapeDrawingTool(tool))
				{
					for (RuntimeStroke* candidate : active)
					{
						if (!candidate || !candidate->awaitingReconnect || candidate->ended) continue;
						InterruptedStrokeReconnectIdentity candidateDownIdentity = downIdentity;
						// C 在候选窗口内切换时，候选仍按原批次宽度模式续接。
						candidateDownIdentity.widthMode = candidate->stroke.widthMode;
						if (!AreInterruptedStrokeReconnectIdentitiesCompatible(
							ReconnectIdentity(*candidate), candidateDownIdentity))
						{
							if (kInterruptedStrokeReconnectManualTestModeEnabled || observer_.penDiagnostics)
							{
								if (!diagnosticRuntime || candidate->deferredUpSnapshot.qpc >
									diagnosticRuntime->deferredUpSnapshot.qpc)
								{
									diagnosticRuntime = candidate;
									diagnosticResult = {};
									diagnosticResult.rejectReason =
										InterruptedStrokeReconnectRejectReason::IdentityMismatch;
									diagnosticResult.gapMilliseconds = QpcDeltaSeconds(
										down.qpc, candidate->deferredUpSnapshot.qpc, qpcFrequency) * 1000.0;
									diagnosticResult.distance = std::hypot(
										down.position.x - candidate->deferredUpSnapshot.position.x,
										down.position.y - candidate->deferredUpSnapshot.position.y);
								}
							}
							continue;
						}
						const double gapSeconds = QpcDeltaSeconds(
							down.qpc, candidate->deferredUpSnapshot.qpc, qpcFrequency);
						const InterruptedStrokeReconnectMotion motion =
							ResolveInterruptedStrokeReconnectMotion(
								candidate->reconnectPredictedResults,
								candidate->stroke.realPoints,
								candidate->reconnectDirection,
								candidate->filteredInputSpeed,
								candidate->lastModelInputTime,
								gapSeconds,
								reconnectDpiScale);
						const InterruptedStrokeReconnectResult result =
							EvaluateInterruptedStrokeReconnect({
								.previousPosition = candidate->deferredUpSnapshot.position,
								.previousUpQpc = candidate->deferredUpSnapshot.qpc,
								.newPosition = down.position,
								.newDownQpc = down.qpc,
								.qpcFrequency = qpcFrequency,
								.dpiScale = reconnectDpiScale,
								.motion = motion
							});
						if (kInterruptedStrokeReconnectManualTestModeEnabled || observer_.penDiagnostics)
						{
							if (!diagnosticRuntime || candidate->deferredUpSnapshot.qpc >
								diagnosticRuntime->deferredUpSnapshot.qpc)
							{
								diagnosticRuntime = candidate;
								diagnosticResult = result;
							}
						}
						if (IsBetterInterruptedStrokeReconnectMatch(result,
							candidate->deferredUpSnapshot.qpc, reconnectResult,
							reconnectRuntime ? reconnectRuntime->deferredUpSnapshot.qpc : 0))
						{
							reconnectRuntime = candidate;
							reconnectResult = result;
						}
					}
				}
				if (kInterruptedStrokeReconnectManualTestModeEnabled || observer_.penDiagnostics)
				{
					if (!reconnectRuntime && diagnosticRuntime)
					{
						std::cout << "[StrokeReconnect] rejected device=" <<
							InputDeviceTypeName(deviceType) << " tool=" << DrawingToolName(tool) <<
							" new_tcid=" << handle.record->TabletContextId() <<
							" new_cid=" << handle.record->ContactId() <<
							" candidate_tcid=" << diagnosticRuntime->handle.record->TabletContextId() <<
							" candidate_cid=" << diagnosticRuntime->handle.record->ContactId() <<
							" candidate_generation=" << diagnosticRuntime->handle.generation <<
							" reason=" << ReconnectRejectReasonName(diagnosticResult.rejectReason) <<
							" motion=" << ReconnectMotionSourceName(diagnosticResult.motionSource) <<
							" gap_ms=" << diagnosticResult.gapMilliseconds <<
							" distance_px=" << diagnosticResult.distance <<
							" predicted_displacement_px=" << diagnosticResult.expectedDistance <<
							" endpoint_error_px=" << diagnosticResult.endpointError <<
							" endpoint_limit_px=" << diagnosticResult.maximumEndpointError <<
							" forecast_ms=" << diagnosticResult.forecastDurationMilliseconds <<
							" horizon_ms=" << diagnosticResult.predictionHorizonMilliseconds <<
							" beyond_horizon_ms=" <<
							diagnosticResult.beyondPredictionHorizonMilliseconds <<
							" reference_speed=" << diagnosticResult.referenceSpeed <<
							" recent_input_speed=" << diagnosticResult.recentInputSpeed <<
							" terminal_speed=" << diagnosticResult.terminalSpeed <<
							" filtered_input_speed=" << diagnosticRuntime->filteredInputSpeed <<
							" longitudinal_px=" << diagnosticResult.longitudinalDistance <<
							" longitudinal_error_px=" << diagnosticResult.longitudinalError <<
							" longitudinal_limit_px=" <<
							diagnosticResult.maximumLongitudinalError <<
							" lateral_error_px=" << diagnosticResult.lateralError <<
							" lateral_limit_px=" << diagnosticResult.maximumLateralError <<
							" range_px=[" << diagnosticResult.minimumDistance << "," <<
							diagnosticResult.maximumDistance << "] forecast_angle_deg=" <<
							diagnosticResult.angleDegrees << " terminal_angle_deg=" <<
							diagnosticResult.terminalVelocityAngleDegrees <<
							" selected_angle_deg=" <<
							diagnosticResult.selectedDirectionAngleDegrees <<
							" direction_reliable=" <<
							(diagnosticResult.directionReliable ? "true" : "false") <<
							" prediction_extrapolated=" <<
							(diagnosticResult.predictionExtrapolated ? "true" : "false") <<
							" terminal_direction_corridor_selected=" <<
							(diagnosticResult.selectedTerminalDirectionCorridor ? "true" : "false") <<
							" in_horizon_terminal_corridor_selected=" <<
							(diagnosticResult.selectedInHorizonTerminalDirectionCorridor ?
								"true" : "false") <<
							" speed_ratio=" << diagnosticResult.speedRatio <<
							" match_score=" << diagnosticResult.matchScore << std::endl;
					}
				}

				if (reconnectRuntime)
				{
					const float reconnectPreviousRawSpeed = reconnectRuntime->filteredInputSpeed;
					const uint32_t reconnectPreviousTabletContextId =
						reconnectRuntime->handle.record->TabletContextId();
					const uint32_t reconnectPreviousContactId =
						reconnectRuntime->handle.record->ContactId();
					const uint64_t reconnectPreviousGeneration = reconnectRuntime->handle.generation;
					const SpeedEraserOcController reconnectOcBefore =
						reconnectRuntime->speedEraserOc;
					ContactSnapshot modelDown = down;
					modelDown.pressure = downPressure;
					float lastPressure = reconnectRuntime->lastPressure;
					float lastTilt = reconnectRuntime->lastTilt;
					float lastOrientation = reconnectRuntime->lastOrientation;
					const float pressure = KeepLastValidStylusValue(
						modelDown.pressure, 1.0f, lastPressure);
					const float tilt = KeepLastValidStylusValue(modelDown.tilt, kHalfPi, lastTilt);
					const float orientation = KeepLastValidOrientation(
						modelDown.orientation, lastOrientation);
					double inputTime = QpcDeltaSeconds(
						down.qpc, reconnectRuntime->qpcOrigin, qpcFrequency);
					if (reconnectRuntime->stroke.useDisplayTime)
					{
						reconnectRuntime->stroke.lastMovementInputTime = inputTime;
						reconnectRuntime->stroke.logicalInputTime = std::max(
							reconnectRuntime->stroke.logicalInputTime, inputTime);
						inputTime = ResolvePenModelInputTime(reconnectRuntime->stroke, inputTime,
							reconnectRuntime->lastModelInputTime, 1.0 / configuration_.timingProfile.target_fps);
					}
					else inputTime = std::max(inputTime, reconnectRuntime->lastModelInputTime + 0.000001);
					if (reconnectRuntime->stroke.widthMode == StrokeWidthMode::SpeedEraser)
					{
						// 恢复历史并重新锚定 raw Down；连接位移和空缺时间不参与测速。
						reconnectRuntime->speedEraserOc.ResumeFromReconnect(
							down.position.x, down.position.y,
							AbsoluteQpcSeconds(down.qpc, qpcFrequency));
					}
					const Input reconnectInput{
						.event_type = Input::EventType::kMove,
						.position = Vec2(down.position.x, down.position.y),
						.time = Time(inputTime),
						.pressure = pressure,
						.tilt = tilt,
						.orientation = orientation
					};
					const size_t reconnectManualTestFirstPointIndex =
						reconnectRuntime->stroke.realPoints.empty()
						? 0 : reconnectRuntime->stroke.realPoints.size() - 1;
					reconnectRuntime->modelInputThisFrame = true;
					const bool endpointRecovery =
						(reconnectRuntime->tool == DrawingTool::Pen ||
							reconnectRuntime->tool == DrawingTool::HardPen) &&
						reconnectRuntime->stroke.endpointAdmission.active;
					if (endpointRecovery) reconnectRuntime->stroke.modelScratch.clear();
					auto& reconnectModelOutput = endpointRecovery
						? reconnectRuntime->stroke.modelScratch
						: reconnectRuntime->stroke.modeledResults;
					if (observer_.penDiagnostics && reconnectRuntime->stroke.useDisplayTime)
						++reconnectRuntime->diagnosticModelUpdates;
					if (absl::Status status = reconnectRuntime->stroke.modeler.Update(
						reconnectInput, reconnectModelOutput); status.ok())
					{
						reconnectRuntime->stationaryModelAdvanceBlocked = false;
						const double gapSeconds = reconnectResult.gapMilliseconds / 1000.0;
						const float alpha = std::clamp(static_cast<float>(
							1.0 - std::exp(-gapSeconds / kInputSpeedSmoothingSeconds)), 0.02f, 0.35f);
						reconnectRuntime->filteredInputSpeed +=
							(reconnectResult.bridgeSpeed - reconnectRuntime->filteredInputSpeed) * alpha;
						if (endpointRecovery)
						{
							AppendRecoveryModeledPoints(reconnectRuntime->stroke,
								reconnectRuntime->stroke.modelScratch,
								{ down.position.x, down.position.y }, reconnectRuntime->filteredInputSpeed);
						}
						else
						{
							AppendRuntimeModeledPoints(*reconnectRuntime,
								reconnectRuntime->filteredInputSpeed, inputTime);
						}
						if constexpr (kInterruptedStrokeReconnectManualTestModeEnabled)
						{
							const size_t lastPointIndex = reconnectRuntime->stroke.realPoints.size();
							if (lastPointIndex > reconnectManualTestFirstPointIndex + 1)
							{
								// 记录旧末点到续接 Down 的模型输出，仅用于人工观察桥接范围。
								reconnectRuntime->reconnectManualTestRanges.push_back({
									reconnectManualTestFirstPointIndex, lastPointIndex });
							}
						}
						input_.Recycle(reconnectRuntime->handle); // 新 contact 接管前释放已经 ConsumerOwned 的旧 slot。
						reconnectRuntime->handle = handle;
						reconnectRuntime->lastSpeedSnapshot = down;
						reconnectRuntime->lastInputSnapshot = down;
						reconnectRuntime->lastModelSnapshot = modelDown;
						reconnectRuntime->lastConsumedSequence = down.sequence;
						reconnectRuntime->lastModelInputTime = inputTime;
						reconnectRuntime->lastPressure = lastPressure;
						reconnectRuntime->lastTilt = lastTilt;
						reconnectRuntime->lastOrientation = lastOrientation;
						reconnectRuntime->awaitingReconnect = false;
						ClearPenTerminalState(reconnectRuntime->stroke);
						if(reconnectRuntime->metricDeviceType!=InputDeviceType::Touch &&
							reconnectRuntime->stroke.widthMode==StrokeWidthMode::SpeedEraser)
						{
							reconnectRuntime->eraserSessionToken=sessionForEntry(reconnectRuntime->resolvedEraser.entry)->
								ClaimContact(AbsoluteQpcSeconds(down.qpc,qpcFrequency));
							reconnectRuntime->mouseSpeedEraserFinished=false;
						}
						reconnectRuntime->reconnectVisualRefresh = true;
						reconnectRuntime->deferredUpSnapshot = {};
						reconnectRuntime->reconnectDeadlineQpc = 0;
						reconnectRuntime->reconnectPredictedResults.clear();
						reconnectRuntime->metricEligibleQpc = down.qpc;
						reconnectRuntime->metricVisible = false;
						reconnectRuntime->movedThisFrame = true;
						reconnectRuntime->stroke.idleFrozen = false;
						reconnectRuntime->stroke.visualStableFrameCount = 0;
						if (!reconnectRuntime->stroke.useDisplayTime)
							reconnectRuntime->stroke.lastMovementInputTime = inputTime;
						if (haptics_ && reconnectRuntime->hapticEligible)
						{
							if (reconnectRuntime->invertedCursor)
								haptics_->StopFeedback(); // 倒转笔尾需在真正接触后重新提交波形。
							haptics_->TickContinuous(HapticFeedbackForRuntime(*reconnectRuntime));
						}
						std::cout << "[StrokeReconnect] linked device=" << InputDeviceTypeName(deviceType) <<
							" tool=" << DrawingToolName(tool) <<
							" new_tcid=" << handle.record->TabletContextId() <<
							" new_cid=" << handle.record->ContactId() <<
							" candidate_tcid=" << reconnectPreviousTabletContextId <<
							" candidate_cid=" << reconnectPreviousContactId <<
							" candidate_generation=" << reconnectPreviousGeneration <<
							" gap_ms=" << reconnectResult.gapMilliseconds <<
							" motion=" << ReconnectMotionSourceName(reconnectResult.motionSource) <<
							" distance_px=" << reconnectResult.distance <<
							" predicted_displacement_px=" << reconnectResult.expectedDistance <<
							" endpoint_error_px=" << reconnectResult.endpointError <<
							" endpoint_limit_px=" << reconnectResult.maximumEndpointError <<
							" forecast_ms=" << reconnectResult.forecastDurationMilliseconds <<
							" horizon_ms=" << reconnectResult.predictionHorizonMilliseconds <<
							" beyond_horizon_ms=" <<
							reconnectResult.beyondPredictionHorizonMilliseconds <<
							" reference_speed=" << reconnectResult.referenceSpeed <<
							" recent_input_speed=" << reconnectResult.recentInputSpeed <<
							" terminal_speed=" << reconnectResult.terminalSpeed <<
							" filtered_input_speed=" << reconnectPreviousRawSpeed <<
							" longitudinal_px=" << reconnectResult.longitudinalDistance <<
							" longitudinal_error_px=" << reconnectResult.longitudinalError <<
							" longitudinal_limit_px=" <<
							reconnectResult.maximumLongitudinalError <<
							" lateral_error_px=" << reconnectResult.lateralError <<
							" lateral_limit_px=" << reconnectResult.maximumLateralError <<
							" forecast_angle_deg=" << reconnectResult.angleDegrees <<
							" terminal_angle_deg=" <<
							reconnectResult.terminalVelocityAngleDegrees <<
							" selected_angle_deg=" <<
							reconnectResult.selectedDirectionAngleDegrees <<
							" direction_reliable=" <<
							(reconnectResult.directionReliable ? "true" : "false") <<
							" prediction_extrapolated=" <<
							(reconnectResult.predictionExtrapolated ? "true" : "false") <<
							" terminal_direction_corridor_selected=" <<
							(reconnectResult.selectedTerminalDirectionCorridor ? "true" : "false") <<
							" in_horizon_terminal_corridor_selected=" <<
							(reconnectResult.selectedInHorizonTerminalDirectionCorridor ?
								"true" : "false") <<
							" speed_ratio=" << reconnectResult.speedRatio <<
							" match_score=" << reconnectResult.matchScore << std::endl;
						return true;
					}
					else
					{
						if (reconnectRuntime->stroke.widthMode == StrokeWidthMode::SpeedEraser)
							reconnectRuntime->speedEraserOc = reconnectOcBefore;
						std::cout << "Failed to continue interrupted stroke: " << status.message() << std::endl;
					}
				}

				RuntimeStroke* runtime = acquireStroke();
				if (!runtime)
				{
					ContactSnapshot cancelled = down;
					cancelled.phase = ContactPhase::Cancelled;
					input_.PublishCancelled(handle.record->TabletContextId(),
						handle.record->ContactId(), cancelled);
					input_.Recycle(handle);
					return false;
				}
				runtime->handle = handle;
				runtime->touchGestureKey = deviceType == InputDeviceType::Touch
					? CanvasTouchKey(handle) : 0;
				if (document_)
				{
					const InkPage* page = document_->PageAt(currentPageIndex_);
					const InkCanvas* canvas = page
						? page->FindCanvas(kDefaultDeviceKey) : nullptr;
					runtime->viewport = canvas ? canvas->Viewport() : InkViewport{};
				}
				else runtime->viewport = {};
				runtime->speedEraserDisplayScale = batchSpeedEraserDisplayScale;
				runtime->speedEraserDeviceMode = batchSpeedEraserDeviceMode;
				runtime->touchContactAreaAssistance=batchTouchArea;
				runtime->eraserInputs=batchEraserInputs;runtime->eraserPolicy=batchEraserPolicy;
				runtime->resolvedEraser=resolvedEraser;runtime->firstEraserCursorFrame=true;
				runtime->selectedTool = batchTool; // 倒转覆盖不能污染同批后续 contact 的原始选择。
				runtime->tool = tool;
				// 产品样式与工具一样在 Down 时锁存，活动笔划和断触续接不读取后续修改。
				runtime->visualStyle = window_.ProductVisualStyleSnapshot();
				runtime->eraserWidthMode = batchEraserWidthMode;
				runtime->eraserWidthModeRevision = batchEraserWidthModeRevision;
				runtime->suppressPressure = suppressPressure;
				runtime->ended = false;
				runtime->cancelled = false;
				runtime->metricVisible = false;
				runtime->movedThisFrame = false;
				runtime->laserParticleMovedThisFrame = false;
				runtime->visibleDirty = {};
				runtime->metricDeviceType = deviceType;
				runtime->invertedCursor = down.isInvertedCursor;
				runtime->hapticEligible = deviceType == InputDeviceType::Pen &&
					tool != DrawingTool::Laser;
				runtime->awaitingReconnect = false;
				runtime->reconnectVisualRefresh = false;
				runtime->deferredUpSnapshot = {};
				runtime->reconnectDirection = {};
				runtime->reconnectDeadlineQpc = 0;
				runtime->reconnectPredictedResults.clear();
				runtime->reconnectManualTestRanges.clear();
				runtime->shape.Reset();
				runtime->laserParticleSeed = LaserSeedForHandle(handle);
				runtime->laserLayerId = 0;
				ResetLaserParticleEmitterState(*runtime);
				runtime->metricEligibleQpc = down.qpc;
				float baseDiameter = CanvasDiameterForTool(
					runtime->tool, runtime->visualStyle, configuration_.dpiScale);
				if (widthMode == StrokeWidthMode::SpeedEraser)
				{
					initializeSpeedEraserController(*runtime, down);
					baseDiameter = runtime->speedEraserOc.Diameter();
				}
				if (runtime->tool == DrawingTool::Eraser && widthMode != StrokeWidthMode::SpeedEraser)
					baseDiameter = SpeedEraser::FixedDiameterPx(runtime->resolvedEraser.config.sizes.fixedDiameterDip, runtime->speedEraserDisplayScale);
				runtime->eraserSize.Reset(baseDiameter, AbsoluteQpcSeconds(down.qpc, qpcFrequency));
				runtime->eraserDiagnostics = {};
				runtime->eraserDiagnostics.active = observer_.eraserDiagnostics && window_.EraserDiagnosticsEnabled();
				runtime->eraserDiagnostics.entry=entry;runtime->eraserDiagnostics.eraserKind=resolvedEraser.kind;
				runtime->eraserDiagnostics.formalPenResponse=resolvedEraser.config.formalPenResponse;
				runtime->eraserDiagnostics.developmentResponseOverride=resolvedEraser.config.developmentResponseOverride;
				runtime->eraserDiagnostics.downSeconds=AbsoluteQpcSeconds(down.qpc,qpcFrequency);
				runtime->eraserDiagnostics.downDiameterPx=baseDiameter;
				if(deviceType!=InputDeviceType::Touch && widthMode==StrokeWidthMode::SpeedEraser)
				{
					const auto* session=sessionForEntry(entry);
					runtime->eraserDiagnostics.sessionInherited=session->LastHandoffInherited();
					runtime->eraserDiagnostics.sessionReason=session->LastHandoffReason();
					runtime->eraserDiagnostics.previousShownDiameterPx=session->LastHandoffPreviousDiameter();
					runtime->eraserDiagnostics.hoverSeconds=session->LastHandoffHoverSeconds();
				}
				runtime->speedEraserModelTime = 0.0;
				runtime->speedEraserModelDiameter = baseDiameter;
				const bool highlighter = runtime->tool == DrawingTool::Highlighter;
				runtime->stroke.Reset(baseDiameter, configuration_.expectedSpeed,
					widthMode, highlighter);
				runtime->stroke.useDisplayTime = (runtime->tool == DrawingTool::Pen || runtime->tool == DrawingTool::HardPen);
				runtime->stroke.captureTerminalTrace = observer_.penDiagnostics != nullptr;
				const auto& modelParams = runtime->tool == DrawingTool::Eraser
					? eraserModelParams : strokeModelParams;
				if (absl::Status status = runtime->stroke.modeler.Reset(modelParams); !status.ok())
				{
					std::cout << "Error: " << status.message() << std::endl;
					ContactSnapshot cancelled = down;
					cancelled.phase = ContactPhase::Cancelled;
					input_.PublishCancelled(handle.record->TabletContextId(),
						handle.record->ContactId(), cancelled);
					input_.Recycle(handle);
					runtime->cancelled = true;
					handBackSpeedEraserController(*runtime);
					runtime->handle = {};
					runtime->inUse = false;
					return false;
				}

				ContactSnapshot modelDown = down;
				modelDown.pressure = downPressure;
				runtime->lastSpeedSnapshot = down;
				runtime->lastInputSnapshot = down;
				runtime->lastModelSnapshot = modelDown;
				runtime->lastConsumedSequence = down.sequence;
				runtime->qpcOrigin = down.qpc;
				runtime->lastModelInputTime = 0.0;
				runtime->diagnosticStrokeId = observer_.penDiagnostics ? ++nextDiagnosticStrokeId : 0;
				runtime->diagnosticModelUpdates = observer_.penDiagnostics ? 1 : 0;
				runtime->filteredInputSpeed = 0.0f;
				runtime->hasFilteredInputSpeed = false;
				runtime->lastPressure = downPressure;
				runtime->lastTilt = down.tilt;
				runtime->lastOrientation = down.orientation;
				ActiveStroke& stroke = runtime->stroke;
				const float initialDiameter = widthMode == StrokeWidthMode::HardwarePressure
					? HardwarePressureDiameter(baseDiameter, downPressure)
					: widthMode == StrokeWidthMode::LaserPressure
						? LaserPressureDiameter(baseDiameter, downPressure) : baseDiameter;
				stroke.inputStartPoint = {
					down.position.x, down.position.y, initialDiameter * 0.5f, 0.0f };
				stroke.hasInputStartPoint = true;
				if (IsShapeDrawingTool(runtime->tool))
				{
					RuntimeShapeState& shape = runtime->shape;
					shape.active = true;
					shape.kind = ShapeKindForTool(runtime->tool);
					shape.rawEndpoint = { down.position.x, down.position.y };
					shape.primitive.start = stroke.inputStartPoint;
					shape.primitive.start.time = 0.0f;
					shape.primitive.end = {
						down.position.x, down.position.y, 0.0f, 0.0f };
					shape.visualChanged = true;
				}
				const Input downInput{
					.event_type = Input::EventType::kDown,
					.position = Vec2(down.position.x, down.position.y),
					.time = Time(0.0),
					.pressure = downPressure,
					.tilt = down.tilt,
					.orientation = down.orientation
				};
				runtime->modelInputThisFrame = true;
				runtime->stationaryModelAdvanceBlocked = false;
				if (absl::Status status = stroke.modeler.Update(downInput, stroke.modeledResults); !status.ok())
				{
					std::cout << "Error: " << status.message() << std::endl;
					ContactSnapshot cancelled = down;
					cancelled.phase = ContactPhase::Cancelled;
					input_.PublishCancelled(handle.record->TabletContextId(),
						handle.record->ContactId(), cancelled);
					input_.Recycle(handle);
					runtime->cancelled = true;
					handBackSpeedEraserController(*runtime);
					runtime->handle = {};
					runtime->inUse = false;
					return false;
				}
				if (runtime->shape.active)
					ExtractShapeModeledEndpoint(*runtime);
				else
					AppendRuntimeModeledPoints(*runtime, -1.0f, 0.0);
				if (runtime->tool == DrawingTool::Laser)
				{
					const WindowSize laserSize = window_.Size();
					if (laserLifecycle.phase == LaserTrailPhase::Inactive)
					{
						renderer_.ClearAllLaserCoverage();
						laserStableBounds = {};
						laserLiveBounds = {};
						laserStrokeLayers.clear();
						laserCoverageMode = LaserCoverageMode::Inactive;
					}
					else if (laserLifecycle.phase != LaserTrailPhase::Active &&
						!laserStrokeLayers.empty())
					{
						// 同帧发生"最后 Up → 新 Down"时，也先把上一批按原顺序烘干。
						UnionRectInPlace(pendingLaserBakeDirty, laserLiveBounds);
						BakeLaserStrokeLayers(laserStrokeLayers, renderer_,
							configuration_.dpiScale, laserSize.width, laserSize.height,
							laserStableBounds, pendingLaserBakeDirty,
							laserCoverageMode);
						laserLiveBounds = {};
						laserCoverageMode = LaserCoverageMode::Inactive;
					}
					else if (laserLifecycle.phase != LaserTrailPhase::Active)
					{
						// Hold/Fade 中的新 Down 保留已烘干颜色，并开启新的增量批次。
						laserCoverageMode = LaserCoverageMode::Inactive;
					}
					runtime->laserLayerId = nextLaserLayerId++;
					laserTrailVisualStyle = runtime->visualStyle;
					LaserStrokeLayer layer{
						.id = runtime->laserLayerId, .runtime = runtime,
						.visualStyle = runtime->visualStyle };
					laserStrokeLayers.push_back(std::move(layer));
					const LaserCoverageMode previousCoverageMode = laserCoverageMode;
					laserCoverageMode = SelectLaserCoverageMode(laserCoverageMode,
						laserStrokeLayers.size(),
						renderer_.LaserIncrementalCoverageAvailable());
					if (laserCoverageMode == LaserCoverageMode::FullRedraw &&
						previousCoverageMode != LaserCoverageMode::FullRedraw)
					{
						renderer_.ClearLaserIncrementalCoverage();
					}
					BeginLaserContact(laserLifecycle);
					laserOpacity = 1.0f;
					// Down 只重置时间累计；得到首条非退化 L0 切线前不发射。
				}
				active.push_back(runtime);
				if (haptics_ && runtime->hapticEligible)
				{
					if (runtime->invertedCursor)
						haptics_->StopFeedback(); // 倒转笔尾不沿用悬停预热的同波形状态。
					haptics_->TickContinuous(HapticFeedbackForRuntime(*runtime));
				}
				return true;
			};

			auto processCommand = [&](ContactRecord* record)
			{
				if (!record)
				{
					input_.AcknowledgeControlWake(); // 先清 pending，随后复查窗口的全部原子请求。
					if (observer_.controlWake)
						observer_.controlWake(observer_.context);
					return;
				}
				const ContactHandle handle{ record, record->Generation() };
				if (activeWorkspace == Bridge::Workspace::Whiteboard &&
					window_.SelectionMode())
				{
					// 白板“拖动”暂不启用平移，也不能让主 Drawpad 继续落笔。
					input_.Recycle(handle);
					return;
				}
				if (Bridge::PresentationInputSuppressed(
					activeWorkspace, activePresentationLoadPending))
				{
					// 磁盘恢复完成前拒绝新输入，避免旧文件覆盖刚落下的墨迹。
					input_.Recycle(handle);
					return;
				}
				initializeStroke(handle); // 出队后立即固定本地 generation。
			};
			auto processCommandAndReconcile = [&](ContactRecord* record)
			{
				processCommand(record);
				// Down 后部分路径会直接 continue，必须在命令边界立即发布 0→1。
				reconcileDrawingActivity();
			};

		auto appendTerminalFallback = [&](RuntimeStroke& runtime,
			const ContactSnapshot& snapshot, double inputTime)
			{

				if(runtime.stroke.widthMode==StrokeWidthMode::SpeedEraser)
				{
					const auto interval=runtime.eraserSize.MakeInterval(runtime.speedEraserModelTime,inputTime,
						runtime.speedEraserModelDiameter,runtime.eraserSize.effectiveDiameterPx,
						runtime.eraserTimeOrigin+inputTime,runtime.speedEraserOc.Configuration());
					if(AppendEraserSizeAnchor(runtime.stroke,interval))runtime.eraserSize.Accepted(interval);
				}
				const float radius = runtime.stroke.widthMode == StrokeWidthMode::SpeedEraser
					? runtime.speedEraserOc.Diameter() * 0.5f
					: runtime.stroke.realPoints.empty()
						? runtime.stroke.inputStartPoint.r : runtime.stroke.realPoints.back().r;
				// 失败回退也沿用显示时间，不能把压缩后的模型时间写回可见尾段。
				const double pointTime = runtime.stroke.useDisplayTime
					? std::max(runtime.stroke.lastMovementInputTime,
						runtime.stroke.realPoints.empty() ? 0.0 :
							static_cast<double>(runtime.stroke.realPoints.back().time))
					: inputTime;
				const InkPoint finalPoint{ snapshot.position.x, snapshot.position.y,
					radius, static_cast<float>(pointTime) };
				if (runtime.stroke.useDisplayTime)
					AppendTerminalFallbackPoint(runtime.stroke, finalPoint);
				else if (runtime.stroke.realPoints.empty())
					runtime.stroke.realPoints.push_back(finalPoint);
				else
				{
					const float deltaX = finalPoint.x - runtime.stroke.realPoints.back().x;
					const float deltaY = finalPoint.y - runtime.stroke.realPoints.back().y;
					if (deltaX * deltaX + deltaY * deltaY > 0.0001f ||
						(runtime.stroke.widthMode==StrokeWidthMode::SpeedEraser &&
						std::abs(finalPoint.r-runtime.stroke.realPoints.back().r)>0.001f))
						runtime.stroke.realPoints.push_back(finalPoint);
					else
						runtime.stroke.realPoints.back() = finalPoint;
				}
				// 模型异常也保留 RTS 的最终位置，不能因随后回收 contact 而吞掉 Up 点。
			};

		auto completeModelUp = [&](RuntimeStroke& runtime,
			const ContactSnapshot& snapshot, bool cancelled,
			int64_t controllerResumeQpc = 0)
			{
				if (runtime.stroke.widthMode == StrokeWidthMode::SpeedEraser &&
					runtime.speedEraserOc.IsPaused())
				{
					const int64_t resumeQpc = controllerResumeQpc > 0
						? controllerResumeQpc : snapshot.qpc;
					runtime.speedEraserOc.ResumeFromReconnect(
						snapshot.position.x, snapshot.position.y,
						AbsoluteQpcSeconds(resumeQpc, qpcFrequency));
				}
				ContactSnapshot modelSnapshot = snapshot;
				if (runtime.suppressPressure) modelSnapshot.pressure = -1.0f;
				double inputTime = QpcDeltaSeconds(snapshot.qpc, runtime.qpcOrigin, qpcFrequency);
				if (runtime.stroke.useDisplayTime)
				{
					LockPenTerminalState(runtime.stroke, inputTime,
						{ runtime.lastSpeedSnapshot.position.x, runtime.lastSpeedSnapshot.position.y },
						{ snapshot.position.x, snapshot.position.y });
					runtime.stroke.logicalInputTime = std::max(runtime.stroke.logicalInputTime, inputTime);
					inputTime = ResolvePenModelInputTime(runtime.stroke, inputTime,
						runtime.lastModelInputTime, 1.0 / configuration_.timingProfile.target_fps);
				}
				else inputTime = std::max(inputTime, runtime.lastModelInputTime + 0.000001);
				runtime.lastModelInputTime = inputTime;
				const float pressure = KeepLastValidStylusValue(
					modelSnapshot.pressure, 1.0f, runtime.lastPressure);
				const float tilt = KeepLastValidStylusValue(
					modelSnapshot.tilt, kHalfPi, runtime.lastTilt);
				const float orientation = KeepLastValidOrientation(
					modelSnapshot.orientation, runtime.lastOrientation);
				const Input upInput{
					.event_type = Input::EventType::kUp,
					.position = Vec2(snapshot.position.x, snapshot.position.y),
					.time = Time(inputTime),
					.pressure = pressure,
					.tilt = tilt,
					.orientation = orientation
				};
				runtime.modelInputThisFrame = true;
				const bool sanitizeEndpoint =
					(runtime.tool == DrawingTool::Pen ||
						runtime.tool == DrawingTool::HardPen) && !runtime.shape.active;
				if (sanitizeEndpoint) runtime.stroke.modelScratch.clear();
				auto& modelOutput = sanitizeEndpoint
					? runtime.stroke.modelScratch : runtime.stroke.modeledResults;
				if (absl::Status status = updateContactModel(
					runtime, upInput, inputTime, modelOutput); status.ok())
				{
					if (runtime.shape.active) ExtractShapeModeledEndpoint(runtime);
					else if (sanitizeEndpoint)
					{
						BeginEndpointAdmission(runtime.stroke,
							{ snapshot.position.x, snapshot.position.y });
						AppendEndpointBoundedModeledPoints(runtime.stroke,
							runtime.stroke.modelScratch, -1.0f,
							runtime.stroke.lastMovementInputTime, true);
					}
					else AppendRuntimeModeledPoints(runtime, -1.0f, inputTime);
				}
				else
				{
					std::cout << "Error: " << status.message() << std::endl;
					if (!runtime.shape.active)
						appendTerminalFallback(runtime, snapshot, inputTime);
				}
				if (runtime.shape.active)
				{
					runtime.shape.rawEndpoint = {
						snapshot.position.x, snapshot.position.y };
					SetShapeVisualEndpoint(runtime.shape, runtime.shape.rawEndpoint);
					runtime.stroke.predictedResults.clear();
				}
				if (sanitizeEndpoint)
					CapturePenTerminalTrace(runtime.stroke);
				runtime.lastModelSnapshot = modelSnapshot;
				runtime.ended = true;
				runtime.cancelled = cancelled;
				runtime.awaitingReconnect = false;
				runtime.reconnectVisualRefresh = false;
				runtime.reconnectDeadlineQpc = 0;
			};

		auto consumeLatestSnapshot = [&](RuntimeStroke& runtime) -> bool
			{
				runtime.laserParticleMovedThisFrame = false;
				if (runtime.ended || runtime.awaitingReconnect) return false;
				ContactSnapshot snapshot;
				if (!input_.TryReadSnapshot(runtime.handle, snapshot) ||
					snapshot.sequence == runtime.lastConsumedSequence) return false;
				runtime.lastConsumedSequence = snapshot.sequence;
				runtime.lastInputSnapshot = snapshot;
				if (snapshot.phase == ContactPhase::Down) return false;
				ContactSnapshot modelSnapshot = snapshot;
				if (runtime.suppressPressure) modelSnapshot.pressure = -1.0f;
				bool shapeRawChanged = false;
				if (runtime.shape.active && std::isfinite(snapshot.position.x) &&
					std::isfinite(snapshot.position.y))
				{
					shapeRawChanged = runtime.shape.rawEndpoint.x != snapshot.position.x ||
						runtime.shape.rawEndpoint.y != snapshot.position.y;
					runtime.shape.rawEndpoint = {
						snapshot.position.x, snapshot.position.y };
				}

				const float deltaX = snapshot.position.x - runtime.lastSpeedSnapshot.position.x;
				const float deltaY = snapshot.position.y - runtime.lastSpeedSnapshot.position.y;
				const float distanceSquared = deltaX * deltaX + deltaY * deltaY;
				const bool terminal = snapshot.phase == ContactPhase::Up ||
					snapshot.phase == ContactPhase::Cancelled;
				if (terminal && runtime.metricDeviceType == InputDeviceType::Touch &&
					runtime.touchGestureKey != 0)
					touchGesture.OnTouchUp(runtime.touchGestureKey);
				const bool deferUp = snapshot.phase == ContactPhase::Up && !IsMouseSpeedEraser(runtime) &&
					GetInterruptedStrokeReconnectEnabled() &&
					IsInterruptedStrokeReconnectIdentitySupported(ReconnectIdentity(runtime)) &&
					runtime.tool != DrawingTool::Laser && !runtime.shape.active;
				const bool endpointTool =
					(runtime.tool == DrawingTool::Pen ||
						runtime.tool == DrawingTool::HardPen) && !runtime.shape.active;
				const bool positionMoved = distanceSquared > kRawMoveThresholdPx * kRawMoveThresholdPx;
				runtime.laserParticleMovedThisFrame = positionMoved;
				const bool stylusStateChanged = HasStylusStateChange(modelSnapshot, runtime.lastModelSnapshot);
				if (runtime.stroke.widthMode == StrokeWidthMode::SpeedEraser)
				{
					// 每份 raw snapshot 先推进 OC；即使本次不进入 modeler，也要累计停笔时间。
					const auto area=ContactAreaFromSnapshot(snapshot);
					runtime.speedEraserOc.UpdatePosition(
						snapshot.position.x, snapshot.position.y,
						AbsoluteQpcSeconds(snapshot.qpc, qpcFrequency),&area,terminal);
					const double rawSeconds=AbsoluteQpcSeconds(snapshot.qpc,qpcFrequency);
					runtime.eraserSize.Update(runtime.speedEraserOc.Diameter(),rawSeconds,
						runtime.speedEraserOc.SecondsSinceMovement(rawSeconds)>=runtime.speedEraserOc.Configuration().idleStartSeconds);
				}
				if (!terminal && !positionMoved && !stylusStateChanged && !shapeRawChanged)
					return false; // Move 抖动已消费但不进入模型，也不改变下一次真实速度基准。

				const double deltaSeconds = QpcDeltaSeconds(
					snapshot.qpc, runtime.lastSpeedSnapshot.qpc, qpcFrequency);
				float inputSpeed = -1.0f;
				if (positionMoved && deltaSeconds > 0.0)
				{
					const float measuredSpeed = std::sqrt(distanceSquared) / static_cast<float>(deltaSeconds);
					if (!runtime.hasFilteredInputSpeed)
					{
						runtime.filteredInputSpeed = measuredSpeed;
						runtime.hasFilteredInputSpeed = true;
					}
					else
					{
						const float alpha = std::clamp(static_cast<float>(
							1.0 - std::exp(-deltaSeconds / kInputSpeedSmoothingSeconds)), 0.02f, 0.35f);
						runtime.filteredInputSpeed +=
							(measuredSpeed - runtime.filteredInputSpeed) * alpha;
					}
					inputSpeed = runtime.filteredInputSpeed; // 每个真实快照先滤速，再交给半径估算器。
				}

				double inputTime = QpcDeltaSeconds(snapshot.qpc, runtime.qpcOrigin, qpcFrequency);
				if (endpointTool)
				{
					if (terminal)
						LockPenTerminalState(runtime.stroke, inputTime,
							{ runtime.lastSpeedSnapshot.position.x, runtime.lastSpeedSnapshot.position.y },
							{ snapshot.position.x, snapshot.position.y });
					runtime.stroke.logicalInputTime = std::max(runtime.stroke.logicalInputTime, inputTime);
					if (positionMoved) runtime.stroke.lastMovementInputTime = inputTime;
					inputTime = ResolvePenModelInputTime(runtime.stroke, inputTime,
						runtime.lastModelInputTime, 1.0 / configuration_.timingProfile.target_fps);
				}
				else inputTime = std::max(inputTime, runtime.lastModelInputTime + 0.000001);
				runtime.lastModelInputTime = inputTime;
				const float pressure = KeepLastValidStylusValue(modelSnapshot.pressure, 1.0f,
					runtime.lastPressure);
				const float tilt = KeepLastValidStylusValue(modelSnapshot.tilt, kHalfPi,
					runtime.lastTilt);
				const float orientation = KeepLastValidOrientation(
					modelSnapshot.orientation, runtime.lastOrientation);
				const Input input{
					.event_type = terminal && !deferUp ? Input::EventType::kUp : Input::EventType::kMove,
					.position = Vec2(snapshot.position.x, snapshot.position.y),
					.time = Time(inputTime),
					.pressure = pressure,
					.tilt = tilt,
					.orientation = orientation
				};
				bool modelUpdateSucceeded = false;
				runtime.modelInputThisFrame = true;
				const bool boundedEndpointUpdate = endpointTool &&
					(terminal || runtime.stroke.endpointAdmission.active);
				if (boundedEndpointUpdate) runtime.stroke.modelScratch.clear();
				auto& modelOutput = boundedEndpointUpdate
					? runtime.stroke.modelScratch : runtime.stroke.modeledResults;
				if (absl::Status status = updateContactModel(
					runtime, input, inputTime, modelOutput); status.ok())
				{
					modelUpdateSucceeded = true;
					runtime.stationaryModelAdvanceBlocked = false;
					if (runtime.shape.active) ExtractShapeModeledEndpoint(runtime);
					else if (boundedEndpointUpdate)
					{
						if (!terminal && (positionMoved || runtime.stroke.endpointAdmission.recovering))
							AppendRecoveryModeledPoints(runtime.stroke, runtime.stroke.modelScratch,
								{ snapshot.position.x, snapshot.position.y }, inputSpeed);
						else
						{
							BeginEndpointAdmission(runtime.stroke,
								{ snapshot.position.x, snapshot.position.y });
							AppendEndpointBoundedModeledPoints(runtime.stroke,
								runtime.stroke.modelScratch, inputSpeed,
								runtime.stroke.lastMovementInputTime, terminal);
						}
					}
					else AppendRuntimeModeledPoints(runtime, inputSpeed, inputTime);
				}
				else
				{
					std::cout << "Error: " << status.message() << std::endl;
					if (runtime.shape.active)
						runtime.shape.rawFallbackRequired = true;
					if (terminal && !deferUp && !runtime.shape.active)
						appendTerminalFallback(runtime, snapshot, inputTime);
				}
				if (terminal && endpointTool && (modelUpdateSucceeded || !deferUp))
					CapturePenTerminalTrace(runtime.stroke);
				runtime.lastModelSnapshot = modelSnapshot;
				if (positionMoved) runtime.lastSpeedSnapshot = snapshot;
				if (positionMoved || stylusStateChanged)
				{
					runtime.stroke.idleFrozen = false;
					runtime.stroke.visualStableFrameCount = 0;
					if (!endpointTool) runtime.stroke.lastMovementInputTime = inputTime;
				}
				if (terminal && runtime.tool == DrawingTool::Laser)
				{
					const bool laserCancelled = snapshot.phase == ContactPhase::Cancelled;
					if (laserCancelled &&
						laserCoverageMode == LaserCoverageMode::Incremental)
					{
						// Cancel 必须脏化旧 coverage；切回完整路径后本帧会重建底图且跳过该层。
						laserCoverageMode = LaserCoverageMode::FullRedraw;
						renderer_.ClearLaserIncrementalCoverage();
					}
					EndLaserContact(laserLifecycle, snapshot.qpc);
					const WindowSize laserSize = window_.Size();
					// contact 结束后复制最终 CPU 几何，避免 runtime 回收破坏同批次层级。
					FinalizeLaserStrokeLayer(laserStrokeLayers, runtime,
						laserCancelled,
						configuration_.dpiScale, laserSize.width, laserSize.height);
				}
				if (terminal && runtime.shape.active)
				{
					// 最终文档端点必须严格使用原始 Up，不能让 prediction/modeler 覆盖。
					SetShapeVisualEndpoint(runtime.shape, runtime.shape.rawEndpoint);
					runtime.stroke.predictedResults.clear();
				}
				if (deferUp)
				{
					DirectX::XMFLOAT2 direction = {};
					if (modelUpdateSucceeded && runtime.hasFilteredInputSpeed &&
						TryGetInterruptedStrokeTailDirection(
							runtime.stroke.realPoints, reconnectDpiScale, direction))
					{
						runtime.reconnectPredictedResults.clear();
						if (runtime.tool != DrawingTool::Eraser &&
							kActivePredictionMode != InkPredictionMode::Disabled)
						{
							// 同帧新 Down 会在渲染前出队，因此候选创建时必须先冻结最新 prediction。
							if (absl::Status status = runtime.stroke.modeler.Predict(
								runtime.reconnectPredictedResults); !status.ok())
								runtime.reconnectPredictedResults.clear();
						}
						runtime.awaitingReconnect = true;
						if (runtime.stroke.widthMode == StrokeWidthMode::SpeedEraser)
						{
							// 候选窗口冻结 OC；成功续接只平移历史时间并重设位置基准。
							runtime.speedEraserOc.PauseForReconnect(
								AbsoluteQpcSeconds(snapshot.qpc, qpcFrequency));
						}
						const bool anotherPenContactActive = std::any_of(active.begin(), active.end(),
							[&](const RuntimeStroke* candidate)
							{
								return candidate && candidate != &runtime &&
									candidate->hapticEligible && !candidate->ended &&
									!candidate->awaitingReconnect;
							});
						if (haptics_ && runtime.hapticEligible && !anotherPenContactActive)
							haptics_->StopFeedback();
						runtime.reconnectVisualRefresh = true;
						runtime.deferredUpSnapshot = snapshot;
						runtime.reconnectDirection = direction;
						runtime.reconnectDeadlineQpc = snapshot.qpc + static_cast<int64_t>(
							kInterruptedStrokeReconnectWindowSeconds * static_cast<double>(qpcFrequency));
					}
					else
					{
						completeModelUp(runtime, snapshot, false);
					}
				}
				else if (terminal)
				{
					runtime.ended = true;
					runtime.cancelled = snapshot.phase == ContactPhase::Cancelled;
				}
				if (terminal) finishMouseSpeedEraser(runtime); // 接受 Up/Cancel 即退出，不等烘干或 Hover。
				return positionMoved || stylusStateChanged || shapeRawChanged || deferUp;
			};

		auto updateCanvasNavigation = [&](int64_t nowQpc)
		{
			DrawingCursorSample navigationPenSample;
			const bool penInRange = window_.ReadPenCursorSample(navigationPenSample) &&
				navigationPenSample.valid;
			const bool penInContact = penInRange && IsPenContactSampleFresh(
				navigationPenSample.inContact, navigationPenSample.qpc,
				suppressedPenTerminalQpc);
			if (ShouldBeginSuppressingPenContactDuringTouchPan(
				touchGesture.PanActive(), penInContact))
			{
				suppressPenUntilRelease = true;
				window_.SuppressPenContactForTouchPan();
			}
			else if (suppressPenUntilRelease && !penInContact &&
				!hasLiveSuppressedPenContact())
				suppressPenUntilRelease = false;
			if (penInContact && !suppressPenUntilRelease)
			{
				// Pen mailbox 不受 Touch-to-Mouse 提升影响，惯性阶段可在 contact 出队前刹停。
				interruptNavigationForPenOrMouse("pen-mailbox-contact");
			}
			const bool candidateBeforeUpdate = touchGesture.InertiaCandidateActive();
			const bool batchAllowedBeforeUpdate = touchGesture.BatchAllowsPan();
			const int64_t candidateFirstDownQpc = touchGesture.FirstDownQpc();
			touchGesture.Update(nowQpc, qpcFrequency);
			if ((candidateBeforeUpdate && !touchGesture.InertiaCandidateActive()) ||
				(batchAllowedBeforeUpdate && !touchGesture.BatchAllowsPan()))
			{
				LogCanvasPan("touch-window-expired now-qpc=%lld first-qpc=%lld elapsed-ms=%.3f candidate-before=%u brake=%u contacts=%zu",
					static_cast<long long>(nowQpc),
					static_cast<long long>(candidateFirstDownQpc),
					QpcDeltaSeconds(nowQpc, candidateFirstDownQpc, qpcFrequency) * 1000.0,
					candidateBeforeUpdate ? 1u : 0u,
					touchGesture.InertiaBrakeRequested() ? 1u : 0u,
					touchGesture.ContactCount());
			}
			bool topologyChanged = false;
			bool panPositionUpdated = false;
			bool panVelocityInputUpdated = false;
			bool panTerminalPositionUpdated = false;
			int64_t newestPanPositionQpc = 0;
			int64_t newestPanVelocityQpc = 0;
			int64_t panReleaseQpc = 0;
			bool panReleaseCancelled = false;
			for (CanvasGestureContactRuntime& contact : gestureContacts)
			{
				ContactSnapshot snapshot = contact.snapshot;
				const bool snapshotRead = input_.TryReadSnapshot(contact.handle, snapshot);
				if ((!snapshotRead && !contact.terminalPending) ||
					!ShouldConsumeCanvasPanContactSnapshot(snapshot.sequence,
						contact.lastConsumedSequence, contact.terminalPending)) continue;
				const ContactSnapshot previousSnapshot = contact.snapshot;
				contact.disposition = touchGesture.Disposition(contact.key);
				contact.lastConsumedSequence = snapshot.sequence;
				contact.snapshot = snapshot;
				contact.terminalPending = false;
				const CanvasVector point{ snapshot.position.x, snapshot.position.y };
				if (snapshot.phase == ContactPhase::Move)
				{
					if (contact.disposition == CanvasTouchDisposition::Pan)
					{
						contact.velocityPosition = snapshot.position;
						panPositionUpdated = true;
						panVelocityInputUpdated = true;
						newestPanPositionQpc = (std::max)(newestPanPositionQpc, snapshot.qpc);
						newestPanVelocityQpc = (std::max)(newestPanVelocityQpc, snapshot.qpc);
					}
				}
				else if (snapshot.phase == ContactPhase::Up ||
					snapshot.phase == ContactPhase::Cancelled)
				{
					if (contact.handle.record &&
						contact.handle.record->DeviceType() == InputDeviceType::Pen)
						suppressedPenTerminalQpc = (std::max)(
							suppressedPenTerminalQpc, snapshot.qpc);
					if (contact.disposition == CanvasTouchDisposition::Pan)
					{
						// Up 只提交最终位移；释放速度由此前 Move 样本窗口决定。
						if (snapshot.phase == ContactPhase::Up &&
							(snapshot.position.x != previousSnapshot.position.x ||
								snapshot.position.y != previousSnapshot.position.y))
						{
							panPositionUpdated = true;
							panTerminalPositionUpdated = true;
							newestPanPositionQpc = (std::max)(newestPanPositionQpc, snapshot.qpc);
						}
						panReleaseQpc = (std::max)(panReleaseQpc, snapshot.qpc);
						panReleaseCancelled = panReleaseCancelled ||
							snapshot.phase == ContactPhase::Cancelled;
						lastTouchPanEndQpc = (std::max)(
							lastTouchPanEndQpc, snapshot.qpc);
					}
				}
			}

			CanvasVector contentDelta = {};
			if (touchGesture.PanActive())
			{
				CanvasVector centroid = {};
				CanvasVector velocityCentroid = {};
				size_t count = 0;
				for (const CanvasGestureContactRuntime& contact : gestureContacts)
				{
					if (touchGesture.Disposition(contact.key) != CanvasTouchDisposition::Pan)
						continue;
					centroid.x += contact.snapshot.position.x;
					centroid.y += contact.snapshot.position.y;
					velocityCentroid.x += contact.velocityPosition.x;
					velocityCentroid.y += contact.velocityPosition.y;
					++count;
				}
				if (count > 0)
				{
					centroid.x /= static_cast<float>(count);
					centroid.y /= static_cast<float>(count);
					velocityCentroid.x /= static_cast<float>(count);
					velocityCentroid.y /= static_cast<float>(count);
					if (!panCentroidValid || topologyChanged || count != previousPanContactCount)
					{
						previousPanCentroid = centroid;
						panCentroidValid = true;
						previousPanContactCount = count;
					}
					else if (panPositionUpdated)
					{
						const CanvasVector centroidDelta{
							centroid.x - previousPanCentroid.x,
							centroid.y - previousPanCentroid.y };
						const bool updateVelocity = panVelocityInputUpdated;
						const int64_t inputQpc = updateVelocity
							? newestPanVelocityQpc : newestPanPositionQpc;
						const CanvasVector velocityDelta = updateVelocity
							? CanvasVector{ velocityCentroid.x - previousPanCentroid.x,
								velocityCentroid.y - previousPanCentroid.y }
							: CanvasVector{};
						const CanvasVector velocityBeforeUpdate = panMotion.velocity;
						const double updateDeltaSeconds = QpcDeltaSeconds(inputQpc,
							panMotion.lastUpdateQpc, qpcFrequency);
						contentDelta = UpdateCanvasPan(panMotion, centroidDelta,
							velocityDelta, inputQpc,
							qpcFrequency, updateVelocity);
						const float inputDistance = std::hypot(centroidDelta.x, centroidDelta.y);
						const float outputDistance = std::hypot(contentDelta.x, contentDelta.y);
						const float speedBeforeUpdate = std::hypot(
							velocityBeforeUpdate.x, velocityBeforeUpdate.y);
						const float speedAfterUpdate = CanvasPanSpeed(panMotion);
						const bool candidateValid = panMotion.releaseVelocityCandidateSource !=
							CanvasPanReleaseCandidateSource::None;
						const bool anomalousPanFrame =
							speedAfterUpdate - speedBeforeUpdate > 2000.0f ||
							(inputDistance < 2.0f && outputDistance > 24.0f) ||
							speedAfterUpdate > 6000.0f;
						if (anomalousPanFrame && inputQpc >= nextPanAnomalyDiagnosticQpc)
						{
							// 可疑帧直接打印候选与新手势速度，验证二者从未混入同一数值。
							LogCanvasPan("pan-anomaly qpc=%lld dt-ms=%.3f contacts=%zu centroid-delta=(%.3f,%.3f) content-delta=(%.3f,%.3f) direct=(%.1f,%.1f) candidate=(%.1f,%.1f) candidate-valid=%u has-new-move=%u velocity-before=(%.1f,%.1f) velocity-after=(%.1f,%.1f) samples=%zu last-sample-qpc=%lld",
								static_cast<long long>(inputQpc), updateDeltaSeconds * 1000.0,
								count, centroidDelta.x, centroidDelta.y,
								contentDelta.x, contentDelta.y, panMotion.directVelocity.x,
								panMotion.directVelocity.y,
								panMotion.releaseVelocityCandidate.x,
								panMotion.releaseVelocityCandidate.y,
								candidateValid ? 1u : 0u, panMotion.hasNewMove ? 1u : 0u,
								velocityBeforeUpdate.x, velocityBeforeUpdate.y,
								panMotion.velocity.x, panMotion.velocity.y,
								panMotion.velocitySampleCount,
								static_cast<long long>(panMotion.lastVelocitySampleQpc));
							nextPanAnomalyDiagnosticQpc = inputQpc +
								(std::max)(int64_t{ 1 }, qpcFrequency / 10);
						}
						if (newestPanPositionQpc >= nextPanMoveDiagnosticQpc)
						{
							const double velocityAge = lastPanInputQpc > 0
								? QpcDeltaSeconds(newestPanPositionQpc,
									lastPanInputQpc, qpcFrequency) : 0.0;
							LogCanvasPan("pan-move engine=application contacts=%zu qpc=%lld velocity-sample=%u terminal-position=%u centroid-delta=(%.3f,%.3f) content-delta=(%.3f,%.3f) direct=(%.1f,%.1f) candidate=(%.1f,%.1f) candidate-valid=%u has-new-move=%u samples=%zu sample-age-ms=%.3f",
								count, static_cast<long long>(newestPanPositionQpc),
								updateVelocity ? 1u : 0u, panTerminalPositionUpdated ? 1u : 0u,
								centroidDelta.x, centroidDelta.y,
								contentDelta.x, contentDelta.y, panMotion.directVelocity.x,
								panMotion.directVelocity.y,
								panMotion.releaseVelocityCandidate.x,
								panMotion.releaseVelocityCandidate.y,
								candidateValid ? 1u : 0u, panMotion.hasNewMove ? 1u : 0u,
								panMotion.velocitySampleCount, velocityAge * 1000.0);
							nextPanMoveDiagnosticQpc = newestPanPositionQpc +
								(std::max)(int64_t{ 1 }, qpcFrequency / 10);
						}
						previousPanCentroid = centroid;
					}
					if (panMotion.lastVelocitySampleQpc > 0)
						lastPanInputQpc = panMotion.lastVelocitySampleQpc;
				}
			}

			for (CanvasGestureContactRuntime& contact : gestureContacts)
			{
				if (contact.snapshot.phase != ContactPhase::Up &&
					contact.snapshot.phase != ContactPhase::Cancelled) continue;
				const CanvasTouchDisposition endedDisposition =
					touchGesture.OnTouchUp(contact.key);
				LogCanvasPan("touch-up key=%llu phase=%s qpc=%lld disposition=%s remaining=%zu position=(%.2f,%.2f)",
					static_cast<unsigned long long>(contact.key),
					contact.snapshot.phase == ContactPhase::Cancelled ? "cancelled" : "up",
					static_cast<long long>(contact.snapshot.qpc),
					CanvasTouchDispositionName(endedDisposition), touchGesture.ContactCount(),
					contact.snapshot.position.x, contact.snapshot.position.y);
				if (endedDisposition == CanvasTouchDisposition::Pan)
				{
					topologyChanged = true;
				}
			}
			std::erase_if(gestureContacts, [&](CanvasGestureContactRuntime& contact)
				{
					if (contact.snapshot.phase != ContactPhase::Up &&
						contact.snapshot.phase != ContactPhase::Cancelled) return false;
					input_.Recycle(contact.handle);
					return true;
				});
			const size_t fsmPanContacts = touchGesture.PanContactCount();
			const size_t gestureRuntimePanContacts = static_cast<size_t>(std::count_if(
				gestureContacts.begin(), gestureContacts.end(),
				[&](const CanvasGestureContactRuntime& contact)
				{
					return touchGesture.Disposition(contact.key) ==
						CanvasTouchDisposition::Pan;
				}));
			const size_t terminalPendingPanContacts = static_cast<size_t>(std::count_if(
				gestureContacts.begin(), gestureContacts.end(),
				[&](const CanvasGestureContactRuntime& contact)
				{
					return contact.terminalPending && touchGesture.Disposition(contact.key) ==
						CanvasTouchDisposition::Pan;
				}));
			const bool panLifecycleConsistent = IsCanvasPanLifecycleOwnershipConsistent(
				touchGesture.PanActive(), fsmPanContacts,
				gestureRuntimePanContacts, terminalPendingPanContacts);
			if (topologyChanged || terminalPendingPanContacts > 0 || !panLifecycleConsistent)
				LogCanvasPan("pan-lifecycle panActive=%u fsmPanContacts=%zu gestureRuntimePanContacts=%zu terminalPendingPanContacts=%zu consistent=%u",
					touchGesture.PanActive() ? 1u : 0u, fsmPanContacts,
					gestureRuntimePanContacts, terminalPendingPanContacts,
					panLifecycleConsistent ? 1u : 0u);
			window_.SetTouchPanActive(touchGesture.PanActive());

			if (touchGesture.PanActive() && topologyChanged)
			{
				rebuildPanContactBaseline((std::max)(panReleaseQpc,
					newestPanPositionQpc), true);
			}

			else if (panMotion.inertiaActive)
			{
				const bool penBrake = penInRange && !suppressPenUntilRelease;
				const bool candidateBrake = touchGesture.InertiaBrakeRequested();
				const bool accelerateStop = penBrake || candidateBrake;
				if (!inertiaBrakeStateValid || previousInertiaBrake != accelerateStop)
				{
					LogCanvasPan("inertia-brake active=%u pen-in-range=%u pen-suppressed=%u candidate-timeout=%u decel=%.1f",
						accelerateStop ? 1u : 0u, penInRange ? 1u : 0u,
						suppressPenUntilRelease ? 1u : 0u, candidateBrake ? 1u : 0u,
						accelerateStop ? kCanvasPanPenBrakeDecelerationDipPerSecondSquared :
							kCanvasPanInertiaDecelerationDipPerSecondSquared);
					previousInertiaBrake = accelerateStop;
					inertiaBrakeStateValid = true;
				}
				const double deltaSeconds = QpcDeltaSeconds(
					nowQpc, lastNavigationQpc, qpcFrequency);
				if (inertiaFirstStepDiagnosticPending)
				{
					LogCanvasPan("inertia-first-step engine=application velocity-before=(%.1f,%.1f) dt-ms=%.3f",
						panMotion.velocity.x, panMotion.velocity.y, deltaSeconds * 1000.0);
					inertiaFirstStepDiagnosticPending = false;
				}
				const bool wasActive = panMotion.inertiaActive;
				const float speedBefore = CanvasPanSpeed(panMotion);
				contentDelta = StepCanvasPanInertia(panMotion,
					deltaSeconds > 0.0 ? deltaSeconds :
						1.0 / configuration_.timingProfile.target_fps, accelerateStop);
				if (wasActive && !panMotion.inertiaActive)
					LogCanvasPan("inertia-stop engine=application reason=threshold speed-before=%.1f brake=%u",
						speedBefore, accelerateStop ? 1u : 0u);
			}

			if ((contentDelta.x != 0.0f || contentDelta.y != 0.0f) && document_)
			{
				InkPage* page = document_->PageAt(currentPageIndex_);
				InkCanvas* canvas = page ? page->FindCanvas(kDefaultDeviceKey) : nullptr;
				if (canvas)
				{
					CanvasViewportState viewport{ canvas->Viewport().x, canvas->Viewport().y };
					const CanvasContentTranslationResult translation =
						ApplyCanvasContentTranslationChecked(
						viewport, contentDelta);
					if (translation.xClamped)
					{
						panMotion.velocity.x = 0.0f;
						if (panMotion.hasNewMove) panMotion.directVelocity.x = 0.0f;
					}
					if (translation.yClamped)
					{
						panMotion.velocity.y = 0.0f;
						if (panMotion.hasNewMove) panMotion.directVelocity.y = 0.0f;
					}
					if (translation.xClamped || translation.yClamped)
						LogCanvasPan("viewport-clamp x=%u y=%u requested=(%.3f,%.3f) applied-viewport=(%.3f,%.3f) velocity=(%.1f,%.1f)",
							translation.xClamped ? 1u : 0u, translation.yClamped ? 1u : 0u,
							contentDelta.x, contentDelta.y,
							translation.viewportDelta.x, translation.viewportDelta.y,
							panMotion.velocity.x, panMotion.velocity.y);
					if ((translation.viewportDelta.x != 0.0f ||
						translation.viewportDelta.y != 0.0f) &&
						canvas->SetViewport({ viewport.x, viewport.y, 1.0f }))
					{
						markPresentationMutation();
						viewportRefreshPending = true;
						historyGpuCache.DiscardHotPreimages();
					}
				}
			}

			if (!touchGesture.PanActive() && panCentroidValid)
			{
				panCentroidValid = false;
				previousPanContactCount = 0;
				const CanvasVector directVelocity = panMotion.directVelocity;
				const CanvasVector releaseCandidate = panMotion.releaseVelocityCandidate;
				const bool candidateValid = panMotion.releaseVelocityCandidateSource !=
					CanvasPanReleaseCandidateSource::None;
				const bool hasNewMove = panMotion.hasNewMove;
				const double secondsSinceLastInput = CanvasPanReleaseAgeSeconds(
					panReleaseQpc, lastPanInputQpc, qpcFrequency, panReleaseCancelled);
				EndCanvasPan(panMotion, secondsSinceLastInput);
				const CanvasVector selectedReleaseVelocity =
					panMotion.selectedReleaseVelocity;
				const bool directReleaseMismatch = hasNewMove &&
					(std::abs(selectedReleaseVelocity.x - directVelocity.x) > 0.01f ||
						std::abs(selectedReleaseVelocity.y - directVelocity.y) > 0.01f);
				if (directReleaseMismatch)
					LogCanvasPan("pan-release-anomaly has-new-move=1 direct=(%.1f,%.1f) candidate=(%.1f,%.1f) selected=(%.1f,%.1f) release-source=%s",
						directVelocity.x, directVelocity.y, releaseCandidate.x,
						releaseCandidate.y, selectedReleaseVelocity.x,
						selectedReleaseVelocity.y,
						CanvasPanReleaseSourceName(panMotion.releaseSource));
				inertiaFirstStepDiagnosticPending = panMotion.inertiaActive;
				inertiaBrakeStateValid = false;
				const char* releaseReason = panReleaseCancelled ? "cancelled" :
					!std::isfinite(secondsSinceLastInput) ? "invalid-release-time" :
					secondsSinceLastInput > kCanvasPanReleaseVelocityHorizonSeconds ? "release-stale" :
					panMotion.lastVelocitySampleQpc <= 0 ? "no-move-samples" :
					CanvasPanSpeed(panMotion) < 5.0f ? "speed-below-threshold" :
					"application-inertia";
				const CanvasPanVelocitySample* firstVelocitySample =
					panMotion.velocitySampleCount > 0 ? &panMotion.velocitySamples[0] : nullptr;
				const CanvasPanVelocitySample* lastVelocitySample =
					panMotion.velocitySampleCount > 0
					? &panMotion.velocitySamples[panMotion.velocitySampleCount - 1] : nullptr;
				const double sampleSpanMilliseconds = firstVelocitySample && lastVelocitySample
					? QpcDeltaSeconds(lastVelocitySample->qpc, firstVelocitySample->qpc,
						qpcFrequency) * 1000.0 : 0.0;
				const double sampleSpanX = firstVelocitySample && lastVelocitySample
					? lastVelocitySample->x - firstVelocitySample->x : 0.0;
				const double sampleSpanY = firstVelocitySample && lastVelocitySample
					? lastVelocitySample->y - firstVelocitySample->y : 0.0;
				LogCanvasPan("pan-release engine=application qpc=%lld last-input-qpc=%lld age-ms=%.3f cancelled=%u samples=%zu sample-first=(%lld,%.2f,%.2f) sample-last=(%lld,%.2f,%.2f) sample-span-ms=%.3f sample-span=(%.2f,%.2f) direct=(%.1f,%.1f) candidate=(%.1f,%.1f) candidate-valid=%u has-new-move=%u selected=(%.1f,%.1f) release-source=%s speed=%.1f inertia=%u reason=%s",
					static_cast<long long>(panReleaseQpc),
					static_cast<long long>(lastPanInputQpc),
					std::isfinite(secondsSinceLastInput) ? secondsSinceLastInput * 1000.0 : -1.0,
					panReleaseCancelled ? 1u : 0u, panMotion.velocitySampleCount,
					static_cast<long long>(firstVelocitySample ? firstVelocitySample->qpc : 0),
					firstVelocitySample ? firstVelocitySample->x : 0.0,
					firstVelocitySample ? firstVelocitySample->y : 0.0,
					static_cast<long long>(lastVelocitySample ? lastVelocitySample->qpc : 0),
					lastVelocitySample ? lastVelocitySample->x : 0.0,
					lastVelocitySample ? lastVelocitySample->y : 0.0,
					sampleSpanMilliseconds, sampleSpanX, sampleSpanY,
					directVelocity.x, directVelocity.y, releaseCandidate.x,
					releaseCandidate.y, candidateValid ? 1u : 0u,
					hasNewMove ? 1u : 0u, selectedReleaseVelocity.x,
					selectedReleaseVelocity.y,
					CanvasPanReleaseSourceName(panMotion.releaseSource),
					CanvasPanSpeed(panMotion), panMotion.inertiaActive ? 1u : 0u,
					releaseReason);
				if (panReleaseQpc > 0) lastPanReleaseQpc = panReleaseQpc;
			}
			lastNavigationQpc = nowQpc;
		};

		TimerPeriodController timerPeriod({ nullptr,
			&BeginOneMillisecondTimerPeriod, &EndOneMillisecondTimerPeriod });
		double lastActiveFrameStartMs = 0.0;
		bool hapticContinuousActive = false;
		std::vector<DrawingCursorVisual> previousCursorVisuals;
		std::vector<DrawingCursorVisual> currentCursorVisuals;
		previousCursorVisuals.reserve(kPreheatedStrokeCount + 1);
		currentCursorVisuals.reserve(kPreheatedStrokeCount + 1);
		uint64_t lastCursorVisualDiagnosticKey = 0;
		bool cursorVisualDiagnosticKnown = false;
		bool cursorVisualDiagnosticRecorded = false;
		double nextCursorVisualDiagnosticMilliseconds = 0.0;
#if defined(DRAW3_RTS_DIAGNOSTICS)
		bool multipleCursorSourceTraceActive = false;
		uint64_t multipleCursorSourceTraceKey = 0;
#endif

		auto updateSpeedEraserHoverLanes = [&](int64_t nowQpc)
		{
			synchronizeSpeedEraserHoverMode();
			if(observedMouseExit!=window_.MouseCanvasExitRevision())
			{
				observedMouseExit=window_.MouseCanvasExitRevision();
				leftMouseSpeedEraser.CancelVisual();rightMouseSpeedEraser.CancelVisual();
			}
			DrawingCursorSample penSample,mouseSample;
			window_.ReadPenCursorSample(penSample);window_.ReadMouseCursorSample(mouseSample);
			if(window_.TouchPanActive() || suppressPenUntilRelease || window_.PenContactSuppressedForTouchPan())penSample.valid=false;
			const auto selected=window_.ActiveTool();
			const auto policy=selected==DrawingTool::Eraser?observedEraserPolicy:SpeedEraser::EraserToolPolicy::ByEntry;
			const double now=AbsoluteQpcSeconds(nowQpc,qpcFrequency);
			auto update=[&](SpeedEraser::MouseLifecycle& session,SpeedEraser::InputEntry entry,
				const DrawingCursorSample& sample,bool tracks)
			{
				if(window_.SelectionMode() || !entryCanErase(entry,selected))
				{if(session.HasPosition() || session.ContactOwned())session.CancelVisual();return false;}
				auto source=sample.valid?sample.source:session.PreviewController().Configuration().inputSource;
				if(entry==SpeedEraser::InputEntry::MouseLeft || entry==SpeedEraser::InputEntry::MouseRight)
					source={SpeedEraser::SourceKind::Mouse};
				const auto cfg=SpeedEraser::ResolveInput(observedSpeedEraserDisplayScale,observedSpeedEraserDeviceMode,
					source,entry,observedEraserInputs,policy);
				if(cfg.kind==SpeedEraser::EraserKind::Fixed)
				{if(session.HasPosition() || session.ContactOwned())session.CancelVisual();return false;}
				session.Configure(cfg.config);
				const float before=session.VisualDiameter();
				if(tracks && sample.valid && !sample.inContact)
					session.ObserveHover(sample.x,sample.y,AbsoluteQpcSeconds(sample.qpc,qpcFrequency));
				session.Advance(now);
				return tracks && sample.valid && (std::abs(before-session.VisualDiameter())>0.001f || session.NeedsAnimation(now));
			};
			const bool left=update(leftMouseSpeedEraser,SpeedEraser::InputEntry::MouseLeft,mouseSample,
				mouseEraserEntry==SpeedEraser::InputEntry::MouseLeft);
			const bool right=update(rightMouseSpeedEraser,SpeedEraser::InputEntry::MouseRight,mouseSample,
				mouseEraserEntry==SpeedEraser::InputEntry::MouseRight);
			const bool tip=update(penEraserHoverLane.lifecycle,SpeedEraser::InputEntry::PenTip,penSample,!penSample.inverted);
			const bool tail=update(invertedPenEraserHoverLane.lifecycle,SpeedEraser::InputEntry::PenTail,penSample,penSample.inverted);
			penEraserHoverLane.sampleVisible=penSample.valid && !penSample.inverted && !penSample.inContact &&
				!penEraserHoverLane.lifecycle.ContactOwned();
			invertedPenEraserHoverLane.sampleVisible=penSample.valid && penSample.inverted && !penSample.inContact &&
				!invertedPenEraserHoverLane.lifecycle.ContactOwned();
			return left || right || tip || tail;
		};
		auto buildDrawingCursorVisuals = [&]()
		{
			currentCursorVisuals.clear();
			laserTipVisuals.clear();
			cursorVisualDiagnosticRecorded = false;
			if (window_.SelectionMode())
			{
				if (CursorDiagnosticsEnabled() &&
					(!cursorVisualDiagnosticKnown || lastCursorVisualDiagnosticKey != UINT64_MAX))
				{
					RecordCursorDiagnostic("frame selection=1 visuals=0 laserTips=0");
					lastCursorVisualDiagnosticKey = UINT64_MAX;
					cursorVisualDiagnosticKnown = true;
					cursorVisualDiagnosticRecorded = true;
				}
				return; // 选择态只呈现画布和瞬态层，不保留绘制光标。
			}
			DrawingCursorSample penSample;
			DrawingCursorSample mouseSample;
			window_.ReadPenCursorSample(penSample);
			window_.ReadMouseCursorSample(mouseSample);
			if (window_.TouchPanActive() || suppressPenUntilRelease ||
				window_.PenContactSuppressedForTouchPan())
				penSample.valid = false;

			if (mouseSpeedEraser().NeedsAnimation(mouseVisualSeconds))
			{
				LARGE_INTEGER visualQpc{};
				QueryPerformanceCounter(&visualQpc);
				mouseVisualSeconds = AbsoluteQpcSeconds(visualQpc.QuadPart, qpcFrequency);
				mouseSpeedEraser().Advance(mouseVisualSeconds); // 耗时帧也按实际呈现时刻收敛。
			}
			// Up 后即使鼠标 mailbox 还停在 Contact，也只绘制无擦除的收尾轮廓。
			if (window_.ActiveTool() == DrawingTool::Eraser && !mouseSpeedEraser().ContactOwned())
			{
				if (!mouseSample.valid && mouseSpeedEraser().HasPosition() &&
					mouseSpeedEraser().NeedsAnimation(mouseVisualSeconds))
				{
					mouseSample.x = mouseSpeedEraser().X();
					mouseSample.y = mouseSpeedEraser().Y();
					mouseSample.valid = true;
					mouseSample.inContact = false;
				}
				if (AbsoluteQpcSeconds(mouseSample.qpc, qpcFrequency) <= mouseSpeedEraser().LastEventSeconds())
					mouseSample.inContact = false;
			}
			DrawingTool cursorTool = window_.EffectiveDrawingCursorTool();
			const bool mouseUsesSystemCursor = window_.GetMouseUsesSystemCursor();
			const DrawingCursorPointerAuthority cursorAuthority = window_.CursorOwner();
			const bool primaryUsesPen = cursorAuthority == DrawingCursorPointerAuthority::Pen ||
				(cursorAuthority == DrawingCursorPointerAuthority::Unknown && penSample.valid);
			const bool primaryUsesMouse = cursorAuthority == DrawingCursorPointerAuthority::Mouse ||
				(cursorAuthority == DrawingCursorPointerAuthority::Unknown &&
					!penSample.valid && mouseSample.valid);
			const RuntimeStroke* primaryRuntime = nullptr;
			for (const RuntimeStroke* runtime : active)
			{
				if (!runtime || runtime->ended || runtime->awaitingReconnect) continue;
				if ((primaryUsesPen && runtime->metricDeviceType == InputDeviceType::Pen) ||
					(primaryUsesMouse && (runtime->metricDeviceType == InputDeviceType::MouseLeft ||
						runtime->metricDeviceType == InputDeviceType::MouseRight)))
				{
					if(!primaryRuntime || runtime->qpcOrigin>primaryRuntime->qpcOrigin)primaryRuntime = runtime;
				}
			}
			if(primaryRuntime)cursorTool=primaryRuntime->tool;
#if defined(DRAW3_RTS_DIAGNOSTICS)
			bool primaryCursorSourceVisible = false;
			size_t runtimeCursorSourceCount = 0;
#endif
			if (cursorTool == DrawingTool::Laser)
			{
				const DrawingCursorVisual primary = ResolveLaserDrawingCursorVisual(
					penSample, mouseSample, window_.CursorOwner(),
					window_.CursorAppearanceForTool(DrawingTool::Laser));
				if (primary.visible)
				{
#if defined(DRAW3_RTS_DIAGNOSTICS)
					primaryCursorSourceVisible = true;
#endif
					laserTipVisuals.push_back({
						.dot = { primary.x, primary.y,
							primary.appearance.width * 0.5f, primary.appearance.opacity },
						.visualStyle = currentProductVisualStyle });
				}
			}
			else
			{
				DrawingCursorVisual primary = ResolvePrimaryDrawingCursorVisual(
					penSample, mouseSample, window_.CursorOwner(),
					window_.CursorAppearanceForTool(cursorTool),
					window_.CursorAppearanceForTool(DrawingTool::Eraser),
					cursorTool == DrawingTool::Eraser,
					drawingCursorDuringContactEnabled_.load(std::memory_order_acquire),
					translucentInkCursorEnabled_.load(std::memory_order_acquire),
					mouseUsesSystemCursor);
				if (primary.visible &&
					primary.appearance.shape == DrawingCursorShape::EraserGripCircle)
				{
					float dynamicDiameter = -1.0f;
					if (primaryRuntime && primaryRuntime->tool == DrawingTool::Eraser &&
						primaryRuntime->stroke.widthMode == StrokeWidthMode::SpeedEraser)
						dynamicDiameter = RuntimeSpeedEraserContactDiameter(*primaryRuntime);
					else if(!primaryRuntime)
					{
						const auto entry=primaryUsesPen?SpeedEraser::EntryForInput(1,penSample.inverted):mouseEraserEntry;
						const auto& sample=primaryUsesPen?penSample:mouseSample;
						const auto cfg=SpeedEraser::ResolveInput(observedSpeedEraserDisplayScale,observedSpeedEraserDeviceMode,
							primaryUsesPen?sample.source:SpeedEraser::InputSource{SpeedEraser::SourceKind::Mouse},
							entry,observedEraserInputs,window_.ActiveTool()==DrawingTool::Eraser?
								observedEraserPolicy:SpeedEraser::EraserToolPolicy::ByEntry);
						if(cfg.kind==SpeedEraser::EraserKind::Fixed)
							dynamicDiameter=SpeedEraser::FixedDiameterPx(cfg.config.sizes.fixedDiameterDip,cfg.config.display);
						else if(const auto* session=sessionForEntry(entry))
						{
							if(sample.inContact && !session->ContactOwned())
							{
								// RTS光标通知可能先于contact入队；仍按同一来源/事件时刻求首帧尺寸，不回退50px。
								SpeedEraser::Controller pending;
								pending.BeginContact(session->HasPosition()?&session->PreviewController():nullptr,
									sample.x,sample.y,AbsoluteQpcSeconds(sample.qpc,qpcFrequency),cfg.config);
								dynamicDiameter=pending.Diameter();
							}
							else dynamicDiameter=session->HasPosition()?session->VisualDiameter():cfg.config.StandardDiameterPx();
						}
					}
					if(primaryRuntime && primaryRuntime->tool==DrawingTool::Eraser &&
						primaryRuntime->stroke.widthMode==StrokeWidthMode::Fixed)
						dynamicDiameter=primaryRuntime->stroke.widthEstimator.baseDiameter;
					ApplySpeedEraserCursorDiameter(primary.appearance, dynamicDiameter);
				}
				if (primary.visible)
				{
#if defined(DRAW3_RTS_DIAGNOSTICS)
					primaryCursorSourceVisible = true;
#endif
					currentCursorVisuals.push_back(primary);
				}
			}

			const size_t primaryCursorCount = currentCursorVisuals.size();
			const size_t primaryLaserTipCount = laserTipVisuals.size();
			const DrawingCursorAppearance eraserAppearance =
				window_.CursorAppearanceForTool(DrawingTool::Eraser);
			for (const RuntimeStroke* runtime : active)
			{
				if (runtime && !runtime->ended && !runtime->awaitingReconnect &&
					runtime->metricDeviceType == InputDeviceType::Touch &&
					runtime->tool == DrawingTool::Laser)
				{
					const ContactSnapshot& snapshot = runtime->lastModelSnapshot;
					// Touch 笔尖沿用各自 Down 样式，避免多 contact 调色后互相串色。
					laserTipVisuals.push_back({
						.dot = { snapshot.position.x, snapshot.position.y,
							LaserSolidRadius(configuration_.dpiScale), 1.0f },
						.visualStyle = runtime->visualStyle });
#if defined(DRAW3_RTS_DIAGNOSTICS)
					++runtimeCursorSourceCount;
#endif
					continue;
				}
				if (!runtime || runtime->ended || runtime->awaitingReconnect ||
					runtime->metricDeviceType != InputDeviceType::Touch ||
					runtime->tool != DrawingTool::Eraser) continue;
				const ContactSnapshot& snapshot = runtime->lastModelSnapshot;
				DrawingCursorAppearance touchAppearance = eraserAppearance;
				if (runtime->stroke.widthMode == StrokeWidthMode::SpeedEraser)
					ApplySpeedEraserCursorDiameter(
						touchAppearance, RuntimeSpeedEraserContactDiameter(*runtime));
				else ApplySpeedEraserCursorDiameter(touchAppearance,runtime->stroke.widthEstimator.baseDiameter);
				const DrawingCursorVisual touchVisual = MakeTouchEraserDrawingCursorVisual(
					snapshot.position.x, snapshot.position.y, touchAppearance);
				if (touchVisual.visible)
				{
					currentCursorVisuals.push_back(touchVisual);
#if defined(DRAW3_RTS_DIAGNOSTICS)
					++runtimeCursorSourceCount;
#endif
				}
			}

			if (CursorDiagnosticsEnabled())
			{
				size_t liveTouchCount = 0;
				for (const RuntimeStroke* runtime : active)
					if (runtime && !runtime->ended && !runtime->awaitingReconnect &&
						runtime->metricDeviceType == InputDeviceType::Touch) ++liveTouchCount;
				uint64_t key = static_cast<uint64_t>(window_.CursorOwner());
				key = key * 17u + static_cast<uint64_t>(cursorTool);
				key = key * 17u + currentCursorVisuals.size();
				key = key * 17u + primaryCursorCount;
				key = key * 17u + laserTipVisuals.size();
				key = key * 17u + primaryLaserTipCount;
				key = key * 17u + liveTouchCount;
				key = key * 17u + (penSample.valid ? 1u : 0u);
				key = key * 17u + (penSample.inContact ? 1u : 0u);
				key = key * 17u + (penSample.inverted ? 1u : 0u);
				key = key * 17u + (mouseSample.valid ? 1u : 0u);
				key = key * 17u + (mouseSample.inContact ? 1u : 0u);
				const double nowMilliseconds = GetQpcTimeMilliseconds();
				const bool activeCursor = !currentCursorVisuals.empty() ||
					!laserTipVisuals.empty() || penSample.valid || mouseSample.valid || liveTouchCount;
				if (!cursorVisualDiagnosticKnown || key != lastCursorVisualDiagnosticKey ||
					(activeCursor && nowMilliseconds >= nextCursorVisualDiagnosticMilliseconds))
				{
					cursorVisualDiagnosticKnown = true;
					lastCursorVisualDiagnosticKey = key;
					nextCursorVisualDiagnosticMilliseconds = nowMilliseconds + 100.0;
					cursorVisualDiagnosticRecorded = true;
					RecordCursorDiagnostic("frame tool=%u owner=%u primary=%zu touchVisuals=%zu laserPrimary=%zu laserTouch=%zu liveTouch=%zu pen=%u/%u/%u mouse=%u/%u mouseLifecycle=%u/%u",
						static_cast<unsigned>(cursorTool), static_cast<unsigned>(window_.CursorOwner()),
						primaryCursorCount, currentCursorVisuals.size() - primaryCursorCount,
						primaryLaserTipCount, laserTipVisuals.size() - primaryLaserTipCount,
						liveTouchCount, penSample.valid ? 1u : 0u,
						penSample.inContact ? 1u : 0u, penSample.inverted ? 1u : 0u,
						mouseSample.valid ? 1u : 0u, mouseSample.inContact ? 1u : 0u,
						mouseSpeedEraser().HasPosition() ? 1u : 0u,
						mouseSpeedEraser().NeedsAnimation(mouseVisualSeconds) ? 1u : 0u);
					for (size_t index = 0; index < currentCursorVisuals.size(); ++index)
					{
						const auto& visual = currentCursorVisuals[index];
						const auto& appearance = visual.appearance;
						RecordCursorDiagnostic("visual index=%zu source=%s shape=%u x=%.1f y=%.1f width=%.1f height=%.1f opacity=%.3f fill=%.3f outline=%.1f rgb=%.2f/%.2f/%.2f",
							index, index < primaryCursorCount ? "primary" : "touch",
							static_cast<unsigned>(appearance.shape), visual.x, visual.y,
							appearance.width, appearance.height, appearance.opacity,
							appearance.fillAlpha, appearance.outlineWidth,
							appearance.red, appearance.green, appearance.blue);
					}
					for (size_t index = 0; index < laserTipVisuals.size(); ++index)
					{
						const auto& visual = laserTipVisuals[index];
						RecordCursorDiagnostic("laser-tip index=%zu source=%s x=%.1f y=%.1f radius=%.1f opacity=%.3f color=0x%08x widthDip=%.1f",
							index, index < primaryLaserTipCount ? "primary" : "touch",
							visual.dot.x, visual.dot.y, visual.dot.radius,
							visual.dot.opacity, visual.visualStyle.colorRgba,
							visual.visualStyle.widthDip);
					}
					for (const RuntimeStroke* runtime : active)
					{
						if (!runtime || runtime->ended || runtime->awaitingReconnect) continue;
						const auto* record = runtime->handle.record;
						RecordCursorDiagnostic("runtime device=%u tool=%u selected=%u tcid=%u cid=%u generation=%llu x=%.1f y=%.1f qpc=%lld",
							static_cast<unsigned>(runtime->metricDeviceType),
							static_cast<unsigned>(runtime->tool),
							static_cast<unsigned>(runtime->selectedTool),
							record ? record->TabletContextId() : 0,
							record ? record->ContactId() : 0,
							static_cast<unsigned long long>(runtime->handle.generation),
							runtime->lastModelSnapshot.position.x,
							runtime->lastModelSnapshot.position.y,
							static_cast<long long>(runtime->lastModelSnapshot.qpc));
					}
				}
			}

			if(observer_.eraserDiagnostics && window_.EraserDiagnosticsEnabled())
			{
				SpeedEraser::Diagnostics d;
				const RuntimeStroke* r=primaryRuntime;
				if(!r)for(const auto* candidate:active)
					if(candidate && !candidate->ended && candidate->tool==DrawingTool::Eraser)
					{r=candidate;break;}
				const SpeedEraser::Controller* controller=nullptr;
				if(r && r->stroke.widthMode==StrokeWidthMode::SpeedEraser)
				{
					d=r->eraserDiagnostics;controller=&r->speedEraserOc;
					d.active=!r->ended;d.inputType=static_cast<uint32_t>(r->metricDeviceType);
					d.nextRadiusPx=r->eraserSize.effectiveDiameterPx*0.5f;
					d.historyRadiusPx=r->stroke.realPoints.empty()?0:r->stroke.realPoints.back().r;
					d.realPointCount=r->stroke.realPoints.size();
					d.firstPointRadiusPx=r->stroke.realPoints.empty()?0:r->stroke.realPoints.front().r;
				}
				else if(window_.ActiveTool()==DrawingTool::Eraser || penSample.inverted)
				{
					const auto* lane=penSample.inverted?&invertedPenEraserHoverLane:&penEraserHoverLane;
					if(primaryUsesPen && lane->lifecycle.HasPosition() && lane->sampleVisible)
					{controller=&lane->lifecycle.PreviewController();d.inputType=static_cast<uint32_t>(InputDeviceType::Pen);}
					else if(primaryUsesMouse && mouseSpeedEraser().HasPosition())
					{controller=&mouseSpeedEraser().PreviewController();d.inputType=static_cast<uint32_t>(
						mouseEraserEntry==SpeedEraser::InputEntry::MouseRight?
						InputDeviceType::MouseRight:InputDeviceType::MouseLeft);}
					d.preview=controller!=nullptr;
					if(controller)d.nextRadiusPx=controller->Diameter()*0.5f;
				}
				if(controller)
				{
					const auto& cfg=controller->Configuration();
					d.mode=cfg.mode;d.motionSource=cfg.motionSource;d.motionUnit=cfg.motionUnit;d.sizes=cfg.sizes;
					d.entry=cfg.inputEntry;d.formalPenResponse=cfg.formalPenResponse;d.developmentResponseOverride=cfg.developmentResponseOverride;
					d.inputSource=cfg.inputSource;d.response=cfg.response;d.inputMapped=cfg.inputMapped;
					d.monitor=cfg.display.monitor;d.displayGeneration=cfg.display.generation;d.displayRevision=cfg.display.revision;
					d.rhoMmPerDip=cfg.rhoMmPerDip;d.penBeta=cfg.penBeta;d.heuristicGain=cfg.heuristicGain;
					d.dpiX=96/cfg.display.dipPerPixelX;d.dpiY=96/cfg.display.dipPerPixelY;
					d.effectiveDiameterDip=controller->DiameterDip();
					d.targetDiameterDip=controller->TargetDiameterDip();d.touchUnlocked=controller->TouchUnlocked();
					d.needsAnimation=controller->NeedsAnimation(mouseVisualSeconds);
					d.contactArea=controller->AreaDiagnostics(mouseVisualSeconds);
					d.fine=controller->FineDiagnostics();
					d.dipPerPixelX=cfg.display.dipPerPixelX;d.dipPerPixelY=cfg.display.dipPerPixelY;
					d.motionPerPixelX=cfg.motionPerPixelX;d.motionPerPixelY=cfg.motionPerPixelY;
					d.pixelWidth=cfg.display.pixelWidth;d.pixelHeight=cfg.display.pixelHeight;
					d.manualWidthCm=cfg.display.development.calibration.widthCm;
					d.manualHeightCm=cfg.display.development.calibration.heightCm;
					d.speed=controller->Speed();d.evidenceSeconds=controller->SweepEvidenceSeconds();
					d.sweeping=controller->Sweeping();d.qualified=controller->SweepQualified();d.limited=controller->TargetLimited();
					d.idleSeconds=controller->SecondsSinceMovement(mouseVisualSeconds);
				}
				if(r)
				{
					d.selectedTool=static_cast<uint32_t>(r->selectedTool);d.effectiveTool=static_cast<uint32_t>(r->tool);
					d.downSeconds=r->eraserDiagnostics.downSeconds;d.downDiameterPx=r->eraserDiagnostics.downDiameterPx;
					d.firstPointRadiusPx=r->stroke.realPoints.empty()?0:r->stroke.realPoints.front().r;
				}
				d.eraserContact=r && !r->ended && r->tool==DrawingTool::Eraser;
				if(d.eraserContact && r->stroke.widthMode!=StrokeWidthMode::SpeedEraser)
				{
					d.inputType=static_cast<uint32_t>(r->metricDeviceType);d.inputSource=r->lastInputSnapshot.source;
					d.entry=r->resolvedEraser.entry;d.eraserKind=r->resolvedEraser.kind;
					const auto& cfg=r->resolvedEraser.config;
					d.nextRadiusPx=r->eraserSize.effectiveDiameterPx*0.5f;
					d.dipPerPixelX=cfg.display.dipPerPixelX;d.dipPerPixelY=cfg.display.dipPerPixelY;
					d.dpiX=96/cfg.display.dipPerPixelX;d.dpiY=96/cfg.display.dipPerPixelY;
					d.effectiveDiameterDip=r->eraserSize.effectiveDiameterPx*std::sqrt(cfg.display.dipPerPixelX*cfg.display.dipPerPixelY);
					d.formalPenResponse=cfg.formalPenResponse;d.developmentResponseOverride=cfg.developmentResponseOverride;
				}
				d.frameSeconds=mouseVisualSeconds;
				d.cursorDiameterPx=currentCursorVisuals.empty()?0:currentCursorVisuals.front().appearance.width;
				observer_.eraserDiagnostics(observer_.context,d);
			}
#if defined(DRAW3_RTS_DIAGNOSTICS)
			DrawingCursorDiagnosticVisualState diagnosticState;
			const bool traceEnabled = ReadDrawingCursorDiagnosticVisualState(diagnosticState);
			const size_t visibleCursorSourceCount = runtimeCursorSourceCount +
				(primaryCursorSourceVisible ? 1u : 0u);
			uint64_t traceKey = 0;
			auto mixTraceKey = [&traceKey](uint64_t value) noexcept
			{
				traceKey ^= value + 0x9e3779b97f4a7c15ull +
					(traceKey << 6) + (traceKey >> 2);
			};
			if (traceEnabled)
			{
				mixTraceKey(visibleCursorSourceCount);
				mixTraceKey(primaryCursorSourceVisible ? 1u : 0u);
				mixTraceKey(static_cast<uint64_t>(window_.CursorOwner()));
				mixTraceKey(static_cast<uint64_t>(cursorTool));
				mixTraceKey(penSample.valid ? 1u : 0u);
				mixTraceKey(static_cast<uint64_t>(penSample.qpc));
				mixTraceKey(mouseSample.valid ? 1u : 0u);
				mixTraceKey(static_cast<uint64_t>(mouseSample.qpc));
				for (const RuntimeStroke* runtime : active)
				{
					if (!runtime) continue;
					const ContactRecord* record = runtime->handle.record;
					mixTraceKey(record ? record->TabletContextId() : 0);
					mixTraceKey(record ? record->ContactId() : 0);
					mixTraceKey(runtime->handle.generation);
					mixTraceKey(static_cast<uint64_t>(runtime->metricDeviceType));
					mixTraceKey(static_cast<uint64_t>(runtime->tool));
					mixTraceKey(static_cast<uint64_t>(runtime->lastModelSnapshot.qpc));
					mixTraceKey(runtime->ended ? 1u : 0u);
					mixTraceKey(runtime->awaitingReconnect ? 1u : 0u);
				}
			}
			if (!traceEnabled || visibleCursorSourceCount < 2)
			{
				multipleCursorSourceTraceActive = false;
				multipleCursorSourceTraceKey = 0;
			}
			else if (!multipleCursorSourceTraceActive ||
				multipleCursorSourceTraceKey != traceKey)
			{
				multipleCursorSourceTraceActive = true;
				multipleCursorSourceTraceKey = traceKey;
				std::cout << "[CURSOR_TRACE][cursor-sources] count=" <<
					visibleCursorSourceCount << " primaryVisible=" <<
					(primaryCursorSourceVisible ? 1u : 0u) << " runtimeSources=" <<
					runtimeCursorSourceCount << " cursorOwner=" <<
					static_cast<unsigned>(window_.CursorOwner()) << " tool=" <<
					DrawingToolName(cursorTool) << std::endl;
				std::cout << "[CURSOR_TRACE][primary-samples] pen={valid=" <<
					(penSample.valid ? 1u : 0u) << ",contact=" <<
					(penSample.inContact ? 1u : 0u) << ",inverted=" <<
					(penSample.inverted ? 1u : 0u) << ",qpc=" << penSample.qpc <<
					",x=" << penSample.x << ",y=" << penSample.y << "} mouse={valid=" <<
					(mouseSample.valid ? 1u : 0u) << ",contact=" <<
					(mouseSample.inContact ? 1u : 0u) << ",qpc=" << mouseSample.qpc <<
					",x=" << mouseSample.x << ",y=" << mouseSample.y << "}" << std::endl;
				size_t traceIndex = 0;
				for (const RuntimeStroke* runtime : active)
				{
					if (!runtime) continue;
					const ContactRecord* record = runtime->handle.record;
					const ContactSnapshot& snapshot = runtime->lastModelSnapshot;
					const bool cursorSource = !runtime->ended && !runtime->awaitingReconnect &&
						runtime->metricDeviceType == InputDeviceType::Touch &&
						(runtime->tool == DrawingTool::Eraser ||
							runtime->tool == DrawingTool::Laser);
					std::cout << "[CURSOR_TRACE][active-runtime] index=" << traceIndex++ <<
						" tcid=" << (record ? record->TabletContextId() : 0) <<
						" cid=" << (record ? record->ContactId() : 0) <<
						" generation=" << runtime->handle.generation <<
						" device=" << InputDeviceTypeName(runtime->metricDeviceType) <<
						" tool=" << DrawingToolName(runtime->tool) <<
						" cursorSource=" << (cursorSource ? 1u : 0u) <<
						" ended=" << (runtime->ended ? 1u : 0u) <<
						" awaitingReconnect=" << (runtime->awaitingReconnect ? 1u : 0u) <<
						" qpc=" << snapshot.qpc << " x=" << snapshot.position.x <<
						" y=" << snapshot.position.y <<
						std::endl;
				}
			}
#endif
		};

		auto cursorVisualsEquivalent = [&]() noexcept
		{
			if (previousCursorVisuals.size() != currentCursorVisuals.size()) return false;
			for (size_t index = 0; index < currentCursorVisuals.size(); ++index)
			{
				if (!AreDrawingCursorVisualsEquivalent(
					previousCursorVisuals[index], currentCursorVisuals[index])) return false;
			}
			return true;
		};

		auto cursorVisualBounds = [&](const std::vector<DrawingCursorVisual>& visuals)
		{
			RECT bounds = {};
			const WindowSize size = window_.Size();
			for (const DrawingCursorVisual& visual : visuals)
				UnionRectInPlace(bounds,
					DrawingCursorVisualBounds(visual, size.width, size.height));
			return bounds;
		};

		auto currentRasterKey = [&]() noexcept
		{
			return InkHistoryRasterKey{
				kDefaultDeviceKey,
				1.0f,
				static_cast<uint32_t>(DXGI_FORMAT_B8G8R8A8_UNORM),
				rasterPipelineGeneration
			};
		};

		auto restorePageContent = [&](size_t pageIndex, int width, int height,
			bool clearTargetTiles)
		{
			CompositionRestoreResult result;
			if (!document_ || pageIndex >= pageRuntimeStates.size()) return result;
			const InkPage* page = document_->PageAt(pageIndex);
			const InkCanvas* canvas = page
				? page->FindCanvas(kDefaultDeviceKey) : nullptr;
			if (!page || !canvas) return result;
			const CanvasPageRuntimeState& runtime = pageRuntimeStates[pageIndex];
			const InkViewport viewport = canvas->Viewport();
			std::vector<SignedTileCoordinate> tiles =
				CollectVisibleCompositionTiles(runtime.history, viewport, width, height);
			if (tiles.empty())
			{
				result.path = CompositionRestorePath::Empty;
				return result;
			}
			const CompositionRestoreRequest request = {
				{ page->PageGuid(), kDefaultDeviceKey },
				currentRasterKey(),
				canvas,
				&runtime.history,
				tiles,
				runtime.history.Items().size(),
				viewport.x,
				viewport.y,
				width,
				height,
				clearTargetTiles
			};
			return historyGpuCache.RestoreComposition(request);
		};

		auto appendBlankPageWithRuntime = [&]() -> std::optional<size_t>
		{
			if (!document_) return std::nullopt;
			pageRuntimeStates.emplace_back();
			const std::optional<size_t> pageIndex = TryAppendBlankPage(*document_);
			if (!pageIndex)
			{
				pageRuntimeStates.pop_back();
				return std::nullopt;
			}
			if (*pageIndex + 1 != pageRuntimeStates.size())
			{
				std::cout << "[InkHistory] page/runtime index mismatch." << std::endl;
				return std::nullopt;
			}
			pageRuntimeStates.back().rasterState = allocateRasterStateToken();
			return pageIndex;
		};
		// active document 留在既有字段中；非活动场景把整组 CPU/runtime 状态停入槽位。
		auto createBlankSlot = [&](DrawingDocumentSlot& slot,
			std::size_t pageCount) -> bool
		{
			if (slot.document) return true;
			InkGuid workspaceGuid;
			if (!TryCreateInkGuid(workspaceGuid)) return false;
			InkCanvasCollection created(workspaceGuid);
			std::vector<CanvasPageRuntimeState> runtimes;
			const std::size_t count = (std::max)(std::size_t{ 1 }, pageCount);
			for (std::size_t index = 0; index < count; ++index)
			{
				const auto page = TryAppendBlankPage(created);
				if (!page || *page != index) return false;
				runtimes.emplace_back();
				runtimes.back().rasterState = allocateRasterStateToken();
			}
			slot.document.emplace(std::move(created));
			slot.pageRuntimeStates = std::move(runtimes);
			slot.currentPageIndex = 0;
			return true;
		};

			auto swapActiveDocument = [&](DrawingDocumentSlot& slot)
		{
			std::swap(document_, slot.document);
			std::swap(pageRuntimeStates, slot.pageRuntimeStates);
			std::swap(activeRetainedSlides, slot.retainedSlides);
			std::swap(currentPageIndex_, slot.currentPageIndex);
			std::swap(activePresentationTarget, slot.presentationTarget);
			std::swap(activePresentationFileGuid, slot.fileGuid);
			std::swap(activePresentationMutationRevision, slot.mutationRevision);
			std::swap(activePresentationQueuedRevision, slot.queuedRevision);
			std::swap(activePresentationCommittedRevision, slot.committedRevision);
			std::swap(activePresentationPersistenceInitialized,
				slot.persistenceInitialized);
			std::swap(activePresentationLoadPending, slot.loadPending);
		};

		auto parkedActiveSlot = [&]() -> DrawingDocumentSlot*
		{
			if (activeWorkspace == Bridge::Workspace::Desktop) return &desktopSlot;
			if (activeWorkspace == Bridge::Workspace::Whiteboard) return &whiteboardSlot;
			if (activePresentationKey)
			{
				auto found = presentationSlots.find(*activePresentationKey);
				if (found != presentationSlots.end()) return &found->second;
			}
			return &isolatedPresentationSlot;
		};

		auto canEvictPresentationSlot = [](const DrawingDocumentSlot& slot) noexcept
		{
			return ShouldEvictPresentationSlot(slot.fileGuid.has_value(),
				slot.mutationRevision, slot.queuedRevision,
				slot.committedRevision, slot.loadPending);
		};

			auto resetGpuForPageSwitch = [&](RECT& frameDirty,
			LaserParticleDirtySnapshot& particleSnapshot, bool& forceFullPresent,
			int width, int height)
		{
			frameDirty = GetFullCanvasRect(width, height);
			renderer_.ClearRTV(renderer_.layerL2RTV.Get(), kTransparentLayerClearColor);
			renderer_.ClearOperatorLayer(renderer_.layerL1);
			renderer_.ClearOperatorLayer(renderer_.layerL0);
			renderer_.ClearAllLaserCoverage();
			renderer_.ClearRTV(renderer_.backBufferRTV.Get(), kTransparentLayerClearColor);
			laserLifecycle = {};
			laserOpacity = 0.0f;
			laserStableBounds = {};
			laserLiveBounds = {};
			laserStrokeLayers.clear();
			laserCoverageMode = LaserCoverageMode::Inactive;
			renderer_.ResetLaserParticles();
			laserParticleDirtyTracker.Clear();
			particleSnapshot = {};
			lastLaserParticleSimulationQpc = 0;
			previousLaserParticleBounds = {};
			previousLaserTipBounds = {};
			forceFullPresent = true;
		};

		auto restoreAfterDocumentSlotSwitch = [&](RECT& frameDirty,
			LaserParticleDirtySnapshot& particleSnapshot, bool& forceFullPresent,
			int width, int height)
		{
			// CPU 文档切换后，所有绑定旧 page/raster identity 的 GPU 状态必须失效。
			historyGpuCache.DiscardHotPreimages();
			historyGpuCache.DiscardCompositionCache();
			compositionMaintenance.clear();
			if (rasterPipelineGeneration ==
				(std::numeric_limits<uint64_t>::max)()) rasterPipelineGeneration = 1;
			else ++rasterPipelineGeneration;
			renderer_.InvalidateTrustedL2Snapshot();
			trustedSnapshotSignatureValid = false;
			viewportTilePlan = {};
			viewportTilePlanIndex = 0;
			viewportRecoveryPending = false;
			viewportRefreshPending = false;
			viewportRefreshClearsTransient = false;
			viewportVisibleClear = true;
			pendingLaserBakeDirty = {};
			laserTipVisuals.clear();
			laserParticleEmissionRequests.clear();
			previousCursorVisuals.clear();
			currentCursorVisuals.clear();
			laserIncrementalEnsureAttempted = false;
			publishedCurrentPageHasContent = !currentPageHasContent();
			resetGpuForPageSwitch(frameDirty, particleSnapshot,
				forceFullPresent, width, height);
			const CompositionRestoreResult restored = restorePageContent(
				currentPageIndex_, width, height, false);
			if (restored.path == CompositionRestorePath::Failed)
			{
				viewportVisibleClear = false;
				viewportRefreshPending = true;
			}
			publishCurrentPageContent();
		};

		auto buildPresentationSaveRequest = [&](const InkCanvasCollection& document,
			const std::vector<CanvasPageRuntimeState>& runtimes,
			std::size_t currentPage, const Bridge::PresentationTarget& target,
			std::optional<draw3::uink::UInkGuid>& fileGuid,
			std::uint64_t mutationRevision,
			std::optional<draw3::uink::UInkGuid> clearPageGuid = std::nullopt)
			-> std::optional<PresentationSaveRequest>
		{
			try
			{
				if (!fileGuid) fileGuid = draw3::uink::CreateUInkGuid();
				if (!fileGuid || target.totalPages != document.Pages().size() ||
					runtimes.size() != document.Pages().size()) return std::nullopt;
				draw3::uink::Draw3UInkExportSnapshot snapshot;
				snapshot.fileGuid = *fileGuid;
				snapshot.workspaceGuid = draw3::uink::UInkGuid(
					document.WorkspaceGuid().Bytes());
				snapshot.workspaceName = target.presentationName;
				snapshot.workspaceType = target.bindingMode ==
					Bridge::SlideBindingMode::StableSlideId ? 2 :
					draw3::uink::kInkeysPageIndexWorkspaceType;
				snapshot.hostId = FormatPresentationKey(target.key);
				snapshot.currentPageIndex = static_cast<std::uint32_t>(currentPage);
				const auto importMode = target.bindingMode ==
					Bridge::SlideBindingMode::StableSlideId
					? draw3::uink::Draw3UInkImportBindingMode::StableSlideId
					: draw3::uink::Draw3UInkImportBindingMode::PageIndexFallback;
				snapshot.workspaceExtra = draw3::uink::MakeInkeysBindingExtra(importMode);
				snapshot.dpiScale = configuration_.dpiScale;
				snapshot.assignedIndependentUndoGroups = true;
				auto captureCanvas = [&](const InkPage* page,
					const CanvasPageRuntimeState& runtime, std::size_t pageIndex,
					std::optional<std::int32_t> slideId, bool retained)
					-> std::optional<draw3::uink::Draw3UInkCanvasSnapshot>
				{
					if (!page || !slideId && target.bindingMode ==
						Bridge::SlideBindingMode::StableSlideId) return std::nullopt;
					const InkCanvas* canvas = page->FindCanvas(kDefaultDeviceKey);
					if (!canvas) return std::nullopt;
					draw3::uink::Draw3UInkCanvasSnapshot output;
					output.pageGuid = draw3::uink::UInkGuid(page->PageGuid().Bytes());
					output.pageIndex = static_cast<std::uint32_t>(pageIndex);
					output.pageNumber = static_cast<std::uint32_t>(pageIndex + 1);
					output.slideId = slideId;
					output.retained = retained;
					output.intervalOrdinal = runtime.intervalOrdinal;
					output.viewport = { canvas->Viewport().x, canvas->Viewport().y,
						canvas->Viewport().scale };
					output.extra = draw3::uink::MakeInkeysBindingExtra(importMode);
					const std::span<const InkStroke> strokes = canvas->Strokes();
					for (const RenderItemState& item : runtime.history.Items())
					{
						if (!item.visible) continue;
						if (item.strokeIndex >= strokes.size()) return std::nullopt;
						const InkStroke& stroke = strokes[item.strokeIndex];
						const auto kind = UInkKindForStoredType(stroke.Style().inkType);
						if (!kind) return std::nullopt;
						draw3::uink::Draw3UInkStrokeSnapshot outputStroke;
						outputStroke.style = { *kind, stroke.Style().opacity,
							stroke.Style().fallbackRgb, stroke.Style().texture };
						outputStroke.undoId = static_cast<std::uint32_t>(output.strokes.size());
						for (const StoredInkPoint& point : stroke.Points())
							outputStroke.points.push_back({ point.x, point.y, point.width });
						output.strokes.push_back(std::move(outputStroke));
					}
					return output;
				};
				for (std::size_t pageIndex = 0;
					pageIndex < document.Pages().size(); ++pageIndex)
				{
					const InkPage* page = document.PageAt(pageIndex);
					const std::optional<std::int32_t> slideId = target.bindingMode ==
						Bridge::SlideBindingMode::StableSlideId
						? std::optional<std::int32_t>(target.slideIds[pageIndex]) : std::nullopt;
					const auto output = captureCanvas(page, runtimes[pageIndex], pageIndex,
						slideId, false);
					if (!output) return std::nullopt;
					snapshot.activeCanvases.push_back(*output);
				}
				// 保留 legacy canvases 投影，兼容旧的 UInk 测试与读取器。
				snapshot.canvases = snapshot.activeCanvases;
				if (target.bindingMode == Bridge::SlideBindingMode::StableSlideId)
					for (const auto& [slideId, retained] : activeRetainedSlides)
					{
						auto output = retained;
						output.slideId = slideId;
						output.retained = true;
						snapshot.retainedCanvases.push_back(std::move(output));
					}
				PresentationSaveRequest request;
				request.target = target;
				request.mutationRevision = mutationRevision;
				request.snapshot = std::move(snapshot);
				request.clearPageGuid = clearPageGuid;
				if (clearPageGuid)
				{
					const auto& active = request.snapshot.activeCanvases.empty()
						? request.snapshot.canvases : request.snapshot.activeCanvases;
					for (const auto& canvas : active)
						if (canvas.pageGuid == *clearPageGuid)
							request.clearIntervalOrdinal = canvas.intervalOrdinal;
					for (const auto& canvas : request.snapshot.retainedCanvases)
						if (canvas.pageGuid == *clearPageGuid)
							request.clearIntervalOrdinal = canvas.intervalOrdinal;
				}
				return request;
			}
			catch (...)
			{
				std::fputs("[Draw3.Presentation] action=capture result=failed\n", stderr);
				return std::nullopt;
			}
		};

		auto submitPresentationSlot = [&](const InkCanvasCollection& document,
			const std::vector<CanvasPageRuntimeState>& runtimes,
			std::size_t currentPage, const Bridge::PresentationTarget& target,
			std::optional<draw3::uink::UInkGuid>& fileGuid,
			std::uint64_t mutationRevision, std::uint64_t& queuedRevision,
			std::optional<draw3::uink::UInkGuid> clearPageGuid = std::nullopt) -> bool
		{
			if (!observer_.presentationSaveRequested ||
				!ShouldQueuePresentationSave(mutationRevision, queuedRevision)) return false;
			auto request = buildPresentationSaveRequest(document, runtimes,
				currentPage, target, fileGuid, mutationRevision, clearPageGuid);
			if (!request || !observer_.presentationSaveRequested(
				observer_.context, std::move(*request))) return false;
			queuedRevision = mutationRevision;
			return true;
		};

		auto capturePresentationAutoSave = [&]() -> bool
		{
			return activeWorkspace == Bridge::Workspace::Presentation &&
				activePresentationKey && activePresentationTarget && document_ &&
				submitPresentationSlot(*document_, pageRuntimeStates, currentPageIndex_,
					*activePresentationTarget, activePresentationFileGuid,
					activePresentationMutationRevision,
					activePresentationQueuedRevision);
		};

		auto materializeCanvasPage = [&](const draw3::uink::Draw3UInkCanvasSnapshot& source,
			bool importedUndoRoot)
			-> std::optional<std::pair<InkPage, CanvasPageRuntimeState>>
		{
			InkPage page(InkGuid(source.pageGuid.Bytes()));
			InkCanvas* canvas = page.GetOrCreateCanvas(kDefaultDeviceKey,
				{ source.viewport.x, source.viewport.y, source.viewport.scale });
			if (!canvas) return std::nullopt;
			CanvasPageRuntimeState runtime;
			runtime.rasterState = allocateRasterStateToken();
			runtime.intervalOrdinal = source.intervalOrdinal;
			for (const auto& sourceStroke : source.strokes)
			{
				StoredInkType inkType;
				switch (sourceStroke.style.kind)
				{
				case draw3::uink::Draw3UInkStrokeKind::Pen:
					inkType = StoredInkType::Pen; break;
				case draw3::uink::Draw3UInkStrokeKind::Highlighter:
					inkType = StoredInkType::Highlighter; break;
				case draw3::uink::Draw3UInkStrokeKind::Eraser:
					inkType = StoredInkType::Eraser; break;
				case draw3::uink::Draw3UInkStrokeKind::SolidLine:
					inkType = StoredInkType::SolidLine; break;
				case draw3::uink::Draw3UInkStrokeKind::DashedLine:
					inkType = StoredInkType::DashedLine; break;
				case draw3::uink::Draw3UInkStrokeKind::OutlineRectangle:
					inkType = StoredInkType::OutlineRectangle; break;
				case draw3::uink::Draw3UInkStrokeKind::FilledRectangle:
					inkType = StoredInkType::FilledRectangle; break;
				default: return std::nullopt;
				}
				std::vector<StoredInkPoint> points;
				for (const auto& point : sourceStroke.points)
					points.push_back({ point.x, point.y, point.width });
				const auto strokeIndex = canvas->AppendStroke(InkStroke({ inkType,
					sourceStroke.style.fallbackRgb, sourceStroke.style.opacity,
					static_cast<std::uint16_t>(sourceStroke.style.texture) },
					std::move(points)));
				if (!strokeIndex) return std::nullopt;
				const auto footprint = BuildStrokeTileFootprint(
					canvas->Strokes()[*strokeIndex]);
				if (!footprint) return std::nullopt;
				const auto item = runtime.history.AppendStroke(
					*strokeIndex, *footprint, true);
				if (!item || item->index != runtime.beforeStates.size())
					return std::nullopt;
				const InkRasterStateToken before = runtime.rasterState;
				const InkRasterStateToken after = allocateRasterStateToken();
				runtime.beforeStates.push_back(before);
				runtime.afterStates.push_back(after);
				runtime.rasterState = after;
			}
			if (importedUndoRoot) runtime.undoFloor = runtime.history.Items().size();
			return std::pair<InkPage, CanvasPageRuntimeState>(
				std::move(page), std::move(runtime));
		};

		auto materializePresentationSlot = [&](const
			draw3::uink::Draw3UInkExportSnapshot& snapshot,
			const Bridge::PresentationTarget& target,
			std::uint64_t committedRevision) -> std::optional<DrawingDocumentSlot>
		{
			try
			{
				const auto& importedActive = snapshot.activeCanvases.empty()
					? snapshot.canvases : snapshot.activeCanvases;
				std::map<std::int32_t, const draw3::uink::Draw3UInkCanvasSnapshot*> bySlideId;
				for (const auto& source : importedActive)
					if (source.slideId) bySlideId.emplace(*source.slideId, &source);
				std::vector<draw3::uink::Draw3UInkCanvasSnapshot> activeCanvases;
				activeCanvases.reserve(target.totalPages);
				for (std::size_t index = 0; index < target.totalPages; ++index)
				{
					if (target.bindingMode == Bridge::SlideBindingMode::StableSlideId)
					{
						auto found = bySlideId.find(target.slideIds[index]);
						if (found != bySlideId.end()) activeCanvases.push_back(*found->second);
						else
						{
							const auto pageGuid = draw3::uink::CreateUInkGuid();
							if (!pageGuid) return std::nullopt;
							draw3::uink::Draw3UInkCanvasSnapshot blank;
							blank.pageGuid = *pageGuid;
							blank.slideId = target.slideIds[index];
							blank.viewport = { 0.0f, 0.0f, 1.0f };
							activeCanvases.push_back(std::move(blank));
						}
					}
					else
					{
						if (index >= importedActive.size()) return std::nullopt;
						activeCanvases.push_back(importedActive[index]);
					}
				}
				if (activeCanvases.size() != target.totalPages ||
					snapshot.workspaceGuid.IsZero() || snapshot.fileGuid.IsZero())
					return std::nullopt;
				DrawingDocumentSlot slot;
				InkCanvasCollection collection(InkGuid(snapshot.workspaceGuid.Bytes()));
				for (std::size_t pageIndex = 0;
					pageIndex < activeCanvases.size(); ++pageIndex)
				{
					// 按当前放映顺序归一化页码，新插入页也必须带有正确的序号。
					activeCanvases[pageIndex].pageIndex = static_cast<std::uint32_t>(pageIndex);
					activeCanvases[pageIndex].pageNumber = static_cast<std::uint32_t>(pageIndex + 1);
					activeCanvases[pageIndex].retained = false;
					const auto& source = activeCanvases[pageIndex];
					if (source.pageIndex != pageIndex || source.deviceGuid)
						return std::nullopt;
					InkPage page(InkGuid(source.pageGuid.Bytes()));
					InkCanvas* canvas = page.GetOrCreateCanvas(kDefaultDeviceKey,
						{ source.viewport.x, source.viewport.y, source.viewport.scale });
					if (!canvas) return std::nullopt;
					CanvasPageRuntimeState runtime;
					runtime.rasterState = allocateRasterStateToken();
					runtime.intervalOrdinal = source.intervalOrdinal;
					for (const auto& sourceStroke : source.strokes)
					{
						StoredInkType inkType;
						switch (sourceStroke.style.kind)
						{
						case draw3::uink::Draw3UInkStrokeKind::Pen:
							inkType = StoredInkType::Pen; break;
						case draw3::uink::Draw3UInkStrokeKind::Highlighter:
							inkType = StoredInkType::Highlighter; break;
						case draw3::uink::Draw3UInkStrokeKind::Eraser:
							inkType = StoredInkType::Eraser; break;
						case draw3::uink::Draw3UInkStrokeKind::SolidLine:
							inkType = StoredInkType::SolidLine; break;
						case draw3::uink::Draw3UInkStrokeKind::DashedLine:
							inkType = StoredInkType::DashedLine; break;
						case draw3::uink::Draw3UInkStrokeKind::OutlineRectangle:
							inkType = StoredInkType::OutlineRectangle; break;
						case draw3::uink::Draw3UInkStrokeKind::FilledRectangle:
							inkType = StoredInkType::FilledRectangle; break;
						default: return std::nullopt;
						}
						std::vector<StoredInkPoint> points;
						for (const auto& point : sourceStroke.points)
							points.push_back({ point.x, point.y, point.width });
						InkStroke stroke({ inkType, sourceStroke.style.fallbackRgb,
							sourceStroke.style.opacity,
							static_cast<std::uint16_t>(sourceStroke.style.texture) },
							std::move(points));
						const auto strokeIndex = canvas->AppendStroke(std::move(stroke));
						if (!strokeIndex) return std::nullopt;
						const auto footprint = BuildStrokeTileFootprint(
							canvas->Strokes()[*strokeIndex]);
						if (!footprint) return std::nullopt;
						const auto item = runtime.history.AppendStroke(
							*strokeIndex, *footprint, true);
						if (!item || item->index != runtime.beforeStates.size())
							return std::nullopt;
						const InkRasterStateToken before = runtime.rasterState;
						const InkRasterStateToken after = allocateRasterStateToken();
						runtime.beforeStates.push_back(before);
						runtime.afterStates.push_back(after);
						runtime.rasterState = after;
					}
					if (!collection.AppendPage(std::move(page))) return std::nullopt;
					 slot.pageRuntimeStates.push_back(std::move(runtime));
				}
				if (target.bindingMode == Bridge::SlideBindingMode::StableSlideId)
					for (const auto& source : snapshot.retainedCanvases)
					{
						if (!source.retained || !source.slideId || source.deviceGuid) return std::nullopt;
						auto retained = source;
						retained.operations.clear();
						slot.retainedSlides.emplace(*source.slideId, std::move(retained));
					}
				slot.document.emplace(std::move(collection));
				slot.currentPageIndex = target.pageIndex;
				slot.presentationTarget = target;
				slot.fileGuid = snapshot.fileGuid;
				slot.mutationRevision = committedRevision;
				slot.queuedRevision = committedRevision;
				slot.committedRevision = committedRevision;
				slot.persistenceInitialized = true;
			slot.loadPending = false;
				return slot;
			}
			catch (...)
			{
				return std::nullopt;
			}
		};

		auto RebindStablePresentationTopology = [&](const Bridge::PresentationTarget& next)
			-> bool
		{
			if (activeWorkspace != Bridge::Workspace::Presentation ||
				!activePresentationTarget ||
				activePresentationTarget->bindingMode != Bridge::SlideBindingMode::StableSlideId ||
				next.bindingMode != Bridge::SlideBindingMode::StableSlideId || !document_)
				return false;
			auto captured = buildPresentationSaveRequest(*document_, pageRuntimeStates,
				currentPageIndex_, *activePresentationTarget, activePresentationFileGuid,
				activePresentationMutationRevision);
			if (!captured) return false;
			std::map<std::int32_t, draw3::uink::Draw3UInkCanvasSnapshot> known;
			for (const auto& canvas : captured->snapshot.activeCanvases)
				if (canvas.slideId) known.emplace(*canvas.slideId, canvas);
			for (const auto& canvas : captured->snapshot.retainedCanvases)
				if (canvas.slideId) known.emplace(*canvas.slideId, canvas);

			draw3::uink::Draw3UInkExportSnapshot remapped = captured->snapshot;
			remapped.activeCanvases.clear();
			remapped.retainedCanvases.clear();
			remapped.currentPageIndex = next.pageIndex;
			for (const std::int32_t slideId : next.slideIds)
			{
				auto found = known.find(slideId);
				if (found != known.end())
				{
					auto canvas = found->second;
					canvas.retained = false;
					remapped.activeCanvases.push_back(std::move(canvas));
					known.erase(found);
					continue;
				}
				const auto pageGuid = draw3::uink::CreateUInkGuid();
				if (!pageGuid) return false;
				draw3::uink::Draw3UInkCanvasSnapshot blank;
				blank.pageGuid = *pageGuid;
				blank.slideId = slideId;
				blank.viewport = { 0.0f, 0.0f, 1.0f };
				blank.extra = draw3::uink::MakeInkeysBindingExtra(
					draw3::uink::Draw3UInkImportBindingMode::StableSlideId);
				remapped.activeCanvases.push_back(std::move(blank));
			}
			for (auto& [slideId, canvas] : known)
			{
				canvas.slideId = slideId;
				canvas.retained = true;
				remapped.retainedCanvases.push_back(std::move(canvas));
			}
			auto rebuilt = materializePresentationSlot(remapped, next,
				activePresentationCommittedRevision);
			if (!rebuilt || !rebuilt->document) return false;
			std::map<std::array<std::uint8_t, 16>, std::size_t> oldRuntimeByPage;
			for (std::size_t index = 0; index < document_->Pages().size() &&
				index < pageRuntimeStates.size(); ++index)
				if (const InkPage* page = document_->PageAt(index))
					oldRuntimeByPage.emplace(page->PageGuid().Bytes(), index);
			for (std::size_t index = 0; index < rebuilt->document->Pages().size() &&
				index < rebuilt->pageRuntimeStates.size(); ++index)
			{
				const InkPage* page = rebuilt->document->PageAt(index);
				const auto found = page
					? oldRuntimeByPage.find(page->PageGuid().Bytes()) : oldRuntimeByPage.end();
				if (found == oldRuntimeByPage.end()) continue;
				CanvasPageRuntimeState& source = pageRuntimeStates[found->second];
				CanvasPageRuntimeState& destination = rebuilt->pageRuntimeStates[index];
				destination.undoFloor = (std::min)(
					source.undoFloor, destination.history.Items().size());
				destination.intervalOrdinal = source.intervalOrdinal;
				destination.intervalLoadPending = source.intervalLoadPending;
				destination.previousClearUndoAvailable =
					source.previousClearUndoAvailable;
				destination.clearRedoAvailable = source.clearRedoAvailable;
				destination.boundaryFallback = std::move(source.boundaryFallback);
			}
			const auto oldMutation = activePresentationMutationRevision;
			const auto oldQueued = activePresentationQueuedRevision;
			const auto oldCommitted = activePresentationCommittedRevision;
			const auto oldPersistence = activePresentationPersistenceInitialized;
			const auto oldLoadPending = activePresentationLoadPending;
			document_ = std::move(rebuilt->document);
			pageRuntimeStates = std::move(rebuilt->pageRuntimeStates);
			activeRetainedSlides = std::move(rebuilt->retainedSlides);
			activePresentationFileGuid = rebuilt->fileGuid;
			activePresentationMutationRevision = oldMutation;
			activePresentationQueuedRevision = oldQueued;
			activePresentationCommittedRevision = oldCommitted;
			activePresentationPersistenceInitialized = oldPersistence;
			activePresentationLoadPending = oldLoadPending;
			return true;
		};

			auto undoCurrentPage = [&](RECT& frameDirty) -> bool
		{
			const auto requestAuthoritativeRecovery = [&]()
			{
				viewportVisibleClear = false;
				viewportRefreshPending = true;
				viewportRefreshClearsTransient = false;
			};
			if (!document_ || currentPageIndex_ >= pageRuntimeStates.size())
			{
				std::cout << "[Undo] result=noop reason=no_canvas" << std::endl;
				return false;
			}
			InkPage* page = document_->PageAt(currentPageIndex_);
			InkCanvas* canvas = page
				? page->FindCanvas(kDefaultDeviceKey) : nullptr;
			CanvasPageRuntimeState& runtime = pageRuntimeStates[currentPageIndex_];
			const std::optional<RenderItemId> itemId = runtime.history.LastVisibleItem();
			if (!page || !canvas || !itemId || itemId->index < runtime.undoFloor)
			{
				std::cout << "[Undo] page=" << (currentPageIndex_ + 1) <<
					" result=noop reason=empty" << std::endl;
				return false;
			}
			const RenderItemState* item = runtime.history.Find(*itemId);
			if (!item || itemId->index >= runtime.beforeStates.size())
			{
				std::cout << "[Undo] page=" << (currentPageIndex_ + 1) <<
					" result=noop reason=history_mismatch" << std::endl;
				return false;
			}
			const std::vector<SignedTileCoordinate> affectedTiles = item->compositionTiles;
			const size_t restoreRangeEnd = static_cast<size_t>(itemId->index) + 1;
			const HistoryCanvasIdentity canvasIdentity = {
				page->PageGuid(), kDefaultDeviceKey };
			const InkHistoryRasterKey rasterKey = currentRasterKey();
			const WindowSize size = window_.Size();
			const InkViewport viewport = canvas->Viewport();
			const HotPreimageRestoreResult hotRestore = historyGpuCache.RestorePreimage(
				canvasIdentity, *itemId, rasterKey, runtime.rasterState,
				viewport.x, viewport.y, size.width, size.height);
			const char* path = "failed";
			RECT dirty = {};
			if (hotRestore.restored)
			{
				if (!runtime.history.UndoLastVisible(*itemId))
				{
					requestAuthoritativeRecovery();
					std::cout << "[Undo] page=" << (currentPageIndex_ + 1) <<
						" result=failed reason=visibility" << std::endl;
					return false;
				}
				runtime.rasterState = hotRestore.restoredState;
				dirty = hotRestore.dirty;
				path = "hot_preimage";
			}
			else
			{
				const InkRasterStateToken beforeState =
					runtime.beforeStates[itemId->index];
				const auto restoreOriginalTiles = [&]()
				{
					const CompositionRestoreRequest rollbackRequest = {
						canvasIdentity,
						rasterKey,
						canvas,
						&runtime.history,
						affectedTiles,
						restoreRangeEnd,
						viewport.x,
						viewport.y,
						size.width,
						size.height,
						true
					};
					return historyGpuCache.RestoreComposition(rollbackRequest);
				};
				const CompositionRestoreRequest request = {
					canvasIdentity,
					rasterKey,
					canvas,
					&runtime.history,
					affectedTiles,
					restoreRangeEnd,
					viewport.x,
					viewport.y,
					size.width,
					size.height,
					true,
					*itemId
				};
				const CompositionRestoreResult restored =
					historyGpuCache.RestoreComposition(request);
				if (restored.path == CompositionRestorePath::Failed)
				{
					const CompositionRestoreResult rollback = restoreOriginalTiles();
					requestAuthoritativeRecovery();
					UnionRectInPlace(frameDirty, restored.dirty);
					UnionRectInPlace(frameDirty, rollback.dirty);
					std::cout << "[Undo] page=" << (currentPageIndex_ + 1) <<
						" item=" << itemId->index <<
						" result=failed reason=restore rollback=" <<
						CompositionRestorePathName(rollback.path) << std::endl;
					return false;
				}
				// 候选画面成功后才提交 visibility，避免失败时丢失历史状态。
				if (!runtime.history.UndoLastVisible(*itemId))
				{
					const CompositionRestoreResult rollback = restoreOriginalTiles();
					requestAuthoritativeRecovery();
					UnionRectInPlace(frameDirty, restored.dirty);
					UnionRectInPlace(frameDirty, rollback.dirty);
					std::cout << "[Undo] page=" << (currentPageIndex_ + 1) <<
						" result=failed reason=visibility rollback=" <<
						CompositionRestorePathName(rollback.path) << std::endl;
					return false;
				}
				runtime.rasterState = beforeState;
				dirty = restored.dirty;
				path = CompositionRestorePathName(restored.path);
			}
			renderer_.ClearOperatorLayer(renderer_.layerL1);
			renderer_.ClearOperatorLayer(renderer_.layerL0);
			UnionRectInPlace(frameDirty, dirty);
			const size_t hotRemaining = historyGpuCache.ConsecutiveHotDepth(
				canvasIdentity, rasterKey, runtime.rasterState);
			const bool historyEnd = !runtime.history.LastVisibleItem().has_value();
			std::cout << "[Undo] page=" << (currentPageIndex_ + 1) <<
				" item=" << itemId->index << " path=" << path <<
				" hot_remaining=" << hotRemaining;
			if (historyEnd) std::cout << " history_end=true";
			std::cout << std::endl;
			return true;
		};

		auto redoCurrentPage = [&](RECT& frameDirty) -> bool
		{
			const auto requestAuthoritativeRecovery = [&]()
			{
				viewportVisibleClear = false;
				viewportRefreshPending = true;
				viewportRefreshClearsTransient = false;
			};
			if (!document_ || currentPageIndex_ >= pageRuntimeStates.size())
			{
				std::cout << "[Redo] result=noop reason=no_canvas" << std::endl;
				return false;
			}
			InkPage* page = document_->PageAt(currentPageIndex_);
			InkCanvas* canvas = page
				? page->FindCanvas(kDefaultDeviceKey) : nullptr;
			CanvasPageRuntimeState& runtime = pageRuntimeStates[currentPageIndex_];
			const std::optional<RenderItemId> itemId = runtime.history.LastRedoItem();
			if (!page || !canvas || !itemId)
			{
				std::cout << "[Redo] page=" << (currentPageIndex_ + 1) <<
					" result=noop reason=empty" << std::endl;
				return false;
			}

			const RenderItemState* item = runtime.history.Find(*itemId);
			const std::span<const InkStroke> strokes = canvas->Strokes();
			if (!item || itemId->index >= runtime.beforeStates.size() ||
				itemId->index >= runtime.afterStates.size() ||
				item->strokeIndex >= strokes.size())
			{
				std::cout << "[Redo] page=" << (currentPageIndex_ + 1) <<
					" result=noop reason=history_mismatch" << std::endl;
				return false;
			}
			const InkRasterStateToken beforeState = runtime.beforeStates[itemId->index];
			const InkRasterStateToken afterState = runtime.afterStates[itemId->index];
			if (runtime.rasterState != beforeState)
			{
				requestAuthoritativeRecovery();
				std::cout << "[Redo] page=" << (currentPageIndex_ + 1) <<
					" item=" << itemId->index <<
					" result=failed reason=raster_state" << std::endl;
				return false;
			}

			const std::vector<SignedTileCoordinate> affectedTiles = item->compositionTiles;
			const HistoryCanvasIdentity canvasIdentity = {
				page->PageGuid(), kDefaultDeviceKey };
			const InkHistoryRasterKey rasterKey = currentRasterKey();
			const WindowSize size = window_.Size();
			const InkViewport viewport = canvas->Viewport();
			const auto restoreHiddenTiles = [&]()
			{
				const CompositionRestoreRequest request = {
					canvasIdentity,
					rasterKey,
					canvas,
					&runtime.history,
					affectedTiles,
					runtime.history.Items().size(),
					viewport.x,
					viewport.y,
					size.width,
					size.height,
					true
				};
				return historyGpuCache.RestoreComposition(request);
			};

			const char* basePath = "trusted_l2";
			RECT dirty = {};
			if (!viewportVisibleClear)
			{
				// 动态恢复未完成时，先把候选下方的隐藏态背景补成权威 L2。
				const CompositionRestoreResult restored = restoreHiddenTiles();
				UnionRectInPlace(dirty, restored.dirty);
				basePath = CompositionRestorePathName(restored.path);
				if (restored.path == CompositionRestorePath::Failed)
				{
					requestAuthoritativeRecovery();
					UnionRectInPlace(frameDirty, dirty);
					std::cout << "[Redo] page=" << (currentPageIndex_ + 1) <<
						" item=" << itemId->index <<
						" result=failed reason=base_restore" << std::endl;
					return false;
				}
			}

			renderer_.ClearOperatorLayer(renderer_.layerL1);
			renderer_.ClearOperatorLayer(renderer_.layerL0);
			const StoredStrokeRasterTarget target = {
				&renderer_.layerL1, viewport.x, viewport.y, size.width, size.height
			};
			const StoredStrokeRasterResult raster = DrawStoredStroke(
				strokes[item->strokeIndex], renderer_, target,
				redoRebuildPoints, redoHighlighterScratch);
			const RECT redoDirty = ClampRectToCanvas(
				raster.dirty, size.width, size.height);
			if (!raster.succeeded)
			{
				renderer_.ClearOperatorLayer(renderer_.layerL1);
				renderer_.ClearOperatorLayer(renderer_.layerL0);
				UnionRectInPlace(frameDirty, dirty);
				std::cout << "[Redo] page=" << (currentPageIndex_ + 1) <<
					" item=" << itemId->index << " base=" << basePath <<
					" result=failed reason=raster" << std::endl;
				return false;
			}

			const HotPreimageCaptureResult preimageCapture =
				historyGpuCache.CapturePreimage({
					canvasIdentity,
					*itemId,
					rasterKey,
					beforeState,
					afterState,
					item->undoTiles,
					viewport.x,
					viewport.y,
					size.width,
					size.height
				});
			bool submitted = true;
			if (!IsEmptyRect(redoDirty))
			{
				submitted = renderer_.ApplyOperatorLayers(renderer_.layerL2RTV.Get(),
					renderer_.layerL1, renderer_.layerL0, redoDirty);
			}

			const auto rollbackRedoPixels = [&]()
			{
				const CompositionRestoreResult rollback = restoreHiddenTiles();
				UnionRectInPlace(dirty, redoDirty);
				UnionRectInPlace(dirty, rollback.dirty);
				if (rollback.path == CompositionRestorePath::Failed)
					requestAuthoritativeRecovery();
				return rollback.path;
			};
			if (!submitted)
			{
				if (preimageCapture.status == HotPreimageCaptureStatus::Captured)
					historyGpuCache.CancelPreimage(preimageCapture.ticket);
				renderer_.ClearOperatorLayer(renderer_.layerL1);
				renderer_.ClearOperatorLayer(renderer_.layerL0);
				const CompositionRestorePath rollbackPath = rollbackRedoPixels();
				UnionRectInPlace(frameDirty, dirty);
				std::cout << "[Redo] page=" << (currentPageIndex_ + 1) <<
					" item=" << itemId->index << " base=" << basePath <<
					" result=failed reason=resolve rollback=" <<
					CompositionRestorePathName(rollbackPath) << std::endl;
				return false;
			}

			// GPU 画面成功后才提交 visibility；失败仍可再次按 6 重试。
			if (!runtime.history.RedoLastUndone(*itemId))
			{
				if (preimageCapture.status == HotPreimageCaptureStatus::Captured)
					historyGpuCache.CancelPreimage(preimageCapture.ticket);
				renderer_.ClearOperatorLayer(renderer_.layerL1);
				renderer_.ClearOperatorLayer(renderer_.layerL0);
				const CompositionRestorePath rollbackPath = rollbackRedoPixels();
				UnionRectInPlace(frameDirty, dirty);
				std::cout << "[Redo] page=" << (currentPageIndex_ + 1) <<
					" item=" << itemId->index << " base=" << basePath <<
					" result=failed reason=visibility rollback=" <<
					CompositionRestorePathName(rollbackPath) << std::endl;
				return false;
			}

			runtime.rasterState = afterState;
			bool hotRearmed = false;
			if (preimageCapture.status == HotPreimageCaptureStatus::Captured)
			{
				hotRearmed = historyGpuCache.CommitPreimage(preimageCapture.ticket);
				if (!hotRearmed)
					historyGpuCache.CancelPreimage(preimageCapture.ticket);
			}
			renderer_.ClearOperatorLayer(renderer_.layerL1);
			renderer_.ClearOperatorLayer(renderer_.layerL0);
			UnionRectInPlace(dirty, redoDirty);
			UnionRectInPlace(frameDirty, dirty);
			viewportVisibleClear = CanvasVisibleClarityAfterAuthoritativeWrite(
				viewportVisibleClear, true);
			std::cout << "[Redo] page=" << (currentPageIndex_ + 1) <<
				" item=" << itemId->index << " base=" << basePath <<
				" path=direct_draw hot_rearmed=" << (hotRearmed ? "true" : "false") <<
				" redo_remaining=" << runtime.history.RedoDepth() << std::endl;
			return true;
		};

			auto clearCurrentPage = [&](RECT& frameDirty,
			LaserParticleDirtySnapshot& particleSnapshot,
			bool& forceFullPresent, int width, int height) -> bool
		{
			if (!document_ || currentPageIndex_ >= pageRuntimeStates.size() ||
				!currentPageHasContent())
				return false;
			InkPage* page = document_->PageAt(currentPageIndex_);
			InkCanvas* canvas = page
				? page->FindCanvas(kDefaultDeviceKey) : nullptr;
			if (!canvas) return false;

			// Clear 截断当前页全部文档与历史；viewport 留在 Canvas 对象中。
			const std::uint32_t previousInterval =
				pageRuntimeStates[currentPageIndex_].intervalOrdinal;
			if (activeWorkspace == Bridge::Workspace::Presentation &&
				previousInterval == UINT32_MAX) return false;
			canvas->ClearStrokes();
			CanvasPageRuntimeState freshRuntime;
			freshRuntime.rasterState = allocateRasterStateToken();
			if (activeWorkspace == Bridge::Workspace::Presentation)
				freshRuntime.intervalOrdinal = previousInterval + 1;
			pageRuntimeStates[currentPageIndex_] = std::move(freshRuntime);

			historyGpuCache.DiscardHotPreimages();
			historyGpuCache.DiscardCompositionCache();
			compositionMaintenance.clear();
			if (rasterPipelineGeneration ==
				(std::numeric_limits<uint64_t>::max)())
				rasterPipelineGeneration = 1;
			else ++rasterPipelineGeneration;

			renderer_.InvalidateTrustedL2Snapshot();
			trustedSnapshotSignatureValid = false;
			viewportTilePlan = {};
			viewportTilePlanIndex = 0;
			viewportRecoveryPending = false;
			viewportRefreshPending = false;
			viewportRefreshClearsTransient = false;
			viewportVisibleClear = true;
			pendingLaserBakeDirty = {};
			laserTipVisuals.clear();
			laserParticleEmissionRequests.clear();
			previousCursorVisuals.clear();
			currentCursorVisuals.clear();
			laserIncrementalEnsureAttempted = false;
			resetGpuForPageSwitch(frameDirty, particleSnapshot,
				forceFullPresent, width, height);
			publishCurrentPageContent();
			return true;
		};

		auto restoreCurrentPageFromSnapshot = [&](const
			draw3::uink::Draw3UInkCanvasSnapshot& snapshot,
			LaserParticleDirtySnapshot& particleSnapshot,
			RECT& frameDirty, bool& forceFullPresent, int width, int height) -> bool
		{
			if (!document_ || currentPageIndex_ >= pageRuntimeStates.size()) return false;
			const InkPage* current = document_->PageAt(currentPageIndex_);
			if (!current || current->PageGuid().Bytes() != snapshot.pageGuid.Bytes())
				return false;
			auto materialized = materializeCanvasPage(snapshot, false);
			if (!materialized || !document_->ReplacePage(
				currentPageIndex_, std::move(materialized->first))) return false;
			pageRuntimeStates[currentPageIndex_] = std::move(materialized->second);
			// 恢复画布成为新的历史根；后续保存同步截断更早 Clear 区间。
			pageRuntimeStates[currentPageIndex_].intervalOrdinal = 0;
			pageRuntimeStates[currentPageIndex_].previousClearUndoAvailable = false;
			pageRuntimeStates[currentPageIndex_].clearRedoAvailable = true;
			restoreAfterDocumentSlotSwitch(frameDirty, particleSnapshot,
				forceFullPresent, width, height);
			return true;
		};

		auto processCanvasCommands = [&](RECT& frameDirty,
			LaserParticleDirtySnapshot& particleSnapshot,
			bool& forceFullPresent, int width, int height)
		{
			auto reportCommand = [&](CanvasCommandType type)
			{
				if (!observer_.commandProcessed) return;
				observer_.commandProcessed(observer_.context, type, currentPageIndex_,
					document_ ? document_->Pages().size() : 0);
			};
			CanvasCommand command;
			while (active.empty() && window_.TryDequeueCanvasCommand(command))
			{
				if (command.type == CanvasCommandType::DesktopPersistenceCompleted)
				{
					if (!command.desktopPersistenceCompletion || !desktopClearRecovery ||
						!desktopClearRecovery->fileGuid ||
						*desktopClearRecovery->fileGuid !=
							command.desktopPersistenceCompletion->fileGuid) continue;
					const auto& completion = *command.desktopPersistenceCompletion;
					if (completion.operation == DesktopPersistenceOperation::Save)
					{
						if (completion.trigger == DesktopAutoSaveTrigger::Clear &&
							completion.status == DesktopPersistenceStatus::Committed)
						{
							desktopClearRecovery->diskReady = true;
							// durable 后立即释放 Clear 前的 CPU 墨迹快照。
							desktopClearRecovery->canvas.reset();
						}
						else desktopClearRecovery->diskSaveRequested = false;
						continue;
					}
					desktopClearRecovery->loadPending = false;
					if (completion.status == DesktopPersistenceStatus::Loaded &&
						completion.loadedSnapshot &&
						completion.loadedSnapshot->canvases.size() == 1 &&
						restoreCurrentPageFromSnapshot(
							completion.loadedSnapshot->canvases.front(), particleSnapshot,
							frameDirty, forceFullPresent, width, height))
					{
						desktopClearRecovery.reset();
						publishCurrentPageContent();
					}
					continue;
				}
				if (command.type == CanvasCommandType::PresentationPersistenceCompleted)
				{
					if (!command.presentationPersistenceCompletion) continue;
					const auto& completion = *command.presentationPersistenceCompletion;
					const std::size_t activePageCount = document_
						? document_->Pages().size() : 0;
					const bool completionIsActive = activeWorkspace ==
						Bridge::Workspace::Presentation && activePresentationKey &&
						*activePresentationKey == completion.target.key &&
						activePresentationTarget && CanReusePresentationDocumentSlot(
							completion.target, *activePresentationTarget, activePageCount);
					DrawingDocumentSlot* parked = nullptr;
					if (!completionIsActive)
					{
						auto found = presentationSlots.find(completion.target.key);
						if (found != presentationSlots.end() &&
							found->second.document && found->second.presentationTarget &&
							CanReusePresentationDocumentSlot(completion.target,
								*found->second.presentationTarget,
								found->second.document->Pages().size()))
							parked = &found->second;
					}

					if (completion.operation == PresentationPersistenceOperation::Save)
					{
						auto releaseBoundaryFallback = [&](InkCanvasCollection* document,
							std::vector<CanvasPageRuntimeState>* runtimes)
						{
							if (!completion.clearPageGuid || !document || !runtimes) return;
							for (std::size_t index = 0; index < document->Pages().size() &&
								index < runtimes->size(); ++index)
								if (const InkPage* page = document->PageAt(index);
									page && page->PageGuid().Bytes() ==
										completion.clearPageGuid->Bytes() &&
									completion.clearIntervalOrdinal &&
									ShouldReleasePresentationClearFallback(
										(*runtimes)[index].intervalOrdinal,
										*completion.clearIntervalOrdinal))
									(*runtimes)[index].boundaryFallback.reset();
						};
						if (completionIsActive)
						{
							if (completion.status == PresentationPersistenceStatus::Committed)
							{
								activePresentationCommittedRevision = (std::max)(
									activePresentationCommittedRevision,
									completion.mutationRevision);
								activePresentationPersistenceInitialized = true;
								releaseBoundaryFallback(document_ ? &*document_ : nullptr,
									&pageRuntimeStates);
							}
							else if (activePresentationQueuedRevision ==
								completion.mutationRevision)
								activePresentationQueuedRevision =
									activePresentationCommittedRevision;
						}
						else if (parked)
						{
							if (completion.status == PresentationPersistenceStatus::Committed)
							{
								parked->committedRevision = (std::max)(
									parked->committedRevision, completion.mutationRevision);
								parked->persistenceInitialized = true;
								releaseBoundaryFallback(parked->document
									? &*parked->document : nullptr,
									&parked->pageRuntimeStates);
							}
							else if (parked->queuedRevision == completion.mutationRevision)
								parked->queuedRevision = parked->committedRevision;
							if (canEvictPresentationSlot(*parked))
								presentationSlots.erase(completion.target.key);
						}
						continue;
					}

					if (completion.loadKind == PresentationLoadKind::PreviousInterval)
					{
						auto installInterval = [&](InkCanvasCollection* targetDocument,
							std::vector<CanvasPageRuntimeState>* runtimes) -> std::optional<std::size_t>
						{
							if (completion.status != PresentationPersistenceStatus::Loaded ||
								!completion.loadedSnapshot || !completion.pageGuid ||
								!targetDocument || !runtimes) return std::nullopt;
							const auto& candidates = completion.loadedSnapshot->activeCanvases.empty()
								? completion.loadedSnapshot->canvases
								: completion.loadedSnapshot->activeCanvases;
							const draw3::uink::Draw3UInkCanvasSnapshot* source = nullptr;
							for (const auto& canvas : candidates)
								if (canvas.pageGuid == *completion.pageGuid) source = &canvas;
							if (!source) return std::nullopt;
							for (std::size_t index = 0; index < targetDocument->Pages().size() &&
								index < runtimes->size(); ++index)
							{
								const InkPage* page = targetDocument->PageAt(index);
								if (!page || page->PageGuid().Bytes() != completion.pageGuid->Bytes())
									continue;
								CanvasPageRuntimeState& pending = (*runtimes)[index];
								if (!pending.intervalLoadPending || pending.intervalOrdinal == 0 ||
									completion.intervalOrdinal + 1 != pending.intervalOrdinal)
									return std::nullopt;
								auto materialized = materializeCanvasPage(*source, false);
								if (!materialized || !targetDocument->ReplacePage(
									index, std::move(materialized->first))) return std::nullopt;
								(*runtimes)[index] = std::move(materialized->second);
								(*runtimes)[index].intervalOrdinal = 0;
								(*runtimes)[index].previousClearUndoAvailable = false;
								(*runtimes)[index].clearRedoAvailable = true;
								return index;
							}
							return std::nullopt;
						};
						if (completionIsActive)
						{
							activePresentationLoadPending = false;
							const auto installed = installInterval(
								document_ ? &*document_ : nullptr, &pageRuntimeStates);
							if (!installed && completion.pageGuid && document_)
								for (std::size_t index = 0; index < document_->Pages().size() &&
									index < pageRuntimeStates.size(); ++index)
									if (const InkPage* page = document_->PageAt(index);
										page && page->PageGuid().Bytes() == completion.pageGuid->Bytes())
										pageRuntimeStates[index].intervalLoadPending = false;
							if (installed)
							{
								markPresentationMutation();
								(void)capturePresentationAutoSave();
								if (*installed == currentPageIndex_)
									restoreAfterDocumentSlotSwitch(frameDirty, particleSnapshot,
										forceFullPresent, width, height);
							}
						}
						else if (parked)
						{
							parked->loadPending = false;
							const auto installed = installInterval(
								parked->document ? &*parked->document : nullptr,
								&parked->pageRuntimeStates);
							if (!installed && completion.pageGuid && parked->document)
								for (std::size_t index = 0; index < parked->document->Pages().size() &&
									index < parked->pageRuntimeStates.size(); ++index)
									if (const InkPage* page = parked->document->PageAt(index);
										page && page->PageGuid().Bytes() == completion.pageGuid->Bytes())
										parked->pageRuntimeStates[index].intervalLoadPending = false;
							if (installed && parked->presentationTarget && parked->document)
							{
								parked->mutationRevision = AdvancePresentationMutationRevision(
									parked->mutationRevision);
								(void)submitPresentationSlot(*parked->document,
									parked->pageRuntimeStates, parked->currentPageIndex,
									*parked->presentationTarget, parked->fileGuid,
									parked->mutationRevision, parked->queuedRevision);
							}
						}
						continue;
					}

					if (!completionIsActive && !parked) continue;
					Bridge::PresentationTarget latestTarget = completionIsActive &&
						activePresentationTarget ? *activePresentationTarget :
						parked && parked->presentationTarget ? *parked->presentationTarget :
						completion.target;
					std::optional<DrawingDocumentSlot> loaded;
					const bool loadedBindingMigration = completion.loadedSnapshot &&
						ShouldPersistLoadedPresentationBindingMigration(
							latestTarget.bindingMode,
							completion.loadedSnapshot->workspaceType);
					if (completion.status == PresentationPersistenceStatus::Loaded &&
						completion.loadedSnapshot)
						loaded = materializePresentationSlot(*completion.loadedSnapshot,
							latestTarget, completion.mutationRevision);

					if (completionIsActive)
					{
						activePresentationLoadPending = false;
						activePresentationPersistenceInitialized = true;
						bool installedLoadedDocument = false;
						if (loaded && activePresentationMutationRevision == 0)
						{
							document_ = std::move(loaded->document);
							pageRuntimeStates = std::move(loaded->pageRuntimeStates);
							currentPageIndex_ = latestTarget.pageIndex;
							activePresentationTarget = latestTarget;
							activePresentationFileGuid = loaded->fileGuid;
							activePresentationMutationRevision = loaded->mutationRevision;
							activePresentationQueuedRevision = loaded->queuedRevision;
							activePresentationCommittedRevision = loaded->committedRevision;
							installedLoadedDocument = true;
						}
						if (installedLoadedDocument && loadedBindingMigration)
						{
							// 冷加载到旧 fallback 文件后，稳定 SlideID 身份本身也是持久化修改。
							markPresentationMutation();
							(void)capturePresentationAutoSave();
						}
						restoreAfterDocumentSlotSwitch(frameDirty, particleSnapshot,
							forceFullPresent, width, height);
						if (activePresentationTarget)
							stageWorkspaceReady(&*activePresentationTarget);
					}
					else
					{
						parked->loadPending = false;
						parked->persistenceInitialized = true;
						const bool installedLoadedDocument = loaded &&
							parked->mutationRevision == 0;
						if (installedLoadedDocument)
							*parked = std::move(*loaded);
						if (installedLoadedDocument && loadedBindingMigration &&
							parked->document && parked->presentationTarget)
						{
							parked->mutationRevision = AdvancePresentationMutationRevision(
								parked->mutationRevision);
							(void)submitPresentationSlot(*parked->document,
								parked->pageRuntimeStates, parked->currentPageIndex,
								*parked->presentationTarget, parked->fileGuid,
								parked->mutationRevision, parked->queuedRevision);
						}
						if (canEvictPresentationSlot(*parked))
							presentationSlots.erase(completion.target.key);
					}
					continue;
				}
				if (command.type == CanvasCommandType::SetPresentationTarget)
				{
					if (!command.presentationTarget) continue;
					const Bridge::PresentationTarget target = *command.presentationTarget;
					const bool stableValid = target.bindingMode !=
						Bridge::SlideBindingMode::StableSlideId ||
						(target.slideId && target.slideIds.size() == target.totalPages &&
							target.pageIndex < target.slideIds.size() &&
							target.slideIds[target.pageIndex] == *target.slideId);
					if (target.key.IsZero() || target.totalPages == 0 ||
						target.totalPages > Bridge::kMaximumPresentationPages ||
						target.pageIndex >= target.totalPages || !stableValid) continue;
					// 同文稿翻页与跨文稿切换都先固定离开侧最新 revision。
					(void)capturePresentationAutoSave();
					pendingWorkspaceReady.reset();

					const bool sameKey = activeWorkspace == Bridge::Workspace::Presentation &&
						activePresentationKey && *activePresentationKey == target.key;
					const std::optional<Bridge::PresentationKey> previousPresentationKey =
						activePresentationKey;
					bool topologyConflict = false;
					if (sameKey && activePresentationTarget)
						topologyConflict = !CanReusePresentationDocumentSlot(
							*activePresentationTarget, target,
							document_ ? document_->Pages().size() : 0);

					if (!sameKey || topologyConflict)
					{
						DrawingDocumentSlot* source = parkedActiveSlot();
						if (!source) continue;
						swapActiveDocument(*source);

						DrawingDocumentSlot* destination = nullptr;
						if (!topologyConflict)
						{
							auto [found, inserted] = presentationSlots.try_emplace(target.key);
							(void)inserted;
							destination = &found->second;
							if (destination->presentationTarget &&
								!CanReusePresentationDocumentSlot(
									*destination->presentationTarget, target,
									destination->document
										? destination->document->Pages().size()
										: target.totalPages))
							{
								topologyConflict = true;
								destination = nullptr;
							}
						}
						if (topologyConflict)
						{
							isolatedPresentationSlot = {};
							destination = &isolatedPresentationSlot;
							std::fputs("[Draw3.Presentation] action=switch result=isolated reason=topology_conflict\n",
								stderr);
						}
						if (!createBlankSlot(*destination, target.totalPages))
						{
							// 创建失败时把来源槽恢复为 active，不能留下空 document。
							swapActiveDocument(*source);
							continue;
						}
						swapActiveDocument(*destination);
						if (previousPresentationKey && canEvictPresentationSlot(*source))
							presentationSlots.erase(*previousPresentationKey);
						activePresentationKey = topologyConflict
							? std::nullopt : std::optional<Bridge::PresentationKey>(target.key);
					}
					activeWorkspace = Bridge::Workspace::Presentation;
					if (topologyConflict) activePresentationTarget.reset();
					else
					{
						const bool bindingUpgrade = activePresentationTarget &&
							CanUpgradePresentationBindingByOrdinal(*activePresentationTarget,
								target, document_ ? document_->Pages().size() : 0);
						// parked/warm slot 先恢复其旧 target，再按有序 SlideID 重映射。
						const bool stableTopologyChange = activePresentationTarget &&
							StablePresentationTopologyChanged(*activePresentationTarget, target);
						if (stableTopologyChange && !RebindStablePresentationTopology(target))
						{
							std::fputs("[Draw3.Presentation] action=rebind result=failed\n", stderr);
							continue;
						}
						activePresentationTarget = target;
						if (bindingUpgrade && (activePresentationMutationRevision != 0 ||
							activePresentationFileGuid)) markPresentationMutation();
					}
					if (!document_ || pageRuntimeStates.size() != target.totalPages ||
						document_->Pages().size() != target.totalPages)
					{
						std::fputs("[Draw3.Presentation] action=switch result=isolated reason=page_count\n",
							stderr);
						continue;
					}
					currentPageIndex_ = target.pageIndex;
					restoreAfterDocumentSlotSwitch(frameDirty, particleSnapshot,
						forceFullPresent, width, height);
					if (activePresentationTarget &&
						!activePresentationPersistenceInitialized &&
						!activePresentationLoadPending &&
						activePresentationMutationRevision == 0 &&
						activePresentationQueuedRevision == 0 &&
						observer_.presentationLoadRequested)
					{
						PresentationLoadRequest load;
						load.target = *activePresentationTarget;
						activePresentationLoadPending =
							observer_.presentationLoadRequested(
								observer_.context, std::move(load));
						if (!activePresentationLoadPending)
							activePresentationPersistenceInitialized = true;
					}
					if (!activePresentationLoadPending)
						(void)capturePresentationAutoSave();
					if (!activePresentationLoadPending)
						stageWorkspaceReady(activePresentationTarget
							? &*activePresentationTarget : nullptr);
					continue;
				}
				if (command.type == CanvasCommandType::SetWorkspace)
				{
					const auto target = static_cast<Bridge::Workspace>(command.workspace);
					if ((target != Bridge::Workspace::Desktop &&
						target != Bridge::Workspace::Presentation &&
						target != Bridge::Workspace::Whiteboard) ||
						(target == activeWorkspace && (target != Bridge::Workspace::Presentation ||
							!activePresentationKey)))
						continue;
					(void)capturePresentationAutoSave();
					pendingWorkspaceReady.reset();
					const std::optional<Bridge::PresentationKey> previousPresentationKey =
						activePresentationKey;
					DrawingDocumentSlot* destination = target == Bridge::Workspace::Desktop
						? &desktopSlot : target == Bridge::Workspace::Whiteboard
						? &whiteboardSlot : &isolatedPresentationSlot;
					if (target == Bridge::Workspace::Presentation)
						isolatedPresentationSlot = {};
					if (!createBlankSlot(*destination, 1)) continue;
					DrawingDocumentSlot* source = parkedActiveSlot();
					if (!source) continue;
					swapActiveDocument(*source);
					swapActiveDocument(*destination);
					if (previousPresentationKey && canEvictPresentationSlot(*source))
						presentationSlots.erase(*previousPresentationKey);
					activeWorkspace = target;
					activePresentationKey.reset();
					activePresentationTarget.reset();
					restoreAfterDocumentSlotSwitch(frameDirty, particleSnapshot,
						forceFullPresent, width, height);
					stageWorkspaceReady();
					continue;
				}
				if (Bridge::PresentationCanvasCommandSuppressed(activeWorkspace,
					activePresentationLoadPending,
					command.type == CanvasCommandType::PrepareExitAutoSave))
				{
					// 恢复未决时，破坏性画布命令与 physical contact 使用同一输入闸门。
					reportCommand(command.type);
					continue;
				}
				// 页面、撤回/重做和键盘平移都以命令时刻的固定视口为起点。
				interruptNavigationForPenOrMouse("canvas-command");
				const bool redoClear = command.type == CanvasCommandType::Redo &&
					currentPageIndex_ < pageRuntimeStates.size() &&
					pageRuntimeStates[currentPageIndex_].clearRedoAvailable;
				// 重做直接复用唯一Clear事务，包括快照、当前页边界与保存请求。
				if (command.type == CanvasCommandType::Clear || redoClear)
				{
					std::optional<draw3::uink::Draw3UInkCanvasSnapshot> preClear;
					if (document_ && currentPageIndex_ < pageRuntimeStates.size())
						if (const InkPage* page = document_->PageAt(currentPageIndex_))
							preClear = captureCanvasTail(*page,
								pageRuntimeStates[currentPageIndex_],
								static_cast<std::uint32_t>(currentPageIndex_),
								activePresentationTarget && activePresentationTarget->slideId
									? activePresentationTarget->slideId : std::nullopt);
					if (currentPageHasContent() && !preClear)
					{
						std::fputs("[Draw3.Clear] result=failed reason=snapshot\n", stderr);
						reportCommand(command.type);
						continue;
					}
					bool boundaryQueued = false;
					bool desktopDiskRequested = false;
					std::optional<draw3::uink::UInkGuid> desktopFileGuid;
					if (preClear && !preClear->strokes.empty() &&
						activeWorkspace == Bridge::Workspace::Presentation &&
						activePresentationTarget && document_)
					{
						markPresentationMutation();
						boundaryQueued = submitPresentationSlot(*document_,
							pageRuntimeStates, currentPageIndex_, *activePresentationTarget,
							activePresentationFileGuid, activePresentationMutationRevision,
							activePresentationQueuedRevision, preClear->pageGuid);
					}
					else if (preClear && !preClear->strokes.empty() &&
						activeWorkspace == Bridge::Workspace::Desktop)
						desktopDiskRequested = captureDesktopAutoSave(
							DesktopAutoSaveTrigger::Clear, &desktopFileGuid);
					const bool cleared = clearCurrentPage(frameDirty, particleSnapshot,
						forceFullPresent, width, height);
					if (cleared && activeWorkspace == Bridge::Workspace::Desktop)
					{
						// 第二次有效 Clear 会替换更老的恢复点。
						desktopClearRecovery = DesktopClearRecovery{
							std::move(*preClear), desktopFileGuid,
							desktopDiskRequested, false, false };
						desktopAutoSavePolicy.CompleteDesktopClear();
					}
					else if (cleared && (activeWorkspace == Bridge::Workspace::Presentation ||
						activeWorkspace == Bridge::Workspace::Whiteboard) &&
						preClear && currentPageIndex_ < pageRuntimeStates.size())
						pageRuntimeStates[currentPageIndex_].boundaryFallback =
							std::move(*preClear);
					(void)boundaryQueued;
					reportCommand(command.type);
					continue;
				}
				if (command.type == CanvasCommandType::PrepareExitAutoSave)
				{
					// 回执是退出排空屏障：此时最终快照已经同步进入后台保存队列。
					(void)captureDesktopAutoSave(DesktopAutoSaveTrigger::Exit);
					(void)capturePresentationAutoSave();
					for (auto& [key, slot] : presentationSlots)
					{
						(void)key;
						if (!slot.document || !slot.presentationTarget) continue;
						(void)submitPresentationSlot(*slot.document,
							slot.pageRuntimeStates, slot.currentPageIndex,
							*slot.presentationTarget, slot.fileGuid,
							slot.mutationRevision, slot.queuedRevision);
					}
					reportCommand(command.type);
					continue;
				}
				if (command.type == CanvasCommandType::Undo)
				{
					renderer_.InvalidateTrustedL2Snapshot();
					trustedSnapshotSignatureValid = false;
					bool changed = undoCurrentPage(frameDirty);
					bool restoredBoundary = false;
					if (!changed && document_ && currentPageIndex_ < pageRuntimeStates.size())
					{
						InkPage* page = document_->PageAt(currentPageIndex_);
						CanvasPageRuntimeState& runtime = pageRuntimeStates[currentPageIndex_];
						if (activeWorkspace == Bridge::Workspace::Desktop &&
							desktopClearRecovery && page)
						{
							if (desktopClearRecovery->canvas &&
								page->PageGuid().Bytes() ==
									desktopClearRecovery->canvas->pageGuid.Bytes())
							{
								auto recovery = std::move(*desktopClearRecovery->canvas);
								auto locator = desktopClearRecovery->fileGuid;
								desktopClearRecovery.reset();
								restoredBoundary = restoreCurrentPageFromSnapshot(recovery,
									particleSnapshot, frameDirty, forceFullPresent, width, height);
								if (!restoredBoundary)
									desktopClearRecovery = DesktopClearRecovery{
										std::move(recovery), locator, false, false, false };
							}
							else if (desktopClearRecovery->diskReady &&
								desktopClearRecovery->fileGuid &&
								!desktopClearRecovery->loadPending &&
								observer_.desktopLoadRequested)
								desktopClearRecovery->loadPending =
									observer_.desktopLoadRequested(observer_.context,
										*desktopClearRecovery->fileGuid);
						}
						else if (runtime.previousClearUndoAvailable &&
							(activeWorkspace == Bridge::Workspace::Presentation ||
							activeWorkspace == Bridge::Workspace::Whiteboard) &&
							page && (runtime.intervalOrdinal != 0 || runtime.boundaryFallback) &&
							!runtime.intervalLoadPending)
						{
							if (runtime.boundaryFallback)
							{
								auto fallback = std::move(*runtime.boundaryFallback);
								runtime.boundaryFallback.reset();
								restoredBoundary = restoreCurrentPageFromSnapshot(fallback,
									particleSnapshot, frameDirty, forceFullPresent, width, height);
							}
							else if (activePresentationTarget &&
								observer_.presentationLoadRequested)
							{
								PresentationLoadRequest load;
								load.target = *activePresentationTarget;
								load.kind = PresentationLoadKind::PreviousInterval;
								load.pageGuid = draw3::uink::UInkGuid(page->PageGuid().Bytes());
								load.intervalOrdinal = runtime.intervalOrdinal - 1;
								runtime.intervalLoadPending =
									observer_.presentationLoadRequested(
										observer_.context, std::move(load));
								activePresentationLoadPending = runtime.intervalLoadPending;
							}
						}
					}
					if (changed)
					{
						// 继续撤回恢复画布后，Redo 应沿笔迹历史前进，不能再重放 Clear。
						pageRuntimeStates[currentPageIndex_].clearRedoAvailable = false;
						markPresentationMutation();
						publishCurrentPageContent();
					}
					else if (restoredBoundary)
					{
						if (activeWorkspace == Bridge::Workspace::Presentation)
						{
							markPresentationMutation();
							(void)capturePresentationAutoSave();
						}
						publishCurrentPageContent();
					}
					reportCommand(command.type);
					continue;
				}
				if (command.type == CanvasCommandType::Redo)
				{
					renderer_.InvalidateTrustedL2Snapshot();
					trustedSnapshotSignatureValid = false;
					if (redoCurrentPage(frameDirty))
					{
						markPresentationMutation();
						publishCurrentPageContent();
					}
					reportCommand(command.type);
					continue;
				}
				if (!document_)
				{
					reportCommand(command.type);
					continue;
				}
				if (command.type == CanvasCommandType::TranslateViewport)
				{
					// 白板拖动态暂不启用画布平移。
					if (activeWorkspace == Bridge::Workspace::Whiteboard)
					{
						reportCommand(command.type);
						continue;
					}
					InkPage* page = document_->PageAt(currentPageIndex_);
					InkCanvas* canvas = page
						? page->FindCanvas(kDefaultDeviceKey) : nullptr;
					if (!canvas) continue;
					CanvasViewportState next{ canvas->Viewport().x, canvas->Viewport().y };
					const CanvasVector applied = ApplyCanvasContentTranslation(
						next, { command.deltaX, command.deltaY });
					if (applied.x == 0.0f && applied.y == 0.0f) continue;
					if (!canvas->SetViewport({ next.x, next.y, 1.0f })) continue;
					markPresentationMutation();
					// 视口改变后屏幕热像失效；Canvas-local composition cache 继续复用。
					historyGpuCache.DiscardHotPreimages();
					viewportRefreshPending = true;
					viewportRefreshClearsTransient = true;
					forceFullPresent = true;
					LogCanvasPan("viewport x=%.3f y=%.3f path=budgeted",
						next.x, next.y);
					reportCommand(command.type);
					continue;
				}
				renderer_.InvalidateTrustedL2Snapshot();
				trustedSnapshotSignatureValid = false;
				const size_t pageCount = document_->Pages().size();
				size_t targetPageIndex = currentPageIndex_;
				const char* action = nullptr;
				const char* key = command.type == CanvasCommandType::NextPage ? "0" :
					command.type == CanvasCommandType::PreviousPage ? "8" : "page";
				if (command.type == CanvasCommandType::NextPage)
				{
					if (currentPageIndex_ + 1 < pageCount)
					{
						targetPageIndex = currentPageIndex_ + 1;
						action = "next";
					}
					else
					{
						const std::optional<size_t> appended = appendBlankPageWithRuntime();
						if (!appended)
						{
							std::cout << "[Page] key=0 result=failed reason=create current=" <<
								(currentPageIndex_ + 1) << " count=" <<
								document_->Pages().size() << std::endl;
							reportCommand(command.type);
							continue;
						}
						targetPageIndex = *appended;
						action = "append";
					}
				}
				else if (command.type == CanvasCommandType::PreviousPage && currentPageIndex_ > 0)
				{
					targetPageIndex = currentPageIndex_ - 1;
					action = "previous";
				}
				else if (command.type == CanvasCommandType::PreviousPage)
				{
					std::cout << "[Page] key=8 result=noop reason=first current=1 count=" <<
						pageCount << std::endl;
					reportCommand(command.type);
					continue;
				}
				else if (command.type == CanvasCommandType::SetPage)
				{
					// PPT 发布绝对页，缺少的 Draw3 页按顺序创建后再一次性切换。
					while (command.pageIndex >= document_->Pages().size())
					{
						if (!appendBlankPageWithRuntime())
						{
							std::cout << "[Page] key=page result=failed reason=create target=" <<
								(command.pageIndex + 1) << " count=" <<
								document_->Pages().size() << std::endl;
							reportCommand(command.type);
							return;
						}
					}
					targetPageIndex = command.pageIndex;
					action = targetPageIndex == currentPageIndex_ ? "absolute-noop" : "absolute";
					if (targetPageIndex == currentPageIndex_)
					{
						reportCommand(command.type);
						continue;
					}
				}
				else
				{
					reportCommand(command.type);
					continue;
				}

				(void)capturePresentationAutoSave();
				currentPageIndex_ = targetPageIndex;
				viewportTilePlan = {};
				viewportTilePlanIndex = 0;
				viewportRecoveryPending = false;
				viewportVisibleClear = true;
				resetGpuForPageSwitch(frameDirty, particleSnapshot,
					forceFullPresent, width, height);
				const CompositionRestoreResult restored = restorePageContent(
					currentPageIndex_, width, height, false);
				if (restored.path == CompositionRestorePath::Failed)
				{
					viewportVisibleClear = false;
					viewportRefreshPending = true;
					viewportRefreshClearsTransient = false;
				}
				std::cout << "[Page] key=" << key << " action=" << action <<
					" current=" << (currentPageIndex_ + 1) << " count=" <<
					document_->Pages().size() << " path=" <<
					CompositionRestorePathName(restored.path) << std::endl;
				publishCurrentPageContent();
				reportCommand(command.type);
			}
		};
		// 预热所有激光着色器路径，消除首笔落下时 Qualcomm/Adreno 等 GPU 驱动的 JIT 编译卡顿。
		renderer_.WarmUpLaserShaders();
		renderer_.WarmUpShapeShaders();
		bool appliedSelectionMode = window_.SelectionMode();
		bool auxiliaryCleanVerificationPending = appliedSelectionMode;
		while (true)
		{
			FlushCursorDiagnostics();
			const bool selectionMode = window_.SelectionMode();
			const bool selectionUsesAuxiliary =
				Bridge::SelectionUsesAuxiliaryOutput(selectionMode, activeWorkspace);
			const TransparentOutputTarget expectedOutputTarget = selectionUsesAuxiliary
				? TransparentOutputTarget::SelectionUlw
				: TransparentOutputTarget::PrimaryDrawpad;
			const bool outputTargetChanged = appliedSelectionMode != selectionMode ||
				presentation_.RequestedOutputTarget() != expectedOutputTarget;
			if (outputTargetChanged)
			{
				presentation_.SetOutputTarget(expectedOutputTarget);
				appliedSelectionMode = selectionMode;
				auxiliaryCleanVerificationPending = selectionUsesAuxiliary;
			}
			// 选择模式整段释放高精度计时器；进入绘制模式时只尝试一次。
			timerPeriod.SetSelectionMode(selectionMode);
			const ProductVisualStyle nextProductVisualStyle =
				window_.ProductVisualStyleSnapshot();
			if (!ProductVisualStyleEqual(
				currentProductVisualStyle, nextProductVisualStyle))
			{
				currentProductVisualStyle = nextProductVisualStyle;
				// Hover 跟随最新产品样式；已开始的笔画仍读取 Down 时锁存的 visualStyle。
				ConfigureProductInkCursorAppearances(window_, currentProductVisualStyle,
					configuration_.dpiScale);
				ConfigureLaserRendererStyle(renderer_, currentProductVisualStyle,
					configuration_.dpiScale);
			}
			const double frameStartMs = GetQpcTimeMilliseconds();
			if (metrics_) metrics_->BeginFrame();
			lastPresentDurationMs_ = 0.0;
			lastPresentSucceeded_ = false;
			bool forceFullPresent = outputTargetChanged || contentRevisionNeedsPresent;
			RECT viewportRecoveryDirty = {};
			if (graphicsRecoveryPending_)
			{
				const HRESULT failure = presentation_.LastFailure();
				graphicsRecoveryPending_ = false;
				// 先释放旧设备上的 GPU history，再由 presenter 在当前绘制线程重建设备或降级后端。
				historyGpuCache.Release();
				if (!presentation_.RecoverFromRuntimeFailure())
				{
					std::cout << "Failed to recover Draw3 graphics after HRESULT 0x" <<
						std::hex << static_cast<unsigned long>(failure) << std::dec << std::endl;
					window_.RequestExit();
					break;
				}

				window_.SetGpuTransparentComposition(
					presentation_.IsGpuTransparentComposition());
				RECT clientRect = {};
				if (GetClientRect(window_.Handle(), &clientRect) &&
					clientRect.right > clientRect.left && clientRect.bottom > clientRect.top)
				{
					const int recoveredWidth = clientRect.right - clientRect.left;
					const int recoveredHeight = clientRect.bottom - clientRect.top;
					window_.CommitSize(recoveredWidth, recoveredHeight);
					if (observer_.resized)
						observer_.resized(observer_.context, recoveredWidth, recoveredHeight);
				}
				if (!historyGpuCache.Initialize(
					renderer_, appliedUndoPolicy, appliedCompositionPolicy))
				{
					std::cout << "Failed to rebuild Draw3 GPU history cache after device recovery." << std::endl;
					window_.RequestExit();
					break;
				}

				trustedSnapshotSignatureValid = false;
				compositionMaintenance.clear();
				if (rasterPipelineGeneration == (std::numeric_limits<uint64_t>::max)())
					rasterPipelineGeneration = 1;
				else ++rasterPipelineGeneration;
				viewportTilePlan = {};
				viewportTilePlanIndex = 0;
				viewportRecoveryPending = false;
				viewportVisibleClear = false;
				viewportRefreshPending = true;
				viewportRefreshClearsTransient = false;

				const WindowSize recoveredSize = window_.Size();
				const RECT fullCanvas = GetFullCanvasRect(
					recoveredSize.width, recoveredSize.height);
				renderer_.ClearRTV(renderer_.layerL2RTV.Get(), kTransparentLayerClearColor);
				renderer_.ClearOperatorLayer(renderer_.layerL1);
				renderer_.ClearOperatorLayer(renderer_.layerL0);
				renderer_.ClearAllLaserCoverage();
				renderer_.ResetLaserParticles();
				renderer_.ClearRTV(renderer_.backBufferRTV.Get(), kTransparentLayerClearColor);
				RebuildActiveLayers(active, renderer_, recoveredSize.width,
					recoveredSize.height, shapePrimitiveScratch);
				laserIncrementalEnsureAttempted = false;
				laserStableBounds = {};
				laserLiveBounds = {};
				for (LaserStrokeLayer& layer : laserStrokeLayers)
				{
					layer.incrementalState = {};
					layer.stableBounds = {};
					layer.liveBounds = {};
					layer.bounds = RectFromLaserPoints(LaserStrokeLayerPoints(layer),
						configuration_.dpiScale, recoveredSize.width, recoveredSize.height);
					UnionRectInPlace(laserLiveBounds, layer.bounds);
				}
				laserCoverageMode = laserStrokeLayers.empty()
					? LaserCoverageMode::Inactive : LaserCoverageMode::FullRedraw;
				pendingLaserBakeDirty = {};
				laserParticleDirtyTracker.Clear();
				lastLaserParticleSimulationQpc = 0;
				previousLaserParticleBounds = {};
				previousLaserTipBounds = {};
				particlesWereEnabled = laserParticlesEnabled_.load(std::memory_order_acquire) &&
					renderer_.LaserParticlesAvailable();
				particlesEnabledEffective = particlesWereEnabled;
				forceFullPresent = true;
				UnionRectInPlace(viewportRecoveryDirty, fullCanvas);
				renderer_.WarmUpLaserShaders();
				renderer_.WarmUpShapeShaders();
				std::cout << "Recovered Draw3 graphics with mode " <<
					TransparentPresentModeName(presentation_.ActiveMode()) << "." << std::endl;
			}
			if (!laserIncrementalEnsureAttempted &&
				window_.ActiveTool() == DrawingTool::Laser)
			{
				laserIncrementalEnsureAttempted = true;
				renderer_.EnsureLaserIncrementalCoverageResources();
			}
			if (active.empty()) window_.ClearActiveDrawingCursorTool();
			const bool drawingCursorRequested = window_.ConsumeDrawingCursorRenderRequest();
			const double previousFrameMs = lastActiveFrameStartMs > 0.0
				? frameStartMs - lastActiveFrameStartMs : 0.0;
			ContactRecord* record = nullptr;

			const uint64_t requestedPolicyGeneration =
				historyCachePolicyGeneration_.load(std::memory_order_acquire);
			if (requestedPolicyGeneration != appliedHistoryCachePolicyGeneration)
			{
				{
					const std::scoped_lock lock(historyCachePolicyMutex_);
					appliedUndoPolicy = undoCachePolicy_;
					appliedCompositionPolicy = compositionCachePolicy_;
					appliedHistoryCachePolicyGeneration =
						historyCachePolicyGeneration_.load(std::memory_order_acquire);
				}
				historyGpuCache.SetUndoPolicy(appliedUndoPolicy);
				historyGpuCache.SetCompositionPolicy(appliedCompositionPolicy);
			}
			if (window_.ConsumeCompositionChangedRequest())
			{
				if (!presentation_.RefreshAfterCompositionChanged())
				{
					graphicsRecoveryPending_ = presentation_.RecoveryPending();
					continue;
				}
				window_.RequestFullPresent();
			}
			if (ProcessPendingResize(false))
			{
				const WindowSize size = window_.Size();
				historyGpuCache.DiscardHotPreimages();
				trustedSnapshotSignatureValid = false;
				compositionMaintenance.clear();
				if (rasterPipelineGeneration == (std::numeric_limits<uint64_t>::max)())
				{
					rasterPipelineGeneration = 1;
					historyGpuCache.DiscardCompositionCache();
				}
				else ++rasterPipelineGeneration;
				// Footprint 是 Canvas-local 真值，窗口 Resize 不需要遍历全页重算。
				renderer_.ClearRTV(
					renderer_.layerL2RTV.Get(), kTransparentLayerClearColor);
				const CompositionRestoreResult resizedPage = restorePageContent(
					currentPageIndex_, size.width, size.height, false);
				if (resizedPage.path == CompositionRestorePath::Failed)
				{
					viewportVisibleClear = false;
					viewportRefreshPending = true;
					viewportRefreshClearsTransient = false;
				}
				std::cout << "[InkHistory] resize generation=" <<
					rasterPipelineGeneration << " path=" <<
					CompositionRestorePathName(resizedPage.path) << std::endl;
				RebuildActiveLayers(active, renderer_, size.width, size.height,
					shapePrimitiveScratch);
				laserStableBounds = ClampRectToCanvas(
					laserStableBounds, size.width, size.height);
				laserLiveBounds = {};
				for (LaserStrokeLayer& layer : laserStrokeLayers)
				{
					layer.incrementalState = {};
					layer.stableBounds = {};
					layer.liveBounds = {};
					const std::vector<InkPoint>& points = LaserStrokeLayerPoints(layer);
					layer.bounds = RectFromLaserPoints(points, configuration_.dpiScale,
						size.width, size.height);
					UnionRectInPlace(laserLiveBounds, layer.bounds);
				}
				const LaserCoverageMode resizedCoverageMode = laserStrokeLayers.empty()
					? LaserCoverageMode::Inactive
					: SelectLaserCoverageMode(laserCoverageMode, laserStrokeLayers.size(),
						renderer_.LaserIncrementalCoverageAvailable());
				laserCoverageMode = resizedCoverageMode;
				forceFullPresent = true; // Resize 保留 L2，并从 CPU 状态恢复共享 L1/L0。
			}
			if (graphicsRecoveryPending_) continue;
			DrawingCursorSample priorityPenSample;
			DrawingCursorSample priorityMouseSample;
			const bool priorityPenSampleValid =
				window_.ReadPenCursorSample(priorityPenSample) &&
				priorityPenSample.valid;
			const bool priorityPenInContact = priorityPenSampleValid &&
				IsPenContactSampleFresh(priorityPenSample.inContact,
					priorityPenSample.qpc, suppressedPenTerminalQpc);
			if (ShouldBeginSuppressingPenContactDuringTouchPan(
				touchGesture.PanActive(), priorityPenInContact))
			{
				suppressPenUntilRelease = true;
				window_.SuppressPenContactForTouchPan();
			}
			else if (suppressPenUntilRelease && !priorityPenInContact &&
				!hasLiveSuppressedPenContact())
				suppressPenUntilRelease = false;
			const bool priorityMouseInContact =
				window_.ReadMouseCursorSample(priorityMouseSample) &&
				priorityMouseSample.valid && priorityMouseSample.inContact;
			const bool navigationInProgress = touchGesture.PanActive() ||
				touchGesture.InertiaCandidateActive() || panMotion.inertiaActive ||
				!gestureContacts.empty();
			if (navigationInProgress && (touchGesture.PanActive() ||
				input_.HasPendingWork() || ShouldPrioritizeDrawingContact(
					navigationInProgress,
					priorityPenInContact && !suppressPenUntilRelease,
					priorityMouseInContact)))
			{
				// 导航推进前先按输入 QPC 归类，避免最后 Touch Up 附近的 Pen 被补画。
				while (input_.TryDequeue(record)) processCommandAndReconcile(record);
			}
			if (window_.ConsumeFullPresentRequest()) forceFullPresent = true;
			LARGE_INTEGER navigationQpc = {};
			QueryPerformanceCounter(&navigationQpc);
			updateCanvasNavigation(navigationQpc.QuadPart);
			if (viewportRefreshPending || viewportRecoveryPending)
			{
				const WindowSize size = window_.Size();
				if (viewportRefreshPending)
				{
					renderer_.ClearRTV(renderer_.layerL2RTV.Get(), kTransparentLayerClearColor);
					if (viewportRefreshClearsTransient)
					{
						leftMouseSpeedEraser.CancelVisual();rightMouseSpeedEraser.CancelVisual();
						penEraserHoverLane.Invalidate();invertedPenEraserHoverLane.Invalidate();
						renderer_.ClearOperatorLayer(renderer_.layerL1);
						renderer_.ClearOperatorLayer(renderer_.layerL0);
						renderer_.ClearAllLaserCoverage();
					}
					viewportTilePlan = {};
					viewportTilePlanIndex = 0;
					viewportVisibleClear = false;
					if (document_ && currentPageIndex_ < pageRuntimeStates.size())
					{
						const InkPage* page = document_->PageAt(currentPageIndex_);
						const InkCanvas* canvas = page
							? page->FindCanvas(kDefaultDeviceKey) : nullptr;
						if (canvas)
						{
							const CanvasViewportState viewport{
								canvas->Viewport().x, canvas->Viewport().y };
							const std::optional<CanvasRect> coverage =
								ComputeCanvasRenderCoverageBounds(viewport,
									static_cast<float>(size.width), static_cast<float>(size.height),
									panMotion.velocity, kCompositionTileSize);
							if (coverage)
							{
								const std::vector<CanvasTileCoordinate> contentTiles =
									CollectCanvasContentTiles(
										pageRuntimeStates[currentPageIndex_].history, *coverage);
								viewportTilePlan = PlanCanvasRenderTiles(contentTiles,
									viewport, static_cast<float>(size.width),
									static_cast<float>(size.height), panMotion.velocity,
									kCompositionTileSize);
							}
						}
					}
					viewportRecoveryPending = !viewportTilePlan.tiles.empty();
					viewportVisibleClear = viewportTilePlan.visibleTileCount == 0;
					forceFullPresent = true;
					viewportRefreshPending = false;
					viewportRefreshClearsTransient = false;
				}

				const CanvasRenderBudget recoveryBudget = ComputeCanvasRenderBudget({
					1000.0 / configuration_.timingProfile.target_fps,
					previousCanvasFrameWorkMilliseconds,
					previousCanvasPresentMilliseconds,
					viewportTileEwmaMilliseconds });
				const uint64_t recoveryWakeGeneration = input_.CaptureWakeGeneration();
				size_t recoveredTiles = 0;
				while (viewportRecoveryPending && recoveredTiles < recoveryBudget.maximumTiles &&
					viewportTilePlanIndex < viewportTilePlan.tiles.size())
				{
					if (input_.HasPendingWork() ||
						input_.CaptureWakeGeneration() != recoveryWakeGeneration) break;
					if (!document_ || currentPageIndex_ >= pageRuntimeStates.size()) break;
					const InkPage* page = document_->PageAt(currentPageIndex_);
					const InkCanvas* canvas = page
						? page->FindCanvas(kDefaultDeviceKey) : nullptr;
					if (!page || !canvas) break;
					CanvasPageRuntimeState& runtime = pageRuntimeStates[currentPageIndex_];
					const CanvasPlannedTile planned =
						viewportTilePlan.tiles[viewportTilePlanIndex];
					const SignedTileCoordinate tile{ planned.tile.x, planned.tile.y };
					const double tileStartMilliseconds = GetQpcTimeMilliseconds();
					bool tileCompleted = true;
					if (planned.priority == CanvasTilePriority::Visible)
					{
						const std::array<SignedTileCoordinate, 1> tiles = { tile };
						const InkViewport viewport = canvas->Viewport();
						const CompositionRestoreRequest request = {
							{ page->PageGuid(), kDefaultDeviceKey }, currentRasterKey(),
							canvas, &runtime.history, tiles, runtime.history.Items().size(),
							viewport.x, viewport.y, size.width, size.height, false };
						const CompositionRestoreResult restored =
							historyGpuCache.RestoreComposition(request);
						tileCompleted = restored.path != CompositionRestorePath::Failed;
						if (tileCompleted)
							UnionRectInPlace(viewportRecoveryDirty, restored.dirty);
					}
					else
					{
						const auto root = runtime.history.CompositionTree().RootNode();
						if (root) historyGpuCache.PrimeCompositionNode(
							{ page->PageGuid(), kDefaultDeviceKey }, currentRasterKey(),
							*canvas, runtime.history, *root, tile, size.width, size.height);
					}
					const double tileMilliseconds = (std::max)(0.01,
						GetQpcTimeMilliseconds() - tileStartMilliseconds);
					viewportTileEwmaMilliseconds = viewportTileEwmaMilliseconds * 0.8 +
						tileMilliseconds * 0.2;
					// 可见 Tile 失败时保留游标，下一帧重试，不能把不完整 L2 标成清晰。
					if (!tileCompleted) break;
					++viewportTilePlanIndex;
					++recoveredTiles;
					if (viewportTilePlanIndex >= viewportTilePlan.visibleTileCount)
						viewportVisibleClear = true;
				}
				viewportRecoveryPending = viewportTilePlanIndex < viewportTilePlan.tiles.size();
				if (!viewportRecoveryPending)
				{
					viewportVisibleClear = true;
					viewportTilePlan = {};
					viewportTilePlanIndex = 0;
				}
			}
			if (haptics_)
			{
				if (window_.ConsumeHapticPointerLeave())
				{
					hapticContinuousActive = false;
					haptics_->StopFeedback();
				}
				uint32_t pointerId = 0;
				bool pointerEraserHint = false;
				if (window_.ConsumeHapticPointerId(pointerId, pointerEraserHint))
				{
					if (touchGesture.PanActive() || suppressPenUntilRelease ||
						window_.PenContactSuppressedForTouchPan())
					{
						hapticContinuousActive = false;
						haptics_->StopFeedback();
					}
					else if (haptics_->AttachPointerId(pointerId))
					{
						if (window_.ActiveTool() == DrawingTool::Laser)
						{
							haptics_->StopFeedback();
							// Laser 只提供视觉反馈，不预启动或维持触觉波形。
						}
						else
						{
							// 笔尾需在 RTS Down 前预启动橡皮波形，RTS 仍在 Down 时校验真实工具。
							const HapticContinuousFeedback hapticFeedback = pointerEraserHint
								? HapticContinuousFeedback::InkContinuous
								: ResolveContinuousHapticFeedback(
									HapticToolForDrawingTool(window_.ActiveTool()));
							haptics_->TickContinuous(hapticFeedback);
						}
					}
				}
			}
			if (window_.ExitRequested()) break;

			LARGE_INTEGER animationQpc = {};
			QueryPerformanceCounter(&animationQpc);
			const bool speedEraserHoverAnimating =
				updateSpeedEraserHoverLanes(animationQpc.QuadPart);
			const LaserTrailPhase previousLaserPhase = laserLifecycle.phase;
			const float previousLaserOpacity = laserOpacity;
			laserOpacity = EvaluateLaserTrailOpacity(laserLifecycle,
				animationQpc.QuadPart, qpcFrequency,
				laserHoldDurationSeconds_.load(std::memory_order_acquire));
			const bool laserExpired = previousLaserPhase != LaserTrailPhase::Inactive &&
				laserLifecycle.phase == LaserTrailPhase::Inactive;
			const bool laserFadeActive = laserLifecycle.phase == LaserTrailPhase::Fade;
			bool laserOpacityChanged =
				std::abs(previousLaserOpacity - laserOpacity) > 0.0001f;
			const bool particlesEnabled =
				laserParticlesEnabled_.load(std::memory_order_acquire) &&
				renderer_.LaserParticlesAvailable();
			// 只在 Inactive（屏幕上无激光笔画）时才更新有效状态，
			// 确保开关切换在当前笔画/Hold/Fade 全部消失后的下一笔才生效。
			if (laserLifecycle.phase == LaserTrailPhase::Inactive)
				particlesEnabledEffective = particlesEnabled;

			RECT frameDirty = {};
			UnionRectInPlace(frameDirty, viewportRecoveryDirty);
			UnionRectInPlace(frameDirty, pendingLaserBakeDirty);
			pendingLaserBakeDirty = {};
			if (particlesWereEnabled != particlesEnabledEffective)
			{
				for (RuntimeStroke* runtime : active)
				{
					if (runtime && runtime->tool == DrawingTool::Laser)
						ResetLaserParticleEmitterState(*runtime);
				}
				if (!particlesEnabledEffective)
				{
					// 关闭后的下一绘制帧清空 GPU 状态，并用旧保守区清除透明窗口残影。
					UnionRectInPlace(frameDirty, previousLaserParticleBounds);
					renderer_.ResetLaserParticles();
					laserParticleDirtyTracker.Clear();
					lastLaserParticleSimulationQpc = 0;
					laserLifecycle.minimumHoldDurationSeconds = 0.0;
				}
				particlesWereEnabled = particlesEnabledEffective;
			}
			const WindowSize animationCanvasSize = window_.Size();
			LaserParticleDirtySnapshot laserParticleSnapshot =
				laserParticleDirtyTracker.Snapshot(animationQpc.QuadPart);
			RECT currentLaserParticleBounds = ClampRectToCanvas(
				laserParticleSnapshot.activeBounds,
				animationCanvasSize.width, animationCanvasSize.height);
			if (!IsEmptyRect(previousLaserParticleBounds) ||
				!IsEmptyRect(currentLaserParticleBounds))
			{
				UnionRectInPlace(frameDirty, previousLaserParticleBounds);
				UnionRectInPlace(frameDirty, currentLaserParticleBounds);
			}
			const bool particleAnimationActive = laserParticleSnapshot.hasActive;
			if (selectionMode && !currentPageHasContent() &&
				auxiliaryCleanVerificationPending && active.empty() &&
				laserLifecycle.phase == LaserTrailPhase::Inactive &&
				!particleAnimationActive)
			{
				// 最后一帧瞬态内容结束后做全帧 ULW 校验，不能按局部脏区推断窗口已净。
				forceFullPresent = true;
			}
			if (laserOpacityChanged)
			{
				UnionRectInPlace(frameDirty, laserStableBounds);
				UnionRectInPlace(frameDirty, laserLiveBounds);
			}
			if (laserExpired)
			{
				UnionRectInPlace(frameDirty, laserStableBounds);
				UnionRectInPlace(frameDirty, laserLiveBounds);
				renderer_.ClearAllLaserCoverage();
				laserStableBounds = {};
				laserLiveBounds = {};
				laserStrokeLayers.clear();
				laserCoverageMode = LaserCoverageMode::Inactive;
			}
			if (active.empty())
			{
				const WindowSize size = window_.Size();
				processCanvasCommands(
					frameDirty, laserParticleSnapshot, forceFullPresent,
					size.width, size.height);
			}
			const bool navigationActive = touchGesture.PanActive() ||
				touchGesture.InertiaCandidateActive() || panMotion.inertiaActive ||
				!gestureContacts.empty() || viewportRefreshPending || viewportRecoveryPending;
			if (active.empty() && !navigationActive && !forceFullPresent && !drawingCursorRequested &&
				!speedEraserHoverAnimating &&
				!laserFadeActive && !particleAnimationActive && IsEmptyRect(frameDirty) &&
				!compositionMaintenance.empty())
			{
				// 每个 tile 之间先检查输入，避免后台预建拉长下一笔 Down 的排队时间。
				if (input_.TryDequeue(record))
				{
					processCommandAndReconcile(record);
					continue;
				}
				const CompositionMaintenanceItem maintenance =
					compositionMaintenance.front();
				compositionMaintenance.pop_front();
				if (maintenance.rasterGeneration == rasterPipelineGeneration &&
					document_ && maintenance.pageIndex < pageRuntimeStates.size())
				{
					const InkPage* page = document_->PageAt(maintenance.pageIndex);
					const InkCanvas* canvas = page
						? page->FindCanvas(kDefaultDeviceKey) : nullptr;
					if (page && canvas)
					{
						const WindowSize size = window_.Size();
						historyGpuCache.PrimeCompositionNode(
							{ page->PageGuid(), kDefaultDeviceKey }, currentRasterKey(),
							*canvas, pageRuntimeStates[maintenance.pageIndex].history,
							maintenance.node, maintenance.tile, size.width, size.height);
					}
				}
				continue;
			}

			if (active.empty() && !navigationActive && !forceFullPresent && !drawingCursorRequested &&
				!speedEraserHoverAnimating &&
				!laserFadeActive && !particleAnimationActive && IsEmptyRect(frameDirty))
			{
				if (hapticContinuousActive && haptics_)
				{
					haptics_->StopFeedback();
					hapticContinuousActive = false;
				}
				if (metrics_) metrics_->BeginIdle(frameStartMs);
				lastActiveFrameStartMs = 0.0;
				if (laserLifecycle.phase == LaserTrailPhase::Hold)
				{
					if (input_.TryDequeue(record))
					{
						processCommandAndReconcile(record);
						if (metrics_) metrics_->EndIdle(GetQpcTimeMilliseconds());
						continue;
					}
					const uint64_t waitGeneration = input_.CaptureWakeGeneration();
					const double holdSeconds = EffectiveLaserHoldDurationSeconds(
						laserLifecycle,
						laserHoldDurationSeconds_.load(std::memory_order_acquire));
					int64_t holdDeadlineQpc = 0;
					if (!TryAddQpcDuration(laserLifecycle.lastAllUpQpc,
						qpcFrequency, holdSeconds, holdDeadlineQpc))
					{
						// 内部状态异常时退回可靠阻塞，避免溢出后忙循环。
						input_.WaitDequeue(record);
						processCommandAndReconcile(record);
						if (metrics_) metrics_->EndIdle(GetQpcTimeMilliseconds());
						continue;
					}
					LARGE_INTEGER waitStartQpc = {};
					QueryPerformanceCounter(&waitStartQpc);
					const double timeoutMilliseconds = QpcDeltaSeconds(
						holdDeadlineQpc, waitStartQpc.QuadPart, qpcFrequency) * 1000.0;
					input_.WaitForWake(waitGeneration, timeoutMilliseconds);
					if (metrics_) metrics_->EndIdle(GetQpcTimeMilliseconds());
					continue; // Hold 静止期只等 deadline 或设置/Input wake，不持续 Present。
				}
				// 二次排空后才等待；竞态窗口内到达的命令会留下信号量计数。
				if (input_.TryDequeue(record))
				{
					processCommandAndReconcile(record);
					if (metrics_) metrics_->EndIdle(GetQpcTimeMilliseconds());
					continue;
				}
				input_.WaitDequeue(record);
				processCommandAndReconcile(record);
				if (metrics_) metrics_->EndIdle(GetQpcTimeMilliseconds());
				continue;
			}

			const bool frameHadActiveContact = HasPhysicalContact(active);
			const uint64_t frameWakeGeneration = input_.CaptureWakeGeneration();
			LARGE_INTEGER frameQpc = {};
			QueryPerformanceCounter(&frameQpc);
			const float laserParticleWallDeltaSeconds =
				lastLaserParticleSimulationQpc > 0
				? static_cast<float>(QpcDeltaSeconds(frameQpc.QuadPart,
					lastLaserParticleSimulationQpc, qpcFrequency)) : 0.0f;
			bool hasEndedStroke = false;
			// 在处理同帧输入前保存 opacity；新 Down 可能直接恢复满亮，仍需覆盖旧淡出区域。
			const float preInputLaserOpacity = laserOpacity;
			const bool interruptedStrokeReconnectEnabled =
				GetInterruptedStrokeReconnectEnabled();
			for (RuntimeStroke* runtime : active)
				if (runtime) runtime->modelInputThisFrame = false;
			if (!interruptedStrokeReconnectEnabled)
			{
				while (input_.TryDequeue(record)) processCommandAndReconcile(record);
				// 关闭开关时保留原先的 Down 出队顺序，确保它是完整的回滚点。
			}
			// 先把旧 contact 的 Up 转成候选，同帧随后出队的新 Down 才能看到它。
			for (RuntimeStroke* runtime : active)
			{
				runtime->movedThisFrame = consumeLatestSnapshot(*runtime);
				hasEndedStroke = hasEndedStroke || runtime->ended;
			}

			size_t reconnectCandidateCount = static_cast<size_t>(std::count_if(
				active.begin(), active.end(), [](const RuntimeStroke* runtime)
					{ return runtime && runtime->awaitingReconnect; }));
			size_t reconnectEvictionCount =
				GetInterruptedStrokeReconnectEvictionCount(reconnectCandidateCount);
			while (reconnectEvictionCount > 0)
			{
				RuntimeStroke* oldest = nullptr;
				for (RuntimeStroke* runtime : active)
				{
					if (!runtime || !runtime->awaitingReconnect) continue;
					if (!oldest || runtime->deferredUpSnapshot.qpc < oldest->deferredUpSnapshot.qpc)
						oldest = runtime;
				}
				if (!oldest) break;
				completeModelUp(*oldest, oldest->deferredUpSnapshot, false,
					frameQpc.QuadPart);
				hasEndedStroke = true;
				--reconnectEvictionCount;
			}

			if (interruptedStrokeReconnectEnabled)
				while (input_.TryDequeue(record)) processCommandAndReconcile(record);
			for (RuntimeStroke* runtime : active)
			{
				if (!runtime || !runtime->awaitingReconnect ||
					(interruptedStrokeReconnectEnabled &&
						!IsInterruptedStrokeReconnectExpired(
							runtime->reconnectDeadlineQpc, frameQpc.QuadPart))) continue;
				completeModelUp(*runtime, runtime->deferredUpSnapshot, false,
					frameQpc.QuadPart);
				hasEndedStroke = true;
			}
			const double frameAbsoluteSeconds = AbsoluteQpcSeconds(
				frameQpc.QuadPart, qpcFrequency);
			mouseVisualSeconds = frameAbsoluteSeconds;
			mouseSpeedEraser().Advance(mouseVisualSeconds);
			for (RuntimeStroke* runtime : active)
			{
				if (!runtime || runtime->ended || runtime->awaitingReconnect ||
					runtime->stroke.widthMode != StrokeWidthMode::SpeedEraser) continue;
				if(!runtime->firstEraserCursorFrame || runtime->lastInputSnapshot.phase!=ContactPhase::Down)
					runtime->speedEraserOc.Advance(frameAbsoluteSeconds);
				runtime->firstEraserCursorFrame=false;
				runtime->eraserSize.Update(runtime->speedEraserOc.Diameter(),frameAbsoluteSeconds,
					runtime->speedEraserOc.SecondsSinceMovement(frameAbsoluteSeconds)>=runtime->speedEraserOc.Configuration().idleStartSeconds);
				// 当前工具独立回缩；历史点不变，下一次几何才添加尺寸断点。
			}
			const double modeledTipFrameIntervalSeconds =
				1.0 / configuration_.timingProfile.target_fps;
			for (RuntimeStroke* runtime : active)
			{
				if (!runtime || runtime->ended || runtime->awaitingReconnect ||
					(runtime->tool != DrawingTool::Pen && runtime->tool != DrawingTool::HardPen) ||
					runtime->stroke.idleFrozen || runtime->modelInputThisFrame ||
					runtime->stationaryModelAdvanceBlocked) continue;
				const DirectX::XMFLOAT2 rawEndpoint = {
					runtime->lastModelSnapshot.position.x,
					runtime->lastModelSnapshot.position.y };
				const double frameRealTime = QpcDeltaSeconds(
					frameQpc.QuadPart, runtime->qpcOrigin, qpcFrequency);
				const double frameInputTime = frameRealTime - runtime->stroke.modelTimeOffset;
				if (!runtime->stroke.endpointAdmission.active &&
					!ShouldStartEndpointSettling(
						frameRealTime - runtime->stroke.lastMovementInputTime,
						modeledTipFrameIntervalSeconds))
					continue; // 单个短暂空帧仍属于 Tracking，不能让 Kalman prediction 常态闪断。
				if (!runtime->stroke.endpointAdmission.active)
					BeginEndpointAdmission(runtime->stroke, rawEndpoint);
				if (IsModeledTipSettled(LatestModeledTip(runtime->stroke),
					rawEndpoint, modeledTipFrameIntervalSeconds))
				{
					runtime->stroke.modelClockStopped = true;
					if (runtime->stroke.endpointAdmission.recovering)
					{
						ClearEndpointAdmission(runtime->stroke);
						BeginEndpointAdmission(runtime->stroke, rawEndpoint);
					}
					runtime->stroke.modelScratch.clear();
					AppendEndpointBoundedModeledPoints(runtime->stroke,
						runtime->stroke.modelScratch, -1.0f,
						runtime->stroke.lastMovementInputTime, true);
					continue;
				}
				const double minimumInputTime = runtime->lastModelInputTime + 0.000001;
				if (!std::isfinite(frameInputTime) || frameInputTime < minimumInputTime)
					continue;
				const Input stationaryInput{
					.event_type = Input::EventType::kMove,
					.position = Vec2(rawEndpoint.x, rawEndpoint.y),
					.time = Time(frameInputTime),
					.pressure = runtime->lastPressure,
					.tilt = runtime->lastTilt,
					.orientation = runtime->lastOrientation
				};
				// Predict 不会推进模型；仅用最后接受的模型锚点完成停笔收敛。
				runtime->modelInputThisFrame = true;
				runtime->stroke.modelScratch.clear();
				if (observer_.penDiagnostics) ++runtime->diagnosticModelUpdates;
				if (absl::Status status = runtime->stroke.modeler.Update(
					stationaryInput, runtime->stroke.modelScratch); status.ok())
				{
					runtime->lastModelInputTime = frameInputTime;
					if (runtime->stroke.endpointAdmission.recovering)
						AppendRecoveryModeledPoints(runtime->stroke, runtime->stroke.modelScratch,
							rawEndpoint, -1.0f);
					else AppendEndpointBoundedModeledPoints(runtime->stroke,
						runtime->stroke.modelScratch, -1.0f, runtime->stroke.lastMovementInputTime);
				}
				else
				{
					runtime->stationaryModelAdvanceBlocked = true;
					// 同一次停笔不再逐帧重试失败输入；下一份成功真实输入会解除锁存。
					std::cout << "Error: " << status.message() << std::endl;
				}
			}
			laserOpacity = EvaluateLaserTrailOpacity(laserLifecycle,
				frameQpc.QuadPart, qpcFrequency,
				laserHoldDurationSeconds_.load(std::memory_order_acquire));
			laserOpacityChanged = laserOpacityChanged ||
				std::abs(preInputLaserOpacity - laserOpacity) > 0.0001f;
			// Up 后同帧新 Down 可能刚完成旧批次 Bake；必须在本帧基础合成前消费该 dirty。
			UnionRectInPlace(frameDirty, pendingLaserBakeDirty);
			pendingLaserBakeDirty = {};

			const bool hasPhysicalContact = HasPhysicalContact(active);
			const auto frameToolIterator = std::find_if(active.begin(), active.end(),
				[](const RuntimeStroke* runtime)
					{ return runtime && !runtime->ended && !runtime->awaitingReconnect; });
			const DrawingTool frameTool = frameToolIterator != active.end()
				? (*frameToolIterator)->tool
				: active.empty() ? window_.ActiveTool() : active.front()->tool;
			const DrawingCursorPointerAuthority cursorAuthority =
				window_.CursorOwner();
			auto activeCursorIterator = std::find_if(active.begin(), active.end(),
				[&](const RuntimeStroke* runtime)
				{
					if (!runtime || runtime->ended || runtime->awaitingReconnect) return false;
					if (cursorAuthority == DrawingCursorPointerAuthority::Pen ||
						cursorAuthority == DrawingCursorPointerAuthority::Unknown)
						return runtime->metricDeviceType == InputDeviceType::Pen;
					if (cursorAuthority == DrawingCursorPointerAuthority::Mouse)
						return runtime->metricDeviceType == InputDeviceType::MouseLeft ||
							runtime->metricDeviceType == InputDeviceType::MouseRight;
					return false;
				});
			if (activeCursorIterator == active.end() &&
				cursorAuthority == DrawingCursorPointerAuthority::Unknown)
			{
				activeCursorIterator = std::find_if(active.begin(), active.end(),
					[](const RuntimeStroke* runtime)
					{
						return runtime && !runtime->ended && !runtime->awaitingReconnect &&
							(runtime->metricDeviceType == InputDeviceType::MouseLeft ||
								runtime->metricDeviceType == InputDeviceType::MouseRight);
					});
			}
			if(activeCursorIterator!=active.end())
			{
				const bool pen=(*activeCursorIterator)->metricDeviceType==InputDeviceType::Pen;
				for(auto it=active.begin();it!=active.end();++it)
					if(*it && !(*it)->ended && !(*it)->awaitingReconnect &&
						(pen?(*it)->metricDeviceType==InputDeviceType::Pen:
							(*it)->metricDeviceType==InputDeviceType::MouseLeft || (*it)->metricDeviceType==InputDeviceType::MouseRight) &&
						(*it)->qpcOrigin>(*activeCursorIterator)->qpcOrigin)activeCursorIterator=it;
			}
			if (activeCursorIterator != active.end())
				window_.SetActiveDrawingCursorTool((*activeCursorIterator)->tool);
			else if (frameToolIterator != active.end())
				window_.SetActiveDrawingCursorTool((*frameToolIterator)->selectedTool);
			else
				window_.ClearActiveDrawingCursorTool();

			const WindowSize size = window_.Size();
			laserParticleEmissionRequests.clear();
			uint32_t spawnedLaserParticleCount = 0;
			RECT currentFrameLaserParticleBatchBounds = {};
			const RECT previousLaserLiveBounds = laserLiveBounds;
			RECT currentLaserLiveBounds = {};
			bool shapeLayerNeedsRebuild = false;
			bool otherLiveLayerNeedsRebuild = false;
			const bool hasLaserRuntime = std::any_of(active.begin(), active.end(),
				[](const RuntimeStroke* runtime)
				{
					return runtime && runtime->tool == DrawingTool::Laser;
				});
			for (RuntimeStroke* runtime : active)
			{
				ActiveStroke& stroke = runtime->stroke;
				if (runtime->shape.active)
				{
					stroke.lastL0Rect = stroke.currentL0Rect;
					stroke.logicalInputTime = std::max(stroke.logicalInputTime,
						QpcDeltaSeconds(frameQpc.QuadPart, runtime->qpcOrigin, qpcFrequency));
					if (!runtime->ended)
					{
						stroke.predictedResults.clear();
						if (!runtime->shape.rawFallbackRequired &&
							kActivePredictionMode != InkPredictionMode::Disabled)
						{
							if (absl::Status status = stroke.modeler.Predict(
								stroke.predictedResults); !status.ok())
								stroke.predictedResults.clear();
						}
						const DirectX::XMFLOAT2 endpoint = ResolveShapeLiveEndpoint(
							stroke.predictedResults, runtime->shape.modeledEndpoint,
							runtime->shape.hasModeledEndpoint, runtime->shape.rawEndpoint,
							runtime->shape.rawFallbackRequired);
						SetShapeVisualEndpoint(runtime->shape, endpoint);
						stroke.predictedResults.clear(); // prediction 只作为末点 scratch，不保留轨迹。
					}
					else
					{
						stroke.predictedResults.clear();
					}
					stroke.currentL0Rect = RectFromShapePrimitive(runtime->shape.primitive,
						runtime->shape.kind, size.width, size.height);
					if (runtime->shape.visualChanged)
					{
						shapeLayerNeedsRebuild = true;
						runtime->shape.visualChanged = false;
						UnionRectInPlace(frameDirty, stroke.lastL0Rect);
						UnionRectInPlace(frameDirty, stroke.currentL0Rect);
						UnionRectInPlace(runtime->visibleDirty, stroke.lastL0Rect);
						UnionRectInPlace(runtime->visibleDirty, stroke.currentL0Rect);
					}
					if (!IsEmptyRect(stroke.currentL0Rect)) runtime->metricVisible = true;
					continue; // Shape 完整几何只在共享 L0，绝不推进稳定前缀到 L1。
				}
				if (runtime->tool == DrawingTool::Laser)
				{
					stroke.logicalInputTime = std::max(stroke.logicalInputTime,
						QpcDeltaSeconds(frameQpc.QuadPart, runtime->qpcOrigin, qpcFrequency));
					if (!runtime->ended)
					{
						stroke.predictedResults.clear();
						if (kActivePredictionMode != InkPredictionMode::Disabled)
						{
							if (absl::Status status = stroke.modeler.Predict(
								stroke.predictedResults); !status.ok())
								stroke.predictedResults.clear();
						}
						RebuildPredictedPoints(stroke);
						RebuildL0DrawPoints(stroke, 0.0,
							StrokeShape::RoundCapsule, size.width, size.height);
						if (stroke.l0DrawPoints.empty() && stroke.hasInputStartPoint)
							stroke.l0DrawPoints.push_back(stroke.inputStartPoint);
						if (LaserStrokeLayer* layer = FindLaserStrokeLayer(
							laserStrokeLayers, runtime->laserLayerId))
						{
							std::array<InkPoint, 1> downFallbackPoint = {};
							std::span<const InkPoint> coverageRealPoints(stroke.realPoints);
							if (stroke.realPoints.empty() && stroke.hasInputStartPoint)
							{
								downFallbackPoint[0] = stroke.inputStartPoint;
								coverageRealPoints = downFallbackPoint;
							}
							std::span<const InkPoint> coverageVisiblePoints(
								stroke.l0DrawPoints);
							const size_t expectedVisiblePointCount =
								coverageRealPoints.size() + stroke.predictedPoints.size();
							if (coverageVisiblePoints.size() < expectedVisiblePointCount)
							{
								runtime->rebuildPoints.assign(
									coverageRealPoints.begin(), coverageRealPoints.end());
								runtime->rebuildPoints.insert(runtime->rebuildPoints.end(),
									stroke.predictedPoints.begin(), stroke.predictedPoints.end());
								coverageVisiblePoints = runtime->rebuildPoints;
							}
							if (laserCoverageMode == LaserCoverageMode::Incremental)
							{
								RECT coverageDirty = {};
								const bool coverageUpdated = UpdateLaserIncrementalCoverage(
									*layer, coverageRealPoints, coverageVisiblePoints,
									configuration_.liveTipDurationSeconds +
									GetPredictionDurationSeconds(stroke), renderer_,
									configuration_.dpiScale, size.width, size.height,
									coverageDirty);
								UnionRectInPlace(frameDirty, coverageDirty);
								UnionRectInPlace(runtime->visibleDirty, coverageDirty);
								if (!coverageUpdated)
								{
									// 增量提交失败时不推进游标，清空 scratch 并锁定本批完整重绘。
									laserCoverageMode = LaserCoverageMode::FullRedraw;
									renderer_.ClearLaserIncrementalCoverage();
									layer->incrementalState = {};
									layer->stableBounds = {};
									layer->liveBounds = {};
									layer->bounds = RectFromLaserPoints(
										coverageVisiblePoints, configuration_.dpiScale,
										size.width, size.height);
									UnionRectInPlace(frameDirty, layer->bounds);
									UnionRectInPlace(runtime->visibleDirty, layer->bounds);
								}
							}
							else
							{
								const LaserLayerDirtyPlan dirtyPlan = PlanLaserLayerDirty(
									coverageRealPoints, coverageVisiblePoints,
									layer->incrementalState, layer->stableBounds,
									layer->liveBounds,
									configuration_.liveTipDurationSeconds +
									GetPredictionDurationSeconds(stroke),
									configuration_.dpiScale, size.width, size.height);
								layer->incrementalState.stableCommittedIndex =
									dirtyPlan.ranges.nextStableCommittedIndex;
								layer->incrementalState.rebuildRequired = false;
								layer->stableBounds = dirtyPlan.stableBounds;
								layer->liveBounds = dirtyPlan.liveBounds;
								layer->bounds = dirtyPlan.layerBounds;
								UnionRectInPlace(frameDirty, dirtyPlan.dirtyBounds);
								UnionRectInPlace(runtime->visibleDirty,
									dirtyPlan.dirtyBounds);
							}
							UnionRectInPlace(currentLaserLiveBounds, layer->bounds);
							UnionRectInPlace(runtime->visibleDirty, layer->bounds);
							if (!IsEmptyRect(layer->bounds)) runtime->metricVisible = true;
						}
					}
					else
					{
						stroke.predictedResults.clear();
						stroke.predictedPoints.clear();
						stroke.l0DrawPoints.clear();
					}
					if (particlesEnabledEffective && !runtime->ended)
					{
						const LaserParticleEmissionSource source =
							ResolveLaserParticleEmissionSource(*runtime);
						if (!source.valid)
						{
							// 首条有效切线出现前不累计静止基线，避免随后补出 Down 爆发。
							runtime->laserParticleFractionalEmission = 0.0f;
						}
						else
						{
							const uint32_t remainingBudget =
								spawnedLaserParticleCount <
								laserParticleConfiguration.maximumSpawnPerFrame
								? laserParticleConfiguration.maximumSpawnPerFrame -
									spawnedLaserParticleCount : 0;
							const float dpiScale = std::max(
								configuration_.dpiScale, 0.01f);
							// 只有真实输入速度决定密度；L0 prediction 只提供出生位置和切线。
							const float motionSpeedDipPerSecond =
								runtime->laserParticleMovedThisFrame
								? runtime->filteredInputSpeed / dpiScale : 0.0f;
							const LaserParticleEmissionSchedule schedule =
								ScheduleLaserParticleEmission(
									laserParticleWallDeltaSeconds,
									motionSpeedDipPerSecond,
									runtime->laserParticleFractionalEmission,
									remainingBudget, laserParticleConfiguration);
							runtime->laserParticleFractionalEmission =
								schedule.fractionalParticles;
							if (schedule.count > 0)
							{
								const LaserParticleEmissionRequest request = {
									source.positionX, source.positionY,
									source.tangentX, source.tangentY,
									source.entityRadius, schedule.count,
									MixLaserSeed(runtime->laserParticleSeed ^
										runtime->laserParticleSeedCursor *
										0x9E3779B9u) };
								laserParticleEmissionRequests.push_back(request);
								runtime->laserParticleSeedCursor += schedule.count;
								spawnedLaserParticleCount += schedule.count;
								const RECT particleBounds =
									ConservativeLaserParticleBatchBounds(
										request, laserParticleConfiguration,
										configuration_.dpiScale,
										kLaserCoreDiameterRatio);
								UnionRectInPlace(
									currentFrameLaserParticleBatchBounds,
									particleBounds);
							}
							// 超出 96 粒预算的整数发射额当帧丢弃，只保留不足一粒的小数。
						}
					}
					continue; // Laser 不写普通 L1/L0，也不进入后续 L2 完成路径。
				}
				if (runtime->awaitingReconnect && !runtime->reconnectVisualRefresh)
					continue; // 暂留候选保持上一帧 L0/L1，不制造脏区或重复 prediction。
				const bool eraser = runtime->tool == DrawingTool::Eraser;
				const bool hardPen = runtime->tool == DrawingTool::HardPen;
				const bool highlighter = runtime->tool == DrawingTool::Highlighter;
				if (!eraser)
					otherLiveLayerNeedsRebuild = true;
				// 保护窗口仍按配置 live-tip 时长推进 L1；taper 仅在非硬件压感普通笔上叠加。
				const double liveTipProtectionSeconds =
					eraser || highlighter || hardPen ? 0.0 : configuration_.liveTipDurationSeconds;
				const double liveTipTaperSeconds = eraser || highlighter || hardPen
					? 0.0
					: ResolveLiveTipTaperDurationSeconds(
						stroke.widthMode, configuration_.liveTipDurationSeconds);
				stroke.lastL0Rect = stroke.currentL0Rect;
				stroke.logicalInputTime = std::max(stroke.logicalInputTime,
					QpcDeltaSeconds(frameQpc.QuadPart, runtime->qpcOrigin, qpcFrequency));

				RECT stableDirty = {};
				if (!runtime->ended)
				{
					stroke.predictedResults.clear();
					if (runtime->awaitingReconnect)
					{
						if (!eraser && !stroke.endpointAdmission.active)
							stroke.predictedResults.assign(
								runtime->reconnectPredictedResults.begin(),
								runtime->reconnectPredictedResults.end());
					}
					else if (!eraser && !stroke.endpointAdmission.active &&
						kActivePredictionMode != InkPredictionMode::Disabled)
					{
						if (absl::Status status = stroke.modeler.Predict(stroke.predictedResults); !status.ok())
							stroke.predictedResults.clear();
					}
					RebuildPredictedPoints(stroke);
					stableDirty = stroke.endpointAdmission.active
						? RECT{}
						: eraser
						? CommitEraserRealPointsToL1(stroke, StrokeShape::RoundCapsule,
							renderer_, size.width, size.height)
						: CommitStablePrefixToL1(stroke, liveTipProtectionSeconds,
							GetPredictionDurationSeconds(stroke),
							ColorForTool(runtime->tool, runtime->visualStyle),
							StrokeShape::RoundCapsule, renderer_, size.width, size.height);
				}
				if (eraser)
				{
					stroke.predictedPoints.clear();
					stroke.l0DrawPoints.clear();
					stroke.currentL0Rect = {};
				}
				else if (!runtime->ended)
				{
					RebuildL0DrawPoints(stroke, liveTipTaperSeconds,
						StrokeShape::RoundCapsule, size.width, size.height);
				}
				else
				{
					// Up 后旧 prediction 仍基于上一真实末端，禁止把它接到新的最终点之后形成折返。
					stroke.predictedResults.clear();
					stroke.predictedPoints.clear();
					stroke.l0DrawPoints.clear();
					stroke.currentL0Rect = {};
				}
				if (!runtime->ended && !runtime->awaitingReconnect)
				{
					const bool modelSettled =
						(runtime->tool != DrawingTool::Pen &&
							runtime->tool != DrawingTool::HardPen) ||
						IsModeledTipSettled(LatestModeledTip(stroke),
							{ runtime->lastModelSnapshot.position.x,
								runtime->lastModelSnapshot.position.y },
							modeledTipFrameIntervalSeconds);
					UpdateIdleFreezeState(stroke, runtime->movedThisFrame,
						modelSettled, liveTipProtectionSeconds);
					publishPenDiagnostics(*runtime, stroke.l0DrawPoints);
				}
				else if (!runtime->ended && runtime->awaitingReconnect && runtime->reconnectVisualRefresh)
				{
					// 发布候选实际 L0；验收须比较等待态与完成态，而非重复读取完成快照。
					publishPenDiagnostics(*runtime, stroke.l0DrawPoints);
				}
				if constexpr (kInterruptedStrokeReconnectManualTestModeEnabled)
				{
					if (!runtime->ended)
						UnionRectInPlace(stableDirty,
							DrawReconnectManualTestRanges(*runtime, renderer_, size.width, size.height));
				}

				UnionRectInPlace(frameDirty, stableDirty);
				UnionRectInPlace(frameDirty, stroke.lastL0Rect);
				UnionRectInPlace(frameDirty, stroke.currentL0Rect);
				UnionRectInPlace(runtime->visibleDirty, stableDirty);
				UnionRectInPlace(runtime->visibleDirty, stroke.lastL0Rect);
				UnionRectInPlace(runtime->visibleDirty, stroke.currentL0Rect);
				if (!IsEmptyRect(stableDirty) || !IsEmptyRect(stroke.currentL0Rect))
					runtime->metricVisible = true;
				runtime->reconnectVisualRefresh = false;
			}
			if (spawnedLaserParticleCount > 0 &&
				!IsEmptyRect(currentFrameLaserParticleBatchBounds))
			{
				// 同一帧的全部请求共享一次未裁剪包络；下一帧不会延长本批次寿命。
				laserParticleDirtyTracker.Add(
					currentFrameLaserParticleBatchBounds,
					LaserParticleLifetimeDeadlineQpc(
						frameQpc.QuadPart, qpcFrequency,
						laserParticleConfiguration));
				RequireLaserMinimumHold(laserLifecycle,
					laserParticleConfiguration.maximumLifetimeSeconds);
			}
			if (hasLaserRuntime || !laserStrokeLayers.empty())
			{
				currentLaserLiveBounds = {};
				for (LaserStrokeLayer& layer : laserStrokeLayers)
				{
					const std::vector<InkPoint>& points = LaserStrokeLayerPoints(layer);
					if (!ShouldCompositeLaserLayer(layer.cancelled, points.size()) ||
						IsEmptyRect(layer.bounds)) continue;
					UnionRectInPlace(currentLaserLiveBounds, layer.bounds);
				}
				laserLiveBounds = currentLaserLiveBounds;
			}
			if (ShouldBakeLaserBatch(laserLifecycle, laserStrokeLayers.size()))
			{
				// 同批次最后一支抬起后一次性烘干，随后 Hold/Fade 只解析稳定颜色。
				UnionRectInPlace(frameDirty, previousLaserLiveBounds);
				BakeLaserStrokeLayers(laserStrokeLayers, renderer_,
					configuration_.dpiScale, size.width, size.height, laserStableBounds,
					frameDirty, laserCoverageMode);
				UnionRectInPlace(frameDirty, laserLiveBounds);
				laserLiveBounds = {};
				laserCoverageMode = LaserCoverageMode::Inactive;
			}

			const bool shouldStepLaserParticles = particlesEnabledEffective &&
				(laserParticleSnapshot.hasActive || laserParticleSnapshot.expiredAny ||
					!laserParticleEmissionRequests.empty());
			const bool shouldDrawLaserParticles = particlesEnabledEffective &&
				(laserParticleSnapshot.hasActive ||
					!laserParticleEmissionRequests.empty());
			if (shouldStepLaserParticles)
			{
				renderer_.StepLaserParticles(
					laserParticleWallDeltaSeconds, laserParticleWallDeltaSeconds,
					laserParticleSnapshot.hasActive || laserParticleSnapshot.expiredAny,
					laserParticleEmissionRequests);
				lastLaserParticleSimulationQpc = frameQpc.QuadPart;
				// 刚到期批次仍提交最后一次 update，但不再绘制退化实例。
			}

			if (hasEndedStroke)
			{
				renderer_.ClearOperatorLayer(renderer_.layerL1);
				renderer_.ClearOperatorLayer(renderer_.layerL0);
				for (RuntimeStroke* runtime : active)
				{
					if (!runtime->ended) continue;
					if (runtime->tool == DrawingTool::Laser)
					{
						UnionRectInPlace(frameDirty, runtime->visibleDirty);
						continue; // Laser 稳定预乘颜色层保留到生命周期结束，永不 resolve 到 L2。
					}
					if (!runtime->cancelled)
					{
						const std::optional<StoredInkStyle> style =
							StoredStyleForTool(runtime->tool, runtime->visualStyle);
						const double completedTipTaperSeconds = runtime->tool == DrawingTool::Pen
							? ResolveLiveTipTaperDurationSeconds(runtime->stroke.widthMode,
								configuration_.liveTipDurationSeconds) : 0.0;
						std::optional<InkStroke> finalizedStroke;
						if (style)
						{
							finalizedStroke = runtime->shape.active
								? FinalizeStoredShape(runtime->shape.primitive, *style,
									runtime->viewport.x, runtime->viewport.y)
								: FinalizeStoredStroke(runtime->stroke, *style,
									completedTipTaperSeconds, runtime->rebuildPoints,
									runtime->viewport.x, runtime->viewport.y);
							publishPenDiagnostics(*runtime, runtime->rebuildPoints);
						}
						InkPage* page = document_ ? document_->PageAt(currentPageIndex_) : nullptr;
						InkCanvas* canvas = page
							? page->FindCanvas(kDefaultDeviceKey) : nullptr;
						const std::optional<size_t> strokeIndex = finalizedStroke && canvas
							? canvas->AppendStroke(std::move(*finalizedStroke)) : std::nullopt;
						if (strokeIndex)
						{
							CanvasPageRuntimeState& pageRuntime =
								pageRuntimeStates[currentPageIndex_];
							// Stored Stroke 已改变文档分支，后续失败也不能恢复旧 redo 候选。
							pageRuntime.history.DiscardRedoBranch();
							// 文档对象先成为真值，再从刚追加的同一 Stroke 完成首次 L2 绘制。
							const std::span<const InkStroke> strokes = canvas->Strokes();
							const InkStroke& storedStroke = strokes[*strokeIndex];
							std::optional<StrokeTileFootprint> footprint =
								BuildStrokeTileFootprint(storedStroke);
							const std::optional<RenderItemId> renderItem = footprint
								? pageRuntime.history.AppendStroke(
									*strokeIndex, std::move(*footprint), true) : std::nullopt;
							HotPreimageCaptureResult preimageCapture;
							InkRasterStateToken afterState = pageRuntime.rasterState;
							if (renderItem && renderItem->index == pageRuntime.beforeStates.size())
							{
								// 进入 runtime history 即成为“有内容”，GPU 呈现失败不回滚文档真值。
								markPresentationMutation();
								publishCurrentPageContent();
								const InkRasterStateToken beforeState = pageRuntime.rasterState;
								afterState = allocateRasterStateToken();
								pageRuntime.beforeStates.push_back(beforeState);
								pageRuntime.afterStates.push_back(afterState);
								// Runtime history 先登记成功，随后才允许产生可见 L2 像素。
								renderer_.ClearOperatorLayer(renderer_.layerL1);
								renderer_.ClearOperatorLayer(renderer_.layerL0);
								const StoredStrokeRasterTarget storedStrokeTarget = {
									&renderer_.layerL1, canvas->Viewport().x,
									canvas->Viewport().y, size.width, size.height
								};
								const StoredStrokeRasterResult completedStrokeRaster =
									DrawStoredStroke(storedStroke,
									renderer_, storedStrokeTarget,
									runtime->rebuildPoints, completedHighlighterScratch);
								RECT completedStrokeDirty = completedStrokeRaster.dirty;
								const RenderItemState* addedItem = pageRuntime.history.Find(*renderItem);
								if (addedItem && completedStrokeRaster.succeeded)
								{
									preimageCapture = historyGpuCache.CapturePreimage({
										{ page->PageGuid(), kDefaultDeviceKey },
										*renderItem,
										currentRasterKey(),
										beforeState,
										afterState,
										addedItem->undoTiles,
										canvas->Viewport().x,
										canvas->Viewport().y,
										size.width,
										size.height
									});
									if ((renderItem->index + 1) % kCompositionLeafItemCount == 0)
									{
										const std::optional<CompositionNodeId> leaf =
											pageRuntime.history.CompositionTree().LeafNodeForItem(
												renderItem->index);
										std::vector<SignedTileCoordinate> leafTiles;
										if (leaf)
										{
											const RenderItemRange range =
												pageRuntime.history.CompositionTree().NodeItemRange(*leaf);
											const std::span<const RenderItemState> items =
												pageRuntime.history.Items();
											for (size_t index = range.begin;
												index < std::min(range.end, items.size()); ++index)
											{
												leafTiles.insert(leafTiles.end(),
													items[index].compositionTiles.begin(),
													items[index].compositionTiles.end());
											}
											std::sort(leafTiles.begin(), leafTiles.end());
											leafTiles.erase(std::unique(
												leafTiles.begin(), leafTiles.end()), leafTiles.end());
											for (SignedTileCoordinate tile : leafTiles)
											{
												if (compositionMaintenance.size() ==
													kMaximumCompositionMaintenanceItems)
													compositionMaintenance.pop_front();
												compositionMaintenance.push_back({
													currentPageIndex_, *leaf, tile,
													rasterPipelineGeneration });
											}
										}
									}
								}
								if constexpr (kInterruptedStrokeReconnectManualTestModeEnabled)
									UnionRectInPlace(completedStrokeDirty,
										DrawReconnectManualTestRanges(
											*runtime, renderer_, size.width, size.height));
								completedStrokeDirty = ClampRectToCanvas(
									completedStrokeDirty, size.width, size.height);
								bool submitted = completedStrokeRaster.succeeded;
								if (submitted && !IsEmptyRect(completedStrokeDirty))
								{
									// 每条 Stroke 独立 resolve，保留高亮透明度和擦除的文档顺序。
									submitted = renderer_.ApplyOperatorLayers(renderer_.layerL2RTV.Get(),
										renderer_.layerL1, renderer_.layerL0,
										completedStrokeDirty);
									if (submitted)
									{
										UnionRectInPlace(frameDirty, completedStrokeDirty);
										runtime->metricVisible = true;
									}
								}
								pageRuntime.rasterState = afterState;
								pageRuntime.clearRedoAvailable = false;
								if (preimageCapture.status == HotPreimageCaptureStatus::Captured)
								{
									if (!submitted ||
										!historyGpuCache.CommitPreimage(preimageCapture.ticket))
										historyGpuCache.CancelPreimage(preimageCapture.ticket);
								}
								if (!submitted)
								{
									viewportVisibleClear =
										CanvasVisibleClarityAfterAuthoritativeWrite(
											viewportVisibleClear, false);
									viewportRefreshPending = true;
									viewportRefreshClearsTransient = false;
									std::cout << "[InkHistory] stored stroke raster failed page=" <<
										(currentPageIndex_ + 1) << " item=" << *strokeIndex <<
										std::endl;
								}
							}
							else
							{
								std::cout << "[InkHistory] failed to append render item page=" <<
									(currentPageIndex_ + 1) << " stroke=" << *strokeIndex <<
									std::endl;
							}
						}
						else
						{
							std::cout << "Failed to append completed stroke to the current ink canvas."
								<< std::endl;
						}
					}
					UnionRectInPlace(frameDirty, runtime->visibleDirty);
				}
				UnionRectInPlace(frameDirty,
					RebuildActiveLayers(active, renderer_, size.width, size.height,
						shapePrimitiveScratch));

				std::erase_if(active, [&](RuntimeStroke* runtime)
					{
						if (!runtime->ended) return false;
						if (!runtime->cancelled && observer_.strokeCompleted)
							observer_.strokeCompleted(observer_.context,
								CompletedStrokeKindForTool(runtime->tool));
						if (metrics_ && runtime->metricVisible && !runtime->cancelled)
							metrics_->StageLanding(runtime->handle.record, runtime->handle.generation,
								runtime->metricDeviceType, static_cast<uint32_t>(runtime->tool),
								runtime->metricEligibleQpc);
						handBackSpeedEraserController(*runtime);
						input_.Recycle(runtime->handle); // L2 提交与活动层重建完成后才归还 slot。
						runtime->stroke.Reset(kPenDiameter, configuration_.expectedSpeed);
						runtime->handle = {};
						runtime->selectedTool = DrawingTool::Pen;
						runtime->tool = DrawingTool::Pen;
						runtime->visualStyle = {};
						runtime->eraserWidthMode = EraserWidthMode::Fixed;
						runtime->eraserWidthModeRevision = 0;
						runtime->suppressPressure = false;
						runtime->lastInputSnapshot = {};
						runtime->invertedCursor = false;
						runtime->hapticEligible = false;
						runtime->visibleDirty = {};
						runtime->ended = false;
						runtime->cancelled = false;
						runtime->awaitingReconnect = false;
						runtime->reconnectVisualRefresh = false;
						runtime->deferredUpSnapshot = {};
						runtime->reconnectDirection = {};
						runtime->reconnectDeadlineQpc = 0;
						runtime->reconnectPredictedResults.clear();
						runtime->reconnectManualTestRanges.clear();
						runtime->speedEraserDisplayScale = {};
						runtime->speedEraserDeviceMode = SpeedEraser::DeviceMode::Laptop;
						runtime->speedEraserModelTime = 0.0;
						runtime->speedEraserModelDiameter = SpeedEraser::Config{}.minimumDiameterPx;
						runtime->shape.Reset();
						runtime->viewport = {};
						runtime->laserParticleSeed = 0;
						runtime->laserLayerId = 0;
						ResetLaserParticleEmitterState(*runtime);
						runtime->metricVisible = false;
						runtime->metricEligibleQpc = 0;
						runtime->inUse = false;
						return true;
					});
			}
			else if (!active.empty())
			{
				const bool hasActiveShape = std::any_of(active.begin(), active.end(),
					[](const RuntimeStroke* runtime)
					{
						return runtime && !runtime->ended && runtime->shape.active;
					});
				if (ShouldRebuildSharedL0(hasActiveShape,
					shapeLayerNeedsRebuild, otherLiveLayerNeedsRebuild))
				{
					// 只有 Shape-only 且末点稳定时才能保留；共享层一旦重建必须重放全部活动 Shape。
					renderer_.ClearOperatorLayer(renderer_.layerL0);
					for (RuntimeStroke* runtime : active)
					{
						if (runtime->tool != DrawingTool::Eraser &&
							runtime->tool != DrawingTool::Laser && !runtime->shape.active &&
							!runtime->stroke.l0DrawPoints.empty())
							DrawL0LiveComposite(runtime->stroke,
								ColorForTool(runtime->tool, runtime->visualStyle),
								StrokeShape::RoundCapsule, renderer_, false);
					}
					DrawActiveShapePrimitives(active, renderer_, shapePrimitiveScratch);
				}
			}

			if (haptics_)
			{
				RuntimeStroke* hapticRuntime = nullptr;
				for (RuntimeStroke* runtime : active)
				{
					if (runtime && runtime->hapticEligible &&
						!runtime->ended && !runtime->awaitingReconnect)
					{
						hapticRuntime = runtime;
						break;
					}
				}
				if (hapticRuntime)
				{
					hapticContinuousActive = haptics_->TickContinuous(
					HapticFeedbackForRuntime(*hapticRuntime)) ||
						hapticContinuousActive;
				}
				else if (hapticContinuousActive)
				{
					haptics_->StopFeedback();
					hapticContinuousActive = false;
				}
			}

			if (active.empty())
				processCanvasCommands(frameDirty, laserParticleSnapshot,
					forceFullPresent, size.width, size.height);

			RECT currentLaserParticleUnclippedBounds =
				laserParticleSnapshot.activeBounds;
			UnionRectInPlace(currentLaserParticleUnclippedBounds,
				currentFrameLaserParticleBatchBounds);
			currentLaserParticleBounds = ClampRectToCanvas(
				currentLaserParticleUnclippedBounds, size.width, size.height);
			if (!IsEmptyRect(previousLaserParticleBounds) ||
				!IsEmptyRect(currentLaserParticleBounds))
			{
				// Tracker 保留未裁剪批次；每帧按当前画布裁剪，resize 后仍覆盖存量粒子。
				UnionRectInPlace(frameDirty, previousLaserParticleBounds);
				UnionRectInPlace(frameDirty, currentLaserParticleBounds);
			}

			buildDrawingCursorVisuals();
			const RECT currentLaserTipBounds = RectFromLaserDots(
				laserTipVisuals, configuration_.dpiScale, size.width, size.height);
			const bool laserTipBoundsChanged =
				previousLaserTipBounds.left != currentLaserTipBounds.left ||
				previousLaserTipBounds.top != currentLaserTipBounds.top ||
				previousLaserTipBounds.right != currentLaserTipBounds.right ||
				previousLaserTipBounds.bottom != currentLaserTipBounds.bottom;
			if (drawingCursorRequested || laserTipBoundsChanged)
			{
				UnionRectInPlace(frameDirty, previousLaserTipBounds);
				UnionRectInPlace(frameDirty, currentLaserTipBounds);
			}
			else if (!IsEmptyRect(frameDirty))
			{
				UnionRectInPlace(frameDirty, currentLaserTipBounds);
			}
			if (laserOpacityChanged || laserLifecycle.phase == LaserTrailPhase::Fade)
			{
				UnionRectInPlace(frameDirty, laserStableBounds);
				UnionRectInPlace(frameDirty, laserLiveBounds);
			}
			if (!cursorVisualsEquivalent())
			{
				// 先重建旧区清除上一帧，再把全部当前 visual 绘制到 backbuffer 最上层。
				UnionRectInPlace(frameDirty, cursorVisualBounds(previousCursorVisuals));
				UnionRectInPlace(frameDirty, cursorVisualBounds(currentCursorVisuals));
			}
			else if (!IsEmptyRect(frameDirty) && !currentCursorVisuals.empty())
			{
				// 其他几何触发 Present 时也要先重建光标区，避免半透明像素在旧 backbuffer 上重复叠加。
				UnionRectInPlace(frameDirty, cursorVisualBounds(currentCursorVisuals));
			}
			frameDirty = ClampRectToCanvas(frameDirty, size.width, size.height);
			if (forceFullPresent) frameDirty = GetFullCanvasRect(size.width, size.height);
			if (metrics_)
			{
				for (RuntimeStroke* runtime : active)
				{
					if (runtime && runtime->metricVisible && !runtime->cancelled)
						metrics_->StageLanding(runtime->handle.record, runtime->handle.generation,
							runtime->metricDeviceType, static_cast<uint32_t>(runtime->tool),
							runtime->metricEligibleQpc);
				}
			}
			bool presentSucceeded = false;
			if (!IsEmptyRect(frameDirty))
			{
				const bool orderedPreview = frameTool == DrawingTool::Pen &&
					kActiveDebugLayerColorMode == DebugLayerColorMode::ColorizeLiveLayer;
				const InkPage* snapshotPage = document_
					? document_->PageAt(currentPageIndex_) : nullptr;
				const InkCanvas* snapshotCanvas = snapshotPage
					? snapshotPage->FindCanvas(kDefaultDeviceKey) : nullptr;
				const uint64_t snapshotRevision = currentPageIndex_ < pageRuntimeStates.size()
					? pageRuntimeStates[currentPageIndex_].history.Revision() : 0;
				if (viewportVisibleClear && snapshotCanvas &&
					(!trustedSnapshotSignatureValid ||
						trustedSnapshotPageIndex != currentPageIndex_ ||
						trustedSnapshotRevision != snapshotRevision ||
						trustedSnapshotViewport.x != snapshotCanvas->Viewport().x ||
						trustedSnapshotViewport.y != snapshotCanvas->Viewport().y))
				{
					if (renderer_.RefreshTrustedL2Snapshot(
						snapshotCanvas->Viewport().x, snapshotCanvas->Viewport().y))
					{
						trustedSnapshotSignatureValid = true;
						trustedSnapshotPageIndex = currentPageIndex_;
						trustedSnapshotRevision = snapshotRevision;
						trustedSnapshotViewport = snapshotCanvas->Viewport();
					}
				}
				if (!viewportVisibleClear && snapshotCanvas)
				{
					renderer_.CompositeTrustedL2SnapshotToBackBuffer({
						snapshotCanvas->Viewport().x, snapshotCanvas->Viewport().y,
						panMotion.velocity.x, panMotion.velocity.y,
						CanvasPanFallbackBlurDip(CanvasPanSpeed(panMotion)),
						configuration_.dpiScale, frameDirty });
					const OperatorLayerMergeMode mergeMode = orderedPreview
						? OperatorLayerMergeMode::Ordered
						: OperatorLayerMergeMode::CoverageUnion;
					renderer_.ApplyOperatorLayers(renderer_.backBufferRTV.Get(),
						renderer_.layerL1, renderer_.layerL0, frameDirty, mergeMode);
				}
				else CompositeLayersToBackBuffer(frameDirty, orderedPreview);
				// 粒子先于激光主体绘制，使粒子辉光托衬在墨迹主体下方，避免遮挡演示内容。
				if (shouldDrawLaserParticles)
				{
					ConfigureLaserRendererStyle(renderer_, laserTrailVisualStyle,
						configuration_.dpiScale);
					renderer_.DrawLaserParticles();
				}
				if (laserLifecycle.phase != LaserTrailPhase::Inactive && laserOpacity > 0.0f)
				{
					renderer_.ResolveLaserCompositedColor(
						renderer_.backBufferRTV.Get(), frameDirty, laserOpacity);
					DrawLaserStrokeLayers(laserStrokeLayers, renderer_,
						renderer_.backBufferRTV.Get(), frameDirty,
						configuration_.dpiScale,
						laserCoverageMode);
				}
				for (const LaserTipVisual& visual : laserTipVisuals)
				{
					ConfigureLaserRendererStyle(renderer_, visual.visualStyle,
						configuration_.dpiScale);
					renderer_.DrawLaserDots(
						std::span<const LaserDot>(&visual.dot, 1));
				}
				for (const DrawingCursorVisual& visual : currentCursorVisuals)
					renderer_.DrawTransientDrawingCursor(visual);
				presentSucceeded = PresentFrame(
					frameDirty, forceFullPresent); // 一帧最多一次 backbuffer 合成和一次 Present。
			}
			if (cursorVisualDiagnosticRecorded)
				RecordCursorDiagnostic("present success=%u visuals=%zu laserTips=%zu dirty=(%ld,%ld,%ld,%ld)",
					presentSucceeded ? 1u : 0u, currentCursorVisuals.size(),
					laserTipVisuals.size(), frameDirty.left, frameDirty.top,
					frameDirty.right, frameDirty.bottom);
			if (presentSucceeded)
			{
				contentRevisionNeedsPresent = false;
				// 文稿身份只能在目标 CPU 文档完成重放并成功提交一帧后成为 ready。
				if (!viewportRecoveryPending && !viewportRefreshPending)
					publishWorkspaceReadyAfterPresent();
				if (selectionMode &&
					presentation_.RequestedOutputTarget() ==
						TransparentOutputTarget::SelectionUlw)
				{
					const TransparentPresentObservation observation =
						presentation_.LastPresentObservation();
					if (observation.fullFrameAllZeroAlpha)
						auxiliaryCleanVerificationPending = false;
					else if (!observation.updatedRegionAllZeroAlpha)
						auxiliaryCleanVerificationPending = true;
				}
			}
			previousCursorVisuals = currentCursorVisuals;
			previousLaserParticleBounds = currentLaserParticleBounds;
			previousLaserTipBounds = currentLaserTipBounds;
			if (metrics_)
			{
				LARGE_INTEGER presentQpc = {};
				QueryPerformanceCounter(&presentQpc);
				metrics_->CommitStagedLandings(presentSucceeded, presentQpc.QuadPart);
			}

			const double canvasFrameElapsedMilliseconds =
				GetQpcTimeMilliseconds() - frameStartMs;
			previousCanvasFrameWorkMilliseconds = (std::max)(0.0,
				canvasFrameElapsedMilliseconds - lastPresentDurationMs_);
			previousCanvasPresentMilliseconds = lastPresentDurationMs_;
			// Up/Cancel 的 Stored 提交与 active 回收完成后再发布最终 1→0。
			reconcileDrawingActivity();
			FlushCursorDiagnostics();
			const bool hasPhysicalContactAfterFrame = HasPhysicalContact(active);
			if (metrics_ && !hasPhysicalContactAfterFrame)
				metrics_->EndActiveFrameSequence();
			const bool eraserIdle = hasPhysicalContactAfterFrame && !navigationActive &&
				std::all_of(active.begin(),active.end(),[&](const RuntimeStroke* r)
				{
					return r && (r->ended || r->awaitingReconnect ||
						(r->stroke.widthMode==StrokeWidthMode::SpeedEraser &&
						 !r->speedEraserOc.NeedsAnimation(mouseVisualSeconds) &&
						 r->speedEraserOc.SecondsSinceMovement(mouseVisualSeconds)>=r->speedEraserOc.Configuration().idleStartSeconds));
				});
			if (hasPhysicalContactAfterFrame && !eraserIdle)
			{
				const double workMs = GetQpcTimeMilliseconds() - frameStartMs;
				if (metrics_ && frameHadActiveContact)
					metrics_->RecordActiveFrame(frameStartMs, workMs,
						lastPresentDurationMs_, lastPresentSucceeded_);
				const double remainingFrameBudgetMs =
					1000.0 / configuration_.timingProfile.target_fps - workMs;
				input_.WaitForFrameDeadline(remainingFrameBudgetMs);
				size_t committedCount = 0;
				size_t realPointCount = 0;
				size_t predictedPointCount = 0;
				size_t l0PointCount = 0;
				bool allIdleFrozen = true;
				for (const RuntimeStroke* runtime : active)
				{
					committedCount += runtime->stroke.committedIndex;
					realPointCount += runtime->stroke.realPoints.size();
					predictedPointCount += runtime->stroke.predictedPoints.size();
					l0PointCount += runtime->stroke.l0DrawPoints.size();
					allIdleFrozen = allIdleFrozen && runtime->stroke.idleFrozen;
				}
				LogFrameTiming(committedCount, realPointCount, predictedPointCount,
					l0PointCount, workMs, previousFrameMs, allIdleFrozen); // Debug 输出全部活动 contact 的聚合帧率。
				lastActiveFrameStartMs = frameStartMs;
			}
			else if (navigationActive || speedEraserHoverAnimating ||
				mouseSpeedEraser().NeedsAnimation(mouseVisualSeconds) ||
				laserLifecycle.phase == LaserTrailPhase::Fade ||
				laserParticleSnapshot.hasActive)
			{
				const double workMs = GetQpcTimeMilliseconds() - frameStartMs;
				const double remainingFrameBudgetMs =
					1000.0 / configuration_.timingProfile.target_fps - workMs;
				input_.WaitForFrameDeadline(remainingFrameBudgetMs);
				lastActiveFrameStartMs = frameStartMs;
			}
			else if (!active.empty())
			{
				lastActiveFrameStartMs = 0.0;
				int64_t nearestDeadlineQpc = (std::numeric_limits<int64_t>::max)();
				for (const RuntimeStroke* runtime : active)
				{
					if (runtime && runtime->awaitingReconnect)
						nearestDeadlineQpc = std::min(
							nearestDeadlineQpc, runtime->reconnectDeadlineQpc);
					else if(runtime && !runtime->ended && runtime->stroke.widthMode==StrokeWidthMode::SpeedEraser)
					{
						const double wake=runtime->speedEraserOc.NextAreaWakeSeconds();
						if(wake>0 && wake<(std::numeric_limits<int64_t>::max)()/static_cast<double>(qpcFrequency))
							nearestDeadlineQpc=std::min(nearestDeadlineQpc,static_cast<int64_t>(wake*qpcFrequency)+1);
					}
				}
				LARGE_INTEGER waitStartQpc = {};
				QueryPerformanceCounter(&waitStartQpc);
				const double timeoutMilliseconds = nearestDeadlineQpc ==
					(std::numeric_limits<int64_t>::max)()
					? 0.0
					: QpcDeltaSeconds(nearestDeadlineQpc,
						waitStartQpc.QuadPart, qpcFrequency) * 1000.0;
				if (metrics_) metrics_->BeginIdle(GetQpcTimeMilliseconds());
				if(eraserIdle && nearestDeadlineQpc==(std::numeric_limits<int64_t>::max)())
				{
					// 现有API的0是轮询；收敛后只等唤醒，不因按住而重新请求帧。
					while(!window_.ExitRequested() && !input_.WaitForWake(frameWakeGeneration,1000.0)) {}
				}
				else input_.WaitForWake(frameWakeGeneration, timeoutMilliseconds);
				if (metrics_) metrics_->EndIdle(GetQpcTimeMilliseconds());
			}
		}

		if (metrics_) metrics_->EndIdle(GetQpcTimeMilliseconds());
		FlushCursorDiagnostics();
		if (haptics_) haptics_->StopFeedback();
		if (drawingPriorityRaised)
			SetThreadPriority(GetCurrentThread(), originalThreadPriority);
	}

}
