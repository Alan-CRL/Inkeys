#pragma once

#include "Draw3.DevicePerformanceModel.h"

#include <json/json.h>

#include <cmath>
#include <limits>
#include <memory>
#include <sstream>
#include <string>

namespace Inkeys::Drawing::Draw3::DevicePerformance
{
	// device-performance.json 的序列化与解析（jsoncpp）。纯内存字符串进出，文件 I/O 由账本负责，
	// 便于单测覆盖往返、坏文件与单桶非法字段。
	inline constexpr const char* kFileSchema = "inkeys.device-performance";
	inline constexpr int kFileVersion = 4; // 4：渲染帧按 DWM 合成周期计数、去掉限制间隔，不兼容 1–3
	inline constexpr std::array<const char*, kCounterCount> kCounterNames =
		{ "all", "writing", "highlighter", "eraser", "laser", "shape", "pan" };

	namespace Detail
	{
		// 计数器按字段顺序写成数组：[frames, costMs, timedFrames, displayedFrames, intervalMs,
		// missed, computeFrames, computeIntervalMs]。
		inline Json::Value CounterToJson(const Counter& counter)
		{
			Json::Value value(Json::arrayValue);
			value.append(Json::Value(static_cast<Json::UInt64>(counter.frames)));
			value.append(Json::Value(counter.costMs));
			value.append(Json::Value(static_cast<Json::UInt64>(counter.timedFrames)));
			value.append(Json::Value(static_cast<Json::UInt64>(counter.displayedFrames)));
			value.append(Json::Value(counter.intervalMs));
			value.append(Json::Value(static_cast<Json::UInt64>(counter.missed)));
			value.append(Json::Value(static_cast<Json::UInt64>(counter.computeFrames)));
			value.append(Json::Value(counter.computeIntervalMs));
			return value;
		}

		inline bool CounterFromJson(const Json::Value& value, Counter& out)
		{
			if (!value.isArray() || value.size() != 8) return false;
			for (const Json::ArrayIndex i : { 0u, 2u, 3u, 5u, 6u })
				if (!value[i].isUInt64()) return false;
			for (const Json::ArrayIndex i : { 1u, 4u, 7u })
				if (!value[i].isNumeric()) return false;
			Counter counter;
			counter.frames = value[0u].asUInt64();
			counter.costMs = value[1u].asDouble();
			counter.timedFrames = value[2u].asUInt64();
			counter.displayedFrames = value[3u].asUInt64();
			counter.intervalMs = value[4u].asDouble();
			counter.missed = value[5u].asUInt64();
			counter.computeFrames = value[6u].asUInt64();
			counter.computeIntervalMs = value[7u].asDouble();
			for (const double ms : { counter.costMs, counter.intervalMs, counter.computeIntervalMs })
				if (!std::isfinite(ms) || ms < 0.0) return false;
			if (counter.timedFrames > counter.frames || counter.displayedFrames > counter.timedFrames ||
				counter.missed > counter.timedFrames) return false;
			if (counter.frames == 0 && counter.costMs != 0.0) return false;
			if (counter.timedFrames == 0 && counter.intervalMs != 0.0) return false;
			if (counter.computeFrames == 0 && counter.computeIntervalMs != 0.0) return false;
			out = counter;
			return true;
		}

		inline Json::Value DisplayToJson(const DisplayRange& display)
		{
			Json::Value value(Json::arrayValue);
			value.append(Json::Value(static_cast<int>(display.minHz)));
			value.append(Json::Value(static_cast<int>(display.maxHz)));
			return value;
		}

		// [0, 0] 表示未测到；否则必须是合理范围内的 [min, max]。
		inline bool DisplayFromJson(const Json::Value& value, DisplayRange& out)
		{
			if (!value.isArray() || value.size() != 2 || !value[0u].isInt() || !value[1u].isInt()) return false;
			const int minHz = value[0u].asInt(), maxHz = value[1u].asInt();
			DisplayRange display;
			if (minHz == 0 && maxHz == 0)
			{
				out = display;
				return true;
			}
			if (minHz < kMinDisplayHz || maxHz > kMaxDisplayHz || minHz > maxHz) return false;
			display.minHz = static_cast<std::uint16_t>(minHz);
			display.maxHz = static_cast<std::uint16_t>(maxHz);
			out = display;
			return true;
		}

		inline Device DeviceFromName(const std::string& name) noexcept
		{
			if (name == "hardware") return Device::Hardware;
			if (name == "warp") return Device::Warp;
			return Device::Unknown;
		}

		inline Presentation PresentationFromName(const std::string& name) noexcept
		{
			if (name == "dcomp") return Presentation::DirectComposition;
			if (name == "ulw") return Presentation::Ulw;
			return Presentation::Unknown;
		}

		// 开关字段：0/1，-1 表示不适用。
		inline Json::Value FlagToJson(std::uint8_t value)
		{
			return value == kNotApplicable ? Json::Value(-1) : Json::Value(static_cast<int>(value));
		}

		inline bool FlagFromJson(const Json::Value& value, std::uint8_t& out)
		{
			if (!value.isInt()) return false;
			const int flag = value.asInt();
			if (flag == -1) out = kNotApplicable;
			else if (flag == 0 || flag == 1) out = static_cast<std::uint8_t>(flag);
			else return false;
			return true;
		}
	}

	inline std::string SerializeStore(const Store& store)
	{
		Json::Value root(Json::objectValue);
		root["schema"] = kFileSchema;
		root["version"] = kFileVersion;
		Json::Value records(Json::arrayValue);
		for (const SignatureRecord& record : store.records)
		{
			const Signature& signature = record.signature;
			Json::Value item(Json::objectValue);
			item["key"] = SignatureKey(signature);
			item["frameRate"] = static_cast<int>(signature.frameRate);
			item["device"] = DeviceName(signature.device);
			item["presentation"] = PresentationName(signature.presentation);
			item["waitable"] = Detail::FlagToJson(signature.waitable);
			item["ulwAsyncReadback"] = Detail::FlagToJson(signature.ulwAsyncReadback);
			item["ulwCompositionAlign"] = Detail::FlagToJson(signature.ulwCompositionAlign);
			item["zhaoxin"] = static_cast<int>(signature.zhaoxinCompatibility);
			item["lastUsed"] = static_cast<Json::Int64>(record.lastUsedUnixSeconds);
			Json::Value days(Json::arrayValue);
			for (const DayBucket& day : record.days)
			{
				Json::Value bucket(Json::objectValue);
				bucket["date"] = static_cast<int>(day.date);
				bucket["updated"] = static_cast<Json::Int64>(day.updatedUnixSeconds);
				bucket["display"] = Detail::DisplayToJson(day.display);
				for (std::size_t i = 0; i < kCounterCount; ++i)
					bucket[kCounterNames[i]] = Detail::CounterToJson(day.counters[i]);
				days.append(bucket);
			}
			item["days"] = days;
			records.append(item);
		}
		root["signatures"] = records;
		Json::StreamWriterBuilder builder;
		builder["indentation"] = "  ";
		return Json::writeString(builder, root);
	}

	enum class ParseResult : std::uint8_t { Ok, Malformed, WrongSchema };

	// 整体结构不对（不是 JSON、schema/version 不符、顶层字段类型错误）返回失败，由调用方改名坏文件；
	// 单个签名或日桶字段非法只丢弃该项，droppedItems 计数。
	inline ParseResult ParseStore(const std::string& text, Store& out, std::size_t* droppedItems = nullptr)
	{
		std::size_t dropped = 0;
		Json::Value root;
		Json::CharReaderBuilder builder;
		builder["collectComments"] = false;
		std::string errors;
		const std::unique_ptr<Json::CharReader> reader(builder.newCharReader());
		if (!reader->parse(text.data(), text.data() + text.size(), &root, &errors) || !root.isObject())
			return ParseResult::Malformed;
		if (!root["schema"].isString() || root["schema"].asString() != kFileSchema ||
			!root["version"].isInt() || root["version"].asInt() != kFileVersion)
			return ParseResult::WrongSchema;
		const Json::Value& records = root["signatures"];
		if (!records.isArray()) return ParseResult::Malformed;

		Store store;
		for (const Json::Value& item : records)
		{
			if (!item.isObject() || !item["frameRate"].isInt() || !item["device"].isString() ||
				!item["presentation"].isString() || !item["zhaoxin"].isInt() || !item["lastUsed"].isInt64() ||
				!item["days"].isArray())
			{
				++dropped;
				continue;
			}
			const int frameRate = item["frameRate"].asInt();
			const int zhaoxin = item["zhaoxin"].asInt();
			Signature signature;
			if (frameRate <= 0 || frameRate > 1000 || (zhaoxin != 0 && zhaoxin != 1) ||
				!Detail::FlagFromJson(item["waitable"], signature.waitable) ||
				!Detail::FlagFromJson(item["ulwAsyncReadback"], signature.ulwAsyncReadback) ||
				!Detail::FlagFromJson(item["ulwCompositionAlign"], signature.ulwCompositionAlign))
			{
				++dropped;
				continue;
			}
			signature.frameRate = static_cast<std::uint16_t>(frameRate);
			signature.device = Detail::DeviceFromName(item["device"].asString());
			signature.presentation = Detail::PresentationFromName(item["presentation"].asString());
			signature.zhaoxinCompatibility = static_cast<std::uint8_t>(zhaoxin);

			SignatureRecord record;
			record.signature = NormalizeSignature(signature);
			record.lastUsedUnixSeconds = item["lastUsed"].asInt64();
			for (const Json::Value& bucket : item["days"])
			{
				DayBucket day;
				bool valid = bucket.isObject() && bucket["date"].isInt() && bucket["updated"].isInt64();
				if (valid)
				{
					day.date = bucket["date"].asInt();
					day.updatedUnixSeconds = bucket["updated"].asInt64();
					valid = IsValidDate(day.date) && Detail::DisplayFromJson(bucket["display"], day.display);
				}
				for (std::size_t i = 0; valid && i < kCounterCount; ++i)
					valid = Detail::CounterFromJson(bucket[kCounterNames[i]], day.counters[i]);
				// 维度计数不能超过总体（总体每帧只计一次）。
				for (std::size_t i = 1; valid && i < kCounterCount; ++i)
					valid = day.counters[i].frames <= day.counters[0].frames &&
						day.counters[i].computeFrames <= day.counters[0].computeFrames;
				if (!valid || day.counters[0].frames == 0)
				{
					++dropped;
					continue;
				}
				MergeDay(record, day);
			}
			if (record.days.empty()) continue;
			// 文件里重复的签名按相加合并。
			Store single;
			single.records.push_back(std::move(record));
			MergeStore(store, single);
		}
		out = std::move(store);
		if (droppedItems) *droppedItems = dropped;
		return ParseResult::Ok;
	}
}
