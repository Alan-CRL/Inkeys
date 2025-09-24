#include <atomic>       // 用于原子变量 std::atomic
#include <array>        // 用于固定大小的数组 std::array
#include <cstddef>      // 用于 std::size_t
#include <optional>     // (可选) 如果你想让 try_pop 返回一个 optional<T>
#include <type_traits>  // 用于类型检查 std::is_trivially_copyable_v
#include <concepts>     // 用于 C++20 的 requires 子句
#include <new>          // 用于 std::hardware_destructive_interference_size

// --- 整体思路讲解 ---
// 1.  **基本结构**: 使用一个固定大小的 `std::array` 作为环形缓冲区（Ring Buffer）来存储元素。
// 2.  **生产者/消费者**: 严格限制为单个生产者线程（调用 `try_push`）和单个消费者线程（调用 `try_pop`）。这是无锁设计的关键简化前提。
// 3.  **索引**: 使用两个 `std::atomic<std::size_t>` 类型的索引：
//     - `head_`: 消费者下次将要读取的位置（的逻辑索引）。
//     - `tail_`: 生产者下次将要写入的位置（的逻辑索引）。
// 4.  **无锁关键**: 通过原子变量和精心选择的内存序（Memory Orderings）来保证线程安全，避免使用互斥锁（mutex）。
// 5.  **区分空/满**: 让 `head_` 和 `tail_` 索引持续增长（理论上可以达到 `size_t` 的最大值）。
//     - 队列为空: 当 `head_ == tail_` 时。
//     - 队列为满: 当 `tail_ - head_ == Capacity` 时。
//     - 当前元素数量: `tail_ - head_`。
//     - 实际数组索引: 通过取模运算 (`% Capacity`) 将逻辑索引映射到数组的物理索引。
// 6.  **内存序**:
//     - `std::memory_order_relaxed`: 最低要求，只保证原子性，不保证顺序。用于读取本线程自己修改的索引（因为没有其他线程会竞争修改它）。
//     - `std::memory_order_acquire`: 获取语义。保证本次加载操作之后的所有读写操作不会被重排到加载之前。并且，能“看到”其他线程以 `release` 语义写入的数据和之前的操作。用于读取 *对方* 线程修改的索引。
//     - `std::memory_order_release`: 释放语义。保证本次存储操作之前的所有读写操作不会被重排到存储之后。并且，使本次存储以及之前的所有写入操作对其他线程以 `acquire` 语义读取时可见。用于更新 *自己* 的索引，并将数据写入/读取完成的状态“发布”给对方线程。
// 7.  **平凡可复制 (Trivially Copyable)**: 要求模板参数 `T` 是平凡可复制类型。这意味着可以直接进行内存复制（如 `memcpy` 或简单的赋值 `=`)，没有复杂的构造、析构或移动操作，极大地简化了无锁设计，避免了处理异常安全和对象生命周期管理的复杂性。
// 8.  **缓存行对齐 (False Sharing Prevention)**: 使用 `alignas` 和 `std::hardware_destructive_interference_size` 尝试将 `head_` 和 `tail_` 放置在不同的缓存行上，避免伪共享（False Sharing）导致的性能下降。当两个线程频繁读写位于同一缓存行的不同数据时，即使数据本身不冲突，缓存一致性协议也会导致缓存行在不同核心间失效和同步，降低效率。

// --- 代码实现 ---

// 帮助获取缓存行大小的常量 (C++17 起)
// 用于对齐，避免 head_ 和 tail_ 原子变量之间的伪共享
constexpr std::size_t cache_line_size =
#ifdef __cpp_lib_hardware_interference_size
std::hardware_destructive_interference_size;
#else
// 如果编译器不支持 C++17 的这个特性，提供一个常见的回退值
64;
#endif

template <typename T, std::size_t Capacity>
	requires std::is_trivially_copyable_v<T> // C++20 约束：T 必须是平凡可复制类型
class LockFreeSpscRingBuffer {
	// 静态断言，确保容量大于 0
	static_assert(Capacity > 0, "Capacity must be positive");

private:
	// --- 成员变量 ---

	// 消费者读取索引（逻辑索引，会持续增长）
	// 使用 alignas 尝试将其与 tail_ 分隔到不同的缓存行
	alignas(cache_line_size) std::atomic<std::size_t> head_{ 0 };

	// 生产者写入索引（逻辑索引，会持续增长）
	// 使用 alignas 尝试将其与 head_ 分隔到不同的缓存行
	alignas(cache_line_size) std::atomic<std::size_t> tail_{ 0 };

	// 底层存储数据的环形缓冲区
	// C++20 可以保证 std::array 的连续存储
	// 对齐到 T 的自然对齐边界
	alignas(alignof(T)) std::array<T, Capacity> buffer_{};

public:
	// --- 构造与析构 ---

	// 默认构造函数
	LockFreeSpscRingBuffer() = default;

	// 删除拷贝和移动构造/赋值函数，防止意外共享导致 SPSC 约束被破坏
	LockFreeSpscRingBuffer(const LockFreeSpscRingBuffer&) = delete;
	LockFreeSpscRingBuffer& operator=(const LockFreeSpscRingBuffer&) = delete;
	LockFreeSpscRingBuffer(LockFreeSpscRingBuffer&&) = delete;
	LockFreeSpscRingBuffer& operator=(LockFreeSpscRingBuffer&&) = delete;

	// --- 核心操作 ---

	/**
	 * @brief 尝试向队列中推入一个元素（由生产者线程 A 调用）。
	 * @param value 要推入的值（会被复制）。
	 * @return 如果成功推入则返回 true，如果队列已满则返回 false。
	 * @note 此函数只能由单个生产者线程调用。
	 */
	bool try_push(const T& value) noexcept {
		// 1. 读取当前的 tail_ 值。因为只有生产者线程修改 tail_，
		//    这里可以用 relaxed 内存序，只需要保证原子性。
		const std::size_t current_tail = tail_.load(std::memory_order::relaxed);

		// 2. 读取当前的 head_ 值。需要用 acquire 内存序。
		//    这确保了我们能看到消费者线程更新 head_ 的最新结果（通过 release 操作），
		//    从而正确判断队列是否已满，防止覆盖掉消费者还未读取的数据。
		const std::size_t current_head = head_.load(std::memory_order::acquire);

		// 3. 检查队列是否已满。
		//    如果 tail_ 领先 head_ 达到或超过 Capacity，说明缓冲区满了。
		if (current_tail - current_head >= Capacity) {
			return false; // 队列已满，直接返回 false
		}

		// 4. 计算实际要写入的数组索引。
		const std::size_t write_index = current_tail % Capacity;

		// 5. 将数据写入缓冲区。
		//    因为 T 是平凡可复制的，直接赋值即可，是安全的。
		buffer_[write_index] = value;

		// 6. 更新 tail_ 索引。需要用 release 内存序。
		//    这确保了上面对 buffer_ 的写入操作先于 tail_ 的更新完成，
		//    并且这个更新以及之前的写入对消费者的 acquire 加载可见。
		//    相当于“发布”了这个新元素。
		tail_.store(current_tail + 1, std::memory_order::release);

		return true; // 推入成功
	}

	/**
	 * @brief 尝试从队列中弹出一个元素（由消费者线程 B 调用）。
	 * @param out_value 用于接收弹出值的引用。
	 * @return 如果成功弹出则返回 true，并将值写入 out_value；如果队列为空则返回 false。
	 * @note 此函数只能由单个消费者线程调用。
	 */
	[[nodiscard]] // 提示编译器检查返回值（是否成功弹出）
	bool try_pop(T& out_value) noexcept {
		// 1. 读取当前的 head_ 值。因为只有消费者线程修改 head_，
		//    这里可以用 relaxed 内存序。
		const std::size_t current_head = head_.load(std::memory_order::relaxed);

		// 2. 读取当前的 tail_ 值。需要用 acquire 内存序。
		//    这确保了我们能看到生产者线程写入数据并更新 tail_ 的最新结果（通过 release 操作），
		//    防止读取无效或尚未完全写入的数据。
		const std::size_t current_tail = tail_.load(std::memory_order::acquire);

		// 3. 检查队列是否为空。
		//    如果 head_ 等于 tail_，说明没有可读的元素。
		if (current_head == current_tail) {
			return false; // 队列为空，直接返回 false
		}

		// 4. 计算实际要读取的数组索引。
		const std::size_t read_index = current_head % Capacity;

		// 5. 从缓冲区读取数据。
		//    因为 T 是平凡可复制的，直接赋值即可。
		out_value = buffer_[read_index];
		// 注意：此时还不能更新 head_，必须先确保数据已读出。

		// 6. 更新 head_ 索引。需要用 release 内存序。
		//    这确保了上面对 buffer_ 的读取操作先于 head_ 的更新完成。
		//    这个更新对生产者的 acquire 加载可见，相当于“释放”了这个缓冲区槽位。
		head_.store(current_head + 1, std::memory_order::release);

		return true; // 弹出成功
	}

	// --- 辅助查询函数 (近似值) ---

	/**
	 * @brief 获取队列中当前元素的近似数量。
	 * @return 队列中的元素数量。
	 * @note 由于并发，结果可能不是精确的瞬时值，但对于监控或调试有用。
	 *       使用 acquire 语义读取，尝试获取较新的状态。
	 */
	std::size_t size_approx() const noexcept {
		// 使用 acquire 确保读取到相对较新的 head 和 tail 值
		const std::size_t current_head = head_.load(std::memory_order::acquire);
		const std::size_t current_tail = tail_.load(std::memory_order::acquire);
		// 注意处理 size_t 溢出回绕的可能性（尽管在实际中很少发生）
		// 简单情况下，tail >= head
		return current_tail - current_head;
		// 更健壮（但几乎不需要）的处理回绕:
		// return (current_tail >= current_head) ? (current_tail - current_head) : (SIZE_MAX - current_head + 1 + current_tail);
	}

	/**
	 * @brief 检查队列是否近似为空。
	 * @return 如果队列可能为空则返回 true。
	 * @note 同样是近似值。
	 */
	bool empty_approx() const noexcept {
		// 使用 acquire 确保读取到相对较新的 head 和 tail 值
		return head_.load(std::memory_order::acquire) == tail_.load(std::memory_order::acquire);
	}

	/**
	* @brief 检查队列是否近似已满。
	* @return 如果队列可能已满则返回 true。
	* @note 同样是近似值。
	*/
	bool full_approx() const noexcept {
		// 使用 acquire 确保读取到相对较新的 head 和 tail 值
		const std::size_t current_head = head_.load(std::memory_order::acquire);
		const std::size_t current_tail = tail_.load(std::memory_order::acquire);
		return current_tail - current_head >= Capacity;
	}

	/**
	 * @brief 获取队列的最大容量。
	 * @return 队列容量。
	 */
	static constexpr std::size_t capacity() noexcept {
		return Capacity;
	}
};

// --- 使用示例 (概念性) ---
/*
#include <iostream>
#include <thread>
#include <vector>

// 定义一个平凡可复制的结构体
struct MyData {
	int id;
	double value;
};

// 创建一个容量为 1024 的队列
LockFreeSpscRingBuffer<MyData, 1024> queue;

// 生产者线程函数
void producer_thread() {
	for (int i = 0; i < 2000; ++i) {
		MyData data{i, i * 1.1};
		// 尝试推入，如果队列满了就短暂等待或做其他事
		while (!queue.try_push(data)) {
			 std::this_thread::yield(); // 让出 CPU 时间片
			// 或者可以加个计数器，满了一定次数就退出或报错
		}
	}
	std::cout << "Producer finished.\n";
}

// 消费者线程函数
void consumer_thread() {
	MyData received_data;
	int count = 0;
	// 持续尝试取出，直到生产者完成并且队列为空（需要外部信号或判断逻辑）
	// 这里简化为接收一定数量
	while (count < 2000) {
		if (queue.try_pop(received_data)) {
			// 成功取出数据，进行处理
			// std::cout << "Consumed: ID=" << received_data.id << ", Value=" << received_data.value << std::endl;
			count++;
		} else {
			// 队列为空，短暂等待或做其他事
			 std::this_thread::yield(); // 让出 CPU 时间片
		}
	}
	 std::cout << "Consumer finished after receiving " << count << " items.\n";
}

int main() {
	std::thread producer(producer_thread);
	std::thread consumer(consumer_thread);

	producer.join();
	consumer.join();

	std::cout << "Final queue size approx: " << queue.size_approx() << std::endl;

	return 0;
}
*/