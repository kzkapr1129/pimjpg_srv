#include "PiFrame.h"
#include "PiCamera.h"
#include "PiMjpgServer.h"
#include <stdio.h>
#include <signal.h>

int main() {
    PiServerSettings settings;
    PiMjpgServer srv(settings);
    return srv.run();
}
