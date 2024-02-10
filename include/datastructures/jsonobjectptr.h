#pragma once
#include <json-c/json.h>
#include <json-c/json_util.h>

struct json_object_ptr {
    json_object_ptr(json_object* obj = nullptr)
        : m_obj { obj }
    {
    }

    json_object_ptr& operator=(json_object_ptr const& other)
    {
        if (m_obj) {
            json_object_put(m_obj);
            m_obj = nullptr;
        }
        m_obj = other.m_obj;
        json_object_get(m_obj);
        return *this;
    }
    json_object_ptr(json_object_ptr const& other)
        : m_obj { nullptr }
    {
        *this = other;
    }

    json_object_ptr& operator=(json_object_ptr&& other)
    {
        if (m_obj) {
            json_object_put(m_obj);
            m_obj = nullptr;
        }
        m_obj = other.m_obj;
        other.m_obj = nullptr;
        return *this;
    }
    json_object_ptr(json_object_ptr&& other)
        : m_obj { nullptr }
    {
        *this = std::move(other);
    }

    ~json_object_ptr()
    {
        json_object_put(m_obj);
        m_obj = nullptr;
    }

    operator bool()
    {
        return m_obj != NULL;
    }

    bool operator==(const json_object_ptr& other) const
    {
        return this->m_obj == other.m_obj;
    }

    bool operator!=(const json_object_ptr& other) const
    {
        return this->m_obj != other.m_obj;
    }

    bool operator<(const json_object_ptr& other) const
    {
        return this->m_obj < other.m_obj;
    }

    operator json_object*()
    {
        return m_obj;
    }

private:
    json_object* m_obj;
};
