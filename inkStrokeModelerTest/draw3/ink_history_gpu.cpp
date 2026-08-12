module;

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <memory>
#include <optional>
#include <span>
#include <utility>
#include <vector>
#include <d3d11.h>
#include <d3d11_1.h>
#include <dxgiformat.h>
#include <windows.h>
#include <wrl/client.h>

module draw3.ink_history_gpu;

import draw3.ink_prediction;

namespace draw3
{
	namespace
	{
		using Microsoft::WRL::ComPtr;

		constexpr float kCompositionShapeType = 14.0f;
		constexpr float kApplyCachedShapeType = 15.0f;

		struct CacheGlobalConstants
		{
			float width = 0.0f;
			float height = 0.0f;
			float shapeType = 0.0f;
			uint32_t bufferOffset = 0;
			float color[4] = {};
			uint32_t operatorKind = 0;
			float padding[3] = {};
		};

		static_assert(sizeof(CacheGlobalConstants) == 48);

		struct HistoryCacheConstants
		{
			float targetRect[4] = {};
			float sourceUvRect[4] = {};
			uint32_t earlierSlice = 0;
			uint32_t laterSlice = 0;
			uint32_t sourceSlice = 0;
			uint32_t padding = 0;
		};

		static_assert(sizeof(HistoryCacheConstants) == 48);
		static_assert(sizeof(HistoryCacheConstants) % 16 == 0);

		bool IsEmptyRectLocal(const RECT& rect) noexcept
		{
			return rect.left >= rect.right || rect.top >= rect.bottom;
		}

		void UnionRectLocal(RECT& target, const RECT& addition) noexcept
		{
			if (IsEmptyRectLocal(addition)) return;
			if (IsEmptyRectLocal(target))
			{
				target = addition;
				return;
			}
			target.left = std::min(target.left, addition.left);
			target.top = std::min(target.top, addition.top);
			target.right = std::max(target.right, addition.right);
			target.bottom = std::max(target.bottom, addition.bottom);
		}

		bool ContainsTile(std::span<const SignedTileCoordinate> tiles,
			SignedTileCoordinate tile) noexcept
		{
			return std::binary_search(tiles.begin(), tiles.end(), tile);
		}

		std::optional<RECT> TileRect(SignedTileCoordinate tile,
			uint32_t tileSize, int width, int height) noexcept
		{
			if (width <= 0 || height <= 0 || tileSize == 0) return std::nullopt;
			const int64_t left64 = static_cast<int64_t>(tile.x) * tileSize;
			const int64_t top64 = static_cast<int64_t>(tile.y) * tileSize;
			const int64_t right64 = left64 + tileSize;
			const int64_t bottom64 = top64 + tileSize;
			const int64_t clippedLeft = std::max<int64_t>(0, left64);
			const int64_t clippedTop = std::max<int64_t>(0, top64);
			const int64_t clippedRight = std::min<int64_t>(width, right64);
			const int64_t clippedBottom = std::min<int64_t>(height, bottom64);
			if (clippedLeft >= clippedRight || clippedTop >= clippedBottom)
				return std::nullopt;
			return RECT{
				static_cast<LONG>(clippedLeft), static_cast<LONG>(clippedTop),
				static_cast<LONG>(clippedRight), static_cast<LONG>(clippedBottom) };
		}

		struct TileScreenMapping
		{
			float canvasLeft = 0.0f;
			float canvasTop = 0.0f;
			float canvasRight = 0.0f;
			float canvasBottom = 0.0f;
			RECT dirty = {};
		};

		std::optional<TileScreenMapping> MapTileToScreen(
			SignedTileCoordinate tile, uint32_t tileSize,
			float viewportX, float viewportY, int width, int height) noexcept
		{
			if (tileSize == 0 || width <= 0 || height <= 0 ||
				!std::isfinite(viewportX) || !std::isfinite(viewportY)) return std::nullopt;
			const double tileLeft = static_cast<double>(tile.x) * tileSize;
			const double tileTop = static_cast<double>(tile.y) * tileSize;
			const double canvasLeft = std::max(tileLeft, static_cast<double>(viewportX));
			const double canvasTop = std::max(tileTop, static_cast<double>(viewportY));
			const double canvasRight = std::min(tileLeft + tileSize,
				static_cast<double>(viewportX) + width);
			const double canvasBottom = std::min(tileTop + tileSize,
				static_cast<double>(viewportY) + height);
			if (!(canvasLeft < canvasRight && canvasTop < canvasBottom))
				return std::nullopt;
			TileScreenMapping mapping;
			mapping.canvasLeft = static_cast<float>(canvasLeft);
			mapping.canvasTop = static_cast<float>(canvasTop);
			mapping.canvasRight = static_cast<float>(canvasRight);
			mapping.canvasBottom = static_cast<float>(canvasBottom);
			mapping.dirty = {
				static_cast<LONG>(std::floor(canvasLeft - viewportX)),
				static_cast<LONG>(std::floor(canvasTop - viewportY)),
				static_cast<LONG>(std::ceil(canvasRight - viewportX)),
				static_cast<LONG>(std::ceil(canvasBottom - viewportY))
			};
			mapping.dirty.left = std::clamp(mapping.dirty.left, 0L, static_cast<LONG>(width));
			mapping.dirty.top = std::clamp(mapping.dirty.top, 0L, static_cast<LONG>(height));
			mapping.dirty.right = std::clamp(mapping.dirty.right, 0L, static_cast<LONG>(width));
			mapping.dirty.bottom = std::clamp(mapping.dirty.bottom, 0L, static_cast<LONG>(height));
			return IsEmptyRectLocal(mapping.dirty)
				? std::nullopt : std::optional<TileScreenMapping>(mapping);
		}

		int64_t TileOrigin(int32_t coordinate, uint32_t tileSize) noexcept
		{
			return static_cast<int64_t>(coordinate) * tileSize;
		}

		void ReportCacheFailureOnce(const wchar_t* message) noexcept
		{
			static bool reported = false;
			if (reported) return;
			reported = true;
			::OutputDebugStringW(message);
		}
	}

	struct InkHistoryGpuCache::Impl
	{
		struct UndoPage
		{
			ComPtr<ID3D11Texture2D> texture;
			uint32_t sliceCount = 0;
		};

		struct UndoSlot
		{
			uint32_t page = 0;
			uint32_t slice = 0;
		};

		struct HotTile
		{
			SignedTileCoordinate tile = {};
			UndoSlot slot = {};
			RECT screenRect = {};
		};

		struct HotEntry
		{
			UndoCacheEntryId id = {};
			HistoryCanvasIdentity canvas = {};
			RenderItemId item = {};
			InkHistoryRasterKey rasterKey = {};
			InkRasterStateToken beforeState = 0;
			InkRasterStateToken afterState = 0;
			float viewportX = 0.0f;
			float viewportY = 0.0f;
			int canvasWidth = 0;
			int canvasHeight = 0;
			bool committed = false;
			std::vector<HotTile> tiles;
		};

		struct OperatorPage
		{
			ComPtr<ID3D11Texture2D> addTexture;
			std::vector<ComPtr<ID3D11ShaderResourceView>> addSRVs;
			std::vector<ComPtr<ID3D11RenderTargetView>> addRTVs;
			ComPtr<ID3D11Texture2D> retainTexture;
			std::vector<ComPtr<ID3D11ShaderResourceView>> retainSRVs;
			std::vector<ComPtr<ID3D11RenderTargetView>> retainRTVs;
			uint32_t sliceCount = 0;
		};

		struct OperatorSlot
		{
			uint32_t page = 0;
			uint32_t slice = 0;
		};

		struct FullCompositionKey
		{
			HistoryCanvasIdentity canvas = {};
			InkHistoryRasterKey rasterKey = {};
			SignedTileCoordinate tile = {};
			CompositionNodeId node = {};
			uint64_t tileGeneration = 0;

			friend bool operator==(const FullCompositionKey& left,
				const FullCompositionKey& right) noexcept
			{
				return left.canvas == right.canvas && left.rasterKey == right.rasterKey &&
					left.tile == right.tile && left.node == right.node &&
					left.tileGeneration == right.tileGeneration;
			}
		};

		struct ResidentNode
		{
			FullCompositionKey key = {};
			CompositionCacheKeyId plannerKey = {};
			OperatorSlot slot = {};
		};

		struct NodeBuildResult
		{
			bool succeeded = false;
			bool rebuilt = false;
			CompositionCacheKeyId plannerKey = {};
			OperatorSlot slot = {};
			bool identity = false;
		};

		struct ReplayRangeResult
		{
			bool succeeded = false;
			bool usedDirect = false;
		};

		InkRenderer* renderer = nullptr;
		ComPtr<ID3D11DeviceContext1> context1;
		UndoCachePolicy undoPolicy = {};
		CompositionCachePolicy compositionPolicy = {};
		UndoCachePlanner undoPlanner;
		CompositionCachePlanner compositionPlanner;
		std::vector<UndoPage> undoPages;
		std::vector<UndoSlot> freeUndoSlots;
		std::vector<HotEntry> hotEntries;
		std::vector<OperatorPage> compositionPages;
		std::vector<OperatorSlot> freeCompositionSlots;
		std::vector<ResidentNode> residentNodes;
		std::array<OperatorPage, 3> scratchPages;
		ComPtr<ID3D11Buffer> cacheConstantBuffer;
		std::vector<InkPoint> pointScratch;
		HighlighterGeometry highlighterScratch;
		uint64_t nextHotEntryId = 1;
		uint64_t nextCompositionKeyId = 1;
		bool scratchReady = false;
		bool compositionPassAvailable = false;

		Impl() : undoPlanner(undoPolicy), compositionPlanner(compositionPolicy) {}

		void Reset() noexcept
		{
			renderer = nullptr;
			context1.Reset();
			undoPages.clear();
			freeUndoSlots.clear();
			hotEntries.clear();
			compositionPages.clear();
			freeCompositionSlots.clear();
			residentNodes.clear();
			for (OperatorPage& page : scratchPages) page = {};
			cacheConstantBuffer.Reset();
			pointScratch.clear();
			highlighterScratch.primitives.clear();
			highlighterScratch.bounds = {};
			undoPlanner = UndoCachePlanner(undoPolicy);
			compositionPlanner = CompositionCachePlanner(compositionPolicy);
			nextHotEntryId = 1;
			nextCompositionKeyId = 1;
			scratchReady = false;
			compositionPassAvailable = false;
		}

		bool CreateUndoPage(uint32_t sliceCount)
		{
			if (!renderer || !renderer->device || sliceCount == 0) return false;
			D3D11_TEXTURE2D_DESC description = {};
			description.Width = kUndoTileSize;
			description.Height = kUndoTileSize;
			description.MipLevels = 1;
			description.ArraySize = sliceCount;
			description.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
			description.SampleDesc.Count = 1;
			description.Usage = D3D11_USAGE_DEFAULT;
			UndoPage page;
			if (FAILED(renderer->device->CreateTexture2D(
				&description, nullptr, page.texture.ReleaseAndGetAddressOf()))) return false;
			page.sliceCount = sliceCount;
			const uint32_t pageIndex = static_cast<uint32_t>(undoPages.size());
			undoPages.push_back(std::move(page));
			for (uint32_t slice = 0; slice < sliceCount; ++slice)
				freeUndoSlots.push_back({ pageIndex, slice });
			return true;
		}

		bool EnsureUndoSlots(size_t required)
		{
			const size_t capacity = undoPlanner.SlotCapacity();
			size_t allocated = 0;
			for (const UndoPage& page : undoPages) allocated += page.sliceCount;
			while (freeUndoSlots.size() < required && allocated < capacity)
			{
				const size_t remaining = capacity - allocated;
				const uint32_t slices = static_cast<uint32_t>(std::min<size_t>(
					remaining, kUndoTilesPerPage));
				if (!CreateUndoPage(slices)) return false;
				allocated += slices;
			}
			return freeUndoSlots.size() >= required;
		}

		void FreeHotEntrySlots(const HotEntry& entry)
		{
			for (const HotTile& tile : entry.tiles) freeUndoSlots.push_back(tile.slot);
		}

		void RemoveHotEntry(UndoCacheEntryId id)
		{
			const auto iterator = std::find_if(hotEntries.begin(), hotEntries.end(),
				[&](const HotEntry& entry) { return entry.id == id; });
			if (iterator == hotEntries.end()) return;
			FreeHotEntrySlots(*iterator);
			hotEntries.erase(iterator);
		}

		void ApplyUndoEvictions(std::span<const UndoCacheEntryId> ids)
		{
			for (UndoCacheEntryId id : ids) RemoveHotEntry(id);
		}

		bool RepackUndoStorageToPolicy()
		{
			const size_t capacity = undoPlanner.SlotCapacity();
			if (capacity == 0 || undoPlanner.Policy().maxEntries == 0)
			{
				hotEntries.clear();
				freeUndoSlots.clear();
				undoPages.clear();
				return true;
			}

			size_t allocated = 0;
			for (const UndoPage& page : undoPages) allocated += page.sliceCount;
			if (allocated <= capacity) return true;
			const size_t required = undoPlanner.UsedSlotCount();
			if (required > capacity) return false;
			if (required == 0)
			{
				freeUndoSlots.clear();
				undoPages.clear();
				return true;
			}

			std::vector<UndoPage> replacementPages;
			std::vector<UndoSlot> replacementFreeSlots;
			size_t replacementAllocated = 0;
			while (replacementFreeSlots.size() < required)
			{
				const uint32_t slices = static_cast<uint32_t>(std::min<size_t>(
					capacity - replacementAllocated, kUndoTilesPerPage));
				if (slices == 0 || !renderer || !renderer->device) return false;
				D3D11_TEXTURE2D_DESC description = {};
				description.Width = kUndoTileSize;
				description.Height = kUndoTileSize;
				description.MipLevels = 1;
				description.ArraySize = slices;
				description.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
				description.SampleDesc.Count = 1;
				description.Usage = D3D11_USAGE_DEFAULT;
				UndoPage page;
				if (FAILED(renderer->device->CreateTexture2D(
					&description, nullptr, page.texture.ReleaseAndGetAddressOf()))) return false;
				page.sliceCount = slices;
				const uint32_t pageIndex = static_cast<uint32_t>(replacementPages.size());
				replacementPages.push_back(std::move(page));
				for (uint32_t slice = 0; slice < slices; ++slice)
					replacementFreeSlots.push_back({ pageIndex, slice });
				replacementAllocated += slices;
			}

			UnbindAllShaderResources();
			renderer->context->OMSetRenderTargets(0, nullptr, nullptr);
			for (HotEntry& entry : hotEntries)
			{
				for (HotTile& tile : entry.tiles)
				{
					const UndoSlot replacement = replacementFreeSlots.back();
					replacementFreeSlots.pop_back();
					const UINT sourceSubresource = D3D11CalcSubresource(0, tile.slot.slice, 1);
					const UINT destinationSubresource = D3D11CalcSubresource(
						0, replacement.slice, 1);
					renderer->context->CopySubresourceRegion(
						replacementPages[replacement.page].texture.Get(), destinationSubresource,
						0, 0, 0, undoPages[tile.slot.page].texture.Get(),
						sourceSubresource, nullptr);
					tile.slot = replacement;
				}
			}
			undoPages = std::move(replacementPages);
			freeUndoSlots = std::move(replacementFreeSlots);
			return true;
		}

		void UnbindAllShaderResources()
		{
			if (!renderer || !renderer->context) return;
			ID3D11ShaderResourceView* nullResources[14] = {};
			renderer->context->VSSetShaderResources(0, 14, nullResources);
			renderer->context->PSSetShaderResources(0, 14, nullResources);
		}

		bool CreateOperatorPage(uint32_t sliceCount, OperatorPage& page)
		{
			if (!renderer || !renderer->device || sliceCount == 0) return false;
			D3D11_TEXTURE2D_DESC description = {};
			description.Width = kCompositionTileSize;
			description.Height = kCompositionTileSize;
			description.MipLevels = 1;
			description.ArraySize = sliceCount;
			description.SampleDesc.Count = 1;
			description.Usage = D3D11_USAGE_DEFAULT;
			description.BindFlags = D3D11_BIND_RENDER_TARGET |
				D3D11_BIND_SHADER_RESOURCE;

			auto createTextureAndViews = [&](DXGI_FORMAT format,
				ComPtr<ID3D11Texture2D>& texture,
				std::vector<ComPtr<ID3D11ShaderResourceView>>& srvs,
				std::vector<ComPtr<ID3D11RenderTargetView>>& rtvs)
			{
				description.Format = format;
				if (FAILED(renderer->device->CreateTexture2D(&description, nullptr,
					texture.ReleaseAndGetAddressOf()))) return false;
				srvs.resize(sliceCount);
				rtvs.resize(sliceCount);
				for (uint32_t slice = 0; slice < sliceCount; ++slice)
				{
					// 每个 SRV 只覆盖一个 slice，父节点可安全写同一 array 的其他 slice。
					D3D11_SHADER_RESOURCE_VIEW_DESC srvDescription = {};
					srvDescription.Format = format;
					srvDescription.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2DARRAY;
					srvDescription.Texture2DArray.MostDetailedMip = 0;
					srvDescription.Texture2DArray.MipLevels = 1;
					srvDescription.Texture2DArray.FirstArraySlice = slice;
					srvDescription.Texture2DArray.ArraySize = 1;
					if (FAILED(renderer->device->CreateShaderResourceView(texture.Get(),
						&srvDescription, srvs[slice].ReleaseAndGetAddressOf()))) return false;
					D3D11_RENDER_TARGET_VIEW_DESC rtvDescription = {};
					rtvDescription.Format = format;
					rtvDescription.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2DARRAY;
					rtvDescription.Texture2DArray.MipSlice = 0;
					rtvDescription.Texture2DArray.FirstArraySlice = slice;
					rtvDescription.Texture2DArray.ArraySize = 1;
					if (FAILED(renderer->device->CreateRenderTargetView(texture.Get(),
						&rtvDescription, rtvs[slice].ReleaseAndGetAddressOf()))) return false;
				}
				return true;
			};

			OperatorPage created;
			if (!createTextureAndViews(DXGI_FORMAT_B8G8R8A8_UNORM,
				created.addTexture, created.addSRVs, created.addRTVs)) return false;
			if (!createTextureAndViews(DXGI_FORMAT_R16_FLOAT,
				created.retainTexture, created.retainSRVs, created.retainRTVs)) return false;
			created.sliceCount = sliceCount;
			page = std::move(created);
			return true;
		}

		bool EnsureScratch()
		{
			if (scratchReady) return true;
			for (OperatorPage& page : scratchPages)
			{
				if (!CreateOperatorPage(1, page))
				{
					for (OperatorPage& cleanup : scratchPages) cleanup = {};
					return false;
				}
			}
			scratchReady = true;
			return true;
		}

		bool EnsureCompositionSlot()
		{
			if (!freeCompositionSlots.empty()) return true;
			const size_t capacity = compositionPlanner.SlotCapacity();
			size_t allocated = 0;
			for (const OperatorPage& page : compositionPages) allocated += page.sliceCount;
			if (allocated >= capacity) return false;
			const uint32_t slices = static_cast<uint32_t>(std::min<size_t>(
				capacity - allocated, kCompositionTilesPerPage));
			OperatorPage page;
			if (!CreateOperatorPage(slices, page)) return false;
			const uint32_t pageIndex = static_cast<uint32_t>(compositionPages.size());
			compositionPages.push_back(std::move(page));
			for (uint32_t slice = 0; slice < slices; ++slice)
				freeCompositionSlots.push_back({ pageIndex, slice });
			return true;
		}

		OperatorPage& PageFor(OperatorSlot slot)
		{
			return compositionPages[slot.page];
		}

		void ClearOperator(OperatorPage& page, uint32_t slice)
		{
			UnbindAllShaderResources();
			ID3D11RenderTargetView* targets[] = {
				page.addRTVs[slice].Get(), page.retainRTVs[slice].Get() };
			renderer->context->OMSetRenderTargets(2, targets, nullptr);
			const float addClear[4] = {};
			const float retainClear[4] = { 1.0f, 1.0f, 1.0f, 1.0f };
			renderer->context->ClearRenderTargetView(targets[0], addClear);
			renderer->context->ClearRenderTargetView(targets[1], retainClear);
		}

		void CopyOperator(OperatorPage& source, uint32_t sourceSlice,
			OperatorPage& destination, uint32_t destinationSlice)
		{
			UnbindAllShaderResources();
			renderer->context->OMSetRenderTargets(0, nullptr, nullptr);
			const UINT sourceSubresource = D3D11CalcSubresource(0, sourceSlice, 1);
			const UINT destinationSubresource = D3D11CalcSubresource(
				0, destinationSlice, 1);
			renderer->context->CopySubresourceRegion(destination.addTexture.Get(),
				destinationSubresource, 0, 0, 0, source.addTexture.Get(),
				sourceSubresource, nullptr);
			renderer->context->CopySubresourceRegion(destination.retainTexture.Get(),
				destinationSubresource, 0, 0, 0, source.retainTexture.Get(),
				sourceSubresource, nullptr);
		}

		bool UpdatePassConstants(float shapeType, float width, float height,
			const HistoryCacheConstants& historyConstants)
		{
			D3D11_MAPPED_SUBRESOURCE mapped = {};
			if (FAILED(renderer->context->Map(renderer->globalCB.Get(), 0,
				D3D11_MAP_WRITE_DISCARD, 0, &mapped))) return false;
			CacheGlobalConstants constants = {};
			constants.width = width;
			constants.height = height;
			constants.shapeType = shapeType;
			std::memcpy(mapped.pData, &constants, sizeof(constants));
			renderer->context->Unmap(renderer->globalCB.Get(), 0);

			if (FAILED(renderer->context->Map(cacheConstantBuffer.Get(), 0,
				D3D11_MAP_WRITE_DISCARD, 0, &mapped))) return false;
			std::memcpy(mapped.pData, &historyConstants, sizeof(historyConstants));
			renderer->context->Unmap(cacheConstantBuffer.Get(), 0);
			return true;
		}

		void BindCommonCachePass()
		{
			renderer->context->IASetInputLayout(nullptr);
			renderer->context->IASetPrimitiveTopology(
				D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
			renderer->context->VSSetShader(renderer->vertexShader.Get(), nullptr, 0);
			renderer->context->PSSetShader(renderer->pixelShader.Get(), nullptr, 0);
			ID3D11Buffer* globalBuffers[] = { renderer->globalCB.Get() };
			ID3D11Buffer* cacheBuffers[] = { cacheConstantBuffer.Get() };
			renderer->context->VSSetConstantBuffers(0, 1, globalBuffers);
			renderer->context->PSSetConstantBuffers(0, 1, globalBuffers);
			renderer->context->VSSetConstantBuffers(2, 1, cacheBuffers);
			renderer->context->PSSetConstantBuffers(2, 1, cacheBuffers);
			ID3D11SamplerState* samplers[] = { renderer->operatorSampler.Get() };
			renderer->context->PSSetSamplers(0, 1, samplers);
			renderer->context->RSSetState(renderer->rasterState.Get());
		}

		void FinishCachePass()
		{
			ID3D11ShaderResourceView* nullResources[4] = {};
			renderer->context->PSSetShaderResources(10, 4, nullResources);
			ID3D11Buffer* nullBuffer[] = { nullptr };
			renderer->context->VSSetConstantBuffers(2, 1, nullBuffer);
			renderer->context->PSSetConstantBuffers(2, 1, nullBuffer);
		}

		void FinishHistoryOperation(int width, int height) noexcept
		{
			if (!renderer || !renderer->context) return;
			UnbindAllShaderResources();
			FinishCachePass();
			renderer->context->OMSetRenderTargets(0, nullptr, nullptr);
			renderer->context->RSSetState(renderer->rasterState.Get());
			if (width > 0 && height > 0)
				renderer->SetScreenSize(
					static_cast<float>(width), static_cast<float>(height));
		}

		bool ComposeOperator(OperatorPage& earlier, uint32_t earlierSlice,
			OperatorPage& later, uint32_t laterSlice,
			OperatorPage& output, uint32_t outputSlice)
		{
			HistoryCacheConstants constants = {};
			constants.targetRect[2] = static_cast<float>(kCompositionTileSize);
			constants.targetRect[3] = static_cast<float>(kCompositionTileSize);
			constants.sourceUvRect[2] = 1.0f;
			constants.sourceUvRect[3] = 1.0f;
			constants.earlierSlice = 0;
			constants.laterSlice = 0;
			if (!UpdatePassConstants(kCompositionShapeType,
				static_cast<float>(kCompositionTileSize),
				static_cast<float>(kCompositionTileSize), constants)) return false;

			UnbindAllShaderResources();
			ID3D11RenderTargetView* targets[] = {
				output.addRTVs[outputSlice].Get(), output.retainRTVs[outputSlice].Get() };
			renderer->context->OMSetRenderTargets(2, targets, nullptr);
			ID3D11ShaderResourceView* sources[] = {
				earlier.addSRVs[earlierSlice].Get(), earlier.retainSRVs[earlierSlice].Get(),
				later.addSRVs[laterSlice].Get(), later.retainSRVs[laterSlice].Get() };
			renderer->context->PSSetShaderResources(10, 4, sources);
			D3D11_VIEWPORT viewport = { 0.0f, 0.0f,
				static_cast<float>(kCompositionTileSize),
				static_cast<float>(kCompositionTileSize), 0.0f, 1.0f };
			renderer->context->RSSetViewports(1, &viewport);
			BindCommonCachePass();
			renderer->context->OMSetBlendState(nullptr, nullptr, 0xFFFFFFFF);
			renderer->context->Draw(6, 0);
			FinishCachePass();
			return true;
		}

		bool ApplyOperator(OperatorPage& source, uint32_t sourceSlice,
			SignedTileCoordinate tile, float viewportX, float viewportY,
			int width, int height)
		{
			const std::optional<TileScreenMapping> mapping = MapTileToScreen(
				tile, kCompositionTileSize, viewportX, viewportY, width, height);
			if (!mapping) return true;
			const int64_t originX = TileOrigin(tile.x, kCompositionTileSize);
			const int64_t originY = TileOrigin(tile.y, kCompositionTileSize);
			HistoryCacheConstants constants = {};
			constants.targetRect[0] = mapping->canvasLeft - viewportX;
			constants.targetRect[1] = mapping->canvasTop - viewportY;
			constants.targetRect[2] = mapping->canvasRight - viewportX;
			constants.targetRect[3] = mapping->canvasBottom - viewportY;
			constants.sourceUvRect[0] =
				(mapping->canvasLeft - static_cast<float>(originX)) / kCompositionTileSize;
			constants.sourceUvRect[1] =
				(mapping->canvasTop - static_cast<float>(originY)) / kCompositionTileSize;
			constants.sourceUvRect[2] =
				(mapping->canvasRight - static_cast<float>(originX)) / kCompositionTileSize;
			constants.sourceUvRect[3] =
				(mapping->canvasBottom - static_cast<float>(originY)) / kCompositionTileSize;
			constants.sourceSlice = 0;
			if (!UpdatePassConstants(kApplyCachedShapeType,
				static_cast<float>(width), static_cast<float>(height), constants)) return false;

			UnbindAllShaderResources();
			renderer->SetOMTarget(renderer->layerL2RTV.Get());
			ID3D11ShaderResourceView* sources[] = {
				source.addSRVs[sourceSlice].Get(),
				source.retainSRVs[sourceSlice].Get(), nullptr, nullptr };
			renderer->context->PSSetShaderResources(10, 4, sources);
			D3D11_VIEWPORT viewport = { 0.0f, 0.0f,
				static_cast<float>(width), static_cast<float>(height), 0.0f, 1.0f };
			renderer->context->RSSetViewports(1, &viewport);
			BindCommonCachePass();
			renderer->context->OMSetBlendState(
				renderer->operatorResolveBlendState.Get(), nullptr, 0xFFFFFFFF);
			renderer->context->Draw(6, 0);
			FinishCachePass();
			return true;
		}

		bool ClearL2Tile(SignedTileCoordinate tile, float viewportX, float viewportY,
			int width, int height)
		{
			const std::optional<TileScreenMapping> mapping = MapTileToScreen(
				tile, kCompositionTileSize, viewportX, viewportY, width, height);
			if (!mapping) return true;
			const RECT clipped = mapping->dirty;
			if (context1)
			{
				const float transparent[4] = {};
				const D3D11_RECT clearRect = {
					clipped.left, clipped.top, clipped.right, clipped.bottom };
				UnbindAllShaderResources();
				context1->ClearView(renderer->layerL2RTV.Get(), transparent, &clearRect, 1);
				return true;
			}
			if (!EnsureScratch()) return false;
			ClearOperator(scratchPages[0], 0);
			UnbindAllShaderResources();
			renderer->context->OMSetRenderTargets(0, nullptr, nullptr);
			D3D11_BOX sourceBox = {};
			sourceBox.left = 0;
			sourceBox.top = 0;
			sourceBox.right = static_cast<UINT>(clipped.right - clipped.left);
			sourceBox.bottom = static_cast<UINT>(clipped.bottom - clipped.top);
			sourceBox.back = 1;
			renderer->context->CopySubresourceRegion(renderer->layerL2Texture.Get(), 0,
				static_cast<UINT>(clipped.left), static_cast<UINT>(clipped.top), 0,
				scratchPages[0].addTexture.Get(), 0, &sourceBox);
			return true;
		}

		bool RasterItem(const InkCanvas& canvas, const RenderItemState& item,
			SignedTileCoordinate tile, OperatorPage& destination, uint32_t slice)
		{
			if (item.strokeIndex >= canvas.Strokes().size()) return false;
			ClearOperator(destination, slice);
			OperatorLayerResources targetLayer;
			targetLayer.addRTV = destination.addRTVs[slice];
			targetLayer.retainRTV = destination.retainRTVs[slice];
			const int64_t originX = TileOrigin(tile.x, kCompositionTileSize);
			const int64_t originY = TileOrigin(tile.y, kCompositionTileSize);
			const StoredStrokeRasterTarget rasterTarget = {
				&targetLayer,
				static_cast<float>(originX), static_cast<float>(originY),
				static_cast<int>(kCompositionTileSize),
				static_cast<int>(kCompositionTileSize)
			};
			return DrawStoredStroke(canvas.Strokes()[item.strokeIndex], *renderer,
				rasterTarget, pointScratch, highlighterScratch).succeeded;
		}

		bool ReplayRangeDirect(const InkCanvas& canvas,
			const CanvasRuntimeHistory& history, SignedTileCoordinate tile,
			size_t begin, size_t end, float viewportX, float viewportY,
			int width, int height)
		{
			const std::optional<TileScreenMapping> mapping = MapTileToScreen(
				tile, kCompositionTileSize, viewportX, viewportY, width, height);
			if (!mapping) return true;
			const std::span<const RenderItemState> items = history.Items();
			end = std::min(end, items.size());
			renderer->ClearOperatorLayer(renderer->layerL0);
			for (size_t index = std::min(begin, end); index < end; ++index)
			{
				const RenderItemState& item = items[index];
				if (!item.visible || !ContainsTile(item.compositionTiles, tile) ||
					item.strokeIndex >= canvas.Strokes().size()) continue;
				renderer->ClearOperatorLayer(renderer->layerL1);
				const StoredStrokeRasterTarget target = {
					&renderer->layerL1, viewportX, viewportY, width, height };
				const StoredStrokeRasterResult rasterized = DrawStoredStroke(
					canvas.Strokes()[item.strokeIndex], *renderer,
					target, pointScratch, highlighterScratch);
				if (!rasterized.succeeded || !renderer->ApplyOperatorLayers(
					renderer->layerL2RTV.Get(), renderer->layerL1,
					renderer->layerL0, mapping->dirty)) return false;
			}
			return true;
		}

		ReplayRangeResult ReplayRange(const InkCanvas& canvas,
			const CanvasRuntimeHistory& history,
			SignedTileCoordinate tile, size_t begin, size_t end,
			float viewportX, float viewportY, int width, int height)
		{
			if (!compositionPassAvailable || !EnsureScratch())
				return { ReplayRangeDirect(
					canvas, history, tile, begin, end,
					viewportX, viewportY, width, height), true };
			const std::span<const RenderItemState> items = history.Items();
			end = std::min(end, items.size());
			for (size_t index = std::min(begin, end); index < end; ++index)
			{
				const RenderItemState& item = items[index];
				if (!item.visible || !ContainsTile(item.compositionTiles, tile)) continue;
				if (!RasterItem(canvas, item, tile, scratchPages[2], 0)) return {};
				if (!ApplyOperator(scratchPages[2], 0, tile,
					viewportX, viewportY, width, height)) return {};
			}
			return { true, false };
		}

		bool BuildNodeByOrderedItems(const InkCanvas& canvas,
			const CanvasRuntimeHistory& history, CompositionNodeId node,
			SignedTileCoordinate tile, OperatorPage& destination, uint32_t slice)
		{
			if (!EnsureScratch()) return false;
			ClearOperator(scratchPages[0], 0);
			uint32_t accumulator = 0;
			const RenderItemRange range = history.CompositionTree().NodeItemRange(node);
			const std::span<const RenderItemState> items = history.Items();
			const size_t end = std::min(range.end, items.size());
			for (size_t index = std::min(range.begin, end); index < end; ++index)
			{
				const RenderItemState& item = items[index];
				if (!item.visible || !ContainsTile(item.compositionTiles, tile)) continue;
				if (!RasterItem(canvas, item, tile, scratchPages[2], 0)) return false;
				const uint32_t nextAccumulator = accumulator == 0 ? 1 : 0;
				if (!ComposeOperator(scratchPages[accumulator], 0,
					scratchPages[2], 0, scratchPages[nextAccumulator], 0)) return false;
				accumulator = nextAccumulator;
			}
			CopyOperator(scratchPages[accumulator], 0, destination, slice);
			return true;
		}

		ResidentNode* FindResident(const FullCompositionKey& key)
		{
			const auto iterator = std::find_if(residentNodes.begin(), residentNodes.end(),
				[&](const ResidentNode& entry) { return entry.key == key; });
			return iterator == residentNodes.end() ? nullptr : &*iterator;
		}

		void RemoveResident(CompositionCacheKeyId key)
		{
			const auto iterator = std::find_if(residentNodes.begin(), residentNodes.end(),
				[&](const ResidentNode& entry) { return entry.plannerKey == key; });
			if (iterator == residentNodes.end()) return;
			freeCompositionSlots.push_back(iterator->slot);
			residentNodes.erase(iterator);
		}

		void ApplyCompositionEvictions(
			std::span<const CompositionCacheKeyId> keys)
		{
			for (CompositionCacheKeyId key : keys) RemoveResident(key);
		}

		bool RepackCompositionStorageToPolicy()
		{
			const size_t capacity = compositionPlanner.SlotCapacity();
			if (capacity == 0)
			{
				residentNodes.clear();
				freeCompositionSlots.clear();
				compositionPages.clear();
				return true;
			}

			size_t allocated = 0;
			for (const OperatorPage& page : compositionPages) allocated += page.sliceCount;
			if (allocated <= capacity) return true;
			if (residentNodes.size() > capacity) return false;
			if (residentNodes.empty())
			{
				freeCompositionSlots.clear();
				compositionPages.clear();
				return true;
			}

			std::vector<OperatorPage> replacementPages;
			std::vector<OperatorSlot> replacementFreeSlots;
			size_t replacementAllocated = 0;
			while (replacementFreeSlots.size() < residentNodes.size())
			{
				const uint32_t slices = static_cast<uint32_t>(std::min<size_t>(
					capacity - replacementAllocated, kCompositionTilesPerPage));
				OperatorPage page;
				if (slices == 0 || !CreateOperatorPage(slices, page)) return false;
				const uint32_t pageIndex = static_cast<uint32_t>(replacementPages.size());
				replacementPages.push_back(std::move(page));
				for (uint32_t slice = 0; slice < slices; ++slice)
					replacementFreeSlots.push_back({ pageIndex, slice });
				replacementAllocated += slices;
			}

			UnbindAllShaderResources();
			renderer->context->OMSetRenderTargets(0, nullptr, nullptr);
			for (ResidentNode& resident : residentNodes)
			{
				const OperatorSlot replacement = replacementFreeSlots.back();
				replacementFreeSlots.pop_back();
				OperatorPage& source = PageFor(resident.slot);
				OperatorPage& destination = replacementPages[replacement.page];
				const UINT sourceSubresource = D3D11CalcSubresource(
					0, resident.slot.slice, 1);
				const UINT destinationSubresource = D3D11CalcSubresource(
					0, replacement.slice, 1);
				renderer->context->CopySubresourceRegion(destination.addTexture.Get(),
					destinationSubresource, 0, 0, 0, source.addTexture.Get(),
					sourceSubresource, nullptr);
				renderer->context->CopySubresourceRegion(destination.retainTexture.Get(),
					destinationSubresource, 0, 0, 0, source.retainTexture.Get(),
					sourceSubresource, nullptr);
				resident.slot = replacement;
			}
			compositionPages = std::move(replacementPages);
			freeCompositionSlots = std::move(replacementFreeSlots);
			return true;
		}

		void UnpinNode(CompositionCacheKeyId key)
		{
			const std::vector<CompositionCacheKeyId> evicted =
				compositionPlanner.Unpin(key);
			ApplyCompositionEvictions(evicted);
		}

		NodeBuildResult GetOrBuildNode(const InkCanvas& canvas,
			const CanvasRuntimeHistory& history, const HistoryCanvasIdentity& canvasIdentity,
			const InkHistoryRasterKey& rasterKey, CompositionNodeId node,
			SignedTileCoordinate tile)
		{
			if (!history.CompositionTree().NodeTouchesTile(node, tile))
				return { true, false, {}, {}, true };
			const FullCompositionKey fullKey = {
				canvasIdentity, rasterKey, tile, node,
				history.CompositionTree().TileGeneration(node, tile)
			};
			if (ResidentNode* resident = FindResident(fullKey))
			{
				const CompositionCacheAcquireResult acquire =
					compositionPlanner.Acquire(resident->plannerKey);
				if (acquire.status == CompositionCacheAcquireStatus::Hit)
				{
					compositionPlanner.Pin(resident->plannerKey);
					return { true, false, resident->plannerKey, resident->slot };
				}
			}

			NodeBuildResult earlierChild;
			NodeBuildResult laterChild;
			const auto unpinChild = [&](const NodeBuildResult& child)
			{
				if (child.succeeded && !child.identity) UnpinNode(child.plannerKey);
			};
			const std::optional<std::pair<CompositionNodeId, CompositionNodeId>> children =
				history.CompositionTree().NodeChildren(node);
			if (children)
			{
				// 子节点在父节点写完前保持 pin，避免 LRU 复用仍在采样的 slice。
				earlierChild = GetOrBuildNode(canvas, history, canvasIdentity,
					rasterKey, children->first, tile);
				if (!earlierChild.succeeded) return {};
				laterChild = GetOrBuildNode(canvas, history, canvasIdentity,
					rasterKey, children->second, tile);
				if (!laterChild.succeeded)
				{
					unpinChild(earlierChild);
					return {};
				}
			}

			const CompositionCacheKeyId plannerKey = { nextCompositionKeyId++ };
			const CompositionCacheAcquireResult acquire =
				compositionPlanner.Acquire(plannerKey);
			if (acquire.status != CompositionCacheAcquireStatus::Allocated)
			{
				if (children)
				{
					unpinChild(laterChild);
					unpinChild(earlierChild);
				}
				return {};
			}
			if (acquire.evictedKey) RemoveResident(*acquire.evictedKey);
			if (!EnsureCompositionSlot())
			{
				compositionPlanner.Release(plannerKey);
				if (children)
				{
					unpinChild(laterChild);
					unpinChild(earlierChild);
				}
				return {};
			}
			const OperatorSlot slot = freeCompositionSlots.back();
			freeCompositionSlots.pop_back();
			residentNodes.push_back({ fullKey, plannerKey, slot });
			compositionPlanner.Pin(plannerKey);
			OperatorPage& page = PageFor(slot);
			bool built = false;
			if (children)
			{
				if (earlierChild.identity && laterChild.identity)
				{
					ClearOperator(page, slot.slice);
					built = true;
				}
				else if (earlierChild.identity)
				{
					OperatorPage& laterPage = PageFor(laterChild.slot);
					CopyOperator(laterPage, laterChild.slot.slice, page, slot.slice);
					built = true;
				}
				else if (laterChild.identity)
				{
					OperatorPage& earlierPage = PageFor(earlierChild.slot);
					CopyOperator(earlierPage, earlierChild.slot.slice, page, slot.slice);
					built = true;
				}
				else
				{
					OperatorPage& earlierPage = PageFor(earlierChild.slot);
					OperatorPage& laterPage = PageFor(laterChild.slot);
					built = ComposeOperator(earlierPage, earlierChild.slot.slice,
						laterPage, laterChild.slot.slice, page, slot.slice);
				}
				unpinChild(laterChild);
				unpinChild(earlierChild);
			}
			else
			{
				built = BuildNodeByOrderedItems(
					canvas, history, node, tile, page, slot.slice);
			}
			if (!built)
			{
				UnpinNode(plannerKey);
				compositionPlanner.Release(plannerKey);
				RemoveResident(plannerKey);
				return {};
			}
			return { true, true, plannerKey, slot };
		}

		bool IsNodeResident(const CanvasRuntimeHistory& history,
			const HistoryCanvasIdentity& canvasIdentity,
			const InkHistoryRasterKey& rasterKey, CompositionNodeId node,
			SignedTileCoordinate tile)
		{
			const FullCompositionKey fullKey = {
				canvasIdentity, rasterKey, tile, node,
				history.CompositionTree().TileGeneration(node, tile)
			};
			ResidentNode* resident = FindResident(fullKey);
			return resident && compositionPlanner.Contains(resident->plannerKey);
		}
	};

	InkHistoryGpuCache::InkHistoryGpuCache() : impl_(std::make_unique<Impl>()) {}
	InkHistoryGpuCache::~InkHistoryGpuCache() = default;
	InkHistoryGpuCache::InkHistoryGpuCache(InkHistoryGpuCache&&) noexcept = default;
	InkHistoryGpuCache& InkHistoryGpuCache::operator=(InkHistoryGpuCache&&) noexcept = default;

	bool InkHistoryGpuCache::Initialize(InkRenderer& renderer,
		UndoCachePolicy undoPolicy, CompositionCachePolicy compositionPolicy)
	{
		if (!impl_) impl_ = std::make_unique<Impl>();
		impl_->Reset();
		if (!renderer.device || !renderer.context || !renderer.layerL2Texture ||
			!renderer.layerL2RTV) return false;
		impl_->renderer = &renderer;
		renderer.context.As(&impl_->context1);
		impl_->undoPolicy = undoPolicy;
		impl_->compositionPolicy = compositionPolicy;
		impl_->undoPlanner = UndoCachePlanner(undoPolicy);
		impl_->compositionPlanner = CompositionCachePlanner(compositionPolicy);

		D3D11_BUFFER_DESC description = {};
		description.ByteWidth = sizeof(HistoryCacheConstants);
		description.Usage = D3D11_USAGE_DYNAMIC;
		description.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
		description.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
		impl_->compositionPassAvailable = SUCCEEDED(renderer.device->CreateBuffer(
			&description, nullptr, impl_->cacheConstantBuffer.ReleaseAndGetAddressOf()));
		if (!impl_->compositionPassAvailable)
			ReportCacheFailureOnce(
				L"[InkHistory] composition cache constants unavailable; hot undo remains enabled.\n");
		return true;
	}

	void InkHistoryGpuCache::Release() noexcept
	{
		if (impl_) impl_->Reset();
	}

	void InkHistoryGpuCache::SetUndoPolicy(UndoCachePolicy policy)
	{
		if (!impl_) return;
		impl_->undoPolicy = policy;
		const std::vector<UndoCacheEntryId> evicted = impl_->undoPlanner.SetPolicy(policy);
		impl_->ApplyUndoEvictions(evicted);
		if (!impl_->RepackUndoStorageToPolicy())
		{
			// 重排失败只丢弃热缓存，CPU history 和冷撤回仍可继续。
			impl_->hotEntries.clear();
			impl_->freeUndoSlots.clear();
			impl_->undoPages.clear();
			impl_->undoPlanner = UndoCachePlanner(policy);
			ReportCacheFailureOnce(
				L"[InkHistory] undo cache repack failed; hot preimages were discarded.\n");
		}
	}

	void InkHistoryGpuCache::SetCompositionPolicy(CompositionCachePolicy policy)
	{
		if (!impl_) return;
		impl_->compositionPolicy = policy;
		const std::vector<CompositionCacheKeyId> evicted =
			impl_->compositionPlanner.SetPolicy(policy);
		impl_->ApplyCompositionEvictions(evicted);
		if (!impl_->RepackCompositionStorageToPolicy())
		{
			// GPU 重排失败时回到按需重建，不影响文档和有序重放。
			impl_->residentNodes.clear();
			impl_->freeCompositionSlots.clear();
			impl_->compositionPages.clear();
			impl_->compositionPlanner = CompositionCachePlanner(policy);
			ReportCacheFailureOnce(
				L"[InkHistory] composition cache repack failed; cached nodes were discarded.\n");
		}
	}

	HotPreimageCaptureResult InkHistoryGpuCache::CapturePreimage(
		const HotPreimageCaptureRequest& request)
	{
		HotPreimageCaptureResult result;
		if (!impl_ || !impl_->renderer || !request.item.IsValid() ||
			request.canvas.pageGuid.IsZero() || request.canvas.device != request.rasterKey.device ||
			request.beforeState == 0 || request.afterState == 0 ||
			request.beforeState == request.afterState || request.canvasWidth <= 0 ||
			request.canvasHeight <= 0 || !std::isfinite(request.viewportX) ||
			!std::isfinite(request.viewportY) ||
			std::floor(request.viewportX) != request.viewportX ||
			std::floor(request.viewportY) != request.viewportY ||
			!std::isfinite(request.rasterKey.scale) ||
			request.rasterKey.scale <= 0.0f)
		{
			result.status = HotPreimageCaptureStatus::InvalidRequest;
			return result;
		}

		std::vector<SignedTileCoordinate> visibleTiles;
		visibleTiles.reserve(request.tiles.size());
		for (SignedTileCoordinate tile : request.tiles)
		{
			if (!MapTileToScreen(tile, kUndoTileSize,
				request.viewportX, request.viewportY,
				request.canvasWidth, request.canvasHeight)) continue;
			if (std::find(visibleTiles.begin(), visibleTiles.end(), tile) == visibleTiles.end())
				visibleTiles.push_back(tile);
		}
		std::sort(visibleTiles.begin(), visibleTiles.end());
		if (visibleTiles.empty())
		{
			result.status = HotPreimageCaptureStatus::InvalidRequest;
			return result;
		}

		const UndoCacheEntryId entryId = { impl_->nextHotEntryId++ };
		const UndoCacheReservation reservation = impl_->undoPlanner.Reserve(
			entryId, visibleTiles.size());
		impl_->ApplyUndoEvictions(reservation.evictedEntries);
		if (reservation.status == UndoCacheReservationStatus::Disabled)
		{
			result.status = HotPreimageCaptureStatus::Disabled;
			return result;
		}
		if (reservation.status == UndoCacheReservationStatus::EntryTooLarge)
		{
			result.status = HotPreimageCaptureStatus::EntryTooLarge;
			return result;
		}
		if (reservation.status != UndoCacheReservationStatus::Reserved ||
			!impl_->EnsureUndoSlots(visibleTiles.size()))
		{
			impl_->undoPlanner.Cancel(entryId);
			result.status = HotPreimageCaptureStatus::ResourceFailure;
			return result;
		}

		Impl::HotEntry entry;
		entry.id = entryId;
		entry.canvas = request.canvas;
		entry.item = request.item;
		entry.rasterKey = request.rasterKey;
		entry.beforeState = request.beforeState;
		entry.afterState = request.afterState;
		entry.viewportX = request.viewportX;
		entry.viewportY = request.viewportY;
		entry.canvasWidth = request.canvasWidth;
		entry.canvasHeight = request.canvasHeight;
		entry.tiles.reserve(visibleTiles.size());

		impl_->UnbindAllShaderResources();
		impl_->renderer->context->OMSetRenderTargets(0, nullptr, nullptr);
		for (SignedTileCoordinate tile : visibleTiles)
		{
			const TileScreenMapping mapping = *MapTileToScreen(
				tile, kUndoTileSize, request.viewportX, request.viewportY,
				request.canvasWidth, request.canvasHeight);
			const RECT screenRect = mapping.dirty;
			const Impl::UndoSlot slot = impl_->freeUndoSlots.back();
			impl_->freeUndoSlots.pop_back();
			const int64_t originX = TileOrigin(tile.x, kUndoTileSize);
			const int64_t originY = TileOrigin(tile.y, kUndoTileSize);
			D3D11_BOX sourceBox = {};
			sourceBox.left = static_cast<UINT>(screenRect.left);
			sourceBox.top = static_cast<UINT>(screenRect.top);
			sourceBox.right = static_cast<UINT>(screenRect.right);
			sourceBox.bottom = static_cast<UINT>(screenRect.bottom);
			sourceBox.back = 1;
			const UINT destinationSubresource = D3D11CalcSubresource(0, slot.slice, 1);
			impl_->renderer->context->CopySubresourceRegion(
				impl_->undoPages[slot.page].texture.Get(), destinationSubresource,
				static_cast<UINT>(static_cast<int64_t>(mapping.canvasLeft) - originX),
				static_cast<UINT>(static_cast<int64_t>(mapping.canvasTop) - originY), 0,
				impl_->renderer->layerL2Texture.Get(), 0, &sourceBox);
			entry.tiles.push_back({ tile, slot, screenRect });
		}

		impl_->hotEntries.push_back(std::move(entry));
		result.status = HotPreimageCaptureStatus::Captured;
		result.ticket = entryId.value;
		result.tileCount = visibleTiles.size();
		return result;
	}

	bool InkHistoryGpuCache::CommitPreimage(uint64_t ticket) noexcept
	{
		if (!impl_ || ticket == 0) return false;
		const UndoCacheEntryId id = { ticket };
		const auto iterator = std::find_if(impl_->hotEntries.begin(), impl_->hotEntries.end(),
			[&](const Impl::HotEntry& entry) { return entry.id == id; });
		if (iterator == impl_->hotEntries.end() || !impl_->undoPlanner.Commit(id)) return false;
		iterator->committed = true;
		return true;
	}

	void InkHistoryGpuCache::CancelPreimage(uint64_t ticket) noexcept
	{
		if (!impl_ || ticket == 0) return;
		const UndoCacheEntryId id = { ticket };
		impl_->undoPlanner.Cancel(id);
		impl_->RemoveHotEntry(id);
	}

	HotPreimageRestoreResult InkHistoryGpuCache::RestorePreimage(
		HistoryCanvasIdentity canvas, RenderItemId item,
		InkHistoryRasterKey rasterKey, InkRasterStateToken currentState,
		float viewportX, float viewportY, int canvasWidth, int canvasHeight)
	{
		HotPreimageRestoreResult result;
		if (!impl_ || !impl_->renderer || currentState == 0) return result;
		auto reverseIterator = std::find_if(impl_->hotEntries.rbegin(),
			impl_->hotEntries.rend(), [&](const Impl::HotEntry& entry)
			{
				return entry.committed && entry.canvas == canvas && entry.item == item &&
					entry.rasterKey == rasterKey && entry.afterState == currentState &&
					entry.viewportX == viewportX && entry.viewportY == viewportY &&
					entry.canvasWidth == canvasWidth && entry.canvasHeight == canvasHeight;
			});
		if (reverseIterator == impl_->hotEntries.rend()) return result;
		const UndoCacheEntryId entryId = reverseIterator->id;
		const InkRasterStateToken restoredState = reverseIterator->beforeState;
		const std::vector<Impl::HotTile> tiles = reverseIterator->tiles;

		impl_->UnbindAllShaderResources();
		impl_->renderer->context->OMSetRenderTargets(0, nullptr, nullptr);
		for (const Impl::HotTile& tile : tiles)
		{
			const int64_t originX = TileOrigin(tile.tile.x, kUndoTileSize);
			const int64_t originY = TileOrigin(tile.tile.y, kUndoTileSize);
			D3D11_BOX sourceBox = {};
			sourceBox.left = static_cast<UINT>(
				static_cast<int64_t>(tile.screenRect.left + viewportX) - originX);
			sourceBox.top = static_cast<UINT>(
				static_cast<int64_t>(tile.screenRect.top + viewportY) - originY);
			sourceBox.right = static_cast<UINT>(
				static_cast<int64_t>(tile.screenRect.right + viewportX) - originX);
			sourceBox.bottom = static_cast<UINT>(
				static_cast<int64_t>(tile.screenRect.bottom + viewportY) - originY);
			sourceBox.back = 1;
			const UINT sourceSubresource = D3D11CalcSubresource(0, tile.slot.slice, 1);
			impl_->renderer->context->CopySubresourceRegion(
				impl_->renderer->layerL2Texture.Get(), 0,
				static_cast<UINT>(tile.screenRect.left),
				static_cast<UINT>(tile.screenRect.top), 0,
				impl_->undoPages[tile.slot.page].texture.Get(), sourceSubresource,
				&sourceBox);
			UnionRectLocal(result.dirty, tile.screenRect);
		}

		impl_->undoPlanner.Consume(entryId);
		impl_->RemoveHotEntry(entryId);
		result.restored = true;
		result.restoredState = restoredState;
		return result;
	}

	size_t InkHistoryGpuCache::ConsecutiveHotDepth(HistoryCanvasIdentity canvas,
		InkHistoryRasterKey rasterKey, InkRasterStateToken currentState) const noexcept
	{
		if (!impl_ || currentState == 0) return 0;
		size_t depth = 0;
		InkRasterStateToken state = currentState;
		while (depth < impl_->undoPlanner.Policy().maxEntries)
		{
			const auto iterator = std::find_if(impl_->hotEntries.rbegin(),
				impl_->hotEntries.rend(), [&](const Impl::HotEntry& entry)
				{
					return entry.committed && entry.canvas == canvas &&
						entry.rasterKey == rasterKey && entry.afterState == state;
				});
			if (iterator == impl_->hotEntries.rend()) break;
			state = iterator->beforeState;
			++depth;
		}
		return depth;
	}

	void InkHistoryGpuCache::DiscardHotPreimages() noexcept
	{
		if (!impl_) return;
		impl_->hotEntries.clear();
		impl_->freeUndoSlots.clear();
		impl_->undoPages.clear();
		impl_->undoPlanner = UndoCachePlanner(impl_->undoPolicy);
	}

	CompositionRestoreResult InkHistoryGpuCache::RestoreComposition(
		const CompositionRestoreRequest& request)
	{
		CompositionRestoreResult result;
		if (!impl_ || !impl_->renderer || !request.documentCanvas || !request.history ||
			request.canvas.pageGuid.IsZero() || request.canvas.device != request.rasterKey.device ||
			request.canvasWidth <= 0 || request.canvasHeight <= 0 ||
			!std::isfinite(request.viewportX) || !std::isfinite(request.viewportY) ||
			!std::isfinite(request.rasterKey.scale) || request.rasterKey.scale <= 0.0f)
			return result;

		std::vector<SignedTileCoordinate> tiles(request.tiles.begin(), request.tiles.end());
		std::sort(tiles.begin(), tiles.end());
		tiles.erase(std::unique(tiles.begin(), tiles.end()), tiles.end());
		tiles.erase(std::remove_if(tiles.begin(), tiles.end(), [&](SignedTileCoordinate tile)
			{
				return !MapTileToScreen(tile, kCompositionTileSize,
					request.viewportX, request.viewportY,
					request.canvasWidth, request.canvasHeight).has_value();
			}), tiles.end());
		if (tiles.empty())
		{
			result.path = CompositionRestorePath::Empty;
			impl_->FinishHistoryOperation(request.canvasWidth, request.canvasHeight);
			return result;
		}

		const size_t rangeEnd = std::min(request.rangeEnd, request.history->Items().size());
		std::optional<size_t> excludedIndex;
		if (request.excludedItem)
		{
			const RenderItemState* excluded = request.history->Find(*request.excludedItem);
			if (!excluded || !excluded->visible || request.excludedItem->index >= rangeEnd)
			{
				impl_->FinishHistoryOperation(request.canvasWidth, request.canvasHeight);
				return result;
			}
			excludedIndex = request.excludedItem->index;
		}
		const auto replayRequestedRangeDirect = [&](SignedTileCoordinate tile)
		{
			if (!excludedIndex)
				return impl_->ReplayRangeDirect(*request.documentCanvas, *request.history,
					tile, 0, rangeEnd, request.viewportX, request.viewportY,
					request.canvasWidth, request.canvasHeight);
			return impl_->ReplayRangeDirect(*request.documentCanvas, *request.history,
				tile, 0, *excludedIndex, request.viewportX, request.viewportY,
				request.canvasWidth, request.canvasHeight) &&
				impl_->ReplayRangeDirect(*request.documentCanvas, *request.history,
					tile, *excludedIndex + 1, rangeEnd,
					request.viewportX, request.viewportY,
					request.canvasWidth, request.canvasHeight);
		};
		bool usedRebuild = false;
		bool usedReplay = !impl_->compositionPassAvailable ||
			impl_->compositionPlanner.SlotCapacity() == 0;
		for (SignedTileCoordinate tile : tiles)
		{
			const RECT tileDirty = MapTileToScreen(tile, kCompositionTileSize,
				request.viewportX, request.viewportY,
				request.canvasWidth, request.canvasHeight)->dirty;
			bool tileSucceeded = !request.clearTargetTiles ||
				impl_->ClearL2Tile(tile, request.viewportX, request.viewportY,
					request.canvasWidth, request.canvasHeight);
			if (tileSucceeded && !usedReplay)
			{
				std::optional<std::vector<CompositionRangePiece>> pieces =
					std::vector<CompositionRangePiece>{};
				const auto appendRange = [&](size_t begin, size_t end)
				{
					if (begin >= end) return true;
					const auto part = request.history->CompositionTree().DecomposeRange(
						begin, end, tile);
					if (!part) return false;
					pieces->insert(pieces->end(), part->begin(), part->end());
					return true;
				};
				if (excludedIndex)
				{
					if (!appendRange(0, *excludedIndex) ||
						!appendRange(*excludedIndex + 1, rangeEnd)) pieces.reset();
				}
				else if (!appendRange(0, rangeEnd)) pieces.reset();
				if (!pieces)
				{
					tileSucceeded = false;
				}
				else
				{
					for (const CompositionRangePiece& piece : *pieces)
					{
						if (piece.kind == CompositionRangePieceKind::CachedNode)
						{
							if (!impl_->IsNodeResident(*request.history, request.canvas,
								request.rasterKey, piece.node, tile)) usedRebuild = true;
							Impl::NodeBuildResult node = impl_->GetOrBuildNode(
								*request.documentCanvas, *request.history, request.canvas,
								request.rasterKey, piece.node, tile);
							if (!node.succeeded)
							{
								tileSucceeded = false;
								break;
							}
							usedRebuild = usedRebuild || node.rebuilt;
							if (!node.identity)
							{
								Impl::OperatorPage& page = impl_->PageFor(node.slot);
								tileSucceeded = impl_->ApplyOperator(page, node.slot.slice,
									tile, request.viewportX, request.viewportY,
									request.canvasWidth, request.canvasHeight);
								impl_->UnpinNode(node.plannerKey);
							}
							if (!tileSucceeded) break;
						}
						else
						{
							usedRebuild = true;
							const Impl::ReplayRangeResult replay = impl_->ReplayRange(
								*request.documentCanvas, *request.history,
								tile, piece.range.begin, piece.range.end,
								request.viewportX, request.viewportY,
								request.canvasWidth, request.canvasHeight);
							usedReplay = usedReplay || replay.usedDirect;
							if (!replay.succeeded)
							{
								tileSucceeded = false;
								break;
							}
						}
					}
				}
			}

			if (usedReplay || !tileSucceeded)
			{
				usedReplay = true;
				if (!impl_->ClearL2Tile(tile, request.viewportX, request.viewportY,
					request.canvasWidth, request.canvasHeight) ||
					!replayRequestedRangeDirect(tile))
				{
					UnionRectLocal(result.dirty, tileDirty);
					result.path = CompositionRestorePath::Failed;
					impl_->FinishHistoryOperation(
						request.canvasWidth, request.canvasHeight);
					return result;
				}
			}
			UnionRectLocal(result.dirty, tileDirty);
			++result.tileCount;
		}

		impl_->FinishHistoryOperation(request.canvasWidth, request.canvasHeight);
		result.path = usedReplay ? CompositionRestorePath::OrderedTileReplay
			: usedRebuild ? CompositionRestorePath::CompositionRebuild
			: CompositionRestorePath::CompositionCache;
		return result;
	}

	bool InkHistoryGpuCache::PrimeCompositionNode(HistoryCanvasIdentity canvas,
		InkHistoryRasterKey rasterKey, const InkCanvas& documentCanvas,
		const CanvasRuntimeHistory& history, CompositionNodeId node,
		SignedTileCoordinate tile, int canvasWidth, int canvasHeight)
	{
		if (!impl_ || !impl_->renderer || canvas.pageGuid.IsZero() ||
			canvas.device != rasterKey.device || !node.IsValid() ||
			canvasWidth <= 0 || canvasHeight <= 0) return false;
		const RenderItemRange range = history.CompositionTree().NodeItemRange(node);
		const size_t existingEnd = std::min(range.end, history.Items().size());
		if (range.begin >= existingEnd) return false;
		const std::optional<std::vector<CompositionRangePiece>> plan =
			history.CompositionTree().DecomposeRange(range.begin, existingEnd, tile);
		if (!plan || plan->size() != 1 ||
			(*plan)[0].kind != CompositionRangePieceKind::CachedNode ||
			(*plan)[0].node != node) return false;

		Impl::NodeBuildResult built = impl_->GetOrBuildNode(documentCanvas, history,
			canvas, rasterKey, node, tile);
		if (built.succeeded && !built.identity) impl_->UnpinNode(built.plannerKey);
		impl_->FinishHistoryOperation(canvasWidth, canvasHeight);
		return built.succeeded;
	}

	void InkHistoryGpuCache::DiscardCompositionCache() noexcept
	{
		if (!impl_) return;
		impl_->residentNodes.clear();
		impl_->freeCompositionSlots.clear();
		impl_->compositionPages.clear();
		impl_->compositionPlanner = CompositionCachePlanner(impl_->compositionPolicy);
	}
}
