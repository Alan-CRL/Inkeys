// Inkeys local extension. This solver does not modify StrokeModeler or Result.
#include "ink_stroke_modeler/terminal_tail_reconstructor.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <new>

namespace ink::stroke_model
{
	namespace
	{
		constexpr std::size_t kHardPointLimit = 4096;
		constexpr std::size_t kHardWorkLimit = 16777216;

		double Distance(Vec2 a, Vec2 b)
		{
			return std::hypot(double(a.x) - b.x, double(a.y) - b.y);
		}

		Vec2 Interpolate(Vec2 a, Vec2 b, double t)
		{
			return {static_cast<float>(double(a.x) + (double(b.x) - a.x) * t),
				static_cast<float>(double(a.y) + (double(b.y) - a.y) * t)};
		}

		double Angle(Vec2 a, Vec2 b, Vec2 c, double epsilon)
		{
			const double ax = double(b.x) - a.x, ay = double(b.y) - a.y;
			const double bx = double(c.x) - b.x, by = double(c.y) - b.y;
			const double lengths = std::hypot(ax, ay) * std::hypot(bx, by);
			if (lengths <= epsilon * epsilon) return 0;
			return std::acos(std::clamp((ax * bx + ay * by) / lengths, -1.0, 1.0));
		}

		double Arc(std::span<const Vec2> points, std::vector<double>& arc)
		{
			arc.resize(points.size());
			arc[0] = 0;
			for (std::size_t i = 1; i < points.size(); ++i)
				arc[i] = arc[i - 1] + Distance(points[i - 1], points[i]);
			return arc.back();
		}

		Vec2 AtArc(std::span<const Vec2> points, std::span<const double> arc, double s)
		{
			if (s <= 0) return points.front();
			if (s >= arc.back()) return points.back();
			const auto it = std::upper_bound(arc.begin(), arc.end(), s);
			const std::size_t b = static_cast<std::size_t>(it - arc.begin());
			return Interpolate(points[b - 1], points[b], (s - arc[b - 1]) / (arc[b] - arc[b - 1]));
		}

		double TerminalTurn(std::span<const Vec2> points, std::span<const double> arc,
			const TerminalTailConfig& config)
		{
			const double probe = arc.back() * config.terminal_probe_fraction;
			return Angle(AtArc(points, arc, arc.back() - probe),
				AtArc(points, arc, arc.back() - probe * 0.5), points.back(), config.epsilon);
		}

		bool SourceOrdered(const TerminalTailSource& a, const TerminalTailSource& b)
		{
			// 来源 ID 只表达顺序，不能当连续数值插值或假设等于 Result 下标。
			if (a.second <= b.first) return true;
			return a.first == b.first && a.second == b.second && a.fraction <= b.fraction;
		}

		bool ValidConfig(const TerminalTailConfig& c)
		{
			return c.max_reference_points >= 4 && c.max_reference_points <= kHardPointLimit
				&& c.max_raw_points >= 3 && c.max_raw_points <= kHardPointLimit
				&& c.max_iterations > 0 && c.max_iterations <= 64
				&& c.max_match_work > 0 && c.max_match_work <= kHardWorkLimit
				&& c.max_validation_work > 0 && c.max_validation_work <= kHardWorkLimit
				&& std::isfinite(c.max_displacement) && c.max_displacement > 0
				&& std::isfinite(c.epsilon) && c.epsilon > 0
				&& std::isfinite(c.min_length_ratio) && c.min_length_ratio > 0 && c.min_length_ratio <= 1
				&& std::isfinite(c.max_length_ratio) && c.max_length_ratio >= 1
				&& std::isfinite(c.raw_weight) && c.raw_weight > 0
				&& std::isfinite(c.displacement_regularization) && c.displacement_regularization > 0
				&& std::isfinite(c.match_band_fraction) && c.match_band_fraction > 0 && c.match_band_fraction <= 0.5
				&& std::isfinite(c.entry_match_band_fraction) && c.entry_match_band_fraction > 0 && c.entry_match_band_fraction <= 0.75
				&& std::isfinite(c.min_entry_alignment) && c.min_entry_alignment >= -1 && c.min_entry_alignment <= 1
				&& std::isfinite(c.weak_terminal_regularization_scale) && c.weak_terminal_regularization_scale > 0 && c.weak_terminal_regularization_scale <= 1
				&& std::isfinite(c.max_local_turn_increase) && c.max_local_turn_increase >= 0 && c.max_local_turn_increase <= 3.141592653589793
				&& std::isfinite(c.physical_turn_spacing) && c.physical_turn_spacing > 0
				&& std::isfinite(c.max_terminal_turn_increase) && c.max_terminal_turn_increase >= 0 && c.max_terminal_turn_increase <= 3.141592653589793
				&& std::isfinite(c.terminal_probe_fraction) && c.terminal_probe_fraction > 0 && c.terminal_probe_fraction <= 0.5
				&& std::isfinite(c.min_relative_improvement) && c.min_relative_improvement >= 0 && c.min_relative_improvement < 1
				&& std::isfinite(c.activation_angle) && c.activation_angle >= 0 && c.activation_angle <= 3.141592653589793
				&& std::isfinite(c.max_raw_corner_angle) && c.max_raw_corner_angle > 0 && c.max_raw_corner_angle <= 3.141592653589793;
		}

		TerminalTailOutcome Finish(TerminalTailStatus status, TerminalTailReason reason,
			const TerminalTailDiagnostics& diagnostics, TerminalTailScratch& scratch)
		{
			scratch.patch_points.clear();
			TerminalTailOutcome result;
			result.status = status;
			result.reason = reason;
			result.diagnostics = diagnostics;
			return result;
		}

		bool Spend(std::size_t& work, std::size_t limit)
		{
			if (work >= limit) return false;
			++work;
			return true;
		}

		bool MaximumPhysicalTurn(std::span<const Vec2> points, std::span<const double> arc,
			const TerminalTailConfig& config, TerminalTailDiagnostics& diagnostics, double& maximum)
		{
			// 先在浮点域检查数量，超长路径/极小间隔不能溢出下标或生成无界采样。
			const double samples = std::ceil(arc.back() / config.physical_turn_spacing) + 1;
			if (!std::isfinite(samples) || samples > double(config.max_validation_work - diagnostics.validation_work)) return false;
			const std::size_t intervals = static_cast<std::size_t>(samples - 1);
			Vec2 before{}, previous{};
			for (std::size_t i = 0; i <= intervals; ++i)
			{
				if (!Spend(diagnostics.validation_work, config.max_validation_work)) return false;
				const Vec2 next = i == intervals ? points.back()
					: AtArc(points, arc, double(i) * config.physical_turn_spacing);
				if (i >= 2) maximum = std::max(maximum, Angle(before, previous, next, config.epsilon));
				before = previous;
				previous = next;
			}
			return true;
		}

		struct Projection
		{
			Vec2 position;
			TerminalTailRawMatch match;
			double arc = 0;
			double squared_distance = std::numeric_limits<double>::infinity();
			bool ambiguous = false;
		};

		bool Project(Vec2 point, double lo, double hi, const TerminalTailRequest& request,
			const TerminalTailScratch& scratch, TerminalTailDiagnostics& diagnostics,
			Projection& best, bool entry)
		{
			const auto& raw = request.raw;
			const auto& arc = scratch.raw_arc;
			const auto& c = request.config;
			// lo 是闭边界：保留前一有效段的 t=1，重复 Up 末边为零时仍能匹配终点。
			const auto start = std::lower_bound(arc.begin(), arc.end(), lo);
			std::size_t s = start == arc.begin() ? 0 : static_cast<std::size_t>(start - arc.begin() - 1);
			s = std::min(s, raw.size() - 2);
			for (; s + 1 < raw.size() && arc[s] <= hi; ++s)
			{
				if (!Spend(diagnostics.match_work, c.max_match_work)) return false;
				const double length = arc[s + 1] - arc[s];
				if (length <= c.epsilon || arc[s + 1] < lo) continue;
				const double ax = raw[s].position.x, ay = raw[s].position.y;
				const double dx = double(raw[s + 1].position.x) - ax;
				const double dy = double(raw[s + 1].position.y) - ay;
				if (entry)
				{
					const double tx = double(request.reference[1].position.x) - request.reference[0].position.x;
					const double ty = double(request.reference[1].position.y) - request.reference[0].position.y;
					if ((dx * tx + dy * ty) / (length * std::hypot(tx, ty)) < c.min_entry_alignment) continue;
				}
				const double t = std::clamp(((double(point.x) - ax) * dx + (double(point.y) - ay) * dy) / (length * length),
					std::max(0.0, (lo - arc[s]) / length), std::min(1.0, (hi - arc[s]) / length));
				const Vec2 projected = Interpolate(raw[s].position, raw[s + 1].position, t);
				const double distance = Distance(point, projected);
				const double projected_arc = arc[s] + t * length;
				const bool tied = std::abs(distance * distance - best.squared_distance) <= c.epsilon * c.epsilon;
				const bool separate_branch = std::abs(projected_arc - best.arc) > arc.back() * c.match_band_fraction;
				if (distance * distance < best.squared_distance)
					best = {projected, {s, t}, projected_arc, distance * distance, tied && separate_branch};
				else if (tied && separate_branch) best.ambiguous = true;
			}
			return std::isfinite(best.squared_distance);
		}

		TerminalTailReason Match(std::span<const Vec2> points, const Projection& entry,
			const TerminalTailRequest& request, TerminalTailScratch& scratch,
			TerminalTailDiagnostics& diagnostics, double& rms)
		{
			const auto n = points.size();
			const double remaining = scratch.raw_arc.back() - entry.arc;
			if (remaining <= request.config.epsilon) return TerminalTailReason::kInsufficientWindow;
			const double band = remaining * request.config.match_band_fraction;
			double previous = entry.arc, sum = 0, weight_sum = 0;
			for (std::size_t i = 0; i < n; ++i)
			{
				Projection projected;
				if (i == 0) projected = entry;
				else if (i + 1 == n)
					projected = {request.endpoint, {request.raw.size() - 2, 1}, scratch.raw_arc.back(), 0};
				else
				{
					const double expected = entry.arc + scratch.reference_arc[i] / scratch.reference_arc.back() * remaining;
					const double lo = std::max(previous, expected - band);
					const double hi = std::min(scratch.raw_arc.back(), expected + band);
					if (lo > hi || !Project(points[i], lo, hi, request, scratch, diagnostics, projected, false))
						return diagnostics.match_work >= request.config.max_match_work
							? TerminalTailReason::kBudgetExceeded : TerminalTailReason::kAmbiguousMatch;
				}
				if (projected.ambiguous) return TerminalTailReason::kAmbiguousMatch;
				scratch.targets[i] = projected.position;
				scratch.matches[i] = projected.match;
				previous = projected.arc;
				// 真实空间区间的梯形积分；重复/停笔节点不因点数多而增加证据权重。
				const double weight = ((i > 0 ? scratch.reference_arc[i] - scratch.reference_arc[i - 1] : 0)
					+ (i + 1 < n ? scratch.reference_arc[i + 1] - scratch.reference_arc[i] : 0)) * 0.5;
				const double distance = Distance(points[i], projected.position);
				sum += weight * distance * distance;
				weight_sum += weight;
			}
			rms = std::sqrt(sum / weight_sum);
			return std::isfinite(rms) ? TerminalTailReason::kApplied : TerminalTailReason::kInvalidInput;
		}

		bool AdjacentOverlap(Vec2 a, Vec2 b, Vec2 c, double epsilon)
		{
			const double ax = double(b.x) - a.x, ay = double(b.y) - a.y;
			const double bx = double(c.x) - b.x, by = double(c.y) - b.y;
			const double left = std::hypot(ax, ay), right = std::hypot(bx, by);
			if (left <= epsilon || right <= epsilon) return false;
			return std::abs(ax * by - ay * bx) <= epsilon * std::max(left, right)
				&& ax * bx + ay * by < 0;
		}

		bool Intersects(Vec2 a, Vec2 b, Vec2 c, Vec2 d, double epsilon)
		{
			const auto cross = [](Vec2 p, Vec2 q, Vec2 r)
			{
				return (double(q.x) - p.x) * (double(r.y) - p.y) - (double(q.y) - p.y) * (double(r.x) - p.x);
			};
			if (Distance(a, b) <= epsilon || Distance(c, d) <= epsilon) return false;
			if (std::max(a.x, b.x) + epsilon < std::min(c.x, d.x)
				|| std::max(c.x, d.x) + epsilon < std::min(a.x, b.x)
				|| std::max(a.y, b.y) + epsilon < std::min(c.y, d.y)
				|| std::max(c.y, d.y) + epsilon < std::min(a.y, b.y)) return false;
			const double tolerance = epsilon * std::max({Distance(a, b), Distance(c, d), epsilon});
			const double ac = cross(a, b, c), ad = cross(a, b, d);
			const double ca = cross(c, d, a), cb = cross(c, d, b);
			return ((ac > tolerance && ad < -tolerance) || (ac < -tolerance && ad > tolerance)
				|| std::abs(ac) <= tolerance || std::abs(ad) <= tolerance)
				&& ((ca > tolerance && cb < -tolerance) || (ca < -tolerance && cb > tolerance)
					|| std::abs(ca) <= tolerance || std::abs(cb) <= tolerance);
		}

		TerminalTailOutcome Run(const TerminalTailRequest& request, TerminalTailScratch& scratch, bool control)
		{
			scratch.patch_points.clear();
			TerminalTailDiagnostics diagnostics;
			const auto& c = request.config;
			const std::size_t n = request.reference.size(), m = request.raw.size();
			const auto reject = [&](TerminalTailReason reason)
			{
				return Finish(TerminalTailStatus::kRejected, reason, diagnostics, scratch);
			};
			if (!ValidConfig(c) || !request.endpoint.IsFinite() || request.replacement_range.end <= request.replacement_range.begin
				|| (n && !request.reference.data()) || (m && !request.raw.data())) return reject(TerminalTailReason::kInvalidInput);
			if (n > c.max_reference_points || m > c.max_raw_points) return reject(TerminalTailReason::kBudgetExceeded);
			if (n < 4 || m < 3) return reject(TerminalTailReason::kInsufficientWindow);
			for (std::size_t i = 0; i < n; ++i)
			{
				const auto& p = request.reference[i];
				if (!p.position.IsFinite() || !std::isfinite(p.source.fraction)
					|| p.source.fraction < 0 || p.source.fraction > 1 || p.source.first > p.source.second
					|| p.source.first < request.replacement_range.begin || p.source.second >= request.replacement_range.end
					|| (p.weak_terminal && i + 1 != n) || (i && !SourceOrdered(request.reference[i - 1].source, p.source)))
					return reject(TerminalTailReason::kInvalidInput);
			}
			for (std::size_t i = 0; i < m; ++i)
				if (!request.raw[i].position.IsFinite() || (i && request.raw[i].source <= request.raw[i - 1].source))
					return reject(TerminalTailReason::kInvalidInput);
			if (!(request.raw.back().position == request.endpoint)) return reject(TerminalTailReason::kInvalidInput);
			if (Distance(request.reference[0].position, request.reference[1].position) <= c.epsilon)
				return reject(TerminalTailReason::kInsufficientWindow);
			scratch.candidate.resize(n);
			scratch.reference_positions.resize(n);
			scratch.displacement.resize(n);
			scratch.targets.resize(n);
			scratch.matches.resize(n);
			scratch.diagonal.resize(n);
			scratch.upper.resize(n);
			scratch.rhs_x.resize(n);
			scratch.rhs_y.resize(n);
			for (std::size_t i = 0; i < n; ++i)
				scratch.candidate[i] = scratch.reference_positions[i] = request.reference[i].position;
			diagnostics.reference_length = Arc(scratch.candidate, scratch.reference_arc);
			scratch.raw_arc.resize(m);
			scratch.raw_arc[0] = 0;
			scratch.distinct_raw_indices.clear();
			scratch.distinct_raw_indices.push_back(0);
			for (std::size_t i = 1; i < m; ++i)
			{
				const double length = Distance(request.raw[i - 1].position, request.raw[i].position);
				scratch.raw_arc[i] = scratch.raw_arc[i - 1] + length;
				if (Distance(request.raw[scratch.distinct_raw_indices.back()].position, request.raw[i].position) > c.epsilon)
					scratch.distinct_raw_indices.push_back(i);
			}
			if (!std::isfinite(diagnostics.reference_length) || diagnostics.reference_length <= c.epsilon
				|| scratch.distinct_raw_indices.size() < 3 || scratch.raw_arc.back() <= c.epsilon) return reject(TerminalTailReason::kInsufficientWindow);
			Projection entry;
			if (!Project(scratch.candidate[0], 0, scratch.raw_arc.back() * c.entry_match_band_fraction,
				request, scratch, diagnostics, entry, true))
				return reject(diagnostics.match_work >= c.max_match_work ? TerminalTailReason::kBudgetExceeded : TerminalTailReason::kAmbiguousMatch);
			if (entry.ambiguous) return reject(TerminalTailReason::kAmbiguousMatch);
			if (std::sqrt(entry.squared_distance) > c.max_displacement) return reject(TerminalTailReason::kInsufficientWindow);
			// 有真实尖角/折返证据时不将它当平滑末钩；这类窗口留给调用方原子回退。
			// 角点证据使用不同位置的相邻节点，停笔/重复消费不会遮住真实转折。
			for (std::size_t k = 1; k + 1 < scratch.distinct_raw_indices.size(); ++k)
			{
				if (!Spend(diagnostics.validation_work, c.max_validation_work)) return reject(TerminalTailReason::kBudgetExceeded);
				const std::size_t a = scratch.distinct_raw_indices[k - 1];
				const std::size_t i = scratch.distinct_raw_indices[k];
				const std::size_t b = scratch.distinct_raw_indices[k + 1];
				if (scratch.raw_arc[i] < entry.arc) continue;
				if (Angle(request.raw[a].position, request.raw[i].position, request.raw[b].position, c.epsilon) > c.max_raw_corner_angle)
					return reject(TerminalTailReason::kSharpCorner);
			}
			auto reason = Match(scratch.candidate, entry, request, scratch, diagnostics, diagnostics.reference_corridor_rms);
			if (reason != TerminalTailReason::kApplied) return reject(reason);
			diagnostics.reference_terminal_turn = TerminalTurn(scratch.candidate, scratch.reference_arc, c);
			const Vec2 last = request.reference.back().position;
			const double delta_x = double(request.endpoint.x) - last.x, delta_y = double(request.endpoint.y) - last.y;
			if (std::hypot(delta_x, delta_y) > c.max_displacement) return reject(TerminalTailReason::kExcessiveDisplacement);
			const double free_arc = diagnostics.reference_length - scratch.reference_arc[1];
			if (free_arc <= c.epsilon) return reject(TerminalTailReason::kInsufficientWindow);
			double control_x = delta_x, control_y = delta_y;
			if (control && request.reference.back().weak_terminal)
			{
				// 已 pin 的末点没有端点残差；把最后可靠点到 Up 的连接量分配到可靠尾段。
				// 使用包含连接边的完整弧长，倒数第二点不会被强行推到 Up 而产生重复端点。
				control_x = double(request.endpoint.x) - request.reference[n - 2].position.x;
				control_y = double(request.endpoint.y) - request.reference[n - 2].position.y;
			}
			for (std::size_t i = 0; i < n; ++i)
			{
				const double t = std::max(0.0, (scratch.reference_arc[i] - scratch.reference_arc[1]) / free_arc);
				const double weight = t * t * (3 - 2 * t);
				scratch.displacement[i] = {static_cast<float>(weight * control_x), static_cast<float>(weight * control_y)};
				scratch.candidate[i] = {static_cast<float>(double(request.reference[i].position.x) + weight * control_x),
					static_cast<float>(double(request.reference[i].position.y) + weight * control_y)};
			}
			scratch.candidate[0] = request.reference[0].position;
			scratch.candidate[1] = request.reference[1].position;
			scratch.candidate.back() = request.endpoint;
			scratch.displacement.back() = {static_cast<float>(delta_x), static_cast<float>(delta_y)};
			if (!control)
			{
				// q=m+d，只正则位移的一阶变化；不直接压平原曲线曲率。
				for (std::size_t iteration = 0; iteration < c.max_iterations; ++iteration)
				{
					double ignored = 0;
					reason = Match(scratch.candidate, entry, request, scratch, diagnostics, ignored);
					if (reason != TerminalTailReason::kApplied) return reject(reason);
					for (std::size_t i = 2; i + 1 < n; ++i)
					{
						const double left_h = std::max((scratch.reference_arc[i] - scratch.reference_arc[i - 1]) / diagnostics.reference_length, c.epsilon / diagnostics.reference_length);
						const double right_h = std::max((scratch.reference_arc[i + 1] - scratch.reference_arc[i]) / diagnostics.reference_length, c.epsilon / diagnostics.reference_length);
						const double left = c.displacement_regularization / left_h;
						const double right = c.displacement_regularization / right_h
							* (i + 2 == n && request.reference.back().weak_terminal ? c.weak_terminal_regularization_scale : 1.0);
						const double data_weight = c.raw_weight * (scratch.reference_arc[i + 1] - scratch.reference_arc[i - 1])
							/ diagnostics.reference_length * 0.5;
						scratch.diagonal[i] = data_weight + left + right;
						scratch.upper[i] = -right;
						scratch.rhs_x[i] = data_weight * (double(scratch.targets[i].x) - request.reference[i].position.x);
						scratch.rhs_y[i] = data_weight * (double(scratch.targets[i].y) - request.reference[i].position.y);
						if (i + 2 == n)
						{
							scratch.rhs_x[i] += right * delta_x;
							scratch.rhs_y[i] += right * delta_y;
						}
						if (i > 2)
						{
							const double multiplier = -left / scratch.diagonal[i - 1];
							scratch.diagonal[i] -= multiplier * scratch.upper[i - 1];
							scratch.rhs_x[i] -= multiplier * scratch.rhs_x[i - 1];
							scratch.rhs_y[i] -= multiplier * scratch.rhs_y[i - 1];
						}
						if (!std::isfinite(scratch.diagonal[i]) || scratch.diagonal[i] <= 0) return reject(TerminalTailReason::kInvalidInput);
					}
					double max_change = 0;
					for (std::size_t i = n - 2; i >= 2; --i)
					{
						const double x = (scratch.rhs_x[i] - (i + 2 < n ? scratch.upper[i] * scratch.displacement[i + 1].x : 0)) / scratch.diagonal[i];
						const double y = (scratch.rhs_y[i] - (i + 2 < n ? scratch.upper[i] * scratch.displacement[i + 1].y : 0)) / scratch.diagonal[i];
						if (!std::isfinite(x) || !std::isfinite(y) || std::hypot(x, y) > c.max_displacement)
							return reject(TerminalTailReason::kExcessiveDisplacement);
						const Vec2 next{static_cast<float>(double(request.reference[i].position.x) + x),
							static_cast<float>(double(request.reference[i].position.y) + y)};
						max_change = std::max(max_change, Distance(next, scratch.candidate[i]));
						scratch.candidate[i] = next;
						scratch.displacement[i] = {static_cast<float>(x), static_cast<float>(y)};
					}
					diagnostics.iterations = iteration + 1;
					if (max_change <= c.epsilon) break;
				}
			}
			diagnostics.candidate_length = Arc(scratch.candidate, scratch.candidate_arc);
			diagnostics.candidate_terminal_turn = TerminalTurn(scratch.candidate, scratch.candidate_arc, c);
			reason = Match(scratch.candidate, entry, request, scratch, diagnostics, diagnostics.candidate_corridor_rms);
			if (reason != TerminalTailReason::kApplied) return reject(reason);
			bool changed = false;
			for (std::size_t i = 0; i < n; ++i)
			{
				if (!scratch.candidate[i].IsFinite()) return reject(TerminalTailReason::kInvalidInput);
				diagnostics.max_displacement = std::max(diagnostics.max_displacement, Distance(scratch.candidate[i], request.reference[i].position));
				changed |= !(scratch.candidate[i] == request.reference[i].position);
				if (i > 0 && i + 1 < n)
				{
					if (!Spend(diagnostics.validation_work, c.max_validation_work)) return reject(TerminalTailReason::kBudgetExceeded);
					const double before_turn = Angle(request.reference[i - 1].position, request.reference[i].position, request.reference[i + 1].position, c.epsilon);
					const double after_turn = Angle(scratch.candidate[i - 1], scratch.candidate[i], scratch.candidate[i + 1], c.epsilon);
					if (after_turn > before_turn + c.max_local_turn_increase) return reject(TerminalTailReason::kExcessiveTurn);
				}
				if (i && scratch.reference_arc[i] - scratch.reference_arc[i - 1] > c.epsilon
					&& scratch.candidate_arc[i] - scratch.candidate_arc[i - 1] <= c.epsilon) return reject(TerminalTailReason::kLengthChange);
			}
			if (diagnostics.max_displacement > c.max_displacement) return reject(TerminalTailReason::kExcessiveDisplacement);
			const double ratio = diagnostics.candidate_length / diagnostics.reference_length;
			if (!std::isfinite(ratio) || ratio < c.min_length_ratio || ratio > c.max_length_ratio) return reject(TerminalTailReason::kLengthChange);
			if (!changed) return Finish(TerminalTailStatus::kNoChange, TerminalTailReason::kUnchanged, diagnostics, scratch);
			// 顶点角度受采样密度影响，另按固定物理弧长检查完整尾窗，避免短环被压成急弯。
			if (!MaximumPhysicalTurn(scratch.reference_positions, scratch.reference_arc, c, diagnostics, diagnostics.reference_max_physical_turn)
				|| !MaximumPhysicalTurn(scratch.candidate, scratch.candidate_arc, c, diagnostics, diagnostics.candidate_max_physical_turn))
				return reject(TerminalTailReason::kBudgetExceeded);
			if (diagnostics.candidate_max_physical_turn > diagnostics.reference_max_physical_turn + c.max_local_turn_increase)
				return reject(TerminalTailReason::kExcessiveTurn);
			// 相邻边共用端点，不属于普通非相邻交点检查；近折返时也不能新增反向重叠。
			// 跳过重复位置但保留原索引，以便与对应参考几何比较。
			std::size_t previous = 0, current = 0;
			for (std::size_t next = 1; next < n; ++next)
			{
				if (Distance(scratch.candidate[current], scratch.candidate[next]) <= c.epsilon) continue;
				if (previous != current)
				{
					if (!Spend(diagnostics.validation_work, c.max_validation_work)) return reject(TerminalTailReason::kBudgetExceeded);
					if (AdjacentOverlap(scratch.candidate[previous], scratch.candidate[current], scratch.candidate[next], c.epsilon)
						&& !AdjacentOverlap(request.reference[previous].position, request.reference[current].position,
							request.reference[next].position, c.epsilon)) return reject(TerminalTailReason::kNewIntersection);
				}
				previous = current;
				current = next;
			}
			for (std::size_t i = 0; i + 1 < n; ++i)
				for (std::size_t j = i + 2; j + 1 < n; ++j)
				{
					if (!Spend(diagnostics.validation_work, c.max_validation_work)) return reject(TerminalTailReason::kBudgetExceeded);
					if (!Intersects(scratch.candidate[i], scratch.candidate[i + 1], scratch.candidate[j], scratch.candidate[j + 1], c.epsilon)) continue;
					if (!Spend(diagnostics.validation_work, c.max_validation_work)) return reject(TerminalTailReason::kBudgetExceeded);
					if (!Intersects(request.reference[i].position, request.reference[i + 1].position,
						request.reference[j].position, request.reference[j + 1].position, c.epsilon)) return reject(TerminalTailReason::kNewIntersection);
				}
			const double gain = 1 - c.min_relative_improvement;
			const bool improved_corridor = diagnostics.candidate_corridor_rms + c.epsilon < diagnostics.reference_corridor_rms * gain;
			const bool improved_turn = diagnostics.candidate_terminal_turn + c.epsilon < diagnostics.reference_terminal_turn * gain;
			const bool no_turn_damage = diagnostics.candidate_terminal_turn <= diagnostics.reference_terminal_turn + c.max_terminal_turn_increase;
			const bool no_corridor_damage = diagnostics.candidate_corridor_rms <= diagnostics.reference_corridor_rms * (1 + c.min_relative_improvement) + c.epsilon;
			const bool endpoint_needed = std::hypot(delta_x, delta_y) > c.epsilon;
			if (!endpoint_needed && (diagnostics.reference_terminal_turn < c.activation_angle
				|| !((improved_corridor && no_turn_damage) || (improved_turn && no_corridor_damage))))
				return Finish(TerminalTailStatus::kNoChange, TerminalTailReason::kNoImprovement, diagnostics, scratch);
			// 所有硬门禁通过后才发布；保持原来源/点数，raw 对应只作独立几何证据。
			scratch.patch_points.reserve(n);
			for (std::size_t i = 0; i < n; ++i)
				scratch.patch_points.push_back({scratch.candidate[i], request.reference[i].source, scratch.matches[i]});
			TerminalTailOutcome result;
			result.status = TerminalTailStatus::kApply;
			result.reason = TerminalTailReason::kApplied;
			result.diagnostics = diagnostics;
			result.patch = {request.replacement_range, request.baseline_token, scratch.patch_points};
			return result;
		}

		struct LocalVector
		{
			double x = 0, y = 0;
		};
		LocalVector Difference(Vec2 a, Vec2 b) { return {double(a.x) - b.x, double(a.y) - b.y}; }
		double Dot(LocalVector a, LocalVector b) { return a.x * b.x + a.y * b.y; }
		bool Normalize(LocalVector& v, double epsilon)
		{
			const double length = std::hypot(v.x, v.y);
			if (!std::isfinite(length) || length <= epsilon) return false;
			v.x /= length; v.y /= length;
			return true;
		}
		bool ValidLocalConfig(const LocalTerminalTailConfig& c)
		{
			return std::isfinite(c.min_repair_length) && c.min_repair_length > 0
				&& std::isfinite(c.max_repair_fraction) && c.max_repair_fraction > 0 && c.max_repair_fraction <= 1
				&& std::isfinite(c.pin_length_multiplier) && c.pin_length_multiplier > 0
				&& std::isfinite(c.model_direction_probe) && c.model_direction_probe > 0
				&& std::isfinite(c.raw_position_weight) && c.raw_position_weight >= 0
				&& c.max_assist_points >= 2 && c.max_assist_points <= 64
				&& std::isfinite(c.activation_angle) && c.activation_angle >= 0 && c.activation_angle <= 3.141592653589793
				&& std::isfinite(c.max_terminal_angle) && c.max_terminal_angle >= 0 && c.max_terminal_angle <= 3.141592653589793;
		}

		TerminalTailOutcome RunLocal(const TerminalTailRequest& request, TerminalTailScratch& scratch,
			const LocalTerminalTailConfig& local)
		{
			scratch.patch_points.clear();
			TerminalTailDiagnostics diagnostics;
			const auto& c = request.config;
			const std::size_t n = request.reference.size(), m = request.raw.size();
			const auto reject = [&](TerminalTailReason reason)
			{ return Finish(TerminalTailStatus::kRejected, reason, diagnostics, scratch); };
			if (!ValidConfig(c) || !ValidLocalConfig(local) || !request.endpoint.IsFinite()
				|| request.replacement_range.end <= request.replacement_range.begin
				|| (n && !request.reference.data()) || (m && !request.raw.data())) return reject(TerminalTailReason::kInvalidInput);
			if (n > c.max_reference_points || m > c.max_raw_points) return reject(TerminalTailReason::kBudgetExceeded);
			if (n < 5) return reject(TerminalTailReason::kInsufficientWindow);
			for (std::size_t i = 0; i < n; ++i)
			{
				const auto& p = request.reference[i];
				if (!p.position.IsFinite() || !std::isfinite(p.source.fraction) || p.source.fraction < 0 || p.source.fraction > 1
					|| p.source.first > p.source.second || p.source.first < request.replacement_range.begin
					|| p.source.second >= request.replacement_range.end || (p.weak_terminal && i + 1 != n)
					|| (i && !SourceOrdered(request.reference[i - 1].source, p.source))) return reject(TerminalTailReason::kInvalidInput);
			}
			for (std::size_t i = 0; i < m; ++i)
				if (!request.raw[i].position.IsFinite() || (i && request.raw[i].source <= request.raw[i - 1].source))
					return reject(TerminalTailReason::kInvalidInput);
			if (Distance(request.reference[0].position, request.reference[1].position) <= c.epsilon)
				return reject(TerminalTailReason::kInsufficientWindow);
			scratch.reference_positions.resize(n);
			scratch.candidate.resize(n);
			scratch.matches.assign(n, {});
			for (std::size_t i = 0; i < n; ++i) scratch.candidate[i] = scratch.reference_positions[i] = request.reference[i].position;
			diagnostics.reference_length = Arc(scratch.reference_positions, scratch.reference_arc);
			if (!std::isfinite(diagnostics.reference_length) || diagnostics.reference_length <= c.epsilon)
				return reject(TerminalTailReason::kInsufficientWindow);
			// 人工 pin 的末边只用于触发与终点，不用作末端方向或强形状先验。
			diagnostics.reference_terminal_turn = Angle(scratch.reference_positions[n - 3], scratch.reference_positions[n - 2], request.endpoint, c.epsilon);
			if (diagnostics.reference_terminal_turn < local.activation_angle)
				return Finish(TerminalTailStatus::kNoChange, TerminalTailReason::kUnchanged, diagnostics, scratch);
			const double pin_length = Distance(scratch.reference_positions[n - 2], request.endpoint);
			const double reach = std::min(diagnostics.reference_length * local.max_repair_fraction,
				std::max(local.min_repair_length, local.pin_length_multiplier * pin_length));
			std::size_t start = static_cast<std::size_t>(std::lower_bound(scratch.reference_arc.begin(), scratch.reference_arc.end(),
				diagnostics.reference_length - reach) - scratch.reference_arc.begin());
			start = std::clamp(start, std::size_t(1), n - 4);
			const double free_length = diagnostics.reference_length - scratch.reference_arc[start];
			if (free_length <= c.epsilon) return reject(TerminalTailReason::kInsufficientWindow);
			LocalVector entry = Difference(scratch.reference_positions[start], scratch.reference_positions[start - 1]);
			const double reliable_end = scratch.reference_arc[n - 2];
			std::size_t probe = static_cast<std::size_t>(std::lower_bound(scratch.reference_arc.begin(), scratch.reference_arc.begin() + n - 1,
				std::max(0.0, reliable_end - local.model_direction_probe)) - scratch.reference_arc.begin());
			if (probe) --probe;
			LocalVector terminal = Difference(scratch.reference_positions[n - 2], scratch.reference_positions[probe]);
			if (!Normalize(entry, c.epsilon) || !Normalize(terminal, c.epsilon)) return reject(TerminalTailReason::kInsufficientWindow);
			// 原始输入是可选证据；重复点不增加权重，最多选择固定数量的后半窗节点。
			scratch.raw_arc.resize(m);
			scratch.distinct_raw_indices.clear();
			for (std::size_t i = 0; i < m; ++i)
			{
				scratch.raw_arc[i] = i ? scratch.raw_arc[i - 1] + Distance(request.raw[i - 1].position, request.raw[i].position) : 0;
				if (scratch.distinct_raw_indices.empty() || Distance(request.raw[scratch.distinct_raw_indices.back()].position, request.raw[i].position) > c.epsilon)
					scratch.distinct_raw_indices.push_back(i);
			}
			std::size_t selected[64]{};
			double selected_arc[64]{};
			const std::size_t distinct = scratch.distinct_raw_indices.size();
			const std::size_t first = distinct > 4 ? distinct / 2 : 0;
			const std::size_t count = std::min(local.max_assist_points, distinct - first);
			for (std::size_t k = 0; k < count; ++k)
				selected[k] = scratch.distinct_raw_indices[first + (count > 1 ? k * (distinct - first - 1) / (count - 1) : 0)];
			// 有序投影到模型弧长，拒绝同距跨分支；不按显示时间硬匹配。
			double previous_arc = 0;
			for (std::size_t k = 0; k < count; ++k)
			{
				double best_distance = std::numeric_limits<double>::infinity(), best_arc = 0;
				bool ambiguous = false;
				for (std::size_t i = 0; i + 1 < n; ++i)
				{
					if (!Spend(diagnostics.match_work, c.max_match_work)) return reject(TerminalTailReason::kBudgetExceeded);
					const double length = scratch.reference_arc[i + 1] - scratch.reference_arc[i];
					if (length <= c.epsilon || scratch.reference_arc[i + 1] < previous_arc) continue;
					const auto v = Difference(scratch.reference_positions[i + 1], scratch.reference_positions[i]);
					const auto offset = Difference(request.raw[selected[k]].position, scratch.reference_positions[i]);
					const double t = std::clamp(Dot(offset, v) / (length * length),
						std::max(0.0, (previous_arc - scratch.reference_arc[i]) / length), 1.0);
					const double where = scratch.reference_arc[i] + t * length;
					const double distance = Distance(request.raw[selected[k]].position, Interpolate(scratch.reference_positions[i], scratch.reference_positions[i + 1], t));
					const bool tied = std::abs(distance - best_distance) <= c.epsilon;
					const bool separate = std::abs(where - best_arc) > diagnostics.reference_length * c.match_band_fraction;
					if (distance < best_distance) { ambiguous = tied && separate; best_distance = distance; best_arc = where; }
					else if (tied && separate) ambiguous = true;
				}
				if (!std::isfinite(best_distance) || ambiguous) return reject(TerminalTailReason::kAmbiguousMatch);
				selected_arc[k] = previous_arc = best_arc;
			}
			// 只保护将被重构段内的真实转角；前段角点不应阻止修正后面的末钩。
			for (std::size_t k = 1; k + 1 < count; ++k)
				if (selected_arc[k] > scratch.reference_arc[start]
					&& Angle(request.raw[selected[k - 1]].position, request.raw[selected[k]].position,
						request.raw[selected[k + 1]].position, c.epsilon) > c.max_raw_corner_angle)
					return reject(TerminalTailReason::kSharpCorner);
			if (count >= 2)
			{
				// 最后四个辅助点做二次最小二乘方向；稀疏两点按较低可信度混入模型方向。
				const std::size_t begin = count > 4 ? count - 4 : 0;
				const std::size_t terms = count - begin >= 3 ? 3 : 2;
				double matrix[3][5]{};
				const double scale = scratch.raw_arc[selected[count - 1]] - scratch.raw_arc[selected[begin]];
				if (scale > c.epsilon)
				{
					for (std::size_t k = begin; k < count; ++k)
					{
						const double z = (scratch.raw_arc[selected[k]] - scratch.raw_arc[selected[count - 1]]) / scale;
						const double powers[3]{1, z, z * z};
						for (std::size_t a = 0; a < terms; ++a)
						{
							for (std::size_t b = 0; b < terms; ++b) matrix[a][b] += powers[a] * powers[b];
							matrix[a][3] += powers[a] * request.raw[selected[k]].position.x;
							matrix[a][4] += powers[a] * request.raw[selected[k]].position.y;
						}
					}
					bool solved = true;
					for (std::size_t a = 0; a < terms; ++a)
					{
						std::size_t pivot = a;
						for (std::size_t b = a + 1; b < terms; ++b) if (std::abs(matrix[b][a]) > std::abs(matrix[pivot][a])) pivot = b;
						if (std::abs(matrix[pivot][a]) <= 1e-12) { solved = false; break; }
						for (std::size_t b = 0; b < 5; ++b) std::swap(matrix[a][b], matrix[pivot][b]);
						const double divisor = matrix[a][a];
						for (std::size_t b = 0; b < 5; ++b) matrix[a][b] /= divisor;
						for (std::size_t row = 0; row < terms; ++row) if (row != a)
						{
							const double factor = matrix[row][a];
							for (std::size_t b = 0; b < 5; ++b) matrix[row][b] -= factor * matrix[a][b];
						}
					}
					LocalVector direction{matrix[1][3], matrix[1][4]};
					if (solved && Normalize(direction, c.epsilon))
					{
						const double confidence = std::min(1.0, double(count - begin - 1) / 3);
						LocalVector blend{confidence * direction.x + (1 - confidence) * terminal.x,
							confidence * direction.y + (1 - confidence) * terminal.y};
						if (Normalize(blend, c.epsilon)) terminal = blend;
					}
				}
			}
			const Vec2 p0 = scratch.reference_positions[start], p3 = request.endpoint;
			const auto basis = [&](double t, LocalVector& base, LocalVector& a, LocalVector& b)
			{
				const double u = 1 - t, left = u * u * u + 3 * u * u * t, right = 3 * u * t * t + t * t * t;
				base = {left * p0.x + right * p3.x, left * p0.y + right * p3.y};
				a = {3 * u * u * t * entry.x, 3 * u * u * t * entry.y};
				b = {-3 * u * t * t * terminal.x, -3 * u * t * t * terminal.y};
			};
			double aa = 0, ab = 0, bb = 0, ar = 0, br = 0;
			const auto accumulate = [&](double t, Vec2 target, double weight)
			{
				LocalVector base, a, b; basis(t, base, a, b);
				const LocalVector residual{double(target.x) - base.x, double(target.y) - base.y};
				aa += weight * Dot(a, a); ab += weight * Dot(a, b); bb += weight * Dot(b, b);
				ar += weight * Dot(a, residual); br += weight * Dot(b, residual);
			};
			const double weight_length = scratch.reference_arc[n - 2] - scratch.reference_arc[start];
			if (weight_length <= c.epsilon) return reject(TerminalTailReason::kInsufficientWindow);
			for (std::size_t i = start; i + 2 < n; ++i)
				accumulate((scratch.reference_arc[i] - scratch.reference_arc[start]) / free_length, scratch.reference_positions[i],
					(scratch.reference_arc[i + 1] - scratch.reference_arc[i]) / weight_length);
			std::size_t used = 0;
			for (std::size_t k = 0; k < count; ++k)
			{
				const double t = (selected_arc[k] - scratch.reference_arc[start]) / free_length;
				if (t > .01 && t < .999) ++used;
			}
			for (std::size_t k = 0; k < count; ++k)
			{
				const double t = (selected_arc[k] - scratch.reference_arc[start]) / free_length;
				if (t > .01 && t < .999) accumulate(t, request.raw[selected[k]].position, local.raw_position_weight / double(used));
			}
			const double determinant = aa * bb - ab * ab;
			if (!std::isfinite(determinant) || determinant <= 1e-12 * aa * bb) return reject(TerminalTailReason::kInsufficientWindow);
			const double h0 = (ar * bb - br * ab) / determinant, h1 = (br * aa - ar * ab) / determinant;
			if (!std::isfinite(h0) || !std::isfinite(h1) || h0 <= c.epsilon || h1 <= c.epsilon)
				return reject(TerminalTailReason::kExcessiveTurn);
			for (std::size_t i = start + 1; i + 1 < n; ++i)
			{
				LocalVector base, a, b; basis((scratch.reference_arc[i] - scratch.reference_arc[start]) / free_length, base, a, b);
				const double x = base.x + a.x * h0 + b.x * h1, y = base.y + a.y * h0 + b.y * h1;
				// 极大有限输入也必须在窄化前拒绝，避免超出 float 的可表示范围。
				const double limit = std::numeric_limits<float>::max();
				if (!std::isfinite(x) || !std::isfinite(y) || std::abs(x) > limit || std::abs(y) > limit)
					return reject(TerminalTailReason::kInvalidInput);
				scratch.candidate[i] = {static_cast<float>(x), static_cast<float>(y)};
			}
			scratch.candidate.back() = request.endpoint;
			diagnostics.iterations = 1;
			diagnostics.candidate_length = Arc(scratch.candidate, scratch.candidate_arc);
			diagnostics.candidate_terminal_turn = Angle(scratch.candidate[n - 3], scratch.candidate[n - 2], scratch.candidate.back(), c.epsilon);
			if (diagnostics.candidate_terminal_turn > local.max_terminal_angle
				|| diagnostics.candidate_terminal_turn + c.epsilon >= diagnostics.reference_terminal_turn * (1 - c.min_relative_improvement))
				return Finish(TerminalTailStatus::kNoChange, TerminalTailReason::kNoImprovement, diagnostics, scratch);
			for (std::size_t i = 0; i < n; ++i)
			{
				if (!scratch.candidate[i].IsFinite()) return reject(TerminalTailReason::kInvalidInput);
				diagnostics.max_displacement = std::max(diagnostics.max_displacement, Distance(scratch.candidate[i], scratch.reference_positions[i]));
				if (i && scratch.reference_arc[i] - scratch.reference_arc[i - 1] > c.epsilon
					&& scratch.candidate_arc[i] - scratch.candidate_arc[i - 1] <= c.epsilon) return reject(TerminalTailReason::kLengthChange);
				if (i && i + 1 < n)
				{
					if (!Spend(diagnostics.validation_work, c.max_validation_work)) return reject(TerminalTailReason::kBudgetExceeded);
					if (Angle(scratch.candidate[i - 1], scratch.candidate[i], scratch.candidate[i + 1], c.epsilon)
						> Angle(scratch.reference_positions[i - 1], scratch.reference_positions[i], scratch.reference_positions[i + 1], c.epsilon) + c.max_local_turn_increase)
						return reject(TerminalTailReason::kExcessiveTurn);
				}
			}
			if (diagnostics.max_displacement > c.max_displacement) return reject(TerminalTailReason::kExcessiveDisplacement);
			const double ratio = diagnostics.candidate_length / diagnostics.reference_length;
			if (!std::isfinite(ratio) || ratio < c.min_length_ratio || ratio > c.max_length_ratio) return reject(TerminalTailReason::kLengthChange);
			if (!MaximumPhysicalTurn(scratch.reference_positions, scratch.reference_arc, c, diagnostics, diagnostics.reference_max_physical_turn)
				|| !MaximumPhysicalTurn(scratch.candidate, scratch.candidate_arc, c, diagnostics, diagnostics.candidate_max_physical_turn))
				return reject(TerminalTailReason::kBudgetExceeded);
			if (diagnostics.candidate_max_physical_turn > diagnostics.reference_max_physical_turn + c.max_local_turn_increase)
				return reject(TerminalTailReason::kExcessiveTurn);
			std::size_t previous_distinct = 0, current_distinct = 0;
			for (std::size_t next = 1; next < n; ++next)
			{
				if (!Spend(diagnostics.validation_work, c.max_validation_work)) return reject(TerminalTailReason::kBudgetExceeded);
				if (Distance(scratch.candidate[current_distinct], scratch.candidate[next]) <= c.epsilon) continue;
				if (previous_distinct != current_distinct
					&& AdjacentOverlap(scratch.candidate[previous_distinct], scratch.candidate[current_distinct], scratch.candidate[next], c.epsilon)
					&& !AdjacentOverlap(scratch.reference_positions[previous_distinct], scratch.reference_positions[current_distinct], scratch.reference_positions[next], c.epsilon))
					return reject(TerminalTailReason::kNewIntersection);
				previous_distinct = current_distinct;
				current_distinct = next;
			}
			for (std::size_t i = 0; i + 1 < n; ++i)
				for (std::size_t j = i + 2; j + 1 < n; ++j)
				{
					if (i + 1 <= start && j + 1 <= start) continue;
					if (!Spend(diagnostics.validation_work, c.max_validation_work)) return reject(TerminalTailReason::kBudgetExceeded);
					if (Intersects(scratch.candidate[i], scratch.candidate[i + 1], scratch.candidate[j], scratch.candidate[j + 1], c.epsilon)
						&& !Intersects(scratch.reference_positions[i], scratch.reference_positions[i + 1], scratch.reference_positions[j], scratch.reference_positions[j + 1], c.epsilon))
						return reject(TerminalTailReason::kNewIntersection);
				}
			// 属性来源始终来自原 reference；raw_match 只表达几何辅助，零辅助时为默认值。
			if (m >= 2 && scratch.raw_arc.back() > c.epsilon)
			{
				double previous = 0;
				for (std::size_t i = 0; i < n; ++i)
				{
					Projection projection;
					if (!Project(scratch.candidate[i], previous, scratch.raw_arc.back(), request, scratch, diagnostics, projection, false))
						return reject(diagnostics.match_work >= c.max_match_work ? TerminalTailReason::kBudgetExceeded : TerminalTailReason::kAmbiguousMatch);
					if (projection.ambiguous) return reject(TerminalTailReason::kAmbiguousMatch);
					scratch.matches[i] = projection.match;
					previous = projection.arc;
				}
			}
			scratch.patch_points.reserve(n);
			for (std::size_t i = 0; i < n; ++i) scratch.patch_points.push_back({scratch.candidate[i], request.reference[i].source, scratch.matches[i]});
			TerminalTailOutcome result;
			result.status = TerminalTailStatus::kApply;
			result.reason = TerminalTailReason::kApplied;
			result.diagnostics = diagnostics;
			result.patch = {request.replacement_range, request.baseline_token, scratch.patch_points};
			return result;
		}

	}

	TerminalTailOutcome ReconstructTerminalTail(const TerminalTailRequest& request, TerminalTailScratch& scratch)
	{
		try { return Run(request, scratch, false); }
		catch (const std::bad_alloc&) { return Finish(TerminalTailStatus::kRejected, TerminalTailReason::kBudgetExceeded, {}, scratch); }
	}

	TerminalTailOutcome ReconstructTerminalTailControl(const TerminalTailRequest& request, TerminalTailScratch& scratch)
	{
		try { return Run(request, scratch, true); }
		catch (const std::bad_alloc&) { return Finish(TerminalTailStatus::kRejected, TerminalTailReason::kBudgetExceeded, {}, scratch); }
	}
	TerminalTailOutcome ReconstructLocalTerminalTail(const TerminalTailRequest& request, TerminalTailScratch& scratch,
		const LocalTerminalTailConfig& config)
	{
		try { return RunLocal(request, scratch, config); }
		catch (const std::bad_alloc&) { return Finish(TerminalTailStatus::kRejected, TerminalTailReason::kBudgetExceeded, {}, scratch); }
	}

}
