#include "defs.h"

SYS_FUNC(getpid)
{
	return RVAL_DECODED | RVAL_TGID;
}

SYS_FUNC(gettid)
{
	return RVAL_DECODED | RVAL_TID;
}

SYS_FUNC(getpgrp)
{
	return RVAL_DECODED | RVAL_PGID;
}

SYS_FUNC(getpgid)
{
	printpid(tcp, tcp->u_arg[0], PT_TGID);

	return RVAL_DECODED | RVAL_PGID;
}

SYS_FUNC(getsid)
{
	printpid(tcp, tcp->u_arg[0], PT_TGID);

	return RVAL_DECODED | RVAL_SID;
}

SYS_FUNC(setpgid)
{
	printpid(tcp, tcp->u_arg[0], PT_TGID);
	tprints(", ");
	printpid(tcp, tcp->u_arg[1], PT_PGID);

	return RVAL_DECODED;
}
