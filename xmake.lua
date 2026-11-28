add_rules("mode.debug", "mode.release", "mode.coverage")
add_requires("doctest", "abseil", "nlohmann_json")

set_languages("c++20")

if is_mode("release") then
    set_optimize("fastest")
end

if is_mode("coverage") then
    add_cxflags("-ftest-coverage", "-fprofile-arcs", {force = true})
end

if is_plat("linux") then
    set_warnings("all", "error")
    -- Termux/Android ships its libc++ headers under $PREFIX
    local prefix = os.getenv("PREFIX")
    if prefix then
        add_cxflags("-Wno-unused-command-line-argument", {force = true})
        add_sysincludedirs(prefix .. "/include/c++/v1", prefix .. "/include", {public = true})
    end
elseif is_plat("windows") then
    add_cxflags("/EHsc /utf-8 /W4 /WX /wd4702", {force = true})
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

add_includedirs("include")
for _, dir in ipairs(sibling_includes) do
    add_includedirs(dir)
end

-- testcases/*.json are opened relative to the project dir at runtime
set_rundir(os.projectdir())

target("NnsPlace")
    set_kind("static")
    add_files("source/*.cpp")
    -- physdes-cpp global router (explicitly instantiated for Point<int,int>)
    add_files("../physdes-cpp/source/global_router.cpp")
    add_packages("abseil", "nlohmann_json", {public = true})

target("test_nnsplace")
    add_deps("NnsPlace")
    add_files("test/source/*.cpp")
    add_packages("doctest", "abseil")
    add_tests("default")

target("nnsplace_standalone")
    add_deps("NnsPlace")
    add_files("standalone/source/main.cpp")
    add_packages("abseil", "nlohmann_json")

target("nnsplace_figures")
    add_deps("NnsPlace")
    add_files("experiments/figures.cpp")
    add_packages("abseil", "nlohmann_json")
