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

int main(int argc, char* argv[]) {
  (void)argc; (void)argv;

  romfsInit();
  socketInitializeDefault();
  fsInitialize();
  fsdevMountSdmc();

  // Move a pre-1.1.0 install to the current title ID and start it there.
  if (MigrateLegacyInstall() && InstallNsp())
    ModuleRestart();

  int upload_fd = SetupUploadServer();

  Version running_version = {};
  bool module_running = QueryModuleVersion(&running_version);

  Arriba::init();

  if (access(kNspPath, F_OK) != 0) {
    if (!ShowInstallPrompt()) {
      Arriba::exit();
      if (upload_fd >= 0) close(upload_fd);
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
  InitUi(upload_fd, ip, module_running, running_version);
  constexpr auto kIpRefreshInterval = std::chrono::seconds(1);
  auto next_ip_refresh = std::chrono::steady_clock::now() + kIpRefreshInterval;

  while (appletMainLoop()) {
    if (HandleUiInput(upload_fd)) break;
    auto now = std::chrono::steady_clock::now();
    if (now >= next_ip_refresh) {
      std::string current_ip = GetLocalIp();
      if (current_ip != ip) {
        ip = current_ip;
        UpdateIpText(ip);
      }
      next_ip_refresh = now + kIpRefreshInterval;
    }
    Arriba::drawFrame();
  }

  if (upload_fd >= 0) close(upload_fd);
  Arriba::exit();
  fsdevUnmountAll();
  fsExit();
  socketExit();
  romfsExit();
  return 0;
}
