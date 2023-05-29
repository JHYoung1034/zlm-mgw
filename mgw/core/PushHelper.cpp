#include "PushHelper.h"

using namespace std;
using namespace toolkit;

namespace mediakit {

PushHelper::PushHelper(const string &name, int channel) : _retry_count(-1) {
    _info.channel = channel == -1 ? getOutputChn(name) : channel;
    _info.status = ChannelStatus_Idle;
    _info.id = name;
}

PushHelper::~PushHelper() {
    _timer.reset();
    if (_pusher && _info.status != ChannelStatus_Idle) {
        _pusher->teardown();
    }
}

void PushHelper::start(const string &url, onStatusChanged on_status_changed, int max_retry,
                        const MediaSource::Ptr &src, const string &netif, uint16_t mtu) {
    _retry_count = max_retry;
    _info.url = url;
    _wek_src = src;
    _netif = netif;
    _mtu = mtu;
    _on_status_changed = on_status_changed;
    _pusher = make_shared<MediaPusher>(src);
    _pusher->setNetif(netif, mtu);

    std::weak_ptr<PushHelper> weak_self = shared_from_this();
    //初始化值为-1，如果第一次推流就失败，不需要尝试重推
    std::shared_ptr<int> failed_cnt(new int(-1));

    auto do_retry = [weak_self, failed_cnt](const SockException &ex) {
        auto strong_self = weak_self.lock();
        if (!strong_self) {
            return;
        }
        //推流中断，会触发这个回调，如果不是主动停止的
        //此时应该尝试重推，并且回调到外层给设备发送正在重连通知
        auto src = strong_self->_wek_src.lock();
        //1. (*failed_cnt) >= 0说明有成功过推流
        //2. src这个源仍然是存在的
        //3. *failed_cnt < strong_self->_retry_count 实际重推次数还没有超过最大重推次数
        //4. strong_self->_retry_count 小于0，说明没有设置最大重推次数，可以无限次重推
        if ((*failed_cnt) >= 0 && src && (*failed_cnt < strong_self->_retry_count || strong_self->_retry_count < 0)) {
            strong_self->rePublish(strong_self->_info.url, (*failed_cnt)++);
        } else {
            //没办法重推了，需要关闭这个推流，通知到外部，外部删除这个推流
            strong_self->_info.stopTime = ::time(NULL);
            strong_self->_info.status = ChannelStatus_Idle;
            if (strong_self->_on_status_changed) {
                strong_self->_on_status_changed(strong_self->_info.id,
                        strong_self->_info.status, strong_self->_info.startTime, ex);
            }
        }
    };

    _pusher->setOnPublished([weak_self, failed_cnt, do_retry](const SockException &ex) {
        //publish的结果，可能是失败，可能是成功
        auto strong_self = weak_self.lock();
        if (!strong_self) {
            return;
        }

        //真的发生了异常，记录下来
        if (ex.operator bool()) {
            strong_self->_info.stopTime = ::time(NULL);
            strong_self->_info.status = ChannelStatus_Idle;
            do_retry(ex);
        } else {
            *failed_cnt = 0;
            strong_self->_info.startTime = ::time(NULL);
            strong_self->_info.status = ChannelStatus_Pushing;
            InfoL << "Publish " << strong_self->_info.url << " success";
        }

        //返回推流结果，可能是失败的，可能是成功的
        if (strong_self->_on_status_changed) {
            strong_self->_on_status_changed(strong_self->_info.id,
                        strong_self->_info.status, strong_self->_info.startTime, ex);
        }
    });

    _pusher->setOnShutdown(do_retry);
    //调用publish的时候，会根据url，判断是何种协议，在调用createPusher创建指定协议的pusher
    _pusher->publish(url);
}

//对于正在推流的通道，如果有新的推流，释放原来的推流，建立一个新的推流
void PushHelper::restart(const string &url, onStatusChanged on_status_changed,
                        const MediaSource::Ptr &src, const string &netif, uint16_t mtu) {
    if (_pusher && _info.status != ChannelStatus_Idle) {
        _pusher->teardown();
    }
    start(url, on_status_changed, _retry_count, src, netif, mtu);
}

void PushHelper::rePublish(const std::string &url, int failed_cnt) {
    //单次最多延时1分钟，最少延时2秒，最多延时 retry_cnt 次数
    auto delay = MAX(2 * 1000, MIN(failed_cnt * 3000, 60 * 1000));
    weak_ptr<PushHelper> weak_self = shared_from_this();
    _info.status = ChannelStatus_RePushing;
    _info.total_retry++;
    if (_on_status_changed) {
        _on_status_changed(_info.id, _info.status, _info.startTime, SockException());
    }
    _timer = std::make_shared<Timer>(delay / 1000.0f, [weak_self, url, failed_cnt]() {
        //推流失败次数越多，则延时越长
        auto strong_self = weak_self.lock();
        if (!strong_self) {
            return false;
        }
        WarnL << "推流重试[" << failed_cnt << "]:" << url;
        strong_self->_pusher->publish(url);
        return false;
    }, _pusher->getPoller());
}

}