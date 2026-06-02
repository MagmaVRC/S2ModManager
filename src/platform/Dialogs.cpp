#include "Dialogs.h"
#include "../core/Paths.h"
#include <windows.h>
#include <shobjidl.h>
#include <wrl/client.h>

namespace platform {

using Microsoft::WRL::ComPtr;

std::optional<std::string> pickFolder(const char* title) {
    std::optional<std::string> result;
    HRESULT init = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);

    ComPtr<IFileOpenDialog> dlg;
    if (SUCCEEDED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_ALL, IID_PPV_ARGS(&dlg)))) {
        DWORD opts = 0;
        dlg->GetOptions(&opts);
        dlg->SetOptions(opts | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM);

        if (title) {
            std::wstring wt = core::widen(title);
            dlg->SetTitle(wt.c_str());
        }

        if (SUCCEEDED(dlg->Show(nullptr))) {
            ComPtr<IShellItem> item;
            if (SUCCEEDED(dlg->GetResult(&item))) {
                PWSTR path = nullptr;
                if (SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &path))) {
                    result = core::narrow(path);
                    CoTaskMemFree(path);
                }
            }
        }
    }

    if (SUCCEEDED(init))
        CoUninitialize();
    return result;
}

std::vector<std::string> pickArchives(const char* title) {
    std::vector<std::string> results;
    HRESULT init = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);

    ComPtr<IFileOpenDialog> dlg;
    if (SUCCEEDED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_ALL, IID_PPV_ARGS(&dlg)))) {
        DWORD opts = 0;
        dlg->GetOptions(&opts);
        dlg->SetOptions(opts | FOS_ALLOWMULTISELECT | FOS_FORCEFILESYSTEM | FOS_FILEMUSTEXIST);

        const COMDLG_FILTERSPEC filters[] = {
            { L"Mod archives", L"*.zip;*.7z;*.rar;*.pak" },
            { L"All files",    L"*.*" },
        };
        dlg->SetFileTypes(2, filters);

        if (title) {
            std::wstring wt = core::widen(title);
            dlg->SetTitle(wt.c_str());
        }

        if (SUCCEEDED(dlg->Show(nullptr))) {
            ComPtr<IShellItemArray> items;
            if (SUCCEEDED(dlg->GetResults(&items))) {
                DWORD count = 0;
                items->GetCount(&count);
                for (DWORD i = 0; i < count; ++i) {
                    ComPtr<IShellItem> item;
                    if (SUCCEEDED(items->GetItemAt(i, &item))) {
                        PWSTR path = nullptr;
                        if (SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &path))) {
                            results.push_back(core::narrow(path));
                            CoTaskMemFree(path);
                        }
                    }
                }
            }
        }
    }

    if (SUCCEEDED(init))
        CoUninitialize();
    return results;
}

std::optional<std::string> saveFile(const char* title, const char* defaultName,
                                    const wchar_t* filterName, const wchar_t* filterPattern,
                                    const wchar_t* defaultExt) {
    std::optional<std::string> result;
    HRESULT init = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);

    ComPtr<IFileSaveDialog> dlg;
    if (SUCCEEDED(CoCreateInstance(CLSID_FileSaveDialog, nullptr, CLSCTX_ALL, IID_PPV_ARGS(&dlg)))) {
        DWORD opts = 0;
        dlg->GetOptions(&opts);
        dlg->SetOptions(opts | FOS_FORCEFILESYSTEM | FOS_OVERWRITEPROMPT);

        const COMDLG_FILTERSPEC filters[] = {
            { filterName, filterPattern },
            { L"All files", L"*.*" },
        };
        dlg->SetFileTypes(2, filters);
        if (defaultExt)
            dlg->SetDefaultExtension(defaultExt);
        if (defaultName) {
            std::wstring wn = core::widen(defaultName);
            dlg->SetFileName(wn.c_str());
        }
        if (title) {
            std::wstring wt = core::widen(title);
            dlg->SetTitle(wt.c_str());
        }

        if (SUCCEEDED(dlg->Show(nullptr))) {
            ComPtr<IShellItem> item;
            if (SUCCEEDED(dlg->GetResult(&item))) {
                PWSTR path = nullptr;
                if (SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &path))) {
                    result = core::narrow(path);
                    CoTaskMemFree(path);
                }
            }
        }
    }

    if (SUCCEEDED(init))
        CoUninitialize();
    return result;
}

std::optional<std::string> pickFile(const char* title,
                                    const wchar_t* filterName, const wchar_t* filterPattern) {
    std::optional<std::string> result;
    HRESULT init = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);

    ComPtr<IFileOpenDialog> dlg;
    if (SUCCEEDED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_ALL, IID_PPV_ARGS(&dlg)))) {
        DWORD opts = 0;
        dlg->GetOptions(&opts);
        dlg->SetOptions(opts | FOS_FORCEFILESYSTEM | FOS_FILEMUSTEXIST);

        const COMDLG_FILTERSPEC filters[] = {
            { filterName, filterPattern },
            { L"All files", L"*.*" },
        };
        dlg->SetFileTypes(2, filters);
        if (title) {
            std::wstring wt = core::widen(title);
            dlg->SetTitle(wt.c_str());
        }

        if (SUCCEEDED(dlg->Show(nullptr))) {
            ComPtr<IShellItem> item;
            if (SUCCEEDED(dlg->GetResult(&item))) {
                PWSTR path = nullptr;
                if (SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &path))) {
                    result = core::narrow(path);
                    CoTaskMemFree(path);
                }
            }
        }
    }

    if (SUCCEEDED(init))
        CoUninitialize();
    return result;
}

void openInExplorer(const std::string& path) {
    std::wstring wp = core::widen(path);
    ShellExecuteW(nullptr, L"open", wp.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
}

}
