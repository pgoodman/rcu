all:
	clang++ -O0 -g3 -I./ -std=c++11 -stdlib=libc++ -c main.cc -o main.o
	clang++ -O0 -std=c++11 -stdlib=libc++ main.o -pthread -o rcu

clean:
	-rm *.o
