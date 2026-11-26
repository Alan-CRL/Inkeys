module;

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <variant>
#include <vector>

export module draw3.uink_model;

export namespace draw3::uink
{
	inline constexpr uint16_t kUInkVersion = 11;
	inline constexpr uint16_t kUInkMinReadableVersion = 10;
	inline constexpr uint16_t kUInkMaxReadableVersion = 19;
	inline constexpr uint32_t kUInkMaxRegistryEntries = 1024;
	inline constexpr uint32_t kUInkMaxShapeVertices = 100000;
	inline constexpr double kUInkMaxCoordinate = 1e7;
	inline constexpr uint16_t kHeaderType = 0;
	inline constexpr uint16_t kHeaderExtensionType = 1;
	inline constexpr uint16_t kCanvasType = 2;
	inline constexpr uint16_t kInkType = 3;
	inline constexpr uint16_t kMediaType = 4;
	inline constexpr uint16_t kShapeType = 5;
	inline constexpr uint16_t kClearType = 6;

	class UInkGuid
	{
	public:
		UInkGuid() noexcept = default;
		explicit UInkGuid(std::array<uint8_t, 16> bytes) noexcept;

		const std::array<uint8_t, 16>& Bytes() const noexcept;
		bool IsZero() const noexcept;
		friend bool operator==(const UInkGuid&, const UInkGuid&) noexcept = default;

	private:
		std::array<uint8_t, 16> bytes_ = {};
	};

	static_assert(sizeof(UInkGuid) == 16);

	// UUID 只接受规范的小写/大写十六进制连字符形式，写出时统一为小写。
	std::optional<UInkGuid> ParseUInkGuid(const std::string& text) noexcept;
	std::string FormatUInkGuid(const UInkGuid& guid);
	std::optional<UInkGuid> CreateUInkGuid() noexcept;

	struct UInkMessagePackExtension
	{
		int8_t type = 0;
		std::vector<std::byte> data;
	};

	struct UInkMessagePackValue
	{
		using Array = std::vector<UInkMessagePackValue>;
		using Map = std::vector<std::pair<UInkMessagePackValue, UInkMessagePackValue>>;
		using Storage = std::variant<std::monostate, bool, int64_t, uint64_t, float, double,
			std::string, std::vector<std::byte>, Array, Map, UInkMessagePackExtension>;

		Storage value;
	};

	struct UInkHeader
	{
		UInkGuid guid;
		uint32_t deviceNum = 1;
		uint32_t workspaceNum = 1;
		uint32_t pageNum = 0;
		uint64_t time = 0;
	};

	struct UInkHardware
	{
		std::optional<std::string> name;
		std::optional<std::string> id;
		std::vector<std::pair<std::string, std::string>> identifiers;
		std::optional<uint32_t> physicalWidth;
		std::optional<uint32_t> physicalHeight;
		std::optional<float> scaleFactor;
	};

	struct UInkDisplayDevice
	{
		int32_t x = 0;
		int32_t y = 0;
		uint32_t width = 0;
		uint32_t height = 0;
	};

	struct UInkWindowDevice
	{
		UInkGuid parentDeviceGuid;
		float x = 0.0f;
		float y = 0.0f;
		float width = 0.0f;
		float height = 0.0f;
		uint32_t zIndex = 0;
	};

	struct UInkUnknownDevice
	{
	};

	struct UInkDevice
	{
		UInkGuid guid;
		int32_t deviceType = 0;
		std::optional<std::string> name;
		std::optional<UInkHardware> hardware;
		std::variant<UInkDisplayDevice, UInkWindowDevice, UInkUnknownDevice> geometry;
		bool parentResolved = true;
		bool usable = true;
	};

	struct UInkWorkspace
	{
		UInkGuid guid;
		int32_t workspaceType = 0;
		std::optional<std::string> name;
		std::optional<UInkGuid> parentWorkspaceGuid;
		std::optional<std::string> hostId;
		uint32_t currentPageIndex = 0;
		bool parentResolved = true;
		bool usable = true;
	};

	struct UInkHeaderExtension
	{
		std::optional<std::string> name;
		std::optional<std::string> explanation;
		std::vector<UInkDevice> devices;
		std::vector<UInkWorkspace> workspaces;
	};

	struct UInkViewport
	{
		float x = 0.0f;
		float y = 0.0f;
		float scale = 1.0f;
	};

	enum class UInkColorSpace : uint8_t
	{
		Srgb,
		ScRgb
	};

	struct UInkExtendedColor
	{
		UInkColorSpace space = UInkColorSpace::Srgb;
		std::array<float, 3> components = {};
	};

	struct UInkColor
	{
		uint32_t fallbackRgb = 0;
		std::optional<UInkExtendedColor> extended;
	};

	enum class UInkInkKind : uint8_t
	{
		Erase,
		Pen,
		Highlighter,
		AdvancedHighlighter
	};

	struct UInkPointStyle
	{
		UInkColor color;
		float opacity = 1.0f;
	};

	struct UInkInkPoint
	{
		float x = 0.0f;
		float y = 0.0f;
		float width = 0.0f;
		std::optional<UInkPointStyle> style;
	};

	struct UInkNib
	{
		int32_t shape = 0;
		std::optional<float> aspect;
		int32_t orientation = 0;
	};

	struct UInkInk
	{
		uint32_t contentId = 0;
		uint32_t undoId = 0;
		int32_t declaredInkType = 1;
		UInkInkKind effectiveKind = UInkInkKind::Pen;
		UInkColor color;
		float opacity = 1.0f;
		int32_t declaredTexture = 0;
		int32_t effectiveTexture = 0;
		std::vector<UInkInkPoint> points;
		bool renderOnlyWhenLatest = false;
		std::optional<UInkNib> nib;
		std::optional<int32_t> cap;
	};

	struct UInkShapePoint
	{
		float x = 0.0f;
		float y = 0.0f;
	};

	struct UInkLineGeometry
	{
		std::vector<UInkShapePoint> points;
	};

	struct UInkRectangleGeometry
	{
		float centerX = 0.0f;
		float centerY = 0.0f;
		float width = 0.0f;
		float height = 0.0f;
		float rotation = 0.0f;
		float cornerRadiusX = 0.0f;
		float cornerRadiusY = 0.0f;
	};

	struct UInkSquareGeometry
	{
		float centerX = 0.0f;
		float centerY = 0.0f;
		float size = 0.0f;
		float rotation = 0.0f;
	};

	struct UInkEllipseGeometry
	{
		float centerX = 0.0f;
		float centerY = 0.0f;
		float width = 0.0f;
		float height = 0.0f;
		float rotation = 0.0f;
	};

	struct UInkCircleGeometry
	{
		float centerX = 0.0f;
		float centerY = 0.0f;
		float radius = 0.0f;
	};

	using UInkShapeGeometry = std::variant<UInkLineGeometry, UInkRectangleGeometry,
		UInkSquareGeometry, UInkEllipseGeometry, UInkCircleGeometry>;

	struct UInkShapeStroke
	{
		UInkColor color;
		float opacity = 1.0f;
		float width = 1.0f;
		std::vector<float> dashArray;
		float dashOffset = 0.0f;
		int32_t declaredStartMarker = 0;
		int32_t declaredEndMarker = 0;
		int32_t effectiveStartMarker = 0;
		int32_t effectiveEndMarker = 0;
		std::optional<int32_t> lineCap;
		std::optional<int32_t> lineJoin;
	};

	struct UInkShapeFill
	{
		int32_t declaredFillType = 0;
		UInkColor color;
		float opacity = 1.0f;
	};

	struct UInkShapeObject
	{
		UInkGuid id;
		uint64_t revision = 0;
		std::optional<std::string> preset;
		uint32_t partCount = 0;
		std::optional<std::array<float, 6>> transform;
		std::optional<UInkMessagePackValue::Map> construction;
		std::optional<UInkMessagePackValue::Map> style;
		std::optional<uint32_t> aux;
	};

	struct UInkShapeObjectRef
	{
		UInkGuid ref;
		uint32_t partIndex = 0;
	};

	struct UInkShape
	{
		uint32_t contentId = 0;
		uint32_t undoId = 0;
		int32_t declaredShapeType = 0;
		UInkShapeGeometry geometry;
		std::optional<UInkShapeStroke> stroke;
		std::optional<UInkShapeFill> fill;
		bool renderOnlyWhenLatest = false;
		std::optional<std::variant<UInkShapeObject, UInkShapeObjectRef>> object;
	};

	enum class UInkMediaResourceState : uint8_t
	{
		Unavailable
	};

	struct UInkMedia
	{
		uint32_t contentId = 0;
		uint32_t undoId = 0;
		std::string path;
		std::string mimeType;
		std::optional<float> width;
		std::optional<float> height;
		std::array<float, 6> transform = { 1.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f };
		float opacity = 1.0f;
		std::optional<uint32_t> pageCount;
		uint32_t pageIndex = 0;
		bool autoplay = false;
		bool loop = false;
		float volume = 1.0f;
		double startTime = 0.0;
		float playbackRate = 1.0f;
		UInkMediaResourceState resourceState = UInkMediaResourceState::Unavailable;
		bool pathIsSafe = false;
	};

	struct UInkClear
	{
		uint32_t contentId = 0;
		uint32_t undoId = 0;
	};

	using UInkContent = std::variant<UInkInk, UInkShape, UInkMedia, UInkClear>;

	struct UInkBackground
	{
		UInkColor color;
		int32_t pattern = 0;
	};

	struct UInkCanvas
	{
		std::optional<UInkGuid> workspaceGuid;
		std::optional<UInkGuid> deviceGuid;
		UInkGuid pageGuid;
		uint32_t pageIndex = 0;
		uint32_t pageNumber = 0;
		uint32_t layerIndex = 0;
		uint32_t layerNumber = 0;
		std::optional<int32_t> slideId;
		std::optional<UInkViewport> viewport;
		std::vector<UInkContent> content;
		bool temporaryWorkspace = false;
		bool temporaryDevice = false;
		bool temporaryPage = false;
		bool temporaryLayer = false;
		bool presentationUnbound = false;
		std::optional<UInkBackground> background;
		std::optional<int32_t> pageState;
		std::optional<int32_t> pageKind;
		std::optional<int32_t> binding;
	};

	// 声明值保留用于读写，有效值用于解释笔尖和页面元数据。
	bool HasInvalidUInkNibAspect(const UInkNib& nib) noexcept;
	UInkNib EffectiveUInkNib(const std::optional<UInkNib>& nib) noexcept;
	int32_t EffectiveUInkCap(const UInkInk& ink) noexcept;
	int32_t EffectiveUInkLineCap(const UInkShapeStroke& stroke) noexcept;
	int32_t EffectiveUInkLineJoin(const UInkShapeStroke& stroke) noexcept;
	int32_t EffectiveUInkBackgroundPattern(const UInkBackground& background) noexcept;
	int32_t EffectiveUInkPageState(const UInkCanvas& canvas) noexcept;
	int32_t EffectiveUInkPageKind(const UInkCanvas& canvas) noexcept;
	int32_t EffectiveUInkBinding(const UInkCanvas& canvas) noexcept;
	// 检查 PPT 页面第 0 层的 SlideID 要求，包含旧私有标记的兼容回退。
	bool HasValidUInkPresentationBinding(const UInkCanvas& layerZero) noexcept;

	struct UInkDocument
	{
		UInkHeader header;
		std::optional<UInkHeaderExtension> headerExtension;
		std::vector<UInkCanvas> canvases;
		bool usesImplicitDevice = true;
		bool usesImplicitWorkspace = true;
	};

	enum class UInkDiagnosticSeverity : uint8_t
	{
		Info,
		Warning,
		Error,
		Fatal
	};

	enum class UInkDiagnosticCode : uint16_t
	{
		None,
		IoError,
		InvalidHeader,
		UnsupportedVersion,
		MalformedMessagePack,
		TruncatedTail,
		LimitExceeded,
		DiagnosticsTruncated,
		DuplicateKnownKey,
		MissingRequiredField,
		InvalidFieldType,
		InvalidFieldValue,
		InvalidObjectOrder,
		UnknownTopLevelType,
		KnownBlockSkipped,
		NumericCompatibility,
		FieldFallback,
		TemporaryIdentity,
		DuplicateGuid,
		ParentCycle,
		HeaderSnapshotMismatch,
		ContentSequenceRecovered,
		UnsafeMediaPath,
		MediaResourceUnavailable,
		WriteFailed,
		FlushFailed,
		RollbackFailed,
		SelfValidationFailed,
		CoordinateOutOfRange,
		DecodeFailed
	};

	struct UInkDiagnostic
	{
		UInkDiagnosticCode code = UInkDiagnosticCode::None;
		UInkDiagnosticSeverity severity = UInkDiagnosticSeverity::Info;
		uint64_t byteOffset = 0;
		uint64_t objectIndex = 0;
		std::string fieldPath;
		uint32_t systemError = 0;
		// Canvas 按对象流从 1 编号，含可识别但被跳过的 Canvas。0 表示文件级诊断。
		uint64_t canvasOrdinal = 0;
	};

	struct UInkReadLimits
	{
		uint64_t maxFileBytes = 128ull * 1024ull * 1024ull;
		uint64_t maxModelCharge = 256ull * 1024ull * 1024ull;
		uint64_t maxTopLevelObjects = 250000;
		uint64_t maxTopLevelObjectBytes = 32ull * 1024ull * 1024ull;
		uint64_t maxGeometryPoints = 4000000;
		uint64_t maxSingleGeometryPoints = 1000000;
		uint32_t maxDepth = 32;
		uint64_t maxStringOrBinaryBytes = 8ull * 1024ull * 1024ull;
		uint64_t maxContainerEntries = 1000000;
		uint32_t maxDiagnostics = 4096;
		uint32_t maxMediaPathBytes = 32768;
	};

	enum class UInkReadStatus : uint8_t
	{
		Complete,
		RecoveredTruncatedTail,
		RecoveredInvalidTail,
		StoppedAtCorruptTail,
		RejectedHeader,
		LimitExceeded,
		IoError
	};

	struct UInkLoadProvenance
	{
		bool containsUnknownTopLevel = false;
		bool usedFieldFallback = false;
		bool usedTemporaryIdentity = false;
		bool containsInvalidCompleteBlocks = false;
		bool invalidBlockBeforeValidBlock = false;
		bool contentSequenceRecovered = false;
		bool requiresSaveAs = false;
	};

	enum class UInkReadOutcome : uint8_t
	{
		Complete,
		Partial,
		Failed
	};

	struct UInkDiagnosticCount
	{
		UInkDiagnosticCode code = UInkDiagnosticCode::None;
		uint64_t count = 0;
		uint64_t firstByteOffset = 0;
		uint64_t firstCanvasOrdinal = 0;
	};

	struct UInkReadSummary
	{
		UInkReadOutcome outcome = UInkReadOutcome::Failed;
		// 按 UInkDiagnosticCode 索引，count 为 0 表示未出现。
		std::array<UInkDiagnosticCount, static_cast<std::size_t>(UInkDiagnosticCode::DecodeFailed) + 1>
			diagnosticCounts{};
		std::optional<uint16_t> version;
		uint64_t decodeMicroseconds = 0;
	};

	struct UInkReadResult
	{
		UInkReadStatus status = UInkReadStatus::RejectedHeader;
		std::optional<UInkDocument> document;
		std::vector<UInkDiagnostic> diagnostics;
		UInkLoadProvenance provenance;
		uint64_t validPrefixLength = 0;
		std::optional<uint64_t> failedObjectOffset;
		std::optional<uint64_t> safeAppendOffset;
		uint64_t decodedObjectCount = 0;
		uint64_t replayedObjectCount = 0;
		uint64_t geometryPointCount = 0;
		uint64_t modelCharge = 0;
		std::wstring sourcePath;
		UInkReadSummary summary;
	};

	bool IsSafeMediaPath(const std::string& path,
		uint64_t maxBytes = 32768) noexcept;
	bool HasMedia(const UInkDocument& document) noexcept;
	// 计算当前 Canvas 的有效显示状态；Clear 隐藏旧区间并终止 latest 反向扫描。
	std::vector<bool> ComputeUInkLatestVisibility(const UInkCanvas& canvas);
}
