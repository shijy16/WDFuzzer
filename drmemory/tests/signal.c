/* **********************************************************
 * Copyright (c) 2011-2014 Google, Inc.  All rights reserved.
 * Copyright (c) 2009 VMware, Inc.  All rights reserved.
 * **********************************************************/

/* Dr. Memory: the memory debugger
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation;
 * version 2.1 of the License, and no later version.

 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * Library General Public License for more details.

 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

/* **********************************************************
 * Copyright (c) 2011 Google, Inc.  All rights reserved.
 * Copyright (c) 2003 VMware, Inc.  All rights reserved.
 * **********************************************************/

/*
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * * Redistributions of source code must retain the above copyright notice,
 *   this list of conditions and the following disclaimer.
 *
 * * Redistributions in binary form must reproduce the above copyright notice,
 *   this list of conditions and the following disclaimer in the documentation
 *   and/or other materials provided with the distribution.
 *
 * * Neither the name of VMware, Inc. nor the names of its contributors may be
 *   used to endorse or promote products derived from this software without
 *   specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL VMWARE, INC. OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
 * CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH
 * DAMAGE.
 */

/* Modeled after DynamoRIO's signal tests */

#define USE_LONGJMP 0
#define BLOCK_IN_HANDLER 0
#ifdef VMX86_SERVER
/* sigaltstack is NYI */
# define USE_SIGSTACK 0
#else
# define USE_SIGSTACK 1
#endif
#define USE_TIMER 0

#include <assert.h>
#include <stdio.h>
#include <math.h>
#include <unistd.h>
#ifdef MACOS
# define _XOPEN_SOURCE 700 /* required to get POSIX, etc. defines out of ucontext.h */
# define __need_struct_ucontext64 /* seems to be missing from Mac headers */
#endif
#include <signal.h>
#include <ucontext.h>
#include <sys/time.h> /* itimer */
#include <string.h> /*  memset */

/* handler with SA_SIGINFO flag set gets three arguments: */
typedef void (*handler_t)(int, siginfo_t *, void *);

#ifdef USE_DYNAMO
#include "dynamorio.h"
#endif

#ifdef X64
# define SC_XIP rip
#else
# define SC_XIP eip
#endif

#if USE_LONGJMP
#include <setjmp.h>
static jmp_buf env;
#endif

#if USE_SIGSTACK
# include <stdlib.h> /* malloc */
/* need more space if might get nested signals */
# if USE_TIMER
#  define ALT_STACK_SIZE  (SIGSTKSZ*4)
# else
#  define ALT_STACK_SIZE  (SIGSTKSZ*2)
# endif
#endif

#if USE_TIMER
/* need to run long enough to get itimer hit */
# define ITERS 3500000
#else
# define ITERS 5000
#endif

static int a[ITERS];

/* strategy: anything that won't be the same across multiple runs,
 * hide inside #if VERBOSE.
 * timer hits won't be the same, just make sure we get at least one.
 */
#if USE_TIMER
static int timer_hits = 0;
#endif

#include <errno.h>

#define ASSERT_NOERR(rc) do {					\
  if (rc) {							\
     fprintf(stderr, "%s:%d rc=%d errno=%d %s\n",		\
	     __FILE__, __LINE__,				\
	     rc, errno, strerror(errno));  		        \
     _exit(1);							\
  }								\
} while (0);

static void
signal_handler(int sig, siginfo_t *siginfo, ucontext_t *ucxt)
{
#if VERBOSE
    fprintf(stderr, "signal_handler: sig=%d, retaddr=0x%08x, fpregs=0x%08x\n",
	    sig, *(&sig - 1), ucxt->uc_mcontext.fpregs);
#else
# if USE_TIMER
    if (sig != SIGVTALRM)
# endif
	fprintf(stderr, "signal_handler: sig=%d\n", sig);
#endif

    switch (sig) {

    case SIGBUS: /* on Mac we get SIBUS due to __PAGEZERO */
    case SIGSEGV: {
#if VERBOSE
	struct sigcontext *sc = (struct sigcontext *) &(ucxt->uc_mcontext);
	void *pc = (void *) sc->SC_XIP;
#endif
#if USE_LONGJMP && BLOCK_IN_HANDLER
	sigset_t set;
	int rc;
#endif
#if VERBOSE
	fprintf(stderr, "Got SIGSEGV @ 0x%08x\n", pc);
#else
	fprintf(stderr, "Got SIGSEGV\n");
#endif
#if USE_LONGJMP
# if BLOCK_IN_HANDLER
	/* longjmp will bypass sigreturn, and sigreturn is what resets
	 * the set of blocked signals, so we have to unblock them here
	 */
	rc = sigemptyset(&set); /* reset blocked signals */
	ASSERT_NOERR(rc);
	sigprocmask(SIG_SETMASK, &set, NULL);
# endif
	longjmp(env, 1);
#endif
        break;
    }

    case SIGUSR1: {
#if VERBOSE
	struct sigcontext *sc = (struct sigcontext *) &(ucxt->uc_mcontext);
	void *pc = (void *) sc->SC_XIP;
	fprintf(stderr, "Got SIGUSR1 @ 0x%08x\n", pc);
#else
	fprintf(stderr, "Got SIGUSR1\n");
#endif
        break;
    }

#if USE_TIMER
    case SIGVTALRM: {
	struct sigcontext *sc = (struct sigcontext *) &(ucxt->uc_mcontext);
	void *pc = (void *) sc->SC_XIP;
#if VERBOSE
	fprintf(stderr, "Got SIGVTALRM @ 0x%08x\n", pc);
#endif
	timer_hits++;
	break;
    }
#endif

    default:
	assert(0);
    }
}

/* set up signal_handler as the handler for signal "sig" */
static void
intercept_signal(int sig, handler_t handler)
{
    int rc;
    struct sigaction act;

    act.sa_sigaction = handler;
#if BLOCK_IN_HANDLER
    rc = sigfillset(&act.sa_mask); /* block all signals within handler */
#else
    rc = sigemptyset(&act.sa_mask); /* no signals are blocked within handler */
#endif
    ASSERT_NOERR(rc);
    act.sa_flags = SA_SIGINFO | SA_ONSTACK; /* send 3 args to handler */

    /* arm the signal */
    rc = sigaction(sig, &act, NULL);
    ASSERT_NOERR(rc);
}


int main(int argc, char *argv[])
{
    double res = 0.;
    int i, j;
#if defined(USE_DYNAMO) || USE_TIMER || USE_SIGSTACK
    int rc;
#endif
#if USE_SIGSTACK
    stack_t sigstack;
#endif
#if USE_TIMER
    struct itimerval t;
#endif

#ifdef USE_DYNAMO
    rc = dynamorio_app_init();
    ASSERT_NOERR(rc);
    dynamorio_app_start();
#endif

#if USE_TIMER
    intercept_signal(SIGVTALRM, (handler_t) signal_handler);
    t.it_interval.tv_sec = 0;
    t.it_interval.tv_usec = 10000;
    t.it_value.tv_sec = 0;
    t.it_value.tv_usec = 10000;
    rc = setitimer(ITIMER_VIRTUAL, &t, NULL);
    ASSERT_NOERR(rc);
#endif

#if USE_SIGSTACK
    sigstack.ss_sp = (char *) malloc(ALT_STACK_SIZE);
    sigstack.ss_size = ALT_STACK_SIZE;
# ifdef LINUX
    sigstack.ss_flags = SS_ONSTACK;
# else
    sigstack.ss_flags = 0;
# endif
    rc = sigaltstack(&sigstack, NULL);
    ASSERT_NOERR(rc);
# if VERBOSE
    fprintf(stderr, "Set up sigstack: 0x%08x - 0x%08x\n",
	    sigstack.ss_sp, sigstack.ss_sp + sigstack.ss_size);
# endif
#endif

    intercept_signal(SIGSEGV, (handler_t) signal_handler);
    /* on Mac we get SIGBUS due to __PAGEZERO if we write to 0x4 */
    intercept_signal(SIGBUS, (handler_t) signal_handler);
    intercept_signal(SIGUSR1, (handler_t) signal_handler);
    intercept_signal(SIGUSR2, (handler_t) SIG_IGN);

    res = cos(0.56);

    fprintf(stderr, "Sending SIGUSR2\n");
    kill(getpid(), SIGUSR2);

    fprintf(stderr, "Sending SIGUSR1\n");
    kill(getpid(), SIGUSR1);

#if USE_SIGSTACK
    /* now remove alt stack */
    sigstack.ss_flags = SS_DISABLE;
    rc = sigaltstack(&sigstack, NULL);
    ASSERT_NOERR(rc);
    /* Although "man sigaltstack" claims ss_sp and ss_size are ignored when
     * SS_DISABLE is set, on Mac we get ENOMEM if we clear ss_size beforehand.
     */
    free(sigstack.ss_sp);
    sigstack.ss_sp = 0;
    sigstack.ss_size = 0;
#endif

    fprintf(stderr, "Generating SIGSEGV\n");
#if USE_LONGJMP
    res = setjmp(env);
    if (res == 0) {
	*((int *)4) = 4;
    }
#else
    kill(getpid(), SIGSEGV);
#endif

    for (i=0; i<ITERS; i++) {
	if (i % 2 == 0) {
	    res += cos(1./(double)(i+1));
	} else {
	    res += sin(1./(double)(i+1));
	}
	j = (i << 4) / (i | 0x38);
	a[i] += j;
    }
    fprintf(stderr, "%f\n", res);

#if USE_TIMER
    memset(&t, 0, sizeof(t));
    rc = setitimer(ITIMER_VIRTUAL, &t, NULL);
    ASSERT_NOERR(rc);

    if (timer_hits == 0)
	fprintf(stderr, "Got 0 timer hits!\n");
    else
	fprintf(stderr, "Got some timer hits!\n");
#endif

#ifdef USE_DYNAMO
    dynamorio_app_stop();
    dynamorio_app_exit();
#endif
    return 0;
}
