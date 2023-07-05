#include <iostream>
#include <vector>
#include <stdlib.h>
#include <string>
#include <unordered_map>
#include <queue>
#include <filesystem>
#include <mutex>
#include <stdio.h>

#include "utimer.hpp"

#include "Tasks.hpp"

#include <ff/ff.hpp>

bool verify;

class ReadEmitter : public ff::ff_monode_t<FRTASK> {
private:
    char* filename;
    int fileSize;
    int nw;
    std::vector<std::unordered_map<char, unsigned>>* maps;
    std::vector<std::vector<std::pair<char, unsigned>>>* mapReds;
    std::vector<std::string>* stringReds;
    std::vector<std::mutex>* mutexes;
public:
    ReadEmitter(
        char* filename, 
        int fileSize, 
        int nw
    ) : filename(filename), 
        fileSize(fileSize), 
        nw(nw)
    {
        maps = new std::vector<std::unordered_map<char, unsigned>>(nw);
        mapReds = new std::vector<std::vector<std::pair<char, unsigned>>>(nw);
        stringReds = new std::vector<std::string>(nw);
        mutexes = new std::vector<std::mutex>(nw);
    }

    FRTASK* svc(FRTASK*) {
        for (int i = 0; i < nw; ++i) {
            auto t = new FRTASK(filename, fileSize, nw, i, maps, mapReds, stringReds, mutexes);
            ff_send_out(t);
        }

        return EOS;
    }
};

class ReadWorker : public ff::ff_node_t<FRTASK> {
    FRTASK* taskPtr;

    FRTASK* svc(FRTASK* t) {
        std::fstream file;
        file.open(t->filename);

        int delta = t->fileSize / t->nw;
        int from = t->i * delta;
        int to = t->i == t->nw - 1 ? t->fileSize : from + delta;

        file.seekg(from, std::ios::beg);

        std::string s;
        s.reserve(delta);

        char c;
        int count = from;
        while (count++ < to && file >> std::noskipws >> c) {
            ++(*t->maps)[t->i][c];
            s += c;
        }
        (*t->stringReds)[t->i] = std::move(s);

        file.close();

        for (const auto& pair : (*t->maps)[t->i]) {
            auto pairHash = std::hash<char>{}(pair.first) % t->nw;
            std::unique_lock ul((*t->mutexes)[pairHash]);
            (*t->mapReds)[pairHash].push_back(pair);
        }

        taskPtr = t;

        return GO_ON;
    }

    void eosnotify(ssize_t) {
        ff_send_out(taskPtr);
    }
};

class ReadCollector : public ff::ff_node_t<FRTASK> {
    FRTASK* taskPtr;
    
    int notifications;

public:
    ReadCollector() : notifications(0) {}
    
    FRTASK* svc(FRTASK* t) {
        if (!notifications) taskPtr = t;

        return GO_ON;
    }

    void eosnotify(ssize_t) {
        if (++notifications == taskPtr->nw) {
            delete taskPtr->maps;
            delete taskPtr->mutexes;

            ff_send_out(taskPtr);
        }
    }
};

class PairsEmitter : public ff::ff_monode_t<FRTASK, REDTASK> {
    std::vector<std::vector<std::pair<char, unsigned>>>* mapReds;
    std::vector<std::string>* stringReds;
    int nw;
public:
    REDTASK* svc(FRTASK* t) {
        mapReds = t->mapReds;
        nw = t->nw;
        stringReds = t->stringReds;

        delete t;

        std::vector<std::unordered_map<char, unsigned>>* outReducedMaps = new std::vector<std::unordered_map<char, unsigned>>(nw);
        for (int i = 0; i < nw; ++i) {
            auto t = new REDTASK(mapReds, stringReds, outReducedMaps, i, nw);
            ff_send_out(t);
        }
        
        return EOS;
    }
};

class PairsReducerWorker : public ff::ff_node_t<REDTASK> {
    REDTASK* taskPtr;

    REDTASK* svc(REDTASK* t) {
        for (const auto& [key, value] : (*t->mapReds)[t->i]) 
            (*t->outReducedMaps)[t->i][key] += value;

        taskPtr = t;
        
        return GO_ON;
    }

    void eosnotify(ssize_t) {
        ff_send_out(taskPtr);
    }
};

class PairsCollector : public ff::ff_node_t<REDTASK> {
    std::unordered_map<char, unsigned>* res;
    std::string* text;
    std::vector<std::string>* stringReds;
    std::vector<std::unordered_map<char, unsigned>>* outReducedMaps;
    int nw;

    int notifications;
public:
    PairsCollector(std::string* text) : text(text), notifications(0) 
    {
        res = new std::unordered_map<char, unsigned>;
    }

    REDTASK* svc(REDTASK* t) {
        if (!notifications) { // Assign just once
            stringReds = t->stringReds;
            nw = t->nw;
            outReducedMaps = t->outReducedMaps;
        }

        delete t;
        
        return GO_ON;
    }

    void eosnotify(ssize_t) {
        if (++notifications == nw) { // Works as a barrier
            for (int i = 0; i < nw; ++i) {
                for (const auto& [key, value] : (*outReducedMaps)[i]) 
                    (*res)[key] += value;
                *text += (*stringReds)[i];
            }

            std::vector<char>* symbols = new std::vector<char>;
            std::vector<unsigned>* freqs = new std::vector<unsigned>;

            START(symbolsAndFreqs)

            symbols->reserve(res->size());
            freqs->reserve(res->size());

            for (const auto& [sym, freq] : *res) {
                symbols->push_back(sym);
                freqs->push_back(freq);
            }

            STOP(symbolsAndFreqs, time)

            std::cout << "Time needed for computing symbols and frequencies vectors: " << time << " usecs" << std::endl;

            ff_send_out(new PARCODETASK(symbols, freqs, nw));
        }
    }

    void svc_end() {
        delete outReducedMaps;
        delete res;
    }
};

class CodesGenerationEmitter : public ff::ff_monode_t<PARCODETASK, CODESTASK> {
    std::vector<char>* symbols;
    std::vector<unsigned>* freqs;
    std::unordered_map<char, std::string>* charCodeMap;
    int nw;
public:
    CodesGenerationEmitter(std::unordered_map<char, std::string>* charCodeMap) : charCodeMap(charCodeMap) {}

    CODESTASK* svc(PARCODETASK* t) {
        symbols = t->symbols;
        freqs = t->freqs;
        nw = t->nw;

        delete t;

        std::priority_queue<std::shared_ptr<Node>, std::vector<std::shared_ptr<Node>>, cmp>* q = 
            new std::priority_queue<std::shared_ptr<Node>, std::vector<std::shared_ptr<Node>>, cmp>;
        std::mutex* queueMutex = new std::mutex;

        for (int i = 0; i < nw; ++i) {
            auto t = new CODESTASK(
                symbols,
                freqs,
                queueMutex,
                q,
                charCodeMap,
                nw,
                i
            );
            ff_send_out(t);
        }
        
        return EOS;
    }
};

class CodesGenerationWorker : public ff::ff_node_t<CODESTASK> {
    CODESTASK* taskPtr;

    CODESTASK* svc(CODESTASK* t) {
        int delta = (*t->symbols).size() / t->nw;
        int from = t->i * delta;
        int to = t->i == t->nw - 1 ? (*t->symbols).size() : from + delta;
        
        for (int j = from; j < to; ++j) {
            (*t->queueMutex).lock();
            (*t->q).push(std::make_shared<Node>((*t->symbols)[j], (*t->freqs)[j]));
            (*t->queueMutex).unlock();
        }

        taskPtr = t;

        return GO_ON;
    }

    void eosnotify(ssize_t) {
        ff_send_out(taskPtr);
    }
};

class CodesGenerationCollector : public ff::ff_node_t<CODESTASK> {
    CODESTASK* taskPtr;

    int notifications;

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

    std::shared_ptr<Node> treeGen(
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
public:
    CodesGenerationCollector() : notifications(0) {}

    CODESTASK* svc(CODESTASK* t) {
        if (!notifications) taskPtr = t; // Assign once
        
        return GO_ON;
    }

    void eosnotify(ssize_t) {
        if (++notifications == taskPtr->nw) {
            utimer t("Tree and codes generation ");

            std::shared_ptr<Node> root = treeGen(*taskPtr->q);

            generateCodes(root, "", *taskPtr->charCodeMap);
            ff_send_out(taskPtr);
        }
    }

    void svc_end() {
        delete taskPtr->queueMutex;
        delete taskPtr->q;
        delete taskPtr->freqs;
        delete taskPtr->symbols;
        // delete taskPtr;
    }
};

class CompressionEmitter : public ff::ff_node_t<CODESTASK, COMPRESSIONTASK> {
    std::string* text;
    std::unordered_map<char, std::string>* charCodeMap;
    std::string* compressedText;
    int avgCodeLen;
    int nw;
    
public:
    CompressionEmitter(
        std::string* text,
        std::string* compressedText
    ) : text(text), compressedText(compressedText) {}

    COMPRESSIONTASK* svc(CODESTASK* t) {
        charCodeMap = t->charCodeMap;
        nw = t->nw;
        
        // delete t;

        int sumSize = 0;
        for (const auto& [_, v] : *charCodeMap)
            sumSize += v.size();

        // Needed for reserving approx space for string during compression and decompression 
        avgCodeLen = sumSize / charCodeMap->size();        

        std::vector<std::string>* compressedResults = new std::vector<std::string>(nw);
        for (int i = 0; i < nw; ++i) {
            auto t = new COMPRESSIONTASK(
                text,
                charCodeMap,
                compressedResults,
                compressedText,
                avgCodeLen,
                nw,
                i
            );
            ff_send_out(t);
        }

        return EOS;
    }
};

class CompressionWorker : public ff::ff_node_t<COMPRESSIONTASK> {
    COMPRESSIONTASK* taskPtr;

    COMPRESSIONTASK* svc(COMPRESSIONTASK* t) {
        int delta = (*t->text).size() / t->nw;
        int from = t->i * delta;
        int to = t->i == t->nw - 1 ? (*t->text).size() : from + delta;

        std::string localS;
        localS.reserve(delta * t->avgCodeLen);

        for (int j = from; j < to; ++j)
            localS += (*t->charCodeMap)[(*t->text)[j]];
        
        (*t->compressedResults)[t->i] = std::move(localS);
        
        taskPtr = t;

        return GO_ON;
    }

    void eosnotify(ssize_t) {
        ff_send_out(taskPtr);
    }
};

class CompressionCollector : public ff::ff_node_t<COMPRESSIONTASK> {
    COMPRESSIONTASK* taskPtr;

    int notifications;
public:
    CompressionCollector() : notifications(0) {}

    COMPRESSIONTASK* svc(COMPRESSIONTASK* t) {
        if (!notifications) taskPtr = t;

        return GO_ON;
    }

    void eosnotify(ssize_t) {
        if (++notifications == taskPtr->nw) {
            int totSize = 0;
            for (int i = 0; i < taskPtr->nw; ++i)
                totSize += (*taskPtr->compressedResults)[i].size();
            
            (*taskPtr->compressedText).reserve(totSize);

            for (int i = 0; i < taskPtr->nw; ++i) 
                *taskPtr->compressedText += (*taskPtr->compressedResults)[i];

            if (!verify)
                ff_send_out(
                    new PARFINAL(taskPtr->compressedText, taskPtr->charCodeMap, taskPtr->avgCodeLen, taskPtr->nw)
                );
        }
    }

    void svc_end() {
        delete taskPtr->compressedResults;
        delete taskPtr;
    }
};

class ToFileCompressionEmitter : public ff::ff_node_t<PARFINAL, TOFILETASK> {
    std::string* fn;
    std::string* compressedText;
    std::vector<std::pair<int, int>>* textPositions;
    std::vector<int>* filePositions;
    int compressedFileSize;
    int nw;

public:
    ToFileCompressionEmitter(char* filename) {
        fn = new std::string("compressed_" + std::string(filename));
    }

    TOFILETASK* svc(PARFINAL* t) {
        compressedText = t->compressedText;
        nw = t->nw;

        delete t;

        compressedFileSize = compressedText->size() / 8;

        FILE* tempFile = fopen(fn->c_str(), "w");
        fseek(tempFile, compressedFileSize, SEEK_SET);
        fputc('\0', tempFile);
        fclose(tempFile);

        textPositions = new std::vector<std::pair<int, int>>(nw);
        filePositions = new std::vector<int>(nw);
        int delta = compressedText->size() / nw;
        delta += delta % 8 == 0 ? 0 : 8 - (delta % 8);
        for (int i = 0; i < nw; ++i) {
            (*textPositions)[i].first = i * delta;
            (*textPositions)[i].second = i == nw - 1 ? compressedText->size() : (i + 1) * delta;
            (*filePositions)[i] = (*textPositions)[i].first / 8;
        }

        for (int i = 0; i < nw; ++i) {
            auto t = new TOFILETASK(
                fn, 
                compressedText, 
                textPositions, 
                filePositions, 
                compressedFileSize, 
                i, 
                nw
            );
            ff_send_out(t);
        }

        return EOS;
    }
};

class ToFileCompressionWorker : public ff::ff_node_t<TOFILETASK> {
    TOFILETASK* taskPtr;

    TOFILETASK* svc(TOFILETASK* t) {
        std::fstream file;
        file.open(*t->filename);

        file.seekp((*t->filePositions)[t->i]);

        uint8_t b = 0;
        int len = 0;
        for (int j = (*t->textPositions)[t->i].first; j < (*t->textPositions)[t->i].second; ++j) { 
            if (len < 7) {
                if ((*t->compressedText)[j] == '1') ++b;
                b <<= 1;
                ++len;
            } else if (len == 7) {
                if ((*t->compressedText)[j] == '1') ++b;
                len = 0;

                file << b;
                b = 0;
            }
        }
        if (t->i == t->nw - 1) {
            for (int i = 0; i < 7 - len; ++i)
                b <<= 1;
            
            file << b;
        }

        file.close();

        taskPtr = t;

        return GO_ON;
    }

    void eosnotify(ssize_t) {
        ff_send_out(taskPtr);
    }
};

class ToFileCompressionCollector : public ff::ff_node_t<TOFILETASK> {
    TOFILETASK* taskPtr;

    int notifications;

public:
    ToFileCompressionCollector() : notifications(0) {}

    TOFILETASK* svc(TOFILETASK* t) {
        taskPtr = t;

        return EOS;
    }

    void eosnotify(ssize_t) {
        if (++notifications == taskPtr->nw) {
            delete taskPtr->filename;
            delete taskPtr->textPositions;
            delete taskPtr->filePositions;
            delete taskPtr;
        }
    }
};

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

template<typename T>
std::vector<std::unique_ptr<ff::ff_node>> createWorkers(int nw) {
    // utimer t("Time spent creating workers ");
    std::vector<std::unique_ptr<ff::ff_node>> workers;
    for (int i = 0; i < nw; ++i)
        workers.push_back(std::make_unique<T>());
    return workers;
}

int main(int argc, char** argv) {
    if (argc < 3 || argc > 4) {
        std::cout << "Usage: " << argv[0] << " filename nw [verify]" << std::endl;
        return 1;
    }

    int nw = atoi(argv[2]);
    verify = argc == 4 && argv[3][0] == 'v';

    int fileSize = std::filesystem::file_size(argv[1]);

    std::string text;
    std::unordered_map<char, std::string> charCodeMap;
    std::string compressedText;

    {
        utimer t("Total program time ");
        START(dichiarazioni)

        std::unique_ptr<ReadEmitter> mapsEmitter = std::make_unique<ReadEmitter>(argv[1], fileSize, nw);
        std::unique_ptr<ReadCollector> mapsCollector = std::make_unique<ReadCollector>();
        ff::ff_Farm<FRTASK> mapsFarm(std::move(createWorkers<ReadWorker>(nw)));
        mapsFarm.add_emitter(*mapsEmitter);
        mapsFarm.add_collector(*mapsCollector);

        std::unique_ptr<PairsEmitter> reducersEmitter = std::make_unique<PairsEmitter>();
        std::unique_ptr<PairsCollector> reducersCollector = std::make_unique<PairsCollector>(&text);
        ff::ff_Farm<REDTASK> reducersFarm(std::move(createWorkers<PairsReducerWorker>(nw)));
        reducersFarm.add_emitter(*reducersEmitter);
        reducersFarm.add_collector(*reducersCollector);

        std::unique_ptr<CodesGenerationEmitter> codesGenerationEmitter = std::make_unique<CodesGenerationEmitter>(&charCodeMap);
        std::unique_ptr<CodesGenerationCollector> codesGenerationCollector = std::make_unique<CodesGenerationCollector>();
        ff::ff_Farm<CODESTASK> codesGenerationFarm(std::move(createWorkers<CodesGenerationWorker>(nw)));
        codesGenerationFarm.add_emitter(*codesGenerationEmitter);
        codesGenerationFarm.add_collector(*codesGenerationCollector);

        std::unique_ptr<CompressionEmitter> compressionEmitter = std::make_unique<CompressionEmitter>(&text, &compressedText);
        std::unique_ptr<CompressionCollector> compressionCollector = std::make_unique<CompressionCollector>();
        ff::ff_Farm<COMPRESSIONTASK> compressionFarm(std::move(createWorkers<CompressionWorker>(nw)));
        compressionFarm.add_emitter(*compressionEmitter);
        compressionFarm.add_collector(*compressionCollector);

        ff::ff_pipeline pipe;
        pipe.add_stage(mapsFarm);
        pipe.add_stage(reducersFarm);
        pipe.add_stage(codesGenerationFarm);
        pipe.add_stage(compressionFarm);

        ff::OptLevel opt;   // No performance change
        opt.remove_collector = true;
        opt.merge_with_emitter = true;
        opt.merge_farms = true;

        if (verify) {
            // ff::optimize_static(pipe, opt);
            START(nowrite)
            pipe.run_and_wait_end();
            STOP(nowrite, time)

            std::cout << "Total time without writing: " << time << " usecs" << std::endl;
            
            std::cerr << decompressStringSequential(compressedText, charCodeMap, fileSize);
        } else {
            std::unique_ptr<ToFileCompressionEmitter> toFileCompressionEmitter = std::make_unique<ToFileCompressionEmitter>(argv[1]);
            std::unique_ptr<ToFileCompressionCollector> toFileCompressionCollector = std::make_unique<ToFileCompressionCollector>();
            ff::ff_Farm<TOFILETASK> toFileCompressionFarm(std::move(createWorkers<ToFileCompressionWorker>(nw)));
            toFileCompressionFarm.add_emitter(*toFileCompressionEmitter);
            toFileCompressionFarm.add_collector(*toFileCompressionCollector);
            
            pipe.add_stage(toFileCompressionFarm);

            STOP(dichiarazioni, time)
            std::cout << "tempo dichiarazioni: " << time << std::endl;
            // ff::optimize_static(pipe, opt);
            pipe.run_and_wait_end();
        }
    }

    return 0;
}