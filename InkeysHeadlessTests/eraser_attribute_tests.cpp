#include "../Inkeys/Inkeys/Drawing/Draw3/Draw3.SpeedEraser.h"
#include "../Inkeys/Inkeys/Drawing/Draw3/Draw3.Bridge.h"
#include <cmath>
#include <iostream>

import Inkeys.UI.Bar.EraserAttributeLayout;

int RunEraserAttributeTests()
{
	using namespace Inkeys::Drawing::Draw3::SpeedEraser;
	using namespace Inkeys::UI::Bar;
	int failures=0;
	auto expect=[&](bool value,const char* message){if(!value){++failures;std::cerr<<"[EraserAttribute] "<<message<<'\n';}};
	InputSettings settings;
	expect(settings.baseSize==BaseSize::Medium && settings.sensitivity==Sensitivity::Medium,"new global defaults are 32/medium");
	for(int invalid:{-1,0,15,33,160,999})expect(RestoreBaseSize(invalid)==BaseSize::Medium,"invalid size restores to32");
	for(int invalid:{-1,3,160,999})expect(RestoreSensitivity(invalid)==Sensitivity::Medium,"invalid sensitivity restores to medium");
	settings.entries[0].kind=EraserKind::Fixed;settings.entries[3].penResponse=PenResponseChoice::ScreenPen;settings.entries[4].penResponse=PenResponseChoice::Tablet;
	expect(GetAutomaticState(settings)==AutomaticState::Mixed,"mixed does not pretend to be mouse-left state");
	expect(SetGlobalAutomatic(settings,GetAutomaticState(settings)!=AutomaticState::On),"mixed click changes table");
	expect(GetAutomaticState(settings)==AutomaticState::On,"mixed click turns all on");
	expect(!SetGlobalAutomatic(settings,true),"no-op operation is observable");
	SetGlobalAutomatic(settings,false);
	expect(GetAutomaticState(settings)==AutomaticState::Off && settings.entries[3].penResponse==PenResponseChoice::ScreenPen && settings.entries[4].penResponse==PenResponseChoice::Tablet,"global command preserves pen responses");
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
	for(double dpi:{1.0,1.5,2.0})for(double ui:{0.5,0.75,1.0,1.5,2.0})
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
			expect(l.items[i+1].Width()>=l.previewDiameters[i] && l.items[i+1].Width()>=BarButtonTwoSideDip,"preview fits its own usable click slot");
			expect(std::abs((l.items[i+1].top+l.items[i+1].bottom)-(l.panel.top+l.panel.bottom))<0.001,"all circle centers vertically centered");
		}
		if(reversed)expect(l.items[0].left>l.items[4].right,"groups mirror without rotating content");
		else expect(l.items[0].right<l.items[1].left && l.items[3].right<l.items[4].left,"standard clear/sizes/automatic order");
		input.diametersDip[2]=240;const auto huge=ResolveEraserAttributeLayout(input);
		expect(huge.previewDiameters[2]*input.zoom==240*dpi && huge.panel.Height()==input.main.Height(),"oversized test circle remains round at real diameter and does not expand panel");
		input.work.right=300;const auto narrow=ResolveEraserAttributeLayout(input);
		expect(narrow.horizontalOverflow && narrow.panel.Width()<=300.001 && narrow.previewDiameters==huge.previewDiameters,"narrow workspace clips only center, never scales the panel or preview");
		input.lockedMenuSide=1;const auto locked=ResolveEraserAttributeLayout(input);expect(locked.menuBelow,"popup direction lock is independent of pointer");
	}
	std::cout<<"[EraserAttribute] layouts="<<layouts<<" failures="<<failures<<'\n';
	return failures;
}
