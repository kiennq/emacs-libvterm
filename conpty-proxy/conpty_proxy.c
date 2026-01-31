#include <Windows.h>
#include <fcntl.h>
#include <io.h>
#include <locale.h>
#include <stdint.h>
#include <stdio.h>

#ifndef EXTENDED_STARTUPINFO_PRESENT
#define EXTENDED_STARTUPINFO_PRESENT 0x00080000
#endif
#ifndef PROC_THREAD_ATTRIBUTE_PSEUDOCONSOLE
#define PROC_THREAD_ATTRIBUTE_PSEUDOCONSOLE 0x00020016
#endif

#if !defined(ENABLE_VIRTUAL_TERMINAL_PROCESSING)
#define ENABLE_VIRTUAL_TERMINAL_PROCESSING 0x0004
#endif

#define COMPLETION_KEY_IO_READ (0x01)
#define COMPLETION_KEY_CTRL_ACCEPT (0x02)

enum {
    ERROR_INVALID_ARGC = 1,
    ERROR_ID_DUP,
    ERROR_CREATE_CTRL_PIPE_FAILED,
    ERROR_CONPTY_API_INIT_FAILED,
    ERROR_INVALID_SIZE,
    ERROR_CREATE_IN_PIPE_FAILED,
    ERROR_CREATE_OUT_PIPE_FAILED,
    ERROR_CREATE_PSEUDO_CONSOLE_FAILED,
    ERROR_OPEN_IN_PIPE_FAILED,
    ERROR_OPEN_OUT_PIPE_FAILED,
    ERROR_MALLOC_PROC_ATTR_FAILED,
    ERROR_INIT_PROC_ATTR_FAILED,
    ERROR_UPDATE_PROC_ATTR_FAILED,
    ERROR_CREATE_PROC_FAILED,
    ERROR_CREATE_MAIN_IOCP_FAILED,
    ERROR_RESIZE_ID_INVALID,
    ERROR_CREATE_IO_READ_IOCP_FAILED,
    ERROR_CREATE_CTRL_READ_IOCP_FAILED,
    ERROR_MAX
};

typedef struct {
    wchar_t* id;
    HANDLE ctrl_pipe;
    wchar_t* cmd;

    HANDLE iocp;
    HANDLE iocp_thread;

    HPCON hpc;
    int width;
    int height;

    HANDLE io_read;
    HANDLE io_write;
    HANDLE std_in;
    HANDLE std_out;

    HANDLE process;
    STARTUPINFOEXW si;

    char io_buf[8192];
    char std_buf[8192];
    char ctrl_buf[256];

    OVERLAPPED io_overl;
    OVERLAPPED ctrl_overl;
} conpty_t;

static conpty_t g_pty;

static HRESULT(WINAPI* g_create_pseudo_console)(COORD, HANDLE, HANDLE, DWORD, HPCON*);
static HRESULT(WINAPI* g_resize_pseudo_console)(HPCON, COORD);
static void(WINAPI* g_close_pseudo_console)(HPCON);

static int g_conpty_api_inited = 0;

static int conpty_api_init(void) {
    if (g_conpty_api_inited == -1) {
        return -1;
    }

    if (g_conpty_api_inited != 0) {
        return 0;
    }

    HMODULE kernel = LoadLibraryA("kernel32.dll");
    if (kernel == NULL) {
        g_conpty_api_inited = -1;
        return -1;
    }
    static struct {
        char* name;
        FARPROC* ptr;
    } conpty_fns[] = {{"CreatePseudoConsole", (FARPROC*) &g_create_pseudo_console},
                      {"ResizePseudoConsole", (FARPROC*) &g_resize_pseudo_console},
                      {"ClosePseudoConsole", (FARPROC*) &g_close_pseudo_console},
                      {NULL, NULL}};

    for (int i = 0; conpty_fns[i].name != NULL && conpty_fns[i].ptr != NULL; i++) {
        FARPROC ptr = GetProcAddress(kernel, conpty_fns[i].name);
        if (ptr == NULL) {
            FreeLibrary(kernel);
            g_conpty_api_inited = -1;
            return -1;
        }
        *conpty_fns[i].ptr = ptr;
    }

    g_conpty_api_inited = 1;
    return 0;
}

static int conpty_init(conpty_t* pty) {
    HRESULT hr;
    SECURITY_ATTRIBUTES sa = {0};
    DWORD mode = PIPE_ACCESS_INBOUND | PIPE_ACCESS_OUTBOUND | FILE_FLAG_FIRST_PIPE_INSTANCE;
    HANDLE in_read = INVALID_HANDLE_VALUE;
    HANDLE out_write = INVALID_HANDLE_VALUE;
    wchar_t namedpipe_in_name[64];
    wchar_t namedpipe_out_name[64];
    int err_code = 0;

    sa.nLength = sizeof(sa);

    swprintf(namedpipe_in_name, sizeof(namedpipe_in_name), L"\\\\.\\pipe\\conpty-proxy-in-%s",
             pty->id);

    if ((in_read = CreateNamedPipeW(namedpipe_in_name, mode,
                                    PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT, 1, 0, 0, 30000,
                                    &sa)) == INVALID_HANDLE_VALUE) {
        err_code = ERROR_CREATE_IN_PIPE_FAILED;
        goto err;
    }

    swprintf(namedpipe_out_name, sizeof(namedpipe_out_name), L"\\\\.\\pipe\\conpty-proxy-out-%s",
             pty->id);

    if ((out_write = CreateNamedPipeW(namedpipe_out_name, mode,
                                      PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT, 1, 0, 0,
                                      30000, &sa)) == INVALID_HANDLE_VALUE) {
        err_code = ERROR_CREATE_OUT_PIPE_FAILED;
        goto err;
    }

    COORD size = {(SHORT) pty->width, (SHORT) pty->height};
    hr = g_create_pseudo_console(size, in_read, out_write, 0, &pty->hpc);
    if (FAILED(hr)) {
        err_code = ERROR_CREATE_PSEUDO_CONSOLE_FAILED;
        goto err;
    }

    pty->io_read = CreateFileW(namedpipe_out_name, GENERIC_READ | GENERIC_WRITE, 0, NULL,
                               OPEN_EXISTING, FILE_FLAG_OVERLAPPED, NULL);
    if (pty->io_read == INVALID_HANDLE_VALUE) {
        err_code = ERROR_OPEN_IN_PIPE_FAILED;
        goto err;
    }

    pty->io_write = CreateFileW(namedpipe_in_name, GENERIC_WRITE | GENERIC_READ, 0, NULL,
                                OPEN_EXISTING, 0, NULL);
    if (pty->io_write == INVALID_HANDLE_VALUE) {
        err_code = ERROR_OPEN_OUT_PIPE_FAILED;
        goto err;
    }

    CloseHandle(in_read);
    CloseHandle(out_write);
    return 0;
err:
    if (in_read != INVALID_HANDLE_VALUE) {
        CloseHandle(in_read);
    }

    if (out_write != INVALID_HANDLE_VALUE) {
        CloseHandle(out_write);
    }
    if (pty->io_read != INVALID_HANDLE_VALUE) {
        CloseHandle(pty->io_read);
    }

    if (pty->io_write != INVALID_HANDLE_VALUE) {
        CloseHandle(pty->io_write);
    }

    return err_code;
}

static int conpty_spawn(conpty_t* pty) {
    PROCESS_INFORMATION pi = {0};
    size_t attrListSize = 0;
    int err_code = 0;

    pty->si.StartupInfo.cb = sizeof(pty->si);
    pty->si.StartupInfo.dwFlags |= STARTF_USESTDHANDLES;
    pty->si.StartupInfo.hStdInput = NULL;
    pty->si.StartupInfo.hStdOutput = NULL;
    pty->si.StartupInfo.hStdError = NULL;

    InitializeProcThreadAttributeList(NULL, 1, 0, &attrListSize);
    pty->si.lpAttributeList = malloc(attrListSize);
    if (pty->si.lpAttributeList == NULL) {
        return ERROR_MALLOC_PROC_ATTR_FAILED;
    }

    if (!InitializeProcThreadAttributeList(pty->si.lpAttributeList, 1, 0, &attrListSize)) {
        err_code = ERROR_INIT_PROC_ATTR_FAILED;
        goto err;
    }

    if (!UpdateProcThreadAttribute(pty->si.lpAttributeList, 0, PROC_THREAD_ATTRIBUTE_PSEUDOCONSOLE,
                                   pty->hpc, sizeof(pty->hpc), NULL, NULL)) {
        err_code = ERROR_UPDATE_PROC_ATTR_FAILED;
        goto err;
    }

    if (!CreateProcessW(NULL, pty->cmd, NULL, NULL, FALSE,
                        EXTENDED_STARTUPINFO_PRESENT | CREATE_UNICODE_ENVIRONMENT, NULL, NULL,
                        &pty->si.StartupInfo, &pi)) {
        err_code = ERROR_CREATE_PROC_FAILED;
        goto err;
    }

    pty->process = pi.hProcess;
    CloseHandle(pi.hThread);

    return 0;
err:
    free(pty->si.lpAttributeList);
    return err_code;
}

static void async_io_read(conpty_t* pty) {
    memset(&pty->io_overl, 0, sizeof(OVERLAPPED));
    ReadFile(pty->io_read, pty->io_buf, sizeof(pty->io_buf) - 1, NULL, &pty->io_overl);
}

static void async_ctrl_accept(conpty_t* pty) {
    DisconnectNamedPipe(pty->ctrl_pipe);
    memset(&pty->ctrl_overl, 0, sizeof(OVERLAPPED));
    ConnectNamedPipe(pty->ctrl_pipe, &pty->ctrl_overl);
}

static void on_ctrl_accept(conpty_t* pty) {
    int width = 0;
    int height = 0;
    char buf[64];
    DWORD readed;
    ReadFile(pty->ctrl_pipe, buf, sizeof(buf), &readed, NULL);
    if (readed <= 0) {
        return;
    }

    sscanf_s(buf, "%d %d", &width, &height);

    if (width <= 0 || height <= 0) {
        return;
    }
    if (pty->width == width && pty->height == height) {
        return;
    }

    pty->width = width;
    pty->height = height;
    COORD size = {(SHORT) width, (SHORT) height};

    g_resize_pseudo_console(pty->hpc, size);
}

static DWORD WINAPI iocp_entry(LPVOID param) {

    conpty_t* pty = param;
    unsigned long bytes_readed;
    unsigned long long comp_key;
    OVERLAPPED* ovl;
    HANDLE iocp = pty->iocp;
    DWORD writted;

    while (1) {
        if (!GetQueuedCompletionStatus(iocp, &bytes_readed, &comp_key, &ovl, INFINITE)) {
            return -1;
        }
        if (comp_key == COMPLETION_KEY_IO_READ) {
            WriteFile(pty->std_out, pty->io_buf, bytes_readed, &writted, NULL);
            async_io_read(pty);
        } else if (comp_key == COMPLETION_KEY_CTRL_ACCEPT) {
            on_ctrl_accept(pty);
            async_ctrl_accept(pty);
        }
    }

    return 0;
}

static int stdio_run(conpty_t* pty) {
    DWORD readed = 0;
    DWORD writted = 0;
    while (1) {
        if (!ReadFile(GetStdHandle(STD_INPUT_HANDLE), pty->std_buf, sizeof(pty->std_buf), &readed, NULL)) {
            return -1;
        }
        if (!WriteFile(pty->io_write, pty->std_buf, readed, &writted, NULL)) {
            return -1;
        }
    }
}

static void usage(void) {
    wprintf(L"\u263a Usage: \n");
    wprintf(L"\tconpty_proxy.exe new id width height cmd\n");
    wprintf(L"\tconpty_proxy.exe resize id width height\n");
}

static void setup_console(void) {
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);
    setlocale(LC_ALL, ".UTF-8");

    HANDLE in = GetStdHandle(STD_INPUT_HANDLE);
    DWORD mode;

    GetConsoleMode(in, &mode);
    mode &= ~(ENABLE_LINE_INPUT | ENABLE_ECHO_INPUT);
    mode |= ENABLE_VIRTUAL_TERMINAL_INPUT;
    SetConsoleMode(in, mode);
}

static int conpty_new(int argc, wchar_t* argv[]) {
    wchar_t ctrl_pipename[64];

    if (argc < 6) {
        usage();
        return ERROR_INVALID_ARGC;
    }

    g_pty.id = argv[2];

    swprintf(ctrl_pipename, sizeof(ctrl_pipename), L"\\\\.\\pipe\\conpty-proxy-ctrl-%s", g_pty.id);
    HANDLE ctrl_pipe = CreateFileW(ctrl_pipename, GENERIC_READ | GENERIC_WRITE, 0, NULL,
                                   OPEN_EXISTING, 0, NULL);
    if (ctrl_pipe != INVALID_HANDLE_VALUE) {
        CloseHandle(ctrl_pipe);
        return ERROR_ID_DUP;
    }

    SECURITY_ATTRIBUTES sa = {0};
    sa.nLength = sizeof(sa);
    g_pty.ctrl_pipe = CreateNamedPipeW(ctrl_pipename,
                                       PIPE_ACCESS_DUPLEX | FILE_FLAG_OVERLAPPED,
                                       PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT, 1, 0, 0,
                                       30000, &sa);
    if (g_pty.ctrl_pipe == INVALID_HANDLE_VALUE) {
        return ERROR_CREATE_CTRL_PIPE_FAILED;
    }

    if (conpty_api_init() != 0) {
        return ERROR_CONPTY_API_INIT_FAILED;
    }

    g_pty.iocp = CreateIoCompletionPort(INVALID_HANDLE_VALUE, NULL, 0, 0);
    if (g_pty.iocp == NULL) {
        return ERROR_CREATE_MAIN_IOCP_FAILED;
    }

    int width = _wtoi(argv[3]);
    int height = _wtoi(argv[4]);
    if (width <= 0 || height <= 0) {
        return ERROR_INVALID_SIZE;
    }
    g_pty.width = width;
    g_pty.height = height;

    int ret = conpty_init(&g_pty);
    if (ret != 0) {
        return ret;
    }

    g_pty.cmd = argv[5];
    ret = conpty_spawn(&g_pty);
    if (ret != 0) {
        return ret;
    }

    if (CreateIoCompletionPort(g_pty.io_read, g_pty.iocp, COMPLETION_KEY_IO_READ, 1) == NULL) {
        return ERROR_CREATE_IO_READ_IOCP_FAILED;
    }

    if (CreateIoCompletionPort(g_pty.ctrl_pipe, g_pty.iocp, COMPLETION_KEY_CTRL_ACCEPT, 1) == NULL) {
        return ERROR_CREATE_CTRL_READ_IOCP_FAILED;
    }

    HANDLE thread = CreateThread(NULL, 0, iocp_entry, &g_pty, 0, NULL);
    if (thread == INVALID_HANDLE_VALUE) {
        return -1;
    }
    g_pty.iocp_thread = thread;

    g_pty.std_in = GetStdHandle(STD_INPUT_HANDLE);
    g_pty.std_out = GetStdHandle(STD_OUTPUT_HANDLE);

    async_io_read(&g_pty);

    async_ctrl_accept(&g_pty);

    stdio_run(&g_pty);

    return 0;
}

static int conpty_resize(int argc, wchar_t* argv[]) {
    wchar_t ctrl_pipename[64];

    if (argc < 5) {
        usage();
        return ERROR_INVALID_ARGC;
    }

    g_pty.id = argv[2];

    swprintf(ctrl_pipename, sizeof(ctrl_pipename), L"\\\\.\\pipe\\conpty-proxy-ctrl-%s", g_pty.id);

    HANDLE ctrl_pipe = CreateFileW(ctrl_pipename, GENERIC_READ | GENERIC_WRITE, 0, NULL,
                                   OPEN_EXISTING, 0, NULL);
    if (ctrl_pipe == INVALID_HANDLE_VALUE) {
        return ERROR_RESIZE_ID_INVALID;
    }

    int width = _wtoi(argv[3]);
    int height = _wtoi(argv[4]);
    if (width <= 0 || height <= 0) {
        return ERROR_INVALID_SIZE;
    }

    char msg[1024];
    int ret = snprintf(msg, sizeof(msg), "%d %d", width, height);
    DWORD written = 0;
    printf("write file: %d\n", ret);
    WriteFile(ctrl_pipe, msg, ret, &written, NULL);
    printf("write file: %lu\n", written);
    CloseHandle(ctrl_pipe);
    return 0;
}

int wmain(int argc, wchar_t* argv[]) {
    setup_console();
    if (argc < 2) {
        usage();
        return -1;
    }

    const wchar_t* action = argv[1];
    if (wcscmp(action, L"new") == 0) {
        return conpty_new(argc, argv);
    } else if (wcscmp(action, L"resize") == 0) {
        return conpty_resize(argc, argv);
    } else {
        usage();
        return -1;
    }
    return 0;
}
