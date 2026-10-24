# vim: set filetype: py

EnsureSConsVersion(4, 0, 0)

import os
import sys

from build_util import Dev, gen_po_name

# Global helpers below only need file/alias operations. Avoid probing the host
# compiler twice before creating the explicitly selected build environment.
DefaultEnvironment(tools=[])

# TODO enable "-fdebug-types-section" when
# <https://sourceware.org/bugzilla/show_bug.cgi?id=20645> is resolved.

# TODO enable LTO once feasible.
# See <https://bugs.launchpad.net/dcplusplus/+bug/2031546>

# Disabled GCC warnings:
#   -Wno-missing-field-initializers: Overzealous; makes sense to disable.
#   -Wno-unknown-pragmas: htmlhelp.h emits these.
#   -Wno-unused-[parameter / value]: We have a ton of these; maybe one day...
gcc_flags = {
    "common": [
        "-gdwarf-5",
        "-Wall",
        "-Wextra",
        "-Wno-missing-field-initializers",
        "-Wno-unknown-pragmas",
        "-Wno-unused-parameter",
        "-fexceptions",
    ],
    "debug": [],
    "release": ["-O3"],
}

gcc_xxflags = {"common": [], "debug": [], "release": []}

msvc_flags = {
    # 4100: unreferenced formal parameter
    # 4121: alignment of member sensitive to packing
    # 4127: conditional expression is constant
    # 4131: 'function' : uses old-style declarator - mainly geoip
    # 4189: var init'd, unused
    # 4244: possible loss of data on conversion
    # 4290: exception spec ignored
    # 4307: integral constant overflow (size_t -1 in boost lockfree)
    # 4324: structure padded due to __declspec(align())
    # 4355: "this" used in a constructor
    # 4456: declaration of 'identifier' hides previous local declaration
    # 4457: declaration of 'identifier' hides function parameter
    # 4458: declaration of 'identifier' hides class member
    # 4510: no default constructor
    # 4512: assn not generated
    # 4610: no default constructor
    # 4706: assignment within conditional expression
    # 4800: converting from BOOL to bool
    # 4996: fn unsafe, use fn_s
    "common": [
        "/W4",
        "/EHsc",
        "/Zi",
        "/Zm200",
        "/GR",
        "/FC",
        "/wd4100",
        "/wd4121",
        "/wd4127",
        "/wd4131",
        "/wd4189",
        "/wd4244",
        "/wd4290",
        "/wd4307",
        "/wd4324",
        "/wd4355",
        "/wd4456",
        "/wd4457",
        "/wd4458",
        "/wd4510",
        "/wd4512",
        "/wd4610",
        "/wd4706",
        "/wd4800",
        "/wd4996",
        "/wd4005",
    ],
    "debug": ["/MDd"],
    "release": ["/MD", "/O2"],
}

msvc_xxflags = {"common": ["/std:c++17"], "debug": [], "release": []}

gcc_link_flags = {
    # ASLR with high entropy and all the other required options for it to work
    # are all on by default on any mingw-w64 toolchains built after mid 2020.
    # --dynamicbase left here for compatibility with old mingw-w64 builds
    # where ASLR doesn't work at all due to bad defaults but at least it sets
    # the Dynamic Base bit in the EXE header which satisfies modern Windows'
    # security so the EXE won't be outright blocked. See L#2039677.
    # @todo remove when we drop support for older mingw-w64 builds / versions
    "common": ["-gdwarf-5", "-Wl,--no-undefined,--nxcompat,--dynamicbase", "-time"],
    "debug": [],
    "release": ["-O3"],
}

msvc_link_flags = {
    "common": [
        "/DEBUG",
        "/FIXED:NO",
        "/INCREMENTAL:NO",
        "/SUBSYSTEM:WINDOWS",
        "/MANIFESTUAC:NO",
    ],
    "debug": [],
    "release": [],
}

msvc_defs = {
    "common": [
        "_REENTRANT",
    ],
    "debug": ["_DEBUG", "_HAS_ITERATOR_DEBUGGING=0", "_SECURE_SCL=0"],
    "release": ["NDEBUG"],
}

gcc_defs = {
    "common": ["_REENTRANT", "NO_VIZ"],
    "debug": ["_DEBUG"],
    "release": ["NDEBUG"],
}

# defEnv will hold a temporary Environment used to get options that can't be
# set after the actual Environment has been created
defEnv = Environment(tools=[], ENV=os.environ)
opts = Variables("custom.py", ARGUMENTS)

if sys.platform == "win32":
    tooldef = "mingw"
else:
    tooldef = "default"

opts.AddVariables(
    EnumVariable(
        "tools",
        "Toolset to compile with (default: mingw on Windows, default elsewhere)",
        tooldef,
        ["mingw", "default"],
    ),
    EnumVariable("mode", "Compile mode", "debug", ["debug", "release"]),
    BoolVariable("pch", "Use precompiled headers", "no"),
    BoolVariable("verbose", "Show verbose command lines", "no"),
    BoolVariable(
        "savetemps",
        "Save intermediate compilation files (assembly output)",
        "no",
    ),
    BoolVariable("i18n", "Rebuild i18n files", "no"),
    BoolVariable("help", "Build help files (requires i18n=1)", "yes"),
    BoolVariable(
        "webhelp",
        "Build help files for the web (requires help=1)",
        "no",
    ),
    (
        "prefix",
        "Prefix to use when calling GCC build tools; defaults to:\n"
        "- nothing for Linux builds.\n"
        '- "i686-w64-mingw32-" for 32-bit MinGW builds.\n'
        '- "x86_64-w64-mingw32-" for 64-bit MinGW builds.\n'
        'May be forced to nothing via "prefix=".',
    ),
    EnumVariable("arch", "Target architecture", "x64", ["x86", "x64"]),
    BoolVariable("msvcproj", "Build MSVC project files", "no"),
    ("test_filter", "GoogleTest filter for explicitly requested tests", "*"),
    ("test_timeout", "Maximum seconds per automated test executable", "600"),
    BoolVariable("test_registry", "Include isolated HKCU registry regression checks", "no"),
    BoolVariable(
        "distro",
        "Produce the official distro (forces tools=mingw, mode=release, "
        "i18n=1, help=1, webhelp=1, arch=x64)",
        "no",
    ),
)

opts.Update(defEnv)
Help(opts.GenerateHelpText(defEnv))

if defEnv["distro"]:
    defEnv["tools"] = "mingw"
    defEnv["mode"] = "release"
    defEnv["i18n"] = 1
    defEnv["help"] = 1
    defEnv["webhelp"] = 1
    defEnv["arch"] = "x64"

# SCons' MSVC architecture table uses amd64, not our public x64 option.
TARGET_ARCH = defEnv["arch"]
if TARGET_ARCH == "x64":
    TARGET_ARCH = "amd64"

env = Environment(
    ENV=os.environ,
    tools=[defEnv["tools"]],
    options=opts,
    TARGET_ARCH=TARGET_ARCH,
    MSVS_ARCH=TARGET_ARCH,
    MSVC_USE_SCRIPT=True,
)

if env["distro"]:
    env["tools"] = "mingw"
    env["mode"] = "release"
    env["i18n"] = 1
    env["help"] = 1
    env["webhelp"] = 1
    env["arch"] = "x64"


dev = Dev(env)
dev.prepare()

test_aliases = {"theme-test", "geoip-test", "attachment-drop-test"}
test_targets = [str(t).replace("\\", "/").rstrip("/") for t in COMMAND_LINE_TARGETS]


def is_test_target(target):
    return (target in test_aliases or target in ("test", "test-build", "test-manual", "test-bench")
            or target.startswith(("test/", "test-build/", "test-manual/", "test-bench/"))
            or "/test/" in target)


dev.tests_only = bool(test_targets) and all(is_test_target(t) for t in test_targets) and not env["distro"]

env.Append(CPPPATH=["#/"])

if dev.is_win32():
    # Windows header defines
    # <https://msdn.microsoft.com/en-us/library/aa383745(VS.85).aspx>
    env.Append(
        CPPDEFINES=[
            "_WIN32_WINNT=0x601",  # Windows 7
            "WINVER=0x601",  # Windows 7
            "_WIN32_IE=0x0A00",  # expose current common-control declarations
            # other defs that influence Windows headers
            "NOMINMAX",
            "STRICT",
            "WIN32_LEAN_AND_MEAN",
        ]
    )

    env.Append(CPPDEFINES=["UNICODE", "_UNICODE"])

    # zlib defines
    env.Append(CPPDEFINES=["CASESENSITIVITYDEFAULT_YES", "ZLIB_WINAPI"])

if "gcc" in env["TOOLS"]:
    if env["savetemps"]:
        env.Append(CCFLAGS=["-save-temps", "-fverbose-asm"])
    else:
        env.Append(CCFLAGS=["-pipe"])

    # Require SSE2 for e.g., significantly faster Tiger hashing and generally
    # to allow 32-bit builds to perform 64-bit arithmetic with more facility
    # https://www.cryptopp.com/benchmarks.html
    # Require SSE3 for fisttp
    if env["arch"] == "x86":
        env.Append(CCFLAGS=["-march=nocona", "-mtune=generic"])  # Through SSE3
    elif env["arch"] == "x64":
        env.Append(CCFLAGS=["-march=nehalem", "-mtune=generic"])  # Through SSE4

if "msvc" in env["TOOLS"]:
    env["pch"] = True
if env["pch"]:
    env.Append(CPPDEFINES=["HAS_PCH"])

if "mingw" in env["TOOLS"]:
    env.Append(CCFLAGS=["-mthreads"])

    # https://gcc.gnu.org/bugzilla/show_bug.cgi?id=40838
    # ("gcc shouldn't assume that the stack is aligned")
    # https://eigen.tuxfamily.org/dox/group__TopicWrongStackAlignment.html
    # Not necessary on either x86 or amd64 Linux or 64-bit Windows.
    # https://msdn.microsoft.com/en-us/library/ms235286.aspx on
    # "Overview of x64 Calling Conventions" says 64-bit Windows stacks are
    # already 16-byte aligned.
    if env["arch"] == "x86":
        env.Append(CCFLAGS=["-mincoming-stack-boundary=2", "-mstackrealign"])

    env.Append(
        LINKFLAGS=[
            # Keep the MinGW runtimes, including winpthreads, in the executable.
            "-static",
            "-mthreads",
            "-Wl,--enable-runtime-pseudo-reloc",
        ]
    )

    if env["mode"] == "release" or sys.platform == "win32":
        env.Append(CCFLAGS=["-mwindows"])
        env.Append(LINKFLAGS=["-mwindows"])
    else:
        env.Append(CPPDEFINES=["CONSOLE"])

    env.Append(CPPPATH=["#/mingw/preload/", "#/mingw/include/"])
    mingw_lib = "#/mingw/lib/"
    if env["arch"] != "x86":
        mingw_lib = mingw_lib + env["arch"] + "/"
    env.Append(LIBPATH=[mingw_lib])

if "msvc" in env["TOOLS"]:
    flags = msvc_flags
    xxflags = msvc_xxflags
    link_flags = msvc_link_flags
    defs = msvc_defs

    env.Append(LIBS=["User32", "shell32", "Advapi32"])

else:
    flags = gcc_flags
    xxflags = gcc_xxflags
    link_flags = gcc_link_flags
    defs = gcc_defs

    env.Tool("gch", toolpath=".")

env.Append(CPPDEFINES=defs[env["mode"]])
env.Append(CPPDEFINES=defs["common"])

env.Append(CCFLAGS=flags[env["mode"]])
env.Append(CCFLAGS=flags["common"])

env.Append(CXXFLAGS=xxflags[env["mode"]])
env.Append(CXXFLAGS=xxflags["common"])

env.Append(LINKFLAGS=link_flags[env["mode"]])
env.Append(LINKFLAGS=link_flags["common"])

import SCons.Scanner

SWIGScanner = SCons.Scanner.ClassicCPP(
    "SWIGScan",
    ".i",
    "CPPPATH",
    '^[ \t]*[%,#][ \t]*(?:include|import)[ \t]*(<|")([^>"]+)(>|")',
)
env.Append(SCANNERS=[SWIGScanner])

#
# internationalization (ardour.org provided the initial idea)
#

po_args = ["msgmerge", "-q", "--update", "--backup=none", "$TARGET", "$SOURCE"]
po_bld = Builder(action=Action([po_args], "Updating translation $TARGET from $SOURCES"))
env.Append(BUILDERS={"PoBuild": po_bld})

mo_args = ["msgfmt", "-c", "-o", "$TARGET", "$SOURCE"]
mo_bld = Builder(
    action=[
        Action([mo_args], "Compiling message catalog $TARGET from $SOURCES"),
        Action(
            lambda target, source, env: gen_po_name(source[0], env),
            "Generating $NAME_FILE",
        ),
    ]
)
env.Append(BUILDERS={"MoBuild": mo_bld})

pot_args = [
    "xgettext",
    "--from-code=UTF-8",
    "--foreign-user",
    "--package-name=$PACKAGE",
    "--copyright-holder=Jacek Sieka",
    "--msgid-bugs-address=dcplusplus-devel@lists.sourceforge.net",
    "--no-wrap",
    "--keyword=_",
    "--keyword=T_",
    "--keyword=TF_",
    "--keyword=TFN_:1,2",
    "--keyword=F_",
    "--keyword=gettext_noop",
    "--keyword=N_",
    "--keyword=CT_",
    "--boost",
    "-s",
    "--output=$TARGET",
    '${list(filter(lambda _: not str(_).endswith((".o", ".obj")), SOURCES))}'
]
pot_bld = Builder(
    action=Action([pot_args], "Extracting messages to $TARGET from $SOURCES")
)
env.Append(BUILDERS={"PotBuild": pot_bld})


def CheckFlag(context, flag):
    context.Message("Checking support for the " + flag + " flag... ")
    prevFlags = context.env["CCFLAGS"]
    context.env.Append(CCFLAGS=[flag])
    ret = context.TryCompile("int main() {}", ".cpp")
    context.env.Replace(CCFLAGS=prevFlags)
    context.Result(ret)
    return ret


conf = Configure(
    env,
    custom_tests={"CheckFlag": CheckFlag},
    conf_dir=env.Dir(dev.get_build_path(".sconf_temp")).abspath,
    log_file=env.File(dev.get_build_path("config.log")).abspath,
    clean=False,
    help=False,
)

if dev.is_win32():
    # some mingw distros have one but not the other, breaking linking (note:
    # CheckLib functions do not support explicit include_quotes)
    if conf.CheckCXXHeader(["windows.h", "htmlhelp.h"], "<>") and conf.CheckLib(
        "htmlhelp", "main", None, "C++"
    ):
        conf.env.Append(CPPDEFINES="HAVE_HTMLHELP_H")

if "mingw" in env["TOOLS"]:
    # some toolchains come with openssl import libraries
    ssl_lib = "#/openssl/lib/"
    if env["arch"] != "x86":
        ssl_lib += env["arch"] + "/"
    if not (env.File(ssl_lib + "libssl.a").exists() and
        env.File(ssl_lib + "libcrypto.a").exists()):
            print("WARNING: OpenSSL static libraries not found in %s; "
                "OpenSSL may be linked dynamically or not at all." % ssl_lib)
            if defEnv["distro"]:
                raise SCons.Errors.StopError(
                    "Required OpenSSL static libraries not found in %s" % ssl_lib)

    # see whether we're compiling with MinGW or MinGW-w64 (2 different projects
    # that can both build a 32-bit program). the only differentiator is
    # __MINGW64_VERSION_MAJOR.
    if not conf.CheckDeclaration(
        "__MINGW64_VERSION_MAJOR", "#include <windows.h>", "C++"
    ):
        conf.env.Append(CPPDEFINES="HAVE_OLD_MINGW")

if "gcc" in env["TOOLS"] and env["mode"] == "debug":
    if conf.CheckFlag("-Og"):
        conf.env.Append(CCFLAGS=["-Og"])
        conf.env.Append(LINKFLAGS=["-Og"])

env = conf.Finish()

# TODO run config tests to determine which libs to build

dev.dwarf = dev.build("dwarf/") if "mingw" in dev.env["TOOLS"] else []
dev.zlib = dev.build("zlib/")
dev.zstd = dev.build("zstd/")
dev.sqlite = dev.build("sqlite/")
dev.bzip2 = dev.build("bzip2/") if dev.is_win32() else []
dev.maxminddb = dev.build("maxminddb/")
dev.intl = dev.build("intl/") if dev.is_win32() else []
dev.miniupnpc = dev.build("miniupnpc/")
dev.natpmp = dev.build("natpmp/")
dev.md4c = dev.build("md4c/")
dev.client = dev.build("dcpp/")

dev.dwt = dev.build("dwt/src/") if dev.is_win32() else []

if dev.is_win32():
    dev.build("help/")

dev.build("utils/")

if dev.is_win32():
    application = dev.build("win32/")

    dev.build("installer/")

# Normal application builds do not compile or run tests. Register their build
# graphs only when a root test target (or a legacy integration alias) is requested.
if any(is_test_target(t) for t in test_targets):
    dev.build("test/")

if not env["distro"] and not env["msvcproj"]:
    Default(application if dev.is_win32() else dev.client)

dev.finalize()
