# Stremio Origin for Android TV: the same C app as on webOS (src/), with src/platform/android, built by the NDK.
#   libSDL2.so  SDL 2.30 (window, GL ES 2 context, keys, audio), from androidtv/deps/SDL
#   libmain.so  the app: src/core, src/ui, src/screens, src/main.c, the third-party code and curl + mbedTLS inside
# Mirrors tools/build.sh (the same files, the same flags); the Java side is in androidtv/app/src.
cmake_minimum_required(VERSION 3.22)
project(stremioorigin C CXX)

get_filename_component(ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../../.." ABSOLUTE)
set(DEPS "${ROOT}/androidtv/deps")
set(TP "${ROOT}/third_party")
set(GEN "${CMAKE_CURRENT_BINARY_DIR}/gen")
if(NOT APPID)
  set(APPID "tv.stremioorigin.app")
endif()
set(OPT -O2 -fno-strict-aliasing -ffunction-sections -fdata-sections)

# ---- SDL2 (shared: SDL's Java loads it by name) -------------------------------------------------
set(SDL_SHARED ON CACHE BOOL "" FORCE)
set(SDL_STATIC OFF CACHE BOOL "" FORCE)
set(SDL_TEST OFF CACHE BOOL "" FORCE)
add_subdirectory("${DEPS}/SDL" sdl EXCLUDE_FROM_ALL)
# the app includes <SDL2/SDL.h>: a folder named SDL2 that points at SDL's headers
file(MAKE_DIRECTORY "${GEN}")
if(NOT EXISTS "${GEN}/SDL2")
  file(CREATE_LINK "${DEPS}/SDL/include" "${GEN}/SDL2" SYMBOLIC)
endif()

# ---- mbedTLS + curl (static, inside libmain.so) ----------------------------------------------------
set(ENABLE_PROGRAMS OFF CACHE BOOL "" FORCE)
set(ENABLE_TESTING OFF CACHE BOOL "" FORCE)
set(USE_SHARED_MBEDTLS_LIBRARY OFF CACHE BOOL "" FORCE)
set(USE_STATIC_MBEDTLS_LIBRARY ON CACHE BOOL "" FORCE)
add_subdirectory("${DEPS}/mbedtls" mbedtls EXCLUDE_FROM_ALL)
set(MBEDTLS_INCLUDE_DIR "${DEPS}/mbedtls/include" CACHE PATH "" FORCE)
# curl exports its targets and may not depend on mbedTLS's: it gets the archives' paths, and an order (below)
set(MBEDTLS_LIBRARY "${CMAKE_CURRENT_BINARY_DIR}/mbedtls/library/libmbedtls.a" CACHE FILEPATH "" FORCE)
set(MBEDX509_LIBRARY "${CMAKE_CURRENT_BINARY_DIR}/mbedtls/library/libmbedx509.a" CACHE FILEPATH "" FORCE)
set(MBEDCRYPTO_LIBRARY "${CMAKE_CURRENT_BINARY_DIR}/mbedtls/library/libmbedcrypto.a" CACHE FILEPATH "" FORCE)

set(BUILD_CURL_EXE OFF CACHE BOOL "" FORCE)
set(CURL_DISABLE_INSTALL ON CACHE BOOL "" FORCE)
set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
set(BUILD_STATIC_LIBS ON CACHE BOOL "" FORCE)
set(BUILD_LIBCURL_DOCS OFF CACHE BOOL "" FORCE)
set(BUILD_MISC_DOCS OFF CACHE BOOL "" FORCE)
set(ENABLE_CURL_MANUAL OFF CACHE BOOL "" FORCE)
set(BUILD_TESTING OFF CACHE BOOL "" FORCE)
set(BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(CURL_USE_MBEDTLS ON CACHE BOOL "" FORCE)
set(CURL_USE_OPENSSL OFF CACHE BOOL "" FORCE)
set(CURL_USE_LIBPSL OFF CACHE BOOL "" FORCE)
set(CURL_USE_LIBSSH2 OFF CACHE BOOL "" FORCE)
set(CURL_ZLIB ON CACHE STRING "" FORCE)
set(CURL_BROTLI OFF CACHE STRING "" FORCE)
set(CURL_ZSTD OFF CACHE STRING "" FORCE)
set(USE_NGHTTP2 OFF CACHE BOOL "" FORCE)
set(USE_LIBIDN2 OFF CACHE BOOL "" FORCE)
set(CURL_DISABLE_LDAP ON CACHE BOOL "" FORCE)
set(CURL_ENABLE_SSL ON CACHE BOOL "" FORCE)
set(ENABLE_UNIX_SOCKETS OFF CACHE BOOL "" FORCE)
set(CURL_CA_BUNDLE "none" CACHE STRING "" FORCE)
set(CURL_CA_PATH "none" CACHE STRING "" FORCE)
add_subdirectory("${DEPS}/curl" curl EXCLUDE_FROM_ALL)
add_dependencies(libcurl_static mbedtls mbedx509 mbedcrypto)

# ---- the third-party code tools/build.sh puts in libtp.a / libass.a -------------------------------
file(GLOB FT_SRC
  ${TP}/freetype/src/base/ftbase.c ${TP}/freetype/src/base/ftinit.c ${TP}/freetype/src/base/ftsystem.c ${TP}/freetype/src/base/ftdebug.c
  ${TP}/freetype/src/base/ftbitmap.c ${TP}/freetype/src/base/ftglyph.c ${TP}/freetype/src/base/ftmm.c ${TP}/freetype/src/base/ftbbox.c
  ${TP}/freetype/src/base/ftsynth.c ${TP}/freetype/src/base/ftstroke.c ${TP}/freetype/src/base/fttype1.c ${TP}/freetype/src/gzip/ftgzip.c
  ${TP}/freetype/src/truetype/truetype.c ${TP}/freetype/src/sfnt/sfnt.c ${TP}/freetype/src/smooth/smooth.c ${TP}/freetype/src/autofit/autofit.c
  ${TP}/freetype/src/psnames/psnames.c ${TP}/freetype/src/pshinter/pshinter.c ${TP}/freetype/src/cff/cff.c ${TP}/freetype/src/psaux/psaux.c)
add_library(tp_ft STATIC ${FT_SRC})
target_compile_options(tp_ft PRIVATE -w ${OPT})
target_compile_definitions(tp_ft PRIVATE FT2_BUILD_LIBRARY "FT_CONFIG_MODULES_H=<ftmodule-sb.h>")
target_include_directories(tp_ft PRIVATE ${TP}/config ${TP}/freetype/include)

add_library(tp_hb STATIC ${TP}/harfbuzz/src/harfbuzz.cc)
target_compile_options(tp_hb PRIVATE -w ${OPT} -std=c++11 -fno-exceptions -fno-rtti)
target_compile_definitions(tp_hb PRIVATE HAVE_FREETYPE=1 HAVE_PTHREAD=1)
target_include_directories(tp_hb PRIVATE ${TP}/freetype/include ${TP}/harfbuzz/src)

file(GLOB FB_SRC ${TP}/fribidi/lib/fribidi*.c)
add_library(tp_fb STATIC ${FB_SRC})
target_compile_options(tp_fb PRIVATE -w ${OPT})
target_compile_definitions(tp_fb PRIVATE HAVE_CONFIG_H)
target_include_directories(tp_fb PRIVATE ${TP}/config/fribidi ${TP}/fribidi/lib)

file(GLOB WEBP_ALL ${TP}/webp/src/dec/*.c ${TP}/webp/src/dsp/*.c ${TP}/webp/src/utils/*.c)
set(WEBP_SRC "")
set(WEBP_SKIP "enc|writer|quant_levels_utils|_sse|_msa|_mips")
if(ANDROID_ABI MATCHES "x86")   # the emulator build: libwebp calls its SSE paths there
  set(WEBP_SKIP "enc|writer|quant_levels_utils|_msa|_mips")
endif()
foreach(f ${WEBP_ALL})   # the decoder only, as tools/build.sh
  get_filename_component(n "${f}" NAME)
  if(NOT n MATCHES "${WEBP_SKIP}")
    list(APPEND WEBP_SRC "${f}")
  endif()
endforeach()
add_library(tp_misc STATIC ${TP}/cjson/cJSON.c ${TP}/qrcodegen/qrcodegen.c ${WEBP_SRC})
target_compile_options(tp_misc PRIVATE -w ${OPT})
target_compile_definitions(tp_misc PRIVATE WEBP_REDUCE_CSP)
target_include_directories(tp_misc PRIVATE ${TP}/webp)

file(GLOB ASS_SRC ${TP}/libass/libass/*.c ${TP}/libass/libass/c/*.c)
add_library(tp_ass STATIC ${ASS_SRC})
target_compile_options(tp_ass PRIVATE -w ${OPT} -std=gnu11)
target_compile_definitions(tp_ass PRIVATE HAVE_CONFIG_H)
target_include_directories(tp_ass PRIVATE ${TP}/config/libass ${TP}/libass/libass ${TP}/freetype/include ${TP}/harfbuzz/src ${TP}/fribidi/lib ${TP}/config/fribidi)

file(GLOB CRYPTO_SRC ${TP}/crypto/*.c)
add_library(tp_crypto STATIC ${CRYPTO_SRC})
target_compile_options(tp_crypto PRIVATE -w ${OPT})
target_include_directories(tp_crypto PRIVATE ${TP}/crypto)

# ---- the built-in keys (tools/build.sh's sbkeys.h: empty unless KEYS names the private file) ---------
set(KEYS_FILE "$ENV{KEYS}")
execute_process(COMMAND python3 "${ROOT}/tools/android-keys.py" "${KEYS_FILE}" "${GEN}/sbkeys.h" RESULT_VARIABLE keys_rc)
if(NOT keys_rc EQUAL 0)
  message(FATAL_ERROR "android-keys.py failed")
endif()

# ---- the app ---------------------------------------------------------------------------------------
file(GLOB APP_SRC CONFIGURE_DEPENDS ${ROOT}/src/core/*.c ${ROOT}/src/ui/*.c ${ROOT}/src/screens/*.c ${ROOT}/src/platform/android/*.c ${ROOT}/src/main.c)
add_library(main SHARED ${APP_SRC})
target_compile_options(main PRIVATE ${OPT} -std=c11 -Wall -Wextra -Wno-unused-parameter -Wno-missing-field-initializers)
target_compile_definitions(main PRIVATE _GNU_SOURCE SB_ANDROID=1 "SB_APP_ID=\"${APPID}\"" _FILE_OFFSET_BITS=64)
target_include_directories(main PRIVATE
  ${GEN} ${ROOT}/src ${TP}/freetype/include ${TP}/harfbuzz/src ${TP}/fribidi/lib ${TP}/config/fribidi ${TP}/cjson ${TP}/stb
  ${TP}/qrcodegen ${TP}/webp ${TP}/crypto ${TP}/libass ${DEPS}/curl/include)
target_link_libraries(main PRIVATE SDL2 tp_ass tp_hb tp_ft tp_fb tp_misc tp_crypto libcurl_static GLESv2 EGL android log z dl m)
target_link_options(main PRIVATE -Wl,--gc-sections)
