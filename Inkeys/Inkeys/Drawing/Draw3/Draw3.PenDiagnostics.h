#pragma once

#include <cstddef>
#include <cstdint>

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
		bool active = false;
		bool recovering = false;
		bool frozen = false;
	};
}
