#define MSGPACK_NO_BOOST
#include <msgpack.hpp>

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cwchar>
#include <cstring>
#include <iostream>
#include <limits>
#include <map>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <thread>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

import draw3.uink_codec;
import draw3.uink_draw3_capture;
import draw3.uink_file;

namespace
{
	using namespace draw3;
	using namespace draw3::uink;

	struct TestState
	{
		int failures = 0;

		void Check(bool condition, const char* expression, int line)
		{
			if (condition) return;
			++failures;
			std::cerr << "UInk FAILED line " << line << ": " << expression << std::endl;
		}
	};

#define UINK_CHECK(state, expression) (state).Check(!!(expression), #expression, __LINE__)

	bool NearlyEqual(float left, float right, float tolerance = 0.0001f)
	{
		return std::abs(left - right) <= tolerance;
	}

	UInkGuid Guid(const char* text)
	{
		const std::optional<UInkGuid> guid = ParseUInkGuid(text);
		return guid ? *guid : UInkGuid{};
	}

	InkGuid Draw3Guid(const char* text)
	{
		return InkGuid(Guid(text).Bytes());
	}

	UInkMessagePackValue Value(std::string value)
	{
		UInkMessagePackValue result;
		result.value = std::move(value);
		return result;
	}

	template <typename T>
	UInkMessagePackValue Value(T value)
	{
		UInkMessagePackValue result;
		result.value = std::move(value);
		return result;
	}

	UInkExtra MakeExtra(size_t binaryBytes = 4)
	{
		UInkMessagePackValue::Array array;
		array.push_back(Value(uint64_t{ 0xffffffffffffffffull }));
		array.push_back(Value(std::vector<std::byte>(binaryBytes, std::byte{ 0x5a })));
		UInkMessagePackExtension extension;
		extension.type = -7;
		extension.data = { std::byte{ 0x11 }, std::byte{ 0x22 } };
		array.push_back(Value(std::move(extension)));
		return { { Value(std::string("vendor.private")), Value(std::move(array)) } };
	}

	UInkInk MakeInk(uint32_t contentId, uint32_t undoId, int32_t inkType = 1)
	{
		UInkInk ink;
		ink.contentId = contentId;
		ink.undoId = undoId;
		ink.declaredInkType = inkType;
		ink.effectiveKind = inkType == 0 ? UInkInkKind::Erase :
			inkType == 2 ? UInkInkKind::Highlighter :
			inkType == 3 ? UInkInkKind::AdvancedHighlighter : UInkInkKind::Pen;
		ink.color.fallbackRgb = 0x3366ff;
		ink.opacity = inkType == 2 || inkType == 3 ? 0.4f : 1.0f;
		ink.points = { { 12.0f, 20.0f, 5.0f, std::nullopt },
			{ 18.0f, 25.0f, 6.0f, std::nullopt } };
		return ink;
	}

	UInkShapeStroke MakeStroke(float width = 3.0f)
	{
		UInkShapeStroke stroke;
		stroke.color.fallbackRgb = 0x0044aa;
		stroke.opacity = 0.8f;
		stroke.width = width;
		return stroke;
	}

	UInkShape MakeShape(uint32_t contentId, int32_t shapeType)
	{
		UInkShape shape;
		shape.contentId = contentId;
		shape.undoId = contentId;
		shape.declaredShapeType = shapeType;
		if (shapeType == 0 || shapeType == 1 || shapeType == 6)
		{
			UInkLineGeometry geometry;
			geometry.points = { { 10.0f, 10.0f }, { 60.0f, 30.0f } };
			if (shapeType != 0) geometry.points.push_back({ 40.0f, 80.0f });
			shape.geometry = std::move(geometry);
		}
		else if (shapeType == 2)
		{
			shape.geometry = UInkRectangleGeometry{ 100.0f, 80.0f, 60.0f, 40.0f,
				0.25f, 6.0f, 4.0f };
		}
		else if (shapeType == 3)
		{
			shape.geometry = UInkSquareGeometry{ 100.0f, 80.0f, 44.0f, 0.5f };
		}
		else if (shapeType == 4)
		{
			shape.geometry = UInkEllipseGeometry{ 100.0f, 80.0f, 60.0f, 40.0f, 0.25f };
		}
		else
		{
			shape.geometry = UInkCircleGeometry{ 100.0f, 80.0f, 30.0f };
		}

		if (shapeType == 0 || shapeType == 1)
		{
			shape.stroke = MakeStroke();
		}
		else
		{
			shape.stroke = MakeStroke();
			UInkShapeFill fill;
			fill.color.fallbackRgb = 0xffcc00;
			fill.opacity = 0.5f;
			shape.fill = fill;
		}
		return shape;
	}

	UInkDocument MakeBasicDocument(bool includeInk = true)
	{
		UInkDocument document;
		document.header.guid = Guid("10000000-0000-4000-8000-00000000b001");
		document.header.deviceNum = 1;
		document.header.workspaceNum = 1;
		document.header.pageNum = 1;
		document.header.time = 1785888000;
		document.usesImplicitDevice = true;
		document.usesImplicitWorkspace = true;
		UInkCanvas canvas;
		canvas.pageGuid = Guid("20000000-0000-4000-8000-00000000b001");
		canvas.pageIndex = 0;
		canvas.pageNumber = 1;
		canvas.layerIndex = 0;
		canvas.layerNumber = 0;
		canvas.viewport = UInkViewport{};
		if (includeInk) canvas.content.push_back(MakeInk(0, 0));
		document.canvases.push_back(std::move(canvas));
		return document;
	}

	UInkDocument MakeRichDocument()
	{
		UInkDocument document;
		document.header.guid = Guid("10000000-0000-4000-8000-00000000c001");
		document.header.deviceNum = 2;
		document.header.workspaceNum = 2;
		document.header.pageNum = 2;
		document.header.time = 1785888000;
		document.usesImplicitDevice = false;
		document.usesImplicitWorkspace = false;

		UInkHeaderExtension extension;
		extension.name = "Rich UInk";
		extension.explanation = "all registered structures";
		UInkDevice display;
		display.guid = Guid("40000000-0000-4000-8000-00000000c001");
		display.deviceType = 0;
		display.name = "Display";
		display.geometry = UInkDisplayDevice{ -20, 10, 1920, 1080 };
		UInkHardware hardware;
		hardware.name = "Panel";
		hardware.id = "DISPLAY-1";
		hardware.identifiers = { { "edid", "abc" }, { "serial", "42" } };
		hardware.physicalWidth = 530;
		hardware.physicalHeight = 300;
		hardware.scaleFactor = 1.5f;
		display.hardware = std::move(hardware);
		display.extra = MakeExtra();
		extension.devices.push_back(std::move(display));

		UInkDevice window;
		window.guid = Guid("40000000-0000-4000-8000-00000000c002");
		window.deviceType = 1;
		window.name = "Window";
		window.geometry = UInkWindowDevice{
			Guid("40000000-0000-4000-8000-00000000c001"),
			20.0f, 30.0f, 800.0f, 600.0f, 2 };
		extension.devices.push_back(std::move(window));

		UInkWorkspace mainWorkspace;
		mainWorkspace.guid = Guid("30000000-0000-4000-8000-00000000c001");
		mainWorkspace.workspaceType = 1;
		mainWorkspace.name = "Board";
		mainWorkspace.currentPageIndex = 0;
		extension.workspaces.push_back(std::move(mainWorkspace));
		UInkWorkspace presentation;
		presentation.guid = Guid("30000000-0000-4000-8000-00000000c002");
		presentation.workspaceType = 2;
		presentation.name = "Slides";
		presentation.hostId = "INKKEYS-PPT-42";
		presentation.parentWorkspaceGuid = Guid("30000000-0000-4000-8000-00000000c001");
		presentation.currentPageIndex = 0;
		extension.workspaces.push_back(std::move(presentation));
		extension.extra = MakeExtra();
		document.headerExtension = std::move(extension);

		UInkCanvas primary;
		primary.workspaceGuid = Guid("30000000-0000-4000-8000-00000000c001");
		primary.deviceGuid = Guid("40000000-0000-4000-8000-00000000c001");
		primary.pageGuid = Guid("50000000-0000-4000-8000-00000000c001");
		primary.pageIndex = 0;
		primary.pageNumber = 1;
		primary.layerIndex = 0;
		primary.layerNumber = 10;
		primary.viewport = UInkViewport{ -120.0f, 45.0f, 1.5f };
		primary.extra = MakeExtra();

		UInkInk advanced = MakeInk(0, 0, 3);
		advanced.color.fallbackRgb = 0xffff00;
		advanced.color.extended = UInkExtendedColor{
			UInkColorSpace::ScRgb, { 1.8f, 0.65f, -0.1f } };
		advanced.declaredTexture = 128;
		advanced.effectiveTexture = 0;
		advanced.points = { { 100.0f, 100.0f, 24.0f,
			UInkPointStyle{ UInkColor{ 0xffaa00,
				UInkExtendedColor{ UInkColorSpace::ScRgb, { 1.4f, 0.4f, 0.1f } } }, 0.25f } },
			{ 112.0f, 103.0f, 25.0f, std::nullopt },
			{ 122.0f, 107.0f, 26.0f,
				UInkPointStyle{ UInkColor{ 0x00ffff, std::nullopt }, 0.6f } } };
		advanced.renderOnlyWhenLatest = true;
		advanced.extra = MakeExtra();
		primary.content.push_back(std::move(advanced));

		UInkInk eraser = MakeInk(1, 1, 0);
		eraser.points.resize(1);
		primary.content.push_back(std::move(eraser));
		for (int32_t shapeType = 0; shapeType <= 6; ++shapeType)
		{
			UInkShape shape = MakeShape(static_cast<uint32_t>(shapeType + 2), shapeType);
			if (shapeType == 0)
			{
				shape.stroke->dashArray = { 12.0f, 8.0f };
				shape.stroke->dashOffset = 2.0f;
				shape.stroke->declaredStartMarker = 128;
				shape.stroke->declaredEndMarker = 1;
			}
			if (shapeType == 3 && shape.fill)
			{
				shape.fill->declaredFillType = 128;
				shape.fill->color.extended = UInkExtendedColor{
					UInkColorSpace::ScRgb, { 1.2f, 0.3f, 0.1f } };
			}
			primary.content.push_back(std::move(shape));
		}

		UInkMedia media;
		media.contentId = 9;
		media.undoId = 9;
		media.path = "media/picture.png";
		media.mimeType = "image/png";
		media.width = 640.0f;
		media.height = 480.0f;
		media.transform = { 1.0f, 0.1f, -0.1f, 1.0f, 20.0f, 30.0f };
		media.opacity = 0.7f;
		media.pageCount = 3;
		media.pageIndex = 1;
		media.autoplay = true;
		media.loop = true;
		media.volume = 0.5f;
		media.startTime = 1.25;
		media.playbackRate = 1.5f;
		media.pathIsSafe = true;
		media.extra = MakeExtra();
		primary.content.push_back(std::move(media));
		document.canvases.push_back(std::move(primary));

		UInkCanvas secondLayer;
		secondLayer.workspaceGuid = Guid("30000000-0000-4000-8000-00000000c001");
		secondLayer.deviceGuid = Guid("40000000-0000-4000-8000-00000000c001");
		secondLayer.pageGuid = Guid("50000000-0000-4000-8000-00000000c001");
		secondLayer.pageIndex = 0;
		secondLayer.pageNumber = 1;
		secondLayer.layerIndex = 1;
		secondLayer.layerNumber = 20;
		document.canvases.push_back(std::move(secondLayer));

		UInkCanvas ppt;
		ppt.workspaceGuid = Guid("30000000-0000-4000-8000-00000000c002");
		ppt.deviceGuid = Guid("40000000-0000-4000-8000-00000000c002");
		ppt.pageGuid = Guid("50000000-0000-4000-8000-00000000c002");
		ppt.pageIndex = 0;
		ppt.pageNumber = 1;
		ppt.layerIndex = 0;
		ppt.layerNumber = 0;
		ppt.slideId = 42;
		ppt.viewport = UInkViewport{ 4.0f, 5.0f, 1.0f };
		ppt.content.push_back(MakeInk(0, 0));
		document.canvases.push_back(std::move(ppt));
		return document;
	}

	bool HasDiagnostic(const UInkReadResult& result, UInkDiagnosticCode code)
	{
		return std::any_of(result.diagnostics.begin(), result.diagnostics.end(),
			[&](const UInkDiagnostic& diagnostic) { return diagnostic.code == code; });
	}

	std::wstring ParentPath(std::wstring path)
	{
		const size_t slash = path.find_last_of(L"\\/");
		return slash == std::wstring::npos ? std::wstring{} : path.substr(0, slash);
	}

	std::wstring FixtureDirectory()
	{
		std::array<wchar_t, 32768> modulePath = {};
		const DWORD length = GetModuleFileNameW(nullptr, modulePath.data(),
			static_cast<DWORD>(modulePath.size()));
		std::wstring root(modulePath.data(), length);
		root = ParentPath(ParentPath(ParentPath(root)));
		return root + L"\\inkStrokeModelerTestTests\\fixtures\\uink-v10";
	}

	std::vector<std::byte> ReadBytes(const std::wstring& path)
	{
		HANDLE handle = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ,
			nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL | FILE_FLAG_SEQUENTIAL_SCAN, nullptr);
		if (handle == INVALID_HANDLE_VALUE) return {};
		LARGE_INTEGER length = {};
		if (!GetFileSizeEx(handle, &length) || length.QuadPart < 0 ||
			static_cast<uint64_t>(length.QuadPart) > std::numeric_limits<size_t>::max())
		{
			CloseHandle(handle);
			return {};
		}
		std::vector<std::byte> bytes(static_cast<size_t>(length.QuadPart));
		size_t position = 0;
		while (position < bytes.size())
		{
			DWORD read = 0;
			const DWORD requested = static_cast<DWORD>(std::min<size_t>(
				bytes.size() - position, 1024 * 1024));
			if (!ReadFile(handle, bytes.data() + position, requested, &read, nullptr) || read == 0)
			{
				bytes.clear();
				break;
			}
			position += read;
		}
		CloseHandle(handle);
		return bytes;
	}

	bool WriteBytes(const std::wstring& path, std::span<const std::byte> bytes)
	{
		HANDLE handle = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
			FILE_ATTRIBUTE_NORMAL, nullptr);
		if (handle == INVALID_HANDLE_VALUE) return false;
		size_t position = 0;
		while (position < bytes.size())
		{
			DWORD written = 0;
			const DWORD requested = static_cast<DWORD>(std::min<size_t>(
				bytes.size() - position, 1024 * 1024));
			if (!WriteFile(handle, bytes.data() + position, requested, &written, nullptr) ||
				written == 0)
			{
				CloseHandle(handle);
				return false;
			}
			position += written;
		}
		const bool flushed = FlushFileBuffers(handle) != FALSE;
		CloseHandle(handle);
		return flushed;
	}

	std::string HexDigest(const std::array<uint8_t, 32>& digest)
	{
		static constexpr char kHex[] = "0123456789abcdef";
		std::string result;
		result.reserve(64);
		for (const uint8_t value : digest)
		{
			result.push_back(kHex[value >> 4]);
			result.push_back(kHex[value & 0x0f]);
		}
		return result;
	}

	std::vector<std::byte> ToBytes(const msgpack::sbuffer& buffer)
	{
		std::vector<std::byte> bytes(buffer.size());
		if (!bytes.empty()) std::memcpy(bytes.data(), buffer.data(), buffer.size());
		return bytes;
	}

	void AppendBytes(std::vector<std::byte>& target, std::span<const std::byte> bytes)
	{
		target.insert(target.end(), bytes.begin(), bytes.end());
	}

	bool ReplaceFirstAscii(std::vector<std::byte>& bytes, std::string_view source,
		std::string_view replacement)
	{
		if (source.size() != replacement.size()) return false;
		const auto found = std::search(bytes.begin(), bytes.end(), source.begin(), source.end(),
			[](std::byte left, char right)
			{
				return std::to_integer<uint8_t>(left) == static_cast<uint8_t>(right);
			});
		if (found == bytes.end()) return false;
		for (size_t index = 0; index < replacement.size(); ++index)
			found[index] = static_cast<std::byte>(replacement[index]);
		return true;
	}

	bool ReplaceGuidAfterKey(std::vector<std::byte>& bytes, std::string_view key,
		std::string_view replacement)
	{
		if (replacement.size() != 36) return false;
		const auto keyPosition = std::search(bytes.begin(), bytes.end(), key.begin(), key.end(),
			[](std::byte left, char right)
			{
				return std::to_integer<uint8_t>(left) == static_cast<uint8_t>(right);
			});
		if (keyPosition == bytes.end()) return false;
		const std::array<std::byte, 2> str8 = { std::byte{ 0xd9 }, std::byte{ 36 } };
		const auto valueMarker = std::search(keyPosition + key.size(), bytes.end(),
			str8.begin(), str8.end());
		if (valueMarker == bytes.end() || bytes.end() - valueMarker < 38) return false;
		for (size_t index = 0; index < replacement.size(); ++index)
			valueMarker[2 + index] = static_cast<std::byte>(replacement[index]);
		return true;
	}

	void PackKey(msgpack::packer<msgpack::sbuffer>& packer, const char* key)
	{
		const uint32_t size = static_cast<uint32_t>(std::strlen(key));
		packer.pack_str(size);
		packer.pack_str_body(key, size);
	}

	void PackGuid(msgpack::packer<msgpack::sbuffer>& packer, const char* guid)
	{
		packer.pack_str(36);
		packer.pack_str_body(guid, 36);
	}

	void PackColor(msgpack::packer<msgpack::sbuffer>& packer, uint32_t fallback = 0x3366ff)
	{
		packer.pack_map(1);
		PackKey(packer, "fallback");
		packer.pack_uint32(fallback);
	}

	std::vector<std::byte> PackUnknown(uint16_t type, uint32_t nestedArrays = 0)
	{
		msgpack::sbuffer buffer;
		msgpack::packer<msgpack::sbuffer> packer(buffer);
		packer.pack_map(2);
		PackKey(packer, "type");
		packer.pack_uint16(type);
		PackKey(packer, "payload");
		for (uint32_t index = 0; index < nestedArrays; ++index) packer.pack_array(1);
		packer.pack_uint32(7);
		return ToBytes(buffer);
	}

	std::vector<std::byte> PackWideUnknown(uint32_t values)
	{
		msgpack::sbuffer buffer;
		msgpack::packer<msgpack::sbuffer> packer(buffer);
		packer.pack_map(2);
		PackKey(packer, "type");
		packer.pack_uint16(999);
		PackKey(packer, "payload");
		packer.pack_array(values);
		for (uint32_t index = 0; index < values; ++index) packer.pack_nil();
		return ToBytes(buffer);
	}

	std::vector<std::byte> PackDuplicateInk()
	{
		msgpack::sbuffer buffer;
		msgpack::packer<msgpack::sbuffer> packer(buffer);
		packer.pack_map(9);
		PackKey(packer, "type"); packer.pack_uint16(kInkType);
		PackKey(packer, "contentId"); packer.pack_uint32(0);
		PackKey(packer, "contentId"); packer.pack_uint32(0);
		PackKey(packer, "undoId"); packer.pack_uint32(0);
		PackKey(packer, "inkType"); packer.pack_int32(1);
		PackKey(packer, "color"); PackColor(packer);
		PackKey(packer, "opacity"); packer.pack_float(1.0f);
		PackKey(packer, "texture"); packer.pack_int32(0);
		PackKey(packer, "points");
		packer.pack_array(1);
		packer.pack_map(3);
		PackKey(packer, "x"); packer.pack_float(1.0f);
		PackKey(packer, "y"); packer.pack_float(2.0f);
		PackKey(packer, "width"); packer.pack_float(3.0f);
		return ToBytes(buffer);
	}

	std::vector<std::byte> PackInvalidInk(bool nanOpacity, int32_t inkType = 1)
	{
		msgpack::sbuffer buffer;
		msgpack::packer<msgpack::sbuffer> packer(buffer);
		packer.pack_map(8);
		PackKey(packer, "type"); packer.pack_uint16(kInkType);
		PackKey(packer, "contentId"); packer.pack_uint32(0);
		PackKey(packer, "undoId"); packer.pack_uint32(0);
		PackKey(packer, "inkType"); packer.pack_int32(inkType);
		PackKey(packer, "color"); PackColor(packer);
		PackKey(packer, "opacity"); packer.pack_float(nanOpacity ?
			std::numeric_limits<float>::quiet_NaN() : 1.0f);
		PackKey(packer, "texture"); packer.pack_int32(0);
		PackKey(packer, "points");
		packer.pack_array(1);
		packer.pack_map(3);
		PackKey(packer, "x"); packer.pack_float(1.0f);
		PackKey(packer, "y"); packer.pack_float(2.0f);
		PackKey(packer, "width"); packer.pack_float(3.0f);
		return ToBytes(buffer);
	}

	std::vector<std::byte> PackUnknownInkWithPointStyle()
	{
		msgpack::sbuffer buffer;
		msgpack::packer<msgpack::sbuffer> packer(buffer);
		packer.pack_map(8);
		PackKey(packer, "type"); packer.pack_uint16(kInkType);
		PackKey(packer, "contentId"); packer.pack_uint32(0);
		PackKey(packer, "undoId"); packer.pack_uint32(0);
		PackKey(packer, "inkType"); packer.pack_int32(128);
		PackKey(packer, "color"); PackColor(packer);
		PackKey(packer, "opacity"); packer.pack_float(1.0f);
		PackKey(packer, "texture"); packer.pack_int32(0);
		PackKey(packer, "points");
		packer.pack_array(1);
		packer.pack_map(5);
		PackKey(packer, "x"); packer.pack_float(1.0f);
		PackKey(packer, "y"); packer.pack_float(2.0f);
		PackKey(packer, "width"); packer.pack_float(3.0f);
		PackKey(packer, "color"); PackColor(packer, 0x00ff00);
		PackKey(packer, "opacity"); packer.pack_float(0.25f);
		return ToBytes(buffer);
	}

	std::vector<std::byte> PackInkWithHugeIntegerCoordinate()
	{
		msgpack::sbuffer buffer;
		msgpack::packer<msgpack::sbuffer> packer(buffer);
		packer.pack_map(8);
		PackKey(packer, "type"); packer.pack_uint16(kInkType);
		PackKey(packer, "contentId"); packer.pack_uint32(0);
		PackKey(packer, "undoId"); packer.pack_uint32(0);
		PackKey(packer, "inkType"); packer.pack_int32(1);
		PackKey(packer, "color"); PackColor(packer);
		PackKey(packer, "opacity"); packer.pack_float(1.0f);
		PackKey(packer, "texture"); packer.pack_int32(0);
		PackKey(packer, "points");
		packer.pack_array(1);
		packer.pack_map(3);
		PackKey(packer, "x"); packer.pack_uint64(std::numeric_limits<uint64_t>::max());
		PackKey(packer, "y"); packer.pack_float(2.0f);
		PackKey(packer, "width"); packer.pack_float(3.0f);
		return ToBytes(buffer);
	}

	std::vector<std::byte> PackHeaderExtensionWithDuplicateHardwareIdentifiers()
	{
		msgpack::sbuffer buffer;
		msgpack::packer<msgpack::sbuffer> packer(buffer);
		packer.pack_map(2);
		PackKey(packer, "type"); packer.pack_uint16(kHeaderExtensionType);
		PackKey(packer, "devices");
		packer.pack_array(1);
		packer.pack_map(7);
		PackKey(packer, "guid");
		PackGuid(packer, "40000000-0000-4000-8000-00000000e001");
		PackKey(packer, "deviceType"); packer.pack_int32(0);
		PackKey(packer, "x"); packer.pack_int32(0);
		PackKey(packer, "y"); packer.pack_int32(0);
		PackKey(packer, "width"); packer.pack_uint32(1920);
		PackKey(packer, "height"); packer.pack_uint32(1080);
		PackKey(packer, "hardware");
		packer.pack_map(1);
		PackKey(packer, "identifiers");
		packer.pack_map(2);
		PackKey(packer, "serial"); packer.pack(std::string("first"));
		PackKey(packer, "serial"); packer.pack(std::string("second"));
		return ToBytes(buffer);
	}

	std::vector<std::byte> PackCanvasForExplicitDevice()
	{
		msgpack::sbuffer buffer;
		msgpack::packer<msgpack::sbuffer> packer(buffer);
		packer.pack_map(7);
		PackKey(packer, "type"); packer.pack_uint16(kCanvasType);
		PackKey(packer, "deviceGuid");
		PackGuid(packer, "40000000-0000-4000-8000-00000000e001");
		PackKey(packer, "pageGuid");
		PackGuid(packer, "50000000-0000-4000-8000-00000000e001");
		PackKey(packer, "pageIndex"); packer.pack_uint32(0);
		PackKey(packer, "pageNumber"); packer.pack_uint32(1);
		PackKey(packer, "layerIndex"); packer.pack_uint32(0);
		PackKey(packer, "layerNumber"); packer.pack_uint32(0);
		return ToBytes(buffer);
	}

	std::vector<std::byte> PackMediaWithInvalidPageCount()
	{
		msgpack::sbuffer buffer;
		msgpack::packer<msgpack::sbuffer> packer(buffer);
		packer.pack_map(8);
		PackKey(packer, "type"); packer.pack_uint16(kMediaType);
		PackKey(packer, "contentId"); packer.pack_uint32(0);
		PackKey(packer, "undoId"); packer.pack_uint32(0);
		PackKey(packer, "path"); packer.pack(std::string("documents/file.pdf"));
		PackKey(packer, "mimeType"); packer.pack(std::string("application/pdf"));
		PackKey(packer, "width"); packer.pack_float(800.0f);
		PackKey(packer, "height"); packer.pack_float(600.0f);
		PackKey(packer, "pageCount"); packer.pack(std::string("invalid"));
		return ToBytes(buffer);
	}

	std::vector<std::byte> PackInvalidCanvas()
	{
		msgpack::sbuffer buffer;
		msgpack::packer<msgpack::sbuffer> packer(buffer);
		packer.pack_map(2);
		PackKey(packer, "type"); packer.pack_uint16(kCanvasType);
		PackKey(packer, "extra"); packer.pack_uint32(1);
		return ToBytes(buffer);
	}

	std::vector<std::byte> PackInkWithInvalidExtraKey()
	{
		msgpack::sbuffer buffer;
		msgpack::packer<msgpack::sbuffer> packer(buffer);
		packer.pack_map(9);
		PackKey(packer, "type"); packer.pack_uint16(kInkType);
		PackKey(packer, "contentId"); packer.pack_uint32(0);
		PackKey(packer, "undoId"); packer.pack_uint32(0);
		PackKey(packer, "inkType"); packer.pack_int32(1);
		PackKey(packer, "color"); PackColor(packer);
		PackKey(packer, "opacity"); packer.pack_float(1.0f);
		PackKey(packer, "texture"); packer.pack_int32(0);
		PackKey(packer, "points");
		packer.pack_array(1);
		packer.pack_map(3);
		PackKey(packer, "x"); packer.pack_float(1.0f);
		PackKey(packer, "y"); packer.pack_float(2.0f);
		PackKey(packer, "width"); packer.pack_float(3.0f);
		PackKey(packer, "extra");
		packer.pack_map(1);
		packer.pack_uint32(7);
		packer.pack_nil();
		return ToBytes(buffer);
	}

	std::vector<size_t> ObjectSizes(std::span<const std::byte> bytes)
	{
		std::vector<size_t> sizes;
		size_t offset = 0;
		while (offset < bytes.size())
		{
			const size_t before = offset;
			try
			{
				(void)msgpack::unpack(reinterpret_cast<const char*>(bytes.data()),
					bytes.size(), offset);
			}
			catch (...)
			{
				break;
			}
			if (offset <= before) break;
			sizes.push_back(offset - before);
		}
		return sizes;
	}

	void TestOfficialFixtures(TestState& state)
	{
		struct Fixture
		{
			const wchar_t* name;
			uint64_t bytes;
			const char* sha256;
			uint64_t objects;
			UInkReadStatus status;
		};
		const Fixture fixtures[] = {
			{ L"explicit-multilayer.uink", 1671,
				"d28363358913585448cb355e209f9f5c8aa3e8a5f7ce3905816fa68ae9a55a0b", 7,
				UInkReadStatus::Complete },
			{ L"implicit-single-canvas.uink", 380,
				"93b9a96aaa1aa624b0f7e41695f68cbe0efab1a011f8cd49d0ff314642b4fb16", 3,
				UInkReadStatus::Complete },
			{ L"incremental-tail.uink", 639,
				"380c4a4fa94ceec9794053728e237b26127a14539f893e4b919a95a8c1c59ba5", 5,
				UInkReadStatus::Complete },
			{ L"media-safe.uink", 536,
				"e1f0a62df9840948b078de88bd3ff30358ba830b4f4b2123c5032af056526d0d", 4,
				UInkReadStatus::Complete },
			{ L"mixed-latest.uink", 863,
				"0d8433bd22686a964811633f31ce2fcadc673b97ba74f73bd89e0fc59d372416", 6,
				UInkReadStatus::Complete },
			{ L"numeric-compat.uink", 218,
				"85dce061a4f68e966938641fe0f7a4d51b7f7123e47fee5533fa331d9c12ebd1", 2,
				UInkReadStatus::Complete },
			{ L"truncated-tail.uink", 458,
				"f833ba93503372b3812b9b7c1132ce73e79fe563e1de778815641331ad1bd38b", 3,
				UInkReadStatus::RecoveredTruncatedTail },
			{ L"unknown-block.uink", 395,
				"f68e0ef78dbce8976ca8478cc6d10ba4dd63421442fbeedcbd0135d42d293594", 4,
				UInkReadStatus::Complete }
		};
		const std::wstring directory = FixtureDirectory();
		for (const Fixture& fixture : fixtures)
		{
			const UInkReadResult result = ReadUInkFile(directory + L"\\" + fixture.name);
			UINK_CHECK(state, result.status == fixture.status);
			UINK_CHECK(state, result.document.has_value());
			UINK_CHECK(state, result.sourceRevision.has_value());
			UINK_CHECK(state, result.decodedObjectCount == fixture.objects);
			if (result.sourceRevision)
			{
				UINK_CHECK(state, result.sourceRevision->length == fixture.bytes);
				UINK_CHECK(state, HexDigest(result.sourceRevision->sha256) == fixture.sha256);
			}
		}

		const UInkReadResult explicitResult = ReadUInkFile(
			directory + L"\\explicit-multilayer.uink");
		UINK_CHECK(state, explicitResult.document && explicitResult.document->headerExtension);
		if (explicitResult.document && explicitResult.document->headerExtension)
		{
			UINK_CHECK(state, explicitResult.document->headerExtension->devices.size() == 2);
			UINK_CHECK(state, explicitResult.document->headerExtension->workspaces.size() == 2);
			UINK_CHECK(state, explicitResult.document->canvases.size() == 3);
			UINK_CHECK(state, explicitResult.document->canvases[1].layerIndex == 1);
		}

		const UInkReadResult incremental = ReadUInkFile(directory + L"\\incremental-tail.uink");
		UINK_CHECK(state, incremental.document && incremental.document->canvases.size() == 2);
		UINK_CHECK(state, HasDiagnostic(incremental, UInkDiagnosticCode::HeaderSnapshotMismatch));
		const UInkReadResult numeric = ReadUInkFile(directory + L"\\numeric-compat.uink");
		UINK_CHECK(state, HasDiagnostic(numeric, UInkDiagnosticCode::NumericCompatibility));
		const UInkReadResult truncated = ReadUInkFile(directory + L"\\truncated-tail.uink");
		UINK_CHECK(state, truncated.validPrefixLength == 380);
		UINK_CHECK(state, truncated.safeAppendOffset && *truncated.safeAppendOffset == 380);
		const UInkReadResult unknown = ReadUInkFile(directory + L"\\unknown-block.uink");
		UINK_CHECK(state, unknown.provenance.containsUnknownTopLevel);
		UINK_CHECK(state, unknown.provenance.requiresSaveAs);
		UINK_CHECK(state, unknown.document && unknown.document->canvases[0].content.size() == 1);

		const UInkReadResult media = ReadUInkFile(directory + L"\\media-safe.uink");
		UINK_CHECK(state, media.document && HasMedia(*media.document));
		UINK_CHECK(state, HasDiagnostic(media, UInkDiagnosticCode::MediaResourceUnavailable));
		if (media.document)
		{
			UINK_CHECK(state, media.document->canvases[0].content.size() == 2);
			const UInkMedia* first = std::get_if<UInkMedia>(&media.document->canvases[0].content[0]);
			UINK_CHECK(state, first && first->path == "images/pixel.png" && first->pathIsSafe);
		}

		const UInkReadResult mixed = ReadUInkFile(directory + L"\\mixed-latest.uink");
		UINK_CHECK(state, mixed.document && mixed.document->canvases[0].content.size() == 4);
		if (mixed.document)
		{
			const std::vector<bool> visible = ComputeUInkLatestVisibility(
				mixed.document->canvases[0]);
			UINK_CHECK(state, visible.size() == 4);
			UINK_CHECK(state, !visible[0] && visible[1] && !visible[2] && visible[3]);
		}

		const std::vector<std::byte> sums = ReadBytes(directory + L"\\SHA256SUMS.txt");
		const std::string sumsText(reinterpret_cast<const char*>(sums.data()), sums.size());
		UINK_CHECK(state, sumsText.find(
			"90ff3db09c512a0a0538ed8072fee45289c92db43f4bec2be65aa31375a40f82  media-safe.uink.extra") !=
			std::string::npos);
		UINK_CHECK(state, GetFileAttributesW((directory + L"\\media-safe.uink.extra").c_str()) ==
			INVALID_FILE_ATTRIBUTES);
	}

	void TestExactEncodingAndRichRoundTrip(TestState& state)
	{
		const UInkEncodeResult basic = EncodeUInkDocument(MakeBasicDocument(false));
		UINK_CHECK(state, basic.status == UInkEncodeStatus::Success);
		UINK_CHECK(state, basic.bytes.size() > 69);
		if (basic.bytes.size() > 69)
		{
			const auto byte = [&](size_t index) { return std::to_integer<uint8_t>(basic.bytes[index]); };
			UINK_CHECK(state, byte(0) == 0x97);
			UINK_CHECK(state, byte(1) == 0xcd && byte(2) == 0x00 && byte(3) == 0x00);
			UINK_CHECK(state, byte(4) == 0xcd && byte(5) == 0x00 && byte(6) == 0x0a);
			UINK_CHECK(state, byte(7) == 0xd9 && byte(8) == 36);
			UINK_CHECK(state, (byte(69) & 0xf0) == 0x80);
		}
		UINK_CHECK(state, ObjectSizes(basic.bytes).size() == 2);

		const UInkEncodeResult encoded = EncodeUInkDocument(MakeRichDocument());
		UINK_CHECK(state, encoded.status == UInkEncodeStatus::Success);
		if (encoded.status != UInkEncodeStatus::Success) return;
		const UInkReadResult decoded = DecodeUInk(encoded.bytes);
		UINK_CHECK(state, decoded.status == UInkReadStatus::Complete);
		UINK_CHECK(state, decoded.document.has_value());
		if (!decoded.document) return;

		const UInkDocument& document = *decoded.document;
		UINK_CHECK(state, document.headerExtension.has_value());
		UINK_CHECK(state, document.header.deviceNum == 2 && document.header.workspaceNum == 2);
		UINK_CHECK(state, document.canvases.size() == 3);
		if (!document.headerExtension || document.canvases.empty()) return;
		UINK_CHECK(state, document.headerExtension->devices.size() == 2);
		const UInkDevice& display = document.headerExtension->devices[0];
		UINK_CHECK(state, display.hardware && display.hardware->identifiers.size() == 2);
		UINK_CHECK(state, std::holds_alternative<UInkDisplayDevice>(display.geometry));
		UINK_CHECK(state, std::holds_alternative<UInkWindowDevice>(
			document.headerExtension->devices[1].geometry));
		UINK_CHECK(state, document.headerExtension->workspaces[1].hostId ==
			std::optional<std::string>("INKKEYS-PPT-42"));

		const UInkCanvas& primary = document.canvases[0];
		UINK_CHECK(state, primary.viewport && NearlyEqual(primary.viewport->scale, 1.5f));
		UINK_CHECK(state, primary.content.size() == 10);
		const UInkInk* advanced = std::get_if<UInkInk>(&primary.content[0]);
		UINK_CHECK(state, advanced && advanced->declaredInkType == 3);
		if (advanced)
		{
			UINK_CHECK(state, advanced->effectiveKind == UInkInkKind::AdvancedHighlighter);
			UINK_CHECK(state, advanced->declaredTexture == 128 && advanced->effectiveTexture == 0);
			UINK_CHECK(state, advanced->color.extended &&
				advanced->color.extended->space == UInkColorSpace::ScRgb);
			UINK_CHECK(state, advanced->points.size() == 3);
			UINK_CHECK(state, NearlyEqual(advanced->points[1].x, 112.0f));
			UINK_CHECK(state, advanced->points[0].style.has_value());
			UINK_CHECK(state, !advanced->points[1].style.has_value());
			UINK_CHECK(state, advanced->points[2].style.has_value());
			UINK_CHECK(state, advanced->extra && advanced->extra->size() == 1);
			if (advanced->extra)
			{
				const auto* values = std::get_if<UInkMessagePackValue::Array>(
					&advanced->extra->front().second.value);
				UINK_CHECK(state, values && values->size() == 3);
				if (values)
				{
					UINK_CHECK(state, std::holds_alternative<uint64_t>((*values)[0].value));
					UINK_CHECK(state, std::holds_alternative<UInkMessagePackExtension>(
						(*values)[2].value));
				}
			}
		}
		const UInkInk* eraser = std::get_if<UInkInk>(&primary.content[1]);
		UINK_CHECK(state, eraser && eraser->effectiveKind == UInkInkKind::Erase);
		for (int32_t shapeType = 0; shapeType <= 6; ++shapeType)
		{
			const UInkShape* shape = std::get_if<UInkShape>(&primary.content[shapeType + 2]);
			UINK_CHECK(state, shape && shape->declaredShapeType == shapeType);
			if (!shape) continue;
			if (shapeType == 0 || shapeType == 1 || shapeType == 6)
				UINK_CHECK(state, std::holds_alternative<UInkLineGeometry>(shape->geometry));
			else if (shapeType == 2)
				UINK_CHECK(state, std::holds_alternative<UInkRectangleGeometry>(shape->geometry));
			else if (shapeType == 3)
				UINK_CHECK(state, std::holds_alternative<UInkSquareGeometry>(shape->geometry));
			else if (shapeType == 4)
				UINK_CHECK(state, std::holds_alternative<UInkEllipseGeometry>(shape->geometry));
			else UINK_CHECK(state, std::holds_alternative<UInkCircleGeometry>(shape->geometry));
		}
		const UInkShape* line = std::get_if<UInkShape>(&primary.content[2]);
		UINK_CHECK(state, line && line->stroke && line->stroke->dashArray.size() == 2);
		if (line && line->stroke)
		{
			UINK_CHECK(state, line->stroke->declaredStartMarker == 128);
			UINK_CHECK(state, line->stroke->effectiveStartMarker == 0);
			UINK_CHECK(state, line->stroke->effectiveEndMarker == 1);
		}
		const UInkShape* square = std::get_if<UInkShape>(&primary.content[5]);
		UINK_CHECK(state, square && square->fill && square->fill->declaredFillType == 128);
		const UInkMedia* media = std::get_if<UInkMedia>(&primary.content[9]);
		UINK_CHECK(state, media && media->resourceState == UInkMediaResourceState::Unavailable);
		if (media)
		{
			UINK_CHECK(state, media->pathIsSafe && media->mimeType == "image/png");
			UINK_CHECK(state, media->pageCount == std::optional<uint32_t>(3));
			UINK_CHECK(state, media->pageIndex == 1 && media->autoplay && media->loop);
			UINK_CHECK(state, std::abs(media->startTime - 1.25) < 0.000001);
		}
		UINK_CHECK(state, document.canvases[1].layerIndex == 1 &&
			!document.canvases[1].viewport.has_value());
		UINK_CHECK(state, document.canvases[2].slideId == std::optional<int32_t>(42));

		// 通过完整 UInk 模型追加普通内容后，高级内容和相对顺序必须保持不变。
		UInkDocument edited = document;
		UInkInk ordinary = MakeInk(10, 10);
		edited.canvases[0].content.push_back(std::move(ordinary));
		const UInkEncodeResult preservedBytes = EncodeUInkDocument(edited);
		UINK_CHECK(state, preservedBytes.status == UInkEncodeStatus::Success);
		const UInkReadResult preserved = DecodeUInk(preservedBytes.bytes);
		UINK_CHECK(state, preserved.document && preserved.document->canvases[0].content.size() == 11);
		if (preserved.document)
		{
			const UInkInk* preservedAdvanced = std::get_if<UInkInk>(
				&preserved.document->canvases[0].content[0]);
			UINK_CHECK(state, preservedAdvanced && preservedAdvanced->points[0].style &&
				preservedAdvanced->color.extended);
			UINK_CHECK(state, std::holds_alternative<UInkMedia>(
				preserved.document->canvases[0].content[9]));
			UINK_CHECK(state, std::holds_alternative<UInkInk>(
				preserved.document->canvases[0].content[10]));
		}
	}

	std::vector<std::byte> HeaderPrefix()
	{
		UInkEncodeResult encoded = EncodeUInkDocument(MakeBasicDocument(false));
		const std::vector<size_t> sizes = ObjectSizes(encoded.bytes);
		if (sizes.empty()) return {};
		encoded.bytes.resize(sizes[0]);
		return encoded.bytes;
	}

	std::vector<std::byte> CanvasPrefix()
	{
		return EncodeUInkDocument(MakeBasicDocument(false)).bytes;
	}

	std::vector<std::byte> PackUnknownShape()
	{
		msgpack::sbuffer buffer;
		msgpack::packer<msgpack::sbuffer> packer(buffer);
		packer.pack_map(6);
		PackKey(packer, "type"); packer.pack_uint16(kShapeType);
		PackKey(packer, "contentId"); packer.pack_uint32(0);
		PackKey(packer, "undoId"); packer.pack_uint32(0);
		PackKey(packer, "shapeType"); packer.pack_int32(128);
		PackKey(packer, "geometry"); packer.pack_map(0);
		PackKey(packer, "stroke");
		packer.pack_map(3);
		PackKey(packer, "color"); PackColor(packer);
		PackKey(packer, "opacity"); packer.pack_float(1.0f);
		PackKey(packer, "width"); packer.pack_float(2.0f);
		return ToBytes(buffer);
	}

	std::vector<std::byte> PackClosedShapeWithMarkers()
	{
		msgpack::sbuffer buffer;
		msgpack::packer<msgpack::sbuffer> packer(buffer);
		packer.pack_map(7);
		PackKey(packer, "type"); packer.pack_uint16(kShapeType);
		PackKey(packer, "contentId"); packer.pack_uint32(0);
		PackKey(packer, "undoId"); packer.pack_uint32(0);
		PackKey(packer, "shapeType"); packer.pack_int32(2);
		PackKey(packer, "geometry");
		packer.pack_map(5);
		PackKey(packer, "centerX"); packer.pack_float(100.0f);
		PackKey(packer, "centerY"); packer.pack_float(80.0f);
		PackKey(packer, "width"); packer.pack_float(60.0f);
		PackKey(packer, "height"); packer.pack_float(40.0f);
		PackKey(packer, "rotation"); packer.pack_float(0.0f);
		PackKey(packer, "stroke");
		packer.pack_map(5);
		PackKey(packer, "color"); PackColor(packer);
		PackKey(packer, "opacity"); packer.pack_float(1.0f);
		PackKey(packer, "width"); packer.pack_float(2.0f);
		PackKey(packer, "startMarker"); packer.pack_int32(1);
		PackKey(packer, "endMarker"); packer.pack_int32(1);
		PackKey(packer, "fill");
		packer.pack_map(3);
		PackKey(packer, "fillType"); packer.pack_int32(0);
		PackKey(packer, "color"); PackColor(packer, 0xffcc00);
		PackKey(packer, "opacity"); packer.pack_float(0.5f);
		return ToBytes(buffer);
	}

	std::vector<std::byte> PackCanvasWithInvalidIdentity(bool highPageIndex)
	{
		msgpack::sbuffer buffer;
		msgpack::packer<msgpack::sbuffer> packer(buffer);
		packer.pack_map(6);
		PackKey(packer, "type"); packer.pack_uint16(kCanvasType);
		PackKey(packer, "pageGuid");
		if (highPageIndex) PackGuid(packer, "20000000-0000-4000-8000-00000000d001");
		else
		{
			packer.pack_str(10);
			packer.pack_str_body("not-a-guid", 10);
		}
		PackKey(packer, "pageIndex");
		if (highPageIndex) packer.pack_uint64(std::numeric_limits<uint64_t>::max());
		else packer.pack_uint32(0);
		PackKey(packer, "pageNumber"); packer.pack_uint32(1);
		PackKey(packer, "layerIndex"); packer.pack_uint32(0);
		PackKey(packer, "layerNumber"); packer.pack_uint32(0);
		return ToBytes(buffer);
	}

	std::vector<std::byte> PackCanvasWithViewport(uint32_t layerIndex, float scale)
	{
		msgpack::sbuffer buffer;
		msgpack::packer<msgpack::sbuffer> packer(buffer);
		packer.pack_map(7);
		PackKey(packer, "type"); packer.pack_uint16(kCanvasType);
		PackKey(packer, "pageGuid");
		PackGuid(packer, "20000000-0000-4000-8000-00000000b001");
		PackKey(packer, "pageIndex"); packer.pack_uint32(0);
		PackKey(packer, "pageNumber"); packer.pack_uint32(1);
		PackKey(packer, "layerIndex"); packer.pack_uint32(layerIndex);
		PackKey(packer, "layerNumber"); packer.pack_uint32(layerIndex);
		PackKey(packer, "viewport");
		packer.pack_map(3);
		PackKey(packer, "x"); packer.pack_float(4.0f);
		PackKey(packer, "y"); packer.pack_float(5.0f);
		PackKey(packer, "scale"); packer.pack_float(scale);
		return ToBytes(buffer);
	}

	UInkDocument MakeNestedExtraDocument(uint32_t nestedArrays,
		size_t arrayEntries, size_t binaryBytes)
	{
		UInkDocument document = MakeBasicDocument(false);
		UInkMessagePackValue::Array leaf;
		leaf.reserve(arrayEntries + 1);
		for (size_t index = 0; index < arrayEntries; ++index)
			leaf.push_back(Value(static_cast<uint64_t>(index)));
		leaf.push_back(Value(std::vector<std::byte>(binaryBytes, std::byte{ 0x33 })));
		UInkMessagePackValue nested = Value(std::move(leaf));
		for (uint32_t index = 0; index < nestedArrays; ++index)
		{
			UInkMessagePackValue::Array parent;
			parent.push_back(std::move(nested));
			nested = Value(std::move(parent));
		}
		document.canvases[0].extra = UInkExtra{
			{ Value(std::string("nested")), std::move(nested) } };
		return document;
	}

	UInkDocument MakeCycleDocument()
	{
		UInkDocument document;
		document.header.guid = Guid("10000000-0000-4000-8000-00000000d100");
		document.header.deviceNum = 2;
		document.header.workspaceNum = 2;
		document.header.pageNum = 1;
		document.usesImplicitDevice = false;
		document.usesImplicitWorkspace = false;
		UInkHeaderExtension extension;
		UInkDevice first;
		first.guid = Guid("40000000-0000-4000-8000-00000000d101");
		first.deviceType = 1;
		first.geometry = UInkWindowDevice{
			Guid("40000000-0000-4000-8000-00000000d102"), 0, 0, 100, 100, 0 };
		UInkDevice second;
		second.guid = Guid("40000000-0000-4000-8000-00000000d102");
		second.deviceType = 1;
		second.geometry = UInkWindowDevice{
			Guid("40000000-0000-4000-8000-00000000d101"), 0, 0, 100, 100, 0 };
		extension.devices = { first, second };
		UInkWorkspace firstWorkspace;
		firstWorkspace.guid = Guid("30000000-0000-4000-8000-00000000d101");
		firstWorkspace.workspaceType = 1;
		firstWorkspace.parentWorkspaceGuid = Guid("30000000-0000-4000-8000-00000000d102");
		UInkWorkspace secondWorkspace;
		secondWorkspace.guid = Guid("30000000-0000-4000-8000-00000000d102");
		secondWorkspace.workspaceType = 1;
		secondWorkspace.parentWorkspaceGuid = Guid("30000000-0000-4000-8000-00000000d101");
		extension.workspaces = { firstWorkspace, secondWorkspace };
		document.headerExtension = std::move(extension);
		UInkCanvas canvas;
		canvas.workspaceGuid = firstWorkspace.guid;
		canvas.deviceGuid = first.guid;
		canvas.pageGuid = Guid("50000000-0000-4000-8000-00000000d101");
		canvas.pageIndex = 0;
		canvas.pageNumber = 1;
		canvas.layerIndex = 0;
		canvas.layerNumber = 0;
		canvas.viewport = UInkViewport{};
		document.canvases.push_back(std::move(canvas));
		return document;
	}

	void TestMalformedAndRecovery(TestState& state)
	{
		const std::vector<std::byte> prefix = CanvasPrefix();
		UINK_CHECK(state, !prefix.empty());
		if (prefix.empty()) return;

		std::vector<std::byte> duplicateTail = prefix;
		const std::vector<std::byte> duplicate = PackDuplicateInk();
		AppendBytes(duplicateTail, duplicate);
		const UInkReadResult duplicateResult = DecodeUInk(duplicateTail);
		UINK_CHECK(state, duplicateResult.status == UInkReadStatus::RecoveredInvalidTail);
		UINK_CHECK(state, duplicateResult.safeAppendOffset &&
			*duplicateResult.safeAppendOffset == prefix.size());
		UINK_CHECK(state, HasDiagnostic(duplicateResult, UInkDiagnosticCode::DuplicateKnownKey));

		std::vector<std::byte> invalidExtra = prefix;
		AppendBytes(invalidExtra, PackInkWithInvalidExtraKey());
		const UInkReadResult invalidExtraResult = DecodeUInk(invalidExtra);
		UINK_CHECK(state, invalidExtraResult.status == UInkReadStatus::RecoveredInvalidTail);
		UINK_CHECK(state, HasDiagnostic(invalidExtraResult, UInkDiagnosticCode::InvalidFieldType));
		UInkDocument invalidExtraDocument = MakeBasicDocument(false);
		invalidExtraDocument.canvases[0].extra = UInkExtra{
			{ Value(uint64_t{ 7 }), Value(std::string("value")) } };
		UINK_CHECK(state, EncodeUInkDocument(invalidExtraDocument).status ==
			UInkEncodeStatus::InvalidModel);

		std::vector<std::byte> duplicateHardware = HeaderPrefix();
		AppendBytes(duplicateHardware, PackHeaderExtensionWithDuplicateHardwareIdentifiers());
		AppendBytes(duplicateHardware, PackCanvasForExplicitDevice());
		const UInkReadResult duplicateHardwareResult = DecodeUInk(duplicateHardware);
		UINK_CHECK(state, duplicateHardwareResult.status == UInkReadStatus::Complete);
		UINK_CHECK(state, duplicateHardwareResult.provenance.usedFieldFallback);
		UINK_CHECK(state, duplicateHardwareResult.document &&
			duplicateHardwareResult.document->headerExtension &&
			!duplicateHardwareResult.document->headerExtension->devices[0].hardware);
		if (duplicateHardwareResult.document)
		{
			UINK_CHECK(state, EncodeUInkDocument(*duplicateHardwareResult.document).status ==
				UInkEncodeStatus::Success);
		}

		UInkAppendObject validInk = MakeInk(0, 0);
		const UInkEncodeResult validInkBytes = EncodeUInkAppendObjects(
			std::span<const UInkAppendObject>(&validInk, 1));
		std::vector<std::byte> invalidMiddle = duplicateTail;
		AppendBytes(invalidMiddle, validInkBytes.bytes);
		const UInkReadResult middleResult = DecodeUInk(invalidMiddle);
		UINK_CHECK(state, middleResult.status == UInkReadStatus::Complete);
		UINK_CHECK(state, middleResult.document &&
			middleResult.document->canvases[0].content.size() == 1);
		UINK_CHECK(state, middleResult.provenance.invalidBlockBeforeValidBlock);
		UINK_CHECK(state, middleResult.provenance.requiresSaveAs);
		UINK_CHECK(state, !middleResult.safeAppendOffset.has_value());

		std::vector<std::byte> invalidCanvasScope = prefix;
		AppendBytes(invalidCanvasScope, PackInvalidCanvas());
		AppendBytes(invalidCanvasScope, validInkBytes.bytes);
		const UInkReadResult invalidCanvasScopeResult = DecodeUInk(invalidCanvasScope);
		UINK_CHECK(state, invalidCanvasScopeResult.status == UInkReadStatus::RecoveredInvalidTail);
		UINK_CHECK(state, invalidCanvasScopeResult.document &&
			invalidCanvasScopeResult.document->canvases[0].content.empty());
		UINK_CHECK(state, invalidCanvasScopeResult.safeAppendOffset &&
			*invalidCanvasScopeResult.safeAppendOffset == prefix.size());

		UInkAppendObject closedShape = MakeShape(0, 2);
		UInkEncodeResult invalidStrokeBytes = EncodeUInkAppendObjects(
			std::span<const UInkAppendObject>(&closedShape, 1));
		UINK_CHECK(state, ReplaceFirstAscii(invalidStrokeBytes.bytes, "opacity", "opacXXy"));
		std::vector<std::byte> invalidStroke = prefix;
		AppendBytes(invalidStroke, invalidStrokeBytes.bytes);
		const UInkReadResult invalidStrokeResult = DecodeUInk(invalidStroke);
		UINK_CHECK(state, invalidStrokeResult.status == UInkReadStatus::Complete);
		UINK_CHECK(state, invalidStrokeResult.provenance.usedFieldFallback);
		if (invalidStrokeResult.document &&
			!invalidStrokeResult.document->canvases[0].content.empty())
		{
			const UInkShape* shape = std::get_if<UInkShape>(
				&invalidStrokeResult.document->canvases[0].content[0]);
			UINK_CHECK(state, shape && !shape->stroke && shape->fill.has_value());
		}
		else UINK_CHECK(state, false);

		std::vector<std::byte> corrupt = prefix;
		corrupt.push_back(std::byte{ 0xc1 });
		AppendBytes(corrupt, validInkBytes.bytes);
		const UInkReadResult corruptResult = DecodeUInk(corrupt);
		UINK_CHECK(state, corruptResult.status == UInkReadStatus::StoppedAtCorruptTail);
		UINK_CHECK(state, corruptResult.failedObjectOffset &&
			*corruptResult.failedObjectOffset == prefix.size());
		UINK_CHECK(state, corruptResult.safeAppendOffset &&
			*corruptResult.safeAppendOffset == prefix.size());

		std::vector<std::byte> truncated = prefix;
		AppendBytes(truncated, std::span<const std::byte>(validInkBytes.bytes).first(
			validInkBytes.bytes.size() / 2));
		const UInkReadResult truncatedResult = DecodeUInk(truncated);
		UINK_CHECK(state, truncatedResult.status == UInkReadStatus::RecoveredTruncatedTail);
		UINK_CHECK(state, truncatedResult.safeAppendOffset &&
			*truncatedResult.safeAppendOffset == prefix.size());

		std::vector<std::byte> nanTail = prefix;
		AppendBytes(nanTail, PackInvalidInk(true));
		UINK_CHECK(state, DecodeUInk(nanTail).status == UInkReadStatus::RecoveredInvalidTail);
		std::vector<std::byte> hugeCoordinate = prefix;
		AppendBytes(hugeCoordinate, PackInkWithHugeIntegerCoordinate());
		UINK_CHECK(state, DecodeUInk(hugeCoordinate).status ==
			UInkReadStatus::RecoveredInvalidTail);
		std::vector<std::byte> unknownShape = prefix;
		AppendBytes(unknownShape, PackUnknownShape());
		const UInkReadResult unknownShapeResult = DecodeUInk(unknownShape);
		UINK_CHECK(state, unknownShapeResult.status == UInkReadStatus::RecoveredInvalidTail);
		UINK_CHECK(state, unknownShapeResult.document &&
			unknownShapeResult.document->canvases[0].content.empty());

		UInkDocument privateInkDocument = MakeBasicDocument();
		UInkInk& privateInk = std::get<UInkInk>(privateInkDocument.canvases[0].content[0]);
		privateInk.declaredInkType = 128;
		privateInk.effectiveKind = UInkInkKind::Pen;
		privateInk.declaredTexture = 129;
		privateInk.effectiveTexture = 0;
		const UInkReadResult privateInkResult = DecodeUInk(
			EncodeUInkDocument(privateInkDocument).bytes);
		UINK_CHECK(state, privateInkResult.document.has_value());
		UINK_CHECK(state, privateInkResult.provenance.requiresSaveAs);
		if (privateInkResult.document)
		{
			const UInkInk& decodedInk = std::get<UInkInk>(
				privateInkResult.document->canvases[0].content[0]);
			UINK_CHECK(state, decodedInk.declaredInkType == 128 &&
				decodedInk.effectiveKind == UInkInkKind::Pen);
			UINK_CHECK(state, decodedInk.declaredTexture == 129 && decodedInk.effectiveTexture == 0);
		}

		UInkDocument unknownDeviceDocument = MakeRichDocument();
		unknownDeviceDocument.headerExtension->devices[0].deviceType = 128;
		unknownDeviceDocument.headerExtension->devices[0].geometry = UInkUnknownDevice{};
		const UInkReadResult unknownDeviceResult = DecodeUInk(
			EncodeUInkDocument(unknownDeviceDocument).bytes);
		UINK_CHECK(state, unknownDeviceResult.document &&
			unknownDeviceResult.provenance.usedTemporaryIdentity &&
			unknownDeviceResult.provenance.requiresSaveAs &&
			!unknownDeviceResult.safeAppendOffset);
		if (unknownDeviceResult.document && unknownDeviceResult.document->headerExtension)
		{
			UINK_CHECK(state, std::holds_alternative<UInkUnknownDevice>(
				unknownDeviceResult.document->headerExtension->devices[0].geometry));
		}

		std::vector<std::byte> unknownStyledInk = prefix;
		AppendBytes(unknownStyledInk, PackUnknownInkWithPointStyle());
		const UInkReadResult unknownStyledInkResult = DecodeUInk(unknownStyledInk);
		UINK_CHECK(state, unknownStyledInkResult.status == UInkReadStatus::Complete);
		UINK_CHECK(state, unknownStyledInkResult.document &&
			unknownStyledInkResult.document->canvases[0].content.size() == 1);
		if (unknownStyledInkResult.document &&
			!unknownStyledInkResult.document->canvases[0].content.empty())
		{
			const UInkInk* decodedInk = std::get_if<UInkInk>(
				&unknownStyledInkResult.document->canvases[0].content[0]);
			UINK_CHECK(state, decodedInk && decodedInk->declaredInkType == 128 &&
				decodedInk->effectiveKind == UInkInkKind::Pen &&
				decodedInk->points.size() == 1 && !decodedInk->points[0].style);
		}
		else UINK_CHECK(state, false);

		std::vector<std::byte> invalidOrder = HeaderPrefix();
		AppendBytes(invalidOrder, validInkBytes.bytes);
		UInkAppendObject canvasObject = MakeBasicDocument(false).canvases[0];
		const UInkEncodeResult canvasBytes = EncodeUInkAppendObjects(
			std::span<const UInkAppendObject>(&canvasObject, 1));
		AppendBytes(invalidOrder, canvasBytes.bytes);
		const UInkReadResult orderResult = DecodeUInk(invalidOrder);
		UINK_CHECK(state, orderResult.provenance.invalidBlockBeforeValidBlock);
		UINK_CHECK(state, orderResult.document && orderResult.document->canvases.size() == 1);
		UINK_CHECK(state, !orderResult.safeAppendOffset);

		std::vector<std::byte> temporaryIdentity = HeaderPrefix();
		AppendBytes(temporaryIdentity, PackCanvasWithInvalidIdentity(false));
		const UInkReadResult temporaryResult = DecodeUInk(temporaryIdentity);
		UINK_CHECK(state, temporaryResult.document && temporaryResult.document->canvases.size() == 1);
		UINK_CHECK(state, temporaryResult.provenance.usedTemporaryIdentity);
		UINK_CHECK(state, !temporaryResult.safeAppendOffset);

		std::vector<std::byte> highInteger = HeaderPrefix();
		AppendBytes(highInteger, PackCanvasWithInvalidIdentity(true));
		const UInkReadResult highIntegerResult = DecodeUInk(highInteger);
		UINK_CHECK(state, highIntegerResult.status == UInkReadStatus::Complete);
		UINK_CHECK(state, highIntegerResult.document &&
			highIntegerResult.document->canvases.size() == 1);
		UINK_CHECK(state, highIntegerResult.provenance.usedTemporaryIdentity);
		UINK_CHECK(state, !highIntegerResult.safeAppendOffset);

		std::vector<std::byte> invalidViewport = HeaderPrefix();
		AppendBytes(invalidViewport, PackCanvasWithViewport(0,
			std::numeric_limits<float>::quiet_NaN()));
		const UInkReadResult invalidViewportResult = DecodeUInk(invalidViewport);
		UINK_CHECK(state, invalidViewportResult.status == UInkReadStatus::Complete);
		UINK_CHECK(state, invalidViewportResult.provenance.usedFieldFallback);
		UINK_CHECK(state, invalidViewportResult.document &&
			invalidViewportResult.document->canvases[0].viewport &&
			NearlyEqual(invalidViewportResult.document->canvases[0].viewport->scale, 1.0f));

		std::vector<std::byte> inheritedViewport = CanvasPrefix();
		AppendBytes(inheritedViewport, PackCanvasWithViewport(1, 2.0f));
		const UInkReadResult inheritedViewportResult = DecodeUInk(inheritedViewport);
		UINK_CHECK(state, inheritedViewportResult.status == UInkReadStatus::Complete);
		UINK_CHECK(state, inheritedViewportResult.provenance.usedFieldFallback);
		UINK_CHECK(state, inheritedViewportResult.document &&
			inheritedViewportResult.document->canvases.size() == 2 &&
			!inheritedViewportResult.document->canvases[1].viewport.has_value());

		std::vector<std::byte> badHeader = HeaderPrefix();
		badHeader[0] = std::byte{ 0x96 };
		UINK_CHECK(state, DecodeUInk(badHeader).status == UInkReadStatus::RejectedHeader);
		badHeader = HeaderPrefix();
		badHeader[6] = std::byte{ 0x0b };
		const UInkReadResult unsupportedVersion = DecodeUInk(badHeader);
		UINK_CHECK(state, unsupportedVersion.status == UInkReadStatus::RejectedHeader);
		UINK_CHECK(state, HasDiagnostic(unsupportedVersion,
			UInkDiagnosticCode::UnsupportedVersion));
		badHeader = HeaderPrefix();
		badHeader[9] = std::byte{ 'z' };
		UINK_CHECK(state, DecodeUInk(badHeader).status == UInkReadStatus::RejectedHeader);
		UINK_CHECK(state, DecodeUInk(std::span<const std::byte>{}).status ==
			UInkReadStatus::RejectedHeader);

		UINK_CHECK(state, EncodeUInkDocument(MakeCycleDocument()).status ==
			UInkEncodeStatus::InvalidModel);
		UInkDocument validTree = MakeCycleDocument();
		validTree.header.workspaceNum = 1;
		validTree.headerExtension->devices[0].deviceType = 0;
		validTree.headerExtension->devices[0].geometry = UInkDisplayDevice{ 0, 0, 1920, 1080 };
		validTree.headerExtension->workspaces.resize(1);
		validTree.headerExtension->workspaces[0].parentWorkspaceGuid.reset();
		const UInkEncodeResult validTreeBytes = EncodeUInkDocument(validTree);
		UINK_CHECK(state, validTreeBytes.status == UInkEncodeStatus::Success);
		std::vector<std::byte> cycleBytes = validTreeBytes.bytes;
		UINK_CHECK(state, ReplaceGuidAfterKey(cycleBytes, "parentDeviceGuid",
			"40000000-0000-4000-8000-00000000d102"));
		const UInkReadResult cycle = DecodeUInk(cycleBytes);
		UINK_CHECK(state, cycle.document && cycle.document->headerExtension);
		UINK_CHECK(state, cycle.provenance.usedTemporaryIdentity);
		UINK_CHECK(state, HasDiagnostic(cycle, UInkDiagnosticCode::ParentCycle));
		UINK_CHECK(state, !cycle.safeAppendOffset);

		std::vector<std::byte> pptMissing = EncodeUInkDocument(MakeRichDocument()).bytes;
		UINK_CHECK(state, ReplaceFirstAscii(pptMissing, "slideId", "slideXX"));
		const UInkReadResult pptResult = DecodeUInk(pptMissing);
		UINK_CHECK(state, pptResult.document && pptResult.document->canvases[2].presentationUnbound);
		UINK_CHECK(state, pptResult.provenance.usedTemporaryIdentity);

		UInkCanvas mismatchedPpt = MakeRichDocument().canvases[2];
		mismatchedPpt.deviceGuid = Guid("40000000-0000-4000-8000-00000000c001");
		mismatchedPpt.slideId = 43;
		mismatchedPpt.content.clear();
		const UInkAppendObject mismatchedPptObject = mismatchedPpt;
		const UInkEncodeResult mismatchedPptBytes = EncodeUInkAppendObjects(
			std::span<const UInkAppendObject>(&mismatchedPptObject, 1));
		std::vector<std::byte> mismatchedPptStream =
			EncodeUInkDocument(MakeRichDocument()).bytes;
		AppendBytes(mismatchedPptStream, mismatchedPptBytes.bytes);
		const UInkReadResult mismatchedPptResult = DecodeUInk(mismatchedPptStream);
		UINK_CHECK(state, mismatchedPptResult.document &&
			mismatchedPptResult.document->canvases.size() == 4);
		if (mismatchedPptResult.document && mismatchedPptResult.document->canvases.size() == 4)
		{
			UINK_CHECK(state, mismatchedPptResult.document->canvases[2].presentationUnbound);
			UINK_CHECK(state, mismatchedPptResult.document->canvases[3].presentationUnbound);
		}
		UINK_CHECK(state, mismatchedPptResult.provenance.usedTemporaryIdentity);
		UINK_CHECK(state, !mismatchedPptResult.safeAppendOffset);

		std::vector<std::byte> invalidDeviceParent =
			EncodeUInkDocument(MakeRichDocument()).bytes;
		UINK_CHECK(state, ReplaceGuidAfterKey(invalidDeviceParent, "parentDeviceGuid",
			"z0000000-0000-4000-8000-00000000c001"));
		const UInkReadResult invalidDeviceParentResult = DecodeUInk(invalidDeviceParent);
		UINK_CHECK(state, invalidDeviceParentResult.document &&
			invalidDeviceParentResult.document->headerExtension &&
			invalidDeviceParentResult.document->headerExtension->devices.size() == 2);
		if (invalidDeviceParentResult.document &&
			invalidDeviceParentResult.document->headerExtension)
		{
			UINK_CHECK(state, !invalidDeviceParentResult.document->headerExtension->
				devices[1].parentResolved);
		}
		UINK_CHECK(state, invalidDeviceParentResult.provenance.usedTemporaryIdentity);

		std::vector<std::byte> invalidWorkspaceParent =
			EncodeUInkDocument(MakeRichDocument()).bytes;
		UINK_CHECK(state, ReplaceGuidAfterKey(invalidWorkspaceParent, "parentWorkspaceGuid",
			"z0000000-0000-4000-8000-00000000c001"));
		const UInkReadResult invalidWorkspaceParentResult = DecodeUInk(invalidWorkspaceParent);
		UINK_CHECK(state, invalidWorkspaceParentResult.document &&
			invalidWorkspaceParentResult.document->headerExtension &&
			invalidWorkspaceParentResult.document->headerExtension->workspaces.size() == 2);
		if (invalidWorkspaceParentResult.document &&
			invalidWorkspaceParentResult.document->headerExtension)
		{
			const UInkWorkspace& workspace = invalidWorkspaceParentResult.document->
				headerExtension->workspaces[1];
			UINK_CHECK(state, !workspace.parentResolved && !workspace.parentWorkspaceGuid);
		}
		UINK_CHECK(state, invalidWorkspaceParentResult.provenance.usedTemporaryIdentity);

		std::vector<std::byte> invalidPageCount = CanvasPrefix();
		AppendBytes(invalidPageCount, PackMediaWithInvalidPageCount());
		const UInkReadResult invalidPageCountResult = DecodeUInk(invalidPageCount);
		UINK_CHECK(state, invalidPageCountResult.status == UInkReadStatus::Complete);
		UINK_CHECK(state, invalidPageCountResult.provenance.usedFieldFallback);
		if (invalidPageCountResult.document &&
			!invalidPageCountResult.document->canvases[0].content.empty())
		{
			const UInkMedia* media = std::get_if<UInkMedia>(
				&invalidPageCountResult.document->canvases[0].content[0]);
			UINK_CHECK(state, media && !media->pageCount.has_value());
		}
		else UINK_CHECK(state, false);

		UINK_CHECK(state, IsSafeMediaPath("images/pixel.png"));
		UINK_CHECK(state, IsSafeMediaPath("images/\xc3\xa9.png"));
		UINK_CHECK(state, !IsSafeMediaPath("images/e\xcc\x81.png"));
		const char* unsafePaths[] = { "../image.png", "images/../image.png", "/image.png",
			"C:/image.png", "https://example.test/image.png", "images\\image.png",
			"images//image.png", "./image.png", "images/control\x01.png",
			"images/control-\xc2\x85.png" };
		for (const char* path : unsafePaths) UINK_CHECK(state, !IsSafeMediaPath(path));
	}

	void CheckLimitFailure(TestState& state, const UInkReadResult& result)
	{
		UINK_CHECK(state, result.status == UInkReadStatus::LimitExceeded);
		UINK_CHECK(state, !result.document.has_value());
		UINK_CHECK(state, !result.safeAppendOffset.has_value());
		UINK_CHECK(state, HasDiagnostic(result, UInkDiagnosticCode::LimitExceeded));
	}

	void TestEncoderTopologyValidation(TestState& state)
	{
		UInkDocument document = MakeBasicDocument();
		document.header.pageNum = 0;
		UINK_CHECK(state, EncodeUInkDocument(document).status == UInkEncodeStatus::InvalidModel);

		document = MakeBasicDocument();
		document.canvases[0].pageIndex = 1;
		UINK_CHECK(state, EncodeUInkDocument(document).status == UInkEncodeStatus::InvalidModel);

		document = MakeRichDocument();
		document.canvases[1].layerIndex = 2;
		UINK_CHECK(state, EncodeUInkDocument(document).status == UInkEncodeStatus::InvalidModel);

		document = MakeRichDocument();
		document.canvases[0].deviceGuid =
			Guid("40000000-0000-4000-8000-00000000ffff");
		UINK_CHECK(state, EncodeUInkDocument(document).status == UInkEncodeStatus::InvalidModel);

		document = MakeRichDocument();
		document.canvases[2].slideId.reset();
		UINK_CHECK(state, EncodeUInkDocument(document).status == UInkEncodeStatus::InvalidModel);

		document = MakeRichDocument();
		UInkCanvas secondPptDevice = document.canvases[2];
		secondPptDevice.deviceGuid = Guid("40000000-0000-4000-8000-00000000c001");
		secondPptDevice.slideId = 43;
		secondPptDevice.content.clear();
		document.canvases.push_back(std::move(secondPptDevice));
		UINK_CHECK(state, EncodeUInkDocument(document).status == UInkEncodeStatus::InvalidModel);

		document = MakeRichDocument();
		UInkCanvas duplicatePptSlide = document.canvases[2];
		duplicatePptSlide.pageGuid = Guid("50000000-0000-4000-8000-00000000c003");
		duplicatePptSlide.pageIndex = 1;
		duplicatePptSlide.content.clear();
		document.header.pageNum = 3;
		document.canvases.push_back(std::move(duplicatePptSlide));
		UINK_CHECK(state, EncodeUInkDocument(document).status == UInkEncodeStatus::InvalidModel);

		document = MakeRichDocument();
		document.headerExtension->workspaces[0].currentPageIndex = 1;
		UINK_CHECK(state, EncodeUInkDocument(document).status == UInkEncodeStatus::InvalidModel);
	}

	void TestReadLimits(TestState& state)
	{
		const UInkReadLimits defaults;
		UINK_CHECK(state, defaults.maxFileBytes == 128ull * 1024ull * 1024ull);
		UINK_CHECK(state, defaults.maxModelCharge == 256ull * 1024ull * 1024ull);
		UINK_CHECK(state, defaults.maxTopLevelObjects == 250000);
		UINK_CHECK(state, defaults.maxTopLevelObjectBytes == 32ull * 1024ull * 1024ull);
		UINK_CHECK(state, defaults.maxGeometryPoints == 4000000);
		UINK_CHECK(state, defaults.maxSingleGeometryPoints == 1000000);
		UINK_CHECK(state, defaults.maxDepth == 32);
		UINK_CHECK(state, defaults.maxStringOrBinaryBytes == 8ull * 1024ull * 1024ull);
		UINK_CHECK(state, defaults.maxContainerEntries == 1000000);
		UINK_CHECK(state, defaults.maxDiagnostics == 4096);
		UINK_CHECK(state, defaults.maxMediaPathBytes == 32768);

		const UInkEncodeResult basicEncoded = EncodeUInkDocument(MakeBasicDocument());
		UINK_CHECK(state, basicEncoded.status == UInkEncodeStatus::Success);
		const UInkReadResult basic = DecodeUInk(basicEncoded.bytes);
		UINK_CHECK(state, basic.status == UInkReadStatus::Complete);
		UINK_CHECK(state, basic.geometryPointCount == 2);

		UInkReadLimits limits;
		limits.maxFileBytes = basicEncoded.bytes.size();
		UINK_CHECK(state, DecodeUInk(basicEncoded.bytes, limits).status == UInkReadStatus::Complete);
		limits.maxFileBytes = basicEncoded.bytes.size() - 1;
		CheckLimitFailure(state, DecodeUInk(basicEncoded.bytes, limits));

		limits = {};
		limits.maxTopLevelObjects = basic.decodedObjectCount;
		UINK_CHECK(state, DecodeUInk(basicEncoded.bytes, limits).status == UInkReadStatus::Complete);
		limits.maxTopLevelObjects = basic.decodedObjectCount - 1;
		CheckLimitFailure(state, DecodeUInk(basicEncoded.bytes, limits));

		const std::vector<size_t> sizes = ObjectSizes(basicEncoded.bytes);
		const size_t largest = *std::max_element(sizes.begin(), sizes.end());
		limits = {};
		limits.maxTopLevelObjectBytes = largest;
		UINK_CHECK(state, DecodeUInk(basicEncoded.bytes, limits).status == UInkReadStatus::Complete);
		limits.maxTopLevelObjectBytes = largest - 1;
		CheckLimitFailure(state, DecodeUInk(basicEncoded.bytes, limits));

		limits = {};
		limits.maxGeometryPoints = 2;
		limits.maxSingleGeometryPoints = 2;
		UINK_CHECK(state, DecodeUInk(basicEncoded.bytes, limits).status == UInkReadStatus::Complete);
		limits.maxSingleGeometryPoints = 1;
		CheckLimitFailure(state, DecodeUInk(basicEncoded.bytes, limits));

		UInkDocument twoInks = MakeBasicDocument();
		std::get<UInkInk>(twoInks.canvases[0].content[0]).points.resize(1);
		UInkInk secondInk = MakeInk(1, 1);
		secondInk.points.resize(1);
		twoInks.canvases[0].content.push_back(std::move(secondInk));
		const std::vector<std::byte> twoInkBytes = EncodeUInkDocument(twoInks).bytes;
		limits = {};
		limits.maxSingleGeometryPoints = 1;
		limits.maxGeometryPoints = 2;
		UINK_CHECK(state, DecodeUInk(twoInkBytes, limits).status == UInkReadStatus::Complete);
		limits.maxGeometryPoints = 1;
		CheckLimitFailure(state, DecodeUInk(twoInkBytes, limits));

		const std::vector<std::byte> nestedBytes = EncodeUInkDocument(
			MakeNestedExtraDocument(4, 12, 40)).bytes;
		uint32_t minimumDepth = 0;
		for (uint32_t depth = 2; depth <= defaults.maxDepth; ++depth)
		{
			limits = {};
			limits.maxDepth = depth;
			if (DecodeUInk(nestedBytes, limits).status == UInkReadStatus::Complete)
			{
				minimumDepth = depth;
				break;
			}
		}
		UINK_CHECK(state, minimumDepth > 2);
		if (minimumDepth > 2)
		{
			limits = {};
			limits.maxDepth = minimumDepth;
			UINK_CHECK(state, DecodeUInk(nestedBytes, limits).status == UInkReadStatus::Complete);
			limits.maxDepth = minimumDepth - 1;
			CheckLimitFailure(state, DecodeUInk(nestedBytes, limits));
		}

		limits = {};
		limits.maxContainerEntries = 13;
		UINK_CHECK(state, DecodeUInk(nestedBytes, limits).status == UInkReadStatus::Complete);
		limits.maxContainerEntries = 12;
		CheckLimitFailure(state, DecodeUInk(nestedBytes, limits));
		limits = {};
		limits.maxStringOrBinaryBytes = 40;
		UINK_CHECK(state, DecodeUInk(nestedBytes, limits).status == UInkReadStatus::Complete);
		limits.maxStringOrBinaryBytes = 39;
		CheckLimitFailure(state, DecodeUInk(nestedBytes, limits));
		limits = {};
		limits.maxStringOrBinaryBytes = 36;
		UINK_CHECK(state, DecodeUInk(CanvasPrefix(), limits).status == UInkReadStatus::Complete);
		limits.maxStringOrBinaryBytes = 35;
		CheckLimitFailure(state, DecodeUInk(CanvasPrefix(), limits));

		limits = {};
		limits.maxModelCharge = basic.modelCharge;
		UINK_CHECK(state, DecodeUInk(basicEncoded.bytes, limits).status == UInkReadStatus::Complete);
		limits.maxModelCharge = basic.modelCharge - 1;
		CheckLimitFailure(state, DecodeUInk(basicEncoded.bytes, limits));
		limits = {};
		limits.maxModelCharge = 127;
		CheckLimitFailure(state, DecodeUInk(basicEncoded.bytes, limits));

		const std::vector<std::byte> richBytes = EncodeUInkDocument(MakeRichDocument()).bytes;
		const size_t mediaPathBytes = std::string("media/picture.png").size();
		limits = {};
		limits.maxMediaPathBytes = static_cast<uint32_t>(mediaPathBytes);
		UINK_CHECK(state, DecodeUInk(richBytes, limits).status == UInkReadStatus::Complete);
		limits.maxMediaPathBytes = static_cast<uint32_t>(mediaPathBytes - 1);
		CheckLimitFailure(state, DecodeUInk(richBytes, limits));

		std::vector<std::byte> diagnosticsBytes = CanvasPrefix();
		for (uint16_t type = 900; type < 906; ++type) AppendBytes(diagnosticsBytes, PackUnknown(type));
		limits = {};
		limits.maxDiagnostics = 3;
		const UInkReadResult diagnostics = DecodeUInk(diagnosticsBytes, limits);
		UINK_CHECK(state, diagnostics.status == UInkReadStatus::Complete);
		UINK_CHECK(state, diagnostics.diagnostics.size() == 3);
		UINK_CHECK(state, diagnostics.diagnostics.back().code ==
			UInkDiagnosticCode::DiagnosticsTruncated);

		std::vector<std::byte> deepUnknown = CanvasPrefix();
		AppendBytes(deepUnknown, PackUnknown(999, 3));
		limits = {};
		limits.maxDepth = 5;
		UINK_CHECK(state, DecodeUInk(deepUnknown, limits).status == UInkReadStatus::Complete);
		limits.maxDepth = 4;
		CheckLimitFailure(state, DecodeUInk(deepUnknown, limits));

		// 大量微小节点的 wire bytes 很少，但 msgpack 临时对象树仍必须先计费。
		std::vector<std::byte> wideUnknown = CanvasPrefix();
		AppendBytes(wideUnknown, PackWideUnknown(4096));
		limits = {};
		limits.maxModelCharge = 64 * 1024;
		CheckLimitFailure(state, DecodeUInk(wideUnknown, limits));
	}

	class TempDirectory
	{
	public:
		TempDirectory()
		{
			std::array<wchar_t, MAX_PATH + 1> base = {};
			const DWORD length = GetTempPathW(static_cast<DWORD>(base.size()), base.data());
			const std::optional<UInkGuid> guid = CreateUInkGuid();
			if (length == 0 || length >= base.size() || !guid) return;
			const std::string token = FormatUInkGuid(*guid);
			path_.assign(base.data(), length);
			path_ += L"Inkeys-UInk-tests-";
			path_.append(token.begin(), token.end());
			if (!CreateDirectoryW(path_.c_str(), nullptr)) path_.clear();
		}

		~TempDirectory()
		{
			ResetUInkFileTestFaultInjection();
			if (path_.empty()) return;
			WIN32_FIND_DATAW data = {};
			HANDLE find = FindFirstFileW((path_ + L"\\*").c_str(), &data);
			if (find != INVALID_HANDLE_VALUE)
			{
				do
				{
					if (std::wcscmp(data.cFileName, L".") == 0 ||
						std::wcscmp(data.cFileName, L"..") == 0 ||
						(data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0) continue;
					DeleteFileW((path_ + L"\\" + data.cFileName).c_str());
				} while (FindNextFileW(find, &data));
				FindClose(find);
			}
			RemoveDirectoryW(path_.c_str());
		}

		bool IsValid() const noexcept { return !path_.empty(); }
		std::wstring File(const wchar_t* name) const { return path_ + L"\\" + name; }

		bool CopyFixture(const wchar_t* fixture, const wchar_t* target) const
		{
			return CopyFileW((FixtureDirectory() + L"\\" + fixture).c_str(),
				File(target).c_str(), FALSE) != FALSE;
		}

	private:
		std::wstring path_;
	};

	class FaultScope
	{
	public:
		explicit FaultScope(UInkFileTestFaultInjection faults)
		{
			SetUInkFileTestFaultInjection(faults);
		}
		~FaultScope() { ResetUInkFileTestFaultInjection(); }
		FaultScope(const FaultScope&) = delete;
		FaultScope& operator=(const FaultScope&) = delete;
	};

	UInkAppendPlan PlanInkAppend(const UInkReadResult& source,
		uint32_t contentId, uint32_t undoId)
	{
		UInkAppendBatch batch;
		batch.objects.emplace_back(MakeInk(contentId, undoId));
		return AnalyzeUInkAppend(source, batch);
	}

	void TestFullFileSave(TestState& state)
	{
		TempDirectory temporary;
		UINK_CHECK(state, temporary.IsValid());
		if (!temporary.IsValid()) return;
		const std::wstring sourcePath = temporary.File(L"save.uink");
		UINK_CHECK(state, temporary.CopyFixture(L"implicit-single-canvas.uink", L"save.uink"));
		UInkReadResult source = ReadUInkFile(sourcePath);
		UINK_CHECK(state, source.status == UInkReadStatus::Complete);
		std::optional<UInkEditingSession> externalSession = CreateUInkEditingSession(source);
		UINK_CHECK(state, externalSession.has_value());
		UINK_CHECK(state, externalSession && externalSession->provenance.requiresSaveAs);
		if (!externalSession) return;
		externalSession->document.canvases[0].content.push_back(MakeInk(1, 1));
		const std::vector<std::byte> importedBefore = ReadBytes(sourcePath);
		UINK_CHECK(state, SaveUInkFile(sourcePath, *externalSession).status ==
			UInkSaveStatus::InvalidSession);
		UINK_CHECK(state, ReadBytes(sourcePath) == importedBefore);

		std::optional<UInkEditingSession> session = CreateUInkEditingSession(source,
			UInkEditingSource::ApplicationOwned);
		UINK_CHECK(state, session.has_value());
		if (!session) return;
		const UInkGuid originalGuid = session->document.header.guid;
		const UInkGuid originalPageGuid = session->document.canvases[0].pageGuid;
		session->document.canvases[0].content.push_back(MakeInk(1, 1));
		const UInkSaveResult saved = SaveUInkFile(sourcePath, *session);
		UINK_CHECK(state, saved.status == UInkSaveStatus::Committed);
		UINK_CHECK(state, saved.revision.has_value());
		const UInkReadResult reloaded = ReadUInkFile(sourcePath);
		UINK_CHECK(state, reloaded.document && reloaded.document->header.guid == originalGuid);
		if (reloaded.document)
		{
			UINK_CHECK(state, reloaded.document->canvases[0].pageGuid == originalPageGuid);
			UINK_CHECK(state, reloaded.document->canvases[0].content.size() == 2);
			UINK_CHECK(state, reloaded.document->header.pageNum == 1);
		}

		const std::wstring saveAsPath = temporary.File(L"save-as.uink");
		UInkSaveOptions saveAsOptions;
		saveAsOptions.mode = UInkSaveMode::SaveAsNewLogicalFile;
		const UInkSaveResult saveAs = SaveUInkFile(saveAsPath, *externalSession, saveAsOptions);
		UINK_CHECK(state, saveAs.status == UInkSaveStatus::Committed);
		const UInkReadResult saveAsRead = ReadUInkFile(saveAsPath);
		UINK_CHECK(state, saveAsRead.document && saveAsRead.document->header.guid != originalGuid);
		UINK_CHECK(state, saveAsRead.document &&
			saveAsRead.document->canvases[0].pageGuid == originalPageGuid);

		const std::wstring retainedIdentityPath =
			temporary.File(L"create-new-retained-identity.uink");
		UInkSaveOptions retainedIdentityOptions;
		retainedIdentityOptions.mode = UInkSaveMode::CreateNewLogicalFileWithIdentity;
		const UInkSaveResult retainedIdentity = SaveUInkFile(
			retainedIdentityPath, *session, retainedIdentityOptions);
		UINK_CHECK(state, retainedIdentity.status == UInkSaveStatus::Committed);
		const UInkReadResult retainedIdentityRead = ReadUInkFile(retainedIdentityPath);
		UINK_CHECK(state, retainedIdentityRead.document &&
			retainedIdentityRead.document->header.guid == originalGuid);
		const std::vector<std::byte> retainedIdentityBytes = ReadBytes(retainedIdentityPath);
		UINK_CHECK(state, SaveUInkFile(retainedIdentityPath, *session,
			retainedIdentityOptions).status == UInkSaveStatus::SourceChanged);
		UINK_CHECK(state, ReadBytes(retainedIdentityPath) == retainedIdentityBytes);

		// 读取返回后不持有编辑期句柄，调用方可以立即获取独占访问。
		HANDLE exclusive = CreateFileW(sourcePath.c_str(), GENERIC_READ, 0, nullptr,
			OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
		UINK_CHECK(state, exclusive != INVALID_HANDLE_VALUE);
		if (exclusive != INVALID_HANDLE_VALUE) CloseHandle(exclusive);

		const std::wstring lockedPath = temporary.File(L"locked.uink");
		UINK_CHECK(state, temporary.CopyFixture(L"implicit-single-canvas.uink", L"locked.uink"));
		exclusive = CreateFileW(lockedPath.c_str(), GENERIC_READ, 0, nullptr,
			OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
		UINK_CHECK(state, exclusive != INVALID_HANDLE_VALUE);
		if (exclusive != INVALID_HANDLE_VALUE)
		{
			UInkFileAccessOptions noRetry;
			noRetry.transientRetryCount = 0;
			const UInkReadResult locked = ReadUInkFile(lockedPath, {}, noRetry);
			UINK_CHECK(state, locked.status == UInkReadStatus::IoError);
			CloseHandle(exclusive);
		}
		UINK_CHECK(state, ReadUInkFile(lockedPath).status == UInkReadStatus::Complete);

		const std::vector<std::byte> stableBytes = ReadBytes(sourcePath);
		auto runSaveFault = [&](const wchar_t* fixtureName,
			const UInkFileTestFaultInjection& faults, UInkSaveStatus expected)
		{
			const std::wstring path = temporary.File(fixtureName);
			UINK_CHECK(state, temporary.CopyFixture(L"implicit-single-canvas.uink", fixtureName));
			const std::vector<std::byte> before = ReadBytes(path);
			const UInkReadResult read = ReadUInkFile(path);
			const std::optional<UInkEditingSession> faultSession = CreateUInkEditingSession(read,
				UInkEditingSource::ApplicationOwned);
			UINK_CHECK(state, faultSession.has_value());
			if (!faultSession) return;
			FaultScope scope(faults);
			const UInkSaveResult result = SaveUInkFile(path, *faultSession);
			UINK_CHECK(state, result.status == expected);
			UINK_CHECK(state, ReadBytes(path) == before);
			UINK_CHECK(state, ReadUInkFile(path).document.has_value());
		};

		UInkFileTestFaultInjection faults;
		faults.failWriteAfterBytes = 1;
		runSaveFault(L"save-write-fault.uink", faults, UInkSaveStatus::IoError);
		faults = {};
		faults.failFlush = true;
		runSaveFault(L"save-flush-fault.uink", faults, UInkSaveStatus::IoError);
		faults = {};
		faults.failSelfValidation = true;
		runSaveFault(L"save-validation-fault.uink", faults,
			UInkSaveStatus::SelfValidationFailed);
		faults = {};
		faults.failCommit = true;
		runSaveFault(L"save-commit-fault.uink", faults, UInkSaveStatus::IoError);
		UINK_CHECK(state, ReadBytes(sourcePath) == stableBytes);

		// 替换后的最终内容复核失败时，旧 predecessor 必须继续作为恢复材料保留。
		const std::wstring finalValidationPath = temporary.File(L"save-final-validation.uink");
		UINK_CHECK(state, temporary.CopyFixture(L"implicit-single-canvas.uink",
			L"save-final-validation.uink"));
		const std::vector<std::byte> finalValidationBefore = ReadBytes(finalValidationPath);
		const std::optional<UInkEditingSession> finalValidationSession = CreateUInkEditingSession(
			ReadUInkFile(finalValidationPath), UInkEditingSource::ApplicationOwned);
		UINK_CHECK(state, finalValidationSession.has_value());
		if (finalValidationSession)
		{
			faults = {};
			faults.failCommittedRevisionValidation = true;
			FaultScope scope(faults);
			const UInkSaveResult result = SaveUInkFile(finalValidationPath,
				*finalValidationSession);
			UINK_CHECK(state, result.status == UInkSaveStatus::PartialCommitRequiresRecovery);
			UINK_CHECK(state, !result.recoveryPath.empty());
			UINK_CHECK(state, ReadBytes(result.recoveryPath) == finalValidationBefore);
			UINK_CHECK(state, ReadUInkFile(result.recoveryPath).document.has_value());
		}

		const std::wstring saveAsValidationPath =
			temporary.File(L"save-as-final-validation.uink");
		if (externalSession)
		{
			UInkSaveOptions saveAsValidation;
			saveAsValidation.mode = UInkSaveMode::SaveAsNewLogicalFile;
			faults = {};
			faults.failCommittedRevisionValidation = true;
			FaultScope scope(faults);
			const UInkSaveResult result = SaveUInkFile(saveAsValidationPath,
				*externalSession, saveAsValidation);
			UINK_CHECK(state, result.status == UInkSaveStatus::PartialCommitRequiresRecovery);
			UINK_CHECK(state, result.recoveryPath == saveAsValidationPath);
			UINK_CHECK(state, ReadUInkFile(saveAsValidationPath).document.has_value());
		}

		const std::wstring conflictPath = temporary.File(L"save-conflict.uink");
		UINK_CHECK(state, temporary.CopyFixture(L"implicit-single-canvas.uink", L"save-conflict.uink"));
		const UInkReadResult conflictRead = ReadUInkFile(conflictPath);
		const std::optional<UInkEditingSession> conflictSession =
			CreateUInkEditingSession(conflictRead, UInkEditingSource::ApplicationOwned);
		UINK_CHECK(state, conflictSession.has_value());
		std::vector<std::byte> externallyChanged = ReadBytes(conflictPath);
		externallyChanged.push_back(std::byte{ 0xc1 });
		UINK_CHECK(state, WriteBytes(conflictPath, externallyChanged));
		if (conflictSession)
		{
			const UInkSaveResult conflict = SaveUInkFile(conflictPath, *conflictSession);
			UINK_CHECK(state, conflict.status == UInkSaveStatus::SourceChanged);
			UINK_CHECK(state, ReadBytes(conflictPath) == externallyChanged);
		}

		const std::wstring unknownPath = temporary.File(L"normalize-unknown.uink");
		UINK_CHECK(state, temporary.CopyFixture(L"unknown-block.uink", L"normalize-unknown.uink"));
		const UInkReadResult unknownRead = ReadUInkFile(unknownPath);
		const std::optional<UInkEditingSession> unknownSession = CreateUInkEditingSession(unknownRead);
		UINK_CHECK(state, unknownSession.has_value());
		if (unknownSession)
		{
			UINK_CHECK(state, SaveUInkFile(unknownPath, *unknownSession).status ==
				UInkSaveStatus::InvalidSession);
			UInkSaveOptions normalize;
			normalize.mode = UInkSaveMode::NormalizeImportedWithExplicitLoss;
			const UInkSaveResult normalized = SaveUInkFile(unknownPath, *unknownSession, normalize);
			UINK_CHECK(state, normalized.status == UInkSaveStatus::Committed);
			const UInkReadResult normalizedRead = ReadUInkFile(unknownPath);
			UINK_CHECK(state, !normalizedRead.provenance.containsUnknownTopLevel);
			UINK_CHECK(state, normalizedRead.document &&
				normalizedRead.document->header.guid == unknownSession->document.header.guid);
		}

		// 字段降级结果只能经显式另存为写出，并转换为可再次严格读取的规范值。
		const std::wstring fallbackPath = temporary.File(L"save-fallback.uink");
		std::vector<std::byte> fallbackBytes = CanvasPrefix();
		AppendBytes(fallbackBytes, PackInvalidInk(false, -1));
		UINK_CHECK(state, WriteBytes(fallbackPath, fallbackBytes));
		const UInkReadResult fallbackRead = ReadUInkFile(fallbackPath);
		UINK_CHECK(state, fallbackRead.status == UInkReadStatus::Complete);
		UINK_CHECK(state, fallbackRead.provenance.usedFieldFallback);
		UINK_CHECK(state, fallbackRead.provenance.requiresSaveAs);
		const std::optional<UInkEditingSession> fallbackSession =
			CreateUInkEditingSession(fallbackRead, UInkEditingSource::ApplicationOwned);
		UINK_CHECK(state, fallbackSession.has_value());
		if (fallbackSession)
		{
			const std::vector<std::byte> before = ReadBytes(fallbackPath);
			UINK_CHECK(state, SaveUInkFile(fallbackPath, *fallbackSession).status ==
				UInkSaveStatus::InvalidSession);
			UINK_CHECK(state, ReadBytes(fallbackPath) == before);
			const std::wstring fallbackSaveAsPath = temporary.File(L"save-fallback-as.uink");
			UInkSaveOptions fallbackSaveAs;
			fallbackSaveAs.mode = UInkSaveMode::SaveAsNewLogicalFile;
			UINK_CHECK(state, SaveUInkFile(fallbackSaveAsPath, *fallbackSession,
				fallbackSaveAs).status == UInkSaveStatus::Committed);
			const UInkReadResult strictFallback = ReadUInkFile(fallbackSaveAsPath);
			UINK_CHECK(state, strictFallback.status == UInkReadStatus::Complete);
			UINK_CHECK(state, !strictFallback.provenance.usedFieldFallback);
			if (strictFallback.document && !strictFallback.document->canvases.empty() &&
				!strictFallback.document->canvases[0].content.empty())
			{
				const UInkInk* ink = std::get_if<UInkInk>(
					&strictFallback.document->canvases[0].content[0]);
				UINK_CHECK(state, ink && ink->declaredInkType == 1 &&
					ink->effectiveKind == UInkInkKind::Pen);
			}
			else UINK_CHECK(state, false);
		}

		// 显式规范化只收敛未知 effective 值，已注册高级内容仍保持强类型语义。
		UInkDocument richFallbackDocument = MakeRichDocument();
		richFallbackDocument.canvases[0].content.pop_back(); // 首版文件保存不处理 Media 资源包。
		richFallbackDocument.headerExtension->workspaces[0].workspaceType = 128;
		const UInkEncodeResult richFallbackBytes = EncodeUInkDocument(richFallbackDocument);
		UINK_CHECK(state, richFallbackBytes.status == UInkEncodeStatus::Success);
		const std::wstring richFallbackPath = temporary.File(L"save-rich-fallback.uink");
		UINK_CHECK(state, WriteBytes(richFallbackPath, richFallbackBytes.bytes));
		const UInkReadResult richFallbackRead = ReadUInkFile(richFallbackPath);
		UINK_CHECK(state, richFallbackRead.document &&
			richFallbackRead.provenance.usedFieldFallback &&
			!richFallbackRead.provenance.usedTemporaryIdentity);
		const std::optional<UInkEditingSession> richFallbackSession =
			CreateUInkEditingSession(richFallbackRead);
		UINK_CHECK(state, richFallbackSession.has_value());
		if (richFallbackSession)
		{
			const std::wstring normalizedPath = temporary.File(L"save-rich-normalized.uink");
			UInkSaveOptions saveAs;
			saveAs.mode = UInkSaveMode::SaveAsNewLogicalFile;
			UINK_CHECK(state, SaveUInkFile(normalizedPath, *richFallbackSession,
				saveAs).status == UInkSaveStatus::Committed);
			const UInkReadResult normalized = ReadUInkFile(normalizedPath);
			UINK_CHECK(state, normalized.status == UInkReadStatus::Complete);
			UINK_CHECK(state, !normalized.provenance.usedFieldFallback);
			if (normalized.document && normalized.document->headerExtension &&
				!normalized.document->canvases.empty())
			{
				const UInkDocument& document = *normalized.document;
				const UInkCanvas& primary = document.canvases[0];
				UINK_CHECK(state, document.headerExtension->workspaces[0].workspaceType == 1);
				UINK_CHECK(state, primary.content.size() == 9);
				const UInkInk* advanced = std::get_if<UInkInk>(&primary.content[0]);
				const UInkShape* line = std::get_if<UInkShape>(&primary.content[2]);
				const UInkShape* square = std::get_if<UInkShape>(&primary.content[5]);
				UINK_CHECK(state, advanced && advanced->declaredInkType == 3 &&
					advanced->declaredTexture == 0 && advanced->color.extended &&
					advanced->points[0].style &&
					advanced->points[0].style->color.extended);
				UINK_CHECK(state, line && line->stroke &&
					line->stroke->declaredStartMarker == 0 &&
					line->stroke->declaredEndMarker == 1);
				UINK_CHECK(state, square && square->fill &&
					square->fill->declaredFillType == 0 && square->fill->color.extended);
			}
			else UINK_CHECK(state, false);
		}

		// 闭合图形上的端点标记应在读取时降级，并可通过另存为规范化为无标记。
		const std::wstring closedMarkerPath = temporary.File(L"save-closed-marker.uink");
		std::vector<std::byte> closedMarkerBytes = CanvasPrefix();
		AppendBytes(closedMarkerBytes, PackClosedShapeWithMarkers());
		UINK_CHECK(state, WriteBytes(closedMarkerPath, closedMarkerBytes));
		const UInkReadResult closedMarkerRead = ReadUInkFile(closedMarkerPath);
		UINK_CHECK(state, closedMarkerRead.status == UInkReadStatus::Complete);
		UINK_CHECK(state, closedMarkerRead.provenance.usedFieldFallback);
		const std::optional<UInkEditingSession> closedMarkerSession =
			CreateUInkEditingSession(closedMarkerRead, UInkEditingSource::ApplicationOwned);
		UINK_CHECK(state, closedMarkerSession.has_value());
		if (closedMarkerSession)
		{
			const std::wstring normalizedPath = temporary.File(L"save-closed-marker-as.uink");
			UInkSaveOptions normalizeMarkers;
			normalizeMarkers.mode = UInkSaveMode::SaveAsNewLogicalFile;
			UINK_CHECK(state, SaveUInkFile(normalizedPath, *closedMarkerSession,
				normalizeMarkers).status == UInkSaveStatus::Committed);
			const UInkReadResult normalized = ReadUInkFile(normalizedPath);
			UINK_CHECK(state, normalized.status == UInkReadStatus::Complete);
			UINK_CHECK(state, !normalized.provenance.usedFieldFallback);
			if (normalized.document && !normalized.document->canvases.empty() &&
				!normalized.document->canvases[0].content.empty())
			{
				const UInkShape* shape = std::get_if<UInkShape>(
					&normalized.document->canvases[0].content[0]);
				UINK_CHECK(state, shape && shape->stroke &&
					shape->stroke->declaredStartMarker == 0 &&
					shape->stroke->declaredEndMarker == 0);
			}
			else UINK_CHECK(state, false);
		}

		// 截断恢复只能显式规范化；普通保存不得静默丢弃尾部。
		const std::wstring truncatedPath = temporary.File(L"save-truncated.uink");
		std::vector<std::byte> truncatedBytes = CanvasPrefix();
		const UInkAppendObject trailingInk = MakeInk(0, 0);
		const UInkEncodeResult trailingInkBytes = EncodeUInkAppendObjects(
			std::span<const UInkAppendObject>(&trailingInk, 1));
		AppendBytes(truncatedBytes, std::span<const std::byte>(trailingInkBytes.bytes).first(
			trailingInkBytes.bytes.size() / 2));
		UINK_CHECK(state, WriteBytes(truncatedPath, truncatedBytes));
		const UInkReadResult truncatedRead = ReadUInkFile(truncatedPath);
		UINK_CHECK(state, truncatedRead.status == UInkReadStatus::RecoveredTruncatedTail);
		UINK_CHECK(state, truncatedRead.provenance.requiresSaveAs);
		const std::optional<UInkEditingSession> truncatedSession =
			CreateUInkEditingSession(truncatedRead, UInkEditingSource::ApplicationOwned);
		UINK_CHECK(state, truncatedSession.has_value());
		if (truncatedSession)
		{
			UINK_CHECK(state, SaveUInkFile(truncatedPath, *truncatedSession).status ==
				UInkSaveStatus::InvalidSession);
			UInkSaveOptions normalizeTruncated;
			normalizeTruncated.mode = UInkSaveMode::NormalizeImportedWithExplicitLoss;
			UINK_CHECK(state, SaveUInkFile(truncatedPath, *truncatedSession,
				normalizeTruncated).status == UInkSaveStatus::Committed);
			const UInkReadResult normalizedTruncated = ReadUInkFile(truncatedPath);
			UINK_CHECK(state, normalizedTruncated.status == UInkReadStatus::Complete);
			UINK_CHECK(state, normalizedTruncated.document &&
				normalizedTruncated.document->canvases[0].content.empty());
		}

		// 临时身份无法证明逻辑对象身份，任何完整保存模式都必须拒绝。
		std::vector<std::byte> temporaryIdentityBytes = HeaderPrefix();
		AppendBytes(temporaryIdentityBytes, PackCanvasWithInvalidIdentity(false));
		const UInkReadResult temporaryIdentityRead = DecodeUInk(temporaryIdentityBytes);
		UINK_CHECK(state, temporaryIdentityRead.provenance.usedTemporaryIdentity);
		UInkReadResult fileBackedTemporaryIdentity = temporaryIdentityRead;
		fileBackedTemporaryIdentity.sourcePath = fallbackPath;
		fileBackedTemporaryIdentity.sourceRevision = ReadUInkFile(fallbackPath).sourceRevision;
		const std::optional<UInkEditingSession> temporaryIdentitySession =
			CreateUInkEditingSession(fileBackedTemporaryIdentity,
				UInkEditingSource::ApplicationOwned);
		UINK_CHECK(state, temporaryIdentitySession.has_value());
		if (temporaryIdentitySession)
		{
			const std::wstring rejectedPath = temporary.File(L"temporary-identity-as.uink");
			UInkSaveOptions rejectedSaveAs;
			rejectedSaveAs.mode = UInkSaveMode::SaveAsNewLogicalFile;
			UINK_CHECK(state, SaveUInkFile(rejectedPath, *temporaryIdentitySession,
				rejectedSaveAs).status == UInkSaveStatus::InvalidSession);
			UINK_CHECK(state, GetFileAttributesW(rejectedPath.c_str()) == INVALID_FILE_ATTRIBUTES);
		}
	}

	void TestAppendTransactions(TestState& state)
	{
		TempDirectory temporary;
		UINK_CHECK(state, temporary.IsValid());
		if (!temporary.IsValid()) return;

		const std::wstring appendPath = temporary.File(L"append.uink");
		UINK_CHECK(state, temporary.CopyFixture(L"implicit-single-canvas.uink", L"append.uink"));
		UInkReadResult source = ReadUInkFile(appendPath);
		UInkAppendPlan plan = PlanInkAppend(source, 1, 1);
		UINK_CHECK(state, plan.status == UInkAppendPlanStatus::Ready);
		const UInkAppendResult appended = ExecuteUInkAppend(appendPath, plan);
		UINK_CHECK(state, appended.status == UInkAppendStatus::CommittedDurable);
		UINK_CHECK(state, appended.revision.has_value());
		source = ReadUInkFile(appendPath);
		UINK_CHECK(state, source.document && source.document->canvases[0].content.size() == 2);

		UInkAppendBatch shapeBatch;
		shapeBatch.objects.emplace_back(MakeShape(2, 0));
		plan = AnalyzeUInkAppend(source, shapeBatch);
		UINK_CHECK(state, plan.status == UInkAppendPlanStatus::Ready);
		UInkAppendOptions buffered;
		buffered.durability = UInkAppendDurability::Buffered;
		const UInkAppendResult bufferedResult = ExecuteUInkAppend(appendPath, plan, buffered);
		UINK_CHECK(state, bufferedResult.status == UInkAppendStatus::CommittedBuffered);
		UINK_CHECK(state, ReadUInkFile(appendPath).document->canvases[0].content.size() == 3);

		const std::wstring canvasPath = temporary.File(L"append-canvas.uink");
		UINK_CHECK(state, temporary.CopyFixture(L"implicit-single-canvas.uink", L"append-canvas.uink"));
		const UInkReadResult canvasSource = ReadUInkFile(canvasPath);
		UInkCanvas newCanvas;
		newCanvas.pageGuid = Guid("20000000-0000-4000-8000-00000000e002");
		newCanvas.pageIndex = 1;
		newCanvas.pageNumber = 2;
		newCanvas.layerIndex = 0;
		newCanvas.layerNumber = 0;
		newCanvas.viewport = UInkViewport{ 10.0f, 20.0f, 1.0f };
		UInkAppendBatch canvasBatch;
		canvasBatch.objects.emplace_back(newCanvas);
		UInkAppendPlan canvasPlan = AnalyzeUInkAppend(canvasSource, canvasBatch);
		UINK_CHECK(state, canvasPlan.status == UInkAppendPlanStatus::Ready);
		UINK_CHECK(state, ExecuteUInkAppend(canvasPath, canvasPlan).status ==
			UInkAppendStatus::CommittedDurable);
		const UInkReadResult twoPages = ReadUInkFile(canvasPath);
		UINK_CHECK(state, twoPages.document && twoPages.document->canvases.size() == 2);
		UINK_CHECK(state, HasDiagnostic(twoPages, UInkDiagnosticCode::HeaderSnapshotMismatch));

		const std::wstring recoveredPath = temporary.File(L"append-recovered.uink");
		UINK_CHECK(state, temporary.CopyFixture(L"truncated-tail.uink", L"append-recovered.uink"));
		const UInkReadResult recoveredSource = ReadUInkFile(recoveredPath);
		UINK_CHECK(state, recoveredSource.status == UInkReadStatus::RecoveredTruncatedTail);
		UInkAppendPlan recoveredPlan = PlanInkAppend(recoveredSource, 1, 1);
		UINK_CHECK(state, recoveredPlan.status == UInkAppendPlanStatus::Ready);
		UINK_CHECK(state, recoveredPlan.truncateOffset == 380);
		UINK_CHECK(state, ExecuteUInkAppend(recoveredPath, recoveredPlan).status ==
			UInkAppendStatus::CommittedDurable);
		const UInkReadResult recoveredRead = ReadUInkFile(recoveredPath);
		UINK_CHECK(state, recoveredRead.status == UInkReadStatus::Complete);
		UINK_CHECK(state, recoveredRead.document &&
			recoveredRead.document->canvases[0].content.size() == 2);

		// 只有 Header 的合法空文件可直接追加第一个 Canvas，但不能直接追加内容。
		const std::wstring firstCanvasPath = temporary.File(L"append-first-canvas.uink");
		const std::vector<std::byte> headerOnly = HeaderPrefix();
		UINK_CHECK(state, WriteBytes(firstCanvasPath, headerOnly));
		const UInkReadResult headerOnlySource = ReadUInkFile(firstCanvasPath);
		UINK_CHECK(state, headerOnlySource.status == UInkReadStatus::Complete);
		UINK_CHECK(state, headerOnlySource.safeAppendOffset &&
			*headerOnlySource.safeAppendOffset == headerOnly.size());
		UINK_CHECK(state, PlanInkAppend(headerOnlySource, 0, 0).status ==
			UInkAppendPlanStatus::InvalidBatch);
		UInkCanvas firstCanvas = MakeBasicDocument(false).canvases[0];
		UInkAppendBatch firstCanvasBatch;
		firstCanvasBatch.objects.emplace_back(std::move(firstCanvas));
		const UInkAppendPlan firstCanvasPlan = AnalyzeUInkAppend(headerOnlySource, firstCanvasBatch);
		UINK_CHECK(state, firstCanvasPlan.status == UInkAppendPlanStatus::Ready);
		UINK_CHECK(state, ExecuteUInkAppend(firstCanvasPath, firstCanvasPlan).status ==
			UInkAppendStatus::CommittedDurable);
		const UInkReadResult firstCanvasRead = ReadUInkFile(firstCanvasPath);
		UINK_CHECK(state, firstCanvasRead.document && firstCanvasRead.document->canvases.size() == 1);

		const std::wstring conflictPath = temporary.File(L"append-conflict.uink");
		UINK_CHECK(state, temporary.CopyFixture(L"implicit-single-canvas.uink", L"append-conflict.uink"));
		const UInkReadResult conflictSource = ReadUInkFile(conflictPath);
		const UInkAppendPlan conflictPlan = PlanInkAppend(conflictSource, 1, 1);
		std::vector<std::byte> conflictBytes = ReadBytes(conflictPath);
		conflictBytes.push_back(std::byte{ 0xc1 });
		UINK_CHECK(state, WriteBytes(conflictPath, conflictBytes));
		const UInkAppendResult conflict = ExecuteUInkAppend(conflictPath, conflictPlan);
		UINK_CHECK(state, conflict.status == UInkAppendStatus::SourceChanged);
		UINK_CHECK(state, ReadBytes(conflictPath) == conflictBytes);

		const std::wstring digestPath = temporary.File(L"append-digest.uink");
		UINK_CHECK(state, temporary.CopyFixture(L"implicit-single-canvas.uink", L"append-digest.uink"));
		UInkAppendPlan digestPlan = PlanInkAppend(ReadUInkFile(digestPath), 1, 1);
		const std::vector<std::byte> digestBefore = ReadBytes(digestPath);
		if (!digestPlan.bytes.empty()) digestPlan.bytes[0] ^= std::byte{ 0x01 };
		UINK_CHECK(state, ExecuteUInkAppend(digestPath, digestPlan).status ==
			UInkAppendStatus::InvalidPlan);
		UINK_CHECK(state, ReadBytes(digestPath) == digestBefore);
		digestPlan = PlanInkAppend(ReadUInkFile(digestPath), 1, 1);
		if (digestPlan.truncateOffset != 0) --digestPlan.truncateOffset;
		UINK_CHECK(state, ExecuteUInkAppend(digestPath, digestPlan).status ==
			UInkAppendStatus::InvalidPlan);
		UINK_CHECK(state, ReadBytes(digestPath) == digestBefore);

		auto prepareFaultPlan = [&](const wchar_t* name,
			std::vector<std::byte>& before, UInkAppendPlan& outputPlan)
		{
			UINK_CHECK(state, temporary.CopyFixture(L"implicit-single-canvas.uink", name));
			const std::wstring path = temporary.File(name);
			before = ReadBytes(path);
			outputPlan = PlanInkAppend(ReadUInkFile(path), 1, 1);
			UINK_CHECK(state, outputPlan.status == UInkAppendPlanStatus::Ready);
		};

		std::vector<std::byte> before;
		UInkAppendPlan faultPlan;
		prepareFaultPlan(L"append-write-fault.uink", before, faultPlan);
		UInkFileTestFaultInjection faults;
		faults.failWriteAfterBytes = std::max<uint64_t>(1, faultPlan.bytes.size() / 2);
		{
			FaultScope scope(faults);
			const UInkAppendResult result = ExecuteUInkAppend(
				temporary.File(L"append-write-fault.uink"), faultPlan);
			UINK_CHECK(state, result.status == UInkAppendStatus::WriteFailedRolledBack);
		}
		UINK_CHECK(state, ReadBytes(temporary.File(L"append-write-fault.uink")) == before);

		prepareFaultPlan(L"append-rollback-flush-fault.uink", before, faultPlan);
		faults = {};
		faults.failWriteAfterBytes = std::max<uint64_t>(1, faultPlan.bytes.size() / 2);
		faults.failFlush = true;
		{
			FaultScope scope(faults);
			const UInkAppendResult result = ExecuteUInkAppend(
				temporary.File(L"append-rollback-flush-fault.uink"), faultPlan);
			UINK_CHECK(state, result.status == UInkAppendStatus::PartialCommitRequiresRecovery);
			UINK_CHECK(state, result.observedLength == before.size());
		}
		UINK_CHECK(state, ReadBytes(temporary.File(L"append-rollback-flush-fault.uink")) == before);

		const std::wstring repairedFailurePath = temporary.File(L"append-repair-fault.uink");
		UINK_CHECK(state, temporary.CopyFixture(L"truncated-tail.uink", L"append-repair-fault.uink"));
		const std::vector<std::byte> truncatedOriginal = ReadBytes(repairedFailurePath);
		const UInkAppendPlan repairFaultPlan = PlanInkAppend(
			ReadUInkFile(repairedFailurePath), 1, 1);
		faults = {};
		faults.failWriteAfterBytes = std::max<uint64_t>(1, repairFaultPlan.bytes.size() / 2);
		{
			FaultScope scope(faults);
			const UInkAppendResult result = ExecuteUInkAppend(repairedFailurePath, repairFaultPlan);
			UINK_CHECK(state, result.status == UInkAppendStatus::TailRepairedNoAppend);
			UINK_CHECK(state, result.observedLength == 380);
		}
		const std::vector<std::byte> repairedBytes = ReadBytes(repairedFailurePath);
		UINK_CHECK(state, repairedBytes.size() == 380);
		UINK_CHECK(state, std::equal(repairedBytes.begin(), repairedBytes.end(),
			truncatedOriginal.begin()));

		prepareFaultPlan(L"append-rollback-fault.uink", before, faultPlan);
		faults = {};
		faults.failWriteAfterBytes = std::max<uint64_t>(1, faultPlan.bytes.size() / 2);
		faults.failRollback = true;
		{
			FaultScope scope(faults);
			const UInkAppendResult result = ExecuteUInkAppend(
				temporary.File(L"append-rollback-fault.uink"), faultPlan);
			UINK_CHECK(state, result.status == UInkAppendStatus::PartialCommitRequiresRecovery);
			UINK_CHECK(state, result.lastTrustedBoundary == before.size());
			UINK_CHECK(state, result.observedLength > before.size());
		}

		prepareFaultPlan(L"append-flush-fault.uink", before, faultPlan);
		faults = {};
		faults.failFlush = true;
		{
			FaultScope scope(faults);
			const UInkAppendResult result = ExecuteUInkAppend(
				temporary.File(L"append-flush-fault.uink"), faultPlan);
			UINK_CHECK(state, result.status == UInkAppendStatus::WrittenNotDurable);
			UINK_CHECK(state, result.revision.has_value());
		}
		UINK_CHECK(state, ReadUInkFile(temporary.File(L"append-flush-fault.uink")).document->
			canvases[0].content.size() == 2);

		prepareFaultPlan(L"append-final-validation.uink", before, faultPlan);
		faults = {};
		faults.failCommittedRevisionValidation = true;
		{
			FaultScope scope(faults);
			const UInkAppendResult result = ExecuteUInkAppend(
				temporary.File(L"append-final-validation.uink"), faultPlan);
			UINK_CHECK(state, result.status == UInkAppendStatus::PartialCommitRequiresRecovery);
			UINK_CHECK(state, !result.revision.has_value());
			UINK_CHECK(state, std::any_of(result.diagnostics.begin(), result.diagnostics.end(),
				[](const UInkDiagnostic& diagnostic)
				{
					return diagnostic.code == UInkDiagnosticCode::SourceChanged;
				}));
		}
		UINK_CHECK(state, ReadUInkFile(temporary.File(L"append-final-validation.uink")).document->
			canvases[0].content.size() == 2);

		prepareFaultPlan(L"append-buffered.uink", before, faultPlan);
		faults = {};
		faults.failFlush = true;
		{
			FaultScope scope(faults);
			const UInkAppendResult result = ExecuteUInkAppend(
				temporary.File(L"append-buffered.uink"), faultPlan, buffered);
			UINK_CHECK(state, result.status == UInkAppendStatus::CommittedBuffered);
		}

		const std::wstring concurrentPath = temporary.File(L"append-concurrent.uink");
		UINK_CHECK(state, temporary.CopyFixture(L"implicit-single-canvas.uink", L"append-concurrent.uink"));
		const UInkReadResult concurrentSource = ReadUInkFile(concurrentPath);
		const UInkAppendPlan firstPlan = PlanInkAppend(concurrentSource, 1, 1);
		const UInkAppendPlan secondPlan = PlanInkAppend(concurrentSource, 1, 1);
		UInkAppendResult firstResult;
		UInkAppendResult secondResult;
		std::thread first([&] { firstResult = ExecuteUInkAppend(concurrentPath, firstPlan); });
		std::thread second([&] { secondResult = ExecuteUInkAppend(concurrentPath, secondPlan); });
		first.join();
		second.join();
		const int committedCount =
			(firstResult.status == UInkAppendStatus::CommittedDurable ? 1 : 0) +
			(secondResult.status == UInkAppendStatus::CommittedDurable ? 1 : 0);
		const int conflictCount =
			(firstResult.status == UInkAppendStatus::SourceChanged ? 1 : 0) +
			(secondResult.status == UInkAppendStatus::SourceChanged ? 1 : 0);
		UINK_CHECK(state, committedCount == 1 && conflictCount == 1);
		UINK_CHECK(state, ReadUInkFile(concurrentPath).document->canvases[0].content.size() == 2);
	}

	void TestMediaFilePolicy(TestState& state)
	{
		TempDirectory temporary;
		UINK_CHECK(state, temporary.IsValid());
		if (!temporary.IsValid()) return;
		const std::wstring mainPath = temporary.File(L"media.uink");
		const std::wstring extraPath = temporary.File(L"media.uink.extra");
		UINK_CHECK(state, temporary.CopyFixture(L"media-safe.uink", L"media.uink"));
		const std::array<std::byte, 5> sentinel = {
			std::byte{ 0x50 }, std::byte{ 0x4b }, std::byte{ 0x01 }, std::byte{ 0x02 }, std::byte{ 0x03 } };
		UINK_CHECK(state, WriteBytes(extraPath, sentinel));
		const std::vector<std::byte> extraBefore = ReadBytes(extraPath);
		const UInkReadResult source = ReadUInkFile(mainPath);
		UINK_CHECK(state, source.document && HasMedia(*source.document));
		const std::optional<UInkEditingSession> session = CreateUInkEditingSession(source);
		UINK_CHECK(state, session.has_value());
		if (!session) return;

		UINK_CHECK(state, SaveUInkFile(mainPath, *session).status ==
			UInkSaveStatus::ResourcePackUnsupported);
		UInkSaveOptions saveAs;
		saveAs.mode = UInkSaveMode::SaveAsNewLogicalFile;
		const std::wstring saveAsPath = temporary.File(L"media-save-as.uink");
		UINK_CHECK(state, SaveUInkFile(saveAsPath, *session, saveAs).status ==
			UInkSaveStatus::ResourcePackUnsupported);
		UINK_CHECK(state, GetFileAttributesW(saveAsPath.c_str()) == INVALID_FILE_ATTRIBUTES);
		UINK_CHECK(state, ReadBytes(extraPath) == extraBefore);

		UInkAppendPlan nonMedia = PlanInkAppend(source, 2, 2);
		UINK_CHECK(state, nonMedia.status == UInkAppendPlanStatus::Ready);
		UINK_CHECK(state, nonMedia.sourceContainsMedia);
		UINK_CHECK(state, ExecuteUInkAppend(mainPath, nonMedia).status ==
			UInkAppendStatus::CommittedDurable);
		UINK_CHECK(state, ReadBytes(extraPath) == extraBefore);
		const UInkReadResult appended = ReadUInkFile(mainPath);
		UINK_CHECK(state, appended.document && appended.document->canvases[0].content.size() == 3);

		UInkMedia media;
		media.contentId = 3;
		media.undoId = 3;
		media.path = "media/new.png";
		media.mimeType = "image/png";
		media.width = 1.0f;
		media.height = 1.0f;
		UInkAppendBatch mediaBatch;
		mediaBatch.objects.emplace_back(media);
		const std::vector<std::byte> mainBeforeRejectedAppend = ReadBytes(mainPath);
		HANDLE exclusive = CreateFileW(mainPath.c_str(), GENERIC_READ, 0, nullptr,
			OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
		UINK_CHECK(state, exclusive != INVALID_HANDLE_VALUE);
		const UInkAppendPlan mediaPlan = AnalyzeUInkAppend(appended, mediaBatch);
		UINK_CHECK(state, mediaPlan.status == UInkAppendPlanStatus::ResourcePackUnsupported);
		UINK_CHECK(state, ExecuteUInkAppend(mainPath, mediaPlan).status ==
			UInkAppendStatus::ResourcePackUnsupported);
		if (exclusive != INVALID_HANDLE_VALUE) CloseHandle(exclusive);
		UINK_CHECK(state, ReadBytes(mainPath) == mainBeforeRejectedAppend);
		UINK_CHECK(state, ReadBytes(extraPath) == extraBefore);
	}

	void TestAppendAnalysisRejections(TestState& state)
	{
		const std::wstring fixturePath = FixtureDirectory() + L"\\implicit-single-canvas.uink";
		const UInkReadResult source = ReadUInkFile(fixturePath);
		UInkAppendBatch empty;
		UINK_CHECK(state, AnalyzeUInkAppend(source, empty).status ==
			UInkAppendPlanStatus::InvalidBatch);
		UInkAppendBatch badContent;
		badContent.objects.emplace_back(MakeInk(8, 0));
		UINK_CHECK(state, AnalyzeUInkAppend(source, badContent).status ==
			UInkAppendPlanStatus::InvalidBatch);
		UInkCanvas gapCanvas;
		gapCanvas.pageGuid = Guid("20000000-0000-4000-8000-00000000e100");
		gapCanvas.pageIndex = 2;
		gapCanvas.pageNumber = 3;
		gapCanvas.layerIndex = 0;
		gapCanvas.layerNumber = 0;
		gapCanvas.viewport = UInkViewport{};
		UInkAppendBatch gapBatch;
		gapBatch.objects.emplace_back(gapCanvas);
		UINK_CHECK(state, AnalyzeUInkAppend(source, gapBatch).status ==
			UInkAppendPlanStatus::InvalidBatch);

		std::vector<std::byte> invalidMiddle = CanvasPrefix();
		AppendBytes(invalidMiddle, PackDuplicateInk());
		UInkAppendObject validInk = MakeInk(0, 0);
		const UInkEncodeResult validBytes = EncodeUInkAppendObjects(
			std::span<const UInkAppendObject>(&validInk, 1));
		AppendBytes(invalidMiddle, validBytes.bytes);
		UInkReadResult synthetic = DecodeUInk(invalidMiddle);
		synthetic.sourcePath = fixturePath;
		synthetic.sourceRevision = source.sourceRevision;
		UInkAppendBatch validBatch;
		validBatch.objects.emplace_back(MakeInk(1, 1));
		UINK_CHECK(state, AnalyzeUInkAppend(synthetic, validBatch).status ==
			UInkAppendPlanStatus::RequiresFullSave);

		UInkReadResult objectLimitSource = source;
		objectLimitSource.decodedObjectCount = UInkReadLimits{}.maxTopLevelObjects;
		UINK_CHECK(state, AnalyzeUInkAppend(objectLimitSource, validBatch).status ==
			UInkAppendPlanStatus::RequiresFullSave);
		UInkReadResult pointLimitSource = source;
		pointLimitSource.geometryPointCount = UInkReadLimits{}.maxGeometryPoints;
		UINK_CHECK(state, AnalyzeUInkAppend(pointLimitSource, validBatch).status ==
			UInkAppendPlanStatus::RequiresFullSave);

		TempDirectory temporary;
		UINK_CHECK(state, temporary.IsValid());
		if (temporary.IsValid())
		{
			const std::wstring richPath = temporary.File(L"append-ppt.uink");
			const UInkEncodeResult richBytes = EncodeUInkDocument(MakeRichDocument());
			UINK_CHECK(state, richBytes.status == UInkEncodeStatus::Success);
			UINK_CHECK(state, WriteBytes(richPath, richBytes.bytes));
			const UInkReadResult richSource = ReadUInkFile(richPath);
			UInkCanvas mismatchedPpt = MakeRichDocument().canvases[2];
			mismatchedPpt.deviceGuid = Guid("40000000-0000-4000-8000-00000000c001");
			mismatchedPpt.slideId = 43;
			mismatchedPpt.content.clear();
			UInkAppendBatch mismatchedBatch;
			mismatchedBatch.objects.emplace_back(std::move(mismatchedPpt));
			UINK_CHECK(state, AnalyzeUInkAppend(richSource, mismatchedBatch).status ==
				UInkAppendPlanStatus::InvalidBatch);
		}
	}

	void TestLatestVisibility(TestState& state)
	{
		UInkCanvas canvas;
		UInkInk first = MakeInk(0, 0);
		first.renderOnlyWhenLatest = true;
		canvas.content.push_back(first);
		UInkShape resultShape = MakeShape(1, 2);
		resultShape.renderOnlyWhenLatest = false;
		canvas.content.push_back(resultShape);
		UInkInk trailing = MakeInk(2, 2);
		trailing.renderOnlyWhenLatest = true;
		canvas.content.push_back(trailing);
		UInkMedia media;
		media.contentId = 3;
		media.undoId = 3;
		media.path = "media/reference.png";
		media.mimeType = "image/png";
		media.width = 1.0f;
		media.height = 1.0f;
		canvas.content.push_back(media);
		UInkShape finalMarked = MakeShape(4, 0);
		finalMarked.renderOnlyWhenLatest = true;
		canvas.content.push_back(finalMarked);
		const std::vector<bool> visible = ComputeUInkLatestVisibility(canvas);
		UINK_CHECK(state, visible.size() == 5);
		UINK_CHECK(state, !visible[0] && visible[1] && visible[2] && visible[3] && visible[4]);

		canvas.content.pop_back();
		canvas.content.pop_back();
		canvas.content.pop_back();
		const std::vector<bool> afterUndo = ComputeUInkLatestVisibility(canvas);
		UINK_CHECK(state, afterUndo.size() == 2);
		UINK_CHECK(state, !afterUndo[0] && afterUndo[1]);
		canvas.content.pop_back();
		const std::vector<bool> originalRestored = ComputeUInkLatestVisibility(canvas);
		UINK_CHECK(state, originalRestored.size() == 1 && originalRestored[0]);
	}

	void TestDraw3Export(TestState& state)
	{
		InkCanvasCollection collection(Draw3Guid(
			"30000000-0000-4000-8000-00000000f001"));
		InkPage page(Draw3Guid("50000000-0000-4000-8000-00000000f001"));
		InkCanvas* canvas = page.GetOrCreateCanvas(kDefaultDeviceKey,
			InkViewport{ 10.0f, -20.0f, 1.0f });
		UINK_CHECK(state, canvas != nullptr);
		if (!canvas) return;
		const StoredInkType types[] = { StoredInkType::Pen, StoredInkType::Highlighter,
			StoredInkType::Eraser, StoredInkType::SolidLine, StoredInkType::DashedLine,
			StoredInkType::OutlineRectangle, StoredInkType::FilledRectangle };
		for (size_t index = 0; index < std::size(types); ++index)
		{
			StoredInkStyle style;
			style.inkType = types[index];
			style.fallbackRgb = static_cast<uint32_t>(0x100000 + index);
			style.opacity = types[index] == StoredInkType::Highlighter ? 0.4f : 1.0f;
			style.texture = 0;
			std::vector<StoredInkPoint> points = {
				{ 10.0f + static_cast<float>(index), 20.0f, 4.0f },
				{ 110.0f + static_cast<float>(index), 60.0f, 4.0f } };
			const std::optional<size_t> appended = canvas->AppendStroke(
				InkStroke(style, std::move(points)));
			UINK_CHECK(state, appended == std::optional<size_t>(index));
		}
		UINK_CHECK(state, collection.AppendPage(std::move(page)) == std::optional<size_t>(0));

		Draw3UInkCaptureOptions options;
		options.fileGuid = Guid("10000000-0000-4000-8000-00000000f001");
		options.workspaceName = "draw3";
		options.dpiScale = 2.0f;
		const Draw3UInkCaptureResult captured = CaptureDraw3UInkExportSnapshot(collection, options);
		UINK_CHECK(state, captured.status == Draw3UInkExportStatus::Success);
		UINK_CHECK(state, captured.snapshot.has_value());
		if (!captured.snapshot) return;
		UINK_CHECK(state, captured.snapshot->assignedIndependentUndoGroups);
		UINK_CHECK(state, captured.snapshot->canvases.size() == 1);
		UINK_CHECK(state, captured.snapshot->canvases[0].strokes.size() == 7);

		const Draw3UInkExportResult exported = ExportDraw3SnapshotToUInk(*captured.snapshot);
		UINK_CHECK(state, exported.status == Draw3UInkExportStatus::Success);
		UINK_CHECK(state, exported.document.has_value());
		UINK_CHECK(state, exported.capabilities.inkCount == 3);
		UINK_CHECK(state, exported.capabilities.shapeCount == 4);
		UINK_CHECK(state, exported.capabilities.approximatedHighlighterNibCount == 1);
		UINK_CHECK(state, exported.capabilities.assignedIndependentUndoGroups);
		if (!exported.document) return;
		const UInkCanvas& output = exported.document->canvases[0];
		UINK_CHECK(state, output.workspaceGuid ==
			std::optional<UInkGuid>(Guid("30000000-0000-4000-8000-00000000f001")));
		UINK_CHECK(state, output.viewport && NearlyEqual(output.viewport->x, 10.0f));
		UINK_CHECK(state, output.content.size() == 7);
		UINK_CHECK(state, std::get<UInkInk>(output.content[0]).effectiveKind == UInkInkKind::Pen);
		UINK_CHECK(state, std::get<UInkInk>(output.content[1]).effectiveKind ==
			UInkInkKind::Highlighter);
		UINK_CHECK(state, std::get<UInkInk>(output.content[2]).effectiveKind == UInkInkKind::Erase);
		const UInkShape& solid = std::get<UInkShape>(output.content[3]);
		const UInkShape& dashed = std::get<UInkShape>(output.content[4]);
		const UInkShape& outline = std::get<UInkShape>(output.content[5]);
		const UInkShape& filled = std::get<UInkShape>(output.content[6]);
		UINK_CHECK(state, solid.declaredShapeType == 0 && solid.stroke);
		UINK_CHECK(state, dashed.declaredShapeType == 0 && dashed.stroke &&
			dashed.stroke->dashArray.size() == 2);
		if (dashed.stroke)
		{
			UINK_CHECK(state, NearlyEqual(dashed.stroke->dashArray[0], 16.0f));
			UINK_CHECK(state, NearlyEqual(dashed.stroke->dashArray[1], 24.0f));
		}
		UINK_CHECK(state, outline.declaredShapeType == 2 && outline.stroke && !outline.fill);
		UINK_CHECK(state, filled.declaredShapeType == 2 && !filled.stroke && filled.fill);
		const UInkRectangleGeometry* rectangle =
			std::get_if<UInkRectangleGeometry>(&outline.geometry);
		UINK_CHECK(state, rectangle && NearlyEqual(rectangle->cornerRadiusX, 8.0f));
		UINK_CHECK(state, EncodeUInkDocument(*exported.document).status == UInkEncodeStatus::Success);

		Draw3UInkExportSnapshot invalid = *captured.snapshot;
		invalid.canvases[0].strokes[0].points[0].width = 0.0f;
		UINK_CHECK(state, ExportDraw3SnapshotToUInk(invalid).status ==
			Draw3UInkExportStatus::InvalidSourceStroke);
		invalid = *captured.snapshot;
		invalid.canvases[0].viewport.scale = 2.0f;
		UINK_CHECK(state, ExportDraw3SnapshotToUInk(invalid).status ==
			Draw3UInkExportStatus::InvalidSnapshot);
		invalid = *captured.snapshot;
		invalid.canvases[0].pageIndex = 1;
		UINK_CHECK(state, ExportDraw3SnapshotToUInk(invalid).status ==
			Draw3UInkExportStatus::InvalidSnapshot);
		invalid = *captured.snapshot;
		invalid.canvases[0].strokes[0].style.texture = 1;
		UINK_CHECK(state, ExportDraw3SnapshotToUInk(invalid).status ==
			Draw3UInkExportStatus::InvalidSourceStroke);
		invalid = *captured.snapshot;
		invalid.canvases[0].strokes[0].style.kind =
			static_cast<Draw3UInkStrokeKind>(255);
		UINK_CHECK(state, ExportDraw3SnapshotToUInk(invalid).status ==
			Draw3UInkExportStatus::InvalidSourceStroke);
		invalid = *captured.snapshot;
		invalid.assignedIndependentUndoGroups = false;
		const Draw3UInkExportResult customGroups = ExportDraw3SnapshotToUInk(invalid);
		UINK_CHECK(state, customGroups.status == Draw3UInkExportStatus::Success);
		UINK_CHECK(state, !customGroups.capabilities.assignedIndependentUndoGroups);

		InkCanvasCollection mappedCollection(Draw3Guid(
			"30000000-0000-4000-8000-00000000f010"));
		InkPage mappedPage(Draw3Guid("50000000-0000-4000-8000-00000000f010"));
		InkCanvas* mappedCanvas = mappedPage.GetOrCreateCanvas(DeviceKey{ 42 });
		UINK_CHECK(state, mappedCanvas != nullptr);
		if (mappedCanvas)
		{
			StoredInkStyle style;
			style.inkType = StoredInkType::Pen;
			UINK_CHECK(state, mappedCanvas->AppendStroke(InkStroke(style,
				{ { 1.0f, 2.0f, 3.0f } })).has_value());
		}
		UINK_CHECK(state, mappedCollection.AppendPage(std::move(mappedPage)).has_value());
		Draw3UInkCaptureOptions missingMapping;
		missingMapping.fileGuid = Guid("10000000-0000-4000-8000-00000000f010");
		UINK_CHECK(state, CaptureDraw3UInkExportSnapshot(mappedCollection, missingMapping).status ==
			Draw3UInkExportStatus::MissingDeviceMapping);
		UInkDevice mappedDevice;
		mappedDevice.guid = Guid("40000000-0000-4000-8000-00000000f010");
		mappedDevice.deviceType = 0;
		mappedDevice.geometry = UInkDisplayDevice{ 0, 0, 1920, 1080 };
		missingMapping.devices.push_back({ DeviceKey{ 42 }, mappedDevice });
		const Draw3UInkCaptureResult mappedCapture =
			CaptureDraw3UInkExportSnapshot(mappedCollection, missingMapping);
		UINK_CHECK(state, mappedCapture.status == Draw3UInkExportStatus::Success);
		UINK_CHECK(state, mappedCapture.snapshot &&
			mappedCapture.snapshot->canvases[0].deviceGuid ==
			std::optional<UInkGuid>(mappedDevice.guid));

		InkCanvasCollection emptyPageCollection(Draw3Guid(
			"30000000-0000-4000-8000-00000000f020"));
		UINK_CHECK(state, emptyPageCollection.AppendPage(Draw3Guid(
			"50000000-0000-4000-8000-00000000f020")).has_value());
		Draw3UInkCaptureOptions emptyPageOptions;
		emptyPageOptions.fileGuid = Guid("10000000-0000-4000-8000-00000000f020");
		UINK_CHECK(state, CaptureDraw3UInkExportSnapshot(emptyPageCollection,
			emptyPageOptions).status == Draw3UInkExportStatus::InvalidSnapshot);
		if (mappedCapture.snapshot)
		{
			Draw3UInkExportSnapshot invalidDevice = *mappedCapture.snapshot;
			std::get<UInkDisplayDevice>(invalidDevice.devices[0].geometry).width = 0;
			const Draw3UInkExportResult invalidDeviceResult =
				ExportDraw3SnapshotToUInk(invalidDevice);
			UINK_CHECK(state, invalidDeviceResult.status ==
				Draw3UInkExportStatus::InvalidSnapshot);
			UINK_CHECK(state, !invalidDeviceResult.diagnostics.empty());
		}
	}
}

int RunUInkTests()
{
	TestState state;
	ResetUInkFileTestFaultInjection();
	TestOfficialFixtures(state);
	TestExactEncodingAndRichRoundTrip(state);
	TestMalformedAndRecovery(state);
	TestEncoderTopologyValidation(state);
	TestReadLimits(state);
	TestFullFileSave(state);
	TestAppendTransactions(state);
	TestMediaFilePolicy(state);
	TestAppendAnalysisRejections(state);
	TestLatestVisibility(state);
	TestDraw3Export(state);
	ResetUInkFileTestFaultInjection();
	if (state.failures == 0)
		std::cout << "All UInk persistence tests passed." << std::endl;
	return state.failures;
}
