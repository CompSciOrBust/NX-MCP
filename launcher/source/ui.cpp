#include "ui.hpp"
#include "constants.hpp"
#include "module.hpp"
#include "network.hpp"

#include <cstdio>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <unistd.h>

#include <arriba.h>
#include <arribaElements.h>
#include <arribaPrimitives.h>

namespace {

Arriba::Elements::Button* buttons[3]   = {nullptr, nullptr, nullptr};
int                       selected     = 0;
Arriba::Primitives::Text* boot2_text   = nullptr;
Arriba::Primitives::Text* status_text  = nullptr;
Arriba::Primitives::Text* version_text = nullptr;

void NavigateButtons(Arriba::Elements::Button** btns, int count, int* sel) {
  using namespace Arriba::Input;
  if (buttonDown(DPadDown) || buttonDown(DPadRight)) {
    *sel = (*sel + 1) % count;
    Arriba::highlightedObject = btns[*sel];
  } else if (buttonDown(DPadUp) || buttonDown(DPadLeft)) {
    *sel = (*sel + count - 1) % count;
    Arriba::highlightedObject = btns[*sel];
  }
}

void UpdateVersionText() {
  if (!version_text) return;

  bool is_outdated = g_module_running &&
      (g_running_version.major_ != kExpectedVersion.major_ ||
       g_running_version.minor_ != kExpectedVersion.minor_ ||
       g_running_version.patch_ != kExpectedVersion.patch_);

  char buf[48];
  if (!g_module_running)
    snprintf(buf, sizeof(buf), "Module: not running");
  else if (is_outdated)
    snprintf(buf, sizeof(buf), "Module: v%u.%u.%u (outdated)",
             g_running_version.major_, g_running_version.minor_, g_running_version.patch_);
  else
    snprintf(buf, sizeof(buf), "Module: v%u.%u.%u",
             g_running_version.major_, g_running_version.minor_, g_running_version.patch_);

  version_text->setText(buf);
  if (is_outdated)
    version_text->setColour({0.9f, 0.7f, 0.2f, 1.0f});
  else
    version_text->setColour({0.45f, 0.45f, 0.45f, 1.0f});
}

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

} // namespace

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

  enum class Action { kNone, kInstall, kExit };
  Action action = Action::kNone;
  install_btn->registerCallback([&]() { action = Action::kInstall; });
  exit_btn->registerCallback([&]() { action = Action::kExit; });

  bool result = false;
  int sel = 0;

  while (appletMainLoop()) {
    if (action == Action::kInstall) {
      action = Action::kNone;
      prompt_status->setText("Installing...");
      Arriba::drawFrame();
      if (InstallNsp()) {
        result = true;
        break;
      }
      prompt_status->setText("Install failed");
    } else if (action == Action::kExit) {
      break;
    }

    NavigateButtons(btns, 2, &sel);
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

bool ShowUpdatePrompt(const Version& current) {
  int w = Arriba::Graphics::windowWidth;
  int h = Arriba::Graphics::windowHeight;

  auto* bg = new Arriba::Primitives::Quad(0, 0, w, h, Arriba::Graphics::Pivot::topLeft);
  bg->setColour({0.1f, 0.1f, 0.12f, 1.0f});

  auto* title = new Arriba::Primitives::Text("NX-MCP Launcher", 58);
  title->transform.position = {w / 2.0f, 55.0f, 0};
  title->setColour({1, 1, 1, 1});

  auto* msg = new Arriba::Primitives::Text("Module update available", 40);
  msg->transform.position = {w / 2.0f, 220.0f, 0};
  msg->setColour({0.9f, 0.9f, 0.9f, 1.0f});

  char submsg_buf[128];
  snprintf(submsg_buf, sizeof(submsg_buf),
    "Running v%u.%u.%u, expected v%u.%u.%u",
    current.major_, current.minor_, current.patch_,
    kExpectedVersion.major_, kExpectedVersion.minor_, kExpectedVersion.patch_);
  auto* submsg = new Arriba::Primitives::Text(submsg_buf, 28);
  submsg->transform.position = {w / 2.0f, 268.0f, 0};
  submsg->setColour({0.55f, 0.55f, 0.55f, 1.0f});

  constexpr int kBtnW = 420;
  constexpr int kBtnH = 72;

  auto* update_btn = new Arriba::Elements::Button();
  update_btn->setDimensions(kBtnW, kBtnH, Arriba::Graphics::Pivot::centre);
  update_btn->transform.position = {w / 2.0f, 370.0f, 0};
  update_btn->setText("Update");

  auto* skip_btn = new Arriba::Elements::Button();
  skip_btn->setDimensions(kBtnW, kBtnH, Arriba::Graphics::Pivot::centre);
  skip_btn->transform.position = {w / 2.0f, 465.0f, 0};
  skip_btn->setText("Skip");

  auto* prompt_status = new Arriba::Primitives::Text("", 28);
  prompt_status->transform.position = {w / 2.0f, 558.0f, 0};
  prompt_status->setColour({0.75f, 0.75f, 0.75f, 1.0f});

  auto* footer = new Arriba::Primitives::Text("A: select     DPad: navigate     +: skip", 26);
  footer->transform.position = {w / 2.0f, (float)h - 28.0f, 0};
  footer->setColour({0.35f, 0.35f, 0.35f, 1.0f});

  Arriba::Elements::Button* btns[2] = {update_btn, skip_btn};
  Arriba::highlightedObject = update_btn;

  enum class Action { kNone, kUpdate, kSkip };
  Action action = Action::kNone;
  update_btn->registerCallback([&]() { action = Action::kUpdate; });
  skip_btn->registerCallback([&]() { action = Action::kSkip; });

  bool result = false;
  int sel = 0;

  while (appletMainLoop()) {
    if (action == Action::kUpdate) {
      action = Action::kNone;
      prompt_status->setText("Installing...");
      Arriba::drawFrame();
      if (InstallNsp()) {
        ModuleRestart();
        result = true;
        break;
      }
      prompt_status->setText("Install failed");
    } else if (action == Action::kSkip) {
      break;
    }

    NavigateButtons(btns, 2, &sel);
    if (Arriba::Input::buttonDown(Arriba::Input::PlusButtonSwitch)) break;
    Arriba::drawFrame();
  }

  bg->destroy();
  title->destroy();
  msg->destroy();
  submsg->destroy();
  update_btn->destroy();
  skip_btn->destroy();
  prompt_status->destroy();
  footer->destroy();
  Arriba::highlightedObject = nullptr;

  return result;
}

void InitUi(int upload_fd, const std::string& ip, bool module_running, const Version& version) {
  g_module_running  = module_running;
  g_running_version = version;

  RegisterStatusCallback([](const char* msg) {
    if (status_text) status_text->setText(msg);
  });
  RegisterVersionCallback([]() { UpdateVersionText(); });

  int w = Arriba::Graphics::windowWidth;
  int h = Arriba::Graphics::windowHeight;

  auto* bg = new Arriba::Primitives::Quad(0, 0, w, h, Arriba::Graphics::Pivot::topLeft);
  bg->setColour({0.1f, 0.1f, 0.12f, 1.0f});

  auto* title = new Arriba::Primitives::Text("NX-MCP Launcher", 58);
  title->transform.position = {w / 2.0f, 55.0f, 0};
  title->setColour({1, 1, 1, 1});

  auto* ip_text = new Arriba::Primitives::Text(ip.c_str(), 30);
  ip_text->transform.position = {w / 2.0f, 98.0f, 0};
  ip_text->setColour({0.6f, 0.6f, 0.6f, 1.0f});

  char mcp_buf[32];
  snprintf(mcp_buf, sizeof(mcp_buf), "MCP: %u", kServerPort);
  auto* mcp_text = new Arriba::Primitives::Text(mcp_buf, 26);
  mcp_text->transform.position = {w / 2.0f, 130.0f, 0};
  mcp_text->setColour({0.45f, 0.45f, 0.45f, 1.0f});

  char upload_buf[32];
  snprintf(upload_buf, sizeof(upload_buf), "Upload: %u%s",
    kUploadPort, upload_fd >= 0 ? "" : " (unavailable)");
  auto* upload_text = new Arriba::Primitives::Text(upload_buf, 26);
  upload_text->transform.position = {w / 2.0f, 158.0f, 0};
  upload_text->setColour({0.45f, 0.45f, 0.45f, 1.0f});

  version_text = new Arriba::Primitives::Text("", 26);
  version_text->transform.position = {w / 2.0f, 186.0f, 0};
  UpdateVersionText();

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

  boot2_text = new Arriba::Primitives::Text("", 32);
  boot2_text->transform.position = {w / 2.0f, 535.0f, 0};
  UpdateBoot2Text();

  status_text = new Arriba::Primitives::Text("", 30);
  status_text->transform.position = {w / 2.0f, 578.0f, 0};
  status_text->setColour({0.75f, 0.75f, 0.75f, 1.0f});

  auto* footer = new Arriba::Primitives::Text("A: select     DPad: navigate     +: exit", 26);
  footer->transform.position = {w / 2.0f, (float)h - 28.0f, 0};
  footer->setColour({0.35f, 0.35f, 0.35f, 1.0f});

  Arriba::highlightedObject = buttons[0];
}

bool HandleUiInput(int upload_fd) {
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

  NavigateButtons(buttons, 3, &selected);
  return Arriba::Input::buttonDown(Arriba::Input::PlusButtonSwitch);
}
