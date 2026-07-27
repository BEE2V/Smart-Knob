#pragma once

#include <stdint.h>

#include "input.h"

void initUI();
void handleUIInput(const InputState &input);
void renderUI();

void showOtaUpdateStart();
void showOtaUpdateProgress(uint8_t percentage);
void showOtaUpdateComplete();
void showOtaUpdateError(const char *message);
