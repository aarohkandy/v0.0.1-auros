#!/usr/bin/env python3
"""Serve one synthetic password field and retain only boolean test results."""
import argparse
import hmac
import http.server
import os
import secrets
import threading
import urllib.parse
from pathlib import Path

PAGE = b'''<!doctype html><meta charset="utf-8"><style>html,body{width:100%;height:100%;margin:0}body{position:relative;background:#01dead}input{position:absolute;left:15%;top:calc(50% - 70px);width:70%;height:140px;font-size:48px}input:focus{outline:12px solid #1234ef}</style><input type="password" autocomplete="new-password" aria-label="Synthetic standalone password test"><script>let s=0,u=0,e=0,c=0,sent=false;const i=document.querySelector('input[type=password]');i.addEventListener('focus',()=>fetch('/focused',{method:'POST',body:'focused=1'}));i.addEventListener('compositionstart',()=>s++);i.addEventListener('compositionupdate',()=>u++);i.addEventListener('compositionend',()=>e++);i.addEventListener('beforeinput',x=>{if(x.isComposing||String(x.inputType).includes('Composition'))c++});i.addEventListener('input',x=>{if(x.isComposing||String(x.inputType).includes('Composition'))c++});const snapshot=focusafter=>new URLSearchParams({value:i.value,starts:String(s),updates:String(u),ends:String(e),compositioninputs:String(c),focusbefore:document.activeElement===i?'yes':'no',focusafter});i.addEventListener('keydown',async x=>{if(sent||!x.isTrusted||x.repeat||x.key!=='Enter'||x.isComposing||x.ctrlKey||x.altKey||x.metaKey||x.shiftKey||document.activeElement!==i)return;sent=true;x.preventDefault();x.stopImmediatePropagation();const r=await fetch('/',{method:'POST',body:snapshot('no')});if(!r.ok)return;const token=await r.text();await new Promise(requestAnimationFrame);await new Promise(requestAnimationFrame);await new Promise(resolve=>setTimeout(resolve,0));const after=snapshot(document.activeElement===i?'yes':'no');after.set('token',token);fetch('/completed',{method:'POST',body:after})},true)</script>'''


def bounded_focus(body: bytes) -> str:
    if len(body) > 64 or body != b"focused=1":
        raise ValueError("invalid focus event")
    return "focus_event_observed=yes\n"


def parsed_result(body: bytes, completion: bool = False) -> dict[str, str]:
    if len(body) > 256:
        raise ValueError("request body too large")
    values = urllib.parse.parse_qs(body.decode("ascii", "strict"),
                                  keep_blank_values=True, strict_parsing=True)
    counters = {"starts", "updates", "ends", "compositioninputs"}
    expected = {"value", "focusbefore", "focusafter"} | counters
    if completion:
        expected.add("token")
    if (set(values) != expected
            or any(len(v) != 1 for v in values.values())):
        raise ValueError("unexpected request fields")
    return {name: item[0] for name, item in values.items()}


def bounded_result(body: bytes) -> tuple[str, dict[str, str]]:
    values = parsed_result(body)
    if values["focusbefore"] != "yes" or values["focusafter"] != "no":
        raise ValueError("password field was not focused before result submission")
    counters = {"starts", "updates", "ends", "compositioninputs"}
    numbers = {name: values[name] if values[name].isdigit() else "invalid" for name in counters}
    result = (f"ascii_value_exact={'yes' if values['value'] == 'nihao' else 'no'}\n"
            f"composition_start_count={numbers['starts']}\n"
            f"composition_update_count={numbers['updates']}\n"
            f"composition_end_count={numbers['ends']}\n"
            f"composition_input_count={numbers['compositioninputs']}\n"
            "active_element_before_result=yes\n")
    return result, values


def bounded_completion(body: bytes, token: str, initial: dict[str, str]) -> str:
    values = parsed_result(body, completion=True)
    if (not hmac.compare_digest(values.pop("token"), token)
            or values != {**initial, "focusafter": "yes"}
            or values["value"] != "nihao"
            or any(values[name] != "0" for name in ("starts", "updates", "ends", "compositioninputs"))
            or values["focusbefore"] != "yes"):
        raise ValueError("password field focus was not retained after result submission")
    return "active_element_after_result=yes\n"


def write_new(path: Path, result: str) -> None:
    flags = os.O_WRONLY | os.O_CREAT | os.O_EXCL | getattr(os, "O_NOFOLLOW", 0)
    fd = os.open(path, flags, 0o600)
    with os.fdopen(fd, "w", encoding="ascii") as stream:
        stream.write(result)


def serve(output: Path, focus_output: Path, completion_output: Path, port: int) -> None:
    session_lock = threading.Lock()
    session: dict[str, object] = {}

    class Handler(http.server.BaseHTTPRequestHandler):
        def log_message(self, *_args):
            pass

        def do_GET(self):
            if self.path != "/":
                self.send_error(404)
                return
            self.send_response(200)
            self.send_header("Content-Type", "text/html; charset=utf-8")
            self.send_header("Content-Length", str(len(PAGE)))
            self.end_headers()
            self.wfile.write(PAGE)

        def do_POST(self):
            try:
                length = int(self.headers.get("Content-Length", "-1"))
                if length < 0 or length > 256:
                    raise ValueError("invalid content length")
                body = self.rfile.read(length)
                if self.path == "/focused":
                    write_new(focus_output, bounded_focus(body))
                    response = b""
                elif self.path == "/":
                    with session_lock:
                        if session:
                            raise ValueError("password result already accepted")
                        reduced, initial = bounded_result(body)
                        token = secrets.token_hex(32)
                        write_new(output, reduced)
                        session.update(token=token, initial=initial)
                    response = token.encode("ascii")
                elif self.path == "/completed":
                    with session_lock:
                        if set(session) != {"token", "initial"}:
                            raise ValueError("password result is not initialized")
                        reduced = bounded_completion(
                            body, str(session["token"]), session["initial"])  # type: ignore[arg-type]
                        write_new(completion_output, reduced)
                        session["completed"] = True
                    response = b""
                else:
                    raise ValueError("unexpected request path")
            except (OSError, UnicodeError, ValueError):
                self.send_error(400)
                return
            self.send_response(200 if response else 204)
            if response:
                self.send_header("Content-Type", "text/plain; charset=us-ascii")
                self.send_header("Content-Length", str(len(response)))
            self.end_headers()
            if response:
                self.wfile.write(response)

    server = http.server.ThreadingHTTPServer(("127.0.0.1", port), Handler)
    server.serve_forever()


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--focus-output", type=Path, required=True)
    parser.add_argument("--completion-output", type=Path, required=True)
    parser.add_argument("--port", type=int, default=8766)
    args = parser.parse_args()
    serve(args.output, args.focus_output, args.completion_output, args.port)


if __name__ == "__main__":
    main()
