#pragma once

#include <charconv>
#include <cstddef>
#include <cstdint>
#include <ios>
#include <ostream>
#include <string_view>
#include <type_traits>

namespace Inkeys::Drawing::Draw3::DiagnosticOutput
{
	inline constexpr std::size_t RecordCapacity = 1024;
	inline constexpr std::size_t TextCapacity = 4096;
	enum class Stream : unsigned char { Output, Error };
	enum class StopResult { Disabled, Stopped, IsolatedPending };
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

	bool Available(Stream stream = Stream::Output, bool mirror = false) noexcept;
	void Write(Stream stream, const char* text, std::size_t length, bool mirror = false) noexcept;
	void Text(Stream stream, const char* text, bool mirror = false) noexcept;
	void Printf(Stream stream, const char* format, ...) noexcept;
	void Wake() noexcept;

	class Session final
	{
	public:
		Session() noexcept = default;
		~Session();
		Session(const Session&) = delete;
		Session& operator=(const Session&) = delete;
		bool Start() noexcept;
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
		explicit Line(Stream stream = Stream::Output, bool mirror = false) noexcept;
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
		Stream stream_;
		int base_ = 10;
		bool mirror_ = false, enabled_ = false, truncated_ = false;
	};

	// 显式早退自检；harness 关闭时返回 2，不能进入普通窗口启动。
	int RunProductionTest(void (*helpers)() noexcept = nullptr) noexcept;
}
