/*
 * compare - print difference between system time and CMOS time
 *
 * Writes to stdout: 
 *	system time (seconds since 1/1/70 per RTC), 
 *	system time - RTC time, 
 *      drift: change in system time - RTC time since last report,
 *	frequency offset (in kernel, ((time_freq+1) >> (SHIFT_KF - 16)))
 * 	time offset (in kernel, time_adjust) 
 * Takes into account systematic error in RTC as recorded in /etc/adjtime, 
 * just like clock -a.
 *
 * AUTHORS 
 *      Jim Van Zandt <jrv@vanzandt.mv.com> 
 * Uses code from clock(8), which had the following contributors: 
 *      Charles Hedrick <hedrick@cs.rutgers.edu>
 *      Rob Hooft <hooft@EMBL-Heidelberg.DE> 
 *      Harald Koenig (koenig@nova.tat.physik.uni-tuebingen.de>
 * Also uses code from adjtimex(8), by:
 *      Steven S. Dick <ssd@nevets.oau.org>
 */
#include <stdio.h>
#include <errno.h>
#include <stdlib.h>
#include <unistd.h>
#include <time.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include <sys/types.h>
#include <sys/time.h>
#include <syscall.h>
#include <sys/timex.h>

_syscall1(int, adjtimex, struct timex *, txcp)
#define USE_INLINE_ASM_IO

 /* Here the information for time adjustments is kept. */
#define ADJPATH "/etc/adjtime"

 /* used for debugging the code. */
 /* #define DEBUG */

 /* Stupid constants */
#define SECONDSPERDAY 86400

 /* Globals */
int universal = 0;


#ifndef USE_INLINE_ASM_IO
int cmos_fd;

#endif

#define CMOS_READ(addr)      ({outb(0x70,(addr)|0x80); inb(0x71);})
#define CMOS_WRITE(addr,val) ({outb(0x70,(addr)|0x80); outb(0x71,(val)); })

static inline void
outb(short port, char val)
{
#ifdef USE_INLINE_ASM_IO
	__asm__ volatile ("out%B0 %0,%1"::"a" (val), "d"(port));

#else
	 lseek(cmos_fd, port, 0);
	write(cmos_fd, &val, 1);
#endif
}

static inline unsigned char
inb(short port)
{
	unsigned char ret;

#ifdef USE_INLINE_ASM_IO
	__asm__ volatile ("in%B0 %1,%0":"=a" (ret):"d"(port));

#else
	lseek(cmos_fd, port, 0);
	read(cmos_fd, &ret, 1);
#endif
	return ret;
}

void
cmos_init()
{
#ifdef USE_INLINE_ASM_IO
	if (ioperm(0x70, 2, 1)) {
		fprintf(stderr, "clock: unable to get I/O port access\n");
		exit(1);
	}
#else
	cmos_fd = open("/dev/port", 2);
	if (cmos_fd < 0) {
		perror("unable to open /dev/port read/write : ");
		exit(1);
	}
	if (lseek(cmos_fd, 0x70, 0) < 0 || lseek(cmos_fd, 0x71, 0) < 0) {
		perror("unable to seek port 0x70 in /dev/port : ");
		exit(1);
	}
#endif
}

static inline int
cmos_read_bcd(int addr)
{
	int b;

	b = CMOS_READ(addr);
	return (b & 15) + (b >> 4) * 10;
}

volatile void
usage()
{
	fprintf(stderr,
		"compare  [-p N] [-c N]\n"
		"  -p N  repeat every N seconds\n"
		"  -c N  loop N times otherwise loop forever\n"
	);
	exit(1);
}

int
main(int argc, char **argv, char **envp)
{
	struct timex txc;
	struct tm tm;
	time_t cmos_time;
	time_t last_time;
	double cmos_sec, system_sec, dif, dif_prev=0.;
	char *zone;
	char zonebuf[256];
	FILE *adj;
	double factor;
	double cmos_adjustment;
	double not_adjusted;
	int i;
	int interval = 0;
	int arg;
	extern char *optarg;
	struct timeval now;
	int count = -1;
	
	while ((arg = getopt(argc, argv, "p:c:")) != -1) {
		switch (arg) {
		case 'p':
			/* reading RTC takes 1 sec */
			interval = atoi(optarg) - 1;
			if (interval <= 0) {
				fprintf(stderr, "repeat interval out of range\n");
				exit(1);
			}
			break;
		case 'c':
			count = atoi(optarg);
			if (count <= 0 ) {
			        fprintf(stderr, "loop count out of range\n");
                                exit(1);
                        }
			break;
		case ':':
			fprintf(stderr, "missing parameter\n");
			break;
		default:
			usage();
		}
	}

	while (count != 0) {
		if (count > 0 ) count--;
		
		cmos_init();

		/* Read adjustment parameters first */
		if ((adj = fopen(ADJPATH, "r")) == NULL) {
			perror(ADJPATH);
			exit(2);
		}
		if (fscanf(adj, "%lf %ld %lf", &factor, &last_time, 
						&not_adjusted) < 0) {
			perror(ADJPATH);
			exit(2);
		}
		fclose(adj);
#ifdef DEBUG
		printf("Last adjustment done at %ld seconds after 1/1/1970\n", (long) last_time);
#endif


		/* read RTC exactly on falling edge of update flag */
		/* Wait for rise.... (may take up to 1 second) */

		for (i = 0; i < 10000000; i++)
			if (CMOS_READ(10) & 0x80)
				break;

		/* Wait for fall.... (must try at least 2.228 ms) */

		for (i = 0; i < 1000000; i++)
			if (!(CMOS_READ(10) & 0x80))
				break;

		/* The "do" loop is "low-risk programming" */
		/* In theory it should never run more than once */
		do {
			tm.tm_sec = cmos_read_bcd(0);
			tm.tm_min = cmos_read_bcd(2);
			tm.tm_hour = cmos_read_bcd(4);
			tm.tm_wday = cmos_read_bcd(6);
			tm.tm_mday = cmos_read_bcd(7);
			tm.tm_mon = cmos_read_bcd(8);
			tm.tm_year = cmos_read_bcd(9);
		}
		while (tm.tm_sec != cmos_read_bcd(0));

		/* fetch system time immediately */
		gettimeofday(&now, NULL);


		tm.tm_mon--;	/* DOS uses 1 base */
		tm.tm_wday -= 3;/* DOS uses 3 - 9 for week days */
		tm.tm_isdst = -1;	/* don't know whether it's daylight */
#ifdef DEBUG
		printf(" mday=%d  mon=%d  wday=%d  year=%d\n",
		       tm.tm_mday, tm.tm_mon, tm.tm_wday, tm.tm_year);
		printf("Cmos time  %d:%02d:%02d\n", tm.tm_hour, tm.tm_min, tm.tm_sec);
#endif

    /*
     * Mktime assumes we're giving it local time.  If the CMOS clock is in
     * GMT, we have to set up TZ to mktime knows it.  Tzset gets called
     * implicitly by the time code, but only the first time.  When
     * changing the environment variable, better call tzset explicitly.
     */
		if (universal) {
			zone = (char *) getenv("TZ");	/* save original time zone */
			(void) putenv("TZ=");
			tzset();
			cmos_time = mktime(&tm);
			/* now put back the original zone */
			if (zone) {
				if ((strlen(zone) + 4) > sizeof(zonebuf)) {
					fprintf(stderr, "Size of TZ variable is too long\n");
					exit(2);
				}
				strcpy(zonebuf, "TZ=");
				strcat(zonebuf, zone);
				putenv(zonebuf);
			} else {/* wasn't one, so clear it */
				putenv("TZ");
			}
			tzset();
			printf("%s", ctime(&cmos_time));
		} else {
			(void) mktime(&tm);	/* fix wday */
			/* printf ("%s", asctime (&tm)); */
		}


		if (universal)
			(void) putenv("TZ=");
		cmos_time = mktime(&tm);
		system_sec = now.tv_sec + .000001 * now.tv_usec;
#ifdef DEBUG
		printf("Number of seconds since 1/1/1970 is %ld\n", 
							(long) cmos_time);
#endif
		cmos_adjustment = ((double) (cmos_time - last_time))
			* factor / SECONDSPERDAY
			+ not_adjusted;
		cmos_sec = cmos_time + cmos_adjustment;
#ifdef DEBUG
		printf("Time since last adjustment is %d sec",
		       (int) (cmos_time - last_time));
		printf(", now needs adjustment by %9.6f sec\n", cmos_adjustment);
#endif
		dif = system_sec - cmos_sec;
		
		txc.modes = 0;
		if (adjtimex(&txc) < 0)
			perror("compare: adjtimex");
			
		printf("%9ld   %9.6f %9.6f %9ld %9ld %9ld\n",
		       (long) cmos_sec,
		       dif,
		       dif - dif_prev,
		       txc.freq,
		       txc.tick,
		       txc.offset);
		dif_prev = dif;
		if(interval == 0)
			break;
		if (count) sleep(interval);
	}
	return 0;
}
