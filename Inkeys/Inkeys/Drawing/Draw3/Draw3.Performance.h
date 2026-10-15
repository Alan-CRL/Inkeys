#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <d3d11.h>
#include <dxgi.h>
#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <utility>

namespace Inkeys::Drawing::Draw3
{
	struct PerformanceExportStatus
	{
		bool enabled = false;
		std::uint64_t hostRuns = 0, exportedHostRuns = 0, failedHostRuns = 0;
		std::uint64_t startupFailedHostRuns = 0, runFailedHostRuns = 0, unavailableHostRuns = 0;
		std::uint64_t framesSeen = 0, framesRetained = 0, framesDropped = 0;
		std::uint64_t inputsSeen = 0, inputsRetained = 0, inputsDropped = 0, payloadBudgetBytes = 0;
		std::uint64_t receiptsSeen = 0, receiptsRetained = 0, receiptsDropped = 0, proofOverflow = 0;
		std::uint64_t graphicsCallsSeen = 0, graphicsCallsRetained = 0, graphicsCallsDropped = 0;
		std::uint64_t slowGraphicsCallsSeen = 0, slowGraphicsCallsRetained = 0, slowGraphicsCallsDropped = 0;
		std::uint64_t resourcesSeen = 0, resourcesRetained = 0, resourcesDropped = 0, graphicsGroupCallsDropped = 0;
		std::uint64_t graphicsSiteCollisions = 0;
	};
	PerformanceExportStatus GetDraw3PerformanceExportStatus() noexcept;
	int RunDraw3PerformanceProbe() noexcept;
	int RunPerformanceRecorderProbe() noexcept;

	enum class PerformanceStage : std::uint32_t
	{
		Control, Ingress, ModelReset, ModelUpdate, ModelPredict, Geometry, Snapshot, Composite,
		UploadMap, UploadCopy, DrawSubmit, Present, UlwCopySubmit, UlwMap, UlwRows, UlwUpdate,
		DeadlineWait, IdleWait, Count
	};
	enum class PerformanceCallbackOutcome : std::uint32_t
	{
		Unknown, Published, PublishRejected, StateBusy, DecoderMissing, DecodeFailed,
		TouchpadFiltered, NoBinding, Cancelled, InvalidArgument
	};
	struct PerformanceInputKey;
	struct PerformanceInputTrace
	{
		class PerformanceSession* session = nullptr;
		std::uint32_t index = 0;
		// Contact模块只借用此函数指针，独立MTd Headless无需链接采集/JSON/GPU实现。
		void (*publication)(PerformanceInputTrace*, PerformanceInputKey, std::int64_t,
			std::uint64_t, std::uint32_t, std::uint32_t) noexcept = nullptr;
		void (*result)(PerformanceInputTrace*, bool) noexcept = nullptr;
	};
	struct PerformanceInputKey
	{
		std::uintptr_t record = 0;
		std::uint64_t generation = 0, sequence = 0;
		friend bool operator==(const PerformanceInputKey&, const PerformanceInputKey&) = default;
	};
	struct PerformanceProof
	{
		std::uint64_t workspace = 0, page = 0, scene = 0, raster = 0, output = 0;
		std::uint64_t content = 0, item = 0, frame = 0, rawOutputRevision = 0;
		std::uint32_t kind = 0, target = 0;
		friend bool operator==(const PerformanceProof&, const PerformanceProof&) = default;
	};
	struct PerformanceFrameFacts
	{
		std::uint32_t physicalBefore = 0, physicalAfter = 0, width = 0, height = 0;
		std::uint32_t reasons = 0, commands = 0, downs = 0, consumed = 0, terminal = 0, tools = 0;
		double threadCpuMs = -1;
	};
	struct PerformanceCapacities
	{
		std::size_t frames = 16384, inputs = 32768, receipts = 65536, gpu = 2048;
		std::size_t graphicsCalls = 8192, slowCalls = 4096, graphicsGroups = 2048, resources = 512;
	};
	struct PerformanceClock
	{
		void* context = nullptr;
		std::int64_t (*read)(void*) noexcept = nullptr;
		std::int64_t frequency = 0;
	};
	enum class PerformanceGraphicsOperation : std::uint32_t
	{
		Map, WriteCopy, WriteConstant, Unmap, Bind, Copy, Clear, Draw, Present, Other, Count
	};
	enum class PerformanceGraphicsRole : std::uint32_t
	{
		Unknown, InkData, GlobalConstants, HighlighterData, LaserData, CompositeVertices,
		LayerTexture, BackBuffer, StagingReadback, SnapshotTexture, Other, Count
	};
	enum class PerformanceGraphicsPass : std::uint32_t
	{
		Unknown, Initialization, Geometry, Snapshot, Composite, Present, Recovery, Count
	};
	struct PerformanceGraphicsSite
	{
		const char* name;
		std::uint32_t id = 2166136261u;
		// 只接受静态字符串；可在编译期生成稳定位置编号，原始包不泄露地址。
		consteval PerformanceGraphicsSite(const char* value) noexcept : name(value)
		{ for (auto* p = value; *p; ++p) { id ^= static_cast<unsigned char>(*p); id *= 16777619u; } }
	};
	struct PerformanceGraphicsArguments
	{
		ID3D11Resource* resource = nullptr;
		ID3D11Resource* relatedResource = nullptr;
		std::uint64_t bytes = 0, offset = 0;
		std::uint32_t subresource = 0, mapType = 0, flags = 0, argument0 = 0, argument1 = 0;
		PerformanceGraphicsPass pass = PerformanceGraphicsPass::Unknown;
	};
	struct PerformanceGraphicsToken
	{
		std::uint64_t serial = 0, frame = 0, epoch = 0, resource = 0, relatedResource = 0;
		std::int64_t start = 0, end = 0;
		std::uint64_t bytes = 0, offset = 0, parent = 0;
		std::uint32_t site = 0;
		PerformanceGraphicsOperation operation = PerformanceGraphicsOperation::Other;
		PerformanceGraphicsRole role = PerformanceGraphicsRole::Unknown;
		PerformanceGraphicsPass pass = PerformanceGraphicsPass::Unknown;
		std::uint32_t subresource = 0, mapType = 0, flags = 0, argument0 = 0, argument1 = 0;
		HRESULT result = S_FALSE;
		std::uint32_t accounting = 0, depth = 0;
	};

	// Host 显式创建；callback 数组多写者各占一槽，receipt/frame 仅由绘制 owner 写。
	class PerformanceSession
	{
	public:
		explicit PerformanceSession(PerformanceCapacities capacities = {}, PerformanceClock clock = {});
		~PerformanceSession();
		PerformanceSession(const PerformanceSession&) = delete;
		PerformanceSession& operator=(const PerformanceSession&) = delete;
		static std::unique_ptr<PerformanceSession> CreateHost(const std::wstring& root) noexcept;
		void ExportHost(bool startupFailed, bool runFailed, std::uint64_t legacyPayloadBytes, bool unavailable = false) noexcept;
		[[nodiscard]] std::int64_t Clock() noexcept;
		[[nodiscard]] std::int64_t Frequency() const noexcept;
		[[nodiscard]] std::size_t AllocatedBytes() const noexcept;
		std::uint64_t RegisterResource(ID3D11Resource*, PerformanceGraphicsRole,
			PerformanceGraphicsSite, HRESULT result = S_OK) noexcept;
		std::uint64_t ResourceId(ID3D11Resource*) const noexcept;
		void RetireResource(ID3D11Resource*) noexcept;
		PerformanceGraphicsToken BeginGraphicsCall(PerformanceGraphicsOperation, PerformanceGraphicsRole,
			PerformanceGraphicsSite, const PerformanceGraphicsArguments& = {}) noexcept;
		void EndGraphicsCall(PerformanceGraphicsToken, bool resultAvailable = false, HRESULT result = S_FALSE) noexcept;

		PerformanceInputTrace* BeginInput(std::uint32_t phase, std::uint32_t tablet,
			std::uint32_t contact, std::uint32_t packets) noexcept;
		void EndInput(PerformanceInputTrace*, PerformanceCallbackOutcome outcome) noexcept;
		void Publication(PerformanceInputTrace*, PerformanceInputKey, std::int64_t sourceQpc,
			std::uint64_t admission, std::uint32_t phase, std::uint32_t device) noexcept;
		void PublicationResult(PerformanceInputTrace*, bool succeeded) noexcept;
		bool PublicationMatches(const PerformanceInputTrace*, PerformanceInputKey,
			std::int64_t source, std::uint64_t admission) const noexcept;
		void Consume(PerformanceInputKey, std::int64_t sourceQpc, std::uint64_t admission,
			std::uint32_t phase, bool proofSupported) noexcept;
		void ProcessEnd(PerformanceInputKey) noexcept;
		void RegisterTool(PerformanceInputKey, std::uint32_t tool) noexcept;
		void InputSupport(PerformanceInputKey, bool supported) noexcept;
		std::uint64_t ReceiptsSeen() const noexcept;
		bool InputProofSupported(PerformanceInputKey) const noexcept;
		std::int64_t PriorStageTicks(std::uint64_t serial, PerformanceStage) const noexcept;
		void Adopt(PerformanceInputKey, std::uint64_t content) noexcept;
		void Raster(PerformanceInputKey, std::uint64_t frame, bool succeeded) noexcept;
		void Stored(PerformanceInputKey, std::uint64_t content) noexcept;
		void StageProof(PerformanceInputKey, const PerformanceProof&) noexcept;
		bool Confirm(PerformanceInputKey, const PerformanceProof&, std::int64_t returned,
			bool eligible, bool apiSucceeded) noexcept;
		void Invalidate(std::uintptr_t record, std::uint64_t generation, std::uint32_t reason) noexcept;
		void LegacyDownConfirmed(std::uintptr_t record, std::uint64_t generation) noexcept;
		bool LegacyDownWasConfirmed(std::uintptr_t record, std::uint64_t generation) const noexcept;

		void BeginFrame(std::uint64_t serial, std::int64_t qpc, std::uint32_t physicalBefore) noexcept;
		void EndFrame(std::int64_t qpc, bool retain, const PerformanceFrameFacts& facts) noexcept;
		void Stage(PerformanceStage, std::int64_t start, std::int64_t end) noexcept;
		void IdleBoundary() noexcept;
		void PresentResult(bool attempted, bool apiAttempted, HRESULT result, std::int64_t returned,
			RECT dirty, bool full, std::uint32_t mode, std::uint32_t target,
			std::uint64_t outputRevision, std::uint64_t contentRevision) noexcept;
		bool LastPresentApiSucceeded() const noexcept;
		void PresentApiResult(HRESULT result, bool attempted = true) noexcept;
		void PresentReturned(std::int64_t qpc) noexcept;
		void Upload(std::uint64_t bytes, D3D11_MAP type, HRESULT result) noexcept;
		void Draw(std::uint32_t count = 1) noexcept;
		void CopiedBytes(std::uint64_t bytes) noexcept;
		void FrameExtent(std::uint32_t width, std::uint32_t height) noexcept;
		void Command() noexcept;
		void Tools(std::uint32_t mask) noexcept;

		void Initialization(const char* stage, std::int64_t start, HRESULT result,
			bool succeeded, std::uint64_t detail0 = 0, std::uint64_t detail1 = 0,
			std::uint64_t detail2 = 0, std::uint64_t detail3 = 0) noexcept;
		void CaptureGraphics(ID3D11Device*, ID3D11DeviceContext*, IDXGIAdapter*,
			std::uint32_t driver, std::uint32_t feature) noexcept;
		void CapturePresenter(std::uint32_t mode, std::uint32_t width, std::uint32_t height) noexcept;
		void CaptureUploadCapability(bool supported, HRESULT queryResult) noexcept;
		void CaptureDisplay(double dpi, std::uint32_t width, std::uint32_t height,
			std::uint64_t generation, std::uint32_t topology) noexcept;
		void CaptureModelConfiguration(double dpiScale, double targetFps) noexcept;
		std::uint64_t ConfirmedInputs() const noexcept;
		void GpuBegin(std::uint64_t frame) noexcept;
		void GpuWork(PerformanceGraphicsPass pass) noexcept;
		void GpuDraw(PerformanceGraphicsPass pass, std::uint32_t count = 1) noexcept;
		void GpuMarker(std::uint32_t marker) noexcept;
		void GpuEnd() noexcept;
		void GpuStop() noexcept;

	private:
		struct Impl;
		std::unique_ptr<Impl> impl_;
		friend int RunPerformanceRecorderProbe() noexcept;
	};

	// 关闭分支只保留空指针；不创建大 POD，不读 clock，不访问全局原子开关。
	class PerformanceScope
	{
	public:
		PerformanceScope(PerformanceSession* session, PerformanceStage stage) noexcept
			: session_(session), stage_(stage), start_(session ? session->Clock() : 0) {}
		~PerformanceScope() { Finish(); }
		void Finish() noexcept
		{
			if (!session_) return;
			auto* session = std::exchange(session_, nullptr);
			session->Stage(stage_, start_, session->Clock());
		}
	private:
		PerformanceSession* session_;
		PerformanceStage stage_;
		std::int64_t start_;
	};
	class PerformanceProcessScope
	{
	public:
		PerformanceProcessScope(PerformanceSession* session, PerformanceInputKey key,
			std::int64_t source, std::uint64_t admission, std::uint32_t phase, bool supported) noexcept
			: session_(session), key_(key)
		{ if (session_) session_->Consume(key, source, admission, phase, supported); }
		~PerformanceProcessScope() { if (session_) session_->ProcessEnd(key_); }
	private:
		PerformanceSession* session_;
		PerformanceInputKey key_;
	};
	class PerformanceInputScope
	{
	public:
		PerformanceInputScope(PerformanceSession* session, std::uint32_t phase,
			std::uint32_t tablet, std::uint32_t contact, std::uint32_t packets) noexcept
			: trace_(session ? session->BeginInput(phase, tablet, contact, packets) : nullptr) {}
		~PerformanceInputScope()
		{ if (trace_) trace_->session->EndInput(trace_, outcome_); }
		PerformanceInputTrace* Trace() const noexcept { return trace_; }
		void Outcome(PerformanceCallbackOutcome outcome) noexcept { if (trace_) outcome_ = outcome; }
	private:
		PerformanceInputTrace* trace_;
		PerformanceCallbackOutcome outcome_ = PerformanceCallbackOutcome::Unknown;
	};
	template<class Call>
	auto PerformanceCall(PerformanceSession* session, PerformanceStage stage, Call&& call)
	{
		PerformanceScope scope(session, stage);
		return std::forward<Call>(call)();
	}
	template<class Arguments, class Call>
	HRESULT PerformanceGraphicsCall(PerformanceSession* session, PerformanceGraphicsOperation operation,
		PerformanceGraphicsRole role, PerformanceGraphicsSite site, Arguments&& arguments, Call&& call)
	{
		if (!session) return std::forward<Call>(call)();
		const auto token = session->BeginGraphicsCall(operation, role, site, std::forward<Arguments>(arguments)());
		const HRESULT result = std::forward<Call>(call)();
		session->EndGraphicsCall(token, true, result);
		return result;
	}
	template<class Arguments, class Call>
	void PerformanceGraphicsVoid(PerformanceSession* session, PerformanceGraphicsOperation operation,
		PerformanceGraphicsRole role, PerformanceGraphicsSite site, Arguments&& arguments, Call&& call)
	{
		if (!session) { std::forward<Call>(call)(); return; }
		const auto token = session->BeginGraphicsCall(operation, role, site, std::forward<Arguments>(arguments)());
		std::forward<Call>(call)();
		session->EndGraphicsCall(token);
	}
	inline HRESULT PerformanceMap(PerformanceSession* session, ID3D11DeviceContext* context,
		ID3D11Resource* resource, UINT subresource, D3D11_MAP type, UINT flags,
		D3D11_MAPPED_SUBRESOURCE* mapped, std::uint64_t bytes = 0,
		PerformanceGraphicsRole role = PerformanceGraphicsRole::Unknown,
		PerformanceGraphicsSite site = "map_unspecified", PerformanceGraphicsPass pass = PerformanceGraphicsPass::Unknown) noexcept
	{
		if (!session) return context->Map(resource, subresource, type, flags, mapped);
		PerformanceScope scope(session, PerformanceStage::UploadMap);
		PerformanceGraphicsArguments arguments{};
		arguments.resource = resource; arguments.bytes = bytes; arguments.subresource = subresource;
		arguments.mapType = type; arguments.flags = flags; arguments.pass = pass;
		const auto token = session->BeginGraphicsCall(PerformanceGraphicsOperation::Map, role, site, arguments);
		const HRESULT result = context->Map(resource, subresource, type, flags, mapped);
		session->EndGraphicsCall(token, true, result);
		session->Upload(bytes, type, result);
		return result;
	}
}
