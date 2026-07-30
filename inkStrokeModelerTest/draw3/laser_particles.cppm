module;

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <array>
#include <cstddef>
#include <cstdint>
#include <d3d11.h>
#include <span>
#include <windows.h>
#include <wrl/client.h>

export module draw3.laser_particles;

export namespace draw3
{
	inline constexpr uint32_t kLaserParticleCapacity = 2048;
	inline constexpr uint32_t kLaserParticlePathCapacity = 32;
	inline constexpr uint32_t kLaserParticlePathPointCapacity = 16384;
	inline constexpr uint32_t kLaserParticleDirtyRegionCapacity = 512;
	inline constexpr uint32_t kInvalidLaserParticlePathSlot = 0xFFFFFFFFu;

	// GPU 粒子行为参数均使用 DIP；提交 Compute Shader 前只乘一次当前 dpiScale。
	struct LaserParticleConfig
	{
		float idleEmissionRatePerSecond = 6.0f;
		float motionEmissionSpacingDip = 4.0f;
		float maximumEmissionRatePerSecond = 48.0f;
		uint32_t maximumSpawnPerFrame = 96;
		float lifetimeSeconds = 3.0f;
		float minimumSpeedDipPerSecond = 12.0f;
		float maximumSpeedDipPerSecond = 96.0f;
		float inputSpeedInfluence = 0.08f;
		float speedResponseSeconds = 0.080f;
		float spreadSeconds = 0.120f;
		float maximumLateralExtraDip = 10.0f;
		float minimumRadiusDip = 0.65f;
		float maximumRadiusDip = 1.10f;
		float glowExtentDip = 3.0f;
		float glowRed = 1.0f;
		float glowGreen = 0.55f;
		float glowBlue = 0.62f;
		float glowAlpha = 0.24f;
		float minimumBrightness = 0.68f;
		float maximumBrightness = 1.0f;
		float breathingAmplitude = 0.12f;
		float minimumBreathingFrequencyHz = 0.8f;
		float maximumBreathingFrequencyHz = 1.4f;
		float breathingRampSeconds = 0.20f;
		float positionResponseSeconds = 0.040f;
		float predictionCorrectionSpeedMultiplier = 2.0f;
		float predictionJumpThresholdDip = 6.0f;
	};

	bool IsValidLaserParticleConfig(const LaserParticleConfig& configuration) noexcept;

	struct LaserParticlePathHandle
	{
		uint32_t slot = kInvalidLaserParticlePathSlot;
		uint32_t generation = 0;

		constexpr bool IsValid() const noexcept
		{
			return slot < kLaserParticlePathCapacity && generation != 0;
		}

		friend bool operator==(const LaserParticlePathHandle&,
			const LaserParticlePathHandle&) = default;
	};

	// 与 HLSL LaserParticlePathPoint 一致；arcLength 是真实前缀和当前预测尾的累计弧长。
	struct LaserParticlePathPoint
	{
		float x = 0.0f;
		float y = 0.0f;
		float radius = 0.0f;
		float arcLength = 0.0f;
	};

	static_assert(sizeof(LaserParticlePathPoint) == 16,
		"LaserParticlePathPoint 必须与 HLSL 结构化缓冲区保持一致");

	// 与 HLSL LaserParticlePathHeader 一致，由 CPU 每帧发布给两个 Compute Shader。
	struct LaserParticlePathHeader
	{
		uint32_t generation = 0;
		uint32_t pointCount = 0;
		uint32_t ended = 0;
		uint32_t active = 0;
		float filteredInputSpeed = 0.0f;
		float padding[3] = {};
	};

	static_assert(sizeof(LaserParticlePathHeader) == 32,
		"LaserParticlePathHeader 必须与 HLSL 结构化缓冲区保持一致");

	// 固定 128 字节 GPU 状态；整笔重绘不会触碰此缓冲区。
	struct LaserGpuParticle
	{
		float position[2] = {};
		float tangent[2] = { 1.0f, 0.0f };
		float pathArcLength = 0.0f;
		float birthArcLength = 0.0f;
		float flowSpeed = 0.0f;
		float speedJitter = 1.0f;
		float ageSeconds = 0.0f;
		float lifetimeSeconds = 0.0f;
		float lateralOffset = 0.0f;
		float lateralStartOffset = 0.0f;
		float lateralExtra = 0.0f;
		float pathRadius = 0.0f;
		float baseRadius = 0.0f;
		float currentRadius = 0.0f;
		float opacity = 0.0f;
		float baseBrightness = 1.0f;
		float currentBrightness = 1.0f;
		float breathingFrequencyHz = 1.0f;
		float breathingPhase = 0.0f;
		float breathingAmplitude = 0.0f;
		float breathingRampSeconds = 0.20f;
		float predictionCorrectionActive = 0.0f;
		uint32_t pathSlot = kInvalidLaserParticlePathSlot;
		uint32_t pathGeneration = 0;
		uint32_t segmentCursor = 0;
		uint32_t seed = 0;
		uint32_t alive = 0;
		uint32_t padding1[3] = {};
	};

	static_assert(sizeof(LaserGpuParticle) == 128,
		"LaserGpuParticle 必须与 HLSL 结构化缓冲区保持一致");

	struct LaserParticleEmissionSchedule
	{
		uint32_t count = 0;
		float fractionalParticles = 0.0f;
		float emissionRatePerSecond = 0.0f;
	};

	// 用时间积分静止基线和 L0 前端正向速度；超额整数部分不积压到下一帧。
	LaserParticleEmissionSchedule ScheduleLaserParticleEmission(float wallDeltaSeconds,
		float forwardArcLength, float fractionalParticles, uint32_t remainingFrameBudget,
		const LaserParticleConfig& configuration) noexcept;

	float LaserParticleTargetSpeed(float filteredInputSpeed, float dpiScale,
		const LaserParticleConfig& configuration) noexcept;
	float SmoothLaserParticleSpeed(float currentSpeed, float targetSpeed,
		float deltaSeconds, float responseSeconds) noexcept;
	float LaserParticleLifeFactor(float ageSeconds, float lifetimeSeconds) noexcept;
	float EvaluateLaserParticleBrightness(float baseBrightness, float ageSeconds,
		float breathingFrequencyHz, float breathingPhase,
		const LaserParticleConfig& configuration) noexcept;
	float MaximumLaserParticleTravelDip(
		const LaserParticleConfig& configuration) noexcept;
	float LaserParticlePathRetirementSeconds(bool hasEmitted,
		const LaserParticleConfig& configuration) noexcept;
	int64_t LaserParticleLifetimeDeadlineQpc(int64_t nowQpc,
		int64_t qpcFrequency, const LaserParticleConfig& configuration) noexcept;

	struct LaserParticleEmissionAnchor
	{
		float x = 0.0f;
		float y = 0.0f;
		bool valid = false;
		bool predictionCorrectionActive = false;
	};

	// 正常预测位置直接跟随；异常跳变按正常粒子速度的固定倍率限速追赶。
	LaserParticleEmissionAnchor UpdateLaserParticleEmissionAnchor(
		LaserParticleEmissionAnchor current, float targetX, float targetY,
		float filteredInputSpeed, float wallDeltaSeconds, float dpiScale,
		const LaserParticleConfig& configuration) noexcept;

	struct LaserParticleArcAdvance
	{
		float arcLength = 0.0f;
		float actualAdvance = 0.0f;
	};

	// 到达当前组合路径末端时丢弃多余位移，后续路径更新不会补追。
	LaserParticleArcAdvance AdvanceLaserParticleArc(float arcLength, float pathEndArcLength,
		float speed, float lifeFactor, float motionDeltaSeconds) noexcept;

	uint32_t NextLaserParticlePathGeneration(uint32_t generation) noexcept;
	uint32_t ClampLaserParticlePathAppendCount(
		uint32_t currentPointCount, uint32_t requestedPointCount) noexcept;

	struct LaserParticleEmissionRequest
	{
		LaserParticlePathHandle path = {};
		float arcLength = 0.0f;
		float anchorX = 0.0f;
		float anchorY = 0.0f;
		uint32_t count = 0;
		uint32_t seedBase = 0;
	};

	struct LaserParticleShaderBytecode
	{
		const void* data = nullptr;
		size_t size = 0;
	};

	// 无 GPU 回读地追踪粒子最坏覆盖区；固定容量避免帧循环分配。
	class LaserParticleDirtyTracker
	{
	public:
		void Add(LaserParticlePathHandle path, RECT bounds, int64_t expiresQpc) noexcept;
		void ExpandPath(LaserParticlePathHandle path, RECT bounds) noexcept;
		RECT ActiveBounds(int64_t nowQpc) noexcept;
		bool HasActive(int64_t nowQpc) const noexcept;
		bool HasAny() const noexcept;
		void Clear() noexcept;

	private:
		struct Region
		{
			LaserParticlePathHandle path = {};
			RECT bounds = {};
			int64_t expiresQpc = 0;
			bool active = false;
		};

		std::array<Region, kLaserParticleDirtyRegionCapacity> regions_ = {};
	};

	// 管理固定 D3D11 粒子池、真实前缀/预测尾路径槽和 Compute Shader 调度。
	class LaserParticleSystem
	{
	public:
		bool Initialize(ID3D11Device* device, ID3D11DeviceContext* context,
			LaserParticleShaderBytecode updateShader,
			LaserParticleShaderBytecode emitShader) noexcept;
		void Release() noexcept;
		void Configure(const LaserParticleConfig& configuration, float dpiScale,
			float coreRadiusRatio) noexcept;
		bool IsAvailable() const noexcept;

		LaserParticlePathHandle AcquirePath() noexcept;
		uint32_t AppendRealPathPoints(LaserParticlePathHandle path,
			std::span<const LaserParticlePathPoint> points) noexcept;
		uint32_t ReplacePredictionPathPoints(LaserParticlePathHandle path,
			std::span<const LaserParticlePathPoint> points) noexcept;
		void SetPathInputSpeed(LaserParticlePathHandle path, float filteredInputSpeed) noexcept;
		void EndPath(LaserParticlePathHandle path) noexcept;

		void Simulate(float wallDeltaSeconds, float motionDeltaSeconds) noexcept;
		void Emit(const LaserParticleEmissionRequest& request) noexcept;
		void Reset() noexcept;

		ID3D11ShaderResourceView* ParticleShaderResourceView() const noexcept;

	private:
		struct PathSlotState
		{
			LaserParticlePathHeader header = {};
			uint32_t realPointCount = 0;
			float retireSeconds = 0.0f;
			bool overflowed = false;
			bool hasEmitted = false;
		};

		bool IsCurrentPath(LaserParticlePathHandle path) const noexcept;
		void WritePathPointRange(uint32_t pathSlot, uint32_t firstPoint,
			std::span<const LaserParticlePathPoint> points) noexcept;
		bool UploadPathHeaders() noexcept;
		bool DispatchUpdate(float wallDeltaSeconds, float motionDeltaSeconds,
			bool resetAll) noexcept;
		void UnbindComputeResources() noexcept;

		Microsoft::WRL::ComPtr<ID3D11Device> device_;
		Microsoft::WRL::ComPtr<ID3D11DeviceContext> context_;
		Microsoft::WRL::ComPtr<ID3D11ComputeShader> updateShader_;
		Microsoft::WRL::ComPtr<ID3D11ComputeShader> emitShader_;
		Microsoft::WRL::ComPtr<ID3D11Buffer> particleBuffer_;
		Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> particleSRV_;
		Microsoft::WRL::ComPtr<ID3D11UnorderedAccessView> particleUAV_;
		Microsoft::WRL::ComPtr<ID3D11Buffer> pathPointBuffer_;
		Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> pathPointSRV_;
		Microsoft::WRL::ComPtr<ID3D11Buffer> pathHeaderBuffer_;
		Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> pathHeaderSRV_;
		Microsoft::WRL::ComPtr<ID3D11Buffer> updateConstants_;
		Microsoft::WRL::ComPtr<ID3D11Buffer> emitConstants_;
		std::array<PathSlotState, kLaserParticlePathCapacity> pathSlots_ = {};
		LaserParticleConfig configuration_ = {};
		float dpiScale_ = 1.0f;
		float coreRadiusRatio_ = 1.0f / 3.0f;
		uint32_t spawnCursor_ = 0;
		bool available_ = false;
	};
}
