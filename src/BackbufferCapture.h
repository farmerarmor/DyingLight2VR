#pragma once
#include <windows.h>
#include <cstdint>
struct BackbufferReport {
    HRESULT result{};
    unsigned width{},height{},format{},samples{},label{};
    uint64_t request{},dispatchSequence{};
    uintptr_t swap{};
    wchar_t path[MAX_PATH]{};
};
void RequestBackbufferCapture(unsigned label=0);
void TryBackbufferCapture(void* swap,uint64_t dispatchSequence);
bool SaveBackbufferCapture(const wchar_t* modulePath,BackbufferReport& report);
