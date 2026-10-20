#pragma once

#include <atomic>
#include <cstdio>
#include <cstdint>
#include <cstring>

namespace Inkeys::Drawing::Draw3::HapticDiagnostics
{
	inline std::atomic_bool enabled = false;
	inline std::atomic<std::uint64_t> generation = 0;
	inline bool Enabled() noexcept { return enabled.load(std::memory_order_relaxed); }
	inline void SetEnabled(bool value) noexcept
	{
		if (enabled.exchange(value, std::memory_order_relaxed) != value)
			generation.fetch_add(1, std::memory_order_relaxed);
	}
	struct ProductMessageOutputScope;
	inline thread_local ProductMessageOutputScope* productMessageOutput = nullptr;
	// 只暂存产品消息闸门内的少量日志；嵌套消息直接写入最外层，解锁后统一输出。
	struct ProductMessageOutputScope
	{
		ProductMessageOutputScope() noexcept
		{
			if (!Enabled() || productMessageOutput) return;
			count = dropped = 0;
			productMessageOutput = this;
			active = true;
		}
		ProductMessageOutputScope(const ProductMessageOutputScope&) = delete;
		ProductMessageOutputScope& operator=(const ProductMessageOutputScope&) = delete;
		~ProductMessageOutputScope() noexcept
		{
			if (!active) return;
			productMessageOutput = nullptr;
			for (unsigned i = 0; i < count; ++i)
				std::fwrite(lines[i], 1, lengths[i], stderr);
			if (dropped)
			{
				char line[128];
				const int length = std::snprintf(line, sizeof(line),
					"[PenHaptics] event=log_drop scope=product_message count=%u\n", dropped);
				if (length > 0) std::fwrite(line, 1, static_cast<std::size_t>(length), stderr);
			}
		}
		void Append(const char* line, std::size_t length) noexcept
		{
			if (count == 4) { ++dropped; return; }
			std::memcpy(lines[count], line, length);
			lengths[count++] = length;
		}
	private:
		// 故意不初始化大缓冲；关闭诊断时不触碰缓冲或 TLS。
		char lines[4][512];
		std::size_t lengths[4];
		unsigned count, dropped;
		bool active = false;
	};

	template<class... Args> void Log(const char* event, const char* format, Args... args) noexcept
	{
		if (!Enabled()) return;
		char detail[384]{};
		char line[512]{};
		std::snprintf(detail, sizeof(detail), format, args...);
		const int count = std::snprintf(line, sizeof(line), "[PenHaptics] event=%s %s\n", event, detail);
		if (count <= 0) return;
		const auto length = static_cast<std::size_t>(count) < sizeof(line) ? count : sizeof(line) - 1;
		if (productMessageOutput) productMessageOutput->Append(line, length);
		else std::fwrite(line, 1, length, stderr);
	}
}
