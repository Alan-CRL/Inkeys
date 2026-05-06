module;

#include "../../IdtMain.h"

#include <initializer_list>
#include <mutex>
#include <string_view>
#include <vector>

export module Inkeys.Other.Config;

export namespace Inkeys
{
	enum class ConfigUploadMode
	{
		NoUpload,
		Upload
	};
}

namespace Inkeys::ConfigDetail
{
	template <typename HandlerT>
	class SchemaWalker
	{
	public:
		explicit SchemaWalker(HandlerT& handlerIn) : handler(handlerIn) {}

		template <typename GroupT, typename FnT>
		void BeginGroup(const char* name, GroupT& group, FnT&& fn)
		{
			handler.EnterGroup(name);
			fn(group);
			handler.LeaveGroup();
		}

		template <typename ValueT>
		void Value(const char* name, ValueT& value, const ValueT& defaultValue, bool canReadFromDocument, Inkeys::ConfigUploadMode uploadMode, const char* uploadName)
		{
			handler.HandleValue(name, value, defaultValue, canReadFromDocument, uploadMode, uploadName);
		}

	private:
		HandlerT& handler;
	};
}

#define INKEYS_CONFIG_SCHEMA(GROUP, X, H) \
	GROUP(Config, \
		X(ConfigUploadMode::NoUpload, "NaN", IdtAtomic<bool>, AutoClean, false) \
	) \
	GROUP(Info, \
		H(ConfigUploadMode::NoUpload, "NaN", std::wstring, UserId, ::userId) \
		H(ConfigUploadMode::Upload, "ev", std::wstring, EditionVersion, ::editionVersion) \
		H(ConfigUploadMode::Upload, "ed", std::wstring, EditionDate, ::editionDate) \
		H(ConfigUploadMode::Upload, "pa", std::wstring, ProgramArchitecture, ::programArchitecture) \
		H(ConfigUploadMode::NoUpload, "NaN", std::wstring, TargetArchitecture, ::targetArchitecture) \
		H(ConfigUploadMode::Upload, "we", std::wstring, WindowsEdition, ::windowsEdition) \
	) \
	GROUP(PlugIn, \
		GROUP(PPTHelper, \
			X(ConfigUploadMode::NoUpload, "NaN", IdtAtomic<bool>, AutoTakeOver, false) \
			X(ConfigUploadMode::NoUpload, "NaN", IdtAtomic<bool>, AutoTakeOverOnce, true) \
			X(ConfigUploadMode::NoUpload, "NaN", IdtAtomic<bool>, AutoTakeOverExpand, true) \
			GROUP(Tentative, \
				X(ConfigUploadMode::NoUpload, "NaN", IdtAtomic<bool>, EnablePageButtonLongPress, false) \
			) \
		) \
	)

namespace Inkeys
{
	/*
	示例：

	Inkeys::Config cfg;
	cfg.ReadMini({ "PlugIn.PPTHelper" });
	bool enabled = cfg.PlugIn.PPTHelper.autoTakeOver.load();

	新增项时，只需要在下面的 schema 里继续写这一处：
	GROUP(...)
		X(...)
	*/
	export class Config
	{
	public:
	#define INKEYS_CONFIG_DECLARE_GROUP(groupName, ...) struct groupName##_Node { __VA_ARGS__ } groupName;
	#define INKEYS_CONFIG_DECLARE_VALUE(uploadMode, uploadName, valueType, valueName, defaultValue) valueType valueName{ defaultValue };
		INKEYS_CONFIG_SCHEMA(INKEYS_CONFIG_DECLARE_GROUP, INKEYS_CONFIG_DECLARE_VALUE, INKEYS_CONFIG_DECLARE_VALUE)
		#undef INKEYS_CONFIG_DECLARE_GROUP
		#undef INKEYS_CONFIG_DECLARE_VALUE

	public:
		Inkeys::Config& operator=(const Inkeys::Config& other);

		bool ReadAll();
		bool ReadMini(std::initializer_list<std::string_view> paths);
		bool Write();
		std::string GetUploadInfo();
		std::wstring GetFilePath() const;
		void ResetToDefaults();

	private:
		mutable shared_mutex rwMutex;

	private:
		Json::Value loadedDocument = Json::Value(Json::objectValue);
		bool hasLoadedDocument = false;
		Json::Value readAllDocument = Json::Value(Json::objectValue);
		bool hasReadAllDocument = false;

		template <typename HandlerT>
		void TraverseSchema(HandlerT&& handler)
		{
			auto& group = *this;
			Inkeys::ConfigDetail::SchemaWalker<HandlerT> walker(handler);

		#define INKEYS_CONFIG_TRAVERSE_GROUP(groupName, ...) walker.BeginGroup(#groupName, group.groupName, [&](auto& group) { __VA_ARGS__ });
		#define INKEYS_CONFIG_TRAVERSE_X_VALUE(uploadMode, uploadName, valueType, valueName, defaultValue) walker.Value(#valueName, group.valueName, valueType{ defaultValue }, true, uploadMode, uploadName);
		#define INKEYS_CONFIG_TRAVERSE_H_VALUE(uploadMode, uploadName, valueType, valueName, defaultValue) walker.Value(#valueName, group.valueName, valueType{ defaultValue }, false, uploadMode, uploadName);
			INKEYS_CONFIG_SCHEMA(INKEYS_CONFIG_TRAVERSE_GROUP, INKEYS_CONFIG_TRAVERSE_X_VALUE, INKEYS_CONFIG_TRAVERSE_H_VALUE)
			#undef INKEYS_CONFIG_TRAVERSE_GROUP
			#undef INKEYS_CONFIG_TRAVERSE_X_VALUE
			#undef INKEYS_CONFIG_TRAVERSE_H_VALUE
		}

		bool ReadImpl(const std::vector<std::string>& paths);
		void ApplyDefaults(const std::vector<std::string>& paths);
		void ApplyDocument(const Json::Value& root, const std::vector<std::string>& paths, bool includeWriteOnly = false);
		void OverlayDocument(Json::Value& root);
		bool LoadDocumentOnly(Json::Value& outRoot) const;
		static bool WriteDocumentToFile(const std::wstring& filePath, const Json::Value& root);
	};

	inline Inkeys::Config& Config::operator=(const Inkeys::Config& other)
	{
		if (this == &other) return *this;

		unique_lock<shared_mutex> thisLock(rwMutex, defer_lock);
		shared_lock<shared_mutex> otherLock(other.rwMutex, defer_lock);
		std::lock(thisLock, otherLock);

		Json::Value snapshot(Json::objectValue);
		const_cast<Inkeys::Config&>(other).OverlayDocument(snapshot);

		ApplyDocument(snapshot, std::vector<std::string>{}, true);
		loadedDocument = other.loadedDocument;
		hasLoadedDocument = other.hasLoadedDocument;
		readAllDocument = other.readAllDocument;
		hasReadAllDocument = other.hasReadAllDocument;
		return *this;
	}

	// 全局配置集合
	export inline Config config{}; // 实时配置集
	export inline Config configOnce{}; // 首次启动配置集
}

#undef INKEYS_CONFIG_SCHEMA

/*
维护说明：

这套配置的目标是：后续只改上面的 INKEYS_CONFIG_SCHEMA(...) 这一处，
不再手动修改其他结构体、反射表、读写逻辑。

一、添加一个配置项
直接在目标 GROUP 内新增一行 X(ConfigUploadMode::NoUpload, "NaN", type, name, defaultValue)。

示例：
GROUP(Config, \
	X(ConfigUploadMode::NoUpload, "NaN", IdtAtomic<bool>, autoClean, false) \
	X(ConfigUploadMode::NoUpload, "NaN", int, saveVersion, 1) \
)

添加后即可自动获得：
1. 成员访问：config.Config.saveVersion
2. ReadAll / ReadMini 读取
3. Write 写回
4. JSON 路径：Config.saveVersion

二、修改一个配置项
直接修改对应的 X(...) 即可。

示例：
X(ConfigUploadMode::NoUpload, "NaN", bool, autoClean, false)
改成
X(ConfigUploadMode::NoUpload, "NaN", IdtAtomic<bool>, autoClean, false)

或者：
X(ConfigUploadMode::NoUpload, "NaN", int, saveVersion, 1)
改成
X(ConfigUploadMode::NoUpload, "NaN", int, saveVersion, 2)

说明：
1. 改类型：只改这一处即可。
2. 改默认值：只改这一处即可。
3. 改名字：JSON 键名也会跟着变。
   旧名字对应的旧 JSON 键不会自动迁移。
   如果 Config.autoClean 为 false，旧键会保留；
   如果 Config.autoClean 为 true，下次 Write 时旧键会被清理。

三、删除一个配置项
直接删除对应的 X(...) 行。

示例：
删除
X(ConfigUploadMode::NoUpload, "NaN", IdtAtomic<bool>, autoTakeOverExpand, false)

说明：
1. 代码里的成员访问路径会同步消失。
2. 如果 Config.autoClean 为 false，磁盘里的旧 JSON 键会暂时保留。
3. 如果 Config.autoClean 为 true，下一次 Write 时旧 JSON 键会被清理。

四、添加一个嵌套 GROUP
直接在 schema 里继续嵌套 GROUP(...)。

示例：
GROUP(Experimental, \
	GROUP(Inkeys3, \
		GROUP(UI3, \
			X(ConfigUploadMode::NoUpload, "NaN", IdtAtomic<bool>, enable, false) \
		) \
	) \
)

添加后即可自动获得：
1. 成员访问：config.Experimental.Inkeys3.UI3.enable
2. JSON 路径：Experimental.Inkeys3.UI3.enable
3. mini 读取：
   config.ReadMini({ "Experimental.Inkeys3" });
   config.ReadMini({ "Experimental.Inkeys3.UI3.enable" });

五、删除一个嵌套 GROUP
直接删除整个 GROUP(...) 块即可。

说明：
1. 这个 GROUP 下所有成员都会一起消失。
2. autoClean 的行为和删除单项相同。

六、推荐使用方式
1. 全量读取：
   config.ReadAll();
2. 仅读一部分：
   config.ReadMini({ "PlugIn.PPTHelper" });
3. 一次读取多个对象或叶子：
   config.ReadMini({ "Config", "PlugIn.PPTHelper", "Experimental.Inkeys3.UI3.enable" });
4. 读取 atomic 值：
   bool enabled = config.PlugIn.PPTHelper.autoTakeOver.load();
5. 修改后写回：
   config.PlugIn.PPTHelper.autoTakeOver = true;
   config.Write();

七、当前支持的类型
1. bool
2. 整数类型
3. 浮点类型
4. std::string
5. std::wstring
6. IdtAtomic<以上类型>

如果新增了这里未支持的类型，需要去 Other.Config.cpp 里补 JsonScalarTraits。
除这种“新增全新类型支持”的情况外，日常新增、修改、删除配置项或 GROUP，
都只需要改 INKEYS_CONFIG_SCHEMA(...) 这一处。
*/