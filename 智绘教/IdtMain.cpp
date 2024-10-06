/*
 * @file		IdtMain.cpp
 * @brief		智绘教项目中心源文件
 * @note		用于初始化智绘教并调用相关模块
 *
 * @envir		Visual Studio 2022 | MSVC v143 | .NET Framework 4.0 | EasyX_20240601
 * @site		https://github.com/Alan-CRL/Intelligent-Drawing-Teaching
 *
 * @author		Alan-CRL
 * @qq			2685549821
 * @email		alan-crl@foxmail.com
*/

#include "IdtMain.h"

#include "IdtConfiguration.h"
#include "IdtD2DPreparation.h"
#include "IdtDisplayManagement.h"
#include "IdtDrawpad.h"
#include "IdtGuid.h"
#include "IdtI18n.h"
#include "IdtImage.h"
#include "IdtMagnification.h"
#include "IdtOther.h"
#include "IdtPlug-in.h"
#include "IdtRts.h"
#include "IdtSetting.h"
#include "IdtStart.h"
#include "IdtState.h"
#include "IdtText.h"
#include "IdtTime.h"
#include "IdtUpdate.h"
#include "IdtWindow.h"

#include <lm.h>
#include <shellscalingapi.h>
#include <shlobj.h>
#pragma comment(lib, "netapi32.lib")

int floating_main();
int drawpad_main();
int SettingMain();
void FreezeFrameWindow();

wstring buildTime = __DATE__ L" " __TIME__;		//构建时间
wstring editionDate = L"20240714a";				//程序发布日期
wstring editionChannel = L"Dev";				//程序发布通道
wstring editionCode = L"24H2(BetaH3)";			//程序版本

wstring userId; //用户ID（主板序列号）
wstring globalPath; //程序当前路径

int offSignal = false, offSignalReady = false; //关闭指令
map <wstring, bool> threadStatus; //线程状态管理

shared_ptr<spdlog::logger> IDTLogger;

// 程序入口点
int WINAPI wWinMain(HINSTANCE /*hInstance*/, HINSTANCE /*hPrevInstance*/, LPWSTR /*lpCmdLine*/, int /*nCmdShow*/)
{
	Testa(StringToUrlencode("你好，世界"));

	// 路径预处理
	{
		globalPath = GetCurrentExeDirectory() + L"\\";
		MessageBoxW(NULL, globalPath.c_str(), L"", MB_OK);

		{
			int typeRoot = 0;

			if (globalPath.find(L"C:\\Program Files\\") != globalPath.npos || globalPath.find(L"C:\\Program Files (x86)\\") != globalPath.npos || globalPath.find(L"C:\\Windows\\") != globalPath.npos) typeRoot = 2;
			else
			{
				wstring time = getTimestamp();
				wstring path = globalPath + L"IdtRootCheck" + time;

				error_code ec;
				try {
					// 创建空白文件
					wofstream ofs(path);
					if (!ofs) typeRoot = 1;
					ofs.close();
					if (_waccess(path.c_str(), 0) == -1) typeRoot = 1;

					// 删除文件
					filesystem::remove(path, ec);
					if (ec) typeRoot = 1;
					if (_waccess(path.c_str(), 0) == 0) typeRoot = 1;
				}
				catch (const filesystem::filesystem_error)
				{
					typeRoot = 1;
				}
			}

			if (typeRoot == 1)
			{
				MessageBox(NULL, L"当前目录权限受限无法正常运行，请将程序转移至其他目录后再运行", L"智绘教提示", MB_SYSTEMMODAL | MB_OK);
				return 0;
			}
			else if (typeRoot == 2)
			{
				MessageBox(NULL, L"当前目录权限受限（文件操作被重定向到虚拟存储目录）无法正常运行，请将程序转移至其他目录后再运行", L"智绘教提示", MB_SYSTEMMODAL | MB_OK);
				return 0;
			}
		}
	}
#ifdef IDT_RELEASE
	// 防止重复启动
	{
		if (_waccess((StringToWstring(globalPath) + L"force_start.signal").c_str(), 0) == 0)
		{
			error_code ec;
			filesystem::remove(StringToWstring(globalPath) + L"force_start.signal", ec);
		}
		else if (ProcessRunningCnt(GetCurrentExePath()) > 1) return 0;
	}
#endif

	// 用户ID获取
	{
		userId = utf8ToUtf16(getDeviceGUID());
		if (userId.empty() || !isValidString(userId)) userId = L"Error";
	}
	// 日志服务初始化
	{
		wstring Timestamp = getTimestamp();

		error_code ec;
		if (_waccess((globalPath + L"log").c_str(), 0) == -1) filesystem::create_directory(globalPath + L"log", ec);
		else
		{
			// 历史日志清理

			auto getCurrentTimeStamp = []()
				{
					auto now = chrono::system_clock::now();
					auto duration = now.time_since_epoch();
					return chrono::duration_cast<chrono::milliseconds>(duration).count();
				};

			auto isLogFile = [](const string& filename)
				{
					regex pattern("idt\\d+\\.log");
					return regex_match(filename, pattern);
				};

			auto getTimeStampFromFilename = [](const string& filename)
				{
					regex pattern("idt(\\d+)\\.log");

					smatch match;
					if (regex_search(filename, match, pattern))
					{
						string timestampStr = match[1];
						return stoll(timestampStr);
					}
					return -1LL;
				};

			auto isOldLogFile = [&getCurrentTimeStamp, &getTimeStampFromFilename](const filesystem::path& filepath)
				{
					time_t currentTimeStamp = getCurrentTimeStamp();
					time_t fileTimeStamp = getTimeStampFromFilename(filepath.filename().string());

					if (fileTimeStamp == -1) return false;
					return (currentTimeStamp - fileTimeStamp) >= (7LL * 24LL * 60LL * 60LL * 1000LL) || (currentTimeStamp - fileTimeStamp) < 0; // 7天的毫秒数
				};

			auto calculateDirectorySize = [](const filesystem::path& directoryPath)
				{
					uintmax_t totalSize = 0;
					for (const auto& entry : filesystem::directory_iterator(directoryPath))
					{
						if (entry.is_regular_file()) {
							totalSize += entry.file_size();
						}
					}
					return totalSize;
				};

			auto deleteOldLogFiles = [&isLogFile, &isOldLogFile, &calculateDirectorySize](const filesystem::path& directory)
				{
					uintmax_t totalSize = calculateDirectorySize(directory);

					for (const auto& entry : filesystem::directory_iterator(directory))
					{
						if (entry.is_regular_file())
						{
							if (isLogFile(entry.path().filename().string()) && (totalSize > 10485760LL || isOldLogFile(entry.path())))
							{
								uintmax_t entrySize = entry.file_size();

								error_code ec;
								filesystem::remove(entry.path(), ec);

								if (!ec) totalSize -= entrySize;
							}
						}
					}
				};

			wstring directoryPath = globalPath + L"log";

			filesystem::path directory(directoryPath);
			if (filesystem::exists(directory) && filesystem::is_directory(directory))
			{
				deleteOldLogFiles(directory);
			}
		}

		if (_waccess((globalPath + L"log\\idt" + Timestamp + L".log").c_str(), 0) == 0) filesystem::remove(globalPath + L"log\\idt" + Timestamp + L".log", ec);

		auto IDTLoggerFileSink = std::make_shared<spdlog::sinks::basic_file_sink_mt>(utf16ToUtf8(globalPath) + "log\\idt" + utf16ToUtf8(Timestamp) + ".log");

		spdlog::init_thread_pool(8192, 64);
		IDTLogger = std::make_shared<spdlog::async_logger>("IDTLogger", IDTLoggerFileSink, spdlog::thread_pool(), spdlog::async_overflow_policy::block);

		IDTLogger->set_level(spdlog::level::info);
		IDTLogger->set_pattern("[%l][%H:%M:%S.%e]%v");

		IDTLogger->flush_on(spdlog::level::info);
		IDTLogger->info("[主线程][IdtMain] 日志开始记录 " + utf16ToUtf8(editionDate) + " " + utf16ToUtf8(userId));

		//logger->info("");
		//logger->warn("");
		//logger->error("");
		//logger->critical("");
	}
	// 程序自动更新
	{
		if (_waccess((globalPath + L"update.json").c_str(), 4) == 0)
		{
			wstring tedition, representation;
			string thash_md5, thash_sha256;
			wstring old_name;

			bool flag = true;

			Json::Reader reader;
			Json::Value root;

			ifstream readjson;
			readjson.imbue(locale("zh_CN.UTF8"));
			readjson.open((globalPath + L"update.json").c_str());

			if (reader.parse(readjson, root))
			{
				if (root.isMember("edition")) tedition = utf8ToUtf16(root["edition"].asString());
				else flag = false;

				if (root.isMember("representation")) representation = utf8ToUtf16(root["representation"].asString());
				else flag = false;

				if (root.isMember("hash"))
				{
					if (root["hash"].isMember("md5")) thash_md5 = root["hash"]["md5"].asString();
					else flag = false;
					if (root["hash"].isMember("sha256")) thash_sha256 = root["hash"]["sha256"].asString();
					else flag = false;
				}
				else flag = false;

				if (root.isMember("old_name")) old_name = utf8ToUtf16(root["old_name"].asString());
			}
			readjson.close();

			string hash_md5, hash_sha256;
			{
				hashwrapper* myWrapper = new md5wrapper();
				hash_md5 = myWrapper->getHashFromFileW(GetCurrentExePath());
				delete myWrapper;
			}
			{
				hashwrapper* myWrapper = new sha256wrapper();
				hash_sha256 = myWrapper->getHashFromFileW(GetCurrentExePath());
				delete myWrapper;
			}

			if (flag && tedition == editionDate && hash_md5 == thash_md5 && hash_sha256 == thash_sha256)
			{
				//符合条件，开始替换版本

				this_thread::sleep_for(chrono::milliseconds(1000));

				filesystem::path directory(globalPath);
				wstring main_path = directory.parent_path().parent_path().wstring();

				error_code ec;
				if (!old_name.empty()) filesystem::remove(main_path + L"\\" + old_name, ec);
				else filesystem::remove(main_path + L"\\智绘教.exe", ec);

				wstring target = main_path + L"\\智绘教" + editionDate + L".exe";
				filesystem::copy_file(globalPath + representation, target, filesystem::copy_options::overwrite_existing, ec);

				ShellExecute(NULL, NULL, target.c_str(), NULL, NULL, SW_SHOWNORMAL);

				return 0;
			}
			else
			{
				error_code ec;
				filesystem::remove(globalPath + L"update.json", ec);

				filesystem::path directory(globalPath);
				wstring main_path = directory.parent_path().parent_path().wstring();

				if (!old_name.empty()) ShellExecuteW(NULL, NULL, (main_path + L"\\" + old_name).c_str(), NULL, NULL, SW_SHOWNORMAL);
				else ShellExecuteW(NULL, NULL, (main_path + L"\\智绘教.exe").c_str(), NULL, NULL, SW_SHOWNORMAL);

				return 0;
			}
		}
		if (_waccess((globalPath + L"installer\\update.json").c_str(), 4) == 0)
		{
			wstring tedition, path;
			string thash_md5, thash_sha256;

			bool flag = true;

			Json::Reader reader;
			Json::Value root;

			ifstream readjson;
			readjson.imbue(locale("zh_CN.UTF8"));
			readjson.open((globalPath + L"installer\\update.json").c_str());

			if (reader.parse(readjson, root))
			{
				if (root.isMember("edition")) tedition = utf8ToUtf16(root["edition"].asString());
				else flag = false;

				if (root.isMember("path")) path = utf8ToUtf16(root["path"].asString());
				else flag = false;

				if (root.isMember("hash"))
				{
					if (root["hash"].isMember("md5")) thash_md5 = root["hash"]["md5"].asString();
					else flag = false;
					if (root["hash"].isMember("sha256")) thash_sha256 = root["hash"]["sha256"].asString();
					else flag = false;
				}
				else flag = false;
			}

			readjson.close();

			string hash_md5, hash_sha256;
			{
				hashwrapper* myWrapper = new md5wrapper();
				hash_md5 = myWrapper->getHashFromFileW(globalPath + path);
				delete myWrapper;
			}
			{
				hashwrapper* myWrapper = new sha256wrapper();
				hash_sha256 = myWrapper->getHashFromFileW(globalPath + path);
				delete myWrapper;
			}

			if (flag && tedition > editionDate && _waccess((globalPath + path).c_str(), 0) == 0 && hash_md5 == thash_md5 && hash_sha256 == thash_sha256)
			{
				//符合条件，开始替换版本
				{
					root["old_name"] = Json::Value(utf16ToUtf8(GetCurrentExeName()));

					Json::StreamWriterBuilder outjson;
					outjson.settings_["emitUTF8"] = true;
					std::unique_ptr<Json::StreamWriter> writer(outjson.newStreamWriter());
					ofstream writejson;
					writejson.imbue(locale("zh_CN.UTF8"));
					writejson.open((globalPath + L"installer\\update.json").c_str());
					writer->write(root, &writejson);
					writejson.close();
				}
				ShellExecuteW(NULL, NULL, (globalPath + path).c_str(), NULL, NULL, SW_SHOWNORMAL);

				return 0;
			}
			else if (tedition == editionDate)
			{
				error_code ec;
				filesystem::remove_all(globalPath + L"installer", ec);
				filesystem::remove_all(globalPath + L"api", ec);
				filesystem::remove(globalPath + L"PptCOM.dll", ec);
			}
		}
	}

	// DPI初始化
	{
		IDTLogger->info("[主线程][IdtMain] 初始化DPI");

		IDTLogger->info("[主线程][IdtMain] 加载Shcore.dll");
		HMODULE hShcore = LoadLibrary(L"Shcore.dll");
		if (hShcore != NULL)
		{
			IDTLogger->info("[主线程][IdtMain] 加载Shcore.dll成功");

			IDTLogger->info("[主线程][IdtMain] 查询接口SetProcessDpiAwareness");
			typedef HRESULT(WINAPI* LPFNSPDPIA)(PROCESS_DPI_AWARENESS);
			LPFNSPDPIA lSetProcessDpiAwareness = (LPFNSPDPIA)GetProcAddress(hShcore, "SetProcessDpiAwareness");
			if (lSetProcessDpiAwareness != NULL)
			{
				IDTLogger->info("[主线程][IdtMain] 查询接口SetProcessDpiAwareness成功");

				IDTLogger->info("[主线程][IdtMain] 执行SetProcessDpiAwareness");
				HRESULT hr = lSetProcessDpiAwareness(PROCESS_PER_MONITOR_DPI_AWARE);
				if (!SUCCEEDED(hr))
				{
					IDTLogger->warn("[主线程][IdtMain] 执行SetProcessDpiAwareness失败");
					if (!SetProcessDPIAware()) IDTLogger->error("[主线程][IdtMain] 调用SetProcessDPIAware失败");
				}
				else IDTLogger->info("[主线程][IdtMain] 执行SetProcessDpiAwareness成功");
			}
			else
			{
				IDTLogger->warn("[主线程][IdtMain] 查询接口SetProcessDpiAwareness失败");
				if (!SetProcessDPIAware()) IDTLogger->error("[主线程][IdtMain] 调用SetProcessDPIAware失败");
			}

			FreeLibrary(hShcore);
		}
		else
		{
			IDTLogger->warn("[主线程][IdtMain] 加载Shcore.dll失败");
			if (!SetProcessDPIAware()) IDTLogger->error("[主线程][IdtMain] 调用SetProcessDPIAware失败");
		}

		//图像DPI转化
		{
			alpha_drawpad.Resize(GetSystemMetrics(SM_CXSCREEN), GetSystemMetrics(SM_CYSCREEN));
			tester.Resize(GetSystemMetrics(SM_CXSCREEN), GetSystemMetrics(SM_CYSCREEN));
			pptdrawpad.Resize(GetSystemMetrics(SM_CXSCREEN), GetSystemMetrics(SM_CYSCREEN));
		}
		IDTLogger->info("[主线程][IdtMain] 初始化DPI完成");
	}
	// 界面绘图库初始化
	{
		D2DStarup();
	}
	// 字体初始化
	{
		IDTLogger->info("[主线程][IdtMain] 初始化字体");

		IDTLogger->info("[主线程][IdtMain] 加载字体");

		INT numFound = 0;
		HRSRC hRes = FindResource(NULL, MAKEINTRESOURCE(198), L"TTF");
		HGLOBAL hMem = LoadResource(NULL, hRes);
		void* pLock = LockResource(hMem);
		DWORD dwSize = SizeofResource(NULL, hRes);

		fontCollection.AddMemoryFont(pLock, dwSize);
		fontCollection.GetFamilies(1, &HarmonyOS_fontFamily, &numFound);

		{
			if (_waccess((globalPath + L"ttf").c_str(), 0) == -1)
			{
				error_code ec;
				filesystem::create_directory(globalPath + L"ttf", ec);
			}
			ExtractResource((globalPath + L"ttf\\hmossscr.ttf").c_str(), L"TTF", MAKEINTRESOURCE(198));

			IdtFontCollectionLoader* D2DFontCollectionLoader = new IdtFontCollectionLoader;
			D2DFontCollectionLoader->AddFont(D2DTextFactory, globalPath + L"ttf\\hmossscr.ttf");

			D2DTextFactory->RegisterFontCollectionLoader(D2DFontCollectionLoader);
			D2DTextFactory->CreateCustomFontCollection(D2DFontCollectionLoader, 0, 0, &D2DFontCollection);
			D2DTextFactory->UnregisterFontCollectionLoader(D2DFontCollectionLoader);
		}

		IDTLogger->info("[主线程][IdtMain] 加载字体完成");

		//filesystem::create_directory(StringToWstring(globalPath) + L"ttf");
		//ExtractResource((StringToWstring(globalPath) + L"ttf\\HarmonyOS_Sans_SC_Regular.ttf").c_str(), L"TTF", MAKEINTRESOURCE(198));
		//fontCollection.AddFontFile((StringToWstring(globalPath) + L"ttf\\HarmonyOS_Sans_SC_Regular.ttf").c_str());
		//filesystem::path directory((StringToWstring(globalPath) + L"ttf").c_str());
		//filesystem::remove_all(directory);

		//AddFontResourceEx((StringToWstring(globalPath) + L"ttf\\HarmonyOS_Sans_SC_Regular.ttf").c_str(), FR_PRIVATE, NULL);
		//AddFontResourceEx((StringToWstring(globalPath) + L"ttf\\Douyu_Font.otf").c_str(), FR_PRIVATE, NULL);
		//AddFontResourceEx((StringToWstring(globalPath) + L"ttf\\SmileySans-Oblique.ttf").c_str(), FR_PRIVATE, NULL);

		//wcscpy(font.lfFaceName, L"HarmonyOS Sans SC");
		//wcscpy(font.lfFaceName, L"DOUYU Gdiplus::Font");
		//wcscpy(font.lfFaceName, L"得意黑");

		IDTLogger->info("[主线程][IdtMain] 设置字体");

		stringFormat.SetAlignment(StringAlignmentCenter);
		stringFormat.SetLineAlignment(StringAlignmentCenter);
		stringFormat.SetFormatFlags(StringFormatFlagsNoWrap);

		stringFormat_left.SetAlignment(StringAlignmentNear);
		stringFormat_left.SetLineAlignment(StringAlignmentNear);
		stringFormat_left.SetFormatFlags(StringFormatFlagsNoWrap);

		IDTLogger->info("[主线程][IdtMain] 设置字体完成");
		IDTLogger->info("[主线程][IdtMain] 初始化字体完成");
	}
	// I18N初始化
	{
		// 先读取完整性的英语文件，在读取配置指定的语言文件
		// 这样如果配置文件缺少某项也能用英语补齐
		loadI18n(1, L"JSON", L"en-US");
		loadI18n(1, L"JSON", L"zh-CN");
	}
	// 监视器信息初始化
	{
		IDTLogger->info("[主线程][IdtMain] 初始化监视器信息");

		// 显示器检查
		IDTLogger->info("[主线程][IdtMain] 监视器信息查询");
		DisplayManagementMain();
		IDTLogger->info("[主线程][IdtMain] 监视器信息查询完成");

		IDTLogger->info("[主线程][IdtMain] 初始化应用栏信息");
		APPBARDATA abd{};
		enableAppBarAutoHide = (setlist.compatibleTaskBarAutoHide && (SHAppBarMessage(ABM_GETSTATE, &abd) == ABS_AUTOHIDE));
		IDTLogger->info("[主线程][IdtMain] 初始化应用栏信息完成");

		shared_lock<shared_mutex> DisplaysNumberLock(DisplaysNumberSm);
		int DisplaysNumberTemp = DisplaysNumber;
		DisplaysNumberLock.unlock();

		IDTLogger->info("[主线程][IdtMain] MagnifierThread函数线程启动");
		thread(MagnifierThread).detach();

		if (DisplaysNumberTemp > 1) IDTLogger->warn("[主线程][IdtMain] 拥有多个监视器");

		IDTLogger->info("[主线程][IdtMain] 初始化监视器信息完成");
	}

	// 配置信息初始化
	{
		IDTLogger->info("[主线程][IdtMain] 初始化配置信息");

		if (1 || _waccess((globalPath + L"opt\\deploy.json").c_str(), 4) == -1)
		{
			IDTLogger->warn("[主线程][IdtMain] 配置信息不存在");

			// 联控测试：start 界面
			StartForInkeys();
		}
		else
		{
			IDTLogger->info("[主线程][IdtMain] 读取配置信息");
			ReadSetting(true);
			IDTLogger->info("[主线程][IdtMain] 读取配置信息完成");
		}

		IDTLogger->info("[主线程][IdtMain] 更新配置信息");
		WriteSetting();
		IDTLogger->info("[主线程][IdtMain] 更新配置信息完成");
	}
	// 插件配置初始化
	{
		// 启动 DesktopDrawpadBlocker
		thread(StartDesktopDrawpadBlocker).detach();
	}

	// COM初始化
	HANDLE hActCtx;
	ULONG_PTR ulCookie;
	{
		IDTLogger->info("[主线程][IdtMain] 初始化COM");

		IDTLogger->info("[主线程][IdtMain] 初始化CoInitialize");
		CoInitialize(NULL);
		IDTLogger->info("[主线程][IdtMain] 初始化CoInitialize完成");

		//PptCOM 组件加载
		{
			IDTLogger->info("[主线程][IdtMain] 初始化PptCOM.dll");
			ExtractResource((globalPath + L"PptCOM.dll").c_str(), L"DLL", MAKEINTRESOURCE(222));
			IDTLogger->info("[主线程][IdtMain] 初始化PptCOM.dll完成");

			IDTLogger->info("[主线程][IdtMain] 初始化上下文API");

			ACTCTX actCtx = { 0 };
			actCtx.cbSize = sizeof(actCtx);
			actCtx.dwFlags = ACTCTX_FLAG_RESOURCE_NAME_VALID | ACTCTX_FLAG_HMODULE_VALID;
			actCtx.lpResourceName = MAKEINTRESOURCE(221);
			actCtx.hModule = GetModuleHandle(NULL);

			hActCtx = CreateActCtx(&actCtx);
			ActivateActCtx(hActCtx, &ulCookie);

			IDTLogger->info("[主线程][IdtMain] 初始化上下文API完成");

			IDTLogger->info("[主线程][IdtMain] 载入PptCOM.dll");
			HMODULE hModule = LoadLibraryW((globalPath + L"PptCOM.dll").c_str());
			IDTLogger->info("[主线程][IdtMain] 载入PptCOM.dll完成");
		}

		IDTLogger->info("[主线程][IdtMain] 初始化COM完成");
	}
	//桌面快捷方式初始化
	if (setlist.CreateLnk)
	{
		IDTLogger->info("[主线程][IdtMain] 初始化桌面快捷方式");
		SetShortcut();
		IDTLogger->info("[主线程][IdtMain] 初始化桌面快捷方式完成");
	}

	// 窗口
	{
		wstring ClassName;
		if (userId == L"Error") ClassName = L"IdtHiEasyX";
		else ClassName = userId;

		IDTLogger->info("[主线程][IdtMain] 创建定格背景窗口");
		hiex::PreSetWindowShowState(SW_HIDE);
		freeze_window = hiex::initgraph_win32(MainMonitor.MonitorWidth, MainMonitor.MonitorHeight, 0, L"Idt4 FreezeWindow", ClassName.c_str());
		IDTLogger->info("[主线程][IdtMain] 创建定格背景窗口完成");

		IDTLogger->info("[主线程][IdtMain] 创建画板窗口");
		hiex::PreSetWindowShowState(SW_HIDE);
		drawpad_window = hiex::initgraph_win32(MainMonitor.MonitorWidth, MainMonitor.MonitorHeight, 0, L"Idt3 DrawpadWindow", ClassName.c_str(), nullptr, freeze_window);
		IDTLogger->info("[主线程][IdtMain] 创建画板窗口完成");

		IDTLogger->info("[主线程][IdtMain] 创建PPT批注控件窗口");
		hiex::PreSetWindowShowState(SW_HIDE);
		ppt_window = hiex::initgraph_win32(MainMonitor.MonitorWidth, MainMonitor.MonitorHeight, 0, L"Idt2 PptWindow", ClassName.c_str(), nullptr, drawpad_window);
		IDTLogger->info("[主线程][IdtMain] 创建PPT批注控件窗口完成");

		IDTLogger->info("[主线程][IdtMain] 创建悬浮窗窗口");
		hiex::PreSetWindowShowState(SW_HIDE);
		floating_window = hiex::initgraph_win32(background.getwidth(), background.getheight(), 0, L"Idt1 FloatingWindow", ClassName.c_str(), nullptr, ppt_window);
		IDTLogger->info("[主线程][IdtMain] 创建悬浮窗窗口完成");

		// 画板窗口在注册 RTS 前必须拥有置顶属性，在显示前先进行一次全局置顶
		SetWindowPos(freeze_window, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);

		// 画板窗口提前配置其不能拥有焦点的样式，再注册 RTS
		{
			while (!(GetWindowLong(drawpad_window, GWL_EXSTYLE) & WS_EX_NOACTIVATE))
			{
				SetWindowLong(drawpad_window, GWL_EXSTYLE, GetWindowLong(drawpad_window, GWL_EXSTYLE) | WS_EX_NOACTIVATE);
				if (GetWindowLong(drawpad_window, GWL_EXSTYLE) & WS_EX_NOACTIVATE) break;

				this_thread::sleep_for(chrono::milliseconds(10));
			}

			SetWindowPos(drawpad_window, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
		}

		IDTLogger->info("[主线程][IdtMain] TopWindow函数线程启动");
		thread TopWindowThread(TopWindow);
		TopWindowThread.detach();
	}
	// RealTimeStylus触控库
	{
		IDTLogger->info("[主线程][IdtMain] 初始化RTS触控库");
		// Create RTS object
		g_pRealTimeStylus = CreateRealTimeStylus(drawpad_window);
		if (g_pRealTimeStylus == NULL)
		{
			IDTLogger->warn("[主线程][IdtMain] RealTimeStylus 为 NULL");

			uRealTimeStylus = -1;
			goto RealTimeStylusEnd;
		}

		// Create EventHandler object
		g_pSyncEventHandlerRTS = CSyncEventHandlerRTS::Create(g_pRealTimeStylus);
		if (g_pSyncEventHandlerRTS == NULL)
		{
			IDTLogger->warn("[主线程][IdtMain] SyncEventHandlerRTS 为 NULL");

			g_pRealTimeStylus->Release();
			g_pRealTimeStylus = NULL;

			uRealTimeStylus = -2;
			goto RealTimeStylusEnd;
		}

		// Enable RTS
		if (!EnableRealTimeStylus(g_pRealTimeStylus))
		{
			IDTLogger->warn("[主线程][IdtMain] 启用 RTS 失败");

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

			offSignal = true;

			// 反初始化 COM 环境
			CoUninitialize();

			return 0;
		}

		IDTLogger->info("[主线程][IdtMain] RTSSpeed函数线程启动");
		thread RTSSpeed_thread(RTSSpeed);
		RTSSpeed_thread.detach();

		IDTLogger->info("[主线程][IdtMain] 初始化RTS触控库完成");
	}
	// 线程
	{
		IDTLogger->info("[主线程][IdtMain] floating_main函数线程启动");
		thread floating_main_thread(floating_main);
		floating_main_thread.detach();

		IDTLogger->info("[主线程][IdtMain] SettingMain函数线程启动");
		thread test_main_thread(SettingMain);
		test_main_thread.detach();

		IDTLogger->info("[主线程][IdtMain] drawpad_main函数线程启动");
		thread drawpad_main_thread(drawpad_main);
		drawpad_main_thread.detach();

		IDTLogger->info("[主线程][IdtMain] FreezeFrameWindow函数线程启动");
		thread FreezeFrameWindow_thread(FreezeFrameWindow);
		FreezeFrameWindow_thread.detach();

		IDTLogger->info("[主线程][IdtMain] StateMonitoring函数线程启动");
		thread(StateMonitoring).detach();
	}

	IDTLogger->info("[主线程][IdtMain] 开始等待关闭程序信号发出");

	while (!offSignal) this_thread::sleep_for(chrono::milliseconds(500));

	IDTLogger->info("[主线程][IdtMain] 等待各函数线程结束");

	{
		int WaitingCount = 0;
		for (; WaitingCount < 20; WaitingCount++)
		{
			if (!threadStatus[L"floating_main"] && !threadStatus[L"drawpad_main"] && !threadStatus[L"SettingMain"] && !threadStatus[L"FreezeFrameWindow"] && !threadStatus[L"NetUpdate"]) break;
			this_thread::sleep_for(chrono::milliseconds(500));
		}
		if (WaitingCount >= 20) IDTLogger->warn("[主线程][IdtMain] 结束函数线程超时并强制结束线程");
	}

	// 反初始化 COM 环境
	{
		IDTLogger->info("[主线程][IdtMain] 反初始化 COM 环境");

		IDTLogger->info("[主线程][IdtMain] 初始化CoUninitialize");
		CoUninitialize();
		IDTLogger->info("[主线程][IdtMain] 初始化CoUninitialize完成");

		IDTLogger->info("[主线程][IdtMain] 释放上下文API");
		DeactivateActCtx(0, ulCookie);
		ReleaseActCtx(hActCtx);
		IDTLogger->info("[主线程][IdtMain] 释放上下文API完成");

		IDTLogger->info("[主线程][IdtMain] 反初始化 COM 环境完成");
	}

#ifdef IDT_RELEASE
	IDTLogger->info("[主线程][IdtMain] 等待崩溃重启助手结束");
	{
		int WaitingCount = 0;
		for (; WaitingCount < 20; WaitingCount++)
		{
			if (offSignalReady) break;
			this_thread::sleep_for(chrono::milliseconds(500));
		}
		if (WaitingCount >= 20) IDTLogger->warn("[主线程][IdtMain] 等待崩溃重启助手结束超时并强制退出");
	}
	IDTLogger->info("[主线程][IdtMain] 崩溃重启助手结束");
#endif

	IDTLogger->info("[主线程][IdtMain] 已结束智绘教所有线程并关闭程序");
	return 0;
}

// 调测专用
#ifndef IDT_RELEASE
void Test()
{
	MessageBox(NULL, L"标记处", L"标记", MB_OK | MB_SYSTEMMODAL);
}
void Testi(long long t)
{
	MessageBox(NULL, to_wstring(t).c_str(), L"数值标记", MB_OK | MB_SYSTEMMODAL);
}
void Testw(wstring t)
{
	MessageBoxW(NULL, t.c_str(), L"字符标记", MB_OK | MB_SYSTEMMODAL);
}
void Testa(string t)
{
	MessageBoxA(NULL, t.c_str(), "字符标记", MB_OK | MB_SYSTEMMODAL);
}
#endif