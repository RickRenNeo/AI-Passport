#!/usr/bin/env python3
"""Static contracts for the key-wake guard wiring in the AI Passport board."""

from pathlib import Path
import re
import unittest


ROOT = Path(__file__).resolve().parents[1]
BOARD = "main/boards/folotoy/ai-passport/ai_passport_board.cc"


def read(relative_path: str) -> str:
    return (ROOT / relative_path).read_text(encoding="utf-8")


def function_body(source: str, name: str) -> str:
    match = re.search(rf"\b{re.escape(name)}\s*\([^;]*?\)\s*\{{", source)
    if not match:
        raise AssertionError(f"function not found: {name}")
    start = match.end() - 1
    depth = 0
    for index in range(start, len(source)):
        if source[index] == "{":
            depth += 1
        elif source[index] == "}":
            depth -= 1
            if depth == 0:
                return source[start + 1:index]
    raise AssertionError(f"function is unterminated: {name}")


class KeyWakeGuardWiringTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        # The wiring used to live in main/main.c's on_key(); this branch runs the
        # three keys through AiPassportBoard::InitializeButtons instead.
        cls.main = read(BOARD)

    def test_every_key_dispatch_runs_the_guard_first(self) -> None:
        # A press-down that wakes the device is never blocked, so the wake cannot
        # deadlock; every graded action (click and long press, three keys) has to
        # consume the guard before it is allowed to act.
        body = function_body(self._source(), "InitializeButtons")
        for event in ("OnClick", "OnLongPress"):
            pattern = (
                rf"{event}\(\s*\[this\]\(\)\s*\{{\s*"
                r"if \(KeyEventsBlocked\(\)\)\s*return;"
            )
            matches = re.findall(pattern, body)
            self.assertGreaterEqual(
                len(matches),
                3,
                f"expected every {event} handler to start with the guard, found "
                f"{len(matches)}",
            )

    def test_press_down_still_wakes_and_cancels_the_countdown(self) -> None:
        # The wake press must reach the power-save timer even while the guard is
        # armed, otherwise a deep-sleep wake press would be swallowed entirely.
        # OnPressDown is a lambda registered inside InitializeButtons.
        body = function_body(self._source(), "InitializeButtons")
        press_down = re.search(
            r"OnPressDown\(\[this\]\(\)\s*\{(.*?)\}\)\s*;", body, re.S
        )
        self.assertIsNotNone(press_down, "OnPressDown registration not found")
        lambda_body = press_down.group(1)
        self.assertIn("WakeUpPowerSaveTimer()", lambda_body)
        self.assertIn("ArmWakeKeyGuard()", lambda_body)

    def test_guard_is_cleared_when_the_press_sequence_ends(self) -> None:
        body = function_body(self._source(), "EndWakeKeyGuard")
        self.assertIn("wake_key_guard_ = false", body)

    def _source(self) -> str:
        return self.main


if __name__ == "__main__":
    unittest.main()
