module;

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

export module Inkeys.UI.StartupPreview.Format;
export import Inkeys.UI.StartupPreview.State;

export namespace Inkeys::UI::StartupPreview
{
	inline constexpr std::size_t PreviewHeaderSize = 160;
	inline constexpr std::uint16_t PreviewFormatVersion = 1;
	inline constexpr std::uint32_t PreviewPixelFormat = 1;
	inline constexpr std::uint64_t PreviewMaximumPayloadSize = 64ull * 1024ull * 1024ull;
	inline constexpr std::uint32_t PreviewMaximumDimension = 8192;
	inline constexpr std::uint32_t PreviewFlagEmbedded = 1u << 0;
	inline constexpr std::uint32_t PreviewFlagDiskCache = 1u << 1;

	struct PreviewMetadata final
	{
		std::uint32_t layoutEpoch = 1;
		std::uint32_t pixelFormat = PreviewPixelFormat;
		std::uint32_t flags = PreviewFlagEmbedded;
		std::uint64_t captureRevision = 0;
		std::array<std::uint8_t, 32> visualSignature{};
		std::uint32_t width = 0;
		std::uint32_t height = 0;
		std::uint32_t stride = 0;
		std::uint64_t payloadSize = 0;
		std::uint32_t captureDpiX = 96;
		std::uint32_t captureDpiY = 96;
		std::uint32_t monitorPixelWidth = 0;
		std::uint32_t monitorPixelHeight = 0;
		std::uint32_t monitorWorkWidth = 0;
		std::uint32_t monitorWorkHeight = 0;
		std::int32_t windowOffsetX = 0;
		std::int32_t windowOffsetY = 0;
		std::int32_t anchorX = 0;
		std::int32_t anchorY = 0;
		std::int32_t progressLeft = 0;
		std::int32_t progressTop = 0;
		std::int32_t progressRight = 0;
		std::int32_t progressBottom = 0;
	};

	struct PreviewCompatibility final
	{
		std::uint32_t layoutEpoch = 1;
		std::array<std::uint8_t, 32> visualSignature{};
		std::uint32_t captureDpiX = 96;
		std::uint32_t captureDpiY = 96;
		std::uint32_t monitorPixelWidth = 0;
		std::uint32_t monitorPixelHeight = 0;
		std::uint32_t monitorWorkWidth = 0;
		std::uint32_t monitorWorkHeight = 0;
		std::int32_t windowOffsetX = 0;
		std::int32_t windowOffsetY = 0;
		std::int32_t anchorX = 0;
		std::int32_t anchorY = 0;
		std::int32_t progressLeft = 0;
		std::int32_t progressTop = 0;
		std::int32_t progressRight = 0;
		std::int32_t progressBottom = 0;
	};

	struct VisualButton final
	{
		std::string id;
		std::uint32_t widthDip = 0;
		std::uint32_t heightDip = 0;
		bool visible = true;
	};

	struct VisualRuntimeState final
	{
		std::int32_t selectedMode = 0;
		std::int32_t selectedPenMode = 0;
		std::int32_t selectedShapeMode = 0;
		std::uint32_t toolColorRgba = 0;
		std::int32_t toolWidthMilli = 0;
		bool folded = false;
		bool drawAttributeVisible = false;
		bool geometryAttributeVisible = false;
		bool morePanelVisible = false;
		bool mainBarRight = false;
		bool primaryBarBelow = false;
		std::int32_t bottomDockMode = 1;
		std::int32_t bottomDockCenterMode = 1;
		bool whiteboardActive = false;
		bool pptPresentationActive = false;
		bool currentPageHasContent = false;
	};

	struct VisualSignatureInputs final
	{
		std::uint32_t layoutEpoch = 1;
		std::string language = "zh-CN";
		std::string theme = "dark";
		std::uint32_t zoomPermille = 1000;
		bool expanded = true;
		std::int32_t dock = 0;
		std::int32_t position = 0;
		std::uint32_t visualResourceVersion = 1;
		std::uint32_t pixelFormat = PreviewPixelFormat;
		std::uint32_t alphaRuleVersion = 1;
		bool edgeLightingEnabled = true;
		bool dynamicEdgeLightingEnabled = false;
		bool debugOverlayEnabled = false;
		VisualRuntimeState runtimeState{};
		std::vector<VisualButton> fixedButtonsA1;
		std::vector<VisualButton> extensionButtons;
		std::vector<VisualButton> fixedButtonsA2;
	};

	enum class PreviewFormatError : std::uint8_t
	{
		None,
		Missing,
		TooSmall,
		TooLarge,
		Magic,
		UnsupportedVersion,
		HeaderSize,
		Reserved,
		PixelFormat,
		Flags,
		Dimension,
		Stride,
		PayloadSize,
		FileSize,
		Dpi,
		Geometry,
		Crc,
		Compatibility,
	};

	struct PreviewParseResult final
	{
		CacheState state = CacheState::Corrupt;
		PreviewFormatError error = PreviewFormatError::None;
		PreviewMetadata metadata{};
		std::vector<std::uint8_t> pixels;

		[[nodiscard]] bool HasPixels() const noexcept
		{
			return state == CacheState::Valid && !pixels.empty();
		}
	};

	[[nodiscard]] bool ReadLe16(std::span<const std::uint8_t> bytes,
		std::size_t offset, std::uint16_t& value) noexcept;
	[[nodiscard]] bool ReadLe32(std::span<const std::uint8_t> bytes,
		std::size_t offset, std::uint32_t& value) noexcept;
	[[nodiscard]] bool ReadLe64(std::span<const std::uint8_t> bytes,
		std::size_t offset, std::uint64_t& value) noexcept;
	[[nodiscard]] bool WriteLe16(std::span<std::uint8_t> bytes,
		std::size_t offset, std::uint16_t value) noexcept;
	[[nodiscard]] bool WriteLe32(std::span<std::uint8_t> bytes,
		std::size_t offset, std::uint32_t value) noexcept;
	[[nodiscard]] bool WriteLe64(std::span<std::uint8_t> bytes,
		std::size_t offset, std::uint64_t value) noexcept;

	[[nodiscard]] std::uint32_t ComputeIeeeCrc32(
		std::span<const std::uint8_t> bytes) noexcept;
	[[nodiscard]] std::uint32_t ComputePreviewCrc32(
		std::span<const std::uint8_t> bytes) noexcept;
	[[nodiscard]] std::array<std::uint8_t, 32> ComputeSha256(
		std::span<const std::uint8_t> bytes) noexcept;
	[[nodiscard]] std::array<std::uint8_t, 32> BuildVisualSignature(
		const VisualSignatureInputs& inputs) noexcept;
	[[nodiscard]] PreviewCompatibility MakeCompatibility(
		const PreviewMetadata& metadata) noexcept;
	[[nodiscard]] bool IsCompatible(const PreviewMetadata& metadata,
		const PreviewCompatibility& compatibility) noexcept;
	[[nodiscard]] bool TryReadPreviewMetadata(
		std::span<const std::uint8_t> header, std::uint64_t fileSize,
		PreviewMetadata& metadata, PreviewFormatError& error) noexcept;
	[[nodiscard]] std::vector<std::uint8_t> SerializePreview(
		PreviewMetadata metadata, std::span<const std::uint8_t> pixels);
	[[nodiscard]] PreviewParseResult MissingPreview() noexcept;
	[[nodiscard]] PreviewParseResult ParsePreview(
		std::span<const std::uint8_t> bytes,
		const PreviewCompatibility* compatibility = nullptr);
}
