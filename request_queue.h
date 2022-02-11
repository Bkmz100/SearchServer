#pragma once
#include <deque>
#include "search_server.h"

class RequestQueue {
public:
    explicit RequestQueue(SearchServer& search_server);

public:
    template <typename DocumentPredicate>
    std::vector<Document> AddFindRequest(std::string_view raw_query, DocumentPredicate document_predicate) {
        auto result = search_server_.FindTopDocuments(raw_query, document_predicate);
        Update(result, raw_query);
        return result;
    }

    std::vector<Document> AddFindRequest(std::string_view raw_query,
        DocumentStatus search_status = DocumentStatus::ACTUAL);

    int GetNoResultRequests() const;

private:
    struct QueryResult {
        std::string raw_query_;
        int documents_size_ = 0;
    };

private:
    static const int kMinInDay = 1440;

private:
    void Update(const std::vector<Document>& result, std::string_view raw_query);
    void Add(const std::string_view, int documents_size);
    void Remove();

private:
    std::deque<QueryResult> requests_;
    int empty_query_ = 0;
    SearchServer& search_server_;
};