#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <cmath>
#include <cstddef>
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

	bool SamePrimitive(const draw3::HighlighterPrimitive& left,
		const draw3::HighlighterPrimitive& right)
	{
		return NearlyEqual(left.p1.x, right.p1.x) && NearlyEqual(left.p1.y, right.p1.y) &&
			NearlyEqual(left.p2.x, right.p2.x) && NearlyEqual(left.p2.y, right.p2.y) &&
			NearlyEqual(left.halfSize.x, right.halfSize.x) &&
			NearlyEqual(left.halfSize.y, right.halfSize.y);
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
	const draw3::StrokeModelConfiguration defaultConfiguration =
		draw3::CreateStrokeModelConfiguration(96);
	HIGHLIGHTER_CHECK(!defaultConfiguration.retainPredictionOnUp);
	HIGHLIGHTER_CHECK(defaultConfiguration.dpiScale == 1.0f);
	HIGHLIGHTER_CHECK(draw3::CreateStrokeModelConfiguration(192).dpiScale == 2.0f);
	draw3::ActiveStroke completedPen(5.0f, 500.0f);
	completedPen.realPoints = {
		{ 10.0f, 20.0f, 2.5f, 0.0f },
		{ 20.0f, 20.0f, 2.4f, 0.01f },
		{ 30.0f, 22.0f, 1.8f, 0.02f },
		{ 40.0f, 25.0f, 0.8f, 0.03f }
	};
	completedPen.committedIndex = 1;
	completedPen.hasCommittedGeometry = true;
	completedPen.previousL0DrawPoints = {
		{ 20.0f, 20.0f, 2.4f, 0.01f },
		{ 50.0f, 30.0f, 1.2f, 0.04f }
	};
	std::vector<draw3::InkPoint> completedTail;
	draw3::BuildCompletedPenTail(completedPen, false, completedTail);
	HIGHLIGHTER_CHECK(completedTail.size() == 3);
	HIGHLIGHTER_CHECK(NearlyEqual(completedTail.front().x, 20.0f));
	HIGHLIGHTER_CHECK(NearlyEqual(completedTail.back().x, 40.0f));
	draw3::BuildCompletedPenTail(completedPen, true, completedTail);
	HIGHLIGHTER_CHECK(completedTail.size() == 2);
	HIGHLIGHTER_CHECK(NearlyEqual(completedTail.front().x, 20.0f));
	HIGHLIGHTER_CHECK(NearlyEqual(completedTail.back().x, 50.0f));

	draw3::ActiveStroke clickPen(5.0f, 500.0f);
	clickPen.inputStartPoint = { 12.0f, 34.0f, 2.5f, 0.0f };
	clickPen.hasInputStartPoint = true;
	draw3::BuildCompletedPenTail(clickPen, false, completedTail);
	HIGHLIGHTER_CHECK(completedTail.size() == 1);
	HIGHLIGHTER_CHECK(NearlyEqual(completedTail.front().x, 12.0f));
	HIGHLIGHTER_CHECK(NearlyEqual(completedTail.front().y, 34.0f));

	constexpr float kHalfHeight = 25.0f;
	constexpr float kHalfWidth = 3.125f;
	HIGHLIGHTER_CHECK(NearlyEqual(draw3::kHighlighterNibAspectRatio, 8.0f));
	HIGHLIGHTER_CHECK(sizeof(draw3::HighlighterPrimitive) == 24);

	draw3::HighlighterGeometry geometry = draw3::BuildHighlighterGeometry({});
	HIGHLIGHTER_CHECK(geometry.primitives.empty());
	HIGHLIGHTER_CHECK(geometry.bounds.left == geometry.bounds.right);
	HIGHLIGHTER_CHECK(geometry.bounds.top == geometry.bounds.bottom);

	const draw3::InkPoint click = { 10.0f, 20.0f, kHalfHeight, 0.0f };
	geometry = draw3::BuildHighlighterGeometry({ click });
	HIGHLIGHTER_CHECK(geometry.primitives.size() == 1);
	if (!geometry.primitives.empty())
	{
		const draw3::HighlighterPrimitive& mark = geometry.primitives.front();
		HIGHLIGHTER_CHECK(NearlyEqual(mark.p1.x, click.x));
		HIGHLIGHTER_CHECK(NearlyEqual(mark.p1.y, click.y));
		HIGHLIGHTER_CHECK(NearlyEqual(mark.p2.x, click.x));
		HIGHLIGHTER_CHECK(NearlyEqual(mark.p2.y, click.y));
		HIGHLIGHTER_CHECK(NearlyEqual(mark.halfSize.x, kHalfWidth));
		HIGHLIGHTER_CHECK(NearlyEqual(mark.halfSize.y, kHalfHeight));
	}
	HIGHLIGHTER_CHECK(geometry.bounds.left == 3);
	HIGHLIGHTER_CHECK(geometry.bounds.top == -8);
	HIGHLIGHTER_CHECK(geometry.bounds.right == 17);
	HIGHLIGHTER_CHECK(geometry.bounds.bottom == 48);

	const std::vector<draw3::InkPoint> underTwelvePixels = {
		click, { 18.0f, 20.0f, kHalfHeight, 0.01f }
	};
	const draw3::HighlighterGeometry underTwelve =
		draw3::BuildHighlighterGeometry(underTwelvePixels);
	HIGHLIGHTER_CHECK(underTwelve.primitives.size() == 1);
	const std::vector<draw3::InkPoint> exactlyTwelvePixels = {
		click, { 22.0f, 20.0f, kHalfHeight, 0.01f }
	};
	const draw3::HighlighterGeometry exactlyTwelve =
		draw3::BuildHighlighterGeometry(exactlyTwelvePixels);
	HIGHLIGHTER_CHECK(exactlyTwelve.primitives.size() == 1); // 12px 不再是可见性或几何分支。
	if (!exactlyTwelve.primitives.empty())
	{
		const draw3::HighlighterPrimitive& sweep = exactlyTwelve.primitives.front();
		HIGHLIGHTER_CHECK(NearlyEqual(sweep.p1.x, 10.0f));
		HIGHLIGHTER_CHECK(NearlyEqual(sweep.p2.x, 22.0f));
		HIGHLIGHTER_CHECK(NearlyEqual(sweep.halfSize.x, kHalfWidth));
		HIGHLIGHTER_CHECK(NearlyEqual(sweep.halfSize.y, kHalfHeight));
	}

	const std::vector<draw3::InkPoint> curve = {
		{ 10.0f, 20.0f, kHalfHeight, 0.0f },
		{ 40.0f, 20.0f, kHalfHeight, 0.01f },
		{ 40.0f, 50.0f, kHalfHeight, 0.02f },
		{ 12.0f, 52.0f, kHalfHeight, 0.03f }
	};
	geometry = draw3::BuildHighlighterGeometry(curve);
	HIGHLIGHTER_CHECK(geometry.primitives.size() == curve.size() - 1); // 转角和回折不再追加圆角 primitive。
	for (std::size_t index = 0; index < geometry.primitives.size(); ++index)
	{
		const draw3::HighlighterPrimitive& primitive = geometry.primitives[index];
		HIGHLIGHTER_CHECK(NearlyEqual(primitive.halfSize.x, kHalfWidth));
		HIGHLIGHTER_CHECK(NearlyEqual(primitive.halfSize.y, kHalfHeight));
		if (index + 1 < geometry.primitives.size())
		{
			const draw3::HighlighterPrimitive& next = geometry.primitives[index + 1];
			HIGHLIGHTER_CHECK(NearlyEqual(primitive.p2.x, next.p1.x));
			HIGHLIGHTER_CHECK(NearlyEqual(primitive.p2.y, next.p1.y));
		}
	}

	const std::vector<draw3::InkPoint> subpixelJitter = {
		click,
		{ 10.01f, 20.0f, kHalfHeight, 0.001f },
		{ 10.20f, 20.0f, kHalfHeight, 0.002f }
	};
	geometry = draw3::BuildHighlighterGeometry(subpixelJitter);
	HIGHLIGHTER_CHECK(geometry.primitives.size() == 1);
	if (!geometry.primitives.empty())
	{
		HIGHLIGHTER_CHECK(NearlyEqual(geometry.primitives.front().p1.x, 10.0f));
		HIGHLIGHTER_CHECK(NearlyEqual(geometry.primitives.front().p2.x, 10.0f));
	}
	std::vector<draw3::InkPoint> accumulatedMovement = subpixelJitter;
	accumulatedMovement.push_back({ 10.26f, 20.0f, kHalfHeight, 0.003f });
	geometry = draw3::BuildHighlighterGeometry(accumulatedMovement);
	HIGHLIGHTER_CHECK(geometry.primitives.size() == 1);
	if (!geometry.primitives.empty())
		HIGHLIGHTER_CHECK(NearlyEqual(geometry.primitives.front().p2.x, 10.26f));

	const std::vector<draw3::InkPoint> committedPoints = {
		{ 10.0f, 20.0f, kHalfHeight, 0.0f },
		{ 22.0f, 20.0f, kHalfHeight, 0.01f }
	};
	const std::vector<draw3::InkPoint> liveTailPoints = {
		{ 22.0f, 20.0f, kHalfHeight, 0.01f },
		{ 22.0f, 36.0f, kHalfHeight, 0.02f },
		{ 40.0f, 48.0f, kHalfHeight, 0.03f }
	};
	const draw3::HighlighterGeometry committedPrefix =
		draw3::BuildHighlighterGeometry(committedPoints);
	const draw3::HighlighterGeometry liveTail =
		draw3::BuildHighlighterGeometry(liveTailPoints);
	const draw3::HighlighterGeometry completedFromCache =
		draw3::MergeHighlighterGeometry(committedPrefix, liveTail);
	HIGHLIGHTER_CHECK(completedFromCache.primitives.size() == 3);
	HIGHLIGHTER_CHECK(SamePrimitive(completedFromCache.primitives.front(),
		committedPrefix.primitives.front()));
	HIGHLIGHTER_CHECK(SamePrimitive(completedFromCache.primitives.back(),
		liveTail.primitives.back()));
	HIGHLIGHTER_CHECK(NearlyEqual(committedPrefix.primitives.back().p2.x,
		liveTail.primitives.front().p1.x));
	HIGHLIGHTER_CHECK(NearlyEqual(committedPrefix.primitives.back().p2.y,
		liveTail.primitives.front().p1.y));

	draw3::ActiveStroke cachedCompletion(50.0f, 500.0f,
		draw3::StrokeWidthMode::Fixed, true);
	cachedCompletion.committedHighlighterGeometry = committedPrefix;
	cachedCompletion.l0HighlighterGeometry = liveTail;
	cachedCompletion.realPoints = {
		{ 300.0f, 400.0f, kHalfHeight, 1.0f },
		{ 100.0f, 50.0f, kHalfHeight, 1.1f }
	};
	const draw3::HighlighterGeometry afterRealPointMutation = draw3::MergeHighlighterGeometry(
		cachedCompletion.committedHighlighterGeometry, cachedCompletion.l0HighlighterGeometry);
	HIGHLIGHTER_CHECK(afterRealPointMutation.primitives.size() == completedFromCache.primitives.size());
	for (std::size_t index = 0; index < completedFromCache.primitives.size(); ++index)
		HIGHLIGHTER_CHECK(SamePrimitive(afterRealPointMutation.primitives[index],
			completedFromCache.primitives[index]));

	if (failures == 0) std::cout << "All highlighter geometry tests passed." << std::endl;
	return failures;
}
