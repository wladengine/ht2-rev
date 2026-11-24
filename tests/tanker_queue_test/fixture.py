"""Package or verify local fuel_test profiles and configuration against the paired drive."""

import argparse
import hashlib
import os
from pathlib import Path
import tempfile
import zipfile

ROOT = Path(__file__).resolve().parents[2]
FIXTURE = ROOT / ".local/fixtures/tanker/fuel_test-tree-inputs.zip"
CONFIGS = ("TRUCK.INI", "d2gi.ini", "fpslimiter.ini", "kmjin.ini")
PROFILES = ("approach-364.035.pl1", "approach-368.52.pl1")


EXPECTED = {
    "approach-364.035.pl1": "38ea9458e407d9baa6197dc0b7a59d4a04953a3c9f7b413c14727daea750593d",
    "approach-368.52.pl1": "8131b4ec218ea76a95a00015cafaff7ff1986fc305f5b9cb9cc5049c6e882a9b",
    "TRUCK.INI": "05e31b7ddd3d443f5ee8c879f53d3d36847085884b8526725cf95ecdff3af7ee",
    "d2gi.ini": "9f04a2c2b3a26b850bde09f83e787d0224b7607be9e946785137acd5f0182c9c",
    "fpslimiter.ini": "35bbf00ddcf728a4c1dd889dfdb128d8162beefca6418d52da5a6fa8004fdcaa",
    "kmjin.ini": "350b0f39c7a88cc4d57f07e27fb9c698dfac587ca42489c6be777d7b2de1ff05"
}


def verify(archive):
    expected = EXPECTED
    if sorted(archive.namelist()) != sorted(expected):
        raise ValueError("Wrong fixture members")
    for name, sha in expected.items():
        with archive.open(name) as stream:
            if hashlib.file_digest(stream, "sha256").hexdigest() != sha:
                raise ValueError(f"Wrong fixture input: {name}")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--profiles", type=Path)
    parser.add_argument("--config", type=Path)
    parser.add_argument("--archive", type=Path, default=FIXTURE)
    args = parser.parse_args()
    if bool(args.profiles) != bool(args.config):
        parser.error("Pass --profiles and --config together to package inputs")
    if args.profiles:
        args.archive.parent.mkdir(parents=True, exist_ok=True)
        with tempfile.TemporaryDirectory(dir=args.archive.parent) as tmp:
            staged = Path(tmp) / "inputs.zip"
            with zipfile.ZipFile(staged, "w") as archive:
                for name in EXPECTED:
                    directory = args.profiles if name in PROFILES else args.config
                    archive.writestr(
                        zipfile.ZipInfo(name),
                        (directory / name).read_bytes(),
                        zipfile.ZIP_DEFLATED,
                    )
            with zipfile.ZipFile(staged) as archive:
                verify(archive)
            if not args.archive.exists():
                # Same filesystem. Publish only a fully verified, closed archive.
                if os.name == "nt":
                    staged.rename(args.archive)
                else:
                    os.link(staged, args.archive)
    with zipfile.ZipFile(args.archive) as archive:
        verify(archive)
    print(
        args.archive, "verified", hashlib.sha256(args.archive.read_bytes()).hexdigest()
    )


if __name__ == "__main__":
    main()
