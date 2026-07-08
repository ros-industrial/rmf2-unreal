# Locate Unreal Engine's bundled OpenSSL and expose it to find_package(OpenSSL).
#
# Under the Unreal libc++ toolchains the compiler sysroot excludes the host's
# /usr/include, so paho's SSL support cannot find a system OpenSSL. Point the
# standard FindOpenSSL result variables at Unreal's copy instead. Requires the
# UNREAL_ENGINE_ROOT environment variable (already used by the toolchains).

if(NOT DEFINED ENV{UNREAL_ENGINE_ROOT})
  message(FATAL_ERROR "UnrealOpenSSL: UNREAL_ENGINE_ROOT is not set.")
endif()

# Pick the highest-versioned OpenSSL directory (e.g. .../OpenSSL/1.1.1t).
file(GLOB _ue_openssl_dirs LIST_DIRECTORIES true
  "$ENV{UNREAL_ENGINE_ROOT}/Engine/Source/ThirdParty/OpenSSL/*")
list(FILTER _ue_openssl_dirs EXCLUDE REGEX "\\.[A-Za-z]+$")  # drop .Build.cs/.tps
list(SORT _ue_openssl_dirs)
list(GET _ue_openssl_dirs -1 UE_OPENSSL_ROOT)

# Per-platform include/lib subdirectories, matching Unreal's layout.
if(WIN32)
  set(_ue_ssl_inc "Win64/VS2015")
  set(_ue_ssl_libdir "lib/Win64/VS2015/Release")
  set(_ue_ssl_name "libssl.lib")
  set(_ue_crypto_name "libcrypto.lib")
elseif(ANDROID)
  set(_ue_ssl_inc "Android")
  set(_ue_ssl_libdir "lib/Android/${CMAKE_ANDROID_ARCH_ABI}")
  set(_ue_ssl_name "libssl.a")
  set(_ue_crypto_name "libcrypto.a")
elseif(CMAKE_SYSTEM_NAME STREQUAL "iOS")
  set(_ue_ssl_inc "IOS")
  set(_ue_ssl_libdir "lib/IOS")
  set(_ue_ssl_name "libssl.a")
  set(_ue_crypto_name "libcrypto.a")
elseif(APPLE)
  set(_ue_ssl_inc "Mac")
  set(_ue_ssl_libdir "lib/Mac")
  set(_ue_ssl_name "libssl.a")
  set(_ue_crypto_name "libcrypto.a")
else()  # Linux / Unix
  set(_ue_ssl_inc "Unix")
  set(_ue_ssl_libdir "lib/Unix/${CMAKE_SYSTEM_PROCESSOR}-unknown-linux-gnu")
  set(_ue_ssl_name "libssl.a")
  set(_ue_crypto_name "libcrypto.a")
endif()

set(OPENSSL_INCLUDE_DIR "${UE_OPENSSL_ROOT}/include/${_ue_ssl_inc}"
  CACHE PATH "Unreal-bundled OpenSSL headers" FORCE)
set(OPENSSL_SSL_LIBRARY "${UE_OPENSSL_ROOT}/${_ue_ssl_libdir}/${_ue_ssl_name}"
  CACHE FILEPATH "Unreal-bundled libssl" FORCE)
set(OPENSSL_CRYPTO_LIBRARY "${UE_OPENSSL_ROOT}/${_ue_ssl_libdir}/${_ue_crypto_name}"
  CACHE FILEPATH "Unreal-bundled libcrypto" FORCE)

if(NOT EXISTS "${OPENSSL_INCLUDE_DIR}/openssl/ssl.h"
    OR NOT EXISTS "${OPENSSL_SSL_LIBRARY}")
  message(FATAL_ERROR
    "UnrealOpenSSL: could not locate Unreal's OpenSSL under ${UE_OPENSSL_ROOT} "
    "(looked for ${OPENSSL_INCLUDE_DIR}/openssl/ssl.h and ${OPENSSL_SSL_LIBRARY}).")
endif()
