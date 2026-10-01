#pragma once

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <span>
#include <type_traits>

namespace Inkeys::UI::Bar
{
	enum class Ui3FiniteScene : std::uint32_t { MainFold = 1, DrawAttribute = 2 };
	enum class Ui3FiniteStatus : std::uint32_t
	{
		Pending, Accepted, CompletedLayoutAndSvg, AcceptedNoChange,
		RejectedByBusiness, Superseded, AmbiguousPublication,
		ResourceUnverified, SourceRejected, UnsupportedState, Stopped, TimedOut, Overflow,
	};
	enum class Ui3BusinessWriteWitness : std::uint32_t { Unknown, NoBusinessWrite, WriteOccurred };
	inline constexpr std::uint64_t Ui3FiniteRequiredMask = 0xFF;
	inline constexpr std::size_t Ui3FiniteCapacity = 512;
	inline constexpr std::uint32_t Ui3FiniteReusedPending = 1u << 0;
	inline constexpr std::uint32_t Ui3FiniteReusedCompleted = 1u << 1;

	struct Ui3FiniteSignature
	{
		std::uint32_t flags = 0;
		std::uint32_t stateMode = 0, penMode = 0, penColorRgb = 0, penWidthBits = 0;
		std::uint32_t mainSide = 0, primarySide = 0, thicknessView = 0, darkStyle = 0, dpi = 0;
		std::uint64_t toolRevision = 0, displaySerial = 0, configZoomBits = 0, validMask = 0;
	};
	struct Ui3FiniteAccepted
	{
		std::uint64_t runSerial = 0, stepId = 0, revision = 0, publicationSerial = 0, sourceSequence = 0;
		Ui3FiniteScene scene = Ui3FiniteScene::MainFold;
		Ui3FiniteStatus status = Ui3FiniteStatus::Pending;
		std::int64_t ownerReceiveTicks = 0, acceptedTicks = 0;
		Ui3FiniteSignature signature;
	};
	struct Ui3FiniteCandidate
	{
		Ui3FiniteAccepted accepted;
		std::uint64_t epoch = 0, surfaceSerial = 0, frameAttemptSerial = 0;
		std::uint64_t rootBatchRevision = 0, drawBatchRevision = 0;
		std::uint32_t pendingRoles = 0, mismatchRoles = 0;
		std::uint32_t requiredSvg = 0, verifiedSvg = 0, failedSvg = 0, unverifiedSvg = 0;
		std::uint32_t firstUnverifiedSvgTag = 0, firstUnverifiedReason = 0;
		std::uint32_t targetWidth = 0, targetHeight = 0, renderDpi = 0;
		std::int64_t targetConsumedTicks = 0, settledTicks = 0;
		bool stablePublication = false, settled = false, svgProofComplete = false;
	};
	struct Ui3FiniteTargetRecord
	{
		Ui3FiniteAccepted accepted;
		Ui3FiniteStatus terminalStatus = Ui3FiniteStatus::Pending;
		std::uint64_t epoch = 0, surfaceSerial = 0, trueBarAttemptSerial = 0;
		std::int64_t consumedTicks = 0, settledTicks = 0, finalCommitTicks = 0;
		std::uint32_t pendingRoles = 0, proofMask = 0, failureFlags = 0;
		std::uint64_t reusedRevision = 0;
		bool timingValid = false;
		std::uint32_t requiredSvg = 0, verifiedSvg = 0, failedSvg = 0, unverifiedSvg = 0;
		std::uint32_t firstUnverifiedSvgTag = 0, firstUnverifiedReason = 0;
	};
	struct Ui3FiniteCounters
	{
		std::uint64_t seen = 0, retained = 0, dropped = 0;
		std::uint64_t accepted = 0, rejected = 0, ambiguous = 0, noChange = 0, invalid = 0;
	};

	// 仅显式无窗口测试安装的有限停点；默认空，不增加生产等待。
	enum class Ui3FiniteTestPoint : std::uint32_t
	{
		AfterOddBeforeFence, AfterFence, AfterBusinessWrite,
		AfterPayloadHalf, BeforeEven, BeforeRenderRequest,
	};
	struct Ui3FiniteTestHooks
	{
		void (*checkpoint)(Ui3FiniteTestPoint, void*) noexcept = nullptr;
		void* context = nullptr;
	};

	class Ui3FinitePublication
	{
	public:
		struct Mutation
		{
			Ui3FinitePublication* owner = nullptr; // 非 owning，不进入 raw DTO。
			std::uint64_t stepId = 0, sourceSequence = 0, serial = 0;
			std::size_t recordIndex = Ui3FiniteCapacity;
			Ui3FiniteScene scene = Ui3FiniteScene::MainFold;
			Ui3BusinessWriteWitness witness = Ui3BusinessWriteWitness::Unknown;
			Ui3FiniteStatus rejectedStatus = Ui3FiniteStatus::RejectedByBusiness;
			std::int64_t ownerReceiveTicks = 0;
			bool active = false, businessAccepted = false;
		};

		Ui3FinitePublication(std::uint64_t runSerial, const Ui3FiniteSignature& initialStable) noexcept;
		Ui3FinitePublication(const Ui3FinitePublication&) = delete;
		Ui3FinitePublication& operator=(const Ui3FinitePublication&) = delete;
		[[nodiscard]] Mutation BeginMutation(std::uint64_t stepId, Ui3FiniteScene scene,
			std::uint64_t sourceSequence) noexcept;
		void MarkBusinessAccepted(Mutation&) noexcept;
		void ObserveBusinessWrite(Mutation&) noexcept;
		void FinishAtRenderRequest(Mutation&, const Ui3FiniteSignature&, std::int64_t acceptedTicks) noexcept;
		void FinishRejected(Mutation&, Ui3FiniteStatus,
			Ui3BusinessWriteWitness witness = Ui3BusinessWriteWitness::Unknown) noexcept;
		[[nodiscard]] bool TryReadStable(Ui3FiniteAccepted&) const noexcept;
		[[nodiscard]] bool StillSameSemanticGoal(const Ui3FiniteAccepted&, Ui3FiniteAccepted&) const noexcept;
		// 后续 observer 仅在真实 layout/SVG/事务证明完成后提供此 revision；P1 未接 observer。
		[[nodiscard]] bool NoteCompletedGoal(std::uint64_t revision) noexcept;
		[[nodiscard]] std::uint64_t CompletedRevision() const noexcept { return completedRevision_.load(std::memory_order_acquire); }
		void SetTestHooks(Ui3FiniteTestHooks hooks) noexcept { hooks_ = hooks; }
		[[nodiscard]] std::uint64_t RunSerial() const noexcept { return runSerial_; }
		[[nodiscard]] std::uint64_t PublicationSerial() const noexcept { return publicationSerial_.load(std::memory_order_acquire); }
		[[nodiscard]] Ui3FiniteSignature InitialStableSignature() const noexcept { return initialStable_; }
		// 只在Interaction及render均真正join后吸收；render不写plain publication rows。
		void AbsorbAfterOwnersStopped(std::span<const Ui3FiniteTargetRecord>) noexcept;
		[[nodiscard]] Ui3FiniteCounters CountersAfterOwnerStopped() const noexcept { return counters_; }
		[[nodiscard]] std::span<const Ui3FiniteTargetRecord> RecordsAfterOwnerStopped() const noexcept
		{
			return { records_.data(), static_cast<std::size_t>(counters_.retained) };
		}

	private:
		friend class Ui3FiniteObserver; // 仅初始真实提交可冻结；Interaction启动后不再写此基线。
		void Checkpoint(Ui3FiniteTestPoint point) noexcept;
		[[nodiscard]] bool Owns(const Mutation&) const noexcept;
		void PublishAndClose(Mutation&) noexcept;
		void ReconcilePreviousGoal(Ui3FiniteStatus pendingStatus) noexcept;
		// R2：payload各word均为atomic；odd后前置release fence，字段后even release。
		static constexpr std::size_t AcceptedWords = sizeof(Ui3FiniteAccepted) / sizeof(std::uint64_t);
		std::atomic<std::uint64_t> publicationSerial_ = 0;
		std::array<std::atomic<std::uint64_t>, AcceptedWords + 1> payload_{};
		std::array<Ui3FiniteTargetRecord, Ui3FiniteCapacity> records_{};
		Ui3FiniteCounters counters_;
		Ui3FiniteAccepted goal_;
		Ui3FiniteSignature initialStable_;
		std::uint64_t runSerial_ = 0, nextRevision_ = 0;
		// 完成receipt由未来render owner发布，不能与Interaction的no-change读取发生plain数据竞争。
		std::atomic<std::uint64_t> completedRevision_ = 0;
		bool goalKnown_ = false, mutationOpen_ = false, mutationInterfered_ = false;
		Ui3FiniteTestHooks hooks_;
	};

	[[nodiscard]] bool IsUi3FiniteSignatureValid(const Ui3FiniteSignature&) noexcept;
	[[nodiscard]] bool SameUi3FiniteSemanticSignature(const Ui3FiniteSignature&, const Ui3FiniteSignature&) noexcept;

	struct Ui3FiniteOwnerRequest
	{
		std::uint64_t stepId = 0, sourceSequence = 0;
		Ui3FiniteScene scene = Ui3FiniteScene::MainFold;
		std::int64_t ownerReceiveTicks = 0;
	};
	struct Ui3FiniteOwnerContext
	{
		Ui3FinitePublication* publication = nullptr;
		Ui3FiniteOwnerRequest request;
		bool requestAvailable = false, clocksEnabled = false;
		std::int64_t (*readTicks)() noexcept = nullptr;
	};
	// 仅 owner 线程在启动前/结束后绑定；普通产品不安装，未安装时所有接缝立即no-op。
	Ui3FiniteOwnerContext* SetUi3FiniteOwnerContext(Ui3FiniteOwnerContext*) noexcept;
	[[nodiscard]] Ui3FinitePublication::Mutation* CurrentUi3FiniteMutation() noexcept;
	void MarkCurrentUi3FiniteBusinessAccepted() noexcept;
	void RejectCurrentUi3FiniteBusiness(Ui3FiniteStatus) noexcept;
	void FinishCurrentUi3FiniteAtRenderRequest(const Ui3FiniteSignature&) noexcept;

	class Ui3FiniteMutationScope
	{
	public:
		explicit Ui3FiniteMutationScope(Ui3FiniteScene scene, bool enabled = true) noexcept;
		~Ui3FiniteMutationScope();
		Ui3FiniteMutationScope(const Ui3FiniteMutationScope&) = delete;
		Ui3FiniteMutationScope& operator=(const Ui3FiniteMutationScope&) = delete;
		void MarkBusinessAccepted() noexcept;
		void ObserveBusinessWrite() noexcept;
		void FinishRejected(Ui3FiniteStatus status, Ui3BusinessWriteWitness witness) noexcept;
	private:
		Ui3FinitePublication::Mutation mutation_;
		Ui3FinitePublication::Mutation* previous_ = nullptr;
		bool installed_ = false;
	};


	enum class Ui3PropertyRole : std::uint32_t
	{
		Unspecified, MainRootGeometry, MainClickPulse, DrawRootGeometry,
		AttributePreview, DockDisplay, SvgSemantic, Feedback, Lighting, Count,
	};
	[[nodiscard]] constexpr std::uint32_t Ui3FiniteRoleMask(Ui3PropertyRole role) noexcept
	{
		return role == Ui3PropertyRole::Unspecified || role >= Ui3PropertyRole::Count
			? 0u : 1u << (static_cast<unsigned>(role) - 1u);
	}
	// 颜色环是PNG，但它的几何与显隐仍属于属性布局；此映射不认证PNG像素。
	[[nodiscard]] constexpr Ui3PropertyRole Ui3FinitePngLayoutRole(bool colorSelectionWheel, bool relevant) noexcept
	{
		return colorSelectionWheel && relevant ? Ui3PropertyRole::AttributePreview : Ui3PropertyRole::Unspecified;
	}
	inline constexpr std::uint32_t Ui3FiniteAllLayoutRoles = 0x3Fu;
	inline constexpr std::uint32_t Ui3FiniteLifecycleDockPending = 1u << 0;
	inline constexpr std::uint32_t Ui3FiniteLifecycleDisplayPending = 1u << 1;
	inline constexpr std::uint32_t Ui3FiniteLifecycleInitialPending = 1u << 2;
	inline constexpr std::uint32_t Ui3FiniteLifecycleInterference = 1u << 16;
	inline constexpr std::uint32_t Ui3FiniteLifecycleUnsupportedGesture = 1u << 17;
	inline constexpr std::uint32_t Ui3FiniteLifecycleTargetLost = 1u << 18;

	// 仅typed producer输入；B3未安装时必须producerPresent=false，零required不等于证明。
	struct Ui3FiniteResourceProof
	{
		std::uint64_t revision = 0, epoch = 0, surfaceSerial = 0, frameAttemptSerial = 0;
		std::uint32_t required = 0, verified = 0, failed = 0, unverified = 0;
		bool producerPresent = false;
		std::uint32_t firstUnverifiedSvgTag = 0, firstUnverifiedReason = 0;
	};

	inline constexpr std::size_t Ui3SvgCapacity = 256;
	inline constexpr std::uint32_t Ui3SvgMapOrdinalMax = 31;
	enum class Ui3SvgProofReason : std::uint32_t
	{
		None, Unbound, Invalid, UnknownBitmap, Semantic, Epoch, Size, Dpi, Coverage,
		Overwrite, HiddenBoundsUnknown, HiddenNotCleared, Quality, NotDrawn, Opacity,
		ApiFailure = 0x100,
	};
	enum class Ui3SvgOffscreenFault : std::uint32_t { None, RejectRasterResult, InvalidUploadAlpha };
	enum class Ui3SvgFailure : std::uint32_t { None, Parse, Raster, Upload, DrawRejected, ProofUnknown };
	enum class Ui3SvgUse : std::uint32_t
	{
		DrawnVerified, HiddenExpected, RetainedVerified, QualityFallback,
		Missing, SemanticMismatch, EpochMismatch, SizeMismatch, Unverified,
	};
	enum class Ui3SvgCoverage : std::uint32_t { Unknown, FullVisibleCoverage, Partial, Empty };
	enum class Ui3SvgStage : std::uint32_t { Parse, Raster, Upload, Draw };
	enum class Ui3SvgOperation : std::uint32_t { Begin, Success, Failure };
	struct Ui3SvgBitmapProof
	{
		std::uint32_t tag = 0, flags = 0, colorMask = 0, color1Rgb = 0, color2Rgb = 0;
		std::uint32_t pixelWidth = 0, pixelHeight = 0, dpi = 0;
		std::uint64_t valueRevision = 0, epoch = 0, surfaceSerial = 0;
		std::uint64_t requestedWBits = 0, requestedHBits = 0;
		bool ready = false, semanticKnown = false;
	};
	struct Ui3SvgDrawObservation
	{
		Ui3SvgBitmapProof used;
		Ui3SvgUse use = Ui3SvgUse::Unverified;
		Ui3SvgFailure failure = Ui3SvgFailure::None;
		std::uint64_t frameAttemptSerial = 0;
		std::uint32_t destBits[4]{}, transformBits[6]{};
		std::uint32_t finalOpacityBits = 0, windowPresentationAlpha = 0;
		std::uint32_t effectiveClipBits[4]{}, expectedVisibleBoundsBits[4]{};
		std::uint64_t bufferMutationSerial = 0, targetInvalidationSerial = 0;
		Ui3SvgCoverage coverage = Ui3SvgCoverage::Unknown;
		bool expectedVisible = false, qualityMatches = false, clearCoversOldBounds = false;
	};
	struct Ui3SvgCounters
	{
		std::uint64_t lookupHit = 0, lookupMiss = 0, createAttempt = 0, createSuccess = 0, createFailure = 0;
		std::uint64_t parseCalls = 0, parseFailure = 0, rasterCalls = 0, rasterFailure = 0;
		std::uint64_t uploadCalls = 0, uploadFailure = 0, drawSubmit = 0, drawRejected = 0;
		std::uint64_t invalidation = 0, replacement = 0, capacityEvict = 0;
		std::uint64_t readyEntries = 0, logicalReadyBytes = 0, unknownReadyEntries = 0;
		std::uint64_t invalid = 0, clockReads = 0;
		double parseMs = 0, rasterAndInternalGeometryMs = 0, uploadMs = 0, drawSubmitMs = 0;
	};
	// 只随原SVG对象保存数值来源，不拥有资源或增加普通产品的观察对象。
	struct Ui3SvgObjectObservation
	{
		Ui3SvgBitmapProof bitmap;
		std::uint64_t ownerSerial = 0, valueRevision = 0;
		std::uint32_t tag = 0;
		Ui3SvgFailure lastFailure = Ui3SvgFailure::None;
		bool initializedObserved = false, semanticKnown = false;
	};
	struct Ui3SvgFrameTarget
	{
		std::uint64_t revision = 0, epoch = 0, surfaceSerial = 0, frameAttemptSerial = 0;
		std::uint32_t width = 0, height = 0, backingWidth = 0, backingHeight = 0, dpi = 0, windowAlpha = 0;
		std::uint32_t viewportBits[4]{};
		std::uint64_t zoomBits = 0;
	};
	class Ui3SvgProbe
	{
	public:
		explicit Ui3SvgProbe(std::uint64_t ownerSerial) noexcept : ownerSerial_(ownerSerial) {}
		Ui3SvgProbe(const Ui3SvgProbe&) = delete;
		Ui3SvgProbe& operator=(const Ui3SvgProbe&) = delete;
		[[nodiscard]] std::uint64_t OwnerSerial() const noexcept { return ownerSerial_; }
		void SetOffscreenFault(Ui3SvgOffscreenFault fault) noexcept { fault_ = fault; }
		[[nodiscard]] Ui3SvgOffscreenFault OffscreenFault() const noexcept { return fault_; }
		void NoteValueWrite(Ui3SvgObjectObservation&, bool ownedInitialization) noexcept;
		[[nodiscard]] bool Bind(Ui3SvgObjectObservation&, const void* object, std::uint32_t tag,
			bool hasBitmap, std::uint32_t pixelWidth, std::uint32_t pixelHeight) noexcept;
		void NoteCacheAttempt(Ui3SvgObjectObservation&) noexcept;
		void NoteCacheResult(Ui3SvgObjectObservation&, const Ui3SvgBitmapProof&, Ui3SvgFailure) noexcept;
		void NoteCacheReset(Ui3SvgObjectObservation&, bool hadBitmap) noexcept;
		void NoteLookup(bool needUpdate, bool knownReady) noexcept;
		void NoteOperation(Ui3SvgStage, Ui3SvgOperation) noexcept;
		void BeginFrame(std::uint64_t revision, std::uint64_t epoch, std::uint64_t attempt) noexcept;
		void Require(const void* object, const Ui3SvgObjectObservation&, const Ui3SvgBitmapProof& expected,
			bool expectedVisible, bool semanticSettled, std::uint32_t opacityBits) noexcept;
		[[nodiscard]] bool NeedsHiddenProof(std::uint32_t tag) const noexcept;
		void BeginBackingWrite(const void* context, const Ui3SvgFrameTarget&) noexcept;
		void PushClip(const void* context, const std::uint32_t rectBits[4], const std::uint32_t transformBits[6]) noexcept;
		void PopClip(const void* context) noexcept;
		void ObserveClear(const void* context) noexcept;
		void ObserveUnknownWrite(const void* context) noexcept;
		void ObserveDraw(const void* context, const Ui3SvgObjectObservation&, Ui3SvgDrawObservation) noexcept;
		void ObserveRejected(const Ui3SvgObjectObservation&, Ui3SvgFailure, bool qualityFallback = false) noexcept;
		[[nodiscard]] Ui3FiniteResourceProof FinishDrawing() noexcept;
		void CompleteAttempt(bool committed) noexcept;
		[[nodiscard]] Ui3SvgDrawObservation Observation(std::uint32_t tag) const noexcept;
		[[nodiscard]] Ui3SvgCounters CountersAfterOwnerStopped() const noexcept { return counters_; }
		[[nodiscard]] Ui3SvgCounters InitializationCountersAfterOwnerStopped() const noexcept { return initializationCounters_; }
		[[nodiscard]] const Ui3SvgFrameTarget& Target() const noexcept { return target_; }
		// 仅 render owner 使用，功能读回不得把失败后的 backing 当作旧成功帧。
		[[nodiscard]] std::uint64_t BufferMutationSerialForCurrentOwner() const noexcept { return mutationSerial_; }
		[[nodiscard]] std::uint64_t TargetInvalidationSerialForCurrentOwner() const noexcept { return invalidationSerial_; }
		[[nodiscard]] double* ElapsedCounter(Ui3SvgStage) noexcept;
		[[nodiscard]] std::int64_t ReadTicks() noexcept;
		static constexpr bool CapacityEvictionApplicable = false;
	private:
		struct Slot
		{
			const void* object = nullptr; // 仅当前owned对象映射，不进入外部DTO。
			std::uint32_t tag = 0;
			Ui3SvgBitmapProof bitmap, expected;
			Ui3SvgDrawObservation draw;
			std::uint32_t oldBounds[4]{};
			std::uint64_t bytes = 0;
			bool required = false, expectedVisible = false, semanticSettled = false;
		bool submitted = false, oldBoundsKnown = false, possiblyVisible = false, overwritten = false;
		bool hiddenLineageUnknown = false, clearedOld = false;
			std::uint32_t opacityBits = 0;
		};
		[[nodiscard]] Slot* Find(std::uint32_t tag) noexcept;
		[[nodiscard]] const Slot* Find(std::uint32_t tag) const noexcept;
		void Increment(std::uint64_t&) noexcept;
		[[nodiscard]] Ui3SvgCounters& CurrentCounters() noexcept;
		std::array<Slot, Ui3SvgCapacity> slots_{};
		std::array<std::array<std::uint32_t, 4>, 8> clips_{};
		std::size_t slotCount_ = 0, clipDepth_ = 0;
		std::uint32_t unboundRequired_ = 0;
		std::uint64_t ownerSerial_ = 0, mutationSerial_ = 0, invalidationSerial_ = 0;
		std::uint64_t previousSurface_ = 0, previousEpoch_ = 0;
		Ui3SvgFrameTarget target_;
		Ui3SvgOffscreenFault fault_ = Ui3SvgOffscreenFault::None;
		Ui3SvgCounters counters_;
		Ui3SvgCounters initializationCounters_;
		const void* context_ = nullptr;
		bool drawing_ = false, clipKnown_ = false, clearObserved_ = false, frameInvalid_ = false, mappingInvalid_ = false;
		bool fullBackingCleared_ = false;
		std::uint32_t clearBounds_[4]{};
	};
	struct Ui3SvgScopeState
	{
		Ui3SvgProbe* probe = nullptr;
		bool clocksEnabled = false, ownedInitialization = false;
	};
	[[nodiscard]] Ui3SvgScopeState* CurrentUi3SvgScope() noexcept;
	class SvgObservationScope
	{
	public:
		SvgObservationScope(Ui3SvgProbe*, bool clocksEnabled = false, bool ownedInitialization = false) noexcept;
		~SvgObservationScope();
		SvgObservationScope(const SvgObservationScope&) = delete;
		SvgObservationScope& operator=(const SvgObservationScope&) = delete;
	private:
		Ui3SvgScopeState state_;
		Ui3SvgScopeState* previous_ = nullptr;
	};
	class Ui3SvgStageTimer
	{
	public:
		explicit Ui3SvgStageTimer(Ui3SvgStage) noexcept;
		~Ui3SvgStageTimer();
	private:
		Ui3SvgProbe* probe_ = nullptr;
		double* elapsed_ = nullptr;
		std::int64_t started_ = 0;
	};
	void ObserveUi3SvgOperation(Ui3SvgStage, Ui3SvgOperation) noexcept;
	void ObserveUi3SvgClear(const void* context) noexcept;
	void ObserveUi3SvgUnknownWrite(const void* context) noexcept;
	void ObserveUi3SvgClipPush(const void* context, const std::uint32_t rectBits[4],
		const std::uint32_t transformBits[6]) noexcept;
	void ObserveUi3SvgClipPop(const void* context) noexcept;
	struct Ui3FiniteCommitIdentity
	{
		std::uint64_t epoch = 0, surfaceSerial = 0, frameAttemptSerial = 0, anchorMappingSerial = 0;
		std::uint64_t mainAnchorBits[2]{}, drawAnchorBits[2]{};
		std::uint32_t targetWidth = 0, targetHeight = 0;
		bool anchorsValid = false;
	};
	struct Ui3FixtureReadyValue
	{
		std::uint64_t generation = 0, committedCount = 0, lastCommittedAttempt = 0, epoch = 0, surfaceSerial = 0;
		std::uint64_t mainAnchorBits[2]{}, drawAnchorBits[2]{}, anchorMappingSerial = 0;
		std::uint32_t targetWidth = 0, targetHeight = 0, flags = 0;
		Ui3FiniteSignature initialStableSignature;
		bool timingValid = false;
		std::int64_t commitTicks = 0;
	};
	inline constexpr std::uint32_t Ui3FiniteReadyRegistered = 1u << 0;
	inline constexpr std::uint32_t Ui3FiniteReadyInteraction = 1u << 1;
	inline constexpr std::uint32_t Ui3FiniteReadyTransaction = 1u << 2;
	inline constexpr std::uint32_t Ui3FiniteReadyLayoutStable = 1u << 3;
	inline constexpr std::uint32_t Ui3FiniteReadyAnchors = 1u << 4;
	inline constexpr std::uint32_t Ui3FiniteReadyStopped = 1u << 5;
	struct Ui3FiniteObserverCounters
	{
		std::uint64_t frames = 0, goalsSeen = 0, retained = 0, dropped = 0;
		std::uint64_t commits = 0, layoutSettled = 0, completed = 0;
		std::uint64_t resourceUnverified = 0, unverified = 0, superseded = 0, invalid = 0;
	};

	class Ui3FiniteObserver
	{
	public:
		explicit Ui3FiniteObserver(Ui3FinitePublication& publication) noexcept;
		Ui3FiniteObserver(const Ui3FiniteObserver&) = delete;
		Ui3FiniteObserver& operator=(const Ui3FiniteObserver&) = delete;
		[[nodiscard]] bool SnapshotAccepted(Ui3FiniteAccepted&) const noexcept;
		// 私有夹具在所有 render/Interaction owner 启动前显式启用，普通 constructor 不变。
		[[nodiscard]] bool EnableBootstrapBaselineBeforeOwnersStart() noexcept;
		[[nodiscard]] bool CopyCompletedOutcomeForCurrentOwner(std::uint64_t run, std::uint64_t step,
			std::uint64_t source, std::uint64_t revision, Ui3FiniteTargetRecord& out) const noexcept;
		[[nodiscard]] bool InitialPublicationOnly() const noexcept { return publication_->PublicationSerial() == 0; }
		void BeginFrame(const Ui3FiniteAccepted&, std::uint64_t epoch, std::uint64_t frameAttempt,
			bool initialPublication = false) noexcept;
		[[nodiscard]] bool MarkConsumed(const Ui3FiniteSignature&, std::uint64_t rootBatch,
			std::uint64_t drawBatch, std::int64_t consumedTicks = 0) noexcept;
		void ObserveProperty(Ui3PropertyRole, bool activeAfterAdvance, bool sameAfterAdvance) noexcept;
		void ObserveLifecycle(std::uint32_t pendingFlags) noexcept;
		void ObserveResources(const Ui3FiniteResourceProof&) noexcept;
		// 绘制后仅补资源字段，禁止再次消费/计layoutSettled或读取新business。
		[[nodiscard]] Ui3FiniteCandidate FinalizeResources(const Ui3FiniteCandidate&, const Ui3FiniteResourceProof&) noexcept;
		void BindSvgProbeBeforeOwnersStart(Ui3SvgProbe* probe) noexcept { svgProbe_ = probe; }
		[[nodiscard]] Ui3SvgProbe* SvgProbe() const noexcept { return svgProbe_; }
		[[nodiscard]] Ui3FiniteCandidate SettleCandidate(std::uint64_t surfaceSerial,
			std::uint32_t width, std::uint32_t height, std::int64_t settledTicks = 0) noexcept;
		[[nodiscard]] Ui3FiniteStatus CompleteAttempt(const Ui3FiniteCandidate&, bool committed,
			bool timingValid, std::int64_t trueCommitTicks, const Ui3FiniteCommitIdentity& = {}) noexcept;
		void AbortFrame(Ui3FiniteStatus) noexcept;
		void NotifyRegistered() noexcept;
		void NotifyInteractionReady() noexcept;
		[[nodiscard]] bool TryReadReady(Ui3FixtureReadyValue&) const noexcept;
		void SealAfterRenderStopped(Ui3FiniteStatus status = Ui3FiniteStatus::Stopped) noexcept;
		[[nodiscard]] Ui3FiniteObserverCounters CountersAfterRenderStopped() const noexcept { return counters_; }
		[[nodiscard]] std::span<const Ui3FiniteTargetRecord> RecordsAfterRenderStopped() const noexcept
		{
			return { records_.data(), static_cast<std::size_t>(counters_.retained) };
		}
	private:
		[[nodiscard]] bool FreezeBootstrapSignature(const Ui3FiniteSignature&) noexcept;
		void PublishReady() noexcept;
		void StoreOutcome(Ui3FiniteStatus, bool timingValid = false, std::int64_t ticks = 0) noexcept;
		void ApplyResourceProof() noexcept;
		[[nodiscard]] std::uint32_t RequiredRoles() const noexcept;
		Ui3FinitePublication* publication_;
		std::atomic<std::uint32_t> renderOwnerThread_ = 0;
		bool bootstrapEnabled_ = false, bootstrapFrozen_ = false;
		Ui3SvgProbe* svgProbe_ = nullptr;
		Ui3FiniteCandidate candidate_;
		Ui3FiniteResourceProof resource_;
		Ui3FiniteSignature consumedSignature_;
		std::uint32_t seenRoles_ = 0, lifecycle_ = 0;
		bool initialPublication_ = false, consumed_ = false, sealed_ = false, frameClosed_ = true, trackedCompleted_ = false;
		Ui3FiniteStatus lastOutcome_ = Ui3FiniteStatus::Pending;
		std::size_t recordIndex_ = Ui3FiniteCapacity;
		std::uint64_t previousEpoch_ = 0, previousSurface_ = 0, trackedRun_ = 0, trackedRevision_ = 0;
		std::array<Ui3FiniteTargetRecord, Ui3FiniteCapacity> records_{};
		Ui3FiniteObserverCounters counters_;
		// bool/padding不进入word编码；ready由render唯一publisher写数字word。
		static constexpr std::size_t ReadyWords = 22;
		std::atomic<std::uint64_t> readySerial_ = 0;
		std::array<std::atomic<std::uint64_t>, ReadyWords> readyWords_{};
		std::atomic<std::uint32_t> ownerReadyFlags_ = 0;
		Ui3FixtureReadyValue ready_;
	};
	// 私有binding仅在各owner启动前安装、全部真正join后撤；普通产品默认null。
	Ui3FiniteObserver* SetActiveUi3FiniteObserver(Ui3FiniteObserver*) noexcept;
	[[nodiscard]] Ui3FiniteObserver* ActiveUi3FiniteObserver() noexcept;
	void NotifyFixtureInteractionReady() noexcept;
	[[nodiscard]] bool FitsUi3SvgCaptureBudget(std::size_t capacity, std::size_t callbackBytes, std::size_t batchBytes) noexcept;

	static_assert(std::is_trivially_copyable_v<Ui3FiniteCandidate> && std::is_trivially_copyable_v<Ui3FixtureReadyValue>);
	static_assert(sizeof(Ui3FiniteObserver) + sizeof(Ui3FinitePublication) <= 512 * 1024);
	static_assert(sizeof(Ui3SvgProbe) <= 256 * 1024);
	static_assert(std::is_trivially_copyable_v<Ui3SvgBitmapProof> && std::is_trivially_copyable_v<Ui3SvgDrawObservation>);

	static_assert(sizeof(Ui3FiniteSignature) == 72 && sizeof(Ui3FiniteAccepted) == 136);
	static_assert(std::is_standard_layout_v<Ui3FiniteAccepted> && std::is_trivially_copyable_v<Ui3FiniteAccepted>);
	static_assert(std::is_trivially_copyable_v<Ui3FiniteTargetRecord>);
	static_assert(sizeof(Ui3FinitePublication) <= 256 * 1024);
}
