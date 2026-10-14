#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
void lucid_button_trace_begin(bool sd_ready);
void lucid_button_trace_report(uint8_t address, uint8_t interface, uint8_t endpoint,
    const uint8_t *data, size_t bytes, bool decoded, uint8_t pressed, bool streaming);
void lucid_button_trace_poll(unsigned transfers, unsigned errors);
// 0=off, 1=ready/saved, 2=write error. Last report is for the LCD footer.
unsigned lucid_button_trace_status(void);
int lucid_button_trace_last(void);
#ifdef __cplusplus
}
#endif
