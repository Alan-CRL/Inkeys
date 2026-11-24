module;

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <array>
#include <cstddef>
#include <cstdint>
#include <d3d11.h>
#include <memory>
#include <optional>
#include <span>
#include <vector>
#include <windows.h>
#include <wrl/client.h>

export module Inkeys.Drawing.Draw3.laser_particles;

export namespace Inkeys::Drawing::Draw3
{
	enum class LaserParticleLimit : uint32_t { Low, Medium, High, Extreme };
	struct LaserParticleLimitTier
	{
		uint32_t poolCapacity;
		uint32_t maximumSeeds;
	};

	inline constexpr std::array<LaserParticleLimitTier, 4> kLaserParticleLimitTiers = {{
		{ 32768, 16384 }, { 131072, 65536 }, { 524288, 262144 }, { 1048576, 524288 }
	}};
	inline constexpr uint32_t kLaserParticleMaximumCapacity = 1048576;
	inline constexpr uint32_t kLaserParticleMaximumBatches = 8192;
	inline constexpr uint32_t kLaserParticleStagingChunk = 16384;
	inline constexpr uint32_t kLaserTrailPoolCapacity = 8192;
	inline constexpr uint32_t kLaserParticleDirtyRegionCapacity = 512;

	// 点击光波不是粒子：它是独立图元，见下方 LaserClickWave。
	enum class LaserParticleKind : uint32_t { Trail = 0, Dissolve = 2 };
	// 飘散画在激光主体之下；行进粒子画在主体之上，压住光晕但避开实体。
	enum class LaserParticlePass : uint32_t { UnderBody, OverBody };
	static_assert(static_cast<uint32_t>(LaserParticleKind::Dissolve) == 2u); // 与粒子着色器的种类常量同步。

	struct LaserDissolveParameters
	{
		uint32_t durationMilliseconds = 900;
		uint32_t gatherMilliseconds = 350;
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
		float seedsPerSquareDip = 0.5f; // 每平方 DIP 墨迹的粒子数；超出上限时整页同比变稀
		uint32_t minimumAlpha = 48; // 低于它的像素视为没有墨迹（抗锯齿边缘）
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
	// 碎裂遮罩把所有像素都碎完时的进度（之后遮罩恒为零）。
	float LaserDissolveCrumbleEndProgress() noexcept;
	float LaserDissolveCoverage(const LaserDissolveFront& front, float x, float y) noexcept;
	uint32_t LaserDissolveSeedCount(double lengthDip, uint32_t maximumSeeds) noexcept;

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
		static constexpr uint32_t LimitShift = 6;
		static constexpr uint32_t LimitMask = 3u << LimitShift;
		static constexpr uint32_t Effects = Trail | ClickWave | Dissolve | ClearDissolve;
		// 与配置默认值一致：总开关关闭，子项与点击光波开启，数量上限为中档。
		static constexpr uint32_t Defaults = Effects | (static_cast<uint32_t>(LaserParticleLimit::Medium) << LimitShift);

		static constexpr uint32_t PackLimit(uint32_t limit) noexcept
		{
			return (limit <= static_cast<uint32_t>(LaserParticleLimit::Extreme)
				? limit : static_cast<uint32_t>(LaserParticleLimit::Medium)) << LimitShift;
		}

		static constexpr uint32_t Pack(bool enabled, bool trail, bool clickWave, bool dissolve) noexcept
		{
			return (enabled ? Enabled : 0u) | (trail ? Trail : 0u) |
				(clickWave ? ClickWave : 0u) | (dissolve ? Dissolve : 0u);
		}
	};

	constexpr LaserParticleLimit LaserParticleLimitFromOptions(uint32_t options) noexcept
	{
		return static_cast<LaserParticleLimit>((options & LaserParticleOptions::LimitMask) >> LaserParticleOptions::LimitShift);
	}

	constexpr uint32_t EffectiveLaserParticleEffects(uint32_t requested, bool available) noexcept
	{
		// 点击光波是独立图元，不依赖粒子池，只看它自己的开关；其余三项要总开关开启且粒子池可用。
		const uint32_t particles = LaserParticleOptions::Trail | LaserParticleOptions::Dissolve |
			LaserParticleOptions::ClearDissolve;
		return (requested & LaserParticleOptions::ClickWave) |
			((available && (requested & LaserParticleOptions::Enabled)) ? requested & particles : 0u);
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

	// 点击光波：一个随时间扩散的圆环。状态只在 CPU，每帧按曲线求出半径、线宽、透明度后
	// 作为普通墨迹图元绘制（InkData 两个槽），不占粒子池，也不需要粒子着色器。
	struct LaserClickWave
	{
		float x = 0.0f;
		float y = 0.0f;
		float entityRadius = 0.0f;
		float color[4] = {};
		int64_t startQpc = 0;
		uint32_t group = 0;
	};

	// 与 InkData 的两个连续槽逐字段对应：{圆心, 环半径, 半线宽} {rgb, 透明度}。
	struct LaserClickWaveVisual
	{
		float x = 0.0f;
		float y = 0.0f;
		float radius = 0.0f;
		float halfWidth = 0.0f;
		float color[3] = {};
		float opacity = 0.0f;
	};
	static_assert(sizeof(LaserClickWaveVisual) == 32);

	// 求光波在 nowQpc 的形态；已结束返回 false。
	bool EvaluateLaserClickWave(const LaserClickWave& wave, int64_t nowQpc, int64_t qpcFrequency,
		const LaserParticleConfig& configuration, LaserClickWaveVisual& visual) noexcept;
	// 光波整个寿命内可能覆盖的范围（含抗锯齿边距）。
	RECT LaserClickWaveBounds(const LaserClickWave& wave, const LaserParticleConfig& configuration) noexcept;

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

	// 写入粒子池的紧凑记录（与 laserParticleCommon.hlsli 同步）。出生时由 CPU 写一次，
	// 之后不再改写：位置、大小、透明度都由顶点着色器按这些参数和时钟求值。
	struct LaserGpuParticle
	{
		float position[2];
		float motion[2];           // 行进：初速度 × 寿命秒数；飘散：风速终值向量
		float motionParameters[4]; // 行进：呼吸角频率、相位、幅度、渐入时长倒数；飘散：加速快慢、蜿蜒幅度、散开位移 xy
		float shapeParameters[4];  // 半径、亮度；行进 z：位移与收缩行程之比；飘散 zw：出生位置的风场值
		uint32_t color;            // RGBA8 直通色
		uint32_t lifetimeMilliseconds; // 0 为空槽
		uint32_t releaseMilliseconds;  // 粒子池时钟下的出生时刻（含释放延时）
		uint32_t kindAndBatch;         // 低 2 位种类，高位为激光飘散批次号
	};

	// CPU 侧的飘散记录；上传时才加出生时刻和批号，group 留在 CPU。
	struct LaserDriftParticle
	{
		LaserGpuParticle particle;
		uint32_t group;
	};

	static_assert(sizeof(LaserDriftParticle) == 68);
	static_assert(offsetof(LaserDriftParticle, group) == sizeof(LaserGpuParticle));
	static_assert(sizeof(LaserParticleSeed) == 88);
	static_assert(sizeof(LaserGpuParticle) == 64);
	static_assert(offsetof(LaserGpuParticle, position) == 0);
	static_assert(offsetof(LaserGpuParticle, motion) == 8);
	static_assert(offsetof(LaserGpuParticle, motionParameters) == 16);
	static_assert(offsetof(LaserGpuParticle, shapeParameters) == 32);
	static_assert(offsetof(LaserGpuParticle, color) == 48);
	static_assert(offsetof(LaserGpuParticle, lifetimeMilliseconds) == 52);
	static_assert(offsetof(LaserGpuParticle, releaseMilliseconds) == 56);
	static_assert(offsetof(LaserGpuParticle, kindAndBatch) == 60);

	// 槽位和批次下标各用自己的掩码；飘散批上限保证同档可容纳两批。
	static_assert(kLaserParticleMaximumBatches != 0 &&
		(kLaserParticleMaximumBatches & (kLaserParticleMaximumBatches - 1u)) == 0);
	static_assert(kLaserParticleLimitTiers.back().poolCapacity == kLaserParticleMaximumCapacity);
	static_assert([]
	{
		uint32_t previousCapacity = 0;
		for (const auto& tier : kLaserParticleLimitTiers)
		{
			const uint32_t capacity = tier.poolCapacity;
			if (capacity <= previousCapacity || (capacity & (capacity - 1u)) != 0 ||
				static_cast<uint64_t>(tier.maximumSeeds) * 2u != capacity ||
				static_cast<uint64_t>(capacity) * sizeof(LaserGpuParticle) > 64u * 1024u * 1024u) return false;
			previousCapacity = capacity;
		}
		return true;
	}());

	// 每次绘制随常量缓冲区下发的时钟与汇聚状态。
	struct LaserParticleClock
	{
		uint32_t nowMilliseconds = 0;
		uint32_t gatherMilliseconds = 0;
		uint32_t gatherBatch = 0;     // 0 表示没有批次在汇聚
		float gatherRemaining = 1.0f; // 1 - 汇聚进度
	};

	struct LaserParticleState
	{
		float x = 0.0f;
		float y = 0.0f;
		float radius = 0.0f;
		float opacity = 0.0f; // 不含颜色自身的透明度
		float white = 0.0f;   // 飘散粒子向白色混合的比例
		bool visible = false;
	};

	LaserGpuParticle PackLaserTrailParticle(const LaserParticleSeed& seed, uint32_t bornMilliseconds) noexcept;
	// 旧种子路径的打包公式，仅供自检对照飘散参数。
	LaserGpuParticle PackLaserParticle(const LaserParticleSeed& seed, uint32_t bornMilliseconds,
		uint32_t dissolveBatch) noexcept;
	// 顶点着色器求值的 CPU 镜像，只供自检核对两边的运动模型。
	LaserParticleState EvaluateLaserParticle(const LaserGpuParticle& particle, const LaserParticleClock& clock,
		float dipPerPixel, const LaserParticleConfig& configuration) noexcept;

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

	struct LaserParticleBatchEnvelope
	{
		RECT bounds = {};
		uint32_t maximumLifetimeMilliseconds = 0;
	};

	struct LaserDriftParticleContext
	{
		float windX, windY;
		float dpiScale, dipPerPixel;
		float windSpeedPxPerSecond, meanderPx;

		// 同一个激光批可含不同 DPI 的笔迹；只在笔迹尺度变化时刷新这些量。
		void SetDpiScale(float scale) noexcept;
	};
	LaserDriftParticleContext MakeDriftParticleContext(float dpiScale, float windAngleDegrees) noexcept;
	LaserDriftParticle MakeDriftParticle(const LaserDriftParticleContext& context, LaserParticleBatchEnvelope& envelope,
		float x, float y, float white, const float (&color)[4], uint32_t random, uint32_t group,
		uint32_t releaseDelayMilliseconds) noexcept;
	void AppendLaserDissolveParticle(std::vector<LaserDriftParticle>& particles, LaserParticleBatchEnvelope& envelope,
		const LaserDriftParticleContext& context, const LaserDissolveFront& front,
		float x, float y, float tangentX, float tangentY, float radius, float coreRadius, const float (&color)[4],
		uint32_t random, uint32_t group, bool isDot = false);
	// 在映射的预乘 BGRA 像素上按整数 DPI 步长抽样；释放与出生像素的碎裂同步。
	uint32_t AppendCanvasClearParticleSeeds(std::vector<LaserDriftParticle>& particles, LaserParticleBatchEnvelope& envelope,
		const uint32_t* premultipliedBgra, size_t rowPitchBytes, uint32_t width, uint32_t height,
		const LaserDriftParticleContext& context, uint32_t seedBase,
		const LaserDissolveFront& front, uint32_t maximumSeeds, RECT* inkBounds = nullptr);

	// 以下旧种子入口仅供两个粒子自检对照。
	void AppendReferenceDriftParticleSeed(std::vector<LaserParticleSeed>& seeds, float x, float y,
		float white, float dpiScale, const float (&color)[4], uint32_t random, uint32_t group,
		float windAngleDegrees, uint32_t releaseDelayMilliseconds);
	void AppendReferenceLaserDissolveParticleSeed(std::vector<LaserParticleSeed>& seeds,
		const LaserDissolveFront& front, float x, float y, float tangentX, float tangentY,
		float radius, float coreRadius, float dpiScale, const float (&color)[4],
		uint32_t random, uint32_t group, float windAngleDegrees, bool isDot);
	uint32_t AppendReferenceCanvasClearParticleSeeds(std::vector<LaserParticleSeed>& seeds,
		const uint32_t* premultipliedBgra, size_t rowPitchBytes, uint32_t width, uint32_t height,
		float dpiScale, float windAngleDegrees, uint32_t seedBase,
		const LaserDissolveFront& front, uint32_t maximumSeeds, RECT* inkBounds = nullptr);
	bool MatchesReferenceDriftBatch(std::span<const LaserDriftParticle> particles,
		std::span<const LaserParticleSeed> seeds, const LaserParticleBatchEnvelope& envelope) noexcept;

	LaserParticleBatchEnvelope ConservativeLaserParticleBatchBounds(
		std::span<const LaserParticleSeed> seeds) noexcept;
	LaserParticleBatchEnvelope ConservativeCanvasClearParticleBounds(RECT pixelRect, float dpiScale) noexcept;
	int64_t LaserParticleLifetimeDeadlineQpc(int64_t nowQpc, int64_t qpcFrequency,
		uint32_t lifetimeMilliseconds) noexcept;

	struct LaserParticleActiveRange { uint32_t start = 0, count = 0; };
	struct LaserParticleBatchId { uint32_t index = kLaserParticleMaximumBatches, generation = 0; };

	// 每批至少占一槽；覆盖写入时同步裁掉最旧批次。
	class LaserParticleActiveRing
	{
	public:
		bool Initialize(uint32_t capacity) noexcept;
		void Clear() noexcept;
		uint32_t Cursor() const noexcept { return cursor_; }
		LaserParticleActiveRange Range() const noexcept;
		LaserParticleBatchId Append(uint32_t count, uint64_t expiresMilliseconds, bool dissolve = false) noexcept;
		// 只在批次身份仍对应环尾且确实缩短时返回 true。
		bool TrimTail(LaserParticleBatchId id, uint32_t count) noexcept;
		uint64_t EndMilliseconds() const noexcept;
		void ExtendLatestDissolve(uint64_t expiresMilliseconds) noexcept;
		void EndLatestDissolve(uint64_t nowMilliseconds) noexcept;
		void Expire(uint64_t nowMilliseconds) noexcept;
		// 最近一批激光飘散仍在环内的槽区间；已退出环时为空。
		LaserParticleActiveRange LatestDissolveRange() const noexcept;

	private:
		struct Batch { uint32_t count, generation; uint64_t expiresMilliseconds; };
		void RemoveFront(uint32_t count) noexcept;
		std::unique_ptr<Batch[]> batches_;
		uint32_t first_ = 0, batchCount_ = 0, cursor_ = 0, activeCount_ = 0;
		uint32_t capacity_ = 0, capacityMask_ = 0;
		uint32_t latestDissolve_ = kLaserParticleMaximumBatches;
		uint32_t generation_ = 0;
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
		enum class Kind { Other, ClearDrift, LaserDissolve };
		void Add(RECT bounds, int64_t expiresQpc, Kind kind = Kind::Other) noexcept;
		void TrackClearSpawn(RECT bounds, int64_t expiresQpc) noexcept;
		RECT RevokeClearSpawn() noexcept;
		void ExtendLatestDissolve(int64_t expiresQpc) noexcept;
		void EndLatestDissolve() noexcept;
		LaserParticleDirtySnapshot Snapshot(int64_t nowQpc, int64_t latestDissolveQpc = 0) noexcept;
		RECT ClearDrift() noexcept;
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
		// 飘散单独存放，容量耗尽时也不会与行进粒子或点击光波合并。
		std::array<Region, kLaserParticleDirtyRegionCapacity> driftRegions_ = {};
		Region latestClearSpawn_;
		Region* latestDissolve_ = nullptr;
	};

#if INKEYS_ENABLE_UI3_PRESENTATION_HARNESS
	struct LaserParticleSystemTestAccess;
#endif

	struct LaserClearSpawnRequest
	{
		RECT pixelRect = {};
		LaserDissolveFront front = MakeLaserDissolveFront();
		float dpiScale = 1.0f;
		float windAngleDegrees = kLaserDissolveParameters.leftUpWindAngleDegrees;
		uint32_t seedBase = 0;
		uint32_t reservedCount = 0;
		ID3D11ShaderResourceView* snapshot = nullptr; // 预乘 BGRA 的 Texture2D，渲染器填入
	};

	struct LaserParticleStepResult
	{
		bool trailAccepted = false;
		bool clearAccepted = false; // CPU 读回路径
		bool dissolveAccepted = false;
		bool clearSpawnAccepted = false;
	};

	struct LaserClearSpawnReadback
	{
		uint32_t written = 0;
		bool batchRevoked = false;
	};

	// 管理独立的行进池与飘散池。出生记录由 CPU 上传或由清空计算着色器写入，顶点着色器逐帧求值。
	// 每池按需创建，并共用着色器、时钟和上传暂存区。
	class LaserParticleSystem
	{
	public:
		bool Initialize(ID3D11Device* device, ID3D11DeviceContext* context,
			LaserParticleShaderBytecode vertexShader, LaserParticleShaderBytecode pixelShader,
			LaserParticleShaderBytecode spawnShader) noexcept;
		void Release() noexcept;
		void Configure(const LaserParticleConfig& configuration) noexcept;
		bool IsAvailable() const noexcept;
		void SetLimit(LaserParticleLimit limit) noexcept;
		LaserParticleLimit EffectiveLimit() const noexcept { return limit_; }
		uint32_t MaximumSeeds() const noexcept;
		// 在生成记录前确定实际单次上限，建池失败时返回 0。
		uint32_t PrepareDriftPool() noexcept;
		// GPU 资源是否已经创建（自检用）。
		bool ResourcesCreated(LaserParticlePass pass) const noexcept { return ParticleBuffer(pass) != nullptr; }
		ID3D11Buffer* ParticleBuffer(LaserParticlePass pass) const noexcept { return Pool(pass).buffer.Get(); }
		LaserParticleStepResult Step(int64_t nowQpc, int64_t qpcFrequency,
			std::span<const LaserParticleSeed> trailSeeds, std::span<const LaserDriftParticle> plainDrift = {},
			std::span<const LaserDriftParticle> dissolveDrift = {}, float dipPerPixel = 1.0f,
			LaserDissolveParticleControl dissolve = {}, const LaserClearSpawnRequest* clearSpawn = nullptr) noexcept;
		bool GpuClearSpawnAvailable() noexcept;
		std::optional<LaserClearSpawnReadback> PollClearSpawn() noexcept;
		// 调用方先设好渲染目标、混合与光栅状态；返回是否提交了绘制（改动了着色器与输入装配状态）。
		bool Draw(LaserParticlePass pass, float viewportWidth, float viewportHeight) noexcept;
		// 提前创建行进池与共享着色器；只有这一次调用真正创建了行进池时返回 true。
		bool Prepare() noexcept;
		// 用一粒空槽提交一次绘制，让延迟编译的驱动现在就编好粒子着色器；调用方负责把视口设为零。
		void WarmUp() noexcept;
		// 分别归还空闲池；两池都空时再归还共享资源，设备会话仍保留。
		void ReleaseIdleResources() noexcept;
		void ReleaseIdleDriftPool() noexcept;
		void RemoveGroup(uint32_t group, bool endDissolve) noexcept;
		void Reset() noexcept;
		LaserParticleActiveRange ActiveRange(LaserParticlePass pass) const noexcept
		{
			return Pool(pass).ring.Range();
		}
		LaserParticleClock Clock() const noexcept { return clock_; }
		float DipPerPixel() const noexcept { return dipPerPixel_; }

	private:
		struct ParticlePool
		{
			Microsoft::WRL::ComPtr<ID3D11Buffer> buffer;
			Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> srv;
			LaserParticleActiveRing ring;
			std::unique_ptr<uint32_t[]> slotGroups; // 撤回时按分组找槽
			uint32_t capacity = 0, capacityMask = 0;
			uint64_t endMilliseconds = 0;
		};

		const ParticlePool& Pool(LaserParticlePass pass) const noexcept
		{
			return pass == LaserParticlePass::OverBody ? trailPool_ : driftPool_;
		}
		bool EnsureResources(LaserParticlePass pass) noexcept;
		bool EnsureDrawResources() noexcept;
		HRESULT CreatePool(ParticlePool& pool, uint32_t capacity, bool unorderedAccess) noexcept;
		bool EnsureClearSpawnResources() noexcept;
		bool AppendClearSpawn(const LaserClearSpawnRequest& request, uint64_t milliseconds) noexcept;
		void ReleaseClearSpawnResources() noexcept;
		bool AppendTrailSeeds(std::span<const LaserParticleSeed> seeds, uint64_t milliseconds) noexcept;
		bool AppendDriftParticles(std::span<const LaserDriftParticle> plainDrift,
			std::span<const LaserDriftParticle> dissolveDrift, float dipPerPixel, uint64_t milliseconds,
			LaserParticleStepResult& result) noexcept;
		bool Submit(const ParticlePool& pool, uint32_t start, uint32_t count, LaserParticleKind kind,
			float viewportWidth, float viewportHeight) noexcept;
		bool Upload(const ParticlePool& pool, uint32_t start, uint32_t count) noexcept;
		bool KillSlots(ParticlePool& pool, LaserParticleActiveRange range) noexcept;
		bool RemovePoolGroup(ParticlePool& pool, uint32_t group) noexcept;

		Microsoft::WRL::ComPtr<ID3D11Device> device_;
		Microsoft::WRL::ComPtr<ID3D11DeviceContext> context_;
		Microsoft::WRL::ComPtr<ID3D11VertexShader> vertexShader_;
		Microsoft::WRL::ComPtr<ID3D11PixelShader> pixelShader_;
		ParticlePool trailPool_, driftPool_;
		Microsoft::WRL::ComPtr<ID3D11Buffer> drawConstants_;
		LaserParticleShaderBytecode vertexBytecode_ = {};
		LaserParticleShaderBytecode pixelBytecode_ = {};
		LaserParticleShaderBytecode spawnBytecode_ = {};
		Microsoft::WRL::ComPtr<ID3D11ComputeShader> spawnShader_;
		Microsoft::WRL::ComPtr<ID3D11UnorderedAccessView> spawnParticleUav_, spawnCountersUav_;
		Microsoft::WRL::ComPtr<ID3D11Buffer> spawnCounters_, spawnReadback_, spawnConstants_;
		struct PendingClearSpawn
		{
			uint64_t deviceGeneration;
			LaserParticleBatchId batch;
			uint32_t reservedCount;
			double submittedMilliseconds;
		};
		std::optional<PendingClearSpawn> pendingClearSpawn_;
		uint64_t deviceGeneration_ = 0;
		bool spawnUnavailable_ = false;
		// 两池打包与清零共用的上传暂存区。
		std::unique_ptr<LaserGpuParticle[]> staging_;
		LaserParticleConfig configuration_ = {};
		LaserParticleLimit limit_ = LaserParticleLimit::Low;
		LaserParticleLimit limitCeiling_ = LaserParticleLimit::Extreme;
		LaserParticleClock clock_ = {};
		int64_t clockOriginQpc_ = 0;
		uint64_t lastMilliseconds_ = 0;
		uint32_t dissolveBatch_ = 0;       // 最近一批激光飘散的批次号
		uint32_t gatheringBatch_ = 0;      // 已进入过汇聚的批次，汇聚被撤销时整批作废
		float dipPerPixel_ = 1.0f;
		bool available_ = false;
		bool trailUnavailable_ = false;
		bool driftUnavailable_ = false;
#if INKEYS_ENABLE_UI3_PRESENTATION_HARNESS
		friend struct LaserParticleSystemTestAccess;
		int maximumDriftTierForTest_ = static_cast<int>(LaserParticleLimit::Extreme);
		bool failTrailCreationForTest_ = false;
		bool failClearSpawnCreationForTest_ = false;
#endif
	};

#if INKEYS_ENABLE_UI3_PRESENTATION_HARNESS
	// 渲染器自检位于另一个模块，通过本模块的 friend 访问注入点和共享资源。
	struct LaserParticleSystemTestAccess
	{
		static void SetMaximumDriftTier(LaserParticleSystem& system, int tier) noexcept
		{
			system.maximumDriftTierForTest_ = tier;
		}

		static void SetTrailCreationFailure(LaserParticleSystem& system, bool fail) noexcept
		{
			system.failTrailCreationForTest_ = fail;
		}

		static void SetClearSpawnCreationFailure(LaserParticleSystem& system, bool fail) noexcept
		{
			system.failClearSpawnCreationForTest_ = fail;
		}

		static ID3D11ShaderResourceView* DriftSrv(const LaserParticleSystem& system) noexcept
		{
			return system.driftPool_.srv.Get();
		}

		static uint64_t DriftEndMilliseconds(const LaserParticleSystem& system) noexcept
		{
			return system.driftPool_.endMilliseconds;
		}

		static bool DrawResourcesMatch(const LaserParticleSystem& system, bool created) noexcept
		{
			return (system.vertexShader_ != nullptr) == created && (system.pixelShader_ != nullptr) == created &&
				(system.drawConstants_ != nullptr) == created && static_cast<bool>(system.staging_) == created;
		}
	};
#endif
}
