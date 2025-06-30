#include "SerialPort.h"
#include "Log.h"
#include <sstream>
#include <iomanip>

SerialPort::SerialPort()
    : hSerial(INVALID_HANDLE_VALUE), currentPort(-1), connected(false) {
}

SerialPort::~SerialPort() {
    Disconnect();
}

bool SerialPort::Connect(int portNumber) {
    if (connected && currentPort == portNumber) {
        return true; // Уже подключены к этому порту
    }

    // Отключаемся от текущего порта
    Disconnect();

    // Формируем имя порта
    std::string portName = "\\\\.\\COM" + std::to_string(portNumber);

    OutputLogMessage("[SERIAL] Попытка подключения к " + portName + "\n");

    // Открываем порт
    hSerial = CreateFileA(
        portName.c_str(),
        GENERIC_READ | GENERIC_WRITE,
        0,
        NULL,
        OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL,
        NULL
    );

    if (hSerial == INVALID_HANDLE_VALUE) {
        LogError("CreateFile");
        return false;
    }

    // Настраиваем порт
    if (!ConfigurePort()) {
        CloseHandle(hSerial);
        hSerial = INVALID_HANDLE_VALUE;
        return false;
    }

    currentPort = portNumber;
    connected = true;

    OutputLogMessage("[SERIAL] Успешно подключено к COM" + std::to_string(portNumber) + "\n");
    return true;
}

void SerialPort::Disconnect() {
    if (hSerial != INVALID_HANDLE_VALUE) {
        CloseHandle(hSerial);
        hSerial = INVALID_HANDLE_VALUE;

        if (connected) {
            OutputLogMessage("[SERIAL] Отключено от COM" + std::to_string(currentPort) + "\n");
        }
    }

    connected = false;
    currentPort = -1;
}

bool SerialPort::IsConnected() const {
    return connected && hSerial != INVALID_HANDLE_VALUE;
}

int SerialPort::GetCurrentPort() const {
    return currentPort;
}

bool SerialPort::ConfigurePort() {
    DCB dcbSerialParams = { 0 };
    dcbSerialParams.DCBlength = sizeof(dcbSerialParams);

    if (!GetCommState(hSerial, &dcbSerialParams)) {
        LogError("GetCommState");
        return false;
    }

    // Настройки как в ESP32: 921600 baud, 8N1
    // CBR_921600 не определен, используем числовое значение
    dcbSerialParams.BaudRate = 921600;  // Исправлено: используем числовое значение
    dcbSerialParams.ByteSize = 8;
    dcbSerialParams.StopBits = ONESTOPBIT;
    dcbSerialParams.Parity = NOPARITY;
    dcbSerialParams.fDtrControl = DTR_CONTROL_ENABLE;
    dcbSerialParams.fRtsControl = RTS_CONTROL_ENABLE;

    if (!SetCommState(hSerial, &dcbSerialParams)) {
        LogError("SetCommState");
        return false;
    }

    // Настройка таймаутов
    COMMTIMEOUTS timeouts = { 0 };
    timeouts.ReadIntervalTimeout = 50;
    timeouts.ReadTotalTimeoutConstant = 50;
    timeouts.ReadTotalTimeoutMultiplier = 10;
    timeouts.WriteTotalTimeoutConstant = 50;
    timeouts.WriteTotalTimeoutMultiplier = 10;

    if (!SetCommTimeouts(hSerial, &timeouts)) {
        LogError("SetCommTimeouts");
        return false;
    }

    return true;
}

bool SerialPort::SendMouseMove(int8_t dx, int8_t dy) {
    if (!IsConnected()) return false;

    uint8_t packet[3] = { 0x01, static_cast<uint8_t>(dx), static_cast<uint8_t>(dy) };
    return SendData(packet, 3);
}

bool SerialPort::SendMousePress(uint8_t button) {
    if (!IsConnected()) return false;

    uint8_t packet[2] = { 0x02, button };
    return SendData(packet, 2);
}

bool SerialPort::SendMouseRelease(uint8_t button) {
    if (!IsConnected()) return false;

    uint8_t packet[2] = { 0x03, button };
    return SendData(packet, 2);
}

bool SerialPort::SendData(const uint8_t* data, size_t size) {
    if (!IsConnected()) return false;

    DWORD bytesWritten;
    if (!WriteFile(hSerial, data, static_cast<DWORD>(size), &bytesWritten, NULL)) {
        LogError("WriteFile");
        // При ошибке записи считаем, что соединение потеряно
        connected = false;
        return false;
    }

    if (bytesWritten != size) {
        OutputLogMessage("[SERIAL] Предупреждение: записано " + std::to_string(bytesWritten) +
            " из " + std::to_string(size) + " байт\n");
        return false;
    }

    return true;
}

void SerialPort::LogError(const std::string& operation) {
    DWORD error = GetLastError();
    std::ostringstream oss;
    oss << "[SERIAL ERROR] " << operation << " failed with error " << error;

    // Добавляем расшифровку частых ошибок
    switch (error) {
    case ERROR_FILE_NOT_FOUND:
        oss << " (Порт не найден)";
        break;
    case ERROR_ACCESS_DENIED:
        oss << " (Доступ запрещен - порт занят?)";
        break;
    case ERROR_INVALID_HANDLE:
        oss << " (Неверный дескриптор)";
        break;
    case ERROR_OPERATION_ABORTED:
        oss << " (Операция прервана)";
        break;
    }

    oss << "\n";
    OutputLogMessage(oss.str());
}