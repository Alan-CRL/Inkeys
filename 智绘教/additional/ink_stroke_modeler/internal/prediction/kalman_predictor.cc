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

#include "ink_stroke_modeler/internal/prediction/kalman_predictor.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <optional>
#include <vector>

#include "ink_stroke_modeler/internal/internal_types.h"
#include "ink_stroke_modeler/internal/utils.h"
#include "ink_stroke_modeler/params.h"
#include "ink_stroke_modeler/types.h"

namespace ink {
namespace stroke_model {
namespace {

KalmanPredictor::State EvaluateCubic(const KalmanPredictor::State &start_state,
                                     Duration delta_time) {
  float dt = delta_time.Value();
  auto dt_squared = dt * dt;
  auto dt_cubed = dt_squared * dt;

  KalmanPredictor::State end_state;
  end_state.position = start_state.position + start_state.velocity * dt +
                       start_state.acceleration * dt_squared / 2.f +
                       start_state.jerk * dt_cubed / 6.f;
  end_state.velocity = start_state.velocity + start_state.acceleration * dt +
                       start_state.jerk * dt_squared / 2.f;
  end_state.acceleration = start_state.acceleration + start_state.jerk * dt;
  end_state.jerk = start_state.jerk;

  return end_state;
}

}  // namespace

void KalmanPredictor::Reset() {
  x_predictor_.Reset();
  y_predictor_.Reset();
  sample_times_.clear();
  last_position_received_ = std::nullopt;
}

void KalmanPredictor::Update(Vec2 position, Time time) {
  last_position_received_ = position;
  sample_times_.push_back(time);
  if (predictor_params_.max_time_samples < 0 ||
      sample_times_.size() >
          static_cast<size_t>(predictor_params_.max_time_samples)) {
    sample_times_.pop_front();
  }

  x_predictor_.Update(position.x);
  y_predictor_.Update(position.y);
}

std::optional<KalmanPredictor::State> KalmanPredictor::GetEstimatedState()
    const {
  if (!IsStable() || sample_times_.empty()) return std::nullopt;

  State estimated_state;
  estimated_state.position = {static_cast<float>(x_predictor_.GetPosition()),
                              static_cast<float>(y_predictor_.GetPosition())};
  estimated_state.velocity = {static_cast<float>(x_predictor_.GetVelocity()),
                              static_cast<float>(y_predictor_.GetVelocity())};
  estimated_state.acceleration = {
      static_cast<float>(x_predictor_.GetAcceleration()),
      static_cast<float>(y_predictor_.GetAcceleration())};
  estimated_state.jerk = {static_cast<float>(x_predictor_.GetJerk()),
                          static_cast<float>(y_predictor_.GetJerk())};

  // The axis predictors are not time-aware, assuming that the time delta
  // between measurements is always 1. To correct for this, we divide the
  // velocity, acceleration, and jerk by the average observed time delta, raised
  // to the appropriate power.
  auto dt = static_cast<float>(
                (sample_times_.back() - sample_times_.front()).Value()) /
            sample_times_.size();
  auto dt_squared = dt * dt;
  auto dt_cubed = dt_squared * dt;
  estimated_state.velocity /= dt;
  estimated_state.acceleration /= dt_squared;
  estimated_state.jerk /= dt_cubed;

  // We want our predictions to tend more towards linearity -- to achieve this,
  // we reduce the acceleration and jerk.
  estimated_state.acceleration *= predictor_params_.acceleration_weight;
  estimated_state.jerk *= predictor_params_.jerk_weight;

  return estimated_state;
}

void KalmanPredictor::ConstructPrediction(
    const TipState &last_state, std::vector<TipState> &prediction) const {
  prediction.clear();
  auto estimated_state = GetEstimatedState();
  if (!estimated_state || !last_position_received_) {
    // We don't yet have enough data to construct a prediction.
    return;
  }

  Duration sample_dt{1. / sampling_params_.min_output_rate};
  ConstructCubicConnector(last_state, *estimated_state, predictor_params_,
                          sample_dt, &prediction);
  auto start_time =
      prediction.empty() ? last_state.time : prediction.back().time;
  ConstructCubicPrediction(*estimated_state, predictor_params_, start_time,
                           sample_dt, NumberOfPointsToPredict(*estimated_state),
                           &prediction);
}

void KalmanPredictor::ConstructCubicPrediction(
    const State &estimated_state, const KalmanPredictorParams &params,
    Time start_time, Duration sample_dt, int n_samples,
    std::vector<TipState> *output) {
  auto current_state = estimated_state;
  auto current_time = start_time;
  for (int i = 0; i < n_samples; ++i) {
    auto next_state = EvaluateCubic(current_state, sample_dt);
    current_time += sample_dt;
    output->push_back({
        .position = next_state.position,
        .velocity = next_state.velocity,
        .acceleration = next_state.acceleration,
        .time = current_time,
    });
    current_state = next_state;
  }
}

void KalmanPredictor::ConstructCubicConnector(
    const TipState &last_tip_state, const State &estimated_state,
    const KalmanPredictorParams &params, Duration sample_dt,
    std::vector<TipState> *output) {
  // Estimate how long it will take for the tip to travel from its last position
  // to the estimated position, based on the start and end velocities. We define
  // a minimum "reasonable" velocity to avoid division by zero.
  float distance_traveled =
      Distance(last_tip_state.position, estimated_state.position);
  float max_velocity_at_ends = std::max(last_tip_state.velocity.Magnitude(),
                                        estimated_state.velocity.Magnitude());
  Duration target_duration{
      distance_traveled /
      std::max(max_velocity_at_ends, params.min_catchup_velocity)};

  // Determine how many samples this will give us, ensuring that there's always
  // at least one. Then, pick a duration that's a multiple of the sample dt.
  int n_points = std::fmax(std::ceil(static_cast<float>(
                               target_duration.Value() / sample_dt.Value())),
                           1.f);
  Duration duration = n_points * sample_dt;

  // We want to construct a cubic curve connecting the last tip state and the
  // estimated state. Given positions p₀ and p₁, velocities v₀ and v₁, and times
  // t₀ and t₁ at the start and end of the curve, we define a pair of functions,
  // f and g, such that the curve is described by the composite function
  // f(g(t)):
  //   f(x) = ax³ + bx² + cx + d
  //   g(t) = (t - t₀) / (t₁ - t₀)
  // We then find the derivatives:
  //   f'(x) = 3ax² + 2bx + c
  //   g'(t) = 1 / (t₁ - t₀)
  //   (f∘g)'(t) = f'(g(t)) ⋅ g'(t) = (3ax² + 2bx + c) / (t₁ - t₀)
  // We then plug in the given values:
  //   f(g(t₀)) = f(0) = p₀
  //   ax³ + bx² + cx + d
  //   f(g(t₁)) = f(1) = p₁
  //   (f∘g)'(t₀) = f'(0) ⋅ g'(t₀) = v₀
  //   (f∘g)'(t₁) = f'(1) ⋅ g'(t₁) = v₁
  // This gives us four linear equations:
  //   a⋅0³ + b⋅0² + c⋅0 + d = p₀
  //   a⋅1³ + b⋅1² + c⋅1 + d = p₁
  //   (3a⋅0² + 2b⋅0 + c) / (t₁ - t₀) = v₀
  //   (3a⋅1² + 2b⋅1 + c) / (t₁ - t₀) = v₁
  // Finally, we can solve for a, b, c, and d:
  //   a = 2p₀ - 2p₁ + (v₀ + v₁)(t₁ - t₀)
  //   b = -3p₀ + 3p₁ - (2v₀ + v₁)(t₁ - t₀)
  //   c = v₀(t₁ - t₀)
  //   d = p₀
  // Note that for now, we do *not* smoothly connect the acceleration vector of
  // the last tip state to the estimated state--only the position and velocity.
  // That means there can be a discontinuities in the acceleration at the start
  // and end of this curve (i.e. momentarily infinite jerk). If this turns out
  // to be a problem later, we may need to revise the above.
  float float_duration = duration.Value();
  Vec2 a =
      2.f * last_tip_state.position - 2.f * estimated_state.position +
      (last_tip_state.velocity + estimated_state.velocity) * float_duration;
  Vec2 b = -3.f * last_tip_state.position + 3.f * estimated_state.position -
           (2.f * last_tip_state.velocity + estimated_state.velocity) *
               float_duration;
  Vec2 c = last_tip_state.velocity * float_duration;
  Vec2 d = last_tip_state.position;

  output->reserve(output->size() + n_points);
  for (int i = 1; i <= n_points; ++i) {
    float t = static_cast<float>(i) / n_points;
    float t_squared = t * t;
    float t_cubed = t_squared * t;
    Vec2 position = a * t_cubed + b * t_squared + c * t + d;
    Vec2 velocity = 3.f * a * t_squared + 2.f * b * t + c;
    Vec2 acceleration = 6.f * a * t + 2.f * b;
    Time time = last_tip_state.time + duration * t;
    output->push_back({
        .position = position,
        .velocity = velocity / float_duration,
        .acceleration = acceleration / (float_duration * float_duration),
        .time = time,
    });
  }
}

int KalmanPredictor::NumberOfPointsToPredict(
    const State &estimated_state) const {
  const KalmanPredictorParams::ConfidenceParams &confidence_params =
      predictor_params_.confidence_params;

  auto target_number =
      static_cast<float>(predictor_params_.prediction_interval.Value() *
                         sampling_params_.min_output_rate);

  // The more samples we've received, the less effect the noise from each
  // individual input affects the result.
  float sample_ratio =
      std::min(1.f, static_cast<float>(x_predictor_.NumIterations()) /
                        confidence_params.desired_number_of_samples);

  // The further the last given position is from the estimated position, the
  // less confidence we have in the result.
  float estimated_error =
      Distance(*last_position_received_, estimated_state.position);
  float normalized_error =
      1.f - Normalize01(0.f, confidence_params.max_estimation_distance,
                        estimated_error);

  // This is the state that the prediction would end at if we predicted the full
  // interval (i.e. if confidence == 1).
  auto end_state =
      EvaluateCubic(estimated_state, predictor_params_.prediction_interval);

  // If the prediction is not traveling quickly, then changes in direction
  // become more apparent, making the prediction appear wobbly.
  float travel_speed =
      Distance(estimated_state.position, end_state.position) /
      static_cast<float>(predictor_params_.prediction_interval.Value());
  float normalized_distance =
      Normalize01(confidence_params.min_travel_speed,
                  confidence_params.max_travel_speed, travel_speed);

  // If the actual prediction differs too much from the linear prediction, it
  // suggests that the acceleration and jerk components overtake the velocity,
  // resulting in a prediction that flies far off from the stroke.
  float deviation_from_linear_prediction = Distance(
      end_state.position,
      estimated_state.position +
          static_cast<float>(predictor_params_.prediction_interval.Value()) *
              estimated_state.velocity);
  float linearity =
      Interp(confidence_params.baseline_linearity_confidence, 1.f,
             1.f - Normalize01(0.f, confidence_params.max_linear_deviation,
                               deviation_from_linear_prediction));

  auto confidence =
      sample_ratio * normalized_error * normalized_distance * linearity;
  return std::fmax(0.f, std::ceil(target_number * confidence));
}

}  // namespace stroke_model
}  // namespace ink
