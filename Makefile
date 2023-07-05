seq:
	g++ -O3 -Wall -pedantic -std=c++20 -o seq SequentialHuf.cpp

par:
	g++ -O3 -std=c++20 -Wall -pedantic -pthread -o par ParallelHuf.cpp

ff:
	g++ -O3 -Wall -pedantic -pthread -std=c++20 -I ~/fastflow -o ff Tasks.hpp FastflowHuf.cpp
