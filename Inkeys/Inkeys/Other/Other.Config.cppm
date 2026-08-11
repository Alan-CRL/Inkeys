module;

#include "../../IdtMain.h"

#include <initializer_list>
#include <mutex>
#include <shared_mutex>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

export module Inkeys.Other.Config;

export namespace Inkeys
{
	enum class ConfigUploadMode
	{
		NoUpload,
		Upload
	};

	namespace BarButtonId
	{
		inline constexpr char Divider[] = "Inkeys.Bar.Divider";
		inline constexpr char MoreBoundary[] = "Inkeys.Bar.MoreBoundary";
		inline constexpr char Select[] = "Inkeys.Bar.Select";
		inline constexpr char Draw[] = "Inkeys.Bar.Draw";
		inline constexpr char Eraser[] = "Inkeys.Bar.Eraser";
		inline constexpr char Geometry[] = "Inkeys.Bar.Geometry";
		inline constexpr char Recall[] = "Inkeys.Bar.Recall";
		inline constexpr char Clean[] = "Inkeys.Bar.Clean";
		inline constexpr char Pierce[] = "Inkeys.Bar.Pierce";
		inline constexpr char Freeze[] = "Inkeys.Bar.Freeze";
		inline constexpr char Setting[] = "Inkeys.Bar.Setting";
	}

// 与 BarButtonSizeEnum 对应的配置侧尺寸枚举，JSON 使用同名字符串。
		enum class BarButtonSizeKind
		{
			TwoTwo,
			TwoOne,
			OneTwo,
			OneOne
		};

		// A1/A2 固定区：只存 Id 与 Size；Visible 不进配置。
		struct BarFixedButtonLayoutEntry
		{
			std::string Id;
			BarButtonSizeKind Size = BarButtonSizeKind::TwoTwo;
		};

		// B 扩展区：Id + Size + 用户显隐。
		struct BarExtensionButtonLayoutEntry
		{
			std::string Id;
			BarButtonSizeKind Size = BarButtonSizeKind::TwoTwo;
			bool Visible = true;
		};

		// 兼容旧单数组 ButtonLayout 的读取形态（仅迁移使用）。
		struct BarLegacyButtonLayoutEntry
		{
			std::string Id;
			bool Visible = true;
		};

		template <typename ElementT>
		class ConfigSequence
		{
		public:
			using ElementType = ElementT;

			ConfigSequence() = default;
			ConfigSequence(std::initializer_list<ElementT> valuesIn) : values(valuesIn) {}
			ConfigSequence(const ConfigSequence& other) : values(other.Snapshot()) {}

			ConfigSequence& operator=(const ConfigSequence& other)
			{
				if (this != &other) Replace(other.Snapshot());
				return *this;
			}

			std::vector<ElementT> Snapshot() const
			{
				// 序列化及外部读取都先取得一致快照，避免观察到修改中的半成品。
				std::shared_lock lock(rwMutex);
				return values;
			}

			void Replace(std::vector<ElementT> valuesIn)
			{
				// 完整的新序列只在独占锁内一次性替换。
				std::unique_lock lock(rwMutex);
				values = std::move(valuesIn);
			}

		private:
			mutable std::shared_mutex rwMutex;
			std::vector<ElementT> values;
		};

		// 自定义序列类型可特化该适配器，向配置层提供一致快照和事务式替换。
		template <typename SequenceT>
		struct ConfigSequenceAdapter;

		template <typename ElementT>
		struct ConfigSequenceAdapter<ConfigSequence<ElementT>>
		{
			using ElementType = ElementT;

			static std::vector<ElementT> Snapshot(const ConfigSequence<ElementT>& sequence)
			{
				return sequence.Snapshot();
			}

			static void Replace(ConfigSequence<ElementT>& sequence, std::vector<ElementT> values)
			{
				sequence.Replace(std::move(values));
			}
		};

		inline BarButtonSizeKind DefaultSizeForBarButtonId(std::string_view id)
		{
			if (id == BarButtonId::Divider) return BarButtonSizeKind::OneTwo;
			if (id == BarButtonId::Pierce || id == BarButtonId::Freeze) return BarButtonSizeKind::TwoOne;
			return BarButtonSizeKind::TwoTwo;
		}

inline bool IsFixedButtonsA1Id(std::string_view id)
			{
				// Divider 不进 A1 配置；仅在 A1|B / B|A2 交界由运行时注入。
				return id == BarButtonId::Select
					|| id == BarButtonId::Draw
					|| id == BarButtonId::Eraser
					|| id == BarButtonId::Geometry
					|| id == BarButtonId::Recall
					|| id == BarButtonId::Clean;
			}

			inline bool IsRuntimeBoundaryDividerId(std::string_view id)
			{
				return id == BarButtonId::Divider;
			}

		inline bool IsFixedButtonsA2Id(std::string_view id)
		{
			return id == BarButtonId::Pierce
				|| id == BarButtonId::Freeze;
		}

		inline bool IsOfficialFixedBarButtonId(std::string_view id)
		{
			return IsFixedButtonsA1Id(id) || IsFixedButtonsA2Id(id);
		}

		// 官方按钮 ID 必须以 Inkeys. 开头（当前形如 Inkeys.Bar.Select）。
		inline bool IsOfficialBarButtonIdPrefix(std::string_view id)
		{
			constexpr std::string_view kPrefix = "Inkeys.";
			return id.size() > kPrefix.size() && id.substr(0, kPrefix.size()) == kPrefix;
		}

		// 点分 ID：至少两段，形如 xxx.xxx 或 xxx.xxx.xxx；不允许首尾点或空段。
		inline bool IsDottedBarButtonId(std::string_view id)
		{
			if (id.empty() || id.front() == '.' || id.back() == '.') return false;

			bool sawDot = false;
			bool segmentNonEmpty = false;
			for (const char ch : id)
			{
				if (ch == '.')
				{
					if (!segmentNonEmpty) return false;
					sawDot = true;
					segmentNonEmpty = false;
					continue;
				}
				segmentNonEmpty = true;
			}

			return sawDot && segmentNonEmpty;
		}

		// 扩展/插件按钮：点分 ID，且不得占用官方 Inkeys. 前缀。
		inline bool IsExtensionBarButtonId(std::string_view id)
		{
			return IsDottedBarButtonId(id) && !IsOfficialBarButtonIdPrefix(id);
		}

inline ConfigSequence<BarFixedButtonLayoutEntry> MakeDefaultFixedButtonsA1()
			{
				return {
					{ BarButtonId::Select, BarButtonSizeKind::TwoTwo },
					{ BarButtonId::Draw, BarButtonSizeKind::TwoTwo },
					{ BarButtonId::Geometry, BarButtonSizeKind::TwoTwo },
					{ BarButtonId::Eraser, BarButtonSizeKind::TwoTwo },
					{ BarButtonId::Recall, BarButtonSizeKind::TwoTwo },
					{ BarButtonId::Clean, BarButtonSizeKind::TwoTwo },
				};
			}

		inline ConfigSequence<BarExtensionButtonLayoutEntry> MakeDefaultExtensionButtons()
		{
			return {
				{ BarButtonId::MoreBoundary, BarButtonSizeKind::TwoTwo, true },
				{ BarButtonId::Setting, BarButtonSizeKind::TwoTwo, true },
			};
		}

		inline ConfigSequence<BarFixedButtonLayoutEntry> MakeDefaultFixedButtonsA2()
		{
			return {
				{ BarButtonId::Pierce, BarButtonSizeKind::TwoOne },
				{ BarButtonId::Freeze, BarButtonSizeKind::TwoOne },
			};
		}
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
GROUP(UI, \
			GROUP(Bar, \
				X(ConfigUploadMode::NoUpload, "NaN", IdtAtomic<double>, Zoom, 1.0) \
				X(ConfigUploadMode::NoUpload, "NaN", ConfigSequence<BarFixedButtonLayoutEntry>, FixedButtonsA1, MakeDefaultFixedButtonsA1()) \
				X(ConfigUploadMode::NoUpload, "NaN", ConfigSequence<BarExtensionButtonLayoutEntry>, ExtensionButtons, MakeDefaultExtensionButtons()) \
				X(ConfigUploadMode::NoUpload, "NaN", ConfigSequence<BarFixedButtonLayoutEntry>, FixedButtonsA2, MakeDefaultFixedButtonsA2()) \
			) \
		) \
	GROUP(Experimental, \
		GROUP(Inkeys3, \
			GROUP(UI3, \
				GROUP(Debug, \
					X(ConfigUploadMode::NoUpload, "NaN", IdtAtomic<bool>, Enable, false) \
					X(ConfigUploadMode::NoUpload, "NaN", IdtAtomic<bool>, ShowFrameRate, true) \
				) \
				GROUP(EdgeLighting, \
					X(ConfigUploadMode::NoUpload, "NaN", IdtAtomic<bool>, Enable, true) \
					X(ConfigUploadMode::NoUpload, "NaN", IdtAtomic<bool>, Dynamic, true) \
				) \
				GROUP(Animation, \
					X(ConfigUploadMode::NoUpload, "NaN", IdtAtomic<bool>, Enable, true) \
					X(ConfigUploadMode::NoUpload, "NaN", IdtAtomic<double>, SpeedRate, 1.0) \
				) \
			) \
		) \
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
			// 旧 UI.Bar.ButtonLayout 单数组拆到 A1/B/A2；仅在新区字段缺失时执行。
			bool TryMigrateLegacyBarButtonLayout(Json::Value& root);
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

七、当前支持的值类型
1. bool
2. 整数类型
3. 浮点类型
4. std::string
5. std::wstring
6. IdtAtomic<以上类型>
7. ConfigSequence<元素类型>

八、添加顺序序列
ConfigSequence<T> 内部使用 shared_mutex。读取 JSON 时会先完整解析数组，
全部元素有效后再一次性替换；写入 JSON 时先在读锁下取得一致快照。

自定义元素类型需要在 Other.Config.cpp 中补 JsonValueCodec<T>，
负责单个元素与 Json::Value 之间的校验和转换。

如果已有自定义序列容器，不需要改为 std::vector：
在本模块中为它特化 ConfigSequenceAdapter<T>，提供 ElementType、
Snapshot(...) 和 Replace(...)，即可复用相同的 JSON 数组协议。

如果新增了这里未支持的普通值类型，需要去 Other.Config.cpp 里补 JsonValueCodec。
除这种“新增全新类型支持”的情况外，日常新增、修改、删除配置项或 GROUP，
都只需要改 INKEYS_CONFIG_SCHEMA(...) 这一处。
*/
