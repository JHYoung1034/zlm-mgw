#ifndef __MGW_CORE_U727_H__
#define __MGW_CORE_U727_H__

#include "Defines.h"
// #include "DomainSockPusher.h"

#include <cinttypes>
#include <memory>
#include <string>
#include <atomic>
#include <functional>
#include <unordered_set>
#include <unordered_map>

namespace mediakit {

//每个机器上只有一个u727
class U727 : public std::enable_shared_from_this<U727> {
public:
    using Ptr = std::shared_ptr<U727>;

    U727() {}
    ~U727() {}

    void setRtmpPort(uint16_t port) { _port_map.emplace("rtmp", port); }
    uint16_t getRtmpPort() { return _port_map["rtmp"]; }
    void setSrtPort(uint16_t port) { _port_map.emplace("srt", port); }
    uint16_t getSrtPort() { return _port_map["srt"]; }
    void setHttpPort(uint16_t port) { _port_map.emplace("http", port); }
    uint16_t getHttpPort() { return _port_map["http"]; }
    void setBlackDevice(const std::string &device) { _black_list.emplace(device); }

    void startStream(const std::string &id, uint32_t delay_ms, StreamType src_type,
            const std::string &src, StreamType dest_type, const std::string &dest);
    void stopStream(const std::string &id);

private:
    std::unordered_set<std::string> _black_list;
    std::unordered_map<std::string, uint16_t> _port_map;
    // std::unordered_map<std::string, DomainSockPusher::Ptr> _sock_cli_map;
};

}
#endif  //__MGW_CORE_U727_H__