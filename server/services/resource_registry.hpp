#pragma once
#include <string>
#include <unordered_map>
#include <vector>

namespace services {

struct Resource {
  std::string uri;
  std::string name;
  std::string description;
  std::string mime_type;
  std::string text;
};

class ResourceRegistry {
 public:
  static ResourceRegistry& Instance();
  void Register(Resource resource);
  const Resource* Get(const std::string& uri) const;
  std::vector<const Resource*> All() const;

 private:
  ResourceRegistry();
  std::unordered_map<std::string, Resource> resources_;
};

}  // namespace services
