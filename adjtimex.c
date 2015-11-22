/*
#define DEBUG

	adjtimex - display or set the kernel time variables


	AUTHORS
		ssd at nevets.oau.org (Steven S. Dick)
		jrvz at comcast.net (Jim Van Zandt)

		TODO: review code controlled by DEBUG and possibly
		change control to verbose flag.
*/

#define _GNU_SOURCE		/* strptime is a GNU extension */

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
#include <signal.h>
#include <unistd.h>
#include <utmp.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <linux/rtc.h>

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
static int port_fd = -1;	/* used to access the RTC via /dev/port I/O */
#endif
static char *cmos_device;	/* filename of the RTC device */
static int cmos_fd = -1;

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

/* command line option variables */

static int using_dev_rtc = -1;	/* 0 = not using /dev/rtc
				 1 = using /dev/rtc
				-1 = will use /dev/rtc if available,
				     but have not tried to open it yet */
static int nointerrupt = 0;	/* 0 = try to detect RTC tick via interrupt
				   1 = via another method */

int adjusting = 0;
int force_adjust;
int comparing = 0;
int logging = 0;
int reviewing = 0;
int resetting = 0;		/* nonzero if need to call
				   reset_time_status() */
int interval = 10;
int count = 8;
int marked;
int universal = 0;
int verbose = 0;
int watch;			/* nonzero if time specified on command line */
int undisturbed_sys = 0;
int undisturbed_cmos = 0;
char *log_path = LOG_PATH;

char *timeserver;		/* points to name of timeserver */

enum {HELP=131};

struct option longopt[]=
{
  {"adjust", 2, NULL, 'a'},
  {"force-adjust", 0, &force_adjust, 1},
  {"compare", 2, NULL, 'c'},
  {"log", 2, NULL, 'l'},
  {"esterror", 1, NULL, 'e'},
  {"frequency", 1, NULL, 'f'},
  {"host", 1, NULL, 'h'},
  {"help", 0, NULL, HELP},
  {"interval", 1, NULL, 'i'},
  {"maxerror", 1, NULL, 'm'},
  {"offset", 1, NULL, 'o'},
  {"print", 0, NULL, 'p'},
  {"review", 2, NULL, 'r'},
  {"singleshot", 1, NULL, 's'},
  {"status", 1, NULL, 'S'},
  {"reset", 0, NULL, 'R'},
  {"timeconstant", 1, NULL, 'T'},
  {"tick", 1, NULL, 't'},
  {"utc", 0, NULL, 'u'},
  {"directisa", 0, NULL, 'd'},
  {"nointerrupt", 0, NULL, 'n'},
  {"version", 0, NULL, 'v'},
  {"verbose", 0, NULL, 'V'},
  {"watch", 0, NULL, 'w'},
  {0,0,0,0}
};

static void usage(void);
static inline void outb (short port, char val);
static inline void outb (short port, char val);
static inline unsigned char inb (short port);
static void cmos_init ();
static void cmos_init_directisa ();
static inline int cmos_read_bcd (int addr);
static void cmos_read_time (time_t *cmos_timep, double *sysp);
static void busywait_uip_fall(struct timeval *timestamp);
static void busywait_second_change(struct tm *cmos, struct timeval *timestamp);
static void compare(void);
static void failntpdate();
static void reset_time_status(void);
static struct cmos_adj *get_cmos_adjustment(void);
static void log_times(void);
static int valid_system_rate(double ftime_sys, double ftime_ref, double sigma_ref);
static int valid_cmos_rate(double ftime_cmos, double ftime_ref, double sigma_ref);
static void sethackent(void);
static void endhackent(void);
static struct hack *gethackent(void);
static void puthackent(struct hack *ph);
static time_t mkgmtime(struct tm *tp);
static int compare_tm(struct tm *first, struct tm *second);
static void *xmalloc(int n);
static void *xrealloc(void *pv, int n);
static void review(void);
static void kalman_update(double *x, int xr, double *p, double *h,
			  double *z, int zr, double *r);


void
usage(void)
{
  char msg[]=
"\n"
"Usage: adjtimex  [OPTION]... \n"
"Mandatory or optional arguments to long options are mandatory or optional\n"
"for short options too.\n"
"\n"
"Get/Set Kernel Time Parameters:\n"
"       -p, --print               print values of kernel time variables\n"
"       -t, --tick val            set the kernel tick interval in usec\n"
"       -f, --frequency newfreq   set system clock frequency offset\n"
"       -s, --singleshot adj      slew the system clock by adj usec\n"
"       -S, --status val          set kernel clock status\n"
"       -R, --reset               reset status after setting parameters\n"
"                                 (needed for early kernels)\n"
"       -o, --offset adj          add a time offset of adj usec\n"
"       -m, --maxerror val        set maximum error (usec)\n"
"       -e, --esterror val        set estimated error (usec)\n"
"       -T, --timeconstant val    set phase locked loop time constant\n"
"       -a, --adjust[=count]      set system clock parameters per CMOS \n"
"                                 clock or (with --review) log file\n"
"       --force-adjust            override +-500 ppm sanity check\n"
"\n"
"Estimate Systematic Drifts:\n"
"       -c, --compare[=count]     compare system and CMOS clocks\n"
"       -i, --interval tim        set clock comparison interval (sec)\n"
"       -l, --log[=file]          log current times to file\n"
"       -h, --host timeserver     query the timeserver\n"
"       -w, --watch               get current time from user\n"
"       -r, --review[=file]       review clock log file, estimate drifts\n"
"       -u, --utc                 the CMOS clock is set to UTC\n"
"       -d, --directisa           access the CMOS clock directly\n"
"       -n, --nointerrupt         bypass the CMOS clock interrupt access method\n"
"\n"
"Informative Output:\n"
"           --help                print this help, then exit\n"
"       -v, --version             print adjtimex program version, then exit\n"
"       -V, --verbose             increase verbosity\n"
;

  fputs(msg, stdout);
  exit(0);
}

/* return apparent value of USER_HZ in HZ, minimum nominal and maximum
   values for tick in TICK_MIN TICK_MID and TICK_MAX, and maximum
   frequency offset in MAXFREQ */
static 
void probe_time(int *hz, int *tick_min, int *tick_mid, int *tick_max,
		long *maxfreq)
{
  struct timex txc;
  int tick_orig, tick_lo, tick_try, tick_hi, i;

  txc.modes = 0;
  adjtimex(&txc);		/* fetch original value */
  txc.modes = ADJ_TICK;
  if (adjtimex(&txc) == -1) 	/* try setting with no change */
    {
      perror("adjtimex");
      exit(1);
    }

  *maxfreq = txc.tolerance;
  tick_orig = tick_hi = txc.tick;
  tick_lo = tick_hi*2/3;
  for (i = 0; i < 15; i++)
    {			/* conduct binary search for minimum
			   accepted tick value */
//      printf(" %d < minimum accepted tick value <= %d\n", tick_lo, tick_hi);
      txc.tick = tick_try = (tick_lo + tick_hi)/2;
      txc.modes = ADJ_TICK;
      if (adjtimex(&txc) == -1) tick_lo = tick_try;
      else tick_hi = tick_try;
    }
  *tick_min = tick_hi;
  tick_lo = tick_orig;
  tick_hi = tick_lo*4/3;
  for (i = 0; i < 15; i++)
    {			/* conduct binary search for maximum
			   accepted tick value */
//      printf(" %d <= maximum accepted tick value < %d\n", tick_lo, tick_hi);
      txc.tick = tick_try = (tick_lo + tick_hi)/2;
      txc.modes = ADJ_TICK;
      if (adjtimex(&txc) == -1) tick_hi = tick_try;
      else tick_lo = tick_try;
    }
  *tick_max = tick_lo;
  *tick_mid = (*tick_min + *tick_max)/2;
  *hz = (900000/ *tick_min + 1100000/ *tick_max)/2;

  txc.tick = tick_orig;
  txc.modes = ADJ_TICK;
  adjtimex(&txc);		/* reset to original value */
}

int
main(int argc, char *argv[])
{
    int ret, saveerr, changes;
    extern char *optarg;
    int c;

    txc.modes = 0;

    while((c = getopt_long_only(argc, argv, 
				"a::c::l::e:f:h:i:m:o:pr::s:S:RT:t:udvVw", 
				longopt, NULL)) != -1)
      {
	switch(c)
	  {
	  case 0: break;	/* options that just set a variable
				   are handled in longopt */
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
	  case 'S':
	    txc.status = atol(optarg);
	    txc.modes |= ADJ_STATUS;
	    break;
	  case 'R': resetting = 1; break;
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
	  case 'd':
	    using_dev_rtc = 0;
	    break;
	  case 'n':
	    nointerrupt = 1;
	    break;
	  case 'v':
	    {
	      printf("adjtimex %s\n", VERSION);
	      exit(0);
	    }
	  case 'V':
	    verbose++;
	    break;
	  case 'w':
	    watch = 1;
	    logging = 1;
	    break;
	  case HELP:
	    usage();
	    break;
	  case '?':
	  default:
	    fprintf(stderr, "Unrecognized option %s\nFor valid "
		    "options, try 'adjtimex --help'\n", argv[optind-1]);
	    exit(1);
	  }
	}

    changes = txc.modes;

    if (count <= 0 ) {
      fprintf(stderr, "loop count out of range\n");
      exit(1);
    }

    if (reviewing)
      {
	review();
	exit(0);
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
	{
	  int hz, tick_min, tick_mid, tick_max;
	  long maxfreq;
	  probe_time(&hz, &tick_min, &tick_mid, &tick_max, &maxfreq);
	  
	  printf("for this kernel:\n"
		 "   USER_HZ = %d (nominally %d ticks per second)\n"
		 "   %d <= tick <= %d\n"
		 "   %ld <= frequency <= %ld\n",
		 hz, hz, tick_min, tick_max, -maxfreq, maxfreq);
	}
	exit(1);
    }
    if (changes && resetting)
      reset_time_status();

    return 0;
}

static inline void
outb (short port, char val)
{
#ifdef USE_INLINE_ASM_IO
  __asm__ volatile ("out%B0 %0,%1"::"a" (val), "d" (port));

#else
  lseek (port_fd, port, 0);
  write (port_fd, &val, 1);
#endif
}

static inline unsigned char
inb (short port)
{
  unsigned char ret;

#ifdef USE_INLINE_ASM_IO
  __asm__ volatile ("in%B0 %1,%0":"=a" (ret):"d" (port));

#else
  lseek (port_fd, port, 0);
  read (port_fd, &ret, 1);
#endif
  return ret;
}

/*
 * Main initialisation of CMOS clock access methods, for all modes.
 * Set the global variable cmos_device to the first available RTC device
 * driver filename (/dev/rtc, /dev/rtc0, etc.), and set cmos_fd to
 * a file descriptor for it.
 * Failing that, select and initialize direct I/O ports mode.
 */
static
void cmos_init ()
{
/*
 following explanation taken from hwclock sources:
 /dev/rtc is conventionally chardev 10/135
 ia64 uses /dev/efirtc, chardev 10/136
 devfs (obsolete) used /dev/misc/... for miscdev
 new RTC framework + udev uses dynamic major and /dev/rtc0.../dev/rtcN
*/
  char *fls[] = {
#ifdef __ia64__
    "/dev/efirtc",
    "/dev/misc/efirtc",
#endif
    "/dev/rtc",
    "/dev/rtc0",
    "/dev/misc/rtc",
    NULL
  };
  char **p=fls;

  if (using_dev_rtc < 0)
    {
      while ((cmos_device=*p++))
	{
	  cmos_fd = open (cmos_device, O_RDONLY);
	  if (cmos_fd >= 0)
	    {
	      if (verbose)
		fprintf (stdout, "opened %s for reading\n", cmos_device);
	      using_dev_rtc = 1;
	      return;
	    }
	  if (verbose)
	    {
	      int saveerr=errno;
	      fprintf (stdout, "adjtimex: cannot open %s for reading\n", 
		       cmos_device);
	      errno = saveerr;
	      perror("adjtimex");
	    }
	}

      using_dev_rtc = 0;
    }
  else if (using_dev_rtc > 0)
    return;

  /* otherwise do direct I/O */
  cmos_init_directisa();
}

/*
 * Initialise CMOS clock access method, only for direct I/O ports mode.
 * Set the global variable port_fd to a file descriptor for /dev/port.
 * This function can safely be called repeatedly (does nothing the
 * second and following times).
 */
static
void cmos_init_directisa ()
{
#ifdef USE_INLINE_ASM_IO
  if (verbose)
    fprintf (stdout, "using I/O ports\n");

  if (ioperm (0x70, 2, 1))
    {
      fprintf (stderr, "adjtimex: unable to get I/O port access\n");
      exit (1);
    }
#else
  if (port_fd >= 0)	/* already initialised */
    return;

  if (verbose)
    fprintf (stdout, "using /dev/port I/O\n");

  if (port_fd < 0)
    port_fd = open ("/dev/port", O_RDWR);
  if (port_fd < 0)
    {
      perror ("adjtimex: unable to open /dev/port read/write");
      exit (1);
    }
  if (verbose)
    fprintf (stdout, "opened /dev/port for reading\n");

  if (lseek (port_fd, 0x70, 0) < 0 || lseek (port_fd, 0x71, 0) < 0)
    {
      perror ("adjtimex: unable to seek port 0x70 in /dev/port");
      exit (1);
    }
#endif
}

#define CMOS_READ(addr)      ({outb(0x70,(addr)|0x80); inb(0x71);})

static inline int
cmos_read_bcd (int addr)
{
  int b;

  b = CMOS_READ (addr);
  return (b & 15) + (b >> 4) * 10;
}

static int timeout;	/* An alarm signal has occurred */

static void
alarm_handler (int const dummy) {
  timeout = 1;
}

/*
 * starts (or cancels) a 2 second timeout period
 * the global boolean timeout indicates that the timeout has been reached
 */
static void
alarm_timeout (int onoff) {
  static struct sigaction oldaction;

  if (onoff)
    {
      struct sigaction action;

      action.sa_handler = &alarm_handler;
      sigemptyset(&action.sa_mask);
      action.sa_flags = 0;
      sigaction(SIGALRM, &action, &oldaction);	/* Install our signal handler */

      timeout = 0;	/* reset global timeout flag */
      alarm(2);		/* generate SIGALARM 2 seconds from now */
    }
  else
    {
      alarm(0);					/* cancel alarm */
      sigaction(SIGALRM, &oldaction, NULL);	/* remove our signal handler */
    }
}


/* return CMOS time in CMOS_TIMEP and sytem time in SYSP.  cmos_init()
   must have been called before this function. */
static void
cmos_read_time (time_t *cmos_timep, double *sysp)
{
  int rc;
  struct tm tm;
  static int summertime_correction=0;
  static int sanity_checked=0;
  time_t cmos_time;
  struct timeval now;
  int noint_fallback = 1;	/* detect tick by 0 => uip, 1 => time change */
  int got_tick = 0;
  int got_time = 0;
  int saveerr;
  int type_uie, count;
  double update_delay;

  if (using_dev_rtc > 0)	/* access the CMOS clock thru /dev/rtc */
    {
      if (!nointerrupt)		/* get RTC tick via update interrupt */
	{
	  struct timeval before;

	  gettimeofday(&before, NULL);

	  /* Ordinarily, reading /dev/rtc does not wait until the
   beginning of the next second.  It only returns the current timer
   value, so it's only accurate to 1 sec which isn't good enough for
   us.  I see this comment in drivers/char/rtc.c, function
   rtc_get_rtc_time(), in the kernel sources:

	 * read RTC once any update in progress is done. The update
	 * can take just over 2ms. We wait 10 to 20ms. There is no need to
	 * to poll-wait (up to 1s - eeccch) for the falling edge of RTC_UIP.
	 * If you need to know *exactly* when a second has started, enable
	 * periodic update complete interrupts, (via ioctl) and then 
	 * immediately read /dev/rtc which will block until you get the IRQ.
	 * Once the read clears, read the RTC time (again via ioctl). Easy. 
   */
	  ioctl (cmos_fd, RTC_PIE_OFF, NULL); /* disable periodic interrupts */
	  rc = ioctl (cmos_fd, RTC_UIE_ON, NULL); /* enable update
						     complete interrupts */
	  if (rc == -1)	/* no interrupts? fallback to busywait */
	    {
	      if (verbose)
		fprintf(stdout, 
			"%s doesn't allow user access to update interrupts\n"
			" - using busy wait instead\n", cmos_device);
	      nointerrupt = 1;
	    }
 	  else		/* wait for update-ended interrupt */
	    {
	      unsigned long interrupt_info;

	      if (verbose)
		fprintf (stdout, "waiting for CMOS update-ended interrupt\n");

	      do {
		{
		  /* avoid blocking even if /dev/rtc never becomes readable */
		  fd_set readfds;
		  struct timeval tv;
		  int retval;
		  
		  FD_ZERO(&readfds);
		  FD_SET(cmos_fd, &readfds);
		  tv.tv_sec=2; tv.tv_usec=0; /* wait up to 2 sec */
		  retval = select(cmos_fd+1, &readfds, NULL, NULL, &tv);
		  if (retval <= 0)
		    {
		      ioctl (cmos_fd, RTC_UIE_OFF, NULL); /* disable update complete
							     interrupts */
		      if (retval == -1)
			perror("select()");
		      if (!retval)
			if (verbose)
			  fprintf(stdout,
				  "timeout waiting for a CMOS update interrupt from %s\n"
				  " - using busy wait instead\n", cmos_device);
		      nointerrupt = 1;
		      goto directisa;
		    }
		}
		rc = read(cmos_fd, &interrupt_info, sizeof(interrupt_info));
		saveerr = errno;
		gettimeofday(&now, NULL);

		if (rc == -1)
		  {
		    /* no timeout, but read(/dev/rtc) failed for another reason */
		    char message[128];
		    snprintf(message, sizeof(message),
			     "adjtimex: "
			     "read() from %s to wait for clock tick failed",
			     cmos_device);
		    perror(message);
		    exit(1);
		  }

		type_uie = (int)(interrupt_info & 0x80);
		count = (int)(interrupt_info >> 8);
	      } while (((type_uie == 0) || (count > 1)) && !nointerrupt);
	      /* The low-order byte holds
		 the interrupt type.  The first read may succeed
		 immediately, but in that case the byte is zero, so we
		 know to try again. If there has been more than one
		 interrupt, then presumably periodic interrupts were
		 enabled.  We need to try again for just the update
		 interrupt.  */
	      
	      update_delay = (now.tv_sec + .000001*now.tv_usec) -  
		(before.tv_sec + .000001*before.tv_usec);

	      if ((type_uie) && (count == 1) && (update_delay > .001))
		got_tick = 1;
	      else
		{
		  if (verbose)
		    fprintf(stdout, "CMOS interrupt with status "
			    "0x%2x came in only %8.6f sec\n"
			    " - using busy wait instead\n",
			    (unsigned int)interrupt_info&0xff, update_delay);
		  nointerrupt = 1;
		}
	    }	    /* end of successful RTC_UIE_ON case */
	}	/* end of interrupt tryouts */


      /* At this stage, we either just detected the clock tick via an
	 update interrupt, or detected nothing yet (interrupts were
	 bypassed, unavailable, or timeouted). Fallback to busywaiting
	 for the tick. */

    directisa:
      if (!got_tick)
	{
	  if (noint_fallback)
	    {
	      busywait_second_change(&tm, &now);
	      got_time = 1;
	    }
	  else
	    busywait_uip_fall(&now);
	  got_tick = 1;
	}


      /* At this stage, we just detected the clock tick, by any method.
	 Now get this just beginning RTC second, unless we already have it */

      if (!got_time)
	{
	  rc = ioctl (cmos_fd, RTC_RD_TIME, &tm);

	  /* RTC_RD_TIME can fail when the device driver detects
	     that the RTC isn't running or contains invalid data.
	     Such failure has been detected earlier, unless: We used
	     noint_fallback=1 to get busywait_uip_fall() as fallback.
	     Or: UIE interrupts do beat, but RTC is invalid. */
	  if (rc == -1)
	    {
	      char message[128];
	      snprintf(message, sizeof(message),
			"adjtimex: "
			"ioctl(%s, RTC_RD_TIME) to read the CMOS clock failed",
			cmos_device);
	      perror(message);
	      exit(1);
	    }
	}


      /* disable update complete interrupts
	 It could seem more natural to do this above, just after we
	 actually got the interrupt. But better do it here at the end,
	 after all time-critical operations including the RTC_RD_TIME. */
      ioctl (cmos_fd, RTC_UIE_OFF, NULL);
    }
  else		/* access the CMOS clock thru I/O ports */
    {
      /* The "do" loop is "low-risk programming" */
      /* In theory it should never run more than once */
      do
	{
	  busywait_uip_fall(&now);
	  tm.tm_sec = cmos_read_bcd (0);
	  tm.tm_min = cmos_read_bcd (2);
	  tm.tm_hour = cmos_read_bcd (4);
	  tm.tm_wday = cmos_read_bcd(6)-1;/* RTC uses 1 - 7 for day of the week, 1=Sunday */
	  tm.tm_mday = cmos_read_bcd (7);
	  tm.tm_mon = cmos_read_bcd(8)-1; /* RTC uses 1 base */
	  /* we assume we're not running on a PS/2, where century is
	     in byte 55 */
	  tm.tm_year = cmos_read_bcd(9)+100*cmos_read_bcd(50)-1900;
	} while (tm.tm_sec != cmos_read_bcd (0));
    }
  tm.tm_isdst = -1;		/* don't know whether it's summer time */

  
  if (universal)
    cmos_time = mkgmtime(&tm);
  else
    cmos_time = mktime(&tm);

  cmos_time += summertime_correction;
  
  if (verbose)
    printf ("CMOS time %s (%s) = %ld\n", asctime (&tm),
	    universal?"assuming UTC":
	    (summertime_correction?"assuming local time with summer time adjustment":
	     "assuming local time without summer time adjustment"),
	    cmos_time);
  
  if (!sanity_checked)
    {
      /* There are clues to whether the CMOS clock is set to
	 summer time, which could be used as suggested by Alain
	 Guibert <alguibert at free.fr>:
	 
	 Since version 2.5, hwclock records CMOS timezone UTC or
	 LOCAL as 1st item of 3rd line of /etc/adjtime. If it's
	 "UTC", take offset 0 as if --utc was used. Otherwise: Since
	 version 2.20, hwclock records CMOS timezone offset in 3rd
	 item of 3rd line of /etc/adjtime. If it's there, take
	 it. Otherwise if last adjustment date (2nd item of 1st line)
	 exists and is not zero, take timezone offset at this last
	 write time. Otherwise take the possibly false current
	 offset.
	 
	 However, /etc/adjtime cannot be relied on.  The user
	 might have booted Windows, which could have adjusted the
	 CMOS clock (including a summer time correction) without
	 updating /etc/adjtime.  Instead, we use a heuristic: if
	 the CMOS and system times differ by more than six
	 minutes, try shifting the CMOS time by some multiple of
	 one hour.
      */
      if (now.tv_sec<cmos_time)
	summertime_correction = -((cmos_time-now.tv_sec+6*60)/(60*60))*(60*60);
      else
	summertime_correction = (((now.tv_sec-cmos_time+6*60)/(60*60))*(60*60));
      if (abs(summertime_correction) > 13*60*60)
	{
	  printf("WARNING: CMOS time %ld differs from system time %ld by %3.2f hours\n",
		 cmos_time, now.tv_sec, (summertime_correction)/3600.);
	  if(logging)
	    {
	      printf("logging suppressed\n");
	      logging=0;
	    }
	}
      if (verbose && summertime_correction)
	printf("adjusting CMOS times by %d hour\n", 
	       summertime_correction/(60*60));
      cmos_time += summertime_correction;	/* assume start/end of
						   summer time or local
						   time/UTC difference */
      if (abs(cmos_time-now.tv_sec)>6*60) /* still different by more than 6 min? */
	{
	  if (cmos_time>now.tv_sec)
	    printf("WARNING: CMOS time is %3.2f min ahead of system clock\n",
		   (cmos_time-now.tv_sec)/60.);
	  else
	    printf("WARNING: CMOS time is %3.2f min behind system clock\n",
		   (now.tv_sec-cmos_time)/60.);
	  if(logging)
	    {
	      printf("logging suppressed\n");
	      logging=0;
	    }
	}
      sanity_checked=1;
    }
  *cmos_timep=cmos_time;
  *sysp = now.tv_sec + .000001*now.tv_usec;
}


/*
 * busywait for UIP fall and timestamp this event
 *
 * Maintainance note: In order to detect its fall, we first have to
 * detect the update-in-progress flag up state. This UIP up state lasts
 * for 2 ms each second. Detecting a 2 ms event, when other processes
 * can steal us one or several 10 ms timeslices, this is something that
 * can very easily fail. We *can* miss a second tick, or even several in
 * a row, under heavy system load. Missing ticks is not a severe problem
 * for adjtimex, as long as we timestamp accurately the tick we'll
 * finally catch. However there is a timeout issue: we can't just arm an
 * alarm_timeout() for 2 seconds, as when waiting for UIE interrupts and
 * in busywait_second_change(), because it would make us hard fail after
 * only one or two missed ticks.
 */
static void
busywait_uip_fall(struct timeval *timestamp)
{
  long i;

  /*
   * Initialise direct I/O access mode.
   * This may have been already done previously by the main
   * initialisation cmos_init(), if the CMOS access mode is direct I/O.
   * We init it here again, to make busywait_uip_fall() usable in any
   * other CMOS access modes (/dev/rtc).
   */
  cmos_init_directisa();

  if (verbose>1)
    fprintf (stdout, "  waiting for CMOS update-in-progress fall\n");

  /* read RTC exactly on falling edge of update-in-progress flag */
  /* Wait for rise.... (may take up to 1 second) */
  
  for (i = 0; i < 10000000; i++)
    if (CMOS_READ (10) & 0x80)
      break;
  
  /* Wait for fall.... (must try at least 2.228 ms) */
  
  for (i = 0; i < 1000000; i++)
    if (!(CMOS_READ (10) & 0x80)) {
      gettimeofday(timestamp, NULL);
      break;
    }
}

/*
 * Busywait for a change in RTC time and timestamp this event.
 * cmos_init() must have been called before, and the selected
 * access method must be using_dev_rtc=1.
 *
 * Important note: an ioctl(RTC_RD_TIME) call that happens while the RTC
 * is updating itself (UIP up, a 2 milliseconds long event) will block.
 * Properly block until UIP release on recent Linux kernels since 2.6.13.
 * However all older Linux kernels had a misfeature: they blocked much
 * longer than necessary, up to 20 ms longer in the worst case.
 * The method used here cannot detect precisely the CMOS clock tick on
 * such older kernels. It would result in a random delay, the timestamp
 * being between 8 and 18 ms late. Hell: that's 3 orders of magnitude
 * worse than the accuracy expected from this function.
 */
static void
busywait_second_change(struct tm *cmos, struct timeval *timestamp)
{
  struct tm begin;
  int rc;

  if (verbose)
    fprintf (stdout, "waiting for CMOS time change\n");

  alarm_timeout(1);	/* arm a 2 seconds timeout */

  /* pick a reference time */
  rc = ioctl (cmos_fd, RTC_RD_TIME, &begin);

  /* RTC_RD_TIME can fail when the device driver detects
     that the RTC isn't running or contains invalid data */
  if (rc == -1)
    {
      char message[128];
      snprintf(message, sizeof(message),
		"adjtimex: "
		"ioctl(%s, RTC_RD_TIME) to read the CMOS clock failed",
		cmos_device);
      perror(message);
      exit(1);
    }

  /* loop until time changes */
  do
    {
      ioctl (cmos_fd, RTC_RD_TIME, cmos);
    }
  while ((cmos->tm_sec == begin.tm_sec) && !timeout);
  gettimeofday(timestamp, NULL);

  alarm_timeout(0);	/* disable timeout */

  /* Sometimes the RTC isn't running, but the device driver didn't
     notice. Then the RTC_RD_TIME call succeeds, but provides us some
     static wrong time. That's why we needed an alarm timeout */

  /* timeout without a change? */
  if (cmos->tm_sec == begin.tm_sec)
    {
      fprintf(stderr,
		"adjtimex: timeout waiting for CMOS time change on %s\n"
		"The CMOS clock appears to be stopped:\n"
		"Please try to set and restart it with hwclock --systohc\n",
		cmos_device);
      exit (1);
    }

}

static inline void 
xusleep(long microseconds)
{
  struct timespec ts;

  ts.tv_sec = microseconds/1000000;
  ts.tv_nsec = (microseconds - ts.tv_sec*1000000) * 1000;

  while (nanosleep (&ts, &ts) < 0)
    continue;
}

/* compare the system and CMOS clocks.  If "adjusting" is nonzero,
   adjust sytem time to match the CMOS clock. */
void
compare()
{
  struct timex txc;
  time_t cmos_time;
  double cmos_sec, system_sec, dif, dif_prev = 0.;
  struct cmos_adj *pca = get_cmos_adjustment();
  double cmos_adjustment;
  int loops = 0;
  extern char *optarg;
  int wrote_to_log = 0;
  int hz, tick_min, tick_mid, tick_max;
  long maxfreq;
  struct timeval tv_dummy;

  /* dummy training call: the next important timestamp will be more accurate */
  gettimeofday(&tv_dummy, NULL);

  probe_time(&hz, &tick_min, &tick_mid, &tick_max, &maxfreq);

#ifdef DEBUG
  /*
cmos clock last adjusted at Tue Aug 26 11:43:57 1997 (= 872610237)
          current cmos time Tue Aug 26 21:27:05 1997 EDT (= 872645225)
*/
  {
    struct tm bdt;
    if (universal)
      {
	bdt = *gmtime(&pca->ca_adj_time);
	(void)mkgmtime(&bdt);	/* set tzname */
      }
    else
      {
	bdt = *localtime(&pca->ca_adj_time);
	(void)mktime(&bdt);	/* set tzname */
      }
    printf ("cmos clock last adjusted %.24s %s "
	    "(= %ld)\n", 
	    ctime(&pca->ca_adj_time), tzname[tm.tm_isdst?1:0], (long) pca->ca_adj_time);
  }
#endif

  cmos_init ();

  while (count != 0)
    {
      if (count > 0) count--;

      cmos_read_time (&cmos_time, &system_sec);
			  

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
      tm=*gmtime(cmos_time);
      printf ("       current cmos time %.24s %s (= %ld)\n", 
	      asctime(&tm), tzname[tm.tm_isdst?1:0], (long) cmos_time);
#endif
      cmos_adjustment = ((double) (cmos_time - pca->ca_adj_time))
	* pca->ca_factor / SECONDSPERDAY
	+ pca->ca_remainder;
      cmos_sec = cmos_time + cmos_adjustment;
#ifdef DEBUG
      printf (
"           time since last adjustment %10.6f days (= %9d sec)\n",
	      (int) (cmos_time - pca->ca_adj_time) / (double)SECONDSPERDAY,
	      (int) (cmos_time - pca->ca_adj_time));
      printf (
"                               factor %10.6f sec/day\n",
	      pca->ca_factor);
      printf (
"                           adjustment %10.6f + %7.6f = %7.6f sec\n",
	       ((double) (cmos_time - pca->ca_adj_time))*pca->ca_factor/SECONDSPERDAY,
	       pca->ca_remainder, cmos_adjustment);
#endif
      dif = system_sec - cmos_sec;

      txc.modes = 0;
      if (adjtimex (&txc) < 0) {perror ("adjtimex"); exit(1);}
/*
                                       --- current ---   -- suggested --
cmos time     system-cmos  error_ppm   tick      freq    tick      freq
1094939320  -14394.974188
1094939330  -14394.971203      298.5  10001   1290819
1094939340  -14394.968203      300.0  10001   1290819    9998   1289097
*/

      if (! marked++ )
	{
	  if (interval)
	    printf (
"                                      --- current ---   -- suggested --\n"
"cmos time     system-cmos  error_ppm   tick      freq    tick      freq\n");
	  else
	    printf (
"cmos time     system-cmos  error_ppm   tick      freq\n");
	}
      printf ("%10ld  %13.6f",
	      (long) cmos_sec,
	      dif);
      if (++loops > 1)
	{			/* print difference in rates */
#define SHIFT (1<<16)
	  double second_diff, error_ppm;
	  second_diff = dif - dif_prev;
	  error_ppm = second_diff/interval*1000000;
	  printf ("%11.1f %6ld %9ld",
		  error_ppm,
		  txc.tick,
		  txc.freq);
	  if (loops > 2)
	    {			/* print suggested values */
	      long tick_delta = 0, freq_delta = 0;
	      
	      tick_delta = ceil((-error_ppm + txc.freq/SHIFT - hz)/hz);
	      error_ppm += tick_delta*hz;
	      freq_delta = -error_ppm*SHIFT;
	      printf("  %6ld %9ld\n",
		     txc.tick + tick_delta, txc.freq + freq_delta);
	      if (loops > 4 && adjusting)
		{
		  if (abs(error_ppm)>500)
		    {
		      if (force_adjust)
			printf (
"\nWARNING: required correction is greater than plus/minus 500 parts \n"
"per million, but adjusting anyway per your request.\n");
		      else
			{
			  printf(
"\nERROR: required correction is greater than plus/minus 500 parts \n"
"per million, quitting (use --force-adjust to override).\n");
			  if (resetting)
			    reset_time_status();
			  exit(1);
			}
		    }

		  txc.modes = ADJ_FREQUENCY | ADJ_TICK;
		  txc.tick += tick_delta;
		  txc.freq += freq_delta;
		  if (adjtimex (&txc) < 0) 
		    {
		      perror ("adjtimex"); 
		      exit(1);
		    }
		  if (resetting)
		    reset_time_status();
		  loops -= 3;
		}
	    }
	  else
	    printf("\n");
	    }
      else
	printf("\n");
      dif_prev = dif;
      if (interval == 0)
	break;
      if (count)		/* if it is not the last round */
	xusleep (interval*1000000L - 500000); /* reading RTC takes 1 sec */
    }
}

void reset_time_status()
{
  /* Using the adjtimex(2) system call to set any time parameter makes
     an early kernel (2.0.40 and 2.4.18 or later are reportedly okay)
     think the clock is synchronized with an external time source, so
     it sets the kernel variable time_status to TIME_OK.  Thereafter,
     it will periodically adjust the CMOS clock to match.  We prevent
     this by setting the clock, because that has the side effect of
     resetting time_status to TIME_BAD.  We try not to actually change
     the clock setting. */
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

static void log_times()
{
#ifdef NET_TIME_CLIENT
  struct protoent proto;
  int sockfd, val, len, c;
  struct sockaddr sa={AF_INET, "127.0.0.1"};
  struct hostent he;
#endif
  double sigma_ref;
  char ch, buf[64], *s;
  int n, ret;
  struct timeval tv_sys;
  struct tm bdt;
  time_t when, tref;
  double ftime_ref, ftime_sys, ftime_cmos;

  /* dummy training call: the next important timestamp will be more accurate */
  gettimeofday(&tv_sys, NULL);

  if (watch)
    {
      while(1) {
	printf("Please press <enter> when you know the time of day: ");
	ch = getchar();
	gettimeofday(&tv_sys, NULL);
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
      FILE *ifile;
      char command[128];
      char buf[BUFLEN];
      int i, j;
      double d, mean=0, val, var=0, num=0; /* for finding statistics */
      
      struct stat filestat;
      char *paths[]={"/usr/bin/ntpdate","/bin/ntpdate",
		     "/usr/sbin/ntpdate","/sbin/ntpdate"};

      for (i=0; i<sizeof(paths)/sizeof(paths[0]); i++)
	{
	  stat(paths[i], &filestat);
	  if (S_ISREG(filestat.st_mode))
	    goto found_ntpdate;
	}
      failntpdate("cannot find ntpdate");

    found_ntpdate:
      sprintf(command, "%s -q -d %.32s ", paths[i], timeserver);
      ifile = popen(command, "r");
	      
      if (ifile == NULL) 
	failntpdate("call to ntpdate failed");
	      
      /* read and save the significant lines, which should look like this:
filter offset: -0.02800 -0.01354 -0.01026 -0.01385
offset -0.013543
 1 Sep 11:51:23 ntpdate[598]: adjust time server 1.2.3.4 offset -0.013543 sec
 */

      while(fgets(buf, BUFLEN, ifile))
	{
	  if (!strncmp(buf, "filter offset:", strlen("filter offset:")))
	    {
	      /* These lines show the offsets for each of ntpdate's
		 samples.  Find their variance, which we will use to
		 indicate the accuracy of the offset we're using.
		 This is probably conservative, since the offset we're
		 using is probably not close to the mean. */
	      s = strstr(buf, ":")+1;
	      for (j = 0; j < 4; j++)
		if ((val = strtod(s, &s))) /* diregard offset of
			      exactly zero - might be okay, but more
			      likely no conversion was performed */
		  {
		    d = val - mean;
		    num += 1.;
		    var = (num-1)/num*(var + d*d/num);
		    mean = ((num-1.)*mean + val)/num;
		  }
	    }

	  if (num>0 && (strstr(buf,"adjust time server") || strstr(buf,"step time server")))
	    goto ntpdate_okay;
	}
      failntpdate("cannot understand ntpdate output");

    ntpdate_okay:
      gettimeofday(&tv_sys, NULL);
      ftime_sys = tv_sys.tv_sec;
	      /* ntpdate selects the offset from one of its samples
		 (the one with the shortest round-trip delay?) */
      ftime_ref = ftime_sys + atof(strstr(buf, "offset") + 6);
      pclose(ifile);
      sigma_ref = sqrt(var);

      {
	time_t now = (time_t)ftime_ref;
	bdt = *gmtime(&now);
	printf("      reference time is %s", ctime(&now));
	printf("reference time - system time = %12.3f - %12.3f "
	       "= %4.3f sec\n", 
	       ftime_ref, ftime_sys, ftime_ref - ftime_sys);
      }


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
      gettimeofday(&tv_sys, NULL);
      now = (time_t)tv_sys.tv_sec;
      bdt = *gmtime(&now);
      ftime_sys = tv_sys.tv_sec + tv_sys.tv_usec*.000001;
      ftime_ref = 0;
      sigma_ref = 0;
    }

  {
    time_t cmos_time;
    double system_sec;

    cmos_init ();
    cmos_read_time (&cmos_time, &system_sec);
    ftime_cmos = ftime_sys + cmos_time - system_sec;
  }

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

static 
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
      printf("System clock is synchronized (by ntpd?) - bad.\n");
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
"/sbin/hwclock has not set system time and adjusted the cmos clock \n"
"since %.24s - good.\n", 
ctime(&prev.log));
      else
	{
	  printf("/sbin/hwclock set system time and adjusted the cmos clock \n"
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
      printf("  it has not been reset with `date' or `/sbin/hwclock`,\n");
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

static 
int valid_cmos_rate(double ftime_cmos, double ftime_ref, double sigma_ref)
{
  int default_answer;
  int ch;
  char buf[BUFLEN];

  default_answer = undisturbed_cmos?'y':'n';
  do
    {
      printf("\nAre you sure that, since %.24s,\n", ctime(&prev.log));
      printf("  the real time clock (cmos clock) has run continuously,\n");
      printf("  it has not been reset with `/sbin/hwclock',\n");
      printf("  no operating system other than Linux has been running, and\n");
      printf("  ntpd has not been running? (y/n) [%c] ", default_answer);
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


/*
 * Read informations from /etc/adjtime file.
 * If file doesn't exist, return default zero values.
 */
static struct cmos_adj *get_cmos_adjustment()
{
  FILE *adj;
  static struct cmos_adj ca;

  ca.ca_factor = ca.ca_adj_time = ca.ca_remainder = 0;
  if ((adj = fopen (ADJPATH, "r")) != NULL)
    {
      if (fscanf (adj, "%lf %ld %lf",
		&ca.ca_factor,
		&ca.ca_adj_time,
		&ca.ca_remainder) < 0)
	{
	  perror (ADJPATH);
	  exit (2);
	}
      fclose (adj);
    }
#ifdef DEBUG
  printf ("CMOS clock was last adjusted %s\n", ctime(&ca.ca_adj_time));
#endif
  return &ca;
}


static FILE *lfile;		/* pointer to log file, or NULL if it
				   has not been opened yet */

static 
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

static 
void endhackent(void)
{
  if (lfile) fclose(lfile);
  lfile = NULL;
}

/* read next entry in clock comparison log, fill a struct hack from
   it, and return a pointer to it.  Ignore lines starting with `#'.
   Return NULL when there are no more lines to read.  */
static 
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
static 
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

  if (verbose)
    {
      fprintf(stdout, "\nlog entry:\n");
      fprintf(stdout, "%s %.*f %.*f %13.3f %5d %7d %s %13.3f %s\n",
	      timestring, 
	      digits, ph->ref, digits, ph->sigma_ref,
	      ph->sys, ph->tick, ph->freq, ph->sys_ok?"y":"n",
	      ph->cmos, ph->cmos_ok?"y":"n");
    }

  if (!logging)
      fprintf(stdout, "logging suppressed\n");

  else
    {
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
      lfile = NULL;
      if (verbose)
	fprintf(stdout, "written to %s\n", log_path);
    }
}

/* convert a broken-down time representing UTC to calendar time
    representation (time_t), and return it.  As a side effect, set the
    tm_wday and tm_yday members of the broken-down time. (like mktime) */
static 
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

static 
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

/*
  review log file and find least-square estimates of drifts.  If
  "adjusting" is nonzero, set sytem time parameters to the
  least-squares estimates. */
static 
void review()
{
  int i, n, nmax = 0, digits;
  struct hack *ph, **hacks = NULL;
  double diff_ppm, sigma_ppm, cmos_time, sys_time, s0, s1, ref_time;
  time_t start, finish;
  char startstring[26], finishstring[26];
  double x[2], p[4], h[4], z[2], r[4], cmos_var, sys_var, ref_var;
  long tick_delta = 0;
  double error_ppm = 0;
  int hz, tick_min, tick_mid, tick_max;
  long maxfreq;

  probe_time(&hz, &tick_min, &tick_mid, &tick_max, &maxfreq);

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
  /*
    In the following, we assume the reference times are most accurate,
    then the CMOS clock, then the system clock.  Hence, when comparing
    CMOS and reference times, we're calculating the error in PPM of
    the CMOS rate, and when comparing system time to either CMOS or
    reference times, we're calculating error in PPM of the system
    rate.  For system time, we're calculating the error if TICK is set
    to the middle of the rnage, and FREQ is zero.
  */

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
	    diff_ppm = 1.e6*(sys_time - cmos_time)/sys_time
	    - 100*(hacks[i]->tick - tick_mid) - hacks[i]->freq/SHIFT;
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
	    diff_ppm = 1.e6*(cmos_time - ref_time)/cmos_time;
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
	    diff_ppm = 1.e6*(sys_time - ref_time)/sys_time
	    - 100*(hacks[i]->tick - tick_mid) - hacks[i]->freq/SHIFT;
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
	 "   cmos_error = %.*f +- %.*f ppm\n",
	 digits, x[0], digits, sigma_ppm);
  if (sigma_ppm < 100)
    printf("      suggested adjustment = %6.4f sec/day\n",
	   -x[0]*SECONDSPERDAY/1.e6);
  else
    printf("      (no suggestion)\n");
  {
    struct cmos_adj *pca = get_cmos_adjustment();
    printf("        current adjustment = %6.4f sec/day\n",
	   pca->ca_factor);
  }

  sigma_ppm = sqrt(p[3]);
  digits = -(int)floor(log(.5*sigma_ppm)/log(10.));
  if (digits < 0) digits = 0;
  printf("   sys_error = %.*f +- %.*f ppm\n",
	 digits, x[1], digits, sigma_ppm);
  if (sigma_ppm < hz)
    {
      error_ppm = x[1];
      if (error_ppm > hz)
	tick_delta = -(error_ppm + hz/2)/hz;
      else if (error_ppm < -hz)
	tick_delta = (-error_ppm + hz/2)/hz;
      error_ppm += tick_delta*hz;
      printf("      suggested tick = %5ld  freq = %9.0f\n",
	     tick_mid + tick_delta, -error_ppm*SHIFT);
      if (abs(error_ppm)>500)
	printf ("WARNING: required correction is greater "
		"than plus/minus 500 parts per million.\n");
    }
  else
    printf("      (no suggestion)\n");
  {
    txc.modes = 0;
    adjtimex(&txc);
    printf("        current tick = %5ld  freq = %9ld\n",
	   txc.tick, txc.freq );
  }
  printf(
"note: clock variations and unstated data errors may mean that the\n"
"least squares solution has a bigger error than estimated here\n");
  if (sigma_ppm < 100 && adjusting)
    {
      if (abs(error_ppm)>500)
	{
	  if (force_adjust)
	    printf (
"\nWARNING: required correction is greater than plus/minus 500 parts \n"
"per million, but adjusting anyway per your request.\n");
	  else
	    {
	      printf(
"\nERROR: required correction is greater than plus/minus 500 parts \n"
"per million, quitting (use --force-adjust to override).\n");
	      if (resetting)
		reset_time_status();
	      exit(1);
	    }
	}
      txc.modes = ADJ_FREQUENCY | ADJ_TICK;
      txc.tick = tick_mid + tick_delta;
      txc.freq = -error_ppm*SHIFT;
      if (adjtimex (&txc) < 0) 
	{
	  perror ("adjtimex"); 
	  exit(1);
	}
      if (resetting)
	reset_time_status();
      printf("new tick = %5ld  freq = %7ld\n", txc.tick, txc.freq );
    }

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
static 
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
