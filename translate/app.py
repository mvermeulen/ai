#!/usr/bin/env python3

from __future__ import annotations

from email import policy
from email.parser import BytesParser
from html import escape
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from urllib.parse import parse_qs
import sys

from translategemma_tool import DEFAULT_MODEL, LANGUAGE_OPTIONS, MODEL_OPTIONS, TranslationRequest, build_prompt, resolve_language_token, translate


HOST = "127.0.0.1"
PORT = 8787


def form_value(form: dict[str, list[str]], key: str, default: str) -> str:
    values = form.get(key)
    if not values:
        return default
    return values[0]


def render_language_options(selected_code: str) -> str:
    return "\n".join(
        f'<option value="{escape(code)}"{" selected" if code == selected_code else ""}>{escape(name)} ({escape(code)})</option>'
        for code, name in LANGUAGE_OPTIONS
    )


def render_model_options(selected_model: str) -> str:
    return "\n".join(
        f'<option value="{escape(model)}"{" selected" if model == selected_model else ""}>{escape(model)}</option>'
        for model in MODEL_OPTIONS
    )


def parse_multipart_form(content_type: str, body: bytes) -> tuple[dict[str, list[str]], list[dict[str, str]]]:
    message = BytesParser(policy=policy.default).parsebytes(
        f"Content-Type: {content_type}\r\nMIME-Version: 1.0\r\n\r\n".encode("utf-8") + body
    )

    fields: dict[str, list[str]] = {}
    uploads: list[dict[str, str]] = []

    for part in message.iter_parts():
        if part.get_content_disposition() != "form-data":
            continue

        name = part.get_param("name", header="content-disposition")
        if not name:
            continue

        filename = part.get_filename()
        if filename:
            content_bytes = part.get_payload(decode=True) or b""
            uploads.append(
                {
                    "name": name,
                    "filename": filename,
                    "size": str(len(content_bytes)),
                    "text": content_bytes.decode("utf-8", errors="replace"),
                }
            )
        else:
            payload = part.get_content()
            fields.setdefault(name, []).append(payload if isinstance(payload, str) else str(payload))

    return fields, uploads


def combine_translation_text(base_text: str, uploads: list[dict[str, str]]) -> str:
  pieces: list[str] = []
  if base_text.strip():
    pieces.append(base_text.strip())
  for upload in uploads:
    file_text = upload["text"].strip()
    if file_text:
      pieces.append(f"File: {upload['filename']}\n{file_text}")
  return "\n\n".join(pieces)


def render_page(state: dict[str, str]) -> str:
    source_lang = escape(state.get("source_lang", "English"))
    source_code = escape(state.get("source_code", "en"))
    target_lang = escape(state.get("target_lang", "Dutch"))
    target_code = escape(state.get("target_code", "nl"))
    source_value = escape(state.get("source_value", source_code))
    target_value = escape(state.get("target_value", target_code))
    model = escape(state.get("model", DEFAULT_MODEL))
    text = escape(state.get("text", ""))
    result = escape(state.get("result", ""))
    error = escape(state.get("error", ""))
    prompt_preview = escape(state.get("prompt_preview", ""))
    upload_cards = state.get("upload_cards", "")

    return f"""<!doctype html>
<html lang="en">
<head>
  <meta charset="utf-8">
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <title>TranslateGemma Studio</title>
  <style>
    :root {{
      color-scheme: light;
      --bg: #f4efe6;
      --bg-accent: #e2d2bb;
      --panel: rgba(255, 251, 245, 0.88);
      --panel-strong: #fffaf2;
      --ink: #1f1a17;
      --muted: #6b5f56;
      --line: rgba(56, 42, 31, 0.14);
      --accent: #a5482a;
      --accent-deep: #7a321c;
      --success: #2d6b45;
      --danger: #9b2c2c;
      --shadow: 0 24px 80px rgba(64, 39, 24, 0.18);
    }}
    * {{ box-sizing: border-box; }}
    body {{
      margin: 0;
      min-height: 100vh;
      font-family: Georgia, "Times New Roman", serif;
      color: var(--ink);
      background:
        radial-gradient(circle at top left, rgba(255,255,255,0.75), transparent 28%),
        radial-gradient(circle at 80% 15%, rgba(165,72,42,0.14), transparent 25%),
        linear-gradient(135deg, var(--bg), var(--bg-accent));
    }}
    .shell {{
      width: min(1200px, calc(100vw - 32px));
      margin: 0 auto;
      padding: 28px 0 40px;
    }}
    .hero {{
      display: grid;
      grid-template-columns: 1.1fr .9fr;
      gap: 20px;
      align-items: end;
      margin-bottom: 20px;
      animation: rise .5s ease both;
    }}
    .eyebrow {{
      text-transform: uppercase;
      letter-spacing: .18em;
      font-size: .74rem;
      color: var(--accent-deep);
      margin-bottom: 10px;
    }}
    h1 {{
      margin: 0;
      font-size: clamp(2.3rem, 4vw, 4.6rem);
      line-height: .95;
      letter-spacing: -0.04em;
    }}
    .lede {{
      margin: 16px 0 0;
      max-width: 60ch;
      font-size: 1.02rem;
      line-height: 1.6;
      color: var(--muted);
    }}
    .hero-card, .panel {{
      background: var(--panel);
      backdrop-filter: blur(16px);
      border: 1px solid var(--line);
      box-shadow: var(--shadow);
    }}
    .hero-card {{
      padding: 20px 22px;
      border-radius: 24px;
    }}
    .hero-card strong {{ display: block; font-size: 1.05rem; margin-bottom: 8px; }}
    .hero-card p {{ margin: 0; color: var(--muted); line-height: 1.55; }}
    .grid {{
      display: grid;
      grid-template-columns: 1fr 1fr;
      gap: 16px;
    }}
    .panel {{
      border-radius: 28px;
      overflow: hidden;
    }}
    .panel-header {{
      display: flex;
      justify-content: space-between;
      align-items: center;
      gap: 12px;
      padding: 18px 20px;
      border-bottom: 1px solid var(--line);
      background: rgba(255,255,255,.55);
    }}
    .panel-header h2 {{ margin: 0; font-size: 1.1rem; }}
    .panel-header span {{ color: var(--muted); font-size: .92rem; }}
    form {{ padding: 20px; display: grid; gap: 16px; }}
    label {{ display: grid; gap: 8px; font-size: .92rem; color: var(--muted); }}
    .inline {{ display: grid; grid-template-columns: repeat(2, minmax(0, 1fr)); gap: 12px; }}
    input, textarea, select, button {{ font: inherit; }}
    input, textarea, select {{
      width: 100%;
      border-radius: 16px;
      border: 1px solid rgba(121, 92, 68, 0.2);
      background: var(--panel-strong);
      color: var(--ink);
      padding: 14px 14px;
      outline: none;
    }}
    input:focus, textarea:focus, select:focus {{ border-color: rgba(165,72,42,.7); box-shadow: 0 0 0 4px rgba(165,72,42,.12); }}
    textarea {{ min-height: 320px; resize: vertical; line-height: 1.55; }}
    .actions {{ display: flex; flex-wrap: wrap; gap: 12px; align-items: center; }}
    .uploads {{ display: grid; gap: 10px; }}
    .upload-control {{
      display: flex;
      flex-wrap: wrap;
      gap: 10px;
      align-items: center;
    }}
    .upload-control input[type="file"] {{
      max-width: 100%;
    }}
    .upload-list {{ display: grid; gap: 10px; margin-top: 4px; }}
    .upload-item {{
      display: flex;
      gap: 12px;
      padding: 12px 14px;
      border-radius: 16px;
      background: rgba(255, 250, 242, .9);
      border: 1px solid rgba(121, 92, 68, 0.16);
      align-items: flex-start;
    }}
    .upload-item[data-upload-server="true"] {{ background: rgba(255, 246, 233, .96); }}
    .upload-item[draggable="true"] {{ cursor: grab; }}
    .upload-item.is-dragging {{ opacity: .55; cursor: grabbing; }}
    .upload-item.drag-over {{ outline: 2px dashed rgba(165,72,42,.55); outline-offset: 2px; }}
    .drag-handle {{
      flex: 0 0 auto;
      user-select: none;
      color: var(--muted);
      font-weight: 800;
      font-size: 1.1rem;
      line-height: 1;
      padding-top: 2px;
    }}
    .upload-main {{ flex: 1 1 auto; min-width: 0; }}
    .upload-name {{ display: block; font-size: .95rem; font-weight: 700; word-break: break-word; }}
    .upload-meta {{ color: var(--muted); font-size: .86rem; margin-top: 2px; }}
    .upload-preview {{ margin: 8px 0 0; color: #5d5147; font-size: .88rem; line-height: 1.45; white-space: pre-wrap; word-break: break-word; }}
    .upload-actions {{ display: flex; flex-direction: column; gap: 6px; flex: 0 0 auto; }}
    .upload-action {{
      border: 1px solid rgba(121, 92, 68, 0.18);
      background: rgba(255, 255, 255, 0.72);
      color: var(--ink);
      border-radius: 999px;
      padding: 7px 10px;
      font-size: .8rem;
      font-weight: 700;
      cursor: pointer;
      box-shadow: none;
    }}
    .upload-action:disabled {{ opacity: .35; cursor: not-allowed; }}
    .upload-empty {{ color: var(--muted); font-style: italic; }}
    button {{
      border: 0;
      border-radius: 999px;
      padding: 13px 18px;
      background: linear-gradient(135deg, var(--accent), var(--accent-deep));
      color: white;
      font-weight: 700;
      cursor: pointer;
      box-shadow: 0 12px 32px rgba(122, 50, 28, .26);
    }}
    .hint {{ color: var(--muted); font-size: .9rem; }}
    .result {{ padding: 20px; white-space: pre-wrap; line-height: 1.68; min-height: 320px; background: rgba(255, 252, 247, .96); }}
    .result.empty {{ color: #8a7e73; font-style: italic; }}
    .stack {{ display: grid; gap: 16px; }}
    .status {{ padding: 14px 16px; border-radius: 18px; border: 1px solid var(--line); background: rgba(255,255,255,.6); }}
    .status.error {{ color: var(--danger); }}
    .status.success {{ color: var(--success); }}
    .prompt {{ font-family: ui-monospace, SFMono-Regular, Menlo, Monaco, Consolas, monospace; font-size: .82rem; color: #43352d; white-space: pre-wrap; max-height: 220px; overflow: auto; }}
    @keyframes rise {{ from {{ opacity: 0; transform: translateY(10px); }} to {{ opacity: 1; transform: translateY(0); }} }}
    @media (max-width: 900px) {{
      .hero, .grid, .inline {{ grid-template-columns: 1fr; }}
      .shell {{ width: min(100vw - 20px, 1200px); padding-top: 12px; }}
      textarea, .result {{ min-height: 260px; }}
    }}
  </style>
</head>
<body>
  <main class="shell">
    <section class="hero">
      <div>
        <div class="eyebrow">TranslateGemma Studio</div>
        <h1>Local translation, but designed like a proper instrument.</h1>
        <p class="lede">Paste text, choose the language pair, and send it to your local Ollama TranslateGemma model. The interface keeps the full prompt visible so you can inspect what the model actually receives.</p>
      </div>
      <div class="hero-card">
        <strong>Current setup</strong>
        <p>{source_lang} ({source_code}) to {target_lang} ({target_code}) using {model}. The app runs locally and returns only the translated text.</p>
      </div>
    </section>

    <section class="grid">
      <div class="panel">
        <div class="panel-header">
          <h2>Translate</h2>
          <span>POST to local Ollama</span>
        </div>
        <form method="post" action="/translate" enctype="multipart/form-data">
          <div class="inline">
            <label>Source language
              <select name="source">
                {render_language_options(source_value)}
              </select>
            </label>
            <label>Target language
              <select name="target">
                {render_language_options(target_value)}
              </select>
            </label>
          </div>
          <div class="inline">
            <label>Source preview<input value="{source_lang} ({source_code})" readonly /></label>
            <label>Target preview<input value="{target_lang} ({target_code})" readonly /></label>
          </div>
          <label>Model
              <select name="model">
                {render_model_options(model)}
              </select>
            </label>
          <div class="uploads">
            <label for="uploads-input">Upload files</label>
            <div class="upload-control">
              <input id="uploads-input" type="file" name="uploads" multiple />
              <span class="hint">Preview, remove, and drag to reorder before submitting.</span>
            </div>
          </div>
          <div class="uploads">
            <div class="hint">Selected files are appended to the translation payload in order.</div>
            <div id="uploads-empty" class="upload-empty" style="{'' if upload_cards else 'display:none;'}">No files selected yet.</div>
            <div id="uploads-list" class="upload-list">{upload_cards}</div>
          </div>
          <label>Text to translate<textarea name="text" placeholder="Paste the source text here">{text}</textarea></label>
          <div class="actions">
            <button type="submit">Translate</button>
            <span class="hint">If you change the model, keep it in the translategemma family.</span>
          </div>
        </form>
      </div>

      <div class="stack">
        <div class="panel">
          <div class="panel-header">
            <h2>Output</h2>
            <span>model response</span>
          </div>
          <div class="result {'empty' if not result else ''}">{result or 'Translation output will appear here.'}</div>
        </div>
        <div class="panel">
          <div class="panel-header">
            <h2>Prompt preview</h2>
            <span>exact request body</span>
          </div>
          <div class="result prompt">{prompt_preview or 'Submit a translation to inspect the prompt payload.'}</div>
        </div>
        {f'<div class="status error">{error}</div>' if error else ''}
      </div>
    </section>
  </main>
  <script>
    (() => {{
      const input = document.getElementById('uploads-input');
      const list = document.getElementById('uploads-list');
      const emptyState = document.getElementById('uploads-empty');

      if (!input || !list || !emptyState || typeof DataTransfer === 'undefined') {{
        return;
      }}

      let selectedFiles = Array.from(input.files || []);
      const hasServerItems = list.querySelector('[data-upload-server="true"]') !== null;
      let draggingIndex = -1;

      const formatSize = (bytes) => {{
        if (bytes < 1024) return `${{bytes}} B`;
        const units = ['KB', 'MB', 'GB'];
        let size = bytes / 1024;
        let unitIndex = 0;
        while (size >= 1024 && unitIndex < units.length - 1) {{
          size /= 1024;
          unitIndex += 1;
        }}
        return `${{size.toFixed(size >= 10 ? 0 : 1)}} ${{units[unitIndex]}}`;
      }};

      const syncInput = () => {{
        const transfer = new DataTransfer();
        selectedFiles.forEach((file) => transfer.items.add(file));
        input.files = transfer.files;
      }};

      const reorderFiles = (fromIndex, toIndex) => {{
        if (fromIndex === toIndex || fromIndex < 0 || toIndex < 0) {{
          return;
        }}

        const [movedFile] = selectedFiles.splice(fromIndex, 1);
        selectedFiles.splice(toIndex, 0, movedFile);
        syncInput();
        render();
      }};

      const render = () => {{
        if (!selectedFiles.length && hasServerItems) {{
          return;
        }}

        list.innerHTML = '';
        emptyState.style.display = selectedFiles.length ? 'none' : 'block';

        selectedFiles.forEach((file, index) => {{
          const row = document.createElement('div');
          row.className = 'upload-item';
          row.draggable = true;
          row.dataset.index = String(index);

          const handle = document.createElement('div');
          handle.className = 'drag-handle';
          handle.textContent = '⋮⋮';

          const main = document.createElement('div');
          main.className = 'upload-main';

          const name = document.createElement('strong');
          name.className = 'upload-name';
          name.textContent = file.name;

          const meta = document.createElement('div');
          meta.className = 'upload-meta';
          meta.textContent = `${{formatSize(file.size)}}${{file.type ? ' • ' + file.type : ''}}`;

          const preview = document.createElement('div');
          preview.className = 'upload-preview';
          preview.textContent = 'Preview loading...';

          main.append(name, meta, preview);

          const actions = document.createElement('div');
          actions.className = 'upload-actions';

          const remove = document.createElement('button');
          remove.type = 'button';
          remove.className = 'upload-action';
          remove.textContent = 'Remove';

          remove.addEventListener('click', () => {{
            selectedFiles = selectedFiles.filter((_, currentIndex) => currentIndex !== index);
            syncInput();
            render();
          }});

          row.addEventListener('dragstart', (event) => {{
            draggingIndex = index;
            row.classList.add('is-dragging');
            event.dataTransfer.effectAllowed = 'move';
            event.dataTransfer.setData('text/plain', String(index));
          }});

          row.addEventListener('dragend', () => {{
            draggingIndex = -1;
            row.classList.remove('is-dragging');
            list.querySelectorAll('.drag-over').forEach((element) => element.classList.remove('drag-over'));
          }});

          row.addEventListener('dragover', (event) => {{
            event.preventDefault();
            if (draggingIndex === -1 || draggingIndex === index) {{
              return;
            }}
            row.classList.add('drag-over');
            event.dataTransfer.dropEffect = 'move';
          }});

          row.addEventListener('dragleave', () => {{
            row.classList.remove('drag-over');
          }});

          row.addEventListener('drop', (event) => {{
            event.preventDefault();
            row.classList.remove('drag-over');
            const fromIndex = draggingIndex;
            const toIndex = Number(row.dataset.index);
            draggingIndex = -1;
            reorderFiles(fromIndex, toIndex);
          }});

          actions.append(remove);
          row.append(handle, main, actions);
          list.append(row);

          const shouldPreview = file.type.startsWith('text/') || /\\.(txt|md|json|csv|ya?ml|py|sh|js|ts|html?|css|xml)$/i.test(file.name);
          if (shouldPreview) {{
            file.text().then((content) => {{
              const snippet = content.trim().slice(0, 240);
              preview.textContent = snippet ? snippet : '(empty file)';
            }}).catch(() => {{
              preview.textContent = '(preview unavailable)';
            }});
          }} else {{
            preview.textContent = '(binary or non-text file)';
          }}
        }});
      }};

      input.addEventListener('change', () => {{
        selectedFiles = Array.from(input.files || []);
        syncInput();
        render();
      }});

      render();
    }})();
  </script>
</body>
</html>"""


class TranslationHandler(BaseHTTPRequestHandler):
    def log_message(self, format: str, *args: object) -> None:  # noqa: A003
        return

    def do_GET(self) -> None:
        if self.path not in {"/", "/translate"}:
            self.send_error(404)
            return
        self.respond(render_page({}))

    def do_POST(self) -> None:
        if self.path != "/translate":
            self.send_error(404)
            return

        content_length = int(self.headers.get("Content-Length", "0"))
        payload = self.rfile.read(content_length)
        content_type = self.headers.get("Content-Type", "")

        uploads: list[dict[str, str]] = []
        if content_type.startswith("multipart/form-data"):
            form, uploads = parse_multipart_form(content_type, payload)
        else:
            form = parse_qs(payload.decode("utf-8"), keep_blank_values=True)

        source_value = form_value(form, "source", "en")
        target_value = form_value(form, "target", "nl")
        source_lang, source_code = resolve_language_token(source_value, default_code="en")
        target_lang, target_code = resolve_language_token(target_value, default_code="nl")

        state = {
            "source_lang": source_lang,
            "source_code": source_code,
            "target_lang": target_lang,
            "target_code": target_code,
            "source_value": source_code,
            "target_value": target_code,
            "model": form_value(form, "model", DEFAULT_MODEL),
            "text": form_value(form, "text", ""),
        }

        if uploads:
            upload_cards = []
            for upload in uploads:
                upload_cards.append(
                    f'<div class="upload-item" data-upload-server="true"><div class="upload-main"><strong class="upload-name">{escape(upload["filename"])}</strong><div class="upload-meta">{escape(upload["size"])} bytes</div><div class="upload-preview">queued</div></div></div>'
                )
            state["upload_cards"] = "".join(upload_cards)

        combined_text = combine_translation_text(state["text"], uploads)

        request = TranslationRequest(
            source_lang=state["source_lang"],
            source_code=state["source_code"],
            target_lang=state["target_lang"],
            target_code=state["target_code"],
            text=combined_text,
            model=state["model"],
        )

        state["prompt_preview"] = build_prompt(request)

        try:
            state["result"] = translate(request)
        except RuntimeError as error:
            state["error"] = str(error)

        self.respond(render_page(state))

    def respond(self, body: str) -> None:
        encoded = body.encode("utf-8")
        self.send_response(200)
        self.send_header("Content-Type", "text/html; charset=utf-8")
        self.send_header("Content-Length", str(len(encoded)))
        self.end_headers()
        self.wfile.write(encoded)


def main() -> int:
    server = ThreadingHTTPServer((HOST, PORT), TranslationHandler)
    print(f"TranslateGemma Studio running at http://{HOST}:{PORT}", file=sys.stderr)
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        print("\nShutting down.", file=sys.stderr)
    finally:
        server.server_close()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
