module;

#include <Windows.h>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>

export module Inkeys.UI.UI3:HostInput;

// 宿主输入接入：一个有界邮箱、一个输入 owner。表面事件与宿主主内容的消息共用同一 FIFO，
// 保序、限批、溢出即取消；UI3 不解释主内容载荷，也不认识任何产品消息类型。
export namespace Inkeys::UI::UI3
{
	struct SurfaceInputEvent
	{
		HWND window = nullptr;
		UINT message = 0;
		WPARAM wParam = 0;
		LPARAM lParam = 0;
		ULONG_PTR extraInfo = 0;
		DWORD messageTime = 0;
		ULONGLONG tick = 0;
		POINT screenPoint{};
		POINT logicalPoint{};
		std::uint64_t generation = 0;
		std::uint64_t presentedRevision = 0;
		std::uint64_t semanticRevision = 0;
		std::uint64_t captureTicket = 0;
		std::uint64_t touchRegistration = 0, touchRouteGeneration = 0;
		std::uint64_t hitButtonId = 0;
		std::uint32_t contactId = 0;
		std::uint64_t contactSource = 0;
		double inputSeconds = 0;
		bool pen = false;
		bool touch = false;
		bool captureOwned = false;
		bool nativeCaptureReleased = false;
		bool logicalPointValid = false;
		bool backgroundHit = false;
		bool preserveMoveSamples = false; // 角度手势逐点累计，仍受有界邮箱的溢出取消保护。
		// 同一 HWND 内的内容实例；与 window 一起构成注册键。今天全部调用方恒为 0。
		std::uint64_t contentInstance = 0;
	};
	using SurfaceInputHandler = void(*)(const SurfaceInputEvent&, void*);
	using SurfaceInputTick = std::chrono::steady_clock::time_point(*)(std::chrono::steady_clock::time_point, void*);
	using SurfaceInputCancel = void(*)(const SurfaceInputEvent&, void*);
	// 注册键为「HWND + 内容实例」：同键的新注册使旧注册失效，注销只清理同键的注册与在途事件。
	bool RegisterInteractionSurface(HWND, std::uint64_t generation, SurfaceInputHandler,
		SurfaceInputTick, SurfaceInputCancel, void*, bool nativeWindow = true,
		std::uint64_t contentInstance = 0);
	void UnregisterInteractionSurface(HWND, std::uint64_t contentInstance = 0);
	[[nodiscard]] bool DrainInteractionSurfaceCallbacks() noexcept;
	bool SubmitSurfaceInput(const SurfaceInputEvent&);
	void WakeInteractionWorker() noexcept;
	// 组件动作交给宿主的业务队列执行；没有主内容处理器或处于离屏 worker 时拒绝。
	[[nodiscard]] bool QueueInteractionComponentAction(std::function<void()> action);
	class ScopedInteractionWorker;
	// 宿主允许且没有其他输入 owner 时才能取得；先注销/排空目标，再销毁 scope。
	[[nodiscard]] std::unique_ptr<ScopedInteractionWorker> CreateOffscreenInteractionWorker();
	class ScopedInteractionWorker final
	{
	public:
		~ScopedInteractionWorker();
		ScopedInteractionWorker(const ScopedInteractionWorker&) = delete;
		ScopedInteractionWorker& operator=(const ScopedInteractionWorker&) = delete;
	private:
		struct State;
		ScopedInteractionWorker();
		std::unique_ptr<State> state_;
		friend std::unique_ptr<ScopedInteractionWorker> CreateOffscreenInteractionWorker();
	};

	// 主内容处理器：worker 泵对宿主主内容（今天是主栏）做的每件事各对应一个回调。
	// context 是宿主的不透明续接上下文：生产 worker 与测试由调用方传给泵，离屏 worker 经 beginWorker 取得。
	// 回调均在输入 owner 线程执行（queueComponentAction、offscreenWorkerAllowed、resetPending 除外）。
	struct MainContentHandler
	{
		using Clock = std::chrono::steady_clock;
		// 处理一条主内容消息；载荷就是入队时交来的那一份。
		void (*dispatch)(std::shared_ptr<void> payload, void* context) = nullptr;
		// 每条信封（含表面事件）之后及批末尝试续接主手势；返回是否推进了一步。
		bool (*resume)(void* context, Clock::time_point now) = nullptr;
		// 主手势已取消：本批停止取件，先让宿主重建续接。
		bool (*cancelled)(void* context) = nullptr;
		// 邮箱溢出：丢弃暂存的主输入并取消已准入的主捕获（先于表面取消）。
		void (*overflow)(void* context) = nullptr;
		// 主内容的时间事件；返回下一次需要唤醒的时刻。
		Clock::time_point (*tick)(Clock::time_point now) = nullptr;
		// 当前续接等待的截止时间。
		Clock::time_point (*deadline)(void* context) = nullptr;
		// 离屏 worker 线程内建立/销毁续接上下文。
		void* (*beginWorker)() = nullptr;
		void (*endWorker)(void* context) = nullptr;
		// 建立离屏 worker 前清空宿主暂存的主输入。
		void (*resetPending)() = nullptr;
		// 宿主的生产窗口/画布在运行或正在退出时不允许建立离屏 worker。
		bool (*offscreenWorkerAllowed)() = nullptr;
		bool (*queueComponentAction)(std::function<void()> action) = nullptr;
	};
	// 同一时刻只有一个主内容处理器；已有处理器或缺少必需回调时返回 false。
	[[nodiscard]] bool RegisterMainContentHandler(const MainContentHandler&);
	void UnregisterMainContentHandler() noexcept;
	// 主内容消息进入共享邮箱。两个回调在邮箱锁内执行：canCoalesce 只在前后两条都是主内容消息时
	// 询问能否合并；admit 在确定准入后调用，返回 false 表示放弃入队。
	using MainContentCoalesce = bool(*)(const void* previous, const void* next);
	using MainContentAdmit = bool(*)(const void* admitted);
	bool SubmitMainContentInput(std::shared_ptr<void> payload, MainContentCoalesce canCoalesce,
		MainContentAdmit admit);

	// 生产输入 owner：循环留在宿主线程，所有权、等待与泵由这里提供。
	[[nodiscard]] bool BeginInteractionWorker() noexcept;
	void EndInteractionWorker() noexcept;
	[[nodiscard]] bool InteractionWorkerRunning() noexcept;
	[[nodiscard]] bool OffscreenInteractionWorkerActive() noexcept;
	[[nodiscard]] std::uint64_t InteractionInputRevision() noexcept;
	void WaitInteractionInput(std::chrono::steady_clock::time_point deadline, std::uint64_t observed);
	// 每批最多取 32 条；返回下一次需要唤醒的时刻。mainContext 为空时不派发主内容。
	std::chrono::steady_clock::time_point PumpInteractionWorker(void* mainContext,
		std::chrono::steady_clock::time_point now);
	void CancelInteractionSurfaces();

	// 仅供无窗口回归：观察取消屏障与注册回收，并在测试结束时清空共享状态。
	[[nodiscard]] bool InteractionInputOverflowPending() noexcept;
	[[nodiscard]] std::size_t InteractionSurfaceRegistrationCount();
	void ResetInteractionInputForTesting();

	// 表面输入被拒绝（注册代次变化、没有活动注册）时的诊断出口。指针诊断通道属于产品输入层，
	// UI3 不依赖它：宿主登记后由宿主输出。stage/reason 是静态字符串，detail 是已格式化的键值文本。
	// enabled 由宿主回答诊断当前是否开启：关闭时 UI3 不格式化文本（为空表示始终输出）。
	using SurfaceInputDiagnosticSink = void(*)(HWND window, const char* stage, const char* reason,
		UINT message, const char* detail) noexcept;
	using SurfaceInputDiagnosticEnabled = bool(*)() noexcept;
	void SetSurfaceInputDiagnosticSink(SurfaceInputDiagnosticSink sink,
		SurfaceInputDiagnosticEnabled enabled = nullptr) noexcept;
}
