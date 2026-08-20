module;

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>

#include "../../../IdtAtomic.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <mutex>
#include <optional>
#include <string_view>
#include <utility>

export module Inkeys.UI.Bar.Animation;

using namespace std;

export
{
	extern IdtAtomic<double> BarUiDefaultDes;
	extern IdtAtomic<double> BarUiDefaultOperationDur;
	extern IdtAtomic<bool> BarUiAnimationEnabled;
	extern IdtAtomic<double> BarUiAnimationSpeedRate;

	// 动效类型
	enum class BarUiValueModeEnum : int
	{
		Once = 0, // 无动画
		Linear = 1, // 兼容旧类型；实际插值由 curve 决定
		Variable = 2 // 兼容旧类型；需要回弹时使用 Back 曲线
	};

	// 动画曲线只负责将线性时间进度映射为数值进度，不改变统一批次的结束时刻。
	enum class BarUiCurveEnum : int
	{
		Linear = 0,
		EaseInSine,
		EaseOutSine,
		EaseInOutSine,
		EaseInCubic,
		EaseOutCubic,
		EaseInOutCubic,
		EaseInBack,
		EaseOutBack,
		EaseInOutBack,
	};

	inline bool BarUiIsBackCurve(BarUiCurveEnum curve)
	{
		return curve == BarUiCurveEnum::EaseInBack
			|| curve == BarUiCurveEnum::EaseOutBack
			|| curve == BarUiCurveEnum::EaseInOutBack;
	}
	struct BarUiCurveExtremaClass
	{
		double minimum = 0.0;
		double maximum = 1.0;
	};
	inline BarUiCurveExtremaClass BarUiGetCurveExtrema(
		BarUiCurveEnum curve) noexcept
	{
		constexpr double back = 1.1;
		auto EaseInMinimum = [](double coefficient)
			{
				double progress = 2.0 * coefficient
					/ (3.0 * (coefficient + 1.0));
				return (coefficient + 1.0) * progress * progress * progress
					- coefficient * progress * progress;
			};
		switch (curve)
		{
		case BarUiCurveEnum::EaseInBack:
			return { EaseInMinimum(back), 1.0 };
		case BarUiCurveEnum::EaseOutBack:
			return { 0.0, 1.0 - EaseInMinimum(back) };
		case BarUiCurveEnum::EaseInOutBack:
		{
			const double minimum = EaseInMinimum(back * 1.525) / 2.0;
			return { minimum, 1.0 - minimum };
		}
		default:
			return {};
		}
	}
	inline double BarUiApplyCurve(BarUiCurveEnum curve, double progress)
	{
		constexpr double pi = 3.14159265358979323846;
		constexpr double back = 1.1; // 克制的 UI 回弹强度
		progress = clamp(progress, 0.0, 1.0);
		switch (curve)
		{
		case BarUiCurveEnum::EaseInSine: return 1.0 - cos(progress * pi / 2.0);
		case BarUiCurveEnum::EaseOutSine: return sin(progress * pi / 2.0);
		case BarUiCurveEnum::EaseInOutSine: return -(cos(pi * progress) - 1.0) / 2.0;
		case BarUiCurveEnum::EaseInCubic: return progress * progress * progress;
		// 固定二、三次幂直接展开，避免逐属性逐帧进入通用 pow。
		case BarUiCurveEnum::EaseOutCubic:
		{
			double inverseProgress = 1.0 - progress;
			return 1.0 - inverseProgress * inverseProgress * inverseProgress;
		}
		case BarUiCurveEnum::EaseInOutCubic:
		{
			if (progress < 0.5) return 4.0 * progress * progress * progress;
			double inverseProgress = -2.0 * progress + 2.0;
			return 1.0
				- inverseProgress * inverseProgress * inverseProgress / 2.0;
		}
		case BarUiCurveEnum::EaseInBack:
			return (back + 1.0) * progress * progress * progress - back * progress * progress;
		case BarUiCurveEnum::EaseOutBack:
		{
			double shiftedProgress = progress - 1.0;
			double shiftedProgressSquared = shiftedProgress * shiftedProgress;
			return 1.0 + (back + 1.0) * shiftedProgressSquared * shiftedProgress
				+ back * shiftedProgressSquared;
		}
		case BarUiCurveEnum::EaseInOutBack:
		{
			double c2 = back * 1.525;
			double doubledProgress = progress < 0.5
				? 2.0 * progress : 2.0 * progress - 2.0;
			double doubledProgressSquared = doubledProgress * doubledProgress;
			return progress < 0.5
				? doubledProgressSquared * ((c2 + 1.0) * doubledProgress - c2) / 2.0
				: (doubledProgressSquared * ((c2 + 1.0) * doubledProgress + c2)
					+ 2.0) / 2.0;
		}
		case BarUiCurveEnum::Linear:
		default: return progress;
		}
	}

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

	inline wstring_view ResolveBarAnnotationPopupTitle(
		BarThicknessPreviewVisualKind anchorKind) noexcept
	{
		return anchorKind == BarThicknessPreviewVisualKind::SoftPen
			? L"标注线（粗细固定，暂未支持）"
			: L"启用标注线（暂不可用）";
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
		coreThickness = max(0.0, coreThickness);
		outerThickness = max(0.0, outerThickness);
		shellProgress = clamp(shellProgress, 0.0, 1.0);
		const double currentShellThickness = coreThickness
			+ (outerThickness - coreThickness) * shellProgress;
		return max(coreThickness, currentShellThickness);
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
		layerThickness = max(0.0, layerThickness);
		animatedOuterDiameter = max(0.0, animatedOuterDiameter);
		sliderTrackThickness = max(0.0, sliderTrackThickness);
		sliderProgress = clamp(sliderProgress, 0.0, 1.0);
		// Slider 展开时端点圆心与芯宽同步收向轨道，避免两套几何在交接帧错位。
		const double endpointDiameter = animatedOuterDiameter
			+ (sliderTrackThickness - animatedOuterDiameter) * sliderProgress;
		return {
			endpointDiameter,
			max(0.0, (endpointDiameter - layerThickness) / 2.0),
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

	// 从曲线中途续接时，单调曲线截取并归一化尾段；Back 为避免非单调除法而重建剩余段。
	inline double BarUiApplyCurveRange(BarUiCurveEnum curve, double startProgress, double progress)
	{
		constexpr double epsilon = 0.000001;
		startProgress = clamp(startProgress, 0.0, 1.0);
		progress = clamp(progress, startProgress, 1.0);
		double localProgress = 1.0 - startProgress <= epsilon
			? 1.0 : (progress - startProgress) / (1.0 - startProgress);
		if (BarUiIsBackCurve(curve)) return BarUiApplyCurve(curve, localProgress);

		double startValue = BarUiApplyCurve(curve, startProgress);
		double endValue = BarUiApplyCurve(curve, 1.0);
		double denominator = endValue - startValue;
		if (!isfinite(denominator) || abs(denominator) <= epsilon)
			return BarUiApplyCurve(curve, localProgress);
		return (BarUiApplyCurve(curve, progress) - startValue) / denominator;
	}

	struct BarUiCurveSpecClass
	{
		BarUiCurveEnum first = BarUiCurveEnum::EaseInOutCubic;
		BarUiCurveEnum second = BarUiCurveEnum::EaseInOutCubic;
		double timelineStartProgress = 0.0;
		bool continueTimelinePhase = false;
	};

	// 一组关联动画共用线性时间轴；中途修改目标时复用剩余时长。
	class BarUiTimelineClass
	{
	public:
		void Restart(double durationT)
		{
			duration = isfinite(durationT) && durationT > 0.0 ? durationT : 0.0;
			progress = duration > 0.0 ? 0.0 : 1.0;
		}
		void Advance(double dt, double speedRate)
		{
			if (!IsActive() || !isfinite(dt) || dt <= 0.0
				|| !isfinite(speedRate) || speedRate <= 0.0) return;
			progress = clamp(progress + dt * speedRate / duration, 0.0, 1.0);
		}
		bool IsActive() const
		{
			return isfinite(duration) && duration > 0.0
				&& isfinite(progress) && progress < 1.0;
		}
		double GetRemainingDuration() const
		{
			if (!IsActive()) return 0.0;
			return duration * (1.0 - progress);
		}
		double GetProgress() const { return clamp(progress, 0.0, 1.0); }
		bool CanJoin(double maxProgress = 0.5) const
		{
			maxProgress = isfinite(maxProgress) ? clamp(maxProgress, 0.0, 1.0) : 0.5;
			return IsActive() && GetProgress() <= maxProgress;
		}

	private:
		double duration = 0.0;
		double progress = 1.0;
	};

	struct BarUiKeyframeTimelineResultClass
	{
		double progress = 0.0;
		bool reachedKeyframe = false;
		bool finished = false;
		uint64_t generation = 0;
	};
	class BarUiKeyframeTimelineClass
	{
	public:
		class LockedView
		{
		public:
			uint64_t Start(double durationT, double keyframeProgressT = 0.5)
			{
				owner.duration = isfinite(durationT) && durationT > 0.0
					? durationT : 0.0;
				owner.keyframeProgress = isfinite(keyframeProgressT)
					? clamp(keyframeProgressT, 0.0, 1.0) : 0.5;
				owner.progress = 0.0;
				owner.keyframeTriggered = false;
				owner.active = true;
				owner.generation = NextGeneration();
				return owner.generation;
			}
			BarUiKeyframeTimelineResultClass Advance(double dt, double speedRate)
			{
				return owner.AdvanceLocked(dt, speedRate);
			}
			uint64_t Cancel()
			{
				owner.duration = 0.0;
				owner.progress = 1.0;
				owner.keyframeTriggered = false;
				owner.active = false;
				owner.generation = NextGeneration();
				return owner.generation;
			}
			bool IsActive() const { return owner.IsActiveLocked(); }
			bool IsCurrentGeneration(uint64_t generationT) const
			{
				return owner.generation == generationT;
			}

		private:
			explicit LockedView(BarUiKeyframeTimelineClass& ownerT) : owner(ownerT) {}

			BarUiKeyframeTimelineClass& owner;
			friend class BarUiKeyframeTimelineClass;
		};

		BarUiKeyframeTimelineClass() : generation(NextGeneration()) {}
		BarUiKeyframeTimelineClass(const BarUiKeyframeTimelineClass& other)
			: generation(NextGeneration())
		{
			lock_guard lock(other.transactionMutex);
			CopyStateLocked(other);
		}
		BarUiKeyframeTimelineClass& operator=(const BarUiKeyframeTimelineClass& other)
		{
			if (this == &other) return *this;
			scoped_lock lock(transactionMutex, other.transactionMutex);
			CopyStateLocked(other);
			generation = NextGeneration();
			return *this;
		}
		BarUiKeyframeTimelineClass(BarUiKeyframeTimelineClass&& other)
			: generation(NextGeneration())
		{
			lock_guard lock(other.transactionMutex);
			CopyStateLocked(other);
		}
		BarUiKeyframeTimelineClass& operator=(BarUiKeyframeTimelineClass&& other)
		{
			if (this == &other) return *this;
			scoped_lock lock(transactionMutex, other.transactionMutex);
			CopyStateLocked(other);
			generation = NextGeneration();
			return *this;
		}

		// 回调内可把时间线和专用载荷作为一个短事务提交，禁止在其中做解析等重活。
		template <typename Callback>
		decltype(auto) Transaction(Callback&& callback)
		{
			lock_guard lock(transactionMutex);
			LockedView view(*this);
			return forward<Callback>(callback)(view);
		}

		uint64_t Start(double durationT, double keyframeProgressT = 0.5)
		{
			lock_guard lock(transactionMutex);
			LockedView view(*this);
			return view.Start(durationT, keyframeProgressT);
		}
		BarUiKeyframeTimelineResultClass Advance(double dt, double speedRate)
		{
			lock_guard lock(transactionMutex);
			return AdvanceLocked(dt, speedRate);
		}
		uint64_t Cancel()
		{
			lock_guard lock(transactionMutex);
			LockedView view(*this);
			return view.Cancel();
		}
		bool IsActive() const
		{
			lock_guard lock(transactionMutex);
			return IsActiveLocked();
		}

	private:
		static uint64_t NextGeneration()
		{
			return nextGeneration.fetch_add(1, memory_order_relaxed) + 1;
		}
		bool IsActiveLocked() const { return active; }
		BarUiKeyframeTimelineResultClass AdvanceLocked(double dt, double speedRate)
		{
			BarUiKeyframeTimelineResultClass result;
			result.generation = generation;
			double currentProgress = clamp(progress, 0.0, 1.0);
			result.progress = currentProgress;
			if (!active) return result;

			double currentDuration = duration;
			if (!isfinite(currentDuration) || currentDuration <= 0.0)
			{
				result.progress = 1.0;
				result.reachedKeyframe = !keyframeTriggered;
				result.finished = true;
				progress = 1.0;
				keyframeTriggered = true;
				active = false;
				return result;
			}
			if (!isfinite(dt) || dt <= 0.0 || !isfinite(speedRate) || speedRate <= 0.0)
				return result;

			double nextProgress = clamp(
				currentProgress + dt * speedRate / currentDuration, 0.0, 1.0);
			double currentKeyframe = clamp(
				static_cast<double>(keyframeProgress), 0.0, 1.0);
			result.reachedKeyframe = !keyframeTriggered && nextProgress >= currentKeyframe;
			// 跨越中点的帧精确取关键帧值，内部时间仍推进到真实位置。
			result.progress = result.reachedKeyframe ? currentKeyframe : nextProgress;
			if (result.reachedKeyframe) keyframeTriggered = true;
			progress = nextProgress;
			if (nextProgress >= 1.0)
			{
				result.finished = true;
				active = false;
			}
			return result;
		}
		void CopyStateLocked(const BarUiKeyframeTimelineClass& other)
		{
			duration = other.duration;
			progress = other.progress;
			keyframeProgress = other.keyframeProgress;
			keyframeTriggered = other.keyframeTriggered;
			active = other.active;
		}

	private:
		inline static atomic<uint64_t> nextGeneration = 0;
		mutable mutex transactionMutex;
		double duration = 0.0;
		double progress = 1.0;
		double keyframeProgress = 0.5;
		bool keyframeTriggered = false;
		bool active = false;
		uint64_t generation = 0;
	};

	struct BarUiAnimationAdvanceContextClass;
	struct BarUiAnimationAdvanceResultClass;

	// SetTar 与渲染推进会同时改动一组字段；小粒度锁保证一次动画段更新不可被拆开观察。
	class BarUiAnimationTransactionClass
	{
	public:
		BarUiAnimationTransactionClass() noexcept = default;
		BarUiAnimationTransactionClass(const BarUiAnimationTransactionClass&) noexcept {}
		BarUiAnimationTransactionClass& operator=(
			const BarUiAnimationTransactionClass&) noexcept { return *this; }
		BarUiAnimationTransactionClass(BarUiAnimationTransactionClass&&) noexcept {}
		BarUiAnimationTransactionClass& operator=(
			BarUiAnimationTransactionClass&&) noexcept { return *this; }

		void Lock() noexcept;
		void Unlock() noexcept;

	private:
		atomic_flag locked = ATOMIC_FLAG_INIT;
	};

	class BarUiAnimationTransactionGuardClass
	{
	public:
		explicit BarUiAnimationTransactionGuardClass(
			BarUiAnimationTransactionClass& transactionT) noexcept
			: transaction(transactionT)
		{
			transaction.Lock();
		}
		~BarUiAnimationTransactionGuardClass() { transaction.Unlock(); }

		BarUiAnimationTransactionGuardClass(
			const BarUiAnimationTransactionGuardClass&) = delete;
		BarUiAnimationTransactionGuardClass& operator=(
			const BarUiAnimationTransactionGuardClass&) = delete;

	private:
		BarUiAnimationTransactionClass& transaction;
	};

	class BarUiStateClass
	{
	public:
		BarUiStateClass() {}
		BarUiStateClass(optional<bool> valT, optional<bool> tarT = nullopt)
		{
			val = valT.has_value() ? valT.value() : false;
			tar = tarT.has_value() ? tarT.value() : static_cast<bool>(val);
		}

		bool IsSame() { return val == tar; }
		bool SetTar(bool tarT)
		{
			if (tar == tarT) return false;
			tar = tarT;
			return true;
		}
		void Initialization(optional<bool> valT, optional<bool> tarT = nullopt)
		{
			val = valT.has_value() ? valT.value() : false;
			tar = tarT.has_value() ? tarT.value() : static_cast<bool>(val);
		}

	public:
		IdtAtomic<bool> val = false;
		IdtAtomic<bool> tar = false;
	};

	class BarUiValueClass
	{
	public:
		BarUiValueClass() {}
		BarUiValueClass(double valT,
			BarUiValueModeEnum modT = BarUiValueModeEnum::Variable,
			optional<double> desT = nullopt)
		{
			mod = modT;
			if (desT.has_value()) des = desT.value();
			SetDirect(valT);
		}

		bool IsSame() { return val == tar && !hasMiddleV; }
		bool SetTar(double tarT, optional<double> durT = nullopt,
			optional<double> middleVT = nullopt, bool forceRestart = false)
		{
			BarUiCurveSpecClass spec{ curve, curve, 0.0, false };
			return SetTar(tarT, durT, middleVT, forceRestart, spec);
		}
		bool SetTar(double tarT, optional<double> durT, optional<double> middleVT,
			bool forceRestart, const BarUiCurveSpecClass& curveSpec)
		{
			// 几何值最终会参与 Win32/D2D 坐标换算，拒绝非有限目标以免污染整条动画链。
			if (!isfinite(tarT) || (middleVT.has_value() && !isfinite(middleVT.value())))
				return false;
			auto IsSameTarget = [&]()
				{
					if (forceRestart || tar != tarT) return false;
					if (!middleVT.has_value()) return true;
					return hasMiddleV && middleV == middleVT.value();
				};
			// 每帧重复提交相同目标时不取得锁；真实变更在锁内再次确认。
			if (IsSameTarget()) return false;
			BarUiAnimationTransactionGuardClass guard(transaction);
			if (IsSameTarget()) return false;

			startV = val;
			progress = 0.0;
			tar = tarT;
			double phaseStart = clamp(curveSpec.timelineStartProgress, 0.0, 1.0);
			hasMiddleV = middleVT.has_value();
			activeCurve = curveSpec.first;
			activeMiddleCurve = curveSpec.second;
			timelineStartProgress = phaseStart;
			continueTimelinePhase = curveSpec.continueTimelinePhase;
			if (middleVT.has_value()) middleV = middleVT.value();
			if (hasMiddleV && continueTimelinePhase && phaseStart >= 0.5)
			{
				hasMiddleV = false;
				activeCurve = activeMiddleCurve;
				timelineStartProgress = clamp((phaseStart - 0.5) * 2.0, 0.0, 1.0);
			}

			double distance = abs(static_cast<double>(tar) - static_cast<double>(startV));
			if (middleVT.has_value())
			{
				distance = abs(middleVT.value() - static_cast<double>(startV))
					+ abs(static_cast<double>(tar) - middleVT.value());
			}
			double defaultSpeed = des;
			if (durT.has_value()) dur = durT.value();
			else if (isfinite(distance) && isfinite(defaultSpeed) && defaultSpeed > 0.0)
				dur = distance / defaultSpeed;
			else dur = 0.0;
			return true;
		}
		void SetDirect(double valueT)
		{
			if (!isfinite(valueT)) return;
			BarUiAnimationTransactionGuardClass guard(transaction);
			val = valueT;
			tar = valueT;
			startV = valueT;
			progress = 0.0;
			dur = 0.0;
			hasMiddleV = false;
			activeCurve = curve;
			activeMiddleCurve = curve;
			timelineStartProgress = 0.0;
			continueTimelinePhase = false;
		}
		void Initialization(double valT,
			BarUiValueModeEnum modT = BarUiValueModeEnum::Variable,
			optional<double> desT = nullopt)
		{
			mod = modT;
			if (desT.has_value()) des = desT.value();
			SetDirect(valT);
		}

	public:
		IdtAtomic<BarUiValueModeEnum> mod = BarUiValueModeEnum::Linear;
		IdtAtomic<BarUiCurveEnum> curve = BarUiCurveEnum::EaseInOutCubic;
		IdtAtomic<BarUiCurveEnum> activeCurve = BarUiCurveEnum::EaseInOutCubic;
		IdtAtomic<BarUiCurveEnum> activeMiddleCurve = BarUiCurveEnum::EaseInOutCubic;
		IdtAtomic<double> timelineStartProgress = 0.0;
		IdtAtomic<bool> continueTimelinePhase = false;
		IdtAtomic<double> val = 0.0;
		IdtAtomic<double> tar = 0.0;
		IdtAtomic<double> des = BarUiDefaultDes;
		IdtAtomic<double> dur = 0.0;
		IdtAtomic<double> startV = 0.0;
		IdtAtomic<double> progress = 0.0;
		IdtAtomic<bool> hasMiddleV = false;
		IdtAtomic<double> middleV = 0.0;

	private:
		BarUiAnimationTransactionClass transaction;
		friend BarUiAnimationAdvanceResultClass BarUiAdvanceAnimation(
			BarUiValueClass& value,
			const BarUiAnimationAdvanceContextClass& context);
	};

	class BarUiColorClass
	{
	public:
		BarUiColorClass() {}
		BarUiColorClass(COLORREF valT, optional<double> desT = nullopt)
		{
			if (desT.has_value()) des = desT.value();
			SetDirect(valT);
		}

		bool IsSame() { return val == tar; }
		bool SetTar(COLORREF tarT, optional<double> durT = nullopt)
		{
			BarUiCurveSpecClass spec{ curve, curve, 0.0, false };
			return SetTar(tarT, durT, spec);
		}
		bool SetTar(COLORREF tarT, optional<double> durT,
			const BarUiCurveSpecClass& curveSpec)
		{
			if (tar == tarT) return false;
			BarUiAnimationTransactionGuardClass guard(transaction);
			if (tar == tarT) return false;
			startColor = val;
			progress = 0.0;
			tar = tarT;
			activeCurve = curveSpec.first;
			timelineStartProgress = clamp(curveSpec.timelineStartProgress, 0.0, 1.0);
			continueTimelinePhase = curveSpec.continueTimelinePhase;

			double defaultSpeed = des;
			if (durT.has_value()) dur = durT.value();
			else if (isfinite(defaultSpeed) && defaultSpeed > 0.0) dur = 1.0 / defaultSpeed;
			else dur = 0.0;
			return true;
		}
		void SetDirect(COLORREF valueT)
		{
			BarUiAnimationTransactionGuardClass guard(transaction);
			val = valueT;
			tar = valueT;
			startColor = valueT;
			progress = 0.0;
			dur = 0.0;
			activeCurve = curve;
			timelineStartProgress = 0.0;
			continueTimelinePhase = false;
		}
		void Initialization(COLORREF valT, optional<double> desT = nullopt)
		{
			if (desT.has_value()) des = desT.value();
			SetDirect(valT);
		}

	public:
		IdtAtomic<BarUiCurveEnum> curve = BarUiCurveEnum::EaseInOutCubic;
		IdtAtomic<BarUiCurveEnum> activeCurve = BarUiCurveEnum::EaseInOutCubic;
		IdtAtomic<double> timelineStartProgress = 0.0;
		IdtAtomic<bool> continueTimelinePhase = false;
		IdtAtomic<COLORREF> val = RGB(0, 0, 0);
		IdtAtomic<COLORREF> tar = RGB(0, 0, 0);
		IdtAtomic<COLORREF> startColor = RGB(0, 0, 0);
		IdtAtomic<double> progress = 0.0;
		IdtAtomic<double> des = 2.5;
		IdtAtomic<double> dur = 0.0;
		IdtAtomic<bool> animateWhenDisabled = false;

	private:
		BarUiAnimationTransactionClass transaction;
		friend BarUiAnimationAdvanceResultClass BarUiAdvanceAnimation(
			BarUiColorClass& color,
			const BarUiAnimationAdvanceContextClass& context);
	};

	class BarUiPctClass
	{
	public:
		BarUiPctClass() {}
		BarUiPctClass(double valT, optional<double> desT = nullopt)
		{
			if (desT.has_value()) des = desT.value();
			SetDirect(valT);
		}

		bool IsSame() { return val == tar && !hasMiddleV; }
		bool SetTar(double tarT, optional<double> durT = nullopt,
			optional<double> middleVT = nullopt, bool forceRestart = false)
		{
			BarUiCurveSpecClass spec{ curve, curve, 0.0, false };
			return SetTar(tarT, durT, middleVT, forceRestart, spec);
		}
		bool SetTar(double tarT, optional<double> durT, optional<double> middleVT,
			bool forceRestart, const BarUiCurveSpecClass& curveSpec)
		{
			tarT = isfinite(tarT) ? clamp(tarT, 0.0, 1.0) : 0.0;
			if (middleVT.has_value())
				middleVT = isfinite(middleVT.value())
					? clamp(middleVT.value(), 0.0, 1.0) : 0.0;
			auto IsSameTarget = [&]()
				{
					if (forceRestart || tar != tarT) return false;
					if (!middleVT.has_value()) return true;
					return hasMiddleV && middleV == middleVT.value();
				};
			if (IsSameTarget()) return false;
			BarUiAnimationTransactionGuardClass guard(transaction);
			if (IsSameTarget()) return false;

			startV = val;
			progress = 0.0;
			tar = tarT;
			double phaseStart = clamp(curveSpec.timelineStartProgress, 0.0, 1.0);
			hasMiddleV = middleVT.has_value();
			activeCurve = curveSpec.first;
			activeMiddleCurve = curveSpec.second;
			timelineStartProgress = phaseStart;
			continueTimelinePhase = curveSpec.continueTimelinePhase;
			if (middleVT.has_value()) middleV = middleVT.value();
			if (hasMiddleV && continueTimelinePhase && phaseStart >= 0.5)
			{
				hasMiddleV = false;
				activeCurve = activeMiddleCurve;
				timelineStartProgress = clamp((phaseStart - 0.5) * 2.0, 0.0, 1.0);
			}

			double defaultSpeed = des;
			if (durT.has_value()) dur = durT.value();
			else if (isfinite(defaultSpeed) && defaultSpeed > 0.0) dur = 1.0 / defaultSpeed;
			else dur = 0.0;
			return true;
		}
		void SetDirect(double valueT)
		{
			valueT = isfinite(valueT) ? clamp(valueT, 0.0, 1.0) : 0.0;
			BarUiAnimationTransactionGuardClass guard(transaction);
			val = valueT;
			tar = valueT;
			startV = valueT;
			progress = 0.0;
			dur = 0.0;
			hasMiddleV = false;
			activeCurve = curve;
			activeMiddleCurve = curve;
			timelineStartProgress = 0.0;
			continueTimelinePhase = false;
		}
		void Initialization(double valT, optional<double> desT = nullopt)
		{
			if (desT.has_value()) des = desT.value();
			SetDirect(valT);
		}

	public:
		IdtAtomic<BarUiCurveEnum> curve = BarUiCurveEnum::EaseOutSine;
		IdtAtomic<BarUiCurveEnum> activeCurve = BarUiCurveEnum::EaseOutSine;
		IdtAtomic<BarUiCurveEnum> activeMiddleCurve = BarUiCurveEnum::EaseOutSine;
		IdtAtomic<double> timelineStartProgress = 0.0;
		IdtAtomic<bool> continueTimelinePhase = false;
		IdtAtomic<double> val = 1.0;
		IdtAtomic<double> tar = 1.0;
		IdtAtomic<double> startV = 1.0;
		IdtAtomic<double> progress = 0.0;
		IdtAtomic<double> des = 2.5;
		IdtAtomic<double> dur = 0.0;
		IdtAtomic<bool> hasMiddleV = false;
		IdtAtomic<double> middleV = 0.0;
		IdtAtomic<bool> animateWhenDisabled = false;

	private:
		BarUiAnimationTransactionClass transaction;
		friend BarUiAnimationAdvanceResultClass BarUiAdvanceAnimation(
			BarUiPctClass& pct,
			const BarUiAnimationAdvanceContextClass& context);
	};

	struct BarUiAnimationAdvanceContextClass
	{
		double dtSeconds = 0.0;
		double speedRate = 1.0;
		bool animationEnabled = true;
		bool forceReplace = false;
	};

	struct BarUiAnimationAdvanceResultClass
	{
		bool changed = false;
		bool active = false;
	};

	COLORREF MixBarUiColor(COLORREF startColor, COLORREF targetColor, double progress);
	BarUiAnimationAdvanceResultClass BarUiAdvanceAnimation(
		BarUiStateClass& state, const BarUiAnimationAdvanceContextClass& context);
	BarUiAnimationAdvanceResultClass BarUiAdvanceAnimation(
		BarUiValueClass& value, const BarUiAnimationAdvanceContextClass& context);
	BarUiAnimationAdvanceResultClass BarUiAdvanceAnimation(
		BarUiColorClass& color, const BarUiAnimationAdvanceContextClass& context);
	BarUiAnimationAdvanceResultClass BarUiAdvanceAnimation(
		BarUiPctClass& pct, const BarUiAnimationAdvanceContextClass& context);
}
