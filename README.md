# SearchServer

Поисковый движок с поддержкой плюс, минус и стоп-слов. Реализована разбивка на страницы.

Реализован с использованием параллельной версии map, многопоточности, итераторов и исключений.

Класс поискового сервера инициализируется стоп-словами. Система поддерживает различные типы документов: актуальные, удаленные, неактуальные и запрещенные.

# Использование
Для использования необходима установка и настройка требуемых компонентов.

Пример использования:
```
#include "process_queries.h"
#include "search_server.h"

#include <execution>
#include <iostream>
#include <string>
#include <vector>

using namespace std;

int main() {
    SearchServer search_server("and with"s);

    int id = 0;
    for (
        const string& text : {
            "white cat and yellow hat"s,
            "curly cat curly tail"s,
            "nasty dog with big eyes"s,
            "nasty pigeon john"s,
        }
        ) {
        search_server.AddDocument(++id, text, DocumentStatus::ACTUAL, { 1, 2 });
    }

    cout << "ACTUAL by default:"s << endl;
    // последовательная версия
    for (const Document& document : search_server.FindTopDocuments("curly nasty cat"s)) {
        PrintDocument(document);
    }
    cout << "BANNED:"s << endl;
    // последовательная версия
    for (const Document& document : search_server.FindTopDocuments(
        execution::seq, 
        "curly nasty cat"s, 
        DocumentStatus::BANNED)) {
        PrintDocument(document);
    }
    cout << "Even ids:"s << endl;
    // параллельная версия
    for (const Document& document : search_server.FindTopDocuments(
        execution::par, 
        "curly nasty cat"s, 
        [](int document_id, DocumentStatus status, int rating) { return document_id % 2 == 0; })) {
        PrintDocument(document);
    }

    return 0;
}
```

# Системные требования

1. C++17 (STL)
2. GCC (MinGW-w64) 11.2.0

# Планы по доработке

Добавить возможность чтение/вывод документов из JSON или используя Protobuf.
