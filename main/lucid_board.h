#pragma once
#include <stdbool.h>

// All SPI/display calls belong to the application task.
void lucid_board_init(void);
void lucid_status(const char *status, const char *detail);
