#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace Inkeys::Drawing::Draw3::SpeedEraser
{
	enum class DeviceMode { LargeScreen, Laptop, Automatic };
	enum class TouchProfileSource { NotTouch, SelectedLaptopPhysical, SelectedLargeScreenPhysical,
		AutomaticPhysical, SelectedLaptopFallback, SelectedLargeScreenFallback, AutomaticFallback };
	enum class ScaleSource { DipOnly, TrustedPhysical, ManualCalibration, ResolutionDpiHeuristic,
		Dip = DipOnly, Physical = TrustedPhysical };
	enum class MotionUnit { DipPerSecond, MillimetersPerSecond, HeuristicPerSecond };
	enum class SourceKind : uint32_t { Unknown, Mouse, ExternalPen, IntegratedPen, Touch, TouchPad };
	enum class SourceRecognition : uint32_t { Unknown, RtsCapabilities, PointerCursor, Conflict };
	enum class ResponseModel { IndirectDip, ScreenPenHybrid, DirectTouch };
	enum class ResponseOverride { Automatic, IndirectDip, ScreenPenHybrid, DirectTouch };
	enum class ScaleOverride { Automatic, ManualSurface, ForceUnavailable };

	// 真实设备关系与响应策略独立；身份和显示映射由 producer 缓存，绝不伪造 Touch。
	struct InputSource
	{
		SourceKind kind = SourceKind::Unknown;
		SourceRecognition recognition = SourceRecognition::Unknown;
		uint32_t contextId = 0, cursorId = 0;
		uint64_t generation = 0;
		uintptr_t mappedMonitor = 0;
		int32_t mappedLeft = 0, mappedTop = 0, mappedWidth = 0, mappedHeight = 0;
		friend bool operator==(const InputSource&, const InputSource&) = default;
	};
	SourceKind ClassifySource(uint32_t actualInputType, bool modernApiAvailable,
		bool capabilitiesKnown, bool integrated, SourceKind pointerKind = SourceKind::Unknown,
		bool pointerMatched = false, bool pointerAmbiguous = false) noexcept;

	struct ManualSurfaceCalibration
	{
		uintptr_t monitor = 0;
		float widthCm = 0, heightCm = 0;
		uint32_t orientation = 0;
		friend bool operator==(const ManualSurfaceCalibration&, const ManualSurfaceCalibration&) = default;
	};
	struct DevelopmentOptions
	{
		ResponseOverride response = ResponseOverride::Automatic;
		ScaleOverride scale = ScaleOverride::Automatic;
		ManualSurfaceCalibration calibration;
		float penBeta = 0.5f;
		bool diagnostics = false;
		bool touchContactAreaAssistance = false;
		bool touchAreaTrace = false;
		friend bool operator==(const DevelopmentOptions&, const DevelopmentOptions&) = default;
	};
	enum class StartKind { Hover, Touch };
	enum class InputEntry : uint32_t { MouseLeft, MouseRight, Touch, PenTip, PenTail };
	enum class EraserKind : int { Fixed = 0, Speed = 1 };
	enum class PenResponseChoice : int { Automatic = 0, ScreenPen = 1, Tablet = 2 };
	enum class EraserToolPolicy : uint32_t { ByEntry, Fixed, Speed };
	struct EntrySettings
	{
		EraserKind kind = EraserKind::Speed;
		PenResponseChoice penResponse = PenResponseChoice::Automatic;
		friend bool operator==(const EntrySettings&,const EntrySettings&) = default;
	};
	// 持久化直接保存直径与稳定枚举，不依赖属性栏临时顺序。
	enum class BaseSize : int { Small = 24, Medium = 32, Large = 40 };
	inline constexpr std::array<BaseSize,3> BaseSizePresets{BaseSize::Small,BaseSize::Medium,BaseSize::Large};
	enum class Sensitivity : int { Low = 0, Medium = 1, High = 2 };
	enum class AutomaticState { Off, On };
	BaseSize RestoreBaseSize(int saved) noexcept;
	Sensitivity RestoreSensitivity(int saved) noexcept;
	float SweepGain(Sensitivity sensitivity) noexcept;
	struct InputSettings
	{
		std::array<EntrySettings,5> entries{};
		bool automaticEnabled = true;
		bool automaticPenSupported = true;
		BaseSize baseSize = BaseSize::Medium;
		Sensitivity sensitivity = Sensitivity::Medium;
		friend bool operator==(const InputSettings&,const InputSettings&) = default;
	};
	AutomaticState GetAutomaticState(const InputSettings& settings) noexcept;
	bool SetGlobalAutomatic(InputSettings& settings, bool enabled) noexcept;
	InputEntry EntryForInput(uint32_t inputType,bool inverted) noexcept;
	EraserKind RestoreEraserKind(int saved,int legacyMode,InputEntry entry) noexcept;
	PenResponseChoice RestorePenResponse(int saved,bool automaticSupported) noexcept;
	const char* InputEntryName(InputEntry entry) noexcept;
	const char* PenResponseName(PenResponseChoice choice) noexcept;

	// 按真实 Down/Up 时刻判定重叠，避免同帧先消费 Up 后丢失旧批次标尺。
	constexpr bool ContactBatchContains(int64_t beginQpc, int64_t terminalQpc,
		int64_t reconnectDeadlineQpc, int64_t incomingDownQpc) noexcept
	{
		return incomingDownQpc >= beginQpc &&
			(terminalQpc == 0 || incomingDownQpc <= terminalQpc ||
				(reconnectDeadlineQpc > 0 && incomingDownQpc <= reconnectDeadlineQpc));
	}

	struct DisplayScale
	{
		uint64_t generation = 0;
		uint64_t revision = 0;
		uintptr_t monitor = 0;
		float dipPerPixelX = 1.0f;
		float dipPerPixelY = 1.0f;
		float cmPerPixelX = 0.0f;
		float cmPerPixelY = 0.0f;
		bool physicalAvailable = false;
		bool directTouchMapped = false; // 保留旧测试适配入口；产品使用逐来源映射。
		int pixelWidth = 0, pixelHeight = 0, desktopLeft = 0, desktopTop = 0;
		uint32_t orientation = 0;
		bool logicalOutputKnown = false;
		DevelopmentOptions development;

		friend bool operator==(const DisplayScale&, const DisplayScale&) = default;
	};

	// 全部为直径 DIP。固定模式直接消费此属性，不实例化速度控制器。
	struct EraserSizes
	{
		float minimumDiameterDip = 16.0f;
		float standardDiameterDip = 32.0f;
		float maximumDiameterDip = 160.0f;
		float touchStartDiameterDip = 16.0f;
		float fixedDiameterDip = 32.0f;
		friend bool operator==(const EraserSizes&, const EraserSizes&) = default;
	};
	EraserSizes ResolveSizes(BaseSize baseSize) noexcept;
	float DiameterToCanvasPx(float diameterDip, const DisplayScale& display) noexcept;
	float FixedDiameterPx(float selectedDiameterDip, const DisplayScale& display) noexcept;

	enum class ContactAreaUnits : uint32_t
	{
		Missing, Unverified, OutsideMetrics, CanvasPixels,
		MissingAxis, AxisUnitsMissing, SpanUnitsMissing, UnsupportedLengthUnits,
		InvalidAxisResolution, InvalidSpanResolution, InvalidAxisRange, InvalidSpanRange,
		InvalidPositionScale, ConversionNonFinite
	};
	enum class ContactAreaReason { Disabled, NotScreenTouch, MappingUnknown, Missing, UnitsUnknown,
		OutsideMetrics, NonFinite, NonPositive, TooSmall, TooLarge, AspectRatio, Outlier, WaitingForMove, Confirming, Ready, Expired,
		MissingAxis, AxisUnitsMissing, SpanUnitsMissing, UnsupportedLengthUnits, InvalidResolution, InvalidRange, InvalidTransform };
	struct ContactAreaSample
	{
		float rawWidth = -1, rawHeight = -1;
		float widthPx = -1, heightPx = -1;
		ContactAreaUnits units = ContactAreaUnits::Missing;
	};
	struct ContactAreaParameters
	{
		float multiplier = 1.10f, paddingDip = 6, maximumFloorDip = 64;
		float minimumSpanDip = 2, maximumReportedSpanDip = 96, maximumAspectRatio = 3.5f;
		float confirmationRatio = 1.25f, outlierRatio = 1.60f, movementNoiseRatio = 0.5f;
		double confirmationSeconds = 0.050, filterSeconds = 0.050, maximumSampleGapSeconds = 0.080;
		double missingTimeoutSeconds = 2.0, invalidGraceSeconds = 0.200, releaseSeconds = 0.180;
		friend bool operator==(const ContactAreaParameters&, const ContactAreaParameters&) = default;
	};
	struct ContactAreaDiagnostics
	{
		ContactAreaSample sample;
		float widthDip = -1, heightDip = -1, referenceFloorDip = 0, activeFloorDip = 0;
		double stableMotionSeconds = 0;
		ContactAreaReason reason = ContactAreaReason::Disabled;
		bool enabled = false, sampleValid = false, referenceReady = false, referenceFresh = false, active = false;
	};

	// PROPERTY_METRICS 描述实际 packet 的逻辑值；不根据数值大小猜单位。
	struct ContactLengthMetrics
	{
		uint32_t units = 0;
		float resolution = 0;
		int32_t logicalMin = 0, logicalMax = 0;
		bool present = false;
	};
	struct ContactLengthTransform
	{
		double spanToAxis = 0, spanToCanvas = 0;
		int32_t logicalMin = 0, logicalMax = 0;
		ContactAreaUnits status = ContactAreaUnits::Unverified;
		bool unitConverted = false, resolutionAdjusted = false;
	};
	ContactLengthTransform ResolveContactLengthTransform(const ContactLengthMetrics& axis,
		const ContactLengthMetrics& span, float positionScale) noexcept;
	ContactAreaSample ConvertContactArea(float rawWidth, float rawHeight,
		const ContactLengthTransform& width, const ContactLengthTransform& height) noexcept;
	ContactAreaReason ContactAreaMetadataReason(ContactAreaUnits units) noexcept;
	const char* ContactPropertyUnitName(uint32_t units) noexcept;
	const char* ContactAreaReasonName(ContactAreaReason reason) noexcept;
	const char* ContactAreaUnitsName(ContactAreaUnits units) noexcept;

	struct Config
	{
		DisplayScale display;
		EraserSizes sizes;
		bool touchContactAreaAssistance = false;
		ContactAreaParameters contactArea;
		InputEntry inputEntry = InputEntry::MouseLeft;
		PenResponseChoice formalPenResponse = PenResponseChoice::Automatic;
		bool developmentResponseOverride = false;
		DeviceMode mode = DeviceMode::Laptop;
		TouchProfileSource touchProfileSource = TouchProfileSource::NotTouch;
		float touchProfileWeight = 0.0f, touchSurfaceLongEdgeMm = 0.0f;
		ScaleSource motionSource = ScaleSource::DipOnly;
		MotionUnit motionUnit = MotionUnit::DipPerSecond;
		InputSource inputSource;
		ResponseModel response = ResponseModel::IndirectDip;
		bool inputMapped = false;
		float rhoMmPerDip = 0.0f;
		float referenceMmPerDip = 0.25f;
		float penBeta = 0.5f;
		float heuristicGain = 1.0f;
		float motionPerPixelX = 1.0f;
		float motionPerPixelY = 1.0f;
		float minimumDiameterPx = 16.0f;
		float maximumDiameterPx = 160.0f;
		float fineToStandardSpeed = 100.0f;
		float fineHoldSpeed = 20.0f, fineReleaseSpeed = 35.0f; // 与本模型的 fineToStandardSpeed 同单位。
		double fineWindowSeconds = 0.140;
		double fineEnterSeconds = 0.100, fineReleaseSeconds = 0.160;
		double fineShrinkTauSeconds = 0.200, fineGrowthTauSeconds = 0.260;
		double fineLogShrinkPerSecond = 4.0, fineLogGrowthPerSecond = 3.0;
		double fineSettleSeconds = 0.040;
		float sweepGain = 1.0f; // 仅清扫动作增益；精细速度与时间门保持原值。
		float sweepEnterSpeed = 800.0f;
		float sweepExitSpeed = 600.0f;
		float largeTargetSpeed = 1900.0f;
		float movementNoiseDistance = 0.75f; // 动作单位，不是尺寸单位。
		float touchUnlockStart = 2.0f;
		float touchUnlockEnd = 6.0f;
		float StandardDiameterPx() const noexcept { return DiameterToCanvasPx(sizes.standardDiameterDip, display); }

		// 固定中档参数集中在此；速度和触摸范围使用动作标尺单位。
		double historyWindowSeconds = 0.080;
		double referenceWindowSeconds = 0.160;
		double evidenceStartSeconds = 0.100;
		double evidenceFullSeconds = 0.240;
		double evidenceDecaySeconds = 0.350;
		double maximumEvidenceIntervalSeconds = 0.080;
		double holdSeconds = 0.100;
		double sweepHoldSeconds = 0.650;
		double decreaseConfirmationSeconds = 0.100;
		double sweepDecreaseConfirmationSeconds = 0.680;
		double decreaseRatio = 0.08;
		double growthTauSeconds = 0.280;
		double largeGrowthTauSeconds = 0.160;
		double shrinkTauSeconds = 0.160;
		double sweepShrinkTauSeconds = 0.300;
		double maximumLogGrowthPerSecond = 4.0;
		double largeLogGrowthPerSecond = 6.0;
		double maximumLogShrinkPerSecond = 4.0;
		double sweepLogShrinkPerSecond = 2.2;
		double sweepEntryFraction = 0.50;
		double sweepExitFraction = 0.25;
		double mouseReleaseSeconds = 0.140;
		double idleStartSeconds = 0.280;
		double idleTauSeconds = 0.200;
		double idleLogShrinkPerSecond = 4.0;
		double settleLogTolerance = 0.003;

		friend bool operator==(const Config&, const Config&) = default;
	};

	Config ResolveConfig(const DisplayScale& display, DeviceMode mode, const InputSource& source,
		const EraserSizes& sizes = {}, PenResponseChoice penResponse = PenResponseChoice::Automatic,
		bool automaticPenSupported = true) noexcept;
	struct ResolvedInput
	{
		InputEntry entry = InputEntry::MouseLeft;
		EraserKind kind = EraserKind::Speed;
		Config config;
	};
	ResolvedInput ResolveInput(const DisplayScale& display,DeviceMode mode,const InputSource& source,
		InputEntry entry,const InputSettings& settings,EraserToolPolicy policy = EraserToolPolicy::ByEntry) noexcept;
	bool SessionConfigCompatible(const Config& left,const Config& right) noexcept;
	double SweepActionSpeed(const Config& config, double speed) noexcept;
	float ReferenceTargetDiameterDip(const Config& config, double speed) noexcept;
	float CompensateTargetDiameterDip(const Config& config, float referenceDip, bool* limited = nullptr) noexcept;
	float ResolutionDpiActionGain(const DisplayScale& display) noexcept;
	const char* SourceKindName(SourceKind kind) noexcept;
	const char* ResponseModelName(ResponseModel model) noexcept;
	const char* ScaleSourceName(ScaleSource source) noexcept;
	const char* MotionUnitName(MotionUnit unit) noexcept;
	const char* DeviceModeName(DeviceMode mode) noexcept;
	const char* TouchProfileSourceName(TouchProfileSource source) noexcept;
	const char* TouchProfileName(const Config& config) noexcept;
	Config ResolveConfig(const DisplayScale& display, DeviceMode mode, bool touch,
		const EraserSizes& sizes = {}) noexcept;

	struct FineBandDiagnostics
	{
		double speed = 0, enterProgress = 0, releaseProgress = 0, changeProgress = 0;
		bool held = false;
		int direction = 0;
	};
	class Controller
	{
	public:
		void Reset(float x, float y, double seconds,
			StartKind kind = StartKind::Hover, const Config& config = Config{}, float initialDiameterDip = 0,
			const ContactAreaSample* contactArea = nullptr) noexcept;
		void ResetPreview(float x, float y, double seconds, const Config& config, float diameterDip = 0) noexcept;
		void BeginContact(const Controller* preview, float x, float y, double seconds, const Config& config) noexcept;
		void LeaveContact(float x,float y,double seconds,float acceptedDiameterPx) noexcept;
		void RefreshCompatibleConfig(const Config& config) noexcept;
		double AcceptedSeconds() const noexcept { return sampleState_.time; }
		float UpdatePosition(float x, float y, double seconds,
			const ContactAreaSample* contactArea = nullptr, bool terminal = false) noexcept;
		float Advance(double seconds) noexcept;
		void PauseForReconnect(double seconds) noexcept;
		float ResumeFromReconnect(float x, float y, double seconds) noexcept;
		float Diameter() const noexcept;
		float DiameterDip() const noexcept;
		double SecondsSinceMovement(double seconds) const noexcept;
		double Speed() const noexcept { return frameState_.speed; }
		double SweepSpeed() const noexcept { return frameState_.sweepSpeed; }
		bool Sweeping() const noexcept { return frameState_.sweeping; }
		bool TargetLimited() const noexcept;
		bool SweepQualified() const noexcept { return frameState_.sweepQualified; }
		bool PreviewOnly() const noexcept { return previewOnly_; }
		float TargetDiameter() const noexcept;
		float TargetDiameterDip() const noexcept;
		float SweepEvidenceCapDiameterDip() const noexcept;
		bool TouchUnlocked() const noexcept;
		FineBandDiagnostics FineDiagnostics() const noexcept;
		ContactAreaDiagnostics AreaDiagnostics(double seconds) const noexcept;
		double NextAreaWakeSeconds() const noexcept;
		bool IsPaused() const noexcept { return paused_; }
		bool NeedsAnimation(double seconds) const noexcept;
		const Config& Configuration() const noexcept { return config_; }
		double SweepEvidenceSeconds() const noexcept { return frameState_.sweepEvidence; }

	private:
		struct MotionSegment
		{
			double startTime = 0.0;
			double endTime = 0.0;
			double distance = 0.0;
			double x0 = 0, y0 = 0, x1 = 0, y1 = 0;
		};

		struct DynamicsState
		{
			double time = 0.0;
			double logDiameter = 0.0;
			double logTarget = 0.0;
			double holdUntil = 0.0;
			double decreaseSince = 0.0;
			double maximumDisplacement = 0.0;
			double sweepEvidence = 0.0;
			double lastMovementTime = 0.0;
			double speed = 0.0;
			double sweepSpeed = 0.0;
			double fineSpeed = 0, fineEnterEvidence = 0, fineReleaseEvidence = 0;
			double fineChangeEvidence = 0, fineStableSeconds = 0;
			int fineDirection = 0, finePendingDirection = 0;
			bool fineHeld = false;
			float areaFloorDip = 0.0f;
			bool sweepQualified = false;
			bool sweeping = false;
			bool decreasePending = false;
			bool shrinking = false;
		};

		struct AreaState
		{
			ContactAreaDiagnostics diagnostic;
			float candidateWidth = 0, candidateHeight = 0, referenceWidth = 0, referenceHeight = 0;
			double lastSampleSeconds = 0, lastValidSeconds = 0, readySeconds = 0, badSince = -1;
			bool hasSample = false, hasCandidate = false;
		};
		AreaState area_;
		bool AreaEligible() const noexcept;
		void ObserveContactArea(const ContactAreaSample& sample,double seconds,bool moving) noexcept;
		double AreaExpirySeconds() const noexcept;
		float AreaReferenceFloor(double seconds) const noexcept;
		void ShiftAreaTime(double seconds) noexcept;
		static constexpr size_t kSegmentCapacity = 64;
		std::array<MotionSegment, kSegmentCapacity> segments_ = {};
		size_t segmentCount_ = 0;
		Config config_;
		DynamicsState sampleState_;
		DynamicsState frameState_;
		double acceptedX_ = 0.0;
		double acceptedY_ = 0.0;
		double acceptedTime_ = 0.0;
		double downX_ = 0.0;
		double downY_ = 0.0;
		double movementX_ = 0.0, movementY_ = 0.0;
		double pauseTime_ = 0.0;
		bool touchStartup_ = false;
		bool previewOnly_ = false;
		bool detached_ = false, detachedHoverObserved_ = false;
		double detachedStandbyDip_ = 32.0;
		bool initialized_ = false;
		bool paused_ = false;

		void AddSegment(const MotionSegment& segment) noexcept;
		void AdvanceDetachedState(DynamicsState& state,double seconds) const noexcept;
		double MotionSpeed(double seconds, double windowSeconds) const noexcept;
		bool HasMotionSupport(double seconds,double windowSeconds = 0,double noiseRatio = 1.0) const noexcept;
		double TargetLogDiameter(double speed, double maximumDisplacement) const noexcept;
		double IdleDiameterDip(const DynamicsState& state) const noexcept;
		void ObserveFineIntent(DynamicsState& state, double referenceTarget, double dt, bool sweepPermitted) const noexcept;
		void FollowFineTarget(DynamicsState& state, double target, double dt) const noexcept;
		void AdvanceState(DynamicsState& state, double seconds,
			const MotionSegment* incoming = nullptr, double incomingX = 0.0,
			double incomingY = 0.0, bool effectiveMovement = false) const noexcept;
		void FollowTarget(DynamicsState& state, double endTime,
			double target, double realMotionSpeed, bool areaMotionEvidence = false) const noexcept;
	};

	// 非Touch尺寸会话；产品按入口分别持有，离面延续逻辑状态但不产生几何。
	class MouseLifecycle
	{
	public:
		void Configure(const Config& config) noexcept;
		void ObserveHover(float x, float y, double seconds) noexcept;
		uint64_t BeginContact(Controller& controller, float x, float y, double seconds,
			const Config& config) noexcept;
		uint64_t ClaimContact(double seconds) noexcept;
		const char* LastHandoffReason() const noexcept { return handoffReason_; }
		bool LastHandoffInherited() const noexcept { return handoffInherited_; }
		float LastHandoffPreviousDiameter() const noexcept { return handoffPreviousDiameter_; }
		double LastHandoffHoverSeconds() const noexcept { return handoffHoverSeconds_; }
		void EndContact(Controller& controller, float acceptedDiameter, float x, float y, double seconds,
			bool anotherOwner, bool cancelled = false, uint64_t ownerToken = 0) noexcept;
		void CancelVisual() noexcept;
		float Advance(double seconds) noexcept;
		float VisualDiameter() const noexcept { return visualDiameter_; }
		float LogicalDiameter() const noexcept { return logicalDiameter_; }
		bool NeedsAnimation(double seconds) const noexcept;
		bool ContactOwned() const noexcept { return contactOwned_; }
		bool HasPosition() const noexcept { return hasPosition_; }
		float X() const noexcept { return x_; }
		float Y() const noexcept { return y_; }
		double LastEventSeconds() const noexcept { return lastEventSeconds_; }
		bool Releasing() const noexcept { return releasing_; }
		const Controller& PreviewController() const noexcept { return hover_; }

	private:
		Config config_;
		Controller hover_;
		bool hoverInitialized_ = false;
		float logicalDiameter_ = Config{}.StandardDiameterPx();
		float visualDiameter_ = Config{}.StandardDiameterPx();
		float releaseFrom_ = Config{}.StandardDiameterPx();
		float x_ = 0.0f, y_ = 0.0f;
		double lastEventSeconds_ = 0.0;
		double lastDownSeconds_ = 0.0;
		double lastHoverSeconds_ = 0.0;
		double releaseSeconds_ = 0.0;
		double visualTime_ = 0.0;
		bool configured_ = false;
		uint64_t ownerToken_ = 0;
		const char* handoffReason_ = "new-session";
		bool handoffInherited_ = false;
		float handoffPreviousDiameter_ = 0;
		double handoffHoverSeconds_ = 0;
		bool contactOwned_ = false;
		bool hasPosition_ = false;
		bool releaseCandidate_ = false;
		bool releasing_ = false;
	};

	struct WidthInterval
	{
		double startTimeSeconds = 0.0;
		double endTimeSeconds = 0.0;
		float startDiameter = 16.0f;
		float endDiameter = 16.0f;
		float minimumDiameter = 16.0f;
		float maximumDiameter = 160.0f;
		bool reanchor = false;
	};

	// 当前工具与历史端点分开。只在下一次真实几何提交时消费尺寸断点。
	struct ContactSizeState
	{
		float effectiveDiameterPx = 32.0f;
		float resumeDiameterPx = 32.0f;
		double effectiveTimeSeconds = 0.0;
		double breakTimeSeconds = 0.0;
		bool breakPending = false;
		void Reset(float diameterPx, double seconds) noexcept;
		void Update(float diameterPx, double seconds, bool stationary) noexcept;
		WidthInterval MakeInterval(double fromModelTime, double toModelTime, float oldDiameter,
			float newDiameter, double rawSeconds, const Config& config) const noexcept;
		void Accepted(const WidthInterval& interval) noexcept;
	};

	struct Diagnostics
	{
		bool active = false;
		bool preview = false;
		bool eraserContact = false;
		uint32_t inputType = 0;
		uintptr_t monitor = 0;
		uint64_t displayGeneration = 0, displayRevision = 0;
		DeviceMode requestedDeviceMode = DeviceMode::Laptop;
		TouchProfileSource touchProfileSource = TouchProfileSource::NotTouch;
		float touchProfileWeight = 0, touchSurfaceLongEdgeMm = 0;
		ScaleSource motionSource = ScaleSource::DipOnly;
		MotionUnit motionUnit = MotionUnit::DipPerSecond;
		InputSource inputSource;
		uint32_t contactId = 0;
		uint64_t contactGeneration = 0;
		bool inputContact = false, inputPositionValid = false, cursorVisible = false;
		float inputCanvasXpx = 0, inputCanvasYpx = 0, cursorCanvasXpx = 0, cursorCanvasYpx = 0;
		ResponseModel response = ResponseModel::IndirectDip;
		bool inputMapped = false;
		float rhoMmPerDip = 0.0f;
		float referenceMmPerDip = 0.25f;
		float penBeta = 0.5f;
		float heuristicGain = 1.0f;
		float fineToStandardSpeed = 0, sweepEnterSpeed = 0, sweepExitSpeed = 0, largeTargetSpeed = 0;
		float sweepGain = 1.0f;
		float evidenceCapDiameterDip = 32;
		double evidenceStartSeconds = 0, evidenceFullSeconds = 0, evidenceDecaySeconds = 0;
		double growthTauSeconds = 0, largeGrowthTauSeconds = 0;
		double maximumLogGrowthPerSecond = 0, largeLogGrowthPerSecond = 0;
		EraserSizes sizes;
		float dpiX = 96, dpiY = 96;
		float dipPerPixelX = 1, dipPerPixelY = 1, motionPerPixelX = 1, motionPerPixelY = 1;
		float targetDiameterDip = 32, manualWidthCm = 0, manualHeightCm = 0;
		int pixelWidth = 0, pixelHeight = 0;
		bool touchUnlocked = false;
		bool needsAnimation = false;
		ContactAreaDiagnostics contactArea;
		FineBandDiagnostics fine;
		uint32_t selectedTool = 0, effectiveTool = 0;
		InputEntry entry = InputEntry::MouseLeft;
		EraserKind eraserKind = EraserKind::Speed;
		PenResponseChoice formalPenResponse = PenResponseChoice::Automatic;
		bool developmentResponseOverride = false, sessionInherited = false;
		const char* sessionReason = "none";
		double downSeconds = 0, hoverSeconds = 0, frameSeconds = 0;
		float previousShownDiameterPx = 0;
		float downDiameterPx = 0, firstPointRadiusPx = 0;
		float effectiveDiameterDip = 32, cursorDiameterPx = 32, nextRadiusPx = 16;
		float historyRadiusPx = 0, resumedMaxRadiusPx = 0;
		float resumedLeft = 0, resumedTop = 0, resumedRight = 0, resumedBottom = 0;
		std::array<float,9> boundaryPoints{}; // 有界的历史点/尺寸锚点/新末点 (x,y,直径px)。
		bool resumedWithAnchor = false, sweeping = false, qualified = false, limited = false;
		double speed = 0, sweepSpeed = 0, evidenceSeconds = 0, idleSeconds = 0;
		uint64_t frameSequence = 0, realPointCount = 0, idleModelReanchors = 0;
	};

	float InterpolateDiameter(const WidthInterval& interval, double seconds) noexcept;
	float ContactDiameter(float acceptedRadius, float fallbackDiameter) noexcept;
}
