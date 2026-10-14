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

#include "ink_stroke_modeler/internal/prediction/kalman_filter/kalman_filter.h"

#include "ink_stroke_modeler/internal/prediction/kalman_filter/matrix.h"

namespace ink {
namespace stroke_model {

KalmanFilter::KalmanFilter(const Matrix4& state_transition,
                           const Matrix4& process_noise_covariance,
                           const Vec4& measurement_vector,
                           double measurement_noise_variance,
                           int min_stable_iteration)
    : state_transition_matrix_(state_transition),
      process_noise_covariance_matrix_(process_noise_covariance),
      measurement_vector_(measurement_vector),
      measurement_noise_variance_(measurement_noise_variance),
      min_stable_iteration_(min_stable_iteration),
      iter_num_(0) {}

void KalmanFilter::Predict() {
  // X = F * X
  state_estimation_ = state_transition_matrix_ * state_estimation_;
  // P = F * P * F' + Q
  error_covariance_matrix_ = state_transition_matrix_ *
                                 error_covariance_matrix_ *
                                 state_transition_matrix_.Transpose() +
                             process_noise_covariance_matrix_;
}

void KalmanFilter::Update(double observation) {
  if (iter_num_++ == 0) {
    // We only update the state estimation in the first iteration.
    state_estimation_[0] = observation;
    return;
  }
  Predict();
  // Y = z - H * X
  double y = observation - DotProduct(measurement_vector_, state_estimation_);
  // S = H * P * H' + R
  double S = DotProduct(measurement_vector_ * error_covariance_matrix_,
                        measurement_vector_) +
             measurement_noise_variance_;
  // K = P * H' * inv(S)
  Vec4 kalman_gain = measurement_vector_ * error_covariance_matrix_ / S;

  // X = X + K * Y
  state_estimation_ = state_estimation_ + kalman_gain * y;

  // I_HK = eye(P) - K * H
  Matrix4 I_KH = Matrix4() - OuterProduct(kalman_gain, measurement_vector_);

  // P = I_KH * P * I_KH' + K * R * K'
  error_covariance_matrix_ =
      I_KH * error_covariance_matrix_ * I_KH.Transpose() +
      OuterProduct(kalman_gain, kalman_gain) * measurement_noise_variance_;
}

void KalmanFilter::Reset() {
  state_estimation_ = {0, 0, 0, 0};
  error_covariance_matrix_ = Matrix4();  // identity
  iter_num_ = 0;
}

}  // namespace stroke_model
}  // namespace ink
