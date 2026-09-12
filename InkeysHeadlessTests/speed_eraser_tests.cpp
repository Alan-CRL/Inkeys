#include "../Inkeys/Inkeys/Drawing/Draw3/Draw3.SpeedEraser.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <iostream>
#include <limits>
#include <vector>

namespace
{
	using namespace Inkeys::Drawing::Draw3::SpeedEraser;
	struct Knot { double time, x, y = 0.0; };

	Knot Position(const std::vector<Knot>& path, double time)
	{
		for (size_t i = 1; i < path.size(); ++i)
		{
			if (time > path[i].time) continue;
			const auto& a = path[i - 1];
			const auto& b = path[i];
			const double fraction = std::clamp((time - a.time) / (b.time - a.time), 0.0, 1.0);
			return { time, a.x + (b.x - a.x) * fraction, a.y + (b.y - a.y) * fraction };
		}
		return { time, path.back().x, path.back().y };
	}

	// 时间轴分别调度真实输入、渲染帧和观察点；观察点不伪造额外输入。
	std::vector<float> Replay(const Config& config, const std::vector<Knot>& path,
		int inputHz, int frameHz, const std::vector<double>& checkpoints,
		StartKind startKind = StartKind::Hover)
	{
		Controller controller;
		controller.Reset(static_cast<float>(path.front().x / config.motionPerPixelX),
			static_cast<float>(path.front().y / config.motionPerPixelY), 0.0, startKind, config);
		int sample = 1;
		int frame = 1;
		std::vector<float> result;
		for (const double checkpoint : checkpoints)
		{
			while (std::min(static_cast<double>(sample) / inputHz,
				static_cast<double>(frame) / frameHz) <= checkpoint + 1e-10)
			{
				const double sampleTime = static_cast<double>(sample) / inputHz;
				const double frameTime = static_cast<double>(frame) / frameHz;
				if (sampleTime <= frameTime)
				{
					const auto point = Position(path, sampleTime);
					controller.UpdatePosition(static_cast<float>(point.x / config.motionPerPixelX),
						static_cast<float>(point.y / config.motionPerPixelY), sampleTime);
					++sample;
				}
				else
				{
					controller.Advance(frameTime);
					++frame;
				}
			}
			result.push_back(controller.Advance(checkpoint));
		}
		return result;
	}

	void FeedLine(Controller& controller, double speed, double duration,
		int hz = 125, const Config& config = Config{})
	{
		for (int i = 1; i <= static_cast<int>(duration * hz + 0.5); ++i)
		{
			const double time = static_cast<double>(i) / hz;
			controller.UpdatePosition(static_cast<float>(time * speed / config.motionPerPixelX), 0.0f, time);
		}
	}
}

int RunSpeedEraserTests()
{
	int failures = 0;
	auto expect = [&](bool condition, const char* name)
	{
		if (!condition) { ++failures; std::cerr << "[SpeedEraser] failed: " << name << '\n'; }
	};
	auto near = [&](double actual, double expected, double tolerance, const char* name)
	{
		if (std::abs(actual - expected) > tolerance)
		{
			++failures;
			std::cerr << "[SpeedEraser] failed: " << name << " actual=" << actual
				<< " expected=" << expected << " tolerance=" << tolerance << '\n';
		}
	};

	DisplayScale physical;
	physical.generation = 7;
	physical.revision = 2;
	physical.monitor = 9;
	physical.cmPerPixelX = 0.02f;
	physical.cmPerPixelY = 0.04f;
	physical.physicalAvailable = true;
	physical.directTouchMapped = true;
	const auto physicalTouch = ResolveConfig(physical, DeviceMode::LargeScreen, true);
	const auto physicalPen = ResolveConfig(physical, DeviceMode::Laptop, false);
	expect(physicalTouch.motionSource == ScaleSource::Physical, "mapped touch uses cm motion");
	expect(physicalPen.motionSource == ScaleSource::Dip && physicalPen.coverageSource == ScaleSource::Physical,
		"indirect input uses DIP motion independently of physical coverage");
	near(physicalTouch.minimumDiameterPx, 1.0 / std::sqrt(0.02 * 0.04), 0.0001, "geometric mean coverage density");
	near(physicalPen.maximumDiameterPx, 4.0 / std::sqrt(0.02 * 0.04), 0.0001, "laptop physical maximum");
	DisplayScale ambiguous = physical;
	ambiguous.directTouchMapped = false;
	expect(ResolveConfig(ambiguous, DeviceMode::LargeScreen, true).motionSource == ScaleSource::Dip,
		"ambiguous target mapping does not imply physical hand motion");
	DisplayScale invalid = physical;
	invalid.physicalAvailable = false;
	const auto invalidConfig = ResolveConfig(invalid, DeviceMode::LargeScreen, true);
	expect(invalidConfig.motionSource == ScaleSource::Dip && invalidConfig.coverageSource == ScaleSource::Dip,
		"clone unknown topology or unavailable EDID use explicit DIP fallback");
	invalid = physical;
	invalid.cmPerPixelX = 0.0f;
	expect(ResolveConfig(invalid, DeviceMode::Laptop, true).coverageSource == ScaleSource::Dip,
		"zero physical metric cannot become valid coverage");
	invalid.cmPerPixelX = std::numeric_limits<float>::quiet_NaN();
	expect(ResolveConfig(invalid, DeviceMode::Laptop, true).coverageSource == ScaleSource::Dip,
		"nonfinite physical metric cannot become valid coverage");
	DisplayScale rotated = physical;
	std::swap(rotated.cmPerPixelX, rotated.cmPerPixelY);
	const auto rotatedConfig = ResolveConfig(rotated, DeviceMode::LargeScreen, true);
	near(rotatedConfig.maximumDiameterPx, physicalTouch.maximumDiameterPx, 0.0001, "rotation retains coverage area");
	near(rotatedConfig.motionPerPixelX, physicalTouch.motionPerPixelY, 0.000001, "rotation swaps motion axes");

	double worstRateError = 0.0;
	double worstIdleResidual = 0.0;
	for (const auto mode : { DeviceMode::LargeScreen, DeviceMode::Laptop })
	{
		for (const bool usePhysical : { false, true })
		{
			for (const int dpi : { 96, 144, 192 })
			{
				DisplayScale display = usePhysical ? physical : DisplayScale{};
				display.dipPerPixelX = display.dipPerPixelY = 96.0f / dpi;
				const auto config = ResolveConfig(display, mode, usePhysical);
				const double speed = config.maximumSpeed * 1.25;
				const std::vector<Knot> stop{ {0,0}, {1,speed}, {4,speed} };
				const std::vector<double> checkpoints{ 0.25, 0.5, 1.0, 1.12, 1.5, 2.3, 4.0 };
				const auto reference = Replay(config, stop, 1000, 240, checkpoints);
				expect(reference[2] >= config.maximumDiameterPx * 0.97f, "fast local motion reaches sweep size");
				expect(reference[3] >= reference[2] * 0.9f, "120ms pause preserves sweep size");
				worstIdleResidual = std::max(worstIdleResidual,
					static_cast<double>(reference[5] / config.minimumDiameterPx - 1.0f));
				expect(reference[5] <= config.minimumDiameterPx * 1.13f, "1.3s idle returns near minimum");
				near(reference[6], config.minimumDiameterPx, 0.001, "idle finally settles exactly");
				for (const int inputHz : { 60, 125, 240, 1000 })
				{
					for (const int frameHz : { 30, 60, 144, 240 })
					{
						const auto result = Replay(config, stop, inputHz, frameHz, checkpoints);
						for (size_t i = 0; i < result.size(); ++i)
						{
							const double error = std::abs(result[i] - reference[i]) / reference[i];
							worstRateError = std::max(worstRateError, error);
							expect(error <= 0.05, "sample and frame rate invariance within 5 percent");
							expect(result[i] >= config.minimumDiameterPx - 0.001f &&
								result[i] <= config.maximumDiameterPx + 0.001f, "diameter stays inside scaled bounds");
						}
					}
				}
				const std::vector<Knot> fine{ {0,0}, {1,speed}, {4,speed + 1.5 * config.minimumSpeed} };
				const auto fineResult = Replay(config, fine, 125, 60, {1.0, 1.12, 2.3});
				expect(fineResult[1] >= fineResult[0] * 0.9f &&
					fineResult[2] <= config.minimumDiameterPx * 1.13f, "sweep converts to sustained fine erasing");
				const std::vector<Knot> turn{ {0,0}, {1,speed}, {1.12,speed}, {1.62,speed * 0.5} };
				const auto turnResult = Replay(config, turn, 125, 60, {1,1.12,1.2,1.6});
				expect(turnResult[1] >= turnResult[0] * 0.9f && turnResult[2] >= turnResult[0] * 0.9f,
					"pause followed by reversal does not collapse diameter");
				std::vector<Knot> sweep{ {0,0} };
				for (int i = 1; i <= 20; ++i)
					sweep.push_back({ i * 0.12, i % 2 ? speed * 0.12 : 0.0 });
				const auto sweepResult = Replay(config, sweep, 125, 60, {0.8,1.0,1.5,2.0,2.4});
				for (const float diameter : sweepResult)
					expect(diameter >= config.maximumDiameterPx * 0.9f && diameter <= config.maximumDiameterPx + 0.001f,
						"continuous local back and forth stays large without accumulating beyond maximum");
			}
		}
	}

	const Config laptop = ResolveConfig({}, DeviceMode::Laptop, false);
	const std::vector<Knot> acceleration{ {0,0}, {1,60}, {2,1060} };
	Config noBoost = laptop;
	noBoost.acceleratedGrowthTauSeconds = noBoost.growthTauSeconds;
	const auto accelerated = Replay(laptop, acceleration, 125, 60, {1.0,1.45,2.0});
	const auto normal = Replay(noBoost, acceleration, 125, 60, {1.0,1.45,2.0});
	expect(accelerated[1] > normal[1], "scalar speed increase permits quicker growth");
	std::vector<Knot> noise{ {0,0} };
	double x = 0.0;
	for (int i = 1; i <= 100; ++i)
	{
		x += (i % 2 ? 14.0 : 16.0) * 0.02;
		noise.push_back({ i * 0.02, x });
	}
	for (const auto value : Replay(laptop, noise, 125, 60, {0.2,0.7,1.2,2.0}))
		near(value, laptop.minimumDiameterPx, 0.001, "low speed noise stays at small endpoint");

	for (const Config config : { laptop, physicalTouch })
	{
		Controller touch;
		touch.Reset(0,0,0,StartKind::Touch,config);
		near(touch.Advance(10.0), config.minimumDiameterPx, 0.001, "new touch held still never grows");
		touch.Reset(0,0,0,StartKind::Touch,config);
		for (int i = 1; i <= 1000; ++i)
		{
			const float position = (i % 2 ? 0.4f : -0.4f) * config.touchUnlockStart / config.motionPerPixelX;
			touch.UpdatePosition(position, 0, i * 0.002);
		}
		near(touch.Diameter(), config.minimumDiameterPx, 0.001, "touch jitter travel does not unlock growth");
		for (int tap = 0; tap < 8; ++tap)
		{
			touch.Reset(100.0f * tap, 0, 5.0 + tap, StartKind::Touch, config);
			near(touch.Advance(5.5 + tap), config.minimumDiameterPx, 0.001, "each genuine touch down resets for dotted erasing");
		}
		touch.Reset(0,0,0,StartKind::Touch,config);
		FeedLine(touch, config.maximumSpeed * 1.5, 1.0, 125, config);
		expect(touch.Diameter() > config.maximumDiameterPx * 0.97f, "observed touch movement unlocks growth");
	}

	Controller hover;
	hover.Reset(0,0,0,StartKind::Hover,laptop);
	FeedLine(hover, 200, 1.0);
	Controller contact = hover;
	near(contact.Diameter(), hover.Diameter(), 0.0001, "hover down copies the whole dynamic state");
	contact.PauseForReconnect(1.0);
	const float pausedDiameter = contact.Diameter();
	near(contact.Advance(100.0), pausedDiameter, 0.0001, "reconnect wait freezes dynamics");
	Controller nearby = contact;
	near(contact.ResumeFromReconnect(100000,20000,2.0), pausedDiameter, 0.0001,
		"far synthetic connection does not change diameter");
	nearby.ResumeFromReconnect(200,0,2.0);
	contact.UpdatePosition(100001,20000,2.008);
	nearby.UpdatePosition(201,0,2.008);
	near(contact.Diameter(), nearby.Diameter(), 0.0001, "resume excludes bridge distance from speed history");
	expect(!contact.IsPaused(), "resume clears pause status");
	Controller touchGap;
	touchGap.Reset(0,0,0,StartKind::Touch,laptop);
	touchGap.PauseForReconnect(0.1);
	touchGap.ResumeFromReconnect(100000,20000,0.2);
	touchGap.UpdatePosition(100000,20000,0.21);
	near(touchGap.Diameter(), laptop.minimumDiameterPx, 0.001, "synthetic connection cannot unlock touch startup");

	Controller delayed = hover;
	Controller timely = hover;
	delayed.Advance(1.1);
	delayed.UpdatePosition(204,0,1.02);
	timely.UpdatePosition(204,0,1.02);
	near(delayed.Diameter(), timely.Diameter(), 0.0001, "late new raw sample is independent of newer preview frame");
	const float beforeInvalid = delayed.Diameter();
	delayed.UpdatePosition(100000,100000,1.02);
	delayed.UpdatePosition(100000,100000,0.9);
	delayed.UpdatePosition(std::numeric_limits<float>::quiet_NaN(),0,1.03);
	near(delayed.Diameter(), beforeInvalid, 0.0001, "duplicate backwards and invalid packets do not invent speed");
	Controller oneIdle = hover;
	Controller manyIdle = hover;
	oneIdle.Advance(4.0);
	for (int i = 1; i <= 300; ++i) manyIdle.Advance(1.0 + i * 0.01);
	near(oneIdle.Diameter(), manyIdle.Diameter(), 0.001, "long idle advance retains elapsed time");
	expect(!oneIdle.NeedsAnimation(4.0), "settled hover stops animation wakes");
	DisplayScale changed;
	changed.generation = 88;
	changed.revision = 2;
	const auto newConfig = ResolveConfig(changed, DeviceMode::LargeScreen, true);
	expect(hover.Configuration() == laptop && hover.Configuration() != newConfig,
		"existing controller retains its batch config across display changes");
	expect(ContactBatchContains(100,500,0,490), "overlapping down retains batch after old up was consumed");
	expect(!ContactBatchContains(100,500,0,501), "later independent down uses new batch");
	expect(ContactBatchContains(100,500,550,525), "pending reconnect retains batch within deadline");
	expect(!ContactBatchContains(100,500,550,551), "expired reconnect releases batch");
	expect(!ContactBatchContains(100,500,0,99), "older down cannot join newer contact batch");

	const WidthInterval interval{ 0,1,50,600,8,800 };
	near(InterpolateDiameter(interval,0.5),325,0.001,"scaled width interpolation exceeds old 200px maximum");
	near(InterpolateDiameter({0,1,4,8,4,8},0),4,0.001,"scaled minimum may be below old 20px limit");
	near(ContactDiameter(162.5f,16.0f),325,0.001,"contact cursor uses accepted geometric endpoint");
	near(ContactDiameter(0,24),24,0.001,"initial cursor uses batch minimum");
	std::cout << "[SpeedEraser] worst sample/frame deviation=" << worstRateError * 100.0
		<< "% idle residual at 1.3s=" << worstIdleResidual * 100.0 << "% failures=" << failures << '\n';
	return failures;
}
