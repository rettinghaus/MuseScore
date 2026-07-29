/*
 * SPDX-License-Identifier: GPL-3.0-only
 * MuseScore-Studio-CLA-applies
 *
 * MuseScore Studio
 * Music Composition & Notation
 *
 * Copyright (C) 2026 MuseScore Limited and others
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 3 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program. If not, see <https://www.gnu.org/licenses/>.
 */

#pragma once

#include "pugixml.hpp"
#include "global/types/string.h"
#include "global/io/iodevice.h"
#include <variant>
#include <vector>
#include <string>
#include <string_view>
#include <sstream>

namespace mu::iex::musicxml {
struct IODeviceXmlWriter : public pugi::xml_writer
{
    muse::io::IODevice* m_device;
    IODeviceXmlWriter(muse::io::IODevice* dev)
        : m_device(dev)
    {}
    void write(const void* data, size_t size) override
    {
        if (m_device) {
            m_device->write(reinterpret_cast<const uint8_t*>(data), size);
        }
    }
};

class XmlWriter
{
public:
    using Value = std::variant<std::monostate, int, unsigned int, signed long int, unsigned long int, signed long long, unsigned long long,
                               double, const char*, std::string_view, muse::String>;
    using Attribute = std::pair<std::string_view, Value>;
    using Attributes = std::vector<Attribute>;

    XmlWriter() = default;
    XmlWriter(muse::io::IODevice* dev)
        : m_device(dev)
    {}
    ~XmlWriter() { flush(); }

    void setDevice(muse::io::IODevice* dev) { m_device = dev; }

    void startDocument()
    {
        pugi::xml_node decl = m_doc.prepend_child(pugi::node_declaration);
        decl.append_attribute("version") = "1.0";
        decl.append_attribute("encoding") = "UTF-8";
    }

    void writeDoctype(const muse::String& doctype)
    {
        pugi::xml_node dt = m_doc.append_child(pugi::node_doctype);
        dt.set_value(doctype.toStdString().c_str());
    }

    void startElement(const muse::AsciiStringView& name, const Attributes& attrs = {})
    {
        pugi::xml_node parent = m_nodeStack.empty() ? m_doc : m_nodeStack.back();
        pugi::xml_node child = parent.append_child(std::string(std::string_view(name)).c_str());
        m_nodeStack.push_back(child);
        addAttributes(child, attrs);
    }

    void startElement(const muse::String& name, const Attributes& attrs = {})
    {
        pugi::xml_node parent = m_nodeStack.empty() ? m_doc : m_nodeStack.back();
        pugi::xml_node child = parent.append_child(name.toStdString().c_str());
        m_nodeStack.push_back(child);
        addAttributes(child, attrs);
    }

    void startElementRaw(const muse::String& rawString)
    {
        pugi::xml_document fragment;
        std::string xmlStr = "<" + rawString.toStdString() + "/>";
        fragment.load_string(xmlStr.c_str());
        pugi::xml_node parent = m_nodeStack.empty() ? m_doc : m_nodeStack.back();
        pugi::xml_node child = parent.append_copy(fragment.first_child());
        m_nodeStack.push_back(child);
    }

    void endElement()
    {
        if (!m_nodeStack.empty()) {
            m_nodeStack.pop_back();
        }
    }

    void tag(const muse::AsciiStringView& name, const Attributes& attrs = {})
    {
        pugi::xml_node parent = m_nodeStack.empty() ? m_doc : m_nodeStack.back();
        pugi::xml_node child = parent.append_child(std::string(std::string_view(name)).c_str());
        addAttributes(child, attrs);
    }

    void tag(const muse::AsciiStringView& name, const Value& body)
    {
        pugi::xml_node parent = m_nodeStack.empty() ? m_doc : m_nodeStack.back();
        pugi::xml_node child = parent.append_child(std::string(std::string_view(name)).c_str());
        muse::String bodyStr = muse::String::decodeXmlEntities(valueToString(body));
        child.append_child(pugi::node_pcdata).set_value(bodyStr.toStdString().c_str());
    }

    void tag(const muse::AsciiStringView& name, const Value& val, const Value& def)
    {
        if (val == def) {
            return;
        }
        tag(name, val);
    }

    void tag(const muse::AsciiStringView& name, const Attributes& attrs, const Value& body)
    {
        pugi::xml_node parent = m_nodeStack.empty() ? m_doc : m_nodeStack.back();
        pugi::xml_node child = parent.append_child(std::string(std::string_view(name)).c_str());
        addAttributes(child, attrs);
        muse::String bodyStr = muse::String::decodeXmlEntities(valueToString(body));
        child.append_child(pugi::node_pcdata).set_value(bodyStr.toStdString().c_str());
    }

    void tagRaw(const muse::String& elementWithAttrs, const Value& body = Value())
    {
        startElementRaw(elementWithAttrs);
        if (!std::holds_alternative<std::monostate>(body)) {
            muse::String bodyStr = muse::String::decodeXmlEntities(valueToString(body));
            m_nodeStack.back().append_child(pugi::node_pcdata).set_value(bodyStr.toStdString().c_str());
        }
        endElement();
    }

    void element(const muse::AsciiStringView& name, const Attributes& attrs = {}) { tag(name, attrs); }
    void element(const muse::AsciiStringView& name, const Value& body) { tag(name, body); }
    void element(const muse::AsciiStringView& name, const Attributes& attrs, const Value& body) { tag(name, attrs, body); }

    void comment(const muse::String& text)
    {
        pugi::xml_node parent = m_nodeStack.empty() ? m_doc : m_nodeStack.back();
        pugi::xml_node c = parent.append_child(pugi::node_comment);
        c.set_value(text.toStdString().c_str());
    }

    void flush()
    {
        if (m_device && !m_written) {
            IODeviceXmlWriter writer(m_device);
            m_doc.save(writer, "  ", pugi::format_default, pugi::encoding_utf8);
            m_written = true;
        }
    }

    static muse::String xmlString(const muse::String& s)
    {
        return s;
    }

private:
    static muse::String valueToString(const Value& val)
    {
        return std::visit([](auto&& arg) -> muse::String {
            using T = std::decay_t<decltype(arg)>;
            if constexpr (std::is_same_v<T, std::monostate>) {
                return muse::String();
            } else if constexpr (std::is_same_v<T, int>) {
                return muse::String::fromStdString(std::to_string(arg));
            } else if constexpr (std::is_same_v<T, unsigned int>) {
                return muse::String::fromStdString(std::to_string(arg));
            } else if constexpr (std::is_same_v<T, signed long>) {
                return muse::String::fromStdString(std::to_string(arg));
            } else if constexpr (std::is_same_v<T, unsigned long>) {
                return muse::String::fromStdString(std::to_string(arg));
            } else if constexpr (std::is_same_v<T, signed long long>) {
                return muse::String::fromStdString(std::to_string(arg));
            } else if constexpr (std::is_same_v<T, unsigned long long>) {
                return muse::String::fromStdString(std::to_string(arg));
            } else if constexpr (std::is_same_v<T, double>) {
                std::stringstream ss;
                ss << arg;
                return muse::String::fromStdString(ss.str());
            } else if constexpr (std::is_same_v<T, const char*>) {
                return muse::String(arg);
            } else if constexpr (std::is_same_v<T, std::string_view>) {
                return muse::String(std::string(arg).c_str());
            } else if constexpr (std::is_same_v<T, muse::String>) {
                return arg;
            } else {
                return muse::String();
            }
        }, val);
    }

    void addAttributes(pugi::xml_node node, const Attributes& attrs)
    {
        for (const auto& attr : attrs) {
            muse::String valStr = valueToString(attr.second);
            std::string key(attr.first);
            node.append_attribute(key.c_str()) = valStr.toStdString().c_str();
        }
    }

    pugi::xml_document m_doc;
    std::vector<pugi::xml_node> m_nodeStack;
    muse::io::IODevice* m_device = nullptr;
    bool m_written = false;
};
} // namespace mu::iex::musicxml
