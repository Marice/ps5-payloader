// PayLoader - Starting the pldmgr this app carries, without pldmgr's own front-end.
// SPDX-License-Identifier: GPL-3.0-or-later

#include "payloader/launcher.hpp"

#include "core/save_file.hpp"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

#include <cstdint>
#include <mutex>

namespace hui::payloader
{

namespace
{

#ifdef MSG_NOSIGNAL
constexpr int kSendFlags = MSG_NOSIGNAL;
#else
constexpr int kSendFlags = 0;
#endif

std::mutex g_paths_lock;
std::string g_assets = "/app0/assets";
std::string g_data = "/download0/hui";

// pldmgr 0.6's defaults (its include/config.h), in the order it writes them.
constexpr const char *kDefaults = "AUTOLOAD_ENABLED=0\n"
                                  "LAST_REPOSITORY_UPDATE=0\n"
                                  "AUTO_BROWSER_OPEN=1\n"
                                  "AUTOLOAD_DELAY=5\n"
                                  "SCAN_USB_PAYLOADS=0\n"
                                  "AUTO_INSTALL_APP=1\n"
                                  "MULTI_SOURCES_ENABLED=0\n"
                                  "KILL_DISC_PLAYER_ON_STARTUP=1\n";

std::string value_of(const std::string &text, std::string_view key)
{
    std::size_t start = 0;
    while (start < text.size())
    {
        std::size_t end = text.find('\n', start);
        if (end == std::string::npos)
            end = text.size();
        const std::string_view line(text.data() + start, end - start);
        if (line.size() > key.size() && line.substr(0, key.size()) == key &&
            line[key.size()] == '=')
        {
            std::string value(line.substr(key.size() + 1));
            while (!value.empty() && (value.back() == '\r' || value.back() == ' '))
                value.pop_back();
            return value;
        }
        start = end + 1;
    }
    return {};
}

} // namespace

void set_payloader_paths(std::string assets, std::string data)
{
    std::lock_guard<std::mutex> lock(g_paths_lock);
    g_assets = std::move(assets);
    g_data = std::move(data);
}

const std::string &assets_dir()
{
    std::lock_guard<std::mutex> lock(g_paths_lock);
    return g_assets;
}

const std::string &data_dir()
{
    std::lock_guard<std::mutex> lock(g_paths_lock);
    return g_data;
}

const BundledPldmgr &bundled_pldmgr()
{
    static const BundledPldmgr found = []
    {
        BundledPldmgr b;
        const std::string folder = assets_dir() + "/payload";
        std::string info;
        std::string head;
        b.path = folder + "/pldmgr.elf";
        if (save::read_file(folder + "/pldmgr.txt", &info, 4096) &&
            save::read_file(b.path, &head, 64u << 20) && head.size() > 64 &&
            head.compare(0, 4,
                         "\x7f"
                         "ELF") == 0)
        {
            b.present = true;
            b.version = value_of(info, "version");
        }
        return b;
    }();
    return found;
}

std::string config_without_front_end(std::string_view text)
{
    std::string source = text.empty() ? std::string(kDefaults) : std::string(text);
    if (!source.empty() && source.back() != '\n')
        source += '\n';
    std::string out;
    bool icon = false;
    bool browser = false;
    std::size_t start = 0;
    while (start < source.size())
    {
        std::size_t end = source.find('\n', start);
        if (end == std::string::npos)
            end = source.size();
        std::string line = source.substr(start, end - start);
        start = end + 1;
        if (line.rfind("AUTO_INSTALL_APP=", 0) == 0)
        {
            line = "AUTO_INSTALL_APP=0";
            icon = true;
        }
        else if (line.rfind("AUTO_BROWSER_OPEN=", 0) == 0)
        {
            line = "AUTO_BROWSER_OPEN=0";
            browser = true;
        }
        out += line;
        out += '\n';
    }
    if (!browser)
        out += "AUTO_BROWSER_OPEN=0\n";
    if (!icon)
        out += "AUTO_INSTALL_APP=0\n";
    return out;
}

bool push_to_loader(int port, std::string_view data)
{
    const int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0)
        return false;
    timeval timeout{};
    timeout.tv_sec = 5;
    setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));
#ifdef SO_NOSIGPIPE
    const int on = 1;
    setsockopt(fd, SOL_SOCKET, SO_NOSIGPIPE, &on, sizeof(on));
#endif
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_port = htons(static_cast<std::uint16_t>(port));
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    bool sent = connect(fd, reinterpret_cast<const sockaddr *>(&address), sizeof(address)) == 0;
    std::size_t done = 0;
    while (sent && done < data.size())
    {
        const ssize_t n = ::send(fd, data.data() + done, data.size() - done, kSendFlags);
        if (n <= 0)
            sent = false;
        else
            done += static_cast<std::size_t>(n);
    }
    close(fd);
    return sent;
}

} // namespace hui::payloader
