#include "IdtDrawpad.h"

bool main_open, draw_content;
int TestMainMode = 1;

RECT DrawGradientLine(HDC hdc, int x1, int y1, int x2, int y2, float width, Color color)
{
	Graphics graphics(hdc);
	graphics.SetSmoothingMode(SmoothingModeHighQuality);

	Pen pen(color);
	pen.SetEndCap(LineCapRound);

	pen.SetWidth(width);
	graphics.DrawLine(&pen, x1, y1, x2, y2);

	// 计算外切矩形
	RECT rect;
	rect.left = LONG(max(0, min(x1, x2) - width));
	rect.top = LONG(max(0, min(y1, y2) - width));
	rect.right = LONG(max(x1, x2) + width);
	rect.bottom = LONG(max(y1, y2) + width);

	return rect;
}
bool checkIntersection(RECT rect1, RECT rect2)
{
	if (rect1.right < rect2.left || rect1.left > rect2.right) {
		return false;
	}
	if (rect1.bottom < rect2.top || rect1.top > rect2.bottom) {
		return false;
	}
	return true;
}
double EuclideanDistance(POINT a, POINT b)
{
	return std::sqrt(std::pow(a.x - b.x, 2) + std::pow(a.y - b.y, 2));
}

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
//保存图像到指定目录
void SaveScreenShot(IMAGE img)
{
	wstring date = CurrentDate(), time = CurrentTime();

	if (_waccess((string_to_wstring(global_path) + L"ScreenShot").c_str(), 0 == -1)) CreateDirectory((string_to_wstring(global_path) + L"ScreenShot").c_str(), NULL);
	if (_waccess((string_to_wstring(global_path) + L"ScreenShot\\" + date).c_str(), 0 == -1)) CreateDirectory((string_to_wstring(global_path) + L"ScreenShot\\" + date).c_str(), NULL);

	/*
	//if (xkl_windows != drawpad_window) Test();
	//bool hide_xkl = (xkl_windows == drawpad_window ? false : true);
	//if (hide_xkl) ShowWindow(xkl_windows, SW_HIDE);
	ShowWindow(floating_window, SW_HIDE);

	int width = GetSystemMetrics(SM_CXSCREEN);
	int height = GetSystemMetrics(SM_CYSCREEN);
	IMAGE desktop(width, height);
	BitBlt(GetImageHDC(&desktop), 0, 0, width, height, GetDC(NULL), 0, 0, SRCCOPY);

	ShowWindow(floating_window, SW_SHOW);
	//if (hide_xkl) ShowWindow(xkl_windows, SW_SHOW);
	*/

	std::shared_lock<std::shared_mutex> lock1(MagnificationBackgroundSm);
	IMAGE blending = MagnificationBackground;
	lock1.unlock();

	saveImageToPNG(img, wstring_to_string(string_to_wstring(global_path) + L"ScreenShot\\" + date + L"\\" + time + L".png").c_str(), true);
	saveImageToPNG(blending, wstring_to_string(string_to_wstring(global_path) + L"ScreenShot\\" + date + L"\\" + time + L"_background.png").c_str());

	hiex::TransparentImage(&blending, 0, 0, &img);
	//saveImageToJPG(blending, wstring_to_string(string_to_wstring(global_path) + L"ScreenShot\\" + date + L"\\" + time + L"_blending.jpg").c_str(),50);
	saveimage((string_to_wstring(global_path) + L"ScreenShot\\" + date + L"\\" + time + L"_blending.jpg").c_str(), &blending);

	//图像目录书写
	if (_waccess((string_to_wstring(global_path) + L"ScreenShot\\attribute_directory.json").c_str(), 4) == 0)
	{
		Json::Reader reader;
		Json::Value root;

		ifstream readjson;
		readjson.imbue(locale("zh_CN.UTF8"));
		readjson.open(wstring_to_string(string_to_wstring(global_path) + L"ScreenShot\\attribute_directory.json").c_str());

		if (reader.parse(readjson, root))
		{
			readjson.close();

			Json::StyledWriter outjson;
			Json::Value set;
			set["date"] = Json::Value(convert_to_utf8(wstring_to_string(date)));
			set["time"] = Json::Value(convert_to_utf8(wstring_to_string(time)));
			set["drawpad"] = Json::Value(convert_to_utf8(wstring_to_string(string_to_wstring(global_path) + L"ScreenShot\\" + date + L"\\" + time + L".png")));
			set["background"] = Json::Value(convert_to_utf8(wstring_to_string(string_to_wstring(global_path) + L"ScreenShot\\" + date + L"\\" + time + L"_background.png")));
			set["blending"] = Json::Value(convert_to_utf8(wstring_to_string(string_to_wstring(global_path) + L"ScreenShot\\" + date + L"\\" + time + L"_blending.jpg")));
			root["Image_Properties"].insert(0, set);

			Json::StreamWriterBuilder OutjsonBuilder;
			OutjsonBuilder.settings_["emitUTF8"] = true;
			std::unique_ptr<Json::StreamWriter> writer(OutjsonBuilder.newStreamWriter());
			ofstream writejson;
			writejson.imbue(locale("zh_CN.UTF8"));
			writejson.open(wstring_to_string(string_to_wstring(global_path) + L"ScreenShot\\attribute_directory.json").c_str());
			writer->write(root, &writejson);
			writejson.close();
		}
		else readjson.close();
	}
	else
	{
		Json::StyledWriter outjson;
		Json::Value root;
		Json::Value set;
		set["date"] = Json::Value(convert_to_utf8(wstring_to_string(date)));
		set["time"] = Json::Value(convert_to_utf8(wstring_to_string(time)));
		set["drawpad"] = Json::Value(convert_to_utf8(wstring_to_string(string_to_wstring(global_path) + L"ScreenShot\\" + date + L"\\" + time + L".png")));
		set["background"] = Json::Value(convert_to_utf8(wstring_to_string(string_to_wstring(global_path) + L"ScreenShot\\" + date + L"\\" + time + L"_background.png")));
		set["blending"] = Json::Value(convert_to_utf8(wstring_to_string(string_to_wstring(global_path) + L"ScreenShot\\" + date + L"\\" + time + L"_blending.jpg")));
		root["Image_Properties"].append(set);

		Json::StreamWriterBuilder OutjsonBuilder;
		OutjsonBuilder.settings_["emitUTF8"] = true;
		std::unique_ptr<Json::StreamWriter> writer(OutjsonBuilder.newStreamWriter());
		ofstream writejson;
		writejson.imbue(locale("zh_CN.UTF8"));
		writejson.open(wstring_to_string(string_to_wstring(global_path) + L"ScreenShot\\attribute_directory.json").c_str());
		writer->write(root, &writejson);
		writejson.close();
	}
}

void ControlTestMain()
{
	ExMessage m;
	while (!off_signal)
	{
		hiex::getmessage_win32(&m, EM_MOUSE, test_window);

		if (TestMainMode == 1 && IsInRect(m.x, m.y, { 220, 625, 980, 675 }))
		{
			if (m.lbutton)
			{
				int lx = m.x, ly = m.y;
				while (1)
				{
					ExMessage m = hiex::getmessage_win32(EM_MOUSE, test_window);
					if (IsInRect(m.x, m.y, { 20, 625, 780, 675 }))
					{
						if (!m.lbutton)
						{
							if (setlist.experimental_functions) setlist.experimental_functions = false;
							else setlist.experimental_functions = true;

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

							break;
						}
					}
					else
					{
						hiex::flushmessage_win32(EM_MOUSE, test_window);

						break;
					}
				}
				hiex::flushmessage_win32(EM_MOUSE, test_window);
			}
		}

		else if (IsInRect(m.x, m.y, { 10, 15, 190, 75 }))
		{
			if (m.lbutton)
			{
				int lx = m.x, ly = m.y;
				while (1)
				{
					ExMessage m = hiex::getmessage_win32(EM_MOUSE, test_window);
					if (IsInRect(m.x, m.y, { 10, 15, 190, 75 }))
					{
						if (!m.lbutton)
						{
							TestMainMode = 1;

							break;
						}
					}
					else
					{
						hiex::flushmessage_win32(EM_MOUSE, test_window);

						break;
					}
				}
				hiex::flushmessage_win32(EM_MOUSE, test_window);
			}
		}
		else if (IsInRect(m.x, m.y, { 10, 85, 190, 145 }))
		{
			if (m.lbutton)
			{
				int lx = m.x, ly = m.y;
				while (1)
				{
					ExMessage m = hiex::getmessage_win32(EM_MOUSE, test_window);
					if (IsInRect(m.x, m.y, { 10, 85, 190, 145 }))
					{
						if (!m.lbutton)
						{
							TestMainMode = 2;

							break;
						}
					}
					else
					{
						hiex::flushmessage_win32(EM_MOUSE, test_window);

						break;
					}
				}
				hiex::flushmessage_win32(EM_MOUSE, test_window);
			}
		}
		else if (IsInRect(m.x, m.y, { 10, 155, 190, 215 }))
		{
			if (m.lbutton)
			{
				int lx = m.x, ly = m.y;
				while (1)
				{
					ExMessage m = hiex::getmessage_win32(EM_MOUSE, test_window);
					if (IsInRect(m.x, m.y, { 10, 155, 190, 215 }))
					{
						if (!m.lbutton)
						{
							TestMainMode = 3;

							break;
						}
					}
					else
					{
						hiex::flushmessage_win32(EM_MOUSE, test_window);

						break;
					}
				}
				hiex::flushmessage_win32(EM_MOUSE, test_window);
			}
		}
	}
}

LRESULT CALLBACK TestWndProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
	switch (uMsg)
	{
	case WM_CLOSE:
		return 0;

	default:
		return HIWINDOW_DEFAULT_PROC;
	}
	return 0;
}
int test_main()
{
	this_thread::sleep_for(chrono::seconds(3));
	while (!already) this_thread::sleep_for(chrono::milliseconds(50));

	thread_status[L"test_main"] = true;

	loadimage(&test_sign[1], L"PNG", L"sign2");
	loadimage(&test_sign[2], L"PNG", L"sign3");
	loadimage(&test_sign[3], L"PNG", L"sign4");

	IMAGE test_icon[5];
	loadimage(&test_icon[1], L"PNG", L"test_icon1");
	loadimage(&test_icon[2], L"PNG", L"test_icon2");
	loadimage(&test_icon[3], L"PNG", L"test_icon3");

	DisableResizing(test_window, true);//禁止窗口拉伸
	DisableSystemMenu(test_window, true);

	LONG style = GetWindowLong(test_window, GWL_STYLE);
	style &= ~WS_SYSMENU;
	SetWindowLong(test_window, GWL_STYLE, style);

	hiex::SetWndProcFunc(test_window, TestWndProc);

	// 设置BLENDFUNCTION结构体
	BLENDFUNCTION blend;
	blend.BlendOp = AC_SRC_OVER;
	blend.BlendFlags = 0;
	blend.SourceConstantAlpha = 255; // 设置透明度，0为全透明，255为不透明
	blend.AlphaFormat = AC_SRC_ALPHA; // 使用源图像的alpha通道
	HDC hdcScreen = GetDC(NULL);
	// 调用UpdateLayeredWindow函数更新窗口
	POINT ptSrc = { 0,0 };
	SIZE sizeWnd = { 1010, 750 };
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
	LONG nRet = ::GetWindowLong(test_window, GWL_EXSTYLE);
	nRet |= WS_EX_LAYERED;
	::SetWindowLong(test_window, GWL_EXSTYLE, nRet);

	thread ControlTestMain_thread(ControlTestMain);
	ControlTestMain_thread.detach();

	POINT pt;
	IMAGE test_title(1010, 750), test_background(1010, 710), test_content(800, 700);

	magnificationWindowReady++;
	while (!off_signal)
	{
		Sleep(500);

		if (test.select == true)
		{
			wstring ppt_LinkTest = LinkTest();
			wstring ppt_IsPptDependencyLoaded = IsPptDependencyLoaded();
			Graphics graphics(GetImageHDC(&test_content));

			if (!IsWindowVisible(test_window)) ShowWindow(test_window, SW_SHOW);

			while (!off_signal)
			{
				if (!choose.select) SetWindowPos(test_window, drawpad_window, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
				else if (ppt_show != NULL) SetWindowPos(test_window, ppt_window, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
				else SetWindowPos(test_window, floating_window, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);

				SetImageColor(test_background, RGBA(0, 0, 0, 0), true);
				hiex::EasyX_Gdiplus_SolidRoundRect(0, 0, 1000, 700, 20, 20, RGBA(245, 245, 245, 255), false, SmoothingModeHighQuality, &test_background);

				if (TestMainMode == 1)
				{
					SetImageColor(test_content, RGBA(0, 0, 0, 0), true);
					hiex::EasyX_Gdiplus_SolidRoundRect(0, 0, 800, 700, 20, 20, RGBA(255, 255, 255, 255), false, SmoothingModeHighQuality, &test_content);

					{
						Gdiplus::Font gp_font(&HarmonyOS_fontFamily, 20, FontStyleRegular, UnitPixel);
						SolidBrush WordBrush(hiex::ConvertToGdiplusColor(RGBA(0, 0, 0, 255), false));
						graphics.SetTextRenderingHint(TextRenderingHintAntiAliasGridFit);
						{
							dwords_rect.left = 0;
							dwords_rect.top = 0;
							dwords_rect.right = 800;
							dwords_rect.bottom = 30;
						}
						graphics.DrawString(L"关闭此页面需再次点击 选项 按钮", -1, &gp_font, hiex::RECTToRectF(dwords_rect), &stringFormat, &WordBrush);
					}

					hiex::TransparentImage(&test_content, 50, 140, &test_sign[2]);

					if (!server_feedback.empty() && server_feedback != "")
					{
						Gdiplus::Font gp_font(&HarmonyOS_fontFamily, 20, FontStyleRegular, UnitPixel);
						SolidBrush WordBrush(hiex::ConvertToGdiplusColor(RGBA(0, 0, 0, 255), false));
						graphics.SetTextRenderingHint(TextRenderingHintAntiAliasGridFit);
						{
							dwords_rect.left = 0;
							dwords_rect.top = 480;
							dwords_rect.right = 800;
							dwords_rect.bottom = 560;
						}
						graphics.DrawString(string_to_wstring(server_feedback).c_str(), -1, &gp_font, hiex::RECTToRectF(dwords_rect), &stringFormat, &WordBrush);
					}

					if (AutomaticUpdateStep == 0)
					{
						hiex::EasyX_Gdiplus_RoundRect(20, 560, 760, 50, 20, 20, RGBA(130, 130, 130, 255), 2, false, SmoothingModeHighQuality, &test_content);

						Gdiplus::Font gp_font(&HarmonyOS_fontFamily, 25, FontStyleRegular, UnitPixel);
						SolidBrush WordBrush(hiex::ConvertToGdiplusColor(RGBA(130, 130, 130, 255), false));
						graphics.SetTextRenderingHint(TextRenderingHintAntiAliasGridFit);
						{
							dwords_rect.left = 20;
							dwords_rect.top = 560;
							dwords_rect.right = 20 + 760;
							dwords_rect.bottom = 560 + 50;
						}
						graphics.DrawString(L"程序自动更新待启用", -1, &gp_font, hiex::RECTToRectF(dwords_rect), &stringFormat, &WordBrush);
					}
					else if (AutomaticUpdateStep == 1)
					{
						hiex::EasyX_Gdiplus_RoundRect(20, 560, 760, 50, 20, 20, RGBA(106, 156, 45, 255), 2, false, SmoothingModeHighQuality, &test_content);

						Gdiplus::Font gp_font(&HarmonyOS_fontFamily, 25, FontStyleRegular, UnitPixel);
						SolidBrush WordBrush(hiex::ConvertToGdiplusColor(RGBA(106, 156, 45, 255), false));
						graphics.SetTextRenderingHint(TextRenderingHintAntiAliasGridFit);
						{
							dwords_rect.left = 20;
							dwords_rect.top = 560;
							dwords_rect.right = 20 + 760;
							dwords_rect.bottom = 560 + 50;
						}
						graphics.DrawString(L"程序版本已经是最新", -1, &gp_font, hiex::RECTToRectF(dwords_rect), &stringFormat, &WordBrush);
					}
					else if (AutomaticUpdateStep == 2)
					{
						hiex::EasyX_Gdiplus_RoundRect(20, 560, 760, 50, 20, 20, RGBA(245, 166, 35, 255), 2, false, SmoothingModeHighQuality, &test_content);

						Gdiplus::Font gp_font(&HarmonyOS_fontFamily, 25, FontStyleRegular, UnitPixel);
						SolidBrush WordBrush(hiex::ConvertToGdiplusColor(RGBA(245, 166, 35, 255), false));
						graphics.SetTextRenderingHint(TextRenderingHintAntiAliasGridFit);
						{
							dwords_rect.left = 20;
							dwords_rect.top = 560;
							dwords_rect.right = 20 + 760;
							dwords_rect.bottom = 560 + 50;
						}
						graphics.DrawString(L"新版本排队下载中", -1, &gp_font, hiex::RECTToRectF(dwords_rect), &stringFormat, &WordBrush);
					}
					else if (AutomaticUpdateStep == 3)
					{
						hiex::EasyX_Gdiplus_RoundRect(20, 560, 760, 50, 20, 20, RGBA(106, 156, 45, 255), 2, false, SmoothingModeHighQuality, &test_content);

						Gdiplus::Font gp_font(&HarmonyOS_fontFamily, 25, FontStyleRegular, UnitPixel);
						SolidBrush WordBrush(hiex::ConvertToGdiplusColor(RGBA(106, 156, 45, 255), false));
						graphics.SetTextRenderingHint(TextRenderingHintAntiAliasGridFit);
						{
							dwords_rect.left = 20;
							dwords_rect.top = 560;
							dwords_rect.right = 20 + 760;
							dwords_rect.bottom = 560 + 50;
						}
						graphics.DrawString(L"重启软件以更新到最新版本", -1, &gp_font, hiex::RECTToRectF(dwords_rect), &stringFormat, &WordBrush);
					}

					if (setlist.experimental_functions)
					{
						hiex::EasyX_Gdiplus_RoundRect(20, 620, 760, 50, 20, 20, RGBA(0, 111, 225, 255), 2, false, SmoothingModeHighQuality, &test_content);

						Gdiplus::Font gp_font(&HarmonyOS_fontFamily, 25, FontStyleRegular, UnitPixel);
						SolidBrush WordBrush(hiex::ConvertToGdiplusColor(RGBA(0, 111, 225, 255), false));
						graphics.SetTextRenderingHint(TextRenderingHintAntiAliasGridFit);
						{
							dwords_rect.left = 20;
							dwords_rect.top = 620;
							dwords_rect.right = 20 + 760;
							dwords_rect.bottom = 620 + 50;
						}
						graphics.DrawString(L"程序实验性功能 已启用（点击禁用）", -1, &gp_font, hiex::RECTToRectF(dwords_rect), &stringFormat, &WordBrush);
					}
					else
					{
						hiex::EasyX_Gdiplus_RoundRect(20, 620, 760, 50, 20, 20, RGBA(130, 130, 130, 255), 2, false, SmoothingModeHighQuality, &test_content);

						Gdiplus::Font gp_font(&HarmonyOS_fontFamily, 25, FontStyleRegular, UnitPixel);
						SolidBrush WordBrush(hiex::ConvertToGdiplusColor(RGBA(130, 130, 130, 255), false));
						graphics.SetTextRenderingHint(TextRenderingHintAntiAliasGridFit);
						{
							dwords_rect.left = 20;
							dwords_rect.top = 620;
							dwords_rect.right = 20 + 760;
							dwords_rect.bottom = 620 + 50;
						}
						graphics.DrawString(L"程序实验性功能 已禁用（点击启用）", -1, &gp_font, hiex::RECTToRectF(dwords_rect), &stringFormat, &WordBrush);
					}
				}
				if (TestMainMode == 2)
				{
					SetImageColor(test_content, RGBA(0, 0, 0, 0), true);
					hiex::EasyX_Gdiplus_SolidRoundRect(0, 0, 800, 700, 20, 20, RGBA(255, 255, 255, 255), false, SmoothingModeHighQuality, &test_content);

					{
						Gdiplus::Font gp_font(&HarmonyOS_fontFamily, 20, FontStyleRegular, UnitPixel);
						SolidBrush WordBrush(hiex::ConvertToGdiplusColor(RGBA(0, 0, 0, 255), false));
						graphics.SetTextRenderingHint(TextRenderingHintAntiAliasGridFit);
						{
							dwords_rect.left = 0;
							dwords_rect.top = 0;
							dwords_rect.right = 800;
							dwords_rect.bottom = 30;
						}
						graphics.DrawString(L"关闭此页面需再次点击 选项 按钮", -1, &gp_font, hiex::RECTToRectF(dwords_rect), &stringFormat, &WordBrush);
					}

					Gdiplus::Font gp_font(&HarmonyOS_fontFamily, 18, FontStyleRegular, UnitPixel);
					SolidBrush WordBrush(hiex::ConvertToGdiplusColor(RGB(0, 0, 0), false));
					graphics.SetTextRenderingHint(TextRenderingHintAntiAliasGridFit);
					{
						dwords_rect.left = 0;
						dwords_rect.top = 0;
						dwords_rect.right = 800;
						dwords_rect.bottom = 700;
					}

					GetCursorPos(&pt);

					wstring text = L"\n\n鼠标左键按下：";
					text += KEY_DOWN(VK_LBUTTON) ? L"是" : L"否";
					text += L"\n光标坐标 " + to_wstring(pt.x) + L"," + to_wstring(pt.y);

					text += L"\n\n此程序使用 RealTimeStylus 触控库（测试触控需进入画笔模式）";
					text += L"\n其兼容 WinXP 及以上版本的系统，效率较高，支持多点触控";

					if (uRealTimeStylus == 2) text += L"\n\nRealTimeStylus 消息：按下";
					else if (uRealTimeStylus == 3) text += L"\n\nRealTimeStylus 消息：抬起";
					else if (uRealTimeStylus == 4) text += L"\n\nRealTimeStylus 消息：移动";
					else text += L"\n\nRealTimeStylus 消息：就绪";

					text += L"\n触控按下：";
					text += touchDown ? L"是" : L"否";
					text += L"\n按下触控点数量：";
					text += to_wstring(touchNum);

					for (int i = 0; i < touchNum; i++)
					{
						std::shared_lock<std::shared_mutex> lock1(PointPosSm);
						POINT pt = TouchPos[TouchList[i]].pt;
						lock1.unlock();

						std::shared_lock<std::shared_mutex> lock2(TouchSpeedSm);
						double speed = TouchSpeed[TouchList[i]];
						lock2.unlock();

						text += L"\n触控点" + to_wstring(i + 1) + L" pid" + to_wstring(TouchList[i]) + L" 坐标" + to_wstring(pt.x) + L"," + to_wstring(pt.y) + L" 速度" + to_wstring(speed);
					}

					text += L"\n\nTouchList ";
					for (const auto& val : TouchList)
					{
						text += to_wstring(val) + L" ";
					}
					text += L"\nTouchTemp ";
					for (size_t i = 0; i < TouchTemp.size(); ++i)
					{
						text += to_wstring(TouchTemp[i].pid) + L" ";
					}

					text += L"\n\n撤回库当前大小：" + to_wstring(max(0, int(RecallImage.size()) - 1));

					text += L"\n\nCOM二进制接口 联动组件 状态：\n";
					text += ppt_LinkTest;
					text += L"\nPPT 联动组件状态：";
					text += ppt_IsPptDependencyLoaded;

					text += L"\n\nPPT 状态：";
					text += ppt_info_stay.TotalPage != -1 ? L"正在播放" : L"未播放";
					text += L"\nPPT 总页面数：";
					text += to_wstring(ppt_info_stay.TotalPage);
					text += L"\nPPT 当前页序号：";
					text += to_wstring(ppt_info_stay.CurrentPage);

					graphics.DrawString(text.c_str(), -1, &gp_font, hiex::RECTToRectF(dwords_rect), &stringFormat, &WordBrush);
				}
				if (TestMainMode == 3)
				{
					SetImageColor(test_content, RGBA(0, 0, 0, 0), true);
					hiex::EasyX_Gdiplus_SolidRoundRect(0, 0, 800, 700, 20, 20, RGBA(255, 255, 255, 255), false, SmoothingModeHighQuality, &test_content);

					hiex::TransparentImage(&test_content, 100, 20, &test_sign[1]);
					{
						Gdiplus::Font gp_font(&HarmonyOS_fontFamily, 20, FontStyleRegular, UnitPixel);
						SolidBrush WordBrush(hiex::ConvertToGdiplusColor(RGBA(0, 0, 0, 255), false));
						graphics.SetTextRenderingHint(TextRenderingHintAntiAliasGridFit);
						{
							dwords_rect.left = 0;
							dwords_rect.top = 0;
							dwords_rect.right = 800;
							dwords_rect.bottom = 30;
						}
						graphics.DrawString(L"关闭此页面需再次点击 选项 按钮", -1, &gp_font, hiex::RECTToRectF(dwords_rect), &stringFormat, &WordBrush);
					}

					Gdiplus::Font gp_font(&HarmonyOS_fontFamily, 18, FontStyleRegular, UnitPixel);
					SolidBrush WordBrush(hiex::ConvertToGdiplusColor(RGB(0, 0, 0), false));
					graphics.SetTextRenderingHint(TextRenderingHintAntiAliasGridFit);
					{
						dwords_rect.left = 0;
						dwords_rect.top = 305;
						dwords_rect.right = 800;
						dwords_rect.bottom = 700;
					}

					wstring text = L"程序版本：" + string_to_wstring(edition_code);
					text += L"\n程序发布版本：" + string_to_wstring(edition_date) + L" 测试分支（将于 10月20日 前切换到默认分支）";
					text += L"\n程序构建时间：" + buildTime;
					text += L"\n用户ID：" + userid;

					text += L"\n\n更新通道：LTS";
					if (!server_code.empty() && server_code != "")
					{
						text += L"\n联网版本代号：" + string_to_wstring(server_code);
						if (server_code == "GWSR") text += L" （广外专用版本）";
					}
					if (procedure_updata_error == 1) text += L"\n程序自动更新：已启用";
					else if (procedure_updata_error == 2) text += L"\n程序自动更新：发生网络错误";
					else text += L"\n程序自动更新：载入中（等待服务器反馈）";

					if (!server_feedback.empty() && server_feedback != "") text += L"\n服务器反馈信息：" + string_to_wstring(server_feedback);
					if (server_updata_error)
					{
						text += L"\n\n服务器通信错误：Error" + to_wstring(server_updata_error);
						if (!server_updata_error_reason.empty()) text += L"\n" + server_updata_error_reason;
					}

					/*
					GetCursorPos(&pt);

					text += L"\n\n鼠标左键按下：";
					text += KEY_DOWN(VK_LBUTTON) ? L"是" : L"否";
					text += L"\n光标坐标 " + to_wstring(pt.x) + L"," + to_wstring(pt.y);

					text += L"\n\n此程序使用 RealTimeStylus 触控库（测试触控需进入画笔模式）";
					text += L"\n其兼容 WinXP 及以上版本的系统，效率较高，支持多点触控";

					if (uRealTimeStylus == 2) text += L"\n\nRealTimeStylus 消息：按下";
					else if (uRealTimeStylus == 3) text += L"\n\nRealTimeStylus 消息：抬起";
					else if (uRealTimeStylus == 4) text += L"\n\nRealTimeStylus 消息：移动";
					else text += L"\n\nRealTimeStylus 消息：就绪";

					text += L"\n触控按下：";
					text += touchDown ? L"是" : L"否";
					text += L"\n按下触控点数量：";
					text += to_wstring(touchNum);

					for (int i = 0; i < 20; i++)
					{
						if (touchNum > i)
						{
							pt = TouchPos[TouchList[i]].pt;
							text += L"\n触控点" + to_wstring(i + 1) + L" pid" + to_wstring(TouchList[i]) + L" 坐标 " + to_wstring(pt.x) + L"," + to_wstring(pt.y);
						}
						else break;
					}

					text += L"\n\nTouchList ";
					for (const auto& val : TouchList)
					{
						text += to_wstring(val) + L" ";
					}
					text += L"\nTouchTemp ";
					for (size_t i = 0; i < TouchTemp.size(); ++i)
					{
						text += to_wstring(TouchTemp[i].pid) + L" ";
					}

					text += L"\n\n撤回库当前大小：" + to_wstring(max(0, int(RecallImage.size()) - 1));

					text += L"\n\nCOM二进制接口 联动组件 状态：\n";
					text += ppt_LinkTest;
					text += L"\nPPT 联动组件状态：";
					text += ppt_IsPptDependencyLoaded;

					text += L"\n\nPPT 状态：";
					text += ppt_info_stay.TotalPage != -1 ? L"正在播放" : L"未播放";
					text += L"\nPPT 总页面数：";
					text += to_wstring(ppt_info_stay.TotalPage);
					text += L"\nPPT 当前页序号：";
					text += to_wstring(ppt_info_stay.CurrentPage);
					*/

					graphics.DrawString(text.c_str(), -1, &gp_font, hiex::RECTToRectF(dwords_rect), &stringFormat, &WordBrush);
				}

				//侧栏
				{
					Graphics graphics(GetImageHDC(&test_background));

					if (TestMainMode == 1)
					{
						hiex::EasyX_Gdiplus_FillRoundRect(10, 10, 180, 60, 20, 20, RGBA(0, 111, 225, 255), RGBA(230, 230, 230, 255), 3, false, SmoothingModeHighQuality, &test_background);

						ChangeColor(test_icon[1], RGBA(0, 111, 225, 255));
						hiex::TransparentImage(&test_background, 20, 20, &test_icon[1]);

						Gdiplus::Font gp_font(&HarmonyOS_fontFamily, 24, FontStyleRegular, UnitPixel);
						SolidBrush WordBrush(hiex::ConvertToGdiplusColor(RGBA(0, 111, 225, 255), false));
						graphics.SetTextRenderingHint(TextRenderingHintAntiAliasGridFit);
						{
							dwords_rect.left = 70;
							dwords_rect.top = 10;
							dwords_rect.right = 70 + 110;
							dwords_rect.bottom = 10 + 63;
						}
						graphics.DrawString(L"主页", -1, &gp_font, hiex::RECTToRectF(dwords_rect), &stringFormat, &WordBrush);
					}
					else
					{
						hiex::EasyX_Gdiplus_FillRoundRect(10, 10, 180, 60, 20, 20, RGBA(230, 230, 230, 255), RGBA(230, 230, 230, 255), 3, false, SmoothingModeHighQuality, &test_background);

						ChangeColor(test_icon[1], RGBA(130, 130, 130, 255));
						hiex::TransparentImage(&test_background, 20, 20, &test_icon[1]);

						Gdiplus::Font gp_font(&HarmonyOS_fontFamily, 24, FontStyleRegular, UnitPixel);
						SolidBrush WordBrush(hiex::ConvertToGdiplusColor(RGBA(130, 130, 130, 255), false));
						graphics.SetTextRenderingHint(TextRenderingHintAntiAliasGridFit);
						{
							dwords_rect.left = 70;
							dwords_rect.top = 10;
							dwords_rect.right = 70 + 110;
							dwords_rect.bottom = 10 + 63;
						}
						graphics.DrawString(L"主页", -1, &gp_font, hiex::RECTToRectF(dwords_rect), &stringFormat, &WordBrush);
					}

					if (TestMainMode == 2)
					{
						hiex::EasyX_Gdiplus_FillRoundRect(10, 80, 180, 60, 20, 20, RGBA(0, 111, 225, 255), RGBA(230, 230, 230, 255), 3, false, SmoothingModeHighQuality, &test_background);

						ChangeColor(test_icon[2], RGBA(0, 111, 225, 255));
						hiex::TransparentImage(&test_background, 20, 90, &test_icon[2]);

						Gdiplus::Font gp_font(&HarmonyOS_fontFamily, 24, FontStyleRegular, UnitPixel);
						SolidBrush WordBrush(hiex::ConvertToGdiplusColor(RGBA(0, 111, 225, 255), false));
						graphics.SetTextRenderingHint(TextRenderingHintAntiAliasGridFit);
						{
							dwords_rect.left = 70;
							dwords_rect.top = 80;
							dwords_rect.right = 70 + 110;
							dwords_rect.bottom = 80 + 63;
						}
						graphics.DrawString(L"调测", -1, &gp_font, hiex::RECTToRectF(dwords_rect), &stringFormat, &WordBrush);
					}
					else
					{
						hiex::EasyX_Gdiplus_FillRoundRect(10, 80, 180, 60, 20, 20, RGBA(230, 230, 230, 255), RGBA(230, 230, 230, 255), 3, false, SmoothingModeHighQuality, &test_background);

						ChangeColor(test_icon[2], RGBA(130, 130, 130, 255));
						hiex::TransparentImage(&test_background, 20, 90, &test_icon[2]);

						Gdiplus::Font gp_font(&HarmonyOS_fontFamily, 24, FontStyleRegular, UnitPixel);
						SolidBrush WordBrush(hiex::ConvertToGdiplusColor(RGBA(130, 130, 130, 255), false));
						graphics.SetTextRenderingHint(TextRenderingHintAntiAliasGridFit);
						{
							dwords_rect.left = 70;
							dwords_rect.top = 80;
							dwords_rect.right = 70 + 110;
							dwords_rect.bottom = 80 + 63;
						}
						graphics.DrawString(L"调测", -1, &gp_font, hiex::RECTToRectF(dwords_rect), &stringFormat, &WordBrush);
					}

					if (TestMainMode == 3)
					{
						hiex::EasyX_Gdiplus_FillRoundRect(10, 150, 180, 60, 20, 20, RGBA(0, 111, 225, 255), RGBA(230, 230, 230, 255), 3, false, SmoothingModeHighQuality, &test_background);

						ChangeColor(test_icon[3], RGBA(0, 111, 225, 255));
						hiex::TransparentImage(&test_background, 20, 160, &test_icon[3]);

						Gdiplus::Font gp_font(&HarmonyOS_fontFamily, 24, FontStyleRegular, UnitPixel);
						SolidBrush WordBrush(hiex::ConvertToGdiplusColor(RGBA(0, 111, 225, 255), false));
						graphics.SetTextRenderingHint(TextRenderingHintAntiAliasGridFit);
						{
							dwords_rect.left = 70;
							dwords_rect.top = 150;
							dwords_rect.right = 70 + 110;
							dwords_rect.bottom = 150 + 63;
						}
						graphics.DrawString(L"关于", -1, &gp_font, hiex::RECTToRectF(dwords_rect), &stringFormat, &WordBrush);
					}
					else
					{
						hiex::EasyX_Gdiplus_FillRoundRect(10, 150, 180, 60, 20, 20, RGBA(230, 230, 230, 255), RGBA(230, 230, 230, 255), 3, false, SmoothingModeHighQuality, &test_background);

						ChangeColor(test_icon[3], RGBA(130, 130, 130, 255));
						hiex::TransparentImage(&test_background, 20, 160, &test_icon[3]);

						Gdiplus::Font gp_font(&HarmonyOS_fontFamily, 24, FontStyleRegular, UnitPixel);
						SolidBrush WordBrush(hiex::ConvertToGdiplusColor(RGBA(130, 130, 130, 255), false));
						graphics.SetTextRenderingHint(TextRenderingHintAntiAliasGridFit);
						{
							dwords_rect.left = 70;
							dwords_rect.top = 150;
							dwords_rect.right = 70 + 110;
							dwords_rect.bottom = 150 + 63;
						}
						graphics.DrawString(L"关于", -1, &gp_font, hiex::RECTToRectF(dwords_rect), &stringFormat, &WordBrush);
					}
				}
				//标题栏
				{
					SetImageColor(test_title, RGBA(0, 0, 0, 0), true);
					hiex::EasyX_Gdiplus_SolidRoundRect(5, 5, 1000, 740, 20, 20, RGBA(0, 111, 225, 200), true, SmoothingModeHighQuality, &test_title);

					Graphics graphics(GetImageHDC(&test_title));
					Gdiplus::Font gp_font(&HarmonyOS_fontFamily, 22, FontStyleRegular, UnitPixel);
					SolidBrush WordBrush(hiex::ConvertToGdiplusColor(RGBA(255, 255, 255, 255), false));
					graphics.SetTextRenderingHint(TextRenderingHintAntiAliasGridFit);
					{
						words_rect.left = 5;
						words_rect.top = 5;
						words_rect.right = 1005;
						words_rect.bottom = 50;
					}
					graphics.DrawString(L"智绘教 程序选项", -1, &gp_font, hiex::RECTToRectF(words_rect), &stringFormat, &WordBrush);
				}
				hiex::TransparentImage(&test_background, 200, 0, &test_content);
				hiex::TransparentImage(&test_title, 5, 45, &test_background);
				hiex::EasyX_Gdiplus_RoundRect(5, 5, 1000, 740, 20, 20, RGBA(0, 111, 225, 255), 3, false, SmoothingModeHighQuality, &test_title);

				{
					RECT rect;
					GetWindowRect(test_window, &rect);

					POINT ptDst = { rect.left, rect.top };
					ulwi.pptDst = &ptDst;
					ulwi.hdcSrc = GetImageHDC(&test_title);
					UpdateLayeredWindowIndirect(test_window, &ulwi);
				}

				if (off_signal || !test.select) break;

				Sleep(20);
			}
		}
		else if (IsWindowVisible(test_window)) ShowWindow(test_window, SW_HIDE);
	}

	ShowWindow(test_window, SW_HIDE);
	thread_status[L"test_main"] = false;

	return 0;
}
void FreezeFrameWindow()
{
	this_thread::sleep_for(chrono::seconds(3));
	while (magnificationWindowReady != -1) this_thread::sleep_for(chrono::milliseconds(50));

	DisableResizing(freeze_window, true);//禁止窗口拉伸
	SetWindowLong(freeze_window, GWL_STYLE, GetWindowLong(freeze_window, GWL_STYLE) & ~WS_CAPTION);//隐藏标题栏
	SetWindowPos(freeze_window, NULL, 0, 0, 0, 0, SWP_NOSIZE | SWP_NOMOVE | SWP_NOZORDER | SWP_DRAWFRAME);
	SetWindowLong(freeze_window, GWL_EXSTYLE, WS_EX_TOOLWINDOW);//隐藏任务栏

	IMAGE freeze_background(GetSystemMetrics(SM_CXSCREEN), GetSystemMetrics(SM_CYSCREEN));

	// 设置BLENDFUNCTION结构体
	BLENDFUNCTION blend;
	blend.BlendOp = AC_SRC_OVER;
	blend.BlendFlags = 0;
	blend.SourceConstantAlpha = 255; // 设置透明度，0为全透明，255为不透明
	blend.AlphaFormat = AC_SRC_ALPHA; // 使用源图像的alpha通道
	HDC hdcScreen = GetDC(NULL);
	// 调用UpdateLayeredWindow函数更新窗口
	POINT ptSrc = { 0,0 };
	SIZE sizeWnd = { freeze_background.getwidth(),freeze_background.getheight() };
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
	LONG nRet = ::GetWindowLong(freeze_window, GWL_EXSTYLE);
	nRet |= WS_EX_LAYERED;
	::SetWindowLong(freeze_window, GWL_EXSTYLE, nRet);

	FreezeFrame.update = true;
	int wait = 0;

	while (!off_signal)
	{
		Sleep(20);

		if (FreezeFrame.mode == 1)
		{
			IMAGE MagnificationTmp;
			if (!IsWindowVisible(freeze_window))
			{
				std::shared_lock<std::shared_mutex> lock1(MagnificationBackgroundSm);
				MagnificationTmp = MagnificationBackground;
				lock1.unlock();

				hiex::TransparentImage(&freeze_background, 0, 0, &MagnificationTmp);
				ulwi.hdcSrc = GetImageHDC(&freeze_background);
				UpdateLayeredWindowIndirect(freeze_window, &ulwi);

				ShowWindow(freeze_window, SW_SHOW);
			}

			if (SeewoCamera) wait = 500;
			while (!off_signal)
			{
				if (FreezeFrame.mode != 1 || ppt_show != NULL) break;

				if (FreezeFrame.update)
				{
					std::shared_lock<std::shared_mutex> lock1(MagnificationBackgroundSm);
					MagnificationTmp = MagnificationBackground;
					lock1.unlock();

					hiex::TransparentImage(&freeze_background, 0, 0, &MagnificationTmp);
					ulwi.hdcSrc = GetImageHDC(&freeze_background);
					UpdateLayeredWindowIndirect(freeze_window, &ulwi);

					FreezeFrame.update = false;
				}
				else if (wait > 0 && SeewoCamera)
				{
					hiex::TransparentImage(&freeze_background, 0, 0, &MagnificationTmp);

					hiex::EasyX_Gdiplus_FillRoundRect((float)GetSystemMetrics(SM_CXSCREEN) / 2 - 160, (float)GetSystemMetrics(SM_CYSCREEN) - 200, 320, 50, 20, 20, RGBA(255, 255, 225, min(255, wait)), RGBA(0, 0, 0, min(150, wait)), 2, true, SmoothingModeHighQuality, &freeze_background);

					Graphics graphics(GetImageHDC(&freeze_background));
					Gdiplus::Font gp_font(&HarmonyOS_fontFamily, 24, FontStyleRegular, UnitPixel);
					SolidBrush WordBrush(hiex::ConvertToGdiplusColor(RGBA(255, 255, 255, min(255, wait)), true));
					graphics.SetTextRenderingHint(TextRenderingHintAntiAliasGridFit);
					{
						dwords_rect.left = GetSystemMetrics(SM_CXSCREEN) / 2 - 160;
						dwords_rect.top = GetSystemMetrics(SM_CYSCREEN) - 200;
						dwords_rect.right = GetSystemMetrics(SM_CXSCREEN) / 2 + 160;
						dwords_rect.bottom = GetSystemMetrics(SM_CYSCREEN) - 200 + 52;
					}
					graphics.DrawString(L"智绘教已自动开启 画面定格", -1, &gp_font, hiex::RECTToRectF(dwords_rect), &stringFormat, &WordBrush);

					ulwi.hdcSrc = GetImageHDC(&freeze_background);
					UpdateLayeredWindowIndirect(freeze_window, &ulwi);

					wait -= 5;

					if (wait == 0)
					{
						hiex::TransparentImage(&freeze_background, 0, 0, &MagnificationTmp);

						ulwi.hdcSrc = GetImageHDC(&freeze_background);
						UpdateLayeredWindowIndirect(freeze_window, &ulwi);
					}
				}

				if (test.select) SetWindowPos(freeze_window, test_window, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
				else if (!choose.select) SetWindowPos(freeze_window, drawpad_window, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
				else if (ppt_show != NULL) SetWindowPos(freeze_window, ppt_window, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
				else SetWindowPos(freeze_window, floating_window, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);

				Sleep(20);
			}

			if (ppt_show != NULL) FreezeFrame.mode = 0;
			FreezeFrame.update = true;
		}
		else if (IsWindowVisible(freeze_window)) ShowWindow(freeze_window, SW_HIDE);

		if (FreezeFrame.mode != 1 && FreezePPT)
		{
			for (int i = -10; i <= 60 && FreezePPT; i++)
			{
				SetImageColor(freeze_background, RGBA(0, 0, 0, 140), true);
				hiex::TransparentImage(&freeze_background, GetSystemMetrics(SM_CXSCREEN) / 2 - 500, GetSystemMetrics(SM_CYSCREEN) / 2 - 163, &test_sign[3]);

				hiex::EasyX_Gdiplus_SolidRoundRect((float)GetSystemMetrics(SM_CXSCREEN) / 2 - 300, (float)GetSystemMetrics(SM_CYSCREEN) / 2 + 200, 600, 10, 10, 10, RGBA(255, 255, 255, 100), true, SmoothingModeHighQuality, &freeze_background);
				hiex::EasyX_Gdiplus_SolidRoundRect((float)GetSystemMetrics(SM_CXSCREEN) / 2 - 300, (float)GetSystemMetrics(SM_CYSCREEN) / 2 + 200, (float)max(0, min(50, i)) * 12, 10, 10, 10, RGBA(255, 255, 255, 255), false, SmoothingModeHighQuality, &freeze_background);

				{
					Graphics graphics(GetImageHDC(&freeze_background));
					Gdiplus::Font gp_font(&HarmonyOS_fontFamily, 24, FontStyleRegular, UnitPixel);
					SolidBrush WordBrush(hiex::ConvertToGdiplusColor(RGBA(255, 255, 255, 255), false));
					graphics.SetTextRenderingHint(TextRenderingHintAntiAliasGridFit);
					{
						dwords_rect.left = GetSystemMetrics(SM_CXSCREEN) / 2 - 500;
						dwords_rect.top = GetSystemMetrics(SM_CYSCREEN) / 2 + 250;
						dwords_rect.right = GetSystemMetrics(SM_CXSCREEN) / 2 + 500;
						dwords_rect.bottom = GetSystemMetrics(SM_CYSCREEN) / 2 + 300;
					}
					graphics.DrawString(L"Tips：无需点击选择，点击下方按钮即可翻页", -1, &gp_font, hiex::RECTToRectF(dwords_rect), &stringFormat, &WordBrush);
				}

				ulwi.hdcSrc = GetImageHDC(&freeze_background);
				UpdateLayeredWindowIndirect(freeze_window, &ulwi);

				if (test.select) SetWindowPos(freeze_window, test_window, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
				else if (!choose.select) SetWindowPos(freeze_window, drawpad_window, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
				else SetWindowPos(freeze_window, ppt_window, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);

				if (!IsWindowVisible(freeze_window)) ShowWindow(freeze_window, SW_SHOWNOACTIVATE);

				hiex::DelayFPS(24);
			}

			FreezePPT = false;
		}
	}
}

int drawpad_main()
{
	thread_status[L"drawpad_main"] = true;

	//画笔初始化
	{
		brush.width = 4;
	}
	//窗口初始化
	{
		{
			/*
			if (IsWindows8OrGreater())
			{
				HMODULE hUser32 = LoadLibrary(TEXT("user32.dll"));
				if (hUser32)
				{
					pGetPointerFrameTouchInfo = (GetPointerFrameTouchInfoType)GetProcAddress(hUser32, "GetPointerFrameTouchInfo");
					uGetPointerFrameTouchInfo = true;
					//当完成使用时, 可以调用 FreeLibrary(hUser32);
				}
			}
			*/

			//hiex::SetWndProcFunc(drawpad_window, WndProc);
		}

		setbkmode(TRANSPARENT);
		setbkcolor(RGB(255, 255, 255));

		HiBeginDraw();
		cleardevice();
		hiex::FlushDrawing({ 0 }); HiEndDraw();

		DisableResizing(drawpad_window, true);//禁止窗口拉伸
		SetWindowLong(drawpad_window, GWL_STYLE, GetWindowLong(drawpad_window, GWL_STYLE) & ~WS_CAPTION);//隐藏标题栏
		SetWindowPos(drawpad_window, NULL, 0, 0, 0, 0, SWP_NOSIZE | SWP_NOMOVE | SWP_NOZORDER | SWP_DRAWFRAME);
		SetWindowLong(drawpad_window, GWL_EXSTYLE, WS_EX_TOOLWINDOW);//隐藏任务栏

		//SetWindowTransparent(drawpad_window, true, 255);

		// 屏幕置顶
		//SetWindowPos(drawpad_window, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOSIZE | SWP_SHOWWINDOW);
	}
	//媒体初始化
	{
		loadimage(&ppt_icon[1], L"PNG", L"ppt1");
		loadimage(&ppt_icon[2], L"PNG", L"ppt2");
		loadimage(&ppt_icon[3], L"PNG", L"ppt3");
	}

	//初始化数值
	{
		//屏幕快照处理
		if (_waccess((string_to_wstring(global_path) + L"ScreenShot\\attribute_directory.json").c_str(), 4) == 0)
		{
			Json::Reader reader;
			Json::Value root;

			ifstream readjson;
			readjson.imbue(locale("zh_CN.UTF8"));
			readjson.open(wstring_to_string(string_to_wstring(global_path) + L"ScreenShot\\attribute_directory.json").c_str());

			deque<wstring> authenticated_file;
			authenticated_file.push_back(string_to_wstring(global_path) + L"ScreenShot\\attribute_directory.json");
			if (reader.parse(readjson, root))
			{
				readjson.close();

				for (int i = 0; i < (int)root["Image_Properties"].size(); i++)
				{
					deque<wstring> date = getPrevTwoDays(CurrentDate());

					auto it = find(date.begin(), date.end(), string_to_wstring(root["Image_Properties"][i]["date"].asString()));
					if (it == date.end())
					{
						remove(convert_to_gbk(root["Image_Properties"][i]["drawpad"].asString()).c_str());
						remove(convert_to_gbk(root["Image_Properties"][i]["background"].asString()).c_str());
						remove(convert_to_gbk(root["Image_Properties"][i]["blending"].asString()).c_str());

						root["Image_Properties"].removeIndex(i, nullptr);
						i--;
					}
					else if (_waccess(string_to_wstring(convert_to_gbk(root["Image_Properties"][i]["drawpad"].asString())).c_str(), 0) == -1)
					{
						remove(convert_to_gbk(root["Image_Properties"][i]["drawpad"].asString()).c_str());
						remove(convert_to_gbk(root["Image_Properties"][i]["background"].asString()).c_str());
						remove(convert_to_gbk(root["Image_Properties"][i]["blending"].asString()).c_str());

						root["Image_Properties"].removeIndex(i, nullptr);
						i--;
					}
					else
					{
						authenticated_file.push_back(string_to_wstring(convert_to_gbk(root["Image_Properties"][i]["drawpad"].asString())));
						authenticated_file.push_back(string_to_wstring(convert_to_gbk(root["Image_Properties"][i]["background"].asString())));
						authenticated_file.push_back(string_to_wstring(convert_to_gbk(root["Image_Properties"][i]["blending"].asString())));
					}
				}
				removeUnknownFiles(string_to_wstring(global_path) + L"ScreenShot", authenticated_file);
				removeEmptyFolders(string_to_wstring(global_path) + L"ScreenShot");

				Json::StyledWriter outjson;

				ofstream writejson;
				writejson.imbue(locale("zh_CN.UTF8"));
				writejson.open(wstring_to_string(string_to_wstring(global_path) + L"ScreenShot\\attribute_directory.json").c_str());
				writejson << outjson.write(root);
				writejson.close();
			}
			else readjson.close();
		}
		else
		{
			//创建路径
			filesystem::create_directory(string_to_wstring(global_path) + L"ScreenShot");

			Json::Value root;
			deque<wstring> authenticated_file;
			authenticated_file.push_back(string_to_wstring(global_path) + L"ScreenShot\\attribute_directory.json");

			removeUnknownFiles(string_to_wstring(global_path) + L"ScreenShot", authenticated_file);
			removeEmptyFolders(string_to_wstring(global_path) + L"ScreenShot");

			root["Image_Properties"].append(Json::Value("nullptr"));
			root["Image_Properties"].removeIndex(0, nullptr);

			Json::StyledWriter outjson;

			ofstream writejson;
			writejson.imbue(locale("zh_CN.UTF8"));
			writejson.open(wstring_to_string(string_to_wstring(global_path) + L"ScreenShot\\attribute_directory.json").c_str());
			writejson << outjson.write(root);
			writejson.close();
		}
	}

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

	LONG nRet = ::GetWindowLong(drawpad_window, GWL_EXSTYLE);
	nRet |= WS_EX_LAYERED;
	::SetWindowLong(drawpad_window, GWL_EXSTYLE, nRet);

	magnificationWindowReady++;
	{
		SetImageColor(drawpad, RGBA(0, 0, 0, 1), true);
		SetImageColor(alpha_drawpad, RGBA(0, 0, 0, 0), true);
		{
			ulwi.hdcSrc = GetImageHDC(&background);
			UpdateLayeredWindowIndirect(drawpad_window, &ulwi);
		}
		ShowWindow(drawpad_window, SW_SHOW);

		struct Mouse
		{
			int x = 0, y = 0;
			int last_x = 0, last_y = 0;
			int last_length = 0;
		}mouse;
		POINT pt = { 0,0 };

		SetWindowPos(drawpad_window, floating_window, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
		LONG style = GetWindowLong(drawpad_window, GWL_EXSTYLE);
		style |= WS_EX_NOACTIVATE;
		SetWindowLong(drawpad_window, GWL_EXSTYLE, style);

		//启动绘图库程序
		hiex::Gdiplus_Try_Starup();
		MagInitialize();
		while (!off_signal)
		{
			if (choose.select == true)
			{
				if (draw_content) last_drawpad = drawpad;

				SetWindowRgn(drawpad_window, CreateRectRgn(0, 0, 0, 0), true);

				if (draw_content)
				{
					thread SaveScreenShot_thread(SaveScreenShot, last_drawpad);
					SaveScreenShot_thread.detach();
				}
				while (choose.select)
				{
					Sleep(50);

					ppt_info.currentSlides = ppt_info_stay.CurrentPage;
					ppt_info.totalSlides = ppt_info_stay.TotalPage;

					if (off_signal) goto drawpad_main_end;
				}

				if (empty_drawpad)
				{
					SetImageColor(drawpad, RGBA(0, 0, 0, 1), true);
					while (!RecallImage.empty())
					{
						RecallImage.pop_back();
					}
					deque<RecallStruct>(RecallImage).swap(RecallImage); // 使用swap技巧来释放未使用的内存

					extreme_point.clear();
				}
				else
				{
					drawpad = last_drawpad;
				}
				empty_drawpad = true;

				draw_content = false;
				{
					ulwi.hdcSrc = GetImageHDC(&drawpad);
					UpdateLayeredWindowIndirect(drawpad_window, &ulwi);
				}
				SetWindowRgn(drawpad_window, CreateRectRgn(0, 0, GetSystemMetrics(SM_CXSCREEN), GetSystemMetrics(SM_CYSCREEN)), true);

				TouchTemp.clear();
			}
			if (penetrate.select == true)
			{
				LONG nRet = ::GetWindowLong(drawpad_window, GWL_EXSTYLE);
				nRet |= WS_EX_TRANSPARENT;
				::SetWindowLong(drawpad_window, GWL_EXSTYLE, nRet);

				while (1)
				{
					SetWindowPos(drawpad_window, floating_window, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
					Sleep(50);

					if (off_signal) goto drawpad_main_end;
					if (penetrate.select == true) continue;
					break;
				}

				nRet = ::GetWindowLong(drawpad_window, GWL_EXSTYLE);
				nRet &= ~WS_EX_TRANSPARENT;
				::SetWindowLong(drawpad_window, GWL_EXSTYLE, nRet);

				TouchTemp.clear();
			}

			if (ppt_show != NULL) SetWindowPos(drawpad_window, ppt_window, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
			else SetWindowPos(drawpad_window, floating_window, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);

			std::shared_lock<std::shared_mutex> lock1(PointTempSm);
			bool start = !TouchTemp.empty();
			lock1.unlock();
			//开始绘图
			if (start)
			{
				if (rubber.select != true && (brush.mode == 1 || brush.mode == 2) && int(state) == 1) target_status = 0;

				std::shared_lock<std::shared_mutex> lock1(PointTempSm);
				LONG pid = TouchTemp.front().pid;
				pt = TouchTemp.front().pt;
				lock1.unlock();

				std::unique_lock<std::shared_mutex> lock2(PointTempSm);
				TouchTemp.pop_front();
				lock2.unlock();

				//hiex::PreSetWindowShowState(SW_HIDE);
				//HWND draw_window = initgraph(GetSystemMetrics(SM_CXSCREEN), GetSystemMetrics(SM_CYSCREEN));

				if (rubber.select == true)
				{
					draw_content = true;

					mouse.last_x = pt.x, mouse.last_y = pt.y;

					double rubbersize = 15, trubbersize = -1;
					while (1)
					{
						std::shared_lock<std::shared_mutex> lock0(PointPosSm);
						bool unfind = TouchPos.find(pid) == TouchPos.end();
						lock0.unlock();

						if (unfind) break;

						std::shared_lock<std::shared_mutex> lock1(PointPosSm);
						pt = TouchPos[pid].pt;
						lock1.unlock();

						std::shared_lock<std::shared_mutex> lock2(TouchSpeedSm);
						double speed = TouchSpeed[pid];
						lock2.unlock();

						std::shared_lock<std::shared_mutex> lock3(PointListSm);
						auto it = std::find(TouchList.begin(), TouchList.end(), pid);
						lock3.unlock();

						if (it == TouchList.end()) break;

						mouse.x = pt.x, mouse.y = pt.y;

						if (setlist.experimental_functions)
						{
							if (speed <= 0.1) trubbersize = 60;
							else if (speed <= 30) trubbersize = max(25, speed * 2.33 + 2.33);
							else trubbersize = min(200, speed + 30);

							if (trubbersize == -1) trubbersize = rubbersize;
							if (rubbersize < trubbersize) rubbersize = rubbersize + max(0.001, (trubbersize - rubbersize) / 50);
							else rubbersize = rubbersize + min(-0.001, (trubbersize - rubbersize) / 50);
						}
						else rubbersize = 60;

						if (pt.x == mouse.last_x && pt.y == mouse.last_y)
						{
							Graphics eraser(GetImageHDC(&drawpad));
							GraphicsPath path;
							path.AddEllipse(INT(mouse.last_x - int(rubbersize / 2)), INT(mouse.last_y - int(rubbersize / 2)), INT(rubbersize), INT(rubbersize));

							Region region(&path);
							eraser.SetSmoothingMode(Gdiplus::SmoothingModeHighQuality);
							eraser.SetClip(&region, CombineModeReplace);
							eraser.Clear(Color(1, 0, 0, 0));
							eraser.ResetClip();

							for (const auto& [point, value] : extreme_point)
							{
								if (value == true && region.IsVisible(point.first, point.second))
								{
									extreme_point[{point.first, point.second}] = false;
								}
							}

							putout = drawpad;
							hiex::EasyX_Gdiplus_Ellipse(mouse.x - (float)(rubbersize) / 2, mouse.y - (float)(rubbersize) / 2, (float)rubbersize, (float)rubbersize, RGBA(130, 130, 130, 200), 3, true, SmoothingModeHighQuality, &putout);
						}
						else
						{
							Graphics eraser(GetImageHDC(&drawpad));
							GraphicsPath path;
							path.AddLine(mouse.last_x, mouse.last_y, mouse.x, mouse.y);

							Pen pen(Color(0, 0, 0, 0), Gdiplus::REAL(rubbersize));
							pen.SetStartCap(LineCapRound);
							pen.SetEndCap(LineCapRound);

							GraphicsPath* widenedPath = path.Clone();
							widenedPath->Widen(&pen);
							Region region(widenedPath);
							eraser.SetClip(&region, CombineModeReplace);
							eraser.Clear(Color(1, 0, 0, 0));
							eraser.ResetClip();

							for (const auto& [point, value] : extreme_point)
							{
								if (value == true && region.IsVisible(point.first, point.second))
								{
									extreme_point[{point.first, point.second}] = false;
								}
							}

							delete widenedPath;

							putout = drawpad;
							hiex::EasyX_Gdiplus_Ellipse(mouse.x - (float)(rubbersize) / 2, mouse.y - (float)(rubbersize) / 2, (float)rubbersize, (float)rubbersize, RGBA(130, 130, 130, 200), 3, true, SmoothingModeHighQuality, &putout);
						}

						mouse.last_x = mouse.x, mouse.last_y = mouse.y;

						{
							RECT rcDirty = { 0, 0, GetSystemMetrics(SM_CXSCREEN), GetSystemMetrics(SM_CYSCREEN) };

							ulwi.hdcSrc = GetImageHDC(&putout);
							ulwi.prcDirty = &rcDirty;
							UpdateLayeredWindowIndirect(drawpad_window, &ulwi);
						}
					}

					{
						// 定义要更新的矩形区域
						RECT rcDirty = { 0, 0, GetSystemMetrics(SM_CXSCREEN), GetSystemMetrics(SM_CYSCREEN) };

						ulwi.hdcSrc = GetImageHDC(&drawpad);
						ulwi.prcDirty = &rcDirty;
						UpdateLayeredWindowIndirect(drawpad_window, &ulwi);
					}
				}
				else if (brush.select == true)
				{
					mouse.last_x = pt.x, mouse.last_y = pt.y;
					RECT rcDirty;
					int draw_width = brush.width;
					draw_content = true;

					double writing_distance = 0;
					vector<Point> points = { Point(mouse.last_x, mouse.last_y) };
					RECT circumscribed_rectangle = { -1,-1,-1,-1 };

					if (RecallImage.empty()) RecallImage.push_back({ drawpad, extreme_point });

					{
						if (brush.mode == 3 || brush.mode == 4);
						else
						{
							if (brush.mode == 1)
							{
								hiex::EasyX_Gdiplus_SolidEllipse(float((float)mouse.last_x - (float)(draw_width) / 2.0), float((float)mouse.last_y - (float)(draw_width) / 2.0), (float)draw_width, (float)draw_width, brush.color, false, SmoothingModeHighQuality, &drawpad);
								{
									// 定义要更新的矩形区域
									RECT rcDirty = { mouse.last_x - draw_width, mouse.last_y - draw_width, mouse.last_x + draw_width, mouse.last_y + draw_width };

									ulwi.hdcSrc = GetImageHDC(&drawpad);
									ulwi.prcDirty = &rcDirty;
									UpdateLayeredWindowIndirect(drawpad_window, &ulwi);
								}
							}
							else
							{
								hiex::EasyX_Gdiplus_SolidEllipse(float((float)mouse.last_x - (float)(draw_width) / 2.0), float((float)mouse.last_y - (float)(draw_width) / 2.0), (float)draw_width, (float)draw_width, brush.color, false, SmoothingModeHighQuality, &alpha_drawpad);
								putout = drawpad;
								{
									HDC dstDC = GetImageHDC(&putout);
									HDC srcDC = GetImageHDC(&alpha_drawpad);
									int w = alpha_drawpad.getwidth();
									int h = alpha_drawpad.getheight();

									// 结构体的第三个成员表示额外的透明度，0 表示全透明，255 表示不透明。
									BLENDFUNCTION bf = { AC_SRC_OVER, 0, 130, AC_SRC_ALPHA };
									// 使用 Windows GDI 函数实现半透明位图
									AlphaBlend(dstDC, 0, 0, w, h, srcDC, 0, 0, w, h, bf);
								}

								{
									// 定义要更新的矩形区域
									RECT rcDirty = { mouse.last_x - draw_width * 10, mouse.last_y - draw_width * 10, mouse.last_x + draw_width * 10, mouse.last_y + draw_width * 10 };

									ulwi.hdcSrc = GetImageHDC(&putout);
									ulwi.prcDirty = &rcDirty;
									UpdateLayeredWindowIndirect(drawpad_window, &ulwi);
								}
							}
						}
					}

					while (1)
					{
						std::shared_lock<std::shared_mutex> lock0(PointPosSm);
						bool unfind = TouchPos.find(pid) == TouchPos.end();
						lock0.unlock();

						if (unfind) break;

						std::shared_lock<std::shared_mutex> lock1(PointPosSm);
						pt = TouchPos[pid].pt;
						lock1.unlock();

						std::shared_lock<std::shared_mutex> lock2(PointListSm);
						auto it = std::find(TouchList.begin(), TouchList.end(), pid);
						lock2.unlock();

						if (it == TouchList.end()) break;

						if (pt.x == mouse.last_x && pt.y == mouse.last_y) continue;

						if (brush.mode == 3)
						{
							SetWorkingImage(&alpha_drawpad);
							setfillcolor(SET_ALPHA(BLACK, 0));
							solidrectangle(0, 0, GetSystemMetrics(SM_CXSCREEN), GetSystemMetrics(SM_CYSCREEN));
							SetWorkingImage(hiex::GetWindowImage());

							Graphics graphics(GetImageHDC(&alpha_drawpad));
							graphics.SetSmoothingMode(SmoothingModeHighQuality);

							Pen pen(hiex::ConvertToGdiplusColor(brush.color, false));
							pen.SetStartCap(LineCapRound);
							pen.SetEndCap(LineCapRound);

							pen.SetWidth(Gdiplus::REAL(draw_width));
							graphics.DrawLine(&pen, mouse.last_x, mouse.last_y, pt.x, pt.y);

							rcDirty = { 0, 0, GetSystemMetrics(SM_CXSCREEN), GetSystemMetrics(SM_CYSCREEN) };
							putout = drawpad;
							{
								HDC dstDC = GetImageHDC(&putout);
								HDC srcDC = GetImageHDC(&alpha_drawpad);
								int w = alpha_drawpad.getwidth();
								int h = alpha_drawpad.getheight();

								// 结构体的第三个成员表示额外的透明度，0 表示全透明，255 表示不透明。
								BLENDFUNCTION bf = { AC_SRC_OVER, 0, (brush.color >> 24), AC_SRC_ALPHA };
								// 使用 Windows GDI 函数实现半透明位图
								AlphaBlend(dstDC, 0, 0, w, h, srcDC, 0, 0, w, h, bf);
							}

							{
								ulwi.hdcSrc = GetImageHDC(&putout);
								ulwi.prcDirty = &rcDirty;
								UpdateLayeredWindowIndirect(drawpad_window, &ulwi);
							}
						}
						else if (brush.mode == 4)
						{
							SetWorkingImage(&alpha_drawpad);
							setfillcolor(SET_ALPHA(BLACK, 0));
							solidrectangle(0, 0, GetSystemMetrics(SM_CXSCREEN), GetSystemMetrics(SM_CYSCREEN));
							SetWorkingImage(hiex::GetWindowImage());

							int rectangle_x = min(mouse.last_x, pt.x), rectangle_y = min(mouse.last_y, pt.y);
							int rectangle_heigth = abs(mouse.last_x - pt.x) + 1, rectangle_width = abs(mouse.last_y - pt.y) + 1;
							hiex::EasyX_Gdiplus_RoundRect((float)rectangle_x, (float)rectangle_y, (float)rectangle_heigth, (float)rectangle_width, 3, 3, brush.color, (float)draw_width, false, SmoothingModeHighQuality, &alpha_drawpad);

							if (points.size() < 2) points.emplace_back(Point(pt.x, pt.y));
							else points[1] = Point(pt.x, pt.y);

							rcDirty = { 0, 0, GetSystemMetrics(SM_CXSCREEN), GetSystemMetrics(SM_CYSCREEN) };
							putout = drawpad;
							{
								HDC dstDC = GetImageHDC(&putout);
								HDC srcDC = GetImageHDC(&alpha_drawpad);
								int w = alpha_drawpad.getwidth();
								int h = alpha_drawpad.getheight();

								// 结构体的第三个成员表示额外的透明度，0 表示全透明，255 表示不透明。
								BLENDFUNCTION bf = { AC_SRC_OVER, 0, (brush.color >> 24), AC_SRC_ALPHA };
								// 使用 Windows GDI 函数实现半透明位图
								AlphaBlend(dstDC, 0, 0, w, h, srcDC, 0, 0, w, h, bf);
							}

							{
								ulwi.hdcSrc = GetImageHDC(&putout);
								ulwi.prcDirty = &rcDirty;
								UpdateLayeredWindowIndirect(drawpad_window, &ulwi);
							}
						}
						else
						{
							writing_distance += EuclideanDistance({ mouse.last_x, mouse.last_y }, { pt.x, pt.y });

							if (brush.mode == 1)
							{
								rcDirty = DrawGradientLine(GetImageHDC(&drawpad), mouse.last_x, mouse.last_y, (mouse.x = pt.x), (mouse.y = pt.y), (float)draw_width, hiex::ConvertToGdiplusColor(brush.color, false));

								{
									if (mouse.x < circumscribed_rectangle.left || circumscribed_rectangle.left == -1) circumscribed_rectangle.left = mouse.x;
									if (mouse.y < circumscribed_rectangle.top || circumscribed_rectangle.top == -1) circumscribed_rectangle.top = mouse.y;
									if (mouse.x > circumscribed_rectangle.right || circumscribed_rectangle.right == -1) circumscribed_rectangle.right = mouse.x;
									if (mouse.y > circumscribed_rectangle.bottom || circumscribed_rectangle.bottom == -1) circumscribed_rectangle.bottom = mouse.y;

									points.push_back(Point(mouse.x, mouse.y));
								}

								{
									ulwi.hdcSrc = GetImageHDC(&drawpad);
									ulwi.prcDirty = &rcDirty;
									UpdateLayeredWindowIndirect(drawpad_window, &ulwi);
								}
							}
							else
							{
								rcDirty = DrawGradientLine(GetImageHDC(&alpha_drawpad), mouse.last_x, mouse.last_y, (mouse.x = pt.x), (mouse.y = pt.y), (float)draw_width, hiex::ConvertToGdiplusColor(brush.color, false));
								putout = drawpad;
								{
									HDC dstDC = GetImageHDC(&putout);
									HDC srcDC = GetImageHDC(&alpha_drawpad);
									int w = alpha_drawpad.getwidth();
									int h = alpha_drawpad.getheight();

									// 结构体的第三个成员表示额外的透明度，0 表示全透明，255 表示不透明。
									BLENDFUNCTION bf = { AC_SRC_OVER, 0, 130, AC_SRC_ALPHA };
									// 使用 Windows GDI 函数实现半透明位图
									AlphaBlend(dstDC, 0, 0, w, h, srcDC, 0, 0, w, h, bf);
								}

								{
									ulwi.hdcSrc = GetImageHDC(&putout);
									ulwi.prcDirty = &rcDirty;
									UpdateLayeredWindowIndirect(drawpad_window, &ulwi);
								}
							}

							mouse.last_x = mouse.x, mouse.last_y = mouse.y;
						}
					}
					if (brush.mode == 3 || brush.mode == 4)
					{
						drawpad = putout;
						SetWorkingImage(&alpha_drawpad);
						setfillcolor(SET_ALPHA(BLACK, 0));
						solidrectangle(0, 0, GetSystemMetrics(SM_CXSCREEN), GetSystemMetrics(SM_CYSCREEN));
						SetWorkingImage(hiex::GetWindowImage());
					}
					else if (brush.mode == 2)
					{
						drawpad = putout;
						SetWorkingImage(&alpha_drawpad);
						setfillcolor(SET_ALPHA(BLACK, 0));
						solidrectangle(0, 0, GetSystemMetrics(SM_CXSCREEN), GetSystemMetrics(SM_CYSCREEN));
						SetWorkingImage(hiex::GetWindowImage());
					}

					//智能绘图模块
					if (brush.mode == 1)
					{
						double redundance = max(GetSystemMetrics(SM_CXSCREEN) / 192, min((GetSystemMetrics(SM_CXSCREEN)) / 76.8, double(GetSystemMetrics(SM_CXSCREEN)) / double((-0.036) * writing_distance + 135)));

						//直线绘制
						if (writing_distance >= 120 && (abs(circumscribed_rectangle.left - circumscribed_rectangle.right) >= 120 || abs(circumscribed_rectangle.top - circumscribed_rectangle.bottom) >= 120) && isLine(points, int(redundance)))
						{
							Point start(points[0]), end(points[points.size() - 1]);

							//端点匹配
							{
								//起点匹配
								{
									Point start_target = start;
									double distance = 10;
									for (const auto& [point, value] : extreme_point)
									{
										if (value == true)
										{
											if (EuclideanDistance({ point.first,point.second }, { start.X,start.Y }) <= distance)
											{
												distance = EuclideanDistance({ point.first,point.second }, { start.X,start.Y });
												start_target = { point.first,point.second };
											}
										}
									}
									start = start_target;
								}
								//终点匹配
								{
									Point end_target = end;
									double distance = 10;
									for (const auto& [point, value] : extreme_point)
									{
										if (value == true)
										{
											if (EuclideanDistance({ point.first,point.second }, { end.X,end.Y }) <= distance)
											{
												distance = EuclideanDistance({ point.first,point.second }, { end.X,end.Y });
												end_target = { point.first,point.second };
											}
										}
									}
									end = end_target;
								}
							}
							extreme_point[{start.X, start.Y}] = extreme_point[{end.X, end.Y}] = true;

							if (!RecallImage.empty()) drawpad = RecallImage.back().img;
							else SetImageColor(drawpad, RGBA(0, 0, 0, 1), true);

							Graphics graphics(GetImageHDC(&drawpad));
							graphics.SetSmoothingMode(SmoothingModeHighQuality);

							Pen pen(hiex::ConvertToGdiplusColor(brush.color, false));
							pen.SetStartCap(LineCapRound);
							pen.SetEndCap(LineCapRound);

							pen.SetWidth(Gdiplus::REAL(draw_width));
							graphics.DrawLine(&pen, start.X, start.Y, end.X, end.Y);
						}
						//平滑曲线
						else if (points.size() > 2)
						{
							if (!RecallImage.empty()) drawpad = RecallImage.back().img;
							else SetImageColor(drawpad, RGBA(0, 0, 0, 1), true);

							Graphics graphics(GetImageHDC(&drawpad));
							graphics.SetSmoothingMode(SmoothingModeHighQuality);

							Pen pen(hiex::ConvertToGdiplusColor(brush.color, false));
							pen.SetLineJoin(LineJoinRound);
							pen.SetStartCap(LineCapRound);
							pen.SetEndCap(LineCapRound);

							pen.SetWidth(Gdiplus::REAL(draw_width));
							graphics.DrawCurve(&pen, points.data(), points.size(), 0.4f);
						}
					}
					else if (brush.mode == 4)
					{
						//端点匹配
						if (points.size() == 2)
						{
							Point l1 = points[0];
							Point l2 = Point(points[0].X, points[points.size() - 1].Y);
							Point r1 = Point(points[points.size() - 1].X, points[0].Y);
							Point r2 = points[points.size() - 1];

							//端点匹配
							{
								{
									Point idx = l1;
									double distance = 10;
									for (const auto& [point, value] : extreme_point)
									{
										if (value == true)
										{
											if (EuclideanDistance({ point.first,point.second }, { l1.X,l1.Y }) <= distance)
											{
												distance = EuclideanDistance({ point.first,point.second }, { l1.X,l1.Y });
												idx = { point.first,point.second };
											}
										}
									}
									l1 = idx;

									l2.X = l1.X;
									r1.Y = l1.Y;
								}
								{
									Point idx = l2;
									double distance = 10;
									for (const auto& [point, value] : extreme_point)
									{
										if (value == true)
										{
											if (EuclideanDistance({ point.first,point.second }, { l2.X,l2.Y }) <= distance)
											{
												distance = EuclideanDistance({ point.first,point.second }, { l2.X,l2.Y });
												idx = { point.first,point.second };
											}
										}
									}
									l2 = idx;

									l1.X = l2.X;
									r2.Y = l2.Y;
								}
								{
									Point idx = r1;
									double distance = 10;
									for (const auto& [point, value] : extreme_point)
									{
										if (value == true)
										{
											if (EuclideanDistance({ point.first,point.second }, { r1.X,r1.Y }) <= distance)
											{
												distance = EuclideanDistance({ point.first,point.second }, { r1.X,r1.Y });
												idx = { point.first,point.second };
											}
										}
									}
									r1 = idx;

									r2.X = r1.X;
									l1.Y = r1.Y;
								}
								{
									Point idx = r2;
									double distance = 10;
									for (const auto& [point, value] : extreme_point)
									{
										if (value == true)
										{
											if (EuclideanDistance({ point.first,point.second }, { r2.X,r2.Y }) <= distance)
											{
												distance = EuclideanDistance({ point.first,point.second }, { r2.X,r2.Y });
												idx = { point.first,point.second };
											}
										}
									}
									r2 = idx;

									r1.X = r2.X;
									r2.Y = r2.Y;
								}
							}
							extreme_point[{l1.X, l1.Y}] = true;
							extreme_point[{l2.X, l2.Y}] = true;
							extreme_point[{r1.X, r1.Y}] = true;
							extreme_point[{r2.X, r2.Y}] = true;

							if (!RecallImage.empty()) drawpad = RecallImage.back().img;
							else SetImageColor(drawpad, RGBA(0, 0, 0, 1), true);

							int x = min(l1.X, r2.X);
							int y = min(l1.Y, r2.Y);
							int w = abs(l1.X - r2.X) + 1;
							int h = abs(l1.Y - r2.Y) + 1;

							hiex::EasyX_Gdiplus_RoundRect((float)x, (float)y, (float)w, (float)h, 3, 3, brush.color, (float)draw_width, false, SmoothingModeHighQuality, &drawpad);
						}
					}
				}

				std::unique_lock<std::shared_mutex> lock3(PointPosSm);
				TouchPos.erase(pid);
				lock3.unlock();

				if (RecallImage.empty() || (!RecallImage.empty() && !CompareImagesWithBuffer(&drawpad, &RecallImage.back().img)))
				{
					RecallImage.push_back({ drawpad, extreme_point });
					if (RecallImage.size() > 16)
					{
						while (RecallImage.size() > 16)
						{
							RecallImage.pop_front();
						}
						deque<RecallStruct>(RecallImage).swap(RecallImage); // 使用swap技巧来释放未使用的内存
					}
				}

				{
					// 定义要更新的矩形区域
					RECT rcDirty = { 0, 0, GetSystemMetrics(SM_CXSCREEN), GetSystemMetrics(SM_CYSCREEN) };

					ulwi.hdcSrc = GetImageHDC(&drawpad);
					ulwi.prcDirty = &rcDirty;
					UpdateLayeredWindowIndirect(drawpad_window, &ulwi);
				}
			}

			Sleep(1);

			if (ppt_info.currentSlides != ppt_info_stay.CurrentPage || ppt_info.totalSlides != ppt_info_stay.TotalPage)
			{
				if (ppt_info.currentSlides != ppt_info_stay.CurrentPage && ppt_info.totalSlides == ppt_info_stay.TotalPage)
				{
					if (draw_content)
					{
						ppt_img.is_save = true;
						ppt_img.is_saved[ppt_info.currentSlides] = true;
						ppt_img.image[ppt_info.currentSlides] = drawpad;
						//SaveScreenShot(drawpad);
					}

					if (ppt_img.is_saved[ppt_info_stay.CurrentPage] == true)
					{
						drawpad = ppt_img.image[ppt_info_stay.CurrentPage];
					}
					else
					{
						if (ppt_info_stay.TotalPage != -1) SetImageColor(drawpad, RGBA(0, 0, 0, 1), true);
					}

					while (!RecallImage.empty())
					{
						RecallImage.pop_back();
						deque<RecallStruct>(RecallImage).swap(RecallImage); // 使用swap技巧来释放未使用的内存
					}
				}
				ppt_info.currentSlides = ppt_info_stay.CurrentPage;
				ppt_info.totalSlides = ppt_info_stay.TotalPage;

				{
					RECT rcDirty = { 0,0,GetSystemMetrics(SM_CXSCREEN),GetSystemMetrics(SM_CYSCREEN) };

					ulwi.hdcSrc = GetImageHDC(&drawpad);
					ulwi.prcDirty = &rcDirty;
					UpdateLayeredWindowIndirect(drawpad_window, &ulwi);
				}
			}
		}
	}

drawpad_main_end:
	ShowWindow(drawpad_window, SW_HIDE);
	thread_status[L"drawpad_main"] = false;
	return 0;
}