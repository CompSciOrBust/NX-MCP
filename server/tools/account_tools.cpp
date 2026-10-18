#include "account_tools.hpp"
#include "tool_registry.hpp"
#include "tool_util.hpp"
#include <switch.h>
#include <cstdio>
#include <string>

namespace tools {

static bool kRegistered =
    ToolRegistry::Instance().Register(std::make_unique<ListAccountsTool>());

std::string ListAccountsTool::Name() const { return "list_accounts"; }

std::string ListAccountsTool::Description() const {
  return "Lists all user accounts on the console with their UIDs and nicknames.";
}

nlohmann::json ListAccountsTool::InputSchema() const {
  return {
    {"type", "object"},
    {"properties", nlohmann::json::object()},
    {"required", nlohmann::json::array()}
  };
}

ToolResult ListAccountsTool::Execute(const nlohmann::json&) {
  Result rc = accountInitialize(AccountServiceType_System);
  if (R_FAILED(rc))
    return {"accountInitialize failed: " + FmtRc(rc), true};

  AccountUid uids[ACC_USER_LIST_SIZE];
  s32 count = 0;
  rc = accountListAllUsers(uids, ACC_USER_LIST_SIZE, &count);
  if (R_FAILED(rc)) {
    accountExit();
    return {"accountListAllUsers failed: " + FmtRc(rc), true};
  }

  std::string result;
  char line[96];

  snprintf(line, sizeof(line), "%-33s  %s\n", "UID", "Nickname");
  result += line;
  snprintf(line, sizeof(line), "%-33s  %s\n",
           "---------------------------------", "--------------------------------");
  result += line;

  for (s32 i = 0; i < count; i++) {
    AccountProfile profile{};
    if (R_FAILED(accountGetProfile(&profile, uids[i]))) continue;

    AccountProfileBase base{};
    accountProfileGet(&profile, nullptr, &base);
    accountProfileClose(&profile);

    snprintf(line, sizeof(line), "%016llX-%016llX  %s\n",
             (unsigned long long)uids[i].uid[0],
             (unsigned long long)uids[i].uid[1],
             base.nickname);
    result += line;
  }

  accountExit();
  return {result, false};
}

}  // namespace tools
