#include "PiMjpgServer.h"
#include "PiBuffer.h"
#include "PiHttpdInterpreter.h"
#include "PiFrame.h"
#include "PiException.h"
#include <algorithm>
#include <sys/socket.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <signal.h>
#include <unistd.h>
#include <sys/types.h> 
#include <sys/wait.h>

#define STR(str) DOSTR(str)
#define DOSTR(str) # str

#define BOUNDARY "boundary"

static PiMjpgServer* gSelf = NULL;

class HttpResponse {
public:
    HttpResponse() {
    }

    HttpResponse(const std::string& str) :  mHeader(str) {
    }

    HttpResponse(const char* format, ...) {
        va_list ap;
        va_start(ap, format);
        size_t size = vsnprintf(NULL, 0, format, ap);
        char* str = (char*)alloca(size + 1);
        if (str) {
            vsnprintf(str, size + 1, format, ap);
            mHeader += std::string(str, size);
        }
        va_end(ap);
    }

    HttpResponse& append(const std::string& header) {
        mHeader += header + "\r\n";
        return *this;
    }

    HttpResponse& append(const char* format, ...) {
        va_list ap;
        va_start(ap, format);
        size_t size = vsnprintf(NULL, 0, format, ap);
        char* str = (char*)alloca(size + 1);
        if (str) {
            vsnprintf(str, size + 1, format, ap);
            mHeader += std::string(str, size) + "\r\n";
        }
        va_end(ap);

        return *this;
    }

    const std::string& toString() const {
        return mHeader;
    }

private:
    std::string mHeader;
};

class TimeString {
public:
    TimeString() {
        time_t now = time(NULL);
        tm* t;
        t = localtime(&now);

        char now_string[64];
        if (strftime(now_string, sizeof(now_string), "%a, %d %b %y %H:%M:%S %Z", t) <= 0) {
            const char default_expires[] = "Mon, 3 Sep 2018 12:34:56 JST";
            memcpy(now_string, default_expires, sizeof(default_expires));
        }

        mValues = now_string;
    }

    const std::string& toString() const {
        return mValues;
    }

private:
    std::string mValues;
};

struct SrvSockInfo {
    // socket object
    int accept_socket;

    // socket address
    sockaddr_in addr;

    SrvSockInfo(uint32_t ip, uint32_t port) : accept_socket(-1) {
        // initialize sockaddr_in object
        memset(&addr, 0, sizeof(addr));

        // initialize socket address
        addr.sin_family     = AF_INET;     // TCP/IP
        addr.sin_addr.s_addr= htonl(ip);   // IP address
        addr.sin_port       = htons(port); // Port number
    }

    ~SrvSockInfo() {
        close();
    }

    void close() {
        if(accept_socket != -1) {
            ::close(accept_socket); // send FIN,ACK
            accept_socket = -1;
        }
    }
};

struct ClientSockInfo {
    // socket object
    int socket;

    // socket address
    sockaddr_in addr;

    // pthread object
    pthread_t thread;

    ClientSockInfo() : socket(-1), thread(0) {
        // initialize sockaddr_in object
        memset(&addr, 0, sizeof(addr));
    }

    ~ClientSockInfo() {
        close();
    }

    void close() {
        if(socket != -1) {
            ::close(socket);
            socket = -1;
        }
    }

    static void* do_run_httpd(ClientSockInfo* client) {
        int status;
        PiHttpdInterpreter intr;
        status  = client->recvRequest(gSelf->mSettings, &intr);

        if (!status && intr.method() == PiHttpdInterpreter::MT_GET && !intr.doc().compare("/bin-cgi/stream")) {
            client->sendMjpeg(gSelf->mSettings);
        } else {
            HttpResponse response(
                "HTTP/1.0 403 Forbidden\r\n"
                "Server: %s\r\n"
                "Content-Type: text/html\r\n"
                "Connection: close\r\n"
                "\r\n", // empty line
                gSelf->mSettings.server_name.c_str());

            if ((status = client->sendString(response.toString(), gSelf->mSettings)) != 0) {
                fprintf(stderr, "sendHeader err=%d\n", status);
                return (void*)status;
            }
        }

        return 0;
    }

    static void* run_httpd(void* arg) {
        void* ret = NULL;
        ClientSockInfo* client = static_cast<ClientSockInfo*>(arg);
        TRAP_LOG(ret = do_run_httpd(client););
        gSelf->removeClient(client);
        return ret;
    }

    int setTimeout(const PiServerSettings& settings) {
        int status;
        if ((status = setsockopt(socket, SOL_SOCKET, SO_SNDTIMEO,
                (char *)&settings.timeout_sending, sizeof(settings.timeout_sending))) < 0) {
            perror("[ClientSockInfo.setTimeout] line=" STR(__LINE__));
            return status;
        }
            
        if ((status = setsockopt(socket, SOL_SOCKET, SO_RCVTIMEO,
                (char *)&settings.timeout_recving, sizeof(settings.timeout_recving))) < 0) {
            perror("[ClientSockInfo.setTimeout] line=" STR(__LINE__));
            return status;
        }

        return 0;
    }

    int recvRequest(const PiServerSettings& settings, PiHttpdInterpreter* itr) {
        fd_set readfds;
        FD_ZERO(&readfds);
        FD_SET(socket, &readfds);

        timeval t(settings.timeout_recving);
        int status;
        int retry = 3;
        while ((status = select(socket+1, &readfds, NULL, NULL, &t)) <= 0) {
            if (errno == ETIMEDOUT) {
                // timeout
                fprintf(stderr, "recvRequest() was timeout: err=%d\n", errno);
                return ETIMEDOUT;
            } else if (status == 0 && errno == 0) {
                if (retry > 0) {
                    fprintf(stderr, "Couldn't read values to socket, so retry select()... retry=%d\n", retry);
                    usleep(1000000); // 100ms
                    retry--;
                } else {
                    fprintf(stderr, "Although retried select(), socket couldn't enable ....\n ");
                    return ETIMEDOUT;
                }
            } else {
                fprintf(stderr, "recvRequest() was failed: err=%d\n", errno);
                return errno;
            }
        }

        const int BUFFER_SIZE = 1024;
        char req_buf[BUFFER_SIZE];
        if ((status = recv(socket, req_buf, sizeof(req_buf), 0))<0) {
            perror("doHttpd() recv line=" STR(__LINE__));
            return errno;
        }

        return itr->init(req_buf, status);
    }

    int sendString(const std::string& string, const PiServerSettings& settings) const {
        fd_set writefds;
        FD_ZERO(&writefds);
        FD_SET(socket, &writefds);

        timeval t(settings.timeout_sending);
        int status;
        int retry = 3;
        while ((status = select(socket+1, NULL, &writefds, NULL, &t)) <= 0) {
            if (errno == ETIMEDOUT) {
                // timeout
                fprintf(stderr, "sendString() was timeout: str=%s", string.c_str());
                return ETIMEDOUT;
            } else if (status == 0 && errno == 0) {
                if (retry > 0) {
                    fprintf(stderr, "Couldn't write values to socket, so retry select()... retry=%d\n", retry);
                    usleep(1000000); // 100ms
                    retry--;
                } else {
                    fprintf(stderr, "Although retried select(), socket couldn't enable ....\n ");
                    return ETIMEDOUT;
                }
            } else {
                fprintf(stderr, "sendString() was failed: err=%d\n", errno);
                return errno;
            }
        }

        if ((status = send(socket, string.c_str(), string.length(), 0)) < 0) {
            fprintf(stderr, "Error in send() of sendString(): err=%d str=%s\n", status, string.c_str());
            return errno;
        }
        return 0;
    }

    int sendBuffer(const uint8_t* values, size_t size, const PiServerSettings& settings) const {
        fd_set writefds;
        FD_ZERO(&writefds);
        FD_SET(socket, &writefds);

        timeval t(settings.timeout_sending);
        int status;
        if ((status = select(socket+1, NULL, &writefds, NULL, &t)) <= 0) {
            if (errno == ETIMEDOUT) {
                // timeout
                fprintf(stderr, "sendBuffer() was timeout\n");
                return ETIMEDOUT;
            } else {
                fprintf(stderr, "sendBuffer() was failed\n");
                return errno;
            }
        }

        if ((status = send(socket, values, size, 0)) < 0) {
            fprintf(stderr, "Error in send() of sendBuffer(): err=%d\n", status);
            return errno;
        }
        return 0;
    }

    int sendMjpeg(const PiServerSettings& settings) const {
        TimeString now;
        HttpResponse responseHeader(
                "HTTP/1.0 200 OK\r\n"
                "Access-Control-Allow-Origin: *\r\n"
                "Connection: close\r\n"
                "Server: %s\r\n"
                "Cache-Control: no-store, no-cache, must-revalidate, pre-check=0, post-check=0, max-age=0\r\n"
                "Pragma: no-cache\r\n"
                "Expires: %s\r\n"
                "Content-Type: multipart/x-mixed-replace;boundary=" BOUNDARY "\r\n",
                settings.server_name.c_str(), now.toString().c_str());
        const std::string boundary_eof = "--" BOUNDARY "--";

        int status;
        if ((status = sendString(responseHeader.toString(), settings)) != 0) {
            fprintf(stderr, "Error in sendString() of sendMjpeg() status=%d\n", status);
            return status;
        }

        const PiCamSettings& cam_settings = settings.cam_settings;
        StaticBuffer tmp_buffer;
        if ((status = tmp_buffer.realloc(cam_settings.width * cam_settings.height * 3)) != 0) {
            fprintf(stderr, "failed to allocate tmp_buffer size=%d status=%d\n",
                    (cam_settings.width * cam_settings.height * 3), status);
            return ENOMEM;
        }

        PiFrame* frame = gSelf->mManager.attach();
        if (frame) {
            int i = 0;
            while (true) {
                status = frame->waitForReady(3);
                if (status) {
                    sendString(boundary_eof, gSelf->mSettings);
                    fprintf(stderr, "Error in waitForReady status=%d\n", status);
                    break; // Error (or timeout)
                }

                status = frame->lock(3);
                if (status) {
                    sendString(boundary_eof, gSelf->mSettings);
                    fprintf(stderr, "Error in PiFrame#lock status=%d\n", status);
                    break; // Error (or timeout)
                }

                size_t frame_size = frame->length;
                if (tmp_buffer.alloc_size < frame_size) {
                    if ((status = tmp_buffer.realloc(frame_size)) != 0) {
                        frame->unlock();
                        sendString(boundary_eof, gSelf->mSettings);
                        fprintf(stderr, "tmp_buffer#realloc err=%d\n", status);
                        break;
                    }
                }

                memcpy(tmp_buffer.values, frame->buffer, frame_size);

                frame->unlock();

                HttpResponse entityHeader(
                    "\r\n" // empty line
                    "--" BOUNDARY"\r\n"
                    "Content-Type: image/jpeg\r\n"
                    "Content-Length: %lu\r\n"
                    "\r\n",
                    frame_size);

                if (gSelf->mIsRunning) {
                    if ((status = sendString(entityHeader.toString(), gSelf->mSettings)) != 0) {
                        fprintf(stderr, "Error in sendString() of sendMjpeg() status=%d\n", status);
                        break;
                    }

                    if ((status = sendBuffer(tmp_buffer.values, frame_size, gSelf->mSettings)) != 0) {
                        fprintf(stderr, "Error in sendBuffer() of sendMjpeg() status=%d\n", status);
                        break;
                    }
                } else {
                    status = sendString(boundary_eof, gSelf->mSettings);
                    printf("send %s status=%d\n", boundary_eof.c_str(), status);
                    break; // finish
                }
            }
            gSelf->mManager.detach(frame);
        }

        printf("finish sendMjpeg()\n");
        return status;
    }
};

PiServerSettings::PiServerSettings() : ip_addr(0), port_number(8080), max_connections(5), server_name("test server") {

    timeout_sending.tv_sec = 10; // 10 seconds
    timeout_sending.tv_usec = 0;
    timeout_recving.tv_sec = 10; // 10 seconds
    timeout_recving.tv_usec = 0;

}

PiMjpgServer::PiMjpgServer(const PiServerSettings& settings)
        : mSettings(settings), mManager(settings.cam_settings), mIsRunning(true)  {
    // Please see following:
    // http://doi-t.hatenablog.com/entry/2014/06/10/033309
    signal(SIGPIPE, SIG_IGN);
    signal(SIGINT, sig_handler);

    gSelf = this;

    int status = pthread_mutex_init(&mMutex, NULL);
    if (status) fprintf(stderr, "Failed to create mMutex status=%d\n", status);
}

PiMjpgServer::~PiMjpgServer() {
    gSelf = NULL;
    signal(SIGINT, SIG_DFL);
    signal(SIGPIPE, SIG_DFL);

    pthread_mutex_destroy(&mMutex);
}

int PiMjpgServer::run() {
    SrvSockInfo srv(mSettings.ip_addr, mSettings.port_number);
    mSrv = &srv;

    int status;

    // Open the server socket
    if ((status = openServerSocket(srv)) < 0) {
        return status; // Error
    }

    ClientSockInfo* client = new ClientSockInfo();
    if (client) {

        // Start the main loop
        while (!wait(srv, *client)) {

            pthread_mutex_lock(&mMutex);      // Lock
            TRAP1(exception, msg, mClients.push_back(client););
            pthread_mutex_unlock(&mMutex); // Unlock
            if (exception) {
                // Remove client from gClient, and delete client
                removeClient(client);
                fprintf(stderr, "Exception in mClients.push_back msg=%s\n", msg.c_str());
                break; // Error
            }

            // Launch the thread communicate with host.
            if ((status = pthread_create(&client->thread, NULL, ClientSockInfo::run_httpd, client)) < 0) {
                fprintf(stderr, "Failed to create thread status=%d\n", status);
                // Remove client from gClient, and delete client
                removeClient(client);
                break;
            }

            // Create new ClientSockInfo
            client = new ClientSockInfo();
            if (client == NULL) {
                fprintf(stderr, "Failed to allocate ClientSockInfo\n");
                break; // Error
            }
        }
    }

    printf("Waitting children...\n");

    // Repeat as long as the clients exist
    while (true) {
        pthread_mutex_lock(&mMutex);
        int n = mClients.size();
        pthread_mutex_unlock(&mMutex);

        if (n <= 0) break;
        usleep(100);
    }


    printf("Finished MjpgServer\n");
    return 0;
}

void PiMjpgServer::removeClient(ClientSockInfo* client) {
    bool removed = false;
    // Cleanup ClientSockInfo from mClients
    pthread_mutex_lock(&mMutex);

    TRAP_LOG(
        std::vector<ClientSockInfo*>::iterator it = std::find(mClients.begin(), mClients.end(), client);
        if (it != mClients.end()) {
            mClients.erase(it);
            removed = true;
        }
    );

    int n = mClients.size();

    pthread_mutex_unlock(&mMutex);
    if (!removed) fprintf(stderr, "Couldn't erase ClientSockInfo\n");

    client->close();
    delete client;

    printf("removeClient: num=%d\n", n);
}

void PiMjpgServer::sig_handler(int signum) {
    if (signum == SIGINT) {
        gSelf->mIsRunning = false;
        gSelf->mSrv->close();
    }
}

int PiMjpgServer::openServerSocket(SrvSockInfo& srv) {
    // Create the server socket
    srv.accept_socket = socket(PF_INET, SOCK_STREAM, 0);
    if (srv.accept_socket < 0) {
        int err = errno;
        if (err != EBADF) perror("Failed to create the accepting socket line=" STR(__LINE__));
        return err;
    }

    int status;

    // Bind the server socket
    if ((status = bind(srv.accept_socket, (sockaddr*)&srv.addr, sizeof(srv.addr))) < 0) {
        perror("Failed to bind line=" STR(__LINE__));
        return status;
    }

    // Listen the server socket
    if ((status = listen(srv.accept_socket, mSettings.max_connections)) < 0) {
        perror("Failed to listen line=" STR(__LINE__));
        return status;
    }

    return 0;
}

int PiMjpgServer::wait(SrvSockInfo& srv, ClientSockInfo& client) {
    for (;;) {
        socklen_t client_addr_len = sizeof(client.addr);
        client.socket = accept(srv.accept_socket, (sockaddr*)&client.addr, &client_addr_len);
        if (client.socket < 0) {
            int err = errno;
            if (err == EINTR) {
                continue; // Retry to call accept();
            }
            if (err != EBADF) perror("accept() line=" STR(__LINE__));
            return err;
        } else {
            client.setTimeout(mSettings);
            break; // Success
        }
    }
    return 0;
}

