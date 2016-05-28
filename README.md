# java-thread-locality-profiler
A profiler of Java applications to detect application objects shared among user threads

It indicates the percentage of objects touched by more than one user-level
thread. It can be useful to show which objects are shared, and which are local.

I developed this profiler during my master thesis work which can be found
[here:]
(http://uu.diva-portal.org/smash/get/diva2:608397/FULLTEXT01.pdf)


To properly compile and use this thread locality profiler agent, you need JDK 5 
or higher.

The following instructions should work on Linux. They are not tested on
any other platforms

# Compilation and installation
Edit the file 'Makefile' according to the given instructions mentioned in
Basically, you need to:

  1. Change the definition of JAVAINCLUDE variable to your own system's include 
	 directory where the header <jvmti.h> is located. Normally it should be 
	 inside the 'include' directory under your JDK directory.
	 After that, type 'make' at your command prompt. This will compile and 
	 generate the agent library. 
	 
  2.  For this agent library to be located by the JVM, it is necessary to copy it
	  to your system's 'lib' directory. You can change the USRLIB definition  
	  to your own lib directory, for example, under my system it is: '/usr/lib'.  

  3.  After that, you can type 'make install' at your command prompt to copy the
 	  agent library to the this agent library and copy it to your 'lib' directory.
 	  Or if you wish, you can manually copy the .so  file located  in 'lib'
 	  directory within this directory to your system's 'lib' directory.

