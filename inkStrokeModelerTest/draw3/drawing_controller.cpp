module;

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
#include <manipulations.h>
#include <memory>
#include <mutex>
#include <ocidl.h>
#include <optional>
#include <span>
#include <utility>
#include <vector>
#include <dxgiformat.h>
#include <windows.h>
#include <wrl/client.h>
#include <mmsystem.h>

#pragma comment(lib, "winmm.lib")

module draw3.drawing_controller;

import draw3.diagnostics;
import draw3.canvas_navigation;
import draw3.haptic_feedback;
import draw3.pen_cursor;

namespace draw3
{
	namespace
	{
		const DirectX::XMFLOAT4 kHighlighterCompositeColor(1.0f, 0.0f, 0.0f, 0.35f);
		const DirectX::XMFLOAT4 kMultiContactInkColor(1.0f, 0.0f, 0.0f, 1.0f);
		const DirectX::XMFLOAT4 kReconnectManualTestColor(0.0f, 1.0f, 0.0f, 1.0f);
		constexpr float kPenDiameter = 5.0f;
		constexpr float kLaserDiameter = kLaserSolidDiameterAt96Dpi;
		constexpr size_t kLaserReservedContactCount = 32;
		constexpr float kMinimumPenCursorDiameterAt96Dpi = 5.0f;
		constexpr float kPenTipCursorFillAlpha = 0.25f;
		constexpr float kWideToolDiameter = 50.0f;
		constexpr float kReconnectManualTestRadiusPx = 4.0f;
		constexpr float kRawMoveThresholdPx = 0.25f;
		constexpr float kStylusPressureEpsilon = 0.0001f;
		constexpr float kStylusAngleEpsilon = 0.0001f;
		constexpr float kPi = 3.14159265358979323846f;
		constexpr float kHalfPi = kPi * 0.5f;
		constexpr float kTwoPi = kPi * 2.0f;
		constexpr double kInputSpeedSmoothingSeconds = 0.060;
		constexpr double kMaximumLaserHoldDurationSeconds = 24.0 * 60.0 * 60.0;
		constexpr size_t kPreheatedStrokeCount = 16;

		class CanvasManipulationEventSink final : public _IManipulationEvents
		{
		public:
			HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** object) override
			{
				if (!object) return E_POINTER;
				*object = nullptr;
				if (iid == __uuidof(IUnknown) || iid == __uuidof(_IManipulationEvents))
				{
					*object = static_cast<_IManipulationEvents*>(this);
					AddRef();
					return S_OK;
				}
				return E_NOINTERFACE;
			}

			ULONG STDMETHODCALLTYPE AddRef() override { return ++references_; }
			ULONG STDMETHODCALLTYPE Release() override
			{
				const ULONG references = --references_;
				if (references == 0) delete this;
				return references;
			}

			HRESULT STDMETHODCALLTYPE ManipulationStarted(FLOAT, FLOAT) override
			{
				ResetDelta();
				completed_ = false;
				return S_OK;
			}

			HRESULT STDMETHODCALLTYPE ManipulationDelta(FLOAT, FLOAT,
				FLOAT translationDeltaX, FLOAT translationDeltaY,
				FLOAT, FLOAT, FLOAT, FLOAT, FLOAT, FLOAT, FLOAT, FLOAT) override
			{
				if (std::isfinite(translationDeltaX) && std::isfinite(translationDeltaY))
				{
					delta_.x += translationDeltaX;
					delta_.y += translationDeltaY;
				}
				return S_OK;
			}

			HRESULT STDMETHODCALLTYPE ManipulationCompleted(
				FLOAT, FLOAT, FLOAT, FLOAT, FLOAT, FLOAT, FLOAT) override
			{
				completed_ = true;
				return S_OK;
			}

			void ResetDelta() noexcept { delta_ = {}; }
			void BeginOperation() noexcept
			{
				delta_ = {};
				completed_ = false;
			}
			CanvasVector TakeDelta() noexcept
			{
				const CanvasVector delta = delta_;
				delta_ = {};
				return delta;
			}
			bool Completed() const noexcept { return completed_; }

		private:
			std::atomic<ULONG> references_ = 1;
			CanvasVector delta_ = {};
			bool completed_ = false;
		};

		class CanvasWindowsManipulation
		{
		public:
			~CanvasWindowsManipulation()
			{
				if (manipulationConnection_ && manipulationCookie_ != 0)
					manipulationConnection_->Unadvise(manipulationCookie_);
				if (inertiaConnection_ && inertiaCookie_ != 0)
					inertiaConnection_->Unadvise(inertiaCookie_);
			}

			bool Initialize()
			{
				using Microsoft::WRL::ComPtr;
				if (FAILED(CoCreateInstance(__uuidof(ManipulationProcessor), nullptr,
					CLSCTX_INPROC_SERVER, IID_PPV_ARGS(manipulation_.ReleaseAndGetAddressOf()))) ||
					FAILED(CoCreateInstance(__uuidof(InertiaProcessor), nullptr,
						CLSCTX_INPROC_SERVER, IID_PPV_ARGS(inertia_.ReleaseAndGetAddressOf()))))
					return false;
				const auto supported = static_cast<MANIPULATION_PROCESSOR_MANIPULATIONS>(
					MANIPULATION_TRANSLATE_X | MANIPULATION_TRANSLATE_Y);
				if (FAILED(manipulation_->put_SupportedManipulations(supported))) return false;
				manipulationSink_.Attach(new (std::nothrow) CanvasManipulationEventSink());
				inertiaSink_.Attach(new (std::nothrow) CanvasManipulationEventSink());
				if (!manipulationSink_ || !inertiaSink_ ||
					!Advise(manipulation_.Get(), manipulationSink_.Get(),
						manipulationConnection_, manipulationCookie_) ||
					!Advise(inertia_.Get(), inertiaSink_.Get(),
						inertiaConnection_, inertiaCookie_)) return false;
				available_ = true;
				return true;
			}

			bool Available() const noexcept { return available_; }

			void Begin(std::span<const std::pair<MANIPULATOR_ID, CanvasVector>> contacts)
			{
				if (!available_) return;
				manipulation_->CompleteManipulation();
				lastManipulationVelocity_ = {};
				for (const auto& [id, point] : contacts)
					if (FAILED(manipulation_->ProcessDown(id, point.x, point.y)))
					{
						available_ = false;
						return;
					}
			}

			void Move(MANIPULATOR_ID id, CanvasVector point) noexcept
			{
				if (!available_) return;
				if (FAILED(manipulation_->ProcessMove(id, point.x, point.y)))
				{
					available_ = false;
					return;
				}
				CaptureManipulationVelocity();
			}

			void Down(MANIPULATOR_ID id, CanvasVector point) noexcept
			{
				if (available_ && FAILED(manipulation_->ProcessDown(id, point.x, point.y)))
					available_ = false;
			}

			void Up(MANIPULATOR_ID id, CanvasVector point) noexcept
			{
				if (!available_) return;
				// 最后一指 ProcessUp 会完成 processor；必须先锁存甩动速度。
				CaptureManipulationVelocity();
				if (FAILED(manipulation_->ProcessUp(id, point.x, point.y)))
					available_ = false;
			}

			CanvasVector VelocityDipPerSecond() const noexcept
			{
				return lastManipulationVelocity_;
			}

			bool StartInertia(CanvasVector velocityDipPerSecond) noexcept
			{
				inertiaRunning_ = false;
				if (!available_ || FAILED(inertia_->Reset())) return false;
				// Stop/CompleteTime 会留下 Completed；新一轮必须显式清掉旧终态。
				inertiaSink_->BeginOperation();
				inertiaRunning_ =
					SUCCEEDED(inertia_->put_InitialOriginX(0.0f)) &&
					SUCCEEDED(inertia_->put_InitialOriginY(0.0f)) &&
					SUCCEEDED(inertia_->put_InitialVelocityX(velocityDipPerSecond.x / 1000.0f)) &&
					SUCCEEDED(inertia_->put_InitialVelocityY(velocityDipPerSecond.y / 1000.0f)) &&
					SUCCEEDED(inertia_->put_DesiredDeceleration(
						kCanvasPanInertiaDecelerationDipPerSecondSquared / 1000000.0f)) &&
					SUCCEEDED(inertia_->put_InitialTimestamp(GetTickCount()));
				return inertiaRunning_;
			}

			bool StepInertia(bool penInRange, CanvasVector& delta, bool& completed) noexcept
			{
				delta = {};
				completed = true;
				if (!available_ || !inertiaRunning_) return false;
				inertiaSink_->ResetDelta();
				BOOL nativeCompleted = FALSE;
				const float deceleration = penInRange
					? kCanvasPanPenBrakeDecelerationDipPerSecondSquared / 1000000.0f
					: kCanvasPanInertiaDecelerationDipPerSecondSquared / 1000000.0f;
				if (FAILED(inertia_->put_DesiredDeceleration(deceleration)) ||
					FAILED(inertia_->ProcessTime(GetTickCount(), &nativeCompleted)))
				{
					inertiaRunning_ = false;
					return false;
				}
				delta = inertiaSink_->TakeDelta();
				completed = nativeCompleted != FALSE || inertiaSink_->Completed();
				if (completed) inertiaRunning_ = false;
				return std::isfinite(delta.x) && std::isfinite(delta.y);
			}

			void Stop() noexcept
			{
				if (manipulation_) manipulation_->CompleteManipulation();
				if (inertia_) inertia_->CompleteTime(GetTickCount());
				inertiaRunning_ = false;
			}

		private:
			void CaptureManipulationVelocity() noexcept
			{
				float x = 0.0f;
				float y = 0.0f;
				if (FAILED(manipulation_->GetVelocityX(&x)) ||
					FAILED(manipulation_->GetVelocityY(&y)) ||
					!std::isfinite(x) || !std::isfinite(y)) return;
				lastManipulationVelocity_ = { x * 1000.0f, y * 1000.0f };
			}

			static bool Advise(IUnknown* source, _IManipulationEvents* sink,
				Microsoft::WRL::ComPtr<IConnectionPoint>& connection, DWORD& cookie)
			{
				Microsoft::WRL::ComPtr<IConnectionPointContainer> container;
				if (!source || FAILED(source->QueryInterface(IID_PPV_ARGS(
					container.ReleaseAndGetAddressOf()))) ||
					FAILED(container->FindConnectionPoint(__uuidof(_IManipulationEvents),
						connection.ReleaseAndGetAddressOf()))) return false;
				return SUCCEEDED(connection->Advise(sink, &cookie));
			}

			Microsoft::WRL::ComPtr<IManipulationProcessor> manipulation_;
			Microsoft::WRL::ComPtr<IInertiaProcessor> inertia_;
			Microsoft::WRL::ComPtr<CanvasManipulationEventSink> manipulationSink_;
			Microsoft::WRL::ComPtr<CanvasManipulationEventSink> inertiaSink_;
			Microsoft::WRL::ComPtr<IConnectionPoint> manipulationConnection_;
			Microsoft::WRL::ComPtr<IConnectionPoint> inertiaConnection_;
			DWORD manipulationCookie_ = 0;
			DWORD inertiaCookie_ = 0;
			CanvasVector lastManipulationVelocity_ = {};
			bool inertiaRunning_ = false;
			bool available_ = false;
		};

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

		float DiameterForTool(DrawingTool tool)
		{
			if (tool == DrawingTool::Pen || IsShapeDrawingTool(tool)) return kPenDiameter;
			if (tool == DrawingTool::Laser) return kLaserDiameter;
			return kWideToolDiameter;
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

		DirectX::XMFLOAT4 ColorForTool(DrawingTool tool)
		{
			if (tool == DrawingTool::Highlighter) return kHighlighterCompositeColor;
			if (tool == DrawingTool::Eraser) return kTransparentLayerClearColor;
			return kMultiContactInkColor;
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

		std::optional<StoredInkStyle> StoredStyleForTool(DrawingTool tool) noexcept
		{
			StoredInkStyle style;
			const DirectX::XMFLOAT4 color = ColorForTool(tool);
			style.fallbackRgb = PackStoredRgb(color);
			style.texture = 0;
			switch (tool)
			{
			case DrawingTool::Pen:
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
			bool suppressPressure = false;
			ContactSnapshot lastSpeedSnapshot = {};
			ContactSnapshot lastInputSnapshot = {};
			ContactSnapshot lastModelSnapshot = {};
			uint64_t lastConsumedSequence = 0;
			int64_t qpcOrigin = 0;
			double lastModelInputTime = 0.0;
			float filteredInputSpeed = 0.0f;
			float lastPressure = -1.0f;
			float lastTilt = -1.0f;
			float lastOrientation = -1.0f;
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

		struct CanvasGestureContactRuntime
		{
			ContactHandle handle = {};
			uint64_t key = 0;
			MANIPULATOR_ID manipulatorId = 0;
			ContactSnapshot snapshot = {};
			uint64_t lastConsumedSequence = 0;
			CanvasTouchDisposition disposition = CanvasTouchDisposition::Suppressed;
		};

		uint64_t CanvasTouchKey(const ContactHandle& handle) noexcept
		{
			if (!handle.record) return 0;
			return static_cast<uint64_t>(handle.record->TabletContextId()) << 32 |
				handle.record->ContactId();
		}

		MANIPULATOR_ID CanvasManipulatorId(const ContactHandle& handle) noexcept
		{
			if (!handle.record) return 0;
			const uint32_t mixed = handle.record->TabletContextId() * 0x9E3779B9u ^
				handle.record->ContactId() * 0x85EBCA6Bu;
			return static_cast<MANIPULATOR_ID>(mixed == 0 ? 1 : mixed);
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
			std::vector<InkPoint> completedPoints;
			LaserIncrementalStrokeState incrementalState;
			RECT stableBounds = {};
			RECT liveBounds = {};
			RECT bounds = {};
			bool cancelled = false;
		};

		struct LaserIncrementalDiagnostics
		{
			uint64_t activeFrames = 0;
			uint64_t stableSubmittedPoints = 0;
			uint64_t liveSubmittedPoints = 0;
			uint64_t fullRedrawEquivalentPoints = 0;
			uint64_t dirtyPixels = 0;
			uint64_t peakDirtyPixels = 0;
			uint64_t coverageSubmissionCount = 0;
			double coverageSubmissionMs = 0.0;
			double maximumCoverageSubmissionMs = 0.0;
			size_t maximumLayerCount = 0;
			size_t maximumVisiblePoints = 0;
			const char* fallbackReason = "none";

			void Reset() noexcept
			{
				*this = {};
				fallbackReason = "none";
			}
		};

		uint64_t RectArea(const RECT& rect) noexcept
		{
			if (IsEmptyRect(rect)) return 0;
			return static_cast<uint64_t>(rect.right - rect.left) *
				static_cast<uint64_t>(rect.bottom - rect.top);
		}

		void LogLaserSummary(const LaserIncrementalDiagnostics& diagnostics,
			bool incremental, bool particlesEnabled)
		{
			if constexpr (!kLaserIncrementalDiagnosticsEnabled) return;
			const double averageCoverageMs = diagnostics.coverageSubmissionCount > 0
				? diagnostics.coverageSubmissionMs /
				static_cast<double>(diagnostics.coverageSubmissionCount) : 0.0;
			std::cout << "[LaserPerf] Summary mode=" <<
				(incremental ? "incremental" : "fallback") <<
				" particles=" << (particlesEnabled ? 1 : 0) <<
				" layers=" << diagnostics.maximumLayerCount <<
				" max_points=" << diagnostics.maximumVisiblePoints <<
				" stable_points=" << diagnostics.stableSubmittedPoints <<
				" live_points=" << diagnostics.liveSubmittedPoints <<
				" full_equivalent_points=" << diagnostics.fullRedrawEquivalentPoints <<
				" dirty_pixels=" << diagnostics.dirtyPixels <<
				" peak_dirty_pixels=" << diagnostics.peakDirtyPixels <<
				" active_frames=" << diagnostics.activeFrames <<
				" coverage_calls=" << diagnostics.coverageSubmissionCount <<
				" coverage_cpu_total_ms=" << diagnostics.coverageSubmissionMs <<
				" coverage_cpu_avg_ms=" << averageCoverageMs <<
				" coverage_cpu_max_ms=" << diagnostics.maximumCoverageSubmissionMs <<
				" fallback=" << diagnostics.fallbackReason << std::endl;
		}

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
			LaserIncrementalDiagnostics& diagnostics, RECT& dirty)
		{
			const double startMs = GetQpcTimeMilliseconds();
			dirty = layer.liveBounds;
			auto finish = [&](bool succeeded) noexcept
			{
				const double elapsedMs = GetQpcTimeMilliseconds() - startMs;
				++diagnostics.coverageSubmissionCount;
				diagnostics.coverageSubmissionMs += elapsedMs;
				diagnostics.maximumCoverageSubmissionMs = std::max(
					diagnostics.maximumCoverageSubmissionMs, elapsedMs);
				return succeeded;
			};
			const LaserIncrementalRanges ranges = PlanLaserIncrementalRanges(
				realPoints, layer.incrementalState, protectedDurationSeconds);
			RECT nextStableBounds = layer.stableBounds;
			if (ranges.stablePointCount > 0)
			{
				if (ranges.stableFirstIndex >= visiblePoints.size() ||
					ranges.stablePointCount >
						visiblePoints.size() - ranges.stableFirstIndex)
					return finish(false);
				const std::span<const InkPoint> stablePoints = visiblePoints.subspan(
					ranges.stableFirstIndex, ranges.stablePointCount);
				renderer.SetLaserCoverageTarget(renderer.laserStrokeCoverage);
				if (renderer.DrawLaserCoverage(stablePoints) != 0)
					return finish(false);
				const RECT stableDirty = RectFromLaserPoints(
					stablePoints, dpiScale, width, height);
				UnionRectInPlace(nextStableBounds, stableDirty);
				UnionRectInPlace(dirty, stableDirty);
				diagnostics.stableSubmittedPoints += stablePoints.size();
			}

			if (!renderer.ClearLaserLiveCoverageRect(layer.liveBounds))
				return finish(false);
			if (ranges.liveFirstIndex > visiblePoints.size()) return finish(false);
			const std::span<const InkPoint> livePoints =
				visiblePoints.subspan(ranges.liveFirstIndex);
			const RECT nextLiveBounds = RectFromLaserPoints(
				livePoints, dpiScale, width, height);
			if (!livePoints.empty())
			{
				renderer.SetLaserLiveCoverageTarget();
				if (renderer.DrawLaserCoverage(livePoints) != 0)
					return finish(false);
				diagnostics.liveSubmittedPoints += livePoints.size();
			}
			UnionRectInPlace(dirty, nextLiveBounds);
			layer.stableBounds = nextStableBounds;
			layer.liveBounds = nextLiveBounds;
			layer.incrementalState.stableCommittedIndex =
				ranges.nextStableCommittedIndex;
			layer.incrementalState.rebuildRequired = false;
			layer.bounds = layer.stableBounds;
			UnionRectInPlace(layer.bounds, layer.liveBounds);
			return finish(true);
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
			RECT& compositedBounds, RECT& bakeDirty, LaserCoverageMode& coverageMode,
			LaserIncrementalDiagnostics& diagnostics)
		{
			diagnostics.maximumLayerCount = std::max(
				diagnostics.maximumLayerCount, layers.size());
			if (coverageMode == LaserCoverageMode::Incremental && layers.size() == 1 &&
				renderer.LaserIncrementalCoverageAvailable())
			{
				LaserStrokeLayer& layer = layers.front();
				bool incrementalBakeSucceeded = true;
				if (!layer.cancelled)
				{
					const std::vector<InkPoint>& points = LaserStrokeLayerPoints(layer);
					diagnostics.maximumVisiblePoints = std::max(
						diagnostics.maximumVisiblePoints, points.size());
					RECT coverageDirty = {};
					const bool coverageUpdated = UpdateLaserIncrementalCoverage(
						layer, points, points, 0.0, renderer, dpiScale,
						width, height, diagnostics, coverageDirty);
					UnionRectInPlace(bakeDirty, coverageDirty);
					if (!coverageUpdated)
					{
						incrementalBakeSucceeded = false;
						coverageMode = LaserCoverageMode::FullRedraw;
						diagnostics.fallbackReason = "coverage_submission_failed";
					}
					else if (!IsEmptyRect(layer.bounds))
					{
						if (renderer.ResolveLaserIncrementalCoverage(
							renderer.laserCompositedColor.rtv.Get(), layer.bounds))
						{
							diagnostics.fullRedrawEquivalentPoints += points.size();
							UnionRectInPlace(compositedBounds, layer.bounds);
						}
						else
						{
							incrementalBakeSucceeded = false;
							coverageMode = LaserCoverageMode::FullRedraw;
							diagnostics.fallbackReason = "coverage_resolve_failed";
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
				diagnostics.maximumVisiblePoints = std::max(
					diagnostics.maximumVisiblePoints, points.size());
				diagnostics.fullRedrawEquivalentPoints += points.size();
				layer.bounds = RectFromLaserPoints(points, dpiScale, width, height);
				if (IsEmptyRect(layer.bounds)) continue;
				UnionRectInPlace(bakeDirty, layer.bounds);
				// 每支笔先独立生成 coverage，再按 Down 顺序烘入稳定预乘颜色。
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
			RECT clipBounds,
			LaserCoverageMode& coverageMode,
			LaserIncrementalDiagnostics& diagnostics)
		{
			if (coverageMode == LaserCoverageMode::Incremental && layers.size() == 1)
			{
				if (!renderer.LaserIncrementalCoverageAvailable())
				{
					coverageMode = LaserCoverageMode::FullRedraw;
					diagnostics.fallbackReason = "resource_unavailable";
					renderer.ClearLaserIncrementalCoverage();
				}
				else
				{
					LaserStrokeLayer& layer = layers.front();
					RECT resolveBounds = {};
					if (!IntersectRect(&resolveBounds, &layer.bounds, &clipBounds)) return;
					if (renderer.ResolveLaserIncrementalCoverage(target, resolveBounds))
					{
						diagnostics.fullRedrawEquivalentPoints +=
							LaserStrokeLayerPoints(layer).size();
						return;
					}
					coverageMode = LaserCoverageMode::FullRedraw;
					diagnostics.fallbackReason = "coverage_resolve_failed";
					renderer.ClearLaserIncrementalCoverage();
				}
			}
			else if (coverageMode == LaserCoverageMode::Incremental)
			{
				coverageMode = LaserCoverageMode::FullRedraw;
				diagnostics.fallbackReason = "multiple_contacts";
				renderer.ClearLaserIncrementalCoverage();
			}
			for (LaserStrokeLayer& layer : layers)
			{
				const std::vector<InkPoint>& points = LaserStrokeLayerPoints(layer);
				if (!ShouldCompositeLaserLayer(layer.cancelled, points.size())) continue;
				RECT resolveBounds = {};
				if (!IntersectRect(&resolveBounds, &layer.bounds, &clipBounds)) continue;
				diagnostics.fullRedrawEquivalentPoints += points.size();
				// 仅处理最终 frame dirty 的交集；完整几何和 Down 顺序保持不变。
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

		RECT RectFromLaserDots(const std::vector<LaserDot>& dots,
			float dpiScale, int width, int height) noexcept
		{
			RECT bounds = {};
			for (const LaserDot& dot : dots)
			{
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
					ColorForTool(runtime.tool));
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
			renderer.DrawStrokeOrDot(stablePoints, ColorForTool(runtime.tool),
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
				scratch.clear();
				for (const RuntimeStroke* runtime : active)
				{
					if (!runtime || runtime->ended || !runtime->shape.active ||
						runtime->shape.kind != kind) continue;
					scratch.push_back(runtime->shape.primitive);
				}
				if (scratch.empty()) continue;
				renderer.SetOperatorTarget(renderer.layerL0);
				renderer.DrawShapePrimitives(scratch, kind,
					ColorForTool(DrawingTool::Pen));
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
					DrawL0LiveComposite(stroke, ColorForTool(runtime->tool),
						StrokeShape::RoundCapsule, renderer, false);
				UnionRectInPlace(dirty, stroke.currentL0Rect);
			}
			DrawActiveShapePrimitives(active, renderer, shapeScratch);
			return ClampRectToCanvas(dirty, width, height);
		}
	}

	DrawingController::DrawingController(ContactInputCoordinator& input, WindowController& window, InkRenderer& renderer,
		TransparentPresentationController& presentation, StrokeModelConfiguration configuration,
		RuntimeMetricsSession* metrics, PenHapticFeedback* haptics)
		: input_(input), window_(window), renderer_(renderer),
		presentation_(presentation), configuration_(std::move(configuration)),
		inputWidthModeSettings_(configuration_.inputWidthModes),
		invertedPenEraserEnabled_(configuration_.invertedPenEraserEnabled),
		interruptedStrokeReconnectEnabled_(configuration_.interruptedStrokeReconnectEnabled),
		drawingCursorDuringContactEnabled_(configuration_.drawingCursorDuringContactEnabled),
		translucentInkCursorEnabled_(configuration_.translucentInkCursorEnabled),
		laserParticlesEnabled_(configuration_.laserParticlesEnabled),
		laserMultiTouchDrawingEnabled_(configuration_.laserMultiTouchDrawingEnabled),
		performanceHudEnabled_(configuration_.performanceHudEnabled),
		laserHoldDurationSeconds_(std::isfinite(configuration_.laserHoldDurationSeconds) &&
			configuration_.laserHoldDurationSeconds >= 0.0
			? configuration_.laserHoldDurationSeconds : 1.0), metrics_(metrics),
		haptics_(haptics)
	{
		window_.SetMouseUsesSystemCursor(configuration_.mouseUsesSystemCursor);
		const DirectX::XMFLOAT4 penColor = ColorForTool(DrawingTool::Pen);
		const float penCursorDiameter = std::max(
			kPenDiameter, kMinimumPenCursorDiameterAt96Dpi * configuration_.dpiScale);
		DrawingCursorAppearance penCursorAppearance = {
			DrawingCursorShape::Circle,
			penCursorDiameter,
			penCursorDiameter,
			penColor.x,
			penColor.y,
			penColor.z
		};
		penCursorAppearance.fillAlpha = kPenTipCursorFillAlpha;
		window_.ConfigureDrawingCursor(DrawingTool::Pen, penCursorAppearance);
		const DirectX::XMFLOAT4 highlighterColor = ColorForTool(DrawingTool::Highlighter);
		DrawingCursorAppearance highlighterCursorAppearance = {
			DrawingCursorShape::Rectangle,
			kWideToolDiameter / kHighlighterNibAspectRatio,
			kWideToolDiameter,
			highlighterColor.x,
			highlighterColor.y,
			highlighterColor.z
		};
		highlighterCursorAppearance.fillAlpha = kPenTipCursorFillAlpha;
		window_.ConfigureDrawingCursor(DrawingTool::Highlighter, highlighterCursorAppearance);
		// 橡皮实际模型使用画布像素宽度，光标不能再次乘 DPI。
		const float eraserCursorDiameter = kWideToolDiameter;
		DrawingCursorAppearance eraserAppearance = {
			DrawingCursorShape::EraserGripCircle,
			eraserCursorDiameter,
			eraserCursorDiameter,
			1.0f, 1.0f, 1.0f
		};
		eraserAppearance.opacity = 0.5f;
		eraserAppearance.fillAlpha = 1.0f;
		eraserAppearance.outlineWidth = eraserCursorDiameter * 0.04f;
		eraserAppearance.outlineRed = 207.0f / 255.0f;
		eraserAppearance.outlineGreen = 207.0f / 255.0f;
		eraserAppearance.outlineBlue = 207.0f / 255.0f;
		window_.ConfigureDrawingCursor(DrawingTool::Eraser, eraserAppearance);
		const float laserSolidDiameter =
			kLaserSolidDiameterAt96Dpi * configuration_.dpiScale;
		DrawingCursorAppearance laserAppearance = {
			DrawingCursorShape::Circle,
			laserSolidDiameter,
			laserSolidDiameter,
			1.0f, 1.0f, 1.0f
		};
		laserAppearance.fillAlpha = 1.0f;
		laserAppearance.outlineWidth = 0.0f;
		window_.ConfigureDrawingCursor(DrawingTool::Laser, laserAppearance);
		renderer_.ConfigureLaserStyle(configuration_.dpiScale);
		renderer_.ConfigureShapePrimitives(configuration_.dpiScale);
		renderer_.ConfigureLaserParticles(
			configuration_.laserParticleConfig, configuration_.dpiScale);
		const bool performanceHudEnabled =
			performanceHudEnabled_.load(std::memory_order_acquire);
		window_.SetPerformanceHudEnabled(performanceHudEnabled);
		if (performanceHudEnabled)
			window_.UpdatePerformanceHudText(L"性能监控\r\n等待开始绘制……");
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

	void DrawingController::SetPerformanceHudEnabled(bool enabled) noexcept
	{
		performanceHudEnabled_.store(enabled, std::memory_order_release);
		performanceHudResetRequested_.store(true, std::memory_order_release);
		window_.SetPerformanceHudEnabled(enabled);
		input_.PublishControlWake();
	}

	bool DrawingController::GetPerformanceHudEnabled() const noexcept
	{
		return performanceHudEnabled_.load(std::memory_order_acquire);
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
		const bool succeeded = presentation_.Present(dirty, presentFull);
		lastPresentDurationMs_ = GetQpcTimeMilliseconds() - presentStartMs;
		lastPresentSucceeded_ = succeeded;
		if (metrics_) metrics_->RecordPresent(lastPresentDurationMs_);
		if (!succeeded) window_.RequestFullPresent(); // 呈现失败时下一轮强制全量刷新兜底。
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
			return false;
		}
		if (!presentation_.Resize(requestedSize.width, requestedSize.height)) // 再通知当前透明呈现器更新外部资源。
		{
			std::cout << "Failed to resize transparent presenter to " << requestedSize.width << "x" << requestedSize.height << std::endl;
			return false;
		}

		// 仅在两组资源都重建成功后提交逻辑尺寸。
		window_.CommitSize(requestedSize.width, requestedSize.height);
		if (presentAfterResize) PresentFullCanvas();
		return true;
	}

	void DrawingController::Run()
	{
		using namespace ink::stroke_model;
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
		std::vector<CanvasPageRuntimeState> pageRuntimeStates;
		pageRuntimeStates.reserve(8);
		pageRuntimeStates.emplace_back();
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
		active.reserve(kPreheatedStrokeCount);
		CanvasTouchGestureState touchGesture;
		CanvasPanMotionState panMotion;
		CanvasWindowsManipulation windowsManipulation;
		const bool windowsManipulationInitialized = windowsManipulation.Initialize();
		std::vector<CanvasGestureContactRuntime> gestureContacts;
		gestureContacts.reserve(kPreheatedStrokeCount);
		CanvasVector previousPanCentroid = {};
		bool panCentroidValid = false;
		size_t previousPanContactCount = 0;
		int64_t lastPanInputQpc = 0;
		int64_t lastNavigationQpc = 0;
		LaserTrailLifecycle laserLifecycle;
		float laserOpacity = 0.0f;
		RECT laserStableBounds = {};
		RECT laserLiveBounds = {};
		RECT pendingLaserBakeDirty = {};
		std::vector<LaserStrokeLayer> laserStrokeLayers;
		LaserCoverageMode laserCoverageMode = LaserCoverageMode::Inactive;
		LaserIncrementalDiagnostics laserDiagnostics;
		bool laserIncrementalEnsureAttempted = false;
		bool laserSummaryPending = false;
		bool laserSummaryWasIncremental = false;
		uint64_t nextLaserLayerId = 1;
		std::vector<LaserDot> laserTipDots;
		std::vector<ShapePrimitive> shapePrimitiveScratch;
		laserStrokeLayers.reserve(kLaserReservedContactCount);
		laserTipDots.reserve(kPreheatedStrokeCount + 1);
		shapePrimitiveScratch.reserve(kLaserReservedContactCount * 2);
		std::vector<LaserParticleEmissionRequest> laserParticleEmissionRequests;
		laserParticleEmissionRequests.reserve(kLaserReservedContactCount);
		HighlighterGeometry completedHighlighterScratch;
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
			CanvasTouchDisposition disposition)
		{
			if (!handle.record) return;
			const uint64_t key = CanvasTouchKey(handle);
			if (std::any_of(gestureContacts.begin(), gestureContacts.end(),
				[key](const CanvasGestureContactRuntime& contact)
				{ return contact.key == key; })) return;
			gestureContacts.push_back({
				handle, key, CanvasManipulatorId(handle), handle.record->DownSnapshot(),
				handle.record->DownSnapshot().sequence, disposition });
		};

		auto cancelTouchDrawingForPan = [&]()
		{
			for (RuntimeStroke* runtime : active)
			{
				if (!runtime || runtime->ended ||
					runtime->metricDeviceType != InputDeviceType::Touch) continue;
				addGestureContact(runtime->handle, CanvasTouchDisposition::Pan);
				runtime->handle = {}; // gesture runtime 接管 handle，直到真实 Up 才回收。
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

		auto interruptNavigationForPenOrMouse = [&]()
		{
			InterruptCanvasPanForDrawing(panMotion, touchGesture);
			windowsManipulation.Stop();
			for (CanvasGestureContactRuntime& contact : gestureContacts)
				contact.disposition = CanvasTouchDisposition::Suppressed;
			panCentroidValid = false;
			previousPanContactCount = 0;
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

		auto initializeStroke = [&](ContactHandle handle) -> bool
			{
				if (!handle.record || handle.record->Generation() != handle.generation) return false;
				const ContactSnapshot down = handle.record->DownSnapshot();
				const InputDeviceType deviceType = handle.record->DeviceType();
				if (deviceType == InputDeviceType::Pen ||
					deviceType == InputDeviceType::MouseLeft ||
					deviceType == InputDeviceType::MouseRight)
				{
					interruptNavigationForPenOrMouse();
				}
				else if (deviceType == InputDeviceType::Touch)
				{
					const bool blockingContactActive = std::any_of(
						active.begin(), active.end(), [](const RuntimeStroke* runtime)
						{
							return runtime && !runtime->ended && !runtime->awaitingReconnect &&
								runtime->metricDeviceType != InputDeviceType::Touch;
						});
					const uint64_t key = CanvasTouchKey(handle);
					const bool inheritedInertia = panMotion.inertiaActive;
					const CanvasTouchDecision touchDecision = touchGesture.OnTouchDown(
						key, down.qpc, qpcFrequency, inheritedInertia,
						blockingContactActive);
					if (touchDecision.disposition != CanvasTouchDisposition::Draw)
					{
						addGestureContact(handle, touchDecision.disposition);
						if (touchDecision.beginPan)
						{
							BeginCanvasPan(panMotion, inheritedInertia);
							lastPanInputQpc = down.qpc;
							windowsManipulation.Stop();
							if (touchDecision.cancelExistingTouchDrawing)
								cancelTouchDrawingForPan();
							std::vector<std::pair<MANIPULATOR_ID, CanvasVector>> contacts;
							contacts.reserve(gestureContacts.size());
							for (const CanvasGestureContactRuntime& contact : gestureContacts)
							{
								if (touchGesture.Disposition(contact.key) ==
									CanvasTouchDisposition::Pan)
									contacts.push_back({ contact.manipulatorId,
										{ contact.snapshot.position.x,
											contact.snapshot.position.y } });
							}
							windowsManipulation.Begin(contacts);
							panCentroidValid = false;
							previousPanContactCount = 0;
						}
						else if (touchDecision.joinedExistingPan)
						{
							windowsManipulation.Down(CanvasManipulatorId(handle),
								{ down.position.x, down.position.y });
							panCentroidValid = false;
						}
						return true;
					}
				}
				DrawingTool batchTool = window_.ActiveTool();
				bool hasActiveBatchContact = false;
				bool hasActiveLaserTouchContact = false;
				for (RuntimeStroke* activeRuntime : active)
				{
					if (activeRuntime && !activeRuntime->ended && !activeRuntime->awaitingReconnect)
					{
						if (!hasActiveBatchContact)
							batchTool = activeRuntime->selectedTool; // 仍有真实落笔时，新 contact 必须沿用当前批次工具。
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
					batchTool == DrawingTool::Pen || batchTool == DrawingTool::Highlighter ||
					IsShapeDrawingTool(batchTool);
				bool effectiveInvertedPenEraserEnabled = false;
				if constexpr (!kInterruptedStrokeReconnectManualTestModeEnabled)
					effectiveInvertedPenEraserEnabled =
						invertedPenEraserEnabled_.load(std::memory_order_acquire);
				const bool invertedEraser = ShouldUseInvertedPenEraser(deviceType,
					down.isInvertedCursor, effectiveInvertedPenEraserEnabled,
					selectedToolSupportsOverride);
				const DrawingTool tool = invertedEraser ? DrawingTool::Eraser : batchTool;
				const bool suppressPressure = deviceType == InputDeviceType::Pen && down.isInvertedCursor;
				const float downPressure = ResolveStylusPressureForModel(
					deviceType, down.isInvertedCursor, down.pressure);
				const StrokeWidthMode widthMode = tool == DrawingTool::Pen
					? ResolveStrokeWidthMode(deviceType,
						inputWidthModeSettings_.Get(), downPressure)
					: tool == DrawingTool::Laser && deviceType == InputDeviceType::Pen
						? StrokeWidthMode::LaserPressure : StrokeWidthMode::Fixed;
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
					IsInterruptedStrokeReconnectDeviceSupported(deviceType) &&
					tool != DrawingTool::Laser && !IsShapeDrawingTool(tool))
				{
					for (RuntimeStroke* candidate : active)
					{
						if (!candidate || !candidate->awaitingReconnect || candidate->ended) continue;
						if (!AreInterruptedStrokeReconnectIdentitiesCompatible(
							ReconnectIdentity(*candidate), downIdentity))
						{
							if constexpr (kInterruptedStrokeReconnectManualTestModeEnabled)
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
						if constexpr (kInterruptedStrokeReconnectManualTestModeEnabled)
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
				if constexpr (kInterruptedStrokeReconnectManualTestModeEnabled)
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
					inputTime = std::max(inputTime, reconnectRuntime->lastModelInputTime + 0.000001);
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
					if (absl::Status status = reconnectRuntime->stroke.modeler.Update(
						reconnectInput, reconnectRuntime->stroke.modeledResults); status.ok())
					{
						const double gapSeconds = reconnectResult.gapMilliseconds / 1000.0;
						const float alpha = std::clamp(static_cast<float>(
							1.0 - std::exp(-gapSeconds / kInputSpeedSmoothingSeconds)), 0.02f, 0.35f);
						reconnectRuntime->filteredInputSpeed +=
							(reconnectResult.bridgeSpeed - reconnectRuntime->filteredInputSpeed) * alpha;
						AppendNewModeledPoints(reconnectRuntime->stroke,
							reconnectRuntime->filteredInputSpeed);
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
						reconnectRuntime->lastModelSnapshot = modelDown;
						reconnectRuntime->lastConsumedSequence = down.sequence;
						reconnectRuntime->lastModelInputTime = inputTime;
						reconnectRuntime->lastPressure = lastPressure;
						reconnectRuntime->lastTilt = lastTilt;
						reconnectRuntime->lastOrientation = lastOrientation;
						reconnectRuntime->awaitingReconnect = false;
						reconnectRuntime->reconnectVisualRefresh = true;
						reconnectRuntime->deferredUpSnapshot = {};
						reconnectRuntime->reconnectDeadlineQpc = 0;
						reconnectRuntime->reconnectPredictedResults.clear();
						reconnectRuntime->metricEligibleQpc = down.qpc;
						reconnectRuntime->metricVisible = false;
						reconnectRuntime->movedThisFrame = true;
						reconnectRuntime->stroke.idleFrozen = false;
						reconnectRuntime->stroke.visualStableFrameCount = 0;
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
				runtime->selectedTool = batchTool; // 倒转覆盖不能污染同批后续 contact 的原始选择。
				runtime->tool = tool;
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
				const float baseDiameter = DiameterForTool(runtime->tool) *
					(runtime->tool == DrawingTool::Laser ? configuration_.dpiScale : 1.0f);
				const bool highlighter = runtime->tool == DrawingTool::Highlighter;
				runtime->stroke.Reset(baseDiameter, configuration_.expectedSpeed,
					widthMode, highlighter);
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
				if (absl::Status status = stroke.modeler.Update(downInput, stroke.modeledResults); !status.ok())
				{
					std::cout << "Error: " << status.message() << std::endl;
					ContactSnapshot cancelled = down;
					cancelled.phase = ContactPhase::Cancelled;
					input_.PublishCancelled(handle.record->TabletContextId(),
						handle.record->ContactId(), cancelled);
					input_.Recycle(handle);
					runtime->handle = {};
					runtime->inUse = false;
					return false;
				}
				if (runtime->shape.active)
					ExtractShapeModeledEndpoint(*runtime);
				else
					AppendNewModeledPoints(stroke);
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
						laserDiagnostics.Reset();
					}
					else if (laserLifecycle.phase != LaserTrailPhase::Active &&
						!laserStrokeLayers.empty())
					{
						// 同帧发生"最后 Up → 新 Down"时，也先把上一批按原顺序烘干。
						UnionRectInPlace(pendingLaserBakeDirty, laserLiveBounds);
						BakeLaserStrokeLayers(laserStrokeLayers, renderer_,
							configuration_.dpiScale, laserSize.width, laserSize.height,
							laserStableBounds, pendingLaserBakeDirty,
							laserCoverageMode, laserDiagnostics);
						LogLaserSummary(laserDiagnostics,
							laserCoverageMode == LaserCoverageMode::Incremental,
							particlesEnabledEffective);
						laserLiveBounds = {};
						laserCoverageMode = LaserCoverageMode::Inactive;
						laserDiagnostics.Reset();
					}
					else if (laserLifecycle.phase != LaserTrailPhase::Active)
					{
						// Hold/Fade 中的新 Down 保留已烘干颜色，但开启新的增量统计批次。
						laserCoverageMode = LaserCoverageMode::Inactive;
						laserDiagnostics.Reset();
					}
					runtime->laserLayerId = nextLaserLayerId++;
					LaserStrokeLayer layer{
						.id = runtime->laserLayerId, .runtime = runtime };
					laserStrokeLayers.push_back(std::move(layer));
					const LaserCoverageMode previousCoverageMode = laserCoverageMode;
					laserCoverageMode = SelectLaserCoverageMode(laserCoverageMode,
						laserStrokeLayers.size(),
						renderer_.LaserIncrementalCoverageAvailable());
					if (laserCoverageMode == LaserCoverageMode::FullRedraw &&
						previousCoverageMode != LaserCoverageMode::FullRedraw)
					{
						laserDiagnostics.fallbackReason = laserStrokeLayers.size() > 1
							? "multiple_contacts" : "resource_unavailable";
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
					return;
				}
				initializeStroke(ContactHandle{ record, record->Generation() }); // 出队后立即固定本地 generation。
			};

		auto appendTerminalFallback = [&](RuntimeStroke& runtime,
			const ContactSnapshot& snapshot, double inputTime)
			{
				const float radius = runtime.stroke.realPoints.empty()
					? runtime.stroke.inputStartPoint.r : runtime.stroke.realPoints.back().r;
				const InkPoint finalPoint{ snapshot.position.x, snapshot.position.y,
					radius, static_cast<float>(inputTime) };
				if (runtime.stroke.realPoints.empty())
					runtime.stroke.realPoints.push_back(finalPoint);
				else
				{
					const float deltaX = finalPoint.x - runtime.stroke.realPoints.back().x;
					const float deltaY = finalPoint.y - runtime.stroke.realPoints.back().y;
					if (deltaX * deltaX + deltaY * deltaY > 0.0001f)
						runtime.stroke.realPoints.push_back(finalPoint);
					else
						runtime.stroke.realPoints.back() = finalPoint;
				}
				// 模型异常也保留 RTS 的最终位置，不能因随后回收 contact 而吞掉 Up 点。
			};

		auto completeModelUp = [&](RuntimeStroke& runtime,
			const ContactSnapshot& snapshot, bool cancelled)
			{
				ContactSnapshot modelSnapshot = snapshot;
				if (runtime.suppressPressure) modelSnapshot.pressure = -1.0f;
				double inputTime = QpcDeltaSeconds(snapshot.qpc, runtime.qpcOrigin, qpcFrequency);
				inputTime = std::max(inputTime, runtime.lastModelInputTime + 0.000001);
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
				if (absl::Status status = runtime.stroke.modeler.Update(
					upInput, runtime.stroke.modeledResults); status.ok())
				{
					if (runtime.shape.active) ExtractShapeModeledEndpoint(runtime);
					else AppendNewModeledPoints(runtime.stroke);
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
				const bool deferUp = snapshot.phase == ContactPhase::Up &&
					GetInterruptedStrokeReconnectEnabled() &&
					IsInterruptedStrokeReconnectDeviceSupported(runtime.metricDeviceType) &&
					runtime.tool != DrawingTool::Laser && !runtime.shape.active;
				const bool positionMoved = distanceSquared > kRawMoveThresholdPx * kRawMoveThresholdPx;
				runtime.laserParticleMovedThisFrame = positionMoved;
				const bool stylusStateChanged = HasStylusStateChange(modelSnapshot, runtime.lastModelSnapshot);
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
				inputTime = std::max(inputTime, runtime.lastModelInputTime + 0.000001);
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
				if (absl::Status status = runtime.stroke.modeler.Update(
					input, runtime.stroke.modeledResults); status.ok())
				{
					modelUpdateSucceeded = true;
					if (runtime.shape.active) ExtractShapeModeledEndpoint(runtime);
					else AppendNewModeledPoints(runtime.stroke, inputSpeed);
				}
				else
				{
					std::cout << "Error: " << status.message() << std::endl;
					if (runtime.shape.active)
						runtime.shape.rawFallbackRequired = true;
					if (terminal && !deferUp && !runtime.shape.active)
						appendTerminalFallback(runtime, snapshot, inputTime);
				}
				runtime.lastModelSnapshot = modelSnapshot;
				if (positionMoved) runtime.lastSpeedSnapshot = snapshot;
				if (positionMoved || stylusStateChanged)
				{
					runtime.stroke.idleFrozen = false;
					runtime.stroke.visualStableFrameCount = 0;
					runtime.stroke.lastMovementInputTime = inputTime;
				}
				if (terminal && runtime.tool == DrawingTool::Laser)
				{
					const bool laserCancelled = snapshot.phase == ContactPhase::Cancelled;
					if (laserCancelled &&
						laserCoverageMode == LaserCoverageMode::Incremental)
					{
						// Cancel 必须脏化旧 coverage；切回完整路径后本帧会重建底图且跳过该层。
						laserCoverageMode = LaserCoverageMode::FullRedraw;
						laserDiagnostics.fallbackReason = "cancelled";
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
				return positionMoved || stylusStateChanged || shapeRawChanged || deferUp;
			};

		auto updateCanvasNavigation = [&](int64_t nowQpc)
		{
			DrawingCursorSample navigationPenSample;
			const bool penInRange = window_.ReadPenCursorSample(navigationPenSample) &&
				navigationPenSample.valid;
			DrawingCursorSample navigationMouseSample;
			const bool mouseInContact = window_.ReadMouseCursorSample(navigationMouseSample) &&
				navigationMouseSample.valid && navigationMouseSample.inContact;
			if ((penInRange && navigationPenSample.inContact) || mouseInContact)
			{
				// RTS 先发布接触光标；利用该原子通道在 Pen contact 出队前立即刹停。
				interruptNavigationForPenOrMouse();
			}
				touchGesture.Update(nowQpc, qpcFrequency);
			bool topologyChanged = false;
			bool panInputUpdated = false;
			int64_t newestPanInputQpc = 0;
			for (CanvasGestureContactRuntime& contact : gestureContacts)
			{
				ContactSnapshot snapshot;
				if (!input_.TryReadSnapshot(contact.handle, snapshot) ||
					snapshot.sequence == contact.lastConsumedSequence) continue;
				contact.lastConsumedSequence = snapshot.sequence;
				contact.snapshot = snapshot;
				contact.disposition = touchGesture.Disposition(contact.key);
				const CanvasVector point{ snapshot.position.x, snapshot.position.y };
				if (snapshot.phase == ContactPhase::Move)
				{
					if (contact.disposition == CanvasTouchDisposition::Pan)
					{
						windowsManipulation.Move(contact.manipulatorId, point);
						panInputUpdated = true;
						newestPanInputQpc = (std::max)(newestPanInputQpc, snapshot.qpc);
					}
				}
				else if (snapshot.phase == ContactPhase::Up ||
					snapshot.phase == ContactPhase::Cancelled)
				{
					const CanvasTouchDisposition endedDisposition =
						touchGesture.OnTouchUp(contact.key);
					if (endedDisposition == CanvasTouchDisposition::Pan)
					{
						windowsManipulation.Up(contact.manipulatorId, point);
						topologyChanged = true;
					}
				}
			}

			std::erase_if(gestureContacts, [&](CanvasGestureContactRuntime& contact)
				{
					if (contact.snapshot.phase != ContactPhase::Up &&
						contact.snapshot.phase != ContactPhase::Cancelled) return false;
					input_.Recycle(contact.handle);
					return true;
				});

			CanvasVector contentDelta = {};
			if (touchGesture.PanActive())
			{
				CanvasVector centroid = {};
				size_t count = 0;
				for (const CanvasGestureContactRuntime& contact : gestureContacts)
				{
					if (touchGesture.Disposition(contact.key) != CanvasTouchDisposition::Pan)
						continue;
					centroid.x += contact.snapshot.position.x;
					centroid.y += contact.snapshot.position.y;
					++count;
				}
				if (count > 0)
				{
					centroid.x /= static_cast<float>(count);
					centroid.y /= static_cast<float>(count);
					if (!panCentroidValid || topologyChanged || count != previousPanContactCount)
					{
						previousPanCentroid = centroid;
						panCentroidValid = true;
						previousPanContactCount = count;
					}
					else if (panInputUpdated)
					{
						const CanvasVector centroidDelta{
							centroid.x - previousPanCentroid.x,
							centroid.y - previousPanCentroid.y };
						const double deltaSeconds = lastPanInputQpc > 0
							? QpcDeltaSeconds(newestPanInputQpc,
								lastPanInputQpc, qpcFrequency) : 0.0;
						contentDelta = UpdateCanvasPan(panMotion, centroidDelta,
							deltaSeconds > 0.0 ? deltaSeconds :
								1.0 / configuration_.timingProfile.target_fps);
						previousPanCentroid = centroid;
					}
					if (panInputUpdated && newestPanInputQpc > 0)
						lastPanInputQpc = newestPanInputQpc;
				}
			}
			else if (panMotion.inertiaActive)
			{
				const bool accelerateStop = penInRange ||
					touchGesture.InertiaBrakeRequested();
				bool completed = false;
				if (windowsManipulationInitialized && windowsManipulation.Available())
				{
					const bool nativeStepSucceeded = windowsManipulation.StepInertia(
						accelerateStop, contentDelta, completed);
					if (nativeStepSucceeded && !completed)
					{
						const double deltaSeconds = QpcDeltaSeconds(
							nowQpc, lastNavigationQpc, qpcFrequency);
						// GetTickCount 毫秒量化可能产生零 delta；未完成时仍保留锁存速度。
						if (deltaSeconds > 0.0 &&
							(contentDelta.x != 0.0f || contentDelta.y != 0.0f))
							SetCanvasPanVelocity(panMotion, {
								static_cast<float>(contentDelta.x / deltaSeconds),
								static_cast<float>(contentDelta.y / deltaSeconds) });
					}
					else if (nativeStepSucceeded) StopCanvasPan(panMotion);
					else
					{
						// 原生 processor 运行中失败时保留当前速度，继续同参数 CPU 惯性。
						const double deltaSeconds = QpcDeltaSeconds(
							nowQpc, lastNavigationQpc, qpcFrequency);
						contentDelta = StepCanvasPanInertia(panMotion,
							deltaSeconds > 0.0 ? deltaSeconds :
								1.0 / configuration_.timingProfile.target_fps, accelerateStop);
					}
				}
				else
				{
					const double deltaSeconds = QpcDeltaSeconds(
						nowQpc, lastNavigationQpc, qpcFrequency);
					contentDelta = StepCanvasPanInertia(panMotion,
						deltaSeconds > 0.0 ? deltaSeconds :
							1.0 / configuration_.timingProfile.target_fps, accelerateStop);
				}
			}

			if ((contentDelta.x != 0.0f || contentDelta.y != 0.0f) && document_)
			{
				InkPage* page = document_->PageAt(currentPageIndex_);
				InkCanvas* canvas = page ? page->FindCanvas(kDefaultDeviceKey) : nullptr;
				if (canvas)
				{
					CanvasViewportState viewport{ canvas->Viewport().x, canvas->Viewport().y };
					const CanvasVector applied = ApplyCanvasContentTranslation(
						viewport, contentDelta);
					if (std::abs(applied.x + contentDelta.x) > 0.001f)
						panMotion.velocity.x = 0.0f;
					if (std::abs(applied.y + contentDelta.y) > 0.001f)
						panMotion.velocity.y = 0.0f;
					if ((applied.x != 0.0f || applied.y != 0.0f) &&
						canvas->SetViewport({ viewport.x, viewport.y, 1.0f }))
					{
						viewportRefreshPending = true;
						historyGpuCache.DiscardHotPreimages();
					}
				}
			}

			if (!touchGesture.PanActive() && panCentroidValid)
			{
				panCentroidValid = false;
				previousPanContactCount = 0;
				if (windowsManipulationInitialized && windowsManipulation.Available())
				{
					const CanvasVector systemVelocity =
						windowsManipulation.VelocityDipPerSecond();
					if (panMotion.inheritedBlendRemainingSeconds <= 0.0 &&
						(systemVelocity.x != 0.0f || systemVelocity.y != 0.0f))
						SetCanvasPanVelocity(panMotion, systemVelocity);
					const double secondsSinceLastInput = lastPanInputQpc > 0
						? QpcDeltaSeconds(nowQpc, lastPanInputQpc, qpcFrequency)
						: (std::numeric_limits<double>::infinity)();
					EndCanvasPan(panMotion, secondsSinceLastInput);
					// StartInertia 失败时保留 motion，由下一帧 CPU fallback 接管。
					if (panMotion.inertiaActive)
						windowsManipulation.StartInertia(panMotion.velocity);
				}
				else
				{
					const double secondsSinceLastInput = lastPanInputQpc > 0
						? QpcDeltaSeconds(nowQpc, lastPanInputQpc, qpcFrequency)
						: (std::numeric_limits<double>::infinity)();
					EndCanvasPan(panMotion, secondsSinceLastInput);
				}
			}
			lastNavigationQpc = nowQpc;
		};

		bool timerPeriodActive = false;
		bool timerPeriodAttempted = false;
		double lastActiveFrameStartMs = 0.0;
		bool hapticContinuousActive = false;
		std::vector<DrawingCursorVisual> previousCursorVisuals;
		std::vector<DrawingCursorVisual> currentCursorVisuals;
		previousCursorVisuals.reserve(kPreheatedStrokeCount + 1);
		currentCursorVisuals.reserve(kPreheatedStrokeCount + 1);

		auto buildDrawingCursorVisuals = [&]()
		{
			currentCursorVisuals.clear();
			laserTipDots.clear();
			DrawingCursorSample penSample;
			DrawingCursorSample mouseSample;
			window_.ReadPenCursorSample(penSample);
			window_.ReadMouseCursorSample(mouseSample);
			const DrawingTool cursorTool = window_.EffectiveDrawingCursorTool();
			const bool mouseUsesSystemCursor = window_.GetMouseUsesSystemCursor();
			if (cursorTool == DrawingTool::Laser)
			{
				const DrawingCursorVisual primary = ResolveLaserDrawingCursorVisual(
					penSample, mouseSample, window_.CursorPointerAuthority(),
					window_.CursorAppearanceForTool(DrawingTool::Laser));
				if (primary.visible)
					laserTipDots.push_back({ primary.x, primary.y,
						primary.appearance.width * 0.5f, primary.appearance.opacity });
			}
			else
			{
				const DrawingCursorVisual primary = ResolvePrimaryDrawingCursorVisual(
					penSample, mouseSample, window_.CursorPointerAuthority(),
					window_.CursorAppearanceForTool(cursorTool),
					window_.CursorAppearanceForTool(DrawingTool::Eraser),
					cursorTool == DrawingTool::Eraser,
					drawingCursorDuringContactEnabled_.load(std::memory_order_acquire),
					translucentInkCursorEnabled_.load(std::memory_order_acquire),
					mouseUsesSystemCursor);
				if (primary.visible) currentCursorVisuals.push_back(primary);
			}

			const DrawingCursorAppearance eraserAppearance =
				window_.CursorAppearanceForTool(DrawingTool::Eraser);
			for (const RuntimeStroke* runtime : active)
			{
				if (runtime && !runtime->ended && !runtime->awaitingReconnect &&
					runtime->metricDeviceType == InputDeviceType::Touch &&
					runtime->tool == DrawingTool::Laser)
				{
					const ContactSnapshot& snapshot = runtime->lastModelSnapshot;
					laserTipDots.push_back({ snapshot.position.x, snapshot.position.y,
						LaserSolidRadius(configuration_.dpiScale), 1.0f });
					continue;
				}
				if (!runtime || runtime->ended || runtime->awaitingReconnect ||
					runtime->metricDeviceType != InputDeviceType::Touch ||
					runtime->tool != DrawingTool::Eraser) continue;
				const ContactSnapshot& snapshot = runtime->lastModelSnapshot;
				const DrawingCursorVisual touchVisual = MakeTouchEraserDrawingCursorVisual(
					snapshot.position.x, snapshot.position.y, eraserAppearance);
				if (touchVisual.visible) currentCursorVisuals.push_back(touchVisual);
			}
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
			laserSummaryPending = false;
			laserDiagnostics.Reset();
			renderer_.ResetLaserParticles();
			laserParticleDirtyTracker.Clear();
			particleSnapshot = {};
			lastLaserParticleSimulationQpc = 0;
			previousLaserParticleBounds = {};
			previousLaserTipBounds = {};
			forceFullPresent = true;
		};

			auto undoCurrentPage = [&](RECT& frameDirty)
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
				return;
			}
			InkPage* page = document_->PageAt(currentPageIndex_);
			InkCanvas* canvas = page
				? page->FindCanvas(kDefaultDeviceKey) : nullptr;
			CanvasPageRuntimeState& runtime = pageRuntimeStates[currentPageIndex_];
			const std::optional<RenderItemId> itemId = runtime.history.LastVisibleItem();
			if (!page || !canvas || !itemId)
			{
				std::cout << "[Undo] page=" << (currentPageIndex_ + 1) <<
					" result=noop reason=empty" << std::endl;
				return;
			}
			const RenderItemState* item = runtime.history.Find(*itemId);
			if (!item || itemId->index >= runtime.beforeStates.size())
			{
				std::cout << "[Undo] page=" << (currentPageIndex_ + 1) <<
					" result=noop reason=history_mismatch" << std::endl;
				return;
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
					return;
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
					return;
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
					return;
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
		};

		auto processCanvasCommands = [&](RECT& frameDirty,
			LaserParticleDirtySnapshot& particleSnapshot,
			bool& forceFullPresent, int width, int height)
		{
			CanvasCommand command;
			while (active.empty() && window_.TryDequeueCanvasCommand(command))
			{
				// 页面、撤回和键盘平移都以命令时刻的固定视口为起点。
				interruptNavigationForPenOrMouse();
				if (command.type == CanvasCommandType::Undo)
				{
					renderer_.InvalidateTrustedL2Snapshot();
					trustedSnapshotSignatureValid = false;
					undoCurrentPage(frameDirty);
					continue;
				}
				if (!document_) continue;
				if (command.type == CanvasCommandType::TranslateViewport)
				{
					InkPage* page = document_->PageAt(currentPageIndex_);
					InkCanvas* canvas = page
						? page->FindCanvas(kDefaultDeviceKey) : nullptr;
					if (!canvas) continue;
					CanvasViewportState next{ canvas->Viewport().x, canvas->Viewport().y };
					const CanvasVector applied = ApplyCanvasContentTranslation(
						next, { command.deltaX, command.deltaY });
					if (applied.x == 0.0f && applied.y == 0.0f) continue;
					if (!canvas->SetViewport({ next.x, next.y, 1.0f })) continue;
					// 视口改变后屏幕热像失效；Canvas-local composition cache 继续复用。
					historyGpuCache.DiscardHotPreimages();
					viewportRefreshPending = true;
					viewportRefreshClearsTransient = true;
					forceFullPresent = true;
					std::cout << "[Viewport] x=" << next.x << " y=" << next.y <<
						" path=budgeted" << std::endl;
					continue;
				}
				renderer_.InvalidateTrustedL2Snapshot();
				trustedSnapshotSignatureValid = false;
				const size_t pageCount = document_->Pages().size();
				size_t targetPageIndex = currentPageIndex_;
				const char* action = nullptr;
				const char* key = command.type == CanvasCommandType::NextPage ? "0" : "8";
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
							continue;
						}
						targetPageIndex = *appended;
						action = "append";
					}
				}
				else if (currentPageIndex_ > 0)
				{
					targetPageIndex = currentPageIndex_ - 1;
					action = "previous";
				}
				else
				{
					std::cout << "[Page] key=8 result=noop reason=first current=1 count=" <<
						pageCount << std::endl;
					continue;
				}

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
			}
		};
		// 预热所有激光着色器路径，消除首笔落下时 Qualcomm/Adreno 等 GPU 驱动的 JIT 编译卡顿。
		renderer_.WarmUpLaserShaders();
		renderer_.WarmUpShapeShaders();
		while (true)
		{
			const double frameStartMs = GetQpcTimeMilliseconds();
			const bool performanceHudEnabled =
				performanceHudEnabled_.load(std::memory_order_acquire);
			if (metrics_) metrics_->BeginFrame();
			if (performanceHudEnabled &&
				performanceHudResetRequested_.exchange(false, std::memory_order_acq_rel))
			{
				performanceHudTracker_.Reset();
				window_.UpdatePerformanceHudText(
					L"性能监控\r\n等待开始绘制……");
			}
			lastPresentDurationMs_ = 0.0;
			lastPresentSucceeded_ = false;
			if (!laserIncrementalEnsureAttempted &&
				window_.ActiveTool() == DrawingTool::Laser)
			{
				laserIncrementalEnsureAttempted = true;
				if constexpr (kLaserIncrementalDiagnosticsEnabled)
				{
					const double resourceStartMs = GetQpcTimeMilliseconds();
					const bool available = renderer_.EnsureLaserIncrementalCoverageResources();
					std::cout << "[LaserPerf] resource_create available=" <<
						(available ? "true" : "false") << " cpu_ms=" <<
						(GetQpcTimeMilliseconds() - resourceStartMs) << std::endl;
				}
				else
				{
					renderer_.EnsureLaserIncrementalCoverageResources();
				}
			}
			if (active.empty()) window_.ClearActiveDrawingCursorTool();
			const bool drawingCursorRequested = window_.ConsumeDrawingCursorRenderRequest();
			const double previousFrameMs = lastActiveFrameStartMs > 0.0
				? frameStartMs - lastActiveFrameStartMs : 0.0;
			ContactRecord* record = nullptr;

			bool forceFullPresent = false;
			RECT viewportRecoveryDirty = {};
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
				presentation_.RefreshAfterCompositionChanged();
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
				if (resizedCoverageMode == LaserCoverageMode::FullRedraw &&
					laserCoverageMode == LaserCoverageMode::Incremental)
				{
					laserDiagnostics.fallbackReason = "resize_resource_unavailable";
					if constexpr (kLaserIncrementalDiagnosticsEnabled)
						std::cout << "[LaserPerf] resource_resize available=false fallback=full_redraw" <<
							std::endl;
				}
				laserCoverageMode = resizedCoverageMode;
				forceFullPresent = true; // Resize 保留 L2，并从 CPU 状态恢复共享 L1/L0。
			}
			DrawingCursorSample priorityPenSample;
			DrawingCursorSample priorityMouseSample;
			const bool priorityPenInContact =
				window_.ReadPenCursorSample(priorityPenSample) &&
				priorityPenSample.valid && priorityPenSample.inContact;
			const bool priorityMouseInContact =
				window_.ReadMouseCursorSample(priorityMouseSample) &&
				priorityMouseSample.valid && priorityMouseSample.inContact;
			const bool navigationInProgress = touchGesture.PanActive() ||
				touchGesture.InertiaCandidateActive() || panMotion.inertiaActive ||
				!gestureContacts.empty();
			if (ShouldPrioritizeDrawingContact(navigationInProgress,
				priorityPenInContact, priorityMouseInContact))
			{
				// 真实 Down 先固定视口并创建 runtime，不能等导航与 Tile 恢复结束。
				while (input_.TryDequeue(record)) processCommand(record);
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
					haptics_->StopFeedback();
				uint32_t pointerId = 0;
				bool pointerEraserHint = false;
				if (window_.ConsumeHapticPointerId(pointerId, pointerEraserHint))
				{
					if (haptics_->AttachPointerId(pointerId))
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
				laserSummaryPending = false;
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
				!laserFadeActive && !particleAnimationActive && IsEmptyRect(frameDirty) &&
				!compositionMaintenance.empty())
			{
				// 每个 tile 之间先检查输入，避免后台预建拉长下一笔 Down 的排队时间。
				if (input_.TryDequeue(record))
				{
					processCommand(record);
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
				!laserFadeActive && !particleAnimationActive && IsEmptyRect(frameDirty))
			{
				if (hapticContinuousActive && haptics_)
				{
					haptics_->StopFeedback();
					hapticContinuousActive = false;
				}
				if (metrics_) metrics_->BeginIdle(frameStartMs);
				lastActiveFrameStartMs = 0.0;
				if (timerPeriodActive)
				{
					timeEndPeriod(1);
					timerPeriodActive = false;
				}
				timerPeriodAttempted = false;
				if (laserLifecycle.phase == LaserTrailPhase::Hold)
				{
					if (input_.TryDequeue(record))
					{
						processCommand(record);
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
						processCommand(record);
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
					processCommand(record);
					if (metrics_) metrics_->EndIdle(GetQpcTimeMilliseconds());
					continue;
				}
				input_.WaitDequeue(record);
				processCommand(record);
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
			if (!interruptedStrokeReconnectEnabled)
			{
				while (input_.TryDequeue(record)) processCommand(record);
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
				completeModelUp(*oldest, oldest->deferredUpSnapshot, false);
				hasEndedStroke = true;
				--reconnectEvictionCount;
			}

			if (interruptedStrokeReconnectEnabled)
				while (input_.TryDequeue(record)) processCommand(record);
			for (RuntimeStroke* runtime : active)
			{
				if (!runtime || !runtime->awaitingReconnect ||
					(interruptedStrokeReconnectEnabled &&
						!IsInterruptedStrokeReconnectExpired(
							runtime->reconnectDeadlineQpc, frameQpc.QuadPart))) continue;
				completeModelUp(*runtime, runtime->deferredUpSnapshot, false);
				hasEndedStroke = true;
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
			if ((hasPhysicalContact || laserLifecycle.phase == LaserTrailPhase::Fade ||
				laserParticleSnapshot.hasActive) &&
				!timerPeriodAttempted)
			{
				timerPeriodAttempted = true;
				timerPeriodActive = timeBeginPeriod(1) == TIMERR_NOERROR; // 只为成功的请求配对 timeEndPeriod。
			}
			const auto frameToolIterator = std::find_if(active.begin(), active.end(),
				[](const RuntimeStroke* runtime)
					{ return runtime && !runtime->ended && !runtime->awaitingReconnect; });
			const DrawingTool frameTool = frameToolIterator != active.end()
				? (*frameToolIterator)->tool
				: active.empty() ? window_.ActiveTool() : active.front()->tool;
			const DrawingCursorPointerAuthority cursorAuthority =
				window_.CursorPointerAuthority();
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
			if (hasLaserRuntime)
			{
				++laserDiagnostics.activeFrames;
				laserDiagnostics.maximumLayerCount = std::max(
					laserDiagnostics.maximumLayerCount, laserStrokeLayers.size());
			}
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
							laserDiagnostics.maximumVisiblePoints = std::max(
								laserDiagnostics.maximumVisiblePoints,
								coverageVisiblePoints.size());
							if (laserCoverageMode == LaserCoverageMode::Incremental)
							{
								RECT coverageDirty = {};
								const bool coverageUpdated = UpdateLaserIncrementalCoverage(
									*layer, coverageRealPoints, coverageVisiblePoints,
									configuration_.liveTipDurationSeconds +
									GetPredictionDurationSeconds(stroke), renderer_,
									configuration_.dpiScale, size.width, size.height,
									laserDiagnostics, coverageDirty);
								UnionRectInPlace(frameDirty, coverageDirty);
								UnionRectInPlace(runtime->visibleDirty, coverageDirty);
								if (!coverageUpdated)
								{
									// 增量提交失败时不推进游标，清空 scratch 并锁定本批完整重绘。
									laserCoverageMode = LaserCoverageMode::FullRedraw;
									laserDiagnostics.fallbackReason = "coverage_submission_failed";
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
				const bool highlighter = runtime->tool == DrawingTool::Highlighter;
				if (!eraser)
					otherLiveLayerNeedsRebuild = true;
				// 保护窗口仍按配置 live-tip 时长推进 L1；taper 仅在非硬件压感普通笔上叠加。
				const double liveTipProtectionSeconds =
					eraser || highlighter ? 0.0 : configuration_.liveTipDurationSeconds;
				const double liveTipTaperSeconds = eraser || highlighter
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
						if (!eraser)
							stroke.predictedResults.assign(
								runtime->reconnectPredictedResults.begin(),
								runtime->reconnectPredictedResults.end());
					}
					else if (!eraser &&
						kActivePredictionMode != InkPredictionMode::Disabled)
					{
						if (absl::Status status = stroke.modeler.Predict(stroke.predictedResults); !status.ok())
							stroke.predictedResults.clear();
					}
					RebuildPredictedPoints(stroke);
					stableDirty = eraser
						? CommitEraserRealPointsToL1(stroke, StrokeShape::RoundCapsule,
							renderer_, size.width, size.height)
						: CommitStablePrefixToL1(stroke, liveTipProtectionSeconds,
							GetPredictionDurationSeconds(stroke), ColorForTool(runtime->tool),
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
					UpdateIdleFreezeState(stroke, runtime->movedThisFrame,
						liveTipProtectionSeconds);
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
					frameDirty, laserCoverageMode, laserDiagnostics);
				UnionRectInPlace(frameDirty, laserLiveBounds);
				laserLiveBounds = {};
				laserSummaryWasIncremental =
					laserCoverageMode == LaserCoverageMode::Incremental;
				laserSummaryPending = true;
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
							StoredStyleForTool(runtime->tool);
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
						}
						InkPage* page = document_ ? document_->PageAt(currentPageIndex_) : nullptr;
						InkCanvas* canvas = page
							? page->FindCanvas(kDefaultDeviceKey) : nullptr;
						const std::optional<size_t> strokeIndex = finalizedStroke && canvas
							? canvas->AppendStroke(std::move(*finalizedStroke)) : std::nullopt;
						if (strokeIndex)
						{
							// 文档对象先成为真值，再从刚追加的同一 Stroke 完成首次 L2 绘制。
							const std::span<const InkStroke> strokes = canvas->Strokes();
							const InkStroke& storedStroke = strokes[*strokeIndex];
							CanvasPageRuntimeState& pageRuntime =
								pageRuntimeStates[currentPageIndex_];
							std::optional<StrokeTileFootprint> footprint =
								BuildStrokeTileFootprint(storedStroke);
							const std::optional<RenderItemId> renderItem = footprint
								? pageRuntime.history.AppendStroke(
									*strokeIndex, std::move(*footprint), true) : std::nullopt;
							HotPreimageCaptureResult preimageCapture;
							InkRasterStateToken afterState = pageRuntime.rasterState;
							if (renderItem && renderItem->index == pageRuntime.beforeStates.size())
							{
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
						if (metrics_ && runtime->metricVisible && !runtime->cancelled)
							metrics_->StageLanding(runtime->handle.record, runtime->handle.generation,
								runtime->metricDeviceType, static_cast<uint32_t>(runtime->tool),
								runtime->metricEligibleQpc);
						input_.Recycle(runtime->handle); // L2 提交与活动层重建完成后才归还 slot。
						runtime->stroke.Reset(kPenDiameter, configuration_.expectedSpeed);
						runtime->handle = {};
						runtime->selectedTool = DrawingTool::Pen;
						runtime->tool = DrawingTool::Pen;
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
							DrawL0LiveComposite(runtime->stroke, ColorForTool(runtime->tool),
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
				laserTipDots, configuration_.dpiScale, size.width, size.height);
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
			if (laserLifecycle.phase != LaserTrailPhase::Inactive || laserSummaryPending)
			{
				const uint64_t dirtyPixels = RectArea(frameDirty);
				laserDiagnostics.dirtyPixels += dirtyPixels;
				laserDiagnostics.peakDirtyPixels = std::max(
					laserDiagnostics.peakDirtyPixels, dirtyPixels);
			}
			if (laserSummaryPending)
			{
				LogLaserSummary(laserDiagnostics, laserSummaryWasIncremental,
					particlesEnabledEffective);
				laserSummaryPending = false;
				laserDiagnostics.Reset();
			}
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
					renderer_.DrawLaserParticles();
				if (laserLifecycle.phase != LaserTrailPhase::Inactive && laserOpacity > 0.0f)
				{
					renderer_.ResolveLaserCompositedColor(
						renderer_.backBufferRTV.Get(), frameDirty, laserOpacity);
					DrawLaserStrokeLayers(laserStrokeLayers, renderer_,
						renderer_.backBufferRTV.Get(), frameDirty,
						laserCoverageMode, laserDiagnostics);
				}
				renderer_.DrawLaserDots(laserTipDots);
				for (const DrawingCursorVisual& visual : currentCursorVisuals)
					renderer_.DrawTransientDrawingCursor(visual);
				presentSucceeded = PresentFrame(
					frameDirty, forceFullPresent); // 一帧最多一次 backbuffer 合成和一次 Present。
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

			if (kLaserIncrementalDiagnosticsEnabled &&
				laserLifecycle.phase != LaserTrailPhase::Inactive)
			{
				const double laserFrameWorkMs = GetQpcTimeMilliseconds() - frameStartMs;
				if (laserFrameWorkMs >= 8.0)
				{
					const bool incrementalFrame =
						laserCoverageMode == LaserCoverageMode::Incremental ||
						(laserCoverageMode == LaserCoverageMode::Inactive &&
							laserSummaryWasIncremental);
					std::cout << "[LaserPerf] SlowFrame workMs=" << laserFrameWorkMs <<
						" presentMs=" << lastPresentDurationMs_ <<
						" presentOk=" << (lastPresentSucceeded_ ? 1 : 0) <<
						" dirty=[" << frameDirty.left << ',' << frameDirty.top << ',' <<
						frameDirty.right << ',' << frameDirty.bottom << ']' <<
						" mode=" << (incrementalFrame ? "incremental" : "fallback") <<
						" particles=" << (particlesEnabledEffective ? 1 : 0) << std::endl;
				}
			}

			const double canvasFrameElapsedMilliseconds =
				GetQpcTimeMilliseconds() - frameStartMs;
			previousCanvasFrameWorkMilliseconds = (std::max)(0.0,
				canvasFrameElapsedMilliseconds - lastPresentDurationMs_);
			previousCanvasPresentMilliseconds = lastPresentDurationMs_;
			const bool hasPhysicalContactAfterFrame = HasPhysicalContact(active);
			if (metrics_ && !hasPhysicalContactAfterFrame)
				metrics_->EndActiveFrameSequence();
			if (!hasPhysicalContactAfterFrame && performanceHudEnabled)
				performanceHudTracker_.EndDrawingFrameSequence();
			if (hasPhysicalContactAfterFrame)
			{
				const double workMs = GetQpcTimeMilliseconds() - frameStartMs;
				bool performanceHudSnapshotReady = false;
				if (frameHadActiveContact && performanceHudEnabled)
					performanceHudSnapshotReady = performanceHudTracker_.RecordDrawingFrame(
						frameStartMs, workMs, lastPresentDurationMs_, lastPresentSucceeded_);
				if (performanceHudSnapshotReady)
					window_.UpdatePerformanceHudText(performanceHudTracker_.FormatText());
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
			else if (navigationActive || laserLifecycle.phase == LaserTrailPhase::Fade ||
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
				if (timerPeriodActive)
				{
					timeEndPeriod(1);
					timerPeriodActive = false;
				}
				timerPeriodAttempted = false;
				int64_t nearestDeadlineQpc = (std::numeric_limits<int64_t>::max)();
				for (const RuntimeStroke* runtime : active)
				{
					if (runtime && runtime->awaitingReconnect)
						nearestDeadlineQpc = std::min(
							nearestDeadlineQpc, runtime->reconnectDeadlineQpc);
				}
				LARGE_INTEGER waitStartQpc = {};
				QueryPerformanceCounter(&waitStartQpc);
				const double timeoutMilliseconds = nearestDeadlineQpc ==
					(std::numeric_limits<int64_t>::max)()
					? 0.0
					: QpcDeltaSeconds(nearestDeadlineQpc,
						waitStartQpc.QuadPart, qpcFrequency) * 1000.0;
				if (metrics_) metrics_->BeginIdle(GetQpcTimeMilliseconds());
				input_.WaitForWake(frameWakeGeneration, timeoutMilliseconds);
				if (metrics_) metrics_->EndIdle(GetQpcTimeMilliseconds());
			}
		}

		if (metrics_) metrics_->EndIdle(GetQpcTimeMilliseconds());
		if (haptics_) haptics_->StopFeedback();
		if (timerPeriodActive) timeEndPeriod(1);
		if (drawingPriorityRaised)
			SetThreadPriority(GetCurrentThread(), originalThreadPriority);
	}

}
