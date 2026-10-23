# D4 -- the Python counterpart of showcase/d4-mcts/search.cml.
#
# The same Monte-Carlo tree search as the Camel program: the same light playout policy, the same
# counter-based generator, the same UCT and the same playout budget, so both build the same tree
# and report the same numbers.  compare.py diffs their output line for line, which is why the
# arithmetic below is written in the same order as the Camel program's: these are IEEE-754
# doubles, and the same additions in another order are another tree.
#
# The shapes differ, and that is what the pair is for.  The Camel tree is made of values: a
# search step returns a new tree, and the updated child is written back through a path.  This
# tree is made of objects the search mutates where they stand.  The search is the same search.
#
# Run:
#   python showcase/d4-mcts/baseline/mcts.py --seed 42 --playouts 20000 --position .........

from __future__ import annotations

import argparse
import math
import sys

# == game ==
# A position is the nine places -- empty 0, X 1, O 2 -- and the player to move.  Whose turn it is
# follows from the two counts, so one string on the command line is a whole position.
LINES = (
    (0, 1, 2), (3, 4, 5), (6, 7, 8),
    (0, 3, 6), (1, 4, 7), (2, 5, 8),
    (0, 4, 8), (2, 4, 6),
)


def winner(board: list[int]) -> int:
    """The player whose line is complete, or 0 when neither has one."""
    for line in LINES:
        mark = board[line[0]]
        if mark != 0 and mark == board[line[1]] and mark == board[line[2]]:
            return mark
    return 0


def full(board: list[int]) -> bool:
    return all(mark != 0 for mark in board)


def empty_places(board: list[int]) -> list[int]:
    return [place for place in range(9) if board[place] == 0]


def moves_of(board: list[int]) -> list[int]:
    """The empty places, or none when the game is over -- a complete line or a full board."""
    return [] if winner(board) != 0 or full(board) else empty_places(board)


def apply_move(board: list[int], to_move: int, place: int) -> tuple[list[int], int]:
    played = list(board)
    played[place] = to_move
    return played, 3 - to_move


def result_for(board: list[int], player: int) -> float:
    """1.0 for a win, 0.5 for a draw, 0.0 for a loss, always from `player`'s point of view."""
    mark = winner(board)
    if mark == player:
        return 1.0
    return 0.5 if mark == 0 else 0.0


# == policy ==
# The light policy both players follow inside a playout: take a win, block the opponent's win,
# and otherwise play a place drawn from the generator.  Lehmer's `x * 48271 mod 2^31 - 1`: its
# next value follows from its value alone, so a subtree's stream of playouts follows from the key
# the subtree carries.
def next_key(key: int) -> int:
    return (key * 48271) % 2147483647


def completing_move(board: list[int], mark: int) -> int:
    """The empty place that completes a line for `mark`, or -1 when there is none."""
    for line in LINES:
        first, second, third = board[line[0]], board[line[1]], board[line[2]]
        if first == mark and second == mark and third == 0:
            return line[2]
        if first == mark and third == mark and second == 0:
            return line[1]
        if second == mark and third == mark and first == 0:
            return line[0]
    return -1


def policy_move(board: list[int], to_move: int, draw: int) -> int:
    win = completing_move(board, to_move)
    block = completing_move(board, 3 - to_move)
    places = empty_places(board)
    if win >= 0:
        return win
    if block >= 0:
        return block
    return places[draw % len(places)]


def playout(board: list[int], to_move: int, key: int, player: int) -> float:
    """One game played to its end with the light policy, from `player`'s point of view."""
    draw = key
    while winner(board) == 0 and not full(board):
        place = policy_move(board, to_move, draw)
        board, to_move = apply_move(board, to_move, place)
        draw = next_key(draw)
    return result_for(board, player)


# == tree ==
# A search node.  `key` seeds every playout below it (a child's key comes from its parent's, so a
# node's stream follows the path that reached it); `moves` is every move the position offers, and
# `children` holds the moves already expanded, in the order of `moves`.  `score` is the sum of the
# results of the playouts through this node, from the root player's point of view.
EXPLORATION = 1.4142135623730951


class Node:
    __slots__ = ("key", "board", "to_move", "place", "visits", "score", "moves", "children")

    def __init__(self, key: int, board: list[int], to_move: int, place: int) -> None:
        self.key = key
        self.board = board
        self.to_move = to_move
        self.place = place
        self.visits = 0
        self.score = 0.0
        self.moves = moves_of(board)
        self.children: list[Node] = []


def open_node(board: list[int], to_move: int, place: int, key: int) -> Node:
    return Node(next_key(key), board, to_move, place)


def node_count(node: Node) -> int:
    """The number of nodes in the tree, the root included."""
    return 1 + sum(node_count(child) for child in node.children)


# == search ==
def score_of(node: Node, index: int, player: int) -> float:
    """The UCT score of child `index`: its mean seen from the player who moves at `node`, plus
    the exploration term."""
    child = node.children[index]
    sign = 1.0 if node.to_move == player else -1.0
    return (sign * child.score / child.visits
            + EXPLORATION * math.sqrt(math.log(node.visits) / child.visits))


def best_child(node: Node, player: int) -> int:
    """The highest-scoring child; ties go to the earliest, so nothing but the tree decides."""
    chosen = 0
    for index in range(1, len(node.children)):
        if score_of(node, index, player) > score_of(node, chosen, player):
            chosen = index
    return chosen


def visit(node: Node, player: int) -> float:
    """One playout below `node`, folded into every node it touched, and its result."""
    if not node.moves:
        played = result_for(node.board, player)
    elif len(node.children) < len(node.moves):
        place = node.moves[len(node.children)]
        child_board, child_to_move = apply_move(node.board, node.to_move, place)
        child = open_node(child_board, child_to_move, place, node.key)
        played = playout(child_board, child_to_move, child.key, player)
        child.visits += 1
        child.score += played
        node.children.append(child)
    else:
        played = visit(node.children[best_child(node, player)], player)
    node.visits += 1
    node.score += played
    return played


def search(root: Node, playouts: int, player: int) -> Node:
    for _ in range(playouts):
        visit(root, player)
    return root


def best_move(node: Node) -> int:
    """The child whose mean result is highest; ties go to the earliest move."""
    chosen = 0
    for index in range(1, len(node.children)):
        here, best = node.children[index], node.children[chosen]
        if here.score / here.visits > best.score / best.visits:
            chosen = index
    return node.moves[chosen]


# == entry ==
def mark_of(character: str) -> int:
    return 0 if character == "." else (1 if character == "X" else 2)


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description="A Monte-Carlo tree search over tic-tac-toe.")
    parser.add_argument("--seed", type=int, required=True)
    parser.add_argument("--playouts", type=int, required=True)
    parser.add_argument("--position", required=True)
    arguments = parser.parse_args(argv)

    board = [mark_of(character) for character in arguments.position]
    x_marks = board.count(1)
    o_marks = board.count(2)
    player = 1 if x_marks == o_marks else 2
    print(f"d4 config seed={arguments.seed} playouts={arguments.playouts} "
          f"position={arguments.position} to_move={player}")

    # X moves first, so a position is reachable only when X has as many marks as O or one more.
    if x_marks != o_marks and x_marks != o_marks + 1:
        print(f"d4 invalid position={arguments.position}: "
              f"X has {x_marks} marks and O has {o_marks}")
        return 1

    # A position with a complete line is over: there is no move to search for.
    if winner(board) != 0:
        print(f"d4 over position={arguments.position}: "
              f"{'X' if winner(board) == 1 else 'O'} has won")
        return 1

    # The root carries the seed itself, not `next_key(seed)`: a child's key is derived from its
    # parent's, so the root is the one node whose key is the caller's.
    tree = search(Node(arguments.seed, board, player, -1), arguments.playouts, player)
    if not tree.children:
        print(f"d4 no search position={arguments.position} playouts={arguments.playouts} "
              f"root_visits={tree.visits}")
        return 1

    print(f"d4 search root_visits={tree.visits} nodes={node_count(tree)} "
          f"children={len(tree.children)} best={best_move(tree)}")
    for child in tree.children:
        mean = child.score / child.visits
        print(f"d4 child place={child.place} visits={child.visits} "
              f"score={child.score:.6f} mean={mean:.6f}")
    return 0


if __name__ == "__main__":
    sys.exit(main())