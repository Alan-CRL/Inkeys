module;

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include "Draw3.DiagnosticOutput.h"
#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <climits>
#include <cstdio>
#include <cstdarg>
#include <cstring>
#include <d3d11.h>
#include <dwmapi.h>
#include <dxgi1_2.h>
#include <iomanip>
#include <initializer_list>
#include <sstream>
#include <iostream>
#include <limits>
#include <mutex>
#include <thread>
#include <windows.h>

module Inkeys.Drawing.Draw3.diagnostics;

namespace Inkeys::Drawing::Draw3
{
	namespace
	{
		std::atomic<bool> startupEnvironmentDiagnosticsEnabled = false;
		constexpr size_t kCursorDiagnosticCapacity = DiagnosticOutput::RecordCapacity;
		std::atomic<bool> cursorDiagnosticsEnabled = false;
		std::atomic<uint64_t> cursorDiagnosticSequence = 0;

		void WriteFastConsoleLine(const char* text, DWORD length)
		{
			DiagnosticOutput::Write(DiagnosticOutput::Stream::Output, text, length);
		}

#if defined(DRAW3_RTS_DIAGNOSTICS)
		constexpr size_t kRtsTraceContactCapacity = 32;
		constexpr size_t kRtsTraceEventCapacity = 10;
		constexpr size_t kRtsTraceMoveEventLimit = 6;
		constexpr size_t kRtsTraceAuxiliaryEventCapacity = 64;

		struct RtsContactTraceSlot
		{
			bool occupied = false;
			bool downObserved = false;
			uint32_t tabletContextId = 0;
			uint32_t contactId = 0;
			uint32_t deviceType = 0;
			uint32_t callbackCount = 0;
			uint32_t packetsCallbackCount = 0;
			uint32_t inAirCallbackCount = 0;
			uint32_t packetSampleCount = 0;
			uint32_t inAirSampleCount = 0;
			uint32_t decodedFailureCount = 0;
			uint32_t publishSuccessCount = 0;
			uint32_t publishFailureCount = 0;
			uint32_t droppedEventCount = 0;
			size_t storedMoveEventCount = 0;
			size_t storedEventCount = 0;
			std::array<RtsCallbackTrace, kRtsTraceEventCapacity> events = {};
		};

		class RtsTraceWriteGuard
		{
		public:
			explicit RtsTraceWriteGuard(std::atomic_flag& lock) noexcept
				: lock_(lock), acquired_(!lock_.test_and_set(std::memory_order_acquire))
			{
			}

			~RtsTraceWriteGuard()
			{
				if (acquired_) lock_.clear(std::memory_order_release);
			}

			explicit operator bool() const noexcept { return acquired_; }

		private:
			std::atomic_flag& lock_;
			bool acquired_ = false;
		};

		std::atomic<bool> rtsTraceEnabled = false;
		std::atomic_flag rtsTraceWriteLock = ATOMIC_FLAG_INIT;
		std::array<RtsContactTraceSlot, kRtsTraceContactCapacity> rtsContactSlots = {};
		std::array<RtsContactTraceSlot, kRtsTraceContactCapacity> rtsCompletedContactSlots = {};
		std::array<RtsCallbackTrace, kRtsTraceAuxiliaryEventCapacity> rtsAuxiliaryEvents = {};
		uint32_t rtsCompletedContactCount = 0;
		uint32_t rtsAuxiliaryEventCount = 0;
		std::atomic<uint32_t> rtsContactSlotDroppedCount = 0;
		std::atomic<uint32_t> rtsCallbackContentionDroppedCount = 0;

		bool TraceEventEquals(const char* eventName, const char* expected) noexcept
		{
			return eventName && std::strcmp(eventName, expected) == 0;
		}

		bool IsMoveTraceEvent(const char* eventName) noexcept
		{
			return TraceEventEquals(eventName, "Packets") ||
				TraceEventEquals(eventName, "InAirPackets");
		}

		bool IsContactTraceEvent(const char* eventName) noexcept
		{
			return TraceEventEquals(eventName, "StylusDown") ||
				IsMoveTraceEvent(eventName) || TraceEventEquals(eventName, "StylusUp");
		}

		void AppendTraceText(char* buffer, size_t capacity, size_t& length,
			const char* format, ...) noexcept
		{
			if (!buffer || capacity == 0 || length >= capacity - 1) return;
			va_list arguments;
			va_start(arguments, format);
			const int written = std::vsnprintf(
				buffer + length, capacity - length, format, arguments);
			va_end(arguments);
			if (written <= 0) return;
			length += (std::min)(static_cast<size_t>(written), capacity - length - 1);
		}

		void WriteRtsTraceLine(const char* text, size_t length) noexcept
		{
			if (!text || length == 0) return;
			DiagnosticOutput::Write(DiagnosticOutput::Stream::Output, text, static_cast<std::streamsize>(length));
			DiagnosticOutput::Wake();
		}

		RtsContactTraceSlot* FindRtsContactSlot(
			uint32_t tabletContextId, uint32_t contactId) noexcept
		{
			for (RtsContactTraceSlot& slot : rtsContactSlots)
			{
				if (slot.occupied && slot.tabletContextId == tabletContextId &&
					slot.contactId == contactId) return &slot;
			}
			return nullptr;
		}

		RtsContactTraceSlot* AcquireRtsContactSlot(
			uint32_t tabletContextId, uint32_t contactId) noexcept
		{
			if (RtsContactTraceSlot* existing = FindRtsContactSlot(
				tabletContextId, contactId)) return existing;
			for (RtsContactTraceSlot& slot : rtsContactSlots)
			{
				if (slot.occupied) continue;
				slot = {};
				slot.occupied = true;
				slot.tabletContextId = tabletContextId;
				slot.contactId = contactId;
				return &slot;
			}
			rtsContactSlotDroppedCount.fetch_add(1, std::memory_order_relaxed);
			return nullptr;
		}

		void PrintRtsCallbackLine(const RtsCallbackTrace& callback) noexcept
		{
			char line[768] = {};
			const int length = std::snprintf(line, sizeof(line),
				"[RTS_TRACE][callback] qpc=%lld tid=%lu event=%s tcid=%u cid=%u "
				"device=%u packets=%lu properties=%lu rawKnown=%u raw=(%ld,%ld) "
				"decoded=%u pixel=(%.3f,%.3f) published=%u dataInterest=0x%08x "
				"result=0x%08lx\r\n",
				static_cast<long long>(callback.qpc),
				static_cast<unsigned long>(callback.threadId),
				callback.eventName ? callback.eventName : "unknown",
				callback.tabletContextId, callback.contactId, callback.deviceType,
				static_cast<unsigned long>(callback.packetCount),
				static_cast<unsigned long>(callback.propertyCount),
				callback.hasRawPosition ? 1u : 0u,
				static_cast<long>(callback.rawX), static_cast<long>(callback.rawY),
				callback.decoded ? 1u : 0u, callback.decodedX, callback.decodedY,
				callback.published ? 1u : 0u,
				callback.dataInterest,
				static_cast<unsigned long>(callback.result));
			if (length > 0) WriteRtsTraceLine(line,
				(std::min)(static_cast<size_t>(length), sizeof(line) - 1));
		}

		void PrintRtsContactSummary(const RtsContactTraceSlot& slot) noexcept
		{
			char line[4096] = {};
			size_t length = 0;
			AppendTraceText(line, sizeof(line), length,
				"[RTS_TRACE][contact] tcid=%u cid=%u device=%u callbacks=%u "
				"packetsCallbacks=%u inAirCallbacks=%u packetSamples=%u inAirSamples=%u "
				"decodeFailures=%u publishOk=%u publishFailed=%u stored=%zu dropped=%u "
				"slotDrops=%u sequence=",
				slot.tabletContextId, slot.contactId, slot.deviceType, slot.callbackCount,
				slot.packetsCallbackCount, slot.inAirCallbackCount, slot.packetSampleCount,
				slot.inAirSampleCount, slot.decodedFailureCount, slot.publishSuccessCount,
				slot.publishFailureCount, slot.storedEventCount, slot.droppedEventCount,
				rtsContactSlotDroppedCount.load(std::memory_order_relaxed));
			for (size_t index = 0; index < slot.storedEventCount; ++index)
			{
				const RtsCallbackTrace& event = slot.events[index];
				AppendTraceText(line, sizeof(line), length,
					"%s%s{qpc=%lld,tid=%lu,pk=%lu,prop=%lu,raw=%u:(%ld,%ld),"
					"decoded=%u:(%.3f,%.3f),published=%u,dataInterest=0x%08x,"
					"result=0x%08lx}",
					index == 0 ? "" : " -> ", event.eventName ? event.eventName : "unknown",
					static_cast<long long>(event.qpc),
					static_cast<unsigned long>(event.threadId),
					static_cast<unsigned long>(event.packetCount),
					static_cast<unsigned long>(event.propertyCount),
					event.hasRawPosition ? 1u : 0u,
					static_cast<long>(event.rawX), static_cast<long>(event.rawY),
					event.decoded ? 1u : 0u, event.decodedX, event.decodedY,
					event.published ? 1u : 0u,
					event.dataInterest,
					static_cast<unsigned long>(event.result));
			}
			AppendTraceText(line, sizeof(line), length, "\r\n");
			WriteRtsTraceLine(line, length);
		}
#endif

		const char* DxgiAlphaModeName(DXGI_ALPHA_MODE mode)
		{
			switch (mode)
			{
			case DXGI_ALPHA_MODE_UNSPECIFIED: return "UNSPECIFIED";
			case DXGI_ALPHA_MODE_PREMULTIPLIED: return "PREMULTIPLIED";
			case DXGI_ALPHA_MODE_STRAIGHT: return "STRAIGHT";
			case DXGI_ALPHA_MODE_IGNORE: return "IGNORE";
			default: return "UNKNOWN";
			}
		}

		const char* DxgiSwapEffectName(DXGI_SWAP_EFFECT effect)
		{
			switch (effect)
			{
			case DXGI_SWAP_EFFECT_DISCARD: return "DISCARD";
			case DXGI_SWAP_EFFECT_SEQUENTIAL: return "SEQUENTIAL";
			case DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL: return "FLIP_SEQUENTIAL";
			case DXGI_SWAP_EFFECT_FLIP_DISCARD: return "FLIP_DISCARD";
			default: return "UNKNOWN";
			}
		}

		const char* DxgiFormatName(DXGI_FORMAT format)
		{
			switch (format)
			{
			case DXGI_FORMAT_B8G8R8A8_UNORM: return "B8G8R8A8_UNORM";
			case DXGI_FORMAT_R8G8B8A8_UNORM: return "R8G8B8A8_UNORM";
			default: return "OTHER";
			}
		}
	}

	namespace
	{
		struct DiagnosticConstant { UINT value; const char* name; };
		std::string DiagnosticHex(UINT value)
		{
			char text[11]{};
			std::snprintf(text, sizeof(text), "0x%08X", value);
			return text;
		}
		std::string DiagnosticEnum(UINT value, const char* type,
			std::initializer_list<DiagnosticConstant> names, bool hex = false)
		{
			std::string name = std::string("未知 ") + type;
			for (const auto& item : names) if (item.value == value) { name = item.name; break; }
			return name + " (" + (hex ? DiagnosticHex(value) : std::to_string(value)) + ")";
		}
	}

	std::string FormatDiagnosticFeatureLevel(D3D_FEATURE_LEVEL value)
	{
		return DiagnosticEnum(static_cast<UINT>(value), "D3D_FEATURE_LEVEL", {
			{D3D_FEATURE_LEVEL_1_0_CORE,"D3D_FEATURE_LEVEL_1_0_CORE"},
			{D3D_FEATURE_LEVEL_9_1,"D3D_FEATURE_LEVEL_9_1"},{D3D_FEATURE_LEVEL_9_2,"D3D_FEATURE_LEVEL_9_2"},
			{D3D_FEATURE_LEVEL_9_3,"D3D_FEATURE_LEVEL_9_3"},{D3D_FEATURE_LEVEL_10_0,"D3D_FEATURE_LEVEL_10_0"},
			{D3D_FEATURE_LEVEL_10_1,"D3D_FEATURE_LEVEL_10_1"},{D3D_FEATURE_LEVEL_11_0,"D3D_FEATURE_LEVEL_11_0"},
			{D3D_FEATURE_LEVEL_11_1,"D3D_FEATURE_LEVEL_11_1"},{D3D_FEATURE_LEVEL_12_0,"D3D_FEATURE_LEVEL_12_0"},
			{D3D_FEATURE_LEVEL_12_1,"D3D_FEATURE_LEVEL_12_1"},{D3D_FEATURE_LEVEL_12_2,"D3D_FEATURE_LEVEL_12_2"}}, true);
	}

	std::string FormatDiagnosticFormat(DXGI_FORMAT value)
	{
		return DiagnosticEnum(static_cast<UINT>(value), "DXGI_FORMAT", {
			{DXGI_FORMAT_UNKNOWN,"DXGI_FORMAT_UNKNOWN"},{DXGI_FORMAT_B8G8R8A8_UNORM,"DXGI_FORMAT_B8G8R8A8_UNORM"},
			{DXGI_FORMAT_R8G8B8A8_UNORM,"DXGI_FORMAT_R8G8B8A8_UNORM"},{DXGI_FORMAT_R10G10B10A2_UNORM,"DXGI_FORMAT_R10G10B10A2_UNORM"},
			{DXGI_FORMAT_R16G16B16A16_FLOAT,"DXGI_FORMAT_R16G16B16A16_FLOAT"},{DXGI_FORMAT_B8G8R8X8_UNORM,"DXGI_FORMAT_B8G8R8X8_UNORM"},
			{DXGI_FORMAT_R8G8B8A8_UNORM_SRGB,"DXGI_FORMAT_R8G8B8A8_UNORM_SRGB"},{DXGI_FORMAT_B8G8R8A8_UNORM_SRGB,"DXGI_FORMAT_B8G8R8A8_UNORM_SRGB"},
			{DXGI_FORMAT_B8G8R8X8_UNORM_SRGB,"DXGI_FORMAT_B8G8R8X8_UNORM_SRGB"}});
	}

	std::string FormatDiagnosticSwapEffect(DXGI_SWAP_EFFECT value)
	{
		return DiagnosticEnum(static_cast<UINT>(value), "DXGI_SWAP_EFFECT", {
			{DXGI_SWAP_EFFECT_DISCARD,"DXGI_SWAP_EFFECT_DISCARD"},{DXGI_SWAP_EFFECT_SEQUENTIAL,"DXGI_SWAP_EFFECT_SEQUENTIAL"},
			{DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL,"DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL"},{DXGI_SWAP_EFFECT_FLIP_DISCARD,"DXGI_SWAP_EFFECT_FLIP_DISCARD"}});
	}

	std::string FormatDiagnosticAlphaMode(DXGI_ALPHA_MODE value)
	{
		return DiagnosticEnum(static_cast<UINT>(value), "DXGI_ALPHA_MODE", {
			{DXGI_ALPHA_MODE_UNSPECIFIED,"DXGI_ALPHA_MODE_UNSPECIFIED"},{DXGI_ALPHA_MODE_PREMULTIPLIED,"DXGI_ALPHA_MODE_PREMULTIPLIED"},
			{DXGI_ALPHA_MODE_STRAIGHT,"DXGI_ALPHA_MODE_STRAIGHT"},{DXGI_ALPHA_MODE_IGNORE,"DXGI_ALPHA_MODE_IGNORE"},
			{static_cast<UINT>(DXGI_ALPHA_MODE_FORCE_DWORD),"DXGI_ALPHA_MODE_FORCE_DWORD"}});
	}

	std::string FormatDiagnosticScaling(DXGI_SCALING value)
	{
		return DiagnosticEnum(static_cast<UINT>(value), "DXGI_SCALING", {
			{DXGI_SCALING_STRETCH,"DXGI_SCALING_STRETCH"},{DXGI_SCALING_NONE,"DXGI_SCALING_NONE"},
			{DXGI_SCALING_ASPECT_RATIO_STRETCH,"DXGI_SCALING_ASPECT_RATIO_STRETCH"}});
	}

	std::string FormatDiagnosticSwapChainFlags(UINT flags)
	{
		if (flags == 0) return "无标志 (0x00000000)";
		constexpr DiagnosticConstant names[]{
			{DXGI_SWAP_CHAIN_FLAG_NONPREROTATED,"DXGI_SWAP_CHAIN_FLAG_NONPREROTATED"},
			{DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH,"DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH"},
			{DXGI_SWAP_CHAIN_FLAG_GDI_COMPATIBLE,"DXGI_SWAP_CHAIN_FLAG_GDI_COMPATIBLE"},
			{DXGI_SWAP_CHAIN_FLAG_RESTRICTED_CONTENT,"DXGI_SWAP_CHAIN_FLAG_RESTRICTED_CONTENT"},
			{DXGI_SWAP_CHAIN_FLAG_RESTRICT_SHARED_RESOURCE_DRIVER,"DXGI_SWAP_CHAIN_FLAG_RESTRICT_SHARED_RESOURCE_DRIVER"},
			{DXGI_SWAP_CHAIN_FLAG_DISPLAY_ONLY,"DXGI_SWAP_CHAIN_FLAG_DISPLAY_ONLY"},
			{DXGI_SWAP_CHAIN_FLAG_FRAME_LATENCY_WAITABLE_OBJECT,"DXGI_SWAP_CHAIN_FLAG_FRAME_LATENCY_WAITABLE_OBJECT"},
			{DXGI_SWAP_CHAIN_FLAG_FOREGROUND_LAYER,"DXGI_SWAP_CHAIN_FLAG_FOREGROUND_LAYER"},
			{DXGI_SWAP_CHAIN_FLAG_FULLSCREEN_VIDEO,"DXGI_SWAP_CHAIN_FLAG_FULLSCREEN_VIDEO"},
			{DXGI_SWAP_CHAIN_FLAG_YUV_VIDEO,"DXGI_SWAP_CHAIN_FLAG_YUV_VIDEO"},
			{DXGI_SWAP_CHAIN_FLAG_HW_PROTECTED,"DXGI_SWAP_CHAIN_FLAG_HW_PROTECTED"},
			{DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING,"DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING"},
			{DXGI_SWAP_CHAIN_FLAG_RESTRICTED_TO_ALL_HOLOGRAPHIC_DISPLAYS,"DXGI_SWAP_CHAIN_FLAG_RESTRICTED_TO_ALL_HOLOGRAPHIC_DISPLAYS"}};
		UINT unknown = flags;
		std::string text;
		for (const auto& item : names) if ((flags & item.value) != 0)
		{
			if (!text.empty()) text += " | ";
			text += item.name; unknown &= ~item.value;
		}
		// 保留未来 SDK/驱动返回的未知位，不把未知值误写成已知常量或零。
		if (unknown != 0) { if (!text.empty()) text += " | "; text += "未知位 " + DiagnosticHex(unknown); }
		return text + " (" + DiagnosticHex(flags) + ")";
	}

	std::string FormatDiagnosticSampleDesc(DXGI_SAMPLE_DESC value)
	{
		return "Count=" + std::to_string(value.Count) + ", Quality=" + std::to_string(value.Quality) +
			(value.Count == 1 && value.Quality == 0 ? "（无 MSAA）" : "");
	}

	std::string FormatDiagnosticHResult(HRESULT value)
	{
		const char* name = "未知 HRESULT";
		switch (value)
		{
		case S_OK: name="S_OK"; break; case S_FALSE: name="S_FALSE"; break;
		case E_FAIL: name="E_FAIL"; break; case E_ABORT: name="E_ABORT"; break;
		case E_NOTIMPL: name="E_NOTIMPL"; break; case E_NOINTERFACE: name="E_NOINTERFACE"; break;
		case E_POINTER: name="E_POINTER"; break; case E_HANDLE: name="E_HANDLE"; break;
		case E_INVALIDARG: name="E_INVALIDARG"; break; case E_OUTOFMEMORY: name="E_OUTOFMEMORY"; break;
		case E_ACCESSDENIED: name="E_ACCESSDENIED"; break; case E_PENDING: name="E_PENDING"; break;
		case E_UNEXPECTED: name="E_UNEXPECTED"; break; case REGDB_E_CLASSNOTREG: name="REGDB_E_CLASSNOTREG"; break;
		case CO_E_NOTINITIALIZED: name="CO_E_NOTINITIALIZED"; break; case RPC_E_CHANGED_MODE: name="RPC_E_CHANGED_MODE"; break;
		case RPC_E_WRONG_THREAD: name="RPC_E_WRONG_THREAD"; break; case RPC_E_DISCONNECTED: name="RPC_E_DISCONNECTED"; break;
		case RPC_E_CALL_REJECTED: name="RPC_E_CALL_REJECTED"; break; case RPC_E_SERVERCALL_RETRYLATER: name="RPC_E_SERVERCALL_RETRYLATER"; break;
		case DXGI_STATUS_OCCLUDED: name="DXGI_STATUS_OCCLUDED"; break;
		case DXGI_ERROR_INVALID_CALL: name="DXGI_ERROR_INVALID_CALL"; break; case DXGI_ERROR_NOT_FOUND: name="DXGI_ERROR_NOT_FOUND"; break;
		case DXGI_ERROR_MORE_DATA: name="DXGI_ERROR_MORE_DATA"; break; case DXGI_ERROR_UNSUPPORTED: name="DXGI_ERROR_UNSUPPORTED"; break;
		case DXGI_ERROR_DEVICE_REMOVED: name="DXGI_ERROR_DEVICE_REMOVED"; break; case DXGI_ERROR_DEVICE_HUNG: name="DXGI_ERROR_DEVICE_HUNG"; break;
		case DXGI_ERROR_DEVICE_RESET: name="DXGI_ERROR_DEVICE_RESET"; break; case DXGI_ERROR_WAS_STILL_DRAWING: name="DXGI_ERROR_WAS_STILL_DRAWING"; break;
		case DXGI_ERROR_DRIVER_INTERNAL_ERROR: name="DXGI_ERROR_DRIVER_INTERNAL_ERROR"; break;
		case DXGI_ERROR_NOT_CURRENTLY_AVAILABLE: name="DXGI_ERROR_NOT_CURRENTLY_AVAILABLE"; break;
		}
		return std::string(name) + " (" + DiagnosticHex(static_cast<UINT>(value)) + ")";
	}

	std::string FormatDiagnosticSwapChain(const DXGI_SWAP_CHAIN_DESC1& value, bool waitable)
	{
		std::ostringstream text;
		text << "交换链尺寸：" << value.Width << " × " << value.Height << "  缓冲数：" << value.BufferCount
			<< "\n格式：" << FormatDiagnosticFormat(value.Format) << "\nSwapEffect：" << FormatDiagnosticSwapEffect(value.SwapEffect)
			<< "\nAlphaMode：" << FormatDiagnosticAlphaMode(value.AlphaMode) << "\nScaling：" << FormatDiagnosticScaling(value.Scaling)
			<< "\n采样（DXGI_SAMPLE_DESC）：" << FormatDiagnosticSampleDesc(value.SampleDesc) << "\nFlags：" << FormatDiagnosticSwapChainFlags(value.Flags)
			<< "\n帧延迟控制：" << (waitable ? "Waitable（最大排队帧数 1）" : "普通交换链") << "\n";
		return text.str();
	}

	std::string FormatDiagnosticRtsAttempt(const char* profile, const char* stage, HRESULT result)
	{
		const std::string label = profile && std::strcmp(profile,"BasicXY") == 0 ? "RTS 基础 X/Y 状态" :
			profile && std::strcmp(profile,"Extended") == 0 ? "RTS 扩展状态" : std::string("RTS ") + (profile ? profile : "未知") + " 状态";
		std::string text = label + "：" + (SUCCEEDED(result) ? "就绪" : "失败");
		if (FAILED(result)) text += std::string("（阶段：") + (stage ? stage : "未知") + "）";
		return text + "，HRESULT：" + FormatDiagnosticHResult(result) + "\n";
	}

	int RunDiagnosticFormattingProductionTest() noexcept
	{
		int failures = 0;
		const auto check = [&](bool ok, const char* label)
		{
			if (!ok) { ++failures; std::fprintf(stderr,"[Draw3DiagnosticFormatting] FAIL: %s\n",label); }
		};
		check(FormatDiagnosticFeatureLevel(D3D_FEATURE_LEVEL_11_1) == "D3D_FEATURE_LEVEL_11_1 (0x0000B100)","feature level has full SDK name and real value");
		check(FormatDiagnosticFeatureLevel(static_cast<D3D_FEATURE_LEVEL>(0x1234)) == "未知 D3D_FEATURE_LEVEL (0x00001234)","unknown feature level preserves its value");
		check(FormatDiagnosticFormat(DXGI_FORMAT_B8G8R8A8_UNORM) == "DXGI_FORMAT_B8G8R8A8_UNORM (87)","BGRA format is fully named");
		check(FormatDiagnosticFormat(DXGI_FORMAT_R8G8B8A8_UNORM) == "DXGI_FORMAT_R8G8B8A8_UNORM (28)","alternate common swapchain format is fully named");
		check(FormatDiagnosticFormat(static_cast<DXGI_FORMAT>(777)) == "未知 DXGI_FORMAT (777)","unknown DXGI format is not relabeled as known");
		check(FormatDiagnosticSwapEffect(DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL) == "DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL (3)","swap effect is fully named");
		check(FormatDiagnosticSwapEffect(static_cast<DXGI_SWAP_EFFECT>(99)) == "未知 DXGI_SWAP_EFFECT (99)","unknown swap effect preserves its value");
		check(FormatDiagnosticAlphaMode(DXGI_ALPHA_MODE_PREMULTIPLIED) == "DXGI_ALPHA_MODE_PREMULTIPLIED (1)","alpha mode is fully named");
		check(FormatDiagnosticAlphaMode(static_cast<DXGI_ALPHA_MODE>(99)) == "未知 DXGI_ALPHA_MODE (99)","unknown alpha mode preserves its value");
		check(FormatDiagnosticScaling(DXGI_SCALING_STRETCH) == "DXGI_SCALING_STRETCH (0)","scaling is fully named");
		check(FormatDiagnosticScaling(static_cast<DXGI_SCALING>(99)) == "未知 DXGI_SCALING (99)","unknown scaling preserves its value");
		check(FormatDiagnosticSwapChainFlags(0) == "无标志 (0x00000000)","zero flags do not invent an SDK NONE constant");
		check(FormatDiagnosticSwapChainFlags(DXGI_SWAP_CHAIN_FLAG_FRAME_LATENCY_WAITABLE_OBJECT) ==
			"DXGI_SWAP_CHAIN_FLAG_FRAME_LATENCY_WAITABLE_OBJECT (0x00000040)","waitable flag has its complete name");
		check(FormatDiagnosticSwapChainFlags(DXGI_SWAP_CHAIN_FLAG_GDI_COMPATIBLE | DXGI_SWAP_CHAIN_FLAG_FRAME_LATENCY_WAITABLE_OBJECT | 0x80000000u) ==
			"DXGI_SWAP_CHAIN_FLAG_GDI_COMPATIBLE | DXGI_SWAP_CHAIN_FLAG_FRAME_LATENCY_WAITABLE_OBJECT | 未知位 0x80000000 (0x80000044)","combined flags retain names, unknown bits and total value");
		check(FormatDiagnosticSampleDesc({1,0}) == "Count=1, Quality=0（无 MSAA）","single sample explicitly identifies no MSAA");
		check(FormatDiagnosticSampleDesc({4,2}) == "Count=4, Quality=2" && FormatDiagnosticSampleDesc({1,2}).find("无 MSAA") == std::string::npos,
			"sample quality is not mistaken for a sample count or silently normalized");
		check(FormatDiagnosticHResult(S_OK) == "S_OK (0x00000000)" && FormatDiagnosticHResult(E_NOINTERFACE) == "E_NOINTERFACE (0x80004002)","success and failure HRESULT share symbolic eight-digit format");
		check(FormatDiagnosticHResult(DXGI_ERROR_DEVICE_REMOVED) == "DXGI_ERROR_DEVICE_REMOVED (0x887A0005)","device errors keep their SDK symbol");
		check(FormatDiagnosticHResult(static_cast<HRESULT>(0x81234567u)) == "未知 HRESULT (0x81234567)","unmapped HRESULT keeps all original bits");
		check(FormatDiagnosticRtsAttempt("Extended","ready",S_OK) == "RTS 扩展状态：就绪，HRESULT：S_OK (0x00000000)\n","successful RTS attempt localizes its state and readiness");
		check(FormatDiagnosticRtsAttempt("BasicXY","input_configuration",E_FAIL) == "RTS 基础 X/Y 状态：失败（阶段：input_configuration），HRESULT：E_FAIL (0x80004005)\n",
			"failed RTS attempt retains its exact stage and result");
		DXGI_SWAP_CHAIN_DESC1 value{};
		value.Width=3840; value.Height=2160; value.BufferCount=2; value.Format=DXGI_FORMAT_B8G8R8A8_UNORM;
		value.SwapEffect=DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL; value.AlphaMode=DXGI_ALPHA_MODE_PREMULTIPLIED;
		value.Scaling=DXGI_SCALING_STRETCH; value.SampleDesc={1,0}; value.Flags=DXGI_SWAP_CHAIN_FLAG_FRAME_LATENCY_WAITABLE_OBJECT;
		const auto actual = FormatDiagnosticSwapChain(value,true);
		for (const char* expected : {"3840 × 2160", "缓冲数：2", "DXGI_FORMAT_B8G8R8A8_UNORM (87)",
			"DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL (3)", "DXGI_ALPHA_MODE_PREMULTIPLIED (1)", "DXGI_SCALING_STRETCH (0)",
			"Count=1, Quality=0（无 MSAA）", "DXGI_SWAP_CHAIN_FLAG_FRAME_LATENCY_WAITABLE_OBJECT (0x00000040)", "Waitable"})
			check(actual.find(expected) != std::string::npos,"combined production summary uses the real supplied description");
		value.Format=static_cast<DXGI_FORMAT>(777); value.SwapEffect=static_cast<DXGI_SWAP_EFFECT>(99);
		value.AlphaMode=static_cast<DXGI_ALPHA_MODE>(99); value.Scaling=static_cast<DXGI_SCALING>(99);
		value.SampleDesc={4,2}; value.Flags=0x80000000u;
		const auto unknown = FormatDiagnosticSwapChain(value,false);
		for (const char* expected : {"未知 DXGI_FORMAT (777)","未知 DXGI_SWAP_EFFECT (99)","未知 DXGI_ALPHA_MODE (99)",
			"未知 DXGI_SCALING (99)","Count=4, Quality=2","未知位 0x80000000 (0x80000000)","普通交换链"})
			check(unknown.find(expected) != std::string::npos,"summary retains unknown metadata instead of substituting startup defaults");
		if (!failures) std::fputs("[Draw3DiagnosticFormatting] PASS: SDK names, flags, samples, HRESULT and RTS summaries\n",stderr);
		return failures == 0 ? 0 : 1;
	}

#if defined(DRAW3_RTS_DIAGNOSTICS)
	void ConfigureRtsTrace(bool enabled) noexcept
	{
		const bool wasEnabled = rtsTraceEnabled.load(std::memory_order_relaxed);
		if (wasEnabled == enabled) return;
		if (!enabled)
		{
			rtsTraceEnabled.store(false, std::memory_order_release);
			return;
		}

		// 先拒绝新的 callback，再尝试取得无等待写入权重置固定诊断缓冲区。
		rtsTraceEnabled.store(false, std::memory_order_release);
		RtsTraceWriteGuard writeGuard(rtsTraceWriteLock);
		if (!writeGuard) return;
		for (RtsContactTraceSlot& slot : rtsContactSlots) slot = {};
		for (RtsContactTraceSlot& slot : rtsCompletedContactSlots) slot = {};
		for (RtsCallbackTrace& callback : rtsAuxiliaryEvents) callback = {};
		rtsCompletedContactCount = 0;
		rtsAuxiliaryEventCount = 0;
		rtsContactSlotDroppedCount.store(0, std::memory_order_relaxed);
		rtsCallbackContentionDroppedCount.store(0, std::memory_order_relaxed);
		rtsTraceEnabled.store(true, std::memory_order_release);
	}

	void LogRtsInitializationState(const RtsInitializationTrace& state) noexcept
	{
		if (!rtsTraceEnabled.load(std::memory_order_acquire)) return;
		char line[2048] = {};
		const int length = std::snprintf(line, sizeof(line),
			"[RTS_TRACE][init] tid=%lu windowThread=%lu hwnd=0x%llx style=0x%llx "
			"exStyle=0x%llx tabletFlags=0x%llx digitizer=0x%x maxTouches=%d "
			"dataInterest=0x%08x selectedProperties=%lu packetOrder=%s\r\n"
			"[RTS_TRACE][init-hresult] CoInitializeEx=0x%08lx CoCreate=0x%08lx "
			"put_HWND=0x%08lx SetAllTabletsMode=0x%08lx packetDesired=0x%08lx "
			"packetFallback=0x%08lx Stylus2=0x%08lx Flicks=0x%08lx "
			"Stylus3=0x%08lx MultiTouchSet=0x%08lx MultiTouchGet=0x%08lx MultiTouch=%u "
			"Marshaler=0x%08lx AddPlugin=0x%08lx Enable=0x%08lx\r\n",
			static_cast<unsigned long>(state.currentThreadId),
			static_cast<unsigned long>(state.windowThreadId),
			static_cast<unsigned long long>(state.windowHandle),
			static_cast<unsigned long long>(state.windowStyle),
			static_cast<unsigned long long>(state.windowExtendedStyle),
			static_cast<unsigned long long>(state.tabletServiceFlags), state.digitizer,
			state.maximumTouches, state.dataInterest,
			static_cast<unsigned long>(state.selectedPacketPropertyCount),
			state.selectedPacketDescription ? state.selectedPacketDescription : "none",
			static_cast<unsigned long>(state.coInitializeResult),
			static_cast<unsigned long>(state.createStylusResult),
			static_cast<unsigned long>(state.putHwndResult),
			static_cast<unsigned long>(state.setAllTabletsModeResult),
			static_cast<unsigned long>(state.desiredPacketDescriptionResult),
			static_cast<unsigned long>(state.fallbackPacketDescriptionResult),
			static_cast<unsigned long>(state.queryStylus2Result),
			static_cast<unsigned long>(state.disableFlicksResult),
			static_cast<unsigned long>(state.queryStylus3Result),
			static_cast<unsigned long>(state.enableMultiTouchResult),
			static_cast<unsigned long>(state.readMultiTouchResult),
			state.multiTouchEnabled ? 1u : 0u,
			static_cast<unsigned long>(state.marshalerResult),
			static_cast<unsigned long>(state.addPluginResult),
			static_cast<unsigned long>(state.enableStylusResult));
		if (length > 0) WriteRtsTraceLine(line,
			(std::min)(static_cast<size_t>(length), sizeof(line) - 1));
	}

	void RecordRtsCallback(const RtsCallbackTrace& callback) noexcept
	{
		if (!rtsTraceEnabled.load(std::memory_order_acquire)) return;
		// 同步回调绝不等待；并发写入时丢弃诊断样本，不影响 production 输入。
		RtsTraceWriteGuard writeGuard(rtsTraceWriteLock);
		if (!writeGuard)
		{
			rtsCallbackContentionDroppedCount.fetch_add(1, std::memory_order_relaxed);
			return;
		}
		if (!IsContactTraceEvent(callback.eventName))
		{
			const uint32_t sequence = ++rtsAuxiliaryEventCount;
			if (sequence <= kRtsTraceAuxiliaryEventCapacity)
				rtsAuxiliaryEvents[sequence - 1] = callback;
			return;
		}

		RtsContactTraceSlot* slot = FindRtsContactSlot(
			callback.tabletContextId, callback.contactId);
		if (TraceEventEquals(callback.eventName, "StylusDown"))
		{
			if (slot) *slot = {};
			slot = slot ? slot : AcquireRtsContactSlot(
				callback.tabletContextId, callback.contactId);
			if (!slot) return;
			slot->occupied = true;
			slot->downObserved = true;
			slot->tabletContextId = callback.tabletContextId;
			slot->contactId = callback.contactId;
			slot->deviceType = callback.deviceType;
		}
		else if (TraceEventEquals(callback.eventName, "InAirPackets") &&
			(!slot || !slot->downObserved))
		{
			// 悬停数据没有活动 Down 时只忽略，不创建无限期 trace 槽位。
			return;
		}
		else if (!slot)
		{
			slot = AcquireRtsContactSlot(
				callback.tabletContextId, callback.contactId);
			if (!slot) return;
			slot->deviceType = callback.deviceType;
		}

		slot->occupied = true;
		slot->callbackCount++;
		if (IsMoveTraceEvent(callback.eventName))
		{
			if (TraceEventEquals(callback.eventName, "Packets"))
			{
				++slot->packetsCallbackCount;
				slot->packetSampleCount += callback.packetCount;
			}
			else
			{
				++slot->inAirCallbackCount;
				slot->inAirSampleCount += callback.packetCount;
			}
		}
		if (!callback.decoded) ++slot->decodedFailureCount;
		if (!TraceEventEquals(callback.eventName, "InAirPackets"))
		{
			if (callback.published) ++slot->publishSuccessCount;
			else ++slot->publishFailureCount;
		}

		const bool isMove = IsMoveTraceEvent(callback.eventName);
		const bool canStore = slot->storedEventCount < kRtsTraceEventCapacity &&
			(!isMove || slot->storedMoveEventCount < kRtsTraceMoveEventLimit);
		if (canStore)
		{
			slot->events[slot->storedEventCount++] = callback;
			if (isMove) ++slot->storedMoveEventCount;
		}
		else ++slot->droppedEventCount;

		if (TraceEventEquals(callback.eventName, "StylusUp"))
		{
			const uint32_t completedIndex = rtsCompletedContactCount++;
			if (completedIndex < kRtsTraceContactCapacity)
				rtsCompletedContactSlots[completedIndex] = *slot;
			*slot = {};
		}
	}

	void FlushRtsCallbackTrace() noexcept
	{
		if (!rtsTraceEnabled.load(std::memory_order_acquire)) return;
		RtsTraceWriteGuard writeGuard(rtsTraceWriteLock);
		if (!writeGuard) return; // 停止回调后的 flush 也不等待异常并发写入。

		const uint32_t storedAuxiliaryCount = (std::min)(rtsAuxiliaryEventCount,
			static_cast<uint32_t>(kRtsTraceAuxiliaryEventCapacity));
		const uint32_t storedCompletedCount = (std::min)(rtsCompletedContactCount,
			static_cast<uint32_t>(kRtsTraceContactCapacity));
		const uint32_t contentionDrops =
			rtsCallbackContentionDroppedCount.load(std::memory_order_relaxed);
		if (storedAuxiliaryCount == 0 && storedCompletedCount == 0 && contentionDrops == 0)
			return;

		for (uint32_t index = 0; index < storedAuxiliaryCount; ++index)
			PrintRtsCallbackLine(rtsAuxiliaryEvents[index]);
		for (uint32_t index = 0; index < storedCompletedCount; ++index)
			PrintRtsContactSummary(rtsCompletedContactSlots[index]);
		for (const RtsContactTraceSlot& slot : rtsContactSlots)
		{
			if (slot.occupied) PrintRtsContactSummary(slot);
		}

		char line[256] = {};
		const int length = std::snprintf(line, sizeof(line),
			"[RTS_TRACE][buffer] auxiliary=%u/%u contacts=%u/%u slotDrops=%u contentionDrops=%u\r\n",
			storedAuxiliaryCount, rtsAuxiliaryEventCount,
			storedCompletedCount, rtsCompletedContactCount,
			rtsContactSlotDroppedCount.load(std::memory_order_relaxed), contentionDrops);
		if (length > 0) WriteRtsTraceLine(line,
			(std::min)(static_cast<size_t>(length), sizeof(line) - 1));

		for (RtsContactTraceSlot& slot : rtsContactSlots) slot = {};
		for (RtsContactTraceSlot& slot : rtsCompletedContactSlots) slot = {};
		for (RtsCallbackTrace& callback : rtsAuxiliaryEvents) callback = {};
		rtsCompletedContactCount = 0;
		rtsAuxiliaryEventCount = 0;
		rtsContactSlotDroppedCount.store(0, std::memory_order_relaxed);
		rtsCallbackContentionDroppedCount.store(0, std::memory_order_relaxed);
	}

#endif

	void SetCursorDiagnosticsEnabled(bool enabled) noexcept
	{
		cursorDiagnosticsEnabled.store(false, std::memory_order_release);
		if (!enabled) return;
		cursorDiagnosticSequence.store(0, std::memory_order_relaxed);
		cursorDiagnosticsEnabled.store(true, std::memory_order_release);
		RecordCursorDiagnostic("enabled schema=2 capacity=%zu rawMode=observe-existing-registration", kCursorDiagnosticCapacity);
		const DWORD previousError = GetLastError();
		std::array<RAWINPUTDEVICE, 32> registrations{};
		UINT count = static_cast<UINT>(registrations.size());
		const UINT result = GetRegisteredRawInputDevices(registrations.data(), &count, sizeof(RAWINPUTDEVICE));
		if (result == UINT_MAX)
			RecordCursorDiagnostic("raw-registration stage=initial known=0 error=%lu",
				static_cast<unsigned long>(GetLastError()));
		else
		{
			bool mouseRegistered = false;
			for (UINT index = 0; index < result; ++index)
			{
				const auto& registration = registrations[index];
				if (registration.usUsagePage != 0x01 || registration.usUsage != 0x02) continue;
				mouseRegistered = true;
				RecordCursorDiagnostic("raw-registration stage=initial known=1 active=1 hwnd=%p flags=0x%lx",
					static_cast<void*>(registration.hwndTarget), static_cast<unsigned long>(registration.dwFlags));
			}
			if (!mouseRegistered) RecordCursorDiagnostic("raw-registration stage=initial known=1 active=0");
		}
		SetLastError(previousError);
	}

	bool CursorDiagnosticsEnabled() noexcept
	{
		return cursorDiagnosticsEnabled.load(std::memory_order_acquire);
	}

	void RecordCursorRawInput(LPARAM rawInputHandle) noexcept
	{
		if (!CursorDiagnosticsEnabled()) return;
		const DWORD previousError = GetLastError();
		RAWINPUTHEADER header{};
		UINT size = sizeof(header);
		const HRAWINPUT handle = reinterpret_cast<HRAWINPUT>(rawInputHandle);
		if (GetRawInputData(handle, RID_HEADER, &header, &size, sizeof(header)) == UINT_MAX)
			RecordCursorDiagnostic("raw-read stage=header ok=0 error=%lu", static_cast<unsigned long>(GetLastError()));
		else if (header.dwType == RIM_TYPEMOUSE)
		{
			RAWINPUT raw{};
			size = sizeof(raw);
			const UINT read = GetRawInputData(handle, RID_INPUT, &raw, &size, sizeof(header));
			if (read == UINT_MAX)
				RecordCursorDiagnostic("raw-read stage=mouse ok=0 error=%lu", static_cast<unsigned long>(GetLastError()));
			else if (read >= sizeof(RAWINPUTHEADER) + sizeof(RAWMOUSE))
				RecordCursorDiagnostic("raw-mouse device=%p mode=%llu flags=0x%x dx=%ld dy=%ld buttons=0x%x data=0x%x extra=0x%lx",
					static_cast<void*>(raw.header.hDevice), static_cast<unsigned long long>(raw.header.wParam),
					static_cast<unsigned>(raw.data.mouse.usFlags), raw.data.mouse.lLastX, raw.data.mouse.lLastY,
					static_cast<unsigned>(raw.data.mouse.usButtonFlags), static_cast<unsigned>(raw.data.mouse.usButtonData),
					static_cast<unsigned long>(raw.data.mouse.ulExtraInformation));
			else RecordCursorDiagnostic("raw-read stage=mouse ok=0 short=%u", read);
		}
		SetLastError(previousError);
	}

	void RecordCursorDiagnostic(const char* format, ...) noexcept
	{
		if (!CursorDiagnosticsEnabled() || !format || !DiagnosticOutput::Available()) return;
		const DWORD previousError = GetLastError();
		const uint64_t tick = GetTickCount64();
		const DWORD thread = GetCurrentThreadId();
		char message[320]{};
		va_list arguments;
		va_start(arguments, format);
		const int length = std::vsnprintf(message, sizeof(message), format, arguments);
		va_end(arguments);
		if (length > 0)
		{
			if (static_cast<size_t>(length) >= sizeof(message))
			{
				constexpr char suffix[] = " [truncated]";
				std::memcpy(message + sizeof(message) - sizeof(suffix), suffix, sizeof(suffix));
			}
			// 保留生产者的时间、线程与序号；唯一输出 worker 承担所有 sink 等待。
			DiagnosticOutput::Printf(DiagnosticOutput::Stream::Output,
				"[CURSOR_TRACE] seq=%llu tick=%llu thread=%lu %s\r\n",
				static_cast<unsigned long long>(cursorDiagnosticSequence.fetch_add(1, std::memory_order_relaxed) + 1),
				static_cast<unsigned long long>(tick), static_cast<unsigned long>(thread), message);
		}
		SetLastError(previousError);
	}

	void FlushCursorDiagnostics() noexcept
	{
		// 兼容原绘制线程调用；只尝试通知，不在该线程执行任何输出或 drain。
		if (CursorDiagnosticsEnabled()) DiagnosticOutput::Wake();
	}

	double GetQpcTimeMilliseconds()
	{
		static LARGE_INTEGER frequency = {};
		if (frequency.QuadPart == 0) QueryPerformanceFrequency(&frequency); // 频率固定，初始化一次即可。
		LARGE_INTEGER counter = {};
		QueryPerformanceCounter(&counter);
		return static_cast<double>(counter.QuadPart) * 1000.0 / static_cast<double>(frequency.QuadPart);
	}

	void HighPrecisionWait(double frameTimeSpentMs, double targetFPS)
	{
		const double waitTimeMs = 1000.0 / targetFPS - frameTimeSpentMs; // 扣除本帧工作耗时后再补足目标帧间隔。
		if (waitTimeMs <= 0.0) return;

		static LARGE_INTEGER frequency = {};
		if (frequency.QuadPart == 0) QueryPerformanceFrequency(&frequency);
		LARGE_INTEGER startCounter = {};
		LARGE_INTEGER currentCounter = {};
		QueryPerformanceCounter(&startCounter);
		const long long waitTicks = static_cast<long long>(waitTimeMs * static_cast<double>(frequency.QuadPart) / 1000.0);
		const long long targetEndTick = startCounter.QuadPart + waitTicks; // 转成 QPC tick，最后用忙等对齐。

		// 先粗略睡眠，最后约 1.5ms 使用忙等待收紧帧间隔。
		if (waitTimeMs > 2.0)
		{
			std::this_thread::sleep_for(std::chrono::duration<double, std::milli>(waitTimeMs - 1.5));
		}
		do
		{
			QueryPerformanceCounter(&currentCounter);
			YieldProcessor(); // 短忙等阶段降低自旋对 CPU 的压力。
		} while (currentCounter.QuadPart < targetEndTick);
	}

	void LogFrameTiming(size_t committedIndex, size_t realPointCount, size_t predictedPointCount,
		size_t l0PointCount, double workMs, double previousFrameMs, bool idleFrozen)
	{
#if defined(_DEBUG)
		// 无输出端时跳过帧计时诊断的取时与格式化。
		if (!DiagnosticOutput::Available()) return;
		static double nextLogTimeMs = 0.0;
		const double nowMs = GetQpcTimeMilliseconds();
		if (nowMs < nextLogTimeMs) return;
		nextLogTimeMs = nowMs + 250.0; // Debug 最多每秒输出四次，避免逐帧日志扰动建模。
		const int logicFps = workMs > 0.001 ? static_cast<int>(1000.0 / workMs) : 0; // 只看本帧代码实际工作耗时。
		const int realFps = previousFrameMs > 0.001 ? static_cast<int>(1000.0 / previousFrameMs) : 0; // 包含等待后的真实帧间隔。
		char buffer[256] = {};
		const int length = std::snprintf(buffer, sizeof(buffer),
			"commit:%zu work:%.3fms logic:%d FPS prev-real:%d FPS(%.3fms) realPts:%zu predPts:%zu l0Pts:%zu frozen:%d\r\n",
			committedIndex, workMs, logicFps, realFps, previousFrameMs, realPointCount,
			predictedPointCount, l0PointCount, idleFrozen ? 1 : 0);
		if (length <= 0) return;
		WriteFastConsoleLine(buffer, static_cast<DWORD>(std::min(length, static_cast<int>(sizeof(buffer) - 1))));
#else
		(void)committedIndex;
		(void)realPointCount;
		(void)predictedPointCount;
		(void)l0PointCount;
		(void)workMs;
		(void)previousFrameMs;
		(void)idleFrozen;
#endif
	}

	void LogCanvasPan(const char* format, ...) noexcept
	{
	#if defined(DRAW3_RTS_DIAGNOSTICS)
		if (!rtsTraceEnabled.load(std::memory_order_acquire)) return;
		if (!format) return;
		char buffer[768] = {};
		constexpr char prefix[] = "[CanvasPan] ";
		std::memcpy(buffer, prefix, sizeof(prefix) - 1);
		va_list arguments;
		va_start(arguments, format);
		constexpr size_t prefixLength = sizeof(prefix) - 1;
		constexpr size_t bodyCapacity = sizeof(buffer) - prefixLength - 3;
		const int bodyLength = std::vsnprintf(buffer + prefixLength,
			bodyCapacity + 1, format, arguments);
		va_end(arguments);
		if (bodyLength < 0) return;
		const size_t used = prefixLength + (std::min)(
			bodyCapacity, static_cast<size_t>(bodyLength));
		buffer[used] = '\r';
		buffer[used + 1] = '\n';
		buffer[used + 2] = '\0';
		DiagnosticOutput::Write(DiagnosticOutput::Stream::Output, buffer, used + 2, true);
	#else
		(void)format;
	#endif
	}

	void LogHResult(const char* step, HRESULT result)
	{
		DiagnosticOutput::Line() << step << " failed. HRESULT=0x" << std::hex
			<< static_cast<unsigned long>(result) << std::dec << std::endl;
	}

	void LogWin32Error(const char* step, DWORD error)
	{
		DiagnosticOutput::Line() << step << " failed. GetLastError=" << error << std::endl;
	}

	const char* BoolText(BOOL value)
	{
		return value ? "true" : "false";
	}

	std::string WideToUtf8(const WCHAR* text)
	{
		if (!text || text[0] == L'\0') return {};
		const int requiredSize = WideCharToMultiByte(CP_UTF8, 0, text, -1, nullptr, 0, nullptr, nullptr); // 先查询 UTF-8 缓冲区大小。
		if (requiredSize <= 1) return {};
		std::string result(static_cast<size_t>(requiredSize), '\0');
		const int written = WideCharToMultiByte(
			CP_UTF8, 0, text, -1, result.data(), requiredSize, nullptr, nullptr);
		if (written != requiredSize) return {};
		result.resize(static_cast<size_t>(written - 1)); // 转换时保留 NUL 空间，返回前再移除。
		return result;
	}

	bool LogDwmColorizationState(const char* modeName, const char* stage, BOOL* outOpaqueBlend)
	{
		DWORD colorizationColor = 0;
		BOOL opaqueBlend = TRUE;
		const HRESULT result = DwmGetColorizationColor(&colorizationColor, &opaqueBlend);
		if (FAILED(result))
		{
			DiagnosticOutput::Line() << "[" << modeName << "] " << stage
				<< " DwmGetColorizationColor failed. HRESULT=0x" << std::hex
				<< static_cast<unsigned long>(result) << std::dec << std::endl;
			return false;
		}
		DiagnosticOutput::Line() << "[" << modeName << "] " << stage << " DwmColorizationColor=0x"
			<< std::hex << static_cast<unsigned long>(colorizationColor) << std::dec
			<< " opaqueBlend=" << BoolText(opaqueBlend) << std::endl;
		if (outOpaqueBlend) *outOpaqueBlend = opaqueBlend;
		return true;
	}

	void LogDwmModeDiagnostics(const char* modeName, HWND window, const char* stage)
	{
		if (!window) return;
		RECT windowRect = {};
		RECT clientRect = {};
		GetWindowRect(window, &windowRect); // 同时记录窗口和客户区，排查边框样式导致的尺寸偏差。
		GetClientRect(window, &clientRect);
		DiagnosticOutput::Line() << "[" << modeName << "] " << stage
			<< " hwnd=0x" << std::hex << reinterpret_cast<UINT_PTR>(window)
			<< " style=0x" << static_cast<unsigned long>(GetWindowLongPtr(window, GWL_STYLE))
			<< " exStyle=0x" << static_cast<unsigned long>(GetWindowLongPtr(window, GWL_EXSTYLE)) << std::dec
			<< " window=(" << windowRect.left << "," << windowRect.top << "," << windowRect.right << "," << windowRect.bottom << ")"
			<< " client=(" << clientRect.left << "," << clientRect.top << "," << clientRect.right << "," << clientRect.bottom << ")" << std::endl;

		BOOL compositionEnabled = FALSE;
		const HRESULT result = DwmIsCompositionEnabled(&compositionEnabled);
		if (SUCCEEDED(result))
		{
			DiagnosticOutput::Line() << "[" << modeName << "] " << stage
				<< " DwmIsCompositionEnabled=" << BoolText(compositionEnabled) << std::endl;
		}
		else
		{
			LogHResult("DwmIsCompositionEnabled", result);
		}
		LogDwmColorizationState(modeName, stage);
	}

	void LogSwapChainDescription(const char* modeName, const char* stage, const DXGI_SWAP_CHAIN_DESC1& description)
	{
		DiagnosticOutput::Line() << "[" << modeName << "] " << stage << " swapchain desc: size="
			<< description.Width << "x" << description.Height
			<< " format=" << DxgiFormatName(description.Format) << "(" << static_cast<unsigned int>(description.Format) << ")"
			<< " bufferCount=" << description.BufferCount
			<< " swapEffect=" << DxgiSwapEffectName(description.SwapEffect)
			<< " alphaMode=" << DxgiAlphaModeName(description.AlphaMode)
			<< " scaling=" << static_cast<unsigned int>(description.Scaling)
			<< " flags=0x" << std::hex << static_cast<unsigned int>(description.Flags) << std::dec << std::endl;
	}

	void LogSwapChainRuntimeDescription(const char* modeName, IDXGISwapChain1* swapChain, const char* stage)
	{
		if (!swapChain) return;
		DXGI_SWAP_CHAIN_DESC1 description = {};
		const HRESULT result = swapChain->GetDesc1(&description);
		if (FAILED(result))
		{
			LogHResult("IDXGISwapChain1::GetDesc1", result);
			return;
		}
		LogSwapChainDescription(modeName, stage, description);
	}

	void LogAdapterDiagnostics(IDXGIAdapter* adapter)
	{
		if (!adapter) return;
		DXGI_ADAPTER_DESC description = {};
		HRESULT result = adapter->GetDesc(&description);
		if (SUCCEEDED(result))
		{
			DiagnosticOutput::Line() << "DXGI adapter: " << WideToUtf8(description.Description)
				<< " VendorId=0x" << std::hex << description.VendorId
				<< " DeviceId=0x" << description.DeviceId
				<< " SubSysId=0x" << description.SubSysId
				<< " Revision=0x" << description.Revision << std::dec
				<< " DedicatedVideoMemory=" << static_cast<unsigned long long>(description.DedicatedVideoMemory)
				<< " DedicatedSystemMemory=" << static_cast<unsigned long long>(description.DedicatedSystemMemory)
				<< " SharedSystemMemory=" << static_cast<unsigned long long>(description.SharedSystemMemory) << std::endl;
		}
		else
		{
			LogHResult("IDXGIAdapter::GetDesc", result);
		}

		LARGE_INTEGER driverVersion = {};
		result = adapter->CheckInterfaceSupport(__uuidof(IDXGIDevice), &driverVersion);
		if (SUCCEEDED(result))
		{
			DiagnosticOutput::Line() << "DXGI adapter UMD driver version raw: high=0x" << std::hex
				<< static_cast<unsigned long>(driverVersion.HighPart) << " low=0x"
				<< static_cast<unsigned long>(driverVersion.LowPart) << std::dec << std::endl;
		}
		else
		{
			LogHResult("IDXGIAdapter::CheckInterfaceSupport(IDXGIDevice)", result);
		}
	}

	void SetStartupEnvironmentDiagnosticsEnabled(bool enabled) noexcept
	{
		startupEnvironmentDiagnosticsEnabled.store(enabled, std::memory_order_release);
	}

	bool StartupEnvironmentDiagnosticsEnabled() noexcept
	{
		return startupEnvironmentDiagnosticsEnabled.load(std::memory_order_acquire);
	}
}
