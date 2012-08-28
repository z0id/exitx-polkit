all:exitx exitx.po
exitx: main.c halt.h hibernate.h logout.h reboot.h
	gcc -O2 -Wall -o exitx main.c `pkg-config gtk+-2.0 --cflags` `pkg-config gtk+-2.0 --libs` `pkg-config --cflags --libs glib-2.0` `pkg-config x11 --libs` 

indent: main.c
	indent -npro -kr -i8 -ts8 -sob -l120 -ss -ncs -cp1 main.c
exitx.po:*.c 
	xgettext -k_ --language=C -o $@  $^ --from-code=utf-8
	#xgettext -j -k_ -o $@  $^
.PHONY:clean
clean:
	-rm -f exitx.po exitx
