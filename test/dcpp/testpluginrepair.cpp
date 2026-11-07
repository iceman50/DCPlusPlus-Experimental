// Copyright (C) 2026 iceman50. GPL-3.0-or-later.
#include "testbase.h"
#include <dcpp/plugins/PluginApiImpl.h>
#include <dcpp/plugins/PluginManager.h>
#include <dcpp/plugins/PluginHttp.h>
#include <dcpp/settings/SettingsManager.h>
#include <dcpp/storage/Archive.h>
#include <dcpp/system/File.h>
#include <dcpp/util/Util.h>
#include <dcpp/chat/Tagger.h>
#include <zlib.h>
#include <filesystem>
#include <future>
#include <chrono>
#include <clocale>
#include <thread>

using namespace dcpp;
namespace fs = std::filesystem;
namespace {
const string fixtureId = "{15503cbe-b840-4ab8-bc95-2ebf2646afe7}";
Bool DCAPI handled(dcptr_t, dcptr_t, dcptr_t common, Bool*) {
	if(common) ++*static_cast<unsigned*>(common);
	return True;
}

class PluginRepair : public testing::Test {
protected:
	fs::path directory;
	Util::PathsMap previousPaths;
	void SetUp() override {
		directory = fs::absolute("build/plugin-repair-tests") / std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
		fs::create_directories(directory);
		Util::PathsMap paths;
		for(auto type: { Util::PATH_USER_CONFIG, Util::PATH_USER_LOCAL, Util::PATH_GLOBAL_CONFIG, Util::PATH_RESOURCES, Util::PATH_LOCALE,
			Util::PATH_DOWNLOADS, Util::PATH_FILE_LISTS, Util::PATH_HUB_LISTS, Util::PATH_NOTEPAD }) {
			previousPaths[type] = Util::getPath(type);
			paths[type] = directory.u8string() + "/";
		}
		Util::initialize(paths);
		PluginManager::newInstance();
		PluginApiImpl::init();
	}
	void TearDown() override {
		PluginApiImpl::shutdown();
		PluginManager::deleteInstance();
		Util::initialize(previousPaths);
		std::error_code ignored;
		fs::remove_all(directory, ignored);
	}
	template<class T> T* api(const char* name, uint32_t version = 1) {
		return reinterpret_cast<T*>(PluginManager::getInstance()->getCore()->query_interface(name, version));
	}
	fs::path fixture() {
#ifdef _WIN32
		std::wstring path(32768, 0);
		path.resize(GetModuleFileNameW(nullptr, path.data(), static_cast<DWORD>(path.size())));
		return fs::path(path).parent_path() / "plugin-repair-fixture.dll";
#else
		return fs::canonical("/proc/self/exe").parent_path() / "plugin-repair-fixture.so";
#endif
	}
	string manifest(const string& id = fixtureId, const string& payload = "plugin.dll", const string& extra = "", const string& version = "1") {
		return "<dcext><ApiVersion>8</ApiVersion><UUID>" + id + "</UUID><Name>Fixture</Name><Version>" + version + "</Version>"
#if defined(_WIN64)
			"<Plugin Platform=\"pe-x64\">"
#elif defined(_WIN32)
			"<Plugin Platform=\"pe-x86\">"
#else
			"<Plugin Platform=\"elf-x64\">"
#endif
			+ payload + "</Plugin>" + extra + "</dcext>";
	}
	string zip(const std::vector<std::pair<string, string>>& files, const string& name = "package.dcext", uint32_t attributes = 0) {
		// Small stored ZIP writer keeps these hostile-input fixtures self-contained.
		string bytes, central;
		auto number = [](string& out, uint32_t value, unsigned width) {
			for(unsigned i = 0; i < width; ++i) out += static_cast<char>(value >> (8 * i));
		};
		for(const auto& file: files) {
			const auto offset = static_cast<uint32_t>(bytes.size());
			const auto crc = crc32(0, reinterpret_cast<const Bytef*>(file.second.data()), static_cast<uInt>(file.second.size()));
			number(bytes, 0x04034b50, 4); number(bytes, 20, 2);
			for(unsigned i = 0; i < 4; ++i) number(bytes, 0, 2); // flags, method, time, date
			number(bytes, crc, 4); number(bytes, file.second.size(), 4); number(bytes, file.second.size(), 4);
			number(bytes, file.first.size(), 2); number(bytes, 0, 2);
			bytes += file.first; bytes += file.second;
			number(central, 0x02014b50, 4); number(central, 0x0314, 2); number(central, 20, 2);
			for(unsigned i = 0; i < 4; ++i) number(central, 0, 2);
			number(central, crc, 4); number(central, file.second.size(), 4); number(central, file.second.size(), 4);
			number(central, file.first.size(), 2);
			for(unsigned i = 0; i < 4; ++i) number(central, 0, 2); // extra, comment, disk, attributes
			number(central, attributes, 4); number(central, offset, 4); central += file.first;
		}
		const auto offset = bytes.size(); bytes += central;
		number(bytes, 0x06054b50, 4); number(bytes, 0, 2); number(bytes, 0, 2);
		number(bytes, files.size(), 2); number(bytes, files.size(), 2);
		number(bytes, central.size(), 4); number(bytes, offset, 4); number(bytes, 0, 2);
		const auto path = (directory / name).u8string();
		File(path, File::WRITE, File::CREATE | File::TRUNCATE).write(bytes);
		return path;
	}
};
}

TEST_F(PluginRepair, CopiesBinaryDataWithoutScanningForTerminators) {
	const uint8_t bytes[] { 0xFF, 0, 0x42, 0xFE };
	DataArray source { bytes, sizeof(bytes) };
	auto copy = PluginApiImpl::copyData(&source);
	ASSERT_NE(nullptr, copy);
	EXPECT_EQ(sizeof(bytes), copy->size);
	EXPECT_EQ(0, memcmp(bytes, copy->pData, sizeof(bytes)));
	PluginApiImpl::releaseData(copy);
	PluginApiImpl::releaseData(copy); // already released: no second free
	PluginApiImpl::releaseData(&source); // borrowed callback data must not be freed
}

TEST_F(PluginRepair, EmptyAndInvalidBinaryInputsAreHandled) {
	DataArray empty { nullptr, 0 }, invalid { nullptr, 1 };
	auto copy = PluginApiImpl::copyData(&empty);
	ASSERT_NE(nullptr, copy);
	EXPECT_EQ(nullptr, copy->pData);
	PluginApiImpl::releaseData(copy);
	EXPECT_EQ(nullptr, PluginApiImpl::copyData(&invalid));
	EXPECT_EQ(nullptr, PluginApiImpl::copyData(static_cast<DataArrayPtr>(nullptr)));
	PluginApiImpl::releaseData(static_cast<DataArrayPtr>(nullptr));
}

TEST_F(PluginRepair, CopiesHubIPAddressSeparatelyFromURL) {
	HubData source {};
	source.url = "adc://longer.example.test:411";
	source.ip = "1.2.3.4";
	auto copy = PluginApiImpl::copyData(&source);
	ASSERT_NE(nullptr, copy);
	EXPECT_STREQ(source.url, copy->url);
	EXPECT_STREQ(source.ip, copy->ip);
	PluginApiImpl::releaseData(copy);
}

TEST_F(PluginRepair, InvalidPathsAndNullArgumentsAreRejected) {
	auto config = api<DCConfig>(DCINTF_CONFIG);
	EXPECT_EQ(nullptr, config->get_path(static_cast<PathType>(-1)));
	EXPECT_EQ(nullptr, config->get_path(static_cast<PathType>(999)));
	EXPECT_EQ(nullptr, config->get_cfg(nullptr, "setting", CFG_TYPE_STRING));
	EXPECT_EQ(nullptr, PluginApiImpl::copyData(static_cast<UserDataPtr>(nullptr)));
	EXPECT_EQ(nullptr, PluginApiImpl::copyData(static_cast<QueueDataPtr>(nullptr)));
	EXPECT_EQ(nullptr, PluginApiImpl::copyData(static_cast<ConfigValuePtr>(nullptr)));
	EXPECT_THROW(PluginManager::getInstallPath("../../escape"), Exception);
	EXPECT_EQ(nullptr, api<DCHooks>(DCINTF_HOOKS)->bind_hook(nullptr, handled, nullptr));
}

TEST_F(PluginRepair, MissingBooleanIsFalse) {
	auto config = api<DCConfig>(DCINTF_CONFIG);
	auto value = config->get_cfg("missing", "missing", CFG_TYPE_BOOL);
	ASSERT_NE(nullptr, value);
	EXPECT_EQ(False, reinterpret_cast<ConfigBoolPtr>(value)->value);
	config->release(value);
}

TEST_F(PluginRepair, ConfigV2DistinguishesMissingFromExplicitEmptyAndFalse) {
	auto pm = PluginManager::getInstance();
	ASSERT_NO_THROW(pm->addPlugin(fixture().u8string()));
	auto config = api<DCConfig2>(DCINTF_CONFIG2, 2);
	ASSERT_NE(nullptr, config);
	EXPECT_EQ(nullptr, config->get_cfg(fixtureId.c_str(), "missing", CFG_TYPE_BOOL));
	pm->setPluginSetting(fixtureId, "empty", "");
	pm->setPluginSetting(fixtureId, "false", "0");
	auto empty = config->get_cfg(fixtureId.c_str(), "empty", CFG_TYPE_STRING);
	auto boolean = config->get_cfg(fixtureId.c_str(), "false", CFG_TYPE_BOOL);
	ASSERT_NE(nullptr, empty);
	ASSERT_NE(nullptr, boolean);
	EXPECT_STREQ("", reinterpret_cast<ConfigStrPtr>(empty)->value);
	EXPECT_EQ(False, reinterpret_cast<ConfigBoolPtr>(boolean)->value);
	PluginApiImpl::releaseData(empty);
	PluginApiImpl::releaseData(boolean);
}

TEST_F(PluginRepair, LegacyUtilitiesSupportSizeQueriesAndExactBuffers) {
	auto utils = api<DCUtils>(DCINTF_DCPP_UTILS);
	char output[4] { '?', '?', '?', '?' };
	EXPECT_EQ(3u, utils->to_utf8(nullptr, "abc", 0));
	EXPECT_EQ(3u, utils->to_utf8(output, "abc", 3));
	EXPECT_EQ('?', output[3]);
	utils->to_utf8(output, "abc", 4);
	EXPECT_STREQ("abc", output);
	uint8_t byte = 0x66;
	EXPECT_EQ(2u, utils->to_base32(output, &byte, 1));
	EXPECT_EQ('M', output[0]);
	EXPECT_EQ('Y', output[1]);
	EXPECT_EQ(0u, utils->from_base32(&byte, "7", 0));
	EXPECT_EQ(0x66, byte);
}

TEST_F(PluginRepair, VersionTwoConversionsValidateCapacityAndCanonicalEncoding) {
	auto utils = api<DCUtils2>(DCINTF_DCPP_UTILS2, 2);
	ASSERT_NE(nullptr, utils);
	char output[4] { '?', '?', '?', '?' };
	EXPECT_EQ(4u, utils->to_utf8(output, 3, "abc"));
	EXPECT_EQ('?', output[0]);
	EXPECT_EQ(4u, utils->to_utf8(output, 4, "abc"));
	EXPECT_STREQ("abc", output);
	uint8_t byte = 0x66;
	EXPECT_EQ(3u, utils->to_base32(nullptr, 0, &byte, 1));
	EXPECT_EQ(3u, utils->to_base32(output, 4, &byte, 1));
	EXPECT_STREQ("MY", output);
	byte = 0;
	EXPECT_EQ(1u, utils->from_base32(&byte, 1, "MY", 2));
	EXPECT_EQ(0x66, byte);
	for(const string invalid: { "M", "MZ", "my", "M!", "MY=" }) {
		byte = 0xAA;
		EXPECT_EQ(SIZE_MAX, utils->from_base32(&byte, 1, invalid.data(), invalid.size()));
		EXPECT_EQ(0xAA, byte);
	}
}

TEST_F(PluginRepair, InterfaceDuplicateDoesNotGrantReleaseAuthority) {
	auto pm = PluginManager::getInstance();
	DCInterface table { 1 }, other { 2 };
	auto first = pm->registerInterface("test.interface", &table);
	ASSERT_NE(nullptr, first);
	EXPECT_EQ(nullptr, pm->registerInterface("test.interface", &other));
	EXPECT_EQ(&table, pm->queryInterface("test.interface"));
	EXPECT_TRUE(pm->releaseInterface(first));
	EXPECT_FALSE(pm->releaseInterface(first));
	EXPECT_EQ(nullptr, pm->queryInterface("test.interface"));
}

TEST_F(PluginRepair, HooksAllowSelfRemovalAndAdditionDuringDispatch) {
	auto pm = PluginManager::getInstance();
	auto hook = pm->createHook("test.hook", nullptr);
	struct Context { HookSubscriber* self; unsigned calls = 0; } context {};
	context.self = pm->bindHook("test.hook", [](dcptr_t, dcptr_t, dcptr_t data, Bool*) -> Bool {
		auto c = static_cast<Context*>(data);
		++c->calls;
		auto manager = PluginManager::getInstance();
		manager->releaseHook(c->self);
		manager->bindHook("test.hook", handled, &c->calls);
		return True;
	}, &context);
	EXPECT_TRUE(pm->runHook(hook, nullptr, nullptr));
	EXPECT_EQ(1u, context.calls);
	EXPECT_TRUE(pm->runHook(hook, nullptr, nullptr));
	EXPECT_EQ(2u, context.calls);
	EXPECT_EQ(0u, pm->releaseHook(context.self));
}

TEST_F(PluginRepair, HooksCanDestroyThemselvesAndSkipRemainingSubscribers) {
	auto pm = PluginManager::getInstance();
	auto hook = pm->createHook("test.hook", nullptr);
	unsigned calls = 0;
	pm->bindHook("test.hook", [](dcptr_t, dcptr_t, dcptr_t common, Bool*) -> Bool {
		PluginManager::getInstance()->destroyHook(static_cast<PluginHook*>(common)); return True;
	}, hook);
	pm->bindHook("test.hook", handled, &calls);
	EXPECT_TRUE(pm->runHook(hook, nullptr, nullptr));
	EXPECT_EQ(0u, calls);
	EXPECT_FALSE(pm->destroyHook(hook));
	EXPECT_FALSE(pm->runHook(hook, nullptr, nullptr));
	EXPECT_FALSE(pm->destroyHook(reinterpret_cast<PluginHook*>(1)));
	EXPECT_EQ(0u, pm->releaseHook(reinterpret_cast<HookSubscriber*>(1)));
	EXPECT_FALSE(pm->runHook("missing", nullptr));
}

TEST_F(PluginRepair, RemovingCallbackWaitsForConcurrentExecution) {
	auto pm = PluginManager::getInstance();
	pm->createHook("test.concurrent", nullptr);
	struct Context { std::promise<void> entered; std::shared_future<void> leave; } context;
	std::promise<void> leave;
	context.leave = leave.get_future().share();
	auto sub = pm->bindHook("test.concurrent", [](dcptr_t, dcptr_t, dcptr_t data, Bool*) -> Bool {
		auto c = static_cast<Context*>(data); c->entered.set_value(); c->leave.wait(); return True;
	}, &context);
	auto dispatch = std::async(std::launch::async, [&] { pm->runHook("test.concurrent", nullptr); });
	context.entered.get_future().wait();
	auto remove = std::async(std::launch::async, [&] { pm->releaseHook(sub); });
	EXPECT_EQ(std::future_status::timeout, remove.wait_for(std::chrono::milliseconds(30)));
	leave.set_value();
	EXPECT_EQ(std::future_status::ready, remove.wait_for(std::chrono::seconds(5)));
	dispatch.get(); remove.get();
	EXPECT_FALSE(pm->runHook("test.concurrent", nullptr));
}

TEST_F(PluginRepair, DeclaredUserHooksAreRegistered) {
	auto pm = PluginManager::getInstance();
	EXPECT_NE(nullptr, pm->bindHook(HOOK_USER_ONLINE, handled, nullptr));
	EXPECT_NE(nullptr, pm->bindHook(HOOK_USER_OFFLINE, handled, nullptr));
}

TEST_F(PluginRepair, LengthAwareUDPEventPreservesEmbeddedNulls) {
	string captured;
	auto pm = PluginManager::getInstance();
	pm->bindHook(HOOK_NETWORK_UDP_IN2, [](dcptr_t, dcptr_t data, dcptr_t common, Bool*) -> Bool {
		auto payload = static_cast<DataArrayPtr>(data);
		static_cast<string*>(common)->assign(static_cast<const char*>(payload->pData), static_cast<size_t>(payload->size));
		return True;
	}, &captured);
	const string message("a\0b", 3);
	EXPECT_TRUE(pm->onUDP(false, "127.0.0.1", "411", message));
	EXPECT_EQ(message, captured);
}

TEST_F(PluginRepair, ConnectionArgumentsAreOnlyAcceptedInTheirCallbackScope) {
	auto connections = api<DCConnection>(DCINTF_DCPP_CONNECTIONS);
	ConnectionData stale {};
	stale.object = reinterpret_cast<void*>(1);
	EXPECT_EQ(nullptr, connections->get_user(&stale));
	connections->send_protocol_cmd(&stale, "ignored");
	connections->terminate_conn(&stale, True);
}

TEST_F(PluginRepair, ConcurrentCallbacksCanRemoveEachOtherWithoutWaitingInCallback) {
	auto pm = PluginManager::getInstance();
	pm->createHook("cross.a", nullptr);
	pm->createHook("cross.b", nullptr);
	struct Context {
		std::promise<void> entered;
		std::shared_future<void> otherEntered;
		HookSubscriber* other = nullptr;
	} first, second;
	first.otherEntered = second.entered.get_future().share();
	second.otherEntered = first.entered.get_future().share();
	auto callback = [](dcptr_t, dcptr_t, dcptr_t common, Bool*) -> Bool {
		auto c = static_cast<Context*>(common);
		c->entered.set_value(); c->otherEntered.wait();
		auto pm = PluginManager::getInstance();
		pm->releaseHook(c->other);
		EXPECT_FALSE(pm->drainHook(c->other));
		return True;
	};
	second.other = pm->bindHook("cross.a", callback, &first);
	first.other = pm->bindHook("cross.b", callback, &second);
	auto a = std::async(std::launch::async, [&] { pm->runHook("cross.a", nullptr); });
	auto b = std::async(std::launch::async, [&] { pm->runHook("cross.b", nullptr); });
	EXPECT_EQ(std::future_status::ready, a.wait_for(std::chrono::seconds(5)));
	EXPECT_EQ(std::future_status::ready, b.wait_for(std::chrono::seconds(5)));
	a.get(); b.get();
	auto hooks = api<DCHooks2>(DCINTF_HOOKS2, 2);
	ASSERT_NE(nullptr, hooks);
	EXPECT_EQ(True, hooks->wait_idle(first.other));
	EXPECT_EQ(True, hooks->wait_idle(second.other));
	EXPECT_EQ(False, hooks->wait_idle(reinterpret_cast<subsHandle>(1)));
}

TEST_F(PluginRepair, TaggerRejectsOutOfBoundsAndReversedRanges) {
	Tagger tagger(string("abc"));
	tagger.addTag(4, 5, "b", "");
	tagger.addTag(2, 1, "b", "");
	tagger.replaceText(8, 9, "x");
	tagger.replaceText(2, 1, "x");
	string temporary;
	EXPECT_EQ("abc", tagger.merge(temporary));
}

TEST_F(PluginRepair, PackageNamesRejectWindowsAliasesAndTraversal) {
	for(const string path: { "../x", "/x", "C:/x", "a//b", "a/./b", "a\\b", "a:stream", "CON.txt", "a/NUL", "a.", "a ", "a/LPT1.txt" })
		EXPECT_FALSE(Archive::isPackagePath(path)) << path;
	EXPECT_FALSE(Archive::isPackagePath(string("a\0b", 3)));
	EXPECT_TRUE(Archive::isPackagePath("assets/icon.ico"));
}

TEST_F(PluginRepair, ArchiveInventoryRejectsDuplicatesSymlinksAndLimits) {
	auto duplicate = zip({ { "a", "1" }, { "A", "2" } });
	EXPECT_THROW(Archive(duplicate).packageEntries(10, 100, 100), Exception);
	auto symlink = zip({ { "link", "target" } }, "symlink.zip", 0120777u << 16);
	EXPECT_THROW(Archive(symlink).packageEntries(10, 100, 100), Exception);
	auto conflict = zip({ { "a", "1" }, { "a/b", "2" } }, "conflict.zip");
	EXPECT_THROW(Archive(conflict).packageEntries(10, 100, 100), Exception);
	auto normal = zip({ { "a", "123" }, { "b", "456" } }, "normal.zip");
	EXPECT_THROW(Archive(normal).packageEntries(1, 100, 100), Exception);
	EXPECT_THROW(Archive(normal).packageEntries(10, 2, 100), Exception);
	EXPECT_THROW(Archive(normal).packageEntries(10, 100, 5), Exception);
	EXPECT_EQ(2u, Archive(normal).packageEntries(10, 100, 100).size());
}

class PluginVersionLocale : public PluginRepair, public testing::WithParamInterface<const char*> { };

TEST_P(PluginVersionLocale, PackageVersionsAreIndependentOfNumericLocale) {
	// setlocale changes process state; restore it even if an assertion fails.
	struct RestoreLocale {
		string previous = std::setlocale(LC_NUMERIC, nullptr);
		~RestoreLocale() { std::setlocale(LC_NUMERIC, previous.c_str()); }
	} restore;
	if(!std::setlocale(LC_NUMERIC, GetParam())) {
		GTEST_SKIP() << "Locale unavailable: " << GetParam();
	}
	SCOPED_TRACE(GetParam());
	const string activeLocale = std::setlocale(LC_NUMERIC, nullptr);
	auto pm = PluginManager::getInstance();
	for(const auto& value: std::vector<std::pair<string, double>> {
		{ "0.3", 0.3 }, { "0.5", 0.5 }, { "1", 1.0 }, { "1.25", 1.25 }, { "5e-1", 0.5 }
	}) {
		SCOPED_TRACE(value.first);
		const auto path = zip({ { "info.xml", manifest(fixtureId, "plugin.dll", "", value.first) },
			{ "plugin.dll", "inert marker" } });
		EXPECT_DOUBLE_EQ(value.second, pm->extract(path).version);
	}
	for(const string value: { "", "0,5", "0.5junk", "0.5.1", "nan", "inf", "-inf", "0", "-0.5", "1e9999", "1e-9999" }) {
		SCOPED_TRACE(value);
		const auto path = zip({ { "info.xml", manifest(fixtureId, "plugin.dll", "", value) },
			{ "plugin.dll", "inert marker" } });
		EXPECT_THROW(pm->extract(path), Exception);
	}
	EXPECT_EQ(activeLocale, std::setlocale(LC_NUMERIC, nullptr));
}

INSTANTIATE_TEST_SUITE_P(NumericLocales, PluginVersionLocale, testing::Values(
#ifdef _WIN32
	"C", "English_United States.1252", "German_Germany.1252", "French_France.1252", "Polish_Poland.1250"
#else
	"C", "en_US.UTF-8", "de_DE.UTF-8", "fr_FR.UTF-8", "pl_PL.UTF-8"
#endif
));

TEST_F(PluginRepair, PackageManifestCannotReusePriorPayloadOrEscapeInstallation) {
	auto pm = PluginManager::getInstance();
	auto first = pm->extract(zip({ { "info.xml", manifest() }, { "plugin.dll", "inert marker" } }));
	EXPECT_THROW(pm->extract(zip({ { "info.xml", manifest() } }, "missing.zip")), Exception);
	EXPECT_THROW(pm->extract(zip({ { "info.xml", manifest("../escaped") }, { "plugin.dll", "marker" } }, "traversal.zip")), Exception);
	EXPECT_THROW(pm->extract(zip({ { "info.xml", manifest(fixtureId, "../outside.dll") } }, "path.zip")), Exception);
	first.uuid = "changed";
	EXPECT_THROW(pm->install(first), Exception);
	EXPECT_FALSE(fs::exists(directory / "escaped"));
}

TEST_F(PluginRepair, PackageReviewBindsExactBytesAndValidatesBinaryIdentity) {
	ASSERT_TRUE(fs::exists(fixture()));
	const auto binary = File(fixture().u8string(), File::READ, File::OPEN).read();
	auto path = zip({ { "info.xml", manifest() }, { "plugin.dll", binary } });
	auto pm = PluginManager::getInstance();
	auto approved = pm->extract(path);
	File(path, File::WRITE, File::CREATE | File::TRUNCATE).write("replaced after review");
	EXPECT_NO_THROW(pm->install(approved));
	EXPECT_TRUE(pm->isLoaded(fixtureId));
	EXPECT_EQ("saved-during-initialization", pm->getPluginSetting(fixtureId, "fixture"));
	EXPECT_THROW(pm->install(approved), Exception);
	const auto installed = pm->getPlugin(fixtureId).path;
	EXPECT_EQ(binary, File(installed, File::READ, File::OPEN).read());
}

TEST_F(PluginRepair, ManifestAndBinaryIdentityMismatchDoesNotRegisterPlugin) {
	ASSERT_TRUE(fs::exists(fixture()));
	const auto binary = File(fixture().u8string(), File::READ, File::OPEN).read();
	auto pm = PluginManager::getInstance();
	auto info = pm->extract(zip({ { "info.xml", manifest("another-id") }, { "plugin.dll", binary } }));
	EXPECT_THROW(pm->install(info), Exception);
	EXPECT_TRUE(pm->getPluginList().empty());
}

TEST_F(PluginRepair, NestedPlatformBinaryUsesPrivateInstallRootForResources) {
	const auto binary = File(fixture().u8string(), File::READ, File::OPEN).read();
	auto pm = PluginManager::getInstance();
	auto info = pm->extract(zip({ { "info.xml", manifest(fixtureId, "x64/plugin.dll", "<Files><File>assets/data.txt</File></Files>") },
		{ "x64/plugin.dll", binary }, { "assets/data.txt", "resource" } }));
	ASSERT_NO_THROW(pm->install(info));
	auto config = api<DCConfig>(DCINTF_CONFIG);
	auto directory = config->get_install_path(fixtureId.c_str());
	ASSERT_NE(nullptr, directory);
	EXPECT_EQ("resource", File((fs::u8path(directory->value) / "assets/data.txt").u8string(), File::READ, File::OPEN).read());
	config->release(reinterpret_cast<ConfigValuePtr>(directory));
}

TEST_F(PluginRepair, InitializationRegistersIdentityBeforeSettingsAndDefersDisable) {
	auto pm = PluginManager::getInstance();
	ASSERT_TRUE(fs::exists(fixture()));
	ASSERT_NO_THROW(pm->addPlugin(fixture().u8string()));
	EXPECT_TRUE(pm->isLoaded(fixtureId));
	EXPECT_EQ("saved-during-initialization", pm->getPluginSetting(fixtureId, "fixture"));
	pm->disablePlugin(fixtureId);
	pm->disablePlugin(fixtureId);
	EXPECT_TRUE(pm->isLoaded(fixtureId));
	EXPECT_FALSE(pm->getPlugin(fixtureId).enabled);
	EXPECT_THROW(pm->removePlugin(fixtureId), Exception);
	pm->enablePlugin(fixtureId);
	EXPECT_TRUE(pm->getPlugin(fixtureId).enabled);
	pm->movePlugin(fixtureId, INT_MAX);
	pm->movePlugin(fixtureId, INT_MIN);
	EXPECT_EQ(fixtureId, pm->getPluginList().front());
}

#ifdef _WIN32
TEST_F(PluginRepair, FailedInitializationRollsBackRegistrationsAndSettings) {
	auto pm = PluginManager::getInstance();
	SetEnvironmentVariableA("DCPP_PLUGIN_FIXTURE_FAIL", "1");
	EXPECT_THROW(pm->addPlugin(fixture().u8string()), Exception);
	SetEnvironmentVariableA("DCPP_PLUGIN_FIXTURE_FAIL", nullptr);
	EXPECT_TRUE(pm->getPluginList().empty());
	EXPECT_EQ(nullptr, pm->queryInterface("fixture.interface"));
	EXPECT_FALSE(pm->runHook(HOOK_TIMER_SECOND, nullptr));
	EXPECT_NO_THROW(pm->addPlugin(fixture().u8string()));
	EXPECT_TRUE(pm->isLoaded(fixtureId));
}
#endif

TEST_F(PluginRepair, HTTPRejectsInsecureCredentialsAndMalformedURLs) {
	for(const string url: { "http://example.test/", "file:///tmp/a", "https://user:secret@example.test/", "https://@example.test/",
		"https:///", "https://example.test/\r\nInjected: x", "https://example.test/#fragment", "https://example.test\\evil/" })
		EXPECT_FALSE(PluginHttp::validUrl(url)) << url;
	EXPECT_TRUE(PluginHttp::validUrl("https://example.test/path?q=test"));
	unsigned failures = 0;
	PluginManager::getInstance()->bindHook(HOOK_DATAACESSOR_HTTP_RESOURCE_NOTIFICATION_FAILED, handled, &failures);
	api<DCDataAccess>(DCINTF_DCPP_DATAACCESSOR)->get_http_resource("http://insecure.test/", nullptr);
	EXPECT_EQ(1u, failures);
}

TEST_F(PluginRepair, OptInHTTPSRequestsAreIsolatedAndAtomic) {
	const auto good = std::getenv("DCPP_PLUGIN_HTTP_OK_URL");
	const auto bad = std::getenv("DCPP_PLUGIN_HTTP_BAD_CERT_URL");
	if(!good || !bad) GTEST_SKIP() << "Run test/support/run_plugin_http_tests.py for transport integration";
#ifdef _WIN32
    WSADATA wsa {};
    ASSERT_EQ(0, WSAStartup(MAKEWORD(2, 2), &wsa));
#endif
    SettingsManager::newInstance();
    struct Stop { ~Stop() {
        PluginHttp::stop();
        SettingsManager::deleteInstance();
#ifdef _WIN32
        WSACleanup();
#endif
    } } stop;
	struct Results {
		std::mutex mutex;
		std::condition_variable ready;
		std::map<string, unsigned> completed, failed;
		std::map<string, string> bodies, errors;
	} results;
	auto pm = PluginManager::getInstance();
	pm->bindHook(HOOK_DATAACESSOR_HTTP_RESOURCE_NOTIFICATION, [](dcptr_t object, dcptr_t, dcptr_t common, Bool*) -> Bool {
		auto r = static_cast<Results*>(common); std::lock_guard<std::mutex> lock(r->mutex);
		++r->completed[static_cast<const char*>(object)]; r->ready.notify_all(); return False;
	}, &results);
	pm->bindHook(HOOK_DATAACESSOR_HTTP_RESOURCE_NOTIFICATION_FAILED, [](dcptr_t object, dcptr_t data, dcptr_t common, Bool*) -> Bool {
		auto r = static_cast<Results*>(common); std::lock_guard<std::mutex> lock(r->mutex);
		++r->failed[static_cast<const char*>(object)];
		if(data) r->errors[static_cast<const char*>(object)] = static_cast<const char*>(data);
		r->ready.notify_all(); return False;
	}, &results);
	pm->bindHook(HOOK_DATAACESSOR_HTTP_RESOURCE_STREAM, [](dcptr_t object, dcptr_t data, dcptr_t common, Bool*) -> Bool {
		auto r = static_cast<Results*>(common); std::lock_guard<std::mutex> lock(r->mutex);
		auto bytes = static_cast<DataArrayPtr>(data);
		r->bodies[static_cast<const char*>(object)].append(static_cast<const char*>(bytes->pData), static_cast<size_t>(bytes->size));
		return False;
	}, &results);
	const auto output = (directory / "completed.html").u8string();
	const auto preserved = (directory / "preserved.html").u8string();
	File(preserved, File::WRITE, File::CREATE | File::TRUNCATE).write("original");
	const string stream = string(good) + "?plugin-stream-test=1";
	auto http = api<DCDataAccess>(DCINTF_DCPP_DATAACCESSOR);
	http->get_http_resource(good, output.c_str());
	http->get_http_resource(bad, preserved.c_str());
	http->get_http_resource(stream.c_str(), nullptr);
	{
		std::unique_lock<std::mutex> lock(results.mutex);
		EXPECT_TRUE(results.ready.wait_for(lock, std::chrono::seconds(90), [&] {
			unsigned total = 0;
			for(const auto& item: results.completed) total += item.second;
			for(const auto& item: results.failed) total += item.second;
			return total >= 3;
		}));
	}
	PluginHttp::stop();
	EXPECT_EQ(1u, results.completed[good]) << results.errors[good];
	EXPECT_EQ(1u, results.completed[stream]);
	EXPECT_EQ(1u, results.failed[bad]);
	EXPECT_NE(string::npos, results.errors[bad].find("TLS error")) << "Expected a certificate validation error: " << results.errors[bad];
	EXPECT_EQ(0u, results.failed[good]);
	EXPECT_EQ(0u, results.completed[bad]);
	EXPECT_TRUE(results.bodies[bad].empty());
	EXPECT_FALSE(results.bodies[good].empty());
	EXPECT_FALSE(results.bodies[stream].empty());
	EXPECT_EQ("original", File(preserved, File::READ, File::OPEN).read());
	if(fs::exists(fs::u8path(output))) EXPECT_EQ(results.bodies[good], File(output, File::READ, File::OPEN).read());
	else ADD_FAILURE() << "Successful request did not publish its file";
}
