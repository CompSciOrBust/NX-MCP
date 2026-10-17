#pragma once
#include <map>
#include <string>

namespace http {

struct Request {
    std::string method;
    std::string path;
    std::string query_string;
    std::map<std::string, std::string> headers;
    std::string body;

    std::string header(const std::string& name) const {
        auto it = headers.find(name);
        return it != headers.end() ? it->second : "";
    }
};

struct Response {
    int status = 200;
    std::string content_type;
    std::string body;

    static Response Ok(std::string body, std::string ct = "application/json") {
        return {200, std::move(ct), std::move(body)};
    }
    static Response NoContent()        { return {204, {}, {}}; }
    static Response NotFound()         { return {404, "text/plain", "Not Found"}; }
    static Response MethodNotAllowed() { return {405, "text/plain", "Method Not Allowed"}; }
    static Response BadRequest(std::string msg = "Bad Request") {
        return {400, "text/plain", std::move(msg)};
    }
};

}  // namespace http
