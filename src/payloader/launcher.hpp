// PayLoader - Starting the pldmgr this app carries, without pldmgr's own front-end.
// SPDX-License-Identifier: GPL-3.0-or-later
//
// PayLoader brings pldmgr (itsPLK's Payload Manager, GPL-3.0) in assets/payload/pldmgr.elf, with
// its version in pldmgr.txt (tools/bundle-pldmgr.sh). When pldmgr does not run, PayLoader can send
// that ELF to the ELF loader on port 9021. Before that it switches off pldmgr's own front-end in its
// config file: no home screen icon of its own (AUTO_INSTALL_APP) and no browser at start
// (AUTO_BROWSER_OPEN). Once running, pldmgr updates itself from its official repository.
//
//   set_payloader_paths("/app0/assets", "/download0/hui");
//   if (bundled_pldmgr().present) offer_start();

#pragma once

#include <string>
#include <string_view>

namespace hui::payloader
{

constexpr int kLoaderPort = 9021;
constexpr const char *kPldmgrConfig = "/data/pldmgr/pldmgr_config.txt";

// The app's assets folder and its own data folder (main.cpp sets them; tests point them elsewhere).
void set_payloader_paths(std::string assets, std::string data);
const std::string &assets_dir();
const std::string &data_dir();

struct BundledPldmgr
{
    bool present = false;
    std::string version; // "0.6.3"
    std::string path;    // the ELF
};

// Read once, then remembered.
const BundledPldmgr &bundled_pldmgr();

// pldmgr's config text with AUTO_INSTALL_APP=0 and AUTO_BROWSER_OPEN=0; every other line stays as it
// was. An empty text gets pldmgr 0.6's defaults with those two off.
std::string config_without_front_end(std::string_view text);

// Sends data to a loopback port and closes, as an ELF loader takes a payload. True when every byte
// went out.
bool push_to_loader(int port, std::string_view data);

} // namespace hui::payloader
