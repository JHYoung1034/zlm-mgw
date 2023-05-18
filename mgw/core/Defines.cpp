#include "Defines.h"
#include <sstream>

using namespace std;

//[R/L]O[n]     remote/local output channel
string getOutputName(bool remote, uint32_t chn) {
    ostringstream ostr;
    ostr << (remote ? "R" : "L") << "O" << chn;
    return ostr.str();
}
//[R/L]S[n]     remote/local source channel
string getSourceName(bool remote, uint32_t chn) {
    ostringstream ostr;
    ostr << (remote ? "R" : "L") << "S" << chn;
    return ostr.str();
}

static inline int getChn(const string &name, const string &sub) {
    int chn = -1;
    size_t pos = name.rfind(sub);
    if (pos != string::npos) {
        chn = atoi(name.substr(pos+1).c_str());
    }
    return chn;
}

int getOutputChn(const string &name) {
    return getChn(name, "O");
}

int getSourceChn(const string &name) {
    return getChn(name, "S");
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