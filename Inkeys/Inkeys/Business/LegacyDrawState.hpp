#pragma once

#include "../../IdtAtomic.h"

// Draw2 暂时保留的跨模块状态，已从 UI2 实现中迁出。
extern double state;
extern double target_status;
extern bool reserve_drawpad;
extern bool smallcard_refresh;
extern int BackgroundColorMode;
extern IdtAtomic<bool> ConfirmaNoMouMsgSignal;
extern IdtAtomic<bool> ConfirmaNoMouFunSignal;
extern IdtAtomic<bool> confirmaNoMouUpSignal;
