#ifndef __HVS_SRT_UTIL_H__
#define __HVS_SRT_UTIL_H__

#include <memory>
#include <sstream>
#include "srt/srt.h"

namespace MGW {

constexpr uint32_t def_maxbw = (6 * 1024 *1024) / 8 * 2;

class SrtUtil {
public:

    static void initialize_once();

    static int connect(const std::string &host, uint16_t port, bool async = true,
                    const std::string &if_ip = "", uint16_t local_port = 0,
                    uint16_t mss = 1500, const std::string &streamid = "");

    static int listen(const uint16_t port, const char *local_ip = "::", int back_log = 1024);

    static int accept(int fd, struct sockaddr* addr = nullptr, int* addrlen = nullptr);

    static int set_default_options(int fd);

    static std::string get_streamid(int fd);

    inline static void set_streamid(int fd, const std::string &streamid);
    inline static void set_nonblock(int fd, bool enable = true);
    inline static void set_maxbw(int fd, uint32_t maxbw = def_maxbw);  //6MByte
    inline static void set_mss(int fd, uint16_t mss = 1500);
    inline static void set_tsbpdmode(int fd, bool enable = false);
    inline static void set_tlpktdrop(int fd, bool enable = false);
    inline static void set_latency(int fd, uint16_t ms = 15000);
    inline static void set_payloadsize(int fd, uint16_t payloadsize = 1316);
    inline static void set_connect_timeout(int fd, int timeo_sec = 5);
    inline static void set_peer_idle_timeout(int fd, int timeo_sec = 5);
    inline static void set_sndbuf(int fd, int bufsize = -1);
    inline static void set_rcvbuf(int fd, int bufsize = -1);
    inline static void set_passphrase(int fd, const std::string &passphrase);
    inline static void set_pbkeylen(int fd, int pbkeylen);

};

}

#endif  //__HVS_SRT_UTIL_H__