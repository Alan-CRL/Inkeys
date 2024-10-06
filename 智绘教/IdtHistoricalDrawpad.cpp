#include "IdtHistoricalDrawpad.h"

#include "IdtConfiguration.h"
#include "IdtDisplayManagement.h"
#include "IdtDraw.h"
#include "IdtDrawpad.h"
#include "IdtFloating.h"
#include "IdtFreezeFrame.h"
#include "IdtImage.h"
#include "IdtMagnification.h"
#include "IdtState.h"
#include "IdtText.h"
#include "IdtTime.h"
#include "IdtWindow.h"

void removeEmptyFolders(std::wstring path)
{
	for (const auto& entry : filesystem::directory_iterator(path)) {
		if (entry.is_directory()) {
			removeEmptyFolders(entry.path());
			if (filesystem::is_empty(entry)) {
				filesystem::remove(entry);
			}
		}
	}
}
void removeUnknownFiles(std::wstring path, std::deque<std::wstring> knownFiles)
{
	for (const auto& entry : filesystem::recursive_directory_iterator(path)) {
		if (entry.is_regular_file()) {
			auto it = std::find(knownFiles.begin(), knownFiles.end(), entry.path().wstring());
			if (it == knownFiles.end()) {
				filesystem::remove(entry);
			}
		}
	}
}
deque<wstring> getPrevTwoDays(const std::wstring& date, int day)
{
	deque<wstring> ret;

	std::wistringstream ss(date);
	std::tm t = {};
	ss >> std::get_time(&t, L"%Y-%m-%d");

	for (int i = 1; i <= day; i++)
	{
		std::mktime(&t);
		std::wostringstream os1;
		os1 << std::put_time(&t, L"%Y-%m-%d");
		ret.push_back(os1.str());

		t.tm_mday -= 1;
	}

	return ret;
}

int current_record_pointer, total_record_pointer;
int reference_record_pointer, practical_total_record_pointer;
Json::Value record_value;

// 载入记录
void LoadDrawpad()
{
	if (_waccess((globalPath + L"ScreenShot\\attribute_directory.json").c_str(), 4) == 0)
	{
		Json::Reader reader;

		ifstream readjson;
		readjson.imbue(locale("zh_CN.UTF8"));
		readjson.open((globalPath + L"ScreenShot\\attribute_directory.json").c_str());

		deque<wstring> authenticated_file;
		authenticated_file.push_back(globalPath + L"ScreenShot\\attribute_directory.json");
		if (reader.parse(readjson, record_value))
		{
			readjson.close();

			for (int i = 0; i < (int)record_value["Image_Properties"].size(); i++)
			{
				deque<wstring> date = getPrevTwoDays(CurrentDate(), 5);

				auto it = find(date.begin(), date.end(), utf8ToUtf16(record_value["Image_Properties"][i]["date"].asString()));
				if (it == date.end())
				{
					error_code ec;
					filesystem::remove(utf8ToUtf16(record_value["Image_Properties"][i]["drawpad"].asString()), ec);
					filesystem::remove(utf8ToUtf16(record_value["Image_Properties"][i]["background"].asString()), ec);
					filesystem::remove(utf8ToUtf16(record_value["Image_Properties"][i]["blending"].asString()), ec);

					record_value["Image_Properties"].removeIndex(i, nullptr);
					i--;
				}
				else if (_waccess(utf8ToUtf16(record_value["Image_Properties"][i]["drawpad"].asString()).c_str(), 0) == -1)
				{
					error_code ec;
					filesystem::remove(utf8ToUtf16(record_value["Image_Properties"][i]["drawpad"].asString()).c_str(), ec);
					filesystem::remove(utf8ToUtf16(record_value["Image_Properties"][i]["background"].asString()).c_str(), ec);
					filesystem::remove(utf8ToUtf16(record_value["Image_Properties"][i]["blending"].asString()).c_str(), ec);

					record_value["Image_Properties"].removeIndex(i, nullptr);
					i--;
				}
				else
				{
					authenticated_file.push_back(StringToWstring(ConvertToGbk(record_value["Image_Properties"][i]["drawpad"].asString())));
					authenticated_file.push_back(StringToWstring(ConvertToGbk(record_value["Image_Properties"][i]["background"].asString())));
					authenticated_file.push_back(StringToWstring(ConvertToGbk(record_value["Image_Properties"][i]["blending"].asString())));
				}
			}
			removeUnknownFiles(StringToWstring(globalPath) + L"ScreenShot", authenticated_file);
			removeEmptyFolders(StringToWstring(globalPath) + L"ScreenShot");

			Json::StreamWriterBuilder OutjsonBuilder;
			OutjsonBuilder.settings_["emitUTF8"] = true;
			std::unique_ptr<Json::StreamWriter> writer(OutjsonBuilder.newStreamWriter());
			ofstream writejson;
			writejson.imbue(locale("zh_CN.UTF8"));
			writejson.open(WstringToString(StringToWstring(globalPath) + L"ScreenShot\\attribute_directory.json").c_str());
			writer->write(record_value, &writejson);
			writejson.close();
		}
		else readjson.close();
	}
	else
	{
		//创建路径
		filesystem::create_directory(StringToWstring(globalPath) + L"ScreenShot");

		deque<wstring> authenticated_file;
		authenticated_file.push_back(StringToWstring(globalPath) + L"ScreenShot\\attribute_directory.json");

		removeUnknownFiles(StringToWstring(globalPath) + L"ScreenShot", authenticated_file);
		removeEmptyFolders(StringToWstring(globalPath) + L"ScreenShot");

		record_value["Image_Properties"].append(Json::Value("nullptr"));
		record_value["Image_Properties"].removeIndex(0, nullptr);

		Json::StreamWriterBuilder OutjsonBuilder;
		OutjsonBuilder.settings_["emitUTF8"] = true;
		std::unique_ptr<Json::StreamWriter> writer(OutjsonBuilder.newStreamWriter());
		ofstream writejson;
		writejson.imbue(locale("zh_CN.UTF8"));
		writejson.open(WstringToString(StringToWstring(globalPath) + L"ScreenShot\\attribute_directory.json").c_str());
		writer->write(record_value, &writejson);
		writejson.close();
	}

	current_record_pointer = reference_record_pointer = 1;
	total_record_pointer = practical_total_record_pointer = (int)record_value["Image_Properties"].size();
}
// 保存图像到指定目录
void SaveScreenShot(IMAGE img, bool record_pointer_add)
{
	shared_lock<shared_mutex> DisplaysNumberLock(DisplaysNumberSm);

	wstring date = CurrentDate(), time = CurrentTime(), stamp = getTimestamp();
	if (_waccess((StringToWstring(globalPath) + L"ScreenShot").c_str(), 0 == -1)) CreateDirectory((StringToWstring(globalPath) + L"ScreenShot").c_str(), NULL);
	if (_waccess((StringToWstring(globalPath) + L"ScreenShot\\" + date).c_str(), 0 == -1)) CreateDirectory((StringToWstring(globalPath) + L"ScreenShot\\" + date).c_str(), NULL);

	saveImageToPNG(img, WstringToString(StringToWstring(globalPath) + L"ScreenShot\\" + date + L"\\" + stamp + L".png").c_str(), true, 10);

	/*
	if (magnificationReady)
	{
		RequestUpdateMagWindow = true;
		while (RequestUpdateMagWindow) this_thread::sleep_for(chrono::milliseconds(100));

		std::shared_lock<std::shared_mutex> lock1(MagnificationBackgroundSm);
		IMAGE blending = MagnificationBackground;
		lock1.unlock();
		saveImageToPNG(blending, WstringToString(StringToWstring(globalPath) + L"ScreenShot\\" + date + L"\\" + stamp + L"_background.png").c_str(), false, 10);
	}*/

	//hiex::TransparentImage(&blending, 0, 0, &img);
	//saveImageToJPG(blending, WstringToString(StringToWstring(globalPath) + L"ScreenShot\\" + date + L"\\" + stamp + L"_blending.jpg").c_str(),50);
	//saveimage((StringToWstring(globalPath) + L"ScreenShot\\" + date + L"\\" + stamp + L"_blending.jpg").c_str(), &blending);

	//图像目录书写
	if (_waccess((StringToWstring(globalPath) + L"ScreenShot\\attribute_directory.json").c_str(), 4) == 0)
	{
		{
			Json::Value set;
			set["date"] = Json::Value(ConvertToUtf8(WstringToString(date)));
			set["time"] = Json::Value(ConvertToUtf8(WstringToString(time)));
			set["drawpad"] = Json::Value(ConvertToUtf8(WstringToString(StringToWstring(globalPath) + L"ScreenShot\\" + date + L"\\" + stamp + L".png")));
			//if (magnificationReady) set["background"] = Json::Value(ConvertToUtf8(WstringToString(StringToWstring(globalPath) + L"ScreenShot\\" + date + L"\\" + stamp + L"_background.png")));
			//set["blending"] = Json::Value(ConvertToUtf8(WstringToString(StringToWstring(globalPath) + L"ScreenShot\\" + date + L"\\" + stamp + L"_blending.jpg")));

			record_value["Image_Properties"].insert(0, set);
		}

		Json::StreamWriterBuilder OutjsonBuilder;
		OutjsonBuilder.settings_["emitUTF8"] = true;
		std::unique_ptr<Json::StreamWriter> writer(OutjsonBuilder.newStreamWriter());
		ofstream writejson;
		writejson.imbue(locale("zh_CN.UTF8"));
		writejson.open(WstringToString(StringToWstring(globalPath) + L"ScreenShot\\attribute_directory.json").c_str());
		writer->write(record_value, &writejson);
		writejson.close();
	}
	else
	{
		{
			Json::Value set;
			set["date"] = Json::Value(ConvertToUtf8(WstringToString(date)));
			set["time"] = Json::Value(ConvertToUtf8(WstringToString(time)));
			set["drawpad"] = Json::Value(ConvertToUtf8(WstringToString(StringToWstring(globalPath) + L"ScreenShot\\" + date + L"\\" + stamp + L".png")));
			//if (magnificationReady) set["background"] = Json::Value(ConvertToUtf8(WstringToString(StringToWstring(globalPath) + L"ScreenShot\\" + date + L"\\" + stamp + L"_background.png")));
			//set["blending"] = Json::Value(ConvertToUtf8(WstringToString(StringToWstring(globalPath) + L"ScreenShot\\" + date + L"\\" + stamp + L"_blending.jpg")));

			record_value["Image_Properties"].append(set);
		}

		Json::StreamWriterBuilder OutjsonBuilder;
		OutjsonBuilder.settings_["emitUTF8"] = true;
		std::unique_ptr<Json::StreamWriter> writer(OutjsonBuilder.newStreamWriter());
		ofstream writejson;
		writejson.imbue(locale("zh_CN.UTF8"));
		writejson.open(WstringToString(StringToWstring(globalPath) + L"ScreenShot\\attribute_directory.json").c_str());
		writer->write(record_value, &writejson);
		writejson.close();
	}

	if (record_pointer_add) current_record_pointer++, reference_record_pointer++;
	total_record_pointer = practical_total_record_pointer = (int)record_value["Image_Properties"].size();
}

// 撤回操作
void IdtRecall()
{
	// 标识绘制等待
	{
		unique_lock<shared_mutex> LockdrawWaitingSm(drawWaitingSm);
		drawWaiting = true;
		LockdrawWaitingSm.unlock();
	}
	// 防止绘图时撤回冲突
	{
		std::shared_lock<std::shared_mutex> LockStrokeImageListSm(StrokeImageListSm);
		bool start = !StrokeImageList.empty();
		LockStrokeImageListSm.unlock();

		//正在绘制则取消操作
		if (start)
		{
			// 取消标识绘制等待
			{
				unique_lock<shared_mutex> LockdrawWaitingSm(drawWaitingSm);
				drawWaiting = false;
				LockdrawWaitingSm.unlock();
			}
			return;
		}
	}

	pair<int, int> tmp_recond = make_pair(0, 0);
	int tmp_recall_image_type = 0;
	if (!RecallImage.empty())
	{
		tmp_recond = RecallImage.back().recond;
		tmp_recall_image_type = RecallImage.back().type;

		if (RecallImage.back().type == 2 && stateMode.StateModeSelect != StateModeSelectEnum::IdtSelection && !CompareImagesWithBuffer(&drawpad, &RecallImage.back().img));
		else RecallImage.pop_back();
		deque<RecallStruct>(RecallImage).swap(RecallImage); // 使用swap技巧来释放未使用的内存
	}

	if (!RecallImage.empty())
	{
		drawpad = RecallImage.back().img;
		extreme_point = RecallImage.back().extreme_point;
		recall_image_recond = RecallImage.back().recond.first;
	}
	else if (tmp_recond.first > 10)
	{
		IdtRecovery();
		return;
	}
	else
	{
		if (tmp_recall_image_type == 2)
		{
			IdtRecovery();
			return;
		}

		SetImageColor(drawpad, RGBA(0, 0, 0, 0), true);
		extreme_point.clear();
		recall_image_recond = 0;
		FirstDraw = true;
	}
	SetImageColor(window_background, RGBA(0, 0, 0, 1), true);
	hiex::TransparentImage(&window_background, 0, 0, &drawpad);

	if (stateMode.StateModeSelect != StateModeSelectEnum::IdtSelection)
	{
		// 设置BLENDFUNCTION结构体
		BLENDFUNCTION blend;
		blend.BlendOp = AC_SRC_OVER;
		blend.BlendFlags = 0;
		blend.SourceConstantAlpha = 255; // 设置透明度，0为全透明，255为不透明
		blend.AlphaFormat = AC_SRC_ALPHA; // 使用源图像的alpha通道
		HDC hdcScreen = GetDC(NULL);
		// 调用UpdateLayeredWindow函数更新窗口
		POINT ptSrc = { 0,0 };
		SIZE sizeWnd = { drawpad.getwidth(),drawpad.getheight() };
		POINT ptDst = { 0,0 }; // 设置窗口位置
		UPDATELAYEREDWINDOWINFO ulwi = { 0 };
		ulwi.cbSize = sizeof(ulwi);
		ulwi.hdcDst = hdcScreen;
		ulwi.pptDst = &ptDst;
		ulwi.psize = &sizeWnd;
		ulwi.pptSrc = &ptSrc;
		ulwi.crKey = RGB(255, 255, 255);
		ulwi.pblend = &blend;
		ulwi.dwFlags = ULW_ALPHA;

		// 定义要更新的矩形区域
		ulwi.hdcSrc = GetImageHDC(&window_background);
		UpdateLayeredWindowIndirect(drawpad_window, &ulwi);
	}
	else
	{
		reserve_drawpad = true;

		stateMode.StateModeSelect = StateModeSelectEnum::IdtPen;
	}

	// 取消标识绘制等待
	{
		unique_lock<shared_mutex> LockdrawWaitingSm(drawWaitingSm);
		drawWaiting = false;
		LockdrawWaitingSm.unlock();
	}
	return;
}
// 超级恢复操作
void IdtRecovery()
{
	if (current_record_pointer == total_record_pointer + 1)
	{
		stateMode.StateModeSelect = StateModeSelectEnum::IdtSelection;

		reference_record_pointer = 1;
		return;
	}

	// 标识绘制等待
	{
		unique_lock<shared_mutex> LockdrawWaitingSm(drawWaitingSm);
		drawWaiting = true;
		LockdrawWaitingSm.unlock();
	}
	// 防止绘图时撤回冲突
	{
		std::shared_lock<std::shared_mutex> LockStrokeImageListSm(StrokeImageListSm);
		bool start = !StrokeImageList.empty();
		LockStrokeImageListSm.unlock();

		//正在绘制则取消操作
		if (start)
		{
			// 取消标识绘制等待
			{
				unique_lock<shared_mutex> LockdrawWaitingSm(drawWaitingSm);
				drawWaiting = false;
				LockdrawWaitingSm.unlock();
			}
			return;
		}
	}

	if (_access(ConvertToGbk(record_value["Image_Properties"][current_record_pointer - 1]["drawpad"].asString()).c_str(), 4) == -1)
	{
		// 取消标识绘制等待
		{
			unique_lock<shared_mutex> LockdrawWaitingSm(drawWaitingSm);
			drawWaiting = false;
			LockdrawWaitingSm.unlock();
		}
		return;
	}

	filesystem::path pathObj(ConvertToGbk(record_value["Image_Properties"][current_record_pointer - 1]["drawpad"].asString()));
	wstring file_name1 = pathObj.parent_path().filename().wstring();
	wstring file_name2 = pathObj.stem().wstring();

	// 符合最新的单个时间戳（带有毫秒）格式
	if (regex_match(file_name2, wregex(L"\\d+")))
	{
		chrono::milliseconds ms(_wtoll(file_name2.c_str()));
		time_t tt = std::chrono::system_clock::to_time_t(chrono::time_point<chrono::system_clock>(ms));
		RecallImageTm = *std::localtime(&tt);
	}

	// 符合旧版 时-分-秒 格式
	else if (regex_match(file_name1 + L" " + file_name2, wregex(L"\\d{4}-\\d{2}-\\d{2} \\d{2}-\\d{2}-\\d{2}")))
	{
		std::wistringstream temp_wiss(file_name1 + L" " + file_name2);
		temp_wiss >> std::get_time(&RecallImageTm, L"%Y-%m-%d %H-%M-%S");
	}

	// 日期处理失败
	else RecallImageTm = (tm)(NULL);
	FreezeRecall = 500;

	IMAGE temp;
	loadimage(&temp, StringToWstring(ConvertToGbk(record_value["Image_Properties"][current_record_pointer - 1]["drawpad"].asString())).c_str(), drawpad.getwidth(), drawpad.getheight(), true);
	drawpad = temp, extreme_point = map<pair<int, int>, bool>();

	current_record_pointer++;

	SetImageColor(window_background, RGBA(0, 0, 0, 1), true);
	hiex::TransparentImage(&window_background, 0, 0, &drawpad);

	if (stateMode.StateModeSelect == StateModeSelectEnum::IdtPen)
	{
		// 设置BLENDFUNCTION结构体
		BLENDFUNCTION blend;
		blend.BlendOp = AC_SRC_OVER;
		blend.BlendFlags = 0;
		blend.SourceConstantAlpha = 255; // 设置透明度，0为全透明，255为不透明
		blend.AlphaFormat = AC_SRC_ALPHA; // 使用源图像的alpha通道
		HDC hdcScreen = GetDC(NULL);
		// 调用UpdateLayeredWindow函数更新窗口
		POINT ptSrc = { 0,0 };
		SIZE sizeWnd = { drawpad.getwidth(),drawpad.getheight() };
		POINT ptDst = { 0,0 }; // 设置窗口位置
		UPDATELAYEREDWINDOWINFO ulwi = { 0 };
		ulwi.cbSize = sizeof(ulwi);
		ulwi.hdcDst = hdcScreen;
		ulwi.pptDst = &ptDst;
		ulwi.psize = &sizeWnd;
		ulwi.pptSrc = &ptSrc;
		ulwi.crKey = RGB(255, 255, 255);
		ulwi.pblend = &blend;
		ulwi.dwFlags = ULW_ALPHA;

		// 定义要更新的矩形区域
		ulwi.hdcSrc = GetImageHDC(&window_background);
		UpdateLayeredWindowIndirect(drawpad_window, &ulwi);
	}
	else
	{
		reserve_drawpad = true;

		stateMode.StateModeSelect = StateModeSelectEnum::IdtPen;
	}

	// 取消标识绘制等待
	{
		unique_lock<shared_mutex> LockdrawWaitingSm(drawWaitingSm);
		drawWaiting = false;
		LockdrawWaitingSm.unlock();
	}
	return;
}