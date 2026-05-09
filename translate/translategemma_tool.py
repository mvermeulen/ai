from __future__ import annotations

from dataclasses import dataclass
from pathlib import Path
import shutil
import subprocess


DEFAULT_MODEL = "translategemma:4b"
DEFAULT_TEMPLATE_PATH = Path(__file__).with_name("prompt_template.txt")

MODEL_OPTIONS = [
    "translategemma:4b",
    "translategemma:12b",
    "translategemma:27b",
]

LANGUAGE_OPTIONS: list[tuple[str, str]] = [
    ("en", "English"),
    ("nl", "Dutch"),
    ("de", "German"),
    ("fr", "French"),
    ("es", "Spanish"),
    ("it", "Italian"),
    ("pt", "Portuguese"),
    ("pt-BR", "Portuguese (Brazil)"),
    ("pt-PT", "Portuguese (Portugal)"),
    ("pl", "Polish"),
    ("sv", "Swedish"),
    ("da", "Danish"),
    ("no", "Norwegian"),
    ("fi", "Finnish"),
    ("cs", "Czech"),
    ("sk", "Slovak"),
    ("sl", "Slovenian"),
    ("ro", "Romanian"),
    ("hu", "Hungarian"),
    ("el", "Greek"),
    ("tr", "Turkish"),
    ("ru", "Russian"),
    ("uk", "Ukrainian"),
    ("ar", "Arabic"),
    ("he", "Hebrew"),
    ("hi", "Hindi"),
    ("bn", "Bengali"),
    ("ur", "Urdu"),
    ("ja", "Japanese"),
    ("ko", "Korean"),
    ("zh-Hans", "Chinese (Simplified)"),
    ("zh-Hant", "Chinese (Traditional)"),
    ("vi", "Vietnamese"),
]

LANGUAGE_BY_CODE = {code.lower(): (code, name) for code, name in LANGUAGE_OPTIONS}
LANGUAGE_BY_NAME = {name.lower(): (code, name) for code, name in LANGUAGE_OPTIONS}


def resolve_language_token(value: str, default_code: str | None = None) -> tuple[str, str]:
    token = value.strip()
    if not token:
        if default_code is None:
            raise ValueError("language token cannot be empty")
        by_default_code = LANGUAGE_BY_CODE.get(default_code.lower())
        if by_default_code is None:
            raise ValueError(f"unknown default language code: {default_code}")
        return by_default_code[1], by_default_code[0]

    by_name = LANGUAGE_BY_NAME.get(token.lower())
    if by_name is not None:
        return by_name[1], by_name[0]

    by_code = LANGUAGE_BY_CODE.get(token.lower())
    if by_code is not None:
        return by_code[1], by_code[0]

    raise ValueError(f"unknown language token: {value}")


def format_language_choices(limit: int | None = None) -> str:
    choices = LANGUAGE_OPTIONS if limit is None else LANGUAGE_OPTIONS[:limit]
    return "\n".join(f"{code:<10} {name}" for code, name in choices)


@dataclass(frozen=True)
class TranslationRequest:
    source_lang: str
    source_code: str
    target_lang: str
    target_code: str
    text: str
    model: str = DEFAULT_MODEL
    template_path: Path = DEFAULT_TEMPLATE_PATH


def load_template(template_path: Path) -> str:
    return template_path.read_text(encoding="utf-8").strip()


def build_prompt(request: TranslationRequest) -> str:
    template = load_template(request.template_path)
    prompt = template.replace("{SOURCE_LANG}", request.source_lang)
    prompt = prompt.replace("{SOURCE_CODE}", request.source_code)
    prompt = prompt.replace("{TARGET_LANG}", request.target_lang)
    prompt = prompt.replace("{TARGET_CODE}", request.target_code)
    return f"{prompt}\n\n{request.text.strip()}"


def translate(request: TranslationRequest) -> str:
    if shutil.which("ollama") is None:
        raise RuntimeError("ollama is not installed or not on PATH")

    prompt = build_prompt(request)
    completed = subprocess.run(
        ["ollama", "run", request.model],
        input=prompt,
        text=True,
        capture_output=True,
        check=False,
    )

    if completed.returncode != 0:
        error_output = completed.stderr.strip() or completed.stdout.strip() or "translation failed"
        raise RuntimeError(error_output)

    return completed.stdout.rstrip()
