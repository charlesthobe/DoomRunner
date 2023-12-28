//======================================================================================================================
// Project: DoomRunner
//----------------------------------------------------------------------------------------------------------------------
// Author:      Jan Broz (Youda008)
// Description: executable file parsing and information extraction
//======================================================================================================================

#include "ExeReader.hpp"

#include "LangUtils.hpp"  // atScopeEndDo
#include "ContainerUtils.hpp"  // span
#include "JsonUtils.hpp"
#include "ErrorHandling.hpp"

#include <QFile>
#include <QFileInfo>
#include <QDateTime>
#include <QStringBuilder>

#if IS_WINDOWS
	#include <windows.h>
	#include <strsafe.h>
#endif


namespace os {


//======================================================================================================================
//  Windows

#if IS_WINDOWS

// I hate you Graph!!!


//----------------------------------------------------------------------------------------------------------------------
//  resources

class Resource {

 public:

	Resource() : hResInfo( nullptr ), hResource( nullptr ), lpData( nullptr ), dwSize( 0 ) {}
	Resource( const Resource & other ) = delete;

	Resource( Resource && other )
	{
		hResInfo = other.hResInfo;    other.hResInfo = nullptr;
		hResource = other.hResource;  other.hResource = nullptr;
		lpData = other.lpData;        other.lpData = nullptr;
		dwSize = other.dwSize;        other.dwSize = 0;
	}

	~Resource()
	{
		if (hResource)
			FreeResource( hResource );
	}

	operator bool() const   { return lpData != nullptr; }

	auto handle() const     { return hResource; }
	auto data() const       { return reinterpret_cast< const uint8_t * >( lpData ); }
	auto size() const       { return dwSize; }

 private:

	friend Resource getResource( const QString & filePath, HMODULE hExeModule, LPWSTR lpType );

	HRSRC hResInfo;
	HGLOBAL hResource;
	const void * lpData;
	DWORD dwSize;

};

Resource getResource( const QString & filePath, HMODULE hExeModule, LPWSTR lpType )
{
	Resource res;

	res.hResInfo = FindResource( hExeModule, MAKEINTRESOURCE(1), lpType );
	if (res.hResInfo == nullptr)
	{
		// this resource is optional, some exe files don't have it
		auto lastError = GetLastError();
		logDebug("ExeReader") << "Cannot find resource "<<lpType<<" in "<<filePath<<", FindResource() failed with error "<<lastError;
		return {};
	}

	res.hResource = LoadResource( hExeModule, res.hResInfo );
	if (res.hResource == nullptr)  // careful: it's nullptr, not INVALID_HANDLE_VALUE
	{
		auto lastError = GetLastError();
		logRuntimeError("ExeReader") << "Cannot load resource "<<lpType<<" from "<<filePath<<", LoadResource() failed with error "<<lastError;
		return {};
	}

	res.lpData = LockResource( res.hResource );
	res.dwSize = SizeofResource( hExeModule, res.hResInfo );
	if (res.lpData == nullptr || res.dwSize == 0)
	{
		auto lastError = GetLastError();
		logRuntimeError("ExeReader") << "Cannot read resource "<<lpType<<" from "<<filePath<<", LockResource() failed with error "<<lastError;
		return {};
	}

	return res;
}


//----------------------------------------------------------------------------------------------------------------------
//  version info extraction

static VS_FIXEDFILEINFO * getRawVersionInfo( const void * resData )
{
	VS_FIXEDFILEINFO * verInfo;
	UINT verInfoSize;
	if (!VerQueryValue( resData, L"\\", (LPVOID*)&verInfo, &verInfoSize ))
	{
		auto lastError = GetLastError();
		logRuntimeError("ExeReader") << "Cannot read version info, VerQueryValue(\"\\\") failed with error "<<lastError;
		return nullptr;
	}
	else if (verInfo == nullptr || verInfoSize < sizeof(VS_FIXEDFILEINFO))
	{
		logRuntimeError("ExeReader") << "Cannot read version info, VerQueryValue(\"\\\") returned "<<verInfo<<','<<verInfoSize;
		return nullptr;
	}
	else if (verInfo->dwSignature != 0xFEEF04BD)
	{
		logRuntimeError("ExeReader") << QStringLiteral("Cannot read version info, VerQueryValue() returned invalid signature %1").arg( verInfo->dwSignature, 0, 16 );
		return nullptr;
	}
	return verInfo;
}

struct LangInfo
{
	WORD language;
	WORD codePage;
};

static span< LangInfo > getLangInfo( const void * resData )
{
	LangInfo * lpTranslate;
	UINT cbTranslate = 0;
	if (!VerQueryValue( resData, TEXT("\\VarFileInfo\\Translation"), (LPVOID*)&lpTranslate, &cbTranslate ))
	{
		auto lastError = GetLastError();
		logRuntimeError("ExeReader") << "Cannot read version info, VerQueryValue(\"\\VarFileInfo\\Translation\") failed with error "<<lastError;
		return { nullptr, 0 };
	}
	else if (lpTranslate == nullptr || cbTranslate < sizeof(LangInfo))
	{
		logRuntimeError("ExeReader") << "No language section in version info, VerQueryValue(\"\\VarFileInfo\\Translation\") returned "<<lpTranslate<<','<<cbTranslate;
		return { nullptr, 0 };
	}
	return { lpTranslate, cbTranslate / sizeof(LangInfo) };
}

static QString getVerInfoValue( const void * resData, const LangInfo & langInfo, const TCHAR * valueName )
{
	TCHAR SubBlock[128];
	HRESULT hr = StringCchPrintf(
		SubBlock, 128, TEXT("\\StringFileInfo\\%04x%04x\\%s"), langInfo.language, langInfo.codePage, valueName
	);
	if (FAILED(hr))
	{
		logRuntimeError("ExeReader") << "StringCchPrintf() failed";
		return {};
	}

	LPVOID lpBuffer;
	UINT cchLen;  // number of characters
	if (!VerQueryValue( resData, SubBlock, &lpBuffer, &cchLen ))
	{
		auto lastError = GetLastError();
		logRuntimeError("ExeReader") << "Cannot read version value, VerQueryValue("<<QString::fromWCharArray(SubBlock)<<") failed with error "<<lastError;
		return {};
	}
	else if (lpBuffer == nullptr || cchLen == 0)
	{
		logRuntimeError("ExeReader") << "Cannot read version value, VerQueryValue("<<QString::fromWCharArray(SubBlock)<<") returned "<<lpBuffer<<','<<cchLen;
		return {};
	}

	return QString::fromWCharArray( reinterpret_cast< const wchar_t * >( lpBuffer ), int( cchLen ) - 1 );
}

static void extractVersionInfo( const Resource & res, ExeVersionInfo & verInfo )
{
	VS_FIXEDFILEINFO * rawVerInfo = getRawVersionInfo( res.data() );
	if (rawVerInfo)
	{
		verInfo.version.major = (rawVerInfo->dwFileVersionMS >> 16) & 0xffff;
		verInfo.version.minor = (rawVerInfo->dwFileVersionMS >>  0) & 0xffff;
		verInfo.version.patch = (rawVerInfo->dwFileVersionLS >> 16) & 0xffff;
		verInfo.version.build = (rawVerInfo->dwFileVersionLS >>  0) & 0xffff;
	}

	auto languages = getLangInfo( res.data() );
	if (!languages.empty())
	{
		verInfo.appName = getVerInfoValue( res.data(), languages[0], TEXT("ProductName") );
		verInfo.description = getVerInfoValue( res.data(), languages[0], TEXT("FileDescription") );
	}
}


//----------------------------------------------------------------------------------------------------------------------
//  PE file parsing

/*
class MappedFile {

 public:

	MappedFile() : hFile( INVALID_HANDLE_VALUE ), hMap( INVALID_HANDLE_VALUE ), lpBaseAddr( nullptr ), llSize(0) {}
	MappedFile( const MappedFile & other ) = delete;

	MappedFile( MappedFile && other )
	{
		hFile = other.hFile;            other.hFile = INVALID_HANDLE_VALUE;
		hMap = other.hMap;              other.hMap = INVALID_HANDLE_VALUE;
		lpBaseAddr = other.lpBaseAddr;  other.lpBaseAddr = nullptr;
		llSize = other.llSize;          other.llSize = 0;
	}

	~MappedFile()
	{
		if (lpBaseAddr)
			UnmapViewOfFile( lpBaseAddr );
		if (hMap != INVALID_HANDLE_VALUE)
			CloseHandle( hMap );
		if (hFile != INVALID_HANDLE_VALUE)
			CloseHandle( hFile );
	}

	bool isOpen() const     { return lpBaseAddr != nullptr; }

	auto baseAddr() const   { return reinterpret_cast< uint8_t * >( lpBaseAddr ); }
	auto size() const       { return size_t( llSize ); }
	auto begin() const      { return baseAddr(); }
	auto end() const        { return baseAddr() + size(); }

	template< typename Field >
	Field * fieldAtOffset( LONGLONG offset )
	{
		if (size_t(offset) + sizeof(Field) > size())
			return nullptr;
		return reinterpret_cast< Field * >( baseAddr() + offset );
	}

 private:

	friend MappedFile mapFileToMemory( const QString & filePath );

	HANDLE hFile;
	HANDLE hMap;
	void * lpBaseAddr;
	LONGLONG llSize;

};

MappedFile mapFileToMemory( const QString & filePath )
{
	MappedFile m;

	// this can take up to 1 second sometimes, whyyy?! antivirus?
	m.hFile = CreateFile( filePath.toStdWString().c_str(),
		GENERIC_READ,           // dwDesiredAccess
		0,                      // dwShareMode
		nullptr,                // lpSecurityAttributes
		OPEN_EXISTING,          // dwCreationDisposition
		FILE_ATTRIBUTE_NORMAL,  // dwFlagsAndAttributes
		nullptr                 // hTemplateFile
	);
	if (m.hFile == INVALID_HANDLE_VALUE)
	{
		auto lastError = GetLastError();
		logRuntimeError("ExeReader") << "Cannot map file to memory, CreateFile() failed with error " << lastError;
		return {};
	}

	LARGE_INTEGER liFileSize;
	if (!GetFileSizeEx( m.hFile, &liFileSize ))
	{
		auto lastError = GetLastError();
		logRuntimeError("ExeReader") << "Cannot map file to memory, GetFileSize() failed with error " << lastError;
		return {};
	}
	m.llSize = liFileSize.QuadPart;

	if (liFileSize.QuadPart == 0)
	{
		logRuntimeError("ExeReader") << "Cannot map file to memory, file is empty";
		return {};
	}

	m.hMap = CreateFileMapping( m.hFile,
		nullptr,        // Mapping attributes
		PAGE_READONLY,  // Protection flags
		0,              // MaximumSizeHigh
		0,              // MaximumSizeLow
		nullptr         // Name
	);
	if (m.hMap == INVALID_HANDLE_VALUE)
	{
		auto lastError = GetLastError();
		logRuntimeError("ExeReader") << "Cannot map file to memory, CreateFileMapping() failed with error " << lastError;
		return {};
	}

	m.lpBaseAddr = MapViewOfFile( m.hMap,
		FILE_MAP_READ,  // dwDesiredAccess
		0,              // dwFileOffsetHigh
		0,              // dwFileOffsetLow
		0               // dwNumberOfBytesToMap
	);
	if (m.lpBaseAddr == nullptr)
	{
		auto lastError = GetLastError();
		logRuntimeError("ExeReader") << "Cannot map file to memory, MapViewOfFile() failed with error " << lastError;
		return {};
	}

	return m;
}

// read PE file using manual memory navigation
ExeVersionInfo readVersionInfoFromPE( const QString & filePath )
{
	ExeVersionInfo verInfo;

 #if IS_WINDOWS

	MappedFile mappedFile = mapFileToMemory( filePath );
	if (!mappedFile.isOpen())
	{
		verInfo.status = ReadStatus::CantOpen;
		return verInfo;
	}

	const auto * dosHeader = mappedFile.fieldAtOffset< IMAGE_DOS_HEADER >( 0 );
	if (dosHeader == nullptr)
	{
		logDebug("ExeReader") << "Cannot read IMAGE_DOS_HEADER";
		verInfo.status = ReadStatus::InvalidFormat;
		return verInfo;
	}

	const auto * ntHeaders = mappedFile.fieldAtOffset< IMAGE_NT_HEADERS >( dosHeader->e_lfanew );
	if (ntHeaders == nullptr)
	{
		logDebug("ExeReader") << "Cannot read IMAGE_NT_HEADERS at " << Qt::hex << dosHeader->e_lfanew;
		verInfo.status = ReadStatus::InvalidFormat;
		return verInfo;
	}

	const IMAGE_DATA_DIRECTORY * resourceDirLocation = nullptr;
	if (ntHeaders->FileHeader.Machine == IMAGE_FILE_MACHINE_I386)
	{
		auto * ntHeaders32 = reinterpret_cast< const IMAGE_NT_HEADERS32 * >( ntHeaders );
		resourceDirLocation = &ntHeaders32->OptionalHeader.DataDirectory[ IMAGE_DIRECTORY_ENTRY_RESOURCE ];
	}
	else if (ntHeaders->FileHeader.Machine == IMAGE_FILE_MACHINE_AMD64)
	{
		auto * ntHeaders64 = reinterpret_cast< const IMAGE_NT_HEADERS64 * >( ntHeaders );
		resourceDirLocation = &ntHeaders64->OptionalHeader.DataDirectory[ IMAGE_DIRECTORY_ENTRY_RESOURCE ];
	}
	else
	{
		logDebug("ExeReader") << "Unknown CPU architecture: " << Qt::hex << ntHeaders->FileHeader.Machine;
		verInfo.status = ReadStatus::InvalidFormat;
		return verInfo;
	}

	const auto * resourceDir = mappedFile.fieldAtOffset< IMAGE_RESOURCE_DIRECTORY >( resourceDirLocation->VirtualAddress );
	if (resourceDir == nullptr)
	{
		logDebug("ExeReader") << "Cannot read IMAGE_RESOURCE_DIRECTORY at " << Qt::hex << resourceDirLocation->VirtualAddress;
		verInfo.status = ReadStatus::InvalidFormat;
		return verInfo;
	}

	// fuck this shit

	verInfo.status = ReadStatus::Uninitialized;

 #else

	verInfo.status = ReadStatus::Uninitialized;

 #endif

	return verInfo;
}

/*/

// read PE file using LoadLibrary and FindResource,LoadResource flow
ExeVersionInfo readVersionInfoUsingWinAPI( const QString & filePath )
{
	ExeVersionInfo verInfo;

	// this can take up to 1 second sometimes, whyyy?! antivirus?
	HMODULE hExeModule = LoadLibraryEx( filePath.toStdWString().c_str(), nullptr, LOAD_LIBRARY_AS_DATAFILE );
	if (!hExeModule)
	{
		auto lastError = GetLastError();
		logRuntimeError("ExeReader") << "Cannot open "<<filePath<<", LoadLibraryEx() failed with error "<<lastError;
		verInfo.status = ReadStatus::CantOpen;
		return verInfo;
	}
	auto moduleGuard = autoClosable( hExeModule, FreeLibrary );

	Resource resource = getResource( filePath, hExeModule, RT_VERSION );
	if (!resource)
	{
		verInfo.status = ReadStatus::InfoNotPresent;
		return verInfo;
	}

	extractVersionInfo( resource, verInfo );

	verInfo.status = ReadStatus::Success;
	return verInfo;
}

/**/

#endif // IS_WINDOWS


//======================================================================================================================
//  public API

ExeVersionInfo readExeVersionInfo( [[maybe_unused]] const QString & filePath )
{
 #if IS_WINDOWS
	return readVersionInfoUsingWinAPI( filePath );
 #else
	return { {}, ReadStatus::NotSupported };
 #endif
}


FileInfoCache< ExeVersionInfo_ > g_cachedExeInfo( readExeVersionInfo );


//----------------------------------------------------------------------------------------------------------------------
//  serialization

void ExeVersionInfo_::serialize( QJsonObject & jsExeInfo ) const
{
	jsExeInfo["app_name"] = appName;
	jsExeInfo["description"] = description;
	jsExeInfo["version"] = version.toString();
}

void ExeVersionInfo_::deserialize( const JsonObjectCtx & jsExeInfo )
{
	appName = jsExeInfo.getString("app_name");
	description = jsExeInfo.getString("description");
	version = Version( jsExeInfo.getString("version") );
}


} // namespace os
