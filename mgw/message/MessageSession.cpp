#include "MessageSession.h"
#include "Common/config.h"

#include <chrono>

using namespace std;
using namespace toolkit;

namespace mediakit {

Session::Ptr MessageSessionCreator::operator()(const Parser &header,
                const HttpSession &parent, const Socket::Ptr &pSock) {
    InfoL << "websocket new connection url:" << header.Url();
    GET_CONFIG(string, device_path, WsSrv::kDevicePath);
    GET_CONFIG(string, u727_path, WsSrv::kU727Path);
    Session::Ptr session = nullptr;
    if (header.Url() == device_path) {
        session = std::make_shared<SessionTypeImp<DeviceSession> >(header, parent, pSock);
    } else if (header.Url() == u727_path) {
        session = std::make_shared<SessionTypeImp<U727Session> >(header, parent, pSock);
    }

    return session;
}

}
