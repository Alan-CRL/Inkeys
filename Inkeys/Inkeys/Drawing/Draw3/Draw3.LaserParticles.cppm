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

export module Inkeys.Drawing.Draw3.laser_particles;

export namespace Inkeys::Drawing::Draw3
{
	inline constexpr uint32_t kLaserParticleCapacity = 2048;
	inline constexpr uint32_t kLaserParticleDirtyRegionCapacity = 512;

	// GPU 粒子行为参数均使用 DIP；提交 Compute Shader 前只乘一次当前 dpiScale。
	struct LaserParticleConfig
	{
		float idleEmissionRatePerSecond = 6.0f;
		float motionEmissionSpacingDip = 1.8f;      // 2.5→1.8 更密集
		float maximumEmissionRatePerSecond = 90.0f; // 72→90
		uint32_t maximumSpawnPerFrame = 120;         // 96→120
		float minimumLifetimeSeconds = 0.55f;        // 0.70→0.55 缩短行程
		float maximumLifetimeSeconds = 0.75f;        // 1.00→0.75
		float minimumLaunchSpeedDipPerSecond = 18.0f; // 28→18 更接近墨迹
		float maximumLaunchSpeedDipPerSecond = 40.0f; // 64→40
		float maximumDeflectionAngleDegrees = 25.0f;
		float shrinkStartTravelRatio = 0.10f;
		float endRadiusScale = 0.20f;
		float minimumRadiusDip = 0.28f;              // 0.45→0.28 更小
		float maximumRadiusDip = 1.4f;               // 2.2→1.4
		float sizeDistributionExponent = 2.0f;       // 1.6→2.0 偏向小粒子
		float sizeBrightnessCorrelation = 0.72f;
		float sizeTravelCorrelation = 0.30f;
		float glowRadiusScale = 2.0f;                // 2.5→2.0 辉光范围收窄
		float glowRed = 1.0f;
		float glowGreen = 0.32f;
		float glowBlue = 0.40f;
		float glowAlpha = 0.18f;                     // 0.28→0.18 辉光更浅
		// 粒子核心已直接使用激光 border 红；以下两项保留配置兼容，PS 不再参与核心色相。
		float coreColorWhiteMix = 0.0f;
		float coreColorWhiteMixJitter = 0.0f;
		float minimumBrightness = 0.42f;
		float maximumBrightness = 1.0f;
		float breathingAmplitude = 0.18f;
		float minimumBreathingFrequencyHz = 0.8f;
		float maximumBreathingFrequencyHz = 1.4f;
		float breathingRampSeconds = 0.20f;
	};

	bool IsValidLaserParticleConfig(const LaserParticleConfig& configuration) noexcept;

	// 固定 80 字节 GPU 状态；出生后只在屏幕空间按自身速度和寿命独立更新。
	struct LaserGpuParticle
	{
		float position[2] = {};
		float velocity[2] = {};
		float ageSeconds = 0.0f;
		float lifetimeSeconds = 0.0f;
		float traveledDistance = 0.0f;
		float maximumTravelDistance = 0.0f;
		float baseRadius = 0.0f;
		float currentRadius = 0.0f;
		float opacity = 0.0f;
		float baseBrightness = 1.0f;
		float currentBrightness = 1.0f;
		float breathingFrequencyHz = 1.0f;
		float breathingPhase = 0.0f;
		float breathingAmplitude = 0.0f;
		float breathingRampSeconds = 0.20f;
		uint32_t seed = 0;
		uint32_t alive = 0;
		uint32_t padding = 0;
	};

	static_assert(sizeof(LaserGpuParticle) == 80,
		"LaserGpuParticle 必须与 HLSL 结构化缓冲区保持一致");
	static_assert(offsetof(LaserGpuParticle, position) == 0);
	static_assert(offsetof(LaserGpuParticle, velocity) == 8);
	static_assert(offsetof(LaserGpuParticle, ageSeconds) == 16);
	static_assert(offsetof(LaserGpuParticle, traveledDistance) == 24);
	static_assert(offsetof(LaserGpuParticle, baseRadius) == 32);
	static_assert(offsetof(LaserGpuParticle, opacity) == 40);
	static_assert(offsetof(LaserGpuParticle, currentBrightness) == 48);
	static_assert(offsetof(LaserGpuParticle, breathingRampSeconds) == 64);
	static_assert(offsetof(LaserGpuParticle, seed) == 68);
	static_assert(offsetof(LaserGpuParticle, padding) == 76);

	struct LaserParticleEmissionSchedule
	{
		uint32_t count = 0;
		float fractionalParticles = 0.0f;
		float emissionRatePerSecond = 0.0f;
	};

	// 笔速只决定发射率；整数超额不积压，prediction 位移不会进入该接口。
	LaserParticleEmissionSchedule ScheduleLaserParticleEmission(float wallDeltaSeconds,
		float motionSpeedDipPerSecond, float fractionalParticles,
		uint32_t remainingFrameBudget,
		const LaserParticleConfig& configuration) noexcept;

	float LaserParticleLifeFactor(float ageSeconds, float lifetimeSeconds) noexcept;
	float LaserParticleRadiusScale(float traveledDistance, float maximumTravelDistance,
		const LaserParticleConfig& configuration) noexcept;
	struct LaserParticleBirthSample
	{
		float launchSpeedDipPerSecond = 0.0f;
		float lifetimeSeconds = 0.0f;
		float baseRadiusDip = 0.0f;
		float baseBrightness = 0.0f;
		float maximumTravelDistanceDip = 0.0f;
		bool valid = false;
	};

	// 与 EmitCS 一致：尺寸偏小分布决定基础层级，再与独立样本弱相关地决定亮度和射程。
	LaserParticleBirthSample ResolveLaserParticleBirthSample(
		float sizeSample, float launchSpeedSample, float lifetimeSample,
		float brightnessSample,
		const LaserParticleConfig& configuration) noexcept;
	float EvaluateLaserParticleBrightness(float baseBrightness, float ageSeconds,
		float breathingFrequencyHz, float breathingPhase,
		const LaserParticleConfig& configuration) noexcept;
	float LaserParticleGlowExtentDip(float currentRadiusDip,
		const LaserParticleConfig& configuration) noexcept;
	float MaximumLaserParticleTravelDip(
		const LaserParticleConfig& configuration) noexcept;
	int64_t LaserParticleLifetimeDeadlineQpc(int64_t nowQpc,
		int64_t qpcFrequency, const LaserParticleConfig& configuration) noexcept;

	struct LaserParticleEmissionDirection
	{
		float x = 0.0f;
		float y = 0.0f;
		bool valid = false;
	};

	// 与 EmitCS 一致：随机选择正/负法线，再在该法线附近均匀偏转。
	LaserParticleEmissionDirection ResolveLaserParticleEmissionDirection(
		float tangentX, float tangentY, float sideSample, float deflectionSample,
		const LaserParticleConfig& configuration) noexcept;

	struct LaserParticleMotionStep
	{
		float deltaX = 0.0f;
		float deltaY = 0.0f;
		float distance = 0.0f;
	};

	// 只依赖粒子自身状态，便于静态验证 Up 和 prediction 跳变不会修正存量粒子。
	LaserParticleMotionStep EvaluateLaserParticleMotionStep(float velocityX,
		float velocityY, float ageSeconds, float lifetimeSeconds,
		float motionDeltaSeconds) noexcept;

	struct LaserParticleEmissionRequest
	{
		float positionX = 0.0f;
		float positionY = 0.0f;
		float tangentX = 0.0f;
		float tangentY = 0.0f;
		float entityRadius = 0.0f;
		uint32_t count = 0;
		uint32_t seedBase = 0;
	};

	// 返回未裁剪的批次包络；调用方必须按当前画布尺寸逐帧裁剪。
	RECT ConservativeLaserParticleBatchBounds(
		const LaserParticleEmissionRequest& request,
		const LaserParticleConfig& configuration, float dpiScale,
		float coreRadiusRatio) noexcept;

	struct LaserParticleDirtySnapshot
	{
		RECT activeBounds = {};
		bool hasActive = false;
		bool expiredAny = false;
	};

	struct LaserParticleShaderBytecode
	{
		const void* data = nullptr;
		size_t size = 0;
	};

	// 无 GPU 回读地按发射批次追踪最坏覆盖区；固定容量避免帧循环分配。
	class LaserParticleDirtyTracker
	{
	public:
		void Add(RECT bounds, int64_t expiresQpc) noexcept;
		LaserParticleDirtySnapshot Snapshot(int64_t nowQpc) noexcept;
		void Clear() noexcept;

	private:
		struct Region
		{
			RECT bounds = {};
			int64_t expiresQpc = 0;
			bool active = false;
		};

		std::array<Region, kLaserParticleDirtyRegionCapacity> regions_ = {};
	};

	// 管理固定 D3D11 粒子池和仅使用 u0 的 Compute Shader 调度。
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

		void Step(float wallDeltaSeconds, float motionDeltaSeconds,
			bool simulateExisting,
			std::span<const LaserParticleEmissionRequest> emissionRequests) noexcept;
		void Reset() noexcept;

		ID3D11ShaderResourceView* ParticleShaderResourceView() const noexcept;

	private:
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
		Microsoft::WRL::ComPtr<ID3D11Buffer> updateConstants_;
		Microsoft::WRL::ComPtr<ID3D11Buffer> emitConstants_;
		LaserParticleConfig configuration_ = {};
		float dpiScale_ = 1.0f;
		float coreRadiusRatio_ = 1.0f / 3.0f;
		uint32_t spawnCursor_ = 0;
		bool available_ = false;
	};
}
