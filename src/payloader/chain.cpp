// PayLoader - Which boot chain the console uses, and ps5-autoloader's list on disk.
// SPDX-License-Identifier: GPL-3.0-or-later

#include "payloader/chain.hpp"

#include "core/save_file.hpp"
#include "payloader/versions.hpp"

#include <algorithm>
#include <sys/stat.h>
#include <unistd.h>
#include <cctype>
#include <ctime>

namespace hui::payloader
{

namespace
{

constexpr const char *kGeneric = "ps5_autoloader";
constexpr const char *kDisabledName = "autoload.txt_disabled";
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
    std::vector<std::string> usbs;
    for (int i = 0; i < 8; ++i)
        usbs.push_back("/mnt/usb" + std::to_string(i));

    // A USB stick with the autoloader's list, switched on (autoload.txt) or off
    // (autoload.txt_disabled).
    for (const std::string &usb : usbs)
    {
        const std::string folder = usb + "/" + kGeneric;
        const bool on = has_list(root, folder);
        std::string off_text;
        const bool off = save::read_file(root + folder + "/" + kDisabledName, &off_text, kMaxList);
        if (on || off)
        {
            chain.usb_drive = usb;
            chain.usb_on = on;
            chain.usb_off = off;
            break;
        }
    }

    // ps5-unified-autoloader's order (its find_autoload_config): the title folders on USB, the
    // title folder in /data, the general folder on USB, the general folder in /data. A title folder
    // applies only to the app the autoloader runs under, which this app cannot know, so the general
    // folders come first here; a title folder counts only when there is no general list, and is
    // the only one at its place.
    std::vector<std::pair<std::string, bool>> steps; // place, title folders (else the general one)
    for (const std::string &usb : usbs)
        steps.emplace_back(usb, false);
    steps.emplace_back("/data", false);
    for (const std::string &usb : usbs)
        steps.emplace_back(usb, true);
    steps.emplace_back("/data", true);
    for (const auto &[place, titled] : steps)
    {
        std::vector<std::string> titles;
        bool generic = false;
        lists_at(root, place, titles, generic);
        chain.title_folders.insert(chain.title_folders.end(), titles.begin(), titles.end());
        std::string directory;
        if (titled && titles.size() == 1)
            directory = titles.front();
        else if (!titled && generic)
            directory = place + "/" + kGeneric;
        if (directory.empty())
            continue;
        chain.kind = ChainKind::autoloader;
        chain.directory = directory;
        chain.usb_list = place != "/data";
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

std::string switch_usb_list(const std::string &root, const std::string &usb, bool on)
{
    const std::string folder = root + usb + "/" + kGeneric;
    const std::string from = folder + "/" + (on ? kDisabledName : "autoload.txt");
    const std::string to = folder + "/" + (on ? "autoload.txt" : kDisabledName);
    std::string there;
    if (save::read_file(to, &there, kMaxList))
        return std::string("The stick has both autoload.txt and ") + kDisabledName +
               ": rename or remove one on a PC first";
    if (::rename(from.c_str(), to.c_str()) != 0)
        return "The file on the USB stick could not be renamed";
    return {};
}

bool read_autoloader(const std::string &root, const std::string &directory, AutoloaderList &out)
{
    AutoloaderList list;
    if (!save::read_file(root + directory + "/autoload.txt", &list.text, kMaxList))
        return false;
    list.entries = parse_autoload_lines(list.text);
    // Listing a folder can come back empty in the app's sandbox on the console while the files are
    // there (the kit notes the same for /app0), so every file the list names is also checked on its
    // own.
    for (const std::string &name : save::list_files(root + directory))
        if (ends_with_nocase(name, ".elf") || ends_with_nocase(name, ".bin"))
            list.files.push_back(name);
    for (const AutoloadEntry &entry : list.entries)
    {
        struct stat info
        {
        };
        if (entry.wait || entry.target.empty())
            continue;
        const std::string path = entry.target.front() == '/'
                                     ? root + entry.target
                                     : root + directory + "/" + entry.target;
        if (::stat(path.c_str(), &info) == 0 && S_ISREG(info.st_mode))
            list.files.push_back(entry.target);
    }
    std::sort(list.files.begin(), list.files.end());
    list.files.erase(std::unique(list.files.begin(), list.files.end()), list.files.end());
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
                            const std::string &source, const std::string &name, bool replace)
{
    if (name.empty() || name.find('/') != std::string::npos || name == "autoload.txt")
        return "Not a payload file name";
    std::string data;
    if (!save::read_file(root + source, &data, kMaxPayloadFile))
        return "The payload file cannot be read";
    const std::string target = root + directory + "/" + name;
    std::string there;
    if (save::read_file(target, &there, kMaxPayloadFile))
    {
        if (there == data)
            return {};
        if (!replace)
            return "Another " + name + " is in the folder already";
    }
    const std::string error = save::write_atomic(target, data);
    return error.empty() ? std::string() : name + " could not be copied";
}

std::string make_recovery_usb(const std::string &root, const std::string &directory,
                              const std::string &date, std::string &usb)
{
    AutoloaderList list;
    if (!read_autoloader(root, directory, list))
        return "The list cannot be read";
    std::string drive;
    for (int i = 0; i < 8 && drive.empty(); ++i)
    {
        const std::string place = "/mnt/usb" + std::to_string(i);
        struct stat info
        {
        };
        if (::stat((root + place).c_str(), &info) != 0 || !S_ISDIR(info.st_mode))
            continue;
        // Writable for this app: a test file in the folder the copy goes to.
        const std::string folder = root + place + "/" + kGeneric;
        save::ensure_directory(folder);
        if (save::write_atomic(folder + "/.payloader-test", "1\n").empty())
        {
            ::unlink((folder + "/.payloader-test").c_str());
            drive = place;
        }
    }
    if (drive.empty())
        return "No USB drive this app can write: plug one in (exFAT or FAT32)";
    if (drive + "/" + kGeneric == directory)
        return "The list in use is on this USB drive already";
    const std::string target = root + drive + "/" + kGeneric;
    std::string existing;
    if (save::read_file(target + "/autoload.txt", &existing, kMaxList))
        return "The stick's list is switched on: switch it off first, so the copy cannot replace "
               "it";
    if (save::read_file(target + "/" + kDisabledName, &existing, kMaxList))
    {
        std::string kept;
        if (!save::read_file(target + "/" + kDisabledName + ".bak-payloader-" + date, &kept,
                             kMaxList) &&
            !save::write_atomic(target + "/" + kDisabledName + ".bak-payloader-" + date, existing)
                 .empty())
            return "The list on the USB drive could not be backed up";
    }
    for (const AutoloadEntry &entry : list.entries)
    {
        if (entry.wait || entry.target.empty() || entry.target.front() == '/' ||
            entry.target.front() == '#' || entry.target.front() == '@')
            continue;
        std::string data;
        if (!save::read_file(root + directory + "/" + entry.target, &data, kMaxPayloadFile))
            return entry.target + " cannot be read";
        if (!save::write_atomic(target + "/" + entry.target, data).empty())
            return entry.target + " could not be copied to the USB drive";
    }
    // Stored switched off: the stick can stay in. Switch it on in PayLoader, or rename the file to
    // autoload.txt on a PC, when the chain in /data no longer starts.
    if (!save::write_atomic(target + "/" + kDisabledName, list.text).empty())
        return "The list could not be written to the USB drive";
    usb = drive;
    return {};
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
