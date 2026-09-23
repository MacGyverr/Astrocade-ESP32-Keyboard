#pragma once

bool statusLedInit();

void statusLedSetBleStartupSlow();
void statusLedSetBleStartupFast();
void statusLedShowBleStartupFailure();
void statusLedOff();

void statusLedSignalUsbDevice();
void statusLedSignalUsbKeyboardReady();
void statusLedSignalUsbFailure();
void statusLedSignalUsbKeypress();
void statusLedSignalBleInput();
void statusLedSignalCrosspointStrobe();
