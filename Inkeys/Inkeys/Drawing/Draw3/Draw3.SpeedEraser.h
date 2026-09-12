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

		// 固定中档参数集中在此；速度和触摸范围使用动作标尺单位。
		double historyWindowSeconds = 0.080;
		double accelerationWindowSeconds = 0.040;
		double referenceWindowSeconds = 0.160;
		double holdSeconds = 0.180;
		double decreaseConfirmationSeconds = 0.180;
		double decreaseRatio = 0.08;
		double growthTauSeconds = 0.140;
		double acceleratedGrowthTauSeconds = 0.080;
		double shrinkTauSeconds = 0.240;
		double maximumLogGrowthPerSecond = 6.0;
		double maximumLogShrinkPerSecond = 3.0;
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
			double target, double speed, double growthTau) const noexcept;
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
