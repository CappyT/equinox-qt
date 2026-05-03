# gdb script for the equinox crash repro.
# Launched by scripts/debug-equinox-crash.sh under `gdb -batch -x`.

set pagination off
set print thread-events on
set print address on
set print pretty on
set demangle-style auto
set auto-solib-add on
set breakpoint pending on

# Mirror everything to a file for offline analysis.
set logging file /tmp/equinox-debug/gdb.log
set logging redirect on
set logging overwrite on
set logging enabled on

# Don't choke on SIGPIPE (mlc + sockets), stop on SIGABRT/SIGSEGV but
# DO NOT pass them to the inferior so the process stays alive long
# enough for us to inspect it.
handle SIGPIPE nostop noprint pass
handle SIGABRT stop print nopass
handle SIGSEGV stop print nopass

# glibc prints messages like 'free(): invalid pointer' from malloc_printerr
# right before raising SIGABRT. Catching it gives us the EXACT stack frame
# where the bad free is happening.
break malloc_printerr
commands
  printf "\n\n=================================================================\n"
  printf "========== malloc_printerr (glibc heap corruption) =============\n"
  printf "=================================================================\n\n"
  printf "[message]:\n"
  x/s $rdi
  printf "\n[backtrace -- current thread]:\n"
  bt full 60
  printf "\n[info threads]:\n"
  info threads
  printf "\n[backtrace -- all threads (top 30 frames each)]:\n"
  thread apply all bt 30
  printf "\n[info sharedlibrary -- pay attention to libmoonlight-common-c]:\n"
  info sharedlibrary
  printf "\n[/proc/<pid>/maps -- text segments only]:\n"
  shell awk '$2 ~ /x/ {print $1, $6}' /proc/$(pgrep -f equinox-qt/app/moonlight | head -1)/maps 2>/dev/null | head -60
  printf "\n=== continuing past malloc_printerr to let SIGABRT raise ===\n"
  continue
end

# When the abort() actually fires, catch the signal cleanly. Use
# catch signal SIGABRT (catchpoint) instead of `break abort` because
# the latter resolves to many symbols (e.g. QAbstractSocket::abort()).
catch signal SIGABRT
commands
  printf "\n\n=================================================================\n"
  printf "========== SIGABRT caught ==========\n"
  printf "=================================================================\n\n"
  printf "[backtrace -- current thread]:\n"
  bt full 60
  printf "\n[backtrace -- all threads]:\n"
  thread apply all bt 50
  printf "\n=== detaching and exiting gdb ===\n"
  detach
  quit
end

catch signal SIGSEGV
commands
  printf "\n========== SIGSEGV caught ==========\n"
  bt full 60
  thread apply all bt 50
  detach
  quit
end

run
printf "\n=== run returned (exit or natural termination) ===\n"
quit
