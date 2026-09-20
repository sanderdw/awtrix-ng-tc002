cmake_minimum_required(VERSION 3.20)
project(awtrix_tc002 VERSION 1.1.0 LANGUAGES C CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_C_STANDARD 99)
find_package(Threads REQUIRED)
# Upstream AWTRIX NG is a pinned submodule that is never edited. The TC002 patch series in
# patches/ is applied into build-upstream/ by tools/upstream.py, and that is what gets compiled.
find_program(TC002_PYTHON NAMES python3 python REQUIRED DOC "Python for tools/upstream.py")
file(GLOB TC002_PATCHES CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/patches/*.patch)
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${TC002_PATCHES}
  ${CMAKE_CURRENT_SOURCE_DIR}/.gitmodules ${CMAKE_CURRENT_SOURCE_DIR}/tools/upstream.py)
execute_process(COMMAND ${TC002_PYTHON} ${CMAKE_CURRENT_SOURCE_DIR}/tools/upstream.py apply
  WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR} RESULT_VARIABLE TC002_UPSTREAM_RESULT)
if(NOT TC002_UPSTREAM_RESULT EQUAL 0)
  message(FATAL_ERROR "tools/upstream.py apply failed; see the output above")
endif()
set(U ${CMAKE_CURRENT_SOURCE_DIR}/build-upstream)
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/tools/webui_brand.py)
execute_process(COMMAND ${TC002_PYTHON} ${CMAKE_CURRENT_SOURCE_DIR}/tools/webui_brand.py
  WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR} RESULT_VARIABLE TC002_WEBUI_RESULT)
if(NOT TC002_WEBUI_RESULT EQUAL 0)
  message(FATAL_ERROR "tools/webui_brand.py failed; see the output above")
endif()
# Hashes of the vendor files the port calls into, compiled in from src/tc002/vendor-fingerprints.json.
set(TC002_GENERATED ${CMAKE_BINARY_DIR}/generated)
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
  ${CMAKE_CURRENT_SOURCE_DIR}/src/tc002/vendor-fingerprints.json ${CMAKE_CURRENT_SOURCE_DIR}/tools/vendor_fingerprints.py)
execute_process(COMMAND ${TC002_PYTHON} ${CMAKE_CURRENT_SOURCE_DIR}/tools/vendor_fingerprints.py generate
  --output ${TC002_GENERATED}/VendorFingerprints.h
  WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR} RESULT_VARIABLE TC002_FINGERPRINT_RESULT)
if(NOT TC002_FINGERPRINT_RESULT EQUAL 0)
  message(FATAL_ERROR "tools/vendor_fingerprints.py failed; see the output above")
endif()
# The clock's panel geometry, applied through the build-time knobs the patch series adds upstream.
set(TC002_PANEL AWTRIX_MATRIX_HEIGHT=16 AWTRIX_PANEL_WIDTH=52)
file(GLOB_RECURSE CORE CONFIGURE_DEPENDS ${U}/src/core/*.cpp ${U}/src/core/*.c)
file(GLOB SIM CONFIGURE_DEPENDS ${U}/src/sim/*.cpp)
list(FILTER SIM EXCLUDE REGEX "main_sim\\.cpp$")
file(GLOB TC002 CONFIGURE_DEPENDS src/tc002/*.cpp)
file(GLOB BERRY CONFIGURE_DEPENDS ${U}/lib/berry/src/*.c ${U}/lib/berry/default/*.c)
add_library(berry STATIC ${BERRY})
target_include_directories(berry PUBLIC ${U}/lib/berry/src ${U}/lib/berry ${U}/lib/berry/default)
add_executable(awtrix-tc002 ${CORE} ${SIM} ${TC002}
  ${U}/src/sim/vendor/tjpgd/tjpgd.c
  ${U}/src/media/GifPlayer.cpp ${U}/src/media/MicroGif.cpp ${U}/src/media/ScriptIcon.cpp
  ${U}/src/persistence/DeviceConfigJson.cpp ${U}/src/persistence/SystemConfigApply.cpp
  ${U}/src/persistence/FsRestoreSink.cpp ${U}/src/system/Log.cpp
  ${U}/src/transport/ScriptMqttBridge.cpp ${U}/src/transport/mqtt/MqttService.cpp
  ${U}/src/transport/mqtt/MqttLink.cpp ${U}/src/transport/mqtt/HaAnnouncer.cpp
  ${U}/src/transport/net/ArtnetService.cpp ${U}/src/transport/net/UdpSocket.cpp
  vendor/PubSubClient/src/PubSubClient.cpp src/updater/FirmwareImage.cpp)
add_executable(tc002-update src/updater/FirmwareUpdate.cpp src/updater/FirmwareImage.cpp src/tc002/VendorLibrary.cpp)
target_include_directories(tc002-update PRIVATE src/tc002 ${TC002_GENERATED})
# src/tc002/compat comes before upstream's sim/compat so the clock's WiFi and socket shims win.
target_include_directories(awtrix-tc002 PRIVATE src/tc002 src/tc002/compat src/updater src/loader ${TC002_GENERATED}
  ${U}/src ${U}/src/sim/compat vendor/base64/src vendor/PubSubClient/src)
target_compile_definitions(awtrix-tc002 PRIVATE AWTRIX_NATIVE AWTRIX_SIM AWTRIX_TC002 ${TC002_PANEL}
  AWTRIX_NG_VERSION="1.1.2-tc002.1" CPPHTTPLIB_THREAD_POOL_COUNT=2 CPPHTTPLIB_OPENSSL_SUPPORT)
target_compile_options(awtrix-tc002 PRIVATE -Wall -Wextra -Wno-unused-parameter
  -ffunction-sections -fdata-sections)
target_link_options(awtrix-tc002 PRIVATE -Wl,--gc-sections)
target_link_libraries(awtrix-tc002 PRIVATE berry Threads::Threads ${CMAKE_DL_LIBS} m)
if(CMAKE_CROSSCOMPILING)
  set(TC002_TLS "$ENV{TC002_TLS}" CACHE PATH "OpenSSL 3.5.8 cross-build directory")
  if(NOT EXISTS "${TC002_TLS}/libssl.a" OR NOT EXISTS "${TC002_TLS}/libcrypto.a")
    message(FATAL_ERROR "Build TLS with tools/build_tls.sh and set TC002_TLS to its build directory")
  endif()
  target_include_directories(awtrix-tc002 PRIVATE "${TC002_TLS}/include")
  target_include_directories(tc002-update PRIVATE "${TC002_TLS}/include")
  target_link_libraries(tc002-update PRIVATE "${TC002_TLS}/libcrypto.a" Threads::Threads ${CMAKE_DL_LIBS})
  set_target_properties(tc002-update PROPERTIES SKIP_BUILD_RPATH TRUE)
  target_link_libraries(awtrix-tc002 PRIVATE "${TC002_TLS}/libssl.a" "${TC002_TLS}/libcrypto.a" Threads::Threads ${CMAKE_DL_LIBS})
  set_target_properties(awtrix-tc002 PROPERTIES SKIP_BUILD_RPATH TRUE)
else()
  find_package(OpenSSL REQUIRED)
  target_link_libraries(awtrix-tc002 PRIVATE OpenSSL::SSL OpenSSL::Crypto)
  target_link_libraries(tc002-update PRIVATE OpenSSL::Crypto)
endif()
if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU" AND CMAKE_CXX_COMPILER_VERSION VERSION_LESS 9)
  message(FATAL_ERROR "Use GCC 9 or newer. The TC002 build is tested with Arm GNU 9.2-2019.12.")
endif()
install(TARGETS awtrix-tc002 RUNTIME DESTINATION bin)
install(FILES ${CMAKE_CURRENT_SOURCE_DIR}/build-webui/index.html DESTINATION share/awtrix-tc002)
if(CMAKE_CROSSCOMPILING)
  add_library(tc002-loader SHARED src/loader/FlyThingsLoader.c)
  target_include_directories(tc002-loader PRIVATE src/loader src/tc002)
  target_link_libraries(tc002-loader PRIVATE ${CMAKE_DL_LIBS})
  set_target_properties(tc002-loader PROPERTIES OUTPUT_NAME zkgui)
endif()
include(CTest)
if(BUILD_TESTING)
  add_executable(test-loader tests/loader_test.c)
  target_include_directories(test-loader PRIVATE src/loader src/tc002)
  add_test(NAME tc002-loader COMMAND test-loader)
  add_executable(test-dns tests/dns_test.cpp src/tc002/Tc002Dns.cpp)
  target_include_directories(test-dns PRIVATE src/tc002 ${U}/src ${U}/src/sim/compat)
  target_compile_definitions(test-dns PRIVATE AWTRIX_NATIVE AWTRIX_SIM AWTRIX_TC002 ${TC002_PANEL})
  target_link_libraries(test-dns PRIVATE ${CMAKE_DL_LIBS})
  add_test(NAME tc002-dns COMMAND test-dns)
  add_executable(test-layout-render tests/layout_render.cpp
    src/tc002/Tc002Layout.cpp
    ${U}/src/core/apps/ClockText.cpp ${U}/src/core/apps/SensorFormat.cpp
    ${U}/src/core/render/Canvas.cpp ${U}/src/core/render/TextRenderer.cpp
    ${U}/src/core/render/TextEncoding.cpp)
  target_include_directories(test-layout-render PRIVATE src/tc002 ${U}/src)
  target_compile_definitions(test-layout-render PRIVATE AWTRIX_TC002 ${TC002_PANEL})
  target_compile_options(test-layout-render PRIVATE -ffunction-sections -fdata-sections)
  target_link_options(test-layout-render PRIVATE -Wl,--gc-sections)
  add_executable(test-tc002 tests/hardware_test.cpp src/tc002/Tc002Hardware.cpp)
  target_include_directories(test-tc002 PRIVATE src/tc002 ${U}/src)
  target_link_libraries(test-tc002 PRIVATE ${CMAKE_DL_LIBS})
  add_test(NAME tc002-hardware COMMAND test-tc002)
endif()
