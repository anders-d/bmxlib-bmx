/*
 * Copyright (C) 2011, British Broadcasting Corporation
 * All Rights Reserved.
 *
 * Author: Philip de Nier
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 *     * Redistributions of source code must retain the above copyright notice,
 *       this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above copyright
 *       notice, this list of conditions and the following disclaimer in the
 *       documentation and/or other materials provided with the distribution.
 *     * Neither the name of the British Broadcasting Corporation nor the names
 *       of its contributors may be used to endorse or promote products derived
 *       from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#include <cstring>
#include <cerrno>

#include <im/XMLWriter.h>
#include <im/IMException.h>
#include <im/Logging.h>

using namespace std;
using namespace im;



static const string INTERNAL_DEFAULT_NAMESPACE_PREFIX = " ";

static const string LT   = "&lt;";
static const string GT   = "&gt;";
static const string AMP  = "&amp;";
static const string QUOT = "&quot;";
static const string APOS = "&apos;";



XMLWriter* XMLWriter::Open(string filename)
{
    FILE *xml_file = fopen(filename.c_str(), "wb");
    if (!xml_file) {
        log_error("Failed to open XML file '%s' for writing: %s\n", filename.c_str(), strerror(errno));
        return 0;
    }

    return new XMLWriter(xml_file);
}


XMLWriter::XMLWriter(FILE *xml_file)
{
    mXMLFile = xml_file;
    mPrevWriteType = NONE;
    mLevel = 0;
}

XMLWriter::~XMLWriter()
{
    size_t i;
    for (i = 0; i < mElementStack.size(); i++)
        WriteElementEnd();

    if (mXMLFile)
        fclose(mXMLFile);
}

void XMLWriter::WriteDocumentStart()
{
    IM_CHECK(mPrevWriteType == NONE);

    WriteProlog();

    mPrevWriteType = START;
}

void XMLWriter::WriteDocumentEnd()
{
    size_t i;
    for (i = 0; i < mElementStack.size(); i++)
        WriteElementEnd();
    mElementStack.clear();

    mPrevWriteType = END;
}

void XMLWriter::WriteElementStart(std::string ns, string local_name)
{
    IM_CHECK(mPrevWriteType == START ||
             mPrevWriteType == ELEMENT_START ||
             mPrevWriteType == DELAYED_ELEMENT_START ||
             mPrevWriteType == ATTRIBUTE_START ||
             mPrevWriteType == ATTRIBUTE_CONTENT ||
             mPrevWriteType == ATTRIBUTE_END ||
             mPrevWriteType == ELEMENT_CONTENT ||
             mPrevWriteType == ELEMENT_END ||
             mPrevWriteType == COMMENT ||
             mPrevWriteType == PROC_INSTRUCTION);

    if (mPrevWriteType == ATTRIBUTE_START || mPrevWriteType == ATTRIBUTE_CONTENT)
        WriteAttributeEnd();

    if (mPrevWriteType == ELEMENT_START || mPrevWriteType == ATTRIBUTE_END)
    {
        Write(">\n", 2);
    }
    else if (mPrevWriteType == ELEMENT_CONTENT ||
             mPrevWriteType == COMMENT ||
             mPrevWriteType == PROC_INSTRUCTION)
    {
        WriteElementEnd();
    }

    Element *parent_element = (mElementStack.empty() ? 0 : mElementStack.back());
    Element *element = new Element(parent_element, ns, local_name);
    mElementStack.push_back(element);

    string prefix = element->GetPrefix(ns);
    if (prefix.empty()) {
        mPrevWriteType = DELAYED_ELEMENT_START;
        return;
    }

    WriteIndent(mLevel);
    Write("<", 1);
    if (prefix != INTERNAL_DEFAULT_NAMESPACE_PREFIX) {
        Write(prefix);
        Write(":", 1);
    }
    Write(local_name);

    mLevel++;
    mPrevWriteType = ELEMENT_START;
}

void XMLWriter::DeclareNamespace(string ns, std::string prefix)
{
    IM_CHECK(mPrevWriteType == DELAYED_ELEMENT_START ||
             mPrevWriteType == ELEMENT_START);
    IM_CHECK(!ns.empty());

    IM_CHECK(!mElementStack.empty());
    Element *element = mElementStack.back();
    
    bool was_added;
    if (prefix.empty())
        was_added = element->AddNamespaceDecl(ns, INTERNAL_DEFAULT_NAMESPACE_PREFIX);
    else
        was_added = element->AddNamespaceDecl(ns, prefix);

    if (was_added) {
        if (mPrevWriteType == DELAYED_ELEMENT_START) {
            WriteIndent(mLevel);
            Write("<", 1);
            if (!prefix.empty()) {
                Write(prefix);
                Write(":", 1);
            }
            Write(element->GetLocalName());

            mLevel++;
            mPrevWriteType = ELEMENT_START;
        }

        Write(" xmlns", 6);
        if (!prefix.empty()) {
            Write(":", 1);
            Write(prefix);
        }
        Write("=\"", 2);
        WriteAttributeData(ns);
        Write("\"", 1);
    }
}

void XMLWriter::WriteAttribute(std::string ns, string local_name, string value)
{
    IM_CHECK(mPrevWriteType == ELEMENT_START ||
             mPrevWriteType == ATTRIBUTE_START ||
             mPrevWriteType == ATTRIBUTE_CONTENT ||
             mPrevWriteType == ATTRIBUTE_END);

    if (mPrevWriteType == ATTRIBUTE_START || mPrevWriteType == ATTRIBUTE_CONTENT)
        WriteAttributeEnd();

    Write(" ", 1);

    if (!ns.empty()) {
        string prefix = GetNonDefaultNSPrefix(ns);
        if (!prefix.empty()) {
            Write(prefix);
            Write(":", 1);
        }
    }

    Write(local_name);
    Write("=\"", 2);
    WriteAttributeData(value);
    Write("\"", 1);

    mPrevWriteType = ATTRIBUTE_END;
}

void XMLWriter::WriteAttributeStart(std::string ns, string local_name)
{
    IM_CHECK(mPrevWriteType == ELEMENT_START ||
             mPrevWriteType == ATTRIBUTE_START ||
             mPrevWriteType == ATTRIBUTE_CONTENT ||
             mPrevWriteType == ATTRIBUTE_END);

    if (mPrevWriteType == ATTRIBUTE_START || mPrevWriteType == ATTRIBUTE_CONTENT)
        WriteAttributeEnd();

    Write(" ", 1);

    if (!ns.empty()) {
        string prefix = GetNonDefaultNSPrefix(ns);
        if (!prefix.empty()) {
            Write(prefix);
            Write(":", 1);
        }
    }

    Write(local_name);
    Write("=\"", 2);

    mPrevWriteType = ATTRIBUTE_START;
}

void XMLWriter::WriteAttributeContent(string value)
{
    IM_CHECK(mPrevWriteType == ATTRIBUTE_START ||
             mPrevWriteType == ATTRIBUTE_CONTENT);

    WriteAttributeData(value);

    mPrevWriteType = ATTRIBUTE_CONTENT;
}

void XMLWriter::WriteAttributeEnd()
{
    IM_CHECK(mPrevWriteType == ATTRIBUTE_START ||
             mPrevWriteType == ATTRIBUTE_CONTENT);

    Write("\"", 1);

    mPrevWriteType = ATTRIBUTE_END;
}

void XMLWriter::WriteElementContent(string content)
{
    IM_CHECK(mPrevWriteType == ELEMENT_START ||
             mPrevWriteType == ATTRIBUTE_START ||
             mPrevWriteType == ATTRIBUTE_CONTENT ||
             mPrevWriteType == ATTRIBUTE_END ||
             mPrevWriteType == ELEMENT_CONTENT);

    if (mPrevWriteType == ATTRIBUTE_START || mPrevWriteType == ATTRIBUTE_CONTENT)
        WriteAttributeEnd();

    if (mPrevWriteType == ELEMENT_START || mPrevWriteType == ATTRIBUTE_END)
        Write(">", 1);

    WriteElementData(content);

    mPrevWriteType = ELEMENT_CONTENT;
}

void XMLWriter::WriteElementEnd()
{
    IM_CHECK(mPrevWriteType == ELEMENT_START ||
             mPrevWriteType == ATTRIBUTE_START ||
             mPrevWriteType == ATTRIBUTE_CONTENT ||
             mPrevWriteType == ATTRIBUTE_END ||
             mPrevWriteType == ELEMENT_CONTENT ||
             mPrevWriteType == ELEMENT_END ||
             mPrevWriteType == COMMENT ||
             mPrevWriteType == PROC_INSTRUCTION);

    mLevel--;

    IM_CHECK(!mElementStack.empty());
    Element *element = mElementStack.back();
    const string &prefix = element->GetPrefix();
    IM_CHECK(!prefix.empty());

    if (mPrevWriteType == ATTRIBUTE_START || mPrevWriteType == ATTRIBUTE_CONTENT)
        WriteAttributeEnd();

    if (mPrevWriteType == ELEMENT_START || mPrevWriteType == ATTRIBUTE_END) {
        Write("/>\n", 3);
    } else {
        if (mPrevWriteType != ELEMENT_CONTENT) {
            if (mPrevWriteType != ELEMENT_END &&
                mPrevWriteType != COMMENT &&
                mPrevWriteType != PROC_INSTRUCTION)
            {
                Write("\n", 1);
            }
            WriteIndent(mLevel);
        }

        Write("</", 2);
        if (prefix != INTERNAL_DEFAULT_NAMESPACE_PREFIX) {
            Write(prefix);
            Write(":", 1);
        }
        Write(element->GetLocalName());
        Write(">\n", 2);
    }

    mElementStack.pop_back();
    delete element;

    mPrevWriteType = ELEMENT_END;
}

void XMLWriter::WriteElement(string ns, string local_name, string content)
{
    WriteElementStart(ns, local_name);
    WriteElementContent(content);
    WriteElementEnd();
}

void XMLWriter::WriteComment(string comment)
{
    IM_CHECK(mPrevWriteType == START ||
             mPrevWriteType == ELEMENT_START ||
             mPrevWriteType == ATTRIBUTE_START ||
             mPrevWriteType == ATTRIBUTE_CONTENT ||
             mPrevWriteType == ATTRIBUTE_END ||
             mPrevWriteType == ELEMENT_END ||
             mPrevWriteType == COMMENT ||
             mPrevWriteType == PROC_INSTRUCTION);

    if (mPrevWriteType == ATTRIBUTE_START || mPrevWriteType == ATTRIBUTE_CONTENT)
        WriteAttributeEnd();

    if (mPrevWriteType == ELEMENT_START || mPrevWriteType == ATTRIBUTE_END)
        Write(">\n", 2);

    WriteIndent(mLevel);
    Write("<!--", 4);
    Write(comment);
    Write("-->\n", 4);

    if (mPrevWriteType != ELEMENT_END && mPrevWriteType != START)
        mPrevWriteType = COMMENT;
}

void XMLWriter::WriteProcInstruction(string target, string instruction)
{
    IM_CHECK(mPrevWriteType == START ||
             mPrevWriteType == ELEMENT_START ||
             mPrevWriteType == ATTRIBUTE_START ||
             mPrevWriteType == ATTRIBUTE_CONTENT ||
             mPrevWriteType == ATTRIBUTE_END ||
             mPrevWriteType == ELEMENT_END ||
             mPrevWriteType == COMMENT ||
             mPrevWriteType == PROC_INSTRUCTION);

    if (mPrevWriteType == ATTRIBUTE_START || mPrevWriteType == ATTRIBUTE_CONTENT)
        WriteAttributeEnd();

    if (mPrevWriteType == ELEMENT_START || mPrevWriteType == ATTRIBUTE_END)
        Write(">\n", 2);

    WriteIndent(mLevel);
    Write("<?", 2);
    Write(target);
    Write(" ", 1);
    Write(instruction);
    Write("?>\n", 3);

    if (mPrevWriteType != ELEMENT_END && mPrevWriteType != START)
        mPrevWriteType = PROC_INSTRUCTION;
}

void XMLWriter::WriteText(string text)
{
    Write(text);
}

void XMLWriter::Flush()
{
    fflush(mXMLFile);
}

string XMLWriter::GetPrefix(string ns)
{
    if (mElementStack.empty())
        return "";

    return mElementStack.back()->GetPrefix(ns);
}

string XMLWriter::GetNonDefaultNSPrefix(string ns)
{
    if (mElementStack.empty())
        return "";

    return mElementStack.back()->GetNonDefaultNSPrefix(ns);
}


XMLWriter::Element::Element(Element *parent_element, string ns, string local_name)
{
    mParentElement = parent_element;
    mNS = ns;
    mLocalName = local_name;
    mDefaultNS = "";

    if (parent_element) {
        mNSpaceDecls = parent_element->GetNamespaceDecls();
        mDefaultNS = parent_element->GetDefaultNamespace();
    }
    mPrefix = GetPrefix(ns);
}

XMLWriter::Element::~Element()
{
}

bool XMLWriter::Element::AddNamespaceDecl(string ns, string prefix)
{
    IM_CHECK(!ns.empty());
    IM_CHECK(!prefix.empty());

    if (prefix == INTERNAL_DEFAULT_NAMESPACE_PREFIX) {
        mDefaultNS = ns;
        if (mNS == ns)
            mPrefix = prefix;
    } else {
        pair<map<string, string>::iterator, bool> result = mNSpaceDecls.insert(pair<string, string>(ns, prefix));
        if (!result.second) {
            if (result.first->first == ns && result.first->second == prefix)
                return false;
            mNSpaceDecls.erase(result.first);
            mNSpaceDecls.insert(make_pair(ns, prefix));
        }

        if (mNS == ns && mDefaultNS != ns)
            mPrefix = prefix;
    }

    return true;
}

string XMLWriter::Element::GetPrefix(const std::string &ns) const
{
    if (mDefaultNS == ns) {
        return INTERNAL_DEFAULT_NAMESPACE_PREFIX;
    } else {
        map<string, string>::const_iterator result = mNSpaceDecls.find(ns);
        if (result == mNSpaceDecls.end())
            return "";

        return result->second;
    }
}

string XMLWriter::Element::GetNonDefaultNSPrefix(const std::string &ns) const
{
    map<string, string>::const_iterator result = mNSpaceDecls.find(ns);
    if (result == mNSpaceDecls.end())
        return "";

    return result->second;
}


void XMLWriter::WriteProlog()
{
    Write("<?xml version=\"1.0\" encoding=\"utf-8\"?>\n");
}

void XMLWriter::WriteElementData(const string &data)
{
    string escaped_data;
    escaped_data.reserve(data.size() * 2);
    const char *data_ptr = data.c_str();
    size_t i;
    for (i = 0; i < data.size(); i++) {
        if (*data_ptr == '>')
            escaped_data.append(GT);
        else if (*data_ptr == '<')
            escaped_data.append(LT);
        else if (*data_ptr == '&')
            escaped_data.append(AMP);
        else
            escaped_data.append(data_ptr, 1);

        data_ptr++;
    }

    Write(escaped_data);
}

void XMLWriter::WriteAttributeData(const string &data)
{
    string escaped_data;
    escaped_data.reserve(data.size() * 2);
    const char *data_ptr = data.c_str();
    size_t i;
    for (i = 0; i < data.size(); i++) {
        if (*data_ptr == '\"')
            escaped_data.append(QUOT);
        else if (*data_ptr == '\'')
            escaped_data.append(APOS);
        else if (*data_ptr == '&')
            escaped_data.append(AMP);
        else
            escaped_data.append(data_ptr, 1);

        data_ptr++;
    }

    Write(escaped_data);
}

void XMLWriter::WriteIndent(int level)
{
    IM_CHECK(level >= 0);

    int i;
    for (i = 0; i < level; i++)
        Write("  ", 2);
}

void XMLWriter::Write(const string &data)
{
    fwrite(data.c_str(), 1, data.size(), mXMLFile);
}

void XMLWriter::Write(const char *data)
{
    fwrite(data, 1, strlen(data), mXMLFile);
}

void XMLWriter::Write(const char *data, size_t len)
{
    fwrite(data, 1, len, mXMLFile);
}
