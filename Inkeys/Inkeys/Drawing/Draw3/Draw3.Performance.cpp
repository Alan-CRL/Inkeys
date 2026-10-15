#include "Draw3.Performance.h"

#include <algorithm>
#include <atomic>
#include <charconv>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <json/json.h>
#include <limits>
#include <mutex>
#include <new>
#include <string_view>
#include <unordered_map>
#include <thread>
#include <wrl/client.h>

namespace Inkeys::Drawing::Draw3
{
	namespace
	{
		constexpr std::size_t kPayloadBudget = 32u * 1024 * 1024, kPendingCapacity = 64, kQuerySlots = 8;
		constexpr std::uint64_t kGpuInterval = 8, kGpuFirstWork = 8, kGpuPriorInterval = 16, kGpuPriorFirstWork = 4;
		constexpr std::size_t kGraphicsOperations = static_cast<std::size_t>(PerformanceGraphicsOperation::Count);
		constexpr std::uint32_t kCallTimeValid = 1, kCallHasResult = 2, kCallAccountingValid = 4, kCallResourceKnown = 8;
		enum class CallAttribution : std::uint32_t { Frame, Prior, SplitPriorFrame, CrossFrame, Unassigned };
		constexpr const char* kOperationNames[] = { "map", "write_copy", "write_constant", "unmap", "bind", "copy", "clear", "draw", "present", "other" };
		constexpr const char* kRoleNames[] = { "unknown", "ink_data", "global_constants", "highlighter_data", "laser_data", "composite_vertices",
			"layer_texture", "back_buffer", "staging_readback", "snapshot_texture", "other" };
		constexpr const char* kPassNames[] = { "unknown", "initialization", "geometry", "snapshot", "composite", "present", "recovery" };
		constexpr const char* kAttributionNames[] = { "frame", "prior", "split_prior_frame", "cross_frame", "unassigned" };
		constexpr const char* kResourceKinds[] = { "unknown", "buffer", "texture1d", "texture2d", "texture3d" };
		template<class T, std::size_t N> const char* EnumName(const char* const (&names)[N], T value) noexcept
		{ const auto index = static_cast<std::size_t>(value); return index < N ? names[index] : "unknown"; }
		bool AddTicks(std::int64_t& total, std::int64_t ticks) noexcept
		{
			if (ticks < 0 || total > (std::numeric_limits<std::int64_t>::max)() - ticks) return false;
			total += ticks; return true;
		}
		bool AddUnsigned(std::uint64_t& total, std::uint64_t value) noexcept
		{
			if (total > (std::numeric_limits<std::uint64_t>::max)() - value) return false;
			total += value; return true;
		}
		struct Overhead
		{
			PerformanceClock clock;
			std::int64_t total = 0, maximum = 0;
			std::uint64_t count = 0, invalid = 0;
			void Record(std::int64_t start, std::int64_t end) noexcept
			{
				++count;
				if (start <= 0 || end < start || !AddTicks(total, end - start)) { ++invalid; return; }
				maximum = (std::max)(maximum, end - start);
			}
		};
		class OverheadScope
		{
		public:
			explicit OverheadScope(Overhead& value) noexcept : value_(value), start_(value.clock.read(value.clock.context)) {}
			~OverheadScope() { value_.Record(start_, value_.clock.read(value_.clock.context)); }
		private:
			Overhead& value_;
			std::int64_t start_;
		};
		constexpr auto kMissing = (std::numeric_limits<std::size_t>::max)();
		std::mutex exportMutex;
		PerformanceExportStatus exportStatus;
		std::uint64_t nextHost = 0;
		std::atomic_uint64_t allocationCount = 0, queryCreateCount = 0;
		std::int64_t Qpc(void*) noexcept
		{
			LARGE_INTEGER value{};
			return QueryPerformanceCounter(&value) ? value.QuadPart : 0;
		}
		const char* CallbackName(PerformanceCallbackOutcome outcome) noexcept
		{
			const char* names[] = { "unknown", "published", "publish_rejected", "state_busy", "decoder_missing",
				"decode_failed", "touchpad_filtered", "no_binding", "cancelled", "invalid_argument" };
			const auto index = static_cast<std::size_t>(outcome);
			return index < std::size(names) ? names[index] : names[0];
		}
		bool ValidProof(const PerformanceProof& p) noexcept
		{
			return p.workspace && p.page && p.scene && p.raster && p.output && p.content && p.frame &&
				(p.kind == 1 || (p.kind == 2 && p.item)) && p.target <= 1;
		}
		struct KeyHash
		{
			std::size_t operator()(const PerformanceInputKey& key) const noexcept
			{
				return std::hash<std::uintptr_t>{}(key.record) ^
					(std::hash<std::uint64_t>{}(key.generation) << 1) ^
					(std::hash<std::uint64_t>{}(key.sequence) << 2);
			}
		};
		// 结束后固定64KiB缓冲批量写；CREATE_NEW拒绝覆写，热路径不触碰此类。
		class OutputFile
		{
		public:
			explicit OutputFile(const std::wstring& path) noexcept : file_(CreateFileW(path.c_str(),
				GENERIC_WRITE, FILE_SHARE_READ, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr)) {}
			~OutputFile() { if (file_ != INVALID_HANDLE_VALUE) CloseHandle(file_); }
			void Text(std::string_view value) noexcept
			{
				while (!value.empty() && good_)
				{
					const auto count = (std::min)(value.size(), buffer_.size() - used_);
					std::memcpy(buffer_.data() + used_, value.data(), count);
					used_ += count; value.remove_prefix(count);
					if (used_ == buffer_.size()) Flush();
				}
			}
			void BeginRow() noexcept { columns_ = 0; }
			void Field(std::string_view value) noexcept { if (columns_++) Text(","); Text(value); }
			void Field(const char* value) noexcept { Field(std::string_view(value)); }
			void Field(bool value) noexcept { Field(value ? "1" : "0"); }
			template<class T> void Field(T value) noexcept
			{
				char bytes[64]{};
				auto result = std::to_chars(bytes, bytes + sizeof(bytes), value);
				Field(result.ec == std::errc{} ? std::string_view(bytes, result.ptr - bytes) : std::string_view{});
			}
			template<class... T> void Row(T... values) noexcept { BeginRow(); (Field(values), ...); Text("\r\n"); }
			void EndRow() noexcept { Text("\r\n"); }
			bool Finish() noexcept { Flush(); return good_ && file_ != INVALID_HANDLE_VALUE; }
		private:
			void Flush() noexcept
			{
				if (!used_) return;
				DWORD written = 0;
				if (file_ == INVALID_HANDLE_VALUE || !WriteFile(file_, buffer_.data(),
					static_cast<DWORD>(used_), &written, nullptr) || written != used_) good_ = false;
				used_ = 0;
			}
			HANDLE file_ = INVALID_HANDLE_VALUE;
			std::array<char, 64u * 1024> buffer_;
			std::size_t used_ = 0, columns_ = 0;
			bool good_ = true;
		};
		Json::Value U64(std::uint64_t value) { return Json::Value(Json::UInt64(value)); }
		bool JsonFile(const std::wstring& path, const Json::Value& value)
		{
			Json::StreamWriterBuilder builder; builder["indentation"] = "  ";
			OutputFile file(path); file.Text(Json::writeString(builder, value)); return file.Finish();
		}
		std::string Utf8(const wchar_t* text)
		{
			const int count = WideCharToMultiByte(CP_UTF8, 0, text, -1, nullptr, 0, nullptr, nullptr);
			if (count <= 1) return {};
			std::string result(static_cast<std::size_t>(count), '\0');
			WideCharToMultiByte(CP_UTF8, 0, text, -1, result.data(), count, nullptr, nullptr);
			result.pop_back(); return result;
		}
	}

	struct PerformanceSession::Impl
	{
		struct Input
		{
			PerformanceInputTrace trace;
			PerformanceInputKey key;
			std::int64_t entry = 0, source = 0, published = 0, end = 0;
			std::uint64_t admission = 0;
			std::uint32_t phase = 0, tablet = 0, contact = 0, packets = 0, device = UINT32_MAX; // 未解码回调不能伪装为Touch。
			PerformanceCallbackOutcome outcome = PerformanceCallbackOutcome::Unknown;
			bool publicationSucceeded = false;
		};
		struct Receipt
		{
			PerformanceInputKey key;
			PerformanceProof proof;
			std::int64_t source = 0, consume = 0, processEnd = 0, adopt = 0, raster = 0, stored = 0, returned = 0;
			std::uint64_t admission = 0, content = 0, rasterFrame = 0;
			std::uint32_t phase = 0, invalidation = 0, tool = UINT32_MAX;
			bool supported = false, staged = false, confirmed = false, presentFailed = false;
		};
		struct Frame
		{
			struct GraphicsSummary
			{
				std::array<std::int64_t, kGraphicsOperations> ticks{};
				std::array<std::uint32_t, kGraphicsOperations> counts{};
				std::uint32_t invalidCalls = 0;
				std::uint16_t observed = 0, invalid = 0;
			};
			std::uint64_t serial = 0, chain = 0, epoch = 0;
			std::int64_t start = 0, end = 0, returned = 0;
			std::array<std::int64_t, static_cast<std::size_t>(PerformanceStage::Count)> stages{};
			std::uint32_t observed = 0, invalid = 0;
			std::array<std::int64_t, static_cast<std::size_t>(PerformanceStage::Count)> priorStages{};
			std::uint32_t priorObserved = 0, priorInvalid = 0;
			std::uint64_t priorUploadBytes = 0, priorDrawCalls = 0, priorMapCalls = 0;
			std::uint32_t priorDowns = 0, priorConsumed = 0, priorTools = 0;
			PerformanceFrameFacts facts;
			RECT dirty{};
			bool idle = false, attempted = false, apiAttempted = false, succeeded = false, full = false;
			HRESULT result = S_FALSE;
			std::uint32_t mode = 0, target = 0;
			std::uint64_t outputRevision = 0, contentRevision = 0, uploadBytes = 0;
			std::uint64_t drawCalls = 0, mapCalls = 0, mapFailures = 0, discard = 0, noOverwrite = 0;
			GraphicsSummary graphics, priorGraphics;
		};
		struct GraphicsGroup
		{
			std::uint32_t site = 0, operation = 0, role = 0, pass = 0, attribution = 0;
			bool used = false, overflow = false;
			std::uint64_t seen = 0, valid = 0, invalid = 0, failed = 0, resultCalls = 0, bytes = 0;
			std::int64_t total = 0, minimum = 0, maximum = 0;
			std::uint64_t maxSerial = 0, maxFrame = 0;
		};
		struct Resource
		{
			ID3D11Resource* pointer = nullptr; // 只作owner内身份查找，不AddRef、不写出地址。
			std::uint64_t id = 0, epoch = 0;
			std::int64_t created = 0, destroyed = 0;
			PerformanceGraphicsRole role = PerformanceGraphicsRole::Unknown;
			std::uint32_t site = 0, kind = 0;
			bool descriptor = false;
			std::uint32_t bytes = 0, width = 0, height = 0, format = 0, usage = 0;
			std::uint32_t bind = 0, cpu = 0, misc = 0, stride = 0;
			HRESULT result = S_FALSE;
		};
		struct Site { const char* name = nullptr; std::uint32_t id = 0; };
		struct CallStack { std::uint64_t serial = 0; bool nested = false; };
		struct Pending
		{
			std::uintptr_t record = 0;
			std::uint64_t generation = 0;
			std::size_t receipt = kMissing;
			bool legacyConfirmed = false, registered = false;
			std::uint32_t tool = UINT32_MAX;
		};
		struct Init
		{
			const char* stage = nullptr;
			std::int64_t start = 0, end = 0;
			HRESULT result = S_OK;
			bool succeeded = false;
			std::array<std::uint64_t, 4> detail{};
		};
		enum class GpuStatus : std::uint32_t { Pending, Ready, Disjoint, Failed, Invalid, Stopped, Replaced };
		struct Gpu
		{
			std::uint64_t frame = 0, epoch = 0, frequency = 0;
			std::array<std::uint64_t, 6> timestamps{};
			GpuStatus status = GpuStatus::Pending;
			HRESULT result = S_FALSE;
			std::uint64_t loop = 0, candidate = 0, draws = 0, geometryDraws = 0, compositeDraws = 0, readbacks = 0;
			std::uint64_t domainCandidate = 0;
			std::int64_t cpuBegin = 0, cpuEnd = 0;
			std::uint32_t markers = 0, clipped = 0, workMask = 0;
			bool firstWork = false, prior = false;
		};
		struct QuerySlot
		{
			Microsoft::WRL::ComPtr<ID3D11Query> disjoint;
			std::array<Microsoft::WRL::ComPtr<ID3D11Query>, 6> timestamps;
			std::size_t row = kMissing;
			std::uint32_t markers = 0;
		};
		// 仅纯CPU probe注入，生产保持空函数表并直接调用既有context；不引入全局开关。
		struct QueryTransport
		{
			void* state = nullptr;
			void (*begin)(void*, std::size_t) noexcept = nullptr;
			void (*timestamp)(void*, std::size_t, std::uint32_t) noexcept = nullptr;
			void (*end)(void*, std::size_t) noexcept = nullptr;
			HRESULT (*data)(void*, std::size_t, std::uint32_t, void*, UINT, UINT) noexcept = nullptr;
		} transport;
		PerformanceCapacities capacity;
		PerformanceClock clock;
		std::unique_ptr<Input[]> inputs;
		std::unique_ptr<Receipt[]> receipts;
		std::unique_ptr<Frame[]> frames;
		std::unique_ptr<Gpu[]> gpu;
		std::unique_ptr<PerformanceGraphicsToken[]> graphicsCalls, slowCalls;
		std::unique_ptr<GraphicsGroup[]> graphicsGroups;
		std::unique_ptr<Resource[]> resources;
		std::array<Site, 512> sites{};
		std::array<CallStack, 64> callStack{};
		std::size_t graphicsCount = 0, slowCount = 0, groupCount = 0, resourceCount = 0, siteCount = 0;
		std::uint64_t graphicsSeen = 0, nextGraphicsSerial = 0, slowSeen = 0, resourcesSeen = 0, groupCallsDropped = 0;
		std::uint64_t graphicsInvalid = 0, graphicsNested = 0, unknownResources = 0, siteCollisions = 0;
		std::uint64_t graphicsCrossFrame = 0, graphicsOverlaps = 0, groupOverflows = 0, arithmeticOverflows = 0;
		std::int64_t slowThreshold = 0, detailDroppedAfter = 0;
		std::int64_t lastLeafEnd = 0;
		std::uint32_t callDepth = 0;
		Overhead metadataOverhead, bookkeepingOverhead, queryIssueOverhead, queryPollOverhead;
		std::atomic_uint64_t inputsSeen = 0, bundledPackets = 0;
		std::uint64_t receiptsSeen = 0, framesSeen = 0, proofOverflow = 0, invalidIntervals = 0, confirmedInputs = 0;
		std::size_t frameCount = 0, receiptCount = 0, gpuCount = 0, initCount = 0;
		std::uint64_t gpuRingFull = 0, gpuPendingStop = 0, gpuPendingReplaced = 0, gpuEpoch = 0, activeChain = 1;
		std::uint64_t gpuWorkCandidates = 0, gpuSelected = 0, gpuSlotsBusy = 0, gpuCapacitySkips = 0, gpuFailures = 0;
		std::uint64_t gpuDrawFrames = 0, gpuLoop = 0, gpuLoopEpoch = 0, lastDrawLoop = 0, gpuEpochCandidates = 0;
		std::uint64_t gpuPriorDrawSegments = 0, gpuPriorCandidates = 0, gpuFrameCandidates = 0, gpuPriorEpochCandidates = 0;
		std::uint64_t gpuFrameSelected = 0, gpuPriorSelected = 0;
		std::uint32_t gpuPassMask = 0;
		bool gpuLoopCandidate = false, gpuLoopHasDraw = false;
		std::array<Pending, kPendingCapacity> pending{};
		std::array<Init, 128> initialization{};
		std::array<QuerySlot, kQuerySlots> querySlots;
		Microsoft::WRL::ComPtr<ID3D11Device> device;
		Microsoft::WRL::ComPtr<ID3D11DeviceContext> context;
		std::size_t currentGpu = kMissing, pollSlot = 0, lastFrame = kMissing;
		HRESULT gpuQueryResult = E_PENDING;
		bool gpuAvailable = false, frameOpen = false, hostExported = false, restartChain = false;
		Frame current, prior;
		std::wstring output;
		DXGI_ADAPTER_DESC adapter{};
		LARGE_INTEGER driverVersion{};
		HRESULT driverVersionResult = E_FAIL;
		bool adapterAvailable = false, noOverwriteSupported = false;
		std::uint32_t driver = 0, feature = 0, presenter = 0, width = 0, height = 0;
		std::size_t allocated = 0;
		double targetDpi = 0, modelDpiScale = 0, targetFps = 0;
		std::uint64_t displayGeneration = 0;
		std::uint32_t displayTopology = 0, displayWidth = 0, displayHeight = 0;
		Pending* Find(std::uintptr_t record, std::uint64_t generation) noexcept
		{
			for (auto& value : pending) if (value.record == record && value.generation == generation) return &value;
			return nullptr;
		}
		const Pending* Find(std::uintptr_t record, std::uint64_t generation) const noexcept
		{
			for (const auto& value : pending) if (value.record == record && value.generation == generation) return &value;
			return nullptr;
		}
		Receipt* Find(PerformanceInputKey key) noexcept
		{
			auto* value = Find(key.record, key.generation);
			return value && value->receipt < receiptCount && receipts[value->receipt].key == key ? &receipts[value->receipt] : nullptr;
		}
		Frame* StageFrame() noexcept { return frameOpen ? &current : &prior; }
		Frame* WaitFrame() noexcept { return frameOpen ? &current : lastFrame < frameCount ? &frames[lastFrame] : nullptr; }
		bool GraphicsDetailComplete() const noexcept
		{
			// 慢记录与完整前缀重叠；只有完整前缀/分组/资源/位置身份缺失才降低此覆盖标志。
			return graphicsSeen == graphicsCount && !groupCallsDropped && resourcesSeen == resourceCount && !siteCollisions;
		}
		std::uint64_t ReadyDrawSamples(bool priorDomain) const noexcept
		{
			std::uint64_t count = 0;
			for (std::size_t i = 0; i < gpuCount; ++i)
			{
				const auto& row = gpu[i];
				if (row.prior != priorDomain || (priorDomain ? row.frame != 0 : row.frame == 0)) continue;
				if (row.status == GpuStatus::Ready && row.frequency > 0 && row.draws > 0 &&
					((row.geometryDraws > 0 && (row.markers & 3) == 3) ||
					 (row.compositeDraws > 0 && (row.markers & 12) == 12))) ++count;
			}
			return count;
		}
		void RegisterSite(PerformanceGraphicsSite site) noexcept
		{
			for (std::size_t i = 0; i < siteCount; ++i)
				if (sites[i].id == site.id)
				{ if (std::strcmp(sites[i].name, site.name)) ++siteCollisions; return; }
			if (siteCount == sites.size()) { ++siteCollisions; return; }
			sites[siteCount++] = { site.name, site.id };
		}
		void Summary(Frame::GraphicsSummary& summary, std::size_t operation,
			std::int64_t ticks, bool valid) noexcept
		{
			if (operation >= kGraphicsOperations) return;
			const auto bit = static_cast<std::uint16_t>(1u << operation); summary.observed |= bit;
			if (summary.counts[operation] == UINT32_MAX)
			{ ++arithmeticOverflows; valid = false; }
			else ++summary.counts[operation];
			if (valid && !AddTicks(summary.ticks[operation], ticks)) { ++arithmeticOverflows; valid = false; }
			if (!valid)
			{
				summary.invalid |= bit;
				if (summary.invalidCalls != UINT32_MAX) ++summary.invalidCalls;
			}
		}
		void Group(const PerformanceGraphicsToken& call, CallAttribution attribution) noexcept
		{
			const auto operation = static_cast<std::uint32_t>(call.operation);
			const auto role = static_cast<std::uint32_t>(call.role), pass = static_cast<std::uint32_t>(call.pass);
			const auto source = static_cast<std::uint32_t>(attribution);
			const auto hash = call.site ^ (operation * 16777619u) ^ (role * 709607u) ^ (pass * 104729u) ^ (source * 8191u);
			for (std::size_t n = 0; n < capacity.graphicsGroups; ++n)
			{
				auto& group = graphicsGroups[(hash + n) % capacity.graphicsGroups];
				if (!group.used)
				{
					group.used = true; group.site = call.site; group.operation = operation;
					group.role = role; group.pass = pass; group.attribution = source; ++groupCount;
				}
				if (group.site != call.site || group.operation != operation || group.role != role ||
					group.pass != pass || group.attribution != source) continue;
				++group.seen;
				if (call.accounting & kCallHasResult) { ++group.resultCalls; if (FAILED(call.result)) ++group.failed; }
				if (!AddUnsigned(group.bytes, call.bytes)) { group.overflow = true; ++arithmeticOverflows; }
				if (!(call.accounting & kCallAccountingValid)) { ++group.invalid; return; }
				const auto ticks = call.end - call.start;
				++group.valid;
				if (group.valid == 1 || ticks < group.minimum) group.minimum = ticks;
				if (group.valid == 1 || ticks > group.maximum)
				{ group.maximum = ticks; group.maxSerial = call.serial; group.maxFrame = call.frame; }
				if (!AddTicks(group.total, ticks)) { group.overflow = true; ++arithmeticOverflows; }
				return;
			}
			++groupCallsDropped; ++groupOverflows;
		}
		HRESULT QueryData(std::size_t slot, std::uint32_t marker, void* data, UINT bytes) noexcept
		{
			constexpr auto flags = D3D11_ASYNC_GETDATA_DONOTFLUSH;
			if (transport.data) return transport.data(transport.state, slot, marker, data, bytes, flags);
			return context->GetData(marker < 6 ? querySlots[slot].timestamps[marker].Get() : querySlots[slot].disjoint.Get(), data, bytes, flags);
		}
		void QueryBegin(std::size_t slot) noexcept
		{ if (transport.begin) transport.begin(transport.state, slot); else context->Begin(querySlots[slot].disjoint.Get()); }
		void QueryTimestamp(std::size_t slot, std::uint32_t marker) noexcept
		{ if (transport.timestamp) transport.timestamp(transport.state, slot, marker); else context->End(querySlots[slot].timestamps[marker].Get()); }
		void QueryEnd(std::size_t slot) noexcept
		{ if (transport.end) transport.end(transport.state, slot); else context->End(querySlots[slot].disjoint.Get()); }
		const char* Outcome(const Receipt* value, const Input& input) const noexcept
		{
			if (!input.publicationSucceeded) return CallbackName(input.outcome);
			if (!value) return "published_not_consumed";
			if (value->phase == 3) return "cancelled";
			if (!value->supported) return "proof_unsupported";
			if (value->confirmed) return "confirmed_software_present";
			if (value->invalidation == UINT32_MAX) return "invalid_interval";
			if (value->invalidation) return "source_invalidated";
			if (!value->adopt) return "consumed_no_geometry";
			if (!value->raster && !value->stored) return "adopted_no_raster";
			return value->presentFailed ? "present_failed" : "rastered_no_present";
		}
		void RetireQueries(GpuStatus reason) noexcept
		{
			for (auto& slot : querySlots)
			{
				if (slot.row < gpuCount && gpu[slot.row].status == GpuStatus::Pending)
				{
					gpu[slot.row].status = reason;
					if (reason == GpuStatus::Stopped) ++gpuPendingStop;
					else if (reason == GpuStatus::Replaced) ++gpuPendingReplaced;
				}
				slot = {};
			}
			currentGpu = kMissing; gpuAvailable = false;
		}
		void AdvanceDeviceEpoch() noexcept
		{
			RetireQueries(GpuStatus::Replaced); ++gpuEpoch;
			gpuEpochCandidates = 0; gpuPriorEpochCandidates = 0; gpuLoopCandidate = false; gpuPassMask = 0;
			// 恢复可发生在Begin之后；本帧实际使用新设备，不能留在旧设备间隔分组中。
			if (frameOpen) current.epoch = gpuEpoch;
		}
		void PollGpu() noexcept
		{
			if (!gpuAvailable || (!context && !transport.data)) return;
			const auto index = pollSlot++ % querySlots.size(); auto& slot = querySlots[index];
			if (slot.row >= gpuCount || slot.row == currentGpu) return;
			auto& row = gpu[slot.row];
			if (row.status != GpuStatus::Pending) { slot.row = kMissing; return; }
			OverheadScope measured(queryPollOverhead);
			D3D11_QUERY_DATA_TIMESTAMP_DISJOINT data{};
			const HRESULT hr = QueryData(index, 6, &data, sizeof(data));
			if (hr == S_FALSE) return; // 单轮非阻塞；不等、不Flush、不强制提交GPU命令。
			row.result = hr;
			if (FAILED(hr)) { row.status = GpuStatus::Failed; ++gpuFailures; slot.row = kMissing; return; }
			if (data.Disjoint || !data.Frequency) { row.status = GpuStatus::Disjoint; slot.row = kMissing; return; }
			row.frequency = data.Frequency;
			bool monotonic = true;
			for (std::size_t i = 0; i < row.timestamps.size(); ++i)
			{
				if (!(slot.markers & (1u << i))) continue;
				const HRESULT result = QueryData(index, static_cast<std::uint32_t>(i), &row.timestamps[i], sizeof(std::uint64_t));
				if (FAILED(result)) { row.result = result; row.status = GpuStatus::Failed; ++gpuFailures; break; }
				if (result != S_OK) return; // 任一timestamp未就绪立即退出，不扫下一项制造隐式等待。
			}
			if (row.status == GpuStatus::Failed) { slot.row = kMissing; return; }
			for (std::size_t i = 0; i < row.timestamps.size(); i += 2)
				if ((slot.markers & (3u << i)) == (3u << i) && row.timestamps[i + 1] < row.timestamps[i]) monotonic = false;
			row.status = monotonic ? GpuStatus::Ready : GpuStatus::Invalid;
			slot.row = kMissing;
		}
	};

	PerformanceSession::PerformanceSession(PerformanceCapacities capacities, PerformanceClock clock)
		: impl_(std::make_unique<Impl>())
	{
		static_assert(sizeof(PerformanceGraphicsToken) == 128 && sizeof(Impl::Frame::GraphicsSummary) == 128);
		static_assert(sizeof(Impl::GraphicsGroup) <= 128 && sizeof(Impl::Resource) <= 128);
		static_assert(sizeof(Impl) + 16384u * sizeof(Impl::Frame) + 32768u * sizeof(Impl::Input) +
			65536u * sizeof(Impl::Receipt) + 2048u * sizeof(Impl::Gpu) +
			(8192u + 4096u) * sizeof(PerformanceGraphicsToken) + 2048u * sizeof(Impl::GraphicsGroup) +
			512u * sizeof(Impl::Resource) <= kPayloadBudget);
		auto& p = *impl_;
		if (!capacities.frames || capacities.frames > 16384 || !capacities.inputs || capacities.inputs > 32768 ||
			!capacities.receipts || capacities.receipts > 65536 || !capacities.gpu || capacities.gpu > 2048 ||
			!capacities.graphicsCalls || capacities.graphicsCalls > 8192 || !capacities.slowCalls || capacities.slowCalls > 4096 ||
			!capacities.graphicsGroups || capacities.graphicsGroups > 2048 || !capacities.resources || capacities.resources > 512) throw std::bad_alloc{};
		p.capacity = capacities; p.clock = clock;
		if (!p.clock.read)
		{
			LARGE_INTEGER frequency{};
			p.clock.read = &Qpc;
			if (QueryPerformanceFrequency(&frequency)) p.clock.frequency = frequency.QuadPart;
		}
		p.slowThreshold = p.clock.frequency > 0 ? p.clock.frequency / 1000 + (p.clock.frequency % 1000 != 0) : 0;
		p.metadataOverhead.clock = p.bookkeepingOverhead.clock = p.queryIssueOverhead.clock = p.queryPollOverhead.clock = p.clock;
		p.allocated = sizeof(Impl) + capacities.frames * sizeof(Impl::Frame) + capacities.inputs * sizeof(Impl::Input) +
			capacities.receipts * sizeof(Impl::Receipt) + capacities.gpu * sizeof(Impl::Gpu) +
			(capacities.graphicsCalls + capacities.slowCalls) * sizeof(PerformanceGraphicsToken) +
			capacities.graphicsGroups * sizeof(Impl::GraphicsGroup) + capacities.resources * sizeof(Impl::Resource);
		if (p.allocated > kPayloadBudget) throw std::bad_alloc{};
		p.inputs = std::make_unique<Impl::Input[]>(capacities.inputs);
		p.receipts = std::make_unique<Impl::Receipt[]>(capacities.receipts);
		p.frames = std::make_unique<Impl::Frame[]>(capacities.frames);
		p.gpu = std::make_unique<Impl::Gpu[]>(capacities.gpu);
		p.graphicsCalls = std::make_unique<PerformanceGraphicsToken[]>(capacities.graphicsCalls);
		p.slowCalls = std::make_unique<PerformanceGraphicsToken[]>(capacities.slowCalls);
		p.graphicsGroups = std::make_unique<Impl::GraphicsGroup[]>(capacities.graphicsGroups);
		p.resources = std::make_unique<Impl::Resource[]>(capacities.resources);
		allocationCount.fetch_add(9, std::memory_order_relaxed);
	}
	PerformanceSession::~PerformanceSession() = default;
	std::int64_t PerformanceSession::Clock() noexcept { return impl_->clock.read(impl_->clock.context); }
	std::int64_t PerformanceSession::Frequency() const noexcept { return impl_->clock.frequency; }
	std::size_t PerformanceSession::AllocatedBytes() const noexcept { return impl_->allocated; }
	std::uint64_t PerformanceSession::ResourceId(ID3D11Resource* resource) const noexcept
	{
		if (!resource) return 0;
		const auto& p = *impl_;
		for (std::size_t i = p.resourceCount; i > 0; --i)
		{
			const auto& value = p.resources[i - 1];
			if (value.pointer == resource && value.epoch == p.gpuEpoch && !value.destroyed && SUCCEEDED(value.result)) return value.id;
		}
		return 0;
	}
	std::uint64_t PerformanceSession::RegisterResource(ID3D11Resource* resource, PerformanceGraphicsRole role,
		PerformanceGraphicsSite site, HRESULT result) noexcept
	{
		auto& p = *impl_; OverheadScope measured(p.metadataOverhead);
		p.RegisterSite(site);
		const auto now = Clock(); const auto id = ++p.resourcesSeen;
		// 同地址重新分配也必须换代；即使描述前缀已满，也不再借用旧身份。
		for (std::size_t i = 0; i < p.resourceCount; ++i)
			if (resource && p.resources[i].pointer == resource && !p.resources[i].destroyed)
			{ p.resources[i].destroyed = now; p.resources[i].pointer = nullptr; }
		if (p.resourceCount == p.capacity.resources) return 0;
		auto& value = p.resources[p.resourceCount++];
		value.id = id; value.epoch = p.gpuEpoch; value.pointer = resource; value.role = role;
		value.site = site.id; value.created = now; value.result = result;
		if (!resource || FAILED(result)) return id;
		D3D11_RESOURCE_DIMENSION kind{}; resource->GetType(&kind); value.kind = kind;
		if (kind == D3D11_RESOURCE_DIMENSION_BUFFER)
		{
			D3D11_BUFFER_DESC desc{}; static_cast<ID3D11Buffer*>(resource)->GetDesc(&desc);
			value.bytes = desc.ByteWidth; value.usage = desc.Usage; value.bind = desc.BindFlags;
			value.cpu = desc.CPUAccessFlags; value.misc = desc.MiscFlags; value.stride = desc.StructureByteStride; value.descriptor = true;
		}
		else if (kind == D3D11_RESOURCE_DIMENSION_TEXTURE1D)
		{
			D3D11_TEXTURE1D_DESC desc{}; static_cast<ID3D11Texture1D*>(resource)->GetDesc(&desc);
			value.width = desc.Width; value.height = 1; value.format = desc.Format; value.usage = desc.Usage;
			value.bind = desc.BindFlags; value.cpu = desc.CPUAccessFlags; value.misc = desc.MiscFlags; value.descriptor = true;
		}
		else if (kind == D3D11_RESOURCE_DIMENSION_TEXTURE2D)
		{
			D3D11_TEXTURE2D_DESC desc{}; static_cast<ID3D11Texture2D*>(resource)->GetDesc(&desc);
			value.width = desc.Width; value.height = desc.Height; value.format = desc.Format; value.usage = desc.Usage;
			value.bind = desc.BindFlags; value.cpu = desc.CPUAccessFlags; value.misc = desc.MiscFlags; value.descriptor = true;
		}
		else if (kind == D3D11_RESOURCE_DIMENSION_TEXTURE3D)
		{
			D3D11_TEXTURE3D_DESC desc{}; static_cast<ID3D11Texture3D*>(resource)->GetDesc(&desc);
			value.width = desc.Width; value.height = desc.Height; value.format = desc.Format; value.usage = desc.Usage;
			value.bind = desc.BindFlags; value.cpu = desc.CPUAccessFlags; value.misc = desc.MiscFlags; value.descriptor = true;
		}
		return id;
	}
	void PerformanceSession::RetireResource(ID3D11Resource* resource) noexcept
	{
		if (!resource) return;
		auto& p = *impl_;
		for (std::size_t i = 0; i < p.resourceCount; ++i)
			if (p.resources[i].pointer == resource && !p.resources[i].destroyed)
			{ p.resources[i].destroyed = Clock(); p.resources[i].pointer = nullptr; }
	}
	PerformanceGraphicsToken PerformanceSession::BeginGraphicsCall(PerformanceGraphicsOperation operation,
		PerformanceGraphicsRole role, PerformanceGraphicsSite site, const PerformanceGraphicsArguments& arguments) noexcept
	{
		auto& p = *impl_; const auto metadataStart = Clock();
		PerformanceGraphicsToken call{};
		call.serial = ++p.nextGraphicsSerial; call.frame = p.frameOpen ? p.current.serial : 0; call.epoch = p.gpuEpoch;
		call.resource = ResourceId(arguments.resource); call.relatedResource = ResourceId(arguments.relatedResource);
		if ((arguments.resource && !call.resource) || (arguments.relatedResource && !call.relatedResource)) ++p.unknownResources;
		if (call.resource) call.accounting |= kCallResourceKnown;
		p.RegisterSite(site); call.site = site.id; call.operation = operation; call.role = role; call.pass = arguments.pass;
		call.bytes = arguments.bytes; call.offset = arguments.offset; call.subresource = arguments.subresource;
		call.mapType = arguments.mapType; call.flags = arguments.flags; call.argument0 = arguments.argument0; call.argument1 = arguments.argument1;
		call.depth = p.callDepth++;
		if (call.depth > 0 && call.depth <= p.callStack.size())
		{ auto& parent = p.callStack[call.depth - 1]; call.parent = parent.serial; parent.nested = true; }
		if (call.depth < p.callStack.size()) p.callStack[call.depth] = { call.serial, call.depth != 0 };
		const bool workPass = arguments.pass != PerformanceGraphicsPass::Unknown && arguments.pass != PerformanceGraphicsPass::Initialization;
		if ((workPass && operation != PerformanceGraphicsOperation::WriteCopy && operation != PerformanceGraphicsOperation::WriteConstant) ||
			operation == PerformanceGraphicsOperation::Draw || operation == PerformanceGraphicsOperation::Copy ||
			operation == PerformanceGraphicsOperation::Clear || operation == PerformanceGraphicsOperation::Present) GpuWork(arguments.pass);
		call.start = Clock(); p.metadataOverhead.Record(metadataStart, call.start);
		return call;
	}
	void PerformanceSession::EndGraphicsCall(PerformanceGraphicsToken call, bool resultAvailable, HRESULT result) noexcept
	{
		auto& p = *impl_; call.end = Clock(); const auto bookkeepingStart = call.end;
		call.result = result; if (resultAvailable) call.accounting |= kCallHasResult;
		const auto operation = static_cast<std::size_t>(call.operation);
		const bool validTime = Frequency() > 0 && call.start > 0 && call.end >= call.start;
		const bool nested = call.parent || call.depth >= p.callStack.size() ||
			p.callDepth != call.depth + 1 || p.callStack[call.depth].serial != call.serial || p.callStack[call.depth].nested;
		if (call.depth < p.callDepth) p.callDepth = call.depth;
		if (nested) ++p.graphicsNested;
		if (validTime) call.accounting |= kCallTimeValid; else ++p.graphicsInvalid;
		CallAttribution attribution = CallAttribution::Unassigned;
		bool assignable = call.epoch == p.gpuEpoch;
		if (p.frameOpen && call.frame == p.current.serial) attribution = CallAttribution::Frame;
		else if (!call.frame && p.frameOpen && validTime && call.start < p.current.start)
		{ attribution = CallAttribution::SplitPriorFrame; call.frame = p.current.serial; }
		else if (!call.frame && !p.frameOpen) attribution = CallAttribution::Prior;
		else { attribution = CallAttribution::CrossFrame; assignable = false; ++p.graphicsCrossFrame; }
		const bool overlap = validTime && !nested && call.start < p.lastLeafEnd;
		if (overlap) ++p.graphicsOverlaps;
		if (validTime && !nested) p.lastLeafEnd = (std::max)(p.lastLeafEnd, call.end);
		const bool valid = validTime && !nested && !overlap && assignable && operation < kGraphicsOperations;
		if (valid) call.accounting |= kCallAccountingValid;
		call.accounting |= static_cast<std::uint32_t>(attribution) << 16;
		if (attribution == CallAttribution::SplitPriorFrame)
		{
			const auto split = (std::min)(call.end, p.current.start);
			p.Summary(p.current.priorGraphics, operation, split - call.start, valid);
			if (call.end >= p.current.start) p.Summary(p.current.graphics, operation, call.end - p.current.start, valid);
		}
		else if (attribution == CallAttribution::Frame) p.Summary(p.current.graphics, operation, validTime ? call.end - call.start : 0, valid);
		else if (attribution == CallAttribution::Prior) p.Summary(p.prior.graphics, operation, validTime ? call.end - call.start : 0, valid);
		else if (p.frameOpen) p.Summary(p.current.graphics, operation, 0, false);
		++p.graphicsSeen; p.Group(call, attribution);
		if (p.graphicsCount < p.capacity.graphicsCalls) p.graphicsCalls[p.graphicsCount++] = call;
		else if (!p.detailDroppedAfter) p.detailDroppedAfter = call.start;
		if (validTime && p.slowThreshold > 0 && call.end - call.start >= p.slowThreshold)
		{
			++p.slowSeen;
			if (p.slowCount < p.capacity.slowCalls) p.slowCalls[p.slowCount++] = call;
		}
		if (call.operation == PerformanceGraphicsOperation::Draw) GpuDraw(call.pass);
		if (call.operation == PerformanceGraphicsOperation::Copy && call.pass == PerformanceGraphicsPass::Present && p.currentGpu < p.gpuCount)
			++p.gpu[p.currentGpu].readbacks;
		p.bookkeepingOverhead.Record(bookkeepingStart, Clock());
	}
	PerformanceExportStatus GetDraw3PerformanceExportStatus() noexcept
	{ std::scoped_lock lock(exportMutex); return exportStatus; }

	std::unique_ptr<PerformanceSession> PerformanceSession::CreateHost(const std::wstring& root) noexcept
	{
		if (root.empty()) return nullptr;
		{ std::scoped_lock lock(exportMutex); exportStatus.enabled = true; ++exportStatus.hostRuns; }
		try
		{
			const std::filesystem::path directory(root);
			if (!directory.is_absolute() || !std::filesystem::is_directory(directory)) throw std::bad_alloc{};
			std::wstring output;
			for (std::size_t attempt = 0; attempt < 10000; ++attempt)
			{
				std::uint64_t number = 0;
				{ std::scoped_lock lock(exportMutex); number = ++nextHost; }
				wchar_t name[64]{};
				swprintf_s(name, L"host-%04llu", static_cast<unsigned long long>(number));
				output = (directory / name).wstring();
				if (CreateDirectoryW(output.c_str(), nullptr)) break;
				if (GetLastError() != ERROR_ALREADY_EXISTS) throw std::bad_alloc{};
				output.clear();
			}
			if (output.empty()) throw std::bad_alloc{};
			auto result = std::make_unique<PerformanceSession>();
			result->impl_->output = std::move(output);
			return result;
		}
		catch (...)
		{
			std::scoped_lock lock(exportMutex);
			++exportStatus.unavailableHostRuns; ++exportStatus.failedHostRuns;
			return nullptr;
		}
	}

	PerformanceInputTrace* PerformanceSession::BeginInput(std::uint32_t phase, std::uint32_t tablet,
		std::uint32_t contact, std::uint32_t packets) noexcept
	{
		auto& p = *impl_;
		const auto ordinal = p.inputsSeen.fetch_add(1, std::memory_order_relaxed);
		p.bundledPackets.fetch_add(packets, std::memory_order_relaxed);
		if (ordinal >= p.capacity.inputs) return nullptr;
		auto& value = p.inputs[ordinal];
		value.trace = { this, static_cast<std::uint32_t>(ordinal),
			[](PerformanceInputTrace* trace, PerformanceInputKey key, std::int64_t source, std::uint64_t admission,
				std::uint32_t phase, std::uint32_t device) noexcept
			{ trace->session->Publication(trace, key, source, admission, phase, device); },
			[](PerformanceInputTrace* trace, bool succeeded) noexcept { trace->session->PublicationResult(trace, succeeded); } };
		value.phase = phase; value.tablet = tablet; value.contact = contact; value.packets = packets;
		value.entry = Clock();
		return &value.trace;
	}
	void PerformanceSession::EndInput(PerformanceInputTrace* trace, PerformanceCallbackOutcome outcome) noexcept
	{
		if (!trace || trace->session != this || trace->index >= impl_->capacity.inputs) return;
		auto& value = impl_->inputs[trace->index];
		value.end = Clock();
		if (outcome != PerformanceCallbackOutcome::Unknown) value.outcome = outcome;
	}
	void PerformanceSession::Publication(PerformanceInputTrace* trace, PerformanceInputKey key,
		std::int64_t source, std::uint64_t admission, std::uint32_t phase, std::uint32_t device) noexcept
	{
		if (!trace || trace->session != this || trace->index >= impl_->capacity.inputs) return;
		auto& value = impl_->inputs[trace->index];
		value.key = key; value.source = source; value.admission = admission;
		value.phase = phase; value.device = device; value.published = Clock();
	}
	void PerformanceSession::PublicationResult(PerformanceInputTrace* trace, bool succeeded) noexcept
	{
		if (!trace || trace->session != this || trace->index >= impl_->capacity.inputs) return;
		auto& value = impl_->inputs[trace->index];
		value.publicationSucceeded = succeeded;
		value.outcome = succeeded ? PerformanceCallbackOutcome::Published : PerformanceCallbackOutcome::PublishRejected;
	}
	bool PerformanceSession::PublicationMatches(const PerformanceInputTrace* trace, PerformanceInputKey key,
		std::int64_t source, std::uint64_t admission) const noexcept
	{
		if (!trace || trace->session != this || trace->index >= impl_->capacity.inputs) return false;
		const auto& value = impl_->inputs[trace->index];
		return value.key == key && value.source == source && value.admission == admission && value.published > 0;
	}
	void PerformanceSession::Consume(PerformanceInputKey key, std::int64_t source, std::uint64_t admission,
		std::uint32_t phase, bool supported) noexcept
	{
		auto& p = *impl_;
		if (auto* existing = p.Find(key)) { existing->supported = existing->supported || supported; return; }
		++p.receiptsSeen;
		if (auto* frame = p.StageFrame()) { ++frame->facts.consumed; if (phase == 0) ++frame->facts.downs; }
		if (!key.record || !key.generation || !key.sequence || (key.sequence & 1) || source <= 0 ||
			p.receiptCount == p.capacity.receipts) return;
		auto* pending = p.Find(key.record, key.generation);
		if (!pending)
		{
			for (auto& candidate : p.pending) if (!candidate.record) { pending = &candidate; break; }
			if (!pending) { ++p.proofOverflow; return; }
			*pending = { key.record, key.generation, kMissing, false };
		}
		auto& value = p.receipts[p.receiptCount];
		value.key = key; value.source = source; value.admission = admission;
		value.consume = Clock(); value.phase = phase; value.supported = supported; value.tool = pending->tool;
		if (value.consume <= 0 || value.consume < source) { ++p.invalidIntervals; value.invalidation = UINT32_MAX; }
		pending->registered = pending->registered || supported;
		pending->receipt = p.receiptCount++;
	}
	std::uint64_t PerformanceSession::ReceiptsSeen() const noexcept { return impl_->receiptsSeen; }
	void PerformanceSession::InputSupport(PerformanceInputKey key, bool supported) noexcept
	{ if (auto* receipt = impl_->Find(key)) receipt->supported = supported; }
	void PerformanceSession::RegisterTool(PerformanceInputKey key, std::uint32_t tool) noexcept
	{
		if (auto* pending = impl_->Find(key.record, key.generation)) { pending->registered = true; pending->tool = tool; }
		if (auto* receipt = impl_->Find(key)) receipt->tool = tool;
		if (tool < 32) if (auto* frame = impl_->StageFrame()) frame->facts.tools |= 1u << tool;
	}
	bool PerformanceSession::InputProofSupported(PerformanceInputKey key) const noexcept
	{ const auto* pending = impl_->Find(key.record, key.generation); return pending && pending->receipt < impl_->receiptCount && impl_->receipts[pending->receipt].supported; }
	std::int64_t PerformanceSession::PriorStageTicks(std::uint64_t serial, PerformanceStage stage) const noexcept
	{
		const auto i = static_cast<std::size_t>(stage);
		if (i >= static_cast<std::size_t>(PerformanceStage::Count)) return 0;
		for (std::size_t f = 0; f < impl_->frameCount; ++f) if (impl_->frames[f].serial == serial) return impl_->frames[f].priorStages[i];
		return 0;
	}
	void PerformanceSession::ProcessEnd(PerformanceInputKey key) noexcept
	{
		if (auto* value = impl_->Find(key); value && !value->confirmed)
		{
			value->processEnd = Clock();
			if (value->processEnd < value->consume) { ++impl_->invalidIntervals; value->invalidation = UINT32_MAX; }
			if (auto* pending = impl_->Find(key.record, key.generation); pending && !pending->registered) *pending = {};
		}
	}
	void PerformanceSession::Adopt(PerformanceInputKey key, std::uint64_t content) noexcept
	{
		if (auto* value = impl_->Find(key); value && value->supported && content && !value->invalidation && !value->confirmed)
		{
			const auto tick = Clock();
			if (tick < value->consume) { ++impl_->invalidIntervals; value->invalidation = UINT32_MAX; return; }
			value->content = content; value->adopt = tick; value->raster = 0; value->staged = false;
		}
	}
	void PerformanceSession::Raster(PerformanceInputKey key, std::uint64_t frame, bool succeeded) noexcept
	{
		if (auto* value = impl_->Find(key); value && value->adopt && succeeded && frame && !value->invalidation && !value->confirmed)
		{
			const auto tick = Clock();
			if (tick < value->adopt) { ++impl_->invalidIntervals; value->invalidation = UINT32_MAX; return; }
			value->raster = tick; value->rasterFrame = frame;
		}
	}
	void PerformanceSession::Stored(PerformanceInputKey key, std::uint64_t content) noexcept
	{
		if (auto* value = impl_->Find(key); value && value->phase == 2 && value->supported && content && !value->invalidation && !value->confirmed)
		{
			const auto tick = Clock();
			if (tick < value->consume) { ++impl_->invalidIntervals; value->invalidation = UINT32_MAX; return; }
			value->content = content; value->stored = tick;
		}
	}
	void PerformanceSession::StageProof(PerformanceInputKey key, const PerformanceProof& proof) noexcept
	{
		auto* value = impl_->Find(key);
		if (!value || !value->supported || value->invalidation || !ValidProof(proof) ||
			value->content != proof.content || value->confirmed ||
			(proof.kind == 1 && (value->phase == 2 || !value->adopt || !value->raster || value->rasterFrame != proof.frame)) ||
			(proof.kind == 2 && (!value->stored || value->phase != 2))) return;
		value->proof = proof; value->staged = true;
	}
	bool PerformanceSession::Confirm(PerformanceInputKey key, const PerformanceProof& proof,
		std::int64_t returned, bool eligible, bool succeeded) noexcept
	{
		auto* value = impl_->Find(key);
		if (!value || !value->staged || value->confirmed || value->invalidation ||
			!eligible || value->proof != proof || !ValidProof(proof)) return false;
		if (!succeeded) { value->presentFailed = true; return false; }
		if (returned <= 0 || returned < value->source || returned < value->consume ||
			(proof.kind == 1 && returned < value->raster) ||
			(proof.kind == 2 && returned < value->stored)) { ++impl_->invalidIntervals; return false; }
		value->returned = returned; value->confirmed = true; ++impl_->confirmedInputs; return true;
	}
	void PerformanceSession::Invalidate(std::uintptr_t record, std::uint64_t generation, std::uint32_t reason) noexcept
	{
		if (auto* pending = impl_->Find(record, generation))
		{
			if (pending->receipt < impl_->receiptCount && !impl_->receipts[pending->receipt].confirmed)
				impl_->receipts[pending->receipt].invalidation = reason + 1;
			*pending = {};
		}
	}
	void PerformanceSession::LegacyDownConfirmed(std::uintptr_t record, std::uint64_t generation) noexcept
	{ if (auto* value = impl_->Find(record, generation)) value->legacyConfirmed = true; }
	bool PerformanceSession::LegacyDownWasConfirmed(std::uintptr_t record, std::uint64_t generation) const noexcept
	{ const auto* value = impl_->Find(record, generation); return value && value->legacyConfirmed; }

	void PerformanceSession::BeginFrame(std::uint64_t serial, std::int64_t qpc, std::uint32_t physical) noexcept
	{
		auto& p = *impl_; p.current = {};
		// idle唤醒后的Down初始化/预热属于起点前工作，不能污染已封口旧帧或伪装成帧内耗时。
		p.current.priorStages = p.prior.stages; p.current.priorObserved = p.prior.observed; p.current.priorInvalid = p.prior.invalid;
		p.current.priorUploadBytes = p.prior.uploadBytes; p.current.priorDrawCalls = p.prior.drawCalls; p.current.priorMapCalls = p.prior.mapCalls;
		p.current.priorDowns = p.prior.facts.downs; p.current.priorConsumed = p.prior.facts.consumed; p.current.priorTools = p.prior.facts.tools;
		p.current.priorGraphics = p.prior.graphics;
		p.current.facts.downs = p.current.priorDowns; p.current.facts.consumed = p.current.priorConsumed; p.current.facts.tools = p.current.priorTools;
		p.prior = {};
		if (p.restartChain) { ++p.activeChain; p.restartChain = false; }
		p.current.serial = serial; p.current.start = qpc; p.current.chain = p.activeChain;
		p.current.epoch = p.gpuEpoch; p.current.facts.physicalBefore = physical; p.frameOpen = true;
	}
	void PerformanceSession::EndFrame(std::int64_t qpc, bool retain, const PerformanceFrameFacts& facts) noexcept
	{
		auto& p = *impl_;
		if (!p.frameOpen) return;
		p.current.end = qpc;
		const auto retainedFacts = p.current.facts; p.current.facts = facts;
		if (!facts.width) p.current.facts.width = retainedFacts.width;
		if (!facts.height) p.current.facts.height = retainedFacts.height;
		if (!facts.commands) p.current.facts.commands = retainedFacts.commands;
		if (!facts.downs) p.current.facts.downs = retainedFacts.downs;
		if (!facts.consumed) p.current.facts.consumed = retainedFacts.consumed;
		if (!facts.tools) p.current.facts.tools = retainedFacts.tools;
		p.frameOpen = false;
		// 下次Begin前仍可能有真实Down预热；为prior另起候选，绝不挂到已封口旧帧。
		p.gpuLoopCandidate = false; p.gpuLoopHasDraw = false; p.gpuLoop = 0;
		if (!retain) return;
		++p.framesSeen;
		if (p.frameCount < p.capacity.frames) { p.lastFrame = p.frameCount; p.frames[p.frameCount++] = p.current; }
		else p.lastFrame = kMissing;
	}
	void PerformanceSession::Stage(PerformanceStage stage, std::int64_t start, std::int64_t end) noexcept
	{
		auto* value = stage == PerformanceStage::DeadlineWait || stage == PerformanceStage::IdleWait ? impl_->WaitFrame() : impl_->StageFrame();
		const auto index = static_cast<std::size_t>(stage);
		if (!value || index >= value->stages.size()) return;
		const std::uint32_t bit = 1u << index; value->observed |= bit;
		if (start <= 0 || end < start || end - start > (std::numeric_limits<std::int64_t>::max)() - value->stages[index])
		{ value->invalid |= bit; ++impl_->invalidIntervals; return; }
		// scope可以跨过initializeStroke内部Begin；按真实QPC边界拆分，不能把起点前的Ingress算帧内。
		if (impl_->frameOpen && stage != PerformanceStage::DeadlineWait && stage != PerformanceStage::IdleWait && start < value->start)
		{
			const auto split = (std::min)(end, value->start);
			value->priorObserved |= bit; value->priorStages[index] += split - start;
			if (end > value->start) value->stages[index] += end - value->start;
		}
		else value->stages[index] += end - start;
	}
	void PerformanceSession::IdleBoundary() noexcept
	{ if (auto* value = impl_->WaitFrame()) value->idle = true; impl_->restartChain = true; }
	void PerformanceSession::PresentResult(bool attempted, bool api, HRESULT result, std::int64_t returned,
		RECT dirty, bool full, std::uint32_t mode, std::uint32_t target,
		std::uint64_t outputRevision, std::uint64_t contentRevision) noexcept
	{
		auto& value = impl_->current;
		value.attempted = attempted; value.apiAttempted = api; value.result = result;
		value.succeeded = api && result == S_OK; value.returned = returned;
		value.dirty = dirty; value.full = full; value.mode = mode; value.target = target;
		value.outputRevision = outputRevision; value.contentRevision = contentRevision;
	}
	bool PerformanceSession::LastPresentApiSucceeded() const noexcept
	{ return impl_->current.apiAttempted && impl_->current.result == S_OK; }
	void PerformanceSession::PresentApiResult(HRESULT result, bool attempted) noexcept
	{ impl_->current.apiAttempted = attempted || impl_->current.apiAttempted; impl_->current.result = result;
		impl_->current.succeeded = impl_->current.apiAttempted && result == S_OK; }
	void PerformanceSession::PresentReturned(std::int64_t qpc) noexcept { impl_->current.returned = qpc; }
	void PerformanceSession::Upload(std::uint64_t bytes, D3D11_MAP type, HRESULT result) noexcept
	{
		if (auto* value = impl_->StageFrame())
		{
			++value->mapCalls; if (FAILED(result)) ++value->mapFailures; else value->uploadBytes += bytes;
			if (type == D3D11_MAP_WRITE_DISCARD) ++value->discard;
			if (type == D3D11_MAP_WRITE_NO_OVERWRITE) ++value->noOverwrite;
		}
	}
	void PerformanceSession::FrameExtent(std::uint32_t width, std::uint32_t height) noexcept
	{ if (auto* v = impl_->StageFrame()) { v->facts.width = width; v->facts.height = height; } }
	void PerformanceSession::Tools(std::uint32_t mask) noexcept { if (auto* v = impl_->StageFrame()) v->facts.tools |= mask; }
	void PerformanceSession::Command() noexcept { if (auto* v = impl_->StageFrame()) ++v->facts.commands; }
	void PerformanceSession::CopiedBytes(std::uint64_t bytes) noexcept
	{ if (auto* v = impl_->StageFrame()) v->uploadBytes += bytes; }
	void PerformanceSession::Draw(std::uint32_t count) noexcept
	{ if (auto* value = impl_->StageFrame()) value->drawCalls += count; }


	void PerformanceSession::Initialization(const char* stage, std::int64_t start, HRESULT result,
		bool succeeded, std::uint64_t a, std::uint64_t b, std::uint64_t c, std::uint64_t d) noexcept
	{
		auto& p = *impl_;
		if (p.initCount < p.initialization.size())
			p.initialization[p.initCount++] = { stage, start, Clock(), result, succeeded, { a, b, c, d } };
	}
	std::uint64_t PerformanceSession::ConfirmedInputs() const noexcept { return impl_->confirmedInputs; }
	void PerformanceSession::CaptureDisplay(double dpi, std::uint32_t width, std::uint32_t height,
		std::uint64_t generation, std::uint32_t topology) noexcept
	{ auto& p = *impl_; p.targetDpi = dpi; p.displayWidth = width; p.displayHeight = height; p.displayGeneration = generation; p.displayTopology = topology; }
	void PerformanceSession::CaptureModelConfiguration(double dpiScale, double targetFps) noexcept
	{ impl_->modelDpiScale = dpiScale; impl_->targetFps = targetFps; }
	void PerformanceSession::CapturePresenter(std::uint32_t mode, std::uint32_t width, std::uint32_t height) noexcept
	{ impl_->presenter = mode; impl_->width = width; impl_->height = height; }
	void PerformanceSession::CaptureUploadCapability(bool supported, HRESULT result) noexcept
	{ impl_->noOverwriteSupported = supported; Initialization("map_no_overwrite", 0, result, SUCCEEDED(result), supported); }
	void PerformanceSession::CaptureGraphics(ID3D11Device* device, ID3D11DeviceContext* context,
		IDXGIAdapter* adapter, std::uint32_t driver, std::uint32_t feature) noexcept
	{
		auto& p = *impl_;
		p.driver = driver; p.feature = feature;
		p.adapterAvailable = adapter && SUCCEEDED(adapter->GetDesc(&p.adapter));
		p.driverVersionResult = adapter ? adapter->CheckInterfaceSupport(__uuidof(IDXGIDevice), &p.driverVersion) : E_POINTER;
		if (p.device.Get() == device && p.context.Get() == context) return;
		p.AdvanceDeviceEpoch(); p.device = device; p.context = context;
		if (!device || !context) { p.gpuQueryResult = E_POINTER; return; }
		p.gpuQueryResult = S_OK;
		for (auto& slot : p.querySlots)
		{
			D3D11_QUERY_DESC desc{ D3D11_QUERY_TIMESTAMP_DISJOINT, 0 };
			queryCreateCount.fetch_add(1, std::memory_order_relaxed);
			HRESULT result = device->CreateQuery(&desc, &slot.disjoint);
			for (auto& timestamp : slot.timestamps)
			{
				if (FAILED(result)) break;
				desc.Query = D3D11_QUERY_TIMESTAMP;
				queryCreateCount.fetch_add(1, std::memory_order_relaxed);
				result = device->CreateQuery(&desc, &timestamp);
			}
			if (FAILED(result))
			{
				p.gpuQueryResult = result; ++p.gpuFailures; p.RetireQueries(Impl::GpuStatus::Failed);
				Initialization("gpu_queries", 0, result, false); return;
			}
		}
		p.gpuAvailable = true;
		Initialization("gpu_queries", 0, S_OK, true, kQuerySlots, kGpuInterval);
	}
	void PerformanceSession::GpuBegin(std::uint64_t frame) noexcept
	{
		GpuEnd(); auto& p = *impl_; p.PollGpu();
		p.gpuLoop = frame; p.gpuLoopEpoch = p.gpuEpoch; p.gpuLoopCandidate = false;
		p.gpuLoopHasDraw = false; p.gpuPassMask = 0;
	}
	void PerformanceSession::GpuWork(PerformanceGraphicsPass pass) noexcept
	{
		auto& p = *impl_;
		const auto passIndex = static_cast<std::uint32_t>(pass);
		if (passIndex >= static_cast<std::uint32_t>(PerformanceGraphicsPass::Count)) return;
		if (!p.gpuLoopCandidate)
		{
			p.gpuLoopCandidate = true; ++p.gpuWorkCandidates;
			const auto domainCandidate = p.frameOpen ? ++p.gpuEpochCandidates : ++p.gpuPriorEpochCandidates;
			if (p.frameOpen) ++p.gpuFrameCandidates; else ++p.gpuPriorCandidates;
			const auto firstLimit = p.frameOpen ? kGpuFirstWork : kGpuPriorFirstWork;
			const auto interval = p.frameOpen ? kGpuInterval : kGpuPriorInterval;
			const bool firstWork = domainCandidate <= firstLimit;
			const bool selected = firstWork || (domainCandidate - firstLimit) % interval == 0;
			if (!selected) return;
			++p.gpuSelected;
			if (p.frameOpen) ++p.gpuFrameSelected; else ++p.gpuPriorSelected;
			if (!p.gpuAvailable || (!p.context && !p.transport.begin)) return;
			if (p.gpuCount == p.capacity.gpu) { ++p.gpuCapacitySkips; ++p.gpuRingFull; return; }
			bool started = false;
			for (std::size_t i = 0; i < p.querySlots.size(); ++i)
			{
				auto& slot = p.querySlots[i]; if (slot.row != kMissing) continue;
				const auto row = p.gpuCount++; auto& value = p.gpu[row];
				value.frame = p.frameOpen ? p.current.serial : 0; value.epoch = p.gpuEpoch;
				value.loop = p.gpuLoop; value.candidate = p.gpuWorkCandidates; value.firstWork = firstWork;
				value.prior = !p.frameOpen; value.domainCandidate = domainCandidate;
				value.cpuBegin = Clock(); slot.row = row; slot.markers = 0; p.currentGpu = row;
				{ OverheadScope measured(p.queryIssueOverhead); p.QueryBegin(i); }
				started = true; break;
			}
			if (!started) { ++p.gpuSlotsBusy; ++p.gpuRingFull; return; }
		}
		if (p.currentGpu >= p.gpuCount) return;
		auto& value = p.gpu[p.currentGpu]; value.workMask |= 1u << passIndex;
		const auto marker = pass == PerformanceGraphicsPass::Geometry || pass == PerformanceGraphicsPass::Snapshot ? 0u :
			pass == PerformanceGraphicsPass::Composite ? 2u : pass == PerformanceGraphicsPass::Present ? 4u : 6u;
		if (marker < 6 && !(value.markers & (1u << marker)))
		{
			// 阶段原起点可能已过去；只补真正工作入口的截取标记，并公开clip mask。
			value.clipped |= 1u << marker; GpuMarker(marker);
		}
	}
	void PerformanceSession::GpuDraw(PerformanceGraphicsPass pass, std::uint32_t count) noexcept
	{
		if (!count) return;
		auto& p = *impl_;
		if (!p.gpuLoopHasDraw)
		{
			if (p.frameOpen) ++p.gpuDrawFrames; else ++p.gpuPriorDrawSegments;
			p.gpuLoopHasDraw = true;
		}
		if (p.currentGpu >= p.gpuCount) return;
		auto& row = p.gpu[p.currentGpu]; row.draws += count;
		if (pass == PerformanceGraphicsPass::Geometry || pass == PerformanceGraphicsPass::Snapshot) row.geometryDraws += count;
		if (pass == PerformanceGraphicsPass::Composite) row.compositeDraws += count;
	}
	void PerformanceSession::GpuMarker(std::uint32_t marker) noexcept
	{
		auto& p = *impl_;
		if (marker >= 6 || p.currentGpu == kMissing) return;
		for (std::size_t i = 0; i < p.querySlots.size(); ++i)
		{
			auto& slot = p.querySlots[i]; if (slot.row != p.currentGpu) continue;
			if (slot.markers & (1u << marker)) return;
			{ OverheadScope measured(p.queryIssueOverhead); p.QueryTimestamp(i, marker); }
			slot.markers |= 1u << marker; p.gpu[p.currentGpu].markers = slot.markers; return;
		}
	}
	void PerformanceSession::GpuEnd() noexcept
	{
		auto& p = *impl_;
		if (p.currentGpu == kMissing) return;
		for (std::size_t i = 0; i < p.querySlots.size(); ++i) if (p.querySlots[i].row == p.currentGpu)
		{
			{ OverheadScope measured(p.queryIssueOverhead); p.QueryEnd(i); }
			p.gpu[p.currentGpu].cpuEnd = Clock(); break;
		}
		p.currentGpu = kMissing;
	}
	void PerformanceSession::GpuStop() noexcept
	{
		GpuEnd(); auto& p = *impl_;
		// 退出只查询固定池一轮；未就绪按owner_stopped输出，不阻塞退出。
		for (std::size_t i = 0; i < p.querySlots.size(); ++i) p.PollGpu();
		p.RetireQueries(Impl::GpuStatus::Stopped); p.context.Reset(); p.device.Reset();
	}

	void PerformanceSession::ExportHost(bool startupFailed, bool runFailed, std::uint64_t legacyBytes, bool unavailable) noexcept
	{
		auto& p = *impl_;
		if (p.hostExported || p.output.empty()) return;
		p.hostExported = true;
		const auto exportStart = Clock();
		bool succeeded = false;
		const auto inputCount = static_cast<std::size_t>((std::min)(p.inputsSeen.load(), static_cast<std::uint64_t>(p.capacity.inputs)));
		try
		{
			const auto path = [&](const wchar_t* name) { return (std::filesystem::path(p.output) / name).wstring(); };
			bool filesSucceeded = true;
			{
				OutputFile f(path(L"frames.csv"));
				f.Text("frame_serial,start_qpc,end_qpc,present_return_qpc,active_chain,idle_boundary,physical_before,physical_after,width,height,reason_flags,command_count,down_count,consumed_count,terminal_count,tool_mask,dirty_left,dirty_top,dirty_right,dirty_bottom,present_attempted,present_api_attempted,present_succeeded,present_hresult,present_mode,output_target,output_revision,content_revision,full_present,thread_cpu_ms,control_qpc,ingress_qpc,model_reset_qpc,model_update_qpc,model_predict_qpc,geometry_qpc,snapshot_qpc,composite_qpc,upload_map_qpc,upload_copy_qpc,draw_submit_qpc,present_qpc,ulw_copy_submit_qpc,ulw_map_qpc,ulw_rows_qpc,ulw_update_qpc,deadline_wait_qpc,idle_wait_qpc,stage_observed_mask,stage_invalid_mask,upload_bytes,draw_calls,map_calls,map_failures,discard_maps,no_overwrite_maps,device_epoch,prior_ingress_qpc,prior_model_reset_qpc,prior_model_update_qpc,prior_model_predict_qpc,prior_upload_map_qpc,prior_upload_copy_qpc,prior_draw_submit_qpc,prior_stage_observed_mask,prior_stage_invalid_mask,prior_upload_bytes,prior_draw_calls,prior_map_calls,prior_down_count,prior_consumed_count,prior_tool_mask");
				for (const char* prefix : { "graphics_", "prior_graphics_" })
				{
					for (const char* field : { "leaf_qpc", "call_count", "invalid_count", "observed_mask", "invalid_mask" })
					{ f.Text(","); f.Text(prefix); f.Text(field); }
					for (const char* suffix : { "_qpc", "_count" }) for (const char* operation : kOperationNames)
					{ f.Text(","); f.Text(prefix); f.Text(operation); f.Text(suffix); }
				}
				f.Text("\r\n");
				for (std::size_t i = 0; i < p.frameCount; ++i)
				{
					const auto& v = p.frames[i]; const auto& a = v.facts;
					f.BeginRow();
					f.Field(v.serial); f.Field(v.start); f.Field(v.end); f.Field(v.returned); f.Field(v.chain); f.Field(v.idle);
					f.Field(a.physicalBefore); f.Field(a.physicalAfter); f.Field(a.width); f.Field(a.height);
					f.Field(a.reasons); f.Field(a.commands); f.Field(a.downs); f.Field(a.consumed); f.Field(a.terminal); f.Field(a.tools);
					f.Field(v.dirty.left); f.Field(v.dirty.top); f.Field(v.dirty.right); f.Field(v.dirty.bottom);
					f.Field(v.attempted); f.Field(v.apiAttempted); f.Field(v.succeeded); f.Field(static_cast<std::int32_t>(v.result));
					f.Field(v.mode); f.Field(v.target); f.Field(v.outputRevision); f.Field(v.contentRevision); f.Field(v.full);
					if (a.threadCpuMs >= 0) f.Field(a.threadCpuMs); else f.Field("");
					for (auto ticks : v.stages) f.Field(ticks);
					f.Field(v.observed); f.Field(v.invalid); f.Field(v.uploadBytes); f.Field(v.drawCalls);
					f.Field(v.mapCalls); f.Field(v.mapFailures); f.Field(v.discard); f.Field(v.noOverwrite); f.Field(v.epoch);
					for (auto index : { 1, 2, 3, 4, 8, 9, 10 }) f.Field(v.priorStages[index]);
					f.Field(v.priorObserved); f.Field(v.priorInvalid); f.Field(v.priorUploadBytes); f.Field(v.priorDrawCalls); f.Field(v.priorMapCalls);
					f.Field(v.priorDowns); f.Field(v.priorConsumed); f.Field(v.priorTools);
					for (const auto* summary : { &v.graphics, &v.priorGraphics })
					{
						std::int64_t total = 0; std::uint64_t count = 0; bool valid = true;
						for (auto ticks : summary->ticks) if (!AddTicks(total, ticks)) valid = false;
						for (auto calls : summary->counts) count += calls;
						if (valid) f.Field(total); else f.Field("");
						f.Field(count); f.Field(summary->invalidCalls); f.Field(summary->observed); f.Field(summary->invalid);
						for (auto ticks : summary->ticks) f.Field(ticks);
						for (auto calls : summary->counts) f.Field(calls);
					}
					f.EndRow();
				}
				filesSucceeded &= f.Finish();
			}
			{
				// 一次hash join；不做逐callback扫全部receipt，保留原始前缀和完整分母。
				std::unordered_map<PerformanceInputKey, const Impl::Receipt*, KeyHash> receipts;
				receipts.reserve(p.receiptCount);
				for (std::size_t i = 0; i < p.receiptCount; ++i) receipts.try_emplace(p.receipts[i].key, &p.receipts[i]);
				std::unordered_map<std::uintptr_t, std::uint64_t> anonymous;
				anonymous.reserve(4096);
				OutputFile f(path(L"input-events.csv"));
				f.Text("callback_token,phase,tablet_context_id,contact_id,packet_count,device_type,callback_entry_qpc,source_qpc,published_qpc,callback_end_qpc,publication_succeeded,callback_outcome,record_id,generation,sequence,admission_revision,consume_qpc,process_end_qpc,adopt_qpc,raster_qpc,stored_qpc,present_return_qpc,frame_serial,workspace_id,page_id,scene_generation,raster_generation,output_generation,output_target,output_revision,content_token,item_token,proof_kind,proof_supported,proof_valid,outcome,effective_tool\r\n");
				for (std::size_t i = 0; i < inputCount; ++i)
				{
					const auto& v = p.inputs[i]; const auto match = receipts.find(v.key);
					const Impl::Receipt* r = match != receipts.end() && match->second->source == v.source &&
						match->second->admission == v.admission ? match->second : nullptr;
					std::uint64_t recordId = 0;
					if (v.key.record) recordId = anonymous.try_emplace(v.key.record, anonymous.size() + 1).first->second;
					const PerformanceProof proof = r ? r->proof : PerformanceProof{};
					f.Row(i + 1, v.phase, v.tablet, v.contact, v.packets, v.device, v.entry, v.source, v.published, v.end,
						v.publicationSucceeded, CallbackName(v.outcome), recordId, v.key.generation, v.key.sequence, v.admission,
						r ? r->consume : 0, r ? r->processEnd : 0, r ? r->adopt : 0, r ? r->raster : 0, r ? r->stored : 0,
						r ? r->returned : 0, proof.frame, proof.workspace, proof.page, proof.scene, proof.raster, proof.output,
						proof.target, proof.rawOutputRevision, proof.content, proof.item, proof.kind,
						r && r->supported, r && r->confirmed && v.publicationSucceeded, p.Outcome(r, v), r ? r->tool : UINT32_MAX);
				}
				filesSucceeded &= f.Finish();
			}
			for (bool slow : { false, true })
			{
				OutputFile f(path(slow ? L"slow-graphics-calls.csv" : L"graphics-calls.csv"));
				f.Text("call_serial,frame_serial,device_epoch,resource_id,related_resource_id,site_id,operation,resource_role,pass,start_qpc,end_qpc,valid_interval,hresult_available,hresult,subresource,map_type,map_flags,offset_bytes,size_bytes,argument0,argument1,parent_call_serial,attribution,accounting_valid,resource_available,depth\r\n");
				const auto count = slow ? p.slowCount : p.graphicsCount;
				const auto* calls = slow ? p.slowCalls.get() : p.graphicsCalls.get();
				for (std::size_t i = 0; i < count; ++i)
				{
					const auto& v = calls[i]; f.BeginRow();
					f.Field(v.serial); f.Field(v.frame); f.Field(v.epoch); f.Field(v.resource); f.Field(v.relatedResource); f.Field(v.site);
					f.Field(EnumName(kOperationNames, v.operation)); f.Field(EnumName(kRoleNames, v.role)); f.Field(EnumName(kPassNames, v.pass));
					f.Field(v.start); f.Field(v.end); f.Field((v.accounting & kCallTimeValid) != 0); f.Field((v.accounting & kCallHasResult) != 0);
					if (v.accounting & kCallHasResult) f.Field(static_cast<std::int32_t>(v.result)); else f.Field("");
					f.Field(v.subresource); f.Field(v.mapType); f.Field(v.flags); f.Field(v.offset); f.Field(v.bytes);
					f.Field(v.argument0); f.Field(v.argument1); f.Field(v.parent); f.Field(EnumName(kAttributionNames, v.accounting >> 16));
					f.Field((v.accounting & kCallAccountingValid) != 0); f.Field((v.accounting & kCallResourceKnown) != 0); f.Field(v.depth); f.EndRow();
				}
				filesSucceeded &= f.Finish();
			}
			{
				OutputFile f(path(L"graphics-groups.csv"));
				f.Text("group_id,site_id,operation,resource_role,pass,attribution,calls_seen,valid_calls,invalid_calls,failed_calls,hresult_calls,size_bytes,total_qpc,min_qpc,max_qpc,max_call_serial,max_frame_serial,arithmetic_overflow\r\n");
				for (std::size_t i = 0; i < p.capacity.graphicsGroups; ++i)
				{
					const auto& v = p.graphicsGroups[i]; if (!v.used) continue;
					f.BeginRow(); f.Field(i + 1); f.Field(v.site); f.Field(EnumName(kOperationNames, v.operation));
					f.Field(EnumName(kRoleNames, v.role)); f.Field(EnumName(kPassNames, v.pass)); f.Field(EnumName(kAttributionNames, v.attribution));
					f.Field(v.seen); f.Field(v.valid); f.Field(v.invalid); f.Field(v.failed); f.Field(v.resultCalls); f.Field(v.bytes); f.Field(v.total);
					if (v.valid) { f.Field(v.minimum); f.Field(v.maximum); } else { f.Field(""); f.Field(""); }
					f.Field(v.maxSerial); f.Field(v.maxFrame); f.Field(v.overflow); f.EndRow();
				}
				filesSucceeded &= f.Finish();
			}
			{
				OutputFile f(path(L"resources.csv"));
				f.Text("resource_id,device_epoch,resource_kind,resource_role,site_id,descriptor_available,byte_width,width,height,format,usage,bind_flags,cpu_access_flags,misc_flags,structure_byte_stride,created_qpc,destroyed_qpc,create_hresult\r\n");
				for (std::size_t i = 0; i < p.resourceCount; ++i)
				{
					const auto& v = p.resources[i];
					f.Row(v.id, v.epoch, EnumName(kResourceKinds, v.kind), EnumName(kRoleNames, v.role), v.site, v.descriptor,
						v.bytes, v.width, v.height, v.format, v.usage, v.bind, v.cpu, v.misc, v.stride, v.created, v.destroyed,
						static_cast<std::int32_t>(v.result));
				}
				filesSucceeded &= f.Finish();
			}
			{
				OutputFile f(path(L"gpu.csv"));
				f.Text("frame_serial,device_epoch,status,frequency,geometry_begin,geometry_end,composite_begin,composite_end,readback_begin,readback_end,hresult,sample_id,loop_serial,candidate_index,sampling_reason,marker_observed_mask,draw_calls_in_sample,geometry_draw_calls,composite_draw_calls,readback_calls,work_mask,cpu_begin_qpc,cpu_end_qpc,frame_retained,marker_clipped_mask,sampling_domain,domain_candidate_index\r\n");
				const char* statuses[] = { "pending", "ready", "disjoint", "query_failed", "invalid", "owner_stopped", "device_replaced" };
				for (std::size_t i = 0; i < p.gpuCount; ++i)
				{
					const auto& v = p.gpu[i];
					bool retained = false;
					for (std::size_t j = 0; j < p.frameCount; ++j)
						if (p.frames[j].serial == v.frame && p.frames[j].epoch == v.epoch) { retained = true; break; }
					f.Row(v.frame, v.epoch, statuses[static_cast<std::size_t>(v.status)], v.frequency, v.timestamps[0], v.timestamps[1],
						v.timestamps[2], v.timestamps[3], v.timestamps[4], v.timestamps[5], static_cast<std::int32_t>(v.result),
						i + 1, v.loop, v.candidate, v.firstWork ? "first_work" : "periodic_work", v.markers,
						v.draws, v.geometryDraws, v.compositeDraws, v.readbacks, v.workMask, v.cpuBegin, v.cpuEnd, retained, v.clipped,
						v.prior ? "prior" : "frame", v.domainCandidate);
				}
				filesSucceeded &= f.Finish();
			}
			{
				Json::Value init;
				init["schema_version"] = 1; init["events"] = Json::Value(Json::arrayValue);
				for (std::size_t i = 0; i < p.initCount; ++i)
				{
					const auto& v = p.initialization[i]; Json::Value event;
					event["stage"] = v.stage ? v.stage : "unknown"; event["start_qpc"] = Json::Int64(v.start);
					event["end_qpc"] = Json::Int64(v.end); event["hresult"] = static_cast<std::int32_t>(v.result);
					event["succeeded"] = v.succeeded;
					for (std::size_t j = 0; j < v.detail.size(); ++j) event["detail" + std::to_string(j)] = U64(v.detail[j]);
					init["events"].append(event);
				}
				init["actual_driver_type"] = p.driver; init["feature_level"] = p.feature;
				init["adapter_available"] = p.adapterAvailable;
				init["adapter_description"] = p.adapterAvailable ? Utf8(p.adapter.Description) : "";
				init["vendor_id"] = p.adapter.VendorId; init["device_id"] = p.adapter.DeviceId;
				init["subsys_id"] = p.adapter.SubSysId; init["revision"] = p.adapter.Revision;
				init["dedicated_video_memory"] = U64(p.adapter.DedicatedVideoMemory);
				init["dedicated_system_memory"] = U64(p.adapter.DedicatedSystemMemory);
				init["shared_system_memory"] = U64(p.adapter.SharedSystemMemory);
				init["driver_version_available"] = SUCCEEDED(p.driverVersionResult);
				init["driver_version"] = U64(static_cast<std::uint64_t>(p.driverVersion.QuadPart));
				init["driver_version_hresult"] = static_cast<std::int32_t>(p.driverVersionResult);
				init["presenter_mode"] = p.presenter; init["width"] = p.width; init["height"] = p.height;
				init["map_no_overwrite_supported"] = p.noOverwriteSupported;
				init["model_dpi_scale"] = p.modelDpiScale; init["model_dpi"] = p.modelDpiScale * 96.0;
				init["target_fps"] = p.targetFps; init["model_configuration_source"] = "actual_Draw3_StrokeModelConfiguration";
				init["target_effective_dpi"] = p.targetDpi; init["display_snapshot_generation"] = U64(p.displayGeneration);
				init["display_topology"] = p.displayTopology; init["display_pixel_width"] = p.displayWidth; init["display_pixel_height"] = p.displayHeight;
				init["display_source"] = "Host_resolved_Display_snapshot_penWidthContext";
				init["target_refresh_rate_available"] = false; init["target_refresh_rate_reason"] = "not_measured_by_Draw3";
				filesSucceeded &= JsonFile(path(L"initialization.json"), init);
			}
			Json::Value session;
			session["schema_version"] = 2; session["qpc_frequency"] = Json::Int64(Frequency());
			session["diagnostic_package_version"] = 2;
			for (const char* file : { "session.json", "frames.csv", "gpu.csv" }) session["raw_schemas"][file] = 2;
			for (const char* file : { "initialization.json", "input-events.csv", "graphics-calls.csv", "slow-graphics-calls.csv", "graphics-groups.csv", "resources.csv" })
				session["raw_schemas"][file] = 1;
			session["capabilities"] = Json::Value(Json::arrayValue);
			for (const char* capability : { "graphics_leaf_calls", "resource_identity", "bounded_slow_calls", "all_call_groups", "graphics_work_gpu_sampling" })
				session["capabilities"].append(capability);
			session["status"] = unavailable || Frequency() <= 0 ? "unavailable" : startupFailed ? "startup_failed" : runFailed ? "run_failed" : "complete";
			session["unavailable"] = unavailable || Frequency() <= 0;
			session["startup_failed"] = startupFailed; session["run_failed"] = runFailed;
			session["payload_budget_bytes"] = U64(kPayloadBudget); session["allocated_payload_bytes"] = U64(p.allocated);
			session["legacy_payload_upper_bound_bytes"] = U64(legacyBytes);
			session["payload_contract_satisfied"] = p.allocated <= kPayloadBudget && legacyBytes <= 64u * 1024 * 1024 - p.allocated;
			const auto component = [&](const char* name, std::size_t size, std::size_t count)
			{
				auto& value = session["payload_components"][name]; value["row_bytes"] = U64(size);
				value["capacity"] = U64(count); value["bytes"] = U64(size * count);
			};
			component("impl", sizeof(Impl), 1); component("frames", sizeof(Impl::Frame), p.capacity.frames);
			component("inputs", sizeof(Impl::Input), p.capacity.inputs); component("receipts", sizeof(Impl::Receipt), p.capacity.receipts);
			component("gpu", sizeof(Impl::Gpu), p.capacity.gpu);
			component("graphics_calls", sizeof(PerformanceGraphicsToken), p.capacity.graphicsCalls);
			component("slow_graphics_calls", sizeof(PerformanceGraphicsToken), p.capacity.slowCalls);
			component("graphics_groups", sizeof(Impl::GraphicsGroup), p.capacity.graphicsGroups);
			component("resources", sizeof(Impl::Resource), p.capacity.resources);
			session["payload_boundary"] = "fixed_POD_and_Impl_only_export_JSON_join_buffers_and_driver_query_allocation_excluded_not_RSS";
			session["frames_seen"] = U64(p.framesSeen); session["frames_retained"] = U64(p.frameCount);
			session["frames_dropped"] = U64(p.framesSeen - p.frameCount);
			session["inputs_seen"] = U64(p.inputsSeen.load()); session["inputs_retained"] = U64(inputCount);
			session["inputs_dropped"] = U64(p.inputsSeen.load() - inputCount);
			session["receipts_seen"] = U64(p.receiptsSeen); session["receipts_retained"] = U64(p.receiptCount);
			session["receipts_dropped"] = U64(p.receiptsSeen - p.receiptCount);
			session["invalid_intervals"] = U64(p.invalidIntervals); session["proof_overflow"] = U64(p.proofOverflow);
			session["callbacks_bundled_packets"] = U64(p.bundledPackets.load());
			session["graphics_call_capacity"] = U64(p.capacity.graphicsCalls);
			session["slow_graphics_call_capacity"] = U64(p.capacity.slowCalls);
			session["graphics_group_capacity"] = U64(p.capacity.graphicsGroups);
			session["resource_capacity"] = U64(p.capacity.resources);
			session["graphics_calls_seen"] = U64(p.graphicsSeen); session["graphics_calls_retained"] = U64(p.graphicsCount);
			session["graphics_calls_dropped"] = U64(p.graphicsSeen - p.graphicsCount);
			session["slow_graphics_calls_seen"] = U64(p.slowSeen); session["slow_graphics_calls_retained"] = U64(p.slowCount);
			session["slow_graphics_calls_dropped"] = U64(p.slowSeen - p.slowCount);
			session["graphics_groups_retained"] = U64(p.groupCount); session["graphics_group_calls_dropped"] = U64(p.groupCallsDropped);
			session["resources_seen"] = U64(p.resourcesSeen); session["resources_retained"] = U64(p.resourceCount);
			session["resources_dropped"] = U64(p.resourcesSeen - p.resourceCount);
			session["graphics_invalid_intervals"] = U64(p.graphicsInvalid); session["graphics_nested_calls"] = U64(p.graphicsNested);
			session["graphics_unknown_resources"] = U64(p.unknownResources); session["graphics_site_collisions"] = U64(p.siteCollisions);
			session["graphics_cross_frame_calls"] = U64(p.graphicsCrossFrame); session["graphics_overlap_calls"] = U64(p.graphicsOverlaps);
			session["graphics_group_overflows"] = U64(p.groupOverflows); session["graphics_arithmetic_overflows"] = U64(p.arithmeticOverflows);
			session["first_retained_call_qpc"] = Json::Int64(p.graphicsCount ? p.graphicsCalls[0].start : 0);
			session["last_retained_call_qpc"] = Json::Int64(p.graphicsCount ? p.graphicsCalls[p.graphicsCount - 1].end : 0);
			session["graphics_detail_dropped_after_qpc"] = Json::Int64(p.detailDroppedAfter);
			session["graphics_detail_policy"] = "prefix_plus_independent_slow_prefix";
			session["graphics_slow_threshold_qpc"] = Json::Int64(p.slowThreshold); session["graphics_slow_threshold_ms"] = 1;
			session["graphics_accounting"] = "non_nested_leaf_intervals_split_at_frame_start";
			session["graphics_residual_boundary"] = "frame_work_minus_valid_leaf_sum_only_when_masks_valid_not_driver_exclusive_cost";
			session["graphics_detail_complete"] = filesSucceeded && p.GraphicsDetailComplete();
			for (std::size_t i = 0; i < std::size(kOperationNames); ++i) session["graphics_operations"][kOperationNames[i]] = static_cast<Json::UInt>(i);
			for (std::size_t i = 0; i < std::size(kRoleNames); ++i) session["graphics_resource_roles"][kRoleNames[i]] = static_cast<Json::UInt>(i);
			for (std::size_t i = 0; i < std::size(kPassNames); ++i) session["graphics_passes"][kPassNames[i]] = static_cast<Json::UInt>(i);
			session["graphics_sites"] = Json::Value(Json::arrayValue);
			for (std::size_t i = 0; i < p.siteCount; ++i)
			{
				Json::Value site; site["site_id"] = p.sites[i].id; site["name"] = p.sites[i].name;
				session["graphics_sites"].append(site);
			}
			session["gpu_sampling_interval"] = U64(kGpuInterval);
			session["gpu_first_work_samples_per_device_epoch"] = U64(kGpuFirstWork);
			session["gpu_work_candidates"] = U64(p.gpuWorkCandidates); session["gpu_candidates_selected"] = U64(p.gpuSelected);
			session["gpu_samples_started"] = U64(p.gpuCount); session["gpu_slots_busy_skips"] = U64(p.gpuSlotsBusy);
			session["gpu_capacity_skips"] = U64(p.gpuCapacitySkips); session["gpu_query_failures"] = U64(p.gpuFailures);
			session["gpu_draw_frames_seen"] = U64(p.gpuDrawFrames); session["gpu_pending_at_device_replace"] = U64(p.gpuPendingReplaced);
			session["gpu_prior_draw_segments"] = U64(p.gpuPriorDrawSegments); session["gpu_prior_work_candidates"] = U64(p.gpuPriorCandidates);
			session["gpu_frame_work_candidates"] = U64(p.gpuFrameCandidates); session["gpu_frame_candidates_selected"] = U64(p.gpuFrameSelected);
			session["gpu_prior_candidates_selected"] = U64(p.gpuPriorSelected);
			session["gpu_prior_sampling_interval"] = U64(kGpuPriorInterval); session["gpu_prior_first_work_samples_per_device_epoch"] = U64(kGpuPriorFirstWork);
			session["gpu_sampling_domain_policy"] = "frame_first8_every8_and_prior_first4_every16_independent_per_device_epoch_prior_never_consumes_frame_sequence";
			// prior 的真实绘制证据单列，不能扩大帧内 GPU 覆盖率。
			session["gpu_ready_draw_samples"] = U64(p.ReadyDrawSamples(false));
			session["gpu_ready_prior_draw_samples"] = U64(p.ReadyDrawSamples(true));
			session["gpu_sampling_boundary"] = "actual_graphics_work_candidates_first8_then_every8_lazy_clipped_markers_immediate_command_stream_not_shader_busy";
			const auto overhead = [&](const char* name, const Overhead& value)
			{
				auto& row = session["diagnostic_overhead_counters"][name]; row["total_qpc"] = Json::Int64(value.total);
				row["max_qpc"] = Json::Int64(value.maximum); row["count"] = U64(value.count); row["invalid_intervals"] = U64(value.invalid);
			};
			overhead("metadata", p.metadataOverhead); overhead("bookkeeping", p.bookkeepingOverhead);
			overhead("gpu_query_issue", p.queryIssueOverhead); overhead("gpu_query_poll", p.queryPollOverhead);
			session["diagnostic_overhead_relationship"] = "metadata_includes_lazy_gpu_issue_do_not_sum_nested_counters_no_per_call_GetThreadTimes_or_cycles_clock";
			session["gpu_status"] = p.gpuQueryResult == S_OK ? "supported" : "unavailable";
			session["gpu_query_hresult"] = static_cast<std::int32_t>(p.gpuQueryResult);
			session["gpu_ring_full"] = U64(p.gpuRingFull); session["gpu_pending_at_stop"] = U64(p.gpuPendingStop);
			session["publication_clock_boundary"] = "before_release_commit";
			session["latency_boundary"] = "real_content_software_present_return";
			session["stage_relationship"] = "inclusive_nested_do_not_sum";
			session["disabled_path_contract"] = "null_session_no_new_clock_statistics_allocation_io_or_gpu_query";
			session["diagnostic_overhead"] = "legacy_runtime_metrics_QPC_and_GetThreadTimes_per_model_call_plus_leaf_endpoint_metadata_bookkeeping_QPC_and_bounded_queries";
			session["prior_work_relationship"] = "prior_QPC_stages_before_frame_start_excluded_from_frame_work_and_owner_thread_cpu";
			session["process_span_relationship"] = "consume_to_process_end_contains_adopt_do_not_chain_process_end_to_adopt";
			session["frame_capacity"] = U64(p.capacity.frames); session["input_capacity"] = U64(p.capacity.inputs);
			session["receipt_capacity"] = U64(p.capacity.receipts);
			session["first_frame_qpc"] = Json::Int64(p.frameCount ? p.frames[0].start : 0);
			session["last_retained_frame_qpc"] = Json::Int64(p.frameCount ? p.frames[p.frameCount - 1].end : 0);
			session["first_input_qpc"] = Json::Int64(inputCount ? p.inputs[0].entry : 0);
			session["last_retained_input_qpc"] = Json::Int64(inputCount ? p.inputs[inputCount - 1].end : 0);
			session["export_start_qpc"] = Json::Int64(exportStart); session["export_end_qpc"] = Json::Int64(Clock());
			session["exported"] = filesSucceeded;
			succeeded = JsonFile(path(L"session.json"), session) && filesSucceeded;
		}
		catch (...) { succeeded = false; }
		{
			std::scoped_lock lock(exportMutex);
			if (succeeded) ++exportStatus.exportedHostRuns; else ++exportStatus.failedHostRuns;
			if (startupFailed) ++exportStatus.startupFailedHostRuns;
			if (runFailed) ++exportStatus.runFailedHostRuns;
			if (unavailable || Frequency() <= 0) ++exportStatus.unavailableHostRuns;
			exportStatus.framesSeen += p.framesSeen; exportStatus.framesRetained += p.frameCount;
			exportStatus.framesDropped += p.framesSeen - p.frameCount;
			exportStatus.inputsSeen += p.inputsSeen.load(); exportStatus.inputsRetained += inputCount;
			exportStatus.inputsDropped += p.inputsSeen.load() - inputCount;
			exportStatus.receiptsSeen += p.receiptsSeen; exportStatus.receiptsRetained += p.receiptCount;
			exportStatus.receiptsDropped += p.receiptsSeen - p.receiptCount; exportStatus.proofOverflow += p.proofOverflow;
			exportStatus.graphicsCallsSeen += p.graphicsSeen; exportStatus.graphicsCallsRetained += p.graphicsCount;
			exportStatus.graphicsCallsDropped += p.graphicsSeen - p.graphicsCount;
			exportStatus.slowGraphicsCallsSeen += p.slowSeen; exportStatus.slowGraphicsCallsRetained += p.slowCount;
			exportStatus.slowGraphicsCallsDropped += p.slowSeen - p.slowCount;
			exportStatus.resourcesSeen += p.resourcesSeen; exportStatus.resourcesRetained += p.resourceCount;
			exportStatus.resourcesDropped += p.resourcesSeen - p.resourceCount; exportStatus.graphicsGroupCallsDropped += p.groupCallsDropped;
			exportStatus.graphicsSiteCollisions += p.siteCollisions;
			exportStatus.payloadBudgetBytes = (std::max)(exportStatus.payloadBudgetBytes, static_cast<std::uint64_t>(p.allocated + legacyBytes));
		}
	}


	int RunPerformanceRecorderProbe() noexcept
	{
		int failures = 0;
		const auto check = [&](bool value, const char* name)
		{ if (!value) { ++failures; std::fprintf(stderr, "[Draw3.Performance.Test] FAIL %s\n", name); } };
		try
		{
			struct TestClock { std::int64_t tick = 100, calls = 0; } clock;
			const PerformanceClock injected{ &clock, [](void* raw) noexcept
				{ auto& c = *static_cast<TestClock*>(raw); ++c.calls; return ++c.tick; }, 1000 };
			PerformanceSession session({ 2, 2, 4, 1 }, injected);
			const auto allocations = allocationCount.load(), queries = queryCreateCount.load();
			const auto calls = clock.calls;
			{ PerformanceScope off(nullptr, PerformanceStage::ModelUpdate); PerformanceInputScope input(nullptr, 0, 1, 1, 1); }
			check(!PerformanceSession::CreateHost(L"") && clock.calls == calls && allocationCount.load() == allocations &&
				queryCreateCount.load() == queries, "disabled no clock/allocation/query");
			int disabledMetadata = 0, disabledCalls = 0;
			const HRESULT disabledResult = PerformanceGraphicsCall(nullptr, PerformanceGraphicsOperation::Map,
				PerformanceGraphicsRole::GlobalConstants, "probe_off_hresult", [&]
				{ ++disabledMetadata; return PerformanceGraphicsArguments{}; }, [&] { ++disabledCalls; return E_OUTOFMEMORY; });
			PerformanceGraphicsVoid(nullptr, PerformanceGraphicsOperation::Unmap, PerformanceGraphicsRole::GlobalConstants,
				"probe_off_void", [&] { ++disabledMetadata; return PerformanceGraphicsArguments{}; }, [&] { ++disabledCalls; });
			check(disabledResult == E_OUTOFMEMORY && disabledCalls == 2 && disabledMetadata == 0 && clock.calls == calls &&
				allocationCount.load() == allocations && queryCreateCount.load() == queries, "disabled lazy builders and exactly once original calls");
			session.BeginFrame(1, 100, 1);
			session.Stage(PerformanceStage::Control, 100, 120);
			session.Stage(PerformanceStage::ModelUpdate, 110, 115);
			session.Stage(PerformanceStage::Control, 120, 119);
			check(session.impl_->current.stages[0] == 20 && session.impl_->current.stages[3] == 5 &&
				session.impl_->current.invalid == 1 && session.impl_->invalidIntervals == 1, "QPC nesting and invalid");
			const PerformanceInputKey key{ 10, 4, 2 };
			auto* input = session.BeginInput(0, 1, 2, 3);
			session.Publication(input, key, 100, 6, 0, 1); session.PublicationResult(input, true);
			session.EndInput(input, PerformanceCallbackOutcome::Unknown);
			session.Consume(key, 100, 6, 0, true); session.ProcessEnd(key);
			const PerformanceProof proof{ 1, 2, 3, 4, 5, 8, 0, 1, 0, 1, 0 };
			session.StageProof(key, proof);
			check(!session.Confirm(key, proof, 200, true, true), "consume cannot prove");
			session.Adopt(key, 8); session.Raster(key, 1, true); session.StageProof(key, proof);
			check(!session.Confirm(key, proof, 200, true, false), "failed Present");
			auto wrong = proof; ++wrong.output;
			check(!session.Confirm(key, wrong, 200, true, true), "old output");
			auto old = key; ++old.generation;
			check(!session.Confirm(old, proof, 200, true, true), "old generation");
			old = key; ++old.sequence;
			check(!session.Confirm(old, proof, 200, true, true), "wrong sequence");
			session.PresentResult(true, false, S_OK, 200, {}, true, 1, 0, 0, 8);
			check(!session.LastPresentApiSucceeded(), "no-op Present");
			session.PresentResult(true, true, DXGI_STATUS_OCCLUDED, 200, {}, true, 1, 0, 0, 8);
			check(!session.LastPresentApiSucceeded(), "occluded Present");
			check(session.Confirm(key, proof, 200, true, true) && !session.Confirm(key, proof, 201, true, true), "proof exactly once");
			session.Invalidate(key.record, key.generation, 1);
			auto up = key; up.sequence = 4;
			session.Consume(up, 100, 6, 2, true); session.Stored(up, 9);
			auto stored = proof; stored.kind = 2; stored.content = 9; stored.item = 11; stored.frame = 2;
			session.StageProof(up, stored);
			check(session.Confirm(up, stored, 250, true, true), "Up stored authority");
			session.EndFrame(260, true, {});
			session.IdleBoundary(); session.BeginFrame(2, 400, 1); session.EndFrame(410, true, {});
			session.BeginFrame(3, 500, 1); session.EndFrame(510, true, {});
			session.BeginInput(1, 1, 2, 1); check(!session.BeginInput(2, 1, 2, 1), "input capacity");
			check(session.impl_->framesSeen == 3 && session.impl_->frameCount == 2 &&
				session.impl_->inputsSeen == 3 && session.impl_->frames[0].idle && session.impl_->frames[1].chain == 2 &&
				session.impl_->inputs[0].key == key && session.AllocatedBytes() <= kPayloadBudget, "prefix retention and idle");
			session.PresentResult(true, false, S_FALSE, 0, {}, true, 0, 0, 1, 1);
			session.PresentApiResult(E_FAIL, true); session.PresentApiResult(E_FAIL, false);
			check(session.impl_->current.apiAttempted && !session.LastPresentApiSucceeded(), "ULW failed API attempt survives failure reporting");
			PerformanceSession boundary({ 2, 2, 4, 1 }, injected);
			boundary.BeginFrame(9, 200, 1); boundary.Stage(PerformanceStage::Ingress, 180, 220); boundary.EndFrame(240, true, {});
			check(boundary.PriorStageTicks(9, PerformanceStage::Ingress) == 20 && boundary.impl_->frames[0].stages[1] == 20, "Ingress across actual Begin QPC split");
			clock.tick = 400; boundary.Consume({ 30, 1, 2 }, 100, 1, 0, true); clock.tick = 300; boundary.Adopt({ 30, 1, 2 }, 8);
			boundary.Raster({ 30, 1, 2 }, 9, true); auto reversed = proof; reversed.frame = 9; boundary.StageProof({ 30, 1, 2 }, reversed);
			check(!boundary.Confirm({ 30, 1, 2 }, reversed, 500, true, true) && boundary.impl_->invalidIntervals > 0, "backwards adopted QPC rejects software proof");
			PerformanceSession recovery({ 2, 1, 1, 1 }, injected);
			recovery.impl_->AdvanceDeviceEpoch(); recovery.BeginFrame(10, 500, 1); recovery.EndFrame(510, true, {});
			recovery.BeginFrame(11, 600, 1); recovery.impl_->AdvanceDeviceEpoch();
			recovery.PresentResult(true, true, S_OK, 620, {}, true, 1, 0, 1, 1); recovery.EndFrame(630, true, {});
			check(recovery.impl_->frames[0].epoch == 1 && recovery.impl_->frames[1].epoch == 2 &&
				recovery.impl_->frames[1].succeeded && queryCreateCount.load() == queries, "actual cold device transition updates open recovery frame without D3D");
			recovery.CaptureUploadCapability(false, E_FAIL);
			check(recovery.impl_->initCount == 1 && recovery.impl_->initialization[0].start == 0 &&
				recovery.impl_->initialization[0].result == E_FAIL, "post-call initialization has result but no invented start");
			PerformanceSession heldIdle({ 2, 1, 1, 1 }, injected);
			PerformanceFrameFacts heldFacts; heldFacts.physicalBefore = 1; heldFacts.physicalAfter = 1; heldFacts.tools = 1u << 3;
			heldIdle.BeginFrame(20, 600, 1); heldIdle.EndFrame(610, true, heldFacts); heldIdle.IdleBoundary();
			clock.tick = 610; PerformanceCall(&heldIdle, PerformanceStage::IdleWait, [&] { clock.tick = 3610; });
			heldIdle.BeginFrame(21, 3620, 1); heldIdle.EndFrame(3630, true, heldFacts);
			const auto& idleFrame = heldIdle.impl_->frames[0]; const auto& resumedFrame = heldIdle.impl_->frames[1];
			check(idleFrame.idle && idleFrame.facts.physicalAfter == 1 && idleFrame.end - idleFrame.start == 10 &&
				idleFrame.stages[static_cast<std::size_t>(PerformanceStage::IdleWait)] == 3000 && resumedFrame.chain != idleFrame.chain &&
				resumedFrame.facts.physicalBefore == 1, "held eraser idle wait stays outside work and breaks active interval");
			PerformanceSession concurrent({ 1, 256, 1, 1 });
			std::array<std::thread, 4> producers;
			for (std::uint32_t t = 0; t < producers.size(); ++t) producers[t] = std::thread([&, t]
			{
				for (std::uint32_t n = 0; n < 128; ++n)
				{
					PerformanceInputScope input(&concurrent, 1, t, n, 2);
					if (auto* trace = input.Trace())
					{
						trace->publication(trace, { t * 256u + n + 1, 1, 2 }, concurrent.Clock(), 1, 1, 1);
						trace->result(trace, true);
					}
				}
			});
			for (auto& producer : producers) producer.join();
			bool unique = true;
			for (std::size_t i = 0; i < concurrent.impl_->capacity.inputs; ++i)
			{
				const auto& row = concurrent.impl_->inputs[i];
				unique &= row.trace.index == i && row.publicationSucceeded && row.key.sequence == 2 && row.end >= row.entry;
			}
			check(unique && concurrent.impl_->inputsSeen == 512, "concurrent callback unique prefix and exact capacity drops");
			const PerformanceClock precise{ &clock, injected.read, 1000000 };
			const PerformanceCapacities probeCapacities{ 3, 1, 1, 16, 16, 4, 16, 3 };
			const auto measuredCall = [&](PerformanceSession& target, PerformanceGraphicsOperation operation,
				PerformanceGraphicsRole role, PerformanceGraphicsSite site, std::int64_t start, std::int64_t end,
				bool available = false, HRESULT result = S_FALSE)
			{
				clock.tick = start - 2;
				auto call = target.BeginGraphicsCall(operation, role, site);
				clock.tick = end - 1; target.EndGraphicsCall(call, available, result);
			};
			PerformanceSession detailed({ 1, 1, 1, 1, 2, 2, 2, 2 }, precise);
			detailed.BeginFrame(100, 1000, 0);
			measuredCall(detailed, PerformanceGraphicsOperation::Map, PerformanceGraphicsRole::InkData, "probe_map", 1010, 1020, true, E_FAIL);
			measuredCall(detailed, PerformanceGraphicsOperation::WriteConstant, PerformanceGraphicsRole::GlobalConstants, "probe_constant", 1030, 1040);
			measuredCall(detailed, PerformanceGraphicsOperation::Map, PerformanceGraphicsRole::InkData, "probe_map", 1050, 3050, true, S_OK);
			measuredCall(detailed, PerformanceGraphicsOperation::Map, PerformanceGraphicsRole::InkData, "probe_map", 3060, 6060, true, S_OK);
			measuredCall(detailed, PerformanceGraphicsOperation::Map, PerformanceGraphicsRole::InkData, "probe_map", 6070, 10070, true, S_OK);
			measuredCall(detailed, PerformanceGraphicsOperation::Clear, PerformanceGraphicsRole::LayerTexture, "probe_clear", 10080, 10085);
			detailed.EndFrame(10100, true, {});
			auto& detail = *detailed.impl_;
			const auto& detailFrame = detail.frames[0].graphics;
			PerformanceSession::Impl::GraphicsGroup* mapGroup = nullptr;
			for (std::size_t i = 0; i < detail.capacity.graphicsGroups; ++i)
				if (detail.graphicsGroups[i].used && detail.graphicsGroups[i].operation == 0) mapGroup = &detail.graphicsGroups[i];
			check(detail.graphicsSeen == 6 && detail.graphicsCount == 2 && detail.slowSeen == 3 && detail.slowCount == 2 &&
				detail.slowCalls[0].serial == 3 && detail.slowCalls[1].serial == 4 && detail.detailDroppedAfter == 1050 &&
				mapGroup && mapGroup->seen == 4 && mapGroup->total == 9010 && mapGroup->maximum == 4000 && mapGroup->failed == 1 &&
				detailFrame.ticks[0] == 9010 && detailFrame.ticks[2] == 10 && detailFrame.ticks[6] == 5 && detail.groupCallsDropped == 1,
				"full prefix saturation retains later slow evidence and complete frame/group sums with exact group overflow");
			check((detail.graphicsCalls[0].accounting & kCallHasResult) && detail.graphicsCalls[0].result == E_FAIL &&
				!(detail.graphicsCalls[1].accounting & kCallHasResult), "HRESULT failure and void availability remain distinct");
			if (mapGroup) mapGroup->total = (std::numeric_limits<std::int64_t>::max)() - 1;
			detailed.BeginFrame(101, 11000, 0);
			measuredCall(detailed, PerformanceGraphicsOperation::Map, PerformanceGraphicsRole::InkData, "probe_map", 11010, 11020, true, S_OK);
			detailed.EndFrame(11030, true, {});
			check(detail.framesSeen == 2 && detail.frameCount == 1 && detail.current.graphics.ticks[0] == 10 &&
				mapGroup && mapGroup->seen == 5 && mapGroup->overflow && detail.arithmeticOverflows == 1,
				"frame prefix full keeps current and whole-session groups, group arithmetic overflow is explicit");
			PerformanceSession detailCoverage({ 1, 1, 1, 1, 4, 1, 4, 1 }, precise);
			measuredCall(detailCoverage, PerformanceGraphicsOperation::Map, PerformanceGraphicsRole::InkData, "probe_coverage", 20000, 22000);
			measuredCall(detailCoverage, PerformanceGraphicsOperation::Map, PerformanceGraphicsRole::InkData, "probe_coverage", 23000, 25000);
			check(detailCoverage.impl_->slowSeen == 2 && detailCoverage.impl_->slowCount == 1 && detailCoverage.impl_->GraphicsDetailComplete(),
				"slow prefix saturation does not hide an otherwise complete detailed prefix");
			PerformanceGraphicsSite collision = "probe_coverage_other";
			collision.id = PerformanceGraphicsSite("probe_coverage").id;
			detailCoverage.impl_->RegisterSite(collision);
			check(detailCoverage.impl_->siteCollisions == 1 && !detailCoverage.impl_->GraphicsDetailComplete(),
				"a colliding site identity makes detail coverage incomplete even with every call retained");
			PerformanceSession spans(probeCapacities, precise);
			measuredCall(spans, PerformanceGraphicsOperation::WriteCopy, PerformanceGraphicsRole::InkData, "probe_prior", 200, 220);
			clock.tick = 228; auto split = spans.BeginGraphicsCall(PerformanceGraphicsOperation::WriteCopy,
				PerformanceGraphicsRole::InkData, "probe_split");
			spans.BeginFrame(201, 240, 0); clock.tick = 259; spans.EndGraphicsCall(split);
			spans.EndFrame(270, true, {});
			check(spans.impl_->frames[0].priorGraphics.ticks[1] == 30 && spans.impl_->frames[0].graphics.ticks[1] == 20 &&
				spans.impl_->graphicsCalls[0].frame == 0 && spans.impl_->graphicsCalls[1].frame == 201 &&
				static_cast<CallAttribution>(spans.impl_->graphicsCalls[1].accounting >> 16) == CallAttribution::SplitPriorFrame,
				"prior completed work and crossing Begin split preserve raw endpoints without old-frame attribution");
			spans.BeginFrame(202, 300, 0); clock.tick = 308;
			auto crossing = spans.BeginGraphicsCall(PerformanceGraphicsOperation::WriteCopy, PerformanceGraphicsRole::InkData, "probe_cross");
			spans.EndFrame(320, true, {}); spans.BeginFrame(203, 330, 0); clock.tick = 349; spans.EndGraphicsCall(crossing);
			check(spans.impl_->graphicsCrossFrame == 1 && !spans.impl_->frames[1].graphics.observed &&
				spans.impl_->current.graphics.ticks[1] == 0 && spans.impl_->current.graphics.invalidCalls == 1,
				"cross-sealed-frame leaf is explicitly unavailable and never rewrites a sealed frame");
			measuredCall(spans, PerformanceGraphicsOperation::Map, PerformanceGraphicsRole::InkData, "probe_zero_clock", 0, 10, true, E_FAIL);
			measuredCall(spans, PerformanceGraphicsOperation::Unmap, PerformanceGraphicsRole::InkData, "probe_backwards", 400, 399);
			measuredCall(spans, PerformanceGraphicsOperation::WriteCopy, PerformanceGraphicsRole::InkData, "probe_measured_zero", 500, 500);
			measuredCall(spans, PerformanceGraphicsOperation::WriteCopy, PerformanceGraphicsRole::InkData, "probe_overlap", 490, 510);
			check(spans.impl_->graphicsInvalid == 2 && spans.impl_->graphicsOverlaps == 1 &&
				(spans.impl_->graphicsCalls[5].accounting & kCallTimeValid) && (spans.impl_->graphicsCalls[5].accounting & kCallAccountingValid) &&
				!(spans.impl_->graphicsCalls[6].accounting & kCallAccountingValid), "zero and backwards clocks are missing while measured zero is valid and overlap is rejected");
			clock.tick = 600;
			auto outer = spans.BeginGraphicsCall(PerformanceGraphicsOperation::Bind, PerformanceGraphicsRole::InkData, "probe_outer");
			auto inner = spans.BeginGraphicsCall(PerformanceGraphicsOperation::Bind, PerformanceGraphicsRole::InkData, "probe_inner");
			spans.EndGraphicsCall(inner); spans.EndGraphicsCall(outer);
			check(spans.impl_->graphicsNested == 2 && spans.impl_->graphicsCalls[7].parent == outer.serial &&
				!(spans.impl_->graphicsCalls[7].accounting & kCallAccountingValid) && !(spans.impl_->graphicsCalls[8].accounting & kCallAccountingValid),
				"nested leaves retain parent identity and cannot double-count frame coverage");
			spans.impl_->current.graphics.ticks[0] = (std::numeric_limits<std::int64_t>::max)() - 1;
			measuredCall(spans, PerformanceGraphicsOperation::Map, PerformanceGraphicsRole::InkData, "probe_overflow", 700, 710);
			check(spans.impl_->arithmeticOverflows == 1 && (spans.impl_->current.graphics.invalid & 1), "summary tick overflow invalidates instead of wrapping");
			struct ProbeBuffer final : ID3D11Buffer
			{
				UINT reads = 0;
				HRESULT STDMETHODCALLTYPE QueryInterface(REFIID, void** out) override { if (out) *out = nullptr; return E_NOINTERFACE; }
				ULONG STDMETHODCALLTYPE AddRef() override { return 1; }
				ULONG STDMETHODCALLTYPE Release() override { return 1; }
				void STDMETHODCALLTYPE GetDevice(ID3D11Device** out) override { if (out) *out = nullptr; }
				HRESULT STDMETHODCALLTYPE GetPrivateData(REFGUID, UINT*, void*) override { return E_NOTIMPL; }
				HRESULT STDMETHODCALLTYPE SetPrivateData(REFGUID, UINT, const void*) override { return E_NOTIMPL; }
				HRESULT STDMETHODCALLTYPE SetPrivateDataInterface(REFGUID, const IUnknown*) override { return E_NOTIMPL; }
				void STDMETHODCALLTYPE GetType(D3D11_RESOURCE_DIMENSION* out) override { *out = D3D11_RESOURCE_DIMENSION_BUFFER; }
				void STDMETHODCALLTYPE SetEvictionPriority(UINT) override {}
				UINT STDMETHODCALLTYPE GetEvictionPriority() override { return 0; }
				void STDMETHODCALLTYPE GetDesc(D3D11_BUFFER_DESC* out) override
				{ ++reads; *out = { 48, D3D11_USAGE_DYNAMIC, D3D11_BIND_CONSTANT_BUFFER, D3D11_CPU_ACCESS_WRITE, 0, 0 }; }
			} buffer;
			PerformanceSession identities(probeCapacities, precise);
			const auto resource1 = identities.RegisterResource(&buffer, PerformanceGraphicsRole::GlobalConstants, "probe_create");
			identities.RetireResource(&buffer); check(identities.ResourceId(&buffer) == 0, "retired resource is not active");
			const auto resource2 = identities.RegisterResource(&buffer, PerformanceGraphicsRole::GlobalConstants, "probe_recreate");
			const auto failedResource = identities.RegisterResource(nullptr, PerformanceGraphicsRole::GlobalConstants, "probe_failed_create", E_FAIL);
			PerformanceGraphicsArguments known{}; known.resource = &buffer; known.bytes = 48; known.mapType = D3D11_MAP_WRITE_DISCARD;
			auto knownCall = identities.BeginGraphicsCall(PerformanceGraphicsOperation::Map, PerformanceGraphicsRole::GlobalConstants, "probe_known", known);
			identities.EndGraphicsCall(knownCall, true, S_OK);
			check(resource1 == 1 && resource2 == 2 && failedResource == 3 && identities.ResourceId(&buffer) == 2 && buffer.reads == 2 &&
				identities.impl_->resources[0].destroyed > 0 && identities.impl_->resources[1].bytes == 48 &&
				identities.impl_->resources[2].result == E_FAIL && !identities.impl_->resources[2].descriptor &&
				identities.impl_->graphicsCalls[0].resource == 2 && identities.impl_->graphicsCalls[0].mapType == D3D11_MAP_WRITE_DISCARD,
				"cold descriptor once per generation, anonymous pointer reuse, failed creation and map arguments");
			identities.RegisterResource(&buffer, PerformanceGraphicsRole::GlobalConstants, "probe_capacity_recreate");
			check(identities.impl_->resourcesSeen == 4 && identities.impl_->resourceCount == 3 && identities.ResourceId(&buffer) == 0 && buffer.reads == 2,
				"metadata capacity cannot accidentally reuse stale same-address identity");
			int enabledMetadata = 0, enabledCalls = 0;
			const auto enabledResult = PerformanceGraphicsCall(&identities, PerformanceGraphicsOperation::Map,
				PerformanceGraphicsRole::InkData, "probe_enabled_hresult", [&]
				{ ++enabledMetadata; PerformanceGraphicsArguments args{}; args.bytes = 64; args.flags = 7; return args; },
				[&] { ++enabledCalls; return E_OUTOFMEMORY; });
			PerformanceGraphicsVoid(&identities, PerformanceGraphicsOperation::Unmap, PerformanceGraphicsRole::InkData,
				"probe_enabled_void", [&] { ++enabledMetadata; return PerformanceGraphicsArguments{}; }, [&] { ++enabledCalls; });
			check(enabledResult == E_OUTOFMEMORY && enabledCalls == 2 && enabledMetadata == 2 &&
				identities.impl_->graphicsCalls[1].bytes == 64 && identities.impl_->graphicsCalls[1].flags == 7 &&
				identities.impl_->graphicsCalls[1].result == E_OUTOFMEMORY && !(identities.impl_->graphicsCalls[2].accounting & kCallHasResult),
				"enabled wrappers preserve exactly-once result and metadata, void has no invented HRESULT");
			struct FakeGpu
			{
				UINT begins = 0, ends = 0, timestamps = 0, reads = 0, wrongFlags = 0;
				std::uint32_t pending = UINT32_MAX, failed = UINT32_MAX;
				bool disjoint = false, backwards = false;
				std::uint64_t frequency = 1000;
			} fakeGpu;
			const auto connectGpu = [&](PerformanceSession& target, FakeGpu& fake)
			{
				auto& p = *target.impl_; p.gpuAvailable = true; p.gpuQueryResult = S_OK; p.transport.state = &fake;
				p.transport.begin = [](void* state, std::size_t) noexcept { ++static_cast<FakeGpu*>(state)->begins; };
				p.transport.timestamp = [](void* state, std::size_t, std::uint32_t) noexcept { ++static_cast<FakeGpu*>(state)->timestamps; };
				p.transport.end = [](void* state, std::size_t) noexcept { ++static_cast<FakeGpu*>(state)->ends; };
				p.transport.data = [](void* state, std::size_t, std::uint32_t marker, void* out, UINT, UINT flags) noexcept -> HRESULT
				{
					auto& fake = *static_cast<FakeGpu*>(state); ++fake.reads;
					if (flags != D3D11_ASYNC_GETDATA_DONOTFLUSH) ++fake.wrongFlags;
					if (marker == fake.failed) return E_FAIL;
					if (marker == fake.pending) return S_FALSE;
					if (marker == 6) *static_cast<D3D11_QUERY_DATA_TIMESTAMP_DISJOINT*>(out) = { fake.frequency, fake.disjoint };
					else *static_cast<std::uint64_t*>(out) = fake.backwards && marker == 1 ? 90 : 100 + marker * 10;
					return S_OK;
				};
			};
			PerformanceSession gpuSchedule(probeCapacities, precise); connectGpu(gpuSchedule, fakeGpu);
			for (std::uint64_t loop = 1; loop <= 100; ++loop) { gpuSchedule.GpuBegin(loop); gpuSchedule.GpuEnd(); }
			check(fakeGpu.begins == 0 && fakeGpu.reads == 0 && gpuSchedule.impl_->gpuWorkCandidates == 0 && gpuSchedule.impl_->gpuCount == 0,
				"empty owner loops never consume first graphics-work GPU candidates or issue queries");
			gpuSchedule.BeginFrame(500, 1000, 0); gpuSchedule.GpuBegin(101); gpuSchedule.GpuMarker(0);
			gpuSchedule.GpuWork(PerformanceGraphicsPass::Geometry); gpuSchedule.GpuWork(PerformanceGraphicsPass::Composite);
			gpuSchedule.GpuDraw(PerformanceGraphicsPass::Geometry, 2); gpuSchedule.GpuMarker(1); gpuSchedule.GpuMarker(3);
			gpuSchedule.GpuEnd(); gpuSchedule.EndFrame(2000, true, {});
			gpuSchedule.impl_->pollSlot = 0; gpuSchedule.impl_->PollGpu();
			const auto& firstGpu = gpuSchedule.impl_->gpu[0];
			check(fakeGpu.begins == 1 && fakeGpu.ends == 1 && firstGpu.frame == 500 && firstGpu.loop == 101 && firstGpu.candidate == 1 &&
				firstGpu.firstWork && firstGpu.markers == 15 && firstGpu.clipped == 5 && firstGpu.draws == 2 && firstGpu.geometryDraws == 2 &&
				firstGpu.compositeDraws == 0 && firstGpu.status == PerformanceSession::Impl::GpuStatus::Ready && gpuSchedule.impl_->gpuWorkCandidates == 1,
				"first actual work starts GPU sample once with real frame, clipped phase markers and actual draw counts");
			for (std::uint64_t loop = 102; loop <= 124; ++loop)
			{
				gpuSchedule.BeginFrame(loop + 399, clock.tick, 0);
				gpuSchedule.GpuBegin(loop); gpuSchedule.GpuWork(PerformanceGraphicsPass::Geometry);
				gpuSchedule.GpuWork(PerformanceGraphicsPass::Composite); gpuSchedule.GpuDraw(PerformanceGraphicsPass::Geometry);
				gpuSchedule.GpuMarker(1); gpuSchedule.GpuMarker(3); gpuSchedule.GpuEnd();
				gpuSchedule.EndFrame(clock.tick + 1, true, {});
			}
			check(gpuSchedule.impl_->gpuWorkCandidates == 24 && gpuSchedule.impl_->gpuSelected == 10 && gpuSchedule.impl_->gpuCount == 10 &&
				gpuSchedule.impl_->gpu[8].candidate == 16 && gpuSchedule.impl_->gpu[9].candidate == 24 && gpuSchedule.impl_->gpuDrawFrames == 24,
				"first eight then every eighth real-work candidate selection independent of empty loops and multiple passes");
			FakeGpu priorGpu;
			PerformanceSession independentGpu(probeCapacities, precise); connectGpu(independentGpu, priorGpu);
			for (std::uint64_t prior = 1; prior <= 20; ++prior)
			{
				independentGpu.GpuBegin(0); independentGpu.GpuWork(PerformanceGraphicsPass::Geometry);
				independentGpu.GpuDraw(PerformanceGraphicsPass::Geometry); independentGpu.GpuMarker(1); independentGpu.GpuEnd();
			}
			independentGpu.BeginFrame(700, clock.tick, 0); independentGpu.GpuBegin(700);
			independentGpu.GpuWork(PerformanceGraphicsPass::Geometry); independentGpu.GpuDraw(PerformanceGraphicsPass::Geometry);
			independentGpu.GpuMarker(1); independentGpu.GpuEnd(); independentGpu.EndFrame(clock.tick + 1, true, {});
			check(independentGpu.impl_->gpuPriorCandidates == 20 && independentGpu.impl_->gpuPriorSelected == 5 &&
				independentGpu.impl_->gpuFrameCandidates == 1 && independentGpu.impl_->gpuFrameSelected == 1 && independentGpu.impl_->gpuCount == 6 &&
				independentGpu.impl_->gpu[0].frame == 0 && independentGpu.impl_->gpu[0].loop == 0 && independentGpu.impl_->gpu[5].frame == 700 &&
				independentGpu.impl_->gpu[5].domainCandidate == 1 && independentGpu.impl_->gpu[5].firstWork && !independentGpu.impl_->gpu[5].prior &&
				independentGpu.impl_->gpuDrawFrames == 1 && independentGpu.impl_->gpuPriorDrawSegments == 20,
				"prior first4 every16 uses an independent sequence and cannot consume first real-frame priority or reuse old frame identity");
			for (std::size_t i = 0; i < independentGpu.impl_->querySlots.size(); ++i) independentGpu.impl_->PollGpu();
			check(independentGpu.impl_->ReadyDrawSamples(false) == 1 && independentGpu.impl_->ReadyDrawSamples(true) == 5,
				"ready frame draw coverage excludes five separately counted prior draw samples");
			independentGpu.impl_->gpu[5].markers = 0;
			check(independentGpu.impl_->ReadyDrawSamples(false) == 0, "draw without an observed marker pair cannot count as ready draw coverage");
			independentGpu.impl_->gpu[5].markers = 3;
			independentGpu.impl_->gpu[5].status = PerformanceSession::Impl::GpuStatus::Failed;
			check(independentGpu.impl_->ReadyDrawSamples(false) == 0, "failed query cannot count as ready draw coverage");
			FakeGpu delayed; delayed.pending = 6;
			PerformanceSession gpuPending(probeCapacities, precise); connectGpu(gpuPending, delayed);
			gpuPending.GpuBegin(1); gpuPending.GpuWork(PerformanceGraphicsPass::Geometry);
			gpuPending.GpuWork(PerformanceGraphicsPass::Composite); gpuPending.GpuMarker(1); gpuPending.GpuMarker(3); gpuPending.GpuEnd();
			gpuPending.impl_->pollSlot = 0; gpuPending.impl_->PollGpu();
			check(delayed.reads == 1 && gpuPending.impl_->gpu[0].status == PerformanceSession::Impl::GpuStatus::Pending,
				"disjoint S_FALSE returns after one DONOTFLUSH read without spin");
			delayed.pending = 1; const auto readsBeforeMarker = delayed.reads;
			gpuPending.impl_->pollSlot = 0; gpuPending.impl_->PollGpu();
			check(delayed.reads - readsBeforeMarker == 3 && delayed.wrongFlags == 0 && gpuPending.impl_->gpu[0].status == PerformanceSession::Impl::GpuStatus::Pending,
				"timestamp S_FALSE returns immediately before later observed markers");
			delayed.pending = UINT32_MAX; delayed.backwards = true;
			gpuPending.impl_->pollSlot = 0; gpuPending.impl_->PollGpu();
			check(gpuPending.impl_->gpu[0].status == PerformanceSession::Impl::GpuStatus::Invalid, "nonmonotonic real marker pair cannot be ready GPU evidence");
			const auto statusCase = [&](FakeGpu settings, PerformanceSession::Impl::GpuStatus expected, const char* name)
			{
				PerformanceSession test(probeCapacities, precise); connectGpu(test, settings);
				test.GpuBegin(1); test.GpuWork(PerformanceGraphicsPass::Geometry); test.GpuMarker(1); test.GpuEnd();
				test.impl_->pollSlot = 0; test.impl_->PollGpu();
				check(test.impl_->gpu[0].status == expected && test.impl_->gpu[0].draws == 0 && settings.wrongFlags == 0, name);
			};
			FakeGpu disjoint; disjoint.disjoint = true;
			statusCase(disjoint, PerformanceSession::Impl::GpuStatus::Disjoint, "disjoint is unavailable with no invented draw");
			FakeGpu zeroFrequency; zeroFrequency.frequency = 0;
			statusCase(zeroFrequency, PerformanceSession::Impl::GpuStatus::Disjoint, "zero frequency is unavailable rather than zero GPU time");
			FakeGpu failedQuery; failedQuery.failed = 6;
			statusCase(failedQuery, PerformanceSession::Impl::GpuStatus::Failed, "query HRESULT failure is retained without draw success");
			FakeGpu missingMarkers;
			PerformanceSession gpuMissing(probeCapacities, precise); connectGpu(gpuMissing, missingMarkers);
			gpuMissing.GpuBegin(1); gpuMissing.GpuWork(PerformanceGraphicsPass::Unknown);
			gpuMissing.GpuDraw(PerformanceGraphicsPass::Unknown); gpuMissing.GpuEnd();
			gpuMissing.impl_->pollSlot = 0; gpuMissing.impl_->PollGpu();
			check(gpuMissing.impl_->gpu[0].status == PerformanceSession::Impl::GpuStatus::Ready &&
				gpuMissing.impl_->gpu[0].markers == 0 && gpuMissing.impl_->gpu[0].draws == 1 && gpuMissing.impl_->gpu[0].geometryDraws == 0,
				"ready transport without stage markers or classified draw cannot imply a measured shader interval");
			FakeGpu busy; busy.pending = 6;
			PerformanceSession gpuBusy(probeCapacities, precise); connectGpu(gpuBusy, busy);
			for (std::uint64_t loop = 1; loop <= 16; ++loop)
			{
				gpuBusy.BeginFrame(loop, clock.tick, 0); gpuBusy.GpuBegin(loop);
				gpuBusy.GpuWork(PerformanceGraphicsPass::Geometry); gpuBusy.GpuEnd(); gpuBusy.EndFrame(clock.tick + 1, false, {});
			}
			check(gpuBusy.impl_->gpuCount == 8 && gpuBusy.impl_->gpuSlotsBusy == 1 && gpuBusy.impl_->gpuCapacitySkips == 0 &&
				gpuBusy.impl_->gpuSelected == 9, "eight-slot pool saturation is bounded and distinct from row capacity");
			const auto readsBeforeStop = busy.reads; gpuBusy.GpuStop();
			check(busy.reads - readsBeforeStop <= 8 && gpuBusy.impl_->gpuPendingStop == 8 &&
				gpuBusy.impl_->gpu[0].status == PerformanceSession::Impl::GpuStatus::Stopped,
				"owner stop polls exactly one bounded pool sweep then marks pending without waiting");
			FakeGpu limited; limited.pending = 6;
			PerformanceSession gpuLimited({ 1, 1, 1, 1, 2, 2, 2, 2 }, precise); connectGpu(gpuLimited, limited);
			gpuLimited.GpuBegin(1); gpuLimited.GpuWork(PerformanceGraphicsPass::Geometry); gpuLimited.GpuEnd();
			gpuLimited.GpuBegin(2); gpuLimited.GpuWork(PerformanceGraphicsPass::Geometry); gpuLimited.GpuEnd();
			check(gpuLimited.impl_->gpuCapacitySkips == 1 && gpuLimited.impl_->gpuSlotsBusy == 0, "GPU row capacity has its own skip truth");
			gpuLimited.impl_->AdvanceDeviceEpoch();
			check(gpuLimited.impl_->gpu[0].status == PerformanceSession::Impl::GpuStatus::Replaced &&
				gpuLimited.impl_->gpuPendingReplaced == 1 && gpuLimited.impl_->gpuPendingStop == 0 && gpuLimited.impl_->gpuEpochCandidates == 0,
				"device replacement retires old generation samples and resets early-work priority");
			{
				PerformanceSession maximum;
				check(maximum.AllocatedBytes() <= kPayloadBudget, "default actual sizeof payload remains within 32MiB");
				std::printf("[Draw3.Performance.Test] payload=%llu budget=%llu frame_row=%llu call_row=%llu group_row=%llu resource_row=%llu\n",
					static_cast<unsigned long long>(maximum.AllocatedBytes()), static_cast<unsigned long long>(kPayloadBudget),
					static_cast<unsigned long long>(sizeof(PerformanceSession::Impl::Frame)), static_cast<unsigned long long>(sizeof(PerformanceGraphicsToken)),
					static_cast<unsigned long long>(sizeof(PerformanceSession::Impl::GraphicsGroup)), static_cast<unsigned long long>(sizeof(PerformanceSession::Impl::Resource)));
			}
			session.GpuStop(); check(queryCreateCount.load() == queries, "pure CPU fake transport creates no real GPU queries");
		}
		catch (...) { ++failures; }
		std::printf("[Draw3.Performance.Test] recorder failures=%d\n", failures);
		return failures ? 1 : 0;
	}
}
