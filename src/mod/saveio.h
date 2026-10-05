#pragma once
#include "common.h"

// Line-preserving INI document (the .bcs/.dat save files are UTF-8 INI with CRLF).
struct IniSection {
    std::string name;  // without brackets
    std::vector<std::string> lines;
};

struct IniDoc {
    bool bom = false;
    std::vector<std::string> head;  // lines before the first section
    std::vector<IniSection> sections;

    IniSection* Find(const std::string& name);
    const IniSection* Find(const std::string& name) const;
    std::string Get(const std::string& sec, const std::string& key, const std::string& def = "") const;
    void Set(const std::string& sec, const std::string& key, const std::string& value);
};

IniDoc ParseIni(const std::string& bytes);
std::string WriteIni(const IniDoc& doc);

// A set of named files sent over the network.
struct Bundle {
    std::vector<std::pair<std::string, std::string>> files;
    void Add(const std::string& name, const std::string& data) { files.emplace_back(name, data); }
    const std::string* Get(const std::string& name) const;
    std::string Pack() const;
    bool Unpack(const std::string& data);
    size_t TotalBytes() const;
};

namespace saveio {

// Host: pack the slot's save files. File names have the slot number replaced by "{S}".
// idOffset is added to [General] IDNo so each client allocates ids from its own range.
bool BuildStateBundle(const std::wstring& slotDir, int slot, int idOffset, Bundle& out, std::string& err);
// Client: write a state bundle into the slot directory (old save files there are removed first).
bool ApplyStateBundle(const Bundle& b, const std::wstring& slotDir, int slot, std::string& err);
// Client: extract this nation's data from its own saved slot (idStart = IDNo received with the month).
bool BuildSubmission(const std::wstring& slotDir, int slot, int nationIdx, long long idStart, Bundle& out,
                     std::string& err);
// Host: merge a client's submission into the host slot files.
bool MergeSubmission(const std::wstring& slotDir, int slot, int nationIdx, const Bundle& sub, std::string& err);
// Read [General] Year/Month from a slot's .bcs.
bool ReadDate(const std::wstring& slotDir, int slot, int& year, int& month);
// Read [General] IDNo from a slot's .bcs (-1 if unavailable).
long long ReadIdNo(const std::wstring& slotDir, int slot);

}  // namespace saveio
