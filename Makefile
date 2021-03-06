all: ext2_mkdir ext2_cp ext2_ln ext2_rm ext2_restore ext2_checker

ext2_mkdir: ext2_mkdir.c ext2_util.h ext2.h
	gcc -Wall -g -o ext2_mkdir ext2_mkdir.c

ext2_cp: ext2_cp.c ext2_util.h ext2.h
	gcc -Wall -g -o ext2_cp ext2_cp.c

ext2_ln: ext2_ln.c ext2_util.h ext2.h
	gcc -Wall -g -o ext2_ln ext2_ln.c

ext2_rm: ext2_rm.c ext2_util.h ext2.h
	gcc -Wall -g -o ext2_rm ext2_rm.c

ext2_restore: ext2_restore.c ext2_util.h ext2.h
	gcc -Wall -g -o ext2_restore ext2_restore.c

ext2_checker: ext2_checker.c ext2_util.h ext2.h
	gcc -Wall -g -o ext2_checker ext2_checker.c

.PHONY: clean
clean : 
	rm -f ext2_mkdir ext2_cp ext2_ln ext2_rm ext2_restore ext2_checker 