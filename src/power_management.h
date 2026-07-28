#pragma once

enum class RuntimeStage : unsigned char
{
  Boot,
  System,
  Input,
  Battery,
  HomeAssistant,
  Mqtt,
  Ota,
  UiInput,
  UiRender,
  History,
  Idle
};

void initPowerManagement();
void enterDeepSleep();
const char *getResetReasonText();
const char *getResetDiagnosticText();
void recordRuntimeStage(RuntimeStage stage);
