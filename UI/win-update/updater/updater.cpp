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

// �ٶ�����ļ����͵�ǰ�ļ���...
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
	// ������ݵĲ�����Ч��ֱ�ӷ���...
	if (lpRootPath == NULL || lpFilePath == NULL || lpJson == NULL)
		return;
	// ��Ҫ�ȶ��ļ�ȫ·�����������������...
	wchar_t wFullPath[MAX_PATH] = { 0 };
	wsprintf(wFullPath, L"%s\\%s", lpRootPath, lpFilePath);
	// ����ļ���СΪ0����Ч�ļ���ֱ�ӷ���...
	DWORD dwFileSize = GetTotalFileSize(wFullPath);
	if (dwFileSize <= 0) return;
	// ��ʾ���ڴ�����ļ�ȫ·����ַ��Ϣ...
	Status(L"���ڼ����ϣ: %s...", wFullPath);
	// ���������ļ��Ĺ�ϣֵ����ת����UTF8��ʽ...
	BYTE    existingHash[BLAKE2_HASH_LENGTH] = { 0 };
	char    path_string[MAX_PATH] = { 0 };
	char    hash_string[BLAKE2_HASH_STR_LENGTH] = { 0 };
	wchar_t fileHashStr[BLAKE2_HASH_STR_LENGTH] = { 0 };
	if (!CalculateFileHash(wFullPath, existingHash))
		return;
	// ��Ҫ��·���������´���...
	HashToString(existingHash, fileHashStr);
	WideToUTF8Buf(hash_string, fileHashStr);
	WideToUTF8Buf(path_string, lpFilePath);
	// json_object_set_new => �����������ü���...
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
	// �ȿ�����ѯ����ĸ�Ŀ¼λ��...
	StringCbCopy(wFindPath, sizeof(wFindPath), lpRootPath);
	// �����Ŀ¼��Ϊ�գ���Ҫ׷����Ŀ¼��ע�����ӷ�...
	if (lpSubPath != NULL) {
		StringCbCat(wFindPath, sizeof(wFindPath), L"\\");
		StringCbCat(wFindPath, sizeof(wFindPath), lpSubPath);
	}
	// ׷���ļ�������Ҫ������ͨ���...
	StringCbCat(wFindPath, sizeof(wFindPath), L"\\*.*");
	hFindHandle = FindFirstFile(wFindPath, &wfd);
	// ָ������Ŀ¼�µ�����Ϊ�ջ���Ч��ֱ�ӷ���...
	if (hFindHandle == INVALID_HANDLE_VALUE)
		return;
	// Ŀ¼��Ч����ʼ����Ŀ¼�����е��ļ���Ŀ¼...
	while (bIsOK) {
		// ����ǵ�ǰĿ¼����һ��Ŀ¼������ļ�������������һ��...
		if ((wcscmp(wfd.cFileName, L".") == 0) || (wcscmp(wfd.cFileName, L"..") == 0) || (wcscmp(wfd.cFileName, L".gitignore") == 0)) {
			bIsOK = FindNextFile(hFindHandle, &wfd);
			continue;
		}
		// �����Ŀ¼��Ч����Ҫ������Ŀ¼...
		if (lpSubPath != NULL) {
			StringCbCopy(wCurPath, sizeof(wCurPath), lpSubPath);
			StringCbCat(wCurPath, sizeof(wCurPath), L"/");
			StringCbCat(wCurPath, sizeof(wCurPath), wfd.cFileName);
		} else {
			StringCbCopy(wCurPath, sizeof(wCurPath), wfd.cFileName);
		}
		// �������Ч��Ŀ¼����Ҫ���еݹ����...
		if (wfd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
			QuickAllFiles(lpRootPath, wCurPath, lpJson);
		} else if (wfd.dwFileAttributes & FILE_ATTRIBUTE_ARCHIVE) {
			// �������Ч���ļ��������ϣֵ��������json�ڵ㵱��...
			QuickCalcFileHash(lpRootPath, wCurPath, lpJson);
			// �����Ѵ�����ļ��ٷֱ� => ��ŵ�����...
			int position = (int)(((float)(++g_nJsonCurFiles) / (float)g_nJsonMaxFiles) * 100.0f);
			SendDlgItemMessage(hwndMain, IDC_PROGRESS, PBM_SETPOS, position, 0);
#ifdef _DEBUG
			OutputDebugString(lpRootPath);
			OutputDebugString(L"\\");
			OutputDebugString(wCurPath);
			OutputDebugString(L"\r\n");
#endif // _DEBUG
		}
		// ����������һ���ļ���Ŀ¼...
		bIsOK = FindNextFile(hFindHandle, &wfd);
	}
	// �ͷŲ��Ҿ������...
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
		Status(L"����ʧ��: �޷���������������");
		return false;
	}

	WinHttpSetOption(hSession, WINHTTP_OPTION_SECURE_PROTOCOLS, (LPVOID)&tlsProtocols, sizeof(tlsProtocols));

	HttpHandle hConnect = WinHttpConnect(hSession, DEF_WEB_CLASS, INTERNET_DEFAULT_HTTPS_PORT, 0);
	if (!hConnect) {
		downloadThreadFailure = true;
		Status(L"����ʧ��: �޷����ӵ�������������");
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

			Status(L"�������� %s", update.outputPath.c_str());

			if (!HTTPGetFile(hConnect, update.sourceURL.c_str(), update.tempPath.c_str(), L"Accept-Encoding: gzip", &responseCode)) {
				downloadThreadFailure = true;
				DeleteFile(update.tempPath.c_str());
				Status(L"����ʧ��: �޷����� %s (����� %d)", update.outputPath.c_str(), responseCode);
				return 1;
			}

			if (responseCode != 200) {
				downloadThreadFailure = true;
				DeleteFile(update.tempPath.c_str());
				Status(L"����ʧ��: �޷����� %s (����� %d)", update.outputPath.c_str(), responseCode);
				return 1;
			}

			BYTE downloadHash[BLAKE2_HASH_LENGTH];
			if (!CalculateFileHash(update.tempPath.c_str(), downloadHash)) {
				downloadThreadFailure = true;
				DeleteFile(update.tempPath.c_str());
				Status(L"����ʧ��: �޷���֤������ %s", update.outputPath.c_str());
				return 1;
			}

			if (memcmp(update.downloadhash, downloadHash, 20)) {
				downloadThreadFailure = true;
				DeleteFile(update.tempPath.c_str());
				Status(L"����ʧ��: ��֤������������ʧ�� %s", update.outputPath.c_str());
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
	Status(L"���ڵȴ��������˳�...");
	// ֻ����Smart�ն˶˵��ⲿ����ָ��...
	if (g_run_mode != kSmartUpdater)
		return true;
	// ��ȡ��Ҫ���ҵĽ������Ʊ�ʶ����...
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

	// ע�⣺ֻ����coreģ�飬����ģ�鲻����...
	if (strcmp(packageName, "core") != 0) //&& !NonCorePackageInstalled(packageName))
		return true;
	// ���ý������ĳ�ʼλ�� => Ĭ��������(0, 100)
	SendDlgItemMessage(hwndMain, IDC_PROGRESS, PBM_SETPOS, 0, 0);
	// ��ʼ�������е��ļ��б�...
	for (size_t j = 0; j < fileCount; j++) {
		json_t *file     = json_array_get(files, j);
		json_t *fileName = json_object_get(file, "name");
		json_t *hash     = json_object_get(file, "hash");
		json_t *size     = json_object_get(file, "size");
		// �����Ѵ����ļ��İٷֱȣ�����ʾ�ڽ�������...
		int position = (int)(((float)(j+1) / (float)fileCount) * 100.0f);
		SendDlgItemMessage(hwndMain, IDC_PROGRESS, PBM_SETPOS, position, 0);
		// ����ļ���|��ϣֵ|���ȣ���Ч��������һ���ļ�...
		if (!json_is_string(fileName))
			continue;
		if (!json_is_string(hash))
			continue;
		if (!json_is_integer(size))
			continue;
		// ��ȡ��Ч���ļ�������ϣֵ������...
		const char *fileUTF8 = json_string_value(fileName);
		const char *hashUTF8 = json_string_value(hash);
		int fileSize         = (int)json_integer_value(size);

		if (strlen(hashUTF8) != BLAKE2_HASH_LENGTH * 2)
			continue;

		// ����64λ�ļ���ʽ�ļ�⣬��������� => obs-x264.dll...
		//if (!isWin64 && is_64bit_file(fileUTF8))
		//	continue;

		/* ignore update files of opposite arch to reduce download */
		// ע�⣺��������32bit�Ľ���ֻ����32bit���ļ�����ϵͳ�汾�޹�...
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
			Status(L"����ʧ��: ����ȫ·�� '%s'", updateFileName);
			return false;
		}

		// �޸İ����Ƶ�ַ��������ԭ����core������Ҫ����վ����Ŀ¼��Ӧ...
		StringCbPrintf(sourceURL, sizeof(sourceURL), L"%s/%s/%s", DEF_UPDATE_URL, wPackageName, updateFileName);
		StringCbPrintf(tempFilePath, sizeof(tempFilePath), L"%s\\%s", tempPath, updateHashStr);

		/* Check file hash */

		wchar_t fileHashStr[BLAKE2_HASH_STR_LENGTH];
		BYTE    existingHash[BLAKE2_HASH_LENGTH];
		bool    has_hash = false;

		/* We don't really care if this fails, it's just to avoid
		 * wasting bandwidth by downloading unmodified files */
		// ע�⣺�Թ���Ŀ¼Ϊ���㣬���㱾���ļ��Ĺ�ϣֵ����ͬ�Ͳ�����...
		Status(L"���ڼ����ϣ: %s...", updateFileName);
		if (CalculateFileHash(updateFileName, existingHash)) {
			HashToString(existingHash, fileHashStr);
			// ��������ļ��Ĺ�ϣֵ��ű���Ĺ�ϣֵһ�£�������...
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
		Status(L"�������� %s...", file.outputPath.c_str());
	} else {
		Status(L"���ڰ�װ %s...", file.outputPath.c_str());
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
				Status(L"����ʧ��: %s ����ʹ���У���ر����еĳ�������һ�Σ�", curFileName);
			} else {
				Status(L"����ʧ��: �޷����²��� %s (����� %d)", curFileName, GetLastError());
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
					Status(L"����ʧ��: �޷�������֤���� %s", curFileName);
					file.state = STATE_INSTALL_FAILED;
					return false;
				}

				if (memcmp(file.hash, patchedFileHash, BLAKE2_HASH_LENGTH) != 0) {
					Status(L"����ʧ��: ��֤������������ʧ�� %s", curFileName);
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
				Status(L"����ʧ��: %s ����ʹ���У���ر����еĳ�������һ�Σ�", curFileName);
			} else {
				Status(L"����ʧ��: �޷����²��� %s (����� %d)", curFileName, GetLastError());
			}
			file.state = STATE_INSTALL_FAILED;
			return false;
		}

		file.state = STATE_INSTALLED;
	} else {
		if (file.patchable) {
			/* Uh oh, we thought we could patch something but it's no longer there! */
			Status(L"����ʧ��: Դ�ļ� %s û���ҵ�", file.outputPath.c_str());
			return false;
		}

		/* We may be installing into new folders,
		 * make sure they exist */
		CreateFoldersForPath(file.outputPath.c_str());

		file.previousFile = L"";

		bool success = !!MyCopyFile(file.tempPath.c_str(), file.outputPath.c_str());
		if (!success) {
			Status(L"����ʧ��: �޷���װ %s (����� %d)", file.outputPath.c_str(), GetLastError());
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

	// �ж��ļ����Ƿ���Ч...
	if (nameUTF8 == NULL || hashUTF8 == NULL || fileSize <= 0) {
		Status(L"Update failed: Could not parse VC2015 redist json");
		return false;
	}
	// ���ļ���ת���ɿ��ַ���ʽ...
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

	// �����ļ��ܳ��ȣ��������֮���ٻָ���ȥ...
	int nSaveTotalFileSize = totalFileSize;
	totalFileSize = fileSize;
	completedFileSize = 0;
	// �ӷ�����������Visual C++ 2015 Redistributable�������½�����...
	if (!HTTPGetFile(hConnect, sourceURL.c_str(), destPath.c_str(), L"Accept-Encoding: gzip", &responseCode)) {
		DeleteFile(destPath.c_str());
		Status(L"Update failed: Could not download "
		       L"%s (error code %d)",
		       L"Visual C++ 2015 Redistributable",
		       responseCode);
		return false;
	}
	// ������ϣ��ָ��ļ��ܳ��Ⱥ��������ļ�����...
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
	Status(L"���ڴ��������ű� manifest.json �ļ�...");
	////////////////////////////////////////////////////////////////
	// ע�⣺json��txt�������ݱ�����UTF8��ʽ����Ӧͨ����...
	// ע�⣺����·�����Ʊ����ǿ��ַ�����ϵͳ����һ��...
	////////////////////////////////////////////////////////////////
	// ��ȡmanifest.json��changelog.txt�Ĵ�ȡ·��...
	wchar_t lpLogPath[MAX_PATH] = { 0 };
	wchar_t lpJsonPath[MAX_PATH] = { 0 };
	wchar_t lpRootPath[MAX_PATH] = { 0 };
	wchar_t lpBasePath[MAX_PATH] = { 0 };
	// ���㹤��Ŀ¼�����趨Ϊ�������̵ĵ�ǰ����Ŀ¼...
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
	// ���㲢�ж��ļ��б��Ŀ¼����Ч��...
	GetFullPathName(lpBasePath, _countof(lpRootPath), lpRootPath, nullptr);
	if ((wcslen(lpRootPath) <= 0) || !PathIsDirectory(lpRootPath)) {
		Status(L"��λ��Ŀ¼ʧ�ܣ�%s", lpRootPath);
		return false;
	}
	// ��ȡ������־ʧ�ܣ�����ʧ�ܽ��...
	string strLogData = QuickReadFile(lpLogPath);
	if (strLogData.size() <= 0) {
		Status(L"��ȡ������־ changelog.txt ʧ��...");
		return false;
	}
	// �ж϶�ȡ����־�ļ��Ƿ���UTF8��ʽ...
	json_t * lpLogUTF8 = json_string(strLogData.c_str());
	if (lpLogUTF8 == NULL) {
		Status(L"������־ changelog.txt ���� UTF8 ��ʽ...");
		return false;
	}
	// ���ó�ʼ�Ľ��������������ļ�����...
	g_nJsonCurFiles = 0;
	SendDlgItemMessage(hwndMain, IDC_PROGRESS, PBM_SETPOS, 0, 0);
	// json_decref => �����ü���ɾ�����ǳ���Ҫ...
	// ������Ӧ�ķ���Ŀ¼�����ļ��б� => ��Ҫ�ݹ��ȡ...
	json_t * lpJsonCoreFile = json_array();
	QuickAllFiles(lpRootPath, NULL, lpJsonCoreFile);
	// ����vc��չ���п�Ĺ�ϣ������json����...
	json_t * lpJsonX86File = json_array();
	json_t * lpJsonX64File = json_array();
	GetCurrentDirectory(_countof(lpRootPath), lpRootPath);
	StringCbCat(lpRootPath, sizeof(lpRootPath), is32bit ? L"\\bin\\32bit" : L"\\bin\\64bit");
	QuickCalcFileHash(lpRootPath, L"vc2015_redist.x86.exe", lpJsonX86File);
	QuickCalcFileHash(lpRootPath, L"vc2015_redist.x64.exe", lpJsonX64File);
	// �����趨��������λ�� => 100%...
	SendDlgItemMessage(hwndMain, IDC_PROGRESS, PBM_SETPOS, 100, 0);

	// ע�⣺json_decref => �����ü���ɾ�����ǳ���Ҫ...
	// json_object_set_new�����������ü���...
	json_t * lpJsonPack = json_array();
	json_t * lpObjCore = json_object();
	json_object_set_new(lpObjCore, "name", json_string("core"));
	json_object_set_new(lpObjCore, "files", lpJsonCoreFile);
	json_array_append_new(lpJsonPack, lpObjCore);

	// ע�⣺json_decref => �����ü���ɾ�����ǳ���Ҫ...
	// ע�⣺OBSJson���Զ���������֤���й��������Զ���ɾ��...
	// ����json�洢���� => �ַ���������UTF8��ʽ...
	OBSJson maniFest(json_object());
	// ���ȼ��������־��Ϣ => �ַ���������UTF8��ʽ...
	json_object_set_new(maniFest, "notes", lpLogUTF8);
	// ׷����Ҫ�������ļ��б� => �ַ���������UTF8��ʽ...
	json_object_set_new(maniFest, "packages", lpJsonPack);
	json_object_set_new(maniFest, "vc2015_x86", lpJsonX86File);
	json_object_set_new(maniFest, "vc2015_x64", lpJsonX64File);
	// ׷�Ӱ汾���룬�ܹ������汾��Ϣ...
	json_object_set_new(maniFest, "version_major", json_integer(LIBOBS_API_MAJOR_VER));
	json_object_set_new(maniFest, "version_minor", json_integer(LIBOBS_API_MINOR_VER));
	json_object_set_new(maniFest, "version_patch", json_integer(LIBOBS_API_PATCH_VER));
	// �ر�ע�⣺����dump��ָ�룬һ��Ҫǿ��ɾ������������ڴ�й©...
	// ��ȡmanifest.json�����ַ�����Ϣ => �ַ���������UTF8��ʽ...
	char * post_body = json_dumps(maniFest, JSON_COMPACT);
	// ���п��ٴ��̲��� => �������ݱ�����UTF8��ʽ...
	if (!QuickWriteFile(lpJsonPath, post_body, strlen(post_body))) {
		Status(L"���������ű� manifest.json ʧ��...");
		free(post_body); post_body = NULL;
		return false;
	}
	// �ر�ע�⣺����dump��ָ�룬һ��Ҫǿ��ɾ������������ڴ�й©...
	free(post_body); post_body = NULL;
	// ��ʾ����manifest.json�����ű��ļ��ɹ�...
	Status(L"�ѳɹ����������ű��ļ�manifest.json...");
	// �޸��˳���ť���ı���Ϣ����ʹ��ť���Ե������...
	SetDlgItemText(hwndMain, IDC_BUTTON, L"�� ��");
	EnableWindow(GetDlgItem(hwndMain, IDC_BUTTON), true);
	return true;
}

static void ParseCmdLine(wchar_t *cmdLine)
{
	Status(L"���ڽ����ⲿ����ָ��...");

	// �Ľ����������ƣ����������������£�
	// kSmartUpdater   => Smart�ն�����ģʽ
	// kSmartBuildJson => Smart�ն˴�������json

	g_run_mode = kDefaultUnknown;
	LPWSTR *argv = NULL;
	int argc = 0;

	do {
		// �������������Ϊ�գ�����ʧ��...
		if (cmdLine[0] == NULL) break;
		// ֻ����һ���ⲿָ����򷵻�ʧ��...
		argv = CommandLineToArgvW(cmdLine, &argc);
		if (argc != 1) break;
		// ����ָ���Ӧ�ı�ţ���Ҫ�����ⲿָ��ĸ���...
		int nMaxTypeNum = sizeof(g_cmd_type) / sizeof(g_cmd_type[0]);
		for (int i = 0; i < nMaxTypeNum; ++i) {
			if (wcsicmp(argv[0], g_cmd_type[i]) == 0) {
				g_run_mode = (RUN_MODE)(i + 1); break;
			}
		}
	} while (false);
	// �ͷ���ʱ����Ŀռ�...
	if (argv != NULL) {
		LocalFree((HLOCAL)argv);
		argv = NULL; argc = 0;
	}
	// ��ͬ���ⲿ����趨��ͬ�ı���������...
	wchar_t wWndTitle[MAX_PATH] = { 0 };
	GetWindowText(hwndMain, wWndTitle, MAX_PATH);
	switch (g_run_mode)
	{
	case kSmartBuildJson: StringCbCat(wWndTitle, MAX_PATH, L" - Smart�ն� - �ؽ��ű�..."); break;
	case kSmartUpdater:   StringCbCat(wWndTitle, MAX_PATH, L" - Smart�ն� - ��������..."); break;
	default:                StringCbCat(wWndTitle, MAX_PATH, L" - ��������"); break;
	}
	// �趨���º�Ĵ��ڱ�����������...
	SetWindowText(hwndMain, wWndTitle);
}

static bool Update(wchar_t *cmdLine)
{
	/* ------------------------------------- *
	 * Check to make sure OBS isn't running  */
	HANDLE hObsUpdateMutex = OpenMutexW(SYNCHRONIZE, false, L"OBSStudioUpdateMutex");
	// ����Ѿ����������������У���Ҫ�ȴ�֮ǰ�����������˳�...
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
	// ���������У��õ�����״̬...
	ParseCmdLine(cmdLine);
	// �ȴ��������˳�����ʦ�˻�ѧ����...
	if (!WaitForParent())
		return false;

	/* ------------------------------------- *
	 * Init crypt stuff                      */

	CryptProvider hProvider;
	if (!CryptAcquireContext(&hProvider, nullptr, MS_ENH_RSA_AES_PROV, PROV_RSA_AES, CRYPT_VERIFYCONTEXT)) {
		Status(L"����ʧ��: CryptAcquireContext ʧ��");
		return false;
	}

	::hProvider = hProvider;

	/* ------------------------------------- */

	// ��������ⲿ����ʧ�ܣ���ʾ��Ϣ������...
	if (g_run_mode <= kDefaultUnknown || g_run_mode > kSmartBuildJson) {
		Status(L"����ʧ�ܣ������ⲿָ����� => {smart,json_smart}");
		return false;
	}
	// ����Ǵ���json�ļ����������ת�ַ�...
	if (g_run_mode == kSmartBuildJson) {
		return doUpdateBuildJson();
	}
	// �����������������������������ø�Ŀ¼·�����Ƶ��趨����Ϊ��ʦ�˺�ѧ���ˣ����ø�Ŀ¼��������ͬ...
	LPCWSTR lpwDataRoot = NULL;
	switch (g_run_mode) {
	case kSmartUpdater: lpwDataRoot = DEF_SMART_DATA; break;
	default:            lpwDataRoot = DEF_SMART_DATA; break;
	}

	Status(L"���ڼ��ز����� manifest.json �����ű�...");

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
			Status(L"����ʧ��: �޷���λ AppData ����Ŀ¼");
			return false;
		}
		StringCbCopy(lpAppDataPath, sizeof(lpAppDataPath), pOut);
	}
	// ע�⣺������Ҫ׷�����ӷ��� => "\\"
	StringCbCat(lpAppDataPath, sizeof(lpAppDataPath), L"\\");
	StringCbCat(lpAppDataPath, sizeof(lpAppDataPath), lpwDataRoot);

	/* ------------------------------------- *
	 * Get download path                     */

	wchar_t manifestPath[MAX_PATH] = { 0 };
	wchar_t tempDirName[MAX_PATH] = { 0 };

	StringCbPrintf(manifestPath, sizeof(manifestPath), L"%s\\updates\\manifest.json", lpAppDataPath);
	if (!GetTempPath(_countof(tempDirName), tempDirName)) {
		Status(L"����ʧ��: ��ȡ��ʱĿ¼ʧ��: %ld", GetLastError());
		return false;
	}
	if (!GetTempFileName(tempDirName, lpwDataRoot, 0, g_tempPath)) {
		Status(L"����ʧ��: ������ʱĿ¼ʧ��: %ld", GetLastError());
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
			Status(L"����ʧ��: �޷����� mainfest.json �ļ�");
			return false;
		}

		json_error_t error;
		root = json_loads(manifestFile.c_str(), 0, &error);

		if (!root) {
			Status(L"����ʧ��: �޷����� manifest.json �ļ�: %S", error.text);
			return false;
		}
	}

	if (!json_is_object(root.get())) {
		Status(L"����ʧ��: ��Ч�� manifest.json �ļ�");
		return false;
	}

	/* ------------------------------------- *
	 * Parse current manifest update files   */

	json_t *packages = json_object_get(root, "packages");
	size_t packageCount = json_array_size(packages);

	for (size_t i = 0; i < packageCount; i++) {
		if (!AddPackageUpdateFiles(packages, i, g_tempPath)) {
			Status(L"�����������Ĺ����з�������");
			return false;
		}
	}

	/* ------------------------------------- *
	 * Exit if updates already installed     */

	if (!updates.size()) {
		Status(L"�����ļ��������°汾������������");
		SetDlgItemText(hwndMain, IDC_BUTTON, L"���ظ�����");
		SendDlgItemMessage(hwndMain, IDC_PROGRESS, PBM_SETPOS, 100, 0);
		return true;
	}

	/* ------------------------------------- *
	 * Check for VS2015 redistributables     */

	// �����أ�Ȼ����й�ϣУ��...
	if (!HasVS2015Redist()) {
		if (!UpdateVS2015Redists(root)) {
			return false;
		}
	}

	/* ------------------------------------- *
	 * Generate file hash json               */
	
	// �����ǽ�json�ļ����͸����������ٴν�������У��
	// ���ǿ��Բ���������ô���ӣ�ֻ��Ҫ���hashУ��Ϳ�����...

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
			Status(L"����ʧ��: HTTP/%d ����������������ʧ��",	responseCode);
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
		Status(L"����ʧ��: �޷���������������: %S", error.text);
		return false;
	}

	if (!json_is_array(root.get())) {
		Status(L"����ʧ��: ��Ч�Ĳ�����");
		return false;
	}

	packageCount = json_array_size(root);

	for (size_t i = 0; i < packageCount; i++) {
		json_t *patch = json_array_get(root, i);

		if (!json_is_object(patch)) {
			Status(L"����ʧ��: ��Ч�Ĳ�����");
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

	// ע�⣺���ص����ӵ�ַ���� https:// ��ȫ���ӵ�ַ...
	// ע�⣺��������趨���������̵߳�������Ĭ��Ϊ2���߳�...
	if (!RunDownloadWorkers(2))
		return false;

	if ((size_t)completedUpdates != updates.size()) {
		//Status(L"����ʧ�ܣ������ļ���������");
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

	Status(L"������ɣ�");
	SetDlgItemText(hwndMain, IDC_BUTTON, L"���ظ�����");
	return true;
}

static DWORD WINAPI UpdateThread(void *arg)
{
	// ʹ�ô��ݵ�������������ø��½ӿ�...
	wchar_t *cmdLine = (wchar_t *)arg;
	bool success = Update(cmdLine);

	if (!success) {
		/* This handles deleting temp files and rolling back and
		 * partially installed updates */
		CleanupPartialUpdates();

		if (g_tempPath[0]) RemoveDirectory(g_tempPath);

		if (WaitForSingleObject(cancelRequested, 0) == WAIT_OBJECT_0) {
			Status(L"������ȡ����");
		}

		SendDlgItemMessage(hwndMain, IDC_PROGRESS, PBM_SETSTATE, PBST_ERROR, 0);

		SetDlgItemText(hwndMain, IDC_BUTTON, L"�� ��");
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

	// ���Ȼ�ȡ��ǰ�Ĺ���Ŀ¼...
	GetCurrentDirectory(_countof(cwd) - 1, cwd);
	// �趨�µĹ���Ŀ¼...
	StringCbCopy(obsPath, sizeof(obsPath), cwd);
	StringCbCat(obsPath, sizeof(obsPath), is32bit ? L"\\bin\\32bit" : L"\\bin\\64bit");
	SetCurrentDirectory(obsPath);
	// �����µĹ���Ŀ¼...
	StringCbCopy(newCwd, sizeof(newCwd), obsPath);
	// �����µĸ������������ƺ�·��...
	const wchar_t * wParentName = g_cmd_type[g_run_mode - 1];
	StringCbPrintf(obsPath, sizeof(obsPath), L"%s\\%s.exe", newCwd, wParentName);
	// �жϸ������ļ��Ƿ���Ч...
	if (!FileExists(obsPath)) {
		// ����ļ�·�������ڣ�ʹ��32λ�ٴγ���...
		StringCbCopy(obsPath, sizeof(obsPath), cwd);
		StringCbCat(obsPath, sizeof(obsPath), L"\\bin\\32bit");
		// ֱ��ʹ��32λ�ٴγ���...
		SetCurrentDirectory(obsPath);
		StringCbCopy(newCwd, sizeof(newCwd), obsPath);
		// ��ȡ32λĿ¼�µĸ������ļ�ȫ·��...
		StringCbPrintf(obsPath, sizeof(obsPath), L"%s\\%s.exe", newCwd, wParentName);
		// ����ļ����ǲ����ڣ�ֱ�ӷ���...
		if (!FileExists(obsPath)) {
			/* TODO: give user a message maybe? */
			return;
		}
	}

	SHELLEXECUTEINFO execInfo;
	ZeroMemory(&execInfo, sizeof(execInfo));
	// ʹ������������������̣�ָ���µĹ���Ŀ¼...
	execInfo.cbSize      = sizeof(execInfo);
	execInfo.lpFile      = obsPath;
	execInfo.lpDirectory = newCwd;
	execInfo.nShow       = SW_SHOWNORMAL;
	// ������Һ�������������...
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
		// ����ָ����ť�ĵ���¼� => �鿴�߳�״̬ => ����ͬ����...
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

	// ��鵱ǰϵͳ�û��Ƿ��ǹ���Ա���...
	success = AllocateAndInitializeSid(&sia, 2, SECURITY_BUILTIN_DOMAIN_RID, DOMAIN_ALIAS_RID_ADMINS, 0, 0, 0, 0, 0, 0, &sid);
	if (success && sid) {
		CheckTokenMembership(nullptr, sid, &elevated);
		FreeSid(sid);
	}
	// ���ؼ����...
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
	
	// ע�⣺ֻ���ڴ����ʱ����Ҫ�õ�...
	//json_set_alloc_funcs(my_malloc, my_free);

	// ������������Ƿ����Թ���Ա��ݵ�¼...
	if (!HasElevation()) {
		HANDLE hLowMutex = CreateMutexW(nullptr, true, L"OBSUpdaterRunningAsNonAdminUser");
		// �������̣��ù���Ա��ݵ�¼...
		RestartAsAdmin(lpCmdLine);
		// �����������رջ������...
		if (hLowMutex) {
			ReleaseMutex(hLowMutex);
			CloseHandle(hLowMutex);
		}
		// ��������...
		return 0;
	} else {
		{
			wchar_t cwd[MAX_PATH];
			wchar_t newPath[MAX_PATH];
			// �õ���ǰ���̵�����Ŀ¼...
			GetCurrentDirectory(_countof(cwd) - 1, cwd);
			// ��ǰ�����Ƿ�Ϊ32λģʽ����ϵͳ�޹أ�ֻ����װ�йأ�...
			is32bit = wcsstr(cwd, L"bin\\32bit") != nullptr;
			StringCbCat(cwd, sizeof(cwd), L"\\..\\..");
			// �������� => ���㹤��Ŀ¼�����趨Ϊ�������̵ĵ�ǰ����Ŀ¼...
			GetFullPathName(cwd, _countof(newPath), newPath, nullptr);
			// �������� => �ֶ��趨�����̵İ�װĿ¼�����Բ���...
			//StringCbCopy(newPath, _countof(newPath), L"C:\\Program Files\\��ʦ��");
			// �趨����������Ҫ���µĹ���Ŀ¼...
			SetCurrentDirectory(newPath);
		}

		hinstMain = hInstance;
		icce.dwSize = sizeof(icce);
		icce.dwICC  = ICC_PROGRESS_CLASS;
		InitCommonControlsEx(&icce);
		// ���������Ի��򣬲��趨�Ի���Ĵ��ڹ���...
		hwndMain = CreateDialog(hInstance,
			MAKEINTRESOURCE(IDD_UPDATEDIALOG),
			nullptr, UpdateDialogProc);
		if (!hwndMain) {
			return -1;
		}

		ShowWindow(hwndMain, SW_SHOWNORMAL);
		SetForegroundWindow(hwndMain);

		// ����ȡ���¼����������´����߳�...
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

		// ����Ǵ���json�ļ����ִ����ϣ�ֱ�ӷ���...
		if (g_run_mode == kSmartBuildJson) {
			return (int)msg.wParam;
		}

		/* there is no non-elevated process waiting for us if UAC is disabled */
		WinHandle hMutex = OpenMutex(SYNCHRONIZE, false, L"OBSUpdaterRunningAsNonAdminUser");

		// ������ϣ����������̣��Զ�����...
		if (msg.wParam == 1 && !hMutex) {
			LaunchParent();
		}

		return (int)msg.wParam;
	}
}
