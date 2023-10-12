#include "IdtImage.h"

//drawpad画笔
IMAGE drawpad(GetSystemMetrics(SM_CXSCREEN), GetSystemMetrics(SM_CYSCREEN)); //主画板
IMAGE alpha_drawpad(GetSystemMetrics(SM_CXSCREEN), GetSystemMetrics(SM_CYSCREEN)); //临时画板
IMAGE last_drawpad; //上一次绘制内容，后续将弃用
IMAGE putout; //主画板上叠加的控件内容
IMAGE tester(GetSystemMetrics(SM_CXSCREEN), GetSystemMetrics(SM_CYSCREEN)); //图形绘制画板
IMAGE pptdrawpad(GetSystemMetrics(SM_CXSCREEN), GetSystemMetrics(SM_CYSCREEN)); //PPT控件画板

IMAGE test_sign[5];

deque<RecallStruct> RecallImage;//撤回栈

//悬浮窗
IMAGE background(576, 386);
Graphics graphics(GetImageHDC(&background));