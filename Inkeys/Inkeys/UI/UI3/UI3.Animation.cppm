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
#include <utility>

export module Inkeys.UI.UI3.Animation;

using namespace std;

export namespace Inkeys::UI::UI3::Detail
{
	[[nodiscard]] inline double ResolveFontCorrectionTarget(
		double targetPixels, bool allowFractional) noexcept
	{
		if (!std::isfinite(targetPixels) || targetPixels <= 0.0) return 0.0;
		// 只约束静止目标，原始字号和中间帧继续沿现有动画轨迹变化。
		return allowFractional ? 1.0 : std::floor(targetPixels) / targetPixels;
	}
}

export namespace Inkeys::UI::UI3
{
	extern IdtAtomic<double> UiDefaultDes;
	extern IdtAtomic<double> UiDefaultOperationDur;
	extern IdtAtomic<bool> UiAnimationEnabled;
	extern IdtAtomic<double> UiAnimationSpeedRate;

	// 动效类型
	enum class UiValueModeEnum : int
	{
		Once = 0, // 无动画
		Linear = 1, // 兼容旧类型；实际插值由 curve 决定
		Variable = 2 // 兼容旧类型；需要回弹时使用 Back 曲线
	};

	// 动画曲线只负责将线性时间进度映射为数值进度，不改变统一批次的结束时刻。
	enum class UiCurveEnum : int
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

	inline bool UiIsBackCurve(UiCurveEnum curve)
	{
		return curve == UiCurveEnum::EaseInBack
			|| curve == UiCurveEnum::EaseOutBack
			|| curve == UiCurveEnum::EaseInOutBack;
	}
	struct UiCurveExtremaClass
	{
		double minimum = 0.0;
		double maximum = 1.0;
	};
	inline UiCurveExtremaClass UiGetCurveExtrema(
		UiCurveEnum curve) noexcept
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
		case UiCurveEnum::EaseInBack:
			return { EaseInMinimum(back), 1.0 };
		case UiCurveEnum::EaseOutBack:
			return { 0.0, 1.0 - EaseInMinimum(back) };
		case UiCurveEnum::EaseInOutBack:
		{
			const double minimum = EaseInMinimum(back * 1.525) / 2.0;
			return { minimum, 1.0 - minimum };
		}
		default:
			return {};
		}
	}
	inline double UiApplyCurve(UiCurveEnum curve, double progress)
	{
		constexpr double pi = 3.14159265358979323846;
		constexpr double back = 1.1; // 克制的 UI 回弹强度
		progress = clamp(progress, 0.0, 1.0);
		switch (curve)
		{
		case UiCurveEnum::EaseInSine: return 1.0 - cos(progress * pi / 2.0);
		case UiCurveEnum::EaseOutSine: return sin(progress * pi / 2.0);
		case UiCurveEnum::EaseInOutSine: return -(cos(pi * progress) - 1.0) / 2.0;
		case UiCurveEnum::EaseInCubic: return progress * progress * progress;
		// 固定二、三次幂直接展开，避免逐属性逐帧进入通用 pow。
		case UiCurveEnum::EaseOutCubic:
		{
			double inverseProgress = 1.0 - progress;
			return 1.0 - inverseProgress * inverseProgress * inverseProgress;
		}
		case UiCurveEnum::EaseInOutCubic:
		{
			if (progress < 0.5) return 4.0 * progress * progress * progress;
			double inverseProgress = -2.0 * progress + 2.0;
			return 1.0
				- inverseProgress * inverseProgress * inverseProgress / 2.0;
		}
		case UiCurveEnum::EaseInBack:
			return (back + 1.0) * progress * progress * progress - back * progress * progress;
		case UiCurveEnum::EaseOutBack:
		{
			double shiftedProgress = progress - 1.0;
			double shiftedProgressSquared = shiftedProgress * shiftedProgress;
			return 1.0 + (back + 1.0) * shiftedProgressSquared * shiftedProgress
				+ back * shiftedProgressSquared;
		}
		case UiCurveEnum::EaseInOutBack:
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
		case UiCurveEnum::Linear:
		default: return progress;
		}
	}

	// 从曲线中途续接时，单调曲线截取并归一化尾段；Back 为避免非单调除法而重建剩余段。
	inline double UiApplyCurveRange(UiCurveEnum curve, double startProgress, double progress)
	{
		constexpr double epsilon = 0.000001;
		startProgress = clamp(startProgress, 0.0, 1.0);
		progress = clamp(progress, startProgress, 1.0);
		double localProgress = 1.0 - startProgress <= epsilon
			? 1.0 : (progress - startProgress) / (1.0 - startProgress);
		if (UiIsBackCurve(curve)) return UiApplyCurve(curve, localProgress);

		double startValue = UiApplyCurve(curve, startProgress);
		double endValue = UiApplyCurve(curve, 1.0);
		double denominator = endValue - startValue;
		if (!isfinite(denominator) || abs(denominator) <= epsilon)
			return UiApplyCurve(curve, localProgress);
		return (UiApplyCurve(curve, progress) - startValue) / denominator;
	}

	struct UiCurveSpecClass
	{
		UiCurveEnum first = UiCurveEnum::EaseInOutCubic;
		UiCurveEnum second = UiCurveEnum::EaseInOutCubic;
		double timelineStartProgress = 0.0;
		bool continueTimelinePhase = false;
	};

	// 主栏普通按钮与 Whiteboard 标准按钮共用同一按压/回弹曲线。
	inline UiCurveSpecClass ButtonPressCurve() noexcept
	{
		return { UiCurveEnum::EaseOutCubic,
			UiCurveEnum::EaseOutCubic, 0.0, false };
	}
	inline UiCurveSpecClass ButtonReleaseCurve() noexcept
	{
		return { UiCurveEnum::EaseOutBack,
			UiCurveEnum::EaseOutBack, 0.0, false };
	}

	// 一组关联动画共用线性时间轴；中途修改目标时复用剩余时长。
	class UiTimelineClass
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

	struct UiKeyframeTimelineResultClass
	{
		double progress = 0.0;
		bool reachedKeyframe = false;
		bool finished = false;
		uint64_t generation = 0;
	};
	class UiKeyframeTimelineClass
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
			UiKeyframeTimelineResultClass Advance(double dt, double speedRate)
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
			explicit LockedView(UiKeyframeTimelineClass& ownerT) : owner(ownerT) {}

			UiKeyframeTimelineClass& owner;
			friend class UiKeyframeTimelineClass;
		};

		UiKeyframeTimelineClass() : generation(NextGeneration()) {}
		UiKeyframeTimelineClass(const UiKeyframeTimelineClass& other)
			: generation(NextGeneration())
		{
			lock_guard lock(other.transactionMutex);
			CopyStateLocked(other);
		}
		UiKeyframeTimelineClass& operator=(const UiKeyframeTimelineClass& other)
		{
			if (this == &other) return *this;
			scoped_lock lock(transactionMutex, other.transactionMutex);
			CopyStateLocked(other);
			generation = NextGeneration();
			return *this;
		}
		UiKeyframeTimelineClass(UiKeyframeTimelineClass&& other)
			: generation(NextGeneration())
		{
			lock_guard lock(other.transactionMutex);
			CopyStateLocked(other);
		}
		UiKeyframeTimelineClass& operator=(UiKeyframeTimelineClass&& other)
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
		UiKeyframeTimelineResultClass Advance(double dt, double speedRate)
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
		UiKeyframeTimelineResultClass AdvanceLocked(double dt, double speedRate)
		{
			UiKeyframeTimelineResultClass result;
			result.generation = generation;
			double currentProgress = clamp(progress, 0.0, 1.0);
			result.progress = currentProgress;
			if (!active) return result;

			double currentDuration = duration;
			// 关闭动画时内容关键帧也须在本帧提交，不能因 idle 首帧 dt=0 留一帧旧图标。
			if (!static_cast<bool>(UiAnimationEnabled)
				|| !isfinite(currentDuration) || currentDuration <= 0.0)
				return FinishLocked();
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
		UiKeyframeTimelineResultClass FinishLocked()
		{
			UiKeyframeTimelineResultClass result;
			result.generation = generation;
			result.progress = clamp(progress, 0.0, 1.0);
			if (!active) return result;
			result.progress = 1.0;
			result.reachedKeyframe = !keyframeTriggered;
			result.finished = true;
			progress = 1.0;
			keyframeTriggered = true;
			active = false;
			return result;
		}
		void CopyStateLocked(const UiKeyframeTimelineClass& other)
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

	// 实时内容更新与旧关键帧取消必须处于同一事务，避免旧帧在返回后覆盖新内容。
	template <typename Changed, typename Apply>
	bool ApplyImmediateContentUpdate(
		UiKeyframeTimelineClass& timeline,
		Changed&& changed, Apply&& apply)
	{
		return timeline.Transaction(
			[&](UiKeyframeTimelineClass::LockedView& locked)
			{
				const bool contentChanged = static_cast<bool>(changed());
				const bool result = locked.IsActive() || contentChanged;
				locked.Cancel();
				apply();
				return result;
			});
	}

	[[nodiscard]] constexpr bool ShouldKeepContentVisibleForExit(
		bool animated, bool contentChanged,
		bool targetEmpty, bool currentlyVisible) noexcept
	{
		return animated && contentChanged && targetEmpty && currentlyVisible;
	}

	struct UiAnimationAdvanceContextClass;
	struct UiAnimationAdvanceResultClass;

	// SetTar 与渲染推进会同时改动一组字段；小粒度锁保证一次动画段更新不可被拆开观察。
	class UiAnimationTransactionClass
	{
	public:
		UiAnimationTransactionClass() noexcept = default;
		UiAnimationTransactionClass(const UiAnimationTransactionClass&) noexcept {}
		UiAnimationTransactionClass& operator=(
			const UiAnimationTransactionClass&) noexcept { return *this; }
		UiAnimationTransactionClass(UiAnimationTransactionClass&&) noexcept {}
		UiAnimationTransactionClass& operator=(
			UiAnimationTransactionClass&&) noexcept { return *this; }

		void Lock() noexcept;
		void Unlock() noexcept;

	private:
		atomic_flag locked = ATOMIC_FLAG_INIT;
	};

	class UiAnimationTransactionGuardClass
	{
	public:
		explicit UiAnimationTransactionGuardClass(
			UiAnimationTransactionClass& transactionT) noexcept
			: transaction(transactionT)
		{
			transaction.Lock();
		}
		~UiAnimationTransactionGuardClass() { transaction.Unlock(); }

		UiAnimationTransactionGuardClass(
			const UiAnimationTransactionGuardClass&) = delete;
		UiAnimationTransactionGuardClass& operator=(
			const UiAnimationTransactionGuardClass&) = delete;

	private:
		UiAnimationTransactionClass& transaction;
	};

	class UiStateClass
	{
	public:
		UiStateClass() {}
		UiStateClass(optional<bool> valT, optional<bool> tarT = nullopt)
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

	class UiValueClass
	{
	public:
		UiValueClass() {}
		UiValueClass(double valT,
			UiValueModeEnum modT = UiValueModeEnum::Variable,
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
			UiCurveSpecClass spec{ curve, curve, 0.0, false };
			return SetTar(tarT, durT, middleVT, forceRestart, spec);
		}
		bool SetTar(double tarT, optional<double> durT, optional<double> middleVT,
			bool forceRestart, const UiCurveSpecClass& curveSpec)
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
			UiAnimationTransactionGuardClass guard(transaction);
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
			UiAnimationTransactionGuardClass guard(transaction);
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
		// 目标窗口改变裁剪原点时平移整条曲线，保持屏幕上的当前值与动画进度。
		void ShiftOrigin(double delta)
		{
			if (!isfinite(delta) || delta == 0) return;
			UiAnimationTransactionGuardClass guard(transaction);
			val = static_cast<double>(val) + delta; tar = static_cast<double>(tar) + delta;
			startV = static_cast<double>(startV) + delta; middleV = static_cast<double>(middleV) + delta;
		}

		void Initialization(double valT,
			UiValueModeEnum modT = UiValueModeEnum::Variable,
			optional<double> desT = nullopt)
		{
			mod = modT;
			if (desT.has_value()) des = desT.value();
			SetDirect(valT);
		}

	public:
		IdtAtomic<UiValueModeEnum> mod = UiValueModeEnum::Linear;
		IdtAtomic<UiCurveEnum> curve = UiCurveEnum::EaseInOutCubic;
		IdtAtomic<UiCurveEnum> activeCurve = UiCurveEnum::EaseInOutCubic;
		IdtAtomic<UiCurveEnum> activeMiddleCurve = UiCurveEnum::EaseInOutCubic;
		IdtAtomic<double> timelineStartProgress = 0.0;
		IdtAtomic<bool> continueTimelinePhase = false;
		IdtAtomic<double> val = 0.0;
		IdtAtomic<double> tar = 0.0;
		IdtAtomic<double> des = UiDefaultDes;
		IdtAtomic<double> dur = 0.0;
		IdtAtomic<double> startV = 0.0;
		IdtAtomic<double> progress = 0.0;
		IdtAtomic<bool> hasMiddleV = false;
		IdtAtomic<double> middleV = 0.0;

	private:
		UiAnimationTransactionClass transaction;
		friend UiAnimationAdvanceResultClass UiAdvanceAnimation(
			UiValueClass& value,
			const UiAnimationAdvanceContextClass& context);
	};

	// 新布局批次也接管同目标的在途位置，避免局部轨迹与父栏使用不同相位。
	inline bool UiSetLayoutPositionTarget(UiValueClass& value,
		double target, double duration, bool newLayoutBatch,
		const UiCurveSpecClass& curve)
	{
		return value.SetTar(target, duration, nullopt, newLayoutBatch, curve);
	}

	class UiColorClass
	{
	public:
		UiColorClass() {}
		UiColorClass(COLORREF valT, optional<double> desT = nullopt)
		{
			if (desT.has_value()) des = desT.value();
			SetDirect(valT);
		}

		bool IsSame() { return val == tar; }
		bool SetTar(COLORREF tarT, optional<double> durT = nullopt)
		{
			UiCurveSpecClass spec{ curve, curve, 0.0, false };
			return SetTar(tarT, durT, spec);
		}
		bool SetTar(COLORREF tarT, optional<double> durT,
			const UiCurveSpecClass& curveSpec)
		{
			if (tar == tarT) return false;
			UiAnimationTransactionGuardClass guard(transaction);
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
			UiAnimationTransactionGuardClass guard(transaction);
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
		IdtAtomic<UiCurveEnum> curve = UiCurveEnum::EaseInOutCubic;
		IdtAtomic<UiCurveEnum> activeCurve = UiCurveEnum::EaseInOutCubic;
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
		UiAnimationTransactionClass transaction;
		friend UiAnimationAdvanceResultClass UiAdvanceAnimation(
			UiColorClass& color,
			const UiAnimationAdvanceContextClass& context);
	};

	class UiPctClass
	{
	public:
		UiPctClass() {}
		UiPctClass(double valT, optional<double> desT = nullopt)
		{
			if (desT.has_value()) des = desT.value();
			SetDirect(valT);
		}

		bool IsSame() { return val == tar && !hasMiddleV; }
		bool SetTar(double tarT, optional<double> durT = nullopt,
			optional<double> middleVT = nullopt, bool forceRestart = false)
		{
			UiCurveSpecClass spec{ curve, curve, 0.0, false };
			return SetTar(tarT, durT, middleVT, forceRestart, spec);
		}
		bool SetTar(double tarT, optional<double> durT, optional<double> middleVT,
			bool forceRestart, const UiCurveSpecClass& curveSpec)
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
			UiAnimationTransactionGuardClass guard(transaction);
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
			UiAnimationTransactionGuardClass guard(transaction);
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
		IdtAtomic<UiCurveEnum> curve = UiCurveEnum::EaseOutSine;
		IdtAtomic<UiCurveEnum> activeCurve = UiCurveEnum::EaseOutSine;
		IdtAtomic<UiCurveEnum> activeMiddleCurve = UiCurveEnum::EaseOutSine;
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
		UiAnimationTransactionClass transaction;
		friend UiAnimationAdvanceResultClass UiAdvanceAnimation(
			UiPctClass& pct,
			const UiAnimationAdvanceContextClass& context);
	};

	struct UiAnimationAdvanceContextClass
	{
		double dtSeconds = 0.0;
		double speedRate = 1.0;
		bool animationEnabled = true;
		bool forceReplace = false;
	};

	struct UiAnimationAdvanceResultClass
	{
		bool changed = false;
		bool active = false;
	};

	COLORREF MixUiColor(COLORREF startColor, COLORREF targetColor, double progress);
	UiAnimationAdvanceResultClass UiAdvanceAnimation(
		UiStateClass& state, const UiAnimationAdvanceContextClass& context);
	UiAnimationAdvanceResultClass UiAdvanceAnimation(
		UiValueClass& value, const UiAnimationAdvanceContextClass& context);
	UiAnimationAdvanceResultClass UiAdvanceAnimation(
		UiColorClass& color, const UiAnimationAdvanceContextClass& context);
	UiAnimationAdvanceResultClass UiAdvanceAnimation(
		UiPctClass& pct, const UiAnimationAdvanceContextClass& context);

	// 字号只投影静止目标；固定状态保存连续修正轨迹，不改原始 size/几何动画。
	class UiFontCorrectionClass
	{
	public:
		double Resolve(double targetPixels, bool allowFractional, std::uint64_t frame,
			const UiAnimationAdvanceContextClass& context, double duration)
		{
			changed_ = false;
			if (!std::isfinite(targetPixels) || targetPixels < 0.0) return value_.val;
			// 退出的原始几何目标为 0 时无需再把修正系数压到 0，重新打开仍沿原曲线。
			double target = value_.tar;
			if (targetPixels == 0.0) { target = value_.val; sourceTarget_ = -1.0; }
			else if (!initialized_ || targetPixels != sourceTarget_ || allowFractional != sourceAllow_)
			{
				target = Inkeys::UI::UI3::Detail::ResolveFontCorrectionTarget(targetPixels,allowFractional);
				sourceTarget_ = targetPixels; sourceAllow_ = allowFractional;
			}
			const double before = value_.val;
			if (!initialized_ || forceNext_ || !context.animationEnabled || context.forceReplace)
			{
				value_.SetDirect(target); initialized_ = true; forceNext_ = false;
			}
			else value_.SetTar(target, std::isfinite(duration) && duration > 0.0
				? duration : static_cast<double>(UiDefaultOperationDur));
			if (!advanced_ || frame != frame_)
			{
				if (!value_.IsSame() && (context.forceReplace || !context.animationEnabled || context.dtSeconds > 0.0))
					(void)UiAdvanceAnimation(value_, context);
				frame_ = frame; advanced_ = true;
			}
			changed_ = before != static_cast<double>(value_.val);
			return value_.val;
		}
		void Reset() noexcept { initialized_ = false; advanced_ = false; }
		void ForceNext() noexcept { forceNext_ = true; }
		bool Active() { return initialized_ && !value_.IsSame(); }
		bool Changed() const noexcept { return changed_; }
		double Current() const noexcept { return value_.val; }
		double Target() const noexcept { return value_.tar; }
	private:
		UiValueClass value_{1.0, UiValueModeEnum::Linear};
		std::uint64_t frame_ = 0;
		double sourceTarget_ = -1.0;
		bool sourceAllow_ = false;
		bool initialized_ = false, advanced_ = false, forceNext_ = false, changed_ = false;
	};

}
