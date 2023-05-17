#include "U727.h"
#include "Common/config.h"
#include "UcastDevice.h"
#include "openssl/sha.h"

using namespace std;
using namespace toolkit;

/**< u727实例必须和u727消息会话绑定在同一个线程上，在其他地方调用u727实例，
 *   比如说设备调用发送通知的时候，必须切换到u727绑定的线程再发送通知，
 *   这样就可以确保线程安全。
 * 
 *   这里存在一个问题，如果u727会话频繁断开又重连上来，就可能会导致u727频繁的
 *   切换绑定到不同的线程，假如上一次绑定的线程调用任务还没结束，就会出现问题。
 *   所以，u727实例还是需要跟随u727会话，如果会话断开的时候，不允许其他实例
 *   调用u727。
 * 
 *   基于上述情况，设备实例有多个，u727实例只有一个，而且是应该跟随u727会话的。
 *   所以，设备实例很难强关联到u727实例，很难直接调用。所以处理这种情况下设备实例
 *   对u727实例的单向调用选择消息通知方式。而u727调用设备实例可以直接查找后调用。
*/

namespace mediakit {

static inline string gen_access_token(void *addr)
{
    unsigned char sha1[21] = {};
    string result;

    time_t now;
    char *datetime;
    time(&now);
    datetime = ctime(&now);

    int pointer = (int)(*(int*)addr);
    char buf[20] = {};
    sprintf(buf, "0x%x", pointer);

    string src = string(buf) + string(datetime);

    SHA1((const unsigned char*)src.data(), src.size(), sha1);
    int i = 0;
    char tmp[3] = {0};
    for (i = 0; i < 20; i++) {
        sprintf(tmp, "%02x", sha1[i]);
        result.append(tmp);
    }

    return result;
}

weak_ptr<U727> U727::_static_u727;

U727::U727() : _start_time(::time(NULL)) {
    _auth.updateKey(gen_access_token((void*)this));
}

void U727::u727Ready() {
    _static_u727 = shared_from_this();
}

string U727::getRtspPushAddr(const string &stream_id) {
    GET_CONFIG(uint32_t, available_time, Mgw::kUrlValidityPeriodSec);
    GET_CONFIG(std::string, host, Mgw::kOutHostIP);
    if (host.empty()) {
        WarnL << "host is empty!";
        return "";
    }
    uint16_t rtsp_port = mINI::Instance()[Rtmp::kPort];
    return _auth.getRtspPushAddr(host, stream_id, _start_time+available_time);
}

string U727::getRtspPullAddr(const string &stream_id) {
    GET_CONFIG(uint32_t, available_time, Mgw::kUrlValidityPeriodSec);
    GET_CONFIG(std::string, host, Mgw::kOutHostIP);
    if (host.empty()) {
        WarnL << "host is empty!";
        return "";
    }
    uint16_t rtsp_port = mINI::Instance()[Rtmp::kPort];
    return _auth.getRtspPullAddr(host, stream_id, _start_time+available_time);
}

bool U727::availableRtspAddr(const string &url) {
    return _auth.availableRtspAddr(url);
}

PushHelper::Ptr U727::pusher(const string &stream_id) {
    auto &pusher = _pusher_map[stream_id];
    if (!pusher) {
        pusher = make_shared<PushHelper>(0);
    }
    return pusher;
}

PlayHelper::Ptr U727::player(const string &stream_id) {
    auto &player = _player_map[stream_id];
    if (!player) {
        player = make_shared<PlayHelper>(stream_id, 10);
    }
    return player;
}

void U727::releasePusher(const string &stream_id) {
    if (_pusher_map.find(stream_id) != _pusher_map.end()) {
        _pusher_map.erase(stream_id);
    }
}

void U727::releasePlayer(const string &stream_id) {
    if (_player_map.find(stream_id) != _player_map.end()) {
        _player_map.erase(stream_id);
    }
}

void U727::stopStream(const std::string &stream_id) {
    releasePusher(stream_id);
}

}