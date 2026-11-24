#!/usr/bin/env python3
"""Apply the DXVK Native 1.9.2 vkCreateDevice VkResult diagnostic.

This edits the pinned upstream source directly instead of relying on git apply.
It is intentionally strict: unexpected source layouts stop the build immediately.
"""
from pathlib import Path
import sys

if len(sys.argv) != 2:
    raise SystemExit("usage: apply-dxvk-device-result.py <dxvk-source-root>")

source = Path(sys.argv[1]) / "src" / "dxvk" / "dxvk_adapter.cpp"
if not source.is_file():
    raise SystemExit(f"ERROR: DXVK source file not found: {source}")

text = source.read_text(encoding="utf-8")

diagnostic = 'DxvkAdapter: vkCreateDevice failed with VkResult'
if diagnostic in text:
    print("DXVK vkCreateDevice VkResult diagnostic already present")
    raise SystemExit(0)

old = '''    if (vr != VK_SUCCESS)
      throw DxvkError("DxvkAdapter: Failed to create device");'''

new = '''    if (vr != VK_SUCCESS) {
      Logger::err(str::format("DxvkAdapter: vkCreateDevice failed with VkResult ",
        static_cast<int32_t>(vr)));
      throw DxvkError("DxvkAdapter: Failed to create device");
    }'''

count = text.count(old)
if count != 1:
    raise SystemExit(
        "ERROR: expected exactly one original vkCreateDevice failure block, "
        f"found {count}; refusing to modify {source}"
    )

updated = text.replace(old, new, 1)
if diagnostic not in updated or updated.count(diagnostic) != 1:
    raise SystemExit("ERROR: diagnostic verification failed; source was not written")

source.write_text(updated, encoding="utf-8")
verified = source.read_text(encoding="utf-8")
if diagnostic not in verified:
    raise SystemExit("ERROR: diagnostic missing after write")

print(f"Applied and verified vkCreateDevice VkResult diagnostic in {source}")
