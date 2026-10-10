#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <d3d11.h>
#include <dxgi.h>
#include <wrl/client.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")

namespace
{
	using Microsoft::WRL::ComPtr;
	constexpr UINT kSize = 160;
	struct Point { float x, y, radius, time = 0.0f; };
	struct Constants
	{
		float width = kSize, height = kSize;
		float shape = 0;
		UINT offset = 0;
		float color[4] = { 1, 0, 0, 1 };
		UINT operation = 0;
		float padding[3] = {};
	};
	static_assert(sizeof(Point) == 16 && sizeof(Constants) == 48);
	struct Layer
	{
		ComPtr<ID3D11Texture2D> add, retain;
		ComPtr<ID3D11RenderTargetView> addView, retainView;
		ComPtr<ID3D11ShaderResourceView> addRead, retainRead;
	};
	struct Pixels
	{
		std::vector<std::array<uint8_t, 4>> add;
		std::vector<float> retain;
	};
	void Require(HRESULT result, const char* stage)
	{
		if (FAILED(result)) throw std::runtime_error(std::string(stage) +
			" HRESULT=" + std::to_string(static_cast<unsigned long>(result)));
	}
	float HalfValue(uint16_t value)
	{
		const int exponent = (value >> 10) & 31;
		return exponent == 0 ? std::ldexp(float(value & 1023), -24)
			: std::ldexp(float(1024 + (value & 1023)), exponent - 25);
	}
	class Offscreen
	{
	public:
		ComPtr<ID3D11Device> device;
		ComPtr<ID3D11DeviceContext> context;
		ComPtr<ID3D11VertexShader> vs;
		ComPtr<ID3D11PixelShader> ps;
		ComPtr<ID3D11Buffer> constants, points;
		ComPtr<ID3D11ShaderResourceView> pointsView;
		ComPtr<ID3D11BlendState> blend, resolveBlend;
		ComPtr<ID3D11RasterizerState> raster;
		ComPtr<ID3D11SamplerState> sampler;
		ComPtr<ID3D11Texture2D> addStage, retainStage;
		Layer whole, stable, live, composed;

		Offscreen()
		{
			D3D_FEATURE_LEVEL feature;
			HRESULT result = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr,
				D3D11_CREATE_DEVICE_BGRA_SUPPORT, nullptr, 0, D3D11_SDK_VERSION,
				&device, &feature, &context);
			bool warp = FAILED(result);
			if (warp) result = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr,
				D3D11_CREATE_DEVICE_BGRA_SUPPORT, nullptr, 0, D3D11_SDK_VERSION,
				&device, &feature, &context);
			Require(result, "Create offscreen device");
			ComPtr<IDXGIDevice> dxgi;
			ComPtr<IDXGIAdapter> adapter;
			Require(device.As(&dxgi), "DXGI device");
			Require(dxgi->GetAdapter(&adapter), "DXGI adapter");
			DXGI_ADAPTER_DESC description{};
			Require(adapter->GetDesc(&description), "Adapter description");
			std::wcout << L"[ThinGPU] " << (warp ? L"WARP " : L"Hardware ")
				<< description.Description << std::endl;
			wchar_t executable[MAX_PATH]{};
			GetModuleFileNameW(nullptr, executable, MAX_PATH);
			const auto source = std::filesystem::path(executable).parent_path() /
				L"inkStrokeModelerTest.exe";
			// 仅映射现有构建链的资源，不执行主程序或创建窗口。
			HMODULE resources = LoadLibraryExW(source.c_str(), nullptr,
				LOAD_LIBRARY_AS_DATAFILE | LOAD_LIBRARY_AS_IMAGE_RESOURCE);
			if (!resources) throw std::runtime_error("Shader resource executable missing");
			try
			{
				for (int id : { 101, 102 })
				{
					HRSRC resource = FindResourceW(resources, MAKEINTRESOURCEW(id), L"SHADER");
					HGLOBAL loaded = resource ? LoadResource(resources, resource) : nullptr;
					const void* bytes = loaded ? LockResource(loaded) : nullptr;
					if (!bytes) throw std::runtime_error("Embedded ink shader missing");
					const DWORD size = SizeofResource(resources, resource);
					Require(id == 101 ? device->CreatePixelShader(bytes, size, nullptr, &ps)
						: device->CreateVertexShader(bytes, size, nullptr, &vs), "Create shader");
				}
			}
			catch (...) { FreeLibrary(resources); throw; }
			FreeLibrary(resources);
			D3D11_BUFFER_DESC buffer{};
			buffer.ByteWidth = sizeof(Constants);
			buffer.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
			Require(device->CreateBuffer(&buffer, nullptr, &constants), "Constants");
			buffer.ByteWidth = 64 * sizeof(Point);
			buffer.BindFlags = D3D11_BIND_SHADER_RESOURCE;
			buffer.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
			buffer.StructureByteStride = sizeof(Point);
			Require(device->CreateBuffer(&buffer, nullptr, &points), "Points");
			Require(device->CreateShaderResourceView(points.Get(), nullptr, &pointsView), "Points SRV");
			D3D11_BLEND_DESC blending{};
			blending.IndependentBlendEnable = TRUE;
			for (int i = 0; i != 2; ++i)
			{
				auto& target = blending.RenderTarget[i];
				target.BlendEnable = TRUE;
				target.SrcBlend = target.DestBlend = D3D11_BLEND_ONE;
				target.SrcBlendAlpha = target.DestBlendAlpha = D3D11_BLEND_ONE;
				target.BlendOp = target.BlendOpAlpha = i == 0 ? D3D11_BLEND_OP_MAX : D3D11_BLEND_OP_MIN;
				target.RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
			}
			Require(device->CreateBlendState(&blending, &blend), "MAX/MIN blend");
			D3D11_BLEND_DESC resolve{};
			resolve.RenderTarget[0].BlendEnable = TRUE;
			resolve.RenderTarget[0].SrcBlend = resolve.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ONE;
			resolve.RenderTarget[0].DestBlend = D3D11_BLEND_SRC1_COLOR;
			resolve.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_SRC1_ALPHA;
			resolve.RenderTarget[0].BlendOp = resolve.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
			resolve.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
			Require(device->CreateBlendState(&resolve, &resolveBlend), "Dual source resolve");
			D3D11_RASTERIZER_DESC rasterDesc{};
			rasterDesc.FillMode = D3D11_FILL_SOLID;
			rasterDesc.CullMode = D3D11_CULL_NONE;
			rasterDesc.DepthClipEnable = TRUE;
			Require(device->CreateRasterizerState(&rasterDesc, &raster), "Rasterizer");
			D3D11_SAMPLER_DESC sampling{};
			sampling.Filter = D3D11_FILTER_MIN_MAG_MIP_POINT;
			sampling.AddressU = sampling.AddressV = sampling.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
			sampling.MaxLOD = D3D11_FLOAT32_MAX;
			Require(device->CreateSamplerState(&sampling, &sampler), "Sampler");
			MakeLayer(whole); MakeLayer(stable); MakeLayer(live); MakeLayer(composed);
			D3D11_TEXTURE2D_DESC stage{};
			whole.add->GetDesc(&stage);
			stage.BindFlags = 0;
			stage.Usage = D3D11_USAGE_STAGING;
			stage.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
			Require(device->CreateTexture2D(&stage, nullptr, &addStage), "Add staging");
			stage.Format = DXGI_FORMAT_R16_FLOAT;
			Require(device->CreateTexture2D(&stage, nullptr, &retainStage), "Retain staging");
		}
		void MakeLayer(Layer& layer)
		{
			D3D11_TEXTURE2D_DESC texture{};
			texture.Width = texture.Height = kSize;
			texture.MipLevels = texture.ArraySize = texture.SampleDesc.Count = 1;
			texture.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
			texture.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
			Require(device->CreateTexture2D(&texture, nullptr, &layer.add), "Add texture");
			texture.Format = DXGI_FORMAT_R16_FLOAT;
			Require(device->CreateTexture2D(&texture, nullptr, &layer.retain), "Retain texture");
			Require(device->CreateRenderTargetView(layer.add.Get(), nullptr, &layer.addView), "Add RTV");
			Require(device->CreateRenderTargetView(layer.retain.Get(), nullptr, &layer.retainView), "Retain RTV");
			Require(device->CreateShaderResourceView(layer.add.Get(), nullptr, &layer.addRead), "Add SRV");
			Require(device->CreateShaderResourceView(layer.retain.Get(), nullptr, &layer.retainRead), "Retain SRV");
		}
		void Clear(Layer& layer)
		{
			const float zero[4]{}, one[4]{ 1, 1, 1, 1 };
			context->ClearRenderTargetView(layer.addView.Get(), zero);
			context->ClearRenderTargetView(layer.retainView.Get(), one);
		}
		void Draw(Layer& layer, const std::vector<Point>& data, Constants values = {}, bool resolve = false)
		{
			if (data.size() < 2 || data.size() > 64) throw std::runtime_error("Invalid test points");
			std::array<Point, 64> upload{};
			std::copy(data.begin(), data.end(), upload.begin());
			context->UpdateSubresource(points.Get(), 0, nullptr, upload.data(), 0, 0);
			context->UpdateSubresource(constants.Get(), 0, nullptr, &values, 0, 0);
			ID3D11RenderTargetView* targets[] = { layer.addView.Get(), layer.retainView.Get() };
			context->OMSetRenderTargets(resolve ? 1 : 2, targets, nullptr);
			context->OMSetBlendState(resolve ? resolveBlend.Get() : blend.Get(), nullptr, 0xffffffff);
			context->RSSetState(raster.Get());
			D3D11_VIEWPORT viewport{ 0, 0, float(kSize), float(kSize), 0, 1 };
			context->RSSetViewports(1, &viewport);
			context->IASetInputLayout(nullptr);
			context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
			context->VSSetShader(vs.Get(), nullptr, 0);
			context->PSSetShader(ps.Get(), nullptr, 0);
			ID3D11Buffer* cb = constants.Get();
			context->VSSetConstantBuffers(0, 1, &cb);
			context->PSSetConstantBuffers(0, 1, &cb);
			ID3D11ShaderResourceView* srv = pointsView.Get();
			context->VSSetShaderResources(0, 1, &srv);
			context->Draw(values.shape == 1 ? 6 : UINT(data.size() - 1) * 6, 0);
			context->OMSetRenderTargets(0, nullptr, nullptr);
		}
		Pixels Read(Layer& layer)
		{
			Pixels pixels;
			pixels.add.resize(kSize * kSize);
			pixels.retain.resize(kSize * kSize);
			context->CopyResource(addStage.Get(), layer.add.Get());
			context->CopyResource(retainStage.Get(), layer.retain.Get());
			D3D11_MAPPED_SUBRESOURCE mapped{};
			Require(context->Map(addStage.Get(), 0, D3D11_MAP_READ, 0, &mapped), "Read Add");
			for (UINT y = 0; y < kSize; ++y)
				memcpy(pixels.add.data() + y * kSize,
					static_cast<const uint8_t*>(mapped.pData) + y * mapped.RowPitch, kSize * 4);
			context->Unmap(addStage.Get(), 0);
			Require(context->Map(retainStage.Get(), 0, D3D11_MAP_READ, 0, &mapped), "Read Retain");
			for (UINT y = 0; y < kSize; ++y)
			{
				const auto* row = reinterpret_cast<const uint16_t*>(
					static_cast<const uint8_t*>(mapped.pData) + y * mapped.RowPitch);
				for (UINT x = 0; x < kSize; ++x) pixels.retain[y * kSize + x] = HalfValue(row[x]);
			}
			context->Unmap(retainStage.Get(), 0);
			return pixels;
		}
		Pixels Union(bool resolve = false, float background = 0)
		{
			Clear(composed);
			if (resolve)
			{
				const float color[4]{ background, background, background, 1 };
				context->ClearRenderTargetView(composed.addView.Get(), color);
			}
			ID3D11ShaderResourceView* views[] = { stable.addRead.Get(), stable.retainRead.Get(),
				nullptr, live.addRead.Get(), live.retainRead.Get() };
			context->PSSetShaderResources(1, 5, views);
			ID3D11SamplerState* sample = sampler.Get();
			context->PSSetSamplers(0, 1, &sample);
			Constants values;
			values.shape = 1;
			Draw(composed, { {0, 0, 0}, {kSize, kSize, 0} }, values, resolve);
			ID3D11ShaderResourceView* empty[5]{};
			context->PSSetShaderResources(1, 5, empty);
			return Read(composed);
		}
	};
	void Check(bool condition, const std::string& description, int& failures)
	{
		if (condition) return;
		if (failures < 80) std::cerr << "[ThinGPU] FAILED " << description << std::endl;
		++failures;
	}
	bool Close(const Pixels& a, const Pixels& b, int quantization = 1)
	{
		float maxRetain = 0;
		int maxAdd = 0;
		size_t location = 0;
		for (size_t i = 0; i < a.add.size(); ++i)
		{
			for (int channel = 0; channel < 4; ++channel)
				if (const int delta = std::abs(int(a.add[i][channel]) - int(b.add[i][channel])); delta > maxAdd)
				{ maxAdd = delta; location = i; }
			maxRetain = std::max(maxRetain, std::abs(a.retain[i] - b.retain[i]));
		}
		if (maxAdd > quantization || maxRetain > 0.001f)
		{
			std::cout << "[ThinGPU] difference maxAdd=" << maxAdd << " maxRetain=" << maxRetain
				<< " at=" << location % kSize << "," << location / kSize << " alpha="
				<< int(a.add[location][3]) << "/" << int(b.add[location][3]) << std::endl;
			return false;
		}
		return true;
	}
	float ThinReference(const Point& a, const Point& b, double x, double y)
	{
		// 独立最小化生成圆的距离，不复制 PS 的 cap/side 分类公式。
		auto distance = [&](double t)
		{
			const double cx = a.x + (b.x - a.x) * t;
			const double cy = a.y + (b.y - a.y) * t;
			return std::hypot(x - cx, y - cy) - (a.radius + (b.radius - a.radius) * t);
		};
		double low = 0, high = 1;
		for (int iteration = 0; iteration < 50; ++iteration)
		{
			const double left = (2 * low + high) / 3, right = (low + 2 * high) / 3;
			if (distance(left) < distance(right)) high = right;
			else low = left;
		}
		double t = (low + high) * 0.5;
		if (distance(0) <= distance(t)) t = 0;
		if (distance(1) <= distance(t)) t = 1;
		const double radius = a.radius + (b.radius - a.radius) * t;
		auto coverage = [](double d)
		{
			const double s = std::clamp((d + 0.75) / 1.5, 0.0, 1.0);
			return 1 - s * s * (3 - 2 * s);
		};
		return float(coverage(distance(t)) - coverage(distance(t) + 2 * radius));
	}
	float EqualWidthReference(const Point& a, const Point& b, float x, float y)
	{
		const double dx = b.x - a.x, dy = b.y - a.y;
		const double t = std::clamp(((x - a.x) * dx + (y - a.y) * dy) / (dx * dx + dy * dy), 0.0, 1.0);
		const double d = std::hypot(x - a.x - t * dx, y - a.y - t * dy) - a.radius;
		auto coverage = [](double distance)
		{
			const double s = std::clamp((distance + 0.75) / 1.5, 0.0, 1.0);
			return 1 - s * s * (3 - 2 * s);
		};
		return float(coverage(d) - coverage(d + 2 * a.radius));
	}
}

int RunThinStrokeGpuTests()
{
	int failures = 0;
	try
	{
		Offscreen gpu;
		// Erase 保留旧 fwidth 公式，用真实 PS 的未修改分支记录对称 quad 退化。
		Constants legacy; legacy.operation = 1;
		const std::vector<Point> symmetric{ {20, 81, 0.025f}, {140, 81, 0.025f} };
		gpu.Clear(gpu.stable); gpu.Draw(gpu.stable, symmetric, legacy);
		const auto oldCoverage = gpu.Read(gpu.stable);
		gpu.Clear(gpu.live); gpu.Draw(gpu.live, symmetric);
		const auto newCoverage = gpu.Read(gpu.live);
		int oldGaps = 0, newGaps = 0;
		for (UINT x = 24; x < 136; ++x)
		{
			float oldSum = 0, newSum = 0;
			for (UINT y = 76; y < 86; ++y)
			{
				oldSum += 1 - oldCoverage.retain[y * kSize + x];
				newSum += newCoverage.add[y * kSize + x][3] / 255.0f;
			}
			oldGaps += oldSum == 0; newGaps += newSum == 0;
		}
		std::cout << "[ThinGPU] symmetric 0.05px legacy/new zero sections="
			<< oldGaps << "/" << newGaps << std::endl;
		Check(newGaps == 0, "Symmetric quad fixed coverage", failures);
		int cases = 0;
		for (float width : { 0.0f, 0.05f, 0.1f, 0.25f, 0.5f, 1.0f, 1.5f, 2.0f, 4.0f })
		for (float angle : { 0.0f, -1.0f, 1.0f, -5.0f, 5.0f, -15.0f, 15.0f, 45.0f, 90.0f })
		for (int phaseX = 0; phaseX < 8; ++phaseX)
		for (int phaseY = 0; phaseY < 8; ++phaseY)
		{
			const float radians = angle * 0.017453292519943295f;
			const float dx = std::cos(radians), dy = std::sin(radians);
			const float cx = 80 + phaseX / 8.0f, cy = 80 + phaseY / 8.0f;
			const Point a{ cx - dx * 60, cy - dy * 60, width * 0.5f };
			const Point b{ cx + dx * 60, cy + dy * 60, width * 0.5f };
			gpu.Clear(gpu.whole); gpu.Draw(gpu.whole, { a, b });
			const auto pixels = gpu.Read(gpu.whole);
			const bool transition = width > 1 && width < 2;
			Pixels legacyPixels;
			const float transitionT = std::clamp(width - 1, 0.0f, 1.0f);
			const float legacyWeight = transitionT * transitionT * (3 - 2 * transitionT);
			if (transition)
			{
				gpu.Clear(gpu.stable); gpu.Draw(gpu.stable, { a, b }, legacy);
				legacyPixels = gpu.Read(gpu.stable);
			}
			const std::string label = "width=" + std::to_string(width) + " angle=" +
				std::to_string(angle) + " phase=" + std::to_string(phaseX) + "," + std::to_string(phaseY);
			bool consistent = true, continuous = true;
			double sum = 0, expectedSum = 0, legacySum = 0;
			int sections = 0;
			const bool xMajor = std::abs(dx) >= std::abs(dy);
			const float major = xMajor ? std::abs(dx) : std::abs(dy);
			for (UINT m = 0; m < kSize; ++m)
			{
				if (std::abs((m + 0.5f - (xMajor ? cx : cy)) / major) > 48) continue;
				double section = 0, expectedSection = 0, legacySection = 0;
				for (UINT n = 0; n < kSize; ++n)
				{
					const size_t i = xMajor ? n * kSize + m : m * kSize + n;
					const auto& p = pixels.add[i];
					section += p[3] / 255.0;
					if (transition)
					{
						const float oldAlpha = 1 - legacyPixels.retain[i];
						const float reference = EqualWidthReference(a, b,
							(xMajor ? m : n) + 0.5f, (xMajor ? n : m) + 0.5f);
						expectedSection += legacyWeight * oldAlpha + (1 - legacyWeight) * reference;
						legacySection += oldAlpha;
					}
					consistent &= p[0] == 0 && p[1] == 0 && p[2] == p[3] &&
						std::abs(p[3] / 255.0f + pixels.retain[i] - 1) <= 0.0025f;
				}
				continuous &= width == 0 ? section == 0 : section > 0;
				sum += section * major;
				expectedSum += expectedSection * major;
				legacySum += legacySection * major;
				++sections;
			}
			Check(continuous, "Longitudinal coverage " + label, failures);
			Check(consistent, "Premultiplied red / Retain " + label, failures);
			// 1.5px 核的离散相位误差约 11.2%；加支持像素的每像素半量化阶误差。
			const double supported = std::ceil((width + 3.0) / major) + 1;
			const double tolerance = width * 0.125 + supported * major / 510.0 + 0.001;
			if (transition)
			{
				// 过渡带保留旧 AA 相位误差；按实测旧覆盖与独立固定核逐像素验算，而非放宽名义宽度误差。
				const double blendTolerance = supported * major *
					(0.5 / 255 + legacyWeight / 4096 + 0.0002);
				Check(std::abs(sum - expectedSum) / sections <= blendTolerance,
					"Integrated specified transition blend " + label, failures);
				Check(std::abs(sum / sections - width) <=
					std::abs(legacySum / sections - width) + blendTolerance,
					"Transition width no worse than measured legacy " + label, failures);
				if ((angle == 0 && phaseX == 0 && phaseY == 7) ||
					(angle == 90 && phaseX == 7 && phaseY == 0))
					std::cout << "[ThinGPU] transition legacy/new/oracle width=" << legacySum / sections
						<< "/" << sum / sections << "/" << expectedSum / sections << " " << label << std::endl;
			}
			else
				Check(std::abs(sum / sections - width) <= tolerance,
					"Integrated width=" + std::to_string(sum / sections) + " " + label, failures);
			++cases;
		}
		// 分段、重复覆盖和真实 shape=1 合成须保持 MAX/MIN 幂等。
		for (float radius : { 0.025f, 0.125f, 0.5f, 0.75f, 1.0f, 2.0f })
		for (float slope : { 0.0f, 0.017f, -0.09f, 0.25f, 1.0f })
		{
			const std::string label = " radius=" + std::to_string(radius) + " slope=" + std::to_string(slope);
			Point a{ 20.125f, 30.375f, radius }, b{ 120.125f, 30.375f + slope * 100, radius };
			Point middle{ (a.x + b.x) * 0.5f, (a.y + b.y) * 0.5f, radius };
			gpu.Clear(gpu.whole); gpu.Draw(gpu.whole, { a, b });
			const auto whole = gpu.Read(gpu.whole);
			gpu.Clear(gpu.stable); gpu.Draw(gpu.stable, { a, middle, b });
			const auto segmented = gpu.Read(gpu.stable);
			if (radius <= 0.5f)
				Check(Close(whole, segmented), "Collinear thin segmentation" + label, failures);
			else
			{
				// 粗线必须保留旧导数 AA；逐个图元测量其原有值，不放宽统一误差门槛。
				gpu.Clear(gpu.stable); gpu.Draw(gpu.stable, { a, b }, legacy);
				const auto oldWhole = gpu.Read(gpu.stable);
				gpu.Clear(gpu.stable); gpu.Draw(gpu.stable, { a, middle }, legacy);
				const auto oldLeft = gpu.Read(gpu.stable);
				gpu.Clear(gpu.stable); gpu.Draw(gpu.stable, { middle, b }, legacy);
				const auto oldRight = gpu.Read(gpu.stable);
				const float t = std::clamp((radius - 0.5f) / 0.5f, 0.0f, 1.0f);
				const float weight = t * t * (3 - 2 * t);
				bool expectedBlend = true;
				float legacyDifference = 0, actualDifference = 0;
				for (UINT y = 0; y < kSize; ++y)
				for (UINT x = 0; x < kSize; ++x)
				{
					const size_t i = y * kSize + x;
					auto expected = [&](const Point& start, const Point& end, float retain)
					{
						return weight * (1 - retain) + (1 - weight) *
							EqualWidthReference(start, end, x + 0.5f, y + 0.5f);
					};
					const float expectedWhole = expected(a, b, oldWhole.retain[i]);
					const float expectedSegmented = std::max(expected(a, middle, oldLeft.retain[i]),
						expected(middle, b, oldRight.retain[i]));
					// BGRA8 半阶 + R16 旧覆盖半阶 + 单精度距离舍入，逐像素独立验算。
					const float tolerance = 0.5f / 255 + weight / 2048 + 0.0002f;
					expectedBlend &= std::abs(whole.add[i][3] / 255.0f - expectedWhole) <= tolerance &&
						std::abs(segmented.add[i][3] / 255.0f - expectedSegmented) <= tolerance;
					legacyDifference = std::max(legacyDifference, std::abs(oldWhole.retain[i] -
						std::min(oldLeft.retain[i], oldRight.retain[i])));
					actualDifference = std::max(actualDifference,
						std::abs(whole.retain[i] - segmented.retain[i]));
				}
				std::cout << "[ThinGPU] legacy/actual segmentation delta=" << legacyDifference
					<< "/" << actualDifference << " blend=" << weight << label << std::endl;
				Check(expectedBlend, "Preserved legacy / specified transition blend" + label, failures);
			}
			gpu.Draw(gpu.whole, { a, b });
			Check(Close(whole, gpu.Read(gpu.whole), 0), "Repeated MAX/MIN idempotency" + label, failures);
			gpu.Clear(gpu.stable); gpu.Draw(gpu.stable, { a, middle });
			gpu.Clear(gpu.live); gpu.Draw(gpu.live, { middle, b });
			Check(Close(segmented, gpu.Union()), "L0/L1 equals same segmented geometry" + label, failures);
		}
		// 退化圆、包含圆、变半径和圆帽：反向参数化与分支选择应一致。
		for (const auto& pair : std::vector<std::array<Point, 2>>{
			{{{70.125f, 70.375f, 0}, {70.125f, 70.375f, 0}}},
			{{{70.125f, 70.375f, 0.05f}, {70.125f, 70.375f, 0.25f}}},
			{{{70.125f, 70.375f, 0.05f}, {70.25f, 70.375f, 0.5f}}},
			{{{20.125f, 60.375f, 0.025f}, {130.125f, 70.375f, 0.45f}}},
			{{{20.125f, 60.375f, 0.25f}, {130.125f, 70.375f, 1.5f}}}})
		{
			gpu.Clear(gpu.whole); gpu.Draw(gpu.whole, { pair[0], pair[1] });
			const auto red = gpu.Read(gpu.whole);
			if (std::max(pair[0].radius, pair[1].radius) <= 0.5f)
			{
				bool referenceMatches = true;
				for (UINT y = 0; y < kSize; ++y)
				for (UINT x = 0; x < kSize; ++x)
				{
					const float expected = ThinReference(pair[0], pair[1], x + 0.5, y + 0.5);
					referenceMatches &= std::abs(red.add[y * kSize + x][3] / 255.0f - expected) <=
						0.5f / 255.0f + 0.0002f;
				}
				Check(referenceMatches, "Nearest generating-circle numerical oracle", failures);
			}
			gpu.Clear(gpu.stable); gpu.Draw(gpu.stable, { pair[1], pair[0] });
			Check(Close(red, gpu.Read(gpu.stable)), "Caps / varying radius reversal", failures);
			Constants black; black.color[0] = 0;
			gpu.Clear(gpu.live); gpu.Draw(gpu.live, { pair[0], pair[1] }, black);
			const auto dark = gpu.Read(gpu.live);
			bool colorCorrect = true;
			for (size_t i = 0; i < red.add.size(); ++i)
			{
				// 白底黑线=Retain，黑底红线=Add；覆盖不应依赖笔色或背景。
				colorCorrect &= dark.add[i][0] == 0 && dark.add[i][1] == 0 && dark.add[i][2] == 0 &&
					dark.add[i][3] == red.add[i][3] && dark.retain[i] == red.retain[i] &&
					std::abs(red.add[i][2] / 255.0f + dark.retain[i] - 1) <= 0.0025f;
			}
			Check(colorCorrect, "Black/white background affine composition", failures);
			gpu.Clear(gpu.stable);
			const auto blackOnWhite = gpu.Union(true, 1);
			gpu.Clear(gpu.live); gpu.Draw(gpu.live, { pair[0], pair[1] });
			const auto redOnBlack = gpu.Union(true, 0);
			bool resolveCorrect = true;
			for (size_t i = 0; i < red.add.size(); ++i)
			{
				resolveCorrect &= blackOnWhite.add[i][3] >= 254 && redOnBlack.add[i][3] >= 254 &&
					redOnBlack.add[i][0] == 0 && redOnBlack.add[i][1] == 0 &&
					std::abs(int(redOnBlack.add[i][2]) - int(red.add[i][2])) <= 1;
				for (int c = 0; c < 3; ++c)
					resolveCorrect &= std::abs(blackOnWhite.add[i][c] / 255.0f - dark.retain[i]) <= 0.0025f;
			}
			Check(resolveCorrect, "Actual dual-source black/white resolve", failures);
		}
		std::cout << "[ThinGPU] " << cases << " width/angle/XY-phase cases; failures=" << failures << std::endl;
	}
	catch (const std::exception& error)
	{
		std::cerr << "[ThinGPU] " << error.what() << std::endl;
		++failures;
	}
	return failures;
}
