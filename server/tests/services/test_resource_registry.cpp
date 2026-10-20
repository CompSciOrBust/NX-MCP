#define CATCH_CONFIG_MAIN
#include <catch2/catch.hpp>
#include "../../services/resource_registry.hpp"

static services::ResourceRegistry& reg() {
  return services::ResourceRegistry::Instance();
}

// ── Get ───────────────────────────────────────────────────────────────────────

TEST_CASE("ResourceRegistry: Get returns nullptr for unknown URI") {
  REQUIRE(reg().Get("nxmcp://docs/no-such-doc") == nullptr);
}

TEST_CASE("ResourceRegistry: Get returns resource for built-in URI") {
  auto* r = reg().Get("nxmcp://docs/file-transfer");
  REQUIRE(r != nullptr);
  REQUIRE(r->uri == "nxmcp://docs/file-transfer");
}

TEST_CASE("ResourceRegistry: Get resource fields are fully populated") {
  auto* r = reg().Get("nxmcp://docs/file-transfer");
  REQUIRE_FALSE(r->name.empty());
  REQUIRE_FALSE(r->description.empty());
  REQUIRE_FALSE(r->mime_type.empty());
  REQUIRE_FALSE(r->text.empty());
}

// ── All ───────────────────────────────────────────────────────────────────────

TEST_CASE("ResourceRegistry: All returns the five built-in docs") {
  REQUIRE(reg().All().size() >= 5);
}

TEST_CASE("ResourceRegistry: All entries have non-empty URIs") {
  for (auto* r : reg().All())
    REQUIRE_FALSE(r->uri.empty());
}

TEST_CASE("ResourceRegistry: All built-in docs are present") {
  auto all = reg().All();
  auto has = [&](const std::string& uri) {
    for (auto* r : all) if (r->uri == uri) return true;
    return false;
  };
  REQUIRE(has("nxmcp://docs/file-transfer"));
  REQUIRE(has("nxmcp://docs/nsp-install"));
  REQUIRE(has("nxmcp://docs/save-data"));
  REQUIRE(has("nxmcp://docs/process-model"));
  REQUIRE(has("nxmcp://docs/sysmodule-install"));
}

// ── Register ──────────────────────────────────────────────────────────────────

TEST_CASE("ResourceRegistry: Register makes resource retrievable via Get") {
  reg().Register({"nxmcp://docs/test-reg", "Test", "desc", "text/plain", "body"});
  auto* r = reg().Get("nxmcp://docs/test-reg");
  REQUIRE(r != nullptr);
  REQUIRE(r->name == "Test");
  REQUIRE(r->description == "desc");
  REQUIRE(r->mime_type == "text/plain");
  REQUIRE(r->text == "body");
}

TEST_CASE("ResourceRegistry: Register increases All count") {
  size_t before = reg().All().size();
  reg().Register({"nxmcp://docs/test-count", "Count", "", "text/plain", ""});
  REQUIRE(reg().All().size() == before + 1);
}

TEST_CASE("ResourceRegistry: Register does not overwrite an existing URI") {
  reg().Register({"nxmcp://docs/test-nooverwrite", "Original", "", "text/plain", "first"});
  reg().Register({"nxmcp://docs/test-nooverwrite", "Replaced", "", "text/plain", "second"});
  auto* r = reg().Get("nxmcp://docs/test-nooverwrite");
  REQUIRE(r->name == "Original");
  REQUIRE(r->text == "first");
}
