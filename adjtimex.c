/*
#define DEBUG

	adjtimex - display or set the kernel time variables


	AUTHORS
		ssd@nevets.oau.org (Steven S. Dick)
		jrv@vanzandt.mv.com (Jim Van Zandt)

	$Id: adjtimex.c,v 1.6 1998/08/23 00:11:47 jrv Exp jrv $

*/

#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <getopt.h>
#include <mat.h>
#include <math.h>
#include <netdb.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/timex.h>
#include <sys/types.h>
#include <syscall.h>
#include <time.h>
#include <unistd.h>
#include <utmp.h>

_syscall1(int, adjtimex, struct timex *, txcp)
int F_print = 0;

#ifndef LOG_PATH
#define LOG_PATH "/var/log/clocks.log"
#endif
#ifndef WTMP_PATH
#define WTMP_PATH "/var/log/wtmp"
#endif

 /* Here the information for CMOS clock adjustments is kept. */
#define ADJPATH "/etc/adjtime"

 /* used for debugging the code. */
 /* #define DEBUG */

#define RTC_JITTER .000025	/* assumed error in reading CMOS clock (sec) */
#define SECONDSPERDAY 86400
#define BUFLEN 128

#ifndef USE_INLINE_ASM_IO
     int cmos_fd;
#endif

#define CMOS_READ(addr)      ({outb(0x70,(addr)|0x80); inb(0x71);})
#define CMOS_WRITE(addr,val) ({outb(0x70,(addr)|0x80); outb(0x71,(val)); })

struct hack {
  double ref;			/* reference time for time hack */
  double sigma_ref;		/* expected error in above (or zero if
				   no reference time available) */
  time_t log;			/* reference time as time_t */
  double sys;			/* system time */
  int tick;			/* "tick" system parameter */
  int freq;			/* "freq" system parameter */
  char sys_ok;			/* nonzero if system clock undisturbed
				   during previous period */
  double cmos;			/* CMOS time */
  char cmos_ok;			/* nonzero if cmos clock undisturbed
				   during previous period */
  char valid;			/* bit flags (see below) */
  double sys_rate, sys_sigma;
  double cmos_rate, cmos_sigma;
  double relative_rate, relative_sigma;
} prev;

/* constants for `valid' member of struct hack */
#define CMOS_VALID 1
#define SYS_VALID 2
#define REF_VALID 4

struct cmos_adj
{
  double ca_factor;
  long ca_adj_time;
  double ca_remainder;
} ca;

struct timex txc;

#define HELP 131

struct option longopt[]=
{
  {"adjust", 2, NULL, 'a'},
  {"compare", 2, NULL, 'c'},
  {"log", 2, NULL, 'l'},
  {"esterr", 1, NULL, 'e'},
  {"frequency", 1, NULL, 'f'},
  {"host", 1, NULL, 'h'},
  {"help", 0, NULL, HELP},
  {"interval", 1, NULL, 'i'},
  {"maxerr", 1, NULL, 'm'},
  {"offset", 1, NULL, 'o'},
  {"print", 0, NULL, 'p'},
  {"review", 2, NULL, 'r'},
  {"singleshot", 1, NULL, 's'},
  {"timeconstant", 1, NULL, 'T'},
  {"tick", 1, NULL, 't'},
  {"utc", 0, NULL, 'u'},
  {"version", 0, NULL, 'v'},
  {"watch", 0, NULL, 'w'},
  {0,0,0,0}
};

int adjusting = 0;
int comparing = 0;
int logging = 0;
int reviewing = 0;
int interval = 10;
int count = 8;
int marked;
int universal = 0;
int watch;			/* nonzero if time specified on command line */
int undisturbed_sys = 0;
int undisturbed_cmos = 0;
char *log_path = LOG_PATH;

char *timeserver;		/* points to name of timeserver */

void compare(void);
void failntpdate();
void reset_time_status(void);
static double compare_cmos_sys(void);
struct cmos_adj *get_cmos_adjustment(void);
void log_times(void);
int valid_system_rate(double ftime_sys, double ftime_ref, double sigma_ref);
int valid_cmos_rate(double ftime_cmos, double ftime_ref, double sigma_ref);
void sethackent(void);
void endhackent(void);
struct hack *gethackent(void);
void puthackent(struct hack *ph);
time_t mkgmtime(struct tm *tp);
int compare_tm(struct tm *first, struct tm *second);
static void *xmalloc(int n);
static void *xrealloc(void *pv, int n);
void review(void);
void kalman_update(double *x, int xr, double *p, double *h,
		   double *z, int zr, double *r);

void
usage(void)
{
  char msg[]=
"
Usage: adjtimex  [OPTION]... 
Mandatory or optional arguments to long options are mandatory or optional
for short options too.

Get/Set Kernel Time Parameters:
       -p, --print               print values of kernel time variables
       -t, --tick val            set the kernel tick interval in usec
       -f, --frequency newfreq   set system clock frequency offset
       -s, --singleshot adj      slew the system clock by adj usec
       -o, --offset adj          add a time offset of adj usec
       -m, --maxerror val        set maximum error (usec)
       -e, --esterror val        set estimated error (usec)
       -T, --timeconstant val    set phase locked loop time constant

Estimate Systematic Drifts:
       -c, --compare[=count]     compare system and CMOS clocks
       -a, --adjust[=count]      adjust system clock per CMOS clock
       -i, --interval tim        set clock comparison interval (sec)
       -l, --log[=file]          log current times to file
       -h, --host timeserver     query the timeserver
       -w, --watch               get current time from user
       -r, --review[=file]       review clock log file, estimate drifts
       -u, --utc                 the CMOS clock is set to UTC

Informative Output:
       -?, --help                print this help, then exit
       -v, --version             print adjtimex program version, then exit
";

  fputs(msg, stdout);
  exit(0);
}

int
main(int argc, char *argv[])
{
    int ret, saveerr, changes;
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
	  case 'l':
	    logging = 1;
	    if (optarg)
	      log_path = strdup(optarg);
	    if (!log_path)
	      {
		fprintf (stderr, "insufficient memory\n");
		exit(1);
	      }
	    break;
	  case 'h':
	    timeserver = strdup(optarg);
	    if (!timeserver)
	      {
		fprintf (stderr, "insufficient memory\n");
		exit(1);
	      }
	    logging = 1;
	    break;
	  case 'r':
	    reviewing = 1;
	    if (optarg)
	      log_path = strdup(optarg);
	    if (!log_path)
	      {
		fprintf (stderr, "insufficient memory\n");
		exit(1);
	      }
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
	  case 'u':
	    universal = 1;
	    break;
	  case 'v':
	    {
	      char version[]="$Revision: 1.6 $";
	      strtok(version, " ");
	      printf("adjtimex %s\n", strtok(NULL, " "));
	      exit(0);
	    }
	  case 'w':
	    watch = 1;
	    logging = 1;
	    break;
	  case HELP:
	    usage();
	    break;
	  case '?':
	  default:
	    fprintf(stderr, "For valid options, try 'adjtimex --help'\n");
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
"-adjust or -compare cannot be used with any other options that"
" set values\n");
	    exit(1);
	  }
	compare();
      }    

    if (logging)
      {
	/*
	if (geteuid() != 0)
	  {
	    fprintf(stderr, "sorry, only root can record clock comparisons\n");
	    exit(1);
	  }
	  */
	log_times();
	exit(0);
      }

    if (reviewing)
      {
	review();
	exit(0);
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
	       (int)txc.time.tv_sec, 
	       (int)txc.time.tv_usec,
	       (int)txc.time.tv_sec, 
	       (int)txc.time.tv_usec);
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
  FILE *adj;
  double factor;
  double cmos_adjustment;
  double not_adjusted;
  int i;
  int loops = 0;
  extern char *optarg;
  struct timeval now;
  int wrote_to_log = 0;

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
  /*
cmos clock last adjusted at Tue Aug 26 11:43:57 1997 (= 872610237)
          current cmos time Tue Aug 26 21:27:05 1997 EDT (= 872645225)
*/
  {
    struct tm bdt;
    if (universal)
      {
	bdt = *gmtime(&last_time);
	(void)mkgmtime(&bdt);	/* set tzname */
      }
    else
      {
	bdt = *localtime(&last_time);
	(void)mktime(&bdt);	/* set tzname */
      }
    printf ("cmos clock last adjusted %.24s %s "
	    "(= %ld)\n", 
	    ctime(&last_time), tzname[tm.tm_isdst?1:0], (long) last_time);
  }
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

      if (universal)
	cmos_time = mkgmtime(&tm);
      else
	cmos_time = mktime (&tm);
      /* printf ("%s", asctime (&tm)); */

      system_sec = now.tv_sec + .000001*now.tv_usec;

      /* If we're adjusting time parameters, we want to make a log
         entry only for the first two comparisons (before we change
         the parameters).  Otherwise, we want to log the first and last
         comparisons in order to maximize the duration. */
      if (logging && (wrote_to_log ^ (adjusting?(loops==0):(count!=0))))
	{
	  struct hack h;

	  h.ref = 0;
	  h.sigma_ref = 0;
	  h.log = (time_t)system_sec;
	  h.sys = system_sec;
	  txc.modes = 0;
	  adjtimex(&txc);
	  h.tick = txc.tick;
	  h.freq = txc.freq;
	  h.sys_ok = wrote_to_log;
	  h.cmos = cmos_time;
	  h.cmos_ok = wrote_to_log;
	  puthackent(&h);
	  wrote_to_log = 1;
	}

#ifdef DEBUG
      printf ("       current cmos time %.24s %s (= %ld)\n", 
	      asctime(&tm), tzname[tm.tm_isdst?1:0], (long) cmos_time);
#endif
      cmos_adjustment = ((double) (cmos_time - last_time))
	* factor / SECONDSPERDAY
	+ not_adjusted;
      cmos_sec = cmos_time + cmos_adjustment;
#ifdef DEBUG
      printf (
"           time since last adjustment %10.6f days (= %9d sec)\n",
	      (int) (cmos_time - last_time) / (double)SECONDSPERDAY,
	      (int) (cmos_time - last_time));
      printf (
"                               factor %10.6f sec/day\n",
	      factor);
      printf (
"                           adjustment %10.6f + %7.6f = %7.6f sec\n",
	       ((double) (cmos_time - last_time))*factor/SECONDSPERDAY,
	       not_adjusted, cmos_adjustment);
#endif
      dif = system_sec - cmos_sec;

      txc.modes = 0;
      if (adjtimex (&txc) < 0) {perror ("adjtimex"); exit(1);}
/*
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
  /* Using the adjtimex(2) system call to set any time parameter makes
     the kernel think the clock is synchronized with an external time
     source, so it sets the kernel variable time_status to TIME_OK.
     Thereafter, it will periodically adjust the CMOS clock to match.
     We prevent this by setting the clock, because that has the side
     effect of resetting time_status to TIME_BAD.  We try not to
     actually change the clock setting. */
  struct timeval tv1, tv2;
  long carry_sec, overhead_usec;
  if (gettimeofday(&tv1, NULL)) {perror("adjtimex"); exit(1);}
  if (gettimeofday(&tv2, NULL)) {perror("adjtimex"); exit(1);}
  overhead_usec = tv2.tv_usec - tv1.tv_usec + 
    1000000*(tv2.tv_sec - tv1.tv_sec);
  tv2.tv_usec += overhead_usec;
  carry_sec = tv2.tv_usec/1000000;
  tv2.tv_sec += carry_sec;
  tv2.tv_usec -= 1000000*carry_sec;
  if (settimeofday(&tv2, NULL)) {perror("adjtimex"); exit(1);}
}  

void log_times()
{
#ifdef NET_TIME_CLIENT
  struct protoent proto;
  int sockfd, val, len, c;
  struct sockaddr sa={AF_INET, "127.0.0.1"};
  struct hostent he;
#endif
  double sigma_ref, cmos_ahead;
  char ch, buf[64], *s;
  int n, ret;
  struct timeval tv_sys;
  struct timezone tz;
  struct tm bdt;
  time_t when, tref;
  double ftime_ref, ftime_sys, ftime_cmos;

  if (watch)
    {
      while(1) {
	printf("Please press <enter> when you know the time of day: ");
	ch = getchar();
	gettimeofday(&tv_sys, &tz);
	when = tv_sys.tv_sec;
	bdt = *localtime(&when); /* set default values for most fields */
	strftime(buf, sizeof(buf), "%Z", &bdt);
	printf("  system time then was %.24s %s\n", asctime(&bdt), buf);
	ftime_sys = tv_sys.tv_sec + tv_sys.tv_usec*.000001;
	printf("What time was that according to your reference (%s)?\n"
	       "(hh:mm:ss, or `r' to retry, or `q' to quit): ", buf);
	if (fgets(buf, sizeof(buf), stdin) == NULL) exit(1);
	s = buf;
	while(isspace(*s)) s++;
	if (*s == 'q') exit(0);
	if (*s == 'r') continue;
	
	strptime(buf, "%T", &bdt); /* set time fields from user input */
	printf("      reference time is %s", asctime(&bdt));
	tref = ftime_ref = mktime(&bdt); /* construct a time_t
					    corresponding to the user
					    input */
	printf(" mktime returns time of %s", ctime(&tref));

	printf("reference time - system time = %12.3f - %12.3f "
	       "= %4.3f sec\n", 
	       ftime_ref, ftime_sys, ftime_ref - ftime_sys);

	printf("How big could the error be in this, in seconds?\n"
	       "(or `r' to retry, or `q' to quit) [.5] : ");
	if (fgets(buf, sizeof(buf), stdin) == NULL) exit(1);
	s = buf;
	while(isspace(*s)) s++;
	if (*s == 'q') exit(0);
	if (*s == 'r') continue;

	n = sscanf(buf, "%lf", &sigma_ref);
	if (n < 1)
	  sigma_ref = .5;
	else if (sigma_ref < .02)
	  {
	    printf("You get credit for .02 sec.\n");
	    sigma_ref = .02;
	  }
	break;
      }
    }
  else if (timeserver)
    {
      int ret;
      FILE *ifile;
      char command[128];
      char buf[BUFLEN];
      char tempfile[32];
      char *str[5];
      int i, n=0;
      double d, mean=0, val, var=0, num=0;

      /* if we're root, make sure temporary file is somewhere an
         ordinary user cannot create symbolic links */
      if (geteuid()==0) strcpy(tempfile, "/etc/adjtimex.temp");
      else strcpy(tempfile, "/tmp/adjtimex.temp");
#ifdef NTPDATE_STUB
      sprintf(command, "cat ../ntpdate-sample 2>&1 >%s", tempfile);
#else
      sprintf(command, "ntpdate -d %.32s 2>&1 >%.32s", timeserver, tempfile);
#endif
      ret = system(command);
      if (ret != 0) failntpdate("call to ntpdate failed");

      /* read and save the significant lines, which should look like this:
filter offset: -0.02800 -0.01354 -0.01026 -0.01385
offset -0.013543
 1 Sep 11:51:23 ntpdate[598]: adjust time server 1.2.3.4 offset -0.013543 sec
 */
      ifile = fopen(tempfile, "r");
      while(fgets(buf, BUFLEN, ifile))
	if (strstr(buf, "offset") && n < 4)
	  str[n++] = strdup(buf);
      fclose(ifile);
      unlink(tempfile);
      if (n != 3) failntpdate("cannot understand ntpdate output");
      gettimeofday(&tv_sys, &tz);
      ftime_sys = tv_sys.tv_sec;


      /* ntpdate selects the offset from one of its samples (the one
         with the shortest round-trip delay?) */
      ftime_ref = ftime_sys + atof(strstr(str[2], "offset") + 6);

      {
	time_t now = (time_t)ftime_ref;
	bdt = *gmtime(&now);
	printf("      reference time is %s", ctime(&now));
	printf("reference time - system time = %12.3f - %12.3f "
	       "= %4.3f sec\n", 
	       ftime_ref, ftime_sys, ftime_ref - ftime_sys);
      }

      /* The first saved line shows the offsets for each of ntpdate's
         samples.  Find their variance, which we will use to indicate
         the accuracy of the offset we're using.  This is probably
         conservative, since the offset we're using is probably not
         close to the mean. */
      s = strstr(str[0], ":");
      if (s++ == NULL) failntpdate("cannot understand ntpdate output");
      for (i = 0; i < 4; i++)
	{
	  val = strtod(s, &s);
	  d = val - mean;
	  num += 1.;
	  var = (num-1)/num*(var + d*d/num);
	  mean = ((num-1.)*mean + val)/num;
	}
      sigma_ref = sqrt(var);

#ifdef OWN_IMPLEMENTATION
      /* this is not even close to working yet */
      proto = *getprotobyname("UDP");
      sockfd = socket(AF_INET, SOCK_DGRAM, proto.p_proto);
      he = *gethostbyname("localhost");
      len = he.h_length;
      memcpy(sa.sa_data, he.h_addr_list[0], len);

#ifdef SEND
      val = connect(sockfd, &sa, len);
      if (val == -1) {perror("connect"); exit(1);}
      /*
	int  connect(int  sockfd, struct sockaddr *serv_addr, int addrlen );
	int  send(int  s,  const void *msg, int len , unsigned int flags);
	int sendto(int s, const void *msg, int  len  unsigned  int
	flags, const struct sockaddr *to, int tolen);
	*/
#endif /* SEND */
      val = sendto(sockfd, (const void *)" ", 1, 0, &sa, len);
      if (val == -1) {perror("sendto"); exit(1);}
      sigma_ref = .010;
#endif /* OWN_IMPLEMENTATION */

    }
  else				/* no absolute time reference */
    {
      time_t now;
      gettimeofday(&tv_sys, &tz);
      now = (time_t)tv_sys.tv_sec;
      bdt = *gmtime(&now);
      ftime_sys = tv_sys.tv_sec + tv_sys.tv_usec*.000001;
      ftime_ref = 0;
      sigma_ref = 0;
    }

  cmos_ahead = compare_cmos_sys();
  ftime_cmos = ftime_sys + cmos_ahead;

  /*
                 -reference-time- --------system-time---------- --cmos-time----
1997-06-14 20:22 888888888.888 -3 888888888.888 10000 6666666 n 888888888.888 y
*/

  {
    struct hack h;
    h.ref = ftime_ref;
    h.sigma_ref = sigma_ref;
    h.log = (time_t)ftime_ref;
    h.sys = ftime_sys;
    txc.modes = 0;
    ret = adjtimex(&txc);
    h.tick = txc.tick;
    h.freq = txc.freq;
    h.sys_ok = valid_system_rate(ftime_sys, ftime_ref, sigma_ref);
    h.cmos = ftime_cmos;
    h.cmos_ok = valid_cmos_rate(ftime_cmos, ftime_ref, sigma_ref);
    puthackent(&h);
  }
}

void failntpdate(char *s)
{
  fprintf(stderr, "%s\n", s);
  exit(1);
}

int valid_system_rate(double ftime_sys, double ftime_ref, double sigma_ref)
{
  int n;
  int default_answer;
  int ch;
  char buf[BUFLEN];
  struct hack *ph;
  struct cmos_adj *pca = get_cmos_adjustment();

  sethackent();
  for (n = 0; (ph = gethackent()); n++)
    prev = *ph;			/* fetch last line from logfile */
  endhackent();
  if (n == 0)
    {
      printf("No previous clock comparison in log file\n");
      return 0;
    }
  undisturbed_sys = undisturbed_cmos = 1;

  printf("Last clock comparison was at %.24s\n", ctime(&prev.log));
  
  if (txc.tick == prev.tick && txc.freq == prev.freq)
    printf("Kernel time variables are unchanged - good.\n");
  else
    {
      printf("Kernel time variables have changed - bad.\n");
      undisturbed_sys = 0;
    }
  if (txc.status & 64)
    printf("System clock is currently not disciplined - good.\n");
  else
    {
      printf("System clock is synchronized (by xntpd?) - bad.\n");
      undisturbed_sys = 0;
    }

  {
    time_t lastboot, newtime;
    struct utmp *up;
    
    lastboot = newtime = prev.sys - 1;
    utmpname(WTMP_PATH);
    setutent();
    printf("Checking wtmp file...\n");
    while((up = getutent()))
      {
	if (up->ut_type == BOOT_TIME) 
	  lastboot = up->ut_time;
	if (up->ut_type == NEW_TIME) 
	  newtime = up->ut_time;
      }
    endutent();
    if (lastboot < prev.sys)
      printf("System has not booted since %.24s - good.\n", 
	     ctime(&prev.log));
    else
      {
	printf("System was booted at %.24s - bad.\n", ctime(&lastboot));
	undisturbed_sys = 0;
      }
    if (newtime < prev.sys)
      printf("System time has not been changed since %.24s - good.\n", 
	     ctime(&prev.log));
    else
      {
	printf("System time was reset at %.24s - bad.\n", ctime(&newtime));
	undisturbed_sys = 0;
      }
  }

  if (pca)
    {
      printf("Checking %s...\n", ADJPATH);
      if (pca->ca_adj_time < prev.log)
	printf(
"/sbin/clock has not set system time and adjusted the cmos clock \n"
"since %.24s - good.\n", 
ctime(&prev.log));
      else
	{
	  printf("/sbin/clock set system time and adjusted the cmos clock \n"
		 "at %.24s - bad.\n", 
		 ctime(&pca->ca_adj_time));
	  undisturbed_sys = undisturbed_cmos = 0;
	}
    }

  do 
    {
      default_answer = undisturbed_sys?'y':'n';
      printf("\nAre you sure that, since %.24s,\n", ctime(&prev.log));
      printf("  the system clock has run continuously,\n");
      printf("  it has not been reset with `date' or `/sbin/clock`,\n");
      printf("  the kernel time variables have not been changed, and\n");
      printf("  the computer has not been suspended? (y/n) [%c] ", 
	     default_answer);
      fgets(buf, BUFLEN, stdin);
      ch = buf[0];
      if (ch == '\n') ch = default_answer;
    } while (ch != 'n' && ch != 'y');

  if ((undisturbed_sys = (ch == 'y')))
    {
      double drift_sys_ppm, err_sys_ppm;
      int digits;
      drift_sys_ppm = ((ftime_sys - ftime_ref) - (prev.sys - prev.ref))*
	1.e6/(ftime_ref - prev.ref);
      err_sys_ppm = (prev.sigma_ref + sigma_ref)*1.e6/
	(ftime_ref - prev.ref);
      digits = -(int)floor(log(.5*sigma_ref)/log(10.));
      if (digits < 0) digits = 0;
      
      printf("The estimated error in system time is %.*f +- %.*f ppm\n", 
	     digits, drift_sys_ppm, digits, err_sys_ppm);
    }

  return undisturbed_sys;
}

int valid_cmos_rate(double ftime_cmos, double ftime_ref, double sigma_ref)
{
  int default_answer;
  int ch;
  char buf[BUFLEN];

  default_answer = undisturbed_cmos?'y':'n';
  do
    {
      printf("\nAre you sure that, since %.24s,\n", ctime(&prev.log));
      printf("  the real time clock (cmos clock) has running continuously,\n");
      printf("  it has not been reset with `/sbin/clock',\n");
      printf("  no operating system other than Linux has been running, and\n");
      printf("  xntpd has not been running? (y/n) [%c] ", default_answer);
      fgets(buf, BUFLEN, stdin);
      ch = buf[0];
      if (ch == '\n') ch = default_answer;
    } while (ch != 'n' && ch != 'y');
  if ((undisturbed_cmos = (ch == 'y')))
    {
      double drift_cmos_ppm, err_cmos_ppm;
      int digits;
	    
      drift_cmos_ppm = 
	((ftime_cmos - ftime_ref) - (prev.cmos - prev.ref))*
	1.e6/(ftime_ref - prev.ref);
      err_cmos_ppm = (prev.sigma_ref + sigma_ref)*1.e6/
	(ftime_ref - prev.ref);
      digits = -(int)floor(log(.5*err_cmos_ppm)/log(10.));
      if (digits < 0) digits = 0;
      printf("The estimated error in the cmos clock is %.*f +- %.*f ppm\n", 
	     digits, drift_cmos_ppm, digits, err_cmos_ppm);
    }

  return undisturbed_cmos;
}

struct cmos_adj *get_cmos_adjustment()
{
  FILE *adj;
  static struct cmos_adj ca;
  if ((adj = fopen (ADJPATH, "r")) == NULL)
    {
      perror (ADJPATH);
      exit (2);
    }
  if (fscanf (adj, "%lf %ld %lf", 
	      &ca.ca_factor, 
	      &ca.ca_adj_time, 
	      &ca.ca_remainder) < 0)
    {
      perror (ADJPATH);
      exit (2);
    }
  fclose (adj);
#ifdef DEBUG
  printf ("CMOS clock was last adjusted %s", ctime(&ca.ca_adj_time));
#endif
  return &ca;
}

/* return the difference in seconds: cmos_time - system_time */
static double
compare_cmos_sys()
{
  struct tm tm;
  time_t cmos_time;
  double system_sec;
  double dif;
  int i;
  extern char *optarg;
  struct timeval now;

  if (geteuid() != 0)
    {
      struct tm bdt;
      char before[256], after[256];
      int fd = open("/proc/rtc", O_RDONLY);
      if (fd == -1)
	{
	  fprintf(stderr, "kernel lacks enhanced real time clock support, "
		  "so only root can read RTC\n");
	  exit(1);
	}
      read(fd, before, sizeof(before));
      close(fd);
      do
	{
	  fd = open("/proc/rtc", O_RDONLY);
	  read(fd, after, sizeof(after));
	  gettimeofday (&now, NULL);
	  close(fd);
	} while (!strncmp(before, after, strlen(after)));
      strptime(after, "rtc_time : %H:%M:%S\nrtc_date : %Y-%m-%d", &bdt);

      if (universal)		/* also set tm_wday and tm_yday */
	cmos_time = mkgmtime(&bdt);
      else
	cmos_time = mktime(&bdt);
#ifdef DEBUG
      printf("RTC says date & time are %.24s %s\n",
	     asctime(&bdt), tzname[bdt.tm_isdst?1:0]);
#endif
      system_sec = now.tv_sec + .000001 * now.tv_usec;
      dif = (double)cmos_time - system_sec;
      return dif;
    }
  else				/* I am superuser */
    {
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
    }
  /*
   * Mktime assumes we're giving it local time.  If the CMOS clock is in
   * GMT, we have to set up TZ so mktime knows it.  Tzset gets called
   * implicitly by the time code, but only the first time.  When
   * changing the environment variable, better call tzset explicitly.
   */
  if (universal)
    {
#ifdef ZONESWITCH
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
#else /* !ZONESWITCH */
      cmos_time = mkgmtime(&tm);
#endif
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

  dif = (double)cmos_time - system_sec;

  return dif;
}


static FILE *lfile;		/* pointer to log file, or NULL if it
				   has not been opened yet */

void sethackent(void)
{
  endhackent();
  lfile = fopen(log_path, "r");
  if (!lfile && logging)
    {
      lfile = fopen(log_path, "a+"); /* create it if it doesn't exist */
      if (!lfile)
	{
	  fprintf(stderr, "%s does not exist, and could not be created\n",
		  log_path);
	  exit(1);
	}
      fseek(lfile, 0L, 0);	/* start at beginning */
    }
}

void endhackent(void)
{
  if (lfile) fclose(lfile);
  lfile = NULL;
}

/* read next entry in clock comparison log, fill a struct hack from
   it, and return a pointer to it.  Ignore lines starting with `#'.
   Return NULL when there are no more lines to read.  */
struct hack *gethackent(void)
{
  char buf[256], sys_flag, cmos_flag, junk[26];
  static struct hack h;

  if (!lfile) sethackent();
  if (!lfile) return NULL;
  while(fgets(buf, sizeof(buf), lfile))
    {
      int tokens;
      if (buf[0] == '#')
	continue;
      tokens = sscanf(buf, "%25s %25s %lf %lf %lf %d %d %c %lf %c",
		      junk, junk, &h.ref, &h.sigma_ref, 
		      &h.sys, &h.tick, &h.freq, &sys_flag,
		      &h.cmos, &cmos_flag);
      if (tokens != 10)
	continue;
      h.sys_ok = (tolower(sys_flag) == 'y');      
      h.cmos_ok = (tolower(cmos_flag) == 'y');
      if (h.ref)
	h.log = (time_t)h.ref;
      else if (h.sys)
	h.log = (time_t)h.sys;
      else
	h.log = (time_t)h.cmos;
      h.valid = 0;
      return &h;
    }
  return NULL;
}

/* append an entry to the clock comparison log.  */
void puthackent(struct hack *ph)
{
  struct tm bdt;
  char timestring[32];
  int digits;

  if (ph->sigma_ref)
    ph->log = (time_t)ph->ref;
  else
    ph->log = (time_t)ph->sys;
  bdt = *gmtime(&ph->log);
  strftime(timestring, sizeof(timestring), "%Y-%m-%d %H:%M", &bdt);
  if (ph->sigma_ref <= 0.) digits = 0;
  else
    {
      digits = -(int)floor(log(.5*ph->sigma_ref)/log(10.));
      if (digits < 0) digits = 0;
    }

#ifdef DEBUG
  fprintf(stdout, "\nlog entry:\n");
  fprintf(stdout, "%s %.*f %.*f %13.3f %5d %7d %s %13.3f %s\n",
	  timestring, 
	  digits, ph->ref, digits, ph->sigma_ref,
	  ph->sys, ph->tick, ph->freq, ph->sys_ok?"y":"n",
	  ph->cmos, ph->cmos_ok?"y":"n");
#endif /* DEBUG */

  lfile = fopen(log_path, "a+");
  if (!lfile)
    {
      fprintf(stderr, "cannot open %s for writing\n", log_path);
      return;
    }
  fprintf(lfile, "%s %.*f %.*f %13.3f %5d %7d %s %13.3f %s\n",
	  timestring, 
	  digits, ph->ref, digits, ph->sigma_ref,
	  ph->sys, ph->tick, ph->freq, ph->sys_ok?"y":"n",
	  ph->cmos, ph->cmos_ok?"y":"n");
  fclose(lfile);
}

/* convert a broken-down time representing UTC to calendar time
    representation (time_t), and return it.  As a side effect, set the
    tm_wday and tm_yday members of the broken-down time. (like mktime) */
time_t mkgmtime(struct tm *tp)
{
  time_t lt;			/* local time */
  long adj;
  struct tm u;
  static char timezone_name[]="UTC";

  lt = mktime(tp);
  if (lt == (time_t)(-1))
    return (time_t)(-1);
  adj = 131072;			/* greater than the number of seconds
				   per day (even when daylight savings
				   time ends) */
  lt -= adj;
  while (adj)
    {
      lt += adj;
      u = *gmtime(&lt);
      if (compare_tm(&u, tp) > 0)
	lt -= adj;
      adj /= 2;
    }
  u = *gmtime(&lt);
  if (compare_tm(&u, tp))
    return (time_t)(-1);
  tzname[0] = tzname[1] = timezone_name;
  return lt;
}

int compare_tm(struct tm *first, struct tm *second)
{
  if (first->tm_year < second->tm_year) return -1;
  if (first->tm_year > second->tm_year) return 1;
  if (first->tm_mon < second->tm_mon) return -1;
  if (first->tm_mon > second->tm_mon) return 1;
  if (first->tm_mday < second->tm_mday) return -1;
  if (first->tm_mday > second->tm_mday) return 1;
  if (first->tm_hour < second->tm_hour) return -1;
  if (first->tm_hour > second->tm_hour) return 1;
  if (first->tm_min < second->tm_min) return -1;
  if (first->tm_min > second->tm_min) return 1;
  if (first->tm_sec < second->tm_sec) return -1;
  if (first->tm_sec > second->tm_sec) return 1;
  return 0;
}

static void *xmalloc(int n)
{	void *p;
	p = xrealloc(NULL, n);
	return p;
}

static void *xrealloc(void *pv, int n)
{
  void *p;
  p = realloc(pv, n);
  if (!p){perror("adjtimex"); exit(1);}
  return p;
}

void review()
{
  int i, n, nmax = 0, digits;
  struct hack *ph, **hacks = NULL;
  double diff_ppm, sigma_ppm, cmos_time, sys_time, s0, s1, ref_time;
  time_t start, finish;
  char startstring[26], finishstring[26];
  double x[2], p[4], h[4], z[2], r[4], cmos_var, sys_var, ref_var;

  /* read all the previous time hacks in */
  sethackent();
  for (n = 0; (ph = gethackent()); n++)
    {
      if (nmax <= n)
	{
	  hacks = xrealloc(hacks, (nmax = 2*nmax + 4)*sizeof(struct hack *));
	}
      hacks[n] = xmalloc(sizeof(struct hack));
      *hacks[n] = *ph;
    }
  endhackent();

  if (n == 0)
    {
      printf("No previous clock comparison in log file\n");
      return;
    }

  /* compare cmos and system rates */
  printf(
"start                     finish                    days    sys - cmos (ppm)\n");
  for (i = 1; i < n; i++)
    {
      if (hacks[i]->sys_ok && 
	  hacks[i]->cmos_ok &&
	  hacks[i]->sys > hacks[i-1]->sys &&
	  hacks[i]->cmos > hacks[i-1]->cmos)
	{
	  sys_time = hacks[i]->sys - hacks[i-1]->sys;
	  cmos_time = hacks[i]->cmos - hacks[i-1]->cmos;
	  hacks[i]->relative_rate =
	    diff_ppm = 1.e6*(sys_time - cmos_time)*2/(sys_time + cmos_time)
	    - 100*(hacks[i]->tick - 10000) - hacks[i]->freq/SHIFT;
	  if (fabs(diff_ppm) > 10000.) /* agree within 1 percent? */
	    continue;
	  hacks[i]->valid = CMOS_VALID | SYS_VALID;
	  hacks[i]->relative_sigma =
	    sigma_ppm = 1.e6*RTC_JITTER*sqrt(2.)/(sys_time + cmos_time);
	  start = (time_t)hacks[i-1]->sys;
	  strcpy(startstring, ctime(&start));
	  finish = (time_t)hacks[i]->sys;
	  strcpy(finishstring, ctime(&finish));
	  digits = -(int)floor(log(.5*sigma_ppm)/log(10.));
	  if (digits < 0) digits = 0;
	  printf("%.24s  %.24s  %6.4f  %.*f +- %.*f\n",
		 startstring, finishstring, sys_time/SECONDSPERDAY,
		 digits, diff_ppm, digits, sigma_ppm);
	}
    }

  /* compare cmos and reference rates */
  printf(
"start                     finish                    days    cmos_error (ppm)\n");
  for (i = 1; i < n; i++)
    {
      if (hacks[i]->sigma_ref != 0 && 
	  hacks[i-1]->sigma_ref != 0 && 
	  hacks[i]->cmos_ok &&
	  hacks[i]->cmos > hacks[i-1]->cmos &&
	  hacks[i]->ref > hacks[i-1]->ref)
	{
	  ref_time = hacks[i]->ref - hacks[i-1]->ref;
	  cmos_time = hacks[i]->cmos - hacks[i-1]->cmos;
	  hacks[i]->cmos_rate =
	    diff_ppm = 1.e6*(cmos_time - ref_time)*2/(ref_time + cmos_time);
	  if (fabs(diff_ppm) > 10000.) /* agree within 1 percent? */
	    continue;
	  hacks[i]->valid |= CMOS_VALID | REF_VALID;
	  s0=hacks[i-1]->sigma_ref;
	  s1=hacks[i]->sigma_ref;
	  hacks[i]->cmos_sigma =
	    sigma_ppm = 1.e6*sqrt(s0*s0 + s1*s1)/ref_time;
	  start = (time_t)hacks[i-1]->sys;
	  strcpy(startstring, ctime(&start));
	  finish = (time_t)hacks[i]->sys;
	  strcpy(finishstring, ctime(&finish));
	  digits = -(int)floor(log(.5*sigma_ppm)/log(10.));
	  if (digits < 0) digits = 0;
	  printf("%.24s  %.24s  %6.4f  %1.*f +- %1.*f\n",
		 startstring, finishstring, ref_time/SECONDSPERDAY,
		 digits, diff_ppm, digits, sigma_ppm);
	}
    }

  /* compare sys and reference rates */
  printf(
"start                     finish                    days    sys_error (ppm)\n");
  for (i = 1; i < n; i++)
    {
      if (hacks[i]->sigma_ref != 0 && 
	  hacks[i-1]->sigma_ref != 0 && 
	  hacks[i]->sys_ok &&
	  hacks[i]->sys > hacks[i-1]->sys &&
	  hacks[i]->ref > hacks[i-1]->ref)
	{
	  ref_time = hacks[i]->ref - hacks[i-1]->ref;
	  sys_time = hacks[i]->sys - hacks[i-1]->sys;
	  hacks[i]->sys_rate =
	    diff_ppm = 1.e6*(sys_time - ref_time)*2/(ref_time + sys_time)
	    - 100*(hacks[i]->tick - 10000) - hacks[i]->freq/SHIFT;
	  if (fabs(diff_ppm) > 10000.) /* agree within 1 percent? */
	    continue;
	  hacks[i]->valid |= REF_VALID | SYS_VALID;
	  s0=hacks[i-1]->sigma_ref;
	  s1=hacks[i]->sigma_ref;
	  hacks[i]->sys_sigma =
	    sigma_ppm = 1.e6*sqrt(s0*s0 + s1*s1)/ref_time;
	  start = (time_t)hacks[i-1]->sys;
	  strcpy(startstring, ctime(&start));
	  finish = (time_t)hacks[i]->sys;
	  strcpy(finishstring, ctime(&finish));
	  digits = -(int)floor(log(.5*sigma_ppm)/log(10.));
	  if (digits < 0) digits = 0;
	  printf("%.24s  %.24s  %6.4f  %1.*f +- %1.*f\n",
		 startstring, finishstring, ref_time/SECONDSPERDAY,
		 digits, diff_ppm, digits, sigma_ppm);
	}
    }

  /* find least-squares solution incorporating all the data */

  p[0] = 1.e10; p[1] = 0.;
  p[2] = 0.;    p[3] = 1.e10;
  x[0] = 0.; x[1] = 0.;

  for (i = 1; i < n; i++)
    {
      switch(hacks[i]->valid)
	{
	case 0: 
	  break;
	case (CMOS_VALID | REF_VALID):
	  /* update only the first component of the state (cmos rate) */
	  h[0] = 1.; h[1] = 0.;
	  z[0] = hacks[i]->cmos_rate;
	  r[0] = hacks[i]->cmos_sigma; r[0] *= r[0];
	  kalman_update(x, 2, p, h, z, 1, r);
	  break;
	case (SYS_VALID | REF_VALID):
	  /* update only the second component of the state (system rate) */
	  h[0] = 0.; h[1] = 1.;
	  z[0] = hacks[i]->sys_rate;
	  r[0] = hacks[i]->sys_sigma; r[0] *= r[0];
	  kalman_update(x, 2, p, h, z, 1, r);
	  break;
	case (CMOS_VALID | SYS_VALID):
	  /* update the difference between the system and cmos rates */
	  h[0] = -1.; h[1] = 1.;
	  z[0] = hacks[i]->relative_rate;
	  r[0] = hacks[i]->relative_sigma; r[0] *= r[0];
	  kalman_update(x, 2, p, h, z, 1, r);
	  break;
	case (CMOS_VALID | SYS_VALID | REF_VALID):
	  /* This is the interesting case.  We have estimates of the
             cmos and system rates, but they are highly correlated
             because they contain the same errors in the reference
             times.  Thus, we know the *difference* between the cmos
             and system rates much better than we know either of them
             independently.  The r matrix describes the
             correlation. */
	  h[0] = 1.; h[1] = 0.;
	  h[2] = 0.; h[3] = 1.;
	  ref_var = hacks[i]->cmos_sigma*hacks[i]->cmos_sigma;
	  cmos_var = hacks[i]->relative_sigma; cmos_var *= cmos_var;
	  sys_var = cmos_var;
	  r[0] = ref_var + 2.*cmos_var;
	  r[1] = r[2] = ref_var;
	  r[3] = ref_var + 2.*sys_var;
	  z[0] = hacks[i]->cmos_rate;
	  z[1] = hacks[i]->sys_rate;
	  kalman_update(x, 2, p, h, z, 2, r);
	  break;
	}
    }

  sigma_ppm = sqrt(p[0]);
  digits = -(int)floor(log(.5*sigma_ppm)/log(10.));
  if (digits < 0) digits = 0;
  printf("least-squares solution:\n"
	 "  cmos_error = %.*f +- %.*f ppm",
	 digits, x[0], digits, sigma_ppm);
  if (sigma_ppm < 100)
    printf("      suggested adjustment = %6.4f sec/day\n",
	   -x[0]*SECONDSPERDAY/1.e6);
  else
    printf("      (no suggestion)\n");

  sigma_ppm = sqrt(p[3]);
  digits = -(int)floor(log(.5*sigma_ppm)/log(10.));
  if (digits < 0) digits = 0;
  printf("   sys_error = %.*f +- %.*f ppm",
	 digits, x[1], digits, sigma_ppm);
  if (sigma_ppm < 100)
    {
      long tick_delta = 0;
      double error_ppm;
      
      error_ppm = x[1];
      if (error_ppm > 100)
	tick_delta = -(error_ppm + 50)/100;
      else if (error_ppm < -100)
	tick_delta = (-error_ppm + 50)/100;
      error_ppm += tick_delta*100;
      printf("      suggested tick = %ld  freq = %1.0f\n",
	     10000 + tick_delta, -error_ppm*SHIFT);
    }
  else
    printf("      (no suggestion)\n");
  printf(
"note: clock variations and unstated data errors may mean that the\n"
"least squares solution has a bigger uncertainty than estimated here\n");

  for (i = 0; i < n; i++)
    free(hacks[i]);
  free(hacks);
}

/* Perform one update on a discrete linear Kalman filter.  z is a
   measurement related to the state x by
       z = h x + v
   where v is the measurement error, normally distributed, with
   covariance r.

   Because of the size of the temporary arrays, this particular
   implementation is restricted to 2 states.  There are no provisions
   here for propagating the state or its covariance between updates,
   because it is not required in this case (i.e. the state transition
   matrix is a unit matrix).  See, for example: Blackman, "Multitarget
   Tracking with Radar Applications" */
void kalman_update(double *x,	/* state vector */
		   int xr,	/* rows in x (must be 1 or 2) */
		   double *p,	/* covariance matrix for x (has xr
				   rows and columns) */
		   double *h,	/* transforms from state space to
				   measurement space (has zr rows
				   and xr columns) */
		   double *z,	/* measurement vector */
		   int zr,	/* rows in z (must be 1 or 2) */
		   double *r)	/* covariance matrix for z (has zr
				   rows and columns) */
{
  static double k[4], num[4], denom[4], v[4], w[4];
  int pr=xr, pc=xr, 
    hr=zr, hc=xr, 
    rr=zr, rc=zr,
    kr=xr, kc=zr,
    nr=xr, nc=zr,
    dr=zr, dc=zr,
    vr=xr, vc=1,
    wr=zr, wc=1;

  /* find the Kalman gain k:
     k = p h' /(h p h' + r) */
  mat_mul_nt(p,pr,pc, h,hr,hc, num,nr,nc);
  mat_similarity(h,hr,hc, p,pr,pc, denom,dr,dc);
  mat_add(denom,dr,dc, r,rr,rc, denom,dr,dc);
  if (sym_factor(denom,dr,dc, denom,dr,dc))
    return;			/* failure - singular */
  sym_rdiv(num,nr,nc, denom,dr,dc, k,kr,kc);

  /* update the state x:
     x <- x + k (z - h x) */
  mat_mul(h,hr,hc, x,xr,1, w,wr,wc);
  mat_sub(z,zr,1, w,wr,wc, w,wr,wc);
  mat_mul(k,kr,kc, w,wr,wc, v,vr,vc);
  mat_add(x,xr,1, v,vr,vc, x,xr,1);

  /* update the covariance p:
     p <- (I - k h) p */
  mat_one(v,xr,xr);
  mat_mul(k,kr,kc, h,hr,hc, w,xr,xr);
  mat_sub(v,xr,xr, w,xr,xr, w,xr,xr);
  mat_mul(w,xr,xr, p,xr,xr, v,xr,xr);
  mat_copy(v,xr,xr, p,xr,xr);
}
