/*
 * Copyright (C) 2026 iceman50
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

#ifndef DCPLUSPLUS_WIN32_ICON_MANAGER_H
#define DCPLUSPLUS_WIN32_ICON_MANAGER_H

#include <cstddef>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include <dwt/resources/Icon.h>

/** Discovers .dcico ZIP packages and loads their application icons with a compiled-resource fallback.
 *
 * An empty ICON_PACK setting selects Dark.dcico only for an effective dark appearance. An explicit
 * package applies to either appearance; EMBEDDED_PACK always uses executable resources.
 * Archive members are read with strict size and path limits,
 * materialized in the process temporary directory for Win32 LoadImage, and cached by resource and size.
 */
namespace dcpp { class Archive; }

class IconManager {
public:
	static constexpr const char* EMBEDDED_PACK = "@embedded";

	struct Package {
		std::string name;
		std::string version;
		std::string scheme;
		std::string path;
		std::size_t iconCount;
	};

	struct RuntimeInfo {
		bool initialized = false;
		bool packResolved = false;
		Package package {};
		std::size_t cachedPackageIcons = 0;
		std::size_t cachedEmbeddedIcons = 0;
	};

	/** Snapshot of the current cache, counted by resource and size; does not load icons or packages. */
	static RuntimeInfo getRuntimeInfo();

	/** Enables package-backed icon loading after the core settings singleton has been created. */
	static void initialize() noexcept;
	static void setDarkMode(bool enabled) noexcept;
	static void reload() noexcept;
	static dwt::IconPtr load(unsigned resourceId, long size);
	static dwt::IconPtr buttonIcon(unsigned id) { return load(id, 16); }
	static dwt::IconPtr menuIcon(unsigned id) { return load(id, 16); }
	static dwt::IconPtr statusIcon(unsigned id) { return load(id, 16); }
	static dwt::IconPtr tabIcon(unsigned id) { return load(id, 16); }
	static dwt::IconPtr toolbarIcon(unsigned id);
	static const char* getFileName(unsigned resourceId) noexcept;
	static Package inspectPackage(const std::string& path);

	/** Every icon a package can replace, in a fixed order. */
	static std::vector<unsigned> getIconIds();
	/** The package an ICON_PACK value uses with the current light or dark appearance: its path,
	 * or an empty string for the icons in the executable. */
	static std::string resolvePackage(const std::string& setting);

	/** The icons of one package, without switching the application to it. The executable's icon
	 * stands in for one the package doesn't have. */
	class Preview {
	public:
		/** `package` is a package path, or empty (or EMBEDDED_PACK) for the executable's icons. */
		explicit Preview(const std::string& package);
		~Preview();
		/** An icon of the package; the package stays open while the preview exists, and its icons
		 * are read straight from it, without temporary files. */
		dwt::IconPtr load(unsigned resourceId, long size);
		/** Whether the package could be read; the executable's icons are shown otherwise. */
		bool isPackage() const { return !path.empty(); }

	private:
		std::string path;
		std::unordered_map<std::string, std::string> entries;
		std::unique_ptr<dcpp::Archive> archive;
	};
	static std::vector<Package> getPackages();
	static std::string getDirectory();
};

#endif
