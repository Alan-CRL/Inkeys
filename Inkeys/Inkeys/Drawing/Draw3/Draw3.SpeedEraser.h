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

	struct Config
	{
		DisplayScale display;
		DeviceMode mode = DeviceMode::Laptop;
		ScaleSource motionSource = ScaleSource::Dip;
		ScaleSource coverageSource = ScaleSource::Dip;
		float motionPerPixelX = 1.0f;
		float motionPerPixelY = 1.0f;
		float minimumDiameterPx = 16.0f;
		float maximumDiameterPx = 160.0f;
		float minimumSpeed = 30.0f;
		float maximumSpeed = 700.0f;
		float touchUnlockStart = 2.0f;
		float touchUnlockEnd = 6.0f;
		// 本轮标准值明确等于原最小值；不引入另一套固定像素或固定橡皮设置。
		float StandardDiameterPx() const noexcept { return minimumDiameterPx; }

		// 固定中档参数集中在此；速度和触摸范围使用动作标尺单位。
		double historyWindowSeconds = 0.080;
		double referenceWindowSeconds = 0.160;
		double evidenceStartSeconds = 0.160;
		double evidenceFullSeconds = 0.380;
		double evidenceDecaySeconds = 0.200;
		double evidenceSpeedStart = 0.20;
		double evidenceSpeedFull = 0.75;
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
		double settleLogTolerance = 0.003;

		friend bool operator==(const Config&, const Config&) = default;
	};

	Config ResolveConfig(const DisplayScale& display, DeviceMode mode, bool touch) noexcept;

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
		float TargetDiameter() const noexcept;
		bool IsPaused() const noexcept { return paused_; }
		bool NeedsAnimation(double seconds) const noexcept;
		const Config& Configuration() const noexcept { return config_; }
		double SweepEvidenceSeconds() const noexcept { return sampleState_.sweepEvidence; }

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
		double pauseTime_ = 0.0;
		bool touchStartup_ = false;
		bool initialized_ = false;
		bool paused_ = false;

		void AddSegment(const MotionSegment& segment) noexcept;
		double MotionSpeed(double seconds, double windowSeconds) const noexcept;
		double TargetLogDiameter(double speed, double maximumDisplacement) const noexcept;
		void AdvanceState(DynamicsState& state, double seconds,
			const MotionSegment* incoming = nullptr, double incomingX = 0.0,
			double incomingY = 0.0) const noexcept;
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
	};

	float InterpolateDiameter(const WidthInterval& interval, double seconds) noexcept;
	float ContactDiameter(float acceptedRadius, float fallbackDiameter) noexcept;
}
