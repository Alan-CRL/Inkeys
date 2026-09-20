module;

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

export module draw3.uink_codec;

export import draw3.uink_model;

export namespace draw3::uink
{
	enum class UInkEncodeStatus : uint8_t
	{
		Success,
		InvalidModel,
		LimitExceeded,
		InternalError
	};

	struct UInkEncodeResult
	{
		UInkEncodeStatus status = UInkEncodeStatus::InvalidModel;
		std::vector<std::byte> bytes;
		std::vector<UInkDiagnostic> diagnostics;
	};

	// 从连续 MessagePack 对象流读取 UInk 10；资源限额超出时不返回部分文档。
	UInkReadResult DecodeUInk(std::span<const std::byte> bytes,
		const UInkReadLimits& limits = {});

	// 写出规范化宽度的完整 Header/Extension/Canvas 对象流。
	UInkEncodeResult EncodeUInkDocument(const UInkDocument& document);

	// 为 append 预编码完整顶层对象，不打开或修改目标文件。
	UInkEncodeResult EncodeUInkAppendObjects(std::span<const UInkAppendObject> objects);
}
