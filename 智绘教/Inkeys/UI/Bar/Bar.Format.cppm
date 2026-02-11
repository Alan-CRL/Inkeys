module;

#include "../../../IdtMain.h"

#include <dwrite.h>

export module Inkeys.UI.Bar.Format;

struct BarFormatKey
{
	IDWriteFontCollection* pFontCollection;
	std::wstring fontFamily;
	float fontSize;
	DWRITE_FONT_WEIGHT fontWeight;
	DWRITE_FONT_STYLE fontStyle;
	DWRITE_FONT_STRETCH fontStretch;
	DWRITE_TEXT_ALIGNMENT textAlignment;
	DWRITE_PARAGRAPH_ALIGNMENT paragraphAlignment;
	std::wstring locale;

	bool operator<(const BarFormatKey& other) const
	{
		return std::tie(pFontCollection, fontFamily, fontSize, fontWeight, fontStyle, fontStretch, textAlignment, paragraphAlignment, locale) <
			std::tie(other.pFontCollection, other.fontFamily, other.fontSize, other.fontWeight, other.fontStyle, other.fontStretch, other.textAlignment, other.paragraphAlignment, other.locale);
	}
};
struct CacheValue
{
	CComPtr<IDWriteTextFormat> pFormat;
	unsigned int usageCountThisFrame = 0; // 用于记录本帧的使用次数
};

export class BarFormatCache
{
public:
	// 构造函数，接收一个 IDWriteFactory 指针
	BarFormatCache(IDWriteFactory* pDWriteFactory) : m_pDWriteFactory(pDWriteFactory) {}

	// 获取 TextFormat 的核心方法 (已改进)
	IDWriteTextFormat* GetFormat(
		const std::wstring& fontFamily,
		float fontSize,
		IDWriteFontCollection* pFontCollection = nullptr,
		DWRITE_FONT_WEIGHT fontWeight = DWRITE_FONT_WEIGHT_NORMAL,
		DWRITE_FONT_STYLE fontStyle = DWRITE_FONT_STYLE_NORMAL,
		DWRITE_FONT_STRETCH fontStretch = DWRITE_FONT_STRETCH_NORMAL,
		const std::wstring& locale = L"zh-cn",
		DWRITE_TEXT_ALIGNMENT textAlign = DWRITE_TEXT_ALIGNMENT_LEADING,
		DWRITE_PARAGRAPH_ALIGNMENT paragraphAlign = DWRITE_PARAGRAPH_ALIGNMENT_NEAR
	)
	{
		// 1. 使用所有参数创建 Key
		BarFormatKey key = { pFontCollection, fontFamily, fontSize, fontWeight, fontStyle, fontStretch, textAlign, paragraphAlign, locale };

		// 2. 在缓存中查找
		auto it = m_cache.find(key);
		if (it != m_cache.end())
		{
			// 找到了！增加本帧使用计数，并返回缓存的对象。
			it->second.usageCountThisFrame++;
			return it->second.pFormat;
		}

		// 3. 没找到，创建一个新的
		IDWriteTextFormat* newFormat = nullptr;
		HRESULT hr = m_pDWriteFactory->CreateTextFormat(
			fontFamily.c_str(),
			pFontCollection, // 使用传入的 FontCollection
			fontWeight,
			fontStyle,
			fontStretch,
			fontSize,
			locale.c_str(),
			&newFormat
		);

		// 默认换行策略：禁用自动换行
		newFormat->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);

		if (SUCCEEDED(hr))
		{
			// 重要：在存入缓存前，设置好所有附加属性！
			newFormat->SetTextAlignment(textAlign);
			newFormat->SetParagraphAlignment(paragraphAlign);

			// 4. 将新创建并完全配置好的对象存入缓存
			CacheValue newValue;
			newValue.pFormat.Attach(newFormat);
			newValue.usageCountThisFrame = 1;

			auto [inserted_it, success] = m_cache.emplace(key, newValue);
			return inserted_it->second.pFormat;
		}

		// 创建失败，返回空指针
		return nullptr;
	}

	void Clean()
	{
		// 使用迭代器循环，因为我们需要在循环中安全地删除元素
		for (auto it = m_cache.begin(); it != m_cache.end(); /* a_it++ in loop */)
		{
			if (it->second.usageCountThisFrame == 0)
			{
				// 本帧未使用，从 map 中擦除。CComPtr 会自动 Release。
				// map::erase(iterator) 会返回下一个有效的迭代器。
				it = m_cache.erase(it);
			}
			else
			{
				// 本帧已使用，将计数器清零为下一帧做准备，然后继续遍历。
				it->second.usageCountThisFrame = 0;
				++it;
			}
		}
	}
	void CleanAll() { m_cache.clear(); };

private:
	CComPtr<IDWriteFactory> m_pDWriteFactory;
	map<BarFormatKey, CacheValue> m_cache;
};