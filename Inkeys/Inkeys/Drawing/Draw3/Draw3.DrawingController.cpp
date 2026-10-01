module;
#include "Assets/EraserGripVisual.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <compare>
#include <cstdint>
#include <cstdio>
#include <cmath>
#include <cstring>
#include <deque>
#include <d3d11.h>
#include <exception>
#include <DirectXMath.h>
#include <iostream>
#include <initializer_list>
#include <json/json.h>
#include <ink_stroke_modeler/stroke_modeler.h>
#include <limits>
#include <memory>
#include <map>
#include <new>
#include <mutex>
#include <optional>
#include <set>
#include <span>
#include <string>
#include <thread>
#include <type_traits>
#include <utility>
#include <vector>
#include <dxgiformat.h>
#include <windows.h>
#include <mmsystem.h>

#include "Draw3.Bridge.h"
#include "Draw3.PptTiming.h"
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
		template<class Process>
		void DrainIngressBatch(ContactInputCoordinator& input, WindowController& window,
			bool& commandBoundaryPending, Process&& process)
		{
			if (commandBoundaryPending) return;
			ContactRecord* record = nullptr;
			while (input.TryDequeue(record))
			{
				process(record);
				if (!record && window.HasPendingCanvasCommand())
				{
					// 命令必须在后续 Down 前处理；普通无命令唤醒不阻断新输入。
					commandBoundaryPending = true;
					break;
				}
			}
		}

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
			std::uint64_t slotGeneration = 0;
		};

		struct PresentationLaneKey
		{
			Bridge::PresentationKey key;
			Bridge::SlideBindingMode bindingMode =
				Bridge::SlideBindingMode::PageIndexFallback;
			friend auto operator<=>(const PresentationLaneKey&,
				const PresentationLaneKey&) noexcept = default;
		};

		using PresentationParkedSlots = std::map<PresentationLaneKey,
			DrawingDocumentSlot>;

		PresentationLaneKey LaneFor(const Bridge::PresentationTarget& target) noexcept
		{
			return { target.key, target.bindingMode };
		}

		using RetainedPresentationSlides =
			std::map<std::int32_t, draw3::uink::Draw3UInkCanvasSnapshot>;

		struct ActivePresentationSlotRefs
		{
			std::optional<InkCanvasCollection>& document;
			std::vector<CanvasPageRuntimeState>& pageRuntimeStates;
			RetainedPresentationSlides& retainedSlides;
			std::size_t& currentPageIndex;
			std::optional<Bridge::PresentationTarget>& target;
			std::optional<draw3::uink::UInkGuid>& fileGuid;
			std::uint64_t& mutationRevision;
			std::uint64_t& queuedRevision;
			std::uint64_t& committedRevision;
			bool& persistenceInitialized;
			bool& loadPending;
			std::uint64_t& slotGeneration;
		};

		void SwapActiveDocumentSlot(ActivePresentationSlotRefs active,
			DrawingDocumentSlot& parked) noexcept
		{
			using std::swap;
			swap(active.document, parked.document);
			swap(active.pageRuntimeStates, parked.pageRuntimeStates);
			swap(active.retainedSlides, parked.retainedSlides);
			swap(active.currentPageIndex, parked.currentPageIndex);
			swap(active.target, parked.presentationTarget);
			swap(active.fileGuid, parked.fileGuid);
			swap(active.mutationRevision, parked.mutationRevision);
			swap(active.queuedRevision, parked.queuedRevision);
			swap(active.committedRevision, parked.committedRevision);
			swap(active.persistenceInitialized, parked.persistenceInitialized);
			swap(active.loadPending, parked.loadPending);
			swap(active.slotGeneration, parked.slotGeneration);
		}

		template <typename AllocateToken>
		bool TryCreateBlankDocumentSlot(DrawingDocumentSlot& slot,
			std::size_t pageCount, AllocateToken&& allocateRasterStateToken)
		{
			if (slot.document) return true;
			try
			{
				InkGuid workspaceGuid;
				if (!TryCreateInkGuid(workspaceGuid)) return false;
				InkCanvasCollection created(workspaceGuid);
				std::vector<CanvasPageRuntimeState> runtimes;
				const std::size_t count = (std::max)(std::size_t{ 1 }, pageCount);
				runtimes.reserve(count);
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
			}
			catch (...)
			{
				return false;
			}
		}

		struct PresentationCpuSwitchResult
		{
			bool accepted = false;
			bool topologyConflict = false;
			std::optional<Bridge::PresentationTarget> preparedTarget;
		};

		template <typename AllocateToken>
		PresentationCpuSwitchResult SwitchPresentationCpuSlot(
			Bridge::Workspace& activeWorkspace,
			std::optional<Bridge::PresentationKey>& activeKey,
			ActivePresentationSlotRefs active, DrawingDocumentSlot& source,
			DrawingDocumentSlot& isolated,
			PresentationParkedSlots& parkedSlots,
			std::uint64_t& nextSlotGeneration,
			const Bridge::PresentationTarget& target,
			AllocateToken&& allocateRasterStateToken)
		{
			static_assert(std::is_nothrow_move_assignable_v<DrawingDocumentSlot>);
			PresentationCpuSwitchResult result;
			try { result.preparedTarget.emplace(target); }
			catch (...) { return result; }
			const bool sameLane = activeWorkspace == Bridge::Workspace::Presentation &&
				activeKey && *activeKey == target.key && active.target &&
				active.target->bindingMode == target.bindingMode;
			const auto previousLane = activeKey && active.target
				? std::optional<PresentationLaneKey>(LaneFor(*active.target))
				: std::nullopt;
			bool topologyConflict = sameLane &&
				!CanReusePresentationDocumentSlot(*active.target, target,
					active.document ? active.document->Pages().size() : 0);
			if (!sameLane || topologyConflict)
			{
				DrawingDocumentSlot* destination = nullptr;
				bool inserted = false;
				if (!topologyConflict)
				{
					try
					{
						auto [found, wasInserted] = parkedSlots.try_emplace(LaneFor(target));
						inserted = wasInserted;
						destination = &found->second;
					}
					catch (...) { return {}; }
					if (destination->presentationTarget &&
						!CanReusePresentationDocumentSlot(*destination->presentationTarget,
							target, destination->document
								? destination->document->Pages().size()
								: Bridge::PresentationDocumentPageCount(target)))
					{
						topologyConflict = true;
						destination = nullptr;
					}
				}
				DrawingDocumentSlot prepared;
				if (topologyConflict)
				{
					if (!TryCreateBlankDocumentSlot(prepared,
						Bridge::PresentationDocumentPageCount(target),
						allocateRasterStateToken)) return {};
				}
				else if (!destination->document)
				{
					if (!TryCreateBlankDocumentSlot(prepared,
						Bridge::PresentationDocumentPageCount(target),
						allocateRasterStateToken) || nextSlotGeneration == 0 ||
						nextSlotGeneration == UINT64_MAX)
					{
						if (inserted) parkedSlots.erase(LaneFor(target));
						return {};
					}
					try { prepared.presentationTarget = *result.preparedTarget; }
					catch (...)
					{
						if (inserted) parkedSlots.erase(LaneFor(target));
						return {};
					}
					prepared.slotGeneration = nextSlotGeneration++;
					*destination = std::move(prepared);
				}
				else if (destination->slotGeneration == 0) return {};
				// 完整候选与 target 复制先成功，再以不抛异常的成组 swap 换权威槽。
				SwapActiveDocumentSlot(active, source);
				if (topologyConflict)
				{
					isolated = std::move(prepared);
					destination = &isolated;
					std::fputs("[Draw3.Presentation] action=switch result=isolated reason=topology_conflict\n",
						stderr);
				}
				SwapActiveDocumentSlot(active, *destination);
				if (previousLane && source.document &&
					ShouldEvictPresentationSlot(source.fileGuid.has_value(),
						source.mutationRevision, source.queuedRevision,
						source.committedRevision, source.loadPending))
					parkedSlots.erase(*previousLane);
				activeKey = topologyConflict
					? std::nullopt : std::optional<Bridge::PresentationKey>(target.key);
			}
			activeWorkspace = Bridge::Workspace::Presentation;
			if (topologyConflict) active.target.reset();
			result.accepted = true;
			result.topologyConflict = topologyConflict;
			return result;
		}

		struct PresentationCompletionRoute
		{
			bool active = false;
			DrawingDocumentSlot* parked = nullptr;
			std::optional<PresentationLaneKey> parkedLane;
		};

		PresentationCompletionRoute RoutePresentationCompletion(
			const PresentationPersistenceCompletion& completion,
			Bridge::Workspace activeWorkspace,
			const std::optional<Bridge::PresentationKey>& activeKey,
			const std::optional<Bridge::PresentationTarget>& activeTarget,
			std::uint64_t activeGeneration,
			const std::optional<InkCanvasCollection>& activeDocument,
			PresentationParkedSlots& parkedSlots) noexcept
		{
			if (completion.slotGeneration == 0) return {};
			if (activeWorkspace == Bridge::Workspace::Presentation && activeKey &&
				*activeKey == completion.target.key && activeTarget &&
				activeTarget->bindingMode == completion.target.bindingMode &&
				activeGeneration == completion.slotGeneration &&
				CanReusePresentationDocumentSlot(completion.target, *activeTarget,
					activeDocument ? activeDocument->Pages().size() : 0))
				return { true, nullptr, std::nullopt };
			const PresentationLaneKey lane = LaneFor(completion.target);
			auto found = parkedSlots.find(lane);
			if (found == parkedSlots.end() || !found->second.document ||
				!found->second.presentationTarget ||
				found->second.slotGeneration != completion.slotGeneration ||
				!CanReusePresentationDocumentSlot(completion.target,
					*found->second.presentationTarget,
					found->second.document->Pages().size())) return {};
			return { false, &found->second, lane };
		}

		bool PresentationSaveCompletionMatchesSlot(
			const PresentationPersistenceCompletion& completion,
			const std::optional<draw3::uink::UInkGuid>& fileGuid,
			std::uint64_t queuedRevision) noexcept
		{
			return completion.fileGuid && fileGuid &&
				*completion.fileGuid == *fileGuid &&
				completion.mutationRevision != 0 &&
				completion.mutationRevision <= queuedRevision;
		}

		bool PresentationTrackMatchesMode(PresentationStorageTrack track,
			Bridge::SlideBindingMode mode) noexcept
		{
			return track == PresentationStorageTrack::Base ||
				(mode == Bridge::SlideBindingMode::StableSlideId
					? track == PresentationStorageTrack::SlideIdSidecar
					: track == PresentationStorageTrack::PageIndexSidecar);
		}

		bool PresentationLoadedForLane(
			const PresentationPersistenceCompletion& completion) noexcept
		{
			if (completion.status != PresentationPersistenceStatus::Loaded ||
				!PresentationTrackMatchesMode(completion.storageTrack,
					completion.target.bindingMode) ||
				!completion.loadedSnapshot || !completion.fileGuid ||
				completion.loadedSnapshot->fileGuid != *completion.fileGuid)
				return false;
			return completion.loadedSnapshot->workspaceType ==
				(completion.target.bindingMode == Bridge::SlideBindingMode::StableSlideId
					? 2 : draw3::uink::kInkeysPageIndexWorkspaceType);
		}

		bool PresentationEmptyLaneVerified(
			const PresentationPersistenceCompletion& completion) noexcept
		{
			return completion.status == PresentationPersistenceStatus::NotFound &&
				PresentationTrackMatchesMode(completion.storageTrack,
					completion.target.bindingMode);
		}

		bool PresentationLoadUnresolved(Bridge::Workspace workspace,
			bool persistenceInitialized, std::uint64_t mutationRevision) noexcept
		{
			return workspace == Bridge::Workspace::Presentation &&
				!persistenceInitialized && mutationRevision == 0;
		}

		template <typename Submit>
		bool SubmitCurrentPresentationLoad(const Bridge::PresentationTarget& target,
			std::uint64_t slotGeneration, Submit&& submit) noexcept
		{
			if (slotGeneration == 0) return false;
			try
			{
				PresentationLoadRequest request;
				request.target = target;
				request.slotGeneration = slotGeneration;
				return submit(std::move(request));
			}
			catch (...) { return false; }
		}

		struct PresentationCurrentLoadRetry
		{
			static constexpr std::array<std::uint64_t, 4> kDelaysMs =
				{ 250, 500, 1000, 2000 };
			bool ExitPrepared() const noexcept { return exitPrepared_; }
			bool AllowsLoad(bool exitRequested) const noexcept
			{
				return !exitPrepared_ && !exitRequested;
			}
			bool PrepareExit() noexcept
			{
				if (exitPrepared_) return false;
				// 退出屏障在首个 ACK 前成为单调终态；迟到回执不可重新排队。
				exitPrepared_ = true;
				Cancel();
				return true;
			}

			void Cancel() noexcept
			{
				target_.reset();
				slotGeneration_ = 0;
				automaticAttempts_ = 0;
				deadlineMs_ = 0;
				scheduled_ = false;
				manualOnly_ = false;
			}

			void OnIoError(const Bridge::PresentationTarget& target,
				std::uint64_t generation, std::uint64_t nowMs) noexcept
			{
				if (exitPrepared_) return;
				if (generation == 0) { Cancel(); return; }
				if (!Matches(target, generation))
				{
					Cancel();
					try { target_.emplace(target); }
					catch (...) { manualOnly_ = true; return; }
					slotGeneration_ = generation;
				}
				if (automaticAttempts_ == kDelaysMs.size())
				{
					scheduled_ = false;
					if (!manualOnly_)
						std::fprintf(stderr,
							"[Draw3.Presentation] action=current_load_retry result=manual_required attempts=%u generation=%llu\n",
							automaticAttempts_,
							static_cast<unsigned long long>(generation));
					manualOnly_ = true;
					return;
				}
				const std::uint64_t delay = kDelaysMs[automaticAttempts_];
				deadlineMs_ = nowMs > UINT64_MAX - delay ? UINT64_MAX : nowMs + delay;
				scheduled_ = true;
				manualOnly_ = false;
				std::fprintf(stderr,
					"[Draw3.Presentation] action=current_load_retry result=scheduled attempt=%u delay_ms=%llu generation=%llu\n",
					static_cast<unsigned>(automaticAttempts_ + 1),
					static_cast<unsigned long long>(delay),
					static_cast<unsigned long long>(generation));
			}

			void OnTerminalFailure() noexcept
			{
				scheduled_ = false;
				manualOnly_ = true;
			}

			std::optional<double> RemainingWaitMilliseconds(
				const Bridge::PresentationTarget& target,
				std::uint64_t generation, std::uint64_t nowMs) const noexcept
			{
				if (exitPrepared_ || !scheduled_ || manualOnly_ ||
					!Matches(target, generation))
					return std::nullopt;
				return static_cast<double>((std::max)(
					std::uint64_t{ 1 }, deadlineMs_ > nowMs ? deadlineMs_ - nowMs : 0));
			}

			template <typename Submit>
			bool TrySubmit(const Bridge::PresentationTarget& target,
				std::uint64_t generation, std::uint64_t nowMs,
				bool& loadPending, bool persistenceInitialized,
				Submit&& submit) noexcept
			{
				if (exitPrepared_ || !scheduled_ || manualOnly_) return false;
				if (!Matches(target, generation) || persistenceInitialized)
				{
					Cancel();
					return false;
				}
				if (loadPending || nowMs < deadlineMs_) return false;
				scheduled_ = false;
				++automaticAttempts_;
				loadPending = SubmitCurrentPresentationLoad(target, generation,
					std::forward<Submit>(submit));
				if (!loadPending) OnIoError(target, generation, nowMs);
				return loadPending;
			}

		private:
			bool Matches(const Bridge::PresentationTarget& target,
				std::uint64_t generation) const noexcept
			{
				return target_ && generation != 0 &&
					slotGeneration_ == generation && *target_ == target;
			}

			std::optional<Bridge::PresentationTarget> target_;
			std::uint64_t slotGeneration_ = 0;
			std::uint64_t deadlineMs_ = 0;
			unsigned automaticAttempts_ = 0;
			bool scheduled_ = false;
			bool manualOnly_ = false;
			bool exitPrepared_ = false;
		};

		template <typename Capture, typename Acknowledge>
		void ProcessPresentationExitBarrier(PresentationCurrentLoadRetry& retry,
			Capture&& capture, Acknowledge&& acknowledge)
		{
			if (!retry.PrepareExit()) return;
			try { capture(); }
			catch (...)
			{
				std::fputs("[Draw3.AutoSave] action=exit_snapshot result=failed reason=exception\n",
					stderr);
			}
			acknowledge();
		}

		bool CanvasCommandAllowedAfterExitBarrier(
			const PresentationCurrentLoadRetry& retry,
			CanvasCommandType type) noexcept
		{
			(void)type;
			// Host 在 ACK 后自行排空保存 worker；Controller 不再应用会派生新 Save 的迟到回执。
			return !retry.ExitPrepared();
		}

		template <typename Submit>
		bool TrySubmitCurrentLoadAtRunSafePoint(
			PresentationCurrentLoadRetry& retry,
			const Bridge::PresentationTarget& target,
			std::uint64_t generation, std::uint64_t nowMs,
			bool& loadPending, bool persistenceInitialized,
			bool exitRequested, Submit&& submit) noexcept
		{
			if (!retry.AllowsLoad(exitRequested))
			{
				retry.Cancel();
				return false;
			}
			return retry.TrySubmit(target, generation, nowMs,
				loadPending, persistenceInitialized,
				std::forward<Submit>(submit));
		}

		const RetainedPresentationSlides& RetainedSlidesForSave(
			const RetainedPresentationSlides& active,
			const DrawingDocumentSlot* parked) noexcept
		{
			// 停放文稿的 retained 页随其 document/history 一起换槽，不能取当前活动文稿。
			return parked ? parked->retainedSlides : active;
		}

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

		struct DesktopAutoSaveSource
		{
			const InkCanvasCollection* document = nullptr;
			const std::vector<CanvasPageRuntimeState>* pageRuntimeStates = nullptr;
			size_t pageIndex = 0;
		};

		const DesktopAutoSaveSource* SelectDesktopAutoSaveSource(
			Bridge::Workspace activeWorkspace, DesktopAutoSaveTrigger trigger,
			const DesktopAutoSaveSource& active,
			const DesktopAutoSaveSource& parkedDesktop) noexcept
		{
			if (activeWorkspace == Bridge::Workspace::Desktop) return &active;
			// 仅退出屏障读取停放的 Desktop；PPT/Whiteboard 当前文档不能写入 Desktop 索引。
			if (trigger == DesktopAutoSaveTrigger::Exit &&
				(activeWorkspace == Bridge::Workspace::Presentation ||
					activeWorkspace == Bridge::Workspace::Whiteboard))
				return &parkedDesktop;
			return nullptr;
		}

		bool CaptureDesktopAutoSaveForScene(
			Bridge::Workspace activeWorkspace, DesktopAutoSaveTrigger trigger,
			const DesktopAutoSaveSource& active,
			const DesktopAutoSaveSource& parkedDesktop,
			const DesktopAutoSavePolicy& policy, bool enabled, float dpiScale,
			const DrawingControllerRuntimeObserver& observer,
			std::optional<draw3::uink::UInkGuid>* acceptedFileGuid = nullptr)
		{
			const DesktopAutoSaveSource* source = SelectDesktopAutoSaveSource(
				activeWorkspace, trigger, active, parkedDesktop);
			if (!source || !observer.desktopAutoSaveRequested ||
				!source->document || !source->pageRuntimeStates ||
				source->pageIndex >= source->pageRuntimeStates->size() ||
				!policy.ShouldCapture(Bridge::Workspace::Desktop, enabled,
					source->pageRuntimeStates->at(source->pageIndex).history
						.LastVisibleItem().has_value())) return false;
			try
			{
				const InkPage* page = source->document->PageAt(source->pageIndex);
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
					source->document->WorkspaceGuid().Bytes());
				snapshot.workspaceName = "Desktop";
				snapshot.dpiScale = dpiScale;
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
					source->pageRuntimeStates->at(source->pageIndex).history;
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
				const bool accepted = observer.desktopAutoSaveRequested(
					observer.context, trigger, std::move(snapshot));
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
		}

		std::optional<PresentationSaveRequest> BuildPresentationSaveRequest(
			const InkCanvasCollection& document,
			const std::vector<CanvasPageRuntimeState>& runtimes,
			std::size_t currentPage, const Bridge::PresentationTarget& target,
			std::optional<draw3::uink::UInkGuid>& fileGuid,
			std::uint64_t mutationRevision, float dpiScale,
			const RetainedPresentationSlides& retainedSlides,
			std::optional<draw3::uink::UInkGuid> clearPageGuid = std::nullopt,
			std::uint64_t slotGeneration = 0)
		{
			try
			{
				if (!fileGuid) fileGuid = draw3::uink::CreateUInkGuid();
				if (!fileGuid || Bridge::PresentationDocumentPageCount(target) !=
					document.Pages().size() ||
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
				snapshot.dpiScale = dpiScale;
				snapshot.assignedIndependentUndoGroups = true;
				auto captureCanvas = [&](const InkPage* page,
					const CanvasPageRuntimeState& runtime, std::size_t pageIndex,
					std::optional<std::int32_t> slideId, bool retained)
					-> std::optional<draw3::uink::Draw3UInkCanvasSnapshot>
				{
					const bool endScreen = !retained && pageIndex == target.totalPages;
					if (!page || (!slideId && !endScreen && target.bindingMode ==
						Bridge::SlideBindingMode::StableSlideId)) return std::nullopt;
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
					output.extra = endScreen
						? draw3::uink::MakeInkeysEndScreenExtra(importMode)
						: draw3::uink::MakeInkeysBindingExtra(importMode);
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
						&& pageIndex < target.totalPages
						? std::optional<std::int32_t>(target.slideIds[pageIndex]) : std::nullopt;
					const auto output = captureCanvas(page, runtimes[pageIndex], pageIndex,
						slideId, false);
					if (!output) return std::nullopt;
					snapshot.activeCanvases.push_back(*output);
				}
				// 保留 legacy canvases 投影，兼容旧的 UInk 测试与读取器。
				snapshot.canvases = snapshot.activeCanvases;
				if (target.bindingMode == Bridge::SlideBindingMode::StableSlideId)
					for (const auto& [slideId, retained] : retainedSlides)
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
				request.slotGeneration = slotGeneration;
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
		}

		void MarkPresentationCanvasRetained(
			draw3::uink::Draw3UInkCanvasSnapshot& canvas)
		{
			canvas.retained = true;
			draw3::uink::UInkExtra extra = canvas.extra.value_or(
				draw3::uink::MakeInkeysBindingExtra(
					draw3::uink::Draw3UInkImportBindingMode::StableSlideId));
			std::erase_if(extra, [](const auto& pair)
				{
					const auto* key = std::get_if<std::string>(&pair.first.value);
					return key && *key == "inkeysPageState";
				});
			draw3::uink::UInkMessagePackValue key;
			key.value = std::string("inkeysPageState");
			draw3::uink::UInkMessagePackValue state;
			state.value = std::string("retained");
			extra.emplace_back(std::move(key), std::move(state));
			canvas.extra = std::move(extra);
		}

		template <typename AllocateToken>
		std::optional<DrawingDocumentSlot> MaterializePresentationSlot(
			const draw3::uink::Draw3UInkExportSnapshot& snapshot,
			const Bridge::PresentationTarget& target,
			std::uint64_t committedRevision,
			AllocateToken&& allocateRasterStateToken)
		{
			try
			{
				const auto& importedActive = snapshot.activeCanvases.empty()
					? snapshot.canvases : snapshot.activeCanvases;
				const bool stable = target.bindingMode ==
					Bridge::SlideBindingMode::StableSlideId;
				std::map<std::int32_t, draw3::uink::Draw3UInkCanvasSnapshot> bySlideId;
				std::set<std::array<uint8_t, 16>> seenPageGuids;
				const draw3::uink::Draw3UInkCanvasSnapshot* endScreen = nullptr;
				if (stable)
				{
					if (!Bridge::ValidPresentationPage(target)) return std::nullopt;
					std::set<std::int32_t> targetSlideIds;
					for (const std::int32_t id : target.slideIds)
						if (id <= 0 || !targetSlideIds.insert(id).second)
							return std::nullopt;
					// 同一份已加载快照的活动/保留页先按 GUID 与 SlideID 核验，再投影到最新拓扑。
					for (const auto& source : importedActive)
					{
						if (source.pageGuid.IsZero() || source.deviceGuid || source.retained ||
							!seenPageGuids.insert(source.pageGuid.Bytes()).second)
							return std::nullopt;
						const auto kind = draw3::uink::InkeysPageKind(source.extra);
						if (kind == draw3::uink::UInkInkeysPageKind::Invalid)
							return std::nullopt;
						if (kind == draw3::uink::UInkInkeysPageKind::EndScreen)
						{
							if (endScreen || source.slideId) return std::nullopt;
							endScreen = &source;
							continue;
						}
						if (!source.slideId || *source.slideId <= 0 ||
							!bySlideId.emplace(*source.slideId, source).second)
							return std::nullopt;
					}
					for (const auto& source : snapshot.retainedCanvases)
					{
						if (source.pageGuid.IsZero() || source.deviceGuid || !source.retained ||
							!source.slideId || *source.slideId <= 0 ||
							draw3::uink::InkeysPageKind(source.extra) !=
								draw3::uink::UInkInkeysPageKind::Normal ||
							!seenPageGuids.insert(source.pageGuid.Bytes()).second ||
							!bySlideId.emplace(*source.slideId, source).second)
							return std::nullopt;
					}
				}
				else
				{
					const auto found = std::find_if(importedActive.begin(),
						importedActive.end(), [](const auto& canvas)
						{
							return draw3::uink::InkeysPageKind(canvas.extra) ==
								draw3::uink::UInkInkeysPageKind::EndScreen;
						});
					if (found != importedActive.end()) endScreen = &*found;
				}
				std::vector<draw3::uink::Draw3UInkCanvasSnapshot> activeCanvases;
				activeCanvases.reserve(Bridge::PresentationDocumentPageCount(target));
				for (std::size_t index = 0; index < target.totalPages; ++index)
				{
					if (stable)
					{
						auto found = bySlideId.find(target.slideIds[index]);
						if (found != bySlideId.end())
						{
							activeCanvases.push_back(std::move(found->second));
							bySlideId.erase(found);
						}
						else
						{
							const auto pageGuid = draw3::uink::CreateUInkGuid();
							if (!pageGuid || !seenPageGuids.insert(pageGuid->Bytes()).second)
								return std::nullopt;
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
				// 历史 UInk 只有 N 个真实页；结束页缺席时新建独立 pageGuid。
				if (endScreen)
					activeCanvases.push_back(*endScreen);
				else
				{
					const auto pageGuid = draw3::uink::CreateUInkGuid();
					if (!pageGuid || (stable &&
						!seenPageGuids.insert(pageGuid->Bytes()).second)) return std::nullopt;
					draw3::uink::Draw3UInkCanvasSnapshot blank;
					blank.pageGuid = *pageGuid;
					blank.viewport = { 0.0f, 0.0f, 1.0f };
					activeCanvases.push_back(std::move(blank));
				}
				if (activeCanvases.size() != Bridge::PresentationDocumentPageCount(target) ||
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
				if (stable)
					for (auto& [slideId, source] : bySlideId)
					{
						auto retained = std::move(source);
						retained.slideId = slideId;
						MarkPresentationCanvasRetained(retained);
						retained.operations.clear();
						slot.retainedSlides.emplace(slideId, std::move(retained));
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
		}

		struct ActivePresentationInstallTarget
		{
			std::optional<InkCanvasCollection>& document;
			std::vector<CanvasPageRuntimeState>& pageRuntimeStates;
			std::size_t& currentPageIndex;
			std::optional<Bridge::PresentationTarget>& presentationTarget;
			RetainedPresentationSlides& retainedSlides;
			std::optional<draw3::uink::UInkGuid>& fileGuid;
			std::uint64_t& mutationRevision;
			std::uint64_t& queuedRevision;
			std::uint64_t& committedRevision;
		};

		bool InstallLoadedActivePresentationSlot(
			std::optional<DrawingDocumentSlot>& loaded,
			const Bridge::PresentationTarget& latestTarget,
			ActivePresentationInstallTarget active,
			bool* targetCopyFailed = nullptr)
		{
			if (targetCopyFailed) *targetCopyFailed = false;
			if (!loaded || active.mutationRevision != 0) return false;
			static_assert(std::is_nothrow_move_assignable_v<
				std::optional<InkCanvasCollection>>);
			static_assert(std::is_nothrow_move_assignable_v<
				std::vector<CanvasPageRuntimeState>>);
			static_assert(std::is_nothrow_move_assignable_v<RetainedPresentationSlides>);
			static_assert(std::is_nothrow_move_assignable_v<
				std::optional<Bridge::PresentationTarget>>);
			static_assert(std::is_nothrow_move_assignable_v<
				std::optional<draw3::uink::UInkGuid>>);
			std::optional<Bridge::PresentationTarget> preparedTarget;
			try { preparedTarget.emplace(latestTarget); }
			catch (...)
			{
				if (targetCopyFailed) *targetCopyFailed = true;
				std::fputs("[Draw3.Presentation] action=load_install result=failed reason=target_copy\n",
					stderr);
				return false;
			}
			// 目标深拷贝可能分配；先准备成功，以下同槽转移均保证不抛异常。
			active.document = std::move(loaded->document);
			active.pageRuntimeStates = std::move(loaded->pageRuntimeStates);
			// retained 页与已加载文档/历史属于同一文稿，只有通过 mutation 门禁后才能成组安装。
			active.retainedSlides = std::move(loaded->retainedSlides);
			active.currentPageIndex = latestTarget.pageIndex;
			active.presentationTarget = std::move(preparedTarget);
			active.fileGuid = std::move(loaded->fileGuid);
			active.mutationRevision = loaded->mutationRevision;
			active.queuedRevision = loaded->queuedRevision;
			active.committedRevision = loaded->committedRevision;
			return true;
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
			InkGuid ownerWorkspaceGuid = {};
			InkGuid ownerPageGuid = {};
			size_t ownerPageIndex = 0;
			bool cpuCommitAttempted = false;
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

		enum class StoredStrokeCommitMode { NormalUp, Fatal };
		struct StoredStrokeCpuCommit
		{
			size_t strokeIndex = 0;
			RenderItemId renderItem = {};
			InkRasterStateToken beforeState = 0;
			InkRasterStateToken afterState = 0;
		};

		template<class AllocateToken>
		std::optional<StoredStrokeCpuCommit> CommitRuntimeStoredStrokeCpu(
			RuntimeStroke& runtime, InkCanvasCollection& document,
			std::vector<CanvasPageRuntimeState>& pageRuntimeStates, size_t pageIndex,
			double liveTipTaperSeconds, StoredStrokeCommitMode mode,
			AllocateToken&& allocateToken, const char** failureReason = nullptr)
		{
			const auto reject = [&](const char* reason) -> std::optional<StoredStrokeCpuCommit>
			{
				if (failureReason) *failureReason = reason;
				return std::nullopt;
			};
			if ((mode == StoredStrokeCommitMode::NormalUp && !runtime.ended) ||
				!runtime.inUse || !runtime.handle || runtime.cancelled ||
				runtime.cpuCommitAttempted || pageIndex >= pageRuntimeStates.size() ||
				runtime.ownerPageIndex != pageIndex ||
				runtime.ownerWorkspaceGuid != document.WorkspaceGuid() ||
				runtime.handle.record->Generation() != runtime.handle.generation)
				return reject("owner_or_handle");
			InkPage* page = document.PageAt(pageIndex);
			InkCanvas* canvas = page ? page->FindCanvas(kDefaultDeviceKey) : nullptr;
			if (!canvas || runtime.ownerPageGuid != page->PageGuid())
				return reject("page_identity");
			CanvasPageRuntimeState& pageRuntime = pageRuntimeStates[pageIndex];
			if (pageRuntime.history.Items().size() != pageRuntime.beforeStates.size() ||
				pageRuntime.beforeStates.size() != pageRuntime.afterStates.size())
				return reject("history_state");
			if (mode == StoredStrokeCommitMode::Fatal)
			{
				const ContactSnapshot& terminal = runtime.awaitingReconnect
					? runtime.deferredUpSnapshot : runtime.lastInputSnapshot;
				if (terminal.phase == ContactPhase::Cancelled ||
					terminal.admissionRevision !=
						runtime.handle.record->DownSnapshot().admissionRevision ||
					terminal.sequence != runtime.lastConsumedSequence ||
					!runtime.stroke.hasInputStartPoint ||
					!std::isfinite(terminal.position.x) ||
					!std::isfinite(terminal.position.y)) return reject("sample_not_accepted");
				// 只用绘制线程已消费的 raw 终点；预测尾与失效 GPU 像素不参与封口。
				if (runtime.shape.active)
				{
					runtime.shape.rawEndpoint = { terminal.position.x, terminal.position.y };
					SetShapeVisualEndpoint(runtime.shape, runtime.shape.rawEndpoint);
				}
				else if (!runtime.ended || runtime.awaitingReconnect)
				{
					const float radius = runtime.stroke.realPoints.empty()
						? runtime.stroke.inputStartPoint.r
						: runtime.stroke.realPoints.back().r;
					const float time = runtime.stroke.realPoints.empty()
						? runtime.stroke.inputStartPoint.time
						: runtime.stroke.realPoints.back().time;
					if (!std::isfinite(radius) || radius <= 0.0f) return reject("invalid_radius");
					AppendTerminalFallbackPoint(runtime.stroke, {
						terminal.position.x, terminal.position.y, radius, time });
				}
			}
			const std::optional<StoredInkStyle> style =
				StoredStyleForTool(runtime.tool, runtime.visualStyle);
			if (!style) return reject("transient_tool"); // Laser 永远是瞬态视觉。
			std::optional<InkStroke> finalizedStroke = runtime.shape.active
				? FinalizeStoredShape(runtime.shape.primitive, *style,
					runtime.viewport.x, runtime.viewport.y)
				: FinalizeStoredStroke(runtime.stroke, *style, liveTipTaperSeconds,
					runtime.rebuildPoints, runtime.viewport.x, runtime.viewport.y);
			if (!finalizedStroke) return reject("finalize");
			std::optional<StrokeTileFootprint> footprint =
				BuildStrokeTileFootprint(*finalizedStroke);
			if (!footprint) return reject("footprint");
			// Canvas 追加后即使 history 失败也不能重试，否则可能产生重复的孤儿 Stroke。
			runtime.cpuCommitAttempted = true;
			const std::optional<size_t> strokeIndex =
				canvas->AppendStroke(std::move(*finalizedStroke));
			if (!strokeIndex) return reject("canvas_append");
			pageRuntime.history.DiscardRedoBranch();
			const std::optional<RenderItemId> renderItem =
				pageRuntime.history.AppendStroke(*strokeIndex, std::move(*footprint), true);
			const RenderItemState* visibleItem = renderItem
				? pageRuntime.history.Find(*renderItem) : nullptr;
			if (!renderItem || renderItem->index != pageRuntime.beforeStates.size() ||
				!visibleItem || !visibleItem->visible ||
				visibleItem->strokeIndex != *strokeIndex) return reject("history_append");
			const InkRasterStateToken beforeState = pageRuntime.rasterState;
			const InkRasterStateToken afterState = allocateToken();
			pageRuntime.beforeStates.push_back(beforeState);
			pageRuntime.afterStates.push_back(afterState);
			if (mode == StoredStrokeCommitMode::Fatal)
			{
				pageRuntime.rasterState = afterState;
				pageRuntime.clearRedoAvailable = false;
			}
			return StoredStrokeCpuCommit{ *strokeIndex, *renderItem,
				beforeState, afterState };
		}

		// 旁挂表只保存 opaque 值键和自有数值，不能给已回收的 contact 延长生存期。
		struct ContentMetricKey
		{
			ContactRecord* opaqueRecord = nullptr;
			uint64_t generation = 0;
			bool operator==(const ContentMetricKey&) const = default;
		};
		ContentMetricKey MetricKey(ContactHandle handle) noexcept
		{ return { handle.record, handle.generation }; }

		enum class ContentMetricAdoptionKind : uint8_t
		{
			None, Model, RawDown, RawTerminal, Shape
		};
		enum class ContentMetricInvalidationReason : uint8_t
		{
			Cancelled, InitRejected, ReconnectSuperseded, ContentSuperseded,
			SceneSuperseded, Fatal, Stopped, ProducerOverflow, Count
		};
		struct ContentMetricOutputIdentity
		{
			bool exists = false;
			TransparentOutputTarget target = TransparentOutputTarget::PrimaryDrawpad;
			uint64_t rawRevision = 0;
			uint64_t generation = 0;
			bool operator==(const ContentMetricOutputIdentity&) const = default;
		};
		struct ContentMetricRasterSignature
		{
			RuntimeMetricsCanvasIdentity canvas;
			uint64_t historyRevision = 0;
			InkRasterStateToken rasterState = 0;
			float viewportX = 0.0f;
			float viewportY = 0.0f;
			float viewportScale = 1.0f;
			int width = 0;
			int height = 0;
			bool operator==(const ContentMetricRasterSignature&) const = default;
		};
		struct ContentMetricL2Stamp
		{
			bool valid = false;
			ContentMetricRasterSignature signature;
		};
		struct ContentMetricLiveNote
		{
			bool exists = false;
			ContentMetricKey key;
			RuntimeMetricsCanvasIdentity canvas;
			uint64_t downAdmission = 0;
			int64_t downQpc = 0;
			uint64_t consumedSequence = 0;
			uint64_t adoptedSequence = 0;
			int64_t adoptedQpc = 0;
			uint64_t adoptedAdmission = 0;
			ContentMetricAdoptionKind adoptionKind = ContentMetricAdoptionKind::None;
			uint64_t contentToken = 0;
			uint64_t geometrySequence = 0;
			uint64_t rasteredSequence = 0;
			uint64_t rasterFrameSerial = 0;
			RECT nonPredictedBounds = {};
			DrawingTool tool = DrawingTool::Pen;
			bool nonPredictedGeometry = false;
			bool landingConfirmed = false;
			uint64_t lastWithheldSequence = 0;
			ContentMetricRasterSignature rasterSurface;
			bool rasterSurfaceKnown = false;
		};
		struct ContentMetricStoredNote
		{
			bool exists = false;
			ContentMetricKey key;
			RuntimeMetricsCanvasIdentity canvas;
			RenderItemId item;
			size_t strokeIndex = 0;
			uint64_t contentGeneration = 0;
			InkRasterStateToken afterState = 0;
			uint64_t terminalSequence = 0;
			int64_t terminalQpc = 0;
			uint64_t terminalAdmission = 0;
			uint64_t contentToken = 0;
		};
		struct ContentMetricCandidate
		{
			ContentMetricKey key;
			RuntimeMetricsLandingProof proof;
			ContentMetricOutputIdentity output;
		};
		struct ContentMetricCanvasMapping
		{
			InkGuid workspace;
			InkGuid page;
			DeviceKey device;
			uint64_t workspaceOrdinal = 0;
			uint64_t pageOrdinal = 0;
		};
		struct ContentMetricCounters
		{
			std::array<uint64_t, static_cast<size_t>(ContentMetricInvalidationReason::Count)> invalidated = {};
			uint64_t proofOverflow = 0;
			uint64_t identityExhausted = 0;
			uint64_t consumedNotAdopted = 0;
			uint64_t outputMismatch = 0;
			uint64_t authoritativeWithheld = 0;
			uint64_t noPresent = 0;
			uint64_t rasterFailed = 0;
			uint64_t excludedLaser = 0;
		};

		struct ControllerContentMetrics
		{
			static constexpr size_t kByteBudget = 32u * 1024 * 1024;
			static constexpr size_t kNoteCapacity = 64;
			static constexpr size_t kCandidateCapacity = kNoteCapacity * 2;
			explicit ControllerContentMetrics(RuntimeMetricsSession& session) noexcept : metrics(session) {}
			static std::unique_ptr<ControllerContentMetrics> Prepare(
				RuntimeMetricsSession* session, size_t externalAuxiliaryBytes = 0) noexcept;
			static bool FitsBudget(RuntimeMetricsSession* session,
				size_t externalAuxiliaryBytes, size_t ownBytes) noexcept;

			uint64_t AllocateContentToken() noexcept
			{
				if (!identityAvailable) return 0;
				if (nextContentToken == 0) { ExhaustIdentity(); return 0; }
				const uint64_t value = nextContentToken;
				nextContentToken = value == (std::numeric_limits<uint64_t>::max)() ? 0 : value + 1;
				return value;
			}
			ContentMetricLiveNote* FindLive(ContentMetricKey key) noexcept
			{
				for (auto& note : live) if (note.exists && note.key == key) return &note;
				return nullptr;
			}
			ContentMetricStoredNote* FindStored(ContentMetricKey key) noexcept
			{
				for (auto& note : stored) if (note.exists && note.key == key) return &note;
				return nullptr;
			}
			bool Register(ContentMetricKey key, const ContactSnapshot& down,
				InputDeviceType device, DrawingTool tool) noexcept
			{
				if (!identityAvailable) return false;
				if (FindLive(key) || FindStored(key)) return true;
				if (tool == DrawingTool::Laser) ++counters.excludedLaser;
				if (!metrics.RegisterContact(key.opaqueRecord, key.generation,
					device, static_cast<uint32_t>(tool), down.qpc)) return false;
				for (auto& note : live)
				{
					if (note.exists) continue;
					// 源快照只在真实初始化入口复制一次；以后只用值键。
					note = {};
					note.exists = true;
					note.key = key;
					note.canvas = canvasIdentity;
					note.downAdmission = down.admissionRevision;
					note.downQpc = down.qpc;
					note.contentToken = AllocateContentToken();
					note.tool = tool;
					return note.exists && note.contentToken != 0;
				}
				++counters.proofOverflow;
				Invalidate(key, ContentMetricInvalidationReason::ProducerOverflow);
				return false;
			}
			void NoteConsumed(ContentMetricKey key, uint64_t sequence) noexcept
			{
				if (auto* note = FindLive(key)) note->consumedSequence = sequence;
			}
			bool Adopt(ContentMetricKey key, const ContactSnapshot& snapshot,
				ContentMetricAdoptionKind kind, bool nonPredictedGeometry) noexcept
			{
				auto* note = FindLive(key);
				if (!note || !nonPredictedGeometry ||
					(kind != ContentMetricAdoptionKind::Model && kind != ContentMetricAdoptionKind::RawDown &&
						kind != ContentMetricAdoptionKind::RawTerminal && kind != ContentMetricAdoptionKind::Shape) ||
					snapshot.sequence == 0 || snapshot.qpc <= 0 ||
					(snapshot.phase != ContactPhase::Down && snapshot.phase != ContactPhase::Move && snapshot.phase != ContactPhase::Up) ||
					snapshot.sequence != note->consumedSequence ||
					snapshot.sequence < note->adoptedSequence || snapshot.qpc < note->adoptedQpc ||
					snapshot.qpc < note->downQpc ||
					snapshot.admissionRevision != note->downAdmission ||
					(kind == ContentMetricAdoptionKind::RawDown && snapshot.phase != ContactPhase::Down) ||
					(kind == ContentMetricAdoptionKind::RawTerminal && snapshot.phase != ContactPhase::Up)) return false;
				const uint64_t geometryToken = AllocateContentToken();
				if (geometryToken == 0) return false;
				note->adoptedSequence = snapshot.sequence;
				note->adoptedQpc = snapshot.qpc;
				note->adoptedAdmission = snapshot.admissionRevision;
				note->adoptionKind = kind;
				note->geometrySequence = snapshot.sequence;
				note->contentToken = geometryToken;
				note->nonPredictedGeometry = true;
				note->rasteredSequence = 0;
				note->rasterFrameSerial = 0;
				return true;
			}
			void ObserveLiveRaster(ContentMetricKey key, uint64_t geometrySequence,
				RECT bounds, uint64_t frameSerial, bool sharedLayersSucceeded,
				const ContentMetricRasterSignature* surface = nullptr) noexcept
			{
				auto* note = FindLive(key);
				if (!note) return;
				const bool accepted = sharedLayersSucceeded && frameSerial != 0 &&
					frameSerial == metrics.Snapshot().frameSerial &&
					SameScene(note->canvas, canvasIdentity) && geometrySequence != 0 &&
					geometrySequence == note->geometrySequence && geometrySequence == note->adoptedSequence;
				note->rasteredSequence = accepted ? geometrySequence : 0;
				note->rasterFrameSerial = accepted ? frameSerial : 0;
				if (accepted) note->canvas.rasterGeneration = canvasIdentity.rasterGeneration;
				note->rasterSurfaceKnown = accepted && surface != nullptr;
				if (note->rasterSurfaceKnown) note->rasterSurface = *surface;
				note->nonPredictedBounds = bounds;
			}
			bool CaptureStored(ContentMetricKey key, const StoredStrokeCpuCommit& committed,
				const CanvasPageRuntimeState& pageRuntime, const ContactSnapshot& terminal) noexcept
			{
				auto* source = FindLive(key);
				if (source && source->landingConfirmed)
				{
					// Down 已经确认；真实 Up 仍计帧，但不再占用第二个 Down landing 槽。
					source->exists = false;
					return true;
				}
				const auto* item = pageRuntime.history.Find(committed.renderItem);
				if (!source || !item || !item->visible || item->strokeIndex != committed.strokeIndex ||
					source->tool == DrawingTool::Laser || !SameScene(source->canvas, canvasIdentity) ||
					item->contentGeneration == 0 || committed.afterState == 0 ||
					committed.renderItem.index >= pageRuntime.afterStates.size() ||
					pageRuntime.afterStates[committed.renderItem.index] != committed.afterState ||
					terminal.sequence == 0 || terminal.qpc <= 0 ||
					terminal.phase != ContactPhase::Up || terminal.sequence != source->consumedSequence ||
					terminal.admissionRevision != source->downAdmission) return false;
				for (auto& note : stored)
				{
					if (note.exists) continue;
					const uint64_t contentToken = AllocateContentToken();
					if (contentToken == 0) return false;
					note = { true, key, source->canvas, committed.renderItem,
						committed.strokeIndex, item->contentGeneration, committed.afterState,
						terminal.sequence, terminal.qpc, terminal.admissionRevision, contentToken };
					source->exists = false;
					return true;
				}
				++counters.proofOverflow;
				Invalidate(key, ContentMetricInvalidationReason::ProducerOverflow);
				return false;
			}
			bool ObserveCanvas(const InkCanvasCollection& document, size_t pageIndex,
				uint64_t rasterGeneration, bool newScene) noexcept
			{
				if (!identityAvailable) return false;
				const auto* page = document.PageAt(pageIndex);
				if (!page || !page->FindCanvas(kDefaultDeviceKey) || rasterGeneration == 0 ||
					document.WorkspaceGuid().IsZero() || page->PageGuid().IsZero()) return false;
				if (rasterGeneration < canvasIdentity.rasterGeneration)
				{ ExhaustIdentity(); return false; } // 产品计数绕回也不能复用旧诊断身份。
				size_t index = 0;
				uint64_t workspaceOrdinal = 0;
				for (; index < mappingCount; ++index)
				{
					if (mappings[index].workspace == document.WorkspaceGuid())
						workspaceOrdinal = mappings[index].workspaceOrdinal;
					if (mappings[index].workspace == document.WorkspaceGuid() &&
						mappings[index].page == page->PageGuid() && mappings[index].device == kDefaultDeviceKey) break;
				}
				if (index == mappingCount)
				{
					if (mappingCount == mappings.size()) { ExhaustIdentity(); return false; }
					if (workspaceOrdinal == 0) workspaceOrdinal = ++lastWorkspaceOrdinal;
					mappings[mappingCount++] = { document.WorkspaceGuid(), page->PageGuid(),
						kDefaultDeviceKey, workspaceOrdinal, ++lastPageOrdinal };
				}
				const auto& mapping = mappings[index];
				if (newScene || canvasIdentity.workspace != mapping.workspaceOrdinal ||
					canvasIdentity.page != mapping.pageOrdinal)
				{
					if (lastSceneGeneration == (std::numeric_limits<uint64_t>::max)())
					{ ExhaustIdentity(); return false; }
					const auto previous = canvasIdentity;
					for (const auto& note : live)
						if (note.exists && SameScene(note.canvas, previous))
							Invalidate(note.key, ContentMetricInvalidationReason::SceneSuperseded);
					for (const auto& note : stored)
						if (note.exists && SameScene(note.canvas, previous))
							Invalidate(note.key, ContentMetricInvalidationReason::SceneSuperseded);
					InvalidateRaster();
					canvasIdentity.sceneGeneration = ++lastSceneGeneration;
				}
				else if (canvasIdentity.rasterGeneration != rasterGeneration) InvalidateRaster();
				canvasIdentity.workspace = mapping.workspaceOrdinal;
				canvasIdentity.page = mapping.pageOrdinal;
				canvasIdentity.rasterGeneration = rasterGeneration;
				ownerWorkspace = document.WorkspaceGuid();
				ownerPage = page->PageGuid();
				return true;
			}
			ContentMetricRasterSignature Signature(const InkCanvasCollection& document,
				size_t pageIndex, const CanvasPageRuntimeState& pageRuntime, int width, int height) const noexcept
			{
				const auto* page = document.PageAt(pageIndex);
				const auto* canvas = page ? page->FindCanvas(kDefaultDeviceKey) : nullptr;
				if (!canvas || document.WorkspaceGuid() != ownerWorkspace || page->PageGuid() != ownerPage)
					return {};
				const auto viewport = canvas->Viewport();
				return { canvasIdentity, pageRuntime.history.Revision(), pageRuntime.rasterState,
					viewport.x, viewport.y, viewport.scale, width, height };
			}
			ContentMetricOutputIdentity ObserveOutput(bool exists, TransparentOutputTarget target, uint64_t rawRevision) noexcept
			{
				if (!identityAvailable) return {};
				if (!exists) { InvalidateOutput(); return {}; }
				if (target != TransparentOutputTarget::PrimaryDrawpad && target != TransparentOutputTarget::SelectionUlw)
				{ InvalidateOutput(); return {}; }
				if (output.exists && output.target == target && output.rawRevision == rawRevision) return output;
				if (nextOutputGeneration == 0) { ExhaustIdentity(); return {}; }
				// raw0 和 UINT64_MAX 都是原始值；匿名编号单独推进，不对 raw 做算术。
				const uint64_t generation = nextOutputGeneration;
				nextOutputGeneration = generation == (std::numeric_limits<uint64_t>::max)() ? 0 : generation + 1;
				output = { true, target, rawRevision, generation };
				return output;
			}
			void InvalidateOutput() noexcept { output = {}; candidateCount = 0; compositeReady = false; }
			static bool SameScene(const RuntimeMetricsCanvasIdentity& left,
				const RuntimeMetricsCanvasIdentity& right) noexcept
			{
				return left.workspace == right.workspace && left.page == right.page &&
					left.sceneGeneration == right.sceneGeneration;
			}
			bool ValidSignature(const ContentMetricRasterSignature& signature) const noexcept
			{
				return identityAvailable && signature.canvas == canvasIdentity &&
					signature.canvas.workspace != 0 && signature.canvas.page != 0 &&
					signature.canvas.sceneGeneration != 0 && signature.canvas.rasterGeneration != 0 &&
					signature.canvas.outputGeneration == 0 && signature.width > 0 && signature.height > 0 &&
					std::isfinite(signature.viewportX) && std::isfinite(signature.viewportY) && signature.viewportScale == 1.0f;
			}
			static bool SameSurface(const ContentMetricRasterSignature& left,
				const ContentMetricRasterSignature& right) noexcept
			{
				return left.canvas == right.canvas && left.width == right.width && left.height == right.height &&
					left.viewportX == right.viewportX && left.viewportY == right.viewportY &&
					left.viewportScale == right.viewportScale;
			}
			bool CompleteFullReplay(const ContentMetricRasterSignature& signature, bool succeeded) noexcept
			{
				InvalidateL2();
				if (!succeeded || !ValidSignature(signature)) return false;
				l2 = { true, signature };
				return true;
			}
			bool BeginLocalWrite(const ContentMetricRasterSignature& before) noexcept
			{
				const bool continuous = l2.valid && l2.signature == before && ValidSignature(before);
				// 先撤旧资格；后来的另一笔成功不能给此前失败的全页补一个成功戳。
				InvalidateL2();
				localWriteBefore = before;
				localWritePending = continuous;
				return continuous;
			}
			bool CompleteLocalWrite(const ContentMetricRasterSignature& before,
				const ContentMetricRasterSignature& after, bool succeeded) noexcept
			{
				const bool continuous = localWritePending && localWriteBefore == before && succeeded &&
					ValidSignature(after) && SameSurface(before, after) && after.historyRevision >= before.historyRevision;
				localWritePending = false;
				l2.valid = false;
				candidateCount = 0;
				compositeReady = false;
				if (!continuous) return false;
				l2 = { true, after };
				return true;
			}
			void BeginVisibleReplay(const ContentMetricRasterSignature& signature) noexcept
			{
				InvalidateL2();
				replaySignature = signature;
				replayPending = ValidSignature(signature);
			}
			bool CompleteVisibleReplay(const ContentMetricRasterSignature& signature, bool complete) noexcept
			{
				l2.valid = false;
				candidateCount = 0;
				compositeReady = false;
				if (!replayPending || !ValidSignature(signature) || signature != replaySignature)
				{ replayPending = false; return false; }
				if (!complete) return false; // 分帧计划保留开始身份，等待全部必要可见 Tile。
				replayPending = false;
				l2 = { true, signature };
				return true;
			}
			void InvalidateL2() noexcept
			{
				l2.valid = false;
				candidateCount = 0;
				compositeReady = false;
				replayPending = false;
				localWritePending = false;
			}
			void InvalidateRaster() noexcept
			{
				InvalidateL2();
				InvalidateLiveRasters();
			}
			void InvalidateLiveRasters() noexcept
			{
				for (auto& note : live) { note.rasteredSequence = 0; note.rasterFrameSerial = 0; }
			}
			void RetireNotes(ContentMetricKey key) noexcept
			{
				if (auto* note = FindLive(key)) note->exists = false;
				if (auto* note = FindStored(key)) note->exists = false;
				size_t kept = 0;
				for (size_t index = 0; index < candidateCount; ++index)
					if (candidates[index].key != key) candidates[kept++] = candidates[index];
				candidateCount = kept;
			}
			bool Invalidate(ContentMetricKey key, ContentMetricInvalidationReason reason) noexcept
			{
				const auto* source = FindLive(key);
				const bool excludedLaser = source && source->tool == DrawingTool::Laser;
				const bool changed = metrics.InvalidateContact(key.opaqueRecord, key.generation);
				const size_t reasonIndex = static_cast<size_t>(reason);
				if (changed && !excludedLaser && reasonIndex < counters.invalidated.size())
					++counters.invalidated[reasonIndex];
				RetireNotes(key); // 已确认、重复或旧代次也只释放这个值键的旁挂状态。
				return changed;
			}
			void ExhaustIdentity() noexcept
			{
				if (!identityAvailable) return;
				identityAvailable = false;
				++counters.identityExhausted;
				for (auto& note : live)
				{
					if (note.exists) metrics.InvalidateContact(note.key.opaqueRecord, note.key.generation);
					note.exists = false;
				}
				for (auto& note : stored)
				{
					if (note.exists) metrics.InvalidateContact(note.key.opaqueRecord, note.key.generation);
					note.exists = false;
				}
				InvalidateRaster();
				InvalidateOutput();
			}
			static bool ClipBounds(RECT bounds, int width, int height, RECT& clipped) noexcept
			{
				clipped = { std::clamp(bounds.left, 0L, static_cast<LONG>(width)),
					std::clamp(bounds.top, 0L, static_cast<LONG>(height)),
					std::clamp(bounds.right, 0L, static_cast<LONG>(width)),
					std::clamp(bounds.bottom, 0L, static_cast<LONG>(height)) };
				return clipped.left < clipped.right && clipped.top < clipped.bottom;
			}
			static bool ContainsBounds(RECT outer, RECT inner) noexcept
			{
				return outer.left <= inner.left && outer.top <= inner.top &&
					outer.right >= inner.right && outer.bottom >= inner.bottom;
			}
			static bool IntersectsBounds(RECT left, RECT right) noexcept
			{
				return left.left < right.right && right.left < left.right &&
					left.top < right.bottom && right.top < left.bottom;
			}
			static bool StoredProjection(InkPixelBounds bounds,
				const ContentMetricRasterSignature& signature, RECT& projection) noexcept
			{
				if (!std::isfinite(bounds.left) || !std::isfinite(bounds.top) ||
					!std::isfinite(bounds.right) || !std::isfinite(bounds.bottom) ||
					bounds.left >= bounds.right || bounds.top >= bounds.bottom) return false;
				// 先在 double 中偏移/裁剪再转 LONG，完全视口外的几何没有成功投影。
				const auto coordinate = [](double value, int extent, bool ceiling) noexcept -> LONG
				{
					return static_cast<LONG>(std::clamp(ceiling ? std::ceil(value) : std::floor(value),
						0.0, static_cast<double>(extent)));
				};
				projection = { coordinate(static_cast<double>(bounds.left) - signature.viewportX, signature.width, false),
					coordinate(static_cast<double>(bounds.top) - signature.viewportY, signature.height, false),
					coordinate(static_cast<double>(bounds.right) - signature.viewportX, signature.width, true),
					coordinate(static_cast<double>(bounds.bottom) - signature.viewportY, signature.height, true) };
				return projection.left < projection.right && projection.top < projection.bottom;
			}
			bool StageCandidate(ContentMetricKey key, const RuntimeMetricsLandingProof& proof) noexcept
			{
				if (candidateCount == candidates.size())
				{
					++counters.proofOverflow;
					Invalidate(key, ContentMetricInvalidationReason::ProducerOverflow);
					return false;
				}
				if (!metrics.StageVerifiedLanding(key.opaqueRecord, key.generation, proof))
				{
					// overflow/已终结保留 Session 的分母，但不能继续占住旁挂槽。
					Invalidate(key, ContentMetricInvalidationReason::ContentSuperseded);
					return false;
				}
				candidates[candidateCount++] = { key, proof, output };
				return true;
			}
			void PruneStored(const InkCanvas& canvas, const CanvasPageRuntimeState& pageRuntime) noexcept
			{
				for (auto& note : stored)
				{
					if (!note.exists) continue;
					const auto* item = pageRuntime.history.Find(note.item);
					if (!SameScene(note.canvas, canvasIdentity) || !item || !item->visible ||
						item->strokeIndex != note.strokeIndex || item->contentGeneration != note.contentGeneration ||
						note.strokeIndex >= canvas.Strokes().size() || note.item.index >= pageRuntime.afterStates.size() ||
						pageRuntime.afterStates[note.item.index] != note.afterState)
						Invalidate(note.key, ContentMetricInvalidationReason::ContentSuperseded);
				}
			}
			size_t FreezeCandidates(const ContentMetricRasterSignature& signature, const InkCanvas& canvas,
				const CanvasPageRuntimeState& pageRuntime, RECT dirty, bool compositeSucceeded, bool fullComposite) noexcept
			{
				candidateCount = 0;
				frozenFrameSerial = metrics.Snapshot().frameSerial;
				frozenOutput = output;
				frozenSignature = signature;
				compositeReady = false;
				const auto viewport = canvas.Viewport();
				if (frozenFrameSerial == 0 || !ValidSignature(signature) ||
					signature.historyRevision != pageRuntime.history.Revision() ||
					signature.rasterState != pageRuntime.rasterState || canvas.Device() != kDefaultDeviceKey ||
					signature.viewportX != viewport.x || signature.viewportY != viewport.y ||
					signature.viewportScale != viewport.scale) return 0;
				const bool currentL2 = l2.valid && l2.signature == signature;
				PruneStored(canvas, pageRuntime);
				RECT compositeBounds;
				if (!compositeSucceeded || !output.exists || output.generation == 0 ||
					!ClipBounds(dirty, signature.width, signature.height, compositeBounds)) return 0;
				// fullComposite 表示实际整视口合成；PresentFull 开关本身没有这个资格。
				if (fullComposite && !ContainsBounds(compositeBounds, { 0, 0, signature.width, signature.height })) return 0;
				compositeReady = true;
				RuntimeMetricsCanvasIdentity proofCanvas = signature.canvas;
				proofCanvas.outputGeneration = output.generation;
				for (auto& note : live)
				{
					if (!note.exists || note.landingConfirmed || note.tool == DrawingTool::Laser) continue;
					if (note.consumedSequence != note.adoptedSequence)
					{
						if (note.consumedSequence != 0 && note.lastWithheldSequence != note.consumedSequence)
						{ ++counters.consumedNotAdopted; note.lastWithheldSequence = note.consumedSequence; }
						continue;
					}
					RECT bounds;
					if (!SameScene(note.canvas, signature.canvas) || note.canvas.rasterGeneration != signature.canvas.rasterGeneration ||
						note.contentToken == 0 || note.adoptedSequence == 0 || note.adoptedQpc <= 0 ||
						note.adoptedAdmission != note.downAdmission || note.adoptionKind == ContentMetricAdoptionKind::None ||
						!note.nonPredictedGeometry || note.rasteredSequence != note.adoptedSequence ||
						note.geometrySequence != note.adoptedSequence || note.rasterFrameSerial != frozenFrameSerial ||
						(note.rasterSurfaceKnown && !SameSurface(note.rasterSurface, signature)) ||
						!ClipBounds(note.nonPredictedBounds, signature.width, signature.height, bounds) ||
						!IntersectsBounds(bounds, compositeBounds)) continue;
					StageCandidate(note.key, { proofCanvas, note.contentToken, 0, note.adoptedSequence,
						RuntimeMetricsProofKind::Live, frozenFrameSerial });
				}
				for (auto& note : stored)
				{
					if (!note.exists) continue;
					const auto* item = pageRuntime.history.Find(note.item);
					RECT projection;
					if (!currentL2 || !item || !StoredProjection(item->pixelBounds, signature, projection) ||
						!ContainsBounds(compositeBounds, projection))
					{ ++counters.authoritativeWithheld; continue; }
					StageCandidate(note.key, { proofCanvas, note.contentToken,
						(uint64_t{ note.item.generation } << 32) | note.item.index,
						note.terminalSequence, RuntimeMetricsProofKind::Stored, frozenFrameSerial });
				}
				return candidateCount;
			}
			void PresentReturned(bool called, bool succeeded, int64_t returnQpc, double wallMs,
				ContentMetricOutputIdentity observed) noexcept
			{
				if (!called) { candidateCount = 0; compositeReady = false; return; }
				// 每个真实调用先独立结算结果；无 proof 或输出不匹配都不能抹掉这次尝试。
				metrics.RecordVerifiedPresent(wallMs, succeeded);
				const bool currentFrame = frozenFrameSerial != 0 && frozenFrameSerial == metrics.Snapshot().frameSerial;
				const bool outputMatches = frozenOutput.exists && observed.exists && output.exists &&
					frozenOutput == observed && frozenOutput == output;
				if (succeeded && currentFrame && frozenOutput.exists && !outputMatches) ++counters.outputMismatch;
				const bool eligible = currentFrame && outputMatches && compositeReady && ValidSignature(frozenSignature);
				for (size_t index = 0; index < candidateCount; ++index)
				{
					const auto candidate = candidates[index];
					if (!eligible || candidate.output != frozenOutput) continue;
					if (candidate.proof.kind == RuntimeMetricsProofKind::Stored &&
						(!l2.valid || l2.signature != frozenSignature))
					{ ++counters.authoritativeWithheld; continue; }
					const auto before = metrics.Snapshot();
					metrics.CommitVerifiedLandings(succeeded, returnQpc, candidate.proof);
					const auto after = metrics.Snapshot();
					if (after.confirmed == before.confirmed + 1)
					{
						if (auto* note = FindStored(candidate.key)) note->exists = false;
						if (auto* note = FindLive(candidate.key)) note->landingConfirmed = true;
					}
					else if (after.invalid != before.invalid && after.pending < before.pending)
					{
						if (auto* note = FindStored(candidate.key)) note->exists = false;
						if (auto* note = FindLive(candidate.key)) note->exists = false;
					}
				}
				candidateCount = 0;
				compositeReady = false;
			}
			void RecordFrame(const RuntimeMetricsFrameSample& sample) noexcept
			{
				if (!sample.presentAttempted) ++counters.noPresent;
				if ((sample.reasonFlags & (1u << 15)) != 0) ++counters.rasterFailed; // 冻结合同 bit15 是 RasterFailed。
				metrics.RecordRenderFrame(sample);
			}

			RuntimeMetricsSession& metrics;
			std::array<ContentMetricLiveNote, kNoteCapacity> live = {};
			std::array<ContentMetricStoredNote, kNoteCapacity> stored = {};
			std::array<ContentMetricCandidate, kCandidateCapacity> candidates = {};
			std::array<ContentMetricCanvasMapping, kNoteCapacity> mappings = {};
			size_t mappingCount = 0;
			size_t candidateCount = 0;
			uint64_t lastWorkspaceOrdinal = 0;
			uint64_t lastPageOrdinal = 0;
			uint64_t lastSceneGeneration = 0;
			uint64_t nextContentToken = 1;
			uint64_t nextOutputGeneration = 1;
			InkGuid ownerWorkspace;
			InkGuid ownerPage;
			RuntimeMetricsCanvasIdentity canvasIdentity;
			ContentMetricOutputIdentity output;
			ContentMetricL2Stamp l2;
			ContentMetricRasterSignature replaySignature;
			bool replayPending = false;
			ContentMetricRasterSignature localWriteBefore;
			bool localWritePending = false;
			ContentMetricOutputIdentity frozenOutput;
			ContentMetricRasterSignature frozenSignature;
			uint64_t frozenFrameSerial = 0;
			bool compositeReady = false;
			bool identityAvailable = true;
			ContentMetricCounters counters;
		};
		static_assert(std::is_trivially_copyable_v<ContentMetricStoredNote>);
		static_assert(std::is_trivially_copyable_v<ContentMetricCandidate>);
		static_assert(sizeof(ControllerContentMetrics) <= 64u * 1024);

		std::unique_ptr<ControllerContentMetrics> ControllerContentMetrics::Prepare(
			RuntimeMetricsSession* session, size_t externalAuxiliaryBytes) noexcept
		{
			if (!FitsBudget(session, externalAuxiliaryBytes, sizeof(ControllerContentMetrics))) return nullptr;
			return std::unique_ptr<ControllerContentMetrics>(new (std::nothrow) ControllerContentMetrics(*session));
		}

		bool ControllerContentMetrics::FitsBudget(RuntimeMetricsSession* session,
			size_t externalAuxiliaryBytes, size_t ownBytes) noexcept
		{
			if (!session || ownBytes > kByteBudget) return false;
			// 先核共同 payload 再分配；Host 的 phase/功能像素缓冲也必须计入余量。
			if (externalAuxiliaryBytes > kByteBudget - ownBytes ||
				session->Snapshot().allocatedBytes > kByteBudget - ownBytes - externalAuxiliaryBytes)
				return false;
			return true;
		}

		struct MetricGeometrySnapshot
		{
			size_t count = 0;
			InkPoint tail = {};
			DirectX::XMFLOAT2 shapeEndpoint = {};
			bool shapeEndpointValid = false;
		};
		MetricGeometrySnapshot CaptureMetricGeometry(const RuntimeStroke& runtime) noexcept
		{
			return { runtime.stroke.realPoints.size(),
				runtime.stroke.realPoints.empty() ? InkPoint{} : runtime.stroke.realPoints.back(),
				runtime.shape.modeledEndpoint, runtime.shape.hasModeledEndpoint };
		}
		bool MetricGeometryChanged(const MetricGeometrySnapshot& before, const RuntimeStroke& runtime) noexcept
		{
			if (runtime.shape.active)
				return runtime.shape.hasModeledEndpoint && (!before.shapeEndpointValid ||
					before.shapeEndpoint.x != runtime.shape.modeledEndpoint.x || before.shapeEndpoint.y != runtime.shape.modeledEndpoint.y);
			if (before.count != runtime.stroke.realPoints.size()) return !runtime.stroke.realPoints.empty();
			if (runtime.stroke.realPoints.empty()) return false;
			const auto& tail = runtime.stroke.realPoints.back();
			return before.tail.x != tail.x || before.tail.y != tail.y || before.tail.r != tail.r || before.tail.time != tail.time;
		}

		void StopFatalInputConsumer(ContactInputCoordinator& input) noexcept
		{
			// fatal 后绘制线程不再消费旧 route；只封闭新 admission，终态由 Host/RTS 收拢。
			input.SetAdmissionBlocked(true);
		}

		void RejectStrokeInitialization(ContactInputCoordinator& input, ContactHandle handle,
			const ContactSnapshot&) noexcept
		{
			// 同 key 可能已属于下一次 Down；只交出失败旧笔的精确代次，等物理终态回收。
			input.DiscardUntilTerminal(handle);
		}

		bool IgnoreAdditionalLaserTouch(ContactInputCoordinator& input, ContactHandle handle,
			DrawingTool batchTool, InputDeviceType deviceType,
			bool hasActiveLaserTouchContact, bool laserMultiTouchEnabled) noexcept
		{
			if (batchTool != DrawingTool::Laser || deviceType != InputDeviceType::Touch ||
				!hasActiveLaserTouchContact || laserMultiTouchEnabled) return false;
			// Down 已出队但仍由 producer 持有 route；隔离到 Up/Cancel 后自动回收槽。
			input.DiscardUntilTerminal(handle);
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

		bool BakeLaserStrokeLayers(std::vector<LaserStrokeLayer>& layers,
			InkRenderer& renderer, float dpiScale, int width, int height,
			RECT& compositedBounds, RECT& bakeDirty, LaserCoverageMode& coverageMode,
			void (*afterLayer)(InkRenderer&, size_t, void*) = nullptr,
			void* afterLayerContext = nullptr)
		{
			if (std::none_of(layers.begin(), layers.end(), [](const LaserStrokeLayer& layer)
				{ return ShouldCompositeLaserLayer(layer.cancelled,
					LaserStrokeLayerPoints(layer).size()); }))
			{
				renderer.ClearLaserIncrementalCoverage();
				layers.clear();
				return true;
			}
			if (!renderer.BeginLaserBake()) return false;
			RECT nextCompositedBounds = compositedBounds;
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
							renderer.LaserBakeTarget(), layer.bounds))
						{
							UnionRectInPlace(nextCompositedBounds, layer.bounds);
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
					renderer.CommitLaserBake();
					compositedBounds = nextCompositedBounds;
					renderer.ClearLaserIncrementalCoverage();
					layers.clear();
					return true;
				}
				renderer.ClearLaserIncrementalCoverage();
				// 增量 resolve 可能已写 scratch；完整回退先恢复已提交底色。
			if (!renderer.BeginLaserBake()) return false;
			}
			size_t completedLayerCount = 0;
			for (LaserStrokeLayer& layer : layers)
			{
				const std::vector<InkPoint>& points = LaserStrokeLayerPoints(layer);
				if (!ShouldCompositeLaserLayer(layer.cancelled, points.size())) continue;
				layer.bounds = RectFromLaserPoints(points, dpiScale, width, height);
				if (IsEmptyRect(layer.bounds)) continue;
				UnionRectInPlace(bakeDirty, layer.bounds);
				// 每支笔先独立生成 coverage，再按 Down 顺序烘入稳定预乘颜色。
				ConfigureLaserRendererStyle(renderer, layer.visualStyle, dpiScale);
				if (!renderer.ClearLaserCoverageRect(layer.bounds))
				{
					coverageMode = LaserCoverageMode::FullRedraw;
					return false;
				}
				renderer.SetLaserCoverageTarget(renderer.laserStrokeCoverage);
				if (renderer.DrawLaserCoverage(points) != 0 ||
					!renderer.ResolveLaserStrokeCoverage(
						renderer.LaserBakeTarget(), layer.bounds))
				{
					coverageMode = LaserCoverageMode::FullRedraw;
					return false;
				}
				UnionRectInPlace(nextCompositedBounds, layer.bounds);
				// 仅显式无窗口故障测试传入回调，生产路径没有状态注入。
				if (afterLayer) afterLayer(renderer, ++completedLayerCount, afterLayerContext);
			}
			renderer.CommitLaserBake();
			compositedBounds = nextCompositedBounds;
			renderer.ClearLaserIncrementalCoverage();
			layers.clear();
			return true;
		}

		bool DrawLaserStrokeLayers(std::vector<LaserStrokeLayer>& layers,
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
					if (!IntersectRect(&resolveBounds, &layer.bounds, &clipBounds)) return true;
					if (renderer.ResolveLaserIncrementalCoverage(target, resolveBounds))
						return true;
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
				if (!renderer.ClearLaserCoverageRect(resolveBounds)) return false;
				renderer.SetLaserCoverageTarget(renderer.laserStrokeCoverage);
				if (renderer.DrawLaserCoverage(points, resolveBounds) != 0 ||
					!renderer.ResolveLaserStrokeCoverage(target, resolveBounds))
					return false;
			}
			return true;
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

		LiveRasterSubmission DrawStablePrefix(RuntimeStroke& runtime, InkRenderer& renderer,
			int width, int height)
		{
			ActiveStroke& stroke = runtime.stroke;
			if (!stroke.hasCommittedGeometry) return {};
			if (runtime.tool == DrawingTool::Highlighter)
			{
				if (stroke.committedHighlighterGeometry.primitives.empty()) return {};
				renderer.SetOperatorTarget(renderer.layerL1);
				if (renderer.DrawHighlighterPrimitives(
					stroke.committedHighlighterGeometry.primitives,
					ColorForTool(runtime.tool, runtime.visualStyle)) < 0)
					return { {}, false };
				return { ClampRectToCanvas(
					stroke.committedHighlighterGeometry.bounds, width, height), true };
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
			if (renderer.DrawStrokeOrDot(stablePoints,
				ColorForTool(runtime.tool, runtime.visualStyle),
				StrokeShape::RoundCapsule, operatorKind) < 0) return { {}, false };
			return { RectFromStrokePoints(stablePoints, width, height), true };
		}

		bool DrawActiveShapePrimitives(const std::vector<RuntimeStroke*>& active,
			InkRenderer& renderer, std::vector<ShapePrimitive>& scratch)
		{
			bool succeeded = true;
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
					if (renderer.DrawShapePrimitives(scratch, kind,
						ColorForTool(DrawingTool::Pen, batchStyle->visualStyle)) < 0)
						succeeded = false;
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
			return succeeded;
		}

		LiveRasterSubmission RebuildActiveLayers(const std::vector<RuntimeStroke*>& active,
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
				const LiveRasterSubmission stable =
					DrawStablePrefix(*runtime, renderer, width, height);
				if (!stable.succeeded) return { {}, false };
				UnionRectInPlace(dirty, stable.dirty);
				if constexpr (kInterruptedStrokeReconnectManualTestModeEnabled)
					UnionRectInPlace(dirty,
						DrawReconnectManualTestRanges(*runtime, renderer, width, height));
				ActiveStroke& stroke = runtime->stroke;
				stroke.lastL0Rect = {};
				stroke.currentL0Rect = runtime->tool == DrawingTool::Highlighter
					? ClampRectToCanvas(stroke.l0HighlighterGeometry.bounds, width, height)
					: RectFromStrokePoints(stroke.l0DrawPoints, width, height);
				if (runtime->tool != DrawingTool::Eraser && !stroke.l0DrawPoints.empty())
				{
					if (!DrawL0LiveComposite(stroke,
						ColorForTool(runtime->tool, runtime->visualStyle),
						StrokeShape::RoundCapsule, renderer, false)) return { {}, false };
				}
				UnionRectInPlace(dirty, stroke.currentL0Rect);
			}
			if (!DrawActiveShapePrimitives(active, renderer, shapeScratch))
				return { {}, false };
			return { ClampRectToCanvas(dirty, width, height), true };
		}
	}

	// 与 Controller 同寿命、只由绘制 owner 写；借用的 Session 由 Host 在真 join 后封口。
	struct DrawingControllerMetricsState : ControllerContentMetrics
	{
		explicit DrawingControllerMetricsState(RuntimeMetricsSession& session) noexcept
			: ControllerContentMetrics(session) {}
		static std::unique_ptr<DrawingControllerMetricsState> Prepare(RuntimeMetricsSession* session) noexcept
		{
			if (!FitsBudget(session, 0, sizeof(DrawingControllerMetricsState))) return nullptr;
			return std::unique_ptr<DrawingControllerMetricsState>(
				new (std::nothrow) DrawingControllerMetricsState(*session));
		}
		void Mark(RuntimeMetricsFrameReason reason) noexcept
		{
			frame.reasonFlags |= static_cast<uint32_t>(reason);
			renderAttempt = true;
		}
		bool BeginRasterAttempt() noexcept
		{
			const bool previous = renderAttempt;
			renderAttempt = true;
			return previous;
		}
		void RasterReturned(bool previousAttempt, bool succeeded, bool didRasterWork = true) noexcept
		{
			// 合法cache miss/Empty不造帧；此前已发生的真实GPU尝试仍保留。
			if (!didRasterWork) renderAttempt = previousAttempt;
			else if (!succeeded) Mark(RuntimeMetricsFrameReason::RasterFailed);
		}
		void Begin(double startMs, uint32_t physicalBefore = 0) noexcept
		{
			metrics.BeginFrame();
			frame = {};
			frame.frameSerial = metrics.Snapshot().frameSerial;
			if (frame.frameSerial == 0) ExhaustIdentity();
			frame.frameStartMs = startMs;
			frame.physicalBefore = physicalBefore;
			if (physicalBefore) frame.reasonFlags |= static_cast<uint32_t>(RuntimeMetricsFrameReason::PhysicalBefore);
			frameOpen = true;
			frameRecorded = false;
			renderAttempt = false;
			candidateCount = 0;
			compositeReady = false;
		}
		void Finish(double wallMs = -1.0) noexcept
		{
			if (frameOpen && renderAttempt && !frameRecorded)
			{
				frame.wallMs = wallMs >= 0.0 ? wallMs : GetQpcTimeMilliseconds() - frame.frameStartMs;
				if (frame.physicalAfter) frame.reasonFlags |= static_cast<uint32_t>(RuntimeMetricsFrameReason::PhysicalAfter);
				frame.reasonFlags |= static_cast<uint32_t>(!frame.presentAttempted ? RuntimeMetricsFrameReason::NoPresent :
					frame.presentSucceeded ? RuntimeMetricsFrameReason::PresentSucceeded : RuntimeMetricsFrameReason::PresentFailed);
				RecordFrame(frame);
				frameRecorded = true;
			}
			frameOpen = false;
		}
		void EndContacts(ContentMetricInvalidationReason reason) noexcept
		{
			for (const auto& note : live) if (note.exists) Invalidate(note.key, reason);
			for (const auto& note : stored) if (note.exists) Invalidate(note.key, reason);
		}
		RuntimeMetricsFrameSample frame;
		bool frameOpen = false;
		bool frameRecorded = false;
		bool renderAttempt = false;
		bool runFrameActive = false;
		bool initialL2Cleared = false;
		int initialWidth = 0;
		int initialHeight = 0;
		bool wholeL2Clear = false;
	};
	static_assert(sizeof(DrawingControllerMetricsState) <= 64u * 1024);

	int RunLaserRasterFailureProductionProbe(InkRenderer& renderer,
		ID3D11Buffer* unwritableInkBuffer) noexcept
	{
		int failures = 0;
		const auto check = [&failures](bool condition, const char* name)
		{
			if (condition) return;
			++failures;
			std::fprintf(stderr, "[Draw3LaserRaster] FAIL: %s\n", name);
		};
		if (!renderer.device || !renderer.context || !renderer.backBufferTexture ||
			!renderer.backBufferRTV || !renderer.laserCompositedColor.texture ||
			!unwritableInkBuffer) return 1;
		Microsoft::WRL::ComPtr<ID3D11Buffer> writableInkBuffer = renderer.inkDataBuffer;
		D3D11_MAPPED_SUBRESOURCE rejectedMap = {};
		check(FAILED(renderer.context->Map(unwritableInkBuffer, 0,
			D3D11_MAP_WRITE_DISCARD, 0, &rejectedMap)),
			"real WARP Map rejects non-CPU-writable InkData buffer");

		const auto makeLayers = []
		{
			std::vector<LaserStrokeLayer> layers;
			LaserStrokeLayer first;
			first.id = 1;
			first.visualStyle = { 0xFF2020FFu, 5.0f };
			first.completedPoints = { { 12.0f, 12.0f, 3.0f, 0.0f },
				{ 46.0f, 46.0f, 3.0f, 1.0f } };
			layers.push_back(std::move(first));
			LaserStrokeLayer second;
			second.id = 2;
			second.visualStyle = { 0x2040FFFFu, 5.0f };
			second.completedPoints = { { 12.0f, 46.0f, 3.0f, 0.0f },
				{ 46.0f, 12.0f, 3.0f, 1.0f } };
			layers.push_back(std::move(second));
			return layers;
		};
		const RECT fullRect = { 0, 0, 64, 64 };
		const auto readCompositedBGRA = [&](std::vector<uint8_t>& pixels)
		{
			renderer.ClearRTV(renderer.backBufferRTV.Get(), kTransparentLayerClearColor);
			if (!renderer.ResolveLaserCompositedColor(
				renderer.backBufferRTV.Get(), fullRect, 1.0f)) return false;
			renderer.context->OMSetRenderTargets(0, nullptr, nullptr);
			D3D11_TEXTURE2D_DESC description = {};
			renderer.backBufferTexture->GetDesc(&description);
			description.Usage = D3D11_USAGE_STAGING;
			description.BindFlags = 0;
			description.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
			description.MiscFlags = 0;
			Microsoft::WRL::ComPtr<ID3D11Texture2D> staging;
			if (FAILED(renderer.device->CreateTexture2D(&description, nullptr,
				staging.GetAddressOf()))) return false;
			renderer.context->CopyResource(staging.Get(), renderer.backBufferTexture.Get());
			D3D11_MAPPED_SUBRESOURCE mapped = {};
			if (FAILED(renderer.context->Map(staging.Get(), 0,
				D3D11_MAP_READ, 0, &mapped))) return false;
			pixels.resize(64u * 64u * 4u);
			for (size_t row = 0; row < 64; ++row)
				std::memcpy(pixels.data() + row * 64u * 4u,
					static_cast<const uint8_t*>(mapped.pData) + row * mapped.RowPitch,
					64u * 4u);
			renderer.context->Unmap(staging.Get(), 0);
			return true;
		};

		renderer.ClearAllLaserCoverage();
		std::vector<uint8_t> blank;
		check(readCompositedBGRA(blank), "read transparent baseline BGRA");
		std::vector<LaserStrokeLayer> referenceLayers = makeLayers();
		RECT referenceBounds = {}, referenceDirty = {};
		LaserCoverageMode referenceMode = LaserCoverageMode::FullRedraw;
		check(BakeLaserStrokeLayers(referenceLayers, renderer, 1.0f, 64, 64,
			referenceBounds, referenceDirty, referenceMode),
			"reference two-layer bake submits all passes");
		std::vector<uint8_t> expected;
		check(readCompositedBGRA(expected), "read two-layer reference BGRA");
		check(expected != blank, "two-layer Laser reference has visible pixels");

		renderer.ClearAllLaserCoverage();
		std::vector<LaserStrokeLayer> retryLayers = makeLayers();
		RECT retryBounds = {}, retryDirty = {};
		LaserCoverageMode retryMode = LaserCoverageMode::FullRedraw;
		struct FailureAfterFirstLayer
		{
			ID3D11Buffer* unwritable = nullptr;
			bool switched = false;
		};
		FailureAfterFirstLayer injection{ unwritableInkBuffer };
		const auto afterLayer = [](InkRenderer& target, size_t completed, void* context)
		{
			auto& failure = *static_cast<FailureAfterFirstLayer*>(context);
			if (completed != 1) return;
			target.inkDataBuffer = failure.unwritable;
			failure.switched = true;
		};
		// 第一层按生产 shader 成功 source-over，第二层用真实 Map 失败中断。
		const bool failedBake = BakeLaserStrokeLayers(retryLayers, renderer,
			1.0f, 64, 64, retryBounds, retryDirty, retryMode,
			afterLayer, &injection);
		renderer.inkDataBuffer = writableInkBuffer;
		check(injection.switched, "fault begins only after first layer");
		check(!failedBake, "second-layer Map failure reports failed bake");
		check(retryLayers.size() == 2 && retryLayers[0].id == 1 &&
			retryLayers[1].id == 2 && retryLayers[0].completedPoints.size() == 2 &&
			retryLayers[1].completedPoints.size() == 2,
			"failed bake retains both ordered CPU layers");
		std::vector<uint8_t> afterFailure;
		check(readCompositedBGRA(afterFailure), "read failed-bake BGRA");
		check(afterFailure == blank,
			"failed second pass leaves committed compositor unchanged");
		check(BakeLaserStrokeLayers(retryLayers, renderer, 1.0f, 64, 64,
			retryBounds, retryDirty, retryMode),
			"restored buffer submits retained layers once");
		std::vector<uint8_t> afterRetry;
		check(readCompositedBGRA(afterRetry), "read retried two-layer BGRA");
		check(afterRetry == expected && retryLayers.empty(),
			"retry has exact reference BGRA without duplicate source-over");
		std::vector<LaserStrokeLayer> liveLayers = makeLayers();
		for (LaserStrokeLayer& layer : liveLayers)
			layer.bounds = RectFromLaserPoints(layer.completedPoints, 1.0f, 64, 64);
		LaserCoverageMode liveMode = LaserCoverageMode::FullRedraw;
		renderer.inkDataBuffer = unwritableInkBuffer;
		check(!DrawLaserStrokeLayers(liveLayers, renderer,
			renderer.backBufferRTV.Get(), fullRect, 1.0f, liveMode),
			"full Laser redraw reports real Map failure");
		renderer.inkDataBuffer = writableInkBuffer;
		renderer.ClearRTV(renderer.backBufferRTV.Get(), kTransparentLayerClearColor);
		check(DrawLaserStrokeLayers(liveLayers, renderer,
			renderer.backBufferRTV.Get(), fullRect, 1.0f, liveMode),
			"full Laser redraw retries from ordered CPU layers");
		return failures;
	}

	int RunDraw3ControlFenceProductionProbe() noexcept
	{
		int failures = 0;
		const auto check = [&failures](bool condition, const char* name)
		{
			if (condition) return;
			++failures;
			std::fprintf(stderr, "[Draw3ControlFence] FAIL: %s\n", name);
		};
		ContactInputCoordinator input;
		WindowController window;
		const auto snapshot = [](ContactPhase phase)
		{
			ContactSnapshot value{};
			value.position = { 1.0f, 1.0f };
			value.qpc = 1;
			value.phase = phase;
			return value;
		};
		check(input.PublishDown(0xB1, 1, InputDeviceType::Pen,
			snapshot(ContactPhase::Down)) &&
			input.PublishUp(0xB1, 1, snapshot(ContactPhase::Up)),
			"old contact accepted before Command");
		check(input.TryReserveCommandWake(), "Command marker reserved");
		input.PublishReservedCommandWake();
		check(input.PublishDown(0xB1, 2, InputDeviceType::Pen,
			snapshot(ContactPhase::Down)) &&
			input.PublishCancelled(0xB1, 2, snapshot(ContactPhase::Cancelled)),
			"new contact accepted after Command");
		std::vector<int> observed;
		bool commandBoundaryPending = false;
		const auto process = [&](ContactRecord* record)
		{
			if (!record)
			{
				check(input.LastDequeuedControlWakeKind() == ControlWakeKind::Command,
					"probe consumes a business Command marker");
				CanvasCommand command;
				command.type = CanvasCommandType::Clear;
				window.EnqueueCanvasCommand(command);
				observed.push_back(0);
				return;
			}
			observed.push_back(static_cast<int>(record->ContactId()));
			const ContactHandle handle{ record, record->Generation() };
			ContactSnapshot terminal{};
			check(input.TryReadSnapshot(handle, terminal) &&
				(terminal.phase == ContactPhase::Up ||
					terminal.phase == ContactPhase::Cancelled),
				"probe keeps each contact terminal");
			input.Recycle(handle);
		};
		DrainIngressBatch(input, window, commandBoundaryPending, process);
		check(observed == std::vector<int>{ 1, 0 } &&
			commandBoundaryPending && window.HasPendingCanvasCommand(),
			"Run ingress batch stops before new Down while Clear is queued");
		CanvasCommand command;
		check(window.TryDequeueCanvasCommand(command) &&
			command.type == CanvasCommandType::Clear,
			"queued Clear remains intact at the boundary");
		commandBoundaryPending = window.HasPendingCanvasCommand();
		DrainIngressBatch(input, window, commandBoundaryPending, process);
		check(observed == std::vector<int>{ 1, 0, 2 },
			"new Down resumes after Clear has left the canvas queue");
		return failures;
	}

	int RunFallbackStableControllerIsolationProbe() noexcept
	{
		Bridge::PresentationTarget fallback;
		fallback.key.bytes[0] = 0xF1;
		fallback.sourceIdentity = "path:c:\\lessons\\fallback-stable.pptx";
		fallback.bindingMode = Bridge::SlideBindingMode::PageIndexFallback;
		fallback.totalPages = 2;
		fallback.pageIndex = 0;
		fallback.bindingRevision = 1;
		Bridge::PresentationTarget stable = fallback;
		stable.bindingMode = Bridge::SlideBindingMode::StableSlideId;
		stable.slideIds = { 202, 101 };
		stable.slideId = 202;
		int failures = 0;
		const auto check = [&failures](bool condition, const char* name)
		{
			if (condition) return;
			++failures;
			std::fprintf(stderr, "[Draw3FallbackStable] FAIL: %s\n", name);
		};
		check(Bridge::ValidPresentationPage(fallback) &&
			Bridge::ValidPresentationPage(stable), "both target fixtures are valid");
		check(CanReusePresentationDocumentSlot(fallback, fallback, 3),
			"unchanged fallback target still reuses its own lane");
		// 旧 ordinal 与新 SlideID 没有一一对应凭证，不允许继承旧笔迹/GUID。
		check(!CanUpgradePresentationBindingByOrdinal(fallback, stable, 3),
			"fallback cannot be upgraded to Stable by ordinal");
		check(!CanReusePresentationDocumentSlot(fallback, stable, 3),
			"fallback and Stable require separate document lanes");
		return failures;
	}

	int RunFallbackStableControllerLaneProbe() noexcept
	{
		int failures = 0;
		const auto check = [&failures](bool condition, const char* name)
		{
			if (condition) return;
			++failures;
			std::fprintf(stderr, "[Draw3PptLane] FAIL: %s\n", name);
		};
		try
		{
			const auto guid = [](std::uint8_t marker)
			{
				std::array<std::uint8_t, 16> bytes{};
				bytes[0] = marker;
				bytes[15] = 0xA5;
				return InkGuid(bytes);
			};
			Bridge::PresentationTarget fallback;
			fallback.key.bytes[0] = 0xF1;
			fallback.sourceIdentity = "path:c:\\lessons\\fallback-stable.pptx";
			fallback.bindingMode = Bridge::SlideBindingMode::PageIndexFallback;
			fallback.totalPages = 2;
			fallback.pageIndex = 0;
			fallback.bindingRevision = 1;
			Bridge::PresentationTarget stable = fallback;
			stable.bindingMode = Bridge::SlideBindingMode::StableSlideId;
			stable.slideIds = { 202, 101 };
			stable.slideId = 202;
			std::optional<InkCanvasCollection> document;
			document.emplace(guid(0x10));
			std::vector<CanvasPageRuntimeState> runtimes(3);
			for (std::uint8_t index = 0; index < 3; ++index)
			{
				const auto appended = document->AppendPage(guid(0x20 + index));
				InkPage* page = appended ? document->PageAt(*appended) : nullptr;
				if (!page || !page->GetOrCreateCanvas(kDefaultDeviceKey))
				{
					check(false, "build fallback real pages and separate EndScreen");
					return 1;
				}
			}
			InkPage* oldPage = document->PageAt(0);
			InkCanvas* oldCanvas = oldPage
				? oldPage->GetOrCreateCanvas(kDefaultDeviceKey) : nullptr;
			if (!oldCanvas || !oldCanvas->AppendStroke(InkStroke({},
				{ { 10.0f, 10.0f, 3.0f }, { 20.0f, 20.0f, 3.0f } })))
			{
				check(false, "build fallback ink");
				return 1;
			}
			const auto footprint = BuildStrokeTileFootprint(oldCanvas->Strokes()[0]);
			if (!footprint || !runtimes[0].history.AppendStroke(0, *footprint, true))
			{
				check(false, "build fallback history");
				return 1;
			}
			const auto oldWorkspaceGuid = document->WorkspaceGuid().Bytes();
			const auto oldPageGuid = oldPage->PageGuid().Bytes();
			RetainedPresentationSlides retained;
			std::size_t pageIndex = 0;
			std::optional<Bridge::PresentationTarget> activeTarget = fallback;
			std::optional<draw3::uink::UInkGuid> fileGuid =
				draw3::uink::UInkGuid(guid(0x30).Bytes());
			std::uint64_t mutationRevision = 2;
			std::uint64_t queuedRevision = 2;
			std::uint64_t committedRevision = 1;
			bool initialized = true;
			bool loadPending = false;
			std::uint64_t generation = 1;
			Bridge::Workspace workspace = Bridge::Workspace::Presentation;
			std::optional<Bridge::PresentationKey> activeKey = fallback.key;
			PresentationParkedSlots parked;
			auto [source, inserted] = parked.try_emplace(LaneFor(fallback));
			(void)inserted;
			DrawingDocumentSlot isolated;
			std::uint64_t nextSlotGeneration = 2;
			std::uint64_t nextRasterToken = 0;
			const auto allocate = [&]() noexcept -> InkRasterStateToken
			{ return ++nextRasterToken; };
			const ActivePresentationSlotRefs active{ document, runtimes, retained,
				pageIndex, activeTarget, fileGuid, mutationRevision, queuedRevision,
				committedRevision, initialized, loadPending, generation };
			const auto switched = SwitchPresentationCpuSlot(workspace, activeKey,
				active, source->second, isolated, parked, nextSlotGeneration,
				stable, allocate);
			check(switched.accepted && !switched.topologyConflict,
				"same source cross-mode switch selects a persistent new lane");
			check(activeKey == stable.key && activeTarget &&
				activeTarget->bindingMode == Bridge::SlideBindingMode::StableSlideId,
				"new Stable lane retains save/load target identity");
			check(document && document->Pages().size() == 3 &&
				document->WorkspaceGuid().Bytes() != oldWorkspaceGuid &&
				!fileGuid && generation != 0,
				"new Stable lane has independent N+1 pages, workspace and file identity");
			const auto old = parked.find(LaneFor(fallback));
			check(old != parked.end() && old->second.document &&
				old->second.presentationTarget &&
				old->second.document->WorkspaceGuid().Bytes() == oldWorkspaceGuid &&
				old->second.document->PageAt(0)->PageGuid().Bytes() == oldPageGuid &&
				old->second.mutationRevision == 2 && old->second.queuedRevision == 2 &&
				old->second.committedRevision == 1 && old->second.slotGeneration == 1,
				"old fallback ink and accepted pending save remain in their lane");
			if (switched.accepted && !switched.topologyConflict && activeTarget &&
				document && old != parked.end() && old->second.document &&
				old->second.presentationTarget)
			{
				auto oldRequest = BuildPresentationSaveRequest(
					*old->second.document, old->second.pageRuntimeStates,
					old->second.currentPageIndex, *old->second.presentationTarget,
					old->second.fileGuid, old->second.mutationRevision, 1.0f,
					old->second.retainedSlides, std::nullopt,
					old->second.slotGeneration);
				check(oldRequest && oldRequest->snapshot.workspaceType ==
					draw3::uink::kInkeysPageIndexWorkspaceType &&
					oldRequest->snapshot.workspaceGuid.Bytes() == oldWorkspaceGuid &&
					oldRequest->snapshot.activeCanvases.size() == 3 &&
					oldRequest->snapshot.activeCanvases[0].strokes.size() == 1 &&
					!oldRequest->snapshot.activeCanvases[0].slideId &&
					oldRequest->snapshot.activeCanvases[0].pageGuid.Bytes() ==
						oldPageGuid && oldRequest->slotGeneration == 1,
					"parked fallback Exit request keeps old ink, page GUID and mode");
				PresentationPersistenceCompletion oldSave;
				oldSave.operation = PresentationPersistenceOperation::Save;
				oldSave.status = PresentationPersistenceStatus::Committed;
				oldSave.target = fallback;
				oldSave.slotGeneration = 1;
				oldSave.storageTrack = PresentationStorageTrack::Base;
				oldSave.fileGuid = old->second.fileGuid;
				oldSave.mutationRevision = 2;
				const auto saveRoute = RoutePresentationCompletion(oldSave, workspace,
					activeKey, activeTarget, generation, document, parked);
				check(!saveRoute.active && saveRoute.parked == &old->second &&
					PresentationSaveCompletionMatchesSlot(oldSave,
						old->second.fileGuid, old->second.queuedRevision),
					"late fallback Save maps only to its old generation/file");
				oldSave.fileGuid = draw3::uink::UInkGuid(guid(0x31).Bytes());
				check(!PresentationSaveCompletionMatchesSlot(oldSave,
					old->second.fileGuid, old->second.queuedRevision),
					"wrong fallback file GUID cannot commit a lane");
				oldSave.slotGeneration = generation;
				check(!RoutePresentationCompletion(oldSave, workspace, activeKey,
					activeTarget, generation, document, parked).parked,
					"stale slot generation cannot alias another lane");
				PresentationPersistenceCompletion oldLoad;
				oldLoad.operation = PresentationPersistenceOperation::Load;
				oldLoad.target = fallback;
				oldLoad.slotGeneration = 1;
				for (const auto kind : { PresentationLoadKind::Current,
					PresentationLoadKind::PreviousInterval })
				{
					oldLoad.loadKind = kind;
					const auto route = RoutePresentationCompletion(oldLoad,
						workspace, activeKey, activeTarget, generation, document, parked);
					check(!route.active && route.parked == &old->second,
						"late fallback Load/PreviousInterval cannot update Stable");
				}
				PresentationPersistenceCompletion stableNotFound;
				stableNotFound.operation = PresentationPersistenceOperation::Load;
				stableNotFound.target = stable;
				stableNotFound.slotGeneration = generation;
				stableNotFound.status = PresentationPersistenceStatus::NotFound;
				stableNotFound.storageTrack = PresentationStorageTrack::SlideIdSidecar;
				check(PresentationEmptyLaneVerified(stableNotFound),
					"selected Stable sidecar NotFound admits a new empty session");
				for (const auto status : { PresentationPersistenceStatus::IoError,
					PresentationPersistenceStatus::SourceChanged,
					PresentationPersistenceStatus::CrossProcessConflictDeferred })
				{
					stableNotFound.status = status;
					check(!PresentationEmptyLaneVerified(stableNotFound),
						"load failure/foreign cannot publish blank as recovered");
				}
				stableNotFound.status = PresentationPersistenceStatus::NotFound;
				stableNotFound.storageTrack = PresentationStorageTrack::PageIndexSidecar;
				check(!PresentationEmptyLaneVerified(stableNotFound),
					"fallback sidecar cannot certify an empty Stable lane");
				stableNotFound.storageTrack = PresentationStorageTrack::Unresolved;
				check(!PresentationEmptyLaneVerified(stableNotFound),
					"unresolved index cannot masquerade as NotFound");
				check(document->PageAt(2) && document->PageAt(1) &&
					document->PageAt(2)->PageGuid().Bytes() !=
						document->PageAt(1)->PageGuid().Bytes() &&
					document->PageAt(0)->PageGuid().Bytes() != oldPageGuid,
					"Stable EndScreen and real slides use new independent page GUIDs");
				auto stableRequest = BuildPresentationSaveRequest(*document,
					runtimes, pageIndex, *activeTarget, fileGuid, 1, 1.0f,
					retained, std::nullopt, generation);
				check(stableRequest && stableRequest->snapshot.workspaceType == 2 &&
					stableRequest->snapshot.activeCanvases.size() == 3 &&
					stableRequest->snapshot.activeCanvases[0].slideId == 202 &&
					stableRequest->snapshot.activeCanvases[1].slideId == 101 &&
					!stableRequest->snapshot.activeCanvases[2].slideId &&
					draw3::uink::InkeysPageKind(
						stableRequest->snapshot.activeCanvases[2].extra) ==
						draw3::uink::UInkInkeysPageKind::EndScreen &&
					stableRequest->slotGeneration == generation &&
					fileGuid != old->second.fileGuid,
					"new Stable save request uses SlideID/end marker and separate file");
				const auto stableWorkspaceGuid = document->WorkspaceGuid().Bytes();
				const auto stableGeneration = generation;
				auto stableSource = parked.find(LaneFor(stable));
				check(stableSource != parked.end(), "Stable parked lane exists for reverse switch");
				if (stableSource != parked.end())
				{
					const auto back = SwitchPresentationCpuSlot(workspace, activeKey,
						active, stableSource->second, isolated, parked,
						nextSlotGeneration, fallback, allocate);
					check(back.accepted && !back.topologyConflict && document &&
						document->WorkspaceGuid().Bytes() == oldWorkspaceGuid &&
						generation == 1 && activeTarget &&
						activeTarget->bindingMode == Bridge::SlideBindingMode::PageIndexFallback,
						"return to fallback recovers original ink lane");
					const auto stableParked = parked.find(LaneFor(stable));
					check(stableParked != parked.end() && stableParked->second.document &&
						stableParked->second.document->WorkspaceGuid().Bytes() ==
								stableWorkspaceGuid &&
						stableParked->second.slotGeneration == stableGeneration,
						"Stable document remains separately parked");
					const auto fallbackSource = parked.find(LaneFor(fallback));
					if (fallbackSource != parked.end())
					{
						const auto forward = SwitchPresentationCpuSlot(workspace, activeKey,
							active, fallbackSource->second, isolated, parked,
							nextSlotGeneration, stable, allocate);
						check(forward.accepted && !forward.topologyConflict && document &&
							document->WorkspaceGuid().Bytes() == stableWorkspaceGuid &&
							generation == stableGeneration,
							"repeat Stable target reuses its existing lane");
					}
				}
				Bridge::PresentationTarget failedTarget = stable;
				failedTarget.key.bytes[0] = 0xF2;
				failedTarget.sourceIdentity += ":new";
				const auto beforeFailureGuid = document->WorkspaceGuid().Bytes();
				const auto beforeFailureGeneration = generation;
				const auto failAllocation = []() -> InkRasterStateToken { throw 1; };
				stableSource = parked.find(LaneFor(stable));
				if (stableSource != parked.end())
				{
					const auto failed = SwitchPresentationCpuSlot(workspace, activeKey,
						active, stableSource->second, isolated, parked,
						nextSlotGeneration, failedTarget, failAllocation);
					check(!failed.accepted && document &&
						document->WorkspaceGuid().Bytes() == beforeFailureGuid &&
						generation == beforeFailureGeneration && activeTarget &&
						activeTarget->bindingMode == Bridge::SlideBindingMode::StableSlideId,
						"candidate allocation failure keeps authoritative slot unchanged");
				}
			}
		}
		catch (...)
		{
			check(false, "probe exception");
		}
		if (failures == 0)
			std::fputs("[Draw3PptLane] PASS: production CPU switch keeps fallback and Stable separate\n",
				stderr);
		return failures == 0 ? 0 : 1;
	}

	int RunPresentationCurrentLoadRetryProbe() noexcept
	{
		int failures = 0;
		const auto check = [&failures](bool condition, const char* name)
		{
			if (condition) return;
			++failures;
			std::fprintf(stderr, "[Draw3PptLoadRetry] FAIL: %s\n", name);
		};
		Bridge::PresentationTarget target;
		target.key.bytes[0] = 0xB1;
		target.sourceIdentity = "path:c:\\lessons\\retry.pptx";
		target.bindingMode = Bridge::SlideBindingMode::StableSlideId;
		target.totalPages = 1;
		target.slideIds = { 101 };
		target.slideId = 101;
		target.targetRevision = 5;
		target.sessionRevision = 9;
		constexpr std::uint64_t generation = 7;
		int submitted = 0;
		const auto submit = [&](PresentationLoadRequest&& request)
		{
			check(request.kind == PresentationLoadKind::Current &&
				request.target == target && request.slotGeneration == generation,
				"production Current request preserves target and slot generation");
			++submitted;
			return true;
		};
		bool loadPending = SubmitCurrentPresentationLoad(target, generation, submit);
		bool initialized = false;
		check(loadPending && submitted == 1 &&
			PresentationLoadUnresolved(Bridge::Workspace::Presentation,
				initialized, 0), "first async load keeps input closed");
		PresentationCurrentLoadRetry retry;
		loadPending = false; // 同一代次的 worker 返回一次性 IoError。
		retry.OnIoError(target, generation, 1000);
		check(!retry.TrySubmit(target, generation, 1249,
			loadPending, initialized, submit) && submitted == 1,
			"transient failure does not busy retry before deadline");
		const bool retried = retry.TrySubmit(target, generation, 1250,
			loadPending, initialized, submit);
		check(retried && loadPending && submitted == 2,
			"same targetRevision retries Current load once after deadline");
		check(!retry.TrySubmit(target, generation, 1250,
			loadPending, initialized, submit) && submitted == 2,
			"in-flight retry has no duplicate submit");
		PresentationPersistenceCompletion notFound;
		notFound.operation = PresentationPersistenceOperation::Load;
		notFound.target = target;
		notFound.slotGeneration = generation;
		notFound.status = PresentationPersistenceStatus::NotFound;
		notFound.storageTrack = PresentationStorageTrack::Base;
		if (retried && PresentationEmptyLaneVerified(notFound))
		{
			loadPending = false;
			initialized = true;
			retry.Cancel();
		}
		check(initialized && !PresentationLoadUnresolved(
			Bridge::Workspace::Presentation, initialized, 0),
			"verified NotFound ends recovery gate after a successful retry");
		PresentationCurrentLoadRetry repeated;
		std::uint64_t nowMs = 5000;
		bool repeatedPending = false;
		for (std::size_t index = 0;
			index < PresentationCurrentLoadRetry::kDelaysMs.size(); ++index)
		{
			repeated.OnIoError(target, generation, nowMs);
			const auto wait = repeated.RemainingWaitMilliseconds(
				target, generation, nowMs);
			check(wait && *wait == static_cast<double>(
				PresentationCurrentLoadRetry::kDelaysMs[index]),
				"retry wait follows bounded deadline without polling");
			nowMs += PresentationCurrentLoadRetry::kDelaysMs[index];
			const int before = submitted;
			check(repeated.TrySubmit(target, generation, nowMs,
				repeatedPending, false, submit) && repeatedPending &&
				submitted == before + 1,
				"each transient error permits only its due retry");
			repeatedPending = false; // 下一次 worker 仍返回 IoError。
		}
		repeated.OnIoError(target, generation, nowMs);
		const int afterLimit = submitted;
		check(!repeated.RemainingWaitMilliseconds(target, generation, nowMs) &&
			!repeated.TrySubmit(target, generation, nowMs + 10000,
				repeatedPending, false, submit) && submitted == afterLimit,
			"persistent IoError stops after four automatic submissions");
		PresentationCurrentLoadRetry loadedRetry;
		loadedRetry.OnIoError(target, generation, 20000);
		bool loadedPending = false;
		const bool loadedSubmitted = loadedRetry.TrySubmit(target, generation,
			20250, loadedPending, false, submit);
		std::array<std::uint8_t, 16> bytes{};
		bytes[0] = 0xA5;
		bytes[15] = 0x5A;
		const draw3::uink::UInkGuid recoveredGuid(bytes);
		auto snapshot = std::make_shared<draw3::uink::Draw3UInkExportSnapshot>();
		snapshot->fileGuid = recoveredGuid;
		snapshot->workspaceType = 2;
		PresentationPersistenceCompletion loaded;
		loaded.operation = PresentationPersistenceOperation::Load;
		loaded.target = target;
		loaded.slotGeneration = generation;
		loaded.status = PresentationPersistenceStatus::Loaded;
		loaded.storageTrack = PresentationStorageTrack::SlideIdSidecar;
		loaded.fileGuid = recoveredGuid;
		loaded.loadedSnapshot = snapshot;
		bool loadedInitialized = loadedSubmitted &&
			PresentationLoadedForLane(loaded);
		if (loadedInitialized) loadedRetry.Cancel();
		check(loadedInitialized && !PresentationLoadUnresolved(
			Bridge::Workspace::Presentation, loadedInitialized, 0),
			"strict Loaded after retry also releases the input gate");
		PresentationCurrentLoadRetry terminal;
		terminal.OnIoError(target, generation, 2000);
		terminal.OnTerminalFailure();
		bool terminalPending = false;
		check(!terminal.TrySubmit(target, generation, 10000,
			terminalPending, false, submit) && !terminalPending,
			"SourceChanged/foreign remains fail-closed without automatic retry");
		Bridge::PresentationTarget explicitTarget = target;
		++explicitTarget.targetRevision;
		terminal.Cancel();
		int explicitRequests = 0;
		check(SubmitCurrentPresentationLoad(explicitTarget, generation,
			[&](PresentationLoadRequest&& request)
			{
				++explicitRequests;
				return request.target == explicitTarget &&
					request.slotGeneration == generation;
			}) && explicitRequests == 1,
			"new explicit target can safely request a fresh Current read");
		PresentationCurrentLoadRetry replaced;
		replaced.OnIoError(target, generation, 2000);
		bool replacedPending = false;
		check(!replaced.TrySubmit(target, generation + 1, 10000,
			replacedPending, false, submit) && !replacedPending,
			"new slot generation cancels stale retry");
		replaced.OnIoError(target, generation, 2000);
		Bridge::PresentationTarget newer = target;
		++newer.targetRevision;
		check(!replaced.TrySubmit(newer, generation, 10000,
			replacedPending, false, submit) && !replacedPending,
			"new target revision cancels old retry");
		replaced.OnIoError(target, generation, 2000);
		replaced.Cancel();
		check(!replaced.TrySubmit(target, generation, 10000,
			replacedPending, false, submit),
			"Exit cancels a scheduled retry");
		PresentationCurrentLoadRetry exitBarrier;
		exitBarrier.OnIoError(target, generation, 1000);
		int exitCaptures = 0;
		int exitAcks = 0;
		ProcessPresentationExitBarrier(exitBarrier,
			[&] { ++exitCaptures; }, [&] { ++exitAcks; });
		check(exitBarrier.ExitPrepared() && !exitBarrier.AllowsLoad(false),
			"ACK terminal state also rejects fresh/manual Current loads");
		check(!CanvasCommandAllowedAfterExitBarrier(exitBarrier,
			CanvasCommandType::SetPresentationTarget) &&
			!CanvasCommandAllowedAfterExitBarrier(exitBarrier,
				CanvasCommandType::Clear) &&
			!CanvasCommandAllowedAfterExitBarrier(exitBarrier,
				CanvasCommandType::PrepareExitAutoSave) &&
			!CanvasCommandAllowedAfterExitBarrier(exitBarrier,
				CanvasCommandType::PresentationPersistenceCompleted),
			"terminal command gate blocks late target/Clear/completion Save");
		// 同一 command 批次可在退出 ACK 后收到迟到的 Current IoError。
		exitBarrier.OnIoError(target, generation, 1001);
		bool exitLoadPending = false;
		const int beforeExit = submitted;
		const bool afterAckSubmitted = TrySubmitCurrentLoadAtRunSafePoint(
			exitBarrier, target, generation, 1251, exitLoadPending, false,
			false, submit);
		check(!afterAckSubmitted && !exitLoadPending && submitted == beforeExit &&
			exitCaptures == 1 && exitAcks == 1,
			"PrepareExit ACK and late IoError cannot enqueue another Load");
		ProcessPresentationExitBarrier(exitBarrier,
			[&] { ++exitCaptures; }, [&] { ++exitAcks; });
		check(exitCaptures == 1 && exitAcks == 1,
			"duplicate PrepareExit cannot capture Save or ACK twice");
		PresentationCurrentLoadRetry failedCapture;
		int failedCaptureAck = 0;
		ProcessPresentationExitBarrier(failedCapture,
			[] { throw 1; }, [&] { ++failedCaptureAck; });
		check(failedCapture.ExitPrepared() && failedCaptureAck == 1,
			"exit capture failure still sends one final barrier ACK");
		PresentationCurrentLoadRetry directExit;
		directExit.OnIoError(target, generation, 1000);
		bool directExitPending = false;
		const int beforeDirectExit = submitted;
		check(!TrySubmitCurrentLoadAtRunSafePoint(directExit,
			target, generation, 1250, directExitPending, false, true, submit) &&
			!directExitPending && submitted == beforeDirectExit,
			"ExitRequested suppresses a due retry at the Run safe point");
		if (failures == 0)
			std::fputs("[Draw3PptLoadRetry] PASS: bounded same-target Current recovery\n",
				stderr);
		return failures == 0 ? 0 : 1;
	}

	int RunIgnoredLaserTouchProductionTest() noexcept
	{
		int failures = 0;
		const auto check = [&](bool condition, const char* label)
		{
			if (condition) return;
			++failures;
			std::fprintf(stderr, "[Draw3LaserIgnoredTouch] FAIL: %s\n", label);
		};
		ContactInputCoordinator input;
		input.EnableDiagnostics(true);
		constexpr uint32_t tablet = 0xF060;
		const auto sample = [](float x, ContactPhase phase)
		{
			ContactSnapshot snapshot;
			snapshot.position = { x, 20.0f };
			snapshot.phase = phase;
			snapshot.qpc = 1000;
			return snapshot;
		};
		auto dequeueContact = [&]() -> ContactHandle
		{
			ContactRecord* record = nullptr;
			while (input.TryDequeue(record))
			{
				if (record) return { record, record->Generation() };
				input.AcknowledgeControlWake();
			}
			return {};
		};
		if (!input.PublishDown(tablet, 1, InputDeviceType::Touch,
			sample(10.0f, ContactPhase::Down))) return 1;
		const ContactHandle first = dequeueContact();
		if (!first) return 1;
		ContactSnapshot firstBeforeBatch;
		check(!IgnoreAdditionalLaserTouch(input, first, DrawingTool::Laser,
			InputDeviceType::Touch, false, false) &&
			input.TryReadSnapshot(first, firstBeforeBatch),
			"first Laser Touch is admitted when no prior touch is active");
		bool allIgnoredRetired = true;
		bool firstPreserved = true;
		bool generationAdvanced = true;
		ContactRecord* ignoredSlot = nullptr;
		uint64_t previousGeneration = 0;
		for (uint32_t index = 0; index < 32; ++index)
		{
			const uint32_t contactId = index + 2;
			if (!input.PublishDown(tablet, contactId, InputDeviceType::Touch,
				sample(30.0f, ContactPhase::Down)))
			{
				check(false, "repeated ignored Down still has available slot");
				break;
			}
			const ContactHandle ignored = dequeueContact();
			if (!ignored)
			{
				check(false, "ignored Touch Down is dequeued");
				break;
			}
			if (ignoredSlot)
				generationAdvanced &= ignored.record == ignoredSlot &&
					ignored.generation != previousGeneration;
			ignoredSlot = ignored.record;
			previousGeneration = ignored.generation;
			// 测试完整生产判定：开启多指时第二根也不应被拒收。
			if (index == 0)
			{
				ContactSnapshot enabledSnapshot;
				check(!IgnoreAdditionalLaserTouch(input, ignored, DrawingTool::Laser,
					InputDeviceType::Touch, true, true) &&
					input.TryReadSnapshot(ignored, enabledSnapshot),
					"multi-touch enabled keeps the second Touch route");
			}
			allIgnoredRetired &= IgnoreAdditionalLaserTouch(input, ignored,
				DrawingTool::Laser, InputDeviceType::Touch, true, false);
			ContactSnapshot abandoned;
			allIgnoredRetired &= !input.TryReadSnapshot(ignored, abandoned);
			const ContactPhase terminal = index % 2 == 0
				? ContactPhase::Up : ContactPhase::Cancelled;
			if (terminal == ContactPhase::Up)
				input.PublishUp(tablet, contactId, sample(35.0f, terminal));
			else input.PublishCancelled(tablet, contactId, sample(35.0f, terminal));
			const auto diagnostics = input.DiagnosticsSnapshot();
			allIgnoredRetired &= diagnostics.occupiedSlots == 1 &&
					diagnostics.recycled == index + 1;
			ContactSnapshot firstSnapshot;
			firstPreserved &= input.TryReadSnapshot(first, firstSnapshot) &&
				firstSnapshot.phase == ContactPhase::Down && input.ContactAdmitted(first);
			input.Recycle(ignored); // 红测手动清理，不能让旧泄漏污染后续迭代。
		}
		const auto keepOtherInput = [&](uint32_t contactId, InputDeviceType device,
			DrawingTool tool, const char* label)
		{
			if (!input.PublishDown(tablet, contactId, device,
				sample(60.0f, ContactPhase::Down)))
			{
				check(false, label);
				return;
			}
			const ContactHandle ordinary = dequeueContact();
			if (!ordinary)
			{
				check(false, label);
				return;
			}
			ContactSnapshot visible;
			check(!IgnoreAdditionalLaserTouch(input, ordinary, tool, device, true, false) &&
				input.TryReadSnapshot(ordinary, visible), label);
			input.PublishUp(tablet, contactId, sample(65.0f, ContactPhase::Up));
			input.Recycle(ordinary);
		};
		keepOtherInput(200, InputDeviceType::Pen, DrawingTool::Laser,
			"non-Touch input is not suppressed by Laser multi-touch gate");
		keepOtherInput(201, InputDeviceType::Touch, DrawingTool::Pen,
			"non-Laser tool is not suppressed by Laser multi-touch gate");
		check(allIgnoredRetired, "ignored Up/Cancel automatically retire without consuming slots");
		check(generationAdvanced, "ignored slot reuses a new generation without ABA");
		check(firstPreserved, "first Laser Touch remains active throughout ignored contacts");
		const uint64_t recycledBeforeClosing = input.DiagnosticsSnapshot().recycled;
		if (input.PublishDown(tablet, 100, InputDeviceType::Touch,
			sample(40.0f, ContactPhase::Down)))
		{
			const ContactHandle ignored = dequeueContact();
			if (ignored)
			{
				ContactClosePauseForTesting pause;
				input.PauseNextCloseAfterRouteClosedForTesting(&pause);
				std::thread producer([&]
					{ input.PublishUp(tablet, 100, sample(45.0f, ContactPhase::Up)); });
				const auto waitFor = [](const std::atomic<bool>& flag, DWORD timeoutMs)
				{
					const ULONGLONG deadline = GetTickCount64() + timeoutMs;
					while (!flag.load(std::memory_order_acquire) &&
						GetTickCount64() < deadline) Sleep(1);
					return flag.load(std::memory_order_acquire);
				};
				const bool closingEntered = waitFor(pause.entered, 1000);
				bool ignoredWithoutWait = false;
				if (closingEntered)
				{
					std::atomic<bool> consumerFinished = false;
					std::thread consumer([&]
						{ IgnoreAdditionalLaserTouch(input, ignored, DrawingTool::Laser,
							InputDeviceType::Touch, true, false);
							consumerFinished.store(true, std::memory_order_release); });
					ignoredWithoutWait = waitFor(consumerFinished, 100);
					pause.resume.store(true, std::memory_order_release);
					producer.join();
					consumer.join();
				}
				else
				{
					pause.resume.store(true, std::memory_order_release);
					producer.join();
					}
				check(closingEntered && ignoredWithoutWait &&
					input.DiagnosticsSnapshot().occupiedSlots == 1 &&
					input.DiagnosticsSnapshot().recycled == recycledBeforeClosing + 1,
					"ignored Closing route returns promptly and producer releases slot");
				input.Recycle(ignored);
			}
		}
		else check(false, "Closing probe obtains ignored Touch slot");
		input.PublishUp(tablet, 1, sample(15.0f, ContactPhase::Up));
		input.Recycle(first);
		check(input.DiagnosticsSnapshot().occupiedSlots == 0,
			"first Laser Touch retires after all ignored contacts");
		if (failures == 0)
			std::fputs("[Draw3LaserIgnoredTouch] PASS: ignored Touch route lifetime\n", stderr);
		return failures == 0 ? 0 : 1;
	}

	int RunRejectedStrokeInitializationProductionTest() noexcept
	{
		int failures = 0;
		const auto check = [&](bool condition, const char* label)
		{
			if (condition) return;
			++failures;
			std::fprintf(stderr, "[Draw3InitRejection] FAIL: %s\n", label);
		};
		const auto sample = [](float x, ContactPhase phase)
		{
			ContactSnapshot snapshot;
			snapshot.position = { x, 20.0f };
			snapshot.qpc = static_cast<int64_t>(x * 1000.0f);
			snapshot.phase = phase;
			return snapshot;
		};
		const auto dequeue = [](ContactInputCoordinator& input) -> ContactHandle
		{
			ContactRecord* record = nullptr;
			while (input.TryDequeue(record))
			{
				if (record) return { record, record->Generation() };
				input.AcknowledgeControlWake();
			}
			return {};
		};
		constexpr uint32_t tablet = 0xE003;
		{
			ContactInputCoordinator input;
			input.EnableDiagnostics(true);
			const bool firstDown = input.PublishDown(tablet, 1, InputDeviceType::Pen,
				sample(10.0f, ContactPhase::Down));
			const bool firstUp = input.PublishUp(tablet, 1, sample(15.0f, ContactPhase::Up));
			const bool secondDown = input.PublishDown(tablet, 1, InputDeviceType::Pen,
				sample(30.0f, ContactPhase::Down));
			const ContactHandle first = dequeue(input);
			const ContactHandle second = dequeue(input);
			check(firstDown && firstUp && secondDown && first && second,
				"I01 serialized old Down/Up and new same-key Down are accepted");
			if (first && second)
			{
				check(first.record != second.record,
					"I01 record identity differs even when generation values may match");
				const ContactSnapshot firstDownSnapshot = first.record->DownSnapshot();
				RejectStrokeInitialization(input, first, firstDownSnapshot);
				ContactSnapshot observed;
				check(!input.TryReadSnapshot(first, observed), "I01 old consumer handle is retired");
				check(input.TryReadSnapshot(second, observed) && observed.phase == ContactPhase::Down,
					"I01 old initialization failure must not cancel accepted new same-key Down");
				check(input.PublishMove(tablet, 1, sample(35.0f, ContactPhase::Move)) &&
					input.TryReadSnapshot(second, observed) && observed.phase == ContactPhase::Move &&
					observed.position.x == 35.0f, "I01 new same-key Move remains accepted and readable");
				check(input.PublishUp(tablet, 1, sample(40.0f, ContactPhase::Up)) &&
					input.TryReadSnapshot(second, observed) && observed.phase == ContactPhase::Up,
					"I01 new same-key physical Up retains its own terminal");
				input.Recycle(second);
				check(input.DiagnosticsSnapshot().occupiedSlots == 0,
					"I01 both completed contacts release their slots");
				check(input.PublishDown(tablet, 1, InputDeviceType::Pen,
					sample(50.0f, ContactPhase::Down)), "I01 released slot accepts another Down");
				const ContactHandle reused = dequeue(input);
				if (reused)
				{
					check(reused.record == first.record && reused.generation != first.generation,
						"I01 first slot is reused with a different generation");
					RejectStrokeInitialization(input, first, firstDownSnapshot);
					input.Recycle(first);
					check(!input.TryReadSnapshot(first, observed) &&
						input.TryReadSnapshot(reused, observed) && observed.phase == ContactPhase::Down,
						"I01 stale failure cleanup must not touch reused record generation");
					check(input.PublishUp(tablet, 1, sample(55.0f, ContactPhase::Up)),
						"I01 reused generation still receives physical Up");
					input.Recycle(reused);
				}
				else check(false, "I01 reused Down is dequeued");
				check(input.DiagnosticsSnapshot().occupiedSlots == 0,
					"I01 stale handle neither leaks nor duplicates a slot");
			}
		}
		for (ContactPhase terminal : { ContactPhase::Up, ContactPhase::Cancelled })
		{
			ContactInputCoordinator input;
			input.EnableDiagnostics(true);
			check(input.PublishDown(tablet, 2, InputDeviceType::Touch,
				sample(20.0f, ContactPhase::Down)), "I02 rejected contact Down is accepted");
			const ContactHandle handle = dequeue(input);
			if (!handle) { check(false, "I02 rejected contact is dequeued"); continue; }
			const ContactSnapshot down = handle.record->DownSnapshot();
			RejectStrokeInitialization(input, handle, down);
			ContactSnapshot observed;
			check(input.DiagnosticsSnapshot().occupiedSlots == 1 &&
				input.DiagnosticsSnapshot().recycled == 0 && input.HasQuarantinedContacts(),
				"I02 rejected Producing route stays occupied until physical terminal");
			check(!input.TryReadSnapshot(handle, observed) &&
				!input.PublishMove(tablet, 2, sample(25.0f, ContactPhase::Move)),
				"I02 discarded route supplies no further model input");
			RejectStrokeInitialization(input, handle, down);
			check(input.DiagnosticsSnapshot().occupiedSlots == 1 &&
				input.DiagnosticsSnapshot().recycled == 0,
				"I02 repeated rejection neither retires early nor releases twice");
			const bool closed = terminal == ContactPhase::Up
				? input.PublishUp(tablet, 2, sample(30.0f, terminal))
				: input.PublishCancelled(tablet, 2, sample(30.0f, terminal));
			check(closed && input.DiagnosticsSnapshot().occupiedSlots == 0 &&
				input.DiagnosticsSnapshot().recycled == 1 &&
				input.DiagnosticsSnapshot().terminalPublished == 1 && !input.HasQuarantinedContacts(),
				"I02 physical Up/Cancel performs the sole rejected-route release");
			check(input.PublishDown(tablet, 2, InputDeviceType::Touch,
				sample(40.0f, ContactPhase::Down)), "I02 next legitimate contact remains accepted");
			const ContactHandle next = dequeue(input);
			check(next && input.PublishUp(tablet, 2, sample(45.0f, ContactPhase::Up)),
				"I02 next legitimate contact receives terminal");
			input.Recycle(next);
			check(input.DiagnosticsSnapshot().occupiedSlots == 0, "I02 next contact releases normally");
		}
		for (ContactPhase terminal : { ContactPhase::Up, ContactPhase::Cancelled })
		{
			ContactInputCoordinator input;
			input.EnableDiagnostics(true);
			check(input.PublishDown(tablet, 3, InputDeviceType::Pen,
				sample(60.0f, ContactPhase::Down)), "I03 Closing contact Down is accepted");
			const ContactHandle handle = dequeue(input);
			if (!handle) { check(false, "I03 Closing contact is dequeued"); continue; }
			const ContactSnapshot down = handle.record->DownSnapshot();
			ContactClosePauseForTesting pause;
			input.PauseNextCloseAfterRouteClosedForTesting(&pause);
			std::atomic<bool> producerClosed = false;
			std::thread producer([&]
			{
				const bool closed = terminal == ContactPhase::Up
					? input.PublishUp(tablet, 3, sample(65.0f, terminal))
					: input.PublishCancelled(tablet, 3, sample(65.0f, terminal));
				producerClosed.store(closed, std::memory_order_release);
			});
			const auto waitFor = [](const std::atomic<bool>& flag, DWORD timeoutMilliseconds)
			{
				const ULONGLONG deadline = GetTickCount64() + timeoutMilliseconds;
				while (!flag.load(std::memory_order_acquire) && GetTickCount64() < deadline) Sleep(1);
				return flag.load(std::memory_order_acquire);
			};
			const bool entered = waitFor(pause.entered, 1000);
			bool returnedBeforeResume = false;
			bool deferredRelease = false;
			if (entered)
			{
				std::atomic<bool> consumerFinished = false;
				std::thread consumer([&]
				{
					RejectStrokeInitialization(input, handle, down);
					consumerFinished.store(true, std::memory_order_release);
				});
				returnedBeforeResume = waitFor(consumerFinished, 100);
				ContactSnapshot observed;
				deferredRelease = returnedBeforeResume && !input.TryReadSnapshot(handle, observed) &&
					input.DiagnosticsSnapshot().occupiedSlots == 1;
				// 失败断言也必须先放行真实 Close，随后 join 两个拥有夹具引用的线程。
				pause.resume.store(true, std::memory_order_release);
				producer.join();
				consumer.join();
			}
			else
			{
				pause.resume.store(true, std::memory_order_release);
				producer.join();
			}
			input.PauseNextCloseAfterRouteClosedForTesting(nullptr);
			check(entered && returnedBeforeResume && deferredRelease,
				"I03 failure cleanup returns while physical Close is paused without freeing its slot");
			check(producerClosed.load(std::memory_order_acquire) &&
				input.DiagnosticsSnapshot().terminalPublished == 1 &&
				input.DiagnosticsSnapshot().recycled == 1 &&
				input.DiagnosticsSnapshot().occupiedSlots == 0,
				"I03 released Close publishes one terminal and releases one slot");
			input.Recycle(handle);
			check(input.DiagnosticsSnapshot().recycled == 1,
				"I03 repeated consumer cleanup does not release twice");
		}
		if (failures == 0)
			std::fputs("[Draw3InitRejection] PASS: shared production failure cleanup identity and lifetime\n", stderr);
		return failures == 0 ? 0 : 1;
	}

	int RunRuntimeMetricsSessionProductionProbe() noexcept
	{
		int failures = 0;
		const auto check = [&failures](bool condition, const char* name)
		{
			if (condition) return;
			++failures;
			std::fprintf(stderr, "[Draw3Metrics] FAIL: %s\n", name);
		};
		try
		{
			LARGE_INTEGER now = {}, frequency = {};
			if (!QueryPerformanceCounter(&now) || !QueryPerformanceFrequency(&frequency) ||
				now.QuadPart <= 0 || frequency.QuadPart <= 0 ||
				frequency.QuadPart > (std::numeric_limits<int64_t>::max)() / 4 ||
				now.QuadPart > (std::numeric_limits<int64_t>::max)() - frequency.QuadPart * 4)
			{
				check(false, "QPC clock is available for deterministic Session fixture");
				return 1;
			}
			ContactInputCoordinator input;
			input.EnableDiagnostics(true);
			uint32_t nextContact = 1;
			const auto acquire = [&]() -> ContactHandle
			{
				ContactSnapshot down;
				down.phase = ContactPhase::Down;
				down.qpc = now.QuadPart;
				if (!input.PublishDown(0xE040, nextContact++, InputDeviceType::Pen, down))
					return {};
				ContactRecord* record = nullptr;
				while (input.TryDequeue(record))
				{
					if (record) return { record, record->Generation() };
					input.AcknowledgeControlWake();
				}
				return {};
			};
			const auto finish = [&](ContactHandle handle)
			{
				if (!handle) return;
				ContactSnapshot up;
				up.phase = ContactPhase::Up;
				up.qpc = now.QuadPart + 1;
				check(input.PublishUp(handle.record->TabletContextId(),
					handle.record->ContactId(), up), "fixture publishes actual Coordinator Up");
				input.Recycle(handle);
			};
			RuntimeMetricsLandingProof stored{
				{ 1, 1, 1, 1, 1 }, 41, 7, 1, RuntimeMetricsProofKind::Stored };
			stored.frameSerial = 1;
			{
				RuntimeMetricsSession metrics(2);
				const ContactHandle first = acquire();
				check(static_cast<bool>(first), "real coordinator admits metrics contact");
				if (!first) return 1;
				metrics.BeginFrame();
				check(metrics.RegisterContact(first.record, first.generation, InputDeviceType::Pen,
					static_cast<uint32_t>(DrawingTool::HardPen), now.QuadPart),
					"M01 register admitted HardPen Down once");
				check(metrics.RegisterContact(first.record, first.generation, InputDeviceType::Pen,
					static_cast<uint32_t>(DrawingTool::HardPen), now.QuadPart) &&
					metrics.Snapshot().contactSeen == 1,
					"M01 duplicate registration does not increase denominator");
				check(metrics.StageVerifiedLanding(first.record, first.generation, stored),
					"M01 stage content-linked Stored contact");
				finish(first);
				ContactSnapshot readBack;
				check(!input.TryReadSnapshot(first, readBack) &&
					input.DiagnosticsSnapshot().occupiedSlots == 0,
					"M01 actual Up and recycle retire runtime identity before first Present");
				metrics.RecordVerifiedPresent(2.0, false);
				metrics.CommitVerifiedLandings(false, now.QuadPart + frequency.QuadPart, stored);
				metrics.BeginFrame();
				check(metrics.Snapshot().confirmed == 0 && metrics.Snapshot().pending == 1,
					"M01 failed Present keeps recycled contact pending across BeginFrame");
				RuntimeMetricsLandingProof nextFrame = stored;
				nextFrame.frameSerial = 2;
				RuntimeMetricsLandingProof otherPage = nextFrame;
				otherPage.canvas.page = 2;
				metrics.RecordVerifiedPresent(1.0, true);
				metrics.CommitVerifiedLandings(true, now.QuadPart + frequency.QuadPart * 2, otherPage);
				check(metrics.Snapshot().confirmed == 0 && metrics.Snapshot().pending == 1,
					"M02 other page success cannot confirm old contact");
				metrics.CommitVerifiedLandings(true, now.QuadPart + frequency.QuadPart * 2, stored);
				check(metrics.Snapshot().confirmed == 0 && metrics.Snapshot().pending == 1,
					"M02 old frame proof cannot confirm current success");
				RuntimeMetricsLandingProof otherContent = nextFrame;
				++otherContent.contentToken;
				metrics.CommitVerifiedLandings(true, now.QuadPart + frequency.QuadPart * 2, otherContent);
				check(metrics.Snapshot().confirmed == 0 && metrics.Snapshot().pending == 1,
					"M02 changed content proof cannot confirm old contact");
				metrics.CommitVerifiedLandings(true, now.QuadPart + frequency.QuadPart * 2, nextFrame);
				check(metrics.Snapshot().confirmed == 1 && metrics.Snapshot().pending == 0,
					"M03 current frame and same stored content confirms once after failure");
				metrics.CommitVerifiedLandings(true, now.QuadPart + frequency.QuadPart * 3, nextFrame);
				check(metrics.Snapshot().confirmed == 1, "M03 duplicate success does not duplicate landing");
				const ContactHandle second = acquire();
				check(second && second.record == first.record && second.generation != first.generation,
					"M04 actual recycled slot reappears with a new generation");
				if (!second) return 1;
				check(metrics.RegisterContact(second.record, second.generation, InputDeviceType::Pen,
					static_cast<uint32_t>(DrawingTool::Laser), now.QuadPart),
					"M04 new generation is independently registered");
				check(metrics.StageVerifiedLanding(second.record, second.generation, nextFrame),
					"M04 new generation is independently staged");
				finish(second);
				metrics.InvalidatePending();
				check(metrics.Snapshot().unpresented == 1 && metrics.Snapshot().pending == 0,
					"M04 Clear or page change accounts pending contact as unpresented");
				const ContactHandle third = acquire();
				check(third && !metrics.RegisterContact(third.record, third.generation,
					InputDeviceType::Pen, static_cast<uint32_t>(DrawingTool::Pen), now.QuadPart) &&
					metrics.Snapshot().contactSeen == 3 && metrics.Snapshot().contactRetained == 2 &&
					metrics.Snapshot().contactDropped == 1,
					"M05 fixed capacity retains complete seen retained dropped denominator");
				finish(third);
				check(metrics.Snapshot().presentAttempts == 2 && metrics.Snapshot().presentSucceeded == 1 &&
					metrics.Snapshot().presentFailed == 1,
					"M06 actual Present attempt success and failure counts are distinct");
			}
			// 只有Stored可跨失败帧保留证明；活动层/Laser必须在新成功帧重新锁存。
			for (const auto kind : { RuntimeMetricsProofKind::Live, RuntimeMetricsProofKind::Laser })
			{
				RuntimeMetricsSession metrics(4);
				const ContactHandle handle = acquire();
				check(static_cast<bool>(handle), "M16 actual contact for live or laser restage contract");
				if (!handle) return 1;
				metrics.BeginFrame();
				RuntimeMetricsLandingProof proof = stored;
				proof.kind = kind;
				check(metrics.RegisterContact(handle.record, handle.generation, InputDeviceType::Pen,
					static_cast<uint32_t>(kind == RuntimeMetricsProofKind::Laser
						? DrawingTool::Laser : DrawingTool::HardPen), now.QuadPart)
					&& metrics.StageVerifiedLanding(handle.record, handle.generation, proof),
					"M16 current live or laser content proof is staged");
				metrics.RecordVerifiedPresent(1.0, false);
				metrics.CommitVerifiedLandings(false, now.QuadPart + frequency.QuadPart, proof);
				metrics.BeginFrame();
				proof.frameSerial = 2;
				metrics.RecordVerifiedPresent(1.0, true);
				metrics.CommitVerifiedLandings(true, now.QuadPart + frequency.QuadPart * 2, proof);
				check(metrics.Snapshot().confirmed == 0 && metrics.Snapshot().pending == 1,
					"M16 live or laser old-frame content cannot confirm without restage");
				metrics.BeginFrame();
				proof.frameSerial = 3;
				check(metrics.StageVerifiedLanding(handle.record, handle.generation, proof),
					"M16 real Session restages current-frame live or laser content");
				metrics.RecordVerifiedPresent(1.0, true);
				metrics.CommitVerifiedLandings(true, now.QuadPart + frequency.QuadPart * 3, proof);
				check(metrics.Snapshot().confirmed == 1 && metrics.Snapshot().pending == 0,
					"M16 only matching restaged success confirms live or laser once");
				finish(handle);
			}
			{
				RuntimeMetricsSession metrics(96);
				metrics.BeginFrame();
				const uint64_t allocatedBefore = metrics.Snapshot().allocatedBytes;
				for (uint64_t index = 0; index != 65; ++index)
				{
					const ContactHandle handle = acquire();
					check(static_cast<bool>(handle), "M07 real contact for fixed pending capacity");
					if (!handle) break;
					RuntimeMetricsLandingProof proof = stored;
					proof.itemToken = index + 1;
					check(metrics.RegisterContact(handle.record, handle.generation, InputDeviceType::Pen,
						static_cast<uint32_t>(DrawingTool::Pen), now.QuadPart),
						"M07 pending capacity does not lower product or registered contact capacity");
					check(metrics.StageVerifiedLanding(handle.record, handle.generation, proof) == (index < 64),
						"M07 sixty-fifth pending sample is explicitly refused");
					finish(handle);
				}
				const RuntimeMetricsSnapshot sample = metrics.Snapshot();
				check(sample.contactSeen == 65 && sample.contactRetained == 65 && sample.pending == 64 &&
					sample.pendingOverflow == 1 && sample.unpresented == 1,
					"M07 pending overflow is explicit without losing its contact denominator");
				check(allocatedBefore > 0 && allocatedBefore == sample.allocatedBytes,
					"M07 all hot sampling storage remains preallocated");
				metrics.InvalidatePending();
				check(metrics.Snapshot().unpresented == 65 && metrics.Snapshot().pending == 0,
					"M07 invalidation preserves all unpresented contacts including overflow");
			}
			{
				RuntimeMetricsSession metrics(8);
				metrics.BeginFrame();
				for (int64_t invalidDown : { int64_t{ 0 }, int64_t{ -1 } })
				{
					const ContactHandle handle = acquire();
					check(handle && !metrics.RegisterContact(handle.record, handle.generation,
						InputDeviceType::Pen, static_cast<uint32_t>(DrawingTool::Pen), invalidDown),
						"M08 zero or negative source QPC is rejected");
					finish(handle);
				}
				const ContactHandle handle = acquire();
				check(handle && metrics.RegisterContact(handle.record, handle.generation, InputDeviceType::Pen,
					static_cast<uint32_t>(DrawingTool::Pen), now.QuadPart) &&
					metrics.StageVerifiedLanding(handle.record, handle.generation, stored),
					"M08 valid contact remains eligible after bad timestamps");
				finish(handle);
				metrics.RecordVerifiedPresent(1.0, true);
				metrics.CommitVerifiedLandings(true, now.QuadPart - 1, stored);
				metrics.RecordVerifiedPresent((std::numeric_limits<double>::quiet_NaN)(), false);
				metrics.RecordVerifiedPresent(-1.0, false);
				check(metrics.Snapshot().confirmed == 0 && metrics.Snapshot().invalid >= 5,
					"M08 reverse QPC nonfinite and negative wall are invalid rather than zero latency");
				metrics.InvalidatePending();
			}
			{
				RuntimeMetricsSession metrics(2);
				for (uint64_t index = 1; index != 5; ++index)
				{
					metrics.BeginFrame();
					RuntimeMetricsFrameSample sample;
					sample.frameSerial = index;
					sample.frameStartMs = static_cast<double>(index);
					sample.wallMs = index == 4 ? (std::numeric_limits<double>::quiet_NaN)() : 1.0;
					sample.presentWallMs = 0.5;
					sample.reasonFlags = 1; // 仅合同夹具，真实原因标志由 U2 的生产 owner 锁存。
					sample.physicalBefore = 1;
					sample.physicalAfter = 0;
					sample.terminalCount = 1;
					sample.presentAttempted = true;
					sample.presentSucceeded = true;
					metrics.RecordRenderFrame(sample);
				}
				const RuntimeMetricsSnapshot sample = metrics.Snapshot();
				check(sample.framesSeen == 4 && sample.framesRetained == 2 &&
					sample.framesDropped == 1 && sample.framesInvalid == 1,
					"M09 terminal render frames retain seen kept dropped invalid independently of held contact");
			}
			{
				RuntimeMetricsSession metrics((std::numeric_limits<size_t>::max)());
				const RuntimeMetricsSnapshot sample = metrics.Snapshot();
				check(sample.requestedSamples == (std::numeric_limits<size_t>::max)() &&
					sample.effectiveSamples > 0 && sample.effectiveSamples < sample.requestedSamples &&
					sample.allocatedBytes > 0 && sample.allocatedBytes <= uint64_t{ 32 } * 1024 * 1024,
					"M10 huge requested capacity is bounded by actual preallocation byte budget");
			}

			// 仅本次 CLI 的随机临时目录；不触及真实配置/UInk，也不递归删除未知内容。
			struct ReportFiles
			{
				wchar_t directory[MAX_PATH] = {};
				std::wstring report;
				std::wstring existing;
				bool ownsDirectory = false;
				~ReportFiles()
				{
					if (!ownsDirectory) return;
					if (!report.empty()) DeleteFileW(report.c_str());
					if (!existing.empty()) DeleteFileW(existing.c_str());
					RemoveDirectoryW(directory);
				}
			} files;
			wchar_t temporary[MAX_PATH] = {};
			const DWORD temporaryLength = GetTempPathW(MAX_PATH, temporary);
			if (temporaryLength == 0 || temporaryLength >= MAX_PATH ||
				!GetTempFileNameW(temporary, L"IDT", 0, files.directory) ||
				!DeleteFileW(files.directory) || !CreateDirectoryW(files.directory, nullptr))
			{
				check(false, "M11 isolated report fixture directory is newly created");
				return 1;
			}
			files.ownsDirectory = true;
			files.report = std::wstring(files.directory) + L"\\report.json";
			files.existing = std::wstring(files.directory) + L"\\existing.json";
			struct FileLease
			{
				HANDLE handle = INVALID_HANDLE_VALUE;
				~FileLease() { if (handle != INVALID_HANDLE_VALUE) CloseHandle(handle); }
			};
			const auto readText = [](const wchar_t* path, std::string& output)
			{
				FileLease file{ CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, nullptr,
					OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr) };
				LARGE_INTEGER size = {};
				if (file.handle == INVALID_HANDLE_VALUE || !GetFileSizeEx(file.handle, &size) ||
					size.QuadPart < 0 || size.QuadPart > 4 * 1024 * 1024) return false;
				output.resize(static_cast<size_t>(size.QuadPart));
				DWORD read = 0;
				return ReadFile(file.handle, output.data(), static_cast<DWORD>(output.size()),
					&read, nullptr) && read == output.size();
			};
			const auto readJson = [&](Json::Value& output)
			{
				std::string text;
				if (!readText(files.report.c_str(), text)) return false;
				Json::CharReaderBuilder builder;
				builder["collectComments"] = false;
				builder["rejectDupKeys"] = true;
				builder["failIfExtra"] = true;
				builder["stackLimit"] = 64;
				builder["allowSpecialFloats"] = false;
				std::string errors;
				const auto reader = std::unique_ptr<Json::CharReader>(builder.newCharReader());
				return reader && reader->parse(text.data(), text.data() + text.size(), &output, &errors);
			};
			{
				constexpr char sentinel[] = "Draw3Metrics-existing-report";
				{
					FileLease file{ CreateFileW(files.existing.c_str(), GENERIC_WRITE, 0, nullptr,
						CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr) };
					DWORD written = 0;
					check(file.handle != INVALID_HANDLE_VALUE &&
						WriteFile(file.handle, sentinel, sizeof(sentinel) - 1, &written, nullptr) &&
						written == sizeof(sentinel) - 1, "M11 existing report sentinel is owned by fixture");
				}
				RuntimeMetricsSession metrics(8);
				check(!metrics.WriteJson(files.existing.c_str(), input.DiagnosticsSnapshot()),
					"M11 WriteJson refuses to overwrite a preexisting report");
				std::string unchanged;
				check(readText(files.existing.c_str(), unchanged) && unchanged == sentinel,
					"M11 refusal preserves preexisting bytes");
			}
			{
				RuntimeMetricsSession legacy(8);
				legacy.BeginFrame();
				const ContactHandle handle = acquire();
				check(static_cast<bool>(handle), "M12 real contact for legacy no-proof API");
				if (!handle) return 1;
				legacy.StageLanding(handle.record, handle.generation, InputDeviceType::Pen,
					static_cast<uint32_t>(DrawingTool::HardPen), now.QuadPart);
				legacy.RecordPresent(1.0);
				legacy.RecordActiveFrame(1.0, 1.0, 1.0, true);
				legacy.RecordActiveFrame((std::numeric_limits<double>::quiet_NaN)(), 1.0, 1.0, true);
				legacy.CommitStagedLandings(true, now.QuadPart + frequency.QuadPart);
				finish(handle);
				check(legacy.Snapshot().confirmed == 0 && legacy.Snapshot().legacyUnverified > 0,
					"M12 old no-proof API produces only unverified observations");
				check(legacy.WriteJson(files.report.c_str(), input.DiagnosticsSnapshot()),
					"M12 new report is written without touching a preexisting file");
				Json::Value report;
				check(readJson(report) && report["schemaVersion"].asUInt() == 2 &&
					report["landings"].isArray() && report["landings"].empty() &&
					report["coverage"]["legacyUnverified"].asUInt64() > 0 &&
					report["summary"]["legacyThresholdMet"].isBool() &&
					!report["summary"].isMember("strictPass"),
					"M12 formal JSON excludes legacy samples and does not claim release strictPass");
				DeleteFileW(files.report.c_str());
			}
			{
				const std::array<std::pair<DrawingTool, const char*>, 9> tools = {{
					{ DrawingTool::Pen, "Pen" }, { DrawingTool::HardPen, "HardPen" },
					{ DrawingTool::Highlighter, "Highlighter" }, { DrawingTool::Eraser, "Eraser" },
					{ DrawingTool::Laser, "Laser" }, { DrawingTool::SolidLine, "SolidLine" },
					{ DrawingTool::DashedLine, "DashedLine" },
					{ DrawingTool::OutlineRectangle, "OutlineRectangle" },
					{ DrawingTool::FilledRectangle, "FilledRectangle" }
				}};
				RuntimeMetricsSession metrics(64);
				uint64_t frameSerial = 0;
				for (const auto& tool : tools)
				{
					for (int64_t seconds = 1; seconds != 4; ++seconds)
					{
						metrics.BeginFrame();
						const ContactHandle handle = acquire();
						if (!handle) { check(false, "M13 real tool contact is admitted"); continue; }
						RuntimeMetricsLandingProof proof = stored;
						proof.frameSerial = ++frameSerial;
						proof.itemToken = frameSerial;
						if (tool.first == DrawingTool::Laser) proof.kind = RuntimeMetricsProofKind::Laser;
						check(metrics.RegisterContact(handle.record, handle.generation, InputDeviceType::Pen,
							static_cast<uint32_t>(tool.first), now.QuadPart) &&
							metrics.StageVerifiedLanding(handle.record, handle.generation, proof),
							"M13 actual DrawingTool symbol is recorded with content proof");
						finish(handle);
						metrics.RecordVerifiedPresent(1.0, true);
						metrics.CommitVerifiedLandings(true, now.QuadPart + frequency.QuadPart * seconds, proof);
					}
				}
				metrics.BeginFrame();
				const ContactHandle unknown = acquire();
				if (unknown)
				{
					RuntimeMetricsLandingProof proof = stored;
					proof.frameSerial = ++frameSerial;
					proof.itemToken = frameSerial;
					check(metrics.RegisterContact(unknown.record, unknown.generation, InputDeviceType::Pen,
						(std::numeric_limits<uint32_t>::max)(), now.QuadPart) &&
						metrics.StageVerifiedLanding(unknown.record, unknown.generation, proof),
						"M13 unknown tool stays an explicitly separate population");
					finish(unknown);
					metrics.RecordVerifiedPresent(1.0, true);
					metrics.CommitVerifiedLandings(true, now.QuadPart + frequency.QuadPart, proof);
				}
				else check(false, "M13 actual unknown-tool fixture contact is admitted");
				check(metrics.Snapshot().confirmed == 28,
					"M13 all nine current product tool populations are retained");
				check(metrics.WriteJson(files.report.c_str(), input.DiagnosticsSnapshot()),
					"M13 formal tool report is written even when behavioral assertions fail");
				Json::Value report;
				check(readJson(report) && report["schemaVersion"].asUInt() == 2,
					"M13 report is valid JSON with no NaN or nonnumeric extension");
				for (const auto& tool : tools)
				{
					const Json::Value* population = nullptr;
					for (const Json::Value& candidate : report["toolSummaries"])
						if (candidate["tool"].asString() == tool.second &&
							candidate["device"].asString() == "Pen") population = &candidate;
					check(population && (*population)["count"].asUInt64() == 3 &&
						(*population)["medianMs"].isNumeric() &&
						std::abs((*population)["medianMs"].asDouble() - 2000.0) < 0.0001 &&
						std::abs((*population)["p95Ms"].asDouble() - 3000.0) < 0.0001 &&
						(*population)["p99Ms"].isNull() &&
						(*population)["insufficientPopulation"].asBool(),
						"M13 tool group has exact known median P95 and null small-sample P99");
				}
				bool unknownPopulation = false;
				for (const Json::Value& candidate : report["toolSummaries"])
					if (candidate["tool"].asString() == "Unknown" && candidate["count"].asUInt64() == 1)
						unknownPopulation = true;
				check(unknownPopulation, "M13 unknown tool is not mislabeled as a known tool");
				check(report["landings"].isArray() && report["landings"].size() == 28,
					"M13 raw landing count matches formal population");
				for (const Json::Value& landing : report["landings"])
					check(!landing.isMember("record") && !landing.isMember("workspaceGuid") &&
						!landing.isMember("pageGuid") && !landing.isMember("documentPath"),
						"M13 raw report does not export pointers GUIDs or document paths");
			}
			{
				check(DeleteFileW(files.report.c_str()), "M14 prior owned report is removed before next output");
				RuntimeMetricsSession metrics(1000);
				bool populationRegistered = true;
				for (uint64_t index = 1; index <= 1000; ++index)
				{
					metrics.BeginFrame();
					const ContactHandle handle = acquire();
					if (!handle) { check(false, "M14 actual percentile fixture contact is admitted"); break; }
					RuntimeMetricsLandingProof proof = stored;
					proof.frameSerial = index;
					proof.itemToken = index;
					const bool registered = metrics.RegisterContact(handle.record, handle.generation,
						InputDeviceType::Pen, static_cast<uint32_t>(DrawingTool::Pen), now.QuadPart);
					const bool staged = metrics.StageVerifiedLanding(handle.record, handle.generation, proof);
					populationRegistered = populationRegistered && registered && staged;
					finish(handle);
					metrics.RecordVerifiedPresent(1.0, true);
					metrics.CommitVerifiedLandings(true, now.QuadPart + frequency.QuadPart, proof);
				}
				check(populationRegistered && metrics.Snapshot().confirmed == 1000,
					"M14 thousand contacts use actual Session identity and proof");
				check(metrics.WriteJson(files.report.c_str(), input.DiagnosticsSnapshot()),
					"M14 thousand-sample report is newly created");
				Json::Value report;
				check(readJson(report) && report["toolSummaries"].isArray() &&
					report["toolSummaries"].size() == 1 &&
					report["toolSummaries"][0]["count"].asUInt64() == 1000 &&
					report["toolSummaries"][0]["p99Ms"].isNumeric() &&
					std::abs(report["toolSummaries"][0]["p99Ms"].asDouble() - 1000.0) < 0.0001 &&
					!report["toolSummaries"][0]["insufficientPopulation"].asBool(),
					"M14 P99 becomes numeric only at the declared per-group population boundary");
				check(DeleteFileW(files.report.c_str()), "M15 prior owned report is removed before empty output");
				RuntimeMetricsSession empty(8);
				check(empty.WriteJson(files.report.c_str(), input.DiagnosticsSnapshot()),
					"M15 empty report is newly created");
				check(readJson(report) && report["landings"].isArray() &&
					report["landings"].empty() && report["toolSummaries"].isArray() &&
					report["toolSummaries"].empty() &&
					report["summary"]["landingMedianMs"].isNull() &&
					report["summary"]["landingP95Ms"].isNull() &&
					report["summary"]["landingP99Ms"].isNull(),
					"M15 empty population percentiles are null rather than a misleading zero");
			}
		}
		catch (...)
		{
			check(false, "metrics fixture catches allocation or report failure");
		}
		if (failures == 0)
			std::fputs("[Draw3Metrics] PASS: production Session pending, bounded outcomes and report contract\n", stderr);
		return failures == 0 ? 0 : 1;
	}

	int RunDraw3ContentProofProductionProbe() noexcept
	{
		int failures = 0;
		const auto check = [&failures](bool condition, const char* name)
		{
			if (condition) return;
			++failures;
			std::fprintf(stderr, "[Draw3ContentProof] FAIL: %s\n", name);
		};
		try
		{
			LARGE_INTEGER now = {}, frequency = {};
			if (!QueryPerformanceCounter(&now) || !QueryPerformanceFrequency(&frequency) ||
				now.QuadPart <= 0 || frequency.QuadPart <= 0 ||
				frequency.QuadPart > (std::numeric_limits<int64_t>::max)() / 4 ||
				now.QuadPart > (std::numeric_limits<int64_t>::max)() - frequency.QuadPart * 4)
			{
				check(false, "fixture has a valid QPC range");
				return 1;
			}
			struct SourceContact
			{
				ContentMetricKey key;
				ContactHandle handle;
				ContactSnapshot down;
				uint32_t contactId = 0;
				bool ready = false;
			};
			struct StoredContact
			{
				SourceContact source;
				StoredStrokeCpuCommit committed;
				ContactSnapshot terminal;
				bool captured = false;
			};
			struct Fixture
			{
				static InkGuid Guid(uint8_t marker)
				{
					std::array<uint8_t, 16> bytes = {};
					bytes.back() = marker;
					return InkGuid(bytes);
				}
				explicit Fixture(int64_t sourceQpc) : now(sourceQpc), producer(ControllerContentMetrics::Prepare(&metrics))
				{
					input.EnableDiagnostics(true);
					if (!producer || !document.AppendPage(Guid(2)) || !document.AppendPage(Guid(3)) ||
						!document.PageAt(0)->GetOrCreateCanvas(kDefaultDeviceKey) ||
						!document.PageAt(1)->GetOrCreateCanvas(kDefaultDeviceKey) ||
						!producer->ObserveCanvas(document, 0, 1, false)) throw 1;
					metrics.BeginFrame();
					producer->ObserveOutput(true, TransparentOutputTarget::PrimaryDrawpad, 0);
				}
				SourceContact Down(InputDeviceType device = InputDeviceType::Pen, DrawingTool tool = DrawingTool::Pen)
				{
					SourceContact value;
					value.contactId = nextContact++;
					ContactSnapshot down;
					down.phase = ContactPhase::Down;
					down.position = { 40.0f, 48.0f };
					down.qpc = now + value.contactId * 4;
					if (!input.PublishDown(0xE042, value.contactId, device, down)) return value;
					ContactRecord* record = nullptr;
					while (input.TryDequeue(record))
					{
						if (record) break;
						input.AcknowledgeControlWake();
					}
					if (!record) return value;
					value.handle = { record, record->Generation() };
					value.key = { record, value.handle.generation };
					value.down = record->DownSnapshot(); // 只在本次实际 handle 仍活时复制。
					value.ready = producer->Register(value.key, value.down, device, tool);
					producer->NoteConsumed(value.key, value.down.sequence);
					producer->Adopt(value.key, value.down, ContentMetricAdoptionKind::RawDown, true);
					producer->ObserveLiveRaster(value.key, value.down.sequence,
						{ 36, 44, 44, 52 }, metrics.Snapshot().frameSerial, true);
					return value;
				}
				bool Up(SourceContact value, ContactSnapshot& terminal)
				{
					if (!value.handle) return false;
					ContactSnapshot up = value.down;
					up.phase = ContactPhase::Up;
					up.position = { 232.0f, 144.0f };
					up.qpc = value.down.qpc + 1;
					return input.PublishUp(0xE042, value.contactId, up) &&
						input.TryReadSnapshot(value.handle, terminal);
				}
				void Retire(SourceContact value)
				{
					ContactSnapshot terminal;
					if (!Up(value, terminal)) throw 1;
					input.Recycle(value.handle);
				}
				StoredContact Store()
				{
					StoredContact value;
					value.source = Down();
					if (!value.source.handle || !Up(value.source, value.terminal)) throw 1;
					RuntimeStroke runtime(1000.0f);
					runtime.handle = value.source.handle;
					runtime.inUse = true;
					runtime.ended = true;
					runtime.ownerWorkspaceGuid = document.WorkspaceGuid();
					runtime.ownerPageGuid = document.PageAt(0)->PageGuid();
					runtime.lastInputSnapshot = value.terminal;
					runtime.lastConsumedSequence = value.terminal.sequence;
					runtime.stroke.hasInputStartPoint = true;
					runtime.stroke.inputStartPoint = { 40.0f, 48.0f, 2.0f, 0.0f };
					runtime.stroke.realPoints = { runtime.stroke.inputStartPoint,
						{ value.terminal.position.x, value.terminal.position.y, 2.0f, 0.008f } };
					const auto committed = CommitRuntimeStoredStrokeCpu(runtime, document,
						runtimes, 0, 0.0, StoredStrokeCommitMode::NormalUp, [&] { return nextRasterToken++; });
					if (!committed) throw 1;
					value.committed = *committed;
					producer->NoteConsumed(value.source.key, value.terminal.sequence);
					producer->Adopt(value.source.key, value.terminal, ContentMetricAdoptionKind::RawTerminal, true);
					value.captured = producer->CaptureStored(value.source.key, *committed, runtimes[0], value.terminal);
					// 与生产相同：CPU afterState 可推进；这一步不授予任何 GPU 成功资格。
					runtimes[0].rasterState = committed->afterState;
					input.Recycle(value.source.handle);
					return value;
				}
				ContentMetricRasterSignature Signature() const
				{ return producer->Signature(document, 0, runtimes[0], 320, 240); }
				bool Authorize()
				{ return producer->CompleteFullReplay(Signature(), true); }
				size_t Freeze(bool compositeSucceeded = true)
				{
					return producer->FreezeCandidates(Signature(),
						*document.PageAt(0)->FindCanvas(kDefaultDeviceKey), runtimes[0],
						{ 0, 0, 320, 240 }, compositeSucceeded, true);
				}
				RuntimeMetricsLandingProof Prime(ContentMetricKey key, bool stored)
				{
					// 只为隔离 Session 精确失效接口建输入；不复制 assembler 的采纳/栅格判定。
					RuntimeMetricsLandingProof proof;
					proof.canvas = producer->canvasIdentity;
					proof.canvas.outputGeneration = 1;
					proof.frameSerial = metrics.Snapshot().frameSerial;
					if (stored)
					{
						const auto* note = producer->FindStored(key);
						if (!note) throw 1;
						proof.kind = RuntimeMetricsProofKind::Stored;
						proof.contentToken = note->contentToken;
						proof.itemToken = (uint64_t{ note->item.generation } << 32) | note->item.index;
						proof.consumedSequence = note->terminalSequence;
					}
					else
					{
						const auto* note = producer->FindLive(key);
						if (!note) throw 1;
						proof.contentToken = note->contentToken;
						proof.consumedSequence = note->adoptedSequence;
					}
					if (!metrics.StageVerifiedLanding(key.opaqueRecord, key.generation, proof)) throw 1;
					return proof;
				}
				int64_t now = 0;
				RuntimeMetricsSession metrics{ 256 };
				ContactInputCoordinator input;
				InkCanvasCollection document{ Guid(1) };
				std::vector<CanvasPageRuntimeState> runtimes = std::vector<CanvasPageRuntimeState>(2);
				std::unique_ptr<ControllerContentMetrics> producer;
				uint32_t nextContact = 1;
				InkRasterStateToken nextRasterToken = 1;
			};
			const int64_t returnQpc = now.QuadPart + frequency.QuadPart;
			{
				Fixture fixture(now.QuadPart);
				const auto a = fixture.Store();
				ContactSnapshot stale;
				check(a.captured && !fixture.input.TryReadSnapshot(a.source.handle, stale) &&
					fixture.input.DiagnosticsSnapshot().occupiedSlots == 0,
					"U201 real CPU Stored is copied before actual Up recycle");
				const bool authoritative = fixture.Authorize();
				const size_t frozen = fixture.Freeze();
				check(authoritative && frozen == 1 && fixture.metrics.Snapshot().pending == 1,
					"U201 authoritative same-item Stored stages a pure-value pending");
				fixture.producer->PresentReturned(true, false, returnQpc, 0.5, fixture.producer->output);
				fixture.metrics.BeginFrame();
				check(fixture.metrics.Snapshot().confirmed == 0 && fixture.metrics.Snapshot().pending == 1,
					"U201 failed Present retains recycled Stored across a new frame");
				fixture.Authorize();
				fixture.Freeze();
				fixture.producer->PresentReturned(true, true, returnQpc + 1, 0.5, fixture.producer->output);
				check(fixture.metrics.Snapshot().confirmed == 1 && fixture.metrics.Snapshot().pending == 0 &&
					!fixture.producer->FindStored(a.source.key),
					"U201 next real-content success confirms once and releases Stored note");
				fixture.producer->PresentReturned(true, true, returnQpc + 2, 0.5, fixture.producer->output);
				check(fixture.metrics.Snapshot().confirmed == 1, "U201 repeated success cannot duplicate landing");
			}
			for (unsigned mismatch = 0; mismatch != 6; ++mismatch)
			{
				Fixture fixture(now.QuadPart);
				const auto a = fixture.Store();
				fixture.Prime(a.source.key, true);
				const auto* item = fixture.runtimes[0].history.Find(a.committed.renderItem);
				check(a.captured && item && item->visible && item->id == a.committed.renderItem,
					"U202 malformed-proof fixture starts with exact actual history item");
				if (mismatch == 0)
				{
					const auto wrongPage = fixture.producer->Signature(fixture.document, 1, fixture.runtimes[1], 320, 240);
					fixture.producer->CompleteFullReplay(wrongPage, true);
					check(fixture.producer->FreezeCandidates(wrongPage,
						*fixture.document.PageAt(1)->FindCanvas(kDefaultDeviceKey), fixture.runtimes[1],
						{ 0, 0, 320, 240 }, true, true) == 0, "U202 actual other page cannot stage old Stored");
				}
				else if (mismatch == 1)
				{
					check(fixture.runtimes[0].history.UndoLastVisible(a.committed.renderItem), "U202 actual Undo hides item");
					fixture.Authorize(); fixture.Freeze();
					check(fixture.metrics.Snapshot().pending == 0 && fixture.metrics.Snapshot().unpresented == 1,
						"U202 hidden Stored is precisely ended before Redo");
					check(fixture.runtimes[0].history.RedoLastUndone(a.committed.renderItem), "U202 actual Redo restores new content generation");
				}
				else
				{
					auto* note = fixture.producer->FindStored(a.source.key);
					if (!note) throw 1;
					if (mismatch == 2) ++note->item.generation;
					if (mismatch == 3) ++note->strokeIndex;
					if (mismatch == 4)
					{
						const auto* canvas = fixture.document.PageAt(0)->FindCanvas(kDefaultDeviceKey);
						auto footprint = BuildStrokeTileFootprint(canvas->Strokes()[a.committed.strokeIndex]);
						if (!footprint || !fixture.runtimes[0].history.UpdateItemGeometry(a.committed.renderItem, std::move(*footprint))) throw 1;
					}
					if (mismatch == 5) ++note->afterState;
				}
				fixture.Authorize();
				check(fixture.Freeze() == (mismatch == 0 ? 1u : 0u),
					"U202 only unchanged exact id/content/state can be staged on owner page");
				fixture.producer->PresentReturned(true, true, returnQpc, 0.5, fixture.producer->output);
				check(fixture.metrics.Snapshot().confirmed == (mismatch == 0 ? 1u : 0u),
					"U202 mismatched or Undo-Redo Stored cannot resurrect old landing");
			}
			{
				Fixture fixture(now.QuadPart);
				const auto a = fixture.Store();
				check(fixture.Freeze() == 0, "U203 CPU afterState alone does not authorize L2");
				const auto beforeB = fixture.Signature();
				fixture.producer->InvalidateRaster();
				check(!fixture.producer->BeginLocalWrite(beforeB), "U203 invalid stamp cannot start authoritative local chain");
				const auto b = fixture.Store();
				check(a.captured && b.captured && !fixture.producer->CompleteLocalWrite(beforeB, fixture.Signature(), true) &&
					fixture.Freeze() == 0, "U203 another successful append does not repair old raster failure");
				fixture.producer->BeginVisibleReplay(beforeB);
				check(!fixture.producer->CompleteVisibleReplay(fixture.Signature(), true),
					"U203 changed history signature cannot certify older replay plan");
				fixture.producer->BeginVisibleReplay(fixture.Signature());
				check(!fixture.producer->CompleteVisibleReplay(fixture.Signature(), false) && fixture.Freeze() == 0,
					"U203 incomplete visible replay remains withheld");
				const bool replay = fixture.Authorize();
				check(replay && fixture.Freeze() == 2, "U203 complete current history replay certifies both unchanged Stored items");
				fixture.producer->PresentReturned(true, true, returnQpc, 0.5, fixture.producer->output);
				check(fixture.metrics.Snapshot().confirmed == 2 && fixture.metrics.Snapshot().presentAttempts == 1,
					"U203 one success confirms two Stored notes after intervening append");
			}
			{
				Fixture fixture(now.QuadPart);
				const auto before = fixture.Signature();
				check(fixture.Authorize() && fixture.producer->BeginLocalWrite(before),
					"U203 exact valid L2 stamp opens a continuous local write");
				fixture.Store();
				check(fixture.producer->CompleteLocalWrite(before, fixture.Signature(), true) && fixture.Freeze() == 1,
					"U203 successful local GPU write advances only its exact before/after chain");
				fixture.producer->PresentReturned(true, true, returnQpc, 0.5, fixture.producer->output);
				const auto beforeFailure = fixture.Signature();
				check(fixture.producer->BeginLocalWrite(beforeFailure), "U203 next local write uses the current stamp");
				fixture.Store();
				check(!fixture.producer->CompleteLocalWrite(beforeFailure, fixture.Signature(), false) &&
					!fixture.producer->CompleteLocalWrite(beforeFailure, fixture.Signature(), true) && fixture.Freeze() == 0,
					"U203 failed write cannot be repaired by a completion without a new valid before stamp");
				fixture.producer->BeginVisibleReplay(fixture.Signature());
				check(!fixture.producer->CompleteVisibleReplay(fixture.Signature(), false) &&
					fixture.producer->CompleteVisibleReplay(fixture.Signature(), true) && fixture.Freeze() == 1,
					"U203 incomplete replay retains its start signature until same-plan full completion");
			}
			{
				Fixture fixture(now.QuadPart);
				fixture.Store();
				const auto live = fixture.Down();
				fixture.Authorize();
				check(live.ready && fixture.Freeze() == 2, "U204 current Stored and Live share one frozen frame");
				fixture.producer->PresentReturned(true, true, returnQpc, 0.5, fixture.producer->output);
				check(fixture.metrics.Snapshot().confirmed == 2 && fixture.metrics.Snapshot().presentAttempts == 1,
					"U204 same success QPC confirms distinct contact proofs once");
				fixture.Retire(live);
			}
			{
				Fixture fixture(now.QuadPart);
				const auto a = fixture.Store(), c = fixture.Store();
				fixture.Prime(a.source.key, true);
				const auto cProof = fixture.Prime(c.source.key, true);
				fixture.metrics.RecordVerifiedPresent(0.5, true);
				check(fixture.producer->Invalidate(a.source.key, ContentMetricInvalidationReason::Cancelled),
					"U204 Cancel precisely ends A Registered/Pending key");
				fixture.metrics.CommitVerifiedLandings(true, returnQpc, cProof);
				check(fixture.metrics.Snapshot().confirmed == 1 && fixture.metrics.Snapshot().pending == 0 &&
					fixture.metrics.Snapshot().unpresented == 1,
					"U204 Cancel A preserves C Stored and current-frame verified success");
				check(!fixture.producer->Invalidate(a.source.key, ContentMetricInvalidationReason::Cancelled) &&
					fixture.metrics.Snapshot().unpresented == 1, "U204 repeated Cancel does not inflate denominator");
			}
			{
				Fixture fixture(now.QuadPart);
				const auto c = fixture.Store();
				fixture.Prime(c.source.key, true);
				bool registered = true, staged = true, invalidated = true;
				for (unsigned index = 0; index != 70; ++index)
				{
					const auto a = fixture.Down(InputDeviceType::Touch);
					registered = a.ready && registered;
					if (a.ready)
					{
						const auto* note = fixture.producer->FindLive(a.key);
						RuntimeMetricsLandingProof proof{ fixture.producer->canvasIdentity,
							note->contentToken, 0, a.down.sequence, RuntimeMetricsProofKind::Live };
						proof.canvas.outputGeneration = 1;
						proof.frameSerial = fixture.metrics.Snapshot().frameSerial;
						staged = fixture.metrics.StageVerifiedLanding(a.key.opaqueRecord, a.key.generation, proof) && staged;
					}
					fixture.Retire(a);
					invalidated = fixture.producer->Invalidate(a.key, ContentMetricInvalidationReason::ReconnectSuperseded) && invalidated;
				}
				check(registered && staged && invalidated && fixture.metrics.Snapshot().pending == 1 &&
					fixture.metrics.Snapshot().pendingOverflow == 0 && fixture.metrics.Snapshot().unpresented == 70 &&
					fixture.input.DiagnosticsSnapshot().occupiedSlots == 0,
					"U205 more than 64 reconnect handoffs release only A while C Stored remains pending");
			}
			{
				Fixture fixture(now.QuadPart);
				const auto a = fixture.Down();
				const auto proof = fixture.Prime(a.key, false);
				fixture.metrics.RecordVerifiedPresent(0.5, true);
				fixture.metrics.CommitVerifiedLandings(true, returnQpc, proof);
				fixture.Retire(a);
				check(!fixture.producer->Invalidate(a.key, ContentMetricInvalidationReason::ReconnectSuperseded) &&
					fixture.metrics.Snapshot().unpresented == 0, "U205 already confirmed A is an idempotent reconnect no-op");
				const auto b = fixture.Down();
				fixture.Prime(b.key, false);
				check(a.key.opaqueRecord == b.key.opaqueRecord && a.key.generation != b.key.generation &&
					!fixture.producer->Invalidate(a.key, ContentMetricInvalidationReason::Cancelled) &&
					fixture.metrics.Snapshot().pending == 1, "U205 stale same-slot generation cannot cancel new B");
				fixture.Retire(b);
			}
			{
				Fixture fixture(now.QuadPart);
				const auto a = fixture.Down();
				check(fixture.producer->Invalidate(a.key, ContentMetricInvalidationReason::InitRejected) &&
					fixture.metrics.Snapshot().unpresented == 1 && fixture.metrics.Snapshot().pending == 0,
					"U205 Registered without any proof is precisely ended at initialization rejection");
				check(!fixture.producer->Invalidate(a.key, ContentMetricInvalidationReason::InitRejected) &&
					!fixture.metrics.InvalidateContact(nullptr, 1) &&
					!fixture.metrics.InvalidateContact(a.key.opaqueRecord, a.key.generation + 1) &&
					fixture.metrics.Snapshot().unpresented == 1, "U205 repeated/unknown/stale exact invalidation is a no-op");
				fixture.Retire(a);
			}
			{
				Fixture fixture(now.QuadPart);
				const auto a = fixture.Store();
				fixture.Prime(a.source.key, true);
				const auto scene = fixture.producer->canvasIdentity.sceneGeneration;
				check(fixture.producer->ObserveCanvas(fixture.document, 0, 1, false) &&
					fixture.producer->canvasIdentity.sceneGeneration == scene && fixture.metrics.Snapshot().pending == 1,
					"U202 unchanged CPU scene does not retire Stored pending");
				fixture.document.PageAt(0)->FindCanvas(kDefaultDeviceKey)->ClearStrokes();
				fixture.runtimes[0] = {};
				check(fixture.producer->ObserveCanvas(fixture.document, 0, 1, true) &&
					fixture.producer->canvasIdentity.sceneGeneration != scene && fixture.metrics.Snapshot().pending == 0 &&
					fixture.metrics.Snapshot().unpresented == 1,
					"U202 actual CPU Clear changes scene and precisely ends old Stored");
				fixture.producer->ObserveCanvas(fixture.document, 1, 1, false);
				fixture.producer->ObserveCanvas(fixture.document, 0, 1, false);
				fixture.Authorize();
				fixture.Freeze();
				fixture.producer->PresentReturned(true, true, returnQpc, 0.5, fixture.producer->output);
				check(fixture.metrics.Snapshot().confirmed == 0 && fixture.metrics.Snapshot().unpresented == 1 &&
					fixture.producer->canvasIdentity.sceneGeneration != scene,
					"U202 returning same page after Clear never resurrects old scene note");
			}
			{
				Fixture fixture(now.QuadPart);
				const auto first = fixture.producer->ObserveOutput(true, TransparentOutputTarget::PrimaryDrawpad, 0);
				const auto repeat = fixture.producer->ObserveOutput(true, TransparentOutputTarget::PrimaryDrawpad, 0);
				const auto selection = fixture.producer->ObserveOutput(true, TransparentOutputTarget::SelectionUlw, 1);
				const auto primary = fixture.producer->ObserveOutput(true, TransparentOutputTarget::PrimaryDrawpad, 2);
				check(first.exists && first.rawRevision == 0 && first.generation != 0 && repeat == first &&
					selection.exists && primary.exists && selection.generation != first.generation &&
					primary.generation != selection.generation,
					"U206 valid raw0 repeat and raw1/raw2 receive distinct checked anonymous identities");
				fixture.producer->InvalidateOutput();
				const auto recovery = fixture.producer->ObserveOutput(true, TransparentOutputTarget::PrimaryDrawpad, 2);
				check(recovery.exists && recovery.generation != primary.generation,
					"U206 recovered identical raw tuple receives a new identity");
				fixture.producer->nextOutputGeneration = (std::numeric_limits<uint64_t>::max)();
				const auto extreme = fixture.producer->ObserveOutput(true, TransparentOutputTarget::SelectionUlw,
					(std::numeric_limits<uint64_t>::max)());
				const auto exhausted = fixture.producer->ObserveOutput(true, TransparentOutputTarget::PrimaryDrawpad, 0);
				check(extreme.exists && extreme.rawRevision == (std::numeric_limits<uint64_t>::max)() &&
					extreme.generation == (std::numeric_limits<uint64_t>::max)() && !exhausted.exists &&
					fixture.producer->counters.identityExhausted != 0,
					"U206 raw revision is never incremented and exhausted generation fails closed");
			}
			{
				Fixture fixture(now.QuadPart);
				fixture.Store(); fixture.Authorize(); fixture.Freeze();
				fixture.producer->PresentReturned(true, false, returnQpc, 0.5, fixture.producer->output);
				fixture.metrics.BeginFrame();
				const auto nextOutput = fixture.producer->ObserveOutput(true, TransparentOutputTarget::SelectionUlw, 1);
				fixture.Authorize();
				check(fixture.Freeze() == 1, "U206 failed Stored can restage against a genuinely new output identity");
				fixture.producer->PresentReturned(true, true, returnQpc + 1, 0.5, nextOutput);
				check(fixture.metrics.Snapshot().confirmed == 1 && fixture.metrics.Snapshot().pending == 0 &&
					fixture.metrics.Snapshot().presentAttempts == 2 && fixture.metrics.Snapshot().presentFailed == 1,
					"U206 restaged new output confirms Stored once and keeps failed attempt");
			}
			for (unsigned result = 0; result != 4; ++result)
			{
				Fixture fixture(now.QuadPart);
				fixture.Store(); fixture.Authorize(); fixture.Freeze(result != 0);
				const bool called = result != 0, succeeded = result >= 2;
				auto observed = fixture.producer->output;
				if (result == 3) ++observed.rawRevision;
				fixture.producer->PresentReturned(called, succeeded, returnQpc, 0.5, observed);
				const auto count = fixture.metrics.Snapshot();
				check(count.presentAttempts == (called ? 1u : 0u) &&
					count.presentSucceeded == (succeeded ? 1u : 0u) && count.presentFailed == (result == 1 ? 1u : 0u) &&
					count.confirmed == (result == 2 ? 1u : 0u),
					"U207 no-call/false/true-match/true-mismatch preserve exact actual attempt/result denominator");
				RuntimeMetricsFrameSample frame;
				frame.frameSerial = count.frameSerial;
				frame.frameStartMs = 1.0; frame.wallMs = 1.0;
				frame.physicalBefore = 1; frame.physicalAfter = 0; frame.terminalCount = 1;
				frame.presentAttempted = called; frame.presentSucceeded = succeeded;
				frame.presentWallMs = called ? 0.5 : 0.0;
				frame.reasonFlags = result == 0 ? (1u << 14) | (1u << 15) :
					(result == 1 ? 1u << 16 : 1u << 17); // 固定 CPU 输入对应冻结 FrameReason 位。
				fixture.producer->RecordFrame(frame);
				check(fixture.metrics.Snapshot().framesSeen == 1 && fixture.metrics.Snapshot().framesRetained == 1 &&
					fixture.producer->counters.noPresent == (called ? 0u : 1u) &&
					fixture.producer->counters.rasterFailed == (result == 0 ? 1u : 0u) &&
					fixture.producer->counters.outputMismatch == (result == 3 ? 1u : 0u),
					"U207 terminal after=0 and no-Present attempts enter real frame coverage");
			}
			{
				Fixture fixture(now.QuadPart);
				const auto a = fixture.Down();
				ContactSnapshot move = a.down;
				move.phase = ContactPhase::Move; move.qpc += 1; move.position.x += 24.0f;
				if (!fixture.input.PublishMove(0xE042, a.contactId, move) ||
					!fixture.input.TryReadSnapshot(a.handle, move)) throw 1;
				fixture.producer->NoteConsumed(a.key, move.sequence);
				fixture.Authorize();
				check(fixture.Freeze() == 0 &&
					!fixture.producer->Adopt(a.key, move, ContentMetricAdoptionKind::Model, false) &&
					fixture.producer->FindLive(a.key)->adoptedSequence == a.down.sequence,
					"U208 consumed or failed model input cannot impersonate an adopted sequence");
				check(fixture.producer->Adopt(a.key, move, ContentMetricAdoptionKind::Model, true),
					"U208 explicit real model geometry adoption advances its own sequence");
				fixture.producer->ObserveLiveRaster(a.key, move.sequence, { 36, 44, 68, 52 },
					fixture.metrics.Snapshot().frameSerial, false);
				check(fixture.Freeze() == 0, "U208 adopted geometry requires successful shared raster");
				fixture.producer->ObserveLiveRaster(a.key, move.sequence, { 36, 44, 68, 52 },
					fixture.metrics.Snapshot().frameSerial, true);
				check(fixture.Freeze() == 1, "U208 current adopted/rastered geometry stages live once");
				fixture.producer->PresentReturned(true, false, returnQpc, 0.5, fixture.producer->output);
				fixture.metrics.BeginFrame();
				check(fixture.Freeze() == 0, "U208 old-frame Live cannot stage without new successful raster");
				ContactSnapshot terminal;
				if (!fixture.Up(a, terminal)) throw 1;
				fixture.producer->NoteConsumed(a.key, terminal.sequence);
				check(fixture.producer->Adopt(a.key, terminal, ContentMetricAdoptionKind::RawTerminal, true) &&
					fixture.producer->FindLive(a.key)->adoptionKind == ContentMetricAdoptionKind::RawTerminal,
					"U208 raw terminal fallback has an explicit non-model adoption kind");
				fixture.input.Recycle(a.handle);
			}
			{
				Fixture fixture(now.QuadPart);
				const auto laser = fixture.Down(InputDeviceType::Pen, DrawingTool::Laser);
				fixture.Authorize();
				check(laser.ready && fixture.Freeze() == 0 && fixture.producer->counters.excludedLaser == 1,
					"U208 Laser formal landing is excluded even with a valid frame and real contact");
				fixture.producer->PresentReturned(true, true, returnQpc, 0.5, fixture.producer->output);
				RuntimeMetricsFrameSample frame;
				frame.frameSerial = fixture.metrics.Snapshot().frameSerial;
				frame.wallMs = 1.0; frame.presentWallMs = 0.5;
				frame.physicalBefore = 1; frame.terminalCount = 1;
				frame.presentAttempted = true; frame.presentSucceeded = true;
				frame.reasonFlags = (1u << 8) | (1u << 17);
				fixture.producer->RecordFrame(frame);
				fixture.Retire(laser);
				fixture.producer->Invalidate(laser.key, ContentMetricInvalidationReason::Stopped);
				check(fixture.metrics.Snapshot().confirmed == 0 && fixture.metrics.Snapshot().presentSucceeded == 1 &&
					fixture.metrics.Snapshot().framesRetained == 1 && fixture.metrics.Snapshot().unpresented == 1 &&
					fixture.producer->counters.invalidated[static_cast<size_t>(ContentMetricInvalidationReason::Stopped)] == 0,
					"U208 excluded Laser still retains frame/result denominator without a false stopped failure reason");
			}
			{
				Fixture fixture(now.QuadPart);
				constexpr size_t ownBytes = sizeof(ControllerContentMetrics);
				const auto allocated = fixture.metrics.Snapshot().allocatedBytes;
				const size_t exactExternal = ControllerContentMetrics::kByteBudget - ownBytes - static_cast<size_t>(allocated);
				check(!ControllerContentMetrics::Prepare(nullptr, (std::numeric_limits<size_t>::max)()) &&
					ControllerContentMetrics::Prepare(&fixture.metrics, exactExternal) &&
					!ControllerContentMetrics::Prepare(&fixture.metrics, exactExternal + 1) &&
					!ControllerContentMetrics::Prepare(&fixture.metrics, (std::numeric_limits<size_t>::max)()) &&
					ControllerContentMetrics::Prepare(&fixture.metrics, 320u * 240 * 4),
					"U209 default-null and shared-budget exact/overflow/checkpoint boundaries precede allocation");
				unsigned captured = 0;
				for (unsigned index = 0; index != 65; ++index) if (fixture.Store().captured) ++captured;
				check(captured == 64 && fixture.producer->counters.proofOverflow == 1 &&
					fixture.metrics.Snapshot().contactSeen == 65 && fixture.metrics.Snapshot().unpresented == 1 &&
					fixture.metrics.Snapshot().allocatedBytes == allocated && fixture.input.DiagnosticsSnapshot().occupiedSlots == 0,
					"U209 fixed note overflow retains denominator without hot growth or product admission loss");
				std::fprintf(stderr, "[Draw3ContentProof] payload: session=%llu auxiliary=%zu commonBudget=%zu\n",
					static_cast<unsigned long long>(allocated), ownBytes, ControllerContentMetrics::kByteBudget);
				check(!DrawingControllerMetricsState::Prepare(nullptr) &&
					DrawingControllerMetricsState::Prepare(&fixture.metrics) &&
					allocated + sizeof(DrawingControllerMetricsState) <= ControllerContentMetrics::kByteBudget,
					"U212 real Controller State includes frame/lifetime POD in the common fixed budget");
				std::fprintf(stderr, "[Draw3ContentProof] runtimeState=%zu\n", sizeof(DrawingControllerMetricsState));
			}
			{
				Fixture fixture(now.QuadPart);
				const auto stored = fixture.Store();
				fixture.Authorize();
				const auto* canvas = fixture.document.PageAt(0)->FindCanvas(kDefaultDeviceKey);
				check(fixture.producer->FreezeCandidates(fixture.Signature(), *canvas, fixture.runtimes[0],
					{ 0, 0, 64, 64 }, true, true) == 0, "U210 PresentFull cannot certify partial actual composite");
				check(fixture.producer->FreezeCandidates(fixture.Signature(), *canvas, fixture.runtimes[0],
					{ 0, 0, 64, 64 }, true, false) == 0, "U210 partial dirty must contain complete Stored projection");
				const auto* item = fixture.runtimes[0].history.Find(stored.committed.renderItem);
				RECT projection;
				if (!item || !ControllerContentMetrics::StoredProjection(item->pixelBounds, fixture.Signature(), projection)) throw 1;
				check(fixture.producer->FreezeCandidates(fixture.Signature(), *canvas, fixture.runtimes[0],
					projection, true, false) == 1, "U210 exact actual partial projection can stage Stored");
				fixture.producer->PresentReturned(true, false, returnQpc, 0.5, fixture.producer->output);
				if (!fixture.document.PageAt(0)->FindCanvas(kDefaultDeviceKey)->SetViewport({ 1000.0f, 1000.0f, 1.0f })) throw 1;
				check(fixture.Freeze() == 0, "U210 old viewport stamp cannot certify moved surface");
				fixture.Authorize();
				check(fixture.Freeze() == 0 && fixture.metrics.Snapshot().confirmed == 0,
					"U210 wholly outside current viewport is not zero-latency success");
			}
			{
				Fixture fixture(now.QuadPart);
				fixture.input.SetAdmissionBlocked(true); fixture.input.SetAdmissionBlocked(false);
				const auto a = fixture.Down();
				ContactSnapshot wrong = a.down;
				wrong.admissionRevision = 0;
				check(a.down.admissionRevision != 0 && !fixture.producer->Adopt(a.key, wrong, ContentMetricAdoptionKind::RawDown, true),
					"U211 real revision2 rejects mismatching0 while initial revision0 remains legal");
				wrong = a.down; wrong.qpc = a.down.qpc - 1;
				check(!fixture.producer->Adopt(a.key, wrong, ContentMetricAdoptionKind::RawDown, true),
					"U211 adopted source QPC cannot precede immutable actual Down");
				fixture.Authorize();
				const auto surface = fixture.Signature();
				fixture.producer->ObserveLiveRaster(a.key, a.down.sequence, { 36, 44, 44, 52 },
					fixture.metrics.Snapshot().frameSerial, true, &surface);
				check(fixture.Freeze() == 1, "U211 current explicit raster surface stages Live");
				fixture.metrics.BeginFrame();
				fixture.producer->ObserveLiveRaster(a.key, a.down.sequence + 1, { 36, 44, 44, 52 },
					fixture.metrics.Snapshot().frameSerial, true, &surface);
				check(fixture.Freeze() == 0, "U211 wrong raster geometry sequence is rejected");
				fixture.producer->ObserveLiveRaster(a.key, a.down.sequence, { 36, 44, 44, 52 },
					fixture.metrics.Snapshot().frameSerial, true, &surface);
				auto changedExtent = surface; --changedExtent.width;
				check(fixture.producer->FreezeCandidates(changedExtent, *fixture.document.PageAt(0)->FindCanvas(kDefaultDeviceKey),
					fixture.runtimes[0], { 0, 0, 319, 240 }, true, true) == 0, "U211 old Live raster cannot certify a new canvas extent");
				fixture.Retire(a);
			}
			// 固定CPU结果只验证正式帧helper；这里不注入或宣称真实GPU故障。
			const char* frameCases[] = {
				"U213 command-only/no-op does not create a raster frame",
				"U213 cache miss/Empty does not create a raster failure frame",
				"U213 failed raster before idle retains one noPresent frame and bit15",
				"U213 CPU visibility rejection preserves prior raster without bit15",
				"U213 rollback raster failure retains one frame rather than two",
				"U213 Empty rollback preserves the prior failed raster frame",
				"U213 true Present result survives a prior raster failure"
			};
			for (size_t index = 0; index != std::size(frameCases); ++index)
			{
				RuntimeMetricsSession metrics(8);
				auto producer = DrawingControllerMetricsState::Prepare(&metrics);
				if (!producer) throw 1;
				producer->Begin(1.0);
				producer->frame.reasonFlags |= static_cast<uint32_t>(RuntimeMetricsFrameReason::Command);
				if (index != 0)
				{
					const bool previous = producer->BeginRasterAttempt();
					producer->RasterReturned(previous, index == 3 || index == 4, index != 1);
				}
				if (index == 4 || index == 5)
				{
					const bool previous = producer->BeginRasterAttempt();
					producer->RasterReturned(previous, false, index == 4);
				}
				if (index == 6)
				{
					producer->PresentReturned(true, true, returnQpc, 0.5, {});
					producer->frame.presentAttempted = true;
					producer->frame.presentSucceeded = true;
					producer->frame.presentWallMs = 0.5;
				}
				producer->Finish(1.0);
				producer->Finish(1.0); // idle/RAII重入不能把同帧或rollback再记一次。
				const auto sample = metrics.Snapshot();
				const bool attempted = index >= 2;
				const bool failed = index == 2 || index >= 4;
				check(sample.framesSeen == (attempted ? 1u : 0u) && sample.framesRetained == sample.framesSeen &&
					sample.framesInvalid == 0 && producer->counters.noPresent == (attempted && index != 6 ? 1u : 0u) &&
					producer->counters.rasterFailed == (failed ? 1u : 0u) &&
					((producer->frame.reasonFlags & (1u << 15)) != 0) == failed &&
					((producer->frame.reasonFlags & static_cast<uint32_t>(RuntimeMetricsFrameReason::NoPresent)) != 0) ==
						(attempted && index != 6) &&
					(producer->frame.reasonFlags & static_cast<uint32_t>(RuntimeMetricsFrameReason::Command)) != 0 &&
					sample.presentAttempts == (index == 6 ? 1u : 0u) && sample.presentSucceeded == sample.presentAttempts &&
					sample.presentFailed == 0 &&
					(producer->frame.reasonFlags & static_cast<uint32_t>(RuntimeMetricsFrameReason::PresentFailed)) == 0 &&
					((producer->frame.reasonFlags & static_cast<uint32_t>(RuntimeMetricsFrameReason::PresentSucceeded)) != 0) ==
						(index == 6), frameCases[index]);
			}
		}
		catch (...)
		{
			check(false, "actual Coordinator/CPU commit fixture could not be prepared");
		}
		if (failures == 0) std::fputs("[Draw3ContentProof] PASS: production content assembler contract\n", stderr);
		return failures;
	}

	int RunParkedDesktopExitAutoSaveTest() noexcept
	{
		int failures = 0;
		const auto check = [&failures](bool condition, const char* name)
		{
			if (condition) return;
			++failures;
			std::fprintf(stderr, "[Draw3DesktopExit] FAIL: %s\n", name);
		};
		const auto guid = [](uint8_t marker)
		{
			std::array<uint8_t, 16> bytes = {};
			bytes[0] = marker;
			bytes[15] = 0xA5;
			return InkGuid(bytes);
		};
		InkCanvasCollection desktop(guid(0x10));
		InkCanvasCollection presentation(guid(0x20));
		InkCanvasCollection emptyDesktop(guid(0x30));
		std::vector<CanvasPageRuntimeState> desktopRuntimes(1);
		std::vector<CanvasPageRuntimeState> presentationRuntimes(1);
		std::vector<CanvasPageRuntimeState> emptyRuntimes(1);
		const auto appendPageAndStroke = [&](InkCanvasCollection& document,
			CanvasPageRuntimeState& runtime, uint8_t pageMarker,
			float firstX) -> bool
		{
			const std::optional<size_t> pageIndex = document.AppendPage(guid(pageMarker));
			InkPage* page = pageIndex ? document.PageAt(*pageIndex) : nullptr;
			InkCanvas* canvas = page ? page->GetOrCreateCanvas(kDefaultDeviceKey) : nullptr;
			if (!canvas) return false;
			StoredInkStyle style;
			style.inkType = StoredInkType::Pen;
			style.fallbackRgb = 0x123456u;
			const std::optional<size_t> strokeIndex = canvas->AppendStroke(InkStroke(style,
				{ { firstX, 10.0f, 3.0f }, { firstX + 12.0f, 20.0f, 3.0f } }));
			if (!strokeIndex) return false;
			std::optional<StrokeTileFootprint> footprint =
				BuildStrokeTileFootprint(canvas->Strokes()[*strokeIndex]);
			return footprint && runtime.history.AppendStroke(
				*strokeIndex, std::move(*footprint), true).has_value();
		};
		if (!appendPageAndStroke(desktop, desktopRuntimes.front(), 0x11, 10.0f) ||
			!appendPageAndStroke(presentation, presentationRuntimes.front(), 0x21, 110.0f) ||
			!emptyDesktop.AppendPage(guid(0x31)))
		{
			check(false, "create isolated CPU documents and visible history");
			return 1;
		}
		desktopRuntimes.front().beforeStates.push_back(1);
		desktopRuntimes.front().afterStates.push_back(2);
		desktopRuntimes.front().rasterState = 2;
		DesktopAutoSavePolicy policy;
		struct Captured
		{
			int calls = 0;
			DesktopAutoSaveTrigger trigger = DesktopAutoSaveTrigger::Clear;
			std::optional<draw3::uink::Draw3UInkExportSnapshot> snapshot;
		};
		Captured captured;
		DrawingControllerRuntimeObserver observer;
		observer.context = &captured;
		observer.desktopAutoSaveRequested = [](void* context,
			DesktopAutoSaveTrigger trigger,
			draw3::uink::Draw3UInkExportSnapshot&& snapshot)
		{
			auto& result = *static_cast<Captured*>(context);
			++result.calls;
			result.trigger = trigger;
			result.snapshot = std::move(snapshot);
			return true;
		};
		const DesktopAutoSaveSource desktopSource{
			&desktop, &desktopRuntimes, 0 };
		const DesktopAutoSaveSource presentationSource{
			&presentation, &presentationRuntimes, 0 };
		const DesktopAutoSaveSource emptySource{
			&emptyDesktop, &emptyRuntimes, 0 };
		const auto capture = [&](Bridge::Workspace workspace,
			DesktopAutoSaveTrigger trigger, bool enabled,
			const DesktopAutoSaveSource& active,
			const DesktopAutoSaveSource& parked)
		{
			captured = {};
			return CaptureDesktopAutoSaveForScene(workspace, trigger, active,
				parked, policy, enabled, 1.0f, observer);
		};
		const auto correctDesktopIdentity = [&]()
		{
			return captured.calls == 1 && captured.trigger == DesktopAutoSaveTrigger::Exit &&
				captured.snapshot && captured.snapshot->workspaceName == "Desktop" &&
				captured.snapshot->workspaceGuid.Bytes() == desktop.WorkspaceGuid().Bytes() &&
				captured.snapshot->canvases.size() == 1 &&
				captured.snapshot->canvases[0].pageGuid.Bytes() ==
					desktop.PageAt(0)->PageGuid().Bytes() &&
				captured.snapshot->canvases[0].strokes.size() == 1 &&
				captured.snapshot->canvases[0].strokes[0].points.size() == 2 &&
				captured.snapshot->canvases[0].strokes[0].points[0].x == 10.0f;
		};
		check(capture(Bridge::Workspace::Desktop, DesktopAutoSaveTrigger::Exit,
			true, desktopSource, presentationSource) && correctDesktopIdentity(),
			"active Desktop Exit submits one Desktop-only snapshot");
		check(capture(Bridge::Workspace::Presentation, DesktopAutoSaveTrigger::Exit,
			true, presentationSource, desktopSource) && correctDesktopIdentity(),
			"PPT-active Exit captures parked Desktop, not PPT page or ink");
		check(capture(Bridge::Workspace::Whiteboard, DesktopAutoSaveTrigger::Exit,
			true, presentationSource, desktopSource) && correctDesktopIdentity(),
			"Whiteboard-active Exit captures parked Desktop exactly once");
		check(!capture(Bridge::Workspace::Presentation, DesktopAutoSaveTrigger::Clear,
			true, presentationSource, desktopSource) && captured.calls == 0,
			"parked Desktop is not saved on non-Desktop Clear");
		check(!capture(Bridge::Workspace::Presentation, DesktopAutoSaveTrigger::Exit,
			false, presentationSource, desktopSource) && captured.calls == 0,
			"disabled Desktop auto-save submits nothing");
		check(!capture(Bridge::Workspace::Presentation, DesktopAutoSaveTrigger::Exit,
			true, presentationSource, emptySource) && captured.calls == 0,
			"empty parked Desktop submits nothing");
		const int desktopFailures = failures;
		if (desktopFailures == 0)
			std::fputs("[Draw3DesktopExit] PASS: production Desktop Exit source and snapshot\n",
				stderr);
		{
			// 红测：真实 contact/mailbox 与生产 Exit builder 中，活动实点必须在 CPU 封口后出现。
			ContactInputCoordinator input;
			ContactSnapshot down;
			down.position = { 40.0f, 50.0f };
			down.phase = ContactPhase::Down;
			down.qpc = 1000;
			ContactRecord* record = nullptr;
			check(input.PublishDown(0xF026, 1, InputDeviceType::Pen, down) &&
				input.TryDequeue(record) && record, "admit fatal Pen Down");
			if (record)
			{
				RuntimeStroke activePen(1000.0f);
				activePen.handle = { record, record->Generation() };
				activePen.inUse = true;
				activePen.ownerWorkspaceGuid = desktop.WorkspaceGuid();
				activePen.ownerPageGuid = desktop.PageAt(0)->PageGuid();
				activePen.ownerPageIndex = 0;
				activePen.viewport = desktop.PageAt(0)->FindCanvas(kDefaultDeviceKey)->Viewport();
				activePen.stroke.hasInputStartPoint = true;
				activePen.stroke.inputStartPoint = { 40.0f, 50.0f, 2.5f, 0.0f };
				activePen.stroke.realPoints.push_back(activePen.stroke.inputStartPoint);
				ContactSnapshot move = down;
				move.position = { 55.0f, 65.0f };
				move.phase = ContactPhase::Move;
				move.qpc = 2000;
				check(input.PublishMove(0xF026, 1, move) &&
					input.TryReadSnapshot(activePen.handle, activePen.lastInputSnapshot),
					"consume fatal Pen Move");
				activePen.lastConsumedSequence = activePen.lastInputSnapshot.sequence;
				// 模型实点只到 Down；封口必须从已消费 raw Move 补 x55。
				activePen.stroke.predictedPoints.push_back({ 500.0f, 600.0f, 2.5f, 0.002f });
				check(capture(Bridge::Workspace::Desktop, DesktopAutoSaveTrigger::Exit,
					true, desktopSource, presentationSource) &&
					captured.snapshot->canvases[0].strokes.size() == 1,
					"pre-seal snapshot keeps completed old stroke only");
				input.SetAdmissionBlocked(true);
				InkRasterStateToken nextToken = 3;
				const auto sealed = CommitRuntimeStoredStrokeCpu(activePen, desktop,
					desktopRuntimes, 0, 0.0, StoredStrokeCommitMode::Fatal,
					[&] { return nextToken++; });
				check(sealed.has_value() &&
					capture(Bridge::Workspace::Desktop, DesktopAutoSaveTrigger::Exit,
						true, desktopSource, presentationSource) &&
					captured.snapshot->canvases[0].strokes.size() == 2 &&
					captured.snapshot->canvases[0].strokes[1].points.back().x == 55.0f,
					"fatal CPU seal appends accepted real Move, excludes prediction");
				const size_t afterPen = desktopRuntimes[0].history.Items().size();
				check(!CommitRuntimeStoredStrokeCpu(activePen, desktop, desktopRuntimes,
					0, 0.0, StoredStrokeCommitMode::Fatal,
					[&] { return nextToken++; }) &&
					desktopRuntimes[0].history.Items().size() == afterPen,
					"repeat fatal cannot append same contact twice");
				auto makeRejected = [&](DrawingTool tool)
				{
					auto runtime = std::make_unique<RuntimeStroke>(1000.0f);
					runtime->handle = activePen.handle;
					runtime->inUse = true;
					runtime->ownerWorkspaceGuid = desktop.WorkspaceGuid();
					runtime->ownerPageGuid = desktop.PageAt(0)->PageGuid();
					runtime->ownerPageIndex = 0;
					runtime->viewport = activePen.viewport;
					runtime->tool = tool;
					runtime->lastInputSnapshot = activePen.lastInputSnapshot;
					runtime->lastConsumedSequence = activePen.lastConsumedSequence;
					runtime->stroke.hasInputStartPoint = true;
					runtime->stroke.inputStartPoint = activePen.stroke.inputStartPoint;
					runtime->stroke.realPoints = activePen.stroke.realPoints;
					return runtime;
				};
				auto cancelled = makeRejected(DrawingTool::Pen);
				cancelled->cancelled = true;
				check(!CommitRuntimeStoredStrokeCpu(*cancelled, desktop, desktopRuntimes,
					0, 0.0, StoredStrokeCommitMode::Fatal,
					[&] { return nextToken++; }) &&
					desktopRuntimes[0].history.Items().size() == afterPen,
					"Cancelled contact is never saved by fatal seal");
				auto laser = makeRejected(DrawingTool::Laser);
				check(!CommitRuntimeStoredStrokeCpu(*laser, desktop, desktopRuntimes,
					0, 0.0, StoredStrokeCommitMode::Fatal,
					[&] { return nextToken++; }) &&
					desktopRuntimes[0].history.Items().size() == afterPen,
					"Laser remains transient under fatal seal");
				auto foreign = makeRejected(DrawingTool::Pen);
				foreign->ownerPageGuid = presentation.PageAt(0)->PageGuid();
				check(!CommitRuntimeStoredStrokeCpu(*foreign, desktop, desktopRuntimes,
					0, 0.0, StoredStrokeCommitMode::Fatal,
					[&] { return nextToken++; }) &&
					desktopRuntimes[0].history.Items().size() == afterPen,
					"cross-page runtime cannot enter current page history");
				auto lateCancel = makeRejected(DrawingTool::Pen);
				lateCancel->lastInputSnapshot.phase = ContactPhase::Cancelled;
				check(!CommitRuntimeStoredStrokeCpu(*lateCancel, desktop, desktopRuntimes,
					0, 0.0, StoredStrokeCommitMode::Fatal,
					[&] { return nextToken++; }) &&
					desktopRuntimes[0].history.Items().size() == afterPen,
					"accepted Cancelled terminal is never saved");
				const auto commitTool = [&](RuntimeStroke& runtime, const char* label)
				{
					const size_t before = desktopRuntimes[0].history.Items().size();
					const auto result = CommitRuntimeStoredStrokeCpu(runtime, desktop,
						desktopRuntimes, 0, 0.0, StoredStrokeCommitMode::Fatal,
						[&] { return nextToken++; });
					const InkCanvas* canvas = desktop.PageAt(0)->FindCanvas(kDefaultDeviceKey);
					check(result && desktopRuntimes[0].history.Items().size() == before + 1 &&
						canvas->Strokes()[result->strokeIndex].Points().back().x ==
							runtime.lastInputSnapshot.position.x, label);
				};
				for (DrawingTool tool : { DrawingTool::HardPen, DrawingTool::Highlighter,
					DrawingTool::Eraser, DrawingTool::SolidLine, DrawingTool::DashedLine,
					DrawingTool::OutlineRectangle, DrawingTool::FilledRectangle })
				{
					auto runtime = makeRejected(tool);
					if (IsShapeDrawingTool(tool))
					{
						runtime->shape.active = true;
						runtime->shape.primitive.start = runtime->stroke.inputStartPoint;
						runtime->shape.primitive.end = { 500.0f, 600.0f, 0.0f, 0.0f };
					}
					commitTool(*runtime, "fatal seal stores real endpoint for durable tool");
				}
				auto speedEraser = makeRejected(DrawingTool::Eraser);
				speedEraser->stroke.widthMode = StrokeWidthMode::SpeedEraser;
				commitTool(*speedEraser, "speed Eraser real geometry is saved");
				auto downOnly = makeRejected(DrawingTool::Pen);
				downOnly->lastInputSnapshot = record->DownSnapshot();
				downOnly->lastConsumedSequence = downOnly->lastInputSnapshot.sequence;
				downOnly->stroke.realPoints.clear();
				commitTool(*downOnly, "Down-only Pen keeps its accepted start point");
				auto deferred = makeRejected(DrawingTool::Pen);
				deferred->awaitingReconnect = true;
				deferred->deferredUpSnapshot = deferred->lastInputSnapshot;
				deferred->deferredUpSnapshot.phase = ContactPhase::Up;
				commitTool(*deferred, "actual deferred Up remains durable");
				auto invalid = makeRejected(DrawingTool::Pen);
				invalid->lastInputSnapshot.position.x =
					(std::numeric_limits<float>::quiet_NaN)();
				const size_t beforeInvalid = desktopRuntimes[0].history.Items().size();
				check(!CommitRuntimeStoredStrokeCpu(*invalid, desktop, desktopRuntimes,
					0, 0.0, StoredStrokeCommitMode::Fatal,
					[&] { return nextToken++; }) &&
					desktopRuntimes[0].history.Items().size() == beforeInvalid,
					"invalid terminal cannot contaminate visible history");
				auto normalUp = makeRejected(DrawingTool::Pen);
				normalUp->ended = true;
				const auto normalCommit = CommitRuntimeStoredStrokeCpu(*normalUp, desktop,
					desktopRuntimes, 0, 0.0, StoredStrokeCommitMode::NormalUp,
					[&] { return nextToken++; });
				check(normalCommit && desktopRuntimes[0].history.Items().size() ==
					beforeInvalid + 1, "normal Up and fatal share CPU history commit");
				check(capture(Bridge::Workspace::Desktop, DesktopAutoSaveTrigger::Exit,
					true, desktopSource, presentationSource) &&
					captured.snapshot->canvases[0].strokes.size() ==
						desktopRuntimes[0].history.Items().size() &&
					std::all_of(captured.snapshot->canvases[0].strokes.begin(),
						captured.snapshot->canvases[0].strokes.end(), [](const auto& stroke)
						{ return std::all_of(stroke.points.begin(), stroke.points.end(),
							[](const auto& point) { return point.x < 100.0f && point.y < 100.0f; }); }),
					"all fatal CPU commits enter Exit snapshot without prediction");
				// PPT 同样从权威 history 构造快照，并保持目标页与 Desktop 隔离。
				presentationRuntimes[0].beforeStates.push_back(1);
				presentationRuntimes[0].afterStates.push_back(2);
				presentationRuntimes[0].rasterState = 2;
				const auto endScreen = presentation.AppendPage(guid(0x22));
				InkPage* endPage = endScreen ? presentation.PageAt(*endScreen) : nullptr;
				InkCanvas* endCanvas = endPage
					? endPage->GetOrCreateCanvas(kDefaultDeviceKey) : nullptr;
				if (endCanvas) presentationRuntimes.emplace_back();
				check(endCanvas != nullptr, "prepare independent PPT EndScreen page");
				if (endCanvas)
				{
					auto pptPen = makeRejected(DrawingTool::Pen);
					pptPen->ownerWorkspaceGuid = presentation.WorkspaceGuid();
					pptPen->ownerPageGuid = presentation.PageAt(0)->PageGuid();
					pptPen->viewport = presentation.PageAt(0)->FindCanvas(kDefaultDeviceKey)->Viewport();
					const auto pptCommit = CommitRuntimeStoredStrokeCpu(*pptPen,
						presentation, presentationRuntimes, 0, 0.0,
						StoredStrokeCommitMode::Fatal, [&] { return nextToken++; });
					Bridge::PresentationTarget target;
					target.key.bytes[0] = 0xA1;
					target.bindingMode = Bridge::SlideBindingMode::StableSlideId;
					target.sourceIdentity = "fatal.pptx";
					target.presentationName = "fatal.pptx";
					target.slideIds = { 101 };
					target.slideId = 101;
					target.pageIndex = 0;
					target.totalPages = 1;
					target.bindingRevision = 1;
					target.targetRevision = 1;
					target.sessionRevision = 1;
					std::optional<draw3::uink::UInkGuid> fileGuid;
					const RetainedPresentationSlides emptyRetained;
					const auto request = BuildPresentationSaveRequest(presentation,
						presentationRuntimes, 0, target, fileGuid, 1, 1.0f, emptyRetained);
					check(pptCommit && request && request->snapshot.activeCanvases.size() == 2 &&
						request->snapshot.activeCanvases[0].strokes.size() == 2 &&
						request->snapshot.activeCanvases[0].pageGuid.Bytes() ==
							presentation.PageAt(0)->PageGuid().Bytes() &&
						request->snapshot.activeCanvases[1].strokes.empty(),
						"PPT fatal CPU ink keeps track, slide and EndScreen identity");
					auto foreignTrack = makeRejected(DrawingTool::Pen);
					check(!CommitRuntimeStoredStrokeCpu(*foreignTrack, presentation,
						presentationRuntimes, 0, 0.0, StoredStrokeCommitMode::Fatal,
						[&] { return nextToken++; }) &&
						presentationRuntimes[0].history.Items().size() == 2,
						"foreign Desktop contact cannot enter PPT track");
				}
				const int beforeClosingFailures = failures;
				ContactInputCoordinator closingInput;
				ContactSnapshot closingDown = down;
				closingDown.position = { 70.0f, 80.0f };
				ContactRecord* closingRecord = nullptr;
				const bool prepared = closingInput.PublishDown(0xF027, 1,
					InputDeviceType::Pen, closingDown) &&
					closingInput.TryDequeue(closingRecord) && closingRecord;
				check(prepared, "prepare real ContactInput Closing probe");
				if (prepared)
				{
					const ContactHandle closingHandle{ closingRecord, closingRecord->Generation() };
					ContactClosePauseForTesting pause;
					closingInput.PauseNextCloseAfterRouteClosedForTesting(&pause);
					std::atomic<bool> upDone = false;
					std::thread upper([&]
						{ closingInput.PublishUp(0xF027, 1, closingDown);
							upDone.store(true, std::memory_order_release); });
					const auto waitFor = [](const std::atomic<bool>& signal, DWORD timeoutMs)
					{
						const ULONGLONG deadline = GetTickCount64() + timeoutMs;
						while (!signal.load(std::memory_order_acquire) &&
							GetTickCount64() < deadline) Sleep(1);
						return signal.load(std::memory_order_acquire);
					};
					// hook 在真实 route CAS 到 Closing 之后置位，无需暂停任意指令的线程。
					const bool closingObserved = waitFor(pause.entered, 1000);
					bool fatalRouteReturned = false;
					if (closingObserved)
					{
						std::atomic<bool> finishStarted = false;
						std::atomic<bool> finishDone = false;
						std::thread finisher([&]
							{ finishStarted.store(true, std::memory_order_release);
								StopFatalInputConsumer(closingInput);
								finishDone.store(true, std::memory_order_release); });
						const bool finishEntered = waitFor(finishStarted, 1000);
						if (finishEntered) fatalRouteReturned = waitFor(finishDone, 100) &&
							closingInput.AdmissionBlocked();
						pause.resume.store(true, std::memory_order_release);
						upper.join();
						finisher.join();
					}
					else
					{
						pause.resume.store(true, std::memory_order_release);
						upper.join();
					}
					closingInput.Recycle(closingHandle);
					check(closingObserved && upDone.load(std::memory_order_acquire),
						"inject real ContactInput Closing state");
					check(!closingObserved || fatalRouteReturned,
						"fatal route finish must return before Closing producer resumes");
				}
				if (failures == beforeClosingFailures)
					std::fputs("[Draw3FatalClosing] PASS: real Closing does not wait at fatal exit\n", stderr);
				input.DiscardUntilTerminal(activePen.handle);
			}
		}
		if (failures == desktopFailures)
			std::fputs("[Draw3FatalActiveInk] PASS: production CPU seal and Exit snapshot\n",
				stderr);
		const int laserFailures = RunIgnoredLaserTouchProductionTest();
		const int initializationFailures = RunRejectedStrokeInitializationProductionTest();
		const int metricsFailures = RunRuntimeMetricsSessionProductionProbe();
		const int contentProofFailures = RunDraw3ContentProofProductionProbe();
		return failures == 0 && laserFailures == 0 && initializationFailures == 0 &&
			metricsFailures == 0 && contentProofFailures == 0 ? 0 : 1;
	}

	int RunParkedPresentationRetainedSaveTest() noexcept
	{
		int failures = 0;
		const auto check = [&failures](bool condition, const char* name)
		{
			if (condition) return;
			++failures;
			std::fprintf(stderr, "[Draw3PptRetained] FAIL: %s\n", name);
		};
		const auto guid = [](uint8_t marker)
		{
			std::array<uint8_t, 16> bytes = {};
			bytes[0] = marker;
			bytes[15] = 0xA5;
			return InkGuid(bytes);
		};
		const auto populate = [&](InkCanvasCollection& document,
			std::vector<CanvasPageRuntimeState>& runtimes,
			uint8_t firstPageMarker, float x) -> bool
		{
			runtimes.resize(2);
			for (uint8_t page = 0; page < 2; ++page)
			{
				const auto index = document.AppendPage(guid(firstPageMarker + page));
				InkPage* inkPage = index ? document.PageAt(*index) : nullptr;
				InkCanvas* canvas = inkPage
					? inkPage->GetOrCreateCanvas(kDefaultDeviceKey) : nullptr;
				if (!canvas) return false;
				if (page != 0) continue; // 第二页是独立的 EndScreen 空槽。
				StoredInkStyle style;
				style.inkType = StoredInkType::Pen;
				style.fallbackRgb = 0x123456u;
				const auto strokeIndex = canvas->AppendStroke(InkStroke(style,
					{ { x, 10.0f, 3.0f }, { x + 8.0f, 18.0f, 3.0f } }));
				if (!strokeIndex) return false;
				auto footprint = BuildStrokeTileFootprint(canvas->Strokes()[*strokeIndex]);
				if (!footprint || !runtimes[*index].history.AppendStroke(
					*strokeIndex, std::move(*footprint), true)) return false;
			}
			return true;
		};
		InkCanvasCollection activeDocument(guid(0x10));
		std::vector<CanvasPageRuntimeState> activeRuntimes;
		DrawingDocumentSlot parked;
		parked.document.emplace(guid(0x20));
		if (!populate(activeDocument, activeRuntimes, 0x11, 10.0f) ||
			!populate(*parked.document, parked.pageRuntimeStates, 0x21, 20.0f))
		{
			check(false, "create A/B active pages, EndScreen and history");
			return 1;
		}
		const auto target = [](uint8_t keyMarker, const char* identity)
		{
			Bridge::PresentationTarget value;
			value.key.bytes[0] = keyMarker;
			value.bindingMode = Bridge::SlideBindingMode::StableSlideId;
			value.sourceIdentity = identity;
			value.presentationName = identity;
			value.slideIds = { 101 };
			value.slideId = 101;
			value.pageIndex = 0;
			value.totalPages = 1;
			value.bindingRevision = 1;
			value.targetRevision = 1;
			value.sessionRevision = 1;
			return value;
		};
		const auto activeTarget = target(0xA1, "A.pptx");
		parked.presentationTarget = target(0xB1, "B.pptx");
		const auto retained = [&](uint8_t pageMarker, float x)
		{
			draw3::uink::Draw3UInkCanvasSnapshot canvas;
			canvas.pageGuid = draw3::uink::UInkGuid(guid(pageMarker).Bytes());
			canvas.slideId = 102; // A/B 的 SlideID 可数值相同，page GUID 与墨迹必须仍隔离。
			canvas.retained = true;
			canvas.viewport = { 0.0f, 0.0f, 1.0f };
			draw3::uink::Draw3UInkStrokeSnapshot stroke;
			stroke.points = { { x, 30.0f, 3.0f }, { x + 6.0f, 36.0f, 3.0f } };
			canvas.strokes.push_back(std::move(stroke));
			return canvas;
		};
		RetainedPresentationSlides activeRetained;
		activeRetained.emplace(102, retained(0x13, 100.0f));
		parked.retainedSlides.emplace(102, retained(0x23, 200.0f));
		std::optional<draw3::uink::UInkGuid> activeFileGuid;
		auto activeRequest = BuildPresentationSaveRequest(activeDocument,
			activeRuntimes, 0, activeTarget, activeFileGuid, 1, 1.0f,
			RetainedSlidesForSave(activeRetained, nullptr));
		check(activeRequest && activeRequest->snapshot.retainedCanvases.size() == 1 &&
			activeRequest->snapshot.retainedCanvases[0].pageGuid.Bytes() ==
				guid(0x13).Bytes(),
			"active A retains only A page identity");
		std::optional<draw3::uink::UInkGuid> parkedFileGuid;
		auto parkedRequest = BuildPresentationSaveRequest(*parked.document,
			parked.pageRuntimeStates, 0, *parked.presentationTarget,
			parkedFileGuid, 1, 1.0f,
			RetainedSlidesForSave(activeRetained, &parked));
		const bool parkedIdentity = parkedRequest &&
			parkedRequest->snapshot.workspaceGuid.Bytes() ==
				parked.document->WorkspaceGuid().Bytes() &&
			parkedRequest->snapshot.activeCanvases.size() == 2 &&
			parkedRequest->snapshot.activeCanvases[0].pageGuid.Bytes() ==
				guid(0x21).Bytes() &&
			parkedRequest->snapshot.activeCanvases[0].strokes.size() == 1 &&
			parkedRequest->snapshot.activeCanvases[0].strokes[0].points[0].x == 20.0f &&
			parkedRequest->snapshot.retainedCanvases.size() == 1 &&
			parkedRequest->snapshot.retainedCanvases[0].pageGuid.Bytes() ==
				guid(0x23).Bytes() &&
			parkedRequest->snapshot.retainedCanvases[0].slideId == 102 &&
			parkedRequest->snapshot.retainedCanvases[0].strokes.size() == 1 &&
			parkedRequest->snapshot.retainedCanvases[0].strokes[0].points[0].x == 200.0f;
		check(parkedIdentity,
			"parked B request keeps B active and retained page GUID/ink");
		RetainedPresentationSlides emptyActive;
		auto noLeakRequest = BuildPresentationSaveRequest(*parked.document,
			parked.pageRuntimeStates, 0, *parked.presentationTarget,
			parkedFileGuid, 1, 1.0f,
			RetainedSlidesForSave(emptyActive, &parked));
		check(noLeakRequest && noLeakRequest->snapshot.retainedCanvases.size() == 1 &&
			noLeakRequest->snapshot.retainedCanvases[0].pageGuid.Bytes() ==
				guid(0x23).Bytes(),
			"parked B retained page survives empty active map");
		if (failures == 0)
			std::fputs("[Draw3PptRetained] PASS: production A/B retained source identity\n",
				stderr);
		return failures == 0 ? 0 : 1;
	}

	int RunPresentationLoadedRetainedInstallTest() noexcept
	{
		int failures = 0;
		const auto check = [&failures](bool condition, const char* name)
		{
			if (condition) return;
			++failures;
			std::fprintf(stderr, "[Draw3PptLoadedRetained] FAIL: %s\n", name);
		};
		const auto guid = [](uint8_t marker)
		{
			std::array<uint8_t, 16> bytes = {};
			bytes[0] = marker;
			bytes[15] = 0xA5;
			return draw3::uink::UInkGuid(bytes);
		};
		Bridge::PresentationTarget target;
		target.key.bytes[0] = 0xA1;
		target.bindingMode = Bridge::SlideBindingMode::StableSlideId;
		target.sourceIdentity = "loaded.pptx";
		target.presentationName = "loaded.pptx";
		target.slideIds = { 101 };
		target.slideId = 101;
		target.pageIndex = 0;
		target.totalPages = 1;
		target.bindingRevision = 1;
		target.targetRevision = 1;
		target.sessionRevision = 1;
		draw3::uink::Draw3UInkExportSnapshot source;
		source.fileGuid = guid(0x40);
		source.workspaceGuid = guid(0x41);
		source.workspaceType = 2;
		source.hostId = FormatPresentationKey(target.key);
		source.currentPageIndex = 0;
		source.workspaceExtra = draw3::uink::MakeInkeysBindingExtra(
			draw3::uink::Draw3UInkImportBindingMode::StableSlideId);
		draw3::uink::Draw3UInkCanvasSnapshot slide;
		slide.pageGuid = guid(0x42);
		slide.slideId = 101;
		slide.viewport = { 0.0f, 0.0f, 1.0f };
		slide.extra = draw3::uink::MakeInkeysBindingExtra(
			draw3::uink::Draw3UInkImportBindingMode::StableSlideId);
		draw3::uink::Draw3UInkStrokeSnapshot activeStroke;
		activeStroke.points = { { 10.0f, 10.0f, 3.0f },
			{ 20.0f, 20.0f, 3.0f } };
		slide.strokes.push_back(std::move(activeStroke));
		source.activeCanvases.push_back(std::move(slide));
		draw3::uink::Draw3UInkCanvasSnapshot endScreen;
		endScreen.pageGuid = guid(0x43);
		endScreen.viewport = { 0.0f, 0.0f, 1.0f };
		endScreen.extra = draw3::uink::MakeInkeysEndScreenExtra(
			draw3::uink::Draw3UInkImportBindingMode::StableSlideId);
		source.activeCanvases.push_back(std::move(endScreen));
		draw3::uink::Draw3UInkCanvasSnapshot retained;
		retained.pageGuid = guid(0x44);
		retained.slideId = 102;
		retained.retained = true;
		retained.viewport = { 0.0f, 0.0f, 1.0f };
		retained.extra = draw3::uink::MakeInkeysBindingExtra(
			draw3::uink::Draw3UInkImportBindingMode::StableSlideId);
		draw3::uink::Draw3UInkStrokeSnapshot retainedStroke;
		retainedStroke.points = { { 100.0f, 30.0f, 3.0f },
			{ 110.0f, 40.0f, 3.0f } };
		retained.strokes.push_back(std::move(retainedStroke));
		source.retainedCanvases.push_back(std::move(retained));
		std::uint64_t nextRasterToken = 1;
		const auto allocate = [&]() { return nextRasterToken++; };
		auto loaded = MaterializePresentationSlot(source, target, 7, allocate);
		check(loaded && loaded->document && loaded->retainedSlides.size() == 1 &&
			loaded->retainedSlides.contains(102) &&
			loaded->retainedSlides.at(102).pageGuid == guid(0x44),
			"production materializer retains loaded slide identity");
		if (!loaded || !loaded->document) return 1;
		std::optional<InkCanvasCollection> activeDocument;
		std::vector<CanvasPageRuntimeState> activeRuntimes;
		std::size_t activePageIndex = 0;
		std::optional<Bridge::PresentationTarget> activeTarget;
		RetainedPresentationSlides activeRetained;
		std::optional<draw3::uink::UInkGuid> activeFileGuid;
		std::uint64_t mutationRevision = 0;
		std::uint64_t queuedRevision = 0;
		std::uint64_t committedRevision = 0;
		const bool installed = InstallLoadedActivePresentationSlot(loaded, target, {
			activeDocument, activeRuntimes, activePageIndex, activeTarget,
			activeRetained, activeFileGuid, mutationRevision, queuedRevision,
			committedRevision });
		check(installed && activeDocument && activeTarget &&
			activeDocument->WorkspaceGuid().Bytes() == source.workspaceGuid.Bytes() &&
			activeDocument->PageAt(0)->PageGuid().Bytes() == guid(0x42).Bytes() &&
			activeDocument->PageAt(1)->PageGuid().Bytes() == guid(0x43).Bytes() &&
			mutationRevision == 7 && queuedRevision == 7 && committedRevision == 7,
			"active install preserves loaded document, EndScreen and revisions");
		if (!activeDocument || !activeTarget) return 1;
		auto saved = BuildPresentationSaveRequest(*activeDocument, activeRuntimes,
			activePageIndex, *activeTarget, activeFileGuid, 8, 1.0f,
			activeRetained);
		check(saved && saved->snapshot.workspaceGuid == source.workspaceGuid &&
			saved->snapshot.activeCanvases.size() == 2 &&
			saved->snapshot.activeCanvases[0].strokes.size() == 1 &&
			saved->snapshot.activeCanvases[0].strokes[0].points[0].x == 10.0f &&
			saved->snapshot.retainedCanvases.size() == 1 &&
			saved->snapshot.retainedCanvases[0].pageGuid == guid(0x44) &&
			saved->snapshot.retainedCanvases[0].strokes.size() == 1 &&
			saved->snapshot.retainedCanvases[0].strokes[0].points[0].x == 100.0f,
			"post-load production save keeps retained page GUID and ink");

		auto lateLoaded = MaterializePresentationSlot(source, target, 7, allocate);
		const auto beforeWorkspace = activeDocument->WorkspaceGuid().Bytes();
		activeRetained.emplace(777, source.retainedCanvases.front());
		mutationRevision = 9; // 用户的新写入使迟到的加载结果失去安装资格。
		check(!InstallLoadedActivePresentationSlot(lateLoaded, target, {
			activeDocument, activeRuntimes, activePageIndex, activeTarget,
			activeRetained, activeFileGuid, mutationRevision, queuedRevision,
			committedRevision }) &&
			activeDocument->WorkspaceGuid().Bytes() == beforeWorkspace &&
			activeRetained.contains(777) && mutationRevision == 9 && lateLoaded,
			"late load cannot overwrite a mutated active slot or retained map");
		if (failures == 0)
			std::fputs("[Draw3PptLoadedRetained] PASS: materialize-install-save identity\n",
				stderr);
		return failures == 0 ? 0 : 1;
	}

	int RunPendingPresentationTopologyLoadTest() noexcept
	{
		int failures = 0;
		const auto check = [&failures](bool condition, const char* name)
		{
			if (condition) return;
			++failures;
			std::fprintf(stderr, "[Draw3PptTopologyLoad] FAIL: %s\n", name);
		};
		const auto guid = [](uint8_t marker)
		{
			std::array<uint8_t, 16> bytes = {};
			bytes[0] = marker;
			bytes[15] = 0xA5;
			return draw3::uink::UInkGuid(bytes);
		};
		const auto target = [](std::vector<std::int32_t> ids)
		{
			Bridge::PresentationTarget value;
			value.key.bytes[0] = 0xA1;
			value.bindingMode = Bridge::SlideBindingMode::StableSlideId;
			value.sourceIdentity = "same-presentation.pptx";
			value.presentationName = "same-presentation.pptx";
			value.slideIds = std::move(ids);
			value.slideId = value.slideIds.front();
			value.totalPages = static_cast<std::uint32_t>(value.slideIds.size());
			value.pageIndex = 0;
			value.bindingRevision = 1;
			value.targetRevision = 2;
			value.sessionRevision = 1;
			return value;
		};
		const auto canvas = [&](uint8_t marker, std::optional<std::int32_t> slideId,
			float x, bool retained, bool endScreen = false)
		{
			draw3::uink::Draw3UInkCanvasSnapshot result;
			result.pageGuid = guid(marker);
			result.slideId = slideId;
			result.retained = retained;
			result.viewport = { 0.0f, 0.0f, 1.0f };
			result.extra = endScreen
				? draw3::uink::MakeInkeysEndScreenExtra(
					draw3::uink::Draw3UInkImportBindingMode::StableSlideId)
				: draw3::uink::MakeInkeysBindingExtra(
					draw3::uink::Draw3UInkImportBindingMode::StableSlideId);
			if (retained)
			{
				draw3::uink::UInkMessagePackValue key;
				key.value = std::string("inkeysPageState");
				draw3::uink::UInkMessagePackValue state;
				state.value = std::string("retained");
				result.extra->emplace_back(std::move(key), std::move(state));
			}
			if (!endScreen)
			{
				draw3::uink::Draw3UInkStrokeSnapshot stroke;
				stroke.points = { { x, 10.0f, 3.0f },
					{ x + 8.0f, 18.0f, 3.0f } };
				result.strokes.push_back(std::move(stroke));
			}
			return result;
		};
		const auto snapshot = [&]()
		{
			draw3::uink::Draw3UInkExportSnapshot result;
			result.fileGuid = guid(0x40);
			result.workspaceGuid = guid(0x41);
			result.workspaceType = 2;
			result.activeCanvases.push_back(canvas(0x42, 101, 10.0f, false));
			result.activeCanvases.push_back(canvas(0xEE, std::nullopt, 0.0f,
				false, true));
			return result;
		};
		std::uint64_t rasterToken = 1;
		const auto allocate = [&]() { return rasterToken++; };
		const auto reducedTarget = target({ 101 });
		auto beforeDeletion = snapshot();
		beforeDeletion.activeCanvases.insert(beforeDeletion.activeCanvases.end() - 1,
			canvas(0x43, 102, 20.0f, false));
		auto deleted = MaterializePresentationSlot(beforeDeletion,
			reducedTarget, 7, allocate);
		check(deleted && deleted->document && deleted->retainedSlides.contains(102) &&
			deleted->retainedSlides.at(102).pageGuid == guid(0x43) &&
			deleted->retainedSlides.at(102).strokes.size() == 1 &&
			deleted->retainedSlides.at(102).strokes[0].points[0].x == 20.0f &&
			deleted->document->PageAt(1)->PageGuid().Bytes() == guid(0xEE).Bytes(),
			"deleted old active 102 becomes retained with ink; EndScreen stays separate");
		if (deleted && deleted->document)
		{
			auto saved = BuildPresentationSaveRequest(*deleted->document,
				deleted->pageRuntimeStates, deleted->currentPageIndex,
				reducedTarget, deleted->fileGuid, 8, 1.0f,
				deleted->retainedSlides);
			check(saved && saved->snapshot.retainedCanvases.size() == 1 &&
				saved->snapshot.retainedCanvases[0].pageGuid == guid(0x43) &&
				draw3::uink::HasInkeysPageStateExtra(
					saved->snapshot.retainedCanvases[0].extra, true),
				"post-delete production save preserves retained identity and marker");
		}

		auto beforeAddition = snapshot();
		beforeAddition.retainedCanvases.push_back(canvas(0x44, 102, 30.0f, true));
		const auto expandedTarget = target({ 101, 102 });
		auto added = MaterializePresentationSlot(beforeAddition,
			expandedTarget, 7, allocate);
		check(added && added->document &&
			added->document->PageAt(1)->PageGuid().Bytes() == guid(0x44).Bytes() &&
			added->pageRuntimeStates[1].history.LastVisibleItem().has_value() &&
			added->retainedSlides.empty() &&
			added->document->PageAt(2)->PageGuid().Bytes() == guid(0xEE).Bytes(),
			"reappearing 102 restores original active page and is not retained twice");
		if (added && added->document)
		{
			auto saved = BuildPresentationSaveRequest(*added->document,
				added->pageRuntimeStates, added->currentPageIndex,
				expandedTarget, added->fileGuid, 8, 1.0f,
				added->retainedSlides);
			check(saved && saved->snapshot.activeCanvases.size() == 3 &&
				saved->snapshot.activeCanvases[1].pageGuid == guid(0x44) &&
				saved->snapshot.activeCanvases[1].strokes.size() == 1 &&
				saved->snapshot.activeCanvases[1].strokes[0].points[0].x == 30.0f &&
				saved->snapshot.retainedCanvases.empty(),
				"post-add production save has one 102 active canvas and no duplicate");
		}
		auto duplicateId = beforeDeletion;
		duplicateId.retainedCanvases.push_back(canvas(0x45, 102, 40.0f, true));
		check(!MaterializePresentationSlot(duplicateId,
			reducedTarget, 7, allocate),
			"duplicate active/retained SlideID is rejected");
		auto repeatedActive = snapshot();
		repeatedActive.activeCanvases.insert(repeatedActive.activeCanvases.end() - 1,
			canvas(0x46, 101, 40.0f, false));
		check(!MaterializePresentationSlot(repeatedActive,
			reducedTarget, 7, allocate),
			"duplicate active SlideID is rejected");
		auto duplicateGuid = beforeAddition;
		duplicateGuid.retainedCanvases.front().pageGuid = guid(0x42);
		check(!MaterializePresentationSlot(duplicateGuid,
			expandedTarget, 7, allocate),
			"duplicate active/retained page GUID is rejected");
		auto badSlideId = snapshot();
		badSlideId.activeCanvases.insert(badSlideId.activeCanvases.end() - 1,
			canvas(0x47, -1, 40.0f, false));
		check(!MaterializePresentationSlot(badSlideId,
			reducedTarget, 7, allocate),
			"nonpositive source SlideID is rejected");
		auto duplicateEnd = snapshot();
		duplicateEnd.activeCanvases.push_back(canvas(0x48, std::nullopt,
			0.0f, false, true));
		check(!MaterializePresentationSlot(duplicateEnd,
			reducedTarget, 7, allocate),
			"duplicate EndScreen is rejected");
		if (failures == 0)
			std::fputs("[Draw3PptTopologyLoad] PASS: two-way SlideID projection\n",
				stderr);
		return failures == 0 ? 0 : 1;
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
			metricsState_(DrawingControllerMetricsState::Prepare(metrics)), haptics_(haptics)
	{
		// 指标预备失败仅关闭诊断，不能影响正常输入/模型/画质或启动。
		metricsUnavailable_ = metrics && !metricsState_;
		if (metricsUnavailable_) metrics_ = nullptr;
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

	DrawingController::~DrawingController() = default;

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

	bool DrawingController::CompositeLayersToBackBuffer(RECT dirty, bool orderLiveOverStable)
	{
		const WindowSize size = window_.Size();
		dirty = ClampRectToCanvas(dirty, size.width, size.height); // 限制脏区，避免纹理复制越界。
		if (IsEmptyRect(dirty)) return true;
		renderer_.CopyResource(renderer_.backBufferTexture.Get(), renderer_.layerL2Texture.Get(), dirty); // L2 是已经稳定的底层画布。
		const OperatorLayerMergeMode mergeMode = orderLiveOverStable
			? OperatorLayerMergeMode::Ordered : OperatorLayerMergeMode::CoverageUnion;
		return renderer_.ApplyOperatorLayers(renderer_.backBufferRTV.Get(),
			renderer_.layerL1, renderer_.layerL0, dirty, mergeMode);
	}

	bool DrawingController::PresentFrame(RECT dirty, bool presentFull)
	{
		const double presentStartMs = GetQpcTimeMilliseconds();
		const bool externalFrame = metricsState_ && !metricsState_->runFrameActive;
		if (metricsState_)
		{
			if (externalFrame && !metricsState_->frameOpen) metricsState_->Begin(presentStartMs);
			metricsState_->renderAttempt = true;
			metricsState_->ObserveOutput(true, presentation_.RequestedOutputTarget(), presentation_.RequestedOutputRevision());
		}
		const bool succeeded = presentation_.Present(
			dirty, presentFull, currentContentRevision_);
		LARGE_INTEGER returnQpc = {};
		if (metricsState_) QueryPerformanceCounter(&returnQpc); // 真返回点，先于观察回调/ready/诊断。
		lastPresentDurationMs_ = GetQpcTimeMilliseconds() - presentStartMs;
		lastPresentSucceeded_ = succeeded;
		if (metricsState_)
		{
			const auto observation = presentation_.LastPresentObservation();
			const auto current = metricsState_->ObserveOutput(true,
				presentation_.RequestedOutputTarget(), presentation_.RequestedOutputRevision());
			const ContentMetricOutputIdentity observed{ succeeded, observation.outputTarget,
				observation.outputRevision, current.generation };
			const auto beforeMismatch = metricsState_->counters.outputMismatch;
			metricsState_->PresentReturned(true, succeeded, returnQpc.QuadPart, lastPresentDurationMs_, observed);
			metricsState_->frame.presentAttempted = true;
			metricsState_->frame.presentSucceeded = succeeded;
			metricsState_->frame.presentWallMs += lastPresentDurationMs_;
			if (metricsState_->counters.outputMismatch != beforeMismatch)
				metricsState_->Mark(RuntimeMetricsFrameReason::OutputMismatch);
			if (externalFrame) metricsState_->Finish(GetQpcTimeMilliseconds() - metricsState_->frame.frameStartMs);
		}
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
		if (metricsState_)
		{
			if (!metricsState_->runFrameActive) metricsState_->Begin(GetQpcTimeMilliseconds());
			metricsState_->Mark(RuntimeMetricsFrameReason::Page);
			metricsState_->InvalidateRaster();
		}
		const WindowSize size = window_.Size();
		const RECT fullCanvas = GetFullCanvasRect(size.width, size.height);
		renderer_.ClearRTV(renderer_.layerL2RTV.Get(), kTransparentLayerClearColor); // 内部画布始终保持真透明背景。
		renderer_.ClearOperatorLayer(renderer_.layerL1); // 清掉当前笔画已确认层。
		renderer_.ClearOperatorLayer(renderer_.layerL0); // 清掉当前帧实时层。
		renderer_.ClearAllLaserCoverage(); // Laser 是独立瞬态层，清屏时必须同步清理。
		renderer_.ResetLaserParticles();
		renderer_.ClearRTV(renderer_.backBufferRTV.Get(), kTransparentLayerClearColor); // backbuffer 也不写入 ULW 的命中测试底层。
		if (!CompositeLayersToBackBuffer(fullCanvas))
		{
			if (metricsState_)
			{
				metricsState_->Mark(RuntimeMetricsFrameReason::RasterFailed);
				if (!metricsState_->runFrameActive) metricsState_->Finish();
			}
			lastPresentSucceeded_ = false;
			if (!renderer_.device || FAILED(renderer_.device->GetDeviceRemovedReason()))
			{
				presentation_.MarkRuntimeFailure(E_FAIL);
				graphicsRecoveryPending_ = true;
			}
			window_.RequestFullPresent();
			return; // 合成未完成时不把透明空帧提交给窗口。
		}
		PresentFrame(fullCanvas, true);
		if (metricsState_)
		{
			metricsState_->initialL2Cleared = true;
			metricsState_->initialWidth = size.width;
			metricsState_->initialHeight = size.height;
		}
	}

	void DrawingController::PresentFullCanvas()
	{
		if (metricsState_)
		{
			if (!metricsState_->runFrameActive) metricsState_->Begin(GetQpcTimeMilliseconds());
			metricsState_->renderAttempt = true;
		}
		const WindowSize size = window_.Size();
		const RECT fullCanvas = GetFullCanvasRect(size.width, size.height);
		if (!CompositeLayersToBackBuffer(fullCanvas))
		{
			if (metricsState_)
			{
				metricsState_->Mark(RuntimeMetricsFrameReason::RasterFailed);
				if (!metricsState_->runFrameActive) metricsState_->Finish();
			}
			lastPresentSucceeded_ = false;
			if (!renderer_.device || FAILED(renderer_.device->GetDeviceRemovedReason()))
			{
				presentation_.MarkRuntimeFailure(E_FAIL);
				graphicsRecoveryPending_ = true;
			}
			window_.RequestFullPresent();
			return;
		}
		PresentFrame(fullCanvas, true);
	}

	bool DrawingController::ProcessPendingResize(bool presentAfterResize)
	{
		WindowSize requestedSize = {};
		if (!window_.ConsumeResizeRequest(requestedSize)) return false; // 只有窗口过程投递过尺寸变化才处理。
		const WindowSize oldSize = window_.Size();
		if (requestedSize.width <= 0 || requestedSize.height <= 0 ||
			(requestedSize.width == oldSize.width && requestedSize.height == oldSize.height)) return false;
		if (metricsState_)
		{
			metricsState_->Mark(RuntimeMetricsFrameReason::Resize);
			metricsState_->InvalidateRaster();
		}

		if (!renderer_.Resize(presentation_.SwapChain(), requestedSize.width, requestedSize.height)) // 先重建所有尺寸相关 D3D 资源。
		{
			if (metricsState_) metricsState_->Mark(RuntimeMetricsFrameReason::RasterFailed);
			std::cout << "Failed to resize D3D resources to " << requestedSize.width << "x" << requestedSize.height << std::endl;
			presentation_.MarkRuntimeFailure();
			graphicsRecoveryPending_ = true;
			return false;
		}
		if (!presentation_.Resize(requestedSize.width, requestedSize.height)) // 再通知当前透明呈现器更新外部资源。
		{
			if (metricsState_) metricsState_->Mark(RuntimeMetricsFrameReason::RasterFailed);
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
		struct MetricsRunExit
		{
			DrawingControllerMetricsState* state;
			~MetricsRunExit()
			{
				if (!state) return;
				state->Finish();
				state->runFrameActive = false;
				state->EndContacts(ContentMetricInvalidationReason::Stopped);
			}
		} metricsRunExit{ metricsState_.get() };
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
		PresentationParkedSlots presentationSlots;
		std::uint64_t nextPresentationSlotGeneration = 1;
		std::optional<Bridge::PresentationKey> activePresentationKey;
		std::optional<Bridge::PresentationTarget> activePresentationTarget;
		std::map<std::int32_t, draw3::uink::Draw3UInkCanvasSnapshot> activeRetainedSlides;
		std::optional<draw3::uink::UInkGuid> activePresentationFileGuid;
		std::uint64_t activePresentationMutationRevision = 0;
		std::uint64_t activePresentationQueuedRevision = 0;
		std::uint64_t activePresentationCommittedRevision = 0;
		bool activePresentationPersistenceInitialized = false;
		bool activePresentationLoadPending = false;
		std::uint64_t activePresentationSlotGeneration = 0;
		PresentationCurrentLoadRetry currentLoadRetry;
		const ActivePresentationSlotRefs activeDocumentSlot{
			document_, pageRuntimeStates, activeRetainedSlides, currentPageIndex_,
			activePresentationTarget, activePresentationFileGuid,
			activePresentationMutationRevision, activePresentationQueuedRevision,
			activePresentationCommittedRevision,
			activePresentationPersistenceInitialized,
			activePresentationLoadPending, activePresentationSlotGeneration };
		const auto presentationLoadUnresolved = [&]() noexcept
		{
			return PresentationLoadUnresolved(activeWorkspace,
				activePresentationPersistenceInitialized,
				activePresentationMutationRevision);
		};
		const auto currentLoadRetryWait = [&]() noexcept -> std::optional<double>
		{
			if (activeWorkspace != Bridge::Workspace::Presentation ||
				!activePresentationTarget || !observer_.presentationLoadRequested ||
				activePresentationMutationRevision != 0 ||
				activePresentationQueuedRevision != 0)
				return std::nullopt;
			return currentLoadRetry.RemainingWaitMilliseconds(
				*activePresentationTarget, activePresentationSlotGeneration,
				GetTickCount64());
		};
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
			const DesktopAutoSaveSource active{ document_ ? &*document_ : nullptr,
				&pageRuntimeStates, currentPageIndex_ };
			const DesktopAutoSaveSource parked{
				desktopSlot.document ? &*desktopSlot.document : nullptr,
				&desktopSlot.pageRuntimeStates, desktopSlot.currentPageIndex };
			return CaptureDesktopAutoSaveForScene(activeWorkspace, trigger,
				active, parked, desktopAutoSavePolicy, window_.AutoSaveEnabled(),
				configuration_.dpiScale, observer_, acceptedFileGuid);
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
		auto metricsSignature = [&](int width, int height) noexcept -> ContentMetricRasterSignature
		{
			if (!metricsState_ || !document_ || currentPageIndex_ >= pageRuntimeStates.size()) return {};
			metricsState_->ObserveCanvas(*document_, currentPageIndex_, rasterPipelineGeneration, false);
			return metricsState_->Signature(*document_, currentPageIndex_, pageRuntimeStates[currentPageIndex_], width, height);
		};
		auto metricsScene = [&](bool newScene = false) noexcept
		{
			if (!metricsState_ || !document_) return;
			metricsState_->ObserveCanvas(*document_, currentPageIndex_, rasterPipelineGeneration, newScene);
		};
		metricsScene();
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
			const bool activeNow = HasPhysicalContact(active) || input_.HasQuarantinedContacts();
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
		bool pendingLaserBakeFailed = false;
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
				if (metricsState_) metricsState_->Invalidate(MetricKey(runtime->handle), ContentMetricInvalidationReason::Cancelled);
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
				if (IgnoreAdditionalLaserTouch(input_, handle, batchTool, deviceType,
					hasActiveLaserTouchContact,
					laserMultiTouchDrawingEnabled_.load(std::memory_order_acquire)))
					return false; // 关闭多指时忽略后续 Touch，保留第一根手指的完整生命周期。
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
				if (metricsState_)
				{
					if (!metricsState_->frameOpen) metricsState_->Begin(GetQpcTimeMilliseconds());
					metricsScene();
					metricsState_->renderAttempt = true;
					metricsState_->Register(MetricKey(handle), down, deviceType, tool);
					metricsState_->NoteConsumed(MetricKey(handle), down.sequence);
				}
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
					const size_t metricsRealBefore = metricsState_ ? reconnectRuntime->stroke.realPoints.size() : 0;
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
						if (metricsState_)
						{
							// 真接管成功才终结旧A；B始终保留自己的Down/QPC，与旁挂Stored C无关。
							metricsState_->Invalidate(MetricKey(reconnectRuntime->handle), ContentMetricInvalidationReason::ReconnectSuperseded);
							metricsState_->Adopt(MetricKey(handle), down, ContentMetricAdoptionKind::Model,
								reconnectRuntime->stroke.realPoints.size() > metricsRealBefore);
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
					if (metricsState_) metricsState_->Invalidate(MetricKey(handle), ContentMetricInvalidationReason::InitRejected);
					RejectStrokeInitialization(input_, handle, down);
					return false;
				}
				runtime->handle = handle;
				runtime->ownerWorkspaceGuid = {};
				runtime->ownerPageGuid = {};
				runtime->ownerPageIndex = currentPageIndex_;
				runtime->cpuCommitAttempted = false;
				runtime->touchGestureKey = deviceType == InputDeviceType::Touch
					? CanvasTouchKey(handle) : 0;
				if (document_)
				{
					const InkPage* page = document_->PageAt(currentPageIndex_);
					const InkCanvas* canvas = page
						? page->FindCanvas(kDefaultDeviceKey) : nullptr;
					runtime->viewport = canvas ? canvas->Viewport() : InkViewport{};
					if (canvas)
					{
						runtime->ownerWorkspaceGuid = document_->WorkspaceGuid();
						runtime->ownerPageGuid = page->PageGuid();
					}
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
					if (metricsState_) metricsState_->Invalidate(MetricKey(handle), ContentMetricInvalidationReason::InitRejected);
					RejectStrokeInitialization(input_, handle, down);
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
					if (metricsState_) metricsState_->Invalidate(MetricKey(handle), ContentMetricInvalidationReason::InitRejected);
					RejectStrokeInitialization(input_, handle, down);
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
				if (metricsState_)
					metricsState_->Adopt(MetricKey(handle), down,
						runtime->shape.active && runtime->shape.hasModeledEndpoint ? ContentMetricAdoptionKind::Shape :
						!runtime->stroke.realPoints.empty() ? ContentMetricAdoptionKind::Model : ContentMetricAdoptionKind::RawDown, true);
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
						const bool baked = BakeLaserStrokeLayers(laserStrokeLayers, renderer_,
							configuration_.dpiScale, laserSize.width, laserSize.height,
							laserStableBounds, pendingLaserBakeDirty,
							laserCoverageMode);
						if (baked)
						{
							laserLiveBounds = {};
							laserCoverageMode = LaserCoverageMode::Inactive;
						}
						else
					{
							// 旧批次失败后仍接受新 Down；全部 CPU 层按原顺序留待重绘。
							pendingLaserBakeFailed = true;
							laserCoverageMode = LaserCoverageMode::FullRedraw;
							renderer_.ClearLaserIncrementalCoverage();
						}
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

		uint64_t observedAdmissionRevision = input_.AdmissionRevision();


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
				if (metricsState_ && snapshot.phase == ContactPhase::Up)
					metricsState_->Adopt(MetricKey(runtime.handle), snapshot, ContentMetricAdoptionKind::RawTerminal, true);
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
				const auto metricsGeometryBefore = metricsState_ ? CaptureMetricGeometry(runtime) : MetricGeometrySnapshot{};
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
					if (metricsState_ && !cancelled)
						metricsState_->Adopt(MetricKey(runtime.handle), snapshot,
							runtime.shape.active ? ContentMetricAdoptionKind::Shape : ContentMetricAdoptionKind::Model,
							MetricGeometryChanged(metricsGeometryBefore, runtime));
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
					if (metricsState_ && !cancelled)
						metricsState_->Adopt(MetricKey(runtime.handle), snapshot, ContentMetricAdoptionKind::Shape, true);
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
				if (metricsState_)
				{
					metricsState_->renderAttempt = true;
					metricsState_->NoteConsumed(MetricKey(runtime.handle), snapshot.sequence);
					if (snapshot.phase == ContactPhase::Up || snapshot.phase == ContactPhase::Cancelled)
					{
						++metricsState_->frame.terminalCount;
						metricsState_->Mark(RuntimeMetricsFrameReason::Terminal);
					}
					if (snapshot.phase == ContactPhase::Cancelled)
						metricsState_->Invalidate(MetricKey(runtime.handle), ContentMetricInvalidationReason::Cancelled);
				}
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
				const auto metricsGeometryBefore = metricsState_ ? CaptureMetricGeometry(runtime) : MetricGeometrySnapshot{};
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
					if (metricsState_)
						metricsState_->Adopt(MetricKey(runtime.handle), snapshot,
							runtime.shape.active ? ContentMetricAdoptionKind::Shape : ContentMetricAdoptionKind::Model,
							MetricGeometryChanged(metricsGeometryBefore, runtime));
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
					if (metricsState_ && snapshot.phase == ContactPhase::Up)
						metricsState_->Adopt(MetricKey(runtime.handle), snapshot, ContentMetricAdoptionKind::Shape, true);
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

		bool hapticContinuousActive = false;
		auto sealPresentationContacts = [&]()
		{
			const uint64_t revision = input_.AdmissionRevision();
			if (revision == observedAdmissionRevision) return;
			observedAdmissionRevision = revision;
			if (!input_.AdmissionBlocked() && active.empty() && gestureContacts.empty()) return;
			// 页边界只收尾已经被绘制线程接受的最后位置，不消费边界后的 Move。
			// 持久笔迹继续走同一 Stored/L2/保存事务；物理路由在提交后隔离到 Up。
			for (RuntimeStroke* runtime : active)
			{
				if (!runtime || runtime->ended) continue;
				ContactSnapshot terminal = runtime->awaitingReconnect
					? runtime->deferredUpSnapshot : runtime->lastInputSnapshot;
				if (metricsState_) metricsState_->Invalidate(MetricKey(runtime->handle), ContentMetricInvalidationReason::SceneSuperseded);
				terminal.phase = ContactPhase::Up;
				completeModelUp(*runtime, terminal, runtime->tool == DrawingTool::Laser);
				finishMouseSpeedEraser(*runtime);
			}
			for (const auto& contact : gestureContacts)
				input_.DiscardUntilTerminal(contact.handle);
			gestureContacts.clear();
			touchGesture.Reset();
			panMotion = {};
			panCentroidValid = false;
			previousPanContactCount = 0;
			window_.SetTouchPanActive(false);
			leftMouseSpeedEraser.CancelVisual();
			rightMouseSpeedEraser.CancelVisual();
			penEraserHoverLane.Invalidate();
			invertedPenEraserHoverLane.Invalidate();
			if (haptics_) haptics_->StopFeedback();
			hapticContinuousActive = false;
			renderer_.ClearAllLaserCoverage();
			renderer_.ResetLaserParticles();
			laserLifecycle = {};
			laserOpacity = 0.0f;
			laserStableBounds = {};
			laserLiveBounds = {};
			laserStrokeLayers.clear();
			laserCoverageMode = LaserCoverageMode::Inactive;
			laserParticleDirtyTracker.Clear();
			window_.RequestFullPresent();
			if (activePresentationTarget) TracePptTiming("contacts_sealed",
				activePresentationTarget->sessionRevision,
				activePresentationTarget->targetRevision);
		};

			bool commandBoundaryPending = false;
			auto processCommand = [&](ContactRecord* record)
			{
				if (!record)
				{
					input_.AcknowledgeControlWake(); // 先清 pending，随后复查窗口的全部原子请求。
					if (observer_.controlWake)
						observer_.controlWake(observer_.context,
							input_.LastDequeuedControlWakeKind());
					if (window_.HasPendingCanvasCommand()) commandBoundaryPending = true;
					sealPresentationContacts();
					return;
				}
				const ContactHandle handle{ record, record->Generation() };
				sealPresentationContacts();
				if (!input_.ContactAdmitted(handle))
				{
					// 被闸门拒绝的 Down 不能在重开后补画；保留其路由直至真实终态。
					input_.DiscardUntilTerminal(handle);
					return;
				}
				if (activeWorkspace == Bridge::Workspace::Whiteboard &&
					window_.SelectionMode())
				{
					// 白板“拖动”暂不启用平移，也不能让主 Drawpad 继续落笔。
					input_.DiscardUntilTerminal(handle);
					return;
				}
				if (Bridge::PresentationInputSuppressed(
					activeWorkspace, activePresentationLoadPending ||
					presentationLoadUnresolved()))
				{
					// 磁盘恢复完成前拒绝新输入，避免旧文件覆盖刚落下的墨迹。
					input_.DiscardUntilTerminal(handle);
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
			auto drainIngressBatch = [&]()
			{
				DrainIngressBatch(input_, window_, commandBoundaryPending,
					processCommandAndReconcile);
			};
			auto tryConsumeOneIngress = [&](ContactRecord*& record)
			{
				if (commandBoundaryPending || !input_.TryDequeue(record)) return false;
				processCommandAndReconcile(record);
				return true;
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
						if (metricsState_) metricsState_->InvalidateRaster();
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
				if(!r)for(const auto* candidate:active)
					if(candidate && !candidate->ended){r=candidate;break;}
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
					d.requestedDeviceMode=cfg.mode;d.touchProfileSource=cfg.touchProfileSource;
					d.touchProfileWeight=cfg.touchProfileWeight;d.touchSurfaceLongEdgeMm=cfg.touchSurfaceLongEdgeMm;
					d.motionSource=cfg.motionSource;d.motionUnit=cfg.motionUnit;d.sizes=cfg.sizes;
					d.fineToStandardSpeed=cfg.fineToStandardSpeed;d.sweepEnterSpeed=cfg.sweepEnterSpeed;
					d.sweepExitSpeed=cfg.sweepExitSpeed;d.largeTargetSpeed=cfg.largeTargetSpeed;d.sweepGain=cfg.sweepGain;
					d.evidenceStartSeconds=cfg.evidenceStartSeconds;d.evidenceFullSeconds=cfg.evidenceFullSeconds;
					d.evidenceDecaySeconds=cfg.evidenceDecaySeconds;d.growthTauSeconds=cfg.growthTauSeconds;
					d.largeGrowthTauSeconds=cfg.largeGrowthTauSeconds;
					d.maximumLogGrowthPerSecond=cfg.maximumLogGrowthPerSecond;
					d.largeLogGrowthPerSecond=cfg.largeLogGrowthPerSecond;
					d.entry=cfg.inputEntry;d.formalPenResponse=cfg.formalPenResponse;d.developmentResponseOverride=cfg.developmentResponseOverride;
					d.inputSource=cfg.inputSource;d.response=cfg.response;d.inputMapped=cfg.inputMapped;
					d.monitor=cfg.display.monitor;d.displayGeneration=cfg.display.generation;d.displayRevision=cfg.display.revision;
					d.rhoMmPerDip=cfg.rhoMmPerDip;d.penBeta=cfg.penBeta;d.heuristicGain=cfg.heuristicGain;
					d.dpiX=96/cfg.display.dipPerPixelX;d.dpiY=96/cfg.display.dipPerPixelY;
					d.effectiveDiameterDip=controller->DiameterDip();
					d.targetDiameterDip=controller->TargetDiameterDip();d.touchUnlocked=controller->TouchUnlocked();
					d.evidenceCapDiameterDip=controller->SweepEvidenceCapDiameterDip();
					d.needsAnimation=controller->NeedsAnimation(mouseVisualSeconds);
					d.contactArea=controller->AreaDiagnostics(mouseVisualSeconds);
					d.fine=controller->FineDiagnostics();
					d.follow=controller->FollowStateDiagnostics();
					d.dipPerPixelX=cfg.display.dipPerPixelX;d.dipPerPixelY=cfg.display.dipPerPixelY;
					d.motionPerPixelX=cfg.motionPerPixelX;d.motionPerPixelY=cfg.motionPerPixelY;
					d.pixelWidth=cfg.display.pixelWidth;d.pixelHeight=cfg.display.pixelHeight;
					d.manualWidthCm=cfg.display.development.calibration.widthCm;
					d.manualHeightCm=cfg.display.development.calibration.heightCm;
					d.speed=controller->Speed();d.sweepSpeed=controller->SweepSpeed();
					d.evidenceSeconds=controller->SweepEvidenceSeconds();
					d.sweeping=controller->Sweeping();d.qualified=controller->SweepQualified();d.limited=controller->TargetLimited();
					d.idleSeconds=controller->SecondsSinceMovement(mouseVisualSeconds);
				}
				if(r)
				{
					d.inputContact=!r->ended && !r->awaitingReconnect;
					d.inputType=static_cast<uint32_t>(r->metricDeviceType);
					if(!controller || d.inputSource.kind==SpeedEraser::SourceKind::Unknown)
						d.inputSource=r->lastInputSnapshot.source;
					if(r->handle.record)
					{d.contactId=r->handle.record->ContactId();d.contactGeneration=r->handle.generation;}
					if(d.inputContact)
					{
						d.inputPositionValid=true;
						d.inputCanvasXpx=r->lastModelSnapshot.position.x;
						d.inputCanvasYpx=r->lastModelSnapshot.position.y;
					}
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
					d.requestedDeviceMode=cfg.mode;d.touchProfileSource=cfg.touchProfileSource;
					d.sizes=cfg.sizes;
					d.nextRadiusPx=r->eraserSize.effectiveDiameterPx*0.5f;
					d.dipPerPixelX=cfg.display.dipPerPixelX;d.dipPerPixelY=cfg.display.dipPerPixelY;
					d.dpiX=96/cfg.display.dipPerPixelX;d.dpiY=96/cfg.display.dipPerPixelY;
					d.effectiveDiameterDip=r->eraserSize.effectiveDiameterPx*std::sqrt(cfg.display.dipPerPixelX*cfg.display.dipPerPixelY);
					d.targetDiameterDip=d.effectiveDiameterDip;
					d.formalPenResponse=cfg.formalPenResponse;d.developmentResponseOverride=cfg.developmentResponseOverride;
				}
				d.frameSeconds=mouseVisualSeconds;
				if(!r && d.preview)
				{
					const auto& sample=primaryUsesPen?penSample:mouseSample;
					if(sample.valid)
					{d.inputPositionValid=true;d.inputCanvasXpx=sample.x;d.inputCanvasYpx=sample.y;}
				}
				if(r && !r->ended && !r->awaitingReconnect && r->metricDeviceType==InputDeviceType::Touch &&
					r->tool==DrawingTool::Eraser)
				{
					// 诊断必须取当前 contact 的最终 Touch 光标，不能读取列表首项的鼠标/其他手指。
					auto appearance=eraserAppearance;
					ApplySpeedEraserCursorDiameter(appearance,r->stroke.widthMode==StrokeWidthMode::SpeedEraser?
						RuntimeSpeedEraserContactDiameter(*r):r->stroke.widthEstimator.baseDiameter);
					const auto& position=r->lastModelSnapshot.position;
					const auto visual=MakeTouchEraserDrawingCursorVisual(position.x,position.y,appearance);
					d.cursorVisible=visual.visible;
					if(visual.visible){d.cursorCanvasXpx=visual.x;d.cursorCanvasYpx=visual.y;}
					d.cursorDiameterPx=visual.visible?visual.appearance.width:0;
				}
				// Touch 圆环追加在主光标之后；没有主光标时不能借它填主输入诊断。
				else if(((r && r==primaryRuntime) || (!r && d.preview)) && primaryCursorCount != 0)
				{
					const auto& visual=currentCursorVisuals.front();
					d.cursorVisible=visual.visible;
					if(visual.visible){d.cursorCanvasXpx=visual.x;d.cursorCanvasYpx=visual.y;}
					d.cursorDiameterPx=visual.visible?visual.appearance.width:0;
				}
				else d.cursorDiameterPx=0;
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
				if (metricsState_ && metricsState_->wholeL2Clear && pageIndex == currentPageIndex_)
				{
					metricsState_->CompleteFullReplay(metricsSignature(width, height), true);
					metricsState_->wholeL2Clear = false;
				}
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
			result = historyGpuCache.RestoreComposition(request);
			if (metricsState_ && metricsState_->wholeL2Clear && pageIndex == currentPageIndex_)
			{
				metricsState_->CompleteFullReplay(metricsSignature(width, height), result.path != CompositionRestorePath::Failed);
				metricsState_->wholeL2Clear = false;
			}
			return result;
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
		auto createBlankSlot = [&](DrawingDocumentSlot& slot,
			std::size_t pageCount) -> bool
		{
			return TryCreateBlankDocumentSlot(slot, pageCount,
				allocateRasterStateToken);
		};

		auto swapActiveDocument = [&](DrawingDocumentSlot& slot) noexcept
		{
			SwapActiveDocumentSlot(activeDocumentSlot, slot);
		};

		auto parkedActiveSlot = [&]() -> DrawingDocumentSlot*
		{
			if (activeWorkspace == Bridge::Workspace::Desktop) return &desktopSlot;
			if (activeWorkspace == Bridge::Workspace::Whiteboard) return &whiteboardSlot;
			if (activePresentationKey && activePresentationTarget)
			{
				auto found = presentationSlots.find(LaneFor(*activePresentationTarget));
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
			if (metricsState_)
			{
				metricsScene(true);
				metricsState_->Mark(RuntimeMetricsFrameReason::Page);
				metricsState_->InvalidateRaster();
			}
			frameDirty = GetFullCanvasRect(width, height);
			renderer_.ClearRTV(renderer_.layerL2RTV.Get(), kTransparentLayerClearColor);
			if (metricsState_) metricsState_->wholeL2Clear = true;
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
			if (metricsState_ && currentPageIndex_ < pageRuntimeStates.size() &&
				pageRuntimeStates[currentPageIndex_].history.Items().empty())
			{
				metricsState_->CompleteFullReplay(metricsSignature(width, height), true);
				metricsState_->wholeL2Clear = false;
			}
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

		auto submitPresentationSlot = [&](const InkCanvasCollection& document,
			const std::vector<CanvasPageRuntimeState>& runtimes,
			std::size_t currentPage, const Bridge::PresentationTarget& target,
			std::optional<draw3::uink::UInkGuid>& fileGuid,
			std::uint64_t mutationRevision, std::uint64_t& queuedRevision,
			std::uint64_t slotGeneration,
			const RetainedPresentationSlides& retainedSlides,
			std::optional<draw3::uink::UInkGuid> clearPageGuid = std::nullopt) -> bool
		{
			if (!observer_.presentationSaveRequested || slotGeneration == 0 ||
				!ShouldQueuePresentationSave(mutationRevision, queuedRevision)) return false;
			auto request = BuildPresentationSaveRequest(document, runtimes,
				currentPage, target, fileGuid, mutationRevision,
				configuration_.dpiScale, retainedSlides, clearPageGuid,
				slotGeneration);
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
					activePresentationQueuedRevision,
					activePresentationSlotGeneration, activeRetainedSlides);
		};

		auto captureExitAutoSave = [&](bool fatal, const char* reason)
		{
			size_t eligibleCount = 0;
			size_t queuedCount = 0;
			const auto submit = [&](bool eligible, const char* source,
				auto&& capture)
			{
				if (eligible) ++eligibleCount;
				bool queued = false;
				if (fatal)
				{
					try { queued = capture(); }
					catch (...) { queued = false; }
				}
				else queued = capture();
				if (queued) ++queuedCount;
				if (eligible && !queued)
					std::fprintf(stderr,
						"[Draw3.AutoSave] action=exit_snapshot reason=%s source=%s result=not_queued\n",
						reason, source);
			};

			const DesktopAutoSaveSource activeDesktop{
				document_ ? &*document_ : nullptr, &pageRuntimeStates, currentPageIndex_ };
			const DesktopAutoSaveSource parkedDesktop{
				desktopSlot.document ? &*desktopSlot.document : nullptr,
				&desktopSlot.pageRuntimeStates, desktopSlot.currentPageIndex };
			const DesktopAutoSaveSource* desktop = SelectDesktopAutoSaveSource(
				activeWorkspace, DesktopAutoSaveTrigger::Exit,
				activeDesktop, parkedDesktop);
			const bool desktopEligible = desktop && desktop->document &&
				desktop->pageRuntimeStates &&
				desktop->pageIndex < desktop->pageRuntimeStates->size() &&
				desktopAutoSavePolicy.ShouldCapture(Bridge::Workspace::Desktop,
					window_.AutoSaveEnabled(),
					(*desktop->pageRuntimeStates)[desktop->pageIndex].history
						.LastVisibleItem().has_value());
			submit(desktopEligible, "desktop", [&]
				{ return captureDesktopAutoSave(DesktopAutoSaveTrigger::Exit); });

			const bool activePresentationEligible =
				activeWorkspace == Bridge::Workspace::Presentation &&
				activePresentationKey && activePresentationTarget && document_ &&
				ShouldQueuePresentationSave(activePresentationMutationRevision,
					activePresentationQueuedRevision);
			submit(activePresentationEligible, "presentation_active",
				[&] { return capturePresentationAutoSave(); });
			for (auto& [key, slot] : presentationSlots)
			{
				(void)key;
				if (!slot.document || !slot.presentationTarget) continue;
				const bool eligible = ShouldQueuePresentationSave(
					slot.mutationRevision, slot.queuedRevision);
				submit(eligible, "presentation_parked", [&]
					{
						return submitPresentationSlot(*slot.document,
							slot.pageRuntimeStates, slot.currentPageIndex,
							*slot.presentationTarget, slot.fileGuid,
							slot.mutationRevision, slot.queuedRevision,
							slot.slotGeneration,
							RetainedSlidesForSave(activeRetainedSlides, &slot));
					});
			}
			if (fatal)
				std::fprintf(stderr,
					"[Draw3.AutoSave] action=fatal_exit_snapshot reason=%s eligible=%zu queued=%zu durable=pending_worker\n",
					reason, eligibleCount, queuedCount);
		};

		bool fatalCaptureAttempted = false;
		auto captureGraphicsFatalExit = [&](const char* reason)
		{
			if (fatalCaptureAttempted) return;
			fatalCaptureAttempted = true;
			StopFatalInputConsumer(input_);
			if (metricsState_)
			{
				metricsState_->EndContacts(ContentMetricInvalidationReason::Fatal);
				metricsState_->InvalidateRaster();
			}
			size_t eligible = 0;
			size_t committed = 0;
			size_t failed = 0;
			for (RuntimeStroke* runtime : active)
			{
				if (!runtime || !runtime->inUse || !runtime->handle) continue;
				ContactSnapshot cutoffSnapshot;
				// 封口前只探测一次 producer 已到达的 Cancel；较晚 Move 不改变已消费截止点。
				const bool cancelledAtCutoff = input_.TryReadSnapshot(
					runtime->handle, cutoffSnapshot) &&
					cutoffSnapshot.phase == ContactPhase::Cancelled;
				if (cancelledAtCutoff)
					std::fprintf(stderr,
						"[Draw3.AutoSave] action=fatal_cpu_seal result=skipped reason=producer_cancel contact=%u\n",
						runtime->handle.record->ContactId());
				if (!cancelledAtCutoff && !runtime->cancelled &&
					runtime->tool != DrawingTool::Laser &&
					!runtime->cpuCommitAttempted && document_ &&
					activeWorkspace != Bridge::Workspace::Whiteboard)
				{
					++eligible;
					try
					{
						const double tipSeconds = runtime->tool == DrawingTool::Pen
							? ResolveLiveTipTaperDurationSeconds(runtime->stroke.widthMode,
								configuration_.liveTipDurationSeconds) : 0.0;
						const char* commitReason = "unknown";
						const auto result = CommitRuntimeStoredStrokeCpu(*runtime, *document_,
							pageRuntimeStates, currentPageIndex_, tipSeconds,
							StoredStrokeCommitMode::Fatal, allocateRasterStateToken,
							&commitReason);
						if (result)
						{
							markPresentationMutation();
							runtime->ended = true;
							runtime->awaitingReconnect = false;
							++committed;
						}
						else
						{
							++failed;
							std::fprintf(stderr,
								"[Draw3.AutoSave] action=fatal_cpu_seal result=failed reason=%s contact=%u generation=%llu\n",
								commitReason, runtime->handle.record->ContactId(),
								static_cast<unsigned long long>(runtime->handle.generation));
						}
					}
					catch (const std::bad_alloc&)
					{
						++failed;
						std::fputs("[Draw3.AutoSave] action=fatal_cpu_seal result=failed reason=allocation\n", stderr);
					}
					catch (const std::exception& error)
					{
						++failed;
						std::fprintf(stderr,
							"[Draw3.AutoSave] action=fatal_cpu_seal result=failed reason=exception detail=%s\n",
							 error.what());
					}
				}
			}
			std::fprintf(stderr,
				"[Draw3.AutoSave] action=fatal_cpu_seal reason=%s eligible=%zu committed=%zu failed=%zu durable=pending_worker\n",
				reason, eligible, committed, failed);
			captureExitAutoSave(true, reason);
			// 此路径随后 break 或重抛；不等 Closing producer，Host Stop 负责 RTS 终态。
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

		auto materializePresentationSlot = [&](
			const draw3::uink::Draw3UInkExportSnapshot& snapshot,
			const Bridge::PresentationTarget& target,
			std::uint64_t committedRevision)
		{
			return MaterializePresentationSlot(snapshot, target,
				committedRevision, allocateRasterStateToken);
		};

		auto RebindStablePresentationTopology = [&](const Bridge::PresentationTarget& next)
			-> bool
		{
			if (activeWorkspace != Bridge::Workspace::Presentation ||
				!activePresentationTarget ||
				activePresentationTarget->bindingMode != Bridge::SlideBindingMode::StableSlideId ||
				next.bindingMode != Bridge::SlideBindingMode::StableSlideId || !document_)
				return false;
			auto captured = BuildPresentationSaveRequest(*document_, pageRuntimeStates,
				currentPageIndex_, *activePresentationTarget, activePresentationFileGuid,
				activePresentationMutationRevision, configuration_.dpiScale,
				activeRetainedSlides);
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
			// EndScreen 不属于真实 SlideID 拓扑，重排和增删页只移动其内部槽位。
			const auto previousEnd = std::find_if(
				captured->snapshot.activeCanvases.begin(),
				captured->snapshot.activeCanvases.end(), [](const auto& canvas)
				{
					return draw3::uink::InkeysPageKind(canvas.extra) ==
						draw3::uink::UInkInkeysPageKind::EndScreen;
				});
			if (previousEnd == captured->snapshot.activeCanvases.end()) return false;
			remapped.activeCanvases.push_back(*previousEnd);
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
			if (metricsState_) metricsScene(true);
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
			const auto metricsBeforeUndo = metricsState_ ? metricsSignature(size.width, size.height) : ContentMetricRasterSignature{};
			if (metricsState_) metricsState_->BeginLocalWrite(metricsBeforeUndo);
			const bool metricsHotAttempt = metricsState_ ? metricsState_->BeginRasterAttempt() : false;
			const HotPreimageRestoreResult hotRestore = historyGpuCache.RestorePreimage(
				canvasIdentity, *itemId, rasterKey, runtime.rasterState,
				viewport.x, viewport.y, size.width, size.height);
			if (metricsState_) metricsState_->RasterReturned(metricsHotAttempt, true,
				hotRestore.restored && !IsEmptyRect(hotRestore.dirty)); // 当前实现false仅前置/未命中返回，未到Copy；void Copy无失败回执。
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
					const bool metricsAttempt = metricsState_ ? metricsState_->BeginRasterAttempt() : false;
					const auto rollback = historyGpuCache.RestoreComposition(rollbackRequest);
					if (metricsState_) metricsState_->RasterReturned(metricsAttempt,
						rollback.path != CompositionRestorePath::Failed, !IsEmptyRect(rollback.dirty));
					return rollback;
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
				const bool metricsColdAttempt = metricsState_ ? metricsState_->BeginRasterAttempt() : false;
				const CompositionRestoreResult restored =
					historyGpuCache.RestoreComposition(request);
				// 当前恢复实现只在映射tile真正尝试后回报dirty；Empty/前置拒绝不冒充栅格失败。
				if (metricsState_) metricsState_->RasterReturned(metricsColdAttempt,
					restored.path != CompositionRestorePath::Failed, !IsEmptyRect(restored.dirty));
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
			if (metricsState_)
			{
				metricsState_->PruneStored(*canvas, runtime);
				metricsState_->CompleteLocalWrite(metricsBeforeUndo, metricsSignature(size.width, size.height), true);
				metricsState_->InvalidateLiveRasters();
				metricsState_->renderAttempt = true;
			}
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
				const bool metricsAttempt = metricsState_ ? metricsState_->BeginRasterAttempt() : false;
				const auto restored = historyGpuCache.RestoreComposition(request);
				if (metricsState_) metricsState_->RasterReturned(metricsAttempt,
					restored.path != CompositionRestorePath::Failed, !IsEmptyRect(restored.dirty));
				return restored;
			};

			const char* basePath = "trusted_l2";
			const auto metricsBeforeRedo = metricsState_ ? metricsSignature(size.width, size.height) : ContentMetricRasterSignature{};
			if (metricsState_) metricsState_->BeginLocalWrite(metricsBeforeRedo);
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

			const bool metricsDrawAttempt = metricsState_ ? metricsState_->BeginRasterAttempt() : false;
			renderer_.ClearOperatorLayer(renderer_.layerL1);
			renderer_.ClearOperatorLayer(renderer_.layerL0);
			const StoredStrokeRasterTarget target = {
				&renderer_.layerL1, viewport.x, viewport.y, size.width, size.height
			};
			const StoredStrokeRasterResult raster = DrawStoredStroke(
				strokes[item->strokeIndex], renderer_, target,
				redoRebuildPoints, redoHighlighterScratch);
			if (metricsState_) metricsState_->RasterReturned(metricsDrawAttempt, raster.succeeded);
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
				const bool metricsResolveAttempt = metricsState_ ? metricsState_->BeginRasterAttempt() : false;
				submitted = renderer_.ApplyOperatorLayers(renderer_.layerL2RTV.Get(),
					renderer_.layerL1, renderer_.layerL0, redoDirty);
				if (metricsState_) metricsState_->RasterReturned(metricsResolveAttempt, submitted);
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
			if (metricsState_)
			{
				metricsState_->PruneStored(*canvas, runtime);
				metricsState_->CompleteLocalWrite(metricsBeforeRedo, metricsSignature(size.width, size.height), true);
				metricsState_->InvalidateLiveRasters();
				metricsState_->renderAttempt = true;
			}
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
				if (metricsState_) metricsState_->frame.reasonFlags |= static_cast<uint32_t>(RuntimeMetricsFrameReason::Command);
				if (!CanvasCommandAllowedAfterExitBarrier(currentLoadRetry,
					command.type))
					continue; // 首次最终保存 ACK 后不再接受会写状态或再次保存的命令。
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
					const PresentationCompletionRoute route = RoutePresentationCompletion(
						completion, activeWorkspace, activePresentationKey,
						activePresentationTarget, activePresentationSlotGeneration,
						document_, presentationSlots);
					const bool completionIsActive = route.active;
					DrawingDocumentSlot* parked = route.parked;
					if (!completionIsActive && !parked) continue;
					if (completion.operation == PresentationPersistenceOperation::Load &&
						!(completionIsActive ? activePresentationLoadPending : parked->loadPending))
						continue;

					if (completion.operation == PresentationPersistenceOperation::Save)
					{
						const auto& currentFileGuid = completionIsActive
							? activePresentationFileGuid : parked->fileGuid;
						if (!PresentationSaveCompletionMatchesSlot(completion,
							currentFileGuid, completionIsActive
								? activePresentationQueuedRevision : parked->queuedRevision))
							continue;
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
							if (completion.status == PresentationPersistenceStatus::Committed &&
								PresentationTrackMatchesMode(completion.storageTrack,
									completion.target.bindingMode))
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
							if (completion.status == PresentationPersistenceStatus::Committed &&
								PresentationTrackMatchesMode(completion.storageTrack,
									completion.target.bindingMode))
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
							if (canEvictPresentationSlot(*parked) && route.parkedLane)
								presentationSlots.erase(*route.parkedLane);
						}
						continue;
					}

					if (completion.loadKind == PresentationLoadKind::PreviousInterval)
					{
						auto installInterval = [&](InkCanvasCollection* targetDocument,
							std::vector<CanvasPageRuntimeState>* runtimes,
							const std::optional<draw3::uink::UInkGuid>& currentFileGuid)
							-> std::optional<std::size_t>
						{
							if (!PresentationLoadedForLane(completion) ||
								!currentFileGuid || *currentFileGuid != *completion.fileGuid ||
								!completion.pageGuid ||
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
								document_ ? &*document_ : nullptr, &pageRuntimeStates,
								activePresentationFileGuid);
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
								&parked->pageRuntimeStates, parked->fileGuid);
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
									parked->mutationRevision, parked->queuedRevision,
									parked->slotGeneration,
									RetainedSlidesForSave(activeRetainedSlides, parked));
							}
						}
						continue;
					}

					const Bridge::PresentationTarget& latestTarget = completionIsActive &&
						activePresentationTarget ? *activePresentationTarget :
						parked && parked->presentationTarget ? *parked->presentationTarget :
						completion.target;
					std::optional<DrawingDocumentSlot> loaded;
					const bool loadedVerified = PresentationLoadedForLane(completion);
					const bool emptyVerified = PresentationEmptyLaneVerified(completion);
					if (loadedVerified)
						loaded = materializePresentationSlot(*completion.loadedSnapshot,
							latestTarget, completion.mutationRevision);

					if (completionIsActive)
					{
						activePresentationLoadPending = false;
						if (!emptyVerified && !loadedVerified)
						{
							activePresentationPersistenceInitialized = false;
							if (completion.status == PresentationPersistenceStatus::IoError &&
								activePresentationTarget)
								currentLoadRetry.OnIoError(*activePresentationTarget,
									activePresentationSlotGeneration, GetTickCount64());
							else
							{
								currentLoadRetry.OnTerminalFailure();
								std::fprintf(stderr,
									"[Draw3.Presentation] action=current_load result=manual_required status=%u generation=%llu\n",
									static_cast<unsigned>(completion.status),
									static_cast<unsigned long long>(activePresentationSlotGeneration));
							}
							continue;
						}
						if (loadedVerified)
						{
							const bool installed = InstallLoadedActivePresentationSlot(
								loaded, latestTarget, {
									document_, pageRuntimeStates, currentPageIndex_,
									activePresentationTarget, activeRetainedSlides,
									activePresentationFileGuid, activePresentationMutationRevision,
									activePresentationQueuedRevision,
									activePresentationCommittedRevision });
							if (!installed)
							{
								// 物化或目标复制失败时，不将空槽发布成已恢复。
								activePresentationPersistenceInitialized = false;
								currentLoadRetry.OnTerminalFailure();
								std::fprintf(stderr,
									"[Draw3.Presentation] action=current_load result=manual_required status=install_failed generation=%llu\n",
									static_cast<unsigned long long>(activePresentationSlotGeneration));
								continue;
							}
						}
						activePresentationPersistenceInitialized = true;
						currentLoadRetry.Cancel();
						restoreAfterDocumentSlotSwitch(frameDirty, particleSnapshot,
							forceFullPresent, width, height);
						if (activePresentationTarget)
							stageWorkspaceReady(&*activePresentationTarget);
					}
					else
					{
						parked->loadPending = false;
						if (!emptyVerified && !loadedVerified)
						{
							parked->persistenceInitialized = false;
							continue;
						}
						if (loadedVerified)
						{
							if (!loaded || parked->mutationRevision != 0)
							{
								parked->persistenceInitialized = false;
								continue;
							}
							loaded->slotGeneration = parked->slotGeneration;
							*parked = std::move(*loaded);
						}
						parked->persistenceInitialized = true;
						if (canEvictPresentationSlot(*parked) && route.parkedLane)
							presentationSlots.erase(*route.parkedLane);
					}
					continue;
				}
				if (command.type == CanvasCommandType::SetPresentationTarget)
				{
					if (!command.presentationTarget) continue;
					const Bridge::PresentationTarget& requestedTarget =
						*command.presentationTarget;
					if (activeWorkspace == Bridge::Workspace::Presentation &&
						activePresentationTarget && *activePresentationTarget == requestedTarget)
					{
						if (!activePresentationPersistenceInitialized &&
							!activePresentationLoadPending &&
							activePresentationMutationRevision == 0 &&
							activePresentationQueuedRevision == 0 &&
							observer_.presentationLoadRequested &&
							currentLoadRetry.AllowsLoad(window_.ExitRequested()))
						{
							currentLoadRetry.Cancel(); // 外部明确重发同一 target，允许安全重读。
							activePresentationLoadPending = SubmitCurrentPresentationLoad(
								requestedTarget, activePresentationSlotGeneration,
								[&](PresentationLoadRequest&& request)
								{
									return observer_.presentationLoadRequested(observer_.context,
										std::move(request));
								});
							if (!activePresentationLoadPending)
								currentLoadRetry.OnIoError(requestedTarget,
									activePresentationSlotGeneration, GetTickCount64());
						}
						continue;
					}
					const Bridge::PresentationTarget target = requestedTarget;
					if (target.key.IsZero() || !Bridge::ValidPresentationPage(target)) continue;
					// 同文稿翻页与跨文稿切换都先固定离开侧最新 revision。
					(void)capturePresentationAutoSave();
					pendingWorkspaceReady.reset();

					DrawingDocumentSlot* source = parkedActiveSlot();
					if (!source) continue;
					PresentationCpuSwitchResult cpuSwitch =
						SwitchPresentationCpuSlot(activeWorkspace, activePresentationKey,
							activeDocumentSlot, *source, isolatedPresentationSlot,
							presentationSlots, nextPresentationSlotGeneration,
							target, allocateRasterStateToken);
					if (!cpuSwitch.accepted) continue;
					if (!cpuSwitch.topologyConflict)
					{
						// parked/warm slot 先恢复其旧 target，再按有序 SlideID 重映射。
						const bool stableTopologyChange = activePresentationTarget &&
							StablePresentationTopologyChanged(*activePresentationTarget, target);
						if (stableTopologyChange && !RebindStablePresentationTopology(target))
						{
							std::fputs("[Draw3.Presentation] action=rebind result=failed\n", stderr);
							continue;
						}
						activePresentationTarget = std::move(cpuSwitch.preparedTarget);
					}
					if (!document_ || pageRuntimeStates.size() !=
						Bridge::PresentationDocumentPageCount(target) ||
						document_->Pages().size() != Bridge::PresentationDocumentPageCount(target))
					{
						std::fputs("[Draw3.Presentation] action=switch result=isolated reason=page_count\n",
							stderr);
						continue;
					}
					currentPageIndex_ = target.pageIndex;
					currentLoadRetry.Cancel(); // 页/场次/代次变化取消上一目标的计时请求。
					TracePptTiming("document_switched", target.sessionRevision, target.targetRevision);
					restoreAfterDocumentSlotSwitch(frameDirty, particleSnapshot,
						forceFullPresent, width, height);
					if (activePresentationTarget &&
						!activePresentationPersistenceInitialized &&
						!activePresentationLoadPending &&
						activePresentationMutationRevision == 0 &&
						activePresentationQueuedRevision == 0 &&
						observer_.presentationLoadRequested &&
						currentLoadRetry.AllowsLoad(window_.ExitRequested()))
					{
						activePresentationLoadPending = SubmitCurrentPresentationLoad(
							*activePresentationTarget, activePresentationSlotGeneration,
							[&](PresentationLoadRequest&& request)
							{
								return observer_.presentationLoadRequested(observer_.context,
									std::move(request));
							});
						if (!activePresentationLoadPending)
							currentLoadRetry.OnIoError(*activePresentationTarget,
								activePresentationSlotGeneration, GetTickCount64());
					}
					if (!activePresentationLoadPending &&
						activePresentationPersistenceInitialized)
						(void)capturePresentationAutoSave();
					if (!activePresentationLoadPending &&
						activePresentationPersistenceInitialized)
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
					const std::optional<PresentationLaneKey> previousPresentationLane =
						activePresentationKey && activePresentationTarget
						? std::optional<PresentationLaneKey>(LaneFor(*activePresentationTarget))
						: std::nullopt;
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
					if (previousPresentationLane && canEvictPresentationSlot(*source))
						presentationSlots.erase(*previousPresentationLane);
					activeWorkspace = target;
					currentLoadRetry.Cancel();
					activePresentationKey.reset();
					activePresentationTarget.reset();
					restoreAfterDocumentSlotSwitch(frameDirty, particleSnapshot,
						forceFullPresent, width, height);
					stageWorkspaceReady();
					continue;
				}
				if (Bridge::PresentationCanvasCommandSuppressed(activeWorkspace,
					activePresentationLoadPending || presentationLoadUnresolved(),
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
							activePresentationQueuedRevision,
							activePresentationSlotGeneration, activeRetainedSlides,
							preClear->pageGuid);
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
					// 回执只表示最终快照已提交给 worker；durable 结果仍由 worker 决定。
					ProcessPresentationExitBarrier(currentLoadRetry,
						[&] { captureExitAutoSave(false, "normal_exit"); },
						[&] { reportCommand(command.type); });
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
								load.slotGeneration = activePresentationSlotGeneration;
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
					if (metricsState_) metricsState_->InvalidateRaster();
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
		if (metricsState_ && metricsState_->initialL2Cleared)
		{
			const auto size = window_.Size();
			if (size.width == metricsState_->initialWidth && size.height == metricsState_->initialHeight &&
				pageRuntimeStates[currentPageIndex_].history.Items().empty())
				metricsState_->CompleteFullReplay(metricsSignature(size.width, size.height), true);
			metricsState_->initialL2Cleared = false;
		}
		bool appliedSelectionMode = window_.SelectionMode();
		bool auxiliaryCleanVerificationPending = appliedSelectionMode;
		unsigned consecutiveRasterFailures = 0;
		ULONGLONG lastRasterFailureLogTick = 0;
		bool activeLayerRebuildPending = false;
		try
		{
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
			if (metricsState_)
			{
				const uint32_t before = static_cast<uint32_t>(std::count_if(active.begin(), active.end(),
					[](const RuntimeStroke* runtime) { return runtime && !runtime->ended && !runtime->awaitingReconnect; }));
				metricsState_->Begin(frameStartMs, before);
				metricsState_->runFrameActive = true;
			}
			struct MetricsLoopExit
			{
				DrawingControllerMetricsState* state;
				const std::vector<RuntimeStroke*>& active;
				~MetricsLoopExit()
				{
					if (!state) return;
					state->frame.physicalAfter = static_cast<uint32_t>(std::count_if(active.begin(), active.end(),
						[](const RuntimeStroke* runtime) { return runtime && !runtime->ended && !runtime->awaitingReconnect; }));
					state->Finish();
					state->runFrameActive = false;
				}
			} metricsLoopExit{ metricsState_.get(), active };
			lastPresentDurationMs_ = 0.0;
			lastPresentSucceeded_ = false;
		bool rasterSubmissionFailed = false;
			bool forceFullPresent = outputTargetChanged || contentRevisionNeedsPresent;
			RECT viewportRecoveryDirty = {};
			if (graphicsRecoveryPending_)
			{
				if (metricsState_)
				{
					metricsState_->Mark(RuntimeMetricsFrameReason::Recovery);
					metricsState_->InvalidateRaster();
					metricsState_->InvalidateOutput();
				}
				const HRESULT failure = presentation_.LastFailure();
				graphicsRecoveryPending_ = false;
				// 先释放旧设备上的 GPU history，再由 presenter 在当前绘制线程重建设备或降级后端。
				historyGpuCache.Release();
				if (!presentation_.RecoverFromRuntimeFailure())
				{
					std::cout << "Failed to recover Draw3 graphics after HRESULT 0x" <<
						std::hex << static_cast<unsigned long>(failure) << std::dec << std::endl;
					// 绘制线程仍持有已完成文档；先交给原保存 worker，再退出失效设备。
					captureGraphicsFatalExit("presenter_recovery");
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
					captureGraphicsFatalExit("history_cache_rebuild");
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
				const LiveRasterSubmission rebuilt = RebuildActiveLayers(active, renderer_,
					recoveredSize.width, recoveredSize.height, shapePrimitiveScratch);
				rasterSubmissionFailed |= !rebuilt.succeeded;
				activeLayerRebuildPending = !rebuilt.succeeded;
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
				if (metricsState_) metricsState_->Mark(RuntimeMetricsFrameReason::Recovery);
				if (!presentation_.RefreshAfterCompositionChanged())
				{
					if (metricsState_) metricsState_->Mark(RuntimeMetricsFrameReason::RasterFailed);
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
				if (metricsState_) metricsState_->wholeL2Clear = true;
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
				const LiveRasterSubmission rebuilt = RebuildActiveLayers(active, renderer_,
					size.width, size.height, shapePrimitiveScratch);
				rasterSubmissionFailed |= !rebuilt.succeeded;
				activeLayerRebuildPending = !rebuilt.succeeded;
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
			if (activeLayerRebuildPending && !rasterSubmissionFailed)
			{
				// 失败帧可能已清空共享层；下一帧先从仍权威的 CPU 笔迹整体重放。
				const WindowSize rebuildSize = window_.Size();
				const LiveRasterSubmission rebuilt = RebuildActiveLayers(active, renderer_,
					rebuildSize.width, rebuildSize.height, shapePrimitiveScratch);
				activeLayerRebuildPending = !rebuilt.succeeded;
				rasterSubmissionFailed |= !rebuilt.succeeded;
				forceFullPresent = true;
			}
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
				drainIngressBatch();
			}
			sealPresentationContacts();
			if (window_.ConsumeFullPresentRequest()) forceFullPresent = true;
			LARGE_INTEGER navigationQpc = {};
			QueryPerformanceCounter(&navigationQpc);
			updateCanvasNavigation(navigationQpc.QuadPart);
			if (viewportRefreshPending || viewportRecoveryPending)
			{
				const WindowSize size = window_.Size();
				if (viewportRefreshPending)
				{
					if (metricsState_)
					{
						metricsState_->Mark(RuntimeMetricsFrameReason::Recovery);
						metricsState_->Mark(RuntimeMetricsFrameReason::PartialReplay);
						if (viewportRefreshClearsTransient) metricsState_->InvalidateLiveRasters();
						metricsState_->BeginVisibleReplay(metricsSignature(size.width, size.height));
					}
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
					if (!tileCompleted)
					{
						rasterSubmissionFailed = true; // 不完整 L2 不能发布成功 Present 或内容版本。
						break;
					}
					++viewportTilePlanIndex;
					++recoveredTiles;
					if (viewportTilePlanIndex >= viewportTilePlan.visibleTileCount)
						viewportVisibleClear = true;
				}
				viewportRecoveryPending = viewportTilePlanIndex < viewportTilePlan.tiles.size();
				if (metricsState_)
				{
					metricsState_->Mark(RuntimeMetricsFrameReason::PartialReplay);
					metricsState_->CompleteVisibleReplay(metricsSignature(size.width, size.height),
						!viewportRecoveryPending && !rasterSubmissionFailed);
				}
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
					if (input_.AdmissionBlocked() || input_.HasQuarantinedContacts() ||
						touchGesture.PanActive() || suppressPenUntilRelease ||
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
			if (window_.ExitRequested())
			{
				currentLoadRetry.Cancel();
				break;
			}
			rasterSubmissionFailed |= std::exchange(pendingLaserBakeFailed, false);

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
				if (commandBoundaryPending && !window_.HasPendingCanvasCommand())
					commandBoundaryPending = false;
				if (activeWorkspace == Bridge::Workspace::Presentation &&
					activePresentationTarget && observer_.presentationLoadRequested &&
					activePresentationMutationRevision == 0 &&
					activePresentationQueuedRevision == 0)
					(void)TrySubmitCurrentLoadAtRunSafePoint(currentLoadRetry,
						*activePresentationTarget,
						activePresentationSlotGeneration, GetTickCount64(),
						activePresentationLoadPending,
						activePresentationPersistenceInitialized,
						window_.ExitRequested(),
						[&](PresentationLoadRequest&& request)
						{
							return observer_.presentationLoadRequested(observer_.context,
								std::move(request));
						});
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
				if (tryConsumeOneIngress(record))
				{
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
				if (metricsState_) metricsState_->Finish(); // idle/Hold等待不能混入render wall。
				lastActiveFrameStartMs = 0.0;
				if (laserLifecycle.phase == LaserTrailPhase::Hold)
				{
					if (tryConsumeOneIngress(record))
					{
						if (metrics_) metrics_->EndIdle(GetQpcTimeMilliseconds());
						continue;
					}
					if (commandBoundaryPending)
					{
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
						if (const auto retryWait = currentLoadRetryWait())
							input_.WaitForWake(waitGeneration, *retryWait);
						else
					{
							input_.WaitDequeue(record);
							processCommandAndReconcile(record);
						}
						if (metrics_) metrics_->EndIdle(GetQpcTimeMilliseconds());
						continue;
					}
					LARGE_INTEGER waitStartQpc = {};
					QueryPerformanceCounter(&waitStartQpc);
					double timeoutMilliseconds = QpcDeltaSeconds(
						holdDeadlineQpc, waitStartQpc.QuadPart, qpcFrequency) * 1000.0;
					if (const auto retryWait = currentLoadRetryWait())
						timeoutMilliseconds = (std::min)(timeoutMilliseconds, *retryWait);
					input_.WaitForWake(waitGeneration, timeoutMilliseconds);
					if (metrics_) metrics_->EndIdle(GetQpcTimeMilliseconds());
					continue; // Hold 静止期只等 deadline 或设置/Input wake，不持续 Present。
				}
				// 二次排空后才等待；提前捕获代次避免计时唤醒吞掉并发命令。
				const uint64_t idleWakeGeneration = input_.CaptureWakeGeneration();
				if (tryConsumeOneIngress(record))
				{
					if (metrics_) metrics_->EndIdle(GetQpcTimeMilliseconds());
					continue;
				}
				if (commandBoundaryPending)
				{
					if (metrics_) metrics_->EndIdle(GetQpcTimeMilliseconds());
					continue;
				}
				if (const auto retryWait = currentLoadRetryWait())
					input_.WaitForWake(idleWakeGeneration, *retryWait);
				else
				{
					input_.WaitDequeue(record);
					processCommandAndReconcile(record);
				}
				if (metrics_) metrics_->EndIdle(GetQpcTimeMilliseconds());
				continue;
			}

			const bool frameHadActiveContact = HasPhysicalContact(active);
			if (metricsState_) metricsState_->renderAttempt = true;
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
					drainIngressBatch();
				// 关闭开关时保留原先的 Down 出队顺序，确保它是完整的回滚点。
			}
			sealPresentationContacts();
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
					drainIngressBatch();
			sealPresentationContacts();
			hasEndedStroke = hasEndedStroke || std::any_of(active.begin(), active.end(),
				[](const RuntimeStroke* runtime) { return runtime && runtime->ended; });
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
				const auto metricsGeometryBefore = metricsState_ ? CaptureMetricGeometry(*runtime) : MetricGeometrySnapshot{};
				if (absl::Status status = runtime->stroke.modeler.Update(
					stationaryInput, runtime->stroke.modelScratch); status.ok())
				{
					runtime->lastModelInputTime = frameInputTime;
					if (runtime->stroke.endpointAdmission.recovering)
						AppendRecoveryModeledPoints(runtime->stroke, runtime->stroke.modelScratch,
							rawEndpoint, -1.0f);
					else AppendEndpointBoundedModeledPoints(runtime->stroke,
						runtime->stroke.modelScratch, -1.0f, runtime->stroke.lastMovementInputTime);
					if (metricsState_ && rawEndpoint.x == runtime->lastInputSnapshot.position.x &&
						rawEndpoint.y == runtime->lastInputSnapshot.position.y)
						metricsState_->Adopt(MetricKey(runtime->handle), runtime->lastInputSnapshot,
							ContentMetricAdoptionKind::Model, MetricGeometryChanged(metricsGeometryBefore, *runtime));
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
					const LiveRasterSubmission stable = stroke.endpointAdmission.active
						? LiveRasterSubmission{}
						: eraser
						? CommitEraserRealPointsToL1(stroke, StrokeShape::RoundCapsule,
							renderer_, size.width, size.height)
						: CommitStablePrefixToL1(stroke, liveTipProtectionSeconds,
							GetPredictionDurationSeconds(stroke),
							ColorForTool(runtime->tool, runtime->visualStyle),
							StrokeShape::RoundCapsule, renderer_, size.width, size.height);
					stableDirty = stable.dirty;
					rasterSubmissionFailed |= !stable.succeeded;
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
				if (metricsState_) metricsState_->Mark(RuntimeMetricsFrameReason::LaserBake);
				// 同批次最后一支抬起后一次性烘干，随后 Hold/Fade 只解析稳定颜色。
				UnionRectInPlace(frameDirty, previousLaserLiveBounds);
				const bool baked = BakeLaserStrokeLayers(laserStrokeLayers, renderer_,
					configuration_.dpiScale, size.width, size.height, laserStableBounds,
					frameDirty, laserCoverageMode);
				rasterSubmissionFailed |= !baked;
				if (baked)
				{
					UnionRectInPlace(frameDirty, laserLiveBounds);
					laserLiveBounds = {};
					laserCoverageMode = LaserCoverageMode::Inactive;
				}
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
						const double completedTipTaperSeconds = runtime->tool == DrawingTool::Pen
							? ResolveLiveTipTaperDurationSeconds(runtime->stroke.widthMode,
								configuration_.liveTipDurationSeconds) : 0.0;
						const auto metricsBeforeWrite = metricsState_ ? metricsSignature(size.width, size.height) : ContentMetricRasterSignature{};
						const auto committed = document_
							? CommitRuntimeStoredStrokeCpu(*runtime, *document_, pageRuntimeStates,
								currentPageIndex_, completedTipTaperSeconds,
								StoredStrokeCommitMode::NormalUp, allocateRasterStateToken)
							: std::nullopt;
						publishPenDiagnostics(*runtime, runtime->rebuildPoints);
						if (committed)
						{
							if (metricsState_) metricsState_->BeginLocalWrite(metricsBeforeWrite);
							InkPage* page = document_->PageAt(currentPageIndex_);
							InkCanvas* canvas = page->FindCanvas(kDefaultDeviceKey);
							CanvasPageRuntimeState& pageRuntime =
								pageRuntimeStates[currentPageIndex_];
							const size_t strokeIndex = committed->strokeIndex;
							// 文档对象先成为真值，再从刚追加的同一 Stroke 完成首次 L2 绘制。
							const std::span<const InkStroke> strokes = canvas->Strokes();
							const InkStroke& storedStroke = strokes[strokeIndex];
							const RenderItemId renderItem = committed->renderItem;
							HotPreimageCaptureResult preimageCapture;
							const InkRasterStateToken afterState = committed->afterState;
							if (metricsState_)
								metricsState_->CaptureStored(MetricKey(runtime->handle), *committed, pageRuntime,
									runtime->lastInputSnapshot.phase == ContactPhase::Up ? runtime->lastInputSnapshot : runtime->deferredUpSnapshot);
							{
								// 进入 runtime history 即成为“有内容”，GPU 呈现失败不回滚文档真值。
								markPresentationMutation();
								publishCurrentPageContent();
								const InkRasterStateToken beforeState = committed->beforeState;
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
								const RenderItemState* addedItem = pageRuntime.history.Find(renderItem);
								if (addedItem && completedStrokeRaster.succeeded)
								{
									preimageCapture = historyGpuCache.CapturePreimage({
										{ page->PageGuid(), kDefaultDeviceKey },
										renderItem,
										currentRasterKey(),
										beforeState,
										afterState,
										addedItem->undoTiles,
										canvas->Viewport().x,
										canvas->Viewport().y,
										size.width,
										size.height
									});
									if ((renderItem.index + 1) % kCompositionLeafItemCount == 0)
									{
										const std::optional<CompositionNodeId> leaf =
											pageRuntime.history.CompositionTree().LeafNodeForItem(
												renderItem.index);
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
								if (metricsState_)
									metricsState_->CompleteLocalWrite(metricsBeforeWrite, metricsSignature(size.width, size.height), submitted);
								pageRuntime.clearRedoAvailable = false;
								if (preimageCapture.status == HotPreimageCaptureStatus::Captured)
								{
									if (!submitted ||
										!historyGpuCache.CommitPreimage(preimageCapture.ticket))
										historyGpuCache.CancelPreimage(preimageCapture.ticket);
								}
								if (!submitted)
								{
									rasterSubmissionFailed = true;
									viewportVisibleClear =
										CanvasVisibleClarityAfterAuthoritativeWrite(
											viewportVisibleClear, false);
									viewportRefreshPending = true;
									viewportRefreshClearsTransient = false;
									std::cout << "[InkHistory] stored stroke raster failed page=" <<
										(currentPageIndex_ + 1) << " item=" << strokeIndex <<
										std::endl;
								}
							}
						}
						else
						{
							if (metricsState_) metricsState_->Invalidate(MetricKey(runtime->handle), ContentMetricInvalidationReason::ContentSuperseded);
							std::cout << "Failed to append completed stroke to the current ink canvas."
								<< std::endl;
						}
					}
					UnionRectInPlace(frameDirty, runtime->visibleDirty);
				}
				const LiveRasterSubmission rebuilt = RebuildActiveLayers(active,
					renderer_, size.width, size.height, shapePrimitiveScratch);
				rasterSubmissionFailed |= !rebuilt.succeeded;
				UnionRectInPlace(frameDirty, rebuilt.dirty);

				std::erase_if(active, [&](RuntimeStroke* runtime)
					{
						if (!runtime->ended) return false;
						if (!runtime->cancelled && observer_.strokeCompleted)
							observer_.strokeCompleted(observer_.context,
								CompletedStrokeKindForTool(runtime->tool));
						if (metricsState_ && (runtime->cancelled || runtime->tool == DrawingTool::Laser ||
							metricsState_->FindLive(MetricKey(runtime->handle))))
							metricsState_->Invalidate(MetricKey(runtime->handle), runtime->cancelled ?
								ContentMetricInvalidationReason::Cancelled : ContentMetricInvalidationReason::Stopped);
						handBackSpeedEraserController(*runtime);
						input_.DiscardUntilTerminal(runtime->handle); // 合成收尾不释放仍按下的物理路由。
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
						runtime->ownerWorkspaceGuid = {};
						runtime->ownerPageGuid = {};
						runtime->ownerPageIndex = 0;
						runtime->cpuCommitAttempted = false;
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
						{
							if (!DrawL0LiveComposite(runtime->stroke,
								ColorForTool(runtime->tool, runtime->visualStyle),
								StrokeShape::RoundCapsule, renderer_, false))
								rasterSubmissionFailed = true;
						}
					}
						if (!DrawActiveShapePrimitives(active, renderer_, shapePrimitiveScratch))
							rasterSubmissionFailed = true;
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
			{
				processCanvasCommands(frameDirty, laserParticleSnapshot,
					forceFullPresent, size.width, size.height);
				if (commandBoundaryPending && !window_.HasPendingCanvasCommand())
					commandBoundaryPending = false;
			}

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
			if (metricsState_)
			{
				const auto surface = metricsSignature(size.width, size.height);
				for (RuntimeStroke* runtime : active)
				{
					if (!runtime || runtime->cancelled || runtime->tool == DrawingTool::Laser) continue;
					const auto key = MetricKey(runtime->handle);
					const auto* note = metricsState_->FindLive(key);
					if (!note || note->landingConfirmed) continue;
					RECT realBounds = {};
					bool realContribution = false;
					if (runtime->shape.active)
					{
						const auto realEnd = note->adoptionKind == ContentMetricAdoptionKind::RawDown ?
							runtime->shape.rawEndpoint : runtime->shape.modeledEndpoint;
						// 预测矩形的边不必包含真实矩形；只有实际绘制末点与已采纳末点相同才认证Live Shape。
						realContribution = runtime->shape.primitive.end.x == realEnd.x && runtime->shape.primitive.end.y == realEnd.y;
						if (realContribution) realBounds = RectFromShapePrimitive(runtime->shape.primitive,
							runtime->shape.kind, size.width, size.height);
					}
					else if (!runtime->stroke.realPoints.empty())
					{
						realBounds = RectFromStrokePoints(runtime->stroke.realPoints, size.width, size.height);
						realContribution = true;
					}
					else if (runtime->stroke.hasInputStartPoint && !runtime->stroke.l0DrawPoints.empty())
					{
						const auto& first = runtime->stroke.l0DrawPoints.front();
						const auto& down = runtime->stroke.inputStartPoint;
						realContribution = first.x == down.x && first.y == down.y;
						if (realContribution)
						{
							const std::array<InkPoint, 1> downPoint = { down };
							realBounds = RectFromStrokePoints(std::span<const InkPoint>(downPoint), size.width, size.height);
						}
					}
					metricsState_->ObserveLiveRaster(key, note->adoptedSequence, realBounds,
						metricsState_->frame.frameSerial, !rasterSubmissionFailed && realContribution, &surface);
				}
			}
			bool presentSucceeded = false;
			if (!IsEmptyRect(frameDirty) && !rasterSubmissionFailed)
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
				bool compositeSucceeded = true;
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
					compositeSucceeded = renderer_.ApplyOperatorLayers(renderer_.backBufferRTV.Get(),
						renderer_.layerL1, renderer_.layerL0, frameDirty, mergeMode);
				}
				else compositeSucceeded = CompositeLayersToBackBuffer(frameDirty, orderedPreview);
				if (!compositeSucceeded)
					rasterSubmissionFailed = true;
				else
				{
					// 粒子先于激光主体绘制，使粒子辉光托衬在墨迹主体下方，避免遮挡演示内容。
					if (shouldDrawLaserParticles)
					{
						ConfigureLaserRendererStyle(renderer_, laserTrailVisualStyle,
							configuration_.dpiScale);
						renderer_.DrawLaserParticles();
					}
					if (laserLifecycle.phase != LaserTrailPhase::Inactive && laserOpacity > 0.0f)
					{
						if (!IsEmptyRect(laserStableBounds) &&
							!renderer_.ResolveLaserCompositedColor(
								renderer_.backBufferRTV.Get(), frameDirty, laserOpacity))
							rasterSubmissionFailed = true;
						if (!rasterSubmissionFailed &&
							!DrawLaserStrokeLayers(laserStrokeLayers, renderer_,
								renderer_.backBufferRTV.Get(), frameDirty,
								configuration_.dpiScale, laserCoverageMode))
							rasterSubmissionFailed = true;
					}
					if (!rasterSubmissionFailed)
					{
						for (const LaserTipVisual& visual : laserTipVisuals)
						{
							ConfigureLaserRendererStyle(renderer_, visual.visualStyle,
								configuration_.dpiScale);
							renderer_.DrawLaserDots(
								std::span<const LaserDot>(&visual.dot, 1));
						}
						for (const DrawingCursorVisual& visual : currentCursorVisuals)
							renderer_.DrawTransientDrawingCursor(visual);
						if (metricsState_ && snapshotCanvas && currentPageIndex_ < pageRuntimeStates.size())
						{
							if (!viewportVisibleClear || viewportRefreshPending || viewportRecoveryPending)
								metricsState_->InvalidateL2();
							metricsState_->ObserveOutput(true, presentation_.RequestedOutputTarget(), presentation_.RequestedOutputRevision());
							const uint64_t withheldBefore = metricsState_->counters.authoritativeWithheld;
							metricsState_->FreezeCandidates(metricsSignature(size.width, size.height), *snapshotCanvas,
								pageRuntimeStates[currentPageIndex_], frameDirty, true,
								frameDirty.left == 0 && frameDirty.top == 0 && frameDirty.right == size.width && frameDirty.bottom == size.height);
							// 只把本次实际拒绝的新增计数接到帧原因，不能从整个L2状态猜测。
							if (metricsState_->counters.authoritativeWithheld != withheldBefore)
								metricsState_->Mark(RuntimeMetricsFrameReason::AuthoritativeWithheld);
						}
						presentSucceeded = PresentFrame(frameDirty,
							forceFullPresent); // 一帧最多一次 backbuffer 合成和一次 Present。
					}
				}
			}
			if (rasterSubmissionFailed)
			{
				if (metricsState_)
				{
					metricsState_->Mark(RuntimeMetricsFrameReason::RasterFailed);
					metricsState_->InvalidateLiveRasters();
				}
				// CPU 点和文档仍为权威；未完成的 GPU 帧不能作为可见回执。
				activeLayerRebuildPending = true;
				consecutiveRasterFailures = std::min(consecutiveRasterFailures + 1, 3u);
				const HRESULT deviceReason = renderer_.device
					? renderer_.device->GetDeviceRemovedReason() : E_FAIL;
				const ULONGLONG nowTick = GetTickCount64();
				if (consecutiveRasterFailures == 1 ||
					nowTick - lastRasterFailureLogTick >= 1000)
				{
					std::fprintf(stderr,
						"[Draw3.Raster] submission failed; attempts=%u device=0x%08X\n",
						consecutiveRasterFailures, static_cast<unsigned>(deviceReason));
					lastRasterFailureLogTick = nowTick;
				}
				// 设备移除沿用 presenter 恢复；普通错误重试时保留尚未 Up 的 CPU contact。
				if (FAILED(deviceReason))
				{
					presentation_.MarkRuntimeFailure(E_FAIL);
					graphicsRecoveryPending_ = true;
				}
				// activeLayerRebuildPending 会在下一轮强制全脏重放；不要给自己排 control wake 跳过 idle 退避。
			}
			else if (presentSucceeded)
			{
				consecutiveRasterFailures = 0;
				activeLayerRebuildPending = false;
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
			if (presentSucceeded)
			{
				previousCursorVisuals = currentCursorVisuals;
				previousLaserParticleBounds = currentLaserParticleBounds;
				previousLaserTipBounds = currentLaserTipBounds;
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
			if (metricsState_)
			{
				metricsState_->frame.physicalAfter = static_cast<uint32_t>(std::count_if(active.begin(), active.end(),
					[](const RuntimeStroke* runtime) { return runtime && !runtime->ended && !runtime->awaitingReconnect; }));
				if (drawingCursorRequested || speedEraserHoverAnimating) metricsState_->Mark(RuntimeMetricsFrameReason::Hover);
				if (laserLifecycle.phase == LaserTrailPhase::Active) metricsState_->Mark(RuntimeMetricsFrameReason::LaserActive);
				if (laserLifecycle.phase == LaserTrailPhase::Hold) metricsState_->Mark(RuntimeMetricsFrameReason::LaserHold);
				if (laserLifecycle.phase == LaserTrailPhase::Fade) metricsState_->Mark(RuntimeMetricsFrameReason::LaserFade);
				if (laserExpired) metricsState_->Mark(RuntimeMetricsFrameReason::LaserExpiry);
				if (shouldStepLaserParticles || shouldDrawLaserParticles) metricsState_->Mark(RuntimeMetricsFrameReason::LaserParticles);
				metricsState_->Finish(canvasFrameElapsedMilliseconds);
			}
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
			if (rasterSubmissionFailed && (active.empty() || eraserIdle))
			{
				// 错误帧无 Present；idle 时等待真实唤醒或有限重试间隔，避免无限满速循环。
				const uint64_t wakeGeneration = input_.CaptureWakeGeneration();
				// 设备已移除时下一帧尽快重建，普通反复 Map 错误才用较长退避。
				if (!input_.HasPendingWork())
					input_.WaitForWake(wakeGeneration, graphicsRecoveryPending_ ? 16.0 : 250.0);
			}
			else if (hasPhysicalContactAfterFrame && !eraserIdle)
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
		}
		catch (const std::bad_alloc&)
		{
			if (metricsState_) metricsState_->EndContacts(ContentMetricInvalidationReason::Fatal);
			std::fputs("[Draw3.AutoSave] action=run_exception result=not_queued reason=allocation\n", stderr);
			throw;
		}
		catch (const std::exception& error)
		{
			if (metricsState_) metricsState_->EndContacts(ContentMetricInvalidationReason::Fatal);
			// Run 的局部 history 仍存活时尽力封口；随后原异常继续交 Host 受控退出。
			std::fprintf(stderr, "[Draw3.AutoSave] action=run_exception reason=%s\n", error.what());
			// CPU 追加中途抛错可能留下半事务；数量不齐时不导出可疑 history。
			const bool activeHistoryConsistent = document_ &&
				pageRuntimeStates.size() == document_->Pages().size() &&
				std::all_of(pageRuntimeStates.begin(), pageRuntimeStates.end(),
					[](const CanvasPageRuntimeState& page)
					{ return page.history.Items().size() == page.beforeStates.size() &&
						page.beforeStates.size() == page.afterStates.size(); });
			if (!activeHistoryConsistent)
				std::fputs("[Draw3.AutoSave] action=run_exception result=not_queued reason=history_state\n", stderr);
			if (!fatalCaptureAttempted && activeHistoryConsistent)
			{
				try { captureGraphicsFatalExit("run_exception"); }
				catch (const std::exception& captureError)
				{
					std::fprintf(stderr,
						"[Draw3.AutoSave] action=run_exception result=not_queued detail=%s\n",
						captureError.what());
				}
			}
			throw;
		}

		if (metrics_) metrics_->EndIdle(GetQpcTimeMilliseconds());
		FlushCursorDiagnostics();
		if (haptics_) haptics_->StopFeedback();
		if (drawingPriorityRaised)
			SetThreadPriority(GetCurrentThread(), originalThreadPriority);
	}

}
