#ifndef BOARD_ADC_LIVE_H
#define BOARD_ADC_LIVE_H
#include "scope_ui.h"
int board_adc_live_init(void);
void board_adc_live_poll(ScopeUI *ui);
void board_adc_live_status(void);
#endif
