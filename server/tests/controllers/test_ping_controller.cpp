#define CATCH_CONFIG_MAIN
#include <catch2/catch.hpp>
#include "../../controllers/ping_controller.hpp"

static http::Request MakeReq(std::string method, std::string path) {
  http::Request r;
  r.method = std::move(method);
  r.path   = std::move(path);
  r.fd     = -1;
  return r;
}

TEST_CASE("Ping: GET /ping returns 200 pong") {
  controllers::PingController ctrl;
  auto resp = ctrl.Handle(MakeReq("GET", "/ping"));
  REQUIRE(resp.status == 200);
  REQUIRE(resp.body == "pong");
  REQUIRE(resp.content_type == "text/plain");
}

TEST_CASE("Ping: wrong method returns 405") {
  controllers::PingController ctrl;
  REQUIRE(ctrl.Handle(MakeReq("POST", "/ping")).status == 405);
}

TEST_CASE("Ping: wrong path returns 404") {
  controllers::PingController ctrl;
  REQUIRE(ctrl.Handle(MakeReq("GET", "/ping/extra")).status == 404);
}

TEST_CASE("Ping: Path() returns /ping") {
  controllers::PingController ctrl;
  REQUIRE(ctrl.Path() == "/ping");
}
