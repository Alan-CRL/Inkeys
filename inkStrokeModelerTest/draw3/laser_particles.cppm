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
		float particlesPerPixel = 0.5f;
		uint32_t maximumSpawnPerFrame = 96;
		float minimumLifetimeSeconds = 0.45f;
		float maximumLifetimeSeconds = 0.70f;
		float minimumSpeedDipPerSecond = 12.0f;
		float maximumSpeedDipPerSecond = 96.0f;
		float inputSpeedInfluence = 0.08f;
		float speedResponseSeconds = 0.080f;
		float spreadSeconds = 0.120f;
		float maximumLateralExtraDip = 1.5f;
		float minimumTravelDip = 24.0f;
		float maximumTravelDip = 56.0f;
		float minimumRadiusDip = 0.65f;
		float maximumRadiusDip = 1.10f;
		float glowExtentDip = 3.0f;
		float fadeStartFraction = 0.55f;
		float convergenceSeconds = 0.220f;
		float positionResponseSeconds = 0.040f;
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

	// 与 HLSL LaserParticlePathPoint 一致；arcLength 是追加式真实路径累计弧长。
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
		float traveledDistance = 0.0f;
		float maximumTravelDistance = 0.0f;
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
		float convergeStartOpacity = 0.0f;
		float convergeStartRadius = 0.0f;
		float convergeStartOffset = 0.0f;
		float convergeStartPosition[2] = {};
		uint32_t pathSlot = kInvalidLaserParticlePathSlot;
		uint32_t pathGeneration = 0;
		uint32_t segmentCursor = 0;
		uint32_t seed = 0;
		uint32_t alive = 0;
		uint32_t phase = 0;
		uint32_t padding1[2] = {};
	};

	static_assert(sizeof(LaserGpuParticle) == 128,
		"LaserGpuParticle 必须与 HLSL 结构化缓冲区保持一致");

	struct LaserParticleEmissionSchedule
	{
		uint32_t count = 0;
		float distanceSinceLastEmission = 0.0f;
	};

	// 按真实新增弧长计算本帧发射量；超出预算的整数部分直接丢弃，不形成下一帧积压。
	LaserParticleEmissionSchedule ScheduleLaserParticleEmission(float appendedArcLength,
		float distanceSinceLastEmission, uint32_t remainingFrameBudget,
		const LaserParticleConfig& configuration) noexcept;

	float LaserParticleTargetSpeed(float filteredInputSpeed, float dpiScale,
		const LaserParticleConfig& configuration) noexcept;
	float SmoothLaserParticleSpeed(float currentSpeed, float targetSpeed,
		float deltaSeconds, float responseSeconds) noexcept;

	struct LaserParticleArcAdvance
	{
		float arcLength = 0.0f;
		float traveledDistance = 0.0f;
	};

	// 到达当前真实路径末端时丢弃多余位移，后续追加路径不会补追。
	LaserParticleArcAdvance AdvanceLaserParticleArc(float arcLength, float pathEndArcLength,
		float traveledDistance, float maximumTravelDistance, float speed,
		float motionDeltaSeconds) noexcept;

	uint32_t NextLaserParticlePathGeneration(uint32_t generation) noexcept;
	uint32_t ClampLaserParticlePathAppendCount(
		uint32_t currentPointCount, uint32_t requestedPointCount) noexcept;
	bool LaserParticleConvergesToEdge(uint32_t seed) noexcept;

	struct LaserParticleEmissionRequest
	{
		LaserParticlePathHandle path = {};
		float startArcLength = 0.0f;
		float endArcLength = 0.0f;
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
		void EndPath(LaserParticlePathHandle path, int64_t expiresQpc) noexcept;
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

	// 管理固定 D3D11 粒子池、真实路径槽和 Compute Shader 调度。
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
		uint32_t AppendPathPoints(LaserParticlePathHandle path,
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
			float retireSeconds = 0.0f;
			bool overflowed = false;
			bool hasEmitted = false;
		};

		bool IsCurrentPath(LaserParticlePathHandle path) const noexcept;
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
