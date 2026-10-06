#include "net.h"
#include <iphlpapi.h>
#include <thread>
#include <atomic>

namespace net {

static const uint32_t kMaxFrame = 96u * 1024 * 1024;

struct Peer {
    int id;
    SOCKET s = INVALID_SOCKET;
    std::string addr;
    std::string in;
    std::string out;
    DWORD lastRecv = 0;
    DWORD lastPingSent = 0;
    int pingMs = -1;
    bool closing = false;
};

static std::mutex g_mx;
static std::map<int, Peer> g_peers;
static std::deque<Msg> g_inbox;
static SOCKET g_listen = INVALID_SOCKET;
static SOCKET g_udp = INVALID_SOCKET;
static std::thread g_thread;
static std::atomic<bool> g_run{false};
static bool g_isHost = false;
static int g_nextId = 1;
static std::string g_discoveryInfo;
static uint16_t g_port = 0;
static bool g_wsa = false;

static void InitWsa() {
    if (!g_wsa) {
        WSADATA w;
        g_wsa = WSAStartup(MAKEWORD(2, 2), &w) == 0;
    }
}

static void Post(Msg m) {
    {
        std::lock_guard<std::mutex> lk(g_mx);
        g_inbox.push_back(std::move(m));
    }
    HWND h = DispatcherHwnd();
    if (h) PostMessageW(h, WM_MP_NET, 0, 0);
}

static void Frame(std::string& out, uint16_t type, const std::string& data) {
    uint32_t len = (uint32_t)data.size() + 2;
    out.append((const char*)&len, 4);
    out.append((const char*)&type, 2);
    out += data;
}

static void ConfigureSocket(SOCKET s) {
    BOOL one = TRUE;
    setsockopt(s, IPPROTO_TCP, TCP_NODELAY, (const char*)&one, sizeof(one));
    setsockopt(s, SOL_SOCKET, SO_KEEPALIVE, (const char*)&one, sizeof(one));
    u_long nb = 1;
    ioctlsocket(s, FIONBIO, &nb);
}

static void DropPeer(int id, const char* why) {
    SOCKET s = INVALID_SOCKET;
    {
        std::lock_guard<std::mutex> lk(g_mx);
        auto it = g_peers.find(id);
        if (it == g_peers.end()) return;
        s = it->second.s;
        g_peers.erase(it);
    }
    if (s != INVALID_SOCKET) closesocket(s);
    Log("net: peer %d dropped (%s)", id, why);
    Msg m;
    m.peer = id;
    m.type = EV_DISCONNECTED;
    m.data = why;
    Post(std::move(m));
}

static void HandleFrames(Peer& p, std::vector<Msg>& outMsgs, std::string& bad) {
    while (p.in.size() >= 6) {
        uint32_t len;
        memcpy(&len, p.in.data(), 4);
        if (len < 2 || len > kMaxFrame) {
            char why[96];
            if (len > kMaxFrame)
                snprintf(why, sizeof(why), "message too large (%u MB, limit %u MB)", len >> 20, kMaxFrame >> 20);
            else
                snprintf(why, sizeof(why), "protocol error (bad frame length %u)", len);
            Log("net: peer %d: %s, %u bytes buffered", p.id, why, (unsigned)p.in.size());
            bad = why;
            return;
        }
        if (p.in.size() < 4 + (size_t)len) return;
        Msg m;
        m.peer = p.id;
        memcpy(&m.type, p.in.data() + 4, 2);
        m.data = p.in.substr(6, len - 2);
        p.in.erase(0, 4 + (size_t)len);
        if (m.type == MSG_PING) {
            Frame(p.out, MSG_PONG, m.data);
            continue;
        }
        if (m.type == MSG_PONG) {
            DWORD sent = m.data.size() == 4 ? *(const DWORD*)m.data.data() : 0;
            if (sent) p.pingMs = (int)(GetTickCount() - sent);
            continue;
        }
        outMsgs.push_back(std::move(m));
    }
}

static void HandleDiscovery() {
    if (g_udp == INVALID_SOCKET) return;
    char buf[512];
    sockaddr_in from;
    int fl = sizeof(from);
    int n = recvfrom(g_udp, buf, sizeof(buf) - 1, 0, (sockaddr*)&from, &fl);
    if (n <= 0) return;
    buf[n] = 0;
    if (strncmp(buf, "RTW3MP?", 7) != 0) return;
    std::string reply;
    {
        std::lock_guard<std::mutex> lk(g_mx);
        reply = "RTW3MP!" + std::to_string(g_port) + "|" + g_discoveryInfo;
    }
    sendto(g_udp, reply.data(), (int)reply.size(), 0, (sockaddr*)&from, fl);
}

static void ThreadMain() {
    while (g_run) {
        fd_set rd, wr;
        FD_ZERO(&rd);
        FD_ZERO(&wr);
        std::vector<std::pair<int, SOCKET>> socks;
        {
            std::lock_guard<std::mutex> lk(g_mx);
            for (auto& kv : g_peers) {
                if (kv.second.s == INVALID_SOCKET) continue;
                socks.emplace_back(kv.first, kv.second.s);
                FD_SET(kv.second.s, &rd);
                if (!kv.second.out.empty()) FD_SET(kv.second.s, &wr);
            }
        }
        if (g_listen != INVALID_SOCKET) FD_SET(g_listen, &rd);
        if (g_udp != INVALID_SOCKET) FD_SET(g_udp, &rd);
        timeval tv = {0, 30000};
        int nready = (rd.fd_count || wr.fd_count) ? select(0, &rd, &wr, nullptr, &tv) : (Sleep(30), 0);
        if (!g_run) break;
        if (nready > 0 && g_listen != INVALID_SOCKET && FD_ISSET(g_listen, &rd)) {
            sockaddr_in a;
            int al = sizeof(a);
            SOCKET c = accept(g_listen, (sockaddr*)&a, &al);
            if (c != INVALID_SOCKET) {
                ConfigureSocket(c);
                char ip[64];
                inet_ntop(AF_INET, &a.sin_addr, ip, sizeof(ip));
                int id;
                {
                    std::lock_guard<std::mutex> lk(g_mx);
                    id = g_nextId++;
                    Peer p;
                    p.id = id;
                    p.s = c;
                    p.addr = ip;
                    p.lastRecv = GetTickCount();
                    g_peers[id] = p;
                }
                Log("net: accepted peer %d from %s", id, ip);
                Msg m;
                m.peer = id;
                m.type = EV_CONNECTED;
                m.data = ip;
                Post(std::move(m));
            }
        }
        if (nready > 0 && g_udp != INVALID_SOCKET && FD_ISSET(g_udp, &rd)) HandleDiscovery();
        std::vector<std::pair<int, std::string>> drops;
        std::vector<Msg> received;
        DWORD now = GetTickCount();
        for (auto& ps : socks) {
            std::lock_guard<std::mutex> lk(g_mx);
            auto it = g_peers.find(ps.first);
            if (it == g_peers.end()) continue;
            Peer& p = it->second;
            try {
                if (nready > 0 && FD_ISSET(ps.second, &rd)) {
                    char buf[65536];
                    while (true) {
                        int n = recv(p.s, buf, sizeof(buf), 0);
                        if (n > 0) {
                            p.in.append(buf, n);
                            p.lastRecv = now;
                            continue;
                        }
                        if (n == 0) {
                            drops.emplace_back(p.id, "closed by remote");
                        } else if (WSAGetLastError() != WSAEWOULDBLOCK) {
                            drops.emplace_back(p.id, "receive error " + std::to_string(WSAGetLastError()));
                        }
                        break;
                    }
                    std::string bad;
                    HandleFrames(p, received, bad);
                    if (!bad.empty()) drops.emplace_back(p.id, bad);
                }
            } catch (const std::bad_alloc&) {
                Log("net: peer %d: out of memory receiving (%u bytes buffered)", p.id, (unsigned)p.in.size());
                std::string().swap(p.in);
                drops.emplace_back(p.id, "out of memory while receiving");
            }
            if (!p.out.empty()) {
                int n = send(p.s, p.out.data(), (int)std::min<size_t>(p.out.size(), 1 << 20), 0);
                if (n > 0)
                    p.out.erase(0, n);
                else if (n < 0 && WSAGetLastError() != WSAEWOULDBLOCK)
                    drops.emplace_back(p.id, "send error " + std::to_string(WSAGetLastError()));
            }
            if (now - p.lastPingSent > 5000) {
                p.lastPingSent = now;
                std::string t((const char*)&now, 4);
                Frame(p.out, MSG_PING, t);
            }
            if (now - p.lastRecv > 60000) drops.emplace_back(p.id, "timed out");
            if (p.closing && p.out.empty()) drops.emplace_back(p.id, "closed");
        }
        for (auto& m : received) Post(std::move(m));
        for (auto& d : drops) DropPeer(d.first, d.second.c_str());
    }
}

static bool StartThread() {
    g_run = true;
    g_thread = std::thread(ThreadMain);
    return true;
}

bool Listen(uint16_t port, std::string& err) {
    Shutdown();
    InitWsa();
    g_listen = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (g_listen == INVALID_SOCKET) {
        err = "socket() failed";
        return false;
    }
    BOOL one = TRUE;
    setsockopt(g_listen, SOL_SOCKET, SO_EXCLUSIVEADDRUSE, (const char*)&one, sizeof(one));
    sockaddr_in a = {};
    a.sin_family = AF_INET;
    a.sin_port = htons(port);
    a.sin_addr.s_addr = htonl(INADDR_ANY);
    if (bind(g_listen, (sockaddr*)&a, sizeof(a)) != 0 || listen(g_listen, 8) != 0) {
        err = "port " + std::to_string(port) + " is in use or blocked (" + std::to_string(WSAGetLastError()) + ")";
        closesocket(g_listen);
        g_listen = INVALID_SOCKET;
        return false;
    }
    u_long nb = 1;
    ioctlsocket(g_listen, FIONBIO, &nb);
    g_udp = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (g_udp != INVALID_SOCKET) {
        sockaddr_in u = {};
        u.sin_family = AF_INET;
        u.sin_port = htons(port + 1);
        u.sin_addr.s_addr = htonl(INADDR_ANY);
        if (bind(g_udp, (sockaddr*)&u, sizeof(u)) != 0) {
            closesocket(g_udp);
            g_udp = INVALID_SOCKET;
        } else {
            ioctlsocket(g_udp, FIONBIO, &nb);
        }
    }
    g_isHost = true;
    g_port = port;
    Log("net: hosting on port %u (discovery %s)", port, g_udp != INVALID_SOCKET ? "on" : "off");
    return StartThread();
}

bool Connect(const std::string& host, uint16_t port, std::string& err) {
    Shutdown();
    InitWsa();
    addrinfo hints = {}, *res = nullptr;
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    if (getaddrinfo(host.c_str(), std::to_string(port).c_str(), &hints, &res) != 0 || !res) {
        err = "cannot resolve " + host;
        return false;
    }
    sockaddr_in addr = *(sockaddr_in*)res->ai_addr;
    freeaddrinfo(res);
    SOCKET s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (s == INVALID_SOCKET) {
        err = "socket() failed";
        return false;
    }
    g_isHost = false;
    g_port = port;
    g_run = true;
    g_thread = std::thread([s, addr, host]() mutable {
        u_long nb = 1;
        ioctlsocket(s, FIONBIO, &nb);
        connect(s, (sockaddr*)&addr, sizeof(addr));
        fd_set wr, ex;
        FD_ZERO(&wr);
        FD_ZERO(&ex);
        FD_SET(s, &wr);
        FD_SET(s, &ex);
        timeval tv = {10, 0};
        int r = select(0, nullptr, &wr, &ex, &tv);
        if (r <= 0 || FD_ISSET(s, &ex)) {
            closesocket(s);
            Msg m;
            m.peer = 0;
            m.type = EV_DISCONNECTED;
            m.data = r == 0 ? "connection timed out" : "connection refused";
            Post(std::move(m));
            g_run = false;
            return;
        }
        ConfigureSocket(s);
        {
            std::lock_guard<std::mutex> lk(g_mx);
            Peer p;
            p.id = 0;
            p.s = s;
            p.addr = host;
            p.lastRecv = GetTickCount();
            g_peers[0] = p;
        }
        Msg m;
        m.peer = 0;
        m.type = EV_CONNECTED;
        m.data = host;
        Post(std::move(m));
        ThreadMain();
    });
    return true;
}

void Shutdown() {
    g_run = false;
    if (g_thread.joinable()) g_thread.join();
    std::lock_guard<std::mutex> lk(g_mx);
    for (auto& kv : g_peers)
        if (kv.second.s != INVALID_SOCKET) closesocket(kv.second.s);
    g_peers.clear();
    g_inbox.clear();
    if (g_listen != INVALID_SOCKET) closesocket(g_listen);
    if (g_udp != INVALID_SOCKET) closesocket(g_udp);
    g_listen = g_udp = INVALID_SOCKET;
    g_isHost = false;
}

bool Active() { return g_run; }
bool IsHost() { return g_isHost; }

void Send(int peer, uint16_t type, const std::string& data) {
    std::lock_guard<std::mutex> lk(g_mx);
    auto it = g_peers.find(peer);
    if (it != g_peers.end()) Frame(it->second.out, type, data);
}

size_t MaxMessageBytes() { return kMaxFrame - 2; }

void SendAll(uint16_t type, const std::string& data) {
    std::lock_guard<std::mutex> lk(g_mx);
    for (auto& kv : g_peers) Frame(kv.second.out, type, data);
}

void Kick(int peer) {
    std::lock_guard<std::mutex> lk(g_mx);
    auto it = g_peers.find(peer);
    if (it != g_peers.end()) it->second.closing = true;
}

bool Pop(Msg& m) {
    std::lock_guard<std::mutex> lk(g_mx);
    if (g_inbox.empty()) return false;
    m = std::move(g_inbox.front());
    g_inbox.pop_front();
    return true;
}

std::string PeerAddress(int peer) {
    std::lock_guard<std::mutex> lk(g_mx);
    auto it = g_peers.find(peer);
    return it == g_peers.end() ? "" : it->second.addr;
}

int PeerPingMs(int peer) {
    std::lock_guard<std::mutex> lk(g_mx);
    auto it = g_peers.find(peer);
    return it == g_peers.end() ? -1 : it->second.pingMs;
}

size_t PendingOut(int peer) {
    std::lock_guard<std::mutex> lk(g_mx);
    auto it = g_peers.find(peer);
    return it == g_peers.end() ? 0 : it->second.out.size();
}

std::vector<LocalAddr> LocalAddresses() {
    std::vector<LocalAddr> out;
    ULONG len = 32 * 1024;
    std::vector<uint8_t> buf(len);
    auto* aa = (IP_ADAPTER_ADDRESSES*)buf.data();
    if (GetAdaptersAddresses(AF_INET, GAA_FLAG_SKIP_ANYCAST | GAA_FLAG_SKIP_MULTICAST | GAA_FLAG_SKIP_DNS_SERVER,
                             nullptr, aa, &len) != NO_ERROR)
        return out;
    for (auto* a = aa; a; a = a->Next) {
        if (a->OperStatus != IfOperStatusUp || a->IfType == IF_TYPE_SOFTWARE_LOOPBACK) continue;
        std::string desc = W2U(a->Description ? a->Description : L"");
        std::string name = W2U(a->FriendlyName ? a->FriendlyName : L"");
        for (auto* u = a->FirstUnicastAddress; u; u = u->Next) {
            char ip[64];
            inet_ntop(AF_INET, &((sockaddr_in*)u->Address.lpSockaddr)->sin_addr, ip, sizeof(ip));
            LocalAddr la;
            la.ip = ip;
            la.adapter = name;
            std::string all = desc + " " + name;
            if (all.find("Hamachi") != std::string::npos || la.ip.rfind("25.", 0) == 0)
                la.vpn = "Hamachi";
            else if (all.find("ZeroTier") != std::string::npos)
                la.vpn = "ZeroTier";
            else if (all.find("Radmin") != std::string::npos || la.ip.rfind("26.", 0) == 0)
                la.vpn = "Radmin VPN";
            out.push_back(la);
        }
    }
    return out;
}

void SetDiscoveryInfo(const std::string& info) {
    std::lock_guard<std::mutex> lk(g_mx);
    g_discoveryInfo = info;
}

std::vector<std::pair<std::string, std::string>> Discover(uint16_t port, int waitMs) {
    std::vector<std::pair<std::string, std::string>> found;
    InitWsa();
    SOCKET s = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (s == INVALID_SOCKET) return found;
    BOOL one = TRUE;
    setsockopt(s, SOL_SOCKET, SO_BROADCAST, (const char*)&one, sizeof(one));
    std::vector<uint32_t> targets = {INADDR_BROADCAST, htonl(INADDR_LOOPBACK)};
    // Directed broadcasts per adapter (VPN adapters often ignore the limited broadcast).
    ULONG len = 32 * 1024;
    std::vector<uint8_t> buf(len);
    auto* aa = (IP_ADAPTER_ADDRESSES*)buf.data();
    if (GetAdaptersAddresses(AF_INET, 0, nullptr, aa, &len) == NO_ERROR) {
        for (auto* a = aa; a; a = a->Next) {
            for (auto* u = a->FirstUnicastAddress; u; u = u->Next) {
                uint32_t ip = ntohl(((sockaddr_in*)u->Address.lpSockaddr)->sin_addr.s_addr);
                uint8_t plen = u->OnLinkPrefixLength;
                if (plen == 0 || plen >= 32) continue;
                uint32_t mask = plen ? (0xFFFFFFFFu << (32 - plen)) : 0;
                targets.push_back(htonl((ip & mask) | ~mask));
            }
        }
    }
    const char* q = "RTW3MP?";
    for (uint32_t t : targets) {
        sockaddr_in a = {};
        a.sin_family = AF_INET;
        a.sin_port = htons(port + 1);
        a.sin_addr.s_addr = t;
        sendto(s, q, 7, 0, (sockaddr*)&a, sizeof(a));
    }
    DWORD end = GetTickCount() + waitMs;
    std::set<std::string> seen;
    while ((int)(end - GetTickCount()) > 0) {
        fd_set rd;
        FD_ZERO(&rd);
        FD_SET(s, &rd);
        DWORD left = end - GetTickCount();
        timeval tv = {(long)(left / 1000), (long)((left % 1000) * 1000)};
        if (select(0, &rd, nullptr, nullptr, &tv) <= 0) break;
        char rb[600];
        sockaddr_in from;
        int fl = sizeof(from);
        int n = recvfrom(s, rb, sizeof(rb) - 1, 0, (sockaddr*)&from, &fl);
        if (n <= 7) continue;
        rb[n] = 0;
        if (strncmp(rb, "RTW3MP!", 7) != 0) continue;
        char ip[64];
        inet_ntop(AF_INET, &from.sin_addr, ip, sizeof(ip));
        if (seen.insert(ip).second) found.emplace_back(ip, std::string(rb + 7));
    }
    closesocket(s);
    return found;
}

}  // namespace net
