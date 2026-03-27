module;

#include "../../IdtMain.h"

#include <initializer_list>
#include <string_view>
#include <vector>

export module Inkeys.Other.Config;

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
		void Value(const char* name, ValueT& value, const ValueT& defaultValue)
		{
			handler.HandleValue(name, value, defaultValue);
		}

	private:
		HandlerT& handler;
	};
}

#define INKEYS_CONFIG_SCHEMA(GROUP, X) \
	GROUP(Config, \
		X(IdtAtomic<int>, autoClean, 0) \
	) \
	GROUP(PlugIn, \
		GROUP(PPTHelper, \
			X(IdtAtomic<bool>, autoTakeOver, false) \
			X(IdtAtomic<bool>, autoTakeOverExpand, false) \
		) \
	)

export namespace Inkeys
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
	#define INKEYS_CONFIG_DECLARE_VALUE(valueType, valueName, defaultValue) valueType valueName{ defaultValue };
		INKEYS_CONFIG_SCHEMA(INKEYS_CONFIG_DECLARE_GROUP, INKEYS_CONFIG_DECLARE_VALUE)
		#undef INKEYS_CONFIG_DECLARE_GROUP
		#undef INKEYS_CONFIG_DECLARE_VALUE

	public:
		bool ReadAll();
		bool ReadMini(std::initializer_list<std::string_view> paths);
		bool Write();
		std::wstring GetFilePath() const;
		void ResetToDefaults();

	private:
		Json::Value loadedDocument = Json::Value(Json::objectValue);
		bool hasLoadedDocument = false;

		template <typename HandlerT>
		void TraverseSchema(HandlerT&& handler)
		{
			auto& group = *this;
			Inkeys::ConfigDetail::SchemaWalker<HandlerT> walker(handler);

		#define INKEYS_CONFIG_TRAVERSE_GROUP(groupName, ...) walker.BeginGroup(#groupName, group.groupName, [&](auto& group) { __VA_ARGS__ });
		#define INKEYS_CONFIG_TRAVERSE_VALUE(valueType, valueName, defaultValue) walker.Value(#valueName, group.valueName, valueType{ defaultValue });
			INKEYS_CONFIG_SCHEMA(INKEYS_CONFIG_TRAVERSE_GROUP, INKEYS_CONFIG_TRAVERSE_VALUE)
			#undef INKEYS_CONFIG_TRAVERSE_GROUP
			#undef INKEYS_CONFIG_TRAVERSE_VALUE
		}

		bool ReadImpl(const std::vector<std::string>& paths);
		void ApplyDefaults(const std::vector<std::string>& paths);
		void ApplyDocument(const Json::Value& root, const std::vector<std::string>& paths);
		void OverlayDocument(Json::Value& root);
		void WriteInfoBlock(Json::Value& root) const;
		bool LoadDocumentOnly(Json::Value& outRoot) const;
		static bool WriteDocumentToFile(const std::wstring& filePath, const Json::Value& root);
	};

	export inline Config config{};
}

#undef INKEYS_CONFIG_SCHEMA