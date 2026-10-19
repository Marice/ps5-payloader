// PayLoader - Which boot chain the console uses, and ps5-autoloader's list on disk.
// SPDX-License-Identifier: GPL-3.0-or-later

#include "payloader/chain.hpp"

#include "core/save_file.hpp"
#include "payloader/versions.hpp"

#include <algorithm>
#include <cctype>
#include <ctime>

namespace hui::payloader
{

namespace
{

constexpr const char *kGeneric = "ps5_autoloader";
constexpr std::size_t kMaxList = 64 * 1024;
constexpr std::size_t kMaxPayloadFile = 64u << 20;

bool has_list(const std::string &root, const std::string &directory)
{
    std::string text;
    return save::read_file(root + directory + "/autoload.txt", &text, kMaxList);
}

bool ends_with_nocase(const std::string &name, const char *suffix)
{
    const std::size_t n = std::char_traits<char>::length(suffix);
    if (name.size() < n)
        return false;
    for (std::size_t i = 0; i < n; ++i)
        if (std::tolower(static_cast<unsigned char>(name[name.size() - n + i])) != suffix[i])
            return false;
    return true;
}

// The folders with a list at one place (a USB drive's root, or /data): title folders first.
void lists_at(const std::string &root, const std::string &place, std::vector<std::string> &titles,
              bool &generic)
{
    std::vector<std::string> names = save::list_files(root + place);
    std::sort(names.begin(), names.end());
    for (const std::string &name : names)
        if (name.rfind(std::string(kGeneric) + "_", 0) == 0 && has_list(root, place + "/" + name))
            titles.push_back(place + "/" + name);
    generic = has_list(root, place + "/" + kGeneric);
}

} // namespace

Chain detect_chain(const std::string &root)
{
    Chain chain;
    std::vector<std::string> places;
    for (int i = 0; i < 8; ++i)
        places.push_back("/mnt/usb" + std::to_string(i));
    places.push_back("/data");
    for (const std::string &place : places)
    {
        std::vector<std::string> titles;
        bool generic = false;
        lists_at(root, place, titles, generic);
        if (titles.empty() && !generic)
            continue;
        chain.kind = ChainKind::autoloader;
        chain.title_folders = titles;
        chain.usb_list = place != "/data";
        // Which title folder applies depends on the app the autoloader runs in, which this app
        // cannot know: with one title folder and no generic one that is the list; else the generic.
        chain.directory = generic || titles.size() != 1 ? place + "/" + kGeneric : titles.front();
        AutoloaderList list;
        if (read_autoloader(root, chain.directory, list))
            for (const AutoloadEntry &entry : list.entries)
                if (!entry.wait && is_system_payload(entry.target))
                    chain.also_pldmgr = true;
        return chain;
    }
    // Nothing found: /data readable means there is no list; unreadable means the app may not look.
    chain.kind = save::list_files(root + "/data").empty() ? ChainKind::unknown : ChainKind::pldmgr;
    return chain;
}

bool read_autoloader(const std::string &root, const std::string &directory, AutoloaderList &out)
{
    AutoloaderList list;
    if (!save::read_file(root + directory + "/autoload.txt", &list.text, kMaxList))
        return false;
    list.entries = parse_autoload_lines(list.text);
    for (const std::string &name : save::list_files(root + directory))
        if (ends_with_nocase(name, ".elf") || ends_with_nocase(name, ".bin"))
            list.files.push_back(name);
    std::sort(list.files.begin(), list.files.end());
    out = std::move(list);
    return true;
}

std::string write_autoloader(const std::string &root, const std::string &directory,
                             const std::vector<AutoloadEntry> &entries, const std::string &date)
{
    std::string text;
    if (!format_autoload_lines(entries, text))
        return "The list holds a name the autoloader cannot read";
    const std::string path = root + directory + "/autoload.txt";
    std::string current;
    if (!save::read_file(path, &current, kMaxList))
        return "autoload.txt cannot be read";
    if (current == text)
        return {};
    const std::string backup = path + ".bak-payloader-" + date;
    std::string existing;
    if (!save::read_file(backup, &existing, kMaxList))
    {
        const std::string error = save::write_atomic(backup, current);
        if (!error.empty())
            return "No backup could be made, so the list was not changed";
    }
    const std::string error = save::write_atomic(path, text);
    return error.empty() ? std::string() : "autoload.txt could not be written";
}

std::string copy_into_chain(const std::string &root, const std::string &directory,
                            const std::string &source, const std::string &name)
{
    if (name.empty() || name.find('/') != std::string::npos || name == "autoload.txt")
        return "Not a payload file name";
    std::string data;
    if (!save::read_file(root + source, &data, kMaxPayloadFile))
        return "The payload file cannot be read";
    const std::string target = root + directory + "/" + name;
    std::string there;
    if (save::read_file(target, &there, kMaxPayloadFile))
        return there == data ? std::string() : "Another " + name + " is in the folder already";
    const std::string error = save::write_atomic(target, data);
    return error.empty() ? std::string() : name + " could not be copied";
}

std::string today()
{
    const std::time_t now = std::time(nullptr);
    // std::localtime, not localtime_r: the console's libc has no localtime_r. Called from the
    // store's worker thread only.
    const std::tm *local = std::localtime(&now);
    char text[16] = "00000000";
    if (local != nullptr)
        std::strftime(text, sizeof(text), "%Y%m%d", local);
    return text;
}

} // namespace hui::payloader
