#include "request_queue.h"

RequestQueue::RequestQueue(SearchServer& search_server)
    :search_server_(search_server) {
}

std::vector<Document> RequestQueue::AddFindRequest(std::string_view raw_query,
    DocumentStatus search_status) {
    return AddFindRequest(raw_query,
        [search_status](int document_id, DocumentStatus status, int rating) {
            return status == search_status;
        });
}

int RequestQueue::GetNoResultRequests() const {
    return empty_query_;
}

void RequestQueue::Update(const std::vector<Document>& result, std::string_view raw_query) {
    Add(raw_query, result.size());
    if (requests_.size() > kMinInDay) {
        Remove();
    }
}

void RequestQueue::Add(std::string_view raw_query, int documents_size) {
    if (documents_size == 0) {
        ++empty_query_;
    }
    requests_.push_back({ static_cast<std::string>(raw_query), documents_size });
}

void RequestQueue::Remove() {
    if (requests_.front().documents_size_ == 0) {
        --empty_query_;
    }
    requests_.pop_front();
}