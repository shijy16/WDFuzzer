#!/usr/bin/perl

# **********************************************************
# Copyright () 2016-2020 Google, Inc.  All rights reserved.
# **********************************************************

# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions are met:
#
# * Redistributions of source code must retain the above copyright notice,
#   this list of conditions and the following disclaimer.
#
# * Redistributions in binary form must reproduce the above copyright notice,
#   this list of conditions and the following disclaimer in the documentation
#   and/or other materials provided with the distribution.
#
# * Neither the name of Google, Inc. nor the names of its contributors may be
#   used to endorse or promote products derived from this software without
#   specific prior written permission.
#
# THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
# AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
# IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
# ARE DISCLAIMED. IN NO EVENT SHALL VMWARE, INC. OR CONTRIBUTORS BE LIABLE
# FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
# DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
# SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
# CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
# LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
# OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH
# DAMAGE.

# Build-and-test driver for Travis CI.
# Travis uses the exit code to check success, so we need a layer outside of
# ctest on runsuite.
# We stick with runsuite rather than creating a parallel scheme using
# a Travis matrix of builds.
# Travis only supports Linux and Mac, so we're ok relying on perl.

# XXX: We currently have a patchwork of scripts and methods of passing arguments
# (some are env vars while others are command-line parameters) and thus have
# too many control points for the details of test builds and package builds.
# Maybe we can clean it up and eliminate this layer of script by moving logic
# in both directions (.{travis,appveyor}.yml and {runsuite,package}.cmake)?

use strict;
use Config;
use Cwd 'abs_path';
use File::Basename;
my $mydir = dirname(abs_path($0));
my $is_CI = 0;
my $is_aarchxx = $Config{archname} =~ /(aarch64)|(arm)/;

# Forward args to runsuite.cmake:
my $args = '';
for (my $i = 0; $i <= $#ARGV; $i++) {
    $is_CI = 1 if ($ARGV[$i] eq 'travis');
    if ($i == 0) {
        $args .= ",$ARGV[$i]";
    } else {
        # We don't use a backslash to escape ; b/c we'll quote below, and
        # the backslash is problematically converted to / by Cygwin perl.
        $args .= ";$ARGV[$i]";
    }
}

my $osdir = $mydir;
if ($^O eq 'cygwin') {
    # CMake is native Windows so pass it a Windows path.
    # We use the full path to cygpath as git's cygpath is earlier on
    # the PATH for AppVeyor and it fails.
    $osdir = `/usr/bin/cygpath -wi \"$mydir\"`;
    chomp $osdir;
}

# We tee to stdout to provide incremental output and avoid the 10-min
# no-output timeout on Travis.
print "Forking child for stdout tee\n";
my $res = '';
my $child = open(CHILD, '-|');
die "Failed to fork: $!" if (!defined($child));
if ($child) {
    # Parent
    # i#4126: We include extra printing to help diagnose hangs on Travis.
    if ($^O ne 'cygwin') {
        print "Parent tee-ing child stdout...\n";
        local $SIG{ALRM} = sub {
            print "\nxxxxxxxxxx 30s elapsed xxxxxxxxxxx\n";
            alarm(30);
        };
        alarm(30);
        while (<CHILD>) {
            print STDOUT $_;
            $res .= $_;
        }
    } else {
        while (<CHILD>) {
            print STDOUT $_;
            $res .= $_;
        }
    }
    close(CHILD);
} elsif ($ENV{'TRAVIS_EVENT_TYPE'} eq 'cron' ||
         $ENV{'APPVEYOR_REPO_TAG'} eq 'true') {
    # A package build.
    my $build = "0";
    # We trigger by setting VERSION_NUMBER in Travis.
    # That sets a tag and we propagate the name into the Appveyor build from the tag:
    if ($ENV{'APPVEYOR_REPO_TAG_NAME'} =~ /release_(.*)/) {
        $ENV{'VERSION_NUMBER'} = $1;
    }
    if ($ENV{'VERSION_NUMBER'} =~ /-(\d+)$/) {
        $build = $1;
    }
    if ($args eq '') {
        $args = ",";
    } else {
        $args .= ";";
    }
    $args .= "build=${build}";
    if ($ENV{'VERSION_NUMBER'} =~ /^(\d+\.\d+\.\d+)/) {
        my $version = $1;
        $args .= ";version=${version}";
    }
    if ($ENV{'DEPLOY_DOCS'} eq 'yes') {
        $args .= ";copy_docs";
    }
    # Include Dr. Memory.
    if (($is_aarchxx || $ENV{'DYNAMORIO_CROSS_AARCHXX_LINUX_ONLY'} eq 'yes') &&
        $args =~ /64_only/) {
        # Dr. Memory is not ported to AArch64 yet.
    } else {
        $args .= ";invoke=${osdir}/../drmemory/package.cmake;drmem_only";
    }
    my $cmd = "ctest -VV -S \"${osdir}/../make/package.cmake${args}\"";
    print "Running ${cmd}\n";
    system("${cmd} 2>&1");
    exit 0;
} else {
    # We have no way to access the log files, so we use -VV to ensure
    # we can diagnose failures.
    my $verbose = "-VV";
    my $cmd = "ctest --output-on-failure ${verbose} -S \"${osdir}/runsuite.cmake${args}\"";
    print "Running ${cmd}\n";
    system("${cmd} 2>&1");
    print "Finished running ${cmd}\n";
    exit 0;
}

my @lines = split('\n', $res);
my $should_print = 0;
my $exit_code = 0;
for (my $i = 0; $i < $#lines; ++$i) {
    my $line = $lines[$i];
    my $fail = 0;
    my $name = '';
    $should_print = 1 if ($line =~ /^RESULTS/);
    if ($line =~ /^([-\w]+):.*\*\*/) {
        $name = $1;
        if ($line =~ /build errors/ ||
            $line =~ /configure errors/ ||
            $line =~ /tests failed:/) {
            $fail = 1;
        } elsif ($line =~ /(\d+) tests failed, of which (\d+)/) {
            $fail = 1 if ($2 < $1);
        }
    } elsif ($line =~ /^\s*ERROR: diff contains/) {
        $fail = 1;
        $should_print = 1;
        $name = "diff pre-commit checks";
    }
    if ($fail && $is_CI && $line =~ /tests failed/) {
        my $is_32 = $line =~ /-32/;
        my $issue_no = "";
        my %ignore_failures_32 = ();
        my %ignore_failures_64 = ();
        if ($^O eq 'cygwin') {
            # FIXME i#2145: ignoring certain AppVeyor test failures until
            # we get all tests passing.
            %ignore_failures_32 = ('code_api|security-common.retnonexisting' => 1,
                                   'code_api|security-win32.gbop-test' => 1, # i#2972
                                   'code_api|win32.reload-newaddr' => 1,
                                   'code_api|win32.rsbtest' => 1, # i#4058
                                   'code_api|client.alloc' => 1, # i#4058
                                   'code_api|client.winxfer' => 1, # i#4058
                                   'code_api|client.drutil-test' => 1, # i#4058
                                   'code_api|tool.drcacheoff.view' => 1, # i#4058
                                   'code_api|tool.drcacheoff.burst_replaceall' => 1, # i#4058
                                   'code_api|client.pcache-use' => 1,
                                   'code_api|api.detach' => 1, # i#2246
                                   'code_api|api.detach_spawn' => 1, # i#2611
                                   'code_api|api.startstop' => 1, # i#2093
                                   'code_api|client.drmgr-test' => 1, # i#653
                                   'code_api|client.nudge_test' => 1, # i#2978
                                   'code_api|client.nudge_ex' => 1);
            %ignore_failures_64 = ('code_api|common.floatpc_xl8all' => 1,
                                   'code_api|common.decode' => 1, # i#4058
                                   'code_api|common.decode-stress' => 1, # i#4058
                                   'code_api|common.nativeexec' => 1, # i#4058
                                   'code_api|win32.mixedmode_late' => 1, # i#4058
                                   'code_api|win32.callback' => 1, # i#4058
                                   'code_api|client.alloc' => 1, # i#4058
                                   'code_api|client.cleancall' => 1, # i#4058
                                   'code_api|client.loader' => 1, # i#4058
                                   'code_api|client.pcache-use' => 1, # i#4058
                                   'code_api|tool.drcachesim.invariants' => 1, # i#4058
                                   'code_api|tool.drcacheoff.view' => 1, # i#4058
                                   'code_api|tool.histogram.offline' => 1, # i#4058
                                   'code_api|win32.reload-newaddr' => 1,
                                   'code_api|client.loader' => 1,
                                   'code_api|client.drmgr-test' => 1, # i#1369
                                   'code_api|client.nudge_test' => 1, # i#2978
                                   'code_api|client.nudge_ex' => 1,
                                   'code_api|api.detach' => 1, # i#2246
                                   'code_api|api.detach_spawn' => 1, # i#2611
                                   'code_api|api.startstop' => 1, # i#2093
                                   'code_api|api.static_noclient' => 1,
                                   'code_api|api.static_noinit' => 1);
            $issue_no = "#2145";
        } elsif ($is_aarchxx) {
            # FIXME i#2416: fix flaky AArch32 tests
            %ignore_failures_32 = ('code_api|tool.histogram.offline' => 1,
                                   'code_api|linux.eintr-noinline' => 1, # i#2894
                                   'code_api|pthreads.ptsig' => 1,
                                   'code_api|linux.sigaction_nosignals' => 1,
                                   'code_api|linux.signal_race' => 1,
                                   'code_api|linux.sigsuspend' => 1, # i#2898
                                   'code_api|client.drmgr-test' => 1, # i#2893
                                   'code_api|tool.drcachesim.delay-simple' => 1, # i#2892
                                   'code_api|tool.drcachesim.invariants' => 1, # i#2892
                                   'code_api|tool.drcacheoff.simple' => 1,
                                   'code_api|tool.histogram.gzip' => 1);
            # FIXME i#2417: fix flaky/regressed AArch64 tests
            %ignore_failures_64 = ('code_api|linux.sigsuspend' => 1,
                                   'code_api|pthreads.pthreads_exit' => 1,
                                   'code_api|tool.drcachesim.invariants' => 1, # i#2892
                                   'code_api|tool.histogram.offline' => 1, # i#3980
                                   'code_api|linux.fib-conflict' => 1,
                                   'code_api|linux.fib-conflict-early' => 1,
                                   'code_api|linux.mangle_asynch' => 1);
            if ($is_32) {
                $issue_no = "#2416";
            } else {
                $issue_no = "#2417";
            }
        } elsif ($^O eq 'darwin') {
            %ignore_failures_32 = ('code_api|common.decode-bad' => 1, # i#3127
                                   'code_api|linux.signal0000' => 1, # i#3127
                                   'code_api|linux.signal0010' => 1, # i#3127
                                   'code_api|linux.signal0100' => 1, # i#3127
                                   'code_api|linux.signal0110' => 1, # i#3127
                                   'code_api|linux.sigaction' => 1, # i#3127
                                   'code_api|security-common.codemod' => 1, # i#3127
                                   'code_api|client.crashmsg' => 1, # i#3127
                                   'code_api|client.exception' => 1, # i#3127
                                   'code_api|client.timer' => 1, # i#3127
                                   'code_api|sample.signal' => 1); # i#3127
        } else {
            # FIXME i#2921: fix flaky ptsig test
            %ignore_failures_32 = ('code_api|pthreads.ptsig' => 1);
            # FIXME i#2941: fix flaky threadfilter test
            %ignore_failures_64 = ('code_api|tool.drcacheoff.burst_threadfilter' => 1);
            $issue_no = "#2941";
        }

        # Read ahead to examine the test failures:
        $fail = 0;
        my $num_ignore = 0;
        for (my $j = $i+1; $j < $#lines; ++$j) {
            my $test;
            if ($lines[$j] =~ /^\t(\S+)\s/) {
                $test = $1;
                if (($is_32 && $ignore_failures_32{$test}) ||
                    (!$is_32 && $ignore_failures_64{$test})) {
                    $lines[$j] = "\t(ignore: i" . $issue_no . ") " . $lines[$j];
                    $num_ignore++;
                } elsif ($test =~ /_FLAKY$/) {
                    # Don't count toward failure.
                } else {
                    $fail = 1;
                }
            } else {
                last if ($lines[$j] =~ /^\S/);
            }
        }
        $line =~ s/: \*/, but ignoring $num_ignore for i$issue_no: */;
    }
    if ($fail) {
        $exit_code++;
        print "\n====> FAILURE in $name <====\n";
    }
    print "$line\n" if ($should_print);
}
if (!$should_print) {
    print "Error: RESULTS line not found\n";
    $exit_code++;
}

exit $exit_code;
