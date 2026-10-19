// PayLoader - Which boot chain the console uses, and ps5-autoloader's list on disk.
// SPDX-License-Identifier: GPL-3.0-or-later
//
// After the exploit, itsPLK's ps5-autoloader looks for ps5_autoloader/autoload.txt
// on USB drives (/mnt/usb0 to /mnt/usb7) first, then in /data; folders named
// ps5_autoloader_<TITLE_ID> come before the generic one. It loads the files
// that list names, with "!<ms>" waits between them. When it finds no list, it
// starts pldmgr, and pldmgr's own list (when its autoload is on) is the chain.
// When the list names pldmgr, both run.
//
//   const Chain chain = detect_chain();
//   if (chain.kind == ChainKind::autoloader) edit(chain.directory);
//
// root prefixes every path, for tests; the app passes "".

#pragma once

#include "payloader/autoload_list.hpp"

#include <string>
#include <vector>

namespace hui::payloader
{

enum class ChainKind : unsigned char
{
    unknown,    // /data cannot be read from the app (etaHEN's "Allow /data in sandbox" is off)
    autoloader, // ps5-autoloader's list in directory
    pldmgr,     // no list found: the autoloader starts pldmgr, whose own list is the chain
};

struct Chain
{
    ChainKind kind = ChainKind::unknown;
    std::string directory;                  // the folder of the list in use (without root)
    std::vector<std::string> title_folders; // ps5_autoloader_<TITLE_ID> folders that have a list
    bool also_pldmgr = false;               // the list starts pldmgr too
    bool usb_list = false;                  // the list in use is on a USB drive
    // The first USB stick with ps5_autoloader/autoload.txt (on) or autoload.txt_disabled (off).
    std::string usb_drive; // "/mnt/usb0", "" when none
    bool usb_on = false;
    bool usb_off = false;
};

Chain detect_chain(const std::string &root = {});

// Switches the stick's list on (autoload.txt_disabled -> autoload.txt) or off (the other way).
// Refused when both names are there. Returns an error, or "".
std::string switch_usb_list(const std::string &root, const std::string &usb, bool on);

struct AutoloaderList
{
    std::vector<AutoloadEntry> entries;
    std::vector<std::string> files; // .elf and .bin files in the folder, and every file the list
                                    // names that exists, sorted
    std::string text;               // autoload.txt as read
};

// Reads directory/autoload.txt and the payload files next to it.
bool read_autoloader(const std::string &root, const std::string &directory, AutoloaderList &out);

// Writes the list (atomically). Before that, the list as it is on disk is kept as
// autoload.txt.bak-payloader-<date> unless today's backup exists already, so the
// first state of a day can always be restored. Returns an error, or "".
std::string write_autoloader(const std::string &root, const std::string &directory,
                             const std::vector<AutoloadEntry> &entries, const std::string &date);

// Copies a payload file into the list's folder as name. Refused when another file
// with that name is there, unless replace (a newer build of the same payload);
// the same file again is fine. Returns an error, or "".
std::string copy_into_chain(const std::string &root, const std::string &directory,
                            const std::string &source, const std::string &name,
                            bool replace = false);

// Copies the list in directory and the files it names (next to it) to <usb>/ps5_autoloader on the
// first USB drive the app can write (/mnt/usb0 to /mnt/usb7), as autoload.txt_disabled: switched
// off, so the stick can stay in. Entries with a full path stay as they are. A switched-off list
// already there is kept as autoload.txt_disabled.bak-payloader-<date> first; a switched-on one is
// not replaced. Sets usb to
// the drive used. Returns an error, or "".
std::string make_recovery_usb(const std::string &root, const std::string &directory,
                              const std::string &date, std::string &usb);

// "20261019" for today, local time.
std::string today();

} // namespace hui::payloader
