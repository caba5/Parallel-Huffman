# A parallel implementation of Huffman coding

The project tries to parallelize the Huffman encoding process of a file given as input.

*A detailed description of the code and the results can be found in the **Parallel Huffman** PDF file*.

The implementations are meant just to measure the various achievable improvements through the parallelizization of a standard encoding mechanism, hence, all of the tests on the user's input and other controls have been ignored. Nonetheless, the programs have been statically tested for memory leaks and memory-related errors in general.

In particular, the program spawns a number of threads (given as argument) which work in parallel on different chunks of a particular task, thus translating into a *map* skeleton.

Nearly all phases were parallelized with the exception of the tree building and codes generation phases, as well as the optional decompression step.

The load balancing between the threads is static.

Parallelising the file reading and writing yields poor improvements due to OS buffer restrictions.

## Versions

Three versions of the program are included:
- A sequential version, with no threads or parallel work;
- A *pthread*-based version;
- A version based on the *FastFlow* library [FastFlow](https://github.com/fastflow/fastflow).

## Tests

The tests were run on a concatenation of 200 *La Divina Commedia* texts (which can be created through ```./gen_concat.sh 200 commedia```), on a AMD EPYC 7301 with 2 sockets, 16 cores per socket with 2-way hyperthreading machine with 256GB of memory.

## Execution

First, the files have to be compiled through ```make seq```, ```make par```, and ```make ff``` for the three versions described above, respectively. The Makefile assumes that the FastFow library is located in the home directory of the user executing the command.

For the sequential program, with *commedia200.txt* as input file:
```
./seq commedia200.txt
```

For the *pthread*s program, with *commedia200.txt* as input file and 16 worker threads:
```
./par commedia200.txt 16
```

For the *FastFow* version, with *commedia200.txt* as input file and 16 worker threads:
```
./ff commedia200.txt 16
```

The verification process for testing the correctness of the parallel compression (through decompression) is made by invoking the commands above followed by a ```v``` flag. 
The output of the command is the decompressed version of the file to ```stderr```.
Example of invocation:
```
./par commedia200.txt 16 v 2> /dev/null
```