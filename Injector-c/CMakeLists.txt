cmake_minimum_required(VERSION 3.16)
project(injector C CXX)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE RelWithDebInfo)
endif()

# Fixed (config-independent) output location: the asset-copying custom commands below need a
# path to copy INTO that's known without depending on the `injector` target itself (referencing
# `injector` there, e.g. via $<TARGET_FILE_DIR:injector>, would create injector -> copy-target ->
# injector cycle once `injector` also depends on those copy targets to stay up to date).
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")

find_package(Threads REQUIRED)
# The scraper (src/screenscraper.c, src/http.c) talks HTTPS directly to screenscraper.fr via
# libcurl -- never roll your own TLS/HTTP client. Try a real CMake package config first (this is
# how vcpkg ships libcurl on Windows, and is generally the more robust lookup where it's
# available) before falling back to pkg-config, which is what actually finds it on macOS's system
# libcurl (no CMake config shipped there) and on Linux distros whose curl-dev package doesn't ship
# one either. Requiring pkg-config unconditionally would break configuring on a plain Windows
# box, where pkg-config itself usually isn't installed at all; see the same fallback pattern below
# for webkit2gtk, which only runs on the UNIX (non-Apple) branch to begin with.
find_package(CURL QUIET)
if(NOT CURL_FOUND)
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(LIBCURL REQUIRED libcurl)
endif()

# --- vendored single-file/single-header libraries -------------------------------------------
add_library(vendor STATIC
    vendor/cJSON.c
    vendor/sha1.c
    vendor/md5.c
    vendor/crc32.c
)
target_include_directories(vendor PUBLIC vendor)
# stb_image*.h are header-only and compiled with the STB_IMAGE_IMPLEMENTATION macro from exactly
# one .c file (src/vendor_impl.c) rather than built into this static lib, per their own
# single-header-library convention. vendor/webview.h follows the same convention but needs a C++
# compiler for its implementation, so it gets its own translation unit: src/webview_impl.cpp.

# --- core logic (no GUI, no network -- Phase 1/2) --------------------------------------------
# Sources are added one at a time as each module lands (see the project plan) rather than
# globbed, so a half-written file never silently breaks the build for modules that already work.
set(INJECTOR_CORE_SOURCES "")
if(EXISTS "${CMAKE_SOURCE_DIR}/src/sd.c")
    list(APPEND INJECTOR_CORE_SOURCES src/sd.c)
endif()
if(EXISTS "${CMAKE_SOURCE_DIR}/src/ini.c")
    list(APPEND INJECTOR_CORE_SOURCES src/ini.c)
endif()
if(EXISTS "${CMAKE_SOURCE_DIR}/src/copytree.c")
    list(APPEND INJECTOR_CORE_SOURCES src/copytree.c)
endif()
if(EXISTS "${CMAKE_SOURCE_DIR}/src/romhash.c")
    list(APPEND INJECTOR_CORE_SOURCES src/romhash.c)
endif()
if(EXISTS "${CMAKE_SOURCE_DIR}/src/manifest.c")
    list(APPEND INJECTOR_CORE_SOURCES src/manifest.c)
endif()
if(EXISTS "${CMAKE_SOURCE_DIR}/src/appdir.c")
    list(APPEND INJECTOR_CORE_SOURCES src/appdir.c)
endif()
if(EXISTS "${CMAKE_SOURCE_DIR}/src/theme_scan.c")
    list(APPEND INJECTOR_CORE_SOURCES src/theme_scan.c)
endif()
if(EXISTS "${CMAKE_SOURCE_DIR}/src/rom_scan.c")
    list(APPEND INJECTOR_CORE_SOURCES src/rom_scan.c)
endif()
if(EXISTS "${CMAKE_SOURCE_DIR}/src/blocklist.c")
    list(APPEND INJECTOR_CORE_SOURCES src/blocklist.c)
endif()
if(EXISTS "${CMAKE_SOURCE_DIR}/src/http.c")
    list(APPEND INJECTOR_CORE_SOURCES src/http.c)
endif()
if(EXISTS "${CMAKE_SOURCE_DIR}/src/screenscraper.c")
    list(APPEND INJECTOR_CORE_SOURCES src/screenscraper.c)
endif()
if(EXISTS "${CMAKE_SOURCE_DIR}/src/scrape.c")
    list(APPEND INJECTOR_CORE_SOURCES src/scrape.c)
endif()

add_library(injector_core STATIC ${INJECTOR_CORE_SOURCES})
target_include_directories(injector_core PUBLIC src vendor)
target_link_libraries(injector_core PUBLIC vendor Threads::Threads)
if(CURL_FOUND)
    target_link_libraries(injector_core PUBLIC CURL::libcurl)
else()
    target_include_directories(injector_core PUBLIC ${LIBCURL_INCLUDE_DIRS})
    target_link_directories(injector_core PUBLIC ${LIBCURL_LIBRARY_DIRS})
    target_link_libraries(injector_core PUBLIC ${LIBCURL_LIBRARIES})
endif()

# --- main GUI executable ----------------------------------------------------------------------
# Frontend is real HTML/CSS/JS (assets/ui/) rendered by the OS's own web engine through the
# vendored webview library (vendor/webview.h), not raster immediate-mode widgets -- see
# src/main.c and src/ui/bridge.c.
add_executable(injector
    src/main.c
    src/vendor_impl.c
    src/webview_impl.cpp
    src/ui/wizard.c
    src/ui/bridge.c
)
target_include_directories(injector PRIVATE src src/ui vendor)
target_link_libraries(injector PRIVATE injector_core)

if(APPLE)
    find_library(COCOA_FRAMEWORK Cocoa REQUIRED)
    find_library(WEBKIT_FRAMEWORK WebKit REQUIRED)
    target_link_libraries(injector PRIVATE ${COCOA_FRAMEWORK} ${WEBKIT_FRAMEWORK}
                                            "-framework CoreFoundation")
elseif(UNIX)
    pkg_check_modules(WEBKIT2GTK webkit2gtk-4.1)
    if(NOT WEBKIT2GTK_FOUND)
        pkg_check_modules(WEBKIT2GTK REQUIRED webkit2gtk-4.0)
    endif()
    target_include_directories(injector PRIVATE ${WEBKIT2GTK_INCLUDE_DIRS})
    target_link_directories(injector PRIVATE ${WEBKIT2GTK_LIBRARY_DIRS})
    target_link_libraries(injector PRIVATE ${WEBKIT2GTK_LIBRARIES} m)
elseif(WIN32)
    # WEBVIEW_EDGE (vendor/webview.h) needs the Microsoft WebView2 SDK -- its headers
    # (WebView2.h, ...) and WebView2Loader.dll/.lib aren't vendored here (binary NuGet package).
    # Install the "Microsoft.Web.WebView2" NuGet package and point CMAKE_PREFIX_PATH /
    # WEBVIEW2_SDK_DIR at it before configuring on Windows; see webview/webview's own build docs.
    # comdlg32 -- GetOpenFileNameW(), used by platform_pick_file() in src/webview_impl.cpp.
    target_link_libraries(injector PRIVATE ole32 shlwapi version comdlg32)
    if(DEFINED WEBVIEW2_SDK_DIR)
        target_include_directories(injector PRIVATE "${WEBVIEW2_SDK_DIR}/build/native/include")
        target_link_directories(injector PRIVATE "${WEBVIEW2_SDK_DIR}/build/native/x64")
        target_link_libraries(injector PRIVATE WebView2Loader)
    else()
        message(WARNING "WEBVIEW2_SDK_DIR is not set -- the injector target will fail to link on "
                         "Windows until the WebView2 SDK is installed and WEBVIEW2_SDK_DIR points "
                         "at it (see the comment above this in CMakeLists.txt).")
    endif()
endif()

# --- copying static assets next to the built binary --------------------------------------------
# These use a real OUTPUT+DEPENDS custom command (a stamp file) rather than a plain
# add_custom_command(TARGET ... POST_BUILD) copy: a POST_BUILD command is only invoked when the
# target it's attached to actually rebuilds/relinks -- if you only edit assets/ui/*.html without
# touching any .c/.cpp file, `injector` is already up to date, so a plain POST_BUILD copy would
# silently NOT re-run and the binary would keep serving a stale copy of the UI. Depending on the
# actual source files (via file(GLOB_RECURSE ... CONFIGURE_DEPENDS)) makes each copy step its own
# real build edge that reruns whenever the files it copies change, independent of the C/C++ build.
function(injector_copy_assets name src_dir dest_subdir)
    file(GLOB_RECURSE _files CONFIGURE_DEPENDS "${src_dir}/*")
    set(_stamp "${CMAKE_BINARY_DIR}/${name}.stamp")
    add_custom_command(
        OUTPUT "${_stamp}"
        COMMAND ${CMAKE_COMMAND} -E copy_directory "${src_dir}"
                "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/${dest_subdir}"
        COMMAND ${CMAKE_COMMAND} -E touch "${_stamp}"
        DEPENDS ${_files}
        COMMENT "Copying ${src_dir} next to the injector binary"
    )
    add_custom_target(${name} ALL DEPENDS "${_stamp}")
    add_dependencies(injector ${name})
endfunction()

# Dev convenience: a sample theme lives at themes-example/ so the theme-install screen has
# something real to find/copy without needing a full release bundle -- copied next to the built
# binary (app_dir(), see src/appdir.h, looks for bundled themes there, same convention deploy.py
# uses for "immediate subfolders of the script's own folder").
if(EXISTS "${CMAKE_SOURCE_DIR}/themes-example")
    injector_copy_assets(injector_themes_example "${CMAKE_SOURCE_DIR}/themes-example" ".")
endif()

# The HTML/CSS/JS frontend itself -- src/main.c loads "<app_dir()>/ui/index.html" via file://.
injector_copy_assets(injector_ui_assets "${CMAKE_SOURCE_DIR}/assets/ui" "ui")

# system_blocklist.txt -- the scraper (src/scrape.c) looks for it at "<app_dir()>/data/...".
if(EXISTS "${CMAKE_SOURCE_DIR}/data")
    injector_copy_assets(injector_data_assets "${CMAKE_SOURCE_DIR}/data" "data")
endif()

# --- tests (Phase 1/2: plain assert binaries, no GUI/network dependency) ----------------------
enable_testing()
foreach(t hash ini sd romhash manifest copytree appdir theme_scan rom_scan blocklist screenscraper)
    if(EXISTS "${CMAKE_SOURCE_DIR}/tests/test_${t}.c")
        add_executable(test_${t} tests/test_${t}.c)
        target_link_libraries(test_${t} PRIVATE injector_core)
        target_include_directories(test_${t} PRIVATE src vendor)
        add_test(NAME ${t} COMMAND test_${t})
    endif()
endforeach()

# wizard.c has no GUI dependency of its own (see its header comment) but isn't part of
# injector_core -- it's compiled straight into the executable alongside bridge.c/webview normally,
# so this test target builds it a second time itself rather than needing bridge.c/webview.h too.
if(EXISTS "${CMAKE_SOURCE_DIR}/tests/test_wizard_jobs.c")
    add_executable(test_wizard_jobs tests/test_wizard_jobs.c src/ui/wizard.c)
    target_link_libraries(test_wizard_jobs PRIVATE injector_core)
    target_include_directories(test_wizard_jobs PRIVATE src src/ui vendor)
    add_test(NAME wizard_jobs COMMAND test_wizard_jobs)
endif()

if(EXISTS "${CMAKE_SOURCE_DIR}/tests/test_wizard_install.c")
    add_executable(test_wizard_install tests/test_wizard_install.c src/ui/wizard.c)
    target_link_libraries(test_wizard_install PRIVATE injector_core)
    target_include_directories(test_wizard_install PRIVATE src src/ui vendor)
    add_test(NAME wizard_install COMMAND test_wizard_install)
endif()
