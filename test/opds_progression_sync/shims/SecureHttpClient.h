#pragma once
#include <deque>
#include <functional>
#include <map>
#include <string>
#include <utility>
#include <vector>

namespace crosspoint {

struct MockHttpResponse {
  int status = 200;
  std::string body;
  std::vector<std::pair<std::string, std::string>> headers = {};
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

  using HeaderSink = std::function<void(const std::string& name, const std::string& value)>;
  void setResponseHeaderSink(HeaderSink sink) { headerSink_ = std::move(sink); }
  void clearResponseHeaderSink() { headerSink_ = nullptr; }

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
    if (headerSink_) {
      for (const auto& [name, value] : resp.headers) {
        headerSink_(name, value);
      }
    }
    lastBody_ = resp.body;
    return resp.status;
  }

  std::string basicUser_;
  std::string basicPass_;
  std::map<std::string, std::string> headers_;
  std::string lastBody_;
  HeaderSink headerSink_;
};

}  // namespace crosspoint
