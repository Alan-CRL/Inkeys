#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <functional>
#include <iostream>
#include <limits>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include "../Inkeys/Inkeys/Drawing/Draw3/Draw3.WhiteboardPattern.h"
#include "../Inkeys/Inkeys/UI/Whiteboard/Whiteboard.PatternFollow.h"

import Inkeys.Drawing.Draw3.canvas_navigation;

// 画布平移的纯 CPU 内核测试：估速器、闭式摩擦积分、摩擦耦合状态机。
// 输入流由确定性 LCG 生成（对应任务 research/sim/streams.py 的投递模型：
// 回调到达时间戳 + 抖动 + 1 ms 量化 + 每帧只取各触点最新快照的邮箱），不依赖窗口/GPU。
namespace
{
	using namespace Inkeys::Drawing::Draw3;

	constexpr int64_t kFreq = 10'000'000; // 10 MHz QPC
	constexpr int64_t kOrigin = 5'000'000'000LL; // QPC 数值像真实机器一样很大
	constexpr double kPi = 3.14159265358979323846;

	struct Checks
	{
		int failures = 0;

		void operator()(bool condition, const char* name)
		{
			if (condition) return;
			++failures;
			std::cerr << "[Draw3CanvasPan] failed: " << name << '\n';
		}

		void Within(double value, double low, double high, const char* name)
		{
			if (value >= low && value <= high) return; // NaN 同样判失败
			++failures;
			std::cerr << "[Draw3CanvasPan] failed: " << name << " value=" << value
				<< " expected=[" << low << ", " << high << "]\n";
		}
	};

	std::string Fmt(double value, int digits = 2)
	{
		char buffer[64];
		std::snprintf(buffer, sizeof(buffer), "%.*f", digits, value);
		return buffer;
	}

	void Info(const std::string& text)
	{
		std::cout << "[Draw3CanvasPan] " << text << '\n';
	}

	int64_t Qpc(double seconds)
	{
		return kOrigin + static_cast<int64_t>(std::llround(seconds * static_cast<double>(kFreq)));
	}

	double Seconds(int64_t qpc)
	{
		return static_cast<double>(qpc - kOrigin) / static_cast<double>(kFreq);
	}

	int64_t Ms(double milliseconds)
	{
		return static_cast<int64_t>(std::llround(milliseconds * static_cast<double>(kFreq) / 1000.0));
	}

	struct Lcg
	{
		uint64_t state;

		explicit Lcg(uint64_t seed) : state(seed * 2862933555777941757ULL + 3037000493ULL) {}

		double Uniform()
		{
			state = state * 6364136223846793005ULL + 1442695040888963407ULL;
			return static_cast<double>(state >> 11) * (1.0 / 9007199254740992.0);
		}

		double Gauss()
		{
			const double u1 = (std::max)(Uniform(), 1.0e-12);
			const double u2 = Uniform();
			return std::sqrt(-2.0 * std::log(u1)) * std::cos(2.0 * kPi * u2);
		}
	};

	// ---- 手指轨迹（沿方向的一维位置，随 Down 起算的秒数） ----
	struct Profile
	{
		std::function<double(double)> position;
		double lift = 0.0;
		double dirX = 1.0;
		double dirY = 0.0;
		double offsetX = 0.0;
		double offsetY = 0.0;
	};

	Profile Steady(double velocity, double ramp = 0.040, double total = 0.250)
	{
		return { [=](double t)
		{
			t = (std::min)(t, total);
			return t <= ramp ? velocity * t * t / (2.0 * ramp) : velocity * (t - ramp / 2.0);
		}, total };
	}

	Profile Linear(double velocity, double total)
	{
		return { [=](double t) { return velocity * (std::min)(t, total); }, total };
	}

	// 恒加速且抬起时仍在加速：一阶拟合滞后的最坏情形。
	Profile FlickLinear(double releaseVelocity, double duration)
	{
		return { [=](double t)
		{
			t = (std::min)(t, duration);
			return releaseVelocity * t * t / (2.0 * duration);
		}, duration };
	}

	// 快速起手、抬起时速度平台。
	Profile FlickSine(double releaseVelocity, double duration)
	{
		return { [=](double t)
		{
			t = (std::min)(t, duration);
			return releaseVelocity * (2.0 * duration / kPi) *
				(1.0 - std::cos(kPi * t / (2.0 * duration)));
		}, duration };
	}

	// 加速 -> 巡航 -> 减速到停 -> 停住 rest 秒后抬起（真实释放速度为 0）。
	Profile StopLift(double velocity, double rest, double ramp = 0.040,
		double cruise = 0.120, double decel = 0.100)
	{
		const double t1 = ramp;
		const double t2 = ramp + cruise;
		const double t3 = t2 + decel;
		return { [=](double t)
		{
			t = (std::min)(t, t3 + rest);
			if (t <= t1) return velocity * t * t / (2.0 * ramp);
			const double p1 = velocity * ramp / 2.0;
			if (t <= t2) return p1 + velocity * (t - t1);
			const double p2 = p1 + velocity * cruise;
			if (t <= t3)
			{
				const double u = t - t2;
				return p2 + velocity * u - velocity * u * u / (2.0 * decel);
			}
			return p2 + velocity * decel / 2.0;
		}, t3 + rest };
	}

	Profile Hold(double seconds)
	{
		return { [](double) { return 0.0; }, seconds };
	}

	// ---- 投递模型 ----
	struct Device
	{
		double rateHz = 120.0;
		double positionNoise = 0.15; // DIP
		double latencyMs = 2.0;
		double jitterMs = 0.7; // 半正态
		double quantMs = 1.0;
	};

	Device Exact(double rateHz = 120.0)
	{
		return { rateHz, 0.0, 0.0, 0.0, 0.0 };
	}

	struct Event
	{
		CanvasPanContactPhase phase = CanvasPanContactPhase::Move;
		int64_t qpc = 0;
		double x = 0.0;
		double y = 0.0;
	};

	std::vector<Event> MakeEvents(const Profile& profile, const Device& device, Lcg& rng,
		double startSeconds)
	{
		struct Report
		{
			CanvasPanContactPhase phase;
			double t;
			double s;
		};
		std::vector<Report> reports;
		const double period = 1.0 / device.rateHz;
		const auto noisy = [&](double t)
		{
			return profile.position(t) +
				(device.positionNoise > 0.0 ? rng.Gauss() * device.positionNoise : 0.0);
		};
		double lastTrue = profile.position(0.0);
		reports.push_back({ CanvasPanContactPhase::Down, 0.0, noisy(0.0) });
		for (int k = 1; k * period < profile.lift - 1.0e-9; ++k)
		{
			const double t = k * period;
			const double s = profile.position(t);
			if (std::fabs(s - lastTrue) >= 0.03) // 触摸屏不上报静止手指
			{
				reports.push_back({ CanvasPanContactPhase::Move, t, noisy(t) });
				lastTrue = s;
			}
		}
		if (std::fabs(profile.position(profile.lift) - lastTrue) >= 0.03)
			reports.push_back({ CanvasPanContactPhase::Move, profile.lift, noisy(profile.lift) });
		reports.push_back({ CanvasPanContactPhase::Up, profile.lift, reports.back().s });

		std::vector<Event> events;
		double last = -1.0;
		for (const Report& report : reports)
		{
			double arrival = report.t + device.latencyMs / 1000.0 +
				(device.jitterMs > 0.0 ? std::fabs(rng.Gauss()) * device.jitterMs / 1000.0 : 0.0);
			arrival = (std::max)(arrival, last + 0.00002);
			if (device.quantMs > 0.0)
			{
				const double quantum = device.quantMs / 1000.0;
				arrival = std::ceil(arrival / quantum - 1.0e-9) * quantum;
			}
			arrival = (std::max)(arrival, last);
			last = arrival;
			events.push_back({ report.phase, Qpc(startSeconds + arrival),
				profile.offsetX + profile.dirX * report.s,
				profile.offsetY + profile.dirY * report.s });
		}
		return events;
	}

	// 渲染循环：每帧每触点只取 q<=T 的最新事件（邮箱覆盖语义，Up 会盖住未消费的 Move）；
	// 未见过的触点先补一个 Down 样本。
	struct Logged
	{
		int64_t qpc = 0;
		CanvasPanFrameOutput out = {};
	};

	class Rig
	{
	public:
		explicit Rig(double fps = 120.0, double start = 0.0, double jitterMs = 0.0,
			uint64_t seed = 11)
			: fps_(fps), start_(start), jitterMs_(jitterMs), rng_(seed) {}

		void Add(uint64_t key, std::vector<Event> events)
		{
			fingers_.push_back({ key, std::move(events), 0, false });
		}

		const CanvasPanFrameOutput& Frame()
		{
			double t = start_ + static_cast<double>(index_++) / fps_;
			if (jitterMs_ > 0.0) t += rng_.Gauss() * jitterMs_ / 1000.0;
			int64_t qpc = Qpc(t);
			if (qpc <= lastQpc_) qpc = lastQpc_ + 1;
			lastQpc_ = qpc;
			samples_.clear();
			for (Finger& finger : fingers_)
			{
				size_t latest = finger.events.size();
				while (finger.next < finger.events.size() && finger.events[finger.next].qpc <= qpc)
					latest = finger.next++;
				if (latest == finger.events.size()) continue;
				if (!finger.announced)
				{
					finger.announced = true;
					const Event& down = finger.events[0];
					samples_.push_back({ finger.key, down.qpc, down.x, down.y,
						CanvasPanContactPhase::Down });
					if (latest == 0) continue;
				}
				const Event& event = finger.events[latest];
				samples_.push_back({ finger.key, event.qpc, event.x, event.y, event.phase });
			}
			CanvasPanFrameInput input;
			input.frameQpc = qpc;
			input.qpcFrequency = kFreq;
			input.contacts = samples_;
			input.penHover = penHover;
			input.penContact = penContact;
			log.push_back({ qpc, dynamics.Step(input) });
			return log.back().out;
		}

		double NextFrameSeconds() const { return start_ + static_cast<double>(index_) / fps_; }

		void RunUntil(double seconds)
		{
			while (NextFrameSeconds() <= seconds) Frame();
		}

		bool RunUntilReleased(int maxFrames = 6000)
		{
			while (maxFrames-- > 0)
				if (Frame().released) return true;
			return false;
		}

		CanvasPanDynamics dynamics;
		std::vector<Logged> log;
		bool penHover = false;
		bool penContact = false;

	private:
		struct Finger
		{
			uint64_t key = 0;
			std::vector<Event> events;
			size_t next = 0;
			bool announced = false;
		};

		double fps_;
		double start_;
		double jitterMs_;
		Lcg rng_;
		size_t index_ = 0;
		int64_t lastQpc_ = 0;
		std::vector<Finger> fingers_;
		std::vector<CanvasPanContactSample> samples_;
	};

	double SumDelta(const Rig& rig, double from, double to)
	{
		double sum = 0.0;
		for (const Logged& frame : rig.log)
		{
			const double t = Seconds(frame.qpc);
			if (t >= from && t < to) sum += frame.out.contentDelta.x;
		}
		return sum;
	}

	double MaxVelocityStep(const Rig& rig, double from, double to)
	{
		double worst = 0.0;
		for (size_t index = 1; index < rig.log.size(); ++index)
		{
			const double t = Seconds(rig.log[index].qpc);
			if (t < from || t >= to) continue;
			worst = (std::max)(worst, std::fabs(
				static_cast<double>(rig.log[index].out.contentVelocity.x) -
				static_cast<double>(rig.log[index - 1].out.contentVelocity.x)));
		}
		return worst;
	}

	// 首个满足条件的帧时刻（秒）；没有则返回 +inf。
	double FirstTime(const Rig& rig, double from, const std::function<bool(const Logged&)>& predicate)
	{
		for (const Logged& frame : rig.log)
			if (Seconds(frame.qpc) >= from && predicate(frame)) return Seconds(frame.qpc);
		return std::numeric_limits<double>::infinity();
	}

	// 第一个时刻不早于 seconds 的帧上的画布 x 速度。
	double VelocityXAt(const Rig& rig, double seconds)
	{
		for (const Logged& frame : rig.log)
			if (Seconds(frame.qpc) >= seconds) return frame.out.contentVelocity.x;
		return std::numeric_limits<double>::quiet_NaN();
	}

	// 一次稳速甩动并释放，返回释放帧时刻。画布此后以自由摩擦滑行。
	double PrepareCoasting(Rig& rig, double speed, double dirX = 1.0, double dirY = 0.0)
	{
		Lcg rng(1);
		Profile profile = Steady(speed);
		profile.dirX = dirX;
		profile.dirY = dirY;
		rig.Add(1, MakeEvents(profile, Exact(), rng, 0.0));
		rig.RunUntilReleased();
		return Seconds(rig.log.back().qpc);
	}

	// 直接驱动：用于精确构造非法/边界输入。
	CanvasPanContactSample Sample(uint64_t key, double ms, double x, double y,
		CanvasPanContactPhase phase = CanvasPanContactPhase::Move)
	{
		return { key, Qpc(ms / 1000.0), x, y, phase };
	}

	CanvasPanFrameOutput StepAt(CanvasPanDynamics& dynamics, double ms,
		const std::vector<CanvasPanContactSample>& samples, bool penHover = false,
		bool penContact = false)
	{
		CanvasPanFrameInput input;
		input.frameQpc = Qpc(ms / 1000.0);
		input.qpcFrequency = kFreq;
		input.contacts = samples;
		input.penHover = penHover;
		input.penContact = penContact;
		return dynamics.Step(input);
	}

	// ============================== 估速器 ==============================
	CanvasPanVelocityEstimate FeedTracker(CanvasPanVelocityTracker& tracker,
		const std::vector<Event>& events, double frameSeconds = 0.0)
	{
		for (const Event& event : events)
			if (event.phase != CanvasPanContactPhase::Up &&
				event.phase != CanvasPanContactPhase::Cancelled)
				tracker.AddSample(event.qpc, event.x, event.y, kFreq, frameSeconds);
		return tracker.Estimate(kFreq);
	}

	void TestTrackerAccuracy(Checks& check)
	{
		double worstSteady = 0.0;
		double lowAccel = 10.0;
		double highAccel = 0.0;
		double highPlateau = 0.0;
		bool allValid = true;
		for (uint64_t seed = 1; seed <= 300; ++seed)
		{
			Lcg rng(seed);
			const Device device;
			CanvasPanVelocityTracker steady;
			const auto a = FeedTracker(steady, MakeEvents(Steady(3000.0), device, rng, 0.0));
			CanvasPanVelocityTracker accel;
			const auto b = FeedTracker(accel, MakeEvents(FlickLinear(4000.0, 0.080), device, rng, 0.0));
			CanvasPanVelocityTracker plateau;
			const auto c = FeedTracker(plateau, MakeEvents(FlickSine(4000.0, 0.120), device, rng, 0.0));
			allValid = allValid && a.valid && b.valid && c.valid;
			worstSteady = (std::max)(worstSteady, std::fabs(a.x / 3000.0 - 1.0));
			lowAccel = (std::min)(lowAccel, b.x / 4000.0);
			highAccel = (std::max)(highAccel, b.x / 4000.0);
			highPlateau = (std::max)(highPlateau, c.x / 4000.0);
		}
		check(allValid, "tracker emits velocity for every research stream");
		check.Within(worstSteady, 0.0, 0.08, "steady 3000 DIP/s estimate within 8%");
		check.Within(lowAccel, 0.85, 1.10, "accelerating flick lowest ratio (was 0.47 with linear fit)");
		check.Within(highAccel, 0.85, 1.10, "accelerating flick highest ratio");
		check.Within(highPlateau, 0.0, 1.20, "plateau flick does not overshoot more than 20%");
		Info("tracker steady worst |err|=" + Fmt(worstSteady, 3) + " accel ratio=[" +
			Fmt(lowAccel, 3) + "," + Fmt(highAccel, 3) + "] plateau max=" + Fmt(highPlateau, 3));
	}

	void TestTrackerValidity(Checks& check)
	{
		const int64_t t0 = Qpc(1.0);
		CanvasPanVelocityTracker tracker;
		check(!tracker.Estimate(kFreq).valid, "empty tracker has no velocity");
		tracker.AddSample(t0, 0.0, 0.0, kFreq);
		tracker.AddSample(t0 + Ms(8), 24.0, 0.0, kFreq);
		check(!tracker.Estimate(kFreq).valid, "two samples never emit a velocity");
		tracker.AddSample(t0 + Ms(16), 48.0, 0.0, kFreq);
		check(tracker.SampleCount() == 3 && !tracker.Estimate(kFreq).valid,
			"three samples spanning 16 ms are still invalid");
		tracker.AddSample(t0 + Ms(24), 72.0, 0.0, kFreq);
		const auto valid = tracker.Estimate(kFreq);
		check(valid.valid && std::fabs(valid.x - 3000.0) < 1.0 && std::fabs(valid.y) < 1.0,
			"four samples spanning 24 ms emit 3000 DIP/s");

		// 回归：重置后 3 ms 的单个 24 DIP Move 曾被旧估速器当作 8000 DIP/s。
		CanvasPanVelocityTracker spike;
		spike.AddSample(t0, 0.0, 0.0, kFreq);
		spike.AddSample(t0 + Ms(3), 24.0, 0.0, kFreq);
		check(!spike.Estimate(kFreq).valid, "3 ms / 24 DIP pair must not produce 8000 DIP/s");

		// Down 与首个 Move 背靠背(0.15 ms)：合并为较新样本，之后速度不被首跳污染。
		CanvasPanVelocityTracker burst;
		burst.AddSample(t0, 0.0, 0.0, kFreq);
		burst.AddSample(t0 + Ms(0.15), 25.0, 0.0, kFreq);
		for (int index = 1; index <= 5; ++index)
			burst.AddSample(t0 + Ms(0.15) + Ms(8.333 * index), 25.0 + 25.0 * index, 0.0, kFreq);
		const auto merged = burst.Estimate(kFreq);
		check(burst.SampleCount() == 6, "0.15 ms pair merges into one sample");
		check.Within(merged.x / 3000.0, 0.9, 1.2, "merged burst pair stays within 1.2x");
		check(merged.x < kCanvasPanVelocityLimitDipPerSecond * 0.5, "merged burst pair is far from the clamp");

		// 1 kHz 逐事件输入不能被链式合并成单个样本。
		CanvasPanVelocityTracker dense;
		for (int index = 0; index <= 60; ++index)
			dense.AddSample(t0 + Ms(index), 3.0 * index, 0.0, kFreq);
		const auto denseEstimate = dense.Estimate(kFreq);
		check(dense.SampleCount() > 10 && denseEstimate.valid &&
			std::fabs(denseEstimate.x - 3000.0) < 30.0, "1 kHz stream keeps a usable window");

		// 间隔 >40 ms 清窗；帧间隔大（渲染卡顿）时放宽，不把卡顿当手停。
		CanvasPanVelocityTracker gap;
		for (int index = 0; index < 6; ++index)
			gap.AddSample(t0 + Ms(8.0 * index), 24.0 * index, 0.0, kFreq, 0.008);
		gap.AddSample(t0 + Ms(40.0 + 45.0), 24.0 * 5 + 135.0, 0.0, kFreq, 0.008);
		check(gap.SampleCount() == 1 && !gap.Estimate(kFreq).valid, "45 ms gap clears the window");
		CanvasPanVelocityTracker stall;
		for (int index = 0; index < 6; ++index)
			stall.AddSample(t0 + Ms(8.0 * index), 24.0 * index, 0.0, kFreq, 0.008);
		stall.AddSample(t0 + Ms(40.0 + 45.0), 24.0 * 5 + 135.0, 0.0, kFreq, 0.060);
		check(stall.SampleCount() == 7, "60 ms frame stall keeps the window while the finger moves");
	}

	void TestTrackerInvalidInput(Checks& check)
	{
		const int64_t t0 = Qpc(2.0);
		CanvasPanVelocityTracker tracker;
		for (int index = 0; index < 5; ++index)
			tracker.AddSample(t0 + Ms(8.0 * index), 24.0 * index, 0.0, kFreq);
		const auto before = tracker.Estimate(kFreq);
		const size_t count = tracker.SampleCount();
		const int64_t last = tracker.LastSampleQpc();
		const double nan = std::numeric_limits<double>::quiet_NaN();
		const double inf = std::numeric_limits<double>::infinity();
		check(!tracker.AddSample(t0 + Ms(50), nan, 0.0, kFreq), "NaN x rejected");
		check(!tracker.AddSample(t0 + Ms(50), 0.0, inf, kFreq), "inf y rejected");
		check(!tracker.AddSample(0, 1.0, 1.0, kFreq), "zero qpc rejected");
		check(!tracker.AddSample(t0 + Ms(50), 1.0, 1.0, 0), "zero frequency rejected");
		check(!tracker.AddSample(last, 500.0, 0.0, kFreq), "duplicate qpc rejected");
		check(!tracker.AddSample(last - Ms(3), 500.0, 0.0, kFreq), "backwards qpc rejected");
		const auto after = tracker.Estimate(kFreq);
		check(tracker.SampleCount() == count && tracker.LastSampleQpc() == last &&
			after.valid == before.valid && after.x == before.x && after.y == before.y,
			"rejected samples leave the tracker unchanged");

		// 速度上限等比缩放并保持方向。
		CanvasPanVelocityTracker fast;
		for (int index = 0; index < 6; ++index)
			fast.AddSample(t0 + Ms(8.0 * index), 600.0 * index, 800.0 * index, kFreq);
		const auto clamped = fast.Estimate(kFreq);
		check.Within(std::hypot(clamped.x, clamped.y), kCanvasPanVelocityLimitDipPerSecond - 1.0e-6,
			kCanvasPanVelocityLimitDipPerSecond + 1.0e-6, "speed clamps to the limit");
		check.Within(clamped.y / clamped.x, 4.0 / 3.0 - 1.0e-9, 4.0 / 3.0 + 1.0e-9,
			"proportional clamp keeps direction");
		fast.ClearAxis(true, false);
		const auto xCleared = fast.Estimate(kFreq);
		check(xCleared.valid && std::fabs(xCleared.x) < 1.0e-9 && xCleared.y > 1000.0,
			"axis clear zeroes only the clamped axis");
	}

	void TestReleaseGate(Checks& check)
	{
		check.Within(CanvasPanReleaseGate(0.020), 1.0, 1.0, "gate at 20 ms");
		check.Within(CanvasPanReleaseGate(0.030), 1.0, 1.0, "gate at 30 ms");
		check.Within(CanvasPanReleaseGate(0.050), 0.5 - 1.0e-9, 0.5 + 1.0e-9, "gate at 50 ms");
		check.Within(CanvasPanReleaseGate(0.070), 0.0, 0.0, "gate at 70 ms");
		check.Within(CanvasPanReleaseGate(0.095), 0.0, 0.0, "gate at 95 ms");
		check.Within(CanvasPanReleaseGate(-0.001), 0.0, 0.0, "negative age has no inertia");
		check.Within(CanvasPanReleaseGate(std::numeric_limits<double>::quiet_NaN()), 0.0, 0.0, "NaN age");
		check.Within(CanvasPanReleaseGate(std::numeric_limits<double>::infinity()), 0.0, 0.0, "infinite age");
	}

	// ============================== 闭式摩擦积分 ==============================
	struct Glide
	{
		double distance = 0.0;
		double seconds = 0.0;
		bool monotonic = true;
		bool finished = false;
	};

	Glide Integrate(double speed, const std::vector<double>& dts, CanvasPanFriction friction)
	{
		Glide glide;
		for (int step = 0; step < 100000; ++step)
		{
			const double dt = dts[static_cast<size_t>(step) % dts.size()];
			const CanvasPanFrictionStep next = StepCanvasPanFriction(speed, dt, friction);
			if (next.speed > speed + 1.0e-9 || next.speed < 0.0 || next.distance < 0.0)
				glide.monotonic = false;
			glide.distance += next.distance;
			glide.seconds += (std::min)(dt, kCanvasPanMaximumStepSeconds);
			speed = next.speed;
			if (speed <= 0.0)
			{
				glide.finished = true;
				break;
			}
		}
		return glide;
	}

	void TestFrictionIntegrator(Checks& check)
	{
		const double d3000 = CanvasPanGlideDistance(3000.0, kCanvasPanFreeFriction);
		const double d12000 = CanvasPanGlideDistance(12000.0, kCanvasPanFreeFriction);
		check.Within(d3000, 683.0, 685.0, "closed-form glide distance at 3000 DIP/s is 684 +-1");
		check.Within(d12000, 3264.0, 3268.0, "closed-form glide distance at 12000 DIP/s is 3266 +-2");
		const Glide uniform3000 = Integrate(3000.0, { 1.0 / 120.0 }, kCanvasPanFreeFriction);
		const Glide uniform12000 = Integrate(12000.0, { 1.0 / 120.0 }, kCanvasPanFreeFriction);
		check.Within(uniform3000.distance, d3000 - 0.6, d3000, "stepped 3000 DIP/s glide matches closed form");
		check.Within(uniform12000.distance, d12000 - 0.6, d12000, "stepped 12000 DIP/s glide matches closed form");
		check(uniform3000.finished && uniform12000.finished && uniform3000.monotonic &&
			uniform12000.monotonic, "glide stops in finite time with monotonic speed");
		check.Within(uniform12000.seconds, 1.0, 1.2, "12000 DIP/s stops in about 1.1 s");

		// 帧率/卡顿无关：不同 dt 序列总距离一致。
		for (const double speed : { 1000.0, 3000.0, 9000.0, 12000.0 })
		{
			const Glide reference = Integrate(speed, { 1.0 / 120.0 }, kCanvasPanFreeFriction);
			const Glide mixed = Integrate(speed, { 1.0 / 240.0, 1.0 / 60.0, 0.03, 1.0 / 120.0 },
				kCanvasPanFreeFriction);
			const Glide coarse = Integrate(speed, { 0.1 }, kCanvasPanFreeFriction);
			check.Within(std::fabs(mixed.distance - reference.distance), 0.0, 0.5,
				"mixed dt schedule keeps total glide distance");
			check.Within(std::fabs(coarse.distance - reference.distance), 0.0, 0.5,
				"100 ms steps keep total glide distance");
		}

		// dt 非法/越界。
		const CanvasPanFrictionStep zero = StepCanvasPanFriction(3000.0, 0.0, kCanvasPanFreeFriction);
		const CanvasPanFrictionStep negative = StepCanvasPanFriction(3000.0, -0.01, kCanvasPanFreeFriction);
		const CanvasPanFrictionStep nan = StepCanvasPanFriction(3000.0,
			std::numeric_limits<double>::quiet_NaN(), kCanvasPanFreeFriction);
		check(zero.distance == 0.0 && zero.speed == 3000.0 && negative.distance == 0.0 &&
			negative.speed == 3000.0 && nan.distance == 0.0 && nan.speed == 3000.0,
			"dt of 0, negative or NaN leaves speed and position unchanged");
		const CanvasPanFrictionStep one = StepCanvasPanFriction(3000.0, 0.1, kCanvasPanFreeFriction);
		double speed = 3000.0;
		double distance = 0.0;
		for (int step = 0; step < 12; ++step)
		{
			const CanvasPanFrictionStep next = StepCanvasPanFriction(speed, 0.1 / 12.0, kCanvasPanFreeFriction);
			distance += next.distance;
			speed = next.speed;
		}
		check.Within(std::fabs(one.distance - distance), 0.0, 0.5, "one 100 ms step equals 12 sub-steps (distance)");
		check.Within(std::fabs(one.speed - speed), 0.0, 1.0, "one 100 ms step equals 12 sub-steps (speed)");
		const CanvasPanFrictionStep capped = StepCanvasPanFriction(3000.0, 1.0, kCanvasPanFreeFriction);
		check(capped.distance == one.distance && capped.speed == one.speed, "dt is capped at 100 ms");
		const CanvasPanFrictionStep slow = StepCanvasPanFriction(10.0, 0.01, kCanvasPanFreeFriction);
		check(slow.distance == 0.0 && slow.speed == 0.0, "below the stop threshold the canvas rests");

		// 误差敏感度：距离近似线性于速度（旧模型为平方）。
		double worstRatio = 0.0;
		for (double v = 1000.0; v <= 9000.0; v += 500.0)
			worstRatio = (std::max)(worstRatio, CanvasPanGlideDistance(2.0 * v, kCanvasPanFreeFriction) /
				CanvasPanGlideDistance(v, kCanvasPanFreeFriction));
		check.Within(worstRatio, 1.0, 2.5, "2x velocity error grows distance by at most 2.5x");
		check.Within(CanvasPanGlideDistance(18000.0, kCanvasPanFreeFriction), 0.0, 5100.0,
			"18000 DIP/s glide stays bounded");
		const Glide hand = Integrate(3000.0, { 1.0 / 120.0 }, kCanvasPanHandFriction);
		check(hand.finished && hand.seconds < 0.25 && hand.distance < 200.0, "hand friction stops fast");
		Info("glide d(3000)=" + Fmt(d3000) + " d(12000)=" + Fmt(d12000) + " d(18000)=" +
			Fmt(CanvasPanGlideDistance(18000.0, kCanvasPanFreeFriction)) + " worst d(2v)/d(v)=" +
			Fmt(worstRatio, 3) + " hand-stop(3000)=" + Fmt(hand.seconds, 3) + "s/" + Fmt(hand.distance) + " DIP");
	}

	// ============================== 状态机：释放与拓扑 ==============================
	void TestSingleFingerRelease(Checks& check)
	{
		Rig rig;
		Lcg rng(5);
		rig.Add(1, MakeEvents(Steady(3000.0), Exact(), rng, 0.0));
		const bool released = rig.RunUntilReleased();
		check(released, "single finger releases");
		const Logged frame = rig.log.back(); // 拷贝：之后继续推帧会使 log 内引用失效
		check.Within(frame.out.releaseSpeed / 3000.0, 0.97, 1.03, "exact stream releases at the hand speed");
		check(frame.out.phase == CanvasPanPhase::Coasting && frame.out.released, "release enters Coasting");
		check(rig.log.front().out.phase == CanvasPanPhase::Locked, "press on a resting canvas locks immediately");
		// 抓住期间内容 1:1 跟手：Down 到最后一次 Move 的总位移 = 手指位移。
		const double pan = SumDelta(rig, -1.0, Seconds(frame.qpc) + 1.0);
		check.Within(pan, 3000.0 * (0.25 - 0.02) - 3.0, 3000.0 * (0.25 - 0.02) + 3.0,
			"locked canvas follows the finger 1:1");
		// 释放后沿闭式曲线滑行到停止。
		const double releasedSpeed = frame.out.releaseSpeed;
		rig.RunUntil(3.0);
		const double glide = SumDelta(rig, Seconds(frame.qpc) + 1.0e-4, 10.0);
		check.Within(std::fabs(glide - CanvasPanGlideDistance(releasedSpeed, kCanvasPanFreeFriction)), 0.0, 1.5,
			"post-release glide follows the closed form");
		check(rig.dynamics.Phase() == CanvasPanPhase::Idle && !rig.dynamics.Active(), "glide ends in Idle");

		// 方向保持：对角甩动。
		Rig diagonal;
		PrepareCoasting(diagonal, 5000.0, 0.6, 0.8);
		const CanvasVector v = diagonal.log.back().out.contentVelocity;
		check.Within(static_cast<double>(v.y) / v.x, 4.0 / 3.0 - 0.01, 4.0 / 3.0 + 0.01, "2D release keeps direction");
		check.Within(std::hypot(v.x, v.y) / 5000.0, 0.97, 1.03, "2D release keeps speed");
	}

	void TestSingleStepDownUpDisplacement(Checks& check)
	{
		for (const bool withMove : { false, true })
		{
			CanvasPanDynamics batched;
			std::vector<CanvasPanContactSample> samples{
				Sample(1, 1.0, 0.0, 0.0, CanvasPanContactPhase::Down) };
			if (withMove) samples.push_back(Sample(1, 3.0, 8.0, -4.0));
			samples.push_back(Sample(1, 5.0, 20.0, -10.0, CanvasPanContactPhase::Up));
			const auto out = StepAt(batched, 8.0, samples);
			check(out.contentDelta.x == 20.0f && out.contentDelta.y == -10.0f,
				"single-step Down/Up keeps the complete endpoint displacement");
			check(out.released && out.phase == CanvasPanPhase::Idle && out.heldContacts == 0 &&
				out.releaseSpeed == 0.0 && out.contentVelocity.x == 0.0f && out.contentVelocity.y == 0.0f,
				"single-step short contact releases without a valid velocity or inertia");
			check(batched.FindTracker(1) == nullptr, "single-step released contact retires its tracker");
			const auto next = StepAt(batched, 16.0, {});
			check(next.contentDelta.x == 0.0f && next.contentDelta.y == 0.0f &&
				next.phase == CanvasPanPhase::Idle && !next.released,
				"single-step contact displacement is consumed exactly once");

			// 同一路径分帧投递，位移与释放行为应与同帧完全一致。
			CanvasPanDynamics split;
			auto frame = StepAt(split, 1.0, { samples.front() });
			double dx = frame.contentDelta.x;
			double dy = frame.contentDelta.y;
			if (withMove)
			{
				frame = StepAt(split, 3.0, { samples[1] });
				dx += frame.contentDelta.x;
				dy += frame.contentDelta.y;
			}
			frame = StepAt(split, 8.0, { samples.back() });
			check(dx + frame.contentDelta.x == out.contentDelta.x &&
				dy + frame.contentDelta.y == out.contentDelta.y && frame.released &&
				frame.phase == out.phase && frame.releaseSpeed == out.releaseSpeed,
				"batching a short contact preserves normal multi-frame behavior");
		}
	}

	void TestStopHandAfterEarlyLift(Checks& check)
	{
		const auto start = [](CanvasPanDynamics& dynamics)
		{
			StepAt(dynamics, 0.0, { Sample(1, 0.0, 0.0, 0.0, CanvasPanContactPhase::Down),
				Sample(2, 0.0, 500.0, 0.0, CanvasPanContactPhase::Down) });
			for (int ms = 8; ms <= 48; ms += 8)
				StepAt(dynamics, ms, { Sample(1, ms, 3.0 * ms, 0.0),
					Sample(2, ms, 500.0 + 3.0 * ms, 0.0) });
		};
		for (const bool batchedLifts : { false, true })
		{
			for (const double stopAgeMs : { 70.0, 75.0, 77.0 })
			{
				CanvasPanDynamics dynamics;
				start(dynamics);
				const auto earlyUp = Sample(1, 50.0, 144.0, 0.0, CanvasPanContactPhase::Up);
				if (!batchedLifts) StepAt(dynamics, 50.0, { earlyUp });
				const double releaseMs = 48.0 + stopAgeMs;
				for (double ms = 56.0; ms < releaseMs; ms += 8.0) StepAt(dynamics, ms, {});
				std::vector<CanvasPanContactSample> releases;
				if (batchedLifts) releases.push_back(earlyUp);
				releases.push_back(Sample(2, releaseMs, 644.0, 0.0, CanvasPanContactPhase::Up));
				const auto out = StepAt(dynamics, releaseMs, releases);
				check(out.released && out.phase == CanvasPanPhase::Idle && out.heldContacts == 0 &&
					out.releaseSpeed == 0.0 && out.contentVelocity.x == 0.0f,
					"stopped remaining finger cannot revive an earlier lifted finger velocity");
				bool noGlide = true;
				for (int frame = 1; frame <= 20; ++frame)
				{
					const auto tail = StepAt(dynamics, releaseMs + 8.0 * frame, {});
					noGlide = noGlide && tail.phase == CanvasPanPhase::Idle &&
						tail.contentDelta.x == 0.0f && tail.contentDelta.y == 0.0f;
				}
				check(noGlide, "early lift followed by a stopped hand leaves no phantom glide");
			}
		}

		// 剩余手持续运动时仍保留早抬手的冻结速度，60 ms 错开抬起不能被按年龄衰减。
		CanvasPanDynamics moving;
		start(moving);
		StepAt(moving, 50.0, { Sample(1, 50.0, 144.0, 0.0, CanvasPanContactPhase::Up) });
		for (int ms = 56; ms <= 104; ms += 8)
			StepAt(moving, ms, { Sample(2, ms, 500.0 + 3.0 * ms, 0.0) });
		const auto release = StepAt(moving, 110.0,
			{ Sample(2, 110.0, 830.0, 0.0, CanvasPanContactPhase::Up) });
		check(release.released && release.phase == CanvasPanPhase::Coasting,
			"moving remaining finger still starts inertia after a 60 ms stagger");
		check.Within(release.releaseSpeed / 3000.0, 0.90, 1.15,
			"moving remaining finger preserves the staggered-lift hand speed");
	}

	// 设备类型由适配层互斥；动力学以不同 key 表示 Touch -> MouseLeft 的非重叠交接。
	void PrepareHandoffGesture(CanvasPanDynamics& dynamics, double speed = 3000.0)
	{
		StepAt(dynamics, 0.0, { Sample(1, 0.0, 0.0, 0.0, CanvasPanContactPhase::Down) });
		for (int ms = 8; ms <= 96; ms += 8)
			StepAt(dynamics, ms, { Sample(1, ms, speed * ms / 1000.0, 0.0) });
	}

	void TestSameStepHandoffTap(Checks& check)
	{
		CanvasPanDynamics batched;
		CanvasPanDynamics split;
		PrepareHandoffGesture(batched);
		PrepareHandoffGesture(split);
		const auto oldUp = Sample(1, 100.0, 300.0, 0.0, CanvasPanContactPhase::Up);
		const auto mouseDown = Sample(2, 105.0, 800.0, 0.0, CanvasPanContactPhase::Down);
		const auto release = StepAt(split, 100.0, { oldUp });
		const auto press = StepAt(split, 105.0, { mouseDown });
		const auto combined = StepAt(batched, 105.0, { oldUp, mouseDown });
		check.Within(std::fabs(combined.contentDelta.x - release.contentDelta.x - press.contentDelta.x),
			0.0, 1.0e-5, "same-step handoff sums the old settle and new slipping displacement");
		check(combined.contentDelta.y == release.contentDelta.y + press.contentDelta.y &&
			combined.contentVelocity.x == press.contentVelocity.x &&
			combined.contentVelocity.y == press.contentVelocity.y && combined.phase == press.phase,
			"same-step handoff matches split-frame delta, velocity and phase");
		check(combined.released && std::fabs(combined.releaseSpeed - 3000.0) < 0.01 &&
			std::fabs(combined.releaseAgeSeconds - 0.004) < 1.0e-9,
			"handoff releases using the old gesture's own Up age");
		check(!combined.handVelocityKnown && batched.FindTracker(1) == nullptr &&
			batched.FindTracker(2) != nullptr, "new mouse tap cannot read the old frozen hand velocity");
		const auto mouseUp = Sample(2, 120.0, 800.0, 0.0, CanvasPanContactPhase::Up);
		const auto tap = StepAt(batched, 120.0, { mouseUp });
		const auto expected = StepAt(split, 120.0, { mouseUp });
		check(tap.contentDelta.x == expected.contentDelta.x && tap.contentDelta.y == expected.contentDelta.y &&
			tap.contentVelocity.x == expected.contentVelocity.x && tap.contentVelocity.y == expected.contentVelocity.y &&
			tap.phase == expected.phase && tap.releaseSpeed == expected.releaseSpeed,
			"still mouse tap release matches the separate-frame path");
		check(tap.released && !tap.handVelocityKnown && tap.phase == CanvasPanPhase::Coasting &&
			tap.releaseSpeed > 2500.0 && tap.releaseSpeed < 2900.0,
			"still tap carries the decaying old inertia without a fresh 3000 DIP/s release");
	}

	void TestSameStepHandoffPress(Checks& check)
	{
		// 覆盖满速、软门限、门限归零及低于最小释放速度，保证复用原来的释放决策。
		for (const double upMs : { 100.0, 146.0, 166.0 })
		{
			for (const double speed : { 3000.0, 180.0 })
			{
				CanvasPanDynamics dynamics;
				PrepareHandoffGesture(dynamics, speed);
				const auto out = StepAt(dynamics, upMs + 5.0, {
					Sample(1, upMs, speed * 0.096, 0.0, CanvasPanContactPhase::Up),
					Sample(2, upMs + 5.0, 800.0, 0.0, CanvasPanContactPhase::Down) });
				const bool slipping = speed == 3000.0 && upMs < 166.0;
				check(out.phase == (slipping ? CanvasPanPhase::Slipping : CanvasPanPhase::Locked) &&
					out.heldContacts == 1, "new handoff press uses the released canvas speed to slip or lock");
				check(!out.handVelocityKnown, "new handoff press starts with its own unknown hand velocity");
			}
		}
	}

	void TestSameStepHandoffMove(Checks& check)
	{
		CanvasPanDynamics batched;
		CanvasPanDynamics split;
		PrepareHandoffGesture(batched);
		PrepareHandoffGesture(split);
		const auto oldUp = Sample(1, 100.0, 300.0, 0.0, CanvasPanContactPhase::Up);
		const auto mouseDown = Sample(2, 105.0, 800.0, 0.0, CanvasPanContactPhase::Down);
		const auto release = StepAt(split, 100.0, { oldUp });
		const auto press = StepAt(split, 105.0, { mouseDown });
		const auto combined = StepAt(batched, 105.0, { oldUp, mouseDown });
		check(combined.phase == CanvasPanPhase::Slipping && combined.contentVelocity.x > 2900.0f &&
			std::fabs(combined.contentDelta.x - release.contentDelta.x - press.contentDelta.x) < 1.0e-5,
			"moving handoff begins by carrying the old inertia through Slipping");
		for (const double ms : { 115.0, 125.0, 135.0 })
		{
			const auto move = Sample(2, ms, 800.0 + (ms - 105.0), (ms - 105.0) * 0.5);
			const auto out = StepAt(batched, ms, { move });
			const auto expected = StepAt(split, ms, { move });
			check(out.contentDelta.x == expected.contentDelta.x && out.contentDelta.y == expected.contentDelta.y &&
				out.contentVelocity.x == expected.contentVelocity.x && out.contentVelocity.y == expected.contentVelocity.y &&
				out.phase == expected.phase, "moving handoff coupling matches normal split-frame Slipping");
			if (ms == 125.0)
			{
				check(out.phase == CanvasPanPhase::Slipping && out.handVelocityKnown &&
					std::fabs(out.handVelocity.x - 1000.0f) < 0.01f &&
					std::fabs(out.handVelocity.y - 500.0f) < 0.01f,
					"coupling uses only the new mouse velocity rather than averaging the old Touch velocity");
				check(out.contentVelocity.x > out.handVelocity.x && out.contentVelocity.x < press.contentVelocity.x &&
					out.contentVelocity.y > 0.0f, "hand friction couples carried inertia toward the new moving hand");
			}
		}
		const auto mouseUp = Sample(2, 140.0, 830.0, 15.0, CanvasPanContactPhase::Up);
		const auto out = StepAt(batched, 140.0, { mouseUp });
		const auto expected = StepAt(split, 140.0, { mouseUp });
		check(out.released && out.phase == expected.phase && out.contentDelta.x == expected.contentDelta.x &&
			out.contentDelta.y == expected.contentDelta.y && out.contentVelocity.x == expected.contentVelocity.x &&
			out.contentVelocity.y == expected.contentVelocity.y,
			"moving handoff releases the normally coupled canvas velocity");
	}

	void TestSameStepHandoffTrackerGap(Checks& check)
	{
		CanvasPanDynamics batched;
		CanvasPanDynamics split;
		for (CanvasPanDynamics* dynamics : { &batched, &split })
		{
			StepAt(*dynamics, 0.0, { Sample(1, 0.0, 0.0, 0.0, CanvasPanContactPhase::Down) });
			for (int ms = 10; ms <= 50; ms += 10)
				StepAt(*dynamics, ms, { Sample(1, ms, 3.0 * ms, 0.0) });
			StepAt(*dynamics, 96.0, {});
		}
		// 80 ms 采样断口：整帧 dt=84 ms 会保窗，旧 Up 段 dt=36 ms 必须清窗。
		const auto oldMove = Sample(1, 130.0, 390.0, 0.0);
		const auto oldUp = Sample(1, 132.0, 396.0, 0.0, CanvasPanContactPhase::Up);
		const auto mouseDown = Sample(2, 137.0, 800.0, 0.0, CanvasPanContactPhase::Down);
		const auto release = StepAt(split, 132.0, { oldMove, oldUp });
		const auto press = StepAt(split, 180.0, { mouseDown });
		const auto out = StepAt(batched, 180.0, { oldMove, oldUp, mouseDown });
		check(release.released && release.releaseSpeed == 0.0 && release.phase == CanvasPanPhase::Idle,
			"old release clears its tracker at the segment's own gap threshold");
		check(out.released && out.releaseSpeed == release.releaseSpeed &&
			out.phase == press.phase && out.phase == CanvasPanPhase::Locked &&
			out.contentVelocity.x == press.contentVelocity.x && out.contentVelocity.y == press.contentVelocity.y &&
			out.contentDelta.x == release.contentDelta.x + press.contentDelta.x &&
			out.contentDelta.y == release.contentDelta.y + press.contentDelta.y,
			"same-step handoff tracker ingestion uses the same frame interval as split delivery");
	}

	void TestStopHandNoPhantomInertia(Checks& check)
	{
		double worst50 = 0.0;
		double worst70 = 0.0;
		double worstGlide = 0.0;
		bool exactZero = true;
		for (uint64_t seed = 1; seed <= 120; ++seed)
		{
			{
				Rig rig(120.0, 0.0, 0.3, seed);
				Lcg rng(seed);
				rig.Add(1, MakeEvents(StopLift(3000.0, 0.050), Device(), rng, 0.0));
				rig.RunUntilReleased();
				worst50 = (std::max)(worst50, rig.log.back().out.releaseSpeed);
				const int64_t releaseFrameQpc = rig.log.back().qpc;
				rig.RunUntil(3.0);
				worstGlide = (std::max)(worstGlide, SumDelta(rig, Seconds(releaseFrameQpc) + 1.0e-4, 10.0));
			}
			for (const double rest : { 0.070, 0.090, 0.150 })
			{
				Rig rig(120.0, 0.0, 0.3, seed + 1000);
				Lcg rng(seed + 1000);
				rig.Add(1, MakeEvents(StopLift(3000.0, rest), Device(), rng, 0.0));
				const bool released = rig.RunUntilReleased();
				const CanvasPanFrameOutput& out = rig.log.back().out;
				worst70 = (std::max)(worst70, out.releaseSpeed);
				if (!released || out.phase != CanvasPanPhase::Idle) exactZero = false;
				if (rest >= 0.090 && (out.releaseSpeed != 0.0 || out.contentVelocity.x != 0.0f)) exactZero = false;
			}
		}
		check.Within(worst50, 0.0, kCanvasPanMinimumReleaseSpeedDipPerSecond - 1.0e-6,
			"50 ms rest releases below the 250 DIP/s inertia threshold");
		check.Within(worstGlide, 0.0, 10.0, "stop-and-place leaves no phantom glide");
		check(exactZero, "rest of 90 ms or more releases with exactly zero inertia and stays Idle");
		check.Within(worst70, 0.0, kCanvasPanMinimumReleaseSpeedDipPerSecond - 1.0e-6,
			"70 ms rest never starts inertia");
		Info("stop-hand release (rest 50ms) worst=" + Fmt(worst50) + " DIP/s, rest>=70ms worst=" +
			Fmt(worst70) + " DIP/s");
	}

	void TestTwoFingerStaggeredLift(Checks& check)
	{
		double low = 10.0;
		double high = 0.0;
		int runs = 0;
		for (const double rate : { 60.0, 120.0, 240.0 })
		{
			double rateHigh = 0.0;
			for (int stagger = 0; stagger <= 60; ++stagger)
			{
				const uint64_t seed = static_cast<uint64_t>(stagger) * 7 + static_cast<uint64_t>(rate);
				Rig rig(120.0, 0.0, 0.3, seed);
				Lcg rng(seed);
				Device device;
				device.rateHz = rate;
				Profile a = Steady(3000.0, 0.040, 0.250);
				Profile b = Steady(3000.0, 0.040, 0.250 + stagger / 1000.0);
				b.offsetX = 500.0;
				rig.Add(1, MakeEvents(a, device, rng, 0.0));
				rig.Add(2, MakeEvents(b, device, rng, 0.0));
				const bool released = rig.RunUntilReleased();
				const double ratio = rig.log.back().out.releaseSpeed / 3000.0;
				check(released && rig.log.back().out.phase == CanvasPanPhase::Coasting,
					"staggered two-finger lift releases into Coasting");
				low = (std::min)(low, ratio);
				high = (std::max)(high, ratio);
				rateHigh = (std::max)(rateHigh, ratio);
				++runs;
			}
			Info("two-finger staggered lift 0-60 ms @" + Fmt(rate, 0) + " Hz max ratio=" + Fmt(rateHigh, 3));
		}
		check.Within(low, 0.90, 1.15, "staggered lift lowest release ratio");
		check.Within(high, 0.90, 1.15, "staggered lift highest release ratio (legacy peak was 5.6x)");
		check(runs == 183, "staggered lift sweep ran");
	}

	void TestTopologyChangesKeepEstimators(Checks& check)
	{
		Rig rig;
		Profile a = Linear(2000.0, 0.5);
		Profile b = Linear(2000.0, 0.2);
		b.offsetX = 600.0;
		Lcg rng(9);
		rig.Add(1, MakeEvents(a, Exact(), rng, 0.0));
		rig.Add(2, MakeEvents(b, Exact(), rng, 0.15)); // 0.15 s 落下第二指，0.35 s 抬起
		rig.RunUntil(0.60);
		const double expected = 2000.0 / 120.0;
		double worstDelta = 0.0;
		size_t minSamples = 1000;
		size_t trackerObservations = 0;
		double worstHand = 0.0;
		bool locked = true;
		for (const Logged& frame : rig.log)
		{
			const double t = Seconds(frame.qpc);
			if (t >= 0.05 && t <= 0.49)
			{
				worstDelta = (std::max)(worstDelta, std::fabs(frame.out.contentDelta.x - expected));
				locked = locked && frame.out.phase == CanvasPanPhase::Locked;
			}
			if (t >= 0.16 && t <= 0.34 && frame.out.handVelocityKnown)
				worstHand = (std::max)(worstHand, std::fabs(frame.out.handVelocity.x / 2000.0 - 1.0));
		}
		check(locked, "two-finger gesture stays Locked through add/remove");
		check.Within(worstDelta, 0.0, 1.0, "adding/removing a finger never jumps the content");
		check.Within(worstHand, 0.0, 0.05, "new finger with <3 samples does not spike the hand velocity");
		// A 指的估速窗口从未被清空（窗口内约 12 个样本；重置会掉到 1-2 个）。
		// 逐帧复现：重新驱动并读取触点 1 的估速器样本数。
		Rig probe;
		Lcg rng2(9);
		probe.Add(1, MakeEvents(a, Exact(), rng2, 0.0));
		probe.Add(2, MakeEvents(b, Exact(), rng2, 0.15));
		while (probe.NextFrameSeconds() <= 0.49)
		{
			probe.Frame();
			const double t = probe.NextFrameSeconds();
			if (t < 0.2) continue;
			const CanvasPanVelocityTracker* tracker = probe.dynamics.FindTracker(1);
			if (tracker)
			{
				++trackerObservations;
				minSamples = (std::min)(minSamples, tracker->SampleCount());
			}
		}
		check(trackerObservations >= 20, "remaining finger tracker is observed across topology changes");
		check(minSamples >= 10, "remaining finger keeps its velocity window across a lift");
		double lastFrame = 0.0;
		for (const Logged& frame : rig.log)
			if (Seconds(frame.qpc) <= 0.495) lastFrame = Seconds(frame.qpc);
		const double total = SumDelta(rig, -1.0, 0.495);
		check.Within(total, 2000.0 * lastFrame - 1.0, 2000.0 * lastFrame + 1.0,
			"total pan equals the moving finger displacement");
		Info("topology: worst frame delta error=" + Fmt(worstDelta, 3) + " DIP, min tracker samples=" +
			std::to_string(minSamples));
	}

	// ============================== 摩擦耦合 ==============================
	void TestPressAndHoldDuringCoasting(Checks& check)
	{
		Lcg rng(3);
		struct Run
		{
			Rig rig;
			double press = 0.0;
		};
		const auto make = [&](bool finger)
		{
			Run run;
			const double release = PrepareCoasting(run.rig, 3000.0);
			run.press = release + 0.10;
			if (finger)
			{
				Profile hold;
				hold.position = [](double t) { return t < 0.5 ? 0.0 : (std::min)(50.0, 2000.0 * (t - 0.5)); };
				hold.lift = 1.2;
				hold.offsetX = 800.0;
				run.rig.Add(2, MakeEvents(hold, Exact(), rng, run.press));
			}
			run.rig.RunUntil(run.press + 1.6);
			return run;
		};
		Run held = make(true);
		Run control = make(false);
		const double lockAt = FirstTime(held.rig, held.press, [](const Logged& f)
			{ return f.out.phase == CanvasPanPhase::Locked; });
		const double idleAt = FirstTime(control.rig, control.press, [](const Logged& f)
			{ return f.out.phase == CanvasPanPhase::Idle; });
		const double holdTime = lockAt - held.press;
		const double freeTime = idleAt - control.press;
		check.Within(holdTime, 0.0, 0.30, "press-and-hold grabs the coasting canvas quickly");
		check(holdTime < 0.5 * freeTime, "press-and-hold stops much faster than free coasting");
		bool onlyDecelerates = true;
		for (const Logged& frame : held.rig.log)
		{
			const double t = Seconds(frame.qpc);
			if (t < held.press || t > lockAt) continue;
			const size_t index = static_cast<size_t>(&frame - held.rig.log.data());
			if (index > 0 && frame.out.contentVelocity.x > held.rig.log[index - 1].out.contentVelocity.x + 1.0e-3f)
				onlyDecelerates = false;
		}
		check(onlyDecelerates, "canvas only decelerates while the hand holds still");
		const double slideHeld = SumDelta(held.rig, held.press, lockAt + 1.0e-4);
		const double slideFree = SumDelta(control.rig, control.press, 10.0);
		check(slideHeld < 0.5 * slideFree, "holding shortens the slide distance");
		// 抓住后 1:1 跟手：0.5 s 起手指再移动 50 DIP。
		const double follow = SumDelta(held.rig, held.press + 0.45, held.press + 1.1);
		check.Within(follow, 49.0, 51.0, "after the grab the content follows the finger 1:1");
		check(held.rig.dynamics.Phase() == CanvasPanPhase::Idle, "slow lift after the grab leaves no inertia");
		Info("press-and-hold: grabbed in " + Fmt(holdTime * 1000.0, 0) + " ms vs free stop " +
			Fmt(freeTime * 1000.0, 0) + " ms; slide " + Fmt(slideHeld) + " vs " + Fmt(slideFree) + " DIP");
	}

	void TestSameDirectionAcceleration(Checks& check)
	{
		Lcg rng(4);
		Rig rig;
		const double release = PrepareCoasting(rig, 3000.0);
		const double press = release + 0.08;
		rig.RunUntil(press - 1.0e-3);
		const double vAtPress = rig.log.back().out.contentVelocity.x;
		Profile hand = Steady(5000.0, 0.040, 0.60);
		hand.offsetX = 800.0;
		rig.Add(2, MakeEvents(hand, Exact(), rng, press));
		rig.RunUntil(press + 0.55);
		const double lockAt = FirstTime(rig, press, [](const Logged& f) { return f.out.phase == CanvasPanPhase::Locked; });
		check.Within(lockAt - press, 0.0, 0.40, "same-direction drag grabs the canvas");
		const double worstStep = MaxVelocityStep(rig, press, press + 0.55);
		check.Within(worstStep, 0.0, 700.0, "content velocity stays continuous");
		double minV = 1.0e9;
		double maxV = 0.0;
		for (const Logged& frame : rig.log)
		{
			const double t = Seconds(frame.qpc);
			if (t < press || t > press + 0.55) continue;
			minV = (std::min)(minV, static_cast<double>(frame.out.contentVelocity.x));
			maxV = (std::max)(maxV, static_cast<double>(frame.out.contentVelocity.x));
		}
		check(minV >= 0.9 * vAtPress, "canvas never slows below its pre-press speed on the way up");
		// 手速估计在加速段结束的拐点处最多高估约 16%（LSQ2 平台型偏差），画布只会被带到手速而不是手速+旧惯性。
		check(maxV <= 5000.0 * 1.16, "canvas is carried to the hand speed, not beyond it");
		check.Within(VelocityXAt(rig, press + 0.5) / 5000.0, 0.95, 1.05, "canvas reaches the hand speed");
		// 抬起：释放速度是手速，不是“手速 + 旧惯性”。
		check(rig.RunUntilReleased(), "same-direction drag releases");
		const double releaseSpeed = rig.log.back().out.releaseSpeed;
		check.Within(releaseSpeed / 5000.0, 0.95, 1.05, "release speed equals the hand speed (no additive boost)");
		Info("same-direction: v_press=" + Fmt(vAtPress, 0) + " grabbed in " + Fmt((lockAt - press) * 1000.0, 0) +
			" ms, max |dv|/frame=" + Fmt(worstStep, 0) + " DIP/s, peak v/hand=" + Fmt(maxV / 5000.0, 3) +
			", release=" + Fmt(releaseSpeed, 0));
	}

	void TestReverseDirectionDecelerationFasterThanHold(Checks& check)
	{
		Lcg rng(6);
		const auto make = [&](bool reverse)
		{
			Rig rig;
			const double release = PrepareCoasting(rig, 3000.0);
			const double press = release + 0.08;
			Profile hand = reverse ? Steady(3000.0, 0.040, 0.60) : Hold(0.60);
			if (reverse) hand.dirX = -1.0;
			hand.offsetX = 800.0;
			rig.Add(2, MakeEvents(hand, Exact(), rng, press));
			rig.RunUntil(press + 0.55);
			return std::make_pair(std::move(rig), press);
		};
		auto reverse = make(true);
		auto hold = make(false);
		const auto below = [](const Logged& f) { return f.out.contentVelocity.x <= 600.0f; };
		const double reverseTime = FirstTime(reverse.first, reverse.second, below) - reverse.second;
		const double holdTime = FirstTime(hold.first, hold.second, below) - hold.second;
		check(reverseTime < holdTime, "reverse drag decelerates the canvas faster than holding still");
		const double lockAt = FirstTime(reverse.first, reverse.second,
			[](const Logged& f) { return f.out.phase == CanvasPanPhase::Locked; });
		check.Within(lockAt - reverse.second, 0.0, 0.40, "reverse drag grabs the canvas");
		check.Within(VelocityXAt(reverse.first, reverse.second + 0.5) / -3000.0, 0.92, 1.08,
			"canvas ends up moving with the reversed hand");
		// 反向时相对速度最大约 6000 DIP/s：手摩擦单帧最大速度变化约 883 DIP/s。
		const double worstStep = MaxVelocityStep(reverse.first, reverse.second, reverse.second + 0.55);
		check.Within(worstStep, 0.0, 1000.0, "reverse transition keeps the velocity continuous");
		Info("reverse: v<=600 after " + Fmt(reverseTime * 1000.0, 0) + " ms vs hold " + Fmt(holdTime * 1000.0, 0) +
			" ms, max |dv|/frame=" + Fmt(worstStep, 0));
	}

	// 落指时手指轻微滚动(>4 DIP 但样本不足以估速)随后静止：仍应被视为静止并抓住画布。
	void TestHoldAfterTinyWiggle(Checks& check)
	{
		Lcg rng(14);
		Rig rig;
		const double release = PrepareCoasting(rig, 3000.0);
		const double press = release + 0.10;
		Profile wiggle;
		wiggle.position = [](double t) { return t < 0.004 ? 0.0 : (t < 0.012 ? 3.0 : 6.0); };
		wiggle.lift = 1.0;
		wiggle.offsetX = 800.0;
		rig.Add(2, MakeEvents(wiggle, Exact(), rng, press));
		rig.RunUntil(press + 0.8);
		const double lockAt = FirstTime(rig, press, [](const Logged& f) { return f.out.phase == CanvasPanPhase::Locked; });
		check.Within(lockAt - press, 0.0, 0.40, "a wiggling then silent finger still grabs the canvas");
		Info("wiggle-then-hold grabbed in " + Fmt((lockAt - press) * 1000.0, 0) + " ms");
	}

	void TestLiftWhileSlipping(Checks& check)
	{
		Lcg rng(8);
		Rig rig;
		const double release = PrepareCoasting(rig, 3000.0);
		const double press = release + 0.10;
		Profile tap = Hold(0.060);
		tap.offsetX = 800.0;
		rig.Add(2, MakeEvents(tap, Exact(), rng, press));
		rig.RunUntil(press + 2.0);
		size_t index = 0;
		for (size_t i = 0; i < rig.log.size(); ++i)
			if (Seconds(rig.log[i].qpc) > press && rig.log[i].out.released) { index = i; break; }
		check(index > 1, "lifting after a short hold produces a release event");
		const double before = rig.log[index - 1].out.contentVelocity.x;
		const double after = rig.log[index].out.contentVelocity.x;
		check.Within(std::fabs(after - before), 0.0, 400.0, "lifting while slipping keeps the canvas velocity");
		check.Within(after, 400.0, 2400.0, "velocity after the short hold is a decayed but real glide");
		check(rig.log[index].out.phase == CanvasPanPhase::Coasting, "lift while slipping continues as Coasting");
		const double rest = SumDelta(rig, Seconds(rig.log[index].qpc) + 1.0e-4, 20.0);
		check.Within(std::fabs(rest - CanvasPanGlideDistance(after, kCanvasPanFreeFriction)), 0.0, 2.0,
			"remaining glide follows the free-friction closed form");
		check(rig.dynamics.Phase() == CanvasPanPhase::Idle, "glide eventually rests");
	}

	void TestTapLeavesStateAlone(Checks& check)
	{
		Lcg rng(10);
		const auto make = [&](double tapLift)
		{
			Rig rig;
			const double release = PrepareCoasting(rig, 3000.0);
			const double press = release + 0.10;
			if (tapLift > 0.0)
			{
				Profile tap = Hold(tapLift);
				tap.offsetX = 800.0;
				rig.Add(2, MakeEvents(tap, Exact(), rng, press));
			}
			rig.RunUntil(press + 0.30);
			return std::make_pair(std::move(rig), press);
		};
		auto tapped = make(0.020);
		auto control = make(0.0);
		check(tapped.first.log.size() == control.first.log.size(), "tap and control ran the same frames");
		const CanvasVector vt = tapped.first.log.back().out.contentVelocity;
		const CanvasVector vc = control.first.log.back().out.contentVelocity;
		check.Within(std::fabs(vt.x - vc.x), 0.0, 0.5, "20 ms tap leaves the coasting velocity alone");
		check.Within(std::fabs(SumDelta(tapped.first, 0.0, 10.0) - SumDelta(control.first, 0.0, 10.0)), 0.0, 0.5,
			"20 ms tap leaves the coasting distance alone");
		check(tapped.first.log.back().out.phase == control.first.log.back().out.phase, "tap keeps the phase");

		// 惯性已停后的轻点不复活旧速度。
		Rig rig;
		const double release = PrepareCoasting(rig, 3000.0);
		rig.RunUntil(release + 2.0);
		check(rig.dynamics.Phase() == CanvasPanPhase::Idle, "coasting finished before the tap");
		Profile tap = Hold(0.020);
		tap.offsetX = 800.0;
		rig.Add(2, MakeEvents(tap, Exact(), rng, release + 2.0));
		const size_t first = rig.log.size();
		rig.RunUntil(release + 3.0);
		double motion = 0.0;
		bool idle = true;
		for (size_t i = first; i < rig.log.size(); ++i)
		{
			motion += std::fabs(rig.log[i].out.contentDelta.x) + std::fabs(rig.log[i].out.contentVelocity.x);
			idle = idle && rig.log[i].out.phase != CanvasPanPhase::Coasting && rig.log[i].out.phase != CanvasPanPhase::Slipping;
		}
		check(motion == 0.0 && idle, "a tap without Move never revives an old velocity");
	}

	// UI hitch 保留 producer 的真时间戳；输入断流则允许清窗，只验证跟手与释放安全。
	void TestQueuedHitchAndInputStall(Checks& check)
	{
		for (int fps : { 30, 60, 120, 240 }) for (int reports : { 1, 4 })
			for (double gap : { 100.0, 250.0 })
			{
				const double frameMs = 1000.0 / fps;
				CanvasPanDynamics queued;
				StepAt(queued, 0.0, { Sample(1, 0.0, 0.0, 0.0, CanvasPanContactPhase::Down) });
				double now = 0.0;
				const int frames = static_cast<int>(std::ceil(400.0 / frameMs));
				for (int frame = 0; frame < frames; ++frame)
				{
					const double begin = now, dt = frame == frames / 2 ? gap : frameMs;
					const int count = static_cast<int>(std::ceil(dt / frameMs * reports));
					std::vector<CanvasPanContactSample> batch;
					for (int i = 1; i <= count; ++i)
					{
						const double at = begin + (std::min)(dt, frameMs * i / reports);
						batch.push_back(Sample(1, at, 6.0 * at, 0.0));
					}
					now += dt;
					const auto out = StepAt(queued, now, batch);
					check.Within(std::fabs(out.contentDelta.x - 6.0 * dt), 0.0, 0.001,
						"queued hitch content delta equals true hand displacement");
					if (begin >= 120.0)
						check(out.handVelocityKnown && std::fabs(out.contentVelocity.x - 6000.0) < 0.05,
							"queued hitch and following frames preserve constant Locked velocity");
				}
				const auto release = StepAt(queued, now + frameMs, {
					Sample(1, now + 0.1, 6.0 * now, 0.0, CanvasPanContactPhase::Up) });
				check.Within(release.releaseSpeed, 5999.95, 6000.05, "queued hitch releases at valid hand speed");
				double glide = 0.0;
				for (int i = 0; queued.Active() && i < fps * 2; ++i)
					glide += StepAt(queued, now + frameMs * (i + 2), {}).contentDelta.x;
				check(!queued.Active() && std::fabs(glide - CanvasPanGlideDistance(release.releaseSpeed,
					kCanvasPanFreeFriction)) < 0.5, "queued hitch glide agrees with closed form");

				for (bool recover : { false, true })
				{
					CanvasPanDynamics stalled;
					PrepareHandoffGesture(stalled, 6000.0);
					double at = 96.0 + gap;
					for (double frameAt = 96.0 + frameMs; frameAt < at - 1.0e-6; frameAt += frameMs)
						StepAt(stalled, frameAt, {});
					const auto out = StepAt(stalled, at, { Sample(1, at, 6.0 * at, 0.0) });
					const auto* tracker = stalled.FindTracker(1);
					check(tracker && tracker->SampleCount() == 1, "producer stall clears the pre-gap tracker window");
					check(!out.handVelocityKnown && out.contentVelocity.x == 0.0f &&
						std::isfinite(out.contentDelta.x) && std::fabs(out.contentDelta.x - 6.0 * gap) < 0.001,
						"input stall invalidates velocity without adding content displacement");
					if (recover)
						for (int i = 0; i < static_cast<int>(std::ceil(120.0 / frameMs)); ++i)
						{
							at += frameMs;
							StepAt(stalled, at, { Sample(1, at, 6.0 * at, 0.0) });
						}
					const auto lifted = StepAt(stalled, at + frameMs, {
						Sample(1, at + 0.1, 6.0 * at, 0.0, CanvasPanContactPhase::Up) });
					check(recover ? std::fabs(lifted.releaseSpeed - 6000.0) < 0.05 :
						(lifted.releaseSpeed == 0.0 && !stalled.Active()),
						"stall lift uses only a valid recovered estimate, otherwise zero");
				}
			}
	}

	void TestTopologyPauseRestartFit(Checks& check)
	{
		CanvasPanVelocityTracker tracker;
		const std::array<double, 6> times{ 0.0, 100.0 / 3.0, 50.0, 200.0 / 3.0, 250.0 / 3.0, 100.0 };
		for (size_t i = 0; i < times.size(); ++i)
		{
			const double x = 25.0 * i;
			tracker.AddSample(Qpc(times[i] / 1000.0), x, x * 0.2, kFreq, 1.0 / 60.0);
			Info("topology60 window t_ms=" + Fmt(times[i] - 100.0, 6) + " x=" + Fmt(x) + " y=" + Fmt(x * 0.2));
		}
		const auto fit = tracker.Estimate(kFreq);
		// 该流先空跑一帧再走五帧；LSQ2 是 a=127.083333,b=30.357143,c=1.488095（时间单位为帧）。
		// feel-bound: 0.02 DIP/s 只容纳 10MHz QPC 取整；不能拿匀速 1.15 界限评价含暂停的窗口。
		check(fit.valid, "pause/restart stream has a valid quadratic fit");
		check.Within(fit.x, 1821.428571 - 0.02, 1821.428571 + 0.02, "60Hz pause/restart analytic x derivative");
		check.Within(fit.y, 364.285714 - 0.02, 364.285714 + 0.02, "60Hz pause/restart analytic y derivative");
		Info("topology60 lift hand=(1500,300) fit=(" + Fmt(fit.x, 6) + "," + Fmt(fit.y, 6) +
			") ratio=" + Fmt(std::hypot(fit.x, fit.y) / std::hypot(1500.0, 300.0), 9) + " expected=17/14");
		for (int i = 1; i <= 8; ++i)
		{
			const double at = 100.0 + 1000.0 * i / 60.0, x = 125.0 + 25.0 * i;
			tracker.AddSample(Qpc(at / 1000.0), x, x * 0.2, kFreq, 1.0 / 60.0);
		}
		const auto steady = tracker.Estimate(kFreq);
		check(steady.valid && std::fabs(steady.x - 1500.0) < 0.02 && std::fabs(steady.y - 300.0) < 0.02,
			"clean constant-speed window recovers the true topology release speed");
	}

	void TestTapUsesLiftTimestamp(Checks& check)
	{
		for (int fps : { 30, 60, 120, 240 }) for (double offset : { 0.1, 1000.0 / fps * 0.5 })
		{
			CanvasPanDynamics tapped, control;
			PrepareHandoffGesture(tapped); PrepareHandoffGesture(control);
			const auto oldUp = Sample(1, 100.0, 288.0, 0.0, CanvasPanContactPhase::Up);
			StepAt(tapped, 100.0, { oldUp }); StepAt(control, 100.0, { oldUp });
			const double down = 100.0 + offset, up = down + 20.0, frameMs = 1000.0 / fps;
			bool sentDown = false, sentUp = false;
			for (int frame = 1; frame <= static_cast<int>(std::ceil((offset + 20.0) / frameMs)) + 1; ++frame)
			{
				const double now = 100.0 + frame * frameMs;
				std::vector<CanvasPanContactSample> batch;
				if (!sentDown && now >= down) { batch.push_back(Sample(2, down, 800.0, 0.0, CanvasPanContactPhase::Down)); sentDown = true; }
				if (!sentUp && now >= up) { batch.push_back(Sample(2, up, 800.0, 0.0, CanvasPanContactPhase::Up)); sentUp = true; }
				const auto actual = StepAt(tapped, now, batch), expected = StepAt(control, now, {});
				// feel-bound: 与探针相同的 0.05 DIP/s，位移差 0.001 DIP 仅容纳 float 积分输出。
				check(std::fabs(actual.contentVelocity.x - expected.contentVelocity.x) < 0.05 &&
					std::fabs(actual.contentDelta.x - expected.contentDelta.x) < 0.001,
					"fixed 20ms tap matches free coast at every fps and delivery phase");
			}
			check(sentUp && tapped.Phase() == CanvasPanPhase::Coasting, "short tap releases back to free coast");
		}
		CanvasPanDynamics delayed, control;
		PrepareHandoffGesture(delayed); PrepareHandoffGesture(control);
		const auto oldUp = Sample(1, 100.0, 288.0, 0.0, CanvasPanContactPhase::Up);
		StepAt(delayed, 100.0, { oldUp }); StepAt(control, 100.0, { oldUp });
		const auto out = StepAt(delayed, 160.0, {
			Sample(2, 101.0, 800.0, 0.0, CanvasPanContactPhase::Down),
			Sample(2, 121.0, 800.0, 0.0, CanvasPanContactPhase::Up) });
		const auto expected = StepAt(control, 160.0, {});
		check(!out.handVelocityKnown && out.contentVelocity.x == expected.contentVelocity.x &&
			out.contentDelta.x == expected.contentDelta.x, "queued 20ms tap does not gain stationary evidence while waiting");
	}

	void TestRepeatedFlicksSaturate(Checks& check)
	{
		Lcg rng(12);
		Rig rig;
		double cursor = 0.0;
		std::vector<double> releases;
		uint64_t key = 1;
		for (const double speed : { 3000.0, 6000.0, 9000.0, 14000.0, 20000.0 })
		{
			Profile flick = Steady(speed);
			flick.offsetX = 100.0 * static_cast<double>(key);
			rig.Add(key++, MakeEvents(flick, Exact(), rng, cursor));
			if (!rig.RunUntilReleased()) break;
			releases.push_back(rig.log.back().out.releaseSpeed);
			cursor = Seconds(rig.log.back().qpc) + 0.15; // 下一次在惯性中落下
		}
		check(releases.size() == 5, "five consecutive flicks release");
		if (releases.size() == 5)
		{
			check.Within(releases[0] / 3000.0, 0.95, 1.05, "flick 1 release");
			check.Within(releases[1] / 6000.0, 0.95, 1.05, "flick 2 grows with the hand speed");
			check.Within(releases[2] / 9000.0, 0.95, 1.05, "flick 3 grows with the hand speed");
			check.Within(releases[3], 11900.0, kCanvasPanVelocityLimitDipPerSecond + 1.0e-6, "flick 4 saturates at the limit");
			check.Within(releases[4], 11900.0, kCanvasPanVelocityLimitDipPerSecond + 1.0e-6, "flick 5 saturates at the limit");
			check(releases[0] < releases[1] && releases[1] < releases[2] && releases[2] < releases[3],
				"consecutive faster flicks grow the speed");
		}
		double peak = 0.0;
		for (const Logged& frame : rig.log)
			peak = (std::max)(peak, std::hypot(static_cast<double>(frame.out.contentVelocity.x),
				static_cast<double>(frame.out.contentVelocity.y)));
		check(peak <= kCanvasPanVelocityLimitDipPerSecond + 1.0e-3, "content speed never exceeds the limit");

		// 同速度重复甩动不会累加。
		Rig same;
		double at = 0.0;
		double worst = 0.0;
		for (uint64_t index = 1; index <= 5; ++index)
		{
			Profile flick = Steady(4000.0);
			flick.offsetX = 100.0 * static_cast<double>(index);
			same.Add(index, MakeEvents(flick, Exact(), rng, at));
			same.RunUntilReleased();
			worst = (std::max)(worst, same.log.back().out.releaseSpeed / 4000.0);
			at = Seconds(same.log.back().qpc) + 0.15;
		}
		check.Within(worst, 0.95, 1.06, "same-speed repeated flicks do not accumulate");
		std::string text;
		for (double r : releases) text += Fmt(r, 0) + " ";
		Info("repeated flick releases (3000,6000,9000,14000,20000 hand): " + text + "| same-speed 4000 worst ratio=" + Fmt(worst, 3));
	}

	void TestTwoSwipeSequences(Checks& check)
	{
		for (const int gapMs : { 80, 150, 300, 500 })
		{
			double totals[2] = {};
			double release2 = 0.0;
			double grabSpeed = 0.0;
			double maxStep = 0.0;
			double lockSeconds = 0.0;
			for (int variant = 0; variant < 2; ++variant) // 0: 摩擦耦合；1: 落指即停（旧行为基线，仅作对照）
			{
				Lcg rng(21);
				Rig rig;
				rig.Add(1, MakeEvents(Steady(4000.0), Exact(), rng, 0.0));
				rig.RunUntilReleased();
				const double press = Seconds(rig.log.back().qpc) + gapMs / 1000.0;
				Profile second = Steady(4000.0);
				second.offsetX = 900.0;
				rig.RunUntil(press - 1.0e-3);
				if (variant == 1)
				{
					grabSpeed = rig.log.back().out.contentVelocity.x;
					rig.dynamics.Stop();
				}
				rig.Add(2, MakeEvents(second, Exact(), rng, press));
				check(rig.RunUntilReleased(), "second swipe releases");
				if (variant == 0)
				{
					release2 = rig.log.back().out.releaseSpeed;
					maxStep = MaxVelocityStep(rig, press, press + 0.5);
					lockSeconds = FirstTime(rig, press, [](const Logged& f)
						{ return f.out.phase == CanvasPanPhase::Locked; }) - press;
				}
				rig.RunUntil(Seconds(rig.log.back().qpc) + 3.0);
				totals[variant] = SumDelta(rig, -1.0, 1.0e9);
			}
			check.Within(release2 / 4000.0, 0.95, 1.06, "second swipe releases at its own hand speed (no boost)");
			check.Within(maxStep, 0.0, 700.0, "two-swipe velocity trace has no jump");
			check.Within(lockSeconds, 0.0, 0.40, "second swipe grabs the canvas");
			Info("two swipes gap " + std::to_string(gapMs) + " ms: residual at grab=" + Fmt(grabSpeed, 0) +
				" DIP/s, grabbed in " + Fmt(lockSeconds * 1000.0, 0) + " ms, release2=" + Fmt(release2, 0) +
				", max |dv|/frame=" + Fmt(maxStep, 0) + ", total coupled=" + Fmt(totals[0], 0) +
				" vs stop-at-grab=" + Fmt(totals[1], 0) + " DIP");
		}
	}

	// ============================== 中断与异常输入 ==============================
	void TestPenAndStop(Checks& check)
	{
		const auto coast = [](bool hover)
		{
			Rig rig;
			const double release = PrepareCoasting(rig, 3000.0);
			rig.penHover = hover;
			rig.RunUntil(release + 4.0);
			return std::make_pair(SumDelta(rig, release + 1.0e-4, 100.0), rig.dynamics.Phase());
		};
		const auto free = coast(false);
		const auto hover = coast(true);
		check.Within(free.first, CanvasPanGlideDistance(3000.0, kCanvasPanFreeFriction) - 1.0,
			CanvasPanGlideDistance(3000.0, kCanvasPanFreeFriction) + 1.0, "free glide follows the closed form");
		check(hover.first < 0.4 * free.first, "pen hover brakes the glide faster than free friction");
		check(hover.second == CanvasPanPhase::Idle, "pen hover glide still ends in Idle");

		// 落笔：同帧急停，不再产生位移。
		Rig press;
		const double release = PrepareCoasting(press, 3000.0);
		press.RunUntil(release + 0.05);
		press.penContact = true;
		const CanvasPanFrameOutput out = press.Frame();
		check(out.phase == CanvasPanPhase::Idle && out.contentDelta.x == 0.0f && out.contentVelocity.x == 0.0f &&
			!press.dynamics.Active(), "pen contact stops the glide in the same frame");
		press.penContact = false;
		const size_t stoppedAt = press.log.size();
		press.RunUntil(release + 1.0);
		double residual = 0.0;
		for (size_t i = stoppedAt; i < press.log.size(); ++i)
			residual += std::fabs(press.log[i].out.contentDelta.x) + std::fabs(press.log[i].out.contentVelocity.x);
		check.Within(residual, 0.0, 0.0, "no residual displacement after the pen lands");

		// 手势中落笔/Stop：Locked 与 Slipping 同样清零。
		CanvasPanDynamics locked;
		StepAt(locked, 0.0, { Sample(1, 0.0, 100.0, 100.0, CanvasPanContactPhase::Down) });
		for (int i = 1; i <= 6; ++i)
			StepAt(locked, i * 8.0, { Sample(1, i * 8.0, 100.0 + 24.0 * i, 100.0) });
		check(locked.Phase() == CanvasPanPhase::Locked, "setup: finger drag locks");
		const auto stopped = StepAt(locked, 60.0, {}, false, true);
		check(stopped.phase == CanvasPanPhase::Idle && stopped.heldContacts == 0 &&
			locked.HeldContactCount() == 0, "pen contact clears a live gesture");
		locked.Stop();
		check(locked.Phase() == CanvasPanPhase::Idle && locked.ContentVelocity().x == 0.0f, "Stop is idempotent");
	}

	void TestCancelAndBackwardsRelease(Checks& check)
	{
		// Cancelled：无惯性。
		CanvasPanDynamics cancelled;
		StepAt(cancelled, 0.0, { Sample(1, 0.0, 0.0, 0.0, CanvasPanContactPhase::Down) });
		for (int i = 1; i <= 8; ++i)
			StepAt(cancelled, i * 8.0, { Sample(1, i * 8.0, 24.0 * i, 0.0) });
		const auto c = StepAt(cancelled, 68.0, { Sample(1, 66.0, 24.0 * 8, 0.0, CanvasPanContactPhase::Cancelled) });
		check(c.phase == CanvasPanPhase::Idle && c.releaseSpeed == 0.0 && c.contentVelocity.x == 0.0f,
			"Cancelled gesture never starts inertia");

		// Up 时间早于最后一个样本（时钟倒退）：无惯性。
		CanvasPanDynamics backwards;
		StepAt(backwards, 0.0, { Sample(1, 0.0, 0.0, 0.0, CanvasPanContactPhase::Down) });
		for (int i = 1; i <= 8; ++i)
			StepAt(backwards, i * 8.0, { Sample(1, i * 8.0, 24.0 * i, 0.0) });
		const auto b = StepAt(backwards, 70.0, { Sample(1, 30.0, 24.0 * 8, 0.0, CanvasPanContactPhase::Up) });
		check(b.phase == CanvasPanPhase::Idle && b.releaseSpeed == 0.0, "an Up earlier than the last sample gives no inertia");

		// 对照：正常 Up 起滑。
		CanvasPanDynamics normal;
		StepAt(normal, 0.0, { Sample(1, 0.0, 0.0, 0.0, CanvasPanContactPhase::Down) });
		for (int i = 1; i <= 8; ++i)
			StepAt(normal, i * 8.0, { Sample(1, i * 8.0, 24.0 * i, 0.0) });
		const auto n = StepAt(normal, 70.0, { Sample(1, 66.0, 24.0 * 8, 0.0, CanvasPanContactPhase::Up) });
		check(n.phase == CanvasPanPhase::Coasting && n.releaseSpeed > 2500.0, "a normal Up starts inertia");

		// 取消一只手但另一只仍按下：不终止手势。
		CanvasPanDynamics two;
		StepAt(two, 0.0, { Sample(1, 0.0, 0.0, 0.0, CanvasPanContactPhase::Down), Sample(2, 0.0, 300.0, 0.0, CanvasPanContactPhase::Down) });
		for (int i = 1; i <= 6; ++i)
			StepAt(two, i * 8.0, { Sample(1, i * 8.0, 24.0 * i, 0.0), Sample(2, i * 8.0, 300.0 + 24.0 * i, 0.0) });
		const auto t = StepAt(two, 56.0, { Sample(2, 50.0, 300.0 + 24.0 * 6, 0.0, CanvasPanContactPhase::Cancelled) });
		check(t.phase == CanvasPanPhase::Locked && t.heldContacts == 1, "cancelling one finger keeps the other pan alive");
	}

	void TestInvalidInputsLeaveStateAlone(Checks& check)
	{
		CanvasPanDynamics dyn;
		StepAt(dyn, 0.0, { Sample(1, 0.0, 0.0, 0.0, CanvasPanContactPhase::Down) });
		for (int i = 1; i <= 6; ++i)
			StepAt(dyn, i * 8.0, { Sample(1, i * 8.0, 24.0 * i, 0.0) });
		const CanvasPanPhase phase = dyn.Phase();
		const CanvasVector velocity = dyn.ContentVelocity();
		const CanvasPanVelocityTracker* tracker = dyn.FindTracker(1);
		check(tracker != nullptr && phase == CanvasPanPhase::Locked, "setup: locked drag");
		const size_t samples = tracker->SampleCount();
		const int64_t lastQpc = tracker->LastSampleQpc();
		const double nan = std::numeric_limits<double>::quiet_NaN();

		const auto unchanged = [&](const CanvasPanFrameOutput& out, const char* name)
		{
			const CanvasPanVelocityTracker* now = dyn.FindTracker(1);
			check(out.contentDelta.x == 0.0f && out.contentDelta.y == 0.0f && dyn.Phase() == phase &&
				dyn.ContentVelocity().x == velocity.x && now != nullptr &&
				now->SampleCount() == samples && now->LastSampleQpc() == lastQpc, name);
		};
		unchanged(StepAt(dyn, 49.0, { Sample(1, 49.0, nan, 0.0) }), "NaN position leaves state alone");
		unchanged(StepAt(dyn, 50.0, { Sample(1, 48.0, 500.0, 0.0) }), "duplicate sample QPC leaves state alone");
		unchanged(StepAt(dyn, 51.0, { Sample(1, 30.0, 700.0, 0.0) }), "backwards sample QPC leaves state alone");
		unchanged(StepAt(dyn, 30.0, { Sample(1, 60.0, 800.0, 0.0) }), "backwards frame clock is ignored");
		CanvasPanFrameInput zero;
		zero.contacts = std::vector<CanvasPanContactSample>{ Sample(1, 100.0, 900.0, 0.0) };
		unchanged(dyn.Step(zero), "zero frame QPC and frequency are ignored");
		const auto resume = StepAt(dyn, 56.0, { Sample(1, 56.0, 24.0 * 7, 0.0) });
		check(resume.contentDelta.x > 0.0f && dyn.Phase() == CanvasPanPhase::Locked, "valid input resumes after rejects");

		// 触点容量上限不会崩溃。
		CanvasPanDynamics crowd;
		std::vector<CanvasPanContactSample> downs;
		for (uint64_t key = 1; key <= 24; ++key)
			downs.push_back(Sample(key, 0.0, 10.0 * static_cast<double>(key), 0.0, CanvasPanContactPhase::Down));
		const auto full = StepAt(crowd, 0.0, downs);
		check(full.heldContacts == kCanvasPanMaximumContacts && full.phase == CanvasPanPhase::Locked,
			"more than the contact capacity is ignored without effect");
	}

	void TestClearAxis(Checks& check)
	{
		Rig rig;
		Lcg rng(2);
		Profile flick = Steady(4000.0);
		flick.dirX = 0.6;
		flick.dirY = 0.8;
		rig.Add(1, MakeEvents(flick, Exact(), rng, 0.0));
		rig.RunUntilReleased();
		const CanvasVector before = rig.dynamics.ContentVelocity();
		rig.dynamics.ClearAxis(true, false);
		check(rig.dynamics.ContentVelocity().x == 0.0f && rig.dynamics.ContentVelocity().y == before.y &&
			rig.dynamics.Phase() == CanvasPanPhase::Coasting, "clearing x keeps the y glide");
		rig.dynamics.ClearAxis(false, true);
		check(rig.dynamics.Phase() == CanvasPanPhase::Idle && !rig.dynamics.Active(),
			"clearing both axes ends the glide");
	}

	// ---- 白板背景纹理的 CPU 几何（GPU 像素对比见 Inkeys.exe --draw3-whiteboard-pattern-test）----
	using PatternPrimitive = std::array<float, 5>; // x0, y0, x1, y1, halfWidth

	std::vector<PatternPrimitive> EnumeratePattern(const WhiteboardPatternParams& params,
		int left, int top, int right, int bottom)
	{
		std::vector<PatternPrimitive> primitives;
		EnumerateWhiteboardPatternPrimitives(params, left, top, right, bottom,
			[&](float x0, float y0, float x1, float y1, float half) {
				primitives.push_back({ x0, y0, x1, y1, half });
			});
		return primitives;
	}

	void TestWhiteboardPatternGeometry(Checks& check)
	{
		const WhiteboardTheme light{ 0xF5F1E6, WhiteboardPattern::Dots };
		const WhiteboardTheme dark{ 0x123B32, WhiteboardPattern::Grid };
		const WhiteboardTheme blank{ 0x123B32, WhiteboardPattern::Blank };
		const WhiteboardTheme invalid{ 0x123B32, static_cast<WhiteboardPattern>(9) };
		// 门控：桌面/PPT、Blank、非法主题、非法 DPI 一律不产生纹理（画布空时必须真透明）。
		check(!ResolveWhiteboardPatternForFrame(false, light, 0, 0, 1920, 1080, 1.0f).Enabled(), "pattern gate: desktop/presentation");
		check(!ResolveWhiteboardPatternForFrame(true, blank, 0, 0, 1920, 1080, 1.0f).Enabled(), "pattern gate: blank");
		check(!ResolveWhiteboardPatternForFrame(true, invalid, 0, 0, 1920, 1080, 1.0f).Enabled(), "pattern gate: invalid theme");
		check(!ResolveWhiteboardPatternForFrame(true, light, 0, 0, 0, 1080, 1.0f).Enabled(), "pattern gate: empty canvas");
		check(!ResolveWhiteboardPatternForFrame(true, light, 0, 0, 1920, 1080, std::numeric_limits<float>::quiet_NaN()).Enabled(), "pattern gate: NaN DPI");
		check(!ResolveWhiteboardPatternForFrame(true, light, 0, 0, 1920, 1080, 0.1f).Enabled(), "pattern gate: absurdly small DPI scale");
		check(ResolveWhiteboardPatternForFrame(true, light, 0, 0, 1920, 1080, 1.0f).Enabled(), "pattern gate: whiteboard dots");
		check(EnumeratePattern(ResolveWhiteboardPatternForFrame(false, light, 0, 0, 1920, 1080, 1.0f), 0, 0, 1920, 1080).empty(),
			"a disabled pattern emits no primitive");

		// 颜色/尺寸：浅色背景黑纹理、深色背景白纹理；24 DIP 间距，点半径 1 DIP，线宽 1 DIP。
		const auto lightDots = ResolveWhiteboardPatternForFrame(true, light, 0, 0, 1920, 1080, 1.5f);
		const auto darkGrid = ResolveWhiteboardPatternForFrame(true, dark, 0, 0, 1920, 1080, 1.5f);
		check(lightDots.shade == 0.0f && darkGrid.shade == 1.0f, "pattern shade follows the background luminance");
		check(lightDots.stepPx == 36.0 && lightDots.dotRadiusPx == 1.5f && darkGrid.lineHalfWidthPx == 0.75f,
			"pattern step/radius/line width scale with DPI");
		check(kWhiteboardPatternAlpha == 0.18f, "pattern alpha is 18%");

		// 相位公式：phase = (W/2 - viewport) mod step，用 double 避免 ±1048576 的 float 精度问题。
		check(WhiteboardPatternPhase(960.0, 0.0, 24.0) == 0.0, "phase at viewport 0 starts at the screen centre lattice");
		check(WhiteboardPatternPhase(960.0, 13.25, 24.0) == 10.75, "phase follows a fractional viewport");
		check(WhiteboardPatternPhase(960.0, -13.25, 24.0) == 13.25, "phase follows a negative viewport");
		for (const double viewport : { 1048576.0, -1048576.0, 1048575.875, -3.5, 24.0, 0.0 })
		{
			const double phase = WhiteboardPatternPhase(540.0, viewport, 36.0);
			check(phase >= 0.0 && phase < 36.0, "phase stays in [0, step)");
			check(phase == WhiteboardPatternPhase(540.0, viewport - 36.0 * 1000.0, 36.0), "phase is periodic in the step");
		}
		check(WhiteboardPatternPhase(960.0, std::numeric_limits<double>::infinity(), 24.0) == 0.0, "non-finite viewport falls back to phase 0");

		// 4K 图元数（设计 §4：约 14.6k/6.5k/3.7k 点，约 250/167/125 条网格线）。
		const auto count = [&](WhiteboardPattern kind, float dpi) {
			return EnumeratePattern(ResolveWhiteboardPatternForFrame(true, WhiteboardTheme{ 0x123B32, kind },
				0, 0, 3840, 2160, dpi), 0, 0, 3840, 2160).size();
		};
		check(count(WhiteboardPattern::Dots, 1.0f) >= 14000 && count(WhiteboardPattern::Dots, 1.0f) <= 15500, "4K 100% dot primitive count");
		check(count(WhiteboardPattern::Dots, 1.5f) >= 6200 && count(WhiteboardPattern::Dots, 1.5f) <= 7200, "4K 150% dot primitive count");
		check(count(WhiteboardPattern::Dots, 2.0f) >= 3500 && count(WhiteboardPattern::Dots, 2.0f) <= 4200, "4K 200% dot primitive count");
		check(count(WhiteboardPattern::Grid, 1.0f) >= 248 && count(WhiteboardPattern::Grid, 1.0f) <= 262, "4K 100% grid line count");
		check(count(WhiteboardPattern::Grid, 2.0f) >= 120 && count(WhiteboardPattern::Grid, 2.0f) <= 134, "4K 200% grid line count");

		// 几何形状：点是零长度线；网格线轴对齐且向两端越过矩形。
		for (const auto& primitive : EnumeratePattern(lightDots, 0, 0, 200, 150))
			check(primitive[0] == primitive[2] && primitive[1] == primitive[3] && primitive[4] == 1.5f, "dot is a zero-length line with radius 1 DIP");
		bool gridShapeOk = true;
		for (const auto& primitive : EnumeratePattern(darkGrid, 100, 50, 300, 250))
		{
			const bool vertical = primitive[0] == primitive[2];
			const bool horizontal = primitive[1] == primitive[3];
			gridShapeOk = gridShapeOk && (vertical != horizontal) && primitive[4] == 0.75f &&
				(vertical ? primitive[1] < 0.0f && primitive[3] > 1080.0f : primitive[0] < 0.0f && primitive[2] > 1920.0f);
		}
		check(gridShapeOk, "grid lines are axis-aligned and span the whole canvas with cap/AA margin");

		// 分块一致：每个分块发出的图元逐位等于整帧同一序号的图元（网格线按整幅画布长度发出，与 dirty 无关），
		// 且整帧中与分块相交的图元不会遗漏。
		for (const WhiteboardPattern kind : { WhiteboardPattern::Dots, WhiteboardPattern::Grid })
		{
			const auto params = ResolveWhiteboardPatternForFrame(true, WhiteboardTheme{ 0xF5F1E6, kind },
				-137.25, 61.5, 640, 360, 1.25f);
			const auto full = EnumeratePattern(params, 0, 0, 640, 360);
			bool subsetOk = true, coveredOk = true;
			for (int top = 0; top < 360; top += 53)
				for (int left = 0; left < 640; left += 97)
				{
					const int right = (std::min)(left + 97, 640), bottom = (std::min)(top + 53, 360);
					const auto tile = EnumeratePattern(params, left, top, right, bottom);
					for (const auto& primitive : tile)
						subsetOk = subsetOk && std::find(full.begin(), full.end(), primitive) != full.end();
					for (const auto& primitive : full)
					{
						const double halfWidth = primitive[4];
						const bool intersects = kind == WhiteboardPattern::Dots
							? primitive[0] + halfWidth > left && primitive[0] - halfWidth < right &&
								primitive[1] + halfWidth > top && primitive[1] - halfWidth < bottom
							: (primitive[0] == primitive[2]
								? primitive[0] + halfWidth > left && primitive[0] - halfWidth < right
								: primitive[1] + halfWidth > top && primitive[1] - halfWidth < bottom);
						if (intersects)
							coveredOk = coveredOk && std::find(tile.begin(), tile.end(), primitive) != tile.end();
					}
				}
			check(subsetOk, "tile primitives are bit-identical to the full-frame primitives");
			check(coveredOk, "every full-frame primitive touching a tile is emitted for that tile");
		}

		// 外观键：只含样式/明暗/间距；相位（视口平移）不入键，主题或 DPI 变化才触发整屏重合成。
		const auto keyAt = [&](WhiteboardPattern kind, std::uint32_t rgb, float dpi, double viewport) {
			return MakeWhiteboardPatternKey(ResolveWhiteboardPatternForFrame(true,
				WhiteboardTheme{ rgb, kind }, viewport, viewport, 1920, 1080, dpi));
		};
		check(keyAt(WhiteboardPattern::Dots, 0x123B32, 1.0f, 0.0) == keyAt(WhiteboardPattern::Dots, 0x123B32, 1.0f, 777.5),
			"pattern key ignores the viewport phase");
		check(keyAt(WhiteboardPattern::Dots, 0x123B32, 1.0f, 0.0) == keyAt(WhiteboardPattern::Dots, 0x172A46, 1.0f, 0.0),
			"pattern key ignores the colour when the shade is unchanged");
		check(keyAt(WhiteboardPattern::Dots, 0x123B32, 1.0f, 0.0) != keyAt(WhiteboardPattern::Grid, 0x123B32, 1.0f, 0.0), "pattern key: dots vs grid");
		check(keyAt(WhiteboardPattern::Dots, 0x123B32, 1.0f, 0.0) != keyAt(WhiteboardPattern::Dots, 0xFFFFFF, 1.0f, 0.0), "pattern key: dark vs light shade");
		check(keyAt(WhiteboardPattern::Dots, 0x123B32, 1.0f, 0.0) != keyAt(WhiteboardPattern::Dots, 0x123B32, 1.5f, 0.0), "pattern key: DPI");
		check(keyAt(WhiteboardPattern::Blank, 0x123B32, 1.0f, 0.0) == WhiteboardPatternKey{}, "pattern key: blank equals the disabled key");
	}
}

namespace
{
	// Freeze 归属下的点阵/网格相位跟随：无锁视口通道、相位数学、重画规划、页身份过期、归属开关门控与诊断聚合。
	void TestWhiteboardFreezePatternFollow(Checks& check)
	{
		namespace Follow = Inkeys::UI::WhiteboardFollow;
		using Sample = Follow::Sample;
		const auto sample = [](bool active, bool moving, std::uint32_t page, std::uint64_t generation, float x, float y) {
			Sample value;
			value.active = active; value.moving = moving; value.page = page;
			value.generation = generation; value.x = x; value.y = y;
			return value;
		};

		// ---- 归属开关：单点、互斥、Draw3 侧门控由同一纯函数决定 ----
		{
			using Inkeys::Drawing::Draw3::WhiteboardPatternOwner;
			check(kWhiteboardPatternOwnedByFreeze != kWhiteboardPatternOwnedByDraw3, "pattern owner constants are exclusive");
			check(WhiteboardPatternDrawnByDraw3(true) == kWhiteboardPatternOwnedByDraw3, "default gate follows the owner constant");
			check(!WhiteboardPatternDrawnByDraw3(false) && !WhiteboardPatternDrawnByDraw3(false, WhiteboardPatternOwner::Draw3),
				"desktop and presentation never draw the Draw3 pattern");
			check(!WhiteboardPatternDrawnByDraw3(true, WhiteboardPatternOwner::Freeze) &&
				WhiteboardPatternDrawnByDraw3(true, WhiteboardPatternOwner::Draw3), "explicit owners select the Draw3 pass");
			check(WhiteboardPatternDrawnByFreeze(WhiteboardPatternOwner::Freeze) && !WhiteboardPatternDrawnByFreeze(WhiteboardPatternOwner::Draw3),
				"explicit owners select the Freeze pattern");
			// 控制器 syncWhiteboardPattern 的组合：Freeze 归属下参数恒为未启用，外观键为空，不会触发 forceFullPresent。
			const WhiteboardTheme dots{ 0x123B32, WhiteboardPattern::Dots };
			for (const WhiteboardPatternOwner owner : { WhiteboardPatternOwner::Freeze, WhiteboardPatternOwner::Draw3 })
			{
				const auto params = ResolveWhiteboardPatternForFrame(WhiteboardPatternDrawnByDraw3(true, owner), dots, 0.0, 0.0, 1920, 1080, 1.0f);
				check(params.Enabled() == (owner == WhiteboardPatternOwner::Draw3), "Draw3 pattern params exist only under Draw3 ownership");
				check((MakeWhiteboardPatternKey(params) == WhiteboardPatternKey{}) == (owner == WhiteboardPatternOwner::Freeze),
					"Freeze ownership keeps the Draw3 appearance key empty");
			}
		}

		// ---- 通道：往返、空通道、位级保持 ----
		{
			Follow::ViewportChannel channel;
			Sample read;
			check(channel.Read(read) == Follow::ReadResult::Empty, "empty channel reports Empty");
			Sample value = sample(true, true, 7, 0x1234567890ABCDEFull, -1048576.0f, 1048575.5f);
			value.serial = 99; value.qpc = -123456789012345LL;
			channel.Write(value);
			check(channel.Read(read) == Follow::ReadResult::Ok && read.active && read.moving && read.page == 7 &&
				read.generation == value.generation && read.x == value.x && read.y == value.y &&
				read.serial == 99 && read.qpc == value.qpc, "channel round-trips every field");
			value = sample(false, false, 0xFFFFFFFFu, 1, std::numeric_limits<float>::quiet_NaN(), -0.0f);
			channel.Write(value);
			check(channel.Read(read) == Follow::ReadResult::Ok && !read.active && !read.moving && read.page == 0xFFFFFFFFu &&
				std::isnan(read.x) && std::signbit(read.y), "channel keeps NaN and signed zero bit-exactly");
		}

		// ---- seqlock 撕裂压力：每个样本满足不变量，读端永不见到混合样本 ----
		{
			Follow::ViewportChannel channel;
			std::atomic<bool> done{ false };
			constexpr std::uint64_t kWrites = 2'000'000;
			std::thread writer([&] {
				for (std::uint64_t n = 1; n <= kWrites; ++n)
				{
					Sample value;
					value.active = true;
					value.moving = (n & 1) != 0;
					value.generation = n;
					value.page = static_cast<std::uint32_t>(n);
					value.x = static_cast<float>(n % 100000);
					value.y = -static_cast<float>(n % 100000);
					value.serial = n * 3;
					value.qpc = static_cast<std::int64_t>(n * 7);
					channel.Write(value);
				}
				done.store(true, std::memory_order_release);
			});
			std::uint64_t reads = 0, torn = 0, contended = 0, lastGeneration = 0, regressions = 0;
			while (!done.load(std::memory_order_acquire) || reads < 1'000'000)
			{
				Sample read;
				const auto result = channel.Read(read);
				if (result == Follow::ReadResult::Empty) continue;
				if (result == Follow::ReadResult::Contended) { ++contended; continue; }
				++reads;
				const std::uint64_t n = read.generation;
				const bool consistent = read.page == static_cast<std::uint32_t>(n) && read.serial == n * 3 &&
					read.qpc == static_cast<std::int64_t>(n * 7) && read.x == static_cast<float>(n % 100000) &&
					read.y == -static_cast<float>(n % 100000) && read.moving == ((n & 1) != 0) && read.active;
				if (!consistent) ++torn;
				if (n < lastGeneration) ++regressions;
				lastGeneration = n;
				if (done.load(std::memory_order_acquire) && reads >= 1'000'000) break;
			}
			writer.join();
			Info("seqlock stress reads=" + std::to_string(reads) + " contended=" + std::to_string(contended) +
				" torn=" + std::to_string(torn));
			check(reads >= 1'000'000, "seqlock stress performed at least one million consistent reads");
			check(torn == 0, "seqlock never exposes a torn sample");
			check(regressions == 0, "seqlock samples never move backwards");
			Sample finalRead;
			check(channel.Read(finalRead) == Follow::ReadResult::Ok && finalRead.generation == kWrites, "final sample is the last write");
		}

		// ---- 相位数学：与 Draw3 后备路径同源，视口 (0,0) 等于旧 Freeze 相位 ----
		{
			for (const float scale : { 1.0f, 1.1f, 1.25f, 1.5f, 2.0f })
			{
				const double step = 24.0 * scale;
				const Follow::Phase zero = Follow::ResolvePhase(1920.0, 1080.0, scale, 0.0, 0.0);
				check(zero.xPx == std::fmod(960.0, step) && zero.yPx == std::fmod(540.0, step), "phase at viewport zero is fmod(W/2, step)");
				check(zero.xDip == zero.xPx / scale && zero.yDip == zero.yPx / scale, "DIP phase is the pixel phase over the scale");
				for (const std::pair<double, double> viewport : { std::pair<double, double>{ 13.25, -7.5 }, { -1048576.0, 1048576.0 },
					{ 1048575.5, -0.125 }, { 0.0, 1e-9 }, { -step, step * 1000.0 } })
				{
					const Follow::Phase phase = Follow::ResolvePhase(1920.0, 1080.0, scale, viewport.first, viewport.second);
					check(phase.xPx >= 0.0 && phase.xPx < step && phase.yPx >= 0.0 && phase.yPx < step, "phase stays inside one period");
					const auto draw3 = MakeWhiteboardPatternParams(WhiteboardTheme{ 0x123B32, WhiteboardPattern::Dots },
						viewport.first, viewport.second, 1920, 1080, scale);
					check(phase.xPx == draw3.phaseX && phase.yPx == draw3.phaseY, "Freeze phase equals the Draw3 fallback phase");
					const Follow::Phase shifted = Follow::ResolvePhase(1920.0, 1080.0, scale, viewport.first + step * 4.0, viewport.second - step * 6.0);
					check(std::fabs(shifted.xPx - phase.xPx) < 1e-6 || std::fabs(std::fabs(shifted.xPx - phase.xPx) - step) < 1e-6,
						"phase is periodic in the viewport");
				}
			}
			check(Follow::ResolvePhase(1920.0, 1080.0, 0.0f, 1.0, 2.0) == Follow::Phase{}, "invalid scale gives a zero phase");
			check(Follow::CircularDistance(23.99, 0.01, 24.0) < 0.03 && Follow::CircularDistance(5.0, 5.0, 24.0) == 0.0, "circular phase distance wraps");
		}

		// ---- 规划：最新者胜出、运动期 ε 合并、落定帧精确、页身份过期、Blank 不画 ----
		{
			constexpr double W = 1920.0, H = 1080.0;
			Follow::State state;
			const std::uint64_t known = 5;
			const auto plan = [&](const Sample& s, std::uint64_t knownGeneration = 5, bool enabled = true, float scale = 1.0f) {
				return Follow::PlanFollow(s, knownGeneration, state, W, H, scale, enabled);
			};
			// 从未画过：首帧必画，且空样本使用视口 (0,0) 的相位（与改动前像素一致）。
			Follow::Plan first = plan(sample(false, false, 0, 0, 0, 0));
			check(!first.usable && !first.draw && first.phase == Follow::ResolvePhase(W, H, 1.0f, 0.0, 0.0), "unusable sample falls back to the viewport-zero phase");
			first = plan(sample(true, false, 0, 5, 0.0f, 0.0f));
			check(first.usable && first.draw, "first usable sample always draws");
			state.drawnValid = true; state.drawn = first.phase;
			// 页身份：更老的 generation 属于上一段会话/上一页，绝不能画到当前页上。
			check(!plan(sample(true, false, 2, 4, 300.0f, 0.0f), known).usable && !plan(sample(true, false, 2, 4, 300.0f, 0.0f), known).draw,
				"sample from an older generation is rejected");
			check(plan(sample(true, false, 2, 5, 300.0f, 0.0f), known).draw, "sample with the known generation is accepted");
			check(plan(sample(true, false, 9, 6, 300.0f, 0.0f), known).draw, "newer generation arriving before the page snapshot is accepted");
			check(plan(sample(true, false, 3, 5, 300.0f, 0.0f), known).usable, "same generation on another page is still accepted");
			check(!plan(sample(false, false, 0, 9, 300.0f, 0.0f), known).usable, "inactive sample is never used");
			check(!plan(sample(true, false, 0, 9, std::numeric_limits<float>::infinity(), 0.0f), known).usable &&
				!plan(sample(true, false, 0, 9, 0.0f, std::numeric_limits<float>::quiet_NaN()), known).usable, "non-finite viewport is rejected");
			check(plan(sample(false, false, 0, 9, 300.0f, 0.0f), known).phase == state.drawn, "rejected sample keeps the drawn phase");
			// ε：运动期亚阈值位移合并，落定帧精确重画。
			check(!plan(sample(true, true, 0, 5, 0.2f, 0.0f)).draw, "moving sub-epsilon shift is coalesced");
			check(plan(sample(true, true, 0, 5, 0.3f, 0.0f)).draw, "moving shift above epsilon redraws");
			check(plan(sample(true, false, 0, 5, 0.2f, 0.0f)).draw, "settled frame redraws even for a sub-epsilon difference");
			check(!plan(sample(true, false, 0, 5, 0.0f, 0.0f)).draw, "settled frame equal to the drawn phase does not redraw");
			check(!plan(sample(true, true, 0, 5, 24.0f * 7.0f, 24.0f * -3.0f)).draw, "whole-period shift needs no redraw");
			state.drawn = Follow::ResolvePhase(W, H, 1.0f, 0.01, 0.0); // 相位约 23.99
			check(!plan(sample(true, true, 0, 5, -0.01f, 0.0f)).draw, "phase wrap 23.99 -> 0.01 counts as a tiny move");
			state.drawn = first.phase;
			check(!plan(sample(true, true, 0, 5, 500.0f, 0.0f), 5, false).draw && plan(sample(true, true, 0, 5, 500.0f, 0.0f), 5, false).usable,
				"Blank pattern never draws but still resolves the phase");
			check(!plan(sample(true, true, 0, 5, 500.0f, 0.0f), 5, true, 0.1f).draw, "out-of-range DPI scale is not drawn");
		}

		// ---- 合成回放：多样本合并为最新者；运动期少画；落定帧精确且之后静止 ----
		{
			constexpr double W = 1920.0, H = 1080.0;
			Follow::State state;
			std::uint64_t serial = 0, draws = 0;
			const std::uint64_t generation = 3;
			Sample latest;
			double viewport = 0.0;
			for (int callbackIndex = 0; callbackIndex < 60; ++callbackIndex)
			{
				// 每个回调之间 Draw3 发布 8 个样本（约 480 Hz）；只有最后一个会被读到。
				for (int n = 0; n < 8; ++n) { viewport += 3.7; latest = sample(true, true, 0, generation, static_cast<float>(viewport), 0.0f); latest.serial = ++serial; }
				const Follow::Plan p = Follow::PlanFollow(latest, generation, state, W, H, 1.0f, true);
				if (p.draw) { state.drawnValid = true; state.drawn = p.phase; state.drawnSerial = latest.serial; ++draws; }
			}
			check(draws == 60, "one redraw per callback no matter how many samples were published in between");
			check(state.drawnSerial == serial, "the redraw uses the latest published sample");
			// 惰性尾巴：运动中但位移 <ε 的样本不触发重画，随后落定样本精确重画一次。
			const std::uint64_t before = draws;
			latest = sample(true, true, 0, generation, static_cast<float>(viewport + 0.1), 0.0f); latest.serial = ++serial;
			Follow::Plan p = Follow::PlanFollow(latest, generation, state, W, H, 1.0f, true);
			check(!p.draw, "inertia tail below epsilon does not redraw");
			latest = sample(true, false, 0, generation, static_cast<float>(viewport + 0.1), 0.0f); latest.serial = ++serial;
			p = Follow::PlanFollow(latest, generation, state, W, H, 1.0f, true);
			check(p.draw && p.phase == Follow::ResolvePhase(W, H, 1.0f, static_cast<float>(viewport + 0.1), 0.0), "settle redraws at the exact final phase");
			state.drawnValid = true; state.drawn = p.phase; state.drawnSerial = latest.serial; ++draws;
			p = Follow::PlanFollow(latest, generation, state, W, H, 1.0f, true);
			check(!p.draw && draws == before + 1, "after the settled frame the plan is idle");
		}

		// ---- 唤醒门控与发布去重 ----
		{
			check(!Follow::ShouldWakeFreeze(0, true, true), "Blank pattern never wakes Freeze");
			check(!Follow::ShouldWakeFreeze(1, false, true) && !Follow::ShouldWakeFreeze(2, true, false), "inactive whiteboard or inactive sample never wakes Freeze");
			check(Follow::ShouldWakeFreeze(1, true, true) && Follow::ShouldWakeFreeze(2, true, true), "dots and grid wake Freeze");
			// 退休规则：Freeze 归属下纹理类型变化退休像素；Draw3 归属下只有底色变化才退休。视口相位不在此规则内。
			check(Follow::ThemeChangeRetiresPixels(0x123B32, 1, 0x123B32, 2, true), "Freeze ownership: pattern-kind change retires pixels");
			check(!Follow::ThemeChangeRetiresPixels(0x123B32, 1, 0x123B32, 2, false), "Draw3 ownership: pattern-kind change does not retire pixels");
			check(Follow::ThemeChangeRetiresPixels(0x123B32, 1, 0x172A46, 1, true) && Follow::ThemeChangeRetiresPixels(0x123B32, 1, 0x172A46, 1, false),
				"RGB change retires pixels under either owner");
			check(!Follow::ThemeChangeRetiresPixels(0x123B32, 1, 0x123B32, 1, true), "identical theme never retires pixels");
			const Sample base = sample(true, true, 2, 5, 10.0f, 20.0f);
			check(!WhiteboardViewportSampleChanged(base, base), "identical sample is deduplicated");
			Sample other = base; other.serial = 42; other.qpc = 99;
			check(!WhiteboardViewportSampleChanged(base, other), "serial and qpc do not affect deduplication");
			for (int field = 0; field < 5; ++field)
			{
				other = base;
				if (field == 0) other.moving = false;
				if (field == 1) other.page = 3;
				if (field == 2) other.generation = 6;
				if (field == 3) other.x = 10.5f;
				if (field == 4) other.y = 19.0f;
				check(WhiteboardViewportSampleChanged(base, other), "each identity or position field changes the sample");
			}
			check(WhiteboardViewportSampleChanged(base, sample(false, false, 0, 0, 0, 0)), "leaving the whiteboard publishes once");
			check(!WhiteboardViewportSampleChanged(sample(false, false, 0, 0, 0, 0), sample(false, true, 4, 9, 7.0f, 7.0f)),
				"inactive samples are equivalent regardless of stale fields");
		}

		// ---- 诊断聚合（M1/M2）：固定容量，无分配 ----
		{
			Follow::Profile profile;
			check(profile.Empty() && profile.Summarize().updates == 0, "empty profile summarises to zero");
			for (int n = 0; n < 101; ++n) profile.Record(1000.0 + n * 16.0, n % 3, 20.0 + (n % 5), 4.0 * (n % 7), 8.0);
			profile.RecordAck(2.0); profile.RecordAck(4.0);
			profile.RecordSettle(33.5);
			const auto summary = profile.Summarize();
			check(summary.updates == 101 && summary.dropped == 0, "profile counts updates");
			check(std::fabs(summary.segmentMs - 1600.0) < 1e-9 && std::fabs(summary.updatesPerSecond - 62.5) < 1e-9, "profile reports updates per second");
			check(std::fabs(summary.gapP50 - 16.0) < 1e-9 && std::fabs(summary.gapMax - 16.0) < 1e-9, "profile reports update gaps");
			check(summary.lagFramesMax == 2.0 && summary.lagMsMax == 24.0 && summary.lagPxMax == 24.0 && summary.costMsP50 == 8.0, "profile reports lag and cost");
			check(summary.ackSamples == 2 && summary.ackLagFramesMax == 4.0 && summary.settleMs == 33.5, "profile reports ack lag and settle latency");
			for (int n = 0; n < 600; ++n) profile.Record(5000.0 + n, 0, 0, 0, 0);
			check(profile.Summarize().updates == Follow::Profile::kCapacity && profile.Summarize().dropped > 0, "profile drops instead of allocating past capacity");
			profile.Reset();
			check(profile.Empty(), "profile resets for the next pan segment");
		}
	}
}

int RunDraw3CanvasPanTests()
{
	Checks check;
	TestReleaseGate(check);
	TestTrackerAccuracy(check);
	TestTrackerValidity(check);
	TestTrackerInvalidInput(check);
	TestFrictionIntegrator(check);
	TestSingleFingerRelease(check);
	TestSingleStepDownUpDisplacement(check);
	TestSameStepHandoffTap(check);
	TestSameStepHandoffPress(check);
	TestSameStepHandoffMove(check);
	TestSameStepHandoffTrackerGap(check);
	TestStopHandNoPhantomInertia(check);
	TestStopHandAfterEarlyLift(check);
	TestTwoFingerStaggeredLift(check);
	TestTopologyChangesKeepEstimators(check);
	TestPressAndHoldDuringCoasting(check);
	TestSameDirectionAcceleration(check);
	TestReverseDirectionDecelerationFasterThanHold(check);
	TestHoldAfterTinyWiggle(check);
	TestLiftWhileSlipping(check);
	TestTapLeavesStateAlone(check);
	TestTapUsesLiftTimestamp(check);
	TestQueuedHitchAndInputStall(check);
	TestTopologyPauseRestartFit(check);
	TestRepeatedFlicksSaturate(check);
	TestTwoSwipeSequences(check);
	TestPenAndStop(check);
	TestCancelAndBackwardsRelease(check);
	TestInvalidInputsLeaveStateAlone(check);
	TestClearAxis(check);
	TestWhiteboardPatternGeometry(check);
	TestWhiteboardFreezePatternFollow(check);
	if (check.failures == 0)
		std::cout << "PASS Draw3 canvas pan tracker, friction integrator, coupling dynamics, whiteboard pattern geometry and Freeze pattern follow\n";
	return check.failures;
}
