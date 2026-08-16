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

#include "ink_stroke_modeler/internal/type_matchers.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "absl/strings/str_cat.h"
#include "ink_stroke_modeler/internal/internal_types.h"
#include "ink_stroke_modeler/types.h"

namespace ink {
namespace stroke_model {
namespace {

using ::testing::AllOf;
using ::testing::DoubleNear;
using ::testing::ExplainMatchResult;
using ::testing::Field;
using ::testing::FloatEq;
using ::testing::FloatNear;
using ::testing::Matcher;
using ::testing::PrintToString;
using ::testing::Property;

MATCHER_P(Vec2EqMatcher, expected,
          absl::StrCat(negation ? "doesn't equal" : "equals",
                       " Vec2 (expected: ", PrintToString(expected), ")")) {
  return ExplainMatchResult(AllOf(Field("x", &Vec2::x, FloatEq(expected.x)),
                                  Field("y", &Vec2::y, FloatEq(expected.y))),
                            arg, result_listener);
}

MATCHER_P2(Vec2NearMatcher, expected, tolerance,
           absl::StrCat(negation ? "doesn't approximately equal"
                                 : "approximately equals",
                        " Vec2 (expected: ", PrintToString(expected),
                        ", tolerance: ", PrintToString(tolerance), ")")) {
  return ExplainMatchResult(
      AllOf(Field("x", &Vec2::x, FloatNear(expected.x, tolerance)),
            Field("y", &Vec2::y, FloatNear(expected.y, tolerance))),
      arg, result_listener);
}

MATCHER_P2(TimeNearMatcher, expected, tolerance,
           absl::StrCat(negation ? "doesn't approximately match"
                                 : "approximately matches",
                        " Time (expected: ", PrintToString(expected),
                        ", tolerance: ", PrintToString(tolerance), ")")) {
  return ExplainMatchResult(
      Property("Value", &Time::Value, DoubleNear(expected.Value(), tolerance)),
      arg, result_listener);
}

MATCHER_P2(TipStateNearMatcher, expected, tolerance,
           absl::StrCat(negation ? "doesn't approximately match"
                                 : "approximately matches",
                        " TipState (expected: ", PrintToString(expected),
                        ", tolerance: ", PrintToString(tolerance), ")")) {
  return ExplainMatchResult(
      AllOf(Field("position", &TipState::position,
                  Vec2Near(expected.position, tolerance)),
            Field("velocity", &TipState::velocity,
                  Vec2Near(expected.velocity, tolerance)),
            Field("acceleration", &TipState::acceleration,
                  Vec2Near(expected.acceleration, tolerance)),
            Field("time", &TipState::time, TimeNear(expected.time, tolerance))),
      arg, result_listener);
}

MATCHER_P2(StylusStateNearMatcher, expected, tolerance,
           absl::StrCat(negation ? "doesn't approximately match"
                                 : "approximately matches",
                        " StylusState (expected: ", PrintToString(expected),
                        ", tolerance: ", PrintToString(tolerance), ")")) {
  return ExplainMatchResult(
      AllOf(Field("pressure", &StylusState::pressure,
                  FloatNear(expected.pressure, tolerance)),
            Field("tilt", &StylusState::tilt,
                  FloatNear(expected.tilt, tolerance)),
            Field("orientation", &StylusState::orientation,
                  FloatNear(expected.orientation, tolerance))),
      arg, result_listener);
}

MATCHER_P3(ResultNearMatcher, expected, tolerance, acceleration_tolerance,
           absl::StrCat(negation ? "doesn't approximately match"
                                 : "approximately matches",
                        " Result (expected: ", PrintToString(expected),
                        ", tolerance: ", PrintToString(tolerance),
                        " acceleration_tolerance: ",
                        PrintToString(acceleration_tolerance), ")")) {
  return ExplainMatchResult(
      AllOf(Field("position", &Result::position,
                  Vec2Near(expected.position, tolerance)),
            Field("velocity", &Result::velocity,
                  Vec2Near(expected.velocity, tolerance)),
            Field("acceleration", &Result::acceleration,
                  Vec2Near(expected.acceleration, acceleration_tolerance)),
            Field("time", &Result::time, TimeNear(expected.time, tolerance)),
            Field("pressure", &Result::pressure,
                  FloatNear(expected.pressure, tolerance)),
            Field("tilt", &Result::tilt, FloatNear(expected.tilt, tolerance)),
            Field("orientation", &Result::orientation,
                  FloatNear(expected.orientation, tolerance))),
      arg, result_listener);
}

}  // namespace

Matcher<Vec2> Vec2Eq(const Vec2& expected) { return Vec2EqMatcher(expected); }

Matcher<Vec2> Vec2Near(const Vec2& expected, float tolerance) {
  return Vec2NearMatcher(expected, tolerance);
}

Matcher<Time> TimeNear(const Time& expected, float tolerance) {
  return TimeNearMatcher(expected, tolerance);
}

Matcher<TipState> TipStateNear(const TipState& expected, float tolerance) {
  return TipStateNearMatcher(expected, tolerance);
}

Matcher<StylusState> StylusStateNear(const StylusState& expected,
                                     float tolerance) {
  return StylusStateNearMatcher(expected, tolerance);
}

Matcher<Result> ResultNear(const Result& expected, float tolerance,
                           float acceleration_tolerance) {
  return ResultNearMatcher(expected, tolerance, acceleration_tolerance);
}

}  // namespace stroke_model
}  // namespace ink
