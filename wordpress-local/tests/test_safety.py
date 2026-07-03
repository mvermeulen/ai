import io

import pytest

from wpsync import safety
from wpsync.config import Config


def test_check_host_pins_on_first_run(project: Config):
    safety.check_host(project)
    path = safety._pinned_host_path(project)
    assert path.exists()
    assert "example.test" in path.read_text(encoding="utf-8")


def test_check_host_allows_unchanged_host(project: Config):
    safety.check_host(project)
    safety.check_host(project)  # no error


def test_check_host_blocks_changed_host_without_confirmation(project: Config):
    safety.check_host(project)
    project.base_url = "https://a-completely-different-site.example/blog"
    with pytest.raises(safety.SafetyError):
        safety.check_host(project)


def test_check_host_allows_changed_host_with_confirmation(project: Config):
    safety.check_host(project)
    project.base_url = "https://a-completely-different-site.example/blog"
    safety.check_host(project, confirm_host_change=True)  # no error
    # and it's now re-pinned
    safety.check_host(project)


def test_check_batch_size_allows_small_batches():
    safety.check_batch_size(5, yes_large_batch=False)


def test_check_batch_size_blocks_large_batches_without_flag():
    with pytest.raises(safety.SafetyError):
        safety.check_batch_size(21, yes_large_batch=False)


def test_check_batch_size_allows_large_batches_with_flag():
    safety.check_batch_size(500, yes_large_batch=True)


def test_check_batch_size_respects_custom_max():
    with pytest.raises(safety.SafetyError):
        safety.check_batch_size(3, yes_large_batch=False, max_batch=2)


class _FakeStream:
    def __init__(self, is_tty: bool, answer: str = ""):
        self._is_tty = is_tty
        self._answer = answer

    def isatty(self):
        return self._is_tty


def test_confirm_skips_prompt_when_assume_yes():
    assert safety.confirm("summary", assume_yes=True) is True


def test_confirm_skips_prompt_when_not_a_tty():
    assert safety.confirm("summary", assume_yes=False, stream=_FakeStream(is_tty=False)) is True


def test_confirm_requires_typed_yes_when_interactive(monkeypatch):
    monkeypatch.setattr("builtins.input", lambda prompt="": "yes")
    assert safety.confirm("summary", assume_yes=False, stream=_FakeStream(is_tty=True)) is True


def test_confirm_rejects_wrong_answer_when_interactive(monkeypatch):
    monkeypatch.setattr("builtins.input", lambda prompt="": "nope")
    assert safety.confirm("summary", assume_yes=False, stream=_FakeStream(is_tty=True)) is False
