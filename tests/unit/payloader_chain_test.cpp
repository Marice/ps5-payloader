// PayLoader - Tests for finding the boot chain and editing ps5-autoloader's list.
// SPDX-License-Identifier: GPL-3.0-or-later

#include "payloader/chain.hpp"

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

namespace hui::payloader
{
namespace
{

namespace fs = std::filesystem;

// Marice's list on 2026-10-19, as ps5-autoloader reads it.
constexpr const char *kRealList = "websrv_v0.34.elf\n!3000\netaHEN-13.60.elf\n!12000\n"
                                  "shadowmountplus_1.7beta5.elf\n!3000\nnanodns.elf\n!3000\n"
                                  "ps5-exporter.elf\n!3000\nps5-homeassistant.elf\n!3000\n"
                                  "ps5-signal.elf\n";

class ChainTest : public ::testing::Test
{
  protected:
    void SetUp() override
    {
        root_ = (fs::temp_directory_path() /
                 ("payloader-chain-" +
                  std::to_string(::testing::UnitTest::GetInstance()->random_seed()) + "-" +
                  ::testing::UnitTest::GetInstance()->current_test_info()->name()))
                    .string();
        fs::remove_all(root_);
        fs::create_directories(root_ + "/data");
    }
    void TearDown() override
    {
        fs::remove_all(root_);
    }
    void write(const std::string &path, const std::string &text)
    {
        fs::create_directories(fs::path(root_ + path).parent_path());
        std::ofstream(root_ + path, std::ios::binary) << text;
    }
    std::string read(const std::string &path)
    {
        std::ifstream file(root_ + path, std::ios::binary);
        std::stringstream text;
        text << file.rdbuf();
        return text.str();
    }
    std::string root_;
};

TEST_F(ChainTest, FindsTheListInData)
{
    write("/data/ps5_autoloader/autoload.txt", kRealList);
    write("/data/pldmgr/autoload.txt", "local/etahen\n");
    const Chain chain = detect_chain(root_);
    EXPECT_EQ(chain.kind, ChainKind::autoloader);
    EXPECT_EQ(chain.directory, "/data/ps5_autoloader");
    EXPECT_FALSE(chain.also_pldmgr);
    EXPECT_FALSE(chain.usb_list);
}

TEST_F(ChainTest, PrefersAUsbDrive)
{
    write("/data/ps5_autoloader/autoload.txt", kRealList);
    write("/mnt/usb1/ps5_autoloader/autoload.txt", "pldmgr.elf\n");
    const Chain chain = detect_chain(root_);
    EXPECT_EQ(chain.directory, "/mnt/usb1/ps5_autoloader");
    EXPECT_TRUE(chain.usb_list);
    EXPECT_TRUE(chain.also_pldmgr);
}

TEST_F(ChainTest, NamesTitleFoldersAndEditsTheGenericOne)
{
    write("/data/ps5_autoloader/autoload.txt", kRealList);
    write("/data/ps5_autoloader_PPSA01650/autoload.txt", "a.elf\n");
    Chain chain = detect_chain(root_);
    EXPECT_EQ(chain.directory, "/data/ps5_autoloader");
    ASSERT_EQ(chain.title_folders.size(), 1u);
    EXPECT_EQ(chain.title_folders[0], "/data/ps5_autoloader_PPSA01650");

    fs::remove_all(root_ + "/data/ps5_autoloader");
    chain = detect_chain(root_);
    EXPECT_EQ(chain.directory, "/data/ps5_autoloader_PPSA01650"); // the only list there is
}

TEST_F(ChainTest, NoListMeansPldmgr)
{
    write("/data/pldmgr/autoload.txt", "local/etahen\n");
    EXPECT_EQ(detect_chain(root_).kind, ChainKind::pldmgr);
    fs::remove_all(root_ + "/data");
    EXPECT_EQ(detect_chain(root_).kind, ChainKind::unknown); // /data cannot be read
}

TEST_F(ChainTest, ReadsTheListAndItsFiles)
{
    write("/data/ps5_autoloader/autoload.txt", kRealList);
    write("/data/ps5_autoloader/ps5-signal.elf", "ELF");
    write("/data/ps5_autoloader/websrv_v0.34.elf", "ELF");
    write("/data/ps5_autoloader/notes.txt", "x");
    AutoloaderList list;
    ASSERT_TRUE(read_autoloader(root_, "/data/ps5_autoloader", list));
    ASSERT_EQ(list.entries.size(), 13u);
    EXPECT_EQ(list.entries[3], AutoloadEntry::pause(12000));
    EXPECT_EQ(list.files, (std::vector<std::string>{"ps5-signal.elf", "websrv_v0.34.elf"}));
    std::string again;
    ASSERT_TRUE(format_autoload_lines(list.entries, again));
    EXPECT_EQ(again, kRealList); // an unchanged list is written back byte for byte
}

TEST_F(ChainTest, KeepsTheFirstListOfTheDay)
{
    write("/data/ps5_autoloader/autoload.txt", kRealList);
    std::vector<AutoloadEntry> entries = parse_autoload_lines(kRealList);
    std::swap(entries[0], entries[2]);
    ASSERT_EQ(write_autoloader(root_, "/data/ps5_autoloader", entries, "20261019"), "");
    EXPECT_EQ(read("/data/ps5_autoloader/autoload.txt.bak-payloader-20261019"), kRealList);
    EXPECT_EQ(read("/data/ps5_autoloader/autoload.txt").rfind("etaHEN-13.60.elf\n", 0), 0u);

    // A second change the same day keeps the first backup.
    std::swap(entries[0], entries[2]);
    ASSERT_EQ(write_autoloader(root_, "/data/ps5_autoloader", entries, "20261019"), "");
    EXPECT_EQ(read("/data/ps5_autoloader/autoload.txt.bak-payloader-20261019"), kRealList);
    EXPECT_EQ(read("/data/ps5_autoloader/autoload.txt"), kRealList);
}

TEST_F(ChainTest, RefusesNamesTheAutoloaderCannotRead)
{
    write("/data/ps5_autoloader/autoload.txt", kRealList);
    EXPECT_NE(write_autoloader(root_, "/data/ps5_autoloader", {AutoloadEntry::payload("a\tb.elf")},
                               "20261019"),
              "");
    EXPECT_EQ(read("/data/ps5_autoloader/autoload.txt"), kRealList);
    EXPECT_FALSE(fs::exists(root_ + "/data/ps5_autoloader/autoload.txt.bak-payloader-20261019"));
}

TEST_F(ChainTest, CopiesAPayloadInUnlessTheNameIsTaken)
{
    write("/data/ps5_autoloader/autoload.txt", kRealList);
    write("/data/pldmgr/payloads/repo/x@1/x.elf", "NEW");
    EXPECT_EQ(copy_into_chain(root_, "/data/ps5_autoloader", "/data/pldmgr/payloads/repo/x@1/x.elf",
                              "x.elf"),
              "");
    EXPECT_EQ(read("/data/ps5_autoloader/x.elf"), "NEW");
    EXPECT_EQ(copy_into_chain(root_, "/data/ps5_autoloader", "/data/pldmgr/payloads/repo/x@1/x.elf",
                              "x.elf"),
              ""); // the same file again
    write("/data/ps5_autoloader/y.elf", "OLD");
    EXPECT_NE(copy_into_chain(root_, "/data/ps5_autoloader", "/data/pldmgr/payloads/repo/x@1/x.elf",
                              "y.elf"),
              "");
    EXPECT_EQ(read("/data/ps5_autoloader/y.elf"), "OLD");
}

} // namespace
} // namespace hui::payloader
