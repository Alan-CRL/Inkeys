#define MSGPACK_NO_BOOST
#include <msgpack.hpp>

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <bcrypt.h>

#include "../inkStrokeModelerTest/draw3/shape_core.h"
#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cwchar>
#include <cstring>
#include <iostream>
#include <limits>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

#pragma comment(lib, "bcrypt.lib")

import draw3.uink_codec;
import draw3.uink_draw3_v11;
import draw3.uink_store;
import draw3.ink_document;
import draw3.ink_history;

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

	UInkClear MakeClear(uint32_t contentId, uint32_t undoId)
	{
		UInkClear clear;
		clear.contentId = contentId;
		clear.undoId = undoId;
		return clear;
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

	std::string FixtureSha256(std::span<const std::byte> bytes)
	{
		BCRYPT_ALG_HANDLE algorithm = nullptr;
		if (BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0) < 0)
			return {};
		BCRYPT_HASH_HANDLE hash = nullptr;
		std::array<UCHAR, 32> digest{};
		// 校验官方夹具原始字节，独立于产品读写路径。
		const bool success = BCryptCreateHash(algorithm, &hash, nullptr, 0, nullptr, 0, 0) >= 0 &&
			BCryptHashData(hash, reinterpret_cast<PUCHAR>(const_cast<std::byte*>(bytes.data())),
				static_cast<ULONG>(bytes.size()), 0) >= 0 &&
			BCryptFinishHash(hash, digest.data(), static_cast<ULONG>(digest.size()), 0) >= 0;
		if (hash) BCryptDestroyHash(hash);
		BCryptCloseAlgorithmProvider(algorithm, 0);
		if (!success) return {};

		constexpr char digits[] = "0123456789abcdef";
		std::string hex;
		hex.reserve(2 * digest.size());
		for (const auto byte : digest)
		{
			hex.push_back(digits[byte >> 4]);
			hex.push_back(digits[byte & 0x0f]);
		}
		return hex;
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
		const auto readFixture = [&](const wchar_t* name)
		{
			return DecodeUInk(ReadBytes(directory + L"\\" + name));
		};
		for (const Fixture& fixture : fixtures)
		{
			const auto bytes = ReadBytes(directory + L"\\" + fixture.name);
			UINK_CHECK(state, bytes.size() == fixture.bytes);
			UINK_CHECK(state, FixtureSha256(bytes) == fixture.sha256);
			const UInkReadResult result = DecodeUInk(bytes);
			UINK_CHECK(state, result.status == fixture.status);
			UINK_CHECK(state, result.document && result.summary.version == 10);
			UINK_CHECK(state, result.decodedObjectCount == fixture.objects);
		}

		const UInkReadResult explicitResult = readFixture(L"explicit-multilayer.uink");
		UINK_CHECK(state, explicitResult.document && explicitResult.document->headerExtension);
		if (explicitResult.document && explicitResult.document->headerExtension)
		{
			UINK_CHECK(state, explicitResult.document->headerExtension->devices.size() == 2);
			UINK_CHECK(state, explicitResult.document->headerExtension->workspaces.size() == 2);
			UINK_CHECK(state, explicitResult.document->canvases.size() == 3);
			UINK_CHECK(state, explicitResult.document->canvases[1].layerIndex == 1);
		}

		const UInkReadResult incremental = readFixture(L"incremental-tail.uink");
		UINK_CHECK(state, incremental.document && incremental.document->canvases.size() == 2);
		UINK_CHECK(state, HasDiagnostic(incremental, UInkDiagnosticCode::HeaderSnapshotMismatch));
		const UInkReadResult numeric = readFixture(L"numeric-compat.uink");
		UINK_CHECK(state, HasDiagnostic(numeric, UInkDiagnosticCode::NumericCompatibility));
		const UInkReadResult truncated = readFixture(L"truncated-tail.uink");
		UINK_CHECK(state, truncated.validPrefixLength == 380);
		UINK_CHECK(state, truncated.safeAppendOffset && *truncated.safeAppendOffset == 380);
		const UInkReadResult unknown = readFixture(L"unknown-block.uink");
		UINK_CHECK(state, unknown.provenance.containsUnknownTopLevel);
		UINK_CHECK(state, unknown.provenance.requiresSaveAs);
		UINK_CHECK(state, unknown.document && unknown.document->canvases[0].content.size() == 1);

		const UInkReadResult media = readFixture(L"media-safe.uink");
		UINK_CHECK(state, media.document && HasMedia(*media.document));
		UINK_CHECK(state, HasDiagnostic(media, UInkDiagnosticCode::MediaResourceUnavailable));
		if (media.document)
		{
			UINK_CHECK(state, media.document->canvases[0].content.size() == 2);
			const UInkMedia* first = std::get_if<UInkMedia>(&media.document->canvases[0].content[0]);
			UINK_CHECK(state, first && first->path == "images/pixel.png" && first->pathIsSafe);
		}

		const UInkReadResult mixed = readFixture(L"mixed-latest.uink");
		UINK_CHECK(state, mixed.document && mixed.document->canvases[0].content.size() == 4);
		if (mixed.document)
		{
			const std::vector<bool> visible = ComputeUInkLatestVisibility(
				mixed.document->canvases[0]);
			UINK_CHECK(state, visible.size() == 4);
			UINK_CHECK(state, !visible[0] && visible[1] && !visible[2] && visible[3]);
		}

		const auto sums = ReadBytes(directory + L"\\SHA256SUMS.txt");
		const auto* text = reinterpret_cast<const char*>(sums.data());
		const std::string_view sumsText = sums.empty() ? std::string_view{} : std::string_view(text, sums.size());
		UINK_CHECK(state, sumsText.find(
			"90ff3db09c512a0a0538ed8072fee45289c92db43f4bec2be65aa31375a40f82  media-safe.uink.extra") !=
			std::string_view::npos);
		UINK_CHECK(state, GetFileAttributesW((directory + L"\\media-safe.uink.extra").c_str()) ==
			INVALID_FILE_ATTRIBUTES);
	}

	const msgpack::object* FindWireField(const msgpack::object& map, std::string_view key)
	{
		for (uint32_t i = 0; i < map.via.map.size; ++i)
		{
			const auto& field = map.via.map.ptr[i];
			if (field.key.type == msgpack::type::STR &&
				std::string_view(field.key.via.str.ptr, field.key.via.str.size) == key)
				return &field.val;
		}
		return nullptr;
	}

	void CheckNoWireExtra(TestState& state, std::span<const std::byte> bytes,
		size_t expectedObjects, bool headerPrefix = false)
	{
		// 解码器会丢掉 extra，直接检查编码字节才能捕获写出回归。
		size_t offset = 0;
		try
		{
			for (size_t index = 0; index < expectedObjects; ++index)
			{
				const auto handle = msgpack::unpack(reinterpret_cast<const char*>(bytes.data()),
					bytes.size(), offset);
				const auto& root = handle.get();
				if (headerPrefix && index == 0)
				{
					UINK_CHECK(state, root.type == msgpack::type::ARRAY && root.via.array.size == 7);
					continue;
				}
				UINK_CHECK(state, root.type == msgpack::type::MAP);
				if (root.type != msgpack::type::MAP) return;
				UINK_CHECK(state, !FindWireField(root, "extra"));
				if (!headerPrefix) continue;
				for (const auto key : { "devices", "workspaces" })
				{
					const auto* entries = FindWireField(root, key);
					UINK_CHECK(state, entries && entries->type == msgpack::type::ARRAY &&
						entries->via.array.size > 0);
					if (!entries || entries->type != msgpack::type::ARRAY || !entries->via.array.size)
						return;
					for (uint32_t i = 0; i < entries->via.array.size; ++i)
					{
						const auto& entry = entries->via.array.ptr[i];
						UINK_CHECK(state, entry.type == msgpack::type::MAP);
						if (entry.type != msgpack::type::MAP) return;
						UINK_CHECK(state, !FindWireField(entry, "extra"));
					}
				}
			}
		}
		catch (...)
		{
			UINK_CHECK(state, false && "unpack produced UInk bytes");
			return;
		}
		UINK_CHECK(state, offset == bytes.size());
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
			UINK_CHECK(state, byte(4) == 0xcd && byte(5) == 0x00 && byte(6) == 0x0b);
			UINK_CHECK(state, byte(7) == 0xd9 && byte(8) == 36);
			UINK_CHECK(state, (byte(69) & 0xf0) == 0x80);
		}
		UINK_CHECK(state, ObjectSizes(basic.bytes).size() == 2);

		const auto rich = MakeRichDocument();
		const auto prefix = EncodeUInkHeaderPrefix(rich.header, rich.headerExtension);
		UINK_CHECK(state, prefix.status == UInkEncodeStatus::Success && !prefix.bytes.empty());
		if (prefix.status == UInkEncodeStatus::Success && !prefix.bytes.empty())
			CheckNoWireExtra(state, prefix.bytes, 2, true);
		const UInkEncodeResult encoded = EncodeUInkDocument(rich);
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

	// 用现行页编码器生成标准内容块，负例再在测试本地改写线格式。
	std::vector<std::byte> EncodeTestContent(TestState& state, UInkContent content)
	{
		UInkCanvas page = MakeBasicDocument(false).canvases.front();
		page.content.push_back(std::move(content));
		auto encoded = EncodeUInkPage(page, true, true);
		const auto sizes = ObjectSizes(encoded.bytes);
		UINK_CHECK(state, encoded.status == UInkEncodeStatus::Success && sizes.size() == 2);
		if (sizes.size() != 2) return {};
		return { encoded.bytes.begin() + sizes.front(), encoded.bytes.end() };
	}

	// 测试夹具的顶层 Map 采用 fixmap；只增计数并附加一个线格式字段。
	void AppendTestMapField(TestState& state, std::vector<std::byte>& block,
		const msgpack::sbuffer& field)
	{
		UINK_CHECK(state, !block.empty() && (block.front() & std::byte{ 0xf0 }) == std::byte{ 0x80 } &&
			block.front() < std::byte{ 0x8f });
		if (block.empty() || block.front() < std::byte{ 0x80 } || block.front() >= std::byte{ 0x8f }) return;
		block.front() = static_cast<std::byte>(std::to_integer<uint8_t>(block.front()) + 1);
		AppendBytes(block, ToBytes(field));
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

	msgpack::sbuffer PackExtraBinary(uint32_t nestedArrays, uint32_t arrayEntries,
		uint32_t binaryBytes)
	{
		msgpack::sbuffer buffer;
		msgpack::packer<msgpack::sbuffer> packer(buffer);
		PackKey(packer, "extra");
		packer.pack_map(1);
		PackKey(packer, "vendor");
		for (uint32_t index = 0; index < nestedArrays; ++index) packer.pack_array(1);
		packer.pack_array(arrayEntries + 1);
		for (uint32_t index = 0; index < arrayEntries; ++index) packer.pack_uint32(index);
		const std::string payload(binaryBytes, 'Z');
		packer.pack_bin(binaryBytes);
		packer.pack_bin_body(payload.data(), binaryBytes);
		return buffer;
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

		const auto validInkBytes = EncodeTestContent(state, MakeInk(0, 0));
		std::vector<std::byte> invalidMiddle = duplicateTail;
		AppendBytes(invalidMiddle, validInkBytes);
		const UInkReadResult middleResult = DecodeUInk(invalidMiddle);
		UINK_CHECK(state, middleResult.status == UInkReadStatus::Complete);
		UINK_CHECK(state, middleResult.document &&
			middleResult.document->canvases[0].content.size() == 1);
		UINK_CHECK(state, middleResult.provenance.invalidBlockBeforeValidBlock);
		UINK_CHECK(state, middleResult.provenance.requiresSaveAs);
		UINK_CHECK(state, !middleResult.safeAppendOffset.has_value());

		std::vector<std::byte> invalidCanvasScope = prefix;
		AppendBytes(invalidCanvasScope, PackInvalidCanvas());
		AppendBytes(invalidCanvasScope, validInkBytes);
		const UInkReadResult invalidCanvasScopeResult = DecodeUInk(invalidCanvasScope);
		UINK_CHECK(state, invalidCanvasScopeResult.status == UInkReadStatus::RecoveredInvalidTail);
		UINK_CHECK(state, invalidCanvasScopeResult.document &&
			invalidCanvasScopeResult.document->canvases[0].content.empty());
		UINK_CHECK(state, invalidCanvasScopeResult.safeAppendOffset &&
			*invalidCanvasScopeResult.safeAppendOffset == prefix.size());

		auto invalidStrokeBytes = EncodeTestContent(state, MakeShape(0, 2));
		UINK_CHECK(state, ReplaceFirstAscii(invalidStrokeBytes, "opacity", "opacXXy"));
		std::vector<std::byte> invalidStroke = prefix;
		AppendBytes(invalidStroke, invalidStrokeBytes);
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
		AppendBytes(corrupt, validInkBytes);
		const UInkReadResult corruptResult = DecodeUInk(corrupt);
		UINK_CHECK(state, corruptResult.status == UInkReadStatus::StoppedAtCorruptTail);
		UINK_CHECK(state, corruptResult.failedObjectOffset &&
			*corruptResult.failedObjectOffset == prefix.size());
		UINK_CHECK(state, corruptResult.safeAppendOffset &&
			*corruptResult.safeAppendOffset == prefix.size());

		std::vector<std::byte> truncated = prefix;
		AppendBytes(truncated, std::span<const std::byte>(validInkBytes).first(
			validInkBytes.size() / 2));
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
		AppendBytes(invalidOrder, validInkBytes);
		const auto canvasBytes = EncodeUInkPage(MakeBasicDocument(false).canvases.front(), true, true);
		UINK_CHECK(state, canvasBytes.status == UInkEncodeStatus::Success);
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
			!invalidViewportResult.document->canvases.empty() &&
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
		badHeader[6] = std::byte{ 0x14 };
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
		const auto mismatchedPptBytes = EncodeUInkPage(mismatchedPpt, false, false);
		UINK_CHECK(state, mismatchedPptBytes.status == UInkEncodeStatus::Success);
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
		UINK_CHECK(state, result.summary.outcome == UInkReadOutcome::Failed && !result.document);
		UINK_CHECK(state, result.provenance.requiresSaveAs && !result.safeAppendOffset);
		UINK_CHECK(state, HasDiagnostic(result, UInkDiagnosticCode::LimitExceeded));
	}

	const UInkDiagnosticCount& DiagnosticCount(const UInkReadResult& result, UInkDiagnosticCode code)
	{
		return result.summary.diagnosticCounts[static_cast<size_t>(code)];
	}

	const UInkCanvas* ExpectV11Canvas(TestState& state, const UInkReadResult& result,
		size_t canvasCount, size_t contentCount, UInkReadOutcome outcome, size_t canvasIndex = 0)
	{
		UINK_CHECK(state, result.summary.outcome == outcome);
		UINK_CHECK(state, result.document && result.document->canvases.size() == canvasCount);
		if (!result.document || result.document->canvases.size() != canvasCount) return nullptr;
		const auto& canvas = result.document->canvases[canvasIndex];
		UINK_CHECK(state, canvas.content.size() == contentCount);
		return canvas.content.size() == contentCount ? &canvas : nullptr;
	}

	void CheckLimitPrefix(TestState& state, const UInkReadResult& result,
		size_t canvasCount, size_t contentCount)
	{
		UINK_CHECK(state, result.status == UInkReadStatus::LimitExceeded);
		UINK_CHECK(state, result.provenance.requiresSaveAs && !result.safeAppendOffset);
		UINK_CHECK(state, HasDiagnostic(result, UInkDiagnosticCode::LimitExceeded));
		ExpectV11Canvas(state, result, canvasCount, contentCount, UInkReadOutcome::Partial);
	}

	bool HasWireBytes(std::span<const std::byte> bytes, std::string_view expected)
	{
		return std::search(bytes.begin(), bytes.end(), expected.begin(), expected.end(),
			[](std::byte byte, char value) { return byte == static_cast<std::byte>(value); })
			!= bytes.end();
	}

	UInkDocument MakeV11Document()
	{
		UInkDocument document = MakeBasicDocument(false);
		document.header.time = 0x0102030405060708ull;
		UInkCanvas& canvas = document.canvases[0];
		canvas.background = UInkBackground{ UInkColor{ 0x112233, std::nullopt }, 2 };
		canvas.pageState = 1;
		canvas.pageKind = 1;
		canvas.binding = 0; // 显式缺省值也必须编码。
		canvas.viewport = UInkViewport{ -2.5f, 1.25f, 0.5f };

		UInkInk highlighter = MakeInk(0, 0, 2);
		highlighter.nib = UInkNib{ 1, 0.125f, 0 };
		canvas.content.emplace_back(std::move(highlighter));
		UInkInk eraser = MakeInk(1, 1, 0);
		eraser.nib = UInkNib{ 1, 0.6666667f, 1 };
		canvas.content.emplace_back(std::move(eraser));
		UInkInk pen = MakeInk(2, 2);
		pen.cap = 2;
		canvas.content.emplace_back(std::move(pen));

		const UInkMessagePackValue::Map point = {
			{ Value(std::string("x")), Value(1.25f) },
			{ Value(std::string("y")), Value(-2.5f) }
		};
		UInkShapeObject object;
		object.id = Guid("60000000-0000-4000-8000-00000000b001");
		object.revision = 0x1122334455667788ull;
		object.preset = "arrow";
		object.partCount = 2;
		object.transform = std::array<float, 6>{ 1, 0, 0, 1, 1.25f, -2.5f };
		object.construction = UInkMessagePackValue::Map{ { Value(std::string("start")), Value(point) } };
		object.style = UInkMessagePackValue::Map{
			{ Value(std::string("point")), Value(point) },
			{ Value(std::string("values")),
				Value(UInkMessagePackValue::Array{ Value(true), Value(std::string("tag")), Value(1.25) }) }
		};
		object.aux = 0;
		UInkShape first = MakeShape(3, 0);
		first.stroke->lineCap = 0;
		first.stroke->lineJoin = 2;
		UInkShape part = MakeShape(4, 0);
		part.undoId = first.undoId;
		part.object = UInkShapeObjectRef{ object.id, 1 };
		first.object = std::move(object);
		canvas.content.emplace_back(std::move(first));
		canvas.content.emplace_back(std::move(part));
		return document;
	}

	void TestV11WireAndRoundTrip(TestState& state)
	{
		using namespace std::string_view_literals;
		const UInkDocument source = MakeV11Document();
		const UInkEncodeResult encoded = EncodeUInkDocument(source);
		UINK_CHECK(state, encoded.status == UInkEncodeStatus::Success);
		if (encoded.status != UInkEncodeStatus::Success) return;
		const auto sizes = ObjectSizes(encoded.bytes);
		UINK_CHECK(state, sizes.size() == 7);
		if (sizes.size() != 7) return;

		std::array<std::span<const std::byte>, 7> blocks;
		size_t offset = 0;
		for (size_t index = 0; index < blocks.size(); ++index)
		{
			blocks[index] = std::span<const std::byte>(encoded.bytes).subspan(offset, sizes[index]);
			offset += sizes[index];
		}
		const auto header = "\x97\xcd\x00\x00\xcd\x00\x0b\xd9\x24"
			"10000000-0000-4000-8000-00000000b001"
			"\xce\x00\x00\x00\x01\xce\x00\x00\x00\x01\xce\x00\x00\x00\x01"
			"\xcf\x01\x02\x03\x04\x05\x06\x07\x08"sv;
		UINK_CHECK(state, blocks[0].size() == header.size() && HasWireBytes(blocks[0], header));
		UINK_CHECK(state, HasWireBytes(blocks[1], "\x8b\xa4type\xcd\x00\x02"sv));
		UINK_CHECK(state, HasWireBytes(blocks[1], "\xa8viewport\x83"
			"\xa1x\xca\xc0\x20\x00\x00\xa1y\xca\x3f\xa0\x00\x00"
			"\xa5scale\xca\x3f\x00\x00\x00"sv));
		UINK_CHECK(state, HasWireBytes(blocks[1], "\xaa" "background\x82"
			"\xa5" "color\x81\xa8" "fallback\xce\x00\x11\x22\x33\xa7pattern\xd2\x00\x00\x00\x02"
			"\xa9pageState\xd2\x00\x00\x00\x01\xa8pageKind\xd2\x00\x00\x00\x01"
			"\xa7" "binding\xd2\x00\x00\x00\x00"sv));
		UINK_CHECK(state, HasWireBytes(blocks[2], "\x89\xa4type\xcd\x00\x03"
			"\xa9" "contentId\xce\x00\x00\x00\x00\xa6undoId\xce\x00\x00\x00\x00"sv));
		UINK_CHECK(state, HasWireBytes(blocks[2], "\xa3nib\x82\xa5shape\xd2\x00\x00\x00\x01"
			"\xa6" "aspect\xca\x3e\x00\x00\x00"sv));
		UINK_CHECK(state, !HasWireBytes(blocks[2], "\xaborientation"sv));
		UINK_CHECK(state, HasWireBytes(blocks[3], "\xa3nib\x83\xa5shape\xd2\x00\x00\x00\x01"
			"\xa6" "aspect\xca\x3f\x2a\xaa\xab\xaborientation\xd2\x00\x00\x00\x01"sv));
		UINK_CHECK(state, HasWireBytes(blocks[4], "\xa3" "cap\xd2\x00\x00\x00\x02"sv));
		UINK_CHECK(state, HasWireBytes(blocks[5], "\x87\xa4type\xcd\x00\x05"
			"\xa9" "contentId\xce\x00\x00\x00\x03\xa6undoId\xce\x00\x00\x00\x03"sv));
		UINK_CHECK(state, HasWireBytes(blocks[5],
			"\xa7lineCap\xd2\x00\x00\x00\x00\xa8lineJoin\xd2\x00\x00\x00\x02"sv));
		UINK_CHECK(state, HasWireBytes(blocks[5],
			"\xa6object\x88\xa2id\xd9\x24" "60000000-0000-4000-8000-00000000b001"
			"\xa8revision\xcf\x11\x22\x33\x44\x55\x66\x77\x88"
			"\xa9partCount\xce\x00\x00\x00\x02\xa6preset\xa5" "arrow"
			"\xa9transform\x96\xca\x3f\x80\x00\x00\xca\x00\x00\x00\x00"
			"\xca\x00\x00\x00\x00\xca\x3f\x80\x00\x00\xca\x3f\xa0\x00\x00\xca\xc0\x20\x00\x00"
			"\xac" "construction\x81\xa5start\x82\xa1x\xca\x3f\xa0\x00\x00\xa1y\xca\xc0\x20\x00\x00"
			"\xa5style\x82\xa5point\x82\xa1x\xca\x3f\xa0\x00\x00\xa1y\xca\xc0\x20\x00\x00"
			"\xa6values\x93\xc3\xa3tag\xcb\x3f\xf4\x00\x00\x00\x00\x00\x00"
			"\xa3" "aux\xce\x00\x00\x00\x00"sv));
		UINK_CHECK(state, HasWireBytes(blocks[6], "\xa6object\x82\xa3ref\xd9\x24"
			"60000000-0000-4000-8000-00000000b001\xa9partIndex\xce\x00\x00\x00\x01"sv));

		const auto read = DecodeUInk(encoded.bytes);
		const UInkCanvas* canvas = ExpectV11Canvas(state, read, 1, 5, UInkReadOutcome::Complete);
		if (!canvas) return;
		UINK_CHECK(state, read.summary.version == 11 && read.diagnostics.empty());
		UINK_CHECK(state, canvas->background && canvas->background->color.fallbackRgb == 0x112233 &&
			canvas->background->pattern == 2 && EffectiveUInkBackgroundPattern(*canvas->background) == 2);
		UINK_CHECK(state, canvas->pageState == 1 && canvas->pageKind == 1 && canvas->binding == 0);
		UINK_CHECK(state, EffectiveUInkPageState(*canvas) == 1 && EffectiveUInkPageKind(*canvas) == 1 &&
			EffectiveUInkBinding(*canvas) == 0);
		UINK_CHECK(state, canvas->viewport && canvas->viewport->x == -2.5f &&
			canvas->viewport->y == 1.25f && canvas->viewport->scale == 0.5f);
		const auto* highlighter = std::get_if<UInkInk>(&canvas->content[0]);
		const auto* eraser = std::get_if<UInkInk>(&canvas->content[1]);
		const auto* pen = std::get_if<UInkInk>(&canvas->content[2]);
		UINK_CHECK(state, highlighter && highlighter->nib && highlighter->nib->shape == 1 &&
			highlighter->nib->aspect == 0.125f && highlighter->nib->orientation == 0 && !highlighter->cap);
		UINK_CHECK(state, eraser && eraser->nib && eraser->nib->shape == 1 &&
			eraser->nib->aspect == 0.6666667f && eraser->nib->orientation == 1 && !eraser->cap);
		UINK_CHECK(state, pen && !pen->nib && pen->cap == 2 && EffectiveUInkCap(*pen) == 2);
		const auto* first = std::get_if<UInkShape>(&canvas->content[3]);
		const auto* part = std::get_if<UInkShape>(&canvas->content[4]);
		UINK_CHECK(state, first && first->stroke && first->stroke->lineCap == 0 &&
			first->stroke->lineJoin == 2 && EffectiveUInkLineJoin(*first->stroke) == 2 && first->object);
		UINK_CHECK(state, part && part->object && part->undoId == 3);
		const auto* object = first && first->object ? std::get_if<UInkShapeObject>(&*first->object) : nullptr;
		const auto* ref = part && part->object ? std::get_if<UInkShapeObjectRef>(&*part->object) : nullptr;
		UINK_CHECK(state, object && object->preset == "arrow" && object->partCount == 2 &&
			object->revision == 0x1122334455667788ull && object->transform &&
			object->construction && object->style && object->aux == 0);
		UINK_CHECK(state, object && ref && ref->ref == object->id && ref->partIndex == 1);
		const auto reencoded = EncodeUInkDocument(*read.document);
		UINK_CHECK(state, reencoded.status == UInkEncodeStatus::Success);
		// 比较整份字节，包含两个通用 Map 的类型、值和条目顺序。
		UINK_CHECK(state, reencoded.bytes == encoded.bytes);
	}

	// 同宽度替换一个字段值，避免破坏夹具的 MessagePack 对象边界。
	bool ReplaceV11Value(std::vector<std::byte>& bytes, std::string_view key, std::string_view value)
	{
		const auto field = std::search(bytes.begin(), bytes.end(), key.begin(), key.end(),
			[](std::byte byte, char text) { return byte == static_cast<std::byte>(text); });
		if (field == bytes.end() || static_cast<size_t>(bytes.end() - field) < key.size() + value.size())
			return false;
		std::transform(value.begin(), value.end(), field + key.size(),
			[](char byte) { return static_cast<std::byte>(byte); });
		return true;
	}

	void TestV11FieldFallbacks(TestState& state)
	{
		using namespace std::string_view_literals;
		UInkDocument source = MakeV11Document();
		auto& sourceCanvas = source.canvases[0];
		sourceCanvas.background->pattern = 128;
		sourceCanvas.pageState = 128;
		sourceCanvas.pageKind = 128;
		sourceCanvas.binding = 128;
		auto& sourceInk = std::get<UInkInk>(sourceCanvas.content[0]);
		sourceInk.nib->shape = 128;
		sourceInk.nib->orientation = 128;
		sourceInk.cap = 128;
		auto& sourceStroke = *std::get<UInkShape>(sourceCanvas.content[3]).stroke;
		sourceStroke.lineCap = 128;
		sourceStroke.lineJoin = 128;
		const auto unknown = EncodeUInkDocument(source);
		UINK_CHECK(state, unknown.status == UInkEncodeStatus::Success);
		if (unknown.status != UInkEncodeStatus::Success) return;
		UInkReadLimits limits;
		limits.maxDiagnostics = 2;
		const auto read = DecodeUInk(unknown.bytes, limits);
		const UInkCanvas* canvas = ExpectV11Canvas(state, read, 1, 5, UInkReadOutcome::Partial);
		if (!canvas) return;
		UINK_CHECK(state, canvas->background && canvas->background->pattern == 128 &&
			EffectiveUInkBackgroundPattern(*canvas->background) == 0);
		UINK_CHECK(state, canvas->pageState == 128 && canvas->pageKind == 128 && canvas->binding == 128);
		UINK_CHECK(state, EffectiveUInkPageState(*canvas) == 0 && EffectiveUInkPageKind(*canvas) == 0 &&
			EffectiveUInkBinding(*canvas) == 0);
		const auto* ink = std::get_if<UInkInk>(&canvas->content[0]);
		const auto* shape = std::get_if<UInkShape>(&canvas->content[3]);
		UINK_CHECK(state, ink && ink->nib && ink->nib->shape == 128 && ink->nib->orientation == 128 &&
			ink->cap == 128 && EffectiveUInkNib(ink->nib).shape == 0 && EffectiveUInkCap(*ink) == 0);
		UINK_CHECK(state, shape && shape->stroke && shape->stroke->lineCap == 128 &&
			shape->stroke->lineJoin == 128 && EffectiveUInkLineCap(*shape->stroke) == 0 &&
			EffectiveUInkLineJoin(*shape->stroke) == 0);
		const auto& count = DiagnosticCount(read, UInkDiagnosticCode::FieldFallback);
		UINK_CHECK(state, count.code == UInkDiagnosticCode::FieldFallback && count.count == 9 &&
			count.firstByteOffset == 69 && count.firstCanvasOrdinal == 1);
		UINK_CHECK(state, read.diagnostics.size() == 2 &&
			read.diagnostics.back().code == UInkDiagnosticCode::DiagnosticsTruncated &&
			read.diagnostics.front().canvasOrdinal == 1);
		limits.maxDiagnostics = 0;
		const auto noDetails = DecodeUInk(unknown.bytes, limits);
		UINK_CHECK(state, noDetails.document && noDetails.document->canvases.size() == 1 &&
			noDetails.summary.outcome == UInkReadOutcome::Partial && noDetails.diagnostics.empty());
		UINK_CHECK(state, DiagnosticCount(noDetails, UInkDiagnosticCode::FieldFallback).count == 9);

		const auto valid = EncodeUInkDocument(MakeV11Document());
		UINK_CHECK(state, valid.status == UInkEncodeStatus::Success);
		if (valid.status != UInkEncodeStatus::Success) return;
		for (const auto aspect : { ""sv, "\xca\xbf\x00\x00\x00"sv, "\xca\x7f\x80\x00\x00"sv })
		{
			auto bytes = valid.bytes;
			const bool modified = aspect.empty() ? ReplaceFirstAscii(bytes, "aspect", "unused") :
				ReplaceV11Value(bytes, "aspect", aspect);
			UINK_CHECK(state, modified);
			const auto bad = DecodeUInk(bytes);
			const UInkCanvas* page = ExpectV11Canvas(state, bad, 1, 5, UInkReadOutcome::Partial);
			if (!page) continue;
			const auto* stroke = std::get_if<UInkInk>(&page->content.front());
			UINK_CHECK(state, stroke && stroke->nib && stroke->nib->shape == 1 &&
				HasInvalidUInkNibAspect(*stroke->nib) && EffectiveUInkNib(stroke->nib).shape == 0);
			UINK_CHECK(state, DiagnosticCount(bad, UInkDiagnosticCode::FieldFallback).count == 1);
		}

		// CANVAS-22：分别缺字段、x 非有限、scale 非正，保留 Canvas 和后续内容。
		for (int fault = 0; fault < 3; ++fault)
		{
			auto bytes = valid.bytes;
			bool modified = false;
			if (fault == 0) modified = ReplaceFirstAscii(bytes, "scale", "spare");
			else if (fault == 1) modified = ReplaceV11Value(bytes, "\xa1x"sv, "\xca\x7f\xc0\x00\x00"sv);
			else modified = ReplaceV11Value(bytes, "scale", "\xca\x00\x00\x00\x00"sv);
			UINK_CHECK(state, modified);
			const auto bad = DecodeUInk(bytes);
			const UInkCanvas* page = ExpectV11Canvas(state, bad, 1, 5, UInkReadOutcome::Partial);
			if (!page) continue;
			UINK_CHECK(state, page->viewport && page->viewport->x == 0 && page->viewport->y == 0 &&
				page->viewport->scale == 1);
			UINK_CHECK(state, DiagnosticCount(bad, UInkDiagnosticCode::FieldFallback).count == 1);
		}

		for (bool wrongType : { false, true })
		{
			auto bytes = valid.bytes;
			const bool modified = wrongType ?
				ReplaceV11Value(bytes, "pattern", "\xca\x3f\x00\x00\x00"sv) :
				ReplaceFirstAscii(bytes, "color", "other");
			UINK_CHECK(state, modified);
			const auto bad = DecodeUInk(bytes);
			const UInkCanvas* page = ExpectV11Canvas(state, bad, 1, 5, UInkReadOutcome::Partial);
			if (!page) continue;
			UINK_CHECK(state, !page->background && page->pageState == 1);
			UINK_CHECK(state, DiagnosticCount(bad, UInkDiagnosticCode::FieldFallback).count == 1);
		}

		auto topLayer = CanvasPrefix();
		const size_t secondCanvas = topLayer.size();
		const auto sizes = ObjectSizes(valid.bytes);
		UINK_CHECK(state, sizes.size() == 7);
		if (sizes.size() != 7) return;
		// 只改第二个 Canvas；二者仍共享页身份。
		std::vector<std::byte> layerBytes(valid.bytes.begin() + sizes[0],
			valid.bytes.begin() + sizes[0] + sizes[1]);
		UINK_CHECK(state, ReplaceV11Value(layerBytes, "layerIndex", "\xce\x00\x00\x00\x01"sv));
		AppendBytes(topLayer, layerBytes);
		const auto layered = DecodeUInk(topLayer);
		const UInkCanvas* layer = ExpectV11Canvas(state, layered, 2, 0, UInkReadOutcome::Partial, 1);
		if (!layer) return;
		UINK_CHECK(state, layer->layerIndex == 1 && !layer->viewport && !layer->background &&
			!layer->pageState && !layer->pageKind && !layer->binding);
		const auto& layerCount = DiagnosticCount(layered, UInkDiagnosticCode::FieldFallback);
		UINK_CHECK(state, layerCount.count == 5 && layerCount.firstCanvasOrdinal == 2 &&
			layerCount.firstByteOffset == secondCanvas);
	}

	void TestV11ShapeObjectRecovery(TestState& state)
	{
		using namespace std::string_view_literals;
		const auto valid = EncodeUInkDocument(MakeV11Document());
		UINK_CHECK(state, valid.status == UInkEncodeStatus::Success);
		if (valid.status != UInkEncodeStatus::Success) return;
		const std::pair<std::string_view, std::string_view> faults[] = {
			{ "\xa2id"sv, "\xd9\x24" "00000000-0000-0000-0000-000000000000"sv },
			{ "partCount"sv, "\xce\x00\x00\x00\x00"sv },
			{ "revision"sv, "\xcf\x00\x00\x00\x00\x00\x00\x00\x00"sv },
			{ "partIndex"sv, "\xce\x00\x00\x00\x00"sv },
			// float32 值不是整数，不能转换为 partCount（TYPE-5）。
			{ "partCount"sv, "\xca\x3f\x00\x00\x00"sv },
		};
		for (const auto& [key, payload] : faults)
		{
			auto bytes = valid.bytes;
			UINK_CHECK(state, ReplaceV11Value(bytes, key, payload));
			const auto read = DecodeUInk(bytes);
			const UInkCanvas* canvas = ExpectV11Canvas(state, read, 1, 5, UInkReadOutcome::Partial);
			if (!canvas) continue;
			const size_t badPart = key == "partIndex" ? 4 : 3;
			const auto* part = std::get_if<UInkShape>(&canvas->content[badPart]);
			UINK_CHECK(state, part && part->stroke && !part->object);
			const auto* sibling = std::get_if<UInkShape>(&canvas->content[badPart == 3 ? 4 : 3]);
			UINK_CHECK(state, sibling && sibling->object);
			UINK_CHECK(state, DiagnosticCount(read, UInkDiagnosticCode::FieldFallback).count == 1);
			UINK_CHECK(state, DiagnosticCount(read, UInkDiagnosticCode::KnownBlockSkipped).count == 0);
		}

		auto block = EncodeTestContent(state, MakeShape(0, 0));
		if (block.empty()) return;
		UINK_CHECK(state, block.front() == std::byte{ 0x86 });
		msgpack::sbuffer extra;
		msgpack::packer<msgpack::sbuffer> packer(extra);
		PackKey(packer, "object");
		packer.pack_map(3);
		PackKey(packer, "ref"); PackGuid(packer, "60000000-0000-4000-8000-00000000b001");
		PackKey(packer, "partIndex"); packer.pack_fix_uint32(1);
		PackKey(packer, "partIndex"); packer.pack_fix_uint32(2);
		AppendTestMapField(state, block, extra);
		auto bytes = CanvasPrefix();
		const size_t shapeOffset = bytes.size();
		AppendBytes(bytes, block);
		const auto read = DecodeUInk(bytes);
		const UInkCanvas* canvas = ExpectV11Canvas(state, read, 1, 1, UInkReadOutcome::Partial);
		if (!canvas) return;
		const auto* part = std::get_if<UInkShape>(&canvas->content.front());
		UINK_CHECK(state, part && part->stroke && !part->object);
		const auto& duplicate = DiagnosticCount(read, UInkDiagnosticCode::DuplicateKnownKey);
		UINK_CHECK(state, duplicate.count == 1 && duplicate.firstByteOffset == shapeOffset &&
			duplicate.firstCanvasOrdinal == 1);
		UINK_CHECK(state, DiagnosticCount(read, UInkDiagnosticCode::FieldFallback).count == 1);
		UINK_CHECK(state, DiagnosticCount(read, UInkDiagnosticCode::KnownBlockSkipped).count == 0);
	}

	std::vector<std::byte> PackV11InkDeltas(std::span<const float> input)
	{
		msgpack::sbuffer buffer;
		msgpack::packer<msgpack::sbuffer> packer(buffer);
		packer.pack_map(8);
		PackKey(packer, "type"); packer.pack_fix_uint16(kInkType);
		PackKey(packer, "contentId"); packer.pack_fix_uint32(0);
		PackKey(packer, "undoId"); packer.pack_fix_uint32(0);
		PackKey(packer, "inkType"); packer.pack_fix_int32(1);
		PackKey(packer, "color"); PackColor(packer);
		PackKey(packer, "opacity"); packer.pack_float(1.0f);
		PackKey(packer, "texture"); packer.pack_fix_int32(0);
		PackKey(packer, "points"); packer.pack_array(static_cast<uint32_t>(input.size()));
		for (float x : input)
		{
			packer.pack_map(3);
			PackKey(packer, "x"); packer.pack_float(x);
			PackKey(packer, "y"); packer.pack_float(0.0f);
			PackKey(packer, "width"); packer.pack_float(1.0f);
		}
		return ToBytes(buffer);
	}

	void TestV11GeometryRecovery(TestState& state)
	{
		auto bytes = CanvasPrefix();
		const size_t firstInvalidOffset = bytes.size();
		AppendBytes(bytes, PackV11InkDeltas(std::array{ 10000000.0f, 0.25f }));
		const float maxFloat = std::numeric_limits<float>::max();
		AppendBytes(bytes, PackV11InkDeltas(std::array{ maxFloat, maxFloat }));
		// 从线格式构造坏块，写入器现在也会拒绝这些坐标。
		for (bool circle : { false, true })
		{
			msgpack::sbuffer buffer;
			msgpack::packer<msgpack::sbuffer> packer(buffer);
			packer.pack_map(6);
			PackKey(packer, "type"); packer.pack_fix_uint16(kShapeType);
			PackKey(packer, "contentId"); packer.pack_fix_uint32(0);
			PackKey(packer, "undoId"); packer.pack_fix_uint32(0);
			PackKey(packer, "shapeType"); packer.pack_fix_int32(circle ? 5 : 0);
			PackKey(packer, "geometry");
			if (circle)
			{
				packer.pack_map(3);
				PackKey(packer, "centerX"); packer.pack_float(100.0f);
				PackKey(packer, "centerY"); packer.pack_float(-10000001.0f);
				PackKey(packer, "radius"); packer.pack_float(30.0f);
			}
			else
			{
				packer.pack_map(1);
				PackKey(packer, "points"); packer.pack_array(2);
				packer.pack_map(2);
				PackKey(packer, "x"); packer.pack_float(10000001.0f);
				PackKey(packer, "y"); packer.pack_float(10.0f);
				packer.pack_map(2);
				PackKey(packer, "x"); packer.pack_float(60.0f);
				PackKey(packer, "y"); packer.pack_float(30.0f);
			}
			PackKey(packer, "stroke"); packer.pack_map(3);
			PackKey(packer, "color"); PackColor(packer);
			PackKey(packer, "opacity"); packer.pack_float(1.0f);
			PackKey(packer, "width"); packer.pack_float(1.0f);
			AppendBytes(bytes, ToBytes(buffer));
		}
		// 在纯 float 下这三个 0.25 都被舍掉，最终 x 会少 1。
		AppendBytes(bytes, PackV11InkDeltas(std::array{ 8388608.0f, 0.25f, 0.25f, 0.25f }));
		const auto read = DecodeUInk(bytes);
		const UInkCanvas* canvas = ExpectV11Canvas(state, read, 1, 1, UInkReadOutcome::Partial);
		if (!canvas) return;
		UINK_CHECK(state, read.validPrefixLength == firstInvalidOffset && !read.failedObjectOffset);
		const auto& coordinates = DiagnosticCount(read, UInkDiagnosticCode::CoordinateOutOfRange);
		UINK_CHECK(state, coordinates.count == 4 && coordinates.firstByteOffset == firstInvalidOffset &&
			coordinates.firstCanvasOrdinal == 1);
		UINK_CHECK(state, DiagnosticCount(read, UInkDiagnosticCode::InvalidFieldValue).count == 1);
		UINK_CHECK(state, DiagnosticCount(read, UInkDiagnosticCode::KnownBlockSkipped).count == 4);
		const auto* ink = std::get_if<UInkInk>(&canvas->content.front());
		UINK_CHECK(state, ink && ink->points.size() == 4 && ink->points.back().x == 8388609.0f);
	}

	void TestV11GeometryEncoding(TestState& state)
	{
		UInkCanvas source = MakeBasicDocument(false).canvases.front();
		source.content.emplace_back(MakeShape(0, 0));
		auto encode = [&] { return EncodeUInkPage(source, true, true); };
		auto& shape = std::get<UInkShape>(source.content.front());
		auto& line = std::get<UInkLineGeometry>(shape.geometry);
		line.points.front().x = 10000000.0f;
		UINK_CHECK(state, encode().status == UInkEncodeStatus::Success);
		line.points.front().x = 10000001.0f;
		UINK_CHECK(state, encode().status == UInkEncodeStatus::InvalidModel);
		shape = MakeShape(0, 5);
		std::get<UInkCircleGeometry>(shape.geometry).centerY = -10000001.0f;
		UINK_CHECK(state, encode().status == UInkEncodeStatus::InvalidModel);

		UInkDocument document = MakeBasicDocument();
		auto& points = std::get<UInkInk>(document.canvases[0].content[0]).points;
		points[0].x = 0.0f;
		points[1].x = 10000000.0f;
		const auto boundary = EncodeUInkDocument(document);
		UINK_CHECK(state, boundary.status == UInkEncodeStatus::Success);
		if (boundary.status != UInkEncodeStatus::Success) return;
		const auto decoded = DecodeUInk(boundary.bytes);
		if (!ExpectV11Canvas(state, decoded, 1, 1, UInkReadOutcome::Complete)) return;

		// 两个绝对坐标都合法，但真正写出的 float32 位移反解会超界。
		points[0].x = 0.25f;
		const auto drift = EncodeUInkDocument(document);
		UINK_CHECK(state, drift.status == UInkEncodeStatus::InvalidModel && drift.bytes.empty());
		points[1].x = 10000001.0f;
		UINK_CHECK(state, EncodeUInkDocument(document).status == UInkEncodeStatus::InvalidModel);
	}

	void TestV11PresetIntegerWidths(TestState& state)
	{
		using namespace std::string_view_literals;
		auto document = MakeV11Document();
		auto& object = std::get<UInkShapeObject>(*std::get<UInkShape>(
			document.canvases[0].content[3]).object);
		object.construction = UInkMessagePackValue::Map{
			{ Value(std::string("mode")), Value(uint64_t{ 1 }) },
			{ Value(std::string("constraint")), Value(int64_t{ -1 }) },
			{ Value(std::string("sides")), Value(int64_t{ 6 }) },
			{ Value(std::string("constraintVertex")), Value(uint64_t{ 2 }) }
		};
		object.style = UInkMessagePackValue::Map{
			{ Value(std::string("linePattern")), Value(uint64_t{ 3 }) },
			{ Value(std::string("custom")), Value(uint64_t{ 4 }) }
		};
		const auto encoded = EncodeUInkDocument(document);
		UINK_CHECK(state, encoded.status == UInkEncodeStatus::Success);
		if (encoded.status != UInkEncodeStatus::Success) return;
		UINK_CHECK(state, HasWireBytes(encoded.bytes, "\xa4mode\xd2\x00\x00\x00\x01"sv));
		UINK_CHECK(state, HasWireBytes(encoded.bytes, "\xaa" "constraint\xd2\xff\xff\xff\xff"sv));
		UINK_CHECK(state, HasWireBytes(encoded.bytes, "\xa5sides\xce\x00\x00\x00\x06"sv));
		UINK_CHECK(state, HasWireBytes(encoded.bytes,
			"\xb0" "constraintVertex\xce\x00\x00\x00\x02"sv));
		UINK_CHECK(state, HasWireBytes(encoded.bytes, "\xablinePattern\xd2\x00\x00\x00\x03"sv));
		UINK_CHECK(state, HasWireBytes(encoded.bytes,
			"\xa6" "custom\xcf\x00\x00\x00\x00\x00\x00\x00\x04"sv));
		const auto decoded = DecodeUInk(encoded.bytes);
		if (!ExpectV11Canvas(state, decoded, 1, 5, UInkReadOutcome::Complete)) return;
		UINK_CHECK(state, EncodeUInkDocument(*decoded.document).bytes == encoded.bytes);

		// TYPE-5 允许无损转换。读入 fixint 后，写回恢复 mode 的规定宽度。
		const auto mode = "\xa4mode\xd2\x00\x00\x00\x01"sv;
		auto compact = encoded.bytes;
		const auto field = std::search(compact.begin(), compact.end(), mode.begin(), mode.end(),
			[](std::byte byte, char value) { return byte == static_cast<std::byte>(value); });
		UINK_CHECK(state, field != compact.end());
		if (field == compact.end()) return;
		compact.erase(field + 5, field + 9);
		const auto compatible = DecodeUInk(compact);
		if (!ExpectV11Canvas(state, compatible, 1, 5, UInkReadOutcome::Complete)) return;
		UINK_CHECK(state, EncodeUInkDocument(*compatible.document).bytes == encoded.bytes);

		for (auto* map : { &*object.construction, &*object.style })
		{
			const size_t known = map == &*object.construction ? map->size() : 1;
			for (size_t index = 0; index < known; ++index)
			{
				auto& value = (*map)[index].second;
				const auto original = value;
				const bool unsignedField = map == &*object.construction && index >= 2;
				value = unsignedField ? Value(uint64_t{ uint64_t{ 1 } << 32 }) :
					Value(static_cast<uint64_t>(INT32_MAX) + 1);
				UINK_CHECK(state, EncodeUInkDocument(document).status == UInkEncodeStatus::InvalidModel);
				value = unsignedField ? Value(int64_t{ -1 }) : Value(static_cast<int64_t>(INT32_MIN) - 1);
				UINK_CHECK(state, EncodeUInkDocument(document).status == UInkEncodeStatus::InvalidModel);
				value = original;
			}
		}
	}

	void TestV11ShapeStyleColorWire(TestState& state)
	{
		using namespace std::string_view_literals;
		auto document = MakeV11Document();
		auto& object = std::get<UInkShapeObject>(*std::get<UInkShape>(
			document.canvases[0].content[3]).object);
		object.style = UInkMessagePackValue::Map{
			{ Value(std::string("strokeColor")), Value(UInkMessagePackValue::Map{
				{ Value(std::string("fallback")), Value(uint64_t{ 0x123456 }) }
			}) },
			{ Value(std::string("fillColor")), Value(UInkMessagePackValue::Map{
				{ Value(std::string("fallback")), Value(int64_t{ 7 }) },
				{ Value(std::string("space")), Value(std::string("scrgb")) },
				{ Value(std::string("components")), Value(UInkMessagePackValue::Array{
					Value(1.5f), Value(0.5), Value(uint64_t{ 0 })
				}) }
			}) }
		};

		const auto encoded = EncodeUInkDocument(document);
		UINK_CHECK(state, encoded.status == UInkEncodeStatus::Success);
		UINK_CHECK(state, HasWireBytes(encoded.bytes,
			"\xa5style\x82\xabstrokeColor\x81\xa8" "fallback\xce\x00\x12\x34\x56"
			"\xa9" "fillColor\x83\xa8" "fallback\xce\x00\x00\x00\x07"
			"\xa5space\xa5scrgb\xaa" "components\x93"
			"\xca\x3f\xc0\x00\x00\xca\x3f\x00\x00\x00\xca\x00\x00\x00\x00"sv));
	}

	void TestV11PresentationBinding(TestState& state)
	{
		for (const bool endScreen : { false, true })
		{
			auto document = MakeRichDocument();
			document.canvases[0].content.clear();
			auto& base = document.canvases[2];
			base.slideId.reset();
			if (endScreen) base.pageKind = 1;
			else base.binding = 1;
			UInkCanvas overlay = base;
			overlay.layerIndex = overlay.layerNumber = 1;
			overlay.binding.reset();
			overlay.pageKind.reset();
			overlay.viewport.reset();
			overlay.content.clear();
			document.canvases.push_back(std::move(overlay));

			const auto encoded = EncodeUInkDocument(document);
			UINK_CHECK(state, encoded.status == UInkEncodeStatus::Success);
			if (encoded.status != UInkEncodeStatus::Success) continue;
			const auto decoded = DecodeUInk(encoded.bytes);
			const auto* page = ExpectV11Canvas(state, decoded, 4, 1, UInkReadOutcome::Complete, 2);
			const auto* layer = ExpectV11Canvas(state, decoded, 4, 0, UInkReadOutcome::Complete, 3);
			if (!page || !layer) continue;
			UINK_CHECK(state, !page->slideId && !page->presentationUnbound &&
				!layer->slideId && !layer->presentationUnbound);

			if (endScreen)
			{
				document.canvases[2].pageKind = 0;
				UINK_CHECK(state, EncodeUInkDocument(document).status == UInkEncodeStatus::InvalidModel);
			}
		}
	}

	void TestV11VersionAndStreamRecovery(TestState& state)
	{
		const auto valid = EncodeUInkDocument(MakeV11Document());
		UINK_CHECK(state, valid.status == UInkEncodeStatus::Success && valid.bytes.size() > 69);
		if (valid.status != UInkEncodeStatus::Success || valid.bytes.size() <= 69) return;
		for (uint8_t version : { 10, 19 })
		{
			auto bytes = valid.bytes;
			bytes[6] = static_cast<std::byte>(version);
			const auto read = DecodeUInk(bytes);
			const UInkCanvas* canvas = ExpectV11Canvas(state, read, 1, 5, UInkReadOutcome::Complete);
			if (!canvas) continue;
			const auto* ink = std::get_if<UInkInk>(&canvas->content[0]);
			UINK_CHECK(state, read.summary.version == version && ink && ink->nib &&
				ink->nib->aspect == 0.125f);
		}
		for (uint8_t version : { 9, 20 })
		{
			auto bytes = valid.bytes;
			bytes[6] = static_cast<std::byte>(version);
			const auto read = DecodeUInk(bytes);
			UINK_CHECK(state, read.status == UInkReadStatus::RejectedHeader && !read.document);
			UINK_CHECK(state, read.summary.outcome == UInkReadOutcome::Failed &&
				read.summary.version == version && read.failedObjectOffset == 0 &&
				read.validPrefixLength == 0);
			const auto& unsupported = DiagnosticCount(read, UInkDiagnosticCode::UnsupportedVersion);
			UINK_CHECK(state, unsupported.count == 1 && unsupported.firstCanvasOrdinal == 0);
		}

		const auto sizes = ObjectSizes(valid.bytes);
		UINK_CHECK(state, sizes.size() == 7);
		if (sizes.size() != 7) return;
		const size_t lastBlock = valid.bytes.size() - sizes.back();
		auto truncated = valid.bytes;
		truncated.pop_back();
		const auto tail = DecodeUInk(truncated);
		const UInkCanvas* tailCanvas = ExpectV11Canvas(state, tail, 1, 4, UInkReadOutcome::Partial);
		if (!tailCanvas) return;
		UINK_CHECK(state, tail.status == UInkReadStatus::RecoveredTruncatedTail &&
			tail.validPrefixLength == lastBlock && tail.failedObjectOffset == lastBlock);
		const auto& truncation = DiagnosticCount(tail, UInkDiagnosticCode::TruncatedTail);
		UINK_CHECK(state, truncation.count == 1 && truncation.firstByteOffset == lastBlock &&
			truncation.firstCanvasOrdinal == 1);

		auto malformed = valid.bytes;
		malformed.insert(malformed.begin() + lastBlock, std::byte{ 0xc1 });
		const auto middle = DecodeUInk(malformed);
		const UInkCanvas* prefix = ExpectV11Canvas(state, middle, 1, 4, UInkReadOutcome::Partial);
		if (!prefix) return;
		UINK_CHECK(state, middle.status == UInkReadStatus::StoppedAtCorruptTail &&
			middle.validPrefixLength == lastBlock && middle.failedObjectOffset == lastBlock);
		const auto& stop = DiagnosticCount(middle, UInkDiagnosticCode::MalformedMessagePack);
		UINK_CHECK(state, stop.count == 1 && stop.firstByteOffset == lastBlock &&
			stop.firstCanvasOrdinal == 1);

		auto noCanvas = HeaderPrefix();
		noCanvas.push_back(std::byte{ 0xc1 });
		const auto failed = DecodeUInk(noCanvas);
		UINK_CHECK(state, failed.summary.outcome == UInkReadOutcome::Failed &&
			failed.failedObjectOffset == 69 && failed.validPrefixLength == 69);
	}

	void TestV11CanvasOrdinals(TestState& state)
	{
		auto numeric = CanvasPrefix();
		constexpr size_t kCanvasOffset = 69;
		constexpr size_t kTypeOffset = kCanvasOffset + 6;
		UINK_CHECK(state, numeric.size() > kTypeOffset + 3);
		if (numeric.size() <= kTypeOffset + 3) return;
		UINK_CHECK(state, numeric[kTypeOffset] == std::byte{ 0xcd });
		numeric.erase(numeric.begin() + kTypeOffset, numeric.begin() + kTypeOffset + 3);
		numeric.insert(numeric.begin() + kTypeOffset, { std::byte{ 0xca }, std::byte{ 0x40 },
			std::byte{ 0 }, std::byte{ 0 }, std::byte{ 0 } });
		const auto compatible = DecodeUInk(numeric);
		if (!ExpectV11Canvas(state, compatible, 1, 0, UInkReadOutcome::Complete)) return;
		const auto& compatibility = DiagnosticCount(compatible, UInkDiagnosticCode::NumericCompatibility);
		UINK_CHECK(state, compatibility.count == 1 && compatibility.firstByteOffset == kCanvasOffset &&
			compatibility.firstCanvasOrdinal == 1);
		for (const auto& diagnostic : compatible.diagnostics)
			UINK_CHECK(state, diagnostic.canvasOrdinal == 1);

		auto bytes = CanvasPrefix();
		const size_t invalidCanvasOffset = bytes.size();
		msgpack::sbuffer invalid;
		msgpack::packer<msgpack::sbuffer> packer(invalid);
		packer.pack_map(2);
		PackKey(packer, "type"); packer.pack_fix_uint16(kCanvasType);
		PackKey(packer, "type"); packer.pack_fix_uint16(kCanvasType);
		AppendBytes(bytes, ToBytes(invalid));
		const auto ink = PackV11InkDeltas(std::array{ 0.0f });
		AppendBytes(bytes, ink);
		const size_t thirdCanvasOffset = bytes.size();
		AppendBytes(bytes, PackCanvasWithViewport(1, 2.0f));
		AppendBytes(bytes, ink);
		const auto read = DecodeUInk(bytes);
		if (!ExpectV11Canvas(state, read, 2, 0, UInkReadOutcome::Partial) ||
			!ExpectV11Canvas(state, read, 2, 1, UInkReadOutcome::Partial, 1)) return;
		const auto& duplicate = DiagnosticCount(read, UInkDiagnosticCode::DuplicateKnownKey);
		UINK_CHECK(state, duplicate.count == 1 && duplicate.firstCanvasOrdinal == 2 &&
			duplicate.firstByteOffset == invalidCanvasOffset);
		UINK_CHECK(state, DiagnosticCount(read, UInkDiagnosticCode::KnownBlockSkipped).count == 2);
		const auto& fallback = DiagnosticCount(read, UInkDiagnosticCode::FieldFallback);
		UINK_CHECK(state, fallback.count == 1 && fallback.firstCanvasOrdinal == 3 &&
			fallback.firstByteOffset == thirdCanvasOffset);

		for (const auto& types : { std::array{ 2, 3 }, std::array{ 3, 2 }, std::array{ 2, -1 } })
		{
			auto conflict = CanvasPrefix();
			const size_t conflictOffset = conflict.size();
			msgpack::sbuffer buffer;
			msgpack::packer<msgpack::sbuffer> fields(buffer);
			fields.pack_map(2);
			for (int type : types)
			{
				PackKey(fields, "type");
				if (type == -1) fields.pack(std::string("bad"));
				else fields.pack_fix_uint16(static_cast<uint16_t>(type));
			}
			AppendBytes(conflict, ToBytes(buffer));
			AppendBytes(conflict, ink);
			const auto rejected = DecodeUInk(conflict);
			if (!ExpectV11Canvas(state, rejected, 1, 0, UInkReadOutcome::Partial)) continue;
			const auto& conflictCount = DiagnosticCount(rejected, UInkDiagnosticCode::DuplicateKnownKey);
			UINK_CHECK(state, conflictCount.count == 1 && conflictCount.firstCanvasOrdinal == 2 &&
				conflictCount.firstByteOffset == conflictOffset);
			UINK_CHECK(state, DiagnosticCount(rejected, UInkDiagnosticCode::KnownBlockSkipped).count == 2);
		}
	}

	void TestV11IgnoredLayerMetadata(TestState& state)
	{
		for (uint32_t layerIndex : { 0u, 1u })
		{
			auto block = PackCanvasWithViewport(layerIndex, 1.0f);
			UINK_CHECK(state, !block.empty() && block.front() == std::byte{ 0x87 });
			if (block.empty() || block.front() != std::byte{ 0x87 }) continue;
			block.front() = std::byte{ 0x88 };
			msgpack::sbuffer buffer;
			msgpack::packer<msgpack::sbuffer> packer(buffer);
			PackKey(packer, "background"); packer.pack_map(2);
			PackKey(packer, "color"); PackColor(packer);
			PackKey(packer, "color"); PackColor(packer);
			AppendBytes(block, ToBytes(buffer));

			auto bytes = layerIndex == 0 ? HeaderPrefix() : CanvasPrefix();
			AppendBytes(bytes, block);
			const auto read = DecodeUInk(bytes);
			const auto& duplicate = DiagnosticCount(read, UInkDiagnosticCode::DuplicateKnownKey);
			if (layerIndex == 0)
			{
				UINK_CHECK(state, read.summary.outcome == UInkReadOutcome::Failed &&
					read.document && read.document->canvases.empty());
				UINK_CHECK(state, duplicate.count == 1 && duplicate.firstCanvasOrdinal == 1);
				UINK_CHECK(state, DiagnosticCount(read, UInkDiagnosticCode::KnownBlockSkipped).count == 1);
			}
			else
			{
				const auto* layer = ExpectV11Canvas(state, read, 2, 0, UInkReadOutcome::Partial, 1);
				if (!layer) continue;
				UINK_CHECK(state, layer->layerIndex == 1 && !layer->background && duplicate.count == 0);
			}
		}
	}

	void TestV11DiagnosticLocations(TestState& state)
	{
		auto bytes = CanvasPrefix();
		const size_t gapOffset = bytes.size();
		auto gap = PackCanvasWithViewport(2, 1.0f);
		AppendBytes(bytes, gap);
		const auto topology = DecodeUInk(bytes);
		if (!ExpectV11Canvas(state, topology, 2, 0, UInkReadOutcome::Partial)) return;
		const auto& temporary = DiagnosticCount(topology, UInkDiagnosticCode::TemporaryIdentity);
		UINK_CHECK(state, temporary.count == 1 && temporary.firstCanvasOrdinal == 1 &&
			temporary.firstByteOffset == HeaderPrefix().size());
		UINK_CHECK(state, gapOffset > temporary.firstByteOffset);

		auto mediaBytes = CanvasPrefix();
		const size_t mediaOffset = mediaBytes.size();
		auto mediaBlock = PackMediaWithInvalidPageCount();
		// 去掉无关回退，使 Partial 只能由不安全路径引起。
		const bool prepared = ReplaceFirstAscii(mediaBlock, "documents/file.pdf", "../uments/file.pdf") &&
			ReplaceFirstAscii(mediaBlock, "pageCount", "extension");
		UINK_CHECK(state, prepared);
		if (!prepared) return;
		AppendBytes(mediaBytes, mediaBlock);
		const auto mediaRead = DecodeUInk(mediaBytes);
		const auto* page = ExpectV11Canvas(state, mediaRead, 1, 1, UInkReadOutcome::Partial);
		if (!page) return;
		const auto* media = std::get_if<UInkMedia>(&page->content.front());
		UINK_CHECK(state, media && !media->pathIsSafe);
		const auto& unsafe = DiagnosticCount(mediaRead, UInkDiagnosticCode::UnsafeMediaPath);
		UINK_CHECK(state, unsafe.count == 1 && unsafe.firstByteOffset == mediaOffset &&
			unsafe.firstCanvasOrdinal == 1);
		const auto headerRead = DecodeUInk(HeaderPrefix());
		UINK_CHECK(state, headerRead.document && headerRead.document->canvases.empty());
		const auto& snapshot = DiagnosticCount(headerRead, UInkDiagnosticCode::HeaderSnapshotMismatch);
		UINK_CHECK(state, snapshot.count == 1 && snapshot.firstByteOffset == 0 &&
			snapshot.firstCanvasOrdinal == 0);
	}

	std::vector<std::byte> PackV11OversizedShape()
	{
		msgpack::sbuffer shape;
		msgpack::packer<msgpack::sbuffer> packer(shape);
		packer.pack_map(5);
		PackKey(packer, "type"); packer.pack_fix_uint16(kShapeType);
		PackKey(packer, "contentId"); packer.pack_fix_uint32(0);
		PackKey(packer, "undoId"); packer.pack_fix_uint32(0);
		PackKey(packer, "shapeType"); packer.pack_fix_int32(1);
		PackKey(packer, "geometry"); packer.pack_map(1);
		PackKey(packer, "points"); packer.pack_array(100001);
		for (uint32_t index = 0; index < 100001; ++index) packer.pack_nil();
		return ToBytes(shape);
	}

	void TestV11ResourceBudgets(TestState& state)
	{
		const auto canvasPrefix = CanvasPrefix();
		const auto header = HeaderPrefix();
		UINK_CHECK(state, canvasPrefix.size() > header.size() && header.size() == 69);
		if (canvasPrefix.size() <= header.size() || header.size() != 69) return;
		const std::vector<std::byte> canvasBlock(canvasPrefix.begin() + header.size(), canvasPrefix.end());
		for (std::string_view key : { "devices", "workspaces" })
		{
			for (uint32_t count : { 1024u, 1025u })
			{
				msgpack::sbuffer buffer;
				msgpack::packer<msgpack::sbuffer> packer(buffer);
				packer.pack_map(4);
				PackKey(packer, "type"); packer.pack_fix_uint16(kHeaderExtensionType);
				PackKey(packer, "name"); packer.pack(std::string("registry regression"));
				const bool tooManyDevices = key == "devices";
				PackKey(packer, tooManyDevices ? "devices" : "workspaces");
				packer.pack_array(count);
				for (uint32_t index = 0; index < count; ++index) packer.pack_nil();
				PackKey(packer, tooManyDevices ? "workspaces" : "devices"); packer.pack_array(1);
				if (tooManyDevices)
				{
					packer.pack_map(2);
					PackKey(packer, "guid"); PackGuid(packer, "30000000-0000-4000-8000-00000000e001");
					PackKey(packer, "workspaceType"); packer.pack_fix_int32(1);
				}
				else
				{
					packer.pack_map(6);
					PackKey(packer, "guid"); PackGuid(packer, "40000000-0000-4000-8000-00000000e001");
					PackKey(packer, "deviceType"); packer.pack_fix_int32(0);
					PackKey(packer, "x"); packer.pack_fix_int32(0);
					PackKey(packer, "y"); packer.pack_fix_int32(0);
					PackKey(packer, "width"); packer.pack_fix_uint32(1920);
					PackKey(packer, "height"); packer.pack_fix_uint32(1080);
				}
				auto bytes = header;
				AppendBytes(bytes, ToBytes(buffer));
				AppendBytes(bytes, canvasBlock);
				UInkReadLimits limits;
				limits.maxDiagnostics = 0;
				const auto read = DecodeUInk(bytes, limits);
				const UInkCanvas* page = ExpectV11Canvas(state, read, 1, 0, UInkReadOutcome::Partial);
				if (!page) continue;
				const auto& extension = read.document->headerExtension;
				UINK_CHECK(state, extension && extension->name == "registry regression");
				if (!extension) continue;
				UINK_CHECK(state, extension->devices.size() == (tooManyDevices ? 0 : 1) &&
					extension->workspaces.size() == (tooManyDevices ? 1 : 0));
				UINK_CHECK(state, read.status == (count == 1024 ? UInkReadStatus::Complete :
					UInkReadStatus::LimitExceeded));
				if (count == 1025)
					UINK_CHECK(state, !read.safeAppendOffset && read.provenance.requiresSaveAs);
				const auto& budget = DiagnosticCount(read, UInkDiagnosticCode::LimitExceeded);
				UINK_CHECK(state, budget.count == (count == 1024 ? 0u : 1u));
				UINK_CHECK(state, count == 1024 ||
					(budget.firstCanvasOrdinal == 0 && budget.firstByteOffset == header.size()));
			}
		}

		// 数量符合容器上限，剩余字节仅能放一个元素，不能据声明分配。
		const auto baseline = DecodeUInk(canvasPrefix);
		if (!ExpectV11Canvas(state, baseline, 1, 0, UInkReadOutcome::Complete)) return;
		auto shortArray = canvasPrefix;
		AppendBytes(shortArray, std::array{ std::byte{ 0xdd }, std::byte{ 0 }, std::byte{ 0x0f },
			std::byte{ 0x42 }, std::byte{ 0x40 }, std::byte{ 0xc0 } });
		const auto truncated = DecodeUInk(shortArray);
		const UInkCanvas* prefix = ExpectV11Canvas(state, truncated, 1, 0, UInkReadOutcome::Partial);
		if (!prefix) return;
		UINK_CHECK(state, truncated.status == UInkReadStatus::RecoveredTruncatedTail &&
			truncated.failedObjectOffset == canvasPrefix.size() &&
			truncated.modelCharge == baseline.modelCharge);

		auto wideShape = canvasPrefix;
		AppendBytes(wideShape, PackV11OversizedShape());
		const auto limited = DecodeUInk(wideShape);
		CheckLimitPrefix(state, limited, 1, 0);
		UINK_CHECK(state, DiagnosticCount(limited, UInkDiagnosticCode::LimitExceeded).count == 1 &&
			limited.geometryPointCount == 0 && limited.validPrefixLength == canvasPrefix.size());
	}

	void TestV11BudgetRollback(TestState& state)
	{
		using namespace std::string_view_literals;
		std::array<float, 256> badPoints{};
		badPoints.front() = 10000001.0f;
		const auto discarded = PackV11InkDeltas(badPoints);
		const auto validInk = PackV11InkDeltas(std::array{ 1.0f });
		const auto makeStream = [&](size_t count)
		{
			auto bytes = CanvasPrefix();
			for (size_t index = 0; index < count; ++index) AppendBytes(bytes, discarded);
			AppendBytes(bytes, validInk);
			return bytes;
		};
		const auto baseline = DecodeUInk(makeStream(0));
		if (!ExpectV11Canvas(state, baseline, 1, 1, UInkReadOutcome::Complete)) return;
		const auto oneBad = DecodeUInk(makeStream(1));
		if (!ExpectV11Canvas(state, oneBad, 1, 1, UInkReadOutcome::Partial)) return;
		UINK_CHECK(state, oneBad.modelCharge > baseline.modelCharge);

		UInkReadLimits limits;
		limits.maxModelCharge = oneBad.modelCharge;
		limits.maxGeometryPoints = badPoints.size();
		const auto manyBad = DecodeUInk(makeStream(3), limits);
		if (!ExpectV11Canvas(state, manyBad, 1, 1, UInkReadOutcome::Partial)) return;
		UINK_CHECK(state, manyBad.status == UInkReadStatus::Complete && manyBad.geometryPointCount == 1);
		UINK_CHECK(state, manyBad.modelCharge == oneBad.modelCharge &&
			DiagnosticCount(manyBad, UInkDiagnosticCode::CoordinateOutOfRange).count == 3);

		// 第三个 Shape 的较大临时树把前两个 Shape 残留的对象层费用暴露到峰值。
		auto document = MakeBasicDocument(false);
		for (uint32_t index = 0; index < 3; ++index)
		{
			UInkShape shape = MakeShape(index, 0);
			UInkShapeObject object;
			object.id = Guid("60000000-0000-4000-8000-00000000b001");
			object.revision = 1;
			object.partCount = 1;
			object.preset = std::string(index == 2 ? 8192 : 1024, 'x');
			object.aux = index;
			shape.object = std::move(object);
			document.canvases[0].content.emplace_back(std::move(shape));
		}
		const auto encoded = EncodeUInkDocument(document);
		UINK_CHECK(state, encoded.status == UInkEncodeStatus::Success);
		if (encoded.status != UInkEncodeStatus::Success) return;
		auto reference = encoded.bytes;
		const bool patchedLast = ReplaceFirstAscii(reference, "\xa3" "aux\xce\x00\x00\x00\x02"sv,
			"\xa3" "aux\xa4oops"sv);
		UINK_CHECK(state, patchedLast);
		if (!patchedLast) return;
		auto allBad = reference;
		for (auto good : { "\xa3" "aux\xce\x00\x00\x00\x00"sv, "\xa3" "aux\xce\x00\x00\x00\x01"sv })
			UINK_CHECK(state, ReplaceFirstAscii(allBad, good, "\xa3" "aux\xa4oops"sv));
		// 对照只保留独立 Shape，第三个 Shape 的坏对象层与实际输入相同。
		const auto sizes = ObjectSizes(reference);
		UINK_CHECK(state, sizes.size() == 5);
		if (sizes.size() != 5) return;
		document.canvases[0].content.pop_back();
		for (auto& content : document.canvases[0].content) std::get<UInkShape>(content).object.reset();
		auto isolated = EncodeUInkDocument(document);
		UINK_CHECK(state, isolated.status == UInkEncodeStatus::Success);
		if (isolated.status != UInkEncodeStatus::Success) return;
		AppendBytes(isolated.bytes, std::span(reference).last(sizes.back()));
		const auto baselineObject = DecodeUInk(isolated.bytes);
		if (!ExpectV11Canvas(state, baselineObject, 1, 3, UInkReadOutcome::Partial)) return;
		limits = {};
		limits.maxModelCharge = baselineObject.modelCharge;
		const auto rolledBack = DecodeUInk(allBad, limits);
		const auto* page = ExpectV11Canvas(state, rolledBack, 1, 3, UInkReadOutcome::Partial);
		if (!page) return;
		UINK_CHECK(state, rolledBack.status == UInkReadStatus::Complete &&
			rolledBack.modelCharge == baselineObject.modelCharge);
		for (const auto& content : page->content)
		{
			const auto* shape = std::get_if<UInkShape>(&content);
			UINK_CHECK(state, shape && !shape->object);
		}
	}

	void TestV11RegistryBudgetRollback(TestState& state)
	{
		const std::string name(1024, 'x');
		const auto makeStream = [&](bool chargeAllNames)
		{
			msgpack::sbuffer buffer;
			msgpack::packer<msgpack::sbuffer> packer(buffer);
			packer.pack_map(2);
			PackKey(packer, "type"); packer.pack_fix_uint16(kHeaderExtensionType);
			PackKey(packer, "workspaces"); packer.pack_array(3);
			for (int index = 0; index < 3; ++index)
			{
				packer.pack_map(4);
				PackKey(packer, "guid"); PackGuid(packer, "30000000-0000-4000-8000-00000000e001");
				PackKey(packer, "workspaceType"); packer.pack_fix_int32(1);
				PackKey(packer, chargeAllNames || index == 2 ? "name" : "junk"); packer.pack(name);
				PackKey(packer, "currentPageIndex"); packer.pack_nil();
			}
			const auto prefix = CanvasPrefix();
			auto bytes = HeaderPrefix();
			const size_t headerSize = bytes.size();
			AppendBytes(bytes, ToBytes(buffer));
			AppendBytes(bytes, std::span(prefix).subspan(headerSize));
			return bytes;
		};
		const auto baseline = DecodeUInk(makeStream(false));
		if (!ExpectV11Canvas(state, baseline, 1, 0, UInkReadOutcome::Partial)) return;
		UInkReadLimits limits;
		limits.maxModelCharge = baseline.modelCharge;
		const auto result = DecodeUInk(makeStream(true), limits);
		if (!ExpectV11Canvas(state, result, 1, 0, UInkReadOutcome::Partial)) return;
		const auto& extension = result.document->headerExtension;
		UINK_CHECK(state, result.status == UInkReadStatus::Complete &&
			extension && extension->workspaces.empty() && result.modelCharge == baseline.modelCharge);
		UINK_CHECK(state, DiagnosticCount(result, UInkDiagnosticCode::KnownBlockSkipped).count == 3);
	}

	void TestV11ContentStorageCharge(TestState& state)
	{
		auto document = MakeBasicDocument(false);
		for (uint32_t index = 0; index < 8; ++index)
			document.canvases.front().content.emplace_back(MakeClear(index, index));
		const auto encoded = EncodeUInkDocument(document);
		UINK_CHECK(state, encoded.status == UInkEncodeStatus::Success);
		if (encoded.status != UInkEncodeStatus::Success) return;
		const auto sizes = ObjectSizes(encoded.bytes);
		UINK_CHECK(state, sizes.size() == 10);
		if (sizes.size() != 10) return;
		size_t prefixBytes = 0;
		for (size_t index = 0; index < 6; ++index) prefixBytes += sizes[index];
		const auto four = DecodeUInk(std::span(encoded.bytes).first(prefixBytes));
		const auto eight = DecodeUInk(encoded.bytes);
		if (!ExpectV11Canvas(state, four, 1, 4, UInkReadOutcome::Complete) ||
			!ExpectV11Canvas(state, eight, 1, 8, UInkReadOutcome::Complete)) return;
		UINK_CHECK(state, eight.modelCharge >= four.modelCharge + 4 * sizeof(UInkContent));
	}

	void TestV11ResourceRecovery(TestState& state)
	{
		msgpack::sbuffer buffer;
		msgpack::packer<msgpack::sbuffer> packer(buffer);
		packer.pack_map(2);
		PackKey(packer, "payload"); packer.pack_array(9);
		for (int index = 0; index < 9; ++index) packer.pack_nil();
		// 在超限字段后再写 type，验证恢复扫描不会依赖键顺序。
		PackKey(packer, "type"); packer.pack_fix_uint16(kCanvasType);
		const auto badCanvas = ToBytes(buffer);
		const auto ink = PackV11InkDeltas(std::array{ 1.0f });
		UInkReadLimits limits;
		limits.maxContainerEntries = 8;
		auto bytes = CanvasPrefix();
		const size_t badOffset = bytes.size();
		AppendBytes(bytes, badCanvas);
		AppendBytes(bytes, ink);
		AppendBytes(bytes, PackCanvasWithViewport(1, 1.0f));
		AppendBytes(bytes, ink);
		const auto recovered = DecodeUInk(bytes, limits);
		CheckLimitPrefix(state, recovered, 2, 0);
		ExpectV11Canvas(state, recovered, 2, 1, UInkReadOutcome::Partial, 1);
		const auto& recoveredLimit = DiagnosticCount(recovered, UInkDiagnosticCode::LimitExceeded);
		UINK_CHECK(state, recovered.validPrefixLength == badOffset && !recovered.failedObjectOffset &&
			recoveredLimit.count == 1 && recoveredLimit.firstCanvasOrdinal == 2 &&
			recoveredLimit.firstByteOffset == badOffset);

		// 同一对象保留超限数组头，但去掉正文，使顶层边界无法确认。
		const auto truncatedCanvas = std::span(badCanvas).first(10);
		auto prefix = CanvasPrefix();
		AppendBytes(prefix, truncatedCanvas);
		const auto stopped = DecodeUInk(prefix, limits);
		CheckLimitPrefix(state, stopped, 1, 0);
		UINK_CHECK(state, stopped.failedObjectOffset == badOffset && stopped.validPrefixLength == badOffset);

		auto noPage = HeaderPrefix();
		const auto headerBytes = noPage.size();
		AppendBytes(noPage, badCanvas);
		const auto failed = DecodeUInk(noPage, limits);
		CheckLimitFailure(state, failed);
		UINK_CHECK(state, failed.validPrefixLength == headerBytes);
	}

	void TestV11RegistryCycles(TestState& state)
	{
		const auto tree = EncodeUInkDocument(MakeRichDocument());
		UINK_CHECK(state, tree.status == UInkEncodeStatus::Success);
		if (tree.status != UInkEncodeStatus::Success) return;
		auto cyclicBytes = tree.bytes;
		UINK_CHECK(state, ReplaceGuidAfterKey(cyclicBytes, "parentDeviceGuid",
			"40000000-0000-4000-8000-00000000c002"));
		UINK_CHECK(state, ReplaceGuidAfterKey(cyclicBytes, "parentWorkspaceGuid",
			"30000000-0000-4000-8000-00000000c002"));
		const auto cyclic = DecodeUInk(cyclicBytes);
		if (!ExpectV11Canvas(state, cyclic, 3, 10, UInkReadOutcome::Partial)) return;
		const auto& extension = cyclic.document->headerExtension;
		UINK_CHECK(state, extension && extension->devices.size() == 2 &&
			extension->workspaces.size() == 2);
		if (!extension || extension->devices.size() != 2 || extension->workspaces.size() != 2) return;
		UINK_CHECK(state, extension->devices[0].parentResolved && !extension->devices[1].parentResolved);
		UINK_CHECK(state, extension->workspaces[0].parentResolved &&
			!extension->workspaces[1].parentResolved && !extension->workspaces[1].parentWorkspaceGuid);
		const auto& cycles = DiagnosticCount(cyclic, UInkDiagnosticCode::ParentCycle);
		UINK_CHECK(state, cycles.count == 2 && cycles.firstByteOffset == 69 &&
			cycles.firstCanvasOrdinal == 0);
	}

	void TestV11RegistryTailCycles(TestState& state)
	{
		auto document = MakeCycleDocument();
		auto& devices = document.headerExtension->devices;
		auto& workspaces = document.headerExtension->workspaces;
		const UInkGuid deviceC = Guid("40000000-0000-4000-8000-00000000d103");
		const UInkGuid workspaceC = Guid("30000000-0000-4000-8000-00000000d103");
		const char* deviceRoot = "40000000-0000-4000-8000-00000000d104";
		const char* workspaceRoot = "30000000-0000-4000-8000-00000000d104";
		std::get<UInkWindowDevice>(devices[1].geometry).parentDeviceGuid = deviceC;
		workspaces[1].parentWorkspaceGuid = workspaceC;
		UInkDevice tail = devices[1];
		tail.guid = deviceC;
		std::get<UInkWindowDevice>(tail.geometry).parentDeviceGuid = Guid(deviceRoot);
		devices.push_back(std::move(tail));
		UInkDevice display;
		display.guid = Guid(deviceRoot);
		display.deviceType = 0;
		display.geometry = UInkDisplayDevice{ 0, 0, 1920, 1080 };
		devices.push_back(std::move(display));
		UInkWorkspace nested = workspaces[1];
		nested.guid = workspaceC;
		nested.parentWorkspaceGuid = Guid(workspaceRoot);
		workspaces.push_back(std::move(nested));
		UInkWorkspace workspace;
		workspace.guid = Guid(workspaceRoot);
		workspace.workspaceType = 1;
		workspaces.push_back(std::move(workspace));
		document.header.deviceNum = document.header.workspaceNum = document.header.pageNum = 4;
		const char* pages[] = { "50000000-0000-4000-8000-00000000d102",
			"50000000-0000-4000-8000-00000000d103", "50000000-0000-4000-8000-00000000d104" };
		for (size_t index = 1; index < workspaces.size(); ++index)
		{
			UInkCanvas canvas = document.canvases.front();
			canvas.workspaceGuid = workspaces[index].guid;
			canvas.pageGuid = Guid(pages[index - 1]);
			document.canvases.push_back(std::move(canvas));
		}

		auto encoded = EncodeUInkDocument(document);
		UINK_CHECK(state, encoded.status == UInkEncodeStatus::Success);
		if (encoded.status != UInkEncodeStatus::Success) return;
		const auto baseline = DecodeUInk(encoded.bytes);
		if (!ExpectV11Canvas(state, baseline, 4, 0, UInkReadOutcome::Complete)) return;
		// 先写出的根 GUID 位于 C 的 parent 字段。将 C→根 改为 C→B。
		const bool linked = ReplaceFirstAscii(encoded.bytes, deviceRoot,
			"40000000-0000-4000-8000-00000000d102") &&
			ReplaceFirstAscii(encoded.bytes, workspaceRoot, "30000000-0000-4000-8000-00000000d102");
		UINK_CHECK(state, linked);
		if (!linked) return;
		const auto cyclic = DecodeUInk(encoded.bytes);
		if (!ExpectV11Canvas(state, cyclic, 4, 0, UInkReadOutcome::Partial)) return;
		const auto& extension = cyclic.document->headerExtension;
		UINK_CHECK(state, extension && extension->devices.size() == 4 && extension->workspaces.size() == 4);
		if (!extension || extension->devices.size() != 4 || extension->workspaces.size() != 4) return;
		UINK_CHECK(state, extension->devices[0].parentResolved && !extension->devices[1].parentResolved &&
			extension->devices[2].parentResolved && extension->devices[3].parentResolved);
		UINK_CHECK(state, extension->workspaces[0].parentResolved &&
			!extension->workspaces[1].parentResolved && !extension->workspaces[1].parentWorkspaceGuid &&
			extension->workspaces[2].parentResolved && extension->workspaces[3].parentResolved);
		const auto& cycles = DiagnosticCount(cyclic, UInkDiagnosticCode::ParentCycle);
		UINK_CHECK(state, cycles.count == 2 && cycles.firstByteOffset == HeaderPrefix().size() &&
			cycles.firstCanvasOrdinal == 0);
	}

	void TestDecoderDepthCeiling(TestState& state)
	{
		auto bytes = CanvasPrefix();
		const size_t endOfCanvas = bytes.size();
		UINK_CHECK(state, endOfCanvas > 0);
		AppendBytes(bytes, PackUnknown(999, 10000));
		UInkReadLimits limits;
		limits.maxDepth = 100000;
		const auto result = DecodeUInk(bytes, limits);
		CheckLimitPrefix(state, result, 1, 0);
		UINK_CHECK(state, DiagnosticCount(result, UInkDiagnosticCode::LimitExceeded).firstByteOffset == endOfCanvas &&
			result.validPrefixLength == endOfCanvas);
	}

	// 声明 100 万项、首项即非法标记的大数组：保留有效前缀，不提交坏块。
	void TestDeclaredContainerReserve(TestState& state)
	{
		auto bytes = CanvasPrefix();
		UINK_CHECK(state, !bytes.empty());
		if (bytes.empty()) return;
		const auto append = [&bytes](std::initializer_list<uint8_t> values)
		{
			for (uint8_t value : values) bytes.push_back(static_cast<std::byte>(value));
		};
		const auto appendText = [&bytes](std::string_view text)
		{
			bytes.push_back(static_cast<std::byte>(0xa0 | text.size()));
			for (char value : text) bytes.push_back(static_cast<std::byte>(value));
		};
		append({ 0x84 });
		appendText("type"); append({ 0xcd, 0x00, 0x06 });
		appendText("contentId"); append({ 0xce, 0x00, 0x00, 0x00, 0x63 });
		appendText("undoId"); append({ 0xce, 0x00, 0x00, 0x00, 0x63 });
		appendText("extra"); append({ 0x81 });
		appendText("a"); append({ 0xdd, 0x00, 0x0f, 0x42, 0x40, 0xc1 });
		bytes.insert(bytes.end(), 999999, std::byte{ 0xc0 });

		const auto actual = DecodeUInk(bytes);
		const auto prefix = DecodeUInk(CanvasPrefix());
		const size_t badOffset = CanvasPrefix().size();
		ExpectV11Canvas(state, actual, 1, 0, UInkReadOutcome::Partial);
		UINK_CHECK(state, actual.status == UInkReadStatus::StoppedAtCorruptTail &&
			actual.failedObjectOffset == badOffset && actual.validPrefixLength == badOffset &&
			actual.safeAppendOffset == badOffset && actual.modelCharge == prefix.modelCharge);
	}

	using WireBytes = std::vector<std::byte>;


	// 2 组短键及 width 长键均保留 float32 标记。偏移指向每个 0xca 后的载荷。
	constexpr std::array<uint8_t, 26> kPlainPointLayout{
		0x83, 0xa1, 'x', 0xca, 0, 0, 0, 0, 0xa1, 'y', 0xca, 0, 0, 0, 0,
		0xa5, 'w', 'i', 'd', 't', 'h', 0xca, 0, 0, 0, 0
	};
	constexpr std::array<size_t, 3> kPlainPointPayloadOffsets{ 4, 11, 22 };

	void SetWireFloat32(WireBytes& bytes, size_t payloadOffset, uint32_t bits)
	{
		for (unsigned shift : { 24u, 16u, 8u, 0u })
			bytes[payloadOffset++] = static_cast<std::byte>((bits >> shift) & 0xffu);
	}

	void TestPlainPointFloat32Decode(TestState& state)
	{
		using PointBits = std::array<uint32_t, 3>;
		constexpr uint32_t negativeZero = 0x80000000u;
		constexpr uint32_t minSubnormal = 0x00000001u;
		constexpr uint32_t nan = 0x7fc12345u;
		constexpr uint32_t positiveInfinity = 0x7f800000u;
		constexpr uint32_t negativeInfinity = 0xff800000u;
		constexpr PointBits basePoint{ negativeZero, negativeZero, std::bit_cast<uint32_t>(1.5f) };
		constexpr size_t pointCount = 3;

		msgpack::sbuffer buffer;
		msgpack::packer<msgpack::sbuffer> packer(buffer);
		packer.pack_map(8);
		PackKey(packer, "type"); packer.pack_fix_uint16(kInkType);
		PackKey(packer, "contentId"); packer.pack_fix_uint32(0);
		PackKey(packer, "undoId"); packer.pack_fix_uint32(0);
		PackKey(packer, "inkType"); packer.pack_fix_int32(1);
		PackKey(packer, "color"); PackColor(packer);
		PackKey(packer, "opacity"); packer.pack_float(0.5f);
		PackKey(packer, "texture"); packer.pack_fix_int32(0);
		PackKey(packer, "points"); packer.pack_array(static_cast<uint32_t>(pointCount));
		auto seed = CanvasPrefix();
		const size_t inkOffset = seed.size();
		AppendBytes(seed, ToBytes(buffer));
		const size_t pointsOffset = seed.size();

		for (size_t i = 0; i < pointCount; ++i)
		{
			const size_t start = seed.size();
			AppendBytes(seed, std::as_bytes(std::span(kPlainPointLayout)));
			for (size_t field = 0; field < basePoint.size(); ++field)
				SetWireFloat32(seed, start + kPlainPointPayloadOffsets[field], basePoint[field]);
		}

		auto check = [&](size_t pointIndex, const PointBits& value, bool accepted)
		{
			auto bytes = seed;
			for (size_t field = 0; field < value.size(); ++field)
				SetWireFloat32(bytes, pointsOffset + pointIndex * kPlainPointLayout.size() +
					kPlainPointPayloadOffsets[field], value[field]);

			const auto actual = DecodeUInk(bytes);
			const size_t inkCount = accepted ? 1 : 0;
			const auto outcome = accepted ? UInkReadOutcome::Complete : UInkReadOutcome::Partial;
			const auto* canvas = ExpectV11Canvas(state, actual, 1, inkCount, outcome);

			const auto& skipped = DiagnosticCount(actual, UInkDiagnosticCode::KnownBlockSkipped);
			UINK_CHECK(state, skipped.count == (accepted ? 0u : 1u));
			UINK_CHECK(state, DiagnosticCount(actual, UInkDiagnosticCode::CoordinateOutOfRange).count == 0);
			UINK_CHECK(state, DiagnosticCount(actual, UInkDiagnosticCode::InvalidFieldValue).count == 0);
			UINK_CHECK(state, actual.geometryPointCount == (accepted ? pointCount : 0));
			if (!accepted)
			{
				UINK_CHECK(state, skipped.firstByteOffset == inkOffset && skipped.firstCanvasOrdinal == 1);
				return;
			}

			if (!canvas) return;
			const auto* ink = std::get_if<UInkInk>(&canvas->content.front());
			UINK_CHECK(state, ink && ink->points.size() == pointCount);
			if (!ink || ink->points.size() != pointCount) return;
			// 其余点的位移均是 -0，因此特值从目标点起保留，首点也保留负零位。
			for (size_t i = 0; i < pointCount; ++i)
			{
				const auto& point = ink->points[i];
				UINK_CHECK(state, std::bit_cast<uint32_t>(point.x) == (i < pointIndex ? negativeZero : value[0]));
				UINK_CHECK(state, std::bit_cast<uint32_t>(point.y) == (i < pointIndex ? negativeZero : value[1]));
				UINK_CHECK(state, std::bit_cast<uint32_t>(point.width) ==
					(i == pointIndex ? value[2] : basePoint[2]));
			}
		};

		for (size_t pointIndex = 0; pointIndex < pointCount; ++pointIndex)
		{
			for (uint32_t bits : { nan, positiveInfinity, negativeInfinity })
			{
				check(pointIndex, { bits, basePoint[1], basePoint[2] }, false);
				check(pointIndex, { basePoint[0], bits, basePoint[2] }, false);
			}
			for (uint32_t bits : { 0u, negativeZero, std::bit_cast<uint32_t>(-1.5f), nan, positiveInfinity })
				check(pointIndex, { basePoint[0], basePoint[1], bits }, false);

			check(pointIndex, { negativeZero, negativeZero, minSubnormal }, true);
			check(pointIndex, { minSubnormal, 0x80000001u, minSubnormal }, true);
			check(pointIndex, { std::bit_cast<uint32_t>(1.5f), std::bit_cast<uint32_t>(-2.5f), minSubnormal }, true);
		}
	}

	void MeasureV11Decode(TestState& state)
	{
		UInkDocument document = MakeBasicDocument(false);
		for (uint32_t index = 0; index < 250; ++index)
		{
			UInkInk ink = MakeInk(index, index);
			ink.points.clear();
			for (uint32_t point = 0; point < 120; ++point)
			{
				ink.points.push_back({ static_cast<float>(point), static_cast<float>(index),
					4.0f, std::nullopt });
			}
			document.canvases[0].content.emplace_back(std::move(ink));
		}
		const auto encoded = EncodeUInkDocument(document);
		UINK_CHECK(state, encoded.status == UInkEncodeStatus::Success);
		if (encoded.status != UInkEncodeStatus::Success) return;
		const auto validated = DecodeUInk(encoded.bytes);
		const auto* page = ExpectV11Canvas(state, validated, 1, 250, UInkReadOutcome::Complete);
		if (!page) return;
		const bool allInk = std::all_of(page->content.begin(), page->content.end(),
			[](const UInkContent& content) { return std::holds_alternative<UInkInk>(content); });
		UINK_CHECK(state, validated.status == UInkReadStatus::Complete && allInk);
		if (validated.status != UInkReadStatus::Complete || !allInk) return;
		constexpr int kRuns = 20;
		uint64_t totalMicroseconds = 0;
		for (int run = 0; run < kRuns; ++run)
			totalMicroseconds += DecodeUInk(encoded.bytes).summary.decodeMicroseconds;
		std::cout << "UInk 1.1 decode (250 x 120 points, " << kRuns << " runs): "
			<< static_cast<double>(totalMicroseconds) / kRuns << " us average" << std::endl;
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

	void TestEncoderPayloadLengthBoundaries(TestState& state)
	{
		const UInkReadLimits defaults;
		UInkCanvas canvas = MakeBasicDocument(false).canvases.front();
		UInkShape shape = MakeShape(0, 0);
		UInkShapeObject object;
		object.id = Guid("60000000-0000-4000-8000-00000000b001");
		object.revision = 1;
		object.partCount = 1;
		UInkMessagePackExtension extension;
		extension.type = -7;
		// 官方 object.style 的通用 EXT 值仍计算类型字节与载荷长度。
		extension.data.resize(static_cast<size_t>(defaults.maxStringOrBinaryBytes - 1), std::byte{ 0x5a });
		object.style = UInkMessagePackValue::Map{
			{ Value(std::string("vendor.ext")), Value(std::move(extension)) } };
		shape.object = std::move(object);
		canvas.content.emplace_back(std::move(shape));
		const auto atLimit = EncodeUInkPage(canvas, true, true);
		UINK_CHECK(state, atLimit.status == UInkEncodeStatus::Success && !atLimit.bytes.empty());
		if (atLimit.status != UInkEncodeStatus::Success || atLimit.bytes.empty()) return;
		const UInkStreamContext context;
		const auto decoded = DecodeUInkStream(atLimit.bytes, defaults, &context);
		const auto* decodedPage = ExpectV11Canvas(state, decoded, 1, 1, UInkReadOutcome::Complete);
		if (!decodedPage) return;
		auto& root = std::get<UInkShapeObject>(*std::get<UInkShape>(canvas.content.front()).object);
		UINK_CHECK(state, decoded.status == UInkReadStatus::Complete &&
			EncodeUInkPage(*decodedPage, true, true).bytes == atLimit.bytes);

		UInkReadLimits limits = defaults;
		--limits.maxStringOrBinaryBytes;
		CheckLimitPrefix(state, DecodeUInkStream(atLimit.bytes, limits, &context), 1, 0);

		auto& ext = std::get<UInkMessagePackExtension>(root.style->front().second.value);
		ext.data.push_back(std::byte{ 0x5a });
		const auto aboveLimit = EncodeUInkPage(canvas, true, true);
		UINK_CHECK(state, aboveLimit.status == UInkEncodeStatus::LimitExceeded && aboveLimit.bytes.empty());
		UINK_CHECK(state, !aboveLimit.diagnostics.empty() &&
			aboveLimit.diagnostics.front().code == UInkDiagnosticCode::LimitExceeded &&
			aboveLimit.diagnostics.front().fieldPath == "shape.object.extension");

		canvas.content.clear();
		limits.maxStringOrBinaryBytes = 35;
		// 规范化的 GUID 是 36 字节，也受字符串上限约束。
		const auto longGuid = EncodeUInkPage(canvas, true, true, limits);
		UINK_CHECK(state, longGuid.status == UInkEncodeStatus::LimitExceeded && longGuid.bytes.empty());
	}

	msgpack::sbuffer PackTestRegistry(const char* key, const msgpack::sbuffer& extra)
	{
		msgpack::sbuffer field;
		msgpack::packer<msgpack::sbuffer> packer(field);
		PackKey(packer, key);
		packer.pack_array(1);
		const bool device = std::string_view(key) == "devices";
		packer.pack_map(device ? 7u : 4u);
		PackKey(packer, "guid");
		PackGuid(packer, device ? "40000000-0000-4000-8000-00000000e001" :
			"30000000-0000-4000-8000-00000000e001");
		if (device)
		{
			PackKey(packer, "deviceType"); packer.pack_fix_int32(0);
			PackKey(packer, "x"); packer.pack_fix_int32(0);
			PackKey(packer, "y"); packer.pack_fix_int32(0);
			PackKey(packer, "width"); packer.pack_fix_uint32(1920);
			PackKey(packer, "height"); packer.pack_fix_uint32(1080);
		}
		else
		{
			PackKey(packer, "workspaceType"); packer.pack_fix_int32(1);
			PackKey(packer, "currentPageIndex"); packer.pack_fix_uint32(0);
		}
		field.write(extra.data(), extra.size());
		return field;
	}

	void TestRegistryExtraScopeAndReplay(TestState& state)
	{
		constexpr uint32_t kLargeExtraBytes = 1024 * 1024;
		const auto extra = PackExtraBinary(0, 0, kLargeExtraBytes);
		const auto devices = PackTestRegistry("devices", extra);
		const auto workspaces = PackTestRegistry("workspaces", extra);
		auto source = MakeBasicDocument();
		source.usesImplicitDevice = source.usesImplicitWorkspace = false;
		auto& extension = source.headerExtension.emplace();
		UInkDevice device;
		device.guid = Guid("40000000-0000-4000-8000-00000000e001");
		device.geometry = UInkDisplayDevice{ 0, 0, 1920, 1080 };
		extension.devices.push_back(device);
		UInkWorkspace workspace;
		workspace.guid = Guid("30000000-0000-4000-8000-00000000e001");
		workspace.workspaceType = 1;
		extension.workspaces.push_back(workspace);
		auto& canvas = source.canvases.front();
		canvas.deviceGuid = device.guid;
		canvas.workspaceGuid = workspace.guid;
		canvas.content.emplace_back(MakeShape(1, 2));

		const auto canonical = EncodeUInkDocument(source);
		const auto header = EncodeUInkHeaderPrefix(source.header, std::nullopt);
		const auto prefix = EncodeUInkHeaderPrefix(source.header, source.headerExtension);
		const auto page = EncodeUInkPage(canvas, false, false);
		const auto pageObjects = ObjectSizes(page.bytes);
		const bool fixtureReady = canonical.status == UInkEncodeStatus::Success &&
			header.status == UInkEncodeStatus::Success && !header.bytes.empty() &&
			prefix.status == UInkEncodeStatus::Success && !prefix.bytes.empty() &&
			page.status == UInkEncodeStatus::Success && pageObjects.size() == 3;
		UINK_CHECK(state, fixtureReady);
		if (!fixtureReady) return;
		const auto canonicalRead = DecodeUInk(canonical.bytes);
		UINK_CHECK(state, canonicalRead.status == UInkReadStatus::Complete && canonicalRead.diagnostics.empty());

		const auto packExtension = [&](bool typeLast, const msgpack::sbuffer* ordinary)
		{
			msgpack::sbuffer block;
			msgpack::packer<msgpack::sbuffer> packer(block);
			packer.pack_map(3 + (ordinary ? 1u : 0u));
			const auto packType = [&]()
			{
				PackKey(packer, "type"); packer.pack_fix_uint16(kHeaderExtensionType);
			};
			if (!typeLast) packType();
			block.write(devices.data(), devices.size());
			block.write(workspaces.data(), workspaces.size());
			if (ordinary) block.write(ordinary->data(), ordinary->size());
			if (typeLast) packType();
			return ToBytes(block);
		};
		const auto checkOfficialFields = [&](const UInkReadResult& read, UInkReadOutcome outcome)
		{
			if (!ExpectV11Canvas(state, read, 1, 2, outcome)) return;
			const auto roundTrip = EncodeUInkDocument(*read.document);
			UINK_CHECK(state, roundTrip.status == UInkEncodeStatus::Success &&
				roundTrip.bytes == canonical.bytes);
		};

		// 同名数组放到 Shape，分别测试两种键和 type 的两种顺序。
		const auto shapeStart = page.bytes.end() - pageObjects.back();
		const auto shapeOffset = prefix.bytes.size() + page.bytes.size() - pageObjects.back();
		UInkReadLimits tight;
		tight.maxModelCharge = kLargeExtraBytes;
		for (const bool typeLast : { false, true })
		{
			auto bytes = header.bytes;
			AppendBytes(bytes, packExtension(typeLast, nullptr));
			AppendBytes(bytes, page.bytes);
			const auto registered = DecodeUInk(bytes, tight);
			UINK_CHECK(state, registered.status == UInkReadStatus::Complete &&
				registered.summary.outcome == UInkReadOutcome::Complete && registered.diagnostics.empty() &&
				registered.validPrefixLength == bytes.size() && registered.modelCharge == canonicalRead.modelCharge);
			UINK_CHECK(state, registered.replayedObjectCount == 0);
			checkOfficialFields(registered, UInkReadOutcome::Complete);

			for (const auto* field : { &devices, &workspaces })
			{
				std::vector<std::byte> shape(shapeStart, page.bytes.end());
				AppendTestMapField(state, shape, *field);
				const bool fieldAppended = shape.size() == pageObjects.back() + field->size();
				UINK_CHECK(state, fieldAppended);
				if (!fieldAppended) return;
				if (typeLast) std::rotate(shape.begin() + 1, shape.end() - field->size(), shape.end());
				bytes = prefix.bytes;
				AppendBytes(bytes, std::span(page.bytes).first(page.bytes.size() - pageObjects.back()));
				AppendBytes(bytes, shape);

				const auto limited = DecodeUInk(bytes, tight);
				CheckLimitPrefix(state, limited, 1, 1);
				const auto& diagnostic = DiagnosticCount(limited, UInkDiagnosticCode::LimitExceeded);
				UINK_CHECK(state, limited.validPrefixLength == shapeOffset &&
					diagnostic.count == 1 && diagnostic.firstByteOffset == shapeOffset);

				const auto charged = DecodeUInk(bytes);
				UINK_CHECK(state, charged.status == UInkReadStatus::Complete &&
					charged.modelCharge > kLargeExtraBytes && charged.replayedObjectCount == 0);
				UInkReadLimits replay;
				replay.maxModelCharge = charged.modelCharge + 1024;
				const auto chargedReplay = DecodeUInk(bytes, replay);
				UINK_CHECK(state, chargedReplay.replayedObjectCount > 0 &&
					chargedReplay.modelCharge == charged.modelCharge && chargedReplay.status == charged.status);
				checkOfficialFields(charged, UInkReadOutcome::Complete);
				checkOfficialFields(chargedReplay, UInkReadOutcome::Complete);
			}
		}

		// 普通字段足以越过较窄的临时窗口。用同一输入对跑，避免引入重复 type 诊断。
		msgpack::sbuffer ordinary;
		msgpack::packer<msgpack::sbuffer> packer(ordinary);
		const std::string padding(4096, 'P');
		PackKey(packer, "vendorPadding");
		packer.pack_bin(static_cast<uint32_t>(padding.size()));
		packer.pack_bin_body(padding.data(), static_cast<uint32_t>(padding.size()));
		std::vector<std::byte> shape(shapeStart, page.bytes.end());
		AppendTestMapField(state, shape, extra);
		AppendTestMapField(state, shape, ordinary);
		auto bytes = header.bytes;
		AppendBytes(bytes, packExtension(true, &ordinary));
		AppendBytes(bytes, std::span(page.bytes).first(page.bytes.size() - pageObjects.back()));
		AppendBytes(bytes, shape);
		const size_t goodEnd = bytes.size();

		// 让诊断对比包含实际的坏标记，同时检查 extra 内的 wire 错误不会被吞掉。
		auto clear = EncodeTestContent(state, MakeClear(0, 0));
		UINK_CHECK(state, !clear.empty());
		if (clear.empty()) return;
		msgpack::sbuffer badExtra;
		msgpack::packer<msgpack::sbuffer> badPacker(badExtra);
		PackKey(badPacker, "extra"); badPacker.pack_map(1); PackKey(badPacker, "vendor");
		badExtra.write("\xc1", 1);
		AppendTestMapField(state, clear, badExtra);
		AppendBytes(bytes, clear);

		const auto fast = DecodeUInk(bytes, tight);
		UInkReadLimits replay;
		replay.maxModelCharge = fast.modelCharge + 1024;
		const auto replayed = DecodeUInk(bytes, replay);
		UINK_CHECK(state, fast.replayedObjectCount == 0 && replayed.replayedObjectCount >= 2);
		UINK_CHECK(state, fast.status == UInkReadStatus::StoppedAtCorruptTail &&
			replayed.status == fast.status && fast.summary.outcome == UInkReadOutcome::Partial &&
			replayed.summary.outcome == fast.summary.outcome);
		UINK_CHECK(state, fast.validPrefixLength == goodEnd && replayed.validPrefixLength == goodEnd &&
			fast.failedObjectOffset == goodEnd && replayed.failedObjectOffset == goodEnd &&
			fast.safeAppendOffset == replayed.safeAppendOffset);
		UINK_CHECK(state, replayed.modelCharge == fast.modelCharge &&
			replayed.decodedObjectCount == fast.decodedObjectCount &&
			replayed.geometryPointCount == fast.geometryPointCount);
		UINK_CHECK(state, fast.diagnostics.size() == 1 && replayed.diagnostics.size() == 1);
		if (fast.diagnostics.size() != 1 || replayed.diagnostics.size() != 1) return;
		const auto& a = fast.diagnostics.front();
		const auto& b = replayed.diagnostics.front();
		UINK_CHECK(state, a.code == UInkDiagnosticCode::MalformedMessagePack && a.byteOffset == goodEnd &&
			b.code == a.code && b.severity == a.severity && b.byteOffset == a.byteOffset &&
			b.objectIndex == a.objectIndex && b.canvasOrdinal == a.canvasOrdinal &&
			b.fieldPath == a.fieldPath && b.systemError == a.systemError);
		checkOfficialFields(fast, UInkReadOutcome::Partial);
		checkOfficialFields(replayed, UInkReadOutcome::Partial);
	}

	void TestExtraWireSkipping(TestState& state)
	{
		// 将大扩展附到带官方对象层的 Shape。按原模型预算解码并重编码，应只留下官方字段。
		auto document = MakeV11Document();
		auto& root = std::get<UInkShapeObject>(*std::get<UInkShape>(document.canvases.front().content[3]).object);
		root.style->emplace_back(Value(std::string("extra")), Value(std::string("object style value")));
		const auto encoded = EncodeUInkDocument(document);
		UINK_CHECK(state, encoded.status == UInkEncodeStatus::Success);
		const auto sizes = ObjectSizes(encoded.bytes);
		UINK_CHECK(state, sizes.size() == 7);
		if (sizes.size() != 7) return;
		size_t shapeOffset = 0;
		for (size_t i = 0; i < 5; ++i) shapeOffset += sizes[i];
		const auto shape = std::span(encoded.bytes).subspan(shapeOffset, sizes[5]);
		std::vector<std::byte> shapeWithExtra(shape.begin(), shape.end());
		AppendTestMapField(state, shapeWithExtra, PackExtraBinary(4, 32768, 1024 * 1024));
		std::vector<std::byte> bytes(encoded.bytes.begin(), encoded.bytes.begin() + shapeOffset);
		AppendBytes(bytes, shapeWithExtra);
		AppendBytes(bytes, std::span(encoded.bytes).last(sizes.back()));
		const auto baseline = DecodeUInk(encoded.bytes);
		if (!ExpectV11Canvas(state, baseline, 1, 5, UInkReadOutcome::Complete)) return;
		UInkReadLimits limits;
		limits.maxModelCharge = baseline.modelCharge;
		const auto skipped = DecodeUInk(bytes, limits);
		if (!ExpectV11Canvas(state, skipped, 1, 5, UInkReadOutcome::Complete)) return;
		UINK_CHECK(state, skipped.modelCharge == baseline.modelCharge && skipped.diagnostics.empty());
		const auto roundTrip = EncodeUInkDocument(*skipped.document);
		UINK_CHECK(state, roundTrip.status == UInkEncodeStatus::Success && roundTrip.bytes == encoded.bytes);
		limits = {};
		limits.maxFileBytes = bytes.size() - 1;
		CheckLimitFailure(state, DecodeUInk(bytes, limits));

		// 合法块前缀后跟含扩展的 Clear。资源限制、坏键与截断仍采用常规流诊断。
		auto clearDocument = MakeBasicDocument();
		clearDocument.canvases.front().content.emplace_back(MakeClear(1, 1));
		const auto clearStream = EncodeUInkDocument(clearDocument);
		const auto clearSizes = ObjectSizes(clearStream.bytes);
		UINK_CHECK(state, clearStream.status == UInkEncodeStatus::Success && clearSizes.size() == 4);
		if (clearSizes.size() != 4) return;
		const size_t failedOffset = clearStream.bytes.size() - clearSizes.back();
		const auto makeTail = [&](const msgpack::sbuffer& extra)
		{
			const auto boundary = clearStream.bytes.begin() + failedOffset;
			std::vector<std::byte> block(boundary, clearStream.bytes.end());
			AppendTestMapField(state, block, extra);
			std::vector<std::byte> withTail(clearStream.bytes.begin(), boundary);
			AppendBytes(withTail, block);
			return withTail;
		};
		auto truncatedBytes = makeTail(PackExtraBinary(0, 0, 64));
		truncatedBytes.pop_back();
		const auto truncated = DecodeUInk(truncatedBytes);
		ExpectV11Canvas(state, truncated, 1, 1, UInkReadOutcome::Partial);
		UINK_CHECK(state, truncated.status == UInkReadStatus::RecoveredTruncatedTail &&
			truncated.failedObjectOffset == failedOffset && truncated.validPrefixLength == failedOffset &&
			HasDiagnostic(truncated, UInkDiagnosticCode::TruncatedTail));

		const auto checkLimited = [&](const auto& input, const UInkReadLimits& budget)
		{
			const auto read = DecodeUInk(input, budget);
			CheckLimitPrefix(state, read, 1, 1);
			const auto& diagnostic = DiagnosticCount(read, UInkDiagnosticCode::LimitExceeded);
			UINK_CHECK(state, read.validPrefixLength == failedOffset && diagnostic.count == 1 &&
				diagnostic.firstByteOffset == failedOffset && diagnostic.firstCanvasOrdinal == 1);
		};
		checkLimited(makeTail(PackExtraBinary(64, 0, 1)), {});
		limits = {};
		limits.maxStringOrBinaryBytes = 64;
		checkLimited(makeTail(PackExtraBinary(0, 0, 65)), limits);
		limits = {};
		limits.maxContainerEntries = 16;
		checkLimited(makeTail(PackExtraBinary(0, 16, 1)), limits);
		limits = {};
		limits.maxTopLevelObjectBytes = *std::max_element(clearSizes.begin(), clearSizes.end());
		checkLimited(makeTail(PackExtraBinary(0, 0, 4096)), limits);

		msgpack::sbuffer invalidKey;
		msgpack::packer<msgpack::sbuffer> packer(invalidKey);
		PackKey(packer, "extra"); packer.pack_map(1);
		packer.pack_str(1); packer.pack_str_body("\xff", 1); packer.pack_nil();
		const auto invalid = DecodeUInk(makeTail(invalidKey));
		ExpectV11Canvas(state, invalid, 1, 1, UInkReadOutcome::Partial);
		UINK_CHECK(state, invalid.status == UInkReadStatus::RecoveredInvalidTail &&
			invalid.validPrefixLength == failedOffset && HasDiagnostic(invalid, UInkDiagnosticCode::InvalidFieldValue));

		msgpack::sbuffer wrongType;
		msgpack::packer<msgpack::sbuffer> wrongPacker(wrongType);
		PackKey(wrongPacker, "extra"); wrongPacker.pack_nil();
		const auto notMap = DecodeUInk(makeTail(wrongType));
		ExpectV11Canvas(state, notMap, 1, 1, UInkReadOutcome::Partial);
		UINK_CHECK(state, notMap.status == UInkReadStatus::RecoveredInvalidTail &&
			notMap.validPrefixLength == failedOffset && HasDiagnostic(notMap, UInkDiagnosticCode::KnownBlockSkipped));

		msgpack::sbuffer emptyExtra;
		msgpack::packer<msgpack::sbuffer> emptyPacker(emptyExtra);
		PackKey(emptyPacker, "extra"); emptyPacker.pack_map(0);
		auto duplicateBytes = makeTail(emptyExtra);
		std::vector<std::byte> duplicateBlock(duplicateBytes.begin() + failedOffset, duplicateBytes.end());
		AppendTestMapField(state, duplicateBlock, emptyExtra);
		duplicateBytes.resize(failedOffset);
		AppendBytes(duplicateBytes, duplicateBlock);
		const auto duplicate = DecodeUInk(duplicateBytes);
		ExpectV11Canvas(state, duplicate, 1, 1, UInkReadOutcome::Partial);
		UINK_CHECK(state, duplicate.status == UInkReadStatus::RecoveredInvalidTail &&
			duplicate.validPrefixLength == failedOffset && HasDiagnostic(duplicate, UInkDiagnosticCode::DuplicateKnownKey));
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
		if (basicEncoded.status != UInkEncodeStatus::Success || basic.status != UInkReadStatus::Complete ||
			basic.geometryPointCount != 2) return;

		UInkReadLimits limits;
		limits.maxFileBytes = basicEncoded.bytes.size();
		UINK_CHECK(state, DecodeUInk(basicEncoded.bytes, limits).status == UInkReadStatus::Complete);
		limits.maxFileBytes = basicEncoded.bytes.size() - 1;
		CheckLimitFailure(state, DecodeUInk(basicEncoded.bytes, limits));

		limits = {};
		limits.maxTopLevelObjects = basic.decodedObjectCount;
		UINK_CHECK(state, DecodeUInk(basicEncoded.bytes, limits).status == UInkReadStatus::Complete);
		limits.maxTopLevelObjects = 0;
		CheckLimitFailure(state, DecodeUInk(basicEncoded.bytes, limits));
		limits.maxTopLevelObjects = basic.decodedObjectCount - 1;
		const auto objectLimited = DecodeUInk(basicEncoded.bytes, limits);
		CheckLimitPrefix(state, objectLimited, 1, 0);

		const std::vector<size_t> sizes = ObjectSizes(basicEncoded.bytes);
		UINK_CHECK(state, sizes.size() == 3);
		if (sizes.size() != 3) return;
		UINK_CHECK(state, objectLimited.failedObjectOffset == sizes[0] + sizes[1]);
		const size_t largest = *std::max_element(sizes.begin(), sizes.end());
		limits = {};
		limits.maxTopLevelObjectBytes = largest;
		UINK_CHECK(state, DecodeUInk(basicEncoded.bytes, limits).status == UInkReadStatus::Complete);
		limits.maxTopLevelObjectBytes = largest - 1;
		const auto byteLimited = DecodeUInk(basicEncoded.bytes, limits);
		if (sizes[1] < largest && sizes[0] < largest)
			CheckLimitPrefix(state, byteLimited, 1, 0);
		else
			CheckLimitFailure(state, byteLimited);

		limits = {};
		limits.maxGeometryPoints = 2;
		limits.maxSingleGeometryPoints = 2;
		UINK_CHECK(state, DecodeUInk(basicEncoded.bytes, limits).status == UInkReadStatus::Complete);
		limits.maxSingleGeometryPoints = 1;
		CheckLimitPrefix(state, DecodeUInk(basicEncoded.bytes, limits), 1, 0);

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
		const auto pointLimited = DecodeUInk(twoInkBytes, limits);
		CheckLimitPrefix(state, pointLimited, 1, 1);
		UINK_CHECK(state, pointLimited.geometryPointCount == 1);

		const auto prefix = CanvasPrefix();
		auto nestedBytes = prefix;
		auto nestedBlock = EncodeTestContent(state, MakeClear(0, 0));
		AppendTestMapField(state, nestedBlock, PackExtraBinary(4, 12, 40));
		AppendBytes(nestedBytes, nestedBlock);
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
			CheckLimitPrefix(state, DecodeUInk(nestedBytes, limits), 1, 0);
		}

		limits = {};
		limits.maxContainerEntries = 13;
		UINK_CHECK(state, DecodeUInk(nestedBytes, limits).status == UInkReadStatus::Complete);
		limits.maxContainerEntries = 12;
		CheckLimitPrefix(state, DecodeUInk(nestedBytes, limits), 1, 0);
		limits = {};
		limits.maxStringOrBinaryBytes = 40;
		UINK_CHECK(state, DecodeUInk(nestedBytes, limits).status == UInkReadStatus::Complete);
		limits.maxStringOrBinaryBytes = 39;
		CheckLimitPrefix(state, DecodeUInk(nestedBytes, limits), 1, 0);
		limits = {};
		limits.maxStringOrBinaryBytes = 36;
		UINK_CHECK(state, DecodeUInk(CanvasPrefix(), limits).status == UInkReadStatus::Complete);
		limits.maxStringOrBinaryBytes = 35;
		CheckLimitFailure(state, DecodeUInk(CanvasPrefix(), limits));

		limits = {};
		limits.maxModelCharge = basic.modelCharge;
		UINK_CHECK(state, DecodeUInk(basicEncoded.bytes, limits).status == UInkReadStatus::Complete);
		limits.maxModelCharge = basic.modelCharge - 1;
		const auto budgetLimited = DecodeUInk(basicEncoded.bytes, limits);
		const auto prefixCharge = DecodeUInk(std::span(basicEncoded.bytes).first(sizes[0] + sizes[1]));
		if (!ExpectV11Canvas(state, prefixCharge, 1, 0, UInkReadOutcome::Complete)) return;
		if (prefixCharge.modelCharge < basic.modelCharge)
			CheckLimitPrefix(state, budgetLimited, 1, 0);
		else
			CheckLimitFailure(state, budgetLimited);
		limits = {};
		limits.maxModelCharge = 127;
		CheckLimitFailure(state, DecodeUInk(basicEncoded.bytes, limits));

		const std::vector<std::byte> richBytes = EncodeUInkDocument(MakeRichDocument()).bytes;
		const size_t mediaPathBytes = std::string("media/picture.png").size();
		limits = {};
		limits.maxMediaPathBytes = static_cast<uint32_t>(mediaPathBytes);
		UINK_CHECK(state, DecodeUInk(richBytes, limits).status == UInkReadStatus::Complete);
		limits.maxMediaPathBytes = static_cast<uint32_t>(mediaPathBytes - 1);
		const auto pathLimited = DecodeUInk(richBytes, limits);
		CheckLimitPrefix(state, pathLimited, 3, 9);
		ExpectV11Canvas(state, pathLimited, 3, 1, UInkReadOutcome::Partial, 2);

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
		CheckLimitPrefix(state, DecodeUInk(deepUnknown, limits), 1, 0);

		// 大量微小节点的 wire bytes 很少，但 msgpack 临时对象树仍必须先计费。
		std::vector<std::byte> wideUnknown = CanvasPrefix();
		AppendBytes(wideUnknown, PackWideUnknown(4096));
		limits = {};
		limits.maxModelCharge = 64 * 1024;
		CheckLimitPrefix(state, DecodeUInk(wideUnknown, limits), 1, 0);
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

	void TestV11FileResourceRejection(TestState& state)
	{
		TempDirectory temporary;
		UINK_CHECK(state, temporary.IsValid());
		if (!temporary.IsValid()) return;
		const auto path = temporary.File(L"resource-limit.uink");
		auto bytes = CanvasPrefix();
		const size_t prefixBytes = bytes.size();
		if (!WriteBytes(path, bytes)) { UINK_CHECK(state, false); return; }
		const auto baseline = ReadUInkStoreFile(path);
		if (!ExpectV11Canvas(state, baseline, 1, 0, UInkReadOutcome::Complete)) return;

		AppendBytes(bytes, PackV11OversizedShape());
		if (!WriteBytes(path, bytes)) { UINK_CHECK(state, false); return; }
		const auto limited = ReadUInkStoreFile(path);
		CheckLimitPrefix(state, limited, 1, 0);
		UINK_CHECK(state, limited.validPrefixLength == prefixBytes && ReadBytes(path) == bytes);
	}

	void TestMediaFileRead(TestState& state)
	{
		TempDirectory temporary;
		UINK_CHECK(state, temporary.IsValid());
		if (!temporary.IsValid()) return;
		const auto mainPath = temporary.File(L"media.uink");
		const auto extraPath = temporary.File(L"media.uink.extra");
		UINK_CHECK(state, temporary.CopyFixture(L"media-safe.uink", L"media.uink"));
		const std::array sentinel{ std::byte{ 0x50 }, std::byte{ 0x4b }, std::byte{ 0x01 },
			std::byte{ 0x02 }, std::byte{ 0x03 } };
		UINK_CHECK(state, WriteBytes(extraPath, sentinel));
		const auto source = ReadUInkStoreFile(mainPath);
		const auto* page = ExpectV11Canvas(state, source, 1, 2, UInkReadOutcome::Complete);
		if (!page) return;
		const auto* media = std::get_if<UInkMedia>(&page->content.front());
		UINK_CHECK(state, media && media->path == "images/pixel.png" && media->pathIsSafe &&
			media->resourceState == UInkMediaResourceState::Unavailable);
		UINK_CHECK(state, HasDiagnostic(source, UInkDiagnosticCode::MediaResourceUnavailable));
		UINK_CHECK(state, ReadBytes(extraPath) == std::vector<std::byte>(sentinel.begin(), sentinel.end()));

		// 整读完成即关闭文件，之后的独占读取无需等待其他持有者。
		HANDLE exclusive = CreateFileW(mainPath.c_str(), GENERIC_READ, 0, nullptr,
			OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
		UINK_CHECK(state, exclusive != INVALID_HANDLE_VALUE);
		if (exclusive != INVALID_HANDLE_VALUE)
		{
			UINK_CHECK(state, ReadUInkStoreFile(mainPath).status == UInkReadStatus::IoError);
			CloseHandle(exclusive);
		}
		UINK_CHECK(state, ReadUInkStoreFile(mainPath).status == UInkReadStatus::Complete);
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

	void TestClearContent(TestState& state)
	{
		UInkDocument document = MakeBasicDocument(false);
		UInkCanvas& canvas = document.canvases[0];
		canvas.content.push_back(MakeInk(0, 0));
		canvas.content.push_back(MakeClear(1, 1));
		canvas.content.push_back(MakeInk(2, 2));
		canvas.content.push_back(MakeClear(3, 3));
		canvas.content.push_back(MakeInk(4, 4));
		std::get<UInkInk>(canvas.content[0]).color.fallbackRgb = 0x102030;
		std::get<UInkInk>(canvas.content[2]).color.fallbackRgb = 0x405060;
		std::get<UInkInk>(canvas.content[4]).color.fallbackRgb = 0x708090;

		const UInkEncodeResult encoded = EncodeUInkDocument(document);
		UINK_CHECK(state, encoded.status == UInkEncodeStatus::Success);
		const UInkReadResult decoded = DecodeUInk(encoded.bytes);
		UINK_CHECK(state, decoded.status == UInkReadStatus::Complete);
		UINK_CHECK(state, decoded.document && decoded.document->canvases.size() == 1);
		if (decoded.document && decoded.document->canvases.size() == 1)
		{
			const UInkCanvas& output = decoded.document->canvases[0];
			UINK_CHECK(state, output.content.size() == 5);
			UINK_CHECK(state, std::holds_alternative<UInkClear>(output.content[1]));
			UINK_CHECK(state, std::holds_alternative<UInkClear>(output.content[3]));
			if (const UInkClear* clear = std::get_if<UInkClear>(&output.content[3]))
				UINK_CHECK(state, clear->contentId == 3 && clear->undoId == 3);

			// 最后一个 Clear 之前的内容不参与当前画布合成。
			const std::vector<bool> visible = ComputeUInkLatestVisibility(output);
			UINK_CHECK(state, visible.size() == 5);
			UINK_CHECK(state, !visible[0] && !visible[1] && !visible[2] &&
				visible[3] && visible[4]);
		}

		UInkDocument sharedWithPrevious = MakeBasicDocument(false);
		sharedWithPrevious.canvases[0].content.push_back(MakeInk(0, 0));
		sharedWithPrevious.canvases[0].content.push_back(MakeClear(1, 0));
		UINK_CHECK(state, EncodeUInkDocument(sharedWithPrevious).status ==
			UInkEncodeStatus::InvalidModel);

		UInkDocument sharedWithFollowing = MakeBasicDocument(false);
		sharedWithFollowing.canvases[0].content.push_back(MakeClear(0, 0));
		sharedWithFollowing.canvases[0].content.push_back(MakeInk(1, 0));
		UINK_CHECK(state, EncodeUInkDocument(sharedWithFollowing).status ==
			UInkEncodeStatus::InvalidModel);

		// 多个 Clear 的文件只恢复最近边界与紧邻的两个区间。
		const auto* page = ExpectV11Canvas(state, decoded, 1, 5, UInkReadOutcome::Complete);
		if (!page) return;
		const auto imported = ImportUInkV11Page(*page);
		UINK_CHECK(state, imported.status == Draw3UInkV11ImportStatus::Success && imported.page);
		if (!imported.page) return;
		const auto& restored = *imported.page;
		UINK_CHECK(state, imported.counts.multipleClears == 1 && restored.intervalOrdinal == 1 &&
			restored.operations.size() == 3 && restored.strokes.size() == 1);
		if (restored.operations.size() != 3 || restored.strokes.size() != 1) return;
		const auto* previous = std::get_if<Draw3UInkStrokeSnapshot>(&restored.operations[0]);
		UINK_CHECK(state, previous && previous->style.fallbackRgb == 0x405060 &&
			std::holds_alternative<Draw3UInkClearSnapshot>(restored.operations[1]) &&
			restored.strokes.front().style.fallbackRgb == 0x708090);
	}

	void TestDraw3PageExport(TestState& state)
	{
		Draw3UInkCanvasSnapshot page;
		page.pageGuid = Guid("50000000-0000-4000-8000-00000000f001");
		page.pageNumber = 1;
		page.viewport = { 10.0f, -20.0f, 1.0f };
		const Draw3UInkStrokeKind kinds[] = { Draw3UInkStrokeKind::Pen, Draw3UInkStrokeKind::Highlighter,
			Draw3UInkStrokeKind::Eraser, Draw3UInkStrokeKind::SolidLine, Draw3UInkStrokeKind::DashedLine,
			Draw3UInkStrokeKind::OutlineRectangle, Draw3UInkStrokeKind::FilledRectangle };
		for (uint32_t index = 0; index < std::size(kinds); ++index)
		{
			Draw3UInkStrokeSnapshot stroke;
			stroke.style.kind = kinds[index];
			stroke.style.fallbackRgb = 0x100000 + index;
			stroke.style.opacity = kinds[index] == Draw3UInkStrokeKind::Highlighter ? 0.4f : 1.0f;
			stroke.points = { { 10.0f + index, 20.0f, 4.0f }, { 110.0f + index, 60.0f, 4.0f } };
			stroke.undoId = index;
			page.strokes.push_back(std::move(stroke));
		}

		Draw3UInkV11PageContext context;
		context.workspaceGuid = Guid("30000000-0000-4000-8000-00000000f001");
		context.workspaceType = 1;
		for (const float dpiScale : { 2.0f, 1.5f })
		{
			context.dpiScale = dpiScale;
			const auto exported = ExportDraw3PageToUInkV11(page, context);
			UINK_CHECK(state, exported.status == Draw3UInkExportStatus::Success && exported.canvas);
			if (!exported.canvas) continue;
			const auto& output = *exported.canvas;
			UINK_CHECK(state, output.workspaceGuid == context.workspaceGuid && output.viewport &&
				output.viewport->x == 10.0f && output.viewport->y == -20.0f && output.content.size() == 7);
			if (output.content.size() != 7) continue;
			UINK_CHECK(state, std::get<UInkInk>(output.content[0]).effectiveKind == UInkInkKind::Pen);
			const auto& highlighter = std::get<UInkInk>(output.content[1]);
			UINK_CHECK(state, highlighter.effectiveKind == UInkInkKind::Highlighter && highlighter.nib &&
				highlighter.nib->aspect == 0.125f);
			UINK_CHECK(state, std::get<UInkInk>(output.content[2]).effectiveKind == UInkInkKind::Erase);
			const auto& solid = std::get<UInkShape>(output.content[3]);
			const auto& dashed = std::get<UInkShape>(output.content[4]);
			const auto& outline = std::get<UInkShape>(output.content[5]);
			const auto& filled = std::get<UInkShape>(output.content[6]);
			UINK_CHECK(state, solid.declaredShapeType == 0 && solid.stroke);
			UINK_CHECK(state, dashed.declaredShapeType == 0 && dashed.stroke &&
				(dashed.stroke->dashArray == std::vector<float>{ 16.0f, 24.0f }));
			UINK_CHECK(state, outline.declaredShapeType == 2 && outline.stroke && !outline.fill);
			UINK_CHECK(state, filled.declaredShapeType == 2 && !filled.stroke && filled.fill);
			const auto* rectangle = std::get_if<UInkRectangleGeometry>(&outline.geometry);
			UINK_CHECK(state, rectangle && rectangle->cornerRadiusX == 4.0f * dpiScale);
			const auto encoded = EncodeUInkPage(output, false, true);
			UINK_CHECK(state, encoded.status == UInkEncodeStatus::Success && !encoded.bytes.empty());
			if (encoded.status == UInkEncodeStatus::Success && !encoded.bytes.empty())
				CheckNoWireExtra(state, encoded.bytes, 1 + output.content.size());
			const auto read = ImportUInkV11Page(output, { dpiScale });
			UINK_CHECK(state, read.status == Draw3UInkV11ImportStatus::Success && read.page &&
				read.page->strokes.size() == 7);
			if (!read.page || read.page->strokes.size() != 7) continue;
			for (uint32_t index = 0; index < std::size(kinds); ++index)
			{
				const auto& stroke = read.page->strokes[index];
				UINK_CHECK(state, stroke.style.kind == (index < 5 ? kinds[index] : Draw3UInkStrokeKind::Shape) &&
					stroke.undoId == index);
			}
			const auto& outlineShape = read.page->strokes[5].shape;
			const auto& filledShape = read.page->strokes[6].shape;
			UINK_CHECK(state, outlineShape && outlineShape->legacyKind == shapes::LegacyKind::OutlineRectangle);
			UINK_CHECK(state, filledShape && filledShape->legacyKind == shapes::LegacyKind::FilledRectangle);
		}

		auto invalid = page;
		invalid.strokes[0].points[0].width = 0.0f;
		UINK_CHECK(state, ExportDraw3PageToUInkV11(invalid, context).status ==
			Draw3UInkExportStatus::InvalidSourceStroke);
		invalid = page;
		invalid.viewport.scale = 2.0f;
		UINK_CHECK(state, ExportDraw3PageToUInkV11(invalid, context).status ==
			Draw3UInkExportStatus::InvalidSnapshot);
		invalid = page;
		invalid.strokes[0].style.texture = 1;
		UINK_CHECK(state, ExportDraw3PageToUInkV11(invalid, context).status ==
			Draw3UInkExportStatus::InvalidSourceStroke);
		invalid = page;
		invalid.strokes[0].style.kind = static_cast<Draw3UInkStrokeKind>(255);
		UINK_CHECK(state, ExportDraw3PageToUInkV11(invalid, context).status ==
			Draw3UInkExportStatus::InvalidSourceStroke);

		page.operations = { page.strokes.front(), Draw3UInkClearSnapshot{ 1 } };
		page.strokes.clear();
		page.intervalOrdinal = 1;
		const auto cleared = ExportDraw3PageToUInkV11(page, context);
		UINK_CHECK(state, cleared.status == Draw3UInkExportStatus::Success && cleared.canvas);
		if (cleared.status != Draw3UInkExportStatus::Success || !cleared.canvas) return;
		const auto encoded = EncodeUInkPage(*cleared.canvas, false, true);
		UINK_CHECK(state, encoded.status == UInkEncodeStatus::Success && !encoded.bytes.empty());
		if (encoded.status != UInkEncodeStatus::Success || encoded.bytes.empty()) return;
		CheckNoWireExtra(state, encoded.bytes, 3);
	}
}

// 使用调用方实际拟合、taper 后的 Stroke，避免仅证明手造持久化样本能往返。
int RunFinalizedTailPersistenceTests(const draw3::InkStroke& stroke)
{
	TestState state;
	TempDirectory temporary;
	UINK_CHECK(state, temporary.IsValid());
	if (!temporary.IsValid()) return state.failures;
	Draw3UInkCanvasSnapshot page;
	page.pageGuid = Guid("50000000-0000-4000-8000-00000000f031");
	page.pageNumber = 1;
	Draw3UInkStrokeSnapshot source;
	source.style.kind = Draw3UInkStrokeKind::Pen;
	source.style.fallbackRgb = stroke.Style().fallbackRgb;
	source.style.opacity = stroke.Style().opacity;
	source.style.texture = stroke.Style().texture;
	for (const auto& point : stroke.Points())
		source.points.push_back({ point.x, point.y, point.width });
	page.strokes.emplace_back(std::move(source));
	const auto exported = ExportDraw3PageToUInkV11(page);
	UINK_CHECK(state, exported.status == Draw3UInkExportStatus::Success && exported.canvas);
	if (!exported.canvas) return state.failures;
	const auto header = MakeDraw3UInkV11FileHeader(Guid("10000000-0000-4000-8000-00000000f031"));
	UINK_CHECK(state, header.status == Draw3UInkExportStatus::Success);
	if (header.status != Draw3UInkExportStatus::Success) return state.failures;
	const auto published = PublishUInkSnapshotFile(temporary.File(L"finalized-tail.uink"),
		header.header, header.headerExtension, *exported.canvas);
	UINK_CHECK(state, published.success && published.published && !published.publishedPath.empty());
	if (!published.success) return state.failures;
	const auto read = ReadUInkStoreFile(published.publishedPath);
	UINK_CHECK(state, read.status == UInkReadStatus::Complete && read.document.has_value());
	if (!read.document) return state.failures;
	UINK_CHECK(state, read.document->canvases.size() == 1 &&
		read.document->canvases[0].content.size() == 1);
	if (read.document->canvases.size() != 1 ||
		read.document->canvases[0].content.size() != 1) return state.failures;
	const auto* encodedInk = std::get_if<UInkInk>(&read.document->canvases[0].content[0]);
	UINK_CHECK(state, encodedInk != nullptr);
	if (!encodedInk) return state.failures;
	UINK_CHECK(state, !encodedInk->color.extended && encodedInk->effectiveTexture == 0);
	for (const auto& point : encodedInk->points) UINK_CHECK(state, !point.style);
	const auto imported = ImportUInkV11Page(read.document->canvases.front());
	UINK_CHECK(state, imported.status == Draw3UInkV11ImportStatus::Success && imported.page &&
		imported.page->strokes.size() == 1);
	if (!imported.page || imported.page->strokes.size() != 1) return state.failures;
	const auto& restored = imported.page->strokes.front();
	UINK_CHECK(state, restored.style.kind == Draw3UInkStrokeKind::Pen &&
		restored.style.fallbackRgb == stroke.Style().fallbackRgb &&
		restored.style.opacity == stroke.Style().opacity &&
		restored.style.texture == stroke.Style().texture &&
		restored.points.size() == stroke.Points().size());
	if (restored.points.size() != stroke.Points().size()) return state.failures;
	std::vector<StoredInkPoint> replayPoints;
	for (size_t i = 0; i < restored.points.size(); ++i)
	{
		UINK_CHECK(state, restored.points[i].x == stroke.Points()[i].x &&
			restored.points[i].y == stroke.Points()[i].y &&
			restored.points[i].width == stroke.Points()[i].width);
		replayPoints.push_back({restored.points[i].x, restored.points[i].y, restored.points[i].width});
	}
	const InkStroke replay(stroke.Style(), std::move(replayPoints));
	const auto originalFootprint = BuildStrokeTileFootprint(stroke);
	const auto replayFootprint = BuildStrokeTileFootprint(replay);
	UINK_CHECK(state, originalFootprint && replayFootprint);
	if (originalFootprint && replayFootprint)
	{
		UINK_CHECK(state, replayFootprint->undoTiles == originalFootprint->undoTiles &&
			replayFootprint->compositionTiles == originalFootprint->compositionTiles);
		UINK_CHECK(state, replayFootprint->pixelBounds.left == originalFootprint->pixelBounds.left &&
			replayFootprint->pixelBounds.top == originalFootprint->pixelBounds.top &&
			replayFootprint->pixelBounds.right == originalFootprint->pixelBounds.right &&
			replayFootprint->pixelBounds.bottom == originalFootprint->pixelBounds.bottom);
	}
	std::cout << "Finalized tail persistence failures: " << state.failures << std::endl;
	return state.failures;
}

int RunUInkTests()
{
	TestState state;
	TestOfficialFixtures(state);
	TestExactEncodingAndRichRoundTrip(state);
	TestMalformedAndRecovery(state);
	TestEncoderTopologyValidation(state);
	TestReadLimits(state);
	TestExtraWireSkipping(state);
	TestRegistryExtraScopeAndReplay(state);
	TestEncoderPayloadLengthBoundaries(state);
	TestV11WireAndRoundTrip(state);
	TestV11FieldFallbacks(state);
	TestV11ShapeObjectRecovery(state);
	TestV11GeometryRecovery(state);
	TestV11GeometryEncoding(state);
	TestV11PresetIntegerWidths(state);
	TestV11ShapeStyleColorWire(state);
	TestV11PresentationBinding(state);
	TestV11VersionAndStreamRecovery(state);
	TestV11CanvasOrdinals(state);
	TestV11IgnoredLayerMetadata(state);
	TestV11DiagnosticLocations(state);
	TestV11ResourceBudgets(state);
	TestV11BudgetRollback(state);
	TestV11RegistryBudgetRollback(state);
	TestV11ContentStorageCharge(state);
	TestV11ResourceRecovery(state);
	TestV11FileResourceRejection(state);
	TestV11RegistryCycles(state);
	TestV11RegistryTailCycles(state);
	TestDecoderDepthCeiling(state);
	TestDeclaredContainerReserve(state);
	TestPlainPointFloat32Decode(state);
	MeasureV11Decode(state);
	TestMediaFileRead(state);
	TestLatestVisibility(state);
	TestClearContent(state);
	TestDraw3PageExport(state);
	if (state.failures == 0)
		std::cout << "All UInk persistence tests passed." << std::endl;
	return state.failures;
}
