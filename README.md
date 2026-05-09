# ai
AI experiments and apps

## TranslateGemma Tooling

The `translate/` folder now contains three ways to use TranslateGemma through Ollama:

- `translate/demo.sh`: Bash entrypoint for quick terminal translation
- `translate/translate.py`: Python CLI with file, stdin, and output support
- `translate/app.py`: Local web app with a richer interface

Typical usage:

```bash
cd translate
printf 'Hello world\n' | bash demo.sh --dry-run
python translate.py --input sample.txt
python app.py
```
