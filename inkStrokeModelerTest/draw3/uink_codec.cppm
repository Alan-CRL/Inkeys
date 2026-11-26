module;

#include <cstddef>
#include <cstdint>
#include <optional>
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

	// 从连续 MessagePack 对象流尽力读取 UInk 10–19；资源限额超出时保留可用页面，状态为 LimitExceeded 且禁止原地追加。
	UInkReadResult DecodeUInk(std::span<const std::byte> bytes,
		const UInkReadLimits& limits = {}) noexcept;

	// 按页读取时借用已校验的注册表；偏移为该段在文件内的起点。
	struct UInkStreamContext
	{
		uint16_t version = kUInkVersion;
		std::optional<UInkHeaderExtension> headerExtension;
		bool usesImplicitDevice = true;
		bool usesImplicitWorkspace = true;
		uint64_t baseOffset = 0;
	};

	// 整文件流式读取保留 Canvas 顺序和内容编号，先校验注册表，无跨 Canvas 校验。
	// 页段模式借用注册表，返回文档的 headerExtension 为空；识别到第二个 Canvas 即拒绝。
	// 页段诊断与失败位置使用文件偏移，validPrefixLength 仍为段内前缀的长度。
	UInkReadResult DecodeUInkStream(std::span<const std::byte> bytes,
		const UInkReadLimits& limits = {}, const UInkStreamContext* context = nullptr) noexcept;

	// 页目录读取可传入范围基址，取得识别到的 Canvas 数（包括被跳过或截断的对象）。
	UInkReadResult DecodeUInkStream(std::span<const std::byte> bytes,
		const UInkReadLimits& limits, const UInkStreamContext& context,
		uint64_t baseOffset, uint64_t& canvasCount) noexcept;

	// 整文件流式读取，同时取得识别到的 Canvas 数（包括类型可识别但字段无效而被丢弃的）。
	UInkReadResult DecodeUInkStream(std::span<const std::byte> bytes,
		const UInkReadLimits& limits, uint64_t& canvasCount) noexcept;

	// 写出规范化宽度的完整 Header/Extension/Canvas 对象流。
	UInkEncodeResult EncodeUInkDocument(const UInkDocument& document);

	// 编码按页文件的 Header 和可选注册表，统计由文件层提供。
	UInkEncodeResult EncodeUInkHeaderPrefix(const UInkHeader& header,
		const std::optional<UInkHeaderExtension>& extension);

	// 编码一个完整 Canvas 版本及其内容块。
	UInkEncodeResult EncodeUInkPage(const UInkCanvas& canvas, bool implicitWorkspace,
		bool implicitDevice, const UInkReadLimits& limits = {});
}
