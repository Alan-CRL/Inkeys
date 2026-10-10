#pragma once
#include "IdtMain.h"
#include "Inkeys/Graphics/Surface.hpp"

//drawpad画笔
extern Inkeys::Graphics::DibSurface alpha_drawpad; //临时画板
extern Inkeys::Graphics::DibSurface putout; //主画板上叠加的控件内容
extern Inkeys::Graphics::DibSurface tester; //图形绘制画板
extern Inkeys::Graphics::DibSurface pptdrawpad; //PPT控件画板

extern int recall_image_recond, recall_image_reference;
extern shared_mutex RecallImageManipulatedSm;
extern chrono::high_resolution_clock::time_point RecallImageManipulated;
extern tm RecallImageTm;
struct RecallStruct
{
	Inkeys::Graphics::DibSurface img;
	std::map<std::pair<int, int>, bool> extreme_point;
	int type;
	pair<int, int> recond;
};
extern int RecallImagePeak;
extern deque<RecallStruct> RecallImage;//撤回栈

//悬浮窗
extern Inkeys::Graphics::DibSurface background;
extern Graphics graphics;

Bitmap* SurfaceToBitmap(const Inkeys::Graphics::DibSurface* surface);
bool CopySurface(Inkeys::Graphics::DibSurface* target, const Inkeys::Graphics::DibSurface* source);

extern shared_mutex loadImageSm;
bool LoadSurfaceFromFile(Inkeys::Graphics::DibSurface* destination, LPCTSTR path, int width = 0, int height = 0);
bool LoadSurfaceFromResource(Inkeys::Graphics::DibSurface* destination, LPCTSTR resourceType, LPCTSTR resourceName, int width = 0, int height = 0);
