#include "IdtHistoricalDrawpad.h"

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

//载入记录
void LoadDrawpad()
{
	if (_waccess((StringToWstring(globalPath) + L"ScreenShot\\attribute_directory.json").c_str(), 4) == 0)
	{
		Json::Reader reader;

		ifstream readjson;
		readjson.imbue(locale("zh_CN.UTF8"));
		readjson.open(WstringToString(StringToWstring(globalPath) + L"ScreenShot\\attribute_directory.json").c_str());

		deque<wstring> authenticated_file;
		authenticated_file.push_back(StringToWstring(globalPath) + L"ScreenShot\\attribute_directory.json");
		if (reader.parse(readjson, record_value))
		{
			readjson.close();

			for (int i = 0; i < (int)record_value["Image_Properties"].size(); i++)
			{
				deque<wstring> date = getPrevTwoDays(CurrentDate(), 5);

				auto it = find(date.begin(), date.end(), StringToWstring(record_value["Image_Properties"][i]["date"].asString()));
				if (it == date.end())
				{
					remove(ConvertToGbk(record_value["Image_Properties"][i]["drawpad"].asString()).c_str());
					remove(ConvertToGbk(record_value["Image_Properties"][i]["background"].asString()).c_str());
					remove(ConvertToGbk(record_value["Image_Properties"][i]["blending"].asString()).c_str());

					record_value["Image_Properties"].removeIndex(i, nullptr);
					i--;
				}
				else if (_waccess(StringToWstring(ConvertToGbk(record_value["Image_Properties"][i]["drawpad"].asString())).c_str(), 0) == -1)
				{
					remove(ConvertToGbk(record_value["Image_Properties"][i]["drawpad"].asString()).c_str());
					remove(ConvertToGbk(record_value["Image_Properties"][i]["background"].asString()).c_str());
					remove(ConvertToGbk(record_value["Image_Properties"][i]["blending"].asString()).c_str());

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
//保存图像到指定目录
void SaveScreenShot(IMAGE img, bool record_pointer_add)
{
	shared_lock<shared_mutex> DisplaysNumberLock(DisplaysNumberSm);
	if (DisplaysNumber != 1)
	{
		DisplaysNumberLock.unlock();
		return;
	}
	DisplaysNumberLock.unlock();

	wstring date = CurrentDate(), time = CurrentTime(), stamp = getTimestamp();
	if (_waccess((StringToWstring(globalPath) + L"ScreenShot").c_str(), 0 == -1)) CreateDirectory((StringToWstring(globalPath) + L"ScreenShot").c_str(), NULL);
	if (_waccess((StringToWstring(globalPath) + L"ScreenShot\\" + date).c_str(), 0 == -1)) CreateDirectory((StringToWstring(globalPath) + L"ScreenShot\\" + date).c_str(), NULL);

	if (FreezeFrame.mode != 1)
	{
		RequestUpdateMagWindow = true;
		while (RequestUpdateMagWindow) Sleep(100);
	}

	std::shared_lock<std::shared_mutex> lock1(MagnificationBackgroundSm);
	IMAGE blending = MagnificationBackground;
	lock1.unlock();

	saveImageToPNG(img, WstringToString(StringToWstring(globalPath) + L"ScreenShot\\" + date + L"\\" + stamp + L".png").c_str(), true, 10);
	saveImageToPNG(blending, WstringToString(StringToWstring(globalPath) + L"ScreenShot\\" + date + L"\\" + stamp + L"_background.png").c_str(), false, 10);

	hiex::TransparentImage(&blending, 0, 0, &img);
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
			set["background"] = Json::Value(ConvertToUtf8(WstringToString(StringToWstring(globalPath) + L"ScreenShot\\" + date + L"\\" + stamp + L"_background.png")));
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
			set["background"] = Json::Value(ConvertToUtf8(WstringToString(StringToWstring(globalPath) + L"ScreenShot\\" + date + L"\\" + stamp + L"_background.png")));
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