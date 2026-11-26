module;

#define MSGPACK_NO_BOOST
#include <msgpack.hpp>

#include <algorithm>
#include <array>
#include <bit>
#include <cstring>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <map>
#include <new>
#include <optional>
#include <set>
#include <span>
#include <string>
#include <string_view>
#include <tuple>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

module draw3.uink_codec;

namespace draw3::uink
{
	namespace
	{
		// 结构尚未读完前，按声明数量一次性预留可能在发现坏字节之前就分配大块内存（design §3.3）。
		// 首次预留最多约 1 MiB 的元素，其余按 vector 正常增长；预算仍按声明数量计费，行为不变。
		template<typename T>
		void ReserveDeclared(std::vector<T>& storage, uint64_t declared)
		{
			constexpr uint64_t kEagerBytes = 1024 * 1024;
			const uint64_t eager = (std::max)(uint64_t{ 1 }, kEagerBytes / sizeof(T));
			const uint64_t capped = (std::min)({ declared, eager, static_cast<uint64_t>(storage.max_size()) });
			storage.reserve(static_cast<size_t>(capped));
		}

		enum class ScanStatus : uint8_t
		{
			Complete,
			NeedMore,
			Malformed,
			LimitExceeded
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

		bool AddWireCharge(uint64_t& charge, uint64_t amount) noexcept
		{
			if (charge > std::numeric_limits<uint64_t>::max() - amount) return false;
			charge += amount;
			return true;
		}

		struct WireHeader
		{
			uint8_t marker = 0;
			uint64_t payloadLength = 0;
			uint64_t elementCount = 0;
			size_t payloadStart = 0;
			bool isArray = false;
			bool isMap = false;

			uint64_t ChildCount() const noexcept { return isMap ? elementCount * 2 : elementCount; }
			bool IsString() const noexcept
			{
				return (marker & 0xe0) == 0xa0 || (marker >= 0xd9 && marker <= 0xdb);
			}
		};

		ScanStatus ReadWireHeader(std::span<const std::byte> bytes, size_t& position,
			WireHeader& header) noexcept
		{
			if (!CanRead(position, 1, bytes.size())) return ScanStatus::NeedMore;

			const uint8_t marker = std::to_integer<uint8_t>(bytes[position++]);
			header.marker = marker;
			header.payloadStart = position;
			auto& payloadLength = header.payloadLength;
			auto& elementCount = header.elementCount;
			auto& isArray = header.isArray;
			auto& isMap = header.isMap;

			if (marker <= 0x7f || marker >= 0xe0 || marker == 0xc0 || marker == 0xc2 || marker == 0xc3)
				return ScanStatus::Complete;
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
					--position;
					return ScanStatus::Malformed;
				case 0xc4:
				case 0xd9:
					if (!CanRead(position, 1, bytes.size())) return ScanStatus::NeedMore;
					payloadLength = std::to_integer<uint8_t>(bytes[position++]);
					break;
				case 0xc5:
				case 0xda:
					if (!CanRead(position, 2, bytes.size())) return ScanStatus::NeedMore;
					payloadLength = ReadBigEndian16(bytes, position);
					position += 2;
					break;
				case 0xc6:
				case 0xdb:
					if (!CanRead(position, 4, bytes.size())) return ScanStatus::NeedMore;
					payloadLength = ReadBigEndian32(bytes, position);
					position += 4;
					break;
				case 0xc7:
					if (!CanRead(position, 1, bytes.size())) return ScanStatus::NeedMore;
					payloadLength = std::to_integer<uint8_t>(bytes[position++]);
					if (!CanRead(position, 1, bytes.size())) return ScanStatus::NeedMore;
					++position;
					break;
				case 0xc8:
					if (!CanRead(position, 2, bytes.size())) return ScanStatus::NeedMore;
					payloadLength = ReadBigEndian16(bytes, position);
					position += 2;
					if (!CanRead(position, 1, bytes.size())) return ScanStatus::NeedMore;
					++position;
					break;
				case 0xc9:
					if (!CanRead(position, 4, bytes.size())) return ScanStatus::NeedMore;
					payloadLength = ReadBigEndian32(bytes, position);
					position += 4;
					if (!CanRead(position, 1, bytes.size())) return ScanStatus::NeedMore;
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
					if (!CanRead(position, 2, bytes.size())) return ScanStatus::NeedMore;
					isArray = true;
					elementCount = ReadBigEndian16(bytes, position);
					position += 2;
					break;
				case 0xdd:
					if (!CanRead(position, 4, bytes.size())) return ScanStatus::NeedMore;
					isArray = true;
					elementCount = ReadBigEndian32(bytes, position);
					position += 4;
					break;
				case 0xde:
					if (!CanRead(position, 2, bytes.size())) return ScanStatus::NeedMore;
					isMap = true;
					elementCount = ReadBigEndian16(bytes, position);
					position += 2;
					break;
				case 0xdf:
					if (!CanRead(position, 4, bytes.size())) return ScanStatus::NeedMore;
					isMap = true;
					elementCount = ReadBigEndian32(bytes, position);
					position += 4;
					break;
				default:
					--position;
					return ScanStatus::Malformed;
				}
			}

			header.payloadStart = position;
			return ScanStatus::Complete;
		}

		bool ScanRecoveryValue(std::span<const std::byte> bytes, size_t& position,
			uint64_t& remainingValues, WireHeader& root) noexcept
		{
			// MessagePack 容器只有子项数，无需递归或分配即可寻找完整对象边界。
			uint64_t pending = 1;
			WireHeader* first = &root;
			while (pending != 0)
			{
				if (pending > remainingValues || pending > bytes.size() - position)
					return false;
				--remainingValues;
				WireHeader header;
				if (ReadWireHeader(bytes, position, header) != ScanStatus::Complete ||
					!CanRead(position, header.payloadLength, bytes.size())) return false;
				if (first)
				{
					*first = header;
					first = nullptr;
				}
				position += static_cast<size_t>(header.payloadLength);
				pending = pending - 1 + header.ChildCount();
			}
			return true;
		}

		bool WireValueIsBlockType(std::span<const std::byte> bytes, const WireHeader& value,
			uint16_t type) noexcept
		{
			if (value.marker == type) return true;
			// 识别与字段读取一致的正数整数表示和可无损转换的 float32/float64。
			if (value.marker != 0xca && value.marker != 0xcb &&
				(value.marker < 0xcc || value.marker > 0xd3)) return false;
			uint64_t bits = 0;
			for (const std::byte byte : bytes.subspan(value.payloadStart,
				static_cast<size_t>(value.payloadLength)))
				bits = (bits << 8) | std::to_integer<uint8_t>(byte);
			if (value.marker == 0xca) return bits == std::bit_cast<uint32_t>(static_cast<float>(type));
			if (value.marker == 0xcb) return bits == std::bit_cast<uint64_t>(static_cast<double>(type));
			return bits == type;
		}

		enum class WireType : uint8_t
		{
			Nil, Bool, UInt, Int, Float32, Float64, String, Binary, Ext, Array, Map
		};

		constexpr uint32_t kMaxWireDepth = 64;

		struct ReplayObject {};

		struct WireFailure
		{
			ScanStatus status;
		};

		class WireReader;

		struct WireValue
		{
			WireReader* reader;
			size_t start;
			size_t payload;
			uint32_t size;
			WireType type;
			uint8_t marker;
			int8_t extType = 0;
			uint8_t depth = 0;
			union
			{
				uint64_t u64 = 0;
				int64_t i64;
				double f64;
				bool boolean;
			};

			std::string_view Text() const noexcept;
			std::span<const std::byte> Data() const noexcept;
			bool IsContainer() const noexcept { return type == WireType::Array || type == WireType::Map; }
		};

		// frontier_ 之前的标记已计费。顺序子值推进同一个游标，乱序访问只借用原字节；
		// Frame 只保存尚未闭合的容器，closed_ 让父游标能跨过刚刚消费的子容器。
		class WireReader
		{
		public:
			WireReader(std::span<const std::byte> bytes, size_t objectStart, const UInkReadLimits& limits,
				uint64_t window = (std::numeric_limits<uint64_t>::max)())
				: bytes_(bytes), start_(objectStart), limits_(limits), frontier_(objectStart), window_(window) {}

			WireValue Read(size_t offset)
			{
				if (offset > frontier_ || (offset == frontier_ && complete_))
					throw WireFailure{ ScanStatus::Malformed };
				const bool fresh = offset == frontier_;
				if (fresh) CheckDepth();
				size_t position = offset;
				const WireHeader header = Header(position);
				if (fresh) CheckHeader(header);
				if (!CanRead(position, header.payloadLength, bytes_.size()))
					throw WireFailure{ ScanStatus::NeedMore };
				WireValue value = Borrow(header, offset);
				if (fresh)
				{
					value.depth = static_cast<uint8_t>(depth_ + 1);
					const bool skipsCharge = Advance(header, offset);
					if (header.ChildCount() != 0)
					{
						CheckDepth(); // 非空容器至少需要一层子值，分配模型槽前即可判定。
						// reserve 之前交叉检查：数组每项至少一字节，Map 每对至少两字节。
						// 不可能装下时，先无分配走到首个真正的错误，以保留损坏/截断/超限的优先级。
						const size_t used = frontier_ - start_;
						if (header.ChildCount() > bytes_.size() - frontier_ ||
							used > limits_.maxTopLevelObjectBytes ||
							header.ChildCount() > limits_.maxTopLevelObjectBytes - used)
							Drain(value.depth);
						if (!skipsCharge && header.ChildCount() > window_ - (used - unchargedBytes_))
							throw ReplayObject{};
					}
				}
				return value;
			}

			size_t End(const WireValue& value)
			{
				if (!value.IsContainer()) return value.payload + value.size;
				if (!value.size) return value.payload;
				if (value.depth)
				{
					const size_t slot = value.depth - 1;
					if (depth_ > slot && frames_[slot].start == value.start)
						Drain(value.depth);
					if (closed_[slot].start == value.start) return closed_[slot].end;
				}
				else
				{
					// 乱序字段可能借用已读区的头；跨过它时优先复用当前栈或最近一次闭合边界。
					for (size_t i = 0; i < depth_; ++i)
						if (frames_[i].start == value.start)
						{
							Drain(i + 1);
							return closed_[i].end;
						}
					for (const auto& range : closed_)
						if (range.start == value.start) return range.end;
				}
				// 只有已完整读取的乱序字段会到这里。无分配前移，绝不将越界输入当作完整对象。
				size_t position = value.payload;
				uint64_t pending = value.type == WireType::Map ? uint64_t{ 2 } * value.size : value.size;
				while (pending)
				{
					const WireHeader header = Header(position);
					if (!CanRead(position, header.payloadLength, frontier_))
						throw WireFailure{ ScanStatus::Malformed };
					position += static_cast<size_t>(header.payloadLength);
					pending = pending - 1 + header.ChildCount();
				}
				return position;
			}

			bool TryPlainPoint(const WireValue& value, UInkInkPoint& point)
			{
				if (value.type != WireType::Map || value.marker != 0x83 || value.payload != frontier_ ||
					depth_ == 0 || frames_[depth_ - 1].start != value.start ||
					depth_ >= kMaxWireDepth || depth_ >= limits_.maxDepth ||
					limits_.maxStringOrBinaryBytes < 5 || !CanRead(frontier_, 25, bytes_.size()))
					return false;
				// 编码器的三个 fixstr 键和 float32 标记均已匹配，也就排除了重复键。
				const std::byte* p = bytes_.data() + frontier_;
				if (std::memcmp(p, "\xa1x\xca", 3) != 0 ||
					std::memcmp(p + 7, "\xa1y\xca", 3) != 0 ||
					std::memcmp(p + 14, "\xa5width\xca", 7) != 0)
					return false;
				const float x = std::bit_cast<float>(ReadBigEndian32(bytes_, frontier_ + 3));
				const float y = std::bit_cast<float>(ReadBigEndian32(bytes_, frontier_ + 10));
				const float width = std::bit_cast<float>(ReadBigEndian32(bytes_, frontier_ + 21));
				if (!std::isfinite(x) || !std::isfinite(y) || !std::isfinite(width) || width <= 0.0f)
					return false;
				if (frontier_ + 25 - start_ - unchargedBytes_ > window_) throw ReplayObject{};
				point = { x, y, width, std::nullopt };
				frontier_ += 25;
				// 7 个值的旧临时开销，加键的 7 字节和三个浮点数的 12 字节。
				--depth_;
				RememberEnd(depth_, value.start);
				Finish(7 * 32 + 7 + 12);
				return true;
			}

			uint64_t Charge() const noexcept { return rootCharge_; }
			std::span<const std::byte> Bytes() const noexcept { return bytes_; }

			// 精确重放已取得整块临时预算，字段诊断按 S1 的模式顺序产生。
			bool replaying = false;
			bool legacyUnpackLimit = false;
			bool duplicateType = false;
			bool hasCanvasType = false;

		private:
			enum class WireScope : uint8_t { Ordinary, TopLevel, Registry, RegistryEntry, Extra, RegistryExtra };

			struct Frame
			{
				size_t start;
				uint64_t remaining;
				uint64_t charge;
				WireScope scope;
				WireScope nextValueScope = WireScope::Ordinary;
			};
			struct Closed
			{
				size_t start = (std::numeric_limits<size_t>::max)();
				size_t end = 0;
			};
			std::span<const std::byte> bytes_;
			size_t start_;
			const UInkReadLimits& limits_;
			size_t frontier_;
			uint64_t window_;
			size_t depth_ = 0;
			std::array<Frame, kMaxWireDepth> frames_;
			std::array<Closed, kMaxWireDepth> closed_;
			uint64_t rootCharge_ = 0;
			bool complete_ = false;
			bool seenType_ = false;
			bool typeValuePending_ = false;
			bool hasHeaderExtensionType_ = false;
			uint64_t unchargedBytes_ = 0;
			uint64_t registryExtraBytes_ = 0;
			uint64_t registryExtraCharge_ = 0;

			WireHeader Header(size_t& position) const
			{
				WireHeader header;
				const ScanStatus status = ReadWireHeader(bytes_, position, header);
				if (status != ScanStatus::Complete) throw WireFailure{ status };
				return header;
			}

			void CheckDepth() const
			{
				if (depth_ >= kMaxWireDepth || depth_ >= limits_.maxDepth)
					throw WireFailure{ ScanStatus::LimitExceeded };
			}

			void CheckHeader(const WireHeader& header) const
			{
				if ((header.isArray || header.isMap) && header.elementCount > limits_.maxContainerEntries)
					throw WireFailure{ ScanStatus::LimitExceeded };
				if ((header.IsString() || (header.marker >= 0xc4 && header.marker <= 0xc9) ||
					(header.marker >= 0xd4 && header.marker <= 0xd8)) &&
					header.payloadLength > limits_.maxStringOrBinaryBytes)
					throw WireFailure{ ScanStatus::LimitExceeded };
			}

			WireValue Borrow(const WireHeader& header, size_t start)
			{
				WireValue value{ this, start, header.payloadStart, static_cast<uint32_t>(header.payloadLength),
					WireType::Nil, header.marker };
				const uint8_t marker = header.marker;
				if (header.isMap || header.isArray)
				{
					value.type = header.isMap ? WireType::Map : WireType::Array;
					value.size = static_cast<uint32_t>(header.elementCount);
				}
				else if (header.IsString()) value.type = WireType::String;
				else if (marker >= 0xc4 && marker <= 0xc6) value.type = WireType::Binary;
				else if ((marker >= 0xc7 && marker <= 0xc9) || (marker >= 0xd4 && marker <= 0xd8))
				{
					value.type = WireType::Ext;
					if (marker >= 0xd4)
					{
						++value.payload;
						--value.size;
					}
					value.extType = std::bit_cast<int8_t>(std::to_integer<uint8_t>(bytes_[value.payload - 1]));
				}
				else if (marker == 0xc2 || marker == 0xc3)
				{
					value.type = WireType::Bool;
					value.boolean = marker == 0xc3;
				}
				else if (marker != 0xc0)
				{
					uint64_t bits = marker;
					if (header.payloadLength)
					{
						bits = 0;
						for (const auto byte : bytes_.subspan(header.payloadStart, value.size))
							bits = (bits << 8) | std::to_integer<uint8_t>(byte);
					}
					if (marker == 0xca || marker == 0xcb)
					{
						value.type = marker == 0xca ? WireType::Float32 : WireType::Float64;
						// msgpack-c 会先将 float32 放入 double，通用值（含 NaN）同样走该转换。
						value.f64 = marker == 0xca ? static_cast<double>(std::bit_cast<float>(
							static_cast<uint32_t>(bits))) : std::bit_cast<double>(bits);
					}
					else
					{
						value.type = WireType::UInt;
						value.u64 = bits;
						if (marker >= 0xe0 || (marker >= 0xd0 && marker <= 0xd3))
						{
							const unsigned width = marker >= 0xe0 ? 8u : value.size * 8u;
							const uint64_t sign = uint64_t{ 1 } << (width - 1);
							if (bits & sign)
							{
								value.type = WireType::Int;
								value.i64 = std::bit_cast<int64_t>((bits ^ sign) - sign);
							}
						}
					}
				}
				return value;
			}

			WireScope ReadScope(const WireHeader& header)
			{
				if (depth_ == 0) return header.isMap ? WireScope::TopLevel : WireScope::Ordinary;
				auto& parent = frames_[depth_ - 1];
				if (parent.scope == WireScope::Registry)
					return header.isMap ? WireScope::RegistryEntry : WireScope::Ordinary;
				if (parent.scope != WireScope::TopLevel && parent.scope != WireScope::RegistryEntry)
					return parent.scope;

				if ((parent.remaining & 1) != 0)
				{
					if (parent.scope == WireScope::TopLevel && typeValuePending_)
					{
						hasCanvasType = hasCanvasType || WireValueIsBlockType(bytes_, header, kCanvasType);
						hasHeaderExtensionType_ = WireValueIsBlockType(bytes_, header, kHeaderExtensionType);
						typeValuePending_ = false;
					}
					if (parent.nextValueScope == WireScope::Registry && !header.isArray)
						return WireScope::Ordinary;
					return parent.nextValueScope;
				}

				const std::string_view key = header.IsString() ?
					std::string_view(reinterpret_cast<const char*>(bytes_.data() + header.payloadStart),
						static_cast<size_t>(header.payloadLength)) : std::string_view{};
				parent.nextValueScope = WireScope::Ordinary;
				if (key == "extra")
				{
					// 根块的 extra 直接豁免，候选注册表条目的 extra 等根块闭合后结算。
					parent.nextValueScope = parent.scope == WireScope::TopLevel ?
						WireScope::Extra : WireScope::RegistryExtra;
					return parent.nextValueScope;
				}
				if (parent.scope == WireScope::TopLevel)
				{
					if (key == "devices" || key == "workspaces")
						parent.nextValueScope = WireScope::Registry;
					else if (key == "type")
					{
						typeValuePending_ = true;
						duplicateType = duplicateType || seenType_;
						seenType_ = true;
					}
				}
				return WireScope::Ordinary;
			}

			void RememberEnd(size_t slot, size_t start) noexcept
			{
				closed_[slot] = { start, frontier_ };
			}

			void Finish(uint64_t charge)
			{
				for (;;)
				{
					if (frontier_ - start_ > limits_.maxTopLevelObjectBytes || charge > limits_.maxModelCharge)
						throw WireFailure{ ScanStatus::LimitExceeded };
					if (!depth_)
					{
						// type 可以出现在注册表之后。只有唯一的 Header Extension 类型才免计费。
						if (registryExtraCharge_ && (!hasHeaderExtensionType_ || duplicateType))
						{
							const uint64_t chargedBytes = frontier_ - start_ - (unchargedBytes_ - registryExtraBytes_);
							if (chargedBytes > window_) throw ReplayObject{};
							if (!AddWireCharge(charge, registryExtraCharge_) || charge > limits_.maxModelCharge)
								throw WireFailure{ ScanStatus::LimitExceeded };
						}
						rootCharge_ = charge;
						complete_ = true;
						return;
					}
					Frame& parent = frames_[depth_ - 1];
					if (!AddWireCharge(parent.charge, charge)) throw WireFailure{ ScanStatus::LimitExceeded };
					if (--parent.remaining) return;
					charge = parent.charge;
					--depth_;
					RememberEnd(depth_, parent.start);
				}
			}

			bool Advance(const WireHeader& header, size_t offset)
			{
				const size_t end = header.payloadStart + static_cast<size_t>(header.payloadLength);
				const WireScope scope = ReadScope(header);
				const bool skipsCharge = scope == WireScope::Extra || scope == WireScope::RegistryExtra;
				// 跳过的字节仍计入对象大小，候选注册表条目另记需要补回的字节和模型费用。
				if (skipsCharge) unchargedBytes_ += end - offset;
				if (scope == WireScope::RegistryExtra)
				{
					registryExtraBytes_ += end - offset;
					if (!AddWireCharge(registryExtraCharge_, 32 + header.payloadLength))
						registryExtraCharge_ = (std::numeric_limits<uint64_t>::max)();
				}
				if (end - start_ - unchargedBytes_ > window_) throw ReplayObject{};
				frontier_ = end;
				// 可变长 EXT 的 size_overflow 在旧 unpack（整块临时预算记账以后）才报告。
				// 有意差异：调用方把上限放宽到 4 GiB 以上时，EXT32 载荷加类型字节按 64 位计算判为超限，
				// msgpack-c 会把 2^32 窄化为 0 而放行；默认上限下不可达。
				if (header.marker >= 0xc7 && header.marker <= 0xc9 &&
					header.payloadLength + 1 > limits_.maxStringOrBinaryBytes)
					legacyUnpackLimit = true;
				if constexpr (sizeof(size_t) == 4)
				{
					if ((header.isArray && header.elementCount > (std::numeric_limits<size_t>::max)() / sizeof(msgpack::object)) ||
						(header.isMap && header.elementCount > (std::numeric_limits<size_t>::max)() / sizeof(msgpack::object_kv)) ||
						(header.marker == 0xc9 && header.payloadLength == (std::numeric_limits<uint32_t>::max)()))
						legacyUnpackLimit = true;
				}
				const uint64_t charge = skipsCharge ? 0 : 32 + header.payloadLength;
				if (header.ChildCount())
					frames_[depth_++] = { offset, header.ChildCount(), charge, scope };
				else
					Finish(charge);
				return skipsCharge;
			}

			void Drain(size_t targetDepth)
			{
				// 只弹出指定容器；结构无效和语义无效由调用层分别处理。
				while (depth_ >= targetDepth)
				{
					CheckDepth();
					const size_t offset = frontier_;
					size_t position = offset;
					const WireHeader header = Header(position);
					CheckHeader(header);
					if (!CanRead(position, header.payloadLength, bytes_.size()))
						throw WireFailure{ ScanStatus::NeedMore };
					Advance(header, offset);
				}
			}
		};

		std::string_view WireValue::Text() const noexcept
		{
			return { reinterpret_cast<const char*>(reader->Bytes().data() + payload), size };
		}

		std::span<const std::byte> WireValue::Data() const noexcept
		{
			return reader->Bytes().subspan(payload, size);
		}

		class WireCursor
		{
		public:
			explicit WireCursor(const WireValue& container)
				: reader_(container.reader), next_(container.payload) {}

			WireValue Next()
			{
				if (previous_) next_ = reader_->End(*previous_);
				previous_ = reader_->Read(next_);
				return *previous_;
			}

		private:
			WireReader* reader_;
			size_t next_;
			std::optional<WireValue> previous_;
		};

		struct RecoveryBoundary
		{
			size_t end;
			bool isCanvas;
		};

		std::optional<RecoveryBoundary> FindRecoveryBoundary(std::span<const std::byte> bytes,
			size_t position, bool* recognizedCanvas = nullptr) noexcept
		{
			// 旧恢复路径保留原扫描预算；页段查重只做无分配的结构遍历，受剩余字节数限制。
			uint64_t remainingValues = recognizedCanvas ? bytes.size() - position : 4000000;
			const size_t start = position;
			WireHeader top;
			if (ReadWireHeader(bytes, position, top) != ScanStatus::Complete)
				return std::nullopt;
			if (!top.isMap)
			{
				position = start;
				if (!ScanRecoveryValue(bytes, position, remainingValues, top)) return std::nullopt;
				return RecoveryBoundary{ position, false };
			}
			--remainingValues;
			// 页段里即使对象未闭合，也需要识别截断前已经出现的 type。
			if (!recognizedCanvas &&
				(top.ChildCount() > remainingValues || top.ChildCount() > bytes.size() - position))
				return std::nullopt;
			bool isCanvas = false;
			for (uint64_t index = 0; index < top.elementCount; ++index)
			{
				WireHeader key, value;
				if (!ScanRecoveryValue(bytes, position, remainingValues, key) ||
					!ScanRecoveryValue(bytes, position, remainingValues, value)) return std::nullopt;
				if (key.IsString() && key.payloadLength == 4 &&
					std::string_view(reinterpret_cast<const char*>(bytes.data() + key.payloadStart),
						4) == "type" && WireValueIsBlockType(bytes, value, kCanvasType))
				{
					isCanvas = true;
					if (recognizedCanvas) *recognizedCanvas = true;
				}
			}
			return RecoveryBoundary{ position, isCanvas };
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

		struct DecodeLocation
		{
			uint64_t objectOffset = 0;
			uint64_t objectIndex = 0;
			uint64_t canvasOrdinal = 0;
		};

		enum class BlockLimit : uint8_t { None, Skip, Stop };

		struct DecodeBudget
		{
			uint64_t totalPoints = 0;
			uint64_t persistentCharge = 0;
		};

		// 路径片段借用调用栈上的前缀，仅在保存诊断明细时拼成字符串。
		struct FieldPath
		{
			const FieldPath* parent = nullptr;
			std::string_view part;

			FieldPath() = default;
			FieldPath(const char* text) : part(text) {}
			FieldPath(std::string_view text) : part(text) {}
			FieldPath(const std::string& text) : part(text) {}
			FieldPath(const FieldPath& prefix, std::string_view key) : parent(&prefix), part(key) {}

			void AppendTo(std::string& text) const
			{
				if (parent) parent->AppendTo(text);
				if (!text.empty()) text.push_back('.');
				text.append(part);
			}

			std::string String() const
			{
				std::string text;
				AppendTo(text);
				return text;
			}
		};

		FieldPath ChildPath(const FieldPath& parent, std::string_view key) noexcept
		{
			return { parent, key };
		}

		struct DecodeContext
		{
			const UInkReadLimits& limits;
			UInkReadResult& result;
			DecodeLocation location;
			DecodeBudget budget;
			DecodeBudget blockStart;
			BlockLimit blockLimit = BlockLimit::None;
			bool readingHeader = false;
			bool registryLimitExceeded = false;
			bool duplicateKnownKey = false;
			uint64_t transientCharge = 0;
			uint64_t transientUpperBound = 0;
			uint64_t blockPeakPersistent = 0;
			size_t detailCheckpoint = 0;
			std::optional<UInkDiagnostic> overwrittenDetail;
			uint64_t emittedDiagnostics = 0;

			void Add(UInkDiagnosticCode code, UInkDiagnosticSeverity severity,
				const FieldPath& fieldPath = {}, uint32_t systemError = 0)
			{
				// 汇总独立于明细上限，分配失败时也已记录本次计数。
				Count(code);
				++emittedDiagnostics;
				if (limits.maxDiagnostics == 0) return;
				if (result.diagnostics.size() < limits.maxDiagnostics)
				{
					result.diagnostics.push_back({ code, severity, location.objectOffset,
						location.objectIndex, fieldPath.String(), systemError, location.canvasOrdinal });
					return;
				}
				if (!result.diagnostics.empty() &&
					result.diagnostics.back().code != UInkDiagnosticCode::DiagnosticsTruncated)
				{
					Count(UInkDiagnosticCode::DiagnosticsTruncated);
					if (result.diagnostics.size() == detailCheckpoint)
						overwrittenDetail = std::move(result.diagnostics.back());
					result.diagnostics.back() = { UInkDiagnosticCode::DiagnosticsTruncated,
						UInkDiagnosticSeverity::Warning, location.objectOffset, location.objectIndex,
						{}, 0, location.canvasOrdinal };
				}
			}

			void Count(UInkDiagnosticCode code) noexcept
			{
				auto& entry = result.summary.diagnosticCounts[static_cast<size_t>(code)];
				if (entry.count++ == 0 || location.objectOffset < entry.firstByteOffset)
				{
					entry.code = code;
					entry.firstByteOffset = location.objectOffset;
					entry.firstCanvasOrdinal = location.canvasOrdinal;
				}
			}

			void Fallback(const FieldPath& fieldPath)
			{
				result.provenance.usedFieldFallback = true;
				Add(UInkDiagnosticCode::FieldFallback, UInkDiagnosticSeverity::Warning,
					fieldPath);
			}

			bool HasBlockLimit() const noexcept { return blockLimit != BlockLimit::None; }

			void ExceedBlockLimit(BlockLimit disposition, const FieldPath& fieldPath)
			{
				if (!HasBlockLimit())
					Add(UInkDiagnosticCode::LimitExceeded, readingHeader
						? UInkDiagnosticSeverity::Fatal : UInkDiagnosticSeverity::Warning,
						fieldPath);
				if (blockLimit != BlockLimit::Stop) blockLimit = disposition;
			}

			bool CanCharge(uint64_t amount) const noexcept
			{
				return transientCharge <= limits.maxModelCharge &&
					budget.persistentCharge <= limits.maxModelCharge - transientCharge &&
					amount <= limits.maxModelCharge - transientCharge - budget.persistentCharge;
			}

			bool Charge(uint64_t amount, const FieldPath& fieldPath)
			{
				if (HasBlockLimit()) return false;
				// 对象尚未闭合时以每个剩余字节最多 33 charge 作上界。
				// 只有上界也能容纳本次分配才直接写模型，否则先取得精确值再重放。
				if (transientUpperBound && (!CanCharge(amount) ||
					transientUpperBound > limits.maxModelCharge - budget.persistentCharge - amount))
					throw ReplayObject{};
				if (!CanCharge(amount))
				{
					// 本块独自放得下时，预算耗尽来自已保留的模型，停在这一块。
					const uint64_t remaining = limits.maxModelCharge - transientCharge;
					const uint64_t blockCharge = budget.persistentCharge - blockStart.persistentCharge;
					const bool local = blockCharge > remaining || amount > remaining - blockCharge;
					ExceedBlockLimit(local ? BlockLimit::Skip : BlockLimit::Stop, fieldPath);
					return false;
				}
				budget.persistentCharge += amount;
				blockPeakPersistent = (std::max)(blockPeakPersistent, budget.persistentCharge);
				// 回滚无效子对象的预算时，仍保留读取期间的峰值。
				result.modelCharge = (std::max)(result.modelCharge,
					budget.persistentCharge + transientCharge);
				return true;
			}

			template<typename T>
			bool ReserveStorage(std::vector<T>& storage, const FieldPath& fieldPath)
			{
				if (HasBlockLimit()) return false;
				if (storage.size() < storage.capacity()) return true;
				const size_t capacity = storage.capacity();
				const size_t growth = capacity == 0 ? 1 :
					(std::min)(capacity, storage.max_size() - capacity);
				if (growth == 0)
				{
					ExceedBlockLimit(BlockLimit::Stop, fieldPath);
					return false;
				}
				const uint64_t available = limits.maxModelCharge - transientCharge - budget.persistentCharge;
				// 预算紧张时缩小预留增量，仍一次取得可负担的槽，避免逐项重分配。
				const size_t increment = static_cast<size_t>((std::min)(
					static_cast<uint64_t>(growth), available / sizeof(T)));
				if (!Charge((std::max)(increment, size_t{ 1 }) * sizeof(T), fieldPath))
					return false;
				storage.reserve(capacity + increment);
				return true;
			}

			bool BeginTransient(uint64_t amount) noexcept
			{
				if (!CanCharge(amount)) return false;
				transientCharge = amount;
				result.modelCharge = (std::max)(result.modelCharge,
					budget.persistentCharge + transientCharge);
				return true;
			}

			void EndTransient() noexcept
			{
				transientCharge = 0;
			}

			bool AddPoints(uint64_t amount, const FieldPath& fieldPath)
			{
				if (amount > limits.maxSingleGeometryPoints)
				{
					ExceedBlockLimit(BlockLimit::Skip, fieldPath);
					return false;
				}
				if (amount > limits.maxGeometryPoints ||
					budget.totalPoints > limits.maxGeometryPoints - amount)
				{
					ExceedBlockLimit(BlockLimit::Stop, fieldPath);
					return false;
				}
				if (!Charge(amount * 64, fieldPath)) return false;
				budget.totalPoints += amount;
				return true;
			}
		};

		class TransientChargeGuard
		{
		public:
			TransientChargeGuard(DecodeContext& context, uint64_t amount) : context_(context),
				active_(context.BeginTransient(amount)) {}
			~TransientChargeGuard() { if (active_) context_.EndTransient(); }
			bool IsActive() const noexcept { return active_; }

		private:
			DecodeContext& context_;
			bool active_ = false;
		};

		std::optional<std::string_view> ObjectStringView(const WireValue& value) noexcept
		{
			if (value.type != WireType::String) return std::nullopt;
			return value.Text();
		}

		// 键表由块的模式固定。只缓存已遇到的已知字段，数组由调用方的游标直接消费。
		template<size_t N>
		class MapFields
		{
		public:
			MapFields(const WireValue& object, const std::string_view (&keys)[N])
				: cursor_(object), keys_(keys), remaining_(object.size) {}

			const WireValue* Find(std::string_view key)
			{
				const size_t slot = Slot(key);
				if (slot == N) return nullptr;
				const uint64_t bit = uint64_t{ 1 } << slot;
				while (!(seen_ & bit) && remaining_) NextPair();
				return seen_ & bit ? &values_[slot] : nullptr;
			}

			std::optional<std::string_view> Finish()
			{
				while (remaining_ && !duplicate_) NextPair();
				return duplicate_;
			}

		private:
			static_assert(N <= 64);
			WireCursor cursor_;
			const std::string_view (&keys_)[N];
			uint32_t remaining_;
			uint64_t seen_ = 0;
			std::optional<std::string_view> duplicate_;
			std::array<WireValue, N> values_;

			size_t Slot(std::string_view key) const noexcept
			{
				for (size_t i = 0; i < N; ++i)
					if (key == keys_[i]) return i;
				return N;
			}

			void NextPair()
			{
				const auto key = ObjectStringView(cursor_.Next());
				const WireValue value = cursor_.Next();
				--remaining_;
				if (!key) return;
				const size_t slot = Slot(*key);
				if (slot == N) return;
				const uint64_t bit = uint64_t{ 1 } << slot;
				if (seen_ & bit)
				{
					// TYPE-7 的诊断先于字段解释；回到本块起点，按此顺序只发一次。
					if (!value.reader->replaying) throw ReplayObject{};
					if (!duplicate_) duplicate_ = keys_[slot];
					return;
				}
				seen_ |= bit;
				values_[slot] = value;
			}
		};

		template<size_t N, typename Decode>
		bool ReadMapFields(const WireValue& object, const std::string_view (&keys)[N],
			DecodeContext& context, const FieldPath& path, Decode decode)
		{
			if (object.type != WireType::Map)
			{
				context.Add(UInkDiagnosticCode::InvalidFieldType, UInkDiagnosticSeverity::Warning, path);
				return false;
			}
			MapFields fields(object, keys);
			if (object.reader->replaying)
			{
				if (const auto duplicate = fields.Finish())
				{
					context.Add(UInkDiagnosticCode::DuplicateKnownKey,
						UInkDiagnosticSeverity::Warning, ChildPath(path, *duplicate));
					context.duplicateKnownKey = true;
					return false;
				}
			}
			const bool valid = decode(fields);
			fields.Finish();
			return valid;
		}

		void AddNumericCompatibility(const WireValue& object,
			WireType exactType, DecodeContext& context, const FieldPath& path)
		{
			if (object.type != exactType)
				context.Add(UInkDiagnosticCode::NumericCompatibility,
					UInkDiagnosticSeverity::Info, path);
		}

		bool ReadUInt64(const WireValue& object, uint64_t& value,
			DecodeContext& context, const FieldPath& path,
			WireType exactType = WireType::UInt)
		{
			if (object.type == WireType::UInt)
			{
				value = object.u64;
				AddNumericCompatibility(object, exactType, context, path);
				return true;
			}
			if (object.type == WireType::Float32 || object.type == WireType::Float64)
			{
				const double number = object.f64;
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

		bool ReadUInt32(const WireValue& object, uint32_t& value,
			DecodeContext& context, const FieldPath& path)
		{
			uint64_t wide = 0;
			if (!ReadUInt64(object, wide, context, path) ||
				wide > std::numeric_limits<uint32_t>::max()) return false;
			value = static_cast<uint32_t>(wide);
			return true;
		}

		bool ReadUInt16(const WireValue& object, uint16_t& value,
			DecodeContext& context, const FieldPath& path)
		{
			uint64_t wide = 0;
			if (!ReadUInt64(object, wide, context, path) ||
				wide > std::numeric_limits<uint16_t>::max()) return false;
			value = static_cast<uint16_t>(wide);
			return true;
		}

		bool ReadInt32(const WireValue& object, int32_t& value,
			DecodeContext& context, const FieldPath& path)
		{
			int64_t wide = 0;
			if (object.type == WireType::Int)
				wide = object.i64;
			else if (object.type == WireType::UInt)
			{
				if (object.u64 > static_cast<uint64_t>(std::numeric_limits<int32_t>::max()))
					return false;
				wide = static_cast<int64_t>(object.u64);
			}
			else if (object.type == WireType::Float32 || object.type == WireType::Float64)
			{
				const double number = object.f64;
				if (!std::isfinite(number) || std::trunc(number) != number ||
					number < static_cast<double>(std::numeric_limits<int32_t>::min()) ||
					number > static_cast<double>(std::numeric_limits<int32_t>::max())) return false;
				wide = static_cast<int64_t>(number);
			}
			else return false;
			if (wide < std::numeric_limits<int32_t>::min() ||
				wide > std::numeric_limits<int32_t>::max()) return false;
			value = static_cast<int32_t>(wide);
			if (object.type == WireType::Float32 || object.type == WireType::Float64)
				context.Add(UInkDiagnosticCode::NumericCompatibility,
					UInkDiagnosticSeverity::Info, path);
			return true;
		}

		bool ReadFloat32(const WireValue& object, float& value,
			DecodeContext& context, const FieldPath& path)
		{
			double number = 0.0;
			if (object.type == WireType::Float32 || object.type == WireType::Float64)
				number = object.f64;
			else if (object.type == WireType::UInt)
			{
				const float converted = static_cast<float>(object.u64);
				// 先排除 float 舍入到 2^64 的情况，避免越界转回 uint64_t。
				if (!std::isfinite(converted) ||
					static_cast<double>(converted) >= std::ldexp(1.0, 64) ||
					static_cast<uint64_t>(converted) != object.u64)
					return false;
				value = converted;
				context.Add(UInkDiagnosticCode::NumericCompatibility,
					UInkDiagnosticSeverity::Info, path);
				return true;
			}
			else if (object.type == WireType::Int)
			{
				const float converted = static_cast<float>(object.i64);
				if (!std::isfinite(converted) || static_cast<int64_t>(converted) != object.i64)
					return false;
				value = converted;
				context.Add(UInkDiagnosticCode::NumericCompatibility,
					UInkDiagnosticSeverity::Info, path);
				return true;
			}
			else return false;
			if (std::isfinite(number) && std::abs(number) > std::numeric_limits<float>::max())
				return false;
			const float converted = static_cast<float>(number);
			if (std::isfinite(number) && static_cast<double>(converted) != number) return false;
			if (!std::isfinite(number) && object.type != WireType::Float32) return false;
			value = converted;
			AddNumericCompatibility(object, WireType::Float32, context, path);
			return true;
		}

		bool ReadFloat64(const WireValue& object, double& value,
			DecodeContext& context, const FieldPath& path)
		{
			if (object.type == WireType::Float64 || object.type == WireType::Float32)
				value = object.f64;
			else if (object.type == WireType::UInt)
			{
				value = static_cast<double>(object.u64);
				if (value >= std::ldexp(1.0, 64) ||
					static_cast<uint64_t>(value) != object.u64) return false;
			}
			else if (object.type == WireType::Int)
			{
				value = static_cast<double>(object.i64);
				if (static_cast<int64_t>(value) != object.i64) return false;
			}
			else return false;
			AddNumericCompatibility(object, WireType::Float64, context, path);
			return true;
		}

		bool ReadBool(const WireValue& object, bool& value) noexcept
		{
			if (object.type != WireType::Bool) return false;
			value = object.boolean;
			return true;
		}

		bool ReadString(const WireValue& object, std::string& value,
			DecodeContext& context, const FieldPath& path)
		{
			const std::optional<std::string_view> text = ObjectStringView(object);
			if (!text || !IsValidUtf8(*text) || !context.Charge(32 + text->size(), path)) return false;
			value.assign(text->data(), text->size());
			return true;
		}

		bool ReadGuid(const WireValue& object, UInkGuid& value,
			DecodeContext& context, const FieldPath& path)
		{
			std::string text;
			if (!ReadString(object, text, context, path)) return false;
			const std::optional<UInkGuid> guid = ParseUInkGuid(text);
			if (!guid) return false;
			value = *guid;
			return true;
		}

		bool DecodeGenericValue(const WireValue& object, UInkMessagePackValue& value,
			DecodeContext& context, const FieldPath& path, uint32_t depth)
		{
			if (depth > (std::min)(context.limits.maxDepth, kMaxWireDepth))
			{
				context.ExceedBlockLimit(BlockLimit::Skip, path);
				return false;
			}
			if (!context.Charge(48, path)) return false;
			switch (object.type)
			{
			case WireType::Nil:
				value.value = std::monostate{};
				return true;
			case WireType::Bool:
				value.value = object.boolean;
				return true;
			case WireType::UInt:
				value.value = object.u64;
				return true;
			case WireType::Int:
				value.value = object.i64;
				return true;
			case WireType::Float32:
				value.value = static_cast<float>(object.f64);
				return true;
			case WireType::Float64:
				value.value = object.f64;
				return true;
			case WireType::String:
			{
				const std::string_view text = object.Text();
				if (!IsValidUtf8(text) || !context.Charge(text.size(), path)) return false;
				value.value = std::string(text);
				return true;
			}
			case WireType::Binary:
			{
				const auto bytes = object.Data();
				if (!context.Charge(bytes.size(), path)) return false;
				value.value = std::vector<std::byte>(bytes.begin(), bytes.end());
				return true;
			}
			case WireType::Array:
			{
				if (!context.Charge(static_cast<uint64_t>(object.size) * 16, path)) return false;
				UInkMessagePackValue::Array array;
				ReserveDeclared(array, object.size);
				WireCursor items(object);
				for (uint32_t index = 0; index < object.size; ++index)
				{
					UInkMessagePackValue item;
					if (!DecodeGenericValue(items.Next(), item, context,
						path, depth + 1)) return false;
					array.push_back(std::move(item));
				}
				value.value = std::move(array);
				return true;
			}
			case WireType::Map:
			{
				if (!context.Charge(static_cast<uint64_t>(object.size) * 32, path)) return false;
				UInkMessagePackValue::Map map;
				ReserveDeclared(map, object.size);
				WireCursor items(object);
				for (uint32_t index = 0; index < object.size; ++index)
				{
					UInkMessagePackValue key;
					UInkMessagePackValue item;
					if (!DecodeGenericValue(items.Next(), key, context,
						path, depth + 1) ||
						!DecodeGenericValue(items.Next(), item, context,
							path, depth + 1)) return false;
					map.emplace_back(std::move(key), std::move(item));
				}
				value.value = std::move(map);
				return true;
			}
			case WireType::Ext:
			{
				const auto data = object.Data();
				if (!context.Charge(data.size(), path)) return false;
				UInkMessagePackExtension extension;
				extension.type = object.extType;
				extension.data.assign(data.begin(), data.end());
				value.value = std::move(extension);
				return true;
			}
			default:
				return false;
			}
		}

		bool SkipExtra(const WireValue& object, DecodeContext& context, const FieldPath& path)
		{
			if (object.type != WireType::Map) return false;
			WireCursor items(object);
			for (uint32_t index = 0; index < object.size; ++index)
			{
				const auto key = ObjectStringView(items.Next());
				if (!key || !IsValidUtf8(*key))
				{
					context.Add(key ? UInkDiagnosticCode::InvalidFieldValue :
						UInkDiagnosticCode::InvalidFieldType,
						UInkDiagnosticSeverity::Warning, ChildPath(path, "key"));
					return false;
				}
				// 下次取键时跨过当前值，最后一个值也必须完整通过 wire 检查。
				const WireValue value = items.Next();
				if (index + 1 == object.size) object.reader->End(value);
			}
			return true;
		}

		bool ReadColor(const WireValue& object, UInkColor& color,
			DecodeContext& context, const FieldPath& path)
		{
			static constexpr std::string_view keys[] = { "fallback", "space", "components" };
			return ReadMapFields(object, keys, context, path, [&](auto& fields)
			{
				const WireValue* fallback = fields.Find("fallback");
				if (!fallback || !ReadUInt32(*fallback, color.fallbackRgb, context,
					ChildPath(path, "fallback"))) return false;
				if (color.fallbackRgb > 0xffffff)
				{
					color.fallbackRgb &= 0xffffff;
					context.result.provenance.usedFieldFallback = true;
					context.Add(UInkDiagnosticCode::FieldFallback, UInkDiagnosticSeverity::Warning,
						ChildPath(path, "fallback"));
				}

				const WireValue* space = fields.Find("space");
				const WireValue* components = fields.Find("components");
				if (!space && !components) return true;
				if (!space || !components || space->type != WireType::String ||
					components->type != WireType::Array || components->size != 3)
				{
					context.result.provenance.usedFieldFallback = true;
					context.Add(UInkDiagnosticCode::FieldFallback, UInkDiagnosticSeverity::Warning, path);
					return true;
				}

				const std::string_view colorSpace = space->Text();
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
				WireCursor items(*components);
				for (size_t index = 0; index < extended.components.size(); ++index)
				{
					if (!ReadFloat32(items.Next(), extended.components[index],
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
			});
		}

		float ClampOpacity(float value) noexcept
		{
			return std::clamp(value, 0.0f, 1.0f);
		}

		void AddMissing(DecodeContext& context, const FieldPath& path)
		{
			context.Add(UInkDiagnosticCode::MissingRequiredField,
				UInkDiagnosticSeverity::Warning, path);
		}

		bool DecodeHeader(const std::array<WireValue, 7>& fields, UInkHeader& header, DecodeContext& context)
		{
			// 先核对 GUID 的 wire 位宽；失败时不应留下版本或数值兼容诊断。
			if (fields[2].marker != 0xd9 || fields[2].Text().size() != 36) return false;
			uint16_t type = 0;
			uint16_t version = 0;
			if (!ReadUInt16(fields[0], type, context, "header.type") ||
				type != kHeaderType ||
				!ReadUInt16(fields[1], version, context, "header.version"))
				return false;
			context.result.summary.version = version;
			if (version < kUInkMinReadableVersion || version > kUInkMaxReadableVersion)
			{
				context.Add(UInkDiagnosticCode::UnsupportedVersion,
					UInkDiagnosticSeverity::Fatal, "header.version");
				return false;
			}
			if (!ReadGuid(fields[2], header.guid, context, "header.guid") ||
				!ReadUInt32(fields[3], header.deviceNum, context, "header.deviceNum") ||
				!ReadUInt32(fields[4], header.workspaceNum, context, "header.workspaceNum") ||
				!ReadUInt32(fields[5], header.pageNum, context, "header.pageNum") ||
				!ReadUInt64(fields[6], header.time, context, "header.time"))
				return false;
			return context.Charge(128, "header");
		}

		bool DecodeHardware(const WireValue& object, UInkHardware& hardware,
			DecodeContext& context, const FieldPath& path)
		{
			static constexpr std::string_view keys[] = { "name", "id", "identifiers", "physicalWidth",
				"physicalHeight", "scaleFactor" };
			return ReadMapFields(object, keys, context, path, [&](auto& fields)
			{
				if (!context.Charge(128, path)) return false;
				if (const WireValue* field = fields.Find("name"))
				{
					std::string value;
					if (!ReadString(*field, value, context, ChildPath(path, "name"))) return false;
					hardware.name = std::move(value);
				}
				if (const WireValue* field = fields.Find("id"))
				{
					std::string value;
					if (!ReadString(*field, value, context, ChildPath(path, "id"))) return false;
					hardware.id = std::move(value);
				}
				if (const WireValue* field = fields.Find("identifiers"))
				{
					if (field->type != WireType::Map ||
						!context.Charge(static_cast<uint64_t>(field->size) * 64,
							ChildPath(path, "identifiers"))) return false;
					std::set<std::string> identifierKeys;
					ReserveDeclared(hardware.identifiers, field->size);
					WireCursor items(*field);
					for (uint32_t index = 0; index < field->size; ++index)
					{
						std::string key;
						std::string value;
						if (!ReadString(items.Next(), key, context,
							ChildPath(path, "identifiers")) ||
							!ReadString(items.Next(), value, context,
								ChildPath(path, "identifiers")) ||
							!identifierKeys.insert(key).second) return false;
						hardware.identifiers.emplace_back(std::move(key), std::move(value));
					}
				}
				if (const WireValue* field = fields.Find("physicalWidth"))
				{
					uint32_t value = 0;
					if (!ReadUInt32(*field, value, context, ChildPath(path, "physicalWidth")) ||
						value == 0) return false;
					hardware.physicalWidth = value;
				}
				if (const WireValue* field = fields.Find("physicalHeight"))
				{
					uint32_t value = 0;
					if (!ReadUInt32(*field, value, context, ChildPath(path, "physicalHeight")) ||
						value == 0) return false;
					hardware.physicalHeight = value;
				}
				if (const WireValue* field = fields.Find("scaleFactor"))
				{
					float value = 0.0f;
					if (!ReadFloat32(*field, value, context, ChildPath(path, "scaleFactor")) ||
						!std::isfinite(value) || value <= 0.0f) return false;
					hardware.scaleFactor = value;
				}
				return true;
			});
		}

		bool DecodeDevice(const WireValue& object, UInkDevice& device,
			DecodeContext& context, const FieldPath& path)
		{
			static constexpr std::string_view keys[] = { "guid", "deviceType", "name", "hardware", "extra",
				"x", "y", "width", "height", "parentDeviceGuid", "zIndex" };
			return ReadMapFields(object, keys, context, path, [&](auto& fields)
			{
				const WireValue* guid = fields.Find("guid");
				const WireValue* type = fields.Find("deviceType");
				if (!guid || !type || !ReadGuid(*guid, device.guid, context, ChildPath(path, "guid")) ||
					!ReadInt32(*type, device.deviceType, context, ChildPath(path, "deviceType")))
					return false;
				if (device.deviceType < 0) return false;
				if (!context.Charge(256, path)) return false;
				if (const WireValue* field = fields.Find("name"))
				{
					std::string value;
					if (!ReadString(*field, value, context, ChildPath(path, "name"))) return false;
					device.name = std::move(value);
				}
				if (const WireValue* field = fields.Find("hardware"))
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
				if (const WireValue* field = fields.Find("extra"))
					if (!SkipExtra(*field, context, ChildPath(path, "extra"))) return false;

				if (device.deviceType == 0)
				{
					UInkDisplayDevice display;
					const WireValue* x = fields.Find("x");
					const WireValue* y = fields.Find("y");
					const WireValue* width = fields.Find("width");
					const WireValue* height = fields.Find("height");
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
					const WireValue* parent = fields.Find("parentDeviceGuid");
					const WireValue* x = fields.Find("x");
					const WireValue* y = fields.Find("y");
					const WireValue* width = fields.Find("width");
					const WireValue* height = fields.Find("height");
					const WireValue* zIndex = fields.Find("zIndex");
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
						if (context.HasBlockLimit()) return false;
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
			});
		}

		bool DecodeWorkspace(const WireValue& object, UInkWorkspace& workspace,
			DecodeContext& context, const FieldPath& path)
		{
			static constexpr std::string_view keys[] = { "guid", "workspaceType", "name", "parentWorkspaceGuid",
				"hostId", "currentPageIndex", "extra" };
			return ReadMapFields(object, keys, context, path, [&](auto& fields)
			{
				const WireValue* guid = fields.Find("guid");
				const WireValue* type = fields.Find("workspaceType");
				if (!guid || !type || !ReadGuid(*guid, workspace.guid, context, ChildPath(path, "guid")) ||
					!ReadInt32(*type, workspace.workspaceType, context, ChildPath(path, "workspaceType")))
					return false;
				if (workspace.workspaceType < 0) return false;
				if (!context.Charge(192, path)) return false;
				if (const WireValue* field = fields.Find("name"))
				{
					std::string value;
					if (!ReadString(*field, value, context, ChildPath(path, "name"))) return false;
					workspace.name = std::move(value);
				}
				if (const WireValue* field = fields.Find("parentWorkspaceGuid"))
				{
					UInkGuid value;
					if (!ReadGuid(*field, value, context, ChildPath(path, "parentWorkspaceGuid")))
					{
						if (context.HasBlockLimit()) return false;
						// 无法解析的父 GUID 按规范断开，Workspace 本身及其 Canvas 继续保留。
						workspace.parentResolved = false;
						context.result.provenance.usedTemporaryIdentity = true;
						context.Add(UInkDiagnosticCode::TemporaryIdentity,
							UInkDiagnosticSeverity::Warning, ChildPath(path, "parentWorkspaceGuid"));
					}
					else workspace.parentWorkspaceGuid = value;
				}
				if (const WireValue* field = fields.Find("hostId"))
				{
					std::string value;
					if (!ReadString(*field, value, context, ChildPath(path, "hostId"))) return false;
					workspace.hostId = std::move(value);
				}
				if (const WireValue* field = fields.Find("currentPageIndex"))
				{
					if (!ReadUInt32(*field, workspace.currentPageIndex, context,
						ChildPath(path, "currentPageIndex"))) return false;
				}
				if (const WireValue* field = fields.Find("extra"))
					if (!SkipExtra(*field, context, ChildPath(path, "extra"))) return false;
				if (workspace.workspaceType > 2)
				{
					context.result.provenance.usedFieldFallback = true;
					context.Add(UInkDiagnosticCode::FieldFallback, UInkDiagnosticSeverity::Warning,
						ChildPath(path, "workspaceType"));
				}
				return true;
			});
		}

		bool DecodeHeaderExtension(const WireValue& object,
			UInkHeaderExtension& extension, DecodeContext& context)
		{
			const FieldPath path = "headerExtension";
			static constexpr std::string_view keys[] = { "type", "name", "explanation", "devices",
				"workspaces", "extra" };
			return ReadMapFields(object, keys, context, path, [&](auto& fields)
			{
				const WireValue* typeField = fields.Find("type");
				uint16_t type = 0;
				if (!typeField || !ReadUInt16(*typeField, type, context, "headerExtension.type") ||
					type != kHeaderExtensionType || !context.Charge(192, path)) return false;
				if (const WireValue* field = fields.Find("name"))
				{
					std::string value;
					if (!ReadString(*field, value, context, "headerExtension.name")) return false;
					extension.name = std::move(value);
				}
				if (const WireValue* field = fields.Find("explanation"))
				{
					std::string value;
					if (!ReadString(*field, value, context, "headerExtension.explanation")) return false;
					extension.explanation = std::move(value);
				}
				if (const WireValue* field = fields.Find("devices"))
				{
					if (field->type != WireType::Array) return false;
					if (field->size > kUInkMaxRegistryEntries)
					{
						// 注册表两侧独立受限，保留另一侧和扩展块的其他字段。
						context.registryLimitExceeded = true;
						context.Add(UInkDiagnosticCode::LimitExceeded, UInkDiagnosticSeverity::Warning,
							"headerExtension.devices");
					}
					else
					{
						if (!context.Charge(static_cast<uint64_t>(field->size) * 64,
							"headerExtension.devices")) return false;
						ReserveDeclared(extension.devices, field->size);
						WireCursor items(*field);
						for (uint32_t index = 0; index < field->size; ++index)
						{
							const DecodeBudget beforeEntry = context.budget;
							UInkDevice device;
							if (DecodeDevice(items.Next(), device, context, "headerExtension.devices"))
								extension.devices.push_back(std::move(device));
							else
							{
								context.budget = beforeEntry;
								if (context.duplicateKnownKey || context.HasBlockLimit()) return false;
								context.result.provenance.usedFieldFallback = true;
								context.Add(UInkDiagnosticCode::KnownBlockSkipped,
									UInkDiagnosticSeverity::Warning, "headerExtension.devices");
							}
							if (context.HasBlockLimit()) return false;
						}
					}
				}
				if (const WireValue* field = fields.Find("workspaces"))
				{
					if (field->type != WireType::Array) return false;
					if (field->size > kUInkMaxRegistryEntries)
					{
						// 注册表两侧独立受限，保留另一侧和扩展块的其他字段。
						context.registryLimitExceeded = true;
						context.Add(UInkDiagnosticCode::LimitExceeded, UInkDiagnosticSeverity::Warning,
							"headerExtension.workspaces");
					}
					else
					{
						if (!context.Charge(static_cast<uint64_t>(field->size) * 64,
							"headerExtension.workspaces")) return false;
						ReserveDeclared(extension.workspaces, field->size);
						WireCursor items(*field);
						for (uint32_t index = 0; index < field->size; ++index)
						{
							const DecodeBudget beforeEntry = context.budget;
							UInkWorkspace workspace;
							if (DecodeWorkspace(items.Next(), workspace, context, "headerExtension.workspaces"))
								extension.workspaces.push_back(std::move(workspace));
							else
							{
								context.budget = beforeEntry;
								if (context.duplicateKnownKey || context.HasBlockLimit()) return false;
								context.result.provenance.usedFieldFallback = true;
								context.Add(UInkDiagnosticCode::KnownBlockSkipped,
									UInkDiagnosticSeverity::Warning, "headerExtension.workspaces");
							}
							if (context.HasBlockLimit()) return false;
						}
					}
				}
				if (const WireValue* field = fields.Find("extra"))
					if (!SkipExtra(*field, context, "headerExtension.extra")) return false;
				return true;
			});
		}

		const UInkWorkspace* FindWorkspace(const std::optional<UInkHeaderExtension>& extension,
			const std::optional<UInkGuid>& guid) noexcept
		{
			if (!extension || !guid) return nullptr;
			for (const UInkWorkspace& workspace : extension->workspaces)
			{
				if (workspace.usable && workspace.guid == *guid) return &workspace;
			}
			return nullptr;
		}

		bool HasDevice(const std::optional<UInkHeaderExtension>& extension, const UInkGuid& guid) noexcept
		{
			if (!extension) return false;
			for (const UInkDevice& device : extension->devices)
			{
				if (device.usable && device.guid == guid) return true;
			}
			return false;
		}

		bool DecodeViewport(const WireValue& object, UInkViewport& viewport,
			DecodeContext& context, const FieldPath& path)
		{
			static constexpr std::string_view keys[] = { "x", "y", "scale" };
			return ReadMapFields(object, keys, context, path, [&](auto& fields)
			{
				const WireValue* x = fields.Find("x");
				const WireValue* y = fields.Find("y");
				const WireValue* scale = fields.Find("scale");
				return x && y && scale &&
					ReadFloat32(*x, viewport.x, context, ChildPath(path, "x")) &&
					ReadFloat32(*y, viewport.y, context, ChildPath(path, "y")) &&
					ReadFloat32(*scale, viewport.scale, context, ChildPath(path, "scale")) &&
					std::isfinite(viewport.x) && std::isfinite(viewport.y) &&
					std::isfinite(viewport.scale) && viewport.scale > 0.0f;
			});
		}

		template<size_t N>
		bool ReadOptionalInt32(MapFields<N>& fields, std::string_view key,
			std::optional<int32_t>& declared, DecodeContext& context, const FieldPath& path,
			int32_t maxKnown)
		{
			const WireValue* field = fields.Find(key);
			if (!field) return true;
			int32_t value = 0;
			const FieldPath fieldPath = ChildPath(path, key);
			if (!ReadInt32(*field, value, context, fieldPath)) return false;
			declared = value;
			if (value < 0 || value > maxKnown) context.Fallback(fieldPath);
			return true;
		}

		bool DecodeBackground(const WireValue& object, UInkBackground& background,
			DecodeContext& context)
		{
			const FieldPath path = "canvas.background";
			static constexpr std::string_view keys[] = { "color", "pattern" };
			return ReadMapFields(object, keys, context, path, [&](auto& fields)
			{
				const WireValue* color = fields.Find("color");
				const WireValue* pattern = fields.Find("pattern");
				if (!color || !ReadColor(*color, background.color, context, "canvas.background.color") ||
					(pattern && !ReadInt32(*pattern, background.pattern, context,
						"canvas.background.pattern"))) return false;
				if (background.pattern != EffectiveUInkBackgroundPattern(background))
					context.Fallback("canvas.background.pattern");
				return true;
			});
		}

		bool DecodeCanvas(const WireValue& object, UInkCanvas& canvas,
			const UInkDocument& document, const std::optional<UInkHeaderExtension>& extension,
			DecodeContext& context, bool requireNonzeroPageGuid)
		{
			const FieldPath path = "canvas";
			static constexpr std::string_view keys[] = { "type", "workspaceGuid", "deviceGuid", "pageGuid",
				"pageIndex", "pageNumber", "layerIndex", "layerNumber", "slideId",
				"viewport", "background", "pageState", "pageKind", "binding", "extra" };
			return ReadMapFields(object, keys, context, path, [&](auto& fields)
			{
				const WireValue* typeField = fields.Find("type");
				uint16_t type = 0;
				if (!typeField || !ReadUInt16(*typeField, type, context, "canvas.type") ||
					type != kCanvasType || !context.Charge(256, path)) return false;

				if (document.usesImplicitWorkspace)
				{
					if (fields.Find("workspaceGuid"))
					{
						context.result.provenance.usedTemporaryIdentity = true;
						context.Add(UInkDiagnosticCode::TemporaryIdentity,
							UInkDiagnosticSeverity::Warning, "canvas.workspaceGuid");
					}
				}
				else
				{
					const WireValue* field = fields.Find("workspaceGuid");
					UInkGuid value;
					if (field && ReadGuid(*field, value, context, "canvas.workspaceGuid"))
					{
						canvas.workspaceGuid = value;
						if (!FindWorkspace(extension, canvas.workspaceGuid)) canvas.temporaryWorkspace = true;
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
					if (fields.Find("deviceGuid"))
					{
						context.result.provenance.usedTemporaryIdentity = true;
						context.Add(UInkDiagnosticCode::TemporaryIdentity,
							UInkDiagnosticSeverity::Warning, "canvas.deviceGuid");
					}
				}
				else
				{
					const WireValue* field = fields.Find("deviceGuid");
					UInkGuid value;
					if (field && ReadGuid(*field, value, context, "canvas.deviceGuid"))
					{
						canvas.deviceGuid = value;
						if (!HasDevice(extension, value)) canvas.temporaryDevice = true;
					}
					else canvas.temporaryDevice = true;
					if (canvas.temporaryDevice)
					{
						context.result.provenance.usedTemporaryIdentity = true;
						context.Add(UInkDiagnosticCode::TemporaryIdentity,
							UInkDiagnosticSeverity::Warning, "canvas.deviceGuid");
					}
				}

				const WireValue* pageGuid = fields.Find("pageGuid");
				if (!pageGuid || !ReadGuid(*pageGuid, canvas.pageGuid, context, "canvas.pageGuid") ||
					(requireNonzeroPageGuid && canvas.pageGuid.IsZero()))
				{
					canvas.temporaryPage = true;
					context.result.provenance.usedTemporaryIdentity = true;
					context.Add(UInkDiagnosticCode::TemporaryIdentity,
						UInkDiagnosticSeverity::Warning, "canvas.pageGuid");
				}
				const WireValue* pageIndex = fields.Find("pageIndex");
				if (!pageIndex || !ReadUInt32(*pageIndex, canvas.pageIndex, context, "canvas.pageIndex"))
				{
					canvas.pageIndex = static_cast<uint32_t>(document.canvases.size());
					canvas.temporaryPage = true;
					context.result.provenance.usedTemporaryIdentity = true;
					context.Add(UInkDiagnosticCode::TemporaryIdentity,
						UInkDiagnosticSeverity::Warning, "canvas.pageIndex");
				}
				const WireValue* pageNumber = fields.Find("pageNumber");
				if (!pageNumber || !ReadUInt32(*pageNumber, canvas.pageNumber, context, "canvas.pageNumber"))
				{
					canvas.pageNumber = canvas.pageIndex;
					context.result.provenance.usedFieldFallback = true;
					context.Add(UInkDiagnosticCode::FieldFallback,
						UInkDiagnosticSeverity::Warning, "canvas.pageNumber");
				}
				const WireValue* layerIndex = fields.Find("layerIndex");
				if (!layerIndex || !ReadUInt32(*layerIndex, canvas.layerIndex, context, "canvas.layerIndex"))
				{
					canvas.layerIndex = 0;
					canvas.temporaryLayer = true;
					context.result.provenance.usedTemporaryIdentity = true;
					context.Add(UInkDiagnosticCode::TemporaryIdentity,
						UInkDiagnosticSeverity::Warning, "canvas.layerIndex");
				}
				const WireValue* layerNumber = fields.Find("layerNumber");
				if (!layerNumber || !ReadUInt32(*layerNumber, canvas.layerNumber, context, "canvas.layerNumber"))
				{
					canvas.layerNumber = canvas.layerIndex;
					context.result.provenance.usedFieldFallback = true;
					context.Add(UInkDiagnosticCode::FieldFallback,
						UInkDiagnosticSeverity::Warning, "canvas.layerNumber");
				}
				if (const WireValue* field = fields.Find("slideId"))
				{
					int32_t value = 0;
					if (!ReadInt32(*field, value, context, "canvas.slideId")) return false;
					canvas.slideId = value;
				}

				if (canvas.layerIndex != 0)
				{
					// CANVAS-8：忽略非第 0 层的页面元数据时，整个字段不参与内部 TYPE-7 校验。
					for (std::string_view key : { "viewport", "background", "pageState", "pageKind", "binding" })
						if (fields.Find(key)) context.Fallback(ChildPath(path, key));
				}
				else
				{
					// CANVAS-22：缺失和无效视口都使用默认值，旧场景读取有效视口。
					canvas.viewport = UInkViewport{};
					if (const WireValue* field = fields.Find("viewport"))
					{
						UInkViewport value;
						if (DecodeViewport(*field, value, context, "canvas.viewport"))
							canvas.viewport = value;
						else if (context.duplicateKnownKey) return false;
						else context.Fallback("canvas.viewport");
					}
					if (const WireValue* field = fields.Find("background"))
					{
						UInkBackground value;
						if (DecodeBackground(*field, value, context)) canvas.background = value;
						else if (context.duplicateKnownKey) return false;
						else context.Fallback("canvas.background");
					}
					if (!ReadOptionalInt32(fields, "pageState", canvas.pageState, context, path, 1) ||
						!ReadOptionalInt32(fields, "pageKind", canvas.pageKind, context, path, 1) ||
						!ReadOptionalInt32(fields, "binding", canvas.binding, context, path, 1))
						return false;
				}
				if (const WireValue* field = fields.Find("extra"))
					if (!SkipExtra(*field, context, "canvas.extra")) return false;
				return true;
			});
		}

		bool DecodeInkPoint(const WireValue& object, UInkInkPoint& point,
			bool advanced, bool unknownInkType, DecodeContext& context,
			const FieldPath& path)
		{
			if (object.reader->TryPlainPoint(object, point)) return true;
			static constexpr std::string_view keys[] = { "x", "y", "width", "color", "opacity" };
			return ReadMapFields(object, keys, context, path, [&](auto& fields)
			{
				const WireValue* x = fields.Find("x");
				const WireValue* y = fields.Find("y");
				const WireValue* width = fields.Find("width");
				if (!x || !y || !width ||
					!ReadFloat32(*x, point.x, context, ChildPath(path, "x")) ||
					!ReadFloat32(*y, point.y, context, ChildPath(path, "y")) ||
					!ReadFloat32(*width, point.width, context, ChildPath(path, "width")) ||
					!std::isfinite(point.x) || !std::isfinite(point.y) ||
					!std::isfinite(point.width) || point.width <= 0.0f) return false;
				const WireValue* color = fields.Find("color");
				const WireValue* opacity = fields.Find("opacity");
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
			});
		}

		bool DecodeNib(const WireValue& object, UInkNib& nib, DecodeContext& context)
		{
			const FieldPath path = "ink.nib";
			static constexpr std::string_view keys[] = { "shape", "aspect", "orientation" };
			return ReadMapFields(object, keys, context, path, [&](auto& fields)
			{
				std::optional<int32_t> shape;
				std::optional<int32_t> orientation;
				if (!ReadOptionalInt32(fields, "shape", shape, context, path, 1) ||
					!ReadOptionalInt32(fields, "orientation", orientation, context, path, 1))
					return false;
				nib.shape = shape.value_or(0);
				nib.orientation = orientation.value_or(0);
				if (const WireValue* field = fields.Find("aspect"))
				{
					float value = 0.0f;
					if (ReadFloat32(*field, value, context, "ink.nib.aspect")) nib.aspect = value;
					else if (nib.shape != 1) return false;
				}
				// NIB-6：保留声明值；有效笔尖按圆形，且只为 aspect 回退记录一次。
				if (HasInvalidUInkNibAspect(nib)) context.Fallback("ink.nib.aspect");
				return true;
			});
		}

		bool CheckCoordinateRange(double x, double y, DecodeContext& context,
			const FieldPath& path)
		{
			if (std::abs(x) <= kUInkMaxCoordinate && std::abs(y) <= kUInkMaxCoordinate) return true;
			context.Add(UInkDiagnosticCode::CoordinateOutOfRange,
				UInkDiagnosticSeverity::Warning, path);
			return false;
		}

		bool DecodeInkPoints(const WireValue& points, UInkInk& ink, bool unknownInkType,
			DecodeContext& context)
		{
			ReserveDeclared(ink.points, points.size);
			double absoluteX = 0.0;
			double absoluteY = 0.0;
			bool coordinatesInRange = true;
			WireCursor items(points);
			for (uint32_t index = 0; index < points.size; ++index)
			{
				UInkInkPoint point;
				if (!DecodeInkPoint(items.Next(), point,
					ink.declaredInkType == 3, unknownInkType, context, "ink.points")) return false;
				// INK-4：只缩窄存入模型的坐标，后续点从 double 累计值继续。
				absoluteX = index == 0 ? point.x : absoluteX + point.x;
				absoluteY = index == 0 ? point.y : absoluteY + point.y;
				constexpr double kMaxFloat = std::numeric_limits<float>::max();
				if (std::abs(absoluteX) > kMaxFloat || std::abs(absoluteY) > kMaxFloat)
				{
					context.Add(UInkDiagnosticCode::InvalidFieldValue,
						UInkDiagnosticSeverity::Warning, "ink.points");
					return false;
				}
				if (coordinatesInRange)
					coordinatesInRange = CheckCoordinateRange(absoluteX, absoluteY, context, "ink.points");
				point.x = static_cast<float>(absoluteX);
				point.y = static_cast<float>(absoluteY);
				ink.points.push_back(std::move(point));
			}
			return coordinatesInRange;
		}

		bool DecodeInk(const WireValue& object, UInkInk& ink, DecodeContext& context)
		{
			const FieldPath path = "ink";
			static constexpr std::string_view keys[] = { "type", "contentId", "undoId", "inkType", "color",
				"opacity", "texture", "points", "renderOnlyWhenLatest", "extra", "nib", "cap" };
			return ReadMapFields(object, keys, context, path, [&](auto& fields)
			{
				uint16_t type = 0;
				const WireValue* typeField = fields.Find("type");
				const WireValue* contentId = fields.Find("contentId");
				const WireValue* undoId = fields.Find("undoId");
				const WireValue* inkType = fields.Find("inkType");
				const WireValue* color = fields.Find("color");
				const WireValue* opacity = fields.Find("opacity");
				const WireValue* texture = fields.Find("texture");
				const WireValue* points = fields.Find("points");
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
					points->type != WireType::Array || points->size == 0 ||
					!std::isfinite(ink.opacity) ||
					!context.AddPoints(points->size, "ink.points")) return false;

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
				const auto readNibAndCap = [&]()
				{
					if (const WireValue* field = fields.Find("nib"))
					{
						UInkNib value;
						if (!DecodeNib(*field, value, context)) return false;
						ink.nib = value;
					}
					return ReadOptionalInt32(fields, "cap", ink.cap, context, path, 2);
				};
				if (object.reader->replaying && !readNibAndCap()) return false;
				const uint64_t beforePoints = context.emittedDiagnostics;
				const bool pointsValid = DecodeInkPoints(*points, ink, unknownInkType, context);
				if (!object.reader->replaying)
				{
					// 通常 points 在 nib/cap 之前。无诊断的点可直接写模型；
					// 一旦两者的解释顺序可见，就按 S1 的 nib/cap 优先顺序重放。
					if (!pointsValid || context.emittedDiagnostics != beforePoints) throw ReplayObject{};
					if (!readNibAndCap()) return false;
				}
				if (!pointsValid) return false;
				if (const WireValue* field = fields.Find("renderOnlyWhenLatest"))
				{
					if (!ReadBool(*field, ink.renderOnlyWhenLatest)) return false;
				}
				if (const WireValue* field = fields.Find("extra"))
					if (!SkipExtra(*field, context, "ink.extra")) return false;
				return true;
			});
		}

		bool DecodeShapePoint(const WireValue& object, UInkShapePoint& point,
			DecodeContext& context, const FieldPath& path)
		{
			static constexpr std::string_view keys[] = { "x", "y" };
			return ReadMapFields(object, keys, context, path, [&](auto& fields)
			{
				const WireValue* x = fields.Find("x");
				const WireValue* y = fields.Find("y");
				return x && y && ReadFloat32(*x, point.x, context, ChildPath(path, "x")) &&
					ReadFloat32(*y, point.y, context, ChildPath(path, "y")) &&
					std::isfinite(point.x) && std::isfinite(point.y) &&
					CheckCoordinateRange(point.x, point.y, context, path);
			});
		}

		bool DecodeShapeGeometry(const WireValue& object, int32_t shapeType,
			UInkShapeGeometry& geometry, DecodeContext& context)
		{
			const FieldPath path = "shape.geometry";
			static constexpr std::string_view keys[] = { "points", "centerX", "centerY", "width", "height",
				"rotation", "cornerRadiusX", "cornerRadiusY", "size", "radius" };
			return ReadMapFields(object, keys, context, path, [&](auto& fields)
			{
				if (shapeType == 0 || shapeType == 1 || shapeType == 6)
				{
					const WireValue* points = fields.Find("points");
					if (!points || points->type != WireType::Array) return false;
					if (points->size > kUInkMaxShapeVertices)
					{
						context.ExceedBlockLimit(BlockLimit::Skip, path);
						return false;
					}
					const uint32_t minimum = shapeType == 6 ? 3 : 2;
					if (points->size < minimum || (shapeType == 0 && points->size != 2) ||
						!context.AddPoints(points->size, path)) return false;
					UInkLineGeometry line;
					ReserveDeclared(line.points, points->size);
					WireCursor items(*points);
					for (uint32_t index = 0; index < points->size; ++index)
					{
						UInkShapePoint point;
						if (!DecodeShapePoint(items.Next(), point, context, path)) return false;
						line.points.push_back(point);
					}
					geometry = std::move(line);
					return true;
				}

				auto readFinite = [&](std::string_view name, float& value) -> bool
				{
					const WireValue* field = fields.Find(name);
					return field && ReadFloat32(*field, value, context, ChildPath(path, name)) &&
						std::isfinite(value);
				};
				auto readOptionalRotation = [&]() -> std::optional<float>
				{
					const WireValue* field = fields.Find("rotation");
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
					const WireValue* radiusX = fields.Find("cornerRadiusX");
					const WireValue* radiusY = fields.Find("cornerRadiusY");
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
					if (!CheckCoordinateRange(rectangle.centerX, rectangle.centerY, context, path))
						return false;
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
					if (!CheckCoordinateRange(square.centerX, square.centerY, context, path))
						return false;
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
					if (fields.Find("cornerRadiusX") || fields.Find("cornerRadiusY"))
					{
						context.result.provenance.usedFieldFallback = true;
						context.Add(UInkDiagnosticCode::FieldFallback,
							UInkDiagnosticSeverity::Warning, "shape.geometry.cornerRadiusX");
					}
					ellipse.rotation = *rotation;
					if (!CheckCoordinateRange(ellipse.centerX, ellipse.centerY, context, path))
						return false;
					geometry = ellipse;
					return true;
				}
				if (shapeType == 5)
				{
					UInkCircleGeometry circle;
					if (!readFinite("centerX", circle.centerX) ||
						!readFinite("centerY", circle.centerY) || !readFinite("radius", circle.radius) ||
						circle.radius <= 0.0f) return false;
					if (!CheckCoordinateRange(circle.centerX, circle.centerY, context, path))
						return false;
					geometry = circle;
					return true;
				}
				return false;
			});
		}

		bool DecodeShapeStroke(const WireValue& object, bool openShape,
			UInkShapeStroke& stroke, DecodeContext& context)
		{
			const FieldPath path = "shape.stroke";
			static constexpr std::string_view keys[] = { "color", "opacity", "width", "dashArray", "dashOffset",
				"startMarker", "endMarker", "lineCap", "lineJoin" };
			return ReadMapFields(object, keys, context, path, [&](auto& fields)
			{
				const WireValue* color = fields.Find("color");
				const WireValue* opacity = fields.Find("opacity");
				const WireValue* width = fields.Find("width");
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
				if (const WireValue* field = fields.Find("dashArray"))
				{
					dashValid = field->type == WireType::Array &&
						(field->size == 0 || field->size % 2 == 0) &&
						context.Charge(static_cast<uint64_t>(field->size) * sizeof(float),
							"shape.stroke.dashArray");
					float total = 0.0f;
					if (dashValid)
					{
						ReserveDeclared(stroke.dashArray, field->size);
						WireCursor items(*field);
						for (uint32_t index = 0; index < field->size; ++index)
						{
							float value = 0.0f;
							if (!ReadFloat32(items.Next(), value, context,
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
				if (const WireValue* field = fields.Find("dashOffset"))
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

				if (const WireValue* field = fields.Find("startMarker"))
				{
					if (!ReadInt32(*field, stroke.declaredStartMarker, context,
						"shape.stroke.startMarker")) return false;
				}
				if (const WireValue* field = fields.Find("endMarker"))
				{
					if (!ReadInt32(*field, stroke.declaredEndMarker, context,
						"shape.stroke.endMarker")) return false;
				}
				if (!ReadOptionalInt32(fields, "lineCap", stroke.lineCap, context, path, 2) ||
					!ReadOptionalInt32(fields, "lineJoin", stroke.lineJoin, context, path, 2))
					return false;
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
			});
		}

		bool DecodeShapeFill(const WireValue& object, UInkShapeFill& fill,
			DecodeContext& context)
		{
			const FieldPath path = "shape.fill";
			static constexpr std::string_view keys[] = { "fillType", "color", "opacity" };
			return ReadMapFields(object, keys, context, path, [&](auto& fields)
			{
				const WireValue* type = fields.Find("fillType");
				const WireValue* color = fields.Find("color");
				const WireValue* opacity = fields.Find("opacity");
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
			});
		}

		bool DecodeShapeObject(const WireValue& object,
			std::variant<UInkShapeObject, UInkShapeObjectRef>& decoded, DecodeContext& context)
		{
			const FieldPath path = "shape.object";
			static constexpr std::string_view keys[] = { "id", "revision", "preset", "partCount", "transform",
				"construction", "style", "aux", "ref", "partIndex" };
			return ReadMapFields(object, keys, context, path, [&](auto& fields)
			{
				const WireValue* id = fields.Find("id");
				const WireValue* revision = fields.Find("revision");
				const WireValue* partCount = fields.Find("partCount");
				const WireValue* ref = fields.Find("ref");
				const WireValue* partIndex = fields.Find("partIndex");
				if (ref || partIndex)
				{
					UInkShapeObjectRef value;
					if (id || revision || partCount || fields.Find("preset") ||
						fields.Find("transform") || fields.Find("construction") ||
						fields.Find("style") || fields.Find("aux") || !ref || !partIndex ||
						!ReadGuid(*ref, value.ref, context, "shape.object.ref") || value.ref.IsZero() ||
						!ReadUInt32(*partIndex, value.partIndex, context, "shape.object.partIndex") ||
						value.partIndex == 0) return false;
					decoded = value;
					return true;
				}

				UInkShapeObject first;
				if (!id || !revision || !partCount ||
					!ReadGuid(*id, first.id, context, "shape.object.id") || first.id.IsZero() ||
					!ReadUInt64(*revision, first.revision, context, "shape.object.revision") ||
					first.revision == 0 ||
					!ReadUInt32(*partCount, first.partCount, context, "shape.object.partCount") ||
					first.partCount == 0) return false;
				if (const WireValue* field = fields.Find("preset"))
				{
					std::string preset;
					if (!ReadString(*field, preset, context, "shape.object.preset")) return false;
					first.preset = std::move(preset);
				}
				if (const WireValue* field = fields.Find("transform"))
				{
					std::array<float, 6> transform;
					if (field->type != WireType::Array || field->size != transform.size())
						return false;
					WireCursor items(*field);
					for (size_t index = 0; index < transform.size(); ++index)
					{
						if (!ReadFloat32(items.Next(), transform[index], context,
							"shape.object.transform") || !std::isfinite(transform[index])) return false;
					}
					first.transform = transform;
				}
				const auto readMap = [&](std::string_view key, std::optional<UInkMessagePackValue::Map>& target)
				{
					const WireValue* field = fields.Find(key);
					if (!field) return true;
					UInkMessagePackValue value;
					if (field->type != WireType::Map ||
						!DecodeGenericValue(*field, value, context, ChildPath(path, key), 1))
						return false;
					target = std::get<UInkMessagePackValue::Map>(std::move(value.value));
					return true;
				};
				if (!readMap("construction", first.construction) || !readMap("style", first.style))
					return false;
				if (const WireValue* field = fields.Find("aux"))
				{
					uint32_t aux = 0;
					if (!ReadUInt32(*field, aux, context, "shape.object.aux")) return false;
					first.aux = aux;
				}
				decoded = std::move(first);
				return true;
			});
		}

		bool DecodeShape(const WireValue& object, UInkShape& shape,
			DecodeContext& context)
		{
			const FieldPath path = "shape";
			static constexpr std::string_view keys[] = { "type", "contentId", "undoId", "shapeType", "geometry",
				"stroke", "fill", "renderOnlyWhenLatest", "extra", "object" };
			return ReadMapFields(object, keys, context, path, [&](auto& fields)
			{
				uint16_t type = 0;
				const WireValue* typeField = fields.Find("type");
				const WireValue* contentId = fields.Find("contentId");
				const WireValue* undoId = fields.Find("undoId");
				const WireValue* shapeType = fields.Find("shapeType");
				const WireValue* geometry = fields.Find("geometry");
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
					!DecodeShapeGeometry(*geometry, shape.declaredShapeType, shape.geometry, context))
					return false;
				const bool openShape = shape.declaredShapeType == 0 || shape.declaredShapeType == 1;
				if (const WireValue* field = fields.Find("stroke"))
				{
					UInkShapeStroke value;
					if (!DecodeShapeStroke(*field, openShape, value, context))
					{
						if (context.duplicateKnownKey || context.HasBlockLimit()) return false;
						context.result.provenance.usedFieldFallback = true;
						context.Add(UInkDiagnosticCode::FieldFallback,
							UInkDiagnosticSeverity::Warning, "shape.stroke");
					}
					else shape.stroke = std::move(value);
				}
				if (const WireValue* field = fields.Find("fill"))
				{
					if (openShape) return false;
					UInkShapeFill value;
					if (!DecodeShapeFill(*field, value, context))
					{
						if (context.duplicateKnownKey || context.HasBlockLimit()) return false;
						context.result.provenance.usedFieldFallback = true;
						context.Add(UInkDiagnosticCode::FieldFallback,
							UInkDiagnosticSeverity::Warning, "shape.fill");
					}
					else shape.fill = std::move(value);
				}
				if ((openShape && !shape.stroke) || (!openShape && !shape.stroke && !shape.fill))
					return false;
				if (const WireValue* field = fields.Find("renderOnlyWhenLatest"))
				{
					if (!ReadBool(*field, shape.renderOnlyWhenLatest)) return false;
				}
				if (const WireValue* field = fields.Find("extra"))
					if (!SkipExtra(*field, context, "shape.extra")) return false;
				if (const WireValue* field = fields.Find("object"))
				{
					const DecodeBudget beforeObject = context.budget;
					std::variant<UInkShapeObject, UInkShapeObjectRef> value;
					if (DecodeShapeObject(*field, value, context)) shape.object = std::move(value);
					else
					{
						context.budget = beforeObject;
						if (context.HasBlockLimit()) return false;
						// SHAPE-14 优先于 TYPE-7：对象层重复已知键时，部件仍可独立绘制。
						context.duplicateKnownKey = false;
						context.Fallback("shape.object");
					}
				}
				return true;
			});
		}

		bool IsVisualMimeType(std::string_view mime) noexcept
		{
			return mime.rfind("image/", 0) == 0 || mime.rfind("video/", 0) == 0 ||
				mime == "application/pdf";
		}

		bool DecodeMedia(const WireValue& object, UInkMedia& media,
			DecodeContext& context)
		{
			const FieldPath path = "media";
			static constexpr std::string_view keys[] = { "type", "contentId", "undoId", "path", "mimeType",
				"width", "height", "transform", "opacity", "pageCount", "pageIndex",
				"autoplay", "loop", "volume", "startTime", "playbackRate", "extra" };
			return ReadMapFields(object, keys, context, path, [&](auto& fields)
			{
				uint16_t type = 0;
				const WireValue* typeField = fields.Find("type");
				const WireValue* contentId = fields.Find("contentId");
				const WireValue* undoId = fields.Find("undoId");
				const WireValue* mediaPath = fields.Find("path");
				const WireValue* mimeType = fields.Find("mimeType");
				if (!typeField || !contentId || !undoId || !mediaPath || !mimeType)
				{
					AddMissing(context, path);
					return false;
				}
				if (mediaPath->type == WireType::String &&
					mediaPath->size > context.limits.maxMediaPathBytes)
				{
					// 专用路径预算必须在复制字符串到持久模型之前生效。
					context.ExceedBlockLimit(BlockLimit::Skip, "media.path");
					return false;
				}
				if (!ReadUInt16(*typeField, type, context, "media.type") || type != kMediaType ||
					!ReadUInt32(*contentId, media.contentId, context, "media.contentId") ||
					!ReadUInt32(*undoId, media.undoId, context, "media.undoId") ||
					!ReadString(*mediaPath, media.path, context, "media.path") ||
					!ReadString(*mimeType, media.mimeType, context, "media.mimeType") ||
					media.path.empty() || media.mimeType.empty()) return false;
				media.pathIsSafe = IsSafeMediaPath(media.path,
					context.limits.maxMediaPathBytes);
				if (!media.pathIsSafe)
					context.Add(UInkDiagnosticCode::UnsafeMediaPath, UInkDiagnosticSeverity::Warning,
						"media.path");

				if (const WireValue* field = fields.Find("width"))
				{
					float value = 0.0f;
					if (!ReadFloat32(*field, value, context, "media.width") ||
						!std::isfinite(value) || value <= 0.0f) return false;
					media.width = value;
				}
				if (const WireValue* field = fields.Find("height"))
				{
					float value = 0.0f;
					if (!ReadFloat32(*field, value, context, "media.height") ||
						!std::isfinite(value) || value <= 0.0f) return false;
					media.height = value;
				}
				if (IsVisualMimeType(media.mimeType) && (!media.width || !media.height)) return false;

				if (const WireValue* field = fields.Find("transform"))
				{
					bool valid = field->type == WireType::Array && field->size == 6;
					if (valid)
					{
						WireCursor items(*field);
						for (size_t index = 0; index < media.transform.size(); ++index)
						{
							if (!ReadFloat32(items.Next(), media.transform[index], context,
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
				if (const WireValue* field = fields.Find("opacity"))
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
				if (const WireValue* field = fields.Find("pageCount"))
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
				if (const WireValue* field = fields.Find("pageIndex"))
				{
					if (!ReadUInt32(*field, media.pageIndex, context, "media.pageIndex")) return false;
				}
				if (const WireValue* field = fields.Find("autoplay"))
				{
					if (!ReadBool(*field, media.autoplay)) return false;
				}
				if (const WireValue* field = fields.Find("loop"))
				{
					if (!ReadBool(*field, media.loop)) return false;
				}
				if (const WireValue* field = fields.Find("volume"))
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
				if (const WireValue* field = fields.Find("startTime"))
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
				if (const WireValue* field = fields.Find("playbackRate"))
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
				if (const WireValue* field = fields.Find("extra"))
					if (!SkipExtra(*field, context, "media.extra")) return false;
				context.Add(UInkDiagnosticCode::MediaResourceUnavailable,
					UInkDiagnosticSeverity::Info, "media.path");
				return true;
			});
		}

		bool DecodeClear(const WireValue& object, UInkClear& clear,
			DecodeContext& context)
		{
			const FieldPath path = "clear";
			static constexpr std::string_view keys[] = { "type", "contentId", "undoId", "extra" };
			return ReadMapFields(object, keys, context, path, [&](auto& fields)
			{
				uint16_t type = 0;
				const WireValue* typeField = fields.Find("type");
				const WireValue* contentId = fields.Find("contentId");
				const WireValue* undoId = fields.Find("undoId");
				if (!typeField || !contentId || !undoId)
				{
					AddMissing(context, path);
					return false;
				}
				if (!ReadUInt16(*typeField, type, context, "clear.type") ||
					type != kClearType ||
					!ReadUInt32(*contentId, clear.contentId, context, "clear.contentId") ||
					!ReadUInt32(*undoId, clear.undoId, context, "clear.undoId")) return false;
				if (const WireValue* field = fields.Find("extra"))
					if (!SkipExtra(*field, context, "clear.extra")) return false;
				return true;
			});
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
				// FILE-22：每条链最多访问注册表中的全部不同条目，重访时断开引用。
				std::set<size_t> path{ index };
				size_t current = index;
				for (;;)
				{
					const UInkDevice& currentDevice = extension.devices[current];
					const UInkWindowDevice* window = std::get_if<UInkWindowDevice>(&currentDevice.geometry);
					if (!currentDevice.parentResolved || !window) break;
					const auto parent = devicesByGuid.find(window->parentDeviceGuid.Bytes());
					if (parent == devicesByGuid.end() ||
						!extension.devices[parent->second].usable)
					{
						extension.devices[current].parentResolved = false;
						MarkTemporary(context, "headerExtension.devices.parentDeviceGuid");
						break;
					}
					if (!path.insert(parent->second).second)
					{
						extension.devices[parent->second].parentResolved = false;
						MarkTemporary(context, "headerExtension.devices.parentDeviceGuid");
						context.Add(UInkDiagnosticCode::ParentCycle,
							UInkDiagnosticSeverity::Warning,
							"headerExtension.devices.parentDeviceGuid");
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
				std::set<size_t> path{ index };
				size_t current = index;
				for (;;)
				{
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
					if (!path.insert(parent->second).second)
					{
						extension.workspaces[parent->second].parentResolved = false;
						extension.workspaces[parent->second].parentWorkspaceGuid.reset();
						MarkTemporary(context, "headerExtension.workspaces.parentWorkspaceGuid");
						context.Add(UInkDiagnosticCode::ParentCycle,
							UInkDiagnosticSeverity::Warning,
							"headerExtension.workspaces.parentWorkspaceGuid");
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

		void ValidateCanvasTopology(UInkDocument& document, DecodeContext& context,
			std::span<const DecodeLocation> canvasLocations)
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
			std::map<PageGroupKey, const UInkCanvas*> presentationBaseLayers;
			std::map<GuidKey, int32_t> presentationSlides;
			std::map<PresentationSlideKey, GuidKey> presentationPagesBySlide;
			std::set<GuidKey> mismatchedPresentationPages;

			for (size_t index = 0; index < document.canvases.size(); ++index)
			{
				UInkCanvas& canvas = document.canvases[index];
				context.location = canvasLocations[index];
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
				const PageGroupKey group(workspace, device, page);
				const UInkWorkspace* owner = FindWorkspace(document.headerExtension, canvas.workspaceGuid);
				if (owner && owner->workspaceType == 2 && canvas.layerIndex == 0)
					presentationBaseLayers.try_emplace(group, &canvas);
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
				layerIndices[group].insert(canvas.layerIndex);
				if (!canvasKeys.emplace(workspace, device, page, canvas.layerIndex).second)
				{
					canvas.temporaryLayer = true;
					MarkTemporary(context, "canvas.layerIndex");
				}
			}

			for (size_t index = 0; index < document.canvases.size(); ++index)
			{
				UInkCanvas& canvas = document.canvases[index];
				const UInkWorkspace* owner = FindWorkspace(document.headerExtension, canvas.workspaceGuid);
				if (!owner || owner->workspaceType != 2) continue;
				const UInkCanvas* bindingSource = &canvas;
				if (canvas.layerIndex != 0 && !canvas.temporaryDevice && !canvas.temporaryPage)
				{
					// CANVAS-8/13：同时看到各层后，按同设备、同页第 0 层判定 PPT 绑定。
					const auto base = presentationBaseLayers.find(
						PageGroupKey(ToKey(canvas.workspaceGuid), ToKey(canvas.deviceGuid),
							canvas.pageGuid.Bytes()));
					if (base != presentationBaseLayers.end()) bindingSource = base->second;
				}
				if (!HasValidUInkPresentationBinding(*bindingSource))
				{
					context.location = canvasLocations[index];
					canvas.presentationUnbound = true;
					MarkTemporary(context, "canvas.slideId");
				}
			}

			// 组级诊断关联该组中在对象流首先出现的 Canvas。
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
					bool reported = false;
					for (size_t index = 0; index < document.canvases.size(); ++index)
					{
						UInkCanvas& canvas = document.canvases[index];
						if (ToKey(canvas.workspaceGuid) != workspace) continue;
						canvas.temporaryPage = true;
						if (!reported)
						{
							context.location = canvasLocations[index];
							MarkTemporary(context, "canvas.pageIndex");
							reported = true;
						}
					}
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
					bool reported = false;
					for (size_t index = 0; index < document.canvases.size(); ++index)
					{
						UInkCanvas& canvas = document.canvases[index];
						if (PageGroupKey(ToKey(canvas.workspaceGuid), ToKey(canvas.deviceGuid),
							canvas.pageGuid.Bytes()) != group) continue;
						canvas.temporaryLayer = true;
						if (!reported)
						{
							context.location = canvasLocations[index];
							MarkTemporary(context, "canvas.layerIndex");
							reported = true;
						}
					}
				}
			}

			if (!mismatchedPresentationPages.empty())
			{
				bool reported = false;
				for (size_t index = 0; index < document.canvases.size(); ++index)
				{
					UInkCanvas& canvas = document.canvases[index];
					const UInkWorkspace* owner = FindWorkspace(document.headerExtension, canvas.workspaceGuid);
					if (!owner || owner->workspaceType != 2 ||
						!mismatchedPresentationPages.contains(canvas.pageGuid.Bytes())) continue;
					canvas.presentationUnbound = true;
					if (!reported)
					{
						context.location = canvasLocations[index];
						MarkTemporary(context, "canvas.slideId");
						reported = true;
					}
				}
			}
		}

		void ValidateWorkspacePages(UInkDocument& document, DecodeContext& context,
			std::span<const DecodeLocation> canvasLocations)
		{
			if (!document.headerExtension || document.usesImplicitWorkspace) return;
			for (UInkWorkspace& workspace : document.headerExtension->workspaces)
			{
				if (!workspace.usable) continue;
				bool hasCurrentPage = false;
				std::optional<DecodeLocation> firstCanvas;
				for (size_t index = 0; index < document.canvases.size(); ++index)
				{
					const UInkCanvas& canvas = document.canvases[index];
					if (canvas.temporaryWorkspace || !canvas.workspaceGuid ||
						*canvas.workspaceGuid != workspace.guid) continue;
					if (!firstCanvas) firstCanvas = canvasLocations[index];
					if (canvas.pageIndex == workspace.currentPageIndex)
					{
						hasCurrentPage = true;
						break;
					}
				}
				if (!hasCurrentPage)
				{
					context.location = firstCanvas.value_or(DecodeLocation{});
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

		void PostValidateDocument(UInkDocument& document, DecodeContext& context,
			DecodeLocation extensionLocation, std::span<const DecodeLocation> canvasLocations)
		{
			context.location = extensionLocation;
			ValidateRegistries(document, context);
			ValidateCanvasTopology(document, context, canvasLocations);
			ValidateWorkspacePages(document, context, canvasLocations);
			context.location = {};
			ValidateHeaderSnapshot(document, context);
		}

		bool ReadTopLevelType(const WireValue& object, uint16_t& type, DecodeContext& context)
		{
			if (object.type != WireType::Map) return false;
			auto& reader = *object.reader;
			if (reader.replaying && reader.duplicateType)
			{
				if (reader.hasCanvasType)
				{
					++context.location.canvasOrdinal;
					type = kCanvasType;
				}
				context.duplicateKnownKey = true;
				context.Add(UInkDiagnosticCode::DuplicateKnownKey, UInkDiagnosticSeverity::Warning, "type");
				return false;
			}
			WireCursor entries(object);
			for (uint32_t i = 0; i < object.size; ++i)
			{
				const auto key = ObjectStringView(entries.Next());
				const WireValue value = entries.Next();
				if (!key || *key != "type") continue;
				if ((value.type == WireType::UInt && value.u64 == kCanvasType) ||
					((value.type == WireType::Float32 || value.type == WireType::Float64) &&
						value.f64 == kCanvasType))
				{
					// Canvas 的序号先递增，由 DecodeCanvas 单独记录 type 数值兼容。
					++context.location.canvasOrdinal;
					type = kCanvasType;
					return true;
				}
				return ReadUInt16(value, type, context, "type");
			}
			return false;
		}

		bool CanOfferSafeAppend(const UInkReadResult& result) noexcept
		{
			return result.status != UInkReadStatus::LimitExceeded &&
				!result.provenance.invalidBlockBeforeValidBlock &&
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
		bool ReadStreamHeader(std::span<const std::byte> bytes, size_t& position,
			UInkHeader& header, DecodeContext& context)
		{
			try
			{
				WireReader reader(bytes, position, context.limits);
				const WireValue root = reader.Read(position);
				std::array<WireValue, 7> fields;
				const bool headerArray = root.type == WireType::Array && root.size == fields.size();
				if (headerArray)
				{
					WireCursor items(root);
					for (auto& field : fields) field = items.Next();
				}
				position = reader.End(root);
				TransientChargeGuard transient(context, reader.Charge());
				if (!transient.IsActive() || reader.legacyUnpackLimit)
					throw WireFailure{ ScanStatus::LimitExceeded };
				if (!headerArray || !DecodeHeader(fields, header, context))
				{
					if (context.HasBlockLimit())
						context.result.status = UInkReadStatus::LimitExceeded;
					else
					{
						context.Add(UInkDiagnosticCode::InvalidHeader,
							UInkDiagnosticSeverity::Fatal, "header");
						context.result.status = UInkReadStatus::RejectedHeader;
					}
					context.result.failedObjectOffset = 0;
					return false;
				}
			}
			catch (const WireFailure& failure)
			{
				const bool limited = failure.status == ScanStatus::LimitExceeded;
				context.Add(limited ? UInkDiagnosticCode::LimitExceeded : UInkDiagnosticCode::InvalidHeader,
					UInkDiagnosticSeverity::Fatal, "header");
				context.result.status = limited ? UInkReadStatus::LimitExceeded : UInkReadStatus::RejectedHeader;
				context.result.failedObjectOffset = 0;
				return false;
			}
			return true;
		}
	}

	static UInkReadResult DecodeUInkObjects(std::span<const std::byte> bytes, const UInkReadLimits& limits,
		bool validateDocument, const UInkStreamContext* streamContext,
		uint64_t baseOffset = 0, uint64_t* canvasCount = nullptr) noexcept
	{
		const auto started = std::chrono::steady_clock::now();
		UInkReadResult result;
		DecodeContext context{ limits, result };
		context.location.objectOffset = baseOffset;
		const auto finish = [&]() noexcept -> UInkReadResult
		{
			if (canvasCount) *canvasCount = context.location.canvasOrdinal;
			// 资源策略跳过的块仍可能完整存在于文件，绝不能允许旧追加路径截掉它。
			if (result.status == UInkReadStatus::LimitExceeded)
			{
				result.safeAppendOffset.reset();
				result.provenance.requiresSaveAs = true;
			}
			if (result.document && !result.document->canvases.empty())
			{
				const auto& provenance = result.provenance;
				const bool partial = result.status != UInkReadStatus::Complete ||
					provenance.containsUnknownTopLevel || provenance.usedFieldFallback ||
					provenance.usedTemporaryIdentity || provenance.containsInvalidCompleteBlocks ||
					provenance.contentSequenceRecovered ||
					result.summary.diagnosticCounts[static_cast<size_t>(
						UInkDiagnosticCode::UnsafeMediaPath)].count != 0;
				result.summary.outcome = partial ? UInkReadOutcome::Partial : UInkReadOutcome::Complete;
			}
			result.summary.decodeMicroseconds = static_cast<uint64_t>(
				std::chrono::duration_cast<std::chrono::microseconds>(
					std::chrono::steady_clock::now() - started).count());
			return std::move(result);
		};
		auto rejectSecondCanvas = [&]() -> UInkReadResult
		{
			result.status = UInkReadStatus::StoppedAtCorruptTail;
			result.failedObjectOffset = context.location.objectOffset;
			result.provenance.requiresSaveAs = true;
			context.detailCheckpoint = 0;
			context.Add(UInkDiagnosticCode::InvalidObjectOrder,
				UInkDiagnosticSeverity::Error, "page.canvas");
			return finish();
		};
		auto findSecondCanvas = [&](size_t scanPosition, uint64_t objectIndex) noexcept
		{
			while (scanPosition < bytes.size())
			{
				bool recognizedCanvas = false;
				const auto boundary = FindRecoveryBoundary(bytes, scanPosition, &recognizedCanvas);
				if (recognizedCanvas && ++context.location.canvasOrdinal > 1)
				{
					context.location.objectOffset = baseOffset + scanPosition;
					context.location.objectIndex = objectIndex;
					return true;
				}
				if (!boundary) break;
				scanPosition = boundary->end;
				++objectIndex;
			}
			return false;
		};
		try
		{
			if (static_cast<uint64_t>(bytes.size()) > limits.maxFileBytes ||
				static_cast<uint64_t>(bytes.size()) > (std::numeric_limits<uint64_t>::max)() - baseOffset)
			{
				context.Add(UInkDiagnosticCode::LimitExceeded,
					UInkDiagnosticSeverity::Fatal, "file");
				result.status = UInkReadStatus::LimitExceeded;
				return finish();
			}
			if (!streamContext && bytes.empty())
			{
				context.Add(UInkDiagnosticCode::InvalidHeader,
					UInkDiagnosticSeverity::Fatal, "header");
				result.status = UInkReadStatus::RejectedHeader;
				result.failedObjectOffset = 0;
				return finish();
			}
			if (limits.maxTopLevelObjects == 0)
			{
				if (streamContext && findSecondCanvas(0, 0)) return rejectSecondCanvas();
				context.Add(UInkDiagnosticCode::LimitExceeded,
					UInkDiagnosticSeverity::Fatal, "file.objects");
				result.status = UInkReadStatus::LimitExceeded;
				return finish();
			}

			UInkDocument document;
			size_t position = 0;
			// WireReader 与恢复扫描只看到传入的 span，基址仅用于报告位置。
			if (streamContext)
			{
				document.usesImplicitDevice = streamContext->usesImplicitDevice;
				document.usesImplicitWorkspace = streamContext->usesImplicitWorkspace;
				result.summary.version = streamContext->version;
			}
			else
			{
				context.readingHeader = true;
				if (!ReadStreamHeader(bytes, position, document.header, context)) return finish();
				context.readingHeader = false;
			}

			enum class StreamState : uint8_t { AfterHeader, BeforeCanvas, InCanvas };
			StreamState state = streamContext ? StreamState::BeforeCanvas : StreamState::AfterHeader;
			std::optional<size_t> currentCanvas;
			std::optional<size_t> invalidTailStart;
			size_t stoppedObjectEnd = 0;
			DecodeLocation extensionLocation;
			std::vector<DecodeLocation> canvasLocations;
			result.decodedObjectCount = streamContext ? 0 : 1;
			result.validPrefixLength = position;

			auto endCanvasScope = [&]()
			{
				currentCanvas.reset();
				state = StreamState::BeforeCanvas;
			};
			auto markInvalid = [&](size_t offset, std::optional<uint16_t> type = std::nullopt)
			{
				result.provenance.containsInvalidCompleteBlocks = true;
				result.provenance.requiresSaveAs = true;
				if (!invalidTailStart) invalidTailStart = offset;
				if (type)
					context.Add(UInkDiagnosticCode::KnownBlockSkipped,
						UInkDiagnosticSeverity::Warning, "topLevel." + std::to_string(*type));
			};
			auto markLimited = [&](bool stopped)
			{
				result.status = UInkReadStatus::LimitExceeded;
				if (stopped) result.failedObjectOffset = baseOffset + position;
			};
			auto skipWireObject = [&](size_t end, const std::optional<RecoveryBoundary>& boundary)
			{
				// 仅在完整分类后保留旧作用域，未知分类按潜在 Canvas 处理。
				if (boundary && boundary->isCanvas) ++context.location.canvasOrdinal;
				if (!boundary || boundary->isCanvas || state == StreamState::AfterHeader)
					endCanvasScope();
				context.Add(UInkDiagnosticCode::LimitExceeded,
					UInkDiagnosticSeverity::Warning, "topLevel");
				markLimited(false);
				markInvalid(position);
				++result.decodedObjectCount;
				position = end;
			};
			auto markValid = [&](size_t end)
			{
				if (invalidTailStart)
				{
					result.provenance.invalidBlockBeforeValidBlock = true;
					invalidTailStart.reset();
				}
				if (!result.provenance.containsInvalidCompleteBlocks &&
					result.status != UInkReadStatus::LimitExceeded)
					result.validPrefixLength = end;
			};
			auto appendContent = [&](UInkContent content) -> bool
			{
				if (!currentCanvas || *currentCanvas >= document.canvases.size()) return false;
				auto& contents = document.canvases[*currentCanvas].content;
				const bool currentIsClear = std::holds_alternative<UInkClear>(content);
				const uint32_t currentUndo = std::visit(
					[](const auto& value) { return value.undoId; }, content);
				if (!contents.empty())
				{
					const UInkContent& previous = contents.back();
					const bool previousIsClear = std::holds_alternative<UInkClear>(previous);
					const uint32_t previousUndo = std::visit(
						[](const auto& value) { return value.undoId; }, previous);
					// Clear 必须独占撤回组，避免清空和绘制被一次操作同时移除。
					if ((currentIsClear || previousIsClear) && currentUndo == previousUndo)
						return false;
				}
				if (!context.ReserveStorage(contents, "canvas.content")) return false;
				contents.push_back(std::move(content));
				return true;
			};

			while (position < bytes.size())
			{
				context.location.objectOffset = baseOffset + position;
				context.location.objectIndex = result.decodedObjectCount;
				if (result.decodedObjectCount >= limits.maxTopLevelObjects)
				{
					context.Add(UInkDiagnosticCode::LimitExceeded,
						UInkDiagnosticSeverity::Warning, "file.objects");
					markLimited(true);
					break;
				}

				context.duplicateKnownKey = false;
				context.blockStart = context.budget;
				context.blockLimit = BlockLimit::None;
				context.registryLimitExceeded = false;
				const auto locationBeforeBlock = context.location;
				const auto provenanceBeforeBlock = result.provenance;
				const auto summaryBeforeBlock = result.summary;
				const uint64_t previousPeak = result.modelCharge;
				const size_t detailCount = result.diagnostics.size();
				context.detailCheckpoint = detailCount;
				context.overwrittenDetail.reset();

				// 结构错误的优先级高于字段诊断；只有对象完整时才发布作用域和模型。
				auto rollback = [&]()
				{
					context.budget = context.blockStart;
					context.location = locationBeforeBlock;
					result.decodedObjectCount = locationBeforeBlock.objectIndex;
					context.blockLimit = BlockLimit::None;
					context.registryLimitExceeded = false;
					context.duplicateKnownKey = false;
					context.transientUpperBound = 0;
					context.EndTransient();
					result.modelCharge = previousPeak;
					result.provenance = provenanceBeforeBlock;
					result.summary = summaryBeforeBlock;
					result.diagnostics.resize(detailCount);
					if (context.overwrittenDetail)
					{
						result.diagnostics.back() = std::move(*context.overwrittenDetail);
						context.overwrittenDetail.reset();
					}
				};

				size_t end = position;
				uint16_t type = (std::numeric_limits<uint16_t>::max)();
				bool valid = false;
				std::variant<std::monostate, UInkHeaderExtension, UInkCanvas, UInkContent> pending;
				auto decodeFields = [&](const WireValue& root)
				{
					// S1 在字段解释前计数；未闭合对象的试读会随 checkpoint 回滚。
					result.decodedObjectCount = locationBeforeBlock.objectIndex + 1;
					type = (std::numeric_limits<uint16_t>::max)();
					if (!ReadTopLevelType(root, type, context)) return false;
					switch (type)
					{
					case kHeaderExtensionType:
						return state == StreamState::AfterHeader && !document.headerExtension &&
							DecodeHeaderExtension(root, pending.emplace<UInkHeaderExtension>(), context) &&
							!context.HasBlockLimit();
					case kCanvasType:
						return DecodeCanvas(root, pending.emplace<UInkCanvas>(), document,
							streamContext ? streamContext->headerExtension : document.headerExtension,
							context, !validateDocument);
					case kInkType:
					case kShapeType:
					case kMediaType:
					case kClearType:
						break;
					case kHeaderType:
						return false;
					default:
						result.provenance.containsUnknownTopLevel = true;
						result.provenance.requiresSaveAs = true;
						context.Add(UInkDiagnosticCode::UnknownTopLevelType,
							UInkDiagnosticSeverity::Warning, "type");
						return true;
					}
					if (state != StreamState::InCanvas || !currentCanvas) return false;
					auto& content = pending.emplace<UInkContent>();
					if (type == kInkType)
						return DecodeInk(root, content.emplace<UInkInk>(), context);
					if (type == kShapeType) return DecodeShape(root, content.emplace<UInkShape>(), context);
					if (type == kMediaType) return DecodeMedia(root, content.emplace<UInkMedia>(), context);
					return DecodeClear(root, content.emplace<UInkClear>(), context);
				};

				const uint64_t wireBytes = (std::min)(static_cast<uint64_t>(bytes.size() - position),
					static_cast<uint64_t>(limits.maxTopLevelObjectBytes));
				// 对未闭合的对象保留临时预算和模型预算。越过窗口才需精确重读，
				// 大文件中连续的小块不应因后续无关页而反复预读。
				const uint64_t available = limits.maxModelCharge - context.budget.persistentCharge;
				const uint64_t window = (std::min)(wireBytes, available / (2 * 33));
				context.transientUpperBound = 33 * window;
				context.blockPeakPersistent = context.blockStart.persistentCharge;
				ScanStatus wireStatus = ScanStatus::Complete;
				bool skipComplete = false;
				bool skippedCanvas = false;
				try
				{
					try
					{
						WireReader reader(bytes, position, limits, window);
						const WireValue root = reader.Read(position);
						const auto finishObject = [&]()
						{
							end = reader.End(root);
							const uint64_t charge = reader.Charge();
							if (reader.duplicateType || reader.legacyUnpackLimit || charge > limits.maxModelCharge ||
								context.blockPeakPersistent > limits.maxModelCharge - charge)
								throw ReplayObject{};
							context.transientUpperBound = 0;
							context.transientCharge = charge;
							result.modelCharge = (std::max)(previousPeak, context.blockPeakPersistent + charge);
						};
						try
						{
							valid = decodeFields(root);
						}
						catch (const std::bad_alloc&)
						{
							// 模型分配失败时，完整对象仍要交付 S1 的临时区峰值。
							finishObject();
							throw;
						}
						finishObject();
					}
					catch (const ReplayObject&)
					{
						++result.replayedObjectCount;
						// 重复键或临界预算可能改变旧路径的首条诊断，按精确 charge 重放该块。
						rollback();
						pending.emplace<std::monostate>();
						WireReader reader(bytes, position, limits);
						reader.replaying = true;
						const WireValue root = reader.Read(position);
						end = reader.End(root);
						skipComplete = !context.BeginTransient(reader.Charge()) || reader.legacyUnpackLimit;
						if (skipComplete) skippedCanvas = reader.hasCanvasType;
						else valid = decodeFields(root);
					}
				}
				catch (const WireFailure& failure)
				{
					rollback();
					wireStatus = failure.status;
				}
				if (skipComplete)
				{
					context.EndTransient();
					const auto boundary = streamContext ?
						std::optional{ RecoveryBoundary{ end, skippedCanvas } } :
						FindRecoveryBoundary(bytes.first(end), position);
					skipWireObject(end, boundary);
					if (streamContext && context.location.canvasOrdinal > 1)
						return rejectSecondCanvas();
					continue;
				}
				if (wireStatus == ScanStatus::LimitExceeded)
				{
					const auto boundary = FindRecoveryBoundary(bytes, position);
					if (!boundary)
					{
						context.Add(UInkDiagnosticCode::LimitExceeded,
							UInkDiagnosticSeverity::Warning, "topLevel");
						markLimited(true);
						break;
					}
					skipWireObject(boundary->end, boundary);
					if (streamContext && context.location.canvasOrdinal > 1)
						return rejectSecondCanvas();
					continue;
				}
				if (wireStatus != ScanStatus::Complete)
				{
					// 字节耗尽丢弃末个对象，非法标记停止；字段无效由下方完整块分支处理。
					result.provenance.requiresSaveAs = true;
					result.failedObjectOffset = baseOffset + position;
					if (wireStatus == ScanStatus::NeedMore)
					{
						if (result.status != UInkReadStatus::LimitExceeded)
							result.status = UInkReadStatus::RecoveredTruncatedTail;
						context.Add(UInkDiagnosticCode::TruncatedTail,
							UInkDiagnosticSeverity::Warning, "tail");
					}
					else
					{
						if (result.status != UInkReadStatus::LimitExceeded)
							result.status = UInkReadStatus::StoppedAtCorruptTail;
						context.Add(UInkDiagnosticCode::MalformedMessagePack,
							UInkDiagnosticSeverity::Error, "tail");
					}
					break;
				}
				if (streamContext && context.location.canvasOrdinal > 1)
					return rejectSecondCanvas();
				if (type == kCanvasType) endCanvasScope();
				if (valid && state == StreamState::AfterHeader && type != kHeaderExtensionType)
					state = StreamState::BeforeCanvas;
				if (valid)
				{
					if (auto* extension = std::get_if<UInkHeaderExtension>(&pending))
					{
						document.usesImplicitDevice = extension->devices.empty();
						document.usesImplicitWorkspace = extension->workspaces.empty();
						document.headerExtension = std::move(*extension);
						extensionLocation = context.location;
						// 流式入口先校验本次读到的注册表，随后逐 Canvas 查找引用。
						if (!validateDocument) ValidateRegistries(document, context);
						state = StreamState::BeforeCanvas;
					}
					else if (auto* canvas = std::get_if<UInkCanvas>(&pending))
					{
						valid = context.ReserveStorage(canvasLocations, "canvas.location");
						if (valid)
						{
							canvasLocations.push_back(context.location);
							document.canvases.push_back(std::move(*canvas));
							currentCanvas = document.canvases.size() - 1;
							state = StreamState::InCanvas;
						}
					}
					else if (auto* content = std::get_if<UInkContent>(&pending))
						valid = appendContent(std::move(*content));
				}
				context.EndTransient();
				if (context.registryLimitExceeded) markLimited(false);
				if (valid) markValid(end);
				else
				{
					// 丢弃整块时回滚点数与持久 charge，modelCharge 峰值和诊断仍有效。
					context.budget = context.blockStart;
					result.provenance = provenanceBeforeBlock;
					if (context.HasBlockLimit())
					{
						const bool stopped = context.blockLimit == BlockLimit::Stop;
						markLimited(stopped);
						if (stopped)
						{
							stoppedObjectEnd = end;
							break;
						}
					}
					if (state == StreamState::AfterHeader) state = StreamState::BeforeCanvas;
					markInvalid(position, type);
				}
				position = end;
			}

			// 模型解码提前停止时只继续识别顶层 type，包括截断对象中已经完整的 type。
			if (streamContext && findSecondCanvas((std::max)(position, stoppedObjectEnd),
				result.decodedObjectCount))
				return rejectSecondCanvas();

			context.detailCheckpoint = 0;
			if (validateDocument)
				PostValidateDocument(document, context, extensionLocation, canvasLocations);
			result.geometryPointCount = CountDocumentGeometryPoints(document);

			if (position == bytes.size() && result.status != UInkReadStatus::LimitExceeded)
			{
				if (invalidTailStart)
				{
					result.status = UInkReadStatus::RecoveredInvalidTail;
					result.failedObjectOffset = baseOffset + *invalidTailStart;
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

			if (validateDocument && CanOfferSafeAppend(result))
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
			if (result.status != UInkReadStatus::LimitExceeded || !document.canvases.empty())
				result.document = std::move(document);
			return finish();
		}
		catch (const std::bad_alloc&)
		{
			result.status = UInkReadStatus::LimitExceeded;
			// 明细可能无法分配，Add 会先记录固定大小的诊断汇总。
			try { context.Add(UInkDiagnosticCode::LimitExceeded, UInkDiagnosticSeverity::Fatal); }
			catch (...) {}
		}
		catch (...)
		{
			result.status = UInkReadStatus::StoppedAtCorruptTail;
			try { context.Add(UInkDiagnosticCode::DecodeFailed, UInkDiagnosticSeverity::Fatal); }
			catch (...) {}
		}
		result.document.reset();
		result.safeAppendOffset.reset();
		result.failedObjectOffset = context.location.objectOffset;
		return finish();
	}

	UInkReadResult DecodeUInk(std::span<const std::byte> bytes, const UInkReadLimits& limits) noexcept
	{
		return DecodeUInkObjects(bytes, limits, true, nullptr);
	}

	UInkReadResult DecodeUInkStream(std::span<const std::byte> bytes, const UInkReadLimits& limits,
		const UInkStreamContext* context) noexcept
	{
		return DecodeUInkObjects(bytes, limits, false, context, context ? context->baseOffset : 0);
	}

	UInkReadResult DecodeUInkStream(std::span<const std::byte> bytes, const UInkReadLimits& limits,
		const UInkStreamContext& context, uint64_t baseOffset, uint64_t& canvasCount) noexcept
	{
		return DecodeUInkObjects(bytes, limits, false, &context, baseOffset, &canvasCount);
	}

	UInkReadResult DecodeUInkStream(std::span<const std::byte> bytes, const UInkReadLimits& limits,
		uint64_t& canvasCount) noexcept
	{
		return DecodeUInkObjects(bytes, limits, false, nullptr, 0, &canvasCount);
	}
}
