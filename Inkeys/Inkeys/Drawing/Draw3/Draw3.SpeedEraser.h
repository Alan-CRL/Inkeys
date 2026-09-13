#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace Inkeys::Drawing::Draw3::SpeedEraser
{
	enum class DeviceMode { LargeScreen, Laptop };
	enum class ScaleSource { Dip, Physical };
	enum class StartKind { Hover, Touch };

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
		bool directTouchMapped = false;

		friend bool operator==(const DisplayScale&, const DisplayScale&) = default;
	};

	// 全部为直径 DIP。固定模式直接消费此属性，不实例化速度控制器。
	struct EraserSizes
	{
		float minimumDiameterDip = 16.0f;
		float standardDiameterDip = 32.0f;
		float maximumDiameterDip = 160.0f;
		float touchStartDiameterDip = 16.0f;
		float fixedDiameterDip = 50.0f;
		friend bool operator==(const EraserSizes&, const EraserSizes&) = default;
	};
	float DiameterToCanvasPx(float diameterDip, const DisplayScale& display) noexcept;
	float FixedDiameterPx(float selectedDiameterDip, const DisplayScale& display) noexcept;

	struct Config
	{
		DisplayScale display;
		EraserSizes sizes;
		DeviceMode mode = DeviceMode::Laptop;
		ScaleSource motionSource = ScaleSource::Dip;
		float motionPerPixelX = 1.0f;
		float motionPerPixelY = 1.0f;
		float minimumDiameterPx = 16.0f;
		float maximumDiameterPx = 160.0f;
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

	Config ResolveConfig(const DisplayScale& display, DeviceMode mode, bool touch,
		const EraserSizes& sizes = {}) noexcept;

	class Controller
	{
	public:
		void Reset(float x, float y, double seconds,
			StartKind kind = StartKind::Hover, const Config& config = Config{}) noexcept;
		float UpdatePosition(float x, float y, double seconds) noexcept;
		float Advance(double seconds) noexcept;
		void PauseForReconnect(double seconds) noexcept;
		float ResumeFromReconnect(float x, float y, double seconds) noexcept;
		float Diameter() const noexcept;
		float DiameterDip() const noexcept;
		double SecondsSinceMovement(double seconds) const noexcept;
		double Speed() const noexcept { return frameState_.speed; }
		bool Sweeping() const noexcept { return frameState_.sweeping; }
		float TargetDiameter() const noexcept;
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
			bool sweepQualified = false;
			bool sweeping = false;
			bool decreasePending = false;
			bool shrinking = false;
		};

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
		bool initialized_ = false;
		bool paused_ = false;

		void AddSegment(const MotionSegment& segment) noexcept;
		double MotionSpeed(double seconds, double windowSeconds) const noexcept;
		double TargetLogDiameter(double speed, double maximumDisplacement) const noexcept;
		double IdleDiameterDip(const DynamicsState& state) const noexcept;
		void AdvanceState(DynamicsState& state, double seconds,
			const MotionSegment* incoming = nullptr, double incomingX = 0.0,
			double incomingY = 0.0, bool effectiveMovement = false) const noexcept;
		void FollowTarget(DynamicsState& state, double endTime,
			double target, double realMotionSpeed) const noexcept;
	};

	// 鼠标定位没有运动控制器。收尾只接收已接受的直径值，不能回写擦除几何。
	class MouseLifecycle
	{
	public:
		void Configure(const Config& config) noexcept;
		void ObserveHover(float x, float y, double seconds) noexcept;
		void BeginContact(Controller& controller, float x, float y, double seconds,
			const Config& config) noexcept;
		void EndContact(Controller& controller, float acceptedDiameter, float x, float y, double seconds,
			bool anotherOwner, bool cancelled = false) noexcept;
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

	private:
		Config config_;
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
		uint32_t inputType = 0;
		DeviceMode mode = DeviceMode::Laptop;
		ScaleSource motionSource = ScaleSource::Dip;
		EraserSizes sizes;
		float dpiX = 96, dpiY = 96;
		float effectiveDiameterDip = 32, cursorDiameterPx = 32, nextRadiusPx = 16;
		float historyRadiusPx = 0, resumedMaxRadiusPx = 0;
		float resumedLeft = 0, resumedTop = 0, resumedRight = 0, resumedBottom = 0;
		std::array<float,9> boundaryPoints{}; // 有界的历史点/尺寸锚点/新末点 (x,y,直径px)。
		bool resumedWithAnchor = false, sweeping = false;
		double speed = 0, evidenceSeconds = 0, idleSeconds = 0;
		uint64_t frameSequence = 0, realPointCount = 0;
	};

	float InterpolateDiameter(const WidthInterval& interval, double seconds) noexcept;
	float ContactDiameter(float acceptedRadius, float fallbackDiameter) noexcept;
}
