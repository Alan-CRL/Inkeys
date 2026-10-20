#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <ink_stroke_modeler/stroke_modeler.h>
#include <absl/strings/string_view.h>
#include <absl/types/compare.h>
#include <json/json.h>

#include <algorithm>
#include <array>
#include <bit>
#include <chrono>
#include <compare>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <sstream>
#include <string_view>
#include <type_traits>
#include <vector>

import draw3.ink_prediction;

// 对照程序本身也必须按固定的 Release ABI 消费两种实现。
#if !defined(NDEBUG) || defined(_DEBUG) || defined(_DLL) || !defined(_MT)
#error Modeler baseline requires NDEBUG and static release CRT.
#endif
static_assert(_ITERATOR_DEBUG_LEVEL == 0);
static_assert(_MSVC_LANG >= 202002L);
static_assert(ABSL_LTS_RELEASE_VERSION == 20250512 && ABSL_LTS_RELEASE_PATCH_LEVEL == 1);
static_assert(ABSL_OPTION_USE_INLINE_NAMESPACE == 1);
static_assert(std::is_same_v<absl::string_view, std::string_view>);
static_assert(std::is_same_v<absl::strong_ordering, std::strong_ordering>);

namespace
{
	using namespace ink::stroke_model;
	using Code = absl::StatusCode;
	using Clock = std::chrono::steady_clock;

	template<class T>
	std::string Bits(T value)
	{
		using Integer = std::conditional_t<sizeof(T) == 4, uint32_t, uint64_t>;
		std::ostringstream stream;
		stream << std::hex << std::setfill('0') << std::setw(sizeof(T) * 2)
			<< std::bit_cast<Integer>(value);
		return stream.str();
	}

	template<class T>
	Json::Value Number(T value)
	{
		Json::Value result;
		result["bits"] = Bits(value);
		// bits 保留 signed zero/NaN；十进制便于人工阅读，JSON 不写非有限数。
		result["value"] = std::isfinite(value) ? Json::Value(static_cast<double>(value)) : Json::Value();
		return result;
	}

	Json::Value Vector(Vec2 value)
	{
		Json::Value result;
		result["x"] = Number(value.x);
		result["y"] = Number(value.y);
		return result;
	}

	Json::Value Serialize(const Input& input)
	{
		Json::Value result;
		result["event_type"] = static_cast<int>(input.event_type);
		result["position"] = Vector(input.position);
		result["time"] = Number(input.time.Value());
		result["pressure"] = Number(input.pressure);
		result["tilt"] = Number(input.tilt);
		result["orientation"] = Number(input.orientation);
		return result;
	}

	Json::Value Serialize(const Result& value)
	{
		Json::Value result;
		result["position"] = Vector(value.position);
		result["velocity"] = Vector(value.velocity);
		result["acceleration"] = Vector(value.acceleration);
		result["time"] = Number(value.time.Value());
		result["pressure"] = Number(value.pressure);
		result["tilt"] = Number(value.tilt);
		result["orientation"] = Number(value.orientation);
		return result;
	}

	Json::Value Serialize(const StrokeModelParams& params)
	{
		Json::Value result;
		const auto& wobble = params.wobble_smoother_params;
		auto& w = result["wobble"];
		w["enabled"] = wobble.is_enabled;
		w["timeout"] = Number(wobble.timeout.Value());
		w["speed_floor"] = Number(wobble.speed_floor);
		w["speed_ceiling"] = Number(wobble.speed_ceiling);
		const auto& position = params.position_modeler_params;
		auto& p = result["position"];
		p["spring_mass_constant"] = Number(position.spring_mass_constant);
		p["drag_constant"] = Number(position.drag_constant);
		const auto& loop = position.loop_contraction_mitigation_params;
		auto& l = p["loop_contraction_mitigation"];
		l["enabled"] = loop.is_enabled;
		l["speed_lower_bound"] = Number(loop.speed_lower_bound);
		l["speed_upper_bound"] = Number(loop.speed_upper_bound);
		l["strength_lower"] = Number(loop.interpolation_strength_at_speed_lower_bound);
		l["strength_upper"] = Number(loop.interpolation_strength_at_speed_upper_bound);
		l["min_speed_sampling_window"] = Number(loop.min_speed_sampling_window.Value());
		const auto& sampling = params.sampling_params;
		auto& s = result["sampling"];
		s["min_output_rate"] = Number(sampling.min_output_rate);
		s["stopping_distance"] = Number(sampling.end_of_stroke_stopping_distance);
		s["end_max_iterations"] = sampling.end_of_stroke_max_iterations;
		s["max_outputs_per_call"] = sampling.max_outputs_per_call;
		s["max_estimated_angle"] = Number(sampling.max_estimated_angle_to_traverse_per_input);
		result["stylus_normal_projection"] = params.stylus_state_modeler_params.use_stroke_normal_projection;
		result["predictor_index"] = static_cast<int>(params.prediction_params.index());
		if (const auto* kalman = std::get_if<KalmanPredictorParams>(&params.prediction_params))
		{
			auto& k = result["kalman"];
			k["process_noise"] = Number(kalman->process_noise);
			k["measurement_noise"] = Number(kalman->measurement_noise);
			k["min_stable_iteration"] = kalman->min_stable_iteration;
			k["max_time_samples"] = kalman->max_time_samples;
			k["min_catchup_velocity"] = Number(kalman->min_catchup_velocity);
			k["acceleration_weight"] = Number(kalman->acceleration_weight);
			k["jerk_weight"] = Number(kalman->jerk_weight);
			k["prediction_interval"] = Number(kalman->prediction_interval.Value());
			const auto& confidence = kalman->confidence_params;
			auto& c = k["confidence"];
			c["desired_number_of_samples"] = confidence.desired_number_of_samples;
			c["max_estimation_distance"] = Number(confidence.max_estimation_distance);
			c["min_travel_speed"] = Number(confidence.min_travel_speed);
			c["max_travel_speed"] = Number(confidence.max_travel_speed);
			c["max_linear_deviation"] = Number(confidence.max_linear_deviation);
			c["baseline_linearity_confidence"] = Number(confidence.baseline_linearity_confidence);
		}
		return result;
	}

	bool Same(const Result& a, const Result& b)
	{
		// 不比较结构体 padding，也不把 +0/-0 混为同一值。
		return std::bit_cast<uint32_t>(a.position.x) == std::bit_cast<uint32_t>(b.position.x) &&
			std::bit_cast<uint32_t>(a.position.y) == std::bit_cast<uint32_t>(b.position.y) &&
			std::bit_cast<uint32_t>(a.velocity.x) == std::bit_cast<uint32_t>(b.velocity.x) &&
			std::bit_cast<uint32_t>(a.velocity.y) == std::bit_cast<uint32_t>(b.velocity.y) &&
			std::bit_cast<uint32_t>(a.acceleration.x) == std::bit_cast<uint32_t>(b.acceleration.x) &&
			std::bit_cast<uint32_t>(a.acceleration.y) == std::bit_cast<uint32_t>(b.acceleration.y) &&
			std::bit_cast<uint64_t>(a.time.Value()) == std::bit_cast<uint64_t>(b.time.Value()) &&
			std::bit_cast<uint32_t>(a.pressure) == std::bit_cast<uint32_t>(b.pressure) &&
			std::bit_cast<uint32_t>(a.tilt) == std::bit_cast<uint32_t>(b.tilt) &&
			std::bit_cast<uint32_t>(a.orientation) == std::bit_cast<uint32_t>(b.orientation);
	}

	bool Same(const std::vector<Result>& a, const std::vector<Result>& b)
	{
		return a.size() == b.size() && std::equal(a.begin(), a.end(), b.begin(),
			[](const Result& x, const Result& y) { return Same(x, y); });
	}

	struct Checks
	{
		Json::Value failures{ Json::arrayValue };
		uint64_t count = 0;

		void Check(bool valid, const std::string& description)
		{
			++count;
			if (valid) return;
			failures.append(description);
			std::cerr << "Modeler baseline FAILED: " << description << '\n';
		}
	};

	StrokeModelParams Parameters(bool kalman)
	{
		const auto configuration = draw3::CreateStrokeModelConfiguration(96);
		auto result = configuration.modelParams;
		result.prediction_params = kalman ? PredictionParams(configuration.kalmanPredictorParams) :
			PredictionParams(StrokeEndPredictorParams{});
		return result;
	}

	Input Point(Input::EventType event, float x, float y, double time, size_t index = 0)
	{
		return { .event_type = event, .position = {x, y}, .time = Time(time),
			.pressure = 0.1f + static_cast<float>(index % 9) * 0.1f,
			.tilt = 0.05f + static_cast<float>(index % 13) * 0.1f,
			.orientation = static_cast<float>(index % 60) * 0.1f };
	}

	std::vector<Input> Inputs(const std::string& name)
	{
		const size_t count = name == "long_stroke" ? 700 : name == "single_point" ? 1 : 64;
		std::vector<Input> result;
		result.reserve(count + 1);
		for (size_t i = 0; i < count; ++i)
		{
			float x = 10.f + static_cast<float>(i) * 2.5f;
			float y = 20.f + static_cast<float>(i) * 0.25f;
			if (name == "arc" || name == "curvature_instant_up" || name == "long_stroke")
			{
				const double angle = static_cast<double>(i) * (name == "curvature_instant_up" ? 0.23 : 0.035);
				const double radius = name == "curvature_instant_up" ? 7.0 : 80.0;
				x = static_cast<float>(100.0 + radius * std::cos(angle));
				y = static_cast<float>(120.0 + radius * std::sin(angle));
			}
			if (name == "repeated_position") { x = 30; y = 40; }
			if (name == "stationary_resume" && i >= 16 && i < 40)
			{
				x = 47.5f;
				y = 23.75f;
			}
			const double time = static_cast<double>(i) / 120.0;
			result.push_back(Point(i == 0 ? Input::EventType::kDown : Input::EventType::kMove, x, y, time, i));
			if (i % 19 == 0) result.back().pressure = result.back().tilt = result.back().orientation = -1;
		}
		auto up = result.back();
		up.event_type = Input::EventType::kUp;
		if (name != "single_point" && name != "curvature_instant_up") up.time += Duration(1.0 / 120.0);
		if (name == "curvature_instant_up") up.position += Vec2{0.75f, -0.5f};
		result.push_back(up);
		return result;
	}

	class Replay
	{
	public:
		Replay(Checks& checks, const std::string& name, const StrokeModelParams& params) :
			checks_(checks), name_(name)
		{
			report["name"] = name;
			report["params"] = Serialize(params);
			report["operations"] = Json::Value(Json::arrayValue);
			Record("reset_params", model.Reset(params), 0, {});
			checks_.Check(lastStatus_ == Code::kOk, name_ + ": valid reset");
		}

		void Update(const Input& input, Code expected = Code::kOk)
		{
			const auto prefix = results;
			const auto status = model.Update(input, results);
			checks_.Check(status.code() == expected, name_ + ": Update status");
			checks_.Check(results.size() >= prefix.size() && std::equal(prefix.begin(), prefix.end(), results.begin(),
				[](const Result& a, const Result& b) { return Same(a, b); }), name_ + ": append prefix");
			if (!status.ok()) checks_.Check(Same(prefix, results), name_ + ": error preserves results");
			Record("update", status, prefix.size(), results);
			report["operations"][report["operations"].size() - 1]["input"] = Serialize(input);
		}

		std::vector<Result> Predict(Code expected = Code::kOk)
		{
			std::vector<Result> prediction(1); // 错误时也必须清空调用者原有的预测。
			const auto status = model.Predict(prediction);
			checks_.Check(status.code() == expected, name_ + ": Predict status");
			if (!status.ok()) checks_.Check(prediction.empty(), name_ + ": failed Predict clears results");
			Record("predict", status, 0, prediction);
			return prediction;
		}

		void Mark(const char* operation)
		{
			Json::Value value;
			value["op"] = operation;
			report["operations"].append(value);
		}

		void Record(const char* op, const absl::Status& status, size_t prefix, const std::vector<Result>& values)
		{
			lastStatus_ = status.code();
			Json::Value operation;
			operation["op"] = op;
			operation["status_code"] = static_cast<int>(status.code());
			operation["status_message"] = std::string(status.message());
			operation["prefix_count"] = Json::UInt64(prefix);
			operation["total_count"] = Json::UInt64(values.size());
			operation["results"] = Json::Value(Json::arrayValue);
			for (size_t i = prefix; i < values.size(); ++i)
			{
				const auto& value = values[i];
				checks_.Check(value.position.IsFinite() && value.velocity.IsFinite() && value.acceleration.IsFinite() &&
					std::isfinite(value.time.Value()) && std::isfinite(value.pressure) && std::isfinite(value.tilt) &&
					std::isfinite(value.orientation), name_ + ": finite output");
				if (i > 0) checks_.Check(value.time >= values[i - 1].time, name_ + ": nondecreasing output time");
				operation["results"].append(Serialize(value));
			}
			report["operations"].append(operation);
		}

		StrokeModeler model;
		std::vector<Result> results;
		Json::Value report;

	private:
		Checks& checks_;
		std::string name_;
		Code lastStatus_ = Code::kUnknown;
	};

	Json::Value StrokeCase(Checks& checks, const std::string& shape, bool kalman)
	{
		const auto params = Parameters(kalman);
		Replay replay(checks, shape + (kalman ? "/kalman" : "/stroke_end"), params);
		StrokeModeler withoutPredict;
		checks.Check(withoutPredict.Reset(params).ok(), "reference reset");
		std::vector<Result> reference;
		for (const auto& input : Inputs(shape))
		{
			replay.Update(input);
			checks.Check(withoutPredict.Update(input, reference).ok(), "reference Update");
			checks.Check(Same(replay.results, reference), "Predict does not change subsequent Update");
			if (input.event_type != Input::EventType::kUp)
			{
				const auto first = replay.Predict();
				const auto second = replay.Predict();
				checks.Check(Same(first, second), "repeated Predict is identical");
			}
		}
		replay.Predict(Code::kFailedPrecondition);
		checks.Check(replay.results.size() >= 2, "completed stroke output");
		return replay.report;
	}

	Json::Value RestoreCase(Checks& checks, bool kalman)
	{
		const auto params = Parameters(kalman);
		Replay replay(checks, kalman ? "save_restore/kalman" : "save_restore/stroke_end", params);
		const auto inputs = Inputs("arc");
		for (size_t i = 0; i < 30; ++i) replay.Update(inputs[i]);
		const auto prefix = replay.results;
		replay.model.Save();
		replay.Mark("save");
		const auto savedPrediction = replay.Predict();
		for (size_t i = 30; i < inputs.size(); ++i) replay.Update(inputs[i]);
		const auto first = replay.results;
		// Restore 还原模型；调用者拥有的输出容器需要自行还原。
		for (int repeat = 0; repeat < 2; ++repeat)
		{
			replay.model.Restore();
			replay.Mark("restore");
			replay.results = prefix;
			checks.Check(Same(savedPrediction, replay.Predict()), "Restore preserves saved prediction");
			for (size_t i = 30; i < inputs.size(); ++i) replay.Update(inputs[i]);
			checks.Check(Same(first, replay.results), "Restore replays identical results");
		}
		const auto reset = replay.model.Reset();
		replay.Record("reset", reset, 0, {});
		checks.Check(reset.ok(), "Reset keeps parameters");
		replay.model.Restore();
		replay.Mark("restore_after_reset");
		replay.Predict(Code::kFailedPrecondition);
		replay.results.clear();
		replay.Update(inputs.front());
		return replay.report;
	}

	Json::Value ErrorCases(Checks& checks)
	{
		Replay replay(checks, "invalid_and_backwards", Parameters(false));
		replay.Update(Point(Input::EventType::kMove, 1, 2, 0), Code::kFailedPrecondition);
		replay.Update(Point(Input::EventType::kUp, 1, 2, 0), Code::kFailedPrecondition);
		replay.Predict(Code::kFailedPrecondition);
		const auto down = Point(Input::EventType::kDown, 1, 2, 1);
		replay.Update(down);
		replay.Update(down, Code::kInvalidArgument);
		replay.Update(Point(Input::EventType::kDown, 2, 3, 1.01), Code::kFailedPrecondition);
		replay.Update(Point(Input::EventType::kMove, 2, 3, 0.9), Code::kInvalidArgument);
		for (int field = 0; field < 6; ++field)
		{
			auto invalid = Point(Input::EventType::kMove, 2, 3, 1.1);
			if (field == 0) invalid.position.x = std::numeric_limits<float>::quiet_NaN();
			if (field == 1) invalid.position.y = std::numeric_limits<float>::infinity();
			if (field == 2) invalid.time = Time(std::numeric_limits<double>::infinity());
			if (field == 3) invalid.time = Time(std::numeric_limits<double>::quiet_NaN());
			if (field == 4) invalid.position.x = -std::numeric_limits<float>::infinity();
			if (field == 5) invalid.position.y = std::numeric_limits<float>::quiet_NaN();
			replay.Update(invalid, Code::kInvalidArgument);
		}
		replay.Update(Point(static_cast<Input::EventType>(99), 1, 2, 1.1), Code::kInvalidArgument);
		replay.Update(Point(Input::EventType::kUp, 1, 2, 1.01));
		replay.Update(Point(Input::EventType::kMove, 1, 2, 1.02), Code::kFailedPrecondition);
		replay.Predict(Code::kFailedPrecondition);
		StrokeModeler uninitialized;
		std::vector<Result> sentinel(1);
		const auto before = sentinel;
		auto status = uninitialized.Update(down, sentinel);
		checks.Check(status.code() == Code::kFailedPrecondition && Same(before, sentinel), "uninitialized Update");
		replay.Record("uninitialized_update", status, 0, sentinel);
		status = uninitialized.Predict(sentinel);
		checks.Check(status.code() == Code::kFailedPrecondition && sentinel.empty(), "uninitialized Predict");
		replay.Record("uninitialized_predict", status, 0, sentinel);
		status = uninitialized.Reset();
		checks.Check(status.code() == Code::kFailedPrecondition, "uninitialized Reset");
		replay.Record("uninitialized_reset", status, 0, {});
		auto invalidParams = Parameters(false);
		invalidParams.sampling_params.min_output_rate = -1;
		status = uninitialized.Reset(invalidParams);
		checks.Check(status.code() == Code::kInvalidArgument, "invalid parameter Reset");
		replay.Record("invalid_reset", status, 0, {});
		replay.report["invalid_params"] = Serialize(invalidParams);
		// 当前库不检查非位置属性的范围；基线要保留实际合同，不能借迁移加强验证。
		auto attributes = down;
		attributes.pressure = 2;
		attributes.tilt = 2;
		attributes.orientation = 7;
		status = ValidateInput(attributes);
		checks.Check(status.ok(), "existing stylus attribute validation contract");
		replay.Record("validate_stylus_attributes", status, 0, {});
		replay.report["operations"][replay.report["operations"].size() - 1]["input"] = Serialize(attributes);
		return replay.report;
	}

	Json::Value BudgetCase(Checks& checks)
	{
		auto params = Parameters(false);
		params.sampling_params.min_output_rate = 128;
		params.sampling_params.max_outputs_per_call = 8;
		params.wobble_smoother_params.is_enabled = false;
		Replay replay(checks, "output_budget", params);
		replay.Update(Point(Input::EventType::kDown, 0, 0, 0));
		const auto before = replay.results.size();
		replay.Update(Point(Input::EventType::kMove, 1, 0, 0.0625));
		checks.Check(replay.results.size() - before == 8, "exact output budget is accepted");
		replay.Update(Point(Input::EventType::kMove, 2, 0, 0.1328125), Code::kInvalidArgument);
		replay.Update(Point(Input::EventType::kMove, 2, 0, 1e10), Code::kInvalidArgument);
		replay.Update(Point(Input::EventType::kUp, 1, 0, 0.125));
		return replay.report;
	}

	bool Write(const wchar_t* path, const Json::Value& report)
	{
		std::ofstream stream(std::filesystem::path(path), std::ios::binary);
		if (!stream) return false;
		Json::StreamWriterBuilder writer;
		writer["precision"] = 17;
		writer["precisionType"] = "significant";
		writer["indentation"] = "";
		stream << Json::writeString(writer, report) << '\n';
		stream.flush();
		return stream.good();
	}

	void Hash(uint64_t& hash, uint64_t value)
	{
		for (int byte = 0; byte < 8; ++byte)
		{
			hash ^= (value >> (byte * 8)) & 255;
			hash *= 1099511628211ULL;
		}
	}

	void Hash(uint64_t& hash, const Result& result)
	{
		for (float value : {result.position.x, result.position.y, result.velocity.x, result.velocity.y,
			result.acceleration.x, result.acceleration.y, result.pressure, result.tilt, result.orientation})
			Hash(hash, std::bit_cast<uint32_t>(value));
		Hash(hash, std::bit_cast<uint64_t>(result.time.Value()));
	}

	template<class Operation>
	double Measure(Operation operation)
	{
		const auto start = Clock::now();
		operation();
		const auto stop = Clock::now();
		return std::chrono::duration<double, std::nano>(stop - start).count();
	}

	Json::Value BenchmarkCase(Checks& checks, bool kalman)
	{
		const auto params = Parameters(kalman);
		const auto inputs = Inputs("arc");
		Json::Value result;
		result["name"] = kalman ? "kalman" : "stroke_end";
		result["params"] = Serialize(params);
		result["inputs"] = Json::Value(Json::arrayValue);
		for (const auto& input : inputs) result["inputs"].append(Serialize(input));
		uint64_t hash = 14695981039346656037ULL;
		StrokeModeler model;
		std::vector<Result> output;
		std::vector<Result> prediction;
		output.reserve(4096);
		prediction.reserve(4096);
		std::array<std::vector<Result>, 64> predictions;
		for (auto& values : predictions) values.reserve(512);
		for (int batch = -8; batch < 64; ++batch)
		{
			checks.Check(model.Reset(params).ok(), "benchmark initialize");
			output.clear();
			checks.Check(model.Update(inputs.front(), output).ok(), "benchmark Down");
			bool ok = true;
			// 分组计时，输入、检查、JSON 和 hash 都在计时区间之外。
			const double moveTime = Measure([&]
			{
				for (size_t i = 1; i + 1 < inputs.size(); ++i) ok &= model.Update(inputs[i], output).ok();
			});
			checks.Check(ok, "benchmark Move");
			const double predictTime = Measure([&]
			{
				for (auto& values : predictions) ok &= model.Predict(values).ok();
			});
			checks.Check(ok, "benchmark Predict");
			const double upTime = Measure([&] { ok &= model.Update(inputs.back(), output).ok(); });
			checks.Check(ok, "benchmark Up");
			const double resetTime = Measure([&]
			{
				for (int repeat = 0; repeat < 64; ++repeat) ok &= model.Reset().ok();
			});
			checks.Check(ok, "benchmark Reset");
			if (batch < 0) continue;
			result["move_ns_per_call"].append(moveTime / static_cast<double>(inputs.size() - 2));
			result["predict_ns_per_call"].append(predictTime / predictions.size());
			result["up_ns_per_call"].append(upTime);
			result["reset_ns_per_call"].append(resetTime / 64.0);
			Hash(hash, output.size());
			for (const auto& value : output) Hash(hash, value);
			for (const auto& values : predictions)
			{
				Hash(hash, values.size());
				for (const auto& value : values) Hash(hash, value);
			}
		}
		result["workload_hash"] = Json::UInt64(hash);
		result["warmup_batches"] = 8;
		result["measured_batches"] = 64;
		result["move_calls"] = Json::UInt64(64 * (inputs.size() - 2));
		result["predict_calls"] = 64 * 64;
		result["up_calls"] = 64;
		result["reset_calls"] = 64 * 64;
		return result;
	}
}

int RunModelerReplay(const wchar_t* reportPath)
{
	Checks checks;
	Json::Value report;
	report["schema"] = "modeler-baseline-replay-v1";
	report["abseil_version"] = "20250512.1";
	report["abi"] = "C++20/MT/NDEBUG/iterator0/std_string_view/std_ordering/lts_20250512";
	for (bool kalman : {false, true})
	{
		for (const char* shape : {"straight", "arc", "curvature_instant_up", "single_point", "repeated_position",
			"stationary_resume", "long_stroke"})
			report["cases"].append(StrokeCase(checks, shape, kalman));
		report["cases"].append(RestoreCase(checks, kalman));
	}
	report["cases"].append(ErrorCases(checks));
	report["cases"].append(BudgetCase(checks));
	auto disabled = Parameters(false);
	disabled.prediction_params = DisabledPredictorParams{};
	Replay noPrediction(checks, "disabled_predictor", disabled);
	noPrediction.Update(Point(Input::EventType::kDown, 1, 2, 0));
	noPrediction.Predict(Code::kFailedPrecondition);
	report["cases"].append(noPrediction.report);
	report["assertions"] = Json::UInt64(checks.count);
	report["failures"] = checks.failures;
	if (!Write(reportPath, report)) { std::cerr << "Cannot write replay report.\n"; return 2; }
	std::cout << "Modeler replay: " << report["cases"].size() << " cases, " << checks.count << " checks, "
		<< checks.failures.size() << " failures.\n";
	return checks.failures.empty() ? 0 : 1;
}

int RunModelerBenchmark(const wchar_t* reportPath)
{
	Checks checks;
	Json::Value report;
	report["schema"] = "modeler-baseline-benchmark-v1";
	report["abseil_version"] = "20250512.1";
	for (bool kalman : {false, true}) report["cases"].append(BenchmarkCase(checks, kalman));
	report["failures"] = checks.failures;
	if (!Write(reportPath, report)) { std::cerr << "Cannot write benchmark report.\n"; return 2; }
	return checks.failures.empty() ? 0 : 1;
}
