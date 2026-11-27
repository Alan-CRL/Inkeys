module;

#define MSGPACK_NO_BOOST
#include <msgpack.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <map>
#include <new>
#include <optional>
#include <set>
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
		bool IsValidUtf8ForWrite(std::string_view text) noexcept
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

		class UInkEncoder
		{
		public:
			explicit UInkEncoder(const UInkReadLimits& limits = {}) : packer_(buffer_), limits_(limits) {}

			bool PackDocument(const UInkDocument& document)
			{
				if (!ValidateDocument(document)) return false;
				if (!PackHeaderBlocks(document.header, document.headerExtension)) return false;

				std::vector<size_t> order(document.canvases.size());
				for (size_t index = 0; index < order.size(); ++index) order[index] = index;
				std::stable_sort(order.begin(), order.end(), [&](size_t left, size_t right)
				{
					return CanvasOrderKey(document, document.canvases[left]) <
						CanvasOrderKey(document, document.canvases[right]);
				});
				for (const size_t index : order)
				{
					const UInkCanvas& canvas = document.canvases[index];
					if (!PackPageBlocks(canvas, document.usesImplicitWorkspace,
						document.usesImplicitDevice)) return false;
				}
				return true;
			}

			bool PackHeaderPrefix(const UInkHeader& header,
				const std::optional<UInkHeaderExtension>& extension)
			{
				if (header.guid.IsZero()) return Fail("header.guid");
				if (extension && !ValidateHeaderExtension(*extension)) return false;
				return PackHeaderBlocks(header, extension);
			}

			bool PackPage(const UInkCanvas& canvas, bool implicitWorkspace, bool implicitDevice)
			{
				if (!ValidateCanvas(canvas, implicitWorkspace, implicitDevice)) return false;
				// 内容之外还需要为 Canvas 自身保留一个顶层对象名额。
				if (canvas.content.size() >= limits_.maxTopLevelObjects) return FailLimit("file.objects");
				uint64_t totalPoints = 0;
				for (const UInkContent& content : canvas.content)
					if (!AddGeometryPoints(GeometryPointCount(content), totalPoints)) return false;
				return PackPageBlocks(canvas, implicitWorkspace, implicitDevice);
			}

			// 续接已落盘页的内容块（SAVE-7/8）：编号接在 firstContentId 之后，撤回组与 Clear 边界规则同 ValidateCanvas。
			bool PackContentBlocks(std::span<const UInkContent> blocks, uint64_t firstContentId,
				uint32_t previousUndoId, bool previousWasClear)
			{
				if (blocks.empty()) return Fail("content.empty");
				if (blocks.size() >= limits_.maxTopLevelObjects) return FailLimit("content.objects");
				uint64_t totalPoints = 0;
				uint32_t previousUndo = previousUndoId;
				bool previousClear = previousWasClear;
				for (size_t index = 0; index < blocks.size(); ++index)
				{
					const uint64_t expectedId = firstContentId + index;
					if (expectedId > (std::numeric_limits<uint32_t>::max)()) return FailLimit("content.contentId");
					const bool currentIsClear = std::holds_alternative<UInkClear>(blocks[index]);
					const bool sequenceValid = std::visit([&](const auto& content)
					{
						if (content.contentId != expectedId) return false;
						if (expectedId == 0) return content.undoId == 0;
						if (content.undoId < previousUndo) return false;
						return !((currentIsClear || previousClear) && content.undoId == previousUndo);
					}, blocks[index]);
					if (!sequenceValid || !ValidateContent(blocks[index]))
						return Fail("content.sequence");
					std::visit([&](const auto& content) { previousUndo = content.undoId; }, blocks[index]);
					previousClear = currentIsClear;
					if (!AddGeometryPoints(GeometryPointCount(blocks[index]), totalPoints)) return false;
				}
				for (const UInkContent& content : blocks)
				{
					const size_t objectStart = buffer_.size();
					PackContent(content);
					if (!CheckPackedObject(objectStart, "content")) return false;
				}
				return true;
			}

			UInkEncodeResult Finish()
			{
				UInkEncodeResult result;
				result.diagnostics = std::move(diagnostics_);
				if (failed_)
				{
					result.status = limitExceeded_ ? UInkEncodeStatus::LimitExceeded :
						UInkEncodeStatus::InvalidModel;
					return result;
				}
				result.bytes.resize(buffer_.size());
				if (!result.bytes.empty())
					std::memcpy(result.bytes.data(), buffer_.data(), buffer_.size());
				result.status = UInkEncodeStatus::Success;
				return result;
			}

		private:
			using GuidBytes = std::array<uint8_t, 16>;
			using CanvasSortKey = std::tuple<size_t, uint32_t, size_t, uint32_t>;

			bool Fail(std::string path, UInkDiagnosticCode code = UInkDiagnosticCode::InvalidFieldValue)
			{
				if (!failed_)
					diagnostics_.push_back({ code, UInkDiagnosticSeverity::Error,
						0, 0, std::move(path), 0 });
				failed_ = true;
				return false;
			}

			bool FailLimit(std::string path)
			{
				limitExceeded_ = true;
				return Fail(std::move(path), UInkDiagnosticCode::LimitExceeded);
			}

			bool CheckOutputLimit()
			{
				if (static_cast<uint64_t>(buffer_.size()) > limits_.maxFileBytes)
					return FailLimit("file");
				return true;
			}

			bool CheckPackedObject(size_t objectStart, const char* path)
			{
				if (failed_) return false;
				if (buffer_.size() < objectStart ||
					static_cast<uint64_t>(buffer_.size() - objectStart) >
					limits_.maxTopLevelObjectBytes) return FailLimit(path);
				return CheckOutputLimit();
			}

			template <typename T>
			uint64_t GeometryPointCount(const T& value) const noexcept
			{
				if constexpr (std::is_same_v<T, UInkInk>) return value.points.size();
				else if constexpr (std::is_same_v<T, UInkShape>)
				{
					if (const UInkLineGeometry* line = std::get_if<UInkLineGeometry>(&value.geometry))
						return line->points.size();
				}
				else if constexpr (std::is_same_v<T, UInkContent>)
				{
					return std::visit([&](const auto& content)
					{
						return GeometryPointCount(content);
					}, value);
				}
				return 0;
			}

			bool AddGeometryPoints(uint64_t count, uint64_t& total)
			{
				if (count > limits_.maxGeometryPoints ||
					total > limits_.maxGeometryPoints - count)
					return FailLimit("geometry.points");
				total += count;
				return true;
			}

			bool ValidateStringLength(size_t size, const char* path)
			{
				if (static_cast<uint64_t>(size) > limits_.maxStringOrBinaryBytes ||
					size > std::numeric_limits<uint32_t>::max()) return FailLimit(path);
				return true;
			}

			bool ValidateString(const std::string& value, const char* path)
			{
				return ValidateStringLength(value.size(), path) &&
					(IsValidUtf8ForWrite(value) || Fail(path));
			}

			void PackString(std::string_view value)
			{
				// 固定键名和 GUID 也占用解码器的字符串长度预算。
				if (!ValidateStringLength(value.size(), "string")) return;
				packer_.pack_str(static_cast<uint32_t>(value.size()));
				packer_.pack_str_body(value.data(), static_cast<uint32_t>(value.size()));
			}

			void PackKey(std::string_view value)
			{
				PackString(value);
			}

			void PackOptionalInt32(std::string_view key, const std::optional<int32_t>& value)
			{
				if (value) { PackKey(key); packer_.pack_fix_int32(*value); }
			}

			void PackGuid(const UInkGuid& guid)
			{
				PackString(FormatUInkGuid(guid));
			}

			bool ValidateGenericValue(const UInkMessagePackValue& value,
				uint32_t depth, uint64_t& charge)
			{
				if (depth > limits_.maxDepth) return FailLimit("shape.object.depth");
				if (limits_.maxModelCharge < 48 || charge > limits_.maxModelCharge - 48)
					return FailLimit("shape.object");
				charge += 48;
				return std::visit([&](const auto& stored) -> bool
				{
					using T = std::decay_t<decltype(stored)>;
					if constexpr (std::is_same_v<T, std::string>)
						return ValidateString(stored, "shape.object.string");
					else if constexpr (std::is_same_v<T, std::vector<std::byte>>)
					{
						if (static_cast<uint64_t>(stored.size()) > limits_.maxStringOrBinaryBytes ||
							stored.size() > std::numeric_limits<uint32_t>::max())
							return FailLimit("shape.object.binary");
						return true;
					}
					else if constexpr (std::is_same_v<T, UInkMessagePackValue::Array>)
					{
						if (static_cast<uint64_t>(stored.size()) > limits_.maxContainerEntries ||
							stored.size() > std::numeric_limits<uint32_t>::max())
							return FailLimit("shape.object.array");
						for (const UInkMessagePackValue& item : stored)
							if (!ValidateGenericValue(item, depth + 1, charge)) return false;
						return true;
					}
					else if constexpr (std::is_same_v<T, UInkMessagePackValue::Map>)
					{
						if (static_cast<uint64_t>(stored.size()) > limits_.maxContainerEntries ||
							stored.size() > std::numeric_limits<uint32_t>::max())
							return FailLimit("shape.object.map");
						for (const auto& [key, item] : stored)
						{
							if (!ValidateGenericValue(key, depth + 1, charge) ||
								!ValidateGenericValue(item, depth + 1, charge)) return false;
						}
						return true;
					}
					else if constexpr (std::is_same_v<T, UInkMessagePackExtension>)
					{
						// EXT 的类型字节计入读取上限，载荷必须严格小于上限。
						if (static_cast<uint64_t>(stored.data.size()) >= limits_.maxStringOrBinaryBytes ||
							stored.data.size() > std::numeric_limits<uint32_t>::max())
							return FailLimit("shape.object.extension");
						return true;
					}
					else return true;
				}, value.value);
			}

			void PackGenericValue(const UInkMessagePackValue& value)
			{
				std::visit([&](const auto& stored)
				{
					using T = std::decay_t<decltype(stored)>;
					if constexpr (std::is_same_v<T, std::monostate>) packer_.pack_nil();
					else if constexpr (std::is_same_v<T, bool>) packer_.pack(stored);
					else if constexpr (std::is_same_v<T, int64_t>) packer_.pack_fix_int64(stored);
					else if constexpr (std::is_same_v<T, uint64_t>) packer_.pack_fix_uint64(stored);
					else if constexpr (std::is_same_v<T, float>) packer_.pack_float(stored);
					else if constexpr (std::is_same_v<T, double>) packer_.pack_double(stored);
					else if constexpr (std::is_same_v<T, std::string>) PackString(stored);
					else if constexpr (std::is_same_v<T, std::vector<std::byte>>)
					{
						packer_.pack_bin(static_cast<uint32_t>(stored.size()));
						packer_.pack_bin_body(reinterpret_cast<const char*>(stored.data()),
							static_cast<uint32_t>(stored.size()));
					}
					else if constexpr (std::is_same_v<T, UInkMessagePackValue::Array>)
					{
						packer_.pack_array(static_cast<uint32_t>(stored.size()));
						for (const UInkMessagePackValue& item : stored) PackGenericValue(item);
					}
					else if constexpr (std::is_same_v<T, UInkMessagePackValue::Map>)
					{
						packer_.pack_map(static_cast<uint32_t>(stored.size()));
						for (const auto& [key, item] : stored)
						{
							PackGenericValue(key);
							PackGenericValue(item);
						}
					}
					else if constexpr (std::is_same_v<T, UInkMessagePackExtension>)
					{
						packer_.pack_ext(static_cast<uint32_t>(stored.data.size()), stored.type);
						packer_.pack_ext_body(reinterpret_cast<const char*>(stored.data.data()),
							static_cast<uint32_t>(stored.data.size()));
					}
				}, value.value);
			}

			bool ValidateColor(const UInkColor& color, const char* path)
			{
				if (color.fallbackRgb > 0xffffff) return Fail(path);
				if (!color.extended) return true;
				for (const float value : color.extended->components)
				{
					if (!std::isfinite(value) ||
						(color.extended->space == UInkColorSpace::Srgb &&
							(value < 0.0f || value > 1.0f))) return Fail(path);
				}
				return true;
			}

			void PackColor(const UInkColor& color)
			{
				packer_.pack_map(color.extended ? 3 : 1);
				PackKey("fallback");
				packer_.pack_fix_uint32(color.fallbackRgb);
				if (!color.extended) return;
				PackKey("space");
				PackString(color.extended->space == UInkColorSpace::Srgb ? "srgb" : "scrgb");
				PackKey("components");
				packer_.pack_array(3);
				for (const float value : color.extended->components) packer_.pack_float(value);
			}

			bool PackHeaderBlocks(const UInkHeader& header,
				const std::optional<UInkHeaderExtension>& extension)
			{
				size_t objectStart = buffer_.size();
				PackHeader(header);
				if (!CheckPackedObject(objectStart, "header")) return false;
				if (!extension) return true;
				objectStart = buffer_.size();
				PackHeaderExtension(*extension);
				return CheckPackedObject(objectStart, "headerExtension");
			}

			bool PackPageBlocks(const UInkCanvas& canvas, bool implicitWorkspace, bool implicitDevice)
			{
				size_t objectStart = buffer_.size();
				PackCanvas(canvas, implicitWorkspace, implicitDevice);
				if (!CheckPackedObject(objectStart, "canvas")) return false;
				for (const UInkContent& content : canvas.content)
				{
					objectStart = buffer_.size();
					PackContent(content);
					if (!CheckPackedObject(objectStart, "content")) return false;
				}
				return true;
			}

			void PackHeader(const UInkHeader& header)
			{
				packer_.pack_array(7);
				packer_.pack_fix_uint16(kHeaderType);
				packer_.pack_fix_uint16(kUInkVersion);
				PackGuid(header.guid);
				packer_.pack_fix_uint32(header.deviceNum);
				packer_.pack_fix_uint32(header.workspaceNum);
				packer_.pack_fix_uint32(header.pageNum);
				packer_.pack_fix_uint64(header.time);
			}

			bool ValidateHardware(const UInkHardware& hardware)
			{
				if (hardware.name && !ValidateString(*hardware.name, "device.hardware.name")) return false;
				if (hardware.id && !ValidateString(*hardware.id, "device.hardware.id")) return false;
				std::set<std::string> keys;
				if (hardware.identifiers.size() > limits_.maxContainerEntries ||
					hardware.identifiers.size() > std::numeric_limits<uint32_t>::max())
					return FailLimit("device.hardware.identifiers");
				for (const auto& [key, value] : hardware.identifiers)
				{
					if (!ValidateString(key, "device.hardware.identifiers") ||
						!ValidateString(value, "device.hardware.identifiers") ||
						!keys.insert(key).second) return Fail("device.hardware.identifiers");
				}
				if ((hardware.physicalWidth && *hardware.physicalWidth == 0) ||
					(hardware.physicalHeight && *hardware.physicalHeight == 0) ||
					(hardware.scaleFactor && (!std::isfinite(*hardware.scaleFactor) ||
						*hardware.scaleFactor <= 0.0f))) return Fail("device.hardware");
				return true;
			}

			void PackHardware(const UInkHardware& hardware)
			{
				uint32_t count = static_cast<uint32_t>(hardware.name.has_value() +
					hardware.id.has_value() + !hardware.identifiers.empty() +
					hardware.physicalWidth.has_value() + hardware.physicalHeight.has_value() +
					hardware.scaleFactor.has_value());
				packer_.pack_map(count);
				if (hardware.name) { PackKey("name"); PackString(*hardware.name); }
				if (hardware.id) { PackKey("id"); PackString(*hardware.id); }
				if (!hardware.identifiers.empty())
				{
					PackKey("identifiers");
					packer_.pack_map(static_cast<uint32_t>(hardware.identifiers.size()));
					for (const auto& [key, value] : hardware.identifiers)
					{
						PackString(key);
						PackString(value);
					}
				}
				if (hardware.physicalWidth)
				{
					PackKey("physicalWidth");
					packer_.pack_fix_uint32(*hardware.physicalWidth);
				}
				if (hardware.physicalHeight)
				{
					PackKey("physicalHeight");
					packer_.pack_fix_uint32(*hardware.physicalHeight);
				}
				if (hardware.scaleFactor)
				{
					PackKey("scaleFactor");
					packer_.pack_float(*hardware.scaleFactor);
				}
			}

			bool ValidateDevice(const UInkDevice& device)
			{
				if (device.guid.IsZero() || !device.usable || !device.parentResolved ||
					device.deviceType < 0) return Fail("headerExtension.devices");
				if (device.name && !ValidateString(*device.name, "device.name")) return false;
				if (device.hardware && !ValidateHardware(*device.hardware)) return false;
				if (device.deviceType == 0)
				{
					const UInkDisplayDevice* display = std::get_if<UInkDisplayDevice>(&device.geometry);
					return display && display->width != 0 && display->height != 0 ||
						Fail("device.geometry");
				}
				if (device.deviceType == 1)
				{
					const UInkWindowDevice* window = std::get_if<UInkWindowDevice>(&device.geometry);
					return window && !window->parentDeviceGuid.IsZero() &&
						std::isfinite(window->x) && std::isfinite(window->y) &&
						std::isfinite(window->width) && std::isfinite(window->height) &&
						window->width > 0.0f && window->height > 0.0f || Fail("device.geometry");
				}
				return std::holds_alternative<UInkUnknownDevice>(device.geometry) ||
					Fail("device.geometry");
			}

			void PackDevice(const UInkDevice& device)
			{
				uint32_t count = 2 + static_cast<uint32_t>(device.name.has_value() +
					device.hardware.has_value());
				if (device.deviceType == 0) count += 4;
				else if (device.deviceType == 1) count += 6;
				packer_.pack_map(count);
				PackKey("guid"); PackGuid(device.guid);
				PackKey("deviceType"); packer_.pack_fix_int32(device.deviceType);
				if (device.name) { PackKey("name"); PackString(*device.name); }
				if (device.hardware) { PackKey("hardware"); PackHardware(*device.hardware); }
				if (const UInkDisplayDevice* display = std::get_if<UInkDisplayDevice>(&device.geometry);
					device.deviceType == 0 && display)
				{
					PackKey("x"); packer_.pack_fix_int32(display->x);
					PackKey("y"); packer_.pack_fix_int32(display->y);
					PackKey("width"); packer_.pack_fix_uint32(display->width);
					PackKey("height"); packer_.pack_fix_uint32(display->height);
				}
				else if (const UInkWindowDevice* window = std::get_if<UInkWindowDevice>(&device.geometry);
					device.deviceType == 1 && window)
				{
					PackKey("parentDeviceGuid"); PackGuid(window->parentDeviceGuid);
					PackKey("x"); packer_.pack_float(window->x);
					PackKey("y"); packer_.pack_float(window->y);
					PackKey("width"); packer_.pack_float(window->width);
					PackKey("height"); packer_.pack_float(window->height);
					PackKey("zIndex"); packer_.pack_fix_uint32(window->zIndex);
				}
			}

			bool ValidateWorkspace(const UInkWorkspace& workspace)
			{
				if (workspace.guid.IsZero() || !workspace.usable || !workspace.parentResolved ||
					workspace.workspaceType < 0 ||
					(workspace.parentWorkspaceGuid &&
						(workspace.parentWorkspaceGuid->IsZero() ||
							*workspace.parentWorkspaceGuid == workspace.guid)))
					return Fail("headerExtension.workspaces");
				if (workspace.name && !ValidateString(*workspace.name, "workspace.name")) return false;
				if (workspace.hostId && !ValidateString(*workspace.hostId, "workspace.hostId")) return false;
				return true;
			}

			void PackWorkspace(const UInkWorkspace& workspace)
			{
				uint32_t count = 3 + static_cast<uint32_t>(workspace.name.has_value() +
					workspace.parentWorkspaceGuid.has_value() + workspace.hostId.has_value());
				packer_.pack_map(count);
				PackKey("guid"); PackGuid(workspace.guid);
				PackKey("workspaceType"); packer_.pack_fix_int32(workspace.workspaceType);
				if (workspace.name) { PackKey("name"); PackString(*workspace.name); }
				if (workspace.parentWorkspaceGuid)
				{
					PackKey("parentWorkspaceGuid"); PackGuid(*workspace.parentWorkspaceGuid);
				}
				if (workspace.hostId) { PackKey("hostId"); PackString(*workspace.hostId); }
				PackKey("currentPageIndex"); packer_.pack_fix_uint32(workspace.currentPageIndex);
			}

			bool ValidateHeaderExtension(const UInkHeaderExtension& extension)
			{
				if (extension.name && !ValidateString(*extension.name, "headerExtension.name")) return false;
				if (extension.explanation &&
					!ValidateString(*extension.explanation, "headerExtension.explanation")) return false;
				if (extension.devices.size() > limits_.maxContainerEntries ||
					extension.workspaces.size() > limits_.maxContainerEntries ||
					extension.devices.size() > kUInkMaxRegistryEntries ||
					extension.workspaces.size() > kUInkMaxRegistryEntries)
					return FailLimit("headerExtension");
				std::map<GuidBytes, size_t> deviceIndices;
				for (size_t index = 0; index < extension.devices.size(); ++index)
				{
					const UInkDevice& device = extension.devices[index];
					if (!ValidateDevice(device) ||
						!deviceIndices.emplace(device.guid.Bytes(), index).second)
						return Fail("headerExtension.devices.guid");
				}
				std::map<GuidBytes, size_t> workspaceIndices;
				for (size_t index = 0; index < extension.workspaces.size(); ++index)
				{
					const UInkWorkspace& workspace = extension.workspaces[index];
					if (!ValidateWorkspace(workspace) ||
						!workspaceIndices.emplace(workspace.guid.Bytes(), index).second)
						return Fail("headerExtension.workspaces.guid");
				}
				for (const UInkDevice& device : extension.devices)
				{
					if (const UInkWindowDevice* window = std::get_if<UInkWindowDevice>(&device.geometry);
						device.deviceType == 1 &&
						deviceIndices.find(window->parentDeviceGuid.Bytes()) == deviceIndices.end())
						return Fail("device.parentDeviceGuid");
				}
				for (const UInkWorkspace& workspace : extension.workspaces)
				{
					if (workspace.parentWorkspaceGuid &&
						workspaceIndices.find(workspace.parentWorkspaceGuid->Bytes()) == workspaceIndices.end())
						return Fail("workspace.parentWorkspaceGuid");
				}

				// Writer 不输出需要 reader 临时断环的注册树。
				for (size_t start = 0; start < extension.devices.size(); ++start)
				{
					std::set<size_t> path;
					size_t current = start;
					for (;;)
					{
						if (!path.insert(current).second)
							return Fail("device.parentDeviceGuid", UInkDiagnosticCode::ParentCycle);
						const UInkDevice& device = extension.devices[current];
						const UInkWindowDevice* window = std::get_if<UInkWindowDevice>(&device.geometry);
						if (device.deviceType != 1 || !window) break;
						current = deviceIndices.find(window->parentDeviceGuid.Bytes())->second;
					}
				}
				for (size_t start = 0; start < extension.workspaces.size(); ++start)
				{
					std::set<size_t> path;
					size_t current = start;
					for (;;)
					{
						if (!path.insert(current).second)
							return Fail("workspace.parentWorkspaceGuid", UInkDiagnosticCode::ParentCycle);
						const std::optional<UInkGuid>& parent =
							extension.workspaces[current].parentWorkspaceGuid;
						if (!parent) break;
						current = workspaceIndices.find(parent->Bytes())->second;
					}
				}
				return true;
			}

			void PackHeaderExtension(const UInkHeaderExtension& extension)
			{
				uint32_t count = 1 + static_cast<uint32_t>(extension.name.has_value() +
					extension.explanation.has_value() + !extension.devices.empty() +
					!extension.workspaces.empty());
				packer_.pack_map(count);
				PackKey("type"); packer_.pack_fix_uint16(kHeaderExtensionType);
				if (extension.name) { PackKey("name"); PackString(*extension.name); }
				if (extension.explanation)
				{
					PackKey("explanation"); PackString(*extension.explanation);
				}
				if (!extension.devices.empty())
				{
					PackKey("devices");
					packer_.pack_array(static_cast<uint32_t>(extension.devices.size()));
					for (const UInkDevice& device : extension.devices) PackDevice(device);
				}
				if (!extension.workspaces.empty())
				{
					PackKey("workspaces");
					packer_.pack_array(static_cast<uint32_t>(extension.workspaces.size()));
					for (const UInkWorkspace& workspace : extension.workspaces) PackWorkspace(workspace);
				}
			}

			bool ValidateViewport(const UInkViewport& viewport)
			{
				return std::isfinite(viewport.x) && std::isfinite(viewport.y) &&
					std::isfinite(viewport.scale) && viewport.scale > 0.0f ||
					Fail("canvas.viewport");
			}

			bool ValidateCanvas(const UInkCanvas& canvas, bool implicitWorkspace,
				bool implicitDevice)
			{
				if (canvas.pageGuid.IsZero() || canvas.temporaryWorkspace || canvas.temporaryDevice ||
					canvas.temporaryPage || canvas.temporaryLayer || canvas.presentationUnbound)
					return Fail("canvas.identity");
				if ((!implicitWorkspace && !canvas.workspaceGuid) ||
					(implicitWorkspace && canvas.workspaceGuid) ||
					(!implicitDevice && !canvas.deviceGuid) ||
					(implicitDevice && canvas.deviceGuid)) return Fail("canvas.registryReference");
				if (canvas.viewport && canvas.layerIndex != 0) return Fail("canvas.viewport");
				if (canvas.viewport && !ValidateViewport(*canvas.viewport)) return false;
				// CANVAS-8：页面元数据只允许写在第 0 层。
				if (canvas.layerIndex != 0 &&
					(canvas.background || canvas.pageState || canvas.pageKind || canvas.binding))
					return Fail("canvas.pageMetadata");
				if (canvas.background &&
					!ValidateColor(canvas.background->color, "canvas.background.color")) return false;
				if (canvas.content.size() > limits_.maxTopLevelObjects) return FailLimit("canvas.content");
				uint32_t previousUndo = 0;
				bool previousWasClear = false;
				for (size_t index = 0; index < canvas.content.size(); ++index)
				{
					const bool currentIsClear =
						std::holds_alternative<UInkClear>(canvas.content[index]);
					const bool sequenceValid = std::visit([&](const auto& content)
					{
						if (content.contentId != index ||
							(index == 0 && content.undoId != 0) ||
							(index != 0 && content.undoId < previousUndo) ||
							(index != 0 && (currentIsClear || previousWasClear) &&
								content.undoId == previousUndo)) return false;
						previousUndo = content.undoId;
						previousWasClear = currentIsClear;
						return true;
					}, canvas.content[index]);
					if (!sequenceValid || !ValidateContent(canvas.content[index]))
						return Fail("canvas.content.sequence");
				}
				return true;
			}

			void PackCanvas(const UInkCanvas& canvas, bool implicitWorkspace, bool implicitDevice)
			{
				uint32_t count = 6 + static_cast<uint32_t>(!implicitWorkspace + !implicitDevice +
					canvas.slideId.has_value() + canvas.viewport.has_value() +
					canvas.background.has_value() + canvas.pageState.has_value() +
					canvas.pageKind.has_value() + canvas.binding.has_value());
				packer_.pack_map(count);
				PackKey("type"); packer_.pack_fix_uint16(kCanvasType);
				if (!implicitWorkspace) { PackKey("workspaceGuid"); PackGuid(*canvas.workspaceGuid); }
				if (!implicitDevice) { PackKey("deviceGuid"); PackGuid(*canvas.deviceGuid); }
				PackKey("pageGuid"); PackGuid(canvas.pageGuid);
				PackKey("pageIndex"); packer_.pack_fix_uint32(canvas.pageIndex);
				PackKey("pageNumber"); packer_.pack_fix_uint32(canvas.pageNumber);
				PackKey("layerIndex"); packer_.pack_fix_uint32(canvas.layerIndex);
				PackKey("layerNumber"); packer_.pack_fix_uint32(canvas.layerNumber);
				if (canvas.slideId) { PackKey("slideId"); packer_.pack_fix_int32(*canvas.slideId); }
				if (canvas.viewport)
				{
					PackKey("viewport");
					packer_.pack_map(3);
					PackKey("x"); packer_.pack_float(canvas.viewport->x);
					PackKey("y"); packer_.pack_float(canvas.viewport->y);
					PackKey("scale"); packer_.pack_float(canvas.viewport->scale);
				}
				if (canvas.background)
				{
					PackKey("background");
					packer_.pack_map(2);
					PackKey("color"); PackColor(canvas.background->color);
					PackKey("pattern"); packer_.pack_fix_int32(canvas.background->pattern);
				}
				PackOptionalInt32("pageState", canvas.pageState);
				PackOptionalInt32("pageKind", canvas.pageKind);
				PackOptionalInt32("binding", canvas.binding);
			}

			bool ValidateCoordinateRange(double x, double y, const char* path)
			{
				return (std::abs(x) <= kUInkMaxCoordinate && std::abs(y) <= kUInkMaxCoordinate) ||
					Fail(path, UInkDiagnosticCode::CoordinateOutOfRange);
			}

			static float InkPositionDelta(size_t index, float position, float previous) noexcept
			{
				return index == 0 ? position : position - previous;
			}

			bool ValidateInk(const UInkInk& ink)
			{
				if (ink.points.size() > limits_.maxSingleGeometryPoints)
					return FailLimit("ink.points");
				if (ink.declaredInkType < 0 || ink.declaredTexture < 0 || ink.points.empty() ||
					!std::isfinite(ink.opacity) || ink.opacity < 0.0f || ink.opacity > 1.0f ||
					!ValidateColor(ink.color, "ink.color"))
					return Fail("ink");
				if (ink.nib && HasInvalidUInkNibAspect(*ink.nib)) return Fail("ink.nib.aspect");
				float previousX = 0.0f;
				float previousY = 0.0f;
				double absoluteX = 0.0;
				double absoluteY = 0.0;
				for (size_t index = 0; index < ink.points.size(); ++index)
				{
					const UInkInkPoint& point = ink.points[index];
					if (!std::isfinite(point.x) || !std::isfinite(point.y) ||
						!std::isfinite(point.width) || point.width <= 0.0f ||
						(point.style.has_value() && ink.declaredInkType != 3)) return Fail("ink.points");
					// INK-4：用实际写出的 float32 位移累加，避免舍入后超出读取范围。
					absoluteX += InkPositionDelta(index, point.x, previousX);
					absoluteY += InkPositionDelta(index, point.y, previousY);
					if (!ValidateCoordinateRange(absoluteX, absoluteY, "ink.points")) return false;
					if (point.style && (!std::isfinite(point.style->opacity) ||
						point.style->opacity < 0.0f || point.style->opacity > 1.0f ||
						!ValidateColor(point.style->color, "ink.points.color"))) return Fail("ink.points");
					previousX = point.x;
					previousY = point.y;
				}
				return true;
			}

			void PackInk(const UInkInk& ink)
			{
				uint32_t count = 8 + static_cast<uint32_t>(ink.renderOnlyWhenLatest +
					ink.nib.has_value() + ink.cap.has_value());
				packer_.pack_map(count);
				PackKey("type"); packer_.pack_fix_uint16(kInkType);
				PackKey("contentId"); packer_.pack_fix_uint32(ink.contentId);
				PackKey("undoId"); packer_.pack_fix_uint32(ink.undoId);
				PackKey("inkType"); packer_.pack_fix_int32(ink.declaredInkType);
				PackKey("color"); PackColor(ink.color);
				PackKey("opacity"); packer_.pack_float(ink.opacity);
				PackKey("texture"); packer_.pack_fix_int32(ink.declaredTexture);
				PackKey("points"); packer_.pack_array(static_cast<uint32_t>(ink.points.size()));
				float previousX = 0.0f;
				float previousY = 0.0f;
				for (size_t index = 0; index < ink.points.size(); ++index)
				{
					const UInkInkPoint& point = ink.points[index];
					packer_.pack_map(point.style ? 5 : 3);
					PackKey("x"); packer_.pack_float(InkPositionDelta(index, point.x, previousX));
					PackKey("y"); packer_.pack_float(InkPositionDelta(index, point.y, previousY));
					PackKey("width"); packer_.pack_float(point.width);
					if (point.style)
					{
						PackKey("color"); PackColor(point.style->color);
						PackKey("opacity"); packer_.pack_float(point.style->opacity);
					}
					previousX = point.x;
					previousY = point.y;
				}
				if (ink.renderOnlyWhenLatest)
				{
					PackKey("renderOnlyWhenLatest"); packer_.pack_true();
				}
				if (ink.nib)
				{
					const bool hasOrientation = ink.nib->orientation != 0;
					PackKey("nib");
					packer_.pack_map(1 + static_cast<uint32_t>(ink.nib->aspect.has_value() + hasOrientation));
					PackKey("shape"); packer_.pack_fix_int32(ink.nib->shape);
					if (ink.nib->aspect) { PackKey("aspect"); packer_.pack_float(*ink.nib->aspect); }
					if (hasOrientation)
					{
						PackKey("orientation"); packer_.pack_fix_int32(ink.nib->orientation);
					}
				}
				PackOptionalInt32("cap", ink.cap);
			}

			enum class PresetIntegerWidth { Generic, Int32, UInt32 };

			static PresetIntegerWidth PresetIntegerType(const UInkMessagePackValue& key,
				bool construction) noexcept
			{
				const auto* text = std::get_if<std::string>(&key.value);
				if (!text) return PresetIntegerWidth::Generic;
				if (construction)
				{
					if (*text == "mode" || *text == "constraint") return PresetIntegerWidth::Int32;
					if (*text == "sides" || *text == "constraintVertex") return PresetIntegerWidth::UInt32;
				}
				else if (*text == "linePattern") return PresetIntegerWidth::Int32;
				return PresetIntegerWidth::Generic;
			}

			static std::optional<int64_t> ReadShapeParameterInteger(
				const UInkMessagePackValue& value) noexcept
			{
				if (const auto* number = std::get_if<int64_t>(&value.value)) return *number;
				if (const auto* number = std::get_if<uint64_t>(&value.value);
					number && std::in_range<int64_t>(*number)) return static_cast<int64_t>(*number);
				return std::nullopt;
			}

			static bool IsShapeStyleColor(const UInkMessagePackValue& key) noexcept
			{
				const auto* text = std::get_if<std::string>(&key.value);
				return text && (*text == "strokeColor" || *text == "fillColor");
			}

			static std::optional<uint32_t> ReadShapeStyleColorFallback(
				const UInkMessagePackValue& value) noexcept
			{
				constexpr uint32_t kMaxRgb = 0xffffff;
				if (const auto number = ReadShapeParameterInteger(value);
					number && *number >= 0 && *number <= kMaxRgb)
					return static_cast<uint32_t>(*number);
				const auto* float32 = std::get_if<float>(&value.value);
				const auto* float64 = std::get_if<double>(&value.value);
				if (!float32 && !float64) return std::nullopt;
				const double number = float64 ? *float64 : *float32;
				if (number >= 0.0 && number <= kMaxRgb && std::trunc(number) == number)
					return static_cast<uint32_t>(number);
				return std::nullopt;
			}

			static std::optional<float> ReadShapeStyleColorComponent(
				const UInkMessagePackValue& value) noexcept
			{
				return std::visit([](const auto& number) -> std::optional<float>
				{
					using T = std::decay_t<decltype(number)>;
					if constexpr (std::is_same_v<T, float> || std::is_same_v<T, double>)
					{
						if (!std::isfinite(number) ||
							std::abs(number) > std::numeric_limits<float>::max()) return std::nullopt;
						const float converted = static_cast<float>(number);
						if (static_cast<T>(converted) == number) return converted;
					}
					else if constexpr (std::is_same_v<T, int64_t> || std::is_same_v<T, uint64_t>)
					{
						const float converted = static_cast<float>(number);
						// 最大整数可能向上舍入，先检查范围再转回整数。
						if (static_cast<double>(converted) < std::ldexp(1.0, std::numeric_limits<T>::digits) &&
							static_cast<T>(converted) == number) return converted;
					}
					return std::nullopt;
				}, value.value);
			}

			bool ValidateShapeStyleColor(const UInkMessagePackValue& value)
			{
				const auto* map = std::get_if<UInkMessagePackValue::Map>(&value.value);
				if (!map) return Fail("shape.object.style.color");
				bool hasFallback = false;
				const std::string* space = nullptr;
				const UInkMessagePackValue::Array* components = nullptr;
				for (const auto& [key, entry] : *map)
				{
					const auto* text = std::get_if<std::string>(&key.value);
					if (!text) continue;
					if (*text == "fallback")
					{
						if (hasFallback || !ReadShapeStyleColorFallback(entry))
							return Fail("shape.object.style.color.fallback");
						hasFallback = true;
					}
					else if (*text == "space")
					{
						if (space || !(space = std::get_if<std::string>(&entry.value)))
							return Fail("shape.object.style.color.space");
					}
					else if (*text == "components")
					{
						if (components || !(components = std::get_if<UInkMessagePackValue::Array>(&entry.value)) ||
							components->size() != 3) return Fail("shape.object.style.color.components");
					}
				}
				if (!hasFallback || (space != nullptr) != (components != nullptr))
					return Fail("shape.object.style.color");
				if (components)
				{
					for (const auto& entry : *components)
					{
						const auto component = ReadShapeStyleColorComponent(entry);
						if (!component || (*space == "srgb" && (*component < 0.0f || *component > 1.0f)))
							return Fail("shape.object.style.color.components");
					}
				}
				return true;
			}

			bool ValidateShapeParameters(const std::optional<UInkMessagePackValue::Map>& parameters,
				bool construction)
			{
				if (!parameters) return true;
				if (parameters->size() > limits_.maxContainerEntries ||
					parameters->size() > std::numeric_limits<uint32_t>::max())
					return FailLimit("shape.object");
				uint64_t charge = 0;
				// Shape、object 和参数 Map 各占一层，键和值从第 4 层开始。
				for (const auto& [key, value] : *parameters)
					if (!ValidateGenericValue(key, 4, charge) || !ValidateGenericValue(value, 4, charge))
						return false;
				for (const auto& [key, value] : *parameters)
				{
					if (!construction && IsShapeStyleColor(key))
					{
						if (!ValidateShapeStyleColor(value)) return false;
						continue;
					}
					const PresetIntegerWidth width = PresetIntegerType(key, construction);
					if (width == PresetIntegerWidth::Generic) continue;
					const auto number = ReadShapeParameterInteger(value);
					if (!number || (width == PresetIntegerWidth::Int32 ?
						!std::in_range<int32_t>(*number) : !std::in_range<uint32_t>(*number)))
						return Fail(construction ? "shape.object.construction" : "shape.object.style");
				}
				return true;
			}

			bool ValidateShapeObject(const std::variant<UInkShapeObject, UInkShapeObjectRef>& object)
			{
				if (const auto* ref = std::get_if<UInkShapeObjectRef>(&object))
					return (!ref->ref.IsZero() && ref->partIndex != 0) || Fail("shape.object.ref");
				const auto& first = std::get<UInkShapeObject>(object);
				if (first.id.IsZero() || first.revision == 0 || first.partCount == 0)
					return Fail("shape.object");
				if (first.preset && !ValidateString(*first.preset, "shape.object.preset"))
					return false;
				if (first.transform)
					for (float value : *first.transform)
						if (!std::isfinite(value)) return Fail("shape.object.transform");
				return ValidateShapeParameters(first.construction, true) &&
					ValidateShapeParameters(first.style, false);
			}

			bool ValidateShape(const UInkShape& shape)
			{
				if (shape.declaredShapeType < 0 || shape.declaredShapeType > 6) return Fail("shape");
				if (shape.object && !ValidateShapeObject(*shape.object)) return false;
				const bool open = shape.declaredShapeType == 0 || shape.declaredShapeType == 1;
				if ((open && (!shape.stroke || shape.fill)) ||
					(!open && !shape.stroke && !shape.fill)) return Fail("shape.style");

				if (shape.declaredShapeType == 0 || shape.declaredShapeType == 1 ||
					shape.declaredShapeType == 6)
				{
					const UInkLineGeometry* line = std::get_if<UInkLineGeometry>(&shape.geometry);
					const size_t minimum = shape.declaredShapeType == 6 ? 3 : 2;
					if (line && (line->points.size() > kUInkMaxShapeVertices ||
						line->points.size() > limits_.maxSingleGeometryPoints))
						return FailLimit("shape.geometry.points");
					if (!line || line->points.size() < minimum ||
						(shape.declaredShapeType == 0 && line->points.size() != 2))
						return Fail("shape.geometry");
					for (const UInkShapePoint& point : line->points)
						if (!ValidateCoordinateRange(point.x, point.y, "shape.geometry")) return false;
				}
				else if (shape.declaredShapeType == 2)
				{
					const UInkRectangleGeometry* rectangle = std::get_if<UInkRectangleGeometry>(&shape.geometry);
					if (!rectangle || !std::isfinite(rectangle->width) ||
						!std::isfinite(rectangle->height) || !std::isfinite(rectangle->rotation) ||
						rectangle->width <= 0.0f || rectangle->height <= 0.0f ||
						!std::isfinite(rectangle->cornerRadiusX) ||
						!std::isfinite(rectangle->cornerRadiusY) ||
						!((rectangle->cornerRadiusX == 0.0f && rectangle->cornerRadiusY == 0.0f) ||
							(rectangle->cornerRadiusX > 0.0f && rectangle->cornerRadiusY > 0.0f &&
								rectangle->cornerRadiusX <= rectangle->width * 0.5f &&
								rectangle->cornerRadiusY <= rectangle->height * 0.5f)))
						return Fail("shape.geometry");
					if (!ValidateCoordinateRange(rectangle->centerX, rectangle->centerY,
						"shape.geometry")) return false;
				}
				else if (shape.declaredShapeType == 3)
				{
					const UInkSquareGeometry* square = std::get_if<UInkSquareGeometry>(&shape.geometry);
					if (!square || !std::isfinite(square->size) ||
						!std::isfinite(square->rotation) || square->size <= 0.0f)
						return Fail("shape.geometry");
					if (!ValidateCoordinateRange(square->centerX, square->centerY,
						"shape.geometry")) return false;
				}
				else if (shape.declaredShapeType == 4)
				{
					const UInkEllipseGeometry* ellipse = std::get_if<UInkEllipseGeometry>(&shape.geometry);
					if (!ellipse || !std::isfinite(ellipse->width) || !std::isfinite(ellipse->height) ||
						!std::isfinite(ellipse->rotation) || ellipse->width <= 0.0f || ellipse->height <= 0.0f)
						return Fail("shape.geometry");
					if (!ValidateCoordinateRange(ellipse->centerX, ellipse->centerY,
						"shape.geometry")) return false;
				}
				else
				{
					const UInkCircleGeometry* circle = std::get_if<UInkCircleGeometry>(&shape.geometry);
					if (!circle || !std::isfinite(circle->radius) || circle->radius <= 0.0f)
						return Fail("shape.geometry");
					if (!ValidateCoordinateRange(circle->centerX, circle->centerY,
						"shape.geometry")) return false;
				}

				if (shape.stroke)
				{
					const UInkShapeStroke& stroke = *shape.stroke;
					if (stroke.dashArray.size() > limits_.maxContainerEntries ||
						stroke.dashArray.size() > std::numeric_limits<uint32_t>::max())
						return FailLimit("shape.stroke.dashArray");
					if (!ValidateColor(stroke.color, "shape.stroke.color") ||
						!std::isfinite(stroke.opacity) || stroke.opacity < 0.0f || stroke.opacity > 1.0f ||
						!std::isfinite(stroke.width) || stroke.width <= 0.0f ||
						!std::isfinite(stroke.dashOffset) || stroke.declaredStartMarker < 0 ||
						stroke.declaredEndMarker < 0 || (!open &&
							(stroke.declaredStartMarker != 0 || stroke.declaredEndMarker != 0)))
						return Fail("shape.stroke");
					if (!stroke.dashArray.empty() && stroke.dashArray.size() % 2 != 0)
						return Fail("shape.stroke.dashArray");
					float total = 0.0f;
					for (const float value : stroke.dashArray)
					{
						if (!std::isfinite(value) || value < 0.0f) return Fail("shape.stroke.dashArray");
						total += value;
					}
					if (!stroke.dashArray.empty() && (!std::isfinite(total) || total <= 0.0f))
						return Fail("shape.stroke.dashArray");
				}
				if (shape.fill)
				{
					if (shape.fill->declaredFillType < 0 ||
						!ValidateColor(shape.fill->color, "shape.fill.color") ||
						!std::isfinite(shape.fill->opacity) || shape.fill->opacity < 0.0f ||
						shape.fill->opacity > 1.0f) return Fail("shape.fill");
				}
				return true;
			}

			void PackShapeGeometry(const UInkShape& shape)
			{
				if (shape.declaredShapeType == 0 || shape.declaredShapeType == 1 ||
					shape.declaredShapeType == 6)
				{
					const UInkLineGeometry& line = std::get<UInkLineGeometry>(shape.geometry);
					packer_.pack_map(1); PackKey("points");
					packer_.pack_array(static_cast<uint32_t>(line.points.size()));
					for (const UInkShapePoint& point : line.points)
					{
						packer_.pack_map(2);
						PackKey("x"); packer_.pack_float(point.x);
						PackKey("y"); packer_.pack_float(point.y);
					}
				}
				else if (shape.declaredShapeType == 2)
				{
					const UInkRectangleGeometry& value = std::get<UInkRectangleGeometry>(shape.geometry);
					const bool rounded = value.cornerRadiusX > 0.0f;
					packer_.pack_map(5 + static_cast<uint32_t>(rounded) * 2);
					PackKey("centerX"); packer_.pack_float(value.centerX);
					PackKey("centerY"); packer_.pack_float(value.centerY);
					PackKey("width"); packer_.pack_float(value.width);
					PackKey("height"); packer_.pack_float(value.height);
					PackKey("rotation"); packer_.pack_float(value.rotation);
					if (rounded)
					{
						PackKey("cornerRadiusX"); packer_.pack_float(value.cornerRadiusX);
						PackKey("cornerRadiusY"); packer_.pack_float(value.cornerRadiusY);
					}
				}
				else if (shape.declaredShapeType == 3)
				{
					const UInkSquareGeometry& value = std::get<UInkSquareGeometry>(shape.geometry);
					packer_.pack_map(4);
					PackKey("centerX"); packer_.pack_float(value.centerX);
					PackKey("centerY"); packer_.pack_float(value.centerY);
					PackKey("size"); packer_.pack_float(value.size);
					PackKey("rotation"); packer_.pack_float(value.rotation);
				}
				else if (shape.declaredShapeType == 4)
				{
					const UInkEllipseGeometry& value = std::get<UInkEllipseGeometry>(shape.geometry);
					packer_.pack_map(5);
					PackKey("centerX"); packer_.pack_float(value.centerX);
					PackKey("centerY"); packer_.pack_float(value.centerY);
					PackKey("width"); packer_.pack_float(value.width);
					PackKey("height"); packer_.pack_float(value.height);
					PackKey("rotation"); packer_.pack_float(value.rotation);
				}
				else
				{
					const UInkCircleGeometry& value = std::get<UInkCircleGeometry>(shape.geometry);
					packer_.pack_map(3);
					PackKey("centerX"); packer_.pack_float(value.centerX);
					PackKey("centerY"); packer_.pack_float(value.centerY);
					PackKey("radius"); packer_.pack_float(value.radius);
				}
			}

			void PackStroke(const UInkShapeStroke& stroke)
			{
				uint32_t count = 3 + static_cast<uint32_t>(!stroke.dashArray.empty() +
					(stroke.dashOffset != 0.0f) + (stroke.declaredStartMarker != 0) +
					(stroke.declaredEndMarker != 0) + stroke.lineCap.has_value() +
					stroke.lineJoin.has_value());
				packer_.pack_map(count);
				PackKey("color"); PackColor(stroke.color);
				PackKey("opacity"); packer_.pack_float(stroke.opacity);
				PackKey("width"); packer_.pack_float(stroke.width);
				if (!stroke.dashArray.empty())
				{
					PackKey("dashArray"); packer_.pack_array(static_cast<uint32_t>(stroke.dashArray.size()));
					for (const float value : stroke.dashArray) packer_.pack_float(value);
				}
				if (stroke.dashOffset != 0.0f)
				{
					PackKey("dashOffset"); packer_.pack_float(stroke.dashOffset);
				}
				if (stroke.declaredStartMarker != 0)
				{
					PackKey("startMarker"); packer_.pack_fix_int32(stroke.declaredStartMarker);
				}
				if (stroke.declaredEndMarker != 0)
				{
					PackKey("endMarker"); packer_.pack_fix_int32(stroke.declaredEndMarker);
				}
				PackOptionalInt32("lineCap", stroke.lineCap);
				PackOptionalInt32("lineJoin", stroke.lineJoin);
			}

			void PackFill(const UInkShapeFill& fill)
			{
				packer_.pack_map(3);
				PackKey("fillType"); packer_.pack_fix_int32(fill.declaredFillType);
				PackKey("color"); PackColor(fill.color);
				PackKey("opacity"); packer_.pack_float(fill.opacity);
			}

			void PackShapeStyleColor(const UInkMessagePackValue::Map& color)
			{
				// TYPE-1：对象样式的 Color Map 也按登记类型编码，保留扩展键。
				packer_.pack_map(static_cast<uint32_t>(color.size()));
				for (const auto& [key, value] : color)
				{
					PackGenericValue(key);
					const auto* text = std::get_if<std::string>(&key.value);
					if (text && *text == "fallback")
						packer_.pack_fix_uint32(*ReadShapeStyleColorFallback(value));
					else if (text && *text == "components")
					{
						const auto& components = std::get<UInkMessagePackValue::Array>(value.value);
						packer_.pack_array(static_cast<uint32_t>(components.size()));
						for (const auto& component : components)
							packer_.pack_float(*ReadShapeStyleColorComponent(component));
					}
					else PackGenericValue(value);
				}
			}

			void PackShapeParameters(const UInkMessagePackValue::Map& parameters, bool construction)
			{
				// TYPE-1：登记的整数使用固定宽度，扩展键沿用通用编码。
				packer_.pack_map(static_cast<uint32_t>(parameters.size()));
				for (const auto& [key, value] : parameters)
				{
					PackGenericValue(key);
					if (!construction && IsShapeStyleColor(key))
					{
						PackShapeStyleColor(std::get<UInkMessagePackValue::Map>(value.value));
						continue;
					}
					switch (PresetIntegerType(key, construction))
					{
					case PresetIntegerWidth::Int32:
						packer_.pack_fix_int32(static_cast<int32_t>(*ReadShapeParameterInteger(value)));
						break;
					case PresetIntegerWidth::UInt32:
						packer_.pack_fix_uint32(static_cast<uint32_t>(*ReadShapeParameterInteger(value)));
						break;
					case PresetIntegerWidth::Generic:
						PackGenericValue(value);
						break;
					}
				}
			}

			void PackShapeObject(const std::variant<UInkShapeObject, UInkShapeObjectRef>& object)
			{
				if (const auto* ref = std::get_if<UInkShapeObjectRef>(&object))
				{
					packer_.pack_map(2);
					PackKey("ref"); PackGuid(ref->ref);
					PackKey("partIndex"); packer_.pack_fix_uint32(ref->partIndex);
					return;
				}
				const auto& first = std::get<UInkShapeObject>(object);
				packer_.pack_map(3 + static_cast<uint32_t>(first.preset.has_value() +
					first.transform.has_value() + first.construction.has_value() +
					first.style.has_value() + first.aux.has_value()));
				PackKey("id"); PackGuid(first.id);
				PackKey("revision"); packer_.pack_fix_uint64(first.revision);
				PackKey("partCount"); packer_.pack_fix_uint32(first.partCount);
				if (first.preset) { PackKey("preset"); PackString(*first.preset); }
				if (first.transform)
				{
					PackKey("transform"); packer_.pack_array(6);
					for (float value : *first.transform) packer_.pack_float(value);
				}
				if (first.construction)
				{
					PackKey("construction"); PackShapeParameters(*first.construction, true);
				}
				if (first.style) { PackKey("style"); PackShapeParameters(*first.style, false); }
				if (first.aux) { PackKey("aux"); packer_.pack_fix_uint32(*first.aux); }
			}

			void PackShape(const UInkShape& shape)
			{
				uint32_t count = 5 + static_cast<uint32_t>(shape.stroke.has_value() +
					shape.fill.has_value() + shape.renderOnlyWhenLatest +
					shape.object.has_value());
				packer_.pack_map(count);
				PackKey("type"); packer_.pack_fix_uint16(kShapeType);
				PackKey("contentId"); packer_.pack_fix_uint32(shape.contentId);
				PackKey("undoId"); packer_.pack_fix_uint32(shape.undoId);
				PackKey("shapeType"); packer_.pack_fix_int32(shape.declaredShapeType);
				PackKey("geometry"); PackShapeGeometry(shape);
				if (shape.stroke) { PackKey("stroke"); PackStroke(*shape.stroke); }
				if (shape.fill) { PackKey("fill"); PackFill(*shape.fill); }
				if (shape.renderOnlyWhenLatest)
				{
					PackKey("renderOnlyWhenLatest"); packer_.pack_true();
				}
				if (shape.object) { PackKey("object"); PackShapeObject(*shape.object); }
			}

			bool ValidateMedia(const UInkMedia& media)
			{
				if (!ValidateString(media.path, "media.path") ||
					!ValidateString(media.mimeType, "media.mimeType") || media.mimeType.empty() ||
					!IsSafeMediaPath(media.path, limits_.maxMediaPathBytes) ||
					!std::isfinite(media.opacity) || media.opacity < 0.0f || media.opacity > 1.0f ||
					!std::isfinite(media.volume) || media.volume < 0.0f || media.volume > 1.0f ||
					!std::isfinite(media.startTime) || media.startTime < 0.0 ||
					!std::isfinite(media.playbackRate) || media.playbackRate <= 0.0f ||
					(media.pageCount && *media.pageCount == 0))
					return Fail("media");
				for (const float value : media.transform)
					if (!std::isfinite(value)) return Fail("media.transform");
				if (media.width && (!std::isfinite(*media.width) || *media.width <= 0.0f))
					return Fail("media.width");
				if (media.height && (!std::isfinite(*media.height) || *media.height <= 0.0f))
					return Fail("media.height");
				const bool visual = media.mimeType.rfind("image/", 0) == 0 ||
					media.mimeType.rfind("video/", 0) == 0 || media.mimeType == "application/pdf";
				return !visual || (media.width && media.height) || Fail("media.size");
			}

			void PackMedia(const UInkMedia& media)
			{
				const bool identityTransform = media.transform ==
					std::array<float, 6>{ 1.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f };
				uint32_t count = 5 + static_cast<uint32_t>(media.width.has_value() +
					media.height.has_value() + !identityTransform + (media.opacity != 1.0f) +
					media.pageCount.has_value() + (media.pageIndex != 0) + media.autoplay +
					media.loop + (media.volume != 1.0f) + (media.startTime != 0.0) +
					(media.playbackRate != 1.0f));
				packer_.pack_map(count);
				PackKey("type"); packer_.pack_fix_uint16(kMediaType);
				PackKey("contentId"); packer_.pack_fix_uint32(media.contentId);
				PackKey("undoId"); packer_.pack_fix_uint32(media.undoId);
				PackKey("path"); PackString(media.path);
				PackKey("mimeType"); PackString(media.mimeType);
				if (media.width) { PackKey("width"); packer_.pack_float(*media.width); }
				if (media.height) { PackKey("height"); packer_.pack_float(*media.height); }
				if (!identityTransform)
				{
					PackKey("transform"); packer_.pack_array(6);
					for (const float value : media.transform) packer_.pack_float(value);
				}
				if (media.opacity != 1.0f) { PackKey("opacity"); packer_.pack_float(media.opacity); }
				if (media.pageCount) { PackKey("pageCount"); packer_.pack_fix_uint32(*media.pageCount); }
				if (media.pageIndex != 0) { PackKey("pageIndex"); packer_.pack_fix_uint32(media.pageIndex); }
				if (media.autoplay) { PackKey("autoplay"); packer_.pack_true(); }
				if (media.loop) { PackKey("loop"); packer_.pack_true(); }
				if (media.volume != 1.0f) { PackKey("volume"); packer_.pack_float(media.volume); }
				if (media.startTime != 0.0) { PackKey("startTime"); packer_.pack_double(media.startTime); }
				if (media.playbackRate != 1.0f)
				{
					PackKey("playbackRate"); packer_.pack_float(media.playbackRate);
				}
			}

			void PackClear(const UInkClear& clear)
			{
				packer_.pack_map(3);
				PackKey("type"); packer_.pack_fix_uint16(kClearType);
				PackKey("contentId"); packer_.pack_fix_uint32(clear.contentId);
				PackKey("undoId"); packer_.pack_fix_uint32(clear.undoId);
			}

			bool ValidateContent(const UInkContent& content)
			{
				return std::visit([&](const auto& value) { return ValidateContentValue(value); }, content);
			}

			template <typename T>
			bool ValidateContentValue(const T& value)
			{
				if constexpr (std::is_same_v<T, UInkInk>) return ValidateInk(value);
				else if constexpr (std::is_same_v<T, UInkShape>) return ValidateShape(value);
				else if constexpr (std::is_same_v<T, UInkMedia>) return ValidateMedia(value);
				else return true;
			}

			void PackContent(const UInkContent& content)
			{
				std::visit([&](const auto& value) { PackContentValue(value); }, content);
			}

			template <typename T>
			void PackContentValue(const T& value)
			{
				if constexpr (std::is_same_v<T, UInkInk>) PackInk(value);
				else if constexpr (std::is_same_v<T, UInkShape>) PackShape(value);
				else if constexpr (std::is_same_v<T, UInkMedia>) PackMedia(value);
				else PackClear(value);
			}

			size_t WorkspaceRank(const UInkDocument& document,
				const std::optional<UInkGuid>& guid) const noexcept
			{
				if (!document.headerExtension || !guid) return 0;
				for (size_t index = 0; index < document.headerExtension->workspaces.size(); ++index)
					if (document.headerExtension->workspaces[index].guid == *guid) return index;
				return std::numeric_limits<size_t>::max();
			}

			size_t DeviceRank(const UInkDocument& document,
				const std::optional<UInkGuid>& guid) const noexcept
			{
				if (!document.headerExtension || !guid) return 0;
				for (size_t index = 0; index < document.headerExtension->devices.size(); ++index)
					if (document.headerExtension->devices[index].guid == *guid) return index;
				return std::numeric_limits<size_t>::max();
			}

			CanvasSortKey CanvasOrderKey(const UInkDocument& document,
				const UInkCanvas& canvas) const noexcept
			{
				return { WorkspaceRank(document, canvas.workspaceGuid), canvas.pageIndex,
					DeviceRank(document, canvas.deviceGuid), canvas.layerIndex };
			}

			bool ValidateDocument(const UInkDocument& document)
			{
				if (document.header.guid.IsZero()) return Fail("header.guid");
				if (document.canvases.size() > limits_.maxTopLevelObjects)
					return FailLimit("file.objects");
				if (document.headerExtension && !ValidateHeaderExtension(*document.headerExtension))
					return false;
				if (document.usesImplicitDevice != (!document.headerExtension ||
					document.headerExtension->devices.empty()) ||
					document.usesImplicitWorkspace != (!document.headerExtension ||
					document.headerExtension->workspaces.empty())) return Fail("headerExtension.registryMode");

				using OptionalGuidBytes = std::optional<GuidBytes>;
				using PageIndexKey = std::pair<OptionalGuidBytes, uint32_t>;
				using PageGroupKey = std::tuple<OptionalGuidBytes, OptionalGuidBytes, GuidBytes>;
				using CanvasKey = std::tuple<OptionalGuidBytes, OptionalGuidBytes,
					GuidBytes, uint32_t>;
				using PresentationSlideKey = std::pair<OptionalGuidBytes, int32_t>;

				std::set<GuidBytes> registeredDevices;
				std::map<GuidBytes, const UInkWorkspace*> registeredWorkspaces;
				if (document.headerExtension)
				{
					for (const UInkDevice& device : document.headerExtension->devices)
						registeredDevices.insert(device.guid.Bytes());
					for (const UInkWorkspace& workspace : document.headerExtension->workspaces)
						registeredWorkspaces.emplace(workspace.guid.Bytes(), &workspace);
				}

				std::map<GuidBytes, std::pair<OptionalGuidBytes, uint32_t>> pagesByGuid;
				std::map<PageIndexKey, GuidBytes> pagesByIndex;
				std::map<OptionalGuidBytes, std::set<uint32_t>> pageIndices;
				std::map<PageGroupKey, std::set<uint32_t>> layerIndices;
				std::set<CanvasKey> keys;
				std::map<GuidBytes, int32_t> presentationSlides;
				std::map<PresentationSlideKey, GuidBytes> presentationPagesBySlide;
				uint64_t objectCount = 1 + (document.headerExtension ? 1 : 0);
				uint64_t totalPoints = 0;
				for (const UInkCanvas& canvas : document.canvases)
				{
					if (!ValidateCanvas(canvas, document.usesImplicitWorkspace,
						document.usesImplicitDevice)) return false;
					const OptionalGuidBytes workspace = canvas.workspaceGuid ?
						OptionalGuidBytes(canvas.workspaceGuid->Bytes()) : std::nullopt;
					const OptionalGuidBytes device = canvas.deviceGuid ?
						OptionalGuidBytes(canvas.deviceGuid->Bytes()) : std::nullopt;
					if ((!document.usesImplicitDevice && registeredDevices.find(*device) ==
						registeredDevices.end()) || (!document.usesImplicitWorkspace &&
						registeredWorkspaces.find(*workspace) == registeredWorkspaces.end()))
						return Fail("canvas.registryReference");
					if (!document.usesImplicitWorkspace)
					{
						const UInkWorkspace* owner = registeredWorkspaces.find(*workspace)->second;
						if (owner->workspaceType == 2)
						{
							// 图层连续性检查保证同组有第 0 层，其他图层沿用该层的绑定。
							if (canvas.layerIndex == 0 && !HasValidUInkPresentationBinding(canvas))
								return Fail("canvas.slideId");
							if (canvas.slideId)
							{
								const auto inserted = presentationSlides.emplace(
									canvas.pageGuid.Bytes(), *canvas.slideId);
								if (!inserted.second && inserted.first->second != *canvas.slideId)
									return Fail("canvas.slideId");
								const auto pageInserted = presentationPagesBySlide.emplace(
									PresentationSlideKey(workspace, *canvas.slideId),
									canvas.pageGuid.Bytes());
								if (!pageInserted.second &&
									pageInserted.first->second != canvas.pageGuid.Bytes())
									return Fail("canvas.slideId");
							}
						}
					}

					const auto pageByGuid = pagesByGuid.emplace(canvas.pageGuid.Bytes(),
						std::make_pair(workspace, canvas.pageIndex));
					if (!pageByGuid.second && pageByGuid.first->second !=
						std::make_pair(workspace, canvas.pageIndex)) return Fail("canvas.pageGuid");
					const auto pageByIndex = pagesByIndex.emplace(
						PageIndexKey(workspace, canvas.pageIndex), canvas.pageGuid.Bytes());
					if (!pageByIndex.second && pageByIndex.first->second != canvas.pageGuid.Bytes())
						return Fail("canvas.pageIndex");
					pageIndices[workspace].insert(canvas.pageIndex);
					layerIndices[PageGroupKey(workspace, device, canvas.pageGuid.Bytes())]
						.insert(canvas.layerIndex);
					const CanvasKey key(workspace, device, canvas.pageGuid.Bytes(), canvas.layerIndex);
					if (!keys.insert(key).second) return Fail("canvas.identity");
					if (canvas.content.size() > std::numeric_limits<uint64_t>::max() -
						objectCount - 1) return FailLimit("file.objects");
					objectCount += 1 + canvas.content.size();
					for (const UInkContent& content : canvas.content)
					{
						const uint64_t contentPoints = std::visit([](const auto& value) -> uint64_t
						{
							using T = std::decay_t<decltype(value)>;
							if constexpr (std::is_same_v<T, UInkInk>) return value.points.size();
							else if constexpr (std::is_same_v<T, UInkShape>)
							{
								if (const UInkLineGeometry* line = std::get_if<UInkLineGeometry>(&value.geometry))
									return line->points.size();
							}
							return 0;
						}, content);
						if (contentPoints > limits_.maxGeometryPoints ||
							totalPoints > limits_.maxGeometryPoints - contentPoints)
							return FailLimit("geometry.points");
						totalPoints += contentPoints;
					}
				}
				for (const auto& [workspace, indices] : pageIndices)
				{
					uint32_t expected = 0;
					for (const uint32_t index : indices)
						if (index != expected++) return Fail("canvas.pageIndex");
				}
				for (const auto& [group, indices] : layerIndices)
				{
					(void)group;
					uint32_t expected = 0;
					for (const uint32_t index : indices)
						if (index != expected++) return Fail("canvas.layerIndex");
				}
				if (!document.usesImplicitWorkspace)
				{
					for (const auto& [guid, workspace] : registeredWorkspaces)
					{
						const auto indices = pageIndices.find(OptionalGuidBytes(guid));
						if (indices == pageIndices.end() ||
							indices->second.find(workspace->currentPageIndex) == indices->second.end())
							return Fail("workspace.currentPageIndex");
					}
				}
				if (objectCount > limits_.maxTopLevelObjects) return FailLimit("file.objects");
				const uint32_t expectedDevices = document.usesImplicitDevice ? 1u :
					static_cast<uint32_t>(registeredDevices.size());
				const uint32_t expectedWorkspaces = document.usesImplicitWorkspace ? 1u :
					static_cast<uint32_t>(registeredWorkspaces.size());
				if (pagesByGuid.size() > std::numeric_limits<uint32_t>::max())
					return FailLimit("header.pageNum");
				if (document.header.deviceNum != expectedDevices ||
					document.header.workspaceNum != expectedWorkspaces ||
					document.header.pageNum != pagesByGuid.size()) return Fail("header.snapshot");
				return true;
			}

			msgpack::sbuffer buffer_;
			msgpack::packer<msgpack::sbuffer> packer_;
			UInkReadLimits limits_;
			std::vector<UInkDiagnostic> diagnostics_;
			bool failed_ = false;
			bool limitExceeded_ = false;
		};

		template<typename Pack>
		UInkEncodeResult EncodeObjectStream(Pack&& pack, const UInkReadLimits& limits = {})
		{
			try
			{
				UInkEncoder encoder(limits);
				pack(encoder);
				return encoder.Finish();
			}
			catch (const std::bad_alloc&)
			{
				return { UInkEncodeStatus::LimitExceeded, {},
					{{ UInkDiagnosticCode::LimitExceeded, UInkDiagnosticSeverity::Fatal,
						0, 0, "allocation", 0 }} };
			}
			catch (...)
			{
				return { UInkEncodeStatus::InternalError, {},
					{{ UInkDiagnosticCode::WriteFailed, UInkDiagnosticSeverity::Error,
						0, 0, "encoder", 0 }} };
			}
		}
	}

	UInkEncodeResult EncodeUInkDocument(const UInkDocument& document)
	{
		return EncodeObjectStream([&](UInkEncoder& encoder) { encoder.PackDocument(document); });
	}

	UInkEncodeResult EncodeUInkHeaderPrefix(const UInkHeader& header,
		const std::optional<UInkHeaderExtension>& extension)
	{
		return EncodeObjectStream([&](UInkEncoder& encoder) { encoder.PackHeaderPrefix(header, extension); });
	}

	UInkEncodeResult EncodeUInkPage(const UInkCanvas& canvas, bool implicitWorkspace,
		bool implicitDevice, const UInkReadLimits& limits)
	{
		return EncodeObjectStream([&](UInkEncoder& encoder)
		{
			encoder.PackPage(canvas, implicitWorkspace, implicitDevice);
		}, limits);
	}

	UInkEncodeResult EncodeUInkContentBlocks(std::span<const UInkContent> blocks, uint64_t firstContentId,
		uint32_t previousUndoId, bool previousWasClear, const UInkReadLimits& limits)
	{
		return EncodeObjectStream([&](UInkEncoder& encoder)
		{
			encoder.PackContentBlocks(blocks, firstContentId, previousUndoId, previousWasClear);
		}, limits);
	}
}
