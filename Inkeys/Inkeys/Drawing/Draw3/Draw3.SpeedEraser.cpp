#include "Draw3.SpeedEraser.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace Inkeys::Drawing::Draw3::SpeedEraser
{
	namespace
	{
		double Positive(double value, double fallback) noexcept
		{
			return std::isfinite(value) && value > 0.0 ? value : fallback;
		}

		double SmoothStep(double value) noexcept
		{
			value = std::clamp(value, 0.0, 1.0);
			return value * value * (3.0 - 2.0 * value);
		}

		// 比例限速段与指数段的精确解，长帧不会越过目标或丢失经过的时间。
		double Follow(double current, double target, double seconds,
			double tau, double rate) noexcept
		{
			const double gap = std::abs(target - current);
			const double limit = tau * rate;
			const double limitedTime = gap > limit ? (gap - limit) / rate : 0.0;
			const double spent = std::min(seconds, limitedTime);
			const double remainder = (gap - rate * spent) *
				std::exp(-(seconds - spent) / tau);
			return target + (current > target ? remainder : -remainder);
		}
	}


	float DiameterToCanvasPx(float diameterDip, const DisplayScale& display) noexcept
	{
		const double unitsPerPixel = std::sqrt(Positive(display.dipPerPixelX,1.0) *
			Positive(display.dipPerPixelY,1.0));
		return static_cast<float>(Positive(diameterDip,32.0) / unitsPerPixel);
	}

	float FixedDiameterPx(float selectedDiameterDip, const DisplayScale& display) noexcept
	{
		return DiameterToCanvasPx(selectedDiameterDip,display);
	}

	SourceKind ClassifySource(uint32_t actualInputType, bool modernApiAvailable,
		bool capabilitiesKnown, bool integrated, SourceKind pointerKind,
		bool pointerMatched, bool pointerAmbiguous) noexcept
	{
		if (actualInputType == 2 || actualInputType == 3) return SourceKind::Mouse;
		// Win7 不扩展自动物理识别；能力存在也不等于当前来源/表面已被确认。
		if (!modernApiAvailable || pointerAmbiguous) return SourceKind::Unknown;
		if (actualInputType == 1)
		{
			if (pointerMatched)
			{
				if (pointerKind != SourceKind::IntegratedPen && pointerKind != SourceKind::ExternalPen)
					return SourceKind::Unknown;
				if (capabilitiesKnown && integrated != (pointerKind == SourceKind::IntegratedPen))
					return SourceKind::Unknown;
				return pointerKind;
			}
			return capabilitiesKnown ? (integrated ? SourceKind::IntegratedPen : SourceKind::ExternalPen)
				: SourceKind::Unknown;
		}
		if (actualInputType == 0)
		{
			if (pointerMatched && pointerKind == SourceKind::TouchPad) return SourceKind::TouchPad;
			if (pointerMatched && pointerKind != SourceKind::Touch) return SourceKind::Unknown;
			if (capabilitiesKnown && !integrated) return SourceKind::Unknown;
			if (pointerMatched || (capabilitiesKnown && integrated)) return SourceKind::Touch;
		}
		return SourceKind::Unknown;
	}

	const char* SourceKindName(SourceKind kind) noexcept
	{
		switch (kind)
		{
		case SourceKind::Mouse: return "Mouse";
		case SourceKind::ExternalPen: return "External Pen";
		case SourceKind::IntegratedPen: return "Integrated Pen";
		case SourceKind::Touch: return "Screen Touch";
		case SourceKind::TouchPad: return "Touch Pad";
		default: return "Unknown";
		}
	}
	const char* ResponseModelName(ResponseModel model) noexcept
	{
		switch (model)
		{
		case ResponseModel::ScreenPenHybrid: return "ScreenPenHybrid";
		case ResponseModel::DirectTouch: return "DirectTouch";
		default: return "IndirectDip";
		}
	}
	const char* ScaleSourceName(ScaleSource source) noexcept
	{
		switch (source)
		{
		case ScaleSource::TrustedPhysical: return "TrustedPhysical";
		case ScaleSource::ManualCalibration: return "ManualCalibration";
		case ScaleSource::ResolutionDpiHeuristic: return "ResolutionDpiHeuristic";
		default: return "DipOnly";
		}
	}
	const char* MotionUnitName(MotionUnit unit) noexcept
	{
		switch (unit)
		{
		case MotionUnit::MillimetersPerSecond: return "mm/s";
		case MotionUnit::HeuristicPerSecond: return "reference DIP/s";
		default: return "DIP/s";
		}
	}

	float ResolutionDpiActionGain(const DisplayScale& display) noexcept
	{
		if (!display.logicalOutputKnown || display.pixelWidth <= 0 || display.pixelHeight <= 0) return 1;
		const double x = display.pixelWidth * Positive(display.dipPerPixelX,1);
		const double y = display.pixelHeight * Positive(display.dipPerPixelY,1);
		// Draw2 的 1920x1080 分辨率参考迁入 DIP；旋转不改变增益，不假定屏幕英寸。
		const double extent = std::min(std::max(x,y)/1920.0, std::min(x,y)/1080.0);
		return static_cast<float>(1.0/std::clamp(extent,0.5,4.0));
	}

	Config ResolveConfig(const DisplayScale& display, DeviceMode mode, const InputSource& source,
		const EraserSizes& sizes) noexcept
	{
		Config config;
		config.display = display;
		config.mode = mode;
		config.inputSource = source;
		config.touchContactAreaAssistance = display.development.touchContactAreaAssistance;
		config.sizes = sizes;
		config.sizes.minimumDiameterDip = static_cast<float>(Positive(sizes.minimumDiameterDip,16.0));
		config.sizes.maximumDiameterDip = std::max(config.sizes.minimumDiameterDip,
			static_cast<float>(Positive(sizes.maximumDiameterDip,160.0)));
		config.sizes.standardDiameterDip = std::clamp(static_cast<float>(Positive(sizes.standardDiameterDip,32.0)),
			config.sizes.minimumDiameterDip,config.sizes.maximumDiameterDip);
		config.sizes.touchStartDiameterDip = std::clamp(static_cast<float>(Positive(sizes.touchStartDiameterDip,16.0)),
			config.sizes.minimumDiameterDip,config.sizes.standardDiameterDip);
		config.sizes.fixedDiameterDip = static_cast<float>(Positive(sizes.fixedDiameterDip,50.0));
		config.minimumDiameterPx = DiameterToCanvasPx(config.sizes.minimumDiameterDip,display);
		config.maximumDiameterPx = DiameterToCanvasPx(config.sizes.maximumDiameterDip,display);
		config.response = source.kind == SourceKind::IntegratedPen ? ResponseModel::ScreenPenHybrid
			: source.kind == SourceKind::Touch ? ResponseModel::DirectTouch : ResponseModel::IndirectDip;
		switch (display.development.response)
		{
		case ResponseOverride::IndirectDip: config.response=ResponseModel::IndirectDip; break;
		case ResponseOverride::ScreenPenHybrid: config.response=ResponseModel::ScreenPenHybrid; break;
		case ResponseOverride::DirectTouch: config.response=ResponseModel::DirectTouch; break;
		default: break;
		}
		const double dipX=Positive(display.dipPerPixelX,1), dipY=Positive(display.dipPerPixelY,1);
		config.motionPerPixelX=static_cast<float>(dipX);
		config.motionPerPixelY=static_cast<float>(dipY);
		config.penBeta=std::isfinite(display.development.penBeta)
			? std::clamp(display.development.penBeta,0.0f,1.0f) : 0.5f;
		config.inputMapped=display.logicalOutputKnown && source.mappedMonitor!=0 &&
			source.mappedMonitor==display.monitor && source.mappedWidth==display.pixelWidth &&
			source.mappedHeight==display.pixelHeight && source.mappedLeft==display.desktopLeft &&
			source.mappedTop==display.desktopTop;
		const bool direct=config.response!=ResponseModel::IndirectDip;
		const bool forced=display.development.response!=ResponseOverride::Automatic;
		double mmX=0, mmY=0;
		const auto& calibration=display.development.calibration;
		const bool manual=display.development.scale==ScaleOverride::ManualSurface &&
			calibration.monitor!=0 && calibration.monitor==display.monitor && display.logicalOutputKnown &&
			display.pixelWidth>0 && display.pixelHeight>0 &&
			std::isfinite(calibration.widthCm) && std::isfinite(calibration.heightCm) &&
			calibration.widthCm>=5 && calibration.heightCm>=5 &&
			calibration.widthCm<=1000 && calibration.heightCm<=1000;
		if (direct && manual && (config.inputMapped || forced))
		{
			const bool rotated=((calibration.orientation ^ display.orientation)&1u)!=0;
			mmX=10.0*(rotated?calibration.heightCm:calibration.widthCm)/display.pixelWidth;
			mmY=10.0*(rotated?calibration.widthCm:calibration.heightCm)/display.pixelHeight;
			config.motionSource=ScaleSource::ManualCalibration;
		}
		else if (direct && display.development.scale==ScaleOverride::Automatic &&
			config.inputMapped && display.physicalAvailable)
		{
			mmX=10.0*display.cmPerPixelX; mmY=10.0*display.cmPerPixelY;
			config.motionSource=ScaleSource::TrustedPhysical;
		}
		const double rho=std::sqrt(mmX/dipX)*std::sqrt(mmY/dipY);
		const bool physical=std::isfinite(mmX) && std::isfinite(mmY) && mmX>0 && mmY>0 &&
			std::isfinite(rho) && rho>=0.01 && rho<=10.0;
		if (physical)
		{
			config.motionPerPixelX=static_cast<float>(mmX);
			config.motionPerPixelY=static_cast<float>(mmY);
			config.motionUnit=MotionUnit::MillimetersPerSecond;
			config.rhoMmPerDip=static_cast<float>(rho);
		}
		else
		{
			config.motionSource=ScaleSource::DipOnly;
			if (direct && (mode==DeviceMode::LargeScreen || display.development.scale==ScaleOverride::ForceUnavailable) && display.logicalOutputKnown)
			{
				config.motionSource=ScaleSource::ResolutionDpiHeuristic;
				config.motionUnit=MotionUnit::HeuristicPerSecond;
				config.heuristicGain=ResolutionDpiActionGain(display);
				config.motionPerPixelX*=config.heuristicGain;
				config.motionPerPixelY*=config.heuristicGain;
			}
		}
		if (config.response==ResponseModel::ScreenPenHybrid)
		{
			config.fineToStandardSpeed=physical?20.0f:80.0f;
			config.sweepEnterSpeed=physical?120.0f:480.0f;
			config.sweepExitSpeed=physical?80.0f:320.0f;
			config.largeTargetSpeed=physical?350.0f:1400.0f;
			config.historyWindowSeconds=0.040;
			config.evidenceStartSeconds=0.060;
			config.evidenceFullSeconds=0.160;
			config.evidenceDecaySeconds=0.280;
			config.growthTauSeconds=0.140;
			config.largeGrowthTauSeconds=0.120;
			config.holdSeconds=config.decreaseConfirmationSeconds=0.080;
			config.sweepHoldSeconds=0.200;
			config.sweepDecreaseConfirmationSeconds=0.240;
			config.shrinkTauSeconds=0.120;
			config.sweepShrinkTauSeconds=0.220;
			config.idleStartSeconds=0.220;
			config.idleTauSeconds=0.160;
		}
		else if (config.response==ResponseModel::DirectTouch)
		{
			const bool heuristic=config.motionSource==ScaleSource::ResolutionDpiHeuristic;
			config.fineToStandardSpeed=physical?30.0f:100.0f;
			config.sweepEnterSpeed=physical?90.0f:heuristic?120.0f:240.0f;
			config.sweepExitSpeed=physical?60.0f:heuristic?80.0f:160.0f;
			config.largeTargetSpeed=physical?250.0f:heuristic?400.0f:700.0f;
			// Touch 单独减轻资格和扩大阻力；不修改鼠标/屏幕笔的任何参数。
			config.historyWindowSeconds=0.050;
			config.evidenceStartSeconds=0.025;
			config.evidenceFullSeconds=0.060;
			config.evidenceDecaySeconds=0.180;
			config.growthTauSeconds=0.120;
			config.largeGrowthTauSeconds=0.100;
			config.maximumLogGrowthPerSecond=6.0;
			config.largeLogGrowthPerSecond=8.0;
		}
		// 间接设备始终按映射后的 DIP 动作，不因大屏选项或 EDID 改为物理测速。
		config.movementNoiseDistance=physical?0.2f:0.75f;
		config.touchUnlockStart=physical?1.0f:2.0f;
		config.touchUnlockEnd=physical?3.0f:6.0f;
		return config;
	}

	Config ResolveConfig(const DisplayScale& display, DeviceMode mode, bool touch, const EraserSizes& sizes) noexcept
	{
		InputSource source;
		source.kind=touch?SourceKind::Touch:SourceKind::Mouse;
		if (touch && display.directTouchMapped)
		{
			source.mappedMonitor=display.monitor;
			source.mappedLeft=display.desktopLeft;source.mappedTop=display.desktopTop;
			source.mappedWidth=display.pixelWidth;source.mappedHeight=display.pixelHeight;
		}
		return ResolveConfig(display,mode,source,sizes);
	}

	float ReferenceTargetDiameterDip(const Config& config, double speed) noexcept
	{
		speed=std::isfinite(speed)?std::max(0.0,speed):0;
		const double minimum=config.sizes.minimumDiameterDip, standard=config.sizes.standardDiameterDip;
		if (speed<config.fineToStandardSpeed)
			return static_cast<float>(minimum+(standard-minimum)*SmoothStep(speed/config.fineToStandardSpeed));
		const double amount=SmoothStep((speed-config.sweepEnterSpeed)/(config.largeTargetSpeed-config.sweepEnterSpeed));
		return static_cast<float>(standard*std::exp(amount*std::log(config.sizes.maximumDiameterDip/standard)));
	}

	float CompensateTargetDiameterDip(const Config& config, float referenceDip, bool* limited) noexcept
	{
		const double standard=config.sizes.standardDiameterDip;
		double target=Positive(referenceDip,standard);
		if (target>standard && config.rhoMmPerDip>0)
		{
			const double ratio=config.referenceMmPerDip/config.rhoMmPerDip;
			if (config.response==ResponseModel::ScreenPenHybrid)
				target=standard+std::clamp(std::pow(ratio,config.penBeta),0.25,4.0)*(target-standard);
			// DirectTouch 的动态目标直接用 DIP，物理信息只参与它的动作测速。
		}
		const double bounded=std::clamp(target,static_cast<double>(config.sizes.minimumDiameterDip),
			static_cast<double>(config.sizes.maximumDiameterDip));
		if (limited) *limited=std::abs(bounded-target)>0.0001;
		return static_cast<float>(bounded);
	}

	ContactLengthTransform ResolveContactLengthTransform(const ContactLengthMetrics& axis,
		const ContactLengthMetrics& span, float positionScale) noexcept
	{
		ContactLengthTransform result;
		result.logicalMin=span.logicalMin;result.logicalMax=span.logicalMax;
		const auto fail=[&](ContactAreaUnits status){result.status=status;return result;};
		if(!span.present)return fail(ContactAreaUnits::Missing);
		if(!axis.present)return fail(ContactAreaUnits::MissingAxis);
		if(axis.units==0)return fail(ContactAreaUnits::AxisUnitsMissing);
		if(span.units==0)return fail(ContactAreaUnits::SpanUnitsMissing);
		// 只接受文档明确的长度单位；SI/English 扩展枚举的额外语义不在此猜测。
		const auto centimeters=[](uint32_t units){return units==1?2.54:units==2?1.0:0.0;};
		const double axisCm=centimeters(axis.units),spanCm=centimeters(span.units);
		if(axisCm==0 || spanCm==0)return fail(ContactAreaUnits::UnsupportedLengthUnits);
		if(!std::isfinite(axis.resolution) || axis.resolution<=0)return fail(ContactAreaUnits::InvalidAxisResolution);
		if(!std::isfinite(span.resolution) || span.resolution<=0)return fail(ContactAreaUnits::InvalidSpanResolution);
		if(axis.logicalMax<=axis.logicalMin)return fail(ContactAreaUnits::InvalidAxisRange);
		if(span.logicalMin<0 || span.logicalMax<=span.logicalMin)return fail(ContactAreaUnits::InvalidSpanRange);
		if(!std::isfinite(positionScale) || positionScale==0)return fail(ContactAreaUnits::InvalidPositionScale);
		// 相对长度只经过现有位置变换的线性部分：不减逻辑原点、不用平移、不再乘 contextScale。
		// rawSpan / spanResolution * (spanCm / axisCm) * axisResolution * abs(positionScale)
		result.spanToAxis=static_cast<double>(axis.resolution)/span.resolution*(spanCm/axisCm);
		result.spanToCanvas=result.spanToAxis*std::abs(static_cast<double>(positionScale));
		if(!std::isfinite(result.spanToCanvas) || result.spanToCanvas<=0 ||
			result.spanToCanvas>std::numeric_limits<float>::max())
			return fail(ContactAreaUnits::ConversionNonFinite);
		result.unitConverted=axis.units!=span.units;
		result.resolutionAdjusted=axis.resolution!=span.resolution;
		result.status=ContactAreaUnits::CanvasPixels;
		return result;
	}

	ContactAreaSample ConvertContactArea(float rawWidth,float rawHeight,
		const ContactLengthTransform& width,const ContactLengthTransform& height) noexcept
	{
		ContactAreaSample result;result.rawWidth=rawWidth;result.rawHeight=rawHeight;
		result.units=width.status!=ContactAreaUnits::CanvasPixels?width.status:height.status;
		if(result.units!=ContactAreaUnits::CanvasPixels)return result;
		const double w=rawWidth*width.spanToCanvas,h=rawHeight*height.spanToCanvas;
		if(!std::isfinite(rawWidth) || !std::isfinite(rawHeight) || !std::isfinite(w) || !std::isfinite(h) ||
			w>std::numeric_limits<float>::max() || h>std::numeric_limits<float>::max())
		{result.units=ContactAreaUnits::ConversionNonFinite;return result;}
		if(static_cast<double>(rawWidth)<width.logicalMin || static_cast<double>(rawWidth)>width.logicalMax ||
			static_cast<double>(rawHeight)<height.logicalMin || static_cast<double>(rawHeight)>height.logicalMax)
		{result.units=ContactAreaUnits::OutsideMetrics;return result;}
		result.widthPx=static_cast<float>(w);result.heightPx=static_cast<float>(h);
		return result;
	}

	ContactAreaReason ContactAreaMetadataReason(ContactAreaUnits units) noexcept
	{
		switch(units)
		{
		case ContactAreaUnits::Missing:return ContactAreaReason::Missing;
		case ContactAreaUnits::OutsideMetrics:return ContactAreaReason::OutsideMetrics;
		case ContactAreaUnits::MissingAxis:return ContactAreaReason::MissingAxis;
		case ContactAreaUnits::AxisUnitsMissing:return ContactAreaReason::AxisUnitsMissing;
		case ContactAreaUnits::SpanUnitsMissing:return ContactAreaReason::SpanUnitsMissing;
		case ContactAreaUnits::UnsupportedLengthUnits:return ContactAreaReason::UnsupportedLengthUnits;
		case ContactAreaUnits::InvalidAxisResolution:case ContactAreaUnits::InvalidSpanResolution:return ContactAreaReason::InvalidResolution;
		case ContactAreaUnits::InvalidAxisRange:case ContactAreaUnits::InvalidSpanRange:return ContactAreaReason::InvalidRange;
		case ContactAreaUnits::InvalidPositionScale:return ContactAreaReason::InvalidTransform;
		case ContactAreaUnits::ConversionNonFinite:return ContactAreaReason::NonFinite;
		default:return ContactAreaReason::UnitsUnknown;
		}
	}

	const char* ContactPropertyUnitName(uint32_t units) noexcept
	{
		constexpr const char* names[]={"DEFAULT/unknown","inches","centimeters","degrees","radians",
			"seconds","pounds","grams","SI-linear/unsupported","SI-rotation","English-linear/unsupported",
			"English-rotation","slugs","kelvin","fahrenheit","ampere","candela"};
		return units<std::size(names)?names[units]:"unrecognized";
	}
	const char* ContactAreaUnitsName(ContactAreaUnits units) noexcept
	{
		switch(units)
		{
		case ContactAreaUnits::CanvasPixels:return "canvas-pixels";
		case ContactAreaUnits::Unverified:return "unverified";
		case ContactAreaUnits::OutsideMetrics:return "outside-metrics";
		case ContactAreaUnits::MissingAxis:return "unverified-missing-axis";
		case ContactAreaUnits::AxisUnitsMissing:return "unverified-axis-units-missing";
		case ContactAreaUnits::SpanUnitsMissing:return "unverified-span-units-missing";
		case ContactAreaUnits::UnsupportedLengthUnits:return "unverified-unsupported-length-unit";
		case ContactAreaUnits::InvalidAxisResolution:return "unverified-invalid-axis-resolution";
		case ContactAreaUnits::InvalidSpanResolution:return "unverified-invalid-span-resolution";
		case ContactAreaUnits::InvalidAxisRange:return "unverified-invalid-axis-range";
		case ContactAreaUnits::InvalidSpanRange:return "unverified-invalid-span-range";
		case ContactAreaUnits::InvalidPositionScale:return "unverified-invalid-position-scale";
		case ContactAreaUnits::ConversionNonFinite:return "unverified-nonfinite-conversion";
		default:return "missing";
		}
	}

	const char* ContactAreaReasonName(ContactAreaReason reason) noexcept
	{
		switch(reason)
		{
		case ContactAreaReason::Disabled:return "disabled";
		case ContactAreaReason::NotScreenTouch:return "not-screen-touch";
		case ContactAreaReason::MappingUnknown:return "mapping-unknown";
		case ContactAreaReason::Missing:return "missing";
		case ContactAreaReason::UnitsUnknown:return "metadata-unverified";
		case ContactAreaReason::OutsideMetrics:return "outside-packet-metrics";
		case ContactAreaReason::NonFinite:return "non-finite";
		case ContactAreaReason::NonPositive:return "non-positive";
		case ContactAreaReason::TooSmall:return "too-small";
		case ContactAreaReason::TooLarge:return "implausibly-large";
		case ContactAreaReason::AspectRatio:return "aspect-ratio";
		case ContactAreaReason::Outlier:return "outlier";
		case ContactAreaReason::WaitingForMove:return "waiting-for-real-move";
		case ContactAreaReason::Confirming:return "confirming";
		case ContactAreaReason::Ready:return "ready";
		case ContactAreaReason::Expired:return "expired";
		case ContactAreaReason::MissingAxis:return "coordinate-property-missing";
		case ContactAreaReason::AxisUnitsMissing:return "coordinate-units-missing";
		case ContactAreaReason::SpanUnitsMissing:return "contact-units-missing";
		case ContactAreaReason::UnsupportedLengthUnits:return "unsupported-length-unit";
		case ContactAreaReason::InvalidResolution:return "invalid-resolution";
		case ContactAreaReason::InvalidRange:return "invalid-declared-range";
		case ContactAreaReason::InvalidTransform:return "invalid-position-transform";
		}
		return "unknown";
	}

	bool Controller::AreaEligible() const noexcept
	{
		return config_.touchContactAreaAssistance && touchStartup_ &&
			config_.inputSource.kind==SourceKind::Touch && config_.inputMapped;
	}

	void Controller::ObserveContactArea(const ContactAreaSample& sample,double seconds,bool moving) noexcept
	{
		auto& a=area_;
		const auto& p=config_.contactArea;
		const double dt=a.hasSample?seconds-a.lastSampleSeconds:0;
		const bool consecutive=a.hasSample && a.diagnostic.sampleValid && dt>0 && dt<=p.maximumSampleGapSeconds;
		a.hasSample=true;a.lastSampleSeconds=seconds;
		a.diagnostic.sample=sample;a.diagnostic.enabled=config_.touchContactAreaAssistance;
		a.diagnostic.widthDip=a.diagnostic.heightDip=-1;
		a.diagnostic.sampleValid=false;
		auto reject=[&](ContactAreaReason reason)
		{
			a.diagnostic.reason=reason;a.diagnostic.stableMotionSeconds=0;a.diagnostic.sampleValid=false;
			if(a.diagnostic.referenceReady && a.badSince<0)a.badSince=seconds;
		};
		if(sample.units!=ContactAreaUnits::CanvasPixels){reject(ContactAreaMetadataReason(sample.units));return;}
		const float w=sample.widthPx*config_.display.dipPerPixelX;
		const float h=sample.heightPx*config_.display.dipPerPixelY;
		if(!std::isfinite(w) || !std::isfinite(h) || !std::isfinite(sample.rawWidth) || !std::isfinite(sample.rawHeight))
		{reject(ContactAreaReason::NonFinite);return;}
		a.diagnostic.widthDip=w;a.diagnostic.heightDip=h;
		if(w<=0 || h<=0 || sample.rawWidth<=0 || sample.rawHeight<=0){reject(ContactAreaReason::NonPositive);return;}
		if(std::min(w,h)<p.minimumSpanDip){reject(ContactAreaReason::TooSmall);return;}
		// 拒绝阈值与辅助上限分开：巨值不能被“修正”为最大的合法辅助。
		if(std::max(w,h)>p.maximumReportedSpanDip){reject(ContactAreaReason::TooLarge);return;}
		if(std::max(w,h)>std::min(w,h)*p.maximumAspectRatio){reject(ContactAreaReason::AspectRatio);return;}
		// 数据有效性与实验开关分开；关闭辅助仍可诊断真实宽高，但不能建立参考。
		a.diagnostic.sampleValid=true;
		if(!config_.touchContactAreaAssistance){a.diagnostic.reason=ContactAreaReason::Disabled;return;}
		if(!touchStartup_ || config_.inputSource.kind!=SourceKind::Touch){a.diagnostic.reason=ContactAreaReason::NotScreenTouch;return;}
		if(!config_.inputMapped){a.diagnostic.reason=ContactAreaReason::MappingUnknown;return;}
		const auto similar=[](float x,float y,float ratio){return std::max(x,y)<=std::min(x,y)*ratio+1.0f;};
		if(a.diagnostic.referenceReady)
		{
			if(!similar(w,a.referenceWidth,p.outlierRatio) || !similar(h,a.referenceHeight,p.outlierRatio))
			{reject(ContactAreaReason::Outlier);return;}
			a.diagnostic.sampleValid=true;a.diagnostic.reason=ContactAreaReason::Ready;
			a.lastValidSeconds=seconds;a.badSince=-1;
			return; // 本接触参考锁存，不随重压、摊开或噪声继续放大。
		}
		a.diagnostic.sampleValid=true;
		if(!a.hasCandidate || !consecutive || !similar(w,a.candidateWidth,p.confirmationRatio) ||
			!similar(h,a.candidateHeight,p.confirmationRatio))
		{
			a.hasCandidate=true;a.candidateWidth=w;a.candidateHeight=h;a.diagnostic.stableMotionSeconds=0;
		}
		else if(moving)
		{
			const float alpha=static_cast<float>(1.0-std::exp(-dt/Positive(p.filterSeconds,0.050)));
			a.candidateWidth+=(w-a.candidateWidth)*alpha;a.candidateHeight+=(h-a.candidateHeight)*alpha;
			a.diagnostic.stableMotionSeconds+=dt;
		}
		else a.diagnostic.stableMotionSeconds=0;
		a.diagnostic.reason=moving?ContactAreaReason::Confirming:ContactAreaReason::WaitingForMove;
		if(a.diagnostic.stableMotionSeconds+1e-9<Positive(p.confirmationSeconds,0.050))return;
		a.referenceWidth=a.candidateWidth;a.referenceHeight=a.candidateHeight;
		const float upper=std::min(config_.sizes.maximumDiameterDip,
			std::max(config_.sizes.standardDiameterDip,p.maximumFloorDip));
		a.diagnostic.referenceFloorDip=std::clamp(p.multiplier*std::max(a.referenceWidth,a.referenceHeight)+p.paddingDip,
			config_.sizes.standardDiameterDip,upper);
		a.diagnostic.referenceReady=true;a.diagnostic.reason=ContactAreaReason::Ready;
		a.readySeconds=a.lastValidSeconds=seconds;a.badSince=-1;
	}

	double Controller::AreaExpirySeconds() const noexcept
	{
		if(!area_.diagnostic.referenceReady)return std::numeric_limits<double>::infinity();
		const double missing=area_.lastValidSeconds+config_.contactArea.missingTimeoutSeconds;
		return area_.badSince>=0?std::min(missing,area_.badSince+config_.contactArea.invalidGraceSeconds):missing;
	}

	float Controller::AreaReferenceFloor(double seconds) const noexcept
	{
		const float minimum=config_.sizes.minimumDiameterDip;
		if(!AreaEligible() || !area_.diagnostic.referenceReady || seconds<area_.readySeconds)return minimum;
		const double age=std::max(0.0,seconds-AreaExpirySeconds());
		const double weight=std::exp(-age/Positive(config_.contactArea.releaseSeconds,0.180));
		return weight<0.001?minimum:static_cast<float>(minimum+(area_.diagnostic.referenceFloorDip-minimum)*weight);
	}

	void Controller::ShiftAreaTime(double seconds) noexcept
	{
		if(area_.hasSample)area_.lastSampleSeconds+=seconds;
		if(area_.diagnostic.referenceReady)
		{area_.readySeconds+=seconds;area_.lastValidSeconds+=seconds;}
		if(area_.badSince>=0)area_.badSince+=seconds;
	}

	ContactAreaDiagnostics Controller::AreaDiagnostics(double seconds) const noexcept
	{
		auto result=area_.diagnostic;
		const double now=paused_?pauseTime_:seconds;
		result.referenceFresh=result.referenceReady && now<=AreaExpirySeconds();
		result.activeFloorDip=AreaEligible()?static_cast<float>(IdleDiameterDip(frameState_)):0;
		result.active=AreaEligible() && result.referenceReady && result.activeFloorDip>config_.sizes.minimumDiameterDip+0.01f;
		if(result.referenceReady && now>AreaExpirySeconds() && result.reason==ContactAreaReason::Ready)
			result.reason=ContactAreaReason::Expired;
		return result;
	}

	double Controller::NextAreaWakeSeconds() const noexcept
	{
		if(!initialized_ || paused_ || !AreaEligible() || !area_.diagnostic.referenceReady ||
			frameState_.areaFloorDip<=config_.sizes.minimumDiameterDip ||
			DiameterDip()<=config_.sizes.minimumDiameterDip+0.01f)return 0;
		const double deadline=AreaExpirySeconds()+0.004;
		return deadline>frameState_.time?deadline:0;
	}

	void Controller::Reset(float x, float y, double seconds, StartKind kind,
		const Config& config, float initialDiameterDip, const ContactAreaSample* contactArea) noexcept
	{
		config_ = config;
		config_.motionPerPixelX = static_cast<float>(Positive(config_.motionPerPixelX, 1.0));
		config_.motionPerPixelY = static_cast<float>(Positive(config_.motionPerPixelY, 1.0));
		const auto resolved=ResolveConfig(config.display,config.mode,kind==StartKind::Touch,config.sizes);
		config_.sizes=resolved.sizes;
		config_.minimumDiameterPx=resolved.minimumDiameterPx;
		config_.maximumDiameterPx=resolved.maximumDiameterPx;
		config_.sweepEnterSpeed=static_cast<float>(Positive(config.sweepEnterSpeed,resolved.sweepEnterSpeed));
		config_.sweepExitSpeed=std::min(config_.sweepEnterSpeed*0.99f,static_cast<float>(Positive(config.sweepExitSpeed,resolved.sweepExitSpeed)));
		config_.largeTargetSpeed=std::max(config_.sweepEnterSpeed+1.0f,static_cast<float>(Positive(config.largeTargetSpeed,resolved.largeTargetSpeed)));
		config_.movementNoiseDistance=static_cast<float>(Positive(config.movementNoiseDistance,resolved.movementNoiseDistance));
		config_.fineToStandardSpeed=std::min(config_.sweepEnterSpeed*0.9f,
			static_cast<float>(Positive(config.fineToStandardSpeed,resolved.fineToStandardSpeed)));
		config_.touchUnlockStart = static_cast<float>(Positive(config_.touchUnlockStart, 2.0));
		config_.touchUnlockEnd = std::max(config_.touchUnlockStart * 1.01f,
			static_cast<float>(Positive(config_.touchUnlockEnd, 6.0)));
		config_.historyWindowSeconds = Positive(config_.historyWindowSeconds, 0.080);
		const Config defaults;
		config_.referenceWindowSeconds = Positive(config_.referenceWindowSeconds, defaults.referenceWindowSeconds);
		config_.evidenceStartSeconds = Positive(config_.evidenceStartSeconds, defaults.evidenceStartSeconds);
		config_.evidenceFullSeconds = Positive(config_.evidenceFullSeconds, defaults.evidenceFullSeconds);
		config_.evidenceDecaySeconds = Positive(config_.evidenceDecaySeconds, defaults.evidenceDecaySeconds);
		config_.maximumEvidenceIntervalSeconds = Positive(config_.maximumEvidenceIntervalSeconds, defaults.maximumEvidenceIntervalSeconds);
		config_.holdSeconds = Positive(config_.holdSeconds, defaults.holdSeconds);
		config_.sweepHoldSeconds = Positive(config_.sweepHoldSeconds, defaults.sweepHoldSeconds);
		config_.decreaseConfirmationSeconds = Positive(config_.decreaseConfirmationSeconds, defaults.decreaseConfirmationSeconds);
		config_.sweepDecreaseConfirmationSeconds = Positive(config_.sweepDecreaseConfirmationSeconds, defaults.sweepDecreaseConfirmationSeconds);
		config_.growthTauSeconds = Positive(config_.growthTauSeconds, defaults.growthTauSeconds);
		config_.largeGrowthTauSeconds = Positive(config_.largeGrowthTauSeconds, defaults.largeGrowthTauSeconds);
		config_.shrinkTauSeconds = Positive(config_.shrinkTauSeconds, defaults.shrinkTauSeconds);
		config_.sweepShrinkTauSeconds = Positive(config_.sweepShrinkTauSeconds, defaults.sweepShrinkTauSeconds);
		config_.maximumLogGrowthPerSecond = Positive(config_.maximumLogGrowthPerSecond, defaults.maximumLogGrowthPerSecond);
		config_.largeLogGrowthPerSecond = Positive(config_.largeLogGrowthPerSecond, defaults.largeLogGrowthPerSecond);
		config_.maximumLogShrinkPerSecond = Positive(config_.maximumLogShrinkPerSecond, defaults.maximumLogShrinkPerSecond);
		config_.sweepLogShrinkPerSecond = Positive(config_.sweepLogShrinkPerSecond, defaults.sweepLogShrinkPerSecond);
		config_.mouseReleaseSeconds = Positive(config_.mouseReleaseSeconds, defaults.mouseReleaseSeconds);
		config_.settleLogTolerance = Positive(config_.settleLogTolerance, defaults.settleLogTolerance);
		config_.evidenceFullSeconds = std::max(config_.evidenceStartSeconds + 0.001, config_.evidenceFullSeconds);
		config_.idleStartSeconds=Positive(config.idleStartSeconds,defaults.idleStartSeconds);
		config_.idleTauSeconds=Positive(config.idleTauSeconds,defaults.idleTauSeconds);
		config_.idleLogShrinkPerSecond=Positive(config.idleLogShrinkPerSecond,defaults.idleLogShrinkPerSecond);
		config_.sweepEntryFraction = std::clamp(Positive(config_.sweepEntryFraction, defaults.sweepEntryFraction), 0.01, 1.0);
		config_.sweepExitFraction = std::clamp(Positive(config_.sweepExitFraction, defaults.sweepExitFraction), 0.0, config_.sweepEntryFraction - 0.001);
		config_.decreaseRatio = std::clamp(Positive(config_.decreaseRatio, 0.08), 0.001, 0.5);
		segments_.fill({});
		segmentCount_ = 0;
		acceptedX_ = downX_ = movementX_ = std::isfinite(x) ? x : 0.0;
		acceptedY_ = downY_ = movementY_ = std::isfinite(y) ? y : 0.0;
		acceptedTime_ = std::isfinite(seconds) ? seconds : 0.0;
		sampleState_ = {};
		sampleState_.time = acceptedTime_;
		sampleState_.lastMovementTime=acceptedTime_;
		sampleState_.areaFloorDip=config_.sizes.minimumDiameterDip;
		const float safeStart=initialDiameterDip>0 && std::isfinite(initialDiameterDip)
			? std::clamp(initialDiameterDip,config_.sizes.minimumDiameterDip,config_.sizes.standardDiameterDip)
			: config_.sizes.standardDiameterDip;
		sampleState_.logDiameter = sampleState_.logTarget = std::log(kind==StartKind::Touch ? config_.sizes.touchStartDiameterDip : safeStart);
		frameState_ = sampleState_;
		pauseTime_ = 0.0;
		touchStartup_ = kind == StartKind::Touch;
		previewOnly_ = false;
		initialized_ = true;
		paused_ = false;
		area_={};
		ObserveContactArea(contactArea?*contactArea:ContactAreaSample{},acceptedTime_,false);
	}

	void Controller::ResetPreview(float x,float y,double seconds,const Config& config,float diameterDip) noexcept
	{
		Reset(x,y,seconds,StartKind::Hover,config,diameterDip);
		previewOnly_=true;
	}
	void Controller::BeginContact(const Controller* preview,float x,float y,double seconds,const Config& config) noexcept
	{
		float safe=0;
		if (preview && preview->initialized_ && preview->previewOnly_ && preview->config_==config &&
			seconds>=preview->acceptedTime_)
		{
			const double distance=std::hypot((x-preview->acceptedX_)*config.display.dipPerPixelX,
				(y-preview->acceptedY_)*config.display.dipPerPixelY);
			// 同位 Down 本身重新确认了定位；跨位置的旧 Hover 则必须足够新鲜。
			if(seconds-preview->acceptedTime_<=0.250 || distance<=0.5)
				safe=std::min(preview->DiameterDip(),config.sizes.standardDiameterDip);
		}
		Reset(x,y,seconds,StartKind::Hover,config,safe);
	}

	void Controller::AddSegment(const MotionSegment& segment) noexcept
	{
		const double retention = std::max(config_.historyWindowSeconds, config_.referenceWindowSeconds);
		// 保留本次积分起点需要的历史，而非提前按新样本终点删掉旧区间。
		const double cutoff = sampleState_.time - retention;
		size_t expired = 0;
		while (expired < segmentCount_ && segments_[expired].endTime <= cutoff) ++expired;
		if (expired)
		{
			for (size_t i = expired; i < segmentCount_; ++i) segments_[i - expired] = segments_[i];
			segmentCount_ -= expired;
		}
		if (segmentCount_ && segments_[0].startTime < cutoff)
		{
			auto& first = segments_[0];
			const double trimmed=(cutoff-first.startTime)/(first.endTime-first.startTime);
			first.x0+=(first.x1-first.x0)*trimmed;
			first.y0+=(first.y1-first.y0)*trimmed;
			first.distance *= (first.endTime - cutoff) / (first.endTime - first.startTime);
			first.startTime = cutoff;
		}
		if (segmentCount_ == kSegmentCapacity)
		{
			// 合并最短的相邻区间；不能反复延长同一旧段，让过期高速路程拖尾。
			size_t merge = 0;
			for (size_t i = 1; i + 1 < segmentCount_; ++i)
				if (segments_[i + 1].endTime - segments_[i].startTime <
					segments_[merge + 1].endTime - segments_[merge].startTime)
					merge = i;
			segments_[merge].endTime = segments_[merge + 1].endTime;
			segments_[merge].x1=segments_[merge+1].x1;
			segments_[merge].y1=segments_[merge+1].y1;
			segments_[merge].distance += segments_[merge + 1].distance;
			for (size_t i = merge + 2; i < segmentCount_; ++i) segments_[i - 1] = segments_[i];
			--segmentCount_;
		}
		segments_[segmentCount_++] = segment;
	}

	double Controller::MotionSpeed(double seconds, double windowSeconds) const noexcept
	{
		double distance = 0.0;
		for (size_t i = 0; i < segmentCount_; ++i)
		{
			const auto& segment = segments_[i];
			const double start = std::max(segment.startTime, seconds - windowSeconds);
			const double end = std::min(segment.endTime, seconds);
			if (end > start && segment.endTime > segment.startTime)
				distance += segment.distance * (end - start) / (segment.endTime - segment.startTime);
		}
		return distance / windowSeconds;
	}


	bool Controller::HasMotionSupport(double seconds,double windowSeconds,double noiseRatio) const noexcept
	{
		double minX=std::numeric_limits<double>::infinity(),minY=minX;
		double maxX=-minX,maxY=-minX;
		for(size_t i=0;i<segmentCount_;++i)
		{
			const auto& s=segments_[i];
			const double begin=std::max(s.startTime,seconds-(windowSeconds>0?windowSeconds:config_.historyWindowSeconds));
			const double end=std::min(s.endTime,seconds);
			if(end<=begin || s.endTime<=s.startTime)continue;
			for(const double t:{begin,end})
			{
				const double f=(t-s.startTime)/(s.endTime-s.startTime);
				const double x=(s.x0+(s.x1-s.x0)*f)*config_.motionPerPixelX;
				const double y=(s.y0+(s.y1-s.y0)*f)*config_.motionPerPixelY;
				minX=std::min(minX,x);maxX=std::max(maxX,x);
				minY=std::min(minY,y);maxY=std::max(maxY,y);
			}
		}
		return std::isfinite(minX) && std::hypot(maxX-minX,maxY-minY)>=config_.movementNoiseDistance*noiseRatio;
	}

	double Controller::IdleDiameterDip(const DynamicsState& state) const noexcept
	{
		if(!AreaEligible())return config_.sizes.minimumDiameterDip;
		const double floor=std::max(config_.sizes.minimumDiameterDip,
			std::min(state.areaFloorDip,AreaReferenceFloor(state.time)));
		// 晚到的面积只能预备下一次移动，不能让静止的有效橡皮反向变大。
		return std::min(std::exp(state.logDiameter),floor);
	}

	double Controller::TargetLogDiameter(double speed,double maximumDisplacement) const noexcept
	{
		float target=ReferenceTargetDiameterDip(config_,speed);
		if(previewOnly_)target=std::min(target,config_.sizes.standardDiameterDip);
		else target=CompensateTargetDiameterDip(config_,target);
		if(touchStartup_ && maximumDisplacement<config_.touchUnlockEnd)
		{
			const double amount=SmoothStep((maximumDisplacement-config_.touchUnlockStart)/
				(config_.touchUnlockEnd-config_.touchUnlockStart));
			target=static_cast<float>(config_.sizes.touchStartDiameterDip+
				amount*(std::min(target,config_.sizes.standardDiameterDip)-config_.sizes.touchStartDiameterDip));
		}
		return std::log(target);
	}


	void Controller::FollowTarget(DynamicsState& state, double endTime,
		double target, double realMotionSpeed, bool areaMotionEvidence) const noexcept
	{
		const double previousTarget=state.logTarget;
		const auto canSettle=[&](double goal)
		{
			// Touch 的移动目标不能按每包微小差值连续吸附，否则高回报率会绕过阻尼。
			return config_.response!=ResponseModel::DirectTouch || std::abs(goal-previousTarget)<=1e-9;
		};
		const double startTime=state.time, dt=endTime-startTime;
		state.time=endTime;
		const float areaGoal=AreaReferenceFloor(endTime);
		const bool areaOpen=AreaEligible() && state.maximumDisplacement>=config_.touchUnlockEnd &&
			areaGoal>config_.sizes.minimumDiameterDip;
		const bool areaMotion=areaOpen && areaMotionEvidence;
		const auto acceptAreaFloor=[&]()
		{
			if(areaMotion)state.areaFloorDip=std::min(areaGoal,static_cast<float>(std::exp(state.logDiameter)));
		};
		acceptAreaFloor();
		if(areaOpen)target=std::max(target,std::log(static_cast<double>(
			areaMotion?areaGoal:std::min(areaGoal,std::max(config_.sizes.minimumDiameterDip,
				std::min(state.areaFloorDip,static_cast<float>(std::exp(state.logDiameter))))))));
		state.logTarget=target;
		const double standard=std::log(config_.sizes.standardDiameterDip);
		const double logRange=std::log(config_.sizes.maximumDiameterDip/config_.sizes.standardDiameterDip);
		if (!previewOnly_ && realMotionSpeed >= config_.sweepEnterSpeed) state.sweepQualified=true;
		else if (realMotionSpeed > 0 && realMotionSpeed < config_.sweepExitSpeed) state.sweepQualified=false;
		const bool qualifies=!previewOnly_ && (!touchStartup_ || state.maximumDisplacement>=config_.touchUnlockStart) && state.sweepQualified && realMotionSpeed >= config_.sweepEnterSpeed;
		const double strength=qualifies ? 0.4 + 0.6*SmoothStep((realMotionSpeed-config_.sweepEnterSpeed)/
			(config_.largeTargetSpeed-config_.sweepEnterSpeed)) : 0.0;
		const double leak=std::exp(-dt/config_.evidenceDecaySeconds);
		// 始终泄漏；低于进入速度的普通动作，无论持续多久都不能充满证据。
		state.sweepEvidence=std::min(config_.evidenceFullSeconds,
			state.sweepEvidence*leak + strength*config_.evidenceDecaySeconds*(1-leak));
		const double idleStart=state.lastMovementTime+config_.idleStartSeconds;
		if (endTime >= idleStart)
		{
			const double elapsed=endTime-std::max(startTime,idleStart);
			target=std::log(IdleDiameterDip(state));
			state.logTarget=target;
			state.sweepQualified=false;
			state.decreasePending=state.shrinking=false;
			state.logDiameter=Follow(state.logDiameter,target,elapsed,config_.idleTauSeconds,config_.idleLogShrinkPerSecond);
			if (std::abs(state.logDiameter-target)<=config_.settleLogTolerance && canSettle(target)) state.logDiameter=target;
			if (state.logDiameter<=standard+config_.settleLogTolerance) state.sweeping=false;
			return;
		}
		// 精细区不背负大尺寸清扫的保持阻力，Hover 永远不能存储清扫证据。
		if (state.logDiameter<=standard+config_.settleLogTolerance && target<=standard)
		{
			state.sweeping=state.decreasePending=state.shrinking=false;
			state.holdUntil=endTime;
			state.logDiameter=Follow(state.logDiameter,target,dt,0.120,4.0);
			if(std::abs(state.logDiameter-target)<=config_.settleLogTolerance && canSettle(target))state.logDiameter=target;
			acceptAreaFloor();
			return;
		}
		const double range=config_.sizes.maximumDiameterDip-config_.sizes.standardDiameterDip;
		const double size=range>0 ? std::clamp((std::exp(state.logDiameter)-config_.sizes.standardDiameterDip)/range,0.0,1.0) : 0;
		if (!state.sweeping && size>=config_.sweepEntryFraction && state.sweepEvidence>=config_.evidenceFullSeconds*0.8)
			state.sweeping=true;
		if (state.sweeping && size<=config_.sweepExitFraction) state.sweeping=false;
		const double resistance=state.sweeping ? SmoothStep((size-0.2)/0.6) : 0.0;
		const auto blend=[](double a,double b,double t){return a+(b-a)*t;};
		const double difference=target-state.logDiameter;
		const bool smaller=difference<=std::log(1-config_.decreaseRatio) || target<=standard+1e-9;
		if (!smaller && qualifies)
			state.holdUntil=endTime+blend(config_.holdSeconds,config_.sweepHoldSeconds,resistance);
		if (difference>=0)
		{
			state.decreasePending=state.shrinking=false;
			double permitted=target;
			if (target>standard)
			{
				const double permission=SmoothStep((state.sweepEvidence-config_.evidenceStartSeconds)/
					(config_.evidenceFullSeconds-config_.evidenceStartSeconds));
				permitted=std::min(target,standard+permission*logRange);
				// 有限的拖擦下限不等于高速清扫，不借面积给速度资格充能。
				if(areaMotion)permitted=std::max(permitted,std::min(target,std::log(static_cast<double>(areaGoal))));
				if (!qualifies && !areaMotion) return;
			}
			else if (realMotionSpeed<=0 && state.maximumDisplacement<config_.touchUnlockEnd) return;
			if (permitted<=state.logDiameter) return;
			const double growth=SmoothStep(size);
			state.logDiameter=Follow(state.logDiameter,permitted,dt,
				blend(config_.growthTauSeconds,config_.largeGrowthTauSeconds,growth),
				blend(config_.maximumLogGrowthPerSecond,config_.largeLogGrowthPerSecond,growth));
			target=permitted;
		}
		else
		{
			if (!smaller && !state.shrinking){state.decreasePending=false;return;}
			if (!state.decreasePending){state.decreasePending=true;state.decreaseSince=startTime;}
			const double confirmation=blend(config_.decreaseConfirmationSeconds,config_.sweepDecreaseConfirmationSeconds,resistance);
			const double releaseStart=std::max(state.holdUntil,state.decreaseSince+confirmation);
			const double elapsed=endTime-std::max(startTime,releaseStart);
			if (elapsed<=0)return;
			state.shrinking=true;
			state.logDiameter=Follow(state.logDiameter,target,elapsed,
				blend(config_.shrinkTauSeconds,config_.sweepShrinkTauSeconds,resistance),
				blend(config_.maximumLogShrinkPerSecond,config_.sweepLogShrinkPerSecond,resistance));
		}
		acceptAreaFloor();
		if(std::abs(state.logDiameter-target)<=config_.settleLogTolerance && canSettle(target))
		{state.logDiameter=target;state.decreasePending=state.shrinking=false;}
	}

	void Controller::AdvanceState(DynamicsState& state, double seconds,
		const MotionSegment* incoming, double incomingX, double incomingY, bool effectiveMovement) const noexcept
	{
		const double retention = std::max(config_.historyWindowSeconds, config_.referenceWindowSeconds);
		const double historyEnd = segmentCount_ ? segments_[segmentCount_ - 1].endTime + retention : state.time;
		while (state.time < seconds)
		{
			const double minimum = std::log(IdleDiameterDip(state));
			const bool canSkipArea=!AreaEligible() || !area_.diagnostic.referenceReady ||
				state.areaFloorDip<=config_.sizes.minimumDiameterDip || AreaReferenceFloor(state.time)<=config_.sizes.minimumDiameterDip ||
				seconds<=AreaExpirySeconds();
			if (!incoming && canSkipArea && state.time >= historyEnd &&
				std::abs(state.logDiameter - minimum) < 1e-9 && state.sweepEvidence < 1e-6)
			{
				state.time = seconds;
				state.logTarget = minimum;
				state.sweepEvidence = 0.0;
				state.sweeping = state.shrinking = state.decreasePending = false;
				break;
			}
			const double end = std::min(seconds, state.time + 0.004);
			if (end <= state.time) { state.time = seconds; break; }
			const double midpoint = (state.time + end) * 0.5;
			if(effectiveMovement && incoming && incoming->endTime-incoming->startTime<=config_.maximumEvidenceIntervalSeconds)
				state.lastMovementTime=end;
			if (incoming)
			{
				const double fraction = std::clamp((end - incoming->startTime) /
					(incoming->endTime - incoming->startTime), 0.0, 1.0);
				const double x = acceptedX_ + fraction * (incomingX - acceptedX_);
				const double y = acceptedY_ + fraction * (incomingY - acceptedY_);
				state.maximumDisplacement = std::max(state.maximumDisplacement,
					std::hypot((x - downX_) * config_.motionPerPixelX,
						(y - downY_) * config_.motionPerPixelY));
			}
			const double speed = midpoint < historyEnd ? MotionSpeed(midpoint, config_.historyWindowSeconds) : 0.0;
			// 稀疏的一个跳点不能证明整个空档都在快擦；正常输入仍按真实 dt 累积。
			state.speed=speed;
			// 空间门只确认时间窗内存在真实移动；每个已观测区间完整积分，不能仅给跨门槛的包记 dt。
			const double observedSpeed = HasMotionSupport(midpoint) && incoming && incoming->distance > 0.0 &&
				incoming->endTime - incoming->startTime <= config_.maximumEvidenceIntervalSeconds
				? std::min(speed, incoming->distance / (incoming->endTime - incoming->startTime)) : 0.0;
			// 面积只需要稳定拖动，不要求清扫速度；复用长路程窗，且仍受实际位移起步与 idle 门控制。
			const bool areaMotion=AreaEligible() && incoming && incoming->distance>0 &&
				incoming->endTime-incoming->startTime<=config_.maximumEvidenceIntervalSeconds &&
				HasMotionSupport(midpoint,config_.referenceWindowSeconds,config_.contactArea.movementNoiseRatio);
			FollowTarget(state, end, TargetLogDiameter(speed, state.maximumDisplacement), observedSpeed,areaMotion);
		}
	}

	float Controller::UpdatePosition(float x, float y, double seconds,
		const ContactAreaSample* contactArea, bool terminal) noexcept
	{
		if (!initialized_) { Reset(x, y, seconds); return Diameter(); }
		if (paused_ || !std::isfinite(x) || !std::isfinite(y) ||
			!std::isfinite(seconds) || seconds <= sampleState_.time) return Diameter();
		const double duration = seconds - acceptedTime_;
		const double distance = std::hypot((static_cast<double>(x) - acceptedX_) * config_.motionPerPixelX,
			(static_cast<double>(y) - acceptedY_) * config_.motionPerPixelY);
		const bool effectiveMovement=std::hypot((x-movementX_)*config_.motionPerPixelX,
			(y-movementY_)*config_.motionPerPixelY)>=config_.movementNoiseDistance;
		if (duration > 1.0)
		{
			// 缺失很久的输入不能证明连续运动；释放时间照常推进，但不猜测缺失轨迹。
			AdvanceState(sampleState_, seconds);
		}
		else
		{
			const MotionSegment segment{ acceptedTime_, seconds, distance, acceptedX_, acceptedY_, x, y };
			AddSegment(segment);
			AdvanceState(sampleState_, seconds, &segment, x, y, effectiveMovement);
		}
		if(effectiveMovement){movementX_=x;movementY_=y;sampleState_.lastMovementTime=seconds;}
		acceptedX_ = x;
		acceptedY_ = y;
		acceptedTime_ = seconds;
		// 新面积只在原始区间结束后采纳，不能反写已经演进的帧或之前的几何。
		if(!terminal && touchStartup_)
			ObserveContactArea(contactArea?*contactArea:ContactAreaSample{},seconds,
				distance>0 && duration<=config_.maximumEvidenceIntervalSeconds &&
				sampleState_.maximumDisplacement>=config_.touchUnlockStart &&
				HasMotionSupport(seconds,config_.referenceWindowSeconds,config_.contactArea.movementNoiseRatio));
		// 帧预览不能反写真实输入锚点；迟到的新 raw 输入从真实状态重新积分。
		frameState_ = sampleState_;
		return Diameter();
	}

	float Controller::Advance(double seconds) noexcept
	{
		if (initialized_ && !paused_ && std::isfinite(seconds) && seconds > frameState_.time)
			AdvanceState(frameState_, seconds);
		return Diameter();
	}

	void Controller::PauseForReconnect(double seconds) noexcept
	{
		if (!initialized_ || paused_ || !std::isfinite(seconds)) return;
		AdvanceState(sampleState_, std::max(seconds, sampleState_.time));
		frameState_ = sampleState_;
		pauseTime_ = sampleState_.time;
		paused_ = true;
	}

	float Controller::ResumeFromReconnect(float x, float y, double seconds) noexcept
	{
		if (!initialized_ || !std::isfinite(x) || !std::isfinite(y) || !std::isfinite(seconds) ||
			seconds < sampleState_.time) return Diameter();
		const double gap = seconds - (paused_ ? pauseTime_ : sampleState_.time);
		for (size_t i = 0; i < segmentCount_; ++i)
		{
			segments_[i].startTime += gap;
			segments_[i].endTime += gap;
		}
		sampleState_.time += gap;
		sampleState_.lastMovementTime += gap;
		ShiftAreaTime(gap);
		sampleState_.holdUntil += gap;
		if (sampleState_.decreasePending) sampleState_.decreaseSince += gap;
		// 平移落点基准以排除断点距离，避免缺失段解锁 Touch 起步限制。
		movementX_ += static_cast<double>(x)-acceptedX_;
		movementY_ += static_cast<double>(y)-acceptedY_;
		downX_ += static_cast<double>(x) - acceptedX_;
		downY_ += static_cast<double>(y) - acceptedY_;
		acceptedX_ = x;
		acceptedY_ = y;
		acceptedTime_ = seconds;
		frameState_ = sampleState_;
		paused_ = false;
		return Diameter();
	}

	float Controller::Diameter() const noexcept
	{
		return DiameterToCanvasPx(DiameterDip(),config_.display);
	}


	float Controller::DiameterDip() const noexcept
	{
		return initialized_ ? static_cast<float>(std::exp(frameState_.logDiameter)) : config_.sizes.standardDiameterDip;
	}
	double Controller::SecondsSinceMovement(double seconds) const noexcept
	{
		return initialized_ ? std::max(0.0,(paused_?pauseTime_:seconds)-frameState_.lastMovementTime) : 0.0;
	}

	float Controller::TargetDiameterDip() const noexcept
	{
		return initialized_?static_cast<float>(std::exp(frameState_.logTarget)):config_.sizes.standardDiameterDip;
	}
	bool Controller::TouchUnlocked() const noexcept
	{
		return touchStartup_ && frameState_.maximumDisplacement>=config_.touchUnlockEnd;
	}

	float Controller::TargetDiameter() const noexcept
	{
		return DiameterToCanvasPx(initialized_ ? static_cast<float>(std::exp(frameState_.logTarget)) : config_.sizes.standardDiameterDip,config_.display);
	}

	bool Controller::TargetLimited() const noexcept
	{
		bool limited=false;
		if(!previewOnly_)CompensateTargetDiameterDip(config_,ReferenceTargetDiameterDip(config_,frameState_.speed),&limited);
		return limited;
	}

	bool Controller::NeedsAnimation(double seconds) const noexcept
	{
		if (!initialized_ || paused_ || !std::isfinite(seconds)) return false;
		const bool areaFading=AreaEligible() && area_.diagnostic.referenceReady &&
			frameState_.time>=AreaExpirySeconds() && AreaReferenceFloor(frameState_.time)>config_.sizes.minimumDiameterDip &&
			frameState_.areaFloorDip>config_.sizes.minimumDiameterDip;
		return areaFading || std::abs(frameState_.logDiameter - std::log(IdleDiameterDip(frameState_))) > 1e-9 ||
			(segmentCount_ && seconds < segments_[segmentCount_ - 1].endTime + config_.referenceWindowSeconds);
	}


	void MouseLifecycle::Configure(const Config& config) noexcept
	{
		if (configured_ && config_ == config) return;
		config_ = config;
		configured_ = true;
		CancelVisual();
	}

	void MouseLifecycle::CancelVisual() noexcept
	{
		logicalDiameter_ = static_cast<float>(Positive(config_.StandardDiameterPx(), 16.0));
		visualDiameter_ = logicalDiameter_;
		releaseCandidate_ = releasing_ = hasPosition_ = hoverInitialized_ = false;
	}

	void MouseLifecycle::ObserveHover(float x, float y, double seconds) noexcept
	{
		if (contactOwned_ || !std::isfinite(x) || !std::isfinite(y) || !std::isfinite(seconds) ||
			seconds < lastEventSeconds_ || seconds < lastHoverSeconds_) return;
		x_ = x;
		y_ = y;
		hasPosition_ = true;
		lastHoverSeconds_ = seconds;
		if (releasing_ && seconds<releaseSeconds_+config_.mouseReleaseSeconds) return;
		if (!hoverInitialized_)
		{
			hover_.ResetPreview(x,y,seconds,config_,logicalDiameter_*
				std::sqrt(config_.display.dipPerPixelX*config_.display.dipPerPixelY));
			hoverInitialized_=true;
		}
		else hover_.UpdatePosition(x,y,seconds);
	}

	void MouseLifecycle::BeginContact(Controller& controller, float x, float y,
		double seconds, const Config& config) noexcept
	{
		Configure(config);
		controller.BeginContact(hoverInitialized_ && hasPosition_ && !releasing_ && !contactOwned_ ? &hover_ : nullptr,
			x,y,seconds,config);
		CancelVisual();
		logicalDiameter_=visualDiameter_=controller.Diameter();
		contactOwned_ = true;
		x_ = x;
		y_ = y;
		hasPosition_ = true;
		lastDownSeconds_ = seconds;
		lastEventSeconds_ = std::max(lastEventSeconds_, seconds);
		// Down 只继承安全尺寸；Controller::BeginContact 已清空速度、证据并重锚。
	}

	void MouseLifecycle::EndContact(Controller& controller, float acceptedDiameter, float x, float y,
		double seconds, bool anotherOwner, bool cancelled) noexcept
	{
		if (!std::isfinite(seconds)) return;
		if (!contactOwned_ && !anotherOwner) return;
		// Up 的逻辑重置立即发生，真实点的半径已由调用方接受，不随此重置变化。
		const float safeDip=std::clamp(acceptedDiameter*static_cast<float>(
			std::sqrt(config_.display.dipPerPixelX*config_.display.dipPerPixelY)),
			config_.sizes.minimumDiameterDip,config_.sizes.standardDiameterDip);
		controller.Reset(x, y, seconds, StartKind::Hover, controller.Configuration(),safeDip);
		contactOwned_ = anotherOwner;
		lastEventSeconds_ = std::max(lastEventSeconds_, seconds);
		// 旧 Up 不恢复旧画面，但仍须按当前所有者集合释放占用；配置切换可能已丢弃候选。
		if (cancelled || (seconds < lastDownSeconds_ && !releaseCandidate_))
		{
			if (!anotherOwner) CancelVisual();
			return;
		}
		if (seconds >= lastDownSeconds_ && (!releaseCandidate_ || seconds >= releaseSeconds_))
		{
			// 复制真正接受的端点直径，不读取 Controller 的待用帧预览。
			releaseFrom_ = static_cast<float>(Positive(acceptedDiameter, config_.StandardDiameterPx()));
			releaseSeconds_ = seconds;
			x_ = x;
			y_ = y;
			hasPosition_ = true;
			releaseCandidate_ = true;
		}
		if (anotherOwner || !releaseCandidate_) return;
		logicalDiameter_ = std::min(releaseFrom_, config_.StandardDiameterPx());
		visualDiameter_ = releaseFrom_;
		releasing_ = releaseFrom_ > logicalDiameter_;
		hover_.ResetPreview(x_,y_,releaseSeconds_+(releasing_?config_.mouseReleaseSeconds:0.0),
			config_,safeDip);
		hoverInitialized_=true;
	}

	float MouseLifecycle::Advance(double seconds) noexcept
	{
		if (!std::isfinite(seconds)) return visualDiameter_;
		visualTime_=std::max(visualTime_,seconds);
		if (contactOwned_) return visualDiameter_;
		if (releasing_)
		{
			const double amount=std::clamp((visualTime_-releaseSeconds_)/
				Positive(config_.mouseReleaseSeconds,0.140),0.0,1.0);
			visualDiameter_=static_cast<float>(releaseFrom_+(logicalDiameter_-releaseFrom_)*SmoothStep(amount));
			if(amount<1.0)return visualDiameter_;
			releasing_=releaseCandidate_=false;
		}
		if(hoverInitialized_)
			logicalDiameter_=visualDiameter_=hover_.Advance(visualTime_);
		return visualDiameter_;
	}

	bool MouseLifecycle::NeedsAnimation(double seconds) const noexcept
	{
		return !contactOwned_ && std::isfinite(seconds) &&
			((releasing_ && seconds<releaseSeconds_+Positive(config_.mouseReleaseSeconds,0.140)) ||
			 (hoverInitialized_ && hover_.NeedsAnimation(seconds)));
	}


	void ContactSizeState::Reset(float diameterPx,double seconds) noexcept
	{
		effectiveDiameterPx=resumeDiameterPx=diameterPx;
		effectiveTimeSeconds=seconds;
		breakTimeSeconds=seconds;
		breakPending=false;
	}
	void ContactSizeState::Update(float diameterPx,double seconds,bool stationary) noexcept
	{
		if(!std::isfinite(diameterPx)||diameterPx<=0||!std::isfinite(seconds)||seconds<effectiveTimeSeconds)return;
		if(stationary && std::abs(diameterPx-effectiveDiameterPx)>0.001f)
		{
			if(!breakPending)breakTimeSeconds=seconds;
			breakPending=true;
			resumeDiameterPx=diameterPx;
		}
		effectiveDiameterPx=diameterPx;
		effectiveTimeSeconds=seconds;
	}
	WidthInterval ContactSizeState::MakeInterval(double fromModelTime,double toModelTime,
		float oldDiameter,float newDiameter,double rawSeconds,const Config& config) const noexcept
	{
		const bool reanchor=breakPending && rawSeconds>=breakTimeSeconds;
		return {fromModelTime,toModelTime,reanchor?resumeDiameterPx:oldDiameter,newDiameter,
			config.minimumDiameterPx,config.maximumDiameterPx,reanchor};
	}
	void ContactSizeState::Accepted(const WidthInterval& interval) noexcept
	{
		if(interval.reanchor)breakPending=false;
	}

	float InterpolateDiameter(const WidthInterval& interval, double seconds) noexcept
	{
		const double minimum = Positive(interval.minimumDiameter, 0.001);
		const double maximum = std::max(minimum, Positive(interval.maximumDiameter, minimum));
		const double start = std::clamp(Positive(interval.startDiameter, minimum), minimum, maximum);
		const double end = std::clamp(Positive(interval.endDiameter, minimum), minimum, maximum);
		if (!std::isfinite(seconds) || !std::isfinite(interval.startTimeSeconds) ||
			!std::isfinite(interval.endTimeSeconds) || interval.endTimeSeconds <= interval.startTimeSeconds)
			return static_cast<float>(end);
		const double amount = std::clamp((seconds - interval.startTimeSeconds) /
			(interval.endTimeSeconds - interval.startTimeSeconds), 0.0, 1.0);
		return static_cast<float>(start + amount * (end - start));
	}

	float ContactDiameter(float acceptedRadius, float fallbackDiameter) noexcept
	{
		const double diameter = static_cast<double>(acceptedRadius) * 2.0;
		return std::isfinite(diameter) && diameter > 0.0 && diameter <= std::numeric_limits<float>::max()
			? static_cast<float>(diameter) : static_cast<float>(Positive(fallbackDiameter, 0.001));
	}
}
