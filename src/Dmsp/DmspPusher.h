#ifndef __MGW_CORE_DMSP_PUSHER_H__
#define __MGW_CORE_DMSP_PUSHER_H__

#include "DmspSplitter.h"
#include "DmspMediaSource.h"
#include "Network/DmsClient.h"
#include "Pusher/PusherBase.h"

namespace mediakit {

class DmspPusher : public DmspSplitter, public toolkit::DmsClient, public PusherBase {
public:
    using Ptr = std::shared_ptr<DmspPusher>;

    DmspPusher(const toolkit::EventPoller::Ptr &poller, const DmspMediaSource::Ptr &src);
    ~DmspPusher() override;

    void publish(const std::string &svr_path) override;
    void teardown() override;
    void setNetif(const std::string &netif, uint16_t mss) override {}

protected:
    //for DmsClient override
    void onRecv(const toolkit::Buffer::Ptr &buf) override;
    void onConnect(const toolkit::SockException &err) override;
    void onErr(const toolkit::SockException &ex) override;

    //for DmspSplitter override
    void onWholeDmspPacket(DmspPacket::Ptr packet) override;
    void onMetaChange(DmspTack tack) override;
    void onSendRawData(toolkit::Buffer::Ptr buffer) override {
        send(std::move(buffer));
    }

private:
    void onPublishResult_l(const toolkit::SockException &ex, bool handshake_done);

private:

    //推流超时定时器
    std::shared_ptr<toolkit::Timer> _publish_timer;
    std::weak_ptr<DmspMediaSource> _publish_src;
    DmspMediaSource::RingType::RingReader::Ptr _dmsp_reader;
};

using DmspPusherImp = PusherImp<DmspPusher, PusherBase>;

}
#endif  //__MGW_CORE_DMSP_PUSHER_H__