CXXFLAGS =	-O2 -g -Wall -fmessage-length=0

OBJS =		cgi.o

LIBS =

TARGET =	cgi

$(TARGET):	$(OBJS)
	$(CXX) -o $(TARGET) $(OBJS) $(LIBS)

all:	$(TARGET)

clean:
	rm -f $(OBJS) $(TARGET)
