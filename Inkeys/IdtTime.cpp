#include "IdtTime.h"

//时间戳
wstring getTimestamp()
{
	auto now = std::chrono::system_clock::now();
	auto duration = now.time_since_epoch();
	auto millis = std::chrono::duration_cast<std::chrono::milliseconds>(duration).count();
	std::wstringstream wss;
	wss << millis;
	return wss.str();
}
wstring getCurrentDate()
{
	auto t = std::time(nullptr);
	auto tm = *std::localtime(&t);
	std::wstringstream wss;
	wss << std::put_time(&tm, L"%Y%m%d");
	return wss.str();
}
//获取日期
wstring CurrentDate()
{
	auto t = std::time(nullptr);
	auto tm = *std::localtime(&t);
	std::wostringstream woss;
	woss << std::put_time(&tm, L"%Y-%m-%d");
	return woss.str();
}
//获取时间
wstring CurrentTime()
{
	auto t = std::time(nullptr);
	auto tm = *std::localtime(&t);
	std::wostringstream woss;
	woss << std::put_time(&tm, L"%H-%M-%S");
	return woss.str();
}

string GetCurrentTimeAll()
{
	auto now = std::chrono::system_clock::now(); // 获取当前时间点
	auto in_time_t = std::chrono::system_clock::to_time_t(now); // 转换为time_t

	std::tm buf; // 定义时间结构体
	localtime_s(&buf, &in_time_t); // 转换为本地时间

	std::stringstream ss;
	ss << std::put_time(&buf, "%Y/%m/%d %H:%M:%S"); // 格式化时间
	return ss.str();
}

tm GetCurrentLocalTime()
{
	time_t currentTime = time(nullptr);
	tm localTime = {};

	localtime_s(&localTime, &currentTime);

	return localTime;
}