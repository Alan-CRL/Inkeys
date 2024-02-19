#include "IdtSetting.h"

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image/stb-master/stb_image.h"

IMAGE SettingSign[5];
WNDCLASSEX ImGuiWc;
ID3D11ShaderResourceView* TextureSettingSign[5];
int SettingMainMode = 1;

int ScreenWidth = GetSystemMetrics(SM_CXSCREEN);//获取显示器的宽
int ScreenHeight = GetSystemMetrics(SM_CYSCREEN);//获取显示器的高

int SettingWindowX = (ScreenWidth - SettingWindowWidth) / 2;
int SettingWindowY = (ScreenHeight - SettingWindowHeight) / 2;
int SettingWindowWidth = 900;
int SettingWindowHeight = 700;

/*
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
void ControlTestMain()
{
	ExMessage m;
	while (!off_signal)
	{
		hiex::getmessage_win32(&m, EM_MOUSE, setting_window);

		if (SettingMainMode == 1 && IsInRect(m.x, m.y, { 220, 625, 980, 675 }))
		{
			if (m.lbutton)
			{
				int lx = m.x, ly = m.y;
				while (1)
				{
					ExMessage m = hiex::getmessage_win32(EM_MOUSE, setting_window);
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
								root["StartUp"] = Json::Value(setlist.StartUp);
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
						hiex::flushmessage_win32(EM_MOUSE, setting_window);

						break;
					}
				}
				hiex::flushmessage_win32(EM_MOUSE, setting_window);
			}
		}

		else if (IsInRect(m.x, m.y, { 10, 15, 190, 75 }))
		{
			if (m.lbutton)
			{
				int lx = m.x, ly = m.y;
				while (1)
				{
					ExMessage m = hiex::getmessage_win32(EM_MOUSE, setting_window);
					if (IsInRect(m.x, m.y, { 10, 15, 190, 75 }))
					{
						if (!m.lbutton)
						{
							SettingMainMode = 1;

							break;
						}
					}
					else
					{
						hiex::flushmessage_win32(EM_MOUSE, setting_window);

						break;
					}
				}
				hiex::flushmessage_win32(EM_MOUSE, setting_window);
			}
		}
		else if (IsInRect(m.x, m.y, { 10, 85, 190, 145 }))
		{
			if (m.lbutton)
			{
				int lx = m.x, ly = m.y;
				while (1)
				{
					ExMessage m = hiex::getmessage_win32(EM_MOUSE, setting_window);
					if (IsInRect(m.x, m.y, { 10, 85, 190, 145 }))
					{
						if (!m.lbutton)
						{
							SettingMainMode = 2;

							break;
						}
					}
					else
					{
						hiex::flushmessage_win32(EM_MOUSE, setting_window);

						break;
					}
				}
				hiex::flushmessage_win32(EM_MOUSE, setting_window);
			}
		}
		else if (IsInRect(m.x, m.y, { 10, 155, 190, 215 }))
		{
			if (m.lbutton)
			{
				int lx = m.x, ly = m.y;
				while (1)
				{
					ExMessage m = hiex::getmessage_win32(EM_MOUSE, setting_window);
					if (IsInRect(m.x, m.y, { 10, 155, 190, 215 }))
					{
						if (!m.lbutton)
						{
							SettingMainMode = 3;

							break;
						}
					}
					else
					{
						hiex::flushmessage_win32(EM_MOUSE, setting_window);

						break;
					}
				}
				hiex::flushmessage_win32(EM_MOUSE, setting_window);
			}
		}
	}
}
int SettingMain()
{
	this_thread::sleep_for(chrono::seconds(3));
	while (!already) this_thread::sleep_for(chrono::milliseconds(50));

	thread_status[L"SettingMain"] = true;

	loadimage(&SettingSign[1], L"PNG", L"sign2");
	loadimage(&SettingSign[2], L"PNG", L"sign3");
	loadimage(&SettingSign[3], L"PNG", L"sign4");
	loadimage(&SettingSign[4], L"PNG", L"sign5");

	IMAGE test_icon[5];
	loadimage(&test_icon[1], L"PNG", L"test_icon1");
	loadimage(&test_icon[2], L"PNG", L"test_icon2");
	loadimage(&test_icon[3], L"PNG", L"test_icon3");

	DisableResizing(setting_window, true);//禁止窗口拉伸
	DisableSystemMenu(setting_window, true);

	LONG style = GetWindowLong(setting_window, GWL_STYLE);
	style &= ~WS_SYSMENU;
	SetWindowLong(setting_window, GWL_STYLE, style);

	hiex::SetWndProcFunc(setting_window, TestWndProc);

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
	LONG nRet = ::GetWindowLong(setting_window, GWL_EXSTYLE);
	nRet |= WS_EX_LAYERED;
	::SetWindowLong(setting_window, GWL_EXSTYLE, nRet);

	thread ControlTestMain_thread(ControlTestMain);
	ControlTestMain_thread.detach();

	POINT pt;
	IMAGE test_title(1010, 750), test_background(1010, 710), test_content(800, 700);

	if (gif_load && _waccess((string_to_wstring(global_path) + L"Toothless.gif").c_str(), 4) == 0)
	{
		GIF_dynamic_effect.load((string_to_wstring(global_path) + L"Toothless.gif").c_str());
		GIF_dynamic_effect.bind(GetImageHDC(&test_title));
		GIF_dynamic_effect.setPos(250, 120);
		GIF_dynamic_effect.setSize(0, 0);
	}

	magnificationWindowReady++;
	while (!off_signal)
	{
		Sleep(500);

		if (test.select == true)
		{
			wstring ppt_LinkTest = LinkTest();
			wstring ppt_IsPptDependencyLoaded = IsPptDependencyLoaded();
			Graphics graphics(GetImageHDC(&test_content));

			if (!IsWindowVisible(setting_window)) ShowWindow(setting_window, SW_SHOW);

			while (!off_signal)
			{
				if (!choose.select) SetWindowPos(setting_window, drawpad_window, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
				else if (ppt_show != NULL) SetWindowPos(setting_window, ppt_window, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
				else SetWindowPos(setting_window, floating_window, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);

				SetImageColor(test_background, RGBA(0, 0, 0, 0), true);
				hiex::EasyX_Gdiplus_SolidRoundRect(0, 0, 1000, 700, 20, 20, RGBA(245, 245, 245, 255), false, SmoothingModeHighQuality, &test_background);

				if (gif_load && !GIF_dynamic_effect.IsPlaying() && SettingMainMode == 1) GIF_dynamic_effect.play();

				if (SettingMainMode == 1)
				{
					SetImageColor(test_content, RGBA(0, 0, 0, 0), true);
					hiex::EasyX_Gdiplus_SolidRoundRect(0, 0, 800, 700, 20, 20, RGBA(255, 255, 255, 255), false, SmoothingModeHighQuality, &test_content);

					{
						Gdiplus::Font20 gp_font(&HarmonyOS_fontFamily, 20, FontStyleRegular, UnitPixel);
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

					hiex::TransparentImage(&test_content, 50, 140, &SettingSign[2]);

					if (!server_feedback.empty() && server_feedback != "")
					{
						Gdiplus::Font20 gp_font(&HarmonyOS_fontFamily, 20, FontStyleRegular, UnitPixel);
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

						Gdiplus::Font20 gp_font(&HarmonyOS_fontFamily, 25, FontStyleRegular, UnitPixel);
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

						Gdiplus::Font20 gp_font(&HarmonyOS_fontFamily, 25, FontStyleRegular, UnitPixel);
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

						Gdiplus::Font20 gp_font(&HarmonyOS_fontFamily, 25, FontStyleRegular, UnitPixel);
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

						Gdiplus::Font20 gp_font(&HarmonyOS_fontFamily, 25, FontStyleRegular, UnitPixel);
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

						Gdiplus::Font20 gp_font(&HarmonyOS_fontFamily, 25, FontStyleRegular, UnitPixel);
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

						Gdiplus::Font20 gp_font(&HarmonyOS_fontFamily, 25, FontStyleRegular, UnitPixel);
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
				if (SettingMainMode == 2)
				{
					SetImageColor(test_content, RGBA(0, 0, 0, 0), true);
					hiex::EasyX_Gdiplus_SolidRoundRect(0, 0, 800, 700, 20, 20, RGBA(255, 255, 255, 255), false, SmoothingModeHighQuality, &test_content);

					{
						Gdiplus::Font20 gp_font(&HarmonyOS_fontFamily, 20, FontStyleRegular, UnitPixel);
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

					Gdiplus::Font20 gp_font(&HarmonyOS_fontFamily, 18, FontStyleRegular, UnitPixel);
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

					text += L"\n\n撤回库当前大小：" + to_wstring(RecallImage.size());
					text += L"\n撤回库 recall_image_recond：" + to_wstring(recall_image_recond);
					text += L"\n撤回库 total_record_pointer：" + to_wstring(total_record_pointer);
					text += L"\n撤回库 practical_total_record_pointer：" + to_wstring(practical_total_record_pointer);
					//text += L"\n\n撤回库 reference_record_pointer：" + to_wstring(reference_record_pointer);
					//text += L"\n撤回库 current_record_pointer：" + to_wstring(current_record_pointer);
					text += L"\n首次绘制状态：", text += (FirstDraw == true) ? L"是" : L"否";

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
				if (SettingMainMode == 3)
				{
					SetImageColor(test_content, RGBA(0, 0, 0, 0), true);
					hiex::EasyX_Gdiplus_SolidRoundRect(0, 0, 800, 700, 20, 20, RGBA(255, 255, 255, 255), false, SmoothingModeHighQuality, &test_content);

					hiex::TransparentImage(&test_content, 100, 20, &SettingSign[1]);
					{
						Gdiplus::Font20 gp_font(&HarmonyOS_fontFamily, 20, FontStyleRegular, UnitPixel);
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

					Gdiplus::Font20 gp_font(&HarmonyOS_fontFamily, 18, FontStyleRegular, UnitPixel);
					SolidBrush WordBrush(hiex::ConvertToGdiplusColor(RGB(0, 0, 0), false));
					graphics.SetTextRenderingHint(TextRenderingHintAntiAliasGridFit);
					{
						dwords_rect.left = 0;
						dwords_rect.top = 305;
						dwords_rect.right = 800;
						dwords_rect.bottom = 630;
					}

					wstring text = L"程序版本：" + string_to_wstring(edition_code);
					text += L"\n程序发布版本：" + string_to_wstring(edition_date) + L" 默认分支";
					text += L"\n程序构建时间：" + buildTime;
					text += L"\n用户ID：" + userid;

					if (gif_load)
					{
						text += L"\n\n更新通道：EasterEgg 不参与更新";
						text += L"\n（参与更新需要删除同目录下的彩蛋 GIF 文件）";
					}
					else
					{
						text += L"\n\n更新通道：LTS";
						if (!server_code.empty() && server_code != "")
						{
							text += L"\n联网版本代号：" + string_to_wstring(server_code);
							if (server_code == "GWSR") text += L" （广外专用版本）";
						}
						if (procedure_updata_error == 1) text += L"\n程序自动更新：已启用";
						else if (procedure_updata_error == 2) text += L"\n程序自动更新：发生网络错误";
						else text += L"\n程序自动更新：载入中（等待服务器反馈）";
					}

					if (!server_feedback.empty() && server_feedback != "") text += L"\n服务器反馈信息：" + string_to_wstring(server_feedback);
					if (server_updata_error)
					{
						text += L"\n\n服务器通信错误：Error" + to_wstring(server_updata_error);
						if (!server_updata_error_reason.empty()) text += L"\n" + server_updata_error_reason;
					}

					graphics.DrawString(text.c_str(), -1, &gp_font, hiex::RECTToRectF(dwords_rect), &stringFormat, &WordBrush);

					{
						Gdiplus::Font20 gp_font(&HarmonyOS_fontFamily, 18, FontStyleRegular, UnitPixel);
						SolidBrush WordBrush(hiex::ConvertToGdiplusColor(RGB(6, 39, 182), false));
						graphics.SetTextRenderingHint(TextRenderingHintAntiAliasGridFit);
						{
							dwords_rect.left = 0;
							dwords_rect.top = 630;
							dwords_rect.right = 800;
							dwords_rect.bottom = 700;
						}
						graphics.DrawString(L"软件作者联系方式\nQQ: 2685549821\ne-mail: alan-crl@foxmail.com", -1, &gp_font, hiex::RECTToRectF(dwords_rect), &stringFormat, &WordBrush);
					}
				}

				//侧栏
				{
					Graphics graphics(GetImageHDC(&test_background));

					if (SettingMainMode == 1)
					{
						hiex::EasyX_Gdiplus_FillRoundRect(10, 10, 180, 60, 20, 20, RGBA(0, 111, 225, 255), RGBA(230, 230, 230, 255), 3, false, SmoothingModeHighQuality, &test_background);

						ChangeColor(test_icon[1], RGBA(0, 111, 225, 255));
						hiex::TransparentImage(&test_background, 20, 20, &test_icon[1]);

						Gdiplus::Font20 gp_font(&HarmonyOS_fontFamily, 24, FontStyleRegular, UnitPixel);
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

						Gdiplus::Font20 gp_font(&HarmonyOS_fontFamily, 24, FontStyleRegular, UnitPixel);
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

					if (SettingMainMode == 2)
					{
						hiex::EasyX_Gdiplus_FillRoundRect(10, 80, 180, 60, 20, 20, RGBA(0, 111, 225, 255), RGBA(230, 230, 230, 255), 3, false, SmoothingModeHighQuality, &test_background);

						ChangeColor(test_icon[2], RGBA(0, 111, 225, 255));
						hiex::TransparentImage(&test_background, 20, 90, &test_icon[2]);

						Gdiplus::Font20 gp_font(&HarmonyOS_fontFamily, 24, FontStyleRegular, UnitPixel);
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

						Gdiplus::Font20 gp_font(&HarmonyOS_fontFamily, 24, FontStyleRegular, UnitPixel);
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

					if (SettingMainMode == 3)
					{
						hiex::EasyX_Gdiplus_FillRoundRect(10, 150, 180, 60, 20, 20, RGBA(0, 111, 225, 255), RGBA(230, 230, 230, 255), 3, false, SmoothingModeHighQuality, &test_background);

						ChangeColor(test_icon[3], RGBA(0, 111, 225, 255));
						hiex::TransparentImage(&test_background, 20, 160, &test_icon[3]);

						Gdiplus::Font20 gp_font(&HarmonyOS_fontFamily, 24, FontStyleRegular, UnitPixel);
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

						Gdiplus::Font20 gp_font(&HarmonyOS_fontFamily, 24, FontStyleRegular, UnitPixel);
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

					hiex::TransparentImage(&test_background, 10, 486, &SettingSign[4]);
				}
				//标题栏
				{
					SetImageColor(test_title, RGBA(0, 0, 0, 0), true);
					hiex::EasyX_Gdiplus_SolidRoundRect(5, 5, 1000, 740, 20, 20, RGBA(0, 111, 225, 200), true, SmoothingModeHighQuality, &test_title);

					Graphics graphics(GetImageHDC(&test_title));
					Gdiplus::Font20 gp_font(&HarmonyOS_fontFamily, 22, FontStyleRegular, UnitPixel);
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

				if (gif_load && SettingMainMode == 1) GIF_dynamic_effect.draw();

				{
					RECT rect;
					GetWindowRect(setting_window, &rect);

					POINT ptDst = { rect.left, rect.top };
					ulwi.pptDst = &ptDst;
					ulwi.hdcSrc = GetImageHDC(&test_title);
					UpdateLayeredWindowIndirect(setting_window, &ulwi);
				}

				if (off_signal || !test.select) break;

				Sleep(20);
			}

			if (gif_load && GIF_dynamic_effect.IsPlaying())
			{
				GIF_dynamic_effect.pause();
				GIF_dynamic_effect.resetPlayState();
			}
		}
		else if (IsWindowVisible(setting_window)) ShowWindow(setting_window, SW_HIDE);
	}

	ShowWindow(setting_window, SW_HIDE);
	thread_status[L"SettingMain"] = false;

	return 0;
}
*/

void SettingSeekBar()
{
	if (!KEY_DOWN(VK_LBUTTON)) return;

	POINT p;
	GetCursorPos(&p);

	int pop_x = p.x - SettingWindowX;
	int pop_y = p.y - SettingWindowY;

	while (1)
	{
		if (!KEY_DOWN(VK_LBUTTON)) break;

		POINT p;
		GetCursorPos(&p);

		SetWindowPos(setting_window,
			NULL,
			SettingWindowX = p.x - pop_x,
			SettingWindowY = p.y - pop_y,
			0,
			0,
			SWP_NOACTIVATE | SWP_NOZORDER | SWP_NOSIZE);
	}

	return;
}
int SettingMain()
{
	while (!already) this_thread::sleep_for(chrono::milliseconds(50));
	thread_status[L"SettingMain"] = true;

	ImGuiWc = { sizeof(WNDCLASSEX), CS_CLASSDC, ImGuiWndProc, 0L, 0L, GetModuleHandle(NULL), NULL, NULL, NULL, NULL, _T("Idt ImGui Tool"), NULL };
	::RegisterClassEx(&ImGuiWc);
	setting_window = ::CreateWindow(ImGuiWc.lpszClassName, _T("Idt ImGui Tool"), WS_OVERLAPPEDWINDOW, SettingWindowX, SettingWindowY, SettingWindowWidth, SettingWindowHeight, NULL, NULL, ImGuiWc.hInstance, NULL);
	SetWindowLong(setting_window, GWL_STYLE, GetWindowLong(setting_window, GWL_STYLE) & ~(WS_CAPTION | WS_BORDER | WS_THICKFRAME));
	SetWindowPos(setting_window, NULL, 0, 0, 0, 0, SWP_NOSIZE | SWP_NOMOVE | SWP_NOZORDER | SWP_FRAMECHANGED);

	::ShowWindow(setting_window, SW_HIDE);
	::UpdateWindow(setting_window);
	CreateDeviceD3D(setting_window);

	IMGUI_CHECKVERSION();
	ImGui::CreateContext();
	ImGuiIO& io = ImGui::GetIO(); (void)io;
	io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;     // 启用键盘控制

	ImGui::StyleColorsLight();

	ImGui_ImplWin32_Init(setting_window);
	ImGui_ImplDX11_Init(g_pd3dDevice, g_pd3dDeviceContext);

	io.IniFilename = nullptr;

	HMODULE hModule = GetModuleHandle(NULL);
	HRSRC hResource = FindResource(hModule, MAKEINTRESOURCE(198), L"TTF");
	HGLOBAL hMemory = LoadResource(hModule, hResource);
	PVOID pResourceData = LockResource(hMemory);
	DWORD dwResourceSize = SizeofResource(hModule, hResource);
	ImFont* Font = io.Fonts->AddFontFromMemoryTTF(pResourceData, dwResourceSize, 26.0f, NULL, io.Fonts->GetGlyphRangesChineseFull());

	ImVec4 clear_color = ImVec4(0.45f, 0.55f, 0.60f, 1.00f);
	ImGuiStyle& style = ImGui::GetStyle();
	auto Color = style.Colors;

	style.ChildRounding = 8.0f;
	style.FrameRounding = 8.0f;
	style.GrabRounding = 8.0f;

	style.Colors[ImGuiCol_WindowBg] = ImVec4(255 / 255.0f, 255 / 255.0f, 255 / 255.0f, 1.0f);
	style.Colors[ImGuiCol_ButtonHovered] = ImVec4(235 / 255.0f, 235 / 255.0f, 235 / 255.0f, 1.0f);
	style.Colors[ImGuiCol_ButtonActive] = ImVec4(215 / 255.0f, 215 / 255.0f, 215 / 255.0f, 1.0f);

	loadimage(&SettingSign[1], L"PNG", L"sign2");
	{
		int width = SettingSign[1].getwidth();
		int height = SettingSign[1].getheight();
		DWORD* pMem = GetImageBuffer(&SettingSign[1]);

		unsigned char* data = new unsigned char[width * height * 4];
		for (int y = 0; y < height; ++y)
		{
			for (int x = 0; x < width; ++x)
			{
				DWORD color = pMem[y * width + x];
				unsigned char alpha = (color & 0xFF000000) >> 24;
				if (alpha != 0)
				{
					data[(y * width + x) * 4 + 0] = ((color & 0x00FF0000) >> 16) * 255 / alpha;
					data[(y * width + x) * 4 + 1] = ((color & 0x0000FF00) >> 8) * 255 / alpha;
					data[(y * width + x) * 4 + 2] = ((color & 0x000000FF) >> 0) * 255 / alpha;
				}
				else
				{
					data[(y * width + x) * 4 + 0] = 0;
					data[(y * width + x) * 4 + 1] = 0;
					data[(y * width + x) * 4 + 2] = 0;
				}
				data[(y * width + x) * 4 + 3] = alpha;
			}
		}

		bool ret = LoadTextureFromFile(data, width, height, &TextureSettingSign[1]);
		delete[] data;

		IM_ASSERT(ret);
	}
	loadimage(&SettingSign[2], L"PNG", L"sign3");
	{
		int width = SettingSign[2].getwidth();
		int height = SettingSign[2].getheight();
		DWORD* pMem = GetImageBuffer(&SettingSign[2]);

		unsigned char* data = new unsigned char[width * height * 4];
		for (int y = 0; y < height; ++y)
		{
			for (int x = 0; x < width; ++x)
			{
				DWORD color = pMem[y * width + x];
				unsigned char alpha = (color & 0xFF000000) >> 24;
				if (alpha != 0)
				{
					data[(y * width + x) * 4 + 0] = ((color & 0x00FF0000) >> 16) * 255 / alpha;
					data[(y * width + x) * 4 + 1] = ((color & 0x0000FF00) >> 8) * 255 / alpha;
					data[(y * width + x) * 4 + 2] = ((color & 0x000000FF) >> 0) * 255 / alpha;
				}
				else
				{
					data[(y * width + x) * 4 + 0] = 0;
					data[(y * width + x) * 4 + 1] = 0;
					data[(y * width + x) * 4 + 2] = 0;
				}
				data[(y * width + x) * 4 + 3] = alpha;
			}
		}

		bool ret = LoadTextureFromFile(data, width, height, &TextureSettingSign[2]);
		delete[] data;

		IM_ASSERT(ret);
	}
	loadimage(&SettingSign[3], L"PNG", L"sign4");
	loadimage(&SettingSign[4], L"PNG", L"sign5");
	{
		int width = SettingSign[4].getwidth();
		int height = SettingSign[4].getheight();
		DWORD* pMem = GetImageBuffer(&SettingSign[4]);

		unsigned char* data = new unsigned char[width * height * 4];
		for (int y = 0; y < height; ++y)
		{
			for (int x = 0; x < width; ++x)
			{
				DWORD color = pMem[y * width + x];
				unsigned char alpha = (color & 0xFF000000) >> 24;
				if (alpha != 0)
				{
					data[(y * width + x) * 4 + 0] = ((color & 0x00FF0000) >> 16) * 255 / alpha;
					data[(y * width + x) * 4 + 1] = ((color & 0x0000FF00) >> 8) * 255 / alpha;
					data[(y * width + x) * 4 + 2] = ((color & 0x000000FF) >> 0) * 255 / alpha;
				}
				else
				{
					data[(y * width + x) * 4 + 0] = 0;
					data[(y * width + x) * 4 + 1] = 0;
					data[(y * width + x) * 4 + 2] = 0;
				}
				data[(y * width + x) * 4 + 3] = alpha;
			}
		}

		bool ret = LoadTextureFromFile(data, width, height, &TextureSettingSign[4]);
		delete[] data;

		IM_ASSERT(ret);
	}

	//初始化定义变量
	hiex::tDelayFPS recond;
	POINT MoushPos = { 0,0 };
	bool ShowWindow = false;
	ImGuiToggleConfig config;
	config.FrameRounding = 0.3f;
	config.KnobRounding = 0.5f;
	config.Size = { 60.0f,30.0f };
	config.Flags = ImGuiToggleFlags_Animated;

	int PushStyleColorNum = 0, PushFontNum = 0;
	int QueryWaitingTime = 5;

	bool StartUp = false, CreateLnk = true;
	bool BrushRecover = setlist.BrushRecover, RubberRecover = setlist.RubberRecover;

	wstring ppt_LinkTest = LinkTest();
	wstring ppt_IsPptDependencyLoaded = L"组件应该没问题(状态显示存在问题，后面再修复)";// = IsPptDependencyLoaded(); //TODO 问题所在
	wstring receivedData;
	POINT pt;

	magnificationWindowReady++;
	while (!off_signal)
	{
		MSG msg;
		while (::PeekMessage(&msg, setting_window, 0U, 0U, PM_REMOVE))
		{
			::TranslateMessage(&msg);
			::DispatchMessage(&msg);

			switch (msg.message)
			{
			case WM_MOUSEMOVE:
				MoushPos.x = GET_X_LPARAM(msg.lParam);
				MoushPos.y = GET_Y_LPARAM(msg.lParam);

				break;

			case WM_LBUTTONDOWN:
				if (IsInRect(MoushPos.x, MoushPos.y, { 0,0,870,30 })) SettingSeekBar();

				break;
			}
		}
		hiex::DelayFPS(recond, 10);

		ImGui_ImplDX11_NewFrame();
		ImGui_ImplWin32_NewFrame();
		ImGui::NewFrame();

		{
			//定义栏操作
			static int Tab = 0;
			enum Tab
			{
				tab1,
				tab2,
				tab3,
				tab4,
				tab5
			};

			ImGui::SetNextWindowPos({ 0,0 });//设置窗口位置
			ImGui::SetNextWindowSize({ static_cast<float>(SettingWindowWidth),static_cast<float>(SettingWindowHeight) });//设置窗口大小

			Font->Scale = 0.76923076f, PushFontNum++, ImGui::PushFont(Font);
			ImGui::Begin(u8"智绘教选项", &test.select, ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoSavedSettings);//开始绘制窗口

			{
				ImGui::SetCursorPos({ 10.0f,44.0f });

				PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Button, Tab == Tab::tab1 ? ImVec4(0 / 255.0f, 111 / 255.0f, 225 / 255.0f, 1.0f) : ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
				PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, Tab == Tab::tab1 ? ImVec4(1.0f, 1.0f, 1.0f, 1.0f) : ImVec4(0.0f, 0.0f, 0.0f, 1.0f));
				if (Tab == Tab::tab1) PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0 / 255.0f, 111 / 255.0f, 225 / 255.0f, 1.0f));
				if (Tab == Tab::tab1) PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0 / 255.0f, 111 / 255.0f, 225 / 255.0f, 1.0f));
				if (ImGui::Button(u8"主页", { 100.0f,40.0f }))
				{
					Tab = Tab::tab1;
				}
				while (PushStyleColorNum) PushStyleColorNum--, ImGui::PopStyleColor();

				ImGui::SetCursorPos({ 10.0f,94.0f });

				PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Button, Tab == Tab::tab2 ? ImVec4(0 / 255.0f, 111 / 255.0f, 225 / 255.0f, 1.0f) : ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
				PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, Tab == Tab::tab2 ? ImVec4(1.0f, 1.0f, 1.0f, 1.0f) : ImVec4(0.0f, 0.0f, 0.0f, 1.0f));
				if (Tab == Tab::tab2) PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0 / 255.0f, 111 / 255.0f, 225 / 255.0f, 1.0f));
				if (Tab == Tab::tab2) PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0 / 255.0f, 111 / 255.0f, 225 / 255.0f, 1.0f));
				if (ImGui::Button(u8"常规", { 100.0f,40.0f }))
				{
					Tab = Tab::tab2;
				}
				while (PushStyleColorNum) PushStyleColorNum--, ImGui::PopStyleColor();

				ImGui::SetCursorPos({ 10.0f,144.0f });

				PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Button, Tab == Tab::tab3 ? ImVec4(0 / 255.0f, 111 / 255.0f, 225 / 255.0f, 1.0f) : ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
				PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, Tab == Tab::tab3 ? ImVec4(1.0f, 1.0f, 1.0f, 1.0f) : ImVec4(0.0f, 0.0f, 0.0f, 1.0f));
				if (Tab == Tab::tab3) PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0 / 255.0f, 111 / 255.0f, 225 / 255.0f, 1.0f));
				if (Tab == Tab::tab3) PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0 / 255.0f, 111 / 255.0f, 225 / 255.0f, 1.0f));
				if (ImGui::Button(u8"调测", { 100.0f,40.0f }))
				{
					Tab = Tab::tab3;
				}
				while (PushStyleColorNum) PushStyleColorNum--, ImGui::PopStyleColor();

				ImGui::SetCursorPos({ 10.0f,194.0f });

				PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Button, Tab == Tab::tab4 ? ImVec4(0 / 255.0f, 111 / 255.0f, 225 / 255.0f, 1.0f) : ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
				PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, Tab == Tab::tab4 ? ImVec4(1.0f, 1.0f, 1.0f, 1.0f) : ImVec4(0.0f, 0.0f, 0.0f, 1.0f));
				if (Tab == Tab::tab4) PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0 / 255.0f, 111 / 255.0f, 225 / 255.0f, 1.0f));
				if (Tab == Tab::tab4) PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0 / 255.0f, 111 / 255.0f, 225 / 255.0f, 1.0f));
				if (ImGui::Button(u8"关于", { 100.0f,40.0f }))
				{
					Tab = Tab::tab4;
				}
				while (PushStyleColorNum) PushStyleColorNum--, ImGui::PopStyleColor();

				ImGui::SetCursorPos({ 10.0f,244.0f });

				PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Button, Tab == Tab::tab5 ? ImVec4(0 / 255.0f, 111 / 255.0f, 225 / 255.0f, 1.0f) : ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
				PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, Tab == Tab::tab5 ? ImVec4(1.0f, 1.0f, 1.0f, 1.0f) : ImVec4(0.0f, 0.0f, 0.0f, 1.0f));
				if (Tab == Tab::tab5) PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0 / 255.0f, 111 / 255.0f, 225 / 255.0f, 1.0f));
				if (Tab == Tab::tab5) PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0 / 255.0f, 111 / 255.0f, 225 / 255.0f, 1.0f));
				if (ImGui::Button(u8"作者留言", { 100.0f,40.0f }))
				{
					Tab = Tab::tab5;
				}
				while (PushStyleColorNum) PushStyleColorNum--, ImGui::PopStyleColor();
			}

			ImGui::SetCursorPos({ 120.0f,44.0f });
			ImGui::BeginChild(u8"公告栏", { 770.0f,616.0f }, true);
			switch (Tab)
			{
			case Tab::tab1:
				// 主页
				Font->Scale = 0.76923076f, PushFontNum++, ImGui::PushFont(Font);

				ImGui::SetCursorPos({ 35.0f,ImGui::GetCursorPosY() + 40.0f });
				ImGui::Image((void*)TextureSettingSign[2], ImVec2((float)SettingSign[2].getwidth(), (float)SettingSign[2].getheight()));

				/*
				if (AutomaticUpdateStep == 0)
				{
					ImGui::SetCursorPos({ 185.0f - (float)SettingSign[4].getwidth() / 2.0f,ImGui::GetCursorPosY() + 20 });
					PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(235 / 255.0f, 235 / 255.0f, 235 / 255.0f, 1.0f));
					PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(215 / 255.0f, 215 / 255.0f, 215 / 255.0f, 1.0f));
					PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(195 / 255.0f, 195 / 255.0f, 195 / 255.0f, 1.0f));
					PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0 / 255.0f, 0 / 255.0f, 0 / 255.0f, 1.0f));
					if (ImGui::Button(u8"程序自动更新待启用", { 400.0f,50.0f }))
					{
						//TODO
					}
				}
				else if (AutomaticUpdateStep == 1)
				{
					ImGui::SetCursorPos({ 185.0f - (float)SettingSign[4].getwidth() / 2.0f,ImGui::GetCursorPosY() + 20 });
					PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(106 / 255.0f, 156 / 255.0f, 45 / 255.0f, 1.0f));
					PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(93 / 255.0f, 136 / 255.0f, 39 / 255.0f, 1.0f));
					PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(79 / 255.0f, 116 / 255.0f, 34 / 255.0f, 1.0f));
					PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(255 / 255.0f, 255 / 255.0f, 255 / 255.0f, 1.0f));
					if (ImGui::Button(u8"程序版本已经是最新", { 400.0f,50.0f }))
					{
						//TODO
					}
				}
				else if (AutomaticUpdateStep == 2)
				{
					ImGui::SetCursorPos({ 185.0f - (float)SettingSign[4].getwidth() / 2.0f,ImGui::GetCursorPosY() + 20 });
					PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(245 / 255.0f, 166 / 255.0f, 35 / 255.0f, 1.0f));
					PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(225 / 255.0f, 153 / 255.0f, 34 / 255.0f, 1.0f));
					PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(205 / 255.0f, 140 / 255.0f, 33 / 255.0f, 1.0f));
					PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(255 / 255.0f, 255 / 255.0f, 255 / 255.0f, 1.0f));
					if (ImGui::Button(u8"程序新版本排队下载中", { 400.0f,50.0f }))
					{
						//TODO
					}
				}
				else if (AutomaticUpdateStep == 3)
				{
					ImGui::SetCursorPos({ 185.0f - (float)SettingSign[4].getwidth() / 2.0f,ImGui::GetCursorPosY() + 20 });
					PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0 / 255.0f, 153 / 255.0f, 255 / 255.0f, 1.0f));
					PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0 / 255.0f, 141 / 255.0f, 235 / 255.0f, 1.0f));
					PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0 / 255.0f, 129 / 255.0f, 215 / 255.0f, 1.0f));
					PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(255 / 255.0f, 255 / 255.0f, 255 / 255.0f, 1.0f));
					if (ImGui::Button(u8"重启程序以更新到最新版本", { 400.0f,50.0f }))
					{
						//TODO
					}
				}
				while (PushStyleColorNum) PushStyleColorNum--, ImGui::PopStyleColor();

				{
					float CursorPosY = ImGui::GetCursorPosY();

					ImGui::SetCursorPos({ 185.0f - (float)SettingSign[4].getwidth() / 2.0f,CursorPosY + 10 });
					PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0 / 255.0f, 153 / 255.0f, 255 / 255.0f, 1.0f));
					PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0 / 255.0f, 141 / 255.0f, 235 / 255.0f, 1.0f));
					PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0 / 255.0f, 129 / 255.0f, 215 / 255.0f, 1.0f));
					PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(255 / 255.0f, 255 / 255.0f, 255 / 255.0f, 1.0f));
					if (ImGui::Button(u8"实验性新功能", { 400.0f,50.0f }))
					{
						//TODO
					}
					while (PushStyleColorNum) PushStyleColorNum--,ImGui::PopStyleColor();
				}*/

				{
					ImGui::SetCursorPosY(616.0f - 168.0f);
					std::wstring author = L"软件作者联系方式\nQQ: 2685549821\nEmail: alan-crl@foxmail.com";
					int left_x = 10, right_x = 570;

					std::vector<std::string> lines;
					std::wstring line, temp;
					std::wstringstream ss(author);

					while (getline(ss, temp, L'\n'))
					{
						bool flag = false;
						line = L"";

						for (wchar_t ch : temp)
						{
							flag = false;

							float text_width = ImGui::CalcTextSize(convert_to_utf8(wstring_to_string(line + ch)).c_str()).x;
							if (text_width > (right_x - left_x))
							{
								lines.emplace_back(convert_to_utf8(wstring_to_string(line)));
								line = L"", flag = true;
							}

							line += ch;
						}

						if (!flag) lines.emplace_back(convert_to_utf8(wstring_to_string(line)));
					}

					for (const auto& temp : lines)
					{
						float text_width = ImGui::CalcTextSize(temp.c_str()).x;
						float text_indentation = ((right_x - left_x) - text_width) * 0.5f;
						if (text_indentation < 0)  text_indentation = 0;
						ImGui::SetCursorPosX(left_x + text_indentation);
						ImGui::TextUnformatted(temp.c_str());
					}
				}
				{
					PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0 / 255.0f, 111 / 255.0f, 225 / 255.0f, 1.0f));

					std::wstring author = L"官方Q群：618720802\n感谢各位一直以来对智绘教的支持~";
					int left_x = 10, right_x = 570;

					std::vector<std::string> lines;
					std::wstring line, temp;
					std::wstringstream ss(author);

					while (getline(ss, temp, L'\n'))
					{
						bool flag = false;
						line = L"";

						for (wchar_t ch : temp)
						{
							flag = false;

							float text_width = ImGui::CalcTextSize(convert_to_utf8(wstring_to_string(line + ch)).c_str()).x;
							if (text_width > (right_x - left_x))
							{
								lines.emplace_back(convert_to_utf8(wstring_to_string(line)));
								line = L"", flag = true;
							}

							line += ch;
						}

						if (!flag) lines.emplace_back(convert_to_utf8(wstring_to_string(line)));
					}

					for (const auto& temp : lines)
					{
						float text_width = ImGui::CalcTextSize(temp.c_str()).x;
						float text_indentation = ((right_x - left_x) - text_width) * 0.5f;
						if (text_indentation < 0)  text_indentation = 0;
						ImGui::SetCursorPosX(left_x + text_indentation);
						ImGui::TextUnformatted(temp.c_str());
					}
				}

				ImGui::SetCursorPos({ 760.0f - (float)SettingSign[4].getwidth(),606.0f - (float)SettingSign[4].getheight() });
				ImGui::Image((void*)TextureSettingSign[4], ImVec2((float)SettingSign[4].getwidth(), (float)SettingSign[4].getheight()));

				break;

			case Tab::tab2:
				// 通用
				ImGui::SetCursorPosY(20.0f);

				{
					ImGui::SetCursorPosX(20.0f);
					ImGui::BeginChild(u8"程序环境", { 730.0f,125.0f }, true, ImGuiWindowFlags_NoScrollbar);

					{
						ImGui::SetCursorPosY(10.0f);

						Font->Scale = 1.0f, PushFontNum++, ImGui::PushFont(Font);
						PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0 / 255.0f, 0 / 255.0f, 0 / 255.0f, 1.0f));
						CenteredText(u8" 查询开机启动状态", 4.0f);

						Font->Scale = 0.7f, PushFontNum++, ImGui::PushFont(Font);
						ImGui::SameLine(); HelpMarker(u8"调整开机自动启动设置前需要查询当前状态（程序将申请管理员权限）", ImGui::GetStyleColorVec4(ImGuiCol_Text));

						if (!receivedData.empty())
						{
							string temp, helptemp;
							if (receivedData.length() >= 5 && receivedData.substr(0, 5) == L"Succe") temp = u8"查询状态成功", helptemp = u8"可以调整开机启动设置";
							else if (receivedData.length() >= 5 && receivedData.substr(0, 5) == L"Error") temp = u8"查询状态错误 " + wstring_to_string(receivedData), helptemp = u8"再次点击查询尝试，或重启程序以管理员身份运行";
							else if (receivedData.length() >= 5 && receivedData.substr(0, 5) == L"TimeO") temp = u8"查询状态超时", helptemp = u8"再次点击查询尝试\n同时超时时间将从 5 秒调整为 15 秒", QueryWaitingTime = 15;
							else if (receivedData.length() >= 5 && receivedData.substr(0, 5) == L"Renew") temp = u8"需要重新查询状态", helptemp = u8"调整开机自动启动设置时超时，请再次点击查询\n同时超时时间将从 5 秒调整为 15 秒", QueryWaitingTime = 15;
							else temp = u8"未知错误", helptemp = u8"再次点击查询尝试，或重启程序以管理员身份运行";

							Font->Scale = 1.0f, PushFontNum++, ImGui::PushFont(Font);
							ImGui::SameLine(); ImGui::SetCursorPosX(730.0f - 80.0f - ImGui::CalcTextSize(temp.c_str()).x - 30.0f);
							PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0 / 255.0f, 0 / 255.0f, 0 / 255.0f, 1.0f));
							CenteredText(temp.c_str(), 4.0f);

							Font->Scale = 0.7f, PushFontNum++, ImGui::PushFont(Font);
							ImGui::SameLine(); HelpMarker(helptemp.c_str(), ImGui::GetStyleColorVec4(ImGuiCol_Text));
						}

						Font->Scale = 0.7f, PushFontNum++, ImGui::PushFont(Font);
						ImGui::SameLine(); ImGui::SetCursorPosX(730.0f - 70.0f);
						PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(235 / 255.0f, 235 / 255.0f, 235 / 255.0f, 1.0f));
						PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(215 / 255.0f, 215 / 255.0f, 215 / 255.0f, 1.0f));
						PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(195 / 255.0f, 195 / 255.0f, 195 / 255.0f, 1.0f));
						PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0 / 255.0f, 0 / 255.0f, 0 / 255.0f, 1.0f));
						if (ImGui::Button(u8"查询", { 60.0f,30.0f }))
						{
							if (_waccess((string_to_wstring(global_path) + L"api").c_str(), 0) == -1) filesystem::create_directory(string_to_wstring(global_path) + L"api");
							ExtractResource((string_to_wstring(global_path) + L"api\\智绘教StartupItemSettings.exe").c_str(), L"EXE", MAKEINTRESOURCE(229));

							ShellExecute(NULL, L"runas", (string_to_wstring(global_path) + L"api\\智绘教StartupItemSettings.exe").c_str(), (L"/\"query\" /\"" + GetCurrentExePath() + L"\\智绘教.exe\"").c_str(), NULL, SW_SHOWNORMAL);

							DWORD dwBytesRead;
							WCHAR buffer[4096];
							HANDLE hPipe = INVALID_HANDLE_VALUE;

							int for_i;
							for (for_i = 0; for_i <= QueryWaitingTime * 10; for_i++)
							{
								if (WaitNamedPipe(TEXT("\\\\.\\pipe\\IDTPipe1"), 100)) break;
								else Sleep(100);
							}

							if (for_i > QueryWaitingTime * 10) receivedData = L"TimeOut";
							else
							{
								hPipe = CreateFile(TEXT("\\\\.\\pipe\\IDTPipe1"), GENERIC_READ | GENERIC_WRITE, 0, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
								if (ReadFile(hPipe, buffer, sizeof(buffer), &dwBytesRead, NULL))
								{
									receivedData.assign(buffer, dwBytesRead / sizeof(WCHAR));
									if (receivedData == L"fail")
									{
										receivedData = L"Success";
										setlist.StartUp = 1, StartUp = false;
									}
									else if (receivedData == L"success")
									{
										receivedData = L"Success";
										setlist.StartUp = 2, StartUp = true;
									}
									else receivedData = L"Error unknown";
								}
								else receivedData = L"Error" + to_wstring(GetLastError());
							}

							CloseHandle(hPipe);
						}
					}
					{
						ImGui::SetCursorPosY(45.0f);

						Font->Scale = 1.0f, PushFontNum++, ImGui::PushFont(Font);
						PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0 / 255.0f, 0 / 255.0f, 0 / 255.0f, 1.0f));
						CenteredText(u8" 开机自动启动", 4.0f);

						Font->Scale = 0.7f, PushFontNum++, ImGui::PushFont(Font);
						ImGui::SameLine();
						if (setlist.StartUp) HelpMarker(u8"程序将申请管理员权限", ImGui::GetStyleColorVec4(ImGuiCol_Text));
						else HelpMarker(u8"调整开机自动启动设置前需要查询当前状态（程序将申请管理员权限）\n请点击上方按钮查询当前状态", ImGui::GetStyleColorVec4(ImGuiCol_Text));

						if (setlist.StartUp)
						{
							PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0 / 255.0f, 111 / 255.0f, 225 / 255.0f, 1.0f));
							PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0 / 255.0f, 101 / 255.0f, 205 / 255.0f, 1.0f));
							PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(235 / 255.0f, 235 / 255.0f, 235 / 255.0f, 1.0f));
							PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, ImVec4(215 / 255.0f, 215 / 255.0f, 215 / 255.0f, 1.0f));
							PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(255 / 255.0f, 255 / 255.0f, 255 / 255.0f, 1.0f));
							ImGui::SameLine(); ImGui::SetCursorPosX(730.0f - 70.0f);
							ImGui::Toggle(u8"##开机自动启动", &StartUp, config);

							if (setlist.StartUp - 1 != (int)StartUp)
							{
								if (_waccess((string_to_wstring(global_path) + L"api").c_str(), 0) == -1) filesystem::create_directory(string_to_wstring(global_path) + L"api");
								ExtractResource((string_to_wstring(global_path) + L"api\\智绘教StartupItemSettings.exe").c_str(), L"EXE", MAKEINTRESOURCE(229));

								if (StartUp) ShellExecute(NULL, L"runas", (string_to_wstring(global_path) + L"api\\智绘教StartupItemSettings.exe").c_str(), (L"/\"set\" /\"" + GetCurrentExePath() + L"\"").c_str(), NULL, SW_SHOWNORMAL);
								else ShellExecute(NULL, L"runas", (string_to_wstring(global_path) + L"api\\智绘教StartupItemSettings.exe").c_str(), (L"/\"delete\" /\"" + GetCurrentExePath() + L"\"").c_str(), NULL, SW_SHOWNORMAL);

								DWORD dwBytesRead;
								WCHAR buffer[4096];
								HANDLE hPipe = INVALID_HANDLE_VALUE;
								wstring treceivedData;

								int for_i;
								for (for_i = 0; for_i <= QueryWaitingTime * 10; for_i++)
								{
									if (WaitNamedPipe(TEXT("\\\\.\\pipe\\IDTPipe1"), 100)) break;
									else Sleep(100);
								}

								if (for_i <= QueryWaitingTime * 10)
								{
									hPipe = CreateFile(TEXT("\\\\.\\pipe\\IDTPipe1"), GENERIC_READ | GENERIC_WRITE, 0, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
									if (ReadFile(hPipe, buffer, sizeof(buffer), &dwBytesRead, NULL))
									{
										treceivedData.assign(buffer, dwBytesRead / sizeof(WCHAR));
										if (treceivedData == L"fail") setlist.StartUp = 1, StartUp = false;
										else if (treceivedData == L"success") setlist.StartUp = 2, StartUp = true;
									}
								}
								else setlist.StartUp = 0, receivedData = L"Renew";

								CloseHandle(hPipe);
							}
						}
					}
					{
						ImGui::SetCursorPosY(80.0f);

						Font->Scale = 1.0f, PushFontNum++, ImGui::PushFont(Font);
						PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0 / 255.0f, 0 / 255.0f, 0 / 255.0f, 1.0f));
						CenteredText(u8" 启动时创建桌面快捷方式", 4.0f);

						Font->Scale = 0.7f, PushFontNum++, ImGui::PushFont(Font);
						ImGui::SameLine();
						HelpMarker(u8"程序将在每次启动时在桌面创建快捷方式\n后续这项功能将变身成为插件，并拥有更多的自定义功能", ImGui::GetStyleColorVec4(ImGuiCol_Text));

						PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0 / 255.0f, 111 / 255.0f, 225 / 255.0f, 1.0f));
						PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0 / 255.0f, 101 / 255.0f, 205 / 255.0f, 1.0f));
						PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(235 / 255.0f, 235 / 255.0f, 235 / 255.0f, 1.0f));
						PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, ImVec4(215 / 255.0f, 215 / 255.0f, 215 / 255.0f, 1.0f));
						PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(255 / 255.0f, 255 / 255.0f, 255 / 255.0f, 1.0f));
						ImGui::SameLine(); ImGui::SetCursorPosX(730.0f - 70.0f);
						ImGui::Toggle(u8"##启动时创建桌面快捷方式", &CreateLnk, config);

						if (setlist.CreateLnk != CreateLnk)
						{
							setlist.CreateLnk = CreateLnk;
							WriteSetting();
						}
					}

					ImGui::EndChild();
				}
				{
					ImGui::SetCursorPosX(20.0f);
					ImGui::BeginChild(u8"画笔调整", { 730.0f,90.0f }, true, ImGuiWindowFlags_NoScrollbar);

					{
						ImGui::SetCursorPosY(10.0f);

						Font->Scale = 1.0f, PushFontNum++, ImGui::PushFont(Font);
						PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0 / 255.0f, 0 / 255.0f, 0 / 255.0f, 1.0f));
						CenteredText(u8" 画笔绘制时自动收起主栏", 4.0f);

						PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0 / 255.0f, 111 / 255.0f, 225 / 255.0f, 1.0f));
						PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0 / 255.0f, 101 / 255.0f, 205 / 255.0f, 1.0f));
						PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(235 / 255.0f, 235 / 255.0f, 235 / 255.0f, 1.0f));
						PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, ImVec4(215 / 255.0f, 215 / 255.0f, 215 / 255.0f, 1.0f));
						PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(255 / 255.0f, 255 / 255.0f, 255 / 255.0f, 1.0f));
						ImGui::SameLine(); ImGui::SetCursorPosX(730.0f - 70.0f);
						ImGui::Toggle(u8"##画笔绘制时自动收起主栏", &BrushRecover, config);

						if (setlist.BrushRecover != BrushRecover)
						{
							setlist.BrushRecover = BrushRecover;
							WriteSetting();
						}
					}
					{
						ImGui::SetCursorPosY(45.0f);

						Font->Scale = 1.0f, PushFontNum++, ImGui::PushFont(Font);
						PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0 / 255.0f, 0 / 255.0f, 0 / 255.0f, 1.0f));
						CenteredText(u8" 橡皮擦除时自动收起主栏", 4.0f);

						PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0 / 255.0f, 111 / 255.0f, 225 / 255.0f, 1.0f));
						PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0 / 255.0f, 101 / 255.0f, 205 / 255.0f, 1.0f));
						PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(235 / 255.0f, 235 / 255.0f, 235 / 255.0f, 1.0f));
						PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, ImVec4(215 / 255.0f, 215 / 255.0f, 215 / 255.0f, 1.0f));
						PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(255 / 255.0f, 255 / 255.0f, 255 / 255.0f, 1.0f));
						ImGui::SameLine(); ImGui::SetCursorPosX(730.0f - 70.0f);
						ImGui::Toggle(u8"##橡皮擦除时自动收起主栏", &RubberRecover, config);

						if (setlist.RubberRecover != RubberRecover)
						{
							setlist.RubberRecover = RubberRecover;
							WriteSetting();
						}
					}

					ImGui::EndChild();
				}

				break;
			case Tab::tab3:
				//调测
				Font->Scale = 0.76923076f, PushFontNum++, ImGui::PushFont(Font);
				{
					ImGui::SetCursorPosY(10.0f);
					wstring text;
					{
						GetCursorPos(&pt);

						text = L"鼠标左键按下：";
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

						text += L"\n\n撤回库当前大小：" + to_wstring(RecallImage.size()) + L"(峰值" + to_wstring(RecallImagePeak) + L")";
						text += L"\n撤回库 recall_image_recond：" + to_wstring(recall_image_recond);
						text += L"\n撤回库 reference_record_pointer：" + to_wstring(reference_record_pointer);
						text += L"\n撤回库 practical_total_record_pointer：" + to_wstring(practical_total_record_pointer);
						text += L"\n撤回库 total_record_pointer：" + to_wstring(total_record_pointer);
						text += L"\n撤回库 current_record_pointer：" + to_wstring(current_record_pointer);
						text += L"\n\n首次绘制状态：", text += (FirstDraw == true) ? L"是" : L"否";

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
					}

					int left_x = 10, right_x = 760;

					std::vector<std::string> lines;
					std::wstring line, temp;
					std::wstringstream ss(text);

					while (getline(ss, temp, L'\n'))
					{
						bool flag = false;
						line = L"";

						for (wchar_t ch : temp)
						{
							flag = false;

							float text_width = ImGui::CalcTextSize(convert_to_utf8(wstring_to_string(line + ch)).c_str()).x;
							if (text_width > (right_x - left_x))
							{
								lines.emplace_back(convert_to_utf8(wstring_to_string(line)));
								line = L"", flag = true;
							}

							line += ch;
						}

						if (!flag) lines.emplace_back(convert_to_utf8(wstring_to_string(line)));
					}

					for (const auto& temp : lines)
					{
						float text_width = ImGui::CalcTextSize(temp.c_str()).x;
						float text_indentation = ((right_x - left_x) - text_width) * 0.5f;
						if (text_indentation < 0)  text_indentation = 0;
						ImGui::SetCursorPosX(left_x + text_indentation);
						ImGui::TextUnformatted(temp.c_str());
					}
				}
				break;

			case Tab::tab4:
				// 关于
				ImGui::SetCursorPos({ 35.0f,ImGui::GetCursorPosY() + 40.0f });
				ImGui::Image((void*)TextureSettingSign[1], ImVec2((float)SettingSign[1].getwidth(), (float)SettingSign[1].getheight()));

				Font->Scale = 0.76923076f, PushFontNum++, ImGui::PushFont(Font);
				{
					ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 20.0f);
					wstring text;
					{
						text = L"程序版本：" + string_to_wstring(edition_code);
						text += L"\n程序发布版本：" + string_to_wstring(edition_date) + L" 默认分支";
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
					}

					int left_x = 10, right_x = 760;

					std::vector<std::string> lines;
					std::wstring line, temp;
					std::wstringstream ss(text);

					while (getline(ss, temp, L'\n'))
					{
						bool flag = false;
						line = L"";

						for (wchar_t ch : temp)
						{
							flag = false;

							float text_width = ImGui::CalcTextSize(convert_to_utf8(wstring_to_string(line + ch)).c_str()).x;
							if (text_width > (right_x - left_x))
							{
								lines.emplace_back(convert_to_utf8(wstring_to_string(line)));
								line = L"", flag = true;
							}

							line += ch;
						}

						if (!flag) lines.emplace_back(convert_to_utf8(wstring_to_string(line)));
					}

					for (const auto& temp : lines)
					{
						float text_width = ImGui::CalcTextSize(temp.c_str()).x;
						float text_indentation = ((right_x - left_x) - text_width) * 0.5f;
						if (text_indentation < 0)  text_indentation = 0;
						ImGui::SetCursorPosX(left_x + text_indentation);
						ImGui::TextUnformatted(temp.c_str());
					}
				}

				break;
			case Tab::tab5:
				// 作者留言
				Font->Scale = 0.76923076f, PushFontNum++, ImGui::PushFont(Font);
				{
					ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 10.0f);
					wstring text;
					{
						text = L"作者留言\n\n";
						text += L"首先非常感谢您使用智绘教，也感谢您对智绘教的支持~\n\n";
						text += L"智绘教是 GPLv3 开源免费软件，欢迎大家加入智绘教的开发\n";
						text += L"目前急需解决的是程序的内存占用问题，智绘教占用的内存是在太大了！\n下个版本估计就会有所改善\n\n";
						text += L"近期我发现智绘教的用户量大幅增加，我非常感谢各位对智绘教的支持\n也感谢许多大佬和站长对智绘教的推广与肯定\n这两天我也得到了许多用户反馈，这个版本就是专门修复CPU占用率高，同时还解决了一些小问题\n\n";
						text += L"我会尽我所能开发智绘教，但是更新的速度和新功能的实现速度肯定比不上商业软件\n正处开学，时间紧迫，先写这么";
					}

					int left_x = 10, right_x = 760;

					std::vector<std::string> lines;
					std::wstring line, temp;
					std::wstringstream ss(text);

					while (getline(ss, temp, L'\n'))
					{
						bool flag = false;
						line = L"";

						for (wchar_t ch : temp)
						{
							flag = false;

							float text_width = ImGui::CalcTextSize(convert_to_utf8(wstring_to_string(line + ch)).c_str()).x;
							if (text_width > (right_x - left_x))
							{
								lines.emplace_back(convert_to_utf8(wstring_to_string(line)));
								line = L"", flag = true;
							}

							line += ch;
						}

						if (!flag) lines.emplace_back(convert_to_utf8(wstring_to_string(line)));
					}

					for (const auto& temp : lines)
					{
						float text_width = ImGui::CalcTextSize(temp.c_str()).x;
						float text_indentation = ((right_x - left_x) - text_width) * 0.5f;
						if (text_indentation < 0)  text_indentation = 0;
						ImGui::SetCursorPosX(left_x + text_indentation);
						ImGui::TextUnformatted(temp.c_str());
					}
				}

				break;
			}

			ImGui::EndChild();

			ImGui::SetCursorPos({ 120.0f,44.0f + 616.0f + 5.0f });
			Font->Scale = 0.76923076f, PushFontNum++, ImGui::PushFont(Font);
			PushStyleColorNum++, ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0 / 255.0f, 0 / 255.0f, 0 / 255.0f, 1.0f));
			CenteredText(u8"所有设置都会自动保存并立即生效", 4.0f);

			ImGui::End();

			ImGui::PopStyleColor(PushStyleColorNum);
			while (PushFontNum) PushFontNum--, ImGui::PopFont();
		}

		// 渲染
		ImGui::Render();
		const float clear_color_with_alpha[4] = { clear_color.x * clear_color.w, clear_color.y * clear_color.w, clear_color.z * clear_color.w, clear_color.w };
		g_pd3dDeviceContext->OMSetRenderTargets(1, &g_mainRenderTargetView, nullptr);
		g_pd3dDeviceContext->ClearRenderTargetView(g_mainRenderTargetView, clear_color_with_alpha);
		ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());

		g_pSwapChain->Present(1, 0); // 同步显示
		//g_pSwapChain->Present(0, 0); // 无同步显示

		if (!test.select)
		{
			if (ShowWindow && IsWindowVisible(setting_window)) ::ShowWindow(setting_window, SW_HIDE), ShowWindow = false;
			while (!test.select) Sleep(100);
		}
		else if (!ShowWindow && !IsWindowVisible(setting_window))
		{
			::ShowWindow(setting_window, SW_SHOW);
			::SetForegroundWindow(setting_window);
			ShowWindow = true;
		}
	}
	::ShowWindow(setting_window, SW_HIDE);

	ImGui_ImplDX11_Shutdown();
	ImGui_ImplWin32_Shutdown();
	//ImGui::DestroyContext(); //不知道为啥加了就蹦了

	CleanupDeviceD3D();
	::DestroyWindow(setting_window);
	::UnregisterClass(ImGuiWc.lpszClassName, ImGuiWc.hInstance);

	thread_status[L"SettingMain"] = false;

	return 0;
}

void FirstSetting(bool info)
{
	if (info && MessageBox(floating_window, L"欢迎使用智绘教~\n本向导还在建设中，目前较为简陋，请谅解\n\n是否尝试设置智绘教开机自动启动？", L"智绘教首次使用向导alpha", MB_YESNO | MB_SYSTEMMODAL) == 6)
	{
		if (_waccess((string_to_wstring(global_path) + L"api").c_str(), 0) == -1) filesystem::create_directory(string_to_wstring(global_path) + L"api");
		ExtractResource((string_to_wstring(global_path) + L"api\\智绘教StartupItemSettings.exe").c_str(), L"EXE", MAKEINTRESOURCE(229));

		ShellExecute(NULL, L"runas", (string_to_wstring(global_path) + L"api\\智绘教StartupItemSettings.exe").c_str(), (L"/\"set\" /\"" + GetCurrentExePath() + L"\"").c_str(), NULL, SW_SHOWNORMAL);

		DWORD dwBytesRead;
		WCHAR buffer[4096];
		HANDLE hPipe = INVALID_HANDLE_VALUE;
		wstring treceivedData;

		int for_i;
		for (for_i = 0; for_i <= 5000; for_i++)
		{
			if (WaitNamedPipe(TEXT("\\\\.\\pipe\\IDTPipe1"), 100)) break;
			else Sleep(100);
		}

		if (for_i <= 5000)
		{
			hPipe = CreateFile(TEXT("\\\\.\\pipe\\IDTPipe1"), GENERIC_READ | GENERIC_WRITE, 0, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
			if (ReadFile(hPipe, buffer, sizeof(buffer), &dwBytesRead, NULL)) treceivedData.assign(buffer, dwBytesRead / sizeof(WCHAR));
		}

		CloseHandle(hPipe);
	}

	if (_waccess((string_to_wstring(global_path) + L"opt").c_str(), 0) == -1) filesystem::create_directory(string_to_wstring(global_path) + L"opt");
	Json::StyledWriter outjson;
	Json::Value root;

	root["edition"] = Json::Value(edition_date);
	root["CreateLnk"] = Json::Value(true);
	root["BrushRecover"] = Json::Value(true);
	root["RubberRecover"] = Json::Value(false);
	root["SetSkinMode"] = Json::Value(0);

	ofstream writejson;
	writejson.imbue(locale("zh_CN.UTF8"));
	writejson.open(wstring_to_string(string_to_wstring(global_path) + L"opt\\deploy.json").c_str());
	writejson << outjson.write(root);
	writejson.close();
}
bool ReadSetting(bool first)
{
	Json::Reader reader;
	Json::Value root;

	ifstream readjson;
	readjson.imbue(locale("zh_CN.UTF8"));
	readjson.open(wstring_to_string(string_to_wstring(global_path) + L"opt\\deploy.json").c_str());

	if (reader.parse(readjson, root))
	{
		if (root.isMember("CreateLnk")) setlist.CreateLnk = root["CreateLnk"].asBool();
		if (root.isMember("BrushRecover")) setlist.BrushRecover = root["BrushRecover"].asBool();
		if (root.isMember("RubberRecover")) setlist.RubberRecover = root["RubberRecover"].asBool();
		if (root.isMember("SetSkinMode")) setlist.SetSkinMode = root["SetSkinMode"].asInt();

		//预处理
		if (first)
		{
			wstring time = CurrentDate();

			if (setlist.SetSkinMode == 0)
			{
				if (L"2024-01-22" <= time && time <= L"2024-02-23") setlist.SkinMode = 3;
				else setlist.SkinMode = 1;
			}
			else setlist.SkinMode = setlist.SetSkinMode;
		}
	}

	readjson.close();

	return true;
}
bool WriteSetting()
{
	Json::StyledWriter outjson;
	Json::Value root;

	root["edition"] = Json::Value(edition_date);
	root["CreateLnk"] = Json::Value(setlist.CreateLnk);
	root["BrushRecover"] = Json::Value(setlist.BrushRecover);
	root["RubberRecover"] = Json::Value(setlist.RubberRecover);
	root["SetSkinMode"] = Json::Value(setlist.SetSkinMode);

	ofstream writejson;
	writejson.imbue(locale("zh_CN.UTF8"));
	writejson.open(wstring_to_string(string_to_wstring(global_path) + L"opt\\deploy.json").c_str());
	writejson << outjson.write(root);
	writejson.close();

	return true;
}

// 辅助函数
bool CreateDeviceD3D(HWND hWnd)
{
	// 设置交换链
	DXGI_SWAP_CHAIN_DESC sd;
	ZeroMemory(&sd, sizeof(sd));
	sd.BufferCount = 2;
	sd.BufferDesc.Width = 0;
	sd.BufferDesc.Height = 0;
	sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
	sd.BufferDesc.RefreshRate.Numerator = 60;
	sd.BufferDesc.RefreshRate.Denominator = 1;
	sd.Flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH;
	sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
	sd.OutputWindow = hWnd;
	sd.SampleDesc.Count = 1;
	sd.SampleDesc.Quality = 0;
	sd.Windowed = TRUE;
	sd.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;

	UINT createDeviceFlags = 0;
	// createDeviceFlags |= D3D11_CREATE_DEVICE_DEBUG;
	D3D_FEATURE_LEVEL featureLevel;
	const D3D_FEATURE_LEVEL featureLevelArray[2] = { D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_0, };
	HRESULT res = D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, createDeviceFlags, featureLevelArray, 2, D3D11_SDK_VERSION, &sd, &g_pSwapChain, &g_pd3dDevice, &featureLevel, &g_pd3dDeviceContext);
	if (res == DXGI_ERROR_UNSUPPORTED) // 如果硬件不可用，请尝试使用高性能的 WARP 软件驱动程序。
		res = D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, createDeviceFlags, featureLevelArray, 2, D3D11_SDK_VERSION, &sd, &g_pSwapChain, &g_pd3dDevice, &featureLevel, &g_pd3dDeviceContext);
	if (res != S_OK)
		return false;

	CreateRenderTarget();
	return true;
}
void CleanupDeviceD3D()
{
	CleanupRenderTarget();
	if (g_pSwapChain) { g_pSwapChain->Release(); g_pSwapChain = nullptr; }
	if (g_pd3dDeviceContext) { g_pd3dDeviceContext->Release(); g_pd3dDeviceContext = nullptr; }
	if (g_pd3dDevice) { g_pd3dDevice->Release(); g_pd3dDevice = nullptr; }
}

void CreateRenderTarget()
{
	ID3D11Texture2D* pBackBuffer;
	g_pSwapChain->GetBuffer(0, IID_PPV_ARGS(&pBackBuffer));
	g_pd3dDevice->CreateRenderTargetView(pBackBuffer, nullptr, &g_mainRenderTargetView);
	pBackBuffer->Release();
}
void CleanupRenderTarget()
{
	if (g_mainRenderTargetView) { g_mainRenderTargetView->Release(); g_mainRenderTargetView = nullptr; }
}

bool LoadTextureFromFile(unsigned char* image_data, int width, int height, ID3D11ShaderResourceView** out_srv)
{
	if (image_data == NULL) return false;

	// Create texture
	D3D11_TEXTURE2D_DESC desc;
	ZeroMemory(&desc, sizeof(desc));
	desc.Width = width;
	desc.Height = height;
	desc.MipLevels = 1;
	desc.ArraySize = 1;
	desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
	desc.SampleDesc.Count = 1;
	desc.Usage = D3D11_USAGE_DEFAULT;
	desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
	desc.CPUAccessFlags = 0;

	ID3D11Texture2D* pTexture = NULL;
	D3D11_SUBRESOURCE_DATA subResource;
	subResource.pSysMem = image_data;
	subResource.SysMemPitch = desc.Width * 4;
	subResource.SysMemSlicePitch = 0;
	g_pd3dDevice->CreateTexture2D(&desc, &subResource, &pTexture);

	// Create texture view
	D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc;
	ZeroMemory(&srvDesc, sizeof(srvDesc));
	srvDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
	srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
	srvDesc.Texture2D.MipLevels = desc.MipLevels;
	srvDesc.Texture2D.MostDetailedMip = 0;
	g_pd3dDevice->CreateShaderResourceView(pTexture, &srvDesc, out_srv);
	pTexture->Release();

	return true;
}

// 从 imgui_impl_win32.cpp 中前向声明消息处理器
extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);
// Win32 消息处理器
// 您可以阅读 io.WantCaptureMouse、io.WantCaptureKeyboard 标志，以了解 dear imgui 是否想使用您的输入。
// - 当 io.WantCaptureMouse 为 true 时，请勿将鼠标输入数据发送到主应用程序，或者清除/覆盖鼠标数据的副本。
// - 当 io.WantCaptureKeyboard 为 true 时，请勿将键盘输入数据发送到主应用程序，或者清除/覆盖键盘数据的副本。
// 通常，您可以始终将所有输入传递给 dear imgui，并根据这两个标志在应用程序中隐藏它们。
LRESULT WINAPI ImGuiWndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
	if (ImGui_ImplWin32_WndProcHandler(hWnd, msg, wParam, lParam))
		return true;

	switch (msg)
	{
	case WM_SIZE:
		if (wParam == SIZE_MINIMIZED)
			return 0;
		g_ResizeWidth = (UINT)LOWORD(lParam); // 排队调整大小
		g_ResizeHeight = (UINT)HIWORD(lParam);
		return 0;
	case WM_SYSCOMMAND:
		if ((wParam & 0xfff0) == SC_KEYMENU) // 禁用 ALT 应用程序菜单
			return 0;
		break;
	case WM_DESTROY:
		::PostQuitMessage(0);
		return 0;
	}
	return ::DefWindowProcW(hWnd, msg, wParam, lParam);
}