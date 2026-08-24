module;

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <windows.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <functional>
#include <string_view>

export module Inkeys.UI.PageControl;

export namespace Inkeys::UI::PageControl
{
	enum class Surface : std::uint8_t
	{
		BottomLeft,
		BottomRight,
		MiddleLeft,
		MiddleRight,
	};

	enum class WorkspaceMode : std::uint8_t
	{
		Hidden,
		PptCompact,
		WhiteboardExpanded,
	};

	struct WorkspaceInputPolicy
	{
		bool click = false;
		bool drag = false;
		bool longPress = false;
		bool wheel = false;
		bool persistPosition = false;
	};

	[[nodiscard]] constexpr WorkspaceInputPolicy ResolveWorkspaceInputPolicy(
		WorkspaceMode mode) noexcept
	{
		// 共享按钮实例不代表共享工作区业务能力。
		if (mode == WorkspaceMode::PptCompact)
			return { true, true, true, true, true };
		if (mode == WorkspaceMode::WhiteboardExpanded)
			return { true, false, false, false, false };
		return {};
	}

	struct DirectionContentPolicy
	{
		std::wstring_view icon;
		std::wstring_view label;
	};

	[[nodiscard]] constexpr DirectionContentPolicy ResolveDirectionContentPolicy(
		WorkspaceMode mode, bool next, bool nextIsAdd) noexcept
	{
		if (mode != WorkspaceMode::WhiteboardExpanded)
			return { L"barMore", {} };
		if (!next) return { L"barMore", L"左翻页" };
		return nextIsAdd
			? DirectionContentPolicy{ L"barAdd", L"加页" }
			: DirectionContentPolicy{ L"barMore", L"右翻页" };
	}

	inline constexpr double PptCompactLongSideDip = 165.0;
	inline constexpr double PptCompactShortSideDip = 42.5;
	inline constexpr double PptDragHandleSlotDip = 10.0;
	inline constexpr double PptArrowButtonSideDip = 32.5;
	inline constexpr double PptPageButtonLongSideDip = 70.0;
	inline constexpr double PageControlGapDip = 5.0;
	inline constexpr double WhiteboardWidthDip = 230.0;
	inline constexpr double WhiteboardHeightDip = 80.0;
	inline constexpr double WhiteboardButtonSideDip = 70.0;

	enum class PptWidgetRole : std::uint8_t
	{
		DragHandle,
		Previous,
		Page,
		Next,
	};

	struct DipRect
	{
		double left = 0.0;
		double top = 0.0;
		double right = 0.0;
		double bottom = 0.0;
	};

	struct PptWidgetContract
	{
		PptWidgetRole role = PptWidgetRole::Page;
		DipRect bounds{};
		bool displaysText = false;
		bool invokesBusinessAction = false;
		bool dragSource = false;
		bool hoverVisual = true;
		bool pressVisual = true;
		bool primaryTextBold = false;
		bool secondaryTextNormal = false;
	};

	[[nodiscard]] inline std::array<PptWidgetContract, 4>
		ResolvePptWidgetContracts(Surface surface) noexcept
	{
		const bool vertical = surface == Surface::MiddleLeft
			|| surface == Surface::MiddleRight;
		const bool right = surface == Surface::BottomRight;
		DipRect drag{};
		DipRect previous{};
		DipRect page{};
		DipRect next{};
		if (vertical)
		{
			drag = { 5.0, 5.0, 37.5, 15.0 };
			previous = { 5.0, 15.0, 37.5, 47.5 };
			page = { 5.0, 52.5, 37.5, 122.5 };
			next = { 5.0, 127.5, 37.5, 160.0 };
		}
		else if (right)
		{
			previous = { 5.0, 5.0, 37.5, 37.5 };
			page = { 42.5, 5.0, 112.5, 37.5 };
			next = { 117.5, 5.0, 150.0, 37.5 };
			drag = { 150.0, 5.0, 160.0, 37.5 };
		}
		else
		{
			drag = { 5.0, 5.0, 15.0, 37.5 };
			previous = { 15.0, 5.0, 47.5, 37.5 };
			page = { 52.5, 5.0, 122.5, 37.5 };
			next = { 127.5, 5.0, 160.0, 37.5 };
		}
		return {
			PptWidgetContract{ PptWidgetRole::DragHandle, drag,
				false, false, true, false, false, false, false },
			PptWidgetContract{ PptWidgetRole::Previous, previous,
				false, true, false, true, true, false, false },
			PptWidgetContract{ PptWidgetRole::Page, page,
				true, false, false, true, true, true, true },
			PptWidgetContract{ PptWidgetRole::Next, next,
				false, true, false, true, true, false, false },
		};
	}

	struct PptLayoutState
	{
		float bottomPairWidth = 0.0F;
		float bottomPairHeight = 0.0F;
		float middlePairWidth = 0.0F;
		float middlePairHeight = 0.0F;
		float bottomPairScale = 1.0F;
		float middlePairScale = 1.0F;
		bool showBottomPair = true;
		bool showMiddlePair = false;
		bool rememberPosition = true;
	};

	struct PptState
	{
		bool presentationVisible = false;
		int currentPage = -1;
		int totalPage = -1;
		PptLayoutState layout;
	};

	struct WhiteboardState
	{
		// 布局目标可先于 Draw3/Freeze 背景就绪，供反向重入立即重定向动画。
		bool expandedLayoutTarget = false;
		bool active = false;
		int currentPage = 1;
		int totalPage = 1;
		bool previousEnabled = false;
		bool previousInteractive = false;
		bool pageEnabled = true;
		bool pageInteractive = true;
		bool nextEnabled = true;
		bool nextInteractive = true;
		bool nextIsAdd = false;
		bool switching = false;
	};

	[[nodiscard]] inline bool ArePptStatesEquivalent(
		const PptState& left, const PptState& right) noexcept
	{
		auto SameFloat = [](float first, float second) noexcept
			{
				return first == second
					|| (std::isnan(first) && std::isnan(second));
			};
		return left.presentationVisible == right.presentationVisible
			&& left.currentPage == right.currentPage
			&& left.totalPage == right.totalPage
			&& SameFloat(left.layout.bottomPairWidth,
				right.layout.bottomPairWidth)
			&& SameFloat(left.layout.bottomPairHeight,
				right.layout.bottomPairHeight)
			&& SameFloat(left.layout.middlePairWidth,
				right.layout.middlePairWidth)
			&& SameFloat(left.layout.middlePairHeight,
				right.layout.middlePairHeight)
			&& SameFloat(left.layout.bottomPairScale,
				right.layout.bottomPairScale)
			&& SameFloat(left.layout.middlePairScale,
				right.layout.middlePairScale)
			&& left.layout.showBottomPair == right.layout.showBottomPair
			&& left.layout.showMiddlePair == right.layout.showMiddlePair
			&& left.layout.rememberPosition == right.layout.rememberPosition;
	}

	[[nodiscard]] constexpr bool AreWhiteboardStatesEquivalent(
		const WhiteboardState& left, const WhiteboardState& right) noexcept
	{
		return left.expandedLayoutTarget == right.expandedLayoutTarget
			&& left.active == right.active
			&& left.currentPage == right.currentPage
			&& left.totalPage == right.totalPage
			&& left.previousEnabled == right.previousEnabled
			&& left.previousInteractive == right.previousInteractive
			&& left.pageEnabled == right.pageEnabled
			&& left.pageInteractive == right.pageInteractive
			&& left.nextEnabled == right.nextEnabled
			&& left.nextInteractive == right.nextInteractive
			&& left.nextIsAdd == right.nextIsAdd
			&& left.switching == right.switching;
	}

	struct PptCallbacks
	{
		std::function<void()> previousPage;
		std::function<void()> nextPage;
		std::function<void(PptLayoutState)> persistPosition;
	};

	struct WhiteboardCallbacks
	{
		std::function<void()> previousPage;
		std::function<void()> nextPage;
	};

	struct ResolvedSurfaceLayout
	{
		RECT logicalBounds{};
		float scale = 1.0F;
		WorkspaceMode mode = WorkspaceMode::Hidden;
		bool visible = false;
		bool vertical = false;
	};

	[[nodiscard]] inline float NormalizeScale(float scale) noexcept
	{
		if (!std::isfinite(scale) || scale <= 0.0F) return 1.0F;
		return std::clamp(scale, 1.0F / 128.0F, 4.0F);
	}

	[[nodiscard]] inline bool ShouldApplyPageControlSceneBounds(
		bool applied, const RECT& previousBounds, float previousScale,
		const RECT& candidateBounds, float candidateScale) noexcept
	{
		candidateScale = NormalizeScale(candidateScale);
		return !applied
			|| previousBounds.left != candidateBounds.left
			|| previousBounds.top != candidateBounds.top
			|| previousBounds.right != candidateBounds.right
			|| previousBounds.bottom != candidateBounds.bottom
			|| previousScale != candidateScale;
	}

	struct PageControlDebugWindowDamagePolicy
	{
		bool includePreviousWindow = false;
		bool includeCurrentWindow = false;
	};

	[[nodiscard]] constexpr PageControlDebugWindowDamagePolicy
		ResolvePageControlDebugWindowDamagePolicy(
			bool forceFullReplacement, bool overlayRefreshPending,
			bool debugEnabled) noexcept
	{
		const bool replaceOverlay = forceFullReplacement || overlayRefreshPending;
		return { replaceOverlay, debugEnabled && replaceOverlay };
	}

	[[nodiscard]] constexpr bool ShouldTreatPageControlDamageAsActiveDebugFrame(
		bool debugEnabled, bool businessDamagePending,
		bool forceFullReplacement, bool finalFramePending) noexcept
	{
		return debugEnabled && (businessDamagePending || forceFullReplacement)
			&& !finalFramePending;
	}

	[[nodiscard]] inline WorkspaceMode ResolveWorkspaceMode(
		Surface surface, const PptState& ppt,
		const WhiteboardState& whiteboard) noexcept
	{
		const bool bottom = surface == Surface::BottomLeft
			|| surface == Surface::BottomRight;
		if (whiteboard.expandedLayoutTarget)
			return bottom ? WorkspaceMode::WhiteboardExpanded
				: WorkspaceMode::Hidden;
		if (!ppt.presentationVisible) return WorkspaceMode::Hidden;
		if (bottom && ppt.layout.showBottomPair)
			return WorkspaceMode::PptCompact;
		if (!bottom && ppt.layout.showMiddlePair)
			return WorkspaceMode::PptCompact;
		return WorkspaceMode::Hidden;
	}

	[[nodiscard]] inline bool ShouldAnimateWorkspaceLayout(
		Surface surface, WorkspaceMode previousMode, WorkspaceMode desiredMode,
		bool previouslyVisible) noexcept
	{
		if (previousMode == desiredMode) return false;
		const bool bottom = surface == Surface::BottomLeft
			|| surface == Surface::BottomRight;
		// 底栏从隐藏态出现时直接采用目标布局，仅执行渐显。
		return !bottom || previouslyVisible;
	}

	[[nodiscard]] constexpr bool ShouldPreserveSurfaceBoundsWhileHiding(
		Surface surface) noexcept
	{
		// 底栏在当前位置渐隐；只有两侧控件保留既有侧向退场。
		return surface == Surface::BottomLeft
			|| surface == Surface::BottomRight;
	}

	[[nodiscard]] constexpr bool ShouldKeepPageControlWindowVisible(
		bool targetVisible, bool exitTransitionActive) noexcept
	{
		// Scene 可能因共享光源持续报告动画；隐藏生命周期只由固定退场时限保活。
		return targetVisible || exitTransitionActive;
	}

	[[nodiscard]] constexpr bool ShouldLockSurfaceInput(
		bool targetVisible, bool transitionActive,
		bool workspaceSwitching) noexcept
	{
		return !targetVisible || transitionActive || workspaceSwitching;
	}

	[[nodiscard]] constexpr bool WhiteboardWorkspaceSwitching(
		const WhiteboardState& whiteboard) noexcept
	{
		return whiteboard.switching
			|| whiteboard.expandedLayoutTarget != whiteboard.active;
	}

	struct StableBackingResolution
	{
		SIZE size{ 1, 1 };
		bool changed = false;
	};

	[[nodiscard]] inline StableBackingResolution ResolveStableBackingSize(
		SIZE current, SIZE presentation, SIZE targetContent,
		LONG currentOutset, double currentScale, double targetScale) noexcept
	{
		current.cx = (std::max)(1L, current.cx);
		current.cy = (std::max)(1L, current.cy);
		presentation.cx = (std::max)(1L, presentation.cx);
		presentation.cy = (std::max)(1L, presentation.cy);
		targetContent.cx = (std::max)(1L, targetContent.cx);
		targetContent.cy = (std::max)(1L, targetContent.cy);
		currentOutset = (std::max)(0L, currentOutset);
		if (!std::isfinite(currentScale) || currentScale <= 0.0)
			currentScale = 1.0;
		if (!std::isfinite(targetScale) || targetScale <= 0.0)
			targetScale = currentScale;
		const LONG targetOutset = static_cast<LONG>(std::ceil(
			static_cast<double>(currentOutset)
				* (std::max)(1.0, targetScale / currentScale)));
		const SIZE resolved{
			(std::max)({ current.cx, presentation.cx,
				targetContent.cx + targetOutset * 2 }),
			(std::max)({ current.cy, presentation.cy,
				targetContent.cy + targetOutset * 2 }),
		};
		return { resolved, resolved.cx != current.cx || resolved.cy != current.cy };
	}

	[[nodiscard]] inline bool ShouldFlashPptSurface(
		Surface surface, const PptState& ppt,
		const WhiteboardState& whiteboard) noexcept
	{
		return !WhiteboardWorkspaceSwitching(whiteboard)
			&& ResolveWorkspaceMode(surface, ppt, whiteboard)
			== WorkspaceMode::PptCompact;
	}

	[[nodiscard]] inline ResolvedSurfaceLayout ResolveSurfaceLayout(
		Surface surface, const RECT& monitor, float dpiScale,
		const PptState& ppt, const WhiteboardState& whiteboard) noexcept
	{
		ResolvedSurfaceLayout result;
		result.mode = ResolveWorkspaceMode(surface, ppt, whiteboard);
		result.visible = result.mode != WorkspaceMode::Hidden;
		result.vertical = surface == Surface::MiddleLeft
			|| surface == Surface::MiddleRight;
		const bool left = surface == Surface::BottomLeft
			|| surface == Surface::MiddleLeft;
		const float normalizedDpi = NormalizeScale(dpiScale);
		float scale = normalizedDpi;
		double widthDip = result.vertical
			? PptCompactShortSideDip : PptCompactLongSideDip;
		double heightDip = result.vertical
			? PptCompactLongSideDip : PptCompactShortSideDip;
		if (result.mode == WorkspaceMode::WhiteboardExpanded)
		{
			widthDip = WhiteboardWidthDip;
			heightDip = WhiteboardHeightDip;
		}
		else if (result.vertical)
			scale *= NormalizeScale(ppt.layout.middlePairScale);
		else
			scale *= NormalizeScale(ppt.layout.bottomPairScale);
		result.scale = scale;
		const LONG width = (std::max)(1L,
			static_cast<LONG>(std::lround(widthDip * scale)));
		const LONG height = (std::max)(1L,
			static_cast<LONG>(std::lround(heightDip * scale)));
		const LONG edge = static_cast<LONG>(std::lround(
			PageControlGapDip * scale));

		LONG x = monitor.left;
		LONG y = monitor.top;
		if (result.mode == WorkspaceMode::WhiteboardExpanded)
		{
			// Whiteboard 始终锚定左右下角，不读取或继承 PPT 用户位置。
			x = left ? monitor.left + edge
				: monitor.right - edge - width;
			y = monitor.bottom - edge - height;
		}
		else if (result.vertical)
		{
			const LONG offsetX = static_cast<LONG>(std::lround(
				ppt.layout.middlePairWidth));
			const LONG offsetY = static_cast<LONG>(std::lround(
				ppt.layout.middlePairHeight));
			x = left ? monitor.left + edge + offsetX
				: monitor.right - edge - offsetX - width;
			y = monitor.top + (monitor.bottom - monitor.top - height) / 2
				- offsetY;
		}
		else
		{
			const LONG offsetX = static_cast<LONG>(std::lround(
				ppt.layout.bottomPairWidth));
			const LONG offsetY = static_cast<LONG>(std::lround(
				ppt.layout.bottomPairHeight));
			x = left ? monitor.left + edge + offsetX
				: monitor.right - edge - offsetX - width;
			y = monitor.bottom - edge - offsetY - height;
		}
		result.logicalBounds = RECT{ x, y, x + width, y + height };
		return result;
	}

	[[nodiscard]] inline bool OverlapsWithGap(
		const RECT& first, const RECT& second, LONG gap) noexcept
	{
		return first.left < second.right + gap
			&& first.right + gap > second.left
			&& first.top < second.bottom + gap
			&& first.bottom + gap > second.top;
	}

	[[nodiscard]] inline PptLayoutState ClampPageControlLayout(
		Surface moved, const RECT& monitor, float dpiScale,
		PptLayoutState layout) noexcept
	{
		const float width = static_cast<float>((std::max)(1L,
			monitor.right - monitor.left));
		const float height = static_cast<float>((std::max)(1L,
			monitor.bottom - monitor.top));
		const bool bottom = moved == Surface::BottomLeft
			|| moved == Surface::BottomRight;
		if (bottom)
		{
			const float scale = NormalizeScale(dpiScale)
				* NormalizeScale(layout.bottomPairScale);
			const double controlWidth = PptCompactLongSideDip;
			const double controlHeight = PptCompactShortSideDip;
			layout.bottomPairWidth = (std::clamp)(layout.bottomPairWidth,
				0.0F, (std::max)(0.0F, width / 2.0F
					- static_cast<float>((controlWidth + PageControlGapDip) * scale)));
			layout.bottomPairHeight = (std::clamp)(layout.bottomPairHeight,
				0.0F, (std::max)(0.0F, height
					- static_cast<float>((controlHeight
						+ PageControlGapDip * 2.0) * scale)));
		}
		else
		{
			const float scale = NormalizeScale(dpiScale)
				* NormalizeScale(layout.middlePairScale);
			layout.middlePairWidth = (std::clamp)(layout.middlePairWidth,
				0.0F, (std::max)(0.0F, width / 2.0F
					- static_cast<float>((PptCompactShortSideDip
						+ PageControlGapDip) * scale)));
			const float verticalLimit = (std::max)(0.0F,
				height / 2.0F - static_cast<float>(
					(PptCompactLongSideDip / 2.0
						+ PageControlGapDip) * scale));
			layout.middlePairHeight = (std::clamp)(layout.middlePairHeight,
				-verticalLimit, verticalLimit);
		}
		return layout;
	}

	[[nodiscard]] inline bool PageControlGroupsOverlap(
		const RECT& monitor, float dpiScale, const PptState& ppt,
		LONG gap) noexcept
	{
		for (const Surface bottom : { Surface::BottomLeft, Surface::BottomRight })
		{
			const auto bottomLayout = ResolveSurfaceLayout(bottom, monitor,
				dpiScale, ppt, {});
			if (!bottomLayout.visible) continue;
			for (const Surface middle : { Surface::MiddleLeft, Surface::MiddleRight })
			{
				const auto middleLayout = ResolveSurfaceLayout(middle, monitor,
					dpiScale, ppt, {});
				if (middleLayout.visible && OverlapsWithGap(bottomLayout.logicalBounds,
					middleLayout.logicalBounds, gap)) return true;
			}
		}
		return false;
	}

	[[nodiscard]] inline PptState ResolveRuntimePageControlLayout(
		const RECT& monitor, float dpiScale, PptState ppt) noexcept
	{
		if (!ppt.presentationVisible) return ppt;
		const float normalizedDpi = NormalizeScale(dpiScale);
		const float width = static_cast<float>((std::max)(1L,
			monitor.right - monitor.left));
		const float height = static_cast<float>((std::max)(1L,
			monitor.bottom - monitor.top));
		const LONG gap = static_cast<LONG>(std::lround(
			PageControlGapDip * normalizedDpi));
		const bool bottomVisible = ppt.layout.showBottomPair;
		const bool middleVisible = ppt.layout.showMiddlePair;

		auto FitScale = [&](bool visible, double controlWidth,
			double controlHeight, float& userScale)
		{
			if (!visible) return;
			const float maximum = (std::min)(
				width / static_cast<float>((controlWidth + PageControlGapDip) * 2.0
					* normalizedDpi),
				height / static_cast<float>((controlHeight
					+ PageControlGapDip * 2.0) * normalizedDpi));
			userScale = (std::min)(NormalizeScale(userScale),
				(std::max)(1.0F / 128.0F, maximum));
		};
		FitScale(bottomVisible, PptCompactLongSideDip,
			PptCompactShortSideDip, ppt.layout.bottomPairScale);
		FitScale(middleVisible, PptCompactShortSideDip,
			PptCompactLongSideDip, ppt.layout.middlePairScale);
		if (bottomVisible)
			ppt.layout = ClampPageControlLayout(Surface::BottomLeft,
				monitor, normalizedDpi, ppt.layout);
		if (middleVisible)
			ppt.layout = ClampPageControlLayout(Surface::MiddleLeft,
				monitor, normalizedDpi, ppt.layout);

		auto TryNearest = [](float start, float minimum, float maximum,
			auto&& invalid, float& resolved)
		{
			start = (std::clamp)(start, minimum, maximum);
			const int distanceLimit = static_cast<int>(std::ceil((std::max)(
				std::abs(start - minimum), std::abs(maximum - start))));
			for (int distance = 0; distance <= distanceLimit; ++distance)
			{
				const float positive = start + static_cast<float>(distance);
				if (positive <= maximum && !invalid(positive))
				{
					resolved = positive;
					return true;
				}
				if (distance == 0) continue;
				const float negative = start - static_cast<float>(distance);
				if (negative >= minimum && !invalid(negative))
				{
					resolved = negative;
					return true;
				}
			}
			return false;
		};

		// 自动纠偏固定底部组，只让侧边组寻找最近可行位置。
		if (middleVisible)
		{
			for (int attempt = 0; attempt < 24; ++attempt)
			{
				ppt.layout = ClampPageControlLayout(Surface::MiddleLeft,
					monitor, normalizedDpi, ppt.layout);
				PptLayoutState upper = ppt.layout;
				upper.middlePairHeight = height;
				upper = ClampPageControlLayout(Surface::MiddleLeft,
					monitor, normalizedDpi, upper);
				const float limit = upper.middlePairHeight;
				float resolved = ppt.layout.middlePairHeight;
				const bool found = TryNearest(ppt.layout.middlePairHeight,
					-limit, limit, [&](float candidate)
					{
						PptState next = ppt;
						next.layout.middlePairHeight = candidate;
						return PageControlGroupsOverlap(
							monitor, normalizedDpi, next, gap);
					}, resolved);
				if (found)
				{
					ppt.layout.middlePairHeight = resolved;
					break;
				}
				if (ppt.layout.middlePairScale <= 1.0F / 128.0F) break;
				ppt.layout.middlePairScale = (std::max)(1.0F / 128.0F,
					ppt.layout.middlePairScale * 0.75F);
			}
		}
		return ppt;
	}

	[[nodiscard]] inline RECT ResolveHiddenSurfaceBounds(
		Surface surface, const RECT& monitor, RECT visibleBounds) noexcept
	{
		const LONG width = visibleBounds.right - visibleBounds.left;
		if (surface == Surface::MiddleLeft)
		{
			visibleBounds.right = monitor.left;
			visibleBounds.left = monitor.left - width;
		}
		else if (surface == Surface::MiddleRight)
		{
			visibleBounds.left = monitor.right;
			visibleBounds.right = monitor.right + width;
		}
		return visibleBounds;
	}

	bool Acquire();
	void Release() noexcept;
	[[nodiscard]] WNDPROC WindowProc() noexcept;
	void SetPptCallbacks(PptCallbacks callbacks);
	void SetWhiteboardCallbacks(WhiteboardCallbacks callbacks);
	void PublishPptState(const PptState& state) noexcept;
	void PublishWhiteboardState(const WhiteboardState& state) noexcept;
	void FlashPptDirection(bool next) noexcept;
	void QueuePptWheel(short delta) noexcept;
	void SetDebugEnabled(bool enabled) noexcept;
	void NotifyLayoutChanged() noexcept;
	void CancelPointerCapture() noexcept;
}
