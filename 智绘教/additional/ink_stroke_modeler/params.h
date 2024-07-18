/*
 * Copyright 2022 Google LLC
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef INK_STROKE_MODELER_PARAMS_H_
#define INK_STROKE_MODELER_PARAMS_H_

#include <cmath>
#include <variant>

#include "absl/status/status.h"
#include "ink_stroke_modeler/types.h"

namespace ink {
namespace stroke_model {

// These structs contain parameters for tuning the behavior of the stroke
// modeler.
//
// The stroke modeler is unit-agnostic, in both time and space. That is, the
// stroke modeler does not know or care whether the inputs and parameters are
// specified in feet and minutes, meters and seconds, or millimeters and years.
// As such, instead of referring to specific units, we refer to "unit distance"
// and "unit time".
//
// These parameters will need to be "tuned" to your use case. Because of this,
// and because of the modeler's unit-agnosticism, it's impossible to define
// "reasonable" default values for many of the parameters -- these parameters
// instead default to -1, which will cause the validation functions to return an
// error.
//
// Where possible, we've indicated what a good starting point for tuning might
// be, but you'll likely need to adjust these for best results.

// These parameters are used for modeling the position of the pen.
struct PositionModelerParams {
  // The mass of the "weight" being pulled along the path, multiplied by the
  // spring constant.
  float spring_mass_constant = 11.f / 32400;

  // The ratio of the pen's velocity that is subtracted from the pen's
  // acceleration per unit time, to simulate drag.
  float drag_constant = 72.f;
};

// These parameters are used for sampling.
struct SamplingParams {
  // The minimum number of modeled inputs to output per unit time. If inputs are
  // received at a lower rate, they will be upsampled to produce output of at
  // least min_output_rate. If inputs are received at a higher rate, the
  // output rate will match the input rate.
  double min_output_rate = -1;

  // This determines stop condition for end-of-stroke modeling; if the position
  // is within this distance of the final raw input, or if the last update
  // iteration moved less than this distance, it stop iterating.
  //
  // This should be a small distance; a good starting point is 2-3 orders of
  // magnitude smaller than the expected distance between input points.
  float end_of_stroke_stopping_distance = -1;

  // The maximum number of iterations to perform at the end of the stroke, if it
  // does not stop due to the constraints of end_of_stroke_stopping_distance.
  int end_of_stroke_max_iterations = 20;

  // Maximum number of outputs to generate per call to Update or Predict.
  // This limit avoids crashes if input events are received with too long of
  // a time between, possibly because a client was suspended and resumed.
  int max_outputs_per_call = 100000;

  // Max absolute value of estimated angle to traverse in a single upsampled
  // input step in radians (0, PI). The traversed angle is estimated by
  // considering the change in the angle of the tip state that would happen due
  // to the input without any upsampling. If set to -1 (the default), input is
  // not upsampled for this reason.
  double max_estimated_angle_to_traverse_per_input = -1;
};

// These parameters are used modeling the state of the stylus once the position
// has been modeled.
struct StylusStateModelerParams {
  // The maximum number of raw inputs to look at when finding the nearest states
  // for interpolation.
  int max_input_samples = 10;
};

// These parameters are used for applying smoothing to the input to reduce
// wobble in the prediction.
struct WobbleSmootherParams {
  // The length of the window over which the moving average of speed and
  // position are calculated.
  //
  // A good starting point is 2.5 divided by the expected number of inputs per
  // unit time.
  Duration timeout{-1};

  // The range of speeds considered for wobble smoothing. At speed_floor, the
  // maximum amount of smoothing is applied. At speed_ceiling, no smoothing is
  // applied.
  //
  // Good starting points are 2% and 3% of the expected speed of the inputs.
  float speed_floor = -1;
  float speed_ceiling = -1;
};

// This struct indicates the "stroke end" prediction strategy should be used,
// which models a prediction as though the last seen input was the
// end-of-stroke. There aren't actually any tunable parameters for this; it uses
// the same PositionModelerParams and SamplingParams as the overall model. Note
// that this "prediction" doesn't actually predict substantially into the
// future, it only allows for very quickly "catching up" to the position of the
// raw input.
struct StrokeEndPredictorParams {};

// This struct indicates that the Kalman filter-based prediction strategy should
// be used, and provides the parameters for tuning it.
//
// Unlike the "stroke end" predictor, this strategy can predict an extension
// of the stroke beyond the last Input position, in addition to the "catch up"
// step.
struct KalmanPredictorParams {
  // The variance of the noise inherent to the stroke itself.
  double process_noise = -1;

  // The variance of the noise that rises from errors in measurement of the
  // stroke.
  double measurement_noise = -1;

  // The minimum number of inputs received before the Kalman predictor is
  // considered stable enough to make a prediction.
  int min_stable_iteration = 4;

  // The Kalman filter assumes that input is received in uniform time steps, but
  // this is not always the case. We hold on to the most recent input timestamps
  // for use in calculating the correction for this. This determines the maximum
  // number of timestamps to save.
  int max_time_samples = 20;

  // The minimum allowed velocity of the "catch up" portion of the prediction,
  // which covers the distance between the last Result (the last corrected
  // position) and the
  //
  // A good starting point is 3 orders of magnitude smaller than the expected
  // speed of the inputs.
  float min_catchup_velocity = -1;

  // These weights are applied to the acceleration (x²) and jerk (x³) terms of
  // the cubic prediction polynomial. The closer they are to zero, the more
  // linear the prediction will be.
  float acceleration_weight = .5;
  float jerk_weight = .1;

  // This value is a hint to the predictor, indicating the desired duration of
  // of the portion of the prediction extending beyond the position of the last
  // input. The actual duration of that portion of the prediction may be less
  // than this, based on the predictor's confidence, but it will never be
  // greater.
  Duration prediction_interval{-1};

  // The Kalman predictor uses several heuristics to evaluate confidence in the
  // prediction. Each heuristic produces a confidence value between 0 and 1, and
  // then we take their product as the total confidence.
  // These parameters may be used to tune those heuristics.
  struct ConfidenceParams {
    // The first heuristic simply increases confidence as we receive more sample
    // (i.e. input points). It evaluates to 0 at no samples, and 1 at
    // desired_number_of_samples.
    int desired_number_of_samples = 20;

    // The second heuristic is based on the distance between the last sample
    // and the current estimate. If the distance is 0, it evaluates to 1, and if
    // the distance is greater than or equal to max_estimation_distance, it
    // evaluates to 0.
    //
    // A good starting point is 1.5 times measurement_noise.
    float max_estimation_distance = -1;

    // The third heuristic is based on the speed of the prediction, which is
    // approximated by measuring the from the start of the prediction to the
    // projected endpoint (if it were extended for the full
    // prediction_interval). It evaluates to 0 at min_travel_speed, and 1
    // at max_travel_speed.
    //
    // Good starting points are 5% and 25% of the expected speed of the inputs.
    float min_travel_speed = -1;
    float max_travel_speed = -1;

    // The fourth heuristic is based on the linearity of the prediction, which
    // is approximated by comparing the endpoint of the prediction with the
    // endpoint of a linear prediction (again, extended for the full
    // prediction_interval). It evaluates to 1 at zero distance, and
    // baseline_linearity_confidence at a distance of max_linear_deviation.
    //
    // A good starting point is an 10 times the measurement_noise.
    float max_linear_deviation = -1;
    float baseline_linearity_confidence = .4;
  };
  ConfidenceParams confidence_params;
};

// Type used to indicate that no prediction strategy should be used. Attempting
// to construct a prediction in combination with this setting results in error.
struct DisabledPredictorParams {};

using PredictionParams =
    std::variant<StrokeEndPredictorParams, KalmanPredictorParams,
                 DisabledPredictorParams>;

// Temporary params governing experimental changes in behavior. Any params
// here may be removed without warning in a future release.
struct ExperimentalParams {};

// This convenience struct is a collection of the parameters for the individual
// parameter structs.
struct StrokeModelParams {
  WobbleSmootherParams wobble_smoother_params;
  PositionModelerParams position_modeler_params;
  SamplingParams sampling_params;
  StylusStateModelerParams stylus_state_modeler_params;
  PredictionParams prediction_params = StrokeEndPredictorParams{};
  ExperimentalParams experimental_params;
};

// These validation functions will return an error if the given parameters are
// invalid.
absl::Status ValidatePositionModelerParams(const PositionModelerParams& params);
absl::Status ValidateSamplingParams(const SamplingParams& params);
absl::Status ValidateStylusStateModelerParams(
    const StylusStateModelerParams& params);
absl::Status ValidateWobbleSmootherParams(const WobbleSmootherParams& params);
absl::Status ValidatePredictionParams(const PredictionParams& params);
absl::Status ValidateStrokeModelParams(const StrokeModelParams& params);

}  // namespace stroke_model
}  // namespace ink

#endif  // INK_STROKE_MODELER_PARAMS_H_
