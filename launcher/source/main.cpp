#include <string>
#include <chrono>
#include <unistd.h>

#include <switch.h>
#include <arriba.h>

#include "version.hpp"
#include "constants.hpp"
#include "module.hpp"
#include "network.hpp"
#include "ui.hpp"

namespace {
bool g_resumed = false;

void OnAppletEvent(AppletHookType type, void*) {
  if (type == AppletHookType_OnResume) g_resumed = true;
}
}  // namespace

int main(int argc, char* argv[]) {
  (void)argc; (void)argv;

  romfsInit();
  socketInitializeDefault();
  fsInitialize();
  fsdevMountSdmc();

  // Move a pre-1.1.0 install to the current title ID and start it there.
  if (MigrateLegacyInstall() && InstallNsp())
    ModuleRestart();

  Version running_version = {};
  bool module_running = QueryModuleVersion(&running_version);

  Arriba::init();

  if (access(kNspPath, F_OK) != 0) {
    if (!ShowInstallPrompt()) {
      Arriba::exit();
      fsdevUnmountAll();
      fsExit();
      socketExit();
      romfsExit();
      return 0;
    }
  }

  if (module_running &&
      (running_version.major_ != kExpectedVersion.major_ ||
       running_version.minor_ != kExpectedVersion.minor_ ||
       running_version.patch_ != kExpectedVersion.patch_)) {
    if (ShowUpdatePrompt(running_version))
      running_version = kExpectedVersion;
  }

  std::string ip = GetLocalIp();
  int upload_fd = ip != kUnavailableIp ? SetupUploadServer() : -1;
  InitUi(upload_fd, ip, module_running, running_version);
  AppletHookCookie hook_cookie;
  appletHook(&hook_cookie, OnAppletEvent, nullptr);
  constexpr auto kIpRefreshInterval = std::chrono::seconds(1);
  auto next_ip_refresh = std::chrono::steady_clock::now() + kIpRefreshInterval;

  while (appletMainLoop()) {
    if (HandleUiInput(upload_fd)) break;
    auto now = std::chrono::steady_clock::now();
    if (g_resumed || now >= next_ip_refresh) {
      bool upload_available = upload_fd >= 0;
      std::string current_ip = GetLocalIp();
      bool network_changed = g_resumed || current_ip != ip;
      g_resumed = false;
      if (current_ip != ip) {
        ip = current_ip;
        UpdateIpText(ip);
      }
      if (network_changed && upload_fd >= 0) {
        close(upload_fd);
        upload_fd = -1;
      }
      if (upload_fd < 0 && ip != kUnavailableIp) {
        upload_fd = SetupUploadServer();
      }
      if (upload_available != (upload_fd >= 0)) UpdateUploadText(upload_fd);
      next_ip_refresh = now + kIpRefreshInterval;
    }
    Arriba::drawFrame();
  }

  appletUnhook(&hook_cookie);
  if (upload_fd >= 0) close(upload_fd);
  Arriba::exit();
  fsdevUnmountAll();
  fsExit();
  socketExit();
  romfsExit();
  return 0;
}
