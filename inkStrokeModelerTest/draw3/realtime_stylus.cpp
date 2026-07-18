module;

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <mutex>
#include <new>
#include <windows.h>
#include <RTSCOM.h>
#include <RTSCOM_i.c>
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
				for (ULONG index = 0; index < contextCount; ++index)
				{
					// 启用慢路径预先缓存 packet 元数据，热路径不做 COM 查询。
					if (!EnsureMetadata(source, contextIds[index], nullptr)) return E_FAIL;
				}
				return S_OK;
			}

			HRESULT STDMETHODCALLTYPE RealTimeStylusDisabled(IRealTimeStylus*,
				ULONG, const TABLET_CONTEXT_ID*) override
			{
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
				if (!DecodeSnapshot(metadata, propertyCount, packet, ContactPhase::Down, snapshot)) return E_INVALIDARG;
				InputDeviceType deviceType = metadata ? metadata->deviceType : InputDeviceType::Pen;
				if (deviceType == InputDeviceType::MouseLeft)
				{
					const bool leftButtonDown = (GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0;
					const bool rightButtonDown = (GetAsyncKeyState(VK_RBUTTON) & 0x8000) != 0;
					deviceType = rightButtonDown && !leftButtonDown
						? InputDeviceType::MouseRight : InputDeviceType::MouseLeft;
				}
				return coordinator_.PublishDown(stylusInfo->tcid, stylusInfo->cid, deviceType, snapshot)
					? S_OK : E_OUTOFMEMORY;
			}

			HRESULT STDMETHODCALLTYPE StylusUp(IRealTimeStylus*, const StylusInfo* stylusInfo,
				ULONG propertyCount, LONG* packet, LONG**) override
			{
				if (!stylusInfo || !packet) return E_INVALIDARG;
				const TabletMetadata* metadata = FindMetadata(stylusInfo->tcid);
				ContactSnapshot snapshot;
				if (!DecodeSnapshot(metadata, propertyCount, packet, ContactPhase::Up, snapshot)) return E_INVALIDARG;
				coordinator_.PublishUp(stylusInfo->tcid, stylusInfo->cid, snapshot);
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
				const TabletMetadata* metadata = FindMetadata(stylusInfo->tcid); // Move 热路径只扫描固定缓存。
				ContactSnapshot snapshot;
				if (!DecodeSnapshot(metadata, propertyCount, lastPacket, ContactPhase::Move, snapshot))
					return E_FAIL;
				coordinator_.PublishMove(stylusInfo->tcid, stylusInfo->cid, snapshot);
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
				return EnsureMetadata(source, contextId, tablet) ? S_OK : E_FAIL;
			}

			HRESULT STDMETHODCALLTYPE TabletRemoved(IRealTimeStylus*, LONG) override
			{
				// 回调只给 tablet index，无法无查询地还原 tcid；设备移除时安全取消全部活动 contact。
				coordinator_.CloseAllProducerContacts(QueryQpc());
				return S_OK;
			}

			HRESULT STDMETHODCALLTYPE Error(IRealTimeStylus*, IStylusPlugin*, RealTimeStylusDataInterest,
				HRESULT errorCode, LONG_PTR*) override
			{
				lastError_.store(errorCode, std::memory_order_release);
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
					return nullptr;

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
				target->xIndex = xIndex;
				target->yIndex = yIndex;
				target->packetScaleX = inkToDeviceScaleX;
				target->packetScaleY = inkToDeviceScaleY;
				target->deviceType = deviceType;
				target->published.store(true, std::memory_order_release);
				return target;
			}

			bool DecodeSnapshot(const TabletMetadata* metadata, ULONG propertyCount,
				const LONG* packet, ContactPhase phase, ContactSnapshot& snapshot) const noexcept
			{
				if (!metadata || !packet || propertyCount < 2) return false;
				const ULONG xIndex = metadata->xIndex;
				const ULONG yIndex = metadata->yIndex;
				if (xIndex >= propertyCount || yIndex >= propertyCount) return false;
				// GetPacketDescriptionData 返回 ink-space 到 device-space 的乘法比例。
				snapshot.position.x = static_cast<float>(packet[xIndex]) * metadata->packetScaleX;
				snapshot.position.y = static_cast<float>(packet[yIndex]) * metadata->packetScaleY;
				snapshot.pressure = -1.0f; // 本阶段保留字段，但真实硬件压感不参与绘制。
				snapshot.contactSize = { -1.0f, -1.0f };
				snapshot.qpc = QueryQpc();
				snapshot.phase = phase;
				return std::isfinite(snapshot.position.x) && std::isfinite(snapshot.position.y);
			}

			std::atomic<ULONG> referenceCount_ = 1;
			std::atomic<HRESULT> lastError_ = S_OK;
			ContactInputCoordinator& coordinator_;
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
		if (SUCCEEDED(result)) result = impl_->stylus->SetAllTabletsMode(TRUE); // 同时接收鼠标、笔和触摸。
		const GUID desiredProperties[] = { GUID_PACKETPROPERTY_GUID_X, GUID_PACKETPROPERTY_GUID_Y };
		if (SUCCEEDED(result)) result = impl_->stylus->SetDesiredPacketDescription(
			static_cast<ULONG>(std::size(desiredProperties)), desiredProperties);
		if (SUCCEEDED(result)) result = impl_->stylus.As(&impl_->stylus3);
		if (SUCCEEDED(result)) result = impl_->stylus3->put_MultiTouchEnabled(TRUE);
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
