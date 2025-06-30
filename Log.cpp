#include "Log.h"

ImGuiTextBuffer g_LogBuffer;
bool g_ScrollToBottom = false;

void OutputLogMessage(const std::string& msg) {
    g_LogBuffer.appendf("%s", msg.c_str());
    g_ScrollToBottom = true;
}

void ClearLogBuffer() {
    g_LogBuffer.clear();
}
