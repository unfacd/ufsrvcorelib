/**
 * Copyright (C) 2015-2026 unfacd works
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Affero General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Affero General Public License for more details.
 *
 * You should have received a copy of the GNU Affero General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <ufsrvcorelib/ufsrv_core/system/ufsrv_signals.h>

#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static sigset_t s_control_signals;

static void
IgnoreUserSignal(int signal)
{
  (void)signal;
}

static void
InstallSignalHandler(int signal, void (*handler)(int), const char *error_message)
{
  struct sigaction action = {.sa_handler = handler, .sa_flags = SA_RESTART};
  sigemptyset(&action.sa_mask);
  if (sigaction(signal, &action, NULL) == -1) {
    perror(error_message);
    exit(EXIT_FAILURE);
  }
}

void
InitSignals(void)
{
  InstallSignalHandler(SIGPIPE, SIG_IGN, "Couldn't ignore SIGPIPE");
  InstallSignalHandler(SIGUSR1, IgnoreUserSignal, "Couldn't install SIGUSR1 handler");
  InstallSignalHandler(SIGUSR2, IgnoreUserSignal, "Couldn't install SIGUSR2 handler");

  sigemptyset(&s_control_signals);
  sigaddset(&s_control_signals, SIGHUP);
  sigaddset(&s_control_signals, SIGTERM);
  sigaddset(&s_control_signals, SIGINT);

  int rc = pthread_sigmask(SIG_BLOCK, &s_control_signals, NULL);
  if (rc != 0) {
    fprintf(stderr, "Couldn't block control signals: %s\n", strerror(rc));
    exit(EXIT_FAILURE);
  }
}

const sigset_t *
UfsrvSignalControlSet(void)
{
  return &s_control_signals;
}
