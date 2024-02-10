#pragma once
// sequence ID of a LSP request. Used to get a matching response
// when asking for responses.
struct LspRequestId {
    int id = -1;
    LspRequestId() = default;
    LspRequestId(int id)
        : id(id) {};

    LspRequestId& operator=(const LspRequestId& other)
    {
        this->id = other.id;
        return *this;
    }

    bool operator<(const LspRequestId& other) const
    {
        return this->id < other.id;
    }
    bool operator==(const LspRequestId& other) const
    {
        return this->id == other.id;
    }
};
