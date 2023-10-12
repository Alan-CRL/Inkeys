#pragma once
#include "IdtMain.h"

//drawpad画笔
extern IMAGE drawpad; //主画板
extern IMAGE alpha_drawpad; //临时画板
extern IMAGE last_drawpad; //上一次绘制内容，后续将弃用
extern IMAGE putout; //主画板上叠加的控件内容
extern IMAGE tester; //图形绘制画板
extern IMAGE pptdrawpad; //PPT控件画板

extern IMAGE test_sign[5];

struct RecallStruct
{
	IMAGE img;
	map<pair<int, int>, bool> extreme_point;
};
extern deque<RecallStruct> RecallImage;//撤回栈

//悬浮窗
extern IMAGE background;
extern Graphics graphics;