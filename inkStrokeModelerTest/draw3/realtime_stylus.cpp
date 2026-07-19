module;

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <mutex>
#include <new>
#include <windows.h>
#include <RTSCOM.h>
#include <RTSCOM_i.c>
#include <tchar.h>
#include <tpcshrd.h>
#include <wrl/client.h>

module draw3.realtime_stylus;

import draw3.diagnostics;

namespace draw3
{
	namespace
	{
		constexpr size_t kTabletMetadataCapacity = 32;
		int64_t QueryQpc() noexcept
		{
			LARGE_INTEGER value = {};
			QueryPerformanceCounter(&value);
			return value.QuadPart;
		}

		struct TabletMetadata
		{
			std::atomic<bool> published = false;
			TABLET_CONTEXT_ID tabletContextId = 0;
			ULONG propertyCount = 0;
			ULONG xIndex = 0;
			ULONG yIndex = 1;
			float packetScaleX = 1.0f;
			float packetScaleY = 1.0f;
			InputDeviceType deviceType = InputDeviceType::Pen;
		};

		class StylusSyncPlugin final : public IStylusSyncPlugin
		{
		public:
			explicit StylusSyncPlugin(ContactInputCoordinator& coordinator)
				: coordinator_(coordinator)
			{
				marshalerResult_ = CoCreateFreeThreadedMarshaler(
					static_cast<IUnknown*>(this), freeThreadedMarshaler_.ReleaseAndGetAddressOf());
			}

			HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** object) override
			{
				if (!object) return E_POINTER;
				*object = nullptr;
				if (iid == __uuidof(IMarshal) && freeThreadedMarshaler_)
					return freeThreadedMarshaler_->QueryInterface(iid, object);
				if (iid == __uuidof(IUnknown) || iid == __uuidof(IStylusPlugin) ||
					iid == __uuidof(IStylusSyncPlugin))
				{
					*object = static_cast<IStylusSyncPlugin*>(this);
					AddRef();
					return S_OK;
				}
				return E_NOINTERFACE;
			}

			HRESULT MarshalerResult() const noexcept
			{
				return marshalerResult_;
			}

			ULONG STDMETHODCALLTYPE AddRef() override
			{
				return referenceCount_.fetch_add(1, std::memory_order_relaxed) + 1;
			}

			ULONG STDMETHODCALLTYPE Release() override
			{
				const ULONG count = referenceCount_.fetch_sub(1, std::memory_order_acq_rel) - 1;
				if (count == 0) delete this;
				return count;
			}

			HRESULT STDMETHODCALLTYPE RealTimeStylusEnabled(IRealTimeStylus* source,
				ULONG contextCount, const TABLET_CONTEXT_ID* contextIds) override
			{
				if (!source || (contextCount > 0 && !contextIds)) return E_INVALIDARG;
				std::cout << "[RTS] enabled contexts=" << contextCount << std::endl;
				pixelScalePublished_.store(false, std::memory_order_release);
				if (contextCount > 0)
				{
					FLOAT scaleX = 1.0f;
					FLOAT scaleY = 1.0f;
					ULONG propertyCount = 0;
					PACKET_PROPERTY* properties = nullptr;
					const HRESULT scaleResult = source->GetPacketDescriptionData(
						contextIds[0], &scaleX, &scaleY, &propertyCount, &properties);
					CoTaskMemFree(properties);
					if (SUCCEEDED(scaleResult) && scaleX > 0.0f && scaleY > 0.0f)
					{
						// 与经过广泛验证的 IdtRts.cpp 一致：首 context 提供全输入统一像素比例。
						inkToPixelScaleX_ = scaleX;
						inkToPixelScaleY_ = scaleY;
						pixelScalePublished_.store(true, std::memory_order_release);
						std::cout << "[RTS] use first-context pixel scale tcid=" << contextIds[0]
							<< " scale=(" << scaleX << "," << scaleY << ")" << std::endl;
					}
					else
					{
						std::cout << "[RTS] first-context scale unavailable tcid=" << contextIds[0]
							<< " HRESULT=0x" << std::hex << static_cast<unsigned long>(scaleResult)
							<< std::dec << " scale=(" << scaleX << "," << scaleY << ")" << std::endl;
					}
				}
				for (ULONG index = 0; index < contextCount; ++index)
				{
					// 启用慢路径预先缓存 packet 元数据，热路径不做 COM 查询。
					EnsureMetadata(source, contextIds[index], nullptr);
				}
				// 单个 tablet 暂时无法查询时不能让整个插件进入 Error；Down 会按 tcid 再尝试一次。
				return S_OK;
			}

			HRESULT STDMETHODCALLTYPE RealTimeStylusDisabled(IRealTimeStylus*,
				ULONG, const TABLET_CONTEXT_ID*) override
			{
				std::cout << "[RTS] disabled." << std::endl;
				coordinator_.CloseAllProducerContacts(QueryQpc());
				return S_OK;
			}

			HRESULT STDMETHODCALLTYPE StylusInRange(IRealTimeStylus*, TABLET_CONTEXT_ID, STYLUS_ID) override
			{
				return S_OK;
			}

			HRESULT STDMETHODCALLTYPE StylusOutOfRange(IRealTimeStylus*, TABLET_CONTEXT_ID, STYLUS_ID) override
			{
				return S_OK;
			}

			HRESULT STDMETHODCALLTYPE StylusDown(IRealTimeStylus* source, const StylusInfo* stylusInfo,
				ULONG propertyCount, LONG* packet, LONG**) override
			{
				if (!source || !stylusInfo || !packet) return E_INVALIDARG;
				const TabletMetadata* metadata = EnsureMetadata(source, stylusInfo->tcid, nullptr);
				ContactSnapshot snapshot;
				if (!DecodeSnapshot(metadata, propertyCount, packet, ContactPhase::Down, snapshot))
				{
					std::cout << "[RTS] down decode failed tcid=" << stylusInfo->tcid
						<< " cid=" << stylusInfo->cid << " properties=" << propertyCount
						<< " metadata=" << (metadata ? "yes" : "no") << std::endl;
					return S_OK;
				}
				InputDeviceType deviceType = metadata ? metadata->deviceType : InputDeviceType::Pen;
				if (deviceType == InputDeviceType::MouseLeft)
				{
					const bool leftButtonDown = (GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0;
					const bool rightButtonDown = (GetAsyncKeyState(VK_RBUTTON) & 0x8000) != 0;
					deviceType = rightButtonDown && !leftButtonDown
						? InputDeviceType::MouseRight : InputDeviceType::MouseLeft;
				}
				const bool published = coordinator_.PublishDown(
					stylusInfo->tcid, stylusInfo->cid, deviceType, snapshot);
#if defined(_DEBUG)
				moveDiagnosticCount_.store(0, std::memory_order_relaxed); // 每次落笔重新保留少量 Move 日志。
				std::cout << "[RTS] down tcid=" << stylusInfo->tcid << " cid=" << stylusInfo->cid
					<< " type=" << static_cast<uint32_t>(deviceType)
					<< " properties=" << propertyCount << " raw=("
					<< packet[metadata->xIndex] << "," << packet[metadata->yIndex] << ") pixel=("
					<< snapshot.position.x << "," << snapshot.position.y << ") published="
					<< (published ? "yes" : "no") << std::endl;
#endif
				return published ? S_OK : E_OUTOFMEMORY;
			}

			HRESULT STDMETHODCALLTYPE StylusUp(IRealTimeStylus* source, const StylusInfo* stylusInfo,
				ULONG propertyCount, LONG* packet, LONG**) override
			{
				if (!stylusInfo || !packet) return E_INVALIDARG;
				const TabletMetadata* metadata = FindMetadata(stylusInfo->tcid);
				if (!metadata && source) metadata = EnsureMetadata(source, stylusInfo->tcid, nullptr);
				ContactSnapshot snapshot;
				if (!DecodeSnapshot(metadata, propertyCount, packet, ContactPhase::Up, snapshot))
				{
					std::cout << "[RTS] up decode failed tcid=" << stylusInfo->tcid
						<< " cid=" << stylusInfo->cid << " properties=" << propertyCount << std::endl;
					return S_OK;
				}
				const bool published = coordinator_.PublishUp(stylusInfo->tcid, stylusInfo->cid, snapshot);
#if defined(_DEBUG)
				std::cout << "[RTS] up tcid=" << stylusInfo->tcid << " cid=" << stylusInfo->cid
					<< " pixel=(" << snapshot.position.x << "," << snapshot.position.y
					<< ") published=" << (published ? "yes" : "no") << std::endl;
#endif
				return S_OK;
			}

			HRESULT STDMETHODCALLTYPE StylusButtonDown(IRealTimeStylus*, STYLUS_ID, const GUID*, POINT*) override
			{
				return S_OK;
			}

			HRESULT STDMETHODCALLTYPE StylusButtonUp(IRealTimeStylus*, STYLUS_ID, const GUID*, POINT*) override
			{
				return S_OK;
			}

			HRESULT STDMETHODCALLTYPE InAirPackets(IRealTimeStylus*, const StylusInfo*, ULONG,
				ULONG, LONG*, ULONG*, LONG**) override
			{
				return S_OK;
			}

			HRESULT STDMETHODCALLTYPE Packets(IRealTimeStylus*, const StylusInfo* stylusInfo,
				ULONG packetCount, ULONG packetBufferLength, LONG* packets, ULONG*, LONG**) override
			{
				if (!stylusInfo || !packets || packetCount == 0 || packetBufferLength < packetCount ||
					packetBufferLength % packetCount != 0) return E_INVALIDARG;
				const ULONG propertyCount = packetBufferLength / packetCount;
				const LONG* lastPacket = packets + static_cast<size_t>(packetCount - 1) * propertyCount;
				const TabletMetadata* metadata = FindMetadata(stylusInfo->tcid); // Move 热路径通常只扫描固定缓存。
				ContactSnapshot snapshot;
				if (!DecodeSnapshot(metadata, propertyCount, lastPacket, ContactPhase::Move, snapshot))
				{
					const uint32_t diagnosticIndex = moveDiagnosticCount_.fetch_add(1, std::memory_order_relaxed);
					if (diagnosticIndex < 8)
					{
						std::cout << "[RTS] move decode failed tcid=" << stylusInfo->tcid
							<< " cid=" << stylusInfo->cid << " packets=" << packetCount
							<< " properties=" << propertyCount << std::endl;
					}
					return S_OK;
				}
				const bool published = coordinator_.PublishMove(stylusInfo->tcid, stylusInfo->cid, snapshot);
#if defined(_DEBUG)
				const uint32_t diagnosticIndex = moveDiagnosticCount_.fetch_add(1, std::memory_order_relaxed);
				if (diagnosticIndex < 8)
				{
					std::cout << "[RTS] move tcid=" << stylusInfo->tcid << " cid=" << stylusInfo->cid
						<< " packets=" << packetCount << " pixel=(" << snapshot.position.x << ","
						<< snapshot.position.y << ") published=" << (published ? "yes" : "no") << std::endl;
				}
#endif
				return S_OK;
			}

			HRESULT STDMETHODCALLTYPE CustomStylusDataAdded(IRealTimeStylus*, const GUID*,
				ULONG, const BYTE*) override
			{
				return S_OK;
			}

			HRESULT STDMETHODCALLTYPE SystemEvent(IRealTimeStylus*, TABLET_CONTEXT_ID, STYLUS_ID,
				SYSTEM_EVENT, SYSTEM_EVENT_DATA) override
			{
				return S_OK;
			}

			HRESULT STDMETHODCALLTYPE TabletAdded(IRealTimeStylus* source, IInkTablet* tablet) override
			{
				if (!source || !tablet) return E_INVALIDARG;
				TABLET_CONTEXT_ID contextId = 0;
				const HRESULT result = source->GetTabletContextIdFromTablet(tablet, &contextId);
				if (FAILED(result)) return result;
				EnsureMetadata(source, contextId, tablet);
				return S_OK; // Tablet 初始化时序不稳定时保留后续 Down 的重试机会。
			}

			HRESULT STDMETHODCALLTYPE TabletRemoved(IRealTimeStylus*, LONG) override
			{
				// 回调只给 tablet index，无法无查询地还原 tcid；设备移除时安全取消全部活动 contact。
				coordinator_.CloseAllProducerContacts(QueryQpc());
				return S_OK;
			}

			HRESULT STDMETHODCALLTYPE Error(IRealTimeStylus*, IStylusPlugin*, RealTimeStylusDataInterest dataInterest,
				HRESULT errorCode, LONG_PTR*) override
			{
				lastError_.store(errorCode, std::memory_order_release);
				std::cout << "[RTS] plugin error dataInterest=0x" << std::hex
					<< static_cast<unsigned long>(dataInterest)
					<< " HRESULT=0x" << static_cast<unsigned long>(errorCode) << std::dec << std::endl;
				coordinator_.CloseAllProducerContacts(QueryQpc());
				return S_OK;
			}

			HRESULT STDMETHODCALLTYPE UpdateMapping(IRealTimeStylus*) override
			{
				return S_OK;
			}

			HRESULT STDMETHODCALLTYPE DataInterest(RealTimeStylusDataInterest* interest) override
			{
				if (!interest) return E_POINTER;
				*interest = static_cast<RealTimeStylusDataInterest>(
					RTSDI_RealTimeStylusEnabled | RTSDI_RealTimeStylusDisabled |
					RTSDI_StylusDown | RTSDI_Packets | RTSDI_StylusUp |
					RTSDI_TabletAdded | RTSDI_TabletRemoved | RTSDI_Error);
				return S_OK;
			}

		private:
			const TabletMetadata* FindMetadata(TABLET_CONTEXT_ID contextId) const noexcept
			{
				for (const TabletMetadata& metadata : metadata_)
				{
					if (metadata.published.load(std::memory_order_acquire) &&
						metadata.tabletContextId == contextId) return &metadata;
				}
				return nullptr;
			}

			const TabletMetadata* EnsureMetadata(IRealTimeStylus* source,
				TABLET_CONTEXT_ID contextId, IInkTablet* suppliedTablet)
			{
				if (const TabletMetadata* existing = FindMetadata(contextId)) return existing;
				std::lock_guard lock(metadataMutex_);
				if (const TabletMetadata* existing = FindMetadata(contextId)) return existing;

				TabletMetadata* target = nullptr;
				for (TabletMetadata& metadata : metadata_)
				{
					if (!metadata.published.load(std::memory_order_relaxed))
					{
						target = &metadata;
						break;
					}
				}
				if (!target) return nullptr;

				FLOAT inkToDeviceScaleX = 1.0f;
				FLOAT inkToDeviceScaleY = 1.0f;
				ULONG propertyCount = 0;
				PACKET_PROPERTY* properties = nullptr;
				const HRESULT packetResult = source->GetPacketDescriptionData(contextId,
					&inkToDeviceScaleX, &inkToDeviceScaleY, &propertyCount, &properties);
				if (FAILED(packetResult))
				{
					CoTaskMemFree(properties);
					std::cout << "[RTS] metadata query failed tcid=" << contextId
						<< " HRESULT=0x" << std::hex << static_cast<unsigned long>(packetResult)
						<< std::dec << std::endl;
					return nullptr;
				}

				ULONG xIndex = 0;
				ULONG yIndex = 1;
				bool hasX = false;
				bool hasY = false;
				for (ULONG index = 0; index < propertyCount; ++index)
				{
					if (IsEqualGUID(properties[index].guid, GUID_PACKETPROPERTY_GUID_X))
					{
						xIndex = index;
						hasX = true;
					}
					else if (IsEqualGUID(properties[index].guid, GUID_PACKETPROPERTY_GUID_Y))
					{
						yIndex = index;
						hasY = true;
					}
				}
				CoTaskMemFree(properties);
				if (!hasX || !hasY || inkToDeviceScaleX <= 0.0f || inkToDeviceScaleY <= 0.0f)
				{
					std::cout << "[RTS] invalid metadata tcid=" << contextId
						<< " properties=" << propertyCount << " hasX=" << hasX << " hasY=" << hasY
						<< " scale=(" << inkToDeviceScaleX << "," << inkToDeviceScaleY << ")" << std::endl;
					return nullptr;
				}

				Microsoft::WRL::ComPtr<IInkTablet> tablet;
				if (suppliedTablet)
					tablet = suppliedTablet;
				else
					source->GetTabletFromTabletContextId(contextId, tablet.ReleaseAndGetAddressOf());
				InputDeviceType deviceType = InputDeviceType::Pen;
				if (tablet)
				{
					Microsoft::WRL::ComPtr<IInkTablet2> tablet2;
					if (SUCCEEDED(tablet.As(&tablet2)))
					{
						TabletDeviceKind kind = TDK_Pen;
						if (SUCCEEDED(tablet2->get_DeviceKind(&kind)))
						{
							if (kind == TDK_Touch) deviceType = InputDeviceType::Touch;
							else if (kind == TDK_Mouse) deviceType = InputDeviceType::MouseLeft;
						}
					}
				}

				target->tabletContextId = contextId;
				target->propertyCount = propertyCount;
				target->xIndex = xIndex;
				target->yIndex = yIndex;
				target->packetScaleX = inkToDeviceScaleX;
				target->packetScaleY = inkToDeviceScaleY;
				target->deviceType = deviceType;
				target->published.store(true, std::memory_order_release);
				std::cout << "[RTS] metadata tcid=" << contextId << " type="
					<< static_cast<uint32_t>(deviceType) << " properties=" << propertyCount
					<< " xyIndex=(" << xIndex << "," << yIndex << ") scale=("
					<< inkToDeviceScaleX << "," << inkToDeviceScaleY << ")" << std::endl;
				return target;
			}

			bool DecodeSnapshot(const TabletMetadata* metadata, ULONG propertyCount,
				const LONG* packet, ContactPhase phase, ContactSnapshot& snapshot) const noexcept
			{
				if (!metadata || !packet || propertyCount < 2) return false;
				const ULONG xIndex = metadata->xIndex;
				const ULONG yIndex = metadata->yIndex;
				if (xIndex >= propertyCount || yIndex >= propertyCount) return false;
				if (!pixelScalePublished_.load(std::memory_order_acquire)) return false;
				// 当前 tcid 只决定属性索引；像素比例统一沿用首 tablet context。
				snapshot.position.x = static_cast<float>(packet[xIndex]) * inkToPixelScaleX_;
				snapshot.position.y = static_cast<float>(packet[yIndex]) * inkToPixelScaleY_;
				snapshot.pressure = -1.0f; // 本阶段保留字段，但真实硬件压感不参与绘制。
				snapshot.contactSize = { -1.0f, -1.0f };
				snapshot.qpc = QueryQpc();
				snapshot.phase = phase;
				return std::isfinite(snapshot.position.x) && std::isfinite(snapshot.position.y);
			}

			std::atomic<ULONG> referenceCount_ = 1;
			std::atomic<HRESULT> lastError_ = S_OK;
			std::atomic<uint32_t> moveDiagnosticCount_ = 0;
			ContactInputCoordinator& coordinator_;
			std::atomic<bool> pixelScalePublished_ = false;
			float inkToPixelScaleX_ = 1.0f;
			float inkToPixelScaleY_ = 1.0f;
			Microsoft::WRL::ComPtr<IUnknown> freeThreadedMarshaler_;
			HRESULT marshalerResult_ = E_UNEXPECTED;
			std::mutex metadataMutex_;
			std::array<TabletMetadata, kTabletMetadataCapacity> metadata_ = {};
		};
	}

	struct RealTimeStylusInputImpl
	{
		Microsoft::WRL::ComPtr<IRealTimeStylus> stylus;
		Microsoft::WRL::ComPtr<IRealTimeStylus3> stylus3;
		Microsoft::WRL::ComPtr<IStylusSyncPlugin> plugin;
		ContactInputCoordinator* coordinator = nullptr;
		bool comInitialized = false;
		bool pluginAdded = false;
		bool initialized = false;
	};

	RealTimeStylusInput::RealTimeStylusInput()
		: impl_(std::make_unique<RealTimeStylusInputImpl>())
	{
	}

	RealTimeStylusInput::~RealTimeStylusInput()
	{
		Shutdown();
	}

	bool RealTimeStylusInput::Initialize(HWND window, ContactInputCoordinator& coordinator)
	{
		if (!window || impl_->initialized) return false;
		impl_->coordinator = &coordinator;
		DWORD windowProcessId = 0;
		const DWORD windowThreadId = GetWindowThreadProcessId(window, &windowProcessId);
		std::cout << "[RTS] initialize currentThread=" << GetCurrentThreadId()
			<< " windowThread=" << windowThreadId
			<< " digitizer=0x" << std::hex << GetSystemMetrics(SM_DIGITIZER) << std::dec
			<< " maxTouches=" << GetSystemMetrics(SM_MAXIMUMTOUCHES)
			<< " tabletWindowFlags=0x" << std::hex
			<< reinterpret_cast<ULONG_PTR>(GetProp(window, MICROSOFT_TABLETPENSERVICE_PROPERTY))
			<< std::dec << std::endl;

		HRESULT result = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
		if (FAILED(result))
		{
			LogHResult("CoInitializeEx(COINIT_MULTITHREADED)", result);
			impl_->coordinator = nullptr;
			return false;
		}
		impl_->comInitialized = true;

		result = CoCreateInstance(__uuidof(RealTimeStylus), nullptr, CLSCTX_INPROC_SERVER,
			IID_PPV_ARGS(impl_->stylus.ReleaseAndGetAddressOf()));
		if (FAILED(result))
		{
			LogHResult("CoCreateInstance(RealTimeStylus)", result);
			Shutdown();
			return false;
		}

		result = impl_->stylus->put_HWND(reinterpret_cast<HANDLE_PTR>(window));
		if (FAILED(result)) LogHResult("Bind RealTimeStylus HWND", result);
		if (SUCCEEDED(result)) result = impl_->stylus->SetAllTabletsMode(TRUE); // 同时接收鼠标、笔和触摸。
		if (FAILED(result)) LogHResult("Set RealTimeStylus all-tablets mode", result);
		const GUID desiredProperties[] = { GUID_PACKETPROPERTY_GUID_X, GUID_PACKETPROPERTY_GUID_Y };
		if (SUCCEEDED(result)) result = impl_->stylus->SetDesiredPacketDescription(
			static_cast<ULONG>(std::size(desiredProperties)), desiredProperties);
		if (FAILED(result)) LogHResult("Set RealTimeStylus packet description", result);
		if (SUCCEEDED(result))
		{
			Microsoft::WRL::ComPtr<IRealTimeStylus2> stylus2;
			if (SUCCEEDED(impl_->stylus.As(&stylus2)))
			{
				// 窗口标志负责多点 opt-in；这里关闭轻拂，避免笔输入被系统手势延迟或接管。
				const HRESULT flicksResult = stylus2->put_FlicksEnabled(FALSE);
				if (FAILED(flicksResult)) LogHResult("Disable RealTimeStylus flicks", flicksResult);
			}
		}
		if (SUCCEEDED(result)) result = impl_->stylus.As(&impl_->stylus3);
		if (SUCCEEDED(result)) result = impl_->stylus3->put_MultiTouchEnabled(TRUE);
		if (SUCCEEDED(result))
		{
			BOOL multiTouchEnabled = FALSE;
			const HRESULT verifyResult = impl_->stylus3->get_MultiTouchEnabled(&multiTouchEnabled);
			if (SUCCEEDED(verifyResult))
				std::cout << "[RTS] MultiTouchEnabled=" << (multiTouchEnabled ? "true" : "false") << std::endl;
			else
				LogHResult("Read RealTimeStylus multi-touch state", verifyResult);
		}
		if (FAILED(result))
		{
			LogHResult("Configure RealTimeStylus multi-contact input", result);
			Shutdown();
			return false;
		}

		auto* plugin = new (std::nothrow) StylusSyncPlugin(coordinator);
		if (!plugin)
		{
			Shutdown();
			return false;
		}
		if (FAILED(plugin->MarshalerResult()))
		{
			LogHResult("CoCreateFreeThreadedMarshaler", plugin->MarshalerResult());
			plugin->Release();
			Shutdown();
			return false;
		}
		impl_->plugin.Attach(plugin);
		result = impl_->stylus->AddStylusSyncPlugin(0, impl_->plugin.Get());
		if (FAILED(result))
		{
			LogHResult("AddStylusSyncPlugin", result);
			Shutdown();
			return false;
		}
		impl_->pluginAdded = true;

		result = impl_->stylus->put_Enabled(TRUE);
		if (FAILED(result))
		{
			LogHResult("Enable RealTimeStylus", result);
			Shutdown();
			return false;
		}
		impl_->initialized = true;
		return true;
	}

	void RealTimeStylusInput::Shutdown() noexcept
	{
		if (!impl_) return;
		if (impl_->stylus)
		{
			const HRESULT disableResult = impl_->stylus->put_Enabled(FALSE); // 先停止产生新的同步回调。
			if (FAILED(disableResult)) LogHResult("Disable RealTimeStylus", disableResult);
			if (impl_->pluginAdded)
			{
				IStylusSyncPlugin* removedPlugin = nullptr;
				const HRESULT removeResult = impl_->stylus->RemoveStylusSyncPlugin(0, &removedPlugin);
				if (FAILED(removeResult)) LogHResult("RemoveStylusSyncPlugin", removeResult);
				if (removedPlugin) removedPlugin->Release();
				impl_->pluginAdded = false;
			}
		}

		if (impl_->coordinator) impl_->coordinator->CloseAllProducerContacts(QueryQpc());
		impl_->plugin.Reset();
		impl_->stylus3.Reset();
		impl_->stylus.Reset();
		impl_->coordinator = nullptr;
		impl_->initialized = false;
		if (impl_->comInitialized)
		{
			CoUninitialize();
			impl_->comInitialized = false;
		}
	}

	bool RealTimeStylusInput::IsInitialized() const noexcept
	{
		return impl_ && impl_->initialized;
	}
}
