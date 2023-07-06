seq:
	g++ -O3 -Wall -pedantic -std=c++20 -I ./ -o seq ./Sequential/SequentialHuf.cpp

par:
	g++ -O3 -std=c++20 -I ./ -Wall -pedantic -pthread -o par ./Pthreads/ParallelHuf.cpp

ff:
	g++ -O3 -Wall -pedantic -pthread -std=c++20 -I ./ -I ~/fastflow -o ff ./FastFlow/Tasks.hpp ./FastFlow/FastflowHuf.cpp
