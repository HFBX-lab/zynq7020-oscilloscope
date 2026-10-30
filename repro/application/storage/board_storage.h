#ifndef BOARD_STORAGE_H
#define BOARD_STORAGE_H
#include "scope_storage_ui.h"
/* UI thread only; scheduler must already be running. Poll under the UI lock. */
int board_storage_start(ScopeStorageUI *panel);
bool board_storage_submit(void *context,const ScopeTfRequest *request);
void board_storage_poll(void);
void board_storage_status(void);
#endif
