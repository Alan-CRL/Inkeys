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

#ifndef INK_STROKE_MODELER_TYPES_H_
#define INK_STROKE_MODELER_TYPES_H_

#include <cmath>
#include <ostream>
#include <string>

#include "absl/status/status.h"
#include "absl/status/statusor.h"

namespace ink {
namespace stroke_model {

// A vector (or point) in 2D space.
struct Vec2 {
  float x = 0;
  float y = 0;

  // The length of the vector, i.e. its distance from the origin.
  float Magnitude() const { return std::hypot(x, y); }

  // The difference in angle between the vector and another vector in radians
  // [0, pi]. Returns an error status if either of the inputs is non-finite.
  absl::StatusOr<float> AbsoluteAngleTo(Vec2 other) const;

  bool IsFinite() const { return std::isfinite(x) && std::isfinite(y); }
};

bool operator==(Vec2 lhs, Vec2 rhs);
bool operator!=(Vec2 lhs, Vec2 rhs);

Vec2 operator+(Vec2 lhs, Vec2 rhs);
Vec2 operator-(Vec2 lhs, Vec2 rhs);
Vec2 operator*(float scalar, Vec2 v);
Vec2 operator*(Vec2 v, float scalar);
Vec2 operator/(Vec2 v, float scalar);

Vec2 &operator+=(Vec2 &lhs, Vec2 rhs);
Vec2 &operator-=(Vec2 &lhs, Vec2 rhs);
Vec2 &operator*=(Vec2 &lhs, float scalar);
Vec2 &operator/=(Vec2 &lhs, float scalar);

std::string ToFormattedString(Vec2 vec);

template <typename Sink>
void AbslStringify(Sink &sink, Vec2 vec) {
  sink.Append(ToFormattedString(vec));
}

std::ostream &operator<<(std::ostream &stream, Vec2 v);

// This represents a duration of time, i.e. the difference between two points in
// time (as represented by class Time, below). This class is unit-agnostic; it
// could represent e.g. hours, seconds, or years.
class Duration {
 public:
  Duration() : Duration(0) {}
  explicit Duration(double value) : value_(value) {}
  double Value() const { return value_; }

 private:
  double value_ = 0;
};

Duration operator+(Duration lhs, Duration rhs);
Duration operator-(Duration lhs, Duration rhs);
Duration operator*(Duration duration, double scalar);
Duration operator*(double scalar, Duration duration);
Duration operator/(Duration duration, double scalar);

Duration &operator+=(Duration &lhs, Duration rhs);
Duration &operator-=(Duration &lhs, Duration rhs);
Duration &operator*=(Duration &duration, double scalar);
Duration &operator/=(Duration &duration, double scalar);

bool operator==(Duration lhs, Duration rhs);
bool operator!=(Duration lhs, Duration rhs);
bool operator<(Duration lhs, Duration rhs);
bool operator>(Duration lhs, Duration rhs);
bool operator<=(Duration lhs, Duration rhs);
bool operator>=(Duration lhs, Duration rhs);

std::string ToFormattedString(Duration duration);

template <typename Sink>
void AbslStringify(Sink &sink, Duration duration) {
  sink.Append(ToFormattedString(duration));
}

std::ostream &operator<<(std::ostream &s, Duration duration);

// This represents a point in time. This class is unit- and offset-agnostic; it
// could be measured in e.g. hours, seconds, or years, and Time(0) has no
// specific meaning outside of the context it is used in.
class Time {
 public:
  Time() : Time(0) {}
  explicit Time(double value) : value_(value) {}
  double Value() const { return value_; }

 private:
  double value_ = 0;
};

Time operator+(Time time, Duration duration);
Time operator+(Duration duration, Time time);
Time operator-(Time time, Duration duration);
Duration operator-(Time lhs, Time rhs);

Time &operator+=(Time &time, Duration duration);
Time &operator-=(Time &time, Duration duration);

bool operator==(Time lhs, Time rhs);
bool operator!=(Time lhs, Time rhs);
bool operator<(Time lhs, Time rhs);
bool operator>(Time lhs, Time rhs);
bool operator<=(Time lhs, Time rhs);
bool operator>=(Time lhs, Time rhs);

std::string ToFormattedString(Time time);

template <typename Sink>
void AbslStringify(Sink &sink, Time time) {
  sink.Append(ToFormattedString(time));
}

std::ostream &operator<<(std::ostream &s, Time time);

// The input passed to the stroke modeler.
struct Input {
  // The type of event represented by the input. A "kDown" event represents
  // the beginning of the stroke, a "kUp" event represents the end of the
  // stroke, and all events in between are "kMove" events.
  enum class EventType { kDown, kMove, kUp };
  EventType event_type;

  // The position of the input.
  Vec2 position{0};

  // The time at which the input occurs.
  Time time{0};

  // The amount of pressure applied to the stylus. This is expected to lie in
  // the range [0, 1]. A negative value indicates unknown pressure.
  float pressure = -1;

  // The angle between the stylus and the plane of the device's screen. This
  // is expected to lie in the range [0, π/2]. A value of 0 indicates that the
  // stylus is perpendicular to the screen, while a value of π/2 indicates
  // that it is flush with the screen. A negative value indicates unknown
  // tilt.
  float tilt = -1;

  // The angle between the projection of the stylus onto the screen and the
  // positive x-axis, measured counter-clockwise. This is expected to lie in
  // the range [0, 2π). A negative value indicates unknown orientation.
  float orientation = -1;
};

bool operator==(const Input &lhs, const Input &rhs);
bool operator!=(const Input &lhs, const Input &rhs);

absl::Status ValidateInput(const Input &input);

std::string ToFormattedString(Input::EventType event_type);

template <typename Sink>
void AbslStringify(Sink &sink, Input::EventType event_type) {
  sink.Append(ToFormattedString(event_type));
}

std::ostream &operator<<(std::ostream &s, Input::EventType event_type);

std::string ToFormattedString(const Input &input);

template <typename Sink>
void AbslStringify(Sink &sink, const Input &input) {
  sink.Append(ToFormattedString(input));
}

std::ostream &operator<<(std::ostream &s, const Input &input);

// A modeled input produced by the stroke modeler.
struct Result {
  // The position/velocity/acceleration of the stroke tip.
  Vec2 position{0};
  Vec2 velocity{0};
  Vec2 acceleration{0};

  // The time at which this input occurs.
  Time time{0};

  // These pressure, tilt, and orientation of the stylus. See the
  // corresponding fields on the Input struct for more info.
  float pressure = -1;
  float tilt = -1;
  float orientation = -1;
};

std::string ToFormattedString(const Result &result);

template <typename Sink>
void AbslStringify(Sink &sink, const Result &result) {
  sink.Append(ToFormattedString(result));
}

std::ostream &operator<<(std::ostream &s, const Result &result);

////////////////////////////////////////////////////////////////////////////////
// Inline function definitions
////////////////////////////////////////////////////////////////////////////////

inline bool operator==(Vec2 lhs, Vec2 rhs) {
  return lhs.x == rhs.x && lhs.y == rhs.y;
}
inline bool operator!=(Vec2 lhs, Vec2 rhs) { return !(lhs == rhs); }

inline Vec2 operator+(Vec2 lhs, Vec2 rhs) {
  return {.x = lhs.x + rhs.x, .y = lhs.y + rhs.y};
}
inline Vec2 operator-(Vec2 lhs, Vec2 rhs) {
  return {.x = lhs.x - rhs.x, .y = lhs.y - rhs.y};
}
inline Vec2 operator*(float scalar, Vec2 v) {
  return {.x = scalar * v.x, .y = scalar * v.y};
}
inline Vec2 operator*(Vec2 v, float scalar) { return scalar * v; }
inline Vec2 operator/(Vec2 v, float scalar) {
  return {.x = v.x / scalar, .y = v.y / scalar};
}

inline Vec2 &operator+=(Vec2 &lhs, Vec2 rhs) {
  lhs.x += rhs.x;
  lhs.y += rhs.y;
  return lhs;
}
inline Vec2 &operator-=(Vec2 &lhs, Vec2 rhs) {
  lhs.x -= rhs.x;
  lhs.y -= rhs.y;
  return lhs;
}
inline Vec2 &operator*=(Vec2 &lhs, float scalar) {
  lhs.x *= scalar;
  lhs.y *= scalar;
  return lhs;
}
inline Vec2 &operator/=(Vec2 &lhs, float scalar) {
  lhs.x /= scalar;
  lhs.y /= scalar;
  return lhs;
}

inline std::ostream &operator<<(std::ostream &stream, Vec2 v) {
  return stream << ToFormattedString(v);
}

inline Duration operator+(Duration lhs, Duration rhs) {
  return Duration(lhs.Value() + rhs.Value());
}
inline Duration operator-(Duration lhs, Duration rhs) {
  return Duration(lhs.Value() - rhs.Value());
}
inline Duration operator*(Duration duration, double scalar) {
  return Duration(duration.Value() * scalar);
}
inline Duration operator*(double scalar, Duration duration) {
  return Duration(scalar * duration.Value());
}
inline Duration operator/(Duration duration, double scalar) {
  return Duration(duration.Value() / scalar);
}

inline Duration &operator+=(Duration &lhs, Duration rhs) {
  lhs = lhs + rhs;
  return lhs;
}
inline Duration &operator-=(Duration &lhs, Duration rhs) {
  lhs = lhs - rhs;
  return lhs;
}
inline Duration &operator*=(Duration &duration, double scalar) {
  duration = duration * scalar;
  return duration;
}
inline Duration &operator/=(Duration &duration, double scalar) {
  duration = duration / scalar;
  return duration;
}

inline bool operator==(Duration lhs, Duration rhs) {
  return lhs.Value() == rhs.Value();
}
inline bool operator!=(Duration lhs, Duration rhs) {
  return lhs.Value() != rhs.Value();
}
inline bool operator<(Duration lhs, Duration rhs) {
  return lhs.Value() < rhs.Value();
}
inline bool operator>(Duration lhs, Duration rhs) {
  return lhs.Value() > rhs.Value();
}
inline bool operator<=(Duration lhs, Duration rhs) {
  return lhs.Value() <= rhs.Value();
}
inline bool operator>=(Duration lhs, Duration rhs) {
  return lhs.Value() >= rhs.Value();
}

inline std::ostream &operator<<(std::ostream &s, Duration duration) {
  return s << ToFormattedString(duration);
}

inline Time operator+(Time time, Duration duration) {
  return Time(time.Value() + duration.Value());
}
inline Time operator+(Duration duration, Time time) {
  return Time(time.Value() + duration.Value());
}
inline Time operator-(Time time, Duration duration) {
  return Time(time.Value() - duration.Value());
}
inline Duration operator-(Time lhs, Time rhs) {
  return Duration(lhs.Value() - rhs.Value());
}

inline Time &operator+=(Time &time, Duration duration) {
  time = time + duration;
  return time;
}
inline Time &operator-=(Time &time, Duration duration) {
  time = time - duration;
  return time;
}

inline bool operator==(Time lhs, Time rhs) {
  return lhs.Value() == rhs.Value();
}
inline bool operator!=(Time lhs, Time rhs) {
  return lhs.Value() != rhs.Value();
}
inline bool operator<(Time lhs, Time rhs) { return lhs.Value() < rhs.Value(); }
inline bool operator>(Time lhs, Time rhs) { return lhs.Value() > rhs.Value(); }
inline bool operator<=(Time lhs, Time rhs) {
  return lhs.Value() <= rhs.Value();
}
inline bool operator>=(Time lhs, Time rhs) {
  return lhs.Value() >= rhs.Value();
}

inline std::ostream &operator<<(std::ostream &s, Time time) {
  return s << ToFormattedString(time);
}

inline bool operator==(const Input &lhs, const Input &rhs) {
  return lhs.event_type == rhs.event_type && lhs.position == rhs.position &&
         lhs.time == rhs.time && lhs.pressure == rhs.pressure &&
         lhs.tilt == rhs.tilt && lhs.orientation == rhs.orientation;
}
inline bool operator!=(const Input &lhs, const Input &rhs) {
  return !(lhs == rhs);
}

inline std::ostream &operator<<(std::ostream &s, Input::EventType event_type) {
  return s << ToFormattedString(event_type);
}

inline std::ostream &operator<<(std::ostream &s, const Input &input) {
  return s << ToFormattedString(input);
}

inline std::ostream &operator<<(std::ostream &s, const Result &result) {
  return s << ToFormattedString(result);
}

}  // namespace stroke_model
}  // namespace ink

#endif  // INK_STROKE_MODELER_TYPES_H_
