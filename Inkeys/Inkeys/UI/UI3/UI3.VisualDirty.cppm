module;

#include <Windows.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <functional>
#include <limits>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

// L1 视觉脏区登记表：视觉对象登记一次，每帧按「最终绘制状态」与上次成功呈现的快照比较，
// 自动得出脏区。只处理采样字节与矩形，不认识任何图元、设备或产品类型。
// 线程约束：登记、准备、提交都由同一个渲染所有者线程调用。
export module Inkeys.UI.UI3.VisualDirty;

export namespace Inkeys::UI::UI3
{
	// 采样缓冲：按字节精确比较。浮点先规范化（-0 → 0，NaN 统一），不做 epsilon 近似，
	// 因为亚像素位置或透明度变化同样会改变像素。
	class VisualStateWriter
	{
	public:
		void Clear() noexcept { bytes_.clear(); volatileMask_.clear(); volatileDepth_ = 0; maskActive_ = false; }
		// 易变段：绘制过程中按设计推进的输入（例如文字字号修正每帧在绘制里推进一次）。
		// 正常比较照常计入；绘制纯度检查跳过这些字节，避免把合法推进误报为绘制期写入。
		void BeginVolatile() noexcept { ++volatileDepth_; }
		void EndVolatile() noexcept { if (volatileDepth_ > 0) --volatileDepth_; }
		void Bool(bool value) { Append(static_cast<std::uint8_t>(value ? 1 : 0)); }
		void Int(std::int64_t value) { Append(value); }
		void UInt(std::uint64_t value) { Append(value); }
		void Real(double value)
		{
			if (std::isnan(value)) value = std::numeric_limits<double>::quiet_NaN();
			else if (value == 0.0) value = 0.0;
			Append(value);
		}
		void Color(COLORREF value) { Append(static_cast<std::uint32_t>(value)); }
		void Rect(const RECT& value)
		{
			Append(static_cast<std::int32_t>(value.left)); Append(static_cast<std::int32_t>(value.top));
			Append(static_cast<std::int32_t>(value.right)); Append(static_cast<std::int32_t>(value.bottom));
		}
		// 文字内容直接入采样（界面文字很短）；长度前缀避免两段相邻文字拼接后相同。
		void Text(std::wstring_view value)
		{
			UInt(value.size());
			AppendRaw(reinterpret_cast<const std::uint8_t*>(value.data()), value.size() * sizeof(wchar_t));
		}
		[[nodiscard]] const std::vector<std::uint8_t>& Bytes() const noexcept { return bytes_; }
		// 与 Bytes 等长；非零表示该字节属于易变段。全部为稳定字节时为空。
		[[nodiscard]] const std::vector<std::uint8_t>& VolatileMask() const noexcept { return volatileMask_; }

	private:
		template <class T> void Append(const T& value)
		{
			std::uint8_t raw[sizeof(T)];
			std::memcpy(raw, &value, sizeof(T));
			AppendRaw(raw, sizeof(T));
		}
		void AppendRaw(const std::uint8_t* data, std::size_t size)
		{
			const auto start = bytes_.size();
			bytes_.insert(bytes_.end(), data, data + size);
			// 第一次写入易变字节时补齐之前的稳定字节（可能为 0 个），此后掩码与字节始终等长。
			if (volatileDepth_ > 0 && !maskActive_) { volatileMask_.assign(start, 0); maskActive_ = true; }
			if (maskActive_) volatileMask_.resize(bytes_.size(), volatileDepth_ > 0 ? 1 : 0);
		}
		std::vector<std::uint8_t> bytes_;
		std::vector<std::uint8_t> volatileMask_;
		int volatileDepth_ = 0;
		bool maskActive_ = false;
	};

	// 登记身份：槽位 + 代次，槽位复用后旧身份自动失效，防止对象地址复用串号。
	struct VisualId
	{
		std::uint32_t index = 0;
		std::uint32_t generation = 0;
		[[nodiscard]] bool Valid() const noexcept { return generation != 0; }
		friend bool operator==(const VisualId&, const VisualId&) = default;
	};
	struct VisualGroupId
	{
		std::uint32_t index = 0;
		std::uint32_t generation = 0;
		[[nodiscard]] bool Valid() const noexcept { return generation != 0; }
		friend bool operator==(const VisualGroupId&, const VisualGroupId&) = default;
	};

	// 组（绘制作用域）的每帧采样：透明度、裁剪、未烘焙进成员几何的位姿、作用域内环境等。
	// 组状态变化 → 组内（含子组）成员全部视为变化；组不可见 → 成员一律视为不可见，且不再调用成员采样。
	struct VisualGroupSample
	{
		bool visible = true;
		VisualStateWriter state;
	};
	// 对象的每帧采样：可见性用真实绘制门槛；bounds 为映射后的宿主追踪坐标包络（含描边、光晕、抗锯齿外扩）；
	// state 只写局部有效绘制状态，整体平移只体现在 bounds 上，便于重基准。
	struct VisualSample
	{
		bool visible = false;
		RECT bounds{};
		VisualStateWriter state;
	};
	using VisualGroupSampler = std::function<void(VisualGroupSample&)>;
	using VisualSampler = std::function<void(VisualSample&)>;

	// 一次准备的结果：damage 已合并「变化对象的已提交包络 ∪ 本次包络」与注销墓碑；
	// serial 用于提交或丢弃（翻页控件按窗口回执延迟确认）。
	struct VisualAttempt
	{
		std::uint64_t serial = 0;
		RECT damage{};
		std::size_t changedCount = 0;
		// 仅在 SetRecordChanged(true) 时填写（诊断用，最多 16 项）。
		std::vector<std::string> changedNames;
		// 与上一次 Prepare（尚无未确认候选时与已提交快照）相比是否有任何采样变化。
		// 宿主据此判断「等待回执的旧候选是否仍代表当前画面」，不受脏区并集是否扩大的影响。
		bool changedSincePrevious = false;
		[[nodiscard]] bool HasDamage() const noexcept
		{
			return damage.right > damage.left && damage.bottom > damage.top;
		}
	};

	// 绘制纯度检查结果：绘制后重新采样与本帧准备时不一致的对象（易变段除外）。
	struct VisualPurityReport
	{
		std::vector<VisualId> mismatched;
		std::vector<VisualGroupId> mismatchedGroups;
		[[nodiscard]] bool Pure() const noexcept { return mismatched.empty() && mismatchedGroups.empty(); }
	};

	class VisualDirtyRegistry
	{
	public:
		VisualGroupId AddGroup(VisualGroupSampler sampler, VisualGroupId parent = {})
		{
			const auto index = Acquire(groups_, freeGroups_);
			auto& group = groups_[index];
			group.generation = NextGeneration(group.generation);
			group.alive = true;
			group.parent = IsLive(parent) ? parent : VisualGroupId{};
			group.sampler = std::move(sampler);
			group.hasCommitted = false;
			group.committedVisible = false;
			group.committedState.clear();
			return { index, group.generation };
		}

		// debugName 只用于诊断（变化项记录与纯度报告），不参与比较。
		VisualId Add(VisualGroupId group, VisualSampler sampler, std::string debugName = {})
		{
			const auto index = Acquire(entries_, freeEntries_);
			auto& entry = entries_[index];
			entry.generation = NextGeneration(entry.generation);
			entry.alive = true;
			entry.name = std::move(debugName);
			entry.group = IsLive(group) ? group : VisualGroupId{};
			entry.sampler = std::move(sampler);
			entry.committed = {};
			entry.current = {};
			return { index, entry.generation };
		}

		// 注销：若对象已呈现，旧包络作为墓碑保留到下一次成功提交，保证旧像素被擦除。
		void Remove(VisualId id)
		{
			if (!IsLive(id)) return;
			auto& entry = entries_[id.index];
			if (entry.committed.valid && entry.committed.visible)
				tombstones_.push_back(entry.committed.bounds);
			// 已准备但未确认的候选可能随后才真实呈现，其包络同样需要擦除责任。
			for (const auto& attempt : pending_)
				if (id.index < attempt.entries.size())
				{
					const auto& snapshot = attempt.entries[id.index];
					if (snapshot.generation == entry.generation && snapshot.sample.valid && snapshot.sample.visible)
						tombstones_.push_back(snapshot.sample.bounds);
				}
			entry.alive = false;
			entry.sampler = {};
			freeEntries_.push_back(id.index);
		}

		// 注销组时连同组内对象与子组一起注销。
		void RemoveGroup(VisualGroupId id)
		{
			if (!IsLive(id)) return;
			// 先在组树完整时收集子树（含孙组），再统一注销，避免停用父组后上溯中断漏掉后代。
			std::vector<std::uint32_t> subtree;
			for (std::uint32_t i = 0; i < groups_.size(); ++i)
				if (groups_[i].alive && InGroup({ i, groups_[i].generation }, id)) subtree.push_back(i);
			for (std::uint32_t i = 0; i < entries_.size(); ++i)
				if (entries_[i].alive && InGroup(entries_[i].group, id))
					Remove({ i, entries_[i].generation });
			for (const auto index : subtree)
			{
				groups_[index].alive = false;
				groups_[index].sampler = {};
				freeGroups_.push_back(index);
			}
		}

		[[nodiscard]] bool IsLive(VisualId id) const noexcept
		{
			return id.Valid() && id.index < entries_.size()
				&& entries_[id.index].alive && entries_[id.index].generation == id.generation;
		}
		[[nodiscard]] bool IsLive(VisualGroupId id) const noexcept
		{
			return id.Valid() && id.index < groups_.size()
				&& groups_[id.index].alive && groups_[id.index].generation == id.generation;
		}

		// 每帧在全部绘制状态准备完成后、BeginDraw 前调用：运行采样、与已提交快照比较、得出脏区。
		// 结果不推进快照；成功呈现后用 Commit(serial)，失败时什么都不做（下一次仍与旧快照比较）。
		VisualAttempt Prepare()
		{
			SampleGroups();
			Attempt attempt;
			attempt.serial = ++serial_;
			attempt.entries.resize(entries_.size());
			attempt.groups.resize(groups_.size());
			for (const RECT& tombstone : tombstones_) UnionInPlace(attempt.damage, tombstone);
			const Attempt* previous = pending_.empty() ? nullptr : &pending_.back();
			bool sincePrevious = previous && previous->tombstones.size() != tombstones_.size();

			for (std::uint32_t i = 0; i < entries_.size(); ++i)
			{
				auto& entry = entries_[i];
				if (!entry.alive) continue;
				auto& sample = entry.current;
				sample.visible = false;
				sample.bounds = {};
				sampleScratch_.visible = false;
				sampleScratch_.bounds = {};
				sampleScratch_.state.Clear();
				const bool groupVisible = !entry.group.Valid() || groupFrames_[entry.group.index].visible;
				if (groupVisible && entry.sampler) entry.sampler(sampleScratch_);
				sample.visible = groupVisible && sampleScratch_.visible && !IsEmpty(sampleScratch_.bounds);
				sample.bounds = sample.visible ? Normalize(sampleScratch_.bounds) : RECT{};
				sample.state = sample.visible ? sampleScratch_.state.Bytes() : std::vector<std::uint8_t>{};
				sample.volatileMask = sample.visible ? sampleScratch_.state.VolatileMask() : std::vector<std::uint8_t>{};
				sample.valid = true;

				const bool groupChanged = entry.group.Valid() && groupFrames_[entry.group.index].changed;
				const auto& committed = entry.committed;
				const bool changed = !committed.valid || groupChanged
					|| committed.visible != sample.visible
					|| (sample.visible && (!SameRect(committed.bounds, sample.bounds)
						|| committed.state != sample.state));
				if (changed && (committed.visible || sample.visible))
				{
					if (committed.valid && committed.visible) UnionInPlace(attempt.damage, committed.bounds);
					if (sample.visible) UnionInPlace(attempt.damage, sample.bounds);
					++attempt.changedCount;
					if (recordChanged_ && attempt.changedNames.size() < 16)
					{
						// 诊断：附上第一个不同字节的偏移（结构变化时为长度差）。
						std::string label = entry.name.empty() ? "#" + std::to_string(i) : entry.name;
						if (committed.valid && committed.visible && sample.visible)
						{
							std::size_t offset = 0;
							const auto& a = committed.state; const auto& b = sample.state;
							while (offset < a.size() && offset < b.size() && a[offset] == b[offset]) ++offset;
							label += "@" + std::to_string(offset) + "/" + std::to_string(b.size());
							if (!SameRect(committed.bounds, sample.bounds)) label += "+bounds";
						}
						attempt.changedNames.push_back(std::move(label));
					}
				}
				if (previous)
				{
					const bool known = i < previous->entries.size() && previous->entries[i].generation == entry.generation;
					const Snapshot& before = known ? previous->entries[i].sample : committed;
					if (!before.valid || before.visible != sample.visible
						|| (sample.visible && (!SameRect(before.bounds, sample.bounds) || before.state != sample.state)))
						sincePrevious = true;
				}
				attempt.entries[i] = { entry.generation, sample };
			}
			if (previous)
			{
				for (std::uint32_t i = 0; i < groups_.size(); ++i)
				{
					if (!groups_[i].alive) continue;
					const bool known = i < previous->groups.size() && previous->groups[i].valid
						&& previous->groups[i].generation == groups_[i].generation;
					if (!known || previous->groups[i].visible != groupFrames_[i].ownVisible
						|| previous->groups[i].state != groupFrames_[i].state) sincePrevious = true;
				}
			}
			else sincePrevious = attempt.changedCount > 0 || !tombstones_.empty();
			for (std::uint32_t i = 0; i < groups_.size(); ++i)
				if (groups_[i].alive)
					attempt.groups[i] = { groups_[i].generation, true,
						groupFrames_[i].ownVisible, groupFrames_[i].state };
			attempt.tombstones = tombstones_;

			if (previous && !sincePrevious)
			{
				// 与最近一个未确认候选完全相同：复用其序号、不另占候选槽，
				// 等待回执期间的重复准备不会把真正被绘制的候选挤出队列。
				return VisualAttempt{ previous->serial, attempt.damage, attempt.changedCount, attempt.changedNames, false };
			}
			VisualAttempt result{ attempt.serial, attempt.damage, attempt.changedCount, attempt.changedNames, sincePrevious };
			pending_.push_back(std::move(attempt));
			// 只保留少量未确认的候选；被淘汰的候选若之后才确认，Commit 会作废已提交快照以免漏刷。
			// 被淘汰候选可能已经上屏：累计其包络，迟到确认时一并擦除。
			while (pending_.size() > MaxPendingAttempts)
			{
				AccumulateAttemptBounds(pending_.front(), evictedEnvelope_);
				pending_.erase(pending_.begin());
			}
			return result;
		}

		// 绘制纯度检查（测试与调试用）：绘制结束后调用，重新运行采样并与本帧 Prepare 的结果比较。
		// 不一致说明绘制阶段改写了已登记对象的采样输入（违反「绘制只读」合同）。不改变任何快照。
		VisualPurityReport VerifyPurity()
		{
			VisualPurityReport report;
			std::vector<GroupFrame> prepared = groupFrames_;
			SampleGroups();
			for (std::uint32_t i = 0; i < groups_.size() && i < prepared.size(); ++i)
				if (groups_[i].alive && prepared[i].sampled
					&& (prepared[i].visible != groupFrames_[i].visible || prepared[i].state != groupFrames_[i].state))
					report.mismatchedGroups.push_back({ i, groups_[i].generation });
			for (std::uint32_t i = 0; i < entries_.size(); ++i)
			{
				auto& entry = entries_[i];
				if (!entry.alive || !entry.current.valid) continue;
				sampleScratch_.visible = false;
				sampleScratch_.bounds = {};
				sampleScratch_.state.Clear();
				const bool groupVisible = !entry.group.Valid() || groupFrames_[entry.group.index].visible;
				if (groupVisible && entry.sampler) entry.sampler(sampleScratch_);
				const bool visible = groupVisible && sampleScratch_.visible && !IsEmpty(sampleScratch_.bounds);
				const auto& before = entry.current;
				bool same = visible == before.visible;
				if (same && visible)
					same = SameRect(Normalize(sampleScratch_.bounds), before.bounds)
						&& SameStable(before.state, before.volatileMask, sampleScratch_.state.Bytes());
				if (!same) report.mismatched.push_back({ i, entry.generation });
			}
			groupFrames_ = std::move(prepared);
			return report;
		}

		// 成功呈现后提交该次准备的快照；早于已提交序号或已丢弃的候选被忽略。
		bool Commit(std::uint64_t serial)
		{
			if (serial <= committedSerial_) return false;
			auto found = std::find_if(pending_.begin(), pending_.end(),
				[serial](const Attempt& item) { return item.serial == serial; });
			if (found == pending_.end())
			{
				// 已送出的候选被淘汰后才确认：窗口像素对应的状态已无记录，不能再以旧快照为基准。
				// 屏幕可能停在已提交、任一未确认或被淘汰候选的状态：这些包络全部转为墓碑，
				// 下一次准备按「全部旧包络 ∪ 当前可见包络」重画，移走或消失的对象也能擦除。
				if (serial <= serial_)
				{
					RECT stale = evictedEnvelope_;
					for (const auto& entry : entries_)
						if (entry.committed.valid && entry.committed.visible) UnionInPlace(stale, entry.committed.bounds);
					for (const RECT& tombstone : tombstones_) UnionInPlace(stale, tombstone);
					for (const auto& attempt : pending_) AccumulateAttemptBounds(attempt, stale);
					InvalidateCommitted();
					if (!IsEmpty(stale)) tombstones_.push_back(stale);
					committedSerial_ = serial;
				}
				return false;
			}
			for (std::uint32_t i = 0; i < found->entries.size() && i < entries_.size(); ++i)
			{
				const auto& snapshot = found->entries[i];
				auto& entry = entries_[i];
				if (!entry.alive || !snapshot.sample.valid || entry.generation != snapshot.generation) continue;
				entry.committed = snapshot.sample;
			}
			for (std::uint32_t i = 0; i < found->groups.size() && i < groups_.size(); ++i)
			{
				const auto& snapshot = found->groups[i];
				auto& group = groups_[i];
				if (!group.alive || !snapshot.valid || group.generation != snapshot.generation) continue;
				group.hasCommitted = true;
				group.committedVisible = snapshot.visible;
				group.committedState = snapshot.state;
			}
			// 该次准备已包含的墓碑随提交清除；之后新增的墓碑继续保留。
			for (const RECT& tombstone : found->tombstones)
			{
				auto it = std::find_if(tombstones_.begin(), tombstones_.end(),
					[&](const RECT& value) { return SameRect(value, tombstone); });
				if (it != tombstones_.end()) tombstones_.erase(it);
			}
			committedSerial_ = serial;
			pending_.erase(pending_.begin(), found + 1);
			// 更新的候选已上屏，被淘汰的更早状态不再可能残留。
			evictedEnvelope_ = {};
			return true;
		}

		// 宿主整体重基准（窗口原点移动但像素随之移动）时调用：已提交包络、墓碑与未确认候选同步平移，
		// 平移后包络与本帧包络相同的对象不会被误判为变化。
		void TranslateCommitted(POINT delta) noexcept
		{
			if (delta.x == 0 && delta.y == 0) return;
			for (auto& entry : entries_)
				if (entry.committed.valid && entry.committed.visible) Translate(entry.committed.bounds, delta);
			for (auto& tombstone : tombstones_) Translate(tombstone, delta);
			Translate(evictedEnvelope_, delta);
			for (auto& attempt : pending_)
			{
				for (auto& snapshot : attempt.entries)
					if (snapshot.sample.valid && snapshot.sample.visible) Translate(snapshot.sample.bounds, delta);
				for (auto& tombstone : attempt.tombstones) Translate(tombstone, delta);
				Translate(attempt.damage, delta);
			}
		}

		// 已提交快照全部作废（例如呈现目标重建后旧像素不可信）：下一次准备把全部可见对象当作新出现。
		void InvalidateCommitted() noexcept
		{
			for (auto& entry : entries_) entry.committed = {};
			for (auto& group : groups_) { group.hasCommitted = false; group.committedState.clear(); }
			tombstones_.clear();
			// 未确认候选基于作废前的快照，一并丢弃。
			pending_.clear();
			evictedEnvelope_ = {};
		}

		[[nodiscard]] std::size_t LiveCount() const noexcept
		{
			return static_cast<std::size_t>(std::count_if(entries_.begin(), entries_.end(),
				[](const Entry& entry) { return entry.alive; }));
		}
		[[nodiscard]] std::uint64_t CommittedSerial() const noexcept { return committedSerial_; }
		void SetRecordChanged(bool value) noexcept { recordChanged_ = value; }
		[[nodiscard]] std::string NameOf(VisualId id) const
		{
			return IsLive(id) ? entries_[id.index].name : std::string{};
		}

		[[nodiscard]] static bool IsEmpty(const RECT& value) noexcept
		{
			return value.right <= value.left || value.bottom <= value.top;
		}
		static void UnionInPlace(RECT& target, const RECT& value) noexcept
		{
			if (IsEmpty(value)) return;
			if (IsEmpty(target)) { target = value; return; }
			target.left = (std::min)(target.left, value.left);
			target.top = (std::min)(target.top, value.top);
			target.right = (std::max)(target.right, value.right);
			target.bottom = (std::max)(target.bottom, value.bottom);
		}

	private:
		static constexpr std::size_t MaxPendingAttempts = 4;

		struct Snapshot
		{
			bool valid = false;
			bool visible = false;
			RECT bounds{};
			std::vector<std::uint8_t> state;
			std::vector<std::uint8_t> volatileMask;
		};
		struct Entry
		{
			std::uint32_t generation = 0;
			bool alive = false;
			VisualGroupId group;
			VisualSampler sampler;
			Snapshot committed;
			Snapshot current;
			std::string name;
		};
		struct Group
		{
			std::uint32_t generation = 0;
			bool alive = false;
			VisualGroupId parent;
			VisualGroupSampler sampler;
			bool hasCommitted = false;
			bool committedVisible = false;
			std::vector<std::uint8_t> committedState;
		};
		struct GroupFrame
		{
			bool sampled = false;
			bool ownVisible = false;
			bool visible = false;
			bool changed = false;
			std::vector<std::uint8_t> state;
		};
		struct EntrySnapshot { std::uint32_t generation = 0; Snapshot sample; };
		struct GroupSnapshot
		{
			std::uint32_t generation = 0;
			bool valid = false;
			bool visible = false;
			std::vector<std::uint8_t> state;
		};
		struct Attempt
		{
			std::uint64_t serial = 0;
			RECT damage{};
			std::size_t changedCount = 0;
			std::vector<std::string> changedNames;
			std::vector<EntrySnapshot> entries;
			std::vector<GroupSnapshot> groups;
			std::vector<RECT> tombstones;
		};

		template <class T>
		static std::uint32_t Acquire(std::vector<T>& items, std::vector<std::uint32_t>& freeList)
		{
			if (!freeList.empty())
			{
				const auto index = freeList.back();
				freeList.pop_back();
				return index;
			}
			items.emplace_back();
			return static_cast<std::uint32_t>(items.size() - 1);
		}
		static std::uint32_t NextGeneration(std::uint32_t current) noexcept
		{
			return current == (std::numeric_limits<std::uint32_t>::max)() ? 1 : current + 1;
		}
		[[nodiscard]] bool InGroup(VisualGroupId group, VisualGroupId ancestor) const noexcept
		{
			// 组树由父先于子创建保证无环；深度很浅，逐级上溯即可。
			for (std::size_t depth = 0; IsLive(group) && depth <= groups_.size(); ++depth)
			{
				if (group == ancestor) return true;
				group = groups_[group.index].parent;
			}
			return false;
		}

		void SampleGroups()
		{
			groupFrames_.resize(groups_.size());
			for (auto& frame : groupFrames_) frame.sampled = false;
			for (std::uint32_t i = 0; i < groups_.size(); ++i)
				if (groups_[i].alive) SampleGroup(i, 0);
		}
		void SampleGroup(std::uint32_t index, std::size_t depth)
		{
			auto& frame = groupFrames_[index];
			if (frame.sampled) return;
			frame.sampled = true;
			auto& group = groups_[index];
			bool parentVisible = true, parentChanged = false;
			if (IsLive(group.parent) && depth < groups_.size())
			{
				SampleGroup(group.parent.index, depth + 1);
				parentVisible = groupFrames_[group.parent.index].visible;
				parentChanged = groupFrames_[group.parent.index].changed;
			}
			groupScratch_.visible = true;
			groupScratch_.state.Clear();
			if (parentVisible && group.sampler) group.sampler(groupScratch_);
			frame.ownVisible = parentVisible && groupScratch_.visible;
			frame.visible = frame.ownVisible;
			frame.state = frame.visible ? groupScratch_.state.Bytes() : std::vector<std::uint8_t>{};
			// 组本身没有像素；只有可见时的状态变化才需要让成员重画。可见性变化由成员自身的可见性比较处理。
			frame.changed = parentChanged || (frame.visible && group.hasCommitted
				&& group.committedVisible && group.committedState != frame.state);
		}

		static RECT Normalize(const RECT& value) noexcept
		{
			return { (std::min)(value.left, value.right), (std::min)(value.top, value.bottom),
				(std::max)(value.left, value.right), (std::max)(value.top, value.bottom) };
		}
		static bool SameRect(const RECT& a, const RECT& b) noexcept
		{
			return a.left == b.left && a.top == b.top && a.right == b.right && a.bottom == b.bottom;
		}
		// 比较时跳过易变字节；长度不同即视为不同（结构变化）。
		static bool SameStable(const std::vector<std::uint8_t>& before, const std::vector<std::uint8_t>& mask,
			const std::vector<std::uint8_t>& after) noexcept
		{
			if (before.size() != after.size()) return false;
			for (std::size_t i = 0; i < before.size(); ++i)
				if (before[i] != after[i] && (i >= mask.size() || mask[i] == 0)) return false;
			return true;
		}
		static void Translate(RECT& value, POINT delta) noexcept
		{
			if (IsEmpty(value)) return;
			value.left += delta.x; value.right += delta.x;
			value.top += delta.y; value.bottom += delta.y;
		}
		// 一个候选可能在屏幕上留下的全部像素范围：可见对象包络与当时的墓碑。
		static void AccumulateAttemptBounds(const Attempt& attempt, RECT& target) noexcept
		{
			for (const auto& snapshot : attempt.entries)
				if (snapshot.sample.valid && snapshot.sample.visible) UnionInPlace(target, snapshot.sample.bounds);
			for (const RECT& tombstone : attempt.tombstones) UnionInPlace(target, tombstone);
		}

		std::vector<Entry> entries_;
		std::vector<Group> groups_;
		std::vector<std::uint32_t> freeEntries_, freeGroups_;
		std::vector<GroupFrame> groupFrames_;
		std::vector<RECT> tombstones_;
		std::vector<Attempt> pending_;
		// 已被淘汰（可能已上屏）候选的包络并集；迟到确认时转为墓碑，更新候选提交后清空。
		RECT evictedEnvelope_{};
		VisualSample sampleScratch_;
		VisualGroupSample groupScratch_;
		std::uint64_t serial_ = 0;
		std::uint64_t committedSerial_ = 0;
		bool recordChanged_ = false;
	};
}
