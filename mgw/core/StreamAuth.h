#ifndef __MGW_STREAMAUTH_H__
#define __MGW_STREAMAUTH_H__

#include <string>
#include <memory>
namespace mediakit {

class StreamAuth : public std::enable_shared_from_this<StreamAuth>
{
public:
    using Ptr = std::shared_ptr<StreamAuth>;

    StreamAuth() : _push_time(0){}
    ~StreamAuth(){}
    void updateKey(const std::string &key) { _key = key; }

    ////////////////////////////////////////////////////////////////////////////////
    std::string getRtmpPushAddr(const std::string &host,
                        const std::string &dev, int time,
                        uint32_t chn = 0, uint16_t port = 1935);
    std::string getRtmpPullAddr(const std::string &host,
                        const std::string &dev, int time,
                        uint32_t chn = 0, uint16_t port = 1935);
    bool availableRtmpAddr(const std::string &url);

    /////////////////////////////////////////////////////////////////////////////////
    std::string getSrtPushAddr(const std::string &host, const std::string &dev,
                        int time, uint16_t port, uint32_t chn = 0);
    std::string getSrtPullAddr(const std::string &host, const std::string &dev,
                        int time, uint16_t port, uint32_t chn = 0);
    bool availableSrtAddr(const std::string &url);

    /////////////////////////////////////////////////////////////////////////////////
    std::string getRtspPushAddr(const std::string &host,
                const std::string &stream_id, int time, uint16_t port = 554);
    std::string getRtspPullAddr(const std::string &host,
                const std::string &stream_id, int time, uint16_t port = 554);
    bool availableRtspAddr(const std::string &url);

private:
    std::string getRtmpAddr(const std::string &host,
                const std::string &dev, const std::string &method,
                int time, uint32_t chn, uint16_t port);
    std::string getSrtAddr(const std::string &host,
                const std::string &dev, const std::string &method,
                int time, uint32_t chn, uint16_t port);
    std::string getRtspAddr(const std::string &host,
                const std::string &stream_id, const std::string &method,
                int time, uint16_t port);

    std::string md5Sum(const std::string &data);
private:
    int         _push_time;
    std::string _key;
};
}
#endif  //__MGW_STREAMAUTH_H__