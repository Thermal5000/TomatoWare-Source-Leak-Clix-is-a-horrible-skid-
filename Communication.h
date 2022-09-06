#include <Windows.h>
#include <vector>
#include <iostream>
#include <Windows.h>
#include <TlHelp32.h>
#include <string>
#include "XorStr.hpp"

#define COMS

#pragma warning (disable : 4996)

enum CALL_TYPES {
	READ_PROCESS_BASE = 1,
	READ_MODULE_BASE,
	READ_MEMORY,
	READ_ARRAY,
	WRITE_MEMORY
};

typedef struct communication_structure {
	int CALL_TYPE;

	ULONG PID, TargetPID;
	ULONGLONG Size;
	UINT_PTR Address, SourceAddress;
	ULONG64 BaseAddress;
	void* Output, * BufferAddress;
	const char* ModuleName;
};

class Driver {
public:
	std::uint32_t pid = 0;
	void* HookedFunctionAddress;

	wchar_t* Convert(const char* c) {
		const size_t cSize = strlen(c) + 1;
		wchar_t* wc = new wchar_t[cSize];
		mbstowcs(wc, c, cSize);

		return wc;
	}

	void Init(uint32_t u_pid) {
		pid = u_pid;
		HMODULE Handle = LoadLibraryA(("win32u.dll"));
		if (Handle == INVALID_HANDLE_VALUE) {
			MessageBoxA(0, ("Couldnt obtain handle!"), ("Error"), 0);
		}
		else {
			uint64_t tempHookedFunctionAddress = (uint64_t)GetProcAddress(Handle, ("NtDxgkCreateTrackedWorkload"));
			HookedFunctionAddress = (void*)(tempHookedFunctionAddress);
		}
	}

	template<typename ... Arg> uint64_t CallHookHandler(const Arg ... args) {
		if (!HookedFunctionAddress) {
			MessageBoxA(0, "Hooked function is NULL", "Error", 0);
			return 0;
		}
		auto func = static_cast<uint64_t(__stdcall*)(Arg...)>(HookedFunctionAddress);
		if (func == 0) {
			MessageBoxA(0, "Casted function is null", "Error", 0);
			return 0;
		}
		return func(args ...);
	}

	template <typename type> type RPM(uintptr_t address) {

		type response{};

		communication_structure coms;
		coms.CALL_TYPE = CALL_TYPES::READ_MEMORY;
		coms.PID = pid;
		coms.Address = address;
		coms.Size = sizeof(type);
		coms.Output = &response;
		CallHookHandler(&coms);

		return response;

	}

	bool WPM(uintptr_t address, uintptr_t source, size_t size) {
		if (!pid) return 0;

		communication_structure coms;
		coms.CALL_TYPE = CALL_TYPES::WRITE_MEMORY;
		coms.PID = pid;
		coms.Address = address;
		coms.Size = size;
		coms.BufferAddress = (void*)source;

		CallHookHandler(&coms);
		return true;
	}

	uint64_t CpyMem(std::uint32_t pid, std::uint32_t dest_pid, uintptr_t source_address, uintptr_t dest_address, size_t size) {
		if (!pid) return 0;

		communication_structure coms;
		coms.CALL_TYPE = CALL_TYPES::READ_ARRAY;
		coms.PID = pid;
		coms.TargetPID = dest_pid;
		coms.Address = dest_address;
		coms.Size = size;
		coms.SourceAddress = source_address;

		CallHookHandler(&coms);

		void* ReturnAddress = coms.Output;

		return (uint64_t)ReturnAddress;
	}

	uint32_t ReadMemorybuffer(uint64_t address, PVOID buffer, size_t size)
	{
		if (!pid) return 0;

		if (address == 0)
			return false;
		return CpyMem(pid, GetCurrentProcessId(), address, uintptr_t(buffer), size);

	}

	uint64_t ReadMemoryChain(uint64_t base, const std::vector<uint64_t>& offsets) {
		if (!pid) return 0;
		uint64_t result = Driver::RPM<uint64_t>(base + offsets.at(0));
		for (int i = 1; i < offsets.size(); i++) {
			result = RPM<uint64_t>(result + offsets.at(i));
		}
		return result;
	}

	ULONG64 GetProcessBase(std::uint32_t pid) {
		communication_structure coms;
		coms.CALL_TYPE = CALL_TYPES::READ_PROCESS_BASE;
		coms.PID = pid;

		CallHookHandler(&coms);

		ULONG64 BaseAddress = 0;
		BaseAddress = coms.BaseAddress;

		return BaseAddress;
	}

	ULONG64 GetModuleBase64(const char* modname) {
		communication_structure coms;
		coms.CALL_TYPE = CALL_TYPES::READ_MODULE_BASE;
		coms.PID = pid;
		coms.ModuleName = modname;

		CallHookHandler(&coms);

		ULONG64 BaseAddress = 0;
		BaseAddress = coms.BaseAddress;

		return BaseAddress;
	}
	template<typename s>bool write(UINT_PTR a, const s& value) {
		return WPM(a, (UINT_PTR)&value, sizeof(s));
	}
};

struct HandleDisposer
{
	using pointer = HANDLE;
	void operator()(HANDLE handle) const
	{
		if (handle != NULL || handle != INVALID_HANDLE_VALUE)
		{
			CloseHandle(handle);
		}
	}
};

using unique_handle = std::unique_ptr<HANDLE, HandleDisposer>;

std::uint32_t get_process_id(std::string_view process_name)
{
	PROCESSENTRY32 processentry;
	const unique_handle snapshot_handle(CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, NULL));

	if (snapshot_handle.get() == INVALID_HANDLE_VALUE)
		return NULL;

	processentry.dwSize = sizeof(MODULEENTRY32);

	while (Process32Next(snapshot_handle.get(), &processentry) == TRUE)
	{
		if (process_name.compare(processentry.szExeFile) == NULL)
		{
			return processentry.th32ProcessID;
		}
	}
	return NULL;
}