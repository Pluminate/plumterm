#pragma once

class QWidget;

#ifdef Q_OS_MACOS
void applyMacTitleBar(QWidget *window, bool dark);
#endif
