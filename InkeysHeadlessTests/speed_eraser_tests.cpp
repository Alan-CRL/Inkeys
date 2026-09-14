#include "../Inkeys/Inkeys/Drawing/Draw3/Draw3.SpeedEraser.h"

#include <algorithm>
#include <bit>
#include <array>
#include <cmath>
#include <iostream>
#include <limits>
#include <vector>

import Inkeys.Drawing.Draw3.contact_input;

#if defined(DRAW3_TESTING)
import Inkeys.Drawing.Draw3.realtime_stylus;
#endif

namespace
{
	using namespace Inkeys::Drawing::Draw3::SpeedEraser;
	struct Knot { double time, x, y = 0.0; };
	InputSource MappedSource(SourceKind kind,const DisplayScale& d)
	{
		InputSource s;s.kind=kind;s.recognition=SourceRecognition::PointerCursor;s.mappedMonitor=d.monitor;
		s.mappedLeft=d.desktopLeft;s.mappedTop=d.desktopTop;s.mappedWidth=d.pixelWidth;s.mappedHeight=d.pixelHeight;
		return s;
	}

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

	struct AreaTrace
	{
		float diameter, target;
		ContactAreaDiagnostics area;
		bool animating;
		double wake;
	};
	ContactAreaSample AreaSample(const Config& c,float widthDip,float heightDip)
	{
		return {widthDip/c.display.dipPerPixelX*10,heightDip/c.display.dipPerPixelY*10,
			widthDip/c.display.dipPerPixelX,heightDip/c.display.dipPerPixelY,ContactAreaUnits::CanvasPixels};
	}
	template<class Provider>
	std::vector<AreaTrace> ReplayArea(const Config& c,const std::vector<Knot>& path,int hz,int fps,
		const std::vector<double>& checkpoints,Provider provider,double inputEnd=1000,StartKind kind=StartKind::Touch)
	{
		Controller controller;const auto initial=provider(0);
		controller.Reset(static_cast<float>(path.front().x/c.motionPerPixelX),
			static_cast<float>(path.front().y/c.motionPerPixelY),0,kind,c,0,&initial);
		int sample=1,frame=1;std::vector<AreaTrace> result;
		for(double checkpoint:checkpoints)
		{
			for(;;)
			{
				const double inputTime=static_cast<double>(sample)/hz;
				const double nextInput=inputTime<=inputEnd+1e-9?inputTime:std::numeric_limits<double>::infinity();
				const double frameTime=static_cast<double>(frame)/fps;
				if(std::min(nextInput,frameTime)>checkpoint+1e-10)break;
				if(nextInput<=frameTime)
				{
					const auto point=Position(path,inputTime);const auto area=provider(inputTime);
					controller.UpdatePosition(static_cast<float>(point.x/c.motionPerPixelX),
						static_cast<float>(point.y/c.motionPerPixelY),inputTime,&area);++sample;
				}
				else {controller.Advance(frameTime);++frame;}
			}
			controller.Advance(checkpoint);
			result.push_back({controller.DiameterDip(),controller.TargetDiameterDip(),controller.AreaDiagnostics(checkpoint),
				controller.NeedsAnimation(checkpoint),controller.NextAreaWakeSeconds()});
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
	physical.pixelWidth=1200;physical.pixelHeight=800;physical.logicalOutputKnown=true;
	const auto physicalTouch = ResolveConfig(physical, DeviceMode::LargeScreen, true);
	const auto physicalPen = ResolveConfig(physical, DeviceMode::Laptop, false);
	expect(physicalTouch.motionSource == ScaleSource::Physical, "mapped touch uses millimeter motion");
	expect(physicalPen.motionSource == ScaleSource::Dip && physicalPen.sizes == EraserSizes{},
		"indirect input uses DIP motion independently of physical coverage");
	near(physicalTouch.minimumDiameterPx,16,0.001,"physical action retains 16 DIP minimum");
	near(physicalPen.maximumDiameterPx,160,0.001,"physical metadata does not set coverage maximum");
	DisplayScale ambiguous = physical;
	ambiguous.directTouchMapped = false;
	expect(ResolveConfig(ambiguous, DeviceMode::LargeScreen, true).motionSource == ScaleSource::ResolutionDpiHeuristic,
		"ambiguous physical mapping uses labelled logical-output heuristic");
	DisplayScale invalid = physical;
	invalid.physicalAvailable = false;
	const auto invalidConfig = ResolveConfig(invalid, DeviceMode::LargeScreen, true);
	expect(invalidConfig.motionSource == ScaleSource::ResolutionDpiHeuristic && invalidConfig.sizes == EraserSizes{},
		"known logical output without EDID uses explicit heuristic fallback");
	invalid = physical;
	invalid.cmPerPixelX = 0.0f;
	expect(ResolveConfig(invalid, DeviceMode::Laptop, true).motionSource == ScaleSource::Dip,
		"zero physical metric cannot become valid coverage");
	invalid.cmPerPixelX = std::numeric_limits<float>::quiet_NaN();
	expect(ResolveConfig(invalid, DeviceMode::Laptop, true).motionSource == ScaleSource::Dip,
		"nonfinite physical metric cannot become valid coverage");
	DisplayScale rotated = physical;
	std::swap(rotated.cmPerPixelX, rotated.cmPerPixelY);
	const auto rotatedConfig = ResolveConfig(rotated, DeviceMode::LargeScreen, true);
	near(rotatedConfig.maximumDiameterPx, physicalTouch.maximumDiameterPx, 0.0001, "rotation retains coverage area");
	near(rotatedConfig.motionPerPixelX, physicalTouch.motionPerPixelY, 0.000001, "rotation swaps motion axes");


	// 先复现标准尺寸下限与空间门槛按包丢失证据的问题，产品实现必须通过同一入口。
	{
		const auto config = ResolveConfig({}, DeviceMode::Laptop, false);
		Controller fine;
		fine.Reset(0,0,0,StartKind::Hover,config);
		expect(fine.Advance(3.0) <= config.minimumDiameterPx * 1.01f,
			"idle contact reaches minimum, not standard");
		MouseLifecycle preview;
		preview.Configure(config);
		preview.ObserveHover(0,0,0);
		expect(preview.Advance(3.0) <= config.minimumDiameterPx * 1.01f,
			"stationary mouse hover reaches minimum");
		Config threshold = config;
		threshold.sweepEnterSpeed=30; threshold.sweepExitSpeed=20; threshold.largeTargetSpeed=70;
		float lowRateDiameter=0;
		for (const int hz : {60,125,240,1000})
		{
			Controller c; c.Reset(0,0,0,StartKind::Hover,threshold);
			FeedLine(c,85,2.0,hz,threshold);
			if(hz==60)lowRateDiameter=c.Diameter();
			std::cout << "[SpeedEraserNoise] hz=" << hz << " diameter=" << c.Diameter()
				<< " evidence=" << c.SweepEvidenceSeconds() << '\n';
			expect(std::abs(c.Diameter()-lowRateDiameter)<=lowRateDiameter*0.05f,
				"sub-noise packet distances preserve full real motion interval evidence");
		}
	}

	// 新交互的回归用例先在旧实现上运行：短促动作不膨胀，大尺寸不因短停塌缩。
	const auto interactionConfig = ResolveConfig({}, DeviceMode::Laptop, false);
	const double interactionSpeed = interactionConfig.largeTargetSpeed * 1.25;
	for (const double burstSeconds : { 0.05, 0.12 })
	{
		const auto burst = Replay(interactionConfig,
			{{0,0},{burstSeconds,interactionSpeed * burstSeconds},{0.6,interactionSpeed * burstSeconds}},
			125,60,{burstSeconds,burstSeconds + 0.08,0.5});
		for (const float diameter : burst)
			expect(diameter <= interactionConfig.StandardDiameterPx() * 1.25f,
				"short fast swipe and residual frames must not create sweep size");
	}
	const auto strongHold = Replay(interactionConfig,
		{{0,0},{1,interactionSpeed},{2.0,interactionSpeed}},125,60,{1.0,1.28,1.55});
	expect(strongHold[1] >= strongHold[0] * 0.9f,
		"established large sweep survives 280ms pause");
	expect(strongHold[2] < strongHold[0],
		"real idle visibly contracts instead of retaining a frozen history radius");

	// 本轮规格回归：尺寸只能来自DIP，普通速度不能靠持续时间进入清扫。
	const auto dipBaseline = ResolveConfig({},DeviceMode::Laptop,false);
	near(dipBaseline.StandardDiameterPx(),32.0,0.001,"standard is an independent 32 DIP at 96 DPI");
	near(ResolveConfig(physical,DeviceMode::Laptop,false).maximumDiameterPx,
		dipBaseline.maximumDiameterPx,0.001,"EDID cannot change dynamic size limits");
	near(ResolveConfig({},DeviceMode::LargeScreen,false).maximumDiameterPx,
		dipBaseline.maximumDiameterPx,0.001,"device mode cannot change DIP size limits");
	const auto ordinaryLong = Replay(dipBaseline,{{0,0},{20,10000}},125,60,{10,20});
	for (const float d : ordinaryLong)
		expect(d <= 32.0f*1.05f,"ordinary 500 DIP per second stays standard after twenty seconds");
	double worstRateError = 0.0;
	double worstPauseDrop = 0.0;
	for (const auto mode : { DeviceMode::LargeScreen, DeviceMode::Laptop })
	{
		for (const bool usePhysical : { false, true })
		{
			for (const int dpi : { 96, 144, 192 })
			{
				DisplayScale display = usePhysical ? physical : DisplayScale{};
				display.dipPerPixelX = display.dipPerPixelY = 96.0f / dpi;
				const auto config = ResolveConfig(display, mode, usePhysical);
				const float sweepTarget=DiameterToCanvasPx(CompensateTargetDiameterDip(config,config.sizes.maximumDiameterDip),display);
				const double speed = config.largeTargetSpeed * 1.25;
				const std::vector<Knot> stop{ {0,0}, {1,speed}, {4,speed} };
				const std::vector<double> checkpoints{ 0.25, 0.5, 1.0, 1.12, 1.5, 2.3, 4.0 };
				const auto reference = Replay(config, stop, 1000, 240, checkpoints);
				expect(reference[2] >= sweepTarget * 0.85f, "fast local motion reaches sweep size");
				expect(reference[3] >= reference[2] * 0.9f, "120ms pause preserves sweep size");
				worstPauseDrop = std::max(worstPauseDrop,
					static_cast<double>(1.0f - reference[4] / reference[2]));
				expect(reference[4] < reference[2], "true stationary state shrinks visibly after its grace period");
				expect(reference[5] <= config.StandardDiameterPx() * 1.10f, "sustained idle eventually releases a large sweep");
				near(reference[6],config.minimumDiameterPx,0.001,"every contact returns to DIP minimum after idle");
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
				const std::vector<Knot> fine{ {0,0}, {1,speed}, {4,speed + 0.3 * config.sweepEnterSpeed} };
				const auto fineResult = Replay(config, fine, 125, 60, {1.0, 1.12, 2.3});
				expect(fineResult[1] >= fineResult[0] * 0.9f &&
					fineResult[2] <= fineResult[0] * 0.7f, "sweep converts to sustained fine erasing");
				const std::vector<Knot> turn{ {0,0}, {1,speed}, {1.12,speed}, {1.62,speed * 0.5} };
				const auto turnResult = Replay(config, turn, 125, 60, {1,1.12,1.2,1.6});
				if(turnResult[1]<turnResult[0]*0.9f || turnResult[2]<turnResult[0]*0.9f)
					std::cerr << "[R7BaselineReverse] mode=" << static_cast<int>(mode) << " physical=" << usePhysical
						<< " dpi=" << dpi << " diameters=" << turnResult[0] << "," << turnResult[1] << "," << turnResult[2] << '\n';
				expect(turnResult[1] >= turnResult[0] * 0.9f && turnResult[2] >= turnResult[0] * 0.9f,
					"pause followed by reversal does not collapse diameter");
				std::vector<Knot> sweep{ {0,0} };
				for (int i = 1; i <= 20; ++i)
					sweep.push_back({ i * 0.12, i % 2 ? speed * 0.12 : 0.0 });
				const auto sweepResult = Replay(config, sweep, 125, 60, {0.8,1.0,1.5,2.0,2.4});
				for (size_t i = 0; i < sweepResult.size(); ++i)
					expect(sweepResult[i] >= sweepTarget * (i == 0 ? 0.65f : i == 1 ? 0.85f : 0.9f) && sweepResult[i] <= config.maximumDiameterPx + 0.001f,
						"continuous local back and forth stays large without accumulating beyond maximum");
			}
		}
	}

	const Config laptop = ResolveConfig({}, DeviceMode::Laptop, false);
	const std::vector<Knot> acceleration{ {0,0}, {1,60}, {1.12,345}, {1.6,345} };
	for (const float d : Replay(laptop,acceleration,125,60,{1.05,1.12,1.3,1.6}))
		expect(d <= laptop.StandardDiameterPx()*1.25f,"isolated acceleration cannot bypass conservative growth");
	const auto onset = Replay(laptop,{{0,0},{1.2,2850}},125,60,{0.12,0.35,0.8,1.0});
	expect(onset[0] <= laptop.StandardDiameterPx()*1.25f,"120ms high speed has not established a sweep");
	expect(onset[1] >= laptop.StandardDiameterPx()*1.25f && onset[1] < laptop.maximumDiameterPx*0.45f,
		"sustained fast action starts controlled expansion by 350ms");
	expect(onset[2] >= laptop.maximumDiameterPx*0.65f && onset[3] >= laptop.maximumDiameterPx*0.85f,
		"sustained fast action reaches visibly large coverage within one second");
	const auto slowRelease = Replay(laptop,{{0,0},{1,2375},{3,2395}},125,60,{1.0,1.55,1.9,3.0});
	expect(slowRelease[1] >= slowRelease[0]*0.9f && slowRelease[2] <= slowRelease[0]*0.9f,
		"continuous slow movement keeps its separate sweep confirmation instead of being treated as idle");
	std::vector<Knot> noise{ {0,0} };
	double x = 0.0;
	for (int i = 1; i <= 100; ++i)
	{
		x += (i % 2 ? 14.0 : 16.0) * 0.02;
		noise.push_back({ i * 0.02, x });
	}
	const auto lowSpeedTrace=Replay(laptop,noise,125,60,{0.2,0.7,1.2,2.0});
	std::cout << "[R7BaselineFine]";for(float d:lowSpeedTrace)std::cout << " " << d;std::cout << '\n';
	// 200ms 仍是从标准尺寸出发的收敛过程，不修改已满意的鼠标动态去满足过早的稳态断言。
	expect(lowSpeedTrace[0]<laptop.StandardDiameterPx() && lowSpeedTrace[0]>lowSpeedTrace[1],
		"fine initial transition converges without overshoot");
	for(size_t i=1;i<lowSpeedTrace.size();++i)
		expect(lowSpeedTrace[i]>=laptop.minimumDiameterPx && lowSpeedTrace[i]<18.5f,"fine low speed settles below standard");

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
		FeedLine(touch, config.largeTargetSpeed * 1.5, 1.0, 125, config);
		expect(touch.Diameter() > DiameterToCanvasPx(CompensateTargetDiameterDip(config,config.sizes.maximumDiameterDip),config.display)*0.85f, "observed touch unlocks its bounded target");
	}

	Controller hover;
	hover.Reset(0,0,0,StartKind::Hover,laptop);
	FeedLine(hover, 200, 1.0);
	Controller contact = hover;
	near(contact.Diameter(), hover.Diameter(), 0.0001, "contact copying preserves reconnect test history");
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


	// 鼠标生命周期直接调用产品接口；Hover 只计算精细预览，不积累清扫。
	for (const auto mode : { DeviceMode::LargeScreen, DeviceMode::Laptop })
	for (const bool validPhysical : { false, true })
	for (const int dpi : {96,144,192})
	{
		auto display = validPhysical ? physical : DisplayScale{};
		display.dipPerPixelX = display.dipPerPixelY = 96.0f / dpi;
		const auto mouseConfig = ResolveConfig(display,mode,false);
		const float standard = mouseConfig.StandardDiameterPx();
		near(standard,DiameterToCanvasPx(32,display),0.001,"standard independently resolves from 32 DIP");
		expect(standard <= mouseConfig.maximumDiameterPx,"standard remains inside existing coverage range");
		MouseLifecycle mouse;
		mouse.Configure(mouseConfig);
		for (int i=0;i<=400;++i)
		{
			mouse.ObserveHover(static_cast<float>(i*50),static_cast<float>(i%2*500),i*0.008);
			expect(mouse.Advance(i*0.008)<=standard+0.001f && mouse.VisualDiameter()>=mouseConfig.minimumDiameterPx-0.001f,"fast hover remains in fine band");
		}
		Controller mouseContact;
		mouse.BeginContact(mouseContact,20000,0,4.0,mouseConfig);
		near(mouseContact.Diameter(),standard,0.001,"mouse down ignores hover speed and starts small");
		near(mouseContact.SweepEvidenceSeconds(),0,0.000001,"mouse down has no inherited sweep evidence");
		mouseContact.UpdatePosition(20000,0,4.5);
		expect(mouseContact.Advance(4.6)<=mouseConfig.minimumDiameterPx*1.02f,"stationary mouse down returns fine");
		const float actualEndDiameter = mouseConfig.maximumDiameterPx * 0.8f;
		mouse.EndContact(mouseContact,actualEndDiameter,20000,0,5.0,false);
		near(mouse.LogicalDiameter(),standard,0.001,"accepted mouse up resets logic immediately");
		near(mouseContact.Diameter(),standard,0.001,"accepted mouse up resets actual controller intent immediately");
		near(mouse.VisualDiameter(),actualEndDiameter,0.001,"release starts from accepted geometry not pending target");
		float lastVisual = actualEndDiameter;
		for (int frame=0;frame<=30;++frame)
		{
			const float shown = mouse.Advance(5.0 + frame*0.008);
			expect(shown <= lastVisual + 0.001f && shown >= mouseConfig.minimumDiameterPx - 0.001f,
				"up-only visual contracts monotonically without extra input or overshoot");
			lastVisual = shown;
		}
		expect(mouse.VisualDiameter()<=standard,"hover continues shrinking after release without a Move");
		expect(!mouse.Releasing(),"140ms release finishes independently of fine preview");
		mouse.BeginContact(mouseContact,20000,0,6.0,mouseConfig);
		mouse.EndContact(mouseContact,actualEndDiameter,20000,0,6.5,false);
		mouse.Advance(6.51);
		mouse.BeginContact(mouseContact,20100,0,6.52,mouseConfig);
		near(mouse.VisualDiameter(),standard,0.001,"new down interrupts release visual at safe size");
		near(mouseContact.Diameter(),standard,0.001,"new down during release uses same safe erasing size");
		mouse.ObserveHover(90000,0,6.51);
		near(mouseContact.SweepEvidenceSeconds(),0,0.000001,"late hover cannot seed new contact history");
		mouseContact.UpdatePosition(20100,0,6.7);
		expect(mouseContact.Diameter()<=standard && mouseContact.SweepEvidenceSeconds()==0,"rapid second down cannot resurrect sweep");
		mouse.EndContact(mouseContact,standard*0.75f,20100,0,7.0,false);
		expect(mouse.Advance(7.1)<=standard*0.75f+0.001f,"release below standard never enlarges");
		mouse.BeginContact(mouseContact,20100,0,8.0,mouseConfig);
		mouse.EndContact(mouseContact,actualEndDiameter,20100,0,8.5,false,true);
		expect(!mouse.ContactOwned() && !mouse.NeedsAnimation(8.5),"cancel releases ownership without a closing eraser");
		mouse.BeginContact(mouseContact,0,0,9.0,mouseConfig);
		mouse.EndContact(mouseContact,actualEndDiameter,0,0,9.5,false);
		auto changedMouseConfig = mouseConfig;
		++changedMouseConfig.display.revision;
		mouse.Configure(changedMouseConfig);
		expect(!mouse.NeedsAnimation(9.51),"configuration change invalidates old-scale closing visual");
		mouse.BeginContact(mouseContact,0,0,10.0,mouseConfig);
		mouse.EndContact(mouseContact,standard,0,0,10.0,false,true);
		expect(!mouse.ContactOwned(),"failed down initialization releases its current contact owner");
		mouse.BeginContact(mouseContact,0,0,11.0,mouseConfig);
		mouse.EndContact(mouseContact,actualEndDiameter,0,0,11.5,false);
		expect(mouse.Advance(12.0)<=standard && !mouse.Releasing(),"late presentation does not restart release");
	}

	MouseLifecycle sharedMouse;
	sharedMouse.Configure(laptop);
	Controller leftMouse, rightMouse;
	sharedMouse.BeginContact(leftMouse,0,0,0.0,laptop);
	sharedMouse.BeginContact(rightMouse,1,0,0.01,laptop);
	sharedMouse.EndContact(leftMouse,80,0,0,1.0,true);
	expect(sharedMouse.ContactOwned() && !sharedMouse.NeedsAnimation(1.0),
		"one mouse button up does not steal another contact owner");
	sharedMouse.EndContact(rightMouse,60,1,0,1.01,false);
	near(sharedMouse.VisualDiameter(),60,0.001,"final shared mouse owner supplies accepted closing size");
	const float sharedMid = sharedMouse.Advance(1.05);
	sharedMouse.EndContact(rightMouse,160,1,0,1.01,false);
	near(sharedMouse.VisualDiameter(),sharedMid,0.001,"duplicate up does not restart closing animation");
	sharedMouse.BeginContact(rightMouse,5,0,1.06,laptop);
	sharedMouse.EndContact(leftMouse,160,0,0,1.0,true);
	expect(sharedMouse.ContactOwned(),"late old up cannot retire new independent down");
	near(rightMouse.Diameter(),laptop.StandardDiameterPx(),0.001,"late old up cannot feed new mouse diameter");


	MouseLifecycle reorderedMouse;
	Controller oldContact, newContact;
	reorderedMouse.Configure(laptop);
	reorderedMouse.BeginContact(oldContact,0,0,0.0,laptop);
	reorderedMouse.BeginContact(newContact,1,0,0.1,laptop);
	reorderedMouse.EndContact(newContact,80,1,0,0.8,true);
	auto reorderedConfig = laptop;
	++reorderedConfig.display.revision;
	reorderedMouse.Configure(reorderedConfig);
	reorderedMouse.EndContact(oldContact,80,0,0,0.05,false);
	expect(!reorderedMouse.ContactOwned() && !reorderedMouse.NeedsAnimation(0.9),
		"late old up after configuration change releases the final owner without reviving old visuals");
	// 中速连续折返应停在对应中间值，而非按折返次数无条件靠近最大值。
	const double mediumSpeed = (laptop.sweepEnterSpeed + laptop.largeTargetSpeed)*0.5;
	std::vector<Knot> moderateSweep{{0,0}};
	for (int i=1;i<=30;++i) moderateSweep.push_back({i*0.12,i%2 ? mediumSpeed*0.12 : 0.0});
	const auto moderate = Replay(laptop,moderateSweep,125,60,{2.0,3.0,3.6});
	const double middleDiameter = std::sqrt(laptop.StandardDiameterPx()*laptop.maximumDiameterPx);
	for (const float d : moderate)
		expect(d >= middleDiameter*0.85 && d <= middleDiameter*1.02,
			"repeated moderate reversals remain controllable at middle size");
	Controller loneObservation;
	loneObservation.Reset(0,0,0,StartKind::Hover,laptop);
	loneObservation.UpdatePosition(1000,0,0.5);
	expect(loneObservation.SweepEvidenceSeconds() == 0.0 &&
		loneObservation.Diameter() <= laptop.StandardDiameterPx()*1.001f,
		"single observation after missing interval cannot prove sustained fast activity");


	// 同一份DIP尺寸跨硬件、模式、DPI保持含义；固定旁路不读取速度或时间。
	for(const int dpi:{96,144,192})
	for(const auto mode:{DeviceMode::Laptop,DeviceMode::LargeScreen})
	for(const bool edid:{false,true})
	{
		DisplayScale s=physical;s.physicalAvailable=edid;s.dipPerPixelX=s.dipPerPixelY=96.0f/dpi;
		s.cmPerPixelX=edid?0.2f:0.0f;s.cmPerPixelY=edid?0.1f:0.0f;
		const auto cfg=ResolveConfig(s,mode,true);
		near(cfg.minimumDiameterPx*96/dpi,16,0.001,"minimum has invariant DIP meaning");
		near(cfg.StandardDiameterPx()*96/dpi,32,0.001,"standard has invariant DIP meaning");
		near(cfg.maximumDiameterPx*96/dpi,160,0.001,"maximum has invariant DIP meaning");
		near(FixedDiameterPx(42,s)*96/dpi,42,0.001,"fixed DIP bypass is independent of EDID and motion");
	}
	for(const double v:{100,300,500,650,750,800,1000,1300,1700,2200})
	{
		const auto scan=Replay(laptop,{{0,0},{20,v*20}},125,60,{10,20});
		if(v<=laptop.sweepEnterSpeed)
			for(const auto d:scan)near(d,laptop.StandardDiameterPx(),0.02,"ordinary speed never accumulates into sweep");
		else expect(scan.back()<=laptop.maximumDiameterPx+0.001f,"qualified velocity scan remains bounded");
	}
	Controller idleTool;idleTool.Reset(0,0,0,StartKind::Hover,laptop);
	FeedLine(idleTool,2600,1.5);
	ContactSizeState effective;effective.Reset(idleTool.Diameter(),1.5);
	const float historicalDiameter=idleTool.Diameter();
	const double idleEnd=2.7;
	// 没有任何输入包，仅调用与产品相同的有效尺寸状态更新。
	for(int frame=1;frame<=144;++frame)
	{
		const double t=1.5+frame/120.0;
		idleTool.Advance(t);
		effective.Update(idleTool.Diameter(),t,idleTool.SecondsSinceMovement(t)>=laptop.idleStartSeconds);
	}
	expect(effective.effectiveDiameterPx<=laptop.StandardDiameterPx()*1.1f &&
		effective.effectiveDiameterPx<historicalDiameter*0.5f,"no-event contact state visibly shrinks");
	idleTool.UpdatePosition(3904,4,idleEnd+0.01);
	const auto resume=effective.MakeInterval(1.5,idleEnd+0.01,historicalDiameter,
		idleTool.Diameter(),idleEnd+0.01,laptop);
	expect(resume.reanchor && resume.startDiameter<=laptop.StandardDiameterPx()*1.1f,
		"resumption interval starts at contracted tool size not historical radius");
	near(historicalDiameter,160,2.0,"historical wide diameter stays immutable");
	const auto late=effective.MakeInterval(1.4,1.6,historicalDiameter,historicalDiameter,1.6,laptop);
	expect(!late.reanchor,"late pre-shrink geometry is not silently re-timed");
	Controller samePosition=idleTool;
	for(int i=1;i<=1000;++i)samePosition.UpdatePosition(3904,4,2.71+i*0.001);
	expect(samePosition.DiameterDip()<=16.1f,"same position packets do not keep the tool large");
	Controller noisy;noisy.Reset(0,0,0,StartKind::Hover,laptop);FeedLine(noisy,2600,1.5);
	for(int i=1;i<=1400;++i)noisy.UpdatePosition(3900+(i%2?0.1f:-0.1f),0,1.5+i*0.001);
	Controller quietNoiseReference;quietNoiseReference.Reset(0,0,0,StartKind::Hover,laptop);
	FeedLine(quietNoiseReference,2600,1.5);quietNoiseReference.Advance(2.9);
	near(noisy.DiameterDip(),quietNoiseReference.DiameterDip(),0.03,
		"bounded sub-threshold noise has the same idle deadline as complete absence of packets");
	Controller ordinaryTouch;ordinaryTouch.Reset(0,0,0,StartKind::Touch,laptop);
	FeedLine(ordinaryTouch,100,1.0);
	expect(ordinaryTouch.DiameterDip()>30 && ordinaryTouch.DiameterDip()<=32.01f,
		"ordinary real touch motion reaches standard without sweep qualification");
	const WidthInterval interval{ 0,1,50,600,8,800 };
	near(InterpolateDiameter(interval,0.5),325,0.001,"scaled width interpolation exceeds old 200px maximum");
	near(InterpolateDiameter({0,1,4,8,4,8},0),4,0.001,"scaled minimum may be below old 20px limit");
	near(ContactDiameter(162.5f,16.0f),325,0.001,"contact cursor uses accepted geometric endpoint");
	near(ContactDiameter(0,24),24,0.001,"initial cursor uses batch minimum");
	// 新规格替代旧 standard 下限；分类、解析和几何仍调用实际产品逻辑。
#if defined(DRAW3_TESTING)
	expect(Inkeys::Drawing::Draw3::RtsSourceRoutingForTesting(),"RTS per-context source and real contact metadata");
#endif
	for(const auto kind:{SourceKind::Unknown,SourceKind::Mouse,SourceKind::ExternalPen,SourceKind::IntegratedPen,SourceKind::Touch,SourceKind::TouchPad})
	{
		const auto c=ResolveConfig(physical,DeviceMode::LargeScreen,MappedSource(kind,physical));
		const auto expected=kind==SourceKind::IntegratedPen?ResponseModel::ScreenPenHybrid:
			kind==SourceKind::Touch?ResponseModel::DirectTouch:ResponseModel::IndirectDip;
		expect(c.response==expected && c.sizes==EraserSizes{},"relationship chooses response without changing base sizes");
		if(expected==ResponseModel::IndirectDip)expect(c.motionUnit==MotionUnit::DipPerSecond && c.sweepEnterSpeed==800,
			"indirect input ignores EDID and large-screen action mode");
	}
	expect(ClassifySource(1,false,true,true)==SourceKind::Unknown,"Win7 does not assume integrated physical input");
	expect(ClassifySource(1,true,false,false)==SourceKind::Unknown,"failed query cannot identify a screen pen");
	expect(ClassifySource(0,true,false,false,SourceKind::TouchPad,true)==SourceKind::TouchPad,"touch pad is not screen touch");
	expect(ClassifySource(1,true,true,false,SourceKind::IntegratedPen,true)==SourceKind::Unknown,"conflicting source evidence is rejected");
	for(const auto response:{ResponseOverride::IndirectDip,ResponseOverride::ScreenPenHybrid,ResponseOverride::DirectTouch})
	{
		auto d=physical;d.development.response=response;
		const auto c=ResolveConfig(d,DeviceMode::Laptop,MappedSource(SourceKind::ExternalPen,d));
		expect(c.inputSource.kind==SourceKind::ExternalPen && c.sizes==EraserSizes{},"forced model keeps real external pen identity");
	}
	auto penDisplay=physical;penDisplay.cmPerPixelX=penDisplay.cmPerPixelY=0.025f;
	const auto penConfig=ResolveConfig(penDisplay,DeviceMode::Laptop,MappedSource(SourceKind::IntegratedPen,penDisplay));
	// 输出冻结模型的浮点位模式；本轮不靠重调鼠标或屏幕笔把 Touch 测试调绿。
	for(const auto kind:{SourceKind::Mouse,SourceKind::IntegratedPen})
	for(int dpi:{96,144,192})for(const auto mode:{DeviceMode::Laptop,DeviceMode::LargeScreen})
	{
		auto d=penDisplay;d.dipPerPixelX=d.dipPerPixelY=96.0f/dpi;
		const auto c=ResolveConfig(d,mode,MappedSource(kind,d));const double fast=c.largeTargetSpeed*1.25;
		const auto values=Replay(c,{{0,0},{0.4,c.fineToStandardSpeed*0.1},{0.8,c.sweepEnterSpeed*0.4},
			{1.5,fast},{1.62,fast},{2.3,0},{4,1}},125,60,{0.1,0.4,0.8,1,1.5,1.62,1.8,2.5,4});
		std::cout << "[R7Frozen] " << static_cast<int>(kind) << " " << dpi << " " << static_cast<int>(mode);
		for(float v:values)std::cout << " " << std::bit_cast<uint32_t>(v);
		Controller preview;preview.ResetPreview(0,0,0,c);FeedLine(preview,fast,0.5,125,c);preview.Advance(2);
		Controller contact;contact.BeginContact(&preview,static_cast<float>(fast*0.5/c.motionPerPixelX),0,2,c);
		std::cout << " " << std::bit_cast<uint32_t>(preview.Diameter()) << " " << std::bit_cast<uint32_t>(contact.Diameter());
		MouseLifecycle mouse;mouse.Configure(c);mouse.ObserveHover(0,0,0);mouse.Advance(2);
		mouse.BeginContact(contact,0,0,2,c);mouse.EndContact(contact,c.maximumDiameterPx,0,0,2.1,false);
		for(double t:{2.1,2.17,2.24,3.0})std::cout << " " << std::bit_cast<uint32_t>(mouse.Advance(t));
		std::cout << '\n';
	}
	for(float rho:{0.125f,0.25f,0.5f})
	{
		auto d=penDisplay;d.cmPerPixelX=d.cmPerPixelY=rho/10;
		const auto c=ResolveConfig(d,DeviceMode::Laptop,MappedSource(SourceKind::Touch,d));
		near(CompensateTargetDiameterDip(c,100),100,0.0001,"R7 touch dynamic target is DIP, independent of physical density");
		std::vector<Knot> local{{0,0}};for(int i=1;i<=8;++i)local.push_back({i*0.2,i%2?36.0:0.0});
		const auto values=Replay(c,local,125,60,{0.8,1.6},StartKind::Touch);
		expect(values[0]>64 && values[1]>64,"R7 36mm local touch strokes at 180mm/s enter usable sweep");
	}
	near(penConfig.rhoMmPerDip,0.25,0.00001,"physical axes resolve millimeters per DIP");
	expect(penConfig.motionUnit==MotionUnit::MillimetersPerSecond && penConfig.sweepEnterSpeed==120 &&
		penConfig.largeTargetSpeed==350,"screen pen uses explicit physical thresholds");
	for(float rho:{0.125f,0.25f,0.5f})for(float beta:{0.0f,0.25f,0.5f,0.75f,1.0f})
	{
		auto c=penConfig;c.rhoMmPerDip=rho;c.penBeta=beta;
		near(CompensateTargetDiameterDip(c,24),24,0.0001,"physical compensation leaves fine band unchanged");
		near(CompensateTargetDiameterDip(c,72),std::clamp(32.0+std::pow(0.25/rho,beta)*40,16.0,160.0),0.0001,
			"pen beta compensates only standard-above increment");
		expect(c.sizes==EraserSizes{},"beta does not change base or fixed attributes");
	}
	const auto onsetPen=Replay(penConfig,{{0,0},{1,350}},1000,240,{0.12,0.3,0.8});
	expect(onsetPen[0]>32 && onsetPen[0]<64,"screen pen begins light growth near 120ms without jumping large");
	expect(onsetPen[1]>onsetPen[0] && onsetPen[2]>100,"physical pen sweep is accessible");
	for(float speed:{20.0f,60.0f,100.0f})for(float d:Replay(penConfig,{{0,0},{20,20*speed}},125,60,{10,20}))
		near(d,32,0.01,"ordinary screen pen stays standard for twenty seconds");
	for(const auto c:{laptop,penConfig,physicalTouch})
	{
		const double v=c.largeTargetSpeed*1.25;
		const auto trace=Replay(c,{{0,0},{1,v},{4,v+3*c.fineToStandardSpeed*0.02}},1000,144,{1,1.12,4},
			c.response==ResponseModel::DirectTouch?StartKind::Touch:StartKind::Hover);
		expect(trace[1]>=trace[0]*0.9f,"all models protect short reversals");
		expect(trace[2]<=c.minimumDiameterPx*1.05f,"mouse pen and unlocked touch all return fine");
		Controller preview;preview.ResetPreview(0,0,0,c);FeedLine(preview,c.largeTargetSpeed*2,1,1000,c);
		expect(preview.Diameter()<=c.StandardDiameterPx()+0.001f && preview.SweepEvidenceSeconds()==0,"preview cannot exceed standard or precharge sweep");
		preview.Advance(3);Controller down;
		down.BeginContact(&preview,static_cast<float>(c.largeTargetSpeed*2/c.motionPerPixelX),0,3,c);
		near(down.Diameter(),c.minimumDiameterPx,0.001,"fine preview Down has no size jump");
		near(down.SweepEvidenceSeconds(),0,0.000001,"preview Down discards movement history");
	}
	for(int dpi:{96,144,192})
	{
		auto d=penDisplay;d.dipPerPixelX=d.dipPerPixelY=96.0f/dpi;
		const auto c=ResolveConfig(d,DeviceMode::LargeScreen,MappedSource(SourceKind::IntegratedPen,d));
		for(double v:{c.sweepEnterSpeed*0.98,c.sweepEnterSpeed*1.02,c.sweepEnterSpeed*1.3})
		{
			const std::vector<Knot> path{{0,0},{2,v*2}};const std::vector<double> times{0.12,0.25,0.5,1,2};
			const auto reference=Replay(c,path,1000,240,times);
			for(int hz:{60,125,240,1000})for(int frames:{30,60,144,240})
			{
				const auto values=Replay(c,path,hz,frames,times);
				for(size_t i=0;i<times.size();++i)
				{
					const double error=std::abs(values[i]-reference[i])/reference[i];
					worstRateError=std::max(worstRateError,error);
					expect(error<=0.05,"near-threshold physical pen rate invariance");
				}
			}
		}
	}
	auto h=penDisplay;h.physicalAvailable=false;h.pixelWidth=1920;h.pixelHeight=1080;
	const auto h1=ResolveConfig(h,DeviceMode::LargeScreen,MappedSource(SourceKind::IntegratedPen,h));
	auto twice=h;twice.pixelWidth*=2;twice.pixelHeight*=2;
	const auto h2=ResolveConfig(twice,DeviceMode::LargeScreen,MappedSource(SourceKind::IntegratedPen,twice));
	expect(h1.motionSource==ScaleSource::ResolutionDpiHeuristic && h1.rhoMmPerDip==0 && !h1.display.physicalAvailable,"heuristic never becomes physical measurement");
	near(h1.motionPerPixelX,h2.motionPerPixelX*2,0.00001,"resolution doubling preserves heuristic action");
	twice.dipPerPixelX=twice.dipPerPixelY=0.5f;
	const auto h3=ResolveConfig(twice,DeviceMode::LargeScreen,MappedSource(SourceKind::IntegratedPen,twice));
	near(h1.motionPerPixelX,h3.motionPerPixelX*2,0.00001,"DPI scaling applied once");
	std::swap(twice.pixelWidth,twice.pixelHeight);twice.orientation=1;
	near(ResolutionDpiActionGain(twice),1,0.00001,"rotation preserves heuristic extent");
	h.logicalOutputKnown=false;
	expect(ResolveConfig(h,DeviceMode::LargeScreen,MappedSource(SourceKind::IntegratedPen,h)).motionSource==ScaleSource::DipOnly,"unknown logical output uses pen DIP fallback");
	auto manual=penDisplay;manual.development.scale=ScaleOverride::ManualSurface;
	manual.development.calibration={manual.monitor,28,18,0};
	const auto calibrated=ResolveConfig(manual,DeviceMode::Laptop,MappedSource(SourceKind::IntegratedPen,manual));
	expect(calibrated.motionSource==ScaleSource::ManualCalibration && calibrated.sizes==EraserSizes{},"manual surface overrides EDID without changing properties");
	near(calibrated.motionPerPixelX,280.0/manual.pixelWidth,0.00001,"manual calibration uses measured width");
	manual.orientation=1;std::swap(manual.pixelWidth,manual.pixelHeight);
	const auto turned=ResolveConfig(manual,DeviceMode::Laptop,MappedSource(SourceKind::IntegratedPen,manual));
	near(turned.motionPerPixelX,calibrated.motionPerPixelY,0.00001,"manual calibration follows rotation once");
	manual.monitor=1234;
	expect(ResolveConfig(manual,DeviceMode::Laptop,MappedSource(SourceKind::IntegratedPen,manual)).motionSource==ScaleSource::DipOnly,"manual surface cannot leak to another monitor");
	for(float rho:{0.25f,0.5f}){auto c=physicalTouch;c.rhoMmPerDip=rho;
		near(CompensateTargetDiameterDip(c,100),100,0.0001,"touch target stays DIP across physical densities");}
	bool clamped=false;auto extreme=penConfig;extreme.rhoMmPerDip=0.01f;
	near(CompensateTargetDiameterDip(extreme,160,&clamped),160,0.001,"extreme physical target is centrally bounded");
	expect(clamped,"target clamping is available for diagnostics");

	// 面积辅助必须走同一个产品控制器；坏数据和开关关闭均不改变速度模型。
	expect(ContactMetricsMatch(2,1000,2,1000),"RTS width and coordinate metrics agree");
	expect(!ContactMetricsMatch(0,1,0,1) && !ContactMetricsMatch(2,1000,2,500) &&
		!ContactMetricsMatch(3,1000,3,1000) && !ContactMetricsMatch(2,NAN,2,1000),"unknown or mismatched packet units are not pixels");
	auto areaDisplay=penDisplay;areaDisplay.development.touchContactAreaAssistance=true;
	const auto areaConfig=ResolveConfig(areaDisplay,DeviceMode::Laptop,MappedSource(SourceKind::Touch,areaDisplay));
	const auto normalArea=AreaSample(areaConfig,30,20);
	const std::vector<Knot> slowDrag{{0,0},{2,20}};
	const std::vector<double> areaTimes{0.3,0.5,1.0,2.0,3.0,4.5,6.0};
	const auto areaReference=ReplayArea(areaConfig,slowDrag,1000,120,areaTimes,[&](double){return normalArea;},2.0);
	expect(areaReference[2].diameter>38 && areaReference[2].diameter<40 && areaReference[2].area.active,
		"ordinary slow touch drag opens bounded area floor without sweep speed");
	near(areaReference[3].area.referenceFloorDip,39,0.001,"max-axis span produces independent bounded floor");
	expect(!areaReference[4].animating && areaReference[4].wake>3.0 &&
		areaReference[4].diameter>38,"held touch rests at its area floor and schedules expiry without spinning");
	near(areaReference.back().diameter,16,0.001,"missing area eventually releases to minimum");
	expect(!areaReference.back().animating,"expired area cannot keep animation awake forever");
	double worstAreaRate=0;
	for(int hz:{60,125,240,1000})for(int fps:{30,60,120})
	{
		const auto values=ReplayArea(areaConfig,slowDrag,hz,fps,areaTimes,[&](double){return normalArea;},2.0);
		for(size_t i=0;i<values.size();++i)
		{
			const double error=std::abs(values[i].diameter-areaReference[i].diameter)/areaReference[i].diameter;
			worstAreaRate=std::max(worstAreaRate,error);
			expect(error<=0.05,"area confirmation/filter/follow are time based within five percent");
		}
	}
	std::cout << "[R7AreaRate] " << worstAreaRate*100 << "%\n";
	for(const auto bad:std::vector<ContactAreaSample>{
		{}, {0,200,0,20,ContactAreaUnits::CanvasPixels}, {-10,200,-1,20,ContactAreaUnits::CanvasPixels},
		{100000,100000,1000,1000,ContactAreaUnits::CanvasPixels}, {300,20,30,2,ContactAreaUnits::CanvasPixels},
		{300,200,NAN,20,ContactAreaUnits::CanvasPixels}, {300,200,30,20,ContactAreaUnits::Unverified},
		{300,200,30,20,ContactAreaUnits::OutsideMetrics}})
	{
		auto off=areaConfig;off.touchContactAreaAssistance=false;
		const auto enabled=ReplayArea(areaConfig,slowDrag,125,60,{0.5,1,2},[&](double){return bad;});
		const auto disabled=ReplayArea(off,slowDrag,125,60,{0.5,1,2},[&](double){return bad;});
		for(size_t i=0;i<enabled.size();++i)
		{
			near(enabled[i].diameter,disabled[i].diameter,0.001,"bad contact width never becomes maximum assistance");
			expect(!enabled[i].area.active && !enabled[i].area.referenceReady,"invalid area does not become a reference");
		}
	}
	for(const auto kind:{SourceKind::Mouse,SourceKind::ExternalPen,SourceKind::IntegratedPen,SourceKind::TouchPad,SourceKind::Unknown})
	{
		auto d=areaDisplay;d.development.response=ResponseOverride::DirectTouch;
		const auto c=ResolveConfig(d,DeviceMode::Laptop,MappedSource(kind,d));
		const auto result=ReplayArea(c,slowDrag,125,60,{1,2},[&](double){return normalArea;},2,StartKind::Hover);
		for(const auto& value:result)expect(!value.area.active,"forced Touch model cannot invent real Touch area");
		near(FixedDiameterPx(50,d),50,0.001,"fixed eraser bypasses area and speed");
	}
	auto unmapped=areaConfig;unmapped.inputMapped=false;
	const auto rejectedMapping=ReplayArea(unmapped,slowDrag,125,60,{1},[&](double){return normalArea;});
	expect(!rejectedMapping[0].area.active && rejectedMapping[0].area.reason==ContactAreaReason::MappingUnknown,
		"unknown area mapping leaves otherwise valid touch input usable");
	const auto press=ReplayArea(areaConfig,{{0,0},{3,0}},1000,120,{0.01,0.5,2,3},
		[&](double t){return AreaSample(areaConfig,t<0.5?30:70,t<0.5?20:50);});
	for(const auto& value:press)near(value.diameter,16,0.001,"stationary press and later larger area cannot open a hole");
	std::vector<Knot> jitter{{0,0}};for(int i=1;i<=200;++i)jitter.push_back({i*0.01,i%2?0.4:-0.4});
	for(const auto& value:ReplayArea(areaConfig,jitter,1000,120,{0.5,1,2},[&](double){return normalArea;}))
		expect(value.diameter==16 && !value.area.active && !value.area.referenceReady,"landing jitter cannot establish dragging area");
	const auto locked=ReplayArea(areaConfig,{{0,0},{1,10},{3,10}},125,60,{1,1.5,2.5},
		[&](double t){return AreaSample(areaConfig,t<1?30:40,t<1?20:28);});
	for(const auto& value:locked)
	{
		near(value.area.referenceFloorDip,39,0.001,"confirmed reference does not breathe with contact size");
		expect(value.diameter<=39.01f,"same-position area increase does not enlarge accepted geometry");
	}
	const auto outlier=ReplayArea(areaConfig,{{0,0},{1,10},{3,10}},125,60,{1,1.1,2.8},
		[&](double t){return AreaSample(areaConfig,t<=1?30:70,t<=1?20:50);});
	expect(outlier[1].area.reason==ContactAreaReason::Outlier && !outlier[1].area.sampleValid,
		"large change inside reported range is rejected, not clamped");
	near(outlier.back().diameter,16,0.1,"persistent outlier releases old reference");
	for(const int dpi:{96,144,192})
	{
		auto d=areaDisplay;d.dipPerPixelX=d.dipPerPixelY=96.0f/dpi;
		const auto c=ResolveConfig(d,DeviceMode::Laptop,MappedSource(SourceKind::Touch,d));
		const auto sample=AreaSample(c,30,20);
		const auto values=ReplayArea(c,slowDrag,125,60,{1.5},[&](double){return sample;});
		near(values[0].area.referenceFloorDip,39,0.001,"per-axis pixel to DIP conversion occurs once");
	}
	auto anisotropic=areaDisplay;anisotropic.dipPerPixelX=0.5f;anisotropic.dipPerPixelY=0.75f;
	const auto anisConfig=ResolveConfig(anisotropic,DeviceMode::Laptop,MappedSource(SourceKind::Touch,anisotropic));
	const auto anSample=AreaSample(anisConfig,30,20);
	const auto an=ReplayArea(anisConfig,slowDrag,125,60,{1.5},[&](double){return anSample;});
	std::swap(anisotropic.dipPerPixelX,anisotropic.dipPerPixelY);std::swap(anisotropic.cmPerPixelX,anisotropic.cmPerPixelY);
	const auto swapConfig=ResolveConfig(anisotropic,DeviceMode::Laptop,MappedSource(SourceKind::Touch,anisotropic));
	const auto swapSample=AreaSample(swapConfig,20,30);
	const auto swapped=ReplayArea(swapConfig,slowDrag,125,60,{1.5},[&](double){return swapSample;});
	near(an[0].area.referenceFloorDip,swapped[0].area.referenceFloorDip,0.001,"rotation swaps axes without changing max-axis assistance");
	for(const auto sizes:{EraserSizes{16,80,100,16,50},EraserSizes{16,32,24,16,50}})
	{
		const auto c=ResolveConfig(areaDisplay,DeviceMode::Laptop,MappedSource(SourceKind::Touch,areaDisplay),sizes);
		const auto result=ReplayArea(c,slowDrag,125,60,{1.5},[&](double){return normalArea;});
		expect(result[0].area.referenceFloorDip>=c.sizes.standardDiameterDip &&
			result[0].area.referenceFloorDip<=c.sizes.maximumDiameterDip,"custom standard/maximum cannot reverse assistance clamp bounds");
	}
	Controller smallFloor;
	const auto smallArea=AreaSample(areaConfig,10,10);
	smallFloor.Reset(0,0,0,StartKind::Touch,areaConfig,0,&smallArea);
	for(int i=1;i<=250;++i)smallFloor.UpdatePosition(static_cast<float>(i*0.08/areaConfig.motionPerPixelX),0,i/125.0,&smallArea);
	smallFloor.Advance(6.0);
	smallFloor.UpdatePosition(static_cast<float>(20.5/areaConfig.motionPerPixelX),0,6.1,&smallArea);
	const float resumedSmall=smallFloor.DiameterDip();smallFloor.Advance(6.2);
	near(smallFloor.DiameterDip(),resumedSmall,0.001,"renewed standard-sized area cannot enlarge a cursor without another actual segment");

	Controller firstFinger,secondFinger;
	const auto largerArea=AreaSample(areaConfig,40,30);
	firstFinger.Reset(0,0,0,StartKind::Touch,areaConfig,0,&normalArea);
	secondFinger.Reset(0,0,0,StartKind::Touch,areaConfig,0,&largerArea);
	for(int i=1;i<=250;++i)
	{
		const double t=i/125.0;const float px=static_cast<float>(t*10/areaConfig.motionPerPixelX);
		firstFinger.UpdatePosition(px,0,t,&normalArea);secondFinger.UpdatePosition(px,0,t,&largerArea);
	}
	near(firstFinger.AreaDiagnostics(2).referenceFloorDip,39,0.001,"first finger retains its reference");
	near(secondFinger.AreaDiagnostics(2).referenceFloorDip,50,0.001,"second finger owns an independent reference");
	const float beforeGap=firstFinger.Diameter();firstFinger.PauseForReconnect(2);
	near(firstFinger.Advance(100),beforeGap,0.001,"reconnect wait freezes area and diameter clocks");
	near(firstFinger.ResumeFromReconnect(100000,0,3),beforeGap,0.001,"synthetic bridge does not enable larger assistance");
	const auto zero=AreaSample(areaConfig,0,0);
	firstFinger.UpdatePosition(100000,0,3.01,&zero,true);
	near(firstFinger.AreaDiagnostics(3.01).referenceFloorDip,39,0.001,"terminal zero size cannot replace touch reference");
	for(int tap=0;tap<5;++tap)
	{
		firstFinger.Reset(0,0,4+tap,StartKind::Touch,areaConfig,0,&normalArea);
		near(firstFinger.Advance(4.5+tap),16,0.001,"each real touch tap starts small without previous reference");
	}
	{
		using namespace Inkeys::Drawing::Draw3;
		ContactInputCoordinator coordinator;ContactSnapshot raw;raw.position={1,2};raw.qpc=10;
		raw.rawContactSize={300,200};raw.contactSize={30,20};raw.contactAreaUnits=ContactAreaUnits::CanvasPixels;
		expect(coordinator.PublishDown(7,1,InputDeviceType::Touch,raw),"publish actual contact dimensions");
		ContactRecord* record=nullptr;expect(coordinator.TryDequeue(record) && record,"dequeue area contact");
		if(record)
		{
			const ContactHandle handle{record,record->Generation()};
			raw.qpc=20;raw.rawContactSize={400,250};raw.contactSize={40,25};raw.contactAreaUnits=ContactAreaUnits::Unverified;
			coordinator.PublishMove(7,1,raw);ContactSnapshot latest;
			expect(coordinator.TryReadSnapshot(handle,latest) && latest.rawContactSize.width==400 &&
				latest.contactSize.height==25 && latest.contactAreaUnits==ContactAreaUnits::Unverified,"raw/pixel/unit state is one actual mailbox snapshot");
			coordinator.PublishUp(7,1,raw);coordinator.Recycle(handle);
		}
	}
	// 记录真实到达时间，不把 tau 当作端到端响应时间；不可达尺寸输出 -1。
	auto touchPlainDisplay=penDisplay;
	const auto touchPlain=ResolveConfig(touchPlainDisplay,DeviceMode::Laptop,MappedSource(SourceKind::Touch,touchPlainDisplay));
	for(double velocity:{10,30,60,90,120,180,250,350})
	{
		Controller c;c.Reset(0,0,0,StartKind::Touch,touchPlain);
		double t32=-1,t64=-1,t96=-1,permission=-1;
		for(int i=1;i<=2500;++i)
		{
			const double t=i/1000.0;c.UpdatePosition(static_cast<float>(velocity*t/touchPlain.motionPerPixelX),0,t);
			const float d=c.DiameterDip();
			if(t32<0 && d>=32)t32=t;if(t64<0 && d>=64)t64=t;if(t96<0 && d>=96)t96=t;
			if(permission<0 && c.SweepEvidenceSeconds()>=touchPlain.evidenceStartSeconds && c.TargetDiameterDip()>32)permission=t;
		}
		std::cout << "[R7Touch] speed=" << velocity << " mm/s target=" << c.TargetDiameterDip()
			<< " final=" << c.DiameterDip() << " permission=" << permission << " t32=" << t32 << " t64=" << t64 << " t96=" << t96 << '\n';
		if(velocity>=250)expect(permission>=0 && permission<=0.14 && t64>0 && t64<0.5,"clear Touch sweep gains permission and grows promptly");
		if(velocity<=90)expect(c.DiameterDip()<=32.01f,"ordinary touch cannot accumulate into sweep solely with time");
	}
	std::vector<Knot> curved{{0,0}};
	for(int i=1;i<=1600;++i)
	{
		const double t=i/1000.0;curved.push_back({t,20.0*(1.0-std::cos(t*3.141592653589793/0.25))});
	}
	const auto rounded=Replay(touchPlain,curved,125,60,{0.6,1.0,1.4},StartKind::Touch);
	for(float d:rounded)expect(d>64,"40mm smooth-decelerating local reversals maintain useful Touch coverage");
	const auto shortTap=Replay(touchPlain,{{0,0},{0.02,3},{1,3}},125,60,{0.02,0.1,1},StartKind::Touch);
	for(float d:shortTap)expect(d<=32.0f,"brief small touch stroke cannot turn into large clearing");
	std::cout << "[SpeedEraser] worst sample/frame deviation=" << worstRateError * 100.0
		<< "% idle drop at 500ms=" << worstPauseDrop * 100.0 << "% failures=" << failures << '\n';
	return failures;
}
