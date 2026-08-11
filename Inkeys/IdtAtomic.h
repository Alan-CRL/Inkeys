#pragma once

#include <atomic>
#include <format>
#include <type_traits>

// 为跨 module 的轻量状态提供统一、可复制的原子包装。
template <typename IdtAtomicT>
class IdtAtomic
{
	static_assert(std::is_trivially_copyable_v<IdtAtomicT>,
		"IdtAtomic<IdtAtomicT>: IdtAtomicT 必须是平凡可复制 (TriviallyCopyable) 的类型。");
	static_assert(std::atomic<IdtAtomicT>::is_always_lock_free,
		"IdtAtomic<IdtAtomicT>: IdtAtomicT 对应的 atomic<IdtAtomicT> 必须保证始终无锁 (lock-free)。");

private:
	std::atomic<IdtAtomicT> value;

public:
	IdtAtomic() noexcept = default;
	IdtAtomic(IdtAtomicT desired) noexcept : value(desired) {}
	IdtAtomic(const IdtAtomic& other) noexcept { value.store(other.value.load()); }

	IdtAtomic& operator=(const IdtAtomic& other) noexcept
	{
		if (this != &other) value.store(other.value.load());
		return *this;
	}

	IdtAtomic(IdtAtomic&& other) noexcept { value.store(other.value.load()); }
	IdtAtomic& operator=(IdtAtomic&& other) noexcept
	{
		value.store(other.value.load());
		return *this;
	}

	IdtAtomicT load(std::memory_order order = std::memory_order_seq_cst) const noexcept
	{
		return value.load(order);
	}
	void store(IdtAtomicT desired, std::memory_order order = std::memory_order_seq_cst) noexcept
	{
		value.store(desired, order);
	}

	IdtAtomicT exchange(IdtAtomicT desired,
		std::memory_order order = std::memory_order_seq_cst) noexcept
	{
		return value.exchange(desired, order);
	}
	bool compare_exchange_weak(IdtAtomicT& expected, IdtAtomicT desired,
		std::memory_order success = std::memory_order_seq_cst,
		std::memory_order failure = std::memory_order_seq_cst) noexcept
	{
		return value.compare_exchange_weak(expected, desired, success, failure);
	}
	bool compare_exchange_strong(IdtAtomicT& expected, IdtAtomicT desired,
		std::memory_order success = std::memory_order_seq_cst,
		std::memory_order failure = std::memory_order_seq_cst) noexcept
	{
		return value.compare_exchange_strong(expected, desired, success, failure);
	}
	bool compare_set_strong(const IdtAtomicT& expectedValue, IdtAtomicT desired,
		std::memory_order success = std::memory_order_seq_cst,
		std::memory_order failure = std::memory_order_seq_cst) noexcept
	{
		IdtAtomicT expected = expectedValue;
		return value.compare_exchange_strong(expected, desired, success, failure);
	}

	template <typename T = IdtAtomicT,
		typename = std::enable_if_t<std::is_arithmetic_v<T>>>
	T fetch_add(T arg, std::memory_order order = std::memory_order_seq_cst) noexcept
	{
		return value.fetch_add(arg, order);
	}
	template <typename T = IdtAtomicT,
		typename = std::enable_if_t<std::is_arithmetic_v<T>>>
	T fetch_sub(T arg, std::memory_order order = std::memory_order_seq_cst) noexcept
	{
		return value.fetch_sub(arg, order);
	}

	operator IdtAtomicT() const noexcept { return load(); }
	IdtAtomic& operator=(IdtAtomicT desired) noexcept
	{
		store(desired);
		return *this;
	}

	template <typename T = IdtAtomicT,
		typename = std::enable_if_t<std::is_integral_v<T>>>
	T operator++() noexcept
	{
		return value.fetch_add(1, std::memory_order_seq_cst) + 1;
	}
	template <typename T = IdtAtomicT,
		typename = std::enable_if_t<std::is_integral_v<T>>>
	T operator++(int) noexcept
	{
		return value.fetch_add(1, std::memory_order_seq_cst);
	}
	template <typename T = IdtAtomicT,
		typename = std::enable_if_t<std::is_integral_v<T>>>
	T operator--() noexcept
	{
		return value.fetch_sub(1, std::memory_order_seq_cst) - 1;
	}
	template <typename T = IdtAtomicT,
		typename = std::enable_if_t<std::is_integral_v<T>>>
	T operator--(int) noexcept
	{
		return value.fetch_sub(1, std::memory_order_seq_cst);
	}

	template <typename T = IdtAtomicT,
		typename = std::enable_if_t<std::is_arithmetic_v<T>>>
	IdtAtomic& operator+=(T arg) noexcept
	{
		fetch_add(arg, std::memory_order_seq_cst);
		return *this;
	}
	template <typename T = IdtAtomicT,
		typename = std::enable_if_t<std::is_arithmetic_v<T>>>
	IdtAtomic& operator-=(T arg) noexcept
	{
		fetch_sub(arg, std::memory_order_seq_cst);
		return *this;
	}
};

template <typename IdtAtomicT, typename CharT>
struct std::formatter<IdtAtomic<IdtAtomicT>, CharT>
	: std::formatter<IdtAtomicT, CharT>
{
	auto format(const IdtAtomic<IdtAtomicT>& obj, std::format_context& ctx) const
	{
		return std::formatter<IdtAtomicT, CharT>::format(obj.load(), ctx);
	}
};
