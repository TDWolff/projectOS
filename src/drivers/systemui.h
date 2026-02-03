#ifndef SYSTEMUI_H
#define SYSTEMUI_H

#include "../include/types.h"

// Initializes the entire System UI (Dock, Top Bar, Widgets)
void systemui_init();

// Redraws dynamic elements (Clock, Battery, etc)
void systemui_update();

#endif
