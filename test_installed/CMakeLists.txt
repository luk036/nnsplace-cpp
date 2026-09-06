cmake_minimum_required(VERSION 3.14)
project(NnsPlaceInstalledTest LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Find dependencies first - the installed config references fmt, NetlistX,
# DiGraphX, Recti, MyWheel, XNetwork, Py2Cpp and nlohmann_json.
find_package(fmt REQUIRED)
find_package(spdlog REQUIRED)
find_package(NetlistX REQUIRED)
find_package(DiGraphX REQUIRED)
find_package(Recti REQUIRED)
find_package(MyWheel REQUIRED)
find_package(XNetwork REQUIRED)
find_package(Py2Cpp REQUIRED)
find_package(nlohmann_json REQUIRED)
find_package(NnsPlace REQUIRED)

add_executable(nnsplace_installed_test main.cpp)
target_link_libraries(nnsplace_installed_test PRIVATE NnsPlace::NnsPlace Threads::Threads)

enable_testing()
add_test(NAME nnsplace_installed_test COMMAND nnsplace_installed_test)
