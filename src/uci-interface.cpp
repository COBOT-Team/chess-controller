#include "chess_controller/uci-interface.hpp"

#include <signal.h>
#include <unistd.h>

#include <chrono>
#include <functional>
#include <stdexcept>
#include <thread>

namespace uci_interface
{

using namespace std;

//                                                                                                //
// ======================================== Static Data ========================================= //
//                                                                                                //

bool _initialized;  ///< Whether the interface has been initialized.

pid_t _engine_pid;  ///< The process ID of the chess engine process.

int _pipe_read;   ///< The file descriptor for the read end of the pipe.
int _pipe_write;  ///< The file descriptor for the write end of the pipe.

std::string _buffer;  ///< A buffer for storing unprocessed messages.

//                                                                                                //
// ===================================== Private Functions ====================================== //
//                                                                                                //

/**
 * Signal handler for `SIGCHLD`. This will be called when the chess engine process terminates.
 *
 * @param[in] signal The signal that was received.
 */
void _signal_handler(int)
{
  _initialized = false;
  close(_pipe_read);
  close(_pipe_write);
  _engine_pid = 0;
  _pipe_read = 0;
  _pipe_write = 0;
}

/**
 * Tries to receive data from the chess engine process. If data is received, it will be appended
 * to the buffer. This function will not process messages from the buffer; use
 * `_try_process_from_buffer()` for that.
 *
 * @return Whether data was received.
 */
bool _try_recv()
{
  static const ssize_t BUFFER_SIZE = 1024;

  if (!_initialized) throw runtime_error("UCI Interface not initialized.");

  // Read from the pipe in chunks of up to 1024 bytes. This should be enough to read any single
  // message at once, but we still need to handle the case where a message is split across multiple
  // chunks.
  char chunk[BUFFER_SIZE];
  ssize_t num_bytes_read = 0;
  ssize_t chunk_size;
  do {
    // Read a chunk from the pipe.
    chunk_size = read(_pipe_read, chunk, BUFFER_SIZE);
    num_bytes_read += chunk_size;

    // Handle errors and EOF.
    switch (num_bytes_read) {
      case -1:
        throw runtime_error("Error reading from engine process: " + to_string(errno));
      case 0:
        return num_bytes_read == 0;
      default:
        _buffer.append(chunk, num_bytes_read);
        break;
    }
  } while (chunk_size == BUFFER_SIZE);  // Stop when we read less than the full chunk size.

  return true;  // If we got here, we read at least one byte. No need to check `num_bytes_read`.
}

/**
 * Processes the first message from the buffer. If a valid message is processed, it will be
 * returned. Otherwise, an empty string will be returned.
 *
 * @return The first message from the buffer, or an empty string if no valid message was found.
 */
std::string _try_process_from_buffer()
{
  if (!_initialized) throw runtime_error("UCI Interface not initialized.");

  // Find the first newline in the buffer. This is potentially brittle if the engine is designed to
  // use Windows-style newlines when running on Linux, but it's good enough for now.
  auto newline_pos = _buffer.find('\n');
  if (newline_pos == string::npos) return "";

  // Extract the first message from the buffer.
  auto message = _buffer.substr(0, newline_pos);
  _buffer.erase(0, newline_pos + 1);

  // TODO: process the message

  return message;
}

/**
 * Send a message to the chess engine process.
 *
 * @param[in] message The message to send.
 */
void _write_to_engine(const std::string& message)
{
  if (!_initialized) throw runtime_error("UCI Interface not initialized.");
  const string& message_with_newline = message.back() == '\n' ? message : (message + '\n');
  size_t to_write = message_with_newline.length();
  const char* write_ptr = message_with_newline.c_str();
  while (to_write > 0) {
    int written = write(_pipe_write, write_ptr, to_write);
    if (written == -1) throw runtime_error("Error writing to engine process: " + to_string(errno));
    to_write -= written;
    write_ptr += written;
  }
}

/**
 * Wait for a message from the chess engine process. If the message is not received before the
 * timeout expires, the engine process will be terminated and an exception will be thrown.
 *
 * A message is considered to match if it begins with `expected`. This means that expecting "id
 * name" will match the message "id name Stockfish 12", but not "id author <...>". We match this
 * way because we generally only care about the first word of a message. If this becomes a problem
 * in the future, regular expressions could be used instead.
 *
 * @param[in] expected The expected message.
 * @param[in] timeout The maximum number of milliseconds to wait for the message before giving up.
 * @throws std::runtime_error if the message is not received before the timeout expires.
 * @return The full message that was received.
 */
string _wait_for_engine(const std::string& expected, uint32_t timeout = 1000)
{
  if (!_initialized) throw runtime_error("UCI Interface not initialized.");

  // Determine the end time for the timeout.
  auto end = chrono::steady_clock::now() + chrono::milliseconds(timeout);

  while (true) {
    // Try to process a message from the buffer.
    auto message = _try_process_from_buffer();
    if (message.rfind(expected, 0) == 0) return message;

    // If we didn't find the expected message, try to receive more data from the engine process.
    if (!_try_recv()) {
      this_thread::sleep_for(chrono::milliseconds(10));  // If we didn't receive any data, wait a
                                                         // bit before trying again.
    }

    // If we've waited longer than the timeout, kill the engine process and throw an exception.
    if (chrono::steady_clock::now() > end) {
      kill(_engine_pid, SIGKILL);
      throw runtime_error("Timeout waiting for engine process.");
    }
  }
}

//                                                                                                //
// ====================================== Public Functions ====================================== //
//                                                                                                //

/**
 * Initialize the UCI Interface. This will start the chess engine process and begin communicating
 * with it via the UCI protocol.
 *
 * @param[in] engine_path The path to the chess engine executable.
 * @param[in] argv The command line arguments to pass to the chess engine process.
 * @throws std::runtime_error If the UCI Interface has already been initialized.
 */
void init(char* const engine_path, char* const argv[])
{
  if (_initialized) throw runtime_error("UCI Interface already initialized.");

  // Create pipes for communication with the chess engine process.
  int pipe_from_engine[2];     // chess engine -> UCI Interface
  int pipe_from_interface[2];  // UCI Interface -> chess engine
  pipe(pipe_from_engine);
  pipe(pipe_from_interface);

  // Register the signal handler for `SIGCHLD`.
  if (signal(SIGCHLD, _signal_handler) == SIG_ERR)
    throw runtime_error("Failed to register signal handler.");

  // Fork the process. This will create two copies of this process with two different values for
  // `engine_pid`. In the parent process, `engine_pid` will contain the PID for the child
  // process. In the child process, it will be `0`.
  int engine_pid = fork();
  if (engine_pid == -1) throw runtime_error("Failed to fork process: " + to_string(errno));

  /*
   * Child Process
   */

  if (engine_pid == 0) {
    // Close the interface ends of the pipes.
    close(pipe_from_engine[0]);
    close(pipe_from_interface[1]);

    // Redirect STDIN and STDOUT to the remaining pipe ends.
    if (dup2(pipe_from_interface[0], STDIN_FILENO) == -1)
      throw runtime_error("Failed to redirect STDIN: " + to_string(errno));
    if (dup2(pipe_from_engine[1], STDOUT_FILENO) == -1)
      throw runtime_error("Failed to redirect STDOUT: " + to_string(errno));

    // Execute the chess engine. This will replace the current process with the chess engine.
    // Control flow will never return to this point.
    execv(engine_path, argv);

    // Unreachable unless `execv` fails.
    throw runtime_error("Failed to execute chess engine: " + to_string(errno));
  }

  /*
   * Parent Process
   */

  _engine_pid = engine_pid;
  _pipe_read = pipe_from_engine[0];
  _pipe_write = pipe_from_interface[1];

  // Close the engine ends of the pipes.
  close(pipe_from_engine[1]);
  close(pipe_from_interface[0]);

  // Initialize the chess engine.
  _write_to_engine("uci");
  _wait_for_engine("uciok");

  _initialized = true;
}

}  // namespace uci_interface