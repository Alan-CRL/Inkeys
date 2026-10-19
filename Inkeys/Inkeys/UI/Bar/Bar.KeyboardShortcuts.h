#pragma once

// 仅由 Bar.Interaction.cpp 在 module import 后包含；所有业务状态归交互线程。
namespace
{
	namespace Keyboard = Inkeys::Input::KeyboardShortcuts;
	namespace ShortcutCanvas = Inkeys::Drawing::Draw3;

	struct BarShortcutBackend
	{
		static auto Capture() { return ShortcutCanvas::CaptureProductShortcutContext(); }
		static auto Publish(ShortcutCanvas::Bridge::CommandType type,
			const ShortcutCanvas::Shortcuts::Scope& scope, std::uint64_t lease = 0)
		{ return ShortcutCanvas::PublishProductShortcutCommand(type, scope, lease); }
		static auto Fence(const ShortcutCanvas::Shortcuts::Context& expected, std::uint64_t lease)
		{ return ShortcutCanvas::PublishProductShortcutFence(expected, lease); }
		static bool Take(std::uint64_t id, ShortcutCanvas::Shortcuts::Completion& result)
		{ return ShortcutCanvas::TryTakeProductShortcutCompletion(id, result); }
		static void Retire(std::uint64_t id) { ShortcutCanvas::RetireProductShortcutRequest(id); }
		static auto Acquire(const ShortcutCanvas::Shortcuts::Context& expected, bool content = false)
		{ return ShortcutCanvas::AcquireProductShortcutLease(expected, content); }
		static void Release(std::uint64_t id) { ShortcutCanvas::ReleaseProductShortcutLease(id); }
		static auto Gesture() { return Keyboard::GetGestureState(); }
		static void UpdateUi()
		{
			barUISet.CollapseAuxiliaryPanels(true);
			barUISet.UpdateRendering();
		}
	};

	template<class Backend = BarShortcutBackend>
	class BarKeyboardShortcutOwner
	{
		using Context = ShortcutCanvas::Shortcuts::Context;
		using Completion = ShortcutCanvas::Shortcuts::Completion;
		using Command = Keyboard::Command;
		struct Pending
		{
			Keyboard::Intent intent{};
			std::uint64_t request = 0, lease = 0, freezeRevision = 0;
			bool firstDelete = false, select = false, toggle = false;
		} pending;
		struct DeleteCandidate
		{
			Keyboard::Intent intent{};
			Context cleared{};
			bool valid = false, ready = false;
		} candidate;
		std::deque<Keyboard::Intent> queue;
		Context context{};
		std::uint64_t scene = 1, generation = 1, ownRevision = 0;
		std::uint64_t chainSourceRevision = 0;
		std::uint64_t foreground = 0, configGeneration = 0, lifecycle = 0;
		bool textInput = false, modal = false, initialized = false;
		std::uint64_t queueDropped = 0, reportedInputDrops = 0, reportedOwnerDrops = 0;
		std::uint32_t lastDropReportTick = 0;

		static bool Success(const Completion& completion) noexcept
		{
			return completion.result == ShortcutCanvas::Shortcuts::Result::Applied ||
				completion.result == ShortcutCanvas::Shortcuts::Result::NoContent;
		}
		void CancelPending()
		{
			if (pending.request) Backend::Retire(pending.request);
			if (pending.lease) Backend::Release(pending.lease);
			pending = {};
		}
		void Retire()
		{
			CancelPending();
			candidate = {};
			queue.clear();
			chainSourceRevision = 0;
		}
		bool Current(const Keyboard::Intent& intent, bool allowToggleChain = false,
			bool requireAvailable = true) const
		{
			if (intent.lifecycle != lifecycle || intent.sceneScope != scene ||
				intent.contextGeneration != generation || intent.foreground != foreground ||
				intent.configGeneration != configGeneration || intent.serviceGeneration != lifecycle || modal ||
				(requireAvailable && !context.available))
				return false;
			const auto revision = StateModeTransitionRevision();
			return intent.modeRevision == revision || (allowToggleChain && chainSourceRevision &&
				intent.modeRevision == chainSourceRevision && ownRevision == revision);
		}
		bool SameDelete(const Keyboard::Intent& intent) const
		{
			return candidate.valid && intent.command == Command::Clear && !intent.repeat &&
				intent.ctrlSessionId == candidate.intent.ctrlSessionId &&
				intent.gestureGeneration == candidate.intent.gestureGeneration &&
				intent.pressId != candidate.intent.pressId && Current(intent);
		}
		bool Select(std::uint64_t revision, std::uint64_t freezeRevision)
		{
			if (!ChangeStateModeForShortcutIfRevision(false, revision, freezeRevision)) return false;
			ownRevision = StateModeTransitionRevision();
			Backend::UpdateUi();
			return true;
		}
		bool FinishSecondDelete(const Context& cleared, const Keyboard::Intent& intent)
		{
			const auto gesture = Backend::Gesture();
			if (!Current(intent) || !gesture.valid ||
				gesture.interruptionGeneration != intent.interruptionGeneration) return false;
			// 第二击只验证具名 Clear 后的空态，不再 Clear，避免覆盖清空恢复点。
			const auto lease = Backend::Acquire(cleared, true);
			if (!lease) return false;
			const auto request = Backend::Fence(cleared, lease);
			if (!request) { Backend::Release(lease); return false; }
			pending = {};
			pending.intent = intent;
			pending.request = request;
			pending.lease = lease;
			pending.freezeRevision = Inkeys::UI::Freeze::Snapshot().revision;
			pending.firstDelete = pending.select = true;
			return true;
		}

		void CompletePending()
		{
			if (!pending.request) return;
			Completion completion;
			if (!Backend::Take(pending.request, completion)) return;
			const auto completed = pending;
			pending = {};
			const bool valid = Success(completion) && Current(completed.intent) &&
				completion.resultContext.scope == context.scope;
			bool selected = false;
			const auto gesture = Backend::Gesture();
			const bool interrupted = completed.firstDelete && (!gesture.valid ||
				gesture.interruptionGeneration != completed.intent.interruptionGeneration);
			if (valid && completed.select && !interrupted)
			{
				if (completed.lease)
					selected = Select(completed.intent.modeRevision, completed.freezeRevision);
				else selected = FinishSecondDelete(completion.resultContext, completed.intent);
			}
			if (completed.lease) Backend::Release(completed.lease);
			if (completed.firstDelete)
			{
				if (valid && !completed.select && candidate.valid)
				{
					candidate.cleared = completion.resultContext;
					candidate.ready = true;
				}
				else candidate = {};
			}
			if (completed.toggle && (!valid || !selected))
			{
				// 后续往返依赖这次退出完成；失败不能把后续 X 解释成又一次清空。
				queue.clear();
				chainSourceRevision = 0;
			}
		}
		bool BeginClear(const Keyboard::Intent& intent, bool select, bool toggle)
		{
			pending = {};
			pending.intent = intent;
			pending.select = select;
			pending.toggle = toggle;
			pending.firstDelete = !toggle;
			pending.freezeRevision = Inkeys::UI::Freeze::Snapshot().revision;
			if (select)
			{
				pending.lease = Backend::Acquire(context);
				if (!pending.lease) { pending = {}; candidate = {}; return false; }
			}
			pending.request = Backend::Publish(
				ShortcutCanvas::Bridge::CommandType::Clear, context.scope, pending.lease);
			if (!pending.request) { CancelPending(); candidate = {}; return false; }
			return true;
		}
		void Dispatch(Keyboard::Intent intent)
		{
			const bool toggle = intent.command == Command::ToggleAnnotation;
			if (!Current(intent, toggle) || (!toggle && textInput)) return;
			const auto mode = GetStateModeVersionedSnapshot();
			if (toggle)
			{
				if (!chainSourceRevision) chainSourceRevision = intent.modeRevision;
				intent.modeRevision = mode.revision;
				candidate = {};
				const auto freeze = Inkeys::UI::Freeze::Snapshot();
				if (IsComputerInteractionMode(mode.state.StateModeSelect))
				{
					if (ChangeStateModeForShortcutIfRevision(true, mode.revision, freeze.revision))
					{
						ownRevision = StateModeTransitionRevision();
						Backend::UpdateUi();
					}
				}
				else if (GetSelectAndClickThroughMode() == 1)
				{
					if (!BeginClear(intent, true, true))
					{
						queue.clear();
						chainSourceRevision = 0;
					}
				}
				else (void)Select(mode.revision, freeze.revision);
				return;
			}
			if (IsComputerInteractionMode(mode.state.StateModeSelect)) return;
			if (intent.command == Command::Clear)
			{
				if (SameDelete(intent) && candidate.ready)
				{
					(void)FinishSecondDelete(candidate.cleared, intent);
					candidate = {};
					return;
				}
				candidate = { intent, {}, true, false };
				(void)BeginClear(intent, false, false);
				return;
			}
			candidate = {};
			chainSourceRevision = 0;
			if (intent.command == Command::Freeze)
			{
				(void)ToggleFreezeForShortcutIfRevision(mode.revision);
				Backend::UpdateUi();
				return;
			}
			const auto command = intent.command == Command::Undo
				? ShortcutCanvas::Bridge::CommandType::Undo : ShortcutCanvas::Bridge::CommandType::Redo;
			pending.intent = intent;
			pending.request = Backend::Publish(command, context.scope);
		}

		void RefreshCanvas()
		{
			const auto next = Backend::Capture();
			if (next.scope != context.scope)
			{
				Retire();
				++scene;
			}
			context = next;
			// 必须在认领下一次 Delete 前退休候选；新墨迹后的 Delete 是一次新的清空。
			if (candidate.valid && candidate.ready && (context.scope != candidate.cleared.scope ||
				context.contentRevision != candidate.cleared.contentRevision)) candidate = {};
		}

		void Drain()
		{
			Keyboard::Intent intent;
			for (unsigned i = 0; i < 16; ++i)
			{
				RefreshCanvas();
				// 两个 Down 已入队时先形成复合意图，再观察 Ctrl Up；松 Ctrl 不撤销已形成第二步。
				if (pending.request && pending.firstDelete && !pending.select &&
					!queue.empty() && SameDelete(queue.front()))
				{
					pending.select = true;
					queue.pop_front();
				}
				CompletePending();
				if (pending.request || queue.empty()) break;
				RefreshCanvas();
				// Selection 的 Host 交接尚未完成时保留后继 X，等同一作用域重新开放。
				if (!context.available || queue.empty()) break;
				intent = queue.front();
				queue.pop_front();
				Dispatch(intent);
			}
			const auto gesture = Backend::Gesture();
			if (gesture.valid && candidate.valid && !(pending.request && pending.select) &&
				(gesture.ctrlSessionId != candidate.intent.ctrlSessionId ||
				gesture.gestureGeneration != candidate.intent.gestureGeneration || !gesture.controlDown ||
				(candidate.ready && (context.scope != candidate.cleared.scope ||
					context.contentRevision != candidate.cleared.contentRevision)))) candidate = {};
			if (!pending.request && queue.empty()) chainSourceRevision = 0;
		}

	public:
		// 嵌套等待只收包。切工具/关闭面板必须在 Run 顶层、旧手势退栈之后执行。
		bool Poll(bool dispatch)
		{
			const auto next = Backend::Capture();
			const auto status = Keyboard::GetStatus();
			const auto mode = GetStateModeVersionedSnapshot();
			const auto nextForeground = reinterpret_cast<std::uint64_t>(GetForegroundWindow());
			const HWND setting = Inkeys::Window::GetService().Handle(Inkeys::Window::WindowRole::Setting);
			const HWND active = reinterpret_cast<HWND>(nextForeground);
			const bool nextTextInput = setting && (active == setting || IsChild(setting, active));
			const bool nextModal = offSignal || Inkeys::UI::MessageBox::IsShowing() ||
				WhiteboardTransactionActive();
			const bool changedScene = initialized && next.scope != context.scope;
			const bool changedRoute = initialized && (foreground != nextForeground ||
				textInput != nextTextInput || modal != nextModal ||
				configGeneration != status.configGeneration || lifecycle != status.lifecycleGeneration);
			const bool externalMode = initialized && ownRevision != mode.revision;
			if (changedScene || changedRoute || externalMode)
			{
				Retire();
				if (changedScene) ++scene;
				if (changedRoute) ++generation;
			}
			context = next;
			foreground = nextForeground;
			textInput = nextTextInput;
			modal = nextModal;
			configGeneration = status.configGeneration;
			lifecycle = status.lifecycleGeneration;
			ownRevision = mode.revision;
			initialized = true;
			Keyboard::PublishRoute({ lifecycle, mode.revision, scene, generation, foreground,
				GetTickCount(), !IsComputerInteractionMode(mode.state.StateModeSelect), textInput,
				modal, context.available && !offSignal });
			Keyboard::Intent intent;
			for (unsigned i = 0; i < 16 && Keyboard::TryPop(intent); ++i)
				if (Current(intent, intent.command == Command::ToggleAnnotation, false))
				{
					if (queue.size() < 128) queue.push_back(intent);
					else ++queueDropped;
				}
			const auto now = GetTickCount();
			if ((status.dropped != reportedInputDrops || queueDropped != reportedOwnerDrops) &&
				now - lastDropReportTick >= 1000)
			{
				// 饱和诊断仅在普通 owner 限频汇总，回调内不格式化或写日志。
				if (IDTLogger) IDTLogger->warn("[KeyboardShortcuts] 快捷键队列饱和: inputDropped={}, ownerDropped={}",
					status.dropped, queueDropped);
				reportedInputDrops = status.dropped;
				reportedOwnerDrops = queueDropped;
				lastDropReportTick = now;
			}
			if (!dispatch) return !queue.empty() || pending.request != 0;
			Drain();
			return !queue.empty() || pending.request != 0;
		}
		void Stop() { Retire(); initialized = false; }
		static int ProductionTest() noexcept;
	};
	// 无窗口探针复用同一个 owner/回执槽，仅把 Host 异步边界替换为可控完成点。
	struct BarShortcutTestBackend
	{
		inline static ShortcutCanvas::Shortcuts::State state;
		inline static Keyboard::GestureState gesture;
		inline static std::uint64_t request = 0;
		inline static unsigned clears = 0, undos = 0, redos = 0;
		inline static bool pauseOnRelease = false;
		static void Reset()
		{
			state.BeginHost();
			ShortcutCanvas::Shortcuts::Context context;
			context.available = true;
			context.hasContent = true;
			context.scope.pageEpoch = 1;
			state.Publish(context, 1);
			gesture = { 1, 1, 1, true, true };
			request = 0;
			clears = undos = redos = 0;
			pauseOnRelease = false;
		}
		static auto Capture() { return state.Snapshot(); }
		static auto Publish(ShortcutCanvas::Bridge::CommandType type,
			const ShortcutCanvas::Shortcuts::Scope& scope, std::uint64_t = 0)
		{
			request = state.Reserve(scope);
			if (request)
			{
				if (type == ShortcutCanvas::Bridge::CommandType::Clear) ++clears;
				else if (type == ShortcutCanvas::Bridge::CommandType::Undo) ++undos;
				else if (type == ShortcutCanvas::Bridge::CommandType::Redo) ++redos;
			}
			return request;
		}
		static auto Fence(const ShortcutCanvas::Shortcuts::Context& expected, std::uint64_t)
		{
			if (Capture().contentRevision != expected.contentRevision) return std::uint64_t{0};
			return request = state.Reserve(expected.scope);
		}
		static bool Take(std::uint64_t id, ShortcutCanvas::Shortcuts::Completion& result)
		{ return state.Take(id, result); }
		static void Retire(std::uint64_t id) { state.Retire(id); }
		static auto Acquire(const ShortcutCanvas::Shortcuts::Context& expected, bool content = false)
		{ return state.Acquire(expected, content); }
		static void Release(std::uint64_t id)
		{
			if (state.Release(id) && pauseOnRelease)
			{
				auto context = Capture();
				context.available = false;
				state.Publish(context, context.scope.intentEpoch);
			}
		}
		static auto Gesture() { return gesture; }
		static void UpdateUi() {}
		static void Finish(ShortcutCanvas::Shortcuts::Result result)
		{
			auto context = Capture();
			if (result == ShortcutCanvas::Shortcuts::Result::Applied ||
				result == ShortcutCanvas::Shortcuts::Result::NoContent)
			{
				if (result == ShortcutCanvas::Shortcuts::Result::Applied) ++context.contentRevision;
				context.hasContent = false;
				state.Publish(context, context.scope.intentEpoch);
			}
			state.Complete(request, result);
		}
	};

	template<class Backend>
	int BarKeyboardShortcutOwner<Backend>::ProductionTest() noexcept
	{
		try
		{
			const auto require = [](bool ok, const char* check) { if (!ok) throw check; };
			const auto seed = [](BarKeyboardShortcutOwner& owner)
			{
				Backend::Reset();
				ChangeStateModeToPenTool(PenToolSelectionEnum::Laser);
				owner.context = Backend::Capture();
				owner.lifecycle = owner.configGeneration = owner.foreground = 1;
				owner.ownRevision = StateModeTransitionRevision();
				owner.initialized = true;
			};
			const auto input = [](const BarKeyboardShortcutOwner& owner, Command command, std::uint64_t press)
			{
				Keyboard::Intent result;
				result.command = command;
				result.lifecycle = result.serviceGeneration = result.configGeneration = result.foreground = 1;
				result.modeRevision = StateModeTransitionRevision();
				result.sceneScope = owner.scene;
				result.contextGeneration = owner.generation;
				result.pressId = press;
				result.ctrlSessionId = result.gestureGeneration = result.interruptionGeneration = 1;
				return result;
			};
			using Result = ShortcutCanvas::Shortcuts::Result;
			(void)SetSelectAndClickThroughMode(0);
			{
				BarKeyboardShortcutOwner owner;
				seed(owner);
				const auto first = input(owner, Command::ToggleAnnotation, 1);
				owner.queue.push_back(first);
				owner.queue.push_back(first);
				owner.Drain();
				require(GetStateModeSnapshot().StateModeSelect == StateModeSelectEnum::IdtPen &&
					IsLaserToolActive() && Backend::clears == 0, "combined-two-x-restore-without-clear");
			}
			(void)SetSelectAndClickThroughMode(1);
			{
				BarKeyboardShortcutOwner owner;
				seed(owner);
				auto first = input(owner, Command::ToggleAnnotation, 1);
				owner.queue.push_back(first);
				owner.Drain();
				require(owner.pending.request && Backend::state.Lease() && Backend::clears == 1 &&
					GetStateModeSnapshot().StateModeSelect == StateModeSelectEnum::IdtPen,
					"separated-x-waits-for-real-clear");
				first.pressId = 2;
				owner.queue.push_back(first);
				Backend::Finish(Result::Applied);
				owner.Drain();
				require(!owner.pending.request && !Backend::state.Lease() && Backend::clears == 1 &&
					IsLaserToolActive(), "separated-two-x-serialize-clear-select-restore");
			}
			{
				BarKeyboardShortcutOwner owner;
				seed(owner);
				const auto first = input(owner, Command::ToggleAnnotation, 1);
				owner.queue.push_back(first);
				owner.Drain();
				owner.queue.push_back(first);
				Backend::pauseOnRelease = true;
				Backend::Finish(Result::Applied);
				owner.Drain();
				require(GetStateModeSnapshot().StateModeSelect == StateModeSelectEnum::IdtSelection &&
					owner.queue.size() == 1, "dependent-x-waits-selection-handoff");
				auto ready = Backend::Capture();
				ready.available = true;
				Backend::state.Publish(ready, ready.scope.intentEpoch);
				owner.Drain();
				require(IsLaserToolActive() && owner.queue.empty() && Backend::clears == 1,
					"dependent-x-resumes-after-selection-handoff");
			}
			for (const auto result : { Result::Applied, Result::NoContent })
			{
				BarKeyboardShortcutOwner owner;
				seed(owner);
				const auto first = input(owner, Command::Clear, 1);
				owner.queue.push_back(first);
				owner.Drain();
				require(owner.pending.request && !Backend::state.Lease(), "single-delete-does-not-block-drawing");
				auto second = first;
				second.pressId = 2;
				owner.queue.push_back(second);
				Backend::gesture.controlDown = false;
				++Backend::gesture.gestureGeneration;
				Backend::Finish(result);
				owner.Drain();
				require(owner.pending.request && Backend::state.Lease() && IsLaserToolActive(),
					"second-delete-waits-controller-fence");
				Backend::Finish(Result::NoContent);
				owner.Drain();
				require(GetStateModeSnapshot().StateModeSelect == StateModeSelectEnum::IdtSelection &&
					Backend::clears == 1 && !Backend::state.Lease(), "second-delete-survives-ctrl-up-no-second-clear");
			}
			for (const auto result : { Result::Blocked, Result::Stale, Result::Failed })
			{
				BarKeyboardShortcutOwner owner;
				seed(owner);
				const auto first = input(owner, Command::ToggleAnnotation, 1);
				owner.queue.push_back(first);
				owner.Drain();
				owner.queue.push_back(first);
				Backend::Finish(result);
				owner.Drain();
				require(IsLaserToolActive() && Backend::clears == 1 && owner.queue.empty() &&
					!Backend::state.Lease(), "failed-x-retires-dependent-toggle-and-own-lease");
			}
			{
				BarKeyboardShortcutOwner owner;
				seed(owner);
				auto first = input(owner, Command::Clear, 1);
				owner.queue.push_back(first);
				owner.Drain();
				Backend::Finish(Result::Applied);
				owner.Drain();
				auto changed = Backend::Capture();
				++changed.contentRevision;
				changed.hasContent = true;
				Backend::state.Publish(changed, changed.scope.intentEpoch);
				first.pressId = 2;
				owner.queue.push_back(first);
				owner.Drain();
				require(IsLaserToolActive() && Backend::clears == 2 && owner.pending.request &&
					!Backend::state.Lease(), "same-poll-new-ink-delete-starts-fresh-clear");
				Backend::Finish(Result::Applied);
				owner.Drain();
			}
			{
				BarKeyboardShortcutOwner owner;
				seed(owner);
				auto first = input(owner, Command::Clear, 1);
				owner.queue.push_back(first);
				owner.Drain();
				first.pressId = 2;
				owner.queue.push_back(first);
				++Backend::gesture.interruptionGeneration;
				Backend::Finish(Result::Applied);
				owner.Drain();
				require(IsLaserToolActive() && Backend::clears == 1, "another-terminal-retires-formed-second-delete");
			}
			{
				BarKeyboardShortcutOwner owner;
				seed(owner);
				owner.queue.push_back(input(owner, Command::Clear, 1));
				owner.Drain();
				Backend::gesture.controlDown = false;
				++Backend::gesture.gestureGeneration;
				owner.Drain();
				Backend::Finish(Result::Applied);
				owner.Drain();
				require(!owner.candidate.valid && IsLaserToolActive(), "ctrl-up-before-second-retires-candidate");
				owner.queue.push_back(input(owner, Command::Undo, 2));
				owner.Drain();
				require(Backend::undos == 1 && owner.pending.request,
					"clear-then-undo-uses-stable-page-not-content-generation");
				Backend::Finish(Result::Applied);
				owner.Drain();
			}
			{
				BarKeyboardShortcutOwner owner;
				seed(owner);
				owner.queue.push_back(input(owner, Command::ToggleAnnotation, 1));
				owner.Drain();
				ChangeStateModeToShape();
				Backend::Finish(Result::Applied);
				owner.Drain();
				require(GetStateModeSnapshot().StateModeSelect == StateModeSelectEnum::IdtShape &&
					!Backend::state.Lease(), "late-clear-does-not-replace-new-tool");
			}
			{
				BarKeyboardShortcutOwner owner;
				seed(owner);
				owner.queue.push_back(input(owner, Command::ToggleAnnotation, 1));
				owner.Drain();
				auto changed = Backend::Capture();
				++changed.scope.pageEpoch;
				Backend::state.Publish(changed, changed.scope.intentEpoch);
				owner.context = Backend::Capture();
				++owner.scene;
				Backend::Finish(Result::Applied);
				owner.Drain();
				require(IsLaserToolActive() && !Backend::state.Lease(), "late-clear-does-not-select-new-page");
			}
			std::fputs("[ShortcutBusinessProduction] PASS: owner roundtrip, named clear, double Delete, leases, cancellation\n", stderr);
			return 0;
		}
		catch (const char* check)
		{ std::fprintf(stderr, "[ShortcutBusinessProduction] FAIL: %s\n", check); }
		catch (...) { std::fputs("[ShortcutBusinessProduction] FAIL: exception\n", stderr); }
		return 1;
	}

	BarKeyboardShortcutOwner<> barKeyboardShortcuts;
}
