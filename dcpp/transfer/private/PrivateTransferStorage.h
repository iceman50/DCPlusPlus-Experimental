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
/** Whether an absolute, normalized path lies in private storage or publication staging, judged by
 * its name alone. No file system access: use it for paths that come from a share scan, whose
 * roots and directories were checked by PrivatePathScanner. */
bool isPrivateLocation(const std::string& path);
/** As above, with storageRoot() already looked up (for checking many paths). */
bool isPrivateLocation(const std::string& path, const std::string& root);
/** Whether the file carries the Private Send marker. One open of an alternate data stream. */
bool hasPrivateMarker(const std::string& path);
/** The full check for a single path chosen outside a share scan (a share root being added, a chat
 * attachment, a finished download, a receive target): its name, its real location through
 * junctions, symbolic links, subst drives, network paths and aliases of the storage root, and
 * the marker. Works for local, mapped and UNC paths. */
bool isPrivatePath(const std::string& path);

/** Private-storage checks for a share scan. Directories cost no file system access unless they
 * are reparse points or named like the storage root; regular files are judged by name, and their
 * markers are checked when they are hashed (HashManager) or found in the hash store unchecked. */
class PrivatePathScanner {
public:
	PrivatePathScanner();
	/** A share root: its name, real location and ancestors. */
	bool isPrivateRoot(const std::string& directory) const;
	/** A directory found by the scan inside a root that isn't private.
	 * @param directory Real path, ending with a separator. */
	bool isPrivateDirectory(const std::string& directory, const std::string& name, bool reparsePoint) const;
	/** A file found by the scan inside a directory that isn't private. */
	bool isPrivateFile(const std::string& path, const std::string& name, bool reparsePoint) const;

private:
	struct Storage;
	std::shared_ptr<Storage> storage;
};

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
