import codecs
for f in [r"nav2\ros2_shim\build.py", r"tools\_cores.py", r"bridge\scripts\build_bridge.py", r"tools\run_ctest.py"]:
    raw = open(f, "rb").read()
    ok = True
    try:
        raw.decode("utf-8")
    except UnicodeDecodeError as e:
        ok = False
        err = str(e)
    # find the first non-ascii byte and show the neighbourhood as repr
    i = next((k for k, b in enumerate(raw) if b >= 0x80), None)
    print("%-38s utf8=%s  first_nonascii@%s  %r" % (f, ok, i, raw[max(0, (i or 0) - 8):(i or 0) + 26]))
    for enc in ("gbk", "utf-8"):
        try:
            raw.decode(enc)
            print("     decodes as", enc)
        except UnicodeDecodeError:
            print("     NOT", enc)