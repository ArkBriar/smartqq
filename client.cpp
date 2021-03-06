#include "client.hpp"

#include <iostream>
#include <thread>
#include <fstream>
#include <cstdio>
#include <ctime>
#include <stdexcept>
using namespace smartqq;

int64_t SmartQQClient::MESSAGE_ID = 32690001L;
const int64_t SmartQQClient::Client_ID = 53999199L;

#ifdef SMARTQQ_DEBUG
#define log_debug(str) std::cerr << str << std::endl
#else
#define log_debug(str)
#endif

#define log(str) std::cout << str << std::endl

#ifdef SMARTQQ_DEBUG
#define log_err(str) std::cout << str << std::endl
#else
#define log_err(str) std::cerr << str << std::endl
#endif

using json = nlohmann::json;

/*@TESTED
 * login()
 * getQRCode()
 * verifyQRCode()
 * getPtwebqq()
 * getVfwebqq()
 * getUinAndPsessionid()
 * getGroupList()
 * pollMessage()
 * sendMessageToFriend()
 * getDiscussList()
 * getFriendList()
 * getFriendListWithCategory()
 * getRecentList()
 * getGroupInfo()
 * getDiscussInfo()
 * getAccountInfo()
 */

/*@NOT TESTED
 * sendMessageToDiscuss()
 * sendMessageToGroup()
 * getFriendInfo()
 * getQQById()
 * getFriendStatus()
 */

SmartQQClient::SmartQQClient() {}

void SmartQQClient::startPolling(MessageCallback& callback)
{
    std::thread poll(&SmartQQClient::pollThread, this, std::ref(callback));
    poll.detach();
}

void SmartQQClient::pollThread(MessageCallback& callback)
{
    mutex.lock();
    pollStarted = true;
    mutex.unlock();
    log("Poll thread start.");
    while(true) {
        mutex.lock();
        if (!pollStarted) {
            mutex.unlock();
            return;
        }
        try {
            pollMessage(callback);
        } catch (std::runtime_error e) {
            log_debug(e.what());
        } catch (const std::invalid_argument& e) {
            log_debug(e.what());
        }
        mutex.unlock();
        std::this_thread::sleep_for(std::chrono::milliseconds(300));
    }
}

void SmartQQClient::login()
{
    getQRCode();
    string url = verifyQRCode();
    getPtwebqq(url);
    cgiReport();
    getVfwebqq();
    afterVfwebqq();
    getUinAndPsessionid();

    getAccountInfo();
}

void SmartQQClient::getQRCode()
{
    log("Getting QRCode.");
    auto r = get(SMARTQQ_API_URL(GET_QR_CODE));
    log_debug(r.cookies.GetEncoded());
    cookies = r.cookies;
    log_debug(cookies["qrsig"]);
    fstream out("QR.png", ios::out);
    out << r.text;
    out.close();
    // execute a shell command with popen
    popen("fish -c \"open QR.png\"", "r");
    cout << "QR Code is in file QR.png. Please open and scan.\n";
}

string SmartQQClient::verifyQRCode()
{
    log("Waiting for scan.");

    while (true) {
        sleep(1);
        auto r = get(SMARTQQ_API_URL(VERIFY_QR_CODE));
        string result = r.text;
        if (r.status_code != 200) {
            log_debug(string("Http return ").append(to_string(r.status_code)));
        }
        if (result.find("成功") != string::npos) {
            log_debug(r.cookies.GetEncoded());
            log_debug(r.text);
            cookies.AddCookie(r.cookies);
            /*
             *cookies.DelCookie("0");
             *cookies.DelCookie("qrsig");
             *cookies.DelCookie("superkey");
             *cookies.DelCookie("supertoken");
             *cookies.DelCookie("superuin");
             */
            for (string::size_type i = 0, j = 0; i != string::npos; i = j + 3) {
                j = result.find("','", i);
                string content = result.substr(i, j - i);
                if(content.substr(0, 4) == "http")
                    return content;
            }
        } else if (result.find("已失效") != string::npos)  {
            log("QR Code's outdated. Try reacquire the QR Code.");
            getQRCode();
        }
    }

}

void SmartQQClient::getPtwebqq(const string& url)
{
    log("Getting ptwebqq.");

    list<string> params;
    params.push_back(url);
    auto r = get(SMARTQQ_API_URL(GET_PTWEBQQ), params);
    cookies.AddCookie(r.cookies);

    log_debug(r.status_code);
    log_debug(r.cookies.GetEncoded());
    /* Get ptwebqq from cookies */
    ptwebqq = cookies["ptwebqq"];
}

void SmartQQClient::cgiReport()
{
    log("Reporting to cgi.");
    list<string> params;
    params.push_back(std::to_string((int64_t)time(nullptr)).append("821"));
    auto r = get(SMARTQQ_API_URL(CGI_REPORT), params);

    log_debug(r.status_code);
}

void SmartQQClient::getVfwebqq()
{
    log("Getting vfwebqq.");

    list<string> params;
    params.push_back(ptwebqq);
    params.push_back(std::to_string((int64_t)std::time(nullptr)).append("172"));
    auto r = get(SMARTQQ_API_URL(GET_VFWEBQQ), params);
    cookies.AddCookie(r.cookies);
    log_debug(r.status_code);

    /* Get vfwebqq */
    vfwebqq = getJsonObjectResult(r)["vfwebqq"];
    log_debug(vfwebqq);
}

void SmartQQClient::afterVfwebqq()
{
    log("Something after getting vfwebqq.");

    auto r = get(SMARTQQ_API_URL(WSPEED_CGI));
    log_debug(r.status_code);
}

void SmartQQClient::getUinAndPsessionid()
{
    log("Getting uin and psessionid.");

    /* Post JSON data */
    json p;
    p["ptwebqq"] = ptwebqq;
    p["clientid"] = Client_ID;
    p["psessionid"] = "";
    p["status"] = "online";

    auto r = post(SMARTQQ_API_URL(GET_UIN_AND_PSESSIONID), p);
    auto jres = getJsonObjectResult(r);
    psessionid = jres["psessionid"];
    uin = jres["uin"].get<int64_t>();
}

list<Group> SmartQQClient::getGroupList()
{
    log("Getting group list.");

    list<Group> groups;

    json j;
    j["vfwebqq"] = vfwebqq;
    j["hash"] = hash();

    auto r = post(SMARTQQ_API_URL(GET_GROUP_LIST), j);
    auto jres = getJsonObjectResult(r);

    /*@Parse JSON result into list
     * */
    auto _groups = jres["gnamelist"].get<list<json>>();
    for (auto i : _groups) {
        groups.push_back(Group::parseJson(i));
    }
    return groups;
}

// throw runtime_error
void SmartQQClient::pollMessage(MessageCallback &callback)
{
    log_debug("Polling message.");

    json j;
    j["ptwebqq"] = ptwebqq;
    j["clientid"] = Client_ID;
    j["psessionid"] = psessionid;
    j["key"] = "";

    auto r = post(SMARTQQ_API_URL(POLL_MESSAGE), j);
    auto jres = getJsonObjectResult(r);
    /*@Parse JSON result into list
     * */
    auto array = jres.get<vector<json>>();
    for (auto message : array) {
        auto type = message["poll_type"].get<string>();
        if("message" == type) {
            callback.onMessage(Message(message["value"]));
        } else if("group_message" == type) {
            callback.onGroupMessage(GroupMessage(message["value"]));
        } else if("discu_message" == type) {
            callback.onDiscussMessage(DiscussMessage(message["value"]));
        }
    }
}

void SmartQQClient::sendMessageToGroup(int64_t groupId, const string &msg)
{
    log(string("Sending message to group ").append(to_string(groupId))
            .append("."));
    log(msg);

    json j;
    j["group_uin"] = groupId;
    j["content"] = json({msg, {"font", Font::DEFAULT_FONT.toString()}}).dump();
    j["face"] = 573;
    j["clientid"] = Client_ID;
    j["msg_id"]  = MESSAGE_ID ++;
    j["psessionid"] = psessionid;

    auto r = post(SMARTQQ_API_URL(SEND_MESSAGE_TO_GROUP), j);
    checkSendMsgResult(r);
}

void SmartQQClient::sendMessageToDiscuss(int discussId, const string& msg)
{
    log(string("Sending message to discuss ").append(to_string(discussId))
            .append("."));
    log(msg);

    json j;
    j["did"] = discussId;
    j["content"] = json({msg, {"font", Font::DEFAULT_FONT.toString()}}).dump();
    j["face"] = 573;
    j["clientid"] = Client_ID;
    j["msg_id"]  = MESSAGE_ID ++;
    j["psessionid"] = psessionid;

    auto r = post(SMARTQQ_API_URL(SEND_MESSAGE_TO_DISCUSS), j);
    checkSendMsgResult(r);
}

void SmartQQClient::sendMessageToFriend(int64_t friendId, const string& msg)
{
    log(string("Sending message to friend ").append(to_string(friendId))
            .append("."));
    log(msg);

    json j;
    j["to"] = friendId;
    j["content"] = json({msg, {"font", Font::DEFAULT_FONT.toString()}}).dump();
    j["face"] = 573;
    j["clientid"] = Client_ID;
    j["msg_id"] = MESSAGE_ID ++;
    j["psessionid"] = psessionid;

    auto r = post(SMARTQQ_API_URL(SEND_MESSAGE_TO_FRIEND), j);
    checkSendMsgResult(r);
}

list<Discuss> SmartQQClient::getDiscussList()
{
    log("Getting discuss list.");
    list<Discuss> discusses;

    auto r = get(SMARTQQ_API_URL(GET_DISCUSS_LIST), list<string>({psessionid, vfwebqq}));
    auto jres = getJsonObjectResult(r);
    /*@Parse result into list
     * */
    auto _diss = jres["dnamelist"].get<list<json>>();
    for (auto i : _diss) {
        discusses.push_back(i);
    }
    return discusses;
}


list<Category> SmartQQClient::getFriendListWithCategory()
{
    log("Getting friend list with category.");
    list<Category> categories;

    json j;
    j["vfwebqq"] = vfwebqq;
    j["hash"] = hash();

    auto r = post(SMARTQQ_API_URL(GET_FRIEND_LIST), j);
    auto jres = getJsonObjectResult(r);
    /*@Parse JSON result into list
     * */
    map<int64_t, Friend> friendMap = parseFriendMap(jres);
    auto _categs = jres["categories"].get<list<json>>();
    map<int64_t, Category> categoryMap;
    categoryMap.insert({0, Category::defaultCategory()});
    for (auto i : _categs) {
        Category c(i);
        categoryMap.insert({c.index, c});
    }

    auto _frids = jres["friends"].get<list<json>>();
    for (auto i : _frids) {
        auto f = friendMap.at(i["uin"].get<int64_t>());
        categoryMap[i["categories"].get<int64_t>()].friends
            .push_back(f);
    }

    for (auto c : categoryMap) {
        categories.push_back(std::move(c.second));
    }

    return categories;
}

list<Category> SmartQQClient::getFriendListWithCategory(std::map<int64_t, Friend>& friendMap_)
{
    log("Getting friend list with category.");
    list<Category> categories;

    json j;
    j["vfwebqq"] = vfwebqq;
    j["hash"] = hash();

    auto r = post(SMARTQQ_API_URL(GET_FRIEND_LIST), j);
    auto jres = getJsonObjectResult(r);
    /*@Parse JSON result into list
     * */
    friendMap_ = parseFriendMap(jres);
    // friendMap is constant
    auto friendMap = const_cast<std::map<int64_t, Friend>&>(friendMap_);
    auto _categs = jres["categories"].get<list<json>>();
    map<int64_t, Category> categoryMap;
    categoryMap.insert({0, Category::defaultCategory()});
    for (auto i : _categs) {
        Category c(i);
        categoryMap.insert({c.index, c});
    }

    auto _frids = jres["friends"].get<list<json>>();
    for (auto i : _frids) {
        auto f = friendMap.at(i["uin"].get<int64_t>());
        categoryMap[i["categories"].get<int64_t>()].friends
            .push_back(f);
    }

    for (auto c : categoryMap) {
        categories.push_back(std::move(c.second));
    }

    return categories;
}

list<Friend> SmartQQClient::getFriendList()
{
    log("Getting friend list.");
    list<Friend> friends;

    json j;
    j["vfwebqq"] = vfwebqq;
    j["hash"] = hash();

    auto r = post(SMARTQQ_API_URL(GET_FRIEND_LIST), j);
    auto jres = getJsonObjectResult(r);
    /*@Parse JSON result into list
     * */
    for (auto i : parseFriendMap(jres)) {
        friends.push_back(i.second);
    }
    return friends;
}

UserInfo SmartQQClient::getAccountInfo()
{
    log("Getting account info.");

    list<string> params;
    params.push_back(std::to_string((int64_t)std::time(nullptr)).append("012"));
    auto r = get(SMARTQQ_API_URL(GET_ACCOUNT_INFO), params);
    auto jres = getJsonObjectResult(r);
    /*@Parse JSON result into info
     * */

    UserInfo uinfo(jres);
    return uinfo;
}

UserInfo SmartQQClient::getFriendInfo(int64_t friendId)
{
    log("Getting friend info.");

    auto r = get(SMARTQQ_API_URL(GET_FRIEND_INFO), list<string>({to_string(friendId), vfwebqq, psessionid}));
    auto jres = getJsonObjectResult(r);
    /*@Parse JSON result into info
     * */

    UserInfo uinfo(jres);
    return uinfo;
}

list<Recent> SmartQQClient::getRecentList()
{
    log("Getting recent list.");
    list<Recent> recents;

    json j;
    j["vfwebqq"] = vfwebqq;
    j["clientid"] = Client_ID;
    j["psessionid"] = "";

    auto r = post(SMARTQQ_API_URL(GET_RECENT_LIST), j);
    auto jres = getJsonObjectResult(r);
    /*@Parse JSON result into list
     * */

    auto _recs = jres.get<list<json>>();
    for (auto i : _recs) {
        recents.push_back(i);
    }
    return recents;
}

int64_t SmartQQClient::getQQById(int64_t friendId)
{
    log(string("Getting qq by id ").append(to_string(friendId))
            .append("."));

    auto r = get(SMARTQQ_API_URL(GET_QQ_BY_ID), list<string>({to_string(friendId), vfwebqq}));
    int64_t qq = getJsonObjectResult(r)["account"].get<int64_t>();
    return qq;
}

list<FriendStatus> SmartQQClient::getFriendStatus()
{
    log("Getting friend status.");
    list<FriendStatus> fses;

    auto r = get(SMARTQQ_API_URL(GET_FRIEND_STATUS), list<string>({vfwebqq, psessionid}));
    auto jres = getJsonObjectResult(r);
    /*@Parse JSON result into list
     * */
    auto _frdstss = jres.get<list<json>>();

    for (auto i : _frdstss) {
        fses.push_back(i);
    }

    return fses;
}

GroupInfo SmartQQClient::getGroupInfo(int64_t groupCode)
{
    log_debug(string("Getting group info of ").append(to_string(groupCode))
            .append("."));

    auto r = get(SMARTQQ_API_URL(GET_GROUP_INFO), list<string>({to_string(groupCode), vfwebqq}));
    auto jres = getJsonObjectResult(r);
    /*@Parse JSON result into info
     * */
    GroupInfo ginfo(jres["ginfo"]);

    map<int64_t, GroupUser> groupUserMap;
    for (auto i : jres["minfo"].get<list<json>>()) {
        GroupUser gu(i);
        groupUserMap.insert({gu.uin, gu});
    }

    auto stats = jres["stats"].get<list<json>>();
    for (auto i : stats) {
        GroupUser& gu = groupUserMap[i["uin"]];
        gu.clientType = i["client_type"];
        gu.status = i["stat"];
    }

    if (jres.find("cards") != jres.end()) {
        auto cards = jres["cards"].get<list<json>>();
        for (auto i : cards) {
            groupUserMap[i["muin"]].card = i["card"];
        }
    }

    auto vipinfos = jres["vipinfo"].get<list<json>>();
    for (auto i : vipinfos) {
        GroupUser& gu = groupUserMap[i["u"]];
        gu.vip = i["is_vip"].get<int>() == 1;
        gu.vipLevel = i["vip_level"];
    }

    for (auto i : groupUserMap) {
        ginfo.users.push_back(i.second);
    }

    return ginfo;
}

DiscussInfo SmartQQClient::getDiscussInfo(int64_t discussId)
{
    log_debug(string("Getting group info of ").append(to_string(discussId))
            .append("."));
    auto r = get(SMARTQQ_API_URL(GET_DISCUSS_INFO), list<string>({to_string(discussId), vfwebqq, psessionid}));
    auto jres = getJsonObjectResult(r);
    /*@Parse JSON result into info
     * */
    DiscussInfo dinfo(jres["info"]);

    auto minfo = jres["mem_info"].get<vector<json>>();
    map<int64_t, DiscussUser> discussUserMap;
    for (auto i : minfo) {
        DiscussUser du(i);
        discussUserMap.insert({du.uin, du});
    }

    auto stats = jres["mem_status"].get<vector<json>>();
    for (auto i : stats) {
        DiscussUser& du = discussUserMap[i["uin"]];
        du.clientType = i["client_type"];
        du.status = i["status"];
    }

    for (auto i : discussUserMap) {
        dinfo.users.push_back(i.second);
    }

    return dinfo;
}

map<int64_t, Friend> SmartQQClient::parseFriendMap(const json& result)
{
    map<int64_t, Friend> friendMap;

    /*@TODO
     * */
    auto info = result["info"].get<vector<json>>();
    for (auto i : info) {
        Friend f;
        f.userId = i["uin"];
        f.nickname = i["nick"];
        friendMap.insert({f.userId, f});
    }

    auto marknames = result["marknames"].get<vector<json>>();
    for (auto i : marknames) {
        friendMap[i["uin"]].markname = i["markname"];
    }

    auto vipinfo = result["vipinfo"].get<vector<json>>();
    for (auto i : vipinfo) {
        Friend& f = friendMap[i["u"]];
        f.vip = i["is_vip"].get<int>() == 1;
        f.vipLevel = i["vip_level"];
    }

    return friendMap;
}

cpr::Response SmartQQClient::get(const ApiUrl& url)
{
    log_debug(string("HTTP/GET ").append(url.getUrl()));
    session.SetUrl(url.getUrl());
    session.SetHeader({{"User-Agent", ApiUrl::USER_AGENT}, {"Referer", url.getReferer()}, {"Connection", "keep-alive"}});
    session.SetCookies(cookies);

    return session.Get();
}

cpr::Response SmartQQClient::get(const ApiUrl& url, const list<string>& params)
{
    log_debug(string("HTTP/GET ").append(url.buildUrl(params)));
    session.SetUrl(url.buildUrl(params));
    session.SetHeader({{"User-Agent", ApiUrl::USER_AGENT}, {"Referer", url.getReferer()}, {"Connection", "keep-alive"}});
    session.SetCookies(cookies);

    return session.Get();
}

cpr::Response SmartQQClient::get(const ApiUrl& url, const map<string, string>& params)
{
    log_debug(string("HTTP/GET ").append(url.getUrl()));
    session.SetUrl(url.getUrl());
    session.SetHeader({{"User-Agent", ApiUrl::USER_AGENT}, {"Referer", url.getReferer()}, {"Connection", "keep-alive"}});
    session.SetCookies(cookies);
    cpr::Parameters _cpr_params;
    for (auto pair : params) {
        _cpr_params.AddParameter({pair.first, pair.second});
    }
    session.SetParameters(std::move(_cpr_params));

    return session.Get();
}

cpr::Response SmartQQClient::post(const ApiUrl& url)
{
    return post(url, json());
}

cpr::Response SmartQQClient::post(const ApiUrl& url, const json& jparam)
{
    log_debug(string("HTTP/POST ").append(url.getUrl()));
    log_debug(jparam.dump());
    session.SetUrl(url.getUrl());
    session.SetHeader({{"User-Agent", ApiUrl::USER_AGENT}, {"Referer", url.getReferer()}, {"Origin", url.getOrigin()}, {"Connection", "keep-alive"}, {"Content-Type", "application/x-www-form-urlencoded"}, {"Accept", "*/*"}});
    session.SetCookies(cookies);

    cpr::Payload _cpr_form({{"r", jparam.dump()}});
    log_debug(_cpr_form.content);
    session.SetPayload(std::move(_cpr_form));

    return session.Post();
}

void SmartQQClient::checkSendMsgResult(const cpr::Response& r)
{
    if (r.status_code != 200) {
        log_err(string("Send failed. Http status code's ").append(to_string(r.status_code)));
    }

    json j = json::parse(r.text);
    log_debug(j.dump());
    if(j.find("retcode") != j.end()) {
        int retcode = j["retcode"].get<int>();
        if(retcode != 0) {
            log_err(string("Send failed. Api return code's ").append(to_string(retcode)));
        }
        return;
    }
    int err_code = j["errCode"].get<int>();
    if (err_code == 0) {
        log("Send ok.");
    } else {
        log_err(string("Send failed. Api return code's ").append(to_string(err_code)));
    }
}

string SmartQQClient::hash()
{
    return hash(uin, ptwebqq);
}

string SmartQQClient::hash(int64_t x, string K)
{
    int N[4];
    N[0] = N[1] = N[2] = N[3] = 0;
    for (string::size_type T = 0; T < K.length(); T ++) {
        N[T & 0x3] ^= K.at(T);
    }
    string U[2] = {"EC", "OK"};
    int64_t V[4];
    V[0] = x >> 24 & 255 ^ U[0].at(0);
    V[1] = x >> 16 & 255 ^ U[0].at(1);
    V[2] = x >> 8 & 255 ^ U[1].at(0);
    V[3] = x & 255 ^ U[1].at(1);

    int64_t U1[8];

    for (int T = 0; T < 8; T ++) {
        U1[T] = T % 2 == 0 ? N[T >> 1] : V[T >> 1];
    }

    string N1[16] = {"0", "1", "2", "3", "4", "5", "6", "7", "8", "9", "A", "B", "C", "D", "E", "F"};
    string V1 = "";
    for (int64_t i : U1) {
        V1 = V1.append(N1[(int)((i >> 4) & 0xf)])
            .append(N1[(int)(i & 15)]);
    }
    return V1;
}

void SmartQQClient::sleep(int64_t seconds)
{
    std::this_thread::sleep_for(std::chrono::seconds(seconds));
}

void SmartQQClient::close()
{
    mutex.lock();
    pollStarted = false;
    mutex.unlock();
}

json SmartQQClient::getResponseJson(const cpr::Response& r)
{
    if (r.status_code != 200) {
        throw std::runtime_error(string("Request failed. Http return code's ").append(to_string(r.status_code)));
    }
    json ret = json::parse(r.text);
    /*@TODO
     * */
    log_debug("Text of response is:");
    log_debug(ret);
    int ret_code = ret["retcode"].get<int>();
    if(ret_code != 0) {
        if(ret_code == 103) {
            log_err(string("Request failed. Api return code's ")
                    .append(to_string(ret_code)
                    .append(". Please check http:w.qq.com and log out.")));
        } else {
            throw std::runtime_error(string("Request failed. Api return code's ").append(to_string(ret_code)));
        }
    }
    return ret;
}

json::array_t SmartQQClient::getJsonArrayResult(const cpr::Response& r)
{
    return getResponseJson(r)["result"].get<json::array_t>();
}

json SmartQQClient::getJsonObjectResult(const cpr::Response& r)
{
    auto j = getResponseJson(r);
    if(j.find("result") != j.end()) {
        return j["result"];
    } else {
        throw runtime_error("Receive an invalid response. ERR:NO RESULT");
    }
}

