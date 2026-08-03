#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <algorithm>
#include <array>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
#include <vector>
#include <windows.h>

import draw3.renderer;
import draw3.ink_prediction;

namespace
{
	bool NearlyEqual(float left, float right, float epsilon = 0.0001f) noexcept
	{
		return std::abs(left - right) <= epsilon;
	}

	bool ContainsText(const std::filesystem::path& path, const std::string& text)
	{
		std::ifstream input(path, std::ios::binary);
		if (!input) return false;
		const std::string contents((std::istreambuf_iterator<char>(input)),
			std::istreambuf_iterator<char>());
		return contents.find(text) != std::string::npos;
	}

	bool TextAppearsBefore(const std::filesystem::path& path,
		const std::string& first, const std::string& second)
	{
		std::ifstream input(path, std::ios::binary);
		if (!input) return false;
		const std::string contents((std::istreambuf_iterator<char>(input)),
			std::istreambuf_iterator<char>());
		const size_t firstPosition = contents.find(first);
		const size_t secondPosition = contents.find(second);
		return firstPosition != std::string::npos &&
			secondPosition != std::string::npos &&
			firstPosition < secondPosition;
	}

	std::filesystem::path FindProjectRoot()
	{
		std::vector<std::filesystem::path> candidates;
		candidates.push_back(std::filesystem::current_path());
		wchar_t executablePath[MAX_PATH] = {};
		const DWORD length = GetModuleFileNameW(nullptr, executablePath, MAX_PATH);
		if (length > 0 && length < MAX_PATH)
			candidates.emplace_back(std::wstring(executablePath, length));

		for (std::filesystem::path candidate : candidates)
		{
			if (!std::filesystem::is_directory(candidate)) candidate = candidate.parent_path();
			for (;;)
			{
				if (std::filesystem::exists(candidate / "inkStrokeModelerTest" / "ink.hlsli"))
					return candidate;
				const std::filesystem::path parent = candidate.parent_path();
				if (parent == candidate) break;
				candidate = parent;
			}
		}
		return {};
	}

	void Check(bool condition, const char* expression, int line, int& failures)
	{
		if (condition) return;
		++failures;
		std::cerr << "FAILED LaserIncremental line " << line << ": " << expression << std::endl;
	}
}

#define LASER_INCREMENTAL_CHECK(expression) \
	Check((expression), #expression, __LINE__, failures)

int RunLaserIncrementalCoverageTests()
{
	int failures = 0;
	const std::vector<draw3::InkPoint> points = {
		{ 0.0f, 0.0f, 2.5f, 0.00f },
		{ 1.0f, 0.0f, 2.5f, 0.10f },
		{ 2.0f, 0.0f, 2.5f, 0.20f },
		{ 3.0f, 0.0f, 2.5f, 0.30f }
	};

	// 稳定边界按时间推进，L1/L0 共享一个连接点。
	draw3::LaserIncrementalStrokeState state;
	draw3::LaserIncrementalRanges ranges =
		draw3::PlanLaserIncrementalRanges(points, state, 0.15);
	LASER_INCREMENTAL_CHECK(ranges.stableFirstIndex == 0);
	LASER_INCREMENTAL_CHECK(ranges.stablePointCount == 2);
	LASER_INCREMENTAL_CHECK(ranges.liveFirstIndex == 1);
	LASER_INCREMENTAL_CHECK(ranges.livePointCount == 3);
	state.stableCommittedIndex = ranges.nextStableCommittedIndex;
	state.rebuildRequired = false;
	ranges = draw3::PlanLaserIncrementalRanges(points, state, 0.05);
	LASER_INCREMENTAL_CHECK(ranges.nextStableCommittedIndex == 2);
	LASER_INCREMENTAL_CHECK(ranges.stableFirstIndex == 1);
	LASER_INCREMENTAL_CHECK(ranges.stablePointCount == 2);
	LASER_INCREMENTAL_CHECK(ranges.liveFirstIndex == 2);
	state.stableCommittedIndex = ranges.nextStableCommittedIndex;
	// prediction 回缩只改变保护时长，不能让已经提交的稳定游标后退。
	ranges = draw3::PlanLaserIncrementalRanges(points, state, 1.0);
	LASER_INCREMENTAL_CHECK(ranges.nextStableCommittedIndex == 2);
	LASER_INCREMENTAL_CHECK(ranges.stablePointCount == 0);
	LASER_INCREMENTAL_CHECK(ranges.liveFirstIndex == 2);

	// Resize/Clear 后覆盖纹理为空，CPU 游标必须从零重建。
	state.stableCommittedIndex = 3;
	state.rebuildRequired = true;
	ranges = draw3::PlanLaserIncrementalRanges(points, state, 0.15);
	LASER_INCREMENTAL_CHECK(ranges.stableFirstIndex == 0);
	LASER_INCREMENTAL_CHECK(ranges.liveFirstIndex == 1);

	// 多 contact fallback 只脏化新增 stable 与旧/新 live，完整 layer bounds 仍可用于绘制。
	const std::vector<draw3::InkPoint> dirtyRealPoints = {
		{ 10.0f, 10.0f, 2.5f, 0.0f },
		{ 30.0f, 10.0f, 2.5f, 0.1f },
		{ 50.0f, 20.0f, 2.5f, 0.2f },
		{ 70.0f, 30.0f, 2.5f, 0.3f }
	};
	const std::vector<draw3::InkPoint> dirtyVisiblePoints = {
		dirtyRealPoints[0], dirtyRealPoints[1], dirtyRealPoints[2],
		dirtyRealPoints[3], { 90.0f, 45.0f, 2.5f, 0.4f }
	};
	draw3::LaserIncrementalStrokeState dirtyState;
	const draw3::LaserLayerDirtyPlan dirtyPlan = draw3::PlanLaserLayerDirty(
		dirtyRealPoints, dirtyVisiblePoints, dirtyState,
		RECT{ 0, 0, 0, 0 }, RECT{ 2, 3, 12, 14 }, 0.15, 1.0f, 200, 200);
	LASER_INCREMENTAL_CHECK(dirtyPlan.ranges.stablePointCount > 0);
	LASER_INCREMENTAL_CHECK(!draw3::IsEmptyRect(dirtyPlan.stableDeltaBounds));
	LASER_INCREMENTAL_CHECK(!draw3::IsEmptyRect(dirtyPlan.liveBounds));
	LASER_INCREMENTAL_CHECK(!draw3::IsEmptyRect(dirtyPlan.layerBounds));
	LASER_INCREMENTAL_CHECK(dirtyPlan.dirtyBounds.left <= 2);
	LASER_INCREMENTAL_CHECK(dirtyPlan.dirtyBounds.top <= 3);
	LASER_INCREMENTAL_CHECK(dirtyPlan.dirtyBounds.right >= dirtyPlan.liveBounds.right);
	LASER_INCREMENTAL_CHECK(dirtyPlan.dirtyBounds.bottom >= dirtyPlan.liveBounds.bottom);

	// 长笔画每帧只提交稳定 delta 与受保护 live 尾部，不随总点数线性增长。
	std::vector<draw3::InkPoint> longStroke;
	longStroke.reserve(200);
	for (size_t index = 0; index < 200; ++index)
		longStroke.push_back({ static_cast<float>(index), 20.0f, 2.5f,
			static_cast<float>(index) * 0.01f });
	draw3::LaserIncrementalStrokeState longStrokeState;
	uint64_t incrementalSubmittedPoints = 0;
	uint64_t fullRedrawEquivalentPoints = 0;
	size_t maximumFrameSubmission = 0;
	for (size_t pointCount = 2; pointCount <= longStroke.size(); ++pointCount)
	{
		const std::vector<draw3::InkPoint> prefix(
			longStroke.begin(), longStroke.begin() + pointCount);
		const draw3::LaserIncrementalRanges frameRanges =
			draw3::PlanLaserIncrementalRanges(prefix, longStrokeState, 0.05);
		const size_t frameSubmission =
			frameRanges.stablePointCount + frameRanges.livePointCount;
		maximumFrameSubmission = std::max(maximumFrameSubmission, frameSubmission);
		incrementalSubmittedPoints += frameSubmission;
		fullRedrawEquivalentPoints += pointCount;
		longStrokeState.stableCommittedIndex = frameRanges.nextStableCommittedIndex;
		longStrokeState.rebuildRequired = false;
	}
	LASER_INCREMENTAL_CHECK(maximumFrameSubmission < 16);
	LASER_INCREMENTAL_CHECK(
		incrementalSubmittedPoints * 4 < fullRedrawEquivalentPoints);

	LASER_INCREMENTAL_CHECK(draw3::SelectLaserCoverageMode(
		draw3::LaserCoverageMode::Inactive, 0, true) ==
		draw3::LaserCoverageMode::Inactive);
	LASER_INCREMENTAL_CHECK(draw3::SelectLaserCoverageMode(
		draw3::LaserCoverageMode::Inactive, 1, true) ==
		draw3::LaserCoverageMode::Incremental);
	LASER_INCREMENTAL_CHECK(draw3::SelectLaserCoverageMode(
		draw3::LaserCoverageMode::Incremental, 2, true) ==
		draw3::LaserCoverageMode::FullRedraw);
	LASER_INCREMENTAL_CHECK(draw3::SelectLaserCoverageMode(
		draw3::LaserCoverageMode::FullRedraw, 1, true) ==
		draw3::LaserCoverageMode::FullRedraw);
	LASER_INCREMENTAL_CHECK(draw3::SelectLaserCoverageMode(
		draw3::LaserCoverageMode::Inactive, 1, false) ==
		draw3::LaserCoverageMode::FullRedraw);

	// 自交/急弯的旧、新 live bounds 必须通过 union 一起保留。
	const std::vector<draw3::InkPoint> selfIntersecting = {
		{ 10.0f, 10.0f, 3.0f, 0.0f },
		{ 110.0f, 110.0f, 3.0f, 0.1f },
		{ 10.0f, 110.0f, 4.0f, 0.2f },
		{ 110.0f, 10.0f, 2.0f, 0.3f }
	};
	const RECT oldBounds = draw3::RectFromLaserPoints(
		std::vector<draw3::InkPoint>{ selfIntersecting[0], selfIntersecting[1] },
		1.0f, 256, 256);
	const RECT newBounds = draw3::RectFromLaserPoints(
		std::vector<draw3::InkPoint>{ selfIntersecting[2], selfIntersecting[3] },
		1.0f, 256, 256);
	RECT dirtyBounds = oldBounds;
	draw3::UnionRectInPlace(dirtyBounds, newBounds);
	LASER_INCREMENTAL_CHECK(dirtyBounds.left <= oldBounds.left);
	LASER_INCREMENTAL_CHECK(dirtyBounds.top <= oldBounds.top);
	LASER_INCREMENTAL_CHECK(dirtyBounds.right >= newBounds.right);
	LASER_INCREMENTAL_CHECK(dirtyBounds.bottom >= newBounds.bottom);

	// 对每个 coverage 通道取 MAX 与整笔 union 等价，且连接处重复提交幂等。
	const std::array<std::array<float, 4>, 4> segmentCoverage = {{
		{{ 0.1f, 0.8f, 0.2f, 0.0f }},
		{{ 0.7f, 0.2f, 0.4f, 0.3f }},
		{{ 0.3f, 0.9f, 0.1f, 0.6f }},
		{{ 0.8f, 0.4f, 0.5f, 0.2f }}
	}};
	std::array<float, 4> stableCoverage = {};
	std::array<float, 4> liveCoverage = {};
	std::array<float, 4> fullCoverage = {};
	for (size_t segment = 0; segment < segmentCoverage.size(); ++segment)
	{
		for (size_t channel = 0; channel < stableCoverage.size(); ++channel)
		{
			fullCoverage[channel] = std::max(fullCoverage[channel],
				segmentCoverage[segment][channel]);
			if (segment <= 2)
				stableCoverage[channel] = std::max(stableCoverage[channel],
					segmentCoverage[segment][channel]);
			if (segment >= 2)
				liveCoverage[channel] = std::max(liveCoverage[channel],
					segmentCoverage[segment][channel]);
		}
	}
	for (size_t channel = 0; channel < fullCoverage.size(); ++channel)
		LASER_INCREMENTAL_CHECK(NearlyEqual(fullCoverage[channel],
			std::max(stableCoverage[channel], liveCoverage[channel])));

	// CPU/HLSL 资源槽、shape 和解绑契约保持显式镜像。
	const std::filesystem::path root = FindProjectRoot();
	LASER_INCREMENTAL_CHECK(!root.empty());
	if (!root.empty())
	{
		const std::filesystem::path particleCommon =
			root / "inkStrokeModelerTest" / "laserParticleCommon.hlsli";
		const std::filesystem::path vertexShader =
			root / "inkStrokeModelerTest" / "inkVertexShader.hlsl";
		const std::filesystem::path rendererSource =
			root / "inkStrokeModelerTest" / "draw3" / "renderer.cpp";
		const std::filesystem::path controllerSource =
			root / "inkStrokeModelerTest" / "draw3" / "drawing_controller.cpp";
		const std::filesystem::path particleSource =
			root / "inkStrokeModelerTest" / "draw3" / "laser_particles.cpp";
		LASER_INCREMENTAL_CHECK(sizeof(draw3::LaserGpuParticle) == 80);
		LASER_INCREMENTAL_CHECK(ContainsText(particleCommon, "uint padding;"));
		LASER_INCREMENTAL_CHECK(TextAppearsBefore(
			particleCommon, "float2 position;", "float2 velocity;"));
		LASER_INCREMENTAL_CHECK(TextAppearsBefore(
			particleCommon, "float2 velocity;", "float ageSeconds;"));
		LASER_INCREMENTAL_CHECK(TextAppearsBefore(
			particleCommon, "float breathingRampSeconds;", "uint seed;"));
		LASER_INCREMENTAL_CHECK(TextAppearsBefore(
			particleCommon, "uint alive;", "uint padding;"));
		LASER_INCREMENTAL_CHECK(ContainsText(rendererSource,
			"StepLaserParticles("));
		LASER_INCREMENTAL_CHECK(ContainsText(rendererSource,
			"uploadedLaserStyleGeneration_"));
		LASER_INCREMENTAL_CHECK(ContainsText(vertexShader,
			"float2 rectMin = globalColor.xy;"));
		LASER_INCREMENTAL_CHECK(TextAppearsBefore(vertexShader,
			"if (type == 8 || type == 11 || type == 12 || type == 13)",
			"uint realIndex = globalBufferOffset + itemIndex;"));
		LASER_INCREMENTAL_CHECK(!ContainsText(vertexShader, "whiteMixRandom"));
		LASER_INCREMENTAL_CHECK(!ContainsText(rendererSource,
			"const InkPoint rectPoints[2]"));
		LASER_INCREMENTAL_CHECK(!ContainsText(controllerSource,
			"renderer_.EmitLaserParticles("));
		LASER_INCREMENTAL_CHECK(ContainsText(controllerSource,
			"laserParticleSnapshot.hasActive || laserParticleSnapshot.expiredAny"));
		LASER_INCREMENTAL_CHECK(ContainsText(controllerSource,
			"if (shouldDrawLaserParticles)"));
		LASER_INCREMENTAL_CHECK(ContainsText(rendererSource,
			"if (laserScissorRasterState)"));
		LASER_INCREMENTAL_CHECK(ContainsText(rendererSource,
			"laserScissorRasterState.Reset();"));
		LASER_INCREMENTAL_CHECK(ContainsText(particleSource,
			"CreateBuffer(&bufferDescription, nullptr"));
		LASER_INCREMENTAL_CHECK(!ContainsText(particleSource,
			"emptyParticles"));
		LASER_INCREMENTAL_CHECK(ContainsText(root / "inkStrokeModelerTest" / "ink.hlsli",
			"LaserLiveCoverage : register(t9)"));
		LASER_INCREMENTAL_CHECK(ContainsText(root / "inkStrokeModelerTest" / "inkPixelShader.hlsl",
			"if (type == 13)"));
		LASER_INCREMENTAL_CHECK(ContainsText(root / "inkStrokeModelerTest" / "inkPixelShader.hlsl",
			"max(stableCoverage, liveCoverage)"));
		LASER_INCREMENTAL_CHECK(ContainsText(root / "inkStrokeModelerTest" / "inkVertexShader.hlsl",
			"type == 13"));
		LASER_INCREMENTAL_CHECK(ContainsText(root / "inkStrokeModelerTest" / "draw3" / "renderer.cpp",
			"PSSetShaderResources(6, ARRAYSIZE(nullLaserResources), nullLaserResources)"));
		LASER_INCREMENTAL_CHECK(ContainsText(root / "inkStrokeModelerTest" / "draw3" / "renderer.cpp",
			"nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr"));
	}

	if (failures == 0)
		std::cout << "All laser incremental coverage tests passed." << std::endl;
	return failures;
}

#undef LASER_INCREMENTAL_CHECK
