#pragma once
#include "text_compiler.h"

enum class TransferState { Idle, Running, Paused, Cancelling, Cancelled, Complete, Maintenance };
struct TransferStatus {
    TransferState state = TransferState::Idle;
    uint32_t id = 0;
    std::size_t sent = 0;
    std::size_t total = 0;
    uint32_t linesSent = 0;
    uint32_t lines = 0;
};
const char *transferStateName(TransferState state);
TransferStatus textTransferStatus();
// On success the worker owns plan; on failure the caller still owns it.
bool textTransferStart(TextKey *plan, std::size_t length, uint32_t lines);
bool textTransferControl(const char *command);
bool textTransferBeginMaintenance();
void textTransferEndMaintenance();
