#pragma once
#include <algorithm>
#include <cassert>
#include <cmath>
#include <execution>
#include <map>
#include <set>
#include <string>
#include <string_view>
#include <vector>

#include "concurrent_map.h"
#include "document.h"
#include "string_processing.h"
#include "log_duration.h"

class SearchServer {
public:
    explicit SearchServer(std::string_view stop_words_text);
    explicit SearchServer(const std::string& stop_words_text);

    template <typename StringContainer>
    explicit SearchServer(const StringContainer stop_words)
        : stop_words_(MakeUniqueNonEmptyStrings(stop_words)) {
    }

public:
    std::set<int>::const_iterator begin() const;
    std::set<int>::const_iterator end() const;
    int GetDocumentCount() const;
    const std::map<std::string_view, double>& GetWordFrequencies(int document_id) const;
    void RemoveDocument(int document_id);
    void RemoveDocument(const std::execution::sequenced_policy&, int document_id);
    void RemoveDocument(const std::execution::parallel_policy&, int document_id);
    void SetStopWords(std::string_view text);
    void AddDocument(int document_id, std::string_view document, DocumentStatus status,
        const std::vector<int>& ratings);
    std::vector<Document> FindTopDocuments(std::string_view raw_query,
        const DocumentStatus search_status = DocumentStatus::ACTUAL) const;
    std::tuple<std::vector<std::string_view>, DocumentStatus>
        MatchDocument(std::string_view raw_query, int document_id) const;

    template <typename KeyMapper>
    std::vector<Document> FindTopDocuments(std::string_view raw_query, KeyMapper key_mapper) const {
        return FindTopDocuments(std::execution::seq, raw_query, key_mapper);
    }

    template <typename ExecutionPolicy>
    std::vector<Document> FindTopDocuments(ExecutionPolicy&& policy, std::string_view raw_query,
        const DocumentStatus search_status = DocumentStatus::ACTUAL) const {
        return FindTopDocuments(policy, raw_query,
            [search_status](int document_id, DocumentStatus status, int rating) {
                return status == search_status;
            });
    }

    template <typename ExecutionPolicy, typename KeyMapper>
    std::vector<Document> FindTopDocuments(ExecutionPolicy&& policy, std::string_view raw_query, 
        KeyMapper key_mapper) const {
        const Query query = ParseQuery(policy, raw_query);
        std::vector <Document> matched_documents = FindAllDocuments(policy, query, key_mapper);
        std::sort(policy, matched_documents.begin(), matched_documents.end(),
            [](const Document& lhs, const Document& rhs) {
                if (std::abs(lhs.relevance - rhs.relevance) < 1e-6) {
                    return lhs.rating > rhs.rating;
                }
                else {
                    return lhs.relevance > rhs.relevance;
                }
            });
        if (matched_documents.size() > MAX_RESULT_DOCUMENT_COUNT) {
            matched_documents.resize(MAX_RESULT_DOCUMENT_COUNT);
        }
        return matched_documents;
    }

    template <typename ExecutionPolicy>
    std::tuple<std::vector<std::string_view>, DocumentStatus>
        MatchDocument(ExecutionPolicy&& policy, std::string_view raw_query, int document_id) const {
        if (!document_ids_.count(document_id)){
            using namespace std::string_literals;
            throw std::out_of_range("out of range"s);
        }

        const Query query = ParseQuery(policy, raw_query);

        const auto word_checker =
            [this, document_id](std::string_view word) {
            const auto it = word_to_document_freqs_.find(word);
            return it != word_to_document_freqs_.end() && it->second.count(document_id);
        };

        if (std::any_of(policy, query.minus_words.begin(), query.minus_words.end(), word_checker)) {
            return { {}, documents_.at(document_id).status };
        }

        std::vector<std::string_view> matched_words;
        matched_words.reserve(query.plus_words.size());

        for_each(policy, query.plus_words.begin(), query.plus_words.end(),
            [&word_checker, &matched_words](std::string_view word) {
                if (word_checker(word)) {
                    matched_words.push_back(word);
                }
            });
        
        return { matched_words, documents_.at(document_id).status };
    }

private:
    struct DocumentData {
        int rating = 0;
        DocumentStatus status = DocumentStatus::ACTUAL;
    };

    struct QueryWord {
        std::string_view data;
        bool is_minus = false;
        bool is_stop = false;
    };

    struct Query {
        std::set<std::string_view> plus_words;
        std::set<std::string_view> minus_words;
    };

private:
    static int ComputeAverageRating(const std::vector<int>& ratings);
    static bool IsValidWord(std::string_view word);
    bool IsStopWord(std::string_view word) const;
    double ComputeWordInverseDocumentFreq(std::string_view word) const;
    std::vector<std::string_view> SplitIntoWordsNoStop(std::string_view text) const;
    QueryWord ParseQueryWord(std::string_view text) const;
    Query ParseQuery(std::string_view text) const;

    template <typename ExecutionPolicy>
    Query ParseQuery(ExecutionPolicy&& policy, std::string_view text) const {
        Query query;
        for (std::string_view word : SplitIntoWords(text)) {
            const QueryWord query_word = ParseQueryWord(policy, word);
            if (!query_word.is_stop) {
                if (query_word.is_minus) {
                    query.minus_words.insert(query_word.data);
                }
                else {
                    query.plus_words.insert(query_word.data);
                }
            }
        }
        return query;
    }

    template <typename StringContainer>
    std::set<std::string, std::less<>> MakeUniqueNonEmptyStrings(const StringContainer& strings) {
        std::set<std::string, std::less<>> non_empty_strings;
        for (std::string_view str : strings) {
            if (!IsValidWord(str)) {
                using namespace std::string_literals;
                throw std::invalid_argument("Error in spelling words"s);
            }
            if (!str.empty()) {
                non_empty_strings.insert(static_cast<std::string>(str));
            }
        }
        return non_empty_strings;
    }

    template <typename ExecutionPolicy>
    QueryWord ParseQueryWord(ExecutionPolicy&& policy, std::string_view text) const {
        if (text.empty()) {
            using namespace std::string_literals;
            throw std::invalid_argument("Empty query"s);
        }

        bool is_minus = false;
        if (text[0] == '-') {
            is_minus = true;
            text = text.substr(1);
        }
        if (text.empty() || text[0] == '-' || !IsValidWord(policy, text)) {
            using namespace std::string_literals;
            throw std::invalid_argument("Error in query"s);
        }

        return {
            text,
            is_minus,
            IsStopWord(text)
        };
    }

    template <typename ExecutionPolicy>
    static bool IsValidWord(ExecutionPolicy&& policy, std::string_view word) {
        return std::none_of(policy, 
            word.begin(), word.end(), [](char c) {
            return c >= '\0' && c < ' ';
            });
    }

    template <typename KeyMapper>
    std::vector<Document> FindAllDocuments(const std::execution::sequenced_policy&, Query query, 
        KeyMapper key_mapper) const {
        std::map<int, double> document_to_relevance;
        for (std::string_view word : query.plus_words) {
            if (word_to_document_freqs_.count(word) == 0) {
                continue;
            }
            const double inverse_document_freq = ComputeWordInverseDocumentFreq(word);
            for (const auto [document_id, term_freq] : word_to_document_freqs_.at(word)) {
                if (key_mapper(document_id, documents_.at(document_id).status, documents_.at(document_id).rating)) {
                    document_to_relevance[document_id] += term_freq * inverse_document_freq;
                }
            }
        }

        for (std::string_view word : query.minus_words) {
            if (word_to_document_freqs_.count(word) == 0) {
                continue;
            }
            for (const auto [document_id, _] : word_to_document_freqs_.at(word)) {
                document_to_relevance.erase(document_id);
            }
        }

        std::vector<Document> matched_documents;
        matched_documents.reserve(document_to_relevance.size());
        for (const auto [document_id, relevance] : document_to_relevance) {
            matched_documents.push_back({
                document_id,
                relevance,
                documents_.at(document_id).rating
                });
        }
        return matched_documents;
    }

    template <typename KeyMapper>
    std::vector<Document> FindAllDocuments(const std::execution::parallel_policy&, Query query, 
        KeyMapper key_mapper) const {
        size_t bucket_count = 77;
        ConcurrentMap<int, double> document_to_relevance_mt(bucket_count);

        for_each(std::execution::par,
            query.plus_words.begin(), query.plus_words.end(),
            [this, key_mapper, &document_to_relevance_mt](std::string_view word) {
                const double inverse_document_freq = ComputeWordInverseDocumentFreq(word);
                for (const auto [document_id, term_freq] : word_to_document_freqs_.at(word)) {
                    if (word_to_document_freqs_.count(word) &&
                        key_mapper(
                            document_id, 
                            documents_.at(document_id).status, 
                            documents_.at(document_id).rating)) {
                        document_to_relevance_mt[document_id].ref_to_value += term_freq * inverse_document_freq;
                    }
                }
            });

        std::map<int, double> document_to_relevance(document_to_relevance_mt.BuildOrdinaryMap());
        std::mutex map_mutex;
        
        for_each(std::execution::par,
            query.minus_words.begin(), query.minus_words.end(),
            [this,&document_to_relevance, &map_mutex](std::string_view word) {
                if (word_to_document_freqs_.count(word)) {
                    std::lock_guard<std::mutex> guard(map_mutex);
                    for (const auto [document_id, _] : word_to_document_freqs_.at(word)) {
                        document_to_relevance.erase(document_id);
                    }
                }
            });

        std::vector<Document> matched_documents;
        matched_documents.reserve(document_to_relevance.size());

        for_each(std::execution::par,
            document_to_relevance.begin(), document_to_relevance.end(),
            [this, &matched_documents](const auto& document) {
                matched_documents.push_back({
                document.first,
                document.second,
                documents_.at(document.first).rating
                    });
            });

        return matched_documents;
    }

private:
    std::set<std::string, std::less<>> stop_words_;
    std::set<std::string, std::less<>> words_;
    std::map<std::string_view, std::map<int, double>> word_to_document_freqs_;
    std::map<int, DocumentData> documents_;
    std::set<int> document_ids_;
    std::map<int, std::map<std::string_view, double>> document_to_word_freqs_;
};

void AddDocument(SearchServer& search_server, int document_id, const std::string& document, DocumentStatus status,
    const std::vector<int>& ratings);

void FindTopDocuments(const SearchServer& search_server, const std::string& raw_query);

void MatchDocuments(const SearchServer& search_server, const std::string& query);
