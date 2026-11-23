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

	// 点击光波不是粒子：它是独立图元，见下方 LaserClickWave。
	enum class LaserParticleKind : uint32_t { Trail = 0, Dissolve = 2 };
	// 飘散画在激光主体之下；行进粒子画在主体之上，压住光晕但避开实体。
	enum class LaserParticlePass : uint32_t { UnderBody, OverBody };
	static_assert(static_cast<uint32_t>(LaserParticleKind::Dissolve) == 2u); // 与粒子着色器的种类常量同步。

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

	static_assert(sizeof(LaserParticleSeed) == 88);
	static_assert(sizeof(LaserGpuParticle) == 64);
	static_assert(offsetof(LaserGpuParticle, motionParameters) == 16);
	static_assert(offsetof(LaserGpuParticle, color) == 48);
	static_assert(offsetof(LaserGpuParticle, kindAndBatch) == 60);

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

	// 把来源层的种子压成粒子池记录；bornMilliseconds 为粒子池时钟，dissolveBatch 只对激光飘散批次非零。
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

	void AppendLaserDissolveParticleSeed(std::vector<LaserParticleSeed>& seeds,
		const LaserDissolveFront& front, float x, float y, float tangentX, float tangentY,
		float radius, float coreRadius, float dpiScale, const float (&color)[4],
		uint32_t random, uint32_t group, float windAngleDegrees, bool point = false);
	// 从清空前的画布像素（预乘 BGRA，行优先，每行 rowPitchBytes 字节）生成飘散种子，返回追加的数量。
	// 直接读映射出来的读回内存，不另存一份；高 DPI 下按整数步长抽样，扫描量随之成平方下降。
	// 每粒的释放时刻取它出生像素在碎裂遮罩 front 下消失的时刻，与画布快照的碎裂对齐；
	// inkBounds 返回取样到的墨迹范围（像素）。
	uint32_t AppendCanvasClearParticleSeeds(std::vector<LaserParticleSeed>& seeds,
		const uint32_t* premultipliedBgra, size_t rowPitchBytes, uint32_t width, uint32_t height,
		float dpiScale, float windAngleDegrees, uint32_t seedBase,
		const LaserDissolveFront& front, RECT* inkBounds = nullptr);

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
		// 最近一批激光飘散仍在环内的槽区间；已退出环时为空。
		LaserParticleActiveRange LatestDissolveRange() const noexcept;

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

	// 管理固定容量的 D3D11 粒子池。没有计算着色器：CPU 在出生时把记录写进环形区间，
	// 每帧只有一到两次实例化绘制，状态由顶点着色器求值。池与着色器在第一次有粒子时才创建，
	// 画布粒子效果关闭时不占显存，也不碰任何粒子相关的驱动路径。
	class LaserParticleSystem
	{
	public:
		bool Initialize(ID3D11Device* device, ID3D11DeviceContext* context,
			LaserParticleShaderBytecode vertexShader,
			LaserParticleShaderBytecode pixelShader) noexcept;
		void Release() noexcept;
		void Configure(const LaserParticleConfig& configuration) noexcept;
		bool IsAvailable() const noexcept;
		// GPU 资源是否已经创建（自检用）。
		bool ResourcesCreated() const noexcept { return particleBuffer_ != nullptr; }
		ID3D11Buffer* ParticleBuffer() const noexcept { return particleBuffer_.Get(); }
		void Step(int64_t nowQpc, int64_t qpcFrequency,
			std::span<const LaserParticleSeed> seeds, uint32_t dissolveSeedCount = 0,
			LaserDissolveParticleControl dissolve = {}) noexcept;
		// 调用方先设好渲染目标、混合与光栅状态；返回是否提交了绘制（改动了着色器与输入装配状态）。
		bool Draw(LaserParticlePass pass, float viewportWidth, float viewportHeight) noexcept;
		// 提前创建粒子池与着色器；只有这一次调用真正创建了资源时返回 true。
		bool Prepare() noexcept;
		// 用一粒空槽提交一次绘制，让延迟编译的驱动现在就编好粒子着色器；调用方负责把视口设为零。
		void WarmUp() noexcept;
		// 没有存活粒子时归还粒子池、着色器与暂存区；系统仍可用，下次有粒子时重新创建。
		void ReleaseIdleResources() noexcept;
		void RemoveGroup(uint32_t group, bool endDissolve) noexcept;
		void Reset() noexcept;
		LaserParticleActiveRange ActiveRange() const noexcept { return ring_.Range(); }
		LaserParticleClock Clock() const noexcept { return clock_; }
		float DipPerPixel() const noexcept { return dipPerPixel_; }

	private:
		bool EnsureResources() noexcept;
		bool Submit(uint32_t start, uint32_t count, LaserParticleKind kind,
			float viewportWidth, float viewportHeight) noexcept;
		bool Upload(uint32_t start, uint32_t count) noexcept;
		bool KillSlots(LaserParticleActiveRange range) noexcept;

		Microsoft::WRL::ComPtr<ID3D11Device> device_;
		Microsoft::WRL::ComPtr<ID3D11DeviceContext> context_;
		Microsoft::WRL::ComPtr<ID3D11VertexShader> vertexShader_;
		Microsoft::WRL::ComPtr<ID3D11PixelShader> pixelShader_;
		Microsoft::WRL::ComPtr<ID3D11Buffer> particleBuffer_;
		Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> particleSRV_;
		Microsoft::WRL::ComPtr<ID3D11Buffer> drawConstants_;
		LaserParticleShaderBytecode vertexBytecode_ = {};
		LaserParticleShaderBytecode pixelBytecode_ = {};
		// 打包与清零共用的上传暂存区，以及每个槽所属的分组（撤回某一笔时按它找槽）。
		std::unique_ptr<LaserGpuParticle[]> staging_;
		std::unique_ptr<uint32_t[]> slotGroups_;
		LaserParticleConfig configuration_ = {};
		LaserParticleActiveRing ring_;
		LaserParticleClock clock_ = {};
		int64_t clockOriginQpc_ = 0;
		uint64_t lastMilliseconds_ = 0;
		// 两类粒子各自最晚的结束时刻：某一类已全部结束时跳过它那一次绘制。
		uint64_t trailEndMilliseconds_ = 0;
		uint64_t driftEndMilliseconds_ = 0;
		uint32_t dissolveBatch_ = 0;       // 最近一批激光飘散的批次号
		uint32_t gatheringBatch_ = 0;      // 已进入过汇聚的批次，汇聚被撤销时整批作废
		float dipPerPixel_ = 1.0f;
		bool available_ = false;
	};
}
