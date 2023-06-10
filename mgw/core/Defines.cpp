#include "Defines.h"
#include <sstream>

using namespace std;

//[R/L]O[n]     remote/local output channel
string getOutputName(bool remote, uint32_t chn) {
    ostringstream ostr;
    ostr << (remote ? "R" : "L") << "_OC" << chn;
    return ostr.str();
}
/**
规则：SN(8字节)_DEV/SVR_PHY/PLA/REC/PUB_SC[通道号]
SN: 设备的sn前8个字符
DEV：设备device
SVR: 服务器server
PHY: 物理输入
PLA: 拉流输入
REC: 录像文件输入
PUB: 推流输入
SC[通道号]：输入源通道号
例如：设备物理输入通道0：HDMN2DEV_DEV_PHY_SC0
*/
string getStreamId(const string &sn, StreamLocation location,
                InputType itype, uint32_t chn) {
    ostringstream oss;
    oss << sn.substr(0, 8) << "_" << (location==Location_Svr?"SVR":"DEV") << "_";
    switch (itype) {
        case InputType_Phy: oss << "PHY"; break;
        case InputType_Pla: oss << "PLA"; break;
        case InputType_Rec: oss << "REC"; break;
        case InputType_Pub: oss << "PUB"; break;
        default: break;
    }
    oss << "_SC" << chn;
    return oss.str();
}

void parseStreamId(const string &id, string &sn,
        StreamLocation &location, InputType &itype, uint32_t &chn) {
    if (id.size() < 20) {
        return;
    }
    string sub = id.substr(8, id.size() - 8);
    //sn
    sn = id.substr(0, SHORT_SN_LEN);
    //location
    size_t pos = sub.find('_');
    if (pos != string::npos) {
        string lo = sub.substr(pos+1, 3);
        if (0 == lo.compare("SVR")) {
            location = Location_Svr;
        } else if (0 == lo.compare("DEV")) {
            location = Location_Dev;
        }
        sub = sub.substr(4, sub.size() - 4);
    }
    //input type
    pos = sub.find('_');
    if (pos != string::npos) {
        string it = sub.substr(pos+1, 3);
        if (0 == it.compare("PHY")) {
            itype = InputType_Phy;
        } else if (0 == it.compare("PLA")) {
            itype = InputType_Pla;
        } else if (0 == it.compare("REC")) {
            itype = InputType_Rec;
        } else if (0 == it.compare("PUB")) {
            itype = InputType_Rec;
        }
        sub = sub.substr(4, sub.size() - 4);
    }
    //channel
    pos = sub.find("_SC");
    if (pos != string::npos) {
        string ch = sub.substr(pos+3);
        chn = atoi(ch.data());
    }
}

static inline int getChn(const string &name, const string &sub) {
    int chn = -1;
    size_t pos = name.rfind(sub);
    if (pos != string::npos) {
        chn = atoi(name.substr(pos+3).c_str());
    }
    return chn;
}

int getOutputChn(const string &name) {
    return getChn(name, "_OC");
}

int getSourceChn(const string &name) {
    return getChn(name, "_SC");
}

void urlAddPort(string &url, uint16_t port) {
    size_t pos = url.find("://");
    if (pos == string::npos)
        return;

    //sub_url = host[:port]/xxx/yyy
    string sub_url = url.substr(pos+3);
    size_t ip_pos = sub_url.find("]");
    if (ip_pos != string::npos) {
        //IPv6
        size_t port_pos = sub_url.find("]:");
        if (port_pos == string::npos) {
            ostringstream oss;
            oss << ":" << port;
            url.insert(pos+1, oss.str());
        }
    } else {
        //IPv4
        size_t port_pos = sub_url.find(":");
        if (port_pos == string::npos) {
            ostringstream oss;
            oss << ":" << port;
            url.insert(sub_url.find("/") + pos + 3, oss.str());
        }
    }
}