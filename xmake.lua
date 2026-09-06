add_rules("mode.debug", "mode.release", "mode.coverage")

add_requires("doctest", {alias = "doctest"})
add_requires("abseil", {alias = "abseil"})
add_requires("nlohmann_json", {alias = "nlohmann_json"})

set_languages("c++20")

if is_mode("release") then
    set_optimize("fastest")
end

if is_mode("coverage") then
    add_cxflags("-ftest-coverage", "-fprofile-arcs", {force = true})
end

if is_plat("linux") then
    set_warnings("all", "error")
    local termux_prefix = os.getenv("PREFIX")
    if termux_prefix then
        add_cxflags("-Wno-unused-command-line-argument", {force = true})
        add_sysincludedirs(termux_prefix .. "/include/c++/v1", {public = true})
        add_sysincludedirs(termux_prefix .. "/include", {public = true})
    end
elseif is_plat("windows") then
    add_cxflags("/EHsc /utf-8 /W4 /WX /wd4702", { force = true })
end

-- sibling header include directories (read-only, resolved by relative path)
local sibling_includes = {
    "../digraphx-cpp/include",
    "../fractions-cpp/include",
    "../mywheel-cpp/include",
    "../netlistx-cpp/include",
    "../physdes-cpp/include",
    "../py2cpp/include",
    "../xnetwork-cpp/include",
}

-- compiled library: large function bodies live in source/*.cpp, the headers
-- only declare them (bodies <= 15 lines stay inline)

target("NnsPlace")
    set_kind("static")
    add_includedirs("include", {public = true})
    for _, dir in ipairs(sibling_includes) do
        add_includedirs(dir, {public = true})
    end
    add_files("source/*.cpp")
    add_packages("abseil", "nlohmann_json", {public = true})

target("test_nnsplace")
    set_kind("binary")
    add_deps("NnsPlace")
    add_includedirs("include", {public = true})
    add_files("test/source/*.cpp")
    add_packages("doctest", "abseil")
    add_tests("default")
    set_rundir(os.projectdir()) -- resolve "testcases/p1.json" at runtime

target("nnsplace_standalone")
    set_kind("binary")
    add_deps("NnsPlace")
    add_includedirs("include", {public = true})
    add_files("standalone/source/main.cpp")
    add_packages("abseil", "nlohmann_json")
    set_rundir(os.projectdir()) -- resolve "testcases/p1.json" at runtime

--
-- If you want to known more usage about xmake, please see https://xmake.io
--
-- ## FAQ
--
-- You can enter the project directory firstly before building project.
--
--   $ cd projectdir
--
-- 1. How to build project?
--
--   $ xmake
--
-- 2. How to configure project?
--
--   $ xmake f -p [macosx|linux|iphoneos ..] -a [x86_64|i386|arm64 ..] -m [debug|release]
--
-- 3. Where is the build output directory?
--
--   The default output directory is `./build` and you can configure the output directory.
--
--   $ xmake f -o outputdir
--   $ xmake
--
-- 4. How to run and debug target after building project?
--
--   $ xmake run [targetname]
--   $ xmake run -d [targetname]
--
-- 5. How to install target to the system directory or other output directory?
--
--   $ xmake install
--   $ xmake install -o installdir
--
-- 6. Add some frequently-used compilation flags in xmake.lua
--
-- @code
--    -- add debug and release modes
--    add_rules("mode.debug", "mode.release")
--
--    -- add macro defination
--    add_defines("NDEBUG", "_GNU_SOURCE=1")
--
--    -- set warning all as error
--    set_warnings("all", "error")
--
--    -- set language: c99, c++11
--    set_languages("c99", "c++11")
--
--    -- set optimization: none, faster, fastest, smallest
--    set_optimize("fastest")
--
--    -- add include search directories
--    add_includedirs("/usr/include", "/usr/local/include")
--
--    -- add link libraries and search directories
--    add_links("tbox")
--    add_linkdirs("/usr/local/lib", "/usr/lib")
--
--    -- add system link libraries
--    add_syslinks("z", "pthread")
--
--    -- add compilation and link flags
--    add_cxflags("-stdnolib", "-fno-strict-aliasing")
--    add_ldflags("-L/usr/local/lib", "-lpthread", {force = true})
--
-- @endcode
--
