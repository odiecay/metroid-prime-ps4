# Build the modern C++ runtime against Orbis's C ABI. The SDK's C wchar_t is
# unsigned short; the compiler's FreeBSD default is 32-bit, so make the ABI
# explicit in both languages before compiling a library that exposes wstring.
include("${CMAKE_CURRENT_LIST_DIR}/toolchain.cmake")
string(PREPEND CMAKE_CXX_FLAGS_INIT "-isystem ${CMAKE_CURRENT_LIST_DIR}/compat ")
# When building libc++ itself, an older libc++ directory in include_next's
# chain shadows the C headers (both versions share wrapper include guards).
string(REPLACE "-isystem ${OO_PS4_TOOLCHAIN}/include/c++/v1" "" CMAKE_CXX_FLAGS_INIT "${CMAKE_CXX_FLAGS_INIT}")
string(APPEND CMAKE_CXX_FLAGS_INIT " -nostdinc++")
string(APPEND CMAKE_C_FLAGS_INIT " -fshort-wchar -D_GNU_SOURCE=1")
string(APPEND CMAKE_CXX_FLAGS_INIT " -fshort-wchar -D_GNU_SOURCE=1")
