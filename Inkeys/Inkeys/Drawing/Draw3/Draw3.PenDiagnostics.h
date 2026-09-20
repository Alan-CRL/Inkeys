#pragma once

#include <cstddef>
#include <cstdint>
#include <array>

namespace Inkeys::Drawing::Draw3
{
	// 仅隐藏集成启用；记录实际绘制尾部，不暴露活动笔画或渲染资源。
	struct PenRuntimeDiagnostics
	{
		std::uint64_t strokeId = 0;
		std::uint64_t inputSequence = 0;
		std::uint64_t modelUpdateCount = 0;
		std::size_t realPointCount = 0;
		std::size_t l0PointCount = 0;
		std::size_t committedRealIndex = 0;
		float endpointError = 0.0f;
		float tipRadius = 0.0f;
		float baseRadius = 0.0f;
		double displayTime = 0.0;
		double physicalUpTime = 0.0;
		std::uint32_t deviceType = 0;
		std::array<float, 2> rawMove = {}, rawUp = {};
		std::array<std::array<float, 2>, 8> modelTail = {}, acceptedTail = {};
		std::size_t modelTailCount = 0, acceptedTailCount = 0;
		bool tailTraceTruncated = false;
		bool terminalLocked = false;
		bool awaitingReconnect = false;
		bool active = false;
		bool recovering = false;
		bool frozen = false;
	};
}
