adjtimex: adjtimex.c
	${CC} -Wall -o adjtimex adjtimex.c

clockdiff: clockdiff.c
	${CC} -Wall -o clockdiff clockdiff.c
	
tar:  README adjtimex.8 adjtimex.c adjtimex clockdiff.c Makefile adjtimex.lsm
	tar -czf adjtimex.tar.gz README adjtimex.8 adjtimex.c adjtimex clockdiff.c \
		Makefile adjtimex.lsm 

shar:  README adjtimex.8 adjtimex.c adjtimex clockdiff.c Makefile adjtimex.lsm
	shar README adjtimex.8 adjtimex.c adjtimex clockdiff.c \
		Makefile adjtimex.lsm >adjtimex.shar 
