#pragma once
#include <string>
#include "imgui.h"


void OutputLogMessage(const std::string& msg);
void ClearLogBuffer();

// Глобальный лог-буфер (для ImGui Console)
extern ImGuiTextBuffer g_LogBuffer;
extern bool g_ScrollToBottom;
