#!/usr/bin/env python3
"""Generate a SteamPipe ``app_build_<appid>.vdf`` for the Teeworlds Archive build.

Each ``--depot NAME=ID=PATH`` entry maps the files under ``<content-root>/PATH``
to the root of depot ``ID``.  The script is deliberately dependency-free so it
can run on any CI runner.

Example::

    scripts/ci/steam-vdf.py \
        --app-id 123456 \
        --content-root content \
        --output scripts_out/app_build_123456.vdf \
        --desc "nightly $GITHUB_SHA" \
        --set-live beta \
        --depot linux=123457=linux \
        --depot windows=123458=windows
"""

from __future__ import annotations

import argparse
import os
import sys


def vdf_escape(value: str) -> str:
    return value.replace("\\", "\\\\").replace('"', '\\"')


def write_vdf(out, indent: int, key: str, value: str) -> None:
    out.write("{}\"{}\"\t\t\"{}\"\n".format("\t" * indent, vdf_escape(key), vdf_escape(value)))


def parse_depot(spec: str):
    parts = spec.split("=", 2)
    if len(parts) != 3:
        raise argparse.ArgumentTypeError(
            "depot must be NAME=DEPOT_ID=RELATIVE_PATH, got {!r}".format(spec)
        )
    name, depot_id, rel_path = (p.strip() for p in parts)
    if not name or not depot_id or not rel_path:
        raise argparse.ArgumentTypeError("depot fields must not be empty: {!r}".format(spec))
    return name, depot_id, rel_path


def main(argv=None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--app-id", required=True, help="Steam AppID")
    parser.add_argument("--content-root", required=True, help="directory containing the depots' files")
    parser.add_argument("--output", required=True, help="path of the app_build .vdf to write")
    parser.add_argument("--depot", action="append", type=parse_depot, default=[],
                        metavar="NAME=ID=PATH",
                        help="depot to map (repeatable), PATH is relative to --content-root")
    parser.add_argument("--desc", default="", help="build description shown in the Steamworks build list")
    parser.add_argument("--build-output", default="", help="directory for SteamPipe logs/chunk cache")
    parser.add_argument("--set-live", default="", help="beta branch to set live after a successful build")
    parser.add_argument("--preview", action="store_true",
                        help="validate the mapping without uploading anything")
    args = parser.parse_args(argv)

    if not args.depot:
        parser.error("at least one --depot is required")

    content_root = os.path.abspath(args.content_root)
    if not os.path.isdir(content_root):
        parser.error("content root does not exist: {}".format(content_root))

    for name, _depot_id, rel_path in args.depot:
        depot_dir = os.path.join(content_root, rel_path)
        if not os.path.isdir(depot_dir):
            parser.error("depot '{}' directory does not exist: {}".format(name, depot_dir))

    build_output = os.path.abspath(args.build_output) if args.build_output else os.path.join(
        os.path.dirname(os.path.abspath(args.output)), "steam_output"
    )
    os.makedirs(os.path.dirname(os.path.abspath(args.output)), exist_ok=True)
    os.makedirs(build_output, exist_ok=True)

    with open(args.output, "w", encoding="utf-8", newline="\n") as out:
        out.write('"AppBuild"\n{\n')
        write_vdf(out, 1, "AppID", args.app_id)
        if args.desc:
            write_vdf(out, 1, "Desc", args.desc)
        write_vdf(out, 1, "ContentRoot", content_root)
        write_vdf(out, 1, "BuildOutput", build_output)
        if args.preview:
            write_vdf(out, 1, "Preview", "1")
        if args.set_live:
            # Steam only allows setting *beta* branches live automatically.
            write_vdf(out, 1, "SetLive", args.set_live)

        out.write('\t"Depots"\n\t{\n')
        for _name, depot_id, rel_path in args.depot:
            out.write('\t\t"{}"\n\t\t{{\n'.format(vdf_escape(depot_id)))
            out.write('\t\t\t"FileMapping"\n\t\t\t{\n')
            write_vdf(out, 4, "LocalPath", "{}/{}".format(rel_path, "*"))
            write_vdf(out, 4, "DepotPath", ".")
            write_vdf(out, 4, "recursive", "1")
            out.write("\t\t\t}\n")
            out.write("\t\t}\n")
        out.write("\t}\n")
        out.write("}\n")

    print("wrote {}".format(args.output))
    for name, depot_id, rel_path in args.depot:
        print("  depot {} (id {}) <- {}".format(name, depot_id, os.path.join(content_root, rel_path)))
    if args.set_live:
        print("  will set branch '{}' live on success".format(args.set_live))
    return 0


if __name__ == "__main__":
    sys.exit(main())
