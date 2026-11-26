#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

#include <fmt/format.h>

// 统一日志服务：进程内唯一写文件线程；调用线程只做过滤、格式化和内存拷贝，不做文件 I/O。
// 本头文件供传统 .cpp 与 module 的全局模块片段直接 include。
namespace Inkeys::Log
{
	enum class Level : std::uint8_t { Debug, Info, Warn, Error, Critical };
	// 设置页档位；Off 只关闭普通分级日志，Critical、会话头与已开启通道仍写入。
	enum class Threshold : std::uint8_t { Debug, Info, Warn, Error, Off };

	// 模块标签：只表达“哪个子系统”，不再在消息里手写 [主线程][IdtMain] 式前缀。
	enum class Module : std::uint8_t
	{
		Main, Launch, Config, I18n, Window, UI3, Bar, Setting, Whiteboard,
		PageControl, SelectionOverlay, Freeze, Magnifier, Ppt, PptCOM,
		Draw3, Input, Haptics, Update, Plugin, State, Shutdown, Logging, UInk,
		Count
	};

	// 调试通道：对应设置页 6 个调试开关；开启即写入，不受 Threshold 过滤。
	enum class Channel : std::uint8_t { TouchArea, PptCOM, Draw3, Haptics, Cursor, CanvasPan, Count };

	// 会话头所需信息由调用方传入，日志服务不依赖 IdtMain 的全局变量。
	struct SessionInfo
	{
		std::wstring rootPath;           // 程序根目录（末尾带反斜杠）；日志位于 rootPath\Inkeys\Logs
		std::wstring editionDate;
		std::wstring editionVersion;
		std::wstring buildTime;
		std::wstring buildConfiguration; // Release / Dev，由调用方按 IDT_RELEASE 决定
		std::wstring architecture;       // 程序架构
		std::wstring targetArchitecture; // 系统架构
		std::wstring osVersion;
		std::wstring userId;
		std::wstring launch;             // 启动参数与启动状态摘要
		Threshold threshold = Threshold::Info; // 读取配置前的临时阈值；正式值由 ApplyConfig 设置
	};

	bool Enabled(Level level) noexcept;            // 原子读阈值；未通过时调用方不格式化
	bool ChannelEnabled(Channel channel) noexcept; // 原子读通道掩码
	void SetThreshold(Threshold threshold) noexcept;
	void SetChannelMask(std::uint32_t mask) noexcept;

	// 热路径：只尝试一次取锁；拿不到锁、缓冲不足或被过滤即返回 false（前两者计入丢弃计数）。
	bool TryWrite(Level level, Module module, std::optional<Channel> channel, std::string_view text) noexcept;
	// 调用方在提交前格式化失败时，记入同一丢弃计数。
	void RecordDropped(Level level) noexcept;

	bool Start(const SessionInfo& info) noexcept; // 建目录、打开文件、写会话头、启动写线程；不执行清理
	// 读取配置后调用一次：设置阈值/通道/清理参数，写一行不受阈值影响的 session.config，再投递启动清理。
	void ApplyConfig(Threshold threshold, int retentionDays, int maxSizeMb, std::uint32_t channelMask) noexcept;
	void FlushAndStop() noexcept;                 // 正常退出：停止接收、排空并落盘
	void EmergencyFlush() noexcept;               // 崩溃处理：有界时间内尽力排空，失败即放弃
	void ApplyRetention(int days, int maxSizeMb) noexcept; // 投递到写线程执行一次清理
	void EnterDiscardMode() noexcept;             // 只接受调用、不缓存不写文件（测试夹具/启动失败）
	bool Started() noexcept;                      // 是否已有写线程在写文件
	std::wstring Directory();                     // 日志目录（Start 前返回空串）

#ifdef DRAW3_TESTING
	// 仅供无窗口测试：在生产者线程观察已格式化的 UInk 消息，回调须存活到 worker 退出。
	using UInkTestSink = void (*)(Level, std::string_view) noexcept;
	void SetUInkTestSink(UInkTestSink sink) noexcept;
#endif

	namespace Detail
	{
		void Submit(Level level, Module module, std::uint8_t channel, bool tryOnly,
			fmt::string_view format, fmt::format_args args) noexcept;
		void SubmitWide(Level level, Module module, std::uint8_t channel, bool tryOnly,
			std::wstring_view text) noexcept;
		inline constexpr std::uint8_t NoChannel = 0xFF;
	}

	// 普通分级日志：阈值过滤后格式化；取锁只等待内存拷贝，不等待磁盘。
	template <class... Args>
	void Write(Level level, Module module, fmt::format_string<Args...> format, Args&&... args) noexcept
	{
		if (!Enabled(level)) return;
		Detail::Submit(level, module, Detail::NoChannel, false, format.get(), fmt::make_format_args(args...));
	}
	inline void Write(Level level, Module module, std::wstring_view text) noexcept
	{
		if (!Enabled(level)) return;
		Detail::SubmitWide(level, module, Detail::NoChannel, false, text);
	}

	template <class... Args>
	void Debug(Module module, fmt::format_string<Args...> format, Args&&... args) noexcept
	{
		Write(Level::Debug, module, format, std::forward<Args>(args)...);
	}
	template <class... Args>
	void Info(Module module, fmt::format_string<Args...> format, Args&&... args) noexcept
	{
		Write(Level::Info, module, format, std::forward<Args>(args)...);
	}
	template <class... Args>
	void Warn(Module module, fmt::format_string<Args...> format, Args&&... args) noexcept
	{
		Write(Level::Warn, module, format, std::forward<Args>(args)...);
	}
	template <class... Args>
	void Error(Module module, fmt::format_string<Args...> format, Args&&... args) noexcept
	{
		Write(Level::Error, module, format, std::forward<Args>(args)...);
	}
	// Critical 不受阈值影响。
	template <class... Args>
	void Critical(Module module, fmt::format_string<Args...> format, Args&&... args) noexcept
	{
		Write(Level::Critical, module, format, std::forward<Args>(args)...);
	}

	inline void Debug(Module module, std::wstring_view text) noexcept { Write(Level::Debug, module, text); }
	inline void Info(Module module, std::wstring_view text) noexcept { Write(Level::Info, module, text); }
	inline void Warn(Module module, std::wstring_view text) noexcept { Write(Level::Warn, module, text); }
	inline void Error(Module module, std::wstring_view text) noexcept { Write(Level::Error, module, text); }
	inline void Critical(Module module, std::wstring_view text) noexcept { Write(Level::Critical, module, text); }

	// 热路径的普通分级日志（输入/渲染线程）：阈值过滤后格式化，只尝试一次取锁，拿不到即丢弃计数。
	template <class... Args>
	void TryFormat(Level level, Module module, fmt::format_string<Args...> format, Args&&... args) noexcept
	{
		if (!Enabled(level)) return;
		Detail::Submit(level, module, Detail::NoChannel, true, format.get(), fmt::make_format_args(args...));
	}

	// 调试通道：开关开启即写入，不受阈值影响；通道多来自热路径，因此只做非阻塞投递。
	template <class... Args>
	void Trace(Channel channel, Module module, Level level, fmt::format_string<Args...> format, Args&&... args) noexcept
	{
		if (!ChannelEnabled(channel)) return;
		Detail::Submit(level, module, static_cast<std::uint8_t>(channel), true, format.get(), fmt::make_format_args(args...));
	}
}
