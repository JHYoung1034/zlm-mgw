#include "EventProcess.h"
#include "UcastDevice.h"
#include "U727.h"
#include "Util/NoticeCenter.h"
#include "Util/onceToken.h"
#include "Common/config.h"

using namespace std;
using namespace toolkit;


namespace mediakit {

EventProcess::EventProcess() {
}

EventProcess::~EventProcess() {
    exit();
}

//从url中提取设备sn
string EventProcess::getSnByStreamId(const string &streamid) {
    size_t pos = streamid.find("_");
    if (pos == string::npos) {
        return string();
    }

    return streamid.substr(0, pos);
}

string EventProcess::getFullUrl(const string &schema, const string &url, uint16_t port) {
    if (schema.empty() || url.empty()) {
        return string();
    }

    if (!port) {
        if (0 == strncasecmp(schema.data(), "rtmp", 4)) {
            port = mINI::Instance()[Rtmp::kPort];
        } else if (0 == strncasecmp(schema.data(), "srt", 3)) {
            GET_CONFIG(uint16_t, srt_port, SrtSrv::kPort);
            port = srt_port;
        } else if (0 == strncasecmp(schema.data(), "rtsp", 4)) {
            port = mINI::Instance()[Rtsp::kPort];
        }
    }

    string full_url = url;
    urlAddPort(full_url, port);
    return full_url;
}

void EventProcess::run() {
    //添加监听
    static onceToken token([this] {
        //推拉流地址鉴权, 如果是本地设备，也应该通过DeviceHelper::findDevice(sn) 查找到
        //TODO: 本地设备的输入源名字也应该是[sn]_C[n]
        auto auth_token = [this](bool publish, const MediaInfo &args, string &sn)->string {
            string err("");
            sn = getSnByStreamId(args._streamid);
            auto device = DeviceHelper::findDevice(sn);
            auto u727 = U727::getU727().lock();
            bool access = false;
            if (device) {
                //url需要加上端口号，校验完整的url
                access = device->availableAddr(getFullUrl(args._schema, args._full_url, args._port));
            } else if (u727) {
                access = u727->availableRtspAddr(getFullUrl(args._schema, args._full_url, args._port));
            } else {
                err = string("illegal stream: ").append(args._streamid);
                return err;
            }
            err = access ? "" : "Access denied.";
            return err;
        };

        //收到rtsp/rtmp推流事件广播，通过该事件控制推流鉴权
        NoticeCenter::Instance().addListener(this,Broadcast::kBroadcastMediaPublish,[auth_token](BroadcastMediaPublishArgs){
            string sn;
            string err = auth_token(true, args, sn);
            ProtocolOption option;
            if (err.empty() && !sn.empty()) {
                //如果错误码是空的，说明鉴权成功
                auto device = DeviceHelper::findDevice(sn);
                device->setAlive(false, true);
                option = device->getEnableOption();
            }
            invoker(err, option);
        });

        //播放rtsp/rtmp/http-flv/hls事件广播，通过该事件控制播放鉴权
        NoticeCenter::Instance().addListener(this,Broadcast::kBroadcastMediaPlayed,[auth_token](BroadcastMediaPlayedArgs){
            string sn;
            string err = auth_token(false, args, sn);
            invoker(err);
        });

        //rtsp/rtmp/http-flv会话流量汇报事件
        //查找设备实例，回调MessageSession给设备端发送流量变化
        //根据ucast的业务，流量要实时统计，并且要尽可能实时上报，所以流量应该提供查询接口，在onManager()中查询并上报
        //这里先不处理会话结束的流量汇报事件,仅设置设备推流流不活跃标记
        NoticeCenter::Instance().addListener(this,Broadcast::kBroadcastFlowReport,[this](BroadcastFlowReportArgs){
            if (!isPlayer) {
                string sn = getSnByStreamId(args._streamid);
                auto device = DeviceHelper::findDevice(sn);
                if (device) {
                    device->setAlive(false, false);
                }
            }
        });

        //未找到流时会广播该事件，请在监听该事件后去拉流或其他方式产生流，这样就能按需拉流了
        //查找设备实例，回调MessageSession给设备端发送请求推流消息
        NoticeCenter::Instance().addListener(this,Broadcast::kBroadcastNotFoundStream,[this](BroadcastNotFoundStreamArgs){
            string sn = getSnByStreamId(args._streamid);
            auto device = DeviceHelper::findDevice(sn);
            if (device) {
                device->doOnNotFoundStream(args._full_url);
            } else {
                //尝试去拉流或者从文件输入，但是需要知道是设备请求的还是u727请求的，因为拉流器实例需要他们各自管理
                // WarnL << "Not found the device: " << sn << ", url:" << args._full_url;
            }
        });

        //观看人数变化时通知，后台显示当前多少人在观看
        //查找设备实例，回调MessageSession给设备端发送观看人数变化消息
        NoticeCenter::Instance().addListener(this,Broadcast::kBroadcastPlayersChanged,[this](BroadcastPlayersChangedArgs){
            string sn = getSnByStreamId(sender.getId());
            auto device = DeviceHelper::findDevice(sn);
            if (device) {
                device->doOnPlayersChange(true, sender.totalReaderCount());
            }
        });

        //无人消费这个流的时候，通知设备
        //查找设备实例，回调MessageSession给设备端发送请求断开推流消息
        NoticeCenter::Instance().addListener(this,Broadcast::kBroadcastStreamNoneReader,[this](BroadcastStreamNoneReaderArgs) {
            string sn = getSnByStreamId(sender.getId());
            auto device = DeviceHelper::findDevice(sn);
            if (device) {
                device->doOnNoReader();
            }
        });
    });
}

void EventProcess::exit() {
    //删除监听
    NoticeCenter::Instance().delListener(this, Broadcast::kBroadcastMediaPublish);
    NoticeCenter::Instance().delListener(this, Broadcast::kBroadcastMediaPlayed);
    NoticeCenter::Instance().delListener(this, Broadcast::kBroadcastFlowReport);
    NoticeCenter::Instance().delListener(this, Broadcast::kBroadcastNotFoundStream);
    NoticeCenter::Instance().delListener(this, Broadcast::kBroadcastPlayersChanged);
    NoticeCenter::Instance().delListener(this, Broadcast::kBroadcastStreamNoneReader);
}

}