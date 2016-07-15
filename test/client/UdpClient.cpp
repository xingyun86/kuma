#include "UdpClient.h"

#if defined(KUMA_OS_WIN)
# include <Ws2tcpip.h>
#else
# include <arpa/inet.h>
#endif

UdpClient::UdpClient(EventLoop* loop, long conn_id, TestLoop* server)
: loop_(loop)
, udp_(loop_)
, server_(server)
, conn_id_(conn_id)
, index_(0)
, max_send_count_(200000)
{
    
}

int UdpClient::bind(const char* bind_host, uint16_t bind_port)
{
    udp_.bind(bind_host, bind_port);
    udp_.setReadCallback([this] (int err) { onReceive(err); });
    udp_.setErrorCallback([this] (int err) { onClose(err); });
    return 0;
}

int UdpClient::close()
{
    return udp_.close();
}

void UdpClient::startSend(const char* host, uint16_t port)
{
    host_ = host;
    port_ = port;
    start_point_ = std::chrono::steady_clock::now();
    sendData();
}

void UdpClient::sendData()
{
    uint8_t buf[1024];
    *(uint32_t*)buf = htonl(++index_);
    udp_.send(buf, sizeof(buf), host_.c_str(), port_);
}

void UdpClient::onReceive(int err)
{
    char buf[4096] = {0};
    char ip[128];
    uint16_t port = 0;
    do {
        int bytes_read = udp_.receive((uint8_t*)buf, sizeof(buf), ip, sizeof(ip), port);
        if(bytes_read > 0) {
            uint32_t index = 0;
            if(bytes_read >= 4) {
                index = ntohl(*(uint32_t*)buf);
            }
            if(index % 10000 == 0) {
                printf("UdpClient::onReceive, bytes_read=%d, index=%d\n", bytes_read, index);
            }
            if(index < max_send_count_) {
                sendData();
            } else {
                std::chrono::steady_clock::time_point end_point = std::chrono::steady_clock::now();
                std::chrono::milliseconds diff_ms = std::chrono::duration_cast<std::chrono::milliseconds>(end_point - start_point_);
                printf("spent %lld ms to echo %u packets\n", diff_ms.count(), max_send_count_);
            }
        } else if (0 == bytes_read) {
            break;
        } else {
            printf("UdpClient::onReceive, err=%d\n", getLastError());
        }
    } while (0);
}

void UdpClient::onClose(int err)
{
    printf("UdpClient::onClose, err=%d\n", err);
    server_->removeObject(conn_id_);
}
