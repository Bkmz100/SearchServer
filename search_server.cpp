#include "search_server.h"

SearchServer::SearchServer(std::string_view stop_words_text)
    : SearchServer(SplitIntoWords(stop_words_text)) {
}

SearchServer::SearchServer(const std::string& stop_words_text)
    : SearchServer(SplitIntoWords(stop_words_text)) {
}

int SearchServer::GetDocumentCount() const {
    return documents_.size();
}

std::set<int>::const_iterator SearchServer::begin() const {
    return document_ids_.begin();
}

std::set<int>::const_iterator SearchServer::end() const {
    return document_ids_.end();
}

const std::map<std::string_view, double>& SearchServer::GetWordFrequencies(int document_id) const {
    static const std::map<std::string_view, double> empty_map;
    if (document_to_word_freqs_.count(document_id)) {
        return document_to_word_freqs_.at(document_id);
    }

    return empty_map;
}

void SearchServer::RemoveDocument(int document_id) {
    return RemoveDocument(std::execution::seq, document_id);
}

void SearchServer::RemoveDocument(const std::execution::sequenced_policy&, int document_id) {
    const auto document_found_it = document_ids_.find(document_id);

    if (document_found_it == document_ids_.end()) {
        return;
    }

    document_ids_.erase(document_found_it);
    documents_.erase(document_id);
    
    for (auto& [word, _] : document_to_word_freqs_.at(document_id)) {
        word_to_document_freqs_.at(word).erase(document_id);
    }

    document_to_word_freqs_.erase(document_id);
}

void SearchServer::RemoveDocument(const std::execution::parallel_policy&, int document_id) {
    const auto document_found_it = document_ids_.find(document_id);

    if (document_found_it == document_ids_.end()) {
        return;
    }

    document_ids_.erase(document_found_it);
    documents_.erase(document_id);
    
    const auto& word_freqs = document_to_word_freqs_.at(document_id);

    for_each(std::execution::par,
        word_freqs.begin(), word_freqs.end(),
        [this, document_id](const auto& item) {
            word_to_document_freqs_.at(item.first).erase(document_id);
        });

    document_to_word_freqs_.erase(document_id);
}

void SearchServer::SetStopWords(std::string_view text) {
    for (std::string_view word : SplitIntoWords(text)) {
        stop_words_.insert(static_cast<std::string>(word));
    }
}

void SearchServer::AddDocument(int document_id, std::string_view document, DocumentStatus status,
    const std::vector<int>& ratings) {
    if ((document_id < 0) || (documents_.count(document_id) > 0)) {
        using namespace std::string_literals;
        throw std::invalid_argument("Invalid document ID"s);
    }

    const std::vector<std::string_view> words = SplitIntoWordsNoStop(document);
    const double inv_word_count = 1.0 / words.size();

    for (std::string_view word : words) {
        auto it = words_.insert(static_cast<std::string>(word));
        word_to_document_freqs_[*it.first][document_id] += inv_word_count;
        document_to_word_freqs_[document_id][*it.first] += inv_word_count;
    }
   
    documents_.emplace(document_id,
        DocumentData{
            ComputeAverageRating(ratings),
            status
        });

    document_ids_.insert(document_id);
}

std::vector<Document> SearchServer::FindTopDocuments(std::string_view raw_query,
    const DocumentStatus search_status) const {
    return FindTopDocuments(std::execution::seq, raw_query, search_status);
}

std::tuple<std::vector<std::string_view>, DocumentStatus>
SearchServer::MatchDocument(std::string_view raw_query, int document_id) const {
    return MatchDocument(std::execution::seq, raw_query, document_id);
}

int SearchServer::ComputeAverageRating(const std::vector<int>& ratings) {
    int rating_sum = 0;
    for (const int rating : ratings) {
        rating_sum += rating;
    }
    return rating_sum / static_cast<int>(ratings.size());
}

bool SearchServer::IsStopWord(std::string_view word) const {
    return stop_words_.count(word) > 0;
}

double SearchServer::ComputeWordInverseDocumentFreq(std::string_view word) const {
    assert(word_to_document_freqs_.count(word) > 0
        && "Word must be contained in search query");
    int word_size = word_to_document_freqs_.at(word).size();
    assert(word_size != 0 && "Division by zero");
    return log(GetDocumentCount() * 1.0 / word_size);
}

bool SearchServer::IsValidWord(std::string_view word) {
    return IsValidWord(std::execution::seq, word);
}

std::vector<std::string_view> SearchServer::SplitIntoWordsNoStop(std::string_view text) const {
    std::vector<std::string_view> words;
    for (std::string_view word : SplitIntoWords(text)) {
        if (!IsValidWord(word)) {
            using namespace std::string_literals;
            throw std::invalid_argument("Error in spelling words"s);
        }
        if (!IsStopWord(word)) {
            words.push_back(word);
        }
    }
    return words;
}

SearchServer::QueryWord SearchServer::ParseQueryWord(std::string_view text) const {
    return ParseQueryWord(std::execution::seq, text);
}

SearchServer::Query SearchServer::ParseQuery(std::string_view text) const {
    return ParseQuery(std::execution::seq, text);
}

void AddDocument(SearchServer& search_server, int document_id, std::string_view document, DocumentStatus status,
    const std::vector<int>& ratings) {
    try {
        search_server.AddDocument(document_id, document, status, ratings);
    }
    catch (const std::invalid_argument& e) {
        using namespace std::string_literals;
        std::cout << "Error when adding document "s << document_id << ": "s << e.what() << std::endl;
    }
}

void FindTopDocuments(const SearchServer& search_server, std::string_view raw_query) {
    using namespace std::string_literals;
    LOG_DURATION_STREAM("Operation time", std::cout);
    std::cout << "Search result for the query: "s << raw_query << std::endl;
    try {
        for (const Document& document : search_server.FindTopDocuments(raw_query)) {
            PrintDocument(document);
        }
    }
    catch (const std::invalid_argument& e) {
        std::cout << "Search error: "s << e.what() << std::endl;
    }
}

void MatchDocuments(const SearchServer& search_server, std::string_view query) {
    using namespace std::string_literals;
    LOG_DURATION_STREAM("Operation time", std::cout);
    try {
        std::cout << "Matching documents on query: "s << query << std::endl;
        for (const int document_id : search_server) {
            const auto [words, status] = search_server.MatchDocument(query, document_id);
            PrintMatchDocumentResult(document_id, words, status);
        }
    }
    catch (const std::invalid_argument& e) {
        std::cout << "Error matching documents on query "s << query << ": "s << e.what() << std::endl;
    }
}