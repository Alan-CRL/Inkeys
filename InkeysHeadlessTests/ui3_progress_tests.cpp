#include <chrono>
#include <cmath>
#include <initializer_list>
#include <iostream>
#include <string_view>

import Inkeys.UI.UI3.Progress;
import Inkeys.UI.UI3.Animation;

using namespace Inkeys::UI::UI3;

namespace
{
	using namespace std::chrono_literals;
	int failures = 0;
	void Check(bool condition, std::string_view name)
	{
		if (!condition) { ++failures; std::cerr << "FAIL " << name << '\n'; }
	}
	bool Near(double a, double b) { return std::abs(a - b) < 1e-10; }
	HoldProgressState::Time At(int ms) { return HoldProgressState::Time{std::chrono::milliseconds(ms)}; }
	HoldProgressConfig Picker(HoldProgressCurve curve = HoldProgressCurve::EaseInOutCubic)
	{
		HoldProgressConfig config;
		config.delay = 500ms; config.duration = 1500ms; config.fadeIn = config.fadeOut = 180ms;
		config.curve = curve; config.completion = HoldProgressCompletion::KeepHint; config.resetProgressOnCancel = true;
		return config;
	}
	void TestGeometry()
	{
		HoldProgressState state; state.Start(At(0));
		Check(state.NextWake() == At(200), "geometry progress first wake is delay");
		auto frame = state.Advance(At(199));
		Check(frame.visibility == 0 && frame.progress == 0 && !frame.justCompleted, "geometry delay is invisible");
		frame = state.Advance(At(200));
		Check(frame.visibility == 0 && frame.progress == 0 && state.NextWake() == At(201), "geometry ring starts at zero phase");
		frame = state.Advance(At(290));
		Check(Near(frame.visibility, std::sqrt(.5)), "geometry half fade uses out sine");
		frame = state.Advance(At(600));
		Check(frame.visibility == 1 && Near(frame.progress, .5), "geometry progress starts after delay");
		frame = state.Advance(At(999));
		Check(!frame.justCompleted && frame.progress < 1, "geometry completion minus one ms");
		frame = state.Advance(At(1000));
		Check(frame.justCompleted && state.Completed() && frame.progress == 1 && frame.visibility == 1, "geometry completion freezes full ring and starts fade");
		frame = state.Advance(At(1070));
		Check(!frame.justCompleted && Near(frame.visibility, 1 - std::sqrt(.5)), "geometry completed ring fades for 140ms");
		frame = state.Advance(At(1140));
		Check(frame.visibility == 0 && !state.NextWake(), "geometry fade ends with no wake");
		state.Start(At(2000)); state.Advance(At(2200)); state.Advance(At(2600));
		state.Cancel(At(2600)); frame = state.Advance(At(2670));
		Check(Near(frame.progress, .5) && Near(frame.visibility, 1 - std::sqrt(.5)) && !frame.justCompleted,
			"geometry cancellation keeps sampled progress while fading");
		state.Advance(At(2740));
		Check(!state.NextWake() && !state.Completed(), "cancelled geometry does not complete later");
		state.Start(At(3000)); state.Advance(At(3200)); state.Advance(At(3500));
		state.Complete(At(4000), false); frame = state.Advance(At(4000));
		Check(frame.justCompleted && Near(frame.progress, .375), "rejected commit fades last arc without forcing full progress");
	}
	void TestPicker()
	{
		for (const auto curve : {HoldProgressCurve::EaseInOutCubic, HoldProgressCurve::EaseOutSine})
		{
			HoldProgressState state; state.Start(At(0), Picker(curve));
			Check(state.NextWake() == At(500), "picker first wake is 500ms");
			auto frame = state.Advance(At(499));
			Check(frame.visibility == 0 && frame.progress == 0, "picker delay is invisible");
			frame = state.Advance(At(500));
			Check(frame.visibility == 0 && frame.progress == 0, "picker starts progress after 500ms");
			frame = state.Advance(At(590));
			Check(Near(frame.visibility, curve == HoldProgressCurve::EaseInOutCubic ? .5 : std::sqrt(.5)), "picker fade preserves selected legacy curve");
			frame = state.Advance(At(1250));
			Check(frame.visibility == 1 && frame.ringVisibility == 1 && Near(frame.progress, .5), "picker progress middle is 1250ms");
			Check(!state.Advance(At(1999)).justCompleted, "picker lock minus one ms");
			frame = state.Advance(At(2000));
			Check(frame.justCompleted && frame.progress == 1 && frame.visibility == 1 && frame.ringVisibility == 1, "picker lock keeps hint while starting ring exit");
			frame = state.Advance(At(2180));
			Check(frame.visibility == 1 && frame.ringVisibility == 0 && frame.completedMix == 1 && !state.NextWake(), "locked hint holds without continuous wake");
			state.Cancel(At(2200)); frame = state.Advance(At(2290));
			const double remaining = curve == HoldProgressCurve::EaseInOutCubic ? .5 : 1 - std::sqrt(.5);
			Check(Near(frame.visibility, remaining) && frame.progress == 0, "picker release clears progress and fades hint");
			state.Advance(At(2380)); Check(!state.NextWake(), "picker cancellation finishes all fade tracks");
		}
	}
	// 逐毫秒与迁移前实际使用的动画值比较，避免只验证新实现自身公式。
	template<class Legacy> void CompareLegacy(HoldProgressConfig config, int cancelMs = -1)
	{
		HoldProgressState state; state.Start(At(0), config);
		Legacy hint{0}, ring{0}, mix{0};
		bool equivalent = true;
		const int begin = static_cast<int>(config.delay.count());
		const int end = begin + static_cast<int>(config.duration.count());
		const int cancelAt = cancelMs < 0 ? end + 200 : cancelMs;
		const bool picker = config.completion == HoldProgressCompletion::KeepHint;
		for (int ms = 0; ms <= end + 500; ++ms)
		{
			if (ms == cancelAt) state.Cancel(At(ms));
			const bool visible = ms >= begin && ms < cancelAt && (picker || ms < end);
			const bool ringVisible = visible && ms < end;
			hint.SetTar(visible ? 1 : 0, std::chrono::duration<double>(visible ? config.fadeIn : config.fadeOut).count());
			ring.SetTar(ringVisible ? 1 : 0, std::chrono::duration<double>(ringVisible ? config.fadeIn : config.fadeOut).count());
			mix.SetTar(visible && ms >= end ? 1 : 0, std::chrono::duration<double>(config.fadeIn).count());
			const UiAnimationAdvanceContextClass context{.001, 1.0, true};
			(void)UiAdvanceAnimation(hint, context); (void)UiAdvanceAnimation(ring, context); (void)UiAdvanceAnimation(mix, context);
			const auto frame = state.Advance(At(ms), {.001});
			equivalent &= Near(frame.visibility, static_cast<double>(hint.val)) && Near(frame.ringVisibility, static_cast<double>(ring.val))
				&& Near(frame.completedMix, static_cast<double>(mix.val));
		}
		Check(equivalent, "progress alpha matches legacy animation at every millisecond");
	}
	void TestFailureAndGeneration()
	{
		HoldProgressState state;
		Check(state.SetDeviceGeneration(1) && !state.SetDeviceGeneration(1), "only device generation changes reset resource state");
		state.Start(At(0)); state.Advance(At(200)); state.Advance(At(600));
		state.MarkResourceFailed(); const auto failed = state.Advance(At(1000));
		Check(state.ResourceFailed() && !state.NextWake() && failed.visibility == 0 && Near(failed.progress, .5)
			&& !failed.justCompleted && !state.Completed(), "resource failure stops this feedback attempt and completion");
		Check(!state.SetDeviceGeneration(1) && state.ResourceFailed(), "same generation cannot retry failed resources");
		Check(state.SetDeviceGeneration(2) && !state.ResourceFailed(), "new device clears failure barrier");
		Check(state.Advance(At(1001)).justCompleted, "device reset preserves original gesture deadline");
		state.MarkResourceFailed(); state.Start(At(2000));
		Check(!state.ResourceFailed() && !state.Completed() && state.NextWake() == At(2001), "next gesture resets flags while previous feedback still needs fade frames");
		state.Cancel(At(2100));
		Check(!state.Advance(At(3000)).justCompleted, "cancel before delay never produces completion");
	}
	void TestOwnerSampling()
	{
		HoldProgressState state;
		HoldProgressConfig config; config.curve = HoldProgressCurve::EaseInOutCubic;
		state.Start(At(0), config);
		auto sample = state.Sample(At(199));
		Check(!sample.visible && !sample.due && sample.progress == 0 && sample.nextWake == At(200),
			"worker sampling waits for exact geometry ring deadline");
		sample = state.Sample(At(200));
		Check(sample.visible && !sample.due && sample.progress == 0 && sample.nextWake == At(1000),
			"worker sampling schedules completion without per-frame wakes");
		Check(Near(state.Sample(At(600)).progress, .5), "render sampling shares geometry progress");
		sample = state.Sample(At(1000));
		Check(sample.due && sample.progress == 1 && !sample.nextWake && !state.Completed(),
			"Up may observe due commit without automatically accepting business action");
		state.MarkResourceFailed();
		Check(state.Sample(At(1001)).due, "failed ring cannot suppress business deadline");
		state.SetDeviceGeneration(1);
		state.ShiftTime(-100ms);
		Check(state.Sample(At(900)).due && !state.Sample(At(899)).due, "clock translation preserves exact deadline");
		state.Start(At(2000), Picker());
		Check(!state.Sample(At(2499)).visible && state.Sample(At(2500)).visible
			&& !state.Sample(At(3999)).due && state.Sample(At(4000)).due, "picker sampling retains 500 plus 1500 ms");
		state.Start(At(2600), Picker());
		Check(!state.Sample(At(3100)).due && state.Sample(At(3100)).progress == 0, "movement restarts picker stable deadline");
	}
	void TestExternalFeedback()
	{
		HoldProgressState state;
		HoldProgressConfig config; config.curve = HoldProgressCurve::EaseInOutCubic;
		state.Start(At(0), config);
		UiValueClass legacy{0};
		bool equivalent = true;
		for (int ms = 0; ms <= 1500; ++ms)
		{
			// 稀疏/零 dt、淡出中重按与业务拒绝，均与图形原有 Value 逐帧比较。
			if (ms == 1080) state.Start(At(ms), config);
			const bool visible = (ms >= 200 && ms < 1000) || (ms >= 1280 && ms < 1400);
			const double dt = ms % 11 == 0 ? 0.0 : .001;
			const bool enabled = ms < 1450;
			legacy.SetTar(visible ? 1.0 : 0.0, visible ? .18 : .14);
			UiAnimationAdvanceResultClass old{};
			if (!enabled || dt > 0 || legacy.IsSame()) old = UiAdvanceAnimation(legacy, {dt, 1.0, enabled});
			else old.active = true;
			const auto frame = state.AdvanceFeedback(At(ms), visible, visible, false, {dt, 1.0, enabled});
			equivalent &= Near(frame.visibility, static_cast<double>(legacy.val)) && frame.changed == old.changed
				&& frame.active == old.active;
		}
		Check(equivalent, "externally accepted geometry feedback matches legacy value and render wake flags");
	}
}

int RunUi3ProgressTests()
{
	TestGeometry(); TestPicker(); TestFailureAndGeneration(); TestOwnerSampling(); TestExternalFeedback();
	CompareLegacy<UiPctClass>({});
	CompareLegacy<UiValueClass>(Picker());
	CompareLegacy<UiValueClass>(Picker(), 501); // 三次淡入刚开始即取消，不能套用 Pct 的微小差值归位。
	CompareLegacy<UiPctClass>(Picker(HoldProgressCurve::EaseOutSine));
	return failures;
}
