/*
 * @file		IdtMain.cpp
 * @brief		智绘教项目中心源文件
 * @note		用于初始化智绘教并调用相关模块
 *
 * @envir		VisualStudio 2022 | MSVC 143 | .NET Framework 4.0 | EasyX_20230723 | Windows 11
 * @site		https://github.com/Alan-CRL/Intelligent-Drawing-Teaching
 *
 * @author		Alan-CRL
 * @qq			2685549821
 * @email		alan-crl@foxmail.com
*/

#include "IdtMain.h"

#include "IdtConfiguration.h"
#include "IdtDisplayManagement.h"
#include "IdtDrawpad.h"
#include "IdtImage.h"
#include "IdtMagnification.h"
#include "IdtOther.h"
#include "IdtRts.h"
#include "IdtSetting.h"
#include "IdtText.h"
#include "IdtUpdate.h"
#include "IdtWindow.h"

#include <shellscalingapi.h>
#include <lm.h>
#pragma comment(lib, "netapi32.lib")

int floating_main();
int drawpad_main();
int SettingMain();
void FreezeFrameWindow();

bool already = false;

wstring buildTime = __DATE__ L" " __TIME__; //构建时间
string edition_date = "20240324a"; //程序发布日期
string edition_code = "24H1(BetaH2)"; //程序版本

wstring userid; //用户ID（主板序列号）
string global_path; //程序当前路径

double server_updata_error, procedure_updata_error;
wstring server_updata_error_reason;

bool off_signal = false; //关闭指令
map <wstring, bool> thread_status; //线程状态管理

//调测专用
#ifndef IDT_RELEASE
void Test()
{
	MessageBox(NULL, L"标记处", L"标记", MB_OK | MB_SYSTEMMODAL);
}
void Testi(int t)
{
	MessageBox(NULL, to_wstring(t).c_str(), L"数值标记", MB_OK | MB_SYSTEMMODAL);
}
void Testw(wstring t)
{
	MessageBox(NULL, t.c_str(), L"字符标记", MB_OK | MB_SYSTEMMODAL);
}
#endif

// 程序入口点
int main()
{
	//全局路径预处理
	{
		global_path = wstring_to_string(GetCurrentExeDirectory() + L"\\");
	}
	//程序日志初始化
	//{
	//	std::string logFilePath = global_path + "IDT.log";
	//
	//	// Create a logger with a specified file name, and a maximum size of 3MB per file
	//	auto logger = spdlog::basic_logger_st("logger", logFilePath, 1024 * 1024 * 3);
	//	// Set the logger to only output to the file, not to the console
	//	logger->set_level(spdlog::level::trace);
	//	logger->set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%^%L%$] [thread %t] %v");
	//}
	//logger->info("完成程序日志初始化");

	//LOG(INFO) << "尝试初始化DPI";
	//DPI 初始化
	{
		HMODULE hShcore = LoadLibrary(L"Shcore.dll");
		if (hShcore != NULL)
		{
			typedef HRESULT(WINAPI* LPFNSPDPIA)(PROCESS_DPI_AWARENESS);
			LPFNSPDPIA lSetProcessDpiAwareness = (LPFNSPDPIA)GetProcAddress(hShcore, "SetProcessDpiAwareness");
			if (lSetProcessDpiAwareness != NULL)
			{
				HRESULT hr = lSetProcessDpiAwareness(PROCESS_PER_MONITOR_DPI_AWARE);
				if (!SUCCEEDED(hr)) SetProcessDPIAware();
			}
			else SetProcessDPIAware();

			FreeLibrary(hShcore);
		}
		else SetProcessDPIAware();

		//图像DPI转化
		{
			alpha_drawpad.Resize(GetSystemMetrics(SM_CXSCREEN), GetSystemMetrics(SM_CYSCREEN));
			tester.Resize(GetSystemMetrics(SM_CXSCREEN), GetSystemMetrics(SM_CYSCREEN));
			pptdrawpad.Resize(GetSystemMetrics(SM_CXSCREEN), GetSystemMetrics(SM_CYSCREEN));
		}
	}
	//LOG(INFO) << "成功初始化DPI";

	//LOG(INFO) << "尝试初始化字体";
	//字体初始化部分
	{
		HMODULE hModule = GetModuleHandle(NULL);
		HRSRC hResource = FindResource(hModule, MAKEINTRESOURCE(198), L"TTF");
		HGLOBAL hMemory = LoadResource(hModule, hResource);
		PVOID pResourceData = LockResource(hMemory);
		DWORD dwResourceSize = SizeofResource(hModule, hResource);
		fontCollection.AddMemoryFont(pResourceData, dwResourceSize);

		INT numFound = 0;
		fontCollection.GetFamilies(1, &HarmonyOS_fontFamily, &numFound);

		//filesystem::create_directory(string_to_wstring(global_path) + L"ttf");
		//ExtractResource((string_to_wstring(global_path) + L"ttf\\HarmonyOS_Sans_SC_Regular.ttf").c_str(), L"TTF", MAKEINTRESOURCE(198));
		//fontCollection.AddFontFile((string_to_wstring(global_path) + L"ttf\\HarmonyOS_Sans_SC_Regular.ttf").c_str());
		//filesystem::path directory((string_to_wstring(global_path) + L"ttf").c_str());
		//filesystem::remove_all(directory);

		//AddFontResourceEx((string_to_wstring(global_path) + L"ttf\\HarmonyOS_Sans_SC_Regular.ttf").c_str(), FR_PRIVATE, NULL);
		//AddFontResourceEx((string_to_wstring(global_path) + L"ttf\\Douyu_Font.otf").c_str(), FR_PRIVATE, NULL);
		//AddFontResourceEx((string_to_wstring(global_path) + L"ttf\\SmileySans-Oblique.ttf").c_str(), FR_PRIVATE, NULL);

		//wcscpy(font.lfFaceName, L"HarmonyOS Sans SC");
		//wcscpy(font.lfFaceName, L"DOUYU Gdiplus::Font");
		//wcscpy(font.lfFaceName, L"得意黑");

		stringFormat.SetAlignment(StringAlignmentCenter);
		stringFormat.SetLineAlignment(StringAlignmentCenter);
		stringFormat.SetFormatFlags(StringFormatFlagsNoWrap);

		stringFormat_left.SetAlignment(StringAlignmentNear);
		stringFormat_left.SetLineAlignment(StringAlignmentNear);
		stringFormat_left.SetFormatFlags(StringFormatFlagsNoWrap);
	}
	//LOG(INFO) << "成功初始化字体";

	//LOG(INFO) << "尝试检查程序更新";
	//程序更新判断
	{
		//当前程序为新版本
		if (_waccess((string_to_wstring(global_path) + L"update.json").c_str(), 4) == 0)
		{
			wstring tedition, representation;

			Json::Reader reader;
			Json::Value root;

			ifstream readjson;
			readjson.imbue(locale("zh_CN.UTF8"));
			readjson.open(wstring_to_string(string_to_wstring(global_path) + L"update.json").c_str());

			if (reader.parse(readjson, root))
			{
				tedition = string_to_wstring(convert_to_gbk(root["edition"].asString()));
				representation = string_to_wstring(convert_to_gbk(root["representation"].asString()));
			}

			readjson.close();

			if (tedition == string_to_wstring(edition_date))
			{
				//符合条件，开始替换版本

				Sleep(1000);

				filesystem::path directory(global_path);
				string main_path = directory.parent_path().parent_path().string();

				error_code ec;
				filesystem::remove(string_to_wstring(main_path) + L"\\智绘教.exe", ec);
				filesystem::copy_file(string_to_wstring(global_path) + representation, string_to_wstring(main_path) + L"\\智绘教.exe", std::filesystem::copy_options::overwrite_existing, ec);

				STARTUPINFOA si = { 0 };
				si.cb = sizeof(si);
				PROCESS_INFORMATION pi = { 0 };
				CreateProcessA(NULL, (main_path + "\\智绘教.exe").data(), NULL, NULL, FALSE, 0, NULL, NULL, &si, &pi);
				CloseHandle(pi.hProcess);
				CloseHandle(pi.hThread);

				return 0;
			}
		}
		//当前程序为旧版本
		if (_waccess((string_to_wstring(global_path) + L"installer\\update.json").c_str(), 4) == 0)
		{
			wstring tedition, path;
			string thash_md5, thash_sha256;

			Json::Reader reader;
			Json::Value root;

			ifstream readjson;
			readjson.imbue(locale("zh_CN.UTF8"));
			readjson.open(wstring_to_string(string_to_wstring(global_path) + L"installer\\update.json").c_str());

			if (reader.parse(readjson, root))
			{
				tedition = string_to_wstring(convert_to_gbk(root["edition"].asString()));
				path = string_to_wstring(convert_to_gbk(root["path"].asString()));

				thash_md5 = convert_to_gbk(root["hash"]["md5"].asString());
				thash_sha256 = convert_to_gbk(root["hash"]["sha256"].asString());
			}

			readjson.close();

			string hash_md5, hash_sha256;
			{
				hashwrapper* myWrapper = new md5wrapper();
				hash_md5 = myWrapper->getHashFromFile(global_path + wstring_to_string(path));
				delete myWrapper;
			}
			{
				hashwrapper* myWrapper = new sha256wrapper();
				hash_sha256 = myWrapper->getHashFromFile(global_path + wstring_to_string(path));
				delete myWrapper;
			}

			if (tedition > string_to_wstring(edition_date) && _waccess((string_to_wstring(global_path) + path).c_str(), 0) == 0 && hash_md5 == thash_md5 && hash_sha256 == thash_sha256)
			{
				//符合条件，开始替换版本

				STARTUPINFOA si = { 0 };
				si.cb = sizeof(si);
				PROCESS_INFORMATION pi = { 0 };
				CreateProcessA(NULL, (global_path + wstring_to_string(path)).data(), NULL, NULL, FALSE, 0, NULL, NULL, &si, &pi);
				CloseHandle(pi.hProcess);
				CloseHandle(pi.hThread);

				return 0;
			}
			else if (tedition == string_to_wstring(edition_date))
			{
				std::error_code ec;
				filesystem::remove_all(string_to_wstring(global_path) + L"installer", ec);
				filesystem::remove_all(string_to_wstring(global_path) + L"api", ec);

				filesystem::remove(string_to_wstring(global_path) + L"PptCOM.dll", ec);
			}
		}
	}
	//LOG(INFO) << "成功检查程序更新";

	//LOG(INFO) << "尝试获取配置信息";
	//初始化信息获取
	{
		if (_waccess((string_to_wstring(global_path) + L"opt\\deploy.json").c_str(), 4) == -1) FirstSetting(true);

		ReadSetting(true);
		WriteSetting();
	}
	//LOG(INFO) << "成功获取配置信息";

	//LOG(INFO) << "尝试初始化COM";
	CoInitialize(NULL);
	//LOG(INFO) << "成功初始化COM";

	//LOG(INFO) << "尝试获取用户ID";
	userid = GetMainHardDiskSerialNumber();
	if (userid.empty() || !isValidString(userid)) userid = L"无法正确识别";
	//LOG(INFO) << "成功获取用户ID";

	//显示器检查
	DisplayManagementMain();

	shared_lock<shared_mutex> DisplaysNumberLock(DisplaysNumberSm);
	int DisplaysNumberTemp = DisplaysNumber;
	DisplaysNumberLock.unlock();

	if (DisplaysNumberTemp)
	{
		thread MagnifierThread_thread(MagnifierThread);
		MagnifierThread_thread.detach();
	}
	else MessageBox(floating_window, (L"检测到计算机拥有 " + to_wstring(DisplaysNumberTemp) + L" 个显示器，智绘教目前不支持拥有拓展显示器电脑！\n\n程序将继续启动，但窗口定格，历史画板保存，超级恢复功能将失效。\n且仅能在主显示器上绘图！").c_str(), L"智绘教警告", MB_OK | MB_SYSTEMMODAL);

	//桌面快捷方式注册
	if (setlist.CreateLnk) SetShortcut();
	// 快捷键注册
	// thread RegisterHotkeyThread(RegisterHotkey);
	// RegisterHotkeyThread.detach();

	// 创建窗口

	//LOG(INFO) << "尝试创建悬浮窗窗口";
	hiex::PreSetWindowShowState(SW_HIDE);
	floating_window = initgraph(background.getwidth(), background.getheight());
	//LOG(INFO) << "成功创建悬浮窗窗口";

	//LOG(INFO) << "尝试创建画笔窗口";
	hiex::PreSetWindowShowState(SW_HIDE);
	drawpad_window = initgraph(GetSystemMetrics(SM_CXSCREEN), GetSystemMetrics(SM_CYSCREEN));
	//LOG(INFO) << "成功创建画笔窗口";

	//LOG(INFO) << "尝试创建PPT控件";
	hiex::PreSetWindowShowState(SW_HIDE);
	ppt_window = initgraph(GetSystemMetrics(SM_CXSCREEN), GetSystemMetrics(SM_CYSCREEN));
	//LOG(INFO) << "成功创建PPT控件";

	//LOG(INFO) << "尝试创建背景窗口";
	hiex::PreSetWindowShowState(SW_HIDE);
	freeze_window = initgraph(GetSystemMetrics(SM_CXSCREEN), GetSystemMetrics(SM_CYSCREEN));
	//LOG(INFO) << "成功创建背景窗口";

	SetWindowPos(floating_window, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
	SetWindowPos(ppt_window, floating_window, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
	SetWindowPos(drawpad_window, ppt_window, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
	SetWindowPos(freeze_window, drawpad_window, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
	SetWindowPos(setting_window, freeze_window, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
	thread TopWindowThread(TopWindow);
	TopWindowThread.detach();

	//LOG(INFO) << "尝试加载PptCOM类库";
	//PptCOM 组件加载
	HANDLE hActCtx;
	ULONG_PTR ulCookie;
	{
		ExtractResource((string_to_wstring(global_path) + L"PptCOM.dll").c_str(), L"DLL", MAKEINTRESOURCE(222));

		ACTCTX actCtx = { 0 };
		actCtx.cbSize = sizeof(actCtx);
		actCtx.dwFlags = ACTCTX_FLAG_RESOURCE_NAME_VALID | ACTCTX_FLAG_HMODULE_VALID;
		actCtx.lpResourceName = MAKEINTRESOURCE(221);
		actCtx.hModule = GetModuleHandle(NULL);

		hActCtx = CreateActCtx(&actCtx);
		ActivateActCtx(hActCtx, &ulCookie);

		HMODULE hModule = LoadLibrary((string_to_wstring(global_path) + L"PptCOM.dll").c_str());
	}
	//LOG(INFO) << "成功加载PptCOM类库";

	//LOG(INFO) << "尝试初始化RTS触控库";
	// 初始化 RealTimeStylus 触控库
	{
		// Create RTS object
		g_pRealTimeStylus = CreateRealTimeStylus(drawpad_window);
		if (g_pRealTimeStylus == NULL)
		{
			uRealTimeStylus = -1;
			goto RealTimeStylusEnd;
		}

		// Create EventHandler object
		g_pSyncEventHandlerRTS = CSyncEventHandlerRTS::Create(g_pRealTimeStylus);
		if (g_pSyncEventHandlerRTS == NULL)
		{
			g_pRealTimeStylus->Release();
			g_pRealTimeStylus = NULL;

			uRealTimeStylus = -2;
			goto RealTimeStylusEnd;
		}

		// Enable RTS
		if (!EnableRealTimeStylus(g_pRealTimeStylus))
		{
			g_pSyncEventHandlerRTS->Release();
			g_pSyncEventHandlerRTS = NULL;

			g_pRealTimeStylus->Release();
			g_pRealTimeStylus = NULL;

			uRealTimeStylus = -3;
			goto RealTimeStylusEnd;
		}

		if (uRealTimeStylus == 0) uRealTimeStylus = 1;

	RealTimeStylusEnd:

		if (uRealTimeStylus <= 0)
		{
			MessageBox(NULL, (L"触控库 RTS 初始化失败，程序停止运行！\nRTS_Err" + to_wstring(-uRealTimeStylus)).c_str(), L"错误", MB_OK | MB_SYSTEMMODAL);

			off_signal = true;

			// 反初始化 COM 环境
			CoUninitialize();

			return 0;
		}

		thread RTSSpeed_thread(RTSSpeed);
		RTSSpeed_thread.detach();
	}
	//LOG(INFO) << "成功初始化RTS触控库";

	//LOG(INFO) << "尝试启动悬浮窗窗口线程";
	thread floating_main_thread(floating_main);
	floating_main_thread.detach();
	//LOG(INFO) << "成功启动悬浮窗窗口线程";
	//LOG(INFO) << "尝试启动画笔窗口线程";
	thread drawpad_main_thread(drawpad_main);
	drawpad_main_thread.detach();
	//LOG(INFO) << "成功启动画笔窗口线程";

	//LOG(INFO) << "尝试启动选项窗口线程";
	thread test_main_thread(SettingMain);
	test_main_thread.detach();

	//LOG(INFO) << "成功启动选项窗口线程";
	//LOG(INFO) << "尝试启动背景窗口线程";
	thread FreezeFrameWindow_thread(FreezeFrameWindow);
	FreezeFrameWindow_thread.detach();
	//LOG(INFO) << "成功启动背景窗口线程";

	//LOG(INFO) << "尝试启动程序网络注册线程";
	//thread NetUpdate_thread(NetUpdate);
	//NetUpdate_thread.detach();
	//LOG(INFO) << "成功启动程序网络注册线程";

	while (!off_signal) Sleep(500);

	//LOG(INFO) << "尝试结束各线程，将关闭程序";

	int i = 1;
	for (; i <= 20; i++)
	{
		if (!thread_status[L"floating_main"] && !thread_status[L"drawpad_main"] && !thread_status[L"SettingMain"] && !thread_status[L"FreezeFrameWindow"] && !thread_status[L"NetUpdate"]) break;
		Sleep(500);
	}

	// 反初始化 COM 环境
	CoUninitialize();
	DeactivateActCtx(0, ulCookie);
	ReleaseActCtx(hActCtx);

	//if (i > 10) LOG(ERROR) << "失败结束各线程，程序将强制退出";
	//else LOG(INFO) << "成功结束各线程，将关闭程序";

	return 0;
}