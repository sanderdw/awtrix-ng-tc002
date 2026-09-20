#pragma once

#include <atomic>
#include <cctype>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <thread>
#include <utility>

#include "Tc002Hardware.h"
#include "core/script/HttpBodyFilter.h"
#include "core/script/ModbusTcp.h"
#include "core/script/ScriptServices.h"
#include "sim/compat/WiFiClient.h"
#include "sim/vendor/httplib.h"
#include "system/Log.h"

namespace awtrix {

// TC002 script HTTP worker: like the simulator's, one detached thread per request, but with TLS
// against the bundled CA store and the body filter fed while streaming so a large response is
// never held in memory.
class Tc002ScriptHttp : public script::IScriptHttp {
 public:
  using ResultFn = std::function<void(script::HttpResult)>;

  void begin(ResultFn onResult) { onResult_ = std::move(onResult); }

  bool request(const script::HttpRequest& req) override {
    if (!onResult_) return false;
    if (pending_.load() >= kMaxPending) return false;

    // Modbus TCP reads share the request queue, as in the simulator.
    if (script::modbus::isUrl(req.url)) {
      script::modbus::Read read;
      if (req.method != "GET" || !script::modbus::parse(req.url, read)) return false;
      pending_.fetch_add(1);
      std::thread([this, read, id = req.id] {
        script::HttpResult result;
        result.id = id;
        WiFiClient client;
        if (client.connect(read.host.c_str(), read.port)) {
          result = script::modbus::exchange(client, read, id, [] {
            return std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now().time_since_epoch()).count();
          }, [] { std::this_thread::sleep_for(std::chrono::milliseconds(1)); });
        }
        client.stop();
        pending_.fetch_sub(1);
        onResult_(std::move(result));
      }).detach();
      return true;
    }

    std::string origin, target;
    if (!split(req.url, origin, target)) {
      logf("script http: unsupported url %s", req.url.c_str());
      return false;
    }
    const std::size_t maxBytes = req.maxBytes ? req.maxBytes : script::kMaxHttpBody;
    pending_.fetch_add(1);
    std::thread([this, req, origin, target, maxBytes] {
      script::HttpResult r;
      r.id = req.id;
      httplib::Client cli(origin);
      if (tc002::hardwareEnabled()) cli.set_ca_cert_path(tc002::caCertPath());
      cli.set_connection_timeout(5, 0);
      cli.set_read_timeout(10, 0);
      cli.set_follow_location(true);

      httplib::Headers headers;
      for (const auto& h : req.headers) headers.emplace(h.first, h.second);
      const std::string type = contentType(req.headers);

      script::HttpBodyFilter filter;
      filter.begin(req.find, req.keep, script::httpBodyCap(maxBytes));
      httplib::Request request;
      request.method = req.method.empty() ? "GET" : req.method;
      request.path = target;
      request.headers = headers;
      request.body = req.body;
      request.set_header("Content-Type", type);
      request.response_handler = [&](const httplib::Response& response) {
        r.status = response.status;
        return true;
      };
      request.content_receiver = [&](const char* bytes, size_t count, uint64_t, uint64_t) {
        filter.feed(bytes, count);
        return !filter.done();
      };
      auto res = cli.send(request);
      if (res || (r.status && filter.done())) {
        r.ok = filter.matched() && !filter.outOfRoom();
        if (r.ok) r.body = std::move(filter.body());
      }
      pending_.fetch_sub(1);
      onResult_(std::move(r));
    }).detach();
    return true;
  }

 private:
  static constexpr unsigned kMaxPending = 8;

  static std::string contentType(const script::HttpHeaders& headers) {
    for (const auto& h : headers) {
      if (h.first.size() != 12) continue;
      std::string lower;
      for (const char c : h.first) lower.push_back(static_cast<char>(std::tolower(c)));
      if (lower == "content-type") return h.second;
    }
    return "application/octet-stream";
  }

  // Split HTTP(S) into the origin and request target accepted by httplib.
  static bool split(const std::string& url, std::string& origin, std::string& target) {
    const std::string scheme = url.rfind("https://", 0) == 0 ? "https://" : "http://";
    if (url.compare(0, scheme.size(), scheme) != 0) return false;
    const std::size_t slash = url.find('/', scheme.size());
    if (slash == std::string::npos) {
      origin = url;
      target = "/";
    } else {
      origin = url.substr(0, slash);
      target = url.substr(slash);
    }
    return origin.size() > scheme.size();
  }

  ResultFn onResult_;
  std::atomic<unsigned> pending_{0};
};

}
