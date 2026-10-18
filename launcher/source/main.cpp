#include <string>
#include <unistd.h>

#include <switch.h>
#include <arriba.h>

#include "version.hpp"
#include "constants.hpp"
#include "network.hpp"
#include "ui.hpp"

int main(int argc, char* argv[]) {
  (void)argc; (void)argv;

  romfsInit();
  socketInitializeDefault();
  fsInitialize();
  fsdevMountSdmc();

  std::string ip = GetLocalIp();
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

  InitUi(upload_fd, ip, module_running, running_version);

  while (appletMainLoop()) {
    if (HandleUiInput(upload_fd)) break;
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
