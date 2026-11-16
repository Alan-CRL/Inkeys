#pragma once

#include <charconv>
#include <cstddef>
#include <cstdint>
#include <ios>
#include <ostream>
#include <string_view>
#include <type_traits>

#include "../../Log/Log.h"

namespace Inkeys::Drawing::Draw3::DiagnosticOutput
{
	inline constexpr std::size_t RecordCapacity = 1024;
	inline constexpr std::size_t TextCapacity = 4096;
	enum class Stream : unsigned char { Output, Error };
	enum class StopResult { Disabled, Stopped, IsolatedPending };
	// 产品默认写入统一日志（Log）；只有测试入口显式以 StdStream 启动 Session 时才走原队列写标准流。
	enum class Mode : unsigned char { Log, StdStream };
	inline constexpr std::uint8_t NoChannel = 0xFF;
	// 一条诊断的去向：标准流模式只使用 stream（测试协议字节不变）；日志模式使用等级、模块与调试通道。
	// 只给 Stream 时：Error 映射 Warn、Output 映射 Debug；调试通道记录不受日志等级过滤。
	struct Target
	{
		Stream stream = Stream::Output;
		::Inkeys::Log::Level level = ::Inkeys::Log::Level::Debug;
		::Inkeys::Log::Module module = ::Inkeys::Log::Module::Draw3;
		std::uint8_t channel = NoChannel;
		constexpr Target(Stream value = Stream::Output) noexcept
			: stream(value), level(value == Stream::Error ? ::Inkeys::Log::Level::Warn : ::Inkeys::Log::Level::Debug) {}
		constexpr Target(Stream value, ::Inkeys::Log::Level levelValue,
			::Inkeys::Log::Module moduleValue = ::Inkeys::Log::Module::Draw3) noexcept
			: stream(value), level(levelValue), module(moduleValue) {}
		constexpr Target(Stream value, ::Inkeys::Log::Channel channelValue,
			::Inkeys::Log::Module moduleValue = ::Inkeys::Log::Module::Draw3,
			::Inkeys::Log::Level levelValue = ::Inkeys::Log::Level::Debug) noexcept
			: stream(value), level(levelValue), module(moduleValue), channel(static_cast<std::uint8_t>(channelValue)) {}
	};
	// 常用的语义等级覆盖（Out*/Err* 表示标准流模式下仍写的原 stream）。
	inline constexpr Target OutInfo{ Stream::Output, ::Inkeys::Log::Level::Info };
	inline constexpr Target OutWarn{ Stream::Output, ::Inkeys::Log::Level::Warn };
	inline constexpr Target OutError{ Stream::Output, ::Inkeys::Log::Level::Error };
	inline constexpr Target ErrInfo{ Stream::Error, ::Inkeys::Log::Level::Info };
	inline constexpr Target ErrError{ Stream::Error, ::Inkeys::Log::Level::Error };
	// 诊断计数按无符号 64 位模数累计；不参与业务准入或持久化判断。
	struct Statistics
	{
		std::uint64_t accepted = 0, written = 0, droppedFull = 0;
		std::uint64_t droppedContention = 0, droppedClosing = 0, truncated = 0;
		std::uint64_t sinkErrors = 0, writeCalls = 0, cancelAttempts = 0;
		std::uint64_t cancelNotFound = 0;
		std::size_t queued = 0, peakQueued = 0;
		unsigned long workerThreadId = 0;
		bool writing = false;
	};

	bool Available(Target target = Stream::Output, bool mirror = false) noexcept;
	void Write(Target target, const char* text, std::size_t length, bool mirror = false) noexcept;
	void Text(Target target, const char* text, bool mirror = false) noexcept;
	void Printf(Target target, const char* format, ...) noexcept;
	void Wake() noexcept;

	class Session final
	{
	public:
		Session() noexcept = default;
		~Session();
		Session(const Session&) = delete;
		Session& operator=(const Session&) = delete;
		// Log：不建队列，按日志服务是否在写文件返回；StdStream：仅测试入口使用，复制当前 stdout/stderr。
		bool Start(Mode mode) noexcept;
		StopResult Stop() noexcept;
		Statistics Snapshot() const noexcept;
	private:
		bool StartHandles(void* output, void* error) noexcept;
		friend int RunProductionTest(void (*helpers)() noexcept) noexcept;
		void* state_ = nullptr;
		Statistics stopped_{};
		StopResult result_ = StopResult::Disabled;
	};

	// 定长拼接替代 cout：只保留原记录的数值格式，不建立 ostream 或动态缓冲。
	class Line final
	{
	public:
		explicit Line(Target target = Stream::Output, bool mirror = false) noexcept;
		~Line();
		Line(const Line&) = delete;
		Line& operator=(const Line&) = delete;
		Line& operator<<(const char* text) noexcept;
		Line& operator<<(char value) noexcept;
		Line& operator<<(std::string_view text) noexcept;
		Line& operator<<(std::ios_base& (*format)(std::ios_base&)) noexcept;
		Line& operator<<(std::ostream& (*format)(std::ostream&)) noexcept;
		template<class T> requires (std::is_arithmetic_v<T> || std::is_enum_v<T>)
		Line& operator<<(T value) noexcept
		{
			if (!enabled_) return *this;
			char number[128];
			if constexpr (std::is_enum_v<T>) return *this << static_cast<std::underlying_type_t<T>>(value);
			else if constexpr (std::is_same_v<T, bool>) return *this << (value ? '1' : '0');
			else if constexpr (std::is_floating_point_v<T>)
			{
				const auto result = std::to_chars(number, number + sizeof(number), value,
					std::chars_format::general, 6);
				if (result.ec == std::errc{}) Append(number, static_cast<std::size_t>(result.ptr - number));
			}
			else
			{
				// 与 ostream 十六进制一致：负整数按其无符号位模式表示。
				if (base_ == 16)
				{
					const auto result = std::to_chars(number, number + sizeof(number),
						static_cast<std::make_unsigned_t<T>>(value), 16);
					if (result.ec == std::errc{}) Append(number, static_cast<std::size_t>(result.ptr - number));
				}
				else
				{
					const auto result = std::to_chars(number, number + sizeof(number), value, 10);
					if (result.ec == std::errc{}) Append(number, static_cast<std::size_t>(result.ptr - number));
				}
			}
			return *this;
		}
		template<class T> requires requires(const T& text) { std::string_view(text.data(), text.size()); }
		Line& operator<<(const T& text) noexcept { return *this << std::string_view(text.data(), text.size()); }
	private:
		void Append(const char* text, std::size_t length) noexcept;
		char text_[TextCapacity];
		std::size_t length_ = 0;
		unsigned long previousError_ = 0;
		Target target_;
		int base_ = 10;
		bool mirror_ = false, enabled_ = false, truncated_ = false;
	};

	// 显式早退自检；harness 关闭时返回 2，不能进入普通窗口启动。
	int RunProductionTest(void (*helpers)() noexcept = nullptr) noexcept;
}
