#include "MessageSession.h"
#include "Common/config.h"

#include <chrono>

using namespace std;
using namespace toolkit;
using namespace mediakit;

namespace MGW {

Session::Ptr MessageSessionCreator::operator()(const Parser &header,
                const HttpSession &parent, const Socket::Ptr &pSock) {
    InfoL << "websocket new connection url:" << header.Url();
    GET_CONFIG(string, device_path, WsSrv::kDevicePath);
    GET_CONFIG(string, u727_path, WsSrv::kU727Path);
    Session::Ptr session = nullptr;

    //统一去掉尾缀'/'
    string req_path = header.Url();
    if (!req_path.empty() && req_path.back() == '/') {
        req_path.pop_back();
    }

    if (req_path == device_path) {
        session = make_shared<SessionTypeImp<DeviceSession> >(header, parent, pSock);
    } else if (req_path == u727_path) {
        session = make_shared<SessionTypeImp<U727Session> >(header, parent, pSock);
    }

    return session;
}

}
