dependdir := $(shell ls .depend >/dev/null 2>&1)
ifneq ($(dependdir), .depend)
    dependdir := $(shell touch .depend)
endif

CC=g++
GCC=gcc
COMP_PARA=-g -Wall  -c -D__MULITI_THREAD__ -D__DEBUG__ -DNDEBUG -D_LINUX -D_mt -DMSGPACK_USE_DEFINE_MAP #-D_CQ_DEBUG_

BASE_PATH=/home/share/debug_proxy
LINK_PARA=# -pg

INCS=-I/usr//include/mysql/  -I/usr/local/boost1.5.3/include -I/usr/include/ -I../../public2/proto/ -I../  -I$(BASE_PATH)/interface/ -I$(BASE_PATH)/interface/bson/ -I/usr/local/include/thrift/ -I../../engine/ -I../../public2/ -I../../public2/csv/

LIBS= -L/usr/local/lib/ -L/usr/local/ssl/lib/ -L/usr/lib64/mysql/ -L/usr/local/boost1.5.3/lib/ -L/lib/ -L../../engine/lib/  -L$(BASE_PATH)/libs -lbsoncpp  -lboost_thread -lboost_filesystem -lboost_program_options  -lboost_system  -lnskernel -lnc -lbase -lmysqlclient -lpthread -lz -lcrypt -lhiredis  -lssl -lmsgpack -lcrypto -lthrift -ljsoncpp -lcurl
CSRCS   = $(wildcard *.c)  $(wildcard ../../public2/*.c) $(wildcard ../../public2/idl/*.c)
CPPSRCS = $(wildcard *.cpp) $(wildcard ../../public2/*.cpp)  $(wildcard ../../public2/csv/*.cpp) ../../public2/proto/proto_10001_account_login.cpp  ../../public2/proto/proto_10100_wx_login.cpp ../../public2/proto/proto_base.cpp 
 

COBJS   = $(patsubst %.c, %.o, $(CSRCS))
CPPOBJS = $(patsubst %.cpp, %.o, $(CPPSRCS))
OBJS = $(CPPOBJS)
 

TARGET=../bin/gateserver

all : $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(LINK_PARA) -o $@ $^ $(LIBS)

testmongo: testmongo.cpp
	$(CC)  -o $@ $^ $(LIBS) $(INCS)

test: $(TESTOBJS)
	$(CC) $(LINK_PARA) -o test  $^ $(LIBS)
	cp -f $@ ../bin


$(COBJS) : %.o:%.c
	${GCC} ${COMP_PARA} $< -o $@ ${INCS}
	@echo

$(CPPOBJS) : %.o:%.cpp
	${CC} ${COMP_PARA} $(INCS) -o $@ $<
	@echo

#.cpp.o :
#	${CC} ${COMP_PARA} $< ${INCS}

#.c.o :
#	${GCC} ${COMP_PARA} $< ${INCS}

clean :
	@-rm -f $(CPPOBJS) $(COBJS) $(TARGET) $(wildcard ../logs/syslog/*.log) ./.depend
	@-rm -rf $(wildcard ../logs/daylog/*)
	
cleanlog :
	@-rm -f $(wildcard ../logs/syslog/*.log) ./.depend
	@-rm -rf $(wildcard ../logs/daylog/*)

depend:
	gcc -E -c $(CFLAGS) $(INCS) -MM *.c >.depend
	g++ -E -c $(CPPFLAGS) $(INCS) -MM *.cpp >>.depend

include .depend
