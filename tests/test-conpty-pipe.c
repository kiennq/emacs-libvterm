// Test program to verify conpty-proxy control pipe functionality
// Compile: gcc -o test-conpty-pipe.exe test-conpty-pipe.c
// Usage: test-conpty-pipe.exe <conpty-id> <width> <height>

#include <stdio.h>
#include <string.h>
#include <windows.h>

int main(int argc, char *argv[]) {
  if (argc < 4) {
    printf("Usage: %s <conpty-id> <width> <height>\n", argv[0]);
    printf("Example: %s test-123 100 30\n", argv[0]);
    return 1;
  }

  const char *conpty_id = argv[1];
  int width = atoi(argv[2]);
  int height = atoi(argv[3]);

  if (width <= 0 || height <= 0) {
    printf("Error: Invalid width/height\n");
    return 1;
  }

  // Construct pipe name
  char pipe_name[256];
  snprintf(pipe_name, sizeof(pipe_name), "\\\\.\\pipe\\conpty-proxy-ctrl-%s",
           conpty_id);
  printf("[1] Connecting to pipe: %s\n", pipe_name);

  // Try to open the named pipe
  HANDLE pipe = CreateFileA(pipe_name, GENERIC_WRITE, 0, NULL, OPEN_EXISTING,
                            FILE_ATTRIBUTE_NORMAL, NULL);

  if (pipe == INVALID_HANDLE_VALUE) {
    DWORD error = GetLastError();
    printf("[ERROR] Failed to open pipe. Error code: %lu\n", error);

    switch (error) {
    case ERROR_FILE_NOT_FOUND:
      printf(
          "  -> Pipe does not exist. Is conpty-proxy running with id '%s'?\n",
          conpty_id);
      break;
    case ERROR_PIPE_BUSY:
      printf("  -> Pipe is busy. Another client may be connected.\n");
      break;
    case ERROR_ACCESS_DENIED:
      printf("  -> Access denied. Check permissions.\n");
      break;
    default:
      printf("  -> Unknown error.\n");
      break;
    }
    return 1;
  }

  printf("[2] Successfully connected to pipe!\n");

  // Prepare resize message
  char msg[64];
  int msg_len = snprintf(msg, sizeof(msg), "%d %d", width, height);
  printf("[3] Sending resize message: '%s' (%d bytes)\n", msg, msg_len);

  // Write the message
  DWORD written = 0;
  BOOL success = WriteFile(pipe, msg, msg_len, &written, NULL);

  if (!success) {
    DWORD error = GetLastError();
    printf("[ERROR] WriteFile failed. Error code: %lu\n", error);
    CloseHandle(pipe);
    return 1;
  }

  printf("[4] Successfully wrote %lu bytes\n", written);

  // Flush to ensure data is sent
  if (!FlushFileBuffers(pipe)) {
    DWORD error = GetLastError();
    printf("[WARNING] FlushFileBuffers failed. Error code: %lu\n", error);
  } else {
    printf("[5] Successfully flushed pipe\n");
  }

  // Close the pipe
  CloseHandle(pipe);
  printf("[6] Closed pipe\n");

  printf("\n[SUCCESS] Resize message sent successfully!\n");
  printf("Check conpty-proxy output to verify the resize was processed.\n");

  return 0;
}
