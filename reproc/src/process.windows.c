#ifndef _WIN32_WINNT
  #define _WIN32_WINNT 0x0600 // _WIN32_WINNT_VISTA
#elif _WIN32_WINNT < 0x0600
  #error "_WIN32_WINNT must be greater than _WIN32_WINNT_VISTA (0x0600)"
#endif

#include "process.h"

#include <stdbool.h>
#include <stdlib.h>
#include <windows.h>

#include "error.h"
#include "macro.h"
#include "utf.h"

const HANDLE PROCESS_INVALID = INVALID_HANDLE_VALUE; // NOLINT

static const DWORD CREATION_FLAGS =
    // Create each child process in a new process group so we don't send
    // `CTRL-BREAK` signals to more than one child process in
    // `process_terminate`.
    CREATE_NEW_PROCESS_GROUP |
    // Create each child process with a Unicode environment as we accept any
    // UTF-16 encoded environment (including Unicode characters). Create each
    CREATE_UNICODE_ENVIRONMENT;

// Argument escaping implementation is based on the following blog post:
// https://blogs.msdn.microsoft.com/twistylittlepassagesallalike/2011/04/23/everyone-quotes-command-line-arguments-the-wrong-way/

static bool argument_should_escape(const char *argument)
{
  bool should_escape = false;
  size_t i = 0;

  ASSERT(argument);

  for (i = 0; i < strlen(argument); i++) {
    should_escape = should_escape || argument[i] == ' ' ||
                    argument[i] == '\t' || argument[i] == '\n' ||
                    argument[i] == '\v' || argument[i] == '\"';
  }

  return should_escape;
}

static size_t argument_escaped_size(const char *argument)
{
  size_t argument_size = 0;
  size_t size = 2; // double quotes
  size_t i = 0;

  ASSERT(argument);

  argument_size = strlen(argument);

  if (!argument_should_escape(argument)) {
    return argument_size;
  }

  for (i = 0; i < argument_size; i++) {
    size_t num_backslashes = 0;

    while (i < argument_size && argument[i] == '\\') {
      i++;
      num_backslashes++;
    }

    if (i == argument_size) {
      size += num_backslashes * 2;
    } else if (argument[i] == '"') {
      size += num_backslashes * 2 + 2;
    } else {
      size += num_backslashes + 1;
    }
  }

  return size;
}

static size_t argument_escape(char *dest, const char *argument)
{
  size_t argument_size = 0;
  const char *begin = dest;
  size_t i = 0;

  ASSERT(dest);
  ASSERT(argument);

  argument_size = strlen(argument);

  if (!argument_should_escape(argument)) {
    strcpy(dest, argument); // NOLINT
    return argument_size;
  }

  *dest++ = '"';

  for (i = 0; i < argument_size; i++) {
    size_t num_backslashes = 0;

    while (i < argument_size && argument[i] == '\\') {
      i++;
      num_backslashes++;
    }

    if (i == argument_size) {
      memset(dest, '\\', num_backslashes * 2);
      dest += num_backslashes * 2;
    } else if (argument[i] == '"') {
      memset(dest, '\\', num_backslashes * 2 + 1);
      dest += num_backslashes * 2 + 1;
      *dest++ = '"';
    } else {
      memset(dest, '\\', num_backslashes);
      dest += num_backslashes;
      *dest++ = argument[i];
    }
  }

  *dest++ = '"';

  return (size_t) (dest - begin);
}

static char *argv_join(const char *const *argv)
{
  // Determine the size of the concatenated string first.
  size_t joined_size = 1; // Count the NUL terminator.
  int i = 0;
  char *joined = NULL;
  char *current = NULL;

  ASSERT(argv);

  for (i = 0; argv[i] != NULL; i++) {
    joined_size += argument_escaped_size(argv[i]);

    if (argv[i + 1] != NULL) {
      joined_size++; // Count whitespace.
    }
  }

  joined = calloc(joined_size, sizeof(char));
  if (joined == NULL) {
    SetLastError(ERROR_NOT_ENOUGH_MEMORY);
    return NULL;
  }

  current = joined;
  for (i = 0; argv[i] != NULL; i++) {
    current += argument_escape(current, argv[i]);

    // We add a space after each argument in the joined arguments string except
    // for the final argument.
    if (argv[i + 1] != NULL) {
      *current++ = ' ';
    }
  }

  *current = '\0';

  return joined;
}

static size_t env_join_size(const char *const *env)
{
  size_t joined_size = 1; // Count the NUL terminator.
  int i = 0;

  ASSERT(env);

  for (i = 0; env[i] != NULL; i++) {
    joined_size += strlen(env[i]) + 1; // Count the NUL terminator.
  }

  return joined_size;
}

typedef BOOL(WINAPI *InitializeProcThreadAttributeListFunction)(
    LPPROC_THREAD_ATTRIBUTE_LIST lpAttributeList,
    DWORD dwAttributeCount,
    DWORD dwFlags,
    PSIZE_T lpSize);

typedef BOOL(WINAPI *UpdateProcThreadAttributeFunction)(
    LPPROC_THREAD_ATTRIBUTE_LIST lpAttributeList,
    DWORD dwFlags,
    DWORD_PTR Attribute,
    PVOID lpValue,
    SIZE_T cbSize,
    PVOID lpPreviousValue,
    PSIZE_T lpReturnSize);

typedef VOID(WINAPI *DeleteProcThreadAttributeListFunction)(
    LPPROC_THREAD_ATTRIBUTE_LIST lpAttributeList);

static InitializeProcThreadAttributeListFunction
    pInitializeProcThreadAttributeList = /* NOLINT */
    NULL;

static UpdateProcThreadAttributeFunction
    pUpdateProcThreadAttribute = /* NOLINT */
    NULL;

static DeleteProcThreadAttributeListFunction
    pDeleteProcThreadAttributeList = /* NOLINT */
    NULL;

static BOOL EnableDebugPrivilege(void)
{
  HANDLE hToken = INVALID_HANDLE_VALUE;
  BOOL fOk = FALSE;
  if (OpenProcessToken(GetCurrentProcess(), TOKEN_ADJUST_PRIVILEGES, &hToken)) {
    TOKEN_PRIVILEGES tp;
    tp.PrivilegeCount = 1;
    LookupPrivilegeValue(NULL, SE_DEBUG_NAME, &tp.Privileges[0].Luid);
    tp.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;

    AdjustTokenPrivileges(hToken, FALSE, &tp, sizeof(tp), NULL, NULL);

    fOk = (GetLastError() == ERROR_SUCCESS);
    CloseHandle(hToken);
  }
  return fOk;
}

static void init_once(void)
{
  HANDLE kernel32 = GetModuleHandleW(L"kernel32.dll");
  pInitializeProcThreadAttributeList =
      (InitializeProcThreadAttributeListFunction) (uintptr_t)
          GetProcAddress(kernel32, "InitializeProcThreadAttributeList");
  pUpdateProcThreadAttribute = (UpdateProcThreadAttributeFunction) (uintptr_t)
      GetProcAddress(kernel32, "UpdateProcThreadAttribute");
  pDeleteProcThreadAttributeList =
      (DeleteProcThreadAttributeListFunction) (uintptr_t)
          GetProcAddress(kernel32, "DeleteProcThreadAttributeList");
  EnableDebugPrivilege();
}

static uintptr_t init_once_flag = REPROC_ONCE_FLAG_INIT; // NOLINT

static void wpInit()
{
  reproc_call_once(&init_once_flag, init_once);
}

static BOOL wpInitializeProcThreadAttributeList(
    LPPROC_THREAD_ATTRIBUTE_LIST lpAttributeList,
    DWORD dwAttributeCount,
    DWORD dwFlags,
    PSIZE_T lpSize)
{
  if (pInitializeProcThreadAttributeList != NULL) {
    return pInitializeProcThreadAttributeList(lpAttributeList, dwAttributeCount,
                                              dwFlags, lpSize);
  }
  return FALSE;
}

static BOOL
wpUpdateProcThreadAttribute(LPPROC_THREAD_ATTRIBUTE_LIST lpAttributeList,
                            DWORD dwFlags,
                            DWORD_PTR Attribute,
                            PVOID lpValue,
                            SIZE_T cbSize,
                            PVOID lpPreviousValue,
                            PSIZE_T lpReturnSize)
{
  if (pUpdateProcThreadAttribute != NULL) {
    return pUpdateProcThreadAttribute(lpAttributeList, dwFlags, Attribute,
                                      lpValue, cbSize, lpPreviousValue,
                                      lpReturnSize);
  }
  return FALSE;
}

static VOID
wpDeleteProcThreadAttributeList(LPPROC_THREAD_ATTRIBUTE_LIST lpAttributeList)
{
  if (pDeleteProcThreadAttributeList != NULL) {
    pDeleteProcThreadAttributeList(lpAttributeList);
  }
}

static char *env_join(const char *const *env)
{
  char *joined = NULL;
  char *current = NULL;
  int i = 0;

  ASSERT(env);

  joined = calloc(env_join_size(env), sizeof(char));
  if (joined == NULL) {
    SetLastError(ERROR_NOT_ENOUGH_MEMORY);
    return NULL;
  }

  current = joined;
  for (i = 0; env[i] != NULL; i++) {
    size_t to_copy = strlen(env[i]) + 1; // Include NUL terminator.
    memcpy(current, env[i], to_copy);
    current += to_copy;
  }

  *current = '\0';

  return joined;
}

static const DWORD NUM_ATTRIBUTES = 1;

static LPPROC_THREAD_ATTRIBUTE_LIST setup_attribute_list(HANDLE *handles,
                                                         size_t num_handles)
{
  int r = -1;
  SIZE_T attribute_list_size = 0;
  LPPROC_THREAD_ATTRIBUTE_LIST attribute_list = NULL;

  ASSERT(handles);

  // Get the required size for `attribute_list`.
  r = wpInitializeProcThreadAttributeList(NULL, NUM_ATTRIBUTES, 0,
                                          &attribute_list_size);
  if (r == 0 && GetLastError() != ERROR_INSUFFICIENT_BUFFER) {
    return NULL;
  }

  if (attribute_list_size == 0) {
    return NULL;
  }

  attribute_list = malloc(attribute_list_size);
  if (attribute_list == NULL) {
    SetLastError(ERROR_NOT_ENOUGH_MEMORY);
    return NULL;
  }

  r = wpInitializeProcThreadAttributeList(attribute_list, NUM_ATTRIBUTES, 0,
                                          &attribute_list_size);
  if (r == 0) {
    free(attribute_list);
    return NULL;
  }

  // Add the handles to be inherited to `attribute_list`.
  r = wpUpdateProcThreadAttribute(attribute_list, 0,
                                  PROC_THREAD_ATTRIBUTE_HANDLE_LIST, handles,
                                  num_handles * sizeof(HANDLE), NULL, NULL);
  if (r == 0) {
    wpDeleteProcThreadAttributeList(attribute_list);
    free(attribute_list);
    return NULL;
  }

  return attribute_list;
}

#define NULSTR_FOREACH(i, l)                                                   \
  for ((i) = (l); (i) && *(i) != L'\0'; (i) = wcschr((i), L'\0') + 1)

static wchar_t *env_concat(const wchar_t *a, const wchar_t *b)
{
  const wchar_t *i = NULL;
  size_t size = 1;
  wchar_t *c = NULL;
  wchar_t *r = NULL;

  NULSTR_FOREACH(i, a) {
    size += wcslen(i) + 1;
  }

  NULSTR_FOREACH(i, b) {
    size += wcslen(i) + 1;
  }

  r = calloc(size, sizeof(wchar_t));
  if (!r) {
    return NULL;
  }

  c = r;

  NULSTR_FOREACH(i, a) {
    wcscpy(c, i);
    c += wcslen(i) + 1;
  }

  NULSTR_FOREACH(i, b) {
    wcscpy(c, i);
    c += wcslen(i) + 1;
  }

  *c = L'\0';

  return r;
}

static wchar_t *env_setup(REPROC_ENV behavior, const char *const *extra)
{
  wchar_t *env_parent_wstring = NULL;
  char *env_extra = NULL;
  wchar_t *env_extra_wstring = NULL;
  wchar_t *env_wstring = NULL;

  if (behavior == REPROC_ENV_EXTEND) {
    env_parent_wstring = GetEnvironmentStringsW();
  }

  if (extra != NULL) {
    size_t joined_size = 0;
    env_extra = env_join(extra);
    if (env_extra == NULL) {
      goto finish;
    }

    joined_size = env_join_size(extra);
    ASSERT(joined_size <= INT_MAX);

    env_extra_wstring = utf16_from_utf8(env_extra, (int) joined_size);
    if (env_extra_wstring == NULL) {
      goto finish;
    }
  }

  env_wstring = env_concat(env_parent_wstring, env_extra_wstring);
  if (env_wstring == NULL) {
    goto finish;
  }

finish:
  FreeEnvironmentStringsW(env_parent_wstring);
  free(env_extra);
  free(env_extra_wstring);

  return env_wstring;
}

int process_start(reproc_t *process,
                  const char *const *argv,
                  struct process_options options)
{
  char *command_line = NULL;
  wchar_t *command_line_wstring = NULL;
  wchar_t *env_wstring = NULL;
  wchar_t *working_directory_wstring = NULL;
  LPPROC_THREAD_ATTRIBUTE_LIST attribute_list = NULL;
  PROCESS_INFORMATION info = { PROCESS_INVALID, HANDLE_INVALID, 0, 0 };
  PROCESS_INFORMATION *pi = NULL;
  int r = -1;
  HANDLE handles[] = { options.handle.exit, options.handle.in,
                       options.handle.out, options.handle.err };
  size_t num_handles = ARRAY_SIZE(handles);
  STARTUPINFOEXW extended_startup_info = { 0 };
  LPSTARTUPINFOW startup_info_address = NULL;
  SECURITY_ATTRIBUTES do_not_inherit = { 0 };
  DWORD previous_error_mode = 0;
  DWORD creation_flags = CREATION_FLAGS;

  ASSERT(process);

  if (argv == NULL) {
    return -ERROR_CALL_NOT_IMPLEMENTED;
  }

  ASSERT(argv[0] != NULL);

  pi = malloc(sizeof(PROCESS_INFORMATION));
  if (pi == NULL) {
    return -ERROR_NOT_ENOUGH_SERVER_MEMORY;
  }

  wpInit();

  // Join `argv` to a whitespace delimited string as required by
  // `CreateProcessW`.
  command_line = argv_join(argv);
  if (command_line == NULL) {
    r = -(int) GetLastError();
    goto finish;
  }

  // Convert UTF-8 to UTF-16 as required by `CreateProcessW`.
  command_line_wstring = utf16_from_utf8(command_line, -1);
  if (command_line_wstring == NULL) {
    r = -(int) GetLastError();
    goto finish;
  }

  // Idem for `working_directory` if it isn't `NULL`.
  if (options.working_directory != NULL) {
    working_directory_wstring = utf16_from_utf8(options.working_directory, -1);
    if (working_directory_wstring == NULL) {
      r = -(int) GetLastError();
      goto finish;
    }
  }

  env_wstring = env_setup(options.env.behavior, options.env.extra);
  if (env_wstring == NULL) {
    r = -(int) GetLastError();
    goto finish;
  }

  // Windows Vista added the `STARTUPINFOEXW` structure in which we can put a
  // list of handles that should be inherited. Only these handles are inherited
  // by the child process. Other code in an application that calls
  // `CreateProcess` without passing a `STARTUPINFOEXW` struct containing the
  // handles it should inherit can still unintentionally inherit handles meant
  // for a reproc child process. See https://stackoverflow.com/a/2345126 for
  // more information.

  if (options.handle.out == options.handle.err) {
    // CreateProcess doesn't like the same handle being specified twice in the
    // `PROC_THREAD_ATTRIBUTE_HANDLE_LIST` attribute.
    num_handles--;
  }

  {
    size_t i = 0;
    // Make sure all the given handles can be inherited.
    for (i = 0; i < num_handles; i++) {
      r = SetHandleInformation(handles[i], HANDLE_FLAG_INHERIT,
                               HANDLE_FLAG_INHERIT);
      if (r == 0) {
        r = -(int) GetLastError();
        goto finish;
      }
    }
  }

  if (reproc_windows_version_greator_or_equal(_WIN32_WINNT_VISTA, 0)) {
    attribute_list = setup_attribute_list(handles, num_handles);
    if (attribute_list == NULL) {
      r = -(int) GetLastError();
      goto finish;
    }
  }

  extended_startup_info.StartupInfo.hStdInput = options.handle.in;
  extended_startup_info.StartupInfo.hStdOutput = options.handle.out;
  extended_startup_info.StartupInfo.hStdError = options.handle.err;
  extended_startup_info.StartupInfo.dwFlags = STARTF_USESTDHANDLES;
  // `STARTF_USESHOWWINDOW`. Make sure the console window of
  // the child process isn't visible. See
  // https://github.com/DaanDeMeyer/reproc/issues/6 and
  // https://github.com/DaanDeMeyer/reproc/pull/7 for more
  // information.
  if (!options.show_console_window) {
    extended_startup_info.StartupInfo.dwFlags |= STARTF_USESHOWWINDOW;
    extended_startup_info.StartupInfo.wShowWindow = SW_SHOWNORMAL;
  }
  extended_startup_info.lpAttributeList = attribute_list;

  startup_info_address = &extended_startup_info.StartupInfo;

  // Child processes inherit the error mode of their parents. To avoid child
  // processes creating error dialogs we set our error mode to not create error
  // dialogs temporarily which is inherited by the child process.
  previous_error_mode = SetErrorMode(SEM_NOGPFAULTERRORBOX);

  do_not_inherit.nLength = sizeof(SECURITY_ATTRIBUTES);
  do_not_inherit.bInheritHandle = false;
  do_not_inherit.lpSecurityDescriptor = NULL;

  if (reproc_windows_version_greator_or_equal(_WIN32_WINNT_VISTA, 0)) {
    // Create each child with an extended STARTUPINFOEXW structure so we can
    // specify which handles should be inherited.
    creation_flags |= EXTENDED_STARTUPINFO_PRESENT;
    extended_startup_info.StartupInfo.cb = sizeof(extended_startup_info);
  } else {
    extended_startup_info.StartupInfo.cb = sizeof(
        extended_startup_info.StartupInfo);
  }

  r = CreateProcessW(NULL, command_line_wstring, &do_not_inherit,
                     &do_not_inherit, true, creation_flags, env_wstring,
                     working_directory_wstring, startup_info_address, &info);

  SetErrorMode(previous_error_mode);

  if (r == 0) {
    r = -(int) GetLastError();
    goto finish;
  }

  process->handle = info.hProcess;
  *pi = info;
  process->extra = pi;
  pi = NULL;
  r = 0;

finish :

{
  size_t i = 0;
  // Make sure all the given handles can not be inherited any more
  for (i = 0; i < num_handles; i++) {
    SetHandleInformation(handles[i], HANDLE_FLAG_INHERIT, 0);
  }
}

  if (command_line != NULL) {
    free(command_line);
  }
  if (command_line_wstring != NULL) {
    free(command_line_wstring);
  }
  if (env_wstring != NULL) {
    free(env_wstring);
  }
  if (working_directory_wstring != NULL) {
    free(working_directory_wstring);
  }
  if (attribute_list != NULL) {
    wpDeleteProcThreadAttributeList(attribute_list);
    free(attribute_list);
  }
  if (pi != NULL) {
    free(pi);
  }
  handle_destroy(info.hThread);

  return r < 0 ? r : 1;
}

int process_pid(process_type process)
{
  DWORD pid = 0;
  ASSERT(process);
  pid = GetProcessId(process);
  /*
  If the function succeeds, the return value is the process identifier.
  If the function fails, the return value is zero.
  To get extended error information, call GetLastError.
  */
  if (pid == 0) {
    return -(int) GetLastError();
  }
  return (int) pid;
}

int process_wait(HANDLE process, int timeout)
{
  int r = -1;
  DWORD status = 0;

  ASSERT(process);

  r = (int) WaitForSingleObject(process, (DWORD)timeout);
  if ((DWORD) r == WAIT_FAILED) {
    return -(int) GetLastError();
  }
  if ((DWORD) r == WAIT_TIMEOUT) {
    r = TerminateProcess(process, (DWORD) REPROC_SIGKILL);
    return r == 0 ? -(int) GetLastError() : 0;
  }

  r = GetExitCodeProcess(process, &status);
  if (r == 0) {
    return -(int) GetLastError();
  }

  // `GenerateConsoleCtrlEvent` causes a process to exit with this exit code.
  // Because `GenerateConsoleCtrlEvent` has roughly the same semantics as
  // `SIGTERM`, we map its exit code to `SIGTERM`.
  if (status == 0xC000013A) {
    status = (DWORD) REPROC_SIGTERM;
  }

  return (int) status;
}

int process_terminate(reproc_t *process)
{
  BOOL r = FALSE;
  PROCESS_INFORMATION *pi = NULL;

  ASSERT(process && process->handle != PROCESS_INVALID &&
         process->extra != NULL);

  pi = process->extra;
  // `GenerateConsoleCtrlEvent` can only be called on a process group. To call
  // `GenerateConsoleCtrlEvent` on a single child process it has to be put in
  // its own process group (which we did when starting the child process).
  r = GenerateConsoleCtrlEvent(CTRL_BREAK_EVENT, pi->dwProcessId);
  if (r == 0) {
    return -(int) GetLastError();
  }
  // Try exit the windows ui event loop to exit the application
  PostThreadMessageW(pi->dwThreadId, WM_QUIT, 0xC000013A, 0);
  SetLastError(0);
  return 0;
}

int process_kill(HANDLE process)
{
  BOOL r = FALSE;

  ASSERT(process && process != PROCESS_INVALID);

  // We use 137 (`SIGKILL`) as the exit status because it is the same exit
  // status as a process that is stopped with the `SIGKILL` signal on POSIX
  // systems.
  r = TerminateProcess(process, (DWORD) REPROC_SIGKILL);

  return r == 0 ? -(int) GetLastError() : 0;
}

HANDLE process_destroy(HANDLE process)
{
  return handle_destroy(process);
}
