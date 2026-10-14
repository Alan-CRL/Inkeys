// Inkeys local extension; original StrokeModeler state and output contracts are unchanged.
#ifndef INK_STROKE_MODELER_TERMINAL_TAIL_RECONSTRUCTOR_H_
#define INK_STROKE_MODELER_TERMINAL_TAIL_RECONSTRUCTOR_H_

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

#include "ink_stroke_modeler/types.h"

namespace ink::stroke_model
{
	struct TerminalTailSource
	{
		std::uint64_t first = 0;
		std::uint64_t second = 0;
		double fraction = 0;
	};

	struct TerminalTailReferencePoint
	{
		Vec2 position;
		TerminalTailSource source;
		// 只允许末点标记：人工 pin 的末边不是可靠曲线先验。
		bool weak_terminal = false;
	};

// 只消费空间顺序；物理时间由调用方/报告保留，不推断或使用显示时间。
	struct TerminalTailRawPoint
	{
		Vec2 position;
		std::uint64_t source = 0;
	};

	struct TerminalTailRange
	{
		std::uint64_t begin = 0;
		std::uint64_t end = 0;
	};

	struct TerminalTailRawMatch
	{
		std::size_t segment = 0;
		double fraction = 0;
	};

	struct TerminalTailPoint
	{
		Vec2 position;
		// 调用方属性转移来源与 raw 几何匹配分别保留，不能互相替代。
		TerminalTailSource source;
		TerminalTailRawMatch raw_match;
	};

// 默认值仅用于离线实验，不是通用产品像素阈值；调用方明确提供统一距离单位。
	struct TerminalTailConfig
	{
		std::size_t max_reference_points = 512;
		std::size_t max_raw_points = 512;
		std::size_t max_match_work = 262144;
		std::size_t max_validation_work = 262144;
		std::size_t max_iterations = 8;
		// 距离单位与输入一致；角度为弧度，其余比例无单位。
		double max_displacement = 3;
		double min_length_ratio = 0.85;
		double max_length_ratio = 1.15;
		double raw_weight = 1;
		double displacement_regularization = 0.03;
		double match_band_fraction = 0.15;
		double entry_match_band_fraction = 0.5;
		double min_entry_alignment = 0.25;
		double weak_terminal_regularization_scale = 0.05;
		double max_local_turn_increase = 0.35;
		// 完整尾窗的固定物理弧长采样间隔；调用方随距离单位一起缩放。
		double physical_turn_spacing = 0.5;
		double max_terminal_turn_increase = 0.02;
		double terminal_probe_fraction = 0.15;
		double min_relative_improvement = 0.03;
		double activation_angle = 0.15;
		double max_raw_corner_angle = 1.5;
		double epsilon = 1e-6;
	};

	struct TerminalTailRequest
	{
		std::span<const TerminalTailReferencePoint> reference;
		std::span<const TerminalTailRawPoint> raw;
		Vec2 endpoint;
		// 半开调用方来源区间；版本 token 由调用方在应用前核对。
		TerminalTailRange replacement_range;
		std::uint64_t baseline_token = 0;
		TerminalTailConfig config;
	};

	enum class TerminalTailStatus { kApply, kNoChange, kRejected };
	enum class TerminalTailReason
	{
		kApplied, kUnchanged, kNoImprovement, kInvalidInput, kBudgetExceeded,
		kInsufficientWindow, kAmbiguousMatch, kSharpCorner,
		kExcessiveDisplacement, kLengthChange, kNewIntersection, kExcessiveTurn
	};

	struct TerminalTailDiagnostics
	{
		double reference_length = 0;
		double candidate_length = 0;
		double max_displacement = 0;
		double reference_corridor_rms = 0;
		double candidate_corridor_rms = 0;
		double reference_terminal_turn = 0;
		double candidate_terminal_turn = 0;
		double reference_max_physical_turn = 0;
		double candidate_max_physical_turn = 0;
		std::size_t match_work = 0;
		std::size_t validation_work = 0;
		std::size_t iterations = 0;
		double repair_length = 0;
		double raw_hint_length = 0;
		double sampling_error = 0;
		double max_curve_turn = 0;
		std::size_t window_attempts = 0;
		std::size_t output_points = 0;
		std::size_t changed_begin = 0;
	};

	struct TerminalTailPatch
	{
		TerminalTailRange replacement_range;
		std::uint64_t baseline_token = 0;
		std::span<const TerminalTailPoint> points;
	};

	struct TerminalTailOutcome
	{
		TerminalTailStatus status = TerminalTailStatus::kRejected;
		TerminalTailReason reason = TerminalTailReason::kInvalidInput;
		TerminalTailPatch patch;
		TerminalTailDiagnostics diagnostics;
	};

	// 一次调用独占一个 scratch；返回 span 仅在下次调用或修改 scratch 前有效。
	// NoChange/Rejected 的 patch 恒为空，旧模型及调用方 reference 从不改写。
	struct TerminalTailScratch
	{
		std::vector<TerminalTailPoint> patch_points;
		std::vector<Vec2> candidate;
		std::vector<Vec2> reference_positions;
		std::vector<Vec2> displacement;
		std::vector<Vec2> targets;
		std::vector<TerminalTailRawMatch> matches;
		std::vector<double> reference_arc;
		std::vector<double> candidate_arc;
		std::vector<double> raw_arc;
		std::vector<double> diagonal;
		std::vector<double> upper;
		std::vector<double> rhs_x;
		std::vector<double> rhs_y;
		std::vector<std::size_t> distinct_raw_indices;
	};

	// 固定原始前两个参考点与真实终点；前两点重复则拒绝，不另寻入口。
	TerminalTailOutcome ReconstructTerminalTail(
		const TerminalTailRequest& request, TerminalTailScratch& scratch);

	// 端点平滑位移对照，使用与主候选完全相同的发布检查。
	TerminalTailOutcome ReconstructTerminalTailControl(
		const TerminalTailRequest& request, TerminalTailScratch& scratch);
	// 局部曲线的距离与输入同单位；只拟合末端，不牵动整个缓存窗。
	struct LocalTerminalTailConfig
	{
		double min_repair_length = 6;
		double max_repair_fraction = 0.4;
		double pin_length_multiplier = 12;
		double model_direction_probe = 0.75;
		double raw_position_weight = 0.35;
		std::size_t max_assist_points = 6;
		double activation_angle = 0.3490658503988659;
		double max_terminal_angle = 0.17453292519943295;
	};

	// raw 可为空或稀疏；此时 raw_match 为默认值，没有对应证据。
	// 保持全部点数/属性来源，修正段之前的位置逐点固定，终点硬约束为 Up。
	TerminalTailOutcome ReconstructLocalTerminalTail(
		const TerminalTailRequest& request, TerminalTailScratch& scratch,
		const LocalTerminalTailConfig& config = {});

	// 完成态专用：可见末钩触发，有限扩窗；位移只记录，不作发布硬门。
	struct AdaptiveTerminalTailConfig
	{
		double visibility_distance = 0.5;
		double min_repair_length = 6;
		double initial_pin_multiplier = 4;
		double window_growth = 1.6;
		double sampling_tolerance = 0.15;
		double activation_angle = 0.3490658503988659;
		double max_curve_turn = 0.35;
		std::size_t max_window_attempts = 8;
		std::size_t max_output_points = 2048;
		std::size_t max_subdivision_depth = 12;
	};
	// 输入来源须为精确原顶点；输出保留原参数锚点并允许有序分数来源插点。
	TerminalTailOutcome ReconstructAdaptiveTerminalTail(
		const TerminalTailRequest& request, TerminalTailScratch& scratch,
		const AdaptiveTerminalTailConfig& config = {});

}

#endif
