/* Iris Color 0.7.0 legacy-layout compatibility bootstrap (Windows x64).
 * No third-party runtime; the checked NSIS v0.7.0 installer is appended at build time.
 * Safely backs up a single-file VST3 or reparse point before launching NSIS.
 * Existing directory bundles are passed through. Never deletes the legacy backup.
 */
#include <stddef.h>
#include <stdint.h>

typedef void *HANDLE;
typedef void *HWND;
typedef void *HINSTANCE;
typedef unsigned long DWORD;
typedef unsigned short WORD;
typedef int BOOL;
typedef long LONG;
typedef wchar_t WCHAR;
typedef const WCHAR *LPCWSTR;
typedef WCHAR *LPWSTR;

#define DLLIMPORT __declspec(dllimport)
#define WINAPI __stdcall
#define INVALID_HANDLE_VALUE ((HANDLE)(intptr_t)-1)
#define INVALID_FILE_ATTRIBUTES 0xFFFFFFFFul
#define FILE_ATTRIBUTE_DIRECTORY 0x10ul
#define FILE_ATTRIBUTE_REPARSE_POINT 0x400ul
#define FILE_ATTRIBUTE_NORMAL 0x80ul
#define FILE_FLAG_BACKUP_SEMANTICS 0x02000000ul
#define FILE_FLAG_OPEN_REPARSE_POINT 0x00200000ul
#define GENERIC_READ 0x80000000ul
#define GENERIC_WRITE 0x40000000ul
#define OPEN_EXISTING 3ul
#define CREATE_NEW 1ul
#define FILE_SHARE_READ 1ul
#define FILE_BEGIN 0ul
#define FILE_END 2ul
#define INFINITE 0xFFFFFFFFul
#define MB_OK 0ul
#define MB_ICONERROR 0x10ul
#define MB_ICONINFORMATION 0x40ul
#define SW_SHOWNORMAL 1
#define CREATE_DEFAULT_ERROR_MODE 0x04000000ul

DLLIMPORT DWORD WINAPI GetModuleFileNameW(HANDLE,LPWSTR,DWORD);
DLLIMPORT DWORD WINAPI GetFileAttributesW(LPCWSTR);
DLLIMPORT HANDLE WINAPI CreateFileW(LPCWSTR,DWORD,DWORD,void*,DWORD,DWORD,HANDLE);
DLLIMPORT BOOL WINAPI CloseHandle(HANDLE);
DLLIMPORT BOOL WINAPI ReadFile(HANDLE,void*,DWORD,DWORD*,void*);
DLLIMPORT BOOL WINAPI WriteFile(HANDLE,const void*,DWORD,DWORD*,void*);
DLLIMPORT DWORD WINAPI GetFileSize(HANDLE,DWORD*);
DLLIMPORT DWORD WINAPI SetFilePointer(HANDLE,LONG,LONG*,DWORD);
DLLIMPORT BOOL WINAPI CreateDirectoryW(LPCWSTR,void*);
DLLIMPORT BOOL WINAPI MoveFileExW(LPCWSTR,LPCWSTR,DWORD);
DLLIMPORT BOOL WINAPI DeleteFileW(LPCWSTR);
DLLIMPORT DWORD WINAPI GetTempPathW(DWORD,LPWSTR);
DLLIMPORT unsigned int WINAPI GetTempFileNameW(LPCWSTR,LPCWSTR,unsigned int,LPWSTR);
DLLIMPORT BOOL WINAPI CreateProcessW(LPCWSTR,LPWSTR,void*,void*,BOOL,DWORD,void*,LPCWSTR,void*,void*);
DLLIMPORT DWORD WINAPI WaitForSingleObject(HANDLE,DWORD);
DLLIMPORT BOOL WINAPI GetExitCodeProcess(HANDLE,DWORD*);
DLLIMPORT void WINAPI ExitProcess(DWORD);
DLLIMPORT int WINAPI MessageBoxW(HWND,LPCWSTR,LPCWSTR,unsigned int);
DLLIMPORT HINSTANCE WINAPI ShellExecuteW(HWND,LPCWSTR,LPCWSTR,LPCWSTR,LPCWSTR,int);
DLLIMPORT BOOL WINAPI IsUserAnAdmin(void);
DLLIMPORT LONG WINAPI SHGetFolderPathW(HWND,int,HANDLE,DWORD,LPWSTR);

typedef struct {
    DWORD cb; LPWSTR lpReserved; LPWSTR lpDesktop; LPWSTR lpTitle;
    DWORD dwX,dwY,dwXSize,dwYSize,dwXCountChars,dwYCountChars,dwFillAttribute;
    DWORD dwFlags; WORD wShowWindow,cbReserved2; unsigned char* lpReserved2;
    HANDLE hStdInput,hStdOutput,hStdError;
} STARTUPINFOW;
typedef struct {HANDLE hProcess,hThread;DWORD dwProcessId,dwThreadId;} PROCESS_INFORMATION;

/* Footer is the last 16 bytes of the wrapped EXE. */
typedef struct { char magic[8]; uint32_t payload_bytes, payload_crc32; } FOOTER;
static WCHAR self_path[2048], common[2048], program_files[2048];
static WCHAR bundle[2048], meta[2048], backups[2048], legacy_backup[2048];
static WCHAR temp_path[2048], temp_tmp[2048], temp_exe[2048], command[4096];
static unsigned char io_buffer[32768];

static int append(WCHAR *dest, unsigned int cap, LPCWSTR src) {
    unsigned int i=0,j=0;
    while (i<cap && dest[i]) ++i;
    if (i==cap) return 0;
    while (src[j]) {
        if (i+1>=cap) return 0;
        dest[i++]=src[j++];
    }
    dest[i]=0;
    return 1;
}
static void copy(WCHAR *out,unsigned int cap,LPCWSTR in) {out[0]=0;append(out,cap,in);}
static void zero(void *p, unsigned int bytes){ unsigned char *b=(unsigned char *)p;while(bytes--)*b++=0; }
static int has_file(LPCWSTR path){return GetFileAttributesW(path)!=INVALID_FILE_ATTRIBUTES;}
static int normal_directory(LPCWSTR path){
    DWORD a=GetFileAttributesW(path);
    return a!=INVALID_FILE_ATTRIBUTES && (a&FILE_ATTRIBUTE_DIRECTORY)!=0 && (a&FILE_ATTRIBUTE_REPARSE_POINT)==0;
}
static int ensure_dir(LPCWSTR path){
    if (normal_directory(path))return 1;
    if (has_file(path))return 0;
    return CreateDirectoryW(path,0) && normal_directory(path);
}
static void error(LPCWSTR s){MessageBoxW(0,s,L"Iris Color installer repair",MB_OK|MB_ICONERROR);}
static void info(LPCWSTR s){MessageBoxW(0,s,L"Iris Color installer repair",MB_OK|MB_ICONINFORMATION);}
static uint32_t crc_step(uint32_t crc,const unsigned char *data,DWORD n){
    DWORD i;int j;
    for(i=0;i<n;i++){
        crc^=data[i];
        for(j=0;j<8;j++) crc=(crc>>1)^((crc&1)?0xEDB88320ul:0);
    }
    return crc;
}
static void decimal4(WCHAR *p,unsigned int n){
    p[0]=(WCHAR)('0'+((n/1000)%10));p[1]=(WCHAR)('0'+((n/100)%10));
    p[2]=(WCHAR)('0'+((n/10)%10));p[3]=(WCHAR)('0'+(n%10));p[4]=0;
}
static int extract_installer(void){
    HANDLE input=INVALID_HANDLE_VALUE, output=INVALID_HANDLE_VALUE;
    FOOTER footer;DWORD bytes,got,put,left;uint32_t crc=0xFFFFFFFFul;int ok=0;
    input=CreateFileW(self_path,GENERIC_READ,FILE_SHARE_READ,0,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,0);
    if(input==INVALID_HANDLE_VALUE)goto finish;
    bytes=GetFileSize(input,0);
    if(bytes==INVALID_FILE_ATTRIBUTES || bytes<sizeof(FOOTER)+1024)goto finish;
    if(SetFilePointer(input,-(LONG)sizeof(FOOTER),0,FILE_END)==INVALID_FILE_ATTRIBUTES)goto finish;
    if(!ReadFile(input,&footer,sizeof(FOOTER),&got,0)||got!=sizeof(FOOTER))goto finish;
    {
        static const char magic[8]={'I','R','I','S','0','7','F','1'};
        int i;for(i=0;i<8;i++)if(footer.magic[i]!=magic[i])goto finish;
    }
    if(footer.payload_bytes<1024 || footer.payload_bytes>32*1024*1024 || footer.payload_bytes>bytes-sizeof(FOOTER))goto finish;
    if(SetFilePointer(input,(LONG)(bytes-sizeof(FOOTER)-footer.payload_bytes),0,FILE_BEGIN)==INVALID_FILE_ATTRIBUTES)goto finish;
    if(!GetTempPathW(2048,temp_path))goto finish;
    if(!GetTempFileNameW(temp_path,L"IR7",0,temp_tmp))goto finish;
    copy(temp_exe,2048,temp_tmp);
    if(!append(temp_exe,2048,L".exe"))goto finish;
    output=CreateFileW(temp_exe,GENERIC_WRITE,0,0,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,0);
    if(output==INVALID_HANDLE_VALUE)goto finish;
    left=footer.payload_bytes;
    while(left){
        DWORD amt=left>(DWORD)sizeof(io_buffer)?(DWORD)sizeof(io_buffer):left;
        if(!ReadFile(input,io_buffer,amt,&got,0)||got!=amt)goto finish;
        crc=crc_step(crc,io_buffer,got);
        if(!WriteFile(output,io_buffer,got,&put,0)||put!=got)goto finish;
        left-=got;
    }
    if((crc^0xFFFFFFFFul)!=footer.payload_crc32)goto finish;
    ok=1;
finish:
    if(input!=INVALID_HANDLE_VALUE)CloseHandle(input);
    if(output!=INVALID_HANDLE_VALUE)CloseHandle(output);
    if(temp_tmp[0])DeleteFileW(temp_tmp);
    if(!ok && temp_exe[0])DeleteFileW(temp_exe);
    return ok;
}
static int migrate_legacy(void){
    DWORD attr=GetFileAttributesW(bundle); HANDLE handle; unsigned int n; WCHAR digits[5];
    if(attr==INVALID_FILE_ATTRIBUTES)return 1;
    if((attr&FILE_ATTRIBUTE_DIRECTORY) && !(attr&FILE_ATTRIBUTE_REPARSE_POINT))return 1;
    if(!ensure_dir(meta) || !ensure_dir(backups)){
        error(L"Could not create a secure backup folder under Program Files. No plugin file was changed.");
        return 0;
    }
    handle=CreateFileW(bundle,GENERIC_READ|GENERIC_WRITE,0,0,OPEN_EXISTING,
        (attr&FILE_ATTRIBUTE_REPARSE_POINT?FILE_FLAG_OPEN_REPARSE_POINT:FILE_ATTRIBUTE_NORMAL) |
        (attr&FILE_ATTRIBUTE_DIRECTORY?FILE_FLAG_BACKUP_SEMANTICS:0),0);
    if(handle==INVALID_HANDLE_VALUE){
        error(L"The existing VST3 cannot be opened exclusively. Close FL Studio and all plugin hosts, then run Setup again. Existing files were not changed.");
        return 0;
    }
    CloseHandle(handle);
    for(n=1;n<=9999;n++){
        copy(legacy_backup,2048,backups);
        if(!append(legacy_backup,2048,L"\\Iris Color.vst3.legacy-"))break;
        decimal4(digits,n);
        if(!append(legacy_backup,2048,digits)||!append(legacy_backup,2048,L".bak"))break;
        if(!has_file(legacy_backup)){
            if(!MoveFileExW(bundle,legacy_backup,0)){
                error(L"Unable to back up the old single-file VST3 or symlink. Close DAWs and check folder permissions. Nothing was deleted.");
                return 0;
            }
            return 1;
        }
    }
    error(L"Legacy plugin backup directory has no unused slot. No files were changed.");
    return 0;
}
static DWORD install_child(void){
    STARTUPINFOW si;PROCESS_INFORMATION pi;DWORD exit_code=1603;
    zero(&si,sizeof(si));zero(&pi,sizeof(pi));si.cb=sizeof(si);
    copy(command,4096,L"\"");
    if(!append(command,4096,temp_exe)||!append(command,4096,L"\""))return 1603;
    if(!CreateProcessW(0,command,0,0,0,CREATE_DEFAULT_ERROR_MODE,0,0,&si,&pi))return 1603;
    WaitForSingleObject(pi.hProcess,INFINITE);
    GetExitCodeProcess(pi.hProcess,&exit_code);
    CloseHandle(pi.hThread);CloseHandle(pi.hProcess);
    return exit_code;
}
static void restore_if_possible(void){
    if(legacy_backup[0] && !has_file(bundle) && has_file(legacy_backup)){
        MoveFileExW(legacy_backup,bundle,0);
    }
}
void mainCRTStartup(void){
    DWORD result=1603;DWORD attr;WCHAR installed[2048];HINSTANCE elevated;
    if(GetModuleFileNameW(0,self_path,2048)==0 || !IsUserAnAdmin()){
        if(self_path[0]){
            elevated=ShellExecuteW(0,L"runas",self_path,0,0,SW_SHOWNORMAL);
            if((intptr_t)elevated<=32)error(L"Administrator permission is required to install into the system VST3 folder.");
        } else error(L"Unable to locate the installer executable.");
        ExitProcess(0);
    }
    if(SHGetFolderPathW(0,0x002B,0,0,common)!=0 || SHGetFolderPathW(0,0x0026,0,0,program_files)!=0){
        error(L"Unable to locate Program Files/Common Files in Windows.");ExitProcess(1603);
    }
    copy(bundle,2048,common);append(bundle,2048,L"\\VST3\\Iris Color.vst3");
    copy(meta,2048,program_files);append(meta,2048,L"\\Iris Color");
    copy(backups,2048,meta);append(backups,2048,L"\\Legacy-Backups");
    if(!extract_installer()){
        error(L"Installer payload validation failed. The executable may be incomplete. No plugin file was changed.");ExitProcess(1603);
    }
    if(!migrate_legacy()){
        DeleteFileW(temp_exe);ExitProcess(1603);
    }
    result=install_child();
    DeleteFileW(temp_exe);
    copy(installed,2048,bundle);append(installed,2048,L"\\Contents\\x86_64-win\\Iris Color.vst3");
    attr=GetFileAttributesW(bundle);
    if(result==0 && attr!=INVALID_FILE_ATTRIBUTES && (attr&FILE_ATTRIBUTE_DIRECTORY) && !(attr&FILE_ATTRIBUTE_REPARSE_POINT) && has_file(installed)){
        if(legacy_backup[0])info(L"Installation completed. The previous single-file VST3/link was preserved in Program Files\\Iris Color\\Legacy-Backups.");
        ExitProcess(0);
    }
    restore_if_possible();
    error(L"Installation was canceled or failed. If the original VST3 could not be restored automatically, find its backup in Program Files\\Iris Color\\Legacy-Backups. Close DAWs and retry.");
    ExitProcess(result?result:1603);
}
