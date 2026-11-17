#!/usr/bin/env python3
import argparse
import json
import socket
import sys


def bounded_integer(label, minimum, maximum):
    def convert(value):
        number = int(value)
        if not minimum <= number <= maximum:
            raise argparse.ArgumentTypeError(f"{label} must be {minimum} through {maximum}")
        return number
    return convert


def parse_args():
    parser = argparse.ArgumentParser(description="Send one command to QUASI88's localhost input API")
    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument("--port", required=True, type=bounded_integer("port", 1, 65535))
    parser.add_argument("--timeout", type=float, default=2.0)
    parser.add_argument("--id", type=bounded_integer("id", 0, 0xFFFFFFFF), default=1)
    commands = parser.add_subparsers(dest="op", required=True)
    commands.add_parser("ping")
    tap = commands.add_parser("tap")
    tap.add_argument("key")
    tap.add_argument("--hold-ms", type=bounded_integer("hold-ms", 1, 5000), default=80)
    for name in ("keydown", "keyup"):
        command = commands.add_parser(name)
        command.add_argument("key")
    args = parser.parse_args()
    if args.timeout <= 0:
        parser.error("timeout must be positive")
    return args


def request_for(args):
    request = {"id": args.id, "op": args.op}
    if args.op != "ping":
        request["key"] = args.key
    if args.op == "tap":
        request["hold_ms"] = args.hold_ms
    return request


def main():
    args = parse_args()
    request = request_for(args)
    encoded = (json.dumps(request, separators=(",", ":")) + "\n").encode("ascii")
    try:
        with socket.create_connection((args.host, args.port), args.timeout) as connection:
            connection.settimeout(args.timeout)
            connection.sendall(encoded)
            response_line = connection.makefile("rb").readline(4097)
    except (OSError, socket.timeout) as error:
        print(f"input API connection failed: {error}", file=sys.stderr)
        return 2
    if not response_line.endswith(b"\n") or len(response_line) > 4096:
        print("input API returned no complete response", file=sys.stderr)
        return 2
    try:
        response = json.loads(response_line)
    except (UnicodeDecodeError, json.JSONDecodeError) as error:
        print(f"input API returned invalid JSON: {error}", file=sys.stderr)
        return 2
    print(response_line.decode("utf-8"), end="")
    if response.get("id") != args.id:
        print(
            f"input API response id {response.get('id')!r} does not match request id {args.id}",
            file=sys.stderr,
        )
        return 2
    return 0 if response.get("ok") is True else 1


if __name__ == "__main__":
    raise SystemExit(main())
