/*
 * @file		IdtPlug-in.h
 * @brief		IDT plugin linkage | 智绘教插件联动
 * @note		PPT linkage components and other plugins | PPT联动组件和其他插件等
 *
 * @author		AlanCRL
 * @qq			2685549821
 * @email		alan-crl@foxmail.com
*/

#pragma once
#include "IdtMain.h"
#include "Inkeys/Graphics/Surface.hpp"

#include "SuperTop/IdtToken.h"

// All function and variable descriptions should be in the corresponding cpp file.
// 所有的函数和变量说明应该在对应的 cpp 文件中。

// --------------------------------------------------
// PPT 联动插件

// -------------------------
// ppt 信息

struct PptImgStruct
{
	bool IsSave;
	map<int, bool> IsSaved;
	map<int, Inkeys::Graphics::DibSurface> Image;
};
extern PptImgStruct PptImg;
struct PptInfoStateStruct
{
	int CurrentPage, TotalPage;
};
extern PptInfoStateStruct PptInfoStateBuffer;
extern PptInfoStateStruct PptInfoState;

extern wstring pptComVersion;

// -------------------------
// Ppt 主项

void NextPptSlides(int check);
void PreviousPptSlides();
void EndPptShow();
void FocusPptShow();

void PPTLinkageMain();

bool IsPowerPointRunAsAdminSet();

// --------------------------------------------------
// 其他插件

void StartDesktopDrawpadBlocker();

class ShortcutAssistantClass
{
public:
	void SetShortcut();
	bool IsShortcutPointingToDirectory(const std::wstring& shortcutPath, const std::wstring& targetDirectory);
	bool CreateShortcut(const std::wstring& shortcutPath, const std::wstring& targetExePath);
};
extern ShortcutAssistantClass shortcutAssistant;
