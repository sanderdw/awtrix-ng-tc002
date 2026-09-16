#include "Tc002HttpRoutes.h"

#include <fcntl.h>
#include <spawn.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <sys/wait.h>
#include <unistd.h>

#include <cerrno>
#include <cstdlib>
#include <filesystem>
#include <mutex>
#include <string>

#include "FirmwareImage.h"
#include "Tc002Board.h"
#include "Tc002Hardware.h"
#include "Tc002System.h"
#include "VendorLibrary.h"
#include "core/AssetPaths.h"
#include "core/ProvisioningPolicy.h"
#include "core/api/ApiRouter.h"
#include "core/api/JsonWriter.h"
#include "core/api/StateJson.h"
#include "persistence/DeviceConfig.h"
#include "sim/SimStore.h"
#include "sim/vendor/httplib.h"
#include "system/Log.h"

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

// The clock has about 14 MiB of RAM to spare and upstream's server buffers every request body.
// Small routes get small caps enforced before the body is read; the two large uploads stream to
// storage and never sit in memory.
constexpr size_t kJsonBodyMax = 64 * 1024;
constexpr size_t kAssetBodyMax = 256 * 1024;
constexpr size_t kRestoreBodyMax = 4u * 1024 * 1024;
constexpr size_t kMp3BodyMax = 8u * 1024 * 1024;
constexpr size_t kFirmwareBodyMax = 0x800000 + 572;
constexpr size_t kStagingReserveBytes = 1024 * 1024;

size_t bufferedBodyLimit(const std::string& path) {
  if (path == "/api/v1/restore") return kRestoreBodyMax;
  if (path == "/api/v1/files" || path.rfind("/api/v1/audio/melodies", 0) == 0 ||
      path.rfind("/api/v1/apps/script/", 0) == 0 || path.rfind("/api/v1/scripts", 0) == 0)
    return kAssetBodyMax;
  return kJsonBodyMax;
}

bool streamedRoute(const std::string& path) {
  return path == "/update" || path == "/api/v1/audio/mp3";
}

// One large upload at a time: the second MP3 or firmware upload answers 409 instead of doubling
// the storage and CPU load on a small device.
std::mutex& uploadMutex() {
  static std::mutex m;
  return m;
}

std::string stagingDir() { return sim::dataDir() + "/staging"; }

// Leftovers from an interrupted upload or a completed install are removed at start-up.
void clearStaging() {
  std::error_code ec;
  for (const auto& entry : stdfs::directory_iterator(stdfs::u8path(stagingDir()), ec))
    stdfs::remove(entry.path(), ec);
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
  if (path == "/api/v1/tc002/vendor") {
    if (method != "GET") {
      sendError(res, 405, "methodNotAllowed", "allowed method(s): GET");
      return true;
    }
    sendJson(res, 200, tc002::vendorStatusJson());
    return true;
  }
  return false;
}

// Checks that run on the loop thread before a streamed upload is accepted: hardware present,
// not provisioning, and authenticated. False means the response has been written.
bool gate(SimHttpServer& server, const DeviceConfig& cfg, const httplib::Request& req,
          httplib::Response& res, bool requireHardware) {
  bool allowed = false;
  server.runOnLoop([&] {
    if (requireHardware && !tc002::hardwareEnabled()) {
      sendError(res, 503, "unavailable", "updates require TC002 hardware");
      return;
    }
    if (tc002::wifiApMode()) {
      sendError(res, 403, "forbidden", "not available during provisioning");
      return;
    }
    if (!authorized(req, res, cfg)) return;
    allowed = true;
  });
  if (!allowed && res.status < 200) sendError(res, 503, "unavailable", "server is stopping");
  return allowed;
}

// Streams an MP3 upload straight into the MP3 folder, validating the name and the first bytes
// as upstream's buffered handler does, without ever holding the file in memory.
void handleMp3Upload(SimHttpServer& server, const DeviceConfig& cfg, const httplib::Request& req,
                     httplib::Response& res, const httplib::ContentReader& reader) {
  if (!gate(server, cfg, req, res, false)) return;
  std::unique_lock<std::mutex> upload(uploadMutex(), std::try_to_lock);
  if (!upload.owns_lock()) {
    sendError(res, 409, "uploadBusy", "another upload is in progress");
    return;
  }
  if (!req.is_multipart_form_data()) {
    sendError(res, 400, "invalidFile", "a multipart MP3 file is required");
    return;
  }
  int fd = -1;
  std::string target, temp;
  bool skipping = false, first = true;
  size_t bytes = 0;
  int status = 0;
  const char* code = nullptr;
  const char* message = nullptr;
  const auto fail = [&](int s, const char* c, const char* m) {
    status = s;
    code = c;
    message = m;
    return false;
  };
  reader(
      [&](const httplib::MultipartFormData& file) {
        skipping = file.filename.empty();
        if (skipping) return true;
        if (fd >= 0) return fail(400, "invalidFile", "one MP3 file per upload");
        const std::string& name = file.filename;
        if (name.find('/') != std::string::npos || !assets::uploadNameOk("/MP3/" + name))
          return fail(400, "invalidName",
                      "expected an MP3 filename with 1-32 characters of A-Z, a-z, 0-9, _ or -");
        target = sim::hostPath("/MP3/" + name);
        std::error_code ec;
        stdfs::create_directories(stdfs::u8path(target).parent_path(), ec);
        temp = target + ".part-XXXXXX";
        fd = mkstemp(temp.data());
        if (fd < 0) return fail(507, "insufficientStorage", "could not create the file");
        fcntl(fd, F_SETFD, FD_CLOEXEC);
        first = true;
        return true;
      },
      [&](const char* data, size_t count) {
        if (skipping) return true;
        if (fd < 0) return false;
        if (first) {
          first = false;
          if (!assets::looksLikeMp3(reinterpret_cast<const unsigned char*>(data),
                                    static_cast<unsigned>(count)))
            return fail(415, "unsupportedMediaType",
                        "file content does not match the target folder; expected MP3 audio");
        }
        if (bytes + count > kMp3BodyMax) return fail(413, "payloadTooLarge", "MP3 files are limited to 8 MiB");
        size_t pos = 0;
        while (pos < count) {
          const ssize_t n = write(fd, data + pos, count - pos);
          if (n <= 0) return fail(507, "insufficientStorage", "could not write the file");
          pos += static_cast<size_t>(n);
        }
        bytes += count;
        return true;
      });
  if (fd >= 0) close(fd);
  if (!status && fd < 0) {
    status = 400;
    code = "invalidFile";
    message = "a multipart MP3 file is required";
  }
  if (status) {
    if (!temp.empty()) unlink(temp.c_str());
    sendError(res, status, code, message);
    return;
  }
  if (std::rename(temp.c_str(), target.c_str()) != 0) {
    unlink(temp.c_str());
    sendError(res, 507, "insufficientStorage", "could not store the file");
    return;
  }
  logf("mp3: uploaded %s (%u B)", target.c_str(), static_cast<unsigned>(bytes));
  sendJson(res, 200, "{\"ok\":true}");
}

// Runs the update helper with one argument and waits; the helper's output goes to the log file.
int runHelper(const char* helper, const char* mode, const char* image, const char* logPath, bool wait) {
  posix_spawn_file_actions_t actions;
  posix_spawn_file_actions_init(&actions);
  posix_spawn_file_actions_addopen(&actions, 1, logPath, O_WRONLY | O_CREAT | O_APPEND, 0600);
  posix_spawn_file_actions_adddup2(&actions, 1, 2);
  char* args[] = {const_cast<char*>(helper), const_cast<char*>(mode), const_cast<char*>(image), nullptr};
  pid_t child = 0;
  const int spawned = posix_spawn(&child, helper, &actions, nullptr, args, environ);
  posix_spawn_file_actions_destroy(&actions);
  if (spawned) return -1;
  if (!wait) return 0;
  int status = 0;
  if (waitpid(child, &status, 0) != child) return -1;
  return WIFEXITED(status) ? WEXITSTATUS(status) : -1;
}

// Streams the firmware upload to the data partition (flash, not the RAM-backed /tmp), validates
// the ZKSWE container, runs the helper's preflight, and only then starts the install.
void handleFirmware(SimHttpServer& server, const DeviceConfig& cfg, const httplib::Request& req,
                    httplib::Response& res, const httplib::ContentReader& reader) {
  if (!gate(server, cfg, req, res, true)) return;
  std::unique_lock<std::mutex> upload(uploadMutex(), std::try_to_lock);
  if (!upload.owns_lock()) {
    sendError(res, 409, "updateBusy", "another upload is in progress");
    return;
  }
  if (!req.is_multipart_form_data()) {
    sendError(res, 400, "invalidImage", "multipart firmware file is required");
    return;
  }
  std::error_code ec;
  stdfs::create_directories(stdfs::u8path(stagingDir()), ec);
  // The data partition is an 8 MiB jffs2 volume; ask for room for this upload, not the largest
  // possible one. Without a Content-Length, assume a typical 5 MiB image.
  const unsigned long long declared =
      req.has_header("Content-Length")
          ? std::strtoull(req.get_header_value("Content-Length").c_str(), nullptr, 10)
          : 5ull * 1024 * 1024;
  struct statvfs space {};
  if (statvfs(stagingDir().c_str(), &space) != 0 ||
      static_cast<unsigned long long>(space.f_bavail) * space.f_frsize <
          declared + kStagingReserveBytes) {
    sendError(res, 507, "insufficientStorage",
              ("the data partition needs " + std::to_string((declared + kStagingReserveBytes) / 1024 / 1024 + 1) +
               " MiB free to stage this image").c_str());
    return;
  }
  std::string path = stagingDir() + "/update-XXXXXX";
  int fd = mkstemp(path.data());
  if (fd < 0) {
    sendError(res, 507, "insufficientStorage", "cannot stage update");
    return;
  }
  fcntl(fd, F_SETFD, FD_CLOEXEC);
  bool fileSeen = false, noSpace = false;
  size_t bytes = 0;
  const bool received = reader(
      [&](const httplib::MultipartFormData& file) {
        if (fileSeen || file.name != "firmware") return false;
        fileSeen = true;
        return true;
      },
      [&](const char* data, size_t count) {
        if (!fileSeen || bytes + count > kFirmwareBodyMax) return false;
        size_t pos = 0;
        while (pos < count) {
          const ssize_t n = write(fd, data + pos, count - pos);
          if (n <= 0) {
            noSpace = errno == ENOSPC;
            return false;
          }
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
    unlink(path.c_str());
    if (noSpace) sendError(res, 507, "insufficientStorage", "the data partition ran out of room while staging");
    else sendError(res, 422, "invalidImage", error.empty() ? "incomplete or invalid upload" : error.c_str());
    return;
  }
  const char* helper = "/tmp/awtrix-update-helper";
  const char* logPath = "/tmp/awtrix-update.log";
  stdfs::copy_file("/res/bin/tc002-update", helper, stdfs::copy_options::overwrite_existing, ec);
  if (ec || chmod(helper, 0700) != 0) {
    unlink(path.c_str());
    sendError(res, 503, "unavailable", "installed update helper is missing");
    return;
  }
  unlink(logPath);
  // Preflight checks the image against the live partition and the flash geometry without
  // writing anything; an install is only started once it has passed.
  const int preflight = runHelper(helper, "--preflight", path.c_str(), logPath, true);
  if (preflight != 0) {
    unlink(path.c_str());
    sendError(res, 422, "preflightFailed",
              "the update helper refused this image; see /tmp/awtrix-update.log on the clock");
    return;
  }
  if (runHelper(helper, "--install", path.c_str(), logPath, false) != 0) {
    unlink(path.c_str());
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
    clearStaging();
    // Hard ceiling for anything that reaches the buffered routes; the per-route caps below reject
    // earlier, before a byte of the body has been read.
    svr.set_payload_max_length(kMp3BodyMax + 65536u);
    svr.set_pre_routing_handler([](const httplib::Request& req, httplib::Response& res) {
      if (req.method == "GET" || req.method == "HEAD" || req.method == "OPTIONS" || streamedRoute(req.path))
        return httplib::Server::HandlerResponse::Unhandled;
      const size_t limit = bufferedBodyLimit(req.path);
      if (req.has_header("Content-Length")) {
        const unsigned long long declared =
            std::strtoull(req.get_header_value("Content-Length").c_str(), nullptr, 10);
        if (declared > limit) {
          res.set_header("Connection", "close");
          sendError(res, 413, "payloadTooLarge",
                    ("request body exceeds " + std::to_string(limit / 1024) + " KiB").c_str());
          return httplib::Server::HandlerResponse::Handled;
        }
      } else if (req.has_header("Transfer-Encoding")) {
        res.set_header("Connection", "close");
        sendError(res, 411, "lengthRequired", "send a Content-Length header");
        return httplib::Server::HandlerResponse::Handled;
      }
      return httplib::Server::HandlerResponse::Unhandled;
    });
    // Two worker threads serve the clock; idle keep-alive connections must not hold them.
    svr.set_keep_alive_max_count(20);
    svr.set_keep_alive_timeout(5);
    svr.set_read_timeout(5, 0);
    svr.set_write_timeout(10, 0);
    svr.Post("/api/v1/audio/mp3", [&server, &cfg](const httplib::Request& req, httplib::Response& res,
                                                   const httplib::ContentReader& reader) {
      handleMp3Upload(server, cfg, req, res, reader);
    });
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
