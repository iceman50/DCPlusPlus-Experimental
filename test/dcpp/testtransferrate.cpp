/*
 * Copyright (C) 2026 iceman50
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 3 of the License, or
 * (at your option) any later version.
 */

#include "testbase.h"

#include <dcpp/connectivity/ConnectivityManager.h>
#include <dcpp/settings/SettingsManager.h>
#include <dcpp/system/File.h>
#include <dcpp/transfer/ThrottleManager.h>
#include <dcpp/transfer/TransferRate.h>

using namespace dcpp;

TEST(TransferRateTest, converts_limits_across_the_signed_32_bit_boundary)
{
	EXPECT_EQ(0, TransferRate::fromKiB(0));
	EXPECT_EQ(1073741824LL, TransferRate::fromKiB(TransferRate::MAX_LIMIT_KIB));
	EXPECT_EQ(2147482624LL, TransferRate::fromKiB(2097151));
	EXPECT_EQ(2147483648LL, TransferRate::fromKiB(2097152));
	EXPECT_EQ(4294967296LL, TransferRate::fromKiB(4194304));
	EXPECT_EQ(2199023254528LL, TransferRate::fromKiB(std::numeric_limits<int>::max()));
	EXPECT_EQ("2.00 GiB", Util::formatBytes(TransferRate::fromKiB(2097152)));
}

TEST(TransferRateTest, advertised_upload_speed_is_validated_before_conversion)
{
	EXPECT_EQ(131072000, TransferRate::fromMibit(1000));
	EXPECT_EQ(2147483648LL, TransferRate::fromMibit(16384));
	EXPECT_EQ(13107200000LL, TransferRate::fromMibit(100000));
	EXPECT_EQ(0, TransferRate::fromMibit(0));
	EXPECT_EQ(0, TransferRate::fromMibit(-1));
	EXPECT_EQ(0, TransferRate::fromMibit(std::numeric_limits<double>::quiet_NaN()));
	EXPECT_EQ(0, TransferRate::fromMibit(std::numeric_limits<double>::infinity()));
	EXPECT_EQ(std::numeric_limits<int64_t>::max(), TransferRate::fromMibit(std::numeric_limits<double>::max()));
}

TEST(TransferRateTest, socket_chunks_fit_the_socket_api_without_wrapping)
{
	EXPECT_EQ(0, TransferRate::socketChunkSize(0));
	EXPECT_EQ(65536, TransferRate::socketChunkSize(65536));
	EXPECT_EQ(std::numeric_limits<int>::max(), TransferRate::socketChunkSize(2147483647ULL));
	EXPECT_EQ(std::numeric_limits<int>::max(), TransferRate::socketChunkSize(2147483648ULL));
	EXPECT_EQ(std::numeric_limits<int>::max(), TransferRate::socketChunkSize(4294967296ULL));
	EXPECT_EQ(std::numeric_limits<int>::max(), TransferRate::socketChunkSize(std::numeric_limits<uint64_t>::max()));
}

class TransferRateSettingsTest : public testing::Test {
protected:
	void SetUp() override {
		SettingsManager::newInstance();
		ConnectivityManager::newInstance();
	}

	void TearDown() override {
		ConnectivityManager::deleteInstance();
		SettingsManager::deleteInstance();
	}

	const SettingsManager::IntSetting limits[4] = {
		SettingsManager::MAX_UPLOAD_SPEED_MAIN, SettingsManager::MAX_DOWNLOAD_SPEED_MAIN,
		SettingsManager::MAX_UPLOAD_SPEED_ALTERNATE, SettingsManager::MAX_DOWNLOAD_SPEED_ALTERNATE
	};
};

TEST_F(TransferRateSettingsTest, all_throttle_settings_enforce_the_current_cap)
{
	auto settings = SettingsManager::getInstance();
	for(auto key: limits) {
		for(int value: { 0, 1, 4096, ThrottleManager::MAX_LIMIT }) {
			settings->set(key, value);
			EXPECT_EQ(value, settings->get(key));
			settings->set(key, std::to_string(value));
			EXPECT_EQ(value, settings->get(key));
		}
		for(int value: { ThrottleManager::MAX_LIMIT + 1, 2097152, std::numeric_limits<int>::max() }) {
			settings->set(key, value);
			EXPECT_EQ(ThrottleManager::MAX_LIMIT, settings->get(key));
		}
		for(const auto* value: { "2147483648", "4294967296", "999999999999999999999999999999" }) {
			settings->set(key, string(value));
			EXPECT_EQ(ThrottleManager::MAX_LIMIT, settings->get(key));
		}
		settings->set(key, -1);
		EXPECT_EQ(0, settings->get(key));
		settings->set(key, string("-999999999999999999999999999999"));
		EXPECT_EQ(0, settings->get(key));
		settings->set(key, string());
		EXPECT_TRUE(settings->isDefault(key));
	}
}

TEST_F(TransferRateSettingsTest, byte_accessors_preserve_unlimited_and_convert_active_limits)
{
	auto settings = SettingsManager::getInstance();
	EXPECT_EQ(0, ThrottleManager::getUpLimitBytes());
	EXPECT_EQ(0, ThrottleManager::getDownLimitBytes());
	settings->set(SettingsManager::MAX_UPLOAD_SPEED_MAIN, ThrottleManager::MAX_LIMIT);
	settings->set(SettingsManager::MAX_DOWNLOAD_SPEED_MAIN, 2048);
	EXPECT_EQ(1073741824LL, ThrottleManager::getUpLimitBytes());
	EXPECT_EQ(2097152, ThrottleManager::getDownLimitBytes());
}

TEST_F(TransferRateSettingsTest, slot_thresholds_keep_the_full_positive_int_range)
{
	auto settings = SettingsManager::getInstance();
	for(auto key: { SettingsManager::MAX_DOWNLOAD_SPEED, SettingsManager::MIN_UPLOAD_SPEED }) {
		settings->set(key, 2097152);
		EXPECT_EQ(2147483648LL, TransferRate::fromKiB(settings->get(key)));
		settings->set(key, string("999999999999999999999999999999"));
		EXPECT_EQ(std::numeric_limits<int>::max(), settings->get(key));
		settings->set(key, -1);
		EXPECT_EQ(0, settings->get(key));
	}
}

TEST_F(TransferRateSettingsTest, xml_limits_are_clamped_before_narrowing)
{
	const auto path = Util::getTempPath() + "dcpp-test-transfer-rate-settings.xml";
	auto settings = SettingsManager::getInstance();
	string xml = "<DCPlusPlus><Settings>";
	for(auto key: limits) {
		const auto& tag = settings->getSettingTags()[key];
		xml += "<" + tag + ">999999999999999999999999999999</" + tag + ">";
	}
	xml += "<MaxDownloadSpeed>-2097152</MaxDownloadSpeed></Settings></DCPlusPlus>";
	{
		File file(path, File::WRITE, File::CREATE | File::TRUNCATE);
		file.write(xml);
	}
	settings->load(path);
	for(auto key: limits)
		EXPECT_EQ(ThrottleManager::MAX_LIMIT, settings->get(key));
	EXPECT_EQ(0, settings->get(SettingsManager::MAX_DOWNLOAD_SPEED));
	File::deleteFile(path);
}
