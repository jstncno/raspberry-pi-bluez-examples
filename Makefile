# adapted from: http://cs.millersville.edu/~katz/linuxHelp/make.html

# This is a simple makefile that compiles multiple C++ source files

# set the names here to be the names of your source files with the
# .cxx or .cpp replaced by .o
# Be *** SURE *** to put the .o files here rather than the source files

CONNECT = connect.o

#------------ no need to change between these lines -------------------
CC=gcc
CFLAGS = -O0 -g3 -Wall -MMD -MP `pkg-config --cflags glib-2.0 --cflags dbus-1`
LDFLAGS=-l bluetooth -l asound `pkg-config --libs glib-2.0 --libs dbus-1`

.SUFFIXES: .c

.c.o:
	@echo "########################################################################################"
	@echo "Building source file: " $<
	$(CC) -lc $(CFLAGS) -c $< -o $@ $(LDFLAGS)

#------------ no need to change between these lines -------------------


#------------ targets --------------------------------------------
# describe how to create the targets - often there will be only one target

all: connect

connect: $(CONNECT)
	@echo "########################################################################################"
	@echo "Building connect ..."
	$(CC) $(CONNECT) -o connect $(CFLAGS) $(LDFLAGS)

	    
clean:
	rm -f *.o *.d connect

#------------ dependencies --------------------------------------------
