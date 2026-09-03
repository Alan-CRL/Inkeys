module;

#include <algorithm>
#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <span>
#include <string>
#include <string_view>
#include <vector>

module Inkeys.UI.StartupPreview.Format;

namespace Inkeys::UI::StartupPreview
{
	namespace
	{
		constexpr std::array<std::uint8_t, 8> Magic{
			'I', 'K', 'S', 'P', 'R', 'V', 'W', 0,
		};
		constexpr std::size_t CrcOffset = 144;

		[[nodiscard]] constexpr std::uint32_t RotateRight(
			std::uint32_t value, unsigned shift) noexcept
		{
			return std::rotr(value, static_cast<int>(shift));
		}

		class Sha256 final
		{
		public:
			void Update(std::span<const std::uint8_t> bytes) noexcept
			{
				for (const auto byte : bytes)
				{
					buffer_[bufferSize_++] = byte;
					bitCount_ += 8;
					if (bufferSize_ == buffer_.size())
					{
						Transform(buffer_);
						bufferSize_ = 0;
					}
				}
			}

			[[nodiscard]] std::array<std::uint8_t, 32> Finish() noexcept
			{
				const auto originalBitCount = bitCount_;
				buffer_[bufferSize_++] = 0x80;
				if (bufferSize_ > 56)
				{
					while (bufferSize_ < 64) buffer_[bufferSize_++] = 0;
					Transform(buffer_);
					bufferSize_ = 0;
				}
				while (bufferSize_ < 56) buffer_[bufferSize_++] = 0;
				for (int shift = 56; shift >= 0; shift -= 8)
					buffer_[bufferSize_++] = static_cast<std::uint8_t>(
						originalBitCount >> shift);
				Transform(buffer_);

				std::array<std::uint8_t, 32> result{};
				for (std::size_t index = 0; index < state_.size(); ++index)
				{
					result[index * 4] = static_cast<std::uint8_t>(state_[index] >> 24);
					result[index * 4 + 1] = static_cast<std::uint8_t>(state_[index] >> 16);
					result[index * 4 + 2] = static_cast<std::uint8_t>(state_[index] >> 8);
					result[index * 4 + 3] = static_cast<std::uint8_t>(state_[index]);
				}
				return result;
			}

		private:
			void Transform(const std::array<std::uint8_t, 64>& block) noexcept
			{
				static constexpr std::array<std::uint32_t, 64> constants{
					0x428a2f98u, 0x71374491u, 0xb5c0fbcfu, 0xe9b5dba5u,
					0x3956c25bu, 0x59f111f1u, 0x923f82a4u, 0xab1c5ed5u,
					0xd807aa98u, 0x12835b01u, 0x243185beu, 0x550c7dc3u,
					0x72be5d74u, 0x80deb1feu, 0x9bdc06a7u, 0xc19bf174u,
					0xe49b69c1u, 0xefbe4786u, 0x0fc19dc6u, 0x240ca1ccu,
					0x2de92c6fu, 0x4a7484aau, 0x5cb0a9dcu, 0x76f988dau,
					0x983e5152u, 0xa831c66du, 0xb00327c8u, 0xbf597fc7u,
					0xc6e00bf3u, 0xd5a79147u, 0x06ca6351u, 0x14292967u,
					0x27b70a85u, 0x2e1b2138u, 0x4d2c6dfcu, 0x53380d13u,
					0x650a7354u, 0x766a0abbu, 0x81c2c92eu, 0x92722c85u,
					0xa2bfe8a1u, 0xa81a664bu, 0xc24b8b70u, 0xc76c51a3u,
					0xd192e819u, 0xd6990624u, 0xf40e3585u, 0x106aa070u,
					0x19a4c116u, 0x1e376c08u, 0x2748774cu, 0x34b0bcb5u,
					0x391c0cb3u, 0x4ed8aa4au, 0x5b9cca4fu, 0x682e6ff3u,
					0x748f82eeu, 0x78a5636fu, 0x84c87814u, 0x8cc70208u,
					0x90befffau, 0xa4506cebu, 0xbef9a3f7u, 0xc67178f2u,
				};
				std::array<std::uint32_t, 64> words{};
				for (std::size_t index = 0; index < 16; ++index)
				{
					const auto offset = index * 4;
					words[index] = (std::uint32_t{ block[offset] } << 24)
						| (std::uint32_t{ block[offset + 1] } << 16)
						| (std::uint32_t{ block[offset + 2] } << 8)
						| std::uint32_t{ block[offset + 3] };
				}
				for (std::size_t index = 16; index < words.size(); ++index)
				{
					const auto s0 = RotateRight(words[index - 15], 7)
						^ RotateRight(words[index - 15], 18)
						^ (words[index - 15] >> 3);
					const auto s1 = RotateRight(words[index - 2], 17)
						^ RotateRight(words[index - 2], 19)
						^ (words[index - 2] >> 10);
					words[index] = words[index - 16] + s0
						+ words[index - 7] + s1;
				}

				auto [a, b, c, d, e, f, g, h] = state_;
				for (std::size_t index = 0; index < words.size(); ++index)
				{
					const auto sigma1 = RotateRight(e, 6) ^ RotateRight(e, 11)
						^ RotateRight(e, 25);
					const auto choose = (e & f) ^ (~e & g);
					const auto temp1 = h + sigma1 + choose
						+ constants[index] + words[index];
					const auto sigma0 = RotateRight(a, 2) ^ RotateRight(a, 13)
						^ RotateRight(a, 22);
					const auto majority = (a & b) ^ (a & c) ^ (b & c);
					const auto temp2 = sigma0 + majority;
					h = g; g = f; f = e; e = d + temp1;
					d = c; c = b; b = a; a = temp1 + temp2;
				}
				state_[0] += a; state_[1] += b; state_[2] += c; state_[3] += d;
				state_[4] += e; state_[5] += f; state_[6] += g; state_[7] += h;
			}

			std::array<std::uint32_t, 8> state_{
				0x6a09e667u, 0xbb67ae85u, 0x3c6ef372u, 0xa54ff53au,
				0x510e527fu, 0x9b05688cu, 0x1f83d9abu, 0x5be0cd19u,
			};
			std::array<std::uint8_t, 64> buffer_{};
			std::size_t bufferSize_ = 0;
			std::uint64_t bitCount_ = 0;
		};

		void AppendLe32(std::vector<std::uint8_t>& bytes, std::uint32_t value)
		{
			bytes.push_back(static_cast<std::uint8_t>(value));
			bytes.push_back(static_cast<std::uint8_t>(value >> 8));
			bytes.push_back(static_cast<std::uint8_t>(value >> 16));
			bytes.push_back(static_cast<std::uint8_t>(value >> 24));
		}

		void AppendString(std::vector<std::uint8_t>& bytes, std::string_view value)
		{
			AppendLe32(bytes, static_cast<std::uint32_t>(value.size()));
			bytes.insert(bytes.end(), value.begin(), value.end());
		}

		void AppendButtons(std::vector<std::uint8_t>& bytes,
			std::span<const VisualButton> buttons)
		{
			AppendLe32(bytes, static_cast<std::uint32_t>(buttons.size()));
			for (const auto& button : buttons)
			{
				AppendString(bytes, button.id);
				AppendLe32(bytes, button.widthDip);
				AppendLe32(bytes, button.heightDip);
				AppendLe32(bytes, button.visible ? 1u : 0u);
			}
		}

		[[nodiscard]] bool AdditionFits(std::int32_t value,
			std::uint32_t extent) noexcept
		{
			const auto sum = static_cast<std::int64_t>(value)
				+ static_cast<std::int64_t>(extent);
			return sum >= (std::numeric_limits<std::int32_t>::min)()
				&& sum <= (std::numeric_limits<std::int32_t>::max)();
		}

		[[nodiscard]] PreviewFormatError ValidateMetadata(
			const PreviewMetadata& metadata, std::size_t actualPayloadSize) noexcept
		{
			if (metadata.pixelFormat != PreviewPixelFormat)
				return PreviewFormatError::PixelFormat;
			const auto allowedFlags = PreviewFlagEmbedded | PreviewFlagDiskCache;
			if ((metadata.flags & ~allowedFlags) != 0
				|| metadata.flags == 0 || metadata.flags == allowedFlags)
				return PreviewFormatError::Flags;
			if (metadata.width == 0 || metadata.height == 0
				|| metadata.width > PreviewMaximumDimension
				|| metadata.height > PreviewMaximumDimension)
				return PreviewFormatError::Dimension;
			const auto expectedStride = static_cast<std::uint64_t>(metadata.width) * 4;
			if (expectedStride > (std::numeric_limits<std::uint32_t>::max)()
				|| metadata.stride != expectedStride)
				return PreviewFormatError::Stride;
			const auto expectedPayload = expectedStride * metadata.height;
			if (expectedPayload > PreviewMaximumPayloadSize
				|| metadata.payloadSize != expectedPayload
				|| metadata.payloadSize != actualPayloadSize)
				return PreviewFormatError::PayloadSize;
			if (metadata.captureDpiX == 0 || metadata.captureDpiY == 0
				|| metadata.captureDpiX > 9600 || metadata.captureDpiY > 9600)
				return PreviewFormatError::Dpi;
			if (metadata.monitorPixelWidth == 0 || metadata.monitorPixelHeight == 0
				|| metadata.monitorWorkWidth == 0 || metadata.monitorWorkHeight == 0
				|| metadata.monitorWorkWidth > metadata.monitorPixelWidth
				|| metadata.monitorWorkHeight > metadata.monitorPixelHeight
				|| metadata.progressLeft < 0 || metadata.progressTop < 0
				|| metadata.progressRight <= metadata.progressLeft
				|| metadata.progressBottom <= metadata.progressTop
				|| static_cast<std::uint32_t>(metadata.progressRight) > metadata.width
				|| static_cast<std::uint32_t>(metadata.progressBottom) > metadata.height
				|| metadata.anchorX < 0 || metadata.anchorY < 0
				|| static_cast<std::uint32_t>(metadata.anchorX) >= metadata.width
				|| static_cast<std::uint32_t>(metadata.anchorY) >= metadata.height
				|| metadata.windowOffsetX < 0 || metadata.windowOffsetY < 0
				|| static_cast<std::uint64_t>(metadata.windowOffsetX)
					+ metadata.width > metadata.monitorPixelWidth
				|| static_cast<std::uint64_t>(metadata.windowOffsetY)
					+ metadata.height > metadata.monitorPixelHeight
				|| !AdditionFits(metadata.windowOffsetX, metadata.width)
				|| !AdditionFits(metadata.windowOffsetY, metadata.height)
				|| !AdditionFits(metadata.anchorX, metadata.width)
				|| !AdditionFits(metadata.anchorY, metadata.height))
				return PreviewFormatError::Geometry;
			return PreviewFormatError::None;
		}

		[[nodiscard]] std::int32_t ToSigned(std::uint32_t value) noexcept
		{
			return std::bit_cast<std::int32_t>(value);
		}
	}

	bool ReadLe16(std::span<const std::uint8_t> bytes, std::size_t offset,
		std::uint16_t& value) noexcept
	{
		if (offset > bytes.size() || bytes.size() - offset < 2) return false;
		value = static_cast<std::uint16_t>(bytes[offset])
			| static_cast<std::uint16_t>(bytes[offset + 1] << 8);
		return true;
	}

	bool ReadLe32(std::span<const std::uint8_t> bytes, std::size_t offset,
		std::uint32_t& value) noexcept
	{
		if (offset > bytes.size() || bytes.size() - offset < 4) return false;
		value = std::uint32_t{ bytes[offset] }
			| (std::uint32_t{ bytes[offset + 1] } << 8)
			| (std::uint32_t{ bytes[offset + 2] } << 16)
			| (std::uint32_t{ bytes[offset + 3] } << 24);
		return true;
	}

	bool ReadLe64(std::span<const std::uint8_t> bytes, std::size_t offset,
		std::uint64_t& value) noexcept
	{
		if (offset > bytes.size() || bytes.size() - offset < 8) return false;
		value = 0;
		for (unsigned index = 0; index < 8; ++index)
			value |= std::uint64_t{ bytes[offset + index] } << (index * 8);
		return true;
	}

	bool WriteLe16(std::span<std::uint8_t> bytes, std::size_t offset,
		std::uint16_t value) noexcept
	{
		if (offset > bytes.size() || bytes.size() - offset < 2) return false;
		bytes[offset] = static_cast<std::uint8_t>(value);
		bytes[offset + 1] = static_cast<std::uint8_t>(value >> 8);
		return true;
	}

	bool WriteLe32(std::span<std::uint8_t> bytes, std::size_t offset,
		std::uint32_t value) noexcept
	{
		if (offset > bytes.size() || bytes.size() - offset < 4) return false;
		for (unsigned index = 0; index < 4; ++index)
			bytes[offset + index] = static_cast<std::uint8_t>(value >> (index * 8));
		return true;
	}

	bool WriteLe64(std::span<std::uint8_t> bytes, std::size_t offset,
		std::uint64_t value) noexcept
	{
		if (offset > bytes.size() || bytes.size() - offset < 8) return false;
		for (unsigned index = 0; index < 8; ++index)
			bytes[offset + index] = static_cast<std::uint8_t>(value >> (index * 8));
		return true;
	}

	std::uint32_t ComputeIeeeCrc32(std::span<const std::uint8_t> bytes) noexcept
	{
		std::uint32_t crc = 0xffffffffu;
		for (const auto byte : bytes)
		{
			crc ^= byte;
			for (int bit = 0; bit < 8; ++bit)
				crc = (crc >> 1) ^ (0xedb88320u &
					static_cast<std::uint32_t>(-static_cast<std::int32_t>(crc & 1)));
		}
		return ~crc;
	}

	std::uint32_t ComputePreviewCrc32(std::span<const std::uint8_t> bytes) noexcept
	{
		std::uint32_t crc = 0xffffffffu;
		for (std::size_t index = 0; index < bytes.size(); ++index)
		{
			const auto byte = index >= CrcOffset && index < CrcOffset + 4
				? std::uint8_t{} : bytes[index];
			crc ^= byte;
			for (int bit = 0; bit < 8; ++bit)
				crc = (crc >> 1) ^ (0xedb88320u &
					static_cast<std::uint32_t>(-static_cast<std::int32_t>(crc & 1)));
		}
		return ~crc;
	}

	std::array<std::uint8_t, 32> ComputeSha256(
		std::span<const std::uint8_t> bytes) noexcept
	{
		Sha256 sha;
		sha.Update(bytes);
		return sha.Finish();
	}

	std::array<std::uint8_t, 32> BuildVisualSignature(
		const VisualSignatureInputs& inputs) noexcept
	{
		try
		{
			std::vector<std::uint8_t> canonical;
			canonical.reserve(256);
			AppendString(canonical, "Inkeys.StartupPreview.Visual.v1");
			AppendLe32(canonical, inputs.layoutEpoch);
			AppendString(canonical, inputs.language);
			AppendString(canonical, inputs.theme);
			AppendLe32(canonical, inputs.zoomPermille);
			AppendLe32(canonical, inputs.expanded ? 1u : 0u);
			AppendLe32(canonical, std::bit_cast<std::uint32_t>(inputs.dock));
			AppendLe32(canonical, std::bit_cast<std::uint32_t>(inputs.position));
			AppendLe32(canonical, inputs.visualResourceVersion);
			AppendLe32(canonical, inputs.pixelFormat);
			AppendLe32(canonical, inputs.alphaRuleVersion);
			AppendLe32(canonical, inputs.edgeLightingEnabled ? 1u : 0u);
			AppendLe32(canonical, inputs.dynamicEdgeLightingEnabled ? 1u : 0u);
			AppendLe32(canonical, inputs.debugOverlayEnabled ? 1u : 0u);
			const auto& runtime = inputs.runtimeState;
			AppendLe32(canonical, std::bit_cast<std::uint32_t>(runtime.selectedMode));
			AppendLe32(canonical, std::bit_cast<std::uint32_t>(runtime.selectedPenMode));
			AppendLe32(canonical, std::bit_cast<std::uint32_t>(runtime.selectedShapeMode));
			AppendLe32(canonical, runtime.toolColorRgba);
			AppendLe32(canonical, std::bit_cast<std::uint32_t>(runtime.toolWidthMilli));
			AppendLe32(canonical, runtime.folded ? 1u : 0u);
			AppendLe32(canonical, runtime.drawAttributeVisible ? 1u : 0u);
			AppendLe32(canonical, runtime.geometryAttributeVisible ? 1u : 0u);
			AppendLe32(canonical, runtime.morePanelVisible ? 1u : 0u);
			AppendLe32(canonical, runtime.mainBarRight ? 1u : 0u);
			AppendLe32(canonical, runtime.primaryBarBelow ? 1u : 0u);
			AppendLe32(canonical, std::bit_cast<std::uint32_t>(runtime.bottomDockMode));
			AppendLe32(canonical,
				std::bit_cast<std::uint32_t>(runtime.bottomDockCenterMode));
			AppendLe32(canonical, runtime.whiteboardActive ? 1u : 0u);
			AppendLe32(canonical, runtime.pptPresentationActive ? 1u : 0u);
			AppendLe32(canonical, runtime.currentPageHasContent ? 1u : 0u);
			AppendButtons(canonical, inputs.fixedButtonsA1);
			AppendButtons(canonical, inputs.extensionButtons);
			AppendButtons(canonical, inputs.fixedButtonsA2);
			return ComputeSha256(canonical);
		}
		catch (...)
		{
			return {};
		}
	}

	PreviewCompatibility MakeCompatibility(const PreviewMetadata& metadata) noexcept
	{
		return {
			metadata.layoutEpoch, metadata.visualSignature,
			metadata.captureDpiX, metadata.captureDpiY,
			metadata.monitorPixelWidth, metadata.monitorPixelHeight,
			metadata.monitorWorkWidth, metadata.monitorWorkHeight,
			metadata.windowOffsetX, metadata.windowOffsetY,
			metadata.anchorX, metadata.anchorY,
			metadata.progressLeft, metadata.progressTop,
			metadata.progressRight, metadata.progressBottom,
		};
	}

	bool IsCompatible(const PreviewMetadata& metadata,
		const PreviewCompatibility& compatibility) noexcept
	{
		return metadata.layoutEpoch == compatibility.layoutEpoch
			&& metadata.visualSignature == compatibility.visualSignature
			&& metadata.captureDpiX == compatibility.captureDpiX
			&& metadata.captureDpiY == compatibility.captureDpiY
			&& metadata.monitorPixelWidth == compatibility.monitorPixelWidth
			&& metadata.monitorPixelHeight == compatibility.monitorPixelHeight
			&& metadata.monitorWorkWidth == compatibility.monitorWorkWidth
			&& metadata.monitorWorkHeight == compatibility.monitorWorkHeight
			&& metadata.windowOffsetX == compatibility.windowOffsetX
			&& metadata.windowOffsetY == compatibility.windowOffsetY
			&& metadata.anchorX == compatibility.anchorX
			&& metadata.anchorY == compatibility.anchorY
			&& metadata.progressLeft == compatibility.progressLeft
			&& metadata.progressTop == compatibility.progressTop
			&& metadata.progressRight == compatibility.progressRight
			&& metadata.progressBottom == compatibility.progressBottom;
	}

	bool TryReadPreviewMetadata(std::span<const std::uint8_t> header,
		std::uint64_t fileSize, PreviewMetadata& metadata,
		PreviewFormatError& error) noexcept
	{
		error = PreviewFormatError::None;
		metadata = {};
		if (fileSize < PreviewHeaderSize || header.size() < PreviewHeaderSize)
		{
			error = PreviewFormatError::TooSmall;
			return false;
		}
		if (fileSize > PreviewHeaderSize + PreviewMaximumPayloadSize)
		{
			error = PreviewFormatError::TooLarge;
			return false;
		}
		if (!std::equal(Magic.begin(), Magic.end(), header.begin()))
		{
			error = PreviewFormatError::Magic;
			return false;
		}
		std::uint16_t version = 0;
		std::uint16_t headerSize = 0;
		(void)ReadLe16(header, 8, version);
		(void)ReadLe16(header, 10, headerSize);
		if (version != PreviewFormatVersion)
		{
			error = PreviewFormatError::UnsupportedVersion;
			return false;
		}
		if (headerSize != PreviewHeaderSize)
		{
			error = PreviewFormatError::HeaderSize;
			return false;
		}

		std::uint32_t signedValue = 0;
		(void)ReadLe32(header, 12, metadata.layoutEpoch);
		(void)ReadLe32(header, 16, metadata.pixelFormat);
		(void)ReadLe32(header, 20, metadata.flags);
		(void)ReadLe64(header, 24, metadata.captureRevision);
		std::copy_n(header.begin() + 32, metadata.visualSignature.size(),
			metadata.visualSignature.begin());
		(void)ReadLe32(header, 64, metadata.width);
		(void)ReadLe32(header, 68, metadata.height);
		(void)ReadLe32(header, 72, metadata.stride);
		if (header[76] || header[77] || header[78] || header[79]
			|| std::any_of(header.begin() + 148, header.begin() + 160,
				[](std::uint8_t value) { return value != 0; }))
		{
			error = PreviewFormatError::Reserved;
			return false;
		}
		(void)ReadLe64(header, 80, metadata.payloadSize);
		(void)ReadLe32(header, 88, metadata.captureDpiX);
		(void)ReadLe32(header, 92, metadata.captureDpiY);
		(void)ReadLe32(header, 96, metadata.monitorPixelWidth);
		(void)ReadLe32(header, 100, metadata.monitorPixelHeight);
		(void)ReadLe32(header, 104, metadata.monitorWorkWidth);
		(void)ReadLe32(header, 108, metadata.monitorWorkHeight);
		(void)ReadLe32(header, 112, signedValue); metadata.windowOffsetX = ToSigned(signedValue);
		(void)ReadLe32(header, 116, signedValue); metadata.windowOffsetY = ToSigned(signedValue);
		(void)ReadLe32(header, 120, signedValue); metadata.anchorX = ToSigned(signedValue);
		(void)ReadLe32(header, 124, signedValue); metadata.anchorY = ToSigned(signedValue);
		(void)ReadLe32(header, 128, signedValue); metadata.progressLeft = ToSigned(signedValue);
		(void)ReadLe32(header, 132, signedValue); metadata.progressTop = ToSigned(signedValue);
		(void)ReadLe32(header, 136, signedValue); metadata.progressRight = ToSigned(signedValue);
		(void)ReadLe32(header, 140, signedValue); metadata.progressBottom = ToSigned(signedValue);

		if (metadata.payloadSize > PreviewMaximumPayloadSize
			|| PreviewHeaderSize + metadata.payloadSize != fileSize)
		{
			error = metadata.payloadSize > PreviewMaximumPayloadSize
				? PreviewFormatError::PayloadSize : PreviewFormatError::FileSize;
			return false;
		}
		error = ValidateMetadata(metadata,
			static_cast<std::size_t>(metadata.payloadSize));
		return error == PreviewFormatError::None;
	}

	std::vector<std::uint8_t> SerializePreview(PreviewMetadata metadata,
		std::span<const std::uint8_t> pixels)
	{
		metadata.stride = metadata.width <= PreviewMaximumDimension
			? metadata.width * 4 : 0;
		metadata.payloadSize = pixels.size();
		if (ValidateMetadata(metadata, pixels.size()) != PreviewFormatError::None)
			return {};
		std::vector<std::uint8_t> bytes(PreviewHeaderSize + pixels.size());
		std::copy(Magic.begin(), Magic.end(), bytes.begin());
		(void)WriteLe16(bytes, 8, PreviewFormatVersion);
		(void)WriteLe16(bytes, 10, static_cast<std::uint16_t>(PreviewHeaderSize));
		(void)WriteLe32(bytes, 12, metadata.layoutEpoch);
		(void)WriteLe32(bytes, 16, metadata.pixelFormat);
		(void)WriteLe32(bytes, 20, metadata.flags);
		(void)WriteLe64(bytes, 24, metadata.captureRevision);
		std::copy(metadata.visualSignature.begin(), metadata.visualSignature.end(),
			bytes.begin() + 32);
		(void)WriteLe32(bytes, 64, metadata.width);
		(void)WriteLe32(bytes, 68, metadata.height);
		(void)WriteLe32(bytes, 72, metadata.stride);
		(void)WriteLe64(bytes, 80, metadata.payloadSize);
		(void)WriteLe32(bytes, 88, metadata.captureDpiX);
		(void)WriteLe32(bytes, 92, metadata.captureDpiY);
		(void)WriteLe32(bytes, 96, metadata.monitorPixelWidth);
		(void)WriteLe32(bytes, 100, metadata.monitorPixelHeight);
		(void)WriteLe32(bytes, 104, metadata.monitorWorkWidth);
		(void)WriteLe32(bytes, 108, metadata.monitorWorkHeight);
		(void)WriteLe32(bytes, 112, std::bit_cast<std::uint32_t>(metadata.windowOffsetX));
		(void)WriteLe32(bytes, 116, std::bit_cast<std::uint32_t>(metadata.windowOffsetY));
		(void)WriteLe32(bytes, 120, std::bit_cast<std::uint32_t>(metadata.anchorX));
		(void)WriteLe32(bytes, 124, std::bit_cast<std::uint32_t>(metadata.anchorY));
		(void)WriteLe32(bytes, 128, std::bit_cast<std::uint32_t>(metadata.progressLeft));
		(void)WriteLe32(bytes, 132, std::bit_cast<std::uint32_t>(metadata.progressTop));
		(void)WriteLe32(bytes, 136, std::bit_cast<std::uint32_t>(metadata.progressRight));
		(void)WriteLe32(bytes, 140, std::bit_cast<std::uint32_t>(metadata.progressBottom));
		std::copy(pixels.begin(), pixels.end(), bytes.begin() + PreviewHeaderSize);
		(void)WriteLe32(bytes, CrcOffset, ComputePreviewCrc32(bytes));
		return bytes;
	}

	PreviewParseResult MissingPreview() noexcept
	{
		PreviewParseResult result;
		result.state = CacheState::Missing;
		result.error = PreviewFormatError::Missing;
		return result;
	}

	PreviewParseResult ParsePreview(std::span<const std::uint8_t> bytes,
		const PreviewCompatibility* compatibility)
	{
		PreviewParseResult result;
		if (bytes.empty())
		{
			result.error = PreviewFormatError::TooSmall;
			return result;
		}
		if (bytes.size() < PreviewHeaderSize)
		{
			result.error = PreviewFormatError::TooSmall;
			return result;
		}
		if (bytes.size() > PreviewHeaderSize + PreviewMaximumPayloadSize)
		{
			result.error = PreviewFormatError::TooLarge;
			return result;
		}
		if (!std::equal(Magic.begin(), Magic.end(), bytes.begin()))
		{
			result.error = PreviewFormatError::Magic;
			return result;
		}
		std::uint16_t version = 0;
		std::uint16_t headerSize = 0;
		(void)ReadLe16(bytes, 8, version);
		(void)ReadLe16(bytes, 10, headerSize);
		if (version != PreviewFormatVersion)
		{
			std::uint64_t commonPayloadSize = 0;
			std::uint32_t commonStoredCrc = 0;
			const bool commonEnvelopeValid = headerSize == PreviewHeaderSize
				&& ReadLe64(bytes, 80, commonPayloadSize)
				&& commonPayloadSize <= PreviewMaximumPayloadSize
				&& commonPayloadSize <= (std::numeric_limits<std::size_t>::max)()
				&& PreviewHeaderSize + static_cast<std::size_t>(commonPayloadSize)
					== bytes.size()
				&& ReadLe32(bytes, CrcOffset, commonStoredCrc)
				&& commonStoredCrc == ComputePreviewCrc32(bytes);
			result.state = commonEnvelopeValid
				? CacheState::Incompatible : CacheState::Corrupt;
			result.error = PreviewFormatError::UnsupportedVersion;
			return result;
		}
		if (headerSize != PreviewHeaderSize)
		{
			result.error = PreviewFormatError::HeaderSize;
			return result;
		}

		if (!TryReadPreviewMetadata(bytes.first(PreviewHeaderSize), bytes.size(),
			result.metadata, result.error)) return result;
		auto& metadata = result.metadata;
		std::uint32_t storedCrc = 0;
		(void)ReadLe32(bytes, CrcOffset, storedCrc);
		if (storedCrc != ComputePreviewCrc32(bytes))
		{
			result.error = PreviewFormatError::Crc;
			return result;
		}
		if (compatibility && !IsCompatible(metadata, *compatibility))
		{
			result.state = CacheState::Incompatible;
			result.error = PreviewFormatError::Compatibility;
			return result;
		}
		result.pixels.assign(bytes.begin() + PreviewHeaderSize, bytes.end());
		result.state = CacheState::Valid;
		result.error = PreviewFormatError::None;
		return result;
	}
}
