/*
 * @file		IdtMain.cpp
 * @brief		智绘教项目中心源文件
 * @note		用于初始化智绘教并调用相关模块
 *
 * @envir		VisualStudio 2022 | MSVC 143 | .NET Framework 3.5 | EasyX_20230723 | Windows 11
 * @site		https://github.com/Alan-CRL/Intelligent-Drawing-Teaching
 *
 * @author		Alan-CRL
 * @qq			2685549821
 * @email		alan-crl@foxmail.com
*/

#include "IdtMain.h"

#include "IdtConfiguration.h"
#include "IdtImage.h"
#include "IdtMagnification.h"
#include "IdtOther.h"
#include "IdtRts.h"
#include "IdtText.h"
#include "IdtUpdate.h"
#include "IdtWindow.h"

int floating_main();
int drawpad_main();
int test_main();
void FreezeFrameWindow();

// 程序入口点
int main()
{
	//全局路径预处理
	{
		global_path = wstring_to_string(GetCurrentExeDirectory() + L"\\");
	}

	//DPI 初始化
	{
		HINSTANCE hUser32 = LoadLibrary(L"User32.dll");
		if (hUser32)
		{
			typedef BOOL(WINAPI* LPSetProcessDPIAware)(void);
			LPSetProcessDPIAware pSetProcessDPIAware = (LPSetProcessDPIAware)GetProcAddress(hUser32, "SetProcessDPIAware");
			if (pSetProcessDPIAware)
			{
				pSetProcessDPIAware();
			}
			FreeLibrary(hUser32);
		}
	}
	//字体初始化部分
	{
		HMODULE hModule = GetModuleHandle(NULL);
		HRSRC hResource = FindResource(hModule, MAKEINTRESOURCE(198), L"TTF");
		HGLOBAL hMemory = LoadResource(hModule, hResource);
		PVOID pResourceData = LockResource(hMemory);
		DWORD dwResourceSize = SizeofResource(hModule, hResource);
		fontCollection.AddMemoryFont(pResourceData, dwResourceSize);

		//filesystem::create_directory(string_to_wstring(global_path) + L"ttf");
		//ExtractResource((string_to_wstring(global_path) + L"ttf\\HarmonyOS_Sans_SC_Regular.ttf").c_str(), L"TTF", MAKEINTRESOURCE(198));
		//fontCollection.AddFontFile((string_to_wstring(global_path) + L"ttf\\HarmonyOS_Sans_SC_Regular.ttf").c_str());
		//filesystem::path directory((string_to_wstring(global_path) + L"ttf").c_str());
		//filesystem::remove_all(directory);

		INT numFound = 0;
		fontCollection.GetFamilies(1, &HarmonyOS_fontFamily, &numFound);

		//AddFontResourceEx((string_to_wstring(global_path) + L"ttf\\HarmonyOS_Sans_SC_Regular.ttf").c_str(), FR_PRIVATE, NULL);
		//AddFontResourceEx((string_to_wstring(global_path) + L"ttf\\Douyu_Font.otf").c_str(), FR_PRIVATE, NULL);
		//AddFontResourceEx((string_to_wstring(global_path) + L"ttf\\SmileySans-Oblique.ttf").c_str(), FR_PRIVATE, NULL);

		//wcscpy(font.lfFaceName, L"HarmonyOS Sans SC");
		//wcscpy(font.lfFaceName, L"DOUYU Gdiplus::Font");
		//wcscpy(font.lfFaceName, L"得意黑");

		stringFormat.SetAlignment(StringAlignmentCenter);
		stringFormat.SetLineAlignment(StringAlignmentCenter);

		stringFormat_left.SetAlignment(StringAlignmentNear);
		stringFormat_left.SetLineAlignment(StringAlignmentNear);
	}

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

	CoInitialize(NULL);

	userid = GetMainHardDiskSerialNumber();
	if (!isValidString(userid) || (userid.find(L"[") != userid.npos || userid.find(L"]") != userid.npos || userid.find(L";") != userid.npos || userid.find(L",") != userid.npos)) userid = L"无法正确识别";

	hiex::PreSetWindowShowState(SW_HIDE);
	floating_window = initgraph(background.getwidth(), background.getheight());
	hiex::PreSetWindowShowState(SW_HIDE);
	drawpad_window = initgraph(GetSystemMetrics(SM_CXSCREEN), GetSystemMetrics(SM_CYSCREEN));
	hiex::PreSetWindowShowState(SW_HIDE);
	ppt_window = initgraph(GetSystemMetrics(SM_CXSCREEN), GetSystemMetrics(SM_CYSCREEN));
	hiex::PreSetWindowShowState(SW_HIDE);
	freeze_window = initgraph(GetSystemMetrics(SM_CXSCREEN), GetSystemMetrics(SM_CYSCREEN));
	hiex::PreSetWindowShowState(SW_HIDE);
	test_window = initgraph(1010, 750);

	//初始化信息获取
	{
		if (_waccess((string_to_wstring(global_path) + L"opt\\deploy.json").c_str(), 4) == -1)
		{
			CreateDirectory((string_to_wstring(global_path) + L"opt").c_str(), NULL);

			Json::StyledWriter outjson;
			Json::Value root;

			root["edition"] = Json::Value(edition_date);
			root["startup"] = Json::Value(true);
			root["experimental_functions"] = Json::Value(true);

			ofstream writejson;
			writejson.imbue(locale("zh_CN.UTF8"));
			writejson.open(wstring_to_string(string_to_wstring(global_path) + L"opt\\deploy.json").c_str());
			writejson << outjson.write(root);
			writejson.close();
		}

		{
			Json::Reader reader;
			Json::Value root;

			ifstream readjson;
			readjson.imbue(locale("zh_CN.UTF8"));
			readjson.open(wstring_to_string(string_to_wstring(global_path) + L"opt\\deploy.json").c_str());

			if (reader.parse(readjson, root))
			{
				if (root.isMember("startup")) setlist.startup = root["startup"].asBool();
				if (root.isMember("experimental_functions")) setlist.experimental_functions = root["experimental_functions"].asBool();
			}

			readjson.close();
		}
		{
			Json::StyledWriter outjson;
			Json::Value root;

			root["edition"] = Json::Value(edition_date);
			root["startup"] = Json::Value(setlist.startup);
			root["experimental_functions"] = Json::Value(setlist.experimental_functions);

			ofstream writejson;
			writejson.imbue(locale("zh_CN.UTF8"));
			writejson.open(wstring_to_string(string_to_wstring(global_path) + L"opt\\deploy.json").c_str());
			writejson << outjson.write(root);
			writejson.close();
		}

		if (setlist.startup == true) ModifyRegedit(true);
		else ModifyRegedit(false);

		if (setlist.experimental_functions)
		{
			thread MagnifierThread_thread(MagnifierThread);
			MagnifierThread_thread.detach();
		}
	}

	//PptCOM 组件加载
	HANDLE hActCtx;
	ULONG_PTR ulCookie;
	{
		ACTCTX actCtx = { 0 };
		actCtx.cbSize = sizeof(actCtx);
		actCtx.dwFlags = ACTCTX_FLAG_RESOURCE_NAME_VALID | ACTCTX_FLAG_HMODULE_VALID;
		actCtx.lpResourceName = MAKEINTRESOURCE(221);
		actCtx.hModule = GetModuleHandle(NULL);

		hActCtx = CreateActCtx(&actCtx);
		ActivateActCtx(hActCtx, &ulCookie);

		HMODULE hModule = LoadLibrary((string_to_wstring(global_path) + L"PptCOM.dll").c_str());
		if (!hModule)
		{
			//加载失败
			ExtractResource((string_to_wstring(global_path) + L"PptCOM.dll").c_str(), L"DLL", MAKEINTRESOURCE(222));
			hModule = LoadLibrary((string_to_wstring(global_path) + L"PptCOM.dll").c_str());
		}
	}

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

	thread floating_main_thread(floating_main);
	floating_main_thread.detach();
	thread drawpad_main_thread(drawpad_main);
	drawpad_main_thread.detach();
	thread test_main_thread(test_main);
	test_main_thread.detach();
	thread FreezeFrameWindow_thread(FreezeFrameWindow);
	FreezeFrameWindow_thread.detach();

	thread NetUpdate_thread(NetUpdate);
	NetUpdate_thread.detach();

	while (!off_signal) Sleep(500);
	while (1) if (!thread_status[L"floating_main"] && !thread_status[L"drawpad_main"] && !thread_status[L"test_main"] && !thread_status[L"NetUpdate"]) break;

	// 反初始化 COM 环境
	CoUninitialize();
	DeactivateActCtx(0, ulCookie);
	ReleaseActCtx(hActCtx);

	return 0;
}

bool debug = false;
bool already = false;

wstring buildTime = __DATE__ L" " __TIME__; //构建时间
string edition_date = "20231011a"; //程序发布日期
string edition_code = "2310 - BUG Bash"; //程序版本

wstring userid; //用户ID（主板序列号）
string global_path; //程序当前路径

string server_feedback, server_code;
double server_updata_error, procedure_updata_error;
wstring server_updata_error_reason;

bool off_signal = false; //关闭指令
map <wstring, bool> thread_status; //线程状态管理