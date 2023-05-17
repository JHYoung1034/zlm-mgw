#include "DmspPusher.h"
#include "Common/Parser.h"

using namespace std;
using namespace toolkit;

namespace mediakit {

 DmspPusher::DmspPusher(const EventPoller::Ptr &poller,const DmspMediaSource::Ptr &src) : DmsClient(poller) {
    DebugL << "src: " << src.get() << ", bool: " << src.operator bool();
    _publish_src = src;
 }

DmspPusher::~DmspPusher() {
   teardown();
   DebugL << endl;
}

void DmspPusher::onPublishResult_l(const toolkit::SockException &ex, bool handshake_done) {
    DebugL << ex.what();
    if (ex.getErrCode() == Err_shutdown) {
        //主动shutdown的，不触发回调
        return;
    }
    if (!handshake_done) {
        //播放结果回调
        _publish_timer.reset();
        onPublishResult(ex);
    } else {
        //播放成功后异常断开回调
        onShutdown(ex);
    }

    if (ex) {
        shutdown(SockException(Err_shutdown,"teardown"));
    }
}

// dmsp://path
void DmspPusher::publish(const std::string &svr_path) {
    teardown();

    string path = FindField(svr_path.data(), "dmsp://", NULL);
    if (!path.size()) {
        onPublishResult_l(SockException(Err_other, "dmsp path 非法"), false);
        return;
    }
    DebugL << "dmsp pubilsh path: " << path;

    weak_ptr<DmspPusher> weakSelf = dynamic_pointer_cast<DmspPusher>(shared_from_this());
    float publishTimeOutSec = (*this)[Client::kTimeoutMS].as<int>() / 1000.0f;
    _publish_timer.reset(new Timer(publishTimeOutSec, [weakSelf]() {
        auto strongSelf = weakSelf.lock();
        if (!strongSelf) {
            return false;
        }
        strongSelf->onPublishResult_l(SockException(Err_timeout, "publish dmsp timeout"), false);
        return false;
    }, getPoller()));

    startConnect(path);
}

void DmspPusher::teardown() {
    if (alive()) {
        _publish_timer.reset();
        reset();
        shutdown(SockException(Err_shutdown, "teardown"));
    }
}

void DmspPusher::onRecv(const toolkit::Buffer::Ptr &buf) {
    try {
        onParseDmsp(buf->data(), buf->size());
    } catch (exception &e) {
        SockException ex(Err_other, e.what());
        onPublishResult_l(ex, !_publish_timer);
    }
}

void DmspPusher::onConnect(const toolkit::SockException &err) {
    if (err) {
        onPublishResult_l(err, false);
        return;
    }

    //连接成功了，设置源，后续发送音视频数据DmspPacket
    auto src = _publish_src.lock();
    if (!src) {
        throw std::runtime_error("the media source was released");
    }

    src->pause(false);
    _dmsp_reader = src->getRing()->attach(getPoller());
    weak_ptr<DmspPusher> weak_self = dynamic_pointer_cast<DmspPusher>(shared_from_this());
    _dmsp_reader->setReadCB([weak_self](const DmspMediaSource::RingDataType &pkt) {
        auto strong_self = weak_self.lock();
        if (!strong_self) {
            return;
        }

        size_t i = 0;
        auto size = pkt->size();
        strong_self->setSendFlushFlag(false);
        pkt->for_each([&](const DmspPacket::Ptr &dmsp) {
            if (++i == size) {
                strong_self->setSendFlushFlag(true);
            }
            strong_self->sendDmsp(dmsp);
        });
    });

    _dmsp_reader->setDetachCB([weak_self](){
        auto strong_self = weak_self.lock();
        if (strong_self) {
            strong_self->onPublishResult_l(SockException(Err_other, "媒体源被释放"), !strong_self->_publish_timer);
        }
    });

    onPublishResult_l(SockException(Err_success, "success"), false);
}

void DmspPusher::onErr(const toolkit::SockException &ex) {
    //定时器_pPublishTimer为空后表明握手结束了
    onPublishResult_l(ex, !_publish_timer);
}

void DmspPusher::onWholeDmspPacket(DmspPacket::Ptr packet_data) {

}

void DmspPusher::onMetaChange(DmspTack tack) {

}

}