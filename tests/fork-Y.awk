NR == 2 {
        strace_pid=$6
}

NR == 3 {
        if (strace_pid != $1)
                exit 1
}
