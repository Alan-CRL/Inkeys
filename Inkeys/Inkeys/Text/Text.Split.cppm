export module Inkeys.Text.Split;

import <string>;
import <vector>;

namespace Inkeys::Split
{
	using namespace std;

	export vector<wstring> Run(const wstring& input, wchar_t custom_sep)
	{
		vector<wstring> result;
		wstring token;

		bool in_quote = false;
		bool in_custom = false;

		for (size_t i = 0; i < input.size(); i++)
		{
			wchar_t ch = input[i];

			if (!in_quote && !in_custom && (ch == L' ' || ch == L'\t'))
			{
				if (!token.empty())
				{
					result.push_back(token);
					token.clear();
				}
				continue;
			}

			// 处理自定义分隔符
			if (!in_quote && ch == custom_sep && !in_custom)
			{
				in_custom = true;
				token += ch; // 保留起始分隔符
				continue;
			}
			else if (in_custom && ch == custom_sep)
			{
				in_custom = false;
				token += ch; // 保留结束分隔符
				continue;
			}

			// 处理双引号分隔符
			if (!in_custom && ch == L'\"' && !in_quote)
			{
				in_quote = true;
				token += ch; // 保留起始分隔符
				continue;
			}
			else if (in_quote && ch == L'\"')
			{
				in_quote = false;
				token += ch; // 保留结束分隔符
				continue;
			}

			// 普通字符
			token += ch;
		}

		if (!token.empty())
		{
			result.push_back(token);
		}
		return result;
	}
}