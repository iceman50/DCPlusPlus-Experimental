/* Copyright (C) 2026 iceman50 */
#include "testbase.h"
#include <dcpp/CID.h>
#include <dcpp/Text.h>
#include <GeoIP.h>
#include <openssl/crypto.h>
#include <openssl/ssl.h>
#include <filesystem>
#include <fstream>
#include <clocale>
#include <libintl.h>

namespace {
struct DependencyFiles {
    std::filesystem::path folder = std::filesystem::temp_directory_path() /
        ("dcpp-dependencies-" + dcpp::CID::generate().toBase32());
    DependencyFiles() { std::filesystem::create_directories(folder); }
    ~DependencyFiles() { std::error_code error; std::filesystem::remove_all(folder, error); }
};

void append32(std::string& data, uint32_t n) {
    for (unsigned i = 0; i < 4; ++i) data.push_back(static_cast<char>(n >> (8 * i)));
}

void writeCountryList(const std::filesystem::path& path, bool ipv6) {
    std::string data;
    for (const auto code : { "US", "GB" }) {
        const uint32_t leaf = 16776960u + GeoIP_id_by_code(code);
        for (unsigned i = 0; i < 3; ++i) data.push_back(static_cast<char>(leaf >> (8 * i)));
    }
    data.append(64, '\0');
    data.append(3, '\xff');
    data.push_back(ipv6 ? GEOIP_COUNTRY_EDITION_V6 : GEOIP_COUNTRY_EDITION);
    std::ofstream(path, std::ios::binary).write(data.data(), data.size());
}
}

TEST(DependencyRuntime, geoip_reads_country_databases_and_ipv6_literals) {
    DependencyFiles files;
#ifdef _WIN32
    const auto path = files.folder / L"countries-\u00e9\u4e16.dat";
    const auto filename = dcpp::Text::fromT(path.native());
#else
    const auto path = files.folder / "countries.dat";
    const auto filename = path.string();
#endif
    writeCountryList(path, false);
    std::unique_ptr<GeoIP, decltype(&GeoIP_delete)> db(GeoIP_open(filename.c_str(), GEOIP_STANDARD), GeoIP_delete);
    ASSERT_NE(nullptr, db);
    EXPECT_STREQ("US", GeoIP_country_code_by_addr(db.get(), "1.2.3.4"));
    EXPECT_STREQ("GB", GeoIP_country_code_by_addr(db.get(), "200.1.2.3"));
    db.reset();
    writeCountryList(path, true);
    db.reset(GeoIP_open(filename.c_str(), GEOIP_STANDARD));
    ASSERT_NE(nullptr, db);
    EXPECT_STREQ("US", GeoIP_country_code_by_addr_v6(db.get(), "2001:db8::1"));
    EXPECT_STREQ("GB", GeoIP_country_code_by_addr_v6(db.get(), "fd00::1"));
#ifdef _WIN32
    EXPECT_EQ(244, GeoIP_Win_GEOID_by_id(GeoIP_id_by_code("US")));
#endif
    EXPECT_STREQ("1.6.12", GeoIP_lib_version());
}

#if defined(__MINGW32__) && defined(_WIN64)
TEST(DependencyRuntime, openssl_headers_match_the_linked_lts_library) {
    EXPECT_STREQ(OPENSSL_VERSION_STR, OpenSSL_version(OPENSSL_VERSION_STRING));
    std::unique_ptr<SSL_CTX, decltype(&SSL_CTX_free)> ctx(SSL_CTX_new(TLS_method()), SSL_CTX_free);
    ASSERT_NE(nullptr, ctx);
    EXPECT_EQ(1, SSL_CTX_set_min_proto_version(ctx.get(), TLS1_2_VERSION));
    EXPECT_EQ(1, SSL_CTX_set_ciphersuites(ctx.get(), "TLS_AES_128_GCM_SHA256"));
}

TEST(DependencyRuntime, gettext_loads_utf8_catalogs_and_plural_forms) {
    EXPECT_EQ(LIBINTL_VERSION, libintl_version);
    DependencyFiles files;
    const auto directory = files.folder / "fr" / "LC_MESSAGES";
    std::filesystem::create_directories(directory);
    const std::vector<std::string> originals = { "", std::string("apple\0apples", 12), "coffee" };
    const std::vector<std::string> translations = {
        "Content-Type: text/plain; charset=UTF-8\nPlural-Forms: nplurals=2; plural=(n > 1);\n",
        std::string("pomme\0pommes", 12), "caf\xc3\xa9"
    };
    std::string data;
    for (uint32_t n : { 0x950412deu, 0u, 3u, 28u, 52u, 0u, 0u }) append32(data, n);
    uint32_t offset = 76;
    for (const auto* entries : { &originals, &translations }) {
        for (const auto& entry : *entries) {
            append32(data, static_cast<uint32_t>(entry.size()));
            append32(data, offset);
            offset += static_cast<uint32_t>(entry.size()) + 1;
        }
    }
    for (const auto* entries : { &originals, &translations })
        for (const auto& entry : *entries) { data += entry; data.push_back('\0'); }
    std::ofstream(directory / "dcpp-dependency-test.mo", std::ios::binary).write(data.data(), data.size());

    struct RestoreLocale {
        std::string locale = setlocale(LC_ALL, nullptr);
        std::string language = getenv("LANGUAGE") ? getenv("LANGUAGE") : "";
        std::string all = getenv("LC_ALL") ? getenv("LC_ALL") : "";
        ~RestoreLocale() {
            _putenv_s("LANGUAGE", language.c_str());
            _putenv_s("LC_ALL", all.c_str());
            setlocale(LC_ALL, locale.c_str());
        }
    } restore;
    // gettext consults the environment for Windows' synthetic LC_MESSAGES.
    // Isolate the test from shells that export LANG=C.UTF-8.
    ASSERT_EQ(0, _putenv_s("LC_ALL", "fr_FR.UTF-8"));
    ASSERT_NE(nullptr, setlocale(LC_ALL, "French"));
    ASSERT_EQ(0, _putenv_s("LANGUAGE", "fr"));
    ASSERT_NE(nullptr, bindtextdomain("dcpp-dependency-test", files.folder.string().c_str()));
    ASSERT_NE(nullptr, bind_textdomain_codeset("dcpp-dependency-test", "UTF-8"));
    EXPECT_STREQ("caf\xc3\xa9", dgettext("dcpp-dependency-test", "coffee"));
    EXPECT_STREQ("pomme", dngettext("dcpp-dependency-test", "apple", "apples", 1));
    EXPECT_STREQ("pommes", dngettext("dcpp-dependency-test", "apple", "apples", 3));
    EXPECT_STREQ("missing", dgettext("dcpp-dependency-test", "missing"));
}
#endif
