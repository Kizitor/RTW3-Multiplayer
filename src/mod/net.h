#pragma once
#include "common.h"

// TCP transport for the session. One background thread does all socket I/O; received messages are
// queued and the main thread is poked with WM_MP_NET. Frames: [u32 len][u16 type][payload].
namespace net {

enum : uint16_t {
    MSG_HELLO = 1,
    MSG_WELCOME,
    MSG_REJECT,
    MSG_LOBBY,
    MSG_CHAT,
    MSG_PICK,
    MSG_STATE,
    MSG_SUBMIT,
    MSG_STATUS,
    MSG_FORCE_SUBMIT,
    MSG_PING,
    MSG_PONG,
    MSG_BYE,
    MSG_ACK,
    MSG_DIPLO,
    MSG_EVENT,
    EV_CONNECTED = 0xFF00,
    EV_DISCONNECTED = 0xFF01,
};

struct Msg {
    int peer = -1;
    uint16_t type = 0;
    std::string data;
};

bool Listen(uint16_t port, std::string& err);
bool Connect(const std::string& host, uint16_t port, std::string& err);  // async: EV_CONNECTED or EV_DISCONNECTED
void Shutdown();
bool Active();
bool IsHost();
void Send(int peer, uint16_t type, const std::string& data);
void SendAll(uint16_t type, const std::string& data);
size_t MaxMessageBytes();  // larger messages are refused by the receiver
void Kick(int peer);
bool Pop(Msg& m);
std::string PeerAddress(int peer);
int PeerPingMs(int peer);
size_t PendingOut(int peer);

struct LocalAddr {
    std::string ip;
    std::string adapter;
    std::string vpn;  // "Hamachi", "ZeroTier", "Radmin VPN" or ""
};
std::vector<LocalAddr> LocalAddresses();

// LAN discovery over UDP broadcast (port+1). Host answers with its session name.
void SetDiscoveryInfo(const std::string& info);  // host side
std::vector<std::pair<std::string, std::string>> Discover(uint16_t port, int waitMs);  // (ip, info)

}  // namespace net
