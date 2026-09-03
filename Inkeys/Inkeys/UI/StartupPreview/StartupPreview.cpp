module;

#include <windows.h>

#include "StartupPreview.CacheWrite.h"

#include <d2d1_1.h>
#include <d2d1effects.h>
#include <d2d1helper.h>
#include <dxgi.h>
#include <wrl/client.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <future>
#include <limits>
#include <memory>
#include <mutex>
#include <optional>
#include <span>
#include <string>
#include <thread>
#include <utility>
#include <vector>

module Inkeys.UI.StartupPreview;
import Inkeys.UI.RenderPipeline;

#pragma comment(lib, "dxguid.lib")

namespace Inkeys::UI::StartupPreview
{
	namespace
	{
		using namespace std::chrono_literals;
		using Inkeys::UI::RenderPipeline::Client;
		using Inkeys::UI::RenderPipeline::FrameContext;
		using Inkeys::UI::RenderPipeline::FrameResult;

		constexpr wchar_t PreviewClassName[] = L"Inkeys.StartupPreview.Owner.v1";
		constexpr UINT OwnerShowMessage = WM_APP + 0x351;
		constexpr UINT OwnerHideMessage = WM_APP + 0x352;
		constexpr UINT OwnerTopmostMessage = WM_APP + 0x353;
		constexpr UINT OwnerMoveMessage = WM_APP + 0x354;
		constexpr UINT OwnerStopMessage = WM_APP + 0x355;

		[[nodiscard]] bool IsSharedDeviceLoss(HRESULT result) noexcept
		{
			return result == DXGI_ERROR_DEVICE_REMOVED
				|| result == DXGI_ERROR_DEVICE_RESET
				|| result == DXGI_ERROR_DRIVER_INTERNAL_ERROR;
		}

		[[nodiscard]] int Width(const RECT& bounds) noexcept
		{
			return (std::max)(0l, bounds.right - bounds.left);
		}

		[[nodiscard]] int Height(const RECT& bounds) noexcept
		{
			return (std::max)(0l, bounds.bottom - bounds.top);
		}

		struct OwnerState final
		{
			std::mutex mutex;
			std::mutex presentationMutex;
			std::condition_variable condition;
			HWND window = nullptr;
			DWORD threadId = 0;
			RECT latestBounds{};
			RECT appliedBounds{};
			std::uint64_t latestMoveRevision = 0;
			std::uint64_t appliedMoveRevision = 0;
			bool ready = false;
			bool exited = false;
			bool creationSucceeded = false;
		};

		struct OwnerPresentationSnapshot final
		{
			HWND window = nullptr;
			RECT bounds{};
			std::uint64_t revision = 0;
		};

		LRESULT CALLBACK PreviewWindowProc(HWND window, UINT message,
			WPARAM wParam, LPARAM lParam)
		{
			switch (message)
			{
			case WM_NCCREATE:
			{
				const auto create = reinterpret_cast<CREATESTRUCTW*>(lParam);
				SetWindowLongPtrW(window, GWLP_USERDATA,
					reinterpret_cast<LONG_PTR>(create->lpCreateParams));
				return TRUE;
			}
			case WM_MOUSEACTIVATE:
				return MA_NOACTIVATEANDEAT;
			case WM_NCHITTEST:
				return HTCLIENT;
			case WM_LBUTTONDOWN:
			case WM_LBUTTONUP:
			case WM_LBUTTONDBLCLK:
			case WM_RBUTTONDOWN:
			case WM_RBUTTONUP:
			case WM_RBUTTONDBLCLK:
			case WM_MBUTTONDOWN:
			case WM_MBUTTONUP:
				return 0;
			case OwnerShowMessage:
				ShowWindow(window, SW_SHOWNOACTIVATE);
				return 0;
			case OwnerHideMessage:
				ShowWindow(window, SW_HIDE);
				return 0;
			case OwnerTopmostMessage:
				(void)SetWindowPos(window, HWND_TOPMOST, 0, 0, 0, 0,
					SWP_NOACTIVATE | SWP_NOMOVE | SWP_NOSIZE);
				return 0;
			case OwnerMoveMessage:
			{
				auto* state = reinterpret_cast<OwnerState*>(
					GetWindowLongPtrW(window, GWLP_USERDATA));
				if (!state) return 0;
				std::scoped_lock presentationLock(state->presentationMutex);
				RECT bounds{};
				std::uint64_t revision = 0;
				{
					std::scoped_lock lock(state->mutex);
					bounds = state->latestBounds;
					revision = state->latestMoveRevision;
					if (revision <= state->appliedMoveRevision) return 0;
				}
				if (SetWindowPos(window, HWND_TOPMOST, bounds.left, bounds.top,
					Width(bounds), Height(bounds), SWP_NOACTIVATE))
				{
					std::scoped_lock lock(state->mutex);
					if (revision > state->appliedMoveRevision)
					{
						state->appliedBounds = bounds;
						state->appliedMoveRevision = revision;
					}
				}
				return 0;
			}
			case OwnerStopMessage:
				ShowWindow(window, SW_HIDE);
				DestroyWindow(window);
				return 0;
			case WM_DESTROY:
				PostQuitMessage(0);
				return 0;
			default:
				return DefWindowProcW(window, message, wParam, lParam);
			}
		}

		class PreviewOwner final
		{
		public:
			~PreviewOwner() { Stop(); }

			[[nodiscard]] bool Start(HINSTANCE instance, const RECT& bounds)
			{
				if (thread_.joinable()) return false;
				state_ = std::make_shared<OwnerState>();
				state_->latestBounds = bounds;
				auto state = state_;
				thread_ = std::thread([state, instance]
					{
						state->threadId = GetCurrentThreadId();
						WNDCLASSEXW windowClass{};
						windowClass.cbSize = sizeof(windowClass);
						windowClass.hInstance = instance;
						windowClass.lpfnWndProc = PreviewWindowProc;
						windowClass.lpszClassName = PreviewClassName;
						windowClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
						const ATOM atom = RegisterClassExW(&windowClass);
						const DWORD registrationError = atom ? ERROR_SUCCESS : GetLastError();
						RECT initialBounds{};
						{
							std::scoped_lock lock(state->mutex);
							initialBounds = state->latestBounds;
						}
						HWND window = nullptr;
						if (atom || registrationError == ERROR_CLASS_ALREADY_EXISTS)
						{
							window = CreateWindowExW(
								WS_EX_LAYERED | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE,
								PreviewClassName, L"", WS_POPUP,
								initialBounds.left, initialBounds.top,
								Width(initialBounds), Height(initialBounds),
								nullptr, nullptr, instance, state.get());
							if (window)
							{
								// HWND_TOPMOST 是 SetWindowPos 插入位，不可作为 popup owner 传入。
								(void)SetWindowPos(window, HWND_TOPMOST, 0, 0, 0, 0,
									SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
							}
						}
						{
							std::scoped_lock lock(state->mutex);
							state->window = window;
							state->creationSucceeded = window != nullptr;
							state->appliedBounds = initialBounds;
							state->ready = true;
						}
						state->condition.notify_all();
						if (window)
						{
							MSG message{};
							while (GetMessageW(&message, nullptr, 0, 0) > 0)
							{
								TranslateMessage(&message);
								DispatchMessageW(&message);
							}
						}
						{
							std::scoped_lock lock(state->mutex);
							state->window = nullptr;
							state->exited = true;
						}
						state->condition.notify_all();
					});
				std::unique_lock lock(state_->mutex);
				if (!state_->condition.wait_for(lock, 2s,
					[this] { return state_->ready; })) return false;
				return state_->creationSucceeded;
			}

			void Stop() noexcept
			{
				if (!thread_.joinable()) return;
				auto state = state_;
				HWND window = nullptr;
				DWORD threadId = 0;
				{
					std::scoped_lock lock(state->mutex);
					window = state->window;
					threadId = state->threadId;
				}
				if (window) PostMessageW(window, OwnerStopMessage, 0, 0);
				else if (threadId) PostThreadMessageW(threadId, WM_QUIT, 0, 0);
				std::unique_lock lock(state->mutex);
				const bool exited = state->condition.wait_for(lock, 1500ms,
					[&state] { return state->exited; });
				lock.unlock();
				if (exited) thread_.join();
				else thread_.detach();
				state_.reset();
			}

			[[nodiscard]] OwnerPresentationSnapshot Snapshot() const noexcept
			{
				if (!state_) return {};
				std::scoped_lock lock(state_->mutex);
				return { state_->window, state_->appliedBounds,
					state_->appliedMoveRevision };
			}

			[[nodiscard]] HWND Window() const noexcept { return Snapshot().window; }

			[[nodiscard]] BOOL PresentPixels(HDC sourceDc, BYTE alpha,
				UINT width, UINT height) noexcept
			{
				if (!state_ || !sourceDc || width == 0 || height == 0) return FALSE;
				auto state = state_;
				std::scoped_lock presentationLock(state->presentationMutex);
				HWND window = nullptr;
				RECT bounds{};
				{
					std::scoped_lock lock(state->mutex);
					window = state->window;
					bounds = state->appliedBounds;
				}
				if (!window || Width(bounds) != static_cast<int>(width)
					|| Height(bounds) != static_cast<int>(height)) return FALSE;

				POINT destination{ bounds.left, bounds.top };
				POINT sourcePoint{};
				SIZE size{ static_cast<LONG>(width), static_cast<LONG>(height) };
				BLENDFUNCTION blend{ AC_SRC_OVER, 0, alpha, AC_SRC_ALPHA };
				UPDATELAYEREDWINDOWINFO update{};
				update.cbSize = sizeof(update);
				// 每帧回显 owner 已提交几何，避免 DWM 接受空几何却不刷新 surface。
				update.pptDst = &destination;
				update.psize = &size;
				update.pptSrc = &sourcePoint;
				update.hdcSrc = sourceDc;
				update.pblend = &blend;
				update.dwFlags = ULW_ALPHA;
				return UpdateLayeredWindowIndirect(window, &update);
			}

			[[nodiscard]] bool HasExited() const noexcept
			{
				if (!state_) return false;
				std::scoped_lock lock(state_->mutex);
				return state_->exited;
			}

			void Show() noexcept { Post(OwnerShowMessage); }
			void Hide() noexcept { Post(OwnerHideMessage); }
			void RevalidateTopmost() noexcept { Post(OwnerTopmostMessage); }
			void RequestStop() noexcept { Post(OwnerStopMessage); }
			void RetryLatestMove() noexcept { Post(OwnerMoveMessage); }

			void Move(const RECT& bounds, std::uint64_t revision) noexcept
			{
				if (!state_) return;
				HWND window = nullptr;
				{
					std::scoped_lock lock(state_->mutex);
					if (revision <= state_->latestMoveRevision) return;
					state_->latestMoveRevision = revision;
					state_->latestBounds = bounds;
					window = state_->window;
				}
				if (window) PostMessageW(window, OwnerMoveMessage, 0, 0);
			}

		private:
			void Post(UINT message) noexcept
			{
				if (const auto window = Window()) PostMessageW(window, message, 0, 0);
			}

			std::shared_ptr<OwnerState> state_;
			std::thread thread_;
		};

		struct CacheWriteRequest final
		{
			std::uint64_t revision = 0;
			PreviewMetadata metadata{};
			std::vector<std::uint8_t> pixels;
			std::wstring targetPath;
			std::shared_ptr<std::atomic_bool> completion;
		};

		struct CacheWriterState final
		{
			std::mutex mutex;
			std::condition_variable condition;
			std::optional<CacheWriteRequest> pending;
			CacheWrite::LatestRevisionPolicy revisions;
			bool stopping = false;
			bool exited = false;
		};

		[[nodiscard]] bool WriteAllBytes(HANDLE file,
			std::span<const std::uint8_t> bytes) noexcept
		{
			std::size_t offset = 0;
			while (offset < bytes.size())
			{
				const auto remaining = bytes.size() - offset;
				const DWORD requested = static_cast<DWORD>((std::min<std::size_t>)(
					remaining, (std::numeric_limits<DWORD>::max)()));
				DWORD written = 0;
				if (!WriteFile(file, bytes.data() + offset, requested, &written, nullptr)
					|| written == 0) return false;
				offset += written;
			}
			return true;
		}

		class DurableFileOperations final
		{
		public:
			DurableFileOperations(const CacheWriteRequest& request,
				std::shared_ptr<CacheWriterState> state) noexcept
				: request_(request), state_(std::move(state)) {}

			[[nodiscard]] bool Prepare()
			{
				bytes_ = SerializePreview(request_.metadata, request_.pixels);
				return !bytes_.empty() && !request_.targetPath.empty()
					&& ParsePreview(bytes_).state == CacheState::Valid;
			}

			[[nodiscard]] bool EnsureParent() const noexcept
			{
				return CacheWrite::EnsureParentDirectory(request_.targetPath);
			}

			[[nodiscard]] bool CreateTemporary()
			{
				static std::atomic_uint64_t temporarySerial = 1;
				for (int attempt = 0; attempt < 8
					&& file_ == INVALID_HANDLE_VALUE; ++attempt)
				{
					temporary_ = request_.targetPath + L"."
					+ std::to_wstring(GetCurrentProcessId()) + L"."
					+ std::to_wstring(request_.revision) + L"."
					+ std::to_wstring(GetTickCount64()) + L"."
					+ std::to_wstring(temporarySerial.fetch_add(
						1, std::memory_order_relaxed)) + L".tmp";
					file_ = CreateFileW(temporary_.c_str(), GENERIC_WRITE, 0, nullptr,
					CREATE_NEW, FILE_ATTRIBUTE_TEMPORARY, nullptr);
				}
				return file_ != INVALID_HANDLE_VALUE;
			}

			[[nodiscard]] bool WriteAll() noexcept
			{
				return file_ != INVALID_HANDLE_VALUE
					&& WriteAllBytes(file_, bytes_);
			}

			[[nodiscard]] bool Flush() noexcept
			{
				return file_ != INVALID_HANDLE_VALUE && FlushFileBuffers(file_) != FALSE;
			}

			void Close() noexcept
			{
				if (file_ == INVALID_HANDLE_VALUE) return;
				CloseHandle(file_);
				file_ = INVALID_HANDLE_VALUE;
			}

			[[nodiscard]] bool IsLatest() const noexcept
			{
				std::scoped_lock lock(state_->mutex);
				return state_->revisions.IsLatest(request_.revision);
			}

			[[nodiscard]] bool Replace() noexcept
			{
				// 与 Submit 的 revision 推进共用锁，避免 latest 检查和 replace 间插入新帧。
				std::scoped_lock lock(state_->mutex);
				if (!state_->revisions.IsLatest(request_.revision)) return false;
				return MoveFileExW(temporary_.c_str(), request_.targetPath.c_str(),
					MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != FALSE;
			}

			void Cleanup() noexcept
			{
				if (!temporary_.empty()) DeleteFileW(temporary_.c_str());
			}

		private:
			const CacheWriteRequest& request_;
			std::shared_ptr<CacheWriterState> state_;
			std::vector<std::uint8_t> bytes_;
			std::wstring temporary_;
			HANDLE file_ = INVALID_HANDLE_VALUE;
		};

		[[nodiscard]] bool DurableReplace(const CacheWriteRequest& request,
			const std::shared_ptr<CacheWriterState>& state) noexcept
		{
			DurableFileOperations operations(request, state);
			return CacheWrite::ExecuteDurableTransaction(operations);
		}

		class CacheWriter final
		{
		public:
			~CacheWriter() { Stop(); }

			void Start()
			{
				if (thread_.joinable()) return;
				state_ = std::make_shared<CacheWriterState>();
				auto state = state_;
				thread_ = std::thread([state]
					{
						for (;;)
						{
							std::optional<CacheWriteRequest> request;
							{
								std::unique_lock lock(state->mutex);
								state->condition.wait(lock, [&state]
									{ return state->stopping || state->pending.has_value(); });
								if (state->stopping) break;
								request = std::move(state->pending);
								state->pending.reset();
							}
							const bool succeeded = DurableReplace(*request, state);
							if (request->completion && succeeded)
								request->completion->store(true, std::memory_order_release);
						}
						{
							std::scoped_lock lock(state->mutex);
							state->exited = true;
						}
						state->condition.notify_all();
					});
			}

			void Submit(CacheWriteRequest request) noexcept
			{
				if (!state_ || request.pixels.empty() || request.targetPath.empty()) return;
				std::scoped_lock lock(state_->mutex);
				if (state_->stopping || !state_->revisions.Accept(request.revision)) return;
				state_->pending = std::move(request);
				state_->condition.notify_one();
			}

			void Stop() noexcept
			{
				if (!thread_.joinable()) return;
				auto state = state_;
				{
					std::scoped_lock lock(state->mutex);
					state->stopping = true;
					state->pending.reset();
				}
				state->condition.notify_all();
				std::unique_lock lock(state->mutex);
				const bool exited = state->condition.wait_for(lock, 1500ms,
					[&state] { return state->exited; });
				lock.unlock();
				if (exited) thread_.join();
				else thread_.detach();
				state_.reset();
			}

		private:
			std::shared_ptr<CacheWriterState> state_;
			std::thread thread_;
		};

		[[nodiscard]] PreviewParseResult ReadPreviewFile(const std::wstring& path,
			const PreviewCompatibility& compatibility)
		{
			if (path.empty()) return MissingPreview();
			HANDLE file = CreateFileW(path.c_str(), GENERIC_READ,
				FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
				nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
			if (file == INVALID_HANDLE_VALUE)
			{
				const DWORD error = GetLastError();
				return error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND
					? MissingPreview() : PreviewParseResult{};
			}
			LARGE_INTEGER size{};
			if (!GetFileSizeEx(file, &size) || size.QuadPart < 0
				|| static_cast<std::uint64_t>(size.QuadPart)
					> PreviewHeaderSize + PreviewMaximumPayloadSize)
			{
				CloseHandle(file);
				PreviewParseResult result;
				result.error = PreviewFormatError::TooLarge;
				return result;
			}
			const auto fileSize = static_cast<std::uint64_t>(size.QuadPart);
			if (fileSize < PreviewHeaderSize)
			{
				std::vector<std::uint8_t> shortBytes(static_cast<std::size_t>(fileSize));
				DWORD read = 0;
				const bool readOk = shortBytes.empty()
					|| (ReadFile(file, shortBytes.data(), static_cast<DWORD>(shortBytes.size()),
						&read, nullptr) && read == static_cast<DWORD>(shortBytes.size()));
				CloseHandle(file);
				return readOk ? ParsePreview(shortBytes) : PreviewParseResult{};
			}

			std::array<std::uint8_t, PreviewHeaderSize> header{};
			DWORD headerRead = 0;
			if (!ReadFile(file, header.data(), static_cast<DWORD>(header.size()),
				&headerRead, nullptr) || headerRead != static_cast<DWORD>(header.size()))
			{
				CloseHandle(file);
				PreviewParseResult result;
				result.error = PreviewFormatError::TooSmall;
				return result;
			}
			std::uint16_t version = 0;
			std::uint16_t headerSize = 0;
			std::uint64_t payloadSize = 0;
			(void)ReadLe16(header, 8, version);
			(void)ReadLe16(header, 10, headerSize);
			(void)ReadLe64(header, 80, payloadSize);
			if (version == PreviewFormatVersion)
			{
				PreviewParseResult inspected;
				if (!TryReadPreviewMetadata(header, fileSize, inspected.metadata,
					inspected.error))
				{
					CloseHandle(file);
					return inspected;
				}
			}
			else
			{
				constexpr std::array<std::uint8_t, 8> magic{
					'I', 'K', 'S', 'P', 'R', 'V', 'W', 0 };
				const bool commonEnvelope = std::equal(
					magic.begin(), magic.end(), header.begin())
					&& headerSize == PreviewHeaderSize
					&& payloadSize <= PreviewMaximumPayloadSize
					&& PreviewHeaderSize + payloadSize == fileSize;
				if (!commonEnvelope)
				{
					CloseHandle(file);
					PreviewParseResult result;
					result.error = PreviewFormatError::UnsupportedVersion;
					return result;
				}
			}

			// 支持版本完成 header/算术/geometry 验证后才分配 payload。
			std::vector<std::uint8_t> bytes(static_cast<std::size_t>(fileSize));
			std::copy(header.begin(), header.end(), bytes.begin());
			std::size_t offset = PreviewHeaderSize;
			while (offset < bytes.size())
			{
				DWORD read = 0;
				const DWORD requested = static_cast<DWORD>((std::min<std::size_t>)(
					bytes.size() - offset, (std::numeric_limits<DWORD>::max)()));
				if (!ReadFile(file, bytes.data() + offset, requested, &read, nullptr)
					|| read == 0) break;
				offset += read;
			}
			CloseHandle(file);
			if (offset != bytes.size())
			{
				PreviewParseResult result;
				result.error = PreviewFormatError::TooSmall;
				return result;
			}
			return ParsePreview(bytes, &compatibility);
		}

		[[nodiscard]] PreviewParseResult ReadEmbeddedPreview(
			HINSTANCE instance, UINT resourceId)
		{
			HRSRC resource = FindResourceW(instance, MAKEINTRESOURCEW(resourceId),
				L"STARTUP_PREVIEW_BIN");
			if (!resource) return {};
			const DWORD size = SizeofResource(instance, resource);
			HGLOBAL loaded = LoadResource(instance, resource);
			const auto* data = loaded
				? static_cast<const std::uint8_t*>(LockResource(loaded)) : nullptr;
			if (!data || size == 0) return {};
			return ParsePreview(std::span(data, static_cast<std::size_t>(size)));
		}

		struct RenderResources final
		{
			std::uint64_t generation = 0;
			UINT width = 0;
			UINT height = 0;
			std::uint64_t sourceRevision = 0;
			ComPtr<ID2D1DeviceContext> context;
			ComPtr<ID2D1Bitmap1> target;
			ComPtr<ID2D1Bitmap1> scaledInput;
			ComPtr<ID2D1Bitmap1> source;
			ComPtr<ID2D1GdiInteropRenderTarget> gdi;
			ComPtr<ID2D1Effect> blur;
			ComPtr<ID2D1LinearGradientBrush> shimmer;
			ComPtr<ID2D1SolidColorBrush> progressBackground;
			ComPtr<ID2D1SolidColorBrush> progressFill;
			ComPtr<ID2D1SolidColorBrush> progressError;

			void Reset() noexcept
			{
				if (context) context->SetTarget(nullptr);
				progressError.Reset();
				progressFill.Reset();
				progressBackground.Reset();
				shimmer.Reset();
				blur.Reset();
				gdi.Reset();
				source.Reset();
				scaledInput.Reset();
				target.Reset();
				context.Reset();
				generation = 0;
				width = 0;
				height = 0;
				sourceRevision = 0;
			}
		};

		struct Runtime final : std::enable_shared_from_this<Runtime>
		{
			std::mutex mutex;
			StartOptions options;
			PreviewOwner owner;
			CacheWriter writer;
			CacheWriter captureWriter;
			PreviewMetadata sourceMetadata{};
			std::vector<std::uint8_t> sourcePixels;
			CacheState cacheState = CacheState::Missing;
			RECT paddedBounds{};
			std::uint64_t desiredBoundsRevision = 0;
			UINT padding = 36;
			float baseBlurSigma = 6.f;
			std::optional<CommittedBarFrame> pendingBarFrame;
			CommittedBarFrame barFrame;
			StateMachine lifecycle;
			std::unique_ptr<ProgressVisualReducer> progressVisual;
			RenderResources resources;
			std::chrono::steady_clock::time_point handoffStart{};
			HandoffFailureReducer handoffFailure;
			std::atomic<BarStartupState> barState = BarStartupState::NotStarted;
			std::atomic_int committedBarAlpha = -1;
			std::atomic_bool barTransparencyDecisionMade = false;
			std::atomic_bool active = false;
			std::atomic_bool presentationAvailable = false;
			std::atomic_bool registered = false;
			std::atomic_bool firstFrameCommitted = false;
			std::mutex presentationMutex;
			std::condition_variable presentationCondition;
			std::atomic_bool fatalRequested = false;
			std::atomic_bool automaticStopPosted = false;
			std::atomic_bool failureFrameCommitted = false;
			std::mutex failureMutex;
			std::condition_variable failureCondition;
			std::wstring developerCapturePath;
			std::shared_ptr<std::atomic_bool> developerCaptureCompleted;
			std::uint64_t lastSubmittedCacheRevision = 0;
		};

		std::mutex runtimeMutex;
		std::shared_ptr<Runtime> currentRuntime;
		std::wstring pendingDeveloperCapturePath;
		PreviewCompatibility pendingDeveloperCaptureCompatibility{};
		std::shared_ptr<std::atomic_bool> pendingDeveloperCaptureCompleted =
			std::make_shared<std::atomic_bool>(false);
		std::unique_ptr<CacheWriter> captureOnlyWriter;
		std::atomic<BarStartupState> globalBarStartupState =
			BarStartupState::NotStarted;

		[[nodiscard]] std::shared_ptr<Runtime> SnapshotRuntime() noexcept
		{
			std::scoped_lock lock(runtimeMutex);
			return currentRuntime;
		}

		[[nodiscard]] RECT PaddedBounds(const RECT& content, UINT padding) noexcept
		{
			return { content.left - static_cast<LONG>(padding),
				content.top - static_cast<LONG>(padding),
				content.right + static_cast<LONG>(padding),
				content.bottom + static_cast<LONG>(padding) };
		}

		[[nodiscard]] HRESULT EnsureResources(Runtime& runtime,
			const FrameContext& frame, const RECT& ownerBounds)
		{
			auto& resources = runtime.resources;
			const UINT width = static_cast<UINT>(Width(ownerBounds));
			const UINT height = static_cast<UINT>(Height(ownerBounds));
			if (width == 0 || height == 0 || !frame.epoch.d2dDevice)
				return E_INVALIDARG;
			if (resources.generation == frame.epoch.generation
				&& resources.width == width && resources.height == height
				&& resources.context && resources.target && resources.scaledInput
				&& resources.gdi) return S_OK;

			RenderResources next;
			next.generation = frame.epoch.generation;
			next.width = width;
			next.height = height;
			HRESULT result = frame.epoch.d2dDevice->CreateDeviceContext(
				D2D1_DEVICE_CONTEXT_OPTIONS_NONE, &next.context);
			if (FAILED(result)) return result;
			const auto targetProperties = D2D1::BitmapProperties1(
				D2D1_BITMAP_OPTIONS_TARGET | D2D1_BITMAP_OPTIONS_GDI_COMPATIBLE,
				D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM,
					D2D1_ALPHA_MODE_PREMULTIPLIED));
			result = next.context->CreateBitmap(D2D1::SizeU(width, height), nullptr,
				0, &targetProperties, &next.target);
			if (FAILED(result)) return result;
			const auto inputProperties = D2D1::BitmapProperties1(
				D2D1_BITMAP_OPTIONS_TARGET,
				D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM,
					D2D1_ALPHA_MODE_PREMULTIPLIED));
			const UINT inputWidth = width > runtime.padding * 2
				? width - runtime.padding * 2 : 0;
			const UINT inputHeight = height > runtime.padding * 2
				? height - runtime.padding * 2 : 0;
			if (inputWidth == 0 || inputHeight == 0) return E_INVALIDARG;
			result = next.context->CreateBitmap(D2D1::SizeU(inputWidth, inputHeight), nullptr,
				0, &inputProperties, &next.scaledInput);
			if (FAILED(result)) return result;
			result = next.context.As(&next.gdi);
			if (FAILED(result)) return result;
			result = next.context->CreateEffect(CLSID_D2D1GaussianBlur, &next.blur);
			if (SUCCEEDED(result))
			{
				(void)next.blur->SetValue(D2D1_GAUSSIANBLUR_PROP_OPTIMIZATION,
					D2D1_GAUSSIANBLUR_OPTIMIZATION_BALANCED);
				(void)next.blur->SetValue(D2D1_GAUSSIANBLUR_PROP_BORDER_MODE,
					D2D1_BORDER_MODE_SOFT);
			}
			std::array<D2D1_GRADIENT_STOP, 5> stops{
				D2D1::GradientStop(0.0f, D2D1::ColorF(1.f, 1.f, 1.f, 0.f)),
				D2D1::GradientStop(0.34f, D2D1::ColorF(1.f, 1.f, 1.f, 0.08f)),
				D2D1::GradientStop(0.50f, D2D1::ColorF(1.f, 1.f, 1.f, 0.30f)),
				D2D1::GradientStop(0.66f, D2D1::ColorF(1.f, 1.f, 1.f, 0.06f)),
				D2D1::GradientStop(1.0f, D2D1::ColorF(1.f, 1.f, 1.f, 0.f)),
			};
			ComPtr<ID2D1GradientStopCollection> stopCollection;
			result = next.context->CreateGradientStopCollection(stops.data(),
				static_cast<UINT32>(stops.size()), &stopCollection);
			if (SUCCEEDED(result))
				result = next.context->CreateLinearGradientBrush(
					D2D1::LinearGradientBrushProperties(
						D2D1::Point2F(0.f, 0.f), D2D1::Point2F(120.f, 40.f)),
					stopCollection.Get(), &next.shimmer);
			if (FAILED(next.context->CreateSolidColorBrush(
				D2D1::ColorF(1.f, 1.f, 1.f, 139.f / 255.f),
				&next.progressBackground)))
				return E_FAIL;
			if (FAILED(next.context->CreateSolidColorBrush(
				D2D1::ColorF(96.f / 255.f, 205.f / 255.f, 1.f, 1.f),
				&next.progressFill)))
				return E_FAIL;
			if (FAILED(next.context->CreateSolidColorBrush(
				D2D1::ColorF(1.f, 153.f / 255.f, 164.f / 255.f, 1.f),
				&next.progressError)))
				return E_FAIL;
			next.context->SetTarget(next.target.Get());
			next.context->SetAntialiasMode(D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
			resources.Reset();
			resources = std::move(next);
			return S_OK;
		}

		[[nodiscard]] HRESULT EnsureSourceBitmap(Runtime& runtime)
		{
			auto& resources = runtime.resources;
			if (resources.source && resources.sourceRevision == 1) return S_OK;
			if (runtime.sourcePixels.empty() || runtime.sourceMetadata.width == 0
				|| runtime.sourceMetadata.height == 0) return E_INVALIDARG;
			const auto properties = D2D1::BitmapProperties1(
				D2D1_BITMAP_OPTIONS_NONE,
				D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM,
					D2D1_ALPHA_MODE_PREMULTIPLIED),
				static_cast<float>(runtime.sourceMetadata.captureDpiX),
				static_cast<float>(runtime.sourceMetadata.captureDpiY));
			ComPtr<ID2D1Bitmap1> bitmap;
			const HRESULT result = resources.context->CreateBitmap(
				D2D1::SizeU(runtime.sourceMetadata.width, runtime.sourceMetadata.height),
				runtime.sourcePixels.data(), runtime.sourceMetadata.stride,
				&properties, &bitmap);
			if (FAILED(result)) return result;
			resources.source = std::move(bitmap);
			resources.sourceRevision = 1;
			return S_OK;
		}

		[[nodiscard]] HRESULT PrepareScaledInput(Runtime& runtime,
			ID2D1Bitmap1* bitmap, const D2D1_RECT_F& sourceRect)
		{
			auto& resources = runtime.resources;
			if (!bitmap) return E_POINTER;
			const auto destination = D2D1::RectF(
				0.f, 0.f,
				static_cast<float>(resources.width - runtime.padding * 2),
				static_cast<float>(resources.height - runtime.padding * 2));
			resources.context->SetTarget(resources.scaledInput.Get());
			resources.context->BeginDraw();
			resources.context->Clear(D2D1::ColorF(0.f, 0.f, 0.f, 0.f));
			resources.context->DrawBitmap(bitmap, destination, 1.f,
				D2D1_INTERPOLATION_MODE_CUBIC, sourceRect);
			const HRESULT result = resources.context->EndDraw();
			resources.context->SetTarget(resources.target.Get());
			return result;
		}

		void DrawPreparedInput(Runtime& runtime, float sigma)
		{
			auto& resources = runtime.resources;
			if (sigma > 0.01f && resources.blur)
			{
				resources.blur->SetInput(0, resources.scaledInput.Get());
				const HRESULT sigmaResult = resources.blur->SetValue(
					D2D1_GAUSSIANBLUR_PROP_STANDARD_DEVIATION, sigma);
				ComPtr<ID2D1Image> blurOutput;
				if (SUCCEEDED(sigmaResult)) resources.blur->GetOutput(&blurOutput);
				D2D1_RECT_F effectBounds{};
				const HRESULT boundsResult = blurOutput
					? resources.context->GetImageLocalBounds(
						blurOutput.Get(), &effectBounds) : E_FAIL;
				const auto inputSize = resources.scaledInput->GetPixelSize();
				const float padding = static_cast<float>(runtime.padding);
				const bool boundsFit = SUCCEEDED(boundsResult)
					&& std::isfinite(effectBounds.left) && std::isfinite(effectBounds.top)
					&& std::isfinite(effectBounds.right) && std::isfinite(effectBounds.bottom)
					&& effectBounds.left >= -padding && effectBounds.top >= -padding
					&& effectBounds.right <= static_cast<float>(inputSize.width) + padding
					&& effectBounds.bottom <= static_cast<float>(inputSize.height) + padding;
				if (boundsFit)
				{
					// effect 的负 origin 显式映射进 padding，Soft blur 不依赖近似裁剪。
					resources.context->DrawImage(blurOutput.Get(),
						D2D1::Point2F(padding + effectBounds.left,
							padding + effectBounds.top), effectBounds,
						D2D1_INTERPOLATION_MODE_LINEAR,
						D2D1_COMPOSITE_MODE_SOURCE_OVER);
					return;
				}
			}
			const auto inputSize = resources.scaledInput->GetPixelSize();
			resources.context->DrawBitmap(resources.scaledInput.Get(), D2D1::RectF(
				static_cast<float>(runtime.padding),
				static_cast<float>(runtime.padding),
				static_cast<float>(runtime.padding + inputSize.width),
				static_cast<float>(runtime.padding + inputSize.height)));
		}

		void DrawShimmer(Runtime& runtime,
			std::chrono::steady_clock::time_point frameTime)
		{
			auto& resources = runtime.resources;
			if (!resources.shimmer || !resources.scaledInput) return;
			constexpr double PeriodSeconds = 1.75;
			const double seconds = std::chrono::duration<double>(
				frameTime.time_since_epoch()).count();
			const double normalized = std::fmod(seconds, PeriodSeconds) / PeriodSeconds;
			const double phase = 0.5 - 0.5 * std::cos(3.141592653589793 * normalized);
			const float travel = static_cast<float>(resources.width + 320);
			resources.shimmer->SetStartPoint(D2D1::Point2F(
				-160.f + travel * static_cast<float>(phase), -40.f));
			resources.shimmer->SetEndPoint(D2D1::Point2F(
				-40.f + travel * static_cast<float>(phase),
				static_cast<float>(resources.height) + 40.f));
			const auto previous = resources.context->GetAntialiasMode();
			resources.context->SetAntialiasMode(D2D1_ANTIALIAS_MODE_ALIASED);
			const auto inputSize = resources.scaledInput->GetPixelSize();
			const auto destination = D2D1::RectF(
				static_cast<float>(runtime.padding),
				static_cast<float>(runtime.padding),
				static_cast<float>(runtime.padding + inputSize.width),
				static_cast<float>(runtime.padding + inputSize.height));
			const auto source = D2D1::RectF(0.f, 0.f,
				static_cast<float>(inputSize.width), static_cast<float>(inputSize.height));
			resources.context->FillOpacityMask(resources.scaledInput.Get(),
				resources.shimmer.Get(), D2D1_OPACITY_MASK_CONTENT_GRAPHICS,
				&destination, &source);
			resources.context->SetAntialiasMode(previous);
		}

		void DrawProgress(Runtime& runtime, const ProgressVisualSnapshot& progress)
		{
			if (progress.opacity <= 0.001) return;
			auto& resources = runtime.resources;
			const float contentWidth = static_cast<float>(
				resources.width - runtime.padding * 2);
			const float contentHeight = static_cast<float>(
				resources.height - runtime.padding * 2);
			const float dpiScale = static_cast<float>((std::max)(
				96u, runtime.options.compatibility.captureDpiX)) / 96.f;
			constexpr float ProgressWidthDip = 192.f;
			constexpr float HorizontalMarginDip = 48.f;
			const float barWidth = (std::min)(ProgressWidthDip * dpiScale,
				(std::max)(0.f, contentWidth - HorizontalMarginDip * 2.f * dpiScale));
			if (barWidth <= 0.f) return;

			const float centerX = runtime.padding + contentWidth * 0.5f;
			const float centerY = runtime.padding + contentHeight * 0.5f;
			const float trackHeight = 1.f * dpiScale;
			const float indicatorHeight = 3.f * dpiScale;
			const auto track = D2D1::RectF(centerX - barWidth * 0.5f,
				centerY - trackHeight * 0.5f, centerX + barWidth * 0.5f,
				centerY + trackHeight * 0.5f);
			// 以完整主栏（含主按钮）居中，并对齐 WinUI 3 的 3/1 DIP 双层样式。
			resources.progressBackground->SetOpacity(static_cast<float>(progress.opacity));
			resources.context->FillRoundedRectangle(
				D2D1::RoundedRect(track, trackHeight * 0.5f, trackHeight * 0.5f),
				resources.progressBackground.Get());
			const float fillWidth = barWidth * static_cast<float>(
				std::clamp(progress.displayedRatio, 0.0, 1.0));
			if (fillWidth <= 0.f) return;
			const auto fill = D2D1::RectF(track.left,
				centerY - indicatorHeight * 0.5f, track.left + fillWidth,
				centerY + indicatorHeight * 0.5f);
			auto* brush = progress.red
				? resources.progressError.Get() : resources.progressFill.Get();
			brush->SetOpacity(static_cast<float>(progress.opacity));
			resources.context->FillRoundedRectangle(
				D2D1::RoundedRect(fill, indicatorHeight * 0.5f,
					indicatorHeight * 0.5f), brush);
		}

		void RequestAutomaticStop(const std::shared_ptr<Runtime>& runtime)
		{
			if (runtime->automaticStopPosted.exchange(true, std::memory_order_acq_rel))
				return;
			const bool posted = Inkeys::UI::RenderPipeline::PostControl([runtime]
				{
					if (runtime->registered.exchange(false, std::memory_order_acq_rel))
						Inkeys::UI::RenderPipeline::Unregister(Client::StartupPreview);
					runtime->resources.Reset();
					runtime->active.store(false, std::memory_order_release);
					runtime->owner.Hide();
					runtime->owner.RequestStop();
				});
			if (!posted)
			{
				runtime->active.store(false, std::memory_order_release);
				runtime->owner.Hide();
				runtime->owner.RequestStop();
			}
		}

		[[nodiscard]] bool ApplyHandoffRecovery(
			const std::shared_ptr<Runtime>& runtime,
			std::chrono::steady_clock::time_point now, bool revealEligible,
			bool ownerAvailable, bool failureObserved) noexcept
		{
			if (failureObserved)
				runtime->handoffFailure.ObserveFailure(now, revealEligible);
			const int committed = runtime->committedBarAlpha.load(std::memory_order_acquire);
			const auto recovery = runtime->handoffFailure.Poll(now, revealEligible,
				ownerAvailable, committed < 0 ? 0 : static_cast<std::uint8_t>(committed));
			if (recovery.requestOpaqueBar && runtime->options.requestBarAlpha)
				runtime->options.requestBarAlpha(255);
			if (!recovery.stopPreview) return false;
			RequestAutomaticStop(runtime);
			return true;
		}

		[[nodiscard]] FrameResult RenderFrame(const std::shared_ptr<Runtime>& runtime,
			const FrameContext& frame)
		{
			if (!runtime->active.load(std::memory_order_acquire)) return FrameResult::Idle;
			{
				std::scoped_lock lock(runtime->mutex);
				if (runtime->pendingBarFrame)
				{
					runtime->barFrame = std::move(*runtime->pendingBarFrame);
					runtime->pendingBarFrame.reset();
				}
			}

			if (runtime->fatalRequested.exchange(false, std::memory_order_acq_rel))
			{
				(void)runtime->lifecycle.FatalFailure();
				(void)runtime->lifecycle.RequestFailureFrame();
			}
			const auto progressSnapshot = runtime->options.progress
				? runtime->options.progress->GetSnapshot() : Inkeys::Startup::Snapshot{};
			const bool barCommitted = runtime->barState.load(std::memory_order_acquire)
				== BarStartupState::FirstFrameCommitted;
			const bool trackerComplete = progressSnapshot.totalUnits != 0
				&& progressSnapshot.completedUnits == progressSnapshot.totalUnits;
			const bool successfulStartup = barCommitted && trackerComplete
				&& !progressSnapshot.failed;
			const bool failed = progressSnapshot.failed
				|| runtime->lifecycle.State() == LifecycleState::FailureRedRequested;
			const auto progressVisual = runtime->progressVisual->Update(frame.frameTime,
				progressSnapshot.actualRatio, successfulStartup, failed);

			auto ownerSnapshot = runtime->owner.Snapshot();
			const bool revealEligible = barCommitted
				&& (trackerComplete || ownerSnapshot.window == nullptr);
			if (ApplyHandoffRecovery(runtime, frame.frameTime, revealEligible,
				ownerSnapshot.window != nullptr, false)) return FrameResult::Idle;
			if (!ownerSnapshot.window) return FrameResult::Idle;

			const bool proxyCurrentGeneration = runtime->barFrame.bitmap
				&& runtime->barFrame.deviceGeneration == frame.epoch.generation;
			if (runtime->lifecycle.State() == LifecycleState::WaitingForBar
				&& successfulStartup
				&& progressVisual.state == ProgressVisualState::Hidden)
			{
				if (!proxyCurrentGeneration)
				{
					if (ApplyHandoffRecovery(runtime, frame.frameTime, true,
						true, true)) return FrameResult::Idle;
					return FrameResult::Continue;
				}

				const RECT targetBounds = PaddedBounds(
					runtime->barFrame.screenDestination, runtime->padding);
				RECT desiredBounds{};
				std::uint64_t desiredRevision = 0;
				{
					std::scoped_lock lock(runtime->mutex);
					const bool changed = runtime->paddedBounds.left != targetBounds.left
						|| runtime->paddedBounds.top != targetBounds.top
						|| runtime->paddedBounds.right != targetBounds.right
						|| runtime->paddedBounds.bottom != targetBounds.bottom;
					if (changed)
					{
						runtime->paddedBounds = targetBounds;
						++runtime->desiredBoundsRevision;
					}
					desiredBounds = runtime->paddedBounds;
					desiredRevision = runtime->desiredBoundsRevision;
				}
				if (ownerSnapshot.revision < desiredRevision)
				{
					runtime->owner.Move(desiredBounds, desiredRevision);
					runtime->owner.RetryLatestMove();
					if (ApplyHandoffRecovery(runtime, frame.frameTime, true,
						true, true)) return FrameResult::Idle;
					return FrameResult::Continue;
				}
				if (CanBeginSuccessfulHandoff(barCommitted, trackerComplete,
					progressSnapshot.failed, progressVisual.state,
					proxyCurrentGeneration, true))
				{
					(void)runtime->lifecycle.BarFrameCommitted();
					runtime->handoffStart = frame.frameTime;
				}
				ownerSnapshot = runtime->owner.Snapshot();
			}

			const HRESULT resourceResult = EnsureResources(
				*runtime, frame, ownerSnapshot.bounds);
			if (FAILED(resourceResult))
			{
				if (ApplyHandoffRecovery(runtime, frame.frameTime, revealEligible,
					true, true)) return FrameResult::Idle;
				return IsSharedDeviceLoss(resourceResult)
					? FrameResult::DeviceLost : FrameResult::Retry;
			}
			const HRESULT sourceResult = EnsureSourceBitmap(*runtime);
			if (FAILED(sourceResult))
			{
				if (ApplyHandoffRecovery(runtime, frame.frameTime, revealEligible,
					true, true)) return FrameResult::Idle;
				return FrameResult::Retry;
			}

			auto& resources = runtime->resources;
			ID2D1Bitmap1* selected = resources.source.Get();
			D2D1_RECT_F selectedSource = D2D1::RectF(0.f, 0.f,
				static_cast<float>(runtime->sourceMetadata.width),
				static_cast<float>(runtime->sourceMetadata.height));
			float sigma = runtime->cacheState == CacheState::Valid
				? 0.f : runtime->baseBlurSigma;
			float previewAlpha = 1.f;

			if (runtime->lifecycle.State() == LifecycleState::Handoff
				&& runtime->barFrame.bitmap
				&& runtime->barFrame.deviceGeneration == frame.epoch.generation)
			{
				const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
					frame.frameTime - runtime->handoffStart);
				const int committed = runtime->committedBarAlpha.load(
					std::memory_order_acquire);
				const auto decision = ResolveHandoffFrame(runtime->lifecycle.Handoff(),
					elapsed, committed < 0 ? 0 : static_cast<std::uint8_t>(committed));
				const auto crop = runtime->barFrame.sourceCrop;
				const auto liveSource = D2D1::RectF(static_cast<float>(crop.left),
					static_cast<float>(crop.top), static_cast<float>(crop.right),
					static_cast<float>(crop.bottom));
				if (decision.useLiveProxy)
				{
					selected = runtime->barFrame.bitmap.Get();
					selectedSource = liveSource;
				}
				sigma = runtime->baseBlurSigma * static_cast<float>(decision.blurRatio);
				previewAlpha = static_cast<float>(decision.previewAlpha);
				if (decision.requestBarAlpha && runtime->options.requestBarAlpha)
					runtime->options.requestBarAlpha(decision.requestedBarAlpha);
				if (decision.stopPreview)
				{
					RequestAutomaticStop(runtime);
					return FrameResult::Idle;
				}
			}

			HRESULT prepareResult = PrepareScaledInput(*runtime, selected, selectedSource);
			if (FAILED(prepareResult))
			{
				resources.Reset();
				if (ApplyHandoffRecovery(runtime, frame.frameTime, revealEligible,
					true, true)) return FrameResult::Idle;
				return IsSharedDeviceLoss(prepareResult)
					? FrameResult::DeviceLost : FrameResult::Retry;
			}
			resources.context->BeginDraw();
			resources.context->Clear(D2D1::ColorF(0.f, 0.f, 0.f, 0.f));
			DrawPreparedInput(*runtime, sigma);
			DrawShimmer(*runtime, frame.frameTime);
			// 最后合成进度条，保证轨道和指示条位于 blur/shimmer 上层。
			DrawProgress(*runtime, progressVisual);

			const auto presentationOwner = runtime->owner.Snapshot();
			if (!presentationOwner.window
				|| Width(presentationOwner.bounds) != static_cast<int>(resources.width)
				|| Height(presentationOwner.bounds) != static_cast<int>(resources.height))
			{
				const HRESULT settleResult = resources.context->EndDraw();
				if (settleResult == D2DERR_RECREATE_TARGET) resources.Reset();
				if (ApplyHandoffRecovery(runtime, frame.frameTime, revealEligible,
					presentationOwner.window != nullptr, true)) return FrameResult::Idle;
				return IsSharedDeviceLoss(settleResult)
					? FrameResult::DeviceLost : FrameResult::Retry;
			}
			HDC sourceDc = nullptr;
			HRESULT getDcResult = resources.gdi->GetDC(
				D2D1_DC_INITIALIZE_MODE_COPY, &sourceDc);
			BOOL updateResult = FALSE;
			HRESULT releaseResult = E_FAIL;
			if (SUCCEEDED(getDcResult) && sourceDc)
			{
				updateResult = runtime->owner.PresentPixels(sourceDc,
					static_cast<BYTE>(std::lround(255.f * previewAlpha)),
					resources.width, resources.height);
				releaseResult = resources.gdi->ReleaseDC(nullptr);
			}
			else if (SUCCEEDED(getDcResult)) getDcResult = E_POINTER;
			const HRESULT endDrawResult = resources.context->EndDraw();
			const bool committed = SUCCEEDED(getDcResult) && updateResult != FALSE
				&& SUCCEEDED(releaseResult) && SUCCEEDED(endDrawResult);
			if (!committed)
			{
				if (endDrawResult == D2DERR_RECREATE_TARGET
					|| releaseResult == D2DERR_RECREATE_TARGET) resources.Reset();
				if (ApplyHandoffRecovery(runtime, frame.frameTime, revealEligible,
					true, true)) return FrameResult::Idle;
				return IsSharedDeviceLoss(getDcResult) || IsSharedDeviceLoss(releaseResult)
					|| IsSharedDeviceLoss(endDrawResult)
					? FrameResult::DeviceLost : FrameResult::Retry;
			}
			runtime->handoffFailure.ObserveSuccess();

			if (!runtime->firstFrameCommitted.exchange(true, std::memory_order_acq_rel))
			{
				runtime->presentationAvailable.store(true, std::memory_order_release);
				(void)runtime->lifecycle.PreviewFrameCommitted();
				if (runtime->options.progress)
					(void)runtime->options.progress->Complete(
						Inkeys::Startup::Milestone::PreviewFirstFrameCommitted);
				runtime->owner.Show();
				runtime->progressVisual->MarkPreviewShown(frame.frameTime);
				runtime->presentationCondition.notify_all();
			}
			if (runtime->lifecycle.State() == LifecycleState::FailureRedRequested)
			{
				(void)runtime->lifecycle.FailureFrameCommitted();
				runtime->failureFrameCommitted.store(true, std::memory_order_release);
				runtime->failureCondition.notify_all();
			}
			return FrameResult::Continue;
		}
	}

	bool Start(const StartOptions& options)
	{
		try
		{
		if (!options.enabled || !options.progress
			|| !Inkeys::UI::RenderPipeline::IsInitialized()) return false;
		{
			std::scoped_lock lock(runtimeMutex);
			if (currentRuntime) return false;
		}
		auto runtime = std::make_shared<Runtime>();
		runtime->options = options;
		runtime->barState.store(globalBarStartupState.load(
			std::memory_order_acquire), std::memory_order_release);
		if (!runtime->options.instance) runtime->options.instance = GetModuleHandleW(nullptr);
		runtime->progressVisual = std::make_unique<ProgressVisualReducer>();
		runtime->committedBarAlpha.store(-1, std::memory_order_release);

		const auto embedded = ReadEmbeddedPreview(runtime->options.instance,
			runtime->options.embeddedResourceId);
		if (embedded.state != CacheState::Valid
			|| embedded.metadata.layoutEpoch != options.compatibility.layoutEpoch
			|| !std::equal(embedded.metadata.visualSignature.begin(),
				embedded.metadata.visualSignature.end(),
				options.embeddedVisualSignature.begin()))
		{
			(void)options.progress->Complete(
				Inkeys::Startup::Milestone::CacheClassified);
			return false;
		}
		auto compatibility = options.compatibility;
		if (compatibility.anchorX == 0 && compatibility.anchorY == 0
			&& compatibility.progressRight == 0
			&& embedded.metadata.captureDpiX != 0)
		{
			const double scale = static_cast<double>(compatibility.captureDpiX)
				/ embedded.metadata.captureDpiX;
			const auto scaledWidth = static_cast<std::int32_t>((std::max)(1ll,
				std::llround(embedded.metadata.width * scale)));
			const auto scaledHeight = static_cast<std::int32_t>((std::max)(1ll,
				std::llround(embedded.metadata.height * scale)));
			if (embedded.metadata.monitorPixelWidth != 0
				&& embedded.metadata.monitorPixelHeight != 0)
			{
				const double centerX = (embedded.metadata.windowOffsetX
					+ embedded.metadata.width / 2.0)
					/ embedded.metadata.monitorPixelWidth;
				const double centerY = (embedded.metadata.windowOffsetY
					+ embedded.metadata.height / 2.0)
					/ embedded.metadata.monitorPixelHeight;
				compatibility.windowOffsetX = static_cast<std::int32_t>(std::llround(
					centerX * compatibility.monitorPixelWidth - scaledWidth / 2.0));
				compatibility.windowOffsetY = static_cast<std::int32_t>(std::llround(
					centerY * compatibility.monitorPixelHeight - scaledHeight / 2.0));
			}
			compatibility.anchorX = static_cast<std::int32_t>(std::llround(
				embedded.metadata.anchorX * scale));
			compatibility.anchorY = static_cast<std::int32_t>(std::llround(
				embedded.metadata.anchorY * scale));
			compatibility.progressLeft = static_cast<std::int32_t>(std::llround(
				embedded.metadata.progressLeft * scale));
			compatibility.progressTop = static_cast<std::int32_t>(std::llround(
				embedded.metadata.progressTop * scale));
			compatibility.progressRight = static_cast<std::int32_t>(std::llround(
				embedded.metadata.progressRight * scale));
			compatibility.progressBottom = static_cast<std::int32_t>(std::llround(
				embedded.metadata.progressBottom * scale));
		}
		runtime->options.compatibility = compatibility;
		const auto cache = ReadPreviewFile(options.cachePath, compatibility);
		runtime->cacheState = cache.state;
		(void)options.progress->Complete(Inkeys::Startup::Milestone::CacheClassified);
		if (cache.state == CacheState::Valid)
		{
			runtime->sourceMetadata = cache.metadata;
			runtime->sourcePixels = cache.pixels;
		}
		else
		{
			runtime->sourceMetadata = embedded.metadata;
			runtime->sourcePixels = embedded.pixels;
		}
		const UINT dpi = (std::max)(96u, options.compatibility.captureDpiX);
		runtime->baseBlurSigma = static_cast<float>(6.0 * dpi / 96.0);
		runtime->padding = static_cast<UINT>(std::ceil(
			runtime->baseBlurSigma * 6.0f));
		RECT content = options.contentBounds;
		if (Width(content) == 0 || Height(content) == 0)
		{
			const double sourceScale = static_cast<double>(compatibility.captureDpiX)
				/ (std::max)(1u, runtime->sourceMetadata.captureDpiX);
			const LONG contentWidth = static_cast<LONG>((std::max)(1ll,
				std::llround(runtime->sourceMetadata.width * sourceScale)));
			const LONG contentHeight = static_cast<LONG>((std::max)(1ll,
				std::llround(runtime->sourceMetadata.height * sourceScale)));
			content = { runtime->options.monitorBounds.left
					+ compatibility.windowOffsetX,
				runtime->options.monitorBounds.top + compatibility.windowOffsetY,
				runtime->options.monitorBounds.left
					+ compatibility.windowOffsetX + contentWidth,
				runtime->options.monitorBounds.top
					+ compatibility.windowOffsetY + contentHeight };
		}
		runtime->paddedBounds = PaddedBounds(content, runtime->padding);
		runtime->desiredBoundsRevision = 0;
		(void)runtime->lifecycle.Start(true, runtime->cacheState, true);
		if (!runtime->owner.Start(runtime->options.instance, runtime->paddedBounds))
			return false;
		(void)options.progress->Complete(Inkeys::Startup::Milestone::PreviewOwnerReady);
		runtime->writer.Start();
		runtime->captureWriter.Start();
		{
			std::scoped_lock lock(runtimeMutex);
			runtime->developerCapturePath = pendingDeveloperCapturePath;
			runtime->developerCaptureCompleted = pendingDeveloperCaptureCompleted;
			if (runtime->developerCaptureCompleted)
				runtime->developerCaptureCompleted->store(false, std::memory_order_release);
			currentRuntime = runtime;
		}
		runtime->active.store(true, std::memory_order_release);
		const bool registered = Inkeys::UI::RenderPipeline::Register(
			Client::StartupPreview, [runtime](const FrameContext& frame)
				{ return RenderFrame(runtime, frame); });
		if (!registered)
		{
			runtime->active.store(false, std::memory_order_release);
			runtime->owner.Stop();
			runtime->writer.Stop();
			runtime->captureWriter.Stop();
			std::scoped_lock lock(runtimeMutex);
			currentRuntime.reset();
			return false;
		}
		runtime->registered.store(true, std::memory_order_release);
		(void)options.progress->Complete(
			Inkeys::Startup::Milestone::PreviewRenderClientReady);
		{
			std::unique_lock lock(runtime->presentationMutex);
			if (!runtime->presentationCondition.wait_for(lock, 500ms, [runtime]
				{ return runtime->firstFrameCommitted.load(std::memory_order_acquire); }))
			{
				lock.unlock();
				Stop();
				return false;
			}
		}
		return true;
		}
		catch (...)
		{
			// Preview/cache 是可降级能力，分配或系统异常不得中止正式启动。
			Stop();
			return false;
		}
	}

	void Stop() noexcept
	{
		std::shared_ptr<Runtime> runtime;
		std::unique_ptr<CacheWriter> standaloneCapture;
		{
			std::scoped_lock lock(runtimeMutex);
			runtime = std::move(currentRuntime);
			standaloneCapture = std::move(captureOnlyWriter);
			pendingDeveloperCapturePath.clear();
			pendingDeveloperCaptureCompatibility = {};
		}
		if (!runtime)
		{
			if (standaloneCapture) standaloneCapture->Stop();
			return;
		}
		runtime->active.store(false, std::memory_order_release);
		if (runtime->registered.exchange(false, std::memory_order_acq_rel))
			Inkeys::UI::RenderPipeline::Unregister(Client::StartupPreview);
		auto completed = std::make_shared<std::promise<void>>();
		auto future = completed->get_future();
		if (Inkeys::UI::RenderPipeline::PostControl([runtime, completed]
			{
				runtime->resources.Reset();
				try { completed->set_value(); }
				catch (...) {}
			}))
			(void)future.wait_for(500ms);
		runtime->owner.Hide();
		runtime->owner.Stop();
		runtime->writer.Stop();
		runtime->captureWriter.Stop();
		if (standaloneCapture) standaloneCapture->Stop();
	}

	bool IsActive() noexcept
	{
		const auto runtime = SnapshotRuntime();
		return runtime && runtime->active.load(std::memory_order_acquire);
	}

	bool IsPresentationAvailable() noexcept
	{
		const auto runtime = SnapshotRuntime();
		return runtime && runtime->presentationAvailable.load(std::memory_order_acquire);
	}

	bool AcceptsCommittedBarFrames() noexcept
	{
		std::scoped_lock lock(runtimeMutex);
		return currentRuntime != nullptr || !pendingDeveloperCapturePath.empty();
	}

	bool ShouldBarStartTransparent() noexcept
	{
		const auto runtime = SnapshotRuntime();
		if (!runtime || !runtime->active.load(std::memory_order_acquire)) return false;
		runtime->barTransparencyDecisionMade.store(true, std::memory_order_release);
		if (runtime->presentationAvailable.load(std::memory_order_acquire)
			&& runtime->owner.Window() != nullptr) return true;
		// Preview 尚未完成首个 ULW 时保持正式 Bar 可见，并终止迟到 Preview。
		RequestAutomaticStop(runtime);
		return false;
	}

	CacheState CurrentCacheState() noexcept
	{
		const auto runtime = SnapshotRuntime();
		return runtime ? runtime->cacheState : CacheState::Missing;
	}

	void RevalidateTopmost() noexcept
	{
		if (const auto runtime = SnapshotRuntime()) runtime->owner.RevalidateTopmost();
	}

	void UpdateContentBounds(const RECT& bounds, std::uint64_t revision) noexcept
	{
		const auto runtime = SnapshotRuntime();
		if (!runtime || Width(bounds) == 0 || Height(bounds) == 0) return;
		const RECT padded = PaddedBounds(bounds, runtime->padding);
		{
			std::scoped_lock lock(runtime->mutex);
			if (revision <= runtime->desiredBoundsRevision) return;
			runtime->paddedBounds = padded;
			runtime->desiredBoundsRevision = revision;
		}
		runtime->owner.Move(padded, revision);
		Inkeys::UI::RenderPipeline::Request(Client::StartupPreview);
	}

	bool PublishCommittedBarFrame(CommittedBarFrame frame) noexcept
	{
		const auto runtime = SnapshotRuntime();
		if (!frame.bitmap || frame.deviceGeneration == 0) return false;
		if (!runtime)
		{
			std::scoped_lock lock(runtimeMutex);
			if (!captureOnlyWriter || pendingDeveloperCapturePath.empty()
				|| !frame.stableForCache || frame.cpuPixels.empty()
				|| frame.metadata.captureDpiX != 96
				|| frame.metadata.captureDpiY != 96
				|| frame.metadata.layoutEpoch
					!= pendingDeveloperCaptureCompatibility.layoutEpoch
				|| frame.metadata.visualSignature
					!= pendingDeveloperCaptureCompatibility.visualSignature) return false;
			frame.metadata.flags = PreviewFlagEmbedded;
			frame.metadata.captureRevision = 0;
			captureOnlyWriter->Submit({ frame.visualRevision,
				frame.metadata, std::move(frame.cpuPixels),
				pendingDeveloperCapturePath, pendingDeveloperCaptureCompleted });
			return true;
		}
		if (frame.stableForCache && !frame.cpuPixels.empty()
			&& frame.visualRevision > runtime->lastSubmittedCacheRevision)
		{
			runtime->lastSubmittedCacheRevision = frame.visualRevision;
			frame.metadata.flags = PreviewFlagDiskCache;
			frame.metadata.captureRevision = frame.visualRevision;
			const bool captureRequested = !runtime->developerCapturePath.empty()
				&& frame.metadata.captureDpiX == 96
				&& frame.metadata.captureDpiY == 96;
			CacheWriteRequest cacheRequest{ frame.visualRevision,
				frame.metadata,
				captureRequested ? frame.cpuPixels : std::move(frame.cpuPixels),
				runtime->options.cachePath, {} };
			runtime->writer.Submit(std::move(cacheRequest));
			if (captureRequested)
			{
				frame.metadata.flags = PreviewFlagEmbedded;
				frame.metadata.captureRevision = 0;
				runtime->captureWriter.Submit({ frame.visualRevision,
					frame.metadata, std::move(frame.cpuPixels), runtime->developerCapturePath,
					runtime->developerCaptureCompleted });
			}
		}
		{
			std::scoped_lock lock(runtime->mutex);
			runtime->pendingBarFrame = std::move(frame);
		}
		Inkeys::UI::RenderPipeline::Request(Client::StartupPreview);
		return true;
	}

	void SetBarStartupState(BarStartupState state) noexcept
	{
		auto current = globalBarStartupState.load(std::memory_order_acquire);
		for (;;)
		{
			const bool currentTerminal = current >= BarStartupState::FirstFrameCommitted;
			if (currentTerminal || static_cast<unsigned>(state)
				< static_cast<unsigned>(current)) return;
			if (globalBarStartupState.compare_exchange_weak(current, state,
				std::memory_order_acq_rel)) break;
		}
		if (state == BarStartupState::FirstFrameCommitted)
			(void)Inkeys::Startup::Report(
				Inkeys::Startup::Milestone::BarFirstFrameCommitted);
		else if (state == BarStartupState::WindowMissing)
			(void)Inkeys::Startup::ReportFailure(0xB001u);
		else if (state == BarStartupState::ClientRegistrationFailed)
			(void)Inkeys::Startup::ReportFailure(0xB002u);
		else if (state == BarStartupState::StartupFailed)
			(void)Inkeys::Startup::ReportFailure(0xB003u);
		else if (state == BarStartupState::StoppedBeforeReady)
			(void)Inkeys::Startup::ReportFailure(0xB004u);
		if (const auto runtime = SnapshotRuntime())
		{
			runtime->barState.store(state, std::memory_order_release);
			Inkeys::UI::RenderPipeline::Request(Client::StartupPreview);
		}
	}

	BarStartupState GetBarStartupState() noexcept
	{
		return globalBarStartupState.load(std::memory_order_acquire);
	}

	void NotifyBarPresentationAlphaCommitted(std::uint8_t alpha) noexcept
	{
		if (const auto runtime = SnapshotRuntime())
		{
			runtime->committedBarAlpha.store(static_cast<int>(alpha),
				std::memory_order_release);
			Inkeys::UI::RenderPipeline::Request(Client::StartupPreview);
		}
	}

	void RequestFailureFrame() noexcept
	{
		if (const auto runtime = SnapshotRuntime())
		{
			runtime->fatalRequested.store(true, std::memory_order_release);
			Inkeys::UI::RenderPipeline::Request(Client::StartupPreview);
		}
	}

	bool WaitForFailureFrame(std::chrono::milliseconds timeout) noexcept
	{
		const auto runtime = SnapshotRuntime();
		if (!runtime) return false;
		std::unique_lock lock(runtime->failureMutex);
		return runtime->failureCondition.wait_for(lock, timeout, [runtime]
			{ return runtime->failureFrameCommitted.load(std::memory_order_acquire); });
	}

	void ConfigureDeveloperCapture(std::wstring outputPath,
		PreviewCompatibility compatibility) noexcept
	{
		try
		{
			std::scoped_lock lock(runtimeMutex);
			pendingDeveloperCapturePath = std::move(outputPath);
			pendingDeveloperCaptureCompatibility = compatibility;
			pendingDeveloperCaptureCompleted = std::make_shared<std::atomic_bool>(false);
			captureOnlyWriter = std::make_unique<CacheWriter>();
			captureOnlyWriter->Start();
		}
		catch (...)
		{
			std::scoped_lock lock(runtimeMutex);
			pendingDeveloperCapturePath.clear();
			captureOnlyWriter.reset();
		}
	}

	bool DeveloperCaptureCompleted() noexcept
	{
		std::scoped_lock lock(runtimeMutex);
		return pendingDeveloperCaptureCompleted
			&& pendingDeveloperCaptureCompleted->load(std::memory_order_acquire);
	}

	bool DeveloperCaptureRequested() noexcept
	{
		std::scoped_lock lock(runtimeMutex);
		return !pendingDeveloperCapturePath.empty();
	}

	Diagnostics SnapshotDiagnostics() noexcept
	{
		const auto runtime = SnapshotRuntime();
		if (!runtime) return {};
		return {
			runtime->cacheState,
			runtime->active.load(std::memory_order_acquire),
			runtime->firstFrameCommitted.load(std::memory_order_acquire),
			runtime->automaticStopPosted.load(std::memory_order_acquire),
			runtime->owner.HasExited(),
		};
	}
}
