filecdc:
	gcc *.c `pkg-config --cflags --libs glib-2.0` -o filecdc