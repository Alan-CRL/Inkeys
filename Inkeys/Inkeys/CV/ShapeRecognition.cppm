module;

#include <cstdint>
#include <span>

export module Inkeys.CV.ShapeRecognition;

export namespace Inkeys::CV
{
	enum class ShapeType : std::uint8_t
	{
		Unknown = 0,
		AxisAlignedRectangle = 1
	};

	struct InkPoint
	{
		float x = 0.0f;
		float y = 0.0f;
		float width = 0.0f;
	};

	struct InkStrokeView
	{
		std::span<const InkPoint> points;
	};

	struct AxisAlignedRectangle
	{
		float left = 0.0f;
		float top = 0.0f;
		float right = 0.0f;
		float bottom = 0.0f;
	};

	struct ShapeResult
	{
		ShapeType type = ShapeType::Unknown;
		float confidence = 0.0f;
		AxisAlignedRectangle rectangle = {};
	};

	// 识别边界不暴露 OpenCV 类型；任何异常都按 Unknown 关闭失败。
	ShapeResult RecognizeInkShape(
		std::span<const InkStrokeView> strokes, float dpiScale) noexcept;
}
