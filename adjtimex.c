/*
	adjtimex - display or set the kernel time variables


	AUTHORS
		ssd@nevets.oau.org (Steven S. Dick)
		jrv@vanzandt.mv.com (Jim Van Zandt)

	$Id: adjtimex.c,v 1.2 1995/03/15 01:08:55 jrv Exp jrv $

	$Log: adjtimex.c,v $
 * Revision 1.2  1995/03/15  01:08:55  jrv
 * Moved documentation to README file and man page.
 * Ran through indent.
 * Usage msg only shows "print" once.
 *
 * Revision 1.1  1995/03/07  01:46:31  jrv
 * Initial revision
 *

********/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/time.h>
#include <syscall.h>
#include <sys/timex.h>

_syscall1(int, adjtimex, struct timex *, txcp)
int F_print = 0;

struct timex txc;

#define TXO(x) &txc.x
struct options {
    const char *name;
    int minlen;
    int mask;
    long *offset;
}

Options[] =
{
    { "print", 1, 0, 0 } ,
    { "offset", 1, ADJ_OFFSET, TXO(offset) } ,
    { "singleshot", 2, ADJ_OFFSET_SINGLESHOT, TXO(offset) } ,
    { "frequency", 1, ADJ_FREQUENCY, TXO(frequency) } ,
    { "maxerror", 1, ADJ_MAXERROR, TXO(maxerror) } ,
    { "esterror", 1, ADJ_ESTERROR, TXO(esterror) } ,
    { "timeconstant", 3, ADJ_TIMECONST, TXO(time_constant) } ,
    { "tick", 3, ADJ_TICK, TXO(tick) } ,
    { "status", 2, ADJ_STATUS, (long *) TXO(status) } ,
    { 0, 0, 0, 0 }
};

void
usage(void)
{
    struct options *op;

    fprintf(stderr, "Usage:\n\tadjtimex [-print] [-option newvalue]\n"
	    "Where option is one of:\n\t");
    for (op = Options; (++op)->name;)
	fprintf(stderr, "%s ", op->name);
    fprintf(stderr, "\n");
    exit(1);
}

int
main(int argc, char *argv[])
{
    int ret, saveerr;
    unsigned int len;
    long num;

    txc.mode = 0;

    {
	char **ap, *s;
	struct options *op;

	for (ap = argv + 1; *ap; ap++) {
	    if (**ap != '-')
		usage();
	    s = *ap + 1;
	    len = strlen(s);
	    for (op = Options; op->name; op++) {
		if (strncmp(s, op->name, len) == 0) {
		    if (len < op->minlen) {
			fprintf(stderr, "Option '%s' not unique, "
			"needs at least %d letters\n", s, op->minlen);
			usage();
		    } else if (!op->offset) {	/* no args */
			if (ap[1] && ap[1][0] != '-') {
			    fprintf(stderr, "Option '%s' does not take "
			    			"arguments.\n", op->name);
			    usage();
			}
			/* only non-arg option is print right now */
			F_print = 1;
		    } else {
			if (!*++ap || sscanf(*ap, "%ld", &num) != 1) {
			    fprintf(stderr, "Option '%s' requires a value.\n", 
			    					op->name);
			    usage();
			} else {
			    if (op->mask == ADJ_STATUS)
				*(int *) op->offset = num;
			    else
				*op->offset = num;
			    txc.mode |= op->mask;
			}
		    }
		    break;
		}
	    }
	    if (!op->name) {
		fprintf(stderr, "Unknown option '%s'.\n", s);
		usage();
	    }
	}
    }
    ret = adjtimex(&txc);
    saveerr = errno;
    if (F_print) {
	printf("         mode: %d\n"
	       "       offset: %ld\n"
	       "    frequency: %ld\n"
	       "     maxerror: %ld\n"
	       "     esterror: %ld\n"
	       "       status: %d\n"
	       "time_constant: %ld\n"
	       "    precision: %ld\n"
	       "    tolerance: %ld\n"
	       "         tick: %ld\n"
	       "         time:  %lds %ldus\n",
	       txc.mode,
	       txc.offset,
	       txc.frequency,
	       txc.maxerror,
	       txc.esterror,
	       txc.status,
	       txc.time_constant,
	       txc.precision,
	       txc.tolerance,
	       txc.tick,
	       txc.time.tv_sec, 
	       txc.time.tv_usec);
	if (saveerr == 0 && ret != 0)
	    printf(" return value = %d\n", ret);
    }
    if (ret != 0 && saveerr != 0) {
	if (ret != -1)
	    fprintf(stderr, "%d ", ret);
	errno = saveerr;
	perror("adjtimex");
	exit(1);
    }
    return 0;
}
