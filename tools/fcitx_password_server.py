#!/usr/bin/env python3
"""Serve one synthetic password field and retain only boolean test results."""
import argparse
import http.server
import os
import urllib.parse
from pathlib import Path

PAGE = b'''<!doctype html><meta charset="utf-8"><style>html,body,form{width:100%;height:100%;margin:0}form{position:relative;background:#01dead}input{position:absolute;left:15%;top:calc(50% - 70px);width:70%;height:140px;font-size:48px}input:focus{outline:12px solid #1234ef}</style><form autocomplete="off"><input type="password" name="value" autocomplete="new-password" aria-label="Synthetic password test"><input type="hidden" name="starts" value="0"><input type="hidden" name="updates" value="0"><input type="hidden" name="ends" value="0"><input type="hidden" name="compositioninputs" value="0"><input type="hidden" name="focusbefore" value="no"><input type="hidden" name="focusafter" value="no"><button hidden>Submit</button></form><script>let s=0,u=0,e=0,c=0;const i=document.querySelector('input[type=password]'),f=document.querySelector('form');i.addEventListener('focus',()=>fetch('/focused',{method:'POST',body:'focused=1'}));i.addEventListener('compositionstart',()=>s++);i.addEventListener('compositionupdate',()=>u++);i.addEventListener('compositionend',()=>e++);i.addEventListener('beforeinput',x=>{if(x.isComposing||String(x.inputType).includes('Composition'))c++});i.addEventListener('input',x=>{if(x.isComposing||String(x.inputType).includes('Composition'))c++});f.addEventListener('submit',async x=>{x.preventDefault();for(const [n,v] of [['starts',s],['updates',u],['ends',e],['compositioninputs',c],['focusbefore',document.activeElement===i?'yes':'no']])document.querySelector('[name='+n+']').value=v;const r=await fetch('/',{method:'POST',body:new URLSearchParams(new FormData(f))});if(!r.ok)return;document.querySelector('[name=focusafter]').value=document.activeElement===i?'yes':'no';fetch('/completed',{method:'POST',body:new URLSearchParams(new FormData(f))})})</script>'''


def bounded_focus(body: bytes) -> str:
    if len(body) > 64 or body != b"focused=1":
        raise ValueError("invalid focus event")
    return "focus_event_observed=yes\n"


def bounded_result(body: bytes) -> str:
    if len(body) > 256:
        raise ValueError("request body too large")
    values = urllib.parse.parse_qs(body.decode("ascii", "strict"),
                                  keep_blank_values=True, strict_parsing=True)
    counters = {"starts", "updates", "ends", "compositioninputs"}
    if (set(values) != {"value", "focusbefore", "focusafter"} | counters
            or any(len(v) != 1 for v in values.values())):
        raise ValueError("unexpected request fields")
    if values["focusbefore"] != ["yes"] or values["focusafter"] != ["no"]:
        raise ValueError("password field was not focused before result submission")
    numbers = {name: values[name][0] if values[name][0].isdigit() else "invalid" for name in counters}
    return (f"ascii_value_exact={'yes' if values['value'][0] == 'nihao' else 'no'}\n"
            f"composition_start_count={numbers['starts']}\n"
            f"composition_update_count={numbers['updates']}\n"
            f"composition_end_count={numbers['ends']}\n"
            f"composition_input_count={numbers['compositioninputs']}\n"
            "active_element_before_result=yes\n")


def bounded_completion(body: bytes) -> str:
    if len(body) > 256:
        raise ValueError("request body too large")
    values = urllib.parse.parse_qs(body.decode("ascii", "strict"),
                                  keep_blank_values=True, strict_parsing=True)
    if (set(values) != {"value", "starts", "updates", "ends", "compositioninputs",
                        "focusbefore", "focusafter"}
            or any(len(v) != 1 for v in values.values())
            or values["focusbefore"] != ["yes"] or values["focusafter"] != ["yes"]):
        raise ValueError("password field focus was not retained after result submission")
    return "active_element_after_result=yes\n"


def write_new(path: Path, result: str) -> None:
    flags = os.O_WRONLY | os.O_CREAT | os.O_EXCL | getattr(os, "O_NOFOLLOW", 0)
    fd = os.open(path, flags, 0o600)
    with os.fdopen(fd, "w", encoding="ascii") as stream:
        stream.write(result)


def serve(output: Path, focus_output: Path, completion_output: Path, port: int) -> None:
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
                elif self.path == "/":
                    write_new(output, bounded_result(body))
                elif self.path == "/completed":
                    write_new(completion_output, bounded_completion(body))
                else:
                    raise ValueError("unexpected request path")
            except (OSError, UnicodeError, ValueError):
                self.send_error(400)
                return
            self.send_response(204)
            self.end_headers()

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
