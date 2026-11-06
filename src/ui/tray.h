#ifndef PRISM_TRAY_H
#define PRISM_TRAY_H

#include "ui/app.h"

void tray_init(App *app);
void tray_shutdown(App *app);
void tray_present(App *app);
gboolean tray_is_active(void);

#endif
