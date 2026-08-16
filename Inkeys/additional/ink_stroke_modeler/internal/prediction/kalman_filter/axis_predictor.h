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

#ifndef INK_STROKE_MODELER_INTERNAL_PREDICTION_KALMAN_FILTER_AXIS_PREDICTOR_H_
#define INK_STROKE_MODELER_INTERNAL_PREDICTION_KALMAN_FILTER_AXIS_PREDICTOR_H_

#include <memory>

#include "ink_stroke_modeler/internal/prediction/kalman_filter/kalman_filter.h"

namespace ink {
namespace stroke_model {

// Class to predict on axis.
//
// This predictor use one instance of Kalman filter to predict one dimension of
// stylus movement.
class AxisPredictor {
 public:
  AxisPredictor(double process_noise, double measurement_noise,
                int min_stable_iteration);

  // Return true if the underlying Kalman filter is stable.
  bool Stable() const;

  // Reset the underlying Kalman filter.
  void Reset();

  // Update the predictor with a new observation.
  void Update(double observation);

  // Returns the number of times Update() has been called since the last time
  // the AxisPredictor was reset.
  int NumIterations() const;

  // Get the predicted values from the underlying Kalman filter.
  double GetPosition() const;
  double GetVelocity() const;
  double GetAcceleration() const;
  double GetJerk() const;

 private:
  KalmanFilter kalman_filter_;
};

}  // namespace stroke_model
}  // namespace ink

#endif  // INK_STROKE_MODELER_INTERNAL_PREDICTION_KALMAN_FILTER_AXIS_PREDICTOR_H_
