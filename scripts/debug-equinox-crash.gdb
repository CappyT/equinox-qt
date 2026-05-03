# gdb script for the equinox crash repro.
# Launched by scripts/debug-equinox-crash.sh under `gdb -batch -x`.

set pagination off
set print thread-events on
set print address on
set demangle-style auto
set auto-solib-add on
set print pretty on

# Mirror everything to a file for offline analysis.
set logging file /tmp/equinox-debug/gdb.log
set logging redirect on
set logging overwrite on
set logging enabled on

# Don't choke on SIGPIPE (mlc + sockets), but stop on SIGABRT/SIGSEGV.
handle SIGPIPE nostop noprint pass
handle SIGSEGV stop print pass
handle SIGABRT stop print pass

# glibc invokes __libc_message when it detects heap corruption (e.g. invalid
# free, double free) before calling abort(). Catching it gives us the exact
# stack frame WHERE the bad pointer is being freed, in addition to the
# eventual abort() backtrace.
break __libc_message
commands 1
  silent
  printf "\n\n=================================================================\n"
  printf "========== __libc_message hit (glibc heap corruption) ==========\n"
  printf "=================================================================\n\n"
  printf "[arg1 / message string]:\n"
  x/s $rsi
  printf "\n[backtrace -- current thread]:\n"
  bt full 60
  printf "\n[info threads]:\n"
  info threads
  printf "\n[backtrace -- all threads (top 30)]:\n"
  thread apply all bt 30
  printf "\n[info sharedlibrary]:\n"
  info sharedlibrary
  printf "\n[/proc/self/maps -- executable segments only]:\n"
  shell awk '$2 ~ /x/ {print $1, $6}' /proc/$(pgrep -f equinox-qt/app/moonlight | head -1)/maps 2>/dev/null
  printf "\n=== continuing to abort() to capture the eventual signal ===\n"
  continue
end

# Catch the eventual abort() too, so we have both stacks even if libc_message
# was missed (e.g. different glibc version or compiled out).
break abort
commands 2
  silent
  printf "\n\n=================================================================\n"
  printf "========== abort() reached ==========\n"
  printf "=================================================================\n\n"
  printf "[backtrace -- current thread]:\n"
  bt full 60
  printf "\n[backtrace -- all threads]:\n"
  thread apply all bt 50
  printf "\n=== detaching and exiting gdb ===\n"
  detach
  quit
end

# Some heap corruption paths go through __assert_fail; catch that too.
break __assert_fail
commands 3
  silent
  printf "\n========== __assert_fail hit ==========\n"
  bt full 30
  continue
end

run
printf "\n=== run returned (exit or natural termination) ===\n"
quit
