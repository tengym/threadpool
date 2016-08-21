CC =arm-hisiv200-linux-gcc 
AR =arm-hisiv200-linux-ar

SOURCES=$(foreach SUBDIR,./,$(wildcard $(SUBDIR)/*.c))
OBJECTS	= $(SOURCES:%.c=%.o)
#%c:%o:
threadpool:$(OBJECTS)
	$(CC) -c $^ -lm
	$(AR) crv libthreadpool.a $(OBJECTS)
	-@rm -f $(OBJECTS) threadpool *.o;
	-@mkdir ../include/threadpool -p
	@cp threadpool.h ../include/threadpool -Rf
	@cp libthreadpool.a ../lib -Rf
clean:
	-@rm -f $(OBJECTS) threadpool *.o libthreadpool.a;
