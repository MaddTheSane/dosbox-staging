/*
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *
 *  Copyright (C) 2020-2023  The DOSBox Staging Team
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with this program; if not, write to the Free Software Foundation, Inc.,
 *  51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
 */

#ifndef DOSBOX_CONFIG_H
#define DOSBOX_CONFIG_H

/* Version and branding tweaks
*/

// Name of project, lower-case without spaces
#define CANONICAL_PROJECT_NAME "dosbox-staging"

// Emulator Semantic Version (MAJOR.MINOR.PATCH), incremented as follows:
//  - MAJOR version when you make incompatible API changes
//  - MINOR version when you add functionality in a backwards compatible manner
//  - PATCH version when you make backwards compatible bug fixes
// Additional labels for pre-release and build metadata are available as extensions to the MAJOR.MINOR.PATCH format.
// Ref: https://semver.org/

#define VERSION "0.81.0-alpha"

/* Strings to be returned by virtual drivers, etc.
*/

// Name of the emulator
#define DOSBOX_NAME "Boxer"
// Development team name
#define DOSBOX_TEAM "The " DOSBOX_NAME " Team"
// Copyright string
#define DOSBOX_COPYRIGHT "(C) " DOSBOX_TEAM

/* Operating System
 */

// Defined if compiling for OS from BSD family
/* #undef BSD */

// Defined if compiling for Linux (non-Android)
/* #undef LINUX */

// Defined if compiling for macOS
#define MACOSX 1

// Defined if compiling for Windows (any option)
#ifndef WIN32
/* #undef WIN32 */
#endif

/* CPU and FPU emulation
 *
 * These defines are mostly relevant to modules src/cpu/ and src/fpu/
 */

// The type of cpu this target has
//--Modified 2019-10-15 by C.W. Betts to enable automatically for x86 and x86_64
#if defined(__i386__)
#define C_TARGETCPU X86
#elif defined(__x86_64__)
#define C_TARGETCPU X86_64
#elif defined(__arm64__)
#define C_TARGETCPU ARMV8LE
#else
#error unknown arch!
#endif

// Define to 1 if target CPU supports unaligned memory access
#define C_UNALIGNED_MEMORY 1

// Define to 1 if the target platform needs per-page dynamic core write or execute (W^X) tagging
#if defined(__arm64__)
#define C_PER_PAGE_W_OR_X 1
#endif

// Define to 1 to use x86/x86_64 dynamic cpu core
// Can not be used together with C_DYNREC
//--Modified 2019-10-15 by C.W. Betts to enable automatically for x86 and x86_64
#if defined(__i386__) || defined(__x86_64__)
    #define C_DYNAMIC_X86 1
#endif
//--End of modifications

// Define to 1 to use recompiling cpu core
// Can not be used together with C_DYNAMIC_X86
//--Modified 2020-07-03 by C.W. Betts to enable automatically for ARM
#if defined(__arm64__)
    #define C_DYNREC 1
    #define PAGESIZE 0x4000
#endif
//--End of modifications

// Define to 1 to enable floating point emulation
#define C_FPU 1

// Define to 1 to use  fpu core implemented in x86 assembler
#if defined(__i386__) || defined(__x86_64__)
    #define C_FPU_X86 1
#else
    #define C_FPU_X86 0
#endif

// TODO Define to 1 to use inlined memory functions in cpu core
#define C_CORE_INLINE 1

/* Emulator features
 *
 * Turn on or off optional emulator features that depend on external libraries.
 * This way it's easier to port or package on a new platform, where these
 * libraries might be missing.
 */

// Define to 1 to enable internal modem emulation (using SDL2_net)
#define C_MODEM 1

// Define to 1 to enable IPX over Internet networking (using SDL2_net)
#define C_IPX 1

// Enable serial port passthrough support
#define C_DIRECTSERIAL 1

// Define to 1 to use opengl display output support
#define C_OPENGL 0

// Define to 1 to enable FluidSynth integration (built-in MIDI synth)
#define C_FLUIDSYNTH 0

// Define to 1 to enable libslirp Ethernet support
#define C_SLIRP 0

// Define to 1 to enable Novell NE 2000 NIC emulation
#define C_NE2000 1

// Define to 1 to enable the Tracy profiling server
#define C_TRACY 0

// Define to 1 to enable internal debugger (using ncurses or pdcurses)
/* #undef C_DEBUG */

// Define to 1 to enable heavy debugging (requires C_DEBUG)
/* #undef C_HEAVY_DEBUG */

// Define to 1 to enable MT-32 emulator
#define C_MT32EMU 0

// Define to 1 to enable mouse mapping support
#define C_MANYMOUSE 1

// ManyMouse optionally supports the X Input 2.0 protocol (regardless of OS). It
// uses the following define to definitively indicate if it should or shouldn't
// use the X Input 2.0 protocol. If this is left undefined, then ManyMouse makes
// an assumption about availability based on OS type.
#define SUPPORT_XINPUT2 0

// Compiler supports Core Audio headers
#define C_COREAUDIO 0

// Compiler supports Core MIDI headers
#define C_COREMIDI 0

// Compiler supports Core Foundation headers
#define C_COREFOUNDATION 1

// Compiler supports Core Services headers
#define C_CORESERVICES 1

// Define to 1 to enable ALSA MIDI support
/* #undef C_ALSA */

/* Compiler features and extensions
 *
 * These are defines for compiler features we can't reliably verify during
 * compilation time.
 */

#define C_HAS_BUILTIN_EXPECT 1

/* Defines for checking availability of standard functions and structs.
 *
 * Sometimes available functions, structs, or struct fields differ slightly
 * between operating systems.
 */

// Defined if function clock_gettime is available
#define HAVE_CLOCK_GETTIME 1

// Defined if function __builtin_available is available
#define HAVE_BUILTIN_AVAILABLE 1

// Defined if function __builtin___clear_cache is available
#define HAVE_BUILTIN_CLEAR_CACHE 1

// Defined if function mprotect is available
#define HAVE_MPROTECT 1

// Defined if function mmap is available
#define HAVE_MMAP 1

// Defined if mmap flag MAPJIT is available
#define HAVE_MAP_JIT 1

// Defined if function pthread_jit_write_protect_np is available
#define HAVE_PTHREAD_WRITE_PROTECT_NP 1

// Defined if function sys_icache_invalidate is available
#define HAVE_SYS_ICACHE_INVALIDATE 1

// Defined if function pthread_setname_np is available
#define HAVE_PTHREAD_SETNAME_NP 1

// Defined if function realpath is available
#define HAVE_REALPATH 1

// Defind if function setpriority is available
#define HAVE_SETPRIORITY 1

// Defind if function strnlen is available
#define HAVE_STRNLEN 1

// field d_type in struct dirent is not defined in POSIX
// Some OSes do not implement it (e.g. Haiku)
#define HAVE_STRUCT_DIRENT_D_TYPE 1



/* Available headers
 *
 * Checks for non-POSIX headers and POSIX headers not supported on Windows.
 */

#define HAVE_NETINET_IN_H 1
#define HAVE_PWD_H 1
#define HAVE_STDLIB_H 1
#define HAVE_STRINGS_H 1
#define HAVE_SYS_SOCKET_H 1
#define HAVE_SYS_TYPES_H 1

/* Hardware-related defines
 */

// Define to 1 when host/target processor uses big endian byte ordering
/* #undef WORDS_BIGENDIAN */

// Non-4K page size (only on selected architectures)
//#define PAGESIZE 16384

/* Windows-related defines
 */

// Prevent <windows.h> from clobbering std::min and std::max
/* #undef NOMINMAX */

// Enables mathematical constants (such as M_PI) in Windows math.h header
// https://docs.microsoft.com/en-us/cpp/c-runtime-library/math-constants
/* #undef _USE_MATH_DEFINES */

// Holds the "--datadir" specified during project setup. This can
// be used as a fallback if the user hasn't populated their
// XDG_DATA_HOME or XDG_DATA_DIRS to include the --datadir.
#define CUSTOM_DATADIR "/usr/local/share"

#endif
