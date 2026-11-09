#include "node_state.h"

String pendingMsg = "";
bool txPending = false;
bool joined = false;
bool wasIdle = false;
unsigned long uplinkCount = 0, downlinkCount = 0, uartLines = 0;
bool pollPending = false;
