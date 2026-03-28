module;

#include "../../IdtMain.h"

#include <initializer_list>
#include <string_view>
#include <vector>

module Inkeys.Other.Config;

import Inkeys.Conv.Text;

namespace
{
	template <typename T>
	inline constexpr bool InkeysConfigDependentFalseV = false;

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
	struct JsonScalarTraits
	{
		static_assert(InkeysConfigDependentFalseV<ValueT>, "Inkeys::Config: unsupported scalar type in schema.");
	};

	template <>
	struct JsonScalarTraits<bool, void>
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
	struct JsonScalarTraits<ValueT, std::enable_if_t<std::is_integral_v<ValueT> && !std::is_same_v<ValueT, bool>>>
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
	struct JsonScalarTraits<ValueT, std::enable_if_t<std::is_floating_point_v<ValueT>>>
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
	struct JsonScalarTraits<std::string, void>
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
	struct JsonScalarTraits<std::wstring, void>
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
	struct JsonScalarTraits<IdtAtomic<ValueT>, void>
	{
		static bool TryRead(const Json::Value& jsonValue, IdtAtomic<ValueT>& outValue)
		{
			ValueT loadedValue{};
			if (!JsonScalarTraits<ValueT>::TryRead(jsonValue, loadedValue)) return false;
			outValue.store(loadedValue);
			return true;
		}

		static Json::Value ToJson(const IdtAtomic<ValueT>& value)
		{
			return JsonScalarTraits<ValueT>::ToJson(value.load());
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
		void HandleValue(const char* name, T& value, const T& defaultValue, bool)
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
		void HandleValue(const char* name, T& value, const T&, bool canReadFromDocument)
		{
			if (!canReadFromDocument && !includeWriteOnly) return;
			if (!IsSelectedValuePath(JoinPath(groupPath, name), selectedPaths)) return;

			const Json::Value* jsonValue = TryGetValueAtPath(root, groupPath, name);
			if (!jsonValue) return;

			T loadedValue{};
			if (!JsonScalarTraits<T>::TryRead(*jsonValue, loadedValue)) return;

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
		void HandleValue(const char* name, T& value, const T&, bool)
		{
			Json::Value& parent = EnsureObjectPath(root, groupPath);
			parent[name] = JsonScalarTraits<T>::ToJson(value);
		}

	private:
		Json::Value& root;
		std::vector<std::string> groupPath;
	};
}

namespace Inkeys
{
	bool Config::ReadAll()
	{
		unique_lock<shared_mutex> lock(rwMutex);
		return ReadImpl(std::vector<std::string>{});
	}

	bool Config::ReadMini(std::initializer_list<std::string_view> paths)
	{
		unique_lock<shared_mutex> lock(rwMutex);
		return ReadImpl(NormalizePaths(paths));
	}

	bool Config::Write()
	{
		unique_lock<shared_mutex> lock(rwMutex);
		Json::Value baseRoot = Json::Value(Json::objectValue);
		if (hasLoadedDocument)
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
			}
		}

		if (!baseRoot.isObject()) baseRoot = Json::Value(Json::objectValue);

		Json::Value outputRoot = LoadConfigValue(this->Config.autoClean) ? Json::Value(Json::objectValue) : baseRoot;
		OverlayDocument(outputRoot);

		if (!WriteDocumentToFile(GetFilePath(), outputRoot)) return false;

		loadedDocument = outputRoot;
		hasLoadedDocument = true;
		return true;
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

		ApplyDocument(parsedRoot, paths);
		loadedDocument = parsedRoot;
		hasLoadedDocument = true;
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
