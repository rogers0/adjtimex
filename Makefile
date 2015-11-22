adjtimex: adjtimex.c
	gcc -Wall -o adjtimex adjtimex.c

clockdiff: clockdiff.c
	gcc -Wall -o clockdiff clockdiff.c
	
tar:  README adjtimex.8 adjtimex.c adjtimex clockdiff.c Makefile adjtimex.lsm
	tar -czf adjtimex.tar.gz README adjtimex.8 adjtimex.c adjtimex clockdiff.c \
		Makefile adjtimex.lsm 

shar:  README adjtimex.8 adjtimex.c adjtimex clockdiff.c Makefile adjtimex.lsm
	shar README adjtimex.8 adjtimex.c adjtimex clockdiff.c \
		Makefile adjtimex.lsm >adjtimex.shar 
