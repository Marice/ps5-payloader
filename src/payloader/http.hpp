// PayLoader - One HTTP/1.1 request to a server on this console.
// SPDX-License-Identifier: GPL-3.0-or-later
//
// pldmgr listens on port 8084. A request goes out on a fresh connection with
// "Connection: close"; the reply is read to its end, with or without a
// Content-Length, chunked or not. Replies above a cap count as failed rather
// than being cut, so a half list never looks like a whole one.
//
//   std::unique_ptr<Transport> http = make_loopback_transport(8084);
//   Request request{"GET", "/version"};
//   const Reply reply = http->send(request);
//   if (reply.status == 200) use(reply.body);

#pragma once

#include <cstddef>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace hui::payloader
{

struct Request
{
    std::string method; // "GET", "POST"
    std::string target; // path and query, already encoded
    std::vector<std::pair<std::string, std::string>> headers;
    std::string body;
    int timeout_ms = 2000;
};

struct Reply
{
    int status = -1; // -1: no answer (refused, timed out, malformed or too large)
    std::string content_type;
    std::string body;
};

class Transport
{
  public:
    virtual ~Transport() = default;
    virtual Reply send(const Request &request) = 0;
};

constexpr std::size_t kMaxReply = 2u << 20;

// A transport to 127.0.0.1:port.
std::unique_ptr<Transport> make_loopback_transport(int port);

// The request as it goes on the wire.
std::string format_request(const Request &request);

// Reads a whole raw answer (head and body). False when it is malformed,
// incomplete, or its body is larger than max_body.
bool parse_reply(std::string_view raw, Reply &out, std::size_t max_body = kMaxReply);

// Percent-encodes a query value (RFC 3986 unreserved characters stay).
std::string url_encode(std::string_view text);

} // namespace hui::payloader
