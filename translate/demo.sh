#!/usr/bin/env bash

set -euo pipefail

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
prompt_template_path="${script_dir}/prompt_template.txt"

source_lang="English"
source_code="en"
target_lang="Dutch"
target_code="nl"
model="${TRANSLATEGEMMA_MODEL:-translategemma:4b}"
input_path=""
output_path=""
dry_run="false"

usage() {
	cat <<'EOF'
Usage: demo.sh [options]

Options:
	-s, --source-lang NAME   Source language name (default: English)
	-S, --source-code CODE   Source language code (default: en)
	-t, --target-lang NAME   Target language name (default: Dutch)
	-T, --target-code CODE   Target language code (default: nl)
	-m, --model NAME         Ollama model tag (default: translategemma:4b)
	-i, --input FILE         Read text from FILE instead of stdin
	-o, --output FILE        Write translation to FILE instead of stdout
			--dry-run            Print the composed prompt and exit
	-h, --help               Show this help text

Input text is read from stdin when --input is not provided.
EOF
}

while (($# > 0)); do
	case "$1" in
		-s|--source-lang)
			source_lang="${2:?Missing value for $1}"
			shift 2
			;;
		-S|--source-code)
			source_code="${2:?Missing value for $1}"
			shift 2
			;;
		-t|--target-lang)
			target_lang="${2:?Missing value for $1}"
			shift 2
			;;
		-T|--target-code)
			target_code="${2:?Missing value for $1}"
			shift 2
			;;
		-m|--model)
			model="${2:?Missing value for $1}"
			shift 2
			;;
		-i|--input)
			input_path="${2:?Missing value for $1}"
			shift 2
			;;
		-o|--output)
			output_path="${2:?Missing value for $1}"
			shift 2
			;;
		--dry-run)
			dry_run="true"
			shift
			;;
		-h|--help)
			usage
			exit 0
			;;
		*)
			printf 'Unknown option: %s\n\n' "$1" >&2
			usage >&2
			exit 2
			;;
	esac
done

if [[ ! -f "$prompt_template_path" ]]; then
	printf 'Missing prompt template: %s\n' "$prompt_template_path" >&2
	exit 1
fi

if ! command -v ollama >/dev/null 2>&1; then
	printf 'ollama is not installed or not on PATH\n' >&2
	exit 1
fi

if [[ -n "$input_path" ]]; then
	input_text="$(cat "$input_path")"
else
	input_text="$(cat)"
fi

prompt_prefix="$(<"$prompt_template_path")"
prompt_prefix="${prompt_prefix//\{SOURCE_LANG\}/$source_lang}"
prompt_prefix="${prompt_prefix//\{SOURCE_CODE\}/$source_code}"
prompt_prefix="${prompt_prefix//\{TARGET_LANG\}/$target_lang}"
prompt_prefix="${prompt_prefix//\{TARGET_CODE\}/$target_code}"

if [[ "$dry_run" == "true" ]]; then
	printf '%s\n\n%s\n' "$prompt_prefix" "$input_text"
	exit 0
fi

translation_output="$(printf '%s\n\n%s\n' "$prompt_prefix" "$input_text" | ollama run "$model")"

if [[ -n "$output_path" ]]; then
	printf '%s\n' "$translation_output" > "$output_path"
else
	printf '%s\n' "$translation_output"
fi
