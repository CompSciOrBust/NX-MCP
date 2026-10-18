#include "version.hpp"

bool QueryModuleVersion(Version* out) {
  static constexpr SmServiceName kServiceName = smEncodeName("nx:mcp");

  bool has = false;
  tipcDispatchInOut(smGetServiceSessionTipc(), 65100, kServiceName, has);
  if (!has) return false;

  Service svc = {};
  if (R_FAILED(smGetService(&svc, "nx:mcp"))) return false;

  Version v = {};
  Result rc = serviceDispatchOut(&svc, 0, v);
  serviceClose(&svc);

  if (R_FAILED(rc)) return false;
  *out = v;
  return true;
}
