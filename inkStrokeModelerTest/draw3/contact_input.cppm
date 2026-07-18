module;

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <atomic>
#include <cstdint>
#include <memory>
#include <type_traits>

export module draw3.contact_input;

namespace draw3
{
	struct ContactInputCoordinatorImpl;
	struct ContactRecordAccess;
}

export namespace draw3
{
	// 标识 RTS 输入设备；数值是跨输入层的固定契约。
	enum class InputDeviceType : uint32_t
	{
		Touch = 0,
		Pen = 1,
		MouseLeft = 2,
		MouseRight = 3
	};

	// 标识 contact 最新的一致生命周期快照。
	enum class ContactPhase : uint32_t
	{
		Down,
		Move,
		Up,
		Cancelled
	};

	struct PointF
	{
		float x = 0.0f;
		float y = 0.0f;
	};

	struct SizeF
	{
		float width = -1.0f;
		float height = -1.0f;
	};

	// 绘制线程一次读取到的完整 contact 状态。
	struct ContactSnapshot
	{
		PointF position = {};
		float pressure = -1.0f;
		SizeF contactSize = {};
		int64_t qpc = 0;
		ContactPhase phase = ContactPhase::Down;
		uint64_t sequence = 0;
	};

	// 仅允许使用所有目标架构上始终无锁的平凡标量。
	template<typename T>
	class IdtAtomic
	{
		static_assert(std::is_trivially_copyable_v<T>, "IdtAtomic 仅支持平凡可复制类型");
		static_assert(std::atomic<T>::is_always_lock_free, "IdtAtomic 要求目标类型始终无锁");

	public:
		constexpr IdtAtomic() noexcept = default;
		constexpr explicit IdtAtomic(T value) noexcept : value_(value) {}
		IdtAtomic(const IdtAtomic&) = delete;
		IdtAtomic& operator=(const IdtAtomic&) = delete;

		T Load(std::memory_order order = std::memory_order_seq_cst) const noexcept
		{
			return value_.load(order);
		}

		void Store(T value, std::memory_order order = std::memory_order_seq_cst) noexcept
		{
			value_.store(value, order);
		}

		bool CompareExchangeStrong(T& expected, T desired,
			std::memory_order success = std::memory_order_seq_cst,
			std::memory_order failure = std::memory_order_seq_cst) noexcept
		{
			return value_.compare_exchange_strong(expected, desired, success, failure);
		}

	private:
		std::atomic<T> value_ = {};
	};

	// 地址在协调器生命周期内稳定；生产完成后仅由绘制线程回收。
	struct alignas(64) ContactRecord
	{
		ContactRecord() = default;
		ContactRecord(const ContactRecord&) = delete;
		ContactRecord& operator=(const ContactRecord&) = delete;
		ContactRecord(ContactRecord&&) = delete;
		ContactRecord& operator=(ContactRecord&&) = delete;

		uint32_t TabletContextId() const noexcept;
		uint32_t ContactId() const noexcept;
		InputDeviceType DeviceType() const noexcept;
		const ContactSnapshot& DownSnapshot() const noexcept;
		uint64_t Generation() const noexcept;

	private:
		friend struct ContactRecordAccess;

		IdtAtomic<uint32_t> tabletContextId_;
		IdtAtomic<uint32_t> contactId_;
		IdtAtomic<uint32_t> deviceType_;
		ContactSnapshot downSnapshot_ = {};
		IdtAtomic<float> x_;
		IdtAtomic<float> y_;
		IdtAtomic<float> pressure_;
		IdtAtomic<float> width_;
		IdtAtomic<float> height_;
		IdtAtomic<int64_t> qpc_;
		IdtAtomic<uint32_t> phase_;
		IdtAtomic<uint64_t> sequence_;
		IdtAtomic<uint64_t> producerRoute_;
		std::atomic_flag writerLatch_ = ATOMIC_FLAG_INIT;
	};

	struct ContactHandle
	{
		ContactRecord* record = nullptr;
		uint64_t generation = 0;

		explicit operator bool() const noexcept { return record != nullptr; }
	};

	enum class IngressCommandKind : uint32_t
	{
		Down,
		ControlWake
	};

	struct IngressCommand
	{
		IngressCommandKind kind = IngressCommandKind::ControlWake;
		ContactHandle contact = {};
	};

	// 协调 RTS 生产者与唯一绘制消费者，并在完全空闲时提供内核等待。
	class ContactInputCoordinator
	{
	public:
		ContactInputCoordinator();
		~ContactInputCoordinator();
		ContactInputCoordinator(const ContactInputCoordinator&) = delete;
		ContactInputCoordinator& operator=(const ContactInputCoordinator&) = delete;

		// 可靠发布不可变 Down，并把 handle 放入阻塞队列。
		bool PublishDown(uint32_t tabletContextId, uint32_t contactId,
			InputDeviceType deviceType, const ContactSnapshot& snapshot);
		// 覆盖最新 Move；写门闩争用时允许丢弃。
		bool PublishMove(uint32_t tabletContextId, uint32_t contactId,
			const ContactSnapshot& snapshot) noexcept;
		// 关闭生产路由并可靠发布不可逆 Up。
		bool PublishUp(uint32_t tabletContextId, uint32_t contactId,
			const ContactSnapshot& snapshot) noexcept;
		// 关闭生产路由并可靠发布不可逆 Cancelled。
		bool PublishCancelled(uint32_t tabletContextId, uint32_t contactId,
			const ContactSnapshot& snapshot) noexcept;

		// 读取 generation 匹配的跨字段一致快照。
		bool TryReadSnapshot(ContactHandle handle, ContactSnapshot& snapshot) const noexcept;
		// 绘制线程在完成 L2 提交后归还 slot。
		void Recycle(ContactHandle handle) noexcept;
		// RTS 停止回调后取消仍由生产者持有的全部 contact。
		void CloseAllProducerContacts(int64_t qpc) noexcept;

		bool TryDequeue(IngressCommand& command) noexcept;
		void WaitDequeue(IngressCommand& command) noexcept;
		// 窗口请求已经原子发布后，合并投递一次控制唤醒。
		bool PublishControlWake() noexcept;
		// 消费 ControlWake 后先清 pending，再复查全部窗口请求。
		void AcknowledgeControlWake() noexcept;

	private:
		std::unique_ptr<ContactInputCoordinatorImpl> impl_;
	};
}
