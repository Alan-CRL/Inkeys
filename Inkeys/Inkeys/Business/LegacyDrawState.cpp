#include "LegacyDrawState.hpp"

double state = 0.0;
double target_status = 0.0;
bool reserve_drawpad = false;
bool smallcard_refresh = true;
int BackgroundColorMode = 0;
IdtAtomic<bool> ConfirmaNoMouMsgSignal;
IdtAtomic<bool> ConfirmaNoMouFunSignal;
IdtAtomic<bool> confirmaNoMouUpSignal;
