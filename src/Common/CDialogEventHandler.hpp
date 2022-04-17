#pragma once

#include "common.hpp"

#ifdef _WIN32
#pragma comment(lib, "shlwapi.lib")
#pragma comment(lib, "runtimeobject.lib")
#pragma comment(lib, "propsys.lib")
#include <windows.h>      // For common windows data types and function headers
#define STRICT_TYPED_ITEMIDS
#include <shlobj.h>
#include <objbase.h>      // For COM headers
#include <shobjidl.h>     // for IFileDialogEvents and IFileDialogControlEvents
#include <shlwapi.h>
#include <knownfolders.h> // for KnownFolder APIs/datatypes/function headers
#include <propvarutil.h>  // for PROPVAR-related functions
#include <propkey.h>      // for the Property key APIs/datatypes
#include <propidl.h>      // for the Property System APIs
#include <strsafe.h>      // for StringCchPrintfW
#include <shtypes.h>      // for COMDLG_FILTERSPEC
#endif

namespace stm {

#ifdef _WIN32

class CDialogEventHandler : public IFileDialogEvents, public IFileDialogControlEvents {
public:
	// IUnknown methods
	inline IFACEMETHODIMP QueryInterface(REFIID riid, void** ppv) {
		static const QITAB qit[] = {
			QITABENT(CDialogEventHandler, IFileDialogEvents),
			QITABENT(CDialogEventHandler, IFileDialogControlEvents),
			{ 0 },
		};
		return QISearch(this, qit, riid, ppv);
	}

	inline IFACEMETHODIMP_(ULONG) AddRef() {
		return InterlockedIncrement(&_cRef);
	}

	inline IFACEMETHODIMP_(ULONG) Release() {
		long cRef = InterlockedDecrement(&_cRef);
		if (!cRef)
			delete this;
		return cRef;
	}

	// IFileDialogEvents methods
	inline IFACEMETHODIMP OnFileOk(IFileDialog *) { return S_OK; };
	inline IFACEMETHODIMP OnFolderChange(IFileDialog *) { return S_OK; };
	inline IFACEMETHODIMP OnFolderChanging(IFileDialog *, IShellItem *) { return S_OK; };
	inline IFACEMETHODIMP OnHelp(IFileDialog *) { return S_OK; };
	inline IFACEMETHODIMP OnSelectionChange(IFileDialog *) { return S_OK; };
	inline IFACEMETHODIMP OnShareViolation(IFileDialog *, IShellItem *, FDE_SHAREVIOLATION_RESPONSE *) { return S_OK; };
	STRATUM_API IFACEMETHODIMP OnTypeChange(IFileDialog *pfd);
	inline IFACEMETHODIMP OnOverwrite(IFileDialog *, IShellItem *, FDE_OVERWRITE_RESPONSE *) { return S_OK; };

	// IFileDialogControlEvents methods
	STRATUM_API IFACEMETHODIMP OnItemSelected(IFileDialogCustomize *pfdc, DWORD dwIDCtl, DWORD dwIDItem);
	inline IFACEMETHODIMP OnButtonClicked(IFileDialogCustomize *, DWORD) { return S_OK; };
	inline IFACEMETHODIMP OnCheckButtonToggled(IFileDialogCustomize *, DWORD, BOOL) { return S_OK; };
	inline IFACEMETHODIMP OnControlActivating(IFileDialogCustomize *, DWORD) { return S_OK; };

	inline CDialogEventHandler() : _cRef(1) {};
private:
	inline ~CDialogEventHandler() {};
	long _cRef;
};

STRATUM_API HRESULT CDialogEventHandler_CreateInstance(REFIID riid, void **ppv);
#endif

STRATUM_API fs::path file_dialog(HWND owner);
STRATUM_API fs::path folder_dialog(HWND owner);

}