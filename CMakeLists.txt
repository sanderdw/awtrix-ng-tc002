cmake_minimum_required(VERSION 3.20)
project(awtrix_tc002 VERSION 1.1.0 LANGUAGES C CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_C_STANDARD 99)
find_package(Threads REQUIRED)
set(U ${CMAKE_CURRENT_SOURCE_DIR}/upstream/awtrix-ng)
file(GLOB_RECURSE CORE CONFIGURE_DEPENDS ${U}/src/core/*.cpp ${U}/src/core/*.c)
file(GLOB SIM CONFIGURE_DEPENDS ${U}/src/sim/*.cpp)
file(GLOB BERRY CONFIGURE_DEPENDS ${U}/lib/berry/src/*.c ${U}/lib/berry/default/*.c)
add_library(berry STATIC ${BERRY})
target_include_directories(berry PUBLIC ${U}/lib/berry/src ${U}/lib/berry ${U}/lib/berry/default)
add_executable(awtrix-tc002 ${CORE} ${SIM}
  ${U}/src/sim/vendor/tjpgd/tjpgd.c
  ${U}/src/media/GifPlayer.cpp ${U}/src/media/MicroGif.cpp ${U}/src/media/ScriptIcon.cpp
  ${U}/src/persistence/DeviceConfigJson.cpp ${U}/src/persistence/SystemConfigApply.cpp
  ${U}/src/persistence/FsRestoreSink.cpp ${U}/src/system/Log.cpp
  ${U}/src/transport/ScriptMqttBridge.cpp ${U}/src/transport/mqtt/MqttService.cpp
  ${U}/src/transport/mqtt/MqttLink.cpp ${U}/src/transport/mqtt/HaAnnouncer.cpp
  ${U}/src/transport/DeviceStateJson.cpp
  ${U}/src/transport/net/ArtnetService.cpp ${U}/src/transport/net/UdpSocket.cpp
  vendor/PubSubClient/src/PubSubClient.cpp
  src/Tc002Hardware.cpp src/Tc002Audio.cpp src/Tc002System.cpp src/Tc002Dns.cpp src/Tc002Mdns.cpp src/FirmwareImage.cpp)
add_executable(tc002-update src/FirmwareUpdate.cpp src/FirmwareImage.cpp)
target_include_directories(awtrix-tc002 PRIVATE src ${U}/src ${U}/src/sim/compat
  vendor/base64/src vendor/PubSubClient/src)
target_compile_definitions(awtrix-tc002 PRIVATE AWTRIX_NATIVE AWTRIX_SIM AWTRIX_TC002
  AWTRIX_NG_VERSION="1.1.0-tc002.8" CPPHTTPLIB_THREAD_POOL_COUNT=2 CPPHTTPLIB_OPENSSL_SUPPORT)
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
install(FILES ${U}/webui/index.html DESTINATION share/awtrix-tc002)
if(CMAKE_CROSSCOMPILING)
  add_library(tc002-loader SHARED src/FlyThingsLoader.c)
  target_link_libraries(tc002-loader PRIVATE ${CMAKE_DL_LIBS})
  set_target_properties(tc002-loader PROPERTIES OUTPUT_NAME zkgui)
endif()
include(CTest)
if(BUILD_TESTING)
  add_executable(test-loader tests/loader_test.c)
  target_include_directories(test-loader PRIVATE src)
  add_test(NAME tc002-loader COMMAND test-loader)
  add_executable(test-dns tests/dns_test.cpp src/Tc002Dns.cpp)
  target_include_directories(test-dns PRIVATE src ${U}/src ${U}/src/sim/compat)
  target_compile_definitions(test-dns PRIVATE AWTRIX_NATIVE AWTRIX_SIM AWTRIX_TC002)
  target_link_libraries(test-dns PRIVATE ${CMAKE_DL_LIBS})
  add_test(NAME tc002-dns COMMAND test-dns)
  add_executable(test-layout-render tests/layout_render.cpp
    ${U}/src/core/apps/builtin/Tc002Layout.cpp
    ${U}/src/core/apps/ClockText.cpp ${U}/src/core/apps/SensorFormat.cpp
    ${U}/src/core/render/Canvas.cpp ${U}/src/core/render/TextRenderer.cpp
    ${U}/src/core/render/TextEncoding.cpp)
  target_include_directories(test-layout-render PRIVATE ${U}/src)
  target_compile_definitions(test-layout-render PRIVATE AWTRIX_TC002)
  target_compile_options(test-layout-render PRIVATE -ffunction-sections -fdata-sections)
  target_link_options(test-layout-render PRIVATE -Wl,--gc-sections)
  add_executable(test-tc002 tests/hardware_test.cpp src/Tc002Hardware.cpp)
  target_include_directories(test-tc002 PRIVATE src ${U}/src)
  target_link_libraries(test-tc002 PRIVATE ${CMAKE_DL_LIBS})
  add_test(NAME tc002-hardware COMMAND test-tc002)
endif()
