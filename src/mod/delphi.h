#pragma once
#include "common.h"

// Delphi (XE+, 32-bit) runtime interop: everything is resolved by name from the game's own RTTI,
// so the mod keeps working across game patches as long as names and signatures stay the same.
namespace dl {

extern uintptr_t g_base;
extern uint32_t g_size;

bool InitImage();
// True once the code section is unpacked and the key classes are present.
bool ScanClasses();

void* ClassVmt(const char* name);
const char* ClassNameOf(void* obj);
bool IsInstanceOf(void* obj, const char* cls);

// Method code address by name (published + extended RTTI), searching parent classes.
void* Method(const char* cls, const char* name);
// Instance field offset by name (published + extended RTTI), searching parent classes. -1 if missing.
int Field(const char* cls, const char* name);

// Calls a Delphi `register` routine: EAX, EDX, ECX, then remaining params pushed left to right (callee pops).
uint32_t Call(void* fn, uint32_t eax, uint32_t edx = 0, uint32_t ecx = 0, int nstack = 0, const uint32_t* stack = nullptr);

// Read a Delphi UnicodeString (pointer to first char, length at -4).
std::wstring ReadUStr(const void* p);

// A heap-allocated string literal (refcount -1) Delphi treats as a constant; copy-on-assign keeps it safe.
class ConstUStr {
public:
    explicit ConstUStr(const std::wstring& s);
    ~ConstUStr();
    uint32_t ptr() const { return (uint32_t)(uintptr_t)m_chars; }

private:
    uint8_t* m_mem;
    wchar_t* m_chars;
};

// VCL control object behind a window handle (via the ControlOfs window property), or nullptr.
void* ObjFromHwnd(HWND h);

// TList helpers (FList at +4, FCount at +8 in 32-bit RTL).
int ListCount(void* list);
void* ListItem(void* list, int i);

template <class T>
T& At(void* obj, int off) {
    return *(T*)((uint8_t*)obj + off);
}

// Byte pattern search inside [start, start+len). Pattern bytes < 0 are wildcards.
uint8_t* FindPattern(uint8_t* start, size_t len, const std::vector<int>& pat);
// Targets of E8 rel32 calls found while linearly scanning a function (stops at maxLen).
std::vector<uint8_t*> CallTargets(uint8_t* fn, size_t maxLen);

}  // namespace dl
