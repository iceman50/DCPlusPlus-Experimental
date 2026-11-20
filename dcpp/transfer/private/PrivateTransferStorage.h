/* Copyright (C) 2026 iceman50
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef DCPLUSPLUS_PRIVATE_TRANSFER_STORAGE_H
#define DCPLUSPLUS_PRIVATE_TRANSFER_STORAGE_H

#include <dcpp/transfer/private/PrivateTransferProtocol.h>

#include <functional>
#include <memory>

namespace dcpp {
namespace pft {

/** Private staging is excluded from sharing; published files retain a private
 * provenance marker even when the recipient chooses another directory. */
std::string storageRoot();
std::string defaultDownloadPath(const std::string& name);
bool isPrivatePath(const std::string& path);

/** isPrivatePath for share scans. It canonicalizes the private root once and each scanned directory
 * once instead of per file, which on a network drive costs several round trips per file. For a
 * regular file isPrivateFile(enter(dir), name) == isPrivatePath(dir + name); a file that is itself a
 * reparse point (symlink) must go through isPrivatePath. */
class PrivatePathScanner {
public:
	struct Directory {
		std::string path;      ///< As given; ends with a separator.
		std::string canonical; ///< With a trailing separator; empty if canonicalization failed.
		bool staging = false;  ///< A component of the path or its canonical form is publication staging.
	};

	PrivatePathScanner();
	/** @param directory Real directory path ending with a separator. */
	Directory enter(const std::string& directory) const;
	bool isPrivateFile(const Directory& dir, const std::string& name) const;

private:
	std::string root;
	std::string canonicalRoot;
};

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
