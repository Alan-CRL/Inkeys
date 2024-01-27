#pragma once
#include "IdtMain.h"

#include "IdtConfiguration.h"
#include "IdtDisplayManagement.h"
#include "IdtDraw.h"
#include "IdtMagnification.h"
#include "IdtText.h"
#include "IdtTime.h"

void removeEmptyFolders(std::wstring path);
void removeUnknownFiles(std::wstring path, std::deque<std::wstring> knownFiles);
deque<wstring> getPrevTwoDays(const std::wstring& date, int day);

extern int current_record_pointer, total_record_pointer;
extern int reference_record_pointer, practical_total_record_pointer;
extern Json::Value record_value;

//载入记录
void LoadDrawpad();
//保存图像到指定目录
void SaveScreenShot(IMAGE img, bool record_pointer_add);