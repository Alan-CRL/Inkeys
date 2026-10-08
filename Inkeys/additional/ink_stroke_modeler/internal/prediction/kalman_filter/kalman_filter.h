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

#ifndef INK_STROKE_MODELER_INTERNAL_PREDICTION_KALMAN_FILTER_KALMAN_FILTER_H_
#define INK_STROKE_MODELER_INTERNAL_PREDICTION_KALMAN_FILTER_KALMAN_FILTER_H_

#include "ink_stroke_modeler/internal/prediction/kalman_filter/matrix.h"

namespace ink {
namespace stroke_model {

// Generates a state estimation based upon observations which can then be used
// to compute predicted values.
class KalmanFilter {
 public:
  KalmanFilter(const Matrix4& state_transition,
               const Matrix4& process_noise_covariance,
               const Vec4& measurement_vector,
               double measurement_noise_variance, int min_stable_iteration);

  // Get the estimation of current state.
  const Vec4& GetStateEstimation() const { return state_estimation_; }

  // Will return true only if the Kalman filter has seen enough data and is
  // considered as stable.
  bool Stable() const { return iter_num_ >= min_stable_iteration_; }

  // Update the observation of the system.
  void Update(double observation);

  void Reset();

  // Returns the number of times Update() has been called since the last time
  // the KalmanFilter was reset.
  int NumIterations() const { return iter_num_; }

 private:
  void Predict();

  // Estimate of the latent state
  // Symbol: X
  // Dimension: state_vector_dim_
  Vec4 state_estimation_;

  // The covariance of the difference between prior predicted latent
  // state and posterior estimated latent state (the so-called "innovation".
  // Symbol: P
  Matrix4 error_covariance_matrix_;

  // For position, state transition matrix is derived from basic physics:
  // new_x = x + v * dt + 1/2 * a * dt^2 + 1/6 * jerk * dt^3
  // new_v = v + a * dt + 1/2 * jerk * dt^2
  // ...
  // Matrix that transmit current state to next state
  // Symbol: F
  Matrix4 state_transition_matrix_;

  // Process_noise_covariance_matrix_ is a time-varying parameter that will be
  // estimated as part of the Kalman filter process.
  // Symbol: Q
  Matrix4 process_noise_covariance_matrix_;

  // Vector to transform estimate to measurement.
  // Symbol: H
  Vec4 measurement_vector_{0, 0, 0, 0};

  // measurement_noise_ is a time-varying parameter that will be estimated as
  // part of the Kalman filter process.
  // Symbol: R
  double measurement_noise_variance_;

  // The first iteration at which the Kalman filter is considered stable enough
  // to make a good estimate of the state.
  int min_stable_iteration_;

  // Tracks the number of update iterations that have occurred.
  int iter_num_;
};

}  // namespace stroke_model
}  // namespace ink

#endif  // INK_STROKE_MODELER_INTERNAL_PREDICTION_KALMAN_FILTER_KALMAN_FILTER_H_
