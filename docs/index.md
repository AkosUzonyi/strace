# GSoC 2020 strace - PID namespace translation support
Ákos Uzonyi, 2020.08.24.

## Abstract
When strace is tracing a process which is in an other process namespace than strace itself, PIDs in system call arguments and return values are printed as PIDs in the target process' namespace. This is normal behavior, as these are the actual values that are passed between the process and the kernel. But it would be useful to have an option for strace to translate these ids to strace's own pid namespace. For example when tracing a process inside a Docker container, the translated PIDs allows us, to access these the processes from the host environment (eg. inspecting processes created by the clone syscall). The goal of the project is to add a flag to strace, which enables this translation.

## Results
The --pidns-translation option is added to strace, which makes strace print all the PIDs in its own PID namespace too, like this:

        getpid() = 4 /* 1238378 in strace's PID NS */

Also, various strace features which require reading files in /proc (thread enumeration, file descriptor decoding, mmap cache) are now working even when /proc is mounted from a different namespace (PIDs are translated for /proc/<pid> paths)

## Commits
The implementation of this feature is done in 5 commits:

1. PID namespace translation support

   Adds the main file responsible for the translation (pidns.c), a trie implementation, the --pidns-translation option, and a description in the manual.

1. Use printpid in decoders

   Modifies syscall decoders, to print PIDs using the printpid function in pidns.c

1. Use get_proc_pid for /proc paths

   If /proc is mounted from an other namespace, it's necessary to translate PIDs to that namespace if strace want to read some information from /proc. This commit uses get_proc_pid function in pidns.c in each place where /proc/<pid> is accessed.

1. Implement testing framework for pidns

   Tests for this feature require to be run in a new PID namespace.

1. Add tests for PID namespace translation

   Adds an extra test for each syscall where PIDs are printed.

## TODO

* Work on reviews
* Fix errors appearing on various systems
* *(Optional) Performance improvement: tranlsation by SCM_CREDENTIALS messages over unix socket*

## Links

[Commits in github](https://github.com/AkosUzonyi/strace/commits/pidns?author=AkosUzonyi&until=2020-10-31)\
(All commits listed here are done during GSoC 2020, although not each of them is related to the project)
