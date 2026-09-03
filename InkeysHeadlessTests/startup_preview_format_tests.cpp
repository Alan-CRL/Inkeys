#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

import Inkeys.UI.StartupPreview.Format;

namespace
{
	using namespace Inkeys::UI::StartupPreview;

	bool Expect(bool condition, const char* name)
	{
		if (!condition) std::cerr << "[StartupPreviewFormat] failed: " << name << '\n';
		return condition;
	}

	PreviewMetadata MakeMetadata()
	{
		PreviewMetadata metadata;
		metadata.layoutEpoch = 7;
		VisualSignatureInputs inputs;
		inputs.layoutEpoch = metadata.layoutEpoch;
		inputs.fixedButtonsA1 = { { "select", 44, 44, true } };
		inputs.extensionButtons = { { "pen", 44, 44, true } };
		inputs.fixedButtonsA2 = { { "more", 44, 44, true } };
		metadata.visualSignature = BuildVisualSignature(inputs);
		metadata.flags = PreviewFlagDiskCache;
		metadata.captureRevision = 42;
		metadata.width = 4;
		metadata.height = 3;
		metadata.captureDpiX = 96;
		metadata.captureDpiY = 96;
		metadata.monitorPixelWidth = 1920;
		metadata.monitorPixelHeight = 1080;
		metadata.monitorWorkWidth = 1920;
		metadata.monitorWorkHeight = 1040;
		metadata.windowOffsetX = 100;
		metadata.windowOffsetY = 200;
		metadata.anchorX = 2;
		metadata.anchorY = 1;
		metadata.progressLeft = 0;
		metadata.progressTop = 1;
		metadata.progressRight = 4;
		metadata.progressBottom = 3;
		return metadata;
	}

	VisualSignatureInputs MakeCanonicalVisualInputs()
	{
		VisualSignatureInputs inputs;
		inputs.fixedButtonsA1 = {
			{ "Inkeys.Bar.Select", 2, 2, true },
			{ "Inkeys.Bar.Draw", 2, 2, true },
			{ "Inkeys.Bar.Geometry", 2, 2, true },
			{ "Inkeys.Bar.Eraser", 2, 2, true },
			{ "Inkeys.Bar.Recall", 2, 2, true },
			{ "Inkeys.Bar.Clean", 2, 2, true },
		};
		inputs.extensionButtons = {
			{ "Inkeys.Bar.MoreBoundary", 2, 2, true },
			{ "Inkeys.Bar.Setting", 2, 2, true },
		};
		inputs.fixedButtonsA2 = {
			{ "Inkeys.Bar.Whiteboard", 2, 1, true },
			{ "Inkeys.Bar.Freeze", 2, 1, true },
			{ "Inkeys.Bar.EndShow", 2, 2, true },
		};
		inputs.runtimeState.mainBarRight = true;
		return inputs;
	}

	int ValidateCanonicalAsset(std::span<const std::uint8_t> bytes,
		const char* source)
	{
		const auto parsed = ParsePreview(bytes);
		std::uint32_t storedCrc = 0;
		(void)ReadLe32(bytes, 144, storedCrc);
		constexpr std::array<std::uint8_t, 32> expectedFileSha{
			0xf9, 0x59, 0x2d, 0x61, 0xcb, 0x77, 0xa2, 0x74,
			0x67, 0xd4, 0x84, 0x71, 0x48, 0xe1, 0xbc, 0x44,
			0x12, 0x39, 0x83, 0x73, 0xc2, 0x30, 0x56, 0x89,
			0x4e, 0x2d, 0x8f, 0xfb, 0x15, 0x7e, 0x32, 0x85,
		};
		const bool valid = parsed.state == CacheState::Valid
			&& parsed.error == PreviewFormatError::None
			&& parsed.metadata.layoutEpoch == 1
			&& parsed.metadata.flags == PreviewFlagEmbedded
			&& parsed.metadata.captureRevision == 0
			&& parsed.metadata.captureDpiX == 96
			&& parsed.metadata.captureDpiY == 96
			&& parsed.metadata.visualSignature
				== BuildVisualSignature(MakeCanonicalVisualInputs())
			&& parsed.metadata.width == 494 && parsed.metadata.height == 105
			&& parsed.metadata.stride == 1976
			&& parsed.metadata.payloadSize == 207480
			&& storedCrc == 0x801594d5u
			&& ComputeSha256(bytes) == expectedFileSha
			&& parsed.pixels.size() == parsed.metadata.payloadSize;
		if (!valid)
		{
			std::cerr << "[StartupPreviewFormat] invalid canonical asset: "
				<< source << " state=" << static_cast<int>(parsed.state)
				<< " error=" << static_cast<int>(parsed.error) << '\n';
			return 1;
		}
		std::cout << "PASS startup preview asset source=" << source
			<< " width=" << parsed.metadata.width
			<< " height=" << parsed.metadata.height
			<< " bytes=" << bytes.size() << '\n';
		return 0;
	}

	std::optional<std::vector<std::uint8_t>> ReadAssetFile(const char* path)
	{
		std::ifstream input(path, std::ios::binary | std::ios::ate);
		if (!input) return std::nullopt;
		const auto end = input.tellg();
		if (end <= 0 || static_cast<std::uint64_t>(end)
			> PreviewHeaderSize + PreviewMaximumPayloadSize) return std::nullopt;
		std::vector<std::uint8_t> bytes(static_cast<std::size_t>(end));
		input.seekg(0, std::ios::beg);
		input.read(reinterpret_cast<char*>(bytes.data()),
			static_cast<std::streamsize>(bytes.size()));
		return input ? std::optional{ std::move(bytes) } : std::nullopt;
	}

	void RefreshCrc(std::vector<std::uint8_t>& bytes)
	{
		(void)WriteLe32(bytes, 144, 0);
		(void)WriteLe32(bytes, 144, ComputePreviewCrc32(bytes));
	}
}

int ValidateStartupPreviewFile(const char* path)
{
	using namespace Inkeys::UI::StartupPreview;
	try
	{
		const auto bytes = ReadAssetFile(path);
		if (!bytes)
		{
			std::cerr << "[StartupPreviewFormat] cannot read bounded asset: "
				<< path << '\n';
			return 1;
		}
		return ValidateCanonicalAsset(*bytes, path);
	}
	catch (...)
	{
		std::cerr << "[StartupPreviewFormat] asset validation threw\n";
		return 1;
	}
}

int ValidateStartupPreviewResource(const char* executablePath,
	const char* sourcePath)
{
	using namespace Inkeys::UI::StartupPreview;
	HMODULE module = LoadLibraryExA(executablePath, nullptr,
		LOAD_LIBRARY_AS_DATAFILE | LOAD_LIBRARY_AS_IMAGE_RESOURCE);
	if (!module)
	{
		std::cerr << "[StartupPreviewFormat] cannot load executable resource\n";
		return 1;
	}
	HRSRC resource = FindResourceW(module, MAKEINTRESOURCEW(306),
		L"STARTUP_PREVIEW_BIN");
	const DWORD size = resource ? SizeofResource(module, resource) : 0;
	HGLOBAL loaded = resource ? LoadResource(module, resource) : nullptr;
	const auto* data = loaded
		? static_cast<const std::uint8_t*>(LockResource(loaded)) : nullptr;
	const auto sourceBytes = ReadAssetFile(sourcePath);
	const bool byteExact = data && size > 0 && sourceBytes
		&& sourceBytes->size() == size
		&& std::equal(sourceBytes->begin(), sourceBytes->end(), data);
	const int result = byteExact
		? ValidateCanonicalAsset(std::span(data, static_cast<std::size_t>(size)),
			executablePath) : 1;
	if (!data || size == 0)
		std::cerr << "[StartupPreviewFormat] embedded resource 306 is missing\n";
	else if (!byteExact)
		std::cerr << "[StartupPreviewFormat] resource differs from source BIN\n";
	FreeLibrary(module);
	return result;
}

int RunStartupPreviewFormatTests()
{
	using namespace Inkeys::UI::StartupPreview;
	int failures = 0;
	constexpr std::string_view vector = "123456789";
	if (!Expect(ComputeIeeeCrc32(std::span(
		reinterpret_cast<const std::uint8_t*>(vector.data()), vector.size()))
		== 0xcbf43926u, "IEEE CRC-32 standard vector")) ++failures;

	auto metadata = MakeMetadata();
	std::vector<std::uint8_t> pixels(metadata.width * metadata.height * 4);
	for (std::size_t index = 0; index < pixels.size(); ++index)
		pixels[index] = static_cast<std::uint8_t>(index * 13);
	const auto serialized = SerializePreview(metadata, pixels);
	if (!Expect(serialized.size() == PreviewHeaderSize + pixels.size()
		&& serialized[0] == 'I' && serialized[7] == 0,
		"serializer writes exact v1 container")) ++failures;
	std::uint16_t version = 0;
	std::uint32_t width = 0;
	std::uint64_t payloadSize = 0;
	if (!Expect(ReadLe16(serialized, 8, version) && version == 1
		&& ReadLe32(serialized, 64, width) && width == metadata.width
		&& ReadLe64(serialized, 80, payloadSize) && payloadSize == pixels.size(),
		"wire fields are little endian at fixed offsets")) ++failures;
	const auto compatibility = MakeCompatibility(metadata);
	const auto parsed = ParsePreview(serialized, &compatibility);
	if (!Expect(parsed.state == CacheState::Valid
		&& parsed.error == PreviewFormatError::None
		&& parsed.pixels == pixels
		&& parsed.metadata.captureRevision == 42,
		"valid container roundtrips metadata and pixels")) ++failures;

	if (!Expect(MissingPreview().state == CacheState::Missing
		&& ParsePreview({}).state == CacheState::Corrupt,
		"missing path and zero-byte file have distinct classification")) ++failures;
	for (std::size_t size = 1; size < serialized.size(); ++size)
	{
		const auto truncated = ParsePreview(std::span(serialized).first(size));
		if (truncated.state != CacheState::Corrupt)
		{
			++failures;
			std::cerr << "[StartupPreviewFormat] failed: truncation size="
				<< size << '\n';
			break;
		}
	}

	auto corrupt = serialized;
	corrupt.back() ^= 0x80;
	if (!Expect(ParsePreview(corrupt).error == PreviewFormatError::Crc,
		"payload mutation is rejected by CRC")) ++failures;

	auto unsupported = serialized;
	(void)WriteLe16(unsupported, 8, 2);
	RefreshCrc(unsupported);
	if (!Expect(ParsePreview(unsupported).state == CacheState::Incompatible,
		"unsupported version requires a valid common envelope")) ++failures;
	unsupported.back() ^= 1;
	if (!Expect(ParsePreview(unsupported).state == CacheState::Corrupt,
		"damaged unsupported version is not guessed as compatible")) ++failures;

	auto incompatibleKey = compatibility;
	++incompatibleKey.layoutEpoch;
	if (!Expect(ParsePreview(serialized, &incompatibleKey).state
		== CacheState::Incompatible,
		"epoch mismatch is incompatible after structural validation")) ++failures;
	incompatibleKey = compatibility;
	++incompatibleKey.captureDpiX;
	if (!Expect(ParsePreview(serialized, &incompatibleKey).state
		== CacheState::Incompatible,
		"DPI mismatch is incompatible")) ++failures;

	auto illegalRect = serialized;
	(void)WriteLe32(illegalRect, 136, 5);
	RefreshCrc(illegalRect);
	if (!Expect(ParsePreview(illegalRect).error == PreviewFormatError::Geometry,
		"out-of-bounds progress rectangle is corrupt")) ++failures;
	auto illegalAnchor = serialized;
	(void)WriteLe32(illegalAnchor, 120, 4);
	RefreshCrc(illegalAnchor);
	if (!Expect(ParsePreview(illegalAnchor).error == PreviewFormatError::Geometry,
		"anchor must remain inside the payload")) ++failures;
	auto illegalOffset = serialized;
	(void)WriteLe32(illegalOffset, 112, 1918);
	RefreshCrc(illegalOffset);
	if (!Expect(ParsePreview(illegalOffset).error == PreviewFormatError::Geometry,
		"window geometry must remain inside monitor bounds")) ++failures;
	auto invalidStride = serialized;
	(void)WriteLe32(invalidStride, 72, 15);
	RefreshCrc(invalidStride);
	if (!Expect(ParsePreview(invalidStride).error == PreviewFormatError::Stride,
		"v1 stride must equal width times four")) ++failures;
	auto reserved = serialized;
	reserved[159] = 1;
	RefreshCrc(reserved);
	if (!Expect(ParsePreview(reserved).error == PreviewFormatError::Reserved,
		"reserved bytes must remain zero")) ++failures;

	auto huge = metadata;
	huge.width = PreviewMaximumDimension;
	huge.height = PreviewMaximumDimension;
	if (!Expect(SerializePreview(huge, pixels).empty(),
		"serializer rejects payload geometry above 64 MiB")) ++failures;

	VisualSignatureInputs signatureInput;
	signatureInput.fixedButtonsA1 = { { "select", 44, 44, true } };
	const auto firstSignature = BuildVisualSignature(signatureInput);
	const auto repeatedSignature = BuildVisualSignature(signatureInput);
	signatureInput.fixedButtonsA1[0].visible = false;
	const auto changedSignature = BuildVisualSignature(signatureInput);
	constexpr std::array<std::uint8_t, 32> expectedSignature{
		0x1e, 0x44, 0x26, 0x5b, 0x94, 0x4d, 0xbf, 0xc4,
		0xf2, 0xfa, 0x9f, 0x5d, 0x36, 0xc5, 0x4e, 0x3d,
		0xc8, 0xe0, 0x41, 0xf7, 0xdc, 0x35, 0x4d, 0x1f,
		0x97, 0x48, 0x54, 0x4d, 0xd1, 0x42, 0x56, 0xa7,
	};
	if (!Expect(firstSignature == expectedSignature
		&& firstSignature == repeatedSignature
		&& firstSignature != changedSignature
		&& std::any_of(firstSignature.begin(), firstSignature.end(),
			[](std::uint8_t byte) { return byte != 0; }),
		"visual signature matches canonical SHA-256 and changes with input")) ++failures;
	VisualSignatureInputs runtimeSignatureInput;
	const auto defaultRuntimeSignature = BuildVisualSignature(runtimeSignatureInput);
	runtimeSignatureInput.edgeLightingEnabled = false;
	const auto edgeSignature = BuildVisualSignature(runtimeSignatureInput);
	runtimeSignatureInput.edgeLightingEnabled = true;
	runtimeSignatureInput.runtimeState.selectedMode = 1;
	runtimeSignatureInput.runtimeState.toolColorRgba = 0xff336699u;
	const auto toolSignature = BuildVisualSignature(runtimeSignatureInput);
	if (!Expect(defaultRuntimeSignature != edgeSignature
		&& defaultRuntimeSignature != toolSignature && edgeSignature != toolSignature,
		"visual signature covers configured lighting and live persistent state")) ++failures;

	auto oversizedWidth = serialized;
	(void)WriteLe32(oversizedWidth, 64, PreviewMaximumDimension + 1);
	RefreshCrc(oversizedWidth);
	if (!Expect(ParsePreview(oversizedWidth).error == PreviewFormatError::Dimension,
		"width above 8192 is rejected before multiplication")) ++failures;
	auto oversizedHeight = serialized;
	(void)WriteLe32(oversizedHeight, 68, PreviewMaximumDimension + 1);
	RefreshCrc(oversizedHeight);
	if (!Expect(ParsePreview(oversizedHeight).error == PreviewFormatError::Dimension,
		"height above 8192 is rejected before multiplication")) ++failures;
	auto oversizedPayload = serialized;
	(void)WriteLe64(oversizedPayload, 80, PreviewMaximumPayloadSize + 1);
	RefreshCrc(oversizedPayload);
	if (!Expect(ParsePreview(oversizedPayload).error
		== PreviewFormatError::PayloadSize,
		"payload above 64 MiB is rejected before allocation")) ++failures;
	auto multiplicationAttack = serialized;
	(void)WriteLe32(multiplicationAttack, 64, 0xffffffffu);
	(void)WriteLe32(multiplicationAttack, 68, 0xffffffffu);
	(void)WriteLe32(multiplicationAttack, 72, 0xffffffffu);
	(void)WriteLe64(multiplicationAttack, 80, 0xffffffffffffffffull);
	RefreshCrc(multiplicationAttack);
	if (!Expect(ParsePreview(multiplicationAttack).state == CacheState::Corrupt,
		"hostile dimensions and products cannot overflow validation")) ++failures;
	auto trailing = serialized;
	trailing.push_back(0);
	if (!Expect(ParsePreview(trailing).error == PreviewFormatError::FileSize,
		"trailing bytes violate exact file length")) ++failures;
	auto shortProduct = std::vector<std::uint8_t>(
		serialized.begin(), serialized.end() - 4);
	(void)WriteLe64(shortProduct, 80, pixels.size() - 4);
	RefreshCrc(shortProduct);
	if (!Expect(ParsePreview(shortProduct).error == PreviewFormatError::PayloadSize,
		"stride-height product must equal payload length")) ++failures;
	return failures;
}
