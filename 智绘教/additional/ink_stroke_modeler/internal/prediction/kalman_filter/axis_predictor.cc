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

#include "ink_stroke_modeler/internal/prediction/kalman_filter/axis_predictor.h"

#include <memory>

#include "ink_stroke_modeler/internal/prediction/kalman_filter/kalman_filter.h"
#include "ink_stroke_modeler/internal/prediction/kalman_filter/matrix.h"

namespace ink {
namespace stroke_model {
namespace {

constexpr int kPositionIndex = 0;
constexpr int kVelocityIndex = 1;
constexpr int kAccelerationIndex = 2;
constexpr int kJerkIndex = 3;

constexpr double kDt = 1.0;
constexpr double kDtSquared = kDt * kDt;
constexpr double kDtCubed = kDt * kDt * kDt;

KalmanFilter MakeKalmanFilter(double process_noise, double measurement_noise,
                              int min_stable_iteration) {
  // State translation matrix is basic physics.
  // new_pos = pre_pos + v * dt + 1/2 * a * dt^2 + 1/6 * J * dt^3.
  // new_v = v + a * dt + 1/2 * J * dt^2.
  // new_a = a + J * dt.
  // new_j = J.
  Matrix4 state_transition(1, kDt, .5 * kDtSquared, 1.0 / 6 * kDtCubed,  //
                           0, 1, kDt, .5 * kDtSquared,                   //
                           0, 0, 1, kDt,                                 //
                           0, 0, 0, 1);
  // We model the system noise as noisy force on the pen.
  // The following matrix describes the impact of that noise on each state.
  Vec4 process_noise_vector(1.0 / 6 * kDtCubed, 0.5 * kDtSquared, kDt, 1.0);
  Matrix4 process_noise_covariance =
      OuterProduct(process_noise_vector, process_noise_vector) * process_noise;

  // Sensor only detects location. Thus measurement only impact the position.
  Vec4 measurement_vector(1.0, 0.0, 0.0, 0.0);

  return KalmanFilter(state_transition, process_noise_covariance,
                      measurement_vector, measurement_noise,
                      min_stable_iteration);
}

}  // namespace

AxisPredictor::AxisPredictor(double process_noise, double measurement_noise,
                             int min_stable_iteration)
    : kalman_filter_(MakeKalmanFilter(process_noise, measurement_noise,
                                      min_stable_iteration)) {}

bool AxisPredictor::Stable() const { return kalman_filter_.Stable(); }

void AxisPredictor::Reset() { kalman_filter_.Reset(); }

void AxisPredictor::Update(double observation) {
  kalman_filter_.Update(observation);
}

int AxisPredictor::NumIterations() const {
  return kalman_filter_.NumIterations();
}

double AxisPredictor::GetPosition() const {
  return kalman_filter_.GetStateEstimation()[kPositionIndex];
}

double AxisPredictor::GetVelocity() const {
  return kalman_filter_.GetStateEstimation()[kVelocityIndex];
}

double AxisPredictor::GetAcceleration() const {
  return kalman_filter_.GetStateEstimation()[kAccelerationIndex];
}

double AxisPredictor::GetJerk() const {
  return kalman_filter_.GetStateEstimation()[kJerkIndex];
}

}  // namespace stroke_model
}  // namespace ink
