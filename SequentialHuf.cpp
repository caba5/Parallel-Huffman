#include <iostream>
#include <queue>
#include <string>
#include <vector>
#include <fstream>
#include <filesystem>
#include <unordered_map>
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

void getTextAndMapChars(
    const char* filename, 
    std::unordered_map<char, unsigned>& map, 
    std::string& text
) {
    std::fstream file;
    file.open(filename);

    if (!file.is_open()) {
        std::cerr << "Could not open the file" << std::endl;
        return;
    }
    
    char c;
    while (file >> std::noskipws >> c) {
        ++map[c];
        text +=  c;
    }

    file.close();
}

void populateSymbolsAndFrequencies(
    std::unordered_map<char, unsigned>& symbMap, 
    std::vector<char>& symbols, 
    std::vector<unsigned>& freqs
) {
    symbols.reserve(symbMap.size());
    freqs.reserve(symbMap.size());

    for (auto& [key, value] : symbMap) {
        symbols.push_back(key);
        freqs.push_back(value);
    }
}

struct cmp {
    bool operator()(std::shared_ptr<Node> n1, std::shared_ptr<Node> n2) {
        if (n1->freq == n2->freq)
            return n1->data < n2->data;
        return n1->freq > n2->freq;
    }
};

std::shared_ptr<Node> generateTree(const std::vector<char>& symbols, const std::vector<unsigned>& freqs, const int size) {
    std::shared_ptr<Node> left, right, top;

    std::priority_queue<std::shared_ptr<Node>, std::vector<std::shared_ptr<Node>>, cmp> q;

    for (int i = 0; i < size; ++i)
        q.push(std::make_shared<Node>(symbols[i], freqs[i]));

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

void generateCodes(const std::shared_ptr<Node> root, const std::string s, std::unordered_map<char, std::string>& charCodeMap) {
    if (!root)
        return;
    
    if (root->data != '$')
        if (!charCodeMap.contains(root->data))
            charCodeMap[root->data] = s;
    
    generateCodes(root->left, s + "0", charCodeMap);
    generateCodes(root->right, s + "1", charCodeMap);    
}

std::string compressToString(
    const std::string& text, 
    std::unordered_map<char, std::string>& charCodeMap,
    const int avg
) {
    std::string compressedText;
    compressedText.reserve(text.size() * avg);

    for (const char c : text)
        compressedText += charCodeMap[c];
    
    return compressedText;
}

void compressToFile(
    const std::string& filename, 
    const std::string& text, 
    std::unordered_map<char, std::string>& charCodeMap
) {
    std::ofstream file;
    file.open("compressed_" + filename);

    uint8_t b = 0;
    int l = 0;
    for (char c : text) {
        for (char bit : charCodeMap[c]) {
            if (l < 7) {
                if (bit == '1') ++b;
                b <<= 1;
                ++l;
            } else if (l == 7) {
                if (bit == '1') ++b;
                l = 0;

                file << b;
                b = 0;
            }
        }
    }
    for (int i = 0; i < 7 - l; ++i)
        b <<= 1;
    
    file << b;
    file.close();
}

std::string decompressString(
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
    if (argc < 2 || argc > 3) {
        std::cout << "Usage: " << argv[0] << " filename [verify]" << std::endl;
        return 1;
    }

    bool verify = argc == 3 && argv[2][0] == 'v';

    std::unordered_map<char, unsigned> symbMap;
    std::string text;

    START(seqComp)

    int fileSize = std::filesystem::file_size(argv[1]);
    text.reserve(fileSize);

    getTextAndMapChars(argv[1], symbMap, text);

    if (!symbMap.size())
        return 1;

    std::vector<char> symbols;
    std::vector<unsigned> freqs;

    populateSymbolsAndFrequencies(symbMap, symbols, freqs);   

    std::unordered_map<char, std::string> charCodeMap; 

    std::shared_ptr<Node> root = generateTree(symbols, freqs, symbols.size());

    generateCodes(root, "", charCodeMap);

    int sumSize = 0;
    for (const auto& [_, v] : charCodeMap)
        sumSize += v.size();

    // Needed for reserving approx space for string in 'compressToString()' 
    int avgCodeLen = sumSize / charCodeMap.size(); 

    if (verify) {

        std::string compressedString = compressToString(text, charCodeMap, avgCodeLen);
        
        text = decompressString(compressedString, charCodeMap, fileSize);
        
        std::cerr << text;
    } else {
        compressToFile(argv[1], text, charCodeMap);
    }

    STOP(seqComp, timeComp)
    std::cout << "computation: " << timeComp << " usecs" << std::endl;

    return 0;
}