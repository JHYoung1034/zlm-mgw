#include "SrtUtil.h"
#include "Util/onceToken.h"
#include "Util/logger.h"
#include "Util/uv_errno.h"
#include "Network/sockutil.h"

#define SET_SRT_OPT_STR(srtfd, optname, buf, size)                              \
    if (srt_setsockflag(srtfd, optname, buf, size) == SRT_ERROR) {              \
        std::stringstream ss;                                                   \
        ss << "srtfd=" << srtfd << ",set " << #optname                          \
           << " failed,err=" << srt_getlasterror_str();                         \
        WarnL << ss.str();                                                        \
    } 

#define SET_SRT_OPT(srtfd, optname, val)                                        \
    if (srt_setsockflag(srtfd, optname, &val, sizeof(val)) == SRT_ERROR) {      \
        std::stringstream ss;                                                   \
        ss << "srtfd=" << srtfd << ",set " << #optname << "=" << val            \
           << " failed,err=" << srt_getlasterror_str();                         \
        WarnL << ss.str();                                                        \
    } 

#define GET_SRT_OPT(srtfd, optname, val)                                        \
    do {                                                                        \
        int size = sizeof(val);                                                 \
        if (srt_getsockflag(srtfd, optname, &val, &size) == SRT_ERROR) {        \
            std::stringstream ss;                                               \
            ss << "srtfd=" << srtfd << ",get " << #optname                      \
               << " failed,err=" << srt_getlasterror_str();                     \
            WarnL << ss.str();                                                  \
        }                                                                       \
    } while (0)

using namespace std;
using namespace toolkit;

namespace MGW {

void SrtUtil::initialize_once() {
    static onceToken s = [](){
        srt_startup();
        srt_setloghandler(nullptr, [](void* opaque, int level, const char* file, int line, const char* area, const char* message) {
            switch (level) {
                case srt_logging::LogLevel::debug:
                    DebugL << file << ":" << line << "(" << area << ") # " << message;
                    break;
                case srt_logging::LogLevel::note:
                    TraceL << file << ":" << line << "(" << area << ") # " << message;
                    break;
                case srt_logging::LogLevel::warning:
                    WarnL << file << ":" << line << "(" << area << ") # " << message;
                    break;
                case srt_logging::LogLevel::error:
                case srt_logging::LogLevel::fatal:
                    ErrorL << file << ":" << line << "(" << area << ") # " << message;
                    break;
                default:
                    TraceL << file << ":" << line << "(" << area << ") # " << message;
                    break;
            }
        });
    };
}

int SrtUtil::connect(const std::string &host, uint16_t port, bool async,
                    const std::string &if_ip, uint16_t local_port,
                    uint16_t mss, const std::string &streamid) {
    if (host.empty()) {
        WarnL << "Empty host!";
        return -1;
    }
    //get dns
    sockaddr_storage addr;
    if (!SockUtil::getDomainIP(host.data(), port, addr, AF_UNSPEC, SOCK_DGRAM, IPPROTO_UDP)) {
        //dns 解析失败
        return -1;
    }
    //2.create socket
    int sockfd = srt_create_socket();
    if (sockfd < 0) {
        WarnL << "Create srt socket failed: " << host;
        return -1;
    }

    //3.set default options
    set_streamid(sockfd, streamid);
    set_nonblock(sockfd, async);
    set_maxbw(sockfd);
    set_mss(sockfd, mss);
    set_tsbpdmode(sockfd, true);
    set_tlpktdrop(sockfd, true);
    set_payloadsize(sockfd);
    set_connect_timeout(sockfd);
    set_peer_idle_timeout(sockfd);
    if (!if_ip.empty()) {
        SET_SRT_OPT_STR(sockfd, SRTO_BINDTODEVICE, if_ip.data(), if_ip.size());
    }

    //4.connect to server
    if (srt_connect(sockfd, (sockaddr *)&addr, SockUtil::get_sock_len((sockaddr*)&addr)) != 0) {
        //连接出错了
        WarnL << "Srt connect failed: " << srt_getlasterror_str();
        srt_close(sockfd);
        return -1;
    }
    SRT_SOCKSTATUS status = srt_getsockstate(sockfd);
    if (async && (status == SRTS_CONNECTED || status == SRTS_CONNECTING)) {
        return sockfd;
    }
    WarnL << "Connect socket to " << host << " " << port << " failed: " << srt_getlasterror_str();
    srt_close(sockfd);
    return -1;
}

int SrtUtil::listen(const uint16_t port, const char *local_ip, int back_log) {
    //1. create socket
    int sockfd = srt_create_socket();
    if (sockfd < 0) {
        WarnL << "Create srt socket failed";
        return -1;
    }
    //2. set options
    int enable = true;
    SET_SRT_OPT(sockfd, SRTO_REUSEADDR, enable);
    set_nonblock(sockfd, true);

    //3. bind to sock
    struct sockaddr_in addr;
    bzero(&addr, sizeof(addr));
    addr.sin_family = AF_UNSPEC;
    addr.sin_port = htons(port);
    addr.sin_addr.s_addr = INADDR_ANY;
    if (srt_bind(sockfd, (sockaddr*)&addr, sizeof(addr)) != 0) {
        WarnL << "srt bind to: " << local_ip << ", port: " << port << ", failed!";
        srt_close(sockfd);
        return -1;
    }

    //4. listen
    if (srt_listen(sockfd, back_log) != 0) {
        WarnL << "srt listen failed";
        srt_close(sockfd);
        return -1;
    }

    return sockfd;
}

int SrtUtil::accept(int fd, struct sockaddr* addr, int* addrlen) {

}

int SrtUtil::set_default_options(int fd) {

}

std::string SrtUtil:: get_streamid(int fd) {

}

//------------------------------------------------------------------------------------------
//set srt options
inline void SrtUtil::set_streamid(int fd, const std::string &streamid) {
    SET_SRT_OPT_STR(fd, SRTO_STREAMID, streamid.data(), streamid.size());
}

inline void SrtUtil::set_nonblock(int fd, bool enable) {
    SET_SRT_OPT(fd, SRTO_SNDSYN, enable);
    SET_SRT_OPT(fd, SRTO_RCVSYN, enable);
}

inline void SrtUtil::set_maxbw(int fd, uint32_t maxbw) {
    SET_SRT_OPT(fd, SRTO_MAXBW, maxbw);
}

inline void SrtUtil::set_mss(int fd, uint16_t mss) {
    SET_SRT_OPT(fd, SRTO_MSS, mss);
}

inline void SrtUtil::set_tsbpdmode(int fd, bool enable) {
    SET_SRT_OPT(fd, SRTO_TSBPDMODE, enable);
}

inline void SrtUtil::set_tlpktdrop(int fd, bool enable) {
    SET_SRT_OPT(fd, SRTO_TLPKTDROP, enable);
}

inline void SrtUtil::set_latency(int fd, uint16_t ms) {
    SET_SRT_OPT(fd, SRTO_LATENCY, ms);
    SET_SRT_OPT(fd, SRTO_RCVLATENCY, ms);
    SET_SRT_OPT(fd, SRTO_PEERLATENCY, ms);
}

inline void SrtUtil::set_payloadsize(int fd, uint16_t payloadsize) {
    SET_SRT_OPT(fd, SRTO_PAYLOADSIZE, payloadsize);
}

inline void SrtUtil::set_connect_timeout(int fd, int timeo_sec) {
    SET_SRT_OPT(fd, SRTO_CONNTIMEO, timeo_sec);
}

inline void SrtUtil::set_peer_idle_timeout(int fd, int timeo_sec) {
    SET_SRT_OPT(fd, SRTO_PEERIDLETIMEO, timeo_sec);
}

inline void SrtUtil::set_sndbuf(int fd, int bufsize) {
    SET_SRT_OPT(fd, SRTO_SNDBUF, bufsize);
}

inline void SrtUtil::set_rcvbuf(int fd, int bufsize) {
    SET_SRT_OPT(fd, SRTO_RCVBUF, bufsize);
}

inline void SrtUtil::set_passphrase(int fd, const std::string &passphrase) {
    SET_SRT_OPT_STR(fd, SRTO_PASSPHRASE, passphrase.data(), passphrase.size());
}

inline void SrtUtil::set_pbkeylen(int fd, int pbkeylen) {
    SET_SRT_OPT(fd, SRTO_PBKEYLEN, pbkeylen);
}

}