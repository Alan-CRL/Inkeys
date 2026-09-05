module;

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

export module draw3.uink_draw3_import;

export import draw3.uink_draw3_export;

export namespace draw3::uink
{
	inline constexpr int32_t kInkeysPageIndexWorkspaceType = 128;

	enum class Draw3UInkImportBindingMode : uint8_t
	{
		StableSlideId,
		PageIndexFallback,
	};

	struct Draw3UInkImportExpectation
	{
		UInkGuid fileGuid;
		std::string hostId;
		Draw3UInkImportBindingMode bindingMode =
			Draw3UInkImportBindingMode::PageIndexFallback;
		std::vector<int32_t> slideIds;
		std::vector<int32_t> knownSlideIds;
		uint32_t pageCount = 0;
	};

	enum class Draw3UInkImportStatus : uint8_t
	{
		Success,
		IdentityMismatch,
		TopologyMismatch,
		UnsupportedContent,
		InvalidDocument,
	};

	struct Draw3UInkImportResult
	{
		Draw3UInkImportStatus status = Draw3UInkImportStatus::InvalidDocument;
		std::optional<Draw3UInkExportSnapshot> snapshot;
		std::string error;
	};

	UInkExtra MakeInkeysBindingExtra(Draw3UInkImportBindingMode mode);
	bool HasInkeysBindingExtra(const std::optional<UInkExtra>& extra,
		Draw3UInkImportBindingMode mode) noexcept;
	bool HasInkeysPageStateExtra(const std::optional<UInkExtra>& extra,
		bool retained) noexcept;
	Draw3UInkImportResult ImportApplicationOwnedPresentation(
		const UInkDocument& document,
		const Draw3UInkImportExpectation& expectation) noexcept;
}
