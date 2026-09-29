#pragma once
// Интерфейс miniC. Окно Windows и DirectX живут в gui_platform.cpp,
// сам интерфейс — в gui.cpp. Между ними только эти три функции.

#include <windows.h>

void guiInit(HWND hwnd, float scale);   // шрифты, тема, начальный текст
void guiFrame();                         // построить один кадр интерфейса
bool guiConfirmClose();                  // спросить про несохранённые изменения
