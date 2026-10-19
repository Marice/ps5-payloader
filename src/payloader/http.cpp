// PayLoader - One HTTP/1.1 request to a server on this console.
// SPDX-License-Identifier: GPL-3.0-or-later

#include "payloader/http.hpp"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cctype>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace hui::payloader
{

namespace
{

#ifdef MSG_NOSIGNAL
constexpr int kSendFlags = MSG_NOSIGNAL;
#else
constexpr int kSendFlags = 0;
#endif

constexpr std::size_t kMaxHead = 16 * 1024;

using Clock = std::chrono::steady_clock;

bool equals_nocase(std::string_view a, std::string_view b)
{
    if (a.size() != b.size())
        return false;
    for (std::size_t i = 0; i < a.size(); ++i)
        if (std::tolower(static_cast<unsigned char>(a[i])) !=
            std::tolower(static_cast<unsigned char>(b[i])))
            return false;
    return true;
}

std::string_view trim(std::string_view text)
{
    while (!text.empty() && (text.front() == ' ' || text.front() == '\t'))
        text.remove_prefix(1);
    while (!text.empty() && (text.back() == ' ' || text.back() == '\t' || text.back() == '\r'))
        text.remove_suffix(1);
    return text;
}

bool parse_size(std::string_view text, int base, std::size_t &out)
{
    if (text.empty() || text.size() > 16)
        return false;
    std::size_t value = 0;
    for (const char c : text)
    {
        int digit = -1;
        if (c >= '0' && c <= '9')
            digit = c - '0';
        else if (base == 16 && c >= 'a' && c <= 'f')
            digit = c - 'a' + 10;
        else if (base == 16 && c >= 'A' && c <= 'F')
            digit = c - 'A' + 10;
        if (digit < 0)
            return false;
        value = value * static_cast<std::size_t>(base) + static_cast<std::size_t>(digit);
    }
    out = value;
    return true;
}

// "1a;name=value\r\n<26 bytes>\r\n ... 0\r\n\r\n"
bool unchunk(std::string_view raw, std::string &out, std::size_t max_body)
{
    std::size_t at = 0;
    for (;;)
    {
        const std::size_t line_end = raw.find("\r\n", at);
        if (line_end == std::string_view::npos)
            return false;
        std::string_view size_text = raw.substr(at, line_end - at);
        const std::size_t extension = size_text.find(';');
        if (extension != std::string_view::npos)
            size_text = size_text.substr(0, extension);
        std::size_t size = 0;
        if (!parse_size(trim(size_text), 16, size))
            return false;
        at = line_end + 2;
        if (size == 0)
            return true; // trailers, if any, are not needed
        if (size > max_body || out.size() + size > max_body || raw.size() - at < size + 2)
            return false;
        out.append(raw.substr(at, size));
        at += size;
        if (raw.substr(at, 2) != "\r\n")
            return false;
        at += 2;
    }
}

class LoopbackTransport final : public Transport
{
  public:
    explicit LoopbackTransport(int port) : port_(port)
    {
    }

    Reply send(const Request &request) override
    {
        Reply reply;
        const Clock::time_point deadline =
            Clock::now() + std::chrono::milliseconds(request.timeout_ms);
        const int fd = connect_loopback();
        if (fd < 0)
            return reply;
        const std::string wire = format_request(request);
        std::string raw;
        if (send_all(fd, wire, deadline))
            receive(fd, raw, deadline);
        close(fd);
        if (!parse_reply(raw, reply))
            reply = Reply{};
        return reply;
    }

  private:
    int connect_loopback() const
    {
        const int fd = socket(AF_INET, SOCK_STREAM, 0);
        if (fd < 0)
            return -1;
#ifdef SO_NOSIGPIPE
        const int on = 1;
        setsockopt(fd, SOL_SOCKET, SO_NOSIGPIPE, &on, sizeof(on));
#endif
        sockaddr_in address{};
        address.sin_family = AF_INET;
        address.sin_port = htons(static_cast<std::uint16_t>(port_));
        address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        // On loopback a refused or accepted connect answers at once; no time-out needed.
        if (connect(fd, reinterpret_cast<const sockaddr *>(&address), sizeof(address)) != 0)
        {
            close(fd);
            return -1;
        }
        return fd;
    }

    static int remaining_ms(Clock::time_point deadline)
    {
        const auto left =
            std::chrono::duration_cast<std::chrono::milliseconds>(deadline - Clock::now()).count();
        return left > 0 ? static_cast<int>(left) : 0;
    }

    static bool wait(int fd, short events, Clock::time_point deadline)
    {
        pollfd entry{fd, events, 0};
        const int left = remaining_ms(deadline);
        return left > 0 && poll(&entry, 1, left) == 1 && (entry.revents & (events | POLLHUP)) != 0;
    }

    static bool send_all(int fd, std::string_view data, Clock::time_point deadline)
    {
        std::size_t sent = 0;
        while (sent < data.size())
        {
            if (!wait(fd, POLLOUT, deadline))
                return false;
            const ssize_t n = ::send(fd, data.data() + sent, data.size() - sent, kSendFlags);
            if (n <= 0)
                return false;
            sent += static_cast<std::size_t>(n);
        }
        return true;
    }

    // Reads until the server closes, the deadline passes or the cap is reached.
    static void receive(int fd, std::string &raw, Clock::time_point deadline)
    {
        char buffer[16 * 1024];
        while (raw.size() <= kMaxReply + kMaxHead)
        {
            if (!wait(fd, POLLIN, deadline))
                return;
            const ssize_t n = recv(fd, buffer, sizeof(buffer), 0);
            if (n <= 0)
                return;
            raw.append(buffer, static_cast<std::size_t>(n));
        }
    }

    int port_;
};

} // namespace

std::unique_ptr<Transport> make_loopback_transport(int port)
{
    return std::make_unique<LoopbackTransport>(port);
}

std::string format_request(const Request &request)
{
    std::string wire;
    wire.reserve(256 + request.body.size());
    wire.append(request.method).append(" ").append(request.target).append(" HTTP/1.1\r\n");
    wire += "Host: 127.0.0.1\r\nConnection: close\r\n";
    for (const auto &[name, value] : request.headers)
        wire.append(name).append(": ").append(value).append("\r\n");
    if (!request.body.empty() || request.method != "GET")
    {
        char length[48];
        std::snprintf(length, sizeof(length), "Content-Length: %zu\r\n", request.body.size());
        wire += length;
    }
    wire += "\r\n";
    wire += request.body;
    return wire;
}

bool parse_reply(std::string_view raw, Reply &out, std::size_t max_body)
{
    const std::size_t head_end = raw.find("\r\n\r\n");
    if (head_end == std::string_view::npos || head_end > kMaxHead)
        return false;
    const std::string_view head = raw.substr(0, head_end);
    const std::string_view rest = raw.substr(head_end + 4);

    // "HTTP/1.1 200 OK"
    const std::size_t first_end = head.find("\r\n");
    const std::string_view status_line = head.substr(0, first_end);
    if (status_line.size() < 12 || status_line.substr(0, 7) != "HTTP/1." || status_line[8] != ' ')
        return false;
    std::size_t status = 0;
    if (!parse_size(status_line.substr(9, 3), 10, status) || status < 100 || status > 599)
        return false;

    Reply reply;
    reply.status = static_cast<int>(status);
    bool chunked = false;
    bool have_length = false;
    std::size_t length = 0;
    std::size_t at = first_end == std::string_view::npos ? head.size() : first_end + 2;
    while (at < head.size())
    {
        std::size_t end = head.find("\r\n", at);
        if (end == std::string_view::npos)
            end = head.size();
        const std::string_view line = head.substr(at, end - at);
        at = end + 2;
        const std::size_t colon = line.find(':');
        if (colon == std::string_view::npos)
            continue;
        const std::string_view name = trim(line.substr(0, colon));
        const std::string_view value = trim(line.substr(colon + 1));
        if (equals_nocase(name, "Content-Length"))
        {
            if (!parse_size(value, 10, length))
                return false;
            have_length = true;
        }
        else if (equals_nocase(name, "Transfer-Encoding"))
        {
            chunked = equals_nocase(value, "chunked");
        }
        else if (equals_nocase(name, "Content-Type"))
        {
            reply.content_type = std::string(value);
        }
    }

    if (chunked)
    {
        if (!unchunk(rest, reply.body, max_body))
            return false;
    }
    else if (have_length)
    {
        if (length > max_body || rest.size() < length)
            return false;
        reply.body = std::string(rest.substr(0, length));
    }
    else
    {
        // Ends where the connection closed.
        if (rest.size() > max_body)
            return false;
        reply.body = std::string(rest);
    }
    out = std::move(reply);
    return true;
}

std::string url_encode(std::string_view text)
{
    static constexpr char kHex[] = "0123456789ABCDEF";
    std::string out;
    out.reserve(text.size() * 3);
    for (const char c : text)
    {
        const unsigned char u = static_cast<unsigned char>(c);
        if (std::isalnum(u) || c == '-' || c == '_' || c == '.' || c == '~')
        {
            out += c;
        }
        else
        {
            out += '%';
            out += kHex[u >> 4];
            out += kHex[u & 0x0F];
        }
    }
    return out;
}

} // namespace hui::payloader
