#pragma once

#include <windows.h>

#include <cstddef>
#include <optional>
#include <utility>

namespace Inkeys::Window::Detail
{
	inline constexpr wchar_t CanvasNonRudeProperty[] = L"NonRudeHWND";

	[[nodiscard]] inline HANDLE CanvasNonRudeValue(bool whiteboard, HANDLE original) noexcept
	{
		return whiteboard ? original : reinterpret_cast<HANDLE>(TRUE);
	}

	// 故障期间后到的隐藏命令优先于旧显隐快照，防止重试重新揭开已撤下的表面。
	inline void NoteCanvasRecoveryHidden(std::optional<bool>& visible, bool& iconic) noexcept
	{
		if (visible) visible = false;
		iconic = false;
	}

	// 由 HWND owner 串行读取实际样式；失败的 FRAMECHANGED 不能被同值重试误当成已完成。
	template<typename Ops>
	[[nodiscard]] bool ApplyCanvasClickThrough(bool enabled, bool& framePending, bool& applying, Ops& ops)
	{
		// USER32 可在实际写位之前同步重入；嵌套请求不能凭旧样式报告成功并推进白板相位。
		if (applying) { SetLastError(ERROR_BUSY); return false; }
		applying = true;
		struct ApplyScope
		{
			bool& applying;
			~ApplyScope() { applying = false; }
		} scope{ applying };
		if (!ops.StillDesired()) return false;
		LONG_PTR actual = 0;
		if (!ops.ReadStyle(actual)) return false;
		const LONG_PTR desired = enabled ? actual | WS_EX_TRANSPARENT
			: actual & ~static_cast<LONG_PTR>(WS_EX_TRANSPARENT);
		if (!ops.StillDesired()) return false;
		if (desired != actual)
		{
			// 先保留 FRAMECHANGED 责任；同步重入只能失败并在外层退栈后重试。
			framePending = true;
			if (!ops.WriteStyle(desired)) return false;
		}
		if (!ops.StillDesired()) return false;
		if (framePending && !ops.CommitStyle()) return false;
		if (!ops.ReadStyle(actual) || !ops.StillDesired()
			|| ((actual & WS_EX_TRANSPARENT) != 0) != enabled) return false;
		framePending = false;
		return true;
	}

	struct CanvasPolicyKey
	{
		HWND hwnd = nullptr;
		RECT bounds{};
		RECT monitor{};
		bool whiteboard = false;

		[[nodiscard]] bool operator==(const CanvasPolicyKey& other) const noexcept
		{
			return hwnd == other.hwnd && whiteboard == other.whiteboard
				&& EqualRect(&bounds, &other.bounds) && EqualRect(&monitor, &other.monitor);
		}
	};

	[[nodiscard]] inline bool CanvasPolicyNeedsUpdate(
		const std::optional<CanvasPolicyKey>& applied, const CanvasPolicyKey& target) noexcept
	{
		return !applied || *applied != target;
	}

	// 只裁屏幕最后一物理行的交集；保留 HWND/client 尺寸及文档、RTS 标尺。
	[[nodiscard]] inline RECT CanvasBottomExclusion(const CanvasPolicyKey& key) noexcept
	{
		if (key.whiteboard || key.monitor.bottom <= key.monitor.top) return {};
		const RECT row{ key.monitor.left, key.monitor.bottom - 1,
			key.monitor.right, key.monitor.bottom };
		RECT intersection{};
		if (!IntersectRect(&intersection, &row, &key.bounds)) return {};
		OffsetRect(&intersection, -key.bounds.left, -key.bounds.top);
		return intersection;
	}

	class CanvasRegion
	{
	public:
		CanvasRegion() = default;
		explicit CanvasRegion(HRGN region) noexcept : region_(region) {}
		~CanvasRegion() { Reset(); }
		CanvasRegion(const CanvasRegion&) = delete;
		CanvasRegion& operator=(const CanvasRegion&) = delete;
		CanvasRegion(CanvasRegion&& other) noexcept : region_(other.Release()) {}
		CanvasRegion& operator=(CanvasRegion&& other) noexcept
		{
			if (this != &other) Reset(other.Release());
			return *this;
		}
		[[nodiscard]] HRGN Get() const noexcept { return region_; }
		[[nodiscard]] HRGN Release() noexcept { return std::exchange(region_, nullptr); }
		void Reset(HRGN region = nullptr) noexcept
		{
			if (region_) DeleteObject(region_);
			region_ = region;
		}
	private:
		HRGN region_ = nullptr;
	};

	[[nodiscard]] inline bool CopyCanvasRegion(HRGN source, CanvasRegion& copy) noexcept
	{
		copy.Reset();
		if (!source) return true;
		CanvasRegion region(CreateRectRgn(0, 0, 0, 0));
		if (!region.Get() || CombineRgn(region.Get(), source, nullptr, RGN_COPY) == ERROR)
			return false;
		copy = std::move(region);
		return true;
	}

	[[nodiscard]] inline bool BuildCanvasRegion(const CanvasPolicyKey& key,
		HRGN original, CanvasRegion& result) noexcept
	{
		if (!CopyCanvasRegion(original, result)) return false;
		const RECT excluded = CanvasBottomExclusion(key);
		if (IsRectEmpty(&excluded)) return true;
		if (!result.Get())
			result.Reset(CreateRectRgn(0, 0, key.bounds.right - key.bounds.left,
				key.bounds.bottom - key.bounds.top));
		CanvasRegion row(CreateRectRgnIndirect(&excluded));
		return result.Get() && row.Get()
			&& CombineRgn(result.Get(), result.Get(), row.Get(), RGN_DIFF) != ERROR;
	}

	// 只编排 owner 实际操作，不等待 Render；准备和完成使用同一个业务请求。
	template<class Ops>
	[[nodiscard]] bool PrepareWorkspacePolicy(Ops& ops)
	{
		return ops.Current() && ops.WithdrawFullscreen() && ops.Current()
			&& ops.PrepareGeometry() && ops.Current() && ops.ApplyMode() && ops.Current()
			&& ops.ApplyBand() && ops.Current() && ops.BlockInput() && ops.Current() && ops.CapturePrepared();
	}
	template<class Ops>
	[[nodiscard]] bool CompleteWorkspacePolicy(Ops& ops)
	{
		return ops.Current() && ops.ValidatePrepared() && ops.ApplyInput() && ops.Current()
			&& ops.ApplyVisibility() && ops.Current() && ops.PublishFullscreen() && ops.Current();
	}

	struct CanvasTransactionResult
	{
		bool succeeded = false;
		bool rollbackSucceeded = true;
	};

	struct CanvasPolicyCompletion
	{
		bool succeeded = false;
		bool retry = false;
		bool dirty = false;

		void UpdateApplied(std::optional<CanvasPolicyKey>& applied,
			const CanvasPolicyKey& target) const noexcept
		{
			if (succeeded) applied = target;
			else if (retry) applied.reset();
			// 同几何完整回滚已恢复旧策略；保留旧 key，首次应用前的空值也不伪造成功。
		}
	};

	[[nodiscard]] inline CanvasPolicyCompletion ResolveCanvasPolicyCompletion(
		const CanvasTransactionResult& result, bool geometryChanged, bool previouslyDirty) noexcept
	{
		// 仅不可信的旧区域需要 owner 重试，已有样式修复意图则保留至真实成功。
		return { result.succeeded,
			!result.succeeded && (!result.rollbackSucceeded || geometryChanged || previouslyDirty),
			!result.succeeded && (previouslyDirty || !result.rollbackSucceeded) };
	}

	// 双画布先共同隐藏再提交；失败时逆序恢复，回滚不完整时保持双隐藏供原状态循环重试。
	// Ops 仅封装本事务的 Win32 调用，便于无 HWND 故障注入验证真实提交次序。
	template<typename Ops>
	[[nodiscard]] CanvasTransactionResult ApplyCanvasPairTransaction(
		std::size_t count, Ops& ops, bool restoreRollbackVisibility = true) noexcept
	{
		std::size_t captured = 0;
		for (; captured < count; ++captured)
			if (!ops.Capture(captured)) return {};
		bool succeeded = true;
		for (std::size_t i = 0; succeeded && i < count; ++i) succeeded = ops.Hide(i);
		for (std::size_t i = 0; succeeded && i < count; ++i) succeeded = ops.Apply(i);
		bool sceneAttempted = false;
		if (succeeded)
		{
			sceneAttempted = true;
			succeeded = ops.ApplyScene();
		}
		for (std::size_t i = 0; succeeded && i < count; ++i)
			succeeded = ops.RestoreVisibility(i);
		if (succeeded) return { true, true };

		bool restored = true;
		for (std::size_t i = 0; i < count; ++i) restored = ops.Hide(i) && restored;
		if (sceneAttempted) restored = ops.RollbackScene() && restored;
		for (std::size_t i = captured; i > 0; --i) restored = ops.Restore(i - 1) && restored;
		if (restored && restoreRollbackVisibility)
			for (std::size_t i = 0; i < count; ++i)
				restored = ops.RestoreVisibility(i) && restored;
		if (!restored)
			for (std::size_t i = 0; i < count; ++i) (void)ops.Hide(i);
		return { false, restored };
	}
}
