module;

export module Inkeys.UI.Bar.Metrics;

export import Inkeys.UI.UI3.Metrics;

using namespace Inkeys::UI::UI3;

export
{
	// 大尺寸翻页：按钮统一 2×2；小尺寸由下方按档位求值的函数给出（旧硬编码 compact 分支已移除）。
	inline constexpr double BarPaginationDragHandleSlotDip = ButtonGapDip * 2.0;
	inline constexpr double BarPptPaginationLongSideDip =
		BarPaginationDragHandleSlotDip + ButtonTwoSideDip * 3.0 + ButtonGapDip * 4.0;
	inline constexpr double BarPptPaginationShortSideDip =
		ButtonTwoSideDip + ButtonGapDip * 2.0;
	inline constexpr double BarWhiteboardPaginationWidthDip =
		ButtonTwoSideDip * 3.0 + ButtonGapDip * 4.0;
	inline constexpr double BarWhiteboardPaginationLeftWidthDip =
		BarWhiteboardPaginationWidthDip + ButtonTwoSideDip + ButtonGapDip;
	inline constexpr double BarWhiteboardPaginationHeightDip =
		ButtonTwoSideDip + ButtonGapDip * 2.0;
	inline constexpr double BarMainBarWidthDip = 80.0;
	inline constexpr double BarMainBarHeightDip = 80.0;
	inline constexpr double BarMainBarCornerRadiusDip = 8.0;
	inline constexpr double BarMainBarFillOpacity = 0.8;
	inline constexpr double BarMainBarFrameOpacity = 0.18;

	// Startup Preview 只依赖这些无副作用的视觉常量，不提前初始化完整 Bar。
	inline constexpr double BarMainButtonWidthDip = 80.0;
	inline constexpr double BarMainButtonHeightDip = 80.0;
	inline constexpr double BarMainButtonCollapsedFillOpacity = 0.6;
	inline constexpr double BarMainButtonToMainBarGapDip = 10.0;
	inline constexpr double BarDefaultButtonColumnStepDip =
		ButtonTwoSideDip + ButtonGapDip;
	// 默认展开布局：A1 的 Select/Draw/Clean 三列，More 一列，A2 的
	// Whiteboard/Freeze 上下共用一列；Divider 只收束列，不增加宽度。
	inline constexpr unsigned BarDefaultA1ColumnCount = 3;
	inline constexpr unsigned BarDefaultMoreColumnCount = 1;
	inline constexpr unsigned BarDefaultA2ColumnCount = 1;
	inline constexpr unsigned BarDefaultMainBarColumnCount =
		BarDefaultA1ColumnCount + BarDefaultMoreColumnCount
		+ BarDefaultA2ColumnCount;
	// CalculateButtonLayoutWidth 从左侧 5 DIP 起步：5 + 5 * 75 = 380。
	inline constexpr double BarDefaultMainBarLayoutWidthDip =
		ButtonGapDip
		+ static_cast<double>(BarDefaultMainBarColumnCount)
			* BarDefaultButtonColumnStepDip;
	inline constexpr double BarDefaultStartupPreviewTotalWidthDip =
		BarMainButtonWidthDip + BarMainButtonToMainBarGapDip
		+ BarDefaultMainBarLayoutWidthDip;
	inline constexpr double StartupPreviewCachedWidthDefaultDip =
		BarDefaultStartupPreviewTotalWidthDip;
	inline constexpr bool StartupPreviewEnabledDefault = true;
	// 下限取两种尺寸中较小的主按钮边长（小尺寸 42.5），小尺寸首帧宽度不会被丢弃。
	inline constexpr double StartupPreviewCachedWidthMinimumDip = 42.5;
	inline constexpr double StartupPreviewCachedWidthMaximumDip = 4096.0;

	static_assert(BarDefaultButtonColumnStepDip == 75.0);
	static_assert(BarDefaultMainBarColumnCount == 5);
	static_assert(BarDefaultMainBarLayoutWidthDip == 380.0);
	static_assert(BarDefaultStartupPreviewTotalWidthDip == 470.0);

	// 界面尺寸档位：大尺寸为既有布局，小尺寸主栏与翻页短边 42.5 DIP。
	// 配置值 0=大、1=小；其他值一律按大尺寸处理。
	enum class BarSizeMode : unsigned char
	{
		Large = 0,
		Compact = 1,
	};

	[[nodiscard]] constexpr BarSizeMode NormalizeBarSizeMode(int value) noexcept
	{
		return value == static_cast<int>(BarSizeMode::Compact)
			? BarSizeMode::Compact : BarSizeMode::Large;
	}

	// 两种尺寸的唯一来源；布局、绘制、命中与吸附都只读本帧解析出的这一份。
	struct BarSizeMetrics
	{
		double gap = ButtonGapDip;                       // 网格间距（按钮与外框、按钮与按钮）
		double one = ButtonOneSideDip;                   // 1×1 边长
		double two = ButtonTwoSideDip;                   // 两格跨度
		double barHeight = BarMainBarHeightDip;          // 主栏与翻页控件短边
		double barCornerRadius = BarMainBarCornerRadiusDip;
		double mainButtonSide = BarMainButtonWidthDip;   // 主按钮静止边长
		double mainButtonGap = BarMainButtonToMainBarGapDip; // 主按钮与主栏水平间距
		double attachedGap = 10.0;                       // 属性窗/提示条/步骤栏与主栏边缘的间隙
		double dividerHeight = 50.0;                     // 主栏分割线高度
		double dockVisualLimit = 24.0;                   // 底栏竖向果冻形变上限
		double taskbarBasicQuantity = 48.0;              // 任务栏基础量：主栏高度按此值对齐 100% 任务栏
		// 主按钮展开态超椭圆指数。指数相同时圆角随边长缩放，小尺寸会显得更尖；
		// 按“对角线顶点与圆角矩形一致”换算，使两档的等效圆角都约为 9.15 DIP（大尺寸 80 边长、n=10）。
		double mainButtonExpandedExponent = 10.0;
	};

	inline constexpr BarSizeMetrics BarLargeSizeMetrics{};
	// 小尺寸：1×1 仍为 32.5，四周与相邻间距与大尺寸同为 5，32.5 + 5×2 = 42.5；两格跨度 70 与大尺寸一致。
	inline constexpr BarSizeMetrics BarCompactSizeMetrics{
		ButtonGapDip, ButtonOneSideDip, ButtonTwoSideDip, 42.5,
		// 圆角与大尺寸主栏一致。
		BarMainBarCornerRadiusDip,
		// 主按钮边长随主栏高度；主按钮/属性窗到主栏的间隙保持 5；分割线高 32.5−4，与其他单行面板一致。
		42.5, 5.0, 5.0, ButtonOneSideDip - 4.0,
		// 竖向形变上限按 42.5/80 缩放，避免矮主栏被夸张拉伸。
		24.0 * 42.5 / 80.0,
		// 任务栏基础量两档统一为 48：大尺寸 80 DIP 主栏按 48/80 显示，小尺寸 42.5 DIP 主栏按 48/42.5 显示。
		48.0,
		// 42.5 边长下等效圆角 9.15 DIP：n = -1 / log2(1 - 9.15/21.25 × (1 - 1/√2)) ≈ 5.14。
		5.14,
	};

	[[nodiscard]] constexpr const BarSizeMetrics& ResolveBarSizeMetrics(
		BarSizeMode mode) noexcept
	{
		return mode == BarSizeMode::Compact ? BarCompactSizeMetrics : BarLargeSizeMetrics;
	}

	static_assert(BarLargeSizeMetrics.two == BarLargeSizeMetrics.one * 2.0 + BarLargeSizeMetrics.gap);
	static_assert(BarLargeSizeMetrics.barHeight == 80.0 && BarLargeSizeMetrics.mainButtonSide == 80.0);
	static_assert(BarCompactSizeMetrics.two == BarCompactSizeMetrics.one * 2.0 + BarCompactSizeMetrics.gap);
	static_assert(BarCompactSizeMetrics.one + BarCompactSizeMetrics.gap * 2.0
		== BarCompactSizeMetrics.barHeight);
	static_assert(BarCompactSizeMetrics.mainButtonSide == BarCompactSizeMetrics.barHeight);
	static_assert(StartupPreviewCachedWidthMinimumDip == BarCompactSizeMetrics.mainButtonSide);
	// 小尺寸默认展开包络：默认 6 个按钮 + 常驻撤回/重做共 8 个单行 1×1，左起 5；分割线落在间隙正中不占宽。
	inline constexpr double BarCompactDefaultMainBarLayoutWidthDip =
		BarCompactSizeMetrics.gap + 8.0 * (BarCompactSizeMetrics.one + BarCompactSizeMetrics.gap);
	inline constexpr double StartupPreviewCachedWidthCompactDefaultDip =
		BarCompactSizeMetrics.mainButtonSide + BarCompactSizeMetrics.mainButtonGap
		+ BarCompactDefaultMainBarLayoutWidthDip;
	static_assert(StartupPreviewCachedWidthCompactDefaultDip == 352.5);

	// 翻页控件按档位求值：大尺寸返回原常量；小尺寸箭头/加页/更多为 1×1，页码为 2×1（竖向 1×2），
	// 间距 5，拖动 lane 5 DIP。
	[[nodiscard]] constexpr double BarPaginationDragLaneDip(BarSizeMode mode) noexcept
	{
		return mode == BarSizeMode::Compact ? 5.0 : BarPaginationDragHandleSlotDip;
	}
	[[nodiscard]] constexpr double BarPaginationArrowSideDip(BarSizeMode mode) noexcept
	{
		return mode == BarSizeMode::Compact ? BarCompactSizeMetrics.one : ButtonTwoSideDip;
	}
	[[nodiscard]] constexpr double BarPptPaginationLongSide(BarSizeMode mode) noexcept
	{
		if (mode != BarSizeMode::Compact) return BarPptPaginationLongSideDip;
		const auto& m = BarCompactSizeMetrics;
		return BarPaginationDragLaneDip(mode) + m.one * 2.0 + m.two + m.gap * 4.0;
	}
	[[nodiscard]] constexpr double BarPptPaginationShortSide(BarSizeMode mode) noexcept
	{
		return mode == BarSizeMode::Compact ? BarCompactSizeMetrics.barHeight : BarPptPaginationShortSideDip;
	}
	[[nodiscard]] constexpr double BarWhiteboardPaginationWidth(BarSizeMode mode) noexcept
	{
		if (mode != BarSizeMode::Compact) return BarWhiteboardPaginationWidthDip;
		const auto& m = BarCompactSizeMetrics;
		return m.one * 2.0 + m.two + m.gap * 4.0;
	}
	[[nodiscard]] constexpr double BarWhiteboardPaginationLeftWidth(BarSizeMode mode) noexcept
	{
		if (mode != BarSizeMode::Compact) return BarWhiteboardPaginationLeftWidthDip;
		const auto& m = BarCompactSizeMetrics;
		return BarWhiteboardPaginationWidth(mode) + m.one + m.gap;
	}
	[[nodiscard]] constexpr double BarWhiteboardPaginationHeight(BarSizeMode mode) noexcept
	{
		return mode == BarSizeMode::Compact ? BarCompactSizeMetrics.barHeight : BarWhiteboardPaginationHeightDip;
	}
	static_assert(BarPptPaginationLongSide(BarSizeMode::Compact) == 160.0);
	static_assert(BarWhiteboardPaginationWidth(BarSizeMode::Compact) == 155.0);
	static_assert(BarWhiteboardPaginationLeftWidth(BarSizeMode::Compact) == 192.5);
}

export namespace Inkeys::UI::Bar
{
	// gapDip：主按钮到主栏的间距（大尺寸 10，小尺寸 5）。
	[[nodiscard]] constexpr double CalculateExpandedTotalWidthDip(
		double mainButtonTargetWidthDip, double layoutTotalWidthDip,
		double gapDip = BarMainButtonToMainBarGapDip) noexcept
	{
		return mainButtonTargetWidthDip
			+ gapDip + layoutTotalWidthDip;
	}

	// 属性窗（绘制/形状/橡皮/More）在非居中底栏时不越过主按钮外侧边：
	// 主栏向右展开时左边不小于主按钮左边，向左展开时右边不大于主按钮右边；未越过时保持原位（居中）。
	// 返回调整后的左边；工作区夹取仍由调用方在此之后执行。
	[[nodiscard]] constexpr double ClampBarAttributePanelLeft(double left, double width,
		double mainButtonLeft, double mainButtonRight, bool opensRight) noexcept
	{
		if (opensRight) return left < mainButtonLeft ? mainButtonLeft : left;
		return left + width > mainButtonRight ? mainButtonRight - width : left;
	}
	static_assert(ClampBarAttributePanelLeft(-20.0, 100.0, 0.0, 42.5, true) == 0.0);
	static_assert(ClampBarAttributePanelLeft(10.0, 100.0, 0.0, 42.5, true) == 10.0);
	static_assert(ClampBarAttributePanelLeft(-20.0, 100.0, 0.0, 42.5, false) == -57.5);
	static_assert(ClampBarAttributePanelLeft(-80.0, 100.0, 0.0, 42.5, false) == -80.0);

	struct BarPaginationDipRect
	{
		double left = 0.0;
		double top = 0.0;
		double right = 0.0;
		double bottom = 0.0;
	};

	struct BarPptStandardPaginationGeometry
	{
		BarPaginationDipRect drag;
		BarPaginationDipRect previous;
		BarPaginationDipRect page;
		BarPaginationDipRect next;
	};

	// 与 Scene 的单行列表布局同源：a 为箭头边长，p 为页码长边，t 为控件厚度（大尺寸三者均为 70）。
	[[nodiscard]] constexpr BarPptStandardPaginationGeometry
		ResolveBarPptStandardPaginationGeometry(bool vertical, bool right,
			BarSizeMode mode = BarSizeMode::Large) noexcept
	{
		const bool compact = mode == BarSizeMode::Compact;
		const double gap = compact ? BarCompactSizeMetrics.gap : ButtonGapDip;
		const double a = BarPaginationArrowSideDip(mode);
		const double p = compact ? BarCompactSizeMetrics.two : ButtonTwoSideDip;
		const double t = compact ? BarCompactSizeMetrics.one : ButtonTwoSideDip;
		const double slot = BarPaginationDragLaneDip(mode);
		if (vertical)
		{
			return {
				{ gap, gap, gap + t, gap + slot },
				{ gap, gap + slot, gap + t, gap + slot + a },
				{ gap, 2 * gap + slot + a, gap + t, 2 * gap + slot + a + p },
				{ gap, 3 * gap + slot + a + p, gap + t, 3 * gap + slot + 2 * a + p },
			};
		}
		if (right)
		{
			return {
				{ 3 * gap + 2 * a + p, gap, 3 * gap + 2 * a + p + slot, gap + t },
				{ gap, gap, gap + a, gap + t },
				{ 2 * gap + a, gap, 2 * gap + a + p, gap + t },
				{ 3 * gap + a + p, gap, 3 * gap + 2 * a + p, gap + t },
			};
		}
		return {
			{ gap, gap, gap + slot, gap + t },
			{ gap + slot, gap, gap + slot + a, gap + t },
			{ 2 * gap + slot + a, gap, 2 * gap + slot + a + p, gap + t },
			{ 3 * gap + slot + a + p, gap, 3 * gap + slot + 2 * a + p, gap + t },
		};
	}

	// 小尺寸悬停提示（仅真实鼠标）：500 ms 延时、12 DIP 文字，默认位于光标右上方。
	inline constexpr double BarHoverHintDelaySeconds = 0.5;
	inline constexpr double BarHoverHintFontSizeDip = 12.0;
	inline constexpr double BarHoverHintPaddingXDip = 8.0;
	inline constexpr double BarHoverHintPaddingYDip = 4.0;
	inline constexpr double BarHoverHintCursorOffsetDip = 12.0;
	inline constexpr double BarHoverHintMaxTextWidthDip = 240.0;
	// 容量预留用的最大提示高度（单行文字 + 上下内边距，含余量）。
	inline constexpr double BarHoverHintMaxHeightDip = 32.0;

	struct BarHoverHintRect
	{
		double left = 0, top = 0, right = 0, bottom = 0;
	};

	// 像素空间：光标 (x, y)、提示尺寸 w×h、偏移 offset、可用范围 bounds（工作区 ∩ 窗口）。
	// 默认左下角在光标右上 offset 处；右侧放不下翻到光标左侧，上方放不下翻到光标下方（2×offset 让开指针），最后整体夹进 bounds。
	[[nodiscard]] constexpr BarHoverHintRect ResolveBarHoverHintRect(double x, double y,
		double w, double h, double offset, BarHoverHintRect bounds) noexcept
	{
		// 可用范围小于提示时先限尺寸（文字由绘制裁剪）。
		const double availableW = bounds.right - bounds.left, availableH = bounds.bottom - bounds.top;
		if (availableW > 0 && w > availableW) w = availableW;
		if (availableH > 0 && h > availableH) h = availableH;
		double left = x + offset;
		if (left + w > bounds.right) left = x - offset - w;
		double top = y - offset - h;
		if (top < bounds.top) top = y + offset * 2.0;
		const double maxLeft = bounds.right - w > bounds.left ? bounds.right - w : bounds.left;
		const double maxTop = bounds.bottom - h > bounds.top ? bounds.bottom - h : bounds.top;
		left = left < bounds.left ? bounds.left : (left > maxLeft ? maxLeft : left);
		top = top < bounds.top ? bounds.top : (top > maxTop ? maxTop : top);
		return { left, top, left + w, top + h };
	}

}
