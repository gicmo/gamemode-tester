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

#ifdef __cplusplus
extern "C" {
#endif

#define STRINGIFY_HELPER(x) #x
#define STRINGIFY(x) STRINGIFY_HELPER(x)

#define GAMEMODE_LIBNAME "libgamemode.so"
#define GAMEMODE_LIBVER 0
#define GAMEMODE_SONAME GAMEMODE_LIBNAME STRINGIFY(GAMEMODE_LIBVER)

#undef STRINGIFY
#undef STRINGIFY_HELPER

typedef const char * (*gamemode_error_string_fn) (void);
typedef int (*gamemode_request_simple_fn) (void);
typedef int (*gamemode_request_forpid_fn) (void);

typedef struct GameModeClient_ {

  int init;
  void *lib;

  gamemode_error_string_fn   error_string;

  gamemode_request_simple_fn request_start;
  gamemode_request_simple_fn request_end;
  gamemode_request_simple_fn query_status;

  gamemode_request_forpid_fn request_start_for;
  gamemode_request_forpid_fn request_end_for;
  gamemode_request_forpid_fn query_status_for;

} GameModeClient;

typedef struct GameModeSymbol_ {
  const char *nick;
  const char *name;
  size_t      offset;
  size_t      size;
  bool        need;
} GameModeSymbol;

#define GMCSYM(x, need) {			\
    STRINGIFY (x),				\
      "real_gamemode_" STRINGIFY (x),		\
      offsetof (GameModeClient, x),		\
      sizeof (((GameModeClient *) 0)->x),	\
      need					\
      }

/* internal error handling */
static char gmc_error_log__[256] = {0};

static inline void log_error (const char *fmt, ... )
{
  va_list args;

  va_start (args, fmt);
  vsnprintf (gmc_error_log__, sizeof (gmc_error_log__), fmt, args);
  va_end (args);
}

  /* utility functions */
static inline int
gmc_internal_load_symbol (GameModeClient *cli,
			  GameModeSymbol *sym)
{
  void *sml;
  char *err;

  /* clear the error */
  err = dlerror ();

  sml = dlsym (cli->lib, sym->name);
  err = dlerror ();

  if (err != NULL)
    {
      log_error ("failed to load symbol '%s': %s", sym->nick, err);
      return sym->need ? -1 : 0;
    }

  memcpy (cli + sym->offset, sym, sym->size);
  return 0;
}

static inline GameModeClient *
gamemode_client_ensure_library (void)
{
  static GameModeClient cli = { 0, NULL, };
  static GameModeSymbol syms[] =
    {
     GMCSYM (error_string, true),
     NULL
    };

  /* not thread safe .. la la la */
  if (cli.init)
    return cli.init > 1 ? &cli: NULL;

  cli.init = 1;
  cli.lib = dlopen (GAMEMODE_SONAME, RTLD_NOW);

  /* fallback to the un-versioned library for now */
  if (cli.lib == NULL)
    cli.lib = dlopen (GAMEMODE_LIBNAME, RTLD_NOW);

  if (cli.lib == NULL)
    return NULL;

  for (GameModeSymbol *sym = syms; sym->name; sym++)
    {
      int r = 0;
      r = gmc_internal_load_symbol (&cli, sym);
      if (r < 0)
	return NULL;
    }

  cli.init = 2;
}

/* public API */
static inline const char *
gamemode_error_string (void)
{
  GameModeClient *cli = gamemode_client_ensure_library ();
  return cli->error_string ();
}

static inline int
gamemode_request_start (void)
{
  GameModeClient *cli = gamemode_client_ensure_library ();
  return cli->request_start ();
}

static inline int
gamemode_request_end (void)
{
  GameModeClient *cli = gamemode_client_ensure_library ();
  return cli->request_end ();
}

static inline int
gamemode_query_status (void)
{
  GameModeClient *cli = gamemode_client_ensure_library ();
  return cli->query_status ();
}

static inline int
gamemode_request_start_for (pid_t pid)
{
  GameModeClient *cli = gamemode_client_ensure_library ();
  return cli->request_start_for (pid);
}

static inline int
gamemode_request_end_for (pid_t pid)
{
  GameModeClient *cli = gamemode_client_ensure_library ();
  return cli->request_end_for (pid);
}

static inline int
gamemode_query_status_for (pid_t pid)
{
  GameModeClient *cli = gamemode_client_ensure_library ();
  return cli->query_status_for (pid);
}

/* cleanup */
#undef log_error

#ifdef __cplusplus
}
#endif
