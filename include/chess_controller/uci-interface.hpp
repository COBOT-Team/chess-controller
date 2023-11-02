#ifndef UCI_INTERFACE__CHESS_CONTROLLER__HPP
#define UCI_INTERFACE__CHESS_CONTROLLER__HPP

namespace uci_interface
{

/**
 * Initialize the UCI. This will start the chess engine process and begin communicating
 * with it via the UCI protocol.
 *
 * @param[in] engine_path The path to the chess engine executable.
 * @param[in] argv The command line arguments to pass to the chess engine process.
 * @throws std::runtime_error If the UCI Interface has already been initialized.
 */
void init(char* const engine_path, char* const argv[]);

}  // namespace uci_interface
#endif