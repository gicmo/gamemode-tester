/* Copyright (c) 2019, Christian Kellner All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 *
 * modification, are permitted provided that the following conditions are met:
 *
 * * Redistributions of source code must retain the above copyright notice,
 *   this list of conditions and the following disclaimer.
 * * Redistributions in binary form must reproduce the above copyright
 *   notice, this list of conditions and the following disclaimer in the
 *   documentation and/or other materials provided with the distribution.
 * * Neither the name of Feral Interactive nor the names of its contributors
 *   may be used to endorse or promote products derived from this software
 *   without specific prior written permission.

 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * RISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#pragma once

#include <dlfcn.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <sys/types.h>

#ifdef __cplusplus
extern "C" {
#endif

#define STRINGIFY_HELPER(x) #x
#define STRINGIFY(x) STRINGIFY_HELPER (x)

#define GAMEMODE_LIBNAME "libgamemode.so"
#define GAMEMODE_LIBVER 0
#define GAMEMODE_SONAME GAMEMODE_LIBNAME STRINGIFY (GAMEMODE_LIBVER)

typedef const char * (*gamemode_error_string_fn) (void);
typedef int (*gamemode_request_simple_fn) (void);
typedef int (*gamemode_request_forpid_fn) (pid_t);

typedef struct GameModeLib_
{
  bool                       initialized;

  void                      *lib;

  gamemode_error_string_fn   error_string;

  gamemode_request_simple_fn request_start;
  gamemode_request_simple_fn request_end;
  gamemode_request_simple_fn query_status;

  gamemode_request_forpid_fn request_start_for;
  gamemode_request_forpid_fn request_end_for;
  gamemode_request_forpid_fn query_status_for;

} GameModeLib;

typedef struct GameModeSym_
{
  const char *nick;
  const char *name;
  size_t      offset;
  size_t      size;
  bool        need;
} GameModeSym;

#define GMC_DESCRIBE_SYM(x, need) {           \
    STRINGIFY (x),                            \
    "real_gamemode_" STRINGIFY (x),           \
    offsetof (GameModeLib, x),                \
    sizeof (((GameModeLib *) 0)->x),          \
    need                                      \
}

/* internal error handling */
static char gmc_error_log__[256] = {0};
#define error_log gmc_error_log__

static inline void
gmc_internal_log_error (const char *fmt, ... )
{
  va_list args;

  va_start (args, fmt);
  vsnprintf (error_log, sizeof (error_log), fmt, args);
  va_end (args);
}
#define log_error gmc_internal_log_error

/* stubs */
static inline const char *
gmc_internal_error_string (void)
{
  return gmc_error_log__;
}

static inline int
gmc_internal_simple_stub (void)
{
  return -1;
}

static inline int
gmc_internal_forpid_stub (int dummy)
{
  return -1;
}

/* utility functions */
static inline int
gmc_internal_load_symbol (GameModeLib *cli,
                          GameModeSym *sym)
{
  void *sml;
  char *err;

  err = dlerror (); /* clear the error, see man dlsym(3) */

  sml = dlsym (cli->lib, sym->name);
  err = dlerror ();

  if (sym->need && (err != NULL || sym == NULL))
    {
      log_error ("failed to load symbol '%s': %s", sym->nick, err);
      return -1;
    }

  memcpy (((char *) cli) + sym->offset, &sml, sym->size);
  return 0;
}

static GameModeLib gmc_handle__ = {
  false,
  NULL,
  gmc_internal_error_string,

  gmc_internal_simple_stub,
  gmc_internal_simple_stub,
  gmc_internal_simple_stub,

  gmc_internal_forpid_stub,
  gmc_internal_forpid_stub,
  gmc_internal_forpid_stub,
};

static inline void
gmc_internal_lib_init (void)
{
  static GameModeSym syms[] =
  {
    GMC_DESCRIBE_SYM (request_start, true),
    GMC_DESCRIBE_SYM (request_end,   true),
    GMC_DESCRIBE_SYM (query_status,  true),

    GMC_DESCRIBE_SYM (request_start_for, false),
    GMC_DESCRIBE_SYM (request_end_for,   false),
    GMC_DESCRIBE_SYM (query_status_for,  false),

    /* error_string is done last so that only if everything
     * is ok, we replace the function pointer to our internal
     * error reporting function with the one from the library
     */
    GMC_DESCRIBE_SYM (error_string, true),

    {NULL, }
  };
  GameModeLib *cli = &gmc_handle__;

  cli->lib = dlopen (GAMEMODE_SONAME, RTLD_NOW);

  /* fallback to the un-versioned library for now */
  if (cli->lib == NULL)
    cli->lib = dlopen (GAMEMODE_LIBNAME, RTLD_NOW);

  if (cli->lib == NULL)
    return;

  for (GameModeSym *sym = syms; sym->name; sym++)
    {
      int r = 0;
      r = gmc_internal_load_symbol (cli, sym);
      if (r < 0)
        break;
    }
}

static inline GameModeLib *
gmc_internal_lib_get (void)
{
  /* This is not at all thread-safe, but then again,
   * calling gamemode functions from different threads
   * concurrently does not make much sense to begin with
   */
  if (!gmc_handle__.initialized)
    gmc_internal_lib_init ();

  return &gmc_handle__;
}

/* public API */
static inline const char *
gamemode_error_string (void)
{
  GameModeLib *lib = gmc_internal_lib_get ();

  return lib->error_string ();
}

static inline int
gamemode_request_start (void)
{
  GameModeLib *lib = gmc_internal_lib_get ();

  return lib->request_start ();
}

static inline int
gamemode_request_end (void)
{
  GameModeLib *lib = gmc_internal_lib_get ();

  return lib->request_end ();
}

static inline int
gamemode_query_status (void)
{
  GameModeLib *lib = gmc_internal_lib_get ();

  return lib->query_status ();
}

static inline int
gamemode_request_start_for (pid_t pid)
{
  GameModeLib *lib = gmc_internal_lib_get ();

  return lib->request_start_for (pid);
}

static inline int
gamemode_request_end_for (pid_t pid)
{
  GameModeLib *lib = gmc_internal_lib_get ();

  return lib->request_end_for (pid);
}

static inline int
gamemode_query_status_for (pid_t pid)
{
  GameModeLib *lib = gmc_internal_lib_get ();

  return lib->query_status_for (pid);
}

/* cleanup */
#undef STRINGIFY
#undef STRINGIFY_HELPER
#undef GMC_DESCRIBE_SYM

#undef error_log
#undef gmc_internal_log_error
#ifdef __cplusplus
}
#endif
