/* Copyright (C) 2026 iceman50
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "stdinc.h"

#include "PrivateTransferStorage.h"

#include "File.h"
#include "MerkleTree.h"
#include "Text.h"
#include "Util.h"

#include <openssl/evp.h>
#include <filesystem>
#include <mutex>
#ifdef _WIN32
#include <sddl.h>
#include <shobjidl.h>
#endif

namespace dcpp {
namespace pft {

std::string storageRoot() {
	return Util::getPath(Util::PATH_USER_LOCAL) + "PrivateSend" PATH_SEPARATOR_STR;
}

bool isPrivatePath(const std::string& path) {
	auto root = storageRoot();
	auto within = [](const std::string& p, const std::string& r) {
		return Util::strnicmp(p, r, r.size()) == 0 || Util::stricmp(p, r.substr(0, r.size() - 1)) == 0;
	};
	if(within(path, root))
		return true;

	// Canonicalization follows directory junctions and normalizes '..'. A
	// failure denies sharing rather than interpreting an ambiguous path.
	std::error_code ec;
	const auto canonical = std::filesystem::weakly_canonical(std::filesystem::u8path(path), ec);
	if(ec)
		return true;

	const auto canonicalRoot = std::filesystem::weakly_canonical(std::filesystem::u8path(root), ec);
	if(ec)
		return true;

	auto canonicalRootString = canonicalRoot.u8string();
	while(!canonicalRootString.empty() && (canonicalRootString.back() == '/' || canonicalRootString.back() == '\\'))
		canonicalRootString.pop_back();
	if(within(canonical.u8string(), canonicalRootString + PATH_SEPARATOR_STR))
		return true;
#ifdef _WIN32
	// NTFS streams follow hard links and renames, so aliases of received files
	// remain excluded even outside the protected receive directory.
	const auto marker = Text::toT(path) + L":DCPlusPlus.PrivateSend";
	const auto h = CreateFileW(marker.c_str(), FILE_READ_ATTRIBUTES, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
		OPEN_EXISTING, FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
	if(h != INVALID_HANDLE_VALUE) {
		CloseHandle(h);

		return true;
	}
#endif
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

void privateDirectory(const std::wstring& path) {
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
}

struct PrivateFile::Impl {
	Handle handle;
	std::vector<Handle> ancestors;
	std::wstring path, directory, target;
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
		impl->ancestors.clear();
		if(!impl->directory.empty())
			RemoveDirectoryW(impl->directory.c_str());

		// Acceptance creates a destination directory before publication. Remove
		// that empty directory on failure too; RemoveDirectory never deletes an
		// existing received file or any other nonempty directory.
		if(!impl->target.empty())
			RemoveDirectoryW(impl->target.substr(0, impl->target.find_last_of(L'\\')).c_str());
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

std::shared_ptr<PrivateFile> PrivateFile::createDestination(const Metadata& m) {
	check(safeName(m.name) && isHex(m.id, 16) && m.size <= MAX_FILE_SIZE);
	auto f = std::shared_ptr<PrivateFile>(new PrivateFile);
	auto root = absoluteLocal(storageRoot());
	if(root.back() == L'\\')
		root.pop_back();

	// Pin the parent before creating or changing permissions on our receive tree.
	auto pinned = pinAncestors(root);
	privateDirectory(root);
	const auto staging = root + L"\\Pending";
	privateDirectory(staging);
	const auto received = root + L"\\Received";
	privateDirectory(received);

	const auto dir = staging + L"\\" + Text::toT(randomHex(16));
	privateDirectory(dir);
	f->impl->directory = dir;
	f->impl->path = dir + L"\\" + Text::toT(m.name);
	f->impl->destination = true;
	const auto targetDirectory = received + L"\\" + Text::toT(m.id);
	f->impl->target = targetDirectory + L"\\" + Text::toT(m.name);
	privateDirectory(targetDirectory);

	f->impl->ancestors = pinAncestors(f->impl->path);
	auto targetPins = pinAncestors(f->impl->target);
	for(auto& h : targetPins)
		f->impl->ancestors.push_back(std::move(h));

	ULARGE_INTEGER free {};
	check(GetDiskFreeSpaceExW(root.c_str(), &free, nullptr, nullptr) && free.QuadPart >= m.size + 16 * 1024 * 1024);

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
	check(impl->destination && !impl->verified && !impl->published && m.size == impl->expected && m.name == name() &&
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
			status = attachment->SetFileName(Text::toT(m.name).c_str());
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

std::shared_ptr<PrivateFile> PrivateFile::createDestination(const Metadata&) {
	throw FileException("Private Send requires Windows");
}

uint64_t PrivateFile::size() const {
	return 0;
}

std::string PrivateFile::name() const {
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
