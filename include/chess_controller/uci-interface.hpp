#ifndef UCI_INTERFACE__CHESS_CONTROLLER__HPP
#define UCI_INTERFACE__CHESS_CONTROLLER__HPP

#include <string>

namespace uci_interface
{

/**
 * A UCI option that can be set.
 */
struct Option {
  /**
   * A type of option that can be set.
   */
  enum class Type {
    CHECK,    ///< A boolean option.
    SPIN,     ///< An integer option.
    COMBO,    ///< A list of string options.
    BUTTON,   ///< A command that can be executed.
    STRING,   ///< A string option.
    UNKNOWN,  ///< An unknown option.
  };

  std::string name = "";           ///< The name of the option.
  Type type = Type::UNKNOWN;       ///< The type of the option.
  std::string default_value = "";  ///< The default value of the option.
  std::string min_value = "";      ///< The minimum value of the option.
  std::string max_value = "";      ///< The maximum value of the option.
  std::string var = "";            ///< A predefined value for a combo option.
};

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