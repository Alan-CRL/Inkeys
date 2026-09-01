module;

#define MSGPACK_NO_BOOST
#include <msgpack.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <limits>
#include <map>
#include <new>
#include <optional>
#include <set>
#include <span>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>
#include <variant>
#include <vector>

module draw3.uink_codec;

namespace draw3::uink
{
	namespace
	{
		enum class ScanStatus : uint8_t
		{
			Complete,
			NeedMore,
			Malformed,
			LimitExceeded
		};

		struct ScanResult
		{
			ScanStatus status = ScanStatus::Malformed;
			size_t end = 0;
			uint64_t transientCharge = 0;
		};

		bool CanRead(size_t position, uint64_t length, size_t size) noexcept
		{
			return length <= size && position <= size - static_cast<size_t>(length);
		}

		uint16_t ReadBigEndian16(std::span<const std::byte> bytes, size_t position) noexcept
		{
			return static_cast<uint16_t>((std::to_integer<uint8_t>(bytes[position]) << 8) |
				std::to_integer<uint8_t>(bytes[position + 1]));
		}

		uint32_t ReadBigEndian32(std::span<const std::byte> bytes, size_t position) noexcept
		{
			return (static_cast<uint32_t>(std::to_integer<uint8_t>(bytes[position])) << 24) |
				(static_cast<uint32_t>(std::to_integer<uint8_t>(bytes[position + 1])) << 16) |
				(static_cast<uint32_t>(std::to_integer<uint8_t>(bytes[position + 2])) << 8) |
				static_cast<uint32_t>(std::to_integer<uint8_t>(bytes[position + 3]));
		}

		ScanResult CompleteScan(size_t end, size_t objectStart, uint64_t charge,
			const UInkReadLimits& limits) noexcept
		{
			if (end - objectStart > limits.maxTopLevelObjectBytes ||
				charge > limits.maxModelCharge)
				return { ScanStatus::LimitExceeded, end, charge };
			return { ScanStatus::Complete, end, charge };
		}

		bool AddScanCharge(uint64_t& charge, uint64_t amount) noexcept
		{
			if (charge > std::numeric_limits<uint64_t>::max() - amount) return false;
			charge += amount;
			return true;
		}

		ScanResult ScanValue(std::span<const std::byte> bytes, size_t position,
			uint32_t depth, size_t objectStart, const UInkReadLimits& limits) noexcept
		{
			if (depth > limits.maxDepth) return { ScanStatus::LimitExceeded, position };
			if (!CanRead(position, 1, bytes.size())) return { ScanStatus::NeedMore, position };

			const uint8_t marker = std::to_integer<uint8_t>(bytes[position++]);
			uint64_t payloadLength = 0;
			uint64_t elementCount = 0;
			bool isArray = false;
			bool isMap = false;
			uint64_t transientCharge = 32;

			if (marker <= 0x7f || marker >= 0xe0 || marker == 0xc0 || marker == 0xc2 || marker == 0xc3)
				return CompleteScan(position, objectStart, transientCharge, limits);
			if ((marker & 0xe0) == 0xa0)
			{
				payloadLength = marker & 0x1f;
			}
			else if ((marker & 0xf0) == 0x90)
			{
				isArray = true;
				elementCount = marker & 0x0f;
			}
			else if ((marker & 0xf0) == 0x80)
			{
				isMap = true;
				elementCount = marker & 0x0f;
			}
			else
			{
				switch (marker)
				{
				case 0xc1:
					return { ScanStatus::Malformed, position - 1 };
				case 0xc4:
				case 0xd9:
					if (!CanRead(position, 1, bytes.size())) return { ScanStatus::NeedMore, position };
					payloadLength = std::to_integer<uint8_t>(bytes[position++]);
					break;
				case 0xc5:
				case 0xda:
					if (!CanRead(position, 2, bytes.size())) return { ScanStatus::NeedMore, position };
					payloadLength = ReadBigEndian16(bytes, position);
					position += 2;
					break;
				case 0xc6:
				case 0xdb:
					if (!CanRead(position, 4, bytes.size())) return { ScanStatus::NeedMore, position };
					payloadLength = ReadBigEndian32(bytes, position);
					position += 4;
					break;
				case 0xc7:
					if (!CanRead(position, 1, bytes.size())) return { ScanStatus::NeedMore, position };
					payloadLength = std::to_integer<uint8_t>(bytes[position++]);
					if (!CanRead(position, 1, bytes.size())) return { ScanStatus::NeedMore, position };
					++position;
					break;
				case 0xc8:
					if (!CanRead(position, 2, bytes.size())) return { ScanStatus::NeedMore, position };
					payloadLength = ReadBigEndian16(bytes, position);
					position += 2;
					if (!CanRead(position, 1, bytes.size())) return { ScanStatus::NeedMore, position };
					++position;
					break;
				case 0xc9:
					if (!CanRead(position, 4, bytes.size())) return { ScanStatus::NeedMore, position };
					payloadLength = ReadBigEndian32(bytes, position);
					position += 4;
					if (!CanRead(position, 1, bytes.size())) return { ScanStatus::NeedMore, position };
					++position;
					break;
				case 0xca: payloadLength = 4; break;
				case 0xcb: payloadLength = 8; break;
				case 0xcc:
				case 0xd0: payloadLength = 1; break;
				case 0xcd:
				case 0xd1: payloadLength = 2; break;
				case 0xce:
				case 0xd2: payloadLength = 4; break;
				case 0xcf:
				case 0xd3: payloadLength = 8; break;
				case 0xd4: payloadLength = 2; break;
				case 0xd5: payloadLength = 3; break;
				case 0xd6: payloadLength = 5; break;
				case 0xd7: payloadLength = 9; break;
				case 0xd8: payloadLength = 17; break;
				case 0xdc:
					if (!CanRead(position, 2, bytes.size())) return { ScanStatus::NeedMore, position };
					isArray = true;
					elementCount = ReadBigEndian16(bytes, position);
					position += 2;
					break;
				case 0xdd:
					if (!CanRead(position, 4, bytes.size())) return { ScanStatus::NeedMore, position };
					isArray = true;
					elementCount = ReadBigEndian32(bytes, position);
					position += 4;
					break;
				case 0xde:
					if (!CanRead(position, 2, bytes.size())) return { ScanStatus::NeedMore, position };
					isMap = true;
					elementCount = ReadBigEndian16(bytes, position);
					position += 2;
					break;
				case 0xdf:
					if (!CanRead(position, 4, bytes.size())) return { ScanStatus::NeedMore, position };
					isMap = true;
					elementCount = ReadBigEndian32(bytes, position);
					position += 4;
					break;
				default:
					return { ScanStatus::Malformed, position - 1 };
				}
			}

			if (isArray || isMap)
			{
				if (elementCount > limits.maxContainerEntries)
					return { ScanStatus::LimitExceeded, position };
				uint64_t values = elementCount;
				if (isMap)
				{
					if (elementCount > std::numeric_limits<uint64_t>::max() / 2)
						return { ScanStatus::LimitExceeded, position };
					values *= 2;
				}
				for (uint64_t index = 0; index < values; ++index)
				{
					const ScanResult nested = ScanValue(bytes, position, depth + 1, objectStart, limits);
					if (nested.status != ScanStatus::Complete) return nested;
					if (!AddScanCharge(transientCharge, nested.transientCharge))
						return { ScanStatus::LimitExceeded, nested.end };
					position = nested.end;
					if (position - objectStart > limits.maxTopLevelObjectBytes)
						return { ScanStatus::LimitExceeded, position };
				}
				return CompleteScan(position, objectStart, transientCharge, limits);
			}

			if ((marker >= 0xa0 && marker <= 0xbf) || marker == 0xd9 || marker == 0xda ||
				marker == 0xdb || marker == 0xc4 || marker == 0xc5 || marker == 0xc6 ||
				marker == 0xc7 || marker == 0xc8 || marker == 0xc9 ||
				(marker >= 0xd4 && marker <= 0xd8))
			{
				if (payloadLength > limits.maxStringOrBinaryBytes)
					return { ScanStatus::LimitExceeded, position };
			}
			if (!CanRead(position, payloadLength, bytes.size()))
				return { ScanStatus::NeedMore, position };
			position += static_cast<size_t>(payloadLength);
			if (!AddScanCharge(transientCharge, payloadLength))
				return { ScanStatus::LimitExceeded, position };
			return CompleteScan(position, objectStart, transientCharge, limits);
		}

		bool IsValidUtf8(std::string_view text) noexcept
		{
			for (size_t index = 0; index < text.size();)
			{
				const uint8_t first = static_cast<uint8_t>(text[index++]);
				if (first <= 0x7f) continue;
				uint32_t codePoint = 0;
				size_t continuation = 0;
				if (first >= 0xc2 && first <= 0xdf)
				{
					codePoint = first & 0x1f;
					continuation = 1;
				}
				else if (first >= 0xe0 && first <= 0xef)
				{
					codePoint = first & 0x0f;
					continuation = 2;
				}
				else if (first >= 0xf0 && first <= 0xf4)
				{
					codePoint = first & 0x07;
					continuation = 3;
				}
				else return false;
				if (index + continuation > text.size()) return false;
				for (size_t part = 0; part < continuation; ++part)
				{
					const uint8_t next = static_cast<uint8_t>(text[index++]);
					if ((next & 0xc0) != 0x80) return false;
					codePoint = (codePoint << 6) | (next & 0x3f);
				}
				if ((continuation == 2 && codePoint < 0x800) ||
					(continuation == 3 && codePoint < 0x10000) ||
					(codePoint >= 0xd800 && codePoint <= 0xdfff) || codePoint > 0x10ffff)
					return false;
			}
			return true;
		}

		struct DecodeContext
		{
			const UInkReadLimits& limits;
			UInkReadResult& result;
			uint64_t objectIndex = 0;
			uint64_t objectOffset = 0;
			uint64_t totalPoints = 0;
			bool limitExceeded = false;
			bool duplicateKnownKey = false;
			uint64_t persistentCharge = 0;
			uint64_t transientCharge = 0;

			void Add(UInkDiagnosticCode code, UInkDiagnosticSeverity severity,
				std::string fieldPath = {}, uint32_t systemError = 0)
			{
				if (limits.maxDiagnostics == 0) return;
				if (result.diagnostics.size() < limits.maxDiagnostics)
				{
					result.diagnostics.push_back({ code, severity, objectOffset,
						objectIndex, std::move(fieldPath), systemError });
					return;
				}
				if (!result.diagnostics.empty() &&
					result.diagnostics.back().code != UInkDiagnosticCode::DiagnosticsTruncated)
				{
					result.diagnostics.back() = { UInkDiagnosticCode::DiagnosticsTruncated,
						UInkDiagnosticSeverity::Warning, objectOffset, objectIndex, {}, 0 };
				}
			}

			bool Charge(uint64_t amount, std::string fieldPath)
			{
				if (transientCharge > limits.maxModelCharge ||
					persistentCharge > limits.maxModelCharge - transientCharge ||
					amount > limits.maxModelCharge - transientCharge - persistentCharge)
				{
					if (!limitExceeded)
						Add(UInkDiagnosticCode::LimitExceeded, UInkDiagnosticSeverity::Fatal,
							std::move(fieldPath));
					limitExceeded = true;
					return false;
				}
				persistentCharge += amount;
				result.modelCharge = std::max(result.modelCharge,
					persistentCharge + transientCharge);
				return true;
			}

			bool BeginTransient(uint64_t amount, std::string fieldPath)
			{
				if (amount > limits.maxModelCharge ||
					persistentCharge > limits.maxModelCharge - amount)
				{
					if (!limitExceeded)
						Add(UInkDiagnosticCode::LimitExceeded, UInkDiagnosticSeverity::Fatal,
							std::move(fieldPath));
					limitExceeded = true;
					return false;
				}
				transientCharge = amount;
				result.modelCharge = std::max(result.modelCharge,
					persistentCharge + transientCharge);
				return true;
			}

			void EndTransient() noexcept
			{
				transientCharge = 0;
			}

			bool AddPoints(uint64_t amount, std::string fieldPath)
			{
				if (amount > limits.maxSingleGeometryPoints || amount > limits.maxGeometryPoints ||
					totalPoints > limits.maxGeometryPoints - amount)
				{
					Add(UInkDiagnosticCode::LimitExceeded, UInkDiagnosticSeverity::Fatal,
						std::move(fieldPath));
					limitExceeded = true;
					return false;
				}
				totalPoints += amount;
				return Charge(amount * 64, std::move(fieldPath));
			}
		};

		class TransientChargeGuard
		{
		public:
			TransientChargeGuard(DecodeContext& context, uint64_t amount,
				std::string fieldPath) : context_(context),
				active_(context.BeginTransient(amount, std::move(fieldPath))) {}
			~TransientChargeGuard() { if (active_) context_.EndTransient(); }
			bool IsActive() const noexcept { return active_; }

		private:
			DecodeContext& context_;
			bool active_ = false;
		};

		std::string ChildPath(std::string_view parent, std::string_view child)
		{
			if (parent.empty()) return std::string(child);
			std::string result(parent);
			result.push_back('.');
			result.append(child);
			return result;
		}

		std::optional<std::string_view> ObjectStringView(const msgpack::object& object) noexcept
		{
			if (object.type != msgpack::type::STR) return std::nullopt;
			return std::string_view(object.via.str.ptr, object.via.str.size);
		}

		bool ValidateMap(const msgpack::object& object,
			std::initializer_list<std::string_view> knownKeys, DecodeContext& context,
			std::string_view path)
		{
			if (object.type != msgpack::type::MAP)
			{
				context.Add(UInkDiagnosticCode::InvalidFieldType, UInkDiagnosticSeverity::Warning,
					std::string(path));
				return false;
			}

			std::vector<bool> seen(knownKeys.size(), false);
			for (uint32_t index = 0; index < object.via.map.size; ++index)
			{
				const std::optional<std::string_view> key =
					ObjectStringView(object.via.map.ptr[index].key);
				if (!key) continue;
				size_t knownIndex = 0;
				for (const std::string_view known : knownKeys)
				{
					if (*key == known)
					{
						if (seen[knownIndex])
						{
							context.Add(UInkDiagnosticCode::DuplicateKnownKey,
								UInkDiagnosticSeverity::Warning, ChildPath(path, known));
							context.duplicateKnownKey = true;
							return false;
						}
						seen[knownIndex] = true;
						break;
					}
					++knownIndex;
				}
			}
			return true;
		}

		const msgpack::object* FindField(const msgpack::object& map, std::string_view key) noexcept
		{
			if (map.type != msgpack::type::MAP) return nullptr;
			for (uint32_t index = 0; index < map.via.map.size; ++index)
			{
				const std::optional<std::string_view> candidate =
					ObjectStringView(map.via.map.ptr[index].key);
				if (candidate && *candidate == key) return &map.via.map.ptr[index].val;
			}
			return nullptr;
		}

		void AddNumericCompatibility(const msgpack::object& object,
			msgpack::type::object_type exactType, DecodeContext& context, const std::string& path)
		{
			if (object.type != exactType)
				context.Add(UInkDiagnosticCode::NumericCompatibility,
					UInkDiagnosticSeverity::Info, path);
		}

		bool ReadUInt64(const msgpack::object& object, uint64_t& value,
			DecodeContext& context, const std::string& path,
			msgpack::type::object_type exactType = msgpack::type::POSITIVE_INTEGER)
		{
			if (object.type == msgpack::type::POSITIVE_INTEGER)
			{
				value = object.via.u64;
				AddNumericCompatibility(object, exactType, context, path);
				return true;
			}
			if (object.type == msgpack::type::FLOAT32 || object.type == msgpack::type::FLOAT64)
			{
				const double number = object.via.f64;
				if (!std::isfinite(number) || number < 0.0 || std::trunc(number) != number ||
					number >= std::ldexp(1.0, 64)) return false;
				const uint64_t converted = static_cast<uint64_t>(number);
				if (static_cast<double>(converted) != number) return false;
				value = converted;
				context.Add(UInkDiagnosticCode::NumericCompatibility,
					UInkDiagnosticSeverity::Info, path);
				return true;
			}
			return false;
		}

		bool ReadUInt32(const msgpack::object& object, uint32_t& value,
			DecodeContext& context, const std::string& path)
		{
			uint64_t wide = 0;
			if (!ReadUInt64(object, wide, context, path) ||
				wide > std::numeric_limits<uint32_t>::max()) return false;
			value = static_cast<uint32_t>(wide);
			return true;
		}

		bool ReadUInt16(const msgpack::object& object, uint16_t& value,
			DecodeContext& context, const std::string& path)
		{
			uint64_t wide = 0;
			if (!ReadUInt64(object, wide, context, path) ||
				wide > std::numeric_limits<uint16_t>::max()) return false;
			value = static_cast<uint16_t>(wide);
			return true;
		}

		bool ReadInt32(const msgpack::object& object, int32_t& value,
			DecodeContext& context, const std::string& path)
		{
			int64_t wide = 0;
			if (object.type == msgpack::type::NEGATIVE_INTEGER)
				wide = object.via.i64;
			else if (object.type == msgpack::type::POSITIVE_INTEGER)
			{
				if (object.via.u64 > static_cast<uint64_t>(std::numeric_limits<int32_t>::max()))
					return false;
				wide = static_cast<int64_t>(object.via.u64);
			}
			else if (object.type == msgpack::type::FLOAT32 || object.type == msgpack::type::FLOAT64)
			{
				const double number = object.via.f64;
				if (!std::isfinite(number) || std::trunc(number) != number ||
					number < static_cast<double>(std::numeric_limits<int32_t>::min()) ||
					number > static_cast<double>(std::numeric_limits<int32_t>::max())) return false;
				wide = static_cast<int64_t>(number);
			}
			else return false;
			if (wide < std::numeric_limits<int32_t>::min() ||
				wide > std::numeric_limits<int32_t>::max()) return false;
			value = static_cast<int32_t>(wide);
			if (object.type == msgpack::type::FLOAT32 || object.type == msgpack::type::FLOAT64)
				context.Add(UInkDiagnosticCode::NumericCompatibility,
					UInkDiagnosticSeverity::Info, path);
			return true;
		}

		bool ReadFloat32(const msgpack::object& object, float& value,
			DecodeContext& context, const std::string& path)
		{
			double number = 0.0;
			if (object.type == msgpack::type::FLOAT32 || object.type == msgpack::type::FLOAT64)
				number = object.via.f64;
			else if (object.type == msgpack::type::POSITIVE_INTEGER)
			{
				const float converted = static_cast<float>(object.via.u64);
				// 先排除 float 舍入到 2^64 的情况，避免越界转回 uint64_t。
				if (!std::isfinite(converted) ||
					static_cast<double>(converted) >= std::ldexp(1.0, 64) ||
					static_cast<uint64_t>(converted) != object.via.u64)
					return false;
				value = converted;
				context.Add(UInkDiagnosticCode::NumericCompatibility,
					UInkDiagnosticSeverity::Info, path);
				return true;
			}
			else if (object.type == msgpack::type::NEGATIVE_INTEGER)
			{
				const float converted = static_cast<float>(object.via.i64);
				if (!std::isfinite(converted) || static_cast<int64_t>(converted) != object.via.i64)
					return false;
				value = converted;
				context.Add(UInkDiagnosticCode::NumericCompatibility,
					UInkDiagnosticSeverity::Info, path);
				return true;
			}
			else return false;
			const float converted = static_cast<float>(number);
			if (std::isfinite(number) && static_cast<double>(converted) != number) return false;
			if (!std::isfinite(number) && object.type != msgpack::type::FLOAT32) return false;
			value = converted;
			AddNumericCompatibility(object, msgpack::type::FLOAT32, context, path);
			return true;
		}

		bool ReadFloat64(const msgpack::object& object, double& value,
			DecodeContext& context, const std::string& path)
		{
			if (object.type == msgpack::type::FLOAT64 || object.type == msgpack::type::FLOAT32)
				value = object.via.f64;
			else if (object.type == msgpack::type::POSITIVE_INTEGER)
			{
				value = static_cast<double>(object.via.u64);
				if (value >= std::ldexp(1.0, 64) ||
					static_cast<uint64_t>(value) != object.via.u64) return false;
			}
			else if (object.type == msgpack::type::NEGATIVE_INTEGER)
			{
				value = static_cast<double>(object.via.i64);
				if (static_cast<int64_t>(value) != object.via.i64) return false;
			}
			else return false;
			AddNumericCompatibility(object, msgpack::type::FLOAT64, context, path);
			return true;
		}

		bool ReadBool(const msgpack::object& object, bool& value) noexcept
		{
			if (object.type != msgpack::type::BOOLEAN) return false;
			value = object.via.boolean;
			return true;
		}

		bool ReadString(const msgpack::object& object, std::string& value,
			DecodeContext& context, const std::string& path)
		{
			const std::optional<std::string_view> text = ObjectStringView(object);
			if (!text || !IsValidUtf8(*text) || !context.Charge(32 + text->size(), path)) return false;
			value.assign(text->data(), text->size());
			return true;
		}

		bool ReadGuid(const msgpack::object& object, UInkGuid& value,
			DecodeContext& context, const std::string& path)
		{
			std::string text;
			if (!ReadString(object, text, context, path)) return false;
			const std::optional<UInkGuid> guid = ParseUInkGuid(text);
			if (!guid) return false;
			value = *guid;
			return true;
		}

		bool DecodeGenericValue(const msgpack::object& object, UInkMessagePackValue& value,
			DecodeContext& context, const std::string& path, uint32_t depth)
		{
			if (depth > context.limits.maxDepth || !context.Charge(48, path))
			{
				context.limitExceeded = true;
				return false;
			}
			switch (object.type)
			{
			case msgpack::type::NIL:
				value.value = std::monostate{};
				return true;
			case msgpack::type::BOOLEAN:
				value.value = object.via.boolean;
				return true;
			case msgpack::type::POSITIVE_INTEGER:
				value.value = object.via.u64;
				return true;
			case msgpack::type::NEGATIVE_INTEGER:
				value.value = object.via.i64;
				return true;
			case msgpack::type::FLOAT32:
				value.value = static_cast<float>(object.via.f64);
				return true;
			case msgpack::type::FLOAT64:
				value.value = object.via.f64;
				return true;
			case msgpack::type::STR:
			{
				const std::string_view text(object.via.str.ptr, object.via.str.size);
				if (!IsValidUtf8(text) || !context.Charge(text.size(), path)) return false;
				value.value = std::string(text);
				return true;
			}
			case msgpack::type::BIN:
			{
				if (!context.Charge(object.via.bin.size, path)) return false;
				std::vector<std::byte> bytes(object.via.bin.size);
				for (uint32_t index = 0; index < object.via.bin.size; ++index)
					bytes[index] = static_cast<std::byte>(object.via.bin.ptr[index]);
				value.value = std::move(bytes);
				return true;
			}
			case msgpack::type::ARRAY:
			{
				if (!context.Charge(static_cast<uint64_t>(object.via.array.size) * 16, path)) return false;
				UInkMessagePackValue::Array array;
				array.reserve(object.via.array.size);
				for (uint32_t index = 0; index < object.via.array.size; ++index)
				{
					UInkMessagePackValue item;
					if (!DecodeGenericValue(object.via.array.ptr[index], item, context,
						path, depth + 1)) return false;
					array.push_back(std::move(item));
				}
				value.value = std::move(array);
				return true;
			}
			case msgpack::type::MAP:
			{
				if (!context.Charge(static_cast<uint64_t>(object.via.map.size) * 32, path)) return false;
				UInkMessagePackValue::Map map;
				map.reserve(object.via.map.size);
				for (uint32_t index = 0; index < object.via.map.size; ++index)
				{
					UInkMessagePackValue key;
					UInkMessagePackValue item;
					if (!DecodeGenericValue(object.via.map.ptr[index].key, key, context,
						path, depth + 1) ||
						!DecodeGenericValue(object.via.map.ptr[index].val, item, context,
							path, depth + 1)) return false;
					map.emplace_back(std::move(key), std::move(item));
				}
				value.value = std::move(map);
				return true;
			}
			case msgpack::type::EXT:
			{
				if (!context.Charge(object.via.ext.size, path)) return false;
				UInkMessagePackExtension extension;
				extension.type = object.via.ext.type();
				extension.data.resize(object.via.ext.size);
				for (uint32_t index = 0; index < object.via.ext.size; ++index)
					extension.data[index] = static_cast<std::byte>(object.via.ext.data()[index]);
				value.value = std::move(extension);
				return true;
			}
			default:
				return false;
			}
		}

		bool ReadExtra(const msgpack::object& object, UInkExtra& extra,
			DecodeContext& context, const std::string& path)
		{
			if (object.type != msgpack::type::MAP) return false;
			for (uint32_t index = 0; index < object.via.map.size; ++index)
			{
				const std::optional<std::string_view> key =
					ObjectStringView(object.via.map.ptr[index].key);
				if (!key || !IsValidUtf8(*key))
				{
					context.Add(key ? UInkDiagnosticCode::InvalidFieldValue :
						UInkDiagnosticCode::InvalidFieldType,
						UInkDiagnosticSeverity::Warning, ChildPath(path, "key"));
					return false;
				}
			}
			UInkMessagePackValue value;
			if (!DecodeGenericValue(object, value, context, path, 1)) return false;
			UInkMessagePackValue::Map* map = std::get_if<UInkMessagePackValue::Map>(&value.value);
			if (!map) return false;
			extra = std::move(*map);
			return true;
		}

		bool ReadColor(const msgpack::object& object, UInkColor& color,
			DecodeContext& context, const std::string& path)
		{
			if (!ValidateMap(object, { "fallback", "space", "components" }, context, path))
				return false;
			const msgpack::object* fallback = FindField(object, "fallback");
			if (!fallback || !ReadUInt32(*fallback, color.fallbackRgb, context,
				ChildPath(path, "fallback"))) return false;
			if (color.fallbackRgb > 0xffffff)
			{
				color.fallbackRgb &= 0xffffff;
				context.result.provenance.usedFieldFallback = true;
				context.Add(UInkDiagnosticCode::FieldFallback, UInkDiagnosticSeverity::Warning,
					ChildPath(path, "fallback"));
			}

			const msgpack::object* space = FindField(object, "space");
			const msgpack::object* components = FindField(object, "components");
			if (!space && !components) return true;
			if (!space || !components || space->type != msgpack::type::STR ||
				components->type != msgpack::type::ARRAY || components->via.array.size != 3)
			{
				context.result.provenance.usedFieldFallback = true;
				context.Add(UInkDiagnosticCode::FieldFallback, UInkDiagnosticSeverity::Warning, path);
				return true;
			}

			const std::string_view colorSpace(space->via.str.ptr, space->via.str.size);
			UInkExtendedColor extended;
			if (colorSpace == "srgb") extended.space = UInkColorSpace::Srgb;
			else if (colorSpace == "scrgb") extended.space = UInkColorSpace::ScRgb;
			else
			{
				context.result.provenance.usedFieldFallback = true;
				context.Add(UInkDiagnosticCode::FieldFallback, UInkDiagnosticSeverity::Warning,
					ChildPath(path, "space"));
				return true;
			}
			for (size_t index = 0; index < extended.components.size(); ++index)
			{
				if (!ReadFloat32(components->via.array.ptr[index], extended.components[index],
					context, ChildPath(path, "components")) ||
					!std::isfinite(extended.components[index]) ||
					(extended.space == UInkColorSpace::Srgb &&
						(extended.components[index] < 0.0f || extended.components[index] > 1.0f)))
				{
					context.result.provenance.usedFieldFallback = true;
					context.Add(UInkDiagnosticCode::FieldFallback, UInkDiagnosticSeverity::Warning,
						ChildPath(path, "components"));
					return true;
				}
			}
			color.extended = extended;
			return true;
		}

		float ClampOpacity(float value) noexcept
		{
			return std::clamp(value, 0.0f, 1.0f);
		}

		void AddMissing(DecodeContext& context, const std::string& path)
		{
			context.Add(UInkDiagnosticCode::MissingRequiredField,
				UInkDiagnosticSeverity::Warning, path);
		}

		bool HeaderGuidUsesStr8(std::span<const std::byte> objectBytes,
			const UInkReadLimits& limits) noexcept
		{
			if (objectBytes.empty()) return false;
			size_t position = 0;
			const uint8_t marker = std::to_integer<uint8_t>(objectBytes[position++]);
			uint32_t count = 0;
			if ((marker & 0xf0) == 0x90) count = marker & 0x0f;
			else if (marker == 0xdc)
			{
				if (!CanRead(position, 2, objectBytes.size())) return false;
				count = ReadBigEndian16(objectBytes, position);
				position += 2;
			}
			else if (marker == 0xdd)
			{
				if (!CanRead(position, 4, objectBytes.size())) return false;
				count = ReadBigEndian32(objectBytes, position);
				position += 4;
			}
			else return false;
			if (count != 7) return false;
			for (size_t index = 0; index < 2; ++index)
			{
				const ScanResult scanned = ScanValue(objectBytes, position, 1, 0, limits);
				if (scanned.status != ScanStatus::Complete) return false;
				position = scanned.end;
			}
			return CanRead(position, 2, objectBytes.size()) &&
				std::to_integer<uint8_t>(objectBytes[position]) == 0xd9 &&
				std::to_integer<uint8_t>(objectBytes[position + 1]) == 36;
		}

		bool DecodeHeader(const msgpack::object& object, std::span<const std::byte> raw,
			UInkHeader& header, DecodeContext& context)
		{
			if (object.type != msgpack::type::ARRAY || object.via.array.size != 7 ||
				!HeaderGuidUsesStr8(raw, context.limits)) return false;
			uint16_t type = 0;
			uint16_t version = 0;
			if (!ReadUInt16(object.via.array.ptr[0], type, context, "header.type") ||
				type != kHeaderType ||
				!ReadUInt16(object.via.array.ptr[1], version, context, "header.version"))
				return false;
			if (version != kUInkVersion)
			{
				context.Add(UInkDiagnosticCode::UnsupportedVersion,
					UInkDiagnosticSeverity::Fatal, "header.version");
				return false;
			}
			if (!ReadGuid(object.via.array.ptr[2], header.guid, context, "header.guid") ||
				!ReadUInt32(object.via.array.ptr[3], header.deviceNum, context, "header.deviceNum") ||
				!ReadUInt32(object.via.array.ptr[4], header.workspaceNum, context, "header.workspaceNum") ||
				!ReadUInt32(object.via.array.ptr[5], header.pageNum, context, "header.pageNum") ||
				!ReadUInt64(object.via.array.ptr[6], header.time, context, "header.time"))
				return false;
			return context.Charge(128, "header");
		}

		bool DecodeHardware(const msgpack::object& object, UInkHardware& hardware,
			DecodeContext& context, const std::string& path)
		{
			if (!ValidateMap(object, { "name", "id", "identifiers", "physicalWidth",
				"physicalHeight", "scaleFactor" }, context, path)) return false;
			if (!context.Charge(128, path)) return false;
			if (const msgpack::object* field = FindField(object, "name"))
			{
				std::string value;
				if (!ReadString(*field, value, context, ChildPath(path, "name"))) return false;
				hardware.name = std::move(value);
			}
			if (const msgpack::object* field = FindField(object, "id"))
			{
				std::string value;
				if (!ReadString(*field, value, context, ChildPath(path, "id"))) return false;
				hardware.id = std::move(value);
			}
			if (const msgpack::object* field = FindField(object, "identifiers"))
			{
				if (field->type != msgpack::type::MAP ||
					!context.Charge(static_cast<uint64_t>(field->via.map.size) * 64,
						ChildPath(path, "identifiers"))) return false;
				std::set<std::string> identifierKeys;
				hardware.identifiers.reserve(field->via.map.size);
				for (uint32_t index = 0; index < field->via.map.size; ++index)
				{
					std::string key;
					std::string value;
					if (!ReadString(field->via.map.ptr[index].key, key, context,
						ChildPath(path, "identifiers")) ||
						!ReadString(field->via.map.ptr[index].val, value, context,
							ChildPath(path, "identifiers")) ||
						!identifierKeys.insert(key).second) return false;
					hardware.identifiers.emplace_back(std::move(key), std::move(value));
				}
			}
			if (const msgpack::object* field = FindField(object, "physicalWidth"))
			{
				uint32_t value = 0;
				if (!ReadUInt32(*field, value, context, ChildPath(path, "physicalWidth")) ||
					value == 0) return false;
				hardware.physicalWidth = value;
			}
			if (const msgpack::object* field = FindField(object, "physicalHeight"))
			{
				uint32_t value = 0;
				if (!ReadUInt32(*field, value, context, ChildPath(path, "physicalHeight")) ||
					value == 0) return false;
				hardware.physicalHeight = value;
			}
			if (const msgpack::object* field = FindField(object, "scaleFactor"))
			{
				float value = 0.0f;
				if (!ReadFloat32(*field, value, context, ChildPath(path, "scaleFactor")) ||
					!std::isfinite(value) || value <= 0.0f) return false;
				hardware.scaleFactor = value;
			}
			return true;
		}

		bool DecodeDevice(const msgpack::object& object, UInkDevice& device,
			DecodeContext& context, const std::string& path)
		{
			if (!ValidateMap(object, { "guid", "deviceType", "name", "hardware", "extra",
				"x", "y", "width", "height", "parentDeviceGuid", "zIndex" }, context, path))
				return false;
			const msgpack::object* guid = FindField(object, "guid");
			const msgpack::object* type = FindField(object, "deviceType");
			if (!guid || !type || !ReadGuid(*guid, device.guid, context, ChildPath(path, "guid")) ||
				!ReadInt32(*type, device.deviceType, context, ChildPath(path, "deviceType")))
				return false;
			if (device.deviceType < 0) return false;
			if (!context.Charge(256, path)) return false;
			if (const msgpack::object* field = FindField(object, "name"))
			{
				std::string value;
				if (!ReadString(*field, value, context, ChildPath(path, "name"))) return false;
				device.name = std::move(value);
			}
			if (const msgpack::object* field = FindField(object, "hardware"))
			{
				UInkHardware value;
				if (!DecodeHardware(*field, value, context, ChildPath(path, "hardware")))
				{
					if (context.duplicateKnownKey) return false;
					context.result.provenance.usedFieldFallback = true;
					context.Add(UInkDiagnosticCode::FieldFallback, UInkDiagnosticSeverity::Warning,
						ChildPath(path, "hardware"));
				}
				else device.hardware = std::move(value);
			}
			if (const msgpack::object* field = FindField(object, "extra"))
			{
				UInkExtra extra;
				if (!ReadExtra(*field, extra, context, ChildPath(path, "extra"))) return false;
				device.extra = std::move(extra);
			}

			if (device.deviceType == 0)
			{
				UInkDisplayDevice display;
				const msgpack::object* x = FindField(object, "x");
				const msgpack::object* y = FindField(object, "y");
				const msgpack::object* width = FindField(object, "width");
				const msgpack::object* height = FindField(object, "height");
				if (!x || !y || !width || !height ||
					!ReadInt32(*x, display.x, context, ChildPath(path, "x")) ||
					!ReadInt32(*y, display.y, context, ChildPath(path, "y")) ||
					!ReadUInt32(*width, display.width, context, ChildPath(path, "width")) ||
					!ReadUInt32(*height, display.height, context, ChildPath(path, "height")) ||
					display.width == 0 || display.height == 0) return false;
				device.geometry = display;
			}
			else if (device.deviceType == 1)
			{
				UInkWindowDevice window;
				const msgpack::object* parent = FindField(object, "parentDeviceGuid");
				const msgpack::object* x = FindField(object, "x");
				const msgpack::object* y = FindField(object, "y");
				const msgpack::object* width = FindField(object, "width");
				const msgpack::object* height = FindField(object, "height");
				const msgpack::object* zIndex = FindField(object, "zIndex");
				if (!x || !y || !width || !height || !zIndex ||
					!ReadFloat32(*x, window.x, context, ChildPath(path, "x")) ||
					!ReadFloat32(*y, window.y, context, ChildPath(path, "y")) ||
					!ReadFloat32(*width, window.width, context, ChildPath(path, "width")) ||
					!ReadFloat32(*height, window.height, context, ChildPath(path, "height")) ||
					!ReadUInt32(*zIndex, window.zIndex, context, ChildPath(path, "zIndex")) ||
					!std::isfinite(window.x) || !std::isfinite(window.y) ||
					!std::isfinite(window.width) || !std::isfinite(window.height) ||
					window.width <= 0.0f || window.height <= 0.0f) return false;
				if (!parent || !ReadGuid(*parent, window.parentDeviceGuid, context,
					ChildPath(path, "parentDeviceGuid")))
				{
					if (context.limitExceeded) return false;
					// 父引用损坏只断开 Window，不丢弃仍可作为临时根使用的几何。
					device.parentResolved = false;
					context.result.provenance.usedTemporaryIdentity = true;
					context.Add(UInkDiagnosticCode::TemporaryIdentity,
						UInkDiagnosticSeverity::Warning, ChildPath(path, "parentDeviceGuid"));
				}
				device.geometry = window;
			}
			else
			{
				device.geometry = UInkUnknownDevice{};
				context.result.provenance.usedFieldFallback = true;
				context.result.provenance.usedTemporaryIdentity = true;
				context.Add(UInkDiagnosticCode::FieldFallback, UInkDiagnosticSeverity::Warning,
					ChildPath(path, "deviceType"));
				context.Add(UInkDiagnosticCode::TemporaryIdentity,
					UInkDiagnosticSeverity::Warning, ChildPath(path, "deviceType"));
			}
			return true;
		}

		bool DecodeWorkspace(const msgpack::object& object, UInkWorkspace& workspace,
			DecodeContext& context, const std::string& path)
		{
			if (!ValidateMap(object, { "guid", "workspaceType", "name", "parentWorkspaceGuid",
				"hostId", "currentPageIndex", "extra" }, context, path)) return false;
			const msgpack::object* guid = FindField(object, "guid");
			const msgpack::object* type = FindField(object, "workspaceType");
			if (!guid || !type || !ReadGuid(*guid, workspace.guid, context, ChildPath(path, "guid")) ||
				!ReadInt32(*type, workspace.workspaceType, context, ChildPath(path, "workspaceType")))
				return false;
			if (workspace.workspaceType < 0) return false;
			if (!context.Charge(192, path)) return false;
			if (const msgpack::object* field = FindField(object, "name"))
			{
				std::string value;
				if (!ReadString(*field, value, context, ChildPath(path, "name"))) return false;
				workspace.name = std::move(value);
			}
			if (const msgpack::object* field = FindField(object, "parentWorkspaceGuid"))
			{
				UInkGuid value;
				if (!ReadGuid(*field, value, context, ChildPath(path, "parentWorkspaceGuid")))
				{
					if (context.limitExceeded) return false;
					// 无法解析的父 GUID 按规范断开，Workspace 本身及其 Canvas 继续保留。
					workspace.parentResolved = false;
					context.result.provenance.usedTemporaryIdentity = true;
					context.Add(UInkDiagnosticCode::TemporaryIdentity,
						UInkDiagnosticSeverity::Warning, ChildPath(path, "parentWorkspaceGuid"));
				}
				else workspace.parentWorkspaceGuid = value;
			}
			if (const msgpack::object* field = FindField(object, "hostId"))
			{
				std::string value;
				if (!ReadString(*field, value, context, ChildPath(path, "hostId"))) return false;
				workspace.hostId = std::move(value);
			}
			if (const msgpack::object* field = FindField(object, "currentPageIndex"))
			{
				if (!ReadUInt32(*field, workspace.currentPageIndex, context,
					ChildPath(path, "currentPageIndex"))) return false;
			}
			if (const msgpack::object* field = FindField(object, "extra"))
			{
				UInkExtra extra;
				if (!ReadExtra(*field, extra, context, ChildPath(path, "extra"))) return false;
				workspace.extra = std::move(extra);
			}
			if (workspace.workspaceType > 2)
			{
				context.result.provenance.usedFieldFallback = true;
				context.Add(UInkDiagnosticCode::FieldFallback, UInkDiagnosticSeverity::Warning,
					ChildPath(path, "workspaceType"));
			}
			return true;
		}

		bool DecodeHeaderExtension(const msgpack::object& object,
			UInkHeaderExtension& extension, DecodeContext& context)
		{
			const std::string path = "headerExtension";
			if (!ValidateMap(object, { "type", "name", "explanation", "devices",
				"workspaces", "extra" }, context, path)) return false;
			const msgpack::object* typeField = FindField(object, "type");
			uint16_t type = 0;
			if (!typeField || !ReadUInt16(*typeField, type, context, "headerExtension.type") ||
				type != kHeaderExtensionType || !context.Charge(192, path)) return false;
			if (const msgpack::object* field = FindField(object, "name"))
			{
				std::string value;
				if (!ReadString(*field, value, context, "headerExtension.name")) return false;
				extension.name = std::move(value);
			}
			if (const msgpack::object* field = FindField(object, "explanation"))
			{
				std::string value;
				if (!ReadString(*field, value, context, "headerExtension.explanation")) return false;
				extension.explanation = std::move(value);
			}
			if (const msgpack::object* field = FindField(object, "devices"))
			{
				if (field->type != msgpack::type::ARRAY ||
					!context.Charge(static_cast<uint64_t>(field->via.array.size) * 64,
						"headerExtension.devices")) return false;
				extension.devices.reserve(field->via.array.size);
				for (uint32_t index = 0; index < field->via.array.size; ++index)
				{
					UInkDevice device;
					const bool duplicateBefore = context.duplicateKnownKey;
					if (DecodeDevice(field->via.array.ptr[index], device, context,
						"headerExtension.devices")) extension.devices.push_back(std::move(device));
					else if (context.duplicateKnownKey != duplicateBefore) return false;
					else
					{
						context.result.provenance.usedFieldFallback = true;
						context.Add(UInkDiagnosticCode::KnownBlockSkipped,
							UInkDiagnosticSeverity::Warning, "headerExtension.devices");
					}
					if (context.limitExceeded) return false;
				}
			}
			if (const msgpack::object* field = FindField(object, "workspaces"))
			{
				if (field->type != msgpack::type::ARRAY ||
					!context.Charge(static_cast<uint64_t>(field->via.array.size) * 64,
						"headerExtension.workspaces")) return false;
				extension.workspaces.reserve(field->via.array.size);
				for (uint32_t index = 0; index < field->via.array.size; ++index)
				{
					UInkWorkspace workspace;
					const bool duplicateBefore = context.duplicateKnownKey;
					if (DecodeWorkspace(field->via.array.ptr[index], workspace, context,
						"headerExtension.workspaces")) extension.workspaces.push_back(std::move(workspace));
					else if (context.duplicateKnownKey != duplicateBefore) return false;
					else
					{
						context.result.provenance.usedFieldFallback = true;
						context.Add(UInkDiagnosticCode::KnownBlockSkipped,
							UInkDiagnosticSeverity::Warning, "headerExtension.workspaces");
					}
					if (context.limitExceeded) return false;
				}
			}
			if (const msgpack::object* field = FindField(object, "extra"))
			{
				UInkExtra extra;
				if (!ReadExtra(*field, extra, context, "headerExtension.extra")) return false;
				extension.extra = std::move(extra);
			}
			return true;
		}

		const UInkWorkspace* FindWorkspace(const UInkDocument& document,
			const std::optional<UInkGuid>& guid) noexcept
		{
			if (!document.headerExtension || !guid) return nullptr;
			for (const UInkWorkspace& workspace : document.headerExtension->workspaces)
			{
				if (workspace.usable && workspace.guid == *guid) return &workspace;
			}
			return nullptr;
		}

		bool HasDevice(const UInkDocument& document, const UInkGuid& guid) noexcept
		{
			if (!document.headerExtension) return false;
			for (const UInkDevice& device : document.headerExtension->devices)
			{
				if (device.usable && device.guid == guid) return true;
			}
			return false;
		}

		bool DecodeViewport(const msgpack::object& object, UInkViewport& viewport,
			DecodeContext& context, const std::string& path)
		{
			if (!ValidateMap(object, { "x", "y", "scale" }, context, path)) return false;
			const msgpack::object* x = FindField(object, "x");
			const msgpack::object* y = FindField(object, "y");
			const msgpack::object* scale = FindField(object, "scale");
			return x && y && scale &&
				ReadFloat32(*x, viewport.x, context, ChildPath(path, "x")) &&
				ReadFloat32(*y, viewport.y, context, ChildPath(path, "y")) &&
				ReadFloat32(*scale, viewport.scale, context, ChildPath(path, "scale")) &&
				std::isfinite(viewport.x) && std::isfinite(viewport.y) &&
				std::isfinite(viewport.scale) && viewport.scale > 0.0f;
		}

		bool DecodeCanvas(const msgpack::object& object, UInkCanvas& canvas,
			const UInkDocument& document, DecodeContext& context)
		{
			const std::string path = "canvas";
			if (!ValidateMap(object, { "type", "workspaceGuid", "deviceGuid", "pageGuid",
				"pageIndex", "pageNumber", "layerIndex", "layerNumber", "slideId",
				"viewport", "extra" }, context, path)) return false;
			const msgpack::object* typeField = FindField(object, "type");
			uint16_t type = 0;
			if (!typeField || !ReadUInt16(*typeField, type, context, "canvas.type") ||
				type != kCanvasType || !context.Charge(256, path)) return false;

			if (document.usesImplicitWorkspace)
			{
				if (FindField(object, "workspaceGuid"))
				{
					context.result.provenance.usedTemporaryIdentity = true;
					context.Add(UInkDiagnosticCode::TemporaryIdentity,
						UInkDiagnosticSeverity::Warning, "canvas.workspaceGuid");
				}
			}
			else
			{
				const msgpack::object* field = FindField(object, "workspaceGuid");
				UInkGuid value;
				if (field && ReadGuid(*field, value, context, "canvas.workspaceGuid"))
				{
					canvas.workspaceGuid = value;
					if (!FindWorkspace(document, canvas.workspaceGuid)) canvas.temporaryWorkspace = true;
				}
				else canvas.temporaryWorkspace = true;
				if (canvas.temporaryWorkspace)
				{
					context.result.provenance.usedTemporaryIdentity = true;
					context.Add(UInkDiagnosticCode::TemporaryIdentity,
						UInkDiagnosticSeverity::Warning, "canvas.workspaceGuid");
				}
			}

			if (document.usesImplicitDevice)
			{
				if (FindField(object, "deviceGuid"))
				{
					context.result.provenance.usedTemporaryIdentity = true;
					context.Add(UInkDiagnosticCode::TemporaryIdentity,
						UInkDiagnosticSeverity::Warning, "canvas.deviceGuid");
				}
			}
			else
			{
				const msgpack::object* field = FindField(object, "deviceGuid");
				UInkGuid value;
				if (field && ReadGuid(*field, value, context, "canvas.deviceGuid"))
				{
					canvas.deviceGuid = value;
					if (!HasDevice(document, value)) canvas.temporaryDevice = true;
				}
				else canvas.temporaryDevice = true;
				if (canvas.temporaryDevice)
				{
					context.result.provenance.usedTemporaryIdentity = true;
					context.Add(UInkDiagnosticCode::TemporaryIdentity,
						UInkDiagnosticSeverity::Warning, "canvas.deviceGuid");
				}
			}

			const msgpack::object* pageGuid = FindField(object, "pageGuid");
			if (!pageGuid || !ReadGuid(*pageGuid, canvas.pageGuid, context, "canvas.pageGuid"))
			{
				canvas.temporaryPage = true;
				context.result.provenance.usedTemporaryIdentity = true;
				context.Add(UInkDiagnosticCode::TemporaryIdentity,
					UInkDiagnosticSeverity::Warning, "canvas.pageGuid");
			}
			const msgpack::object* pageIndex = FindField(object, "pageIndex");
			if (!pageIndex || !ReadUInt32(*pageIndex, canvas.pageIndex, context, "canvas.pageIndex"))
			{
				canvas.pageIndex = static_cast<uint32_t>(document.canvases.size());
				canvas.temporaryPage = true;
				context.result.provenance.usedTemporaryIdentity = true;
				context.Add(UInkDiagnosticCode::TemporaryIdentity,
					UInkDiagnosticSeverity::Warning, "canvas.pageIndex");
			}
			const msgpack::object* pageNumber = FindField(object, "pageNumber");
			if (!pageNumber || !ReadUInt32(*pageNumber, canvas.pageNumber, context, "canvas.pageNumber"))
			{
				canvas.pageNumber = canvas.pageIndex;
				context.result.provenance.usedFieldFallback = true;
				context.Add(UInkDiagnosticCode::FieldFallback,
					UInkDiagnosticSeverity::Warning, "canvas.pageNumber");
			}
			const msgpack::object* layerIndex = FindField(object, "layerIndex");
			if (!layerIndex || !ReadUInt32(*layerIndex, canvas.layerIndex, context, "canvas.layerIndex"))
			{
				canvas.layerIndex = 0;
				canvas.temporaryLayer = true;
				context.result.provenance.usedTemporaryIdentity = true;
				context.Add(UInkDiagnosticCode::TemporaryIdentity,
					UInkDiagnosticSeverity::Warning, "canvas.layerIndex");
			}
			const msgpack::object* layerNumber = FindField(object, "layerNumber");
			if (!layerNumber || !ReadUInt32(*layerNumber, canvas.layerNumber, context, "canvas.layerNumber"))
			{
				canvas.layerNumber = canvas.layerIndex;
				context.result.provenance.usedFieldFallback = true;
				context.Add(UInkDiagnosticCode::FieldFallback,
					UInkDiagnosticSeverity::Warning, "canvas.layerNumber");
			}
			if (const msgpack::object* field = FindField(object, "slideId"))
			{
				int32_t value = 0;
				if (!ReadInt32(*field, value, context, "canvas.slideId")) return false;
				canvas.slideId = value;
			}

			const UInkWorkspace* workspace = FindWorkspace(document, canvas.workspaceGuid);
			const bool presentation = workspace && workspace->workspaceType == 2;
			if (presentation && !canvas.slideId)
			{
				canvas.presentationUnbound = true;
				context.result.provenance.usedTemporaryIdentity = true;
				context.Add(UInkDiagnosticCode::TemporaryIdentity,
					UInkDiagnosticSeverity::Warning, "canvas.slideId");
			}

			if (const msgpack::object* field = FindField(object, "viewport"))
			{
				if (canvas.layerIndex != 0)
				{
					context.result.provenance.usedFieldFallback = true;
					context.Add(UInkDiagnosticCode::FieldFallback,
						UInkDiagnosticSeverity::Warning, "canvas.viewport");
				}
				else
				{
					UInkViewport value;
					if (!DecodeViewport(*field, value, context, "canvas.viewport"))
					{
						if (context.duplicateKnownKey) return false;
						context.result.provenance.usedFieldFallback = true;
						context.Add(UInkDiagnosticCode::FieldFallback,
							UInkDiagnosticSeverity::Warning, "canvas.viewport");
						canvas.viewport = UInkViewport{};
					}
					else canvas.viewport = value;
				}
			}
			else if (canvas.layerIndex == 0) canvas.viewport = UInkViewport{};
			if (const msgpack::object* field = FindField(object, "extra"))
			{
				UInkExtra extra;
				if (!ReadExtra(*field, extra, context, "canvas.extra")) return false;
				canvas.extra = std::move(extra);
			}
			return true;
		}

		bool DecodeInkPoint(const msgpack::object& object, UInkInkPoint& point,
			bool advanced, bool unknownInkType, DecodeContext& context,
			const std::string& path)
		{
			if (!ValidateMap(object, { "x", "y", "width", "color", "opacity" },
				context, path)) return false;
			const msgpack::object* x = FindField(object, "x");
			const msgpack::object* y = FindField(object, "y");
			const msgpack::object* width = FindField(object, "width");
			if (!x || !y || !width ||
				!ReadFloat32(*x, point.x, context, ChildPath(path, "x")) ||
				!ReadFloat32(*y, point.y, context, ChildPath(path, "y")) ||
				!ReadFloat32(*width, point.width, context, ChildPath(path, "width")) ||
				!std::isfinite(point.x) || !std::isfinite(point.y) ||
				!std::isfinite(point.width) || point.width <= 0.0f) return false;
			const msgpack::object* color = FindField(object, "color");
			const msgpack::object* opacity = FindField(object, "opacity");
			if (!color && !opacity) return true;
			// 未知 inkType 必须保留基础几何并降级为普通笔，点级私有样式不参与解释。
			if (unknownInkType) return true;
			if (!advanced || !color || !opacity) return false;
			UInkPointStyle style;
			if (!ReadColor(*color, style.color, context, ChildPath(path, "color")) ||
				!ReadFloat32(*opacity, style.opacity, context, ChildPath(path, "opacity")) ||
				!std::isfinite(style.opacity)) return false;
			if (style.opacity < 0.0f || style.opacity > 1.0f)
			{
				style.opacity = ClampOpacity(style.opacity);
				context.result.provenance.usedFieldFallback = true;
				context.Add(UInkDiagnosticCode::FieldFallback, UInkDiagnosticSeverity::Warning,
					ChildPath(path, "opacity"));
			}
			point.style = std::move(style);
			return true;
		}

		bool DecodeInk(const msgpack::object& object, UInkInk& ink, DecodeContext& context)
		{
			const std::string path = "ink";
			if (!ValidateMap(object, { "type", "contentId", "undoId", "inkType", "color",
				"opacity", "texture", "points", "renderOnlyWhenLatest", "extra" },
				context, path)) return false;
			uint16_t type = 0;
			const msgpack::object* typeField = FindField(object, "type");
			const msgpack::object* contentId = FindField(object, "contentId");
			const msgpack::object* undoId = FindField(object, "undoId");
			const msgpack::object* inkType = FindField(object, "inkType");
			const msgpack::object* color = FindField(object, "color");
			const msgpack::object* opacity = FindField(object, "opacity");
			const msgpack::object* texture = FindField(object, "texture");
			const msgpack::object* points = FindField(object, "points");
			if (!typeField || !contentId || !undoId || !inkType || !color || !opacity ||
				!texture || !points)
			{
				AddMissing(context, path);
				return false;
			}
			if (!ReadUInt16(*typeField, type, context, "ink.type") || type != kInkType ||
				!ReadUInt32(*contentId, ink.contentId, context, "ink.contentId") ||
				!ReadUInt32(*undoId, ink.undoId, context, "ink.undoId") ||
				!ReadInt32(*inkType, ink.declaredInkType, context, "ink.inkType") ||
				!ReadColor(*color, ink.color, context, "ink.color") ||
				!ReadFloat32(*opacity, ink.opacity, context, "ink.opacity") ||
				!ReadInt32(*texture, ink.declaredTexture, context, "ink.texture") ||
				points->type != msgpack::type::ARRAY || points->via.array.size == 0 ||
				!std::isfinite(ink.opacity) || !context.Charge(256, path) ||
				!context.AddPoints(points->via.array.size, "ink.points")) return false;

			const bool unknownInkType = ink.declaredInkType < 0 || ink.declaredInkType > 3;
			switch (ink.declaredInkType)
			{
			case 0: ink.effectiveKind = UInkInkKind::Erase; break;
			case 1: ink.effectiveKind = UInkInkKind::Pen; break;
			case 2: ink.effectiveKind = UInkInkKind::Highlighter; break;
			case 3: ink.effectiveKind = UInkInkKind::AdvancedHighlighter; break;
			default:
				ink.effectiveKind = UInkInkKind::Pen;
				context.result.provenance.usedFieldFallback = true;
				context.Add(UInkDiagnosticCode::FieldFallback, UInkDiagnosticSeverity::Warning,
					"ink.inkType");
				break;
			}
			ink.effectiveTexture = 0;
			if (ink.declaredTexture != 0)
			{
				context.result.provenance.usedFieldFallback = true;
				context.Add(UInkDiagnosticCode::FieldFallback, UInkDiagnosticSeverity::Warning,
					"ink.texture");
			}
			if (ink.opacity < 0.0f || ink.opacity > 1.0f)
			{
				ink.opacity = ClampOpacity(ink.opacity);
				context.result.provenance.usedFieldFallback = true;
				context.Add(UInkDiagnosticCode::FieldFallback, UInkDiagnosticSeverity::Warning,
					"ink.opacity");
			}
			ink.points.reserve(points->via.array.size);
			float absoluteX = 0.0f;
			float absoluteY = 0.0f;
			for (uint32_t index = 0; index < points->via.array.size; ++index)
			{
				UInkInkPoint point;
				if (!DecodeInkPoint(points->via.array.ptr[index], point,
					ink.declaredInkType == 3, unknownInkType, context, "ink.points")) return false;
				if (index == 0)
				{
					absoluteX = point.x;
					absoluteY = point.y;
				}
				else
				{
					absoluteX += point.x;
					absoluteY += point.y;
					if (!std::isfinite(absoluteX) || !std::isfinite(absoluteY)) return false;
					point.x = absoluteX;
					point.y = absoluteY;
				}
				ink.points.push_back(std::move(point));
			}
			if (const msgpack::object* field = FindField(object, "renderOnlyWhenLatest"))
			{
				if (!ReadBool(*field, ink.renderOnlyWhenLatest)) return false;
			}
			if (const msgpack::object* field = FindField(object, "extra"))
			{
				UInkExtra extra;
				if (!ReadExtra(*field, extra, context, "ink.extra")) return false;
				ink.extra = std::move(extra);
			}
			return true;
		}

		bool DecodeShapePoint(const msgpack::object& object, UInkShapePoint& point,
			DecodeContext& context, const std::string& path)
		{
			if (!ValidateMap(object, { "x", "y" }, context, path)) return false;
			const msgpack::object* x = FindField(object, "x");
			const msgpack::object* y = FindField(object, "y");
			return x && y && ReadFloat32(*x, point.x, context, ChildPath(path, "x")) &&
				ReadFloat32(*y, point.y, context, ChildPath(path, "y")) &&
				std::isfinite(point.x) && std::isfinite(point.y);
		}

		bool DecodeShapeGeometry(const msgpack::object& object, int32_t shapeType,
			UInkShapeGeometry& geometry, DecodeContext& context)
		{
			const std::string path = "shape.geometry";
			if (!ValidateMap(object, { "points", "centerX", "centerY", "width", "height",
				"rotation", "cornerRadiusX", "cornerRadiusY", "size", "radius" },
				context, path)) return false;
			if (shapeType == 0 || shapeType == 1 || shapeType == 6)
			{
				const msgpack::object* points = FindField(object, "points");
				if (!points || points->type != msgpack::type::ARRAY) return false;
				const uint32_t minimum = shapeType == 6 ? 3 : 2;
				if (points->via.array.size < minimum || (shapeType == 0 && points->via.array.size != 2) ||
					!context.AddPoints(points->via.array.size, path)) return false;
				UInkLineGeometry line;
				line.points.reserve(points->via.array.size);
				for (uint32_t index = 0; index < points->via.array.size; ++index)
				{
					UInkShapePoint point;
					if (!DecodeShapePoint(points->via.array.ptr[index], point, context, path)) return false;
					line.points.push_back(point);
				}
				geometry = std::move(line);
				return true;
			}

			auto readFinite = [&](std::string_view name, float& value) -> bool
			{
				const msgpack::object* field = FindField(object, name);
				return field && ReadFloat32(*field, value, context, ChildPath(path, name)) &&
					std::isfinite(value);
			};
			auto readOptionalRotation = [&]() -> std::optional<float>
			{
				const msgpack::object* field = FindField(object, "rotation");
				if (!field) return 0.0f;
				float value = 0.0f;
				if (!ReadFloat32(*field, value, context, "shape.geometry.rotation") ||
					!std::isfinite(value)) return std::nullopt;
				return value;
			};

			if (shapeType == 2)
			{
				UInkRectangleGeometry rectangle;
				const std::optional<float> rotation = readOptionalRotation();
				if (!rotation || !readFinite("centerX", rectangle.centerX) ||
					!readFinite("centerY", rectangle.centerY) || !readFinite("width", rectangle.width) ||
					!readFinite("height", rectangle.height) || rectangle.width <= 0.0f ||
					rectangle.height <= 0.0f) return false;
				rectangle.rotation = *rotation;
				const msgpack::object* radiusX = FindField(object, "cornerRadiusX");
				const msgpack::object* radiusY = FindField(object, "cornerRadiusY");
				bool radiiValid = !radiusX && !radiusY;
				if (radiusX && radiusY)
				{
					radiiValid = ReadFloat32(*radiusX, rectangle.cornerRadiusX, context,
						"shape.geometry.cornerRadiusX") &&
						ReadFloat32(*radiusY, rectangle.cornerRadiusY, context,
							"shape.geometry.cornerRadiusY") &&
						std::isfinite(rectangle.cornerRadiusX) && std::isfinite(rectangle.cornerRadiusY) &&
						((rectangle.cornerRadiusX == 0.0f && rectangle.cornerRadiusY == 0.0f) ||
							(rectangle.cornerRadiusX > 0.0f && rectangle.cornerRadiusY > 0.0f &&
								rectangle.cornerRadiusX <= rectangle.width * 0.5f &&
								rectangle.cornerRadiusY <= rectangle.height * 0.5f));
				}
				if (!radiiValid)
				{
					rectangle.cornerRadiusX = 0.0f;
					rectangle.cornerRadiusY = 0.0f;
					context.result.provenance.usedFieldFallback = true;
					context.Add(UInkDiagnosticCode::FieldFallback,
						UInkDiagnosticSeverity::Warning, "shape.geometry.cornerRadiusX");
				}
				geometry = rectangle;
				return true;
			}
			if (shapeType == 3)
			{
				UInkSquareGeometry square;
				const std::optional<float> rotation = readOptionalRotation();
				if (!rotation || !readFinite("centerX", square.centerX) ||
					!readFinite("centerY", square.centerY) || !readFinite("size", square.size) ||
					square.size <= 0.0f) return false;
				square.rotation = *rotation;
				geometry = square;
				return true;
			}
			if (shapeType == 4)
			{
				UInkEllipseGeometry ellipse;
				const std::optional<float> rotation = readOptionalRotation();
				if (!rotation || !readFinite("centerX", ellipse.centerX) ||
					!readFinite("centerY", ellipse.centerY) || !readFinite("width", ellipse.width) ||
					!readFinite("height", ellipse.height) || ellipse.width <= 0.0f ||
					ellipse.height <= 0.0f) return false;
				if (FindField(object, "cornerRadiusX") || FindField(object, "cornerRadiusY"))
				{
					context.result.provenance.usedFieldFallback = true;
					context.Add(UInkDiagnosticCode::FieldFallback,
						UInkDiagnosticSeverity::Warning, "shape.geometry.cornerRadiusX");
				}
				ellipse.rotation = *rotation;
				geometry = ellipse;
				return true;
			}
			if (shapeType == 5)
			{
				UInkCircleGeometry circle;
				if (!readFinite("centerX", circle.centerX) ||
					!readFinite("centerY", circle.centerY) || !readFinite("radius", circle.radius) ||
					circle.radius <= 0.0f) return false;
				geometry = circle;
				return true;
			}
			return false;
		}

		bool DecodeShapeStroke(const msgpack::object& object, bool openShape,
			UInkShapeStroke& stroke, DecodeContext& context)
		{
			const std::string path = "shape.stroke";
			if (!ValidateMap(object, { "color", "opacity", "width", "dashArray", "dashOffset",
				"startMarker", "endMarker" }, context, path)) return false;
			const msgpack::object* color = FindField(object, "color");
			const msgpack::object* opacity = FindField(object, "opacity");
			const msgpack::object* width = FindField(object, "width");
			if (!color || !opacity || !width || !ReadColor(*color, stroke.color, context,
				"shape.stroke.color") ||
				!ReadFloat32(*opacity, stroke.opacity, context, "shape.stroke.opacity") ||
				!ReadFloat32(*width, stroke.width, context, "shape.stroke.width") ||
				!std::isfinite(stroke.opacity) || !std::isfinite(stroke.width) || stroke.width <= 0.0f ||
				!context.Charge(192, path)) return false;
			if (stroke.opacity < 0.0f || stroke.opacity > 1.0f)
			{
				stroke.opacity = ClampOpacity(stroke.opacity);
				context.result.provenance.usedFieldFallback = true;
				context.Add(UInkDiagnosticCode::FieldFallback, UInkDiagnosticSeverity::Warning,
					"shape.stroke.opacity");
			}

			bool dashValid = true;
			if (const msgpack::object* field = FindField(object, "dashArray"))
			{
				dashValid = field->type == msgpack::type::ARRAY &&
					(field->via.array.size == 0 || field->via.array.size % 2 == 0) &&
					context.Charge(static_cast<uint64_t>(field->via.array.size) * sizeof(float),
						"shape.stroke.dashArray");
				float total = 0.0f;
				if (dashValid)
				{
					stroke.dashArray.reserve(field->via.array.size);
					for (uint32_t index = 0; index < field->via.array.size; ++index)
					{
						float value = 0.0f;
						if (!ReadFloat32(field->via.array.ptr[index], value, context,
							"shape.stroke.dashArray") || !std::isfinite(value) || value < 0.0f)
						{
							dashValid = false;
							break;
						}
						stroke.dashArray.push_back(value);
						total += value;
					}
					if (!std::isfinite(total) || (!stroke.dashArray.empty() && total <= 0.0f))
						dashValid = false;
				}
			}
			if (const msgpack::object* field = FindField(object, "dashOffset"))
			{
				if (!ReadFloat32(*field, stroke.dashOffset, context, "shape.stroke.dashOffset") ||
					!std::isfinite(stroke.dashOffset)) dashValid = false;
			}
			if (!dashValid)
			{
				stroke.dashArray.clear();
				stroke.dashOffset = 0.0f;
				context.result.provenance.usedFieldFallback = true;
				context.Add(UInkDiagnosticCode::FieldFallback, UInkDiagnosticSeverity::Warning,
					"shape.stroke.dashArray");
			}

			if (const msgpack::object* field = FindField(object, "startMarker"))
			{
				if (!ReadInt32(*field, stroke.declaredStartMarker, context,
					"shape.stroke.startMarker")) return false;
			}
			if (const msgpack::object* field = FindField(object, "endMarker"))
			{
				if (!ReadInt32(*field, stroke.declaredEndMarker, context,
					"shape.stroke.endMarker")) return false;
			}
			stroke.effectiveStartMarker = openShape && stroke.declaredStartMarker == 1 ? 1 : 0;
			stroke.effectiveEndMarker = openShape && stroke.declaredEndMarker == 1 ? 1 : 0;
			if ((!openShape && (stroke.declaredStartMarker != 0 || stroke.declaredEndMarker != 0)) ||
				(openShape && (stroke.declaredStartMarker < 0 || stroke.declaredStartMarker > 1 ||
					stroke.declaredEndMarker < 0 || stroke.declaredEndMarker > 1)))
			{
				context.result.provenance.usedFieldFallback = true;
				context.Add(UInkDiagnosticCode::FieldFallback, UInkDiagnosticSeverity::Warning,
					"shape.stroke.startMarker");
			}
			return true;
		}

		bool DecodeShapeFill(const msgpack::object& object, UInkShapeFill& fill,
			DecodeContext& context)
		{
			const std::string path = "shape.fill";
			if (!ValidateMap(object, { "fillType", "color", "opacity" }, context, path))
				return false;
			const msgpack::object* type = FindField(object, "fillType");
			const msgpack::object* color = FindField(object, "color");
			const msgpack::object* opacity = FindField(object, "opacity");
			if (!type || !color || !opacity ||
				!ReadInt32(*type, fill.declaredFillType, context, "shape.fill.fillType") ||
				!ReadColor(*color, fill.color, context, "shape.fill.color") ||
				!ReadFloat32(*opacity, fill.opacity, context, "shape.fill.opacity") ||
				!std::isfinite(fill.opacity) || !context.Charge(128, path)) return false;
			if (fill.opacity < 0.0f || fill.opacity > 1.0f)
			{
				fill.opacity = ClampOpacity(fill.opacity);
				context.result.provenance.usedFieldFallback = true;
				context.Add(UInkDiagnosticCode::FieldFallback, UInkDiagnosticSeverity::Warning,
					"shape.fill.opacity");
			}
			if (fill.declaredFillType != 0)
			{
				context.result.provenance.usedFieldFallback = true;
				context.Add(UInkDiagnosticCode::FieldFallback, UInkDiagnosticSeverity::Warning,
					"shape.fill.fillType");
			}
			return true;
		}

		bool DecodeShape(const msgpack::object& object, UInkShape& shape,
			DecodeContext& context)
		{
			const std::string path = "shape";
			if (!ValidateMap(object, { "type", "contentId", "undoId", "shapeType", "geometry",
				"stroke", "fill", "renderOnlyWhenLatest", "extra" }, context, path)) return false;
			uint16_t type = 0;
			const msgpack::object* typeField = FindField(object, "type");
			const msgpack::object* contentId = FindField(object, "contentId");
			const msgpack::object* undoId = FindField(object, "undoId");
			const msgpack::object* shapeType = FindField(object, "shapeType");
			const msgpack::object* geometry = FindField(object, "geometry");
			if (!typeField || !contentId || !undoId || !shapeType || !geometry)
			{
				AddMissing(context, path);
				return false;
			}
			if (!ReadUInt16(*typeField, type, context, "shape.type") || type != kShapeType ||
				!ReadUInt32(*contentId, shape.contentId, context, "shape.contentId") ||
				!ReadUInt32(*undoId, shape.undoId, context, "shape.undoId") ||
				!ReadInt32(*shapeType, shape.declaredShapeType, context, "shape.shapeType") ||
				shape.declaredShapeType < 0 || shape.declaredShapeType > 6 ||
				!DecodeShapeGeometry(*geometry, shape.declaredShapeType, shape.geometry, context) ||
				!context.Charge(256, path)) return false;
			const bool openShape = shape.declaredShapeType == 0 || shape.declaredShapeType == 1;
			if (const msgpack::object* field = FindField(object, "stroke"))
			{
				UInkShapeStroke value;
				if (!DecodeShapeStroke(*field, openShape, value, context))
				{
					if (context.duplicateKnownKey || context.limitExceeded) return false;
					context.result.provenance.usedFieldFallback = true;
					context.Add(UInkDiagnosticCode::FieldFallback,
						UInkDiagnosticSeverity::Warning, "shape.stroke");
				}
				else shape.stroke = std::move(value);
			}
			if (const msgpack::object* field = FindField(object, "fill"))
			{
				if (openShape) return false;
				UInkShapeFill value;
				if (!DecodeShapeFill(*field, value, context))
				{
					if (context.duplicateKnownKey || context.limitExceeded) return false;
					context.result.provenance.usedFieldFallback = true;
					context.Add(UInkDiagnosticCode::FieldFallback,
						UInkDiagnosticSeverity::Warning, "shape.fill");
				}
				else shape.fill = std::move(value);
			}
			if ((openShape && !shape.stroke) || (!openShape && !shape.stroke && !shape.fill))
				return false;
			if (const msgpack::object* field = FindField(object, "renderOnlyWhenLatest"))
			{
				if (!ReadBool(*field, shape.renderOnlyWhenLatest)) return false;
			}
			if (const msgpack::object* field = FindField(object, "extra"))
			{
				UInkExtra extra;
				if (!ReadExtra(*field, extra, context, "shape.extra")) return false;
				shape.extra = std::move(extra);
			}
			return true;
		}

		bool IsVisualMimeType(std::string_view mime) noexcept
		{
			return mime.rfind("image/", 0) == 0 || mime.rfind("video/", 0) == 0 ||
				mime == "application/pdf";
		}

		bool DecodeMedia(const msgpack::object& object, UInkMedia& media,
			DecodeContext& context)
		{
			const std::string path = "media";
			if (!ValidateMap(object, { "type", "contentId", "undoId", "path", "mimeType",
				"width", "height", "transform", "opacity", "pageCount", "pageIndex",
				"autoplay", "loop", "volume", "startTime", "playbackRate", "extra" },
				context, path)) return false;
			uint16_t type = 0;
			const msgpack::object* typeField = FindField(object, "type");
			const msgpack::object* contentId = FindField(object, "contentId");
			const msgpack::object* undoId = FindField(object, "undoId");
			const msgpack::object* mediaPath = FindField(object, "path");
			const msgpack::object* mimeType = FindField(object, "mimeType");
			if (!typeField || !contentId || !undoId || !mediaPath || !mimeType)
			{
				AddMissing(context, path);
				return false;
			}
			if (mediaPath->type == msgpack::type::STR &&
				mediaPath->via.str.size > context.limits.maxMediaPathBytes)
			{
				// 专用路径预算必须在复制字符串到持久模型之前生效。
				context.Add(UInkDiagnosticCode::LimitExceeded,
					UInkDiagnosticSeverity::Fatal, "media.path");
				context.limitExceeded = true;
				return false;
			}
			if (!ReadUInt16(*typeField, type, context, "media.type") || type != kMediaType ||
				!ReadUInt32(*contentId, media.contentId, context, "media.contentId") ||
				!ReadUInt32(*undoId, media.undoId, context, "media.undoId") ||
				!ReadString(*mediaPath, media.path, context, "media.path") ||
				!ReadString(*mimeType, media.mimeType, context, "media.mimeType") ||
				media.path.empty() || media.mimeType.empty() ||
				!context.Charge(256, path)) return false;
			media.pathIsSafe = IsSafeMediaPath(media.path,
				context.limits.maxMediaPathBytes);
			if (!media.pathIsSafe)
				context.Add(UInkDiagnosticCode::UnsafeMediaPath, UInkDiagnosticSeverity::Warning,
					"media.path");

			if (const msgpack::object* field = FindField(object, "width"))
			{
				float value = 0.0f;
				if (!ReadFloat32(*field, value, context, "media.width") ||
					!std::isfinite(value) || value <= 0.0f) return false;
				media.width = value;
			}
			if (const msgpack::object* field = FindField(object, "height"))
			{
				float value = 0.0f;
				if (!ReadFloat32(*field, value, context, "media.height") ||
					!std::isfinite(value) || value <= 0.0f) return false;
				media.height = value;
			}
			if (IsVisualMimeType(media.mimeType) && (!media.width || !media.height)) return false;

			if (const msgpack::object* field = FindField(object, "transform"))
			{
				bool valid = field->type == msgpack::type::ARRAY && field->via.array.size == 6;
				if (valid)
				{
					for (size_t index = 0; index < media.transform.size(); ++index)
					{
						if (!ReadFloat32(field->via.array.ptr[index], media.transform[index], context,
							"media.transform") || !std::isfinite(media.transform[index]))
						{
							valid = false;
							break;
						}
					}
				}
				if (!valid)
				{
					media.transform = { 1.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f };
					context.result.provenance.usedFieldFallback = true;
					context.Add(UInkDiagnosticCode::FieldFallback, UInkDiagnosticSeverity::Warning,
						"media.transform");
				}
			}
			if (const msgpack::object* field = FindField(object, "opacity"))
			{
				if (!ReadFloat32(*field, media.opacity, context, "media.opacity") ||
					!std::isfinite(media.opacity)) return false;
				if (media.opacity < 0.0f || media.opacity > 1.0f)
				{
					media.opacity = ClampOpacity(media.opacity);
					context.result.provenance.usedFieldFallback = true;
					context.Add(UInkDiagnosticCode::FieldFallback,
						UInkDiagnosticSeverity::Warning, "media.opacity");
				}
			}
			if (const msgpack::object* field = FindField(object, "pageCount"))
			{
				uint32_t value = 0;
				if (!ReadUInt32(*field, value, context, "media.pageCount") || value == 0)
				{
					context.result.provenance.usedFieldFallback = true;
					context.Add(UInkDiagnosticCode::FieldFallback,
						UInkDiagnosticSeverity::Warning, "media.pageCount");
				}
				else media.pageCount = value;
			}
			if (const msgpack::object* field = FindField(object, "pageIndex"))
			{
				if (!ReadUInt32(*field, media.pageIndex, context, "media.pageIndex")) return false;
			}
			if (const msgpack::object* field = FindField(object, "autoplay"))
			{
				if (!ReadBool(*field, media.autoplay)) return false;
			}
			if (const msgpack::object* field = FindField(object, "loop"))
			{
				if (!ReadBool(*field, media.loop)) return false;
			}
			if (const msgpack::object* field = FindField(object, "volume"))
			{
				if (!ReadFloat32(*field, media.volume, context, "media.volume") ||
					!std::isfinite(media.volume))
				{
					media.volume = 1.0f;
					context.result.provenance.usedFieldFallback = true;
					context.Add(UInkDiagnosticCode::FieldFallback,
						UInkDiagnosticSeverity::Warning, "media.volume");
				}
				else if (media.volume < 0.0f || media.volume > 1.0f)
				{
					media.volume = ClampOpacity(media.volume);
					context.result.provenance.usedFieldFallback = true;
					context.Add(UInkDiagnosticCode::FieldFallback,
						UInkDiagnosticSeverity::Warning, "media.volume");
				}
			}
			if (const msgpack::object* field = FindField(object, "startTime"))
			{
				if (!ReadFloat64(*field, media.startTime, context, "media.startTime") ||
					!std::isfinite(media.startTime)) return false;
				if (media.startTime < 0.0)
				{
					media.startTime = 0.0;
					context.result.provenance.usedFieldFallback = true;
					context.Add(UInkDiagnosticCode::FieldFallback,
						UInkDiagnosticSeverity::Warning, "media.startTime");
				}
			}
			if (const msgpack::object* field = FindField(object, "playbackRate"))
			{
				if (!ReadFloat32(*field, media.playbackRate, context, "media.playbackRate") ||
					!std::isfinite(media.playbackRate) || media.playbackRate <= 0.0f)
				{
					media.playbackRate = 1.0f;
					context.result.provenance.usedFieldFallback = true;
					context.Add(UInkDiagnosticCode::FieldFallback,
						UInkDiagnosticSeverity::Warning, "media.playbackRate");
				}
			}
			if (const msgpack::object* field = FindField(object, "extra"))
			{
				UInkExtra extra;
				if (!ReadExtra(*field, extra, context, "media.extra")) return false;
				media.extra = std::move(extra);
			}
			context.Add(UInkDiagnosticCode::MediaResourceUnavailable,
				UInkDiagnosticSeverity::Info, "media.path");
			return true;
		}

		using GuidKey = std::array<uint8_t, 16>;
		using OptionalGuidKey = std::optional<GuidKey>;

		OptionalGuidKey ToKey(const std::optional<UInkGuid>& guid)
		{
			return guid ? OptionalGuidKey(guid->Bytes()) : std::nullopt;
		}

		void MarkTemporary(DecodeContext& context, std::string path)
		{
			context.result.provenance.usedTemporaryIdentity = true;
			context.Add(UInkDiagnosticCode::TemporaryIdentity,
				UInkDiagnosticSeverity::Warning, std::move(path));
		}

		void ValidateRegistries(UInkDocument& document, DecodeContext& context)
		{
			if (!document.headerExtension) return;
			UInkHeaderExtension& extension = *document.headerExtension;

			std::map<GuidKey, size_t> devicesByGuid;
			for (size_t index = 0; index < extension.devices.size(); ++index)
			{
				UInkDevice& device = extension.devices[index];
				if (device.guid.IsZero() || !devicesByGuid.emplace(device.guid.Bytes(), index).second)
				{
					device.usable = false;
					MarkTemporary(context, "headerExtension.devices.guid");
					context.Add(UInkDiagnosticCode::DuplicateGuid,
						UInkDiagnosticSeverity::Warning, "headerExtension.devices.guid");
				}
			}

			for (size_t index = 0; index < extension.devices.size(); ++index)
			{
				UInkDevice& device = extension.devices[index];
				if (!device.usable || !device.parentResolved ||
					!std::holds_alternative<UInkWindowDevice>(device.geometry))
					continue;
				std::set<size_t> path;
				size_t current = index;
				for (;;)
				{
					if (!path.insert(current).second)
					{
						extension.devices[current].parentResolved = false;
						MarkTemporary(context, "headerExtension.devices.parentDeviceGuid");
						context.Add(UInkDiagnosticCode::ParentCycle,
							UInkDiagnosticSeverity::Warning,
							"headerExtension.devices.parentDeviceGuid");
						break;
					}
					const UInkWindowDevice* window = std::get_if<UInkWindowDevice>(
						&extension.devices[current].geometry);
					if (!window) break;
					const auto parent = devicesByGuid.find(window->parentDeviceGuid.Bytes());
					if (parent == devicesByGuid.end() ||
						!extension.devices[parent->second].usable)
					{
						extension.devices[current].parentResolved = false;
						MarkTemporary(context, "headerExtension.devices.parentDeviceGuid");
						break;
					}
					current = parent->second;
				}
			}

			std::map<GuidKey, size_t> workspacesByGuid;
			for (size_t index = 0; index < extension.workspaces.size(); ++index)
			{
				UInkWorkspace& workspace = extension.workspaces[index];
				if (workspace.guid.IsZero() ||
					!workspacesByGuid.emplace(workspace.guid.Bytes(), index).second)
				{
					workspace.usable = false;
					MarkTemporary(context, "headerExtension.workspaces.guid");
					context.Add(UInkDiagnosticCode::DuplicateGuid,
						UInkDiagnosticSeverity::Warning, "headerExtension.workspaces.guid");
				}
			}

			for (size_t index = 0; index < extension.workspaces.size(); ++index)
			{
				UInkWorkspace& workspace = extension.workspaces[index];
				if (!workspace.usable || !workspace.parentWorkspaceGuid) continue;
				std::set<size_t> path;
				size_t current = index;
				for (;;)
				{
					if (!path.insert(current).second)
					{
						extension.workspaces[current].parentResolved = false;
						extension.workspaces[current].parentWorkspaceGuid.reset();
						MarkTemporary(context, "headerExtension.workspaces.parentWorkspaceGuid");
						context.Add(UInkDiagnosticCode::ParentCycle,
							UInkDiagnosticSeverity::Warning,
							"headerExtension.workspaces.parentWorkspaceGuid");
						break;
					}
					const std::optional<UInkGuid>& parentGuid =
						extension.workspaces[current].parentWorkspaceGuid;
					if (!parentGuid) break;
					const auto parent = workspacesByGuid.find(parentGuid->Bytes());
					if (parent == workspacesByGuid.end() ||
						!extension.workspaces[parent->second].usable)
					{
						extension.workspaces[current].parentResolved = false;
						extension.workspaces[current].parentWorkspaceGuid.reset();
						MarkTemporary(context, "headerExtension.workspaces.parentWorkspaceGuid");
						break;
					}
					current = parent->second;
				}
			}
		}

		void ValidateContentSequence(UInkCanvas& canvas, DecodeContext& context)
		{
			uint32_t previousUndo = 0;
			bool recovered = false;
			for (size_t index = 0; index < canvas.content.size(); ++index)
			{
				std::visit([&](auto& content)
				{
					const uint32_t expectedContent = static_cast<uint32_t>(index);
					if (content.contentId != expectedContent)
					{
						content.contentId = expectedContent;
						recovered = true;
					}
					if (index == 0 && content.undoId != 0)
					{
						content.undoId = 0;
						recovered = true;
					}
					else if (index != 0 && content.undoId < previousUndo)
					{
						content.undoId = previousUndo;
						recovered = true;
					}
					previousUndo = content.undoId;
				}, canvas.content[index]);
			}
			if (recovered)
			{
				context.result.provenance.contentSequenceRecovered = true;
				context.result.provenance.usedFieldFallback = true;
				context.Add(UInkDiagnosticCode::ContentSequenceRecovered,
					UInkDiagnosticSeverity::Warning, "canvas.content");
			}
		}

		void ValidateCanvasTopology(UInkDocument& document, DecodeContext& context)
		{
			using PageIndexKey = std::pair<OptionalGuidKey, uint32_t>;
			using PageGroupKey = std::tuple<OptionalGuidKey, OptionalGuidKey, GuidKey>;
			using CanvasKey = std::tuple<OptionalGuidKey, OptionalGuidKey, GuidKey, uint32_t>;
			using PresentationSlideKey = std::pair<OptionalGuidKey, int32_t>;

			std::map<GuidKey, std::pair<OptionalGuidKey, uint32_t>> pagesByGuid;
			std::map<PageIndexKey, GuidKey> pagesByIndex;
			std::map<OptionalGuidKey, std::set<uint32_t>> pageIndices;
			std::map<PageGroupKey, std::set<uint32_t>> layerIndices;
			std::set<CanvasKey> canvasKeys;
			std::map<GuidKey, int32_t> presentationSlides;
			std::map<PresentationSlideKey, GuidKey> presentationPagesBySlide;
			std::set<GuidKey> mismatchedPresentationPages;

			for (UInkCanvas& canvas : document.canvases)
			{
				ValidateContentSequence(canvas, context);
				if (canvas.pageGuid.IsZero())
				{
					canvas.temporaryPage = true;
					MarkTemporary(context, "canvas.pageGuid");
				}
				if (canvas.temporaryWorkspace || canvas.temporaryDevice || canvas.temporaryPage)
					continue;

				const OptionalGuidKey workspace = ToKey(canvas.workspaceGuid);
				const OptionalGuidKey device = ToKey(canvas.deviceGuid);
				const GuidKey page = canvas.pageGuid.Bytes();
				const UInkWorkspace* owner = FindWorkspace(document, canvas.workspaceGuid);
				if (owner && owner->workspaceType == 2 && canvas.slideId)
				{
					const auto inserted = presentationSlides.emplace(page, *canvas.slideId);
					if (!inserted.second && inserted.first->second != *canvas.slideId)
						mismatchedPresentationPages.insert(page);
					const auto pageInserted = presentationPagesBySlide.emplace(
						PresentationSlideKey(workspace, *canvas.slideId), page);
					if (!pageInserted.second && pageInserted.first->second != page)
					{
						mismatchedPresentationPages.insert(page);
						mismatchedPresentationPages.insert(pageInserted.first->second);
					}
				}
				const auto guidInserted = pagesByGuid.emplace(page,
					std::make_pair(workspace, canvas.pageIndex));
				if (!guidInserted.second && guidInserted.first->second !=
					std::make_pair(workspace, canvas.pageIndex))
				{
					canvas.temporaryPage = true;
					MarkTemporary(context, "canvas.pageGuid");
					continue;
				}
				const auto indexInserted = pagesByIndex.emplace(
					PageIndexKey(workspace, canvas.pageIndex), page);
				if (!indexInserted.second && indexInserted.first->second != page)
				{
					canvas.temporaryPage = true;
					MarkTemporary(context, "canvas.pageIndex");
					continue;
				}

				pageIndices[workspace].insert(canvas.pageIndex);
				const PageGroupKey group(workspace, device, page);
				layerIndices[group].insert(canvas.layerIndex);
				if (!canvasKeys.emplace(workspace, device, page, canvas.layerIndex).second)
				{
					canvas.temporaryLayer = true;
					MarkTemporary(context, "canvas.layerIndex");
				}
			}

			for (const auto& [workspace, indices] : pageIndices)
			{
				uint32_t expected = 0;
				bool contiguous = true;
				for (const uint32_t index : indices)
				{
					if (index != expected++)
					{
						contiguous = false;
						break;
					}
				}
				if (!contiguous)
				{
					for (UInkCanvas& canvas : document.canvases)
					{
						if (ToKey(canvas.workspaceGuid) == workspace)
							canvas.temporaryPage = true;
					}
					MarkTemporary(context, "canvas.pageIndex");
				}
			}

			for (const auto& [group, indices] : layerIndices)
			{
				uint32_t expected = 0;
				bool contiguous = true;
				for (const uint32_t index : indices)
				{
					if (index != expected++)
					{
						contiguous = false;
						break;
					}
				}
				if (!contiguous)
				{
					for (UInkCanvas& canvas : document.canvases)
					{
						if (PageGroupKey(ToKey(canvas.workspaceGuid), ToKey(canvas.deviceGuid),
							canvas.pageGuid.Bytes()) == group) canvas.temporaryLayer = true;
					}
					MarkTemporary(context, "canvas.layerIndex");
				}
			}

			if (!mismatchedPresentationPages.empty())
			{
				for (UInkCanvas& canvas : document.canvases)
				{
					const UInkWorkspace* owner = FindWorkspace(document, canvas.workspaceGuid);
					if (owner && owner->workspaceType == 2 &&
						mismatchedPresentationPages.contains(canvas.pageGuid.Bytes()))
						canvas.presentationUnbound = true;
				}
				MarkTemporary(context, "canvas.slideId");
			}
		}

		void ValidateWorkspacePages(UInkDocument& document, DecodeContext& context)
		{
			if (!document.headerExtension || document.usesImplicitWorkspace) return;
			for (UInkWorkspace& workspace : document.headerExtension->workspaces)
			{
				if (!workspace.usable) continue;
				bool hasCurrentPage = false;
				for (const UInkCanvas& canvas : document.canvases)
				{
					if (!canvas.temporaryWorkspace && canvas.workspaceGuid &&
						*canvas.workspaceGuid == workspace.guid &&
						canvas.pageIndex == workspace.currentPageIndex)
					{
						hasCurrentPage = true;
						break;
					}
				}
				if (!hasCurrentPage)
				{
					workspace.currentPageIndex = 0;
					context.result.provenance.usedFieldFallback = true;
					context.Add(UInkDiagnosticCode::FieldFallback,
						UInkDiagnosticSeverity::Warning,
						"headerExtension.workspaces.currentPageIndex");
				}
			}
		}

		void ValidateHeaderSnapshot(const UInkDocument& document, DecodeContext& context)
		{
			uint32_t deviceCount = 1;
			uint32_t workspaceCount = 1;
			if (document.headerExtension && !document.usesImplicitDevice)
			{
				deviceCount = static_cast<uint32_t>(std::count_if(
					document.headerExtension->devices.begin(),
					document.headerExtension->devices.end(),
					[](const UInkDevice& device) { return device.usable; }));
			}
			if (document.headerExtension && !document.usesImplicitWorkspace)
			{
				workspaceCount = static_cast<uint32_t>(std::count_if(
					document.headerExtension->workspaces.begin(),
					document.headerExtension->workspaces.end(),
					[](const UInkWorkspace& workspace) { return workspace.usable; }));
			}
			std::set<GuidKey> pages;
			for (const UInkCanvas& canvas : document.canvases)
			{
				if (!canvas.temporaryPage && !canvas.pageGuid.IsZero())
					pages.insert(canvas.pageGuid.Bytes());
			}
			const uint32_t pageCount = static_cast<uint32_t>(pages.size());
			if (document.header.deviceNum != deviceCount ||
				document.header.workspaceNum != workspaceCount ||
				document.header.pageNum != pageCount)
			{
				context.Add(UInkDiagnosticCode::HeaderSnapshotMismatch,
					UInkDiagnosticSeverity::Info, "header");
			}
		}

		void PostValidateDocument(UInkDocument& document, DecodeContext& context)
		{
			ValidateRegistries(document, context);
			ValidateCanvasTopology(document, context);
			ValidateWorkspacePages(document, context);
			ValidateHeaderSnapshot(document, context);
		}

		bool ReadTopLevelType(const msgpack::object& object, uint16_t& type,
			DecodeContext& context)
		{
			if (object.type != msgpack::type::MAP) return false;
			const msgpack::object* typeField = nullptr;
			for (uint32_t index = 0; index < object.via.map.size; ++index)
			{
				const std::optional<std::string_view> key =
					ObjectStringView(object.via.map.ptr[index].key);
				if (!key || *key != "type") continue;
				if (typeField)
				{
					context.duplicateKnownKey = true;
					context.Add(UInkDiagnosticCode::DuplicateKnownKey,
						UInkDiagnosticSeverity::Warning, "type");
					return false;
				}
				typeField = &object.via.map.ptr[index].val;
			}
			return typeField && ReadUInt16(*typeField, type, context, "type");
		}

		size_t ToMsgPackLimit(uint64_t value) noexcept
		{
			return value > static_cast<uint64_t>(std::numeric_limits<size_t>::max())
				? std::numeric_limits<size_t>::max()
				: static_cast<size_t>(value);
		}

		bool CanOfferSafeAppend(const UInkReadResult& result) noexcept
		{
			return !result.provenance.invalidBlockBeforeValidBlock &&
				!result.provenance.usedTemporaryIdentity &&
				!result.provenance.contentSequenceRecovered;
		}

		uint64_t CountDocumentGeometryPoints(const UInkDocument& document) noexcept
		{
			uint64_t total = 0;
			for (const UInkCanvas& canvas : document.canvases)
			{
				for (const UInkContent& content : canvas.content)
				{
					const uint64_t count = std::visit([](const auto& value) -> uint64_t
					{
						using T = std::decay_t<decltype(value)>;
						if constexpr (std::is_same_v<T, UInkInk>) return value.points.size();
						else if constexpr (std::is_same_v<T, UInkShape>)
						{
							if (const UInkLineGeometry* line =
								std::get_if<UInkLineGeometry>(&value.geometry))
								return line->points.size();
						}
						return 0;
					}, content);
					if (total > std::numeric_limits<uint64_t>::max() - count)
						return std::numeric_limits<uint64_t>::max();
					total += count;
				}
			}
			return total;
		}
	}

	UInkReadResult DecodeUInk(std::span<const std::byte> bytes, const UInkReadLimits& limits)
	{
		UInkReadResult result;
		try
		{
			DecodeContext context{ limits, result };
			if (static_cast<uint64_t>(bytes.size()) > limits.maxFileBytes)
			{
				context.Add(UInkDiagnosticCode::LimitExceeded,
					UInkDiagnosticSeverity::Fatal, "file");
				result.status = UInkReadStatus::LimitExceeded;
				return result;
			}
			if (bytes.empty())
			{
				context.Add(UInkDiagnosticCode::InvalidHeader,
					UInkDiagnosticSeverity::Fatal, "header");
				result.status = UInkReadStatus::RejectedHeader;
				result.failedObjectOffset = 0;
				return result;
			}
			if (limits.maxTopLevelObjects == 0)
			{
				context.Add(UInkDiagnosticCode::LimitExceeded,
					UInkDiagnosticSeverity::Fatal, "file.objects");
				result.status = UInkReadStatus::LimitExceeded;
				return result;
			}

			const msgpack::unpack_limit unpackLimits(
				ToMsgPackLimit(limits.maxContainerEntries),
				ToMsgPackLimit(limits.maxContainerEntries),
				ToMsgPackLimit(limits.maxStringOrBinaryBytes),
				ToMsgPackLimit(limits.maxStringOrBinaryBytes),
				ToMsgPackLimit(limits.maxStringOrBinaryBytes),
				limits.maxDepth);

			const ScanResult headerScan = ScanValue(bytes, 0, 1, 0, limits);
			if (headerScan.status == ScanStatus::LimitExceeded)
			{
				context.Add(UInkDiagnosticCode::LimitExceeded,
					UInkDiagnosticSeverity::Fatal, "header");
				result.status = UInkReadStatus::LimitExceeded;
				return result;
			}
			if (headerScan.status != ScanStatus::Complete)
			{
				context.Add(UInkDiagnosticCode::InvalidHeader,
					UInkDiagnosticSeverity::Fatal, "header");
				result.status = UInkReadStatus::RejectedHeader;
				result.failedObjectOffset = 0;
				return result;
			}

			UInkDocument document;
			{
				TransientChargeGuard transient(context, headerScan.transientCharge, "header");
				if (!transient.IsActive())
				{
					result.status = UInkReadStatus::LimitExceeded;
					result.failedObjectOffset = 0;
					return result;
				}
				try
				{
					size_t consumed = 0;
					msgpack::object_handle handle = msgpack::unpack(
						reinterpret_cast<const char*>(bytes.data()), headerScan.end,
						consumed, nullptr, nullptr, unpackLimits);
					if (consumed != headerScan.end ||
						!DecodeHeader(handle.get(), bytes.first(headerScan.end),
							document.header, context))
					{
						// Header 字段同样受资源预算约束，不能把限额耗尽误报成格式损坏。
						if (context.limitExceeded)
						{
							result.status = UInkReadStatus::LimitExceeded;
							result.safeAppendOffset.reset();
							result.failedObjectOffset = 0;
							return result;
						}
						context.Add(UInkDiagnosticCode::InvalidHeader,
							UInkDiagnosticSeverity::Fatal, "header");
						result.status = UInkReadStatus::RejectedHeader;
						result.failedObjectOffset = 0;
						return result;
					}
				}
				catch (const msgpack::unpack_error&)
				{
					context.Add(UInkDiagnosticCode::InvalidHeader,
						UInkDiagnosticSeverity::Fatal, "header");
					result.status = UInkReadStatus::RejectedHeader;
					result.failedObjectOffset = 0;
					return result;
				}
			}

			enum class StreamState : uint8_t { AfterHeader, BeforeCanvas, InCanvas };
			StreamState state = StreamState::AfterHeader;
			size_t position = headerScan.end;
			std::optional<size_t> currentCanvas;
			std::optional<size_t> invalidTailStart;
			result.decodedObjectCount = 1;
			result.validPrefixLength = position;

			auto markInvalid = [&](size_t offset, uint16_t type)
			{
				result.provenance.containsInvalidCompleteBlocks = true;
				result.provenance.requiresSaveAs = true;
				if (!invalidTailStart) invalidTailStart = offset;
				context.Add(UInkDiagnosticCode::KnownBlockSkipped,
					UInkDiagnosticSeverity::Warning, "topLevel." + std::to_string(type));
			};
			auto markValid = [&](size_t end)
			{
				if (invalidTailStart)
				{
					result.provenance.invalidBlockBeforeValidBlock = true;
					invalidTailStart.reset();
				}
				if (!result.provenance.containsInvalidCompleteBlocks)
					result.validPrefixLength = end;
			};

			while (position < bytes.size())
			{
				if (result.decodedObjectCount >= limits.maxTopLevelObjects)
				{
					context.objectOffset = position;
					context.objectIndex = result.decodedObjectCount;
					context.Add(UInkDiagnosticCode::LimitExceeded,
						UInkDiagnosticSeverity::Fatal, "file.objects");
					result.status = UInkReadStatus::LimitExceeded;
					result.document.reset();
					result.safeAppendOffset.reset();
					result.failedObjectOffset = position;
					return result;
				}

				context.objectOffset = position;
				context.objectIndex = result.decodedObjectCount;
				context.duplicateKnownKey = false;
				const ScanResult scanned = ScanValue(bytes, position, 1, position, limits);
				if (scanned.status == ScanStatus::LimitExceeded)
				{
					context.Add(UInkDiagnosticCode::LimitExceeded,
						UInkDiagnosticSeverity::Fatal, "topLevel");
					result.status = UInkReadStatus::LimitExceeded;
					result.document.reset();
					result.safeAppendOffset.reset();
					result.failedObjectOffset = position;
					return result;
				}
				if (scanned.status != ScanStatus::Complete)
				{
					result.provenance.requiresSaveAs = true;
					result.failedObjectOffset = position;
					if (scanned.status == ScanStatus::NeedMore)
					{
						result.status = UInkReadStatus::RecoveredTruncatedTail;
						context.Add(UInkDiagnosticCode::TruncatedTail,
							UInkDiagnosticSeverity::Warning, "tail");
					}
					else
					{
						result.status = UInkReadStatus::StoppedAtCorruptTail;
						context.Add(UInkDiagnosticCode::MalformedMessagePack,
							UInkDiagnosticSeverity::Error, "tail");
					}
					break;
				}
				TransientChargeGuard transient(context, scanned.transientCharge, "topLevel");
				if (!transient.IsActive())
				{
					result.status = UInkReadStatus::LimitExceeded;
					result.document.reset();
					result.safeAppendOffset.reset();
					result.failedObjectOffset = position;
					return result;
				}

				msgpack::object_handle handle;
				try
				{
					size_t consumed = 0;
					handle = msgpack::unpack(
						reinterpret_cast<const char*>(bytes.data() + position),
						scanned.end - position, consumed, nullptr, nullptr, unpackLimits);
					if (consumed != scanned.end - position)
					{
						result.status = UInkReadStatus::StoppedAtCorruptTail;
						result.failedObjectOffset = position;
						context.Add(UInkDiagnosticCode::MalformedMessagePack,
							UInkDiagnosticSeverity::Error, "topLevel");
						break;
					}
				}
				catch (const msgpack::size_overflow&)
				{
					context.Add(UInkDiagnosticCode::LimitExceeded,
						UInkDiagnosticSeverity::Fatal, "topLevel");
					result.status = UInkReadStatus::LimitExceeded;
					result.document.reset();
					result.safeAppendOffset.reset();
					result.failedObjectOffset = position;
					return result;
				}
				catch (const msgpack::unpack_error&)
				{
					result.status = UInkReadStatus::StoppedAtCorruptTail;
					result.failedObjectOffset = position;
					context.Add(UInkDiagnosticCode::MalformedMessagePack,
						UInkDiagnosticSeverity::Error, "topLevel");
					break;
				}

				++result.decodedObjectCount;
				uint16_t type = std::numeric_limits<uint16_t>::max();
				const UInkLoadProvenance provenanceBeforeBlock = result.provenance;
				bool valid = ReadTopLevelType(handle.get(), type, context);
				if (valid && state == StreamState::AfterHeader && type != kHeaderExtensionType)
					state = StreamState::BeforeCanvas;

				if (valid && type == kHeaderExtensionType)
				{
					if (state != StreamState::AfterHeader || document.headerExtension)
						valid = false;
					else
					{
						UInkHeaderExtension extension;
						valid = DecodeHeaderExtension(handle.get(), extension, context);
						if (valid)
						{
							document.usesImplicitDevice = extension.devices.empty();
							document.usesImplicitWorkspace = extension.workspaces.empty();
							document.headerExtension = std::move(extension);
							state = StreamState::BeforeCanvas;
						}
					}
				}
				else if (valid && type == kCanvasType)
				{
					// 即使 Canvas 字段无效，它也已经结束了前一个内容作用域。
					currentCanvas.reset();
					state = StreamState::BeforeCanvas;
					UInkCanvas canvas;
					valid = DecodeCanvas(handle.get(), canvas, document, context);
					if (valid)
					{
						document.canvases.push_back(std::move(canvas));
						currentCanvas = document.canvases.size() - 1;
						state = StreamState::InCanvas;
					}
				}
				else if (valid && type == kInkType)
				{
					if (state != StreamState::InCanvas || !currentCanvas) valid = false;
					else
					{
						UInkInk ink;
						valid = DecodeInk(handle.get(), ink, context);
						if (valid) document.canvases[*currentCanvas].content.emplace_back(std::move(ink));
					}
				}
				else if (valid && type == kShapeType)
				{
					if (state != StreamState::InCanvas || !currentCanvas) valid = false;
					else
					{
						UInkShape shape;
						valid = DecodeShape(handle.get(), shape, context);
						if (valid) document.canvases[*currentCanvas].content.emplace_back(std::move(shape));
					}
				}
				else if (valid && type == kMediaType)
				{
					if (state != StreamState::InCanvas || !currentCanvas) valid = false;
					else
					{
						UInkMedia media;
						valid = DecodeMedia(handle.get(), media, context);
						if (valid) document.canvases[*currentCanvas].content.emplace_back(std::move(media));
					}
				}
				else if (valid && type == kHeaderType)
				{
					valid = false;
				}
				else if (valid)
				{
					result.provenance.containsUnknownTopLevel = true;
					result.provenance.requiresSaveAs = true;
					context.Add(UInkDiagnosticCode::UnknownTopLevelType,
						UInkDiagnosticSeverity::Warning, "type");
				}

				if (context.limitExceeded)
				{
					result.status = UInkReadStatus::LimitExceeded;
					result.document.reset();
					result.safeAppendOffset.reset();
					result.failedObjectOffset = position;
					return result;
				}
				if (valid) markValid(scanned.end);
				else
				{
					// 被跳过块内产生的临时回退不属于最终文档，只保留块级无效状态。
					result.provenance = provenanceBeforeBlock;
					if (state == StreamState::AfterHeader) state = StreamState::BeforeCanvas;
					markInvalid(position, type);
				}
				position = scanned.end;
			}

			context.objectOffset = position;
			context.objectIndex = result.decodedObjectCount;
			PostValidateDocument(document, context);
			if (context.limitExceeded)
			{
				result.status = UInkReadStatus::LimitExceeded;
				result.document.reset();
				result.safeAppendOffset.reset();
				return result;
			}
			result.geometryPointCount = CountDocumentGeometryPoints(document);

			if (position == bytes.size())
			{
				if (invalidTailStart)
				{
					result.status = UInkReadStatus::RecoveredInvalidTail;
					result.failedObjectOffset = *invalidTailStart;
				}
				else result.status = UInkReadStatus::Complete;
			}
			if (result.provenance.invalidBlockBeforeValidBlock)
				result.provenance.requiresSaveAs = true;
			result.provenance.requiresSaveAs = result.provenance.requiresSaveAs ||
				result.provenance.usedFieldFallback ||
				result.provenance.usedTemporaryIdentity ||
				result.provenance.containsInvalidCompleteBlocks ||
				result.provenance.contentSequenceRecovered;

			if (CanOfferSafeAppend(result))
			{
				if (result.provenance.invalidBlockBeforeValidBlock)
					result.safeAppendOffset.reset();
				else if (invalidTailStart)
					result.safeAppendOffset = *invalidTailStart;
				else if (result.failedObjectOffset)
					result.safeAppendOffset = *result.failedObjectOffset;
				else
					result.safeAppendOffset = bytes.size();
			}
			result.document = std::move(document);
			return result;
		}
		catch (const std::bad_alloc&)
		{
			result.document.reset();
			result.safeAppendOffset.reset();
			result.status = UInkReadStatus::LimitExceeded;
			try
			{
				result.diagnostics.push_back({ UInkDiagnosticCode::LimitExceeded,
					UInkDiagnosticSeverity::Fatal, 0, 0, "allocation", 0 });
			}
			catch (...) {}
			return result;
		}
	}
}
