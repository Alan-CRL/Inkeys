#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <ink_stroke_modeler/stroke_modeler.h>
#include <ink_stroke_modeler/terminal_tail_reconstructor.h>
#include <json/json.h>

#include <algorithm>
#include <array>
#include <bit>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <numeric>
#include <sstream>
#include <string>
#include <type_traits>
#include <vector>

import draw3.ink_prediction;

namespace
{
	using namespace ink::stroke_model;
	constexpr double kPi = 3.14159265358979323846;
	constexpr double kMetricSpacing = 0.5;
	constexpr double kProbeLength = 6.0;
	using Clock = std::chrono::steady_clock;

	template<class T>
	Json::Value Number(T value)
	{
		using Integer = std::conditional_t<sizeof(T) == 4, std::uint32_t, std::uint64_t>;
		std::ostringstream bits;
		bits << std::hex << std::setfill('0') << std::setw(sizeof(T) * 2) << std::bit_cast<Integer>(value);
		Json::Value result;
		result["bits"] = bits.str();
		result["value"] = std::isfinite(value) ? Json::Value(static_cast<double>(value)) : Json::Value();
		return result;
	}

	Json::Value Position(Vec2 point)
	{
		Json::Value result;
		result["x"] = Number(point.x);
		result["y"] = Number(point.y);
		return result;
	}

	bool Exact(Vec2 a, Vec2 b)
	{
		return std::bit_cast<std::uint32_t>(a.x) == std::bit_cast<std::uint32_t>(b.x) &&
			std::bit_cast<std::uint32_t>(a.y) == std::bit_cast<std::uint32_t>(b.y);
	}

	Json::Value Source(TerminalTailSource value)
	{
		Json::Value result;
		result["first"] = Json::UInt64(value.first);
		result["second"] = Json::UInt64(value.second);
		result["fraction"] = Number(value.fraction);
		return result;
	}

	Json::Value Serialize(const Input& value)
	{
		Json::Value result;
		result["event"] = static_cast<int>(value.event_type);
		result["position"] = Position(value.position);
		result["physical_time"] = Number(value.time.Value());
		result["model_time"] = Number(value.time.Value());
		result["pressure"] = Number(value.pressure);
		result["tilt"] = Number(value.tilt);
		result["orientation"] = Number(value.orientation);
		return result;
	}

	Json::Value Serialize(const Result& value)
	{
		Json::Value result;
		result["position"] = Position(value.position);
		result["velocity"] = Position(value.velocity);
		result["acceleration"] = Position(value.acceleration);
		result["time"] = Number(value.time.Value());
		result["pressure"] = Number(value.pressure);
		result["tilt"] = Number(value.tilt);
		result["orientation"] = Number(value.orientation);
		return result;
	}

	Json::Value Parameters(const StrokeModelParams& params)
	{
		Json::Value result;
		const auto& w = params.wobble_smoother_params;
		result["wobble"]["enabled"] = w.is_enabled;
		result["wobble"]["timeout"] = Number(w.timeout.Value());
		result["wobble"]["floor"] = Number(w.speed_floor);
		result["wobble"]["ceiling"] = Number(w.speed_ceiling);
		const auto& p = params.position_modeler_params;
		result["position"]["spring_mass_constant"] = Number(p.spring_mass_constant);
		result["position"]["drag_constant"] = Number(p.drag_constant);
		const auto& l = p.loop_contraction_mitigation_params;
		result["loop"]["enabled"] = l.is_enabled;
		result["loop"]["speed_lower"] = Number(l.speed_lower_bound);
		result["loop"]["speed_upper"] = Number(l.speed_upper_bound);
		result["loop"]["strength_lower"] = Number(l.interpolation_strength_at_speed_lower_bound);
		result["loop"]["strength_upper"] = Number(l.interpolation_strength_at_speed_upper_bound);
		result["loop"]["window"] = Number(l.min_speed_sampling_window.Value());
		const auto& s = params.sampling_params;
		result["sampling"]["rate"] = Number(s.min_output_rate);
		result["sampling"]["stop_distance"] = Number(s.end_of_stroke_stopping_distance);
		result["sampling"]["end_iterations"] = s.end_of_stroke_max_iterations;
		result["sampling"]["max_outputs"] = s.max_outputs_per_call;
		result["sampling"]["max_angle"] = Number(s.max_estimated_angle_to_traverse_per_input);
		result["normal_projection"] = params.stylus_state_modeler_params.use_stroke_normal_projection;
		result["predictor_index"] = static_cast<int>(params.prediction_params.index());
		if (const auto* k = std::get_if<KalmanPredictorParams>(&params.prediction_params))
		{
			result["kalman"]["process_noise"] = Number(k->process_noise);
			result["kalman"]["measurement_noise"] = Number(k->measurement_noise);
			result["kalman"]["min_stable_iteration"] = k->min_stable_iteration;
			result["kalman"]["max_time_samples"] = k->max_time_samples;
			result["kalman"]["min_catchup_velocity"] = Number(k->min_catchup_velocity);
			result["kalman"]["acceleration_weight"] = Number(k->acceleration_weight);
			result["kalman"]["jerk_weight"] = Number(k->jerk_weight);
			result["kalman"]["prediction_interval"] = Number(k->prediction_interval.Value());
			const auto& c = k->confidence_params;
			result["kalman"]["confidence"]["samples"] = c.desired_number_of_samples;
			result["kalman"]["confidence"]["distance"] = Number(c.max_estimation_distance);
			result["kalman"]["confidence"]["min_speed"] = Number(c.min_travel_speed);
			result["kalman"]["confidence"]["max_speed"] = Number(c.max_travel_speed);
			result["kalman"]["confidence"]["deviation"] = Number(c.max_linear_deviation);
			result["kalman"]["confidence"]["baseline"] = Number(c.baseline_linearity_confidence);
		}
		return result;
	}

	Json::Value Config(const TerminalTailConfig& c)
	{
		Json::Value result;
		result["max_reference_points"] = Json::UInt64(c.max_reference_points);
		result["max_raw_points"] = Json::UInt64(c.max_raw_points);
		result["max_match_work"] = Json::UInt64(c.max_match_work);
		result["max_validation_work"] = Json::UInt64(c.max_validation_work);
		result["max_iterations"] = Json::UInt64(c.max_iterations);
		result["max_displacement"] = Number(c.max_displacement);
		result["min_length_ratio"] = Number(c.min_length_ratio);
		result["max_length_ratio"] = Number(c.max_length_ratio);
		result["raw_weight"] = Number(c.raw_weight);
		result["displacement_regularization"] = Number(c.displacement_regularization);
		result["match_band_fraction"] = Number(c.match_band_fraction);
		result["entry_match_band_fraction"] = Number(c.entry_match_band_fraction);
		result["min_entry_alignment"] = Number(c.min_entry_alignment);
		result["weak_terminal_regularization_scale"] = Number(c.weak_terminal_regularization_scale);
		result["max_local_turn_increase"] = Number(c.max_local_turn_increase);
		result["max_terminal_turn_increase"] = Number(c.max_terminal_turn_increase);
		result["physical_turn_spacing"] = Number(c.physical_turn_spacing);
		result["terminal_probe_fraction"] = Number(c.terminal_probe_fraction);
		result["min_relative_improvement"] = Number(c.min_relative_improvement);
		result["activation_angle"] = Number(c.activation_angle);
		result["max_raw_corner_angle"] = Number(c.max_raw_corner_angle);
		result["epsilon"] = Number(c.epsilon);
		return result;
	}

	struct Checks
	{
		Json::Value failures{Json::arrayValue};
		std::uint64_t count = 0;
		void Check(bool good, const std::string& message)
		{
			++count;
			if (good) return;
			failures.append(message);
			std::cerr << "Tail test FAILED: " << message << '\n';
		}
	};

	double GeometryDistance(Vec2 a, Vec2 b)
	{
		return std::hypot(static_cast<double>(a.x) - b.x, static_cast<double>(a.y) - b.y);
	}

	double Length(const std::vector<Vec2>& points)
	{
		double result = 0;
		for (size_t i = 1; i < points.size(); ++i) result += GeometryDistance(points[i - 1], points[i]);
		return result;
	}

	Vec2 AtArc(const std::vector<Vec2>& points, double distance)
	{
		for (size_t i = 1; i < points.size(); ++i)
		{
			const double step = GeometryDistance(points[i - 1], points[i]);
			if (step > 0 && distance <= step)
			{
				const double t = distance / step;
				return {static_cast<float>(points[i - 1].x + t * (points[i].x - points[i - 1].x)),
					static_cast<float>(points[i - 1].y + t * (points[i].y - points[i - 1].y))};
			}
			distance -= step;
		}
		return points.back();
	}

	std::vector<Vec2> TerminalSamples(const std::vector<Vec2>& points, double probe, double spacing)
	{
		std::vector<Vec2> result;
		if (points.empty()) return result;
		const double length = Length(points);
		const double start = std::max(0.0, length - probe);
		for (double arc = start; arc < length; arc += spacing) result.push_back(AtArc(points, arc));
		result.push_back(points.back());
		return result;
	}

	double Turn(Vec2 a, Vec2 b, Vec2 c)
	{
		const double ux = static_cast<double>(b.x) - a.x, uy = static_cast<double>(b.y) - a.y;
		const double vx = static_cast<double>(c.x) - b.x, vy = static_cast<double>(c.y) - b.y;
		if (std::hypot(ux, uy) < 1e-7 || std::hypot(vx, vy) < 1e-7) return 0;
		return std::atan2(ux * vy - uy * vx, ux * vx + uy * vy);
	}

	double SegmentDistance(Vec2 p, Vec2 a, Vec2 b)
	{
		const double dx = static_cast<double>(b.x) - a.x, dy = static_cast<double>(b.y) - a.y;
		const double denominator = dx * dx + dy * dy;
		const double t = denominator > 0 ? std::clamp(((p.x - a.x) * dx + (p.y - a.y) * dy) / denominator, 0.0, 1.0) : 0;
		return std::hypot(static_cast<double>(p.x) - a.x - t * dx, static_cast<double>(p.y) - a.y - t * dy);
	}

	double Cross(Vec2 a, Vec2 b, Vec2 c)
	{
		return (static_cast<double>(b.x) - a.x) * (c.y - a.y) - (static_cast<double>(b.y) - a.y) * (c.x - a.x);
	}

	size_t Intersections(const std::vector<Vec2>& points)
	{
		size_t result = 0;
		for (size_t i = 1; i < points.size(); ++i)
			for (size_t j = i + 2; j < points.size(); ++j)
			{
				const Vec2 a = points[i - 1], b = points[i], c = points[j - 1], d = points[j];
				if (Cross(a, b, c) * Cross(a, b, d) < -1e-12 && Cross(c, d, a) * Cross(c, d, b) < -1e-12) ++result;
			}
		return result;
	}

	size_t AdjacentOverlapCount(const std::vector<Vec2>& points, double epsilon)
	{
		std::vector<Vec2> distinct;
		for (Vec2 point : points)
			if (distinct.empty() || GeometryDistance(distinct.back(), point) > epsilon) distinct.push_back(point);
		size_t result = 0;
		for (size_t i = 2; i < distinct.size(); ++i)
		{
			const Vec2 a = distinct[i - 2], b = distinct[i - 1], c = distinct[i];
			const double length = GeometryDistance(a, b);
			const double backwardProjection = ((static_cast<double>(c.x) - b.x) * (b.x - a.x) +
				(static_cast<double>(c.y) - b.y) * (b.y - a.y)) / length;
			if (std::abs(Cross(a, b, c)) / length <= epsilon && backwardProjection < -epsilon) ++result;
		}
		return result;
	}

	struct Metrics
	{
		double length = 0, maxTurn = 0, totalTurn = 0, maxCorridor = 0, rmsCorridor = 0;
		double entryMaxTurn = 0, entryDiscreteTurn = 0, fullMaxTurn = 0, fullAbsoluteTurn = 0;
		size_t intersections = 0, adjacentOverlaps = 0;
	};

	Metrics MeasureGeometry(const std::vector<Vec2>& points, const std::vector<Vec2>& raw, double probe, double spacing = kMetricSpacing)
	{
		Metrics result;
		result.length = Length(points);
		result.intersections = Intersections(points);
		result.adjacentOverlaps = AdjacentOverlapCount(points, spacing * 2e-6);
		const auto samples = TerminalSamples(points, probe, spacing);
		for (size_t i = 2; i < samples.size(); ++i)
		{
			const double turn = Turn(samples[i - 2], samples[i - 1], samples[i]);
			result.maxTurn = std::max(result.maxTurn, std::abs(turn));
			result.totalTurn += turn;
		}
		const auto fullSamples = TerminalSamples(points, result.length, spacing);
		for (size_t i = 2; i < fullSamples.size(); ++i)
		{
			const double turn = std::abs(Turn(fullSamples[i - 2], fullSamples[i - 1], fullSamples[i]));
			result.fullMaxTurn = std::max(result.fullMaxTurn, turn);
			result.fullAbsoluteTurn += turn;
			if (static_cast<double>(i) * spacing <= probe + spacing) result.entryMaxTurn = std::max(result.entryMaxTurn, turn);
		}
		if (points.size() >= 3) result.entryDiscreteTurn = std::abs(Turn(points[0], points[1], points[2]));
		if (raw.size() < 2) return result;
		// 无序最近距离仅为额外诊断；不能证明自交时的分支身份，另检 raw_match 顺序。
		for (Vec2 point : samples)
		{
			double best = std::numeric_limits<double>::infinity();
			for (size_t i = 1; i < raw.size(); ++i) best = std::min(best, SegmentDistance(point, raw[i - 1], raw[i]));
			result.maxCorridor = std::max(result.maxCorridor, best);
			result.rmsCorridor += best * best;
		}
		if (!samples.empty()) result.rmsCorridor = std::sqrt(result.rmsCorridor / samples.size());
		return result;
	}

	Json::Value Serialize(const Metrics& value)
	{
		Json::Value result;
		result["length"] = value.length;
		result["terminal_max_turn_radians"] = value.maxTurn;
		result["terminal_signed_turn_radians"] = value.totalTurn;
		result["unordered_corridor_max_distance"] = value.maxCorridor;
		result["unordered_corridor_rms_distance"] = value.rmsCorridor;
		result["proper_self_intersections"] = Json::UInt64(value.intersections);
		result["adjacent_overlap_count"] = Json::UInt64(value.adjacentOverlaps);
		result["entry_max_physical_turn_radians"] = value.entryMaxTurn;
		result["entry_third_point_discrete_turn_radians"] = value.entryDiscreteTurn;
		result["full_max_physical_turn_radians"] = value.fullMaxTurn;
		result["full_absolute_physical_turn_radians"] = value.fullAbsoluteTurn;
		return result;
	}

	std::vector<Input> MakeInputs(const std::string& shape)
	{
		const bool nonuniform = shape == "arc_nonuniform";
		const double unitScale = shape == "arc_scale2" ? 2.0 : 1.0;
		const std::string geometry = nonuniform || unitScale != 1 ? "arc" : shape == "duplicate_corner" ? "real_corner" : shape;
		const size_t count = shape == "single" ? 1 : shape == "sparse" ? 12 : 72;
		const double dt = shape == "sparse" ? 1.0 / 24.0 : 1.0 / 120.0;
		std::vector<Input> inputs;
		for (size_t i = 0; i < count; ++i)
		{
			const double uniformT = static_cast<double>(i) / std::max(size_t{1}, count - 1);
			const double t = nonuniform ? std::pow(uniformT, 1.35) : uniformT;
			double x = 10 + 220 * t, y = 50;
			if (geometry == "circle" || geometry == "arc" || geometry == "quick_curve" || geometry == "sparse")
			{
				const double radius = geometry == "quick_curve" ? 12 : 45;
				const double angle = geometry == "circle" ? 2 * kPi * t : geometry == "quick_curve" ? 10 * t : 3.5 * t;
				x = 100 + radius * std::cos(angle); y = 100 + radius * std::sin(angle);
			}
			if (shape == "s_bend") y = 50 + 25 * std::sin(3 * kPi * t);
			if (shape == "reverse") { x = 10 + 200 * (t < 0.6 ? t : 1.2 - t); y = 50 + 2 * t; }
			if (shape == "loop" || shape == "self_cross")
			{
				const double angle = (shape == "loop" ? 2.5 : 4) * kPi * t;
				x = 100 + 30 * std::sin(angle); y = 100 + 25 * std::sin(2 * angle);
			}
			if (geometry == "real_corner")
			{
				x = i < count - 6 ? 10 + 3 * i : 10 + 3 * (count - 7);
				y = i < count - 6 ? 50 : 50 + 4 * (i - (count - 7));
				if (shape == "duplicate_corner" && i == count - 6) y = 50;
			}
			if (shape == "repeated") { x = 40; y = 50; }
			if (shape == "stationary" && i > count - 24) { x = 10 + 220.0 * (count - 24) / (count - 1); y = 50; }
			Input input{.event_type = i == 0 ? Input::EventType::kDown : Input::EventType::kMove,
				.position = {static_cast<float>(x * unitScale), static_cast<float>(y * unitScale)}, .time = Time(t * (count - 1) * dt),
				.pressure = 0.2f + 0.05f * static_cast<float>(i % 12),
				.tilt = 0.05f * static_cast<float>(i % 20), .orientation = 0.1f * static_cast<float>(i % 60)};
			inputs.push_back(input);
		}
		auto up = inputs.back();
		up.event_type = Input::EventType::kUp;
		// quick_curve 使用最后 Move 同时刻的真实 Up，专门覆盖立即收尾。
		if (shape != "quick_curve" && shape != "single") up.time += Duration(dt * 0.2);
		inputs.push_back(up);
		return inputs;
	}

	struct Fixture
	{
		std::string name;
		StrokeModelParams params;
		std::vector<Input> inputs;
		std::vector<Result> outputs;
		std::vector<int> statuses;
		std::vector<TerminalTailReferencePoint> accepted;
		std::vector<double> acceptedTimes;
		std::vector<TerminalTailRawPoint> raw;
		size_t terminalStart = 0, gateAccepted = 0;
		double unitScale = 1;
	};

	Fixture MakeFixture(Checks& checks, const std::string& shape, bool kalman)
	{
		Fixture fixture;
		fixture.name = shape + (kalman ? "/kalman" : "/stroke_end");
		fixture.unitScale = shape == "arc_scale2" ? 2 : 1;
		const auto configuration = draw3::CreateStrokeModelConfiguration(static_cast<int>(96 * fixture.unitScale));
		fixture.params = configuration.modelParams;
		fixture.params.prediction_params = kalman ? PredictionParams(configuration.kalmanPredictorParams) : PredictionParams(StrokeEndPredictorParams{});
		fixture.inputs = MakeInputs(shape);
		StrokeModeler model;
		checks.Check(model.Reset(fixture.params).ok(), fixture.name + " reset actual Modeler");
		std::vector<Result> output;
		for (size_t i = 0; i < fixture.inputs.size(); ++i)
		{
			const auto& input = fixture.inputs[i];
			fixture.raw.push_back({input.position, 100 + i});
			output.clear();
			const auto status = model.Update(input, output);
			checks.Check(status.ok(), fixture.name + " actual Update");
			fixture.statuses.push_back(static_cast<int>(status.code()));
			fixture.outputs.insert(fixture.outputs.end(), output.begin(), output.end());
			if (input.event_type == Input::EventType::kUp)
			{
				fixture.terminalStart = fixture.accepted.size();
				double previous = fixture.accepted.empty() ? std::numeric_limits<double>::infinity() : GeometryDistance(fixture.accepted.back().position, input.position);
				// 明确的离线接纳替身：到 Up 距离不再下降即停，然后 pin；不伪称产品 helper。
				for (const auto& result : output)
				{
					const double distance = GeometryDistance(result.position, input.position);
					if (!result.position.IsFinite() || distance <= 1e-6 || distance + 1e-6 >= previous) break;
					previous = distance;
					fixture.accepted.push_back({result.position, {}, false});
					fixture.acceptedTimes.push_back(std::min(result.time.Value(), input.time.Value()));
					++fixture.gateAccepted;
				}
				fixture.accepted.push_back({input.position, {}, true});
				fixture.acceptedTimes.push_back(input.time.Value());
			}
			else
				for (const auto& result : output)
				{
					fixture.accepted.push_back({result.position, {}, false});
					fixture.acceptedTimes.push_back(result.time.Value());
				}
		}
		for (size_t i = 0; i < fixture.accepted.size(); ++i)
			fixture.accepted[i].source = {1000 + i, 1000 + i, 0};
		return fixture;
	}

	struct Window
	{
		std::vector<TerminalTailReferencePoint> reference;
		std::vector<TerminalTailRawPoint> raw;
		size_t begin = 0, rawBegin = 0;
		double duration = 0;
		double unitScale = 1;
		TerminalTailConfig config;

		TerminalTailRequest Request() const
		{
			TerminalTailRequest request;
			request.reference = reference;
			request.raw = raw;
			request.endpoint = raw.back().position;
			request.replacement_range = {reference.front().source.first, reference.back().source.second + 1};
			request.baseline_token = 17;
			request.config = config;
			return request;
		}
	};

	Window MakeWindow(const Fixture& fixture, double duration)
	{
		Window window;
		window.duration = duration;
		window.unitScale = fixture.unitScale;
		window.config.max_displacement *= fixture.unitScale;
		window.config.epsilon *= fixture.unitScale;
		window.config.physical_turn_spacing *= fixture.unitScale;
		const double cutoff = fixture.inputs.back().time.Value() - duration;
		while (window.begin + 2 < fixture.accepted.size() && fixture.acceptedTimes[window.begin + 1] < cutoff) ++window.begin;
		window.reference.assign(fixture.accepted.begin() + window.begin, fixture.accepted.end());
		const double rawCutoff = fixture.acceptedTimes[window.begin] - 0.05;
		while (window.rawBegin + 2 < fixture.raw.size() && fixture.inputs[window.rawBegin + 1].time.Value() < rawCutoff) ++window.rawBegin;
		window.raw.assign(fixture.raw.begin() + window.rawBegin, fixture.raw.end());
		return window;
	}

	Json::Value Serialize(const TerminalTailRequest& request)
	{
		Json::Value result;
		result["reference"] = Json::Value(Json::arrayValue);
		for (const auto& reference : request.reference)
		{
			Json::Value value;
			value["position"] = Position(reference.position);
			value["caller_source"] = Source(reference.source);
			value["weak_terminal_pin"] = reference.weak_terminal;
			result["reference"].append(value);
		}
		result["raw"] = Json::Value(Json::arrayValue);
		for (const auto& raw : request.raw)
		{
			Json::Value value;
			value["position"] = Position(raw.position);
			value["consumed_source"] = Json::UInt64(raw.source);
			result["raw"].append(value);
		}
		result["endpoint"] = Position(request.endpoint);
		result["range_begin"] = Json::UInt64(request.replacement_range.begin);
		result["range_end"] = Json::UInt64(request.replacement_range.end);
		result["baseline_token"] = Json::UInt64(request.baseline_token);
		result["config"] = Config(request.config);
		return result;
	}

	std::vector<Vec2> Positions(const std::vector<TerminalTailReferencePoint>& points)
	{
		std::vector<Vec2> result;
		for (const auto& point : points) result.push_back(point.position);
		return result;
	}

	std::vector<Vec2> RawPositions(const Window& window)
	{
		std::vector<Vec2> result;
		for (const auto& point : window.raw) result.push_back(point.position);
		return result;
	}

	const char* Reason(TerminalTailReason reason)
	{
		switch (reason)
		{
		case TerminalTailReason::kApplied: return "applied";
		case TerminalTailReason::kUnchanged: return "unchanged";
		case TerminalTailReason::kNoImprovement: return "no_improvement";
		case TerminalTailReason::kInvalidInput: return "invalid_input";
		case TerminalTailReason::kBudgetExceeded: return "budget_exceeded";
		case TerminalTailReason::kInsufficientWindow: return "insufficient_window";
		case TerminalTailReason::kAmbiguousMatch: return "ambiguous_match";
		case TerminalTailReason::kSharpCorner: return "sharp_corner";
		case TerminalTailReason::kExcessiveDisplacement: return "excessive_displacement";
		case TerminalTailReason::kLengthChange: return "length_change";
		case TerminalTailReason::kNewIntersection: return "new_intersection";
		case TerminalTailReason::kExcessiveTurn: return "excessive_turn";
		}
		return "unknown";
	}

	struct Evaluation
	{
		Json::Value report;
		Metrics metrics;
		TerminalTailStatus status = TerminalTailStatus::kRejected;
		bool hardChecksPassed = true;
		std::vector<Vec2> points;
	};

	Evaluation Evaluate(Checks& checks, const Window& window, bool control, const std::string& fixtureName)
	{
		Evaluation evaluation;
		std::ostringstream label;
		label << std::setprecision(17) << fixtureName << " window=" << window.duration
			<< " raw_weight=" << window.config.raw_weight << " lambda=" << window.config.displacement_regularization
			<< " unit_scale=" << window.unitScale << (control ? " control" : " candidate");
		const std::string context = label.str();
		const auto verify = [&](bool good, const std::string& message)
		{
			evaluation.hardChecksPassed &= good;
			checks.Check(good, context + ": " + message);
		};
		evaluation.report["verification_context"] = context;
		TerminalTailScratch scratch;
		const auto request = window.Request();
		const auto before = Serialize(request);
		const auto outcome = control ? ReconstructTerminalTailControl(request, scratch) : ReconstructTerminalTail(request, scratch);
		verify(before == Serialize(request), "solver cannot mutate raw/reference/config");
		evaluation.status = outcome.status;
		evaluation.points = Positions(window.reference);
		evaluation.report["status"] = static_cast<int>(outcome.status);
		evaluation.report["reason"] = Reason(outcome.reason);
		evaluation.report["patch"] = Json::Value(Json::arrayValue);
		if (outcome.status == TerminalTailStatus::kApply)
		{
			verify(outcome.patch.points.size() == request.reference.size(), "Apply point count");
			verify(outcome.patch.replacement_range.begin == request.replacement_range.begin &&
				outcome.patch.replacement_range.end == request.replacement_range.end && outcome.patch.baseline_token == request.baseline_token, "Apply identity/range");
			verify(outcome.patch.points.size() >= 3, "Apply nonempty tail");
			if (outcome.patch.points.size() >= 3)
			{
				verify(Exact(outcome.patch.points[0].position, request.reference[0].position) && Exact(outcome.patch.points[1].position, request.reference[1].position), "fixed entry and incoming segment");
				verify(Exact(outcome.patch.points.back().position, request.endpoint), "exact physical Up");
			}
			evaluation.points.clear();
			double previousRaw = -1, maxDisplacement = 0;
			for (size_t i = 0; i < outcome.patch.points.size(); ++i)
			{
				const auto& point = outcome.patch.points[i];
				verify(point.position.IsFinite(), "finite patch position");
				verify(point.source.first >= request.replacement_range.begin && point.source.second < request.replacement_range.end &&
					point.source.first <= point.source.second && std::isfinite(point.source.fraction) && point.source.fraction >= 0 && point.source.fraction <= 1, "caller source in range");
				if (i < request.reference.size())
				{
					const auto expected = request.reference[i].source;
					verify(point.source.first == expected.first && point.source.second == expected.second &&
						std::bit_cast<std::uint64_t>(point.source.fraction) == std::bit_cast<std::uint64_t>(expected.fraction), "caller source metadata retained exactly");
				}
				if (i > 0)
				{
					const auto previous = outcome.patch.points[i - 1].source;
					const bool sameInterval = previous.first == point.source.first && previous.second == point.source.second;
					verify(sameInterval ? previous.fraction <= point.source.fraction : previous.second <= point.source.first, "integer caller source order without double conversion");
				}
				verify(point.raw_match.segment + 1 < request.raw.size() && std::isfinite(point.raw_match.fraction) &&
					point.raw_match.fraction >= 0 && point.raw_match.fraction <= 1, "raw match in range");
				const double raw = point.raw_match.segment + point.raw_match.fraction;
				verify(raw + 1e-12 >= previousRaw, "raw branch mapping nondecreasing");
				previousRaw = raw;
				if (i < request.reference.size()) maxDisplacement = std::max(maxDisplacement, GeometryDistance(point.position, request.reference[i].position));
				evaluation.points.push_back(point.position);
				Json::Value record;
				record["position"] = Position(point.position);
				record["source"] = Source(point.source);
				record["raw_segment"] = Json::UInt64(point.raw_match.segment);
				record["raw_fraction"] = Number(point.raw_match.fraction);
				evaluation.report["patch"].append(record);
			}
			const double beforeLength = Length(Positions(window.reference)), afterLength = Length(evaluation.points);
			verify(maxDisplacement <= request.config.max_displacement + 1e-5, "independent displacement bound");
			verify(afterLength + 1e-5 >= beforeLength * request.config.min_length_ratio &&
				afterLength <= beforeLength * request.config.max_length_ratio + 1e-5, "independent length bounds");
			verify(Intersections(evaluation.points) <= Intersections(Positions(window.reference)), "no additional proper self-intersections");
			verify(AdjacentOverlapCount(evaluation.points, request.config.epsilon) <= AdjacentOverlapCount(Positions(window.reference), request.config.epsilon), "no additional adjacent collinear overlap");
			evaluation.report["independent_max_displacement"] = maxDisplacement;
		}
		else verify(outcome.patch.points.empty() && scratch.patch_points.empty(), "non-Apply publishes no partial output");
		verify(outcome.diagnostics.match_work <= request.config.max_match_work &&
			outcome.diagnostics.validation_work <= request.config.max_validation_work && outcome.diagnostics.iterations <= request.config.max_iterations, "bounded solver work");
		auto& diagnostics = evaluation.report["library_diagnostics"];
		diagnostics["match_work"] = Json::UInt64(outcome.diagnostics.match_work);
		diagnostics["validation_work"] = Json::UInt64(outcome.diagnostics.validation_work);
		diagnostics["iterations"] = Json::UInt64(outcome.diagnostics.iterations);
		diagnostics["reference_terminal_turn"] = outcome.diagnostics.reference_terminal_turn;
		diagnostics["candidate_terminal_turn"] = outcome.diagnostics.candidate_terminal_turn;
		diagnostics["reference_max_physical_turn"] = outcome.diagnostics.reference_max_physical_turn;
		diagnostics["candidate_max_physical_turn"] = outcome.diagnostics.candidate_max_physical_turn;
		diagnostics["reference_corridor_rms"] = outcome.diagnostics.reference_corridor_rms;
		diagnostics["candidate_corridor_rms"] = outcome.diagnostics.candidate_corridor_rms;
		evaluation.metrics = MeasureGeometry(evaluation.points, RawPositions(window), std::min(kProbeLength * window.unitScale, Length(Positions(window.reference))), kMetricSpacing * window.unitScale);
		evaluation.report["physical_metrics"] = Serialize(evaluation.metrics);
		if (outcome.status == TerminalTailStatus::kApply)
		{
			const auto baselineMetrics = MeasureGeometry(Positions(window.reference), RawPositions(window), std::min(kProbeLength * window.unitScale, Length(Positions(window.reference))), kMetricSpacing * window.unitScale);
			verify(evaluation.metrics.entryMaxTurn <= baselineMetrics.entryMaxTurn + request.config.max_local_turn_increase + 1e-5, "independent entry physical turn bound");
			verify(evaluation.metrics.fullMaxTurn <= baselineMetrics.fullMaxTurn + request.config.max_local_turn_increase + 1e-5, "independent full-window physical turn bound");
			// 大来源编号不能转 double 检查；同一几何改为 >2^53 的相邻整数应产生相同补丁。
			auto largeReference = window.reference;
			constexpr std::uint64_t offset = std::uint64_t{1} << 54;
			for (auto& reference : largeReference) { reference.source.first += offset; reference.source.second += offset; }
			auto largeRequest = request;
			largeRequest.reference = largeReference;
			largeRequest.replacement_range.begin += offset;
			largeRequest.replacement_range.end += offset;
			TerminalTailScratch largeScratch;
			const auto large = control ? ReconstructTerminalTailControl(largeRequest, largeScratch) : ReconstructTerminalTail(largeRequest, largeScratch);
			verify(large.status == TerminalTailStatus::kApply && large.patch.points.size() == evaluation.points.size(), "large source IDs preserve successful geometry");
			for (size_t i = 0; i < std::min(large.patch.points.size(), evaluation.points.size()); ++i)
				verify(Exact(large.patch.points[i].position, evaluation.points[i]) && large.patch.points[i].source.first == largeReference[i].source.first &&
					large.patch.points[i].source.second == largeReference[i].source.second, "large source positions and identities exact");
			// 先复制成功补丁再复用 scratch；失败后旧 span 不得继续消费。
			auto invalid = request;
			invalid.config.raw_weight = std::numeric_limits<double>::quiet_NaN();
			const auto rejected = control ? ReconstructTerminalTailControl(invalid, scratch) : ReconstructTerminalTail(invalid, scratch);
			verify(rejected.status == TerminalTailStatus::kRejected && rejected.patch.points.empty() && scratch.patch_points.empty(), "success then rejected scratch clears patch");
			verify(before == Serialize(request), "scratch reuse cannot mutate valid inputs");
		}
		evaluation.report["all_hard_checks_passed"] = evaluation.hardChecksPassed;
		return evaluation;
	}

	Evaluation EvaluateLocal(Checks& checks, const Window& window, bool noAux, const std::string& fixtureName)
	{
		Evaluation evaluation;
		std::ostringstream label;
		label << std::setprecision(17) << fixtureName << " window=" << window.duration
			<< " raw_weight=" << window.config.raw_weight << " lambda=" << window.config.displacement_regularization
			<< " unit_scale=" << window.unitScale << (noAux ? " local_zero_aux" : " local_raw_assisted");
		const std::string context = label.str();
		const auto verify = [&](bool good, const std::string& message)
		{
			evaluation.hardChecksPassed &= good;
			checks.Check(good, context + ": " + message);
		};
		evaluation.report["verification_context"] = context;
		TerminalTailScratch scratch;
		auto request = window.Request();
		if (noAux) request.raw = {};
		LocalTerminalTailConfig localConfig;
		localConfig.min_repair_length *= window.unitScale;
		localConfig.model_direction_probe *= window.unitScale;
		evaluation.report["request"] = Serialize(request);
		evaluation.report["local_config"]["min_repair_length"] = localConfig.min_repair_length;
		evaluation.report["local_config"]["max_repair_fraction"] = localConfig.max_repair_fraction;
		evaluation.report["local_config"]["pin_length_multiplier"] = localConfig.pin_length_multiplier;
		evaluation.report["local_config"]["model_direction_probe"] = localConfig.model_direction_probe;
		evaluation.report["local_config"]["raw_position_weight"] = localConfig.raw_position_weight;
		evaluation.report["local_config"]["max_assist_points"] = Json::UInt64(localConfig.max_assist_points);
		evaluation.report["local_config"]["activation_angle"] = localConfig.activation_angle;
		evaluation.report["local_config"]["max_terminal_angle"] = localConfig.max_terminal_angle;
		const auto before = Serialize(request);
		const auto outcome = ReconstructLocalTerminalTail(request, scratch, localConfig);
		verify(before == Serialize(request), "solver cannot mutate raw/reference/config");
		evaluation.status = outcome.status;
		evaluation.points = Positions(window.reference);
		evaluation.report["status"] = static_cast<int>(outcome.status);
		evaluation.report["reason"] = Reason(outcome.reason);
		evaluation.report["patch"] = Json::Value(Json::arrayValue);
		if (outcome.status == TerminalTailStatus::kApply)
		{
			verify(outcome.patch.points.size() == request.reference.size(), "Apply point count");
			verify(outcome.patch.replacement_range.begin == request.replacement_range.begin &&
				outcome.patch.replacement_range.end == request.replacement_range.end && outcome.patch.baseline_token == request.baseline_token, "Apply identity/range");
			verify(outcome.patch.points.size() >= 3, "Apply nonempty tail");
			if (outcome.patch.points.size() >= 3)
			{
				verify(Exact(outcome.patch.points[0].position, request.reference[0].position) && Exact(outcome.patch.points[1].position, request.reference[1].position), "fixed entry and incoming segment");
				verify(Exact(outcome.patch.points.back().position, request.endpoint), "exact physical Up");
			}
			evaluation.points.clear();
			double previousRaw = -1, maxDisplacement = 0;
			for (size_t i = 0; i < outcome.patch.points.size(); ++i)
			{
				const auto& point = outcome.patch.points[i];
				verify(point.position.IsFinite(), "finite patch position");
				verify(point.source.first >= request.replacement_range.begin && point.source.second < request.replacement_range.end &&
					point.source.first <= point.source.second && std::isfinite(point.source.fraction) && point.source.fraction >= 0 && point.source.fraction <= 1, "caller source in range");
				if (i < request.reference.size())
				{
					const auto expected = request.reference[i].source;
					verify(point.source.first == expected.first && point.source.second == expected.second &&
						std::bit_cast<std::uint64_t>(point.source.fraction) == std::bit_cast<std::uint64_t>(expected.fraction), "caller source metadata retained exactly");
				}
				if (i > 0)
				{
					const auto previous = outcome.patch.points[i - 1].source;
					const bool sameInterval = previous.first == point.source.first && previous.second == point.source.second;
					verify(sameInterval ? previous.fraction <= point.source.fraction : previous.second <= point.source.first, "integer caller source order without double conversion");
				}
				if (request.raw.size() >= 2)
				{
					verify(point.raw_match.segment + 1 < request.raw.size() && std::isfinite(point.raw_match.fraction) &&
						point.raw_match.fraction >= 0 && point.raw_match.fraction <= 1, "raw match in range");
					const double raw = point.raw_match.segment + point.raw_match.fraction;
					verify(raw + 1e-12 >= previousRaw, "raw branch mapping nondecreasing");
					previousRaw = raw;
				}
				else verify(point.raw_match.segment == 0 && point.raw_match.fraction == 0, "no fabricated raw match");
				if (i < request.reference.size()) maxDisplacement = std::max(maxDisplacement, GeometryDistance(point.position, request.reference[i].position));
				evaluation.points.push_back(point.position);
				Json::Value record;
				record["position"] = Position(point.position);
				record["source"] = Source(point.source);
				record["raw_segment"] = Json::UInt64(point.raw_match.segment);
				record["raw_fraction"] = Number(point.raw_match.fraction);
				evaluation.report["patch"].append(record);
			}
			const double beforeLength = Length(Positions(window.reference)), afterLength = Length(evaluation.points);
			verify(maxDisplacement <= request.config.max_displacement + 1e-5, "independent displacement bound");
			verify(afterLength + 1e-5 >= beforeLength * request.config.min_length_ratio &&
				afterLength <= beforeLength * request.config.max_length_ratio + 1e-5, "independent length bounds");
			verify(Intersections(evaluation.points) <= Intersections(Positions(window.reference)), "no additional proper self-intersections");
			verify(AdjacentOverlapCount(evaluation.points, request.config.epsilon) <= AdjacentOverlapCount(Positions(window.reference), request.config.epsilon), "no additional adjacent collinear overlap");
			evaluation.report["independent_max_displacement"] = maxDisplacement;
		}
		else verify(outcome.patch.points.empty() && scratch.patch_points.empty(), "non-Apply publishes no partial output");
		verify(outcome.diagnostics.match_work <= request.config.max_match_work &&
			outcome.diagnostics.validation_work <= request.config.max_validation_work && outcome.diagnostics.iterations <= request.config.max_iterations, "bounded solver work");
		auto& diagnostics = evaluation.report["library_diagnostics"];
		diagnostics["match_work"] = Json::UInt64(outcome.diagnostics.match_work);
		diagnostics["validation_work"] = Json::UInt64(outcome.diagnostics.validation_work);
		diagnostics["iterations"] = Json::UInt64(outcome.diagnostics.iterations);
		diagnostics["reference_terminal_turn"] = outcome.diagnostics.reference_terminal_turn;
		diagnostics["candidate_terminal_turn"] = outcome.diagnostics.candidate_terminal_turn;
		diagnostics["reference_max_physical_turn"] = outcome.diagnostics.reference_max_physical_turn;
		diagnostics["candidate_max_physical_turn"] = outcome.diagnostics.candidate_max_physical_turn;
		diagnostics["reference_corridor_rms"] = outcome.diagnostics.reference_corridor_rms;
		diagnostics["candidate_corridor_rms"] = outcome.diagnostics.candidate_corridor_rms;
		evaluation.metrics = MeasureGeometry(evaluation.points, RawPositions(window), std::min(kProbeLength * window.unitScale, Length(Positions(window.reference))), kMetricSpacing * window.unitScale);
		evaluation.report["physical_metrics"] = Serialize(evaluation.metrics);
		if (outcome.status == TerminalTailStatus::kApply)
		{
			const auto baselineMetrics = MeasureGeometry(Positions(window.reference), RawPositions(window), std::min(kProbeLength * window.unitScale, Length(Positions(window.reference))), kMetricSpacing * window.unitScale);
			verify(evaluation.metrics.entryMaxTurn <= baselineMetrics.entryMaxTurn + request.config.max_local_turn_increase + 1e-5, "independent entry physical turn bound");
			verify(evaluation.metrics.fullMaxTurn <= baselineMetrics.fullMaxTurn + request.config.max_local_turn_increase + 1e-5, "independent full-window physical turn bound");
			// 大来源编号不能转 double 检查；同一几何改为 >2^53 的相邻整数应产生相同补丁。
			auto largeReference = window.reference;
			constexpr std::uint64_t offset = std::uint64_t{1} << 54;
			for (auto& reference : largeReference) { reference.source.first += offset; reference.source.second += offset; }
			auto largeRequest = request;
			largeRequest.reference = largeReference;
			largeRequest.replacement_range.begin += offset;
			largeRequest.replacement_range.end += offset;
			TerminalTailScratch largeScratch;
			const auto large = ReconstructLocalTerminalTail(largeRequest, largeScratch, localConfig);
			verify(large.status == TerminalTailStatus::kApply && large.patch.points.size() == evaluation.points.size(), "large source IDs preserve successful geometry");
			for (size_t i = 0; i < std::min(large.patch.points.size(), evaluation.points.size()); ++i)
				verify(Exact(large.patch.points[i].position, evaluation.points[i]) && large.patch.points[i].source.first == largeReference[i].source.first &&
					large.patch.points[i].source.second == largeReference[i].source.second, "large source positions and identities exact");
			// 先复制成功补丁再复用 scratch；失败后旧 span 不得继续消费。
			auto invalid = request;
			invalid.config.raw_weight = std::numeric_limits<double>::quiet_NaN();
			const auto rejected = ReconstructLocalTerminalTail(invalid, scratch, localConfig);
			verify(rejected.status == TerminalTailStatus::kRejected && rejected.patch.points.empty() && scratch.patch_points.empty(), "success then rejected scratch clears patch");
			verify(before == Serialize(request), "scratch reuse cannot mutate valid inputs");
		}
		const auto baselinePositions = Positions(window.reference);
		const auto lastAngle = [](const std::vector<Vec2>& points)
		{
			return points.size() < 3 ? 0.0 : std::abs(Turn(points[points.size() - 3], points[points.size() - 2], points.back())) * 180 / kPi;
		};
		size_t fixedPrefix = 0;
		while (fixedPrefix < evaluation.points.size() && Exact(evaluation.points[fixedPrefix], baselinePositions[fixedPrefix])) ++fixedPrefix;
		evaluation.report["preserved_prefix_points"] = Json::UInt64(fixedPrefix);
		evaluation.report["before_last_angle_degrees"] = lastAngle(baselinePositions);
		evaluation.report["after_last_angle_degrees"] = lastAngle(evaluation.points);
		evaluation.report["positions"] = Json::Value(Json::arrayValue);
		for (Vec2 point : evaluation.points) evaluation.report["positions"].append(Position(point));
		if (outcome.status == TerminalTailStatus::kApply)
		{
			verify(fixedPrefix >= 2, "normal prefix exact");
			verify(lastAngle(evaluation.points) <= localConfig.max_terminal_angle * 180 / kPi + 1e-4, "actual final segment kink removed");
		}
		evaluation.report["all_hard_checks_passed"] = evaluation.hardChecksPassed;
		return evaluation;
	}

	void LocalEdgeCases(Checks& checks, Json::Value& report, const Window& valid)
	{
		for (const char* label : {"nan_reference", "nan_endpoint", "nan_raw", "bad_range", "unordered_source", "short_reference",
			"point_budget", "raw_budget", "match_budget", "validation_budget", "nan_config", "nan_local_config", "zero_probe", "over_fraction", "extreme_finite"})
		{
			auto reference = valid.reference;
			auto raw = valid.raw;
			auto request = valid.Request();
			LocalTerminalTailConfig local;
			const std::string name(label);
			if (name == "nan_reference") reference[2].position.x = std::numeric_limits<float>::quiet_NaN();
			if (name == "nan_endpoint") request.endpoint.x = std::numeric_limits<float>::quiet_NaN();
			if (name == "nan_raw") raw[1].position.x = std::numeric_limits<float>::quiet_NaN();
			if (name == "bad_range") request.replacement_range.end = request.replacement_range.begin;
			if (name == "unordered_source") reference[2].source = reference[0].source;
			if (name == "short_reference") reference.resize(2);
			if (name == "point_budget") request.config.max_reference_points = reference.size() - 1;
			if (name == "raw_budget") request.config.max_raw_points = raw.size() - 1;
			if (name == "match_budget") request.config.max_match_work = 1;
			if (name == "validation_budget") request.config.max_validation_work = 1;
			if (name == "nan_config") request.config.epsilon = std::numeric_limits<double>::quiet_NaN();
			if (name == "nan_local_config") local.raw_position_weight = std::numeric_limits<double>::quiet_NaN();
			if (name == "zero_probe") local.model_direction_probe = 0;
			if (name == "over_fraction") local.max_repair_fraction = 1.1;
			if (name == "extreme_finite")
			{
				// 极大但有限的坐标应有界拒绝，不能发布无效窄化位置。
				for (auto& p : reference) { p.position.x *= 1e35f; p.position.y *= 1e35f; }
				request.endpoint = reference.back().position;
				raw.clear();
			}
			request.reference = reference; request.raw = raw;
			const auto before = Serialize(request);
			TerminalTailScratch scratch;
			scratch.patch_points.push_back({});
			const auto outcome = ReconstructLocalTerminalTail(request, scratch, local);
			checks.Check(outcome.status == TerminalTailStatus::kRejected && outcome.patch.points.empty() && scratch.patch_points.empty(), "local atomic reject " + name);
			checks.Check(before == Serialize(request), "local invalid cannot mutate " + name);
			checks.Check(outcome.diagnostics.match_work <= request.config.max_match_work && outcome.diagnostics.validation_work <= request.config.max_validation_work,
				"local failed work bounded " + name);
			Json::Value row; row["name"] = name; row["reason"] = Reason(outcome.reason);
			report.append(row);
		}
		// 可选原始点不需要伪造 Up；单点辅助不能形成方向或匹配分支。
		auto request = valid.Request();
		request.raw = request.raw.first(1);
		TerminalTailScratch scratch;
		const auto result = ReconstructLocalTerminalTail(request, scratch);
		checks.Check(result.reason != TerminalTailReason::kInvalidInput && result.reason != TerminalTailReason::kInsufficientWindow,
			"local one raw point not prerequisite failure");
	}

	Json::Value FixtureMetadata(const Fixture& fixture)
	{
		Json::Value result;
		result["name"] = fixture.name;
		result["coordinate_units_per_fixture_pixel"] = fixture.unitScale;
		result["params"] = Parameters(fixture.params);
		result["raw_inputs"] = Json::Value(Json::arrayValue);
		for (const auto& input : fixture.inputs) result["raw_inputs"].append(Serialize(input));
		result["all_original_model_results"] = Json::Value(Json::arrayValue);
		for (const auto& output : fixture.outputs) result["all_original_model_results"].append(Serialize(output));
		result["update_status_codes"] = Json::Value(Json::arrayValue);
		for (int status : fixture.statuses) result["update_status_codes"].append(status);
		result["terminal_first_accepted_index"] = Json::UInt64(fixture.terminalStart);
		result["surrogate_accepted_up_results"] = Json::UInt64(fixture.gateAccepted);
		result["accepted_times"] = Json::Value(Json::arrayValue);
		for (double time : fixture.acceptedTimes) result["accepted_times"].append(Number(time));
		return result;
	}

	void EdgeCases(Checks& checks, Json::Value& report, const Window& valid)
	{
		const std::array<const char*, 26> labels{"nonfinite_reference", "nonfinite_raw", "nonfinite_endpoint", "bad_range", "unordered_source",
			"unordered_raw", "short_window", "degenerate_entry", "point_budget", "match_budget", "invalid_config", "nonfinite_weight",
			"negative_threshold", "invalid_fraction", "range_overflow", "huge_raw", "raw_budget", "validation_budget", "huge_raw_count", "empty_raw", "empty_reference", "large_source_reversed", "nonfinite_spacing", "zero_spacing", "tiny_spacing_budget", "huge_path_budget"};
		for (const char* label : labels)
		{
			Window window = valid;
			const std::string name(label);
			if (name == "nonfinite_reference") window.reference[2].position.x = std::numeric_limits<float>::quiet_NaN();
			if (name == "nonfinite_raw") window.raw[2].position.y = std::numeric_limits<float>::infinity();
			if (name == "unordered_source") window.reference[2].source = window.reference[0].source;
			if (name == "unordered_raw") window.raw[2].source = window.raw[1].source;
			if (name == "short_window") window.reference.resize(2);
			if (name == "degenerate_entry") window.reference[1].position = window.reference[0].position;
			if (name == "point_budget") window.config.max_reference_points = window.reference.size() - 1;
			if (name == "match_budget") window.config.max_match_work = 1;
			if (name == "invalid_config") window.config.max_displacement = std::numeric_limits<double>::quiet_NaN();
			if (name == "nonfinite_weight") window.config.raw_weight = std::numeric_limits<double>::infinity();
			if (name == "negative_threshold") window.config.activation_angle = -1;
			if (name == "invalid_fraction") window.reference[2].source.fraction = 1.1;
			if (name == "huge_raw") window.raw[2].position.x = std::numeric_limits<float>::max();
			if (name == "raw_budget") window.config.max_raw_points = window.raw.size() - 1;
			if (name == "validation_budget") window.config.max_validation_work = 1;
			if (name == "nonfinite_spacing") window.config.physical_turn_spacing = std::numeric_limits<double>::infinity();
			if (name == "zero_spacing") window.config.physical_turn_spacing = 0;
			if (name == "tiny_spacing_budget") window.config.physical_turn_spacing = 1e-300;
			if (name == "huge_path_budget")
			{
				for (auto& point : window.reference) { point.position.x *= 1e20f; point.position.y *= 1e20f; }
				for (auto& point : window.raw) { point.position.x *= 1e20f; point.position.y *= 1e20f; }
				window.config.max_displacement = 1e22;
			}
			if (name == "huge_raw_count")
			{
				const Vec2 endpoint = window.raw.back().position;
				window.raw.resize(4096);
				for (size_t i = 0; i < window.raw.size(); ++i) window.raw[i] = {endpoint, 10000 + i};
			}
			if (name == "large_source_reversed")
			{
				constexpr std::uint64_t offset = std::uint64_t{1} << 54;
				for (auto& reference : window.reference) { reference.source.first += offset; reference.source.second += offset; }
				window.reference[2].source = window.reference[1].source;
				--window.reference[2].source.first;
				--window.reference[2].source.second;
			}
			auto request = window.Request();
			if (name == "nonfinite_endpoint") request.endpoint.x = std::numeric_limits<float>::infinity();
			if (name == "bad_range") ++request.replacement_range.begin;
			if (name == "range_overflow") request.replacement_range = {std::numeric_limits<std::uint64_t>::max(), 0};
			if (name == "empty_raw") request.raw = {};
			if (name == "empty_reference") request.reference = {};
			const auto before = Serialize(request);
			for (bool control : {false, true})
			{
				TerminalTailScratch scratch;
				scratch.patch_points.push_back({{1, 2}, {1, 1, 0}, {0, 0}});
				const auto outcome = control ? ReconstructTerminalTailControl(request, scratch) : ReconstructTerminalTail(request, scratch);
				checks.Check(name == "huge_raw" ? outcome.status != TerminalTailStatus::kApply : outcome.status == TerminalTailStatus::kRejected, name + " rejected or guarded");
				if (name == "short_window" || name == "degenerate_entry" || name == "empty_raw" || name == "empty_reference")
					checks.Check(outcome.reason == TerminalTailReason::kInsufficientWindow, name + " insufficient-window reason");
				else if (name == "point_budget" || name == "match_budget" || name == "raw_budget" || name == "validation_budget" || name == "huge_raw_count" || name == "tiny_spacing_budget" || name == "huge_path_budget")
					checks.Check(outcome.reason == TerminalTailReason::kBudgetExceeded, name + " explicit budget reason");
				else if (name != "huge_raw") checks.Check(outcome.reason == TerminalTailReason::kInvalidInput, name + " invalid-input reason");
				checks.Check(outcome.patch.points.empty() && scratch.patch_points.empty(), name + " clears stale patch atomically");
				checks.Check(before == Serialize(request), name + " immutable inputs");
				if (name == "huge_raw_count") checks.Check(outcome.diagnostics.match_work == 0, "raw point budget rejects before geometric matching");
				Json::Value edge;
				edge["name"] = name;
				edge["control"] = control;
				edge["request"] = before;
				edge["status"] = static_cast<int>(outcome.status);
				edge["reason"] = Reason(outcome.reason);
				report.append(edge);
			}
		}
	}

	void DuplicateRawRegression(Checks& checks, Json::Value& report, const Window& original, const std::string& fixtureName)
	{
		for (bool internal : {false, true})
		{
			Window changed = original;
			const size_t last = changed.raw.size() - 1;
			const bool terminalDuplicate = Exact(changed.raw[last - 1].position, changed.raw[last].position);
			checks.Check(terminalDuplicate, fixtureName + " immediate Up fixture has duplicate raw endpoint");
			if (!terminalDuplicate) return;
			// 删除重复 Move 保留真实 Up；内部重复另用独立有序编号，几何来源与属性来源无关。
			if (!internal) changed.raw.erase(changed.raw.end() - 2);
			else
			{
				const size_t middle = changed.raw.size() / 2;
				changed.raw.insert(changed.raw.begin() + middle, changed.raw[middle]);
				for (size_t i = 0; i < changed.raw.size(); ++i) changed.raw[i].source = 10000 + i;
			}
			for (bool control : {false, true})
			{
				std::ostringstream label;
				label << std::setprecision(17) << fixtureName << " duplicate_raw window=" << original.duration
					<< (internal ? " internal" : " terminal") << (control ? " control" : " candidate");
				const std::string context = label.str();
				const auto originalRequest = original.Request(), changedRequest = changed.Request();
				const auto originalBefore = Serialize(originalRequest), changedBefore = Serialize(changedRequest);
				TerminalTailScratch originalScratch, changedScratch;
				const auto a = control ? ReconstructTerminalTailControl(originalRequest, originalScratch) : ReconstructTerminalTail(originalRequest, originalScratch);
				const auto b = control ? ReconstructTerminalTailControl(changedRequest, changedScratch) : ReconstructTerminalTail(changedRequest, changedScratch);
				checks.Check(a.status == b.status && a.reason == b.reason, context + " duplicate consumption preserves outcome");
				checks.Check(a.patch.points.size() == b.patch.points.size(), context + " equivalent patch count");
				Json::Value edge;
				edge["name"] = context;
				edge["original_request"] = originalBefore;
				edge["changed_request"] = changedBefore;
				edge["original_status"] = static_cast<int>(a.status);
				edge["changed_status"] = static_cast<int>(b.status);
				edge["original_reason"] = Reason(a.reason);
				edge["changed_reason"] = Reason(b.reason);
				for (size_t i = 0; i < std::min(a.patch.points.size(), b.patch.points.size()); ++i)
				{
					const auto& x = a.patch.points[i];
					const auto& y = b.patch.points[i];
					checks.Check(Exact(x.position, y.position) && x.source.first == y.source.first && x.source.second == y.source.second &&
						std::bit_cast<std::uint64_t>(x.source.fraction) == std::bit_cast<std::uint64_t>(y.source.fraction), context + " exact geometry and caller source");
					const auto matchedPosition = [](const Window& window, const TerminalTailRawMatch& match)
					{
						const Vec2 from = window.raw[match.segment].position, to = window.raw[match.segment + 1].position;
						return Vec2{static_cast<float>(double(from.x) + (double(to.x) - from.x) * match.fraction),
							static_cast<float>(double(from.y) + (double(to.y) - from.y) * match.fraction)};
					};
					// raw 段编号随删点/插点变化，核验它们指向的真实几何位置。
					checks.Check(Exact(matchedPosition(original, x.raw_match), matchedPosition(changed, y.raw_match)), context + " equivalent raw geometry correspondence");
					edge["original_patch_positions"].append(Position(x.position));
					edge["changed_patch_positions"].append(Position(y.position));
				}
				checks.Check(originalBefore == Serialize(originalRequest) && changedBefore == Serialize(changedRequest), context + " inputs immutable");
				report.append(edge);
			}
		}
	}

	void EndpointRepairRegression(Checks& checks, Json::Value& report)
	{
		Window window;
		for (size_t i = 0; i < 5; ++i)
		{
			window.reference.push_back({{static_cast<float>(i), 0}, {1000 + i, 1000 + i, 0}, false});
			window.raw.push_back({{static_cast<float>(i), i == 4 ? 0.2f : 0.0f}, 100 + i});
		}
		const auto request = window.Request();
		const auto before = Serialize(request);
		for (bool control : {false, true})
		{
			const std::string context = std::string("nonpinned_true_endpoint_repair ") + (control ? "control" : "candidate");
			TerminalTailScratch scratch;
			const auto outcome = control ? ReconstructTerminalTailControl(request, scratch) : ReconstructTerminalTail(request, scratch);
			checks.Check(outcome.status == TerminalTailStatus::kApply && outcome.reason == TerminalTailReason::kApplied, context + " publishes endpoint repair");
			checks.Check(outcome.patch.points.size() == request.reference.size(), context + " fixed point count");
			checks.Check(outcome.patch.replacement_range.begin == request.replacement_range.begin && outcome.patch.replacement_range.end == request.replacement_range.end &&
				outcome.patch.baseline_token == request.baseline_token, context + " range and version identity");
			if (outcome.patch.points.size() == request.reference.size())
			{
				checks.Check(Exact(outcome.patch.points[0].position, request.reference[0].position) && Exact(outcome.patch.points[1].position, request.reference[1].position), context + " fixed incoming segment");
				checks.Check(Exact(outcome.patch.points.back().position, request.endpoint), context + " exact true Up");
			}
			Json::Value edge;
			edge["name"] = context;
			edge["scope"] = "Endpoint repair of an unpinned reference after hard gates; no hook improvement claim.";
			edge["request"] = before;
			edge["status"] = static_cast<int>(outcome.status);
			edge["reason"] = Reason(outcome.reason);
			for (size_t i = 0; i < outcome.patch.points.size(); ++i)
			{
				const auto& point = outcome.patch.points[i];
				checks.Check(point.position.IsFinite(), context + " finite position");
				if (i < request.reference.size())
				{
					const auto source = request.reference[i].source;
					checks.Check(point.source.first == source.first && point.source.second == source.second &&
						std::bit_cast<std::uint64_t>(point.source.fraction) == std::bit_cast<std::uint64_t>(source.fraction), context + " exact caller source");
				}
				edge["patch_positions"].append(Position(point.position));
			}
			checks.Check(before == Serialize(request), context + " immutable inputs");
			report.append(edge);
		}
	}

	void AdjacentOverlapRegression(Checks& checks, Json::Value& report)
	{
		Window window;
		window.reference = {{{0, 0}, {1000, 1000, 0}, false}, {{1, 0}, {1001, 1001, 0}, false},
			{{0.5f, 0.05f}, {1002, 1002, 0}, false}, {{0.2f, 0}, {1003, 1003, 0}, false}};
		window.raw = {{{0, 0}, 100}, {{1, 0}, 101}, {{0.5f, 0}, 102}, {{0.2f, 0}, 103}};
		window.config.displacement_regularization = 1e-12;
		window.config.activation_angle = 0;
		window.config.max_raw_corner_angle = kPi;
		const auto request = window.Request();
		const auto before = Serialize(request);
		TerminalTailScratch scratch;
		const auto outcome = ReconstructTerminalTail(request, scratch);
		checks.Check(outcome.status == TerminalTailStatus::kRejected && outcome.reason == TerminalTailReason::kNewIntersection, "public API rejects newly adjacent overlapping reverse segments");
		checks.Check(outcome.patch.points.empty() && scratch.patch_points.empty() && before == Serialize(request), "overlap rejection atomic and input immutable");
		Json::Value edge;
		edge["name"] = "near_reversal_becomes_adjacent_overlap";
		edge["scope"] = "synthetic public API adversarial case; independent from actual Modeler effect fixtures";
		edge["request"] = before;
		edge["status"] = static_cast<int>(outcome.status);
		edge["reason"] = Reason(outcome.reason);
		edge["reference_adjacent_overlap_count"] = Json::UInt64(AdjacentOverlapCount(Positions(window.reference), request.config.epsilon));
		report.append(edge);
	}

	bool Write(const wchar_t* path, const Json::Value& report)
	{
		std::ofstream stream(std::filesystem::path(path), std::ios::binary);
		Json::StreamWriterBuilder builder;
		builder["precision"] = 17;
		builder["precisionType"] = "significant";
		builder["indentation"] = "";
		stream << Json::writeString(builder, report) << '\n';
		stream.flush();
		return stream.good();
	}

	double Percentile(std::vector<double> values, double fraction)
	{
		std::sort(values.begin(), values.end());
		const double index = (values.size() - 1) * fraction;
		const size_t lo = static_cast<size_t>(index), hi = std::min(lo + 1, values.size() - 1);
		return values[lo] + (values[hi] - values[lo]) * (index - lo);
	}

	Window DenseStress(const Window& input, size_t count)
	{
		Window result = input;
		result.reference.clear();
		result.raw.clear();
		const auto reference = Positions(input.reference), raw = RawPositions(input);
		const double referenceLength = Length(reference), rawLength = Length(raw);
		for (size_t i = 0; i < count; ++i)
		{
			const double fraction = static_cast<double>(i) / (count - 1);
			result.reference.push_back({AtArc(reference, referenceLength * fraction), {1000 + i, 1000 + i, 0}, i + 1 == count});
			result.raw.push_back({AtArc(raw, rawLength * fraction), 10000 + i});
		}
		result.reference.back().position = input.reference.back().position;
		result.raw.back().position = input.raw.back().position;
		return result;
	}

	struct SweepTotals
	{
		double rawWeight = 0, regularization = 0;
		std::uint64_t cases = 0, applies = 0, improvements = 0, negativeDamage = 0, hardCheckFailures = 0;
	};
}

int RunModelerTailReplay(const wchar_t* reportPath)
{
	Checks checks;
	Json::Value report;
	report["schema"] = "modeler-tail-replay-v1";
	report["provider"] = "vcpkg-abseil-20250512.1#0";
	report["acceptance_geometry"] = "offline_surrogate_distance_decrease_then_pin_true_up";
	report["limitations"] = "No Draw3 geometry admission helper/L1/width/taper/Stored/reconnect integration; unit=fixture pixel; consumed raw and model times coincide; unordered corridor distances are diagnostics, not branch proofs.";
	report["metric_spacing"] = kMetricSpacing;
	report["terminal_probe_length"] = kProbeLength;
	report["negative_safeguards_scope"] = "Explicit research allowances of 0.1 rad signed-turn error and 0.25 fixture pixel corridor growth; not proof of production visual/pressure safety.";
	report["weight_sweep_scope"] = "Predeclared 9 raw-weight/regularization combinations on every fixture/window; defaults retained separately. No production parameter choice is implied by the report.";
	std::array<SweepTotals, 9> sweepTotals;
	size_t combination = 0;
	for (double rawWeight : {0.25, 1.0, 4.0})
		for (double regularization : {0.003, 0.03, 0.3})
			sweepTotals[combination++] = {rawWeight, regularization};
	std::uint64_t improvementCount = 0, applyCount = 0, caseCount = 0;
	Window edgeFixture;
	for (bool kalman : {false, true})
		for (const char* shape : {"straight", "circle", "arc", "arc_scale2", "arc_nonuniform", "quick_curve", "s_bend", "reverse", "loop", "self_cross", "real_corner", "duplicate_corner", "sparse", "repeated", "stationary", "single"})
		{
			const auto fixture = MakeFixture(checks, shape, kalman);
			Json::Value fixtureReport = FixtureMetadata(fixture);
			for (double duration : {0.055, 0.1, 0.16})
			{
				Window window = MakeWindow(fixture, duration);
				if (std::string(shape) == "quick_curve") DuplicateRawRegression(checks, report["edge_cases"], window, fixture.name);
				const auto baselinePoints = Positions(window.reference), rawPoints = RawPositions(window);
				const double probe = std::min(kProbeLength * window.unitScale, Length(baselinePoints));
				const auto baseline = MeasureGeometry(baselinePoints, rawPoints, probe, kMetricSpacing * window.unitScale), raw = MeasureGeometry(rawPoints, rawPoints, probe, kMetricSpacing * window.unitScale);
				const auto control = Evaluate(checks, window, true, fixture.name), candidate = Evaluate(checks, window, false, fixture.name);
				Json::Value row;
				row["duration_seconds"] = duration;
				row["physical_metric_spacing_units"] = kMetricSpacing * window.unitScale;
				row["physical_probe_length_units"] = probe;
				row["caller_uncommitted_begin_index"] = Json::UInt64(window.begin);
				row["raw_context_begin_index"] = Json::UInt64(window.rawBegin);
				row["request"] = Serialize(window.Request());
				row["baseline"] = Serialize(baseline);
				row["raw_truth"] = Serialize(raw);
				row["control"] = control.report;
				row["candidate"] = candidate.report;
				Window localWindow = window;
				const double rawCutoff = fixture.inputs.back().time.Value() - duration;
				localWindow.raw.clear();
				for (size_t rawIndex = 0; rawIndex < fixture.raw.size(); ++rawIndex)
					if (fixture.inputs[rawIndex].time.Value() >= rawCutoff) localWindow.raw.push_back(fixture.raw[rawIndex]);
				if (localWindow.raw.size() > 64) localWindow.raw.erase(localWindow.raw.begin(), localWindow.raw.end() - 64);
				// 模型窗口复用；原始点可选，零辅助结果也由真实 C++ 求解器生成。
				const auto local = EvaluateLocal(checks, localWindow, false, fixture.name);
				row["local_candidate"] = local.report;
				if (std::string(shape) == "sparse")
				{
					const auto zeroAux = EvaluateLocal(checks, localWindow, true, fixture.name);
					row["local_zero_aux"] = zeroAux.report;
					if (duration == 0.16) checks.Check(zeroAux.status == TerminalTailStatus::kApply, fixture.name + " zero auxiliary visual repair must Apply");
				}
				if (duration == 0.16)
				{
					const std::string name(shape);
					if (name == "quick_curve" || name == "circle" || name == "arc" || name == "s_bend" || name == "loop" || name == "sparse")
						checks.Check(local.status == TerminalTailStatus::kApply, fixture.name + " local representative visual repair must Apply");
					if (name == "straight") checks.Check(local.status != TerminalTailStatus::kApply, fixture.name + " normal straight must stay unchanged");
				}

				const double beforeError = std::abs(baseline.totalTurn - raw.totalTurn), afterError = std::abs(candidate.metrics.totalTurn - raw.totalTurn);
				row["baseline_terminal_turn_error_to_raw"] = beforeError;
				row["candidate_terminal_turn_error_to_raw"] = afterError;
				const bool positiveShape = std::string(shape) == "quick_curve" || std::string(shape).starts_with("arc") || std::string(shape) == "circle";
				const bool improved = candidate.status == TerminalTailStatus::kApply && positiveShape &&
					baseline.maxTurn > raw.maxTurn + 0.05 && candidate.metrics.maxTurn + 0.03 < baseline.maxTurn && afterError + 0.03 < beforeError;
				row["independent_physical_hook_improvement"] = improved;
				row["negative_safeguard_turn_allowance_radians"] = 0.1;
				row["negative_safeguard_corridor_allowance_units"] = 0.25 * window.unitScale;
				if (improved) ++improvementCount;
				if (candidate.status == TerminalTailStatus::kApply)
				{
					++applyCount;
					if (!positiveShape)
						checks.Check(afterError <= beforeError + 0.1 && candidate.metrics.maxCorridor <= baseline.maxCorridor + 0.25 * window.unitScale, fixture.name + " negative sample safeguard");
				}
				for (auto& total : sweepTotals)
				{
					Window experimental = window;
					experimental.config.raw_weight = total.rawWeight;
					experimental.config.displacement_regularization = total.regularization;
					const bool sameAsDefault = total.rawWeight == window.config.raw_weight && total.regularization == window.config.displacement_regularization;
					const auto sweepCandidate = sameAsDefault ? candidate : Evaluate(checks, experimental, false, fixture.name);
					const auto sweepControl = sameAsDefault ? control : Evaluate(checks, experimental, true, fixture.name);
					const double sweepError = std::abs(sweepCandidate.metrics.totalTurn - raw.totalTurn);
					const bool sweepImproved = sweepCandidate.status == TerminalTailStatus::kApply && positiveShape &&
						baseline.maxTurn > raw.maxTurn + 0.05 && sweepCandidate.metrics.maxTurn + 0.03 < baseline.maxTurn && sweepError + 0.03 < beforeError;
					const bool negativeDamage = !positiveShape && sweepCandidate.status == TerminalTailStatus::kApply &&
						(sweepError > beforeError + 0.1 || sweepCandidate.metrics.maxCorridor > baseline.maxCorridor + 0.25 * window.unitScale);
					Json::Value experimentalRow;
					experimentalRow["config"] = Config(experimental.config);
					experimentalRow["candidate"] = sweepCandidate.report;
					experimentalRow["control"] = sweepControl.report;
					experimentalRow["terminal_turn_error_to_raw"] = sweepError;
					experimentalRow["independent_physical_hook_improvement"] = sweepImproved;
					experimentalRow["negative_research_safeguard_damage"] = negativeDamage;
					experimentalRow["all_candidate_and_control_hard_checks_passed"] = sweepCandidate.hardChecksPassed && sweepControl.hardChecksPassed;
					row["weight_sweep"].append(experimentalRow);
					++total.cases;
					if (sweepCandidate.status == TerminalTailStatus::kApply) ++total.applies;
					if (sweepImproved) ++total.improvements;
					if (negativeDamage) ++total.negativeDamage;
					if (!sweepCandidate.hardChecksPassed || !sweepControl.hardChecksPassed) ++total.hardCheckFailures;
				}
				report["candidate_status_histogram"][std::to_string(static_cast<int>(candidate.status))] =
					report["candidate_status_histogram"][std::to_string(static_cast<int>(candidate.status))].asUInt() + 1;
				fixtureReport["candidate_status_histogram"][std::to_string(static_cast<int>(candidate.status))] =
					fixtureReport["candidate_status_histogram"][std::to_string(static_cast<int>(candidate.status))].asUInt() + 1;
				fixtureReport["windows"].append(row);
				++caseCount;
				if (std::string(shape) == "arc" && !kalman && duration == 0.1) edgeFixture = window;
			}
			report["fixtures"].append(fixtureReport);
		}
	EdgeCases(checks, report["edge_cases"], edgeFixture);
	LocalEdgeCases(checks, report["local_edge_cases"], edgeFixture);
	AdjacentOverlapRegression(checks, report["edge_cases"]);
	EndpointRepairRegression(checks, report["edge_cases"]);
	bool eligibleResearchConfiguration = false;
	for (const auto& total : sweepTotals)
	{
		Json::Value row;
		row["raw_weight"] = total.rawWeight;
		row["displacement_regularization"] = total.regularization;
		row["window_cases"] = Json::UInt64(total.cases);
		row["applies"] = Json::UInt64(total.applies);
		row["physical_hook_improvements"] = Json::UInt64(total.improvements);
		row["negative_research_safeguard_damage"] = Json::UInt64(total.negativeDamage);
		row["hard_check_failure_windows"] = Json::UInt64(total.hardCheckFailures);
		const bool eligible = total.improvements > 0 && total.negativeDamage == 0 && total.hardCheckFailures == 0;
		row["eligible_by_research_effect_and_negative_samples"] = eligible;
		eligibleResearchConfiguration |= eligible;
		report["weight_sweep_summary"].append(row);
	}
	checks.Check(eligibleResearchConfiguration, "at least one predeclared configuration improves an actual Modeler hook without negative research safeguard damage");
	report["window_cases"] = Json::UInt64(caseCount);
	report["apply_count"] = Json::UInt64(applyCount);
	report["physical_hook_improvement_count"] = Json::UInt64(improvementCount);
	report["initial_defaults_have_physical_improvement"] = improvementCount > 0;
	report["assertions"] = Json::UInt64(checks.count);
	report["failures"] = checks.failures;
	if (!Write(reportPath, report)) return 2;
	std::cout << "Tail replay: " << caseCount << " window cases, " << applyCount << " Apply, " << improvementCount << " physical improvements, " << checks.failures.size() << " failures.\n";
	return checks.failures.empty() ? 0 : 1;
}

int RunModelerTailBenchmark(const wchar_t* reportPath)
{
	Checks checks;
	Json::Value report;
	report["schema"] = "modeler-tail-benchmark-v1";
	report["provider"] = "vcpkg-abseil-20250512.1#0";
	report["scope"] = "single independent terminal solve on prewarmed scratch; excludes forward model, report, and metrics";
	for (bool kalman : {false, true})
	for (const char* shape : {"quick_curve", "arc", "s_bend", "self_cross", "dense_512", "dense_budget_reject"})
	{
		const bool stress = std::string(shape).starts_with("dense_");
		const auto fixture = MakeFixture(checks, stress ? "arc" : shape, kalman);
		auto window = MakeWindow(fixture, 0.16);
		if (stress) window = DenseStress(window, 512);
		if (std::string(shape) == "dense_budget_reject") window.config.max_reference_points = 511;
		const auto request = window.Request();
		for (int algorithm : {0, 1, 2})
		{
			const bool control = algorithm == 1;
			TerminalTailScratch scratch;
			std::vector<double> durations;
			Json::Value row;
			row["shape"] = shape;
			row["predictor"] = kalman ? "kalman" : "stroke_end";
			row["control"] = control;
			row["algorithm"] = algorithm == 2 ? "local" : control ? "control" : "stage2";
			row["stress_scope"] = stress ? "512-point interpolation of actual Modeler/raw window; bounded requested workload, not a universal worst-case proof" : "actual Modeler window";
			row["reference_points"] = Json::UInt64(request.reference.size());
			row["raw_points"] = Json::UInt64(request.raw.size());
			row["request"] = Serialize(request);
			row["actual_forward_fixture"] = FixtureMetadata(fixture);
			size_t peakMatchWork = 0, peakValidationWork = 0, peakIterations = 0;
			for (int i = -16; i < 256; ++i)
			{
				const auto start = Clock::now();
				const auto result = algorithm == 2 ? ReconstructLocalTerminalTail(request, scratch) : control ? ReconstructTerminalTailControl(request, scratch) : ReconstructTerminalTail(request, scratch);
				const auto stop = Clock::now();
				checks.Check(result.diagnostics.match_work <= request.config.max_match_work && result.diagnostics.validation_work <= request.config.max_validation_work &&
					result.diagnostics.iterations <= request.config.max_iterations, "benchmark work budget");
				if (std::string(shape) == "dense_budget_reject") checks.Check(result.status == TerminalTailStatus::kRejected && result.reason == TerminalTailReason::kBudgetExceeded && result.diagnostics.match_work == 0, "benchmark early point-budget reject");
				if (i < 0) continue;
				peakMatchWork = std::max(peakMatchWork, result.diagnostics.match_work);
				peakValidationWork = std::max(peakValidationWork, result.diagnostics.validation_work);
				peakIterations = std::max(peakIterations, result.diagnostics.iterations);
				durations.push_back(std::chrono::duration<double, std::micro>(stop - start).count());
				row["status_histogram"][std::to_string(static_cast<int>(result.status))] = row["status_histogram"][std::to_string(static_cast<int>(result.status))].asUInt() + 1;
			}
			row["samples_us"] = Json::Value(Json::arrayValue);
			for (double value : durations) row["samples_us"].append(value);
			row["mean_us"] = std::accumulate(durations.begin(), durations.end(), 0.0) / durations.size();
			row["median_us"] = Percentile(durations, 0.5);
			row["p95_us"] = Percentile(durations, 0.95);
			row["max_us"] = *std::max_element(durations.begin(), durations.end());
			row["warmup_calls"] = 16;
			row["measured_calls"] = 256;
			row["peak_match_work"] = Json::UInt64(peakMatchWork);
			row["peak_validation_work"] = Json::UInt64(peakValidationWork);
			row["peak_iterations"] = Json::UInt64(peakIterations);
			report["cases"].append(row);
		}
	}
	report["assertions"] = Json::UInt64(checks.count);
	report["failures"] = checks.failures;
	if (!Write(reportPath, report)) return 2;
	return checks.failures.empty() ? 0 : 1;
}
