#include "ink_stroke_modeler/types.h"

#include <algorithm>
#include <cmath>
#include <string>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_format.h"
#include "ink_stroke_modeler/internal/validation.h"

// This convenience macro evaluates the given expression, and if it does not
// return an OK status, returns and propagates the status.
#define RETURN_IF_ERROR(expr)                              \
  do {                                                     \
    if (auto status = (expr); !status.ok()) return status; \
  } while (false)

namespace ink {
namespace stroke_model {

absl::StatusOr<float> Vec2::AbsoluteAngleTo(Vec2 other) const {
  if (!IsFinite() || !other.IsFinite()) {
    return absl::InvalidArgumentError(
        absl::StrFormat("Non-finite inputs: this=%v; other=%v.", *this, other));
  }
  float magnitude = Magnitude();
  float other_magnitude = other.Magnitude();
  if (magnitude == 0 || other_magnitude == 0) return 0;

  Vec2 unit_vec = *this / magnitude;
  Vec2 other_unit_vec = other / other_magnitude;
  float dot = unit_vec.x * other_unit_vec.x + unit_vec.y * other_unit_vec.y;
  return std::acos(std::clamp(dot, -1.f, 1.f));
}

std::string ToFormattedString(Vec2 vec) {
  // Use StrCat instead of StrFormat to avoid trailing zeros in short decimals.
  return absl::StrCat("(", vec.x, ", ", vec.y, ")");
}

absl::Status ValidateInput(const Input &input) {
  switch (input.event_type) {
    case Input::EventType::kUp:
    case Input::EventType::kMove:
    case Input::EventType::kDown:
      break;
    default:
      return absl::InvalidArgumentError("Unknown Input.event_type.");
  }
  RETURN_IF_ERROR(ValidateIsFiniteNumber(input.position.x, "Input.position.x"));
  RETURN_IF_ERROR(ValidateIsFiniteNumber(input.position.y, "Input.position.y"));
  RETURN_IF_ERROR(ValidateIsFiniteNumber(input.time.Value(), "Input.time"));
  // This probably should also ValidateIsFiniteNumber for pressure, tilt, and
  // orientation, since unknown values for those should be represented as -1.
  // However, some consumers are forwarding NaN values for those fields.
  return absl::OkStatus();
}

std::string ToFormattedString(Duration duration) {
  // Use StrCat instead of StrFormat to avoid trailing zeros in short decimals.
  return absl::StrCat(duration.Value());
}

std::string ToFormattedString(Time time) {
  // Use StrCat instead of StrFormat to avoid trailing zeros in short decimals.
  return absl::StrCat(time.Value());
}

std::string ToFormattedString(Input::EventType event_type) {
  switch (event_type) {
    case Input::EventType::kDown:
      return "Down";
    case Input::EventType::kMove:
      return "Move";
    case Input::EventType::kUp:
      return "Up";
  }
  return absl::StrFormat("UnknownEventType<%d>", static_cast<int>(event_type));
}

std::string ToFormattedString(const Input &input) {
  return absl::StrFormat(
      "<Input: %v, pos: %v, time: %v, pressure: %v, tilt: %v, orientation:%v>",
      input.event_type, input.position, input.time, input.pressure, input.tilt,
      input.orientation);
}

std::string ToFormattedString(const Result &result) {
  return absl::StrFormat(
      "<Result: pos: %v, vel: %v, acc: %v, time: %v, pressure: %v, tilt: %v, "
      "orientation: %v>",
      result.position, result.velocity, result.acceleration, result.time,
      result.pressure, result.tilt, result.orientation);
}

}  // namespace stroke_model
}  // namespace ink
