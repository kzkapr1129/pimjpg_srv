#pragma once

#include "PiCameraManager.h"
#include <sys/time.h>
#include <stdint.h>
#include <string>

struct PiServerSettings {
    uint32_t ip_addr; // def: 0
    uint32_t port_number; // def: 8080
    timeval timeout_sending; // def: 10sec
    timeval timeout_recving;  // def: 10sec
    uint32_t max_connections; // def: 5
    std::string server_name; // test
    PiCamSettings cam_settings;

    PiServerSettings();
};

struct SrvSockInfo;
struct ClientSockInfo;
class PiMjpgServer {
public:
    PiMjpgServer(const PiServerSettings& settings);
    ~PiMjpgServer();

    int run();

private:
    static void sig_handler(int signum);

    int openServerSocket(SrvSockInfo& srv);
    int wait(SrvSockInfo& srv, ClientSockInfo& client);

private:
    void removeClient(ClientSockInfo* client);

    PiServerSettings mSettings;
    PiCameraManager mManager;
    pthread_mutex_t mMutex;

    volatile bool mIsRunning;

    SrvSockInfo* mSrv;
    std::vector<ClientSockInfo*> mClients;

    friend ClientSockInfo;
};
