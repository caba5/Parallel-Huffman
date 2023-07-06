#include <iostream>
#include <fstream>
#include <vector>
#include <unordered_map>
#include <queue>
#include <string>
#include <stdlib.h>
#include <filesystem>
#include <thread>
#include <mutex>
#include <stdio.h>

#include "utimer.hpp"

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
            return n1->data < n2->data; // Used to match order inside of the heap
        return n1->freq > n2->freq;
    }
};

void mapPairs(
    const char* filename, 
    const int fileSize, 
    std::vector<std::unordered_map<char, unsigned>>& maps,
    std::vector<std::vector<std::pair<char, unsigned>>>& mapReds,
    std::vector<std::string>& stringReds,
    std::vector<std::mutex>& mutexes,
    const int i, 
    const int nw
) {
    utimer t1("tutto");
    std::fstream file;
    file.open(filename);

    int delta = fileSize / nw;
    int from = i * delta;
    int to = i == nw - 1 ? fileSize : from + delta;

    file.seekg(from, std::ios::beg);

    std::string s;
    s.reserve(delta);

    char c;
    int count = from; // Avoiding tellg() saves much time
    while (count++ < to && file >> std::noskipws >> c) {
        ++maps[i][c];
        s += c;
    }   
    stringReds[i] = std::move(s);

    file.close();

    {
        utimer t2("Hash");
        for (const auto& pair : maps[i]) {
            auto pairHash = std::hash<char>{}(pair.first) % nw;
            std::unique_lock ul(mutexes[pairHash]);
            mapReds[pairHash].push_back(pair);
        }
    }
}

void reducePairs(
    const std::vector<std::vector<std::pair<char, unsigned>>>& mapReds,
    std::mutex& m, 
    std::unordered_map<char, unsigned>& res,
    const int i
) {
    std::unordered_map<char, unsigned> localRed;
    for (const auto& [key, value] : mapReds[i]) {
        localRed[key] += value;
    }
    for (const auto& [key, value] : localRed) {
        std::unique_lock ul(m);
        res[key] += value;
    }
}

void populateHeap(
    std::priority_queue<std::shared_ptr<Node>, std::vector<std::shared_ptr<Node>>, cmp>& q,
    const std::vector<char>& symbols,
    const std::vector<unsigned>& freqs,
    std::mutex& queueMutex,
    const int i,
    const int nw
) {
    int delta = symbols.size() / nw;
    int from = i * delta;
    int to = i == nw - 1 ? symbols.size() : from + delta;

    for (int j = from; j < to; ++j) {
        queueMutex.lock();
        q.push(std::make_shared<Node>(symbols[j], freqs[j]));
        queueMutex.unlock();
    }
}

std::shared_ptr<Node> sequentialTreeGeneration(
    std::priority_queue<std::shared_ptr<Node>, std::vector<std::shared_ptr<Node>>, cmp>& q
) {
    std::shared_ptr<Node> left, right, top;
    while (q.size() > 1) {
        left = q.top();
        q.pop();

        right = q.top();
        q.pop();

        top = std::make_shared<Node>('$', left->freq + right->freq);

        top->left = left;
        top->right = right;

        q.push(top);
    }

    return q.top();
}

void generateCodes(
    const std::shared_ptr<Node> root,
    const std::string currCode, 
    std::unordered_map<char, std::string>& charCodeMap
) {
    if (!root)
        return;
    
    if (root->data != '$')
        if (!charCodeMap.contains(root->data))
            charCodeMap[root->data] = currCode;
    
    generateCodes(root->left, currCode + "0", charCodeMap);
    generateCodes(root->right, currCode + "1", charCodeMap);
}

void compressToString(
    const std::string& text, 
    std::unordered_map<char, std::string>& charCodeMap,
    std::vector<std::string>& compressedResults,
    const int avgCodeLen,
    const int i,
    const int nw
) {
    int delta = text.size() / nw;
    int from = i * delta;
    int to = i == nw - 1 ? text.size() : from + delta;

    std::string localS;
    localS.reserve(delta * avgCodeLen);

    for (int j = from; j < to; ++j)
        localS += charCodeMap[text[j]];
    
    compressedResults[i] = std::move(localS);
}

void compressToFilePar(
    const std::string& filename,
    const std::string& compressedText,
    const std::vector<std::pair<int, int>>& textPositions,
    const std::vector<int>& filePositions,
    const int compressedFileSize,
    const int i,
    const int nw
) {
    std::fstream file;
    file.open(filename);

    file.seekp(filePositions[i]);

    uint8_t b = 0;
    int len = 0;
    for (int j = textPositions[i].first; j < textPositions[i].second; ++j) { 
        if (len < 7) {
            if (compressedText[j] == '1') ++b;
            b <<= 1;
            ++len;
        } else if (len == 7) {
            if (compressedText[j] == '1') ++b;
            len = 0;

            file << b;
            b = 0;
        }
    }
    if (i == nw - 1) {
        for (int i = 0; i < 7 - len; ++i)
            b <<= 1;
        
        file << b;
    }

    file.close();
}

std::string decompressStringSequential(
    const std::string& compressedText, 
    std::unordered_map<char, std::string>& charCodeMap,
    const int fileSize
) {
    std::unordered_map<std::string, char> revCharCodeMap;
    for (auto& [key, value] : charCodeMap) 
        revCharCodeMap[value] = key;

    std::string decompressedString;
    decompressedString.reserve(fileSize);
    
    std::string s = "";
    for (const char c : compressedText) {
        s += c;
        if (revCharCodeMap.contains(s)) {
            decompressedString += revCharCodeMap[s];
            s = "";
        }
    }

    return decompressedString;
} 

int main(int argc, char** argv) {
    if (argc < 3 || argc > 4) {
        std::cout << "Usage: " << argv[0] << " filename nw [verify]" << std::endl;
        return 1;
    }

    int nw = atoi(argv[2]);
    bool verify = argc == 4 && argv[3][0] == 'v';
    
    std::vector<std::thread> tids(nw);

    int fileSize = std::filesystem::file_size(argv[1]);

    std::unordered_map<char, unsigned> symbMap;
    std::mutex redsMutex;
    std::string text;

    text.reserve(fileSize);
    
    START(total)
    START(nowrite)
    {
        std::vector<std::unordered_map<char, unsigned>> maps(nw);
        std::vector<std::vector<std::pair<char, unsigned>>> mapReds(nw);
        std::vector<std::string> stringReds(nw);
        std::vector<std::mutex> mutexes(nw);

        // utimer t1("Mapping file content:\t");
        for (int i = 0; i < nw; ++i) 
            tids[i] = std::thread(mapPairs, argv[1], fileSize, std::ref(maps), std::ref(mapReds), std::ref(stringReds), std::ref(mutexes), i, nw);
        for (int i = 0; i < nw; ++i)
            tids[i].join();
    
    
        // utimer t2("Reducing read file pairs: ");
        for (int i = 0; i < nw; ++i)
            tids[i] = std::thread(reducePairs, std::ref(mapReds), std::ref(redsMutex), std::ref(symbMap), i);
        
        for (int i = 0; i < nw; ++i) {
            tids[i].join();
            text += stringReds[i]; // Reduction of the ordered strings
        }
    }

    // Not parallelized------------------------
    std::vector<char> symbols;
    std::vector<unsigned> freqs;
    {
        utimer t("Sequential symbol and frequency computation ");
        symbols.reserve(symbMap.size());
        freqs.reserve(symbMap.size());

        for (const auto& [sym, freq] : symbMap) {
            symbols.push_back(sym);
            freqs.push_back(freq);
        }
    }
    // ----------------------------------------

    std::priority_queue<std::shared_ptr<Node>, std::vector<std::shared_ptr<Node>>, cmp> q;
    std::shared_ptr<Node> root;
    std::unordered_map<char, std::string> charCodeMap;

    {
        std::mutex queueMutex;
        // utimer t1("Computing codes: ");
        
        for (int i = 0; i < nw; ++i) 
            tids[i] = std::thread(populateHeap, std::ref(q), std::ref(symbols), std::ref(freqs), std::ref(queueMutex), i, nw);
        for (int i = 0; i < nw; ++i)
            tids[i].join();

        utimer t("Sequential tree and codes generation ");
        root = sequentialTreeGeneration(q); // Cannot be parallelized
        generateCodes(root, "", charCodeMap); // Cannot be parallelized
    }

    int sumSize = 0;
    for (const auto& [_, v] : charCodeMap)
        sumSize += v.size();

    // Needed for reserving approx space for string during compression 
    int avgCodeLen = sumSize / charCodeMap.size();    

    std::string compressedText;
    std::vector<std::string> resultingCompressedStrings(nw);

    {
        // utimer t1("Compressing text: ");
        for (int i = 0; i < nw; ++i) 
            tids[i] = std::thread(compressToString, std::ref(text), std::ref(charCodeMap), std::ref(resultingCompressedStrings), avgCodeLen, i, nw);
        for (int i = 0; i < nw; ++i)
            tids[i].join();
        
        int totSize = 0;
        for (int i = 0; i < nw; ++i)
            totSize += resultingCompressedStrings[i].size();
        
        compressedText.reserve(totSize);

        for (int i = 0; i < nw; ++i) 
            compressedText += resultingCompressedStrings[i];
    }

    STOP(nowrite, elapsedTimeWithoutWriting);
    std::cout << "Program time without writing compressed data to file: " << elapsedTimeWithoutWriting << " usecs" << std::endl;

    if (verify) {
        std::vector<std::string> decompressedStrings(nw);

        // utimer t1("Decompressing: ");

        std::cerr << decompressStringSequential(compressedText, charCodeMap, fileSize);
    } else {
        // utimer t1("File compression: ");

        // START(mid)
        std::string fn = "compressed_" + std::string(argv[1]);

        /* compressedText is a string, thus each one of its elements is a char of size 1B. 
            Since we write a byte each 8 chars read from the string, the file size will ultimately be
            the string size / 8 */
        int compressedFileSize = compressedText.size() / 8;
        
        FILE* tempFile = fopen(fn.c_str(), "w");
        fseek(tempFile, compressedFileSize, SEEK_SET);
        fputc('\0', tempFile);
        fclose(tempFile);
        
        // STOP(mid, m)
        // std::cout << "Time spent on creating file: " << m << std::endl;

        std::vector<std::pair<int, int>> textPositions(nw);
        std::vector<int> filePositions(nw);
        int delta = compressedText.size() / nw;
        delta += delta % 8 == 0 ? 0 : 8 - (delta % 8);
        for (int i = 0; i < nw; ++i) {
            textPositions[i].first = i * delta;
            textPositions[i].second = i == nw - 1 ? compressedText.size() : (i + 1) * delta;
            filePositions[i] = textPositions[i].first / 8;
        }

        for (int i = 0; i < nw; ++i)
            tids[i] = std::thread(compressToFilePar, std::ref(fn), std::ref(compressedText), std::ref(textPositions), std::ref(filePositions), compressedFileSize, i, nw);
        for (int i = 0; i < nw; ++i)
            tids[i].join();
    }
    STOP(total, elapsed)
    std::cout << "Total program time: " << elapsed << " usecs" << std::endl;

    return 0;
}