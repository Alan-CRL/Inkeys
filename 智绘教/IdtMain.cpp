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
#include "IdtGuid.h"
#include "IdtImage.h"
#include "IdtMagnification.h"
#include "IdtOther.h"
#include "IdtRts.h"
#include "IdtSetting.h"
#include "IdtSysNotifications.h"
#include "IdtText.h"
#include "IdtUpdate.h"
#include "IdtWindow.h"

#include <lm.h>
#include <shellscalingapi.h>
#pragma comment(lib, "netapi32.lib")

int floating_main();
int drawpad_main();
int SettingMain();
void FreezeFrameWindow();

bool already = false;

wstring buildTime = __DATE__ L" " __TIME__;		//构建时间
string edition_date = "20240504a";				//程序发布日期
string edition_channel = "LTS";					//程序发布通道
string edition_code = "24H1(BetaH2)";			//程序版本

wstring userid; //用户ID（主板序列号）
string global_path; //程序当前路径

int off_signal = false, off_signal_ready = false; //关闭指令
map <wstring, bool> thread_status; //线程状态管理

shared_ptr<spdlog::logger> IDTLogger;

// 程序入口点
//int main()
int WINAPI wWinMain(HINSTANCE /*hInstance*/, HINSTANCE /*hPrevInstance*/, LPWSTR /*lpCmdLine*/, int /*nCmdShow*/)
{
	// 路径预处理
	{
		global_path = wstring_to_string(GetCurrentExeDirectory() + L"\\");

		if (!HasReadWriteAccess(string_to_wstring(global_path)))
		{
			if (IsUserAnAdmin()) MessageBox(NULL, L"当前目录权限受限无法正常运行，请将程序转移至其他目录后再运行", L"智绘教提示", MB_SYSTEMMODAL | MB_OK);
			else ShellExecute(NULL, L"runas", GetCurrentExePath().c_str(), NULL, NULL, SW_SHOWNORMAL);
			return 0;
		}
	}
	// 用户ID获取
	{
		userid = string_to_wstring(getDeviceGUID());
		if (userid.empty() || !isValidString(userid)) userid = L"Error";
	}
	// 日志服务初始化
	{
		wstring Timestamp = getTimestamp();

		error_code ec;
		if (_waccess((string_to_wstring(global_path) + L"log").c_str(), 0) == -1) filesystem::create_directory(string_to_wstring(global_path) + L"log", ec);
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

			string directoryPath = global_path + "log";

			filesystem::path directory(directoryPath);
			if (filesystem::exists(directory) && filesystem::is_directory(directory))
			{
				deleteOldLogFiles(directory);
			}
		}

		if (_waccess((string_to_wstring(global_path) + L"log\\idt" + Timestamp + L".log").c_str(), 0) == 0) filesystem::remove(string_to_wstring(global_path) + L"log\\idt" + Timestamp + L".log", ec);

		auto IDTLoggerFileSink = std::make_shared<spdlog::sinks::basic_file_sink_mt>(global_path + "log\\idt" + wstring_to_string(Timestamp) + ".log");

		spdlog::init_thread_pool(8192, 64);
		IDTLogger = std::make_shared<spdlog::async_logger>("IDTLogger", IDTLoggerFileSink, spdlog::thread_pool(), spdlog::async_overflow_policy::block);

		IDTLogger->set_level(spdlog::level::info);
		IDTLogger->set_pattern("[%l][%H:%M:%S.%e]%v");

		IDTLogger->flush_on(spdlog::level::info);
		IDTLogger->info("[主线程][IdtMain] 日志开始记录 " + wstring_to_string(userid));

		//logger->info("");
		//logger->warn("");
		//logger->error("");
		//logger->critical("");
	}
	// 程序自动更新
	{
		if (_waccess((string_to_wstring(global_path) + L"update.json").c_str(), 4) == 0)
		{
			wstring tedition, representation;
			string thash_md5, thash_sha256;
			wstring old_name;

			bool flag = true;

			Json::Reader reader;
			Json::Value root;

			ifstream readjson;
			readjson.imbue(locale("zh_CN.UTF8"));
			readjson.open(wstring_to_string(string_to_wstring(global_path) + L"update.json").c_str());

			if (reader.parse(readjson, root))
			{
				if (root.isMember("edition")) tedition = string_to_wstring(convert_to_gbk(root["edition"].asString()));
				else flag = false;

				if (root.isMember("representation")) representation = string_to_wstring(convert_to_gbk(root["representation"].asString()));
				else flag = false;

				if (root.isMember("hash"))
				{
					if (root["hash"].isMember("md5")) thash_md5 = convert_to_gbk(root["hash"]["md5"].asString());
					else flag = false;
					if (root["hash"].isMember("sha256")) thash_sha256 = convert_to_gbk(root["hash"]["sha256"].asString());
					else flag = false;
				}
				else flag = false;

				if (root.isMember("old_name")) old_name = string_to_wstring(convert_to_gbk(root["old_name"].asString()));
			}
			readjson.close();

			string hash_md5, hash_sha256;
			{
				hashwrapper* myWrapper = new md5wrapper();
				hash_md5 = myWrapper->getHashFromFile(wstring_to_string(GetCurrentExePath()));
				delete myWrapper;
			}
			{
				hashwrapper* myWrapper = new sha256wrapper();
				hash_sha256 = myWrapper->getHashFromFile(wstring_to_string(GetCurrentExePath()));
				delete myWrapper;
			}

			if (flag && tedition == string_to_wstring(edition_date) && hash_md5 == thash_md5 && hash_sha256 == thash_sha256)
			{
				//符合条件，开始替换版本

				Sleep(1000);

				filesystem::path directory(global_path);
				string main_path = directory.parent_path().parent_path().string();

				error_code ec;
				if (!old_name.empty()) filesystem::remove(string_to_wstring(main_path) + L"\\" + old_name, ec);
				else filesystem::remove(string_to_wstring(main_path) + L"\\智绘教.exe", ec);
				filesystem::copy_file(string_to_wstring(global_path) + representation, string_to_wstring(main_path) + L"\\智绘教.exe", std::filesystem::copy_options::overwrite_existing, ec);

				ShellExecute(NULL, NULL, (string_to_wstring(main_path) + L"\\智绘教.exe").c_str(), NULL, NULL, SW_SHOWNORMAL);

				return 0;
			}
			else
			{
				error_code ec;
				filesystem::remove(string_to_wstring(global_path) + L"update.json", ec);

				filesystem::path directory(global_path);
				string main_path = directory.parent_path().parent_path().string();
				if (!old_name.empty()) ShellExecute(NULL, NULL, (string_to_wstring(main_path) + L"\\" + old_name).c_str(), NULL, NULL, SW_SHOWNORMAL);
				else ShellExecute(NULL, NULL, (string_to_wstring(main_path) + L"\\智绘教.exe").c_str(), NULL, NULL, SW_SHOWNORMAL);

				return 0;
			}
		}
		if (_waccess((string_to_wstring(global_path) + L"installer\\update.json").c_str(), 4) == 0)
		{
			wstring tedition, path;
			string thash_md5, thash_sha256;

			bool flag = true;

			Json::Reader reader;
			Json::Value root;

			ifstream readjson;
			readjson.imbue(locale("zh_CN.UTF8"));
			readjson.open(wstring_to_string(string_to_wstring(global_path) + L"installer\\update.json").c_str());

			if (reader.parse(readjson, root))
			{
				if (root.isMember("edition")) tedition = string_to_wstring(convert_to_gbk(root["edition"].asString()));
				else flag = false;

				if (root.isMember("path")) path = string_to_wstring(convert_to_gbk(root["path"].asString()));
				else flag = false;

				if (root.isMember("hash"))
				{
					if (root["hash"].isMember("md5")) thash_md5 = convert_to_gbk(root["hash"]["md5"].asString());
					else flag = false;
					if (root["hash"].isMember("sha256")) thash_sha256 = convert_to_gbk(root["hash"]["sha256"].asString());
					else flag = false;
				}
				else flag = false;
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

			if (flag && tedition > string_to_wstring(edition_date) && _waccess((string_to_wstring(global_path) + path).c_str(), 0) == 0 && hash_md5 == thash_md5 && hash_sha256 == thash_sha256)
			{
				//符合条件，开始替换版本
				{
					root["old_name"] = Json::Value(convert_to_utf8(wstring_to_string(GetCurrentExeName())));

					Json::StreamWriterBuilder outjson;
					outjson.settings_["emitUTF8"] = true;
					std::unique_ptr<Json::StreamWriter> writer(outjson.newStreamWriter());
					ofstream writejson;
					writejson.imbue(locale("zh_CN.UTF8"));
					writejson.open(wstring_to_string(string_to_wstring(global_path) + L"installer\\update.json").c_str());
					writer->write(root, &writejson);
					writejson.close();
				}

				if (ExtractResource((string_to_wstring(global_path) + L"Inkeys.png").c_str(), L"PNG_ICON", MAKEINTRESOURCE(236)))
				{
					IdtSysNotificationsImageAndText04(L"智绘教", 5000, string_to_wstring(global_path) + L"Inkeys.png", L"智绘教正在自动更新，请耐心等待", L"已通过 MD5 和 SHA265 完整性校验", L"版本号 " + string_to_wstring(edition_date) + L" -> " + tedition);

					error_code ec;
					filesystem::remove(string_to_wstring(global_path) + L"Inkeys.png", ec);
				}
				ShellExecute(NULL, NULL, (string_to_wstring(global_path) + path).c_str(), NULL, NULL, SW_SHOWNORMAL);

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
	// 字体初始化
	{
		IDTLogger->info("[主线程][IdtMain] 初始化字体");

		IDTLogger->info("[主线程][IdtMain] 加载字体");

		HMODULE hModule = GetModuleHandle(NULL);
		HRSRC hResource = FindResource(hModule, MAKEINTRESOURCE(198), L"TTF");
		HGLOBAL hMemory = LoadResource(hModule, hResource);
		PVOID pResourceData = LockResource(hMemory);
		DWORD dwResourceSize = SizeofResource(hModule, hResource);
		fontCollection.AddMemoryFont(pResourceData, dwResourceSize);

		INT numFound = 0;
		fontCollection.GetFamilies(1, &HarmonyOS_fontFamily, &numFound);

		{
			if (_waccess((string_to_wstring(global_path) + L"ttf").c_str(), 0) == -1)
			{
				error_code ec;
				filesystem::create_directory(string_to_wstring(global_path) + L"ttf", ec);
			}
			ExtractResource((string_to_wstring(global_path) + L"ttf\\hmossscr.ttf").c_str(), L"TTF", MAKEINTRESOURCE(198));
		}

		IDTLogger->info("[主线程][IdtMain] 加载字体完成");

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
	// 配置信息初始化
	{
		IDTLogger->info("[主线程][IdtMain] 初始化配置信息");

		if (_waccess((string_to_wstring(global_path) + L"opt\\deploy.json").c_str(), 4) == -1)
		{
			IDTLogger->warn("[主线程][IdtMain] 配置信息不存在");

			IDTLogger->info("[主线程][IdtMain] 生成配置信息");
			FirstSetting(true);
			IDTLogger->info("[主线程][IdtMain] 生成配置信息完成");
		}

		IDTLogger->info("[主线程][IdtMain] 读取配置信息");
		ReadSetting(true);
		IDTLogger->info("[主线程][IdtMain] 读取配置信息完成");

		IDTLogger->info("[主线程][IdtMain] 更新配置信息");
		WriteSetting();
		IDTLogger->info("[主线程][IdtMain] 更新配置信息完成");
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
			ExtractResource((string_to_wstring(global_path) + L"PptCOM.dll").c_str(), L"DLL", MAKEINTRESOURCE(222));
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
			HMODULE hModule = LoadLibrary((string_to_wstring(global_path) + L"PptCOM.dll").c_str());
			IDTLogger->info("[主线程][IdtMain] 载入PptCOM.dll完成");
		}

		IDTLogger->info("[主线程][IdtMain] 初始化COM完成");
	}
	// 监视器信息初始化
	{
		IDTLogger->info("[主线程][IdtMain] 初始化监视器信息");

		// 显示器检查
		IDTLogger->info("[主线程][IdtMain] 监视器信息查询");
		DisplayManagementMain();
		IDTLogger->info("[主线程][IdtMain] 监视器信息查询完成");

		shared_lock<shared_mutex> DisplaysNumberLock(DisplaysNumberSm);
		int DisplaysNumberTemp = DisplaysNumber;
		DisplaysNumberLock.unlock();
		if (DisplaysNumberTemp)
		{
			IDTLogger->info("[主线程][IdtMain] MagnifierThread函数线程启动");
			thread MagnifierThread_thread(MagnifierThread);
			MagnifierThread_thread.detach();
		}
		else
		{
			IDTLogger->warn("[主线程][IdtMain] 拥有多个监视器");
			MessageBox(floating_window, (L"检测到计算机拥有 " + to_wstring(DisplaysNumberTemp) + L" 个显示器，智绘教目前不支持拥有拓展显示器电脑！\n\n程序将继续启动，但窗口定格，历史画板保存，超级恢复功能将失效。\n且仅能在主显示器上绘图！").c_str(), L"智绘教警告", MB_OK | MB_SYSTEMMODAL);
		}

		IDTLogger->info("[主线程][IdtMain] 初始化监视器信息完成");
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
		IDTLogger->info("[主线程][IdtMain] 创建悬浮窗窗口");
		hiex::PreSetWindowShowState(SW_HIDE);
		floating_window = initgraph(background.getwidth(), background.getheight());
		IDTLogger->info("[主线程][IdtMain] 创建悬浮窗窗口完成");

		IDTLogger->info("[主线程][IdtMain] 创建PPT批注控件窗口");
		hiex::PreSetWindowShowState(SW_HIDE);
		ppt_window = initgraph(GetSystemMetrics(SM_CXSCREEN), GetSystemMetrics(SM_CYSCREEN));
		IDTLogger->info("[主线程][IdtMain] 创建PPT批注控件窗口完成");

		IDTLogger->info("[主线程][IdtMain] 创建画板窗口");
		hiex::PreSetWindowShowState(SW_HIDE);
		drawpad_window = initgraph(GetSystemMetrics(SM_CXSCREEN), GetSystemMetrics(SM_CYSCREEN));
		IDTLogger->info("[主线程][IdtMain] 创建画板窗口完成");

		IDTLogger->info("[主线程][IdtMain] 创建定格背景窗口");
		hiex::PreSetWindowShowState(SW_HIDE);
		freeze_window = initgraph(GetSystemMetrics(SM_CXSCREEN), GetSystemMetrics(SM_CYSCREEN));
		IDTLogger->info("[主线程][IdtMain] 创建定格背景窗口完成");

		IDTLogger->info("[主线程][IdtMain] 置顶悬浮窗窗口");
		SetWindowPos(floating_window, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
		IDTLogger->info("[主线程][IdtMain] 置顶PPT批注控件窗口");
		SetWindowPos(ppt_window, floating_window, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
		IDTLogger->info("[主线程][IdtMain] 置顶画板窗口");
		SetWindowPos(drawpad_window, ppt_window, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
		IDTLogger->info("[主线程][IdtMain] 置顶定格背景窗口");
		SetWindowPos(freeze_window, drawpad_window, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);

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

			off_signal = true;

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

		IDTLogger->info("[主线程][IdtMain] drawpad_main函数线程启动");
		thread drawpad_main_thread(drawpad_main);
		drawpad_main_thread.detach();

		IDTLogger->info("[主线程][IdtMain] SettingMain函数线程启动");
		thread test_main_thread(SettingMain);
		test_main_thread.detach();

		IDTLogger->info("[主线程][IdtMain] FreezeFrameWindow函数线程启动");
		thread FreezeFrameWindow_thread(FreezeFrameWindow);
		FreezeFrameWindow_thread.detach();
	}

	while (!off_signal) Sleep(500);

	IDTLogger->info("[主线程][IdtMain] 等待各函数线程结束");

	int WaitingCount = 0;
	for (; WaitingCount < 20; WaitingCount++)
	{
		if (!thread_status[L"floating_main"] && !thread_status[L"drawpad_main"] && !thread_status[L"SettingMain"] && !thread_status[L"FreezeFrameWindow"] && !thread_status[L"NetUpdate"]) break;
		Sleep(500);
	}
	if (WaitingCount >= 20) IDTLogger->warn("[主线程][IdtMain] 结束函数线程超时并强制结束线程");

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
	while (!off_signal_ready) Sleep(500);
	IDTLogger->info("[主线程][IdtMain] 崩溃重启助手结束");
#endif

	IDTLogger->info("[主线程][IdtMain] 已结束智绘教所有线程并关闭程序");
	return 0;
}
// 路径权限检测
bool HasReadWriteAccess(const std::wstring& directoryPath)
{
	DWORD attributes = GetFileAttributesW(directoryPath.c_str());
	if (attributes == INVALID_FILE_ATTRIBUTES) return false;
	if (!(attributes & FILE_ATTRIBUTE_DIRECTORY)) return false;
	if (attributes & FILE_ATTRIBUTE_READONLY) return false;
	if (attributes & FILE_ATTRIBUTE_READONLY) return false;

	return true;
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