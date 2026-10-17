#include <cstdio>
#include <cstring>
#include <string>
#include <strings.h>
#include <sys/stat.h>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <unistd.h>

#include <switch.h>
#include <arriba.h>
#include <arribaElements.h>
#include <arribaPrimitives.h>

namespace {

// ---- constants -------------------------------------------------------------

constexpr u64   kModuleProgramId = 0x0100000000057331;
constexpr char  kNspPath[]       = "sdmc:/atmosphere/contents/0100000000057331/exefs.nsp";
constexpr char  kBoot2FlagsDir[] = "sdmc:/atmosphere/contents/0100000000057331/flags";
constexpr char  kBoot2FlagPath[] = "sdmc:/atmosphere/contents/0100000000057331/flags/boot2.flag";
constexpr u16   kUploadPort      = 7332;
constexpr u16   kServerPort      = 57331;
constexpr size_t kUploadChunk    = 65536;

// ---- state -----------------------------------------------------------------

char chunk[kUploadChunk];
Arriba::Primitives::Text*  status_text   = nullptr;
Arriba::Primitives::Text*  boot2_text    = nullptr;
Arriba::Elements::Button*  buttons[3]    = {nullptr, nullptr, nullptr};
int                        selected      = 0;

// ---- network ---------------------------------------------------------------

std::string GetLocalIp() {
  int sock = socket(AF_INET, SOCK_DGRAM, 0);
  if (sock < 0) return "?.?.?.?";

  struct sockaddr_in addr{};
  addr.sin_family = AF_INET;
  addr.sin_port   = htons(1);
  inet_pton(AF_INET, "1.1.1.1", &addr.sin_addr);

  if (connect(sock, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
    close(sock);
    return "?.?.?.?";
  }

  struct sockaddr_in local{};
  socklen_t len = sizeof(local);
  getsockname(sock, (struct sockaddr*)&local, &len);
  close(sock);

  char buf[INET_ADDRSTRLEN];
  inet_ntop(AF_INET, &local.sin_addr, buf, sizeof(buf));
  return buf;
}

// ---- upload server ---------------------------------------------------------

ssize_t RecvLine(int fd, char* buf, size_t max) {
  size_t n = 0;
  char c;
  while (n < max - 1) {
    if (recv(fd, &c, 1, 0) <= 0) return -1;
    if (c == '\n') {
      if (n > 0 && buf[n - 1] == '\r') n--;
      break;
    }
    buf[n++] = c;
  }
  buf[n] = '\0';
  return (ssize_t)n;
}

void SendResponse(int fd, int status) {
  char buf[64];
  snprintf(buf, sizeof(buf),
    "HTTP/1.1 %d %s\r\nContent-Length: 0\r\n\r\n",
    status, status == 200 ? "OK" : "Error");
  send(fd, buf, strlen(buf), 0);
}

bool HandleUpload(int fd) {
  char line[512];
  size_t content_length = 0;

  if (RecvLine(fd, line, sizeof(line)) < 0) {
    SendResponse(fd, 400);
    return false;
  }

  while (true) {
    ssize_t n = RecvLine(fd, line, sizeof(line));
    if (n < 0) return false;
    if (n == 0) break;
    if (strncasecmp(line, "Content-Length:", 15) == 0)
      content_length = strtoul(line + 15, nullptr, 10);
  }

  if (content_length == 0) {
    SendResponse(fd, 400);
    return false;
  }

  FILE* f = fopen(kNspPath, "wb");
  if (!f) {
    SendResponse(fd, 500);
    return false;
  }

  size_t remaining = content_length;
  bool ok = true;
  while (remaining > 0) {
    size_t want = remaining < kUploadChunk ? remaining : kUploadChunk;
    ssize_t got = recv(fd, chunk, want, 0);
    if (got <= 0) { ok = false; break; }
    if (fwrite(chunk, 1, (size_t)got, f) != (size_t)got) { ok = false; break; }
    remaining -= (size_t)got;
  }
  fclose(f);

  SendResponse(fd, ok ? 200 : 500);
  return ok;
}

int SetupUploadServer() {
  int fd = socket(AF_INET, SOCK_STREAM, 0);
  if (fd < 0) return -1;

  int opt = 1;
  setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

  struct sockaddr_in addr{};
  addr.sin_family      = AF_INET;
  addr.sin_addr.s_addr = INADDR_ANY;
  addr.sin_port        = htons(kUploadPort);

  if (bind(fd, (struct sockaddr*)&addr, sizeof(addr)) < 0 ||
      listen(fd, 4) < 0) {
    close(fd);
    return -1;
  }
  return fd;
}

// ---- module control --------------------------------------------------------

void SetStatus(const std::string& msg) {
  if (status_text) status_text->setText(msg.c_str());
}

void ModuleRestart() {
  Result rc_term = 0, rc_launch = 0;
  Result rc = pmshellInitialize();
  if (R_SUCCEEDED(rc)) {
    rc_term = pmshellTerminateProgram(kModuleProgramId);
    NcmProgramLocation loc = {kModuleProgramId, NcmStorageId_None};
    rc_launch = pmshellLaunchProgram(0, &loc, nullptr);
    pmshellExit();
  } else {
    rc_term = rc_launch = rc;
  }

  char buf[128];
  snprintf(buf, sizeof(buf), "Kill: 0x%08X%s  Launch: 0x%08X%s",
    rc_term,   R_SUCCEEDED(rc_term)   ? " (ok)" : " (not running)",
    rc_launch, R_SUCCEEDED(rc_launch) ? " (ok)" : " (FAILED)");
  SetStatus(buf);
}

void ModuleKill() {
  Result rc_term = 0;
  Result rc = pmshellInitialize();
  if (R_SUCCEEDED(rc)) {
    rc_term = pmshellTerminateProgram(kModuleProgramId);
    pmshellExit();
  } else {
    rc_term = rc;
  }

  char buf[64];
  snprintf(buf, sizeof(buf), "Kill: 0x%08X%s",
    rc_term, R_SUCCEEDED(rc_term) ? " (ok)" : " (not running)");
  SetStatus(buf);
}

// ---- install ---------------------------------------------------------------

bool InstallNsp() {
  mkdir("sdmc:/atmosphere", 0777);
  mkdir("sdmc:/atmosphere/contents", 0777);
  mkdir("sdmc:/atmosphere/contents/0100000000057331", 0777);

  FILE* src = fopen("romfs:/exefs.nsp", "rb");
  if (!src) return false;

  FILE* dst = fopen(kNspPath, "wb");
  if (!dst) { fclose(src); return false; }

  bool ok = true;
  size_t n;
  while ((n = fread(chunk, 1, kUploadChunk, src)) > 0) {
    if (fwrite(chunk, 1, n, dst) != n) { ok = false; break; }
  }
  fclose(src);
  fclose(dst);
  if (!ok) remove(kNspPath);
  return ok;
}

enum class PromptAction { kNone, kInstall, kExit };

bool ShowInstallPrompt() {
  int w = Arriba::Graphics::windowWidth;
  int h = Arriba::Graphics::windowHeight;

  auto* bg = new Arriba::Primitives::Quad(0, 0, w, h, Arriba::Graphics::Pivot::topLeft);
  bg->setColour({0.1f, 0.1f, 0.12f, 1.0f});

  auto* title = new Arriba::Primitives::Text("NX-MCP Launcher", 58);
  title->transform.position = {w / 2.0f, 55.0f, 0};
  title->setColour({1, 1, 1, 1});

  auto* msg = new Arriba::Primitives::Text("Server module not installed", 40);
  msg->transform.position = {w / 2.0f, 220.0f, 0};
  msg->setColour({0.9f, 0.9f, 0.9f, 1.0f});

  auto* submsg = new Arriba::Primitives::Text("exefs.nsp not found on SD card. Install now?", 28);
  submsg->transform.position = {w / 2.0f, 268.0f, 0};
  submsg->setColour({0.55f, 0.55f, 0.55f, 1.0f});

  constexpr int kBtnW = 420;
  constexpr int kBtnH = 72;

  auto* install_btn = new Arriba::Elements::Button();
  install_btn->setDimensions(kBtnW, kBtnH, Arriba::Graphics::Pivot::centre);
  install_btn->transform.position = {w / 2.0f, 370.0f, 0};
  install_btn->setText("Install");

  auto* exit_btn = new Arriba::Elements::Button();
  exit_btn->setDimensions(kBtnW, kBtnH, Arriba::Graphics::Pivot::centre);
  exit_btn->transform.position = {w / 2.0f, 465.0f, 0};
  exit_btn->setText("Exit");

  auto* prompt_status = new Arriba::Primitives::Text("", 28);
  prompt_status->transform.position = {w / 2.0f, 558.0f, 0};
  prompt_status->setColour({0.75f, 0.75f, 0.75f, 1.0f});

  auto* footer = new Arriba::Primitives::Text("A: select     DPad: navigate     +: exit", 26);
  footer->transform.position = {w / 2.0f, (float)h - 28.0f, 0};
  footer->setColour({0.35f, 0.35f, 0.35f, 1.0f});

  Arriba::Elements::Button* btns[2] = {install_btn, exit_btn};
  Arriba::highlightedObject = install_btn;

  PromptAction action = PromptAction::kNone;
  install_btn->registerCallback([&]() { action = PromptAction::kInstall; });
  exit_btn->registerCallback([&]() { action = PromptAction::kExit; });

  bool result = false;
  int sel = 0;

  while (appletMainLoop()) {
    if (action == PromptAction::kInstall) {
      action = PromptAction::kNone;
      prompt_status->setText("Installing...");
      Arriba::drawFrame();
      if (InstallNsp()) {
        result = true;
        break;
      }
      prompt_status->setText("Install failed");
    } else if (action == PromptAction::kExit) {
      break;
    }

    if (Arriba::Input::buttonDown(Arriba::Input::DPadDown) ||
        Arriba::Input::buttonDown(Arriba::Input::DPadRight)) {
      sel = (sel + 1) % 2;
      Arriba::highlightedObject = btns[sel];
    } else if (Arriba::Input::buttonDown(Arriba::Input::DPadUp) ||
               Arriba::Input::buttonDown(Arriba::Input::DPadLeft)) {
      sel = (sel + 1) % 2;
      Arriba::highlightedObject = btns[sel];
    }

    if (Arriba::Input::buttonDown(Arriba::Input::PlusButtonSwitch)) break;

    Arriba::drawFrame();
  }

  bg->destroy();
  title->destroy();
  msg->destroy();
  submsg->destroy();
  install_btn->destroy();
  exit_btn->destroy();
  prompt_status->destroy();
  footer->destroy();
  Arriba::highlightedObject = nullptr;

  return result;
}

// ---- boot2 flag ------------------------------------------------------------

bool Boot2FlagExists() {
  return access(kBoot2FlagPath, F_OK) == 0;
}

void UpdateBoot2Text() {
  if (!boot2_text) return;
  if (Boot2FlagExists()) {
    boot2_text->setText("Boot2: ON");
    boot2_text->setColour({0.2f, 0.9f, 0.3f, 1.0f});
  } else {
    boot2_text->setText("Boot2: OFF");
    boot2_text->setColour({0.9f, 0.3f, 0.3f, 1.0f});
  }
}

void ToggleBoot2() {
  if (Boot2FlagExists()) {
    remove(kBoot2FlagPath);
    SetStatus("Boot2 flag removed");
  } else {
    mkdir(kBoot2FlagsDir, 0777);
    FILE* f = fopen(kBoot2FlagPath, "w");
    if (f) {
      fclose(f);
      SetStatus("Boot2 flag created");
    } else {
      SetStatus("Boot2: failed to create flag");
    }
  }
  UpdateBoot2Text();
}

// ---- ui --------------------------------------------------------------------

void InitUi(int upload_fd, const std::string& ip) {
  int w = Arriba::Graphics::windowWidth;
  int h = Arriba::Graphics::windowHeight;

  // Background
  auto* bg = new Arriba::Primitives::Quad(0, 0, w, h, Arriba::Graphics::Pivot::topLeft);
  bg->setColour({0.1f, 0.1f, 0.12f, 1.0f});

  // Title
  auto* title = new Arriba::Primitives::Text("NX-MCP Launcher", 58);
  title->transform.position = {w / 2.0f, 55.0f, 0};
  title->setColour({1, 1, 1, 1});

  // Console IP
  auto* ip_text = new Arriba::Primitives::Text(ip.c_str(), 30);
  ip_text->transform.position = {w / 2.0f, 98.0f, 0};
  ip_text->setColour({0.6f, 0.6f, 0.6f, 1.0f});

  // MCP port
  char mcp_buf[32];
  snprintf(mcp_buf, sizeof(mcp_buf), "MCP: %u", kServerPort);
  auto* mcp_text = new Arriba::Primitives::Text(mcp_buf, 26);
  mcp_text->transform.position = {w / 2.0f, 130.0f, 0};
  mcp_text->setColour({0.45f, 0.45f, 0.45f, 1.0f});

  // Upload port
  char upload_buf[32];
  snprintf(upload_buf, sizeof(upload_buf), "Upload: %u%s",
    kUploadPort, upload_fd >= 0 ? "" : " (unavailable)");
  auto* upload_text = new Arriba::Primitives::Text(upload_buf, 26);
  upload_text->transform.position = {w / 2.0f, 158.0f, 0};
  upload_text->setColour({0.45f, 0.45f, 0.45f, 1.0f});

  // Buttons
  constexpr int kBtnW = 500;
  constexpr int kBtnH = 72;

  buttons[0] = new Arriba::Elements::Button();
  buttons[0]->setDimensions(kBtnW, kBtnH, Arriba::Graphics::Pivot::centre);
  buttons[0]->transform.position = {w / 2.0f, 265.0f, 0};
  buttons[0]->setText("Restart Module");
  buttons[0]->registerCallback([]() { ModuleRestart(); });

  buttons[1] = new Arriba::Elements::Button();
  buttons[1]->setDimensions(kBtnW, kBtnH, Arriba::Graphics::Pivot::centre);
  buttons[1]->transform.position = {w / 2.0f, 360.0f, 0};
  buttons[1]->setText("Kill Module");
  buttons[1]->registerCallback([]() { ModuleKill(); });

  buttons[2] = new Arriba::Elements::Button();
  buttons[2]->setDimensions(kBtnW, kBtnH, Arriba::Graphics::Pivot::centre);
  buttons[2]->transform.position = {w / 2.0f, 455.0f, 0};
  buttons[2]->setText("Toggle Boot2 Flag");
  buttons[2]->registerCallback([]() { ToggleBoot2(); });

  // Boot2 status
  boot2_text = new Arriba::Primitives::Text("", 32);
  boot2_text->transform.position = {w / 2.0f, 535.0f, 0};
  UpdateBoot2Text();

  // Module operation status
  status_text = new Arriba::Primitives::Text("", 30);
  status_text->transform.position = {w / 2.0f, 578.0f, 0};
  status_text->setColour({0.75f, 0.75f, 0.75f, 1.0f});

  // Footer hint
  auto* footer = new Arriba::Primitives::Text("A: select     DPad: navigate     +: exit", 26);
  footer->transform.position = {w / 2.0f, (float)h - 28.0f, 0};
  footer->setColour({0.35f, 0.35f, 0.35f, 1.0f});

  Arriba::highlightedObject = buttons[0];
}

}  // namespace

int main(int argc, char* argv[]) {
  (void)argc; (void)argv;

  romfsInit();
  socketInitializeDefault();
  fsInitialize();
  fsdevMountSdmc();

  std::string ip = GetLocalIp();
  int upload_fd = SetupUploadServer();

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

  InitUi(upload_fd, ip);

  while (appletMainLoop()) {
    // Non-blocking upload check
    if (upload_fd >= 0) {
      struct timeval tv = {0, 0};
      fd_set fds;
      FD_ZERO(&fds);
      FD_SET(upload_fd, &fds);
      if (select(upload_fd + 1, &fds, nullptr, nullptr, &tv) > 0) {
        int client = accept(upload_fd, nullptr, nullptr);
        if (client >= 0) {
          SetStatus("Uploading...");
          Arriba::drawFrame();
          bool ok = HandleUpload(client);
          close(client);
          if (ok)
            ModuleRestart();
          else
            SetStatus("Upload: FAILED");
        }
      }
    }

    // DPad navigation between buttons
    if (Arriba::Input::buttonDown(Arriba::Input::DPadDown) ||
        Arriba::Input::buttonDown(Arriba::Input::DPadRight)) {
      selected = (selected + 1) % 3;
      Arriba::highlightedObject = buttons[selected];
    } else if (Arriba::Input::buttonDown(Arriba::Input::DPadUp) ||
               Arriba::Input::buttonDown(Arriba::Input::DPadLeft)) {
      selected = (selected + 2) % 3;
      Arriba::highlightedObject = buttons[selected];
    }

    if (Arriba::Input::buttonDown(Arriba::Input::PlusButtonSwitch)) break;

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
