# A simple make file, compiles the thread locaily information agent to a shared
# object, and generates a dynamic link library out of it.
#
# How to use:
# Change JAVAINCLUDE to your own system's include directory where the header
# <jvmti.h> is located. If you are using a Mac, you may want to remove the 2nd
# include flag from IFLAGS. Also change the compiler variable CC to hold the
# C++ compiler you use.
#
# For this agent library to be located by the JVM, it is necessary to copy it
# to your system's 'lib' directory. You can change the USRLIB definition
# to your own lib directory, for example, under my system it is: '/usr/lib'.
# After that, you can type 'make intall' at your command prompt to copy the
# agent library to the this agent library and copy it to your 'lib' directory.
# Or if you wish, you can manually copy the .so  file located  in 'lib'
# directory within this directory to your system's 'lib' directory.
#
# There are targets for both Linux and Mac.
# 
# Author: Nosheen Zaza

USRLIB = /usr/lib

JAVAINCLUDE = /etc/java/jdk1.7.0_01/include
IFLAGS = -I$(JAVAINCLUDE) -I$(JAVAINCLUDE)/linux
CC = g++

# To enable some C++0x features (e.g unordered_map), we add '-std=gnu++0x' flag
CFLAGS = $(IFLAGS)

# Source code directory
SRCDIR = src

#headers:
_DEPS = info_file_io.h
DEPS = $(patsubst %,$(SRCDIR)/%,$(_DEPS))

# Object directory
ODIR = obj
_OBJ = info_file_io.o profiling_info_parser.o thread_locaity_info.o
OBJ = $(patsubst %,$(ODIR)/%,$(_OBJ))

# Directory in which the generated .so library will be stored.
LIBDIR = lib/
LIBNAME = libthread_locaity_info.so
LIB = $(LIBDIR)$(LIBNAME)

# Executables directory
EXEDIR = info_parser

EXEBIN = bin_info_parser

all: thread_locaity_info bin_info_parser

$(SRCDIR)/%.cpp: $(SRCDIR)/%.h
	touch $@;

$(ODIR)/%.o: $(SRCDIR)/%.cpp
# -fPIC is to make the generated object file a shared one
	$(CC) -fPIC -c $^ -o $@ $(CFLAGS)

# Make the output file parser executable
$(EXEBIN): $(ODIR)/info_file_io.o $(ODIR)/profiling_info_parser.o
	$(CC) $^ -o $(EXEDIR)/$@ $(CFLAGS)
	
thread_locaity_info: $(OBJ)

# Combine the object files into a library file
# For some reason, this version of the options to make to .so library
# did not do the job:
#	ld -G *.o -o libjvmti_aspect.so
# The one I am using here, however, does it nicely.
	$(CC) -shared  $^ -o $(LIB)

.PHONY: clean

# Clean library and object directories
clean:
	rm -f $(OBJ)
	rm -f $(LIB)
	rm -f $(EXEDIR)/$(EXEBIN)
	

# TODO it seems it needs reboot or else it does not see the library. Check this
# issue.
# Copy generated agent library to the system library directory
install:

	@if `whoami` != "root";\
		then \
		@echo 'To proceed, please enter your root password'; \
	fi
	@sudo cp -u -f $(LIB) $(USRLIB)
	@echo 'Finished Successfully'

osx_compile:
	clang++ -O3 -DNDEBUG -feliminate-unused-debug-symbols -fPIC -shared -o obj/libthread_locaity_info.jnilib -I/System/Library/Frameworks/JavaVM.framework/Versions/A/Headers/ src/thread_locaity_info.cpp

osx_test:
	javac -d java_test_code/classes java_test_code/src/ThreadsTest.java
	java -agentpath:/Users/tobias/Work/rsrch/CONCUR/trunk/nosheen/ThreadLocality_JVMAgent/obj/libthread_locaity_info.jnilib -classpath java_test_code/classes/ ThreadsTest

osx_hello:
	javac -d java_test_code/classes java_test_code/src/Hello.java
	java -agentpath:/Users/tobias/Work/rsrch/CONCUR/trunk/nosheen/ThreadLocality_JVMAgent/obj/libthread_locaity_info.jnilib -classpath java_test_code/classes/ Hello
