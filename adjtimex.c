/*
	adjtimex - display or set the kernel time variables


	AUTHORS
		ssd@nevets.oau.org (Steven S. Dick)
		jrv@vanzandt.mv.com (Jim Van Zandt)

	$Id: adjtimex.c,v 1.3 1997/02/28 02:57:27 jrv Exp jrv $

	$Log: adjtimex.c,v $
	Revision 1.3  1997/02/28 02:57:27  jrv
	Added --help and --version switches.

	Revision 1.2.1.4  1997/02/25 11:53:12  jrv
	put settimeofday() stuff in separate function, called twice.

	Revision 1.2.1.3  1997/02/25 03:39:52  jrv
	Incorporated clockdiff functions into adjtimex, as the "--comparing"
	and "--adjusting" options.  Repeat count set by "--count", and
	interval set by "--interval".

	Revision 1.2.1.2  1997/02/25 02:05:29  jrv
	Parsing command line options with getopt_long_only().
	Test for "-singleshot" had broken "-offset" option.

	Revision 1.2.1.1  1997/02/21 01:29:58  jrv
	Removed "status" setting option.
	Checking that ADJ_OFFSET_SINGLESHOT is not used with any other option.
	Label "time" -> "raw time".
	Ensuring kernel resets its internal status to TIME_BAD by calling
	settimeofday().

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
#include <getopt.h>

_syscall1(int, adjtimex, struct timex *, txcp)
int F_print = 0;

struct timex txc;

struct option longopt[]=
{
  {"adjust", 2, NULL, 'a'},
  {"compare", 2, NULL, 'c'},
  {"interval", 1, NULL, 'i'},
  {"print", 0, NULL, 'p'},
  {"offset", 1, NULL, 'o'},
  {"singleshot", 1, NULL, 's'},
  {"frequency", 1, NULL, 'f'},
  {"maxerr", 1, NULL, 'm'},
  {"esterr", 1, NULL, 'e'},
  {"timeconstant", 1, NULL, 'T'},
  {"tick", 1, NULL, 't'},
  {0,0,0,0}
};

int universal = 0;
int comparing = 0;
int adjusting = 0;
int interval = 10;
int count = 8;
int marked;

void compare();
void reset_time_status();

void
usage(void)
{
    struct option *op;

    fprintf(stderr, "Usage:\n\tadjtimex [-print] [-option newvalue]\n"
	    "Where option is one of:\n\t");
    for (op = longopt; (++op)->name;)
	fprintf(stderr, "%s ", op->name);
    fprintf(stderr, "\n");
    exit(1);
}


int
main(int argc, char *argv[])
{
    int ret, saveerr, changes;
    unsigned int len;
    long num;
    extern char *optarg;
    int c;

    txc.modes = 0;

    while((c = getopt_long_only(argc, argv, "", 
				longopt, NULL)) != -1)
      {
	switch(c)
	  {
	  case 'a':
	    adjusting = 1;
	    if (optarg)
	      count = atoi(optarg);
	    break;
	  case 'c':
	    comparing = 1;
	    if (optarg)
	      count = atoi(optarg);
	    break;
	  case 'i':
	    interval = atoi (optarg);
	    if (interval <= 1 || interval > 1000)
	      {
		fprintf (stderr, "repeat interval out of range\n");
		exit (1);
	      }
	    break;
	  case 'p':
	    F_print = 1;
	    break;
	  case 'o':
	    txc.offset = atol(optarg);
	    txc.modes |= ADJ_OFFSET;
	    break;
	  case 's':
	    txc.offset = atol(optarg);
	    txc.modes |= ADJ_OFFSET_SINGLESHOT;
	    break;
	  case 'f':
	    txc.freq = atol(optarg);
	    txc.modes |= ADJ_FREQUENCY;
	    break;
	  case 'm':
	    txc.maxerror = atol(optarg);
	    txc.modes |= ADJ_MAXERROR;
	    break;
	  case 'e':
	    txc.esterror = atol(optarg);
	    txc.modes |= ADJ_ESTERROR;
	    break;
	  case 'T':
	    txc.constant = atol(optarg);
	    txc.modes |= ADJ_TIMECONST;
	    break;
	  case 't':
	    txc.tick = atol(optarg);
	    txc.modes |= ADJ_TICK;
	    break;
	  case '?':
	  default:
	    usage();
	    exit(1);
	  }
	}

    changes = txc.modes;

    if (count <= 0 ) {
      fprintf(stderr, "loop count out of range\n");
      exit(1);
    }

    if (adjusting || comparing)
      {
	if (changes || F_print)
	  {
	    fprintf(stderr, 
"-adjust or -compare cannot be used with any other options\n");
	    exit(1);
	  }
	compare();
      }    

    if ((txc.modes & ADJ_OFFSET_SINGLESHOT) == ADJ_OFFSET_SINGLESHOT && 
	txc.modes != ADJ_OFFSET_SINGLESHOT)
      {
	fprintf(stderr, "-singleshot cannot be used with "
		"any other option except -print\n");
	usage();
      }

    errno = 0;
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
	       "     raw time:  %ds %dus = %d.%06d\n",
	       txc.modes,
	       txc.offset,
	       txc.freq,
	       txc.maxerror,
	       txc.esterror,
	       txc.status,
	       txc.constant,
	       txc.precision,
	       txc.tolerance,
	       txc.tick,
	       txc.time.tv_sec, 
	       txc.time.tv_usec,
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
    if (changes)
      reset_time_status();

    return 0;
}



 /* Here the information for CMOS clock adjustments is kept. */
#define ADJPATH "/etc/adjtime"

 /* used for debugging the code. */
 /* #define DEBUG */


#define SECONDSPERDAY 86400

#ifndef USE_INLINE_ASM_IO
     int cmos_fd;
#endif

#define CMOS_READ(addr)      ({outb(0x70,(addr)|0x80); inb(0x71);})
#define CMOS_WRITE(addr,val) ({outb(0x70,(addr)|0x80); outb(0x71,(val)); })

     static inline void
       outb (short port, char val)
{
#ifdef USE_INLINE_ASM_IO
  __asm__ volatile ("out%B0 %0,%1"::"a" (val), "d" (port));

#else
  lseek (cmos_fd, port, 0);
  write (cmos_fd, &val, 1);
#endif
}

static inline unsigned char
inb (short port)
{
  unsigned char ret;

#ifdef USE_INLINE_ASM_IO
  __asm__ volatile ("in%B0 %1,%0":"=a" (ret):"d" (port));

#else
  lseek (cmos_fd, port, 0);
  read (cmos_fd, &ret, 1);
#endif
  return ret;
}

void
cmos_init ()
{
#ifdef USE_INLINE_ASM_IO
  if (ioperm (0x70, 2, 1))
    {
      fprintf (stderr, "clock: unable to get I/O port access\n");
      exit (1);
    }
#else
  cmos_fd = open ("/dev/port", 2);
  if (cmos_fd < 0)
    {
      perror ("unable to open /dev/port read/write : ");
      exit (1);
    }
  if (lseek (cmos_fd, 0x70, 0) < 0 || lseek (cmos_fd, 0x71, 0) < 0)
    {
      perror ("unable to seek port 0x70 in /dev/port : ");
      exit (1);
    }
#endif
}

static inline int
cmos_read_bcd (int addr)
{
  int b;

  b = CMOS_READ (addr);
  return (b & 15) + (b >> 4) * 10;
}

static void 
xusleep(long microseconds)
{
  fd_set rfds, wfds, efds;
  struct timeval tv;

  FD_ZERO(&rfds);
  FD_ZERO(&wfds);
  FD_ZERO(&efds);
  tv.tv_sec = microseconds/1000000;
  tv.tv_usec = microseconds - tv.tv_sec*1000000;
  (void)select(0, &rfds, &wfds, &efds, &tv);
}

/* compare the system and CMOS clocks.  If "adjusting" is nonzero,
   adjust sytem time to match the CMOS clock. */
void
compare()
{
  struct timex txc;
  struct tm tm;
  time_t cmos_time;
  time_t last_time;
  double cmos_sec, system_sec, dif, dif_prev = 0.;
  char *zone;
  char zonebuf[256];
  FILE *adj;
  double factor;
  double cmos_adjustment;
  double not_adjusted;
  int i;
  int loops = 0;
  int arg;
  extern char *optarg;
  struct timeval now;

      /* Read adjustment parameters first */
  if ((adj = fopen (ADJPATH, "r")) == NULL)
    {
      perror (ADJPATH);
      exit (2);
    }
  if (fscanf (adj, "%lf %ld %lf", &factor, &last_time, &not_adjusted) < 0)
    {
      perror (ADJPATH);
      exit (2);
    }
  fclose (adj);
#ifdef DEBUG
  printf ("Last adjustment done at %ld seconds after 1/1/1970\n", 
	  (long) last_time);
#endif

  while (count != 0)
    {
      if (count > 0) count--;

      cmos_init ();

      /* read RTC exactly on falling edge of update flag */
      /* Wait for rise.... (may take up to 1 second) */

      for (i = 0; i < 10000000; i++)
	if (CMOS_READ (10) & 0x80)
	  break;

      /* Wait for fall.... (must try at least 2.228 ms) */

      for (i = 0; i < 1000000; i++)
	if (!(CMOS_READ (10) & 0x80))
	  break;

      /* The "do" loop is "low-risk programming" */
      /* In theory it should never run more than once */
      do
	{
	  tm.tm_sec = cmos_read_bcd (0);
	  tm.tm_min = cmos_read_bcd (2);
	  tm.tm_hour = cmos_read_bcd (4);
	  tm.tm_wday = cmos_read_bcd (6);
	  tm.tm_mday = cmos_read_bcd (7);
	  tm.tm_mon = cmos_read_bcd (8);
	  tm.tm_year = cmos_read_bcd (9);
	}
      while (tm.tm_sec != cmos_read_bcd (0));

      /* fetch system time immediately */
      gettimeofday (&now, NULL);


      tm.tm_mon--;		/* DOS uses 1 base */
      tm.tm_wday -= 3;		/* DOS uses 3 - 9 for week days */
      tm.tm_isdst = -1;		/* don't know whether it's daylight */
#ifdef DEBUG
      printf (" mday=%d  mon=%d  wday=%d  year=%d\n",
	      tm.tm_mday, tm.tm_mon, tm.tm_wday, tm.tm_year);
      printf ("Cmos time  %d:%02d:%02d\n", tm.tm_hour, tm.tm_min, tm.tm_sec);
#endif

      /*
       * Mktime assumes we're giving it local time.  If the CMOS clock is in
       * GMT, we have to set up TZ so mktime knows it.  Tzset gets called
       * implicitly by the time code, but only the first time.  When
       * changing the environment variable, better call tzset explicitly.
       */
      if (universal)
	{
	  zone = (char *) getenv ("TZ");	/* save original time zone */
	  (void) putenv ("TZ=");
	  tzset ();
	  cmos_time = mktime (&tm);
	  /* now put back the original zone */
	  if (zone)
	    {
	      if ((strlen (zone) + 4) > sizeof (zonebuf))
		{
		  fprintf (stderr, "Size of TZ variable is too long\n");
		  exit (2);
		}
	      strcpy (zonebuf, "TZ=");
	      strcat (zonebuf, zone);
	      putenv (zonebuf);
	    }
	  else
	    {			/* wasn't one, so clear it */
	      putenv ("TZ");
	    }
	  tzset ();
	  printf ("%s", ctime (&cmos_time));
	}
      else
	{
	  cmos_time = mktime (&tm);
	  /* printf ("%s", asctime (&tm)); */
	}

      system_sec = now.tv_sec + .000001 * now.tv_usec;
#ifdef DEBUG
      printf ("Number of seconds since 1/1/1970 is %ld\n",
	      (long) cmos_time);
#endif
      cmos_adjustment = ((double) (cmos_time - last_time))
	* factor / SECONDSPERDAY
	+ not_adjusted;
      cmos_sec = cmos_time + cmos_adjustment;
#ifdef DEBUG
      printf ("Time since last adjustment is %d sec",
	      (int) (cmos_time - last_time));
      printf (", now needs adjustment by %9.6f sec\n", cmos_adjustment);
#endif
      dif = system_sec - cmos_sec;

      txc.modes = 0;
      if (adjtimex (&txc) < 0) {perror ("adjtimex"); exit(1);}
/*
999999999   999999999 999999999 999999999 999999999 999999999
cmos time        diff  2nd diff      freq      tick    offset
843237856   -14399.601992 -14399.601992         0     10000         0
cmos time        diff  2nd diff      freq      tick    offset
843237866   -14399.609014 -0.007021         0     10000         0
cmos time        diff  2nd diff      freq      tick    offset
843237876   -14399.615963 -0.006949         0     10000         0
cmos time        diff  2nd diff      freq      tick    offset
843237886   -14399.622909 -0.006946         0     10000         0
cmos time        diff  2nd diff      freq      tick    offset
843237896   -14399.629853 -0.006943         0     10000         0


                                           --- current ---    -- suggested --
cmos time     system-cmos       2nd diff    tick      freq     tick      freq
856231718    -17999.867235  -17999.867235   10000         0
856231728    -17999.867023       0.000212   10000         0
856231738    -17999.866792       0.000231   10000         0    10000   1678819

*/


      if (! marked++ )
	{
	  if (interval)
	    printf (
"                                           --- current ---    -- suggested --\n"
"cmos time     system-cmos       2nd diff    tick      freq     tick      freq\n");
	  else
	    printf (
"cmos time     system-cmos       2nd diff    tick      freq\n");
	}
      printf ("%9ld  %14.6f %14.6f %7ld %9ld",
	      (long) cmos_sec,
	      dif,
	      dif - dif_prev,
	      txc.tick,
	      txc.freq);
      if (++loops > 2)
	{
#define SHIFT (1<<16)
	  long tick_delta = 0;
	  double second_diff, error_ppm;

	  second_diff = dif - dif_prev;
	  error_ppm = second_diff/interval*1000000 - txc.freq/(double)SHIFT;
	  if (error_ppm > 100)
	    tick_delta = -(error_ppm + 50)/100;
	  else if (error_ppm < -100)
	    tick_delta = (-error_ppm + 50)/100;
	  error_ppm += tick_delta*100;
	  printf("  %7ld %9.0f\n", txc.tick + tick_delta, -error_ppm*SHIFT);
	  if (loops > 4 && adjusting)
	    {
	      txc.modes = ADJ_FREQUENCY | ADJ_TICK;
	      txc.tick += tick_delta;
	      txc.freq = -error_ppm*SHIFT;
	      if (adjtimex (&txc) < 0) 
		{
		  perror ("adjtimex"); 
		  exit(1);
		}
	      reset_time_status();
	      loops -= 3;
	    }
	}
      else
	printf("\n");
      dif_prev = dif;
      if (interval == 0)
	break;
      xusleep (interval*1000000L - 900000); /* reading RTC takes 1 sec */
    }
}

void reset_time_status()
{
  /* Using the adjtimex(2) system call to set any time
     parameter makes the kernel think the clock is
     synchronized with an external time source, so it sets
     the kernel variable time_status to TIME_OK.
     Thereafter, it will periodically adjust the CMOS clock
     to match.  We prevent this by setting the clock,
     because that has the side effect of resetting
     time_status to TIME_BAD.  We try not to actually
     change the clock setting. */
  struct timeval tv1, tv2;
  long carry, overhead_usec;
  if (gettimeofday(&tv1, NULL)) {perror("adjtimex"); exit(1);}
  if (gettimeofday(&tv2, NULL)) {perror("adjtimex"); exit(1);}
  overhead_usec = tv2.tv_usec - tv1.tv_usec + 
    1000000*(tv2.tv_sec - tv1.tv_sec);
  tv2.tv_usec += overhead_usec;
  carry = tv2.tv_usec/1000000;
  tv2.tv_sec += carry;
  tv2.tv_usec -= 1000000*carry;
  if (settimeofday(&tv2, NULL)) {perror("adjtimex"); exit(1);}
}  
