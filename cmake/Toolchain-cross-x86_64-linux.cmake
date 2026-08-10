#-------------------------------------------------------------------------
# Toolchain Cross x86_64 Linux (depends on the gcc-x86-64-linux-gnu package)
#-------------------------------------------------------------------------

# the name of the target operating system
set(CMAKE_VERBOSE_MAKEFILE ON)
set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSTEM_VERSION 1)
set(CMAKE_SYSTEM_PROCESSOR x86_64)
set(CMAKE_SIZEOF_VOID_P 8)

SET(COMPILER_PREFIX x86_64-linux-gnu)
SET (CMAKE_C_COMPILER ${COMPILER_PREFIX}-gcc CACHE STRING "Set the cross-compiler tool gcc" FORCE)
SET (CMAKE_CXX_COMPILER ${COMPILER_PREFIX}-g++ CACHE STRING "Set the cross-compiler tool g++" FORCE)
SET (CMAKE_LINKER ${COMPILER_PREFIX}-ld CACHE STRING "Set the cross-compiler tool LD" FORCE)
SET (CMAKE_AR ${COMPILER_PREFIX}-ar CACHE STRING "Set the cross-compiler tool AR" FORCE)
SET (CMAKE_NM ${COMPILER_PREFIX}-nm CACHE STRING "Set the cross-compiler tool NM" FORCE)
SET (CMAKE_OBJCOPY ${COMPILER_PREFIX}-objcopy CACHE STRING "Set the cross-compiler tool OBJCOPY" FORCE)
SET (CMAKE_OBJDUMP ${COMPILER_PREFIX}-objdump CACHE STRING "Set the cross-compiler tool OBJDUMP" FORCE)
SET (CMAKE_RANLIB ${COMPILER_PREFIX}-ranlib CACHE STRING "Set the cross-compiler tool RANLIB" FORCE)
SET (CMAKE_STRIP ${COMPILER_PREFIX}-strip CACHE STRING "Set the cross-compiler tool STRIP" FORCE)

# adjust the default behaviour of the FIND_XXX() commands:
# search headers and libraries in the target environment, search
# programs in the host environment
if(NOT CMAKE_FIND_ROOT_PATH_MODE_PROGRAM)
	set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
endif()
if(NOT CMAKE_FIND_ROOT_PATH_MODE_LIBRARY)
	set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
endif()
if(NOT CMAKE_FIND_ROOT_PATH_MODE_INCLUDE)
	set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
endif()
if(NOT CMAKE_FIND_ROOT_PATH_MODE_PACKAGE)
	set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)
endif()

set(CMAKE_LIBRARY_ARCHITECTURE x86_64-linux-gnu)
