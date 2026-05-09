#!/usr/bin/env python3

from __future__ import annotations

import argparse
from pathlib import Path
import sys

from translategemma_tool import (
    DEFAULT_MODEL,
    TranslationRequest,
    build_prompt,
    format_language_choices,
    resolve_language_token,
    translate,
)


HELP_EPILOG = f"""Examples:
  python translate.py --source en --target nl < sample.txt
  python translate.py --source English --target Dutch --input sample.txt

Common languages:
{format_language_choices(limit=12)}

Use --list-languages to print the full set of supported language pairs."""


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Translate text with TranslateGemma via Ollama",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog=HELP_EPILOG,
    )
    parser.add_argument("-s", "--source", default="en", help="Source language code or name")
    parser.add_argument("-t", "--target", default="nl", help="Target language code or name")
    parser.add_argument("-m", "--model", default=DEFAULT_MODEL, help="Ollama model tag")
    parser.add_argument("-i", "--input", type=Path, help="Read text from a file instead of stdin")
    parser.add_argument("-o", "--output", type=Path, help="Write translation to a file")
    parser.add_argument("--show-prompt", action="store_true", help="Print the composed prompt and exit")
    parser.add_argument("--list-languages", action="store_true", help="Print supported language pairs and exit")
    return parser.parse_args()


def read_input_text(path: Path | None) -> str:
    if path is not None:
        return path.read_text(encoding="utf-8")
    return sys.stdin.read()


def main() -> int:
    args = parse_args()

    if args.list_languages:
        print(format_language_choices())
        return 0

    source_lang, source_code = resolve_language_token(args.source, default_code="en")
    target_lang, target_code = resolve_language_token(args.target, default_code="nl")
    input_text = read_input_text(args.input)

    request = TranslationRequest(
        source_lang=source_lang,
        source_code=source_code,
        target_lang=target_lang,
        target_code=target_code,
        text=input_text,
        model=args.model,
    )

    if args.show_prompt:
        print(build_prompt(request))
        return 0

    try:
        translated_text = translate(request)
    except RuntimeError as error:
        print(str(error), file=sys.stderr)
        return 1

    if args.output is not None:
        args.output.write_text(f"{translated_text}\n", encoding="utf-8")
    else:
        print(translated_text)

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
