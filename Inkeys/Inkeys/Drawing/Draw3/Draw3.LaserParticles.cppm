module;

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <array>
#include <cstddef>
#include <cstdint>
#include <d3d11.h>
#include <memory>
#include <span>
#include <vector>
#include <windows.h>
#include <wrl/client.h>

export module Inkeys.Drawing.Draw3.laser_particles;

export namespace Inkeys::Drawing::Draw3
{
	inline constexpr uint32_t kLaserParticleCapacity = 32768;
	inline constexpr uint32_t kLaserParticleDirtyRegionCapacity = 512;

	enum class LaserParticleKind : uint32_t { Trail, ClickWave, Dissolve };
	// 飘散与光波画在激光主体之下；行进粒子画在主体之上，压住光晕但避开实体。
	enum class LaserParticlePass : uint32_t { UnderBody, OverBody };
	static_assert(static_cast<uint32_t>(LaserParticleKind::ClickWave) == 1u); // 与 UpdateCS 的种类常量同步。
	static_assert(static_cast<uint32_t>(LaserParticleKind::Dissolve) == 2u);

	struct LaserDissolveParameters
	{
		uint32_t durationMilliseconds = 900;
		uint32_t gatherMilliseconds = 350;
		uint32_t maximumSeeds = 16384;
		float seedsPerDip = 3.0f;
		// 飘散方向由设置选择：向左上或向右上（屏幕坐标 y 向下）。
		float leftUpWindAngleDegrees = 210.0f;
		float rightUpWindAngleDegrees = 330.0f;
		// 风速是粒子被加速到的终值；每粒乘一个随机比例，快慢不一才会拉成缕。
		float windSpeedDipPerSecond = 140.0f;
		float minimumWindSpeedFraction = 0.40f;
		float maximumWindSpeedFraction = 1.25f;
		// 被风带起来的快慢（越小起步越慢）。按出生位置的阵风起伏，再叠一点个体差异：
		// 相邻粒子相近、线上各段不同，才是一段一段被吹散。
		float accelerationPerSecond = 1.4f;
		float gustVariation = 0.55f;
		float accelerationJitter = 0.15f;
		float meanderDip = 22.0f; // 风场让流线左右蜿蜒的幅度
		float minimumBurstDip = 1.0f; // 出生时向四周散开的距离
		float maximumBurstDip = 8.0f;
		// 整条线同时开始碎：各像素在进度 crumbleMidpoint ± noiseAmplitude/2 内随机消失（约 0.5 秒碎完）。
		// 先后取两个哈希的平均，开头和收尾少、中间多，节奏缓入缓出。
		float crumbleMidpoint = 0.25f;
		float noiseAmplitude = 0.50f;
		float edgeHalfWidth = 0.06f; // 单个像素淡出占的进度半宽
		float minimumRadiusDip = 0.35f;
		float maximumRadiusDip = 1.6f;
		float radiusDistributionExponent = 2.8f;
		float endRadiusScale = 0.5f;
		uint32_t minimumLifetimeMilliseconds = 1100;
		uint32_t maximumLifetimeMilliseconds = 2500;
		float whiteFadeSeconds = 0.25f; // 白芯处出生的粒子褪成激光色的时间
	};
	inline constexpr LaserDissolveParameters kLaserDissolveParameters;

	// 清空画布时的飘散：从清空前的画布像素取位置与颜色，运动参数与激光飘散共用。
	struct CanvasClearParticleParameters
	{
		uint32_t maximumSeeds = 16384;
		float seedsPerSquareDip = 0.5f; // 每平方 DIP 墨迹的粒子数；超出上限时整页同比变稀
		uint32_t minimumAlpha = 48; // 低于它的像素视为没有墨迹（抗锯齿边缘）
		uint32_t releaseMilliseconds = 260;
	};
	inline constexpr CanvasClearParticleParameters kCanvasClearParticleParameters;
	inline constexpr uint32_t kCanvasClearParticleGroup = 0xFFFFFFFFu; // 不与任何激光笔画的分组号相同

	// 投影与屏幕像素哈希的 CPU/GPU 合同；进度为零时使用原合成路径。
	struct LaserDissolveFront
	{
		float directionX = 0.0f;
		float directionY = 0.0f;
		float projectionMinimum = 0.0f;
		float inverseProjectionSpan = 0.0f;
		float progress = 0.0f;
		friend bool operator==(const LaserDissolveFront&, const LaserDissolveFront&) = default;
	};

	LaserDissolveFront MakeLaserDissolveFront() noexcept;
	float LaserDissolveCoverage(const LaserDissolveFront& front, float x, float y) noexcept;
	uint32_t LaserDissolveSeedCount(double lengthDip) noexcept;

	struct LaserDissolveParticleControl
	{
		int64_t gatherStartQpc = 0;
		float gatherProgress = 0.0f;
		bool stop = false;
		int64_t gatherEndQpc = 0;
	};

	struct LaserParticleOptions
	{
		static constexpr uint32_t Enabled = 1u;
		static constexpr uint32_t Trail = 2u;
		static constexpr uint32_t ClickWave = 4u;
		static constexpr uint32_t Dissolve = 8u;
		static constexpr uint32_t ClearDissolve = 16u;
		static constexpr uint32_t WindRightUp = 32u; // 飘散方向：未置位向左上，置位向右上
		static constexpr uint32_t Effects = Trail | ClickWave | Dissolve | ClearDissolve;
		// 与配置默认值一致：画布粒子总开关关闭，子项与点击光波开启。
		static constexpr uint32_t Defaults = Effects;

		static constexpr uint32_t Pack(bool enabled, bool trail, bool clickWave, bool dissolve) noexcept
		{
			return (enabled ? Enabled : 0u) | (trail ? Trail : 0u) |
				(clickWave ? ClickWave : 0u) | (dissolve ? Dissolve : 0u);
		}
	};

	constexpr uint32_t EffectiveLaserParticleEffects(uint32_t requested, bool available) noexcept
	{
		if (!available) return 0u;
		// 点击光波不属于画布粒子效果，只看它自己的开关；其余三项还要总开关开启。
		const uint32_t particles = LaserParticleOptions::Trail | LaserParticleOptions::Dissolve |
			LaserParticleOptions::ClearDissolve;
		return (requested & LaserParticleOptions::ClickWave) |
			((requested & LaserParticleOptions::Enabled) ? requested & particles : 0u);
	}

	constexpr float LaserDissolveWindAngleDegrees(uint32_t requested) noexcept
	{
		return (requested & LaserParticleOptions::WindRightUp)
			? kLaserDissolveParameters.rightUpWindAngleDegrees : kLaserDissolveParameters.leftUpWindAngleDegrees;
	}

	struct LaserParticleConfig
	{
		float idleEmissionRatePerSecond = 6.0f;
		float motionEmissionSpacingDip = 1.8f;
		float maximumEmissionRatePerSecond = 90.0f;
		uint32_t maximumSpawnPerFrame = 120;
		float minimumLifetimeSeconds = 0.55f;
		float maximumLifetimeSeconds = 0.75f;
		float minimumLaunchSpeedDipPerSecond = 18.0f;
		float maximumLaunchSpeedDipPerSecond = 40.0f;
		float maximumDeflectionAngleDegrees = 25.0f;
		float backwardBiasDegrees = 12.0f; // 喷出方向整体朝运动反方向偏一点
		// 出生点离中心线的距离 = 实体半径 × 该比例 + 粒子自身半径：贴着内芯散射带的外沿出生，不压到内芯。
		float trailBirthOffsetRatio = 0.5f;
		float shrinkStartTravelRatio = 0.10f;
		float endRadiusScale = 0.20f;
		float minimumRadiusDip = 0.45f;
		float maximumRadiusDip = 1.8f;
		float sizeDistributionExponent = 2.2f;
		float sizeBrightnessCorrelation = 0.72f;
		float sizeTravelCorrelation = 0.30f;
		float minimumBrightness = 0.42f;
		float breathingAmplitude = 0.18f;
		float minimumBreathingFrequencyHz = 0.8f;
		float maximumBreathingFrequencyHz = 1.4f;
		float breathingRampSeconds = 0.20f;

		double clickMaximumSeconds = 0.250;
		float clickMaximumDisplacementDip = 6.0f;
		// 点击光波：一圈细线从实体边缘向外荡开，线宽与透明度随扩散衰减。
		uint32_t clickWaveLifetimeMilliseconds = 900;
		// 尺寸取激光实体半径的倍数，随粗细设置与 DPI 一起缩放（默认粗细下约 34 DIP 与 2.4 DIP）。
		float clickWaveExpansionRatio = 13.6f;
		float clickWaveLineWidthRatio = 0.96f;
	};

	bool IsValidLaserParticleConfig(const LaserParticleConfig& configuration) noexcept;
	bool IsLaserParticleClick(double durationSeconds, float maximumDisplacementSquaredPx,
		float dpiScale, const LaserParticleConfig& configuration) noexcept;

	// 来源提交画布像素、运动参数和自身颜色；模拟层不读取笔迹或材质。
	struct LaserParticleSeed
	{
		float position[2] = {};
		float velocity[2] = {};
		float color[4] = {};
		float radius = 0.0f;
		float brightness = 1.0f;
		float breathingFrequencyHz = 0.0f;
		float breathingPhase = 0.0f;
		float dragPerSecond = 0.0f;
		float angularSpeedRadians = 0.0f;
		float maximumTravelDistance = 0.0f;
		float breathingAmplitude = 0.0f;
		float breathingRampSeconds = 0.0f;
		uint32_t lifetimeMilliseconds = 0;
		uint32_t releaseDelayMilliseconds = 0;
		uint32_t randomSeed = 0;
		uint32_t group = 0;
		LaserParticleKind kind = LaserParticleKind::Trail;
	};

	struct LaserGpuParticle
	{
		LaserParticleSeed source;
		float position[2];
		float currentRadius;
		float opacity;
		float currentBrightness;
		uint32_t birthMilliseconds;
		uint32_t alive;
	};

	static_assert(sizeof(LaserParticleSeed) == 88);
	static_assert(offsetof(LaserParticleSeed, color) == 16);
	static_assert(offsetof(LaserParticleSeed, dragPerSecond) == 48);
	static_assert(offsetof(LaserParticleSeed, lifetimeMilliseconds) == 68);
	static_assert(offsetof(LaserParticleSeed, group) == 80);
	static_assert(offsetof(LaserParticleSeed, kind) == 84);
	static_assert(sizeof(LaserGpuParticle) == 116);
	static_assert(offsetof(LaserGpuParticle, position) == 88);
	static_assert(offsetof(LaserGpuParticle, opacity) == 100);
	static_assert(offsetof(LaserGpuParticle, birthMilliseconds) == 108);
	static_assert(offsetof(LaserGpuParticle, alive) == 112);

	struct LaserParticleEmissionSchedule
	{
		uint32_t count = 0;
		float fractionalParticles = 0.0f;
		float emissionRatePerSecond = 0.0f;
	};

	LaserParticleEmissionSchedule ScheduleLaserParticleEmission(float wallDeltaSeconds,
		float motionSpeedDipPerSecond, float fractionalParticles, uint32_t remainingFrameBudget,
		const LaserParticleConfig& configuration) noexcept;

	struct LaserParticleEmissionDirection
	{
		float x = 0.0f;
		float y = 0.0f;
		bool valid = false;
	};

	LaserParticleEmissionDirection ResolveLaserParticleEmissionDirection(float tangentX,
		float tangentY, float sideSample, float deflectionSample, const LaserParticleConfig& configuration) noexcept;
	void AppendLaserTrailParticleSeeds(std::vector<LaserParticleSeed>& seeds, float x, float y,
		float tangentX, float tangentY, float entityRadius,
		float dpiScale, const float (&color)[4], uint32_t count,
		uint32_t seedBase, uint32_t group, const LaserParticleConfig& configuration);
	void AppendLaserClickParticleSeeds(std::vector<LaserParticleSeed>& seeds, float x, float y,
		float entityRadius, const float (&color)[4], uint32_t seedBase, uint32_t group,
		const LaserParticleConfig& configuration);

	void AppendLaserDissolveParticleSeed(std::vector<LaserParticleSeed>& seeds,
		const LaserDissolveFront& front, float x, float y, float tangentX, float tangentY,
		float radius, float coreRadius, float dpiScale, const float (&color)[4],
		uint32_t random, uint32_t group, float windAngleDegrees, bool point = false);
	// 从清空前的画布像素（预乘 BGRA，行优先）生成飘散种子，返回追加的数量。
	uint32_t AppendCanvasClearParticleSeeds(std::vector<LaserParticleSeed>& seeds,
		std::span<const uint32_t> premultipliedBgra, uint32_t width, uint32_t height,
		float dpiScale, float windAngleDegrees, uint32_t seedBase);

	struct LaserParticleBatchEnvelope
	{
		RECT bounds = {};
		uint32_t maximumLifetimeMilliseconds = 0;
	};
	LaserParticleBatchEnvelope ConservativeLaserParticleBatchBounds(
		std::span<const LaserParticleSeed> seeds) noexcept;
	int64_t LaserParticleLifetimeDeadlineQpc(int64_t nowQpc, int64_t qpcFrequency,
		uint32_t lifetimeMilliseconds) noexcept;

	struct LaserParticleActiveRange { uint32_t start = 0, count = 0; };

	// 每批至少占一槽；覆盖写入时同步裁掉最旧批次。
	class LaserParticleActiveRing
	{
	public:
		bool Initialize() noexcept;
		void Clear() noexcept;
		uint32_t Cursor() const noexcept { return cursor_; }
		LaserParticleActiveRange Range() const noexcept;
		void Append(uint32_t count, uint64_t expiresMilliseconds, bool dissolve = false) noexcept;
		void ExtendLatestDissolve(uint64_t expiresMilliseconds) noexcept;
		void EndLatestDissolve(uint64_t nowMilliseconds) noexcept;
		void Expire(uint64_t nowMilliseconds) noexcept;

	private:
		struct Batch { uint32_t count; uint64_t expiresMilliseconds; };
		void RemoveFront(uint32_t count) noexcept;
		std::unique_ptr<Batch[]> batches_;
		uint32_t first_ = 0, batchCount_ = 0, cursor_ = 0, activeCount_ = 0;
		uint32_t latestDissolve_ = kLaserParticleCapacity;
	};

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
		void Add(RECT bounds, int64_t expiresQpc, bool dissolve = false) noexcept;
		void ExtendLatestDissolve(int64_t expiresQpc) noexcept;
		void EndLatestDissolve() noexcept;
		LaserParticleDirtySnapshot Snapshot(int64_t nowQpc, int64_t latestDissolveQpc = 0) noexcept;
		void Clear() noexcept;

	private:
		struct Region
		{
			RECT bounds = {};
			int64_t expiresQpc = 0;
			bool active = false;
			bool shared = false;
		};

		std::array<Region, kLaserParticleDirtyRegionCapacity> regions_ = {};
		Region* latestDissolve_ = nullptr;
	};

	// 管理固定 D3D11 粒子池和仅使用 u0 的 Compute Shader 调度。
	class LaserParticleSystem
	{
	public:
		bool Initialize(ID3D11Device* device, ID3D11DeviceContext* context,
			LaserParticleShaderBytecode updateShader,
			LaserParticleShaderBytecode emitShader) noexcept;
		void Release() noexcept;
		void Configure(const LaserParticleConfig& configuration) noexcept;
		bool IsAvailable() const noexcept;
		void Step(int64_t nowQpc, int64_t qpcFrequency,
			std::span<const LaserParticleSeed> seeds, uint32_t dissolveSeedCount = 0,
			LaserDissolveParticleControl dissolve = {}) noexcept;
		void RemoveGroup(uint32_t group, bool endDissolve) noexcept;
		void Reset() noexcept;
		LaserParticleActiveRange ActiveRange() const noexcept { return ring_.Range(); }
		ID3D11ShaderResourceView* ParticleShaderResourceView() const noexcept;

	private:
		bool DispatchUpdate(uint32_t start, uint32_t count, uint32_t milliseconds,
			uint32_t mode, uint32_t group, uint32_t gatherMilliseconds, float gatherProgress) noexcept;
		void BindComputeParticles() noexcept;
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
		Microsoft::WRL::ComPtr<ID3D11Buffer> seedBuffer_;
		Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> seedSRV_;
		LaserParticleConfig configuration_ = {};
		LaserParticleActiveRing ring_;
		int64_t clockOriginQpc_ = 0;
		uint64_t lastMilliseconds_ = 0;
		uint32_t dissolveBirthMilliseconds_ = 0;
		bool available_ = false;
	};
}
