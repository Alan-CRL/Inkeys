#include "../Inkeys/Inkeys/Drawing/Draw3/Draw3.SpeedEraser.h"
#include "../Inkeys/Inkeys/Drawing/Draw3/Draw3.Bridge.h"
#include <algorithm>
#include <cmath>
#include <iostream>

import Inkeys.UI.Bar.EraserAttributeMotion;
import Inkeys.UI.Bar.Animation;

int RunEraserAttributeTests()
{
	using namespace Inkeys::Drawing::Draw3::SpeedEraser;
	using namespace Inkeys::UI::Bar;
	int failures=0;
	auto expect=[&](bool value,const char* message){if(!value){++failures;std::cerr<<"[EraserAttribute] "<<message<<'\n';}};
	// 方案B已确认：以下断言先在af22b4a6实现上运行，定位旧预设/锚点/菜单差异。
	expect(static_cast<int>(BaseSize::Small)==24 && static_cast<int>(BaseSize::Large)==40,"B presets are24/32/40");
	for(const auto pair:{std::pair{16,24},std::pair{24,24},std::pair{32,32},std::pair{64,40},std::pair{40,40}})
	{
		const auto migrated=RestoreBaseSize(pair.first);
		expect(static_cast<int>(migrated)==pair.second && RestoreBaseSize(static_cast<int>(migrated))==migrated,"legacy preset migration is ordinal and idempotent");
	}
	{
		EraserAttributeLayoutInput input;input.work={0,0,1400,900};input.main={500,420,880,500};input.anchor={650,425,720,495};
		const auto l=ResolveEraserAttributeLayout(input);
		auto cx=[](EraserAttributeRect r){return (r.left+r.right)/2;};
		auto cy=[](EraserAttributeRect r){return (r.top+r.bottom)/2;};
		expect(std::abs(cx(l.items[0])-cx(input.anchor))<0.001,"clear remains anchored to the main eraser entry");
		expect(std::abs(l.panel.Width()-342.0)<0.001,"asymmetric stable panel is342 DIP wide");
		expect(l.items[3].right<l.items[0].left && l.items[0].right<l.items[4].left,"B order is circular presets / clear / automatic");
		const EraserAttributeRect automatic{l.items[4].left,l.items[4].top,l.items[5].right,l.items[4].bottom};
		expect(std::abs(automatic.Width()-90.0)<0.001 && std::abs(automatic.Height()-70.0)<0.001 &&
			std::abs(l.items[4].Width()-70.0)<0.001 && std::abs(l.items[5].Width()-20.0)<0.001,
			"automatic composite is90x70 with70/20 action regions");
		expect(std::abs(l.automaticDivider.Width()-1.0)<0.001 &&
			std::abs(l.automaticDivider.top-l.dividers[0].top)<0.001 &&
			std::abs(l.automaticDivider.bottom-l.dividers[0].bottom)<0.001,
			"internal divider matches the centered main divider height");
		expect(std::abs(cx(l.menu)-cx(automatic))<0.001,"menu centers on entire automatic control");
		expect(l.menu.Width()>=180 && l.menu.Width()<=200 && l.menu.Height()<=100,"sensitivity menu has two compact rows");
		expect(l.items[9].bottom<=l.items[6].top,"disabled gear lives in header row");
		expect(std::abs(cy(l.menuTitle)-(l.menu.top+l.items[6].top)/2)<0.001,
			"sensitivity title is vertically centered between popup top and button row");
		const double firstGap=cx(l.items[2])-cx(l.items[1])-(l.previewDiameters[0]+l.previewDiameters[1])/2;
		const double secondGap=cx(l.items[3])-cx(l.items[2])-(l.previewDiameters[1]+l.previewDiameters[2])/2;
		expect(std::abs(firstGap-16.0)<0.001 && std::abs(secondGap-16.0)<0.001 &&
			std::abs(l.previews[0].left-l.panel.left-16.0)<0.001 &&
			std::abs(l.dividers[0].left-l.previews[2].right-16.0)<0.001,
			"circle group has four16 DIP edge gaps");
		expect(std::abs(l.items[0].left-l.dividers[0].right-5.0)<0.001 &&
			std::abs(l.dividers[1].left-l.items[0].right-5.0)<0.001 &&
			std::abs(l.automatic.left-l.dividers[1].right-5.0)<0.001 &&
			std::abs(l.panel.right-l.automatic.right-5.0)<0.001,
			"automatic and central divider spacing uses four5 DIP gaps");
		expect(std::abs(l.items[1].Width()-(l.previews[0].Width()+BarButtonGapDip*2))<0.001,
			"small circle hit bounds follow the visible circle instead of the old70DIP slot");
		const auto small=l.previews[0];const double hitRadius=small.Width()/2+BarButtonGapDip;
		expect(EraserAttributeCircleContains(small,BarButtonGapDip,EraserRectCenterX(small),EraserRectCenterY(small)+hitRadius-0.01) &&
			!EraserAttributeCircleContains(small,BarButtonGapDip,EraserRectCenterX(small)+hitRadius-0.01,EraserRectCenterY(small)+hitRadius-0.01),
			"size preset hit testing is a true circle rather than its bounding rectangle");
	}
	InputSettings settings;
	expect(settings.automaticEnabled && settings.baseSize==BaseSize::Medium && settings.sensitivity==Sensitivity::Medium,"new global defaults are automatic on and32/medium");
	for(int invalid:{-1,0,15,33,160,999})expect(RestoreBaseSize(invalid)==BaseSize::Medium,"invalid size restores to32");
	for(int invalid:{-1,3,160,999})expect(RestoreSensitivity(invalid)==Sensitivity::Medium,"invalid sensitivity restores to medium");
	settings.entries[0].kind=EraserKind::Fixed;settings.entries[3].penResponse=PenResponseChoice::ScreenPen;settings.entries[4].penResponse=PenResponseChoice::Tablet;
	const auto entries=settings.entries;
	expect(GetAutomaticState(settings)==AutomaticState::On,"master switch is independent of mixed entry preferences");
	expect(!SetGlobalAutomatic(settings,true),"no-op operation is observable");
	expect(SetGlobalAutomatic(settings,false),"master switch reports a real state change");
	expect(GetAutomaticState(settings)==AutomaticState::Off && settings.entries==entries && settings.entries[3].penResponse==PenResponseChoice::ScreenPen && settings.entries[4].penResponse==PenResponseChoice::Tablet,
		"global command preserves entry kinds and pen responses");
	expect(SetGlobalAutomatic(settings,true) && settings.entries==entries,"reenabling restores saved mixed entry preferences");
	for(auto size:{BaseSize::Small,BaseSize::Medium,BaseSize::Large})for(float dpi:{1.0f,1.5f,2.0f})
	for(auto sourceKind:{SourceKind::Mouse,SourceKind::ExternalPen,SourceKind::IntegratedPen,SourceKind::Touch,SourceKind::TouchPad,SourceKind::Unknown})
	for(int route=0;route<3;++route)
	{
		InputSettings p;p.baseSize=size;
		DisplayScale d;d.dipPerPixelX=d.dipPerPixelY=1/dpi;d.monitor=1;d.pixelWidth=1920;d.pixelHeight=1080;d.logicalOutputKnown=true;
		d.physicalAvailable=route==0;d.cmPerPixelX=30.0f/1920;d.cmPerPixelY=20.0f/1080;
		InputSource source;source.kind=sourceKind;source.mappedMonitor=1;source.mappedWidth=1920;source.mappedHeight=1080;
		const auto mode=route==2?DeviceMode::LargeScreen:DeviceMode::Laptop;
		const auto entry=sourceKind==SourceKind::Touch?InputEntry::Touch:sourceKind==SourceKind::Mouse?InputEntry::MouseLeft:InputEntry::PenTip;
		const auto config=ResolveInput(d,mode,source,entry,p).config;
		const float b=static_cast<float>(size);
		expect(config.sizes.minimumDiameterDip==b/2 && config.sizes.standardDiameterDip==b && config.sizes.maximumDiameterDip==b*5 && config.sizes.touchStartDiameterDip==b/2,"ordered size ranges derive only from B");
		expect(std::abs(FixedDiameterPx(config.sizes.fixedDiameterDip,d)-b*dpi)<0.001f,"fixed and preview share true DIP units");
		const auto baseline=ResolveConfig(d,mode,source);
		if(size==BaseSize::Medium)expect(config==baseline,"medium/medium is exactly the reference resolver");
		for(auto sensitivity:{Sensitivity::Low,Sensitivity::Medium,Sensitivity::High})
		{
			p.sensitivity=sensitivity;const auto c=ResolveInput(d,mode,source,entry,p).config;
			expect(c.sizes==config.sizes && c.fineHoldSpeed==config.fineHoldSpeed && c.contactArea==config.contactArea && c.evidenceStartSeconds==config.evidenceStartSeconds,"sensitivity preserves size, fine-band, area and time gates");
			expect(ReferenceTargetDiameterDip(c,c.fineHoldSpeed/2)==b/2,"fine plateau is unchanged at every sensitivity");
			Controller tap;tap.Reset(0,0,0,StartKind::Touch,c);tap.UpdatePosition(0,0,0.02);tap.Advance(1);
			expect(tap.DiameterDip()<=b,"stationary touch never becomes a sweep");
		}
	}
	for(auto kind:{SourceKind::Mouse,SourceKind::IntegratedPen,SourceKind::Touch})
	{
		InputSource source;source.kind=kind;
		InputSettings p;
		const auto reference=ResolveInput({},DeviceMode::Laptop,source,InputEntry::MouseLeft,p).config;
		const double speed=(reference.sweepEnterSpeed+reference.largeTargetSpeed)/2;
		float values[3]{};
		for(int level=0;level<3;++level)
		{
			p.sensitivity=static_cast<Sensitivity>(level);const auto c=ResolveInput({},DeviceMode::Laptop,source,InputEntry::MouseLeft,p).config;
			Controller controller;controller.Reset(0,0,0,kind==SourceKind::Touch?StartKind::Touch:StartKind::Hover,c);
			for(int t=1;t<=2000;++t)controller.UpdatePosition(static_cast<float>(speed*t/1000),0,t/1000.0);
			values[level]=controller.DiameterDip();
			expect(std::isfinite(values[level]) && values[level]<=160,"bounded sensitivity replay");
		}
		expect(values[0]<values[1] && values[1]<values[2],"low/medium/high have ordered actual controller effects");
		std::cout<<"[EraserSensitivity] source="<<static_cast<int>(kind)<<" low/medium/high="<<values[0]<<'/'<<values[1]<<'/'<<values[2]<<'\n';
	}
	int layouts=0;
	for(double dpi:{1.0,1.5,2.0})for(double ui:{0.5,0.65,0.75,1.0,1.5,2.0})
	for(bool below:{false,true})for(bool reversed:{false,true})for(double x:{0.0,600.0,1250.0})
	{
		EraserAttributeLayoutInput input;input.dpiScale=dpi;input.zoom=dpi*ui;
		input.work={0,0,1400,900};input.main={x,below?30.0:790.0,x+380,below?110.0:870.0};
		input.anchor={x+150,input.main.top,x+220,input.main.bottom};input.below=below;input.reversed=reversed;
		const auto l=ResolveEraserAttributeLayout(input);++layouts;
		expect(l.panel.Height()==input.main.Height() && l.panel.left>=input.work.left && l.panel.right<=input.work.right+0.001,"panel follows actual height and stays in workspace");
		expect(l.menu.left>=0 && l.menu.right<=1400 && l.menu.top>=0 && l.menu.bottom<=900,"popup stays in work area");
		for(size_t i=0;i<3;++i)
		{
			expect(std::abs(l.previewDiameters[i]*input.zoom-input.diametersDip[i]*dpi)<0.001,"preview pixels are independent of custom UI scale");
			expect(l.items[i+1].Width()>=0 && l.items[i+1].Width()<=(std::max)(BarButtonOneSideDip,l.previewDiameters[i]+BarButtonGapDip*2)+0.001,"circle hit expansion is bounded without70DIP slots");
			expect(std::abs((l.items[i+1].top+l.items[i+1].bottom)-(l.panel.top+l.panel.bottom))<0.001,"all circle centers vertically centered");
		}
		expect(l.items[1].right<=l.items[2].left+0.001 && l.items[2].right<=l.items[3].left+0.001,"circle hit regions remain disjoint after clipping");
		if(reversed)expect(l.items[4].right<l.items[0].left && l.items[0].right<l.previews[0].left,"B reversal exchanges complete side groups");
		else expect(l.previews[2].right<l.items[0].left && l.items[0].right<l.items[4].left,"B standard circles/clear/automatic order");
		const double expectedClear=(std::clamp)(EraserRectCenterX(input.anchor),input.work.left+BarButtonTwoSideDip/2,input.work.right-BarButtonTwoSideDip/2);
		expect(std::abs(EraserRectCenterX(l.items[0])-expectedClear)<0.001,"clear stays anchored while asymmetric sides extend independently");
		const double gap1=l.previews[1].left-l.previews[0].right,gap2=l.previews[2].left-l.previews[1].right;
		expect(std::abs(gap1-16.0)<0.001 && std::abs(gap2-16.0)<0.001,"normal circle edge gaps remain 16 DIP");
		input.diametersDip[2]=240;const auto huge=ResolveEraserAttributeLayout(input);
		expect(huge.previewDiameters[2]*input.zoom==240*dpi && huge.panel.Height()==input.main.Height(),"oversized test circle remains round at real diameter and does not expand panel");
		input.work.right=300;const auto narrow=ResolveEraserAttributeLayout(input);
		const double narrowCircleEndGap=input.reversed
			?narrow.previews[0].left-narrow.dividers[1].right
			:narrow.dividers[0].left-narrow.previews[2].right;
		const double narrowAutomaticGap=input.reversed
			?narrow.dividers[0].left-narrow.automatic.right
			:narrow.automatic.left-narrow.dividers[1].right;
		expect(narrow.horizontalOverflow && narrow.panel.Width()<=300.001 && narrow.previewDiameters==huge.previewDiameters &&
			narrow.items[0].Width()==BarButtonTwoSideDip && narrow.automatic.Width()==BarButtonTwoSideDip+EraserAttributeAutomaticArrowWidthDip &&
			narrow.items[4].Width()==BarButtonTwoSideDip && narrow.items[5].Width()==EraserAttributeAutomaticArrowWidthDip &&
			std::abs((narrow.previews[1].left-narrow.previews[0].right)-EraserAttributeCircleGapDip)<0.001 &&
			std::abs((narrow.previews[2].left-narrow.previews[1].right)-EraserAttributeCircleGapDip)<0.001 &&
			std::abs(narrowCircleEndGap-EraserAttributeCircleGapDip)<0.001 &&
			std::abs(narrow.items[0].left-narrow.dividers[0].right-EraserAttributeStandardGapDip)<0.001 &&
			std::abs(narrow.dividers[1].left-narrow.items[0].right-EraserAttributeStandardGapDip)<0.001 &&
			std::abs(narrowAutomaticGap-EraserAttributeStandardGapDip)<0.001,
			"narrow workspace clips normal-layout overflow without compacting normal gaps or controls");
		input.lockedMenuSide=1;const auto locked=ResolveEraserAttributeLayout(input);expect(locked.menuBelow,"popup direction lock is independent of pointer");
	}
	{
		EraserAttributeLayoutInput beforeDrag;
		beforeDrag.work={0,0,1000,1000};beforeDrag.main={300,600,680,680};beforeDrag.anchor={450,605,520,675};
		beforeDrag.below=false;
		const auto presented=ResolveEraserAttributeLayout(beforeDrag);
		auto moving=beforeDrag;moving.work={0,550,1000,1550};
		const auto snapped=ResolveEraserAttributeLayout(moving);
		moving.lockPanelSide=true;
		const auto held=ResolveEraserAttributeLayout(moving);
		expect(!presented.below && snapped.below && !held.below,
			"drag placement lock preserves the presented panel direction until release");
	}
	expect(ResolveEraserAttributeRelease(-1,0,true,false)==-1,"opening Up without a new panel Down can never clear");
	expect(ResolveEraserAttributeRelease(4,5,true,false)==4 && ResolveEraserAttributeRelease(5,4,true,false)==5,"split action remains owned by Down region");
	expect(ResolveEraserAttributeRelease(1,2,true,false)==-1 && ResolveEraserAttributeRelease(2,2,true,false)==2,
		"size preset release must stay inside the same circular action");
	expect(ResolveEraserAttributeRelease(4,5,true,true)==-1 && ResolveEraserAttributeRelease(4,-1,false,false)==-1,"cancel and external release produce no action");
	// 直接推进生产surface motion：Back过冲、关闭可见性、反向连续、父子锚点与禁用动画。
	{
		EraserSurfaceMotion motion;
		BarUiAnimationAdvanceContextClass context{1.0/60,1,true,false};
		motion.Retarget(true,0.4);double openingPeak=0,closingPeak=0;
		for(int f=0;f<30;++f){motion.Advance(context);openingPeak=(std::max)(openingPeak,motion.Geometry());expect(motion.Opacity()>=0 && motion.Opacity()<=1,"alpha remains bounded during overshoot");}
		expect(openingPeak>1.01 && motion.Geometry()==1 && !motion.Active(),"opening has real geometric overshoot and returns exactly to1");
		motion.Retarget(false,0.4);
		for(int f=0;f<30;++f){motion.Advance(context);closingPeak=(std::max)(closingPeak,motion.Geometry());if(f<20)expect(motion.Visible(),"EaseInBack close does not disappear during early negative curve phase");}
		expect(closingPeak>1.01 && !motion.Visible() && !motion.Active(),"closing expands briefly then fully retires");
		motion.Retarget(true,0.4);for(int f=0;f<7;++f)motion.Advance(context);
		const double before=motion.Geometry();motion.Retarget(false,0.4);motion.Advance({0,1,true,false});
		expect(motion.Geometry()==before,"mid-animation reversal starts from current value");
		motion.Retarget(true,0.4);motion.Advance({0,1,false,false});
		expect(motion.Geometry()==1 && motion.Opacity()==1 && !motion.Active(),"disabled animation settles immediately");
		motion.Retarget(false,0.4);motion.Advance({0,1,false,false});expect(!motion.Visible(),"disabled close has no hidden hit surface");
		BarUiTimelineClass parent;parent.Restart(0.4);parent.Advance(0.1,1);
		motion.Retarget(true,0.4,&parent);expect(std::abs(motion.Timeline().GetRemainingDuration()-0.3)<0.001,"child joins parent's existing first-half timeline");
		for(int f=0;f<10;++f)motion.Advance({1.0/60,2,true,false});expect(!motion.Active(),"global speed changes the same frame clock");
		const EraserAttributeRect panel{200,300,558,380},anchor{344,390,414,460},automatic{430,305,532.5,375},menu{390,200,572.5,290};
		for(double g:{0.0,0.2,0.8,1.04,1.0})
		{
			const auto p=ResolveEraserSurfacePose(panel,anchor,g,60);
			const auto c=ComposeEraserPose(p,ResolveEraserSurfacePose(menu,automatic,0,60));
			expect(std::abs(EraserRectCenterX(c.Apply(menu))-EraserRectCenterX(p.Apply(automatic)))<0.001 && std::abs(EraserRectCenterY(c.Apply(menu))-EraserRectCenterY(p.Apply(automatic)))<0.001,"closed child follows actual moving parent anchor");
			if(g>1)expect(p.Apply(panel).Width()>panel.Width(),"bounds must include actual overshoot, not final shell");
		}
		std::cout<<"[EraserMotion] openingPeak="<<openingPeak<<" closingPeak="<<closingPeak<<'\n';
	}
	std::cout<<"[EraserAttribute] layouts="<<layouts<<" failures="<<failures<<'\n';
	return failures;
}
