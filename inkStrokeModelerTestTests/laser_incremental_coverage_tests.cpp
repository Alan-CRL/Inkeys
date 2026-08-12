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

	std::string ReadText(const std::filesystem::path& path)
	{
		std::ifstream input(path, std::ios::binary);
		if (!input) return {};
		return std::string((std::istreambuf_iterator<char>(input)),
			std::istreambuf_iterator<char>());
	}

	bool ContainsText(const std::filesystem::path& path, const std::string& text)
	{
		const std::string contents = ReadText(path);
		return contents.find(text) != std::string::npos;
	}

	std::string TextBetween(const std::string& contents,
		const std::string& first, const std::string& second)
	{
		const size_t firstPosition = contents.find(first);
		if (firstPosition == std::string::npos) return {};
		const size_t secondPosition = contents.find(second, firstPosition + first.size());
		if (secondPosition == std::string::npos) return {};
		return contents.substr(firstPosition, secondPosition - firstPosition);
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

	// Fade 尾端的新 Down 会把整组激光恢复满亮，必须把这次跃迁识别为 opacity 变化。
	draw3::LaserTrailLifecycle opacityLifecycle;
	draw3::BeginLaserContact(opacityLifecycle);
	draw3::EndLaserContact(opacityLifecycle, 1000);
	const float fadedOpacity = draw3::EvaluateLaserTrailOpacity(
		opacityLifecycle, 2790, 1000, 1.0);
	LASER_INCREMENTAL_CHECK(fadedOpacity > 0.0f && fadedOpacity < 0.02f);
	const float preInputOpacity = fadedOpacity;
	draw3::BeginLaserContact(opacityLifecycle);
	const float restoredOpacity = draw3::EvaluateLaserTrailOpacity(
		opacityLifecycle, 2790, 1000, 1.0);
	LASER_INCREMENTAL_CHECK(NearlyEqual(restoredOpacity, 1.0f));
	LASER_INCREMENTAL_CHECK(
		std::abs(preInputOpacity - restoredOpacity) > 0.0001f);

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
		const std::filesystem::path rendererLaserSource =
			root / "inkStrokeModelerTest" / "draw3" / "renderer_laser.cpp";
		const std::filesystem::path rendererPrimitivesSource =
			root / "inkStrokeModelerTest" / "draw3" / "renderer_primitives.cpp";
		const std::filesystem::path controllerSource =
			root / "inkStrokeModelerTest" / "draw3" / "drawing_controller.cpp";
		const std::filesystem::path windowControlSource =
			root / "inkStrokeModelerTest" / "draw3" / "window_control.cpp";
		const std::filesystem::path contactInputSource =
			root / "inkStrokeModelerTest" / "draw3" / "contact_input.cpp";
		const std::filesystem::path realtimeStylusSource =
			root / "inkStrokeModelerTest" / "draw3" / "realtime_stylus.cpp";
		const std::filesystem::path win7CompatSource =
			root / "inkStrokeModelerTest" / "draw3" / "win7_compat.cpp";
		const std::filesystem::path hapticSource =
			root / "inkStrokeModelerTest" / "draw3" / "haptic_feedback.cpp";
		const std::filesystem::path runtimeMetricsSource =
			root / "inkStrokeModelerTest" / "draw3" / "runtime_metrics.cpp";
		const std::filesystem::path presentationSource =
			root / "inkStrokeModelerTest" / "draw3" / "transparent_presentation.cpp";
		const std::filesystem::path diagnosticsSource =
			root / "inkStrokeModelerTest" / "draw3" / "diagnostics.cpp";
		const std::filesystem::path particleSystemSource =
			root / "inkStrokeModelerTest" / "draw3" / "laser_particle_system.cpp";
		const std::filesystem::path performanceHudSource =
			root / "inkStrokeModelerTest" / "draw3" / "window_performance_hud.cpp";
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
		LASER_INCREMENTAL_CHECK(ContainsText(rendererLaserSource,
			"StepLaserParticles("));
		LASER_INCREMENTAL_CHECK(ContainsText(rendererLaserSource,
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
		// 无窗口测试通过源码契约锁定设置只在值变化时唤醒一次。
		const std::string controllerText = ReadText(controllerSource);
		const std::string contactCursorSetter = TextBetween(controllerText,
			"void DrawingController::SetDrawingCursorDuringContactEnabled(bool enabled) noexcept",
			"bool DrawingController::GetDrawingCursorDuringContactEnabled() const noexcept");
		LASER_INCREMENTAL_CHECK(!contactCursorSetter.empty());
		LASER_INCREMENTAL_CHECK(contactCursorSetter.find(
			"drawingCursorDuringContactEnabled_.exchange(") != std::string::npos);
		LASER_INCREMENTAL_CHECK(contactCursorSetter.find(
			"std::memory_order_acq_rel") != std::string::npos);
		LASER_INCREMENTAL_CHECK(contactCursorSetter.find(
			"== enabled) return;") != std::string::npos);
		const size_t firstContactCursorWake = contactCursorSetter.find(
			"input_.PublishControlWake()");
		LASER_INCREMENTAL_CHECK(firstContactCursorWake != std::string::npos);
		LASER_INCREMENTAL_CHECK(contactCursorSetter.find(
			"input_.PublishControlWake()", firstContactCursorWake + 1) == std::string::npos);
		const std::string contactCursorGetter = TextBetween(controllerText,
			"bool DrawingController::GetDrawingCursorDuringContactEnabled() const noexcept",
			"void DrawingController::SetLaserParticlesEnabled(bool enabled) noexcept");
		LASER_INCREMENTAL_CHECK(contactCursorGetter.find(
			"drawingCursorDuringContactEnabled_.load(std::memory_order_acquire)") !=
			std::string::npos);
		const std::string translucentCursorSetter = TextBetween(controllerText,
			"void DrawingController::SetTranslucentInkCursorEnabled(bool enabled) noexcept",
			"bool DrawingController::GetTranslucentInkCursorEnabled() const noexcept");
		LASER_INCREMENTAL_CHECK(translucentCursorSetter.find(
			"translucentInkCursorEnabled_.exchange(") != std::string::npos);
		LASER_INCREMENTAL_CHECK(translucentCursorSetter.find(
			"input_.PublishControlWake()") != std::string::npos);
		const std::string translucentCursorGetter = TextBetween(controllerText,
			"bool DrawingController::GetTranslucentInkCursorEnabled() const noexcept",
			"void DrawingController::SetMouseUsesSystemCursor(bool enabled) noexcept");
		LASER_INCREMENTAL_CHECK(translucentCursorGetter.find(
			"translucentInkCursorEnabled_.load(std::memory_order_acquire)") !=
			std::string::npos);
		const std::string mouseCursorSetter = TextBetween(controllerText,
			"void DrawingController::SetMouseUsesSystemCursor(bool enabled) noexcept",
			"bool DrawingController::GetMouseUsesSystemCursor() const noexcept");
		LASER_INCREMENTAL_CHECK(mouseCursorSetter.find(
			"window_.SetMouseUsesSystemCursor(enabled)") != std::string::npos);
		LASER_INCREMENTAL_CHECK(ContainsText(controllerSource,
			"const bool mouseUsesSystemCursor = window_.GetMouseUsesSystemCursor()"));
		LASER_INCREMENTAL_CHECK(ContainsText(windowControlSource,
			"return draw3::ShouldIgnoreMouseCursorMessage("));
		LASER_INCREMENTAL_CHECK(ContainsText(windowControlSource,
			"ResolveGetPointerType() != nullptr"));
		LASER_INCREMENTAL_CHECK(ContainsText(windowControlSource,
			"initialExtendedStyle_ = WS_EX_TOPMOST"));
		LASER_INCREMENTAL_CHECK(ContainsText(windowControlSource, "WS_EX_TOPMOST"));
		LASER_INCREMENTAL_CHECK(ContainsText(windowControlSource, "SW_SHOWNORMAL"));
		LASER_INCREMENTAL_CHECK(!ContainsText(windowControlSource, "WS_EX_NOACTIVATE"));
		LASER_INCREMENTAL_CHECK(!ContainsText(windowControlSource, "SW_SHOWNOACTIVATE"));
		LASER_INCREMENTAL_CHECK(!ContainsText(windowControlSource, "case WM_MOUSEACTIVATE:"));
		LASER_INCREMENTAL_CHECK(ContainsText(windowControlSource,
			"monitorRect.bottom - monitorRect.top - 1L"));
		LASER_INCREMENTAL_CHECK(TextAppearsBefore(controllerSource,
			"const float preInputLaserOpacity = laserOpacity;",
			"if (!interruptedStrokeReconnectEnabled)"));
		LASER_INCREMENTAL_CHECK(ContainsText(controllerSource,
			"laserParticleSnapshot.hasActive || laserParticleSnapshot.expiredAny"));
		LASER_INCREMENTAL_CHECK(ContainsText(controllerSource,
			"if (shouldDrawLaserParticles)"));
		LASER_INCREMENTAL_CHECK(ContainsText(rendererLaserSource,
			"if (laserScissorRasterState)"));
		LASER_INCREMENTAL_CHECK(ContainsText(rendererSource,
			"laserScissorRasterState.Reset();"));
		LASER_INCREMENTAL_CHECK(ContainsText(particleSystemSource,
			"CreateBuffer(&bufferDescription, nullptr"));
		LASER_INCREMENTAL_CHECK(!ContainsText(particleSystemSource,
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
		// 性能 HUD 必须保持独立、点击穿透且只由物理绘制帧低频更新。
		LASER_INCREMENTAL_CHECK(ContainsText(performanceHudSource, "WS_EX_LAYERED"));
		LASER_INCREMENTAL_CHECK(ContainsText(performanceHudSource, "WS_EX_TRANSPARENT"));
		LASER_INCREMENTAL_CHECK(ContainsText(performanceHudSource, "WS_EX_TOOLWINDOW"));
		LASER_INCREMENTAL_CHECK(ContainsText(performanceHudSource, "WS_EX_NOACTIVATE"));
		LASER_INCREMENTAL_CHECK(ContainsText(performanceHudSource, "UpdateLayeredWindow("));
		LASER_INCREMENTAL_CHECK(ContainsText(performanceHudSource, "Consolas"));
		LASER_INCREMENTAL_CHECK(ContainsText(performanceHudSource, "MulDiv(360"));
		LASER_INCREMENTAL_CHECK(ContainsText(performanceHudSource, "availableWidth"));
		LASER_INCREMENTAL_CHECK(!ContainsText(performanceHudSource, "contactFontHeight"));
		LASER_INCREMENTAL_CHECK(ContainsText(performanceHudSource,
			"performanceHudRefreshPosted_.exchange("));
		LASER_INCREMENTAL_CHECK(!ContainsText(performanceHudSource, "WM_TIMER"));
		LASER_INCREMENTAL_CHECK(!ContainsText(performanceHudSource, "frameDirty"));
		LASER_INCREMENTAL_CHECK(!ContainsText(performanceHudSource, "backBuffer"));
		LASER_INCREMENTAL_CHECK(ContainsText(controllerSource,
			"if (frameHadActiveContact && performanceHudEnabled)"));
		LASER_INCREMENTAL_CHECK(ContainsText(controllerSource,
			"window_.UpdatePerformanceHudText(performanceHudTracker_.FormatText("));
		LASER_INCREMENTAL_CHECK(!ContainsText(controllerSource, "QueryVideoMemoryUsageMiB"));
		LASER_INCREMENTAL_CHECK(!ContainsText(controllerSource, "performanceHudContacts_"));
		LASER_INCREMENTAL_CHECK(ContainsText(controllerSource,
			"performanceHudTracker_.EndDrawingFrameSequence();"));
		LASER_INCREMENTAL_CHECK(ContainsText(controllerSource,
			"WaitForFrameDeadline(remainingFrameBudgetMs)"));
		LASER_INCREMENTAL_CHECK(!ContainsText(controllerSource,
			"WaitForWake(frameWakeGeneration, remainingFrameBudgetMs)"));
		// 安全回退必须显式失败，不能继续消费旧 GPU 数据或不受信任的数值/模块路径。
		LASER_INCREMENTAL_CHECK(ContainsText(rendererPrimitivesSource,
			"FAILED(context->Map(inkDataBuffer.Get()"));
		LASER_INCREMENTAL_CHECK(ContainsText(rendererPrimitivesSource,
			"FAILED(context->Map(globalCB.Get()"));
		LASER_INCREMENTAL_CHECK(ContainsText(contactInputSource,
			"kMaximumContactSlotCapacity = 4096"));
		LASER_INCREMENTAL_CHECK(ContainsText(contactInputSource,
			"TryComputeQpcDeadline("));
		LASER_INCREMENTAL_CHECK(ContainsText(realtimeStylusSource,
			"kMaximumPacketPropertyCount = 256"));
		LASER_INCREMENTAL_CHECK(ContainsText(realtimeStylusSource,
			"propertyCount != decoder.propertyCount"));
		const std::string realtimeStylusText = ReadText(realtimeStylusSource);
		const std::string inAirPacketsBody = TextBetween(realtimeStylusText,
			"HRESULT STDMETHODCALLTYPE InAirPackets(",
			"HRESULT STDMETHODCALLTYPE Packets(");
		const std::string packetsBody = TextBetween(realtimeStylusText,
			"HRESULT STDMETHODCALLTYPE Packets(",
			"HRESULT STDMETHODCALLTYPE CustomStylusDataAdded(");
		LASER_INCREMENTAL_CHECK(!inAirPacketsBody.empty());
		LASER_INCREMENTAL_CHECK(!packetsBody.empty());
		const std::array forbiddenPacketStateAccess = {
			"ResolveContextDecoder", "EnsureContextDecoder", "BuildContextDecoder",
			"RebuildCurrentContextDecoders", "GetAllTabletContextIds",
			"GetPacketDescriptionData", "GetTabletFromTabletContextId",
			"GetTabletContextIdFromTablet", "QueryInterface", "IInkTablet2", "source->",
			"CoTaskMemFree", "RtsStateWriterGuard", "std::lock_guard", "std::mutex",
			"new ", "std::vector", "std::wstring" };
		for (const char* forbidden : forbiddenPacketStateAccess)
		{
			LASER_INCREMENTAL_CHECK(inAirPacketsBody.find(forbidden) == std::string::npos);
			LASER_INCREMENTAL_CHECK(packetsBody.find(forbidden) == std::string::npos);
		}
		LASER_INCREMENTAL_CHECK(inAirPacketsBody.find("RtsPacketStateGuard") !=
			std::string::npos);
		LASER_INCREMENTAL_CHECK(inAirPacketsBody.find("FindCachedContextDecoder") !=
			std::string::npos);
		LASER_INCREMENTAL_CHECK(inAirPacketsBody.find("RecordCallback(\"InAirPackets\"") !=
			std::string::npos);
		LASER_INCREMENTAL_CHECK(packetsBody.find("RtsPacketStateGuard") !=
			std::string::npos);
		LASER_INCREMENTAL_CHECK(packetsBody.find("activeBindings_.Find") !=
			std::string::npos);
		const std::string errorBody = TextBetween(realtimeStylusText,
			"HRESULT STDMETHODCALLTYPE Error(",
			"HRESULT STDMETHODCALLTYPE UpdateMapping(");
		LASER_INCREMENTAL_CHECK(errorBody.find("RtsStateWriterGuard") != std::string::npos);
		LASER_INCREMENTAL_CHECK(errorBody.find("ResetActiveContactState(true)") !=
			std::string::npos);
		LASER_INCREMENTAL_CHECK(errorBody.find("ResetDecoderLifecycleState") ==
			std::string::npos);
		LASER_INCREMENTAL_CHECK(!ContainsText(hapticSource,
			"LoadLibraryW(L\"combase.dll\")"));
		LASER_INCREMENTAL_CHECK(!ContainsText(runtimeMetricsSource,
			"LoadLibraryW(L\"psapi.dll\")"));
		LASER_INCREMENTAL_CHECK(!ContainsText(runtimeMetricsSource, "GetProcessTimes("));
		LASER_INCREMENTAL_CHECK(!ContainsText(runtimeMetricsSource,
			"GetProcessMemoryInfo"));
		LASER_INCREMENTAL_CHECK(!ContainsText(runtimeMetricsSource,
			"QueryVideoMemoryInfo"));
		LASER_INCREMENTAL_CHECK(!ContainsText(presentationSource,
			"LoadLibraryW(L\"dcomp.dll\")"));
		LASER_INCREMENTAL_CHECK(ContainsText(hapticSource,
			"GetSystemDirectoryW("));
		LASER_INCREMENTAL_CHECK(!ContainsText(runtimeMetricsSource,
			"GetSystemDirectoryW("));
		LASER_INCREMENTAL_CHECK(ContainsText(presentationSource,
			"GetSystemDirectoryW("));
		LASER_INCREMENTAL_CHECK(!ContainsText(hapticSource,
			"LOAD_LIBRARY_SEARCH_SYSTEM32"));
		LASER_INCREMENTAL_CHECK(!ContainsText(runtimeMetricsSource,
			"LOAD_LIBRARY_SEARCH_SYSTEM32"));
		LASER_INCREMENTAL_CHECK(!ContainsText(presentationSource,
			"LOAD_LIBRARY_SEARCH_SYSTEM32"));
		LASER_INCREMENTAL_CHECK(ContainsText(win7CompatSource,
			"__imp_GetSystemTimePreciseAsFileTime"));
		LASER_INCREMENTAL_CHECK(ContainsText(win7CompatSource,
			"GetProcAddress(kernel32, \"GetSystemTimePreciseAsFileTime\")"));
		LASER_INCREMENTAL_CHECK(ContainsText(win7CompatSource,
			"GetSystemTimeAsFileTime(value)"));
		LASER_INCREMENTAL_CHECK(ContainsText(presentationSource,
			"const LONG copyWidth = std::min"));
		LASER_INCREMENTAL_CHECK(ContainsText(presentationSource,
			"dirty.right = std::min(copyWidth, dirty.right)"));
		LASER_INCREMENTAL_CHECK(ContainsText(diagnosticsSource,
			"std::string result(static_cast<size_t>(requiredSize), '\\0')"));
		LASER_INCREMENTAL_CHECK(ContainsText(diagnosticsSource,
			"written != requiredSize"));
	}

	if (failures == 0)
		std::cout << "All laser incremental coverage tests passed." << std::endl;
	return failures;
}

#undef LASER_INCREMENTAL_CHECK
