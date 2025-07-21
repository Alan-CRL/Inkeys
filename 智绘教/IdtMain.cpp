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

#include "IdtBar.h"
#include "IdtConfiguration.h"
#include "IdtD2DPreparation.h"
#include "IdtDisplayManagement.h"
#include "IdtDraw.h"
#include "IdtDrawpad.h"
#include "IdtFloating.h"
#include "IdtFreezeFrame.h"
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
#include "Launch/IdtLaunchState.h"
#include "CrashHandler/CrashHandler.h"
#include "SuperTop/IdtSuperTop.h"

#include <lm.h>
#include <shellscalingapi.h>
#include <shlobj.h>
#pragma comment(lib, "netapi32.lib")

wstring buildTime = __DATE__ L" " __TIME__;		// 构建时间
wstring editionDate = L"20250721a";				// 程序发布日期
wstring editionChannel = L"LTS";				// 程序发布通道

wstring userId;									// 用户GUID
wstring globalPath;								// 程序当前路径
wstring dataPath;								// 数据保存的路径

wstring programArchitecture = L"win32";
wstring targetArchitecture = L"win32";

int offSignal = false;							// 关闭指令
map <wstring, bool> threadStatus;				// 线程状态管理

void CloseProgram()
{
	CrashHandler::Shutdown();
	offSignal = 1;
}
void RestartProgram()
{
	CrashHandler::Shutdown();
	offSignal = 2;
}

shared_ptr<spdlog::logger> IDTLogger;

// 程序入口点
int WINAPI wWinMain(HINSTANCE /*hInstance*/, HINSTANCE /*hPrevInstance*/, LPWSTR lpCmdLine, int /*nCmdShow*/)
// int main()
{
	// 路径预处理
	{
		globalPath = GetCurrentExeDirectory() + L"\\";
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
				MessageBox(NULL, L"The current directory permissions are restricted and cannot run normally. Please transfer the program to another directory before running it again.(#1)\n当前目录权限受限无法正常运行，请将程序转移至其他目录后再运行。(#1)", L"Inkeys Tips | 智绘教提示", MB_SYSTEMMODAL | MB_OK);
				return 0;
			}
			else if (typeRoot == 2)
			{
				MessageBox(NULL, L"The current directory permissions are restricted (file operations are redirected to the virtual storage directory) and cannot run properly. Please transfer the program to another directory before running it again.(#2)\n当前目录权限受限（文件操作被重定向到虚拟存储目录）无法正常运行，请将程序转移至其他目录后再运行。(#2)", L"Inkeys Tips | 智绘教提示", MB_SYSTEMMODAL | MB_OK);
				return 0;
			}
		}

		wstring appName = GetCurrentExeName();
		if (!isAsciiPrintable(appName))
		{
			MessageBox(NULL, L"The file name of this software can only contain English characters. Please rename and restart the software.(#3)\n此软件文件名称只能包含英文字符，请重命名后重启软件。(#3)", L"Inkeys Tips | 智绘教提示", MB_SYSTEMMODAL | MB_OK);
			return 0;
		}

		// 获取数据存储路径
		{
			wchar_t buffer[MAX_PATH];
			if (GetEnvironmentVariableW(L"ProgramData", buffer, MAX_PATH) != 0) dataPath = buffer;
			else dataPath = L"C:\\ProgramData";
			dataPath += L"\\Inkeys";
		}
	}
	// 防止重复启动
	{
		// TODO 将为启动标识重写书写逻辑，运行存在多个并行的启动标识
		// 示例
		// -restart
		// -path="..."

		// 相关标识
		bool superTopComplete = false;

		{
			vector<wstring> args = CustomSplit::Run(GetCommandLineW(), L'*');
			for (size_t i = 1; i < args.size(); i++)
			{
				bool addCommandLine = true;

				wstring commandLine = args[i];

				cout << utf16ToUtf8(commandLine) << endl;

				if (commandLine == L"-Restart") LaunchState::restart = true;
				else if (commandLine == L"-WarnTry") LaunchState::warnTry = true;
				else if (commandLine == L"-CrashTry") LaunchState::crashTry = true;
				else if (commandLine == L"-SuperTopComplete") superTopComplete = true, addCommandLine = false;
				else if (commandLine.substr(0, 9) == L"-SuperTop")
				{
					addCommandLine = false;

					wregex pattern(LR"(^[^*]*\*([^*]+)\*[^*]*$)");
					wsmatch matches;
					if (regex_match(commandLine, matches, pattern))
					{
						SurperTopMain(matches[1].str());
						exit(0);
					}
				}

				if (addCommandLine) LaunchState::commandLine += commandLine + L" ";
			}
		}

#ifdef IDT_RELEASE
		if (!LaunchState::restart && !LaunchState::warnTry && !LaunchState::crashTry && !superTopComplete)
		{
			wstring currentExeDirectory = GetCurrentExeDirectory();
			{
				wstringstream result;
				for (wchar_t ch : currentExeDirectory)
				{
					if (ch < 128 && ch != L'\\' && ch != L':' && ch != L';' && ch != L'"') result << ch;
					else if (ch < 128) result << L'_';
					else result << L"U" << std::hex << std::uppercase << static_cast<int>(ch);
				}
				currentExeDirectory = result.str();
			}

			wstring mutexName = L"Inkeys_" + currentExeDirectory;
			if (mutexName.length() > 255) mutexName = mutexName.substr(255);

			launchMutex = CreateMutexW(
				NULL,           // 默认安全属性
				TRUE,           // 请求立即拥有所有权 (bInitialOwner = TRUE)
				mutexName.c_str()    // 唯一的互斥体名称
			);

			DWORD lastError = GetLastError();
			if (launchMutex != NULL && lastError == ERROR_ALREADY_EXISTS)
			{
				MessageBox(NULL, L"智绘教Inkeys is already running. If not, you need to end the relevant process and reopen the program.\n智绘教Inkeys 已经运行。如果没有则需要结束相关进程后重新打开程序。", L"Inkeys Tips | 智绘教提示", MB_SYSTEMMODAL | MB_OK);

				return 0;
			}
		}
#endif
		if (LaunchState::crashTry) CrashHandler::IsSecond(true);
	}
	// 崩溃助手初始化
	{
		CrashHandler::Initialize();
	}
	// 体系架构识别
	{
#if defined(_M_ARM64)
		programArchitecture = L"arm64";
#elif defined(_M_ARM64EC)
		programArchitecture = L"arm64ec";
#elif defined(_WIN64)
		programArchitecture = L"win64";
#else
		programArchitecture = L"win32";
#endif

		USHORT processMachine = 0, nativeMachine = 0;
		bool successFlg = false;

		HMODULE hKernel32 = GetModuleHandleW(L"kernel32");
		if (hKernel32)
		{
			typedef BOOL(WINAPI* LPFN_ISWOW64PROCESS2)(HANDLE, USHORT*, USHORT*);
			LPFN_ISWOW64PROCESS2 fnIsWow64Process2 = (LPFN_ISWOW64PROCESS2)GetProcAddress(hKernel32, "IsWow64Process2");

			if (fnIsWow64Process2)
			{
				// 如果 IsWow64Process2 可用
				if (fnIsWow64Process2(GetCurrentProcess(), &processMachine, &nativeMachine))
				{
					if (nativeMachine == IMAGE_FILE_MACHINE_ARM64)
					{
						targetArchitecture = L"arm64";
						successFlg = true;
					}
					else if (nativeMachine == IMAGE_FILE_MACHINE_AMD64)
					{
						targetArchitecture = L"win64";
						successFlg = true;
					}
					else
					{
						targetArchitecture = L"win32";
						successFlg = true;
					}
				}
			}
		}
		if (!successFlg)
		{
			SYSTEM_INFO sysInfo;
			GetNativeSystemInfo(&sysInfo);
			if (sysInfo.wProcessorArchitecture == PROCESSOR_ARCHITECTURE_ARM64) targetArchitecture = L"arm64";
			else if (sysInfo.wProcessorArchitecture == PROCESSOR_ARCHITECTURE_AMD64) targetArchitecture = L"win64";
			else targetArchitecture = L"win32";
		}

		if (targetArchitecture == L"arm64" && programArchitecture != L"arm64" && programArchitecture != L"arm64ec") inconsistentArchitecture = true;
		if (targetArchitecture == L"win64" && programArchitecture != L"win64") inconsistentArchitecture = true;
		if (targetArchitecture == L"win32" && programArchitecture != L"win32") inconsistentArchitecture = true;
	}
	// 程序自动更新
	{
		if (_waccess((globalPath + L"update.json").c_str(), 4) == 0)
		{
			wstring tedition, representation;
			string thash_md5, thash_sha256;
			wstring old_name;

			bool flag = true;
			string jsonContent;

			HANDLE fileHandle = NULL;
			if (OccupyFileForRead(&fileHandle, globalPath + L"update.json"))
			{
				LARGE_INTEGER fileSize;
				if (flag && !GetFileSizeEx(fileHandle, &fileSize)) flag = false;

				if (flag)
				{
					DWORD dwSize = static_cast<DWORD>(fileSize.QuadPart);
					jsonContent = string(dwSize, '\0');

					DWORD bytesRead = 0;
					if (flag && SetFilePointer(fileHandle, 0, NULL, FILE_BEGIN) == INVALID_SET_FILE_POINTER) flag = false;
					if (flag && !ReadFile(fileHandle, &jsonContent[0], dwSize, &bytesRead, NULL) || bytesRead != dwSize) flag = false;
					if (flag && jsonContent.compare(0, 3, "\xEF\xBB\xBF") == 0) jsonContent = jsonContent.substr(3);
				}
			}
			else flag = false;
			UnOccupyFile(&fileHandle);

			Json::Value updateVal;
			if (flag)
			{
				istringstream jsonContentStream(jsonContent);
				Json::CharReaderBuilder readerBuilder;
				string jsonErr;

				if (Json::parseFromStream(readerBuilder, jsonContentStream, &updateVal, &jsonErr))
				{
					if (updateVal.isMember("edition") && updateVal["edition"].isString()) tedition = utf8ToUtf16(updateVal["edition"].asString());
					else flag = false;

					if (updateVal.isMember("representation") && updateVal["representation"].isString()) representation = utf8ToUtf16(updateVal["representation"].asString());
					else flag = false;

					if (updateVal.isMember("hash") && updateVal["hash"].isObject())
					{
						if (updateVal["hash"].isMember("md5") && updateVal["hash"]["md5"].isString()) thash_md5 = updateVal["hash"]["md5"].asString();
						else flag = false;
						if (updateVal["hash"].isMember("sha256") && updateVal["hash"]["sha256"].isString()) thash_sha256 = updateVal["hash"]["sha256"].asString();
						else flag = false;
					}
					else flag = false;

					if (updateVal.isMember("old_name") && updateVal["old_name"].isString()) old_name = utf8ToUtf16(updateVal["old_name"].asString());
				}
				else flag = false;
			}

			string hash_md5, hash_sha256;
			if (flag)
			{
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
			}

			if (flag && tedition == editionDate && hash_md5 == thash_md5 && hash_sha256 == thash_sha256)
			{
				//符合条件，开始替换版本
				filesystem::path directory(globalPath);
				wstring main_path = directory.parent_path().parent_path().wstring() + L"\\";

				int times;
				for (times = 0; times <= 20; times++)
				{
					if (!old_name.empty())
					{
						if (!isProcessRunning((main_path + old_name).c_str()))
							break;
					}
					else
					{
						if (!isProcessRunning((main_path + L"智绘教.exe").c_str()))
							break;
					}

					this_thread::sleep_for(chrono::milliseconds(100));
				}
				if (times > 20) goto fail;

				error_code ec;
				if (!old_name.empty()) filesystem::remove(main_path + old_name, ec);
				else filesystem::remove(main_path + L"智绘教.exe", ec);

				wstring target = main_path + L"Inkeys" + L".exe";
				filesystem::copy_file(globalPath + representation, target, filesystem::copy_options::overwrite_existing, ec);

				ShellExecuteW(NULL, NULL, target.c_str(), NULL, NULL, SW_SHOWNORMAL);

				return 0;
			}
			else flag = false;

			if (!flag)
			{
			fail:
				error_code ec;
				filesystem::remove(globalPath + L"update.json", ec);

				filesystem::path directory(globalPath);
				wstring main_path = directory.parent_path().parent_path().wstring() + L"\\";

				if (!old_name.empty()) ShellExecuteW(NULL, NULL, (main_path + old_name).c_str(), NULL, NULL, SW_SHOWNORMAL);
				else ShellExecuteW(NULL, NULL, (main_path + L"智绘教.exe").c_str(), NULL, NULL, SW_SHOWNORMAL);

				return 0;
			}
		}
		if (_waccess((globalPath + L"installer\\update.json").c_str(), 4) == 0)
		{
			wstring tedition, path;
			string thash_md5, thash_sha256;

			bool flag = true;
			bool mandatoryUpdate = false;
			string jsonContent;

			HANDLE fileHandle = NULL;
			if (OccupyFileForRead(&fileHandle, globalPath + L"installer\\update.json"))
			{
				LARGE_INTEGER fileSize;
				if (flag && !GetFileSizeEx(fileHandle, &fileSize)) flag = false;

				if (flag)
				{
					DWORD dwSize = static_cast<DWORD>(fileSize.QuadPart);
					jsonContent = string(dwSize, '\0');

					DWORD bytesRead = 0;
					if (flag && SetFilePointer(fileHandle, 0, NULL, FILE_BEGIN) == INVALID_SET_FILE_POINTER) flag = false;
					if (flag && !ReadFile(fileHandle, &jsonContent[0], dwSize, &bytesRead, NULL) || bytesRead != dwSize) flag = false;
					if (flag && jsonContent.compare(0, 3, "\xEF\xBB\xBF") == 0) jsonContent = jsonContent.substr(3);
				}
			}
			else flag = false;
			UnOccupyFile(&fileHandle);

			Json::Value updateVal;
			if (flag)
			{
				istringstream jsonContentStream(jsonContent);
				Json::CharReaderBuilder readerBuilder;
				string jsonErr;

				if (Json::parseFromStream(readerBuilder, jsonContentStream, &updateVal, &jsonErr))
				{
					if (updateVal.isMember("edition") && updateVal["edition"].isString()) tedition = utf8ToUtf16(updateVal["edition"].asString());
					else flag = false;

					if (updateVal.isMember("path") && updateVal["path"].isString()) path = utf8ToUtf16(updateVal["path"].asString());
					else flag = false;

					if (updateVal.isMember("hash") && updateVal["hash"].isObject())
					{
						if (updateVal["hash"].isMember("md5") && updateVal["hash"]["md5"].isString()) thash_md5 = updateVal["hash"]["md5"].asString();
						else flag = false;
						if (updateVal["hash"].isMember("sha256") && updateVal["hash"]["sha256"].isString()) thash_sha256 = updateVal["hash"]["sha256"].asString();
						else flag = false;
					}
					else flag = false;

					if (updateVal.isMember("MandatoryUpdate") && updateVal["MandatoryUpdate"].isBool())
						mandatoryUpdate = updateVal["MandatoryUpdate"].asBool();
				}
				else flag = false;
			}

			string hash_md5, hash_sha256;
			if (flag)
			{
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
			}

			if (flag && (tedition > editionDate || mandatoryUpdate) && _waccess((globalPath + path).c_str(), 0) == 0 && hash_md5 == thash_md5 && hash_sha256 == thash_sha256)
			{
				//符合条件，开始替换版本

				updateVal["old_name"] = Json::Value(utf16ToUtf8(GetCurrentExeName()));
				if (mandatoryUpdate) updateVal["MandatoryUpdate"] = Json::Value(false);

				if (!OccupyFileForWrite(&fileHandle, globalPath + L"installer\\update.json")) flag = false;
				if (flag && SetFilePointer(fileHandle, 0, NULL, FILE_BEGIN) == INVALID_SET_FILE_POINTER) flag = false;
				if (flag && !SetEndOfFile(fileHandle)) flag = false;

				if (flag)
				{
					Json::StreamWriterBuilder writerBuilder;
					string jsonContent = "\xEF\xBB\xBF" + Json::writeString(writerBuilder, updateVal);

					DWORD bytesWritten = 0;
					if (!WriteFile(fileHandle, jsonContent.data(), static_cast<DWORD>(jsonContent.size()), &bytesWritten, NULL) || bytesWritten != jsonContent.size()) flag = false;
				}
				UnOccupyFile(&fileHandle);

				if (flag)
				{
					HINSTANCE hInst = ShellExecuteW(NULL, NULL, (globalPath + path).c_str(), NULL, NULL, SW_SHOWNORMAL);
					if ((INT_PTR)hInst > 32) return 0;
					flag = false;
				}
			}
			else flag = false;

			if (!flag)
			{
				error_code ec;
				filesystem::remove_all(globalPath + L"installer", ec);
			}
		}
	}

	// InkeysSuperTop 阶段
	if (_waccess((globalPath + L"opt\\deploy.json").c_str(), 4) == 0)
	{
		ReadSettingMini();

		HANDLE proc_self = GetCurrentProcess();
		HANDLE tok_self;
		OpenProcessToken(proc_self, TOKEN_ALL_ACCESS, &tok_self);

		if (setlist.plugInSetting.superTop.enable && !hasUiAccess(tok_self))
		{
			while (true)
			{
				bool hasExistsFaild = false;

				error_code ec;
				if (filesystem::exists(globalPath + L"superTop_try.signal", ec))
				{
					string dateContent;
					{
						HANDLE fileHandle = NULL;
						if (!OccupyFileForRead(&fileHandle, globalPath + L"superTop_try.signal"))
						{
							UnOccupyFile(&fileHandle);
							break;
						}

						LARGE_INTEGER fileSize;
						if (!GetFileSizeEx(fileHandle, &fileSize))
						{
							UnOccupyFile(&fileHandle);
							break;
						}

						DWORD dwSize = static_cast<DWORD>(fileSize.QuadPart);
						dateContent = string(dwSize, '\0');

						DWORD bytesRead = 0;
						if (SetFilePointer(fileHandle, 0, NULL, FILE_BEGIN) == INVALID_SET_FILE_POINTER)
						{
							UnOccupyFile(&fileHandle);
							break;
						}
						if (!ReadFile(fileHandle, &dateContent[0], dwSize, &bytesRead, NULL) || bytesRead != dwSize)
						{
							UnOccupyFile(&fileHandle);
							break;
						}

						if (dateContent.compare(0, 3, "\xEF\xBB\xBF") == 0) dateContent = dateContent.substr(3);
						UnOccupyFile(&fileHandle);
					}

					double diff;
					{
						tm tm = {};
						int year, month, day, hour, minute, second;
						if (sscanf_s(dateContent.c_str(), "%d-%d-%d %d:%d:%d", &year, &month, &day, &hour, &minute, &second) != 6)
							break;

						tm.tm_year = year - 1900; // 年份从1900开始
						tm.tm_mon = month - 1;    // 月份0-11
						tm.tm_mday = day;
						tm.tm_hour = hour;
						tm.tm_min = minute;
						tm.tm_sec = second;
						tm.tm_isdst = -1;         // 让系统自动判断夏令时

						time_t old_time = mktime(&tm);
						time_t new_time = time(nullptr);

						diff = difftime(new_time, old_time);
					}

					// 如果小于 10s 则视为同一次尝试，此时宣告尝试失败
					if (diff <= 10) break;
					else hasExistsFaild = true;
				}

				{
					if (!filesystem::exists(globalPath + L"superTop_try.signal", ec) || hasExistsFaild)
					{
						string date;
						{
							auto now = chrono::system_clock::now();
							time_t t = chrono::system_clock::to_time_t(now);
							tm tm; localtime_s(&tm, &t);

							ostringstream oss;
							oss << put_time(&tm, "%Y-%m-%d %H:%M:%S");
							date = oss.str();
						}

						bool isSuccess = false;
						while (true)
						{
							HANDLE fileHandle = NULL;
							if (!OccupyFileForWrite(&fileHandle, globalPath + L"superTop_try.signal"))
							{
								UnOccupyFile(&fileHandle);
								break;
							}
							if (SetFilePointer(fileHandle, 0, NULL, FILE_BEGIN) == INVALID_SET_FILE_POINTER)
							{
								UnOccupyFile(&fileHandle);
								break;
							}
							if (!SetEndOfFile(fileHandle))
							{
								UnOccupyFile(&fileHandle);
								break;
							}

							Json::StreamWriterBuilder writerBuilder;
							string dateContent = "\xEF\xBB\xBF" + date;

							DWORD bytesWritten = 0;
							if (!WriteFile(fileHandle, dateContent.data(), static_cast<DWORD>(dateContent.size()), &bytesWritten, NULL) || bytesWritten != dateContent.size())
							{
								UnOccupyFile(&fileHandle);
								break;
							}

							UnOccupyFile(&fileHandle);

							isSuccess = true;
							break;
						}

						if (!isSuccess)
						{
							HANDLE fileHandle = NULL;
							OccupyFileForWrite(&fileHandle, globalPath + L"superTop_try.signal");
							UnOccupyFile(&fileHandle);
						}
					}

					LaunchSurperTop(LaunchState::commandLine);
				}
				break;
			}
		}

		error_code ec;
		filesystem::remove(globalPath + L"superTop_try.signal", ec);

		hasSuperTop = hasUiAccess(tok_self);
	}

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

		auto IDTLoggerFileSink = std::make_shared<spdlog::sinks::basic_file_sink<std::mutex>>(globalPath + L"log\\idt" + Timestamp + L".log", true);

		spdlog::init_thread_pool(8192, 64);
		IDTLogger = std::make_shared<spdlog::async_logger>("IDTLogger", IDTLoggerFileSink, spdlog::thread_pool(), spdlog::async_overflow_policy::block);

		IDTLogger->set_level(spdlog::level::info);
		IDTLogger->set_pattern("[%l][%H:%M:%S.%e]%v");

		IDTLogger->flush_on(spdlog::level::info);
		IDTLogger->info("[主线程][IdtMain] 日志开始记录 " + utf16ToUtf8(editionDate) + " " + utf16ToUtf8(userId));

		if (LaunchState::crashTry) IDTLogger->warn("[主线程][IdtMain] 发现程序先前发生过崩溃错误");

		//logger->info("");
		//logger->warn("");
		//logger->error("");
		//logger->critical("");
	}
	// DPI初始化
	{
		HMODULE hShcore = LoadLibrary(L"Shcore.dll");
		if (hShcore != NULL)
		{
			typedef HRESULT(WINAPI* LPFNSPDPIA)(PROCESS_DPI_AWARENESS);
			LPFNSPDPIA lSetProcessDpiAwareness = (LPFNSPDPIA)GetProcAddress(hShcore, "SetProcessDpiAwareness");
			if (lSetProcessDpiAwareness != NULL)
			{
				HRESULT hr = lSetProcessDpiAwareness(PROCESS_PER_MONITOR_DPI_AWARE);
				if (!SUCCEEDED(hr))
				{
					IDTLogger->warn("[主线程][IdtMain] 执行SetProcessDpiAwareness失败");
					if (!SetProcessDPIAware()) IDTLogger->error("[主线程][IdtMain] 调用SetProcessDPIAware失败");
				}
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

		IDTLogger->info("[主线程][IdtMain] DPI初始化完成");
	}
	// COM初始化
	HANDLE hActCtx;
	ULONG_PTR ulCookie;
	CoInitializeEx(NULL, COINIT_MULTITHREADED);

	// 显示器信息初始化
	{
		// 显示器检查
		DisplayManagementMain();

		int DisplaysNumberTemp = DisplaysNumber;
		if (DisplaysNumberTemp > 1) IDTLogger->warn("[主线程][IdtMain] 拥有多个显示器");
		IDTLogger->info("[主线程][IdtMain] 显示器信息初始化完成");
	}

	// 配置信息初始化
	{
		// 读取配置文件前初始化操作
		{
			// 软件配置
			{
				setlist.configurationSetting.enable = true;
			}
			// 软件版本
			{
				setlist.enableAutoUpdate = true;
				setlist.UpdateChannel = "LTS";
				{
					setlist.updateArchitecture = utf16ToUtf8(programArchitecture);
					if (setlist.updateArchitecture == "arm64ec") setlist.updateArchitecture = "arm64";
				}
			}
			// 常规
			{
				setlist.startUp = false;

				setlist.SetSkinMode = 0;
				setlist.SkinMode = 1;

				setlist.topSleepTime = 3;
				setlist.RightClickClose = false;

				setlist.BrushRecover = true;
				setlist.RubberRecover = false;
				setlist.regularSetting.moveRecover = false;
				setlist.regularSetting.clickRecover = false;

				setlist.regularSetting.avoidFullScreen = true;
				setlist.regularSetting.teachingSafetyMode = 0;
			}
			// 绘制
			{
				setlist.liftStraighten = false, setlist.waitStraighten = true;
				setlist.pointAdsorption = true;

				setlist.smoothWriting = true;

				{
					setlist.eraserSetting.eraserMode = 0;

					float drawingScale = GetDrawingScale();
					setlist.eraserSetting.eraserSize = static_cast<int>(60 * drawingScale);
				}

				setlist.hideTouchPointer = false;
			}
			// 保存
			{
				setlist.saveSetting.enable = true;
				setlist.saveSetting.saveDays = 2;
			}
			// 性能
			{
				setlist.performanceSetting.preparationQuantity = 2;
				setlist.performanceSetting.drawpadFps = 72;
				setlist.performanceSetting.superDraw = false;
			}
			// 预设
			{
				setlist.presetSetting.memoryWidth = true;
				setlist.presetSetting.memoryColor = false;

				setlist.presetSetting.autoDefaultWidth = true;
				setlist.presetSetting.defaultBrush1Width = 3.0f;
				setlist.presetSetting.defaultHighlighter1Width = 35.0f;
			}

			// 插件
			{
				{
					setlist.shortcutAssistant.correctLnk = true;
					setlist.shortcutAssistant.createLnk = false;
				}
				{
					setlist.plugInSetting.superTop.enable = false;
					setlist.plugInSetting.superTop.indicator = true;
				}
			}
			// 组件
			{
				{
					{
						{
							setlist.component.shortcutButton.appliance.explorer = false;
							setlist.component.shortcutButton.appliance.taskmgr = false;
							setlist.component.shortcutButton.appliance.control = false;
						}
						{
							setlist.component.shortcutButton.system.desktop = false;
							setlist.component.shortcutButton.system.lockWorkStation = false;
						}
						{
							setlist.component.shortcutButton.keyboard.keyboardesc = false;
							setlist.component.shortcutButton.keyboard.keyboardAltF4 = false;
						}
						{
							setlist.component.shortcutButton.linkage.classislandSettings = false;
							setlist.component.shortcutButton.linkage.classislandProfile = false;
							setlist.component.shortcutButton.linkage.classislandClassswap = false;
							setlist.component.shortcutButton.linkage.classislandIslandCaller = false;
						}
					}
				}
			}

			{
				// 获取系统默认语言标识符
				LANGID langId = GetUserDefaultUILanguage();
				// 获取主语言标识符
				WORD primaryLangId = PRIMARYLANGID(langId);
				// 获取子语言标识符
				WORD subLangId = SUBLANGID(langId);

				// 检查是否为中文
				if (primaryLangId == LANG_CHINESE)
				{
					switch (subLangId) {
					case SUBLANG_CHINESE_SIMPLIFIED:
					case SUBLANG_CHINESE_SINGAPORE:
					{
						setlist.selectLanguage = 1;
						break;
					}

					case SUBLANG_CHINESE_TRADITIONAL:
					case SUBLANG_CHINESE_HONGKONG:
					case SUBLANG_CHINESE_MACAU:
					{
						setlist.selectLanguage = 2;
						break;
					}
					}
				}
				else setlist.selectLanguage = 0;
			}
			{
				int digitizerStatus = GetSystemMetrics(SM_DIGITIZER);
				bool hasTouchDevice = (digitizerStatus & NID_READY) && (digitizerStatus & (NID_INTEGRATED_TOUCH | NID_EXTERNAL_TOUCH));
				if (hasTouchDevice)
				{
					if (MainMonitor.MonitorPhyWidth == 0 || MainMonitor.MonitorPhyHeight == 0) setlist.paintDevice = 0, setlist.liftStraighten = true;
					else if (MainMonitor.MonitorPhyWidth * MainMonitor.MonitorPhyHeight >= 1200) setlist.paintDevice = 0, setlist.liftStraighten = true;
					else setlist.paintDevice = 1;
				}
				else setlist.paintDevice = 1;
			}
			{
				//// 获取屏幕设备上下文
				//HDC screen = GetDC(NULL);

				//// 获取屏幕的 DPI 值
				//int dpiX = GetDeviceCaps(screen, LOGPIXELSX);
				//int dpiY = GetDeviceCaps(screen, LOGPIXELSY);

				//// 释放设备上下文
				//ReleaseDC(NULL, screen);

				setlist.settingGlobalScale = 1.0f;
			}
		}

		// 读取配置
		{
			if (_waccess((globalPath + L"opt\\deploy.json").c_str(), 4) == -1)
			{
				IDTLogger->warn("[主线程][IdtMain] 配置信息不存在");

				// 联控测试：start 界面
				// StartForInkeys();
			}
			else ReadSetting();
			WriteSetting();
		}

		// 初次读取配置后的操作
		{
			// 开机自启设定
			{
				bool isStartUp = QueryStartupState(GetCurrentExePath(), L"$Inkeys");
				if (isStartUp != setlist.startUp) SetStartupState(setlist.startUp, GetCurrentExePath(), L"$Inkeys");
			}
			// 皮肤设定
			{
				if (setlist.SetSkinMode == 0) setlist.SkinMode = 1;
				else setlist.SkinMode = setlist.SetSkinMode;
			}
			// 崩溃选项设定
			CrashHandler::SetFlag(setlist.regularSetting.teachingSafetyMode);
		}

		IDTLogger->info("[主线程][IdtMain] 配置信息初始化完成");
	}
	// I18N初始化
	{
		// 先读取完整性的英语文件，在读取配置指定的语言文件
		// 这样如果配置文件缺少某项也能用英语补齐
		I18n::load(1, L"JSON", L"en-US");

		if (setlist.selectLanguage == 1) I18n::load(1, L"JSON", L"zh-CN");
		else if (setlist.selectLanguage == 2) I18n::load(1, L"JSON", L"zh-TW");
		else I18n::load(1, L"JSON", L"en-US");

		IDTLogger->info("[主线程][IdtMain] I18N初始化完成");
	}
	// 插件初始化
	{
		// 桌面快捷方式初始化
		shortcutAssistant.SetShortcut();
		// 启动 DesktopDrawpadBlocker
		StartDesktopDrawpadBlocker();
	}

	// COM 清单加载
	{
		//PptCOM 组件加载
		{
			if (!ExtractResource((globalPath + L"PptCOM.dll").c_str(), L"DLL", MAKEINTRESOURCE(222)))
				IDTLogger->warn("[主线程][IdtMain] 解压PptCOM.dll失败");

			ACTCTX actCtx = { 0 };
			actCtx.cbSize = sizeof(actCtx);
			actCtx.dwFlags = ACTCTX_FLAG_RESOURCE_NAME_VALID | ACTCTX_FLAG_HMODULE_VALID;
			actCtx.lpResourceName = MAKEINTRESOURCE(221);
			actCtx.hModule = GetModuleHandle(NULL);

			hActCtx = CreateActCtx(&actCtx);
			ActivateActCtx(hActCtx, &ulCookie);

			HMODULE hModule = LoadLibraryW((globalPath + L"PptCOM.dll").c_str());
		}

		IDTLogger->info("[主线程][IdtMain] COM初始化完成");
	}
	// 自动更新初始化
	{
#ifdef IDT_RELEASE
		thread(AutomaticUpdate).detach();
#endif
	}

	// 界面绘图库初始化
	{
		D2DStarup();

		IDTLogger->info("[主线程][IdtMain] 界面绘图库初始化完成");
	}
	// 字体初始化
	{
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

		stringFormat.SetAlignment(StringAlignmentCenter);
		stringFormat.SetLineAlignment(StringAlignmentCenter);
		stringFormat.SetFormatFlags(StringFormatFlagsNoWrap);

		stringFormat_left.SetAlignment(StringAlignmentNear);
		stringFormat_left.SetLineAlignment(StringAlignmentNear);
		stringFormat_left.SetFormatFlags(StringFormatFlagsNoWrap);

		IDTLogger->info("[主线程][IdtMain] 字体初始化完成");
	}

	// 窗口
	{
		wstring ClassName;
		if (userId == L"Error") ClassName = L"HiEasyX041";
		else ClassName = userId;

		CreateMagnifierWindow();

		hiex::PreSetWindowStyleEx(WS_EX_NOACTIVATE);
		freeze_window = hiex::initgraph_win32(MainMonitor.MonitorWidth, MainMonitor.MonitorHeight, 0, L"Inkeys5 FreezeWindow", (L"Inkeys1;" + ClassName).c_str(), nullptr, magnifierWindow);

		hiex::PreSetWindowStyleEx(WS_EX_NOACTIVATE);
		drawpad_window = hiex::initgraph_win32(MainMonitor.MonitorWidth, MainMonitor.MonitorHeight, 0, L"Inkeys4 DrawpadWindow", (L"Inkeys2;" + ClassName).c_str(), nullptr, freeze_window);

		SettingWindowBegin();

		hiex::PreSetWindowStyleEx(WS_EX_NOACTIVATE);
		ppt_window = hiex::initgraph_win32(MainMonitor.MonitorWidth, MainMonitor.MonitorHeight, 0, L"Inkeys2 PptWindow", (L"Inkeys4;" + ClassName).c_str(), nullptr, setting_window);
		//ppt_window = hiex::initgraph_win32(MainMonitor.MonitorWidth, MainMonitor.MonitorHeight, 0, L"Inkeys2 PptWindow", (L"Inkeys4;" + ClassName).c_str(), nullptr, drawpad_window);

		hiex::PreSetWindowStyleEx(WS_EX_NOACTIVATE);
		floating_window = hiex::initgraph_win32(background.getwidth(), background.getheight(), 0, L"Inkeys1 FloatingWindow", (L"Inkeys5;" + ClassName).c_str(), nullptr, ppt_window);

		// 画板窗口在注册 RTS 前必须拥有置顶属性，在显示前先进行一次全局置顶
		SetWindowPos(magnifierWindow, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);

		thread TopWindowThread(TopWindow);
		TopWindowThread.detach();

		IDTLogger->info("[主线程][IdtMain] 窗口初始化完成");

		//hiex::init_console();
	}
	// RealTimeStylus触控库
	{
		bool hasErr = false;

		HRESULT hr;
		GUID desiredPacketProperties[] = { GUID_PACKETPROPERTY_GUID_X, GUID_PACKETPROPERTY_GUID_Y, GUID_PACKETPROPERTY_GUID_NORMAL_PRESSURE,GUID_PACKETPROPERTY_GUID_WIDTH, GUID_PACKETPROPERTY_GUID_HEIGHT };

		// Create RTS object
		g_pRealTimeStylus = CreateRealTimeStylus(drawpad_window);
		if (g_pRealTimeStylus == NULL)
		{
			IDTLogger->warn("[主线程][IdtMain] RealTimeStylus 为 NULL");

			hasErr = true;
			goto RealTimeStylusEnd;
		}

		// 设置属性包
		hr = g_pRealTimeStylus->SetDesiredPacketDescription(5, desiredPacketProperties);
		if (FAILED(hr))
		{
			IDTLogger->warn("[主线程][IdtMain] SetDesiredPacketDescription 为 失败");

			g_pRealTimeStylus->Release();
			g_pRealTimeStylus = NULL;

			hasErr = true;
			goto RealTimeStylusEnd;
		}

		// Create EventHandler object
		g_pSyncEventHandlerRTS = CSyncEventHandlerRTS::Create(g_pRealTimeStylus);
		if (g_pSyncEventHandlerRTS == NULL)
		{
			IDTLogger->warn("[主线程][IdtMain] SyncEventHandlerRTS 为 NULL");

			g_pRealTimeStylus->Release();
			g_pRealTimeStylus = NULL;

			hasErr = true;
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

			hasErr = true;
			goto RealTimeStylusEnd;
		}

	RealTimeStylusEnd:

		if (hasErr)
		{
			IDTLogger->critical("[主线程][IdtMain] 程序意外退出：RealTimeStylus 触控库初始化失败。");

			if (LaunchState::warnTry) MessageBox(NULL, L"Program unexpected exit: RealTimeStylus touch library initialization failed.(#4)\n程序意外退出：RealTimeStylus 触控库初始化失败。(#4)", L"Inkeys Error | 智绘教错误", MB_OK | MB_SYSTEMMODAL);
			else ShellExecuteW(NULL, NULL, GetCurrentExePath().c_str(), L"-WarnTry", NULL, SW_SHOWNORMAL);

			exit(0);
		}

		thread RTSSpeed_thread(RTSSpeed);
		RTSSpeed_thread.detach();

		rtsWait = false;
		IDTLogger->info("[主线程][IdtMain] RealTimeStylus触控库初始化完成");
	}
	// 线程
	{
		thread(floating_main).detach();
		//thread([&]() { barInitialization.Initialization(); }).detach();
		thread(SettingMain).detach();
		thread(drawpad_main).detach();
		thread(FreezeFrameWindow).detach();
		thread(StateMonitoring).detach();

		// 放大API
		thread(MagnifierThread).detach();

		// 启动 PPT 联动插件
		thread(PPTLinkageMain).detach();

		IDTLogger->info("[主线程][IdtMain] 线程初始化完成");
	}
	CrashHandler::IsSecond(false);

	IDTLogger->info("[主线程][IdtMain] 开始等待关闭程序信号发出");

	while (!offSignal) this_thread::sleep_for(chrono::milliseconds(500));

	IDTLogger->info("[主线程][IdtMain] 等待各函数线程结束");

	{
		int WaitingCount = 0;
		for (; WaitingCount < 20; WaitingCount++)
		{
			if (!threadStatus[L"floating_main"] && !threadStatus[L"drawpad_main"] && !threadStatus[L"SettingMain"] && !threadStatus[L"FreezeFrameWindow"] && !threadStatus[L"NetUpdate"] && !threadStatus[L"PPTLinkageMain"]) break;
			this_thread::sleep_for(chrono::milliseconds(500));
		}
		if (WaitingCount >= 20) IDTLogger->warn("[主线程][IdtMain] 结束函数线程超时并强制结束线程");
	}

	// 反初始化 COM 环境
	{
		CoUninitialize();

		DeactivateActCtx(0, ulCookie);
		ReleaseActCtx(hActCtx);

		IDTLogger->info("[主线程][IdtMain] 反初始化 COM 环境完成");
	}
	// 释放互斥体
	if (launchMutex)
	{
		// ReleaseMutex(hMutex); // 调用 CloseHandle 通常就够了，因为我们是创建者且未等待它
		CloseHandle(launchMutex);
		launchMutex = NULL;
	}

	if (offSignal == 2) ShellExecuteW(NULL, NULL, GetCurrentExePath().c_str(), L"-Restart", NULL, SW_SHOWNORMAL);

	IDTLogger->info("[主线程][IdtMain] 已结束智绘教所有线程并关闭程序");
	return 0;
}

// 调测专用
#ifndef IDT_RELEASE
void Test()
{
	MessageBoxW(NULL, L"标记处", L"标记", MB_OK | MB_SYSTEMMODAL);
}
void Testb(bool t)
{
	MessageBoxW(NULL, t ? L"true" : L"false", L"真否标记", MB_OK | MB_SYSTEMMODAL);
}
void Testi(long long t)
{
	MessageBoxW(NULL, to_wstring(t).c_str(), L"数值标记", MB_OK | MB_SYSTEMMODAL);
}
void Testd(double t)
{
	MessageBoxW(NULL, to_wstring(t).c_str(), L"浮点标记", MB_OK | MB_SYSTEMMODAL);
}
void Testw(wstring t)
{
	MessageBoxW(NULL, t.c_str(), L"字符标记", MB_OK | MB_SYSTEMMODAL);
}
void Testa(string t)
{
	MessageBoxW(NULL, utf8ToUtf16(t).c_str(), L"字符标记", MB_OK | MB_SYSTEMMODAL);
}
#endif