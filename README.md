# Chess Controller

This package contains a ROS2 node that interfaces with a UCI-compatible chess engine to play chess.
It contains an action server that accepts a board state and clock time, and returns a move.

The action server is defined in `chess_controller/action/Chess.action`. It accepts a
`chess_msgs/ChessBoardFEN` and a `chess_msgs/ChessClock` and returns a `chess_msgs/ChessMoveUCI`.
Feedback is published as `chess_msgs/EngineInfo` while the engine is thinking. If a new goal is
received, the action server will preempt the current goal and return the best move found so far.
