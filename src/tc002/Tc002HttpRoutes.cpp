#include "Tc002HttpRoutes.h"

#include <fcntl.h>
#include <spawn.h>
#include <sys/stat.h>
#include <unistd.h>

#include <filesystem>
#include <mutex>
#include <string>

#include "FirmwareImage.h"
#include "Tc002Board.h"
#include "Tc002Hardware.h"
#include "Tc002System.h"
#include "core/ProvisioningPolicy.h"
#include "core/api/ApiRouter.h"
#include "core/api/JsonWriter.h"
#include "core/api/StateJson.h"
#include "persistence/DeviceConfig.h"
#include "sim/vendor/httplib.h"

extern char** environ;

namespace awtrix {

namespace {

namespace stdfs = std::filesystem;

void sendJson(httplib::Response& res, int status, const std::string& body) {
  res.status = status;
  res.set_content(body, "application/json");
}

void sendError(httplib::Response& res, int status, const char* code, const char* message) {
  sendJson(res, status, api::errorJson(code, message));
}

bool authorized(const httplib::Request& req, httplib::Response& res, const DeviceConfig& cfg) {
  if (!cfg.authEnabled) return true;
  const auto expected = httplib::make_basic_authentication_header(cfg.authUser, cfg.authPass);
  if (req.get_header_value(expected.first) == expected.second) return true;
  res.set_header("WWW-Authenticate", "Basic realm=\"AWTRIX NG TC002\"");
  sendError(res, 401, "unauthorized", "authentication required");
  return false;
}

// Runs on the loop thread ahead of upstream's routing. Returns true when the response is complete.
bool preRoute(const httplib::Request& req, httplib::Response& res, Tc002Board& board,
              const DeviceConfig& cfg) {
  if (!authorized(req, res, cfg)) return true;
  const std::string& path = req.path;
  const api::MethodResolution resolved = api::resolveHttpMethod(
      req.method, path,
      req.has_header(api::kMethodOverrideHeader) ? req.get_header_value(api::kMethodOverrideHeader)
                                                 : std::string());
  if (resolved.error) return false;  // upstream reports the bad override
  const std::string& method = resolved.method;
  if (tc002::wifiApMode() && !provisioning::apModeAllows(method, path)) {
    sendError(res, 403, "forbidden", "not available during provisioning");
    return true;
  }
  if (path == "/sim" || path.rfind("/sim/", 0) == 0) {
    if (tc002::hardwareEnabled()) {
      sendError(res, 404, "notFound", "not found");
      return true;
    }
    if (method == "POST" && (path == "/sim/rotary/left" || path == "/sim/rotary/right")) {
      board.pendingRotation += path == "/sim/rotary/right" ? 1 : -1;
      sendJson(res, 200, "{\"ok\":true}");
      return true;
    }
    return false;
  }
  if (path == "/api/v1/system/wifi-scan" && method == "GET" && tc002::hardwareEnabled()) {
    const auto scan = tc002::wifiScan();
    sendJson(res, scan.empty() ? 202 : 200, scan.empty() ? "[]" : scan);
    return true;
  }
  if (path == "/update") {
    sendError(res, 405, "methodNotAllowed", "use multipart POST");
    return true;
  }
  return false;
}

// Streams the multipart upload to a temporary file, validates the ZKSWE container, and hands it
// to the update helper. The helper does its own geometry checks before anything is erased.
void handleFirmware(SimHttpServer& server, const DeviceConfig& cfg, const httplib::Request& req,
                    httplib::Response& res, const httplib::ContentReader& reader) {
  bool allowed = false;
  server.runOnLoop([&] {
    if (!tc002::hardwareEnabled()) {
      sendError(res, 503, "unavailable", "updates require TC002 hardware");
      return;
    }
    if (tc002::wifiApMode()) {
      sendError(res, 403, "forbidden", "updates are disabled during provisioning");
      return;
    }
    if (!authorized(req, res, cfg)) return;
    allowed = true;
  });
  if (!allowed) return;
  static std::mutex uploadMutex;
  std::unique_lock<std::mutex> upload(uploadMutex, std::try_to_lock);
  if (!upload.owns_lock()) {
    sendError(res, 409, "updateBusy", "another upload is in progress");
    return;
  }
  if (!req.is_multipart_form_data()) {
    sendError(res, 400, "invalidImage", "multipart firmware file is required");
    return;
  }
  char path[] = "/tmp/awtrix-update-XXXXXX";
  int fd = mkstemp(path);
  if (fd < 0) {
    sendError(res, 507, "insufficientStorage", "cannot stage update");
    return;
  }
  fcntl(fd, F_SETFD, FD_CLOEXEC);
  bool fileSeen = false;
  size_t bytes = 0;
  const bool received = reader(
      [&](const httplib::MultipartFormData& file) {
        if (fileSeen || file.name != "firmware") return false;
        fileSeen = true;
        return true;
      },
      [&](const char* data, size_t count) {
        if (!fileSeen || bytes + count > 0x800000 + 572) return false;
        size_t pos = 0;
        while (pos < count) {
          const ssize_t n = write(fd, data + pos, count - pos);
          if (n <= 0) return false;
          pos += static_cast<size_t>(n);
        }
        bytes += count;
        return true;
      });
  tc002::FirmwareImage image;
  std::string error;
  const bool valid = received && fileSeen && tc002::validateFirmware(fd, image, error);
  close(fd);
  if (!valid) {
    unlink(path);
    sendError(res, 422, "invalidImage", error.empty() ? "incomplete or invalid upload" : error.c_str());
    return;
  }
  const char* helper = "/tmp/awtrix-update-helper";
  std::error_code ec;
  stdfs::copy_file("/res/bin/tc002-update", helper, stdfs::copy_options::overwrite_existing, ec);
  if (ec || chmod(helper, 0700) != 0) {
    unlink(path);
    sendError(res, 503, "unavailable", "installed update helper is missing");
    return;
  }
  char argument[] = "--install";
  char* args[] = {const_cast<char*>(helper), argument, path, nullptr};
  posix_spawn_file_actions_t actions;
  posix_spawn_file_actions_init(&actions);
  posix_spawn_file_actions_addopen(&actions, 1, "/tmp/awtrix-update.log", O_WRONLY | O_CREAT | O_TRUNC, 0600);
  posix_spawn_file_actions_adddup2(&actions, 1, 2);
  pid_t child;
  const int status = posix_spawn(&child, helper, &actions, nullptr, args, environ);
  posix_spawn_file_actions_destroy(&actions);
  if (status) {
    unlink(path);
    sendError(res, 500, "updateFailed", "cannot start update helper");
    return;
  }
  sendJson(res, 202, "{\"ok\":true}");
}

}

SimHttpExtension tc002HttpExtension(SimHttpServer& server, Tc002Board& board, DeviceConfig& cfg) {
  SimHttpExtension ext;
  ext.route = [&board, &cfg](const httplib::Request& req, httplib::Response& res) {
    return preRoute(req, res, board, cfg);
  };
  ext.configure = [&server, &cfg](httplib::Server& svr) {
    svr.set_payload_max_length(8u * 1024u * 1024u + 65536u);
    svr.Post("/update", [&server, &cfg](const httplib::Request& req, httplib::Response& res,
                                        const httplib::ContentReader& reader) {
      handleFirmware(server, cfg, req, res, reader);
    });
  };
  ext.deviceFacts = [&board](DeviceFacts& facts) {
    facts.boardType = "tc002";
    facts.soc = "ssd202d";
    facts.ipAddress = tc002::wifiApMode() ? "0.0.0.0" : tc002::ipAddress();
    facts.freeHeapBytes = tc002::availableMemory();
    facts.minFreeHeapBytes = 0;
    facts.hasBattery = tc002::batteryAvailable();
    facts.hasLightSensor = false;
    facts.wifiRssi = tc002::wifiRssi();
    facts.hasTemperature = false;
    facts.hasHumidity = false;
    (void)board;
  };
  return ext;
}

}
