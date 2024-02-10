#pragma once
#include "datastructures/jsonobjectptr.h"
#include <optional>
#include "datastructures/lsprequestid.h"

struct LspNonblockingResponse {
    LspRequestId request;
    std::optional<json_object_ptr> response;
    LspNonblockingResponse()
        : request(-1)
        , response(std::nullopt) {};
    LspNonblockingResponse(LspRequestId request)
        : request(request)
        , response(std::nullopt) {};
};

