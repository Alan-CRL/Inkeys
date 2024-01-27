#pragma once
#include "IdtMain.h"

//drawpad画笔
extern IMAGE alpha_drawpad; //临时画板
extern IMAGE putout; //主画板上叠加的控件内容
extern IMAGE tester; //图形绘制画板
extern IMAGE pptdrawpad; //PPT控件画板

extern IMAGE test_sign[5];

extern int recall_image_recond, recall_image_reference;
extern shared_mutex RecallImageManipulatedSm;
extern chrono::high_resolution_clock::time_point RecallImageManipulated;
extern tm RecallImageTm;
struct RecallStruct
{
	IMAGE img;
	std::map<std::pair<int, int>, bool> extreme_point;
	int type;
	pair<int, int> recond;
};
extern deque<RecallStruct> RecallImage;//撤回栈

//悬浮窗
extern IMAGE background;
extern Graphics graphics;

Bitmap* IMAGEToBitmap(IMAGE* easyXImage);