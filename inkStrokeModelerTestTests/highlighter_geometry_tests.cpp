#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <cmath>
#include <iostream>
#include <vector>
#include <windows.h>

import draw3.renderer;
import draw3.ink_prediction;

namespace
{
	bool NearlyEqual(float left, float right, float epsilon = 0.001f)
	{
		return std::abs(left - right) <= epsilon;
	}

	const draw3::HighlighterPrimitive* FirstBody(const draw3::HighlighterGeometry& geometry)
	{
		for (const draw3::HighlighterPrimitive& primitive : geometry.primitives)
		{
			if (primitive.type == draw3::HighlighterPrimitiveType::Body) return &primitive;
		}
		return nullptr;
	}

	const draw3::HighlighterPrimitive* FirstPrimitive(
		const draw3::HighlighterGeometry& geometry, draw3::HighlighterPrimitiveType type)
	{
		for (const draw3::HighlighterPrimitive& primitive : geometry.primitives)
		{
			if (primitive.type == type) return &primitive;
		}
		return nullptr;
	}

	float Cross(float ax, float ay, float bx, float by)
	{
		return ax * by - ay * bx;
	}

	bool SectorContains(const draw3::HighlighterPrimitive& primitive, float x, float y)
	{
		const float vectorX = x - primitive.p1.x;
		const float vectorY = y - primitive.p1.y;
		const float sign = primitive.startExtension >= 0.0f ? 1.0f : -1.0f;
		return Cross(primitive.direction1.x, primitive.direction1.y, vectorX, vectorY) * sign >= -0.00001f &&
			Cross(vectorX, vectorY, primitive.direction2.x, primitive.direction2.y) * sign >= -0.00001f;
	}

	void Check(bool condition, const char* expression, int line, int& failures)
	{
		if (condition) return;
		++failures;
		std::cerr << "FAILED highlighter line " << line << ": " << expression << std::endl;
	}

#define HIGHLIGHTER_CHECK(expression) Check(!!(expression), #expression, __LINE__, failures)
}

int RunHighlighterGeometryTests()
{
	int failures = 0;
	const draw3::HighlighterBoundaryFlags complete =
		draw3::HighlighterBoundaryFlags::Start | draw3::HighlighterBoundaryFlags::End;
	const draw3::HighlighterStartDirectionState unlocked = {};

	const std::vector<draw3::InkPoint> underThreshold = {
		{ 10.0f, 20.0f, 25.0f, 0.0f },
		{ 18.0f, 20.0f, 25.0f, 0.01f }
	};
	draw3::HighlighterGeometry geometry =
		draw3::BuildHighlighterGeometry(underThreshold, complete, false, unlocked);
	HIGHLIGHTER_CHECK(geometry.primitives.empty());
	HIGHLIGHTER_CHECK(geometry.bounds.left == geometry.bounds.right);
	HIGHLIGHTER_CHECK(geometry.bounds.top == geometry.bounds.bottom);

	draw3::HighlighterStartDirectionState shortDirection;
	shortDirection.locked = true;
	shortDirection.direction = { 0.0f, 1.0f };
	geometry = draw3::BuildHighlighterGeometry(
		{ underThreshold.front() }, complete, true, shortDirection);
	HIGHLIGHTER_CHECK(geometry.primitives.size() == 1);
	if (!geometry.primitives.empty())
	{
		const draw3::HighlighterPrimitive& mark = geometry.primitives.front();
		HIGHLIGHTER_CHECK(mark.type == draw3::HighlighterPrimitiveType::ShortMark);
		HIGHLIGHTER_CHECK(NearlyEqual(mark.p1.x, 10.0f));
		HIGHLIGHTER_CHECK(NearlyEqual(mark.p1.y, 20.0f));
		HIGHLIGHTER_CHECK(NearlyEqual(mark.p2.x, 10.0f));
		HIGHLIGHTER_CHECK(NearlyEqual(mark.p2.y, 32.0f));
		HIGHLIGHTER_CHECK(NearlyEqual(mark.radius, 25.0f));
	}

	draw3::HighlighterStartDirectionState locked;
	locked.locked = true;
	locked.direction = { 1.0f, 0.0f };
	const std::vector<draw3::InkPoint> exactThreshold = {
		{ 10.0f, 20.0f, 25.0f, 0.0f },
		{ 22.0f, 20.0f, 25.0f, 0.01f }
	};
	const draw3::HighlighterGeometry firstVisible =
		draw3::BuildHighlighterGeometry(exactThreshold, complete, false, locked);
	const draw3::HighlighterPrimitive* firstBody = FirstBody(firstVisible);
	HIGHLIGHTER_CHECK(firstBody != nullptr);
	draw3::HighlighterPrimitive firstCap = {};
	if (firstBody)
	{
		firstCap = *firstBody;
		HIGHLIGHTER_CHECK(NearlyEqual(firstCap.p1.x, 10.0f));
		HIGHLIGHTER_CHECK(NearlyEqual(firstCap.p1.y, 20.0f));
		HIGHLIGHTER_CHECK(NearlyEqual(firstCap.p2.x, 22.0f));
		HIGHLIGHTER_CHECK(NearlyEqual(firstCap.p2.y, 20.0f));
		HIGHLIGHTER_CHECK(NearlyEqual(firstCap.startExtension, 0.0f));
	}

	const std::vector<draw3::InkPoint> appendedCurve = {
		{ 10.0f, 20.0f, 25.0f, 0.0f },
		{ 22.0f, 20.0f, 25.0f, 0.01f },
		{ 22.0f, 36.0f, 25.0f, 0.02f },
		{ 40.0f, 48.0f, 25.0f, 0.03f }
	};
	for (const draw3::HighlighterBoundaryFlags flags : {
		draw3::HighlighterBoundaryFlags::Start, complete })
	{
		const draw3::HighlighterGeometry appended =
			draw3::BuildHighlighterGeometry(appendedCurve, flags, false, locked);
		const draw3::HighlighterPrimitive* appendedBody = FirstBody(appended);
		HIGHLIGHTER_CHECK(appendedBody != nullptr);
		if (firstBody && appendedBody)
		{
			HIGHLIGHTER_CHECK(NearlyEqual(appendedBody->p1.x, firstCap.p1.x));
			HIGHLIGHTER_CHECK(NearlyEqual(appendedBody->p1.y, firstCap.p1.y));
			HIGHLIGHTER_CHECK(NearlyEqual(appendedBody->p2.x, firstCap.p2.x));
			HIGHLIGHTER_CHECK(NearlyEqual(appendedBody->p2.y, firstCap.p2.y));
			HIGHLIGHTER_CHECK(NearlyEqual(appendedBody->startExtension, 0.0f));
		}
	}

	const std::vector<draw3::InkPoint> internalSlice = {
		{ 22.0f, 20.0f, 25.0f, 0.01f },
		{ 22.0f, 36.0f, 25.0f, 0.02f }
	};
	geometry = draw3::BuildHighlighterGeometry(
		internalSlice, draw3::HighlighterBoundaryFlags::None, false, unlocked);
	HIGHLIGHTER_CHECK(FirstBody(geometry) != nullptr); // 非全局 Start 的 L1 切片不受首次可见闸门影响。

	// 连续 90° 转角必须用外侧扇区补齐；否则截图中会出现三角形缺角。
	const std::vector<draw3::InkPoint> clockwiseCorner = {
		{ 10.0f, 20.0f, 25.0f, 0.0f },
		{ 40.0f, 20.0f, 25.0f, 0.01f },
		{ 40.0f, 50.0f, 25.0f, 0.02f }
	};
	geometry = draw3::BuildHighlighterGeometry(
		clockwiseCorner, complete, false, locked);
	const draw3::HighlighterPrimitive* clockwiseSector =
		FirstPrimitive(geometry, draw3::HighlighterPrimitiveType::RoundJoinSector);
	HIGHLIGHTER_CHECK(clockwiseSector != nullptr);
	if (clockwiseSector)
	{
		HIGHLIGHTER_CHECK(SectorContains(*clockwiseSector, 50.0f, 10.0f));
		HIGHLIGHTER_CHECK(!SectorContains(*clockwiseSector, 30.0f, 30.0f));
		HIGHLIGHTER_CHECK(geometry.bounds.left <= 14 && geometry.bounds.top <= -6);
		HIGHLIGHTER_CHECK(geometry.bounds.right >= 66 && geometry.bounds.bottom >= 53);
	}

	const std::vector<draw3::InkPoint> counterClockwiseCorner = {
		{ 10.0f, 50.0f, 25.0f, 0.0f },
		{ 40.0f, 50.0f, 25.0f, 0.01f },
		{ 40.0f, 20.0f, 25.0f, 0.02f }
	};
	geometry = draw3::BuildHighlighterGeometry(
		counterClockwiseCorner, complete, false, locked);
	const draw3::HighlighterPrimitive* counterClockwiseSector =
		FirstPrimitive(geometry, draw3::HighlighterPrimitiveType::RoundJoinSector);
	HIGHLIGHTER_CHECK(counterClockwiseSector != nullptr);
	if (counterClockwiseSector)
	{
		HIGHLIGHTER_CHECK(SectorContains(*counterClockwiseSector, 50.0f, 60.0f));
		HIGHLIGHTER_CHECK(!SectorContains(*counterClockwiseSector, 30.0f, 40.0f));
	}

	const std::vector<draw3::InkPoint> hairpin = {
		{ 10.0f, 20.0f, 25.0f, 0.0f },
		{ 40.0f, 20.0f, 25.0f, 0.01f },
		{ 10.0f, 21.0f, 25.0f, 0.02f }
	};
	geometry = draw3::BuildHighlighterGeometry(hairpin, complete, false, locked);
	HIGHLIGHTER_CHECK(FirstPrimitive(
		geometry, draw3::HighlighterPrimitiveType::RoundJoinCircle) != nullptr);

	const std::vector<draw3::InkPoint> duplicateStart = {
		{ 10.0f, 20.0f, 25.0f, 0.0f },
		{ 10.0f, 20.0f, 25.0f, 0.001f },
		{ 30.0f, 20.0f, 25.0f, 0.01f }
	};
	geometry = draw3::BuildHighlighterGeometry(duplicateStart, complete, false, unlocked);
	HIGHLIGHTER_CHECK(geometry.primitives.empty());

	if (failures == 0) std::cout << "All highlighter geometry tests passed." << std::endl;
	return failures;
}
