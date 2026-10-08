#pragma once

#include "PenWidthPolicy.hpp"
#include <json/json.h>
#include <array>

namespace Inkeys::Business::PenWidth
{
    inline constexpr int SchemaVersion = 1;
    inline constexpr const char* SlotNames[] = { "Brush1", "Highlighter1", "Laser" };
    struct Preferences
    {
        std::array<Selection, 3> selections{};
        std::array<bool, 3> valid{};
        bool memoryWidth = true;
        bool memoryColor = false;
        bool autoDefaultWidth = true;
        bool unknownSchema = false;
        bool migrated = false;
        Json::Value original;
        Json::Value migration;
    };
    inline bool DecodeSelection(const Json::Value& value, Selection& selection)
    {
        if (!value.isObject() || !value["Source"].isString()) return false;
        if (value["Source"].asString() == "Preset")
        {
            if (!value["Preset"].isInt()) return false;
            const auto preset = static_cast<Preset>(value["Preset"].asInt());
            if (!IsValidPreset(preset)) return false;
            selection = { Source::Preset, preset, 3.0 };
            return true;
        }
        if (value["Source"].asString() != "ExplicitDip" || !value["Dip"].isNumeric()) return false;
        const double dip = value["Dip"].asDouble();
        if (!IsValidDip(dip)) return false;
        selection = { Source::ExplicitDip, Preset::Medium, dip };
        return true;
    }
    inline Json::Value EncodeSelection(Selection selection)
    {
        Json::Value result(Json::objectValue);
        if (!IsValidSelection(selection)) selection = {};
        result["Source"] = selection.source == Source::Preset ? "Preset" : "ExplicitDip";
        if (selection.source == Source::Preset) result["Preset"] = static_cast<int>(selection.preset);
        else result["Dip"] = selection.explicitDip;
        return result;
    }
    inline void DecodeFlags(const Json::Value& value, Preferences& result)
    {
        if (!value.isObject()) return;
        if (value["MemoryWidth"].isBool()) result.memoryWidth = value["MemoryWidth"].asBool();
        if (value["MemoryColor"].isBool()) result.memoryColor = value["MemoryColor"].asBool();
        if (value["AutoDefaultWidth"].isBool()) result.autoDefaultWidth = value["AutoDefaultWidth"].asBool();
    }
    inline Preferences DecodePreferences(const Json::Value& root, bool memory, const Context& context)
    {
        Preferences result;
        if (!root.isObject()) return result;
        if (root.isMember("PenWidth"))
        {
            const auto& schema = root["PenWidth"];
            result.original = schema;
            // 带标记的未知或损坏对象不按旧像素解释，保存时也不降级覆盖。
            if (!schema.isObject() || !schema["Version"].isInt() || schema["Version"].asInt() != SchemaVersion
                || !schema["Unit"].isString() || schema["Unit"].asString() != "DIP")
            {
                result.unknownSchema = true;
                return result;
            }
            DecodeFlags(schema, result);
            const auto& slots = schema[memory ? "Selections" : "Startup"];
            if (slots.isObject())
                for (int i = 0; i < 3; ++i) result.valid[i] = DecodeSelection(slots[SlotNames[i]], result.selections[i]);
            result.migration = schema["Migration"];
            return result;
        }
        const auto& legacy = root[memory ? "Draw" : "Preset"];
        if (!legacy.isObject()) return result;
        DecodeFlags(legacy, result);
        // 旧普通笔字段是 canvas px；缺少值关联的历史 DPI，按当前目标估计。
        for (int i = 0; i < 3; ++i)
        {
            if (!memory && i == 2) continue; // 旧启动配置没有已确认的激光宽度字段。
            if (memory && !legacy[SlotNames[i]].isObject()) continue;
            const auto& number = memory ? legacy[SlotNames[i]]["Width"]
                : legacy[i == 0 ? "DefaultBrush1Width" : "DefaultHighlighter1Width"];
            if (!number.isNumeric()) continue;
            const double raw = number.asDouble();
            const double dip = i == 2 ? raw : raw / SystemScale(context);
            if (!std::isfinite(raw) || raw <= 0.0 || !IsValidDip(dip)) continue;
            result.selections[i] = { Source::ExplicitDip, Preset::Medium, dip };
            result.valid[i] = true;
            result.migrated = true;
        }
        if (result.migrated || !memory)
        {
            result.migration["Legacy"] = legacy;
            result.migration["EstimatedSourceDpi"] = 96.0 * SystemScale(context);
            result.migration["Estimated"] = true;
            result.migration["Note"] = "Legacy ordinary widths were canvas pixels; source DPI unavailable.";
        }
        return result;
    }
    inline Json::Value EncodePreferences(const Preferences& preferences, bool memory)
    {
        if (preferences.unknownSchema) return preferences.original;
        Json::Value schema = preferences.original.isObject() ? preferences.original : Json::Value(Json::objectValue);
        schema["Version"] = SchemaVersion;
        schema["Unit"] = "DIP";
        schema["MemoryWidth"] = preferences.memoryWidth;
        schema["MemoryColor"] = preferences.memoryColor;
        schema["AutoDefaultWidth"] = preferences.autoDefaultWidth;
        auto& slots = schema[memory ? "Selections" : "Startup"];
        if (!slots.isObject()) slots = Json::Value(Json::objectValue);
        for (int i = 0; i < 3; ++i)
        {
            if (preferences.valid[i]) slots[SlotNames[i]] = EncodeSelection(preferences.selections[i]);
            else slots.removeMember(SlotNames[i]);
        }
        if (!preferences.migration.isNull()) schema["Migration"] = preferences.migration;
        return schema;
    }
    inline Selection StartupSelection(const Preferences& startup, const Preferences& memory, int slot) noexcept
    {
        if (startup.memoryWidth && memory.valid[slot]) return memory.selections[slot];
        if (!startup.autoDefaultWidth && startup.valid[slot]) return startup.selections[slot];
        return {};
    }
}
