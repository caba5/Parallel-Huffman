#ifndef TASKS_H
#define TASKS_H

#include <vector>
#include <string>
#include <mutex>
#include <unordered_map>
#include <queue>
#include <memory>

struct Node {
    char data;
    unsigned freq;
    std::shared_ptr<Node> left, right;

    Node(char data, unsigned freq) :
        data(data),
        freq(freq),
        left(nullptr),
        right(nullptr)
    {}   
};

struct cmp {
    bool operator()(std::shared_ptr<Node> n1, std::shared_ptr<Node> n2) {
        if (n1->freq == n2->freq)
            return n1->data < n2->data;
        return n1->freq > n2->freq;
    }
};

// Task for parallel file reading
typedef struct __frtask {
    char* filename;
    int fileSize;
    int nw;
    int i;
    std::vector<std::unordered_map<char, unsigned>>* maps;
    std::vector<std::vector<std::pair<char, unsigned>>>* mapReds;
    std::vector<std::string>* stringReds;
    std::vector<std::mutex>* mutexes;

    __frtask(
        char* filename, 
        int fileSize, 
        int nw,
        int i,
        std::vector<std::unordered_map<char, unsigned>>* maps,
        std::vector<std::vector<std::pair<char, unsigned>>>* mapReds,
        std::vector<std::string>* stringReds,
        std::vector<std::mutex>* mutexes
    ) : filename(filename), 
        fileSize(fileSize), 
        nw(nw),
        i(i),
        maps(maps), 
        mapReds(mapReds), 
        stringReds(stringReds), 
        mutexes(mutexes) 
    {}
} FRTASK;

// Task used by the reducers farm to work on the read sections of the file
typedef struct __redtask {
    std::vector<std::vector<std::pair<char, unsigned>>>* mapReds;
    std::vector<std::string>* stringReds;
    std::vector<std::unordered_map<char, unsigned>>* outReducedMaps;
    int i;
    int nw;

    __redtask(
        std::vector<std::vector<std::pair<char, unsigned>>>* mapReds,
        std::vector<std::string>* stringReds,
        std::vector<std::unordered_map<char, unsigned>>* outReducedMaps,
        int i,
        int nw
    ) : mapReds(mapReds), stringReds(stringReds), outReducedMaps(outReducedMaps), i(i), nw(nw) {}

    __redtask() {}
} REDTASK;

// Partial codetask used "astride" of the reducers farm and the codes generation farm
typedef struct __parcodetask {
    std::vector<char>* symbols;
    std::vector<unsigned>* freqs;
    int nw;

    __parcodetask(
        std::vector<char>* symbols,
        std::vector<unsigned>* freqs,
        int nw
    ) : symbols(symbols), freqs(freqs), nw(nw) {}
} PARCODETASK;

// Task used in the codes generation farm
typedef struct __codestask {
    std::vector<char>* symbols;
    std::vector<unsigned>* freqs;
    std::mutex* queueMutex;
    std::priority_queue<std::shared_ptr<Node>, std::vector<std::shared_ptr<Node>>, cmp>* q;
    std::unordered_map<char, std::string>* charCodeMap;
    int nw;
    int i;

    __codestask(
        std::vector<char>* symbols,
        std::vector<unsigned>* freqs,
        std::mutex* queueMutex,
        std::priority_queue<std::shared_ptr<Node>, std::vector<std::shared_ptr<Node>>, cmp>* q,
        std::unordered_map<char, std::string>* charCodeMap,
        int nw,
        int i
    ) : symbols(symbols), freqs(freqs), queueMutex(queueMutex), q(q), charCodeMap(charCodeMap), nw(nw), i(i) {}
} CODESTASK;


typedef struct __compressiontask {
    std::string* text;
    std::unordered_map<char, std::string>* charCodeMap;
    std::vector<std::string>* compressedResults;
    std::string* compressedText;
    int avgCodeLen;
    int nw;
    int i;

    __compressiontask(
        std::string* text,
        std::unordered_map<char, std::string>* charCodeMap,
        std::vector<std::string>* compressedResults,
        std::string* compressedText,
        int avgCodeLen,
        int nw,
        int i
    ) : text(text), 
        charCodeMap(charCodeMap), 
        compressedResults(compressedResults),
        compressedText(compressedText),
        avgCodeLen(avgCodeLen), 
        nw(nw),
        i(i)
    {}
} COMPRESSIONTASK;

/* Partial task for the final stage (which can be either writing to file,
    either none) */
typedef struct __parfinal {
    std::string* compressedText;
    std::unordered_map<char, std::string>* charCodeMap;
    int avgCodeLen;
    int nw;

    __parfinal(
        std::string* compressedText,
        std::unordered_map<char, std::string>* charCodeMap,
        int avgCodeLen,
        int nw
    ) : compressedText(compressedText), charCodeMap(charCodeMap), avgCodeLen(avgCodeLen), nw(nw) {}
} PARFINAL;

// Task used when writing the compressed string to the file
typedef struct __tofiletask {
    std::string* filename;
    std::string* compressedText;
    std::vector<std::pair<int, int>>* textPositions;
    std::vector<int>* filePositions;
    int compressedFileSize;
    int i;
    int nw;

    __tofiletask(
        std::string* filename,
        std::string* compressedText,
        std::vector<std::pair<int, int>>* textPositions,
        std::vector<int>* filePositions,
        int compressedFileSize,
        int i,
        int nw
    ) : filename(filename), 
        compressedText(compressedText), 
        textPositions(textPositions), 
        filePositions(filePositions), 
        compressedFileSize(compressedFileSize), 
        i(i), 
        nw(nw) 
    {}
} TOFILETASK;

// Unused -> decompressing in parallel is unfeasible without errors
typedef struct __decompressiontask {
    std::string* compressedText; 
    std::unordered_map<std::string, char>* revCharCodeMap;
    std::vector<std::string>* decompressedStrings;
    int avgCodeLen;
    int i;
    int nw;

    __decompressiontask(
        std::string* compressedText,
        std::unordered_map<std::string, char>* revCharCodeMap,
        std::vector<std::string>* decompressedStrings,
        int avgCodeLen,
        int i,
        int nw
    ) : compressedText(compressedText), 
        revCharCodeMap(revCharCodeMap), 
        decompressedStrings(decompressedStrings), 
        avgCodeLen(avgCodeLen), 
        i(i), 
        nw(nw) 
    {}
} DECOMPRESSIONTASK;
#endif