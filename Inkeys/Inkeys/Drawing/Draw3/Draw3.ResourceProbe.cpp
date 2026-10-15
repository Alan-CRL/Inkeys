#include "Draw3.ResourceProbe.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "../../../resource.h"
#include <windows.h>
#include <d3d11.h>
#include <dxgi.h>
#include <wrl/client.h>
#include <json/json.h>
#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <memory>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")

namespace Inkeys::Drawing::Draw3
{
	namespace
	{
		using Microsoft::WRL::ComPtr;
		constexpr char kMarker[] = "INKEYS_DRAW3_DIAGNOSTICS_V2";
		constexpr char kProfile[] = "draw3-resource-v2-fixed";
		constexpr char kComparisonPath[] = "offscreen_embedded_301_302_contract";
		constexpr unsigned kBudgetMs = 8000, kCallCapacity = 4096, kIterations = 6;
		enum class Work { Ink, Constant, Highlighter, Composite, ClearCopy, Readback };
		enum class Update { Discard, NoOverwrite, Default };
		enum class Write { Fields, Memcpy, Volatile };
		struct Case
		{
			const char* name;
			Work work;
			unsigned bytes, writeBytes, draws;
			Update update = Update::Discard;
			Write write = Write::Fields;
			unsigned width = 64, height = 64;
		};
		constexpr Case kCases[] = {
			{ "ink_large_discard_idle", Work::Ink, 3200000, 32, 0 },
			{ "ink_small_discard_idle", Work::Ink, 256, 32, 0 },
			{ "ink_large_discard_draw", Work::Ink, 3200000, 32, 1 },
			{ "ink_small_discard_draw", Work::Ink, 256, 32, 1 },
			{ "ink_large_no_overwrite_draw", Work::Ink, 3200000, 32, 1, Update::NoOverwrite },
			{ "ink_large_write4096_draw", Work::Ink, 3200000, 4096, 1 },
			{ "constant48_fields_idle", Work::Constant, 48, 36, 0 },
			{ "constant48_fields_draw", Work::Constant, 48, 36, 1 },
			{ "constant48_memcpy_draw", Work::Constant, 48, 48, 1, Update::Discard, Write::Memcpy },
			{ "constant48_volatile_draw", Work::Constant, 48, 48, 1, Update::Discard, Write::Volatile },
			{ "ink_default_update_draw", Work::Ink, 3200000, 32, 1, Update::Default },
			{ "constant48_default_update_draw", Work::Constant, 48, 48, 1, Update::Default, Write::Memcpy },
			{ "highlighter_large_draw", Work::Highlighter, 4800000, 48, 1 },
			{ "composite_large_draw", Work::Composite, 3200000, 32, 1 },
			{ "composite_small_draw", Work::Composite, 256, 32, 1 },
			{ "submission2_draw", Work::Ink, 3200000, 32, 2 },
			{ "submission3_draw", Work::Ink, 3200000, 32, 3 },
			{ "submission6_draw", Work::Ink, 3200000, 32, 6 },
			{ "target1080_clear_copy", Work::ClearCopy, 8294400, 8294400, 0,
				Update::Default, Write::Memcpy, 1920, 1080 },
			{ "target64_readback", Work::Readback, 16384, 16384, 0,
				Update::Default, Write::Memcpy }
		};
		struct Constants
		{
			float width, height, shape;
			std::uint32_t offset;
			float color[4];
			std::uint32_t operation;
			float padding[3];
		};
		struct Point { float x, y, radius, time; };
		struct Highlighter { float x1, y1, x2, y2, halfWidth, halfHeight; };
		static_assert(sizeof(Constants) == 48 && offsetof(Constants, operation) == 32);
		static_assert(sizeof(Point) == 16 && sizeof(Highlighter) == 24);
		static_assert(std::size(kCases) == 20);

		std::int64_t Qpc() noexcept
		{
			LARGE_INTEGER value{};
			return QueryPerformanceCounter(&value) ? value.QuadPart : 0;
		}
		std::uint64_t FileTimeValue(FILETIME value) noexcept
		{
			return (static_cast<std::uint64_t>(value.dwHighDateTime) << 32) | value.dwLowDateTime;
		}
		struct CpuStamp { std::uint64_t value = 0; bool available = false; };
		CpuStamp CpuTime(bool process) noexcept
		{
			FILETIME created{}, exited{}, kernel{}, user{};
			const BOOL ok = process ? GetProcessTimes(GetCurrentProcess(), &created, &exited, &kernel, &user)
				: GetThreadTimes(GetCurrentThread(), &created, &exited, &kernel, &user);
			return { ok ? FileTimeValue(kernel) + FileTimeValue(user) : 0, ok != FALSE };
		}
		std::string Utf8(std::wstring_view value)
		{
			const int size = WideCharToMultiByte(CP_UTF8, 0, value.data(), static_cast<int>(value.size()),
				nullptr, 0, nullptr, nullptr);
			std::string result(size, '\0');
			if (size) WideCharToMultiByte(CP_UTF8, 0, value.data(), static_cast<int>(value.size()),
				result.data(), size, nullptr, nullptr);
			return result;
		}
		Json::Value U64(std::uint64_t value) { return Json::Value(Json::UInt64(value)); }
		bool WriteBytes(const std::wstring& path, std::string_view data)
		{
			HANDLE file = CreateFileW(path.c_str(), GENERIC_WRITE, FILE_SHARE_READ, nullptr,
				CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
			if (file == INVALID_HANDLE_VALUE) return false;
			DWORD written = 0;
			const bool ok = data.size() <= 4u * 1024 * 1024 && WriteFile(file, data.data(),
				static_cast<DWORD>(data.size()), &written, nullptr) && written == data.size() && FlushFileBuffers(file);
			CloseHandle(file);
			return ok;
		}
		std::string JsonText(const Json::Value& value)
		{
			Json::StreamWriterBuilder builder;
			builder["indentation"] = "  ";
			return Json::writeString(builder, value) + "\r\n";
		}
		bool AtomicJson(const std::wstring& path, const Json::Value& value, bool replace = false)
		{
			const auto temporary = path + L".tmp";
			if (!WriteBytes(temporary, JsonText(value))) return false;
			const bool ok = MoveFileExW(temporary.c_str(), path.c_str(),
				replace ? MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH : MOVEFILE_WRITE_THROUGH) != FALSE;
			if (!ok) DeleteFileW(temporary.c_str());
			return ok;
		}
		bool Stdout(std::string_view value) noexcept
		{
			DWORD written = 0;
			return WriteFile(GetStdHandle(STD_OUTPUT_HANDLE), value.data(), static_cast<DWORD>(value.size()),
				&written, nullptr) && written == value.size();
		}
		const char* UpdateName(Update update) noexcept
		{
			return update == Update::Default ? "default_update_subresource" :
				update == Update::NoOverwrite ? "discard_then_no_overwrite" : "write_discard";
		}
		const char* WriteName(Write write) noexcept
		{
			return write == Write::Memcpy ? "local_pod_memcpy" :
				write == Write::Volatile ? "volatile_write_only" : "production_field_stores";
		}
		Json::Value Definition(const Case& test, bool warp)
		{
			Json::Value result;
			const std::string backend = warp ? "warp" : "hardware";
			result["case_id"] = backend + "-" + test.name;
			result["profile_id"] = test.name;
			result["requested_backend"] = backend;
			result["resource_bytes"] = test.bytes; result["write_bytes"] = test.writeBytes;
			result["draws_per_iteration"] = test.draws;
			result["width"] = test.width; result["height"] = test.height;
			result["update_method"] = UpdateName(test.update);
			result["constant_write_method"] = WriteName(test.write);
			return result;
		}
		Json::Value Envelope()
		{
			Json::Value result;
			result["schema_version"] = 1; result["probe_revision"] = 2;
			result["capability_marker"] = kMarker; result["profile"] = kProfile;
			result["comparison_path"] = kComparisonPath;
			return result;
		}
		Json::Value CaseList()
		{
			auto result = Envelope();
			result["native_budget_ms"] = kBudgetMs;
			result["cold_iterations"] = 1; result["warmup_iterations"] = 1; result["measured_iterations"] = 4;
			result["call_capacity"] = kCallCapacity;
			result["case_count"] = static_cast<unsigned>(std::size(kCases) * 2);
			result["cases"] = Json::arrayValue;
			for (const auto& test : kCases)
				for (bool warp : { false, true }) result["cases"].append(Definition(test, warp));
			return result;
		}
		const Case* FindCase(std::wstring_view name, bool& warp)
		{
			const auto encoded = Utf8(name);
			for (const auto& test : kCases)
				for (bool candidate : { false, true })
					if (encoded == std::string(candidate ? "warp-" : "hardware-") + test.name)
					{ warp = candidate; return &test; }
			return nullptr;
		}
		bool SessionValid(std::wstring_view session) noexcept
		{
			if (session.empty() || session.size() > 128) return false;
			for (wchar_t ch : session)
				if (!((ch >= L'a' && ch <= L'z') || (ch >= L'A' && ch <= L'Z') ||
					(ch >= L'0' && ch <= L'9') || ch == L'_' || ch == L'-' || ch == L'.')) return false;
			return true;
		}
		struct Call
		{
			const char* phase = "initialization";
			const char* operation = "other";
			const char* site = "unknown";
			unsigned iteration = 0, resource = 0, related = 0, map = 0, flags = 0;
			unsigned offset = 0, bytes = 0, vertices = 0, instances = 0;
			std::int64_t start = 0, end = 0;
			HRESULT result = S_OK;
			bool hasResult = false;
		};
		struct Resource
		{
			unsigned id = 0;
			const char* role = "unknown";
			D3D11_BUFFER_DESC buffer{};
			D3D11_TEXTURE2D_DESC texture{};
			HRESULT result = S_OK;
			bool isBuffer = true;
		};
		struct Gpu
		{
			ComPtr<ID3D11Query> disjoint, begin, end;
			const char* status = "not_started";
			std::uint64_t frequency = 0, first = 0, last = 0;
			unsigned draws = 0;
		};
		struct EventCompletion
		{
			HRESULT result = S_FALSE;
			unsigned polls = 0;
			bool complete = false;
		};
		template<class Poll, class BeforeDeadline, class Pause>
		EventCompletion WaitForEvent(Poll&& poll, BeforeDeadline&& beforeDeadline, Pause&& pause)
		{
			EventCompletion value;
			while (beforeDeadline())
			{
				BOOL ready = FALSE;
				value.result = poll(&ready); ++value.polls;
				// EVENT 的 S_OK 仍须核实返回的 BOOL；未完成只在对照进程的截止内继续。
				value.complete = value.result == S_OK && ready == TRUE;
				if (value.complete || FAILED(value.result)) break;
				pause();
			}
			return value;
		}
		class Probe
		{
		public:
			Probe(const Case& test, bool warp, std::wstring directory, std::wstring session)
				: test_(test), warp_(warp), directory_(std::move(directory)), session_(std::move(session)),
				calls_(std::make_unique<Call[]>(kCallCapacity))
			{
				LARGE_INTEGER frequency{}; QueryPerformanceFrequency(&frequency); frequency_ = frequency.QuadPart;
				start_ = Qpc(); threadStart_ = CpuTime(false); processStart_ = CpuTime(true);
				deadline_ = GetTickCount64() + kBudgetMs;
				resources_.reserve(12);
				identity_ = Envelope();
				identity_["case_definition"] = Definition(test_, warp_);
				identity_["case_id"] = identity_["case_definition"]["case_id"];
				identity_["profile_id"] = test_.name;
				identity_["session_id"] = Utf8(session_);
				identity_["requested_backend"] = warp_ ? "warp" : "hardware";
				identity_["final_pid"] = Json::UInt(GetCurrentProcessId());
				FILETIME created{}, exited{}, kernel{}, user{};
				identityValid_ = GetProcessTimes(GetCurrentProcess(), &created, &exited, &kernel, &user) != FALSE;
				identity_["process_start_filetime_utc"] = std::to_string(FileTimeValue(created));
				std::wstring executable(32768, L'\0');
				executable.resize(GetModuleFileNameW(nullptr, executable.data(), static_cast<DWORD>(executable.size())));
				identityValid_ = identityValid_ && !executable.empty() && FileTimeValue(created) != 0;
				identity_["exe_path"] = Utf8(executable);
				identity_["output_directory"] = Utf8(directory_);
			}
			int Run()
			{
				const auto lockPath = Path(L".draw3-resource-probe.lock");
				HANDLE lock = CreateFileW(lockPath.c_str(), GENERIC_WRITE, FILE_SHARE_READ, nullptr,
					CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
				if (lock == INVALID_HANDLE_VALUE) return 4;
				struct Close { HANDLE handle; ~Close() { CloseHandle(handle); } } close{ lock };
				if (!identityValid_ || !AtomicJson(Path(L"probe-start.json"), identity_)) return 4;
				Checkpoint("initializing");
				if (frequency_ <= 0 || start_ <= 0) Failure(E_FAIL, "qpc_unavailable");
				else if (Initialize())
				{
					for (iteration_ = 0; iteration_ < kIterations; ++iteration_)
					{
						phase_ = iteration_ == 0 ? "cold" : iteration_ == 1 ? "warmup" : "measure";
						if (Expired()) break;
						Checkpoint(phase_);
						BeginGpu(iteration_);
						const bool ok = Iterate();
						EndGpu(iteration_);
						if (!ok) break;
						++completed_[iteration_ == 0 ? 0 : iteration_ == 1 ? 1 : 2];
					}
					Checkpoint("drain");
					phase_ = "drain";
					Drain();
				}
				Checkpoint("exporting");
				return Export();
			}
		private:
			std::wstring Path(const wchar_t* leaf) const { return (std::filesystem::path(directory_) / leaf).wstring(); }
			void Checkpoint(const char* stage)
			{
				auto value = identity_; value["stage"] = stage;
				value["iteration"] = iteration_; value["qpc"] = Json::Int64(Qpc());
				if (!AtomicJson(Path(L"probe-stage.json"), value, true)) ioFailed_ = true;
			}
			bool Expired()
			{
				if (GetTickCount64() < deadline_) return false;
				partial_ = true; reason_ = "native_deadline"; return true;
			}
			bool Failure(HRESULT result, const char* stage, bool unsupported = false)
			{
				if (FAILED(result) && SUCCEEDED(firstResult_))
				{ firstResult_ = result; firstStage_ = stage; unsupported_ = unsupported; }
				return SUCCEEDED(result);
			}
			template<class F> HRESULT HCall(Call call, F&& function)
			{
				call.phase = phase_; call.iteration = iteration_; call.hasResult = true;
				call.start = Qpc(); call.result = function(); call.end = Qpc(); Record(call); return call.result;
			}
			template<class F> void VCall(Call call, F&& function)
			{
				call.phase = phase_; call.iteration = iteration_;
				call.start = Qpc(); function(); call.end = Qpc(); Record(call);
			}
			void Record(const Call& call)
			{
				++seen_;
				if (call.start <= 0 || call.end < call.start || frequency_ <= 0) ++invalid_;
				if (retained_ < kCallCapacity) calls_[retained_++] = call;
				else partial_ = true;
			}
			HRESULT Buffer(unsigned id, const char* role, D3D11_BUFFER_DESC description,
				ComPtr<ID3D11Buffer>& buffer, const void* initial = nullptr)
			{
				D3D11_SUBRESOURCE_DATA data{}; data.pSysMem = initial;
				Call call; call.operation = "create"; call.site = role; call.resource = id; call.bytes = description.ByteWidth;
				const auto result = HCall(call, [&] { return device_->CreateBuffer(&description,
					initial ? &data : nullptr, buffer.ReleaseAndGetAddressOf()); });
				resources_.push_back({ id, role, description, {}, result, true });
				return result;
			}
			HRESULT Texture(unsigned id, const char* role, D3D11_TEXTURE2D_DESC description,
				ComPtr<ID3D11Texture2D>& texture)
			{
				Call call; call.operation = "create"; call.site = role; call.resource = id;
				const auto result = HCall(call, [&] { return device_->CreateTexture2D(&description,
					nullptr, texture.ReleaseAndGetAddressOf()); });
				resources_.push_back({ id, role, {}, description, result, false });
				return result;
			}
			bool Initialize()
			{
				const D3D_FEATURE_LEVEL levels[] = { D3D_FEATURE_LEVEL_11_1, D3D_FEATURE_LEVEL_11_0 };
				const auto driver = warp_ ? D3D_DRIVER_TYPE_WARP : D3D_DRIVER_TYPE_HARDWARE;
				Call call; call.site = "d3d11_create_device"; call.operation = "create";
				auto result = HCall(call, [&] { return D3D11CreateDevice(nullptr, driver, nullptr,
					D3D11_CREATE_DEVICE_BGRA_SUPPORT, levels, 2, D3D11_SDK_VERSION,
					device_.ReleaseAndGetAddressOf(), &feature_, context_.ReleaseAndGetAddressOf()); });
				if (result == E_INVALIDARG)
				{
					call.site = "d3d11_create_device_11_0_retry";
					result = HCall(call, [&] { return D3D11CreateDevice(nullptr, driver, nullptr,
						D3D11_CREATE_DEVICE_BGRA_SUPPORT, levels + 1, 1, D3D11_SDK_VERSION,
						device_.ReleaseAndGetAddressOf(), &feature_, context_.ReleaseAndGetAddressOf()); });
				}
				// 对照必须保持指定 backend；硬件失败绝不悄悄切到 WARP。
				if (!Failure(result, "d3d11_create_device")) return false;
				actualBackend_ = warp_ ? "warp" : "hardware";
				CaptureAdapter();
				capsResult_ = device_->CheckFeatureSupport(D3D11_FEATURE_D3D11_OPTIONS, &caps_, sizeof(caps_));
				if (test_.update == Update::NoOverwrite &&
					(FAILED(capsResult_) || !caps_.MapNoOverwriteOnDynamicBufferSRV))
					return Failure(E_NOTIMPL, "map_no_overwrite_dynamic_srv", true);
				if (Expired()) return false;
				if (test_.work == Work::ClearCopy || test_.work == Work::Readback) return CreateTargets();
				const bool constant = test_.work == Work::Constant;
				D3D11_BUFFER_DESC description{};
				description.ByteWidth = test_.bytes;
				description.Usage = test_.update == Update::Default ? D3D11_USAGE_DEFAULT : D3D11_USAGE_DYNAMIC;
				description.BindFlags = constant ? D3D11_BIND_CONSTANT_BUFFER : D3D11_BIND_SHADER_RESOURCE;
				description.CPUAccessFlags = test_.update == Update::Default ? 0 : D3D11_CPU_ACCESS_WRITE;
				description.MiscFlags = constant ? 0 : D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
				description.StructureByteStride = constant ? 0 : test_.work == Work::Highlighter ? 24 : 16;
				if (!Failure(Buffer(1, constant ? "primary_constant48" : test_.work == Work::Highlighter ?
					"primary_highlighter" : "primary_ink", description, primary_), "create_primary_buffer")) return false;
				if (!test_.draws) { gpuStatus_ = "not_collected_cpu_update_only"; return true; }
				if (!constant)
				{
					D3D11_SHADER_RESOURCE_VIEW_DESC view{};
					view.ViewDimension = D3D11_SRV_DIMENSION_BUFFER;
					view.Buffer.NumElements = test_.bytes / description.StructureByteStride;
					if (!Failure(device_->CreateShaderResourceView(primary_.Get(), &view, primarySRV_.GetAddressOf()),
						"create_primary_srv")) return false;
				}
				D3D11_BUFFER_DESC constantDescription = { 48, D3D11_USAGE_DYNAMIC,
					D3D11_BIND_CONSTANT_BUFFER, D3D11_CPU_ACCESS_WRITE, 0, 0 };
				if (constant) global_ = primary_;
				else if (!Failure(Buffer(2, "draw_global_constant48", constantDescription, global_), "create_draw_constants")) return false;
				if (constant)
				{
					const Point points[] = { { 8, 8, 3, 0 }, { 56, 56, 3, 0 } };
					D3D11_BUFFER_DESC ink = { 32, D3D11_USAGE_IMMUTABLE, D3D11_BIND_SHADER_RESOURCE,
						0, D3D11_RESOURCE_MISC_BUFFER_STRUCTURED, 16 };
					if (!Failure(Buffer(7, "immutable_draw_points", ink, immutable_, points), "create_immutable_points")) return false;
					D3D11_SHADER_RESOURCE_VIEW_DESC view{}; view.ViewDimension = D3D11_SRV_DIMENSION_BUFFER;
					view.Buffer.NumElements = 2;
					if (!Failure(device_->CreateShaderResourceView(immutable_.Get(), &view, primarySRV_.GetAddressOf()),
						"create_immutable_srv")) return false;
				}
				if (!CreateTargets() || !CreatePipeline()) return false;
				CreateQueries();
				return true;
			}
			void CaptureAdapter()
			{
				ComPtr<IDXGIDevice> dxgi; ComPtr<IDXGIAdapter> adapter;
				DXGI_ADAPTER_DESC description{}; LARGE_INTEGER driver{};
				if (FAILED(device_.As(&dxgi)) || FAILED(dxgi->GetAdapter(adapter.GetAddressOf())) ||
					FAILED(adapter->GetDesc(&description))) { adapter_["available"] = false; return; }
				adapter_["available"] = true; adapter_["description"] = Utf8(description.Description);
				adapter_["vendor_id"] = description.VendorId; adapter_["device_id"] = description.DeviceId;
				adapter_["subsystem_id"] = description.SubSysId; adapter_["revision"] = description.Revision;
				adapter_["dedicated_video_memory"] = U64(description.DedicatedVideoMemory);
				adapter_["dedicated_system_memory"] = U64(description.DedicatedSystemMemory);
				adapter_["shared_system_memory"] = U64(description.SharedSystemMemory);
				const auto result = adapter->CheckInterfaceSupport(__uuidof(ID3D11Device), &driver);
				adapter_["driver_version_available"] = SUCCEEDED(result);
				adapter_["driver_version"] = SUCCEEDED(result) ? std::to_string(driver.QuadPart) : "";
				adapter_["driver_query_hresult"] = static_cast<int>(result);
			}
			bool CreateTargets()
			{
				D3D11_TEXTURE2D_DESC description{};
				description.Width = test_.width; description.Height = test_.height;
				description.MipLevels = description.ArraySize = description.SampleDesc.Count = 1;
				description.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
				description.Usage = D3D11_USAGE_DEFAULT;
				description.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
				if (!Failure(Texture(3, "offscreen_bgra", description, target_), "create_offscreen_bgra") ||
					!Failure(device_->CreateRenderTargetView(target_.Get(), nullptr, targetRTV_.GetAddressOf()), "create_bgra_rtv") ||
					!Failure(Texture(5, "copy_source_bgra", description, copy_), "create_copy_bgra") ||
					!Failure(device_->CreateShaderResourceView(copy_.Get(), nullptr, copySRV_.GetAddressOf()), "create_copy_srv")) return false;
				description.Format = DXGI_FORMAT_R16_FLOAT;
				if (!Failure(Texture(4, "offscreen_retain_r16f", description, retain_), "create_offscreen_r16f", true) ||
					!Failure(device_->CreateRenderTargetView(retain_.Get(), nullptr, retainRTV_.GetAddressOf()), "create_r16f_rtv", true) ||
					!Failure(device_->CreateShaderResourceView(retain_.Get(), nullptr, retainSRV_.GetAddressOf()), "create_r16f_srv", true)) return false;
				if (test_.work == Work::Readback)
				{
					description.Format = DXGI_FORMAT_B8G8R8A8_UNORM; description.Usage = D3D11_USAGE_STAGING;
					description.BindFlags = 0; description.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
					if (!Failure(Texture(6, "staging_readback", description, staging_), "create_staging")) return false;
				}
				const float zero[4] = {}, one[4] = { 1, 1, 1, 1 };
				Call call; call.operation = "clear"; call.site = "initialize_targets"; call.resource = 3;
				VCall(call, [&] { context_->ClearRenderTargetView(targetRTV_.Get(), zero); });
				call.resource = 4; VCall(call, [&] { context_->ClearRenderTargetView(retainRTV_.Get(), one); });
				call.operation = "copy"; call.resource = 5; call.related = 3;
				VCall(call, [&] { context_->CopyResource(copy_.Get(), target_.Get()); });
				if (!test_.draws) CreateQueries();
				return true;
			}
			bool CreatePipeline()
			{
				auto shader = [](int id, std::size_t& size) -> const void*
				{
					const auto module = GetModuleHandleW(nullptr);
					const auto resource = FindResourceW(module, MAKEINTRESOURCEW(id), L"SHADER");
					if (!resource) return nullptr;
					size = SizeofResource(module, resource);
					const auto loaded = LoadResource(module, resource);
					return loaded ? LockResource(loaded) : nullptr;
				};
				std::size_t vsBytes = 0, psBytes = 0;
				const auto vs = shader(IDR_DRAW3_INK_VERTEX_SHADER, vsBytes);
				const auto ps = shader(IDR_DRAW3_INK_PIXEL_SHADER, psBytes);
				shader_["vertex_resource_id"] = IDR_DRAW3_INK_VERTEX_SHADER;
				shader_["pixel_resource_id"] = IDR_DRAW3_INK_PIXEL_SHADER;
				shader_["vertex_bytes"] = U64(vsBytes); shader_["pixel_bytes"] = U64(psBytes);
				auto hash = [](const void* bytes, std::size_t count)
				{
					std::uint64_t value = 14695981039346656037ull;
					for (std::size_t i = 0; i < count; ++i) value = (value ^ static_cast<const unsigned char*>(bytes)[i]) * 1099511628211ull;
					return std::to_string(value);
				};
				if (!vs || !ps) return Failure(E_FAIL, "embedded_shader_resource");
				shader_["vertex_fnv1a64"] = hash(vs, vsBytes); shader_["pixel_fnv1a64"] = hash(ps, psBytes);
				Call call; call.operation = "create"; call.site = "production_vertex_shader";
				if (!Failure(HCall(call, [&] { return device_->CreateVertexShader(vs, vsBytes, nullptr,
					vertex_.GetAddressOf()); }), call.site)) return false;
				call.site = "production_pixel_shader";
				if (!Failure(HCall(call, [&] { return device_->CreatePixelShader(ps, psBytes, nullptr,
					pixel_.GetAddressOf()); }), call.site)) return false;
				D3D11_BLEND_DESC blend{};
				blend.IndependentBlendEnable = TRUE;
				for (unsigned i = 0; i < 2; ++i)
				{
					auto& output = blend.RenderTarget[i]; output.BlendEnable = TRUE;
					output.SrcBlend = output.DestBlend = output.SrcBlendAlpha = output.DestBlendAlpha = D3D11_BLEND_ONE;
					output.BlendOp = output.BlendOpAlpha = i ? D3D11_BLEND_OP_MIN : D3D11_BLEND_OP_MAX;
					output.RenderTargetWriteMask = i ? D3D11_COLOR_WRITE_ENABLE_RED : D3D11_COLOR_WRITE_ENABLE_ALL;
				}
				if (test_.work == Work::Composite)
				{
					blend = {}; auto& output = blend.RenderTarget[0]; output.BlendEnable = TRUE;
					output.SrcBlend = output.SrcBlendAlpha = D3D11_BLEND_ONE;
					output.DestBlend = D3D11_BLEND_SRC1_COLOR; output.DestBlendAlpha = D3D11_BLEND_SRC1_ALPHA;
					output.BlendOp = output.BlendOpAlpha = D3D11_BLEND_OP_ADD;
					output.RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
				}
				if (!Failure(device_->CreateBlendState(&blend, blend_.GetAddressOf()), "create_production_blend")) return false;
				D3D11_RASTERIZER_DESC raster{}; raster.FillMode = D3D11_FILL_SOLID;
				raster.CullMode = D3D11_CULL_NONE; raster.DepthClipEnable = TRUE;
				if (!Failure(device_->CreateRasterizerState(&raster, raster_.GetAddressOf()), "create_raster")) return false;
				D3D11_SAMPLER_DESC sampler{}; sampler.Filter = D3D11_FILTER_MIN_MAG_MIP_POINT;
				sampler.AddressU = sampler.AddressV = sampler.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
				sampler.ComparisonFunc = D3D11_COMPARISON_NEVER; sampler.MaxLOD = D3D11_FLOAT32_MAX;
				return Failure(device_->CreateSamplerState(&sampler, sampler_.GetAddressOf()), "create_sampler");
			}
			void CreateQueries()
			{
				D3D11_QUERY_DESC description{ D3D11_QUERY_EVENT, 0 };
				if (FAILED(device_->CreateQuery(&description, completion_.GetAddressOf()))) gpuStatus_ = "query_failed";
				for (auto& sample : gpu_)
				{
					description.Query = D3D11_QUERY_TIMESTAMP_DISJOINT;
					if (FAILED(device_->CreateQuery(&description, sample.disjoint.GetAddressOf()))) { sample.status = "query_failed"; continue; }
					description.Query = D3D11_QUERY_TIMESTAMP;
					if (FAILED(device_->CreateQuery(&description, sample.begin.GetAddressOf())) ||
						FAILED(device_->CreateQuery(&description, sample.end.GetAddressOf()))) sample.status = "query_failed";
				}
			}
			void BeginGpu(unsigned index)
			{
				auto& sample = gpu_[index];
				if (!sample.disjoint || !sample.begin || !sample.end) return;
				context_->Begin(sample.disjoint.Get()); context_->End(sample.begin.Get()); sample.status = "pending";
			}
			void EndGpu(unsigned index)
			{
				auto& sample = gpu_[index];
				if (std::strcmp(sample.status, "pending") != 0) return;
				context_->End(sample.end.Get()); context_->End(sample.disjoint.Get());
			}
			Constants ConstantData(unsigned offset) const noexcept
			{
				Constants value{};
				value.width = static_cast<float>(test_.width); value.height = static_cast<float>(test_.height);
				value.shape = test_.work == Work::Composite ? 1.0f : test_.work == Work::Highlighter ? 3.0f : 0.0f;
				value.offset = offset / (test_.work == Work::Highlighter ? 24 : 16);
				value.color[0] = 0.2f; value.color[1] = 0.5f; value.color[2] = 0.8f; value.color[3] = 0.8f;
				return value;
			}
			void WriteConstants(void* destination, const Constants& value, Write method)
			{
				if (method == Write::Memcpy) { std::memcpy(destination, &value, sizeof(value)); return; }
				if (method == Write::Volatile)
				{
					// 严格只写，避免对 write-combined 内存引入 read-modify-write。
					std::array<std::uint32_t, 12> words{}; std::memcpy(words.data(), &value, 48);
					auto* output = static_cast<volatile std::uint32_t*>(destination);
					for (unsigned i = 0; i < words.size(); ++i) output[i] = words[i];
					return;
				}
				auto* output = static_cast<Constants*>(destination);
				output->width = value.width; output->height = value.height;
				std::memcpy(output->color, value.color, sizeof(value.color));
				output->shape = value.shape; output->offset = value.offset; output->operation = value.operation;
			}
			bool UpdateBuffer(ID3D11Buffer* buffer, unsigned id, bool constant, unsigned offset)
			{
				const auto value = ConstantData(offset);
				const unsigned bytes = constant ? (test_.work == Work::Constant ? test_.writeBytes : 36) : test_.writeBytes;
				std::array<unsigned char, 4096> data{};
				if (constant) std::memcpy(data.data(), &value, 48);
				else if (test_.work == Work::Highlighter)
				{
					const Highlighter primitive{ 8, 8, 56, 56, 2, 8 }; std::memcpy(data.data(), &primitive, 24);
					std::memcpy(data.data() + 24, &primitive, 24);
				}
				else
				{
					const Point points[] = { { 8, 8, 3, 0 }, { 56, 56, 3, 0 } };
					std::memcpy(data.data(), points, sizeof(points));
				}
				Call call; call.resource = id; call.bytes = bytes;
				// shader 的 structured offset 同样写入 CB，但常量数据实际始终从缓冲首地址写。
				call.offset = constant ? 0 : offset;
				if (buffer == primary_.Get() && test_.update == Update::Default)
				{
					call.operation = "update_subresource"; call.site = constant ? "default_constant48" : "default_ink";
					D3D11_BOX box{ offset, 0, 0, offset + bytes, 1, 1 };
					// DEFAULT 常量缓冲必须整体更新；结构化 buffer 才使用字节 box。
					VCall(call, [&] { context_->UpdateSubresource(buffer, 0, constant ? nullptr : &box, data.data(), 0, 0); });
					return true;
				}
				D3D11_MAPPED_SUBRESOURCE mapped{};
				call.operation = "map"; call.site = constant ? "map_constant48" : "map_structured";
				call.map = buffer == primary_.Get() && test_.update == Update::NoOverwrite && iteration_ > 0
					? D3D11_MAP_WRITE_NO_OVERWRITE : D3D11_MAP_WRITE_DISCARD;
				if (!Failure(HCall(call, [&] { return context_->Map(buffer, 0,
					static_cast<D3D11_MAP>(call.map), 0, &mapped); }), call.site)) return false;
				call.operation = constant ? "write_constant" : "write_copy";
				call.site = constant ? WriteName(test_.work == Work::Constant ? test_.write : Write::Fields) : "memcpy_points";
				VCall(call, [&] {
					if (constant) WriteConstants(mapped.pData, value, test_.work == Work::Constant ? test_.write : Write::Fields);
					else std::memcpy(static_cast<unsigned char*>(mapped.pData) + offset, data.data(), bytes);
				});
				call.operation = "unmap"; call.site = constant ? "unmap_constant48" : "unmap_structured";
				VCall(call, [&] { context_->Unmap(buffer, 0); });
				return true;
			}
			bool Iterate()
			{
				if (test_.work == Work::ClearCopy || test_.work == Work::Readback) return TextureIteration();
				for (unsigned draw = 0; draw < (std::max)(1u, test_.draws); ++draw)
				{
					if (Expired()) return false;
					// 所有 NO_OVERWRITE 区域在本 case 生命周期中只用一次，不回卷覆盖在途 GPU 数据。
					const unsigned offset = test_.update == Update::NoOverwrite ? (iteration_ * 6 + draw) * 32 : 0;
					if (!UpdateBuffer(primary_.Get(), 1, test_.work == Work::Constant, offset)) return false;
					if (!test_.draws) continue;
					if (test_.work != Work::Constant && !UpdateBuffer(global_.Get(), 2, true, offset)) return false;
					BindAndDraw();
				}
				return true;
			}
			void BindAndDraw()
			{
				Call call; call.operation = "bind"; call.resource = 3;
				ID3D11RenderTargetView* outputs[] = { targetRTV_.Get(), retainRTV_.Get() };
				call.site = "om_targets"; VCall(call, [&] { context_->OMSetRenderTargets(test_.work == Work::Composite ? 1 : 2, outputs, nullptr); });
				call.site = "ia_layout"; VCall(call, [&] { context_->IASetInputLayout(nullptr); });
				call.site = "ia_topology"; VCall(call, [&] { context_->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST); });
				call.site = "vs_shader"; VCall(call, [&] { context_->VSSetShader(vertex_.Get(), nullptr, 0); });
				ID3D11ShaderResourceView* source = primarySRV_.Get(); call.resource = test_.work == Work::Constant ? 7 : 1;
				call.site = "vs_srv"; VCall(call, [&] { context_->VSSetShaderResources(test_.work == Work::Highlighter ? 3 : 0, 1, &source); });
				ID3D11Buffer* constants = global_.Get(); call.resource = test_.work == Work::Constant ? 1 : 2;
				call.site = "vs_constants"; VCall(call, [&] { context_->VSSetConstantBuffers(0, 1, &constants); });
				call.site = "ps_shader"; VCall(call, [&] { context_->PSSetShader(pixel_.Get(), nullptr, 0); });
				call.site = "ps_constants"; VCall(call, [&] { context_->PSSetConstantBuffers(0, 1, &constants); });
				if (test_.work == Work::Composite)
				{
					ID3D11ShaderResourceView* sources[] = { copySRV_.Get(), retainSRV_.Get(), nullptr, copySRV_.Get(), retainSRV_.Get() };
					call.site = "ps_composite_sources"; call.resource = 5; call.related = 4;
					VCall(call, [&] { context_->PSSetShaderResources(1, 5, sources); });
				}
				ID3D11SamplerState* sampler = sampler_.Get(); call.site = "ps_sampler";
				VCall(call, [&] { context_->PSSetSamplers(0, 1, &sampler); });
				call.site = "om_blend"; VCall(call, [&] { context_->OMSetBlendState(blend_.Get(), nullptr, 0xffffffff); });
				call.site = "rs_state"; VCall(call, [&] { context_->RSSetState(raster_.Get()); });
				D3D11_VIEWPORT viewport{ 0, 0, static_cast<float>(test_.width), static_cast<float>(test_.height), 0, 1 };
				call.site = "rs_viewport"; VCall(call, [&] { context_->RSSetViewports(1, &viewport); });
				call.operation = "draw"; call.site = "production_shader_draw"; call.resource = test_.work == Work::Constant ? 7 : 1;
				call.vertices = 6; call.instances = 1; VCall(call, [&] { context_->Draw(6, 0); }); ++gpu_[iteration_].draws;
				ID3D11ShaderResourceView* nulls[6]{};
				call.operation = "bind"; call.site = "vs_unbind"; VCall(call, [&] { context_->VSSetShaderResources(0, 6, nulls); });
				call.site = "ps_unbind"; VCall(call, [&] { context_->PSSetShaderResources(0, 6, nulls); });
			}
			bool TextureIteration()
			{
				Call call; call.resource = 3; call.operation = "clear"; call.site = "clear_bgra";
				const float clear[4] = { 0.1f, 0.2f, 0.3f, 0.5f };
				VCall(call, [&] { context_->ClearRenderTargetView(targetRTV_.Get(), clear); });
				call.resource = 4; call.site = "clear_r16f";
				VCall(call, [&] { context_->ClearRenderTargetView(retainRTV_.Get(), clear); });
				call.operation = "copy"; call.resource = test_.work == Work::Readback ? 6 : 5; call.related = 3;
				call.bytes = test_.width * test_.height * 4; call.site = "copy_resource";
				VCall(call, [&] { context_->CopyResource(test_.work == Work::Readback ? staging_.Get() : copy_.Get(), target_.Get()); });
				if (test_.work != Work::Readback)
				{
					const D3D11_BOX box{ 0, 0, 0, 64, 64, 1 }; call.site = "copy_region64"; call.bytes = 64 * 64 * 4;
					VCall(call, [&] { context_->CopySubresourceRegion(copy_.Get(), 0, 0, 0, 0, target_.Get(), 0, &box); });
					return true;
				}
				D3D11_MAPPED_SUBRESOURCE mapped{}; call.operation = "map"; call.site = "staging_read"; call.map = D3D11_MAP_READ;
				if (!Failure(HCall(call, [&] { return context_->Map(staging_.Get(), 0, D3D11_MAP_READ, 0, &mapped); }), call.site)) return false;
				call.operation = "write_copy"; call.site = "staging_rows_to_ram"; call.flags = mapped.RowPitch;
				VCall(call, [&] { for (unsigned row = 0; row < 64; ++row)
					std::memcpy(readback_.data() + row * 256, static_cast<const unsigned char*>(mapped.pData) + row * mapped.RowPitch, 256); });
				call.operation = "unmap"; call.site = "staging_unmap";
				VCall(call, [&] { context_->Unmap(staging_.Get(), 0); }); return true;
			}
			void Drain()
			{
				if (!context_) return;
				if (!test_.draws && test_.work != Work::ClearCopy && test_.work != Work::Readback)
				{ drainComplete_ = true; return; }
				if (completion_) context_->End(completion_.Get());
				Call call; call.operation = "flush"; call.site = "probe_only_batch_flush";
				VCall(call, [&] { context_->Flush(); });
				if (completion_)
				{
					call.operation = "completion_wait"; call.site = "probe_only_event_donotflush";
					EventCompletion completion;
					const auto result = HCall(call, [&] {
						completion = WaitForEvent([&](BOOL* ready) {
							return context_->GetData(completion_.Get(), ready, sizeof(*ready), D3D11_ASYNC_GETDATA_DONOTFLUSH);
						}, [&] { return GetTickCount64() < deadline_; }, [] { Sleep(1); });
						return completion.result;
					});
					polls_ += completion.polls;
					drainComplete_ = completion.complete; if (!drainComplete_) { partial_ = true; reason_ = "gpu_drain_incomplete"; }
					if (FAILED(result)) Failure(result, "gpu_completion_query");
				}
				for (auto& sample : gpu_)
				{
					if (std::strcmp(sample.status, "pending") != 0) continue;
					D3D11_QUERY_DATA_TIMESTAMP_DISJOINT disjoint{};
					const auto d = context_->GetData(sample.disjoint.Get(), &disjoint, sizeof(disjoint), D3D11_ASYNC_GETDATA_DONOTFLUSH);
					const auto a = context_->GetData(sample.begin.Get(), &sample.first, sizeof(sample.first), D3D11_ASYNC_GETDATA_DONOTFLUSH);
					const auto b = context_->GetData(sample.end.Get(), &sample.last, sizeof(sample.last), D3D11_ASYNC_GETDATA_DONOTFLUSH);
					if (FAILED(d) || FAILED(a) || FAILED(b)) sample.status = "query_failed";
					else if (d != S_OK || a != S_OK || b != S_OK) sample.status = "pending";
					else if (disjoint.Disjoint) sample.status = "disjoint";
					else if (!disjoint.Frequency || sample.last < sample.first) sample.status = "invalid";
					else { sample.status = "ready"; sample.frequency = disjoint.Frequency; }
				}
			}
			bool RawFiles()
			{
				std::ostringstream calls;
				calls << "call_serial,phase,iteration,operation,site,resource_id,related_resource_id,start_qpc,end_qpc,valid_interval,hresult_available,hresult,map_type,map_flags,subresource,offset_bytes,size_bytes,vertex_count,instance_count\r\n";
				for (unsigned i = 0; i < retained_; ++i)
				{
					const auto& call = calls_[i]; calls << i + 1 << ',' << call.phase << ',' << call.iteration << ',' << call.operation << ',' << call.site
						<< ',' << call.resource << ',' << call.related << ',' << call.start << ',' << call.end << ','
						<< (call.start > 0 && call.end >= call.start && frequency_ > 0 ? 1 : 0) << ',' << (call.hasResult ? 1 : 0) << ',';
					if (call.hasResult) calls << static_cast<int>(call.result);
					calls << ',' << call.map << ',' << call.flags << ",0," << call.offset << ',' << call.bytes << ',' << call.vertices << ',' << call.instances << "\r\n";
				}
				std::ostringstream resources;
				resources << "resource_id,role,resource_kind,byte_width,width,height,format,usage,bind_flags,cpu_access_flags,misc_flags,structure_byte_stride,create_hresult\r\n";
				for (const auto& resource : resources_)
				{
					resources << resource.id << ',' << resource.role << ',' << (resource.isBuffer ? "buffer" : "texture2d") << ',';
					if (resource.isBuffer) resources << resource.buffer.ByteWidth << ",0,0,0," << resource.buffer.Usage << ',' << resource.buffer.BindFlags << ','
						<< resource.buffer.CPUAccessFlags << ',' << resource.buffer.MiscFlags << ',' << resource.buffer.StructureByteStride;
					else resources << "0," << resource.texture.Width << ',' << resource.texture.Height << ',' << resource.texture.Format << ',' << resource.texture.Usage << ','
						<< resource.texture.BindFlags << ',' << resource.texture.CPUAccessFlags << ',' << resource.texture.MiscFlags << ",0";
					resources << ',' << static_cast<int>(resource.result) << "\r\n";
				}
				std::ostringstream gpu; gpu << "phase,iteration,status,frequency,begin_timestamp,end_timestamp,draw_calls,marker_mask\r\n";
				for (unsigned i = 0; i < gpu_.size(); ++i)
				{
					const auto& sample = gpu_[i]; gpu << (i == 0 ? "cold" : i == 1 ? "warmup" : "measure") << ',' << i << ',' << sample.status << ','
						<< sample.frequency << ',' << sample.first << ',' << sample.last << ',' << sample.draws << ','
						<< (std::strcmp(sample.status, "not_started") && std::strcmp(sample.status, "query_failed") ? 3 : 0) << "\r\n";
				}
				return WriteBytes(Path(L"calls.csv"), calls.str()) && WriteBytes(Path(L"resources.csv"), resources.str()) && WriteBytes(Path(L"gpu.csv"), gpu.str());
			}
			int Export()
			{
				const auto end = Qpc(); const auto threadEnd = CpuTime(false), processEnd = CpuTime(true);
				auto result = identity_; int exit = unsupported_ ? 3 : FAILED(firstResult_) ? 4 : partial_ ? 5 : 0;
				if (completed_[2] != 4 && exit == 0) { exit = 5; reason_ = "iterations_incomplete"; }
				if (exit == 0 && (invalid_ || frequency_ <= 0 || start_ <= 0 || end < start_))
				{ exit = 5; reason_ = "invalid_clock_intervals"; }
				result["actual_backend"] = actualBackend_;
				result["status"] = exit == 0 ? "complete" : exit == 3 ? "unsupported" : exit == 5 ? "partial" : "failed";
				result["exit_code"] = exit; result["reason"] = reason_;
				result["first_hresult"] = static_cast<int>(firstResult_); result["first_failure_stage"] = firstStage_;
				result["qpc_frequency"] = Json::Int64(frequency_); result["start_qpc"] = Json::Int64(start_); result["end_qpc"] = Json::Int64(end);
				result["thread_cpu_available"] = threadStart_.available && threadEnd.available && threadEnd.value >= threadStart_.value;
				result["process_cpu_available"] = processStart_.available && processEnd.available && processEnd.value >= processStart_.value;
				result["thread_cpu_ms"] = result["thread_cpu_available"].asBool() ? Json::Value((threadEnd.value - threadStart_.value) / 10000.0) : Json::Value();
				result["process_cpu_ms"] = result["process_cpu_available"].asBool() ? Json::Value((processEnd.value - processStart_.value) / 10000.0) : Json::Value();
				result["wall_time_available"] = frequency_ > 0 && start_ > 0 && end >= start_;
				result["cpu_timing_scope"] = "worker_entry_through_measurement_and_drain_includes_checkpoint_io";
				result["adapter"] = adapter_; result["feature_level"] = static_cast<unsigned>(feature_);
				result["device_creation_flags"] = static_cast<unsigned>(D3D11_CREATE_DEVICE_BGRA_SUPPORT);
				result["caps"]["query_hresult"] = static_cast<int>(capsResult_);
				result["caps"]["map_no_overwrite_dynamic_srv"] = SUCCEEDED(capsResult_) && caps_.MapNoOverwriteOnDynamicBufferSRV;
				result["caps"]["map_no_overwrite_dynamic_cb"] = SUCCEEDED(capsResult_) && caps_.MapNoOverwriteOnDynamicConstantBuffer;
				result["shader"] = shader_; result["cold_iterations_completed"] = completed_[0];
				result["warmup_iterations_completed"] = completed_[1]; result["measured_iterations_completed"] = completed_[2];
				result["calls_seen"] = seen_; result["calls_retained"] = retained_; result["calls_dropped"] = seen_ - retained_;
				result["invalid_intervals"] = invalid_; result["drain_complete"] = drainComplete_;
				result["gpu_status"] = gpuStatus_; result["completion_poll_count"] = polls_;
				result["gpu_timing_scope"] = "immediate_command_stream_includes_cpu_submission_gaps_probe_only_single_batch_flush";
				result["native_budget_ms"] = kBudgetMs; result["removed_reason"] = device_ ? static_cast<int>(device_->GetDeviceRemovedReason()) : 0;
				std::uint64_t checksum = 14695981039346656037ull;
				for (auto value : readback_) checksum = (checksum ^ value) * 1099511628211ull;
				result["ram_readback_checksum"] = std::to_string(checksum);
				if (!RawFiles() || ioFailed_) { result["status"] = "failed"; result["exit_code"] = 4; result["reason"] = "export_failed"; exit = 4; }
				return AtomicJson(Path(L"probe-result.json"), result) ? exit : 4;
			}
			const Case& test_; bool warp_ = false;
			std::wstring directory_, session_;
			std::unique_ptr<Call[]> calls_; std::vector<Resource> resources_;
			unsigned seen_ = 0, retained_ = 0, invalid_ = 0, iteration_ = 0, completed_[3]{}, polls_ = 0;
			std::int64_t start_ = 0, frequency_ = 0;
			std::uint64_t deadline_ = 0; CpuStamp threadStart_{}, processStart_{};
			const char* phase_ = "initialization";
			const char* reason_ = ""; const char* firstStage_ = "";
			const char* actualBackend_ = ""; const char* gpuStatus_ = "recorded_per_iteration";
			bool partial_ = false, unsupported_ = false, ioFailed_ = false, drainComplete_ = false, identityValid_ = false;
			HRESULT firstResult_ = S_OK, capsResult_ = E_NOTIMPL;
			D3D_FEATURE_LEVEL feature_{}; D3D11_FEATURE_DATA_D3D11_OPTIONS caps_{};
			Json::Value identity_, adapter_, shader_;
			ComPtr<ID3D11Device> device_; ComPtr<ID3D11DeviceContext> context_;
			ComPtr<ID3D11Buffer> primary_, global_, immutable_;
			ComPtr<ID3D11Texture2D> target_, retain_, copy_, staging_;
			ComPtr<ID3D11RenderTargetView> targetRTV_, retainRTV_;
			ComPtr<ID3D11ShaderResourceView> primarySRV_, retainSRV_, copySRV_;
			ComPtr<ID3D11VertexShader> vertex_; ComPtr<ID3D11PixelShader> pixel_;
			ComPtr<ID3D11BlendState> blend_; ComPtr<ID3D11RasterizerState> raster_;
			ComPtr<ID3D11SamplerState> sampler_; ComPtr<ID3D11Query> completion_;
			std::array<Gpu, kIterations> gpu_{};
			std::array<unsigned char, 64 * 64 * 4> readback_{};
		};
		int CpuTest()
		{
			unsigned failures = 0;
			// 注入真实等待接缝，验证 S_OK/FALSE、S_FALSE、期限与错误，不接触 D3D 或窗口。
			unsigned eventPolls = 0, eventPauses = 0;
			const auto eventual = WaitForEvent([&](BOOL* ready) -> HRESULT {
				++eventPolls; *ready = eventPolls == 3 ? TRUE : FALSE;
				return eventPolls == 2 ? S_FALSE : S_OK;
			}, [&] { return eventPolls < 4; }, [&] { ++eventPauses; });
			if (!eventual.complete || eventual.result != S_OK || eventual.polls != 3 || eventPauses != 2) ++failures;
			eventPolls = eventPauses = 0;
			const auto deadline = WaitForEvent([&](BOOL* ready) -> HRESULT {
				++eventPolls; *ready = FALSE; return S_OK;
			}, [&] { return eventPolls < 2; }, [&] { ++eventPauses; });
			if (deadline.complete || deadline.result != S_OK || deadline.polls != 2 || eventPauses != 2) ++failures;
			const auto failed = WaitForEvent([](BOOL*) -> HRESULT { return E_FAIL; }, [] { return true; }, [&] { ++eventPauses; });
			if (failed.complete || failed.result != E_FAIL || failed.polls != 1 || eventPauses != 2) ++failures;
			const auto expired = WaitForEvent([](BOOL*) -> HRESULT { return S_OK; }, [] { return false; }, [&] { ++eventPauses; });
			if (expired.complete || expired.result != S_FALSE || expired.polls || eventPauses != 2) ++failures;
			const auto list = CaseList();
			if (list["cases"].size() != 40 || list["probe_revision"].asInt() != 2) ++failures;
			std::vector<std::string> names;
			for (const auto& entry : list["cases"])
			{
				const auto name = entry["case_id"].asString();
				if (std::find(names.begin(), names.end(), name) != names.end()) ++failures;
				names.push_back(name); bool warp = false; std::wstring wide(name.begin(), name.end());
				const auto test = FindCase(wide, warp);
				if (!test || Definition(*test, warp) != entry) ++failures;
				if (test && test->update == Update::NoOverwrite && 6u * 6u * 32u > test->bytes) ++failures;
			}
			bool warp = false;
			if (FindCase(L"hardware-unknown", warp) || SessionValid(L"../x") || SessionValid(L"") ||
				SessionValid(std::wstring(129, L'a')) || !SessionValid(L"draw3-20261015_1234.abcd")) ++failures;
			auto rejected = [&](std::initializer_list<const wchar_t*> arguments) {
				std::vector<wchar_t*> argv;
				for (auto* value : arguments) argv.push_back(const_cast<wchar_t*>(value));
				int exit = -1;
				if (!TryRunDraw3ResourceProbeEarly(static_cast<int>(argv.size()), argv.data(), exit) || exit != 2) ++failures;
			};
			rejected({ L"Inkeys.exe", L"--DRAW3-RESOURCE-PROBE-LIST" });
			rejected({ L"Inkeys.exe", L"--draw3-resource-probe-unknown" });
			rejected({ L"Inkeys.exe", L"--draw3-resource-probe-list", L"extra" });
			rejected({ L"Inkeys.exe", L"extra", L"--draw3-resource-probe-test" });
			rejected({ L"Inkeys.exe", L"--draw3-resource-probe" });
			rejected({ L"Inkeys.exe", L"--draw3-resource-probe", L"hardware-unknown", L"relative", L"valid" });
			rejected({ L"Inkeys.exe", L"--draw3-resource-probe", L"hardware-ink_large_discard_draw", L"relative", L"valid" });
			rejected({ L"Inkeys.exe", L"--draw3-resource-probe", L"hardware-ink_large_discard_draw", L"relative", L"../bad" });
			wchar_t normalName[] = L"Inkeys.exe", normalFlag[] = L"--some-other-product-flag";
			wchar_t* normal[] = { normalName, normalFlag }; int normalExit = 99;
			if (TryRunDraw3ResourceProbeEarly(2, normal, normalExit) || normalExit != 99) ++failures;
			// JSON 整数和显式未知值也是父进程身份/状态协议的一部分。
			auto protocol = Envelope(); protocol["final_pid"] = 123u; protocol["exit_code"] = 5;
			protocol["process_start_filetime_utc"] = "133000000000000000"; protocol["thread_cpu_ms"] = Json::Value();
			Json::CharReaderBuilder reader; Json::Value parsed; std::string errors;
			std::istringstream stream(JsonText(protocol));
			if (!Json::parseFromStream(reader, stream, &parsed, &errors) || !parsed["final_pid"].isUInt() ||
				!parsed["exit_code"].isInt() || !parsed["thread_cpu_ms"].isNull() ||
				parsed["process_start_filetime_utc"].asString() != "133000000000000000") ++failures;
			auto result = Envelope(); result["test"] = "cpu_protocol_only";
			result["failures"] = failures; result["status"] = failures ? "failed" : "complete";
			return Stdout(JsonText(result)) && !failures ? 0 : 1;
		}
	}

	bool TryRunDraw3ResourceProbeEarly(int argc, wchar_t** argv, int& exitCode) noexcept
	{
		// 不把非法探针参数落入普通启动，否则会意外打开产品窗口。
		bool found = false;
		for (int i = 1; argv && i < argc; ++i)
			if (argv[i])
			{
				constexpr std::wstring_view prefix = L"--draw3-resource-probe";
				const std::wstring_view value(argv[i]);
				if (value.size() >= prefix.size())
				{
					bool match = true;
					for (std::size_t j = 0; j < prefix.size(); ++j)
					{
						const wchar_t ch = value[j] >= L'A' && value[j] <= L'Z' ? value[j] + (L'a' - L'A') : value[j];
						if (ch != prefix[j]) { match = false; break; }
					}
					if (match) found = true;
				}
			}
		if (!found) return false;
		exitCode = 2;
		try
		{
			if (argc == 2 && std::wstring_view(argv[1]) == L"--draw3-resource-probe-list")
			{ exitCode = Stdout(JsonText(CaseList())) ? 0 : 4; return true; }
			if (argc == 2 && std::wstring_view(argv[1]) == L"--draw3-resource-probe-test")
			{ exitCode = CpuTest(); return true; }
			if (argc != 5 || std::wstring_view(argv[1]) != L"--draw3-resource-probe" || !SessionValid(argv[4])) return true;
			bool warp = false; const auto test = FindCase(argv[2], warp);
			const std::filesystem::path directory(argv[3]);
			if (!test || !directory.is_absolute() || !std::filesystem::is_directory(directory)) return true;
			Probe probe(*test, warp, std::filesystem::absolute(directory).lexically_normal().wstring(), argv[4]);
			exitCode = probe.Run();
		}
		catch (...) { exitCode = 4; }
		return true;
	}
}
