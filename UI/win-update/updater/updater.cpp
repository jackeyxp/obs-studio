/*
 * Copyright (c) 2017-2018 Hugh Bailey <obs.jim@gmail.com>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include "updater.hpp"
#include <psapi.h>
#include <shlwapi.h>
#include <obs-config.h>
#include <util/windows/CoTaskMemPtr.hpp>

#include <future>
#include <vector>
#include <string>
#include <mutex>

using namespace std;

#pragma comment(lib, "shlwapi.lib")

#define UTF8ToWideBuf(wide, utf8) UTF8ToWide(wide, _countof(wide), utf8)
#define WideToUTF8Buf(utf8, wide) WideToUTF8(utf8, _countof(utf8), wide)

#define DEF_WEB_CLASS     L"edu.ihaoyi.cn"
#define DEF_UPDATE_URL    L"https://" DEF_WEB_CLASS L"/update_studio"
#define DEF_SMART_DATA    L"obs-smart"

enum RUN_MODE {
	kDefaultUnknown     = 0,
	kSmartUpdater		= 1,
	kSmartBuildJson     = 2,
};

static LPWSTR g_cmd_type[] = {
	L"smart", L"json_smart",
};

/* ----------------------------------------------------------------------- */
RUN_MODE   g_run_mode      = kDefaultUnknown;
HANDLE     cancelRequested = nullptr;
HANDLE     updateThread    = nullptr;
HINSTANCE  hinstMain       = nullptr;
HWND       hwndMain        = nullptr;
HCRYPTPROV hProvider       = 0;

static bool bExiting     = false;
static bool updateFailed = false;
static bool is32bit      = false;

static bool downloadThreadFailure = false;

int totalFileSize     = 0;
int completedFileSize = 0;
static int completedUpdates  = 0;

// 假定最大文件数和当前文件数...
int g_nJsonMaxFiles = 500;
int g_nJsonCurFiles = 0;

struct LastError {
	DWORD code;
	inline LastError() { code = GetLastError(); }
};

void FreeWinHttpHandle(HINTERNET handle)
{
	WinHttpCloseHandle(handle);
}

/* ----------------------------------------------------------------------- */

static inline bool UTF8ToWide(wchar_t *wide, int wideSize, const char *utf8)
{
	return !!MultiByteToWideChar(CP_UTF8, 0, utf8, -1, wide, wideSize);
}

static inline bool WideToUTF8(char *utf8, int utf8Size, const wchar_t *wide)
{
	return !!WideCharToMultiByte(CP_UTF8, 0, wide, -1, utf8, utf8Size, nullptr, nullptr);
}

static inline DWORD GetTotalFileSize(const wchar_t *path)
{
	DWORD outFileSize = 0;
	WinHandle handle = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
	if (handle != INVALID_HANDLE_VALUE) {
		outFileSize = GetFileSize(handle, nullptr);
		if (outFileSize == INVALID_FILE_SIZE) {
			outFileSize = 0;
		}
	}
	return outFileSize;
}

static inline bool is_64bit_windows(void);

static inline bool HasVS2015Redist2()
{
	wchar_t base[MAX_PATH];
	wchar_t path[MAX_PATH];
	WIN32_FIND_DATAW wfd;
	HANDLE handle;
	int folder = (is32bit && is_64bit_windows())
		? CSIDL_SYSTEMX86
		: CSIDL_SYSTEM;

	SHGetFolderPathW(NULL, folder, NULL, SHGFP_TYPE_CURRENT, base);

	StringCbCopyW(path, sizeof(path), base);
	StringCbCatW(path, sizeof(path), L"\\msvcp140.dll");
	handle = FindFirstFileW(path, &wfd);
	if (handle == INVALID_HANDLE_VALUE) {
		return false;
	} else {
		FindClose(handle);
	}

	StringCbCopyW(path, sizeof(path), base);
	StringCbCatW(path, sizeof(path), L"\\vcruntime140.dll");
	handle = FindFirstFileW(path, &wfd);
	if (handle == INVALID_HANDLE_VALUE) {
		return false;
	} else {
		FindClose(handle);
	}

	return true;
}

static bool HasVS2015Redist()
{
	PVOID old = nullptr;
	bool redirect = !!Wow64DisableWow64FsRedirection(&old);
	bool success = HasVS2015Redist2();
	if (redirect) Wow64RevertWow64FsRedirection(old);
	return success;
}

static void Status(const wchar_t *fmt, ...)
{
	wchar_t str[512];

	va_list argptr;
	va_start(argptr, fmt);

	StringCbVPrintf(str, sizeof(str), fmt, argptr);

	SetDlgItemText(hwndMain, IDC_STATUS, str);

	va_end(argptr);
}

static void CreateFoldersForPath(const wchar_t *path)
{
	wchar_t *p = (wchar_t *)path;

	while (*p) {
		if (*p == '\\' || *p == '/') {
			*p = 0;
			CreateDirectory(path, nullptr);
			*p = '\\';
		}
		p++;
	}
}

static bool MyCopyFile(const wchar_t *src, const wchar_t *dest)
try {
	WinHandle hSrc;
	WinHandle hDest;

	hSrc = CreateFile(src, GENERIC_READ, 0, nullptr, OPEN_EXISTING, FILE_FLAG_SEQUENTIAL_SCAN, nullptr);
	if (!hSrc.Valid()) throw LastError();

	hDest = CreateFile(dest, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, 0, nullptr);
	if (!hDest.Valid()) throw LastError();

	BYTE  buf[65536];
	DWORD read, wrote;

	for (;;) {
		if (!ReadFile(hSrc, buf, sizeof(buf), &read, nullptr))
			throw LastError();

		if (read == 0)
			break;

		if (!WriteFile(hDest, buf, read, &wrote, nullptr))
			throw LastError();

		if (wrote != read)
			return false;
	}

	return true;

} catch (LastError error) {
	SetLastError(error.code);
	return false;
}

static bool IsSafeFilename(const wchar_t *path)
{
	const wchar_t *p = path;

	if (!*p)
		return false;

	if (wcsstr(path, L".."))
		return false;

	if (*p == '/')
		return false;

	while (*p) {
		if (!isalnum(*p) &&
		    *p != '.' &&
		    *p != '/' &&
		    *p != '_' &&
		    *p != '-')
			return false;
		p++;
	}

	return true;
}

static bool QuickWriteFile(const wchar_t *path, const void *data, size_t size)
{
	try {
		WinHandle hDest;
		DWORD     written = 0;
		hDest = CreateFile(path, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, 0, nullptr);
		if (!hDest.Valid())
			throw LastError();
		if (!WriteFile(hDest, data, size, &written, nullptr))
			throw LastError();
	} catch (LastError error) {
		SetLastError(error.code);
		return false;
	}
}

static string QuickReadFile(const wchar_t *path)
{
	string data;

	WinHandle handle = CreateFileW(path, GENERIC_READ, 0, nullptr, OPEN_EXISTING, 0, nullptr);
	if (!handle.Valid()) {
		return string();
	}

	LARGE_INTEGER size;

	if (!GetFileSizeEx(handle, &size)) {
		return string();
	}

	data.resize((size_t)size.QuadPart);

	DWORD read;
	if (!ReadFile(handle, &data[0], (DWORD)data.size(), &read, nullptr)) {
		return string();
	}
	if (read != size.QuadPart) {
		return string();
	}

	return data;
}

static void QuickCalcFileHash(const wchar_t * lpRootPath, const wchar_t * lpFilePath, json_t * lpJson)
{
	// 如果传递的参数无效，直接返回...
	if (lpRootPath == NULL || lpFilePath == NULL || lpJson == NULL)
		return;
	// 需要先对文件全路径进行重新组合利用...
	wchar_t wFullPath[MAX_PATH] = { 0 };
	wsprintf(wFullPath, L"%s\\%s", lpRootPath, lpFilePath);
	// 如果文件大小为0，无效文件，直接返回...
	DWORD dwFileSize = GetTotalFileSize(wFullPath);
	if (dwFileSize <= 0) return;
	// 显示正在处理的文件全路径地址信息...
	Status(L"正在计算哈希: %s...", wFullPath);
	// 计算整个文件的哈希值，并转换成UTF8格式...
	BYTE    existingHash[BLAKE2_HASH_LENGTH] = { 0 };
	char    path_string[MAX_PATH] = { 0 };
	char    hash_string[BLAKE2_HASH_STR_LENGTH] = { 0 };
	wchar_t fileHashStr[BLAKE2_HASH_STR_LENGTH] = { 0 };
	if (!CalculateFileHash(wFullPath, existingHash))
		return;
	// 需要对路径进行重新处理...
	HashToString(existingHash, fileHashStr);
	WideToUTF8Buf(hash_string, fileHashStr);
	WideToUTF8Buf(path_string, lpFilePath);
	// json_object_set_new => 不会增加引用计数...
	json_t * lpObjFile = json_object();
	json_object_set_new(lpObjFile, "hash", json_string(hash_string));
	json_object_set_new(lpObjFile, "name", json_string(path_string));
	json_object_set_new(lpObjFile, "size", json_integer(dwFileSize));
	json_array_append_new(lpJson, lpObjFile);
}

static void QuickAllFiles(const wchar_t * lpRootPath, const wchar_t * lpSubPath, json_t * lpJson)
{
	bool     bIsOK = true;
	HANDLE   hFindHandle = NULL;
	wchar_t  wCurPath[MAX_PATH] = { 0 };
	wchar_t  wFindPath[MAX_PATH] = { 0 };
	WIN32_FIND_DATA  wfd = { 0 };
	// 先拷贝查询最初的根目录位置...
	StringCbCopy(wFindPath, sizeof(wFindPath), lpRootPath);
	// 如果子目录不为空，需要追加子目录，注意连接符...
	if (lpSubPath != NULL) {
		StringCbCat(wFindPath, sizeof(wFindPath), L"\\");
		StringCbCat(wFindPath, sizeof(wFindPath), lpSubPath);
	}
	// 追加文件查找需要的连接通配符...
	StringCbCat(wFindPath, sizeof(wFindPath), L"\\*.*");
	hFindHandle = FindFirstFile(wFindPath, &wfd);
	// 指定查找目录下的内容为空或无效，直接返回...
	if (hFindHandle == INVALID_HANDLE_VALUE)
		return;
	// 目录有效，开始遍历目录下所有的文件和目录...
	while (bIsOK) {
		// 如果是当前目录或上一级目录或忽略文件，继续查找下一个...
		if ((wcscmp(wfd.cFileName, L".") == 0) || (wcscmp(wfd.cFileName, L"..") == 0) || (wcscmp(wfd.cFileName, L".gitignore") == 0)) {
			bIsOK = FindNextFile(hFindHandle, &wfd);
			continue;
		}
		// 如果子目录有效，需要叠加子目录...
		if (lpSubPath != NULL) {
			StringCbCopy(wCurPath, sizeof(wCurPath), lpSubPath);
			StringCbCat(wCurPath, sizeof(wCurPath), L"/");
			StringCbCat(wCurPath, sizeof(wCurPath), wfd.cFileName);
		} else {
			StringCbCopy(wCurPath, sizeof(wCurPath), wfd.cFileName);
		}
		// 如果是有效的目录，需要进行递归操作...
		if (wfd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
			QuickAllFiles(lpRootPath, wCurPath, lpJson);
		} else if (wfd.dwFileAttributes & FILE_ATTRIBUTE_ARCHIVE) {
			// 如果是有效的文件，计算哈希值，并存入json节点当中...
			QuickCalcFileHash(lpRootPath, wCurPath, lpJson);
			// 计算已处理的文件百分比 => 大概的数字...
			int position = (int)(((float)(++g_nJsonCurFiles) / (float)g_nJsonMaxFiles) * 100.0f);
			SendDlgItemMessage(hwndMain, IDC_PROGRESS, PBM_SETPOS, position, 0);
#ifdef _DEBUG
			OutputDebugString(lpRootPath);
			OutputDebugString(L"\\");
			OutputDebugString(wCurPath);
			OutputDebugString(L"\r\n");
#endif // _DEBUG
		}
		// 继续查找下一个文件或目录...
		bIsOK = FindNextFile(hFindHandle, &wfd);
	}
	// 释放查找句柄对象...
	FindClose(hFindHandle);
}

/* ----------------------------------------------------------------------- */

enum state_t {
	STATE_INVALID,
	STATE_PENDING_DOWNLOAD,
	STATE_DOWNLOADING,
	STATE_DOWNLOADED,
	STATE_INSTALL_FAILED,
	STATE_INSTALLED,
};

struct update_t {
	wstring sourceURL;
	wstring outputPath;
	wstring tempPath;
	wstring previousFile;
	wstring basename;
	string  packageName;

	DWORD   fileSize = 0;
	BYTE    hash[BLAKE2_HASH_LENGTH];
	BYTE    downloadhash[BLAKE2_HASH_LENGTH];
	BYTE    my_hash[BLAKE2_HASH_LENGTH];
	state_t state     = STATE_INVALID;
	bool    has_hash  = false;
	bool    patchable = false;

	inline update_t() {}
	inline update_t(const update_t &from)
	        : sourceURL(from.sourceURL),
	          outputPath(from.outputPath),
	          tempPath(from.tempPath),
	          previousFile(from.previousFile),
	          basename(from.basename),
	          packageName(from.packageName),
	          fileSize(from.fileSize),
	          state(from.state),
	          has_hash(from.has_hash),
	          patchable(from.patchable)
	{
		memcpy(hash, from.hash, sizeof(hash));
		memcpy(downloadhash, from.downloadhash, sizeof(downloadhash));
		memcpy(my_hash, from.my_hash, sizeof(my_hash));
	}

	inline update_t(update_t &&from)
	        : sourceURL(std::move(from.sourceURL)),
	          outputPath(std::move(from.outputPath)),
	          tempPath(std::move(from.tempPath)),
	          previousFile(std::move(from.previousFile)),
	          basename(std::move(from.basename)),
	          packageName(std::move(from.packageName)),
	          fileSize(from.fileSize),
	          state(from.state),
	          has_hash(from.has_hash),
	          patchable(from.patchable)
	{
		from.state = STATE_INVALID;

		memcpy(hash, from.hash, sizeof(hash));
		memcpy(downloadhash, from.downloadhash, sizeof(downloadhash));
		memcpy(my_hash, from.my_hash, sizeof(my_hash));
	}

	void CleanPartialUpdate()
	{
		if (state == STATE_INSTALL_FAILED ||
			state == STATE_INSTALLED) {
			if (!previousFile.empty()) {
				DeleteFile(outputPath.c_str());
				MyCopyFile(previousFile.c_str(),
						outputPath.c_str());
				DeleteFile(previousFile.c_str());
			} else {
				DeleteFile(outputPath.c_str());
			}
		} else if (state == STATE_DOWNLOADED) {
			DeleteFile(tempPath.c_str());
		}
	}

	inline update_t &operator=(const update_t &from)
	{
	    sourceURL = from.sourceURL;
	    outputPath = from.outputPath;
	    tempPath = from.tempPath;
	    previousFile = from.previousFile;
	    basename = from.basename;
	    packageName = from.packageName;
	    fileSize = from.fileSize;
	    state = from.state;
	    has_hash = from.has_hash;
	    patchable = from.patchable;

		memcpy(hash, from.hash, sizeof(hash));
		memcpy(downloadhash, from.downloadhash, sizeof(downloadhash));
		memcpy(my_hash, from.my_hash, sizeof(my_hash));

		return *this;
	}
};

static vector<update_t> updates;
static mutex updateMutex;

static inline void CleanupPartialUpdates()
{
	for (update_t &update : updates)
		update.CleanPartialUpdate();
}

/* ----------------------------------------------------------------------- */

bool DownloadWorkerThread()
{
	const DWORD tlsProtocols = WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_2;

	HttpHandle hSession = WinHttpOpen(L"HaoYi Updater/2.1",
	                                  WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
	                                  WINHTTP_NO_PROXY_NAME,
	                                  WINHTTP_NO_PROXY_BYPASS,
	                                  0);
	if (!hSession) {
		downloadThreadFailure = true;
		Status(L"升级失败: 无法打开升级服务器。");
		return false;
	}

	WinHttpSetOption(hSession, WINHTTP_OPTION_SECURE_PROTOCOLS, (LPVOID)&tlsProtocols, sizeof(tlsProtocols));

	HttpHandle hConnect = WinHttpConnect(hSession, DEF_WEB_CLASS, INTERNET_DEFAULT_HTTPS_PORT, 0);
	if (!hConnect) {
		downloadThreadFailure = true;
		Status(L"升级失败: 无法连接到升级服务器。");
		return false;
	}

	for (;;) {
		bool foundWork = false;

		unique_lock<mutex> ulock(updateMutex);

		for (update_t &update : updates) {
			int responseCode;

			DWORD waitResult = WaitForSingleObject(cancelRequested, 0);
			if (waitResult == WAIT_OBJECT_0) {
				return false;
			}

			if (update.state != STATE_PENDING_DOWNLOAD)
				continue;

			update.state = STATE_DOWNLOADING;

			ulock.unlock();

			foundWork = true;

			if (downloadThreadFailure) {
				return false;
			}

			Status(L"正在下载 %s", update.outputPath.c_str());

			if (!HTTPGetFile(hConnect, update.sourceURL.c_str(), update.tempPath.c_str(), L"Accept-Encoding: gzip", &responseCode)) {
				downloadThreadFailure = true;
				DeleteFile(update.tempPath.c_str());
				Status(L"升级失败: 无法下载 %s (错误号 %d)", update.outputPath.c_str(), responseCode);
				return 1;
			}

			if (responseCode != 200) {
				downloadThreadFailure = true;
				DeleteFile(update.tempPath.c_str());
				Status(L"升级失败: 无法下载 %s (错误号 %d)", update.outputPath.c_str(), responseCode);
				return 1;
			}

			BYTE downloadHash[BLAKE2_HASH_LENGTH];
			if (!CalculateFileHash(update.tempPath.c_str(), downloadHash)) {
				downloadThreadFailure = true;
				DeleteFile(update.tempPath.c_str());
				Status(L"升级失败: 无法验证完整性 %s", update.outputPath.c_str());
				return 1;
			}

			if (memcmp(update.downloadhash, downloadHash, 20)) {
				downloadThreadFailure = true;
				DeleteFile(update.tempPath.c_str());
				Status(L"升级失败: 验证补丁的完整性失败 %s", update.outputPath.c_str());
				return 1;
			}

			ulock.lock();

			update.state = STATE_DOWNLOADED;
			completedUpdates++;
		}

		if (!foundWork) {
			break;
		}
		if (downloadThreadFailure) {
			return false;
		}
	}

	return true;
}

static bool RunDownloadWorkers(int num)
try {
	vector<future<bool>> thread_success_results;
	thread_success_results.resize(num);

	for (future<bool> &result : thread_success_results) {
		result = async(DownloadWorkerThread);
	}
	for (future<bool> &result : thread_success_results) {
		if (!result.get()) {
			return false;
		}
	}

	return true;

} catch (...) {
	return false;
}

/* ----------------------------------------------------------------------- */

#define WAITIFOBS_SUCCESS       0
#define WAITIFOBS_WRONG_PROCESS 1
#define WAITIFOBS_CANCELLED     2

static inline DWORD WaitIfOBS(DWORD id, const wchar_t *expected)
{
	wchar_t path[MAX_PATH];
	wchar_t *name;
	*path = 0;

	WinHandle proc = OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ | SYNCHRONIZE, false, id);
	if (!proc.Valid()) return WAITIFOBS_WRONG_PROCESS;

	if (!GetProcessImageFileName(proc, path, _countof(path)))
		return WAITIFOBS_WRONG_PROCESS;

	name = wcsrchr(path, L'\\');
	if (name) name += 1;
	else name = path;

	if (_wcsnicmp(name, expected, 5) == 0) {
		HANDLE hWait[2];
		hWait[0] = proc;
		hWait[1] = cancelRequested;

		int i = WaitForMultipleObjects(2, hWait, false, INFINITE);
		if (i == WAIT_OBJECT_0 + 1) {
			return WAITIFOBS_CANCELLED;
		}
		return WAITIFOBS_SUCCESS;
	}

	return WAITIFOBS_WRONG_PROCESS;
}

static bool WaitForParent()
{
	Status(L"正在等待父进程退出...");
	// 只处理Smart终端端的外部升级指令...
	if (g_run_mode != kSmartUpdater)
		return true;
	// 获取需要查找的进程名称标识符号...
	DWORD proc_ids[1024], needed, count;
	const wchar_t *name = g_cmd_type[g_run_mode - 1];

	if (!EnumProcesses(proc_ids, sizeof(proc_ids), &needed)) {
		return true;
	}

	count = needed / sizeof(DWORD);

	for (DWORD i = 0; i < count; i++) {
		DWORD id = proc_ids[i];
		if (id != 0) {
			switch (WaitIfOBS(id, name)) {
			case WAITIFOBS_SUCCESS:
				return true;
			case WAITIFOBS_WRONG_PROCESS:
				break;
			case WAITIFOBS_CANCELLED:
				return false;
			}
		}
	}

	return true;
}

/* ----------------------------------------------------------------------- */

static inline bool FileExists(const wchar_t *path)
{
	WIN32_FIND_DATAW wfd;
	HANDLE           hFind;

	hFind = FindFirstFileW(path, &wfd);
	if (hFind != INVALID_HANDLE_VALUE)
		FindClose(hFind);

	return hFind != INVALID_HANDLE_VALUE;
}

static bool NonCorePackageInstalled(const char *name)
{
	if (is32bit) {
		if (strcmp(name, "obs-browser") == 0) {
			return FileExists(L"obs-plugins\\32bit\\obs-browser.dll");
		} else if (strcmp(name, "realsense") == 0) {
			return FileExists(L"obs-plugins\\32bit\\win-ivcam.dll");
		}
	} else {
		if (strcmp(name, "obs-browser") == 0) {
			return FileExists(L"obs-plugins\\64bit\\obs-browser.dll");
		} else if (strcmp(name, "realsense") == 0) {
			return FileExists(L"obs-plugins\\64bit\\win-ivcam.dll");
		}
	}

	return false;
}

static inline bool is_64bit_windows(void)
{
#ifdef _WIN64
	return true;
#else
	BOOL x86 = false;
	bool success = !!IsWow64Process(GetCurrentProcess(), &x86);
	return success && !!x86;
#endif
}

static inline bool is_64bit_file(const char *file)
{
	if (!file) return false;

	return strstr(file, "64bit") != nullptr ||
	       strstr(file, "64.dll") != nullptr ||
	       strstr(file, "64.exe") != nullptr;
}

static inline bool has_str(const char *file, const char *str)
{
	return (file && str) ? (strstr(file, str) != nullptr) : false;
}

static bool AddPackageUpdateFiles(json_t *root, size_t idx, const wchar_t *tempPath)
{
	json_t *package = json_array_get(root, idx);
	json_t *name    = json_object_get(package, "name");
	json_t *files   = json_object_get(package, "files");

	bool isWin64 = is_64bit_windows();

	if (!json_is_array(files))
		return true;
	if (!json_is_string(name))
		return true;

	const wchar_t * wPackageName = g_cmd_type[g_run_mode - 1];
	const char * packageName = json_string_value(name);
	size_t fileCount = json_array_size(files);

	//wchar_t wPackageName[512] = {0};
	//if (!UTF8ToWideBuf(wPackageName, packageName))
	//	return false;

	// 注意：只处理core模块，其它模块不处理...
	if (strcmp(packageName, "core") != 0) //&& !NonCorePackageInstalled(packageName))
		return true;
	// 重置进度条的初始位置 => 默认区间是(0, 100)
	SendDlgItemMessage(hwndMain, IDC_PROGRESS, PBM_SETPOS, 0, 0);
	// 开始遍历所有的文件列表...
	for (size_t j = 0; j < fileCount; j++) {
		json_t *file     = json_array_get(files, j);
		json_t *fileName = json_object_get(file, "name");
		json_t *hash     = json_object_get(file, "hash");
		json_t *size     = json_object_get(file, "size");
		// 计算已处理文件的百分比，并显示在进度条上...
		int position = (int)(((float)(j+1) / (float)fileCount) * 100.0f);
		SendDlgItemMessage(hwndMain, IDC_PROGRESS, PBM_SETPOS, position, 0);
		// 如果文件名|哈希值|长度，无效，继续下一个文件...
		if (!json_is_string(fileName))
			continue;
		if (!json_is_string(hash))
			continue;
		if (!json_is_integer(size))
			continue;
		// 获取有效的文件名、哈希值、长度...
		const char *fileUTF8 = json_string_value(fileName);
		const char *hashUTF8 = json_string_value(hash);
		int fileSize         = (int)json_integer_value(size);

		if (strlen(hashUTF8) != BLAKE2_HASH_LENGTH * 2)
			continue;

		// 屏蔽64位文件格式的检测，会有误操作 => obs-x264.dll...
		//if (!isWin64 && is_64bit_file(fileUTF8))
		//	continue;

		/* ignore update files of opposite arch to reduce download */
		// 注意：这里是让32bit的进程只下载32bit的文件，跟系统版本无关...
		if (( is32bit && has_str(fileUTF8, "/64bit/")) ||
		    (!is32bit && has_str(fileUTF8, "/32bit/")))
			continue;

		/* convert strings to wide */

		wchar_t sourceURL[1024];
		wchar_t updateFileName[MAX_PATH];
		wchar_t updateHashStr[BLAKE2_HASH_STR_LENGTH];
		wchar_t tempFilePath[MAX_PATH];

		if (!UTF8ToWideBuf(updateFileName, fileUTF8))
			continue;
		if (!UTF8ToWideBuf(updateHashStr, hashUTF8))
			continue;

		/* make sure paths are safe */

		if (!IsSafeFilename(updateFileName)) {
			Status(L"升级失败: 不安全路径 '%s'", updateFileName);
			return false;
		}

		// 修改包名称地址，不能用原来的core，而需要与网站升级目录对应...
		StringCbPrintf(sourceURL, sizeof(sourceURL), L"%s/%s/%s", DEF_UPDATE_URL, wPackageName, updateFileName);
		StringCbPrintf(tempFilePath, sizeof(tempFilePath), L"%s\\%s", tempPath, updateHashStr);

		/* Check file hash */

		wchar_t fileHashStr[BLAKE2_HASH_STR_LENGTH];
		BYTE    existingHash[BLAKE2_HASH_LENGTH];
		bool    has_hash = false;

		/* We don't really care if this fails, it's just to avoid
		 * wasting bandwidth by downloading unmodified files */
		// 注意：以工作目录为基点，计算本地文件的哈希值，相同就不下载...
		Status(L"正在计算哈希: %s...", updateFileName);
		if (CalculateFileHash(updateFileName, existingHash)) {
			HashToString(existingHash, fileHashStr);
			// 如果本地文件的哈希值与脚本里的哈希值一致，不下载...
			if (wcscmp(fileHashStr, updateHashStr) == 0)
				continue;
			has_hash = true;
		} else {
			has_hash = false;
		}

		/* Add update file */

		update_t update;
		update.fileSize     = fileSize;
		update.basename     = updateFileName;
		update.outputPath   = updateFileName;
		update.tempPath     = tempFilePath;
		update.sourceURL    = sourceURL;
		update.packageName  = packageName;
		update.state        = STATE_PENDING_DOWNLOAD;
		update.patchable    = false;

		StringToHash(updateHashStr, update.downloadhash);
		memcpy(update.hash, update.downloadhash, sizeof(update.hash));

		update.has_hash = has_hash;
		if (has_hash) {
			StringToHash(fileHashStr, update.my_hash);
		}
		updates.push_back(move(update));

		totalFileSize += fileSize;
	}

	return true;
}

/*static void UpdateWithPatchIfAvailable(const char *name, const char *hash, const char *source, int size)
{
	wchar_t widePatchableFilename[MAX_PATH];
	wchar_t widePatchHash[MAX_PATH];
	wchar_t sourceURL[1024];
	wchar_t patchHashStr[BLAKE2_HASH_STR_LENGTH];

	if (strncmp(source, "https://cdn-fastly.obsproject.com/", 34) != 0)
		return;

	string patchPackageName = name;

	const char *slash = strchr(name, '/');
	if (!slash)
		return;

	patchPackageName.resize(slash - name);
	name = slash + 1;

	if (!UTF8ToWideBuf(widePatchableFilename, name))
		return;
	if (!UTF8ToWideBuf(widePatchHash, hash))
		return;
	if (!UTF8ToWideBuf(sourceURL, source))
		return;
	if (!UTF8ToWideBuf(patchHashStr, hash))
		return;

	for (update_t &update : updates) {
		if (update.packageName != patchPackageName)
			continue;
		if (update.basename != widePatchableFilename)
			continue;

		StringToHash(patchHashStr, update.downloadhash);

		// Replace the source URL with the patch file, mark it as
		// patchable, and re-calculate download size
		totalFileSize -= (update.fileSize - size);
		update.sourceURL = sourceURL;
		update.fileSize  = size;
		update.patchable = true;
		break;
	}
}*/

static bool UpdateFile(update_t &file)
{
	wchar_t oldFileRenamedPath[MAX_PATH];

	if (file.patchable) {
		Status(L"正在升级 %s...", file.outputPath.c_str());
	} else {
		Status(L"正在安装 %s...", file.outputPath.c_str());
	}

	/* Check if we're replacing an existing file or just installing a new one */
	DWORD attribs = GetFileAttributes(file.outputPath.c_str());

	if (attribs != INVALID_FILE_ATTRIBUTES) {
		wchar_t *curFileName = nullptr;
		wchar_t  baseName[MAX_PATH];

		StringCbCopy(baseName, sizeof(baseName), file.outputPath.c_str());

		curFileName = wcsrchr(baseName, '/');
		if (curFileName) {
			curFileName[0] = '\0';
			curFileName++;
		} else {
			curFileName = baseName;
		}

		/* Backup the existing file in case a rollback is needed */
		StringCbCopy(oldFileRenamedPath,
				sizeof(oldFileRenamedPath),
				file.outputPath.c_str());
		StringCbCat(oldFileRenamedPath,
				sizeof(oldFileRenamedPath),
				L".old");

		if (!MyCopyFile(file.outputPath.c_str(), oldFileRenamedPath)) {
			int is_sharing_violation = (GetLastError() == ERROR_SHARING_VIOLATION);

			if (is_sharing_violation) {
				Status(L"升级失败: %s 正在使用中，请关闭所有的程序再试一次！", curFileName);
			} else {
				Status(L"升级失败: 无法更新补丁 %s (错误号 %d)", curFileName, GetLastError());
			}
			return false;
		}

		file.previousFile = oldFileRenamedPath;

		int  error_code;
		bool installed_ok;

		if (file.patchable) {
			error_code = ApplyPatch(file.tempPath.c_str(), file.outputPath.c_str());
			installed_ok = (error_code == 0);

			if (installed_ok) {
				BYTE patchedFileHash[BLAKE2_HASH_LENGTH];
				if (!CalculateFileHash(file.outputPath.c_str(), patchedFileHash)) {
					Status(L"升级失败: 无法完整验证补丁 %s", curFileName);
					file.state = STATE_INSTALL_FAILED;
					return false;
				}

				if (memcmp(file.hash, patchedFileHash, BLAKE2_HASH_LENGTH) != 0) {
					Status(L"升级失败: 验证补丁的完整性失败 %s", curFileName);
					file.state = STATE_INSTALL_FAILED;
					return false;
				}
			}
		} else {
			installed_ok = MyCopyFile(file.tempPath.c_str(), file.outputPath.c_str());
			error_code = GetLastError();
		}

		if (!installed_ok) {
			int is_sharing_violation = (error_code == ERROR_SHARING_VIOLATION);

			if (is_sharing_violation) {
				Status(L"升级失败: %s 正在使用中，请关闭所有的程序再试一次！", curFileName);
			} else {
				Status(L"升级失败: 无法更新补丁 %s (错误号 %d)", curFileName, GetLastError());
			}
			file.state = STATE_INSTALL_FAILED;
			return false;
		}

		file.state = STATE_INSTALLED;
	} else {
		if (file.patchable) {
			/* Uh oh, we thought we could patch something but it's no longer there! */
			Status(L"升级失败: 源文件 %s 没有找到", file.outputPath.c_str());
			return false;
		}

		/* We may be installing into new folders,
		 * make sure they exist */
		CreateFoldersForPath(file.outputPath.c_str());

		file.previousFile = L"";

		bool success = !!MyCopyFile(file.tempPath.c_str(), file.outputPath.c_str());
		if (!success) {
			Status(L"升级失败: 无法安装 %s (错误号 %d)", file.outputPath.c_str(), GetLastError());
			file.state = STATE_INSTALL_FAILED;
			return false;
		}

		file.state = STATE_INSTALLED;
	}

	return true;
}

static wchar_t g_tempPath[MAX_PATH] = {};

static bool UpdateVS2015Redists(json_t *root)
{
	json_t *redistJson = json_object_get(root, is32bit ? "vc2015_x86" : "vc2015_x64");
	json_t *jsonFile = json_array_get(redistJson, 0);
	json_t *jsonName = json_object_get(jsonFile, "name");
	json_t *jsonHash = json_object_get(jsonFile, "hash");
	json_t *jsonSize = json_object_get(jsonFile, "size");
	const char *nameUTF8 = json_string_value(jsonName);
	const char *hashUTF8 = json_string_value(jsonHash);
	int fileSize = (int)json_integer_value(jsonSize);

	// 判断文件名是否有效...
	if (nameUTF8 == NULL || hashUTF8 == NULL || fileSize <= 0) {
		Status(L"Update failed: Could not parse VC2015 redist json");
		return false;
	}
	// 将文件名转换成宽字符形式...
	wchar_t wFileName[512] = { 0 };
	if (!UTF8ToWideBuf(wFileName, nameUTF8)) {
		Status(L"Update failed: UTF8 to WideBuf error.");
		return false;
	}

	/* ------------------------------------------ *
	 * Initialize session                         */

	const DWORD tlsProtocols = WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_2;

	HttpHandle hSession = WinHttpOpen(L"OBS Studio Updater/2.1",
	                                  WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
	                                  WINHTTP_NO_PROXY_NAME,
	                                  WINHTTP_NO_PROXY_BYPASS,
	                                  0);
	if (!hSession) {
		Status(L"Update failed: Couldn't open %s", DEF_WEB_CLASS);
		return false;
	}

	WinHttpSetOption(hSession, WINHTTP_OPTION_SECURE_PROTOCOLS, (LPVOID)&tlsProtocols, sizeof(tlsProtocols));

	HttpHandle hConnect = WinHttpConnect(hSession, DEF_WEB_CLASS, INTERNET_DEFAULT_HTTPS_PORT, 0);
	if (!hConnect) {
		Status(L"Update failed: Couldn't connect to %s", DEF_WEB_CLASS);
		return false;
	}

	int responseCode;

	DWORD waitResult = WaitForSingleObject(cancelRequested, 0);
	if (waitResult == WAIT_OBJECT_0) {
		return false;
	}

	/* ------------------------------------------ *
	 * Download redist                            */

	Status(L"Downloading %s", L"Visual C++ 2015 Redistributable");

	wstring sourceURL;
	sourceURL += DEF_UPDATE_URL;
	sourceURL += L"/";
	sourceURL += wFileName;

	wstring destPath;
	destPath += g_tempPath;
	destPath += L"\\";
	destPath += wFileName;

	// 重置文件总长度，更新完毕之后再恢复过去...
	int nSaveTotalFileSize = totalFileSize;
	totalFileSize = fileSize;
	completedFileSize = 0;
	// 从服务器上下载Visual C++ 2015 Redistributable，并更新进度条...
	if (!HTTPGetFile(hConnect, sourceURL.c_str(), destPath.c_str(), L"Accept-Encoding: gzip", &responseCode)) {
		DeleteFile(destPath.c_str());
		Status(L"Update failed: Could not download "
		       L"%s (error code %d)",
		       L"Visual C++ 2015 Redistributable",
		       responseCode);
		return false;
	}
	// 下载完毕，恢复文件总长度和已下载文件长度...
	totalFileSize = nSaveTotalFileSize;
	completedFileSize = 0;

	/* ------------------------------------------ *
	 * Get expected hash                          */

	const char *expectedHashUTF8 = hashUTF8;
	wchar_t expectedHashWide[BLAKE2_HASH_STR_LENGTH];
	BYTE expectedHash[BLAKE2_HASH_LENGTH];

	if (!UTF8ToWideBuf(expectedHashWide, expectedHashUTF8)) {
		DeleteFile(destPath.c_str());
		Status(L"Update failed: Couldn't convert Json for redist hash");
		return false;
	}

	StringToHash(expectedHashWide, expectedHash);

	wchar_t downloadHashWide[BLAKE2_HASH_STR_LENGTH];
	BYTE downloadHash[BLAKE2_HASH_LENGTH];

	/* ------------------------------------------ *
	 * Get download hash                          */

	if (!CalculateFileHash(destPath.c_str(), downloadHash)) {
		DeleteFile(destPath.c_str());
		Status(L"Update failed: Couldn't verify integrity of %s", L"Visual C++ 2015 Redistributable");
		return false;
	}

	/* ------------------------------------------ *
	 * If hashes do not match, integrity failed   */

	HashToString(downloadHash, downloadHashWide);
	if (wcscmp(expectedHashWide, downloadHashWide) != 0) {
		DeleteFile(destPath.c_str());
		Status(L"Update failed: Couldn't verify integrity of %s", L"Visual C++ 2015 Redistributable");
		return false;
	}

	/* ------------------------------------------ *
	 * If hashes match, install redist            */

	wchar_t commandline[MAX_PATH + MAX_PATH];
	StringCbPrintf(commandline, sizeof(commandline), L"%s /install /quiet /norestart", destPath.c_str());

	PROCESS_INFORMATION pi = {};
	STARTUPINFO si = {};
	si.cb = sizeof(si);

	bool success = !!CreateProcessW(destPath.c_str(), commandline,
			nullptr, nullptr, false, CREATE_NO_WINDOW,
			nullptr, nullptr, &si, &pi);
	if (success) {
		Status(L"Installing %s...", L"Visual C++ 2015 Redistributable");

		CloseHandle(pi.hThread);
		WaitForSingleObject(pi.hProcess, INFINITE);
		CloseHandle(pi.hProcess);
	} else {
		Status(L"Update failed: Could not execute "
		       L"%s (error code %d)",
		       L"Visual C++ 2015 Redistributable",
		       (int)GetLastError());
	}

	DeleteFile(destPath.c_str());

	waitResult = WaitForSingleObject(cancelRequested, 0);
	if (waitResult == WAIT_OBJECT_0) {
		return false;
	}

	return success;
}

static bool doUpdateBuildJson()
{
	Status(L"正在创建升级脚本 manifest.json 文件...");
	////////////////////////////////////////////////////////////////
	// 注意：json和txt存盘内容必须是UTF8格式，适应通用性...
	// 注意：访问路径名称必须是宽字符，与系统保持一致...
	////////////////////////////////////////////////////////////////
	// 获取manifest.json和changelog.txt的存取路径...
	wchar_t lpLogPath[MAX_PATH] = { 0 };
	wchar_t lpJsonPath[MAX_PATH] = { 0 };
	wchar_t lpRootPath[MAX_PATH] = { 0 };
	wchar_t lpBasePath[MAX_PATH] = { 0 };
	// 计算工作目录，并设定为升级进程的当前工作目录...
	GetCurrentDirectory(_countof(lpJsonPath), lpJsonPath);
	StringCbCopy(lpBasePath, sizeof(lpBasePath), lpJsonPath);

	LPCWSTR lpwBaseRoot = NULL;
	switch (g_run_mode) {
	case kSmartBuildJson: lpwBaseRoot = L"\\..\\rundir\\Release"; break;
	default:              lpwBaseRoot = L"\\..\\rundir\\Release"; break;
	}
	StringCbCat(lpBasePath, sizeof(lpBasePath), lpwBaseRoot);

	LPCWSTR lpwJsonRoot = NULL;
	switch (g_run_mode) {
	case kSmartBuildJson: lpwJsonRoot = L"\\smart"; break;
	default:              lpwJsonRoot = L"\\smart"; break;
	}
	StringCbCat(lpJsonPath, sizeof(lpJsonPath), lpwJsonRoot);

	StringCbCopy(lpLogPath, sizeof(lpLogPath), lpJsonPath);
	StringCbCat(lpLogPath, sizeof(lpLogPath), L"\\changelog.txt");
	StringCbCat(lpJsonPath, sizeof(lpJsonPath), L"\\manifest.json");
	// 计算并判断文件列表根目录的有效性...
	GetFullPathName(lpBasePath, _countof(lpRootPath), lpRootPath, nullptr);
	if ((wcslen(lpRootPath) <= 0) || !PathIsDirectory(lpRootPath)) {
		Status(L"定位根目录失败：%s", lpRootPath);
		return false;
	}
	// 读取更新日志失败，返回失败结果...
	string strLogData = QuickReadFile(lpLogPath);
	if (strLogData.size() <= 0) {
		Status(L"读取更新日志 changelog.txt 失败...");
		return false;
	}
	// 判断读取的日志文件是否是UTF8格式...
	json_t * lpLogUTF8 = json_string(strLogData.c_str());
	if (lpLogUTF8 == NULL) {
		Status(L"更新日志 changelog.txt 不是 UTF8 格式...");
		return false;
	}
	// 设置初始的进度条，并重置文件数量...
	g_nJsonCurFiles = 0;
	SendDlgItemMessage(hwndMain, IDC_PROGRESS, PBM_SETPOS, 0, 0);
	// json_decref => 靠引用计数删除，非常重要...
	// 遍历对应的发行目录所有文件列表 => 需要递归读取...
	json_t * lpJsonCoreFile = json_array();
	QuickAllFiles(lpRootPath, NULL, lpJsonCoreFile);
	// 计算vc扩展运行库的哈希并存入json对象...
	json_t * lpJsonX86File = json_array();
	json_t * lpJsonX64File = json_array();
	GetCurrentDirectory(_countof(lpRootPath), lpRootPath);
	StringCbCat(lpRootPath, sizeof(lpRootPath), is32bit ? L"\\bin\\32bit" : L"\\bin\\64bit");
	QuickCalcFileHash(lpRootPath, L"vc2015_redist.x86.exe", lpJsonX86File);
	QuickCalcFileHash(lpRootPath, L"vc2015_redist.x64.exe", lpJsonX64File);
	// 最终设定进度条的位置 => 100%...
	SendDlgItemMessage(hwndMain, IDC_PROGRESS, PBM_SETPOS, 100, 0);

	// 注意：json_decref => 靠引用计数删除，非常重要...
	// json_object_set_new不会增加引用计数...
	json_t * lpJsonPack = json_array();
	json_t * lpObjCore = json_object();
	json_object_set_new(lpObjCore, "name", json_string("core"));
	json_object_set_new(lpObjCore, "files", lpJsonCoreFile);
	json_array_append_new(lpJsonPack, lpObjCore);

	// 注意：json_decref => 靠引用计数删除，非常重要...
	// 注意：OBSJson会自动析构，保证所有关联都会自动被删除...
	// 构造json存储对象 => 字符串必须是UTF8格式...
	OBSJson maniFest(json_object());
	// 首先加入更新日志信息 => 字符串必须是UTF8格式...
	json_object_set_new(maniFest, "notes", lpLogUTF8);
	// 追加需要升级的文件列表 => 字符串必须是UTF8格式...
	json_object_set_new(maniFest, "packages", lpJsonPack);
	json_object_set_new(maniFest, "vc2015_x86", lpJsonX86File);
	json_object_set_new(maniFest, "vc2015_x64", lpJsonX64File);
	// 追加版本号码，总共三个版本信息...
	json_object_set_new(maniFest, "version_major", json_integer(LIBOBS_API_MAJOR_VER));
	json_object_set_new(maniFest, "version_minor", json_integer(LIBOBS_API_MINOR_VER));
	json_object_set_new(maniFest, "version_patch", json_integer(LIBOBS_API_PATCH_VER));
	// 特别注意：这里dump的指针，一定要强制删除，否则会有内存泄漏...
	// 获取manifest.json整个字符串信息 => 字符串必须是UTF8格式...
	char * post_body = json_dumps(maniFest, JSON_COMPACT);
	// 进行快速存盘操作 => 数据内容必须是UTF8格式...
	if (!QuickWriteFile(lpJsonPath, post_body, strlen(post_body))) {
		Status(L"保存升级脚本 manifest.json 失败...");
		free(post_body); post_body = NULL;
		return false;
	}
	// 特别注意：这里dump的指针，一定要强制删除，否则会有内存泄漏...
	free(post_body); post_body = NULL;
	// 显示创建manifest.json升级脚本文件成功...
	Status(L"已成功创建升级脚本文件manifest.json...");
	// 修改退出按钮的文本信息，并使按钮可以点击操作...
	SetDlgItemText(hwndMain, IDC_BUTTON, L"退 出");
	EnableWindow(GetDlgItem(hwndMain, IDC_BUTTON), true);
	return true;
}

static void ParseCmdLine(wchar_t *cmdLine)
{
	Status(L"正在解析外部升级指令...");

	// 改进检查命令机制，产生六种命令如下：
	// kSmartUpdater   => Smart终端升级模式
	// kSmartBuildJson => Smart终端创建升级json

	g_run_mode = kDefaultUnknown;
	LPWSTR *argv = NULL;
	int argc = 0;

	do {
		// 如果命令行内容为空，返回失败...
		if (cmdLine[0] == NULL) break;
		// 只能有一个外部指令，否则返回失败...
		argv = CommandLineToArgvW(cmdLine, &argc);
		if (argc != 1) break;
		// 查找指令对应的编号，需要计算外部指令的个数...
		int nMaxTypeNum = sizeof(g_cmd_type) / sizeof(g_cmd_type[0]);
		for (int i = 0; i < nMaxTypeNum; ++i) {
			if (wcsicmp(argv[0], g_cmd_type[i]) == 0) {
				g_run_mode = (RUN_MODE)(i + 1); break;
			}
		}
	} while (false);
	// 释放临时分配的空间...
	if (argv != NULL) {
		LocalFree((HLOCAL)argv);
		argv = NULL; argc = 0;
	}
	// 不同的外部命令，设定不同的标题栏名称...
	wchar_t wWndTitle[MAX_PATH] = { 0 };
	GetWindowText(hwndMain, wWndTitle, MAX_PATH);
	switch (g_run_mode)
	{
	case kSmartBuildJson: StringCbCat(wWndTitle, MAX_PATH, L" - Smart终端 - 重建脚本..."); break;
	case kSmartUpdater:   StringCbCat(wWndTitle, MAX_PATH, L" - Smart终端 - 正在升级..."); break;
	default:                StringCbCat(wWndTitle, MAX_PATH, L" - 错误命令"); break;
	}
	// 设定更新后的窗口标题名称内容...
	SetWindowText(hwndMain, wWndTitle);
}

static bool Update(wchar_t *cmdLine)
{
	/* ------------------------------------- *
	 * Check to make sure OBS isn't running  */
	HANDLE hObsUpdateMutex = OpenMutexW(SYNCHRONIZE, false, L"OBSStudioUpdateMutex");
	// 如果已经有升级进程在运行，需要等待之前的升级进程退出...
	if (hObsUpdateMutex != NULL) {
		HANDLE hWait[2] = { 0 };
		hWait[0] = hObsUpdateMutex;
		hWait[1] = cancelRequested;

		int i = WaitForMultipleObjects(2, hWait, false, INFINITE);

		if (i == WAIT_OBJECT_0) {
			ReleaseMutex(hObsUpdateMutex);
		}
		CloseHandle(hObsUpdateMutex);
		if (i == WAIT_OBJECT_0 + 1)
			return false;
	}
	// 解析命令行，得到命令状态...
	ParseCmdLine(cmdLine);
	// 等待父进程退出，老师端或学生端...
	if (!WaitForParent())
		return false;

	/* ------------------------------------- *
	 * Init crypt stuff                      */

	CryptProvider hProvider;
	if (!CryptAcquireContext(&hProvider, nullptr, MS_ENH_RSA_AES_PROV, PROV_RSA_AES, CRYPT_VERIFYCONTEXT)) {
		Status(L"升级失败: CryptAcquireContext 失败");
		return false;
	}

	::hProvider = hProvider;

	/* ------------------------------------- */

	// 如果解析外部命令失败，显示信息并返回...
	if (g_run_mode <= kDefaultUnknown || g_run_mode > kSmartBuildJson) {
		Status(L"升级失败：解析外部指令错误 => {smart,json_smart}");
		return false;
	}
	// 如果是创建json文件命令，进行跳转分发...
	if (g_run_mode == kSmartBuildJson) {
		return doUpdateBuildJson();
	}
	// 如果是升级命令操作，进行数据配置根目录路径名称的设定，分为讲师端和学生端，配置根目录会有所不同...
	LPCWSTR lpwDataRoot = NULL;
	switch (g_run_mode) {
	case kSmartUpdater: lpwDataRoot = DEF_SMART_DATA; break;
	default:            lpwDataRoot = DEF_SMART_DATA; break;
	}

	Status(L"正在加载并解析 manifest.json 升级脚本...");

	/* ------------------------------------- *
	 * Get config path                       */
	
	wchar_t lpAppDataPath[MAX_PATH] = { 0 };
	bool bIsPortable = false;
	if (bIsPortable) {
		GetCurrentDirectory(_countof(lpAppDataPath), lpAppDataPath);
		StringCbCat(lpAppDataPath, sizeof(lpAppDataPath), L"\\config");
	} else {
		CoTaskMemPtr<wchar_t> pOut;
		HRESULT hr = SHGetKnownFolderPath(FOLDERID_RoamingAppData, KF_FLAG_DEFAULT, nullptr, &pOut);
		if (hr != S_OK) {
			Status(L"升级失败: 无法定位 AppData 工作目录");
			return false;
		}
		StringCbCopy(lpAppDataPath, sizeof(lpAppDataPath), pOut);
	}
	// 注意：这里需要追加连接符号 => "\\"
	StringCbCat(lpAppDataPath, sizeof(lpAppDataPath), L"\\");
	StringCbCat(lpAppDataPath, sizeof(lpAppDataPath), lpwDataRoot);

	/* ------------------------------------- *
	 * Get download path                     */

	wchar_t manifestPath[MAX_PATH] = { 0 };
	wchar_t tempDirName[MAX_PATH] = { 0 };

	StringCbPrintf(manifestPath, sizeof(manifestPath), L"%s\\updates\\manifest.json", lpAppDataPath);
	if (!GetTempPath(_countof(tempDirName), tempDirName)) {
		Status(L"升级失败: 获取临时目录失败: %ld", GetLastError());
		return false;
	}
	if (!GetTempFileName(tempDirName, lpwDataRoot, 0, g_tempPath)) {
		Status(L"升级失败: 创建临时目录失败: %ld", GetLastError());
		return false;
	}

	DeleteFile(g_tempPath);
	CreateDirectory(g_tempPath, nullptr);

	/* ------------------------------------- *
	 * Load manifest file                    */

	OBSJson root;
	{
		string manifestFile = QuickReadFile(manifestPath);
		if (manifestFile.empty()) {
			Status(L"升级失败: 无法加载 mainfest.json 文件");
			return false;
		}

		json_error_t error;
		root = json_loads(manifestFile.c_str(), 0, &error);

		if (!root) {
			Status(L"升级失败: 无法解析 manifest.json 文件: %S", error.text);
			return false;
		}
	}

	if (!json_is_object(root.get())) {
		Status(L"升级失败: 无效的 manifest.json 文件");
		return false;
	}

	/* ------------------------------------- *
	 * Parse current manifest update files   */

	json_t *packages = json_object_get(root, "packages");
	size_t packageCount = json_array_size(packages);

	for (size_t i = 0; i < packageCount; i++) {
		if (!AddPackageUpdateFiles(packages, i, g_tempPath)) {
			Status(L"处理升级包的过程中发生错误");
			return false;
		}
	}

	/* ------------------------------------- *
	 * Exit if updates already installed     */

	if (!updates.size()) {
		Status(L"所有文件都是最新版本，无需升级。");
		SetDlgItemText(hwndMain, IDC_BUTTON, L"加载父进程");
		SendDlgItemMessage(hwndMain, IDC_PROGRESS, PBM_SETPOS, 100, 0);
		return true;
	}

	/* ------------------------------------- *
	 * Check for VS2015 redistributables     */

	// 先下载，然后进行哈希校验...
	if (!HasVS2015Redist()) {
		if (!UpdateVS2015Redists(root)) {
			return false;
		}
	}

	/* ------------------------------------- *
	 * Generate file hash json               */
	
	// 这里是将json文件发送给服务器，再次进行其它校验
	// 我们可以不用做的那么复杂，只需要针对hash校验就可以了...

	//#define PATCH_MANIFEST_URL  L"https://obsproject.com/update_studio/getpatchmanifest"
	//#define HASH_NULL           L"0000000000000000000000000000000000000000"

	/*OBSJson files(json_array());
	for (update_t &update : updates) {
		wchar_t whash_string[BLAKE2_HASH_STR_LENGTH];
		char    hash_string[BLAKE2_HASH_STR_LENGTH];
		char    outputPath[MAX_PATH];

		if (!update.has_hash)
			continue;

		HashToString(update.my_hash, whash_string);
		if (wcscmp(whash_string, HASH_NULL) == 0)
			continue;

		if (!WideToUTF8Buf(hash_string, whash_string))
			continue;
		if (!WideToUTF8Buf(outputPath, update.basename.c_str()))
			continue;

		string package_path;
		package_path = update.packageName;
		package_path += "/";
		package_path += outputPath;

		json_t *obj = json_object();
		json_object_set_new(obj, "name", json_string(package_path.c_str()));
		json_object_set_new(obj, "hash", json_string(hash_string));
		json_array_append_new(files, obj);
	}*/

	/* ------------------------------------- *
	 * Send file hashes                      */

	/*string newManifest;
	if (json_array_size(files) > 0) {
		char *post_body = json_dumps(files, JSON_COMPACT);

		int    responseCode;

		int len = (int)strlen(post_body);
		uLong compressSize = compressBound(len);
		string compressedJson;

		compressedJson.resize(compressSize);
		compress2((Bytef*)&compressedJson[0], &compressSize,
				(const Bytef*)post_body, len,
				Z_BEST_COMPRESSION);
		compressedJson.resize(compressSize);

		bool success = !!HTTPPostData(PATCH_MANIFEST_URL,
				(BYTE *)&compressedJson[0],
				(int)compressedJson.size(),
				L"Accept-Encoding: gzip", &responseCode,
				newManifest);
		free(post_body);

		if (!success)
			return false;

		if (responseCode != 200) {
			Status(L"升级失败: HTTP/%d 尝试下载升级补丁失败",	responseCode);
			return false;
		}
	} else {
		newManifest = "[]";
	}*/

	/* ------------------------------------- *
	 * Parse new manifest                    */

	/*json_error_t error;
	root = json_loads(newManifest.c_str(), 0, &error);
	if (!root) {
		Status(L"升级失败: 无法解析补丁包内容: %S", error.text);
		return false;
	}

	if (!json_is_array(root.get())) {
		Status(L"升级失败: 无效的补丁包");
		return false;
	}

	packageCount = json_array_size(root);

	for (size_t i = 0; i < packageCount; i++) {
		json_t *patch = json_array_get(root, i);

		if (!json_is_object(patch)) {
			Status(L"升级失败: 无效的补丁包");
			return false;
		}

		json_t *name_json   = json_object_get(patch, "name");
		json_t *hash_json   = json_object_get(patch, "hash");
		json_t *source_json = json_object_get(patch, "source");
		json_t *size_json   = json_object_get(patch, "size");

		if (!json_is_string(name_json))
			continue;
		if (!json_is_string(hash_json))
			continue;
		if (!json_is_string(source_json))
			continue;
		if (!json_is_integer(size_json))
			continue;

		const char *name   = json_string_value(name_json);
		const char *hash   = json_string_value(hash_json);
		const char *source = json_string_value(source_json);
		int         size   = (int)json_integer_value(size_json);

		UpdateWithPatchIfAvailable(name, hash, source, size);
	}*/

	/* ------------------------------------- *
	 * Download Updates                      */

	// 注意：下载的链接地址都是 https:// 安全链接地址...
	// 注意：这里可以设定开启下载线程的数量，默认为2个线程...
	if (!RunDownloadWorkers(2))
		return false;

	if ((size_t)completedUpdates != updates.size()) {
		//Status(L"升级失败：下载文件发生错误！");
		return false;
	}

	/* ------------------------------------- *
	 * Install updates                       */

	int updatesInstalled = 0;
	int lastPosition = 0;

	SendDlgItemMessage(hwndMain, IDC_PROGRESS, PBM_SETPOS, 0, 0);

	for (update_t &update : updates) {
		if (!UpdateFile(update)) {
			return false;
		} else {
			updatesInstalled++;
			int position = (int)(((float)updatesInstalled / (float)completedUpdates) * 100.0f);
			if (position > lastPosition) {
				lastPosition = position;
				SendDlgItemMessage(hwndMain, IDC_PROGRESS, PBM_SETPOS, position, 0);
			}
		}
	}

	/* If we get here, all updates installed successfully so we can purge
	 * the old versions */
	for (update_t &update : updates) {
		if (!update.previousFile.empty()) {
			DeleteFile(update.previousFile.c_str());
		}
		/* We delete here not above in case of duplicate hashes */
		if (!update.tempPath.empty()) {
			DeleteFile(update.tempPath.c_str());
		}
	}

	SendDlgItemMessage(hwndMain, IDC_PROGRESS, PBM_SETPOS, 100, 0);

	Status(L"升级完成！");
	SetDlgItemText(hwndMain, IDC_BUTTON, L"加载父进程");
	return true;
}

static DWORD WINAPI UpdateThread(void *arg)
{
	// 使用传递的命令参数，调用更新接口...
	wchar_t *cmdLine = (wchar_t *)arg;
	bool success = Update(cmdLine);

	if (!success) {
		/* This handles deleting temp files and rolling back and
		 * partially installed updates */
		CleanupPartialUpdates();

		if (g_tempPath[0]) RemoveDirectory(g_tempPath);

		if (WaitForSingleObject(cancelRequested, 0) == WAIT_OBJECT_0) {
			Status(L"升级被取消！");
		}

		SendDlgItemMessage(hwndMain, IDC_PROGRESS, PBM_SETSTATE, PBST_ERROR, 0);

		SetDlgItemText(hwndMain, IDC_BUTTON, L"退 出");
		EnableWindow(GetDlgItem(hwndMain, IDC_BUTTON), true);

		updateFailed = true;
	} else {
		if (g_tempPath[0]) RemoveDirectory(g_tempPath);
	}

	if (bExiting) ExitProcess(success);
	return 0;
}

static void CancelUpdate(bool quit)
{
	if (WaitForSingleObject(updateThread, 0) != WAIT_OBJECT_0) {
		bExiting = quit;
		SetEvent(cancelRequested);
	} else {
		PostQuitMessage(0);
	}
}

static void LaunchParent()
{
	wchar_t cwd[MAX_PATH];
	wchar_t newCwd[MAX_PATH];
	wchar_t obsPath[MAX_PATH];

	// 首先获取当前的工作目录...
	GetCurrentDirectory(_countof(cwd) - 1, cwd);
	// 设定新的工作目录...
	StringCbCopy(obsPath, sizeof(obsPath), cwd);
	StringCbCat(obsPath, sizeof(obsPath), is32bit ? L"\\bin\\32bit" : L"\\bin\\64bit");
	SetCurrentDirectory(obsPath);
	// 保存新的工作目录...
	StringCbCopy(newCwd, sizeof(newCwd), obsPath);
	// 计算新的父进程完整名称和路径...
	const wchar_t * wParentName = g_cmd_type[g_run_mode - 1];
	StringCbPrintf(obsPath, sizeof(obsPath), L"%s\\%s.exe", newCwd, wParentName);
	// 判断父进程文件是否有效...
	if (!FileExists(obsPath)) {
		// 如果文件路径不存在，使用32位再次尝试...
		StringCbCopy(obsPath, sizeof(obsPath), cwd);
		StringCbCat(obsPath, sizeof(obsPath), L"\\bin\\32bit");
		// 直接使用32位再次尝试...
		SetCurrentDirectory(obsPath);
		StringCbCopy(newCwd, sizeof(newCwd), obsPath);
		// 获取32位目录下的父进程文件全路径...
		StringCbPrintf(obsPath, sizeof(obsPath), L"%s\\%s.exe", newCwd, wParentName);
		// 如果文件还是不存在，直接返回...
		if (!FileExists(obsPath)) {
			/* TODO: give user a message maybe? */
			return;
		}
	}

	SHELLEXECUTEINFO execInfo;
	ZeroMemory(&execInfo, sizeof(execInfo));
	// 使用外挂命令启动父进程，指定新的工作目录...
	execInfo.cbSize      = sizeof(execInfo);
	execInfo.lpFile      = obsPath;
	execInfo.lpDirectory = newCwd;
	execInfo.nShow       = SW_SHOWNORMAL;
	// 调用外挂函数启动父进程...
	ShellExecuteEx(&execInfo);
}

static INT_PTR CALLBACK UpdateDialogProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam)
{
	switch (message) {
	case WM_INITDIALOG: {
		static HICON hMainIcon = LoadIcon(hinstMain, MAKEINTRESOURCE(IDI_ICON1));
		SendMessage(hwnd, WM_SETICON, ICON_BIG, (LPARAM)hMainIcon);
		SendMessage(hwnd, WM_SETICON, ICON_SMALL, (LPARAM)hMainIcon);
		return true;
	}
	case WM_COMMAND:
		// 发生指定按钮的点击事件 => 查看线程状态 => 处理不同过程...
		if ((LOWORD(wParam) == IDC_BUTTON) && (HIWORD(wParam) == BN_CLICKED)) {
			DWORD result = WaitForSingleObject(updateThread, 0);
			if (result == WAIT_OBJECT_0) {
				PostQuitMessage(updateFailed ? 0 : 1);
			} else {
				EnableWindow((HWND)lParam, false);
				CancelUpdate(false);
			}
		}
		return true;
	case WM_CLOSE:
		CancelUpdate(true);
		return true;
	}
	return false;
}

static void RestartAsAdmin(LPWSTR lpCmdLine)
{
	wchar_t myPath[MAX_PATH] = { 0 };
	if (!GetModuleFileNameW(nullptr, myPath, _countof(myPath) - 1)) {
		return;
	}

	wchar_t cwd[MAX_PATH];
	GetCurrentDirectoryW(_countof(cwd) - 1, cwd);

	SHELLEXECUTEINFO shExInfo = {0};
	shExInfo.cbSize           = sizeof(shExInfo);
	shExInfo.fMask            = SEE_MASK_NOCLOSEPROCESS;
	shExInfo.hwnd             = 0;
	shExInfo.lpVerb           = L"runas";  /* Operation to perform */
	shExInfo.lpFile           = myPath;    /* Application to start */
	shExInfo.lpParameters     = lpCmdLine; /* Additional parameters */
	shExInfo.lpDirectory      = cwd;
	shExInfo.nShow            = SW_NORMAL;
	shExInfo.hInstApp         = 0;

	/* annoyingly the actual elevated updater will disappear behind other windows :( */
	AllowSetForegroundWindow(ASFW_ANY);

	if (ShellExecuteEx(&shExInfo)) {
		DWORD exitCode = 0;

		if (GetExitCodeProcess(shExInfo.hProcess, &exitCode)) {
			if (exitCode == 1) {
				LaunchParent();
			}
		}
		CloseHandle(shExInfo.hProcess);
	}
}

static bool HasElevation()
{
	SID_IDENTIFIER_AUTHORITY sia = SECURITY_NT_AUTHORITY;
	PSID sid = nullptr;
	BOOL elevated = false;
	BOOL success = false;

	// 检查当前系统用户是否是管理员身份...
	success = AllocateAndInitializeSid(&sia, 2, SECURITY_BUILTIN_DOMAIN_RID, DOMAIN_ALIAS_RID_ADMINS, 0, 0, 0, 0, 0, 0, &sid);
	if (success && sid) {
		CheckTokenMembership(nullptr, sid, &elevated);
		FreeSid(sid);
	}
	// 返回检查结果...
	return elevated;
}

/*void * my_malloc(size_t inSize)
{
	return bmalloc(inSize);
}

void my_free(void * inPtr)
{
	bfree(inPtr);
}*/

int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE, LPWSTR lpCmdLine, int)
{
	INITCOMMONCONTROLSEX icce;
	
	// 注意：只在内存跟踪时才需要用到...
	//json_set_alloc_funcs(my_malloc, my_free);

	// 检查升级进程是否是以管理员身份登录...
	if (!HasElevation()) {
		HANDLE hLowMutex = CreateMutexW(nullptr, true, L"OBSUpdaterRunningAsNonAdminUser");
		// 重启进程，用管理员身份登录...
		RestartAsAdmin(lpCmdLine);
		// 重启结束，关闭互斥对象...
		if (hLowMutex) {
			ReleaseMutex(hLowMutex);
			CloseHandle(hLowMutex);
		}
		// 结束进程...
		return 0;
	} else {
		{
			wchar_t cwd[MAX_PATH];
			wchar_t newPath[MAX_PATH];
			// 得到当前进程的运行目录...
			GetCurrentDirectory(_countof(cwd) - 1, cwd);
			// 当前进程是否为32位模式（与系统无关，只跟安装有关）...
			is32bit = wcsstr(cwd, L"bin\\32bit") != nullptr;
			StringCbCat(cwd, sizeof(cwd), L"\\..\\..");
			// 正常运行 => 计算工作目录，并设定为升级进程的当前工作目录...
			GetFullPathName(cwd, _countof(newPath), newPath, nullptr);
			// 调试运行 => 手动设定父进程的安装目录，调试测试...
			//StringCbCopy(newPath, _countof(newPath), L"C:\\Program Files\\讲师端");
			// 设定升级程序需要的新的工作目录...
			SetCurrentDirectory(newPath);
		}

		hinstMain = hInstance;
		icce.dwSize = sizeof(icce);
		icce.dwICC  = ICC_PROGRESS_CLASS;
		InitCommonControlsEx(&icce);
		// 创建升级对话框，并设定对话框的窗口工程...
		hwndMain = CreateDialog(hInstance,
			MAKEINTRESOURCE(IDD_UPDATEDIALOG),
			nullptr, UpdateDialogProc);
		if (!hwndMain) {
			return -1;
		}

		ShowWindow(hwndMain, SW_SHOWNORMAL);
		SetForegroundWindow(hwndMain);

		// 创建取消事件和升级更新处理线程...
		cancelRequested = CreateEvent(nullptr, true, false, nullptr);
		updateThread = CreateThread(nullptr, 0, UpdateThread, lpCmdLine, 0, nullptr);

		MSG msg = { 0 };
		while (GetMessage(&msg, nullptr, 0, 0)) {
			if (!IsDialogMessage(hwndMain, &msg)) {
				TranslateMessage(&msg);
				DispatchMessage(&msg);
			}
		}

/*#ifdef _DEBUG
		wchar_t wLeaks[MAX_PATH] = { 0 };
		wsprintf(wLeaks, L"Number of memory leaks: %ld\r\n", bnum_allocs());
		OutputDebugString(wLeaks);
#endif // _DEBUG*/

		// 如果是创建json文件命令，执行完毕，直接返回...
		if (g_run_mode == kSmartBuildJson) {
			return (int)msg.wParam;
		}

		/* there is no non-elevated process waiting for us if UAC is disabled */
		WinHandle hMutex = OpenMutex(SYNCHRONIZE, false, L"OBSUpdaterRunningAsNonAdminUser");

		// 升级完毕，重启主进程，自动运行...
		if (msg.wParam == 1 && !hMutex) {
			LaunchParent();
		}

		return (int)msg.wParam;
	}
}
