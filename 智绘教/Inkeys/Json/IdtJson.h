#pragma once

#include <iostream>
#include <string>
#include <sstream>

class IdtJson
{
public:
	static std::string removeJsoncComments(const std::string& input)
	{
		std::istringstream iss(input);
		std::ostringstream oss;
		std::string line;
		while (std::getline(iss, line))
		{
			size_t comment_pos = std::string::npos;
			bool in_string = false;
			for (size_t i = 0; i < line.length(); ++i)
			{
				if (line[i] == '"')
				{
					// 计算连续反斜杠数
					int backslash_count = 0;
					int j = static_cast<int>(i) - 1;
					while (j >= 0 && line[j] == '\\')
					{
						backslash_count++;
						j--;
					}
					// 如果反斜杠个数为偶数，则不是被转义的引号
					if (backslash_count % 2 == 0)
					{
						in_string = !in_string;
					}
				}
				// 字符串外侧的 '//' 为注释
				if (!in_string && i + 1 < line.length() && line[i] == '/' && line[i + 1] == '/')
				{
					comment_pos = i;
					break;
				}
			}
			if (comment_pos != std::string::npos)
			{
				line.erase(comment_pos);
			}
			oss << line << '\n';
		}
		return oss.str();
	}
private:
	IdtJson() = delete;
};
