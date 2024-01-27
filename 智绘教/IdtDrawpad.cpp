#include "IdtDrawpad.h"

bool main_open;
int TestMainMode = 1;
bool FirstDraw = true;

//将要废弃的绘制函数
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
						dwords_rect.bottom = 630;
					}

					wstring text = L"程序版本：" + string_to_wstring(edition_code);
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

					graphics.DrawString(text.c_str(), -1, &gp_font, hiex::RECTToRectF(dwords_rect), &stringFormat, &WordBrush);

					{
						Gdiplus::Font gp_font(&HarmonyOS_fontFamily, 18, FontStyleRegular, UnitPixel);
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

unordered_map<LONG, shared_mutex> StrokeImageSm;
shared_mutex StrokeImageListSm;
map<LONG, pair<IMAGE*, int>> StrokeImage; // second 表示绘制状态 01画笔 23橡皮 （单绘制/双停止）
vector<LONG> StrokeImageList;
shared_mutex StrokeBackImageSm;

IMAGE drawpad(GetSystemMetrics(SM_CXSCREEN), GetSystemMetrics(SM_CYSCREEN)); //主画板
IMAGE window_background(GetSystemMetrics(SM_CXSCREEN), GetSystemMetrics(SM_CYSCREEN));

void MultiFingerDrawing(LONG pid, POINT pt)
{
	struct Mouse
	{
		int x = 0, y = 0;
		int last_x = 0, last_y = 0;
		int last_length = 0;
	} mouse;
	struct
	{
		bool rubber_choose = rubber.select, brush_choose = brush.select;
		int width = brush.width, mode = brush.mode;
		COLORREF color = brush.color;
	}draw_info;

	IMAGE Canvas(GetSystemMetrics(SM_CXSCREEN), GetSystemMetrics(SM_CYSCREEN));
	SetImageColor(Canvas, RGBA(0, 0, 0, 0), true);
	IMAGE* BackCanvas = new IMAGE;

	std::chrono::high_resolution_clock::time_point start;
	if (draw_info.rubber_choose == true)
	{
		mouse.last_x = pt.x, mouse.last_y = pt.y;
		double rubbersize = 15, trubbersize = -1;

		//首次绘制
		std::shared_lock<std::shared_mutex> lock1(StrokeBackImageSm);
		Graphics eraser(GetImageHDC(&drawpad));
		lock1.unlock();
		{
			GraphicsPath path;
			path.AddEllipse(float(mouse.last_x - rubbersize / 2.0f), float(mouse.last_y - rubbersize / 2.0f), float(rubbersize), float(rubbersize));

			Region region(&path);
			eraser.SetClip(&region, CombineModeReplace);

			std::unique_lock<std::shared_mutex> lock1(StrokeBackImageSm);
			eraser.Clear(Color(0, 0, 0, 0));
			lock1.unlock();

			eraser.ResetClip();

			std::unique_lock<std::shared_mutex> LockExtremePointSm(ExtremePointSm);
			for (const auto& [point, value] : extreme_point)
			{
				if (value == true && region.IsVisible(point.first, point.second))
				{
					extreme_point[{point.first, point.second}] = false;
				}
			}
			LockExtremePointSm.unlock();

			hiex::EasyX_Gdiplus_Ellipse(mouse.last_x - (float)(rubbersize) / 2, mouse.last_y - (float)(rubbersize) / 2, (float)rubbersize, (float)rubbersize, RGBA(130, 130, 130, 200), 3, true, SmoothingModeHighQuality, &Canvas);
		}

		std::unique_lock<std::shared_mutex> lock2(StrokeImageSm[pid]);
		StrokeImage[pid] = make_pair(&Canvas, 2552);
		lock2.unlock();
		std::unique_lock<std::shared_mutex> lock3(StrokeImageListSm);
		StrokeImageList.emplace_back(pid);
		lock3.unlock();

		while (1)
		{
			std::shared_lock<std::shared_mutex> lock1(PointPosSm);
			bool unfind = TouchPos.find(pid) == TouchPos.end();
			if (unfind)
			{
				lock1.unlock();
				break;
			}
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
				if (speed <= 0.2) trubbersize = 60;
				else if (speed <= 20) trubbersize = max(25, speed * 2.33 + 13.33);
				else trubbersize = min(200, 3 * speed);

				if (trubbersize == -1) trubbersize = rubbersize;
				if (rubbersize < trubbersize) rubbersize = rubbersize + max(0.1, (trubbersize - rubbersize) / 50);
				else if (rubbersize > trubbersize) rubbersize = rubbersize + min(-0.1, (trubbersize - rubbersize) / 50);
			}
			else rubbersize = 60;

			if ((pt.x == mouse.last_x && pt.y == mouse.last_y))
			{
				GraphicsPath path;
				path.AddEllipse(float(mouse.last_x - rubbersize / 2.0f), float(mouse.last_y - rubbersize / 2.0f), float(rubbersize), float(rubbersize));

				Region region(&path);
				eraser.SetClip(&region, CombineModeReplace);

				std::unique_lock<std::shared_mutex> lock1(StrokeBackImageSm);
				eraser.Clear(Color(0, 0, 0, 0));
				lock1.unlock();

				eraser.ResetClip();

				std::unique_lock<std::shared_mutex> LockExtremePointSm(ExtremePointSm);
				for (const auto& [point, value] : extreme_point)
				{
					if (value == true && region.IsVisible(point.first, point.second))
					{
						extreme_point[{point.first, point.second}] = false;
					}
				}
				LockExtremePointSm.unlock();

				std::unique_lock<std::shared_mutex> lock2(StrokeImageSm[pid]);
				SetImageColor(Canvas, RGBA(0, 0, 0, 0), true);
				hiex::EasyX_Gdiplus_Ellipse(mouse.x - (float)(rubbersize) / 2, mouse.y - (float)(rubbersize) / 2, (float)rubbersize, (float)rubbersize, RGBA(130, 130, 130, 200), 3, true, SmoothingModeHighQuality, &Canvas);
				lock2.unlock();
			}
			else
			{
				GraphicsPath path;
				path.AddLine(mouse.last_x, mouse.last_y, mouse.x, mouse.y);

				Pen pen(Color(0, 0, 0, 0), Gdiplus::REAL(rubbersize));
				pen.SetStartCap(LineCapRound);
				pen.SetEndCap(LineCapRound);

				GraphicsPath* widenedPath = path.Clone();
				widenedPath->Widen(&pen);
				Region region(widenedPath);
				eraser.SetClip(&region, CombineModeReplace);

				std::unique_lock<std::shared_mutex> lock1(StrokeBackImageSm);
				eraser.Clear(Color(0, 0, 0, 0));
				lock1.unlock();

				eraser.ResetClip();

				std::unique_lock<std::shared_mutex> LockExtremePointSm(ExtremePointSm);
				for (const auto& [point, value] : extreme_point)
				{
					if (value == true && region.IsVisible(point.first, point.second))
					{
						extreme_point[{point.first, point.second}] = false;
					}
				}
				LockExtremePointSm.unlock();
				delete widenedPath;

				std::unique_lock<std::shared_mutex> lock2(StrokeImageSm[pid]);
				SetImageColor(Canvas, RGBA(0, 0, 0, 0), true);
				hiex::EasyX_Gdiplus_Ellipse(mouse.x - (float)(rubbersize) / 2, mouse.y - (float)(rubbersize) / 2, (float)rubbersize, (float)rubbersize, RGBA(130, 130, 130, 200), 3, true, SmoothingModeHighQuality, &Canvas);
				lock2.unlock();
			}

			mouse.last_x = mouse.x, mouse.last_y = mouse.y;
		}
		SetImageColor(Canvas, RGBA(0, 0, 0, 0), true);
	}
	else if (draw_info.brush_choose == true)
	{
		mouse.last_x = pt.x, mouse.last_y = pt.y;

		double writing_distance = 0;
		int instant_writing_distance = 0;
		vector<Point> points = { Point(mouse.last_x, mouse.last_y) };
		RECT circumscribed_rectangle = { -1,-1,-1,-1 };

		//首次绘制
		Graphics graphics(GetImageHDC(&Canvas));
		graphics.SetSmoothingMode(SmoothingModeHighQuality);
		if (draw_info.mode == 1 || draw_info.mode == 2) hiex::EasyX_Gdiplus_SolidEllipse(float((float)mouse.last_x - (float)(draw_info.width) / 2.0), float((float)mouse.last_y - (float)(draw_info.width) / 2.0), (float)draw_info.width, (float)draw_info.width, draw_info.color, false, SmoothingModeHighQuality, &Canvas);

		std::unique_lock<std::shared_mutex> lock1(StrokeImageSm[pid]);
		StrokeImage[pid] = make_pair(&Canvas, draw_info.mode == 2 ? (/*draw_info.color >> 24*/130) * 10 + 0 : 2550);
		lock1.unlock();
		std::unique_lock<std::shared_mutex> lock2(StrokeImageListSm);
		StrokeImageList.emplace_back(pid);
		lock2.unlock();

		clock_t tRecord = clock();
		while (1)
		{
			std::shared_lock<std::shared_mutex> lock0(PointPosSm);
			bool unfind = TouchPos.find(pid) == TouchPos.end();
			if (unfind)
			{
				lock0.unlock();
				break;
			}
			pt = TouchPos[pid].pt;
			lock0.unlock();

			std::shared_lock<std::shared_mutex> lock2(PointListSm);
			auto it = std::find(TouchList.begin(), TouchList.end(), pid);
			lock2.unlock();

			if (it == TouchList.end()) break;
			if (pt.x == mouse.last_x && pt.y == mouse.last_y) continue;

			if (draw_info.mode == 3)
			{
				Pen pen(hiex::ConvertToGdiplusColor(draw_info.color, false));
				pen.SetStartCap(LineCapRound);
				pen.SetEndCap(LineCapRound);
				pen.SetWidth(Gdiplus::REAL(draw_info.width));

				std::unique_lock<std::shared_mutex> LockStrokeImageSm(StrokeImageSm[pid]);
				SetImageColor(Canvas, RGBA(0, 0, 0, 0), true);
				graphics.DrawLine(&pen, mouse.last_x, mouse.last_y, pt.x, pt.y);
				LockStrokeImageSm.unlock();
			}
			else if (draw_info.mode == 4)
			{
				int rectangle_x = min(mouse.last_x, pt.x), rectangle_y = min(mouse.last_y, pt.y);
				int rectangle_heigth = abs(mouse.last_x - pt.x) + 1, rectangle_width = abs(mouse.last_y - pt.y) + 1;

				std::unique_lock<std::shared_mutex> LockStrokeImageSm(StrokeImageSm[pid]);
				SetImageColor(Canvas, RGBA(0, 0, 0, 0), true);
				hiex::EasyX_Gdiplus_RoundRect((float)rectangle_x, (float)rectangle_y, (float)rectangle_heigth, (float)rectangle_width, 3, 3, draw_info.color, (float)draw_info.width, false, SmoothingModeHighQuality, &Canvas);
				LockStrokeImageSm.unlock();

				if (points.size() < 2) points.emplace_back(Point(pt.x, pt.y));
				else points[1] = Point(pt.x, pt.y);
			}
			else
			{
				writing_distance += EuclideanDistance({ mouse.last_x, mouse.last_y }, { pt.x, pt.y });

				Pen pen(hiex::ConvertToGdiplusColor(draw_info.color, false));
				pen.SetEndCap(LineCapRound);
				pen.SetWidth((float)draw_info.width);

				std::unique_lock<std::shared_mutex> lock1(StrokeImageSm[pid]);
				graphics.DrawLine(&pen, mouse.last_x, mouse.last_y, (mouse.x = pt.x), (mouse.y = pt.y));
				lock1.unlock();

				{
					if (mouse.x < circumscribed_rectangle.left || circumscribed_rectangle.left == -1) circumscribed_rectangle.left = mouse.x;
					if (mouse.y < circumscribed_rectangle.top || circumscribed_rectangle.top == -1) circumscribed_rectangle.top = mouse.y;
					if (mouse.x > circumscribed_rectangle.right || circumscribed_rectangle.right == -1) circumscribed_rectangle.right = mouse.x;
					if (mouse.y > circumscribed_rectangle.bottom || circumscribed_rectangle.bottom == -1) circumscribed_rectangle.bottom = mouse.y;

					instant_writing_distance += (int)EuclideanDistance({ mouse.last_x, mouse.last_y }, { pt.x, pt.y });
					if (instant_writing_distance >= 4)
					{
						points.push_back(Point(mouse.x, mouse.y));
						instant_writing_distance %= 4;
					}
				}

				mouse.last_x = mouse.x, mouse.last_y = mouse.y;
			}

			//防止写锁过快导致无法读锁
			if (draw_info.mode == 3 || draw_info.mode == 4)
			{
				if (tRecord)
				{
					int delay = 1000 / 24 - (clock() - tRecord);
					if (delay > 0) std::this_thread::sleep_for(std::chrono::milliseconds(delay));
				}
				tRecord = clock();
			}
		}

		start = std::chrono::high_resolution_clock::now();
		std::unique_lock<std::shared_mutex> lock3(StrokeImageSm[pid]);
		*BackCanvas = Canvas;
		StrokeImage[pid] = make_pair(BackCanvas, draw_info.mode == 2 ? (/*draw_info.color >> 24*/130) * 10 + 0 : 2550);
		lock3.unlock();

		//智能绘图模块
		if (draw_info.mode == 1)
		{
			double redundance = max(GetSystemMetrics(SM_CXSCREEN) / 192, min((GetSystemMetrics(SM_CXSCREEN)) / 76.8, double(GetSystemMetrics(SM_CXSCREEN)) / double((-0.036) * writing_distance + 135)));

			//直线绘制
			if (writing_distance >= 120 && (abs(circumscribed_rectangle.left - circumscribed_rectangle.right) >= 120 || abs(circumscribed_rectangle.top - circumscribed_rectangle.bottom) >= 120) && isLine(points, int(redundance), std::chrono::high_resolution_clock::now()))
			{
				Point start(points[0]), end(points[points.size() - 1]);

				//端点匹配
				{
					//起点匹配
					{
						Point start_target = start;
						double distance = 10;

						std::shared_lock<std::shared_mutex> LockExtremePointSm(ExtremePointSm);
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
						LockExtremePointSm.unlock();

						start = start_target;
					}
					//终点匹配
					{
						Point end_target = end;
						double distance = 10;

						std::shared_lock<std::shared_mutex> LockExtremePointSm(ExtremePointSm);
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
						LockExtremePointSm.unlock();

						end = end_target;
					}
				}

				std::unique_lock<std::shared_mutex> LockExtremePointSm(ExtremePointSm);
				extreme_point[{start.X, start.Y}] = extreme_point[{end.X, end.Y}] = true;
				LockExtremePointSm.unlock();

				SetImageColor(Canvas, RGBA(0, 0, 0, 0), true);

				Graphics graphics(GetImageHDC(&Canvas));
				graphics.SetSmoothingMode(SmoothingModeHighQuality);

				Pen pen(hiex::ConvertToGdiplusColor(draw_info.color, false));
				pen.SetStartCap(LineCapRound);
				pen.SetEndCap(LineCapRound);

				pen.SetWidth(Gdiplus::REAL(draw_info.width));
				graphics.DrawLine(&pen, start.X, start.Y, end.X, end.Y);
			}
			//平滑曲线
			else if (points.size() > 2)
			{
				SetImageColor(Canvas, RGBA(0, 0, 0, 0), true);

				Graphics graphics(GetImageHDC(&Canvas));
				graphics.SetSmoothingMode(SmoothingModeHighQuality);

				Pen pen(hiex::ConvertToGdiplusColor(draw_info.color, false));
				pen.SetLineJoin(LineJoinRound);
				pen.SetStartCap(LineCapRound);
				pen.SetEndCap(LineCapRound);

				pen.SetWidth(Gdiplus::REAL(draw_info.width));
				graphics.DrawCurve(&pen, points.data(), points.size(), 0.4f);
			}
		}
		else if (draw_info.mode == 2)
		{
			//平滑曲线
			if (points.size() > 2)
			{
				SetImageColor(Canvas, RGBA(0, 0, 0, 0), true);

				Graphics graphics(GetImageHDC(&Canvas));
				graphics.SetSmoothingMode(SmoothingModeHighQuality);

				Pen pen(hiex::ConvertToGdiplusColor(draw_info.color, false));
				pen.SetLineJoin(LineJoinRound);
				pen.SetStartCap(LineCapRound);
				pen.SetEndCap(LineCapRound);

				pen.SetWidth(Gdiplus::REAL(draw_info.width));
				graphics.DrawCurve(&pen, points.data(), points.size(), 0.4f);
			}
		}
		else if (draw_info.mode == 3); // 直线吸附待实现
		else if (draw_info.mode == 4)
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

						std::shared_lock<std::shared_mutex> LockExtremePointSm(ExtremePointSm);
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
						LockExtremePointSm.unlock();

						l1 = idx;

						l2.X = l1.X;
						r1.Y = l1.Y;
					}
					{
						Point idx = l2;
						double distance = 10;

						std::shared_lock<std::shared_mutex> LockExtremePointSm(ExtremePointSm);
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
						LockExtremePointSm.unlock();

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

				std::unique_lock<std::shared_mutex> LockExtremePointSm(ExtremePointSm);
				extreme_point[{l1.X, l1.Y}] = true;
				extreme_point[{l2.X, l2.Y}] = true;
				extreme_point[{r1.X, r1.Y}] = true;
				extreme_point[{r2.X, r2.Y}] = true;
				LockExtremePointSm.unlock();

				SetImageColor(Canvas, RGBA(0, 0, 0, 0), true);

				int x = min(l1.X, r2.X);
				int y = min(l1.Y, r2.Y);
				int w = abs(l1.X - r2.X) + 1;
				int h = abs(l1.Y - r2.Y) + 1;

				SetImageColor(Canvas, RGBA(0, 0, 0, 0), true);
				hiex::EasyX_Gdiplus_RoundRect((float)x, (float)y, (float)w, (float)h, 3, 3, draw_info.color, (float)draw_info.width, false, SmoothingModeHighQuality, &Canvas);
			}
		}
	}

	std::unique_lock<std::shared_mutex> lock3(PointPosSm);
	TouchPos.erase(pid);
	lock3.unlock();

	std::unique_lock<std::shared_mutex> lock4(StrokeImageSm[pid]);
	*BackCanvas = Canvas;
	if (draw_info.rubber_choose == true) StrokeImage[pid] = make_pair(BackCanvas, 2553);
	else if (draw_info.brush_choose == true)
	{
		if (draw_info.mode == 2) StrokeImage[pid] = make_pair(BackCanvas, (/*draw_info.color >> 24*/130) * 10 + 1);
		else StrokeImage[pid] = make_pair(BackCanvas, 2551);
	}
	lock4.unlock();
}
void DrawpadDrawing()
{
	thread_status[L"DrawpadDrawing"] = true;

	// 设置BLENDFUNCTION结构体
	BLENDFUNCTION blend;
	blend.BlendOp = AC_SRC_OVER;
	blend.BlendFlags = 0;
	blend.SourceConstantAlpha = 255; // 设置透明度，0为全透明，255为不透明
	blend.AlphaFormat = AC_SRC_ALPHA; // 使用源图像的alpha通道
	HDC hdcScreen = GetDC(NULL);
	// 调用UpdateLayeredWindow函数更新窗口
	POINT ptSrc = { 0,0 };
	SIZE sizeWnd = { GetSystemMetrics(SM_CXSCREEN), GetSystemMetrics(SM_CYSCREEN) };
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

	window_background.Resize(GetSystemMetrics(SM_CXSCREEN), GetSystemMetrics(SM_CYSCREEN));
	drawpad.Resize(GetSystemMetrics(SM_CXSCREEN), GetSystemMetrics(SM_CYSCREEN));

	SetImageColor(window_background, RGBA(0, 0, 0, 1), true);
	SetImageColor(drawpad, RGBA(0, 0, 0, 0), true);
	{
		ulwi.hdcSrc = GetImageHDC(&window_background);
		UpdateLayeredWindowIndirect(drawpad_window, &ulwi);
	}
	ShowWindow(drawpad_window, SW_SHOW);

	chrono::high_resolution_clock::time_point reckon;
	clock_t tRecord = 0;
	for (;;)
	{
		for (int for_i = 1;; for_i = 2)
		{
			if (choose.select == true)
			{
			ChooseEnd:
				{
					SetImageColor(window_background, RGBA(0, 0, 0, 0), true);
					ulwi.hdcSrc = GetImageHDC(&window_background);
					UpdateLayeredWindowIndirect(drawpad_window, &ulwi);
				}

				IMAGE empty_drawpad = CreateImageColor(drawpad.getwidth(), drawpad.getheight(), RGBA(0, 0, 0, 0), true);
				if (reference_record_pointer == current_record_pointer && !CompareImagesWithBuffer(&empty_drawpad, &drawpad))
				{
					if (RecallImage.empty())
					{
						bool save_recond = false;
						if (recall_image_reference > recall_image_recond) recall_image_recond++;
						else recall_image_recond = recall_image_reference = recall_image_reference + 1, save_recond = true;

						if (recall_image_recond % 10 == 0 && save_recond && recall_image_recond >= 20)
						{
							thread SaveScreenShot_thread(SaveScreenShot, RecallImage[0].img, false);
							SaveScreenShot_thread.detach();
						}

						std::unique_lock<std::shared_mutex> LockStrokeBackImageSm(StrokeBackImageSm);
						std::unique_lock<std::shared_mutex> LockExtremePointSm(ExtremePointSm);

						RecallImage.push_back({ drawpad, extreme_point, 0, make_pair(recall_image_recond, recall_image_reference) });

						LockExtremePointSm.unlock();
						LockStrokeBackImageSm.unlock();
					}
					else if (!RecallImage.empty() && !CompareImagesWithBuffer(&drawpad, &RecallImage.back().img))
					{
						bool save_recond = false;
						if (recall_image_reference > recall_image_recond) recall_image_recond++;
						else recall_image_recond = recall_image_reference = recall_image_reference + 1, save_recond = true;

						if (recall_image_recond % 10 == 0 && save_recond && recall_image_recond >= 20)
						{
							thread SaveScreenShot_thread(SaveScreenShot, RecallImage[0].img, false);
							SaveScreenShot_thread.detach();
						}

						std::unique_lock<std::shared_mutex> LockStrokeBackImageSm(StrokeBackImageSm);
						std::unique_lock<std::shared_mutex> LockExtremePointSm(ExtremePointSm);

						RecallImage.push_back({ drawpad, extreme_point, 0, make_pair(recall_image_recond, recall_image_reference) });

						if (RecallImage.size() > 10)
						{
							while (RecallImage.size() > 10)
							{
								if (RecallImage.front().type == 1)
								{
									current_record_pointer = reference_record_pointer = max(1, reference_record_pointer - 1);
									practical_total_record_pointer++;
								}
								RecallImage.pop_front();
							}
						}

						LockExtremePointSm.unlock();
						LockStrokeBackImageSm.unlock();
					}
				}

				if (!RecallImage.empty() && !CompareImagesWithBuffer(&empty_drawpad, &RecallImage.back().img))
				{
					std::unique_lock<std::shared_mutex> LockStrokeBackImageSm(StrokeBackImageSm);
					std::unique_lock<std::shared_mutex> LockExtremePointSm(ExtremePointSm);

					RecallImage.back().type = 1;
					if (off_signal) SaveScreenShot(RecallImage.back().img, true);
					else
					{
						thread SaveScreenShot_thread(SaveScreenShot, RecallImage.back().img, true);
						SaveScreenShot_thread.detach();
					}

					extreme_point.clear();
					RecallImage.push_back({ empty_drawpad, extreme_point, 2, make_pair(0,0) });
					if (RecallImage.size() > 10)
					{
						while (RecallImage.size() > 10)
						{
							if (RecallImage.front().type == 1)
							{
								current_record_pointer = reference_record_pointer = max(1, reference_record_pointer - 1);
								practical_total_record_pointer++;
							}
							RecallImage.pop_front();
						}
					}

					LockExtremePointSm.unlock();
					LockStrokeBackImageSm.unlock();
				}
				else if (!RecallImage.empty())
				{
					std::unique_lock<std::shared_mutex> LockStrokeBackImageSm(StrokeBackImageSm);

					RecallImage.back().recond = make_pair(0, 0);

					LockStrokeBackImageSm.unlock();
				}

				if (off_signal) goto DrawpadDrawingEnd;

				if (RecallImage.size() >= 1 && ppt_info.totalSlides != -1)
				{
					if (!CompareImagesWithBuffer(&empty_drawpad, &drawpad))
					{
						ppt_img.is_save = true;
						ppt_img.is_saved[ppt_info.currentSlides] = true;

						ppt_img.image[ppt_info.currentSlides] = drawpad;
					}
				}
				recall_image_recond = 0, FirstDraw = true;
				current_record_pointer = reference_record_pointer;
				RecallImageManipulated = std::chrono::high_resolution_clock::time_point();

				int ppt_switch_count = 0;
				while (choose.select)
				{
					Sleep(50);

					if (ppt_info.currentSlides != ppt_info_stay.CurrentPage) ppt_info.currentSlides = ppt_info_stay.CurrentPage, ppt_switch_count++;
					ppt_info.totalSlides = ppt_info_stay.TotalPage;

					if (off_signal) goto DrawpadDrawingEnd;
				}

				{
					if (ppt_info.totalSlides != -1 && ppt_switch_count != 0 && ppt_img.is_saved[ppt_info.currentSlides])
					{
						drawpad = ppt_img.image[ppt_info.currentSlides];
					}
					else if (!reserve_drawpad) SetImageColor(drawpad, RGBA(0, 0, 0, 0), true);
					reserve_drawpad = false;

					SetImageColor(window_background, RGBA(0, 0, 0, 1), true);
					hiex::TransparentImage(&window_background, 0, 0, &drawpad);

					ulwi.hdcSrc = GetImageHDC(&window_background);
					UpdateLayeredWindowIndirect(drawpad_window, &ulwi);
				}
				TouchTemp.clear();
			}
			if (penetrate.select == true)
			{
				LONG nRet = ::GetWindowLong(drawpad_window, GWL_EXSTYLE);
				nRet |= WS_EX_TRANSPARENT;
				::SetWindowLong(drawpad_window, GWL_EXSTYLE, nRet);

				while (1)
				{
					if (ppt_show != NULL) SetWindowPos(drawpad_window, ppt_window, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
					else SetWindowPos(drawpad_window, floating_window, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);

					if (ppt_info.currentSlides != ppt_info_stay.CurrentPage || ppt_info.totalSlides != ppt_info_stay.TotalPage)
					{
						if (ppt_info.currentSlides != ppt_info_stay.CurrentPage && ppt_info.totalSlides == ppt_info_stay.TotalPage)
						{
							IMAGE empty_drawpad = CreateImageColor(drawpad.getwidth(), drawpad.getheight(), RGBA(0, 0, 0, 0), true);
							if (!RecallImage.empty() && !CompareImagesWithBuffer(&empty_drawpad, &RecallImage.back().img))
							{
								std::unique_lock<std::shared_mutex> LockStrokeBackImageSm(StrokeBackImageSm);
								std::unique_lock<std::shared_mutex> LockExtremePointSm(ExtremePointSm);

								RecallImage.back().type = 1;
								thread SaveScreenShot_thread(SaveScreenShot, RecallImage.back().img, true);
								SaveScreenShot_thread.detach();

								ppt_img.is_save = true;
								ppt_img.is_saved[ppt_info.currentSlides] = true;
								ppt_img.image[ppt_info.currentSlides] = RecallImage.back().img;

								extreme_point.clear();
								RecallImage.push_back({ empty_drawpad, extreme_point, 0, make_pair(0,0) });
								if (RecallImage.size() > 10)
								{
									while (RecallImage.size() > 10)
									{
										if (RecallImage.front().type == 1)
										{
											current_record_pointer = reference_record_pointer = max(1, reference_record_pointer - 1);
											practical_total_record_pointer++;
										}
										RecallImage.pop_front();
									}
								}

								LockExtremePointSm.unlock();
								LockStrokeBackImageSm.unlock();
							}

							if (ppt_img.is_saved[ppt_info_stay.CurrentPage] == true)
							{
								drawpad = ppt_img.image[ppt_info_stay.CurrentPage];
							}
							else
							{
								if (ppt_info_stay.TotalPage != -1) SetImageColor(drawpad, RGBA(0, 0, 0, 0), true);
							}
						}
						ppt_info.currentSlides = ppt_info_stay.CurrentPage;
						ppt_info.totalSlides = ppt_info_stay.TotalPage;

						{
							SetImageColor(window_background, RGBA(0, 0, 0, 1), true);
							hiex::TransparentImage(&window_background, 0, 0, &drawpad);

							ulwi.hdcSrc = GetImageHDC(&window_background);
							UpdateLayeredWindowIndirect(drawpad_window, &ulwi);
						}
					}

					Sleep(50);

					if (off_signal) goto DrawpadDrawingEnd;
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

			std::shared_lock<std::shared_mutex> lock1(StrokeImageListSm);
			bool start = !StrokeImageList.empty();
			lock1.unlock();
			if (start) break;

			if (off_signal)
			{
				if (!choose.select) goto ChooseEnd;
				goto DrawpadDrawingEnd;
			}
			if (ppt_info.currentSlides != ppt_info_stay.CurrentPage || ppt_info.totalSlides != ppt_info_stay.TotalPage)
			{
				if (ppt_info.currentSlides != ppt_info_stay.CurrentPage && ppt_info.totalSlides == ppt_info_stay.TotalPage)
				{
					IMAGE empty_drawpad = CreateImageColor(drawpad.getwidth(), drawpad.getheight(), RGBA(0, 0, 0, 0), true);
					if (!RecallImage.empty() && !CompareImagesWithBuffer(&empty_drawpad, &RecallImage.back().img))
					{
						std::unique_lock<std::shared_mutex> LockStrokeBackImageSm(StrokeBackImageSm);
						std::unique_lock<std::shared_mutex> LockExtremePointSm(ExtremePointSm);

						RecallImage.back().type = 1;
						thread SaveScreenShot_thread(SaveScreenShot, RecallImage.back().img, true);
						SaveScreenShot_thread.detach();

						ppt_img.is_save = true;
						ppt_img.is_saved[ppt_info.currentSlides] = true;
						ppt_img.image[ppt_info.currentSlides] = RecallImage.back().img;

						extreme_point.clear();
						RecallImage.push_back({ empty_drawpad, extreme_point, 0, make_pair(0,0) });
						if (RecallImage.size() > 10)
						{
							while (RecallImage.size() > 10)
							{
								if (RecallImage.front().type == 1)
								{
									current_record_pointer = reference_record_pointer = max(1, reference_record_pointer - 1);
									practical_total_record_pointer++;
								}
								RecallImage.pop_front();
							}
						}

						LockExtremePointSm.unlock();
						LockStrokeBackImageSm.unlock();
					}

					if (ppt_img.is_saved[ppt_info_stay.CurrentPage] == true)
					{
						drawpad = ppt_img.image[ppt_info_stay.CurrentPage];
					}
					else
					{
						if (ppt_info_stay.TotalPage != -1) SetImageColor(drawpad, RGBA(0, 0, 0, 0), true);
					}
				}
				else if (ppt_info.totalSlides != ppt_info_stay.TotalPage && ppt_info_stay.TotalPage == -1)
				{
					choose.select = true;

					brush.select = false;
					rubber.select = false;
					penetrate.select = false;
				}
				ppt_info.currentSlides = ppt_info_stay.CurrentPage;
				ppt_info.totalSlides = ppt_info_stay.TotalPage;

				{
					SetImageColor(window_background, RGBA(0, 0, 0, 1), true);
					hiex::TransparentImage(&window_background, 0, 0, &drawpad);

					ulwi.hdcSrc = GetImageHDC(&window_background);
					UpdateLayeredWindowIndirect(drawpad_window, &ulwi);
				}
			}

			Sleep(10);
		}
		reckon = std::chrono::high_resolution_clock::now();

		// 开始绘制（前期实现FPS显示）
		SetImageColor(window_background, RGBA(0, 0, 0, 1), true);
		std::shared_lock<std::shared_mutex> lock1(StrokeBackImageSm);
		hiex::TransparentImage(&window_background, 0, 0, &drawpad);
		lock1.unlock();

		std::shared_lock<std::shared_mutex> lock2(StrokeImageListSm);
		int siz = StrokeImageList.size();
		lock2.unlock();

		int t1 = 0, t2 = 0;
		for (int i = 0; i < siz; i++)
		{
			std::shared_lock<std::shared_mutex> lock1(StrokeImageListSm);
			int pid = StrokeImageList[i];
			lock1.unlock();

			std::chrono::high_resolution_clock::time_point s1 = std::chrono::high_resolution_clock::now();
			std::shared_lock<std::shared_mutex> lock2(StrokeImageSm[pid]);
			t1 += (int)std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::high_resolution_clock::now() - s1).count();

			s1 = std::chrono::high_resolution_clock::now();
			int info = StrokeImage[pid].second;
			hiex::TransparentImage(&window_background, 0, 0, StrokeImage[pid].first, (info / 10));
			t2 += (int)std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::high_resolution_clock::now() - s1).count();

			lock2.unlock();
			if ((info % 10) % 2 == 1)
			{
				if ((info % 10) == 1)
				{
					std::shared_lock<std::shared_mutex> lock1(StrokeBackImageSm);
					std::shared_lock<std::shared_mutex> lock2(StrokeImageSm[pid]);
					hiex::TransparentImage(&drawpad, 0, 0, StrokeImage[pid].first, (info / 10));
					lock2.unlock();
					lock1.unlock();

					std::unique_lock<std::shared_mutex> lock3(StrokeImageSm[pid]);
					delete StrokeImage[pid].first;
					StrokeImage[pid].first = nullptr;

					StrokeImage.erase(pid);
					lock3.unlock();
					StrokeImageSm.erase(pid);

					std::unique_lock<std::shared_mutex> lock4(StrokeImageListSm);
					StrokeImageList.erase(StrokeImageList.begin() + i);
					lock4.unlock();
					i--;

					std::shared_lock<std::shared_mutex> LockRecallImageManipulatedSm(RecallImageManipulatedSm);
					bool free = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::high_resolution_clock::now() - RecallImageManipulated).count() >= 1000;
					LockRecallImageManipulatedSm.unlock();
					if (free)
					{
						bool save_recond = false;
						if (recall_image_reference > recall_image_recond) recall_image_recond++;
						else recall_image_recond = recall_image_reference = recall_image_reference + 1, save_recond = true;

						if (recall_image_recond % 10 == 0 && save_recond && recall_image_recond >= 20)
						{
							thread SaveScreenShot_thread(SaveScreenShot, RecallImage[0].img, false);
							SaveScreenShot_thread.detach();
						}

						std::unique_lock<std::shared_mutex> LockStrokeBackImageSm(StrokeBackImageSm);
						std::unique_lock<std::shared_mutex> LockExtremePointSm(ExtremePointSm);

						RecallImage.push_back({ drawpad, extreme_point, 0, make_pair(recall_image_recond,recall_image_reference) });

						if (RecallImage.size() > 10)
						{
							while (RecallImage.size() > 10)
							{
								if (RecallImage.front().type == 1)
								{
									current_record_pointer = reference_record_pointer = max(1, reference_record_pointer - 1);
									practical_total_record_pointer++;
								}
								RecallImage.pop_front();
							}
						}

						LockExtremePointSm.unlock();
						LockStrokeBackImageSm.unlock();

						std::unique_lock<std::shared_mutex> LockRecallImageManipulatedSm(RecallImageManipulatedSm);
						RecallImageManipulated = std::chrono::high_resolution_clock::now();
						LockRecallImageManipulatedSm.unlock();
					}
				}
				else if ((info % 10) == 3)
				{
					std::unique_lock<std::shared_mutex> lock2(StrokeImageSm[pid]);
					delete StrokeImage[pid].first;
					StrokeImage[pid].first = nullptr;

					StrokeImage.erase(pid);
					lock2.unlock();
					StrokeImageSm.erase(pid);

					std::unique_lock<std::shared_mutex> lock3(StrokeImageListSm);
					StrokeImageList.erase(StrokeImageList.begin() + i);
					lock3.unlock();
					i--;

					std::shared_lock<std::shared_mutex> LockRecallImageManipulatedSm(RecallImageManipulatedSm);
					bool free = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::high_resolution_clock::now() - RecallImageManipulated).count() >= 1000;
					LockRecallImageManipulatedSm.unlock();
					if (free)
					{
						bool save_recond = false;
						if (recall_image_reference > recall_image_recond) recall_image_recond++;
						else recall_image_recond = recall_image_reference = recall_image_reference + 1, save_recond = true;

						bool save = false;
						if (!RecallImage.empty())
						{
							std::shared_lock<std::shared_mutex> LockStrokeBackImageSm(StrokeBackImageSm);
							save = !CompareImagesWithBuffer(&drawpad, &RecallImage.back().img);
							LockStrokeBackImageSm.unlock();
						}
						else
						{
							IMAGE empty_drawpad = CreateImageColor(drawpad.getwidth(), drawpad.getheight(), RGBA(0, 0, 0, 0), true);
							save = !CompareImagesWithBuffer(&drawpad, &empty_drawpad);
						}
						if (save)
						{
							if (recall_image_recond % 10 == 0 && save_recond && recall_image_recond >= 20)
							{
								thread SaveScreenShot_thread(SaveScreenShot, RecallImage[0].img, false);
								SaveScreenShot_thread.detach();
							}

							std::unique_lock<std::shared_mutex> LockStrokeBackImageSm(StrokeBackImageSm);
							std::unique_lock<std::shared_mutex> LockExtremePointSm(ExtremePointSm);

							RecallImage.push_back({ drawpad, extreme_point, 0, make_pair(recall_image_recond,recall_image_reference) });
							if (RecallImage.size() > 10)
							{
								while (RecallImage.size() > 10)
								{
									if (RecallImage.front().type == 1)
									{
										current_record_pointer = reference_record_pointer = max(1, reference_record_pointer - 1);
										practical_total_record_pointer++;
									}
									RecallImage.pop_front();
								}
							}

							LockExtremePointSm.unlock();
							LockStrokeBackImageSm.unlock();

							std::unique_lock<std::shared_mutex> LockRecallImageManipulatedSm(RecallImageManipulatedSm);
							RecallImageManipulated = std::chrono::high_resolution_clock::now();
							LockRecallImageManipulatedSm.unlock();
						}
					}
				}
			}

			std::shared_lock<std::shared_mutex> lock3(StrokeImageListSm);
			siz = StrokeImageList.size();
			lock3.unlock();
		}

		//帧率锁
		{
			clock_t tNow = clock();
			if (tRecord)
			{
				int delay = 1000 / 72 - (tNow - tRecord);
				if (delay > 0) std::this_thread::sleep_for(std::chrono::milliseconds(delay));
			}
		}

		ulwi.hdcSrc = GetImageHDC(&window_background);
		UpdateLayeredWindowIndirect(drawpad_window, &ulwi);

		tRecord = clock();
	}

DrawpadDrawingEnd:
	thread_status[L"DrawpadDrawing"] = false;
	return;
}
int drawpad_main()
{
	thread_status[L"drawpad_main"] = true;

	//画笔初始化
	{
		brush.width = 3;
		brush.color = brush.primary_colour = RGBA(50, 30, 181, 255);
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
		LoadDrawpad();
	}

	magnificationWindowReady++;
	//LOG(INFO) << "成功初始化画笔窗口绘制模块";

	thread DrawpadDrawing_thread(DrawpadDrawing);
	DrawpadDrawing_thread.detach();
	{
		SetImageColor(alpha_drawpad, RGBA(0, 0, 0, 0), true);

		SetWindowPos(drawpad_window, floating_window, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
		LONG style = GetWindowLong(drawpad_window, GWL_EXSTYLE);
		style |= WS_EX_NOACTIVATE;
		SetWindowLong(drawpad_window, GWL_EXSTYLE, style);

		//启动绘图库程序
		hiex::Gdiplus_Try_Starup();

		//LOG(INFO) << "成功初始化画笔窗口绘制模块配置内容，并进入主循环";
		while (!off_signal)
		{
			if (choose.select == true || penetrate.select == true)
			{
				Sleep(100);
				continue;
			}

			while (1)
			{
				std::shared_lock<std::shared_mutex> lock1(PointTempSm);
				bool start = !TouchTemp.empty();
				lock1.unlock();
				//开始绘图
				if (start)
				{
					if (rubber.select != true && (brush.mode == 1 || brush.mode == 2) && int(state) == 1) target_status = 0;
					if (current_record_pointer != reference_record_pointer)
					{
						current_record_pointer = reference_record_pointer = max(1, reference_record_pointer - 1);
						practical_total_record_pointer++;

						shared_lock<std::shared_mutex> LockStrokeBackImageSm(StrokeBackImageSm);
						shared_lock<std::shared_mutex> LockExtremePointSm(ExtremePointSm);
						RecallImage.push_back({ drawpad, extreme_point, 0 });
						LockExtremePointSm.unlock();
						LockStrokeBackImageSm.unlock();
					}
					if (FirstDraw) RecallImageManipulated = std::chrono::high_resolution_clock::now();
					FirstDraw = false;

					std::shared_lock<std::shared_mutex> lock1(PointTempSm);
					LONG pid = TouchTemp.front().pid;
					POINT pt = TouchTemp.front().pt;
					lock1.unlock();

					std::unique_lock<std::shared_mutex> lock2(PointTempSm);
					TouchTemp.pop_front();
					lock2.unlock();

					//hiex::PreSetWindowShowState(SW_HIDE);
					//HWND draw_window = initgraph(GetSystemMetrics(SM_CXSCREEN), GetSystemMetrics(SM_CYSCREEN));

					thread MultiFingerDrawing_thread(MultiFingerDrawing, pid, pt);
					MultiFingerDrawing_thread.detach();
				}
				else break;
			}

			Sleep(10);
		}
	}

	ShowWindow(drawpad_window, SW_HIDE);

	for (int i = 1; i <= 10; i++)
	{
		if (!thread_status[L"DrawpadDrawing"]) break;
		Sleep(500);
	}
	thread_status[L"drawpad_main"] = false;
	return 0;
}