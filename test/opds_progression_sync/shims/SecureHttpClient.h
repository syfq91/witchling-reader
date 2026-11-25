#pragma once
#include <deque>
#include <map>
#include <string>
#include <vector>

namespace crosspoint {

struct MockHttpResponse {
  int status = 200;
  std::string body;
};

struct RecordedHttpRequest {
  std::string method;
  std::string url;
  std::string body;
  std::string basicUser;
  std::string basicPass;
  std::map<std::string, std::string> headers;
};

struct MockHttpTransport {
  std::deque<MockHttpResponse> responses;
  std::vector<RecordedHttpRequest> requests;

  void reset() {
    responses.clear();
    requests.clear();
  }
};

inline MockHttpTransport gMockHttp;

class SecureHttpClient {
 public:
  void setTimeout(int) {}
  void setCACert(const char*) {}
  void setAllowCertificateDateErrors(bool) {}
  void setAllowInsecureFallback(bool) {}

  void setBasicAuth(const std::string& user, const std::string& pass) {
    basicUser_ = user;
    basicPass_ = pass;
  }

  void addHeader(const std::string& name, const std::string& value) { headers_[name] = value; }

  void clearHeaders() {
    headers_.clear();
    basicUser_.clear();
    basicPass_.clear();
  }

  int GET(const std::string& url) { return dispatch("GET", url, ""); }
  int PUT(const std::string& url, const std::string& body) { return dispatch("PUT", url, body); }

  const std::string& getBody() const { return lastBody_; }

 private:
  int dispatch(const std::string& method, const std::string& url, const std::string& body) {
    gMockHttp.requests.push_back({method, url, body, basicUser_, basicPass_, headers_});
    if (gMockHttp.responses.empty()) {
      lastBody_.clear();
      return 500;
    }
    const auto resp = gMockHttp.responses.front();
    gMockHttp.responses.pop_front();
    lastBody_ = resp.body;
    return resp.status;
  }

  std::string basicUser_;
  std::string basicPass_;
  std::map<std::string, std::string> headers_;
  std::string lastBody_;
};

}  // namespace crosspoint
