/* Copyright (C) 2026 iceman50
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef DCPLUSPLUS_PRIVATE_TRANSFER_STORAGE_H
#define DCPLUSPLUS_PRIVATE_TRANSFER_STORAGE_H

#include "PrivateTransferProtocol.h"

#include <functional>
#include <memory>

namespace dcpp {
namespace pft {

/** Private staging is excluded from sharing; published files retain a private
 * provenance marker even when the recipient chooses another directory. */
std::string storageRoot();
std::string defaultDownloadPath(const std::string& name);
bool isPrivatePath(const std::string& path);

/** Startup only: old grants cannot survive restart; remove owned partials
 * without following reparse points or touching completed received files. */
bool purgeUnfinished() noexcept;

/** Stable regular-file handle. Windows ancestors are pinned against rename and
 * reparse replacement for its lifetime. No peer-provided path reaches openSource.
 */
class PrivateFile {
public:
	using Cancelled = std::function<bool()>;

	struct Hashes {
		std::string sha256, tth;
	};

	static std::shared_ptr<PrivateFile> openSource(const std::string& path);
	static std::shared_ptr<PrivateFile> createDestination(const Metadata& metadata, const std::string& target = {});
	~PrivateFile();

	uint64_t size() const;
	std::string name() const;
	std::string targetPath() const;

	size_t read(uint64_t offset, void* buffer, size_t size);
	void write(uint64_t offset, const void* buffer, size_t size);
	Hashes hashes(uint64_t bytes, const Cancelled& cancelled);

	/** Runs attachment policy and verifies again after scanner intervention.
	 * Publication is separate so the offer owner can serialize it with cancel.
	 */
	void verifyAndScan(const Metadata& metadata, const Cancelled& cancelled);
	std::string publish();

private:
	struct Impl;
	std::unique_ptr<Impl> impl;

	PrivateFile();
};

}
}

#endif
