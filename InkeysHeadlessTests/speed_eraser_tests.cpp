#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <filesystem>
#include <fstream>
#include <string>
#include "../Inkeys/Inkeys/Drawing/Draw3/Draw3.SpeedEraser.h"
#include "../Inkeys/Inkeys/Drawing/Draw3/Draw3.Bridge.h"

#include <algorithm>
#include <bit>
#include <array>
#include <cmath>
#include <iostream>
#include <limits>
#include <vector>

import Inkeys.Drawing.Draw3.contact_input;
import Inkeys.Other.Config;
extern std::wstring globalPath;

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

namespace
{

	struct FineTraceStats
	{
		float minimum=10000,maximum=0,finalDiameter=0;
		double enterTime=-1,recoverTime=-1,settleTime=-1,maximumStep=0;
		int heldExits=0;
		bool sleeping=false;
	};
	template<class C> int FineHeldState(const C& c)
	{
		if constexpr(requires { c.FineDiagnostics().held; })return c.FineDiagnostics().held?1:0;
		return -1; // 红灯基线没有新诊断接口，不能伪称观察到了其内部状态。
	}
	template<class C> void CheckFineDiagnostic(const C& c, bool& valid, double& progress)
	{
		if constexpr(requires { c.FineDiagnostics().releaseProgress; })
		{
			const auto d=c.FineDiagnostics();
			valid=std::isfinite(d.speed) && d.enterProgress>=0 && d.enterProgress<=1 &&
				d.releaseProgress>=0 && d.releaseProgress<=1;
			progress=d.releaseProgress;
		}
		else {valid=false;progress=-1;}
	}
	void PrimeFine(Controller& c,const Config& config,bool hover)
	{
		if(config.response==ResponseModel::DirectTouch)
		{
			c.Reset(-4*config.touchUnlockEnd/config.motionPerPixelX,0,0,StartKind::Touch,config);
			c.UpdatePosition(0,0,0.2); // 真实位移解除起步，不能只用时间解锁。
		}
		else if(hover)c.ResetPreview(0,0,0,config);
		else c.Reset(0,0,0,StartKind::Hover,config);
		c.UpdatePosition(0,0,3);
	}
	template<class Path>
	FineTraceStats ReplayFine(const Config& config,Path path,int hz,int fps,double phase,
		double grid,bool sparse,bool hover,double duration=2.0)
	{
		Controller c;PrimeFine(c,config,hover);
		FineTraceStats result;
		double input=(1.0+phase)/hz,frame=1.0/fps;
		float previousX=0,previousY=0,previousSize=c.DiameterDip();
		int held=FineHeldState(c);
		const auto record=[&](double seconds)
		{
			const float size=c.DiameterDip();
			result.minimum=std::min(result.minimum,size);result.maximum=std::max(result.maximum,size);
			result.maximumStep=std::max(result.maximumStep,static_cast<double>(std::abs(size-previousSize)));
			if(result.recoverTime<0 && size>=config.sizes.standardDiameterDip-0.5f)result.recoverTime=seconds;
			const int currentHeld=FineHeldState(c);
			if(held==1 && currentHeld==0)++result.heldExits;
			if(currentHeld>=0)held=currentHeld;
			previousSize=size;
		};
		while(std::min(input,frame)<=duration+1e-9)
		{
			if(input<=frame)
			{
				const auto point=path(input);
				float px=static_cast<float>(point.x/config.motionPerPixelX),py=static_cast<float>(point.y/config.motionPerPixelY);
				if(grid>0)
				{
					px=static_cast<float>(grid*(std::floor(px/grid+phase)-std::floor(phase)));
					py=static_cast<float>(grid*(std::floor(py/grid+1-phase)-std::floor(1-phase)));
				}
				if(!sparse || px!=previousX || py!=previousY)c.UpdatePosition(px,py,3+input);
				previousX=px;previousY=py;
				record(input);input+=1.0/hz;
			}
			else {c.Advance(3+frame);record(frame);frame+=1.0/fps;}
		}
		result.finalDiameter=c.Advance(3+duration);
		c.Advance(3+duration+3);result.sleeping=!c.NeedsAnimation(3+duration+3);
		return result;
	}

	int RunFineBandRegressions()
	{
		int failures=0;
		const auto check=[&](bool condition,const char* name)
		{
			if(!condition){if(failures<35)std::cerr<<"[FineBand] failed: "<<name<<'\n';++failures;}
		};
		std::vector<Config> configs;
		for(const auto kind:{SourceKind::Mouse,SourceKind::IntegratedPen,SourceKind::Touch})
		for(bool physical:{false,true})
		{
			DisplayScale d;d.monitor=17;d.logicalOutputKnown=true;d.pixelWidth=2880;d.pixelHeight=1920;
			d.physicalAvailable=physical;d.cmPerPixelX=d.cmPerPixelY=0.025f;
			configs.push_back(ResolveConfig(d,DeviceMode::Laptop,MappedSource(kind,d)));
		}
		double worstBand=0,worstEnter=0,worstRecovery=0;int totalExits=0;size_t quantizedCases=0;
		for(const auto& config:configs)
		{
			const float minimum=config.sizes.minimumDiameterDip,standard=config.sizes.standardDiameterDip;
			for(double ratio:{0.0,0.05,0.10,0.15,0.20})
			{
				const double speed=config.fineToStandardSpeed*ratio;
				check(std::abs(ReferenceTargetDiameterDip(config,speed)-minimum)<0.001,
					"finite low-speed plateau includes zero through 20 percent");
				for(bool startMinimum:{false,true})
				{
					Controller c;c.Reset(0,0,0,StartKind::Hover,config,startMinimum?minimum:standard);
					double entered=-1;
					for(int i=1;i<=250;++i)
					{
						const double time=i/125.0;c.UpdatePosition(static_cast<float>(speed*time/config.motionPerPixelX),0,time);
						if(entered<0 && c.DiameterDip()<=minimum+0.5f)entered=time;
					}
					worstEnter=std::max(worstEnter,entered);
					check(std::abs(c.DiameterDip()-minimum)<0.001,"steady plateau reaches the exact minimum from either initial size");
					check(entered>=0 && entered<1.6,"fine entry converges in bounded real time");
				}
			}
			Controller fine;PrimeFine(fine,config,false);
			float pos=0;
			for(int i=1;i<=625;++i)
			{
				const double time=i/125.0;
				// 迟滞带内的微动不是退出证据，不能越擦越容易解除。
				pos=static_cast<float>(config.fineToStandardSpeed*0.30*time/config.motionPerPixelX);
				fine.UpdatePosition(pos,0,3+time);
			}
			check(fine.DiameterDip()<=minimum+0.5f,"five seconds of weak release intent does not accumulate into unlock");
			check(FineHeldState(fine)!=0,"hysteresis interval keeps an established fine hold");
			fine.UpdatePosition(pos+1,0,8.008);
			for(int i=1;i<=30;++i)fine.Advance(8.008+i*0.008);
			check(fine.DiameterDip()<=minimum+0.5f,"single one-pixel step does not release fine hold");
			PrimeFine(fine,config,false);
			double released=-1,recovered=-1,last=minimum,largestStep=0;
			for(int i=1;i<=250;++i)
			{
				const double time=i/125.0;fine.UpdatePosition(static_cast<float>(config.fineToStandardSpeed*1.25*time/config.motionPerPixelX),0,3+time);
				if(released<0 && FineHeldState(fine)==0)released=time;
				if(recovered<0 && fine.DiameterDip()>=standard-0.5f)recovered=time;
				largestStep=std::max(largestStep,std::abs(fine.DiameterDip()-last));last=fine.DiameterDip();
			}
			worstRecovery=std::max(worstRecovery,recovered);
			check(recovered>0.2 && recovered<1.7,"sustained ordinary movement exits fine without a step or an excessive wait");
			check(largestStep<1.5,"fine recovery remains continuous at 125 Hz");
			bool diagnosticValid=false;double progress=0;CheckFineDiagnostic(fine,diagnosticValid,progress);
			check(diagnosticValid,"fine diagnostic progress has finite bounded values");
			std::cout<<"[FineLatency] model="<<ResponseModelName(config.response)<<" unit="<<MotionUnitName(config.motionUnit)
				<<" release="<<released<<" standard-minus-0.5="<<recovered<<'\n';

			// 清扫许可独立：取得原资格后不能再串联低区退出确认。
			PrimeFine(fine,config,false);double permission=-1,growth=-1;
			for(int i=1;i<=180;++i)
			{
				const double time=i/1000.0;
				fine.UpdatePosition(static_cast<float>(config.largeTargetSpeed*1.5*time/config.motionPerPixelX),0,3+time);
				if(permission<0 && fine.SweepEvidenceSeconds()>=config.evidenceStartSeconds)permission=time;
				if(permission>=0 && growth<0 && fine.DiameterDip()>minimum+0.05f)growth=time;
			}
			check(permission>=0 && growth>=permission && growth-permission<0.020,
				"qualified high sweep bypasses low-band confirmation");

			// 复制、帧先行、重连都只继承同一真实历史，不让未来帧写回输入。
			PrimeFine(fine,config,false);Controller late=fine,timely=fine;
			late.Advance(3.1);
			late.UpdatePosition(0.5f,0,3.02);timely.UpdatePosition(0.5f,0,3.02);
			check(std::abs(late.DiameterDip()-timely.DiameterDip())<0.0001f,"fine frame preview does not contaminate delayed raw input");
			fine.PauseForReconnect(3.1);const float heldSize=fine.DiameterDip();
			fine.Advance(50);fine.ResumeFromReconnect(10000,20000,4.1);fine.UpdatePosition(10001,20000,4.12);
			check(std::abs(fine.DiameterDip()-heldSize)<0.5f,"reconnect translates clocks without bridge movement or renewed fine growth");
			Controller preview;PrimeFine(preview,config,true);
			if(config.response!=ResponseModel::DirectTouch)
			{
				Controller down;down.BeginContact(&preview,0,0,3.05,config);
				check(std::abs(down.DiameterDip()-minimum)<0.001f && down.SweepEvidenceSeconds()==0,
					"fine Hover to Down inherits only compatible safe size, not sweep momentum");
			}
			Controller high;high.Reset(0,0,0,StartKind::Hover,config);
			const double velocity=config.largeTargetSpeed*1.5;
			for(int i=1;i<=2500;++i)high.UpdatePosition(static_cast<float>(velocity*i/1000/config.motionPerPixelX),0,i/1000.0);
			check(high.DiameterDip()>standard*2,"high-zone comparison begins from an established large state");
			std::cout<<"[FineHighBaseline] "<<ResponseModelName(config.response)<<" "<<MotionUnitName(config.motionUnit);
			double highPos=velocity*2.5;
			for(int i=1;i<=600;++i)
			{
				const double time=i/1000.0;
				const double v=i<=100?velocity:i<=200?0:i<=400?-velocity:velocity;
				highPos+=v/1000;
				high.UpdatePosition(static_cast<float>(highPos/config.motionPerPixelX),0,2.5+time);
				if(i%50==0)std::cout<<" "<<std::bit_cast<uint32_t>(high.DiameterDip());
			}
			std::cout<<'\n';
		}

		// 采用交错覆盖而非固定一个理想浮点输入：轴、量化、DPI、频率、相位和稀疏事件。
		for(const auto& original:configs)for(int dpi:{96,144,192,288})for(int hz:{60,125,240,1000})
		for(double grid:{0.5,1.0,2.0})for(int motion=0;motion<3;++motion)for(bool sparse:{false,true})
		{
			auto display=original.display;display.dipPerPixelX=display.dipPerPixelY=96.0f/dpi;
			display.cmPerPixelX=display.cmPerPixelY=0.025f*96.0f/dpi;
			auto config=ResolveConfig(display,original.mode,MappedSource(original.inputSource.kind,display));
			const double speed=config.fineToStandardSpeed*0.15;
			const double phase=(motion+static_cast<int>(grid*2)+dpi/48)%2?0.25:0.75;
			const int fps=motion==0?30:motion==1?60:144;
			const auto path=[&](double time)
			{
				if(motion==0)return Knot{time,speed*time,0};
				if(motion==1)return Knot{time,speed*time/std::sqrt(2.0),speed*time/std::sqrt(2.0)};
				const double period=0.18,phaseTime=std::fmod(time,period*2);
				return Knot{time,speed*(phaseTime<=period?phaseTime:2*period-phaseTime),0};
			};
			const auto trace=ReplayFine(config,path,hz,fps,phase,grid,sparse,motion==0);
			worstBand=std::max(worstBand,static_cast<double>(trace.maximum-trace.minimum));
			totalExits+=trace.heldExits;++quantizedCases;
			check(trace.maximum-trace.minimum<=0.5f && trace.maximum<=config.sizes.minimumDiameterDip+0.5f,
				"quantized or sparse plateau stays within 0.5 DIP without periodic thickening");
			check(trace.heldExits==0 && trace.sleeping,"fine plateau does not unlock and true idle stops animation");
		}
		for(const auto& config:configs)for(int moveMs:{60,100})for(int stopMs:{40,120})
		{
			const double cycle=(moveMs+stopMs)/1000.0,moving=moveMs/1000.0,speed=config.fineToStandardSpeed*0.18;
			const auto path=[&](double time){return Knot{time,speed*(std::floor(time/cycle)*moving+std::min(moving,std::fmod(time,cycle))),0};};
			const auto trace=ReplayFine(config,path,125,60,0.3,1,true,false,3);
			check(trace.maximum<=config.sizes.minimumDiameterDip+0.5f && trace.heldExits==0,
				"alternating slow movement and no-Move pauses keep fine intent");
		}
		std::cout<<"[FineQuantized] cases="<<quantizedCases<<" worstPeakToPeakDIP="<<worstBand<<" heldExits="<<totalExits
			<<" enterToMinimumPlus0.5="<<worstEnter<<" recoverToStandardMinus0.5="<<worstRecovery<<" failures="<<failures<<'\n';
		return failures;
	}

}

int RunSpeedEraserTests()
{
	int failures = RunFineBandRegressions();
	auto expect = [&](bool condition, const char* name)
	{
		if (!condition) { ++failures; std::cerr << "[SpeedEraser] failed: " << name << '\n'; }
	};
	auto Near = [&](double actual, double expected, double tolerance, const char* name)
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
	Near(physicalTouch.minimumDiameterPx,16,0.001,"physical action retains 16 DIP minimum");
	Near(physicalPen.maximumDiameterPx,160,0.001,"physical metadata does not set coverage maximum");
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
	Near(rotatedConfig.maximumDiameterPx, physicalTouch.maximumDiameterPx, 0.0001, "rotation retains coverage area");
	Near(rotatedConfig.motionPerPixelX, physicalTouch.motionPerPixelY, 0.000001, "rotation swaps motion axes");


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
	Near(dipBaseline.StandardDiameterPx(),32.0,0.001,"standard is an independent 32 DIP at 96 DPI");
	Near(ResolveConfig(physical,DeviceMode::Laptop,false).maximumDiameterPx,
		dipBaseline.maximumDiameterPx,0.001,"EDID cannot change dynamic size limits");
	Near(ResolveConfig({},DeviceMode::LargeScreen,false).maximumDiameterPx,
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
				Near(reference[6],config.minimumDiameterPx,0.001,"every contact returns to DIP minimum after idle");
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
		Near(touch.Advance(10.0), config.minimumDiameterPx, 0.001, "new touch held still never grows");
		touch.Reset(0,0,0,StartKind::Touch,config);
		for (int i = 1; i <= 1000; ++i)
		{
			const float position = (i % 2 ? 0.4f : -0.4f) * config.touchUnlockStart / config.motionPerPixelX;
			touch.UpdatePosition(position, 0, i * 0.002);
		}
		Near(touch.Diameter(), config.minimumDiameterPx, 0.001, "touch jitter travel does not unlock growth");
		for (int tap = 0; tap < 8; ++tap)
		{
			touch.Reset(100.0f * tap, 0, 5.0 + tap, StartKind::Touch, config);
			Near(touch.Advance(5.5 + tap), config.minimumDiameterPx, 0.001, "each genuine touch down resets for dotted erasing");
		}
		touch.Reset(0,0,0,StartKind::Touch,config);
		FeedLine(touch, config.largeTargetSpeed * 1.5, 1.0, 125, config);
		expect(touch.Diameter() > DiameterToCanvasPx(CompensateTargetDiameterDip(config,config.sizes.maximumDiameterDip),config.display)*0.85f, "observed touch unlocks its bounded target");
	}

	Controller hover;
	hover.Reset(0,0,0,StartKind::Hover,laptop);
	FeedLine(hover, 200, 1.0);
	Controller contact = hover;
	Near(contact.Diameter(), hover.Diameter(), 0.0001, "contact copying preserves reconnect test history");
	contact.PauseForReconnect(1.0);
	const float pausedDiameter = contact.Diameter();
	Near(contact.Advance(100.0), pausedDiameter, 0.0001, "reconnect wait freezes dynamics");
	Controller nearby = contact;
	Near(contact.ResumeFromReconnect(100000,20000,2.0), pausedDiameter, 0.0001,
		"far synthetic connection does not change diameter");
	nearby.ResumeFromReconnect(200,0,2.0);
	contact.UpdatePosition(100001,20000,2.008);
	nearby.UpdatePosition(201,0,2.008);
	Near(contact.Diameter(), nearby.Diameter(), 0.0001, "resume excludes bridge distance from speed history");
	expect(!contact.IsPaused(), "resume clears pause status");
	Controller touchGap;
	touchGap.Reset(0,0,0,StartKind::Touch,laptop);
	touchGap.PauseForReconnect(0.1);
	touchGap.ResumeFromReconnect(100000,20000,0.2);
	touchGap.UpdatePosition(100000,20000,0.21);
	Near(touchGap.Diameter(), laptop.minimumDiameterPx, 0.001, "synthetic connection cannot unlock touch startup");

	Controller delayed = hover;
	Controller timely = hover;
	delayed.Advance(1.1);
	delayed.UpdatePosition(204,0,1.02);
	timely.UpdatePosition(204,0,1.02);
	Near(delayed.Diameter(), timely.Diameter(), 0.0001, "late new raw sample is independent of newer preview frame");
	const float beforeInvalid = delayed.Diameter();
	delayed.UpdatePosition(100000,100000,1.02);
	delayed.UpdatePosition(100000,100000,0.9);
	delayed.UpdatePosition(std::numeric_limits<float>::quiet_NaN(),0,1.03);
	Near(delayed.Diameter(), beforeInvalid, 0.0001, "duplicate backwards and invalid packets do not invent speed");
	Controller oneIdle = hover;
	Controller manyIdle = hover;
	oneIdle.Advance(4.0);
	for (int i = 1; i <= 300; ++i) manyIdle.Advance(1.0 + i * 0.01);
	Near(oneIdle.Diameter(), manyIdle.Diameter(), 0.001, "long idle advance retains elapsed time");
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


	// 本轮替代Up Reset/140ms夹小；验证同入口尺寸会话，而非保留旧断言。
	for(const auto entry:{InputEntry::MouseLeft,InputEntry::MouseRight,InputEntry::PenTip,InputEntry::PenTail})
	for(int dpi:{96,144,192})for(double gap:{0.0,0.020,0.050,0.100,0.200,0.500,2.0})
	{
		auto display=physical;display.dipPerPixelX=display.dipPerPixelY=96.0f/dpi;
		auto source=MappedSource(entry==InputEntry::MouseLeft || entry==InputEntry::MouseRight?SourceKind::Mouse:SourceKind::IntegratedPen,display);
		source.contextId=5;source.cursorId=11;
		const auto resolved=ResolveInput(display,DeviceMode::Laptop,source,entry,{});
		const auto cfg=resolved.config;
		MouseLifecycle session;Controller current;
		const auto ticket=session.BeginContact(current,0,0,0,cfg);
		FeedLine(current,cfg.largeTargetSpeed*1.5,2,125,cfg);
		const float accepted=current.Diameter();
		const float lastX=static_cast<float>(cfg.largeTargetSpeed*3/cfg.motionPerPixelX);
		session.EndContact(current,accepted,lastX,0,2,false,false,ticket);
		Near(session.LogicalDiameter(),accepted,0.0001,"Up preserves the accepted logical diameter");
		Near(current.Diameter(),accepted,0.0001,"Up does not rewrite the finished contact controller");
		Controller atEvent=session.PreviewController();const float expected=atEvent.Advance(2+gap);
		session.Advance(2+gap+0.2); // 帧先行不能污染稍晚到达的Down事件状态。
		Controller next;const auto nextTicket=session.BeginContact(next,lastX+10000,9000,2+gap,cfg);
		Near(next.Diameter(),expected,0.0001,"Down inherits the same event-time state, including values above standard");
		expect(nextTicket!=ticket && session.LastHandoffInherited(),"independent segment owns a fresh session ticket");
		const double evidence=next.SweepEvidenceSeconds();
		next.UpdatePosition(lastX+10000,9000,2+gap+0.008);
		expect(next.SweepEvidenceSeconds()<=evidence+1e-9,"unobserved gap displacement does not create speed or sweep evidence");
		session.EndContact(current,160,lastX,0,3+gap,false,false,ticket);
		expect(session.ContactOwned(),"delayed old Up cannot overwrite a newer segment");
		session.EndContact(next,next.Diameter(),lastX+10000,9000,2+gap+0.008,false,false,nextTicket);
		const float leave=session.LogicalDiameter();
		session.Advance(20+gap);
		expect(session.LogicalDiameter()<=leave+0.001f && session.LogicalDiameter()<=cfg.StandardDiameterPx()+0.001f &&
			!session.NeedsAnimation(20+gap),"long unobserved separation settles without growing or spinning");
		std::cout<<"[EntryGap] entry="<<InputEntryName(entry)<<" dpi="<<dpi<<" gap="<<gap
			<<" upPx="<<accepted<<" downPx="<<expected<<'\n';
	}
	{
		auto cfg=ResolveConfig(physical,DeviceMode::Laptop,MappedSource(SourceKind::IntegratedPen,physical));
		cfg.inputSource.contextId=5;cfg.inputSource.cursorId=10;
		Controller hover;hover.ResetPreview(0,0,0,cfg);hover.Advance(3);
		auto metadata=cfg;++metadata.display.revision;++metadata.display.generation;
		metadata.display.development.diagnostics=true;metadata.inputSource.cursorId=11;metadata.inputSource.generation=99;
		Controller down;down.BeginContact(&hover,0,0,3,metadata);
		Near(down.DiameterDip(),hover.DiameterDip(),0.0001,"harmless metadata no longer makes pen Hover 16 jump to 32");
		expect(SessionConfigCompatible(cfg,metadata),"stable source/config ignores contact ID and diagnostic revisions");
		metadata.inputSource.contextId=6;
		expect(!SessionConfigCompatible(cfg,metadata),"different tablet context does not share a size session");
		metadata=cfg;metadata.display.monitor=900;
		expect(!SessionConfigCompatible(cfg,metadata),"monitor change preserves mapping safety");
		metadata=cfg;metadata.display.dipPerPixelX*=2;
		expect(!SessionConfigCompatible(cfg,metadata),"DPI change is an effective configuration change");
	}
	{
		InputSettings settings;
		expect(settings.automaticEnabled,"automatic master gate defaults to enabled");
		for(int index=0;index<5;++index)
		{
			const auto entry=static_cast<InputEntry>(index);
			expect(RestoreEraserKind(-1,-1,entry)==EraserKind::Speed,"missing settings default every entry to speed");
			expect(RestoreEraserKind(0,1,entry)==EraserKind::Fixed,"explicit saved Fixed is respected");
			const bool oldOrdinary=index==0 || index==2 || index==3;
			expect(RestoreEraserKind(-1,2,entry)==(oldOrdinary?EraserKind::Fixed:EraserKind::Speed),
				"legacy explicit Fixed migrates ordinary entries, not implicit right/tail behavior");
			for(auto kind:{EraserKind::Fixed,EraserKind::Speed})
			{
				settings.entries[index].kind=kind;
				const auto parsed=ResolveInput(physical,DeviceMode::Laptop,MappedSource(SourceKind::IntegratedPen,physical),entry,settings);
				expect(parsed.entry==entry && parsed.kind==kind,"five entries resolve their own Fixed/Speed setting");
				expect(ResolveInput(physical,DeviceMode::Laptop,{},entry,settings,EraserToolPolicy::Fixed).kind==EraserKind::Fixed,
					"explicit Fixed tool remains fixed independently of defaults");
			}
		}
		settings.entries[0].kind=EraserKind::Fixed;settings.entries[1].kind=EraserKind::Speed;
		settings.entries[3].kind=EraserKind::Fixed;settings.entries[4].kind=EraserKind::Speed;
		expect(ResolveInput(physical,DeviceMode::Laptop,{},InputEntry::MouseLeft,settings).kind!=
			ResolveInput(physical,DeviceMode::Laptop,{},InputEntry::MouseRight,settings).kind,"same snapshot keeps left/right choices independent");
		expect(ResolveInput(physical,DeviceMode::Laptop,{},InputEntry::PenTip,settings).kind!=
			ResolveInput(physical,DeviceMode::Laptop,{},InputEntry::PenTail,settings).kind,"same snapshot keeps tip/tail choices independent");
		const auto savedEntries=settings.entries;
		expect(SetGlobalAutomatic(settings,false) && settings.entries==savedEntries,
			"master gate disables automatic sizing without rewriting five entry preferences");
		expect(ResolveInput(physical,DeviceMode::Laptop,{},InputEntry::MouseRight,settings).kind==EraserKind::Fixed &&
			ResolveInput(physical,DeviceMode::Laptop,{},InputEntry::MouseRight,settings,EraserToolPolicy::Speed).kind==EraserKind::Fixed,
			"disabled master gate overrides configured and explicit Speed policies");
		expect(SetGlobalAutomatic(settings,true) && settings.entries==savedEntries &&
			ResolveInput(physical,DeviceMode::Laptop,{},InputEntry::MouseRight,settings).kind==EraserKind::Speed,
			"reenabling master gate restores the saved entry configuration");
		for(bool automatic:{false,true})for(int saved:{-1,0,1,2,99})
		{
			const auto value=RestorePenResponse(saved,automatic);
			expect(automatic || value!=PenResponseChoice::Automatic,"Win7 Auto/unknown safely resolves to tablet without index shifting");
			if(saved==1)expect(value==PenResponseChoice::ScreenPen,"Win7 UI value 1 still means ScreenPen");
			if(saved==2)expect(value==PenResponseChoice::Tablet,"Win7 UI value 2 still means Tablet");
		}
		settings.automaticPenSupported=false;settings.entries[4].penResponse=PenResponseChoice::ScreenPen;
		InputSource external;external.kind=SourceKind::ExternalPen;external.contextId=8;
		const auto manual=ResolveInput(physical,DeviceMode::Laptop,external,InputEntry::PenTail,settings);
		expect(manual.config.response==ResponseModel::ScreenPenHybrid && manual.config.inputSource.kind==SourceKind::ExternalPen &&
			!manual.config.inputMapped && manual.config.motionUnit==MotionUnit::DipPerSecond,
			"manual screen pen changes response, not true identity or physical mapping");
		auto debug=physical;debug.development.response=ResponseOverride::IndirectDip;
		const auto forced=ResolveInput(debug,DeviceMode::Laptop,external,InputEntry::PenTail,settings);
		expect(forced.config.response==ResponseModel::IndirectDip && forced.config.developmentResponseOverride,
			"explicit development override has documented priority");
		Inkeys::Drawing::Draw3::Bridge::StateBridge bridge;bridge.Reset();
		Inkeys::Drawing::Draw3::Bridge::ProductState product;product.tool=Inkeys::Drawing::Draw3::Bridge::Tool::ConfiguredEraser;
		product.eraserInputs=settings;bridge.PublishState(product);
		expect(bridge.Snapshot().eraserInputs==settings,"whole entry preferences are published as one bridge snapshot");
	}
	{
		namespace fs=std::filesystem;
		const fs::path root=fs::temp_directory_path()/(L"inkeys-eraser-settings-"+std::to_wstring(GetCurrentProcessId())+L"-"+std::to_wstring(GetTickCount64()));
		const auto oldPath=globalPath;std::error_code error;
		const bool owned=fs::create_directory(root,error);
		if(owned)
		{
			fs::create_directories(root/L"Inkeys"/L"Config",error);
			std::ofstream(root/L"Inkeys"/L"Config"/L"main.json")<<"{}";
			globalPath=root.wstring()+L"\\";
			Inkeys::Config saved;
			expect(saved.Drawing.Eraser.Automatic.load() && saved.Drawing.Eraser.BaseDiameterDip.load()==32 && saved.Drawing.Eraser.Sensitivity.load()==1,
				"missing global settings default to automatic enabled and32/medium without changing entry migration");
			saved.Drawing.Eraser.Automatic=false;
			saved.Drawing.Eraser.BaseDiameterDip=64;saved.Drawing.Eraser.Sensitivity=2;
			saved.Drawing.Eraser.MouseLeft=0;saved.Drawing.Eraser.MouseRight=1;saved.Drawing.Eraser.Touch=0;
			saved.Drawing.Eraser.PenTip=0;saved.Drawing.Eraser.PenTail=1;
			saved.Drawing.Eraser.PenTipResponse=1;saved.Drawing.Eraser.PenTailResponse=2;
			saved.Experimental.Inkeys3.Draw3.TouchContactAreaAssistance=true;
			expect(saved.Write(),"actual Config module writes entry settings");
			Inkeys::Config read;expect(read.ReadAll(),"actual Config module restores settings on restart");
			expect(!read.Drawing.Eraser.Automatic.load() && read.Drawing.Eraser.MouseLeft.load()==0 && read.Drawing.Eraser.MouseRight.load()==1 &&
				read.Drawing.Eraser.Touch.load()==0 && read.Drawing.Eraser.PenTip.load()==0 && read.Drawing.Eraser.PenTail.load()==1 &&
				read.Drawing.Eraser.PenTipResponse.load()==1 && read.Drawing.Eraser.PenTailResponse.load()==2 &&
				read.Drawing.Eraser.BaseDiameterDip.load()==64 && read.Drawing.Eraser.Sensitivity.load()==2 &&
				read.Experimental.Inkeys3.Draw3.TouchContactAreaAssistance.load(),"master gate, five kinds, two pen choices and old area key round-trip independently");
			globalPath=oldPath;
			if(root.parent_path()==fs::temp_directory_path())fs::remove_all(root,error);
		}
		else expect(false,"create owned config fixture");
	}
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
		Near(cfg.minimumDiameterPx*96/dpi,16,0.001,"minimum has invariant DIP meaning");
		Near(cfg.StandardDiameterPx()*96/dpi,32,0.001,"standard has invariant DIP meaning");
		Near(cfg.maximumDiameterPx*96/dpi,160,0.001,"maximum has invariant DIP meaning");
		Near(FixedDiameterPx(42,s)*96/dpi,42,0.001,"fixed DIP bypass is independent of EDID and motion");
	}
	for(const double v:{100,300,500,650,750,800,1000,1300,1700,2200})
	{
		const auto scan=Replay(laptop,{{0,0},{20,v*20}},125,60,{10,20});
		if(v<=laptop.sweepEnterSpeed)
			for(const auto d:scan)Near(d,laptop.StandardDiameterPx(),0.02,"ordinary speed never accumulates into sweep");
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
	Near(historicalDiameter,160,2.0,"historical wide diameter stays immutable");
	const auto late=effective.MakeInterval(1.4,1.6,historicalDiameter,historicalDiameter,1.6,laptop);
	expect(!late.reanchor,"late pre-shrink geometry is not silently re-timed");
	Controller samePosition=idleTool;
	for(int i=1;i<=1000;++i)samePosition.UpdatePosition(3904,4,2.71+i*0.001);
	expect(samePosition.DiameterDip()<=16.1f,"same position packets do not keep the tool large");
	Controller noisy;noisy.Reset(0,0,0,StartKind::Hover,laptop);FeedLine(noisy,2600,1.5);
	for(int i=1;i<=1400;++i)noisy.UpdatePosition(3900+(i%2?0.1f:-0.1f),0,1.5+i*0.001);
	Controller quietNoiseReference;quietNoiseReference.Reset(0,0,0,StartKind::Hover,laptop);
	FeedLine(quietNoiseReference,2600,1.5);quietNoiseReference.Advance(2.9);
	Near(noisy.DiameterDip(),quietNoiseReference.DiameterDip(),0.03,
		"bounded sub-threshold noise has the same idle deadline as complete absence of packets");
	Controller ordinaryTouch;ordinaryTouch.Reset(0,0,0,StartKind::Touch,laptop);
	FeedLine(ordinaryTouch,100,1.0);
	expect(ordinaryTouch.DiameterDip()>30 && ordinaryTouch.DiameterDip()<=32.01f,
		"ordinary real touch motion reaches standard without sweep qualification");
	const WidthInterval interval{ 0,1,50,600,8,800 };
	Near(InterpolateDiameter(interval,0.5),325,0.001,"scaled width interpolation exceeds old 200px maximum");
	Near(InterpolateDiameter({0,1,4,8,4,8},0),4,0.001,"scaled minimum may be below old 20px limit");
	Near(ContactDiameter(162.5f,16.0f),325,0.001,"contact cursor uses accepted geometric endpoint");
	Near(ContactDiameter(0,24),24,0.001,"initial cursor uses batch minimum");
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
		std::cout << "[LowBandChangedReplay] " << static_cast<int>(kind) << " " << dpi << " " << static_cast<int>(mode);
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
		Near(CompensateTargetDiameterDip(c,100),100,0.0001,"R7 touch dynamic target is DIP, independent of physical density");
		std::vector<Knot> local{{0,0}};for(int i=1;i<=8;++i)local.push_back({i*0.2,i%2?36.0:0.0});
		const auto values=Replay(c,local,125,60,{0.8,1.6},StartKind::Touch);
		expect(values[0]>64 && values[1]>64,"R7 36mm local touch strokes at 180mm/s enter usable sweep");
	}
	Near(penConfig.rhoMmPerDip,0.25,0.00001,"physical axes resolve millimeters per DIP");
	expect(penConfig.motionUnit==MotionUnit::MillimetersPerSecond && penConfig.sweepEnterSpeed==120 &&
		penConfig.largeTargetSpeed==350,"screen pen uses explicit physical thresholds");
	for(float rho:{0.125f,0.25f,0.5f})for(float beta:{0.0f,0.25f,0.5f,0.75f,1.0f})
	{
		auto c=penConfig;c.rhoMmPerDip=rho;c.penBeta=beta;
		Near(CompensateTargetDiameterDip(c,24),24,0.0001,"physical compensation leaves fine band unchanged");
		Near(CompensateTargetDiameterDip(c,72),std::clamp(32.0+std::pow(0.25/rho,beta)*40,16.0,160.0),0.0001,
			"pen beta compensates only standard-above increment");
		expect(c.sizes==EraserSizes{},"beta does not change base or fixed attributes");
	}
	const auto onsetPen=Replay(penConfig,{{0,0},{1,350}},1000,240,{0.12,0.3,0.8});
	expect(onsetPen[0]>32 && onsetPen[0]<64,"screen pen begins light growth near 120ms without jumping large");
	expect(onsetPen[1]>onsetPen[0] && onsetPen[2]>100,"physical pen sweep is accessible");
	for(float speed:{20.0f,60.0f,100.0f})for(float d:Replay(penConfig,{{0,0},{20,20*speed}},125,60,{10,20}))
		Near(d,32,0.01,"ordinary screen pen stays standard for twenty seconds");
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
		Near(down.Diameter(),c.minimumDiameterPx,0.001,"fine preview Down has no size jump");
		Near(down.SweepEvidenceSeconds(),0,0.000001,"preview Down discards movement history");
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
	Near(h1.motionPerPixelX,h2.motionPerPixelX*2,0.00001,"resolution doubling preserves heuristic action");
	twice.dipPerPixelX=twice.dipPerPixelY=0.5f;
	const auto h3=ResolveConfig(twice,DeviceMode::LargeScreen,MappedSource(SourceKind::IntegratedPen,twice));
	Near(h1.motionPerPixelX,h3.motionPerPixelX*2,0.00001,"DPI scaling applied once");
	std::swap(twice.pixelWidth,twice.pixelHeight);twice.orientation=1;
	Near(ResolutionDpiActionGain(twice),1,0.00001,"rotation preserves heuristic extent");
	h.logicalOutputKnown=false;
	expect(ResolveConfig(h,DeviceMode::LargeScreen,MappedSource(SourceKind::IntegratedPen,h)).motionSource==ScaleSource::DipOnly,"unknown logical output uses pen DIP fallback");
	auto manual=penDisplay;manual.development.scale=ScaleOverride::ManualSurface;
	manual.development.calibration={manual.monitor,28,18,0};
	const auto calibrated=ResolveConfig(manual,DeviceMode::Laptop,MappedSource(SourceKind::IntegratedPen,manual));
	expect(calibrated.motionSource==ScaleSource::ManualCalibration && calibrated.sizes==EraserSizes{},"manual surface overrides EDID without changing properties");
	Near(calibrated.motionPerPixelX,280.0/manual.pixelWidth,0.00001,"manual calibration uses measured width");
	manual.orientation=1;std::swap(manual.pixelWidth,manual.pixelHeight);
	const auto turned=ResolveConfig(manual,DeviceMode::Laptop,MappedSource(SourceKind::IntegratedPen,manual));
	Near(turned.motionPerPixelX,calibrated.motionPerPixelY,0.00001,"manual calibration follows rotation once");
	manual.monitor=1234;
	expect(ResolveConfig(manual,DeviceMode::Laptop,MappedSource(SourceKind::IntegratedPen,manual)).motionSource==ScaleSource::DipOnly,"manual surface cannot leak to another monitor");
	for(float rho:{0.25f,0.5f}){auto c=physicalTouch;c.rhoMmPerDip=rho;
		Near(CompensateTargetDiameterDip(c,100),100,0.0001,"touch target stays DIP across physical densities");}
	bool clamped=false;auto extreme=penConfig;extreme.rhoMmPerDip=0.01f;
	Near(CompensateTargetDiameterDip(extreme,160,&clamped),160,0.001,"extreme physical target is centrally bounded");
	expect(clamped,"target clamping is available for diagnostics");

	// 面积辅助必须走同一个产品控制器；坏数据和开关关闭均不改变速度模型。
	{
		// 合成明确 PROPERTY_METRICS，只验证产品换算，不冒充 Surface 实测。
		const ContactLengthMetrics axis{2,1000,100,30100,true},span{2,100,0,10000,true};
		const auto x=ResolveContactLengthTransform(axis,span,0.1f);
		const auto y=ResolveContactLengthTransform(axis,span,0.2f);
		expect(x.status==ContactAreaUnits::CanvasPixels && x.resolutionAdjusted && !x.unitConverted,
			"same length unit with different resolutions is convertible");
		Near(x.spanToAxis,10,0.00001,"resolution ratio maps span to coordinate logical length");
		auto sample=ConvertContactArea(30,10,x,y);
		Near(sample.widthPx,30,0.00001,"relative length does not subtract nonzero coordinate logical origin");
		Near(sample.heightPx,20,0.00001,"Y uses its own linear position transform");
		Near(sample.rawWidth,30,0,"conversion preserves original packet width");
		auto same=span;same.resolution=1000;
		const auto sameTransform=ResolveContactLengthTransform(axis,same,0.1f);
		expect(!sameTransform.resolutionAdjusted && !sameTransform.unitConverted,"equal metadata needs no unit or resolution adaptation");
		Near(ConvertContactArea(300,200,sameTransform,sameTransform).widthPx,30,0.00001,"same-resolution length remains compatible");
		auto inches=span;inches.units=1;inches.resolution=254;
		const auto inchTransform=ResolveContactLengthTransform(axis,inches,0.1f);
		expect(inchTransform.unitConverted && inchTransform.resolutionAdjusted &&
			inchTransform.status==ContactAreaUnits::CanvasPixels,"inch contact and centimeter coordinate metadata convert");
		Near(ConvertContactArea(30,20,inchTransform,inchTransform).widthPx,30,0.00001,"inch/cm conversion includes exactly one unit ratio");
		auto inchAxis=axis;inchAxis.units=1;inchAxis.resolution=2540;
		const auto reverse=ResolveContactLengthTransform(inchAxis,span,0.1f);
		Near(ConvertContactArea(30,20,reverse,reverse).widthPx,30,0.00001,"centimeter contact and inch coordinate metadata convert");
		const auto reflected=ResolveContactLengthTransform(axis,span,-0.1f);
		Near(ConvertContactArea(30,20,reflected,reflected).widthPx,30,0.00001,"a reflected position axis keeps contact length positive");
		const auto checkInvalid=[&](ContactLengthMetrics a,ContactLengthMetrics w,float scale,ContactAreaUnits expected)
		{
			const auto transform=ResolveContactLengthTransform(a,w,scale);
			const auto rejected=ConvertContactArea(30,20,transform,y);
			expect(transform.status==expected && rejected.units==expected && rejected.widthPx<0 &&
				rejected.heightPx<0 && rejected.rawWidth==30,"invalid metadata preserves raw values without inventing pixels");
		};
		auto bad=axis;bad.present=false;checkInvalid(bad,span,0.1f,ContactAreaUnits::MissingAxis);
		bad=span;bad.present=false;checkInvalid(axis,bad,0.1f,ContactAreaUnits::Missing);
		bad=axis;bad.units=0;checkInvalid(bad,span,0.1f,ContactAreaUnits::AxisUnitsMissing);
		bad=span;bad.units=0;checkInvalid(axis,bad,0.1f,ContactAreaUnits::SpanUnitsMissing);
		for(uint32_t unit:{3u,8u,10u,99u})
		{
			bad=span;bad.units=unit;checkInvalid(axis,bad,0.1f,ContactAreaUnits::UnsupportedLengthUnits);
		}
		for(float resolution:{0.0f,-1.0f,std::numeric_limits<float>::infinity(),std::numeric_limits<float>::quiet_NaN()})
		{
			bad=axis;bad.resolution=resolution;checkInvalid(bad,span,0.1f,ContactAreaUnits::InvalidAxisResolution);
			bad=span;bad.resolution=resolution;checkInvalid(axis,bad,0.1f,ContactAreaUnits::InvalidSpanResolution);
		}
		bad=axis;bad.logicalMax=bad.logicalMin;checkInvalid(bad,span,0.1f,ContactAreaUnits::InvalidAxisRange);
		bad=span;bad.logicalMax=bad.logicalMin;checkInvalid(axis,bad,0.1f,ContactAreaUnits::InvalidSpanRange);
		bad=span;bad.logicalMax=-1;checkInvalid(axis,bad,0.1f,ContactAreaUnits::InvalidSpanRange);
		bad=span;bad.logicalMin=-1;checkInvalid(axis,bad,0.1f,ContactAreaUnits::InvalidSpanRange);
		checkInvalid(axis,span,0,ContactAreaUnits::InvalidPositionScale);
		checkInvalid(axis,span,std::numeric_limits<float>::quiet_NaN(),ContactAreaUnits::InvalidPositionScale);
		bad=span;bad.resolution=std::numeric_limits<float>::denorm_min();
		checkInvalid(axis,bad,1,ContactAreaUnits::ConversionNonFinite);
		expect(ConvertContactArea(10001,20,x,y).units==ContactAreaUnits::OutsideMetrics &&
			ConvertContactArea(20,-1,x,y).units==ContactAreaUnits::OutsideMetrics,"packet spans outside declared metrics are rejected, not clamped");
		expect(ConvertContactArea(10000,0,x,y).units==ContactAreaUnits::CanvasPixels,"declared endpoints are inclusive; zero is rejected by the existing area policy");
		expect(ConvertContactArea(NAN,20,x,y).units==ContactAreaUnits::ConversionNonFinite,"nonfinite raw length never creates usable pixels");
		expect(ContactAreaMetadataReason(ContactAreaUnits::InvalidSpanResolution)==ContactAreaReason::InvalidResolution &&
			ContactAreaMetadataReason(ContactAreaUnits::InvalidSpanRange)==ContactAreaReason::InvalidRange &&
			ContactAreaMetadataReason(ContactAreaUnits::SpanUnitsMissing)==ContactAreaReason::SpanUnitsMissing,
			"diagnostic reasons separate unknown units, invalid resolution and invalid ranges");
		std::cout << "[TouchAreaConversion] synthetic metadata cases completed\n";
	}
	auto areaDisplay=penDisplay;areaDisplay.development.touchContactAreaAssistance=true;
	const auto areaConfig=ResolveConfig(areaDisplay,DeviceMode::Laptop,MappedSource(SourceKind::Touch,areaDisplay));
	const auto normalArea=AreaSample(areaConfig,30,20);
	for(const int dpi:{96,144,192})
	{
		auto display=areaDisplay;display.dipPerPixelX=display.dipPerPixelY=96.0f/dpi;
		const auto config=ResolveConfig(display,DeviceMode::Laptop,MappedSource(SourceKind::Touch,display));
		const ContactLengthMetrics axis{2,1000,0,30000,true},span{2,100,0,10000,true};
		const auto transform=ResolveContactLengthTransform(axis,span,0.1f*dpi/96);
		const auto area=ConvertContactArea(30,20,transform,transform);
		Controller controller;controller.Reset(0,0,0,StartKind::Touch,config,0,&area);
		Near(controller.DiameterDip(),16,0.001,"converted metadata retains Touch small Down");
		for(int i=1;i<=250;++i)
			controller.UpdatePosition(static_cast<float>(i*0.08/config.motionPerPixelX),0,i/125.0,&area);
		const auto diagnostic=controller.AreaDiagnostics(2);
		expect(diagnostic.sampleValid && diagnostic.referenceReady && diagnostic.active,
			"synthetic converted metadata reaches the unchanged area reference and accepted floor");
		Near(diagnostic.widthDip,30,0.001,"metadata-to-canvas and canvas-to-DIP do not double scale");
		Near(diagnostic.heightDip,20,0.001,"height stays consistent at every DPI");
		Near(diagnostic.referenceFloorDip,39,0.001,"conversion does not alter area multiplier, padding or ceiling");
		controller.Advance(6);
		Near(controller.DiameterDip(),16,0.01,"converted area retains true no-Move expiry and small idle size");
	}
	{
		auto display=areaDisplay;display.dipPerPixelX=0.5f;display.dipPerPixelY=0.75f;
		const ContactLengthMetrics axis{2,1000,0,30000,true},span{2,100,0,10000,true};
		const auto x=ResolveContactLengthTransform(axis,span,0.2f);
		const auto y=ResolveContactLengthTransform(axis,span,0.1f/0.75f);
		const auto area=ConvertContactArea(30,20,x,y);
		const auto config=ResolveConfig(display,DeviceMode::Laptop,MappedSource(SourceKind::Touch,display));
		Controller c;c.Reset(0,0,0,StartKind::Touch,config,0,&area);
		Near(c.AreaDiagnostics(0).widthDip,30,0.001,"anisotropic X conversion uses X DIP scale");
		Near(c.AreaDiagnostics(0).heightDip,20,0.001,"anisotropic Y conversion uses Y DIP scale");
		std::swap(display.dipPerPixelX,display.dipPerPixelY);
		const auto rotated=ConvertContactArea(20,30,y,x);
		const auto rotatedConfig=ResolveConfig(display,DeviceMode::Laptop,MappedSource(SourceKind::Touch,display));
		c.Reset(0,0,1,StartKind::Touch,rotatedConfig,0,&rotated);
		Near(c.AreaDiagnostics(1).widthDip,20,0.001,"rotation swaps span and linear-axis transforms together");
		Near(c.AreaDiagnostics(1).heightDip,30,0.001,"rotation preserves physical axis correspondence");
	}
	{
		auto disabled=areaConfig;disabled.touchContactAreaAssistance=false;
		Controller inspect;inspect.Reset(0,0,0,StartKind::Touch,disabled,0,&normalArea);
		const auto d=inspect.AreaDiagnostics(0);
		expect(!d.enabled && !d.active && d.sampleValid && d.widthDip==30 && d.heightDip==20,
			"area-off diagnostics still expose valid measured DIP dimensions");
		auto unknown=normalArea;unknown.units=ContactAreaUnits::Unverified;
		inspect.UpdatePosition(0,0,0.01,&unknown);
		const auto u=inspect.AreaDiagnostics(0.01);
		expect(!u.sampleValid && u.widthDip<0 && u.reason==ContactAreaReason::UnitsUnknown,
			"unverified area units never appear as verified DIP");
	}

	const std::vector<Knot> slowDrag{{0,0},{2,20}};
	const std::vector<double> areaTimes{0.3,0.5,1.0,2.0,3.0,4.5,6.0};
	const auto areaReference=ReplayArea(areaConfig,slowDrag,1000,120,areaTimes,[&](double){return normalArea;},2.0);
	expect(areaReference[2].diameter>38 && areaReference[2].diameter<40 && areaReference[2].area.active,
		"ordinary slow touch drag opens bounded area floor without sweep speed");
	Near(areaReference[3].area.referenceFloorDip,39,0.001,"max-axis span produces independent bounded floor");
	expect(!areaReference[4].animating && areaReference[4].wake>3.0 &&
		areaReference[4].diameter>38,"held touch rests at its area floor and schedules expiry without spinning");
	Near(areaReference.back().diameter,16,0.001,"missing area eventually releases to minimum");
	expect(!areaReference.back().animating && !areaReference.back().area.referenceFresh,"expired area cannot stay fresh or keep animation awake forever");
	double worstAreaRate=0;
	for(int hz:{60,125,240,1000})for(int fps:{30,60,120})
	{
		const auto values=ReplayArea(areaConfig,slowDrag,hz,fps,areaTimes,[&](double){return normalArea;},2.0);
		for(size_t i=0;i<values.size();++i)
		{
			const double error=std::abs(values[i].diameter-areaReference[i].diameter)/areaReference[i].diameter;
			worstAreaRate=std::max(worstAreaRate,error);
			if(error>0.05)std::cerr << "[AreaRateDetail] hz=" << hz << " fps=" << fps << " t=" << areaTimes[i]
				<< " value=" << values[i].diameter << " ref=" << areaReference[i].diameter << " floor=" << values[i].area.activeFloorDip
				<< " refFloor=" << areaReference[i].area.activeFloorDip << " ready=" << values[i].area.referenceReady << '\n';
			expect(error<=0.05,"area confirmation/filter/follow are time based within five percent");
		}
	}
	std::cout << "[R7AreaRate] " << worstAreaRate*100 << "%\n";
	// 无面积时也覆盖慢速 Touch 的移动目标，防止每包吸附重新引入高回报率直追。
	auto noAreaConfig=areaConfig;noAreaConfig.touchContactAreaAssistance=false;
	const auto noAreaReference=ReplayArea(noAreaConfig,slowDrag,1000,120,areaTimes,[&](double){return normalArea;},2.0);
	for(int hz:{60,125,240,1000})for(int fps:{30,60,120})
	{
		const auto values=ReplayArea(noAreaConfig,slowDrag,hz,fps,areaTimes,[&](double){return normalArea;},2.0);
		for(size_t i=0;i<values.size();++i)
			expect(std::abs(values[i].diameter-noAreaReference[i].diameter)<=noAreaReference[i].diameter*0.05f,
				"moving Touch target follows damping independently of packet rate with assistance off");
	}

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
			Near(enabled[i].diameter,disabled[i].diameter,0.001,"bad contact width never becomes maximum assistance");
			expect(!enabled[i].area.active && !enabled[i].area.referenceReady,"invalid area does not become a reference");
		}
	}
	for(const auto kind:{SourceKind::Mouse,SourceKind::ExternalPen,SourceKind::IntegratedPen,SourceKind::TouchPad,SourceKind::Unknown})
	{
		auto d=areaDisplay;d.development.response=ResponseOverride::DirectTouch;
		const auto c=ResolveConfig(d,DeviceMode::Laptop,MappedSource(kind,d));
		const auto result=ReplayArea(c,slowDrag,125,60,{1,2},[&](double){return normalArea;},2,StartKind::Hover);
		for(const auto& value:result)expect(!value.area.active,"forced Touch model cannot invent real Touch area");
		Near(FixedDiameterPx(50,d),50,0.001,"fixed eraser bypasses area and speed");
	}
	auto unmapped=areaConfig;unmapped.inputMapped=false;
	const auto rejectedMapping=ReplayArea(unmapped,slowDrag,125,60,{1},[&](double){return normalArea;});
	expect(!rejectedMapping[0].area.active && rejectedMapping[0].area.reason==ContactAreaReason::MappingUnknown,
		"unknown area mapping leaves otherwise valid touch input usable");
	const auto press=ReplayArea(areaConfig,{{0,0},{3,0}},1000,120,{0.01,0.5,2,3},
		[&](double t){return AreaSample(areaConfig,t<0.5?30:70,t<0.5?20:50);});
	for(const auto& value:press)Near(value.diameter,16,0.001,"stationary press and later larger area cannot open a hole");
	std::vector<Knot> jitter{{0,0}};for(int i=1;i<=200;++i)jitter.push_back({i*0.01,i%2?0.4:-0.4});
	for(const auto& value:ReplayArea(areaConfig,jitter,1000,120,{0.5,1,2},[&](double){return normalArea;}))
		expect(value.diameter==16 && !value.area.active && !value.area.referenceReady,"landing jitter cannot establish dragging area");
	const auto locked=ReplayArea(areaConfig,{{0,0},{1,10},{3,10}},125,60,{1,1.5,2.5},
		[&](double t){return AreaSample(areaConfig,t<1?30:40,t<1?20:28);});
	for(const auto& value:locked)
	{
		Near(value.area.referenceFloorDip,39,0.001,"confirmed reference does not breathe with contact size");
		expect(value.diameter<=39.01f,"same-position area increase does not enlarge accepted geometry");
	}
	const auto outlier=ReplayArea(areaConfig,{{0,0},{1,10},{3,10}},125,60,{1,1.1,2.8},
		[&](double t){return AreaSample(areaConfig,t<=1?30:70,t<=1?20:50);});
	expect(outlier[1].area.reason==ContactAreaReason::Outlier && !outlier[1].area.sampleValid,
		"large change inside reported range is rejected, not clamped");
	Near(outlier.back().diameter,16,0.1,"persistent outlier releases old reference");
	for(const int dpi:{96,144,192})
	{
		auto d=areaDisplay;d.dipPerPixelX=d.dipPerPixelY=96.0f/dpi;
		const auto c=ResolveConfig(d,DeviceMode::Laptop,MappedSource(SourceKind::Touch,d));
		const auto sample=AreaSample(c,30,20);
		const auto values=ReplayArea(c,slowDrag,125,60,{1.5},[&](double){return sample;});
		Near(values[0].area.referenceFloorDip,39,0.001,"per-axis pixel to DIP conversion occurs once");
	}
	auto anisotropic=areaDisplay;anisotropic.dipPerPixelX=0.5f;anisotropic.dipPerPixelY=0.75f;
	const auto anisConfig=ResolveConfig(anisotropic,DeviceMode::Laptop,MappedSource(SourceKind::Touch,anisotropic));
	const auto anSample=AreaSample(anisConfig,30,20);
	const auto an=ReplayArea(anisConfig,slowDrag,125,60,{1.5},[&](double){return anSample;});
	std::swap(anisotropic.dipPerPixelX,anisotropic.dipPerPixelY);std::swap(anisotropic.cmPerPixelX,anisotropic.cmPerPixelY);
	const auto swapConfig=ResolveConfig(anisotropic,DeviceMode::Laptop,MappedSource(SourceKind::Touch,anisotropic));
	const auto swapSample=AreaSample(swapConfig,20,30);
	const auto swapped=ReplayArea(swapConfig,slowDrag,125,60,{1.5},[&](double){return swapSample;});
	Near(an[0].area.referenceFloorDip,swapped[0].area.referenceFloorDip,0.001,"rotation swaps axes without changing max-axis assistance");
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
	Near(smallFloor.DiameterDip(),resumedSmall,0.001,"renewed standard-sized area cannot enlarge a cursor without another actual segment");

	Controller firstFinger,secondFinger;
	const auto largerArea=AreaSample(areaConfig,40,30);
	firstFinger.Reset(0,0,0,StartKind::Touch,areaConfig,0,&normalArea);
	secondFinger.Reset(0,0,0,StartKind::Touch,areaConfig,0,&largerArea);
	for(int i=1;i<=250;++i)
	{
		const double t=i/125.0;const float px=static_cast<float>(t*10/areaConfig.motionPerPixelX);
		firstFinger.UpdatePosition(px,0,t,&normalArea);secondFinger.UpdatePosition(px,0,t,&largerArea);
	}
	Near(firstFinger.AreaDiagnostics(2).referenceFloorDip,39,0.001,"first finger retains its reference");
	Near(secondFinger.AreaDiagnostics(2).referenceFloorDip,50,0.001,"second finger owns an independent reference");
	const float beforeGap=firstFinger.Diameter();firstFinger.PauseForReconnect(2);
	Near(firstFinger.Advance(100),beforeGap,0.001,"reconnect wait freezes area and diameter clocks");
	Near(firstFinger.ResumeFromReconnect(100000,0,3),beforeGap,0.001,"synthetic bridge does not enable larger assistance");
	const auto zero=AreaSample(areaConfig,0,0);
	firstFinger.UpdatePosition(100000,0,3.01,&zero,true);
	Near(firstFinger.AreaDiagnostics(3.01).referenceFloorDip,39,0.001,"terminal zero size cannot replace touch reference");
	for(int tap=0;tap<5;++tap)
	{
		firstFinger.Reset(0,0,4+tap,StartKind::Touch,areaConfig,0,&normalArea);
		Near(firstFinger.Advance(4.5+tap),16,0.001,"each real touch tap starts small without previous reference");
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
	// 三种动作单位和手动标尺都执行实际 Touch 控制器，不仅检查参数解析。
	for(int source=0;source<4;++source)
	{
		auto display=penDisplay;
		auto mode=DeviceMode::Laptop;
		if(source==1){display.development.scale=ScaleOverride::ManualSurface;display.development.calibration={display.monitor,30,20,0};}
		if(source>=2)display.physicalAvailable=false;
		if(source==2)mode=DeviceMode::LargeScreen;
		const auto config=ResolveConfig(display,mode,MappedSource(SourceKind::Touch,display));
		const double v=config.largeTargetSpeed*0.72;
		std::vector<Knot> path{{0,0}};for(int i=1;i<=8;++i)path.push_back({i*0.2,i%2?v*0.2:0.0});
		const std::vector<double> times{0.25,0.5,0.8,1.6};
		const auto reference=Replay(config,path,1000,120,times,StartKind::Touch);
		expect(reference.back()>64,"physical/manual/heuristic/DIP Touch all reach useful local-sweep coverage");
		for(int hz:{60,125,240,1000})for(int fps:{30,60,120})
		{
			const auto values=Replay(config,path,hz,fps,times,StartKind::Touch);
			for(size_t i=0;i<values.size();++i)
				expect(std::abs(values[i]-reference[i])<=reference[i]*0.05f,"all Touch scale routes retain five-percent rate consistency");
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
