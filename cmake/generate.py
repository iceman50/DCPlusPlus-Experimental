"""Generate build-local metadata without writing into the shared source tree."""
import argparse
import os
from pathlib import Path
import sys


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("kind", choices=("revision", "help"))
    parser.add_argument("root", type=Path)
    parser.add_argument("output", type=Path)
    args = parser.parse_args()
    root = args.root.resolve()
    output = args.output.resolve()
    output.parent.mkdir(parents=True, exist_ok=True)
    sys.path[:0] = [str(root), str(root / "dcpp"), str(root / "help")]
    os.chdir(root)
    if args.kind == "revision":
        from gen_rev_id import get_rev_id
        text = '#define DCPP_REVISION "%s"\n' % get_rev_id(None)
        # A no-op build must not relink just because Git was queried again.
        if not output.exists() or output.read_text(encoding="utf-8") != text:
            output.write_text(text, encoding="utf-8")
    else:
        from gen_cshelp import gen_cshelp
        sources = sorted(path for path in (root / "help").glob("*.html")
                         if path.name not in ("changelog.html", "compile.html", "fdl.html"))
        temporary = output.with_suffix(".tmp")
        try:
            gen_cshelp([temporary], sources)
            if not output.exists() or output.read_bytes() != temporary.read_bytes():
                temporary.replace(output)
        finally:
            temporary.unlink(missing_ok=True)


if __name__ == "__main__":
    main()
