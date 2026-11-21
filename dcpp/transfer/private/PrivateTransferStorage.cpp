/* Copyright (C) 2026 iceman50
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "stdinc.h"

#include <dcpp/transfer/private/PrivateTransferStorage.h>

#include <dcpp/core/io/File.h>
#include <dcpp/hash/value/MerkleTree.h>
#include <dcpp/settings/SettingsManager.h>
#include <dcpp/util/text/Text.h>
#include <dcpp/util/Util.h>

#include <openssl/evp.h>
#include <filesystem>
#include <mutex>
#ifdef _WIN32
#include <sddl.h>
#include <shobjidl.h>
#endif

namespace dcpp {
namespace pft {

namespace {
const std::string publicationPrefix = ".DCPlusPlus-PrivateSend-";

bool isStagingName(const std::string& name) {
	return name.size() == publicationPrefix.size() + 32 &&
		Util::strnicmp(name, publicationPrefix, publicationPrefix.size()) == 0 &&
		isHex(name.substr(publicationPrefix.size()), 16);
}

/** Whether any component of the path is a publication staging directory. Allocates only for
 * components of exactly the staging name's length. */
bool hasStagingComponent(const std::string& path) {
	size_t start = 0;
	while(start < path.size()) {
		auto end = path.find_first_of("\\/", start);
		if(end == std::string::npos)
			end = path.size();
		if(end - start == publicationPrefix.size() + 32 && isStagingName(path.substr(start, end - start)))
			return true;
		start = end + 1;
	}
	return false;
}
}

std::string storageRoot() {
	return Util::getPath(Util::PATH_USER_LOCAL) + "PrivateSend" PATH_SEPARATOR_STR;
}

std::string defaultDownloadPath(const std::string& name) {
	if(!safeName(name))
		throw ProtocolException("Invalid private transfer filename");
	const auto& directory = SETTING(DOWNLOAD_DIRECTORY);
	return (std::filesystem::u8path(directory.empty() ? Util::getPath(Util::PATH_DOWNLOADS) : directory) /
		std::filesystem::u8path(name)).u8string();
}

namespace {

bool withinRoot(const std::string& p, const std::string& r) {
	if(r.empty())
		return false;
	return Util::strnicmp(p, r, r.size()) == 0 || Util::stricmp(p, r.substr(0, r.size() - 1)) == 0;
}

std::string withSeparator(std::string path) {
	if(!path.empty() && path.back() != '\\' && path.back() != '/')
		path += PATH_SEPARATOR;
	return path;
}

bool isSeparator(char c) {
	return c == '\\' || c == '/';
}

#ifdef _WIN32
std::wstring withoutExtendedPrefix(const std::wstring& path) {
	if(path.compare(0, 8, L"\\\\?\\UNC\\") == 0)
		return L"\\\\" + path.substr(8);
	if(path.compare(0, 4, L"\\\\?\\") == 0)
		return path.substr(4);
	return path;
}

/** Absolute form with '.', '..' and '/' resolved the way Win32 resolves them. No file system access. */
std::string lexicalPath(const std::string& path) {
	const auto wide = Text::toT(path);
	const auto needed = GetFullPathNameW(wide.c_str(), 0, nullptr, nullptr);
	if(needed == 0)
		return path;
	std::wstring buffer(needed, L'\0');
	const auto length = GetFullPathNameW(wide.c_str(), needed, &buffer[0], nullptr);
	if(length == 0 || length >= needed)
		return path;
	buffer.resize(length);
	return Text::fromT(withoutExtendedPrefix(buffer));
}

/** Length of the volume or share root at the start of a lexical path: "C:\" or "\\server\share". */
size_t rootLength(const std::string& path) {
	if(path.size() >= 3 && path[1] == ':' && isSeparator(path[2]))
		return 3;
	if(path.size() >= 2 && isSeparator(path[0]) && isSeparator(path[1])) {
		auto server = path.find_first_of("\\/", 2);
		if(server == std::string::npos)
			return path.size();
		auto share = path.find_first_of("\\/", server + 1);
		return share == std::string::npos ? path.size() : share;
	}
	return 0;
}

bool isRemote(const std::string& lexical) {
	if(lexical.size() >= 2 && isSeparator(lexical[0]) && isSeparator(lexical[1]))
		return true;
	if(lexical.size() >= 3 && lexical[1] == ':') {
		const auto drive = Text::toT(lexical.substr(0, 3));
		return GetDriveTypeW(drive.c_str()) == DRIVE_REMOTE;
	}
	return false;
}

struct Identity {
	DWORD volume = 0;
	DWORD high = 0;
	DWORD low = 0;
	bool valid() const { return volume != 0 || high != 0 || low != 0; }
	bool operator==(const Identity& rhs) const { return volume == rhs.volume && high == rhs.high && low == rhs.low; }
};

HANDLE openForQuery(const std::string& path) {
	// Follows reparse points, so junctions, symbolic links and mount points resolve to their target.
	return CreateFileW(File::toNativePath(path).c_str(), FILE_READ_ATTRIBUTES,
		FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, nullptr);
}

Identity identityOf(HANDLE h) {
	Identity id;
	BY_HANDLE_FILE_INFORMATION info {};
	if(GetFileInformationByHandle(h, &info)) {
		id.volume = info.dwVolumeSerialNumber;
		id.high = info.nFileIndexHigh;
		id.low = info.nFileIndexLow;
	}
	return id;
}

Identity identityOf(const std::string& path) {
	const auto h = openForQuery(path);
	if(h == INVALID_HANDLE_VALUE)
		return {};
	const auto id = identityOf(h);
	CloseHandle(h);
	return id;
}

std::string finalPathOf(HANDLE h) {
	for(const DWORD flags: { DWORD(FILE_NAME_NORMALIZED | VOLUME_NAME_DOS), DWORD(FILE_NAME_OPENED | VOLUME_NAME_DOS) }) {
		std::wstring buffer(512, L'\0');
		auto length = GetFinalPathNameByHandleW(h, &buffer[0], static_cast<DWORD>(buffer.size()), flags);
		if(length >= buffer.size()) {
			buffer.resize(static_cast<size_t>(length) + 1);
			length = GetFinalPathNameByHandleW(h, &buffer[0], static_cast<DWORD>(buffer.size()), flags);
		}
		if(length > 0 && length < buffer.size()) {
			buffer.resize(length);
			return Text::fromT(withoutExtendedPrefix(buffer));
		}
	}
	return {};
}

bool isUnavailable(DWORD error) {
	switch(error) {
	case ERROR_NOT_READY:
	case ERROR_BAD_NETPATH:
	case ERROR_BAD_NET_NAME:
	case ERROR_NETNAME_DELETED:
	case ERROR_UNEXP_NET_ERR:
	case ERROR_DEV_NOT_EXIST:
	case ERROR_INVALID_DRIVE:
		return true;
	default:
		return false;
	}
}

/** The real location of a lexical path: its nearest existing ancestor resolved through junctions,
 * symbolic links, subst drives and network drive mappings, followed by the missing components.
 * Empty when the location can't be determined for a reason other than missing components.
 * @param id Receives the identity of the path itself when it exists.
 * @param unavailable_ Set when nothing on the path exists (a disconnected drive or server). */
std::string resolvedPath(const std::string& lexical, Identity* id = nullptr, bool* unavailable_ = nullptr) {
	const auto root = rootLength(lexical);
	if(root == 0)
		return {};
	const bool trailing = !lexical.empty() && isSeparator(lexical.back());
	auto existing = lexical;
	while(existing.size() > root && isSeparator(existing.back()))
		existing.pop_back();
	std::string rest;
	for(;;) {
		const auto h = openForQuery(existing.size() == root && root > 3 ? existing + PATH_SEPARATOR_STR : existing);
		if(h != INVALID_HANDLE_VALUE) {
			if(id && rest.empty())
				*id = identityOf(h);
			auto result = finalPathOf(h);
			CloseHandle(h);
			if(result.empty())
				return {};
			if(!rest.empty())
				result = withSeparator(result) + rest;
			return trailing ? withSeparator(result) : result;
		}
		const auto error = GetLastError();
		if(isUnavailable(error)) {
			if(unavailable_)
				*unavailable_ = true;
			return {};
		}
		if(error != ERROR_FILE_NOT_FOUND && error != ERROR_PATH_NOT_FOUND)
			return {};
		if(existing.size() <= root) {
			if(unavailable_)
				*unavailable_ = true;
			return {};
		}
		const auto separator = existing.find_last_of("\\/");
		if(separator == std::string::npos || separator < root - (root > 3 ? 0 : 1))
			return {};
		const auto leaf = existing.substr(separator + 1);
		rest = rest.empty() ? leaf : leaf + PATH_SEPARATOR_STR + rest;
		existing.erase(separator < root ? root : separator);
	}
}

/** Whether the path, or one of its existing ancestors, is the directory with the given identity. */
bool isWithinIdentity(const std::string& lexical, const Identity& target) {
	if(!target.valid())
		return false;
	const auto root = rootLength(lexical);
	if(root == 0)
		return false;
	auto current = lexical;
	while(current.size() > root && isSeparator(current.back()))
		current.pop_back();
	for(;;) {
		const auto id = identityOf(current.size() == root && root > 3 ? current + PATH_SEPARATOR_STR : current);
		if(id.valid() && id == target)
			return true;
		if(current.size() <= root)
			return false;
		const auto separator = current.find_last_of("\\/");
		if(separator == std::string::npos || separator + 1 <= root)
			current.erase(root);
		else
			current.erase(separator);
	}
}
#else
std::string lexicalPath(const std::string& path) {
	return std::filesystem::u8path(path).lexically_normal().u8string();
}

bool isRemote(const std::string&) {
	return false;
}

struct Identity {
	bool valid() const { return false; }
};

std::string resolvedPath(const std::string& lexical, Identity* = nullptr, bool* = nullptr) {
	std::error_code ec;
	const auto canonical = std::filesystem::weakly_canonical(std::filesystem::u8path(lexical), ec);
	if(ec)
		return {};
	return canonical.u8string();
}

bool isWithinIdentity(const std::string&, const Identity&) {
	return false;
}
#endif

/** The storage root as written, as resolved, and its identity (when it exists). */
struct StorageLocation {
	std::string lexical;
	std::string resolved;
	Identity id;
};

StorageLocation storageLocation() {
	StorageLocation location;
	location.lexical = withSeparator(lexicalPath(storageRoot()));
	const auto resolved = resolvedPath(location.lexical, &location.id);
	if(!resolved.empty())
		location.resolved = withSeparator(resolved);
	return location;
}

/** Checks a path's real location against the storage root and staging names.
 * @param ancestors Also compare the identities of the path's ancestors with the storage root,
 * which catches aliases that resolve to another name for the same directory (such as
 * \\localhost\C$\...). */
bool isPrivateLocationResolved(const std::string& lexical, const StorageLocation& storage, bool ancestors) {
	Identity id;
	bool unavailable = false;
	const auto resolved = resolvedPath(lexical, &id, &unavailable);
	if(resolved.empty()) {
		// Nothing exists there (a disconnected drive or server), so it can't lead anywhere.
		if(unavailable)
			return false;
		// Local paths fail closed. A network path whose server can't report its final name is
		// compared as written: private storage is local, so only an alias could lead into it.
		if(!isRemote(lexical))
			return true;
	} else {
		if(withinRoot(resolved, storage.resolved) || withinRoot(resolved, storage.lexical) || hasStagingComponent(resolved))
			return true;
	}
	if(storage.id.valid()) {
		if(id.valid() && id == storage.id)
			return true;
		// Walk the resolved location: a link's own ancestors say nothing about where it leads.
		if(ancestors && isWithinIdentity(resolved.empty() ? lexical : resolved, storage.id))
			return true;
	}
	return false;
}

} // namespace

bool isPrivateLocation(const std::string& path) {
	return isPrivateLocation(path, storageRoot());
}

bool isPrivateLocation(const std::string& path, const std::string& root) {
	return withinRoot(path, root) || hasStagingComponent(path);
}

bool hasPrivateMarker(const std::string& path) {
#ifdef _WIN32
	// NTFS streams follow hard links and renames, so aliases of received files
	// remain excluded even outside the protected receive directory.
	const auto marker = File::toNativePath(path + ":DCPlusPlus.PrivateSend");
	const auto h = CreateFileW(marker.c_str(), FILE_READ_ATTRIBUTES, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
		OPEN_EXISTING, FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
	if(h != INVALID_HANDLE_VALUE) {
		CloseHandle(h);
		return true;
	}
#else
	(void)path;
#endif
	return false;
}

bool isPrivatePath(const std::string& path) {
	const auto lexical = lexicalPath(path);
	const auto storage = storageLocation();
	if(withinRoot(lexical, storage.lexical) || hasStagingComponent(lexical))
		return true;
	if(isPrivateLocationResolved(lexical, storage, true))
		return true;
	return hasPrivateMarker(lexical);
}

struct PrivatePathScanner::Storage {
	StorageLocation location;
	std::string leafName;
};

PrivatePathScanner::PrivatePathScanner() :
	storage(std::make_shared<Storage>())
{
	storage->location = storageLocation();
	auto leaf = storage->location.lexical;
	while(!leaf.empty() && isSeparator(leaf.back()))
		leaf.pop_back();
	storage->leafName = Util::getFileName(leaf);
}

bool PrivatePathScanner::isPrivateRoot(const std::string& directory) const {
	const auto lexical = lexicalPath(directory);
	if(withinRoot(lexical, storage->location.lexical) || hasStagingComponent(lexical))
		return true;
	return isPrivateLocationResolved(lexical, storage->location, true);
}

bool PrivatePathScanner::isPrivateDirectory(const std::string& directory, const std::string& name, bool reparsePoint) const {
	if(isStagingName(name) || withinRoot(directory, storage->location.lexical))
		return true;
	// A reparse point can lead anywhere. A plain directory is where its parent says it is; only one
	// named like the storage root can be that root reached through another name for an ancestor.
	if(reparsePoint)
		return isPrivateLocationResolved(directory, storage->location, true);
	if(Util::stricmp(name, storage->leafName) == 0)
		return isPrivateLocationResolved(directory, storage->location, false);
	return false;
}

bool PrivatePathScanner::isPrivateFile(const std::string& path, const std::string& name, bool reparsePoint) const {
	if(isStagingName(name) || withinRoot(path, storage->location.lexical))
		return true;
	// A symbolic link can point anywhere; a regular file's marker is checked when it is hashed.
	if(reparsePoint)
		return isPrivateLocationResolved(path, storage->location, true) || hasPrivateMarker(path);
	return false;
}

#ifdef _WIN32
namespace {

void check(bool ok) {
	if(!ok)
		throw FileException("Private Send could not safely access the file");
}

struct Handle {
	HANDLE value = INVALID_HANDLE_VALUE;
	Handle() = default;

	explicit Handle(HANDLE h) :
		value(h)
	{ }

	Handle(Handle&& o) noexcept :
		value(o.value)
	{
		o.value = INVALID_HANDLE_VALUE;
	}

	Handle& operator=(Handle&& o) noexcept {
		if(this != &o) {
			close();
			value = o.value;
			o.value = INVALID_HANDLE_VALUE;
		}

		return *this;
	}

	~Handle() { close(); }

	void close() {
		if(value != INVALID_HANDLE_VALUE)
			CloseHandle(value);
		value = INVALID_HANDLE_VALUE;
	}
};

std::wstring absoluteLocal(const std::string& path) {
	const auto wide = Text::toT(path);
	check(wide.size() >= 3 && ((wide[0] >= L'A' && wide[0] <= L'Z') || (wide[0] >= L'a' && wide[0] <= L'z')) && wide[1] == L':' &&
		wide[2] == L'\\');
	check(wide.find(L':', 2) == std::wstring::npos && wide.find(L'/') == std::wstring::npos);
	check(wide.find(L'\0') == std::wstring::npos);

	std::vector<wchar_t> buffer(32768);
	const auto length = GetFullPathNameW(wide.c_str(), static_cast<DWORD>(buffer.size()), buffer.data(), nullptr);
	check(length > 0 && length < buffer.size());

	std::wstring full(buffer.data(), length);
	check(GetDriveTypeW(full.substr(0, 3).c_str()) == DRIVE_FIXED);

	return full;
}

std::vector<Handle> pinAncestors(const std::wstring& file) {
	std::vector<Handle> handles;
	for(size_t pos = 3; pos < file.size(); ++pos)
		if(file[pos] == L'\\') {
			const auto path = file.substr(0, pos);
			Handle h(CreateFileW(path.c_str(), FILE_READ_ATTRIBUTES, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING,
				FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, nullptr));
			check(h.value != INVALID_HANDLE_VALUE);
			BY_HANDLE_FILE_INFORMATION info {};
			check(GetFileInformationByHandle(h.value, &info) != 0);
			check((info.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) && !(info.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT));
			handles.push_back(std::move(h));
		}

	return handles;
}

uint64_t fileSize(HANDLE h) {
	LARGE_INTEGER n {};
	check(GetFileSizeEx(h, &n) != 0 && n.QuadPart >= 0);

	return static_cast<uint64_t>(n.QuadPart);
}

void regular(HANDLE h) {
	BY_HANDLE_FILE_INFORMATION info {};
	check(GetFileType(h) == FILE_TYPE_DISK && GetFileInformationByHandle(h, &info));
	check(!(info.dwFileAttributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT)));
}

void seek(HANDLE h, uint64_t pos) {
	LARGE_INTEGER n {};
	n.QuadPart = static_cast<LONGLONG>(pos);
	check(SetFilePointerEx(h, n, nullptr, FILE_BEGIN));
}

void privateDirectory(const std::wstring& path, bool createOnly = false) {
	// Explicit account SID avoids relying on inherited ACLs from a user-selected
	// or portable application directory. Existing directories are validated below.
	HANDLE token = nullptr;
	check(OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token) != 0);
	Handle ownedToken(token);
	DWORD bytes = 0;
	GetTokenInformation(token, TokenUser, nullptr, 0, &bytes);
	std::vector<unsigned char> buffer(bytes);
	check(bytes && GetTokenInformation(token, TokenUser, buffer.data(), bytes, &bytes));

	LPWSTR sid = nullptr;
	check(ConvertSidToStringSidW(reinterpret_cast<TOKEN_USER*>(buffer.data())->User.Sid, &sid));
	std::wstring descriptor = L"D:P(A;OICI;FA;;;SY)(A;OICI;FA;;;" + std::wstring(sid) + L")";
	LocalFree(sid);

	PSECURITY_DESCRIPTOR sd = nullptr;
	check(ConvertStringSecurityDescriptorToSecurityDescriptorW(descriptor.c_str(), SDDL_REVISION_1, &sd, nullptr));

	SECURITY_ATTRIBUTES sa { sizeof(sa), sd, FALSE };
	const bool made = CreateDirectoryW(path.c_str(), &sa) != 0;
	const auto error = GetLastError();
	if(!made && createOnly) {
		LocalFree(sd);
		check(false);
	}
	if(!made && error == ERROR_ALREADY_EXISTS) {
		const auto attrs = GetFileAttributesW(path.c_str());
		if(attrs == INVALID_FILE_ATTRIBUTES || !(attrs & FILE_ATTRIBUTE_DIRECTORY) || (attrs & FILE_ATTRIBUTE_REPARSE_POINT)) {
			LocalFree(sd);
			check(false);
		}

		if(!SetFileSecurityW(path.c_str(), DACL_SECURITY_INFORMATION | PROTECTED_DACL_SECURITY_INFORMATION, sd)) {
			LocalFree(sd);
			check(false);
		}
	}

	LocalFree(sd);
	check(made || error == ERROR_ALREADY_EXISTS);
}

DWORD CALLBACK copyProgress(LARGE_INTEGER, LARGE_INTEGER, LARGE_INTEGER, LARGE_INTEGER,
	DWORD, DWORD, HANDLE, HANDLE, LPVOID context) {
	try {
		return (*static_cast<const PrivateFile::Cancelled*>(context))() ? PROGRESS_CANCEL : PROGRESS_CONTINUE;
	} catch(...) {
		return PROGRESS_CANCEL;
	}
}

std::string readEvidence(const std::wstring& path) {
	Handle file(CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
		nullptr, OPEN_EXISTING, FILE_FLAG_OPEN_REPARSE_POINT, nullptr));
	check(file.value != INVALID_HANDLE_VALUE);
	char bytes[256];
	DWORD count = 0;
	check(ReadFile(file.value, bytes, sizeof(bytes), &count, nullptr));
	return std::string(bytes, count);
}
}

struct PrivateFile::Impl {
	Handle handle;
	std::vector<Handle> ancestors;
	std::wstring path, directory, target, publicationDirectory, publicationPath;
	std::string offeredName;
	DWORD targetVolume = 0;
	std::mutex mutex;
	uint64_t expected = 0;
	bool destination = false, published = false, verified = false;
};

bool purgeUnfinished() noexcept {
	try {
		const auto pending = absoluteLocal(storageRoot() + "Pending");
		if(GetFileAttributesW(pending.c_str()) == INVALID_FILE_ATTRIBUTES)
			return GetLastError() == ERROR_FILE_NOT_FOUND || GetLastError() == ERROR_PATH_NOT_FOUND;

		auto ancestors = pinAncestors(pending + L"\\placeholder");
		bool clean = true;
		for(const auto& directory : std::filesystem::directory_iterator(pending)) {
			if(!isHex(directory.path().filename().u8string(), 16)) {
				clean = false;
				continue;
			}

			const auto attributes = GetFileAttributesW(directory.path().c_str());
			if(attributes == INVALID_FILE_ATTRIBUTES || !(attributes & FILE_ATTRIBUTE_DIRECTORY) ||
				(attributes & FILE_ATTRIBUTE_REPARSE_POINT)) {
				clean = false;
				continue;
			}

			auto pin = pinAncestors(directory.path().wstring() + L"\\placeholder");
			for(const auto& entry : std::filesystem::directory_iterator(directory.path())) {
				Handle file(CreateFileW(entry.path().c_str(), DELETE | FILE_READ_ATTRIBUTES, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
					FILE_FLAG_OPEN_REPARSE_POINT, nullptr));
				if(file.value == INVALID_HANDLE_VALUE) {
					clean = false;
					continue;
				}

				BY_HANDLE_FILE_INFORMATION info {};
				if(!GetFileInformationByHandle(file.value, &info) ||
					(info.dwFileAttributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT))) {
					clean = false;
					continue;
				}

				FILE_DISPOSITION_INFO disposition { TRUE };
				if(!SetFileInformationByHandle(file.value, FileDispositionInfo, &disposition, sizeof(disposition)))
					clean = false;
			}

			pin.clear();
			if(!RemoveDirectoryW(directory.path().c_str()))
				clean = false;
		}

		return clean;
	} catch(...) {
		return false;
	}
}

PrivateFile::PrivateFile() :
	impl(new Impl)
{
}

PrivateFile::~PrivateFile() {
	if(impl->destination && !impl->published) {
		// Delete by retained handle, not by a path that might now name another file.
		if(impl->handle.value != INVALID_HANDLE_VALUE) {
			FILE_DISPOSITION_INFO info { TRUE };
			SetFileInformationByHandle(impl->handle.value, FileDispositionInfo, &info, sizeof(info));
		}

		impl->handle.close();
		// A failed cross-volume copy can leave a partial file. Keep the parent
		// pinned while acquiring its handle; never clean it up through a path
		// whose directory could have been replaced after releasing our pins.
		if(!impl->publicationPath.empty() && impl->publicationPath != impl->path) {
			Handle partial(CreateFileW(impl->publicationPath.c_str(), DELETE | FILE_READ_ATTRIBUTES, FILE_SHARE_READ,
				nullptr, OPEN_EXISTING, FILE_FLAG_OPEN_REPARSE_POINT, nullptr));
			BY_HANDLE_FILE_INFORMATION info {};
			if(partial.value != INVALID_HANDLE_VALUE && GetFileInformationByHandle(partial.value, &info) &&
				!(info.dwFileAttributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT))) {
				FILE_DISPOSITION_INFO discard { TRUE };
				SetFileInformationByHandle(partial.value, FileDispositionInfo, &discard, sizeof(discard));
			}
		}
		impl->ancestors.clear();
		if(!impl->directory.empty())
			RemoveDirectoryW(impl->directory.c_str());

		// Only remove staging we created, never the user's destination folder.
		if(!impl->publicationDirectory.empty()) {
			RemoveDirectoryW(impl->publicationDirectory.c_str());
		}
	}
}

std::shared_ptr<PrivateFile> PrivateFile::openSource(const std::string& path) {
	auto f = std::shared_ptr<PrivateFile>(new PrivateFile);
	f->impl->path = absoluteLocal(path);
	f->impl->ancestors = pinAncestors(f->impl->path);
	f->impl->handle = Handle(CreateFileW(f->impl->path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
		FILE_FLAG_OPEN_REPARSE_POINT | FILE_FLAG_SEQUENTIAL_SCAN, nullptr));
	check(f->impl->handle.value != INVALID_HANDLE_VALUE);
	regular(f->impl->handle.value);
	f->impl->expected = fileSize(f->impl->handle.value);
	check(f->impl->expected <= MAX_FILE_SIZE && safeName(f->name()));

	return f;
}

std::shared_ptr<PrivateFile> PrivateFile::createDestination(const Metadata& m, const std::string& target) {
	check(safeName(m.name) && isHex(m.id, 16) && m.size <= MAX_FILE_SIZE);
	auto f = std::shared_ptr<PrivateFile>(new PrivateFile);
	const auto selected = target.empty() ? defaultDownloadPath(m.name) : target;
	check(safeName(selected.substr(selected.find_last_of("\\/") + 1)));
	f->impl->target = absoluteLocal(selected);
	const auto filename = f->impl->target.substr(f->impl->target.find_last_of(L'\\') + 1);
	check(safeName(Text::fromT(filename)));
	const auto attributes = GetFileAttributesW(f->impl->target.c_str());
	if(attributes != INVALID_FILE_ATTRIBUTES)
		throw FileException("A file or folder already exists at the selected destination. Choose another filename.");
	check(GetLastError() == ERROR_FILE_NOT_FOUND);
	check(!isPrivatePath(Text::fromT(f->impl->target)));
	f->impl->ancestors = pinAncestors(f->impl->target);
	const auto targetDirectory = f->impl->target.substr(0, f->impl->target.find_last_of(L'\\') + 1);
	Handle targetParent(CreateFileW(targetDirectory.c_str(), FILE_READ_ATTRIBUTES, FILE_SHARE_READ | FILE_SHARE_WRITE,
		nullptr, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, nullptr));
	BY_HANDLE_FILE_INFORMATION targetInfo {};
	check(targetParent.value != INVALID_HANDLE_VALUE && GetFileInformationByHandle(targetParent.value, &targetInfo));
	check((targetInfo.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) && !(targetInfo.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT));
	f->impl->targetVolume = targetInfo.dwVolumeSerialNumber;
	f->impl->ancestors.push_back(std::move(targetParent));
	f->impl->offeredName = m.name;
	auto root = absoluteLocal(storageRoot());
	if(root.back() == L'\\')
		root.pop_back();

	// Pin the parent before creating or changing permissions on our receive tree.
	auto pinned = pinAncestors(root);
	privateDirectory(root);
	const auto staging = root + L"\\Pending";
	privateDirectory(staging);

	const auto dir = staging + L"\\" + Text::toT(randomHex(16));
	privateDirectory(dir);
	f->impl->directory = dir;
	// Scan using the recipient's chosen filename and extension.
	f->impl->path = dir + L"\\" + filename;
	f->impl->destination = true;

	auto stagingPins = pinAncestors(f->impl->path);
	for(auto& h : stagingPins)
		f->impl->ancestors.push_back(std::move(h));

	ULARGE_INTEGER free {};
	check(GetDiskFreeSpaceExW(root.c_str(), &free, nullptr, nullptr) && free.QuadPart >= m.size + 16 * 1024 * 1024);
	check(GetDiskFreeSpaceExW(targetDirectory.c_str(), &free, nullptr, nullptr) && free.QuadPart >= m.size + 16 * 1024 * 1024);

	f->impl->handle = Handle(CreateFileW(f->impl->path.c_str(), GENERIC_READ | GENERIC_WRITE | DELETE, FILE_SHARE_READ, nullptr, CREATE_NEW,
		FILE_FLAG_OPEN_REPARSE_POINT, nullptr));
	check(f->impl->handle.value != INVALID_HANDLE_VALUE);
	regular(f->impl->handle.value);
	f->impl->destination = true;
	f->impl->expected = m.size;

	return f;
}

uint64_t PrivateFile::size() const {
	return impl->expected;
}

std::string PrivateFile::name() const {
	return Text::fromT(impl->path.substr(impl->path.find_last_of(L'\\') + 1));
}

std::string PrivateFile::targetPath() const {
	return Text::fromT(impl->target);
}

size_t PrivateFile::read(uint64_t offset, void* buffer, size_t bytes) {
	std::lock_guard<std::mutex> lock(impl->mutex);

	check(offset <= impl->expected && bytes <= impl->expected - offset && bytes <= 1024 * 1024);
	seek(impl->handle.value, offset);
	DWORD got = 0;
	check(ReadFile(impl->handle.value, buffer, static_cast<DWORD>(bytes), &got, nullptr) && got == bytes);

	return got;
}

void PrivateFile::write(uint64_t offset, const void* buffer, size_t bytes) {
	std::lock_guard<std::mutex> lock(impl->mutex);

	check(impl->destination && !impl->published && !impl->verified && offset <= impl->expected && bytes <= impl->expected - offset &&
		bytes <= 1024 * 1024 && offset == fileSize(impl->handle.value));
	seek(impl->handle.value, offset);
	DWORD written = 0;
	check(WriteFile(impl->handle.value, buffer, static_cast<DWORD>(bytes), &written, nullptr) && written == bytes);
}

PrivateFile::Hashes PrivateFile::hashes(uint64_t bytes, const Cancelled& cancelled) {
	check(bytes <= impl->expected);
	std::unique_ptr<EVP_MD_CTX, decltype(&EVP_MD_CTX_free)> hash(EVP_MD_CTX_new(), EVP_MD_CTX_free);
	check(hash && EVP_DigestInit_ex(hash.get(), EVP_sha256(), nullptr) == 1);

	TigerTree tree(TigerTree::calcBlockSize(static_cast<int64_t>(bytes), 1));
	std::vector<unsigned char> buffer(256 * 1024);

	for(uint64_t pos = 0; pos < bytes;) {
		check(!cancelled());
		const auto n = static_cast<size_t>(std::min<uint64_t>(buffer.size(), bytes - pos));
		read(pos, buffer.data(), n);
		check(EVP_DigestUpdate(hash.get(), buffer.data(), n) == 1);
		tree.update(buffer.data(), n);
		pos += n;
	}

	check(!cancelled());
	unsigned char result[EVP_MAX_MD_SIZE];
	unsigned length = 0;
	check(EVP_DigestFinal_ex(hash.get(), result, &length) == 1 && length == 32);
	tree.finalize();

	return { hex(result, length), tree.getRoot().toBase32() };
}

void PrivateFile::verifyAndScan(const Metadata& m, const Cancelled& cancelled) {
	check(impl->destination && !impl->verified && !impl->published && m.size == impl->expected && m.name == impl->offeredName &&
		fileSize(impl->handle.value) == m.size && FlushFileBuffers(impl->handle.value));
	auto h = hashes(m.size, cancelled);
	check(h.sha256 == m.sha256 && h.tth == m.tth);

	BY_HANDLE_FILE_INFORMATION identity {};
	check(GetFileInformationByHandle(impl->handle.value, &identity));

	// The scanner needs to reopen by path. The directory remains pinned and has
	// a protected ACL while our data handle is temporarily closed.
	impl->handle.close();
	const auto hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
	IAttachmentExecute* attachment = nullptr;

	// Use the SDK type UUIDs; some MinGW uuid libraries omit the corresponding
	// CLSID_AttachmentServices and IID_IAttachmentExecute data symbols.
	auto status = SUCCEEDED(hr) ?
		CoCreateInstance(__uuidof(AttachmentServices), nullptr, CLSCTX_INPROC_SERVER,
			__uuidof(IAttachmentExecute), reinterpret_cast<void**>(&attachment)) :
		hr;
	if(SUCCEEDED(status)) {
		status = attachment->SetLocalPath(impl->path.c_str());
		if(SUCCEEDED(status))
			status = attachment->SetFileName(Text::toT(name()).c_str());
		if(SUCCEEDED(status))
			status = attachment->SetSource(L"https://private-send.invalid/");
		if(SUCCEEDED(status))
			status = attachment->Save();
		attachment->Release();
	}

	if(SUCCEEDED(hr))
		CoUninitialize();

	// Reacquire ownership even when attachment policy failed, for cleanup. Keep
	// write access only while marking provenance, before final verification.
	impl->handle = Handle(CreateFileW(impl->path.c_str(), GENERIC_READ | GENERIC_WRITE | DELETE, FILE_SHARE_READ | FILE_SHARE_WRITE,
		nullptr, OPEN_EXISTING, FILE_FLAG_OPEN_REPARSE_POINT, nullptr));
	check(SUCCEEDED(status) && impl->handle.value != INVALID_HANDLE_VALUE);
	regular(impl->handle.value);
	BY_HANDLE_FILE_INFORMATION reopened {};
	check(GetFileInformationByHandle(impl->handle.value, &reopened));
	check(identity.dwVolumeSerialNumber == reopened.dwVolumeSerialNumber && identity.nFileIndexHigh == reopened.nFileIndexHigh &&
		identity.nFileIndexLow == reopened.nFileIndexLow);

	// Explicit Internet-zone provenance is required even if local attachment
	// policy omitted it. Failure (e.g. a filesystem without ADS) fails closed.
	Handle zone(CreateFileW((impl->path + L":Zone.Identifier").c_str(), GENERIC_WRITE,
		FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr));
	check(zone.value != INVALID_HANDLE_VALUE);
	const char evidence[] = "[ZoneTransfer]\r\nZoneId=3\r\nHostUrl=https://private-send.invalid/\r\n";
	DWORD written = 0;
	check(WriteFile(zone.value, evidence, sizeof(evidence) - 1, &written, nullptr) && written == sizeof(evidence) - 1 &&
		FlushFileBuffers(zone.value));
	zone.close();

	Handle marker(CreateFileW((impl->path + L":DCPlusPlus.PrivateSend").c_str(), GENERIC_WRITE,
		FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr));
	check(marker.value != INVALID_HANDLE_VALUE && WriteFile(marker.value, "PFT1", 4, &written, nullptr) && written == 4 &&
		FlushFileBuffers(marker.value));
	marker.close();

	impl->handle.close();
	impl->handle = Handle(CreateFileW(
		impl->path.c_str(), GENERIC_READ | DELETE, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_FLAG_OPEN_REPARSE_POINT, nullptr));
	check(impl->handle.value != INVALID_HANDLE_VALUE);
	regular(impl->handle.value);
	check(GetFileInformationByHandle(impl->handle.value, &reopened) && identity.dwVolumeSerialNumber == reopened.dwVolumeSerialNumber &&
		identity.nFileIndexHigh == reopened.nFileIndexHigh && identity.nFileIndexLow == reopened.nFileIndexLow);
	check(fileSize(impl->handle.value) == m.size);

	h = hashes(m.size, cancelled);
	check(h.sha256 == m.sha256 && h.tth == m.tth && !cancelled());
	if(reopened.dwVolumeSerialNumber != impl->targetVolume) {
		// Prepare on the destination volume in this worker, before the manager
		// takes its publication lock. The final rename stays atomic and cheap.
		const auto directory = impl->target.substr(0, impl->target.find_last_of(L'\\') + 1) +
			Text::toT(publicationPrefix + randomHex(16));
		privateDirectory(directory, true);
		impl->publicationDirectory = directory;
		SetFileAttributesW(directory.c_str(), FILE_ATTRIBUTE_HIDDEN);
		impl->publicationPath = directory + L"\\" + Text::toT(name());
		auto pins = pinAncestors(impl->publicationPath);
		for(auto& pin : pins)
			impl->ancestors.push_back(std::move(pin));
		check(CopyFileExW(impl->path.c_str(), impl->publicationPath.c_str(), copyProgress,
			const_cast<Cancelled*>(&cancelled), nullptr, COPY_FILE_FAIL_IF_EXISTS) != 0);
		Handle copy(CreateFileW(impl->publicationPath.c_str(), GENERIC_READ | DELETE, FILE_SHARE_READ,
			nullptr, OPEN_EXISTING, FILE_FLAG_OPEN_REPARSE_POINT, nullptr));
		check(copy.value != INVALID_HANDLE_VALUE);
		regular(copy.value);
		check(fileSize(copy.value) == m.size && readEvidence(impl->publicationPath + L":DCPlusPlus.PrivateSend") == "PFT1" &&
			readEvidence(impl->publicationPath + L":Zone.Identifier").find("ZoneId=3") != std::string::npos);
		FILE_DISPOSITION_INFO discard { TRUE };
		check(SetFileInformationByHandle(impl->handle.value, FileDispositionInfo, &discard, sizeof(discard)));
		impl->handle = std::move(copy);
		impl->path = impl->publicationPath;
		h = hashes(m.size, cancelled);
		check(h.sha256 == m.sha256 && h.tth == m.tth && !cancelled());
	}
	impl->verified = true;
}

std::string PrivateFile::publish() {
	std::lock_guard<std::mutex> lock(impl->mutex);

	check(impl->destination && impl->verified && !impl->published);

	std::vector<unsigned char> bytes(sizeof(FILE_RENAME_INFO) + impl->target.size() * sizeof(wchar_t));
	auto info = reinterpret_cast<FILE_RENAME_INFO*>(bytes.data());
	info->ReplaceIfExists = FALSE;
	info->RootDirectory = nullptr;
	info->FileNameLength = static_cast<DWORD>(impl->target.size() * sizeof(wchar_t));
	memcpy(info->FileName, impl->target.data(), info->FileNameLength);
	check(SetFileInformationByHandle(impl->handle.value, FileRenameInfo, info, static_cast<DWORD>(bytes.size())));

	impl->published = true;
	impl->handle.close();
	impl->ancestors.clear();
	RemoveDirectoryW(impl->directory.c_str());
	if(!impl->publicationDirectory.empty())
		RemoveDirectoryW(impl->publicationDirectory.c_str());

	return Text::fromT(impl->target);
}
#else
bool purgeUnfinished() noexcept {
	return true;
}

struct PrivateFile::Impl {};

PrivateFile::PrivateFile() :
	impl(new Impl)
{
}

PrivateFile::~PrivateFile() = default;

std::shared_ptr<PrivateFile> PrivateFile::openSource(const std::string&) {
	throw FileException("Private Send requires Windows");
}

std::shared_ptr<PrivateFile> PrivateFile::createDestination(const Metadata&, const std::string&) {
	throw FileException("Private Send requires Windows");
}

uint64_t PrivateFile::size() const {
	return 0;
}

std::string PrivateFile::name() const {
	return {};
}

std::string PrivateFile::targetPath() const {
	return {};
}

size_t PrivateFile::read(uint64_t, void*, size_t) {
	throw FileException("Private Send requires Windows");
}

void PrivateFile::write(uint64_t, const void*, size_t) {
	throw FileException("Private Send requires Windows");
}

PrivateFile::Hashes PrivateFile::hashes(uint64_t, const Cancelled&) {
	throw FileException("Private Send requires Windows");
}

void PrivateFile::verifyAndScan(const Metadata&, const Cancelled&) {
	throw FileException("Private Send requires Windows");
}

std::string PrivateFile::publish() {
	throw FileException("Private Send requires Windows");
}
#endif
}
}
