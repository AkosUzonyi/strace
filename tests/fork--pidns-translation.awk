{
        match($0, "([0-9]+) in strace\x27s PID NS", a);
        if (a[1])
                fork_pid = a[1]

        match($0, /([0-9]+) \+\+\+ exited with 0 \+\+\+/, a);
        if (a[1] && !exit_pid)
                exit_pid = a[1]
}

END {
        if (!fork_pid || !exit_pid || fork_pid != exit_pid)
                exit 1
}
