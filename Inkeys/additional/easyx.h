/******************************************************
 * EasyX Library for C++ (Ver:20240601)
 * https://easyx.cn
 *
 * EasyX.h
 *		Provides the latest API.
 ******************************************************/

#pragma once

#ifndef WINVER
#define WINVER 0x0400			// Specifies that the minimum required platform is Windows 95 and Windows NT 4.0.
#endif

#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0500		// Specifies that the minimum required platform is Windows 2000
#endif

#ifndef _WIN32_WINDOWS
#define _WIN32_WINDOWS 0x0410	// Specifies that the minimum required platform is Windows 98
#endif

#ifdef UNICODE
	#pragma comment(lib,"EasyXw.lib")
#else
	#pragma comment(lib,"EasyXa.lib")
#endif


#ifndef __cplusplus
#error EasyX is only for C++
#endif

#include <windows.h>
#include <tchar.h>

// EasyX Window Properties
#define EX_SHOWCONSOLE		1		// Maintain the console window when creating a graphics window
#define EX_NOCLOSE			2		// Disable the close button
#define EX_NOMINIMIZE		4		// Disable the minimize button
#define EX_DBLCLKS			8		// Support double-click events


// Color constant
#define	BLACK			0
#define	BLUE			0xAA0000
#define	GREEN			0x00AA00
#define	CYAN			0xAAAA00
#define	RED				0x0000AA
#define	MAGENTA			0xAA00AA
#define	BROWN			0x0055AA
#define	LIGHTGRAY		0xAAAAAA
#define	DARKGRAY		0x555555
#define	LIGHTBLUE		0xFF5555
#define	LIGHTGREEN		0x55FF55
#define	LIGHTCYAN		0xFFFF55
#define	LIGHTRED		0x5555FF
#define	LIGHTMAGENTA	0xFF55FF
#define	YELLOW			0x55FFFF
#define	WHITE			0xFFFFFF

// Color conversion macro
#define BGR(color)	( (((color) & 0xFF) << 16) | ((color) & 0xFF00FF00) | (((color) & 0xFF0000) >> 16) )


class IMAGE;

// Line style class
class LINESTYLE
{
public:
	LINESTYLE();
	LINESTYLE(const LINESTYLE &style);
	LINESTYLE& operator = (const LINESTYLE &style);
	virtual ~LINESTYLE();

	DWORD	style;
	DWORD	thickness;
	DWORD	*puserstyle;
	DWORD	userstylecount;
};

// Fill style class
class FILLSTYLE
{
public:
	FILLSTYLE();
	FILLSTYLE(const FILLSTYLE &style);
	FILLSTYLE& operator = (const FILLSTYLE &style);
	virtual ~FILLSTYLE();

	int			style;				// Fill style
	long		hatch;				// Hatch pattern
	IMAGE*		ppattern;			// Fill image
};

// Image class
class IMAGE
{
public:
	int getwidth() const;			// Get the width of the image
	int getheight() const;			// Get the height of the image

private:
	int			width, height;		// Width and height of the image
	HBITMAP		m_hBmp;
	HDC			m_hMemDC;
	float		m_data[6];
	COLORREF	m_LineColor;		// Current line color
	COLORREF	m_FillColor;		// Current fill color
	COLORREF	m_TextColor;		// Current text color
	COLORREF	m_BkColor;			// Current background color
	DWORD*		m_pBuffer;			// Memory buffer of the image

	LINESTYLE	m_LineStyle;		// Current line style
	FILLSTYLE	m_FillStyle;		// Current fill style

	virtual void SetDefault();		// Set the graphics environment as default

public:
	IMAGE(int _width = 0, int _height = 0);
	IMAGE(const IMAGE &img);
	IMAGE& operator = (const IMAGE &img);
	virtual ~IMAGE();
	virtual void Resize(int _width, int _height);			// Resize image
};



// Graphics window related functions

HWND initgraph(int width, int height, int flag = 0);		// Create graphics window
void closegraph();											// Close graphics window


// Graphics environment related functions

void cleardevice();											// Clear device
void setcliprgn(HRGN hrgn);									// Set clip region
void clearcliprgn();										// Clear clip region

void getlinestyle(LINESTYLE* pstyle);						// Get line style
void setlinestyle(const LINESTYLE* pstyle);					// Set line style
void setlinestyle(int style, int thickness = 1, const DWORD *puserstyle = NULL, DWORD userstylecount = 0);	// Set line style
void getfillstyle(FILLSTYLE* pstyle);						// Get fill style
void setfillstyle(const FILLSTYLE* pstyle);					// Set fill style
void setfillstyle(int style, long hatch = NULL, IMAGE* ppattern = NULL);		// Set fill style
void setfillstyle(BYTE* ppattern8x8);						// Set fill style

void setorigin(int x, int y);								// Set coordinate origin
void getaspectratio(float *pxasp, float *pyasp);			// Get aspect ratio
void setaspectratio(float xasp, float yasp);				// Set aspect ratio

int  getrop2();						// Get binary raster operation mode
void setrop2(int mode);				// Set binary raster operation mode
int  getpolyfillmode();				// Get polygon fill mode
void setpolyfillmode(int mode);		// Set polygon fill mode

void graphdefaults();				// Reset the graphics environment as default

COLORREF getlinecolor();			// Get line color
void setlinecolor(COLORREF color);	// Set line color
COLORREF gettextcolor();			// Get text color
void settextcolor(COLORREF color);	// Set text color
COLORREF getfillcolor();			// Get fill color
void setfillcolor(COLORREF color);	// Set fill color
COLORREF getbkcolor();				// Get background color
void setbkcolor(COLORREF color);	// Set background color
int  getbkmode();					// Get background mode
void setbkmode(int mode);			// Set background mode

// Color model transformation related functions
COLORREF RGBtoGRAY(COLORREF rgb);
void RGBtoHSL(COLORREF rgb, float *H, float *S, float *L);
void RGBtoHSV(COLORREF rgb, float *H, float *S, float *V);
COLORREF HSLtoRGB(float H, float S, float L);
COLORREF HSVtoRGB(float H, float S, float V);


// Drawing related functions

COLORREF getpixel(int x, int y);				// Get pixel color
void putpixel(int x, int y, COLORREF color);	// Set pixel color

void line(int x1, int y1, int x2, int y2);		// Draw a line

void rectangle	   (int left, int top, int right, int bottom);	// Draw a rectangle without filling
void fillrectangle (int left, int top, int right, int bottom);	// Draw a filled rectangle with a border
void solidrectangle(int left, int top, int right, int bottom);	// Draw a filled rectangle without a border
void clearrectangle(int left, int top, int right, int bottom);	// Clear a rectangular region

void circle		(int x, int y, int radius);		// Draw a circle without filling
void fillcircle (int x, int y, int radius);		// Draw a filled circle with a border
void solidcircle(int x, int y, int radius);		// Draw a filled circle without a border
void clearcircle(int x, int y, int radius);		// Clear a circular region

void ellipse	 (int left, int top, int right, int bottom);	// Draw an ellipse without filling
void fillellipse (int left, int top, int right, int bottom);	// Draw a filled ellipse with a border
void solidellipse(int left, int top, int right, int bottom);	// Draw a filled ellipse without a border
void clearellipse(int left, int top, int right, int bottom);	// Clear an elliptical region

void roundrect	   (int left, int top, int right, int bottom, int ellipsewidth, int ellipseheight);		// Draw a rounded rectangle without filling
void fillroundrect (int left, int top, int right, int bottom, int ellipsewidth, int ellipseheight);		// Draw a filled rounded rectangle with a border
void solidroundrect(int left, int top, int right, int bottom, int ellipsewidth, int ellipseheight);		// Draw a filled rounded rectangle without a border
void clearroundrect(int left, int top, int right, int bottom, int ellipsewidth, int ellipseheight);		// Clear a rounded rectangular region

void arc	 (int left, int top, int right, int bottom, double stangle, double endangle);	// Draw an arc
void pie	 (int left, int top, int right, int bottom, double stangle, double endangle);	// Draw a sector without filling
void fillpie (int left, int top, int right, int bottom, double stangle, double endangle);	// Draw a filled sector with a border
void solidpie(int left, int top, int right, int bottom, double stangle, double endangle);	// Draw a filled sector without a border
void clearpie(int left, int top, int right, int bottom, double stangle, double endangle);	// Clear a rounded rectangular region

void polyline	 (const POINT *points, int num);								// Draw multiple consecutive lines
void polygon	 (const POINT *points, int num);								// Draw a polygon without filling
void fillpolygon (const POINT *points, int num);								// Draw a filled polygon with a border
void solidpolygon(const POINT *points, int num);								// Draw a filled polygon without a border
void clearpolygon(const POINT *points, int num);								// Clear a polygon region

void polybezier(const POINT *points, int num);									// Draw three square Bezier curves
void floodfill(int x, int y, COLORREF color, int filltype = FLOODFILLBORDER);	// Fill the area



// Text related functions
void outtextxy(int x, int y, LPCTSTR str);				// Output a string at the specified location
void outtextxy(int x, int y, TCHAR c);					// Output a char at the specified location
int textwidth(LPCTSTR str);								// Get the width of a string
int textwidth(TCHAR c);									// Get the width of a char
int textheight(LPCTSTR str);							// Get the height of a string
int textheight(TCHAR c);								// Get the height of a char
int drawtext(LPCTSTR str, RECT* pRect, UINT uFormat);	// Output a string in the specified format within the specified area.
int drawtext(TCHAR c, RECT* pRect, UINT uFormat);		// Output a char in the specified format within the specified area.

// Set current text style.
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
void settextstyle(int nHeight, int nWidth, LPCTSTR lpszFace);
void settextstyle(int nHeight, int nWidth, LPCTSTR lpszFace, int nEscapement, int nOrientation, int nWeight, bool bItalic, bool bUnderline, bool bStrikeOut);
void settextstyle(int nHeight, int nWidth, LPCTSTR lpszFace, int nEscapement, int nOrientation, int nWeight, bool bItalic, bool bUnderline, bool bStrikeOut, BYTE fbCharSet, BYTE fbOutPrecision, BYTE fbClipPrecision, BYTE fbQuality, BYTE fbPitchAndFamily);
void settextstyle(const LOGFONT *font);	// Set current text style
void gettextstyle(LOGFONT *font);		// Get current text style



// Image related functions
void loadimage(IMAGE *pDstImg, LPCTSTR pImgFile, int nWidth = 0, int nHeight = 0, bool bResize = false);					// Load image from a file (bmp/gif/jpg/png/tif/emf/wmf/ico)
void loadimage(IMAGE *pDstImg, LPCTSTR pResType, LPCTSTR pResName, int nWidth = 0, int nHeight = 0, bool bResize = false);	// Load image from resources (bmp/gif/jpg/png/tif/emf/wmf/ico)
void saveimage(LPCTSTR pImgFile, IMAGE* pImg = NULL);																		// Save image to a file (bmp/gif/jpg/png/tif)
void getimage(IMAGE *pDstImg, int srcX, int srcY, int srcWidth, int srcHeight);												// Get image from device
void putimage(int dstX, int dstY, const IMAGE *pSrcImg, DWORD dwRop = SRCCOPY);												// Put image to device
void putimage(int dstX, int dstY, int dstWidth, int dstHeight, const IMAGE *pSrcImg, int srcX, int srcY, DWORD dwRop = SRCCOPY);		// Put image to device
void rotateimage(IMAGE *dstimg, IMAGE *srcimg, double radian, COLORREF bkcolor = BLACK, bool autosize = false, bool highquality = true);// Rotate image
void Resize(IMAGE* pImg, int width, int height);	// Resize the device
DWORD* GetImageBuffer(IMAGE* pImg = NULL);			// Get the display buffer of the graphics device
IMAGE* GetWorkingImage();							// Get current graphics device
void SetWorkingImage(IMAGE* pImg = NULL);			// Set current graphics device
HDC GetImageHDC(IMAGE* pImg = NULL);				// Get the graphics device handle


// Other functions

int	getwidth();			// Get the width of current graphics device
int	getheight();		// Get the height of current graphics device

void BeginBatchDraw();	// Begin batch drawing mode
void FlushBatchDraw();	// Refreshes the undisplayed drawing
void FlushBatchDraw(int left, int top, int right, int bottom);	// Refreshes the undisplayed drawing
void EndBatchDraw();	// End batch drawing mode and refreshes the undisplayed drawing
void EndBatchDraw(int left, int top, int right, int bottom);	// End batch drawing mode and refreshes the undisplayed drawing

HWND GetHWnd();								// Get the handle of the graphics window
const TCHAR* GetEasyXVer();						// Get version of EasyX library

// Get user input as a dialog box
bool InputBox(LPTSTR pString, int nMaxCount, LPCTSTR pPrompt = NULL, LPCTSTR pTitle = NULL, LPCTSTR pDefault = NULL, int width = 0, int height = 0, bool bOnlyOK = true);



// Message
//
//	Category	Type				Description
//
//	EX_MOUSE	WM_MOUSEMOVE		Mouse moves
//				WM_MOUSEWHEEL		Mouse wheel is rotated
//				WM_LBUTTONDOWN		Left mouse button is pressed
//				WM_LBUTTONUP		Left mouse button is released
//				WM_LBUTTONDBLCLK	Left mouse button is double-clicked
//				WM_MBUTTONDOWN		Middle mouse button is pressed
//				WM_MBUTTONUP		Middle mouse button is released
//				WM_MBUTTONDBLCLK	Middle mouse button is double-clicked
//				WM_RBUTTONDOWN		Right mouse button is pressed
//				WM_RBUTTONUP		Right mouse button is released
//				WM_RBUTTONDBLCLK	Right mouse button is double-clicked
//
//	EX_KEY		WM_KEYDOWN			A key is pressed
//				WM_KEYUP			A key is released
//
//	EX_CHAR		WM_CHAR
//
//	EX_WINDOW	WM_ACTIVATE			The window is activated or deactivated
//				WM_MOVE				The window has been moved
//				WM_SIZE				The size of window has changed

// Message Category
#define EX_MOUSE	1
#define EX_KEY		2
#define EX_CHAR		4
#define EX_WINDOW	8

// Message Structure
struct ExMessage
{
	USHORT message;					// The message identifier
	union
	{
		// Data of the mouse message
		struct
		{
			bool ctrl		:1;		// Indicates whether the CTRL key is pressed
			bool shift		:1;		// Indicates whether the SHIFT key is pressed
			bool lbutton	:1;		// Indicates whether the left mouse button is pressed
			bool mbutton	:1;		// Indicates whether the middle mouse button is pressed
			bool rbutton	:1;		// Indicates whether the right mouse button is pressed
			short x;				// The x-coordinate of the cursor
			short y;				// The y-coordinate of the cursor
			short wheel;			// The distance the wheel is rotated, expressed in multiples or divisions of 120
		};

		// Data of the key message
		struct
		{
			BYTE vkcode;			// The virtual-key code of the key
			BYTE scancode;			// The scan code of the key. The value depends on the OEM
			bool extended	:1;		// Indicates whether the key is an extended key, such as a function key or a key on the numeric keypad. The value is true if the key is an extended key; otherwise, it is false.
			bool prevdown	:1;		// Indicates whether the key is previously up or down
		};

		// Data of the char message
		TCHAR ch;

		// Data of the window message
		struct
		{
			WPARAM wParam;
			LPARAM lParam;
		};
	};
};

// Message Function
ExMessage getmessage(BYTE filter = -1);										// Get a message until a message is available for retrieval
void getmessage(ExMessage *msg, BYTE filter = -1);							// Get a message until a message is available for retrieval
bool peekmessage(ExMessage *msg, BYTE filter = -1, bool removemsg = true);	// Get a message if any exist, otherwise return false
void flushmessage(BYTE filter = -1);										// Flush the message buffer
void setcapture();				// Enable the ability to capture mouse messages outside of the graphics window
void releasecapture();			// Disable the ability to capture mouse messages outside of the graphics window