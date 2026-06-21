# TranslateGemma Tooling (`translate`)

A collection of lightweight wrapper utilities and applications for performing local text and file translation using Google's **TranslateGemma** open model via **Ollama**.

TranslateGemma is a specialized variant of Gemma optimized for translation tasks. It is designed to preserve meaning, tone, and format across multiple language pairs.

---

## Prerequisites

1. **Ollama**: Install Ollama from [ollama.com](https://ollama.com). Ensure the Ollama background daemon is running.
2. **Download Model**: Pull the TranslateGemma model tag you want to use. The utilities default to the `4b` parameter version:
   ```bash
   ollama pull translategemma:4b
   ```
   *(Note: You can also use other sizes like `translategemma:12b` or `translategemma:27b` depending on your system hardware capacity.)*
3. **Python 3**: Needed to run the python CLI and web dashboard. No external dependencies are required beyond the standard library.

---

## Project Components

The project includes three separate entry points suited for different environments and workflows:

### 1. Web UI Dashboard (`app.py`)
A lightweight, zero-dependency Python web application serving a premium, responsive interface.
- **Key Features**:
  - Interactive source/target language selection dropdowns.
  - File uploader supporting **multiple files** simultaneously.
  - Drag-and-drop capability to re-order files before translation.
  - Live preview of the uploaded text snippets.
  - **Live Prompt Preview**: Real-time rendering of the exact prompt payload before sending it to the LLM.
- **To Launch**:
  ```bash
  python3 app.py
  ```
  Then open your browser and navigate to: [http://127.0.0.1:8787](http://127.0.0.1:8787)

### 2. Python Command Line Tool (`translate.py`)
A flexible Python CLI wrapper for automation scripts and pipelines.
- **Key Features**:
  - Auto-resolves both full language names (e.g. `English`, `Dutch`) and standard ISO language codes (e.g. `en`, `nl`).
  - Supports reading inputs from files or standard input (`stdin`).
  - Supports writing output directly to files.
  - `--show-prompt` flag to debug or inspect the raw prompt template layout.
  - `--list-languages` option to print all predefined supported languages.
- **Usage Examples**:
  ```bash
  # Translate content from a file to stdout (defaults: en -> nl)
  python3 translate.py --input sample.txt

  # Translate using standard input (stdin) and write to a file
  echo "Translate this text" | python3 translate.py --source en --target de --output output.txt

  # Inspect prompt layout
  python3 translate.py --source English --target Spanish --show-prompt < sample.txt

  # List all predefined language pairings
  python3 translate.py --list-languages
  ```

### 3. Bash Quick Script (`demo.sh`)
A fast terminal-based shell script for simple, low-overhead pipeline translation.
- **Usage Examples**:
  ```bash
  # Quick translate of a string to stdout
  echo "Hello world" | bash demo.sh

  # Dry run to print the composed prompt and template variables
  echo "Hello world" | bash demo.sh --dry-run
  ```

---

## Configuration & prompt engineering

The underlying prompt structure resides in [prompt_template.txt](file:///home/mev/source/ai/translate/prompt_template.txt):
```text
Translate the following text from {SOURCE_LANG} to {TARGET_LANG}.
Use {SOURCE_CODE} as the source language code and {TARGET_CODE} as the target language code.
```

The script replaces the bracketed variables (`{SOURCE_LANG}`, etc.) with your choices, appends the input text block, and feeds it directly into Ollama's execution channel.

---

## File Structure

- `app.py` - Single-file Python web server containing the HTML/CSS/JS frontend assets and API request handlers.
- `translate.py` - CLI parameter parser and entry point.
- `translategemma_tool.py` - Common utility library housing language dictionaries, prompt building, and the subprocess calls to `ollama`.
- `prompt_template.txt` - Prompt layout injected into TranslateGemma.
- `sample.txt` - Example text file for testing.
- `dutch_prompt.txt` - Prompt reference for Dutch translation rules.
