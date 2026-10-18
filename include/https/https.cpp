#include "pch.hpp"
#include "https.hpp"
#include "server_data.hpp"

#include <openssl/err.h>
#include <cstdlib>

#ifdef _WIN32
    #include <winsock2.h>
    #include <ws2tcpip.h>
#else
    #include <csignal>
    #include <unistd.h>
    #include <arpa/inet.h>
    #include <netinet/in.h>
    #include <netinet/tcp.h> // @note TCP_DEFER_ACCEPT
    #include <sys/socket.h>

    #define SOCKET int
    #define INVALID_SOCKET	(SOCKET)(~0)
    #define SOCKET_ERROR	(-1)

#endif

/* cross-platform socket close */
static void cross_close(SOCKET fd)
{
    int ret =
#ifdef _WIN32
    closesocket(fd)
#else // @note unix
    close(fd)
#endif
    ; // ending of ret. hehe some silly code ;)

    if (ret == SOCKET_ERROR) printf("socket close error.\n");
}

/* cross-platform WSA error log, fallback on linux with strerror() */
static void cross_log(const std::string &message)
{
#ifdef _WIN32
    std::fprintf(stderr, "%s: %d\n", message.c_str(), WSAGetLastError());
#else // @note unix
    std::fprintf(stderr, "%s: %s\n", message.c_str(), strerror(errno));
#endif
}

void https::listener()
{
    OpenSSL_add_all_algorithms();
    SSL_load_error_strings();
    constexpr int enable = 1;

    /* https://docs.openssl.org/3.0/man3/SSL_CTX_new/#return-values */
    SSL_CTX *ctx = SSL_CTX_new(TLS_server_method());
    if (!ctx)
    {
        ERR_print_errors_fp(stderr);
    }

    /* https://docs.openssl.org/master/man3/SSL_CTX_use_certificate/#return-values */
    if (SSL_CTX_use_certificate_chain_file(ctx, "resources/ctx/server.crt") != 1 ||
        SSL_CTX_use_PrivateKey_file(ctx, "resources/ctx/server.key", SSL_FILETYPE_PEM)  != 1)
    {
        ERR_print_errors_fp(stderr);
    }

#ifdef SIGPIPE // @note unix
    std::signal(SIGPIPE, SIG_IGN);
#endif

    /* https://learn.microsoft.com/en-us/windows/win32/api/winsock2/nf-winsock2-socket */
    SOCKET socket = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (socket == INVALID_SOCKET)
    {
        cross_log("socket function failed");
    }

#ifdef SO_REUSEADDR
    setsockopt(socket, SOL_SOCKET, SO_REUSEADDR, (char*)&enable, sizeof(enable));
#endif
#ifdef TCP_DEFER_ACCEPT // @note unix
    setsockopt(socket, IPPROTO_TCP, TCP_DEFER_ACCEPT, (char*)&enable, sizeof(enable));
#endif
    struct sockaddr_in addr;
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    u_short https_port = 443; // @note local dev override: GUROTOPIA_HTTPS_PORT env (bind <1024 needs root)
    if (const char *port_env = std::getenv("GUROTOPIA_HTTPS_PORT")) https_port = static_cast<u_short>(std::stoi(port_env));
    addr.sin_port = htons(https_port);
    socklen_t addrlen = sizeof(addr);

    /* https://learn.microsoft.com/en-us/windows/win32/api/winsock/nf-winsock-bind */
    if (bind(socket, (struct sockaddr*)&addr, addrlen) == SOCKET_ERROR)
    {
        cross_log("could not bind socket");
    }

    const std::string Content =
        std::format(
            "server|{}\n"
            "port|{}\n"
            "type|{}\n"
            "type2|{}\n" // @todo remove for older clients
            "#maint|{}\n"
            "loginurl|{}\n"
            "meta|{}\n"
            "RTENDMARKERBS1001", 
            gServer_data.server, gServer_data.port, gServer_data.type, gServer_data.type2, gServer_data.maint, gServer_data.loginurl, gServer_data.meta
        );
    const std::string response =
        std::format(
            "HTTP/1.1 200 OK\r\n"
            "Content-Type: text/plain\r\n"
            "Content-Length: {}\r\n"
            "Connection: close\r\n\r\n"
            "{}",
            Content.size(), Content);

    /* https://learn.microsoft.com/en-us/windows/win32/api/winsock2/nf-winsock2-listen */
    if (listen(socket, SOMAXCONN) == SOCKET_ERROR)
    {
        cross_log("failed to listen on socket");
    }
    else std::printf("listening on %s:%hu\n", gServer_data.server.c_str(), gServer_data.port);
    
    while (true)
    {
        /* https://learn.microsoft.com/en-us/windows/win32/api/winsock2/nf-winsock2-accept */
        SOCKET fd = accept(socket, reinterpret_cast<sockaddr*>(&addr), &addrlen);
        if (fd == INVALID_SOCKET) continue;

        char peer_ip[INET_ADDRSTRLEN] = "?"; // @note debug: log client IP for diagnostics
        {
            struct sockaddr_in peer_addr{};
            socklen_t peer_len = sizeof(peer_addr);
            if (getpeername(fd, reinterpret_cast<sockaddr*>(&peer_addr), &peer_len) == 0)
                inet_ntop(AF_INET, &peer_addr.sin_addr, peer_ip, sizeof(peer_ip));
        }
        if (g_verbose) printf("[https] connection from %s\n", peer_ip);

        /* https://docs.openssl.org/3.0/man3/SSL_new/#return-values */
        SSL *ssl = SSL_new(ctx);
        if (!ssl || !SSL_up_ref(ssl)) continue;
        /* https://docs.openssl.org/3.0/man3/SSL_set_fd/#return-values */
        /* https://docs.openssl.org/3.0/man3/SSL_accept/#return-values */
        else if (SSL_set_fd(ssl, fd) != 1 || SSL_accept(ssl) <= 0) {
            //int ret = SSL_get_error(ssl, 3);
            ERR_print_errors_fp(stderr);
        }
        else {
            char buf[4096]; // @note read the whole request (client POST size varies by version)
            const int length{ sizeof(buf) };

            int rbytes = SSL_read(ssl, buf, length);
            if (rbytes <= 0) { if (g_verbose) ERR_print_errors_fp(stderr); } // @note eof here is usually just a client disconnect
            else
            {
                int line_end = 0; // @note log request first line: method + path (debug)
                while (line_end < rbytes && buf[line_end] != '\r' && buf[line_end] != '\n') ++line_end;
                if (g_verbose) printf("[https] request bytes: %d | %s | %.*s\n", rbytes, peer_ip, line_end, buf);

                /* @note item database download: the logon varlist points the client's CDN at
                   us, so the request for items.dat must return the EXACT bytes we hash and
                   advertise (items_dat_raw) - otherwise the client keeps re-updating. */
                const std::string request_line(buf, buf + line_end);
                if (request_line.find("items.dat") != std::string::npos && !items_dat_raw.empty())
                {
                    const std::string header = std::format(
                        "HTTP/1.1 200 OK\r\n"
                        "Content-Type: application/octet-stream\r\n"
                        "Content-Length: {}\r\n"
                        "Connection: close\r\n\r\n",
                        items_dat_raw.size());
                    printf("[https] served items.dat (%zu bytes, hash %u) to %s\n", items_dat_raw.size(), items_dat_hash, peer_ip);
                    if (SSL_write(ssl, header.c_str(), static_cast<int>(header.size())) > 0)
                        SSL_write(ssl, items_dat_raw.data(), static_cast<int>(items_dat_raw.size()));
                }
                else
                {
                    int wbytes = SSL_write(ssl, response.c_str(), response.size());
                    if (wbytes <= 0) ERR_print_errors_fp(stderr); // @todo support retryable
                }
            }
        }

        /* "It can also occur when not all data was read using SSL_read()." */
        if (SSL_shutdown(ssl) < 0) 
        {
            if (g_verbose) ERR_print_errors_fp(stderr);
        }
        SSL_free(ssl);
        if (shutdown(fd, 2) == SOCKET_ERROR) cross_log("failed to shutdown socket"); // @todo unsure if WSA can handle this.
        cross_close(fd);
    }
}

#ifndef _WIN32
    #undef SOCKET
#endif
