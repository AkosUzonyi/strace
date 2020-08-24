# GSoC 2020 - PID namespace translation support
Ákos Uzonyi, 2020.08.24.

## Abstract
When strace is tracing a process which is in an other process namespace than strace itself, PIDs in system call arguments and return values are printed as PIDs in the target process' namespace. This is normal behavior, as these are the actual values that are passed between the process and the kernel. But it would be useful to have an option for strace to translate these ids to strace's own pid namespace. For example when tracing a process inside a Docker container, the translated PIDs allows us, to access these the processes from the host environment (eg. inspecting processes created by the clone syscall). The goal of the project is to add a flag to strace, which enables this translation.

## Results
Introduced a new `--pidns-translation` option, which makes strace print all PIDs in its own PID namespace too, like this:

	getpid() = 4 /* 1238378 in strace's PID NS */

Also, various strace features which require reading files in /proc (thread enumeration, file descriptor decoding, mmap cache) are now working even when /proc is mounted from a different namespace (PIDs are translated for /proc/\<pid\> paths)

The code is not yet merged, but I'll work on it even after the deadline.

## Commits
The implementation of this feature is done in 5 commits:

1. **PID namespace translation support**\
   Adds the main file responsible for the translation (pidns.c), a trie implementation, the --pidns-translation option, and a description to the manual.

1. **Use printpid in decoders**\
   Modifies syscall decoders, to print PIDs using the printpid function in pidns.c

1. **Use get_proc_pid for /proc paths**\
   Where a /proc/\<pid\> directory is accessed, adds a get_proc_pid call to translate the PID to the namespace of /proc.

1. **Implement testing framework for pidns**\
   Tests for this feature require to be run in a new PID namespace.

   Adds a little framework for pidns tests (tests/pidns.c), which creates child processes in a new namespace, and helps printing the expected translation PID.

1. **Add tests for PID namespace translation**\
   Adds an extra test for each syscall where PIDs are printed.

## TODO
* Work on reviews
* Fix errors appearing on various systems
* *(Optional) Performance improvement: translation by SCM_CREDENTIALS messages over unix socket*

## Links
[Commits on github](https://github.com/AkosUzonyi/strace/commits/pidns?author=AkosUzonyi&until=2020-10-31)\
(All commits listed here are made during GSoC 2020, although not all of them is related to the main project)

[GSoC project page](https://summerofcode.withgoogle.com/projects/#5517277224501248)
