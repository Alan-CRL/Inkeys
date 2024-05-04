// Copyright 2022 Google LLC
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "ink_stroke_modeler/params.h"

#include <variant>

#include "absl/status/status.h"
#include "absl/strings/string_view.h"
#include "absl/strings/substitute.h"
#include "ink_stroke_modeler/internal/validation.h"
#include "ink_stroke_modeler/numbers.h"

// This convenience macro evaluates the given expression, and if it does not
// return an OK status, returns and propagates the status.
#define RETURN_IF_ERROR(expr)                              \
  do {                                                     \
    if (auto status = (expr); !status.ok()) return status; \
  } while (false)

namespace ink {
namespace stroke_model {
namespace {

// We need to cap the value of end_of_stroke_max_iterations, because later we
// will attempt to allocate an array of that length. The default value is 20,
// and 1000 should be way more than anyone needs.
constexpr int kMaxEndOfStrokeMaxIterations = 1000;

}  // namespace

absl::Status ValidatePositionModelerParams(
    const PositionModelerParams& params) {
  RETURN_IF_ERROR(
      ValidateGreaterThanZero(params.spring_mass_constant,
                              "PositionModelerParams::spring_mass_constant"));
  return ValidateGreaterThanZero(params.drag_constant,
                                 "PositionModelerParams::drag_ratio");
}

absl::Status ValidateSamplingParams(const SamplingParams& params) {
  RETURN_IF_ERROR(ValidateGreaterThanZero(params.min_output_rate,
                                          "SamplingParams::min_output_rate"));
  RETURN_IF_ERROR(ValidateGreaterThanZero(
      params.end_of_stroke_stopping_distance,
      "SamplingParams::end_of_stroke_stopping_distance"));
  RETURN_IF_ERROR(
      ValidateGreaterThanZero(params.end_of_stroke_max_iterations,
                              "SamplingParams::end_of_stroke_max_iterations"));
  if (params.end_of_stroke_max_iterations > kMaxEndOfStrokeMaxIterations) {
    return absl::InvalidArgumentError(absl::Substitute(
        "SamplingParams::end_of_stroke_max_iterations must be "
        "at most $0. Actual value: $1",
        kMaxEndOfStrokeMaxIterations, params.end_of_stroke_max_iterations));
  }
  RETURN_IF_ERROR(ValidateGreaterThanZero(
      params.max_outputs_per_call, "SamplingParams::max_outputs_per_call"));
  if (params.max_estimated_angle_to_traverse_per_input != -1) {
    RETURN_IF_ERROR(ValidateGreaterThanZero(
        params.max_estimated_angle_to_traverse_per_input,
        "SamplingParams::max_estimated_angle_to_traverse_per_input"));
    if (params.max_estimated_angle_to_traverse_per_input >= kPi) {
      return absl::InvalidArgumentError(absl::Substitute(
          "SamplingParams::max_estimated_angle_to_traverse_per_input must be "
          "less than kPi ($0). Actual value: $1",
          kPi, params.max_estimated_angle_to_traverse_per_input));
    }
  }
  return absl::OkStatus();
}

absl::Status ValidateStylusStateModelerParams(
    const StylusStateModelerParams& params) {
  return ValidateGreaterThanZero(params.max_input_samples,
                                 "StylusStateModelerParams::max_input_samples");
}

absl::Status ValidateWobbleSmootherParams(const WobbleSmootherParams& params) {
  RETURN_IF_ERROR(ValidateGreaterThanOrEqualToZero(
      params.timeout.Value(), "WobbleSmootherParams::timeout"));
  RETURN_IF_ERROR(ValidateGreaterThanOrEqualToZero(
      params.speed_floor, "WobbleSmootherParams::speed_floor"));
  RETURN_IF_ERROR(ValidateIsFiniteNumber(
      params.speed_ceiling, "WobbleSmootherParams::speed_ceiling"));
  if (params.speed_ceiling < params.speed_floor) {
    return absl::InvalidArgumentError(absl::Substitute(
        "WobbleSmootherParams::speed_ceiling must be greater than or "
        "equal to WobbleSmootherParams::speed_floor ($0). Actual "
        "value: $1",
        params.speed_floor, params.speed_ceiling));
  }
  return absl::OkStatus();
}

namespace {

absl::Status ValidateKalmanPredictorParams(
    const KalmanPredictorParams& kalman_params) {
  RETURN_IF_ERROR(ValidateGreaterThanZero(
      kalman_params.process_noise, "KalmanPredictorParams::process_noise"));
  RETURN_IF_ERROR(
      ValidateGreaterThanZero(kalman_params.measurement_noise,
                              "KalmanPredictorParams::measurement_noise"));
  RETURN_IF_ERROR(
      ValidateGreaterThanZero(kalman_params.min_stable_iteration,
                              "KalmanPredictorParams::min_stable_iteration"));
  RETURN_IF_ERROR(
      ValidateGreaterThanZero(kalman_params.max_time_samples,
                              "KalmanPredictorParams::max_time_samples"));
  RETURN_IF_ERROR(
      ValidateGreaterThanZero(kalman_params.min_catchup_velocity,
                              "KalmanPredictorParams::min_catchup_velocity"));
  RETURN_IF_ERROR(
      ValidateIsFiniteNumber(kalman_params.acceleration_weight,
                             "KalmanPredictorParams::acceleration_weight"));
  RETURN_IF_ERROR(ValidateIsFiniteNumber(kalman_params.jerk_weight,
                                         "KalmanPredictorParams::jerk_weight"));
  RETURN_IF_ERROR(
      ValidateGreaterThanZero(kalman_params.prediction_interval.Value(),
                              "KalmanPredictorParams::jerk_weight"));

  const KalmanPredictorParams::ConfidenceParams& confidence_params =
      kalman_params.confidence_params;
  RETURN_IF_ERROR(ValidateGreaterThanZero(
      confidence_params.desired_number_of_samples,
      "KalmanPredictorParams::ConfidenceParams::desired_number_of_samples"));
  RETURN_IF_ERROR(ValidateGreaterThanZero(
      confidence_params.max_estimation_distance,
      "KalmanPredictorParams::ConfidenceParams::max_estimation_distance"));
  RETURN_IF_ERROR(ValidateGreaterThanOrEqualToZero(
      confidence_params.min_travel_speed,
      "KalmanPredictorParams::ConfidenceParams::min_travel_speed"));
  RETURN_IF_ERROR(ValidateIsFiniteNumber(
      confidence_params.max_travel_speed,
      "KalmanPredictorParams::ConfidenceParams::max_travel_speed"));
  if (confidence_params.max_travel_speed < confidence_params.min_travel_speed) {
    return absl::InvalidArgumentError(
        absl::Substitute("KalmanPredictorParams::ConfidenceParams::max_"
                         "travel_speed must be greater than or equal to "
                         "KalmanPredictorParams::ConfidenceParams::min_"
                         "travel_speed ($0). Actual value: $1",
                         confidence_params.min_travel_speed,
                         confidence_params.max_travel_speed));
  }
  RETURN_IF_ERROR(ValidateGreaterThanZero(
      confidence_params.max_linear_deviation,
      "KalmanPredictorParams::ConfidenceParams::max_linear_deviation"));
  if (confidence_params.baseline_linearity_confidence < 0 ||
      confidence_params.baseline_linearity_confidence > 1) {
    return absl::InvalidArgumentError(absl::Substitute(
        "KalmanPredictorParams::ConfidenceParams::baseline_linearity_"
        "confidence must lie in the interval [0, 1]. Actual value: $0",
        confidence_params.baseline_linearity_confidence));
  }
  return absl::OkStatus();
}

}  // namespace

absl::Status ValidatePredictionParams(const PredictionParams& params) {
  if (const auto* kalman_params = std::get_if<KalmanPredictorParams>(&params)) {
    return ValidateKalmanPredictorParams(*kalman_params);
  }

  // StrokeEndPredictorParams and DisabledPredictorParams have nothing to
  // validate.
  return absl::OkStatus();
}

absl::Status ValidateStrokeModelParams(const StrokeModelParams& params) {
  RETURN_IF_ERROR(ValidateWobbleSmootherParams(params.wobble_smoother_params));
  RETURN_IF_ERROR(
      ValidatePositionModelerParams(params.position_modeler_params));
  RETURN_IF_ERROR(ValidateSamplingParams(params.sampling_params));
  RETURN_IF_ERROR(
      ValidateStylusStateModelerParams(params.stylus_state_modeler_params));
  return ValidatePredictionParams(params.prediction_params);
}

}  // namespace stroke_model
}  // namespace ink

#undef RETURN_IF_ERROR
