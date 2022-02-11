#include "remove_duplicates.h"

void RemoveDuplicates(SearchServer& search_server) {
    std::vector<int> ids_to_remove;
    std::set<std::set<std::string_view>> unique_documents_words;
    
    for (const int document_id : search_server) {
        std::set<std::string_view> words_in_document;
        for (const auto& [word, _] : search_server.GetWordFrequencies(document_id)) {
            words_in_document.insert(word);
        }
        if (unique_documents_words.count(words_in_document))
        {
            ids_to_remove.push_back(document_id);
        }
        else {
            unique_documents_words.insert(words_in_document);
        }
    }

    for (const int document_id : ids_to_remove) {
        std::cout << "Found duplicate document id " << document_id << std::endl;
        search_server.RemoveDocument(document_id);
    }
}