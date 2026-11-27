#include "Draw3.DiagnosticOutput.h"
#include "Draw3.Host.h"
#include "Draw3.RecoverySession.h"
#include "../../Helper/Helper.Fatal.h"
#include "../../../IdtStart.h"
#include "Draw3.PresentationState.h"
#include "Draw3.Presentation.h"
#include "Draw3.HapticDiagnostics.h"
#include "Draw3.Product.h"
#include <dxgi.h>
#include "Draw3.PptTiming.h"
#include "Draw3.SpeedEraser.h"
#include "Draw3.DevicePerformanceLedger.h"

import Inkeys.Conv.Text;
import Inkeys.Display;
import Inkeys.UI.PageControl;

import Inkeys.Drawing.Draw3.contact_input;
import Inkeys.Drawing.Draw3.pen_cursor;
import Inkeys.Drawing.Draw3.desktop_ink_snapshots;
import draw3.uink_store; // C1.6b 恢复读取 ReadUInkStoreFilePrefix
import Inkeys.Drawing.Draw3.drawing_controller;
import Inkeys.Drawing.Draw3.graphics_initialization;
import Inkeys.Drawing.Draw3.haptic_feedback;
import Inkeys.Drawing.Draw3.ink_prediction;
import Inkeys.Drawing.Draw3.paged_ink_store;
import Inkeys.Drawing.Draw3.renderer;
import Inkeys.Drawing.Draw3.realtime_stylus;
import Inkeys.Drawing.Draw3.runtime_metrics;
import Inkeys.Drawing.Draw3.transparent_presentation;
import Inkeys.Drawing.Draw3.window_control;

#include <atomic>
#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <exception>
#include <iostream>
#include <limits>
#include <map>
#include <mutex>
#include <set>
#include <stdexcept>
#include <thread>
#include <utility>

namespace Inkeys::Drawing::Draw3
{
	namespace
	{
		// 橡皮/触摸面积诊断受 TouchArea 调试开关控制，写入日志 TouchArea 通道；标准流模式仍写 stderr。
		constexpr DiagnosticOutput::Target kTouchAreaTarget{ DiagnosticOutput::Stream::Error,
			Inkeys::Log::Channel::TouchArea, Inkeys::Log::Module::Input };

		struct HiddenPresentationAccounting
		{
			HostHiddenPersistenceSnapshot counts;
			std::set<std::uint64_t> outstandingIds;

			std::uint64_t Accept() noexcept
			{
				// 整个 Host 运行代次共用序号，即使请求跨文稿或业务身份相同也可逐笔区分。
				const auto id = ++counts.presentationAccepted;
				++counts.presentationOutstanding;
				try { outstandingIds.insert(id); }
				catch (...) { /* 诊断登记失败保留未结项，准入结果仍由服务决定。 */ }
				return id;
			}

			void Settle(const PagedInkCompletion& completion) noexcept
			{
				if (outstandingIds.erase(completion.requestId) == 0)
				{
					++counts.presentationUnmatchedTerminals;
					return;
				}
				--counts.presentationOutstanding;
				switch (completion.result)
				{
				case PagedInkResult::Succeeded:
					if (completion.identity.kind == PagedInkRequestKind::Full ||
						completion.identity.kind == PagedInkRequestKind::Clear)
					{
						++counts.presentationCommitted;
						break;
					}
					if (completion.identity.kind == PagedInkRequestKind::LoadLane)
					{
						// 批内每个失败页单计，整批只占一条失败终态。
						const auto failures = std::count_if(completion.lanePages.begin(), completion.lanePages.end(),
							[](const PagedInkLanePage& page) { return page.result != PagedInkResult::Succeeded; });
						if (failures != 0)
						{
							counts.presentationLanePageFailures += static_cast<std::uint64_t>(failures);
							++counts.presentationFailed;
							break;
						}
						if (completion.lanePages.empty()) ++counts.presentationEmptyLane;
					}
					++counts.presentationLoaded;
					break;
				case PagedInkResult::Failed:
				case PagedInkResult::FileFailed: ++counts.presentationFailed; break;
				case PagedInkResult::Merged: ++counts.presentationMerged; break;
				case PagedInkResult::Canceled: ++counts.presentationCanceled; break;
				}
			}
		};

		class PresentationStoreRegistry
		{
		public:
			void BeginRun() noexcept
			{
				const std::scoped_lock lock(mutex_);
				accepting_ = true;
			}

			PagedInkStore* Get(const Bridge::PresentationTarget& target, const std::wstring& root,
				float dpiScale, void (*wake)(void*) noexcept, void* wakeContext,
				PagedInkStoreTestHook testHook = {},
				void (*decorate)(void*, PagedInkStoreOptions&) = nullptr, void* decorateContext = nullptr)
			{
				if (target.key.IsZero() || root.empty()) return nullptr;
				const std::scoped_lock lock(mutex_);
				if (!accepting_) return nullptr;
				if (auto found = stores_.find(target.key); found != stores_.end())
					return found->second.get();

				const auto workspaceGuid = draw3::uink::CreateUInkGuid();
				if (!workspaceGuid) return nullptr;
				PagedInkStoreOptions options;
				options.root = root;
				options.naming = { PagedInkFileKind::Presentation, utf8ToUtf16(target.presentationName),
					draw3::uink::UInkGuid(target.key.bytes) };
				options.pageContext.workspaceGuid = *workspaceGuid;
				options.pageContext.workspaceType = 2;
				options.pageContext.dpiScale = dpiScale;
				options.hostId = FormatPresentationKey(target.key);
				options.importOptions.dpiScale = dpiScale;
				options.wake = wake;
				options.wakeContext = wakeContext;
				// C1.6b：按文稿键接上崩溃恢复会话的写前登记钩子。
				if (decorate) decorate(decorateContext, options);
				auto store = std::make_unique<PagedInkStore>(std::move(options), testHook);
				return stores_.emplace(target.key, std::move(store)).first->second.get();
			}

			std::optional<PresentationPagedCompletion> Take() noexcept
			{
				const std::scoped_lock lock(mutex_);
				// 逐文稿保留服务回执顺序，轮转避免较早文稿填满终态槽而饿死其他文稿。
				auto next = stores_.upper_bound(lastTaken_);
				for (std::size_t i = 0; i < stores_.size(); ++i, ++next)
				{
					if (next == stores_.end()) next = stores_.begin();
					if (auto completion = next->second->Take())
					{
						lastTaken_ = next->first;
						return PresentationPagedCompletion{ next->first, std::move(*completion) };
					}
				}
				return std::nullopt;
			}

			void Drain()
			{
				const std::scoped_lock lock(mutex_);
				for (auto& entry : stores_) entry.second->Drain();
			}

			void Close()
			{
				const std::scoped_lock lock(mutex_);
				accepting_ = false;
				for (auto& entry : stores_) entry.second->Close();
			}

			void Clear() noexcept
			{
				const std::scoped_lock lock(mutex_);
				stores_.clear();
				lastTaken_ = {};
			}

		private:
			std::mutex mutex_;
			std::map<Bridge::PresentationKey, std::unique_ptr<PagedInkStore>> stores_;
			Bridge::PresentationKey lastTaken_;
			bool accepting_ = false;
		};

		template<class Observe>
		std::size_t DrainPresentationCompletions(PresentationStoreRegistry& stores,
			std::optional<PresentationPagedCompletion>& pending, WindowController& window,
			Observe&& observe) noexcept
		{
			std::size_t delivered = 0;
			for (;;)
			{
				if (!pending)
				{
					pending = stores.Take();
					if (!pending) break;
					// 无窗口自检可在 Host 核销前丢弃服务回执。
					if (!observe(pending->completion))
					{
						pending.reset();
						continue;
					}
				}
				if (!window.TryEnqueuePresentationCompletion(pending)) break;
				++delivered;
			}
			return delivered;
		}

		UINT QueryCompatibleWindowDpi(HWND hwnd) noexcept
		{
			if (!hwnd) return 96;
			using GetDpiForWindowFunction = UINT(WINAPI*)(HWND);
			const HMODULE user32 = GetModuleHandleW(L"user32.dll");
			const auto getDpiForWindow = user32
				? reinterpret_cast<GetDpiForWindowFunction>(
					GetProcAddress(user32, "GetDpiForWindow")) : nullptr;
			if (getDpiForWindow)
			{
				const UINT dpi = getDpiForWindow(hwnd);
				if (dpi != 0) return dpi;
			}
			// Win7 无 GetDpiForWindow：沿用显示快照，再退到系统 GDI DPI。
			const auto display = Inkeys::Display::GetSnapshot();
			const HMONITOR monitor = MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST);
			if (display)
				if (const auto* info = display->Find(monitor))
					if (info->effectiveDpiX != 0) return info->effectiveDpiX;
			const HDC dc = GetDC(hwnd);
			if (dc)
			{
				const int dpi = GetDeviceCaps(dc, LOGPIXELSX);
				ReleaseDC(hwnd, dc);
				if (dpi > 0) return static_cast<UINT>(dpi);
			}
			return 96;
		}

		const char* DisplayTopologyName(Inkeys::Display::DisplayTopology value) noexcept
		{
			switch(value)
			{
			case Inkeys::Display::DisplayTopology::Single:return "Single";
			case Inkeys::Display::DisplayTopology::Extended:return "Extended";
			case Inkeys::Display::DisplayTopology::CloneOrMixed:return "CloneOrMixed";
			default:return "Unknown";
			}
		}
		const char* EdidStatusName(Inkeys::Display::EdidStatus value) noexcept
		{
			switch(value)
			{
			case Inkeys::Display::EdidStatus::Parsed:return "Parsed";
			case Inkeys::Display::EdidStatus::ReadFailed:return "ReadFailed";
			case Inkeys::Display::EdidStatus::ParseFailed:return "ParseFailed";
			default:return "Unavailable";
			}
		}

		const char* HostOutputTargetName(HostOutputTarget value) noexcept
		{
			return value == HostOutputTarget::SelectionUlw
				? "DrawpadPresentation" : "Drawpad";
		}

		const char* Draw3BuildArchitectureName() noexcept
		{
#if defined(_M_ARM64)
			return "ARM64";
#elif defined(_M_X64)
			return "x64";
#elif defined(_M_IX86)
			return "Win32";
#else
			return "unknown";
#endif
		}

		void LogDraw3BuildIdentity()
		{
			// 工程的 Debug/Release 配置共用 NDEBUG 定义，配置名由交付构建命令记录；
			// 这里只输出不会被配置宏误判的编译身份。
			DiagnosticOutput::Line(DiagnosticOutput::OutInfo) << "[Draw3Diag][build] id=draw3-" << __DATE__ << "T" << __TIME__
				<< " arch=" << Draw3BuildArchitectureName()
				<< " msvc=" << _MSC_VER << std::endl;
		}

		void LogDraw3WindowState(const char* eventName, HWND primary, HWND selection)
		{
			if (!eventName) return;
			const auto logWindow = [eventName](const char* role, HWND window)
			{
				const BOOL valid = window && IsWindow(window);
				const BOOL visible = valid && IsWindowVisible(window);
				RECT windowRect = {};
				RECT clientRect = {};
				const BOOL windowRectOk = valid && GetWindowRect(window, &windowRect);
				const BOOL clientRectOk = valid && GetClientRect(window, &clientRect);
				DWORD processId = 0;
				const DWORD threadId = valid ? GetWindowThreadProcessId(window, &processId) : 0;
				DiagnosticOutput::Line() << "[Draw3Diag][window] event=" << eventName << " role=" << role
					<< " hwnd=0x" << std::hex << reinterpret_cast<UINT_PTR>(window)
					<< " valid=" << std::dec << (valid ? 1 : 0)
					<< " visible=" << (visible ? 1 : 0)
					<< " pid=" << processId << " tid=" << threadId
					<< " owner=0x" << std::hex << reinterpret_cast<UINT_PTR>(valid ? GetWindow(window, GW_OWNER) : nullptr)
					<< " previous=0x" << reinterpret_cast<UINT_PTR>(valid ? GetWindow(window, GW_HWNDPREV) : nullptr)
					<< " style=0x" << std::hex
					<< static_cast<unsigned long>(valid ? GetWindowLongPtrW(window, GWL_STYLE) : 0)
					<< " exStyle=0x" << static_cast<unsigned long>(valid ? GetWindowLongPtrW(window, GWL_EXSTYLE) : 0)
					<< std::dec << " windowRectOk=" << (windowRectOk ? 1 : 0)
					<< " clientRectOk=" << (clientRectOk ? 1 : 0)
					<< " window=(" << windowRect.left << "," << windowRect.top << ","
					<< windowRect.right << "," << windowRect.bottom << ")"
					<< " client=(" << clientRect.left << "," << clientRect.top << ","
					<< clientRect.right << "," << clientRect.bottom << ")" << std::endl;
			};
			logWindow("Drawpad", primary);
			logWindow("DrawpadPresentation", selection);
		}
		const char* PhysicalReasonName(Inkeys::Display::PhysicalSizeUnavailableReason value) noexcept
		{
			switch(value)
			{
			case Inkeys::Display::PhysicalSizeUnavailableReason::None:return "None";
			case Inkeys::Display::PhysicalSizeUnavailableReason::SnapshotFallback:return "SnapshotFallback";
			case Inkeys::Display::PhysicalSizeUnavailableReason::TopologyUnknown:return "TopologyUnknown";
			case Inkeys::Display::PhysicalSizeUnavailableReason::CloneOrMixed:return "CloneOrMixed";
			case Inkeys::Display::PhysicalSizeUnavailableReason::DisplayTargetAmbiguous:return "DisplayTargetAmbiguous";
			case Inkeys::Display::PhysicalSizeUnavailableReason::EdidUnavailable:return "EdidUnavailable";
			case Inkeys::Display::PhysicalSizeUnavailableReason::EdidReadFailed:return "EdidReadFailed";
			case Inkeys::Display::PhysicalSizeUnavailableReason::EdidParseFailed:return "EdidParseFailed";
			case Inkeys::Display::PhysicalSizeUnavailableReason::MissingDimensions:return "MissingDimensions";
			default:return "DimensionsBelowMinimum";
			}
		}
		const char* InputDeviceTypeName(uint32_t value) noexcept
		{
			switch(static_cast<InputDeviceType>(value))
			{
			case InputDeviceType::Touch:return "Touch";
			case InputDeviceType::Pen:return "Pen";
			case InputDeviceType::MouseLeft:return "MouseLeft";
			case InputDeviceType::MouseRight:return "MouseRight";
			default:return "Unknown";
			}
		}
	}

#if defined(INKEYS_ENABLE_UI3_PRESENTATION_HARNESS) && INKEYS_ENABLE_UI3_PRESENTATION_HARNESS
	int RunPresentationCompletionDeliveryTest() noexcept
	{
		int failures = 0;
		const auto check = [&](bool result, const char* message)
		{
			if (!result)
			{
				++failures;
				std::fprintf(stderr, "[Draw3PptCompletion] FAIL: %s\n", message);
			}
		};
		try
		{
			ContactInputCoordinator input;
			WindowController window;
			window.SetInputCoordinator(&input);
			const auto clearWakes = [&]
			{
				ContactRecord* record = nullptr;
				while (input.TryDequeue(record))
					if (!record) input.AcknowledgeControlWake();
			};
			Bridge::PresentationTarget target;
			for (std::size_t i = 0; i < target.key.bytes.size(); ++i)
				target.key.bytes[i] = static_cast<std::uint8_t>(i + 1);
			target.bindingMode = Bridge::SlideBindingMode::StableSlideId;
			target.sourceIdentity = "path:c:\\draw3-completion-test\\deck.pptx";
			target.presentationName = "deck.pptx";
			target.provider = "PowerPoint";
			target.bindingToken = "PowerPoint:12:34:1";
			target.slideIds = { 101 };
			target.slideId = 101;
			target.totalPages = 1;
			target.bindingRevision = 1;
			target.targetRevision = 1;
			const auto terminal = [&](std::uint64_t slotGeneration)
			{
				PresentationPagedCompletion value;
				value.key = target.key;
				value.completion.identity.slotGeneration = slotGeneration;
				value.completion.identity.loadGeneration = slotGeneration + 10;
				value.completion.identity.kind = PagedInkRequestKind::LoadLane;
				value.completion.identity.laneBinding = 0;
				return std::optional{ std::move(value) };
			};
			auto first = terminal(1);
			auto second = terminal(2);
			CanvasCommand clear;
			clear.type = CanvasCommandType::Clear;
			CanvasCommand undo;
			undo.type = CanvasCommandType::Undo;
			CanvasCommand scene;
			scene.type = CanvasCommandType::SetPresentationTarget;
			scene.presentationTarget = std::make_shared<Bridge::PresentationTarget>(target);
			window.EnqueueCanvasCommand(clear);
			window.EnqueueCanvasCommand(scene);
			check(window.TryEnqueuePresentationCompletion(first), "first terminal enters reserved slot");
			check(!window.TryEnqueuePresentationCompletion(second), "occupied slot cannot overwrite terminal");
			for (std::uint64_t revision : { 2, 3 })
			{
				target.targetRevision = revision;
				scene.presentationTarget = std::make_shared<Bridge::PresentationTarget>(target);
				window.EnqueueCanvasCommand(scene);
			}
			window.EnqueueCanvasCommand(undo);
			CanvasCommand command;
			const auto isTerminal = [&](std::uint64_t generation, const Bridge::PresentationKey& key)
			{
				if (command.type != CanvasCommandType::PresentationPersistenceCompleted ||
					!command.presentationPersistenceCompletion || command.presentationPersistenceCompletion->key != key)
					return false;
				const auto& identity = command.presentationPersistenceCompletion->completion.identity;
				return identity.slotGeneration == generation && identity.loadGeneration == generation + 10 &&
					identity.kind == PagedInkRequestKind::LoadLane && identity.laneBinding == 0;
			};
			check(window.TryDequeueCanvasCommand(command) && command.type == clear.type,
				"ordinary command before terminal keeps FIFO");
			check(window.TryDequeueCanvasCommand(command) && command.type == scene.type &&
				command.presentationTarget->targetRevision == 1, "target coalescing cannot cross terminal barrier");
			clearWakes();
			const auto beforeWake = input.CaptureWakeGeneration();
			check(window.TryDequeueCanvasCommand(command) && isTerminal(1, target.key) && !first,
				"reserved terminal preserves payload identity");
			check(input.CaptureWakeGeneration() != beforeWake, "terminal consumption wakes next retained delivery");
			check(window.TryDequeueCanvasCommand(command) && command.type == scene.type &&
				command.presentationTarget->targetRevision == 3, "adjacent targets after barrier still coalesce");
			check(window.TryDequeueCanvasCommand(command) && command.type == undo.type &&
				!window.HasPendingCanvasCommand(), "ordinary command after terminal keeps FIFO");
			check(window.TryEnqueuePresentationCompletion(second) && window.HasPendingCanvasCommand(),
				"terminal alone keeps command boundary pending");
			window.DetachExternal();
			check(!window.HasPendingCanvasCommand() && !window.TryDequeueCanvasCommand(command),
				"stop clears terminal slot and barrier count");
			window.SetInputCoordinator(&input);

			// 使用真实 service 和 Host 共用 drain；不初始化 HWND、GPU 或 RTS。
			wchar_t temporary[MAX_PATH]{};
			const DWORD temporaryLength = GetTempPathW(MAX_PATH, temporary);
			check(temporaryLength != 0 && temporaryLength < MAX_PATH, "private fixture root available");
			if (temporaryLength == 0 || temporaryLength >= MAX_PATH) return 1;
			struct TemporaryRoot
			{
				std::wstring path;
				bool created = false;
				~TemporaryRoot() { if (created) RemoveDirectoryW(path.c_str()); }
			} root{ std::wstring(temporary) + L"InkeysPptCompletion-" + std::to_wstring(GetCurrentProcessId()) +
				L"-" + std::to_wstring(GetTickCount64()) };
			root.created = CreateDirectoryW(root.path.c_str(), nullptr) != FALSE;
			check(root.created, "real service uses a private fixture root");
			if (!root.created) return 1;
			HiddenPresentationAccounting accounting;
			PagedInkStoreTestHook hook;
			hook.context = &accounting;
			hook.recordAccepted = [](void* context) noexcept
			{
				return static_cast<HiddenPresentationAccounting*>(context)->Accept();
			};
			PresentationStoreRegistry stores;
			stores.BeginRun();
			const auto wake = [](void* context) noexcept
			{
				(void)static_cast<ContactInputCoordinator*>(context)->PublishControlWake();
			};
			const auto getStore = [&](const Bridge::PresentationTarget& storeTarget)
			{
				return stores.Get(storeTarget, root.path, 1.0f, wake, &input, hook);
			};
			auto* store = getStore(target);
			check(store != nullptr, "real document store starts");
			if (!store) return 1;
			auto fallback = target;
			fallback.bindingMode = Bridge::SlideBindingMode::PageIndexFallback;
			check(getStore(fallback) == store, "two bindings use the same document store");
			auto another = target;
			another.key.bytes.back() ^= 1;
			auto* secondStore = getStore(another);
			check(secondStore != nullptr && secondStore != store, "document registry uses the complete key");
			if (!secondStore) return 1;

			clearWakes();
			const auto beforeServiceWake = input.CaptureWakeGeneration();
			for (std::uint64_t generation : { 11, 12, 13 })
				check(store->LoadLane({ 0, generation, generation + 10 }) == PagedInkAdmission::Accepted,
					"real service accepts each identity-stamped lane request");
			stores.Drain();
			check(input.CaptureWakeGeneration() != beforeServiceWake, "service publication wakes the Host pump");
			std::optional<PresentationPagedCompletion> pending;
			std::size_t droppedTerminals = 0;
			const auto pump = [&](bool dropForTest = false)
			{
				return DrainPresentationCompletions(stores, pending, window,
					[&](const PagedInkCompletion& completion) noexcept
					{
						if (dropForTest)
						{
							++droppedTerminals;
							return false;
						}
						accounting.Settle(completion);
						return true;
					});
			};
			const auto isEmptyLane = [&]
			{
				const auto& completion = command.presentationPersistenceCompletion->completion;
				return completion.result == PagedInkResult::Succeeded && completion.lanePages.empty();
			};
			window.EnqueueCanvasCommand(clear);
			check(pump() == 1 && pending && pending->completion.identity.slotGeneration == 12,
				"full slot retains second terminal in Host");
			window.EnqueueCanvasCommand(undo);
			check(window.TryDequeueCanvasCommand(command) && command.type == clear.type,
				"real delivery follows earlier ordinary command");
			check(window.TryDequeueCanvasCommand(command) && isTerminal(11, target.key) && isEmptyLane(),
				"first empty-lane success delivered once");
			check(pump() == 1 && pending && pending->completion.identity.slotGeneration == 13,
				"third terminal retained while second waits");
			check(window.TryDequeueCanvasCommand(command) && command.type == undo.type,
				"later terminal cannot overtake already queued ordinary command");
			check(window.TryDequeueCanvasCommand(command) && isTerminal(12, target.key) && isEmptyLane(),
				"second empty-lane success not starved");
			check(pump() == 1 && !pending, "last terminal drains without a new persistence event or user input");
			check(window.TryDequeueCanvasCommand(command) && isTerminal(13, target.key) && isEmptyLane() &&
				!window.HasPendingCanvasCommand(), "all accepted terminals delivered in order");

			check(store->LoadLane({ 0, 14, 24 }) == PagedInkAdmission::Accepted &&
				secondStore->LoadLane({ 0, 15, 25 }) == PagedInkAdmission::Accepted,
				"the two real stores accept concurrent lane requests");
			stores.Close();
			check(pump() == 1 && pending && pending->key == target.key &&
				window.TryDequeueCanvasCommand(command) && isTerminal(15, another.key) && isEmptyLane() &&
				pump() == 1 && !pending && window.TryDequeueCanvasCommand(command) &&
				isTerminal(14, target.key) && isEmptyLane(),
				"document rotation preserves routing even while the slot is occupied");

			first = terminal(1);
			check(window.TryEnqueuePresentationCompletion(first), "fixture leaves old terminal pending");
			pending = terminal(2);
			window.DetachExternal();
			pending.reset(); // 与 Host Stop 的 owner-join 后清理一致。
			stores.Clear();
			stores.BeginRun();
			window.SetInputCoordinator(&input);
			store = getStore(target);
			check(store != nullptr, "document store starts in the next Host run");
			if (!store) return 1;
			check(store->LoadLane({ 0, 16, 26 }) == PagedInkAdmission::Accepted, "new generation load accepted");
			stores.Close();
			check(pump() == 1 && window.TryDequeueCanvasCommand(command) &&
				isTerminal(16, target.key) && isEmptyLane() && !window.HasPendingCanvasCommand() && !pending,
				"restart cannot deliver stale terminal");
			check(accounting.counts.presentationAccepted == 6 && accounting.counts.presentationLoaded == 6 &&
				accounting.counts.presentationOutstanding == 0 && accounting.counts.presentationUnmatchedTerminals == 0,
				"every request from the real stores settles once");

			// 实际准入后丢弃一个真实终态，验证已排空的服务仍有一笔未对账。
			stores.Clear();
			stores.BeginRun();
			store = getStore(target);
			check(store && store->LoadLane({ 0, 17, 27 }) == PagedInkAdmission::Accepted &&
				accounting.counts.presentationAccepted == 7 && accounting.counts.presentationOutstanding == 1,
				"real admission is counted before the Host receives a terminal");
			stores.Close();
			check(pump(true) == 0 && droppedTerminals == 1 && !pending && !window.HasPendingCanvasCommand() &&
				accounting.counts.presentationOutstanding == 1 && accounting.counts.presentationLoaded == 6 &&
				accounting.counts.presentationUnmatchedTerminals == 0,
				"a dropped terminal leaves one accepted request outstanding");

			HiddenPresentationAccounting mixedAccounting;
			auto mixed = terminal(18);
			mixed->completion.requestId = mixedAccounting.Accept();
			mixed->completion.result = PagedInkResult::Succeeded;
			mixed->completion.lanePages.resize(3);
			mixed->completion.lanePages.front().result = PagedInkResult::Succeeded;
			mixedAccounting.Settle(mixed->completion);
			check(mixedAccounting.counts.presentationLanePageFailures == 2 &&
				mixedAccounting.counts.presentationFailed == 1 && mixedAccounting.counts.presentationLoaded == 0 &&
				mixedAccounting.counts.presentationEmptyLane == 0,
				"two failed pages count separately and their lane counts as one failure");
			mixedAccounting.Settle(mixed->completion);
			check(mixedAccounting.counts.presentationUnmatchedTerminals == 1 &&
				mixedAccounting.counts.presentationOutstanding == 0 && mixedAccounting.counts.presentationFailed == 1 &&
				mixedAccounting.counts.presentationLanePageFailures == 2,
				"a duplicate terminal is unmatched and cannot settle a request twice");

			window.SetInputCoordinator(nullptr);
			root.created = RemoveDirectoryW(root.path.c_str()) == FALSE;
			check(!root.created, "empty lanes leave the fixture root empty");
		}
		catch (...)
		{
			check(false, "completion delivery fixture completes without exception");
		}
		if (failures == 0)
			std::fputs("[Draw3PptCompletion] PASS: reserved FIFO terminal, real Host drain, wake, restart and request accounting\n", stderr);
		return failures == 0 ? 0 : 1;
	}

#endif

	struct Host::Impl
	{
		struct RuntimeMetricsState
		{
			// 会话在 drawing 之前声明，析构顺序也保证 Controller 的借用先结束。
			std::unique_ptr<RuntimeMetricsSession> session;
			std::atomic_bool enabled = false, unavailable = false, joined = false, sealed = false;
			std::atomic_bool startupFailed = false, runFailed = false;
			mutable std::atomic_bool exportUnavailable = false;
			std::atomic_uint64_t runSerial = 0;
			std::atomic_uint64_t contactSeen = 0, confirmed = 0, pending = 0;
			std::atomic_uint64_t invalid = 0, contactDropped = 0, pendingOverflow = 0;
			std::atomic_uint64_t frameSerial = 0, presentAttempts = 0;
			std::atomic_uint64_t allocatedBytes = 0, payloadByteUpperBound = 0, effectiveSamples = 0;
			std::uint64_t nextRunSerial = 0;
			bool ownerCreated = false;
			mutable std::mutex metadataMutex;
			HostRuntimeMetricsMetadata metadata;
			ContactInputDiagnosticsSnapshot inputBaseline, finalInput;
			RuntimeMetricsSnapshot finalSnapshot;
		};
		static_assert(sizeof(RuntimeMetricsState) <= 4096);
		RuntimeMetricsState runtimeMetrics;
		Bridge::StateBridge bridge;
		// 设备性能账本：声明在 drawing 之前，析构晚于 Controller；Start 早于绘制线程、Stop 晚于 join。
		DevicePerformanceLedger devicePerformance;
		// 只由绘制 owner 访问；槽占用时保留终态，下一次 General wake 再交付。
		std::optional<PresentationPagedCompletion> pendingPresentationCompletion;
		std::optional<DesktopInkCompletion> pendingDesktopCompletion;
		bool desktopPersistenceInstallationClosed = false; // 由绘制 owner 在退出 ACK 前封闭。
		ContactInputCoordinator input;
		// 声明在 input 之后，析构时 worker 不会唤醒已析构的协调器。
		DesktopInkSnapshots desktopSnapshots;
		PresentationStoreRegistry presentationStores;
		// ---- C1.6b 崩溃恢复会话 ----
		struct RecoveryAdapter
		{
			Recovery::Session* session = nullptr;
			Recovery::Workspace slot = Recovery::Workspace::Whiteboard;
			std::string key;
		};
		Recovery::Session recoverySession;
		// 启动恢复线程：扫描被放弃的会话、弹窗、把桌面区间交给绘制线程装入。只认领最新的一份。
		std::thread recoveryThread;
		std::atomic<bool> recoveryStop{ false };
		void* recoveryClaim = nullptr; // 认领锁持有到进程结束（DELETE_ON_CLOSE）
		std::vector<void*> olderRecoveryClaims; // 较旧的被放弃会话：认领后才删除或借用，同样持有到进程结束
		// 装入后的落盘结果（绘制线程回报）：确认写进本进程文件后才结算旧记录。
		std::optional<bool> desktopRecoveryDurable;
		std::optional<bool> whiteboardRecoveryDurable;
		// 开始停止（退出屏障之前）：恢复线程不再弹窗或交接装入，绘制线程不再取待装入数据。
		std::atomic<bool> recoveryClosing{ false };
		std::stop_source recoveryPromptStop; // Stop 入口取消仍在显示的恢复询问，恢复线程才能及时退出
		std::mutex recoveryMutex;
		std::condition_variable recoveryCondition;
		std::optional<draw3::uink::Draw3UInkCanvasSnapshot> pendingDesktopRecovery;
		// 上一区间（desktop\ 下最近一次 Clear 的快照）：随当前区间一起交给绘制线程，恢复后可撤回到这一区间。
		std::optional<draw3::uink::Draw3UInkCanvasSnapshot> pendingDesktopPrevious;
		// 本次运行最近一次桌面 Clear 快照的请求 ID：其保存成功后登记为会话的上一区间。
		std::mutex desktopClearRequestMutex;
		std::optional<std::array<std::uint8_t, 16>> desktopClearRequestId;
		std::atomic<bool> desktopRecoveryPending{ false };
		std::optional<bool> desktopRecoveryResult;
		// PPT：本次启动内识别到同一文稿时接管（键为 FormatPresentationKey）。扫描完成前识别到的文稿短暂等待。
		struct PresentationAdopt
		{
			std::vector<draw3::uink::UInkCanvas> pages;
			std::optional<draw3::uink::UInkGuid> workspaceGuid;
		};
		std::map<std::string, PresentationAdopt> pendingPresentationAdopt;
		bool presentationScanDone = true;
		// 白板：未进入白板时装入白板槽。
		std::optional<std::vector<draw3::uink::Draw3UInkCanvasSnapshot>> pendingWhiteboardRecovery;
		std::size_t pendingWhiteboardPage = 0;
		std::atomic<bool> whiteboardRecoveryPending{ false };
		std::optional<bool> whiteboardRecoveryResult;

		static bool TakeWhiteboardRecovery(void* context, std::vector<draw3::uink::Draw3UInkCanvasSnapshot>& pages,
			std::size_t& currentPage) noexcept
		{
			auto& self = *static_cast<Impl*>(context);
			if (!self.whiteboardRecoveryPending.load(std::memory_order_acquire) ||
				self.recoveryClosing.load(std::memory_order_acquire)) return false;
			try
			{
				const std::scoped_lock lock(self.recoveryMutex);
				if (!self.pendingWhiteboardRecovery) return false;
				pages = std::move(*self.pendingWhiteboardRecovery);
				currentPage = self.pendingWhiteboardPage;
				self.pendingWhiteboardRecovery.reset();
				self.whiteboardRecoveryPending.store(false, std::memory_order_release);
				return true;
			}
			catch (...) { return false; }
		}

		static void WhiteboardRecoveryInstalled(void* context, bool installed) noexcept
		{
			auto& self = *static_cast<Impl*>(context);
			{
				const std::scoped_lock lock(self.recoveryMutex);
				self.whiteboardRecoveryResult = installed;
			}
			self.recoveryCondition.notify_all();
		}

		static bool TakeDesktopRecovery(void* context, draw3::uink::Draw3UInkCanvasSnapshot& output,
			std::optional<draw3::uink::Draw3UInkCanvasSnapshot>& previous) noexcept
		{
			auto& self = *static_cast<Impl*>(context);
			if (!self.desktopRecoveryPending.load(std::memory_order_acquire) ||
				self.recoveryClosing.load(std::memory_order_acquire)) return false;
			try
			{
				const std::scoped_lock lock(self.recoveryMutex);
				if (!self.pendingDesktopRecovery) return false;
				output = std::move(*self.pendingDesktopRecovery);
				previous = std::move(self.pendingDesktopPrevious);
				self.pendingDesktopRecovery.reset();
				self.pendingDesktopPrevious.reset();
				self.desktopRecoveryPending.store(false, std::memory_order_release);
				return true;
			}
			catch (...) { return false; }
		}

		static void RecoveryInkVisible(void* context) noexcept
		{
			auto& self = *static_cast<Impl*>(context);
			try
			{
				if (self.startOptions.recoveryEnterDrawing)
					self.startOptions.recoveryEnterDrawing(self.startOptions.recoveryPromptContext);
			}
			catch (...) {}
		}

		static void RecoveryDurable(void* context, std::uint8_t scene, bool durable) noexcept
		{
			auto& self = *static_cast<Impl*>(context);
			{
				const std::scoped_lock lock(self.recoveryMutex);
				(scene == 0 ? self.desktopRecoveryDurable : self.whiteboardRecoveryDurable) = durable;
			}
			self.recoveryCondition.notify_all();
		}

		// 恢复线程等待落盘结果；退出或超时按未结算处理（下次启动仍可恢复）。
		bool WaitRecoveryDurable(std::optional<bool>& result) noexcept
		{
			try
			{
				std::unique_lock lock(recoveryMutex);
				recoveryCondition.wait_for(lock, std::chrono::minutes(2), [&]
				{
					return result.has_value() || recoveryStop.load(std::memory_order_acquire);
				});
				return result.value_or(false);
			}
			catch (...) { return false; }
		}

		static void DesktopRecoveryInstalled(void* context, bool installed) noexcept
		{
			auto& self = *static_cast<Impl*>(context);
			{
				const std::scoped_lock lock(self.recoveryMutex);
				self.desktopRecoveryResult = installed;
			}
			self.recoveryCondition.notify_all();
		}

		// 恢复读取：FileId 与记录一致且有写前登记时截到登记起点（该次整页、搬页或图形组写入不完整），
		// 否则读全文件；单块残缺由解码器丢弃。返回各页最新完整版本。
		static std::optional<draw3::uink::UInkReadResult> ReadRecoveredFile(const Recovery::FileEntry& entry) noexcept
		{
			Recovery::FileIdentity actual;
			if (!Recovery::QueryFileIdentity(entry.path, actual)) return std::nullopt;
			std::uint64_t limit = (std::numeric_limits<std::uint64_t>::max)();
			if (entry.pending && entry.identity.valid && actual == entry.identity) limit = entry.pending->start;
			auto read = draw3::uink::ReadUInkStoreFilePrefix(entry.path, limit);
			if (!read.document || read.document->canvases.empty()) return std::nullopt;
			return read;
		}

		// desktop\ 下的单页 Clear 快照（隐式 Workspace、单 Canvas）。读不出或无墨迹时返回空。
		static std::optional<draw3::uink::Draw3UInkCanvasSnapshot> ReadDesktopSnapshot(const std::wstring& path,
			float dpiScale) noexcept
		{
			try
			{
				const auto read = draw3::uink::ReadUInkStoreFile(path);
				if (!read.document || read.document->canvases.size() != 1) return std::nullopt;
				draw3::uink::Draw3UInkV11ImportOptions importOptions;
				importOptions.dpiScale = dpiScale;
				auto imported = draw3::uink::ImportUInkV11Page(read.document->canvases.front(), importOptions);
				if (!imported.page || imported.page->strokes.empty()) return std::nullopt;
				return std::move(imported.page);
			}
			catch (...) { return std::nullopt; }
		}

		static std::uint64_t LastWriteFileTime(const std::wstring& path) noexcept
		{
			WIN32_FILE_ATTRIBUTE_DATA data{};
			if (!GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &data)) return 0;
			return (static_cast<std::uint64_t>(data.ftLastWriteTime.dwHighDateTime) << 32) | data.ftLastWriteTime.dwLowDateTime;
		}

		// 用户决定（2026-11-27）：最后一次保存墨迹距今不超过 5 分钟直接恢复，超过才询问。
		// 取不到时间或时间在未来（时钟回拨）时按需要询问处理。
		static bool RecoveryNeedsPrompt(std::uint64_t lastWriteFileTime) noexcept
		{
			constexpr std::uint64_t kSilentWindow = 5ull * 60ull * 10'000'000ull; // FILETIME 单位 100 ns
			if (lastWriteFileTime == 0) return true;
			FILETIME now{};
			GetSystemTimeAsFileTime(&now);
			const std::uint64_t current = (static_cast<std::uint64_t>(now.dwHighDateTime) << 32) | now.dwLowDateTime;
			return current < lastWriteFileTime || current - lastWriteFileTime > kSilentWindow;
		}

		void RunRecoveryScan(std::wstring root, HostStartOptions options, float dpiScale) noexcept
		{
			struct ScanDone
			{
				Impl& self;
				~ScanDone()
				{
					{
						const std::scoped_lock lock(self.recoveryMutex);
						self.presentationScanDone = true;
					}
					self.recoveryCondition.notify_all();
				}
			} scanDone{ *this };
			try
			{
				auto sessions = Recovery::ScanAbandonedSessions(root, recoverySession.Name());
				if (sessions.empty()) return;
				auto& session = sessions.front();
				recoveryClaim = Recovery::ClaimSession(root, session.baseName);
				if (!recoveryClaim) return; // 已被另一个实例认领
				auto& record = session.record;
				// 只恢复最新的一份。较旧的被放弃会话先认领：最新会话缺某个场景而旧会话该场景未结算时借用过来
				// （双重崩溃：上一次恢复装入后、写盘确认前又崩溃），其余旧记录在最新记录持久化后删除（文件留在磁盘上）。
				std::vector<std::wstring> olderRecords;
				bool canDeleteOlderRecords = true;
				enum class CandidatePath { Retained, Missing, Unknown };
				const auto candidatePath = [&](const std::optional<Recovery::FileEntry>& entry) noexcept
				{
					if (!entry || entry->identity.valid || GetFileAttributesW(entry->path.c_str()) != INVALID_FILE_ATTRIBUTES)
						return CandidatePath::Retained;
					const auto error = GetLastError();
					if (error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND) return CandidatePath::Missing;
					// 权限或 I/O 错误无法确认候选文件是否已发布，保留条目和旧来源供下次扫描。
					canDeleteOlderRecords = false;
					return CandidatePath::Unknown;
				};
				auto desktopPath = candidatePath(record.desktop);
				auto whiteboardPath = candidatePath(record.whiteboard);
				if (desktopPath == CandidatePath::Missing) record.desktop.reset();
				if (whiteboardPath == CandidatePath::Missing) record.whiteboard.reset();
				// 桌面只看最近一个带桌面状态的会话，已清空、已结算或未能认领的状态都能挡住更旧条目。
				bool desktopResolved = record.desktop || record.desktopCleared || record.desktopClearGeneration != 0 ||
					(record.desktopPreviousInterval && record.desktopPreviousGeneration == record.desktopClearGeneration);
				for (std::size_t index = 1; index < sessions.size(); ++index)
				{
					const auto& older = sessions[index].record;
					const auto olderDesktopPath = candidatePath(older.desktop);
					const auto olderWhiteboardPath = candidatePath(older.whiteboard);
					const bool olderHasDesktopState = (older.desktop && olderDesktopPath != CandidatePath::Missing) ||
						older.desktopCleared || older.desktopClearGeneration != 0 ||
						(older.desktopPreviousInterval && older.desktopPreviousGeneration == older.desktopClearGeneration);
					void* claim = Recovery::ClaimSession(root, sessions[index].baseName);
					if (!claim)
					{
						canDeleteOlderRecords = false;
						desktopResolved = desktopResolved || olderHasDesktopState;
						continue;
					}
					olderRecoveryClaims.push_back(claim);
					olderRecords.push_back(sessions[index].recordPath);
					if (!desktopResolved && olderHasDesktopState)
					{
						// 已结算的桌面状态也并入最新记录，防止旧记录只删掉一部分时丢失 Clear 边界。
						desktopResolved = true;
						desktopPath = olderDesktopPath;
						record.desktop = desktopPath == CandidatePath::Missing ? std::nullopt : older.desktop;
						record.desktopCleared = older.desktopCleared;
						record.desktopClearGeneration = older.desktopClearGeneration;
						record.desktopPreviousInterval = older.desktopPreviousInterval;
						record.desktopPreviousGeneration = older.desktopPreviousGeneration;
						record.settledDesktop = older.settledDesktop;
					}
					if (!record.whiteboard && older.whiteboard && olderWhiteboardPath != CandidatePath::Missing && !older.settledWhiteboard)
					{
						whiteboardPath = olderWhiteboardPath;
						record.whiteboard = older.whiteboard;
						record.whiteboardPageIndex = older.whiteboardPageIndex;
						record.whiteboardPageGuid = older.whiteboardPageGuid;
						record.settledWhiteboard = false;
					}
				}
				const bool crashSceneDesktop = record.workspace == Recovery::Workspace::Desktop;
				// PPT 只在本次启动内待接管；先暂存读出的页，等消费标记尝试落盘后再交给绘制线程。
				std::map<std::string, PresentationAdopt> presentationAdopt;
				const auto settledPresentationCount = record.settledPresentations.size();
				for (const auto& entry : record.presentations)
				{
					if (std::find(record.settledPresentations.begin(), record.settledPresentations.end(), entry.key)
						!= record.settledPresentations.end()) continue;
					if (auto read = ReadRecoveredFile(entry.file))
					{
						PresentationAdopt adopt;
						if (read->document->headerExtension && !read->document->headerExtension->workspaces.empty())
							adopt.workspaceGuid = read->document->headerExtension->workspaces.front().guid;
						adopt.pages = std::move(read->document->canvases);
						presentationAdopt[entry.key] = std::move(adopt);
					}
					record.settledPresentations.push_back(entry.key);
				}
				const bool mergedPersisted = Recovery::WriteSessionRecord(session.recordPath, record);
				{
					const std::scoped_lock lock(recoveryMutex);
					for (auto& [key, adopt] : presentationAdopt)
						pendingPresentationAdopt[key] = std::move(adopt);
					presentationScanDone = true;
				}
				recoveryCondition.notify_all();
				if (!mergedPersisted && record.settledPresentations.size() != settledPresentationCount)
					DiagnosticOutput::Text(DiagnosticOutput::OutWarn, "[Draw3.Recovery] action=settle_presentations result=not_persisted\n");
				// 合并未落盘或仍有旧来源时，保留最新记录作为桌面恢复边界。
				bool olderRecordsDeleted = mergedPersisted && canDeleteOlderRecords;
				if (olderRecordsDeleted)
					for (const auto& path : olderRecords)
						if (!DeleteFileW(path.c_str()) && GetLastError() != ERROR_FILE_NOT_FOUND) olderRecordsDeleted = false;
				// 白板：崩溃场景且有墨迹时弹窗（静默重启档除外），否则静默恢复数据、界面留在桌面。
				if (record.whiteboard && !record.settledWhiteboard && whiteboardPath != CandidatePath::Unknown)
				{
					auto read = ReadRecoveredFile(*record.whiteboard);
					bool hasInk = false;
					if (read)
						for (const auto& canvas : read->document->canvases) hasInk = hasInk || !canvas.content.empty();
					if (!read || !hasInk)
					{
						if (read || ++record.failedRecoveryAttempts >= 3) record.settledWhiteboard = true;
					}
					else
					{
						const bool crashSceneWhiteboard = record.workspace == Recovery::Workspace::Whiteboard;
						auto promptResult = RecoveryPromptResult::Accepted;
						const auto lastWrite = LastWriteFileTime(record.whiteboard->path);
						if (crashSceneWhiteboard && !options.recoverySkipPrompt && options.recoveryPrompt &&
							RecoveryNeedsPrompt(lastWrite) && !recoveryClosing.load(std::memory_order_acquire))
							promptResult = options.recoveryPrompt(options.recoveryPromptContext, 1, lastWrite, recoveryPromptStop.get_token());
						// 退出取消当前场景，仍需收尾并保留已完成的结算。
						if (recoveryClosing.load(std::memory_order_acquire)) promptResult = RecoveryPromptResult::Unavailable;
						if (promptResult == RecoveryPromptResult::Declined) record.settledWhiteboard = true;
						else if (promptResult == RecoveryPromptResult::Accepted)
						{
							draw3::uink::Draw3UInkV11ImportOptions importOptions;
							importOptions.dpiScale = dpiScale;
							std::vector<draw3::uink::Draw3UInkCanvasSnapshot> pages;
							for (const auto& canvas : read->document->canvases)
								if (auto imported = draw3::uink::ImportUInkV11Page(canvas, importOptions); imported.page)
									pages.push_back(std::move(*imported.page));
							{
								const std::scoped_lock lock(recoveryMutex);
								pendingWhiteboardRecovery = std::move(pages);
								pendingWhiteboardPage = record.whiteboardPageIndex.value_or(0);
								whiteboardRecoveryResult.reset();
								whiteboardRecoveryDurable.reset(); // 发布前清空：落盘结果可能紧跟装入结果到达
								whiteboardRecoveryPending.store(true, std::memory_order_release);
							}
							(void)input.PublishControlWake();
							std::unique_lock lock(recoveryMutex);
							recoveryCondition.wait_for(lock, std::chrono::seconds(60), [this]
							{
								return whiteboardRecoveryResult.has_value() || recoveryStop.load(std::memory_order_acquire);
							});
							if (whiteboardRecoveryResult.value_or(false))
							{
								lock.unlock();
								if (crashSceneWhiteboard && options.recoveryEnterWhiteboard)
									options.recoveryEnterWhiteboard(options.recoveryPromptContext);
								// 结算等装入的各页确认写进本进程的白板文件：之前再崩溃，旧记录仍可恢复。
								if (WaitRecoveryDurable(whiteboardRecoveryDurable)) record.settledWhiteboard = true;
								else DiagnosticOutput::Text(DiagnosticOutput::OutWarn,
									"[Draw3.Recovery] action=whiteboard_install result=deferred reason=not_durable\n");
							}
							else
							{
								pendingWhiteboardRecovery.reset();
								whiteboardRecoveryPending.store(false, std::memory_order_release);
							}
						}
					}
				}

				// 上一区间快照与最新 Clear 同代次才有效（Clear 后崩溃、尚未落新笔时桌面条目为空，但仍可恢复后撤回）。
				const bool previousValid = record.desktopPreviousInterval &&
					record.desktopPreviousGeneration == record.desktopClearGeneration;
				// 已在退出：不再弹窗装入桌面，直接写下白板的结算结果。
				if ((record.desktop || previousValid) && !record.settledDesktop &&
					desktopPath != CandidatePath::Unknown && !recoveryClosing.load(std::memory_order_acquire))
				{
					auto read = record.desktop ? ReadRecoveredFile(*record.desktop) : std::nullopt;
					const draw3::uink::UInkCanvas* canvas = read ? &read->document->canvases.front() : nullptr;
					const bool hasCurrent = canvas && !canvas->content.empty();
					std::optional<draw3::uink::Draw3UInkCanvasSnapshot> previous;
					if (previousValid) previous = ReadDesktopSnapshot(*record.desktopPreviousInterval, dpiScale);
					// 区分“无条目 / 读到空页 / 读失败”：读失败沿用 3 次重试预算，不用另一段内容顶替后结算。
					const bool currentFailed = record.desktop && !read;
					const bool previousFailed = previousValid && !previous;
					if (currentFailed || (!hasCurrent && !previous))
					{
						if ((currentFailed || previousFailed) ? ++record.failedRecoveryAttempts >= 3 : true)
							record.settledDesktop = true;
					}
					else
					{
						auto promptResult = RecoveryPromptResult::Accepted;
						const auto lastWrite = record.desktop ? LastWriteFileTime(record.desktop->path) : 0;
						// 只有当前区间有墨迹时才询问；只剩上一区间（Clear 后崩溃）时静默装入撤回点。
						if (hasCurrent && crashSceneDesktop && !options.recoverySkipPrompt && options.recoveryPrompt &&
							RecoveryNeedsPrompt(lastWrite) && !recoveryClosing.load(std::memory_order_acquire))
							promptResult = options.recoveryPrompt(options.recoveryPromptContext, 0, lastWrite, recoveryPromptStop.get_token());
						// 退出时跳过桌面恢复，继续写回此前的白板结算。
						if (recoveryClosing.load(std::memory_order_acquire)) promptResult = RecoveryPromptResult::Unavailable;
						if (promptResult == RecoveryPromptResult::Declined) record.settledDesktop = true;
						else if (promptResult == RecoveryPromptResult::Accepted)
						{
							draw3::uink::Draw3UInkV11ImportOptions importOptions;
							importOptions.dpiScale = dpiScale;
							std::optional<draw3::uink::Draw3UInkCanvasSnapshot> current;
							if (hasCurrent) current = draw3::uink::ImportUInkV11Page(*canvas, importOptions).page;
							else if (previous)
							{
								current.emplace();
								current->pageGuid = previous->pageGuid; // 空的当前区间：只装入撤回点
							}
							// 交接时的 Clear 代次：装入后若用户又 Clear 过，上一区间不再属于当前区间，放弃接手。
							const auto expectedGeneration = recoverySession.DesktopClearGeneration();
							if (current && !recoveryClosing.load(std::memory_order_acquire))
							{
								{
									const std::scoped_lock lock(recoveryMutex);
									pendingDesktopRecovery = std::move(current);
									pendingDesktopPrevious = std::move(previous);
									desktopRecoveryResult.reset();
									desktopRecoveryDurable.reset();
									desktopRecoveryPending.store(true, std::memory_order_release);
								}
								(void)input.PublishControlWake();
								std::unique_lock lock(recoveryMutex);
								// 绘制线程在空闲时装入；恢复线程等待结果，退出时放弃（保持未结算）。
								recoveryCondition.wait_for(lock, std::chrono::seconds(60), [this]
								{
									return desktopRecoveryResult.has_value() || recoveryStop.load(std::memory_order_acquire);
								});
								if (desktopRecoveryResult.value_or(false))
								{
									lock.unlock();
									if (WaitRecoveryDurable(desktopRecoveryDurable))
									{
										// 本进程接手上一区间：再次崩溃时仍能撤回到它。代次已变（装入后又 Clear）时不接手；
										// 登记失败则保留旧记录，下次启动仍可恢复。
										bool handedOver = true;
										if (previousValid && recoverySession.DesktopClearGeneration() == expectedGeneration)
											handedOver = recoverySession.RegisterDesktopPreviousInterval(*record.desktopPreviousInterval,
												expectedGeneration);
										record.settledDesktop = handedOver;
									}
									else DiagnosticOutput::Text(DiagnosticOutput::OutWarn,
										"[Draw3.Recovery] action=desktop_install result=deferred reason=not_durable\n");
								}
								else
								{
									pendingDesktopRecovery.reset();
									pendingDesktopPrevious.reset();
									desktopRecoveryPending.store(false, std::memory_order_release);
									DiagnosticOutput::Text(DiagnosticOutput::OutWarn,
										"[Draw3.Recovery] action=desktop_install result=deferred reason=not_installed\n");
								}
							}
							else if (++record.failedRecoveryAttempts >= 3) record.settledDesktop = true;
						}
					}
				}
				const bool presentationsSettled = std::all_of(record.presentations.begin(), record.presentations.end(),
					[&](const Recovery::PresentationEntry& entry)
					{
						return std::find(record.settledPresentations.begin(), record.settledPresentations.end(),
							entry.key) != record.settledPresentations.end();
					});
				if (olderRecordsDeleted && (!(record.desktop || previousValid) || record.settledDesktop) &&
					(!record.whiteboard || record.settledWhiteboard) && presentationsSettled &&
					(DeleteFileW(session.recordPath.c_str()) || GetLastError() == ERROR_FILE_NOT_FOUND)) return;
				// 删除未完成时写回最新结算，避免下次启动重复恢复。
				(void)Recovery::WriteSessionRecord(session.recordPath, record);
			}
			catch (...) {}
		}
		RecoveryAdapter whiteboardRecovery{ &recoverySession, Recovery::Workspace::Whiteboard, {} };
		RecoveryAdapter desktopRecovery{ &recoverySession, Recovery::Workspace::Desktop, {} };
		std::map<std::string, std::unique_ptr<RecoveryAdapter>> presentationRecovery;

		static bool RecoveryRegisterFile(void* context, const std::wstring& path) noexcept
		{
			auto* adapter = static_cast<RecoveryAdapter*>(context);
			if (!adapter || !adapter->session || !adapter->session->Active()) return true;
			Recovery::FileIdentity identity;
			(void)Recovery::QueryFileIdentity(path, identity);
			bool registered = false;
			switch (adapter->slot)
			{
			case Recovery::Workspace::Desktop: registered = adapter->session->RegisterDesktopFile(path, identity); break;
			case Recovery::Workspace::Whiteboard: registered = adapter->session->RegisterWhiteboardFile(path, identity); break;
			default: registered = adapter->session->RegisterPresentationFile(adapter->key, path, identity); break;
			}
			// 开关关闭：照常尝试登记（便于中途开启），但会话记录写失败不影响 C1.4 的普通保存。
			return registered || !adapter->session->RecoverInk();
		}

		static bool RecoveryRegisterPending(void* context, const std::wstring& path, std::uint64_t start,
			std::uint8_t kind) noexcept
		{
			auto* adapter = static_cast<RecoveryAdapter*>(context);
			if (!adapter || !adapter->session || !adapter->session->Active()) return true;
			// 开关关闭：不做同步登记，写入开销与 C1.4 一致（清除照常，用于收掉开启期间的残留登记）。
			if (!adapter->session->RecoverInk()) return true;
			// 恢复只在文件身份一致时才按登记截断：查不到身份的登记形同虚设，按登记失败处理。
			Recovery::FileIdentity identity;
			if (!Recovery::QueryFileIdentity(path, identity)) return false;
			Recovery::PendingWrite pending;
			pending.kind = static_cast<Recovery::PendingKind>(kind <= 3 ? kind : 0);
			pending.start = start;
			return adapter->session->RegisterPending(adapter->slot, adapter->key, identity, pending);
		}

		static bool RecoveryClearPending(void* context, const std::wstring& path) noexcept
		{
			auto* adapter = static_cast<RecoveryAdapter*>(context);
			if (!adapter || !adapter->session || !adapter->session->Active()) return true;
			Recovery::FileIdentity identity;
			(void)Recovery::QueryFileIdentity(path, identity);
			return adapter->session->ClearPending(adapter->slot, adapter->key, identity);
		}

		static bool RecoveryClearFile(void* context, std::uint64_t clearGeneration) noexcept
		{
			auto* adapter = static_cast<RecoveryAdapter*>(context);
			if (!adapter || !adapter->session || !adapter->session->Active()) return true;
			return adapter->slot != Recovery::Workspace::Desktop || adapter->session->ClearDesktopFile(clearGeneration);
		}

		static void RecoveryDecoratePresentationStore(void* context, PagedInkStoreOptions& options)
		{
			auto& self = *static_cast<Impl*>(context);
			if (!self.recoverySession.Active() || !options.hostId) return;
			try
			{
				// 扫描完成前识别到的文稿：有界等待（超时按无恢复处理），保证接管先于首次 LoadLane。
				std::unique_lock lock(self.recoveryMutex);
				self.recoveryCondition.wait_for(lock, std::chrono::seconds(3), [&]
				{
					return self.presentationScanDone || self.recoveryClosing.load(std::memory_order_acquire);
				});
				// 退出唤醒只结束等待，保留待接管页。
				if (const auto found = self.pendingPresentationAdopt.find(*options.hostId);
					found != self.pendingPresentationAdopt.end() && !self.recoveryClosing.load(std::memory_order_acquire))
				{
					if (found->second.workspaceGuid) options.pageContext.workspaceGuid = *found->second.workspaceGuid;
					options.adoptPages = std::move(found->second.pages);
					self.pendingPresentationAdopt.erase(found);
				}
			}
			catch (...) {}
			auto& adapter = self.presentationRecovery[*options.hostId];
			if (!adapter) adapter = std::make_unique<RecoveryAdapter>(RecoveryAdapter{ &self.recoverySession,
				Recovery::Workspace::Presentation, *options.hostId });
			options.recoveryContext = adapter.get();
			options.registerRecoveryFile = &RecoveryRegisterFile;
			options.registerRecoveryPending = &RecoveryRegisterPending;
			options.clearRecoveryPending = &RecoveryClearPending;
		}

		static void RecoverySceneChanged(void* context, Bridge::Workspace workspace,
			const std::uint8_t* pageGuid, std::size_t pageIndex) noexcept
		{
			auto& self = *static_cast<Impl*>(context);
			if (!self.recoverySession.Active()) return;
			self.recoverySession.PublishWorkspace(workspace == Bridge::Workspace::Whiteboard ? Recovery::Workspace::Whiteboard
				: workspace == Bridge::Workspace::Presentation ? Recovery::Workspace::Presentation : Recovery::Workspace::Desktop);
			if (pageGuid)
			{
				try
				{
					std::array<std::uint8_t, 16> bytes{};
					std::copy(pageGuid, pageGuid + bytes.size(), bytes.begin());
					self.recoverySession.PublishWhiteboardPage(draw3::uink::FormatUInkGuid(draw3::uink::UInkGuid(bytes)),
						static_cast<std::uint32_t>(pageIndex));
				}
				catch (...) {}
			}
		}
		float presentationStoreDpiScale = 1.0f;
		WindowController window;
		Haptics::ClickMailbox hapticClicks;
		std::mutex commandPublishMutex;
		mutable std::mutex eraserDiagnosticsMutex;
		SpeedEraser::Diagnostics eraserDiagnostics;
		PenRuntimeDiagnostics penDiagnostics;
		mutable std::mutex displayMutex;
		SpeedEraser::DevelopmentOptions eraserDevelopment;
		SpeedEraser::ContactAreaSample hiddenContactArea;
		std::atomic_bool touchAreaTraceEnabled = false;
		std::atomic_uint64_t touchAreaTraceRevision = 0;
		uint64_t observedTouchAreaTraceRevision = 0;
		std::chrono::steady_clock::time_point lastTouchAreaMetadataTrace{};
		SpeedEraser::InputSource lastTouchAreaMetadataSource;
		bool touchAreaMetadataPending = true;
		std::chrono::steady_clock::time_point lastTouchAreaTrace{};
		bool touchAreaTraceWasEnabled = false, lastTracedContact = false, lastTracedInputActivity = false;
		Inkeys::Display::SnapshotPtr pendingDisplaySnapshot;
		Inkeys::Display::Subscription displaySubscription;
		std::atomic_bool displayScaleDirty = false;
		RECT appliedDisplayClientBounds{};
		Inkeys::Business::PenWidth::Context appliedPenWidthContext;
		std::uintptr_t appliedPenWidthMonitor = 0;
		bool penWidthContextPrepared = false;
		// 初始化信息只在 owner 的状态边界更新；设置页不借用图形资源。
		mutable std::mutex canvasInformationMutex;
		std::uint64_t canvasInformationGeneration = 0;
		std::string canvasLifecycle = "尚未初始化";
		std::string canvasGraphics = "图形设备与交换链：尚未初始化\n";
		std::string canvasInput = "输入后端：尚未启用\n";
		std::string canvasInformation = "画布尚未初始化";
		PresentationFallbackReport canvasFallback;

		void UpdateCanvasInformation(const char* lifecycle = nullptr,
			const std::string* graphicsText = nullptr, const std::string* inputText = nullptr,
			bool beginGeneration = false, const PresentationFallbackReport* previous = nullptr) noexcept
		{
			try
			{
				std::scoped_lock lock(canvasInformationMutex);
				if (beginGeneration)
				{
					++canvasInformationGeneration;
					canvasFallback = previous ? *previous : PresentationFallbackReport{};
					canvasGraphics = "图形设备与交换链：尚未初始化\n";
					canvasInput = "输入后端：尚未启用\n";
				}
				if (lifecycle) canvasLifecycle = lifecycle;
				if (graphicsText) canvasGraphics = *graphicsText;
				if (inputText) canvasInput = *inputText;
				canvasInformation = "画布状态：" + canvasLifecycle + "\n初始化代次："
					+ std::to_string(canvasInformationGeneration) + "\n" + canvasGraphics + canvasInput;
			}
			catch (...) {} // 可选诊断失败不得干扰启动/退出协议。
		}

		void RecordHostStartupFailure(const char* stage, const char* detail = nullptr) noexcept
		{
			try
			{
				std::scoped_lock lock(canvasInformationMutex);
				// 优先保留 presenter 已发布的具体失败点和 HRESULT，供新 HWND 代次继承。
				if (!canvasFallback.lastAttemptFailure.empty()) return;
				canvasFallback.lastAttemptFailure = stage;
				if (detail && *detail) canvasFallback.lastAttemptFailure += std::string(": ") + detail;
			}
			catch (...) {} // 诊断分配失败不得中断启动清理。
		}

		static void ObserveCanvasGraphics(void* context, const std::string& information) noexcept
		{
			if (auto* self = static_cast<Impl*>(context))
				self->UpdateCanvasInformation(nullptr, &information);
		}

		static void ObserveCanvasFallback(void* context, const PresentationFallbackReport& report) noexcept
		{
			if (auto* self = static_cast<Impl*>(context))
			{
				try
				{
					std::scoped_lock lock(self->canvasInformationMutex);
					self->canvasFallback = report;
				}
				catch (...) {} // 可选诊断失败不干扰呈现恢复。
			}
		}

		void CaptureCanvasInputInformation(HostInputMode selected, bool triedRts) noexcept
		{
			try
			{
				const char* name = selected == HostInputMode::RtsExtended ? "RealTimeStylus (RTS，扩展属性)" :
					selected == HostInputMode::RtsBasicXY ? "RealTimeStylus (RTS，基础 X/Y)" :
					selected == HostInputMode::Pointer ? "WM_POINTER" :
					selected == HostInputMode::Win32Mouse ? (triedRts ? "Win32 鼠标兼容回退" : "Win32 鼠标兼容") : "不可用";
				std::string information = std::string("输入后端：") + name;
				if (triedRts) information += "\n" + stylus.StartupInformation();
				UpdateCanvasInformation(nullptr, nullptr, &information);
			}
			catch (...) {} // 诊断分配失败不使已就绪的输入后端变成失败。
		}

		GraphicsDeviceResources graphics;
		InkRenderer renderer;
		TransparentPresentationController presentation;
		std::atomic_bool ulwMultiStepCommitEnabled = true;
		// 启动前已有显式发布时，Start 不能再用启动选项里的旧值覆盖它。
		std::atomic_bool ulwMultiStepCommitPublished = false;
		std::optional<bool> appliedUlwMultiStepCommitEnabled;
		RealTimeStylusInput stylus;
		std::unique_ptr<DrawingController> drawing;
		std::atomic_bool interruptedStrokeReconnectEnabled = true;
		std::atomic_uint32_t interruptedStrokeReconnectDevices = 3; // bit0 触摸，bit1 笔尾
		uint32_t appliedInterruptedStrokeReconnectDevices = UINT32_MAX;
		// 一份原子快照保存通用偏好；绘制 owner 应用，不能跨线程借用 Controller。
		std::atomic_uint32_t generalDrawingOptions = 3u | (2u << 2);
		std::atomic<LiveTaperOptions> liveTaperOptions{ LiveTaperOptions{} };
		std::optional<LiveTaperOptions> appliedLiveTaperOptions;
		std::atomic_uint32_t laserOptions = LaserParticleOptions::Defaults << 1;
		std::atomic_uint32_t multiTouchTools = kMultiTouchToolsAll;
		uint32_t appliedMultiTouchTools = UINT32_MAX;
		std::atomic_uint32_t canvasPanInertiaOptions = CanvasPanInertiaOptions{}.Encode();
		uint32_t appliedCanvasPanInertiaOptions = UINT32_MAX;
		uint32_t appliedGeneralDrawingOptions = UINT32_MAX;
		uint32_t appliedLaserOptions = UINT32_MAX;
		mutable std::mutex palmEraserOptionsMutex;
		PalmEraser::Settings palmEraserOptions;
		std::optional<PalmEraser::Settings> appliedPalmEraserOptions;
		// 位0=画布调试开关，位1=可视化模式；绘制 owner 在 PumpBridgeState 应用。
		std::atomic_uint32_t canvasDebugOptions = 0;
		uint32_t appliedCanvasDebugOptions = UINT32_MAX;
		uint32_t appliedCanvasDebugInputMode = UINT32_MAX;
		std::atomic_bool drawingHapticsEnabled = true;
		std::jthread drawingThread;
		std::atomic_bool running = false;
		std::atomic_bool firstFrameReady = false;
		std::atomic<HWND> attachedWindow = nullptr;
		std::atomic<HWND> attachedPresentationWindow = nullptr;
		std::atomic<HostPresentationMode> presentationMode = HostPresentationMode::Automatic;
		std::atomic<std::uint64_t> presentCount = 0;
		std::atomic<std::uint64_t> successfulPresentCount = 0;
		std::atomic<std::uint64_t> partialPresentCount = 0;
		std::atomic<std::uint64_t> resizeCount = 0;
		std::atomic<std::uint64_t> clearCommandCount = 0;
		std::atomic<std::uint64_t> undoCommandCount = 0;
		std::atomic<std::uint64_t> redoCommandCount = 0;
		std::atomic<std::uint64_t> nextPageCommandCount = 0;
		std::atomic<std::uint64_t> previousPageCommandCount = 0;
		std::atomic<std::uint64_t> ulwDirtyRectPresentCount = 0;
		std::atomic<std::uint64_t> ulwPremultipliedAlphaFailureCount = 0;
		std::atomic_bool ulwTransparentFullFrameVerified = false;
		std::atomic_bool lastPresentSucceeded = false;
		bool presentDiagnosticInitialized = false;
		HostOutputTarget lastDiagnosticOutputTarget = HostOutputTarget::PrimaryDrawpad;
		std::uint64_t lastDiagnosticOutputRevision = 0;
		bool lastDiagnosticPresentSucceeded = false;
		std::atomic<int> committedWidth = 0;
		std::atomic<int> committedHeight = 0;
		std::atomic<std::size_t> currentPageIndex = 0;
		std::atomic<std::size_t> pageCount = 0;
		std::atomic_bool currentPageHasContent = false;
		std::atomic_bool laserVisible = false;
		std::atomic_bool laserHoldsBar = false;
		mutable std::mutex whiteboardMutex;
		Bridge::WhiteboardStyleToken whiteboardStyle;
		bool whiteboardStylePending = false;
		WhiteboardTheme whiteboardTheme;
		std::uint64_t whiteboardGeneration = 0;
		std::atomic_bool whiteboardLoadPending = false;
		bool whiteboardUnsaved = false;
		std::size_t whiteboardPageIndex = 0, whiteboardPageCount = 1;
		bool whiteboardCanUndo = false, whiteboardCanRedo = false;
		bool whiteboardHasContent = false;
		std::uint64_t whiteboardContentRevision = 0;
		std::atomic_bool canUndo = false;
		std::atomic_bool canRedo = false;
		std::atomic<std::uint64_t> contentRevision = 0;
		std::atomic<Bridge::CompletedStrokeKind> completedStrokeKind =
			Bridge::CompletedStrokeKind::None;
		std::atomic_bool selectionMode = true;
		std::atomic<Bridge::Workspace> workspace = Bridge::Workspace::Desktop;
		mutable std::mutex presentationTargetMutex;
		std::optional<Bridge::PresentationReadyIdentity> readyPresentationTarget;
		std::optional<Bridge::PresentationReadyIdentity> uiReadyPresentationTarget;
		std::optional<Bridge::PresentationReadyIdentity> suspendedPresentationTarget;
		bool commandScenePending = false;
		std::uint64_t workspaceTransitionEpoch = 0;
		bool workspaceTransitionPending = false;
		std::uint64_t appliedProductRevision = 0;
		Bridge::Tool appliedTool = Bridge::Tool::Pen;
		std::uint32_t inputGateReasons = 0;
		CanvasRecoveryState recovery;
		CanvasRecoveryState reportedRecovery;
		bool recoveryReportAvailable = false;
		std::uint64_t recoveryRevision = 0;
		bool shortcutSelectionHandoffPending = false;
		bool restoreLatestScene = false;
		std::atomic<HostOutputTarget> requestedOutputTarget =
			HostOutputTarget::PrimaryDrawpad;
		std::atomic<std::uint64_t> requestedOutputRevision = 0;
		std::atomic<HostOutputTarget> readyOutputTarget =
			HostOutputTarget::PrimaryDrawpad;
		std::atomic<std::uint64_t> readyOutputRevision = 0;
		std::atomic<std::uint64_t> presentedContentRevision = 0;
		std::atomic_bool auxiliaryFullFrameClean = false;
		std::atomic_uint64_t auxiliaryReadbackPayloadBytes = 0;
		struct AuxiliaryReleaseRequest
		{
			std::uint64_t hostGeneration = 0, productRevision = 0;
			std::uint64_t outputRevision = 0, contentRevision = 0, successfulPresents = 0;
			HostOutputTarget target = HostOutputTarget::PrimaryDrawpad;
			int width = 0, height = 0;
			bool operator==(const AuxiliaryReleaseRequest&) const = default;
		};
		std::mutex surfaceRequestMutex;
		std::optional<AuxiliaryReleaseRequest> pendingAuxiliaryRelease;
		std::shared_ptr<ShownFrameRequest> pendingShownFrame; // 由 surfaceRequestMutex 交给绘制线程。
		std::shared_ptr<ShownFrameRequest> renderingShownFrame; // 仅绘制 owner 访问。
		Detail::HostRuntimeRevisionSignal runtimeRevision;
		Detail::HostDrawingActivityState drawingActivity;
		Detail::HostAcceptedStrokeState acceptedStrokes;
		Shortcuts::State shortcuts;
		std::atomic<LONG> lastDirtyLeft = 0;
		std::atomic<LONG> lastDirtyTop = 0;
		std::atomic<LONG> lastDirtyRight = 0;
		std::atomic<LONG> lastDirtyBottom = 0;
		bool hiddenTestContactInjectionEnabled = false;
		std::uint64_t appliedBridgeRevision = (std::numeric_limits<std::uint64_t>::max)();
		bool requestedProductPage = false;
		std::uint32_t requestedProductPageIndex = 0;
		// 绘制 owner 记录最后实际入队的场景，不能用尚未前进的 presented workspace 代替。
		Bridge::Workspace requestedWorkspace = Bridge::Workspace::Desktop;
		std::uint64_t requestedPresentationTargetRevision = 0;
		HostStyleCallbacks styleCallbacks = {};
		HostRuntimeCallbacks runtimeCallbacks = {};
		HostStartOptions startOptions = {};
		mutable std::mutex hiddenPersistenceMutex;
		HiddenPresentationAccounting hiddenPersistence;
		std::mutex startupMutex;
		std::condition_variable startupCondition;
		mutable std::mutex contentMutex;
		mutable std::condition_variable contentCondition;
		std::mutex exitAutoSaveMutex;
		std::condition_variable exitAutoSaveCondition;
		bool exitAutoSavePrepared = false;
		std::atomic_bool whiteboardExitDurable = true;
		bool graphicsReady = false;
		struct alignas(8) InputRun
		{
			InputPreference preference = InputPreference::Automatic;
			bool largeScreen = false;
			InputPreference resolved = InputPreference::Automatic;
			HostInputMode actual = HostInputMode::Disabled;
			InputFallback fallback = InputFallback::None;
			TouchpadAvailability touchpad = TouchpadAvailability::BackendDisabled;
		};
		// 同一次启动的偏好、实际结果和回退原因一起发布给调试板与设置页。
		std::atomic<InputRun> inputRun{InputRun{}};
		std::atomic_bool inputStopping = false;
		bool inputDecision = false;
		bool inputSucceeded = false;
		bool startupCompleted = false;
		bool startupSucceeded = false;

		static HostPresentationMode ToHostMode(TransparentPresentMode mode) noexcept
		{
			switch (mode)
			{
			case TransparentPresentMode::UlwDirtyRect: return HostPresentationMode::UlwDirtyRect;
			case TransparentPresentMode::DirectCompositionVisualTree:
				return HostPresentationMode::DirectCompositionVisualTree;
			case TransparentPresentMode::DwmBlurBehind: return HostPresentationMode::DwmBlurBehind;
			default: return HostPresentationMode::Automatic;
			}
		}

		static bool IsRequiredMode(HostPresentationMode mode) noexcept
		{
			return mode != HostPresentationMode::Automatic;
		}

		static HostOutputTarget ToHostOutputTarget(
			TransparentOutputTarget target) noexcept
		{
			return target == TransparentOutputTarget::SelectionUlw
				? HostOutputTarget::SelectionUlw
				: HostOutputTarget::PrimaryDrawpad;
		}

		void BeginRuntimeMetricsRun(const HostStartOptions& options) noexcept
		{
			// 串行 Start 已确认上一线程不可 join；每 run 新建会话，不 Reset 或复用旧键。
			runtimeMetrics.session.reset();
			runtimeMetrics.enabled.store(options.enableRuntimeMetrics, std::memory_order_release);
			runtimeMetrics.unavailable.store(false, std::memory_order_release);
			runtimeMetrics.joined.store(false, std::memory_order_release);
			runtimeMetrics.sealed.store(false, std::memory_order_release);
			runtimeMetrics.startupFailed.store(options.enableRuntimeMetrics, std::memory_order_release);
			runtimeMetrics.runFailed.store(false, std::memory_order_release);
			runtimeMetrics.exportUnavailable.store(false, std::memory_order_release);
			runtimeMetrics.runSerial.store(0, std::memory_order_release);
			runtimeMetrics.contactSeen.store(0, std::memory_order_release);
			runtimeMetrics.confirmed.store(0, std::memory_order_release);
			runtimeMetrics.pending.store(0, std::memory_order_release);
			runtimeMetrics.invalid.store(0, std::memory_order_release);
			runtimeMetrics.contactDropped.store(0, std::memory_order_release);
			runtimeMetrics.pendingOverflow.store(0, std::memory_order_release);
			runtimeMetrics.frameSerial.store(0, std::memory_order_release);
			runtimeMetrics.presentAttempts.store(0, std::memory_order_release);
			runtimeMetrics.allocatedBytes.store(0, std::memory_order_release);
			runtimeMetrics.payloadByteUpperBound.store(0, std::memory_order_release);
			runtimeMetrics.effectiveSamples.store(0, std::memory_order_release);
			runtimeMetrics.inputBaseline = {};
			runtimeMetrics.finalInput = {};
			runtimeMetrics.finalSnapshot = {};
			runtimeMetrics.ownerCreated = false;
			{ std::scoped_lock lock(runtimeMetrics.metadataMutex); runtimeMetrics.metadata = {}; }
			if (!options.enableRuntimeMetrics) return;
			if (runtimeMetrics.nextRunSerial == (std::numeric_limits<std::uint64_t>::max)() ||
				options.runtimeMetricsMaximumSamples == 0 || options.runtimeMetricsMaximumSamples > 32768)
			{
				runtimeMetrics.unavailable.store(true, std::memory_order_release);
				return;
			}
			runtimeMetrics.runSerial.store(++runtimeMetrics.nextRunSerial, std::memory_order_release);
		}

		void PrepareRuntimeMetrics(const HostStartOptions& options) noexcept
		{
			if (!runtimeMetrics.enabled.load(std::memory_order_acquire) ||
				runtimeMetrics.unavailable.load(std::memory_order_acquire)) return;
			try
			{
				runtimeMetrics.session = std::make_unique<RuntimeMetricsSession>(options.runtimeMetricsMaximumSamples);
				// Controller Prepare 仍执行共同预算；此处另预留其静态上限和 Host 固定载荷。
				constexpr std::uint64_t auxiliaryBytes = 64u * 1024 + sizeof(RuntimeMetricsState) + sizeof(RuntimeMetricsSession);
				constexpr std::uint64_t byteBudget = 32u * 1024 * 1024;
				const auto snapshot = runtimeMetrics.session->Snapshot();
				LARGE_INTEGER frequency{};
				if (snapshot.allocatedBytes > byteBudget - auxiliaryBytes ||
					!QueryPerformanceFrequency(&frequency) || frequency.QuadPart <= 0)
				{
					runtimeMetrics.session.reset();
					runtimeMetrics.unavailable.store(true, std::memory_order_release);
					return;
				}
				runtimeMetrics.payloadByteUpperBound.store(snapshot.allocatedBytes + auxiliaryBytes, std::memory_order_release);
				{ std::scoped_lock lock(runtimeMetrics.metadataMutex); runtimeMetrics.metadata.qpcFrequency = frequency.QuadPart; }
				PublishRuntimeMetricsProgress();
			}
			catch (...)
			{
				runtimeMetrics.session.reset();
				runtimeMetrics.unavailable.store(true, std::memory_order_release);
			}
		}

		void PublishRuntimeMetricsProgress() noexcept
		{
			if (!runtimeMetrics.session) return;
			// 绘制 callback 读取 plain Session，再发布原子进度；caller 不跨线程读 Session。
			const auto value = runtimeMetrics.session->Snapshot();
			runtimeMetrics.contactSeen.store(value.contactSeen, std::memory_order_release);
			runtimeMetrics.confirmed.store(value.confirmed, std::memory_order_release);
			runtimeMetrics.pending.store(value.pending, std::memory_order_release);
			runtimeMetrics.invalid.store(value.invalid, std::memory_order_release);
			runtimeMetrics.contactDropped.store(value.contactDropped, std::memory_order_release);
			runtimeMetrics.pendingOverflow.store(value.pendingOverflow, std::memory_order_release);
			runtimeMetrics.frameSerial.store(value.frameSerial, std::memory_order_release);
			runtimeMetrics.presentAttempts.store(value.presentAttempts, std::memory_order_release);
			runtimeMetrics.allocatedBytes.store(value.allocatedBytes, std::memory_order_release);
			runtimeMetrics.effectiveSamples.store(value.effectiveSamples, std::memory_order_release);
		}

		void CaptureRuntimeMetricsMetadata(bool copyAdapter) noexcept
		{
			if (!runtimeMetrics.session) return;
			// 只有 GPU owner 调用；正常帧只抄数值，不查询 adapter 或增加时钟采样。
			std::scoped_lock lock(runtimeMetrics.metadataMutex);
			auto& value = runtimeMetrics.metadata;
			value.graphicsAvailable = graphics.device && graphics.context;
			value.driverType = value.graphicsAvailable ? static_cast<std::uint32_t>(graphics.driverType) : 0;
			value.featureLevel = value.graphicsAvailable ? static_cast<std::uint32_t>(graphics.featureLevel) : 0;
			value.presentationMode = ToHostMode(presentation.ActiveMode());
			value.outputTarget = ToHostOutputTarget(presentation.RequestedOutputTarget());
			value.outputRevision = presentation.RequestedOutputRevision();
			if (copyAdapter)
			{
				DXGI_ADAPTER_DESC description{};
				value.adapterAvailable = graphics.adapter && SUCCEEDED(graphics.adapter->GetDesc(&description));
				value.adapterDescription = {};
				if (value.adapterAvailable)
					std::copy_n(description.Description, value.adapterDescription.size(), value.adapterDescription.begin());
				value.adapterDescription.back() = L'\0';
			}
		}

		void SealRuntimeMetricsRun(bool startupFailed = false) noexcept
		{
			if (!runtimeMetrics.enabled.load(std::memory_order_acquire) ||
				runtimeMetrics.sealed.load(std::memory_order_acquire) || drawingThread.joinable() || drawing) return;
			if (startupFailed) runtimeMetrics.startupFailed.store(true, std::memory_order_release);
			runtimeMetrics.joined.store(runtimeMetrics.ownerCreated, std::memory_order_release);
			if (runtimeMetrics.session)
			{
				// producer 已停止、Controller 已销毁且线程真 join 后，尾 pending 才终结。
				runtimeMetrics.session->InvalidatePending();
				runtimeMetrics.finalSnapshot = runtimeMetrics.session->Snapshot();
				runtimeMetrics.finalInput = input.DiagnosticsSnapshot();
				const auto subtract = [](std::uint64_t value, std::uint64_t baseline) noexcept
				{ return value >= baseline ? value - baseline : 0; };
				auto& final = runtimeMetrics.finalInput;
				const auto& base = runtimeMetrics.inputBaseline;
				final.downPublished = subtract(final.downPublished, base.downPublished);
				final.downRejected = subtract(final.downRejected, base.downRejected);
				final.movePublished = subtract(final.movePublished, base.movePublished);
				final.moveContended = subtract(final.moveContended, base.moveContended);
				final.terminalPublished = subtract(final.terminalPublished, base.terminalPublished);
				final.recycled = subtract(final.recycled, base.recycled);
				final.controlWakes = subtract(final.controlWakes, base.controlWakes);
				final.controlWakeEnqueueFailures = subtract(final.controlWakeEnqueueFailures, base.controlWakeEnqueueFailures);
				final.controlWakeInlineRecoveries = subtract(final.controlWakeInlineRecoveries, base.controlWakeInlineRecoveries);
				final.activeWaits = subtract(final.activeWaits, base.activeWaits);
				PublishRuntimeMetricsProgress();
			}
			runtimeMetrics.sealed.store(true, std::memory_order_release);
		}

		bool WriteRuntimeMetrics(const wchar_t* path) const noexcept
		{
			if (!runtimeMetrics.enabled.load(std::memory_order_acquire) ||
				runtimeMetrics.unavailable.load(std::memory_order_acquire) ||
				!runtimeMetrics.joined.load(std::memory_order_acquire) ||
				!runtimeMetrics.sealed.load(std::memory_order_acquire) ||
				runtimeMetrics.startupFailed.load(std::memory_order_acquire) ||
				runtimeMetrics.runFailed.load(std::memory_order_acquire) || !runtimeMetrics.session) return false;
			// 私有根及 reparse 由 caller 验真；此薄接口至少拒绝相对/drive-relative 路径。
			if (!path || path[0] == L'\0') return false;
			const bool driveAbsolute = ((path[0] >= L'A' && path[0] <= L'Z') ||
				(path[0] >= L'a' && path[0] <= L'z')) && path[1] == L':' && (path[2] == L'\\' || path[2] == L'/');
			const bool uncAbsolute = path[0] == L'\\' && path[1] == L'\\' && path[2] != L'\0';
			if (!driveAbsolute && !uncAbsolute) return false;
			try
			{
				const bool written = runtimeMetrics.session->WriteJson(path, runtimeMetrics.finalInput);
				runtimeMetrics.exportUnavailable.store(!written, std::memory_order_release);
				return written;
			}
			catch (...)
			{
				// 排序/分群/格式化也会分配；noexcept 导出不能把诊断失败变为 terminate。
				runtimeMetrics.exportUnavailable.store(true, std::memory_order_release);
				return false;
			}
		}

		HostRuntimeMetricsSnapshot CopyRuntimeMetricsSnapshot() const noexcept
		{
			HostRuntimeMetricsSnapshot value;
			value.enabled = runtimeMetrics.enabled.load(std::memory_order_acquire);
			// defaultoff 也暴露实际分配计数，smoke 不能仅凭 enable=false 推断无 Session。
			value.allocatedBytes = runtimeMetrics.allocatedBytes.load(std::memory_order_acquire);
			if (!value.enabled) return value;
			value.unavailable = runtimeMetrics.unavailable.load(std::memory_order_acquire);
			value.joined = runtimeMetrics.joined.load(std::memory_order_acquire);
			value.sealed = runtimeMetrics.sealed.load(std::memory_order_acquire);
			value.startupFailed = runtimeMetrics.startupFailed.load(std::memory_order_acquire);
			value.runFailed = runtimeMetrics.runFailed.load(std::memory_order_acquire);
			value.exportUnavailable = runtimeMetrics.exportUnavailable.load(std::memory_order_acquire);
			value.runSerial = runtimeMetrics.runSerial.load(std::memory_order_acquire);
			value.contactSeen = runtimeMetrics.contactSeen.load(std::memory_order_acquire);
			value.confirmed = runtimeMetrics.confirmed.load(std::memory_order_acquire);
			value.pending = runtimeMetrics.pending.load(std::memory_order_acquire);
			value.invalid = runtimeMetrics.invalid.load(std::memory_order_acquire);
			value.contactDropped = runtimeMetrics.contactDropped.load(std::memory_order_acquire);
			value.pendingOverflow = runtimeMetrics.pendingOverflow.load(std::memory_order_acquire);
			value.frameSerial = runtimeMetrics.frameSerial.load(std::memory_order_acquire);
			value.presentAttempts = runtimeMetrics.presentAttempts.load(std::memory_order_acquire);
			value.payloadByteUpperBound = runtimeMetrics.payloadByteUpperBound.load(std::memory_order_acquire);
			value.effectiveSamples = runtimeMetrics.effectiveSamples.load(std::memory_order_acquire);
			{ std::scoped_lock lock(runtimeMetrics.metadataMutex); value.metadata = runtimeMetrics.metadata; }
			return value;
		}

		void PublishRuntimeRevision() noexcept
		{
			runtimeRevision.Publish();
		}

		static TransparentPresentMode ToTransparentMode(HostPresentationMode mode) noexcept
		{
			switch (mode)
			{
			case HostPresentationMode::UlwDirtyRect: return TransparentPresentMode::UlwDirtyRect;
			case HostPresentationMode::DwmBlurBehind: return TransparentPresentMode::DwmBlurBehind;
			case HostPresentationMode::DirectCompositionVisualTree:
			default: return TransparentPresentMode::DirectCompositionVisualTree;
			}
		}

		bool ShortcutBaselineBlockedLocked() const noexcept
		{
			const auto desired = bridge.Snapshot();
			const auto expected = desired.presentationTarget
				? std::optional(Bridge::ReadyIdentityFor(*desired.presentationTarget)) : std::nullopt;
			return whiteboardStylePending || workspaceTransitionPending || inputStopping.load(std::memory_order_acquire) ||
				input.ConsumerFatal() || CanvasRecoveryBlocksAdmission(recovery,
					recoveryReportAvailable ? &reportedRecovery : nullptr) ||
				shortcutSelectionHandoffPending || commandScenePending ||
				(desired.workspace == Bridge::Workspace::Whiteboard && whiteboardLoadPending.load(std::memory_order_acquire)) ||
				(desired.workspace == Bridge::Workspace::Presentation
				? !expected || readyPresentationTarget != expected ||
					suspendedPresentationTarget.has_value() ||
					(startOptions.requirePresentationUiReady && uiReadyPresentationTarget != expected)
				: workspace.load(std::memory_order_acquire) == Bridge::Workspace::Presentation);
		}

		Shortcuts::Context ShortcutContextLocked() noexcept
		{
			auto context = shortcuts.Snapshot();
			const auto desired = bridge.Snapshot();
			const auto target = desired.presentationTarget
				? std::optional(Bridge::ReadyIdentityFor(*desired.presentationTarget)) : std::nullopt;
			context.available = running.load(std::memory_order_acquire) &&
				context.scope.pageEpoch != 0 && !ShortcutBaselineBlockedLocked() &&
				context.scope.workspace == desired.workspace && context.scope.presentation == target &&
				(!desired.hasPage || context.scope.pageIndex == desired.page);
			// 即使渲染合并了 A→B→A，也要退休旧意图；内容修订不参与该身份。
			shortcuts.Publish(context, desired.shortcutIntentEpoch);
			return shortcuts.Snapshot();
		}

		void RetireRecoveryForDesiredLocked(const Bridge::ProductState& desired)
		{
			// Bridge 可在 owner 消费前合并 D→W→D；仅投影当前身份，不丢弃 owner 仍承担的故障报告。
			auto next = ProjectCanvasRecovery(recoveryReportAvailable ? &reportedRecovery : nullptr,
				desired, input.ConsumerFatal());
			next.revision = recovery.revision;
			if (next == recovery) return;
			next.revision = ++recoveryRevision;
			recovery = next;
			PublishRuntimeRevision();
		}

		static void ObserveRecovery(void* context, const CanvasRecoveryState& value) noexcept
		{
			auto* self = static_cast<Impl*>(context);
			if (!self) return;
			std::scoped_lock lock(self->presentationTargetMutex);
			self->reportedRecovery = value;
			self->reportedRecovery.revision = 0;
			self->recoveryReportAvailable = true;
			// 旧业务报告可以保留在 owner 侧；对外只暴露与当前 Bridge 精确匹配的状态。
			self->RefreshPresentationInputGateLocked();
		}

		static bool RecoveryRetryAllowed(void* context, std::uint64_t revision) noexcept
		{
			auto* self = static_cast<Impl*>(context);
			if (!self) return false;
			std::scoped_lock lock(self->presentationTargetMutex);
			return revision == self->recovery.revision && CanvasRecoveryRetryMatches(self->recovery,
				self->recovery, self->bridge.Snapshot(), self->running.load(std::memory_order_acquire),
				self->inputStopping.load(std::memory_order_acquire), self->input.ConsumerFatal());
		}

		// 调用方持有 presentationTargetMutex；发布、ready 和 UI ack 共用这一交接门。
		void RefreshPresentationInputGateLocked()
		{
			const auto desired = bridge.Snapshot();
			RetireRecoveryForDesiredLocked(desired);
			const auto expected = desired.presentationTarget
				? std::optional(Bridge::ReadyIdentityFor(*desired.presentationTarget))
				: std::nullopt;
			if (suspendedPresentationTarget != expected) suspendedPresentationTarget.reset();
			if (uiReadyPresentationTarget != expected) uiReadyPresentationTarget.reset();
			// 原有准入条件分别记录原因，持续关闸时也能观察阻塞从加载转为 UI 等待。
			std::uint32_t reasons = 0;
			if (inputStopping.load(std::memory_order_acquire)) reasons |= 1u;
			if (commandScenePending) reasons |= 2u;
			if (workspaceTransitionPending) reasons |= 1024u;
			if (desired.workspace == Bridge::Workspace::Whiteboard && whiteboardStylePending) reasons |= 2048u;
			if (input.ConsumerFatal()) reasons |= 256u;
			if (CanvasRecoveryBlocksAdmission(recovery,
				recoveryReportAvailable ? &reportedRecovery : nullptr)) reasons |= 512u;
			if (desired.workspace == Bridge::Workspace::Whiteboard &&
				whiteboardLoadPending.load(std::memory_order_acquire)) reasons |= 4u;
			if (desired.workspace == Bridge::Workspace::Presentation)
			{
				if (!expected) reasons |= 8u;
				if (readyPresentationTarget != expected) reasons |= 16u;
				if (suspendedPresentationTarget) reasons |= 32u;
				if (startOptions.requirePresentationUiReady && uiReadyPresentationTarget != expected)
					reasons |= 64u;
			}
			else if (workspace.load(std::memory_order_acquire) == Bridge::Workspace::Presentation)
				reasons |= 128u;
			const bool blocked = reasons != 0;
			const bool changed = input.BaseAdmissionBlocked() != blocked;
			input.SetAdmissionBlocked(blocked);
			if (reasons != inputGateReasons)
			{
				inputGateReasons = reasons;
				DiagnosticOutput::Printf(DiagnosticOutput::Stream::Output,
					"[Draw3.Input] action=admission reasons=%u desired=%u actual=%u session=%llu target=%llu\n",
					reasons, static_cast<unsigned>(desired.workspace),
					static_cast<unsigned>(workspace.load(std::memory_order_acquire)),
					static_cast<unsigned long long>(expected ? expected->sessionRevision : 0),
					static_cast<unsigned long long>(expected ? expected->targetRevision : 0));
			}
			if (changed)
			{
				if (expected) TracePptTiming(blocked ? "input_closed" : "input_open",
					expected->sessionRevision, expected->targetRevision);
				PublishRuntimeRevision();
			}
		}

		void ResetRuntimeDiagnostics()
		{
			{
				const std::scoped_lock lock(hiddenPersistenceMutex);
				hiddenPersistence = {};
			}
			presentationMode.store(HostPresentationMode::Automatic, std::memory_order_release);
			presentCount.store(0, std::memory_order_release);
			successfulPresentCount.store(0, std::memory_order_release);
			partialPresentCount.store(0, std::memory_order_release);
			resizeCount.store(0, std::memory_order_release);
			clearCommandCount.store(0, std::memory_order_release);
			undoCommandCount.store(0, std::memory_order_release);
			redoCommandCount.store(0, std::memory_order_release);
			nextPageCommandCount.store(0, std::memory_order_release);
			previousPageCommandCount.store(0, std::memory_order_release);
			ulwDirtyRectPresentCount.store(0, std::memory_order_release);
			ulwPremultipliedAlphaFailureCount.store(0, std::memory_order_release);
			ulwTransparentFullFrameVerified.store(false, std::memory_order_release);
			lastPresentSucceeded.store(false, std::memory_order_release);
			presentDiagnosticInitialized = false;
			lastDiagnosticOutputTarget = HostOutputTarget::PrimaryDrawpad;
			lastDiagnosticOutputRevision = 0;
			lastDiagnosticPresentSucceeded = false;
			currentPageIndex.store(0, std::memory_order_release);
			pageCount.store(0, std::memory_order_release);
			currentPageHasContent.store(false, std::memory_order_release);
			laserVisible.store(false, std::memory_order_release);
			laserHoldsBar.store(false, std::memory_order_release);
			// 新 Host 生命周期不能沿用上次运行的撤回/重做可用性。
			canUndo.store(false, std::memory_order_release);
			canRedo.store(false, std::memory_order_release);
			contentRevision.store(0, std::memory_order_release);
			completedStrokeKind.store(
				Bridge::CompletedStrokeKind::None, std::memory_order_release);
			acceptedStrokes.BeginHost();
			shortcuts.BeginHost();
			selectionMode.store(true, std::memory_order_release);
			workspace.store(Bridge::Workspace::Desktop, std::memory_order_release);
			whiteboardLoadPending.store(false, std::memory_order_release);
			whiteboardStyle = {}; whiteboardStylePending = false;
			{
				std::scoped_lock lock(presentationTargetMutex);
				readyPresentationTarget.reset();
				uiReadyPresentationTarget.reset();
				suspendedPresentationTarget.reset();
				input.SetAdmissionBlocked(false);
				input.SetShortcutAdmissionBlocked(false);
				shortcutSelectionHandoffPending = false;
				commandScenePending = false;
				workspaceTransitionEpoch = 0;
				workspaceTransitionPending = false;
				appliedProductRevision = 0;
				appliedTool = Bridge::Tool::Pen;
				inputGateReasons = 0;
				recovery = {};
				reportedRecovery = {};
				recoveryReportAvailable = false;
				recovery.revision = ++recoveryRevision;
				restoreLatestScene = false;
			}
			requestedOutputTarget.store(
				HostOutputTarget::PrimaryDrawpad, std::memory_order_release);
			requestedOutputRevision.store(0, std::memory_order_release);
			readyOutputTarget.store(
				HostOutputTarget::PrimaryDrawpad, std::memory_order_release);
			readyOutputRevision.store(0, std::memory_order_release);
			presentedContentRevision.store(0, std::memory_order_release);
			auxiliaryFullFrameClean.store(false, std::memory_order_release);
			auxiliaryReadbackPayloadBytes.store(0, std::memory_order_release);
			{
				std::scoped_lock lock(surfaceRequestMutex);
				pendingAuxiliaryRelease.reset();
				pendingShownFrame.reset();
			}
			renderingShownFrame.reset();
			runtimeRevision.Reset();
			drawingActivity.Reset();
			lastDirtyLeft.store(0, std::memory_order_release);
			lastDirtyTop.store(0, std::memory_order_release);
			lastDirtyRight.store(0, std::memory_order_release);
			lastDirtyBottom.store(0, std::memory_order_release);
			appliedBridgeRevision = (std::numeric_limits<std::uint64_t>::max)();
			requestedProductPage = false;
			requestedProductPageIndex = 0;
			requestedWorkspace = Bridge::Workspace::Desktop;
			requestedPresentationTargetRevision = 0;
			{
				std::scoped_lock lock(exitAutoSaveMutex);
				exitAutoSavePrepared = false;
			}
		}

		static void ObservePresented(void* context, bool succeeded, RECT dirty,
			bool presentFull, TransparentPresentObservation observation)
		{
			auto* self = static_cast<Impl*>(context);
			if (!self) return;
			// 运行期设备恢复可能切换透明后端，快照和窗口语义随真实 presenter 更新。
			self->presentationMode.store(ToHostMode(self->presentation.ActiveMode()),
				std::memory_order_release);
			self->window.SetGpuTransparentComposition(
				self->presentation.IsGpuTransparentComposition());
			self->presentCount.fetch_add(1, std::memory_order_acq_rel);
			if (succeeded) self->successfulPresentCount.fetch_add(1, std::memory_order_acq_rel);
			if (!presentFull) self->partialPresentCount.fetch_add(1, std::memory_order_acq_rel);
			self->lastPresentSucceeded.store(succeeded, std::memory_order_release);
			self->lastDirtyLeft.store(dirty.left, std::memory_order_relaxed);
			self->lastDirtyTop.store(dirty.top, std::memory_order_relaxed);
			self->lastDirtyRight.store(dirty.right, std::memory_order_relaxed);
			self->lastDirtyBottom.store(dirty.bottom, std::memory_order_relaxed);
			bool runtimeChanged = self->auxiliaryReadbackPayloadBytes.exchange(
				self->presentation.AuxiliaryReadbackPayloadBytes(), std::memory_order_acq_rel) !=
				self->presentation.AuxiliaryReadbackPayloadBytes();
			const HostOutputTarget outputTarget =
				ToHostOutputTarget(observation.outputTarget);
			const bool diagnosticTargetChanged = !self->presentDiagnosticInitialized ||
				self->lastDiagnosticOutputTarget != outputTarget ||
				self->lastDiagnosticOutputRevision != observation.outputRevision;
			const bool diagnosticRecovery = self->presentDiagnosticInitialized &&
				self->lastDiagnosticPresentSucceeded != succeeded;
			if (diagnosticTargetChanged || diagnosticRecovery)
			{
				DiagnosticOutput::Line() << "[Draw3Diag][present] event="
					<< (succeeded ? (diagnosticRecovery ? "recovery" : "success") : "failure")
					<< " mode=" << TransparentPresentModeName(self->presentation.ActiveMode())
					<< " target=" << HostOutputTargetName(outputTarget)
					<< " revision=" << observation.outputRevision
					<< " contentRevision=" << observation.presentedContentRevision
					<< " dirty=(" << dirty.left << "," << dirty.top << ","
					<< dirty.right << "," << dirty.bottom << ")"
					<< " dirtyRectCount=" << observation.dirtyRectCount
					<< " full=" << (presentFull ? 1 : 0)
					<< " hwnd=0x" << std::hex
					<< reinterpret_cast<UINT_PTR>(outputTarget == HostOutputTarget::SelectionUlw
						? self->attachedPresentationWindow.load(std::memory_order_acquire)
						: self->attachedWindow.load(std::memory_order_acquire))
					<< " result=0x" << static_cast<unsigned long>(
						succeeded ? S_OK : self->presentation.LastFailure())
					<< std::dec << std::endl;
				if (!succeeded || diagnosticTargetChanged || diagnosticRecovery)
					LogDraw3WindowState(succeeded ? "present-success" : "present-failure",
						self->attachedWindow.load(std::memory_order_acquire),
						self->attachedPresentationWindow.load(std::memory_order_acquire));
			}
			self->presentDiagnosticInitialized = true;
			self->lastDiagnosticOutputTarget = outputTarget;
			self->lastDiagnosticOutputRevision = observation.outputRevision;
			self->lastDiagnosticPresentSucceeded = succeeded;
			runtimeChanged = self->requestedOutputTarget.exchange(
				outputTarget, std::memory_order_acq_rel) != outputTarget || runtimeChanged;
			runtimeChanged = self->requestedOutputRevision.exchange(
				observation.outputRevision, std::memory_order_acq_rel) !=
				observation.outputRevision || runtimeChanged;
			if (succeeded)
			{
				runtimeChanged = self->presentedContentRevision.exchange(
					observation.presentedContentRevision, std::memory_order_acq_rel) !=
					observation.presentedContentRevision || runtimeChanged;
				if (presentFull)
				{
					runtimeChanged = self->readyOutputTarget.exchange(
						outputTarget, std::memory_order_acq_rel) != outputTarget || runtimeChanged;
					runtimeChanged = self->readyOutputRevision.exchange(
						observation.outputRevision, std::memory_order_acq_rel) !=
						observation.outputRevision || runtimeChanged;
				}
				if (outputTarget == HostOutputTarget::SelectionUlw)
				{
					bool clean = self->auxiliaryFullFrameClean.load(
						std::memory_order_acquire);
					if (observation.fullFrameAllZeroAlpha) clean = true;
					else if (presentFull || !observation.updatedRegionAllZeroAlpha) clean = false;
					runtimeChanged = self->auxiliaryFullFrameClean.exchange(
						clean, std::memory_order_acq_rel) != clean || runtimeChanged;
				}
			}
			if (observation.ulw)
			{
				if (observation.usedDirtyRect)
					self->ulwDirtyRectPresentCount.fetch_add(1, std::memory_order_acq_rel);
				if (!observation.premultipliedAlphaValid)
					self->ulwPremultipliedAlphaFailureCount.fetch_add(1, std::memory_order_acq_rel);
				if (observation.fullFrameAllZeroAlpha)
					self->ulwTransparentFullFrameVerified.store(true, std::memory_order_release);
			}
			if (self->renderingShownFrame && presentFull)
			{
				auto request = std::move(self->renderingShownFrame);
				self->FinishShownFrame(*request, succeeded);
			}
			if (runtimeChanged) self->PublishRuntimeRevision();
			if (self->runtimeMetrics.session)
			{
				self->CaptureRuntimeMetricsMetadata(false);
				self->PublishRuntimeMetricsProgress();
			}
			// 首帧尚未置 firstFrameReady；只有已 Stored 内容的真实成功版本可停在此门。
			const auto& options = self->startOptions;
			if (options.enableHiddenTestContactInjection && options.successfulPresentReachedEvent &&
				options.continueSuccessfulPresentEvent && succeeded &&
				self->firstFrameReady.load(std::memory_order_acquire) &&
				self->currentPageHasContent.load(std::memory_order_acquire) &&
				observation.presentedContentRevision == self->contentRevision.load(std::memory_order_acquire))
			{
				if (!SetEvent(options.successfulPresentReachedEvent) ||
					WaitForSingleObject(options.continueSuccessfulPresentEvent, INFINITE) != WAIT_OBJECT_0)
					throw std::runtime_error("hidden successful-present gate failed");
			}
		}

		static void ObserveResized(void* context, int width, int height)
		{
			auto* self = static_cast<Impl*>(context);
			if (!self) return;
			self->committedWidth.store(width, std::memory_order_release);
			self->committedHeight.store(height, std::memory_order_release);
			self->auxiliaryFullFrameClean.store(false, std::memory_order_release);
			self->auxiliaryReadbackPayloadBytes.store(
				self->presentation.AuxiliaryReadbackPayloadBytes(), std::memory_order_release);
			self->resizeCount.fetch_add(1, std::memory_order_acq_rel);
		}

		static void ObserveCommand(void* context, CanvasCommandType type,
			std::size_t currentPage, std::size_t pages)
		{
			auto* self = static_cast<Impl*>(context);
			if (!self) return;
			const std::size_t previousPage = self->currentPageIndex.exchange(
				currentPage, std::memory_order_acq_rel);
			const std::size_t previousCount = self->pageCount.exchange(
				pages, std::memory_order_acq_rel);
			switch (type)
			{
			case CanvasCommandType::Clear:
				self->clearCommandCount.fetch_add(1, std::memory_order_acq_rel); break;
			case CanvasCommandType::Undo:
				self->undoCommandCount.fetch_add(1, std::memory_order_acq_rel); break;
			case CanvasCommandType::Redo:
				self->redoCommandCount.fetch_add(1, std::memory_order_acq_rel); break;
			case CanvasCommandType::NextPage:
				self->nextPageCommandCount.fetch_add(1, std::memory_order_acq_rel); break;
			case CanvasCommandType::PreviousPage:
				self->previousPageCommandCount.fetch_add(1, std::memory_order_acq_rel); break;
			case CanvasCommandType::PrepareExitAutoSave:
				self->desktopPersistenceInstallationClosed = true;
				{
					std::scoped_lock lock(self->exitAutoSaveMutex);
					self->exitAutoSavePrepared = true;
				}
				self->exitAutoSaveCondition.notify_all();
				break;
			default: break;
			}
			(void)self->runtimeRevision.PublishPageChange(
				previousPage, previousCount, currentPage, pages);
		}

		static void ObserveDocument(void* context, std::size_t currentPage, std::size_t pages)
		{
			auto* self = static_cast<Impl*>(context);
			if (!self) return;
			const std::size_t previousPage = self->currentPageIndex.exchange(
				currentPage, std::memory_order_acq_rel);
			const std::size_t previousCount = self->pageCount.exchange(
				pages, std::memory_order_acq_rel);
			(void)self->runtimeRevision.PublishPageChange(
				previousPage, previousCount, currentPage, pages);
		}

		static void ObserveCurrentPageContent(
			void* context, bool hasContent, std::uint64_t revision)
		{
			auto* self = static_cast<Impl*>(context);
			if (!self) return;
			{
				std::scoped_lock lock(self->contentMutex);
				// 不同页面可同为有墨迹或空白；内容版本也是发布载荷的一部分。
				if (self->currentPageHasContent.load(
					std::memory_order_relaxed) == hasContent &&
					self->contentRevision.load(std::memory_order_relaxed) == revision) return;
				self->currentPageHasContent.store(hasContent, std::memory_order_release);
				self->contentRevision.store(revision, std::memory_order_release);
			}
			self->contentCondition.notify_all();
			self->PublishRuntimeRevision();
		}

		static void ObserveLaserVisibility(void* context, bool visible, bool holdsBar)
		{
			auto* self = static_cast<Impl*>(context);
			// 激光随时间改变，只推进运行时版本，文档内容版本仍由墨迹提交维护。
			if (!self) return;
			const bool visibleChanged = self->laserVisible.exchange(visible, std::memory_order_acq_rel) != visible;
			const bool holdChanged = self->laserHoldsBar.exchange(holdsBar, std::memory_order_acq_rel) != holdsBar;
			if (visibleChanged || holdChanged) self->PublishRuntimeRevision();
		}

		static void ObserveWhiteboardThemeEdited(void* context, WhiteboardTheme theme) noexcept
		{
			auto* self = static_cast<Impl*>(context);
			if (self->runtimeCallbacks.whiteboardThemeEdited)
				self->runtimeCallbacks.whiteboardThemeEdited(self->runtimeCallbacks.context, theme);
		}

		// 直接转发给产品层（不经 Host 快照/条件变量/state 线程），保持视口通道在绘制线程到 UI3 之间只跳一次。
		static void ObserveWhiteboardViewport(void* context, const WhiteboardViewportSample& sample) noexcept
		{
			auto* self = static_cast<Impl*>(context);
			if (self && self->runtimeCallbacks.whiteboardViewportChanged)
				self->runtimeCallbacks.whiteboardViewportChanged(self->runtimeCallbacks.context, sample);
		}

		static void ObserveWhiteboardExit(void* context, bool durable)
		{
			static_cast<Impl*>(context)->whiteboardExitDurable.store(durable, std::memory_order_release);
		}

		static void ObserveWhiteboard(void* context, WhiteboardTheme theme,
			std::uint64_t generation, bool pending, bool unsaved, std::size_t page, std::size_t count,
			bool hasContent, std::uint64_t contentVersion)
		{
			auto* self = static_cast<Impl*>(context);
			{
				std::scoped_lock lock(self->presentationTargetMutex, self->whiteboardMutex);
				Bridge::WhiteboardStyleToken token{ self->acceptedStrokes.HostGeneration(), generation, page, theme.backgroundRgb, self->whiteboardStyle.serial };
				if (!pending && token != self->whiteboardStyle)
				{
					// 在回执对外可见前关住新Down；同页主题修改也必须有独立身份。
					++token.serial;
					self->whiteboardStyle = token;
					self->whiteboardStylePending = true;
				}
				self->whiteboardTheme = theme;
				self->whiteboardGeneration = generation;
				self->whiteboardLoadPending.store(pending, std::memory_order_release);
				self->whiteboardUnsaved = unsaved;
				self->whiteboardPageIndex = page;
				self->whiteboardPageCount = count;
				self->whiteboardHasContent = hasContent;
				self->whiteboardContentRevision = contentVersion;
				self->whiteboardCanUndo = self->canUndo.load(std::memory_order_acquire);
				self->whiteboardCanRedo = self->canRedo.load(std::memory_order_acquire);
				self->RefreshPresentationInputGateLocked();
			}
			self->PublishRuntimeRevision();
		}

		static void ObserveHistoryAvailability(void* context, bool canUndo, bool canRedo)
		{
			auto* self = static_cast<Impl*>(context);
			if (!self) return;
			const bool undoChanged = self->canUndo.exchange(canUndo, std::memory_order_acq_rel) != canUndo;
			const bool redoChanged = self->canRedo.exchange(canRedo, std::memory_order_acq_rel) != canRedo;
			if (undoChanged || redoChanged) self->PublishRuntimeRevision();
		}

		static void ObserveStrokeCompleted(
			void* context, Bridge::CompletedStrokeKind kind)
		{
			auto* self = static_cast<Impl*>(context);
			if (!self || kind == Bridge::CompletedStrokeKind::None) return;
			self->completedStrokeKind.store(kind, std::memory_order_release);
		}

		static void ObserveWorkspace(void* context, Bridge::Workspace value,
			std::size_t currentPage, std::size_t pages,
			const Bridge::PresentationReadyIdentity* presentationTarget)
		{
			auto* self = static_cast<Impl*>(context);
			if (!self) return;
			if (presentationTarget) TracePptTiming("canvas_presented",
				presentationTarget->sessionRevision, presentationTarget->targetRevision);
			{
				std::scoped_lock lock(self->presentationTargetMutex, self->whiteboardMutex);
				self->workspace.store(value, std::memory_order_release);
				self->currentPageIndex.store(currentPage, std::memory_order_release);
				self->pageCount.store(pages, std::memory_order_release);
				self->readyPresentationTarget = presentationTarget
					? std::optional<Bridge::PresentationReadyIdentity>(*presentationTarget)
					: std::nullopt;
				self->commandScenePending = false;
				if (value != Bridge::Workspace::Whiteboard)
					self->whiteboardLoadPending.store(false, std::memory_order_release);
				self->RefreshPresentationInputGateLocked();
			}
			self->requestedProductPage = false;
			self->PublishRuntimeRevision();
			const Bridge::ProductState desiredState = self->bridge.Snapshot();
			const Bridge::Workspace desired = desiredState.workspace;
			if (desired != value)
			{
				// 活动 contact 延迟期间目标可能再次变化；确认旧请求后立即补发最新目标。
				CanvasCommand workspaceCommand;
				workspaceCommand.type = CanvasCommandType::SetWorkspace;
				workspaceCommand.workspace = static_cast<std::uint8_t>(desired);
				workspaceCommand.sceneClosedAdmission = true;
				self->window.EnqueueCanvasCommand(workspaceCommand);
				self->requestedWorkspace = desired;
				self->requestedPresentationTargetRevision = 0;
			}
			else if (desired == Bridge::Workspace::Presentation &&
				desiredState.presentationTarget && (!presentationTarget ||
					desiredState.presentationTarget->targetRevision !=
						presentationTarget->targetRevision))
			{
				CanvasCommand targetCommand;
				targetCommand.type = CanvasCommandType::SetPresentationTarget;
				targetCommand.presentationTarget = desiredState.presentationTarget;
				targetCommand.sceneClosedAdmission = true;
				self->window.EnqueueCanvasCommand(std::move(targetCommand));
				self->requestedWorkspace = desired;
				self->requestedPresentationTargetRevision = desiredState.presentationTarget->targetRevision;
			}
		}

		static const char* DesktopAdmissionReason(DesktopInkAdmission status) noexcept
		{
			switch (status)
			{
			case DesktopInkAdmission::Accepted: return "accepted";
			case DesktopInkAdmission::Closed: return "closed";
			case DesktopInkAdmission::InvalidRequest: return "invalid_request";
			case DesktopInkAdmission::ReservationFailed: return "reservation_failed";
			case DesktopInkAdmission::MissingSave: return "missing_save";
			}
			return "unknown";
		}

		static bool ObserveDesktopAutoSave(void* context,
			DesktopSnapshotTrigger trigger, DesktopInkSaveRequest request) noexcept
		{
			auto* self = static_cast<Impl*>(context);
			if (!self) return false;
			if (trigger == DesktopSnapshotTrigger::Clear && self->recoverySession.Active())
			{
				try
				{
					const std::scoped_lock lock(self->desktopClearRequestMutex);
					self->desktopClearRequestId = request.identity.requestId.Bytes();
				}
				catch (...) {}
			}
			const auto status = self->desktopSnapshots.Save(std::move(request));
			if (status == DesktopInkAdmission::Accepted) return true;
			DiagnosticOutput::Printf(DiagnosticOutput::ErrError,
				"[Draw3.DesktopInkSnapshots] action=submit trigger=%s result=failed reason=%s\n",
				trigger == DesktopSnapshotTrigger::Exit ? "exit" : "clear", DesktopAdmissionReason(status));
			return false;
		}

		static bool ObserveDesktopLoad(void* context, DesktopInkLoadRequest request) noexcept
		{
			auto* self = static_cast<Impl*>(context);
			if (!self) return false;
			const auto status = self->desktopSnapshots.Load(std::move(request));
			if (status == DesktopInkAdmission::Accepted) return true;
			DiagnosticOutput::Printf(DiagnosticOutput::ErrError,
				"[Draw3.DesktopInkSnapshots] action=load_submit result=failed reason=%s\n", DesktopAdmissionReason(status));
			return false;
		}

		void StartDesktopSnapshots(float dpiScale) noexcept
		{
			if (startOptions.autoSaveRoot.empty()) return;
			DesktopInkSnapshotsTestHook hook;
			if (startOptions.enableHiddenTestContactInjection)
			{
				const auto& fixture = startOptions.desktopInkSnapshotsTestHook;
				hook.context = this;
				if (fixture.beforeSave)
					hook.beforeWork = [](void* context, DesktopInkOperation operation, const DesktopInkIdentity&)
					{
						if (operation != DesktopInkOperation::Save) return;
						const auto& hook = static_cast<Impl*>(context)->startOptions.desktopInkSnapshotsTestHook;
						hook.beforeSave(hook.context);
					};
				if (fixture.afterSave)
					hook.afterSave = [](void* context, const DesktopInkCompletion& completion) noexcept
					{
						if (!completion.publishedPath) return;
						const auto& hook = static_cast<Impl*>(context)->startOptions.desktopInkSnapshotsTestHook;
						hook.afterSave(hook.context, *completion.publishedPath);
					};
			}
			try
			{
				if (desktopSnapshots.Start({ startOptions.autoSaveRoot, dpiScale,
					&WakeForPresentationPersistence, this }, hook)) return;
			}
			catch (...) {}
			DiagnosticOutput::Text(DiagnosticOutput::ErrError,
				"[Draw3.DesktopInkSnapshots] action=start result=failed reason=service_start\n");
		}

		static PagedInkStore* ObservePresentationStore(void* context,
			const Bridge::PresentationTarget& target) noexcept
		{
			auto* self = static_cast<Impl*>(context);
			if (!self || self->startOptions.autoSaveRoot.empty()) return nullptr;
			const char* failureReason = "store_unavailable";
			try
			{
				PagedInkStoreTestHook hook;
				if (self->startOptions.enableHiddenTestContactInjection)
				{
					hook.context = self;
					hook.recordAccepted = [](void* owner) noexcept
					{
						auto& host = *static_cast<Impl*>(owner);
						const std::scoped_lock lock(host.hiddenPersistenceMutex);
						return host.hiddenPersistence.Accept();
					};
				}
				if (self->startOptions.enableHiddenTestContactInjection &&
					self->startOptions.beforePresentationStoreWriteForTest)
				{
					hook.beforeWork = [](void* owner, const PagedInkRequestIdentity& identity)
					{
						auto& options = static_cast<Impl*>(owner)->startOptions;
						if (identity.kind == PagedInkRequestKind::Full || identity.kind == PagedInkRequestKind::Clear)
							options.beforePresentationStoreWriteForTest(options.presentationStoreTestContext);
					};
				}
				if (auto* store = self->presentationStores.Get(target, self->startOptions.autoSaveRoot,
					self->presentationStoreDpiScale, &WakeForPresentationPersistence, self, hook,
					&RecoveryDecoratePresentationStore, self))
					return store;
			}
			catch (...) { failureReason = "exception"; }
			DiagnosticOutput::Printf(DiagnosticOutput::ErrError,
				"[Draw3.Presentation] action=start result=failed reason=%s\n", failureReason);
			return nullptr;
		}

		static void ObservePresentationDrain(void* context, void* receiver,
			void (*settle)(void*, PresentationPagedCompletion&&))
		{
			auto& self = *static_cast<Impl*>(context);
			self.presentationStores.Drain();
			// 退出/致命故障已封闭业务，先结算已离开 worker 的终态，再取仍在服务里的终态。
			if (auto reserved = self.window.TakePresentationCompletionForExit())
				settle(receiver, std::move(*reserved));
			if (auto pending = std::exchange(self.pendingPresentationCompletion, std::nullopt))
				settle(receiver, std::move(*pending));
			while (auto completion = self.presentationStores.Take())
			{
				self.ObservePresentationTerminal(completion->completion);
				settle(receiver, std::move(*completion));
			}
		}

		static void WakeForPresentationPersistence(void* context) noexcept
		{
			auto* self = static_cast<Impl*>(context);
			if (self) (void)self->input.PublishControlWake();
		}


		static void ObservePenDiagnostics(void* context, const PenRuntimeDiagnostics& value)
		{
			auto* self = static_cast<Impl*>(context);
			// 复用诊断快照锁；只有隐藏测试安装此回调，产品绘制帧不增加锁开销。
			std::scoped_lock lock(self->eraserDiagnosticsMutex);
			self->penDiagnostics = value;
		}

		static void ObserveEraserDiagnostics(void* context,const SpeedEraser::Diagnostics& value)
		{
			auto* self=static_cast<Impl*>(context);
			uint64_t sequence=0;
			{
				std::scoped_lock lock(self->eraserDiagnosticsMutex);
				sequence=self->eraserDiagnostics.frameSequence+1;
				self->eraserDiagnostics=value;
				self->eraserDiagnostics.frameSequence=sequence;
			}
			if(!self->touchAreaTraceEnabled.load(std::memory_order_relaxed))
			{self->touchAreaTraceWasEnabled=false;return;}
			const auto now=std::chrono::steady_clock::now();
			const bool contact=value.eraserContact;
			const bool inputActivity=value.inputContact || value.preview;
			const auto traceRevision=self->touchAreaTraceRevision.load(std::memory_order_relaxed);
			const bool newlyEnabled=!self->touchAreaTraceWasEnabled || traceRevision!=self->observedTouchAreaTraceRevision;
			self->observedTouchAreaTraceRevision=traceRevision;
			const bool touch=value.inputType==static_cast<uint32_t>(InputDeviceType::Touch) &&
				value.inputSource.contextId!=0;
			if(newlyEnabled || (touch && value.inputSource!=self->lastTouchAreaMetadataSource))
				self->touchAreaMetadataPending=true;
			const auto mode = self->inputRun.load(std::memory_order_acquire).actual;
			if(mode != HostInputMode::Win32Mouse && mode != HostInputMode::Pointer &&
				self->touchAreaMetadataPending && (newlyEnabled ||
				now-self->lastTouchAreaMetadataTrace>=std::chrono::seconds(1)))
			{
				const auto source=touch?value.inputSource:SpeedEraser::InputSource{};
				self->touchAreaMetadataPending=!self->stylus.TraceTouchAreaDiagnostics(source);
				self->lastTouchAreaMetadataTrace=now;
				if(!self->touchAreaMetadataPending)self->lastTouchAreaMetadataSource=source;
			}
			const bool edge=newlyEnabled || contact!=self->lastTracedContact ||
				inputActivity!=self->lastTracedInputActivity;
			if(!edge && (!inputActivity || now-self->lastTouchAreaTrace<std::chrono::milliseconds(250)))return;
			const char* event=newlyEnabled?"enabled":contact!=self->lastTracedContact?
				(contact?"begin":"end"):"sample";
			const char* inputEvent=newlyEnabled?"enabled":inputActivity!=self->lastTracedInputActivity?
				(inputActivity?"begin":"end"):"sample";
			self->touchAreaTraceWasEnabled=true;self->lastTracedContact=contact;
			self->lastTracedInputActivity=inputActivity;self->lastTouchAreaTrace=now;
			const auto& d=value;const auto& a=d.contactArea;const auto& source=d.inputSource;
			const bool requested=self->window.TouchContactAreaAssistance();
			const char* gate=!contact?"no-eraser-contact":!d.active?"not-speed-eraser":
				!requested?"option-off":!a.enabled?"waiting-new-contact-batch":
				d.inputType!=static_cast<uint32_t>(InputDeviceType::Touch)?"not-touch-input":
				source.kind!=SpeedEraser::SourceKind::Touch?"touch-relationship-unknown":
				!d.inputMapped?"display-mapping-unknown":
				a.sample.units!=SpeedEraser::ContactAreaUnits::CanvasPixels?"area-units-unusable":
				!a.sampleValid?"area-rejected":!a.referenceReady?"waiting-stable-drag":
				!d.touchUnlocked?"waiting-startup-displacement":!a.referenceFresh?"reference-expired":
				!a.active?"waiting-accepted-movement":"area-floor-active";
			if(d.eraserContact || d.preview)
				DiagnosticOutput::Printf(kTouchAreaTarget,"[EraserEntry] seq=%llu entry=%s kind=%s penResponse=%s debugOverride=%d inherited=%d reason=%s hoverTime=%.6f downTime=%.6f frameTime=%.6f previousShownPx=%.3f downPx=%.3f firstRadiusPx=%.3f cursorPx=%.3f currentRadiusPx=%.3f\n",
					static_cast<unsigned long long>(sequence),SpeedEraser::InputEntryName(d.entry),
					d.eraserKind==SpeedEraser::EraserKind::Speed?"Speed":"Fixed",SpeedEraser::PenResponseName(d.formalPenResponse),
					d.developmentResponseOverride,d.sessionInherited,d.sessionReason,d.hoverSeconds,d.downSeconds,d.frameSeconds,d.previousShownDiameterPx,d.downDiameterPx,
					d.firstPointRadiusPx,d.cursorDiameterPx,d.nextRadiusPx);
			// 同一选中输入的限频快照；无可见光标或非橡皮尺寸明确写 -1，不代用其他设备。
			const bool eraserSizeValid=d.eraserContact || d.preview;
			const char* eraserKind=eraserSizeValid?
				(d.eraserKind==SpeedEraser::EraserKind::Speed?"Speed":"Fixed"):"N/A";
			char inputText[1024]{};
			std::snprintf(inputText,sizeof(inputText),
				"[EraserInput] seq=%llu event=%s frameSeconds=%.6f inputContact=%d eraserContact=%d preview=%d tool=%u/%u device=%s inputType=%u source=%s recognition=%u tcid=%u contactId=%u contactGen=%llu cursorId=%u inputCanvasPx=(%.3f,%.3f) inputPositionValid=%d cursorCanvasPx=(%.3f,%.3f) cursorVisible=%d eraserKind=%s B=%.3f targetDip=%.3f actualDip=%.3f cursorDiameterPx=%.3f geometryDiameterPx=%.3f speed=%.3f sweepSpeed=%.3f unit=%s qualified=%d evidenceMs=%.1f areaRaw=(%.3f,%.3f) areaDip=(%.3f,%.3f) areaActive=%d\n",
				static_cast<unsigned long long>(sequence),inputEvent,d.frameSeconds,d.inputContact,d.eraserContact,d.preview,
				d.selectedTool,d.effectiveTool,d.inputPositionValid?InputDeviceTypeName(d.inputType):"Unknown",d.inputType,
				SpeedEraser::SourceKindName(source.kind),static_cast<unsigned>(source.recognition),source.contextId,
				d.contactId,static_cast<unsigned long long>(d.contactGeneration),source.cursorId,
				d.inputPositionValid?d.inputCanvasXpx:-1.0f,d.inputPositionValid?d.inputCanvasYpx:-1.0f,d.inputPositionValid,
				d.cursorVisible?d.cursorCanvasXpx:-1.0f,d.cursorVisible?d.cursorCanvasYpx:-1.0f,d.cursorVisible,
				eraserKind,eraserSizeValid?d.sizes.standardDiameterDip:-1.0f,
				eraserSizeValid?d.targetDiameterDip:-1.0f,eraserSizeValid?d.effectiveDiameterDip:-1.0f,
				d.cursorVisible?d.cursorDiameterPx:-1.0f,d.eraserContact?d.nextRadiusPx*2:-1.0f,
				d.speed,d.sweepSpeed,SpeedEraser::MotionUnitName(d.motionUnit),d.qualified,d.evidenceSeconds*1000,
				a.sample.rawWidth,a.sample.rawHeight,a.widthDip,a.heightDip,a.active);
			DiagnosticOutput::Text(kTouchAreaTarget, inputText, true);
			if(d.eraserContact && d.eraserKind==SpeedEraser::EraserKind::Speed &&
				(d.response==SpeedEraser::ResponseModel::DirectTouch ||
					d.response==SpeedEraser::ResponseModel::ScreenPenHybrid))
			{
				const auto& f=d.follow;
				char followText[768]{};
				// 当前帧无Move时许可为零；另列最近真实输入，避免把残留目标当成新运动。
				std::snprintf(followText,sizeof(followText),
					"[EraserFollow] seq=%llu rawTargetDip=%.3f effectiveTargetDip=%.3f evidenceCapDip=%.3f growthGoalDip=%.3f actualDip=%.3f lastInputSweepSpeed=%.3f lastInputAgeMs=%.1f sweepMotionQualified=%d areaMotionQualified=%d holdRemainingMs=%.1f holdReason=%s decreaseMs=%.1f decreaseConfirmMs=%.1f decreasePending=%d shrinking=%d sweeping=%d decreaseAction=%s resetCount=%llu lastResetReason=%s\n",
					static_cast<unsigned long long>(sequence),f.rawTargetDip,d.targetDiameterDip,d.evidenceCapDiameterDip,
					f.growthGoalDip,d.effectiveDiameterDip,f.lastInputSweepSpeed,f.lastInputAgeSeconds*1000,
					f.motionGrowthPermitted,f.areaGrowthPermitted,f.holdRemainingSeconds*1000,f.holdReason,
					f.decreaseProgressSeconds*1000,f.decreaseConfirmationSeconds*1000,f.decreasePending,f.shrinking,
					d.sweeping,f.decreaseReason,static_cast<unsigned long long>(f.decreaseResetCount),f.lastDecreaseResetReason);
				DiagnosticOutput::Text(kTouchAreaTarget, followText, true);
			}
			char text[3072]{};
			if(!d.active)
			{
				std::snprintf(text,sizeof(text),"[TouchArea] seq=%llu event=%s gate=%s contact=%d inputType=%u source=%s cursorPx=%.3f requested=%d\n",
					static_cast<unsigned long long>(sequence),event,gate,contact,d.inputType,
					SpeedEraser::SourceKindName(source.kind),d.cursorDiameterPx,requested);
				DiagnosticOutput::Text(kTouchAreaTarget, text, true);return;
			}
			std::snprintf(text,sizeof(text),
				"[TouchArea] seq=%llu event=%s gate=%s contact=%d speedMode=%d inputType=%u source=%s recognition=%u tcid=%u cid=%u sourceGen=%llu\n"
				"[TouchArea] seq=%llu model=%s scale=%s unit=%s monitor=%p mappedMonitor=%p mapped=%d mappedRect=(%d,%d,%d,%d) pixels=%dx%d dpi=%.1fx%.1f DIP/px=%.6fx%.6f motion/px=%.6fx%.6f rho=%.6f manualCm=%.1fx%.1f displayGen=%llu/%llu\n"
				"[TouchArea] seq=%llu requested=%d latched=%d raw=%.3fx%.3f convertedPx=%.3fx%.3f units=%s DIP=%.3fx%.3f valid=%d reason=%s ready=%d fresh=%d releasing=%d unlocked=%d stableMs=%.1f refDip=%.3fx%.3f outlierAxes=%u outlierRatio=%.3fx%.3f recoveryCandidateDip=%.3fx%.3f recoveryMs=%.1f recovering=%d recoveryCount=%u firstRefFloor=%.3f refFloor=%.3f acceptedFloor=%.3f areaActive=%d areaAboveB=%d speed=%.3f evidenceMs=%.1f targetDIP=%.3f actualDIP=%.3f cursorPx=%.3f nextRadiusPx=%.3f historyRadiusPx=%.3f points=%llu idleMs=%.1f animate=%d\n",
				static_cast<unsigned long long>(sequence),event,gate,contact,d.active,d.inputType,
				SpeedEraser::SourceKindName(source.kind),static_cast<unsigned>(source.recognition),source.contextId,source.cursorId,static_cast<unsigned long long>(source.generation),
				static_cast<unsigned long long>(sequence),SpeedEraser::ResponseModelName(d.response),SpeedEraser::ScaleSourceName(d.motionSource),SpeedEraser::MotionUnitName(d.motionUnit),
				reinterpret_cast<void*>(d.monitor),reinterpret_cast<void*>(source.mappedMonitor),d.inputMapped,
				source.mappedLeft,source.mappedTop,source.mappedWidth,source.mappedHeight,d.pixelWidth,d.pixelHeight,d.dpiX,d.dpiY,
				d.dipPerPixelX,d.dipPerPixelY,d.motionPerPixelX,d.motionPerPixelY,d.rhoMmPerDip,d.manualWidthCm,d.manualHeightCm,
				static_cast<unsigned long long>(d.displayGeneration),static_cast<unsigned long long>(d.displayRevision),
				static_cast<unsigned long long>(sequence),requested,a.enabled,a.sample.rawWidth,a.sample.rawHeight,a.sample.widthPx,a.sample.heightPx,
				SpeedEraser::ContactAreaUnitsName(a.sample.units),a.widthDip,a.heightDip,a.sampleValid,SpeedEraser::ContactAreaReasonName(a.reason),
				a.referenceReady,a.referenceFresh,a.releasing,d.touchUnlocked,a.stableMotionSeconds*1000,
				a.referenceWidthDip,a.referenceHeightDip,static_cast<unsigned>(a.outlierAxes),
				a.outlierWidthRatio,a.outlierHeightRatio,a.recoveryCandidateWidthDip,a.recoveryCandidateHeightDip,
				a.recoveryMotionSeconds*1000,a.recovering,a.recoveryCount,a.firstReferenceFloorDip,
				a.referenceFloorDip,a.activeFloorDip,a.active,a.areaFloorAboveStandard,
				d.speed,d.evidenceSeconds*1000,d.targetDiameterDip,d.effectiveDiameterDip,d.cursorDiameterPx,d.nextRadiusPx,d.historyRadiusPx,
				static_cast<unsigned long long>(d.realPointCount),d.idleSeconds*1000,d.needsAnimation);
			DiagnosticOutput::Printf(kTouchAreaTarget,"[FineBand] seq=%llu speed=%.3f unit=%s held=%d enter=%.3f release=%.3f change=%.3f direction=%d targetDIP=%.3f actualDIP=%.3f areaFloorDIP=%.3f animate=%d\n",
				static_cast<unsigned long long>(sequence),d.fine.speed,SpeedEraser::MotionUnitName(d.motionUnit),
				d.fine.held,d.fine.enterProgress,d.fine.releaseProgress,d.fine.changeProgress,d.fine.direction,
				d.targetDiameterDip,d.effectiveDiameterDip,a.activeFloorDip,d.needsAnimation);
			const char* profile=d.response!=SpeedEraser::ResponseModel::DirectTouch?"N/A":
				d.motionUnit!=SpeedEraser::MotionUnit::MillimetersPerSecond?"Fallback":
				d.touchProfileWeight<=0?"Small":d.touchProfileWeight>=1?"Classroom":"Intermediate";
			char curveText[960]{};
			std::snprintf(curveText,sizeof(curveText),
				"[TouchCurve] seq=%llu requestedDeviceMode=%s resolvedTouchProfile=%s profileWeight=%.4f profileSource=%s longEdgeMm=%.1f motionSource=%s unit=%s fine=%.3f enter=%.3f exit=%.3f large=%.3f B=%.3f sweepGain=%.3f speed=%.3f sweepSpeed=%.3f fineSpeed=%.3f qualified=%d evidenceMs=%.1f evidenceStartMs=%.1f evidenceFullMs=%.1f evidenceDecayMs=%.1f evidenceCapDIP=%.3f growthTauMs=%.1f largeGrowthTauMs=%.1f logGrowth=%.2f largeLogGrowth=%.2f targetDIP=%.3f actualDIP=%.3f cursorPx=%.3f geometryPx=%.3f\n",
				static_cast<unsigned long long>(sequence),SpeedEraser::DeviceModeName(d.requestedDeviceMode),profile,
				d.touchProfileWeight,SpeedEraser::TouchProfileSourceName(d.touchProfileSource),d.touchSurfaceLongEdgeMm,
				SpeedEraser::ScaleSourceName(d.motionSource),SpeedEraser::MotionUnitName(d.motionUnit),
				d.fineToStandardSpeed,d.sweepEnterSpeed,d.sweepExitSpeed,d.largeTargetSpeed,d.sizes.standardDiameterDip,
				d.sweepGain,d.speed,d.sweepSpeed,d.fine.speed,d.qualified,d.evidenceSeconds*1000,
				d.evidenceStartSeconds*1000,d.evidenceFullSeconds*1000,d.evidenceDecaySeconds*1000,
				d.evidenceCapDiameterDip,d.growthTauSeconds*1000,d.largeGrowthTauSeconds*1000,
				d.maximumLogGrowthPerSecond,d.largeLogGrowthPerSecond,
				d.targetDiameterDip,d.effectiveDiameterDip,d.cursorDiameterPx,d.nextRadiusPx*2);
			// 只在帧级诊断入口限频输出，不在 RTS packet 热路径写日志，也不持快照锁输出。
			DiagnosticOutput::Text(kTouchAreaTarget, text, true);
			DiagnosticOutput::Text(kTouchAreaTarget, curveText, true);
		}

		static void ObserveDrawingActivity(void* context, bool active) noexcept
		{
			auto* self = static_cast<Impl*>(context);
			if (!self) return;
			(void)self->drawingActivity.Publish(active,
				self->runtimeCallbacks.context,
				self->runtimeCallbacks.drawingActivityChanged);
		}

		static void ObserveShortcutContext(void* context, Shortcuts::Context value) noexcept
		{
			auto* self = static_cast<Impl*>(context);
			if (!self) return;
			std::scoped_lock lock(self->presentationTargetMutex);
			self->shortcuts.Publish(value, self->bridge.Snapshot().shortcutIntentEpoch);
		}
		static Shortcuts::Result AllowShortcutCommand(void* context,
			const Shortcuts::Request& request, const Shortcuts::Context& actual) noexcept
		{
			auto* self = static_cast<Impl*>(context);
			if (!self) return Shortcuts::Result::Stale;
			std::scoped_lock lock(self->presentationTargetMutex);
			(void)self->ShortcutContextLocked();
			return self->shortcuts.Authorize(request, actual);
		}
		static void CompleteShortcut(void* context, std::uint64_t id, Shortcuts::Result result) noexcept
		{
			auto* self = static_cast<Impl*>(context);
			if (self) self->shortcuts.Complete(id, result);
		}

		static void ObserveAcceptedStrokeScene(void* context, const Bridge::AcceptedStrokeBegin& scene) noexcept
		{
			auto* self = static_cast<Impl*>(context);
			if (self) self->acceptedStrokes.PublishScene(scene);
		}

		static void ObserveAcceptedStroke(void* context, const Bridge::AcceptedStrokeBegin& value) noexcept
		{
			auto* self = static_cast<Impl*>(context);
			if (!self || !self->runtimeCallbacks.acceptedStrokeBegan) return;
			auto accepted = value;
			self->acceptedStrokes.Stamp(accepted);
			self->runtimeCallbacks.acceptedStrokeBegan(self->runtimeCallbacks.context, accepted);
		}

		void EndDrawingActivity() noexcept
		{
			(void)drawingActivity.EndIfActive(runtimeCallbacks.context,
				runtimeCallbacks.drawingActivityChanged);
			// 绘制线程每个退出点都经此：视口通道复位为“离开白板”，Freeze 不再依据旧样本画相位。
			if (runtimeCallbacks.whiteboardViewportChanged)
				runtimeCallbacks.whiteboardViewportChanged(runtimeCallbacks.context, WhiteboardViewportSample{});
		}

		static bool ApplyStyle(void* context, DWORD setMask, DWORD clearMask)
		{
			const auto* callbacks = static_cast<const HostStyleCallbacks*>(context);
			// 无产品宿主时允许隐藏测试使用既有样式；产品宿主必须提供 Window Service 回调。
			return !callbacks || !callbacks->setExtendedStyleFlags ||
				callbacks->setExtendedStyleFlags(callbacks->context, setMask, clearMask);
		}


		void PublishDisplaySnapshot(Inkeys::Display::SnapshotPtr snapshot)
		{
			{
				std::scoped_lock lock(displayMutex);
				pendingDisplaySnapshot = std::move(snapshot);
			}
			displayScaleDirty.store(true, std::memory_order_release);
			(void)input.PublishControlWake();
		}

		void PumpDisplayScale()
		{
			if (!displayScaleDirty.exchange(false, std::memory_order_acq_rel)) return;
			Inkeys::Display::SnapshotPtr snapshot;
			SpeedEraser::DevelopmentOptions development;
			{
				std::scoped_lock lock(displayMutex);
				snapshot = pendingDisplaySnapshot;
				development = eraserDevelopment;
			}
			const HWND hwnd = attachedWindow.load(std::memory_order_acquire);
			SpeedEraser::DisplayScale scale;
			scale.development = development;
			window.SetTouchContactAreaAssistance(development.touchContactAreaAssistance);
			scale.development.touchAreaTrace=false;
			touchAreaTraceEnabled.store(development.touchAreaTrace,std::memory_order_relaxed);
			scale.development.touchContactAreaAssistance=false; // 面积开关不属于 Mouse/Pen 的显示标尺。
			window.SetEraserDiagnosticsEnabled(development.diagnostics || development.touchAreaTrace || hiddenTestContactInjectionEnabled || startOptions.enableEraserDiagnostics);
			scale.generation = snapshot ? snapshot->generation : 0;
			const HMONITOR monitorHandle = hwnd ? MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST) : nullptr;
			scale.monitor = reinterpret_cast<std::uintptr_t>(monitorHandle);
			const auto* monitor = snapshot ? snapshot->Find(monitorHandle) : nullptr;
			RECT clientBounds{};
			POINT origin{};
			const bool clientKnown = hwnd && GetClientRect(hwnd, &clientBounds) && ClientToScreen(hwnd, &origin);
			if (clientKnown) OffsetRect(&clientBounds, origin.x, origin.y);
			if (monitor)
			{
				scale.dipPerPixelX = 96.0f / (monitor->effectiveDpiX ? monitor->effectiveDpiX : 96u);
				scale.dipPerPixelY = 96.0f / (monitor->effectiveDpiY ? monitor->effectiveDpiY : 96u);
				scale.pixelWidth=monitor->pixelWidth;scale.pixelHeight=monitor->pixelHeight;
				scale.desktopLeft=monitor->bounds.left;scale.desktopTop=monitor->bounds.top;
				scale.orientation=monitor->orientation;
				scale.logicalOutputKnown=!snapshot->fallback && !monitor->fallback && clientKnown &&
					clientBounds.left>=monitor->bounds.left && clientBounds.top>=monitor->bounds.top &&
					clientBounds.right<=monitor->bounds.right && clientBounds.bottom<=monitor->bounds.bottom;
				scale.physicalAvailable = monitor->physicalSize.available &&
					monitor->pixelWidth > 0 && monitor->pixelHeight > 0;
				if (scale.physicalAvailable)
				{
					scale.cmPerPixelX = static_cast<float>(monitor->physicalSize.widthCm) / monitor->pixelWidth;
					scale.cmPerPixelY = static_cast<float>(monitor->physicalSize.heightCm) / monitor->pixelHeight;
				}
				// 输入直接性及目标映射交由当前 RTS 来源确认，不能按整机单屏推断。
			}
			else if (hwnd)
			{
				const UINT dpi = QueryCompatibleWindowDpi(hwnd);
				scale.dipPerPixelX = scale.dipPerPixelY = 96.0f / (dpi ? dpi : 96u);
			}
			const bool syntheticDisplay=hiddenTestContactInjectionEnabled && startOptions.hiddenTestDisplayScale.has_value();
			if(syntheticDisplay)
			{
				const auto policy=scale.development;
				scale=*startOptions.hiddenTestDisplayScale;scale.development=policy;
			}
			if(development.touchAreaTrace)
			{
				// 只在显示/诊断配置发布时记录同一快照；活动分辨率不冒充 EDID 原始时序。
				const auto* edidMonitor=syntheticDisplay?nullptr:monitor;
				const Inkeys::Display::EdidInfo emptyEdid;
				const Inkeys::Display::PhysicalSizeInfo emptyPhysicalSize;
				const auto& edid=edidMonitor?edidMonitor->edid:emptyEdid;
				const auto& physicalSize=edidMonitor?edidMonitor->physicalSize:emptyPhysicalSize;
				char displayText[768]{};
				std::snprintf(displayText,sizeof(displayText),
					"[EraserDisplay] synthetic=%d snapshotGen=%llu scaleGen=%llu topology=%s monitor=%p desktopLeftTopSizePx=(%d,%d,%d,%d) activeResolutionPx=%dx%d effectiveDpi=%.1fx%.1f DIP/px=%.6fx%.6f logicalKnown=%d edidStatus=%s edidValid=%d edidVersion=%u.%u edidRawCm=%dx%d physicalAvailable=%d physicalCm=%dx%d physicalReason=%s\n",
					syntheticDisplay,static_cast<unsigned long long>(snapshot?snapshot->generation:0),
					static_cast<unsigned long long>(scale.generation),DisplayTopologyName(snapshot?snapshot->topology:Inkeys::Display::DisplayTopology::Unknown),
					reinterpret_cast<void*>(scale.monitor),scale.desktopLeft,scale.desktopTop,scale.pixelWidth,scale.pixelHeight,
					scale.pixelWidth,scale.pixelHeight,96.0/scale.dipPerPixelX,96.0/scale.dipPerPixelY,
					scale.dipPerPixelX,scale.dipPerPixelY,scale.logicalOutputKnown,EdidStatusName(edid.status),edid.valid,
					static_cast<unsigned>(edid.majorVersion),static_cast<unsigned>(edid.minorVersion),
					edid.rawPhysicalWidthCm,edid.rawPhysicalHeightCm,physicalSize.available,
					physicalSize.widthCm,physicalSize.heightCm,
					edidMonitor?PhysicalReasonName(physicalSize.unavailableReason):"NoMonitor");
				DiagnosticOutput::Text(kTouchAreaTarget, displayText, true);
			}
			const auto previous = window.SpeedEraserDisplayScaleSnapshot();
			scale.revision = previous.revision;
			if (!(scale == previous) || !EqualRect(&clientBounds, &appliedDisplayClientBounds))
			{
				scale.revision = previous.revision + 1;
				appliedDisplayClientBounds = clientBounds;
				window.SetSpeedEraserDisplayScale(scale);
			}
			Inkeys::Business::PenWidth::Context penContext;
			penContext.paintDevice = bridge.Snapshot().paintDevice;
			penContext.effectiveDpi = monitor && !syntheticDisplay
				? (monitor->effectiveDpiX ? monitor->effectiveDpiX : 96u)
				: syntheticDisplay ? 96.0 / scale.dipPerPixelX : QueryCompatibleWindowDpi(hwnd);
			penContext.pixelWidth = scale.pixelWidth;
			penContext.pixelHeight = scale.pixelHeight;
			if (!monitor && !syntheticDisplay)
			{
				// 快照缺目标时用同一目标全幅矩形回退，不借用窗口/工作区/虚拟桌面。
				MONITORINFO info{};
				info.cbSize = sizeof(info);
				if (monitorHandle && GetMonitorInfoW(monitorHandle, &info))
				{
					penContext.pixelWidth = info.rcMonitor.right - info.rcMonitor.left;
					penContext.pixelHeight = info.rcMonitor.bottom - info.rcMonitor.top;
				}
			}
			if (!penWidthContextPrepared || appliedPenWidthMonitor != scale.monitor ||
				!Inkeys::Business::PenWidth::EquivalentContext(penContext, appliedPenWidthContext))
			{
				penContext.revision = appliedPenWidthContext.revision + 1;
				appliedPenWidthContext = penContext;
				appliedPenWidthMonitor = scale.monitor;
				penWidthContextPrepared = true;
				// 不持显示锁；返回前产品必须发布同代 resolved DIP 和目标上下文。
				if (runtimeCallbacks.penWidthContextChanged)
					runtimeCallbacks.penWidthContextChanged(runtimeCallbacks.context, penContext);
			}
		}

		void ApplyLaserOptions(DrawingController& controller, uint32_t general) noexcept
		{
			const uint32_t savedLaser = laserOptions.load(std::memory_order_acquire);
			const uint32_t effectiveLaser = (savedLaser & ~1u) | ((general & 1u) ? savedLaser & 1u : 0u);
			if (effectiveLaser != appliedLaserOptions)
			{
				controller.SetLaserMultiTouchDrawingEnabled((effectiveLaser & 1u) != 0);
				controller.SetLaserParticleOptions(effectiveLaser >> 1);
				appliedLaserOptions = effectiveLaser;
			}
		}

		void PumpBridgeState()
		{
			// 仅绘制 owner 访问 Controller；独立配置不受 bridge revision 去重影响。
			// 呈现器按批次取值，活动接触期间也可以更新。
			const bool multiStepCommitEnabled = ulwMultiStepCommitEnabled.load(std::memory_order_acquire);
			if (appliedUlwMultiStepCommitEnabled != multiStepCommitEnabled)
			{
				presentation.SetUlwMultiStepCommitEnabled(multiStepCommitEnabled);
				appliedUlwMultiStepCommitEnabled = multiStepCommitEnabled;
			}
			const bool reconnectEnabled = interruptedStrokeReconnectEnabled.load(std::memory_order_acquire);
			if (drawing && drawing->GetInterruptedStrokeReconnectEnabled() != reconnectEnabled)
				drawing->SetInterruptedStrokeReconnectEnabled(reconnectEnabled);
			if (drawing)
			{
				const uint32_t reconnectDevices = interruptedStrokeReconnectDevices.load(std::memory_order_acquire);
				if (reconnectDevices != appliedInterruptedStrokeReconnectDevices)
				{
					drawing->SetInterruptedStrokeReconnectDevices(reconnectDevices);
					appliedInterruptedStrokeReconnectDevices = reconnectDevices;
				}
			}
			if (drawing)
			{
				PalmEraser::Settings palmOptions;
				{ std::scoped_lock lock(palmEraserOptionsMutex); palmOptions = palmEraserOptions; }
				if (!appliedPalmEraserOptions || *appliedPalmEraserOptions != palmOptions)
				{
					drawing->SetPalmEraserOptions(palmOptions);
					appliedPalmEraserOptions = palmOptions;
				}
				const uint32_t canvasDebug = canvasDebugOptions.load(std::memory_order_acquire);
				if (canvasDebug != appliedCanvasDebugOptions)
				{
					drawing->SetCanvasDebugOptions((canvasDebug & 1u) != 0, (canvasDebug & 2u) != 0);
					appliedCanvasDebugOptions = canvasDebug;
				}
				// 输入通路只在启动/回退时变化；与 HostInputMode 数值一致，仅供调试板显示。
				const uint32_t inputPath = static_cast<uint32_t>(inputRun.load(std::memory_order_acquire).actual);
				if (inputPath != appliedCanvasDebugInputMode)
				{
					drawing->SetCanvasDebugInputMode(static_cast<uint8_t>(inputPath));
					appliedCanvasDebugInputMode = inputPath;
				}
				const uint32_t panInertia = canvasPanInertiaOptions.load(std::memory_order_acquire);
				if (panInertia != appliedCanvasPanInertiaOptions)
				{
					drawing->SetCanvasPanInertiaOptions(CanvasPanInertiaOptions::Decode(panInertia));
					appliedCanvasPanInertiaOptions = panInertia;
				}
				const auto taper = liveTaperOptions.load(std::memory_order_acquire);
				if (!appliedLiveTaperOptions || *appliedLiveTaperOptions != taper)
				{
					drawing->SetLiveTaperOptions(taper);
					appliedLiveTaperOptions = taper;
				}
				const uint32_t general = generalDrawingOptions.load(std::memory_order_acquire);
				if (general != appliedGeneralDrawingOptions)
				{
					drawing->SetGeneralDrawingOptions((general & 1u) != 0, (general & 2u) != 0,
						static_cast<InkPredictionMode>((general >> 2) & 3u));
					appliedGeneralDrawingOptions = general;
				}
				const uint32_t tools = multiTouchTools.load(std::memory_order_acquire);
				if (tools != appliedMultiTouchTools)
				{
					drawing->SetMultiTouchToolOptions(tools);
					appliedMultiTouchTools = tools;
				}
				ApplyLaserOptions(*drawing, general);
				drawing->SetDrawingHapticsEnabled(drawingHapticsEnabled.load(std::memory_order_acquire));
			}
			PumpDisplayScale();
			{
				std::scoped_lock lock(presentationTargetMutex);
				RefreshPresentationInputGateLocked();
			}
			const Bridge::ProductState state = bridge.Snapshot();
			if (state.revision == appliedBridgeRevision && !restoreLatestScene) return;
			appliedBridgeRevision = state.revision;

			window.SetSelectionMode(state.selectionMode);
			{
				std::scoped_lock lock(presentationTargetMutex);
				if (shortcutSelectionHandoffPending && window.SelectionMode() == bridge.Snapshot().selectionMode)
				{
					shortcutSelectionHandoffPending = false;
					input.SetShortcutAdmissionBlocked(shortcuts.Lease() != 0);
				}
			}
			window.SetAutoSaveEnabled(state.autoSaveEnabled);
			window.SetCrashRecoverInkEnabled(state.crashRecoverInk);
			recoverySession.PublishRecoverInk(state.crashRecoverInk);
			const bool selectionChanged = selectionMode.exchange(state.selectionMode,
				std::memory_order_acq_rel) != state.selectionMode;
			DrawingTool tool = DrawingTool::Pen;
			switch (state.tool)
			{
			case Bridge::Tool::HardPen: tool = DrawingTool::HardPen; break;
			case Bridge::Tool::Highlighter: tool = DrawingTool::Highlighter; break;
			case Bridge::Tool::ConfiguredEraser:
			case Bridge::Tool::FixedEraser:
			case Bridge::Tool::SpeedEraser: tool = DrawingTool::Eraser; break;
			case Bridge::Tool::Laser: tool = DrawingTool::Laser; break;
			case Bridge::Tool::SolidLine: tool = DrawingTool::SolidLine; break;
			case Bridge::Tool::DashedLine: tool = DrawingTool::DashedLine; break;
			case Bridge::Tool::OutlineRectangle: tool = DrawingTool::OutlineRectangle; break;
			case Bridge::Tool::FilledRectangle: tool = DrawingTool::FilledRectangle; break;
			case Bridge::Tool::Shape: tool = DrawingTool::Shape; break;
			default: break;
			}
			window.SetActiveTool(tool);
			// 活动 contact 可能仍锁存旧工具，原始偏好交由 Controller 按实际工具投影。
			if (drawing) drawing->SetCursorSettings(state.cursorSettings);
			window.SetSpeedEraserDeviceMode(state.paintDevice == 0
				? SpeedEraser::DeviceMode::LargeScreen : SpeedEraser::DeviceMode::Laptop);
			window.SetEraserWidthMode(state.tool == Bridge::Tool::SpeedEraser
				? EraserWidthMode::Speed : EraserWidthMode::Fixed);
			window.SetEraserInputs(state.eraserInputs);
			if (drawing)
			{
				// 仅绘制 owner 消费 Window 的整表快照；形状由 Controller 在 Down 锁存。
				const auto eraser = window.EraserInputsSnapshot();
				drawing->SetEraserCursorAppearance({ eraser.shape, eraser.cursorPattern, eraser.cursorTheme });
			}
			window.SetEraserToolPolicy(state.tool==Bridge::Tool::FixedEraser?SpeedEraser::EraserToolPolicy::Fixed:
				state.tool==Bridge::Tool::SpeedEraser?SpeedEraser::EraserToolPolicy::Speed:SpeedEraser::EraserToolPolicy::ByEntry);
			window.SetProductVisualStyle(state.colorRgba, state.widthDip, state.penWidthContext,
				state.shapeTool, state.shapeStyle);
			const bool dispatchScene = PresentationSceneNeedsDispatch(state,
				workspace.load(std::memory_order_acquire), requestedWorkspace,
				requestedPresentationTargetRevision, restoreLatestScene);
			if (state.workspace != Bridge::Workspace::Presentation && dispatchScene)
			{
				CanvasCommand workspaceCommand;
				workspaceCommand.type = CanvasCommandType::SetWorkspace;
				workspaceCommand.workspace = static_cast<std::uint8_t>(state.workspace);
				{
					std::scoped_lock lock(presentationTargetMutex);
					workspaceCommand.sceneClosedAdmission = PresentationSceneNeedsReady(state,
						workspace.load(std::memory_order_acquire), readyPresentationTarget, commandScenePending);
				}
				window.EnqueueCanvasCommand(workspaceCommand);
				requestedWorkspace = state.workspace;
				requestedPresentationTargetRevision = 0;
			}
			if (state.workspace == Bridge::Workspace::Presentation &&
				state.presentationTarget && dispatchScene)
			{
				CanvasCommand targetCommand;
				targetCommand.type = CanvasCommandType::SetPresentationTarget;
				targetCommand.presentationTarget = state.presentationTarget;
				{
					std::scoped_lock lock(presentationTargetMutex);
					targetCommand.sceneClosedAdmission = PresentationSceneNeedsReady(state,
						workspace.load(std::memory_order_acquire), readyPresentationTarget, commandScenePending);
				}
				window.EnqueueCanvasCommand(std::move(targetCommand));
				requestedWorkspace = state.workspace;
				requestedPresentationTargetRevision =
					state.presentationTarget->targetRevision;
			}
			else if (state.workspace == Bridge::Workspace::Presentation &&
				!state.presentationTarget && dispatchScene)
			{
				CanvasCommand workspaceCommand;
				workspaceCommand.type = CanvasCommandType::SetWorkspace;
				workspaceCommand.workspace = static_cast<std::uint8_t>(
					Bridge::Workspace::Presentation);
				{
					std::scoped_lock lock(presentationTargetMutex);
					workspaceCommand.sceneClosedAdmission = PresentationSceneNeedsReady(state,
						workspace.load(std::memory_order_acquire), readyPresentationTarget, commandScenePending);
				}
				window.EnqueueCanvasCommand(workspaceCommand);
				requestedWorkspace = state.workspace;
				requestedPresentationTargetRevision = 0;
			}
			restoreLatestScene = false;
			bool preferencesChanged;
			{
				std::scoped_lock lock(presentationTargetMutex);
				// 只确认 owner 已完整应用的版本；Bridge 发布目标不等于实际工具已就绪。
				preferencesChanged = appliedProductRevision != state.revision || appliedTool != state.tool;
				appliedProductRevision = state.revision;
				appliedTool = state.tool;
				// 清本次样式原因不能释放其他工作区/加载/恢复门。
				if (state.workspace != Bridge::Workspace::Whiteboard)
					whiteboardStylePending = false;
				else if (state.whiteboardStyle == whiteboardStyle && state.whiteboardStyle.hostGeneration != 0)
					whiteboardStylePending = false;
				RefreshPresentationInputGateLocked();
			}
			if (selectionChanged || preferencesChanged) PublishRuntimeRevision();
		}

		void EnqueueCommandScene(const Bridge::Command& command)
		{
			bool sceneClosedAdmission = false;
			{
				std::scoped_lock lock(presentationTargetMutex);
				const auto captured = command.presentationTarget
					? std::optional(Bridge::ReadyIdentityFor(*command.presentationTarget))
					: std::nullopt;
				if ((command.workspace == Bridge::Workspace::Presentation ||
					workspace.load(std::memory_order_acquire) == Bridge::Workspace::Presentation) &&
					(captured != readyPresentationTarget || command.workspace != workspace.load(std::memory_order_acquire)))
				{
					// 旧场景上的已接受命令仍须 FIFO 完成；重放期间不能沿用 latest 的旧 ready 开放输入。
					readyPresentationTarget.reset();
					commandScenePending = true;
					input.SetAdmissionBlocked(true);
					sceneClosedAdmission = true;
					PublishRuntimeRevision();
				}
			}
			CanvasCommand scene;
			scene.sceneClosedAdmission = sceneClosedAdmission;
			if (command.workspace == Bridge::Workspace::Presentation &&
				command.presentationTarget)
			{
				scene.type = CanvasCommandType::SetPresentationTarget;
				scene.presentationTarget = command.presentationTarget;
			}
			else
			{
				scene.type = CanvasCommandType::SetWorkspace;
				scene.workspace = static_cast<std::uint8_t>(command.workspace);
			}
			window.EnqueueCanvasCommand(std::move(scene));
			requestedWorkspace = command.workspace;
			requestedPresentationTargetRevision = command.workspace == Bridge::Workspace::Presentation &&
				command.presentationTarget ? command.presentationTarget->targetRevision : 0;
		}

		void PumpOneBridgeCommand()
		{
			Bridge::Command command;
			if (!bridge.TryConsume(command))
			{
				DiagnosticOutput::Text(DiagnosticOutput::Stream::Error, "[Draw3.Input] action=command_wake result=failed reason=bridge_empty\n");
				return;
			}
			// 每个独立 Command marker 只执行一条 scene-stamped 命令。
			restoreLatestScene = true;
			if (!command.shortcut) EnqueueCommandScene(command); // 快捷键只条件执行当前页，禁止重放旧页。
			CanvasCommand canvas;
			switch (command.type)
			{
			case Bridge::CommandType::ShapeInteraction:
				canvas.type = CanvasCommandType::ShapeInteraction; canvas.shapeAction = command.shapeAction; break;
			case Bridge::CommandType::SetWhiteboardTheme:
				canvas.type = CanvasCommandType::SetWhiteboardTheme;
				canvas.whiteboardTheme = command.whiteboardTheme;
				canvas.whiteboardGeneration = command.whiteboardGeneration;
				canvas.pageIndex = command.whiteboardPage;
				break;
			case Bridge::CommandType::Clear: canvas.type = CanvasCommandType::Clear; break;
			case Bridge::CommandType::Undo: canvas.type = CanvasCommandType::Undo; break;
			case Bridge::CommandType::Redo: canvas.type = CanvasCommandType::Redo; break;
			case Bridge::CommandType::ShortcutFence: canvas.type = CanvasCommandType::ShortcutFence; break;
			case Bridge::CommandType::NextPage: canvas.type = CanvasCommandType::NextPage; break;
			case Bridge::CommandType::PreviousPage: canvas.type = CanvasCommandType::PreviousPage; break;
			case Bridge::CommandType::PrepareExitAutoSave:
				canvas.type = CanvasCommandType::PrepareExitAutoSave; break;
			default: return;
			}
			if (command.workspace == Bridge::Workspace::Whiteboard &&
				workspace.load(std::memory_order_acquire) == Bridge::Workspace::Whiteboard &&
				(canvas.type == CanvasCommandType::NextPage || canvas.type == CanvasCommandType::PreviousPage))
			{
				std::scoped_lock lock(presentationTargetMutex);
				// 与 PPT 页边界一致：立即关闭新 Down，并由现有 admission revision 封口旧 contact。
				commandScenePending = true;
				canvas.sceneClosedAdmission = true;
				RefreshPresentationInputGateLocked();
				PublishRuntimeRevision();
			}
			canvas.shortcut = std::move(command.shortcut);
			if (!canvas.shortcut && canvas.type != CanvasCommandType::PrepareExitAutoSave)
			{
				// 场景前缀若准备失败，业务命令必须明确失败，不能落到别的文稿。
				canvas.requireSceneMatch = true;
				canvas.workspace = static_cast<std::uint8_t>(command.workspace);
				canvas.presentationTarget = std::move(command.presentationTarget);
			}
			window.EnqueueCanvasCommand(canvas);
		}

		void ObservePresentationTerminal(const PagedInkCompletion& completion) noexcept
		{
			if (!startOptions.enableHiddenTestContactInjection) return;
			const std::scoped_lock lock(hiddenPersistenceMutex);
			hiddenPersistence.Settle(completion);
		}

		void PumpPresentationCompletions()
		{
			DrainPresentationCompletions(presentationStores, pendingPresentationCompletion, window,
				[this](const PagedInkCompletion& completion) noexcept
				{
					ObservePresentationTerminal(completion);
					return true;
				});
		}

		static void LogDesktopCancellation(const DesktopInkCompletion& completion) noexcept
		{
			if (completion.result != DesktopInkResult::Canceled) return;
			DiagnosticOutput::Printf(DiagnosticOutput::Stream::Error,
				"[Draw3.DesktopInkSnapshots] action=%s result=canceled reason=closing generation=%llu systemError=%lu diagnostics=%zu read_diagnostics=%zu\n",
				completion.operation == DesktopInkOperation::Save ? "save" : "load",
				static_cast<unsigned long long>(completion.identity.clearGeneration),
				static_cast<unsigned long>(completion.systemError), completion.diagnostics.size(),
				completion.readDiagnostics.size());
		}

		// C1.6b：最近一次 Clear 的快照完整落盘后登记为会话的上一区间（代次与 Rotate 的 Clear 代次一致）。
		void NoteDesktopClearSnapshotSaved(const DesktopInkCompletion& completion) noexcept
		{
			if (!recoverySession.Active() || completion.operation != DesktopInkOperation::Save ||
				completion.result != DesktopInkResult::Succeeded || completion.degraded || !completion.publishedPath)
				return;
			try
			{
				{
					const std::scoped_lock lock(desktopClearRequestMutex);
					if (!desktopClearRequestId || *desktopClearRequestId != completion.identity.requestId.Bytes()) return;
				}
				// 本回调可能在绘制线程上：只记下请求，由会话 worker 写盘。开关关闭时不登记。
				if (recoverySession.RecoverInk())
					recoverySession.PublishDesktopPreviousInterval(*completion.publishedPath, completion.identity.clearGeneration);
			}
			catch (...) {}
		}

		void SettleDesktopCompletions() noexcept
		{
			// ACK 后或绘制线程退出后只核销终态；已取入槽的回执已记录诊断。
			(void)window.TakeDesktopCompletionForExit();
			pendingDesktopCompletion.reset();
			while (auto completion = desktopSnapshots.Take()) LogDesktopCancellation(*completion);
		}

		void PumpDesktopCompletions() noexcept
		{
			if (desktopPersistenceInstallationClosed)
			{
				SettleDesktopCompletions();
				return;
			}
			for (;;)
			{
				if (!pendingDesktopCompletion)
				{
					pendingDesktopCompletion = desktopSnapshots.Take();
					if (!pendingDesktopCompletion) break;
					LogDesktopCancellation(*pendingDesktopCompletion);
					NoteDesktopClearSnapshotSaved(*pendingDesktopCompletion);
				}
				// 已有回执占槽时保留本个，消费槽后仍会发布普通 wake。
				if (!window.TryEnqueueDesktopCompletion(pendingDesktopCompletion)) break;
			}
		}

		bool ShownSceneCurrent(const ShownFrameKey& key) const
		{
			if (!running.load() || bridge.Snapshot().revision != key.productRevision ||
				workspace.load() != key.workspace || currentPageIndex.load() != key.pageIndex ||
				contentRevision.load() != key.contentRevision || resizeCount.load() != key.resizeCount ||
				ToHostMode(presentation.ActiveMode()) != key.mode) return false;
			std::scoped_lock lock(presentationTargetMutex);
			return !commandScenePending && recovery.revision == key.recoveryRevision && acceptedStrokes.HostGeneration() == key.hostGeneration &&
				appliedProductRevision == key.productRevision && readyPresentationTarget == key.page;
		}

		void FinishShownFrame(ShownFrameRequest& request, bool frameSucceeded)
		{
			const auto& key = request.key;
			const auto target = key.target == HostOutputTarget::SelectionUlw
				? TransparentOutputTarget::SelectionUlw : TransparentOutputTarget::PrimaryDrawpad;
			const bool confirmed = request.status.load() == ShownFrameStatus::Pending &&
				frameSucceeded && ShownSceneCurrent(key) &&
				presentation.ConfirmShownFrame(target, key.outputRevision, key.contentRevision) &&
				ShownSceneCurrent(key) && std::chrono::steady_clock::now() < request.deadline;
			auto pending = ShownFrameStatus::Pending;
			if (request.status.compare_exchange_strong(pending,
				confirmed ? ShownFrameStatus::Confirmed : ShownFrameStatus::Failed))
				PublishRuntimeRevision();
		}

		void PumpSurfaceRequests()
		{
			std::optional<AuxiliaryReleaseRequest> request;
			std::shared_ptr<ShownFrameRequest> shown;
			{
				std::scoped_lock lock(surfaceRequestMutex);
				request.swap(pendingAuxiliaryRelease);
				shown.swap(pendingShownFrame);
			}
			if (shown && shown->status.load() == ShownFrameStatus::Pending)
			{
				if (shown->key.target == HostOutputTarget::PrimaryDrawpad && ShownSceneCurrent(shown->key))
				{
					// 先消费显示请求，再从画布重画全帧；窗口显示消息触发的早先帧不能冒充回执。
					renderingShownFrame = std::move(shown);
					window.RequestFullPresent();
				}
				else FinishShownFrame(*shown, shown->key.target == HostOutputTarget::SelectionUlw);
			}
			if (!request || !running.load(std::memory_order_acquire) ||
				IsWindowVisible(attachedPresentationWindow.load(std::memory_order_acquire)) ||
				!lastPresentSucceeded.load(std::memory_order_acquire) ||
				auxiliaryReadbackPayloadBytes.load(std::memory_order_acquire) == 0) return;
			{
				std::scoped_lock lock(presentationTargetMutex);
				if (request->hostGeneration != acceptedStrokes.HostGeneration() ||
					request->productRevision != appliedProductRevision) return;
			}
			if (bridge.Snapshot().revision != request->productRevision ||
				requestedOutputTarget.load(std::memory_order_acquire) != request->target ||
				readyOutputTarget.load(std::memory_order_acquire) != request->target ||
				requestedOutputRevision.load(std::memory_order_acquire) != request->outputRevision ||
				readyOutputRevision.load(std::memory_order_acquire) != request->outputRevision ||
				presentedContentRevision.load(std::memory_order_acquire) != request->contentRevision ||
				contentRevision.load(std::memory_order_acquire) != request->contentRevision ||
				successfulPresentCount.load(std::memory_order_acquire) != request->successfulPresents ||
				committedWidth.load(std::memory_order_acquire) != request->width ||
				committedHeight.load(std::memory_order_acquire) != request->height ||
				(request->target == HostOutputTarget::SelectionUlw &&
					(currentPageHasContent.load(std::memory_order_acquire) ||
						laserVisible.load(std::memory_order_acquire) || !auxiliaryFullFrameClean.load(std::memory_order_acquire)))) return;
			const auto target = request->target == HostOutputTarget::SelectionUlw
				? TransparentOutputTarget::SelectionUlw : TransparentOutputTarget::PrimaryDrawpad;
			if (presentation.ReleaseAuxiliaryReadbackResources(target,
				request->outputRevision, request->contentRevision))
			{
				// 不请求全帧：回收自己的控制 wake 不得把刚归还的空白缓冲建回来。
				auxiliaryReadbackPayloadBytes.store(0, std::memory_order_release);
				PublishRuntimeRevision();
			}
		}

		static void ConsumeBridge(void* context, ControlWakeKind kind)
		{
			auto* self = static_cast<Impl*>(context);
			if (!self) return;
			// General 只做状态/完成收敛；Command 恰好对应一条 Bridge 命令。
			self->PumpDesktopCompletions();
			self->PumpPresentationCompletions();
			if (kind == ControlWakeKind::Command) self->PumpOneBridgeCommand();
			self->PumpBridgeState();
			self->PumpSurfaceRequests();
		}

		bool Start(HWND hwnd, HWND presentationHwnd,
			HostStyleCallbacks styleCallbacks, HostStartOptions options,
			HostRuntimeCallbacks runtimeCallbacks)
		{
			const auto failedCleanup = options.failedCleanup;
			if (running.load(std::memory_order_acquire) || drawingThread.joinable() ||
				attachedWindow.load(std::memory_order_acquire) ||
				attachedPresentationWindow.load(std::memory_order_acquire)) return false;
			UpdateCanvasInformation("正在初始化", nullptr, nullptr, true, &options.previousAttempt);
			LogDraw3BuildIdentity();
			LogDraw3WindowState("start-request", hwnd, presentationHwnd);
			BeginRuntimeMetricsRun(options);
			// 严格模式在附着外部 HWND 之前拒绝不可用的 Win7 玻璃路径。
			if ((options.requiredPresentationMode == HostPresentationMode::DwmBlurBehind && !IsWin7DwmAvailable()) ||
				!hwnd || !presentationHwnd || !IsWindow(hwnd) || !IsWindow(presentationHwnd))
			{
				RecordHostStartupFailure(!hwnd || !presentationHwnd || !IsWindow(hwnd) || !IsWindow(presentationHwnd)
					? "Drawpad HWND validation" : "Required presentation mode unavailable");
				UpdateCanvasInformation("初始化失败（窗口或呈现请求不可用）");
				SealRuntimeMetricsRun(true);
				return false;
			}
			PrepareRuntimeMetrics(options);
			// 上一代 Stop 尾部可能留下普通唤醒；新 Bridge 不能消费旧 marker。
			input.ResetForNextRun();
			const auto version = GetWindowsVersion();
			InputRun run{ options.inputPreference, options.largeScreenTouchMode,
				ResolveInputBackend(options.inputPreference, options.largeScreenTouchMode,
					version.majorVersion, version.minorVersion, version.buildNumber) };
			if (run.preference == InputPreference::Pointer &&
				!SupportsPointerInput(version.majorVersion, version.minorVersion))
				run.fallback = InputFallback::PointerUnavailableOnOs;
			inputRun.store(run, std::memory_order_release);
			inputStopping.store(false, std::memory_order_release);
			bridge.Reset();
			firstFrameReady.store(false, std::memory_order_release);
			ResetRuntimeDiagnostics();
			{std::scoped_lock lock(eraserDiagnosticsMutex);eraserDiagnostics={};penDiagnostics={};}
			this->styleCallbacks = styleCallbacks;
			this->runtimeCallbacks = runtimeCallbacks;
			startOptions = options;
			if (!ulwMultiStepCommitPublished.load(std::memory_order_acquire))
				ulwMultiStepCommitEnabled.store(options.enableUlwMultiStepCommit, std::memory_order_release);
			hiddenTestContactInjectionEnabled = options.enableHiddenTestContactInjection;
			attachedWindow.store(hwnd, std::memory_order_release);
			attachedPresentationWindow.store(presentationHwnd, std::memory_order_release);
			ExternalWindowCallbacks windowCallbacks{};
			if (!window.AttachExternal(hwnd, windowCallbacks))
			{
				RecordHostStartupFailure("WindowController::AttachExternal");
				UpdateCanvasInformation("初始化失败（附着画布窗口失败）");
				failedCleanup.BeginKnownFailure();
				attachedWindow.store(nullptr, std::memory_order_release);
				attachedPresentationWindow.store(nullptr, std::memory_order_release);
				hiddenTestContactInjectionEnabled = false;
				SealRuntimeMetricsRun(true);
				return false;
			}
			if (startOptions.startupMilestone)
				startOptions.startupMilestone(startOptions.startupContext,
					HostStartupStage::WindowAttached);
			LogDraw3WindowState("attached", hwnd, presentationHwnd);
			desktopSnapshots.Close();
			SettleDesktopCompletions();
			desktopPersistenceInstallationClosed = false;
			presentationStores.BeginRun();
			// C1.6b：先拿存活锁再发布会话记录；隐藏自检不建立会话。失败等同开关关闭（写 Warn）。
			if (!options.autoSaveRoot.empty() && !options.enableHiddenTestContactInjection &&
				!recoverySession.Active() && !recoverySession.Start(options.autoSaveRoot, options.crashRecoverInk))
				DiagnosticOutput::Text(DiagnosticOutput::OutWarn, "[Draw3.Recovery] action=session_start result=failed\n");
			const bool scanRecovery = recoverySession.Active() && options.crashRecoverInk;
			if (scanRecovery)
			{
				recoveryStop.store(false, std::memory_order_release);
				recoveryClosing.store(false, std::memory_order_release);
				recoveryPromptStop = std::stop_source{};
			}
			{
				// 绘制线程可能先进入 Run，提前设置 PPT 存储是否等待扫描。
				const std::scoped_lock lock(recoveryMutex);
				presentationScanDone = !scanRecovery;
			}
			devicePerformance.Start(options.devicePerformancePath);
			input.EnableDiagnostics(options.enableHiddenTestContactInjection || options.enableRuntimeMetrics);
			if (options.enableRuntimeMetrics) runtimeMetrics.inputBaseline = input.DiagnosticsSnapshot();
			window.canvasContactDown = [] { Inkeys::UI::PageControl::RequestWhiteboardMenuDismiss(); };
			window.SetInputCoordinator(&input);
			window.SetSpeedEraserDisplayScale({});
			appliedDisplayClientBounds = {};
			penWidthContextPrepared = false;
			PublishDisplaySnapshot(Inkeys::Display::GetSnapshot());
			// 启动握手保证绘制线程先拥有独立 GPU 资源，再启用唯一 RTS producer。
			{
				std::scoped_lock lock(startupMutex);
				graphicsReady = false;
				inputDecision = false;
				inputSucceeded = false;
				startupCompleted = false;
				startupSucceeded = false;
			}
			hapticClicks.Start();
			running.store(true, std::memory_order_release);
			auto runDrawing = [this, failedCleanup](std::stop_token token)
			{
				// COM 的创建、调用和析构都留在此 owner；异常/首帧失败同样配对清理。
				PenHapticFeedback haptics(false);
				const bool hapticReady = haptics.Initialize();
				HapticDiagnostics::Log("host", "backend_initialized=%d run=%llu", hapticReady, static_cast<unsigned long long>(hapticClicks.Run()));
				bool initialized = false;
				bool graphicsInitialized = false;
				const char* startupStage = "InitializeGraphicsDevice";
					DiagnosticOutput::Line() << "[Draw3Diag][startup] stage=begin hwnd=0x" << std::hex
						<< reinterpret_cast<UINT_PTR>(attachedWindow.load(std::memory_order_acquire))
						<< " selectionHwnd=0x"
						<< reinterpret_cast<UINT_PTR>(attachedPresentationWindow.load(std::memory_order_acquire))
						<< std::dec << std::endl;
				try
				{
					const HWND windowHandle = attachedWindow.load(std::memory_order_acquire);
					const HWND presentationWindowHandle =
						attachedPresentationWindow.load(std::memory_order_acquire);
					graphicsInitialized = windowHandle && InitializeGraphicsDevice(graphics,
						startOptions.enableHardwareAcceleration, startOptions.allowWarpFallback,
						static_cast<GraphicsAdapterPreference>(startOptions.adapterPreference));
					DiagnosticOutput::Line() << "[Draw3Diag][startup] stage=graphics result="
						<< (graphicsInitialized ? "ready" : "failed")
						<< " hwnd=0x" << std::hex << reinterpret_cast<UINT_PTR>(windowHandle)
						<< " selectionHwnd=0x" << reinterpret_cast<UINT_PTR>(presentationWindowHandle)
						<< std::dec << " driver=" << (graphics.driverType == D3D_DRIVER_TYPE_WARP ? "WARP" : "Hardware")
						<< " featureLevel=0x" << std::hex << static_cast<unsigned>(graphics.featureLevel)
						<< std::dec << std::endl;
					// 已知失败后先激活；普通设备初始化尚未返回时不设置冷启动上限。
					if (!graphicsInitialized)
					{
						failedCleanup.BeginKnownFailure();
						const std::string failure = "图形设备：创建失败\n";
						UpdateCanvasInformation(nullptr, &failure);
					}
					if (graphicsInitialized && startOptions.startupMilestone)
						startOptions.startupMilestone(startOptions.startupContext,
							HostStartupStage::GraphicsReady);
					if (graphicsInitialized)
					{
						const WindowSize size = window.Size();
						committedWidth.store(size.width, std::memory_order_release);
						committedHeight.store(size.height, std::memory_order_release);
						const TransparentPresentationCallbacks presentationCallbacks{
							&this->styleCallbacks, &ApplyStyle, this, &ObserveCanvasGraphics, &ObserveCanvasFallback
						};
						TransparentPresentationOptions presentationOptions{};
						presentationOptions.requireMode =
							IsRequiredMode(startOptions.requiredPresentationMode);
						presentationOptions.requiredMode =
							ToTransparentMode(startOptions.requiredPresentationMode);
						presentationOptions.allowDirectComposition =
							startOptions.allowDirectComposition;
						if (IsRequiredMode(startOptions.preferredPresentationMode))
							presentationOptions.preferredMode = ToTransparentMode(startOptions.preferredPresentationMode);
						presentationOptions.previousAttempt = startOptions.previousAttempt;
						presentationOptions.failedCleanup = failedCleanup;
						presentationOptions.enableHardwareAcceleration = startOptions.enableHardwareAcceleration;
						presentationOptions.allowWarpFallback = startOptions.allowWarpFallback;
						presentationOptions.adapterPreference =
							static_cast<GraphicsAdapterPreference>(startOptions.adapterPreference);
						presentationOptions.enableZhaoxinCompatibility = startOptions.enableZhaoxinCompatibility;
						presentationOptions.enableWaitableWait = startOptions.enableWaitableWait;
						presentationOptions.enableUlwAsyncReadback = startOptions.enableUlwAsyncReadback;
						presentationOptions.enableUlwCompositionAlign = startOptions.enableUlwCompositionAlign;
						presentationOptions.enableDcompCompositionAlign = startOptions.enableDcompCompositionAlign;
						presentationOptions.enableWin7DwmCompositionAlign = startOptions.enableWin7DwmCompositionAlign;
						startupStage = "TransparentPresentationController::Initialize";
						graphicsInitialized = presentation.Initialize(windowHandle,
							presentationWindowHandle, graphics, renderer,
							static_cast<UINT>((std::max)(1, size.width)),
							static_cast<UINT>((std::max)(1, size.height)), presentationCallbacks,
							presentationOptions);
						appliedUlwMultiStepCommitEnabled.reset();
						DiagnosticOutput::Line() << "[Draw3Diag][startup] stage=presenter result="
							<< (graphicsInitialized ? "ready" : "failed")
							<< " mode=" << TransparentPresentModeName(presentation.ActiveMode())
							<< " renderer=0x" << std::hex << reinterpret_cast<UINT_PTR>(&renderer)
							<< std::dec << " size=" << size.width << "x" << size.height << std::endl;
						if (!graphicsInitialized) failedCleanup.BeginKnownFailure();
						if (graphicsInitialized)
						{
							if (startOptions.startupMilestone)
								startOptions.startupMilestone(startOptions.startupContext,
									HostStartupStage::PresenterReady);
							presentationMode.store(ToHostMode(presentation.ActiveMode()),
								std::memory_order_release);
							window.SetGpuTransparentComposition(
								presentation.IsGpuTransparentComposition());
							CaptureRuntimeMetricsMetadata(true);
						}
					}

					// 输入准入前由绘制 owner 同步解析目标并消费产品样式，首笔无异步纠正。
					if (graphicsInitialized) PumpBridgeState();
					{
						std::scoped_lock lock(startupMutex);
						graphicsReady = graphicsInitialized;
					}
					startupCondition.notify_all();
					if (graphicsInitialized)
					{
						startupStage = "Draw3 input startup handshake";
						// 输入必须在图形资源准备好后才启用，避免 producer 先于 renderer 存活。
						bool inputApproved = false;
						bool inputDecisionValue = false;
						bool inputSucceededValue = false;
						{
							std::unique_lock lock(startupMutex);
							DiagnosticOutput::Line() << "[Draw3Diag][startup] gate=input-ready state=waiting" << std::endl;
							startupCondition.wait(lock, [this, &token]
								{ return inputDecision || token.stop_requested(); });
							inputDecisionValue = inputDecision;
							inputSucceededValue = inputSucceeded;
							inputApproved = inputDecision && inputSucceeded && !token.stop_requested();
						}
						DiagnosticOutput::Line() << "[Draw3Diag][startup] gate=input-ready result="
							<< (inputApproved ? "open" : "closed")
							<< " decision=" << (inputDecisionValue ? 1 : 0)
							<< " input=" << (inputSucceededValue ? 1 : 0)
							<< " stopped=" << (token.stop_requested() ? 1 : 0) << std::endl;
						if (inputApproved)
						{
							startupStage = "DrawingController initialization";
							const auto timingProfile = startOptions.frameRate == 30 ? StrokeTimingProfileId::Fps30
								: startOptions.frameRate == 60 ? StrokeTimingProfileId::Fps60
								: startOptions.frameRate == 240 ? StrokeTimingProfileId::Fps240 : StrokeTimingProfileId::Fps120;
							StrokeModelConfiguration configuration =
								CreateStrokeModelConfiguration(QueryCompatibleWindowDpi(windowHandle), timingProfile);
							configuration.interruptedStrokeReconnectEnabled =
								interruptedStrokeReconnectEnabled.load(std::memory_order_acquire);
							window.SetEraserDiagnosticsEnabled(startOptions.enableEraserDiagnostics ||
								startOptions.enableHiddenTestContactInjection);
							StartDesktopSnapshots(configuration.dpiScale);
							presentationStoreDpiScale = configuration.dpiScale;
							DrawingControllerRuntimeObserver observer{
								this, &ObservePresented, &ObserveResized,
								&ObserveCommand, &ObserveDocument,
								&ObserveCurrentPageContent, &ObserveStrokeCompleted,
								&ObserveWorkspace, &ConsumeBridge,
								&ObserveDesktopAutoSave,
								&ObserveDesktopLoad,
								&ObservePresentationStore,
								&ObservePresentationDrain,
								&ObserveDrawingActivity,
								&ObserveEraserDiagnostics,
								startOptions.enableHiddenTestContactInjection ? &ObservePenDiagnostics : nullptr
							};
							observer.historyAvailabilityChanged = &ObserveHistoryAvailability;
							observer.laserVisibilityChanged = &ObserveLaserVisibility;
							observer.whiteboardChanged = &ObserveWhiteboard;
							observer.whiteboardExitSaved = &ObserveWhiteboardExit;
							observer.whiteboardRoot = startOptions.autoSaveRoot;
							if (recoverySession.Active())
							{
								observer.takeDesktopRecovery = &TakeDesktopRecovery;
								observer.takeWhiteboardRecovery = &TakeWhiteboardRecovery;
								observer.whiteboardRecoveryInstalled = &WhiteboardRecoveryInstalled;
								observer.desktopRecoveryInstalled = &DesktopRecoveryInstalled;
								observer.recoveryDurable = &RecoveryDurable;
								observer.recoveryInkVisible = &RecoveryInkVisible;
								observer.whiteboardRecoveryContext = &whiteboardRecovery;
								observer.desktopRecoveryContext = &desktopRecovery;
								observer.registerRecoveryFile = &RecoveryRegisterFile;
								observer.registerRecoveryPending = &RecoveryRegisterPending;
								observer.clearRecoveryPending = &RecoveryClearPending;
								observer.clearRecoveryFile = &RecoveryClearFile;
								observer.recoverySceneChanged = &RecoverySceneChanged;
								observer.recoveryContext = this;
							}
							observer.whiteboardDefaultTheme = startOptions.whiteboardDefaultTheme;
							observer.whiteboardThemeEdited = &ObserveWhiteboardThemeEdited;
							observer.whiteboardViewportChanged = &ObserveWhiteboardViewport;
							observer.acceptedStrokeBegan = &ObserveAcceptedStroke;
							observer.acceptedStrokeSceneChanged = &ObserveAcceptedStrokeScene;
							observer.shortcutContextChanged = &ObserveShortcutContext;
							observer.shortcutCommandAllowed = &AllowShortcutCommand;
							observer.shortcutCompleted = &CompleteShortcut;
							observer.recoveryChanged = &ObserveRecovery;
							observer.recoveryRetryAllowed = &RecoveryRetryAllowed;
							drawing = std::make_unique<DrawingController>(input, window, renderer,
								presentation, configuration, observer, runtimeMetrics.session.get(), &haptics, &hapticClicks,
								&devicePerformance);
							// 新 Controller 必须重新消费期望值，不能沿用上一代的已应用缓存。
							appliedGeneralDrawingOptions = appliedLaserOptions = appliedCanvasPanInertiaOptions = appliedMultiTouchTools = UINT32_MAX;
							appliedLiveTaperOptions.reset();
							appliedPalmEraserOptions.reset();
							appliedCanvasDebugOptions = UINT32_MAX;
							appliedInterruptedStrokeReconnectDevices = UINT32_MAX;
							appliedCanvasDebugInputMode = UINT32_MAX;
							// 创建前已消费过 Bridge revision，首帧前显式补齐新 Controller 的光标偏好。
							drawing->SetCursorSettings(bridge.Snapshot().cursorSettings);
							const auto eraser = window.EraserInputsSnapshot();
							drawing->SetEraserCursorAppearance({ eraser.shape, eraser.cursorPattern, eraser.cursorTheme });
							DiagnosticOutput::Line() << "[Draw3Diag][startup] stage=controller result=ready renderer=0x"
								<< std::hex << reinterpret_cast<UINT_PTR>(&renderer)
								<< std::dec << std::endl;
							if (startOptions.startupMilestone)
								startOptions.startupMilestone(startOptions.startupContext,
									HostStartupStage::ControllerReady);
							startupStage = "Draw3 first frame";
							// 首帧清屏和所有 Renderer 访问均发生在 Draw3 绘制线程。
							PumpBridgeState();
							presentation.SetOutputTarget(window.SelectionMode() &&
								PresentModeUsesAuxiliarySelection(presentation.ActiveMode())
								? TransparentOutputTarget::SelectionUlw
								: TransparentOutputTarget::PrimaryDrawpad);
							DiagnosticOutput::Line() << "[Draw3Diag][startup] stage=first-frame attempt target="
								<< HostOutputTargetName(ToHostOutputTarget(presentation.RequestedOutputTarget()))
								<< " revision=" << presentation.RequestedOutputRevision()
								<< " size=" << committedWidth.load(std::memory_order_acquire)
								<< "x" << committedHeight.load(std::memory_order_acquire) << std::endl;
							drawing->ClearCanvas();
							if (runtimeMetrics.session)
							{
								// Controller 旁挂失败会关闭真实计数；不把空 Session 导出成正常 run。
								const auto metrics = runtimeMetrics.session->Snapshot();
								if (metrics.frameSerial == 0 || metrics.presentAttempts == 0)
									runtimeMetrics.unavailable.store(true, std::memory_order_release);
							}
							initialized = lastPresentSucceeded.load(std::memory_order_acquire);
							DiagnosticOutput::Line() << "[Draw3Diag][startup] stage=first-frame result="
								<< (initialized ? "committed" : "failed")
								<< " target=" << HostOutputTargetName(
									ToHostOutputTarget(presentation.RequestedOutputTarget()))
								<< " revision=" << presentation.RequestedOutputRevision()
								<< " size=" << committedWidth.load(std::memory_order_acquire)
								<< "x" << committedHeight.load(std::memory_order_acquire) << std::endl;
							LogDraw3WindowState("first-frame", windowHandle, presentationWindowHandle);
							if (!initialized) failedCleanup.BeginKnownFailure();
							firstFrameReady.store(initialized, std::memory_order_release);
							if (initialized && startOptions.startupMilestone)
								startOptions.startupMilestone(startOptions.startupContext,
									HostStartupStage::FirstFrameCommitted);
						}
					}
				}
				catch (const std::exception& exception)
				{
					failedCleanup.BeginKnownFailure();
					RecordHostStartupFailure(startupStage, exception.what());
					// 初始化失败也必须完成握手；保留异常原因供隐藏测试和现场诊断。
					DiagnosticOutput::Printf(DiagnosticOutput::ErrError, "[Draw3] startup failed: %s\n", exception.what());
					initialized = false;
				}
				catch (...)
				{
					failedCleanup.BeginKnownFailure();
					DiagnosticOutput::Text(DiagnosticOutput::ErrError, "[Draw3] startup failed: unknown exception\n");
					initialized = false;
				}
				if (!initialized) RecordHostStartupFailure(startupStage);
				UpdateCanvasInformation(initialized ? "就绪" : "初始化失败");
				// Signal 由此 owner 按值持有，覆盖失败握手与随后全部 GPU 释放。
				if (!initialized) failedCleanup.BeginKnownFailure();
				if (!initialized && runtimeMetrics.enabled.load(std::memory_order_acquire))
					runtimeMetrics.startupFailed.store(true, std::memory_order_release);
				{
					std::scoped_lock lock(startupMutex);
					startupSucceeded = initialized;
					startupCompleted = true;
				}
				startupCondition.notify_all();
				if (initialized && drawing && !token.stop_requested())
				{
					try
					{
						drawing->Run();
					}
					catch (const std::exception& exception)
					{
						runtimeMetrics.runFailed.store(true, std::memory_order_release);
						// 绘制循环异常必须收敛到宿主停止，不能穿出 jthread 触发 terminate。
						DiagnosticOutput::Printf(DiagnosticOutput::ErrError, "[Draw3] drawing loop stopped: %s\n",
							exception.what());
						window.RequestExit();
					}
					catch (...)
					{
						runtimeMetrics.runFailed.store(true, std::memory_order_release);
						DiagnosticOutput::Text(DiagnosticOutput::ErrError, "[Draw3] drawing loop stopped: unknown exception\n");
						window.RequestExit();
					}
				}
				// Run 返回后的宿主清理仍算 Draw3 槽的工作单元；清理卡住超过 15 秒由教学安全模式兜底。
				if (initialized) Inkeys::Fatal::Beat(Inkeys::Fatal::Slot::Draw3Loop);
				if (initialized) UpdateCanvasInformation(runtimeMetrics.runFailed.load(std::memory_order_acquire)
					? "绘制异常，已停止" : "已停止");
				// Run 正常、异常或 stop 返回时都补齐活动结束，再释放 controller。
				EndDrawingActivity();
				CaptureRuntimeMetricsMetadata(true);
				// GPU 资源在拥有它们的绘制线程释放，避免跨线程访问 Renderer。
				if (drawing) drawing.reset();
				hapticClicks.Stop();
				haptics.Shutdown();
				presentation.Shutdown();
				auxiliaryReadbackPayloadBytes.store(0, std::memory_order_release);
				renderer.ReleaseResources();
				graphics = {};
				running.store(false, std::memory_order_release);
				shortcuts.Stop();
				contentCondition.notify_all();
				exitAutoSaveCondition.notify_all();
				runtimeRevision.NotifyAll();
			};
			try
			{
				drawingThread = std::jthread(std::move(runDrawing));
				runtimeMetrics.ownerCreated = true;
			}
			catch (...)
			{
				RecordHostStartupFailure("Draw3 drawing thread creation");
				UpdateCanvasInformation("初始化失败（无法创建绘制线程）");
				// owner 创建失败也先监督，再排空已建立的 worker 并解除外部窗口附着。
				failedCleanup.BeginKnownFailure();
				running.store(false, std::memory_order_release);
				shortcuts.Stop();
				desktopSnapshots.Close();
				SettleDesktopCompletions();
				presentationStores.Close();
				presentationStores.Clear();
				devicePerformance.Stop();
				EndDrawingActivity();
				window.SetInputCoordinator(nullptr);
				window.DetachExternal();
				pendingPresentationCompletion.reset();
				attachedWindow.store(nullptr, std::memory_order_release);
				attachedPresentationWindow.store(nullptr, std::memory_order_release);
				hiddenTestContactInjectionEnabled = false;
				firstFrameReady.store(false, std::memory_order_release);
				runtimeCallbacks = {};
				SealRuntimeMetricsRun(true);
				return false;
			}

		std::unique_lock lock(startupMutex);
		startupCondition.wait(lock, [this] { return graphicsReady || startupCompleted; });
		if (!graphicsReady)
		{
			failedCleanup.BeginKnownFailure();
			lock.unlock();
			if (drawingThread.joinable()) drawingThread.join();
			desktopSnapshots.Close();
			SettleDesktopCompletions();
			presentationStores.Close();
			presentationStores.Clear();
			devicePerformance.Stop();
			EndDrawingActivity();
			window.SetInputCoordinator(nullptr);
			window.DetachExternal();
			pendingPresentationCompletion.reset();
			attachedWindow.store(nullptr, std::memory_order_release);
			attachedPresentationWindow.store(nullptr, std::memory_order_release);
			hiddenTestContactInjectionEnabled = false;
			firstFrameReady.store(false, std::memory_order_release);
			runtimeCallbacks = {};
			SealRuntimeMetricsRun(true);
			return false;
		}

		lock.unlock();
		bool inputInitialized = false;
		try
		{
			bool triedRts = false;
			const auto addFallback = [&](InputFallback reason) noexcept
			{
				run.fallback = static_cast<InputFallback>(static_cast<unsigned>(run.fallback) |
					static_cast<unsigned>(reason));
			};
			run.actual = StartInputAndNavigation(version.majorVersion, version.minorVersion, version.buildNumber,
				run.touchpad, run.resolved, [&]
			{
				if (window.StartPointerInput()) return true;
				// 窗口 owner 必须确认 Pointer 和配套鼠标已停产，再开放 RTS。
				if (!window.StopPointerInput())
				{
					failedCleanup.BeginKnownFailure();
					failedCleanup.FailUnprovenProducerStop();
				}
				addFallback(InputFallback::PointerInitFailed);
				return false;
			}, [&]
			{
				triedRts = true;
				if (!stylus.Initialize(hwnd, input, &window, failedCleanup))
				{
					addFallback(InputFallback::RtsFailed);
					return HostInputMode::Disabled;
				}
				const bool basic = stylus.UsesBasicPacketDescription();
				if (basic) addFallback(InputFallback::RtsExtendedFailed);
				stylus.EnableContactPublication();
				return basic ? HostInputMode::RtsBasicXY : HostInputMode::RtsExtended;
			}, [&]
			{
				if (window.SetMouseCompatibilityEnabled(true))
				{
					if (triedRts) DiagnosticOutput::Text(DiagnosticOutput::Stream::Error,
						"[Draw3] RTS unavailable; Win32 mouse compatibility input enabled\n");
					return true;
				}
				// 手选鼠标兼容失败后还会继续尝试 RTS/Pointer，此处不能提前启动失败截止或记录启动失败；
				// 全部生产者都失败时由下方统一记录并 BeginKnownFailure。
				if (!window.SetMouseCompatibilityEnabled(false))
				{
					failedCleanup.BeginKnownFailure();
					failedCleanup.FailUnprovenProducerStop();
				}
				addFallback(InputFallback::MouseFailed);
				return false;
			}, [&](TouchpadEntry entry)
			{
				const auto result = window.StartTouchpadNavigation(entry);
				// 超时可能发生在 owner 调用中，须确认残留入口已退休后才继续使用绘制生产者。
				if (result != TouchpadAvailability::Ok && !window.StopTouchpadNavigation())
				{
					failedCleanup.BeginKnownFailure();
					failedCleanup.FailUnprovenProducerStop();
				}
				return result;
			});
			inputInitialized = run.actual != HostInputMode::Disabled;
			inputRun.store(run, std::memory_order_release);
			CaptureCanvasInputInformation(run.actual, triedRts);
			const char* modeName = run.actual == HostInputMode::RtsExtended ? "RtsExtended" :
				run.actual == HostInputMode::RtsBasicXY ? "RtsBasicXY" :
				run.actual == HostInputMode::Pointer ? "Pointer" :
				run.actual == HostInputMode::Win32Mouse ? "Win32Mouse" : "Disabled";
			DiagnosticOutput::Line(DiagnosticOutput::OutInfo) << "[Draw3] Input initialization result="
				<< (inputInitialized ? "ready" : "failed") << " input_mode=" << modeName
				<< " preference=" << InputPreferenceName(run.preference)
				<< " large_screen=" << run.largeScreen << " os=" << version.majorVersion << '.'
				<< version.minorVersion << '.' << version.buildNumber << " resolved=" << InputPreferenceName(run.resolved)
				<< " fallback="
				<< (run.fallback == InputFallback::None ? "none" : "")
				<< (HasInputFallback(run.fallback, InputFallback::PointerUnavailableOnOs) ? "[PointerUnavailableOnOs]" : "")
				<< (HasInputFallback(run.fallback, InputFallback::PointerInitFailed) ? "[PointerInitFailed]" : "")
				<< (HasInputFallback(run.fallback, InputFallback::RtsExtendedFailed) ? "[RtsExtendedFailed]" : "")
				<< (HasInputFallback(run.fallback, InputFallback::RtsFailed) ? "[RtsFailed]" : "")
				<< (HasInputFallback(run.fallback, InputFallback::MouseFailed) ? "[MouseFailed]" : "")
				<< " touchpad=" << static_cast<unsigned>(run.touchpad) << std::endl;
			if (!inputInitialized)
			{
				RecordHostStartupFailure("Draw3 input initialization (all backends failed)");
				failedCleanup.BeginKnownFailure();
			}
			if (inputInitialized && startOptions.startupMilestone)
				startOptions.startupMilestone(startOptions.startupContext,
					HostStartupStage::InputReady);
		}
		catch (...)
		{
			failedCleanup.BeginKnownFailure();
			// 意外异常不降级；仍完成握手，避免绘制 owner 永久等待。
			inputInitialized = false;
			RecordHostStartupFailure("Draw3 input initialization exception");
			UpdateCanvasInformation("初始化失败（输入初始化异常）");
		}
		{
			std::scoped_lock startupLock(startupMutex);
			inputSucceeded = inputInitialized;
			inputDecision = true;
		}
		startupCondition.notify_all();

		lock.lock();
		startupCondition.wait(lock, [this] { return startupCompleted; });
		const bool startupSucceededValue = startupSucceeded;
		lock.unlock();
		if (!startupSucceededValue)
		{
			failedCleanup.BeginKnownFailure();
			// 失败时先确认最终输入 producer 停止，再让绘制线程退出并释放 GPU 资源。
			StopInputProducer();
			window.RequestExit();
			input.PublishControlWake();
			if (drawingThread.joinable()) drawingThread.request_stop();
			startupCondition.notify_all();
			if (drawingThread.joinable()) drawingThread.join();
			desktopSnapshots.Close();
			SettleDesktopCompletions();
			presentationStores.Close();
			presentationStores.Clear();
			devicePerformance.Stop();
			EndDrawingActivity();
			window.SetInputCoordinator(nullptr);
			window.DetachExternal();
			pendingPresentationCompletion.reset();
			attachedWindow.store(nullptr, std::memory_order_release);
			attachedPresentationWindow.store(nullptr, std::memory_order_release);
			hiddenTestContactInjectionEnabled = false;
			firstFrameReady.store(false, std::memory_order_release);
			runtimeCallbacks = {};
			SealRuntimeMetricsRun(true);
			return false;
		}
		displaySubscription = Inkeys::Display::Subscribe(
			[this](Inkeys::Display::SnapshotPtr snapshot)
			{
				// 通知线程只发布快照并唤醒，窗口信息由绘制线程低频解析。
				PublishDisplaySnapshot(std::move(snapshot));
			});
		// Subscribe 仍可能抛出；只有 caller 完整 Start 成功后才清失败标记。
		if (runtimeMetrics.enabled.load(std::memory_order_acquire))
			runtimeMetrics.startupFailed.store(false, std::memory_order_release);
		// 启动握手成功后才认领旧会话，失败回退到 ULW 时没有恢复线程或认领锁残留。
		if (scanRecovery)
		{
			// DPI 按值传入，与绘制线程启动时采用相同的窗口和算法。
			const float scanDpiScale = (std::max)(static_cast<float>(QueryCompatibleWindowDpi(hwnd)) / 96.0f, 0.1f);
			try { recoveryThread = std::thread(&Impl::RunRecoveryScan, this, options.autoSaveRoot, options, scanDpiScale); }
			catch (...)
			{
				{
					const std::scoped_lock lock(recoveryMutex);
					presentationScanDone = true;
				}
				recoveryCondition.notify_all();
			}
		}
		return true;
		}

		void CloseInputAdmission() noexcept
		{
			window.CloseMouseInputAdmission();
			{
				std::scoped_lock lock(presentationTargetMutex);
				inputStopping.store(true, std::memory_order_release);
				workspaceTransitionEpoch = 0;
				workspaceTransitionPending = false;
				RefreshPresentationInputGateLocked();
			}
		}

		void StopInputProducer() noexcept
		{
			CloseInputAdmission();
			// DM 对象与回调由窗口 owner 释放；未确认停产时不能继续拆除 HWND。
			if (!window.StopTouchpadNavigation())
			{
				startOptions.failedCleanup.BeginKnownFailure();
				startOptions.failedCleanup.FailUnprovenProducerStop();
			}
			if (!window.SetMouseCompatibilityEnabled(false))
			{
				startOptions.failedCleanup.BeginKnownFailure();
				startOptions.failedCleanup.FailUnprovenProducerStop();
			}
			stylus.Shutdown();
			auto stopped = inputRun.load(std::memory_order_acquire);
			stopped.actual = HostInputMode::Disabled;
			stopped.touchpad = TouchpadAvailability::BackendDisabled;
			inputRun.store(stopped, std::memory_order_release);
		}

		void CancelInkRecoveryPrompts() noexcept
		{
			// 主线程在报错或退出时可先关准入，绘制线程仍可在退出屏障期间报告落盘结果。
			{
				// 与 PPT 等待使用同一把锁，避免退出通知在进入等待前丢失。
				const std::scoped_lock lock(recoveryMutex);
				recoveryClosing.store(true, std::memory_order_release);
			}
			recoveryCondition.notify_all();
			recoveryPromptStop.request_stop();
		}

		void Stop() noexcept
		{
			CancelInkRecoveryPrompts();
			hapticClicks.Stop();
			displaySubscription.Reset(); // 等待回调退出后再拆除输入与窗口。
			if (!attachedWindow.load(std::memory_order_acquire) &&
				!attachedPresentationWindow.load(std::memory_order_acquire) &&
				!running.load(std::memory_order_acquire) && !drawingThread.joinable())
			{
				desktopSnapshots.Close();
				SettleDesktopCompletions();
				presentationStores.Close();
				presentationStores.Clear();
				devicePerformance.Stop();
				EndDrawingActivity();
				runtimeCallbacks = {};
				SealRuntimeMetricsRun();
				return;
			}
			if (running.load(std::memory_order_acquire)) UpdateCanvasInformation("正在停止");
			// 先关闭 contact 准入，仍保留业务命令通道供下面发布最终保存屏障。
			CloseInputAdmission();
			{
				std::scoped_lock lock(exitAutoSaveMutex);
				exitAutoSavePrepared = false;
			}
			// 关闭产品命令生产端，并把退出保存屏障排在所有已接受命令之后。
			bool exitBarrierQueued = false;
			{
				std::scoped_lock lock(commandPublishMutex);
				if (input.TryReserveCommandWake())
				{
					exitBarrierQueued = bridge.StopWithFinalCommand(
						Bridge::CommandType::PrepareExitAutoSave);
					if (!exitBarrierQueued) input.CancelReservedCommandWake();
				}
				else bridge.Stop(); // 预约失败不可接受一个无法送达的最终屏障。
			}
			// 停止选定 producer 并确认 owner drain 后，才发布最终保存屏障。
			StopInputProducer();
			if (exitBarrierQueued) input.PublishReservedCommandWake();
			else input.PublishControlWake();
			if (exitBarrierQueued)
			{
				std::unique_lock lock(exitAutoSaveMutex);
				exitAutoSaveCondition.wait(lock, [this]
					{
						return exitAutoSavePrepared ||
							!running.load(std::memory_order_acquire);
					});
				if (exitAutoSavePrepared && !whiteboardExitDurable.load(std::memory_order_acquire))
					DiagnosticOutput::Text(DiagnosticOutput::ErrError, "[Draw3.DesktopInkSnapshots] action=exit_barrier result=failed reason=whiteboard_not_durable\n");
				if (!exitAutoSavePrepared)
					DiagnosticOutput::Text(DiagnosticOutput::ErrError, "[Draw3.DesktopInkSnapshots] action=exit_barrier result=failed reason=controller_stopped\n");
			}
			else
				DiagnosticOutput::Text(DiagnosticOutput::ErrError, "[Draw3.DesktopInkSnapshots] action=exit_barrier result=failed reason=bridge_closed\n");
			// worker 无超时排空后才允许绘制线程销毁 Document/Controller。
			desktopSnapshots.Close();
			presentationStores.Close();
			window.RequestExit();
			input.PublishControlWake();
			if (drawingThread.joinable()) drawingThread.request_stop();
			startupCondition.notify_all();
			if (drawingThread.joinable()) drawingThread.join();
			SettleDesktopCompletions();
			// owner 退出后才能释放各槽借用的服务。
			presentationStores.Clear();
			// 恢复线程可能还在等弹窗或装入结果：通知退出后等待（弹窗本身由 MessageBox 自行收口）。
			// 绘制线程退出前已按各场景的实际回执报告落盘结果（见 settleRecoveryDurable 的退出调用）。
			{
				// 与等待谓词使用同一把锁，避免退出通知跑在恢复线程真正入等之前。
				const std::scoped_lock lock(recoveryMutex);
				recoveryStop.store(true, std::memory_order_release);
			}
			recoveryCondition.notify_all();
			if (recoveryThread.joinable()) recoveryThread.join();
			// 三个存储服务都已排空关闭（白板随绘制线程退出）：写 clean 后释放存活锁。
			// 退出屏障未完成时不写 clean，下次启动按非正常结束处理。
			recoverySession.Stop(exitAutoSavePrepared);
			// 绘制线程已退出（Run 退出前已移交最后一段）：账本立即落盘并停止写盘线程。
			devicePerformance.Stop();
			EndDrawingActivity();
			window.SetInputCoordinator(nullptr);
			window.DetachExternal();
			pendingPresentationCompletion.reset();
			attachedWindow.store(nullptr, std::memory_order_release);
			attachedPresentationWindow.store(nullptr, std::memory_order_release);
			hiddenTestContactInjectionEnabled = false;
			firstFrameReady.store(false, std::memory_order_release);
			running.store(false, std::memory_order_release);
			shortcuts.Stop();
			contentCondition.notify_all();
			runtimeRevision.NotifyAll();
			runtimeCallbacks = {};
			SealRuntimeMetricsRun();
		}
	};

	Host::Host() : impl_(std::make_unique<Impl>()) {}
	Host::~Host() { Stop(); }
	bool Host::Start(HWND drawpad, HWND drawpadPresentation,
		HostStyleCallbacks callbacks, HostStartOptions options,
		HostRuntimeCallbacks runtimeCallbacks)
	{
		return impl_->Start(drawpad, drawpadPresentation, callbacks, options,
			runtimeCallbacks);
	}
	void Host::Stop() noexcept { impl_->Stop(); }
	void Host::CancelInkRecoveryPrompts() noexcept { impl_->CancelInkRecoveryPrompts(); }
	bool Host::WriteRuntimeMetrics(const wchar_t* absoluteOutputPath) const noexcept
	{
		return impl_->WriteRuntimeMetrics(absoluteOutputPath);
	}
	bool Host::Running() const noexcept { return impl_->running.load(std::memory_order_acquire); }
	std::string Host::CanvasInformation(PresentationFallbackReport* fallback) const
	{
		std::scoped_lock lock(impl_->canvasInformationMutex);
		if (fallback) *fallback = impl_->canvasFallback;
		return impl_->canvasInformation;
	}

	PresentationFallbackReport Host::CanvasFallbackReport() const
	{
		std::scoped_lock lock(impl_->canvasInformationMutex);
		return impl_->canvasFallback;
	}
	std::optional<HostHiddenPersistenceSnapshot> Host::HiddenPersistenceSnapshot() const noexcept
	{
		// startOptions 在当前 generation 不变，Stop 不改写此字段；禁止并行下一 Start。
		if (!impl_->startOptions.enableHiddenTestContactInjection) return std::nullopt;
		const auto desktop = impl_->desktopSnapshots.Diagnostics();
		const std::scoped_lock lock(impl_->hiddenPersistenceMutex);
		auto snapshot = impl_->hiddenPersistence.counts;
		snapshot.desktopAccepted = desktop.accepted;
		snapshot.desktopCommitted = desktop.committed;
		snapshot.desktopFailed = desktop.failed;
		snapshot.desktopPending = desktop.pending;
		return snapshot;
	}

	bool Host::FirstFrameReady() const noexcept
	{
		return impl_->firstFrameReady.load(std::memory_order_acquire);
	}
	HostRuntimeSnapshot Host::RuntimeSnapshot() const noexcept
	{
		HostRuntimeSnapshot snapshot;
		// 等待基准先取：后续字段读取期间的新发布必须使 WaitForChange 立即返回。
		snapshot.runtimeRevision = impl_->runtimeRevision.Revision();
		{std::scoped_lock lock(impl_->eraserDiagnosticsMutex);snapshot.eraser=impl_->eraserDiagnostics;snapshot.pen=impl_->penDiagnostics;}
		snapshot.touchContactAreaAssistanceEnabled=impl_->window.TouchContactAreaAssistance();
		snapshot.runtimeMetrics = impl_->CopyRuntimeMetricsSnapshot();
		snapshot.running = impl_->running.load(std::memory_order_acquire);
		snapshot.firstFrameReady = impl_->firstFrameReady.load(std::memory_order_acquire);
		snapshot.lastPresentSucceeded =
			impl_->lastPresentSucceeded.load(std::memory_order_acquire);
		snapshot.presentationMode = impl_->presentationMode.load(std::memory_order_acquire);
		const auto run = impl_->inputRun.load(std::memory_order_acquire);
		snapshot.inputPreference = run.preference;
		snapshot.largeScreenForcedRts = run.largeScreen && run.preference == InputPreference::Automatic;
		snapshot.resolvedInputBackend = run.resolved;
		snapshot.inputMode = run.actual;
		snapshot.inputFallback = run.fallback;
		snapshot.touchpadGestureAvailable = run.touchpad == TouchpadAvailability::Ok;
		snapshot.touchpadGestureAvailability = run.touchpad;
		snapshot.presentCount = impl_->presentCount.load(std::memory_order_acquire);
		snapshot.successfulPresentCount =
			impl_->successfulPresentCount.load(std::memory_order_acquire);
		snapshot.partialPresentCount =
			impl_->partialPresentCount.load(std::memory_order_acquire);
		snapshot.resizeCount = impl_->resizeCount.load(std::memory_order_acquire);
		snapshot.clearCommandCount =
			impl_->clearCommandCount.load(std::memory_order_acquire);
		snapshot.undoCommandCount = impl_->undoCommandCount.load(std::memory_order_acquire);
		snapshot.redoCommandCount = impl_->redoCommandCount.load(std::memory_order_acquire);
		snapshot.nextPageCommandCount =
			impl_->nextPageCommandCount.load(std::memory_order_acquire);
		snapshot.previousPageCommandCount =
			impl_->previousPageCommandCount.load(std::memory_order_acquire);
		const ContactInputDiagnosticsSnapshot inputDiagnostics =
			impl_->input.DiagnosticsSnapshot();
		snapshot.inputDownPublished = inputDiagnostics.downPublished;
		snapshot.inputMovePublished = inputDiagnostics.movePublished;
		snapshot.inputTerminalPublished = inputDiagnostics.terminalPublished;
		snapshot.inputRecycled = inputDiagnostics.recycled;
		snapshot.ulwDirtyRectPresentCount =
			impl_->ulwDirtyRectPresentCount.load(std::memory_order_acquire);
		snapshot.ulwPremultipliedAlphaFailureCount =
			impl_->ulwPremultipliedAlphaFailureCount.load(std::memory_order_acquire);
		snapshot.ulwTransparentFullFrameVerified =
			impl_->ulwTransparentFullFrameVerified.load(std::memory_order_acquire);
		snapshot.committedWidth = impl_->committedWidth.load(std::memory_order_acquire);
		snapshot.committedHeight = impl_->committedHeight.load(std::memory_order_acquire);
		snapshot.currentPageIndex = impl_->currentPageIndex.load(std::memory_order_acquire);
		snapshot.pageCount = impl_->pageCount.load(std::memory_order_acquire);
		snapshot.contentRevision = impl_->contentRevision.load(std::memory_order_acquire);
		// revision 的 release 发布发生在内容布尔值之后；先 acquire revision，
		// 再读取布尔值，避免把新 revision 与旧内容拼成不可重试的快照。
		snapshot.currentPageHasContent =
			impl_->currentPageHasContent.load(std::memory_order_acquire);
		snapshot.laserVisible = impl_->laserVisible.load(std::memory_order_acquire);
		snapshot.laserHoldsBar = impl_->laserHoldsBar.load(std::memory_order_acquire);
		{
			std::scoped_lock lock(impl_->whiteboardMutex);
			snapshot.whiteboardStyle = impl_->whiteboardStyle;
			snapshot.whiteboardTheme = impl_->whiteboardTheme;
			snapshot.whiteboardGeneration = impl_->whiteboardGeneration;
			snapshot.whiteboardLoadPending = impl_->whiteboardLoadPending;
			snapshot.whiteboardUnsaved = impl_->whiteboardUnsaved;
		}
		snapshot.canUndo = impl_->canUndo.load(std::memory_order_acquire);
		snapshot.canRedo = impl_->canRedo.load(std::memory_order_acquire);
		snapshot.completedStrokeKind =
			impl_->completedStrokeKind.load(std::memory_order_acquire);
		snapshot.selectionMode = impl_->selectionMode.load(std::memory_order_acquire);
		// 页码、主题、代次和加载闸门取同一个发布快照。
		{
			std::scoped_lock lock(impl_->whiteboardMutex);
			snapshot.workspace = impl_->workspace.load(std::memory_order_acquire);
			if (snapshot.workspace == Bridge::Workspace::Whiteboard)
			{
				snapshot.currentPageIndex = impl_->whiteboardPageIndex;
				snapshot.pageCount = impl_->whiteboardPageCount;
				snapshot.whiteboardStyle = impl_->whiteboardStyle;
			snapshot.whiteboardTheme = impl_->whiteboardTheme;
				snapshot.whiteboardGeneration = impl_->whiteboardGeneration;
				snapshot.whiteboardLoadPending = impl_->whiteboardLoadPending;
				snapshot.whiteboardUnsaved = impl_->whiteboardUnsaved;
				snapshot.canUndo = impl_->whiteboardCanUndo;
				snapshot.canRedo = impl_->whiteboardCanRedo;
				snapshot.currentPageHasContent = impl_->whiteboardHasContent;
				snapshot.contentRevision = impl_->whiteboardContentRevision;
			}
		}
		{
			std::scoped_lock lock(impl_->presentationTargetMutex);
			snapshot.hostGeneration = impl_->acceptedStrokes.HostGeneration();
			snapshot.appliedProductRevision = impl_->appliedProductRevision;
			snapshot.appliedTool = impl_->appliedTool;
			snapshot.presentationReady = impl_->readyPresentationTarget;
			snapshot.presentationUiReady = impl_->uiReadyPresentationTarget;
			snapshot.presentationSuspended = impl_->suspendedPresentationTarget;
			snapshot.commandScenePending = impl_->commandScenePending;
			snapshot.consumerFatal = impl_->input.ConsumerFatal();
			snapshot.recovery = impl_->recovery;
			snapshot.presentationInputReady = !impl_->input.AdmissionBlocked();
		}
		snapshot.requestedOutputTarget =
			impl_->requestedOutputTarget.load(std::memory_order_acquire);
		snapshot.requestedOutputRevision =
			impl_->requestedOutputRevision.load(std::memory_order_acquire);
		snapshot.readyOutputTarget =
			impl_->readyOutputTarget.load(std::memory_order_acquire);
		snapshot.readyOutputRevision =
			impl_->readyOutputRevision.load(std::memory_order_acquire);
		snapshot.presentedContentRevision =
			impl_->presentedContentRevision.load(std::memory_order_acquire);
		snapshot.auxiliaryFullFrameClean =
			impl_->auxiliaryFullFrameClean.load(std::memory_order_acquire);
		snapshot.auxiliaryReadbackPayloadBytes =
			impl_->auxiliaryReadbackPayloadBytes.load(std::memory_order_acquire);
		snapshot.lastDirtyRect.left = impl_->lastDirtyLeft.load(std::memory_order_relaxed);
		snapshot.lastDirtyRect.top = impl_->lastDirtyTop.load(std::memory_order_relaxed);
		snapshot.lastDirtyRect.right = impl_->lastDirtyRight.load(std::memory_order_relaxed);
		snapshot.lastDirtyRect.bottom = impl_->lastDirtyBottom.load(std::memory_order_relaxed);
		return snapshot;
	}

	bool Host::IsAcceptedStrokeCurrent(const Bridge::AcceptedStrokeBegin& accepted) const noexcept
	{
		std::scoped_lock lock(impl_->presentationTargetMutex);
		const auto desired = impl_->bridge.Snapshot();
		Detail::HostAcceptedStrokeContext current;
		current.running = impl_->running.load(std::memory_order_acquire);
		current.desiredWorkspace = desired.workspace;
		if (desired.presentationTarget)
			current.desiredPresentation = Bridge::ReadyIdentityFor(*desired.presentationTarget);
		current.commandScenePending = impl_->commandScenePending;
		current.admissionBlocked = impl_->input.AdmissionBlocked();
		return impl_->acceptedStrokes.IsCurrent(accepted, current) &&
			impl_->running.load(std::memory_order_acquire);
	}

	bool Host::WaitForRuntimeRevision(std::uint64_t revision,
		std::uint32_t timeoutMilliseconds, bool waitWhileStopped) const noexcept
	{
		return impl_->runtimeRevision.WaitForChange(
			revision, timeoutMilliseconds, impl_->running, waitWhileStopped);
	}

	void Host::RequestShownFrame(std::shared_ptr<ShownFrameRequest> request)
	{
		{
			std::scoped_lock lock(impl_->surfaceRequestMutex);
			impl_->pendingShownFrame = std::move(request);
		}
		(void)impl_->input.PublishControlWake();
	}

	void Host::AcknowledgeAuxiliarySurfaceHidden(const HostRuntimeSnapshot& expected) noexcept
	{
		if (!expected.running || !expected.firstFrameReady || !expected.lastPresentSucceeded ||
			expected.auxiliaryReadbackPayloadBytes == 0 || expected.hostGeneration == 0 ||
			expected.requestedOutputTarget != expected.readyOutputTarget ||
			expected.requestedOutputRevision != expected.readyOutputRevision ||
			expected.presentedContentRevision != expected.contentRevision ||
			(expected.readyOutputTarget == HostOutputTarget::SelectionUlw &&
				(expected.currentPageHasContent || expected.laserVisible || !expected.auxiliaryFullFrameClean))) return;
		const Impl::AuxiliaryReleaseRequest request{ expected.hostGeneration,
			expected.appliedProductRevision, expected.readyOutputRevision,
			expected.presentedContentRevision, expected.successfulPresentCount,
			expected.readyOutputTarget, expected.committedWidth, expected.committedHeight };
		try
		{
			std::scoped_lock lock(impl_->surfaceRequestMutex);
			if (impl_->pendingAuxiliaryRelease == request) return;
			impl_->pendingAuxiliaryRelease = request;
		}
		catch (...) { return; } // 优化请求失败保留缓冲，不改变显示或 ready。
		(void)impl_->input.PublishControlWake();
	}

	void Host::NotifyWorkspaceProgress() noexcept
	{
		impl_->PublishRuntimeRevision();
	}

	bool Host::WaitForContentRevision(std::uint64_t revision,
		std::uint32_t timeoutMilliseconds) const noexcept
	{
		std::unique_lock lock(impl_->contentMutex);
		return impl_->contentCondition.wait_for(lock,
			std::chrono::milliseconds(timeoutMilliseconds), [this, revision]
			{
				return impl_->contentRevision.load(std::memory_order_acquire) != revision ||
					!impl_->running.load(std::memory_order_acquire);
			});
	}

	bool Host::PublishHiddenTestContact(WPARAM phaseValue, LPARAM position) noexcept
	{
		if (!impl_->hiddenTestContactInjectionEnabled) return false;
		const auto phase = static_cast<HiddenTestContactPhase>(static_cast<std::uint32_t>(phaseValue)&0xffu);
		const auto deviceType=(phaseValue & kHiddenTestRightMouseFlag)?InputDeviceType::MouseRight:
			(phaseValue & kHiddenTestMouseFlag) ? InputDeviceType::MouseLeft :
			(phaseValue & kHiddenTestTouchFlag) ? InputDeviceType::Touch : InputDeviceType::Pen;
		ContactSnapshot snapshot{};
		{
			std::scoped_lock lock(impl_->displayMutex);
			const auto& area=impl_->hiddenContactArea;
			snapshot.rawContactSize={area.rawWidth,area.rawHeight};
			snapshot.contactSize={area.widthPx,area.heightPx};snapshot.contactAreaUnits=area.units;
		}
		snapshot.position.x = static_cast<float>(static_cast<short>(LOWORD(position)));
		snapshot.position.y = static_cast<float>(static_cast<short>(HIWORD(position)));
		snapshot.pressure = phase == HiddenTestContactPhase::Down ? 0.8f : 0.7f;
		LARGE_INTEGER qpc = {};
		QueryPerformanceCounter(&qpc);
		snapshot.qpc = qpc.QuadPart;
		if ((phaseValue & kHiddenTestNoPressureFlag) != 0) snapshot.pressure = -1.0f;
		if (phase == HiddenTestContactPhase::Up && (phaseValue & kHiddenTestDelayedUpFlag) != 0)
		{
			// 隐藏验收在最后 Move 后等待 40ms，模拟 Up 包迟到 30ms 而非篡改模型时间。
			LARGE_INTEGER frequency = {};
			QueryPerformanceFrequency(&frequency);
			snapshot.qpc -= frequency.QuadPart * 30 / 1000;
		}
		snapshot.isInvertedCursor=(phaseValue & kHiddenTestPenTailFlag)!=0;
		snapshot.source.kind=(deviceType==InputDeviceType::MouseLeft || deviceType==InputDeviceType::MouseRight) ? SpeedEraser::SourceKind::Mouse :
			deviceType==InputDeviceType::Touch ? SpeedEraser::SourceKind::Touch :
			(phaseValue & kHiddenTestIntegratedPenFlag) ? SpeedEraser::SourceKind::IntegratedPen :
			(phaseValue & kHiddenTestExternalPenFlag) ? SpeedEraser::SourceKind::ExternalPen : SpeedEraser::SourceKind::Unknown;
		snapshot.source.recognition=SpeedEraser::SourceRecognition::RtsCapabilities;
		snapshot.source.contextId=0xD303u;snapshot.source.cursorId=0xD304u+static_cast<uint32_t>(SpeedEraser::EntryForInput(static_cast<uint32_t>(deviceType),snapshot.isInvertedCursor));snapshot.source.generation=1;
		const auto scale=impl_->window.SpeedEraserDisplayScaleSnapshot();
		snapshot.source.mappedMonitor=scale.monitor;
		snapshot.source.mappedLeft=scale.desktopLeft;snapshot.source.mappedTop=scale.desktopTop;
		snapshot.source.mappedWidth=scale.pixelWidth;snapshot.source.mappedHeight=scale.pixelHeight;

		// 隐藏测试走同一个无锁 contact mailbox，不触碰 Renderer 或 RTS 内部状态。
		constexpr std::uint32_t tabletContextId = 0xD303u;
		const std::uint32_t contactId = snapshot.source.cursorId;
		bool published = false;
		switch (phase)
		{
		case HiddenTestContactPhase::Down:
			if(deviceType==InputDeviceType::Touch)impl_->window.NotifyTouchContactBegin();
			snapshot.phase = ContactPhase::Down;
			published = impl_->input.PublishDown(tabletContextId, contactId,
				deviceType, snapshot);
			break;
		case HiddenTestContactPhase::Move:
			snapshot.phase = ContactPhase::Move;
			published = impl_->input.PublishMove(tabletContextId, contactId, snapshot);
			break;
		case HiddenTestContactPhase::Up:
			snapshot.phase = ContactPhase::Up;
			published = impl_->input.PublishUp(tabletContextId, contactId, snapshot);
			break;
		case HiddenTestContactPhase::Hover:
			published = true;
			break;
		case HiddenTestContactPhase::Cancelled:
			snapshot.phase = ContactPhase::Cancelled;
			published = impl_->input.PublishCancelled(tabletContextId, contactId, snapshot);
			break;
		default:
			return false;
		}

		// 隐藏注入复用真实 RTS 的 Touch 模态通知，不能让旧 Mouse Hover 掩盖 Touch 光标。
		if(deviceType==InputDeviceType::Touch &&
			((phase==HiddenTestContactPhase::Down && !published) ||
			 (published && (phase==HiddenTestContactPhase::Up || phase==HiddenTestContactPhase::Cancelled))))
			impl_->window.NotifyTouchContactEnd();

		if(published && (deviceType==InputDeviceType::MouseLeft || deviceType==InputDeviceType::MouseRight || deviceType==InputDeviceType::Pen))
		{
			DrawingCursorSample cursor;
			cursor.x=snapshot.position.x;cursor.y=snapshot.position.y;cursor.qpc=snapshot.qpc;
			cursor.valid=phase!=HiddenTestContactPhase::Cancelled;
			cursor.inContact=phase==HiddenTestContactPhase::Down || phase==HiddenTestContactPhase::Move;
			cursor.source=snapshot.source;cursor.inverted=snapshot.isInvertedCursor;
			if(deviceType==InputDeviceType::MouseLeft || deviceType==InputDeviceType::MouseRight)impl_->window.PublishHiddenTestMouseCursor(cursor);
			else impl_->window.PublishPenCursorSample(cursor);
		}
		if (published) (void)impl_->input.PublishControlWake();
		return published;
	}

	void Host::SetInterruptedStrokeReconnectEnabled(bool enabled) noexcept
	{
		if (impl_->interruptedStrokeReconnectEnabled.exchange(enabled, std::memory_order_acq_rel) == enabled)
			return;
		// Start/Stop 不清除此期望值；空闲时也唤醒，关闭后及时正常提交等待接续的笔画。
		(void)impl_->input.PublishControlWake();
	}

	void Host::SetInterruptedStrokeReconnectDevices(bool touch, bool penTail) noexcept
	{
		const uint32_t mask = (touch ? 1u : 0u) | (penTail ? 2u : 0u);
		if (impl_->interruptedStrokeReconnectDevices.exchange(mask, std::memory_order_acq_rel) == mask)
			return;
		(void)impl_->input.PublishControlWake(); // 取消勾选后及时正常提交仍在等待接续的笔画。
	}

	void Host::SetGeneralDrawingOptions(bool multiTouchEnabled, bool penTailCorrectionEnabled, int predictionMode) noexcept
	{
		const uint32_t mode = predictionMode >= 0 && predictionMode <= 2 ? static_cast<uint32_t>(predictionMode) : 2u;
		const uint32_t next = (multiTouchEnabled ? 1u : 0u) | (penTailCorrectionEnabled ? 2u : 0u) | (mode << 2);
		if (impl_->generalDrawingOptions.exchange(next, std::memory_order_acq_rel) != next)
			(void)impl_->input.PublishControlWake();
	}

	void Host::SetMultiTouchToolOptions(bool pen, bool highlighter, bool eraser, bool shape) noexcept
	{
		// 四个子项同代发布；总开关不清子项，等值发布不打断帧节拍。
		const uint32_t next = (pen ? kMultiTouchToolPen : 0u) | (highlighter ? kMultiTouchToolHighlighter : 0u) |
			(eraser ? kMultiTouchToolEraser : 0u) | (shape ? kMultiTouchToolShape : 0u);
		if (impl_->multiTouchTools.exchange(next, std::memory_order_acq_rel) != next)
			(void)impl_->input.PublishControlWake();
	}

	void Host::SetLiveTaperOptions(bool enabled, bool mouse, bool touch, bool pen) noexcept
	{
		// 一次发布整组偏好，让落笔始终读取同一次设置变化的结果。
		const LiveTaperOptions next{ enabled, mouse, touch, pen };
		if (impl_->liveTaperOptions.exchange(next, std::memory_order_acq_rel) != next)
			(void)impl_->input.PublishControlWake();
	}

	void Host::SetCanvasPanInertiaOptions(CanvasPanInertiaOptions options) noexcept
	{
		// 六个偏好同代交给绘制 owner；总开关不清子项，等值发布不打断帧节拍。
		if (impl_->canvasPanInertiaOptions.exchange(options.Encode(), std::memory_order_acq_rel) != options.Encode())
			(void)impl_->input.PublishControlWake();
	}

	void Host::SetLaserOptions(bool multiTouchEnabled, const CanvasParticleOptions& particles) noexcept
	{
		// 保存原始子偏好；总开关只影响 owner 计算的有效值，重新开启后可恢复。
		const uint32_t requested = LaserParticleOptions::Pack(particles.enabled, particles.laserTrail,
			particles.laserClickWave, particles.laserDissolve) |
			(particles.clearDissolve ? LaserParticleOptions::ClearDissolve : 0u) |
			(particles.windRightUp ? LaserParticleOptions::WindRightUp : 0u) |
			LaserParticleOptions::PackLimit(particles.limit);
		const uint32_t next = (multiTouchEnabled ? 1u : 0u) | (requested << 1);
		if (impl_->laserOptions.exchange(next, std::memory_order_acq_rel) != next)
			(void)impl_->input.PublishControlWake();
	}

	void Host::SetPalmEraserOptions(const PalmEraser::Settings& options)
	{
		const auto normalized = PalmEraser::NormalizeSettings(options);
		{
			std::scoped_lock lock(impl_->palmEraserOptionsMutex);
			if (impl_->palmEraserOptions == normalized) return;
			impl_->palmEraserOptions = normalized;
		}
		// 只发布期望快照，Controller 仍只由绘制 owner 访问。
		(void)impl_->input.PublishControlWake();
	}
	PalmEraser::Settings Host::PalmEraserOptions() const
	{
		std::scoped_lock lock(impl_->palmEraserOptionsMutex);
		return impl_->palmEraserOptions;
	}

	void Host::SetUlwMultiStepCommitEnabled(bool enabled) noexcept
	{
		impl_->ulwMultiStepCommitPublished.store(true, std::memory_order_release);
		if (impl_->ulwMultiStepCommitEnabled.exchange(enabled, std::memory_order_acq_rel) != enabled)
			(void)impl_->input.PublishControlWake();
	}

	void Host::SetCanvasDebugOptions(bool enabled, bool visual) noexcept
	{
		// 可视化模式只在画布调试开启时有意义；保存规范化后的期望值，Controller 由绘制 owner 应用。
		const uint32_t next = enabled ? (1u | (visual ? 2u : 0u)) : 0u;
		if (impl_->canvasDebugOptions.exchange(next, std::memory_order_acq_rel) != next)
			(void)impl_->input.PublishControlWake();
	}

	void Host::InjectDebugFatalForTest() noexcept
	{
		Inkeys::Fatal::RequestDraw3DebugFault();
		// 绘制线程可能正阻塞在空闲等待里，控制唤醒让它立刻进入下一轮循环消费标志。
		(void)impl_->input.PublishControlWake();
	}

	bool Host::CanvasDebugEnabled() const noexcept
	{
		return (impl_->canvasDebugOptions.load(std::memory_order_acquire) & 1u) != 0;
	}

	bool Host::CanvasDebugVisual() const noexcept
	{
		return (impl_->canvasDebugOptions.load(std::memory_order_acquire) & 2u) != 0;
	}

	bool Host::DevicePerformanceSnapshot(std::uint64_t knownRevision, DevicePerformance::Report& out) const
	{
		return impl_->devicePerformance.Snapshot(knownRevision, out);
	}

	void Host::SetDevicePerformanceRecording(bool enabled) noexcept
	{
		impl_->devicePerformance.SetRecordingEnabled(enabled);
	}

	void Host::ClearDevicePerformance() noexcept
	{
		impl_->devicePerformance.Clear();
	}

	void Host::SetEraserDevelopmentOptions(const SpeedEraser::DevelopmentOptions& options)
	{
		{
			std::scoped_lock lock(impl_->displayMutex);
			if(impl_->eraserDevelopment==options)return;
			impl_->eraserDevelopment=options;
		}
		if(impl_->touchAreaTraceEnabled.exchange(options.touchAreaTrace,std::memory_order_relaxed)!=options.touchAreaTrace)
			impl_->touchAreaTraceRevision.fetch_add(1,std::memory_order_relaxed);
		impl_->window.SetEraserDiagnosticsEnabled(options.diagnostics || options.touchAreaTrace ||
			impl_->hiddenTestContactInjectionEnabled || impl_->startOptions.enableEraserDiagnostics);
		impl_->displayScaleDirty.store(true,std::memory_order_release);
		(void)impl_->input.PublishControlWake();
	}
	SpeedEraser::DevelopmentOptions Host::EraserDevelopmentOptions() const
	{
		std::scoped_lock lock(impl_->displayMutex);
		return impl_->eraserDevelopment;
	}
	void Host::SetHiddenTestContactArea(const SpeedEraser::ContactAreaSample& sample)
	{
		if(!impl_->hiddenTestContactInjectionEnabled)return;
		std::scoped_lock lock(impl_->displayMutex);impl_->hiddenContactArea=sample;
	}

	SpeedEraser::DisplayScale Host::EraserDisplayScaleSnapshot() const
	{
		return impl_->window.SpeedEraserDisplayScaleSnapshot();
	}

	LRESULT Host::ForwardMessage(HWND window, UINT message, WPARAM wParam, LPARAM lParam)
	{
		if (message == kDraw3HiddenTestContactMessage &&
			impl_->hiddenTestContactInjectionEnabled)
			return PublishHiddenTestContact(wParam, lParam) ? 0 : -1;
		if (window == impl_->attachedWindow.load(std::memory_order_acquire) &&
			(message == WM_WINDOWPOSCHANGED || message == WM_DPICHANGED || message == WM_DISPLAYCHANGE))
		{
			impl_->displayScaleDirty.store(true, std::memory_order_release);
			(void)impl_->input.PublishControlWake();
		}
		return impl_->window.HandleExternalMessage(window, message, wParam, lParam);
	}
	bool Host::RetryCanvasRecovery(const CanvasRecoveryState& expected,
		const std::function<bool()>& stillDesired) noexcept
	{
		std::scoped_lock lock(impl_->presentationTargetMutex);
		const auto desired = impl_->bridge.Snapshot();
		if ((stillDesired && !stillDesired()) || !CanvasRecoveryRetryMatches(expected, impl_->recovery, desired, Running(),
			impl_->inputStopping.load(std::memory_order_acquire), impl_->input.ConsumerFatal())) return false;
		CanvasCommand command;
		command.type = desired.workspace == Bridge::Workspace::Presentation && desired.presentationTarget
			? CanvasCommandType::SetPresentationTarget : CanvasCommandType::SetWorkspace;
		command.workspace = static_cast<std::uint8_t>(desired.workspace);
		command.presentationTarget = desired.presentationTarget;
		command.sceneClosedAdmission = true;
		command.explicitRecoveryRetry = true;
		command.recoveryRevision = expected.revision;
		try { impl_->window.EnqueueCanvasCommand(std::move(command)); }
		catch (...) { return false; }
		// 当前恢复状态已经关闸；仅成功呈现能解除，队列中的旧 token 不额外制造场景债务。
		impl_->PublishRuntimeRevision();
		return true;
	}

	void Host::SetActivationAllowed(bool enabled) noexcept
	{
		impl_->window.SetActivationAllowed(enabled);
	}
	Haptics::Context Host::HapticContext() const noexcept
	{
		return Haptics::ButtonContext(Running() ? impl_->hapticClicks.Run() : 0);
	}
	void Host::SetHapticPreferences(bool drawingEnabled, bool buttonsEnabled) noexcept
	{
		impl_->drawingHapticsEnabled.store(drawingEnabled, std::memory_order_release);
		impl_->hapticClicks.SetEnabled(buttonsEnabled);
		// 空闲或悬停预设也必须收敛，COM 的停止和恢复仍由绘制 owner 执行。
		(void)impl_->input.PublishControlWake();
	}
	bool Host::PublishHapticClick(const Haptics::Click& source,
		HostHapticClickDiagnostics* diagnostics) noexcept
	{
		const auto context = HapticContext();
		const auto now = GetTickCount64();
		if (diagnostics) diagnostics->context = context;
		if (!impl_->hapticClicks.Publish(source, context, now,
			diagnostics ? &diagnostics->rejection : nullptr)) return false;
		(void)impl_->input.PublishControlWake();
		return true;
	}
	Bridge::StateBridge& Host::ProductBridge() noexcept { return impl_->bridge; }
	void Host::PublishState(const Bridge::ProductState& state) noexcept
	{
		{
			std::scoped_lock lock(impl_->presentationTargetMutex);
			impl_->bridge.PublishState(state);
			impl_->RefreshPresentationInputGateLocked();
		}
		if (impl_->attachedWindow.load(std::memory_order_acquire))
			(void)impl_->input.PublishControlWake();
	}
	bool Host::SetWorkspaceTransitionPending(std::uint64_t epoch, bool pending,
		std::uint64_t expectedHostGeneration) noexcept
	{
		if (!epoch || !expectedHostGeneration) return false;
		{
			std::scoped_lock lock(impl_->presentationTargetMutex);
			if (!Running() || impl_->inputStopping.load(std::memory_order_acquire) ||
				expectedHostGeneration != impl_->acceptedStrokes.HostGeneration() ||
				epoch < impl_->workspaceTransitionEpoch ||
				(!pending && epoch != impl_->workspaceTransitionEpoch) ||
				(pending && epoch == impl_->workspaceTransitionEpoch && !impl_->workspaceTransitionPending))
				return false;
			if (epoch == impl_->workspaceTransitionEpoch && pending == impl_->workspaceTransitionPending)
				return true;
			// 新意图先同步关闸；Host 重启和同 run 的旧版本都不能覆盖当前原因。
			impl_->workspaceTransitionEpoch = epoch;
			impl_->workspaceTransitionPending = pending;
			impl_->RefreshPresentationInputGateLocked();
		}
		// 准入只拦新 Down，目标应用、实际帧和终态仍由原 owner 主动推进。
		(void)impl_->input.PublishControlWake();
		impl_->PublishRuntimeRevision();
		return true;
	}

	int RunLaserParticleOptionsProductionTest() noexcept
	{
		int failures = 0;
		const auto check = [&](bool condition, const char* name)
		{
			if (condition) return;
			++failures;
			std::fprintf(stderr, "[Draw3GeneralOptions] FAIL: %s\n", name);
		};
		try
		{
			Host host;
			auto& state = *host.impl_;
			auto configuration = CreateStrokeModelConfiguration(96);
			configuration.laserParticleOptions = 0u;
			DrawingController controller(state.input, state.window, state.renderer, state.presentation, configuration);
			state.ApplyLaserOptions(controller, 0u);
			check(controller.GetLaserParticleOptions() == LaserParticleOptions::Defaults &&
				LaserParticleLimitFromOptions(controller.GetLaserParticleOptions()) == LaserParticleLimit::Medium,
				"unpublished Host particle options default to the medium tier");
			const uint32_t effects = LaserParticleOptions::Enabled | LaserParticleOptions::Effects |
				LaserParticleOptions::WindRightUp;
			CanvasParticleOptions particles;
			particles.enabled = true;
			particles.windRightUp = true;
			for (const auto limit : { LaserParticleLimit::Low, LaserParticleLimit::Medium,
				LaserParticleLimit::High, LaserParticleLimit::Extreme })
			{
				particles.limit = static_cast<uint32_t>(limit);
				host.SetLaserOptions(true, particles);
				state.ApplyLaserOptions(controller, 0u); // 总多指关闭时仍下发粒子档位。
				const auto options = controller.GetLaserParticleOptions();
				check(LaserParticleLimitFromOptions(options) == limit &&
					(options & ~LaserParticleOptions::LimitMask) == effects,
					"all four tiers reach the drawing owner without changing the other option bits");
				check(EffectiveLaserParticleEffects(options, true) == LaserParticleOptions::Effects &&
					EffectiveLaserParticleEffects(options, false) == LaserParticleOptions::ClickWave,
					"limit bits do not affect effective particle effects");
			}
		}
		catch (...)
		{
			check(false, "Host particle options fixture could not be created");
		}
		return failures;
	}

	int RunHostWorkspaceTransitionInputProductionProbe() noexcept
	{
		int failures = 0;
		const auto check = [&](bool condition, const char* name)
		{
			if (condition) return;
			++failures;
			std::fprintf(stderr, "[Draw3WorkspaceTransition] FAIL: %s\n", name);
		};
		try
		{
			Host host;
			auto& state = *host.impl_;
			check(host.CanvasInformation() == "画布尚未初始化", "canvas information begins unavailable");
			state.UpdateCanvasInformation("正在初始化", nullptr, nullptr, true);
			const std::string firstGraphics = "Hardware / DirectComposition / FLIP_SEQUENTIAL\n";
			const std::string firstInput = "RTS Extended\n";
			Host::Impl::ObserveCanvasGraphics(&state, firstGraphics);
			state.UpdateCanvasInformation("就绪", nullptr, &firstInput);
			check(host.CanvasInformation().find(firstGraphics + firstInput) != std::string::npos,
				"canvas information merges actual graphics callback and input publication");
			state.UpdateCanvasInformation("正在初始化", nullptr, nullptr, true);
			check(host.CanvasInformation().find("Hardware") == std::string::npos &&
				host.CanvasInformation().find("RTS Extended") == std::string::npos,
				"new canvas generation removes old successful graphics and input");
			// 同一代的两类提醒各自保留；新启动只继承本次重建明确传入的失败上下文。
			const PresentationFallbackReport report{ "DComp → ULW", "CreateSwapChain 0x80070057" };
			Host::Impl::ObserveCanvasFallback(&state, report);
			PresentationFallbackReport snapshot;
			host.CanvasInformation(&snapshot);
			check(snapshot.presentationWarning == report.presentationWarning,
				"canvas report carries the presentation warning");
			state.RecordHostStartupFailure("Draw3 first frame");
			const auto retry = host.CanvasFallbackReport();
			state.UpdateCanvasInformation("正在初始化", nullptr, nullptr, true, &retry);
			check(host.CanvasFallbackReport().lastAttemptFailure == report.lastAttemptFailure,
				"retry generation retains failed request details");
			state.UpdateCanvasInformation("正在初始化", nullptr, nullptr, true);
			check(host.CanvasFallbackReport().presentationWarning.empty(),
				"fresh generation clears stale warning strips");
			state.RecordHostStartupFailure("DrawingController initialization", "test failure");
			check(host.CanvasFallbackReport().lastAttemptFailure == "DrawingController initialization: test failure",
				"host startup failure records its stage when presenter had no error");
			const std::string fallbackGraphics = "WARP / ULW / ordinary\n";
			const std::string fallbackInput = "Win32Mouse / RTS failed\n";
			Host::Impl::ObserveCanvasGraphics(&state, fallbackGraphics);
			state.UpdateCanvasInformation("就绪", nullptr, &fallbackInput);
			state.UpdateCanvasInformation("已停止");
			check(host.CanvasInformation().find(fallbackGraphics + fallbackInput) != std::string::npos &&
				host.CanvasInformation().find("已停止") != std::string::npos,
				"stopped canvas labels retained fallback facts as last generation records");
			check(!host.SetWorkspaceTransitionPending(1, true, 1), "stopped Host refuses transition publication");
			// 只建立准入所需运行前提；使用真实 Impl/Coordinator，不 Start、附着 HWND 或启动 owner。
			state.input.ResetForNextRun();
			state.bridge.Reset();
			state.ResetRuntimeDiagnostics();
			state.startOptions.requirePresentationUiReady = true;
			state.running.store(true, std::memory_order_release);
			struct RestoreStopped
			{
				std::atomic_bool& running;
				~RestoreStopped() { running.store(false, std::memory_order_release); }
			} restore{ state.running };
			state.input.EnableDiagnostics(true);
			const auto firstRun = host.RuntimeSnapshot();
			Bridge::ProductState preferences;
			preferences.tool = Bridge::Tool::HardPen; preferences.selectionMode = false;
			host.PublishState(preferences);
			const auto preferenceRevision = state.bridge.Snapshot().revision;
			check(firstRun.hostGeneration != 0 && firstRun.appliedProductRevision == 0 &&
				host.RuntimeSnapshot().appliedProductRevision == 0,
				"Bridge publication cannot forge an applied tool acknowledgement");
			state.PumpBridgeState();
			const auto applied = host.RuntimeSnapshot();
			check(applied.hostGeneration == firstRun.hostGeneration &&
				applied.appliedProductRevision == preferenceRevision && applied.appliedTool == Bridge::Tool::HardPen &&
				applied.runtimeRevision > firstRun.runtimeRevision &&
				state.window.ActiveTool() == DrawingTool::HardPen && !state.window.SelectionMode(),
				"actual owner preference application publishes its exact tool and ProductState revision");
			int controlWakes = 0;
			const auto takeDown = [&]() -> ContactHandle
			{
				ContactRecord* record = nullptr;
				while (state.input.TryDequeue(record))
				{
					if (record) return { record, record->Generation() };
					++controlWakes;
					state.input.AcknowledgeControlWake();
				}
				return {};
			};
			ContactSnapshot sample;
			sample.position = { 1.0f, 1.0f }; sample.qpc = 1;
			check(state.input.PublishDown(0xF501, 1, InputDeviceType::MouseLeft, sample),
				"source contact enters before the transition");
			const auto source = takeDown();
			const auto beforeTransitionWake = state.input.CaptureWakeGeneration();
			const auto beforeTransitionRevision = state.runtimeRevision.Revision();
			check(state.input.ContactAdmitted(source) && host.SetWorkspaceTransitionPending(1, true, firstRun.hostGeneration) &&
				state.input.CaptureWakeGeneration() != beforeTransitionWake &&
				state.runtimeRevision.Revision() > beforeTransitionRevision &&
				state.input.AdmissionBlocked() && !state.input.ContactAdmitted(source),
				"new pending synchronously closes real admission and invalidates earlier queued Down");
			check(state.input.PublishMove(0xF501, 1, sample) && state.input.PublishUp(0xF501, 1, sample),
				"existing producer Move and physical terminal continue while pending");
			ContactSnapshot terminal;
			check(state.input.TryReadSnapshot(source, terminal) && terminal.phase == ContactPhase::Up,
				"pending keeps the source contact terminal readable for drain");
			state.input.Recycle(source);
			check(state.input.PublishDown(0xF501, 2, InputDeviceType::MouseRight, sample),
				"producer may queue a Down while admission is closed");
			const auto duringPending = takeDown();
			check(duringPending && controlWakes != 0 && !state.input.ContactAdmitted(duringPending),
				"control wake continues and transition Down is not admitted");
			Bridge::PresentationTarget target;
			target.key.bytes[0] = 0xF5;
			target.sourceIdentity = "path:c:\\tests\\transition.pptx";
			target.totalPages = 1; target.bindingRevision = 1; target.sessionRevision = 1;
			check(state.bridge.PublishPresentationTarget(target).has_value(), "pending permits target publication");
			const auto desired = state.bridge.Snapshot();
			if (!desired.presentationTarget) return failures + 1;
			host.PublishState(desired);
			const auto ready = Bridge::ReadyIdentityFor(*desired.presentationTarget);
			Host::Impl::ObserveWorkspace(&state, Bridge::Workspace::Presentation, 0, 1, &ready);
			check(host.PublishPresentationUiReady(ready) && state.inputGateReasons == 1024u &&
				state.input.AdmissionBlocked(), "CPU scene and UI ACK cannot retire the workspace fence");
			check(host.SetWorkspaceTransitionPending(2, true, firstRun.hostGeneration) && host.SetWorkspaceTransitionPending(2, true, firstRun.hostGeneration) &&
				!host.SetWorkspaceTransitionPending(1, false, firstRun.hostGeneration) && !host.SetWorkspaceTransitionPending(1, true, firstRun.hostGeneration) &&
				!host.SetWorkspaceTransitionPending(3, false, firstRun.hostGeneration) && state.input.AdmissionBlocked(),
				"new epoch supersedes pending and stale or unmatched retirement cannot clear it");
			check(host.SetWorkspaceTransitionPending(2, false, firstRun.hostGeneration) && host.SetWorkspaceTransitionPending(2, false, firstRun.hostGeneration) &&
				!host.SetWorkspaceTransitionPending(2, true, firstRun.hostGeneration) && !state.input.AdmissionBlocked() &&
				!state.input.ContactAdmitted(duringPending),
				"only matching retirement opens its own reason; old Down and late Begin remain rejected");
			state.input.DiscardUntilTerminal(duringPending);
			check(state.input.PublishUp(0xF501, 2, sample) && !state.input.HasQuarantinedContacts(),
				"rejected transition Down still retires through its real terminal");
			check(state.input.PublishDown(0xF501, 2, InputDeviceType::MouseRight, sample),
				"new contact can follow matching transition retirement");
			const auto next = takeDown();
			check(state.input.ContactAdmitted(next) && state.input.PublishUp(0xF501, 2, sample),
				"new generation uses the reopened admission revision");
			state.input.Recycle(next);
			Bridge::ProductState desktop;
			host.PublishState(desktop);
			state.PumpBridgeState();
			Host::Impl::ObserveWorkspace(&state, Bridge::Workspace::Desktop, 0, 1, nullptr);
			Shortcuts::Context inkShortcut;
			inkShortcut.scope.pageEpoch = 1;
			inkShortcut.available = true; inkShortcut.hasContent = true;
			Host::Impl::ObserveShortcutContext(&state, inkShortcut);
			const auto shortcutBefore = host.ShortcutContext();
			const auto oldLease = host.AcquireShortcutLease(shortcutBefore, false);
			check(shortcutBefore.available && oldLease && host.SetWorkspaceTransitionPending(3, true, firstRun.hostGeneration),
				"ink shortcut lease can predate workspace pending");
			host.ReleaseShortcutLease(oldLease);
			check(!state.shortcuts.Lease() && state.input.BaseAdmissionBlocked() &&
				!host.ShortcutContext().available && !host.AcquireShortcutLease(shortcutBefore, false),
				"old shortcut release completes while pending prevents a new ink scope and lease");
			check(host.SetWorkspaceTransitionPending(3, false, firstRun.hostGeneration) && host.ShortcutContext().available,
				"matching retirement restores an otherwise eligible ink shortcut scope");
			std::uint64_t epoch = 4;
			for (const auto reason : { 2u, 4u, 512u })
			{
				Bridge::ProductState baseline;
				baseline.workspace = reason == 4u ? Bridge::Workspace::Whiteboard : Bridge::Workspace::Desktop;
				host.PublishState(baseline);
				Host::Impl::ObserveWorkspace(&state, baseline.workspace, 0, 1, nullptr);
				check(host.SetWorkspaceTransitionPending(epoch, true, firstRun.hostGeneration), "next gate fixture begins a real transition");
				if (reason == 2u) state.commandScenePending = true;
				if (reason == 4u) state.whiteboardLoadPending.store(true, std::memory_order_release);
				if (reason == 512u)
				{
					CanvasRecoveryState recovery; recovery.status = CanvasRecoveryStatus::Retrying;
					Host::Impl::ObserveRecovery(&state, recovery);
				}
				{ std::scoped_lock lock(state.presentationTargetMutex); state.RefreshPresentationInputGateLocked(); }
				check(host.SetWorkspaceTransitionPending(epoch++, false, firstRun.hostGeneration) && state.input.AdmissionBlocked() &&
					(state.inputGateReasons & reason) != 0,
					"retirement preserves independent scene, whiteboard load and owner recovery reasons");
				inkShortcut.scope.workspace = baseline.workspace;
				Host::Impl::ObserveShortcutContext(&state, inkShortcut);
				const auto blockedShortcut = host.ShortcutContext();
				check(!blockedShortcut.available && !host.AcquireShortcutLease(blockedShortcut, false),
					"scene, load and recovery still refuse new ink shortcuts after pending retires");
				state.commandScenePending = false;
				state.whiteboardLoadPending.store(false, std::memory_order_release);
				Host::Impl::ObserveRecovery(&state, {});
				check(!state.input.AdmissionBlocked(), "original owner can later clear its own reason");
			}
			const auto minimizedEpoch = epoch | std::uint64_t{ 1 };
			check(host.SetWorkspaceTransitionPending(minimizedEpoch, true, firstRun.hostGeneration),
				"minimize begins with the existing workspace admission reason");
			Bridge::ProductState whiteboard;
			whiteboard.workspace = Bridge::Workspace::Whiteboard; whiteboard.selectionMode = false;
			host.PublishState(whiteboard); state.PumpBridgeState();
			Host::Impl::ObserveWorkspace(&state, Bridge::Workspace::Whiteboard, 0, 1, nullptr);
			// 稳定最小化只退休协调 Pending，不撤销同一个 Host 原因。
			check(host.SetWorkspaceTransitionPending(minimizedEpoch, true, firstRun.hostGeneration) &&
				state.input.AdmissionBlocked() && state.inputGateReasons == 1024u,
				"ready and repeated stable-minimized publication keep Pen admission closed");
			const auto restoreEpoch = minimizedEpoch + 2;
			check(host.SetWorkspaceTransitionPending(restoreEpoch, true, firstRun.hostGeneration) &&
				!host.SetWorkspaceTransitionPending(minimizedEpoch, false, firstRun.hostGeneration) &&
				state.input.AdmissionBlocked(), "restore takes a new odd epoch and an old minimize retirement cannot open it");
			Host::Impl::ObserveWorkspace(&state, Bridge::Workspace::Whiteboard, 0, 1, nullptr);
			check(state.input.AdmissionBlocked() &&
				host.SetWorkspaceTransitionPending(restoreEpoch, false, firstRun.hostGeneration) &&
				!state.input.AdmissionBlocked(), "only matching restore retirement reopens otherwise ready Pen admission");
			// 实际观察入口、Bridge快照和owner应用确认组成完整无窗口样式门回归。
			Host::Impl::ObserveWhiteboard(&state, {0xffffff, WhiteboardPattern::Blank}, 41, false, false, 0, 1, false, 0);
			check(state.whiteboardStylePending && state.input.AdmissionBlocked(), "theme closes Down before business observes it");
			const auto lightStyle = host.RuntimeSnapshot().whiteboardStyle;
			Host::Impl::ObserveWhiteboard(&state, {0, WhiteboardPattern::Blank}, 41, false, false, 0, 1, false, 0);
			whiteboard.whiteboardStyle = lightStyle; host.PublishState(whiteboard); state.PumpBridgeState();
			check(state.whiteboardStylePending && state.input.AdmissionBlocked(), "same-page old-theme ACK cannot release new style");
			whiteboard.whiteboardStyle = host.RuntimeSnapshot().whiteboardStyle;
			state.whiteboardLoadPending.store(true, std::memory_order_release);
			host.PublishState(whiteboard); state.PumpBridgeState();
			check(!state.whiteboardStylePending && state.input.AdmissionBlocked(), "style ACK preserves independent cold-load gate");
			Host::Impl::ObserveWhiteboard(&state, {0, WhiteboardPattern::Blank}, 41, false, false, 0, 1, false, 0);
			check(!state.whiteboardStylePending && !state.input.AdmissionBlocked(), "same-theme repeat is idempotent after matching application");
			host.PublishState(desktop); state.PumpBridgeState();
			Host::Impl::ObserveWorkspace(&state, Bridge::Workspace::Desktop, 0, 1, nullptr);
			inkShortcut.scope.workspace = Bridge::Workspace::Desktop;
			Host::Impl::ObserveShortcutContext(&state, inkShortcut);
			epoch = restoreEpoch + 1;
			check(host.SetWorkspaceTransitionPending(epoch, true, firstRun.hostGeneration), "shortcut fixture begins transition");
			state.input.SetShortcutAdmissionBlocked(true);
			check(host.SetWorkspaceTransitionPending(epoch++, false, firstRun.hostGeneration) && state.input.AdmissionBlocked() &&
				!state.input.BaseAdmissionBlocked(), "transition retirement does not clear the separate shortcut OR gate");
			state.input.SetShortcutAdmissionBlocked(false);
			check(host.SetWorkspaceTransitionPending(epoch, true, firstRun.hostGeneration), "fatal fixture begins transition");
			state.input.SetConsumerFatal();
			check(host.SetWorkspaceTransitionPending(epoch, false, firstRun.hostGeneration) && state.input.ConsumerFatal() &&
				state.input.AdmissionBlocked() && !host.ShortcutContext().available &&
				!host.AcquireShortcutLease(host.ShortcutContext(), false),
				"transition retirement cannot reopen a fatal consumer or its ink shortcuts");
			// 与 Start 相同的无 producer reset；旧 pending/fatal 不泄漏到下一 run。
			state.input.ResetForNextRun(); state.bridge.Reset(); state.ResetRuntimeDiagnostics();
			const auto restarted = host.RuntimeSnapshot();
			check(!state.input.AdmissionBlocked() && !state.input.ConsumerFatal() &&
				restarted.hostGeneration != firstRun.hostGeneration && restarted.appliedProductRevision == 0 &&
				!host.SetWorkspaceTransitionPending(1, true, firstRun.hostGeneration) && !state.input.AdmissionBlocked(),
				"new run advances Host identity and rejects an old-run Begin without resurrecting its fence");
			check(host.SetWorkspaceTransitionPending(1, true, restarted.hostGeneration) &&
				!host.SetWorkspaceTransitionPending(1, false, firstRun.hostGeneration) &&
				!host.SetWorkspaceTransitionPending(2, true, firstRun.hostGeneration) && state.input.AdmissionBlocked(),
				"same numeric scene epoch in a new run cannot be cleared or replaced by old-run publications");
			check(host.SetWorkspaceTransitionPending(1, false, restarted.hostGeneration) &&
				!state.input.AdmissionBlocked() && !host.SetWorkspaceTransitionPending(2, true, firstRun.hostGeneration) &&
				!state.input.AdmissionBlocked() && host.SetWorkspaceTransitionPending(3, true, restarted.hostGeneration),
				"new-run owner alone retires and reopens its fence; late old Begin cannot close a stable scene");
			inkShortcut.scope.workspace = Bridge::Workspace::Desktop;
			Host::Impl::ObserveShortcutContext(&state, inkShortcut);
			state.CloseInputAdmission();
			check(state.input.AdmissionBlocked() && !state.workspaceTransitionPending &&
				state.workspaceTransitionEpoch == 0 && state.inputGateReasons == 1u &&
				!host.SetWorkspaceTransitionPending(3, false, restarted.hostGeneration) &&
				!host.SetWorkspaceTransitionPending(4, true, restarted.hostGeneration) && !host.ShortcutContext().available &&
				!host.AcquireShortcutLease(host.ShortcutContext(), false),
				"stop retires its fence and refuses reopen or new ink shortcuts while preserving stop admission");
			check(state.input.DiagnosticsSnapshot().occupiedSlots == 0, "all real contact routes are released");
		}
		catch (...) { check(false, "workspace transition production fixture completes without exception"); }
		if (!failures) std::fputs("[Draw3WorkspaceTransition] PASS: epoch fence, owner ACK, independent reasons and real contact drain\n", stderr);
		return failures;
	}

	bool Host::PublishPresentationUiReady(
		const Bridge::PresentationReadyIdentity& identity) noexcept
	{
		if (!Running()) return false;
		std::scoped_lock lock(impl_->presentationTargetMutex);
		const auto state = impl_->bridge.Snapshot();
		if (!TryLatchPresentationUiReady(state, impl_->suspendedPresentationTarget,
			identity, impl_->uiReadyPresentationTarget)) return false;
		TracePptTiming("page_ui_ack", identity.sessionRevision, identity.targetRevision);
		impl_->RefreshPresentationInputGateLocked();
		return true;
	}

	bool Host::SetPresentationInputSuspended(
		const Bridge::PresentationReadyIdentity& expected, bool suspended) noexcept
	{
		if (!Running()) return false;
		std::scoped_lock lock(impl_->presentationTargetMutex);
		const auto state = impl_->bridge.Snapshot();
		if (state.workspace != Bridge::Workspace::Presentation || !state.presentationTarget ||
			Bridge::ReadyIdentityFor(*state.presentationTarget) != expected) return false;
		impl_->suspendedPresentationTarget = suspended ? std::optional(expected) : std::nullopt;
		// Unknown/结束页可能把数字改成占位；恢复时必须等待当前 UI 再次确认。
		if (suspended)
		{
			impl_->uiReadyPresentationTarget.reset();
			// UI 可合并掉中间的占位页；恢复时换 target revision，要求真实呈现及新回执。
			(void)impl_->bridge.RequirePresentationTargetRefresh(expected);
		}
		impl_->RefreshPresentationInputGateLocked();
		return true;
	}

	Bridge::CommandResult Host::PublishWhiteboardTheme(WhiteboardTheme theme,
		std::uint64_t generation, std::size_t page) noexcept
	{
		std::scoped_lock lock(impl_->commandPublishMutex);
		if (!impl_->input.TryReserveCommandWake()) return Bridge::CommandResult::QueueFull;
		const auto result = impl_->bridge.PublishWhiteboardTheme(theme, generation, page);
		if (result == Bridge::CommandResult::Accepted) impl_->input.PublishReservedCommandWake();
		else impl_->input.CancelReservedCommandWake();
		return result;
	}

	Bridge::CommandResult Host::PublishShapeAction(const ShapeUiAction& action) noexcept
	{
		std::scoped_lock lock(impl_->commandPublishMutex);
		if (!impl_->input.TryReserveCommandWake()) return Bridge::CommandResult::QueueFull;
		bool coalesced = false;
		const auto result = impl_->bridge.PublishShapeAction(action, &coalesced);
		// 覆盖已排队预览时其原有唤醒仍在，预留的新唤醒必须退回，保持唤醒与命令一一对应。
		if (result == Bridge::CommandResult::Accepted && !coalesced) impl_->input.PublishReservedCommandWake();
		else impl_->input.CancelReservedCommandWake();
		return result;
	}

	Shortcuts::Context Host::ShortcutContext() const noexcept
	{
		std::scoped_lock lock(impl_->presentationTargetMutex);
		return impl_->ShortcutContextLocked();
	}

	std::uint64_t Host::PublishShortcutCommand(Bridge::CommandType type,
		const Shortcuts::Scope& expected, std::uint64_t leaseId,
		std::optional<std::uint64_t> expectedContentRevision) noexcept
	{
		std::scoped_lock commandLock(impl_->commandPublishMutex);
		std::scoped_lock lock(impl_->presentationTargetMutex);
		const auto current = impl_->ShortcutContextLocked();
		if (!current.available || current.scope != expected || impl_->shortcuts.Lease() != leaseId)
			return 0;
		const auto id = impl_->shortcuts.Reserve(expected);
		if (!id) return 0;
		if (!impl_->input.TryReserveCommandWake())
		{
			impl_->shortcuts.Retire(id);
			return 0;
		}
		Bridge::CommandResult result = Bridge::CommandResult::QueueFull;
		try
		{
			auto request = std::make_shared<Shortcuts::Request>();
			*request = { id, expected, leaseId, expectedContentRevision };
			result = impl_->bridge.PublishShortcut(type, std::move(request));
		}
		catch (...) {}
		if (result == Bridge::CommandResult::Accepted)
		{
			impl_->input.PublishReservedCommandWake();
			return id;
		}
		impl_->input.CancelReservedCommandWake();
		impl_->shortcuts.Retire(id);
		return 0;
	}

	bool Host::TakeShortcutCompletion(std::uint64_t id, Shortcuts::Completion& result) noexcept
	{
		return impl_->shortcuts.Take(id, result);
	}
	void Host::RetireShortcutRequest(std::uint64_t id) noexcept { impl_->shortcuts.Retire(id); }
	std::uint64_t Host::AcquireShortcutLease(const Shortcuts::Context& expected,
		bool requireContentRevision) noexcept
	{
		std::scoped_lock lock(impl_->presentationTargetMutex);
		const auto current = impl_->ShortcutContextLocked();
		if (!current.available || current.scope != expected.scope) return 0;
		const auto id = impl_->shortcuts.Acquire(expected, requireContentRevision);
		if (id) impl_->input.SetShortcutAdmissionBlocked(true);
		return id;
	}
	void Host::ReleaseShortcutLease(std::uint64_t id) noexcept
	{
		std::scoped_lock lock(impl_->presentationTargetMutex);
		if (impl_->shortcuts.Release(id))
		{
			// 状态内核已发布 Selection，但绘制 owner 尚未应用时继续关门，避免 Clear 后插入新笔迹。
			impl_->shortcutSelectionHandoffPending =
				impl_->bridge.Snapshot().selectionMode != impl_->window.SelectionMode();
			impl_->input.SetShortcutAdmissionBlocked(impl_->shortcutSelectionHandoffPending);
		}
	}

	Bridge::CommandResult Host::PublishCommand(Bridge::CommandType command) noexcept
	{
		std::scoped_lock lock(impl_->commandPublishMutex);
		// 与 Acquire/Release 共用提交门；已接受的旧命令仍按原 FIFO 在 Clear/Fence 之前完成。
		std::scoped_lock sceneLock(impl_->presentationTargetMutex);
		if (!impl_->shortcuts.OrdinaryCommandAllowed(command)) return Bridge::CommandResult::QueueFull;
		if (!impl_->input.TryReserveCommandWake()) return Bridge::CommandResult::QueueFull;
		const Bridge::CommandResult result = impl_->bridge.Publish(command);
		if (result == Bridge::CommandResult::Accepted)
			impl_->input.PublishReservedCommandWake();
		else impl_->input.CancelReservedCommandWake();
		return result;
	}
}
