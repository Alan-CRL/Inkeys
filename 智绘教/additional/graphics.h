/******************************************************
 * EasyX Library for C++ (Ver:20240601)
 * https://easyx.cn
 *
 * graphics.h
 *		Based on easyx.h and retaining several old APIs.
 *		The functions and constants declared in this file are only for compatibility and are not recommended.
 ******************************************************/

#pragma once

#include <easyx.h>



// Old Window Properties
#define SHOWCONSOLE		1		// Maintain the console window when creating a graphics window
#define NOCLOSE			2		// Disable the close button
#define NOMINIMIZE		4		// Disable the minimize button
#define EW_SHOWCONSOLE	1		// Maintain the console window when creating a graphics window
#define EW_NOCLOSE		2		// Disable the close button
#define EW_NOMINIMIZE	4		// Disable the minimize button
#define EW_DBLCLKS		8		// Support double-click events



// Old fill styles
#define	NULL_FILL			BS_NULL
#define	EMPTY_FILL			BS_NULL
#define	SOLID_FILL			BS_SOLID
// Old normal fill style
#define	BDIAGONAL_FILL		BS_HATCHED, HS_BDIAGONAL					// Fill with ///.
#define CROSS_FILL			BS_HATCHED, HS_CROSS						// Fill with +++.
#define DIAGCROSS_FILL		BS_HATCHED, HS_DIAGCROSS					// Fill with xxx (heavy cross hatch fill).
#define DOT_FILL			(BYTE*)"\x80\x00\x08\x00\x80\x00\x08\x00"	// Fill with xxx.
#define FDIAGONAL_FILL		BS_HATCHED, HS_FDIAGONAL					// Fill with \\\.
#define HORIZONTAL_FILL		BS_HATCHED, HS_HORIZONTAL					// Fill with ===.
#define VERTICAL_FILL		BS_HATCHED, HS_VERTICAL						// Fill with |||.
// Old dense fill style
#define BDIAGONAL2_FILL		(BYTE*)"\x44\x88\x11\x22\x44\x88\x11\x22"
#define CROSS2_FILL			(BYTE*)"\xff\x11\x11\x11\xff\x11\x11\x11"
#define DIAGCROSS2_FILL		(BYTE*)"\x55\x88\x55\x22\x55\x88\x55\x22"
#define DOT2_FILL			(BYTE*)"\x88\x00\x22\x00\x88\x00\x22\x00"
#define FDIAGONAL2_FILL		(BYTE*)"\x22\x11\x88\x44\x22\x11\x88\x44"
#define HORIZONTAL2_FILL	(BYTE*)"\x00\x00\xff\x00\x00\x00\xff\x00"
#define VERTICAL2_FILL		(BYTE*)"\x11\x11\x11\x11\x11\x11\x11\x11"
// Old heavy line fill style
#define BDIAGONAL3_FILL		(BYTE*)"\xe0\xc1\x83\x07\x0e\x1c\x38\x70"
#define CROSS3_FILL			(BYTE*)"\x30\x30\x30\x30\x30\x30\xff\xff"
#define DIAGCROSS3_FILL		(BYTE*)"\xc7\x83\xc7\xee\x7c\x38\x7c\xee"
#define DOT3_FILL			(BYTE*)"\xc0\xc0\x0c\x0c\xc0\xc0\x0c\x0c"
#define FDIAGONAL3_FILL		(BYTE*)"\x07\x83\xc1\xe0\x70\x38\x1c\x0e"
#define HORIZONTAL3_FILL	(BYTE*)"\xff\xff\x00\x00\xff\xff\x00\x00"
#define VERTICAL3_FILL		(BYTE*)"\x33\x33\x33\x33\x33\x33\x33\x33"
// Old other fill style
#define INTERLEAVE_FILL		(BYTE*)"\xcc\x33\xcc\x33\xcc\x33\xcc\x33"



//
#if _MSC_VER > 1200 && _MSC_VER < 1900
	#define _EASYX_DEPRECATE					__declspec(deprecated("This function is deprecated."))
	#define _EASYX_DEPRECATE_WITHNEW(_NewFunc)	__declspec(deprecated("This function is deprecated. Instead, use this new function: " #_NewFunc ". See https://docs.easyx.cn/" #_NewFunc " for details."))
	#define _EASYX_DEPRECATE_OVERLOAD(_Func)	__declspec(deprecated("This overload is deprecated. See https://docs.easyx.cn/" #_Func " for details."))
#else
	#define _EASYX_DEPRECATE
	#define _EASYX_DEPRECATE_WITHNEW(_NewFunc)
	#define _EASYX_DEPRECATE_OVERLOAD(_Func)
#endif

// Old text related functions
//		nHeight: The height of the text
//		nWidth: The average width of the character. If 0, the scale is adaptive.
//		lpszFace: The font name
//		nEscapement: The writing angle of the string, 0.1 degrees, defaults to 0.
//		nOrientation: The writing angle of each character, 0.1 degrees, defaults to 0.
//		nWeight: The stroke weight of the character
//		bItalic: Specify whether the font is italic
//		bUnderline: Specify whether the font is underlined
//		bStrikeOut: Specify whether the font has a strikeout
//		fbCharSet: Specifies the character set
//		fbOutPrecision: Specifies the output accuracy of the text
//		fbClipPrecision: Specifies the clip accuracy of the text
//		fbQuality: Specifies the output quality of the text
//		fbPitchAndFamily: Specifies a font family that describes a font in a general way
_EASYX_DEPRECATE_WITHNEW(settextstyle) void setfont(int nHeight, int nWidth, LPCTSTR lpszFace);
_EASYX_DEPRECATE_WITHNEW(settextstyle) void setfont(int nHeight, int nWidth, LPCTSTR lpszFace, int nEscapement, int nOrientation, int nWeight, bool bItalic, bool bUnderline, bool bStrikeOut);
_EASYX_DEPRECATE_WITHNEW(settextstyle) void setfont(int nHeight, int nWidth, LPCTSTR lpszFace, int nEscapement, int nOrientation, int nWeight, bool bItalic, bool bUnderline, bool bStrikeOut, BYTE fbCharSet, BYTE fbOutPrecision, BYTE fbClipPrecision, BYTE fbQuality, BYTE fbPitchAndFamily);
_EASYX_DEPRECATE_WITHNEW(settextstyle) void setfont(const LOGFONT *font);	// Set current text style
_EASYX_DEPRECATE_WITHNEW(gettextstyle) void getfont(LOGFONT *font);			// Get current text style

// Old drawing related functions
void bar(int left, int top, int right, int bottom);		// Draw a filled rectangle without a border
void bar3d(int left, int top, int right, int bottom, int depth, bool topflag);	// Draw a filled 3D rectangle with a border

void drawpoly(int numpoints, const int *polypoints);	// Draw a polygon without filling
void fillpoly(int numpoints, const int *polypoints);	// Draw a filled polygon with a border

int getmaxx();					// Get the maximum x-coordinate in the physical coordinates of the graphics window
int getmaxy();					// Get the maximum y-coordinate in the physical coordinates of the graphics window

COLORREF getcolor();			// Get current foreground color
void setcolor(COLORREF color);	// Set current foreground color

void setwritemode(int mode);	// Set binary raster operation mode

// Old current location related functions
_EASYX_DEPRECATE	int	getx();								// Get current x coordinates
_EASYX_DEPRECATE	int	gety();								// Get current y coordinates
_EASYX_DEPRECATE	void moveto(int x, int y);				// Move current location
_EASYX_DEPRECATE	void moverel(int dx, int dy);			// Move current location
_EASYX_DEPRECATE	void lineto(int x, int y);				// Draw a line
_EASYX_DEPRECATE	void linerel(int dx, int dy);			// Draw a line
_EASYX_DEPRECATE	void outtext(LPCTSTR str);				// Output a string at current location
_EASYX_DEPRECATE	void outtext(TCHAR c);					// Output a char at current location

// Old mouse related functions
// Mouse message
//		WM_MOUSEMOVE		Mouse moves
//		WM_MOUSEWHEEL		Mouse wheel is rotated
//		WM_LBUTTONDOWN		Left mouse button is pressed
//		WM_LBUTTONUP		Left mouse button is released
//		WM_LBUTTONDBLCLK	Left mouse button is double-clicked
//		WM_MBUTTONDOWN		Middle mouse button is pressed
//		WM_MBUTTONUP		Middle mouse button is released
//		WM_MBUTTONDBLCLK	Middle mouse button is double-clicked
//		WM_RBUTTONDOWN		Right mouse button is pressed
//		WM_RBUTTONUP		Right mouse button is released
//		WM_RBUTTONDBLCLK	Right mouse button is double-clicked
struct MOUSEMSG
{
	UINT uMsg;				// Mouse message
	bool mkCtrl		:1;		// Indicates whether the CTRL key is pressed
	bool mkShift	:1;		// Indicates whether the SHIFT key is pressed
	bool mkLButton	:1;		// Indicates whether the left mouse button is pressed
	bool mkMButton	:1;		// Indicates whether the middle mouse button is pressed
	bool mkRButton	:1;		// Indicates whether the right mouse button is pressed
	short x;				// The x-coordinate of the cursor
	short y;				// The y-coordinate of the cursor
	short wheel;			// The distance the wheel is rotated, expressed in multiples or divisions of 120
};
_EASYX_DEPRECATE							bool MouseHit();			// Indicates whether there are mouse messages
_EASYX_DEPRECATE_WITHNEW(getmessage)		MOUSEMSG GetMouseMsg();		// Get a mouse message. if mouse message queue is empty, wait.
_EASYX_DEPRECATE_WITHNEW(peekmessage)		bool PeekMouseMsg(MOUSEMSG *pMsg, bool bRemoveMsg = true);	// Get a mouse message and return immediately
_EASYX_DEPRECATE_WITHNEW(flushmessage)		void FlushMouseMsgBuffer();	// Empty the mouse message buffer

typedef ExMessage EASYXMSG;	// Old message structure

// Old message category
#define EM_MOUSE	1
#define EM_KEY		2
#define EM_CHAR		4
#define EM_WINDOW	8