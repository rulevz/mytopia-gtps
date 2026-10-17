/*
    gtproxy - transparent ENet capture proxy for the Gurotopia GTPS.

    Sits between a Growtopia client and the game server and relays ENet
    connections message-by-message (each side sees a normal peer), logging
    every application packet it sees: text messages (`protocol|..`,
    `action|..`) and tank packets - with variant RPC (varlist) decoding.

    Why: capture what the *retail client* sends for features the server does
    not implement yet (trade, redeem codes, clash, gacha, ...) so they can be
    implemented from real traffic. Format reference: docs/PROTOCOL.md.

    The client-facing host mirrors the game server's ENet profile
    (usingNewPacketForServer, crc32 checksum, range coder) because real
    clients expect exactly that; the server-facing host uses the same
    profile (it is what today's working setup negotiates with).

    Build:  make            (inside tools/gtproxy)
    Usage:  gtproxy [--listen 17091] [--backend 127.0.0.1:17092]
                    [--log-dir captures] [--channel-limit 2] [--no-new-packet]

    Capture-mode runbook: tools/gtproxy/README.md
*/

#include <enet/enet.h>

#include <algorithm>
#include <chrono>
#include <csignal>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <deque>
#include <filesystem>
#include <memory>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

namespace
{

struct options
{
    int         listen_port   = 17091;
    std::string backend_host  = "127.0.0.1";
    int         backend_port  = 17092;
    std::string log_dir       = "captures";
    int         channel_limit = 2;   // mirror of the game server's enet_host_create
    bool        new_packet    = true; // usingNewPacketForServer on the client-facing host
} gOpts;

struct queued
{
    std::vector<enet_uint8> bytes;
    enet_uint8  channel{};
    enet_uint32 flags{};
};

struct relay
{
    int         id{};
    std::string name;              // "ip:port" of the client
    ENetPeer   *client = nullptr;  // peer on the listen host (client side)
    ENetHost   *backend_host = nullptr;
    ENetPeer   *backend = nullptr; // peer on the backend host (server side)
    bool        backend_up = false;
    bool        finished = false;
    FILE       *log = nullptr;
    unsigned long long c2s_bytes = 0, s2c_bytes = 0;
    std::deque<queued> pending;    // client messages that arrived before backend connect
};

std::vector<std::unique_ptr<relay>> gRelays;
std::unordered_map<ENetPeer *, relay *> gClientToRelay;
std::unordered_map<ENetPeer *, relay *> gBackendToRelay;
ENetHost *gClientHost = nullptr;
int       gNextId = 1;
volatile std::sig_atomic_t gRunning = 1;

void on_signal(int) { gRunning = 0; }

std::string strf(const char *fmt, ...)
{
    char buf[8192];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    return std::string(buf);
}

std::string json_escape(const std::string &in)
{
    std::string out;
    out.reserve(in.size() + 8);
    for (unsigned char c : in)
    {
        if (c == '"' || c == '\\') { out += '\\'; out += static_cast<char>(c); }
        else if (c < 0x20)          out += '.';
        else                        out += static_cast<char>(c);
    }
    return out;
}

std::string hex_head(const enet_uint8 *d, size_t n, size_t cap = 96)
{
    std::string out;
    const size_t take = std::min(n, cap);
    for (size_t i = 0; i < take; ++i) out += strf("%02x ", d[i]);
    if (n > take) out += strf("(+%zu more)", n - take);
    else if (!out.empty()) out.pop_back();
    return out;
}

/* text message: 4-byte header, payload from byte 4.
   inbound (client->server) mirrors receive.cpp, which also drops the last byte. */
std::string decode_text(const enet_uint8 *d, size_t n, bool inbound)
{
    if (n <= 4) return strf("<short text packet: %zu bytes>", n);
    size_t end = n;
    if (inbound) --end;
    std::string s(reinterpret_cast<const char *>(d + 4), end - 4);
    while (!s.empty() && (s.back() == '\0' || s.back() == '\n' || s.back() == '\r')) s.pop_back();
    for (char &c : s) if (c == '\n' || c == '\r') c = '|'; // same view the server has
    if (s.size() > 1000) s = s.substr(0, 1000) + strf("...( +%zu more)", s.size() - 1000);
    return s;
}

/* variant list: [u8 count] then per slot [u8 index][u8 type][data] */
std::string decode_varlist(const enet_uint8 *d, size_t n)
{
    if (n < 1) return "<empty varlist>";
    size_t pos = 0;
    const unsigned count = d[pos++];
    std::string out = strf("varlist n=%u ", count);
    std::string args;
    for (unsigned i = 0; i < count && pos + 2 <= n; ++i)
    {
        const unsigned idx = d[pos++];
        const unsigned type = d[pos++];
        std::string value;
        auto need = [&](size_t k) { return pos + k <= n; };
        switch (type)
        {
            case 1: { if (!need(4)) return out + "..."; float f; memcpy(&f, d + pos, 4); pos += 4; value = strf("%.4g", f); break; }
            case 2: {
                if (!need(4)) return out + "...";
                uint32_t len; memcpy(&len, d + pos, 4); pos += 4;
                if (!need(len)) return out + "...";
                std::string s(reinterpret_cast<const char *>(d + pos), len); pos += len;
                while (!s.empty() && (s.back() == '\0' || s.back() == '\n')) s.pop_back();
                value = "\"" + s + "\"";
                break;
            }
            case 3: { if (!need(8)) return out + "..."; float x, y; memcpy(&x, d + pos, 4); memcpy(&y, d + pos + 4, 4); pos += 8; value = strf("(%.4g,%.4g)", x, y); break; }
            case 4: { if (!need(12)) return out + "..."; float x, y, z; memcpy(&x, d + pos, 4); memcpy(&y, d + pos + 4, 4); memcpy(&z, d + pos + 8, 4); pos += 12; value = strf("(%.4g,%.4g,%.4g)", x, y, z); break; }
            case 5: { if (!need(4)) return out + "..."; uint32_t u; memcpy(&u, d + pos, 4); pos += 4; value = strf("%u", u); break; }
            case 8: { if (!need(16)) return out + "..."; pos += 16; value = "<rect>"; break; }
            case 9: { if (!need(4)) return out + "..."; int32_t v; memcpy(&v, d + pos, 4); pos += 4; value = strf("%d", v); break; }
            default: return out + strf("[unknown type %u at idx %u]", type, idx);
        }
        if (idx == 0) out += value;                        // function name
        else { if (!args.empty()) args += ","; args += value; }
    }
    if (!args.empty()) out += "(" + args + ")";
    return out;
}

std::string decode_tank(const enet_uint8 *d, size_t n)
{
    if (n < 60) return strf("<short tank packet: %zu bytes>", n);
    int32_t i32[15]{};
    float   f32[15]{};
    for (int k = 0; k < 15; ++k) { memcpy(&i32[k], d + k * 4, 4); memcpy(&f32[k], d + k * 4, 4); }

    const size_t payload = n - 60;
    std::string out = strf("tank type=0x%02x netid=%d uid=%d state=0x%x id=%d pos=(%.1f,%.1f) speed=(%.1f,%.1f) punch=(%d,%d) payload=%zu",
        i32[1], i32[2], i32[3], i32[4], i32[6], f32[7], f32[8], f32[9], f32[10], i32[12], i32[13], payload);

    if (i32[1] == 0x01 && payload > 0) out += " | " + decode_varlist(d + 60, payload);
    else if (i32[1] == 0x04 && payload > 0) out += " | map blob";
    else if (i32[1] == 0x10 && payload > 0) out += " | items.dat blob";
    else if (i32[1] == 0x09 && payload > 0) out += " | inventory blob";
    return out;
}

void relay_log(relay &r, const std::string &line)
{
    if (!r.log) return;
    fputs(line.c_str(), r.log);
    fputc('\n', r.log);
    fflush(r.log);
}

double now_epoch()
{
    const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    return static_cast<double>(ms) / 1000.0;
}

std::string timestamp_hms()
{
    std::time_t t = std::time(nullptr);
    std::tm tm{}; localtime_r(&t, &tm);
    char buf[16]; std::strftime(buf, sizeof(buf), "%H:%M:%S", &tm);
    return buf;
}

std::string client_ip_port(const ENetPeer &p)
{
    char buf[64];
    snprintf(buf, sizeof(buf), "%u.%u.%u.%u:%u",
             p.address.host.v4[0], p.address.host.v4[1], p.address.host.v4[2], p.address.host.v4[3],
             p.address.port);
    return buf;
}

/* decode + console + jsonl for one application message */
void handle_message(relay &r, const char *dir, enet_uint8 channel, const enet_uint8 *data, size_t n)
{
    const bool inbound = (dir[0] == 'c');
    if (inbound) r.c2s_bytes += n; else r.s2c_bytes += n;

    const char *kind = "raw";
    std::string detail;
    if (n >= 1 && (data[0] == 2 || data[0] == 3)) { kind = "text"; detail = decode_text(data, n, inbound); }
    else if (n >= 1 && data[0] == 4)              { kind = "tank"; detail = decode_tank(data, n); }
    else                                          detail = strf("first_byte=%u", n ? data[0] : 0);

    printf("[%s] #%d %s %s len=%zu :: %s\n", timestamp_hms().c_str(), r.id, dir, kind, n, detail.c_str());
    fflush(stdout);

    relay_log(r, strf("{\"t\":%.3f,\"conn\":%d,\"dir\":\"%s\",\"ch\":%u,\"kind\":\"%s\",\"len\":%zu,\"hex\":\"%s\",\"detail\":\"%s\"}",
                      now_epoch(), r.id, dir, channel, kind, n,
                      hex_head(data, n).c_str(), json_escape(detail).c_str()));
}

void forward(relay &r, bool to_backend, const enet_uint8 *data, size_t n, enet_uint8 channel, enet_uint32 flags)
{
    ENetPeer *dst = to_backend ? r.backend : r.client;
    if (!dst) return;
    ENetPacket *pkt = enet_packet_create(data, n, flags & (ENET_PACKET_FLAG_RELIABLE | ENET_PACKET_FLAG_UNSEQUENCED));
    if (!pkt) return;
    if (enet_peer_send(dst, channel, pkt)) enet_packet_destroy(pkt);
}

relay *find(std::unordered_map<ENetPeer *, relay *> &map, ENetPeer *peer)
{
    const auto it = map.find(peer);
    return it == map.end() ? nullptr : it->second;
}

void finish_relay(relay &r, const char *why)
{
    if (r.finished) return;
    r.finished = true;
    if (r.backend) { gBackendToRelay.erase(r.backend); r.backend = nullptr; }
    if (r.client)  { gClientToRelay.erase(r.client);  r.client  = nullptr; }
    if (r.backend_host) { enet_host_destroy(r.backend_host); r.backend_host = nullptr; }

    relay_log(r, strf("{\"t\":%.3f,\"conn\":%d,\"event\":\"close\",\"why\":\"%s\",\"c2s_bytes\":%llu,\"s2c_bytes\":%llu}",
                      now_epoch(), r.id, why, r.c2s_bytes, r.s2c_bytes));
    printf("[%s] #%d closed (%s) c2s=%llu B s2c=%llu B\n",
           timestamp_hms().c_str(), r.id, why, r.c2s_bytes, r.s2c_bytes);
    fflush(stdout);
    if (r.log) { fclose(r.log); r.log = nullptr; }
}

relay *make_relay(ENetPeer *client_peer)
{
    auto owned = std::make_unique<relay>();
    relay *r = owned.get();
    r->id = gNextId++;
    r->client = client_peer;
    r->name = client_ip_port(*client_peer);

    std::error_code ec;
    std::filesystem::create_directories(gOpts.log_dir, ec);
    const std::time_t t = std::time(nullptr);
    std::tm tm{}; localtime_r(&t, &tm);
    char stamp[32]; std::strftime(stamp, sizeof(stamp), "%Y%m%d-%H%M%S", &tm);
    const std::string path = strf("%s/capture-%s-conn%d.jsonl", gOpts.log_dir.c_str(), stamp, r->id);
    r->log = fopen(path.c_str(), "a");
    if (!r->log) fprintf(stderr, "[gtproxy] warning: cannot open %s: %s\n", path.c_str(), strerror(errno));

    ENetAddress addr{};
    addr.type = ENET_ADDRESS_TYPE_IPV4;
    addr.port = static_cast<enet_uint16>(gOpts.backend_port);
    if (enet_address_set_host_ip(&addr, gOpts.backend_host.c_str()) != 0)
    {
        fprintf(stderr, "[gtproxy] cannot resolve backend host '%s'\n", gOpts.backend_host.c_str());
        finish_relay(*r, "bad-backend-address");
        return nullptr;
    }

    r->backend_host = enet_host_create(ENET_ADDRESS_TYPE_IPV4, nullptr, 8 /*client-side: one peer per relay*/, (size_t)gOpts.channel_limit, 0, 0);
    if (!r->backend_host) { finish_relay(*r, "backend-host-create-failed"); return nullptr; }
    /* the game server only answers peers in "new packet" mode - verified experimentally:
       crc+coder+this flag connects, everything else is silently dropped */
    r->backend_host->usingNewPacket = 1;
    r->backend_host->checksum = enet_crc32;
    enet_host_compress_with_range_coder(r->backend_host);

    r->backend = enet_host_connect(r->backend_host, &addr, client_peer->channelCount, 0);
    if (!r->backend) { finish_relay(*r, "backend-connect-failed"); return nullptr; }

    gClientToRelay[client_peer] = r;
    gBackendToRelay[r->backend] = r;

    relay_log(*r, strf("{\"t\":%.3f,\"conn\":%d,\"event\":\"open\",\"client\":\"%s\",\"backend\":\"%s:%d\"}",
                       now_epoch(), r->id, r->name.c_str(), gOpts.backend_host.c_str(), gOpts.backend_port));
    printf("[%s] #%d open: client %s -> backend %s:%d\n",
           timestamp_hms().c_str(), r->id, r->name.c_str(), gOpts.backend_host.c_str(), gOpts.backend_port);
    fflush(stdout);

    gRelays.push_back(std::move(owned));
    return r;
}

void client_event(ENetEvent &ev)
{
    switch (ev.type)
    {
        case ENET_EVENT_TYPE_CONNECT:
            make_relay(ev.peer);
            break;

        case ENET_EVENT_TYPE_RECEIVE:
        {
            relay *r = find(gClientToRelay, ev.peer);
            if (r && !r->finished)
            {
                handle_message(*r, "c2s", ev.channelID, ev.packet->data, ev.packet->dataLength);
                if (r->backend_up) forward(*r, true, ev.packet->data, ev.packet->dataLength, ev.channelID, ev.packet->flags);
                else r->pending.push_back(queued{std::vector<enet_uint8>(ev.packet->data, ev.packet->data + ev.packet->dataLength), ev.channelID, ev.packet->flags});
            }
            enet_packet_destroy(ev.packet);
            break;
        }

        case ENET_EVENT_TYPE_DISCONNECT:
        {
            relay *r = find(gClientToRelay, ev.peer);
            if (r)
            {
                gClientToRelay.erase(ev.peer);
                r->client = nullptr;
                if (r->backend) enet_peer_disconnect(r->backend, ev.data);
                if (!r->backend) finish_relay(*r, "client-disconnect");
            }
            break;
        }

        default: break;
    }
}

void backend_event(relay &r, ENetEvent &ev)
{
    switch (ev.type)
    {
        case ENET_EVENT_TYPE_CONNECT:
        {
            r.backend_up = true;
            printf("[%s] #%d backend connected (%zu buffered upstream)\n", timestamp_hms().c_str(), r.id, r.pending.size());
            fflush(stdout);
            for (queued &q : r.pending) forward(r, true, q.bytes.data(), q.bytes.size(), q.channel, q.flags);
            r.pending.clear();
            break;
        }

        case ENET_EVENT_TYPE_RECEIVE:
            if (!r.finished)
            {
                handle_message(r, "s2c", ev.channelID, ev.packet->data, ev.packet->dataLength);
                forward(r, false, ev.packet->data, ev.packet->dataLength, ev.channelID, ev.packet->flags);
            }
            enet_packet_destroy(ev.packet);
            break;

        case ENET_EVENT_TYPE_DISCONNECT:
            if (r.client) enet_peer_disconnect(r.client, ev.data);
            finish_relay(r, "server-disconnect");
            break;

        default: break;
    }
}

void parse_args(int argc, char **argv)
{
    for (int i = 1; i < argc; ++i)
    {
        const std::string a = argv[i];
        auto value = [&](const char *what) -> std::string
        {
            if (i + 1 >= argc) { fprintf(stderr, "gtproxy: %s needs a value\n", what); std::exit(2); }
            return argv[++i];
        };
        if (a == "--listen")        gOpts.listen_port = std::stoi(value("--listen"));
        else if (a == "--backend")
        {
            const std::string v = value("--backend");
            const auto colon = v.rfind(':');
            if (colon == std::string::npos) { fprintf(stderr, "gtproxy: --backend wants host:port\n"); std::exit(2); }
            gOpts.backend_host = v.substr(0, colon);
            gOpts.backend_port = std::stoi(v.substr(colon + 1));
        }
        else if (a == "--log-dir")       gOpts.log_dir = value("--log-dir");
        else if (a == "--channel-limit") gOpts.channel_limit = std::stoi(value("--channel-limit"));
        else if (a == "--no-new-packet") gOpts.new_packet = false;
        else if (a == "--help" || a == "-h")
        {
            printf("usage: gtproxy [--listen 17091] [--backend 127.0.0.1:17092] [--log-dir captures]\n"
                   "               [--channel-limit 2] [--no-new-packet]\n");
            std::exit(0);
        }
        else { fprintf(stderr, "gtproxy: unknown option '%s' (try --help)\n", a.c_str()); std::exit(2); }
    }
}

} // namespace

int main(int argc, char **argv)
{
    parse_args(argc, argv);
    setvbuf(stdout, nullptr, _IOLBF, 0);

    std::signal(SIGINT, on_signal);
    std::signal(SIGTERM, on_signal);

    if (enet_initialize() != 0) { fprintf(stderr, "gtproxy: enet_initialize failed\n"); return 1; }

    ENetAddress listen_addr{};
    listen_addr.type = ENET_ADDRESS_TYPE_IPV4;
    listen_addr.port = static_cast<enet_uint16>(gOpts.listen_port);

    gClientHost = enet_host_create(ENET_ADDRESS_TYPE_IPV4, &listen_addr, 64ull, (size_t)gOpts.channel_limit, 0u, 0u);
    if (!gClientHost) { fprintf(stderr, "gtproxy: cannot bind udp %d\n", gOpts.listen_port); enet_deinitialize(); return 1; }

    /* mirror the game server so real clients accept this endpoint */
    if (gOpts.new_packet) gClientHost->usingNewPacketForServer = 1;
    gClientHost->checksum = enet_crc32;
    enet_host_compress_with_range_coder(gClientHost);

    printf("gtproxy: udp %d -> %s:%d | new-packet=%s crc32=on range-coder=on | logs: %s/\n",
           gOpts.listen_port, gOpts.backend_host.c_str(), gOpts.backend_port,
           gOpts.new_packet ? "on" : "off", gOpts.log_dir.c_str());
    fflush(stdout);

    ENetEvent ev{};
    while (gRunning)
    {
        while (enet_host_service(gClientHost, &ev, 0) > 0) client_event(ev);

        for (size_t i = 0; i < gRelays.size(); ++i)
        {
            relay *r = gRelays[i].get();
            while (!r->finished && r->backend_host && enet_host_service(r->backend_host, &ev, 0) > 0)
                backend_event(*r, ev);
        }

        enet_host_flush(gClientHost);
        for (auto &r : gRelays) if (!r->finished && r->backend_host) enet_host_flush(r->backend_host);

        gRelays.erase(std::remove_if(gRelays.begin(), gRelays.end(),
                                     [](const std::unique_ptr<relay> &r) { return r->finished; }),
                      gRelays.end());

        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    for (auto &r : gRelays) finish_relay(*r, "shutdown");
    if (gClientHost) enet_host_destroy(gClientHost);
    enet_deinitialize();
    puts("gtproxy: stopped");
    return 0;
}
