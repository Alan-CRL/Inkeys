module;

#include "../../../IdtMain.h"

#include <d2d1_1.h>
#include <wrl/client.h>

#define STBI_ONLY_PNG
#define STBI_NO_STDIO
#define STB_IMAGE_IMPLEMENTATION
#include "../../../additional/stbimage/stb_image.h"

#define LUNASVG_BUILD_STATIC
#include <lunasvg/lunasvg.h>
#pragma comment(lib, "lunasvg.lib")
#pragma comment(lib, "plutovg.lib")

module Inkeys.UI.Bar;
import :UI;

import Inkeys.Load;
import Inkeys.Conv.Text;

/// 继承
//// 位置继承
BarUiInheritClass::BarUiInheritClass(double xT, double yT)
{
	x = xT;
	y = yT;
}
BarUiInheritClass::BarUiInheritClass(BarUiInheritEnum typeT, double xO, double yO, double wO, double hO, double xT, double yT, double wT, double hT)
{
	// w/h 为控件自身的宽高 -> 最终得出的都是左上角绘制坐标 -> 方便绘制
	// O 为当前项 T 为目标继承项

	// 继承类型
	type = typeT;

	// 基础位置
	x = xO;
	y = yO;
	// 先设置的位置为控件所设置的偏移量（也就是相对于控件中心的偏移量），接下来的计算中，以 Center 为例
	// (xT + wT / 2.0, yT + hT / 2.0) 是目标控件的中心点位置，然后在减去控件自身的宽高的一半，得到左上角的绘制坐标

	// TODO 拓展更多类型组合
	if (type == BarUiInheritEnum::TopLeft) { x += xT, y += yT; }
	else if (type == BarUiInheritEnum::Top) { x += xT + wT / 2.0 - wO / 2.0, y += yT; }
	else if (type == BarUiInheritEnum::CenterFromTopLeft) { x += xT - wO / 2.0, y += yT - hO / 2.0; }
	else if (type == BarUiInheritEnum::Left) { x += xT, y += yT + hT / 2.0 - hO / 2.0; }
	else if (type == BarUiInheritEnum::Center) { x += xT + wT / 2.0 - wO / 2.0, y += yT + hT / 2.0 - hO / 2.0; }
	else if (type == BarUiInheritEnum::Right) { x += xT + wT - wO, y += yT + hT / 2.0 - hO / 2.0; }

	else if (type == BarUiInheritEnum::ToTop) { x += xT + wT / 2.0 - wO / 2.0, y += yT - hO; }
	else if (type == BarUiInheritEnum::ToLeft) { x += xT - wO, y += yT + hT / 2.0 - hO / 2.0; }
	else if (type == BarUiInheritEnum::ToRight) { x += xT + wT, y += yT + hT / 2.0 - hO / 2.0; }
	else if (type == BarUiInheritEnum::ToBottom) { x += xT + wT / 2.0 - wO / 2.0, y += yT + hT; }
}

/// 控件
//// 单个形状控件
BarUiShapeClass::BarUiShapeClass(double xT, double yT, double wT, double hT, optional<double> rwT, optional<double> rhT, optional<double> ftT, optional<COLORREF>fillT, optional<COLORREF>frameT, BarUiValueModeEnum type)
{
	x.Initialization(xT, type);
	y.Initialization(yT, type);
	w.Initialization(wT, type);
	h.Initialization(hT, type);
	if (rwT.has_value()) { rw = BarUiValueClass(); rw.value().Initialization(rwT.value(), type); }
	if (rhT.has_value()) { rh = BarUiValueClass(); rh.value().Initialization(rhT.value(), type); }
	if (ftT.has_value()) { ft = BarUiValueClass(); ft.value().Initialization(ftT.value(), type); }
	if (fillT.has_value()) { fill = BarUiColorClass(); fill.value().Initialization(fillT.value()); }
	if (frameT.has_value()) { frame = BarUiColorClass(); frame.value().Initialization(frameT.value()); }
}
void BarUiShapeClass::Initialization(double xT, double yT, double wT, double hT, optional<double> rwT, optional<double> rhT, optional<double> ftT, optional<COLORREF>fillT, optional<COLORREF>frameT, BarUiValueModeEnum type)
{
	x.Initialization(xT, type);
	y.Initialization(yT, type);
	w.Initialization(wT, type);
	h.Initialization(hT, type);
	if (rwT.has_value()) { rw = BarUiValueClass(); rw.value().Initialization(rwT.value(), type); }
	if (rhT.has_value()) { rh = BarUiValueClass(); rh.value().Initialization(rhT.value(), type); }
	if (ftT.has_value()) { ft = BarUiValueClass(); ft.value().Initialization(ftT.value(), type); }
	if (fillT.has_value()) { fill = BarUiColorClass(); fill.value().Initialization(fillT.value()); }
	if (frameT.has_value()) { frame = BarUiColorClass(); frame.value().Initialization(frameT.value()); }
}
bool BarUiRoundedRectContainsPoint(int mx, int my, double zoom,
	double leftDip, double topDip, double widthDip, double heightDip,
	double radiusXDip, double radiusYDip, double epsilon) noexcept
{
	if (widthDip <= 0.0 || heightDip <= 0.0
		|| radiusXDip < 0.0 || radiusYDip < 0.0) return false;

	double xO = leftDip * zoom;
	double yO = topDip * zoom;

	double rx = clamp(radiusXDip * zoom, 0.0, (widthDip * zoom) / 2.0);
	double ry = clamp(radiusYDip * zoom, 0.0, (heightDip * zoom) / 2.0);

	// 矩形区域内才有可能
	if (static_cast<double>(mx) < xO - epsilon || static_cast<double>(mx) > xO + (widthDip * zoom) + epsilon ||
		static_cast<double>(my) < yO - epsilon || static_cast<double>(my) > yO + (heightDip * zoom) + epsilon)
		return false;

	// “内矩形”范围
	double ix0 = xO + rx;         // 内部矩形左
	double ix1 = xO + (widthDip * zoom) - rx; // 内部矩形右
	double iy0 = yO + ry;         // 上
	double iy1 = yO + heightDip * zoom - ry; // 下

	// 若点在内矩形，直接返回
	if (static_cast<double>(mx) >= ix0 - epsilon && static_cast<double>(mx) <= ix1 + epsilon &&
		static_cast<double>(my) >= iy0 - epsilon && static_cast<double>(my) <= iy1 + epsilon)
		return true;

	// 否则一定在四角矩形外或圆角四象限内，枚举距离最近的圆角中心
	// Clamp到最近的角
	double cx = (static_cast<double>(mx) < ix0) ? ix0 : ((static_cast<double>(mx) > ix1) ? ix1 : static_cast<double>(mx));
	double cy = (static_cast<double>(my) < iy0) ? iy0 : ((static_cast<double>(my) > iy1) ? iy1 : static_cast<double>(my));

	// 对应的圆角中心
	// 只有在圆角四象限判定，否则前面矩形部分已经返回true
	double dx = static_cast<double>(mx) - cx;
	double dy = static_cast<double>(my) - cy;

	// 椭圆(中心0,0, 半径rx,ry)上的判定
	// (dx/rx)^2 + (dy/ry)^2 <= 1

	if (rx > 0 && ry > 0)
	{
		double ellipseVal = (dx * dx) / (rx * rx) + (dy * dy) / (ry * ry);
		return ellipseVal <= 1.0 + epsilon;
	}
	// 若rx或ry为0，则为直角矩形，已在前面矩形段判断过
	return false;
}

bool BarUiShapeClass::IsClick(int mx, int my, double zoom, double epsilon)
{
	const double width = static_cast<double>(w.val);
	const double height = static_cast<double>(h.val);
	const double radiusX = rw.has_value()
		? static_cast<double>(rw->val) : 0.0;
	const double radiusY = rh.has_value()
		? static_cast<double>(rh->val) : 0.0;
	return BarUiRoundedRectContainsPoint(mx, my, zoom,
		inhX, inhY, width, height, radiusX, radiusY, epsilon);
}

//// 单个超椭圆控件
BarUiSuperellipseClass::BarUiSuperellipseClass(double xT, double yT, double wT, double hT, optional<double> nT, optional<double> ftT, optional<COLORREF>fillT, optional<COLORREF>frameT, BarUiValueModeEnum type)
{
	x.Initialization(xT, type);
	y.Initialization(yT, type);
	w.Initialization(wT, type);
	h.Initialization(hT, type);
	if (nT.has_value()) { n = BarUiValueClass(); n.value().Initialization(nT.value(), type); }
	if (ftT.has_value()) { ft = BarUiValueClass(); ft.value().Initialization(ftT.value(), type); }
	if (fillT.has_value()) { fill = BarUiColorClass(); fill.value().Initialization(fillT.value()); }
	if (frameT.has_value()) { frame = BarUiColorClass(); frame.value().Initialization(frameT.value()); }
}
void BarUiSuperellipseClass::Initialization(double xT, double yT, double wT, double hT, optional<double> nT, optional<double> ftT, optional<COLORREF>fillT, optional<COLORREF>frameT, BarUiValueModeEnum type)
{
	x.Initialization(xT, type);
	y.Initialization(yT, type);
	w.Initialization(wT, type);
	h.Initialization(hT, type);
	if (nT.has_value()) { n = BarUiValueClass(); n.value().Initialization(nT.value(), type); }
	if (ftT.has_value()) { ft = BarUiValueClass(); ft.value().Initialization(ftT.value(), type); }
	if (fillT.has_value()) { fill = BarUiColorClass(); fill.value().Initialization(fillT.value()); }
	if (frameT.has_value()) { frame = BarUiColorClass(); frame.value().Initialization(frameT.value()); }
}
bool BarUiSuperellipseClass::IsClick(int mx, int my, double zoom, double epsilon)
{
	double left = inhX * zoom;
	double top = inhY * zoom;
	double right = left + w.val * zoom;
	double bottom = top + h.val * zoom;
	double radius = min(right - left, bottom - top) / 2.0;
	if (radius <= 0.0 || (!n.has_value() || n.value().val <= 0.0)) return false;

	double px = static_cast<double>(mx);
	double py = static_cast<double>(my);
	if (px < left - epsilon || px > right + epsilon || py < top - epsilon || py > bottom + epsilon) return false;

	// 与绘制一致：非正方形只延长直边，命中区域的四角仍按等宽高超椭圆计算。
	double cx = clamp(px, left + radius, right - radius);
	double cy = clamp(py, top + radius, bottom - radius);
	double normx = (px - cx) / radius;
	double normy = (py - cy) / radius;
	double val = pow(abs(normx), n.value().val) + pow(abs(normy), n.value().val);

	// 内/边判定
	if (epsilon > 0.0) return val <= 1.0 + epsilon;
	return val <= 1.0;
}

//// 单个 SVG 控件
BarUiSVGClass::BarUiSVGClass(double xT, double yT, optional<COLORREF> color1T, optional<COLORREF> color2T, BarUiValueModeEnum type)
{
	x.Initialization(xT, type);
	y.Initialization(yT, type);
	angle.Initialization(0.0, type);
	if (color1T.has_value()) { color1 = BarUiColorClass(); color1.value().Initialization(color1T.value()); }
	if (color2T.has_value()) { color2 = BarUiColorClass(); color2.value().Initialization(color2T.value()); }
}
void BarUiSVGClass::Initialization(double xT, double yT, optional<COLORREF> color1T, optional<COLORREF> color2T, BarUiValueModeEnum type)
{
	x.Initialization(xT, type);
	y.Initialization(yT, type);
	angle.Initialization(0.0, type);
	if (color1T.has_value()) { color1 = BarUiColorClass(); color1.value().Initialization(color1T.value()); }
	if (color2T.has_value()) { color2 = BarUiColorClass(); color2.value().Initialization(color2T.value()); }
}
void BarUiSVGClass::InitializationFromString(wstring valT)
{
	CancelContentTransition();
	transitionSvg.Initialization(valT);
	ApplyContentDirect(valT);
}
void BarUiSVGClass::InitializationFromResource(const wstring& resType, const wstring& resName)
{
	string valT;
	Inkeys::Load::ExtractResourceString(valT, resType, resName);
	if (!valT.empty()) InitializationFromString(utf8ToUtf16(valT));
}
void BarUiSVGClass::SetTarFromString(wstring valT)
{
	CancelContentTransition();
	transitionSvg.Initialization(valT);
	svg.SetTar(valT);
	ResetCache();

	auto temp = CalcWH();
	rW = temp.first, rH = temp.second;
}
void BarUiSVGClass::SetTarFromResource(const wstring& resType, const wstring& resName)
{
	string valT;
	Inkeys::Load::ExtractResourceString(valT, resType, resName);
	if (!valT.empty()) SetTarFromString(utf8ToUtf16(valT));
}
bool BarUiSVGClass::TransitionToString(const wstring& valT, optional<double> durT,
	double keyframeProgressT, double middleScaleT)
{
	if (valT.empty()) return false;
	double duration = durT.has_value() ? durT.value() : static_cast<double>(BarUiDefaultOperationDur);
	return contentTransitionTimeline.Transaction(
		[&](BarUiKeyframeTimelineClass::LockedView& timeline)
		{
			if (timeline.IsActive() && transitionSvg.GetTar() == valT) return false;
			if (!timeline.IsActive() && svg.GetVal() == valT && svg.GetTar() == valT)
			{
				transitionSvg.Initialization(valT);
				return false;
			}

			// 目标、起始视觉值与新代次必须在同一事务内发布。
			transitionSvg.SetTar(valT);
			contentTransitionStartScale = max(0.0, static_cast<double>(contentScale));
			contentTransitionStartPct = clamp(static_cast<double>(contentPct), 0.0, 1.0);
			contentTransitionMiddleScale = isfinite(middleScaleT)
				? max(0.0, middleScaleT) : 0.8;
			contentTransitionKeyframeProgress = isfinite(keyframeProgressT)
				? clamp(keyframeProgressT, 0.0, 1.0) : 0.5;
			timeline.Start(duration, contentTransitionKeyframeProgress);
			return true;
		});
}
bool BarUiSVGClass::TransitionToResource(const wstring& resType, const wstring& resName,
	optional<double> durT, double keyframeProgressT, double middleScaleT)
{
	string valT;
	Inkeys::Load::ExtractResourceString(valT, resType, resName);
	if (valT.empty()) return false;
	return TransitionToString(utf8ToUtf16(valT), durT, keyframeProgressT, middleScaleT);
}
bool BarUiSVGClass::AdvanceContentTransition(double dt, double speedRate)
{
	struct TransitionSnapshot
	{
		BarUiKeyframeTimelineResultClass result;
		wstring target;
		double startScale = 1.0;
		double startPct = 1.0;
		double middleScale = 0.8;
		double keyframe = 0.5;
		bool applyContent = false;
	};
	optional<TransitionSnapshot> snapshot = contentTransitionTimeline.Transaction(
		[&](BarUiKeyframeTimelineClass::LockedView& timeline)
			-> optional<TransitionSnapshot>
		{
			if (!timeline.IsActive()) return nullopt;
			TransitionSnapshot value;
			value.result = timeline.Advance(dt, speedRate);
			value.target = transitionSvg.GetTar();
			value.startScale = contentTransitionStartScale;
			value.startPct = contentTransitionStartPct;
			value.middleScale = max(0.0, contentTransitionMiddleScale);
			value.keyframe = clamp(contentTransitionKeyframeProgress, 0.0, 1.0);
			value.applyContent = value.result.reachedKeyframe
				|| (value.result.finished
					&& (svg.GetVal() != value.target || svg.GetTar() != value.target));
			return value;
		});
	if (!snapshot.has_value()) return false;

	double progress = clamp(snapshot->result.progress, 0.0, 1.0);
	double nextScale = 1.0;
	double nextPct = 1.0;
	if (progress <= snapshot->keyframe)
	{
		double localProgress = snapshot->keyframe > 0.000001
			? progress / snapshot->keyframe : 1.0;
		nextScale = snapshot->startScale
			+ (snapshot->middleScale - snapshot->startScale)
			* BarUiApplyCurve(BarUiCurveEnum::EaseInCubic, localProgress);
		nextPct = snapshot->startPct
			* (1.0 - BarUiApplyCurve(BarUiCurveEnum::EaseInSine, localProgress));
	}
	else
	{
		double localProgress = 1.0 - snapshot->keyframe > 0.000001
			? (progress - snapshot->keyframe) / (1.0 - snapshot->keyframe) : 1.0;
		nextScale = snapshot->middleScale + (1.0 - snapshot->middleScale)
			* BarUiApplyCurve(BarUiCurveEnum::EaseOutBack, localProgress);
		nextPct = BarUiApplyCurve(BarUiCurveEnum::EaseOutSine, localProgress);
	}

	pair<double, double> targetSize = { 0.0, 0.0 };
	if (snapshot->applyContent)
	{
		// SVG 解析可能较重，先对快照目标解析，再凭代次提交短状态。
		auto document = lunasvg::Document::loadFromData(utf16ToUtf8(snapshot->target));
		if (document)
			targetSize = { static_cast<double>(document->width()),
				static_cast<double>(document->height()) };
	}

	bool contentCommitted = false;
	contentTransitionTimeline.Transaction(
		[&](BarUiKeyframeTimelineClass::LockedView& timeline)
		{
			if (!timeline.IsCurrentGeneration(snapshot->result.generation)) return;
			contentScale = isfinite(nextScale) ? max(0.0, nextScale) : 1.0;
			contentPct = isfinite(nextPct) ? clamp(nextPct, 0.0, 1.0) : 1.0;

			if (snapshot->result.reachedKeyframe)
			{
				contentScale = snapshot->middleScale;
				contentPct = 0.0;
			}
			if (snapshot->applyContent)
			{
				svg.Initialization(snapshot->target);
				rW = targetSize.first;
				rH = targetSize.second;
				contentCommitted = true;
			}
			if (snapshot->result.finished)
			{
				contentScale = 1.0;
				contentPct = 1.0;
			}
		});
	if (contentCommitted)
	{
		// cache 只由渲染线程持有，不扩大时间线锁的临界区。
		ResetCache();
	}
	return true;
}
void BarUiSVGClass::CancelContentTransition()
{
	contentTransitionTimeline.Transaction(
		[&](BarUiKeyframeTimelineClass::LockedView& timeline)
		{
			timeline.Cancel();
			transitionSvg.Initialization(svg.GetTar());
			contentScale = 1.0;
			contentPct = 1.0;
			contentTransitionStartScale = 1.0;
			contentTransitionStartPct = 1.0;
			contentTransitionMiddleScale = 0.8;
			contentTransitionKeyframeProgress = 0.5;
		});
}
void BarUiSVGClass::ResetCache()
{
	cacheBitmap.Reset();
	cW = cH = 0.0;
	cColor1 = cColor2 = RGB(0, 0, 0);
}
void BarUiSVGClass::ApplyContentDirect(const wstring& valT)
{
	svg.Initialization(valT);
	ResetCache();
	auto temp = CalcWH();
	rW = temp.first, rH = temp.second;
}
bool BarUiSVGClass::CacheBitmap(ID2D1DeviceContext* deviceContext, double tarW, double tarH)
{
	// 初始化解析
	string svgContent;
	unique_ptr<lunasvg::Document> document;
	{
		svgContent = utf16ToUtf8(svg.GetVal());
		// 替换颜色，如果有
		if (color1.has_value() || color2.has_value())
		{
			auto SvgReplaceColor = [](const string& input, const optional<BarUiColorClass>& color1, const optional<BarUiColorClass>& color2) -> string
				{
					auto colorref_to_rgb = [](COLORREF c) -> string
						{
							int r = GetRValue(c);
							int g = GetGValue(c);
							int b = GetBValue(c);

							return "rgb(" + to_string(r) + "," + to_string(g) + "," + to_string(b) + ")";
						};
					COLORREF col1;
					COLORREF col2;

					string result = input;
					if (color1.has_value())
					{
						col1 = color1.value().val;

						size_t pos = 0;
						const string tag = "rgba(10,0,7,0)";
						const string rgb_str = colorref_to_rgb(col1);
						while ((pos = result.find(tag, pos)) != string::npos)
						{
							result.replace(pos, tag.length(), rgb_str);
							pos += rgb_str.length();
						}
					}
					if (color2.has_value())
					{
						col2 = color2.value().val;

						size_t pos = 0;
						const string tag = "rgba(9,0,2,0)";
						const string rgb_str = colorref_to_rgb(col2);
						while ((pos = result.find(tag, pos)) != string::npos)
						{
							result.replace(pos, tag.length(), rgb_str);
							pos += rgb_str.length();
						}
					}

					return result;
				};

			svgContent = SvgReplaceColor(svgContent, color1, color2);
		}

		// 解析SVG
		document = lunasvg::Document::loadFromData(svgContent);
		if (!document) return false; // 解析失败
	}

	// 绘制到离屏位图
	lunasvg::Bitmap bitmap = document->renderToBitmap(static_cast<int>(tarW), static_cast<int>(tarH));
	{
		if (bitmap.width() == 0 || bitmap.height() == 0 || !bitmap.data()) return false;

		D2D1_BITMAP_PROPERTIES props = D2D1::BitmapProperties(D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED));
		Microsoft::WRL::ComPtr<ID2D1Bitmap> newBitmap;
		// lunasvg 文档声明：数据为BGRA，8bits每通道，正好适配D2D位图
		HRESULT hr = deviceContext->CreateBitmap(
			D2D1::SizeU(bitmap.width(), bitmap.height()),
			bitmap.data(),
			bitmap.width() * 4, // stride
			props,
			newBitmap.GetAddressOf());

		if (FAILED(hr) || !newBitmap) return false;

		cacheBitmap = newBitmap;
	}

	// 记录缓存值
	{
		cW = tarW, cH = tarH;
		if (color1.has_value()) cColor1 = color1.value().val;
		if (color2.has_value()) cColor2 = color2.value().val;
	}

	return true;
}
bool BarUiSVGClass::SetWH(optional<double> wT, optional<double> hT)
{
	double tarW, tarH;

	if (wT.has_value() && hT.has_value()) { tarW = wT.value(), tarH = hT.value(); }
	else
	{
		if (rW <= 0 || rH <= 0) return false; // 尺寸失败

		if (wT.has_value() && !hT.has_value())
		{
			// 高度自动
			tarW = wT.value();
			tarH = rH * (wT.value() / rW);
		}
		else if (!wT.has_value() && hT.has_value())
		{
			// 宽度自动
			tarW = rW * (hT.value() / rH);
			tarH = hT.value();
		}
		else
		{
			// 原尺寸
			tarW = rW;
			tarH = rH;
		}
	}

	w.SetTar(tarW);
	h.SetTar(tarH);

	return true;
}
pair<double, double> BarUiSVGClass::CalcWH()
{
	// 解析SVG
	unique_ptr<lunasvg::Document> document = lunasvg::Document::loadFromData(utf16ToUtf8(svg.GetTar()));
	if (!document) return make_pair(0, 0); // 解析失败

	double w = static_cast<double>(document->width());
	double h = static_cast<double>(document->height());
	return make_pair(w, h);
}

//// 单个 PNG 控件
BarUiPNGClass::BarUiPNGClass(double xT, double yT, BarUiValueModeEnum type)
{
	Initialization(xT, yT, type);
}
void BarUiPNGClass::Initialization(double xT, double yT, BarUiValueModeEnum type)
{
	x.Initialization(xT, type);
	y.Initialization(yT, type);
	angle.Initialization(0.0, type);
}
bool BarUiPNGClass::InitializationFromMemory(const void* data, size_t size)
{
	if (!data || size == 0 || size > static_cast<size_t>(INT_MAX)) return false;

	int width = 0, height = 0, channels = 0;
	stbi_uc* decoded = stbi_load_from_memory(
		static_cast<const stbi_uc*>(data), static_cast<int>(size),
		&width, &height, &channels, STBI_rgb_alpha);
	if (!decoded || width <= 0 || height <= 0)
	{
		if (decoded) stbi_image_free(decoded);
		return false;
	}

	const size_t maxBufferSize = (numeric_limits<size_t>::max)();
	if (static_cast<size_t>(width) > maxBufferSize / static_cast<size_t>(height))
	{
		stbi_image_free(decoded);
		return false;
	}
	const size_t pixelCount = static_cast<size_t>(width) * static_cast<size_t>(height);
	if (pixelCount > maxBufferSize / 4
		|| static_cast<UINT32>(width) > (numeric_limits<UINT32>::max)() / 4)
	{
		stbi_image_free(decoded);
		return false;
	}

	vector<unsigned char> nextPixels(pixelCount * 4);
	for (size_t i = 0; i < pixelCount; i++)
	{
		const unsigned int alpha = decoded[i * 4 + 3];
		// stb 输出直通 RGBA；D2D 分层窗口使用预乘 BGRA，上传前一次性转换。
		nextPixels[i * 4 + 0] = static_cast<unsigned char>(
			(decoded[i * 4 + 2] * alpha + 127) / 255);
		nextPixels[i * 4 + 1] = static_cast<unsigned char>(
			(decoded[i * 4 + 1] * alpha + 127) / 255);
		nextPixels[i * 4 + 2] = static_cast<unsigned char>(
			(decoded[i * 4 + 0] * alpha + 127) / 255);
		nextPixels[i * 4 + 3] = static_cast<unsigned char>(alpha);
	}
	stbi_image_free(decoded);

	bitmapPixels = move(nextPixels);
	bitmapWidth = static_cast<UINT32>(width);
	bitmapHeight = static_cast<UINT32>(height);
	rW = static_cast<double>(width);
	rH = static_cast<double>(height);
	ResetCache();
	return true;
}
bool BarUiPNGClass::InitializationFromResource(const wstring& resType, const wstring& resName)
{
	void* resourceData = nullptr;
	DWORD resourceSize = 0;
	if (!Inkeys::Load::ExtractResourcePtr(resourceData, resourceSize, resType, resName))
		return false;
	return InitializationFromMemory(resourceData, static_cast<size_t>(resourceSize));
}
bool BarUiPNGClass::SetWH(optional<double> wT, optional<double> hT)
{
	double tarW = 0.0, tarH = 0.0;
	if (wT.has_value() && hT.has_value())
	{
		// 同时指定宽高时允许非等比拉伸。
		tarW = wT.value();
		tarH = hT.value();
	}
	else
	{
		if (rW <= 0.0 || rH <= 0.0) return false;
		if (wT.has_value())
		{
			tarW = wT.value();
			tarH = rH * (tarW / rW);
		}
		else if (hT.has_value())
		{
			tarH = hT.value();
			tarW = rW * (tarH / rH);
		}
		else
		{
			tarW = rW;
			tarH = rH;
		}
	}
	if (!isfinite(tarW) || !isfinite(tarH) || tarW <= 0.0 || tarH <= 0.0)
		return false;

	w.SetTar(tarW);
	h.SetTar(tarH);
	return true;
}
bool BarUiPNGClass::CacheBitmap(ID2D1DeviceContext* deviceContext)
{
	if (!deviceContext || bitmapPixels.empty() || bitmapWidth == 0 || bitmapHeight == 0)
		return false;

	D2D1_BITMAP_PROPERTIES props = D2D1::BitmapProperties(
		D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED));
	ComPtr<ID2D1Bitmap> nextBitmap;
	HRESULT hr = deviceContext->CreateBitmap(
		D2D1::SizeU(bitmapWidth, bitmapHeight), bitmapPixels.data(),
		bitmapWidth * 4, props, &nextBitmap);
	if (FAILED(hr) || !nextBitmap) return false;

	cacheBitmap = move(nextBitmap);
	return true;
}
void BarUiPNGClass::ResetCache()
{
	cacheBitmap.Reset();
}

//// 单个文字控件
BarUiWordClass::BarUiWordClass(double xT, double yT, double wT, double hT, wstring contentT, double sizeT, COLORREF colorT, BarUiValueModeEnum type)
{
	x.Initialization(xT, type);
	y.Initialization(yT, type);
	w.Initialization(wT, type);
	h.Initialization(hT, type);
	content.Initialization(contentT);
	size.Initialization(sizeT);
	color.Initialization(colorT);
}
void BarUiWordClass::Initialization(double xT, double yT, double wT, double hT, wstring contentT, double sizeT, COLORREF colorT, BarUiValueModeEnum type)
{
	CancelContentTransition();
	x.Initialization(xT, type);
	y.Initialization(yT, type);
	w.Initialization(wT, type);
	h.Initialization(hT, type);
	content.Initialization(contentT);
	size.Initialization(sizeT);
	color.Initialization(colorT);
}
bool BarUiWordClass::TransitionToString(const wstring& contentT, optional<double> durT,
	double keyframeProgressT, double middleScaleT)
{
	double duration = durT.has_value()
		? durT.value() : static_cast<double>(BarUiDefaultOperationDur);
	return contentTransitionTimeline.Transaction(
		[&](BarUiKeyframeTimelineClass::LockedView& timeline)
		{
			if (timeline.IsActive() && transitionContent.GetTar() == contentT)
				return false;
			if (!timeline.IsActive()
				&& content.GetVal() == contentT && content.GetTar() == contentT)
			{
				transitionContent.Initialization(contentT);
				return false;
			}

			transitionContent.SetTar(contentT);
			contentTransitionStartScale = max(0.0, static_cast<double>(contentScale));
			contentTransitionStartPct = clamp(static_cast<double>(contentPct), 0.0, 1.0);
			contentTransitionMiddleScale = isfinite(middleScaleT)
				? max(0.0, middleScaleT) : 0.8;
			contentTransitionKeyframeProgress = isfinite(keyframeProgressT)
				? clamp(keyframeProgressT, 0.0, 1.0) : 0.5;
			timeline.Start(duration, contentTransitionKeyframeProgress);
			return true;
		});
}
bool BarUiWordClass::AdvanceContentTransition(double dt, double speedRate)
{
	struct TransitionSnapshot
	{
		BarUiKeyframeTimelineResultClass result;
		wstring target;
		double startScale = 1.0;
		double startPct = 1.0;
		double middleScale = 0.8;
		double keyframe = 0.5;
		bool applyContent = false;
	};
	optional<TransitionSnapshot> snapshot = contentTransitionTimeline.Transaction(
		[&](BarUiKeyframeTimelineClass::LockedView& timeline)
			-> optional<TransitionSnapshot>
		{
			if (!timeline.IsActive()) return nullopt;
			TransitionSnapshot value;
			value.result = timeline.Advance(dt, speedRate);
			value.target = transitionContent.GetTar();
			value.startScale = contentTransitionStartScale;
			value.startPct = contentTransitionStartPct;
			value.middleScale = max(0.0, contentTransitionMiddleScale);
			value.keyframe = clamp(contentTransitionKeyframeProgress, 0.0, 1.0);
			value.applyContent = value.result.reachedKeyframe
				|| (value.result.finished
					&& (content.GetVal() != value.target || content.GetTar() != value.target));
			return value;
		});
	if (!snapshot.has_value()) return false;

	double progress = clamp(snapshot->result.progress, 0.0, 1.0);
	double nextScale = 1.0;
	double nextPct = 1.0;
	if (progress <= snapshot->keyframe)
	{
		double localProgress = snapshot->keyframe > 0.000001
			? progress / snapshot->keyframe : 1.0;
		nextScale = snapshot->startScale
			+ (snapshot->middleScale - snapshot->startScale)
			* BarUiApplyCurve(BarUiCurveEnum::EaseInCubic, localProgress);
		nextPct = snapshot->startPct
			* (1.0 - BarUiApplyCurve(BarUiCurveEnum::EaseInSine, localProgress));
	}
	else
	{
		double localProgress = 1.0 - snapshot->keyframe > 0.000001
			? (progress - snapshot->keyframe) / (1.0 - snapshot->keyframe) : 1.0;
		nextScale = snapshot->middleScale + (1.0 - snapshot->middleScale)
			* BarUiApplyCurve(BarUiCurveEnum::EaseOutBack, localProgress);
		nextPct = BarUiApplyCurve(BarUiCurveEnum::EaseOutSine, localProgress);
	}

	contentTransitionTimeline.Transaction(
		[&](BarUiKeyframeTimelineClass::LockedView& timeline)
		{
			if (!timeline.IsCurrentGeneration(snapshot->result.generation)) return;
			contentScale = isfinite(nextScale) ? max(0.0, nextScale) : 1.0;
			contentPct = isfinite(nextPct) ? clamp(nextPct, 0.0, 1.0) : 1.0;

			if (snapshot->result.reachedKeyframe)
			{
				contentScale = snapshot->middleScale;
				contentPct = 0.0;
			}
			if (snapshot->applyContent)
				content.Initialization(snapshot->target);
			if (snapshot->result.finished)
			{
				contentScale = 1.0;
				contentPct = 1.0;
			}
		});
	return true;
}
void BarUiWordClass::CancelContentTransition()
{
	contentTransitionTimeline.Transaction(
		[&](BarUiKeyframeTimelineClass::LockedView& timeline)
		{
			timeline.Cancel();
			transitionContent.Initialization(content.GetTar());
			contentScale = 1.0;
			contentPct = 1.0;
			contentTransitionStartScale = 1.0;
			contentTransitionStartPct = 1.0;
			contentTransitionMiddleScale = 0.8;
			contentTransitionKeyframeProgress = 0.5;
		});
}
