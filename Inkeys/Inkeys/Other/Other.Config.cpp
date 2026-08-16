module;

#include "../../IdtMain.h"

#include <initializer_list>
#include <sstream>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

module Inkeys.Other.Config;

import Inkeys.Conv.Text;

namespace
{
	template <typename T>
	inline constexpr bool InkeysConfigDependentFalseV = false;

	constexpr bool IsRetiredLegacyBarButtonId(std::string_view id) noexcept
	{
		return id == "Inkeys.Bar.Pierce";
	}

	static_assert(IsRetiredLegacyBarButtonId("Inkeys.Bar.Pierce"));
	static_assert(!Inkeys::IsFixedButtonsA2Id("Inkeys.Bar.Pierce"));

	bool OccupyConfigFileForRead(HANDLE* hFile, const std::wstring& filePath)
	{
		if (!std::filesystem::exists(filePath)) return false;

		for (int time = 1; time <= 5; time++)
		{
			*hFile = CreateFileW(
				filePath.c_str(),
				GENERIC_READ,
				0,
				NULL,
				OPEN_EXISTING,
				FILE_ATTRIBUTE_NORMAL,
				NULL
			);

			if (*hFile != INVALID_HANDLE_VALUE) return true;
			if (time >= 3) return false;

			std::this_thread::sleep_for(std::chrono::milliseconds(100));
		}

		return false;
	}

	bool OccupyConfigFileForWrite(HANDLE* hFile, const std::wstring& filePath)
	{
		std::filesystem::path directoryPath = std::filesystem::path(filePath).parent_path();
		if (!std::filesystem::exists(directoryPath))
		{
			std::error_code ec;
			std::filesystem::create_directories(directoryPath, ec);
		}

		for (int time = 1; time <= 5; time++)
		{
			*hFile = CreateFileW(
				filePath.c_str(),
				GENERIC_READ | GENERIC_WRITE,
				0,
				NULL,
				OPEN_ALWAYS,
				FILE_ATTRIBUTE_NORMAL,
				NULL
			);

			if (*hFile != INVALID_HANDLE_VALUE) return true;
			if (time >= 3) return false;

			std::this_thread::sleep_for(std::chrono::milliseconds(100));
		}

		return false;
	}

	bool UnOccupyConfigFile(HANDLE* hFile)
	{
		if (*hFile != NULL && *hFile != INVALID_HANDLE_VALUE)
		{
			CloseHandle(*hFile);
			*hFile = NULL;
			return true;
		}
		return false;
	}

	std::vector<std::string> NormalizePaths(std::initializer_list<std::string_view> paths)
	{
		std::vector<std::string> result;
		result.reserve(paths.size());

		for (const std::string_view path : paths)
		{
			if (path.empty()) continue;
			result.emplace_back(path);
		}

		return result;
	}

	bool IsSelectedValuePath(std::string_view valuePath, const std::vector<std::string>& selectedPaths)
	{
		if (selectedPaths.empty()) return true;

		for (const std::string& selectedPath : selectedPaths)
		{
			if (selectedPath == valuePath) return true;

			if (valuePath.size() > selectedPath.size() &&
				valuePath.compare(0, selectedPath.size(), selectedPath) == 0 &&
				valuePath[selectedPath.size()] == '.')
			{
				return true;
			}
		}

		return false;
	}

	std::string JoinPath(const std::vector<std::string>& groupPath, std::string_view leaf = {})
	{
		std::string result;

		for (size_t i = 0; i < groupPath.size(); i++)
		{
			if (!result.empty()) result += '.';
			result += groupPath[i];
		}

		if (!leaf.empty())
		{
			if (!result.empty()) result += '.';
			result.append(leaf.data(), leaf.size());
		}

		return result;
	}

	std::string ResolveJsonGroupName(std::string_view schemaGroupName)
	{
		if (schemaGroupName == "Info") return "$Info";
		return std::string(schemaGroupName);
	}

	Json::Value& EnsureObjectPath(Json::Value& root, const std::vector<std::string>& groupPath)
	{
		if (!root.isObject()) root = Json::Value(Json::objectValue);

		Json::Value* current = &root;
		for (const std::string& name : groupPath)
		{
			Json::Value& next = (*current)[name];
			if (!next.isObject()) next = Json::Value(Json::objectValue);
			current = &next;
		}

		return *current;
	}

	const Json::Value* TryGetValueAtPath(const Json::Value& root, const std::vector<std::string>& groupPath, std::string_view leaf)
	{
		if (!root.isObject()) return nullptr;

		const Json::Value* current = &root;
		for (const std::string& name : groupPath)
		{
			if (!current->isMember(name) || !(*current)[name].isObject()) return nullptr;
			current = &(*current)[name];
		}

		const std::string leafName(leaf);
		if (!current->isMember(leafName)) return nullptr;
		return &(*current)[leafName];
	}

	template <typename ValueT, typename EnableT = void>
	struct JsonValueCodec
	{
		static_assert(InkeysConfigDependentFalseV<ValueT>, "Inkeys::Config: unsupported value type in schema.");
	};

	template <>
	struct JsonValueCodec<bool, void>
	{
		static bool TryRead(const Json::Value& jsonValue, bool& outValue)
		{
			if (!jsonValue.isBool()) return false;
			outValue = jsonValue.asBool();
			return true;
		}

		static Json::Value ToJson(bool value)
		{
			return Json::Value(value);
		}
	};

	template <typename ValueT>
	struct JsonValueCodec<ValueT, std::enable_if_t<std::is_integral_v<ValueT> && !std::is_same_v<ValueT, bool>>>
	{
		static bool TryRead(const Json::Value& jsonValue, ValueT& outValue)
		{
			if constexpr (std::is_signed_v<ValueT>)
			{
				if (!jsonValue.isIntegral()) return false;
				outValue = static_cast<ValueT>(jsonValue.asInt64());
			}
			else
			{
				if (!jsonValue.isUInt() && !jsonValue.isUInt64()) return false;
				outValue = static_cast<ValueT>(jsonValue.asUInt64());
			}
			return true;
		}

		static Json::Value ToJson(ValueT value)
		{
			if constexpr (std::is_signed_v<ValueT>) return Json::Value(static_cast<Json::Int64>(value));
			else return Json::Value(static_cast<Json::UInt64>(value));
		}
	};

	template <typename ValueT>
	struct JsonValueCodec<ValueT, std::enable_if_t<std::is_floating_point_v<ValueT>>>
	{
		static bool TryRead(const Json::Value& jsonValue, ValueT& outValue)
		{
			if (!jsonValue.isDouble() && !jsonValue.isInt() && !jsonValue.isUInt() && !jsonValue.isInt64() && !jsonValue.isUInt64()) return false;
			outValue = static_cast<ValueT>(jsonValue.asDouble());
			return true;
		}

		static Json::Value ToJson(ValueT value)
		{
			return Json::Value(static_cast<double>(value));
		}
	};

	template <>
	struct JsonValueCodec<std::string, void>
	{
		static bool TryRead(const Json::Value& jsonValue, std::string& outValue)
		{
			if (!jsonValue.isString()) return false;
			outValue = jsonValue.asString();
			return true;
		}

		static Json::Value ToJson(const std::string& value)
		{
			return Json::Value(value);
		}
	};

	template <>
	struct JsonValueCodec<std::wstring, void>
	{
		static bool TryRead(const Json::Value& jsonValue, std::wstring& outValue)
		{
			if (!jsonValue.isString()) return false;
			outValue = utf8ToUtf16(jsonValue.asString());
			return true;
		}

		static Json::Value ToJson(const std::wstring& value)
		{
			return Json::Value(utf16ToUtf8(value));
		}
	};

	template <typename ValueT>
	struct JsonValueCodec<IdtAtomic<ValueT>, void>
	{
		static bool TryRead(const Json::Value& jsonValue, IdtAtomic<ValueT>& outValue)
		{
			ValueT loadedValue{};
			if (!JsonValueCodec<ValueT>::TryRead(jsonValue, loadedValue)) return false;
			outValue.store(loadedValue);
			return true;
		}

		static Json::Value ToJson(const IdtAtomic<ValueT>& value)
		{
			return JsonValueCodec<ValueT>::ToJson(value.load());
		}
	};

bool TryReadBarButtonSizeKind(const Json::Value& jsonValue, Inkeys::BarButtonSizeKind& outSize)
		{
			if (!jsonValue.isString()) return false;
			const std::string text = jsonValue.asString();
			if (text == "twoTwo")
			{
				outSize = Inkeys::BarButtonSizeKind::TwoTwo;
				return true;
			}
			if (text == "twoOne")
			{
				outSize = Inkeys::BarButtonSizeKind::TwoOne;
				return true;
			}
			if (text == "oneTwo")
			{
				outSize = Inkeys::BarButtonSizeKind::OneTwo;
				return true;
			}
			if (text == "oneOne")
			{
				outSize = Inkeys::BarButtonSizeKind::OneOne;
				return true;
			}
			return false;
		}

		const char* BarButtonSizeKindToText(Inkeys::BarButtonSizeKind size)
		{
			switch (size)
			{
			case Inkeys::BarButtonSizeKind::TwoTwo: return "twoTwo";
			case Inkeys::BarButtonSizeKind::TwoOne: return "twoOne";
			case Inkeys::BarButtonSizeKind::OneTwo: return "oneTwo";
			case Inkeys::BarButtonSizeKind::OneOne: return "oneOne";
			}
			return "twoTwo";
		}

		// 固定区：读取 Id；Size 缺省/非法时先落默认，Load 时再纠正到注册默认。
		// 误带 Visible 时忽略，不导致整字段失败。
		template <>
		struct JsonValueCodec<Inkeys::BarFixedButtonLayoutEntry, void>
		{
			static bool TryRead(const Json::Value& jsonValue, Inkeys::BarFixedButtonLayoutEntry& outValue)
			{
				if (!jsonValue.isObject()) return false;
				if (!jsonValue.isMember("Id") || !jsonValue["Id"].isString()) return false;

				std::string id = jsonValue["Id"].asString();
				if (id.empty()) return false;

				Inkeys::BarButtonSizeKind size = Inkeys::DefaultSizeForBarButtonId(id);
				if (jsonValue.isMember("Size"))
				{
					Inkeys::BarButtonSizeKind parsedSize = size;
					if (TryReadBarButtonSizeKind(jsonValue["Size"], parsedSize)) size = parsedSize;
				}

				outValue = { std::move(id), size };
				return true;
			}

			static Json::Value ToJson(const Inkeys::BarFixedButtonLayoutEntry& value)
			{
				Json::Value result(Json::objectValue);
				result["Id"] = value.Id;
				result["Size"] = BarButtonSizeKindToText(value.Size);
				return result;
			}
		};

		template <>
		struct JsonValueCodec<Inkeys::BarExtensionButtonLayoutEntry, void>
		{
			static bool TryRead(const Json::Value& jsonValue, Inkeys::BarExtensionButtonLayoutEntry& outValue)
			{
				if (!jsonValue.isObject()) return false;
				if (!jsonValue.isMember("Id") || !jsonValue["Id"].isString()) return false;

				std::string id = jsonValue["Id"].asString();
				if (id.empty()) return false;

				Inkeys::BarButtonSizeKind size = Inkeys::DefaultSizeForBarButtonId(id);
				if (jsonValue.isMember("Size"))
				{
					Inkeys::BarButtonSizeKind parsedSize = size;
					if (TryReadBarButtonSizeKind(jsonValue["Size"], parsedSize)) size = parsedSize;
				}

				bool visible = true;
				if (jsonValue.isMember("Visible"))
				{
					if (!jsonValue["Visible"].isBool()) return false;
					visible = jsonValue["Visible"].asBool();
				}

				outValue = { std::move(id), size, visible };
				return true;
			}

			static Json::Value ToJson(const Inkeys::BarExtensionButtonLayoutEntry& value)
			{
				Json::Value result(Json::objectValue);
				result["Id"] = value.Id;
				result["Size"] = BarButtonSizeKindToText(value.Size);
				result["Visible"] = value.Visible;
				return result;
			}
		};

		// 旧单数组仅用于启动迁移，不进入 schema。
		template <>
		struct JsonValueCodec<Inkeys::BarLegacyButtonLayoutEntry, void>
		{
			static bool TryRead(const Json::Value& jsonValue, Inkeys::BarLegacyButtonLayoutEntry& outValue)
			{
				if (!jsonValue.isObject()) return false;
				if (!jsonValue.isMember("Id") || !jsonValue["Id"].isString()) return false;

				std::string id = jsonValue["Id"].asString();
				if (id.empty()) return false;

				bool visible = true;
				if (jsonValue.isMember("Visible"))
				{
					if (!jsonValue["Visible"].isBool()) return false;
					visible = jsonValue["Visible"].asBool();
				}

				outValue = { std::move(id), visible };
				return true;
			}

			static Json::Value ToJson(const Inkeys::BarLegacyButtonLayoutEntry& value)
			{
				Json::Value result(Json::objectValue);
				result["Id"] = value.Id;
				result["Visible"] = value.Visible;
				return result;
			}
		};

	template <typename ValueT>
	struct JsonValueCodec<ValueT, std::void_t<typename Inkeys::ConfigSequenceAdapter<ValueT>::ElementType>>
	{
		using AdapterT = Inkeys::ConfigSequenceAdapter<ValueT>;
		using ElementT = typename AdapterT::ElementType;

		static bool TryRead(const Json::Value& jsonValue, ValueT& outValue)
		{
			if (!jsonValue.isArray()) return false;

			std::vector<ElementT> loadedValues;
			loadedValues.reserve(jsonValue.size());
			for (Json::ArrayIndex index = 0; index < jsonValue.size(); index++)
			{
				ElementT loadedValue{};
				if (!JsonValueCodec<ElementT>::TryRead(jsonValue[index], loadedValue)) return false;
				loadedValues.emplace_back(std::move(loadedValue));
			}

			// 只有整个数组解析成功后，才由序列适配器一次性接收。
			AdapterT::Replace(outValue, std::move(loadedValues));
			return true;
		}

		static Json::Value ToJson(const ValueT& value)
		{
			Json::Value result(Json::arrayValue);
			const std::vector<ElementT> values = AdapterT::Snapshot(value);
			for (const ElementT& element : values)
			{
				result.append(JsonValueCodec<ElementT>::ToJson(element));
			}
			return result;
		}
	};

	template <typename ValueT>
	void AssignConfigValue(ValueT& target, const ValueT& source)
	{
		target = source;
	}

	template <typename ValueT>
	ValueT LoadConfigValue(const ValueT& value)
	{
		return value;
	}

	template <typename ValueT>
	ValueT LoadConfigValue(const IdtAtomic<ValueT>& value)
	{
		return value.load();
	}

	std::string JsonValueToUploadText(const Json::Value& jsonValue)
	{
		if (jsonValue.isString()) return jsonValue.asString();
		if (jsonValue.isBool()) return jsonValue.asBool() ? "true" : "false";
		if (jsonValue.isInt64()) return std::to_string(jsonValue.asInt64());
		if (jsonValue.isUInt64()) return std::to_string(jsonValue.asUInt64());
		if (jsonValue.isDouble())
		{
			std::ostringstream stream;
			stream << jsonValue.asDouble();
			return stream.str();
		}

		Json::StreamWriterBuilder writerBuilder;
		writerBuilder["indentation"] = "";
		return Json::writeString(writerBuilder, jsonValue);
	}

	void MakeUploadTextSingleLine(std::string& text)
	{
		for (char& ch : text)
		{
			if (ch == '\r' || ch == '\n') ch = ' ';
		}
	}

	bool UsesFullUploadPath(const char* uploadName)
	{
		return uploadName == nullptr || std::string_view(uploadName) == "NaN";
	}

	class DefaultValueHandler
	{
	public:
		explicit DefaultValueHandler(const std::vector<std::string>& selectedPathsIn)
			: selectedPaths(selectedPathsIn) {}

		void EnterGroup(const char* name)
		{
			groupPath.emplace_back(name);
		}

		void LeaveGroup()
		{
			if (!groupPath.empty()) groupPath.pop_back();
		}

		template <typename T>
		void HandleValue(const char* name, T& value, const T& defaultValue, bool, Inkeys::ConfigUploadMode, const char*)
		{
			if (!IsSelectedValuePath(JoinPath(groupPath, name), selectedPaths)) return;
			AssignConfigValue(value, defaultValue);
		}

	private:
		const std::vector<std::string>& selectedPaths;
		std::vector<std::string> groupPath;
	};

	class ReadDocumentHandler
	{
	public:
		ReadDocumentHandler(const Json::Value& rootIn, const std::vector<std::string>& selectedPathsIn, bool includeWriteOnlyIn)
			: root(rootIn), selectedPaths(selectedPathsIn), includeWriteOnly(includeWriteOnlyIn) {}

		void EnterGroup(const char* name)
		{
			groupPath.emplace_back(ResolveJsonGroupName(name));
		}

		void LeaveGroup()
		{
			if (!groupPath.empty()) groupPath.pop_back();
		}

		template <typename T>
		void HandleValue(const char* name, T& value, const T&, bool canReadFromDocument, Inkeys::ConfigUploadMode, const char*)
		{
			if (!canReadFromDocument && !includeWriteOnly) return;
			if (!IsSelectedValuePath(JoinPath(groupPath, name), selectedPaths)) return;

			const Json::Value* jsonValue = TryGetValueAtPath(root, groupPath, name);
			if (!jsonValue) return;

			T loadedValue{};
			if (!JsonValueCodec<T>::TryRead(*jsonValue, loadedValue)) return;

			AssignConfigValue(value, loadedValue);
		}

	private:
		const Json::Value& root;
		const std::vector<std::string>& selectedPaths;
		bool includeWriteOnly = false;
		std::vector<std::string> groupPath;
	};

	class WriteDocumentHandler
	{
	public:
		explicit WriteDocumentHandler(Json::Value& rootIn) : root(rootIn) {}

		void EnterGroup(const char* name)
		{
			groupPath.emplace_back(ResolveJsonGroupName(name));
			(void)EnsureObjectPath(root, groupPath);
		}

		void LeaveGroup()
		{
			if (!groupPath.empty()) groupPath.pop_back();
		}

		template <typename T>
		void HandleValue(const char* name, T& value, const T&, bool, Inkeys::ConfigUploadMode, const char*)
		{
			Json::Value& parent = EnsureObjectPath(root, groupPath);
			parent[name] = JsonValueCodec<T>::ToJson(value);
		}

	private:
		Json::Value& root;
		std::vector<std::string> groupPath;
	};

	class UploadInfoHandler
	{
	public:
		void EnterGroup(const char* name)
		{
			groupPath.emplace_back(name);
		}

		void LeaveGroup()
		{
			if (!groupPath.empty()) groupPath.pop_back();
		}

		template <typename T>
		void HandleValue(const char* name, T& value, const T&, bool, Inkeys::ConfigUploadMode uploadMode, const char* uploadName)
		{
			if (uploadMode != Inkeys::ConfigUploadMode::Upload) return;

			std::string valueText = JsonValueToUploadText(JsonValueCodec<T>::ToJson(value));
			MakeUploadTextSingleLine(valueText);
			const std::string keyText = UsesFullUploadPath(uploadName) ? JoinPath(groupPath, name) : std::string(uploadName);

			result += StringToUrlencode(keyText);
			result += '=';
			result += StringToUrlencode(valueText);
			result += ';';
		}

		const std::string& GetResult() const
		{
			return result;
		}

	private:
		std::string result;
		std::vector<std::string> groupPath;
	};
}

namespace Inkeys
{
	bool Config::ReadAll()
	{
		unique_lock<shared_mutex> lock(rwMutex);
		const bool readOk = ReadImpl(std::vector<std::string>{});
		if (readOk && hasLoadedDocument)
		{
			readAllDocument = loadedDocument;
			hasReadAllDocument = true;
		}
		else
		{
			readAllDocument = Json::Value(Json::objectValue);
			hasReadAllDocument = false;
		}
		return readOk;
	}

	bool Config::ReadMini(std::initializer_list<std::string_view> paths)
	{
		unique_lock<shared_mutex> lock(rwMutex);
		return ReadImpl(NormalizePaths(paths));
	}

	bool Config::Write()
	{
		unique_lock<shared_mutex> lock(rwMutex);
		const bool autoCleanEnabled = LoadConfigValue(this->Config.AutoClean);

		Json::Value baseRoot = Json::Value(Json::objectValue);
		if (!autoCleanEnabled)
		{
			if (hasReadAllDocument)
			{
				baseRoot = readAllDocument;
			}
			else if (hasLoadedDocument)
			{
				baseRoot = loadedDocument;
			}
			else
			{
				Json::Value loadedRoot;
				if (LoadDocumentOnly(loadedRoot))
				{
					baseRoot = loadedRoot;
					loadedDocument = loadedRoot;
					hasLoadedDocument = true;
					readAllDocument = loadedRoot;
					hasReadAllDocument = true;
				}
			}

			if (!baseRoot.isObject()) baseRoot = Json::Value(Json::objectValue);
		}

		Json::Value outputRoot = autoCleanEnabled ? Json::Value(Json::objectValue) : baseRoot;
		OverlayDocument(outputRoot);

		if (!WriteDocumentToFile(GetFilePath(), outputRoot)) return false;

		loadedDocument = outputRoot;
		hasLoadedDocument = true;
		readAllDocument = outputRoot;
		hasReadAllDocument = true;
		return true;
	}

	std::string Config::GetUploadInfo()
	{
		shared_lock<shared_mutex> lock(rwMutex);
		UploadInfoHandler handler;
		TraverseSchema(handler);
		return handler.GetResult();
	}

	std::wstring Config::GetFilePath() const
	{
		return globalPath + L"Inkeys\\Config\\main.json";
	}

	void Config::ResetToDefaults()
	{
		ApplyDefaults(std::vector<std::string>{});
	}

	bool Config::ReadImpl(const std::vector<std::string>& paths)
	{
		ApplyDefaults(paths);

		Json::Value parsedRoot;
		if (!LoadDocumentOnly(parsedRoot))
		{
			loadedDocument = Json::Value(Json::objectValue);
			hasLoadedDocument = false;
			return false;
		}

		// 先迁移旧单数组，再套用文档，避免新区默认值挡住拆分结果。
		// 直接写回完整迁移文档；不能依赖 Bar 后续恰好再次规范化 A1/A2。
		if (TryMigrateLegacyBarButtonLayout(parsedRoot))
			(void)WriteDocumentToFile(GetFilePath(), parsedRoot);
		ApplyDocument(parsedRoot, paths);
		loadedDocument = parsedRoot;
		hasLoadedDocument = true;
		return true;
	}

	bool Config::TryMigrateLegacyBarButtonLayout(Json::Value& root)
	{
		if (!root.isObject() || !root.isMember("UI") || !root["UI"].isObject()) return false;
		Json::Value& uiNode = root["UI"];
		if (!uiNode.isMember("Bar") || !uiNode["Bar"].isObject()) return false;
		Json::Value& barNode = uiNode["Bar"];

		// 已有任一新字段时不再从旧数组迁移，避免覆盖用户新区配置。
		const bool hasNewLayoutFields =
			barNode.isMember("FixedButtonsA1")
			|| barNode.isMember("ExtensionButtons")
			|| barNode.isMember("FixedButtonsA2");
		if (hasNewLayoutFields) return false;
		if (!barNode.isMember("ButtonLayout") || !barNode["ButtonLayout"].isArray()) return false;

		auto sizeText = [](BarButtonSizeKind size) -> const char*
		{
			switch (size)
			{
			case BarButtonSizeKind::TwoTwo: return "twoTwo";
			case BarButtonSizeKind::TwoOne: return "twoOne";
			case BarButtonSizeKind::OneTwo: return "oneTwo";
			case BarButtonSizeKind::OneOne: return "oneOne";
			}
			return "twoTwo";
		};

		auto makeFixedObject = [&](const std::string& id) -> Json::Value
		{
			Json::Value item(Json::objectValue);
			item["Id"] = id;
			item["Size"] = sizeText(DefaultSizeForBarButtonId(id));
			return item;
		};

		auto makeExtensionObject = [&](const std::string& id, bool visible) -> Json::Value
		{
			Json::Value item(Json::objectValue);
			item["Id"] = id;
			item["Size"] = sizeText(DefaultSizeForBarButtonId(id));
			item["Visible"] = visible;
			return item;
		};

		Json::Value fixedA1Json(Json::arrayValue);
		Json::Value extensionJson(Json::arrayValue);
		Json::Value fixedA2Json(Json::arrayValue);

		for (Json::ArrayIndex index = 0; index < barNode["ButtonLayout"].size(); index++)
		{
			const Json::Value& jsonValue = barNode["ButtonLayout"][index];
			if (!jsonValue.isObject()) return false;
			if (!jsonValue.isMember("Id") || !jsonValue["Id"].isString()) return false;

			const std::string id = jsonValue["Id"].asString();
			if (id.empty()) return false;

			bool visible = true;
			if (jsonValue.isMember("Visible"))
			{
				if (!jsonValue["Visible"].isBool()) return false;
				visible = jsonValue["Visible"].asBool();
			}

// 旧布局中的 Divider 不迁入三区配置；交界分割线改由运行时注入。
			if (IsRuntimeBoundaryDividerId(id)) continue;
			// 旧 Pierce 已退出产品合同，迁移时直接丢弃而不是伪装成扩展按钮。
			if (IsRetiredLegacyBarButtonId(id)) continue;
			if (IsFixedButtonsA1Id(id)) fixedA1Json.append(makeFixedObject(id));
			else if (IsFixedButtonsA2Id(id)) fixedA2Json.append(makeFixedObject(id));
			else extensionJson.append(makeExtensionObject(id, visible));
			}

		// 迁移结果写回文档节点；A 区是否合法由后续 Load 严校验决定。
		barNode["FixedButtonsA1"] = fixedA1Json;
		barNode["ExtensionButtons"] = extensionJson;
		barNode["FixedButtonsA2"] = fixedA2Json;
		barNode.removeMember("ButtonLayout");
		return true;
	}
	void Config::ApplyDefaults(const std::vector<std::string>& paths)
	{
		DefaultValueHandler handler(paths);
		TraverseSchema(handler);
	}

	void Config::ApplyDocument(const Json::Value& root, const std::vector<std::string>& paths, bool includeWriteOnly)
	{
		ReadDocumentHandler handler(root, paths, includeWriteOnly);
		TraverseSchema(handler);
	}

	void Config::OverlayDocument(Json::Value& root)
	{
		WriteDocumentHandler handler(root);
		TraverseSchema(handler);
	}

	bool Config::LoadDocumentOnly(Json::Value& outRoot) const
	{
		outRoot = Json::Value(Json::objectValue);

		HANDLE fileHandle = NULL;
		if (!OccupyConfigFileForRead(&fileHandle, GetFilePath()))
		{
			UnOccupyConfigFile(&fileHandle);
			return false;
		}

		LARGE_INTEGER fileSize;
		if (!GetFileSizeEx(fileHandle, &fileSize))
		{
			UnOccupyConfigFile(&fileHandle);
			return false;
		}

		if (fileSize.QuadPart > static_cast<LONGLONG>(MAXDWORD))
		{
			UnOccupyConfigFile(&fileHandle);
			return false; // 文件过大
		}

		const DWORD dwSize = static_cast<DWORD>(fileSize.QuadPart);
		std::string jsonContent(dwSize, '\0');

		DWORD bytesRead = 0;
		if (SetFilePointer(fileHandle, 0, NULL, FILE_BEGIN) == INVALID_SET_FILE_POINTER)
		{
			UnOccupyConfigFile(&fileHandle);
			return false;
		}

		if (dwSize > 0)
		{
			if (!ReadFile(fileHandle, jsonContent.data(), dwSize, &bytesRead, NULL) || bytesRead != dwSize)
			{
				UnOccupyConfigFile(&fileHandle);
				return false;
			}
		}

		UnOccupyConfigFile(&fileHandle);

		if (jsonContent.compare(0, 3, "\xEF\xBB\xBF") == 0) jsonContent.erase(0, 3);

		std::istringstream jsonContentStream(jsonContent);
		Json::CharReaderBuilder readerBuilder;
		std::string jsonErr;

		if (!Json::parseFromStream(readerBuilder, jsonContentStream, &outRoot, &jsonErr)) return false;
		return true;
	}

	bool Config::WriteDocumentToFile(const std::wstring& filePath, const Json::Value& root)
	{
		HANDLE fileHandle = NULL;
		if (!OccupyConfigFileForWrite(&fileHandle, filePath))
		{
			UnOccupyConfigFile(&fileHandle);
			return false;
		}

		if (SetFilePointer(fileHandle, 0, NULL, FILE_BEGIN) == INVALID_SET_FILE_POINTER)
		{
			UnOccupyConfigFile(&fileHandle);
			return false;
		}

		if (!SetEndOfFile(fileHandle))
		{
			UnOccupyConfigFile(&fileHandle);
			return false;
		}

		Json::StreamWriterBuilder writerBuilder;
		writerBuilder["indentation"] = "\t";
		std::string jsonContent = "\xEF\xBB\xBF" + Json::writeString(writerBuilder, root);

		DWORD bytesWritten = 0;
		if (!WriteFile(fileHandle, jsonContent.data(), static_cast<DWORD>(jsonContent.size()), &bytesWritten, NULL) ||
			bytesWritten != jsonContent.size())
		{
			UnOccupyConfigFile(&fileHandle);
			return false;
		}

		UnOccupyConfigFile(&fileHandle);
		return true;
	}
}
