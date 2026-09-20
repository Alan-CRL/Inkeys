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

#ifndef INK_STROKE_MODELER_INTERNAL_TYPE_MATCHERS_H_
#define INK_STROKE_MODELER_INTERNAL_TYPE_MATCHERS_H_

#include "gtest/gtest.h"
#include "ink_stroke_modeler/internal/internal_types.h"
#include "ink_stroke_modeler/types.h"

namespace ink {
namespace stroke_model {

// These matchers compare Vec2s component-wise, delegating to
// ::testing::FloatEq() and ::testing::FloatNear(), respectively.
::testing::Matcher<Vec2> Vec2Eq(const Vec2& expected);
::testing::Matcher<Vec2> Vec2Near(const Vec2& expected, float tolerance);

// This compares Time, delegating to ::testing::DoubleNear().
::testing::Matcher<Time> TimeNear(const Time& expected, float tolerance);

// These convenience matchers perform comparisons using ::testing::FloatNear(),
// TimeNear(), and Vec2Near().
::testing::Matcher<TipState> TipStateNear(const TipState& expected,
                                          float tolerance);
::testing::Matcher<StylusState> StylusStateNear(const StylusState& expected,
                                                float tolerance);

// This compares Result component-wise, delegating to ::testing::FloatNear(),
// TimeNear(), and Vec2Near() using a separate tolerance for acceleration,
// because the values for acceleration tend to be several orders of magnitude
// larger than the other fields.
::testing::Matcher<Result> ResultNear(const Result& expected, float tolerance,
                                      float acceleration_tolerance);

}  // namespace stroke_model
}  // namespace ink

#endif  // INK_STROKE_MODELER_INTERNAL_TYPE_MATCHERS_H_
