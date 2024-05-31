CPP=g++
CFLAGS=-std=c++11 -pthread
LDFLAGS=-lcurl

scheduler_os: final.cpp
	$(CPP) $(CFLAGS) final.cpp -o scheduler_os $(LDFLAGS)
	chmod +x scheduler_os

clean:
	rm -f scheduler_os